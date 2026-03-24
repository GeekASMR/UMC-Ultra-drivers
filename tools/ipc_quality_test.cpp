/*
 * ipc_quality_test.cpp - 自动化音频质量测试
 * 
 * 测试流程:
 * 1. 往 PLAY_0 ring buffer 写入 1kHz 正弦波 (模拟 WDM 驱动)
 * 2. 等待 ASIO 驱动读取 (观察 readPos 前进)
 * 3. 分析 ring buffer 中的数据完整性
 * 4. 检测: 速度偏差, 数据丢失, 异常值 (NaN/Inf/spike)
 */
#include <windows.h>
#include <iostream>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <vector>
#include <algorithm>

#define IPC_RING_SIZE 131072
#define IPC_RING_MASK (IPC_RING_SIZE - 1)
#define PI 3.14159265358979323846

struct IpcAudioBuffer {
    std::atomic<uint32_t> writePos;
    std::atomic<uint32_t> readPos;
    float ringL[IPC_RING_SIZE];
    float ringR[IPC_RING_SIZE];
};

HANDLE g_hMap = NULL;
IpcAudioBuffer* openOrCreateIpc(const char* direction, int id) {
    const char* brands[] = { "AsmrtopWDM", "VirtualAudioWDM" };
    const char* prefixes[] = { "Global\\", "" };
    char name[256];
    
    // Try to open existing
    for (auto brand : brands) {
        for (auto prefix : prefixes) {
            snprintf(name, sizeof(name), "%s%s_%s_%d", prefix, brand, direction, id);
            g_hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
            if (g_hMap) {
                auto* buf = (IpcAudioBuffer*)MapViewOfFile(g_hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(IpcAudioBuffer));
                if (buf) {
                    printf("Connected to: %s\n", name);
                    return buf;
                }
                CloseHandle(g_hMap);
                g_hMap = NULL;
            }
        }
    }
    
    printf("ERROR: Could not open PLAY_0. Is the WDM driver installed?\n");
    return nullptr;
}

