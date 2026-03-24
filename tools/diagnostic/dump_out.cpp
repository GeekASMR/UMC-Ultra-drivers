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
    DWORD br; BYTE tmp[1040]; 
    DeviceIoControl(h, 0x80882004, NULL, 0, tmp, 24, &br, NULL);
    DeviceIoControl(h, 0x80882820, NULL, 0, tmp, 4, &br, NULL);
    DeviceIoControl(h, 0x808820C4, NULL, 0, tmp, 1040, &br, NULL);
    DWORD mode = 0; DeviceIoControl(h, 0x80882804, &mode, 4, NULL, 0, &br, NULL);
    DeviceIoControl(h, 0x80882808, NULL, 0, tmp, 292, &br, NULL);
    
    BYTE chList[8200] = {};
    DeviceIoControl(h, 0x8088280C, NULL, 0, chList, 8200, &br, NULL);
    
    // 8200 = 4100 * 2. Check if split at midpoint
    // 4100 = 4 + 256*16. So each half holds up to 256 channels.
    printf("=== Checking fixed offsets for output channel block ===\n");
    DWORD offsets[] = {292, 4100, 4096, 4104, 2052, 2048};
    for (int i = 0; i < 6; i++) {
        DWORD off = offsets[i];
        if (off + 4 > 8200) continue;
        DWORD val = *(DWORD*)&chList[off];
        printf("  offset %4u: DWORD=%u (0x%08X)\n", off, val, val);
        if (val > 0 && val <= 100) {
            // Looks like a channel count! Check first entry
            DWORD dir = *(DWORD*)&chList[off+4];
            DWORD stream = *(DWORD*)&chList[off+8];
            DWORD chId = *(DWORD*)&chList[off+12];
            printf("    first entry: dir=%u stream=0x%04X chId=0x%02X\n", dir, stream, chId);
        }
    }
    
    // Scan for any non-zero DWORD that looks like a output channel count (10-30)
    printf("\n=== Scanning for output block (non-zero DWORD 10-30) ===\n");
    for (DWORD off = 292; off < 8200 - 20; off += 4) {
        DWORD val = *(DWORD*)&chList[off];
        if (val >= 10 && val <= 30) {
            // Check if followed by plausible channel data
            DWORD dir = *(DWORD*)&chList[off+4];
            DWORD stream = *(DWORD*)&chList[off+8];
            DWORD chId = *(DWORD*)&chList[off+12];
            if ((dir == 1 || dir == 2) && stream > 0 && stream < 0x1000 && chId >= 0x40 && chId <= 0xFF) {
                printf("  FOUND at offset %u: numCh=%u\n", off, val);
                printf("    first: dir=%u stream=0x%04X chId=0x%02X\n", dir, stream, chId);
                
                // Query channel info
                BYTE info[108] = {};
                DeviceIoControl(h, 0x80882810, &chList[off+4], 16, info, 108, &br, NULL);
                DWORD role = *(DWORD*)&info[16];
                wchar_t wn[32]={}; memcpy(wn, &info[24], 64);
                char nm[64]={}; WideCharToMultiByte(CP_UTF8, 0, wn, -1, nm, 64, NULL, NULL);
                printf("    name='%s' role=%u\n", nm, role);
                
                // List all channels
                for (DWORD i = 0; i < val && i < 25; i++) {
                    DWORD o2 = off + 4 + i * 16;
                    DWORD d2 = *(DWORD*)&chList[o2];
                    DWORD s2 = *(DWORD*)&chList[o2+4];
                    DWORD c2 = *(DWORD*)&chList[o2+8];
                    BYTE info2[108] = {};
                    DeviceIoControl(h, 0x80882810, &chList[o2], 16, info2, 108, &br, NULL);
                    wchar_t wn2[32]={}; memcpy(wn2, &info2[24], 64);
                    char nm2[64]={}; WideCharToMultiByte(CP_UTF8, 0, wn2, -1, nm2, 64, NULL, NULL);
                    DWORD role2 = *(DWORD*)&info2[16];
                    printf("    [%2u] dir=%u role=%u ChId=0x%02X '%s'\n", i, d2, role2, c2, nm2);
                }
                break;
            }
        }
    }
    
    CloseHandle(h);
    return 0;
}
