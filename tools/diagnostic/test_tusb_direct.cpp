#include <windows.h>
#include <setupapi.h>
#include <stdio.h>

#pragma comment(lib, "setupapi.lib")

#define TUSB_GET_DEVICE_INFO    0x80882004
#define TUSB_GET_STATUS         0x80882820
#define TUSB_GET_DEVICE_PROPS   0x808820C4
#define TUSB_SET_MODE           0x80882804
#define TUSB_SET_CALLBACKS      0x80882880
#define TUSB_GET_STREAM_CONFIG  0x80882808
#define TUSB_GET_CHANNEL_LIST   0x8088280C
#define TUSB_GET_CHANNEL_INFO   0x80882810
#define TUSB_SET_BUFFER_SIZE    0x80882824
#define TUSB_SELECT_CHANNEL     0x80882840
#define TUSB_MAP_CHANNEL_BUF    0x808828A0
#define TUSB_START_STREAMING    0x808828C8
#define TUSB_STOP_STREAMING     0x808828CC

static const GUID GUID_TUSB = {0x215A80EF, 0x69BD, 0x4D85, {0xAC,0x71,0x0C,0x6E,0xA6,0xE6,0xBE,0x17}};

bool ioctl(HANDLE h, DWORD code, void* in, DWORD inSz, void* out, DWORD outSz, DWORD* ret) {
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, code, in, inSz, out, outSz, &br, NULL);
    if (ret) *ret = br;
    return ok != FALSE;
}

int main() {
    printf("=== TusbAudioDirect Protocol Test ===\n\n");
    
    // Find and open device
    HDEVINFO di = SetupDiGetClassDevsW(&GUID_TUSB, NULL, NULL, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA id = {}; id.cbSize = sizeof(id);
    SetupDiEnumDeviceInterfaces(di, NULL, &GUID_TUSB, 0, &id);
    DWORD rs; SetupDiGetDeviceInterfaceDetailW(di, &id, NULL, 0, &rs, NULL);
    auto* dt = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)calloc(1, rs);
    dt->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    SetupDiGetDeviceInterfaceDetailW(di, &id, dt, rs, NULL, NULL);
    
    HANDLE hDev = CreateFileW(dt->DevicePath, GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    free(dt); SetupDiDestroyDeviceInfoList(di);
    if (hDev == INVALID_HANDLE_VALUE) { printf("Open fail\n"); return 1; }
    printf("Device opened\n");
    
    DWORD br;
    
    // === Follow exact IOCTL sequence from spy ===
    
    // 1. GET_DEVICE_INFO
    BYTE info[24]; ioctl(hDev, TUSB_GET_DEVICE_INFO, NULL, 0, info, 24, &br);
    printf("[1] GET_DEVICE_INFO: %u bytes\n", br);
    
    // 2. GET_STATUS
    DWORD status; ioctl(hDev, TUSB_GET_STATUS, NULL, 0, &status, 4, &br);
    printf("[2] GET_STATUS: 0x%08X\n", status);
    
    // 3. GET_DEVICE_PROPS
    BYTE props[1040]; ioctl(hDev, TUSB_GET_DEVICE_PROPS, NULL, 0, props, 1040, &br);
    printf("[3] GET_DEVICE_PROPS: VID=0x%04X PID=0x%04X (%u bytes)\n",
           *(WORD*)&props[0], *(WORD*)&props[4], br);
    // Serial at offset 12 as UTF-16
    wchar_t serial[32] = {}; memcpy(serial, &props[12], 32);
    printf("    Serial: %ls\n", serial);
    
    // 4. SET_MODE (0 = ASIO mode)
    DWORD mode = 0;
    bool ok = ioctl(hDev, TUSB_SET_MODE, &mode, 4, NULL, 0, &br);
    printf("[4] SET_MODE(0): %s\n", ok ? "OK" : "FAIL");
    
    // 5. SET_CALLBACKS (32 bytes - for now send zeros, we'll refine later)
    BYTE cbBuf[32] = {};
    ok = ioctl(hDev, TUSB_SET_CALLBACKS, cbBuf, 32, NULL, 0, &br);
    printf("[5] SET_CALLBACKS: %s\n", ok ? "OK" : "FAIL");
    
    // 6. GET_STREAM_CONFIG
    BYTE config[292] = {};
    ioctl(hDev, TUSB_GET_STREAM_CONFIG, NULL, 0, config, 292, &br);
    DWORD rate = *(DWORD*)&config[0], numRates = *(DWORD*)&config[4];
    printf("[6] GET_STREAM_CONFIG: rate=%u, numRates=%u\n", rate, numRates);
    for (DWORD i = 0; i < numRates && i < 8; i++)
        printf("    Rate[%u]: %u\n", i, *(DWORD*)&config[8 + i*4]);
    
    // 7. GET_CHANNEL_LIST
    BYTE chList[8200] = {};
    ioctl(hDev, TUSB_GET_CHANNEL_LIST, NULL, 0, chList, 8200, &br);
    DWORD numCh = *(DWORD*)&chList[0];
    printf("[7] GET_CHANNEL_LIST: %u channels (%u bytes)\n", numCh, br);
    
    // Parse channels (offset 4, 16 bytes each)
    DWORD offset = 4;
    int inCount = 0, outCount = 0;
    for (DWORD i = 0; i < numCh && offset + 16 <= br; i++) {
        DWORD dir = *(DWORD*)&chList[offset];
        DWORD streamId = *(DWORD*)&chList[offset+4];
        DWORD chId = *(DWORD*)&chList[offset+8];
        DWORD type = *(DWORD*)&chList[offset+12];
        
        BYTE infoBuf[108] = {};
        ioctl(hDev, TUSB_GET_CHANNEL_INFO, &chList[offset], 16, infoBuf, 108, &br);
        wchar_t wn[32] = {}; memcpy(wn, &infoBuf[24], 64);
        char name[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, wn, -1, name, 64, NULL, NULL);
        
        if (dir == 1) inCount++; else outCount++;
        printf("    [%2u] %s ChId=0x%02X Stream=0x%04X '%s'\n",
               i, dir==1?"IN ":"OUT", chId, streamId, name);
        offset += 16;
    }
    printf("  Total: %d in + %d out\n", inCount, outCount);
    
    CloseHandle(hDev);
    printf("\nDone!\n");
    return 0;
}
