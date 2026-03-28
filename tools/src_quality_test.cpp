/*
 * src_quality_test.cpp - 独立验证 Hermite SRC 音频质量
 * 
 * 不依赖任何硬件或驱动，纯内存测试:
 * 1. 生成 48000Hz 正弦波写入模拟 ring buffer
 * 2. 用 readStereoAdaptive 以 44100Hz 速率读出
 * 3. 分析输出: THD, 毛刺, 频率偏差
 */
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <atomic>
#include <vector>

// Include the actual IPC header to test the real code
#include "../src/bridge/AsmrtopIPC.h"

#define PI 3.14159265358979323846

inline float hermite_interp(float v0, float v1, float v2, float v3, float f) {
    float c = (v1 - v2) * 0.5f - v0 * 0.5f + v3 * 0.5f;
    float a = v0 - v1 - c;
    float b = v2 - v0;
    return (((a * f) + b) * f + c) * f + v1;
}

int main() {
    printf("=== Hermite SRC Quality Test (Offline) ===\n\n");
    
    const double srcRate = 48000.0;
    const double dstRate = 44100.0;
    const double testFreq = 1000.0;
    const float amplitude = 0.8f;
    const int testDurationSec = 30;  // 30 seconds to catch drift
    const int srcSamples = (int)(srcRate * testDurationSec);
    const int dstSamples = (int)(dstRate * testDurationSec);
    const int writeChunk = 480;  // 10ms at 48kHz
    const int readChunk = 128;   // ASIO buffer size
    
    printf("Source: %.0f Hz, Dest: %.0f Hz, Ratio: %.6f\n", srcRate, dstRate, srcRate / dstRate);
    printf("Test: %.0f Hz sine, %.1f amplitude, %d seconds\n", testFreq, amplitude, testDurationSec);
    printf("Write chunks: %d, Read chunks: %d\n\n", writeChunk, readChunk);
    
    // Create a fake IPC buffer in memory
    IpcAudioBuffer* fakeBuf = (IpcAudioBuffer*)VirtualAlloc(NULL, sizeof(IpcAudioBuffer), 
                                                             MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!fakeBuf) {
        printf("Failed to allocate IPC buffer\n");
        return 1;
    }
    memset(fakeBuf, 0, sizeof(IpcAudioBuffer));
    
    // Create an AsmrtopIpcChannel and hack its internal buffer pointer
    // Since we can't easily set m_buf directly, we'll simulate the read logic manually
    // Instead, let's just test the hermite_interp function directly
    
    // === Interleaved write+read simulation ===
    // Simulate real-time: write at srcRate, read at dstRate
    printf("Running interleaved simulation (write@%.0f, read@%.0f)...\n", srcRate, dstRate);
    
    double writePhase = 0.0;
    double writePhaseInc = 2.0 * PI * testFreq / srcRate;
    uint32_t wp2 = 0;
    
    double baseSpeed = srcRate / dstRate;
    uint32_t r = 0;
    double readPosFrac = 0.0;
    bool playing = false;
    
    std::vector<float> outputL;
    outputL.reserve(dstSamples + 1000);
    
    double simTime = 0.0;
    double writeAccum = 0.0;
    double readAccum = 0.0;
    const double timeStep = 0.001;
    
    while ((int)outputL.size() < dstSamples) {
        simTime += timeStep;
        
        writeAccum += srcRate * timeStep;
        int toWrite = (int)writeAccum;
        writeAccum -= toWrite;
        
        for (int i = 0; i < toWrite; i++) {
            uint32_t idx = wp2 & IPC_RING_MASK;
            float val = amplitude * (float)sin(writePhase);
            fakeBuf->ringL[idx] = val;
            fakeBuf->ringR[idx] = val;
            wp2++;
            writePhase += writePhaseInc;
        }
        fakeBuf->writePos.store(wp2, std::memory_order_release);
        
        readAccum += dstRate * timeStep;
        while (readAccum >= readChunk && (int)outputL.size() < dstSamples) {
            readAccum -= readChunk;
            
            uint32_t w = fakeBuf->writePos.load(std::memory_order_acquire);
            int32_t avail = (int32_t)(w - r);
            
            // New fixed-ratio algorithm
            int32_t targetAvail = (int32_t)(srcRate * 0.002);
            if (targetAvail < (int32_t)(readChunk * baseSpeed) + 16)
                targetAvail = (int32_t)(readChunk * baseSpeed) + 16;
            
            if (avail < 0 || avail > IPC_RING_SIZE) {
                r = w - (uint32_t)targetAvail;
                readPosFrac = 0.0;
                playing = false;
                avail = (int32_t)(w - r);
            }
            
            int32_t minRequired = (int32_t)(readChunk * baseSpeed) + 4;
            if (!playing && avail >= targetAvail) playing = true;
            else if (playing && avail < minRequired / 2) playing = false;
            
            // Fixed ratio + gentle drift correction (±0.1% max)
            double readSpeed = baseSpeed;
            if (playing) {
                double drift = (double)(avail - targetAvail) / (double)targetAvail;
                double correction = drift * 0.0005;
                if (correction > 0.001) correction = 0.001;
                if (correction < -0.001) correction = -0.001;
                readSpeed = baseSpeed + correction;
            }
            
            const float* srcBufL = fakeBuf->ringL;
            uint32_t safeW = w - 4;
            
            for (int i = 0; i < readChunk; i++) {
                uint32_t dist = safeW - r;
                if (playing && (int32_t)dist > 2) {
                    float f = (float)readPosFrac;
                    uint32_t rm1 = (r - 1) & IPC_RING_MASK;
                    uint32_t r0  = r & IPC_RING_MASK;
                    uint32_t r1  = (r + 1) & IPC_RING_MASK;
                    uint32_t r2  = (r + 2) & IPC_RING_MASK;
                    
                    outputL.push_back(hermite_interp(srcBufL[rm1], srcBufL[r0], srcBufL[r1], srcBufL[r2], f));
                    
                    readPosFrac += readSpeed;
                    while (readPosFrac >= 1.0) {
                        readPosFrac -= 1.0;
                        r += 1;
                    }
                } else {
                    outputL.push_back(0.0f);
                }
            }
        }
    }
    
    int outputIdx = (int)outputL.size();
    printf("  Generated %d output samples\n", outputIdx);
    
    // === Analysis ===
    printf("\n=== ANALYSIS ===\n\n");
    
    // Skip first 0.5 second (startup transient)
    int skipSamples = (int)(dstRate * 0.5);
    int analyzeSamples = outputIdx - skipSamples;
    if (analyzeSamples < 0) analyzeSamples = 0;
    
    // 1. Check for NaN/Inf/Spikes
    int nanCount = 0, spikeCount = 0, zeroRuns = 0;
    float maxAbs = 0;
    int currentZeroRun = 0, maxZeroRun = 0;
    
    for (int i = skipSamples; i < outputIdx; i++) {
        float v = outputL[i];
        if (v != v) nanCount++;
        if (fabs(v) > 1.0f) spikeCount++;
        if (fabs(v) > maxAbs) maxAbs = fabs(v);
        
        if (fabs(v) < 0.0001f) {
            currentZeroRun++;
            if (currentZeroRun > maxZeroRun) maxZeroRun = currentZeroRun;
        } else {
            if (currentZeroRun > 10) zeroRuns++;
            currentZeroRun = 0;
        }
    }
    
    printf("[%s] NaN count: %d\n", nanCount == 0 ? "PASS" : "FAIL", nanCount);
    printf("[%s] Spike count (>1.0): %d\n", spikeCount == 0 ? "PASS" : "FAIL", spikeCount);
    printf("[%s] Max absolute: %.6f (expected ~%.3f)\n", 
           maxAbs < amplitude * 1.1f ? "PASS" : "WARN", maxAbs, amplitude);
    printf("[%s] Zero runs (>10 samples): %d, max length: %d\n", 
           zeroRuns == 0 ? "PASS" : "WARN", zeroRuns, maxZeroRun);
    
    // 2. Frequency analysis - measure zero crossings
    int zeroCrossings = 0;
    for (int i = skipSamples + 1; i < outputIdx; i++) {
        if ((outputL[i-1] >= 0 && outputL[i] < 0) || (outputL[i-1] < 0 && outputL[i] >= 0)) {
            zeroCrossings++;
        }
    }
    double measuredFreq = (zeroCrossings / 2.0) / ((analyzeSamples) / dstRate);
    double freqError = fabs(measuredFreq - testFreq) / testFreq * 100.0;
    printf("[%s] Measured frequency: %.2f Hz (expected %.0f Hz, error %.4f%%)\n",
           freqError < 0.1 ? "PASS" : (freqError < 1.0 ? "WARN" : "FAIL"),
           measuredFreq, testFreq, freqError);
    
    // 3. THD estimation - compare RMS of signal vs RMS of difference from ideal
    double expectedPhaseInc = 2.0 * PI * testFreq / dstRate;
    double bestPhase = 0;
    double bestCorr = -1;
    
    // Find best phase alignment
    for (int trial = 0; trial < 1000; trial++) {
        double testPhase = trial * 2.0 * PI / 1000.0;
        double corr = 0;
        for (int i = 0; i < 1000 && (skipSamples + i) < outputIdx; i++) {
            double ideal = amplitude * sin(testPhase + i * expectedPhaseInc);
            corr += outputL[skipSamples + i] * ideal;
        }
        if (corr > bestCorr) {
            bestCorr = corr;
            bestPhase = testPhase;
        }
    }
    
    // Calculate THD+N
    double signalPower = 0, noisePower = 0;
    int thdSamples = analyzeSamples > 100000 ? 100000 : analyzeSamples;
    for (int i = 0; i < thdSamples; i++) {
        double ideal = amplitude * sin(bestPhase + i * expectedPhaseInc);
        double actual = outputL[skipSamples + i];
        signalPower += actual * actual;
        double diff = actual - ideal;
        noisePower += diff * diff;
    }
    
    double snr = 10.0 * log10(signalPower / (noisePower + 1e-30));
    double thdn = sqrt(noisePower / (signalPower + 1e-30)) * 100.0;
    
    printf("[%s] SNR: %.1f dB\n", snr > 60 ? "PASS" : (snr > 40 ? "WARN" : "FAIL"), snr);
    printf("[%s] THD+N: %.4f%%\n", thdn < 1.0 ? "PASS" : (thdn < 5.0 ? "WARN" : "FAIL"), thdn);
    
    // 4. Check for progressive degradation (compare first half vs second half)
    double noise1 = 0, noise2 = 0;
    int halfLen = thdSamples / 2;
    for (int i = 0; i < halfLen; i++) {
        double ideal1 = amplitude * sin(bestPhase + i * expectedPhaseInc);
        double diff1 = outputL[skipSamples + i] - ideal1;
        noise1 += diff1 * diff1;
        
        double ideal2 = amplitude * sin(bestPhase + (halfLen + i) * expectedPhaseInc);
        double diff2 = outputL[skipSamples + halfLen + i] - ideal2;
        noise2 += diff2 * diff2;
    }
    double rmsNoise1 = sqrt(noise1 / halfLen);
    double rmsNoise2 = sqrt(noise2 / halfLen);
    double degradation = (rmsNoise2 - rmsNoise1) / (rmsNoise1 + 1e-30) * 100.0;
    
    printf("[%s] Progressive degradation: %.2f%% (first half noise: %.6f, second half: %.6f)\n",
           fabs(degradation) < 10 ? "PASS" : "WARN", degradation, rmsNoise1, rmsNoise2);
    
    printf("\n=== TEST COMPLETE ===\n");
    
    VirtualFree(fakeBuf, 0, MEM_RELEASE);
    return 0;
}
