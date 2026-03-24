// Minimal spy v2 - track OVERLAPPED and CreateEventW
#include <windows.h>
#include <stdio.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

static FILE* g_log = NULL;
static int g_callCount = 0;

// Original functions
typedef BOOL (WINAPI *PFN_DeviceIoControl)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef HANDLE (WINAPI *PFN_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI *PFN_CreateEventW)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
typedef BOOL (WINAPI *PFN_SetEvent)(HANDLE);

static PFN_DeviceIoControl g_origDIO = NULL;
static PFN_CreateFileW g_origCFW = NULL;
static PFN_CreateEventW g_origCEW = NULL;

static int g_eventCount = 0;

HANDLE WINAPI Hook_CreateEventW(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCWSTR name) {
    HANDLE h = g_origCEW(sa, manual, initial, name);
    g_eventCount++;
    if (g_log) {
        fprintf(g_log, "  CreateEventW(%s, %s) = %p (#%d)\n",
                manual?"MANUAL":"AUTO", initial?"SET":"UNSET", h, g_eventCount);
        fflush(g_log);
    }
    return h;
}

HANDLE WINAPI Hook_CreateFileW(LPCWSTR fn, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t) {
    HANDLE h = g_origCFW(fn, a, s, sa, c, f, t);
    if (g_log && fn && (wcsstr(fn, L"vid_1397") || wcsstr(fn, L"tusbaudio") || wcsstr(fn, L"215a80ef"))) {
        fprintf(g_log, "\n=== CreateFileW: %ls => %p (flags=0x%X) ===\n", fn, h, f);
        fflush(g_log);
    }
    return h;
}

BOOL WINAPI Hook_DeviceIoControl(HANDLE hDev, DWORD code, LPVOID in, DWORD inSz,
    LPVOID out, DWORD outSz, LPDWORD ret, LPOVERLAPPED ov) {
    g_callCount++;
    BOOL ok = g_origDIO(hDev, code, in, inSz, out, outSz, ret, ov);
    DWORD err = ok ? 0 : GetLastError();
    
    if (g_log) {
        DWORD devType = (code >> 16) & 0xFFFF;
        if (devType == 0x8088) {
            DWORD func = (code >> 2) & 0xFFF;
            fprintf(g_log, "\n--- IOCTL #%d: 0x%08X Func=%u ---\n", g_callCount, code, func);
            fprintf(g_log, "  Overlapped: %p\n", ov);
            if (ov) fprintf(g_log, "  OV.hEvent=%p Internal=%llu\n", ov->hEvent, ov->Internal);
            fprintf(g_log, "  Result: %s (err=%lu)\n", ok?"OK":"FAIL/PENDING", err);
            if (err == ERROR_IO_PENDING) fprintf(g_log, "  ** IO_PENDING - async! **\n");
            fflush(g_log);
        }
    }
    if (!ok) SetLastError(err);
    return ok;
}

