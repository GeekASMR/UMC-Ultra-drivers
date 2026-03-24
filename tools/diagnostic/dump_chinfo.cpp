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
    
    // GET_CHANNEL_LIST
    BYTE chList[8200] = {};
    DeviceIoControl(h, 0x8088280C, NULL, 0, chList, 8200, &br, NULL);
    DWORD numCh = *(DWORD*)&chList[0];
    printf("Channels: %u (from channel list)\n\n", numCh);
    
    // Parse each channel and query info
    int inCount=0, outCount=0;
    for (DWORD i = 0; i < numCh; i++) {
        DWORD off = 4 + i * 16;
        DWORD dir = *(DWORD*)&chList[off];
        DWORD streamId = *(DWORD*)&chList[off+4];
        DWORD chId = *(DWORD*)&chList[off+8];
        DWORD type = *(DWORD*)&chList[off+12];
        
        // Query CHANNEL_INFO
        BYTE out[108] = {};
        DeviceIoControl(h, 0x80882810, &chList[off], 16, out, 108, &br, NULL);
        
        // Parse output: first 16 = echo, then DWORD[4]=?, DWORD[5]=?, then name
        DWORD outVal4 = *(DWORD*)&out[16];  // direction? type? role?
        DWORD outVal5 = *(DWORD*)&out[20];  // sub-type?
        
        wchar_t wn[32] = {}; memcpy(wn, &out[24], 64);
        char name[64] = {};
        WideCharToMultiByte(CP_UTF8, 0, wn, -1, name, 64, NULL, NULL);
        
        const char* dirStr;
        if (outVal4 == 1) { dirStr = "IN "; inCount++; }
        else if (outVal4 == 2) { dirStr = "OUT"; outCount++; }
        else { dirStr = "???"; }
        
        printf("[%2u] dir=%u val4=%u val5=%u  ChId=0x%02X  Stream=0x%04X  '%s'  [%s]\n",
               i, dir, outVal4, outVal5, chId, streamId, name, dirStr);
    }
    
    printf("\nTotal: %d inputs + %d outputs = %d\n", inCount, outCount, inCount+outCount);
    
    CloseHandle(h);
    return 0;
}
