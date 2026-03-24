// test_stream5 - 完全模拟官方 DLL 的行为
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#pragma comment(lib, "setupapi.lib")

static const GUID G = {0x215A80EF,0x69BD,0x4D85,{0xAC,0x71,0x0C,0x6E,0xA6,0xE6,0xBE,0x17}};

static HANDLE g_ovEvt = NULL;
static OVERLAPPED g_ov = {};

bool io(HANDLE h, DWORD code, void* in, DWORD inSz, void* out=NULL, DWORD outSz=0, DWORD* pRet=NULL) {
    memset(&g_ov, 0, sizeof(g_ov));
    g_ov.hEvent = g_ovEvt;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, code, in, inSz, out, outSz, &br, &g_ov);
    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            ok = GetOverlappedResult(h, &g_ov, &br, TRUE);
        }
    }
    if (pRet) *pRet = br;
    return ok != FALSE;
}

// bufferSwitch event tracking
static volatile LONG g_switchCount = 0;
static HANDLE g_switchEvent = NULL;

int main() {
    printf("=== Stream Test v5 - Full Official Protocol ===\n\n");
    
    // Create events in SAME order as official DLL
    HANDLE evt_auto1 = CreateEventW(NULL, FALSE, FALSE, NULL); // AUTO
    HANDLE evt_auto2 = CreateEventW(NULL, FALSE, FALSE, NULL); // AUTO
    HANDLE evt_auto3 = CreateEventW(NULL, FALSE, FALSE, NULL); // AUTO
    HANDLE evt_man1  = CreateEventW(NULL, TRUE, FALSE, NULL);  // MANUAL
    HANDLE evt_man2  = CreateEventW(NULL, TRUE, FALSE, NULL);  // MANUAL
    HANDLE evt_man3  = CreateEventW(NULL, TRUE, FALSE, NULL);  // MANUAL
    HANDLE evt_man4  = CreateEventW(NULL, TRUE, FALSE, NULL);  // MANUAL
    HANDLE evt_man5  = CreateEventW(NULL, TRUE, FALSE, NULL);  // MANUAL
    HANDLE evt_man6  = CreateEventW(NULL, TRUE, FALSE, NULL);  // MANUAL
    g_switchEvent = evt_auto1;
    
    printf("Events AUTO: %p %p %p\n", evt_auto1, evt_auto2, evt_auto3);
    
    // Open device (FILE_FLAG_OVERLAPPED like official)
    HDEVINFO di = SetupDiGetClassDevsW(&G, NULL, NULL, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA id = {}; id.cbSize = sizeof(id);
    SetupDiEnumDeviceInterfaces(di, NULL, &G, 0, &id);
    DWORD rs; SetupDiGetDeviceInterfaceDetailW(di, &id, NULL, 0, &rs, NULL);
    auto* dt = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)calloc(1,rs);
    dt->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    SetupDiGetDeviceInterfaceDetailW(di, &id, dt, rs, NULL, NULL);
    HANDLE h = CreateFileW(dt->DevicePath, GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 
        FILE_FLAG_OVERLAPPED, NULL); // OVERLAPPED like official
    free(dt); SetupDiDestroyDeviceInfoList(di);
    if (h == INVALID_HANDLE_VALUE) { printf("Open fail %lu\n", GetLastError()); return 1; }
    
    // Create OVERLAPPED event (MANUAL reset like official #10)
    g_ovEvt = CreateEventW(NULL, TRUE, FALSE, NULL);
    
    DWORD br; BYTE tmp[1040];
    
    // Init sequence (OVERLAPPED, same order as official)
    io(h, 0x80882004, NULL, 0, tmp, 24);
    io(h, 0x80882820, NULL, 0, tmp, 4);
    io(h, 0x808820C4, NULL, 0, tmp, 1040);
    DWORD mode = 0; io(h, 0x80882804, &mode, 4);
    
    // SET_CALLBACKS: use new/malloc like official (HeapAlloc)
    size_t ctrlSize = 0x10000; // 64KB
    BYTE* ctrl = (BYTE*)malloc(ctrlSize);
    memset(ctrl, 0, ctrlSize);
    printf("ctrl=%p (malloc)\n", ctrl);
    
    BYTE cb[32] = {};
    *(UINT64*)&cb[0]  = (UINT64)(uintptr_t)ctrl;
    *(UINT64*)&cb[8]  = (UINT64)(uintptr_t)evt_auto1;
    *(UINT64*)&cb[16] = (UINT64)(uintptr_t)evt_auto2;
    *(UINT64*)&cb[24] = (UINT64)(uintptr_t)evt_auto3;
    bool ok = io(h, 0x80882880, cb, 32);
    printf("SET_CALLBACKS: %s (err=%lu)\n", ok?"OK":"FAIL", ok?0:GetLastError());
    
    BYTE config[292]; io(h, 0x80882808, NULL, 0, config, 292);
    printf("Rate: %u\n", *(DWORD*)config);
    
    BYTE chList[8200] = {};
    io(h, 0x8088280C, NULL, 0, chList, 8200, &br);
    DWORD numIn = *(DWORD*)&chList[0];
    DWORD numOut = *(DWORD*)&chList[4100];
    printf("Ch: %u in + %u out\n", numIn, numOut);
    
    // Get channel info for all (like official)
    for (DWORD i = 0; i < numIn; i++) {
        BYTE info[108]; io(h, 0x80882810, &chList[4+i*16], 16, info, 108);
    }
    for (DWORD i = 0; i < numOut; i++) {
        BYTE info[108]; io(h, 0x80882810, &chList[4104+i*16], 16, info, 108);
    }
    
    // Get stream config again (like official does before createBuffers)
    io(h, 0x80882808, NULL, 0, config, 292);
    
    // SET_BUFFER_SIZE
    DWORD bufSz = 128;
    io(h, 0x80882824, &bufSz, 4);
    
    // Allocate DMA buffers with malloc (like official)
    int totalCh = 2; // Just 1 in + 1 out for test
    BYTE** dmaBufs = new BYTE*[totalCh];
    for (int i = 0; i < totalCh; i++) {
        dmaBufs[i] = (BYTE*)malloc(0x10000);
        memset(dmaBufs[i], 0, 0x10000);
    }
    
    // SELECT + MAP
    // Input channel 0
    io(h, 0x80882840, &chList[4+0*16], 16); // SELECT In 0
    BYTE mapBuf[24] = {};
    *(DWORD*)&mapBuf[0] = *(DWORD*)&chList[4+0*16+8];     // chId
    *(DWORD*)&mapBuf[4] = *(DWORD*)&chList[4+0*16+12];     // type
    *(DWORD*)&mapBuf[8] = bufSz * 4;                        // bufferSize
    *(DWORD*)&mapBuf[12] = 32;                               // bits
    *(UINT64*)&mapBuf[16] = (UINT64)(uintptr_t)dmaBufs[0]; // addr
    ok = io(h, 0x808828A0, mapBuf, 24);
    printf("MAP In0 -> %p: %s\n", dmaBufs[0], ok?"OK":"FAIL");
    
    // Output channel 0
    io(h, 0x80882840, &chList[4104+0*16], 16); // SELECT Out 0
    *(DWORD*)&mapBuf[0] = *(DWORD*)&chList[4104+0*16+8];
    *(DWORD*)&mapBuf[4] = *(DWORD*)&chList[4104+0*16+12];
    *(DWORD*)&mapBuf[8] = bufSz * 4;
    *(DWORD*)&mapBuf[12] = 32;
    *(UINT64*)&mapBuf[16] = (UINT64)(uintptr_t)dmaBufs[1];
    ok = io(h, 0x808828A0, mapBuf, 24);
    printf("MAP Out0 -> %p: %s\n", dmaBufs[1], ok?"OK":"FAIL");
    
    // START
    ok = io(h, 0x808828C8, NULL, 0);
    printf("START: %s\n\n", ok?"OK":"FAIL");
    
    if (ok) {
        printf("Waiting for events (3s)...\n");
        HANDLE evts[] = {evt_auto1, evt_auto2, evt_auto3};
        int counts[3] = {};
        DWORD t0 = GetTickCount();
        
        while (GetTickCount() - t0 < 3000) {
            DWORD wait = WaitForMultipleObjects(3, evts, FALSE, 50);
            if (wait >= WAIT_OBJECT_0 && wait < WAIT_OBJECT_0+3) {
                int ei = wait - WAIT_OBJECT_0;
                counts[ei]++;
                int total = counts[0]+counts[1]+counts[2];
                if (total <= 5) {
                    printf("  [%4ums] Evt%d! total=%d\n", GetTickCount()-t0, ei, total);
                    // Check DMA
                    float* in0 = (float*)dmaBufs[0];
                    printf("    In0: %.6f %.6f %.6f %.6f\n", in0[0],in0[1],in0[2],in0[3]);
                }
            }
        }
        
        printf("\nEvents: auto1=%d auto2=%d auto3=%d\n", counts[0], counts[1], counts[2]);
        int total = counts[0]+counts[1]+counts[2];
        printf("Total: %d (%.1f Hz)\n", total, total/3.0);
        
        // STOP
        io(h, 0x808828CC, NULL, 0);
    }
    
    for (int i = 0; i < totalCh; i++) free(dmaBufs[i]);
    delete[] dmaBufs;
    free(ctrl);
    CloseHandle(h);
    return 0;
}
