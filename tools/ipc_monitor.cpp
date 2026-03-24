/*
 * ipc_monitor.cpp - ASMRTOP IPC ring buffer realtime monitor
 *
 * Auto-reconnects every second. Start playing audio to Virtual endpoints
 * and watch the data flow.
 */
#include <windows.h>
#include <iostream>
#include <atomic>
#include <cstdint>

#define IPC_RING_SIZE 131072

struct IpcAudioBuffer {
    std::atomic<uint32_t> writePos;
    std::atomic<uint32_t> readPos;
    float ringL[IPC_RING_SIZE];
    float ringR[IPC_RING_SIZE];
};

struct IpcHandle {
    HANDLE hMap = NULL;
    IpcAudioBuffer* buf = nullptr;

    void tryOpen(const char* direction, int id) {
        if (buf) return; // already connected
        const char* brands[] = { "AsmrtopWDM", "VirtualAudioWDM" };
        const char* prefixes[] = { "Global\\", "" };
        char name[256];
        for (auto brand : brands) {
            for (auto prefix : prefixes) {
                snprintf(name, sizeof(name), "%s%s_%s_%d", prefix, brand, direction, id);
                hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, name);
                if (hMap) {
                    buf = (IpcAudioBuffer*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(IpcAudioBuffer));
                    if (buf) {
                        printf("  >> Connected: %s\n", name);
                        return;
                    }
                    CloseHandle(hMap);
                    hMap = NULL;
                }
            }
        }
    }
};

int main() {
    printf("=== IPC Ring Buffer Monitor (auto-reconnect) ===\n");
    printf("Play audio to Virtual 1/2 or open Mic 1/2 to see data.\n\n");

    IpcHandle play[4], rec[4];
    uint32_t prevW[4] = {}, prevR[4] = {};
    uint32_t prevRecW[4] = {}, prevRecR[4] = {};

    printf("%-5s", "Time");
    for (int i = 0; i < 4; i++) printf(" | PLAY%d_W    PLAY%d_R    Avail  Wr/s   Rd/s", i, i);
    printf("\n");

    for (int sec = 0; ; sec++) {
        // Auto-reconnect
        for (int i = 0; i < 4; i++) {
            play[i].tryOpen("PLAY", i);
            rec[i].tryOpen("REC", i);
        }

        printf("t=%-3d", sec);

        for (int i = 0; i < 4; i++) {
            if (play[i].buf) {
                uint32_t w = play[i].buf->writePos.load();
                uint32_t r = play[i].buf->readPos.load();
                int32_t avail = (int32_t)(w - r);
                uint32_t wr = w - prevW[i];
                uint32_t rd = r - prevR[i];
                printf(" | W=%-8u R=%-8u A=%-5d Wr=%-5u Rd=%-5u", w, r, avail, wr, rd);
                prevW[i] = w; prevR[i] = r;
            } else {
                printf(" | PLAY%d --                              ", i);
            }
        }

        // Also show REC channels on a second line if connected
        bool anyRec = false;
        for (int i = 0; i < 4; i++) if (rec[i].buf) anyRec = true;

        if (anyRec) {
            printf("\n     ");
            for (int i = 0; i < 4; i++) {
                if (rec[i].buf) {
                    uint32_t w = rec[i].buf->writePos.load();
                    uint32_t r = rec[i].buf->readPos.load();
                    int32_t avail = (int32_t)(w - r);
                    uint32_t wr = w - prevRecW[i];
                    uint32_t rd = r - prevRecR[i];
                    printf(" | REC%d W=%-6u R=%-6u A=%-4d Wr=%-5u", i, w, r, avail, wr);
                    prevRecW[i] = w; prevRecR[i] = r;
                }
            }
        }

        printf("\n");
        Sleep(1000);
    }
    return 0;
}
