/*
 * TUSBAUDIO IOCTL Protocol Spy
 * 
 * Hook DeviceIoControl in the official ASIO DLL to capture
 * the exact IOCTL protocol used for audio streaming.
 */

#include <windows.h>
#include <stdio.h>
#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// Log file
static FILE* g_logFile = NULL;
static int g_callCount = 0;

// Original DeviceIoControl
typedef BOOL (WINAPI *PFN_DeviceIoControl)(
    HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static PFN_DeviceIoControl g_origDeviceIoControl = NULL;

// Original CreateFileW
typedef HANDLE (WINAPI *PFN_CreateFileW)(
    LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static PFN_CreateFileW g_origCreateFileW = NULL;

static void hexdump(FILE* f, const void* data, DWORD size, int maxBytes) {
    const unsigned char* p = (const unsigned char*)data;
    DWORD show = (size < (DWORD)maxBytes) ? size : (DWORD)maxBytes;
    for (DWORD i = 0; i < show; i++) {
        fprintf(f, "%02X ", p[i]);
        if ((i + 1) % 32 == 0) fprintf(f, "\n        ");
    }
    if (show < size) fprintf(f, "... (%u more bytes)", size - show);
    fprintf(f, "\n");
}

HANDLE WINAPI Hook_CreateFileW(
    LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    HANDLE h = g_origCreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
        lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    
    if (g_logFile && lpFileName) {
        // Only log USB/TUSBAUDIO paths
        if (wcsstr(lpFileName, L"vid_1397") || wcsstr(lpFileName, L"tusbaudio") ||
            wcsstr(lpFileName, L"215a80ef")) {
            fprintf(g_logFile, "\n=== CreateFileW ===\n");
            fprintf(g_logFile, "  Path: %ls\n", lpFileName);
            fprintf(g_logFile, "  Access: 0x%08X  Flags: 0x%08X\n", dwDesiredAccess, dwFlagsAndAttributes);
            fprintf(g_logFile, "  Result: %p (err=%lu)\n", h, h == INVALID_HANDLE_VALUE ? GetLastError() : 0);
            fflush(g_logFile);
        }
    }
    return h;
}

BOOL WINAPI Hook_DeviceIoControl(
    HANDLE hDevice, DWORD dwIoControlCode,
    LPVOID lpInBuffer, DWORD nInBufferSize,
    LPVOID lpOutBuffer, DWORD nOutBufferSize,
    LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped)
{
    g_callCount++;
    
    // Call original first
    BOOL result = g_origDeviceIoControl(hDevice, dwIoControlCode,
        lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize,
        lpBytesReturned, lpOverlapped);
    
    DWORD err = result ? 0 : GetLastError();
    DWORD returned = lpBytesReturned ? *lpBytesReturned : 0;
    
    if (g_logFile) {
        // Decode IOCTL
        DWORD deviceType = (dwIoControlCode >> 16) & 0xFFFF;
        DWORD access = (dwIoControlCode >> 14) & 3;
        DWORD function = (dwIoControlCode >> 2) & 0xFFF;
        DWORD method = dwIoControlCode & 3;
        
        const char* accessStr[] = {"ANY", "READ", "WRITE", "RW"};
        const char* methodStr[] = {"BUFFERED", "IN_DIRECT", "OUT_DIRECT", "NEITHER"};
        
        fprintf(g_logFile, "\n--- IOCTL #%d ---\n", g_callCount);
        fprintf(g_logFile, "  Handle: %p\n", hDevice);
        fprintf(g_logFile, "  Code: 0x%08X (DevType=0x%X Func=%u Method=%s Access=%s)\n",
                dwIoControlCode, deviceType, function, methodStr[method], accessStr[access]);
        fprintf(g_logFile, "  InBuf: %p (%u bytes)\n", lpInBuffer, nInBufferSize);
        if (lpInBuffer && nInBufferSize > 0) {
            fprintf(g_logFile, "    In: ");
            hexdump(g_logFile, lpInBuffer, nInBufferSize, 128);
        }
        fprintf(g_logFile, "  OutBuf: %p (%u bytes)\n", lpOutBuffer, nOutBufferSize);
        fprintf(g_logFile, "  Result: %s (err=%lu, returned=%u)\n",
                result ? "OK" : "FAIL", err, returned);
        if (result && lpOutBuffer && returned > 0) {
            fprintf(g_logFile, "    Out: ");
            hexdump(g_logFile, lpOutBuffer, returned, 128);
        }
        fflush(g_logFile);
    }
    
    if (!result) SetLastError(err);
    return result;
}

// IAT hook helper
static bool PatchIAT(HMODULE hModule, const char* dllName, const char* funcName, void* newFunc, void** origFunc) {
    ULONG size;
    PIMAGE_IMPORT_DESCRIPTOR importDesc = (PIMAGE_IMPORT_DESCRIPTOR)
        ImageDirectoryEntryToDataEx(hModule, TRUE, IMAGE_DIRECTORY_ENTRY_IMPORT, &size, NULL);
    
    if (!importDesc) return false;
    
    for (; importDesc->Name; importDesc++) {
        const char* name = (const char*)((BYTE*)hModule + importDesc->Name);
        if (_stricmp(name, dllName) != 0) continue;
        
        PIMAGE_THUNK_DATA origThunk = (PIMAGE_THUNK_DATA)((BYTE*)hModule + importDesc->OriginalFirstThunk);
        PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)((BYTE*)hModule + importDesc->FirstThunk);
        
        for (; origThunk->u1.AddressOfData; origThunk++, thunk++) {
            if (origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            PIMAGE_IMPORT_BY_NAME import = (PIMAGE_IMPORT_BY_NAME)((BYTE*)hModule + origThunk->u1.AddressOfData);
            if (strcmp(import->Name, funcName) == 0) {
                DWORD oldProtect;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect);
                *origFunc = (void*)thunk->u1.Function;
                thunk->u1.Function = (ULONG_PTR)newFunc;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &oldProtect);
                return true;
            }
        }
    }
    return false;
}

