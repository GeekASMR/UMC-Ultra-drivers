#include <initguid.h>
#include "BehringerASIO.h"
#include "../utils/Logger.h"
#include <cstring>
#include <cstdio>
#include <tchar.h>

// Our Virtual Driver CLSID
DEFINE_GUID(CLSID_BehringerASIO,
    0xa1b2c3d4, 0xe5f6, 0x7890,
    0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90);

// The original Behringer UMC ASIO driver's CLSID (as found in registry)
DEFINE_GUID(CLSID_UmcAudioAsio, 
    0x0351302F, 0xB1F1, 0x4A5D, 
    0x86, 0x13, 0x78, 0x7F, 0x77, 0xC2, 0x0E, 0xA4);

#define LOG_MODULE "NativeASIOProxy"

BehringerASIO* BehringerASIO::s_instance = nullptr;

BehringerASIO::BehringerASIO(LPUNKNOWN pUnk, HRESULT* phr)
    : m_refCount(1), m_baseAsio(nullptr), m_dawCallbacks(nullptr),
      m_numVirtualInputs(0), m_numVirtualOutputs(0), m_bufferSize(0), m_sampleRate(48000.0),
      m_hwNumInputs(0), m_hwNumOutputs(0)
{
    s_instance = this;
    try {
        Logger::getInstance().init();
        LOG_INFO(LOG_MODULE, "Native ASIO Proxy starting...");
    } catch (...) {}

    memset(&m_ourCallbacks, 0, sizeof(m_ourCallbacks));
    
    // Create base ASIO driver using CLSID as IID (ASIO standard convention)
    HRESULT hr = CoCreateInstance(
        CLSID_UmcAudioAsio, 
        nullptr, 
        CLSCTX_INPROC_SERVER, 
        CLSID_UmcAudioAsio, // ASIO conventionally uses CLSID for the IID
        (void**)&m_baseAsio
    );

    // Fallback: load DLL manually if CoCreateInstance fails because of no registry
    if (FAILED(hr) || !m_baseAsio) {
        LOG_WARN(LOG_MODULE, "CoCreateInstance failed. Attempting DLL load...");
        HMODULE hMod = LoadLibraryA("C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
        if (hMod) {
            typedef HRESULT(STDAPICALLTYPE *DllGetClassObject_t)(REFCLSID, REFIID, LPVOID*);
            auto fGetClass = (DllGetClassObject_t)GetProcAddress(hMod, "DllGetClassObject");
            if (fGetClass) {
                IClassFactory* pCF = nullptr;
                if (SUCCEEDED(fGetClass(CLSID_UmcAudioAsio, IID_IClassFactory, (void**)&pCF)) && pCF) {
                    pCF->CreateInstance(nullptr, CLSID_UmcAudioAsio, (void**)&m_baseAsio);
                    pCF->Release();
                }
            }
        }
    }

    if (m_baseAsio) {
        LOG_INFO(LOG_MODULE, "Successfully loaded base ASIO driver");
        if (phr) *phr = S_OK;
    } else {
        LOG_ERROR(LOG_MODULE, "CRITICAL ERROR: Could not load base ASIO driver");
        if (phr) *phr = E_FAIL;
    }
}

BehringerASIO::~BehringerASIO() {
    stop();
    disposeBuffers();
    if (m_baseAsio) {
        m_baseAsio->Release();
        m_baseAsio = nullptr;
    }
    s_instance = nullptr;
    LOG_INFO(LOG_MODULE, "Proxy Destroyed");
}

HRESULT BehringerASIO::CreateInstance(LPUNKNOWN pUnk, REFIID riid, void** ppv) {
    HRESULT hr = S_OK;
    auto* drv = new BehringerASIO(pUnk, &hr);
    if (FAILED(hr)) { delete drv; return hr; }
    hr = drv->QueryInterface(riid, ppv);
    drv->Release();
    return hr;
}

STDMETHODIMP BehringerASIO::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == CLSID_BehringerASIO) {
        *ppv = static_cast<IASIO*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}
STDMETHODIMP_(ULONG) BehringerASIO::AddRef()  { return InterlockedIncrement(&m_refCount); }
STDMETHODIMP_(ULONG) BehringerASIO::Release() {
    LONG r = InterlockedDecrement(&m_refCount);
    if (r == 0) delete this;
    return r;
}

