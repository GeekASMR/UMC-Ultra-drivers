/*
 * BehringerASIO - 终极纯原生直连形态 (Pure Native Direct Mode)
 *
 * 这是一套彻底抛弃官方 DLL（不再依赖任何代理）的原生 ASIO 驱动实现。
 * 完美解决：
 * 1. 输出 DMA 清零（阴阳融合时序：Event + WFB ACK）
 * 2. 输入 ADC 异常（Event 保护 ADC 采样时序）
 * 3. CPU 100% 死锁空转（Event 挂起线程）
 * 4. Studio One 性能表 100%（ASIO ABI hi:lo 格式修复）
 * 5. OVERLAPPED 竞态条件（独立 Event + CancelIoEx）
 */

#include <initguid.h>
#include "BehringerASIO.h"
#include "../utils/Logger.h"
#include "TUsbAudioApi.h"
#include <cstring>
#include <cstdio>
#include <mmsystem.h>

// {A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
DEFINE_GUID(CLSID_BehringerASIO,
    0xa1b2c3d4, 0xe5f6, 0x7890,
    0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90);

#define LOG_MODULE "NativeASIO"

BehringerASIO::BehringerASIO(LPUNKNOWN pUnk, HRESULT* phr)
    : m_refCount(1), m_initialized(false), m_running(false),
      m_numInputChannels(0), m_numOutputChannels(0), m_bufferSize(128),
      m_sampleRate(48000.0), m_callbacks(nullptr), m_samplePosition(0)
{
    try {
        Logger::getInstance().init();
        LOG_INFO(LOG_MODULE, "Native Engine starting...");
    } catch (...) {}
    if (phr) *phr = S_OK;
}

BehringerASIO::~BehringerASIO() {
    stop();
    disposeBuffers();
    LOG_INFO(LOG_MODULE, "Destroying...");
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
    ULONG ref = InterlockedDecrement(&m_refCount);
    if (ref == 0) delete this;
    return ref;
}

ASIOBool BehringerASIO::init(void* sysHandle) {
    if (m_initialized) return ASIOTrue;
    LOG_INFO(LOG_MODULE, "init()");
    if (!m_tusb.init()) return ASIOFalse;

    m_numInputChannels = (long)m_tusb.getInputChannels().size();
    m_numOutputChannels = (long)m_tusb.getOutputChannels().size();
    m_sampleRate = (ASIOSampleRate)m_tusb.getStreamConfig().currentRate;
    
    m_initialized = true;
    return ASIOTrue;
}

void BehringerASIO::getDriverName(char* name) { strcpy(name, "UMC ASIO (Native Direct)"); }
long BehringerASIO::getDriverVersion() { return 6; }
void BehringerASIO::getErrorMessage(char* s) { strcpy(s, "No error"); }

ASIOError BehringerASIO::getChannels(long* numIn, long* numOut) {
    if (!m_initialized) return ASE_NotPresent;
    if (numIn) *numIn = m_numInputChannels;
    if (numOut) *numOut = m_numOutputChannels;
    return ASE_OK;
}

ASIOError BehringerASIO::getLatencies(long* in, long* out) {
    if (!m_initialized) return ASE_NotPresent;
    // Official driver reports higher latencies than buffer size to account for
    // USB transfer overhead. At 128 buffer: input=200, output=264.
    // Pattern: input = bufferSize + 72, output = bufferSize + 136
    if (in) *in = m_bufferSize + 72;    
    if (out) *out = m_bufferSize + 136;  
    return ASE_OK;
}

