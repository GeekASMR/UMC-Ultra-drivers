/*
 * audio_diag2.cpp - Snapshot the current IPC ring buffer and analyze
 * 
 * Instead of waiting for live data, this reads whatever is currently
 * in the ring buffer (the last ~131072 samples worth) and analyzes it.
 * This works even if audio isn't currently playing.
 */
#include <windows.h>
#include <cstdio>
#include <cstdint>
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

// Write WAV file
void writeWav(const char* filename, float* left, float* right, int numSamples, int sampleRate) {
    FILE* f = fopen(filename, "wb");
    if (!f) { printf("Cannot create %s\n", filename); return; }
    
    uint32_t dataSize = numSamples * 2 * sizeof(int16_t);
    uint32_t fileSize = 36 + dataSize;
    
    fwrite("RIFF", 1, 4, f);
    fwrite(&fileSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t fmtSize = 16; fwrite(&fmtSize, 4, 1, f);
    uint16_t audioFormat = 1; fwrite(&audioFormat, 2, 1, f);
    uint16_t numChannels = 2; fwrite(&numChannels, 2, 1, f);
    uint32_t sr = sampleRate; fwrite(&sr, 4, 1, f);
    uint32_t byteRate = sampleRate * 4; fwrite(&byteRate, 4, 1, f);
    uint16_t blockAlign = 4; fwrite(&blockAlign, 2, 1, f);
    uint16_t bitsPerSample = 16; fwrite(&bitsPerSample, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, 4, 1, f);
    
    for (int i = 0; i < numSamples; i++) {
        float l = left[i]; if (l > 1.0f) l = 1.0f; if (l < -1.0f) l = -1.0f;
        float r = right[i]; if (r > 1.0f) r = 1.0f; if (r < -1.0f) r = -1.0f;
        int16_t sl = (int16_t)(l * 32767.0f);
        int16_t sr = (int16_t)(r * 32767.0f);
        fwrite(&sl, 2, 1, f);
        fwrite(&sr, 2, 1, f);
    }
    fclose(f);
    printf("  Written: %s (%d samples)\n", filename, numSamples);
}

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
                if (buf) { printf("  Connected: %s\n", name); return buf; }
                CloseHandle(h);
            }
        }
    }
    return nullptr;
}

void analyzeBuffer(const char* label, IpcAudioBuffer* buf) {
    if (!buf) { printf("\n%s: NOT CONNECTED\n", label); return; }
    
    uint32_t w = buf->writePos.load();
    uint32_t r = buf->readPos.load();
    int32_t avail = (int32_t)(w - r);
    
    printf("\n========== %s ==========\n", label);
    printf("writePos: %u  readPos: %u  avail: %d\n", w, r, avail);
    
    // Analyze the last 48000 samples (1 second) before writePos
    const int ANALYZE_COUNT = 48000;
    uint32_t startIdx = w - ANALYZE_COUNT;
    
    int nanCount = 0, infCount = 0, overrange = 0;
    int silentL = 0, silentR = 0;
    float peakL = 0, peakR = 0;
    double rmsL = 0, rmsR = 0;
    int glitchCount = 0;
    int zeroRunL = 0, maxZeroRunL = 0;
    
    float prevL = 0, prevR = 0;
    
    for (int i = 0; i < ANALYZE_COUNT; i++) {
        uint32_t idx = (startIdx + i) & IPC_RING_MASK;
        float l = buf->ringL[idx];
        float r = buf->ringR[idx];
        
        if (std::isnan(l) || std::isnan(r)) nanCount++;
        if (std::isinf(l) || std::isinf(r)) infCount++;
        if (fabsf(l) > 1.0f) overrange++;
        if (fabsf(r) > 1.0f) overrange++;
        
        if (fabsf(l) < 0.00001f) { silentL++; zeroRunL++; }
        else { if (zeroRunL > maxZeroRunL) maxZeroRunL = zeroRunL; zeroRunL = 0; }
        if (fabsf(r) < 0.00001f) silentR++;
        
        if (fabsf(l) > peakL) peakL = fabsf(l);
        if (fabsf(r) > peakR) peakR = fabsf(r);
        rmsL += (double)l * l;
        rmsR += (double)r * r;
        
        if (i > 0) {
            float dL = fabsf(l - prevL);
            float dR = fabsf(r - prevR);
            if (dL > 0.1f || dR > 0.1f) {
                glitchCount++;
                if (glitchCount <= 10) {
                    printf("  GLITCH @ offset %d: L=%.6f->%.6f (d=%.6f) R=%.6f->%.6f (d=%.6f)\n",
                        i, prevL, l, dL, prevR, r, dR);
                }
            }
        }
        prevL = l; prevR = r;
    }
    if (zeroRunL > maxZeroRunL) maxZeroRunL = zeroRunL;
    
    rmsL = sqrt(rmsL / ANALYZE_COUNT);
    rmsR = sqrt(rmsR / ANALYZE_COUNT);
    
    printf("NaN/Inf:          %d / %d\n", nanCount, infCount);
    printf("Overrange >1.0:   %d\n", overrange);
    printf("Silent (L/R):     %d / %d  (%.1f%% / %.1f%%)\n",
           silentL, silentR, 100.0*silentL/ANALYZE_COUNT, 100.0*silentR/ANALYZE_COUNT);
    printf("Max zero run:     %d samples (%.2fms @ 48kHz)\n", maxZeroRunL, maxZeroRunL*1000.0/48000);
    printf("Peak (L/R):       %.6f / %.6f\n", peakL, peakR);
    printf("RMS (L/R):        %.6f / %.6f\n", rmsL, rmsR);
    printf("Glitch count:     %d (>0.1 jump between samples)\n", glitchCount);
    
    // Print first 32 samples for visual inspection
    printf("\nFirst 32 samples from ring (at readPos):\n");
    for (int i = 0; i < 32; i++) {
        uint32_t idx = (r + i) & IPC_RING_MASK;
        printf("  [%2d] L=% .8f  R=% .8f\n", i, buf->ringL[idx], buf->ringR[idx]);
    }
    
    // Save WAV of last 1 second
    float wavL[48000], wavR[48000];
    for (int i = 0; i < 48000; i++) {
        uint32_t idx = (startIdx + i) & IPC_RING_MASK;
        wavL[i] = buf->ringL[idx];
        wavR[i] = buf->ringR[idx];
    }
    
    char wavName[256];
    snprintf(wavName, sizeof(wavName), "D:\\Autigravity\\UMCasio\\diag_%s.wav", label);
    writeWav(wavName, wavL, wavR, 48000, 48000);
}

int main() {
    printf("=== Audio Diagnostic Tool v2 (Snapshot) ===\n\n");
    
    IpcAudioBuffer* play0 = openIpc("PLAY", 0);
    IpcAudioBuffer* cap0 = openIpc("CAP", 0);
    
    analyzeBuffer("PLAY_0", play0);
    analyzeBuffer("CAP_0", cap0);
    
    printf("\n=== DONE ===\n");
    printf("Check diag_PLAY_0.wav and diag_CAP_0.wav in Audacity\n");
    return 0;
}
