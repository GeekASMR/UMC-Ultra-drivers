// test_stream7 - VirtualAlloc 页对齐地址 + 完整协议
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

DWORD WINAPI PollThread(LPVOID param) {
    HANDLE* events = (HANDLE*)param;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    while (g_running) {
        DWORD w = WaitForMultipleObjects(3, events, FALSE, 100);
        if (w >= WAIT_OBJECT_0 && w < WAIT_OBJECT_0+3) InterlockedIncrement(&g_switchCount);
    }
    return 0;
}

int main() {
    printf("=== Stream Test v7 - VirtualAlloc aligned ===\n\n");
    
    // Events: 3 AUTO + 6 MANUAL (same as official)
    HANDLE evt_auto[3], evt_man[6];
    for (int i = 0; i < 3; i++) evt_auto[i] = CreateEventW(NULL, FALSE, FALSE, NULL);
    for (int i = 0; i < 6; i++) evt_man[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
    
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
    
    // Use VirtualAlloc for PAGE-ALIGNED memory (key difference!)
    BYTE* ctrl = (BYTE*)VirtualAlloc(NULL, 0x10000, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    printf("ctrl=%p (VirtualAlloc, page-aligned)\n", ctrl);
    
    BYTE cb[32] = {};
    *(UINT64*)&cb[0]  = (UINT64)(uintptr_t)ctrl;
    *(UINT64*)&cb[8]  = (UINT64)(uintptr_t)evt_auto[0];
    *(UINT64*)&cb[16] = (UINT64)(uintptr_t)evt_auto[1];
    *(UINT64*)&cb[24] = (UINT64)(uintptr_t)evt_auto[2];
    bool ok = io(h, 0x80882880, cb, 32);
    printf("SET_CALLBACKS: %s (err=%lu)\n", ok?"OK":"FAIL", ok?0:GetLastError());
    
    BYTE config[292]; io(h, 0x80882808, NULL, 0, config, 292);
    BYTE chList[8200] = {};
    io(h, 0x8088280C, NULL, 0, chList, 8200, &br);
    DWORD numIn = *(DWORD*)&chList[0];
    DWORD numOut = *(DWORD*)&chList[4100];
    
    // Channel info like official
    for (DWORD i = 0; i < numIn; i++) { BYTE info[108]; io(h, 0x80882810, &chList[4+i*16], 16, info, 108); }
    for (DWORD i = 0; i < numOut; i++) { BYTE info[108]; io(h, 0x80882810, &chList[4104+i*16], 16, info, 108); }
    io(h, 0x80882808, NULL, 0, config, 292);
    
    DWORD bufSz = 128, bufBytes = bufSz * 4;
    io(h, 0x80882824, &bufSz, 4);
    
    // DMA buffers: VirtualAlloc for page alignment!
    BYTE* dmaBuf[2];
    for (int i = 0; i < 2; i++)
        dmaBuf[i] = (BYTE*)VirtualAlloc(NULL, 0x10000, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    printf("dma[0]=%p dma[1]=%p (page-aligned)\n", dmaBuf[0], dmaBuf[1]);
    
    // SELECT + MAP In0
    io(h, 0x80882840, &chList[4], 16);
    BYTE mapBuf[24] = {};
    *(DWORD*)&mapBuf[0] = *(DWORD*)&chList[4+8]; *(DWORD*)&mapBuf[4] = *(DWORD*)&chList[4+12];
    *(DWORD*)&mapBuf[8] = bufBytes; *(DWORD*)&mapBuf[12] = 32;
    *(UINT64*)&mapBuf[16] = (UINT64)(uintptr_t)dmaBuf[0];
    printf("MAP In0: %s\n", io(h, 0x808828A0, mapBuf, 24)?"OK":"FAIL");
    
    // SELECT + MAP Out0
    io(h, 0x80882840, &chList[4104], 16);
    *(DWORD*)&mapBuf[0] = *(DWORD*)&chList[4104+8]; *(DWORD*)&mapBuf[4] = *(DWORD*)&chList[4104+12];
    *(UINT64*)&mapBuf[16] = (UINT64)(uintptr_t)dmaBuf[1];
    printf("MAP Out0: %s\n", io(h, 0x808828A0, mapBuf, 24)?"OK":"FAIL");
    
    // Start thread BEFORE start
    g_running = true;
    HANDLE hThread = CreateThread(NULL, 0, PollThread, evt_auto, 0, NULL);
    
    ok = io(h, 0x808828C8, NULL, 0);
    printf("START: %s\n", ok?"OK":"FAIL");
    printf("Running 2s...\n");
    Sleep(2000);
    
    printf("\n=== RESULTS ===\n");
    printf("Events: %ld (%.1f Hz, expected 375)\n", g_switchCount, g_switchCount/2.0);
    
    BYTE* d = dmaBuf[0];
    printf("In0 hex: %02X %02X %02X %02X  %02X %02X %02X %02X\n",
           d[0],d[1],d[2],d[3], d[4],d[5],d[6],d[7]);
    printf("ctrl[0..7]: %02X %02X %02X %02X  %02X %02X %02X %02X\n",
           ctrl[0],ctrl[1],ctrl[2],ctrl[3], ctrl[4],ctrl[5],ctrl[6],ctrl[7]);
    
    g_running = false;
    WaitForSingleObject(hThread, 1000);
    io(h, 0x808828CC, NULL, 0);
    
    VirtualFree(dmaBuf[0], 0, MEM_RELEASE);
    VirtualFree(dmaBuf[1], 0, MEM_RELEASE);
    VirtualFree(ctrl, 0, MEM_RELEASE);
    CloseHandle(h);
    printf("Done!\n");
    return 0;
}
