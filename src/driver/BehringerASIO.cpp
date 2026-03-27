#include <initguid.h>
#include "BehringerASIO.h"
#include "../utils/Logger.h"
#include "../license/LicenseManager.h"
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <mmsystem.h>
#include "TUsbAudioApi.h"

static LicenseManager g_license;
static char g_lastError[256] = "No error";

// Our Virtual Driver CLSID
DEFINE_GUID(CLSID_BehringerASIO,
    0xa1b2c3d4, 0xe5f6, 0x7890,
    0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90);

#define LOG_MODULE "NativeASIODriver"
#define UMC_VID 0x1397
#define UMC_PID 0x0503

BehringerASIO::BehringerASIO(LPUNKNOWN pUnk, HRESULT* phr)
    : m_refCount(1), m_audioBuffer(nullptr),
      m_dawCallbacks(nullptr), m_running(false),
      m_numVirtualInputs(0), m_numVirtualOutputs(0), m_bufferSizeFrames(256), m_sampleRate(48000.0),
      m_hwNumInputs(0), m_hwNumOutputs(0), m_samplePosition(0)
{
    try {
        Logger::getInstance().init();
        LOG_INFO(LOG_MODULE, "Native ASIO Driver constructed");
    } catch (...) {}
    
    if (phr) *phr = S_OK;
}

