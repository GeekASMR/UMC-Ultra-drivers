// 直接通过官方 DLL 启动流, 然后监听事件
#include <windows.h>
#include <stdio.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

typedef BOOL (WINAPI *PFN_DIO)(HANDLE,DWORD,LPVOID,DWORD,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
static PFN_DIO g_oDIO = NULL;

static HANDLE g_capturedEvents[3] = {};
static int g_capturedEvtCount = 0;
static BYTE* g_capturedCtrl = NULL;

BOOL WINAPI Hook_DIO(HANDLE hDev, DWORD code, LPVOID in, DWORD inSz,
    LPVOID out, DWORD outSz, LPDWORD ret, LPOVERLAPPED ov) {
    BOOL ok = g_oDIO(hDev, code, in, inSz, out, outSz, ret, ov);
    DWORD devType = (code >> 16) & 0xFFFF;
    if (devType == 0x8088 && code == 0x80882880 && in && inSz == 32) {
        // Capture SET_CALLBACKS data
        g_capturedCtrl = (BYTE*)(uintptr_t)(*(UINT64*)((BYTE*)in + 0));
        g_capturedEvents[0] = (HANDLE)(uintptr_t)(*(UINT64*)((BYTE*)in + 8));
        g_capturedEvents[1] = (HANDLE)(uintptr_t)(*(UINT64*)((BYTE*)in + 16));
        g_capturedEvents[2] = (HANDLE)(uintptr_t)(*(UINT64*)((BYTE*)in + 24));
        printf("[CAPTURED] ctrl=%p events=%p %p %p\n", 
               g_capturedCtrl, g_capturedEvents[0], g_capturedEvents[1], g_capturedEvents[2]);
    }
    if (!ok) SetLastError(GetLastError());
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
                DWORD op; VirtualProtect(&th->u1.Function,8,PAGE_READWRITE,&op);
                *of = (void*)th->u1.Function;
                th->u1.Function = (ULONG_PTR)nf;
                VirtualProtect(&th->u1.Function,8,op,&op);
                return true;
            }
        }
    }
    return false;
}

int main() {
    printf("=== Event Monitor - Official DLL ===\n\n");
    
    HMODULE hDll = LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    if (!hDll) return 1;
    
    // Only hook DeviceIoControl to capture SET_CALLBACKS
    const char* dlls[] = {"kernel32.dll","api-ms-win-core-io-l1-1-0.dll"};
    for (auto d : dlls) {
        if (!g_oDIO) PatchIAT(hDll, d, "DeviceIoControl", (void*)Hook_DIO, (void**)&g_oDIO);
    }
    if (!g_oDIO) g_oDIO = (PFN_DIO)GetProcAddress(GetModuleHandleA("kernel32.dll"), "DeviceIoControl");
    
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
    typedef long (__thiscall *fn_db)(void*);
    
    BOOL initOk = ((fn_init)vt[3])(pU, NULL);
    printf("init: %d\n", initOk);
    if (!initOk) { pU->Release(); return 1; }
    
    long nIn, nOut;
    ((fn_getChannels)vt[9])(pU, &nIn, &nOut);
    printf("Ch: %ld/%ld\n", nIn, nOut);
    
    long minB, maxB, prefB, gran;
    ((fn_getBufferSize)vt[11])(pU, &minB, &maxB, &prefB, &gran);
    
    // createBuffers
    #pragma pack(push, 4)
    struct BI { long isInput; long channelNum; void* buffers[2]; };
    struct CB {
        void (*bs)(long,long); void (*sr)(double);
        long (*am)(long,long,void*,double*); void* (*bsti)(void*,long,long);
    };
    #pragma pack(pop)
    
    static volatile LONG g_sc = 0;
    struct L {
        static void bs(long i, long d) { InterlockedIncrement(&g_sc); }
        static void sr(double r) {}
        static long am(long s, long v, void* m, double* o) { if(s==1||s==7) return 1; return 0; }
    };
    CB cbs = {L::bs, L::sr, L::am, NULL};
    
    BI bi[2] = {{1,0,{0,0}}, {0,0,{0,0}}};
    typedef long (__thiscall *fn_cb)(void*, BI*, long, long, CB*);
    
    long cbr = ((fn_cb)vt[19])(pU, bi, 2, prefB, &cbs);
    printf("createBuffers: %ld\n", cbr);
    printf("Buffers: in=%p/%p out=%p/%p\n", bi[0].buffers[0],bi[0].buffers[1],bi[1].buffers[0],bi[1].buffers[1]);
    
    if (cbr == 0) {
        printf("\nStarting...\n");
        long sr = ((fn_start)vt[7])(pU);
        printf("start: %ld\n", sr);
        
        if (sr == 0) {
            // Monitor events AND bufferSwitch for 2 seconds
            DWORD t0 = GetTickCount();
            int evtCounts[3] = {};
            
            while (GetTickCount() - t0 < 2000) {
                if (g_capturedEvents[0]) {
                    DWORD wait = WaitForMultipleObjects(3, g_capturedEvents, FALSE, 10);
                    if (wait >= WAIT_OBJECT_0 && wait < WAIT_OBJECT_0+3) {
                        evtCounts[wait-WAIT_OBJECT_0]++;
                        int total = evtCounts[0]+evtCounts[1]+evtCounts[2];
                        if (total <= 3)
                            printf("  [%4ums] Evt%d, total=%d, bs=%ld\n", 
                                   GetTickCount()-t0, (int)(wait-WAIT_OBJECT_0), total, g_sc);
                    }
                }
                Sleep(1);
            }
            
            printf("\nResults (2s):\n");
            printf("  bufferSwitch: %ld (%.1f Hz)\n", g_sc, g_sc/2.0);
            printf("  events: %d %d %d\n", evtCounts[0], evtCounts[1], evtCounts[2]);
            
            if (bi[0].buffers[0]) {
                float* d = (float*)bi[0].buffers[0];
                printf("  In0 data: %.6f %.6f %.6f %.6f\n", d[0],d[1],d[2],d[3]);
            }
            
            // Check ctrl page
            if (g_capturedCtrl) {
                printf("  ctrl first 32B: ");
                for (int i = 0; i < 32; i++) printf("%02X ", g_capturedCtrl[i]);
                printf("\n");
            }
        }
        
        ((fn_stop)vt[8])(pU);
        ((fn_db)vt[20])(pU);
    }
    
    pU->Release();
    printf("\nDone!\n");
    return 0;
}
