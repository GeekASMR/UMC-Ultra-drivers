/*
 * AsioProxy - 官方 ASIO 驱动代理实现
 */

#include "AsioProxy.h"
#include "../utils/Logger.h"
#include <cstring>

#define LOG_MODULE "AsioProxy"

AsioProxy::AsioProxy()
    : m_hDll(nullptr)
    , m_asio(nullptr)
    , m_fnGetClassObject(nullptr)
{
}

AsioProxy::~AsioProxy() {
    unload();
}

bool AsioProxy::load(const std::wstring& dllPath, const CLSID& clsid) {
    if (m_asio) {
        LOG_WARN(LOG_MODULE, "Already loaded, unloading first");
        unload();
    }

    // 加载官方 DLL
    m_hDll = LoadLibraryW(dllPath.c_str());
    if (!m_hDll) {
        DWORD err = GetLastError();
        char pathA[512];
        WideCharToMultiByte(CP_UTF8, 0, dllPath.c_str(), -1, pathA, sizeof(pathA), nullptr, nullptr);
        LOG_ERROR(LOG_MODULE, "Failed to load DLL '%s': 0x%08X", pathA, err);
        return false;
    }

    // 获取 DllGetClassObject
    m_fnGetClassObject = (FnDllGetClassObject)GetProcAddress(m_hDll, "DllGetClassObject");
    if (!m_fnGetClassObject) {
        LOG_ERROR(LOG_MODULE, "DllGetClassObject not found in DLL");
        FreeLibrary(m_hDll);
        m_hDll = nullptr;
        return false;
    }

    // 创建 ClassFactory
    IClassFactory* pFactory = nullptr;
    HRESULT hr = m_fnGetClassObject(clsid, IID_IClassFactory, (void**)&pFactory);
    if (FAILED(hr) || !pFactory) {
        LOG_ERROR(LOG_MODULE, "DllGetClassObject failed: 0x%08X", hr);
        FreeLibrary(m_hDll);
        m_hDll = nullptr;
        m_fnGetClassObject = nullptr;
        return false;
    }

    // 创建 IASIO 实例
    hr = pFactory->CreateInstance(nullptr, clsid, (void**)&m_asio);
    pFactory->Release();

    if (FAILED(hr) || !m_asio) {
        LOG_ERROR(LOG_MODULE, "CreateInstance failed: 0x%08X", hr);
        FreeLibrary(m_hDll);
        m_hDll = nullptr;
        m_fnGetClassObject = nullptr;
        return false;
    }

    char pathA[512];
    WideCharToMultiByte(CP_UTF8, 0, dllPath.c_str(), -1, pathA, sizeof(pathA), nullptr, nullptr);
    LOG_INFO(LOG_MODULE, "Loaded official ASIO driver from '%s'", pathA);
    return true;
}

void AsioProxy::unload() {
    if (m_asio) {
        m_asio->Release();
        m_asio = nullptr;
    }
    if (m_hDll) {
        FreeLibrary(m_hDll);
        m_hDll = nullptr;
    }
    m_fnGetClassObject = nullptr;
}

// === 转发所有 IASIO 方法 ===

ASIOBool AsioProxy::init(void* sysHandle) {
    if (!m_asio) return ASIOFalse;
    ASIOBool result = m_asio->init(sysHandle);
    LOG_INFO(LOG_MODULE, "init() -> %s", result ? "OK" : "FAIL");
    return result;
}

void AsioProxy::getDriverName(char* name) {
    if (m_asio) m_asio->getDriverName(name);
}

long AsioProxy::getDriverVersion() {
    return m_asio ? m_asio->getDriverVersion() : 0;
}

void AsioProxy::getErrorMessage(char* string) {
    if (m_asio) m_asio->getErrorMessage(string);
}

ASIOError AsioProxy::start() {
    if (!m_asio) return ASE_NotPresent;
    ASIOError err = m_asio->start();
    LOG_INFO(LOG_MODULE, "start() -> %d", err);
    return err;
}

ASIOError AsioProxy::stop() {
    if (!m_asio) return ASE_NotPresent;
    ASIOError err = m_asio->stop();
    LOG_INFO(LOG_MODULE, "stop() -> %d", err);
    return err;
}

ASIOError AsioProxy::getChannels(long* numIn, long* numOut) {
    if (!m_asio) return ASE_NotPresent;
    return m_asio->getChannels(numIn, numOut);
}

ASIOError AsioProxy::getLatencies(long* inputLatency, long* outputLatency) {
    if (!m_asio) return ASE_NotPresent;
    return m_asio->getLatencies(inputLatency, outputLatency);
}

ASIOError AsioProxy::getBufferSize(long* minSize, long* maxSize,
                                     long* preferredSize, long* granularity) {
    if (!m_asio) return ASE_NotPresent;
    return m_asio->getBufferSize(minSize, maxSize, preferredSize, granularity);
}

ASIOError AsioProxy::canSampleRate(ASIOSampleRate sampleRate) {
    if (!m_asio) return ASE_NotPresent;
    return m_asio->canSampleRate(sampleRate);
}

ASIOError AsioProxy::getSampleRate(ASIOSampleRate* sampleRate) {
    if (!m_asio) return ASE_NotPresent;
    return m_asio->getSampleRate(sampleRate);
}

ASIOError AsioProxy::setSampleRate(ASIOSampleRate sampleRate) {
    if (!m_asio) return ASE_NotPresent;
    ASIOError err = m_asio->setSampleRate(sampleRate);
    LOG_INFO(LOG_MODULE, "setSampleRate(%.0f) -> %d", sampleRate, err);
    return err;
}

ASIOError AsioProxy::getClockSources(ASIOClockSource* clocks, long* numSources) {
    if (!m_asio) return ASE_NotPresent;
    return m_asio->getClockSources(clocks, numSources);
}

ASIOError AsioProxy::setClockSource(long reference) {
    if (!m_asio) return ASE_NotPresent;
    return m_asio->setClockSource(reference);
}

ASIOError AsioProxy::getSamplePosition(long long* sPos, long long* tStamp) {
    if (!m_asio) return ASE_NotPresent;
    return m_asio->getSamplePosition(sPos, tStamp);
}

ASIOError AsioProxy::getChannelInfo(ASIOChannelInfo* info) {
    if (!m_asio) return ASE_NotPresent;
    return m_asio->getChannelInfo(info);
}

ASIOError AsioProxy::createBuffers(ASIOBufferInfo* bufferInfos, long numChannels,
                                     long bufferSize, ASIOCallbacks* callbacks) {
    if (!m_asio) return ASE_NotPresent;
    ASIOError err = m_asio->createBuffers(bufferInfos, numChannels, bufferSize, callbacks);
    LOG_INFO(LOG_MODULE, "createBuffers(ch=%ld, buf=%ld) -> %d", numChannels, bufferSize, err);
    return err;
}

ASIOError AsioProxy::disposeBuffers() {
    if (!m_asio) return ASE_NotPresent;
    return m_asio->disposeBuffers();
}

ASIOError AsioProxy::controlPanel() {
    if (!m_asio) return ASE_NotPresent;
    return m_asio->controlPanel();
}

ASIOError AsioProxy::future(long selector, void* opt) {
    if (!m_asio) return ASE_InvalidParameter;
    return m_asio->future(selector, opt);
}

ASIOError AsioProxy::outputReady() {
    if (!m_asio) return ASE_NotPresent;
    return m_asio->outputReady();
}
