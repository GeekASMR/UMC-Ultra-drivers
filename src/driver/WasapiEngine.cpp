/*
 * WasapiEngine - WASAPI audio engine implementation
 */

#include "WasapiEngine.h"
#include "AudioBuffer.h"
#include "../utils/Logger.h"
#include <avrt.h>
#include <cmath>
#include <ks.h>
#include <ksmedia.h>

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")

#define LOG_MODULE "WasapiEngine"

// Use GUIDs from ksmedia.h - KSDATAFORMAT_SUBTYPE_PCM and KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
// are already defined there.

WasapiEngine::WasapiEngine()
    : m_captureDevice(nullptr)
    , m_captureClient(nullptr)
    , m_captureService(nullptr)
    , m_renderDevice(nullptr)
    , m_renderClient(nullptr)
    , m_renderService(nullptr)
    , m_sampleRate(44100)
    , m_channelCount(2)
    , m_bytesPerSample(4)
    , m_bufferFrames(256)
    , m_actualBufferFrames(256)
    , m_latencyFrames(0)
    , m_audioBuffer(nullptr)
    , m_audioThread(nullptr)
    , m_captureEvent(nullptr)
    , m_renderEvent(nullptr)
    , m_stopEvent(nullptr)
    , m_running(false)
    , m_currentBufferIndex(0)
    , m_captureInitialized(false)
    , m_renderInitialized(false)
    , m_captureRingWritePos(0)
    , m_captureRingReadPos(0)
    , m_renderRingWritePos(0)
    , m_renderRingReadPos(0)
    , m_renderDiagCounter(0)
{
    memset(&m_captureFormat, 0, sizeof(m_captureFormat));
    memset(&m_renderFormat, 0, sizeof(m_renderFormat));
}

WasapiEngine::~WasapiEngine() {
    shutdown();
}

bool WasapiEngine::initCapture(const std::wstring& deviceId, int sampleRate, int bufferFrames) {
    LOG_INFO(LOG_MODULE, "Initializing capture: rate=%d, frames=%d", sampleRate, bufferFrames);

    m_sampleRate = sampleRate;
    m_bufferFrames = bufferFrames;

    // Create capture event
    m_captureEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_captureEvent) {
        LOG_ERROR(LOG_MODULE, "Failed to create capture event");
        return false;
    }

    // Get device
    IMMDeviceEnumerator* pEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&pEnumerator);

    if (FAILED(hr)) {
        LOG_ERROR(LOG_MODULE, "Failed to create device enumerator: 0x%08X", hr);
        return false;
    }

    if (deviceId.empty()) {
        hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &m_captureDevice);
    } else {
        hr = pEnumerator->GetDevice(deviceId.c_str(), &m_captureDevice);
    }
    pEnumerator->Release();

    if (FAILED(hr) || !m_captureDevice) {
        LOG_ERROR(LOG_MODULE, "Failed to get capture device: 0x%08X", hr);
        return false;
    }

    if (!initAudioClient(m_captureDevice, true, sampleRate, bufferFrames)) {
        return false;
    }

    m_captureInitialized = true;

    // Allocate capture ring buffer
    m_captureRing.resize(m_channelCount);
    for (int ch = 0; ch < m_channelCount; ch++) {
        m_captureRing[ch].resize(CAPTURE_RING_SIZE, 0.0f);
    }
    m_captureRingWritePos = 0;
    m_captureRingReadPos = 0;
    LOG_INFO(LOG_MODULE, "Capture ring buffer allocated: %d channels x %d frames", m_channelCount, CAPTURE_RING_SIZE);

    LOG_INFO(LOG_MODULE, "Capture initialized successfully (actual buffer: %d frames)", m_actualBufferFrames);
    return true;
}

