/* ipc_data_check.cpp - 检查ring buffer里的实际数据格式 */
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

int main() {
    printf("=== IPC Data Format Check ===\n\n");
    
    const char* brands[] = { "AsmrtopWDM", "VirtualAudioWDM" };
    const char* prefixes[] = { "Global\\", "" };
    HANDLE hMap = NULL;
    IpcAudioBuffer* buf = nullptr;
    char name[256];
    
    for (auto brand : brands) {
        if (buf) break;
        for (auto prefix : prefixes) {
            snprintf(name, sizeof(name), "%s%s_PLAY_0", prefix, brand);
            hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
            if (hMap) {
                buf = (IpcAudioBuffer*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(IpcAudioBuffer));
                if (buf) { printf("Connected: %s\n", name); break; }
                CloseHandle(hMap); hMap = NULL;
            }
        }
    }
    if (!buf) { printf("Not found\n"); return 1; }
    
    printf("sizeof(IpcAudioBuffer) = %zu bytes\n", sizeof(IpcAudioBuffer));
    printf("offset writePos = %zu\n", offsetof(IpcAudioBuffer, writePos));
    printf("offset readPos  = %zu\n", offsetof(IpcAudioBuffer, readPos));
    printf("offset ringL    = %zu\n", offsetof(IpcAudioBuffer, ringL));
    printf("offset ringR    = %zu\n", offsetof(IpcAudioBuffer, ringR));
    
    uint32_t w = buf->writePos.load();
    uint32_t r = buf->readPos.load();
    printf("\nwritePos=%u  readPos=%u  avail=%d\n\n", w, r, (int32_t)(w-r));
    
    // Read 16 recent samples
    printf("=== Recent samples (as float) ===\n");
    int start = (w > 16) ? -16 : 0;
    for (int i = start; i < 0; i++) {
        uint32_t idx = (w + i) & IPC_RING_MASK;
        float vL = buf->ringL[idx];
        float vR = buf->ringR[idx];
        uint32_t rawL = *(uint32_t*)&vL;
        uint32_t rawR = *(uint32_t*)&vR;
        printf("  [%d] L=%.8f (0x%08X)  R=%.8f (0x%08X)\n", i, vL, rawL, vR, rawR);
    }
    
    // Scan last 1000 samples for stats
    printf("\n=== Stats (last 1000 samples) ===\n");
    int nanL=0, nanR=0, infL=0, infR=0;
    float minL=999, maxL=-999, minR=999, maxR=-999;
    int zeroL=0, zeroR=0;
    int bigL=0, bigR=0;
    
    for (int i = -1000; i < 0; i++) {
        uint32_t idx = (w + i) & IPC_RING_MASK;
        float vL = buf->ringL[idx];
        float vR = buf->ringR[idx];
        if (vL != vL) nanL++; if (vR != vR) nanR++;
        if (!isfinite(vL)) infL++; if (!isfinite(vR)) infR++;
        if (vL == 0.0f) zeroL++; if (vR == 0.0f) zeroR++;
        if (fabs(vL) > 1.0f) bigL++; if (fabs(vR) > 1.0f) bigR++;
        if (vL < minL) minL = vL; if (vL > maxL) maxL = vL;
        if (vR < minR) minR = vR; if (vR > maxR) maxR = vR;
    }
    
    printf("  NaN:      L=%d  R=%d\n", nanL, nanR);
    printf("  Inf:      L=%d  R=%d\n", infL, infR);
    printf("  Zero:     L=%d  R=%d\n", zeroL, zeroR);
    printf("  >1.0:     L=%d  R=%d\n", bigL, bigR);
    printf("  Range L:  [%.6f, %.6f]\n", minL, maxL);
    printf("  Range R:  [%.6f, %.6f]\n", minR, maxR);
    
    // Check if data looks like int16/int32 stored as float
    printf("\n=== Format detection ===\n");
    uint32_t idx0 = (w - 1) & IPC_RING_MASK;
    float v = buf->ringL[idx0];
    uint32_t raw = *(uint32_t*)&v;
    
    if (fabs(v) <= 1.0f && v == v && isfinite(v)) {
        printf("  Data appears to be FLOAT32 (values in [-1,1]) ✓\n");
    } else if (raw > 0x40000000) {
        printf("  WARNING: Data might be INT32 PCM stored in float buffer!\n");
        printf("  Raw value 0x%08X interpreted as float = %g\n", raw, v);
    }
    
    UnmapViewOfFile(buf);
    CloseHandle(hMap);
    return 0;
}