ASIOBool BehringerASIO::init(void* sysHandle) {
    if (!m_baseAsio) return ASIOFalse;
    
    ASIOBool ret = m_baseAsio->init(sysHandle);
    if (!ret) return ASIOFalse;

    // Determine hardware channel counts
    long inCount = 0, outCount = 0;
    m_baseAsio->getChannels(&inCount, &outCount);
    m_hwNumInputs = inCount;
    m_hwNumOutputs = outCount;

    // Open virtual channel IPC (ASMRTOP WDM shared memory)
    for (int p = 0; p < NUM_VIRTUAL_PAIRS; p++) {
        m_playIpc[p].open("PLAY", p);
        m_capIpc[p].open("REC", p);
    }
    
    // 核心修复：就算底层通道暂时没准备好（或者没被创建），
    // 也要无条件把所有虚拟通道暴露给宿主！
    // 只有这样，宿主才会永远保留这 8 个虚拟输入/输出接口，
    // 后续底层 WDM 连接后才能动态热插拔自动流入流出声音！
    m_numVirtualInputs = NUM_VIRTUAL_PAIRS * 2;
    m_numVirtualOutputs = NUM_VIRTUAL_PAIRS * 2;
    
    LOG_INFO(LOG_MODULE, "Proxy Init: HW In=%d, HW Out=%d, VRT In=%d, VRT Out=%d", 
             m_hwNumInputs, m_hwNumOutputs, m_numVirtualInputs, m_numVirtualOutputs);

    return ret;
}

void BehringerASIO::getDriverName(char* name) {
    strcpy(name, "UMC ASIO VRT-Proxy");
}

long BehringerASIO::getDriverVersion() {
    return 6;
}

void BehringerASIO::getErrorMessage(char* string) {
    if (m_baseAsio) m_baseAsio->getErrorMessage(string);
}

ASIOError BehringerASIO::start() {
    if (!m_baseAsio) return ASE_NotPresent;
    return m_baseAsio->start();
}

ASIOError BehringerASIO::stop() {
    if (!m_baseAsio) return ASE_NotPresent;
    return m_baseAsio->stop();
}

ASIOError BehringerASIO::getChannels(long* numInputChannels, long* numOutputChannels) {
    if (!m_baseAsio) return ASE_NotPresent;
    long hwIn = 0, hwOut = 0;
    ASIOError err = m_baseAsio->getChannels(&hwIn, &hwOut);
    if (err != ASE_OK) return err;

    m_hwNumInputs = hwIn;
    m_hwNumOutputs = hwOut;
    
    if (numInputChannels) *numInputChannels = hwIn + m_numVirtualInputs;
    if (numOutputChannels) *numOutputChannels = hwOut + m_numVirtualOutputs;
    return ASE_OK;
}

ASIOError BehringerASIO::getLatencies(long* inputLatency, long* outputLatency) {
    if (!m_baseAsio) return ASE_NotPresent;
    return m_baseAsio->getLatencies(inputLatency, outputLatency);
}

ASIOError BehringerASIO::getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) {
    if (!m_baseAsio) return ASE_NotPresent;
    return m_baseAsio->getBufferSize(minSize, maxSize, preferredSize, granularity);
}

ASIOError BehringerASIO::canSampleRate(ASIOSampleRate sampleRate) {
    if (!m_baseAsio) return ASE_NotPresent;
    return m_baseAsio->canSampleRate(sampleRate);
}

ASIOError BehringerASIO::getSampleRate(ASIOSampleRate* sampleRate) {
    if (!m_baseAsio) return ASE_NotPresent;
    auto err = m_baseAsio->getSampleRate(sampleRate);
    if (err == ASE_OK) m_sampleRate = *sampleRate;
    return err;
}

ASIOError BehringerASIO::setSampleRate(ASIOSampleRate sampleRate) {
    if (!m_baseAsio) return ASE_NotPresent;
    auto err = m_baseAsio->setSampleRate(sampleRate);
    if (err == ASE_OK) m_sampleRate = sampleRate;
    return err;
}

ASIOError BehringerASIO::getClockSources(ASIOClockSource* clocks, long* numSources) {
    if (!m_baseAsio) return ASE_NotPresent;
    return m_baseAsio->getClockSources(clocks, numSources);
}

ASIOError BehringerASIO::setClockSource(long reference) {
    if (!m_baseAsio) return ASE_NotPresent;
    return m_baseAsio->setClockSource(reference);
}

