#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#pragma comment(lib, "setupapi.lib")

static const GUID G = {0x215A80EF,0x69BD,0x4D85,{0xAC,0x71,0x0C,0x6E,0xA6,0xE6,0xBE,0x17}};

bool ioctl(HANDLE h, DWORD code, void* in, DWORD inSz, void* out, DWORD outSz, DWORD* ret=NULL) {
    DWORD br=0; BOOL ok = DeviceIoControl(h, code, in, inSz, out, outSz, &br, NULL);
    if (ret) *ret = br;
    return ok != FALSE;
}

int main() {
    printf("=== TUSBAUDIO Streaming Test ===\n\n");
    
    // Open device
    HDEVINFO di = SetupDiGetClassDevsW(&G, NULL, NULL, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA id = {}; id.cbSize = sizeof(id);
    SetupDiEnumDeviceInterfaces(di, NULL, &G, 0, &id);
    DWORD rs; SetupDiGetDeviceInterfaceDetailW(di, &id, NULL, 0, &rs, NULL);
    auto* dt = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)calloc(1,rs);
    dt->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    SetupDiGetDeviceInterfaceDetailW(di, &id, dt, rs, NULL, NULL);
    HANDLE h = CreateFileW(dt->DevicePath, GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    free(dt); SetupDiDestroyDeviceInfoList(di);
    if (h == INVALID_HANDLE_VALUE) { printf("Open fail\n"); return 1; }
    
    DWORD br;
    BYTE tmp[1040];
    
    // Init sequence
    ioctl(h, 0x80882004, NULL, 0, tmp, 24);          // GET_DEVICE_INFO
    ioctl(h, 0x80882820, NULL, 0, tmp, 4);            // GET_STATUS
    ioctl(h, 0x808820C4, NULL, 0, tmp, 1040);         // GET_DEVICE_PROPS
    DWORD mode = 0; ioctl(h, 0x80882804, &mode, 4, NULL, 0); // SET_MODE
    
    // Allocate control page (64KB) + channel buffers
    // Control page = first 64KB, then each channel gets 64KB
    int totalPages = 1 + 38; // 1 control + 38 channels
    BYTE* bigAlloc = (BYTE*)VirtualAlloc(NULL, (SIZE_T)totalPages * 0x10000,
                                          MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!bigAlloc) { printf("VirtualAlloc fail\n"); return 1; }
    printf("Allocated %d pages at %p\n", totalPages, bigAlloc);
    
    BYTE* controlPage = bigAlloc;
    
    // Create 3 events for SET_CALLBACKS
    HANDLE evt1 = CreateEventW(NULL, FALSE, FALSE, NULL); // auto-reset
    HANDLE evt2 = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE evt3 = CreateEventW(NULL, FALSE, FALSE, NULL);
    printf("Events: %p, %p, %p\n", evt1, evt2, evt3);
    
    // SET_CALLBACKS: try with events as handles
    BYTE cbBuf[32] = {};
    *(UINT64*)&cbBuf[0]  = (UINT64)(uintptr_t)controlPage; // shared memory addr
    *(UINT64*)&cbBuf[8]  = (UINT64)(uintptr_t)evt1;        // event handle 1
    *(UINT64*)&cbBuf[16] = (UINT64)(uintptr_t)evt2;        // event handle 2
    *(UINT64*)&cbBuf[24] = (UINT64)(uintptr_t)evt3;        // event handle 3
    
    bool ok = ioctl(h, 0x80882880, cbBuf, 32, NULL, 0);
    printf("SET_CALLBACKS: %s (err=%lu)\n", ok?"OK":"FAIL", ok?0:GetLastError());
    
    // GET_STREAM_CONFIG
    BYTE config[292]; ioctl(h, 0x80882808, NULL, 0, config, 292);
    printf("Rate: %u\n", *(DWORD*)config);
    
    // GET_CHANNEL_LIST
    BYTE chList[8200] = {};
    ioctl(h, 0x8088280C, NULL, 0, chList, 8200, &br);
    DWORD numIn = *(DWORD*)&chList[0];
    DWORD numOut = *(DWORD*)&chList[4100];
    printf("Channels: %u in + %u out\n", numIn, numOut);
    
    // SET_BUFFER_SIZE
    DWORD bufSize = 128;
    ok = ioctl(h, 0x80882824, &bufSize, 4, NULL, 0);
    printf("SET_BUFFER_SIZE(%u): %s\n", bufSize, ok?"OK":"FAIL");
    
    // SELECT + MAP just first 2 input and 2 output channels for testing
    int testChannels[][2] = {{0, 0}, {0, 1}, {4100, 0}, {4100, 1}}; // block_offset, index
    BYTE* dmaBase = bigAlloc + 0x10000; // skip control page
    
    for (int i = 0; i < 4; i++) {
        int blockOff = testChannels[i][0];
        int idx = testChannels[i][1];
        DWORD chOff = blockOff + 4 + idx * 16;
        
        // SELECT_CHANNEL
        ok = ioctl(h, 0x80882840, &chList[chOff], 16, NULL, 0);
        DWORD chId = *(DWORD*)&chList[chOff + 8];
        printf("SELECT ch 0x%02X: %s\n", chId, ok?"OK":"FAIL");
        
        // MAP_CHANNEL_BUFFER
        BYTE mapBuf[24] = {};
        *(DWORD*)&mapBuf[0] = chId;
        *(DWORD*)&mapBuf[4] = *(DWORD*)&chList[chOff + 12]; // type
        *(DWORD*)&mapBuf[8] = bufSize * 4; // buffer bytes
        *(DWORD*)&mapBuf[12] = 32; // bits
        *(UINT64*)&mapBuf[16] = (UINT64)(uintptr_t)(dmaBase + i * 0x10000);
        
        ok = ioctl(h, 0x808828A0, mapBuf, 24, NULL, 0);
        printf("MAP ch 0x%02X -> %p: %s (err=%lu)\n", chId, dmaBase + i*0x10000, ok?"OK":"FAIL", ok?0:GetLastError());
    }
    
    // START_STREAMING
    printf("\nSTART_STREAMING...\n");
    ok = ioctl(h, 0x808828C8, NULL, 0, NULL, 0);
    printf("START: %s (err=%lu)\n", ok?"OK":"FAIL", ok?0:GetLastError());
    
    if (ok) {
        // Wait for events
        printf("Waiting for buffer switch events (2 seconds)...\n");
        HANDLE events[] = {evt1, evt2, evt3};
        int switchCount = 0;
        DWORD startTime = GetTickCount();
        
        while (GetTickCount() - startTime < 2000) {
            DWORD wait = WaitForMultipleObjects(3, events, FALSE, 10);
            if (wait >= WAIT_OBJECT_0 && wait < WAIT_OBJECT_0 + 3) {
                switchCount++;
                int evtIdx = wait - WAIT_OBJECT_0;
                if (switchCount <= 5)
                    printf("  Event %d signaled! (count=%d)\n", evtIdx, switchCount);
            }
            // Also check control page for changes
            if (switchCount == 1) {
                printf("  Control page first 32 bytes: ");
                for (int j = 0; j < 32; j++) printf("%02X ", controlPage[j]);
                printf("\n");
            }
        }
        printf("Total events in 2s: %d (%.1f Hz)\n", switchCount, switchCount / 2.0);
        
        // Check DMA buffer for audio data
        printf("\nInput ch0 first 16 samples: ");
        float* inBuf = (float*)(dmaBase);
        for (int j = 0; j < 16; j++) printf("%.6f ", inBuf[j]);
        printf("\n");
        
        // STOP
        printf("\nSTOP_STREAMING...\n");
        ioctl(h, 0x808828CC, NULL, 0, NULL, 0);
        printf("Stopped\n");
    }
    
    VirtualFree(bigAlloc, 0, MEM_RELEASE);
    CloseHandle(evt1); CloseHandle(evt2); CloseHandle(evt3);
    CloseHandle(h);
    return 0;
}