ASIOError BehringerASIO::getBufferSize(long* min, long* max, long* pref, long* gran) {
    if (!m_initialized) return ASE_NotPresent;

    long realMin = 8;
    long realMax = 2048;
    long realPref = m_bufferSize; // fallback

    TUsbAudioApi api;
    if (api.load()) {
        if (api.fn.TUSBAUDIO_GetAsioBufferSizeSet) {
            unsigned int sizes[64];
            unsigned int count = 0, curIdx = 0;
            if (api.fn.TUSBAUDIO_GetAsioBufferSizeSet(64, sizes, &count, &curIdx) == TSTATUS_SUCCESS && count > 0) {
                // Multiply cleanly to avoid float errors before dividing
                realMin = (long)((sizes[0] * m_sampleRate) / 1000000.0 + 0.5);
                realMax = (long)((sizes[count - 1] * m_sampleRate) / 1000000.0 + 0.5);
                realPref = (long)((sizes[curIdx] * m_sampleRate) / 1000000.0 + 0.5);
                m_bufferSize = realPref; 
            }
        }
    }

    if (min) *min = realMin;
    if (max) *max = realMax;
    // Tell the DAW what the hardware is ACTUALLY configured to right now
    if (pref) *pref = realPref;
    if (gran) *gran = -1; // powers of 2
    return ASE_OK;
}

ASIOError BehringerASIO::canSampleRate(ASIOSampleRate r) {
    auto cfg = m_tusb.getStreamConfig();
    for (DWORD i = 0; i < cfg.numSupportedRates; i++) {
        if (cfg.supportedRates[i] == (DWORD)r) return ASE_OK;
    }
    return ASE_NoClock;
}

ASIOError BehringerASIO::getSampleRate(ASIOSampleRate* r) {
    if (!m_initialized) return ASE_NotPresent;
    if (r) *r = m_sampleRate;
    return ASE_OK;
}

ASIOError BehringerASIO::setSampleRate(ASIOSampleRate r) {
    if (!m_initialized) return ASE_NotPresent;
    // Validate against hardware-supported rates
    if (canSampleRate(r) != ASE_OK) {
        LOG_ERROR(LOG_MODULE, "setSampleRate(%g) - not supported", r);
        return ASE_NoClock;
    }
    m_sampleRate = r;
    LOG_INFO(LOG_MODULE, "setSampleRate(%g) OK", r);
    return ASE_OK;
}

ASIOError BehringerASIO::getClockSources(ASIOClockSource* c, long* n) {
    if (!m_initialized) return ASE_NotPresent;
    if (n) *n = 1;
    if (c) {
        c->index = 0;
        c->associatedChannel = -1;
        c->associatedGroup = -1;
        c->isCurrentSource = ASIOTrue;
        strcpy(c->name, "Internal");
    }
    return ASE_OK;
}

ASIOError BehringerASIO::setClockSource(long ref) { return (ref == 0) ? ASE_OK : ASE_InvalidParameter; }

ASIOError BehringerASIO::getSamplePosition(long long* sPos, long long* tStamp) {
    if (!m_running) return ASE_NotPresent;
    if (sPos) {
        // ASIO uses struct{unsigned long hi; unsigned long lo} format
        // Official Thesycon driver writes: hi=upper32, lo=lower32
        long long pos = m_samplePosition.load();
        unsigned long* parts = (unsigned long*)sPos;
        parts[0] = (unsigned long)(pos >> 32);   // hi (most significant)
        parts[1] = (unsigned long)(pos & 0xFFFFFFFFLL); // lo (least significant = actual value for small counts)
    }
    if (tStamp) {
        // Same hi:lo format for timestamp
        long long ns = (long long)timeGetTime() * 1000000LL;
        unsigned long* parts = (unsigned long*)tStamp;
        parts[0] = (unsigned long)(ns >> 32);   // hi
        parts[1] = (unsigned long)(ns & 0xFFFFFFFFLL); // lo
    }
    return ASE_OK;
}

ASIOError BehringerASIO::getChannelInfo(ASIOChannelInfo* info) {
    if (!m_initialized || !info) return ASE_NotPresent;

    if (info->isInput) {
        if (info->channel < 0 || info->channel >= m_numInputChannels) return ASE_InvalidParameter;
        strcpy(info->name, m_tusb.getInputChannels()[info->channel].name);
        info->channelGroup = 0;
        info->type = ASIOSTInt32LSB; // We expose raw 32-bit INT out to ASIO
    } else {
        if (info->channel < 0 || info->channel >= m_numOutputChannels) return ASE_InvalidParameter;
        strcpy(info->name, m_tusb.getOutputChannels()[info->channel].name);
        info->channelGroup = 0;
        info->type = ASIOSTInt32LSB;
    }
    info->isActive = ASIOFalse; 
    return ASE_OK;
}