bool WasapiEngine::initRender(const std::wstring& deviceId, int sampleRate, int bufferFrames) {
    LOG_INFO(LOG_MODULE, "Initializing render: rate=%d, frames=%d", sampleRate, bufferFrames);

    m_sampleRate = sampleRate;
    m_bufferFrames = bufferFrames;

    // Create render event
    m_renderEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_renderEvent) {
        LOG_ERROR(LOG_MODULE, "Failed to create render event");
        return false;
    }

    // Get device
    IMMDeviceEnumerator* pEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&pEnumerator);

    if (FAILED(hr)) {
        LOG_ERROR(LOG_MODULE, "Failed to create device enumerator: 0x%08X", hr);
        return false;
    }

    if (deviceId.empty()) {
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_renderDevice);
    } else {
        hr = pEnumerator->GetDevice(deviceId.c_str(), &m_renderDevice);
    }
    pEnumerator->Release();

    if (FAILED(hr) || !m_renderDevice) {
        LOG_ERROR(LOG_MODULE, "Failed to get render device: 0x%08X", hr);
        return false;
    }

    if (!initAudioClient(m_renderDevice, false, sampleRate, bufferFrames)) {
        return false;
    }

    m_renderInitialized = true;

    // Allocate render ring buffer (bridges ASIO buffer -> WASAPI buffer)
    m_renderRing.resize(m_channelCount);
    for (int ch = 0; ch < m_channelCount; ch++) {
        m_renderRing[ch].resize(RENDER_RING_SIZE, 0.0f);
    }
    m_renderRingWritePos = 0;
    m_renderRingReadPos = 0;
    LOG_INFO(LOG_MODULE, "Render ring buffer allocated: %d channels x %d frames", m_channelCount, RENDER_RING_SIZE);

    LOG_INFO(LOG_MODULE, "Render initialized successfully");
    return true;
}

