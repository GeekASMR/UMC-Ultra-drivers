/*
 * KsAudioStream.cpp - Kernel Streaming audio I/O implementation
 * 
 * Opens KS filter devices created by the official Thesycon driver (umc_audioks.sys)
 * and streams audio through KS pins using DeviceIoControl.
 */

#include "KsAudioStream.h"
#include "../utils/Logger.h"
#include <initguid.h>
#include <mmreg.h>       // WAVEFORMATEX, WAVE_FORMAT_PCM
#include <devioctl.h>    // CTL_CODE, FILE_DEVICE_KS
#include <ks.h>
#include <ksmedia.h>

#pragma comment(lib, "setupapi.lib")

// KS IOCTL codes (may not be defined in all SDK versions)
#ifndef IOCTL_KS_CREATE_PIN
#define IOCTL_KS_CREATE_PIN CTL_CODE(FILE_DEVICE_KS, 0x003, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif
#ifndef IOCTL_KS_READ_STREAM
#define IOCTL_KS_READ_STREAM CTL_CODE(FILE_DEVICE_KS, 0x004, METHOD_NEITHER, FILE_READ_ACCESS)
#endif
#ifndef IOCTL_KS_WRITE_STREAM
#define IOCTL_KS_WRITE_STREAM CTL_CODE(FILE_DEVICE_KS, 0x005, METHOD_NEITHER, FILE_WRITE_ACCESS)
#endif

// This struct may not be defined in all SDK versions
#ifndef _KSDATAFORMAT_WAVEFORMATEX_
#define _KSDATAFORMAT_WAVEFORMATEX_
typedef struct {
    KSDATAFORMAT DataFormat;
    WAVEFORMATEX WaveFormatEx;
} KSDATAFORMAT_WAVEFORMATEX, *PKSDATAFORMAT_WAVEFORMATEX;
#endif

#define LOG_MODULE "KsAudioStream"

// ============================================================================
// Constructor / Destructor
// ============================================================================

KsAudioStream::KsAudioStream()
    : m_filterHandle(INVALID_HANDLE_VALUE)
    , m_pinHandle(INVALID_HANDLE_VALUE)
    , m_channels(0)
    , m_sampleRate(44100)
    , m_bitsPerSample(24)
    , m_isCapture(false)
{
}

KsAudioStream::~KsAudioStream() {
    close();
}

// ============================================================================
// Device Discovery
// ============================================================================

int KsAudioStream::parseChannelCount(const std::wstring& path) {
    // Find "c_NN" pattern in path like "pcm_out_01_c_10_sd6_48000"
    size_t pos = path.find(L"_c_");
    if (pos == std::wstring::npos) return 0;
    pos += 3; // skip "_c_"
    
    int count = 0;
    while (pos < path.size() && path[pos] >= L'0' && path[pos] <= L'9') {
        count = count * 10 + (path[pos] - L'0');
        pos++;
    }
    return count;
}

std::wstring KsAudioStream::findDevice(bool capture, int preferredChannels, int sampleRate) {
    // Use KSCATEGORY_RENDER for output, KSCATEGORY_CAPTURE for input
    const GUID& category = capture ? KSCATEGORY_CAPTURE : KSCATEGORY_RENDER;
    
    HDEVINFO devInfo = SetupDiGetClassDevsW(&category, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return L"";
    
    SP_DEVICE_INTERFACE_DATA ifData;
    ifData.cbSize = sizeof(ifData);
    
    std::wstring bestPath;
    int bestChannels = 0;
    
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &category, i, &ifData); i++) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &size, nullptr);
        
        auto* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)malloc(size);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        
        if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, size, nullptr, nullptr)) {
            std::wstring path(detail->DevicePath);
            
            // Check if Behringer device
            if (path.find(L"vid_1397") != std::wstring::npos) {
                // Check if PCM audio (not MIDI)
                bool isPcm = capture ? 
                    (path.find(L"pcm_in_") != std::wstring::npos) :
                    (path.find(L"pcm_out_") != std::wstring::npos);
                
                if (isPcm) {
                    int channels = parseChannelCount(path);
                    
                    // Pick the device with the most channels (or exact match)
                    if (channels == preferredChannels) {
                        bestPath = path;
                        bestChannels = channels;
                        free(detail);
                        break; // Exact match
                    }
                    if (channels > bestChannels) {
                        bestPath = path;
                        bestChannels = channels;
                    }
                }
            }
        }
        free(detail);
    }
    
    SetupDiDestroyDeviceInfoList(devInfo);
    
    if (!bestPath.empty()) {
        LOG_INFO(LOG_MODULE, "%s device found: %d channels, path=%ls",
                 capture ? "Capture" : "Render", bestChannels, bestPath.c_str());
    }
    return bestPath;
}

// ============================================================================
// Open / Close
// ============================================================================

