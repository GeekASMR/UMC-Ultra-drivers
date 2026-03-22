/*
 * WasapiEngine - WASAPI 音频引擎
 *
 * 管理 WASAPI Exclusive Mode 音频流，提供低延迟音频 I/O。
 * 使用事件驱动模型，在专用线程中处理音频回调。
 */

#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <string>
#include <atomic>
#include <functional>
#include "../asio/asio.h"

// Forward declarations
class AudioBuffer;

// Callback type for buffer processing
typedef std::function<void(int bufferIndex)> BufferSwitchCallback;

class WasapiEngine {
public:
    WasapiEngine();
    ~WasapiEngine();

    // Initialize with a specific WASAPI device
    bool initCapture(const std::wstring& deviceId, int sampleRate, int bufferFrames);
    bool initRender(const std::wstring& deviceId, int sampleRate, int bufferFrames);

    // Start/Stop audio streaming
    bool start();
    bool stop();

    // Start only the WASAPI stream (no audio thread)
    // Used for capture when driven from render thread via captureInto()
    bool startStreamOnly();

    // Check if streaming
    bool isRunning() const { return m_running; }

    // Set the callback for buffer processing
    void setBufferSwitchCallback(BufferSwitchCallback callback) { m_callback = callback; }

    // Set the audio buffer for data transfer
    void setAudioBuffer(AudioBuffer* buffer) { m_audioBuffer = buffer; }

    // Set the current buffer index (for external synchronization)
    void setBufferIndex(int idx) { m_currentBufferIndex = idx; }

    // Perform a single capture pass: read available data into audio buffer
    // Called from the render thread for synchronized capture
    void captureInto(int bufferIndex);

    // Get WASAPI format information
    int getSampleRate() const { return m_sampleRate; }
    int getChannelCount() const { return m_channelCount; }
    int getBytesPerSample() const { return m_bytesPerSample; }
    int getBufferFrames() const { return m_bufferFrames; }
    int getActualBufferFrames() const { return m_actualBufferFrames; }

    // Get latency in frames
    int getLatencyFrames() const { return m_latencyFrames; }

    // Query supported buffer sizes
    struct BufferSizeRange {
        int minFrames;
        int maxFrames;
        int defaultFrames;
    };
    
    static BufferSizeRange queryBufferSizeRange(const std::wstring& deviceId, int sampleRate);

    // Query device's actual sample rate (from mix format)
    static int queryDeviceSampleRate(const std::wstring& deviceId);

    // Query supported sample rates
    static bool isSampleRateSupported(const std::wstring& deviceId, int sampleRate);

    // Get the mix format for a device
    static WAVEFORMATEX* getMixFormat(const std::wstring& deviceId);

    // Cleanup
    void shutdown();

private:
    // Audio processing thread
    static DWORD WINAPI audioThreadProc(LPVOID param);
    void audioLoop();

    // Initialize IAudioClient with the given format
    bool initAudioClient(IMMDevice* device, bool isCapture, int sampleRate, int bufferFrames);

    // WASAPI objects
    IMMDevice*     m_captureDevice;
    IAudioClient*  m_captureClient;
    IAudioCaptureClient* m_captureService;
    
    IMMDevice*     m_renderDevice;
    IAudioClient*  m_renderClient;
    IAudioRenderClient* m_renderService;

    // Audio format
    WAVEFORMATEXTENSIBLE m_captureFormat;
    WAVEFORMATEXTENSIBLE m_renderFormat;
    
    int m_sampleRate;
    int m_channelCount;
    int m_bytesPerSample;
    int m_bufferFrames;       // Requested buffer frames
    int m_actualBufferFrames; // Actual WASAPI buffer frames
    int m_latencyFrames;

    // Audio buffer reference
    AudioBuffer* m_audioBuffer;

    // Callback
    BufferSwitchCallback m_callback;

    // Thread control
    HANDLE m_audioThread;
    HANDLE m_captureEvent;
    HANDLE m_renderEvent;
    HANDLE m_stopEvent;
    std::atomic<bool> m_running;

    // Buffer tracking
    std::atomic<int> m_currentBufferIndex;
    bool m_captureInitialized;
    bool m_renderInitialized;

    // Capture ring buffer (to bridge WASAPI chunk size -> ASIO buffer size)
    static const int CAPTURE_RING_SIZE = 65536;  // Must be power of 2
    std::vector<std::vector<float>> m_captureRing;  // [channel][CAPTURE_RING_SIZE]
    std::atomic<int> m_captureRingWritePos;
    std::atomic<int> m_captureRingReadPos;

    // Render ring buffer (to bridge ASIO buffer size -> WASAPI chunk size)
    static const int RENDER_RING_SIZE = 65536;  // Must be power of 2
    std::vector<std::vector<float>> m_renderRing;  // [channel][RENDER_RING_SIZE]
    std::atomic<int> m_renderRingWritePos;
    std::atomic<int> m_renderRingReadPos;

    // Write ASIO output data into render ring buffer (called after DAW callback)
    void writeOutputToRing(int bufferIndex);

    // Diagnostic counter for render operations
    int m_renderDiagCounter;
};
