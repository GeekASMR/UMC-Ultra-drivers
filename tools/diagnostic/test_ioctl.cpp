#include <windows.h>
#include <setupapi.h>
#include <stdio.h>

#pragma comment(lib, "setupapi.lib")

// TUSBAUDIO device interface GUID
static const GUID GUID_TUSBAUDIO = 
    {0x215A80EF, 0x69BD, 0x4D85, {0xAC, 0x71, 0x0C, 0x6E, 0xA6, 0xE6, 0xBE, 0x17}};

int main() {
    printf("=== TUSBAUDIO Device Interface Test ===\n\n");
    
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_TUSBAUDIO, NULL, NULL,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) {
        printf("SetupDiGetClassDevs failed: %lu\n", GetLastError());
        return 1;
    }
    
    SP_DEVICE_INTERFACE_DATA ifData = {};
    ifData.cbSize = sizeof(ifData);
    
    for (DWORD idx = 0; SetupDiEnumDeviceInterfaces(devInfo, NULL, &GUID_TUSBAUDIO, idx, &ifData); idx++) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, NULL, 0, &reqSize, NULL);
        
        auto* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)malloc(reqSize);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        
        if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, reqSize, NULL, NULL)) {
            wprintf(L"Device[%d]: %s\n", idx, detail->DevicePath);
            
            // Try opening the device
            HANDLE hDev = CreateFileW(detail->DevicePath,
                                       GENERIC_READ | GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       NULL, OPEN_EXISTING,
                                       FILE_FLAG_OVERLAPPED, NULL);
            
            if (hDev != INVALID_HANDLE_VALUE) {
                printf("  -> Opened successfully! Handle=%p\n", hDev);
                
                // Try the 4 ASIO-specific IOCTLs
                DWORD ioctls[] = {0x00220073, 0x00220900, 0x00220C00, 0x0022B900};
                const char* names[] = {"IOCTL_0073(Func28)", "IOCTL_0900(Func576)", 
                                        "IOCTL_0C00(Func768)", "IOCTL_B900(Func3648)"};
                
                for (int i = 0; i < 4; i++) {
                    BYTE outBuf[4096] = {};
                    DWORD returned = 0;
                    BOOL ok = DeviceIoControl(hDev, ioctls[i], NULL, 0, 
                                               outBuf, sizeof(outBuf), &returned, NULL);
                    printf("  %s: %s (returned %lu bytes, err=%lu)\n",
                           names[i], ok ? "OK" : "FAIL", returned, ok ? 0 : GetLastError());
                    
                    if (ok && returned > 0 && returned <= 64) {
                        printf("    Data: ");
                        for (DWORD j = 0; j < returned && j < 32; j++)
                            printf("%02X ", outBuf[j]);
                        printf("\n");
                    }
                }
                
                CloseHandle(hDev);
            } else {
                printf("  -> Open failed: %lu\n", GetLastError());
            }
        }
        free(detail);
    }
    
    SetupDiDestroyDeviceInfoList(devInfo);
    printf("\nDone.\n");
    return 0;
}
