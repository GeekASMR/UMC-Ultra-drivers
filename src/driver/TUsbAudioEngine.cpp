/*
 * TUsbAudioEngine.cpp - Hybrid Audio Engine
 * 
 * TUSBAUDIO API for device configuration.
 * WASAPI exclusive mode for audio streaming (kernel-level latency).
 */

#include "TUsbAudioEngine.h"
#include "WasapiEngine.h"
#include "DeviceDetector.h"
#include "AudioBuffer.h"
#include "../utils/Logger.h"
#include <algorithm>

#define LOG_MODULE "TUsbAudioEngine"

// ==========================================================================
// Constructor / Destructor
// ==========================================================================

TUsbAudioEngine::TUsbAudioEngine()
    : m_deviceHandle(nullptr)
    , m_inputChannelCount(0)
    , m_outputChannelCount(0)
    , m_sampleRate(44100)
    , m_bitsPerSample(24)
    , m_bufferFrames(256)
    , m_audioBuffer(nullptr)
    , m_audioThread(nullptr)
    , m_stopEvent(nullptr)
    , m_running(false)
    , m_currentBufferIndex(0)
    , m_captureEngine(nullptr)
    , m_renderEngine(nullptr)
    , m_initialized(false)
{
    memset(&m_deviceProps, 0, sizeof(m_deviceProps));
}

TUsbAudioEngine::~TUsbAudioEngine() {
    shutdown();
}

// ==========================================================================
// Initialization
// ==========================================================================