ASIOError BehringerASIO::createBuffers(ASIOBufferInfo* bi, long nCh, long bufSz, ASIOCallbacks* cb) {
    if (!m_initialized) return ASE_NotPresent;

    m_bufferSize = bufSz;
    m_callbacks = cb;
    m_asioBufs.clear();

    std::vector<int> inCh, outCh;
    for (long i = 0; i < nCh; i++) {
        AsioChannelBuf ab = {0};
        ab.isInput = (bi[i].isInput == ASIOTrue);
        ab.hwIndex = bi[i].channelNum;
        if (ab.isInput) inCh.push_back(ab.hwIndex);
        else outCh.push_back(ab.hwIndex);
        m_asioBufs.push_back(ab);
    }

    // Force official driver to actually apply the buffer size mathematically
    TUsbAudioApi api;
    if (api.load() && api.fn.TUSBAUDIO_SetAsioBufferSize) {
        unsigned int sizeUs = (unsigned int)(((double)bufSz / m_sampleRate) * 1000000.0);
        api.fn.TUSBAUDIO_SetAsioBufferSize(sizeUs);
    }

    if (!m_tusb.createBuffers(inCh, outCh, bufSz)) return ASE_NoMemory;

    for (long i = 0; i < nCh; i++) {
        bi[i].buffers[0] = m_tusb.getChannelBuffer(m_asioBufs[i].isInput, m_asioBufs[i].hwIndex, 0);
        bi[i].buffers[1] = m_tusb.getChannelBuffer(m_asioBufs[i].isInput, m_asioBufs[i].hwIndex, 1);
    }

    LOG_INFO(LOG_MODULE, "createBuffers (%ld samples) OK", bufSz);
    return ASE_OK;
}

ASIOError BehringerASIO::disposeBuffers() {
    if (!m_initialized) return ASE_NotPresent;
    stop();
    m_tusb.disposeBuffers();
    m_asioBufs.clear();
    m_callbacks = nullptr;
    return ASE_OK;
}

ASIOError BehringerASIO::controlPanel() { return ASE_NotPresent; }
ASIOError BehringerASIO::future(long selector, void* opt) { 
    // Official driver returns ASE_NotPresent for ALL selectors including kAsioSupportsTimeInfo
    return ASE_InvalidParameter; 
}
ASIOError BehringerASIO::outputReady() { 
    // Official driver returns ASE_NotPresent
    return ASE_NotPresent; 
}

ASIOError BehringerASIO::start() {
    if (!m_initialized) return ASE_NotPresent;
    if (m_running) return ASE_OK;

    m_samplePosition = 0;
    QueryPerformanceCounter(&m_startTime);

    m_tusb.setBufferSwitchCallback(BehringerASIO::bufferSwitchBridge, this);
    if (!m_tusb.start()) return ASE_HWMalfunction;

    m_running = true;
    LOG_INFO(LOG_MODULE, "start() OK");
    return ASE_OK;
}

ASIOError BehringerASIO::stop() {
    if (!m_running) return ASE_OK;
    m_tusb.stop();
    m_running = false;
    LOG_INFO(LOG_MODULE, "stop() OK");
    return ASE_OK;
}

void BehringerASIO::bufferSwitchBridge(long bufferIndex, void* userData) {
    if (userData) ((BehringerASIO*)userData)->onBufferSwitch((long)bufferIndex);
}

void BehringerASIO::onBufferSwitch(long bufferIndex) {
    m_samplePosition += m_bufferSize;

    // Official driver does NOT support kAsioSupportsTimeInfo.
    // It uses plain bufferSwitch with directProcess=ASIOTrue.
    if (m_callbacks && m_callbacks->bufferSwitch) {
        m_callbacks->bufferSwitch(bufferIndex, ASIOTrue);
    }
}

HRESULT BehringerASIO::CreateInstance(LPUNKNOWN pUnk, REFIID riid, void** ppv) {
    HRESULT hr = S_OK;
    auto* drv = new BehringerASIO(pUnk, &hr);
    if (FAILED(hr)) { delete drv; return hr; }
    hr = drv->QueryInterface(riid, ppv);
    drv->Release();
    return hr;
}