bool WasapiEngine::initAudioClient(IMMDevice* device, bool isCapture, int sampleRate, int bufferFrames) {
    IAudioClient** ppClient = isCapture ? &m_captureClient : &m_renderClient;
    WAVEFORMATEXTENSIBLE& format = isCapture ? m_captureFormat : m_renderFormat;
    HANDLE event = isCapture ? m_captureEvent : m_renderEvent;

    HRESULT hr = device->Activate(
        __uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)ppClient);
    
    if (FAILED(hr)) {
        LOG_ERROR(LOG_MODULE, "Failed to activate audio client: 0x%08X", hr);
        return false;
    }

    IAudioClient* pClient = *ppClient;

    // Get the device's mix format
    WAVEFORMATEX* pMixFormat = nullptr;
    hr = pClient->GetMixFormat(&pMixFormat);
    if (FAILED(hr)) {
        LOG_ERROR(LOG_MODULE, "Failed to get mix format: 0x%08X", hr);
        return false;
    }

    LOG_DEBUG(LOG_MODULE, "Device mix format: %d channels, %d Hz, %d bits",
              pMixFormat->nChannels, pMixFormat->nSamplesPerSec, pMixFormat->wBitsPerSample);

    m_channelCount = pMixFormat->nChannels;

    // ============================================================
    // Use WASAPI Shared Mode
    // The official kernel driver (umc_audio.sys) already controls
    // the hardware, so we use shared mode to avoid conflicts.
    // ============================================================

    // In shared mode, we MUST use the device's mix format
    if (pMixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        memcpy(&format, pMixFormat, sizeof(WAVEFORMATEXTENSIBLE));
    } else {
        memset(&format, 0, sizeof(format));
        format.Format = *pMixFormat;
        format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        format.Samples.wValidBitsPerSample = pMixFormat->wBitsPerSample;
        format.dwChannelMask = (pMixFormat->nChannels == 2)
            ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)
            : SPEAKER_FRONT_CENTER;
        format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }

    m_sampleRate = format.Format.nSamplesPerSec;
    m_channelCount = format.Format.nChannels;
    m_bytesPerSample = format.Format.wBitsPerSample / 8;

    CoTaskMemFree(pMixFormat);

    // Shared mode: periodicity must be 0, use device's mix format
    REFERENCE_TIME requestedDuration = (REFERENCE_TIME)(10000000.0 * bufferFrames / m_sampleRate);

    hr = pClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        requestedDuration,
        0,  // periodicity must be 0 in shared mode
        (WAVEFORMATEX*)&format,
        nullptr);

    if (FAILED(hr)) {
        LOG_ERROR(LOG_MODULE, "Shared mode init failed (0x%08X), retrying with raw mix format...", hr);

        // Release and re-create client
        (*ppClient)->Release(); *ppClient = nullptr;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)ppClient);
        if (FAILED(hr)) return false;
        pClient = *ppClient;

        // Get fresh mix format and use it directly
        WAVEFORMATEX* pFreshMix = nullptr;
        pClient->GetMixFormat(&pFreshMix);
        if (!pFreshMix) return false;

        hr = pClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            requestedDuration,
            0,
            pFreshMix,
            nullptr);

        if (SUCCEEDED(hr)) {
            // Update our format info from the raw mix format
            m_sampleRate = pFreshMix->nSamplesPerSec;
            m_channelCount = pFreshMix->nChannels;
            m_bytesPerSample = pFreshMix->wBitsPerSample / 8;
            if (pFreshMix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                memcpy(&format, pFreshMix, sizeof(WAVEFORMATEXTENSIBLE));
            }
        }
        CoTaskMemFree(pFreshMix);

        if (FAILED(hr)) {
            LOG_ERROR(LOG_MODULE, "Shared mode also failed with raw format: 0x%08X", hr);
            return false;
        }
    }

    LOG_INFO(LOG_MODULE, "Using shared mode (%s): rate=%d, channels=%d, bits=%d",
             isCapture ? "capture" : "render",
             m_sampleRate, m_channelCount, m_bytesPerSample * 8);

    // Set event handle
    hr = pClient->SetEventHandle(event);
    if (FAILED(hr)) {
        LOG_ERROR(LOG_MODULE, "Failed to set event handle: 0x%08X", hr);
        return false;
    }

    // Get actual buffer size
    UINT32 actualFrames = 0;
    pClient->GetBufferSize(&actualFrames);
    m_actualBufferFrames = actualFrames;
    LOG_INFO(LOG_MODULE, "Actual buffer size: %d frames (%.1f ms)",
             actualFrames, 1000.0 * actualFrames / m_sampleRate);

    // Calculate latency
    REFERENCE_TIME latency = 0;
    pClient->GetStreamLatency(&latency);
    m_latencyFrames = (int)(latency * m_sampleRate / 10000000);
    LOG_INFO(LOG_MODULE, "Stream latency: %d frames (%.1f ms)",
             m_latencyFrames, 1000.0 * m_latencyFrames / m_sampleRate);

    // Get the capture/render service
    if (isCapture) {
        hr = pClient->GetService(__uuidof(IAudioCaptureClient), (void**)&m_captureService);
    } else {
        hr = pClient->GetService(__uuidof(IAudioRenderClient), (void**)&m_renderService);
    }

    if (FAILED(hr)) {
        LOG_ERROR(LOG_MODULE, "Failed to get audio service: 0x%08X", hr);
        return false;
    }

    return true;
}

