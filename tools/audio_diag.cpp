/*
 * audio_diag.cpp - 直接从共享内存读取原始音频数据并分析
 * 
 * 诊断项：
 * 1. 检测 NaN / Infinity / 超范围值
 * 2. 检测波形断裂（相邻样本之间的巨大跳跃）
 * 3. 检测静音段
 * 4. 统计峰值、RMS
 * 5. 将原始PCM写入 WAV 文件以便在外部工具中查看
 */
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <atomic>
#include <vector>

#define IPC_RING_SIZE 131072
#define IPC_RING_MASK (IPC_RING_SIZE - 1)
#define SAMPLE_RATE 48000
#define CAPTURE_SECONDS 5

struct IpcAudioBuffer {
    std::atomic<uint32_t> writePos;
    std::atomic<uint32_t> readPos;
    float ringL[IPC_RING_SIZE];
    float ringR[IPC_RING_SIZE];
};

IpcAudioBuffer* openIpc(const char* direction, int id) {
    const char* brands[] = { "AsmrtopWDM", "VirtualAudioWDM" };
    const char* prefixes[] = { "Global\\", "" };
    char name[256];
    
    for (auto brand : brands) {
        for (auto prefix : prefixes) {
            snprintf(name, sizeof(name), "%s%s_%s_%d", prefix, brand, direction, id);
            HANDLE h = OpenFileMappingA(FILE_MAP_READ, FALSE, name);
            if (h) {
                auto* buf = (IpcAudioBuffer*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(IpcAudioBuffer));
                if (buf) {
                    printf("  Connected: %s\n", name);
                    return buf;
                }
                CloseHandle(h);
            }
        }
    }
    return nullptr;
}

// Write WAV file
void writeWav(const char* filename, const std::vector<float>& left, const std::vector<float>& right, int sampleRate) {
    FILE* f = fopen(filename, "wb");
    if (!f) { printf("Cannot create %s\n", filename); return; }
    
    uint32_t numSamples = (uint32_t)left.size();
    uint32_t dataSize = numSamples * 2 * sizeof(int16_t); // stereo 16-bit
    uint32_t fileSize = 36 + dataSize;
    
    // RIFF header
    fwrite("RIFF", 1, 4, f);
    fwrite(&fileSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    
    // fmt chunk
    fwrite("fmt ", 1, 4, f);
    uint32_t fmtSize = 16;
    fwrite(&fmtSize, 4, 1, f);
    uint16_t audioFormat = 1; // PCM
    fwrite(&audioFormat, 2, 1, f);
    uint16_t numChannels = 2;
    fwrite(&numChannels, 2, 1, f);
    uint32_t sr = sampleRate;
    fwrite(&sr, 4, 1, f);
    uint32_t byteRate = sampleRate * 2 * 2;
    fwrite(&byteRate, 4, 1, f);
    uint16_t blockAlign = 4;
    fwrite(&blockAlign, 2, 1, f);
    uint16_t bitsPerSample = 16;
    fwrite(&bitsPerSample, 2, 1, f);
    
    // data chunk
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);
    
    for (uint32_t i = 0; i < numSamples; i++) {
        float l = left[i]; if (l > 1.0f) l = 1.0f; if (l < -1.0f) l = -1.0f;
        float r = right[i]; if (r > 1.0f) r = 1.0f; if (r < -1.0f) r = -1.0f;
        int16_t sl = (int16_t)(l * 32767.0f);
        int16_t sr = (int16_t)(r * 32767.0f);
        fwrite(&sl, 2, 1, f);
        fwrite(&sr, 2, 1, f);
    }
    fclose(f);
    printf("  Written %s (%u samples, %.2fs)\n", filename, numSamples, (float)numSamples / sampleRate);
}

