/*
 * ipc_output_test.cpp - Test writing a sine tone to REC_0 (Virtual Mic)
 *
 * Generates a 440Hz sine wave and writes it to the REC_0 IPC ring buffer.
 * If working correctly, you should hear/see it on Mic 1/2 in Windows Sound settings.
 */
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <atomic>

#define IPC_RING_SIZE 131072
#define IPC_RING_MASK (IPC_RING_SIZE - 1)
#define SAMPLE_RATE 48000
#define TONE_FREQ 440.0
#define PI 3.14159265358979323846

struct IpcAudioBuffer {
    std::atomic<uint32_t> writePos;
    std::atomic<uint32_t> readPos;
    float ringL[IPC_RING_SIZE];
    float ringR[IPC_RING_SIZE];
};

struct IpcHandle {
    HANDLE hMap = NULL;
    IpcAudioBuffer* buf = nullptr;

    bool tryOpen(const char* direction, int id) {
        if (buf) return true;
        const char* brands[] = { "AsmrtopWDM", "VirtualAudioWDM" };
        char name[256];
        for (auto brand : brands) {
            snprintf(name, sizeof(name), "Global\\%s_%s_%d", brand, direction, id);
            hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
            if (hMap) {
                buf = (IpcAudioBuffer*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(IpcAudioBuffer));
                if (buf) {
                    printf("Connected: %s\n", name);
                    return true;
                }
                CloseHandle(hMap);
                hMap = NULL;
            }
        }
        return false;
    }
};

int main() {
    printf("=== IPC Output Test (REC_0 Sine Wave) ===\n");
    printf("Writing 440Hz sine to Virtual Mic 1/2...\n");
    printf("Open Windows Sound Settings > Recording to see Mic 1/2 level.\n\n");

    IpcHandle rec;

    // Wait for REC_0 to become available
    printf("Waiting for REC_0 shared memory...\n");
    printf("  (Open an app that uses Mic 1/2 to trigger kernel to create it)\n");

    while (!rec.tryOpen("REC", 0)) {
        Sleep(500);
        printf(".");
    }
    printf("\nConnected! Starting sine wave output...\n\n");

    double phase = 0.0;
    double phaseInc = 2.0 * PI * TONE_FREQ / SAMPLE_RATE;
    int blockSize = 480; // 10ms blocks

    float blockL[480];
    float blockR[480];

    for (int sec = 0; ; sec++) {
        // Write 1 second of audio (100 blocks of 480 samples)
        for (int blk = 0; blk < 100; blk++) {
            // Generate sine
            for (int i = 0; i < blockSize; i++) {
                float sample = (float)(sin(phase) * 0.5); // -6dB
                blockL[i] = sample;
                blockR[i] = sample;
                phase += phaseInc;
                if (phase >= 2.0 * PI) phase -= 2.0 * PI;
            }

            // Write to ring buffer
            uint32_t wp = rec.buf->writePos.load(std::memory_order_relaxed);
            for (int i = 0; i < blockSize; i++) {
                uint32_t idx = (wp + i) & IPC_RING_MASK;
                rec.buf->ringL[idx] = blockL[i];
                rec.buf->ringR[idx] = blockR[i];
            }
            rec.buf->writePos.store(wp + blockSize, std::memory_order_release);

            Sleep(10); // ~10ms per block
        }

        uint32_t w = rec.buf->writePos.load();
        uint32_t r = rec.buf->readPos.load();
        int32_t avail = (int32_t)(w - r);
        printf("t=%d  W=%u  R=%u  Avail=%d  (Rd/s=%u)\n", sec, w, r, avail, r);
    }

    return 0;
}
