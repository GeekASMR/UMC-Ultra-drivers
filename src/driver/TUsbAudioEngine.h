/*
 * TUsbAudioEngine.h - Hybrid Audio Engine
 * 
 * Architecture:
 *   TUSBAUDIO API → device configuration (channels, sample rates, formats)
 *   WASAPI Exclusive → actual audio streaming (same latency as KS)
 *
 * WASAPI exclusive mode bypasses the Windows audio engine entirely.
 * At the kernel level it uses KS, same as the official driver.
 */

#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include "TUsbAudioApi.h"

class AudioBuffer;
class WasapiEngine;
class DeviceDetector;

class TUsbAudioEngine {
public:
    using BufferSwitchCallback = std::function<void(int bufferIndex)>;

    TUsbAudioEngine();
    ~TUsbAudioEngine();

    // Initialize - load API DLL, enumerate devices, open device
    bool init();

    // Get device info after init
    const TUsbAudioDeviceProperties& getDeviceProperties() const { return m_deviceProps; }
    std::string getDeviceName() const;

    // Sample rate management
    bool getSupportedSampleRates(std::vector<unsigned int>& rates);
    unsigned int getCurrentSampleRate();
    bool setSampleRate(unsigned int sampleRate);

    // Stream format management
    bool getInputFormats(std::vector<TUsbAudioStreamFormat>& formats);
    bool getOutputFormats(std::vector<TUsbAudioStreamFormat>& formats);
    bool setStreamFormat(bool isInput, unsigned int formatId);

    // Channel info
    unsigned int getInputChannelCount();
    unsigned int getOutputChannelCount();
    bool getInputChannelProperties(std::vector<TUsbAudioChannelProperty>& channels);
    bool getOutputChannelProperties(std::vector<TUsbAudioChannelProperty>& channels);

    // Buffer size management
    bool getAsioBufferSizes(std::vector<unsigned int>& sizes_us, unsigned int* currentIndex = nullptr);
    bool setAsioBufferSize(unsigned int size_us);
    unsigned int getAsioBufferSize_us();

    // Audio streaming
    bool start(int bufferFrames);
    bool stop();
    bool isRunning() const { return m_running; }

    // Set callbacks and buffer
    void setAudioBuffer(AudioBuffer* buffer) { m_audioBuffer = buffer; }
    void setBufferSwitchCallback(BufferSwitchCallback cb) { m_callback = cb; }

    // Get API and device handle for advanced use
    TUsbAudioApi& getApi() { return m_api; }
    TUsbAudioHandle getDeviceHandle() const { return m_deviceHandle; }

    // Cleanup
    void shutdown();

private:
    // Audio processing thread (driven by WASAPI render events)
    static DWORD WINAPI audioThreadProc(LPVOID param);
    void audioLoop();

    // TUSBAUDIO API (device config)
    TUsbAudioApi m_api;
    TUsbAudioHandle m_deviceHandle;
    TUsbAudioDeviceProperties m_deviceProps;

    // Device info
    unsigned int m_inputChannelCount;
    unsigned int m_outputChannelCount;
    unsigned int m_sampleRate;
    unsigned int m_bitsPerSample;
    int m_bufferFrames;

    // Audio buffer reference
    AudioBuffer* m_audioBuffer;

    // Callback
    BufferSwitchCallback m_callback;

    // Thread control
    HANDLE m_audioThread;
    HANDLE m_stopEvent;
    std::atomic<bool> m_running;
    std::atomic<int> m_currentBufferIndex;

    // WASAPI streaming
    WasapiEngine* m_captureEngine;  // WASAPI capture (microphone input)
    WasapiEngine* m_renderEngine;   // WASAPI render (speaker output)
    std::wstring m_wasapiCaptureId;
    std::wstring m_wasapiRenderId;

    bool m_initialized;
};