int main() {
    printf("=== Audio Diagnostic Tool ===\n\n");
    
    printf("Opening PLAY_0 (system audio -> DAW)...\n");
    IpcAudioBuffer* play = openIpc("PLAY", 0);
    
    if (!play) {
        printf("ERROR: Cannot open PLAY_0 shared memory. Is the WDM driver running?\n");
        return 1;
    }
    
    // Wait for audio to be flowing
    printf("\n*** Play some audio on your system now! ***\n");
    printf("Waiting for data...\n");
    
    uint32_t prevW = play->writePos.load();
    for (int i = 0; i < 30; i++) {
        Sleep(500);
        uint32_t w = play->writePos.load();
        if (w != prevW) {
            printf("Data detected! writePos moving: %u -> %u\n", prevW, w);
            break;
        }
        if (i == 29) {
            printf("No data for 15s, proceeding anyway...\n");
        }
    }
    
    // Phase 1: Capture raw ring buffer data for CAPTURE_SECONDS
    printf("\n--- Phase 1: Capturing %d seconds of raw ring buffer data ---\n", CAPTURE_SECONDS);
    
    uint32_t startW = play->writePos.load();
    uint32_t samples = SAMPLE_RATE * CAPTURE_SECONDS;
    std::vector<float> capturedL(samples);
    std::vector<float> capturedR(samples);
    
    uint32_t collected = 0;
    uint32_t lastW = startW;
    
    while (collected < samples) {
        Sleep(10); // 10ms poll
        uint32_t w = play->writePos.load();
        int32_t newSamples = (int32_t)(w - lastW);
        
        if (newSamples < 0) newSamples = 0;
        if (newSamples > IPC_RING_SIZE) newSamples = 0; // wrap protection
        
        for (int32_t i = 0; i < newSamples && collected < samples; i++) {
            uint32_t idx = (lastW + i) & IPC_RING_MASK;
            capturedL[collected] = play->ringL[idx];
            capturedR[collected] = play->ringR[idx];
            collected++;
        }
        lastW = w;
        
        // progress
        if (collected % SAMPLE_RATE == 0) {
            printf("  Captured: %u / %u samples\n", collected, samples);
        }
    }
    
    printf("  Capture complete: %u samples\n", collected);
    
    // Phase 2: Analyze the data
    printf("\n--- Phase 2: Analysis ---\n");
    
    int nanCountL = 0, nanCountR = 0;
    int infCountL = 0, infCountR = 0;
    int overrangeL = 0, overrangeR = 0;
    int silentL = 0, silentR = 0;
    float peakL = 0, peakR = 0;
    double rmsL = 0, rmsR = 0;
    int glitchCount = 0;
    int zeroRunL = 0, maxZeroRunL = 0;
    
    const float GLITCH_THRESHOLD = 0.1f; // adjacent sample difference > 0.1 is suspicious
    
    for (uint32_t i = 0; i < samples; i++) {
        float l = capturedL[i];
        float r = capturedR[i];
        
        // NaN / Inf check
        if (std::isnan(l)) nanCountL++;
        if (std::isnan(r)) nanCountR++;
        if (std::isinf(l)) infCountL++;
        if (std::isinf(r)) infCountR++;
        
        // Overrange check
        if (fabsf(l) > 1.0f) overrangeL++;
        if (fabsf(r) > 1.0f) overrangeR++;
        
        // Silence check
        if (fabsf(l) < 0.00001f) { silentL++; zeroRunL++; }
        else { if (zeroRunL > maxZeroRunL) maxZeroRunL = zeroRunL; zeroRunL = 0; }
        if (fabsf(r) < 0.00001f) silentR++;
        
        // Peak
        if (fabsf(l) > peakL) peakL = fabsf(l);
        if (fabsf(r) > peakR) peakR = fabsf(r);
        
        // RMS
        rmsL += (double)l * l;
        rmsR += (double)r * r;
        
        // Glitch detection (large jumps between adjacent samples)
        if (i > 0) {
            float diffL = fabsf(l - capturedL[i-1]);
            float diffR = fabsf(r - capturedR[i-1]);
            if (diffL > GLITCH_THRESHOLD || diffR > GLITCH_THRESHOLD) {
                glitchCount++;
                if (glitchCount <= 20) {
                    printf("  GLITCH @ sample %u: L[%u]=%.6f L[%u]=%.6f (dL=%.6f) R[%u]=%.6f R[%u]=%.6f (dR=%.6f)\n",
                        i, i-1, capturedL[i-1], i, l, diffL,
                        i-1, capturedR[i-1], i, r, diffR);
                }
            }
        }
    }
    
    rmsL = sqrt(rmsL / samples);
    rmsR = sqrt(rmsR / samples);
    
    printf("\n=== Results ===\n");
    printf("Samples captured:        %u\n", samples);
    printf("NaN (L/R):               %d / %d\n", nanCountL, nanCountR);
    printf("Infinity (L/R):          %d / %d\n", infCountL, infCountR);
    printf("Overrange >1.0 (L/R):    %d / %d\n", overrangeL, overrangeR);
    printf("Silent samples (L/R):    %d / %d  (%.1f%% / %.1f%%)\n", 
           silentL, silentR, 100.0 * silentL / samples, 100.0 * silentR / samples);
    printf("Max consecutive silence: %d samples (%.1fms)\n", maxZeroRunL, 1000.0 * maxZeroRunL / SAMPLE_RATE);
    printf("Peak (L/R):              %.6f / %.6f  (%.1f dB / %.1f dB)\n", 
           peakL, peakR, 20*log10(peakL+1e-10), 20*log10(peakR+1e-10));
    printf("RMS (L/R):               %.6f / %.6f  (%.1f dB / %.1f dB)\n",
           rmsL, rmsR, 20*log10(rmsL+1e-10), 20*log10(rmsR+1e-10));
    printf("Glitch count (dS>0.1):   %d  (%.1f per sec)\n", glitchCount, (float)glitchCount / CAPTURE_SECONDS);
    
    if (glitchCount > 20) {
        printf("  (showing first 20 only)\n");
    }
    
    // Phase 3: Save to WAV
    printf("\n--- Phase 3: Saving WAV ---\n");
    writeWav("D:\\Autigravity\\UMCasio\\diag_raw_ipc.wav", capturedL, capturedR, SAMPLE_RATE);
    
    printf("\n=== Diagnosis ===\n");
    if (nanCountL + nanCountR > 0) printf("!! CRITICAL: NaN values detected - memory corruption!\n");
    if (infCountL + infCountR > 0) printf("!! CRITICAL: Infinity values detected - overflow!\n");
    if (maxZeroRunL > 480) printf("!! WARNING: Long silence runs (%d samples = %.1fms) - buffer underruns!\n", maxZeroRunL, 1000.0*maxZeroRunL/SAMPLE_RATE);
    if (glitchCount > CAPTURE_SECONDS * 10) printf("!! WARNING: High glitch rate - possible SRC or phase discontinuity!\n");
    if (peakL < 0.001f && peakR < 0.001f) printf("!! WARNING: No audio signal detected!\n");
    
    if (nanCountL + nanCountR + infCountL + infCountR == 0 && 
        maxZeroRunL < 480 && glitchCount < CAPTURE_SECONDS * 5 && peakL > 0.001f) {
        printf(">> Ring buffer data looks CLEAN. Problem is likely in SRC/resampling layer.\n");
    }
    
    printf("\nDone. Check diag_raw_ipc.wav in Audacity for visual waveform inspection.\n");
    return 0;
}
