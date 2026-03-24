/*
 * AsmrtopIPC.h - ASMRTOP WDM Virtual Audio Driver IPC Bridge
 *
 * Shared memory ring buffer interface for communicating with
 * the ASMRTOP/VirtualAudio WDM kernel driver.
 *
 * Each IPC channel is a stereo ring buffer (L/R channels).
 * The kernel driver creates shared memory sections named:
 *   Global\AsmrtopWDM_PLAY_0 .. _3  (system playback -> VST/ASIO)
 *   Global\AsmrtopWDM_REC_0  .. _3  (VST/ASIO -> system microphone)
 *   Global\VirtualAudioWDM_PLAY_0 .. _3  (public brand)
 *   Global\VirtualAudioWDM_REC_0  .. _3  (public brand)
 *
 * Usage:
 *   Reading (PLAY -> DAW):  readDirect()
 *   Writing (DAW -> REC):   writeDirect()
 */

#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <atomic>

#define IPC_RING_SIZE 131072
#define IPC_RING_MASK (IPC_RING_SIZE - 1)

struct IpcAudioBuffer {
    std::atomic<uint32_t> writePos;
    std::atomic<uint32_t> readPos;
    float ringL[IPC_RING_SIZE];
    float ringR[IPC_RING_SIZE];
};

class AsmrtopIpcChannel {
public:
    AsmrtopIpcChannel() : m_hMap(NULL), m_buf(nullptr),
        m_readPos(0), m_readPosFrac(0.0), m_lastReadL(0.0f), m_lastReadR(0.0f),
        m_writePosFrac(0.0), m_lastWriteL(0.0f), m_lastWriteR(0.0f),
        m_channelId(-1), m_callCount(0), m_playing(false),
        m_driftRatio(1.0), m_driftFrac(0.0) {
        m_direction[0] = '\0';
    }

    ~AsmrtopIpcChannel() { close(); }

    bool open(const char* direction, int channelId) {
        strncpy(m_direction, direction, sizeof(m_direction) - 1);
        m_direction[sizeof(m_direction) - 1] = '\0';
        m_channelId = channelId;
        return tryOpen();
    }

    bool tryOpen() {
        if (m_buf) return true;
        const char* brands[] = { "VirtualAudioWDM" };

        // Only scan Global namespace - kernel driver creates shared memory here.
        // Never use CreateFileMapping to self-create, that goes to Session isolation
        // and causes complete silence when the kernel driver starts later.
        const char* prefixes[] = { "Global\\" };
        char name[256];

        for (auto brand : brands) {
            for (auto prefix : prefixes) {
                snprintf(name, sizeof(name), "%s%s_%s_%d", prefix, brand, m_direction, m_channelId);
                m_hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
                if (m_hMap) {
                    m_buf = (IpcAudioBuffer*)MapViewOfFile(m_hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(IpcAudioBuffer));
                    if (m_buf) return true;
                    CloseHandle(m_hMap);
                    m_hMap = NULL;
                }
            }
        }
        return false;
    }

    void close() {
        if (m_buf) { UnmapViewOfFile(m_buf); m_buf = nullptr; }
        if (m_hMap) { CloseHandle(m_hMap); m_hMap = NULL; }
    }

    bool isOpen() const { return m_buf != nullptr; }