ASIOError BehringerASIO::getSamplePosition(long long* sPos, long long* tStamp) {
    if (!m_baseAsio) return ASE_NotPresent;
    return m_baseAsio->getSamplePosition(sPos, tStamp);
}

ASIOError BehringerASIO::getChannelInfo(ASIOChannelInfo* info) {
    if (!m_baseAsio) return ASE_NotPresent;

    bool isVrt = false;
    int vIdx = 0;

    if (info->isInput) {
        if (info->channel >= m_hwNumInputs) {
            isVrt = true;
            vIdx = info->channel - m_hwNumInputs;
        }
    } else {
        if (info->channel >= m_hwNumOutputs) {
            isVrt = true;
            vIdx = info->channel - m_hwNumOutputs;
        }
    }

    if (!isVrt) {
        return m_baseAsio->getChannelInfo(info);
    }

    info->isActive = ASIOTrue;
    info->channelGroup = 0;
    info->type = ASIOSTFloat32LSB; // Virtual natively supports flawless 32-bit floats!

    if (info->isInput) {
        snprintf(info->name, 32, "VRT IN %d", vIdx + 1);
    } else {
        snprintf(info->name, 32, "VRT OUT %d", vIdx + 1);
    }
    return ASE_OK;
}

void BehringerASIO::bufferSwitchProxy(long bufferIndex, ASIOBool directProcess) {
    if (s_instance) s_instance->onBufferSwitch_Read(bufferIndex);
    if (s_instance && s_instance->m_dawCallbacks && s_instance->m_dawCallbacks->bufferSwitch) {
        s_instance->m_dawCallbacks->bufferSwitch(bufferIndex, directProcess);
    }
    if (s_instance) s_instance->onBufferSwitch_Write(bufferIndex);
}
void BehringerASIO::sampleRateDidChangeProxy(ASIOSampleRate sRate) {
    if (s_instance) s_instance->m_sampleRate = sRate;
    if (s_instance && s_instance->m_dawCallbacks && s_instance->m_dawCallbacks->sampleRateDidChange) {
        s_instance->m_dawCallbacks->sampleRateDidChange(sRate);
    }
}
long BehringerASIO::asioMessageProxy(long selector, long value, void* message, double* opt) {
    if (s_instance && s_instance->m_dawCallbacks && s_instance->m_dawCallbacks->asioMessage) {
        return s_instance->m_dawCallbacks->asioMessage(selector, value, message, opt);
    }
    return 0;
}
ASIOTime* BehringerASIO::bufferSwitchTimeInfoProxy(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess) {
    if (s_instance) s_instance->onBufferSwitch_Read(doubleBufferIndex);
    ASIOTime* res = nullptr;
    if (s_instance && s_instance->m_dawCallbacks && s_instance->m_dawCallbacks->bufferSwitchTimeInfo) {
        res = s_instance->m_dawCallbacks->bufferSwitchTimeInfo(params, doubleBufferIndex, directProcess);
    }
    if (s_instance) s_instance->onBufferSwitch_Write(doubleBufferIndex);
    return res;
}

