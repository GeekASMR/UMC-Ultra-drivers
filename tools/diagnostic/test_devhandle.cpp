/*
 * test_audiobuf.cpp - Minimal: just open device and check buffer memory
 */
#include <windows.h>
#include <stdio.h>
#include <math.h>

typedef unsigned int TUsbAudioStatus;
typedef void* TUsbAudioHandle;

int main() {
    printf("Loading DLL...\n"); fflush(stdout);
    HMODULE hDll = LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioapi_x64.dll");
    if (!hDll) { printf("Cannot load DLL: %d\n", GetLastError()); return 1; }

    auto pfnEnum = (decltype(&GetLastError))GetProcAddress(hDll, "TUSBAUDIO_EnumerateDevices");
    auto pfnCount = (unsigned int(__cdecl*)())GetProcAddress(hDll, "TUSBAUDIO_GetDeviceCount");
    auto pfnOpen = (TUsbAudioStatus(__cdecl*)(unsigned int, TUsbAudioHandle*))GetProcAddress(hDll, "TUSBAUDIO_OpenDeviceByIndex");
    auto pfnClose = (TUsbAudioStatus(__cdecl*)(TUsbAudioHandle))GetProcAddress(hDll, "TUSBAUDIO_CloseDevice");
    auto pfnSetMode = (TUsbAudioStatus(__cdecl*)(TUsbAudioHandle, unsigned int))GetProcAddress(hDll, "TUSBAUDIO_SetDeviceStreamingMode");
    
    printf("Enumerating...\n"); fflush(stdout);
    unsigned int st = (unsigned int)(size_t)pfnEnum();
    printf("Enum result: 0x%08X\n", st); fflush(stdout);
    if (st != 0) { printf("Enum failed!\n"); FreeLibrary(hDll); return 1; }

    printf("Count: %d\n", pfnCount()); fflush(stdout);

    TUsbAudioHandle handle = nullptr;
    st = pfnOpen(0, &handle);
    printf("Open: 0x%08X handle=%p\n", st, handle); fflush(stdout);
    if (st != 0 || !handle) { FreeLibrary(hDll); return 1; }

    unsigned char* base = (unsigned char*)handle;
    
    // Read key pointers safely
    printf("\nChecking memory regions...\n"); fflush(stdout);
    
    struct { int offset; const char* name; } ptrs[] = {
        {0x48, "buf_0x48"},
        {0x780, "buf_0x780"}, {0x788, "buf_0x788"}, 
        {0x790, "buf_0x790"}, {0x798, "buf_0x798"},
        {0x7A0, "buf_0x7A0"}, {0x7B0, "buf_0x7B0"},
        {0x7D0, "buf_0x7D0"}, {0x7E0, "buf_0x7E0"},
        {0x820, "buf_0x820"}, {0x830, "buf_0x830"},
        {0x840, "buf_0x840"}, {0x850, "buf_0x850"},
        {0x860, "buf_0x860"}, {0x870, "buf_0x870"},
        {0x890, "buf_0x890"}, {0x8A0, "buf_0x8A0"},
        {0x8B0, "buf_0x8B0"},
    };

    for (auto& pt : ptrs) {
        void* addr = *(void**)(base + pt.offset);
        if (!addr) continue;
        
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(addr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) continue;
        
        int size = (int)mbi.RegionSize;
        if (size > 4096) size = 4096; // limit check
        
        unsigned char* p = (unsigned char*)addr;
        int nonZero = 0;
        for (int i = 0; i < size; i++) if (p[i] != 0) nonZero++;
        
        // Max as 32-bit int
        float maxF = 0;
        int* s32 = (int*)p;
        for (int i = 0; i < size/4 && i < 512; i++) {
            float f = fabsf((float)s32[i] / 2147483648.0f);
            if (f > maxF) maxF = f;
        }
        
        printf("  %s: addr=%p regionSize=%llu nonZero=%d max32=%.6f\n", 
               pt.name, addr, (ULONGLONG)mbi.RegionSize, nonZero, maxF);
        printf("    [0..31]: ");
        for (int i = 0; i < 32; i++) printf("%02X ", p[i]);
        printf("\n");
        fflush(stdout);
    }
    
    // Snapshot 192KB buffer, wait, compare
    void* mainBuf = *(void**)(base + 0x788);
    if (mainBuf) {
        unsigned char snap[512];
        memcpy(snap, mainBuf, 512);
        printf("\nWaiting 500ms...\n"); fflush(stdout);
        Sleep(500);
        int changed = 0;
        for (int i = 0; i < 512; i++)
            if (((unsigned char*)mainBuf)[i] != snap[i]) changed++;
        printf("192KB buffer: %d/512 bytes changed -> %s\n", changed, changed > 0 ? "LIVE AUDIO!" : "static");
        fflush(stdout);
    }

    pfnSetMode(handle, 1);
    pfnClose(handle);
    FreeLibrary(hDll);
    printf("\nDone\n");
    return 0;
}
