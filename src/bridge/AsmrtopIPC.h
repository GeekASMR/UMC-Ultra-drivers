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
        m_channelId(-1), m_callCount(0), m_firstCall(true),
        m_lastSeenWritePos(0), m_driftCounter(0), m_lastOpenTry(0) {
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
        
        // 核心负优化修复：若底层虚拟声卡通道未激活，绝对不能让 6000Hz 回调狂刷 OpenFileMappingA 系统内核指令！
        // 加入 GetTickCount 回退锁，空闲状态最快只允许每 1 秒申请 1 次内核句柄，彻底释放微核高频阻塞。
        DWORD now = GetTickCount();
        if (m_lastOpenTry != 0 && (now - m_lastOpenTry < 1000)) {
            return false;
        }
        m_lastOpenTry = now == 0 ? 1 : now;

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
        resetState();
    }

    // Soft reset: preserve shared memory mapping, only reset read/write state.
    // Use when DAW switches buffer size — WDM shared memory stays alive.
    void resetState() {
        m_readPos = 0;
        m_readPosFrac = 0.0;
        m_lastReadL = 0.0f;
        m_lastReadR = 0.0f;
        m_writePosFrac = 0.0;
        m_lastWriteL = 0.0f;
        m_lastWriteR = 0.0f;
        m_lastSeenWritePos = 0;
        m_callCount = 0;
        m_firstCall = true;
        m_driftCounter = 0;
    }

    bool isOpen() const { return m_buf != nullptr; }

    // Periodic health check: verify mapped memory is still valid.
    // If the WDM driver restarts, the shared memory handle becomes stale.
    // Thresholds scaled for 8-sample buffers (~6000 callbacks/sec).
    void validateOrReconnect() {
        if (!m_buf) return;

        // Ensure we track writePos for reference, but don't close purely over idleness
        uint32_t currentWp = m_buf->writePos.load(std::memory_order_relaxed);
        m_lastSeenWritePos = currentWp;

        // Periodic memory validity check (backup: catches actual driver unload!)
        if (++m_callCount < 5000) return;
        m_callCount = 0;

        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(m_buf, &mbi, sizeof(mbi)) == 0 ||
            mbi.State != MEM_COMMIT) {
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

        if (left && right) {
            // AVX Fast Path via standard library memcpy (auto-vectorized)
            uint32_t wpIdx = wp & IPC_RING_MASK;
            if (wpIdx + numFrames <= IPC_RING_SIZE) {
                memcpy(&m_buf->ringL[wpIdx], left, numFrames * sizeof(float));
                memcpy(&m_buf->ringR[wpIdx], right, numFrames * sizeof(float));
            } else {
                uint32_t break1 = IPC_RING_SIZE - wpIdx;
                uint32_t break2 = numFrames - break1;
                memcpy(&m_buf->ringL[wpIdx], left, break1 * sizeof(float));
                memcpy(&m_buf->ringR[wpIdx], right, break1 * sizeof(float));
                memcpy(&m_buf->ringL[0], left + break1, break2 * sizeof(float));
                memcpy(&m_buf->ringR[0], right + break1, break2 * sizeof(float));
            }
        } else {
            for (int i = 0; i < numFrames; i++) {
                uint32_t wIdx = (wp + i) & IPC_RING_MASK;
                m_buf->ringL[wIdx] = left  ? left[i]  : 0.0f;
                m_buf->ringR[wIdx] = right ? right[i] : 0.0f;
            }
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

        const int32_t LOW_TH  = 1024;   // Below this: stuff (slow down read)
        const int32_t HIGH_TH = 8192;   // Above this: drop (speed up read)

        // Gross overflow/underflow or first call after reset: start exactly at write head.
        // We do NOT rewind to w - 4096 here because grabbing old memory causes repetitive trash bursting on idle channels.
        // The leaky integrator will naturally and smoothly build up the safety cushion over time.
        if (m_firstCall || available < 0 || available > 16384) {
            r = w;
            available = 0;
            m_firstCall = false;
            m_lastReadL = 0.0f;
            m_lastReadR = 0.0f;
        }

        // Determine how many ring samples to consume for this callback.
        // Normally consume exactly numFrames. Adjust ±1 for drift compensation.
        int samplesToConsume = numFrames;
        if (available < LOW_TH && available > numFrames) {
            // Buffer running low - stuff: drift into positive compensation (~0.05% shift)
            m_driftCounter += numFrames;
            if (m_driftCounter >= 2000) {
                int drops = m_driftCounter / 2000;
                samplesToConsume -= drops;
                m_driftCounter %= 2000;
            }
        } else if (available > HIGH_TH) {
            // Buffer growing - drop: drift into negative compensation
            m_driftCounter -= numFrames;
            if (m_driftCounter <= -2000) {
                int adds = (-m_driftCounter) / 2000;
                samplesToConsume += adds;
                m_driftCounter = -((-m_driftCounter) % 2000);
            }
        } else {
            // Leaky Integrator: Gently decay back to 0 when healthy, preventing sudden jitters from erasing our drift record
            if (m_driftCounter > 0) m_driftCounter--;
            else if (m_driftCounter < 0) m_driftCounter++;
        }

        // Make sure we don't consume more than available
        if (samplesToConsume > available) {
            samplesToConsume = available;
        }

        // Create a fast path for perfect sync (99% of calls)
        if (samplesToConsume == numFrames && available >= numFrames && left && right) {
            // Seamless AVX auto-vectorization block via memcpy
            uint32_t rIdx = r & IPC_RING_MASK;
            if (rIdx + numFrames <= IPC_RING_SIZE) {
                memcpy(left, &m_buf->ringL[rIdx], numFrames * sizeof(float));
                memcpy(right, &m_buf->ringR[rIdx], numFrames * sizeof(float));
            } else {
                uint32_t break1 = IPC_RING_SIZE - rIdx;
                uint32_t break2 = numFrames - break1;
                memcpy(left, &m_buf->ringL[rIdx], break1 * sizeof(float));
                memcpy(right, &m_buf->ringR[rIdx], break1 * sizeof(float));
                memcpy(left + break1, &m_buf->ringL[0], break2 * sizeof(float));
                memcpy(right + break1, &m_buf->ringR[0], break2 * sizeof(float));
            }
            m_lastReadL = left[numFrames - 1];
            m_lastReadR = right[numFrames - 1];
        } else {
            // Drift compensation slow-path
            for (int i = 0; i < numFrames; i++) {
                double srcIdx = (samplesToConsume == numFrames) ? (double)i : (double)i * samplesToConsume / numFrames;

                int s = (int)srcIdx;
                if (samplesToConsume <= 0) s = -1; 
                else if (s >= samplesToConsume) s = samplesToConsume - 1;

                if (s >= 0 && s < available) {
                    float sL, sR;
                    if (s + 1 < available && s + 1 < samplesToConsume) {
                        float frac = (float)(srcIdx - s);
                        uint32_t i0 = (r + s) & IPC_RING_MASK;
                        uint32_t i1 = (r + s + 1) & IPC_RING_MASK;
                        sL = m_buf->ringL[i0] + (m_buf->ringL[i1] - m_buf->ringL[i0]) * frac;
                        sR = m_buf->ringR[i0] + (m_buf->ringR[i1] - m_buf->ringR[i0]) * frac;
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
                    m_lastReadL *= 0.95f;
                    m_lastReadR *= 0.95f;
                    if (left)  left[i]  = m_lastReadL;
                    if (right) right[i] = m_lastReadR;
                    if (fabsf(m_lastReadL) < 1e-7f) m_lastReadL = 0.0f;
                    if (fabsf(m_lastReadR) < 1e-7f) m_lastReadR = 0.0f;
                }
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

        // Gross anomaly or first call after reset: snap strictly to write head
        if (m_firstCall || available < 0 || available > 16384) {
            r = w;
            available = 0;
            m_lastReadL = 0.0f;
            m_lastReadR = 0.0f;
            m_readPos = r;
            m_readPosFrac = 0.0;
            m_firstCall = false;
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

        // Math optimization & Hoisted branch
        if (left && right) {
            for (int i = 0; i < numFrames; i++) {
                double srcPos = m_readPosFrac + i * ratio;
                int s = (int)srcPos;
                float f = (float)(srcPos - s);
                uint32_t idx0 = (r + s)     & IPC_RING_MASK;
                uint32_t idx1 = (r + s + 1) & IPC_RING_MASK;
                left[i]  = m_buf->ringL[idx0] + (m_buf->ringL[idx1] - m_buf->ringL[idx0]) * f;
                right[i] = m_buf->ringR[idx0] + (m_buf->ringR[idx1] - m_buf->ringR[idx0]) * f;
            }
        } else {
            for (int i = 0; i < numFrames; i++) {
                double srcPos = m_readPosFrac + i * ratio;
                int s = (int)srcPos;
                float f = (float)(srcPos - s);
                uint32_t idx0 = (r + s)     & IPC_RING_MASK;
                uint32_t idx1 = (r + s + 1) & IPC_RING_MASK;
                if (left)  left[i]  = m_buf->ringL[idx0] + (m_buf->ringL[idx1] - m_buf->ringL[idx0]) * f;
                if (right) right[i] = m_buf->ringR[idx0] + (m_buf->ringR[idx1] - m_buf->ringR[idx0]) * f;
            }
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

        double step = srcRate / dstRate;
        
        // Dynamically calculate the exact required number of output frames
        // to consume precisely `numFrames` of input data based on current fractional phase
        int numOut = 0;
        while ((m_writePosFrac + numOut * step) < numFrames) {
            numOut++;
        }
        
        if (numOut <= 0) return;

        uint32_t wp = m_buf->writePos.load(std::memory_order_relaxed);

        if (left || right) {
            auto getSampleL = [&](int i) -> float {
                if (!left) return 0.0f;
                if (i < 0) return m_lastWriteL;
                if (i >= numFrames) return left[numFrames - 1];
                return left[i];
            };
            auto getSampleR = [&](int i) -> float {
                if (!right) return 0.0f;
                if (i < 0) return m_lastWriteR;
                if (i >= numFrames) return right[numFrames - 1];
                return right[i];
            };

            for (int i = 0; i < numOut; i++) {
                double srcPos = m_writePosFrac + i * step;
                int idx = (int)srcPos; // integer phase [0 to numFrames]
                float frac = (float)(srcPos - idx); // fractional phase [0.0 to 1.0)
                
                float sL0 = getSampleL(idx - 1);
                float sL1 = getSampleL(idx);
                float sR0 = getSampleR(idx - 1);
                float sR1 = getSampleR(idx);

                uint32_t wIdx = (wp + i) & IPC_RING_MASK;
                m_buf->ringL[wIdx] = sL0 + (sL1 - sL0) * frac;
                m_buf->ringR[wIdx] = sR0 + (sR1 - sR0) * frac;
            }
            if (left  && numFrames > 0) m_lastWriteL = left[numFrames - 1];
            if (right && numFrames > 0) m_lastWriteR = right[numFrames - 1];
        } else {
            for (int i = 0; i < numOut; i++) {
                uint32_t wIdx = (wp + i) & IPC_RING_MASK;
                m_buf->ringL[wIdx] = 0.0f;
                m_buf->ringR[wIdx] = 0.0f;
            }
        }

        m_writePosFrac = (m_writePosFrac + numOut * step) - numFrames;

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
    bool m_firstCall;        // Force snap on first read after reset

    // Stale shared memory detection (hot-switch fix)
    uint32_t m_lastSeenWritePos;  // Last observed writePos value
    int m_driftCounter;           // Rate limits the slip buffer drift adjustments
    DWORD m_lastOpenTry;          // Throttle OpenFileMappingA calls
};