bool WasapiEngine::start() {
    if (m_running) {
        LOG_WARN(LOG_MODULE, "Engine already running");
        return true;
    }

    LOG_INFO(LOG_MODULE, "Starting audio engine...");

    // Create stop event
    m_stopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    // Pre-fill render buffer with a SMALL amount of silence (just 1 period)
    // Don't fill the entire buffer — we need room for real audio data.
    if (m_renderInitialized && m_renderService) {
        // Only pre-fill half the buffer to leave room for real data
        UINT32 preFillFrames = m_actualBufferFrames / 2;
        if (preFillFrames > 0) {
            BYTE* pData = nullptr;
            HRESULT hr = m_renderService->GetBuffer(preFillFrames, &pData);
            if (SUCCEEDED(hr) && pData) {
                memset(pData, 0, preFillFrames * m_channelCount * m_bytesPerSample);
                m_renderService->ReleaseBuffer(preFillFrames, AUDCLNT_BUFFERFLAGS_SILENT);
                LOG_INFO(LOG_MODULE, "Pre-filled render buffer with %d silent frames", preFillFrames);
            }
        }
    }

    m_running = true;
    m_currentBufferIndex = 0;
    m_renderDiagCounter = 0;

    // Start the audio thread
    m_audioThread = CreateThread(nullptr, 0, audioThreadProc, this, 0, nullptr);
    if (!m_audioThread) {
        LOG_ERROR(LOG_MODULE, "Failed to create audio thread");
        m_running = false;
        return false;
    }

    // Start the WASAPI streams
    if (m_captureClient) {
        HRESULT hr = m_captureClient->Start();
        if (FAILED(hr)) {
            LOG_ERROR(LOG_MODULE, "Failed to start capture: 0x%08X", hr);
            stop();
            return false;
        }
    }

    if (m_renderClient) {
        HRESULT hr = m_renderClient->Start();
        if (FAILED(hr)) {
            LOG_ERROR(LOG_MODULE, "Failed to start render: 0x%08X", hr);
            stop();
            return false;
        }
    }

    LOG_INFO(LOG_MODULE, "Audio engine started (render=%s, capture=%s)",
             m_renderInitialized ? "YES" : "NO",
             m_captureInitialized ? "YES" : "NO");
    return true;
}

bool WasapiEngine::startStreamOnly() {
    if (m_running) {
        LOG_WARN(LOG_MODULE, "Engine already running");
        return true;
    }

    LOG_INFO(LOG_MODULE, "Starting WASAPI stream only (no audio thread)...");

    m_running = true;
    m_currentBufferIndex = 0;

    // Start the WASAPI streams without creating audio thread
    if (m_captureClient) {
        HRESULT hr = m_captureClient->Start();
        if (FAILED(hr)) {
            LOG_ERROR(LOG_MODULE, "Failed to start capture stream: 0x%08X", hr);
            m_running = false;
            return false;
        }
    }

    if (m_renderClient) {
        HRESULT hr = m_renderClient->Start();
        if (FAILED(hr)) {
            LOG_ERROR(LOG_MODULE, "Failed to start render stream: 0x%08X", hr);
            m_running = false;
            return false;
        }
    }

    LOG_INFO(LOG_MODULE, "WASAPI stream started (externally driven)");
    return true;
}

bool WasapiEngine::stop() {
    if (!m_running) return true;

    LOG_INFO(LOG_MODULE, "Stopping audio engine...");

    m_running = false;

    // Signal the stop event
    if (m_stopEvent) {
        SetEvent(m_stopEvent);
    }

    // Wait for audio thread to finish
    if (m_audioThread) {
        WaitForSingleObject(m_audioThread, 5000);
        CloseHandle(m_audioThread);
        m_audioThread = nullptr;
    }

    // Stop WASAPI streams
    if (m_captureClient) {
        m_captureClient->Stop();
        m_captureClient->Reset();
    }

    if (m_renderClient) {
        m_renderClient->Stop();
        m_renderClient->Reset();
    }

    // Clean up stop event
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }

    LOG_INFO(LOG_MODULE, "Audio engine stopped");
    return true;
}

void WasapiEngine::shutdown() {
    stop();

    if (m_captureService) { m_captureService->Release(); m_captureService = nullptr; }
    if (m_captureClient) { m_captureClient->Release(); m_captureClient = nullptr; }
    if (m_captureDevice) { m_captureDevice->Release(); m_captureDevice = nullptr; }

    if (m_renderService) { m_renderService->Release(); m_renderService = nullptr; }
    if (m_renderClient) { m_renderClient->Release(); m_renderClient = nullptr; }
    if (m_renderDevice) { m_renderDevice->Release(); m_renderDevice = nullptr; }

    if (m_captureEvent) { CloseHandle(m_captureEvent); m_captureEvent = nullptr; }
    if (m_renderEvent) { CloseHandle(m_renderEvent); m_renderEvent = nullptr; }

    m_captureInitialized = false;
    m_renderInitialized = false;
}