bool KsAudioStream::open(bool capture, int preferredChannels, int sampleRate) {
    if (m_filterHandle != INVALID_HANDLE_VALUE) close();
    
    m_isCapture = capture;
    m_sampleRate = sampleRate;
    
    // Find the device
    m_devicePath = findDevice(capture, preferredChannels, sampleRate);
    if (m_devicePath.empty()) {
        LOG_ERROR(LOG_MODULE, "No %s device found for Behringer VID_1397",
                  capture ? "capture" : "render");
        return false;
    }
    
    // Open the filter device
    m_filterHandle = CreateFileW(m_devicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr);
    
    if (m_filterHandle == INVALID_HANDLE_VALUE) {
        LOG_ERROR(LOG_MODULE, "Failed to open KS filter: error %d", GetLastError());
        return false;
    }
    
    m_channels = parseChannelCount(m_devicePath);
    LOG_INFO(LOG_MODULE, "KS filter opened: %s, %d channels",
             capture ? "capture" : "render", m_channels);
    return true;
}

void KsAudioStream::close() {
    if (m_pinHandle != INVALID_HANDLE_VALUE) {
        setState(KSSTATE_STOP);
        CloseHandle(m_pinHandle);
        m_pinHandle = INVALID_HANDLE_VALUE;
    }
    if (m_filterHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_filterHandle);
        m_filterHandle = INVALID_HANDLE_VALUE;
    }
    m_channels = 0;
}

// ============================================================================
// Pin Creation
// ============================================================================

bool KsAudioStream::createPin(int channels, int bitsPerSample, int sampleRate) {
    if (m_filterHandle == INVALID_HANDLE_VALUE) return false;
    if (m_pinHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pinHandle);
        m_pinHandle = INVALID_HANDLE_VALUE;
    }
    
    m_bitsPerSample = bitsPerSample;
    m_sampleRate = sampleRate;
    m_channels = channels;
    
    // Build WAVEFORMATEX
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = (WORD)channels;
    wfx.nSamplesPerSec = sampleRate;
    wfx.wBitsPerSample = (WORD)bitsPerSample;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    wfx.cbSize = 0;

    // Build KSPIN_CONNECT + KSDATAFORMAT_WAVEFORMATEX
    DWORD connectSize = sizeof(KSPIN_CONNECT) + sizeof(KSDATAFORMAT_WAVEFORMATEX);
    auto* connect = (KSPIN_CONNECT*)calloc(1, connectSize);
    
    // Pin 0 for render (Sink), Pin 0 for capture (Source)
    connect->PinId = 0;
    connect->PinToHandle = nullptr;
    connect->Priority.PriorityClass = KSPRIORITY_NORMAL;
    connect->Priority.PrioritySubClass = 1;
    connect->Interface.Set = KSINTERFACESETID_Standard;
    connect->Interface.Id = KSINTERFACE_STANDARD_STREAMING;
    connect->Medium.Set = KSMEDIUMSETID_Standard;
    connect->Medium.Id = KSMEDIUM_TYPE_ANYINSTANCE;
    
    auto* dataFormat = (KSDATAFORMAT_WAVEFORMATEX*)(connect + 1);
    dataFormat->DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
    dataFormat->DataFormat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    dataFormat->DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
    dataFormat->DataFormat.FormatSize = sizeof(KSDATAFORMAT_WAVEFORMATEX);
    dataFormat->DataFormat.SampleSize = wfx.nBlockAlign;
    dataFormat->WaveFormatEx = wfx;

    // Create the pin using IOCTL
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(m_filterHandle,
        IOCTL_KS_CREATE_PIN,
        connect, connectSize,
        &m_pinHandle, sizeof(m_pinHandle),
        &bytesReturned, nullptr);

    free(connect);

    if (!result || m_pinHandle == INVALID_HANDLE_VALUE) {
        LOG_ERROR(LOG_MODULE, "Failed to create KS pin: error %d", GetLastError());
        m_pinHandle = INVALID_HANDLE_VALUE;
        return false;
    }
    
    // Allocate conversion buffer
    int maxFrames = sampleRate; // 1 second max
    m_convBuffer.resize(maxFrames * channels * (bitsPerSample / 8));
    
    LOG_INFO(LOG_MODULE, "KS pin created: %d ch, %d-bit, %d Hz",
             channels, bitsPerSample, sampleRate);
    return true;
}

// ============================================================================
// Pin State Control
// ============================================================================

bool KsAudioStream::setState(KSSTATE state) {
    if (m_pinHandle == INVALID_HANDLE_VALUE) return false;
    
    KSPROPERTY prop;
    prop.Set = KSPROPSETID_Connection;
    prop.Id = KSPROPERTY_CONNECTION_STATE;
    prop.Flags = KSPROPERTY_TYPE_SET;
    
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(m_pinHandle,
        IOCTL_KS_PROPERTY,
        &prop, sizeof(prop),
        &state, sizeof(state),
        &bytesReturned, nullptr);
    
    if (!result) {
        LOG_ERROR(LOG_MODULE, "Failed to set pin state %d: error %d", state, GetLastError());
        return false;
    }
    
    const char* stateNames[] = {"STOP", "ACQUIRE", "PAUSE", "RUN"};
    LOG_INFO(LOG_MODULE, "Pin state -> %s", stateNames[state]);
    return true;
}

