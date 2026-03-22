/*
 * SharedMemoryBridge - 用户态共享内存 IPC 实现
 *
 * 改造自 ASMRTOP Driver V2 (公版) AsmrtopKernelIPC.h
 * 内核态 ZwCreateSection -> 用户态 CreateFileMappingW
 * 内核态 MmMapViewInSystemSpace -> 用户态 MapViewOfFile
 */

#include "SharedMemoryBridge.h"
#include "../utils/Logger.h"
#include <cstring>
#include <algorithm>

#define LOG_MODULE "SharedMemoryBridge"

// ============================================================
// SharedMemoryChannel
// ============================================================

SharedMemoryChannel::SharedMemoryChannel()
    : m_hMapping(NULL)
    , m_pBuffer(nullptr)
{
}

SharedMemoryChannel::~SharedMemoryChannel() {
    close();
}

std::wstring SharedMemoryChannel::buildName(IpcDirection direction, int channelId) {
    const wchar_t* dirStr = L"UNKNOWN";
    switch (direction) {
        case IpcDirection::Playback: dirStr = L"PLAY"; break;
        case IpcDirection::Capture:  dirStr = L"REC";  break;
        case IpcDirection::Loopback: dirStr = L"LOOP"; break;
    }
    // Format: "Local\\UMCStudio_PLAY_0"
    // "Local\\" prefix for session-scoped named objects
    wchar_t buf[256];
    swprintf_s(buf, 256, L"Local\\%s_%s_%d", IPC_NAME_PREFIX, dirStr, channelId);
    return std::wstring(buf);
}

bool SharedMemoryChannel::create(IpcDirection direction, int channelId, int sampleRate) {
    close();

    m_name = buildName(direction, channelId);
    DWORD size = sizeof(IpcAudioBuffer);

    // Create or open existing shared memory section
    // SECURITY_ATTRIBUTES with NULL DACL for accessibility
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;
    sa.lpSecurityDescriptor = NULL;  // Default security

    m_hMapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,       // Backed by page file
        &sa,
        PAGE_READWRITE,
        0,                          // High DWORD of size
        size,                       // Low DWORD of size
        m_name.c_str()
    );

    if (!m_hMapping) {
        DWORD err = GetLastError();
        LOG_ERROR(LOG_MODULE, "CreateFileMapping failed for '%ls': 0x%08X",
                  m_name.c_str(), err);
        return false;
    }

    bool alreadyExists = (GetLastError() == ERROR_ALREADY_EXISTS);

    // Map view into our address space
    m_pBuffer = (IpcAudioBuffer*)MapViewOfFile(
        m_hMapping,
        FILE_MAP_ALL_ACCESS,
        0, 0,                       // Offset
        size
    );

    if (!m_pBuffer) {
        DWORD err = GetLastError();
        LOG_ERROR(LOG_MODULE, "MapViewOfFile failed for '%ls': 0x%08X",
                  m_name.c_str(), err);
        CloseHandle(m_hMapping);
        m_hMapping = NULL;
        return false;
    }

    // Initialize if we created it (not opened existing)
    if (!alreadyExists) {
        memset(m_pBuffer, 0, sizeof(IpcAudioBuffer));
        m_pBuffer->sampleRate = (ULONG)sampleRate;
        m_pBuffer->channelCount = 2;  // Stereo by default
    }

    LOG_INFO(LOG_MODULE, "Channel '%ls' %s (size=%u bytes, rate=%d)",
             m_name.c_str(),
             alreadyExists ? "opened" : "created",
             size, sampleRate);
    return true;
}

void SharedMemoryChannel::close() {
    if (m_pBuffer) {
        m_pBuffer->flags &= ~IPC_FLAG_ACTIVE;
        UnmapViewOfFile(m_pBuffer);
        m_pBuffer = nullptr;
    }
    if (m_hMapping) {
        CloseHandle(m_hMapping);
        m_hMapping = NULL;
    }
}