DWORD WINAPI WasapiEngine::audioThreadProc(LPVOID param) {
    WasapiEngine* engine = (WasapiEngine*)param;
    
    // Initialize COM for this thread
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Boost thread priority for real-time audio
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (hTask) {
        AvSetMmThreadPriority(hTask, AVRT_PRIORITY_CRITICAL);
        LOG_DEBUG(LOG_MODULE, "Thread priority set to Pro Audio/Critical");
    } else {
        // Fallback to high thread priority
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        LOG_WARN(LOG_MODULE, "AvSetMmThreadCharacteristics failed, using TIME_CRITICAL");
    }

    engine->audioLoop();

    if (hTask) {
        AvRevertMmThreadCharacteristics(hTask);
    }

    CoUninitialize();
    return 0;
}

void WasapiEngine::audioLoop() {
    LOG_INFO(LOG_MODULE, "Audio loop started (render=%s, capture=%s, bufSize=%d, actualBuf=%d)",
             m_renderInitialized ? "yes" : "no",
             m_captureInitialized ? "yes" : "no",
             m_bufferFrames, m_actualBufferFrames);

    HANDLE waitHandles[3] = { m_stopEvent, nullptr, nullptr };
    int handleCount = 1;

    if (m_captureEvent) {
        waitHandles[handleCount++] = m_captureEvent;
    }
    if (m_renderEvent) {
        waitHandles[handleCount++] = m_renderEvent;
    }

    const int ringMask = RENDER_RING_SIZE - 1;
    int asioBufferSize = m_bufferFrames;
    int loopCount = 0;

    while (m_running) {
        // Wait for events (capture/render/stop)
        DWORD waitResult = WaitForMultipleObjects(handleCount, waitHandles, FALSE, 100);

        if (waitResult == WAIT_OBJECT_0) {
            // Stop event
            break;
        }

        if (!m_running) break;

        // === Phase 1: Run enough ASIO callbacks to fill render ring buffer ===
        // In shared mode, WASAPI needs more frames per event than our ASIO buffer.
        // We call the callback multiple times to produce enough data.
        if (m_renderInitialized && m_renderService) {
            UINT32 padding = 0;
            HRESULT hr = m_renderClient->GetCurrentPadding(&padding);
            if (FAILED(hr)) {
                LOG_ERROR(LOG_MODULE, "GetCurrentPadding failed: 0x%08X", hr);
                continue;
            }
            UINT32 wasapiNeed = m_actualBufferFrames - padding;

            // How many frames in render ring?
            int ringAvail = m_renderRingWritePos.load() - m_renderRingReadPos.load();

            // Diagnostic logging (first 20 iterations, then every 500th)
            if (loopCount < 20 || (loopCount % 500 == 0)) {
                LOG_DEBUG(LOG_MODULE, "Loop[%d]: padding=%d need=%d ringAvail=%d asioSize=%d",
                         loopCount, padding, wasapiNeed, ringAvail, asioBufferSize);
            }

            // Produce callbacks until we have enough data in the ring
            int needed = (int)wasapiNeed - ringAvail;
            int maxCallbacks = 8;  // safety limit
            int callbacksRun = 0;
            while (needed > 0 && maxCallbacks > 0 && m_running) {
                // Invoke the ASIO callback (captures input + DAW processes + routing)
                if (m_callback) {
                    m_callback(m_currentBufferIndex);
                }

                // Copy DAW output into render ring buffer
                writeOutputToRing(m_currentBufferIndex);

                // Toggle buffer index
                m_currentBufferIndex = 1 - m_currentBufferIndex;

                needed -= asioBufferSize;
                maxCallbacks--;
                callbacksRun++;
            }

            // === Phase 2: Drain render ring buffer -> WASAPI render ===
            ringAvail = m_renderRingWritePos.load() - m_renderRingReadPos.load();

            // Detect ring buffer overflow (writes far ahead of reads)
            if (ringAvail > RENDER_RING_SIZE / 2) {
                LOG_WARN(LOG_MODULE, "Render ring overflow! avail=%d, resetting", ringAvail);
                m_renderRingReadPos.store(m_renderRingWritePos.load() - asioBufferSize);
                ringAvail = asioBufferSize;
            }

            UINT32 framesToWrite = (UINT32)(std::min)((int)wasapiNeed, ringAvail);

            if (framesToWrite > 0) {
                BYTE* pData = nullptr;
                hr = m_renderService->GetBuffer(framesToWrite, &pData);
                if (SUCCEEDED(hr) && pData) {
                    int bytesPerFrame = m_channelCount * m_bytesPerSample;
                    int readPos = m_renderRingReadPos.load();

                    for (UINT32 s = 0; s < framesToWrite; s++) {
                        int ringIdx = (readPos + s) & ringMask;
                        BYTE* frameDst = pData + s * bytesPerFrame;

                        for (int ch = 0; ch < m_channelCount && ch < (int)m_renderRing.size(); ch++) {
                            float value = m_renderRing[ch][ringIdx];

                            // Clamp
                            if (value > 1.0f) value = 1.0f;
                            if (value < -1.0f) value = -1.0f;

                            BYTE* sampleDst = frameDst + ch * m_bytesPerSample;
                            switch (m_bytesPerSample) {
                                case 2:
                                    *(short*)sampleDst = (short)(value * 32767.0f);
                                    break;
                                case 3: {
                                    int iv = (int)(value * 8388607.0f);
                                    sampleDst[0] = (BYTE)(iv & 0xFF);
                                    sampleDst[1] = (BYTE)((iv >> 8) & 0xFF);
                                    sampleDst[2] = (BYTE)((iv >> 16) & 0xFF);
                                    break;
                                }
                                case 4:
                                    *(float*)sampleDst = value;
                                    break;
                            }
                        }
                    }
                    m_renderRingReadPos.store(readPos + (int)framesToWrite);
                    m_renderService->ReleaseBuffer(framesToWrite, 0);

                    if (loopCount < 20 || (loopCount % 500 == 0)) {
                        LOG_DEBUG(LOG_MODULE, "  Rendered %d frames (callbacks=%d, ringAfter=%d)",
                                 framesToWrite, callbacksRun,
                                 m_renderRingWritePos.load() - m_renderRingReadPos.load());
                    }
                } else {
                    LOG_WARN(LOG_MODULE, "GetBuffer(%d) failed: 0x%08X", framesToWrite, hr);
                }
            } else if (loopCount < 20) {
                LOG_DEBUG(LOG_MODULE, "  No frames to write (need=%d, avail=%d)",
                         wasapiNeed, ringAvail);
            }
        } else {
            // No render - just do capture + callback
            if (m_callback) {
                m_callback(m_currentBufferIndex);
            }
            m_currentBufferIndex = 1 - m_currentBufferIndex;
        }

        loopCount++;
    }

    LOG_INFO(LOG_MODULE, "Audio loop ended (total iterations: %d)", loopCount);
}

