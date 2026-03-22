/*
 * BehringerASIO - 百灵达 UMC ASIO 驱动主类
 *
 * 方案: TUSBAUDIO 直连模式 (v6 - 纯原生直连终极版)
 * 通过逆向的 TUSBAUDIO 专有协议直接和 umc_audio.sys 通信，
 * 使用 DMA 内存映射传输音频，完全绕过官方 ASIO DLL。
 * 采用阴阳融合时序 (Event + WFB ACK) + ASIO ABI hi:lo 格式兼容。
 *
 * 数据流:
 *   DAW <-> BehringerASIO (TUSBAUDIO IOCTL) <-> umc_audio.sys <-> USB <-> 硬件
 */

#pragma once

#include <windows.h>
#include "../asio/asio.h"
#include "../asio/iasiodrv.h"
#include "TusbAudioDirect.h"
#include <vector>
#include <string>
#include <mutex>
#include <atomic>

// Driver Version
#define BEHRINGER_ASIO_VERSION  6

// Driver Name
#define BEHRINGER_ASIO_NAME     "Behringer UMC ASIO"

// Max channels for stack arrays in hot path
#define MAX_ROUTING_CHANNELS 32

// CLSID for this driver
EXTERN_C extern const GUID CLSID_BehringerASIO;

class BehringerASIO : public IASIO {
public:
    BehringerASIO(LPUNKNOWN pUnk, HRESULT* phr);
    virtual ~BehringerASIO();

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IASIO methods
    ASIOBool init(void* sysHandle) override;
    void getDriverName(char* name) override;
    long getDriverVersion() override;
    void getErrorMessage(char* string) override;

    ASIOError start() override;
    ASIOError stop() override;

    ASIOError getChannels(long* numInputChannels, long* numOutputChannels) override;
    ASIOError getLatencies(long* inputLatency, long* outputLatency) override;
    ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) override;

    ASIOError canSampleRate(ASIOSampleRate sampleRate) override;
    ASIOError getSampleRate(ASIOSampleRate* sampleRate) override;
    ASIOError setSampleRate(ASIOSampleRate sampleRate) override;

    ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) override;
    ASIOError setClockSource(long reference) override;
    ASIOError getSamplePosition(long long* sPos, long long* tStamp) override;

    ASIOError getChannelInfo(ASIOChannelInfo* info) override;
    ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels,
                            long bufferSize, ASIOCallbacks* callbacks) override;
    ASIOError disposeBuffers() override;

    ASIOError controlPanel() override;
    ASIOError future(long selector, void* opt) override;
    ASIOError outputReady() override;

    // Static factory method
    static HRESULT CreateInstance(LPUNKNOWN pUnk, REFIID riid, void** ppv);


private:
    // COM reference count
    volatile LONG m_refCount;

    // State
    bool m_initialized;
    bool m_buffersCreated;
    bool m_running;
    HWND m_sysHandle;

    // === TUSBAUDIO 直连引擎 ===
    TusbAudioDirect m_tusb;

    // ASIO buffer pointers (Float32, converted from INT32 DMA)
    struct AsioChannelBuf {
        bool isInput;
        int hwIndex;       // index in TusbAudioDirect's channel list
        float* buf[2];     // double buffer (ASIO float32)
    };
    std::vector<AsioChannelBuf> m_asioBufs;

    // Callbacks
    ASIOCallbacks* m_callbacks;
    long m_bufferSize;

    // Channel counts
    int m_numInputChannels;
    int m_numOutputChannels;

    // Audio state
    ASIOSampleRate m_sampleRate;

    // Sample position tracking
    std::atomic<long long> m_samplePosition;
    LARGE_INTEGER m_startTime;

    // Error message
    char m_errorMessage[256];

    // Mutex
    std::recursive_mutex m_mutex;

    // Static callback bridge
    static void bufferSwitchBridge(long bufferIndex, void* userData);
    void onBufferSwitch(long bufferIndex);

    // Helpers
    bool isRateSupported(double rate) const;
};