    // Periodic health check: verify mapped memory is still valid.
    // If the WDM driver restarts, the shared memory handle becomes stale.
    // We detect this cheaply via VirtualQuery every ~100 calls.
    void validateOrReconnect() {
        if (!m_buf) return;
        if (++m_callCount < 100) return;
        m_callCount = 0;

        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(m_buf, &mbi, sizeof(mbi)) == 0 ||
            mbi.State != MEM_COMMIT) {
            // Memory is gone — driver was restarted
            close();
            tryOpen();
        }
    }

    // =========================================================================
    // Write to Ring Buffer (DAW VRT OUT -> System Virtual Mic)
    //
    // No SRC - direct 48k->48k write. The WDM kernel anchors its read position
    // to the current writePos when a mic stream opens.
    // We just keep writing forward, relying on uint32 natural wrap-around.
    // =========================================================================
    void writeDirect(const float* left, const float* right, int numFrames) {
        validateOrReconnect();
        if (!m_buf && !tryOpen()) return;

        uint32_t wp = m_buf->writePos.load(std::memory_order_relaxed);

        for (int i = 0; i < numFrames; i++) {
            uint32_t wIdx = (wp + i) & IPC_RING_MASK;
            m_buf->ringL[wIdx] = left  ? left[i]  : 0.0f;
            m_buf->ringR[wIdx] = right ? right[i] : 0.0f;
        }

        m_buf->writePos.store(wp + numFrames, std::memory_order_release);
    }

    // =========================================================================
    // Read from Ring Buffer (System Audio -> DAW VRT IN)
    //
    // Drift-compensating reader using sample stuff/drop:
    // - When buffer level drops too low: read one FEWER sample (stuff)
    // - When buffer level rises too high: read one EXTRA sample (drop)
    // - At most ±1 sample per callback = ~0.02% rate adjustment
    // - Target buffer: ~4096 samples (~85ms @ 48kHz)
    // =========================================================================
    void readDirect(float* left, float* right, int numFrames) {
        validateOrReconnect();
        if (!m_buf && !tryOpen()) {
            if (left)  memset(left,  0, numFrames * sizeof(float));
            if (right) memset(right, 0, numFrames * sizeof(float));
            return;
        }

        uint32_t w = m_buf->writePos.load(std::memory_order_acquire);
        uint32_t r = m_readPos;
        int32_t available = (int32_t)(w - r);

        const int32_t TARGET  = 4096;   // Target buffer level
        const int32_t LOW_TH  = 1024;   // Below this: stuff (slow down read)
        const int32_t HIGH_TH = 8192;   // Above this: drop (speed up read)

        // Gross overflow/underflow: snap to target with crossfade
        if (available < 0 || available > IPC_RING_SIZE / 2) {
            // Snap to target
            uint32_t newR = w - TARGET;
            // Crossfade: read first sample at new position for smooth transition
            float newL = m_buf->ringL[newR & IPC_RING_MASK];
            float newR_ = m_buf->ringR[newR & IPC_RING_MASK];
            // Blend with last known sample to avoid hard click
            m_lastReadL = m_lastReadL * 0.5f + newL * 0.5f;
            m_lastReadR = m_lastReadR * 0.5f + newR_ * 0.5f;
            r = newR;
            available = TARGET;
        }

        // Determine how many ring samples to consume for this callback.
        // Normally consume exactly numFrames. Adjust ±1 for drift compensation.
        int samplesToConsume = numFrames;
        if (available < LOW_TH && available > numFrames) {
            // Buffer running low - stuff: consume one fewer sample
            samplesToConsume = numFrames - 1;
        } else if (available > HIGH_TH) {
            // Buffer growing - drop: consume one extra sample
            samplesToConsume = numFrames + 1;
        }

        // Make sure we don't consume more than available
        if (samplesToConsume > available) {
            samplesToConsume = available;
        }

        // Read with linear resampling when samplesToConsume != numFrames
        for (int i = 0; i < numFrames; i++) {
            // Map output sample i to input position
            double srcIdx = (samplesToConsume == numFrames) ?
                (double)i :
                (double)i * samplesToConsume / numFrames;

            int s = (int)srcIdx;
            if (s >= samplesToConsume) s = samplesToConsume - 1;

            if (s < available) {
                float sL, sR;
                if (s + 1 < available && s + 1 < samplesToConsume) {
                    // Linear interpolation between s and s+1
                    float frac = (float)(srcIdx - s);
                    uint32_t i0 = (r + s) & IPC_RING_MASK;
                    uint32_t i1 = (r + s + 1) & IPC_RING_MASK;
                    sL = m_buf->ringL[i0] * (1.0f - frac) + m_buf->ringL[i1] * frac;
                    sR = m_buf->ringR[i0] * (1.0f - frac) + m_buf->ringR[i1] * frac;
                } else {
                    uint32_t idx = (r + s) & IPC_RING_MASK;
                    sL = m_buf->ringL[idx];
                    sR = m_buf->ringR[idx];
                }
                if (left)  left[i]  = sL;
                if (right) right[i] = sR;
                m_lastReadL = sL;
                m_lastReadR = sR;
            } else {
                // Underrun: smooth fade
                m_lastReadL *= 0.95f;
                m_lastReadR *= 0.95f;
                if (left)  left[i]  = m_lastReadL;
                if (right) right[i] = m_lastReadR;
                if (fabsf(m_lastReadL) < 1e-7f) m_lastReadL = 0.0f;
                if (fabsf(m_lastReadR) < 1e-7f) m_lastReadR = 0.0f;
            }
        }

        r += samplesToConsume;
        m_readPos = r;
        m_buf->readPos.store(r, std::memory_order_release);
    }

    // =========================================================================
    // Read with optional SRC (PLAY -> DAW)
    // When srcRate == dstRate: zero-overhead passthrough to readDirect()
    // When srcRate != dstRate: linear interpolation SRC
    // =========================================================================
    void readStereoAdaptive(float* left, float* right, int numFrames, double srcRate, double dstRate) {
        // Fast path: same rate = direct read
        if (fabs(srcRate - dstRate) < 1.0) {
            readDirect(left, right, numFrames);
            return;
        }

        if (!m_buf && !tryOpen()) {
            if (left)  memset(left,  0, numFrames * sizeof(float));
            if (right) memset(right, 0, numFrames * sizeof(float));
            return;
        }

        double ratio = srcRate / dstRate; // e.g. 48000/44100 = 1.0884
        int srcNeeded = (int)(numFrames * ratio) + 2;

        uint32_t w = m_buf->writePos.load(std::memory_order_acquire);
        uint32_t r = m_readPos;
        int32_t available = (int32_t)(w - r);

        if (available < 0 || available > IPC_RING_SIZE) {
            r = w;
            available = 0;
            m_readPosFrac = 0.0;
        }

        if (available < srcNeeded) {
            // Not enough data: fade out
            for (int i = 0; i < numFrames; i++) {
                m_lastReadL *= 0.95f;
                m_lastReadR *= 0.95f;
                if (left)  left[i]  = m_lastReadL;
                if (right) right[i] = m_lastReadR;
                if (fabsf(m_lastReadL) < 1e-7f) m_lastReadL = 0.0f;
                if (fabsf(m_lastReadR) < 1e-7f) m_lastReadR = 0.0f;
            }
            return;
        }

        // Linear interpolation resample
        for (int i = 0; i < numFrames; i++) {
            double srcPos = m_readPosFrac + i * ratio;
            int s = (int)srcPos;
            float f = (float)(srcPos - s);
            uint32_t idx0 = (r + s)     & IPC_RING_MASK;
            uint32_t idx1 = (r + s + 1) & IPC_RING_MASK;
            if (left)  left[i]  = m_buf->ringL[idx0] * (1.0f - f) + m_buf->ringL[idx1] * f;
            if (right) right[i] = m_buf->ringR[idx0] * (1.0f - f) + m_buf->ringR[idx1] * f;
        }

        if (left  && numFrames > 0) m_lastReadL = left[numFrames - 1];
        if (right && numFrames > 0) m_lastReadR = right[numFrames - 1];

        double consumed = m_readPosFrac + numFrames * ratio;
        int intConsumed = (int)consumed;
        m_readPosFrac = consumed - intConsumed;
        r += intConsumed;

        m_readPos = r;
        m_buf->readPos.store(r, std::memory_order_release);
    }

    // =========================================================================
    // Write with optional SRC (DAW -> REC)
    // When srcRate == dstRate: zero-overhead passthrough to writeDirect()
    // When srcRate != dstRate: linear interpolation SRC
    // =========================================================================
    void writeStereoSRC(const float* left, const float* right, int numFrames, double srcRate, double dstRate) {
        // Fast path: same rate = direct write
        if (fabs(srcRate - dstRate) < 1.0) {
            writeDirect(left, right, numFrames);
            return;
        }

        if (!m_buf && !tryOpen()) return;

        double ratio = dstRate / srcRate; // output samples per input sample
        int numOut = (int)(numFrames * ratio + 0.5);
        if (numOut <= 0) return;

        uint32_t wp = m_buf->writePos.load(std::memory_order_relaxed);

        for (int i = 0; i < numOut; i++) {
            double srcPos = m_writePosFrac + i * (srcRate / dstRate);
            int idx = (int)srcPos;
            float frac = (float)(srcPos - idx);

            float sL0 = (idx >= 0 && idx < numFrames && left)  ? left[idx]  : m_lastWriteL;
            float sR0 = (idx >= 0 && idx < numFrames && right) ? right[idx] : m_lastWriteR;
            float sL1 = (idx + 1 >= 0 && idx + 1 < numFrames && left)  ? left[idx + 1]  : sL0;
            float sR1 = (idx + 1 >= 0 && idx + 1 < numFrames && right) ? right[idx + 1] : sR0;

            uint32_t wIdx = (wp + i) & IPC_RING_MASK;
            m_buf->ringL[wIdx] = sL0 * (1.0f - frac) + sL1 * frac;
            m_buf->ringR[wIdx] = sR0 * (1.0f - frac) + sR1 * frac;
        }

        m_lastWriteL = left  ? left[numFrames - 1]  : 0.0f;
        m_lastWriteR = right ? right[numFrames - 1] : 0.0f;

        double c = m_writePosFrac + numOut * (srcRate / dstRate);
        m_writePosFrac = c - numFrames;
        if (m_writePosFrac < 0.0) m_writePosFrac = 0.0;

        m_buf->writePos.store(wp + numOut, std::memory_order_release);
    }

private:
    HANDLE m_hMap;
    IpcAudioBuffer* m_buf;

    // Reader state
    uint32_t m_readPos;
    double m_readPosFrac;    // Fractional position for SRC
    float m_lastReadL;       // For smooth fade on underrun
    float m_lastReadR;

    // Writer state
    double m_writePosFrac;   // Fractional position for SRC
    float m_lastWriteL;
    float m_lastWriteR;

    int m_channelId;
    char m_direction[16];
    int m_callCount;         // Counter for periodic health check
    bool m_playing;          // State: buffering vs playing

    // Clock drift compensation
    double m_driftRatio;     // Current read speed ratio (1.0 = normal)
    double m_driftFrac;      // Fractional read position accumulator
};
