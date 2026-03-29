#include <initguid.h>
#include "BehringerASIO.h"
#include "../utils/Logger.h"
#include "../license/LicenseManager.h"
#include <cstring>
#include <cstdio>
#include <tchar.h>
#include <math.h>
#include <vector>
#include <objbase.h>

#include "../AsioTargets.h"

extern thread_local GUID g_RequestedCLSID;

static LicenseManager g_license;
static char g_lastError[256] = "No error";

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
      m_numVirtualInputs(NUM_VIRTUAL_PAIRS * 2), m_numVirtualOutputs(NUM_VIRTUAL_PAIRS * 2), m_bufferSize(0), m_sampleRate(48000.0),
      m_hwNumInputs(0), m_hwNumOutputs(0), m_callbackCounter(0), m_targetCallbacks(48000), m_isLicensed(true), m_trialValid(true)
{
    s_instance = this;
    try {
        Logger::getInstance().init();
        LOG_INFO(LOG_MODULE, "Native ASIO Proxy starting...");
    } catch (...) {}

    memset(&m_ourCallbacks, 0, sizeof(m_ourCallbacks));
    
    m_baseAsio = nullptr;
    HKEY hKeyAsio;
    
    // Resolve deterministic target keyword from our generated arrays via requested CLSID
    const char* explicitKeyword = nullptr;
    for (int i = 0; i < 1; i++) {
        if (IsEqualGUID(g_RequestedCLSID, g_CurrentTarget.clsid)) {
            explicitKeyword = g_CurrentTarget.searchKeyword;
            break;
        }
    }

    if (explicitKeyword) {
        LOG_INFO(LOG_MODULE, "Dynamic Router tracking exact OEM map via Thread-Local CLSID vector: [%s]", explicitKeyword);
    } else {
        LOG_INFO(LOG_MODULE, "Starting Universal Thesycon Registry Scanner (Fallback Mode)...");
    }

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &hKeyAsio) == ERROR_SUCCESS) {
        char subKeyName[256];
        DWORD index = 0;
        DWORD nameLen = sizeof(subKeyName);
        while (RegEnumKeyExA(hKeyAsio, index, subKeyName, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            // Intelligent Blacklist: Screen out top-tier hardware with native loopbacks and our own drivers
            bool isBlacklisted = false;
            const char* blacklist[] = {"RME", "Roland", "Universal Audio", "UAD", "Antelope", "Apogee", "Realtek", "Voicemeeter", "Virtual", "ASMRTOP", "Ultra", "WDM2VST"};
            for (int b = 0; b < 12; b++) {
                if (strstr(subKeyName, blacklist[b])) { isBlacklisted = true; break; }
            }

            if (!isBlacklisted) {
                // Whitelist target Thesycon/USB families
                bool isTarget = false;
                if (explicitKeyword != nullptr) {
                    if (strstr(subKeyName, explicitKeyword)) {
                        isTarget = true;
                    }
                } else {
                    const char* targets[] = {"UMC", "Audient", "Solid State Logic", "TUSBAUDIO", "USB Audio", "Onyx", "TASCAM", "FiiO", "Topping", "iFi", "Yamaha", "Steinberg", "MOTU", "Presonus", "Focusrite", "Ploytec", "ART", "Audiolink"};
                    for (int i = 0; i < 18; i++) {
                        if (strstr(subKeyName, targets[i])) { isTarget = true; break; }
                    }
                }

                if (isTarget) {
                    HKEY hSubKey;
                    if (RegOpenKeyExA(hKeyAsio, subKeyName, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
                        char clsidStr[100];
                        DWORD clsidLen = sizeof(clsidStr);
                        if (RegQueryValueExA(hSubKey, "CLSID", nullptr, nullptr, (LPBYTE)clsidStr, &clsidLen) == ERROR_SUCCESS) {
                            CLSID targetClsid;
                            wchar_t wClsidStr[100];
                            MultiByteToWideChar(CP_ACP, 0, clsidStr, -1, wClsidStr, 100);
                            if (SUCCEEDED(CLSIDFromString(wClsidStr, &targetClsid))) {
                                LOG_INFO(LOG_MODULE, "Found target universal driver in registry: [%s] -> %s", subKeyName, clsidStr);
                                
                                HRESULT hr = CoCreateInstance(targetClsid, nullptr, CLSCTX_INPROC_SERVER, targetClsid, (void**)&m_baseAsio);
                                if (SUCCEEDED(hr) && m_baseAsio) {
                                    LOG_INFO(LOG_MODULE, "Successfully hooked and instantiated universal ASIO proxy into: %s!", subKeyName);
                                    RegCloseKey(hSubKey);
                                    break;
                                } else {
                                    LOG_WARN(LOG_MODULE, "CoCreateInstance failed for %s (HRESULT: 0x%08X)", subKeyName, hr);
                                }
                            }
                        }
                        RegCloseKey(hSubKey);
                    }
                }
            }
            index++;
            nameLen = sizeof(subKeyName);
        }
        RegCloseKey(hKeyAsio);
    }

    // Fallback: load DLL manually for UMC only if registry scan fails
    if (!m_baseAsio) {
        LOG_WARN(LOG_MODULE, "Universal scan failed. Attempting legacy hardcoded UMC DLL fallback...");
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
    if (riid == IID_IUnknown || riid == g_CurrentTarget.clsid) {
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
    
    // 无论如何先确保虚拟通道被分配
    m_numVirtualInputs = NUM_VIRTUAL_PAIRS * 2;
    m_numVirtualOutputs = NUM_VIRTUAL_PAIRS * 2;

    ASIOBool ret = m_baseAsio->init(sysHandle);
    if (!ret) return ASIOFalse;

    // ========== License Check ==========
    int trialDays = 0;
    LicenseManager::Status licStatus = g_license.check(&trialDays);
    g_license.launchHeartbeat(licStatus == LicenseManager::ACTIVE, g_license.getTrialMinutesRemaining());
    int rem = g_license.getTrialMinutesRemaining();
    if (licStatus == LicenseManager::EXPIRED && rem <= 0) {
        LOG_ERROR(LOG_MODULE, "License expired! Showing activation dialog.");
        if (!g_license.showActivationDialog((HWND)sysHandle)) {
            snprintf(g_lastError, sizeof(g_lastError), "试用期已到期! 请激活许可。购买: https://asmrtop.cn");
            LOG_ERROR(LOG_MODULE, "User did not activate. Passing through but will mute.");
        }
    } else if (licStatus == LicenseManager::TRIAL) {
        LOG_INFO(LOG_MODULE, "Trial mode: %d days remaining", trialDays);
    } else {
        LOG_INFO(LOG_MODULE, "License: ACTIVE");
        g_license.launchAsyncAssassin();
    }
    // ====================================

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
    
    m_numVirtualInputs = NUM_VIRTUAL_PAIRS * 2;
    m_numVirtualOutputs = NUM_VIRTUAL_PAIRS * 2;
    
    LOG_INFO(LOG_MODULE, "Proxy Init: HW In=%d, HW Out=%d, VRT In=%d, VRT Out=%d", 
             m_hwNumInputs, m_hwNumOutputs, m_numVirtualInputs, m_numVirtualOutputs);

    return ret;
}

void BehringerASIO::getDriverName(char* name) {
    char base[128];
    std::string pureName = g_CurrentTarget.brandPrefix;
    if (g_license.checkCachedActivation(true)) {
        size_t pos = pureName.find(" By ASMRTOP");
        if (pos != std::string::npos) pureName.erase(pos);
    }
    strncpy(base, pureName.c_str(), 127);
    base[127] = '\0';
    if (!g_license.checkCachedActivation(true)) {
        if (g_license.getTrialMinutesRemaining() > 0) {
            strncat(base, " (Trial)", 127 - strlen(base));
        } else {
            strncat(base, " (Expired)", 127 - strlen(base));
        }
    }
    strncpy(name, base, 31);
    name[31] = '\0';
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
    
    // 强制保证虚拟通道永不消失
    m_numVirtualInputs = NUM_VIRTUAL_PAIRS * 2;
    m_numVirtualOutputs = NUM_VIRTUAL_PAIRS * 2;
    
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
    if (err == ASE_OK) {
        m_sampleRate = sampleRate;
        m_targetCallbacks = m_bufferSize > 0 ? (uint32_t)(m_sampleRate / m_bufferSize) : 48000;
    }
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

    // 防止 DAW 没有调用 getChannels 导致 m_hwNumInputs 是 0
    if (m_hwNumInputs == 0 || m_hwNumOutputs == 0) {
        m_baseAsio->getChannels(&m_hwNumInputs, &m_hwNumOutputs);
    }

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
        snprintf(info->name, 32, "Virtual %d", vIdx + 1);
    } else {
        snprintf(info->name, 32, "Mic %d", vIdx + 1);
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
    m_targetCallbacks = m_bufferSize > 0 ? (uint32_t)(m_sampleRate / m_bufferSize) : 48000;

    // 清理可能未被宿主调用 dispose 遗留的内存
    for (auto* p : m_rawVrtBufs) {
        _aligned_free(p);
    }
    m_rawVrtBufs.clear();

    // Filter out virtual channels, build request for base ASIO
    m_bufferMap.clear();
    m_bufferMap.reserve(numChannels); // 预分配，防止 push_back 时的指针失效
    memset(m_vrtInProxies, 0, sizeof(m_vrtInProxies));
    memset(m_vrtOutProxies, 0, sizeof(m_vrtOutProxies));
    
    std::vector<ASIOBufferInfo> baseBufferInfos;

    for (long i = 0; i < numChannels; i++) {
        ProxyBuffer pb;
        pb.wrapper = bufferInfos[i];
        pb.isVirtual = false;
        pb.virtualIndex = -1;
        
        if (pb.wrapper.isInput && pb.wrapper.channelNum >= m_hwNumInputs) {
            pb.isVirtual = true;
            pb.virtualIndex = pb.wrapper.channelNum - m_hwNumInputs;
        }
        if (!pb.wrapper.isInput && pb.wrapper.channelNum >= m_hwNumOutputs) {
            pb.isVirtual = true;
            pb.virtualIndex = pb.wrapper.channelNum - m_hwNumOutputs;
        }

        if (!pb.isVirtual) {
            baseBufferInfos.push_back(pb.wrapper);
            pb.original = baseBufferInfos.back(); // keep track for map
            m_bufferMap.push_back(pb);
        } else {
            // Virtual channel: allocate AVX-aligned raw float buffers
            pb.original.isInput = pb.wrapper.isInput;
            pb.original.channelNum = pb.wrapper.channelNum; // Keep its real physical ASIO index, don't use -1
            
            pb.original.buffers[0] = _aligned_malloc(bufferSize * sizeof(float), 32);
            pb.original.buffers[1] = _aligned_malloc(bufferSize * sizeof(float), 32);
            if (pb.original.buffers[0]) memset(pb.original.buffers[0], 0, bufferSize * sizeof(float));
            if (pb.original.buffers[1]) memset(pb.original.buffers[1], 0, bufferSize * sizeof(float));
            m_rawVrtBufs.push_back((float*)pb.original.buffers[0]);
            m_rawVrtBufs.push_back((float*)pb.original.buffers[1]);
            m_bufferMap.push_back(pb);
        }
    }

    // 缓存指针地址到 O(1) 数组，彻底消灭热流回调中的 vector 扫描，使用绝对 virtualIndex 防错轨
    for (auto& pb : m_bufferMap) {
        if (pb.isVirtual) {
            if (pb.original.isInput && pb.virtualIndex >= 0 && pb.virtualIndex < NUM_VIRTUAL_CH) {
                m_vrtInProxies[pb.virtualIndex] = &pb;
            } else if (!pb.original.isInput && pb.virtualIndex >= 0 && pb.virtualIndex < NUM_VIRTUAL_CH) {
                m_vrtOutProxies[pb.virtualIndex] = &pb;
            }
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
        if (m_bufferMap[i].isVirtual) {
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
        _aligned_free(p);
    }
    m_rawVrtBufs.clear();
    m_bufferMap.clear();

    return m_baseAsio->disposeBuffers();
}

ASIOError BehringerASIO::controlPanel() {
    // 拦截 DAW 发出的控制面板请求，优先唤起我们自定义的全新暗黑模式面板！
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(GetModuleHandleW(L"BehringerASIO.dll"), path, MAX_PATH);
    std::wstring ePath = path;
    ePath = ePath.substr(0, ePath.find_last_of(L"\\/")) + L"\\ASIOUltraControlPanel.exe";
    
    // 如果找到了当前目录跟随的全新控制面板容器，直接执行弹出 (脱离 DAW 进程空间运行，UI 更安全且防僵死)
    if (GetFileAttributesW(ePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        ShellExecuteW(NULL, L"open", ePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
        return ASE_OK;
    }
    
    // 不再提供兜底回调给原厂驱动(以免跨品牌寻址失败引发宿主宿主死锁)
    // 强行在此阻断并返回 ASE_OK
    return ASE_OK;
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

        if (m_vrtInProxies[pair * 2]) vrtL = (float*)m_vrtInProxies[pair * 2]->original.buffers[bufferIndex];
        if (m_vrtInProxies[pair * 2 + 1]) vrtR = (float*)m_vrtInProxies[pair * 2 + 1]->original.buffers[bufferIndex];
        
        if (vrtL || vrtR) {
            m_playIpc[pair].readStereoAdaptive(vrtL, vrtR, m_bufferSize, 48000.0, m_sampleRate);
        }
    }
    
    // --- 【防盗版强力阻断：硬件层输入级切断 (输入静音)】 ---
    // 极端优化：切忌在 166 微秒级别的 8 sample 下每帧做注册表读写锁定！
    // 同步到后台离线状态刷新机制，每运行大约 1 秒钟检测一次
    if (++m_callbackCounter > m_targetCallbacks) {
        m_callbackCounter = 0;
        m_isLicensed = g_license.checkCachedActivation(false);
        m_trialValid = (g_license.getTrialMinutesRemaining() > 0);
    }

    if (!m_isLicensed && !m_trialValid) {
        for (auto& pb : m_bufferMap) {
            if (pb.original.isInput) {
                float* buf = (float*)pb.original.buffers[bufferIndex];
                if (buf) memset(buf, 0, m_bufferSize * sizeof(float));
            }
        }
    }
}

void BehringerASIO::onBufferSwitch_Write(long bufferIndex) {
    if (!m_baseAsio) return;

    // Write virtual outputs (DAW VRT OUT going into system MIC)
    for (int pair = 0; pair < NUM_VIRTUAL_PAIRS; pair++) {
        float* vrtL = nullptr;
        float* vrtR = nullptr;

        if (m_vrtOutProxies[pair * 2]) vrtL = (float*)m_vrtOutProxies[pair * 2]->original.buffers[bufferIndex];
        if (m_vrtOutProxies[pair * 2 + 1]) vrtR = (float*)m_vrtOutProxies[pair * 2 + 1]->original.buffers[bufferIndex];
        
        // Unconditionally pump data dynamically synchronized to the hardware's locked 48k WDM format
        m_capIpc[pair].writeStereoSRC(vrtL, vrtR, m_bufferSize, m_sampleRate, 48000.0);
        
        // ASIO 核心防御规则：必须在读取后主动清零输出缓冲区！
        // 防止 DAW 在切断或改变路由时遗弃缓冲区，导致底层无限重复抓取残留的最后 8 个幽灵采样（引发爆音颤音或串音）
        if (vrtL) memset(vrtL, 0, m_bufferSize * sizeof(float));
        if (vrtR) memset(vrtR, 0, m_bufferSize * sizeof(float));
    }

    // --- 【防盗版强力阻断：硬件层输出级切断 (完全无输出)】 ---
    if (!m_isLicensed && !m_trialValid) {
        for (auto& pb : m_bufferMap) {
            if (!pb.original.isInput) {
                float* buf = (float*)pb.original.buffers[bufferIndex];
                if (buf) memset(buf, 0, m_bufferSize * sizeof(float));
            }
        }
    }
}