void WasapiEngine::writeOutputToRing(int bufferIndex) {
    if (!m_audioBuffer || m_renderRing.empty()) return;

    const int ringMask = RENDER_RING_SIZE - 1;
    long bufferSize = m_audioBuffer->getBufferSize();
    int writePos = m_renderRingWritePos.load();

    // Diagnostic: check for non-zero data on first few writes
    bool hasNonZero = false;

    for (int ch = 0; ch < m_channelCount && ch < (int)m_renderRing.size(); ch++) {
        const float* srcBuffer = (const float*)m_audioBuffer->getBuffer(false, ch, bufferIndex);
        if (!srcBuffer) {
            // Zero-fill this channel's ring segment if source is null
            for (long s = 0; s < bufferSize; s++) {
                int ringIdx = (writePos + s) & ringMask;
                m_renderRing[ch][ringIdx] = 0.0f;
            }
            continue;
        }

        for (long s = 0; s < bufferSize; s++) {
            int ringIdx = (writePos + s) & ringMask;
            m_renderRing[ch][ringIdx] = srcBuffer[s];
            if (srcBuffer[s] != 0.0f) hasNonZero = true;
        }
    }
    m_renderRingWritePos.store(writePos + (int)bufferSize);

    // Log first few writes to verify data is flowing
    m_renderDiagCounter++;
    if (m_renderDiagCounter <= 30 || (m_renderDiagCounter % 1000 == 0)) {
        LOG_DEBUG(LOG_MODULE, "writeOutputToRing[%d]: bufIdx=%d size=%ld chans=%d data=%s ringWr=%d",
                 m_renderDiagCounter, bufferIndex, bufferSize, m_channelCount,
                 hasNonZero ? "NON-ZERO" : "silence", writePos + (int)bufferSize);
    }
}

