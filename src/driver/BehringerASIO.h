#pragma once

#include <windows.h>
#include "../asio/asio.h"
#include "../asio/iasiodrv.h"
#include "../bridge/AsmrtopIPC.h"
#include "../usb/UsbAudioProtocol.h"
#include "TusbAudioDirect.h"
#include "AudioBuffer.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>

// Virtual Channels Settings
#define NUM_VIRTUAL_PAIRS   4    // 4 stereo pairs = 8 virtual channels
#define NUM_VIRTUAL_CH      (NUM_VIRTUAL_PAIRS * 2)

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

    // 真正的硬件控制与传输引擎 (V6 Native Direct)
    TusbAudioDirect m_tusb;
    AudioBuffer*    m_audioBuffer;
    
    long long       m_samplePosition;
    std::atomic<bool> m_running;

    ASIOCallbacks* m_dawCallbacks;
    long m_bufferSizeFrames;
    ASIOSampleRate m_sampleRate;

    // Channel metadata
    long m_hwNumInputs, m_hwNumOutputs;
    int  m_numVirtualInputs;
    int  m_numVirtualOutputs;

    // Virtual Channels IPC (ASMRTOP WDM IPC)
    AsmrtopIpcChannel m_playIpc[NUM_VIRTUAL_PAIRS];  // WDM -> DAW
    AsmrtopIpcChannel m_capIpc[NUM_VIRTUAL_PAIRS];   // DAW -> WDM
    
    // Callbacks runner loop
    static void bufferSwitchBridge(long bufferIndex, void* userData);
    void onBufferSwitch(long bufferIndex);
};