void SharedMemoryChannel::writeStereo(const float* left, const float* right, int numFrames) {
    if (!m_pBuffer) return;

    ULONG writePos = m_pBuffer->writePos;

    for (int i = 0; i < numFrames; i++) {
        ULONG idx = ((writePos + i) & IPC_RING_MASK) * 2;
        m_pBuffer->ring[idx]     = left[i];
        m_pBuffer->ring[idx + 1] = right[i];
    }

    // Memory fence to ensure data is written before updating position
    MemoryBarrier();
    m_pBuffer->writePos = writePos + numFrames;
}

void SharedMemoryChannel::writeMono(const float* data, int numFrames) {
    if (!m_pBuffer) return;

    ULONG writePos = m_pBuffer->writePos;

    for (int i = 0; i < numFrames; i++) {
        ULONG idx = ((writePos + i) & IPC_RING_MASK) * 2;
        m_pBuffer->ring[idx]     = data[i];
        m_pBuffer->ring[idx + 1] = data[i];
    }

    MemoryBarrier();
    m_pBuffer->writePos = writePos + numFrames;
}

int SharedMemoryChannel::readStereo(float* left, float* right, int numFrames) {
    if (!m_pBuffer) {
        // Zero output on failure
        if (left)  memset(left,  0, numFrames * sizeof(float));
        if (right) memset(right, 0, numFrames * sizeof(float));
        return 0;
    }

    ULONG w = m_pBuffer->writePos;
    ULONG r = m_pBuffer->readPos;
    LONG available = (LONG)(w - r);

    // Overflow protection (same logic as ASMRTOP)
    if (available < 0 || available > IPC_RING_SIZE) {
        r = w;
        available = 0;
        m_pBuffer->flags |= IPC_FLAG_OVERFLOW;
    }

    int framesRead = 0;
    for (int i = 0; i < numFrames; i++) {
        if (available > 0) {
            ULONG idx = (r & IPC_RING_MASK) * 2;
            float lVal = m_pBuffer->ring[idx];
            float rVal = m_pBuffer->ring[idx + 1];

            // Clamp (same as ASMRTOP)
            if (lVal >  1.0f) lVal =  1.0f; else if (lVal < -1.0f) lVal = -1.0f;
            if (rVal >  1.0f) rVal =  1.0f; else if (rVal < -1.0f) rVal = -1.0f;

            if (left)  left[i]  = lVal;
            if (right) right[i] = rVal;

            r++;
            available--;
            framesRead++;
        } else {
            // Underflow: output silence
            if (left)  left[i]  = 0.0f;
            if (right) right[i] = 0.0f;
            m_pBuffer->flags |= IPC_FLAG_UNDERFLOW;
        }
    }

    MemoryBarrier();
    m_pBuffer->readPos = r;
    return framesRead;
}

int SharedMemoryChannel::getAvailableFrames() const {
    if (!m_pBuffer) return 0;
    LONG avail = (LONG)(m_pBuffer->writePos - m_pBuffer->readPos);
    if (avail < 0 || avail > IPC_RING_SIZE) return 0;
    return (int)avail;
}

void SharedMemoryChannel::setActive(bool active) {
    if (!m_pBuffer) return;
    if (active)
        m_pBuffer->flags |= IPC_FLAG_ACTIVE;
    else
        m_pBuffer->flags &= ~IPC_FLAG_ACTIVE;
}

bool SharedMemoryChannel::isActive() const {
    if (!m_pBuffer) return false;
    return (m_pBuffer->flags & IPC_FLAG_ACTIVE) != 0;
}

void SharedMemoryChannel::reset() {
    if (!m_pBuffer) return;
    m_pBuffer->writePos = 0;
    m_pBuffer->readPos = 0;
    m_pBuffer->flags = 0;
    // Don't zero the ring buffer - just reset cursors
}