// ============================================================================  
// Audio I/O
// ============================================================================

int KsAudioStream::read(float* buffer, int frames, int channels) {
    if (m_pinHandle == INVALID_HANDLE_VALUE) return 0;
    
    int bytesPerFrame = channels * (m_bitsPerSample / 8);
    int totalBytes = frames * bytesPerFrame;
    
    if ((int)m_convBuffer.size() < totalBytes) {
        m_convBuffer.resize(totalBytes);
    }
    
    // Build KSSTREAM_HEADER
    KSSTREAM_HEADER header = {};
    header.Size = sizeof(KSSTREAM_HEADER);
    header.Data = m_convBuffer.data();
    header.FrameExtent = totalBytes;
    header.DataUsed = 0;
    
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    
    BOOL result = DeviceIoControl(m_pinHandle,
        IOCTL_KS_READ_STREAM,
        nullptr, 0,
        &header, sizeof(header),
        nullptr, &ov);
    
    DWORD bytesRead = 0;
    if (!result && GetLastError() == ERROR_IO_PENDING) {
        // Wait for completion (with 100ms timeout)
        if (WaitForSingleObject(ov.hEvent, 100) == WAIT_OBJECT_0) {
            GetOverlappedResult(m_pinHandle, &ov, &bytesRead, FALSE);
            bytesRead = header.DataUsed;
        } else {
            CancelIo(m_pinHandle);
            GetOverlappedResult(m_pinHandle, &ov, &bytesRead, TRUE);
            bytesRead = 0;
        }
    } else if (result) {
        bytesRead = header.DataUsed;
    }
    
    CloseHandle(ov.hEvent);
    
    if (bytesRead == 0) return 0;
    
    // Convert 24-bit PCM to float
    int framesRead = bytesRead / bytesPerFrame;
    const unsigned char* src = m_convBuffer.data();
    
    for (int f = 0; f < framesRead; f++) {
        for (int c = 0; c < channels; c++) {
            int idx = (f * channels + c) * 3; // 24-bit = 3 bytes
            // Sign-extend 24-bit to 32-bit
            int sample = (src[idx] | (src[idx + 1] << 8) | (src[idx + 2] << 16));
            if (sample & 0x800000) sample |= 0xFF000000; // sign extend
            buffer[f * channels + c] = (float)sample / 8388608.0f; // normalize to [-1, 1]
        }
    }
    
    return framesRead;
}

int KsAudioStream::write(const float* buffer, int frames, int channels) {
    if (m_pinHandle == INVALID_HANDLE_VALUE) return 0;
    
    int bytesPerFrame = channels * (m_bitsPerSample / 8);
    int totalBytes = frames * bytesPerFrame;
    
    if ((int)m_convBuffer.size() < totalBytes) {
        m_convBuffer.resize(totalBytes);
    }
    
    // Convert float to 24-bit PCM
    unsigned char* dst = m_convBuffer.data();
    for (int f = 0; f < frames; f++) {
        for (int c = 0; c < channels; c++) {
            float sample = buffer[f * channels + c];
            // Clamp to [-1, 1]
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            int intSample = (int)(sample * 8388607.0f);
            int idx = (f * channels + c) * 3;
            dst[idx] = (unsigned char)(intSample & 0xFF);
            dst[idx + 1] = (unsigned char)((intSample >> 8) & 0xFF);
            dst[idx + 2] = (unsigned char)((intSample >> 16) & 0xFF);
        }
    }
    
    // Build KSSTREAM_HEADER
    KSSTREAM_HEADER header = {};
    header.Size = sizeof(KSSTREAM_HEADER);
    header.Data = m_convBuffer.data();
    header.FrameExtent = totalBytes;
    header.DataUsed = totalBytes;
    
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    
    BOOL result = DeviceIoControl(m_pinHandle,
        IOCTL_KS_WRITE_STREAM,
        &header, sizeof(header),
        nullptr, 0,
        nullptr, &ov);
    
    DWORD bytesWritten = 0;
    if (!result && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 100) == WAIT_OBJECT_0) {
            GetOverlappedResult(m_pinHandle, &ov, &bytesWritten, FALSE);
            bytesWritten = header.DataUsed;
        } else {
            CancelIo(m_pinHandle);
            GetOverlappedResult(m_pinHandle, &ov, &bytesWritten, TRUE);
            bytesWritten = 0;
        }
    } else if (result) {
        bytesWritten = header.DataUsed;
    }
    
    CloseHandle(ov.hEvent);
    
    return bytesWritten / bytesPerFrame;
}
