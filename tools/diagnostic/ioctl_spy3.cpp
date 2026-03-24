// Spy v3 - Hook VirtualAlloc + Memory allocation tracking
#include <windows.h>
#include <stdio.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

static FILE* g_log = NULL;

// Original functions
typedef BOOL (WINAPI *PFN_DIO)(HANDLE,DWORD,LPVOID,DWORD,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
typedef HANDLE (WINAPI *PFN_CFW)(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
typedef HANDLE (WINAPI *PFN_CEW)(LPSECURITY_ATTRIBUTES,BOOL,BOOL,LPCWSTR);
typedef LPVOID (WINAPI *PFN_VA)(LPVOID,SIZE_T,DWORD,DWORD);
typedef LPVOID (WINAPI *PFN_VAEx)(HANDLE,LPVOID,SIZE_T,DWORD,DWORD);
typedef LPVOID (WINAPI *PFN_MVF)(HANDLE,DWORD,DWORD,DWORD,SIZE_T);
typedef LPVOID (WINAPI *PFN_MVFEx)(HANDLE,DWORD,DWORD,DWORD,SIZE_T,LPVOID);
typedef HANDLE (WINAPI *PFN_CFM)(LPSECURITY_ATTRIBUTES,DWORD,DWORD,DWORD,LPCWSTR);

static PFN_DIO g_oDIO = NULL;
static PFN_CFW g_oCFW = NULL;
static PFN_CEW g_oCEW = NULL;
static PFN_VA g_oVA = NULL;
static PFN_VAEx g_oVAEx = NULL;
static PFN_MVF g_oMVF = NULL;
static PFN_MVFEx g_oMVFEx = NULL;
static PFN_CFM g_oCFM = NULL;

static int g_evtCount = 0;

HANDLE WINAPI Hook_CEW(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL init, LPCWSTR name) {
    HANDLE h = g_oCEW(sa, manual, init, name);
    g_evtCount++;
    fprintf(g_log, "[EVT] CreateEventW(%s,%s) = %p (#%d)\n", manual?"MAN":"AUTO", init?"SET":"UNSET", h, g_evtCount);
    fflush(g_log);
    return h;
}

LPVOID WINAPI Hook_VA(LPVOID addr, SIZE_T sz, DWORD type, DWORD prot) {
    LPVOID r = g_oVA(addr, sz, type, prot);
    if (sz >= 0x1000) { // Only log significant allocations
        fprintf(g_log, "[MEM] VirtualAlloc(addr=%p, size=0x%llX, type=0x%X, prot=0x%X) = %p\n",
                addr, (unsigned long long)sz, type, prot, r);
        fflush(g_log);
    }
    return r;
}

LPVOID WINAPI Hook_VAEx(HANDLE proc, LPVOID addr, SIZE_T sz, DWORD type, DWORD prot) {
    LPVOID r = g_oVAEx(proc, addr, sz, type, prot);
    if (sz >= 0x1000) {
        fprintf(g_log, "[MEM] VirtualAllocEx(proc=%p, addr=%p, size=0x%llX, type=0x%X, prot=0x%X) = %p\n",
                proc, addr, (unsigned long long)sz, type, prot, r);
        fflush(g_log);
    }
    return r;
}

HANDLE WINAPI Hook_CFM(LPSECURITY_ATTRIBUTES sa, DWORD prot, DWORD szHi, DWORD szLo, LPCWSTR name) {
    HANDLE h = g_oCFM(sa, prot, szHi, szLo, name);
    fprintf(g_log, "[MEM] CreateFileMappingW(prot=0x%X, size=0x%X%08X, name=%ls) = %p (err=%lu)\n",
            prot, szHi, szLo, name?name:L"(null)", h, h?0:GetLastError());
    fflush(g_log);
    return h;
}

LPVOID WINAPI Hook_MVF(HANDLE map, DWORD access, DWORD offHi, DWORD offLo, SIZE_T bytes) {
    LPVOID r = g_oMVF(map, access, offHi, offLo, bytes);
    fprintf(g_log, "[MEM] MapViewOfFile(map=%p, access=0x%X, off=0x%X%08X, bytes=0x%llX) = %p\n",
            map, access, offHi, offLo, (unsigned long long)bytes, r);
    fflush(g_log);
    return r;
}

HANDLE WINAPI Hook_CFW(LPCWSTR fn, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t) {
    HANDLE h = g_oCFW(fn, a, s, sa, c, f, t);
    if (fn && (wcsstr(fn,L"vid_1397")||wcsstr(fn,L"tusbaudio")||wcsstr(fn,L"215a80ef")))
        fprintf(g_log, "\n[DEV] CreateFileW: %ls => %p (flags=0x%X)\n", fn, h, f);
    fflush(g_log);
    return h;
}

static void hexdump(FILE* f, const void* d, DWORD sz, int max) {
    const unsigned char* p = (const unsigned char*)d;
    DWORD show = sz < (DWORD)max ? sz : (DWORD)max;
    for (DWORD i = 0; i < show; i++) {
        fprintf(f, "%02X ", p[i]);
        if ((i+1)%32==0) fprintf(f, "\n      ");
    }
    if (show < sz) fprintf(f, "...(+%u)", sz-show);
    fprintf(f, "\n");
}

BOOL WINAPI Hook_DIO(HANDLE hDev, DWORD code, LPVOID in, DWORD inSz,
    LPVOID out, DWORD outSz, LPDWORD ret, LPOVERLAPPED ov) {
    BOOL ok = g_oDIO(hDev, code, in, inSz, out, outSz, ret, ov);
    DWORD err = ok ? 0 : GetLastError();
    DWORD devType = (code >> 16) & 0xFFFF;
    if (devType == 0x8088 && g_log) {
        DWORD func = (code >> 2) & 0xFFF;
        DWORD br = ret ? *ret : 0;
        fprintf(g_log, "\n[IO] 0x%08X Func=%u OV=%p Result=%s(err=%lu,ret=%u)\n",
                code, func, ov, ok?"OK":"FAIL", err, br);
        if (in && inSz > 0) { fprintf(g_log, "  In(%u): ", inSz); hexdump(g_log, in, inSz, 64); }
        if (ok && out && br > 0) { fprintf(g_log, "  Out(%u): ", br); hexdump(g_log, out, br, 64); }
        fflush(g_log);
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
    printf("=== Spy v3 - Memory + Events + IOCTL ===\n");
    g_log = fopen("D:\\UMCasio\\ioctl_trace3.log", "w");
    fprintf(g_log, "=== TUSBAUDIO Trace v3 (Memory+Events+IOCTL) ===\n\n");
    
    HMODULE hDll = LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    if (!hDll) { printf("Load fail\n"); return 1; }
    printf("DLL at %p\n", hDll);
    
    const char* dlls[] = {"kernel32.dll", "api-ms-win-core-io-l1-1-0.dll", 
        "api-ms-win-core-file-l1-1-0.dll", "api-ms-win-core-synch-l1-2-0.dll",
        "api-ms-win-core-synch-l1-1-0.dll", "api-ms-win-core-memory-l1-1-0.dll",
        "api-ms-win-core-file-l1-2-0.dll"};
    
    for (auto d : dlls) {
        if (!g_oDIO) PatchIAT(hDll, d, "DeviceIoControl", (void*)Hook_DIO, (void**)&g_oDIO);
        if (!g_oCFW) PatchIAT(hDll, d, "CreateFileW", (void*)Hook_CFW, (void**)&g_oCFW);
        if (!g_oCEW) PatchIAT(hDll, d, "CreateEventW", (void*)Hook_CEW, (void**)&g_oCEW);
        if (!g_oVA)  PatchIAT(hDll, d, "VirtualAlloc", (void*)Hook_VA, (void**)&g_oVA);
        if (!g_oVAEx) PatchIAT(hDll, d, "VirtualAllocEx", (void*)Hook_VAEx, (void**)&g_oVAEx);
        if (!g_oCFM) PatchIAT(hDll, d, "CreateFileMappingW", (void*)Hook_CFM, (void**)&g_oCFM);
        if (!g_oMVF) PatchIAT(hDll, d, "MapViewOfFile", (void*)Hook_MVF, (void**)&g_oMVF);
    }
    
    // Fallbacks
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (!g_oDIO) g_oDIO = (PFN_DIO)GetProcAddress(k32, "DeviceIoControl");
    if (!g_oCFW) g_oCFW = (PFN_CFW)GetProcAddress(k32, "CreateFileW");
    if (!g_oCEW) g_oCEW = (PFN_CEW)GetProcAddress(k32, "CreateEventW");
    if (!g_oVA)  g_oVA  = (PFN_VA)GetProcAddress(k32, "VirtualAlloc");
    if (!g_oVAEx) g_oVAEx = (PFN_VAEx)GetProcAddress(k32, "VirtualAllocEx");
    if (!g_oCFM) g_oCFM = (PFN_CFM)GetProcAddress(k32, "CreateFileMappingW");
    if (!g_oMVF) g_oMVF = (PFN_MVF)GetProcAddress(k32, "MapViewOfFile");
    
    printf("Hooks: DIO=%d CFW=%d CEW=%d VA=%d VAEx=%d CFM=%d MVF=%d\n",
           !!g_oDIO, !!g_oCFW, !!g_oCEW, !!g_oVA, !!g_oVAEx, !!g_oCFM, !!g_oMVF);
    
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
    
    fprintf(g_log, "\n=== init() ===\n");
    BOOL initOk = ((fn_init)vt[3])(pU, NULL);
    printf("init: %d\n", initOk);
    
    if (initOk) {
        long nIn, nOut;
        ((fn_getChannels)vt[9])(pU, &nIn, &nOut);
        printf("Ch: %ld in / %ld out\n", nIn, nOut);
        
        long minB, maxB, prefB, gran;
        ((fn_getBufferSize)vt[11])(pU, &minB, &maxB, &prefB, &gran);
        printf("Buf: pref=%ld\n", prefB);
        
        // createBuffers with minimal channels
        #pragma pack(push, 4)
        struct BI { long isInput; long channelNum; void* buffers[2]; };
        struct CB {
            void (*bs)(long,long);
            void (*sr)(double);
            long (*am)(long,long,void*,double*);
            void* (*bsti)(void*,long,long);
        };
        #pragma pack(pop)
        
        static volatile int g_sc = 0;
        struct L {
            static void bs(long i, long d) { g_sc++; }
            static void sr(double r) {}
            static long am(long s, long v, void* m, double* o) { if(s==1||s==7) return 1; return 0; }
        };
        CB cbs = {L::bs, L::sr, L::am, NULL};
        
        BI bi[2] = {{1,0,{0,0}}, {0,0,{0,0}}}; // 1 in + 1 out
        
        typedef long (__thiscall *fn_cb)(void*, BI*, long, long, CB*);
        typedef long (__thiscall *fn_start)(void*);
        typedef long (__thiscall *fn_stop)(void*);
        typedef long (__thiscall *fn_db)(void*);
        
        fprintf(g_log, "\n=== createBuffers(2ch, %ld) ===\n", prefB);
        long cbr = ((fn_cb)vt[19])(pU, bi, 2, prefB, &cbs);
        printf("createBuffers: %ld\n", cbr);
        
        if (cbr == 0) {
            printf("Buffers: in[0]=%p/%p out[0]=%p/%p\n",
                   bi[0].buffers[0], bi[0].buffers[1],
                   bi[1].buffers[0], bi[1].buffers[1]);
            
            fprintf(g_log, "\n=== start() ===\n");
            long sr = ((fn_start)vt[7])(pU);
            printf("start: %ld\n", sr);
            
            if (sr == 0) {
                Sleep(500);
                printf("bufferSwitch count: %d (%.1f Hz)\n", g_sc, g_sc/0.5);
                
                float* inBuf = (float*)bi[0].buffers[0];
                if (inBuf) {
                    printf("In0 buf0: %.6f %.6f %.6f %.6f\n", inBuf[0],inBuf[1],inBuf[2],inBuf[3]);
                }
            }
            
            fprintf(g_log, "\n=== stop() ===\n");
            ((fn_stop)vt[8])(pU);
            fprintf(g_log, "\n=== disposeBuffers() ===\n");
            ((fn_db)vt[20])(pU);
        }
    }
    
    pU->Release();
    fprintf(g_log, "\nDone. Events=%d\n", g_evtCount);
    fclose(g_log);
    printf("Done! See ioctl_trace3.log\n");
    return 0;
}