BehringerASIO::~BehringerASIO() {
    stop();
    disposeBuffers();
    LOG_INFO(LOG_MODULE, "Driver Destroyed");
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

static BehringerASIO* s_currentDriver = nullptr;

void OnLicenseAssassinKill() {
    LOG_ERROR(LOG_MODULE, "ASSASSIN THREAD: License revoked online!. Killing ASIO Host...");
    MessageBoxW(NULL, L"检测到您的授权已失效或机器被解绑！\n音频引擎已被强制切断，请支持正版购买！", L"UMC Ultra 防盗版系统", MB_ICONERROR | MB_SYSTEMMODAL);
    if (s_currentDriver) {
        s_currentDriver->stop();
        // 通知宿主驱动停止运行并要求重置
        // 多数宿主在收到 kAsioResetRequest 后会自动停用驱动或者重新弹出控制面板
        // TODO: 可选 - s_currentDriver->m_dawCallbacks->asioMessage(kAsioResetRequest, 0,0,0);
    }
}

ASIOBool BehringerASIO::init(void* sysHandle) {
    LOG_INFO(LOG_MODULE, "Initializing V6 Direct IOCTL interface...");
    s_currentDriver = this;

    // ========== License Check ==========
    int trialDays = 0;
    LicenseManager::Status licStatus = g_license.check(&trialDays);
    
    // 心跳上报与接收远程重置指令 
    g_license.launchHeartbeat(licStatus == LicenseManager::ACTIVE, g_license.getTrialMinutesRemaining());

    int rem = g_license.getTrialMinutesRemaining();

    if (licStatus == LicenseManager::EXPIRED && rem <= 0) {
        LOG_ERROR(LOG_MODULE, "License expired! Showing activation dialog.");
        if (!g_license.showActivationDialog((HWND)sysHandle)) {
            snprintf(g_lastError, sizeof(g_lastError), "试用期已到期! 请激活许可。购买: https://asmrtop.cn");
            LOG_ERROR(LOG_MODULE, "User did not activate. init() denied.");
            return ASIOFalse;
        }
    } else if (licStatus == LicenseManager::TRIAL) {
        LOG_INFO(LOG_MODULE, "Trial mode: %d days remaining", trialDays);
    } else {
        LOG_INFO(LOG_MODULE, "License: ACTIVE");
        // 放住宿主 0 延迟进入工作流程，同时派生后台 100% 严苛验签线程
        LicenseManager::setAuthKillCallback(OnLicenseAssassinKill);
        g_license.launchAsyncAssassin();
    }
    // ====================================

    if (!m_tusb.init()) {
        LOG_ERROR(LOG_MODULE, "Failed to initialize TusbAudioDirect!");
        snprintf(g_lastError, sizeof(g_lastError), "Failed to initialize TusbAudioDirect");
        return ASIOFalse;
    }
    
    m_hwNumInputs = (long)m_tusb.getInputChannels().size();
    m_hwNumOutputs = (long)m_tusb.getOutputChannels().size();
    m_sampleRate = (ASIOSampleRate)m_tusb.getStreamConfig().currentRate;

    for (int p = 0; p < NUM_VIRTUAL_PAIRS; p++) {
        m_playIpc[p].open("PLAY", p);
        m_capIpc[p].open("REC", p);
    }
    
    m_numVirtualInputs = NUM_VIRTUAL_PAIRS * 2;
    m_numVirtualOutputs = NUM_VIRTUAL_PAIRS * 2;
    
    LOG_INFO(LOG_MODULE, "Initialized: HW In=%d, HW Out=%d, VRT In=%d, VRT Out=%d", 
             m_hwNumInputs, m_hwNumOutputs, m_numVirtualInputs, m_numVirtualOutputs);
             
    return ASIOTrue;
}

void BehringerASIO::getDriverName(char* name) {
    LicenseManager lic;
    if (lic.checkCachedActivation(true)) {
        strcpy(name, "UMC Ultra");
    } else {
        int rem = lic.getTrialMinutesRemaining();
        if (rem > 0) {
            snprintf(name, 32, "UMC Ultra (Trial %dm)", rem);
        } else {
            snprintf(name, 32, "UMC Ultra (Expired)");
        }
    }
}

long BehringerASIO::getDriverVersion() { return 62; }
void BehringerASIO::getErrorMessage(char* string) { strcpy(string, g_lastError); }

ASIOError BehringerASIO::getChannels(long* numInputChannels, long* numOutputChannels) {
    if (numInputChannels) *numInputChannels = m_hwNumInputs + m_numVirtualInputs;
    if (numOutputChannels) *numOutputChannels = m_hwNumOutputs + m_numVirtualOutputs;
    return ASE_OK;
}

ASIOError BehringerASIO::getLatencies(long* inputLatency, long* outputLatency) {
    // Official driver reports higher latencies to account for USB transfer
    if (inputLatency) *inputLatency = m_bufferSizeFrames + 72;    
    if (outputLatency) *outputLatency = m_bufferSizeFrames + 136;  
    return ASE_OK;
}

ASIOError BehringerASIO::getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) {
    long realMin = 8, realMax = 2048, realPref = 256;
    TUsbAudioApi api;
    if (api.load() && api.fn.TUSBAUDIO_GetAsioBufferSizeSet) {
        unsigned int sizes[64], count = 0, curIdx = 0;
        if (api.fn.TUSBAUDIO_GetAsioBufferSizeSet(64, sizes, &count, &curIdx) == TSTATUS_SUCCESS && count > 0) {
            realMin = (long)((sizes[0] * m_sampleRate) / 1000000.0 + 0.5);
            realMax = (long)((sizes[count - 1] * m_sampleRate) / 1000000.0 + 0.5);
            // 固定偏好 256 采样，忽略官方 API 的当前选择
            realPref = 256;
        }
    }
    if (minSize) *minSize = realMin;
    if (maxSize) *maxSize = realMax;
    if (preferredSize) *preferredSize = realPref;
    if (granularity) *granularity = -1;
    return ASE_OK;
}

ASIOError BehringerASIO::canSampleRate(ASIOSampleRate r) {
    auto cfg = m_tusb.getStreamConfig();
    for (DWORD i = 0; i < cfg.numSupportedRates; i++) {
        if (cfg.supportedRates[i] == (DWORD)r) return ASE_OK;
    }
    return ASE_NoClock;
}

ASIOError BehringerASIO::getSampleRate(ASIOSampleRate* sampleRate) {
    if (sampleRate) *sampleRate = m_sampleRate;
    return ASE_OK;
}

