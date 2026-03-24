// 使用SEH保护的事件监听器
#include <windows.h>
#include <stdio.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

typedef BOOL (WINAPI *PFN_DIO)(HANDLE,DWORD,LPVOID,DWORD,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
static PFN_DIO g_oDIO = NULL;
static HANDLE g_capturedEvents[3] = {};
static BYTE* g_capturedCtrl = NULL;

BOOL WINAPI Hook_DIO(HANDLE hDev, DWORD code, LPVOID in, DWORD inSz,
    LPVOID out, DWORD outSz, LPDWORD ret, LPOVERLAPPED ov) {
    BOOL ok = g_oDIO(hDev, code, in, inSz, out, outSz, ret, ov);
    if (((code >> 16) & 0xFFFF) == 0x8088 && code == 0x80882880 && in && inSz == 32) {
        g_capturedCtrl = (BYTE*)(uintptr_t)(*(UINT64*)((BYTE*)in));
        g_capturedEvents[0] = (HANDLE)(uintptr_t)(*(UINT64*)((BYTE*)in+8));
        g_capturedEvents[1] = (HANDLE)(uintptr_t)(*(UINT64*)((BYTE*)in+16));
        g_capturedEvents[2] = (HANDLE)(uintptr_t)(*(UINT64*)((BYTE*)in+24));
        printf("[CAPTURED] ctrl=%p evt=%p %p %p\n", g_capturedCtrl,
               g_capturedEvents[0], g_capturedEvents[1], g_capturedEvents[2]);
    }
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
                *of = (void*)th->u1.Function; th->u1.Function = (ULONG_PTR)nf;
                VirtualProtect(&th->u1.Function,8,op,&op); return true;
            }
        }
    }
    return false;
}

// ASIO Time Info struct (simplified)
struct ASIOTimeInfo { double speed; UINT64 samplePos; UINT64 timeCode; DWORD flags; char pad[64]; };

static volatile LONG g_sc = 0;
static void* g_inBuf0 = NULL;

void __cdecl my_bufferSwitch(long idx, long dp) {
    InterlockedIncrement(&g_sc);
}

void __cdecl my_sampleRateChanged(double r) {}

long __cdecl my_asioMessage(long sel, long val, void* msg, double* opt) {
    switch (sel) {
        case 1: return 1; // kAsioSelectorSupported
        case 4: return 1; // kAsioSupportsTimeInfo
        case 7: return 1; // kAsioSupportsTimeCode
        default: return 0;
    }
}

void* __cdecl my_bufferSwitchTimeInfo(void* params, long idx, long dp) {
    InterlockedIncrement(&g_sc);
    return params;
}

int main() {
    printf("=== Event Monitor v2 ===\n\n");
    
    HMODULE hDll = LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    if (!hDll) { printf("Load fail\n"); return 1; }
    
    const char* dlls[] = {"kernel32.dll","api-ms-win-core-io-l1-1-0.dll"};
    for (auto d : dlls) {
        if (!g_oDIO) PatchIAT(hDll, d, "DeviceIoControl", (void*)Hook_DIO, (void**)&g_oDIO);
    }
    if (!g_oDIO) g_oDIO = (PFN_DIO)GetProcAddress(GetModuleHandleA("kernel32.dll"),"DeviceIoControl");
    
    typedef HRESULT (WINAPI *FnGCO)(REFCLSID,REFIID,LPVOID*);
    auto pGCO = (FnGCO)GetProcAddress(hDll, "DllGetClassObject");
    const CLSID clsid = {0x0351302f,0xb1f1,0x4a5d,{0x86,0x13,0x78,0x7f,0x77,0xc2,0x0e,0xa4}};
    IClassFactory* pF; pGCO(clsid, IID_IClassFactory, (void**)&pF);
    IUnknown* pU; pF->CreateInstance(NULL, clsid, (void**)&pU); pF->Release();
    void** vt = *(void***)pU;
    
    typedef BOOL (__thiscall *fn_init)(void*,void*);
    typedef long (__thiscall *fn_getChannels)(void*,long*,long*);
    typedef long (__thiscall *fn_getBufferSize)(void*,long*,long*,long*,long*);
    typedef long (__thiscall *fn_start)(void*);
    typedef long (__thiscall *fn_stop)(void*);
    typedef long (__thiscall *fn_db)(void*);

    if (!((fn_init)vt[3])(pU, NULL)) { printf("init fail\n"); pU->Release(); return 1; }
    printf("init OK\n");
    
    long nIn, nOut;
    ((fn_getChannels)vt[9])(pU, &nIn, &nOut);
    long minB,maxB,prefB,gran;
    ((fn_getBufferSize)vt[11])(pU, &minB,&maxB,&prefB,&gran);
    printf("Ch: %ld/%ld  Buf: %ld\n", nIn, nOut, prefB);
    
    // Use proper struct layout with 8-byte aligned function pointers
    struct Callbacks {
        void (__cdecl *bufferSwitch)(long, long);
        void (__cdecl *sampleRateChanged)(double);
        long (__cdecl *asioMessage)(long, long, void*, double*);
        void* (__cdecl *bufferSwitchTimeInfo)(void*, long, long);
    };
    
    Callbacks cbs;
    cbs.bufferSwitch = my_bufferSwitch;
    cbs.sampleRateChanged = my_sampleRateChanged;
    cbs.asioMessage = my_asioMessage;
    cbs.bufferSwitchTimeInfo = my_bufferSwitchTimeInfo;
    
    #pragma pack(push, 8)
    struct BI { long isInput; long channelNum; void* buffers[2]; };
    #pragma pack(pop)
    
    BI bi[2]; memset(bi, 0, sizeof(bi));
    bi[0].isInput = 1; bi[0].channelNum = 0;
    bi[1].isInput = 0; bi[1].channelNum = 0;
    
    typedef long (__thiscall *fn_cb)(void*,BI*,long,long,Callbacks*);
    
    long cbr = ((fn_cb)vt[19])(pU, bi, 2, prefB, &cbs);
    printf("createBuffers: %ld\n", cbr);
    
    if (cbr == 0) {
        g_inBuf0 = bi[0].buffers[0];
        printf("In0: %p/%p  Out0: %p/%p\n", bi[0].buffers[0],bi[0].buffers[1],bi[1].buffers[0],bi[1].buffers[1]);
        
        printf("Starting...\n");
        g_sc = 0;
        long sr = ((fn_start)vt[7])(pU);
        printf("start: %ld\n", sr);
        
        if (sr == 0) {
            Sleep(1000);
            printf("\n=== After 1s ===\n");
            printf("bufferSwitch count: %ld (%.1f Hz)\n", g_sc, (double)g_sc);
            
            if (g_inBuf0) {
                float* d = (float*)g_inBuf0;
                printf("In0 buf0: %.6f %.6f %.6f %.6f\n", d[0],d[1],d[2],d[3]);
            }
            
            if (g_capturedCtrl) {
                printf("ctrl[0..31]: ");
                for (int i = 0; i < 32; i++) printf("%02X ", g_capturedCtrl[i]);
                printf("\n");
            }
        }
        
        printf("\nStopping...\n");
        ((fn_stop)vt[8])(pU);
        ((fn_db)vt[20])(pU);
    }
    
    pU->Release();
    printf("Done!\n");
    return 0;
}
