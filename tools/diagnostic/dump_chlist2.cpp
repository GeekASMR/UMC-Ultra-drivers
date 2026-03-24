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
    BYTE tmp[1040]; 
    DeviceIoControl(h, 0x80882004, NULL, 0, tmp, 24, &br, NULL);
    DeviceIoControl(h, 0x80882820, NULL, 0, tmp, 4, &br, NULL);
    DeviceIoControl(h, 0x808820C4, NULL, 0, tmp, 1040, &br, NULL);
    DWORD mode = 0; DeviceIoControl(h, 0x80882804, &mode, 4, NULL, 0, &br, NULL);
    DeviceIoControl(h, 0x80882808, NULL, 0, tmp, 292, &br, NULL);
    
    BYTE chList[8200] = {};
    DeviceIoControl(h, 0x8088280C, NULL, 0, chList, 8200, &br, NULL);
    DWORD numCh1 = *(DWORD*)&chList[0];
    printf("First numCh=%u at offset 0\n", numCh1);
    
    // After 18 channels (offset 4 + 18*16 = 292), check for second block
    DWORD off2 = 4 + numCh1 * 16;
    DWORD numCh2 = *(DWORD*)&chList[off2];
    printf("Second numCh=%u at offset %u\n", numCh2, off2);
    
    if (numCh2 > 0 && numCh2 < 100) {
        for (DWORD i = 0; i < numCh2 && i < 30; i++) {
            DWORD off = off2 + 4 + i * 16;
            DWORD dir = *(DWORD*)&chList[off];
            DWORD stream = *(DWORD*)&chList[off+4];
            DWORD chId = *(DWORD*)&chList[off+8];
            DWORD type = *(DWORD*)&chList[off+12];
            
            BYTE info[108] = {};
            DeviceIoControl(h, 0x80882810, &chList[off], 16, info, 108, &br, NULL);
            wchar_t wn[32]={}; memcpy(wn, &info[24], 64);
            char nm[64]={}; WideCharToMultiByte(CP_UTF8, 0, wn, -1, nm, 64, NULL, NULL);
            
            DWORD role = *(DWORD*)&info[16];
            printf("  [%2u] dir=%u role=%u ChId=0x%02X Stream=0x%04X '%s'\n",
                   i, dir, role, chId, stream, nm);
        }
    }
    
    CloseHandle(h);
    return 0;
}
