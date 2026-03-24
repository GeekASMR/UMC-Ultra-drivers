/*
 * test_asio_mode.cpp - Test TUSBAUDIO ASIO streaming mode
 * Check GetDeviceStreamingMode, SetDeviceStreamingMode, GetASIORelation
 */

#include <windows.h>
#include <stdio.h>
#include <string>

// Minimal TUSBAUDIO types
typedef unsigned int TUsbAudioStatus;
typedef void* TUsbAudioHandle;
#define TSTATUS_SUCCESS 0

typedef TUsbAudioStatus (__cdecl *PFN_EnumerateDevices)();
typedef unsigned int (__cdecl *PFN_GetDeviceCount)();
typedef TUsbAudioStatus (__cdecl *PFN_OpenDeviceByIndex)(unsigned int, TUsbAudioHandle*);
typedef TUsbAudioStatus (__cdecl *PFN_CloseDevice)(TUsbAudioHandle);
typedef TUsbAudioStatus (__cdecl *PFN_GetDeviceStreamingMode)(TUsbAudioHandle, unsigned int*);
typedef TUsbAudioStatus (__cdecl *PFN_SetDeviceStreamingMode)(TUsbAudioHandle, unsigned int);
typedef TUsbAudioStatus (__cdecl *PFN_GetASIORelation)(TUsbAudioHandle, unsigned int*);
typedef TUsbAudioStatus (__cdecl *PFN_GetASIOInstanceCount)(unsigned int*);
typedef TUsbAudioStatus (__cdecl *PFN_GetASIOInstanceAllowed)(TUsbAudioHandle, unsigned int*);
typedef TUsbAudioStatus (__cdecl *PFN_GetASIOInstanceInfo)(unsigned int, void*, unsigned int);
typedef TUsbAudioStatus (__cdecl *PFN_GetASIOInstanceDetails)(unsigned int, void*, unsigned int);
typedef const char* (__cdecl *PFN_StatusCodeStringA)(TUsbAudioStatus);