ASIOError BehringerASIO::createBuffers(ASIOBufferInfo* bufferInfos, long numChannels,
                                        long bufferSize, ASIOCallbacks* callbacks) {
    if (!m_baseAsio) return ASE_NotPresent;

    m_dawCallbacks = callbacks;
    m_bufferSize = bufferSize;

    // Filter out virtual channels, build request for base ASIO
    m_bufferMap.clear();
    std::vector<ASIOBufferInfo> baseBufferInfos;

    for (long i = 0; i < numChannels; i++) {
        ProxyBuffer pb;
        pb.wrapper = bufferInfos[i];
        
        bool isVrt = false;
        if (pb.wrapper.isInput && pb.wrapper.channelNum >= m_hwNumInputs) isVrt = true;
        if (!pb.wrapper.isInput && pb.wrapper.channelNum >= m_hwNumOutputs) isVrt = true;

        if (!isVrt) {
            baseBufferInfos.push_back(pb.wrapper);
            pb.original = baseBufferInfos.back(); // keep track for map
            m_bufferMap.push_back(pb);
        } else {
            // Virtual channel: allocate raw float buffers
            pb.original.isInput = pb.wrapper.isInput;
            pb.original.channelNum = -1; // virtual flag
            pb.original.buffers[0] = new float[bufferSize];
            pb.original.buffers[1] = new float[bufferSize];
            memset(pb.original.buffers[0], 0, bufferSize * sizeof(float));
            memset(pb.original.buffers[1], 0, bufferSize * sizeof(float));
            m_rawVrtBufs.push_back((float*)pb.original.buffers[0]);
            m_rawVrtBufs.push_back((float*)pb.original.buffers[1]);
            m_bufferMap.push_back(pb);
        }
    }

    m_ourCallbacks.bufferSwitch = bufferSwitchProxy;
    m_ourCallbacks.sampleRateDidChange = sampleRateDidChangeProxy;
    m_ourCallbacks.asioMessage = asioMessageProxy;
    m_ourCallbacks.bufferSwitchTimeInfo = bufferSwitchTimeInfoProxy;

    ASIOError err = m_baseAsio->createBuffers(baseBufferInfos.data(), (long)baseBufferInfos.size(), bufferSize, &m_ourCallbacks);
    if (err != ASE_OK) return err;

    // Map the returned buffer pointers back to the DAW
    int baseIdx = 0;
    for (size_t i = 0; i < m_bufferMap.size(); i++) {
        if (m_bufferMap[i].original.channelNum == -1) {
            // Virtual
            bufferInfos[i].buffers[0] = m_bufferMap[i].original.buffers[0];
            bufferInfos[i].buffers[1] = m_bufferMap[i].original.buffers[1];
        } else {
            // Hardware
            bufferInfos[i].buffers[0] = baseBufferInfos[baseIdx].buffers[0];
            bufferInfos[i].buffers[1] = baseBufferInfos[baseIdx].buffers[1];
            m_bufferMap[i].original = baseBufferInfos[baseIdx]; // sync pointers
            baseIdx++;
        }
    }

    return ASE_OK;
}

ASIOError BehringerASIO::disposeBuffers() {
    if (!m_baseAsio) return ASE_NotPresent;

    for (auto* p : m_rawVrtBufs) {
        delete[] p;
    }
    m_rawVrtBufs.clear();
    m_bufferMap.clear();

    return m_baseAsio->disposeBuffers();
}

ASIOError BehringerASIO::controlPanel() {
    if (!m_baseAsio) return ASE_NotPresent;
    return m_baseAsio->controlPanel();
}

ASIOError BehringerASIO::future(long selector, void* opt) {
    if (!m_baseAsio) return ASE_NotPresent;
    return m_baseAsio->future(selector, opt);
}

ASIOError BehringerASIO::outputReady() {
    if (!m_baseAsio) return ASE_NotPresent;
    return m_baseAsio->outputReady();
}

void BehringerASIO::onBufferSwitch_Read(long bufferIndex) {
    if (!m_baseAsio) return;

    // Read virtual inputs (system audio going into DAW VRT IN)
    for (int pair = 0; pair < NUM_VIRTUAL_PAIRS; pair++) {

        float* vrtL = nullptr;
        float* vrtR = nullptr;

        int vInIdx = 0;
        for (auto& pb : m_bufferMap) {
            if (pb.original.channelNum == -1 && pb.original.isInput) {
                if (vInIdx == pair * 2) vrtL = (float*)pb.original.buffers[bufferIndex];
                if (vInIdx == pair * 2 + 1) vrtR = (float*)pb.original.buffers[bufferIndex];
                vInIdx++;
            }
        }
        
        if (vrtL || vrtR) {
            m_playIpc[pair].readStereoAdaptive(vrtL, vrtR, m_bufferSize, 48000.0, m_sampleRate);
        }
    }
}

void BehringerASIO::onBufferSwitch_Write(long bufferIndex) {
    if (!m_baseAsio) return;

    // Write virtual outputs (DAW VRT OUT going into system MIC)
    for (int pair = 0; pair < NUM_VIRTUAL_PAIRS; pair++) {

        float* vrtL = nullptr;
        float* vrtR = nullptr;

        int vOutIdx = 0;
        for (auto& pb : m_bufferMap) {
            if (pb.original.channelNum == -1 && !pb.original.isInput) {
                if (vOutIdx == pair * 2) vrtL = (float*)pb.original.buffers[bufferIndex];
                if (vOutIdx == pair * 2 + 1) vrtR = (float*)pb.original.buffers[bufferIndex];
                vOutIdx++;
            }
        }
        
        if (vrtL || vrtR) {
            m_capIpc[pair].writeStereoSRC(vrtL, vrtR, m_bufferSize, m_sampleRate, 48000.0);
        }
    }
}