bool TUsbAudioEngine::init() {
    if (m_initialized) return true;

    LOG_INFO(LOG_MODULE, "Initializing TUSBAUDIO engine...");

    // 1. Load the API DLL
    // IMPORTANT: Must use the official install location - copies in System32 cause
    // enumeration failures (0xEE001006) due to missing registry configuration.
    
    if (!m_api.load()) {
        LOG_ERROR(LOG_MODULE, "Failed to locate and load any Thesycon TUSBAUDIO API DLL across the system");
        return false;
    }
    LOG_INFO(LOG_MODULE, "TUSBAUDIO API DLL loaded successfully");

    // 2. Check API version
    if (m_api.fn.TUSBAUDIO_GetApiVersion) {
        unsigned int ver = m_api.fn.TUSBAUDIO_GetApiVersion();
        LOG_INFO(LOG_MODULE, "API version: %d.%d", ver >> 16, ver & 0xFFFF);
    }

    // 3. Get driver info
    if (m_api.fn.TUSBAUDIO_GetDriverInfo) {
        TUsbAudioDriverInfo driverInfo;
        if (m_api.fn.TUSBAUDIO_GetDriverInfo(&driverInfo) == TSTATUS_SUCCESS) {
            LOG_INFO(LOG_MODULE, "Driver version: %d.%d.%d",
                     driverInfo.driverVersionMajor,
                     driverInfo.driverVersionMinor,
                     driverInfo.driverVersionSub);
        }
    }

    // 4. Enumerate devices (retry if locked by another instance)
    TUsbAudioStatus status = TSTATUS_GENERAL_ERROR;
    for (int retry = 0; retry < 5; retry++) {
        status = m_api.fn.TUSBAUDIO_EnumerateDevices();
        if (status == TSTATUS_SUCCESS) break;
        LOG_WARN(LOG_MODULE, "EnumerateDevices attempt %d failed: 0x%08X, retrying...", retry + 1, status);
        Sleep(500);
    }
    if (status != TSTATUS_SUCCESS) {
        LOG_ERROR(LOG_MODULE, "Failed to enumerate devices after retries: 0x%08X", status);
        return false;
    }

    unsigned int deviceCount = m_api.fn.TUSBAUDIO_GetDeviceCount();
    LOG_INFO(LOG_MODULE, "Found %d TUSBAUDIO device(s)", deviceCount);

    if (deviceCount == 0) {
        LOG_ERROR(LOG_MODULE, "No TUSBAUDIO devices found");
        return false;
    }

    // 5. Open the first device (or find Behringer UMC)
    int targetDevice = -1;
    for (unsigned int i = 0; i < deviceCount; i++) {
        TUsbAudioHandle tmpHandle = nullptr;
        status = m_api.fn.TUSBAUDIO_OpenDeviceByIndex(i, &tmpHandle);
        if (status != TSTATUS_SUCCESS || !tmpHandle) continue;

        TUsbAudioDeviceProperties props;
        memset(&props, 0, sizeof(props));
        if (m_api.fn.TUSBAUDIO_GetDeviceProperties) {
            m_api.fn.TUSBAUDIO_GetDeviceProperties(tmpHandle, &props);
        }

        LOG_INFO(LOG_MODULE, "Device[%d]: VID=0x%04X PID=0x%04X '%ls' by '%ls'",
                 i, props.usbVendorId, props.usbProductId,
                 props.productString, props.manufacturerString);

        // No more rigid Behringer VIP locking (0x1397). Just attach to whatever 
        // valid interface the loaded Thesycon DLL owns.
        if (targetDevice < 0) {
            targetDevice = (int)i;
            m_deviceHandle = tmpHandle;
            m_deviceProps = props;
        } else {
            m_api.fn.TUSBAUDIO_CloseDevice(tmpHandle);
        }
    }

    // If no Behringer device, use first device
    if (targetDevice < 0) {
        status = m_api.fn.TUSBAUDIO_OpenDeviceByIndex(0, &m_deviceHandle);
        if (status != TSTATUS_SUCCESS) {
            LOG_ERROR(LOG_MODULE, "Failed to open device: 0x%08X", status);
            return false;
        }
        if (m_api.fn.TUSBAUDIO_GetDeviceProperties) {
            m_api.fn.TUSBAUDIO_GetDeviceProperties(m_deviceHandle, &m_deviceProps);
        }
    }

    LOG_INFO(LOG_MODULE, "Opened device: %ls (VID=0x%04X, PID=0x%04X)",
             m_deviceProps.productString, m_deviceProps.usbVendorId, m_deviceProps.usbProductId);

    // 6. Query channel counts (use channel properties for accurate count)
    std::vector<TUsbAudioChannelProperty> initInChans, initOutChans;
    if (getInputChannelProperties(initInChans)) {
        m_inputChannelCount = (unsigned int)initInChans.size();
    } else {
        m_inputChannelCount = getInputChannelCount();
    }
    if (getOutputChannelProperties(initOutChans)) {
        m_outputChannelCount = (unsigned int)initOutChans.size();
    } else {
        m_outputChannelCount = getOutputChannelCount();
    }
    LOG_INFO(LOG_MODULE, "Channels: %d inputs, %d outputs", m_inputChannelCount, m_outputChannelCount);

    // 7. Query current sample rate
    m_sampleRate = getCurrentSampleRate();
    LOG_INFO(LOG_MODULE, "Current sample rate: %d Hz", m_sampleRate);

    // 8. Log supported sample rates
    std::vector<unsigned int> rates;
    if (getSupportedSampleRates(rates)) {
        std::string rateStr;
        for (auto r : rates) {
            if (!rateStr.empty()) rateStr += ", ";
            rateStr += std::to_string(r);
        }
        LOG_INFO(LOG_MODULE, "Supported sample rates: %s", rateStr.c_str());
    }

    // 9. Log stream formats
    std::vector<TUsbAudioStreamFormat> inFormats, outFormats;
    if (getInputFormats(inFormats)) {
        for (auto& f : inFormats) {
            LOG_INFO(LOG_MODULE, "Input format[%d]: %d-bit, %d channels, '%ls'",
                     f.formatId, f.bitsPerSample, f.numberOfChannels, f.formatNameString);
        }
    }
    if (getOutputFormats(outFormats)) {
        for (auto& f : outFormats) {
            LOG_INFO(LOG_MODULE, "Output format[%d]: %d-bit, %d channels, '%ls'",
                     f.formatId, f.bitsPerSample, f.numberOfChannels, f.formatNameString);
        }
    }

    // 10. Log channel names
    std::vector<TUsbAudioChannelProperty> inChans, outChans;
    if (getInputChannelProperties(inChans)) {
        for (auto& ch : inChans) {
            LOG_INFO(LOG_MODULE, "Input channel[%d]: '%ls'", ch.channelIndex, ch.channelNameString);
        }
    }
    if (getOutputChannelProperties(outChans)) {
        for (auto& ch : outChans) {
            LOG_INFO(LOG_MODULE, "Output channel[%d]: '%ls'", ch.channelIndex, ch.channelNameString);
        }
    }

    // 11. Find matching WASAPI device IDs for audio streaming
    {
        DeviceDetector detector;
        if (detector.enumerate() && detector.hasDevices()) {
            const BehringerDevice* dev = detector.getPreferredDevice();
            if (!dev) dev = detector.getDevice(0);
            if (dev) {
                m_wasapiCaptureId = dev->deviceId;
                m_wasapiRenderId = detector.findMatchingRenderDevice(*dev);
                LOG_INFO(LOG_MODULE, "WASAPI capture ID: %ls", m_wasapiCaptureId.c_str());
                LOG_INFO(LOG_MODULE, "WASAPI render ID: %ls", m_wasapiRenderId.c_str());
            }
        }
        if (m_wasapiCaptureId.empty()) {
            LOG_WARN(LOG_MODULE, "No WASAPI capture device found, input will be silent");
        }
        if (m_wasapiRenderId.empty()) {
            LOG_WARN(LOG_MODULE, "No WASAPI render device found, output will be silent");
        }
    }

    m_initialized = true;
    LOG_INFO(LOG_MODULE, "TUSBAUDIO engine initialized successfully");
    return true;
}

