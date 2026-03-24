#include <windows.h>
#include <setupapi.h>
#include <stdio.h>

#pragma comment(lib, "setupapi.lib")

typedef unsigned int TUsbAudioStatus;
typedef void* TUsbAudioHandle;

// TUSBAUDIO API function pointers
typedef TUsbAudioStatus (*fn_EnumerateDevices)(void);
typedef TUsbAudioStatus (*fn_GetDeviceCount)(unsigned int*);
typedef TUsbAudioStatus (*fn_OpenDeviceByIndex)(unsigned int, TUsbAudioHandle*);
typedef TUsbAudioStatus (*fn_CloseDevice)(TUsbAudioHandle);
typedef TUsbAudioStatus (*fn_GetCurrentSampleRate)(TUsbAudioHandle, unsigned int*);
typedef TUsbAudioStatus (*fn_GetStreamChannelCount)(TUsbAudioHandle, unsigned int, unsigned int*);
typedef TUsbAudioStatus (*fn_SetDeviceStreamingMode)(TUsbAudioHandle, unsigned int);
typedef TUsbAudioStatus (*fn_GetDeviceStreamingMode)(TUsbAudioHandle, unsigned int*);

int main() {
    printf("=== TUSBAUDIO Streaming Mode Test ===\n\n");
    
    HMODULE hApi = LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioapi_x64.dll");
    if (!hApi) { printf("Failed to load API DLL\n"); return 1; }
    
    auto Enumerate = (fn_EnumerateDevices)GetProcAddress(hApi, "TUSBAUDIO_EnumerateDevices");
    auto GetCount = (fn_GetDeviceCount)GetProcAddress(hApi, "TUSBAUDIO_GetDeviceCount");
    auto OpenDev = (fn_OpenDeviceByIndex)GetProcAddress(hApi, "TUSBAUDIO_OpenDeviceByIndex");
    auto CloseDev = (fn_CloseDevice)GetProcAddress(hApi, "TUSBAUDIO_CloseDevice");
    auto GetRate = (fn_GetCurrentSampleRate)GetProcAddress(hApi, "TUSBAUDIO_GetCurrentSampleRate");
    auto GetStreamCh = (fn_GetStreamChannelCount)GetProcAddress(hApi, "TUSBAUDIO_GetStreamChannelCount");
    auto SetMode = (fn_SetDeviceStreamingMode)GetProcAddress(hApi, "TUSBAUDIO_SetDeviceStreamingMode");
    auto GetMode = (fn_GetDeviceStreamingMode)GetProcAddress(hApi, "TUSBAUDIO_GetDeviceStreamingMode");
    
    TUsbAudioStatus st = Enumerate();
    printf("Enumerate: 0x%08X\n", st);
    
    unsigned int count = 0;
    GetCount(&count);
    printf("Device count: %u\n", count);
    
    if (count == 0) { printf("No devices\n"); return 1; }
    
    TUsbAudioHandle hDev = NULL;
    st = OpenDev(0, &hDev);
    printf("Open: 0x%08X, handle=%p\n", st, hDev);
    
    unsigned int rate = 0;
    GetRate(hDev, &rate);
    printf("Sample rate: %u\n", rate);
    
    unsigned int inCh = 0, outCh = 0;
    GetStreamCh(hDev, 0, &inCh);  // 0 = capture
    GetStreamCh(hDev, 1, &outCh); // 1 = playback
    printf("Stream channels: in=%u out=%u\n", inCh, outCh);
    
    // 检查当前 streaming mode
    unsigned int mode = 0;
    if (GetMode) {
        st = GetMode(hDev, &mode);
        printf("Current streaming mode: %u (0x%08X)\n", mode, st);
    }
    
    // 尝试切换到 ASIO 模式 (mode=2)
    printf("\nSetting streaming mode to ASIO (2)...\n");
    st = SetMode(hDev, 2);
    printf("SetStreamingMode(2): 0x%08X\n", st);
    
    if (GetMode) {
        st = GetMode(hDev, &mode);
        printf("Mode after set: %u (0x%08X)\n", mode, st);
    }
    
    // 恢复 WDM 模式
    printf("\nRestoring WDM mode (1)...\n");
    st = SetMode(hDev, 1);
    printf("SetStreamingMode(1): 0x%08X\n", st);
    
    CloseDev(hDev);
    FreeLibrary(hApi);
    printf("\nDone.\n");
    return 0;
}