int main() {
    printf("=== TUSBAUDIO ASIO Mode Test ===\n");

    HMODULE hDll = LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioapi_x64.dll");
    if (!hDll) {
        printf("Failed to load DLL\n");
        return 1;
    }

    auto pfnEnumerate = (PFN_EnumerateDevices)GetProcAddress(hDll, "TUSBAUDIO_EnumerateDevices");
    auto pfnGetCount = (PFN_GetDeviceCount)GetProcAddress(hDll, "TUSBAUDIO_GetDeviceCount");
    auto pfnOpen = (PFN_OpenDeviceByIndex)GetProcAddress(hDll, "TUSBAUDIO_OpenDeviceByIndex");
    auto pfnClose = (PFN_CloseDevice)GetProcAddress(hDll, "TUSBAUDIO_CloseDevice");
    auto pfnGetMode = (PFN_GetDeviceStreamingMode)GetProcAddress(hDll, "TUSBAUDIO_GetDeviceStreamingMode");
    auto pfnSetMode = (PFN_SetDeviceStreamingMode)GetProcAddress(hDll, "TUSBAUDIO_SetDeviceStreamingMode");
    auto pfnGetRelation = (PFN_GetASIORelation)GetProcAddress(hDll, "TUSBAUDIO_GetASIORelation");
    auto pfnGetInstCount = (PFN_GetASIOInstanceCount)GetProcAddress(hDll, "TUSBAUDIO_GetASIOInstanceCount");
    auto pfnGetInstAllowed = (PFN_GetASIOInstanceAllowed)GetProcAddress(hDll, "TUSBAUDIO_GetASIOInstanceAllowed");
    auto pfnGetInstInfo = (PFN_GetASIOInstanceInfo)GetProcAddress(hDll, "TUSBAUDIO_GetASIOInstanceInfo");
    auto pfnGetInstDetails = (PFN_GetASIOInstanceDetails)GetProcAddress(hDll, "TUSBAUDIO_GetASIOInstanceDetails");
    auto pfnStatusStr = (PFN_StatusCodeStringA)GetProcAddress(hDll, "TUSBAUDIO_StatusCodeStringA");

    printf("GetDeviceStreamingMode: %p\n", pfnGetMode);
    printf("SetDeviceStreamingMode: %p\n", pfnSetMode);
    printf("GetASIORelation: %p\n", pfnGetRelation);
    printf("GetASIOInstanceCount: %p\n", pfnGetInstCount);
    printf("GetASIOInstanceAllowed: %p\n", pfnGetInstAllowed);
    printf("GetASIOInstanceInfo: %p\n", pfnGetInstInfo);
    printf("GetASIOInstanceDetails: %p\n", pfnGetInstDetails);

    TUsbAudioStatus st = pfnEnumerate();
    printf("\nEnumerate: 0x%08X\n", st);
    if (st != TSTATUS_SUCCESS) { FreeLibrary(hDll); return 1; }

    unsigned int count = pfnGetCount();
    printf("Device count: %d\n", count);

    // ASIO instance count
    if (pfnGetInstCount) {
        unsigned int instCount = 0;
        st = pfnGetInstCount(&instCount);
        printf("\nASIO instance count: %d (status=0x%08X)\n", instCount, st);
        
        // Try to get instance info
        if (instCount > 0 && pfnGetInstInfo) {
            unsigned char info[1024] = {};
            st = pfnGetInstInfo(0, info, sizeof(info));
            printf("ASIO instance info[0]: status=0x%08X\n", st);
            if (st == TSTATUS_SUCCESS) {
                // Dump first 128 bytes
                printf("  Data: ");
                for (int i = 0; i < 128; i++) printf("%02X ", info[i]);
                printf("\n");
            }
        }
        if (instCount > 0 && pfnGetInstDetails) {
            unsigned char details[1024] = {};
            st = pfnGetInstDetails(0, details, sizeof(details));
            printf("ASIO instance details[0]: status=0x%08X\n", st);
            if (st == TSTATUS_SUCCESS) {
                printf("  Data: ");
                for (int i = 0; i < 128; i++) printf("%02X ", details[i]);
                printf("\n");
            }
        }
    }

    for (unsigned int i = 0; i < count; i++) {
        TUsbAudioHandle handle = nullptr;
        st = pfnOpen(i, &handle);
        if (st != TSTATUS_SUCCESS || !handle) continue;

        printf("\n--- Device[%d] ---\n", i);

        // Get current streaming mode
        if (pfnGetMode) {
            unsigned int mode = 9999;
            st = pfnGetMode(handle, &mode);
            printf("  StreamingMode: %d (status=0x%08X %s)\n", mode, st,
                   pfnStatusStr ? pfnStatusStr(st) : "");
        }

        // Get ASIO relation
        if (pfnGetRelation) {
            unsigned int relation = 9999;
            st = pfnGetRelation(handle, &relation);
            printf("  ASIORelation: %d (status=0x%08X %s)\n", relation, st,
                   pfnStatusStr ? pfnStatusStr(st) : "");
        }

        // Check ASIO instance allowed
        if (pfnGetInstAllowed) {
            unsigned int allowed = 0;
            st = pfnGetInstAllowed(handle, &allowed);
            printf("  ASIOInstanceAllowed: %d (status=0x%08X)\n", allowed, st);
        }

        // Try setting ASIO mode (mode=1 might be ASIO)
        if (pfnSetMode) {
            printf("\n  Trying mode 0 (WDM?)...\n");
            st = pfnSetMode(handle, 0);
            printf("    SetMode(0): 0x%08X %s\n", st, pfnStatusStr ? pfnStatusStr(st) : "");

            printf("  Trying mode 1 (ASIO?)...\n");
            st = pfnSetMode(handle, 1);
            printf("    SetMode(1): 0x%08X %s\n", st, pfnStatusStr ? pfnStatusStr(st) : "");

            printf("  Trying mode 2...\n");
            st = pfnSetMode(handle, 2);
            printf("    SetMode(2): 0x%08X %s\n", st, pfnStatusStr ? pfnStatusStr(st) : "");

            // Read back
            if (pfnGetMode) {
                unsigned int mode = 9999;
                pfnGetMode(handle, &mode);
                printf("  Current mode after tests: %d\n", mode);
            }
        }

        pfnClose(handle);
    }

    FreeLibrary(hDll);
    printf("\n=== Done ===\n");
    return 0;
}