// ==========================================================================
// Device Info
// ==========================================================================

std::string TUsbAudioEngine::getDeviceName() const {
    if (m_deviceProps.productString[0] == 0) return "Unknown";
    // Convert wchar_t to string
    char name[256];
    WideCharToMultiByte(CP_UTF8, 0, m_deviceProps.productString, -1, name, sizeof(name), nullptr, nullptr);
    return std::string(name);
}

// ==========================================================================
// Sample Rate
// ==========================================================================

bool TUsbAudioEngine::getSupportedSampleRates(std::vector<unsigned int>& rates) {
    if (!m_deviceHandle || !m_api.fn.TUSBAUDIO_GetSupportedSampleRates) return false;

    unsigned int rateArray[32];
    unsigned int rateCount = 0;
    TUsbAudioStatus status = m_api.fn.TUSBAUDIO_GetSupportedSampleRates(
        m_deviceHandle, 32, rateArray, &rateCount);

    if (status != TSTATUS_SUCCESS) return false;

    rates.clear();
    for (unsigned int i = 0; i < rateCount; i++) {
        rates.push_back(rateArray[i]);
    }
    return true;
}

unsigned int TUsbAudioEngine::getCurrentSampleRate() {
    if (!m_deviceHandle || !m_api.fn.TUSBAUDIO_GetCurrentSampleRate) return 44100;

    unsigned int rate = 0;
    TUsbAudioStatus status = m_api.fn.TUSBAUDIO_GetCurrentSampleRate(m_deviceHandle, &rate);
    if (status == TSTATUS_SUCCESS && rate > 0) {
        m_sampleRate = rate;
        return rate;
    }
    return m_sampleRate;
}

bool TUsbAudioEngine::setSampleRate(unsigned int sampleRate) {
    if (!m_deviceHandle || !m_api.fn.TUSBAUDIO_SetSampleRate) return false;

    LOG_INFO(LOG_MODULE, "Setting sample rate to %d Hz", sampleRate);
    TUsbAudioStatus status = m_api.fn.TUSBAUDIO_SetSampleRate(m_deviceHandle, sampleRate);
    if (status == TSTATUS_SUCCESS) {
        m_sampleRate = sampleRate;
        LOG_INFO(LOG_MODULE, "Sample rate set to %d Hz", sampleRate);
        return true;
    }

    const char* errStr = m_api.fn.TUSBAUDIO_StatusCodeStringA ?
        m_api.fn.TUSBAUDIO_StatusCodeStringA(status) : "unknown";
    LOG_ERROR(LOG_MODULE, "Failed to set sample rate: %s (0x%08X)", errStr, status);
    return false;
}

// ==========================================================================
// Stream Formats
// ==========================================================================

