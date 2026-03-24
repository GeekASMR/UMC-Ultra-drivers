// test_stream6 - 完整直连实现: OVERLAPPED + 事件 + 轮询线程
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
    if (!ok && GetLastError() == ERROR_IO_PENDING)
        ok = GetOverlappedResult(h, &g_ov, &br, TRUE);
    if (pRet) *pRet = br;
    return ok != FALSE;
}

static volatile LONG g_switchCount = 0;
static volatile bool g_running = false;

struct AudioThread {
    HANDLE events[3]; // auto-reset events from SET_CALLBACKS
    BYTE* ctrl;       // control page
};

DWORD WINAPI AudioPollThread(LPVOID param) {
    AudioThread* at = (AudioThread*)param;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    
    while (g_running) {
        // Wait for any of the 3 events (buffer switch signal from kernel)
        DWORD wait = WaitForMultipleObjects(3, at->events, FALSE, 100);
        if (wait >= WAIT_OBJECT_0 && wait < WAIT_OBJECT_0 + 3) {
            InterlockedIncrement(&g_switchCount);
        }
    }
    return 0;
}

int main() {
    printf("=== Stream Test v6 - Direct IOCTL + Poll Thread ===\n\n");
    
    // Create events (same pattern as official)
    HANDLE evt_auto[3], evt_man[6];
    for (int i = 0; i < 3; i++) evt_auto[i] = CreateEventW(NULL, FALSE, FALSE, NULL);
    for (int i = 0; i < 6; i++) evt_man[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
    printf("AUTO events: %p %p %p\n", evt_auto[0], evt_auto[1], evt_auto[2]);
    
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
    
    g_ovEvt = CreateEventW(NULL, TRUE, FALSE, NULL);
    
    DWORD br; BYTE tmp[1040];
    io(h, 0x80882004, NULL, 0, tmp, 24);
    io(h, 0x80882820, NULL, 0, tmp, 4);
    io(h, 0x808820C4, NULL, 0, tmp, 1040);
    DWORD mode = 0; io(h, 0x80882804, &mode, 4);
    
    // Control page (use malloc like official)
    BYTE* ctrl = (BYTE*)calloc(1, 0x10000);
    printf("ctrl=%p\n", ctrl);
    
    // SET_CALLBACKS
    BYTE cb[32] = {};
    *(UINT64*)&cb[0]  = (UINT64)(uintptr_t)ctrl;
    *(UINT64*)&cb[8]  = (UINT64)(uintptr_t)evt_auto[0];
    *(UINT64*)&cb[16] = (UINT64)(uintptr_t)evt_auto[1];
    *(UINT64*)&cb[24] = (UINT64)(uintptr_t)evt_auto[2];
    printf("SET_CALLBACKS: %s\n", io(h, 0x80882880, cb, 32)?"OK":"FAIL");
    
    BYTE config[292]; io(h, 0x80882808, NULL, 0, config, 292);
    printf("Rate: %u\n", *(DWORD*)config);
    
    BYTE chList[8200] = {};
    io(h, 0x8088280C, NULL, 0, chList, 8200, &br);
    DWORD numIn = *(DWORD*)&chList[0];
    DWORD numOut = *(DWORD*)&chList[4100];
    printf("Ch: %u in + %u out\n", numIn, numOut);
    
    // Channel info (like official)
    for (DWORD i = 0; i < numIn; i++) {
        BYTE info[108]; io(h, 0x80882810, &chList[4+i*16], 16, info, 108);
    }
    for (DWORD i = 0; i < numOut; i++) {
        BYTE info[108]; io(h, 0x80882810, &chList[4104+i*16], 16, info, 108);
    }
    io(h, 0x80882808, NULL, 0, config, 292); // second config query
    
    // Buffer size
    DWORD bufSz = 128;
    io(h, 0x80882824, &bufSz, 4);
    DWORD bufBytes = bufSz * 4;
    
    // SELECT + MAP (1 in + 1 out, like test)
    // Allocate with calloc (like official uses HeapAlloc)
    BYTE* dmaBuf[2];
    for (int i = 0; i < 2; i++) dmaBuf[i] = (BYTE*)calloc(1, 0x10000);
    
    // In 0
    io(h, 0x80882840, &chList[4], 16);
    BYTE mapBuf[24] = {};
    *(DWORD*)&mapBuf[0] = *(DWORD*)&chList[4+8];
    *(DWORD*)&mapBuf[4] = *(DWORD*)&chList[4+12];
    *(DWORD*)&mapBuf[8] = bufBytes;
    *(DWORD*)&mapBuf[12] = 32;
    *(UINT64*)&mapBuf[16] = (UINT64)(uintptr_t)dmaBuf[0];
    printf("MAP In0 -> %p: %s\n", dmaBuf[0], io(h, 0x808828A0, mapBuf, 24)?"OK":"FAIL");
    
    // Out 0
    io(h, 0x80882840, &chList[4104], 16);
    *(DWORD*)&mapBuf[0] = *(DWORD*)&chList[4104+8];
    *(DWORD*)&mapBuf[4] = *(DWORD*)&chList[4104+12];
    *(UINT64*)&mapBuf[16] = (UINT64)(uintptr_t)dmaBuf[1];
    printf("MAP Out0 -> %p: %s\n", dmaBuf[1], io(h, 0x808828A0, mapBuf, 24)?"OK":"FAIL");
    
    // Start poll thread BEFORE starting
    g_running = true;
    AudioThread at;
    at.events[0] = evt_auto[0]; at.events[1] = evt_auto[1]; at.events[2] = evt_auto[2];
    at.ctrl = ctrl;
    HANDLE hThread = CreateThread(NULL, 0, AudioPollThread, &at, 0, NULL);
    SetThreadPriority(hThread, THREAD_PRIORITY_TIME_CRITICAL);
    
    // START
    printf("START: %s\n", io(h, 0x808828C8, NULL, 0)?"OK":"FAIL");
    
    // Monitor for 2 seconds
    printf("Running for 2s...\n");
    Sleep(2000);
    
    printf("\n=== Results ===\n");
    printf("bufferSwitch events: %ld (%.1f Hz, expected 375)\n", g_switchCount, g_switchCount/2.0);
    
    // Check data
    float* in0 = (float*)dmaBuf[0];
    printf("In0 samples: %.6f %.6f %.6f %.6f\n", in0[0], in0[1], in0[2], in0[3]);
    printf("In0 buf[1]: %.6f %.6f\n", 
           *(float*)(dmaBuf[0]+bufBytes), *(float*)(dmaBuf[0]+bufBytes+4));
    
    // Ctrl page
    printf("ctrl[0..15]: ");
    for (int i = 0; i < 16; i++) printf("%02X ", ctrl[i]);
    printf("\n");
    
    // STOP
    g_running = false;
    WaitForSingleObject(hThread, 1000);
    io(h, 0x808828CC, NULL, 0);
    
    for (int i = 0; i < 2; i++) free(dmaBuf[i]);
    free(ctrl);
    CloseHandle(h);
    printf("Done!\n");
    return 0;
}