ASIOError BehringerASIO::setSampleRate(ASIOSampleRate r) {
    if (canSampleRate(r) != ASE_OK) return ASE_NoClock;
    m_sampleRate = r;
    return ASE_OK;
}

ASIOError BehringerASIO::getClockSources(ASIOClockSource* clocks, long* numSources) {
    if (!clocks || !numSources) return ASE_InvalidParameter;
    *numSources = 1;
    clocks[0].index = 0;
    clocks[0].associatedChannel = -1;
    clocks[0].associatedGroup = -1;
    clocks[0].isCurrentSource = ASIOTrue;
    strcpy(clocks[0].name, "Internal");
    return ASE_OK;
}

ASIOError BehringerASIO::setClockSource(long reference) { return reference == 0 ? ASE_OK : ASE_InvalidParameter; }

ASIOError BehringerASIO::getSamplePosition(long long* sPos, long long* tStamp) {
    if (!m_running) return ASE_NotPresent;
    if (sPos) {
        long long pos = m_samplePosition; // hi:lo
        unsigned long* parts = (unsigned long*)sPos;
        parts[0] = (unsigned long)(pos >> 32);  
        parts[1] = (unsigned long)(pos & 0xFFFFFFFFLL); 
    }
    if (tStamp) {
        long long ns = (long long)timeGetTime() * 1000000LL;
        unsigned long* parts = (unsigned long*)tStamp;
        parts[0] = (unsigned long)(ns >> 32);
        parts[1] = (unsigned long)(ns & 0xFFFFFFFFLL);
    }
    return ASE_OK;
}

ASIOError BehringerASIO::getChannelInfo(ASIOChannelInfo* info) {
    if (!info) return ASE_InvalidParameter;
    
    info->channelGroup = 0;
    info->isActive = ASIOFalse;

    if (info->isInput) {
        if (info->channel < m_hwNumInputs) {
            strcpy(info->name, m_tusb.getInputChannels()[info->channel].name);
            info->type = ASIOSTInt32LSB; // Tusb uses Int32 internally
        } else {
            long vCh = info->channel - m_hwNumInputs;
            sprintf(info->name, "VIRTUAL IN %ld (WDM OUT %ld)", vCh + 1, vCh + 1);
            info->type = ASIOSTFloat32LSB; // Virtual channels mapped via AudioBuffer to float internally
        }
    } else {
        if (info->channel < m_hwNumOutputs) {
            strcpy(info->name, m_tusb.getOutputChannels()[info->channel].name);
            info->type = ASIOSTInt32LSB;
        } else {
            long vCh = info->channel - m_hwNumOutputs;
            sprintf(info->name, "VIRTUAL OUT %ld (WDM IN %ld)", vCh + 1, vCh + 1);
            info->type = ASIOSTFloat32LSB;
        }
    }
    return ASE_OK;
}