bool TUsbAudioEngine::getInputFormats(std::vector<TUsbAudioStreamFormat>& formats) {
    if (!m_deviceHandle || !m_api.fn.TUSBAUDIO_GetSupportedStreamFormats) return false;

    TUsbAudioStreamFormat fmtArray[16];
    unsigned int fmtCount = 0;
    TUsbAudioStatus status = m_api.fn.TUSBAUDIO_GetSupportedStreamFormats(
        m_deviceHandle, 1, 16, fmtArray, &fmtCount);

    if (status != TSTATUS_SUCCESS) return false;

    formats.clear();
    for (unsigned int i = 0; i < fmtCount; i++) {
        formats.push_back(fmtArray[i]);
    }
    return true;
}

bool TUsbAudioEngine::getOutputFormats(std::vector<TUsbAudioStreamFormat>& formats) {
    if (!m_deviceHandle || !m_api.fn.TUSBAUDIO_GetSupportedStreamFormats) return false;

    TUsbAudioStreamFormat fmtArray[16];
    unsigned int fmtCount = 0;
    TUsbAudioStatus status = m_api.fn.TUSBAUDIO_GetSupportedStreamFormats(
        m_deviceHandle, 0, 16, fmtArray, &fmtCount);

    if (status != TSTATUS_SUCCESS) return false;

    formats.clear();
    for (unsigned int i = 0; i < fmtCount; i++) {
        formats.push_back(fmtArray[i]);
    }
    return true;
}

bool TUsbAudioEngine::setStreamFormat(bool isInput, unsigned int formatId) {
    if (!m_deviceHandle || !m_api.fn.TUSBAUDIO_SetCurrentStreamFormat) return false;

    TUsbAudioStatus status = m_api.fn.TUSBAUDIO_SetCurrentStreamFormat(
        m_deviceHandle, isInput ? 1 : 0, formatId);
    return status == TSTATUS_SUCCESS;
}

// ==========================================================================
// Channel Info
// ==========================================================================

unsigned int TUsbAudioEngine::getInputChannelCount() {
    if (m_initialized) return m_inputChannelCount;
    if (!m_deviceHandle || !m_api.fn.TUSBAUDIO_GetStreamChannelCount) return 0;

    unsigned int count = 0;
    m_api.fn.TUSBAUDIO_GetStreamChannelCount(m_deviceHandle, 1, &count);
    return count;
}

unsigned int TUsbAudioEngine::getOutputChannelCount() {
    if (m_initialized) return m_outputChannelCount;
    if (!m_deviceHandle || !m_api.fn.TUSBAUDIO_GetStreamChannelCount) return 0;

    unsigned int count = 0;
    m_api.fn.TUSBAUDIO_GetStreamChannelCount(m_deviceHandle, 0, &count);
    return count;
}

bool TUsbAudioEngine::getInputChannelProperties(std::vector<TUsbAudioChannelProperty>& channels) {
    if (!m_deviceHandle || !m_api.fn.TUSBAUDIO_GetChannelProperties) return false;

    TUsbAudioChannelProperty chArray[32];
    unsigned int chCount = 0;
    TUsbAudioStatus status = m_api.fn.TUSBAUDIO_GetChannelProperties(
        m_deviceHandle, 1, 32, chArray, &chCount);

    if (status != TSTATUS_SUCCESS) return false;

    channels.clear();
    for (unsigned int i = 0; i < chCount; i++) {
        channels.push_back(chArray[i]);
    }
    return true;
}

bool TUsbAudioEngine::getOutputChannelProperties(std::vector<TUsbAudioChannelProperty>& channels) {
    if (!m_deviceHandle || !m_api.fn.TUSBAUDIO_GetChannelProperties) return false;

    TUsbAudioChannelProperty chArray[32];
    unsigned int chCount = 0;
    TUsbAudioStatus status = m_api.fn.TUSBAUDIO_GetChannelProperties(
        m_deviceHandle, 0, 32, chArray, &chCount);

    if (status != TSTATUS_SUCCESS) return false;

    channels.clear();
    for (unsigned int i = 0; i < chCount; i++) {
        channels.push_back(chArray[i]);
    }
    return true;
}

// ==========================================================================
// Buffer Size
// ==========================================================================