// ASIOBufferInfo and ASIOCallbacks structures
#pragma pack(push, 4)
struct MyASIOBufferInfo {
    long isInput;
    long channelNum;
    void* buffers[2];
};

struct MyASIOCallbacks {
    void (*bufferSwitch)(long doubleBufferIndex, long directProcess);
    void (*sampleRateDidChange)(double sRate);
    long (*asioMessage)(long selector, long value, void* message, double* opt);
    void* (*bufferSwitchTimeInfo)(void* params, long doubleBufferIndex, long directProcess);
};
#pragma pack(pop)

static long g_bufferIndex = 0;

static void myBufferSwitch(long idx, long directProcess) {
    g_bufferIndex = idx;
    if (g_logFile) {
        fprintf(g_logFile, "\n  >>> bufferSwitch(idx=%ld, direct=%ld)\n", idx, directProcess);
        fflush(g_logFile);
    }
}
static void mySampleRateChanged(double r) {}
static long myAsioMessage(long sel, long val, void* msg, double* opt) {
    if (sel == 1) return 1;
    if (sel == 7) return 1;
    return 0;
}

MyASIOCallbacks g_callbacks = { myBufferSwitch, mySampleRateChanged, myAsioMessage, NULL };

int main() {
    printf("=== TUSBAUDIO IOCTL Protocol Spy ===\n\n");
    
    g_logFile = fopen("D:\\UMCasio\\ioctl_trace.log", "w");
    if (!g_logFile) { printf("Cannot open log\n"); return 1; }
    fprintf(g_logFile, "TUSBAUDIO IOCTL Protocol Trace\n");
    fprintf(g_logFile, "==============================\n\n");
    
    // Load official DLL
    HMODULE hDll = LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    if (!hDll) { printf("Failed to load DLL: %lu\n", GetLastError()); return 1; }
    printf("DLL loaded at %p\n", hDll);
    
    // Hook DeviceIoControl and CreateFileW in the loaded DLL's IAT
    bool hooked1 = PatchIAT(hDll, "kernel32.dll", "DeviceIoControl", 
                             (void*)Hook_DeviceIoControl, (void**)&g_origDeviceIoControl);
    bool hooked2 = PatchIAT(hDll, "kernel32.dll", "CreateFileW",
                             (void*)Hook_CreateFileW, (void**)&g_origCreateFileW);
    
    // Also try api-ms-win-core-* (modern Windows redirects)
    if (!hooked1) hooked1 = PatchIAT(hDll, "api-ms-win-core-io-l1-1-0.dll", "DeviceIoControl",
                                      (void*)Hook_DeviceIoControl, (void**)&g_origDeviceIoControl);
    if (!hooked2) hooked2 = PatchIAT(hDll, "api-ms-win-core-file-l1-1-0.dll", "CreateFileW",
                                      (void*)Hook_CreateFileW, (void**)&g_origCreateFileW);
    
    printf("Hook DeviceIoControl: %s\n", hooked1 ? "OK" : "FAIL");
    printf("Hook CreateFileW: %s\n", hooked2 ? "OK" : "FAIL");
    
    if (!hooked1) {
        // Fallback: use real functions but still log
        g_origDeviceIoControl = (PFN_DeviceIoControl)GetProcAddress(GetModuleHandleA("kernel32.dll"), "DeviceIoControl");
        g_origCreateFileW = (PFN_CreateFileW)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateFileW");
        printf("Using fallback (no hooking)\n");
    }
    
    // Get DllGetClassObject
    typedef HRESULT (WINAPI *FnDllGetClassObject)(REFCLSID, REFIID, LPVOID*);
    auto pGetClassObj = (FnDllGetClassObject)GetProcAddress(hDll, "DllGetClassObject");
    if (!pGetClassObj) { printf("No DllGetClassObject\n"); return 1; }
    
    // CLSID of official Behringer ASIO
    const CLSID clsid = {0x0351302f, 0xb1f1, 0x4a5d, {0x86,0x13,0x78,0x7f,0x77,0xc2,0x0e,0xa4}};
    
    IClassFactory* pFactory = NULL;
    HRESULT hr = pGetClassObj(clsid, IID_IClassFactory, (void**)&pFactory);
    printf("GetClassObject: 0x%08X\n", hr);
    if (FAILED(hr)) return 1;
    
    fprintf(g_logFile, "\n========== Creating ASIO Instance ==========\n");
    
    IUnknown* pUnk = NULL;
    hr = pFactory->CreateInstance(NULL, clsid, (void**)&pUnk);
    pFactory->Release();
    printf("CreateInstance: 0x%08X, ptr=%p\n", hr, pUnk);
    if (FAILED(hr) || !pUnk) return 1;
    
    // Cast to IASIO - we use vtable directly since the interface is defined in our headers
    // But the official DLL's IASIO vtable might not match ours exactly
    // Instead, call methods via the COM interface
    
    // Since we have the IUnknown, and the object IS an IASIO, 
    // we can use the vtable directly (IASIO inherits from IUnknown)
    void** vtable = *(void***)pUnk;
    
    // IASIO vtable layout (after IUnknown):
    // [0] QueryInterface, [1] AddRef, [2] Release
    // [3] init, [4] getDriverName, [5] getDriverVersion, [6] getErrorMessage
    // [7] start, [8] stop, [9] getChannels, [10] getLatencies, [11] getBufferSize
    // [12] canSampleRate, [13] getSampleRate, [14] setSampleRate
    // [15] getClockSources, [16] setClockSource, [17] getSamplePosition
    // [18] getChannelInfo, [19] createBuffers, [20] disposeBuffers
    // [21] controlPanel, [22] future, [23] outputReady
    
    typedef BOOL (__thiscall *fn_init)(void*, void*);
    typedef void (__thiscall *fn_getDriverName)(void*, char*);
    typedef long (__thiscall *fn_getDriverVersion)(void*);
    typedef long (__thiscall *fn_getChannels)(void*, long*, long*);
    typedef long (__thiscall *fn_getBufferSize)(void*, long*, long*, long*, long*);
    typedef long (__thiscall *fn_getSampleRate)(void*, double*);
    typedef long (__thiscall *fn_setSampleRate)(void*, double);
    typedef long (__thiscall *fn_start)(void*);
    typedef long (__thiscall *fn_stop)(void*);
    typedef long (__thiscall *fn_disposeBuffers)(void*);
    typedef long (__thiscall *fn_outputReady)(void*);
    typedef long (__thiscall *fn_createBuffers)(void*, MyASIOBufferInfo*, long, long, MyASIOCallbacks*);
    
    fprintf(g_logFile, "\n========== init() ==========\n");
    printf("\nCalling init()...\n");
    BOOL initOk = ((fn_init)vtable[3])(pUnk, NULL);
    printf("init: %d\n", initOk);
    
    if (initOk) {
        char name[64] = {};
        ((fn_getDriverName)vtable[4])(pUnk, name);
        printf("Driver name: %s\n", name);
        
        long ver = ((fn_getDriverVersion)vtable[5])(pUnk);
        printf("Version: %ld\n", ver);
        
        long numIn = 0, numOut = 0;
        fprintf(g_logFile, "\n========== getChannels() ==========\n");
        ((fn_getChannels)vtable[9])(pUnk, &numIn, &numOut);
        printf("Channels: %ld in / %ld out\n", numIn, numOut);
        
        long minBuf, maxBuf, prefBuf, gran;
        fprintf(g_logFile, "\n========== getBufferSize() ==========\n");
        ((fn_getBufferSize)vtable[11])(pUnk, &minBuf, &maxBuf, &prefBuf, &gran);
        printf("Buffer: min=%ld max=%ld pref=%ld gran=%ld\n", minBuf, maxBuf, prefBuf, gran);
        
        double rate;
        fprintf(g_logFile, "\n========== getSampleRate() ==========\n");
        ((fn_getSampleRate)vtable[13])(pUnk, &rate);
        printf("Rate: %.0f\n", rate);
        
        // === createBuffers ===
        long totalCh = numIn + numOut;
        if (totalCh > 64) totalCh = 64;
        MyASIOBufferInfo* bufInfos = new MyASIOBufferInfo[totalCh];
        memset(bufInfos, 0, sizeof(MyASIOBufferInfo) * totalCh);
        
        long idx = 0;
        for (long i = 0; i < numIn && idx < totalCh; i++, idx++) {
            bufInfos[idx].isInput = 1;
            bufInfos[idx].channelNum = i;
        }
        for (long i = 0; i < numOut && idx < totalCh; i++, idx++) {
            bufInfos[idx].isInput = 0;
            bufInfos[idx].channelNum = i;
        }
        
        fprintf(g_logFile, "\n========== createBuffers(%ld ch, %ld frames) ==========\n", idx, prefBuf);
        printf("\nCreating buffers (%ld channels, %ld frames)...\n", idx, prefBuf);
        
        long cbErr = ((fn_createBuffers)vtable[19])(pUnk, bufInfos, idx, prefBuf, &g_callbacks);
        printf("createBuffers: %ld\n", cbErr);
        
        if (cbErr == 0) {
            printf("Buffer pointers:\n");
            for (long i = 0; i < idx && i < 4; i++) {
                printf("  ch%ld (%s): buf[0]=%p buf[1]=%p\n", 
                       bufInfos[i].channelNum,
                       bufInfos[i].isInput ? "in" : "out",
                       bufInfos[i].buffers[0], bufInfos[i].buffers[1]);
            }
            
            // === start ===
            fprintf(g_logFile, "\n========== start() ==========\n");
            printf("\nStarting...\n");
            long startErr = ((fn_start)vtable[7])(pUnk);
            printf("start: %ld\n", startErr);
            
            if (startErr == 0) {
                // Let it run briefly
                printf("Running for 500ms...\n");
                Sleep(500);
                
                // === stop ===
                fprintf(g_logFile, "\n========== stop() ==========\n");
                printf("Stopping...\n");
                long stopErr = ((fn_stop)vtable[8])(pUnk);
                printf("stop: %ld\n", stopErr);
            }
            
            // === disposeBuffers ===
            fprintf(g_logFile, "\n========== disposeBuffers() ==========\n");
            ((fn_disposeBuffers)vtable[20])(pUnk);
        }
        
        delete[] bufInfos;
    }
    
    printf("\n%d IOCTL calls logged to ioctl_trace.log\n", g_callCount);
    
    // Cleanup
    fprintf(g_logFile, "\n========== Release ==========\n");
    pUnk->Release();
    
    fprintf(g_logFile, "\nTotal IOCTL calls: %d\n", g_callCount);
    fclose(g_logFile);
    FreeLibrary(hDll);
    
    printf("Done! Check D:\\UMCasio\\ioctl_trace.log\n");
    return 0;
}