WasapiEngine::BufferSizeRange WasapiEngine::queryBufferSizeRange(const std::wstring& deviceId, int sampleRate) {
    BufferSizeRange range = { 32, 4096, 256 };  // Defaults

    IMMDeviceEnumerator* pEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&pEnumerator);

    if (FAILED(hr)) return range;

    IMMDevice* pDevice = nullptr;
    if (deviceId.empty()) {
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    } else {
        hr = pEnumerator->GetDevice(deviceId.c_str(), &pDevice);
    }

    if (SUCCEEDED(hr) && pDevice) {
        IAudioClient* pClient = nullptr;
        hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pClient);
        
        if (SUCCEEDED(hr) && pClient) {
            // Try to get minimum period
            REFERENCE_TIME defaultPeriod = 0, minPeriod = 0;
            hr = pClient->GetDevicePeriod(&defaultPeriod, &minPeriod);
            
            if (SUCCEEDED(hr)) {
                range.minFrames = (int)(minPeriod * sampleRate / 10000000);
                range.defaultFrames = (int)(defaultPeriod * sampleRate / 10000000);
                
                // Round to power of 2
                if (range.minFrames < 32) range.minFrames = 32;
                if (range.defaultFrames < range.minFrames) range.defaultFrames = range.minFrames;

                LOG_DEBUG(LOG_MODULE, "Buffer range: min=%d, default=%d, max=%d",
                         range.minFrames, range.defaultFrames, range.maxFrames);
            }
            
            pClient->Release();
        }
        pDevice->Release();
    }

    pEnumerator->Release();
    return range;
}

int WasapiEngine::queryDeviceSampleRate(const std::wstring& deviceId) {
    IMMDeviceEnumerator* pEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&pEnumerator);
    if (FAILED(hr)) return 0;

    IMMDevice* pDevice = nullptr;
    if (deviceId.empty()) {
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    } else {
        hr = pEnumerator->GetDevice(deviceId.c_str(), &pDevice);
    }

    int sampleRate = 0;
    if (SUCCEEDED(hr) && pDevice) {
        IAudioClient* pClient = nullptr;
        hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pClient);
        if (SUCCEEDED(hr) && pClient) {
            WAVEFORMATEX* pMixFormat = nullptr;
            hr = pClient->GetMixFormat(&pMixFormat);
            if (SUCCEEDED(hr) && pMixFormat) {
                sampleRate = (int)pMixFormat->nSamplesPerSec;
                LOG_INFO(LOG_MODULE, "Device mix format rate: %d Hz", sampleRate);
                CoTaskMemFree(pMixFormat);
            }
            pClient->Release();
        }
        pDevice->Release();
    }
    pEnumerator->Release();
    return sampleRate;
}

bool WasapiEngine::isSampleRateSupported(const std::wstring& deviceId, int sampleRate) {
    // In shared mode, the mix format determines the sample rate.
    // We check if the device's mix format matches the requested rate.
    int deviceRate = queryDeviceSampleRate(deviceId);
    if (deviceRate > 0) {
        return (deviceRate == sampleRate);
    }
    // Fallback: assume common rates are supported
    return (sampleRate == 44100 || sampleRate == 48000 || sampleRate == 96000);
}