bool TUsbAudioEngine::getAsioBufferSizes(std::vector<unsigned int>& sizes_us, unsigned int* currentIndex) {
    if (!m_api.fn.TUSBAUDIO_GetAsioBufferSizeSet) return false;

    unsigned int sizeArray[64];
    unsigned int sizeCount = 0;
    unsigned int curIdx = 0;
    TUsbAudioStatus status = m_api.fn.TUSBAUDIO_GetAsioBufferSizeSet(
        64, sizeArray, &sizeCount, &curIdx);

    if (status != TSTATUS_SUCCESS) return false;

    sizes_us.clear();
    for (unsigned int i = 0; i < sizeCount; i++) {
        sizes_us.push_back(sizeArray[i]);
    }
    if (currentIndex) *currentIndex = curIdx;
    return true;
}

bool TUsbAudioEngine::setAsioBufferSize(unsigned int size_us) {
    if (!m_api.fn.TUSBAUDIO_SetAsioBufferSize) return false;

    TUsbAudioStatus status = m_api.fn.TUSBAUDIO_SetAsioBufferSize(size_us);
    return status == TSTATUS_SUCCESS;
}

unsigned int TUsbAudioEngine::getAsioBufferSize_us() {
    if (!m_api.fn.TUSBAUDIO_GetAsioBufferSize) return 0;

    unsigned int size = 0;
    m_api.fn.TUSBAUDIO_GetAsioBufferSize(&size);
    return size;
}

// ==========================================================================
// Audio Streaming via WASAPI Exclusive Mode
// ==========================================================================

bool TUsbAudioEngine::start(int bufferFrames) {
    if (m_running) return true;

    LOG_INFO(LOG_MODULE, "Starting audio streaming (buffer: %d frames)...", bufferFrames);
    m_bufferFrames = bufferFrames;

    // NOTE: Do NOT call TUSBAUDIO_SetDeviceStreamingMode here.
    // The kernel driver (umc_audio.sys) manages its own streaming state.
    // Calling SetDeviceStreamingMode while WASAPI accesses the device
    // causes a race condition in the kernel driver, resulting in BSOD.
    // WASAPI shared mode works through the standard Windows audio stack
    // which already cooperates with the kernel driver.

    // Initialize WASAPI capture
    if (!m_wasapiCaptureId.empty()) {
        m_captureEngine = new WasapiEngine();
        m_captureEngine->setAudioBuffer(m_audioBuffer);
        if (!m_captureEngine->initCapture(m_wasapiCaptureId, m_sampleRate, bufferFrames)) {
            LOG_WARN(LOG_MODULE, "WASAPI capture init failed, input will be silent");
            delete m_captureEngine;
            m_captureEngine = nullptr;
        } else {
            LOG_INFO(LOG_MODULE, "WASAPI capture ready: %d ch, %d Hz, %d/%d frames",
                     m_captureEngine->getChannelCount(), m_captureEngine->getSampleRate(),
                     bufferFrames, m_captureEngine->getActualBufferFrames());
        }
    }

    // Initialize WASAPI render
    if (!m_wasapiRenderId.empty()) {
        m_renderEngine = new WasapiEngine();
        m_renderEngine->setAudioBuffer(m_audioBuffer);
        m_renderEngine->setBufferSwitchCallback(m_callback);
        if (!m_renderEngine->initRender(m_wasapiRenderId, m_sampleRate, bufferFrames)) {
            LOG_WARN(LOG_MODULE, "WASAPI render init failed, output will be silent");
            delete m_renderEngine;
            m_renderEngine = nullptr;
        } else {
            LOG_INFO(LOG_MODULE, "WASAPI render ready: %d ch, %d Hz, %d/%d frames",
                     m_renderEngine->getChannelCount(), m_renderEngine->getSampleRate(),
                     bufferFrames, m_renderEngine->getActualBufferFrames());
        }
    }

    // If we have both, link capture to render for synchronized operation
    if (m_renderEngine && m_captureEngine) {
        // Start capture stream only (no thread - driven from render)
        if (!m_captureEngine->startStreamOnly()) {
            LOG_WARN(LOG_MODULE, "Capture stream start failed");
        }
    }

    // Start render engine (this drives the audio callback loop)
    if (m_renderEngine) {
        // Wrap callback to also capture
        auto originalCallback = m_callback;
        WasapiEngine* capEng = m_captureEngine;
        m_renderEngine->setBufferSwitchCallback([originalCallback, capEng](int bufIdx) {
            // Read captured data first
            if (capEng) {
                capEng->captureInto(bufIdx);
            }
            // Then call DAW (DAW reads inputs, writes outputs, routing happens)
            if (originalCallback) {
                originalCallback(bufIdx);
            }
        });
        
        if (!m_renderEngine->start()) {
            LOG_ERROR(LOG_MODULE, "Failed to start WASAPI render stream");
            // Fall back to timer-based
        } else {
            m_running = true;
            LOG_INFO(LOG_MODULE, "Audio streaming started (WASAPI shared mode)");
            return true;
        }
    }

    // Fallback: timer-based callback (no actual audio I/O)
    LOG_WARN(LOG_MODULE, "Falling back to timer-based callback (no WASAPI)");
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent) return false;

    m_running = true;
    m_currentBufferIndex = 0;
    m_audioThread = CreateThread(nullptr, 0, audioThreadProc, this, 0, nullptr);
    if (!m_audioThread) {
        m_running = false;
        CloseHandle(m_stopEvent); m_stopEvent = nullptr;
        return false;
    }
    SetThreadPriority(m_audioThread, THREAD_PRIORITY_TIME_CRITICAL);
    LOG_INFO(LOG_MODULE, "Audio streaming started (timer fallback)");
    return true;
}

