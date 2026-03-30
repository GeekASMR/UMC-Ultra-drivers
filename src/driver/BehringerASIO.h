#pragma once

#include <windows.h>
#include "../asio/asio.h"
#include "../asio/iasiodrv.h"
#include "../bridge/AsmrtopIPC.h"
#include <vector>
#include <mutex>
#include <atomic>

// Virtual Channels Settings
#define NUM_VIRTUAL_PAIRS   4    // 4 stereo pairs = 8 virtual channels
#define NUM_VIRTUAL_CH      (NUM_VIRTUAL_PAIRS * 2)

// Inline conversions for ASIO float<->int formats
inline void convertFloatToAsio(float f, uint8_t* rawOut, int idx, ASIOSampleType type) {
    if (f > 1.0f) f = 1.0f; else if (f < -1.0f) f = -1.0f;
    if (type == ASIOSTInt32LSB) {
        int32_t val = (int32_t)(f * 2147483647.0f);
        ((int32_t*)rawOut)[idx] = val;
    } else if (type == ASIOSTFloat32LSB) {
        ((float*)rawOut)[idx] = f;
    } else if (type == ASIOSTInt24LSB) {
        int32_t val = (int32_t)(f * 8388607.0f);
        rawOut[idx * 3 + 0] = (uint8_t)(val & 0xFF);
        rawOut[idx * 3 + 1] = (uint8_t)((val >> 8) & 0xFF);
        rawOut[idx * 3 + 2] = (uint8_t)((val >> 16) & 0xFF);
    } else if (type == ASIOSTInt16LSB) {
        int16_t val = (int16_t)(f * 32767.0f);
        ((int16_t*)rawOut)[idx] = val;
    }
}

inline float convertAsioToFloat(const uint8_t* rawIn, int idx, ASIOSampleType type) {
    if (type == ASIOSTInt32LSB) {
        return (float)(((int32_t*)rawIn)[idx]) / 2147483648.0f;
    } else if (type == ASIOSTFloat32LSB) {
        return ((float*)rawIn)[idx];
    } else if (type == ASIOSTInt24LSB) {
        int32_t val = rawIn[idx * 3] | (rawIn[idx * 3 + 1] << 8) | (rawIn[idx * 3 + 2] << 16);
        if (val & 0x800000) val |= 0xFF000000;
        return (float)val / 8388608.0f;
    } else if (type == ASIOSTInt16LSB) {
        return (float)(((int16_t*)rawIn)[idx]) / 32768.0f;
    }
    return 0.0f;
}

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
    volatile LONG m_refCount;
    IASIO* m_baseAsio;             // The real hardware ASIO driver

    // Original and wrapper callbacks
    ASIOCallbacks m_ourCallbacks;
    ASIOCallbacks* m_dawCallbacks;
    long m_bufferSize;
    long m_bufferSizeFrames;
    ASIOSampleRate m_sampleRate;
    long m_callbackCounter;
    long m_targetCallbacks;
    bool m_isLicensed;
    bool m_trialValid;

    // Channel metadata
    long m_hwNumInputs, m_hwNumOutputs;
    int m_numVirtualInputs;
    int m_numVirtualOutputs;
    ASIOSampleType m_vrtType; // Dynamically cloned from native hardware

    // Virtual Channels IPC (ASMRTOP WDM IPC)
    AsmrtopIpcChannel m_playIpc[NUM_VIRTUAL_PAIRS];  // WDM -> DAW
    AsmrtopIpcChannel m_capIpc[NUM_VIRTUAL_PAIRS];   // DAW -> WDM
    
    // Original buffer copies for proxying
    struct ProxyBuffer {
        ASIOBufferInfo original;
        ASIOBufferInfo wrapper;
        bool isVirtual;
        int virtualIndex;
    };
    
    ProxyBuffer* m_vrtInProxies[NUM_VIRTUAL_PAIRS * 2];
    ProxyBuffer* m_vrtOutProxies[NUM_VIRTUAL_PAIRS * 2];
    
    std::vector<ProxyBuffer> m_bufferMap;
    std::vector<float*> m_rawVrtBufs;

    // Helper functions for proxy callbacks
    static void bufferSwitchProxy(long bufferIndex, ASIOBool directProcess);
    static void sampleRateDidChangeProxy(ASIOSampleRate sRate);
    static long asioMessageProxy(long selector, long value, void* message, double* opt);
    static ASIOTime* bufferSwitchTimeInfoProxy(ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess);

    // Instance ptr for static callbacks
    static BehringerASIO* s_instance;

    void onBufferSwitch_Read(long bufferIndex);
    void onBufferSwitch_Write(long bufferIndex);
};
