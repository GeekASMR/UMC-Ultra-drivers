// restore_mode.cpp - Set device back to WDM mode (1)
#include <windows.h>
#include <stdio.h>
typedef unsigned int TUsbAudioStatus;
typedef void* TUsbAudioHandle;
typedef TUsbAudioStatus (__cdecl *PFN_Enum)();
typedef unsigned int (__cdecl *PFN_Count)();
typedef TUsbAudioStatus (__cdecl *PFN_Open)(unsigned int, TUsbAudioHandle*);
typedef TUsbAudioStatus (__cdecl *PFN_Close)(TUsbAudioHandle);
typedef TUsbAudioStatus (__cdecl *PFN_SetMode)(TUsbAudioHandle, unsigned int);
typedef TUsbAudioStatus (__cdecl *PFN_GetMode)(TUsbAudioHandle, unsigned int*);
int main() {
    HMODULE h = LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioapi_x64.dll");
    if (!h) { printf("Cannot load DLL\n"); return 1; }
    auto e = (PFN_Enum)GetProcAddress(h, "TUSBAUDIO_EnumerateDevices");
    auto c = (PFN_Count)GetProcAddress(h, "TUSBAUDIO_GetDeviceCount");
    auto o = (PFN_Open)GetProcAddress(h, "TUSBAUDIO_OpenDeviceByIndex");
    auto cl = (PFN_Close)GetProcAddress(h, "TUSBAUDIO_CloseDevice");
    auto sm = (PFN_SetMode)GetProcAddress(h, "TUSBAUDIO_SetDeviceStreamingMode");
    auto gm = (PFN_GetMode)GetProcAddress(h, "TUSBAUDIO_GetDeviceStreamingMode");
    if (e() != 0) { printf("Enumerate failed\n"); return 1; }
    for (unsigned int i = 0; i < c(); i++) {
        TUsbAudioHandle dev; if (o(i, &dev) != 0) continue;
        unsigned int mode = 0; gm(dev, &mode);
        printf("Device %d: mode=%d", i, mode);
        if (mode != 1) { sm(dev, 1); gm(dev, &mode); printf(" -> %d", mode); }
        printf("\n"); cl(dev);
    }
    FreeLibrary(h); printf("Done - WDM mode restored\n"); return 0;
}