bool TUsbAudioEngine::stop() {
    if (!m_running) return true;

    LOG_INFO(LOG_MODULE, "Stopping audio streaming...");
    m_running = false;

    // Stop WASAPI engines
    if (m_renderEngine) {
        m_renderEngine->stop();
        m_renderEngine->shutdown();
        delete m_renderEngine;
        m_renderEngine = nullptr;
    }
    if (m_captureEngine) {
        m_captureEngine->stop();
        m_captureEngine->shutdown();
        delete m_captureEngine;
        m_captureEngine = nullptr;
    }

    // Stop timer thread (fallback)
    if (m_stopEvent) {
        SetEvent(m_stopEvent);
    }
    if (m_audioThread) {
        WaitForSingleObject(m_audioThread, 3000);
        CloseHandle(m_audioThread);
        m_audioThread = nullptr;
    }
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }

    LOG_INFO(LOG_MODULE, "Audio streaming stopped");
    return true;
}

DWORD WINAPI TUsbAudioEngine::audioThreadProc(LPVOID param) {
    auto* engine = static_cast<TUsbAudioEngine*>(param);
    engine->audioLoop();
    return 0;
}

void TUsbAudioEngine::audioLoop() {
    LOG_INFO(LOG_MODULE, "Audio loop started (timer fallback)");

    HANDLE waitTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!waitTimer) return;

    double periodMs = (double)m_bufferFrames / (double)m_sampleRate * 1000.0;
    LARGE_INTEGER dueTime;
    dueTime.QuadPart = -(LONGLONG)(periodMs * 10000.0);
    SetWaitableTimer(waitTimer, &dueTime, (LONG)periodMs, nullptr, nullptr, FALSE);

    while (m_running) {
        HANDLE handles[] = { m_stopEvent, waitTimer };
        DWORD result = WaitForMultipleObjects(2, handles, FALSE, (DWORD)(periodMs * 3));
        if (result == WAIT_OBJECT_0) break;
        if (!m_running) break;

        if (m_callback) m_callback(m_currentBufferIndex);
        m_currentBufferIndex = 1 - m_currentBufferIndex;
    }

    CancelWaitableTimer(waitTimer);
    CloseHandle(waitTimer);
    LOG_INFO(LOG_MODULE, "Audio loop ended");
}

// ==========================================================================
// Shutdown
// ==========================================================================

void TUsbAudioEngine::shutdown() {
    stop();

    if (m_deviceHandle) {
        if (m_api.fn.TUSBAUDIO_CloseDevice) {
            m_api.fn.TUSBAUDIO_CloseDevice(m_deviceHandle);
        }
        m_deviceHandle = nullptr;
    }

    m_api.unload();
    m_initialized = false;
}