void WasapiEngine::captureInto(int bufferIndex) {
    if (!m_captureInitialized || !m_captureService || !m_audioBuffer) return;
    if (m_captureRing.empty()) return;

    const int ringMask = CAPTURE_RING_SIZE - 1;
    int bytesPerFrame = m_channelCount * m_bytesPerSample;

    // === Phase 1: Drain all WASAPI capture packets into ring buffer ===
    UINT32 packetLength = 0;
    m_captureService->GetNextPacketSize(&packetLength);

    while (packetLength > 0) {
        BYTE* pData = nullptr;
        UINT32 numFrames = 0;
        DWORD flags = 0;

        HRESULT hr = m_captureService->GetBuffer(&pData, &numFrames, &flags, nullptr, nullptr);
        if (FAILED(hr)) break;

        bool isSilence = (flags & AUDCLNT_BUFFERFLAGS_SILENT);

        if (!isSilence && pData && numFrames > 0) {
            int writePos = m_captureRingWritePos.load();

            for (UINT32 s = 0; s < numFrames; s++) {
                int ringIdx = (writePos + s) & ringMask;

                for (int ch = 0; ch < m_channelCount && ch < (int)m_captureRing.size(); ch++) {
                    const BYTE* sampleData = pData + s * bytesPerFrame + ch * m_bytesPerSample;
                    float value = 0.0f;

                    switch (m_bytesPerSample) {
                        case 2: {
                            short sv = *(const short*)sampleData;
                            value = sv / 32768.0f;
                            break;
                        }
                        case 3: {
                            int iv = sampleData[0] | (sampleData[1] << 8) | (sampleData[2] << 16);
                            if (iv & 0x800000) iv |= 0xFF000000;
                            value = iv / 8388608.0f;
                            break;
                        }
                        case 4: {
                            value = *(const float*)sampleData;
                            break;
                        }
                    }
                    m_captureRing[ch][ringIdx] = value;
                }
            }
            m_captureRingWritePos.store(writePos + (int)numFrames);
        }

        m_captureService->ReleaseBuffer(numFrames);
        m_captureService->GetNextPacketSize(&packetLength);
    }

    // === Phase 2: Read exactly bufferSize frames from ring into ASIO buffer ===
    long bufferSize = m_audioBuffer->getBufferSize();
    int readPos = m_captureRingReadPos.load();
    int writePos = m_captureRingWritePos.load();
    int available = writePos - readPos;

    // If we have enough data, read bufferSize frames
    if (available >= bufferSize) {
        for (int ch = 0; ch < m_channelCount; ch++) {
            float* dstBuffer = (float*)m_audioBuffer->getBuffer(true, ch, bufferIndex);
            if (!dstBuffer) continue;

            for (long s = 0; s < bufferSize; s++) {
                int ringIdx = (readPos + s) & ringMask;
                dstBuffer[s] = m_captureRing[ch][ringIdx];
            }
        }
        m_captureRingReadPos.store(readPos + (int)bufferSize);
    } else {
        // Not enough data yet - copy what we have and zero-fill the rest
        int framesToCopy = (available > 0) ? available : 0;
        for (int ch = 0; ch < m_channelCount; ch++) {
            float* dstBuffer = (float*)m_audioBuffer->getBuffer(true, ch, bufferIndex);
            if (!dstBuffer) continue;

            for (int s = 0; s < framesToCopy; s++) {
                int ringIdx = (readPos + s) & ringMask;
                dstBuffer[s] = m_captureRing[ch][ringIdx];
            }
            // Zero-fill remainder
            if (framesToCopy < bufferSize) {
                memset(&dstBuffer[framesToCopy], 0, (bufferSize - framesToCopy) * sizeof(float));
            }
        }
        if (framesToCopy > 0) {
            m_captureRingReadPos.store(readPos + framesToCopy);
        }
    }
}
