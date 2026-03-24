/*
 * src_diag.cpp - Simulate readStereoAdaptive exactly as the ASIO driver 
 * does it, capture output, save to WAV for comparison
 */
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <atomic>
#include <vector>

#define IPC_RING_SIZE 131072
#define IPC_RING_MASK (IPC_RING_SIZE - 1)

struct IpcAudioBuffer {
    std::atomic<uint32_t> writePos;
    std::atomic<uint32_t> readPos;
    float ringL[IPC_RING_SIZE];
    float ringR[IPC_RING_SIZE];
};

inline float hermite_interp(float v0, float v1, float v2, float v3, float f) {
    float c0 = v1;
    float c1 = 0.5f * (v2 - v0);
    float c2 = v0 - 2.5f * v1 + 2.0f * v2 - 0.5f * v3;
    float c3 = 0.5f * (v3 - v0) + 1.5f * (v1 - v2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

void writeWav(const char* filename, const std::vector<float>& left, const std::vector<float>& right, int sampleRate) {
    FILE* f = fopen(filename, "wb");
    if (!f) return;
    uint32_t numSamples = (uint32_t)left.size();
    uint32_t dataSize = numSamples * 2 * sizeof(int16_t);
    uint32_t fileSize = 36 + dataSize;
    fwrite("RIFF", 1, 4, f); fwrite(&fileSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f); fwrite("fmt ", 1, 4, f);
    uint32_t fmtSize = 16; fwrite(&fmtSize, 4, 1, f);
    uint16_t audioFormat = 1; fwrite(&audioFormat, 2, 1, f);
    uint16_t numChannels = 2; fwrite(&numChannels, 2, 1, f);
    uint32_t sr = sampleRate; fwrite(&sr, 4, 1, f);
    uint32_t byteRate = sampleRate * 4; fwrite(&byteRate, 4, 1, f);
    uint16_t blockAlign = 4; fwrite(&blockAlign, 2, 1, f);
    uint16_t bitsPerSample = 16; fwrite(&bitsPerSample, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataSize, 4, 1, f);
    for (uint32_t i = 0; i < numSamples; i++) {
        float l = left[i]; if (l > 1.0f) l = 1.0f; if (l < -1.0f) l = -1.0f;
        float r = right[i]; if (r > 1.0f) r = 1.0f; if (r < -1.0f) r = -1.0f;
        int16_t sl = (int16_t)(l * 32767.0f); int16_t sr2 = (int16_t)(r * 32767.0f);
        fwrite(&sl, 2, 1, f); fwrite(&sr2, 2, 1, f);
    }
    fclose(f);
    printf("  Written: %s (%u samples)\n", filename, numSamples);
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

int main() {
    printf("=== SRC Diagnostic Tool ===\n\n");
    
    IpcAudioBuffer* play = openIpc("PLAY", 0);
    if (!play) { printf("ERROR: Cannot open PLAY_0\n"); return 1; }
    
    uint32_t w = play->writePos.load();
    printf("Current writePos: %u\n", w);
    
    // Test 1: Direct ring buffer copy (NO SRC, NO Hermite) -> baseline
    printf("\n--- Test 1: Direct copy from ring (no SRC) ---\n");
    {
        int numSamples = 48000; // 1 second
        uint32_t startR = w - numSamples - 8192; // 8192 behind writePos
        std::vector<float> outL(numSamples), outR(numSamples);
        
        for (int i = 0; i < numSamples; i++) {
            uint32_t idx = (startR + i) & IPC_RING_MASK;
            outL[i] = play->ringL[idx];
            outR[i] = play->ringR[idx];
        }
        writeWav("D:\\Autigravity\\UMCasio\\diag_direct.wav", outL, outR, 48000);
    }
    
    // Test 2: Hermite SRC with ratio=1.0 (same rate simulation)
    printf("\n--- Test 2: Hermite SRC ratio=1.0 ---\n");
    {
        int numSamples = 48000;
        uint32_t r = w - numSamples - 8192;
        double readPosFrac = 0.0;
        const int BLOCK_SIZE = 128; // typical ASIO buffer size
        int totalBlocks = numSamples / BLOCK_SIZE;
        double ratio = 1.0;
        
        std::vector<float> outL(numSamples), outR(numSamples);
        int outIdx = 0;
        
        for (int b = 0; b < totalBlocks; b++) {
            for (int i = 0; i < BLOCK_SIZE; i++) {
                double srcPos = readPosFrac + i * ratio;
                int s = (int)srcPos;
                float f = (float)(srcPos - s);
                
                uint32_t idx0 = (r + s - 1) & IPC_RING_MASK;
                uint32_t idx1 = (r + s) & IPC_RING_MASK;
                uint32_t idx2 = (r + s + 1) & IPC_RING_MASK;
                uint32_t idx3 = (r + s + 2) & IPC_RING_MASK;
                
                outL[outIdx] = hermite_interp(play->ringL[idx0], play->ringL[idx1], play->ringL[idx2], play->ringL[idx3], f);
                outR[outIdx] = hermite_interp(play->ringR[idx0], play->ringR[idx1], play->ringR[idx2], play->ringR[idx3], f);
                outIdx++;
            }
            
            double consumed = readPosFrac + BLOCK_SIZE * ratio;
            int intConsumed = (int)consumed;
            readPosFrac = consumed - (double)intConsumed;
            r += intConsumed;
        }
        
        writeWav("D:\\Autigravity\\UMCasio\\diag_hermite_1_0.wav", outL, outR, 48000);
    }
    
    // Test 3: Compare direct vs hermite to find differences
    printf("\n--- Test 3: Difference analysis ---\n");
    {
        int numSamples = 48000;
        uint32_t startR = w - numSamples - 8192;
        
        // Direct
        std::vector<float> directL(numSamples), directR(numSamples);
        for (int i = 0; i < numSamples; i++) {
            uint32_t idx = (startR + i) & IPC_RING_MASK;
            directL[i] = play->ringL[idx];
            directR[i] = play->ringR[idx];
        }
        
        // Hermite
        uint32_t r = startR;
        double readPosFrac = 0.0;
        const int BLOCK_SIZE = 128;
        int totalBlocks = numSamples / BLOCK_SIZE;
        double ratio = 1.0;
        
        std::vector<float> hermL(numSamples), hermR(numSamples);
        int outIdx = 0;
        
        for (int b = 0; b < totalBlocks; b++) {
            for (int i = 0; i < BLOCK_SIZE; i++) {
                double srcPos = readPosFrac + i * ratio;
                int s = (int)srcPos;
                float f = (float)(srcPos - s);
                
                uint32_t idx0 = (r + s - 1) & IPC_RING_MASK;
                uint32_t idx1 = (r + s) & IPC_RING_MASK;
                uint32_t idx2 = (r + s + 1) & IPC_RING_MASK;
                uint32_t idx3 = (r + s + 2) & IPC_RING_MASK;
                
                hermL[outIdx] = hermite_interp(play->ringL[idx0], play->ringL[idx1], play->ringL[idx2], play->ringL[idx3], f);
                hermR[outIdx] = hermite_interp(play->ringR[idx0], play->ringR[idx1], play->ringR[idx2], play->ringR[idx3], f);
                outIdx++;
            }
            
            double consumed = readPosFrac + BLOCK_SIZE * ratio;
            int intConsumed = (int)consumed;
            readPosFrac = consumed - (double)intConsumed;
            r += intConsumed;
        }
        
        // Compute difference
        float maxDiff = 0;
        int maxDiffIdx = 0;
        for (int i = 0; i < numSamples; i++) {
            float d = fabsf(directL[i] - hermL[i]);
            if (d > maxDiff) { maxDiff = d; maxDiffIdx = i; }
        }
        printf("  Max diff between direct and Hermite(1.0): %.10f at sample %d\n", maxDiff, maxDiffIdx);
        
        // Check if direct is clean
        int directGlitches = 0;
        for (int i = 1; i < numSamples; i++) {
            float d = fabsf(directL[i] - directL[i-1]);
            if (d > 0.5f) directGlitches++;
        }
        printf("  Massive glitches in direct copy (>0.5): %d\n", directGlitches);
    }
    
    // Test 4: Check if data wraps around ring correctly
    printf("\n--- Test 4: Ring wrap boundary check ---\n");
    {
        uint32_t ringPos = w & IPC_RING_MASK;
        printf("  writePos mod IPC_RING_SIZE = %u\n", ringPos);
        printf("  Ring position where write pointer currently is:\n");
        for (int i = -5; i <= 5; i++) {
            uint32_t idx = (w + i) & IPC_RING_MASK;
            printf("    [w%+d] idx=%6u  L=%.8f  R=%.8f\n", i, idx, play->ringL[idx], play->ringR[idx]);
        }
    }
    
    printf("\n=== DONE ===\n");
    printf("Compare diag_direct.wav (clean baseline) with diag_hermite_1_0.wav in Audacity\n");
    printf("If both sound identical and clean -> problem is in PI controller or readPos tracking\n");
    printf("If hermite has noise -> problem is in Hermite interpolation\n");
    printf("If direct has noise -> problem is in kernel WDM driver data\n");
    
    return 0;
}