// ============================================================
// SharedMemoryBridge
// ============================================================

SharedMemoryBridge::SharedMemoryBridge() {
}

SharedMemoryBridge::~SharedMemoryBridge() {
    shutdown();
}

bool SharedMemoryBridge::init(int numPlaybackPairs, int numLoopbackChannels,
                               int numVirtualRecPairs, int sampleRate) {
    shutdown();  // Clean up any previous state

    LOG_INFO(LOG_MODULE, "Initializing IPC bridge: %d playback pairs, %d loopback, %d vrec pairs @ %dHz",
             numPlaybackPairs, numLoopbackChannels, numVirtualRecPairs, sampleRate);

    // Create playback channels (PLAYBACK 1~N, stereo pairs)
    m_playbackChannels.resize(numPlaybackPairs);
    for (int i = 0; i < numPlaybackPairs; i++) {
        if (!m_playbackChannels[i].create(IpcDirection::Playback, i, sampleRate)) {
            LOG_ERROR(LOG_MODULE, "Failed to create playback channel %d", i);
            shutdown();
            return false;
        }
    }

    // Create loopback channels (one per physical output, mono)
    m_loopbackChannels.resize(numLoopbackChannels);
    for (int i = 0; i < numLoopbackChannels; i++) {
        if (!m_loopbackChannels[i].create(IpcDirection::Loopback, i, sampleRate)) {
            LOG_ERROR(LOG_MODULE, "Failed to create loopback channel %d", i);
            shutdown();
            return false;
        }
    }

    // Create virtual recording channels (VIRTUAL REC 1~N, stereo pairs)
    m_virtualRecChannels.resize(numVirtualRecPairs);
    for (int i = 0; i < numVirtualRecPairs; i++) {
        if (!m_virtualRecChannels[i].create(IpcDirection::Capture, i, sampleRate)) {
            LOG_ERROR(LOG_MODULE, "Failed to create virtual rec channel %d", i);
            shutdown();
            return false;
        }
    }

    int totalChannels = numPlaybackPairs + numLoopbackChannels + numVirtualRecPairs;
    LOG_INFO(LOG_MODULE, "IPC bridge initialized: %d total channels", totalChannels);
    return true;
}

void SharedMemoryBridge::shutdown() {
    for (auto& ch : m_playbackChannels) ch.close();
    for (auto& ch : m_loopbackChannels) ch.close();
    for (auto& ch : m_virtualRecChannels) ch.close();

    m_playbackChannels.clear();
    m_loopbackChannels.clear();
    m_virtualRecChannels.clear();

    LOG_INFO(LOG_MODULE, "IPC bridge shutdown");
}

SharedMemoryChannel* SharedMemoryBridge::getPlaybackChannel(int index) {
    if (index >= 0 && index < (int)m_playbackChannels.size())
        return &m_playbackChannels[index];
    return nullptr;
}

SharedMemoryChannel* SharedMemoryBridge::getLoopbackChannel(int index) {
    if (index >= 0 && index < (int)m_loopbackChannels.size())
        return &m_loopbackChannels[index];
    return nullptr;
}

SharedMemoryChannel* SharedMemoryBridge::getVirtualRecChannel(int index) {
    if (index >= 0 && index < (int)m_virtualRecChannels.size())
        return &m_virtualRecChannels[index];
    return nullptr;
}

void SharedMemoryBridge::resetAll() {
    for (auto& ch : m_playbackChannels)   ch.reset();
    for (auto& ch : m_loopbackChannels)   ch.reset();
    for (auto& ch : m_virtualRecChannels) ch.reset();
}

void SharedMemoryBridge::setAllActive(bool active) {
    for (auto& ch : m_playbackChannels)   ch.setActive(active);
    for (auto& ch : m_loopbackChannels)   ch.setActive(active);
    for (auto& ch : m_virtualRecChannels) ch.setActive(active);
}