static bool PatchIAT(HMODULE hMod, const char* dll, const char* func, void* nf, void** of) {
    ULONG sz; auto im = (PIMAGE_IMPORT_DESCRIPTOR)ImageDirectoryEntryToDataEx(hMod,TRUE,IMAGE_DIRECTORY_ENTRY_IMPORT,&sz,NULL);
    if (!im) return false;
    for (; im->Name; im++) {
        if (_stricmp((char*)((BYTE*)hMod+im->Name), dll) != 0) continue;
        auto ot = (PIMAGE_THUNK_DATA)((BYTE*)hMod+im->OriginalFirstThunk);
        auto th = (PIMAGE_THUNK_DATA)((BYTE*)hMod+im->FirstThunk);
        for (; ot->u1.AddressOfData; ot++, th++) {
            if (ot->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            auto imp = (PIMAGE_IMPORT_BY_NAME)((BYTE*)hMod+ot->u1.AddressOfData);
            if (strcmp(imp->Name, func)==0) {
                DWORD op; VirtualProtect(&th->u1.Function, 8, PAGE_READWRITE, &op);
                *of = (void*)th->u1.Function;
                th->u1.Function = (ULONG_PTR)nf;
                VirtualProtect(&th->u1.Function, 8, op, &op);
                return true;
            }
        }
    }
    return false;
}

int main() {
    printf("=== IOCTL Spy v2 (Overlapped + Events) ===\n");
    g_log = fopen("D:\\UMCasio\\ioctl_trace2.log", "w");
    fprintf(g_log, "TUSBAUDIO Trace v2\n==================\n");
    
    HMODULE hDll = LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    if (!hDll) { printf("Load fail\n"); return 1; }
    
    // Hook in DLL
    const char* dlls[] = {"kernel32.dll", "api-ms-win-core-io-l1-1-0.dll", "api-ms-win-core-file-l1-1-0.dll", 
                           "api-ms-win-core-synch-l1-2-0.dll", "api-ms-win-core-synch-l1-1-0.dll"};
    for (auto d : dlls) {
        if (!g_origDIO) PatchIAT(hDll, d, "DeviceIoControl", (void*)Hook_DeviceIoControl, (void**)&g_origDIO);
        if (!g_origCFW) PatchIAT(hDll, d, "CreateFileW", (void*)Hook_CreateFileW, (void**)&g_origCFW);
        if (!g_origCEW) PatchIAT(hDll, d, "CreateEventW", (void*)Hook_CreateEventW, (void**)&g_origCEW);
    }
    
    if (!g_origDIO) g_origDIO = (PFN_DeviceIoControl)GetProcAddress(GetModuleHandleA("kernel32.dll"), "DeviceIoControl");
    if (!g_origCFW) g_origCFW = (PFN_CreateFileW)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateFileW");
    if (!g_origCEW) g_origCEW = (PFN_CreateEventW)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateEventW");
    
    printf("Hooks: DIO=%s CFW=%s CEW=%s\n", g_origDIO?"Y":"N", g_origCFW?"Y":"N", g_origCEW?"Y":"N");
    
    // Create ASIO instance
    typedef HRESULT (WINAPI *FnGCO)(REFCLSID, REFIID, LPVOID*);
    auto pGCO = (FnGCO)GetProcAddress(hDll, "DllGetClassObject");
    const CLSID clsid = {0x0351302f,0xb1f1,0x4a5d,{0x86,0x13,0x78,0x7f,0x77,0xc2,0x0e,0xa4}};
    IClassFactory* pF; pGCO(clsid, IID_IClassFactory, (void**)&pF);
    IUnknown* pU; pF->CreateInstance(NULL, clsid, (void**)&pU); pF->Release();
    void** vt = *(void***)pU;
    
    typedef BOOL (__thiscall *fn_init)(void*, void*);
    typedef long (__thiscall *fn_getChannels)(void*, long*, long*);
    typedef long (__thiscall *fn_getBufferSize)(void*, long*, long*, long*, long*);
    typedef long (__thiscall *fn_start)(void*);
    typedef long (__thiscall *fn_stop)(void*);
    typedef long (__thiscall *fn_disposeBuffers)(void*);
    
    fprintf(g_log, "\n========== init ==========\n");
    BOOL initOk = ((fn_init)vt[3])(pU, NULL);
    printf("init: %d\n", initOk);
    if (!initOk) { pU->Release(); return 1; }
    
    long nIn, nOut;
    ((fn_getChannels)vt[9])(pU, &nIn, &nOut);
    printf("Channels: %ld in / %ld out\n", nIn, nOut);
    
    long minB, maxB, prefB, gran;
    ((fn_getBufferSize)vt[11])(pU, &minB, &maxB, &prefB, &gran);
    
    // createBuffers with callbacks
    #pragma pack(push, 4)
    struct BufInfo { long isInput; long channelNum; void* buffers[2]; };
    struct Callbacks {
        void (*bufferSwitch)(long, long);
        void (*sampleRateDidChange)(double);
        long (*asioMessage)(long, long, void*, double*);
        void* (*bufferSwitchTimeInfo)(void*, long, long);
    };
    #pragma pack(pop)
    
    static int g_switchCount = 0;
    struct L {
        static void bufSwitch(long idx, long dp) { g_switchCount++; }
        static void srChange(double r) {}
        static long asioMsg(long s, long v, void* m, double* o) { 
            if (s==1) return 1; if (s==7) return 1; return 0; 
        }
    };
    Callbacks cbs = {L::bufSwitch, L::srChange, L::asioMsg, NULL};
    
    // Only use 2 in + 2 out for testing
    long numCh = 4;
    BufInfo* bi = new BufInfo[numCh];
    memset(bi, 0, sizeof(BufInfo)*numCh);
    bi[0] = {1, 0, {0,0}};
    bi[1] = {1, 1, {0,0}};
    bi[2] = {0, 0, {0,0}};
    bi[3] = {0, 1, {0,0}};
    
    typedef long (__thiscall *fn_createBuffers)(void*, BufInfo*, long, long, Callbacks*);
    fprintf(g_log, "\n========== createBuffers ==========\n");
    long cbResult = ((fn_createBuffers)vt[19])(pU, bi, numCh, prefB, &cbs);
    printf("createBuffers: %ld\n", cbResult);
    
    if (cbResult == 0) {
        fprintf(g_log, "\n========== start ==========\n");
        printf("Starting...\n");
        long sr = ((fn_start)vt[7])(pU);
        printf("start: %ld\n", sr);
        
        if (sr == 0) {
            printf("Running 2s... (watching bufferSwitch)\n");
            Sleep(2000);
            printf("bufferSwitch called %d times (%.1f Hz)\n", g_switchCount, g_switchCount/2.0);
            
            // Check buffer data
            if (bi[0].buffers[0]) {
                float* inData = (float*)bi[0].buffers[0];
                printf("In0 buf0: %.6f %.6f %.6f %.6f\n", inData[0], inData[1], inData[2], inData[3]);
            }
        }
        
        fprintf(g_log, "\n========== stop ==========\n");
        ((fn_stop)vt[8])(pU);
        ((fn_disposeBuffers)vt[20])(pU);
    }
    
    fprintf(g_log, "\nTotal IOCTLs: %d, Events created: %d\n", g_callCount, g_eventCount);
    pU->Release();
    fclose(g_log);
    printf("Done! See ioctl_trace2.log\n");
    return 0;
}
