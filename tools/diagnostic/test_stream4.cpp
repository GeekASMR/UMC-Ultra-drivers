#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#pragma comment(lib, "setupapi.lib")

static const GUID G = {0x215A80EF,0x69BD,0x4D85,{0xAC,0x71,0x0C,0x6E,0xA6,0xE6,0xBE,0x17}};

bool io(HANDLE h, DWORD code, void* in, DWORD inSz, void* out=NULL, DWORD outSz=0, DWORD* pRet=NULL) {
    DWORD br; BOOL ok = DeviceIoControl(h, code, in, inSz, out, outSz, &br, NULL);
    if (pRet) *pRet = br; return ok!=FALSE;
}

int main() {
    printf("=== Stream Test v4 - Events + Sync IO ===\n\n");
    
    HDEVINFO di = SetupDiGetClassDevsW(&G, NULL, NULL, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA id = {}; id.cbSize = sizeof(id);
    SetupDiEnumDeviceInterfaces(di, NULL, &G, 0, &id);
    DWORD rs; SetupDiGetDeviceInterfaceDetailW(di, &id, NULL, 0, &rs, NULL);
    auto* dt = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)calloc(1,rs);
    dt->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    SetupDiGetDeviceInterfaceDetailW(di, &id, dt, rs, NULL, NULL);
    // Open WITHOUT FILE_FLAG_OVERLAPPED (sync mode)
    HANDLE h = CreateFileW(dt->DevicePath, GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    free(dt); SetupDiDestroyDeviceInfoList(di);
    if (h == INVALID_HANDLE_VALUE) { printf("Open fail %lu\n", GetLastError()); return 1; }
    
    DWORD br; BYTE tmp[1040];
    io(h, 0x80882004, NULL, 0, tmp, 24);
    io(h, 0x80882820, NULL, 0, tmp, 4);
    io(h, 0x808820C4, NULL, 0, tmp, 1040);
    DWORD mode = 0; io(h, 0x80882804, &mode, 4);
    
    // Create 3 AUTO-RESET events (matching official DLL pattern)
    HANDLE evt1 = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE evt2 = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE evt3 = CreateEventW(NULL, FALSE, FALSE, NULL);
    printf("Events: %p %p %p\n", evt1, evt2, evt3);
    
    // Alloc control page + DMA
    BYTE* mem = (BYTE*)VirtualAlloc(NULL, 40*0x10000, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    BYTE* ctrl = mem;
    BYTE* dma = mem + 0x10000;
    
    // SET_CALLBACKS: addr + 3 event handles
    BYTE cb[32] = {};
    *(UINT64*)&cb[0]  = (UINT64)(uintptr_t)ctrl;
    *(UINT64*)&cb[8]  = (UINT64)(uintptr_t)evt1;
    *(UINT64*)&cb[16] = (UINT64)(uintptr_t)evt2;
    *(UINT64*)&cb[24] = (UINT64)(uintptr_t)evt3;
    bool ok = io(h, 0x80882880, cb, 32);
    printf("SET_CALLBACKS: %s (err=%lu)\n", ok?"OK":"FAIL", ok?0:GetLastError());
    
    BYTE config[292]; io(h, 0x80882808, NULL, 0, config, 292);
    BYTE chList[8200] = {}; io(h, 0x8088280C, NULL, 0, chList, 8200, &br);
    DWORD numIn = *(DWORD*)&chList[0];
    DWORD numOut = *(DWORD*)&chList[4100];
    printf("Ch: %u in + %u out\n", numIn, numOut);
    
    DWORD bufSz = 128; io(h, 0x80882824, &bufSz, 4);
    
    // Map all channels
    int chIdx = 0;
    auto setup = [&](DWORD blk, DWORD num) {
        for (DWORD i = 0; i < num; i++) {
            DWORD off = blk + 4 + i * 16;
            io(h, 0x80882840, &chList[off], 16);
            DWORD chId = *(DWORD*)&chList[off+8];
            BYTE m[24] = {};
            *(DWORD*)&m[0] = chId;
            *(DWORD*)&m[4] = *(DWORD*)&chList[off+12];
            *(DWORD*)&m[8] = bufSz * 4;
            *(DWORD*)&m[12] = 32;
            *(UINT64*)&m[16] = (UINT64)(uintptr_t)(dma + chIdx * 0x10000);
            io(h, 0x808828A0, m, 24);
            chIdx++;
        }
    };
    setup(0, numIn);
    setup(4100, numOut);
    printf("Mapped %d ch\n", chIdx);
    
    // START
    ok = io(h, 0x808828C8, NULL, 0);
    printf("START: %s\n", ok?"OK":"FAIL");
    
    HANDLE events[] = {evt1, evt2, evt3};
    int switchCount = 0;
    DWORD t0 = GetTickCount();
    
    while (GetTickCount() - t0 < 3000) {
        DWORD wait = WaitForMultipleObjects(3, events, FALSE, 50);
        if (wait >= WAIT_OBJECT_0 && wait < WAIT_OBJECT_0+3) {
            switchCount++;
            if (switchCount <= 10) {
                int ei = wait - WAIT_OBJECT_0;
                printf("  [%4ums] Event %d! count=%d\n", GetTickCount()-t0, ei, switchCount);
                
                // Check control page
                printf("    ctrl[0-7]: ");
                for (int j = 0; j < 8; j++) printf("%02X ", ctrl[j]);
                printf("\n");
                
                // Check DMA
                float* in0 = (float*)dma;
                printf("    In0: %.6f %.6f %.6f %.6f\n", in0[0], in0[1], in0[2], in0[3]);
            }
        }
    }
    
    printf("\nEvents in 3s: %d (%.1f Hz)\n", switchCount, switchCount/3.0);
    
    io(h, 0x808828CC, NULL, 0);
    VirtualFree(mem, 0, MEM_RELEASE);
    CloseHandle(evt1); CloseHandle(evt2); CloseHandle(evt3);
    CloseHandle(h);
    return 0;
}