int main() {
    printf("=== IPC Audio Quality Test ===\n\n");
    
    auto* buf = openOrCreateIpc("PLAY", 0);
    if (!buf) return 1;
    
    const double sampleRate = 48000.0;
    const double freq = 1000.0;      // 1kHz test tone
    const float amplitude = 0.5f;
    const int testDurationSec = 10;
    const int totalSamples = (int)(sampleRate * testDurationSec);
    const int chunkSize = 480;       // Simulate WASAPI 10ms buffer
    
    printf("\nTest parameters:\n");
    printf("  Sample rate: %.0f Hz\n", sampleRate);
    printf("  Test tone: %.0f Hz sine, amplitude %.2f\n", freq, amplitude);
    printf("  Duration: %d seconds (%d samples)\n", testDurationSec, totalSamples);
    printf("  Write chunk: %d samples (%.1f ms)\n\n", chunkSize, chunkSize / sampleRate * 1000);
    
    // Record initial state
    uint32_t startW = buf->writePos.load();
    uint32_t startR = buf->readPos.load();
    printf("Initial state: W=%u  R=%u  Avail=%d\n\n", startW, startR, (int32_t)(startW - startR));
    
    // Phase 1: Write sine wave to ring buffer
    printf("Phase 1: Writing %d samples of 1kHz sine...\n", totalSamples);
    
    uint32_t wp = buf->writePos.load();
    int written = 0;
    double phase = 0.0;
    double phaseInc = 2.0 * PI * freq / sampleRate;
    
    LARGE_INTEGER perfFreq, startTime, endTime;
    QueryPerformanceFrequency(&perfFreq);
    QueryPerformanceCounter(&startTime);
    
    while (written < totalSamples) {
        int toWrite = chunkSize;
        if (written + toWrite > totalSamples) toWrite = totalSamples - written;
        
        for (int i = 0; i < toWrite; i++) {
            float val = amplitude * (float)sin(phase);
            uint32_t idx = (wp + i) & IPC_RING_MASK;
            buf->ringL[idx] = val;
            buf->ringR[idx] = val;
            phase += phaseInc;
        }
        wp += toWrite;
        buf->writePos.store(wp, std::memory_order_release);
        written += toWrite;
        
        // Simulate real WDM timing (~10ms per chunk)
        Sleep(9);
    }
    
    QueryPerformanceCounter(&endTime);
    double writeElapsed = (double)(endTime.QuadPart - startTime.QuadPart) / perfFreq.QuadPart;
    double actualWriteRate = totalSamples / writeElapsed;
    
    printf("  Written %d samples in %.2f sec (%.1f samples/sec)\n", written, writeElapsed, actualWriteRate);
    
    // Phase 2: Wait for ASIO to consume and check readPos advancement
    printf("\nPhase 2: Monitoring ASIO read for 5 seconds...\n");
    
    uint32_t postWriteW = buf->writePos.load();
    uint32_t postWriteR = buf->readPos.load();
    printf("  Post-write: W=%u  R=%u  Avail=%d\n", postWriteW, postWriteR, (int32_t)(postWriteW - postWriteR));
    
    // Track read rate over 5 seconds
    std::vector<uint32_t> readSamples;
    uint32_t prevR = postWriteR;
    
    for (int sec = 0; sec < 5; sec++) {
        Sleep(1000);
        uint32_t curR = buf->readPos.load();
        uint32_t curW = buf->writePos.load();
        uint32_t readDelta = curR - prevR;
        int32_t avail = (int32_t)(curW - curR);
        readSamples.push_back(readDelta);
        printf("  t=%d: R=%u (read %u/s)  Avail=%d\n", sec + 1, curR, readDelta, avail);
        prevR = curR;
    }
    
    // Phase 3: Analysis
    printf("\n=== RESULTS ===\n\n");
    
    // Check 1: Was data consumed?
    uint32_t finalR = buf->readPos.load();
    uint32_t totalRead = finalR - startR;
    bool asioReading = (totalRead > 0);
    printf("[%s] ASIO Reading: %s (consumed %u samples)\n", 
           asioReading ? "PASS" : "FAIL", 
           asioReading ? "YES" : "NO - ASIO is not reading from ring buffer!", totalRead);
    
    // Check 2: Read rate stability
    if (readSamples.size() > 0 && asioReading) {
        double avgRead = 0;
        for (auto s : readSamples) avgRead += s;
        avgRead /= readSamples.size();
        
        double maxDev = 0;
        for (auto s : readSamples) {
            double dev = fabs((double)s - avgRead) / avgRead * 100.0;
            if (dev > maxDev) maxDev = dev;
        }
        
        double rateError = fabs(avgRead - sampleRate) / sampleRate * 100.0;
        
        printf("[%s] Read Rate: %.1f samples/sec (expected %.0f, error %.2f%%)\n",
               rateError < 1.0 ? "PASS" : (rateError < 10.0 ? "WARN" : "FAIL"),
               avgRead, sampleRate, rateError);
        printf("[%s] Rate Stability: max deviation %.2f%%\n",
               maxDev < 5.0 ? "PASS" : "WARN", maxDev);
    }
    
    // Check 3: Ring buffer avail stability
    uint32_t finalW = buf->writePos.load();
    int32_t finalAvail = (int32_t)(finalW - finalR);
    printf("[%s] Final Avail: %d samples (%.2f ms)\n",
           (finalAvail >= 0 && finalAvail < 10000) ? "PASS" : "WARN",
           finalAvail, finalAvail / sampleRate * 1000.0);
    
    // Check 4: Scan ring buffer for anomalies
    int nanCount = 0, infCount = 0, spikeCount = 0;
    float maxVal = 0;
    uint32_t scanStart = (finalR - 48000) & IPC_RING_MASK; // Last 1 second
    for (int i = 0; i < 48000; i++) {
        uint32_t idx = (scanStart + i) & IPC_RING_MASK;
        float vL = buf->ringL[idx];
        float vR = buf->ringR[idx];
        if (vL != vL || vR != vR) nanCount++;
        if (!isfinite(vL) || !isfinite(vR)) infCount++;
        float absL = fabs(vL), absR = fabs(vR);
        if (absL > 1.0f || absR > 1.0f) spikeCount++;
        if (absL > maxVal) maxVal = absL;
        if (absR > maxVal) maxVal = absR;
    }
    
    printf("[%s] NaN values: %d\n", nanCount == 0 ? "PASS" : "FAIL", nanCount);
    printf("[%s] Inf values: %d\n", infCount == 0 ? "PASS" : "FAIL", infCount);
    printf("[%s] Spike values (>1.0): %d\n", spikeCount == 0 ? "PASS" : "WARN", spikeCount);
    printf("[INFO] Max absolute value: %.6f\n", maxVal);
    
    printf("\n=== TEST COMPLETE ===\n");
    
    if (buf) UnmapViewOfFile(buf);
    if (g_hMap) CloseHandle(g_hMap);
    return 0;
}