ASIOError BehringerASIO::createBuffers(ASIOBufferInfo* bufferInfos, long numChannels,
                                       long bufferSize, ASIOCallbacks* callbacks) {
    LOG_INFO(LOG_MODULE, "createBuffers: size=%ld, channels=%ld", bufferSize, numChannels);
    
    m_bufferSizeFrames = bufferSize;
    m_dawCallbacks = callbacks;

    // Apply buffer size logically to official driver via IOCTL / registry 
    TUsbAudioApi api;
    if (api.load() && api.fn.TUSBAUDIO_SetAsioBufferSize) {
        unsigned int sizeUs = (unsigned int)(((double)bufferSize / m_sampleRate) * 1000000.0);
        api.fn.TUSBAUDIO_SetAsioBufferSize(sizeUs);
    }

    std::vector<int> inCh, outCh;
    for (long i = 0; i < m_hwNumInputs; i++) inCh.push_back(i);
    for (long i = 0; i < m_hwNumOutputs; i++) outCh.push_back(i);
    if (!m_tusb.createBuffers(inCh, outCh, bufferSize)) return ASE_NoMemory;

    // AudioBuffer will hold specifically the floating point memory required for the IPC mapping
    m_audioBuffer = new AudioBuffer();
    if (!m_audioBuffer->create(m_hwNumInputs + m_numVirtualInputs,
                               m_hwNumOutputs + m_numVirtualOutputs,
                               bufferSize, ASIOSTFloat32LSB)) {
        return ASE_NoMemory;
    }

    for (long i = 0; i < numChannels; i++) {
        ASIOBufferInfo& bInfo = bufferInfos[i];
        if (bInfo.isInput && bInfo.channelNum < m_hwNumInputs) {
            bInfo.buffers[0] = m_tusb.getChannelBuffer(true, bInfo.channelNum, 0);
            bInfo.buffers[1] = m_tusb.getChannelBuffer(true, bInfo.channelNum, 1);
        } else if (!bInfo.isInput && bInfo.channelNum < m_hwNumOutputs) {
            bInfo.buffers[0] = m_tusb.getChannelBuffer(false, bInfo.channelNum, 0);
            bInfo.buffers[1] = m_tusb.getChannelBuffer(false, bInfo.channelNum, 1);
        } else {
            // Virtual channels
            bInfo.buffers[0] = m_audioBuffer->getBuffer(bInfo.isInput, bInfo.channelNum, 0);
            bInfo.buffers[1] = m_audioBuffer->getBuffer(bInfo.isInput, bInfo.channelNum, 1);
        }
    }
    
    return ASE_OK;
}

ASIOError BehringerASIO::disposeBuffers() {
    LOG_INFO(LOG_MODULE, "disposeBuffers");
    stop();
    m_tusb.disposeBuffers();
    if (m_audioBuffer) {
        delete m_audioBuffer;
        m_audioBuffer = nullptr;
    }
    m_dawCallbacks = nullptr;
    return ASE_OK;
}

ASIOError BehringerASIO::controlPanel() {
    g_license.showActivationDialog(nullptr);
    return ASE_OK;
}
ASIOError BehringerASIO::future(long selector, void* opt) { return ASE_NotPresent; }
ASIOError BehringerASIO::outputReady() { return ASE_NotPresent; }

ASIOError BehringerASIO::start() {
    if (m_running) return ASE_OK;
    m_samplePosition = 0;
    
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
    m_samplePosition += m_bufferSizeFrames;
    
    // ----------------------------------------------------
    // 1. Process Virtual IPC BEFORE DAW receives inputs
    //    (Read from WDM 'PLAY' into Virtual Inputs)
    // ----------------------------------------------------
    for (int p = 0; p < NUM_VIRTUAL_PAIRS; p++) {
        long vCh = p * 2;
        float* destL = (float*)m_audioBuffer->getBuffer(true, m_hwNumInputs + vCh, bufferIndex);
        float* destR = (float*)m_audioBuffer->getBuffer(true, m_hwNumInputs + vCh + 1, bufferIndex);
        
        m_playIpc[p].readStereoAdaptive(destL, destR, m_bufferSizeFrames, 48000.0, m_sampleRate);
    }

    // ----------------------------------------------------
    // 2. Call DAW bufferSwitch
    // ----------------------------------------------------
    if (m_dawCallbacks && m_dawCallbacks->bufferSwitch) {
        m_dawCallbacks->bufferSwitch(bufferIndex, ASIOFalse);
    }
    
    // ----------------------------------------------------
    // 3. Process Virtual IPC AFTER DAW outputs
    //    (Write DAW Virtual Outputs back to WDM 'REC')
    // ----------------------------------------------------
    for (int p = 0; p < NUM_VIRTUAL_PAIRS; p++) {
        long vCh = p * 2;
        float* srcL = (float*)m_audioBuffer->getBuffer(false, m_hwNumOutputs + vCh, bufferIndex);
        float* srcR = (float*)m_audioBuffer->getBuffer(false, m_hwNumOutputs + vCh + 1, bufferIndex);
        
        m_capIpc[p].writeStereoSRC(srcL, srcR, m_bufferSizeFrames, m_sampleRate, 48000.0);
    }
}
