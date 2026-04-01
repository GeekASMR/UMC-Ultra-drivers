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
        m_channelId(-1), m_callCount(0), m_firstCall(true), m_prefillMode(true),
        m_lastSeenWritePos(0), m_driftCounter(0), m_lastOpenTry(0),
        m_detectedWdmRate(48000.0), m_wpVelocityStart(0), m_wpVelocityCount(0) {
        m_direction[0] = '\0';
        QueryPerformanceFrequency((LARGE_INTEGER*)&m_qpcFreq);
        QueryPerformanceCounter((LARGE_INTEGER*)&m_qpcLastV);
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
        m_prefillMode = true;
        m_driftCounter = 0;
        m_detectedWdmRate = 48000.0;
        m_wpVelocityCount = 0;
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

    // Call this before readStereoAdaptive to get the dynamically measured WDM Sample Rate
    double getDetectedWdmRate() const { return m_detectedWdmRate; }

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

        // Gross overflow/underflow or first call after reset: start exactly at write head
        if (m_firstCall || available < 0 || available > 131000) {
            r = w; // We don't rewind. We just start empty and let prefill handle the cushion!
            available = 0;
            m_firstCall = false;
            m_lastReadL = 0.0f;
            m_lastReadR = 0.0f;
            m_prefillMode = true; // Enter prefill to build safe cushion
        }

        int safeCushion = numFrames * 2 + 1200;
        if (safeCushion < 2400) safeCushion = 2400;

        if (m_prefillMode) {
            // Need a dynamic cushion to absorb Windows bursty WDM writes without deadlocking large DAW buffers
            if (available < safeCushion) {
                if (left)  memset(left,  0, numFrames * sizeof(float));
                if (right) memset(right, 0, numFrames * sizeof(float));
                return;
            } else {
                m_prefillMode = false;
            }
        }

        if (available < numFrames) {
            // Underrun! WDM starved us. Fade out the remainder and re-enter prefill mode!
            for (int i = 0; i < numFrames; i++) {
                m_lastReadL *= 0.95f;
                m_lastReadR *= 0.95f;
                if (left)  left[i]  = m_lastReadL;
                if (right) right[i] = m_lastReadR;
                if (fabsf(m_lastReadL) < 1e-7f) m_lastReadL = 0.0f;
                if (fabsf(m_lastReadR) < 1e-7f) m_lastReadR = 0.0f;
            }
            m_prefillMode = true; // Wait for buffer to pile back up
            return; // We do NOT advance the read pointer
        }

        // Clean slip forward if excessively huge (allows up to 2-second bulk WASAPI blocks without truncating them out of existence!)
        if (available > 131072 || available > (96000 + safeCushion)) {
            r = w - safeCushion;
            available = safeCushion;
        }

        // Fast path for perfect sync
        if (left && right) {
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
        }

        r += numFrames;
        m_readPos = r;
        m_buf->readPos.store(r, std::memory_order_release);
    }

    // =========================================================================
    // Read with optional SRC (PLAY -> DAW)
    // When srcRate == dstRate: zero-overhead passthrough to readDirect()
    // When srcRate != dstRate: linear interpolation SRC
    // =========================================================================
    void readStereoAdaptive(float* left, float* right, int numFrames, double /*deprecated_srcRate*/, double dstRate) {
        validateOrReconnect(); // !! VERY IMPORTANT BUGBIX !!
        if (!m_buf && !tryOpen()) {
            if (left)  memset(left,  0, numFrames * sizeof(float));
            if (right) memset(right, 0, numFrames * sizeof(float));
            return;
        }

        uint32_t w = m_buf->writePos.load(std::memory_order_acquire);
        uint32_t r = m_readPos;
        int32_t available = (int32_t)(w - r);

        // Gross anomaly or first call after reset: snap strictly to write head
        if (m_firstCall || available < 0 || available > 131000) {
            r = w;
            available = 0;
            m_lastReadL = 0.0f;
            m_lastReadR = 0.0f;
            m_readPos = r;
            m_readPosFrac = 0.0;
            m_firstCall = false;
            m_prefillMode = true;
            // !!! INSTANTLY RESET VELOCITY DETECTOR !!!
            m_wpVelocityCount = 0;
            // Best guess for starting rate is DAW dstRate to minimize stutter before detection
            m_detectedWdmRate = dstRate;
        }

        // Measure velocity of writePos dynamically to adapt to WASAPI Exclusive rates!
        if (m_wpVelocityCount++ > 0) { // Check every loop!
            uint64_t qpcNow;
            QueryPerformanceCounter((LARGE_INTEGER*)&qpcNow);
            double elapsedSec = (double)(qpcNow - m_qpcLastV) / (double)m_qpcFreq;
            if (elapsedSec >= 0.1 || m_wpVelocityCount == 2) { // Evaluate every 100ms or instantly!
                double velocity = (double)(w - m_wpVelocityStart) / elapsedSec;
                // Snap to standard rates if close
                double standardRates[] = {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0, 384000.0};
                double finalRate = velocity;
                bool isStandard = false;
                for (double st : standardRates) {
                    if (fabs(velocity - st) < (st * 0.05)) { finalRate = st; isStandard = true; break; }
                }
                if (finalRate > 8000.0 && finalRate < 768000.0) {
                    if (isStandard) {
                        m_detectedWdmRate = finalRate; // Instant snap
                    } else {
                        m_detectedWdmRate = m_detectedWdmRate * 0.8 + finalRate * 0.2; // Smooth drift
                    }
                }
                m_qpcLastV = qpcNow;
                m_wpVelocityStart = w;
            }
        } else {
            QueryPerformanceCounter((LARGE_INTEGER*)&m_qpcLastV);
            m_wpVelocityStart = w;
        }

        // Use the dynamically detected rate!
        double srcRate = m_detectedWdmRate;
        
        // Fast path: dynamically determined same rate = zero overhead direct read!
        if (fabs(srcRate - dstRate) < 1.0) {
            readDirect(left, right, numFrames);
            return;
        }

        double ratio = srcRate / dstRate; // e.g. 48000/44100 = 1.0884
        int srcNeeded = (int)(numFrames * ratio) + 2;
        int safeCushion = srcNeeded * 2 + 1200; // Build healthy dynamic cushion!
        if (safeCushion < 2400) safeCushion = 2400; // Floor

        if (m_prefillMode) {
            if (available < safeCushion) {
                if (left)  memset(left,  0, numFrames * sizeof(float));
                if (right) memset(right, 0, numFrames * sizeof(float));
                return;
            } else {
                m_prefillMode = false;
            }
        }

        if (available < srcNeeded) {
            // Not enough data: fade out
            for (int i = 0; i < numFrames; i++) {
                m_lastReadL *= 0.95f;
                m_lastReadR *= 0.95f;
                if (left)  left[i]  = m_lastReadL;
                if (right) right[i] = m_lastReadR;
            }
            if (fabsf(m_lastReadL) < 1e-7f) m_lastReadL = 0.0f;
            if (fabsf(m_lastReadR) < 1e-7f) m_lastReadR = 0.0f;
            m_prefillMode = true; // Wait for safe cushion again
            return;
        }

        if (available > 131072 || available > (96000 + safeCushion)) {
            r = w - safeCushion;
            available = safeCushion;
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
    bool m_prefillMode;      // Absorbs initial burst variations by padding zeros until a safe 50ms cushion builds

    // Stale shared memory detection (hot-switch fix)
    uint32_t m_lastSeenWritePos;  // Last observed writePos value
    int m_driftCounter;           // Rate limits the slip buffer drift adjustments
    DWORD m_lastOpenTry;          // Throttle OpenFileMappingA calls

    // Dynamic Auto-Rate Detection
    double m_detectedWdmRate;
    uint32_t m_wpVelocityStart;
    uint32_t m_wpVelocityCount;
    uint64_t m_qpcLastV;
    uint64_t m_qpcFreq;
};
