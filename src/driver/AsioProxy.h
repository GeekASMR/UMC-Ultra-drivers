/*
 * AsioProxy - 官方 ASIO 驱动代理
 *
 * 加载官方 umc_audioasio_x64.dll，获取其 IASIO 接口，
 * 转发所有底层音频操作，同时允许我们在 bufferSwitch 中
 * 插入路由矩阵、虚拟通道等附加功能。
 */

#pragma once

#include <windows.h>
#include "../asio/asio.h"
#include "../asio/iasiodrv.h"
#include <string>
#include <functional>

class AsioProxy {
public:
    AsioProxy();
    ~AsioProxy();

    // 加载官方 DLL 并创建 IASIO 接口
    bool load(const std::wstring& dllPath, const CLSID& clsid);
    void unload();
    bool isLoaded() const { return m_asio != nullptr; }

    // === 转发 IASIO 方法 ===
    ASIOBool init(void* sysHandle);
    void getDriverName(char* name);
    long getDriverVersion();
    void getErrorMessage(char* string);

    ASIOError start();
    ASIOError stop();

    ASIOError getChannels(long* numIn, long* numOut);
    ASIOError getLatencies(long* inputLatency, long* outputLatency);
    ASIOError getBufferSize(long* minSize, long* maxSize,
                             long* preferredSize, long* granularity);

    ASIOError canSampleRate(ASIOSampleRate sampleRate);
    ASIOError getSampleRate(ASIOSampleRate* sampleRate);
    ASIOError setSampleRate(ASIOSampleRate sampleRate);

    ASIOError getClockSources(ASIOClockSource* clocks, long* numSources);
    ASIOError setClockSource(long reference);
    ASIOError getSamplePosition(long long* sPos, long long* tStamp);

    ASIOError getChannelInfo(ASIOChannelInfo* info);
    ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels,
                             long bufferSize, ASIOCallbacks* callbacks);
    ASIOError disposeBuffers();

    ASIOError controlPanel();
    ASIOError future(long selector, void* opt);
    ASIOError outputReady();

    // 获取内部 IASIO 指针（高级用途）
    IASIO* getRawInterface() { return m_asio; }

private:
    HMODULE m_hDll;
    IASIO*  m_asio;

    // DllGetClassObject 函数指针
    typedef HRESULT (WINAPI *FnDllGetClassObject)(REFCLSID, REFIID, LPVOID*);
    FnDllGetClassObject m_fnGetClassObject;
};
