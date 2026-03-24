#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#pragma comment(lib, "setupapi.lib")

static const GUID G = {0x215A80EF,0x69BD,0x4D85,{0xAC,0x71,0x0C,0x6E,0xA6,0xE6,0xBE,0x17}};

int main() {
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

    DWORD br;
    // Must follow init sequence
    BYTE info[24]; DeviceIoControl(h, 0x80882004, NULL, 0, info, 24, &br, NULL);
    DWORD status; DeviceIoControl(h, 0x80882820, NULL, 0, &status, 4, &br, NULL);
    BYTE props[1040]; DeviceIoControl(h, 0x808820C4, NULL, 0, props, 1040, &br, NULL);
    DWORD mode = 0; DeviceIoControl(h, 0x80882804, &mode, 4, NULL, 0, &br, NULL);
    
    // GET_STREAM_CONFIG
    BYTE config[292]; DeviceIoControl(h, 0x80882808, NULL, 0, config, 292, &br, NULL);
    
    // GET_CHANNEL_LIST - full dump
    BYTE chList[8200] = {};
    BOOL ok = DeviceIoControl(h, 0x8088280C, NULL, 0, chList, 8200, &br, NULL);
    printf("GET_CHANNEL_LIST: ok=%d, br=%u\n", ok, br);
    
    DWORD numCh = *(DWORD*)&chList[0];
    printf("numCh = %u\n\n", numCh);
    
    // Each channel: 16 bytes starting at offset 4
    // But wait - maybe it's 20 bytes? Let me check by looking for pattern
    // Try both 16 and 20 byte offsets
    
    // First dump raw DWORDs
    printf("Raw DWORDs (first 200):\n");
    for (DWORD i = 0; i < 50 && i*4 < br; i++) {
        DWORD v = *(DWORD*)&chList[i*4];
        printf("[%2u] off=%3u: %10u (0x%08X)\n", i, i*4, v, v);
    }
    
    CloseHandle(h);
    return 0;
}
