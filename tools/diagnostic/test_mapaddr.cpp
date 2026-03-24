// 验证: MAP 地址上有没有数据 vs createBuffers 返回的地址
#include <windows.h>
#include <stdio.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

typedef BOOL (WINAPI *PFN_DIO)(HANDLE,DWORD,LPVOID,DWORD,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
static PFN_DIO g_oDIO = NULL;

// Capture MAP_CHANNEL_BUFFER addresses
static void* g_mapAddrs[4] = {};
static int g_mapCount = 0;

BOOL WINAPI Hook_DIO(HANDLE h, DWORD code, LPVOID in, DWORD inSz,
    LPVOID out, DWORD outSz, LPDWORD ret, LPOVERLAPPED ov) {
    BOOL ok = g_oDIO(h, code, in, inSz, out, outSz, ret, ov);
    if (((code>>16)&0xFFFF)==0x8088 && code==0x808828A0 && in && inSz==24 && g_mapCount < 4) {
        void* addr = (void*)(uintptr_t)(*(UINT64*)((BYTE*)in+16));
        DWORD chId = *(DWORD*)((BYTE*)in);
        g_mapAddrs[g_mapCount] = addr;
        printf("[MAP] ch=0x%02X addr=%p (#%d)\n", chId, addr, g_mapCount);
        g_mapCount++;
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

static volatile LONG g_sc = 0;
void __cdecl bswitch(long i, long d) { InterlockedIncrement(&g_sc); }
void __cdecl srch(double r) {}
long __cdecl amsg(long s, long v, void* m, double* o) { if(s==1||s==7) return 1; return 0; }
void* __cdecl bsti(void* p, long i, long d) { InterlockedIncrement(&g_sc); return p; }

int main() {
    printf("=== MAP Address vs Buffer Addr ===\n\n");
    
    HMODULE hDll = LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    if (!hDll) return 1;
    const char* dlls[] = {"kernel32.dll","api-ms-win-core-io-l1-1-0.dll"};
    for (auto d : dlls) { if (!g_oDIO) PatchIAT(hDll,d,"DeviceIoControl",(void*)Hook_DIO,(void**)&g_oDIO); }
    if (!g_oDIO) g_oDIO = (PFN_DIO)GetProcAddress(GetModuleHandleA("kernel32.dll"),"DeviceIoControl");

    typedef HRESULT (WINAPI *FnGCO)(REFCLSID,REFIID,LPVOID*);
    const CLSID clsid = {0x0351302f,0xb1f1,0x4a5d,{0x86,0x13,0x78,0x7f,0x77,0xc2,0x0e,0xa4}};
    IClassFactory* pF; ((FnGCO)GetProcAddress(hDll,"DllGetClassObject"))(clsid,IID_IClassFactory,(void**)&pF);
    IUnknown* pU; pF->CreateInstance(NULL,clsid,(void**)&pU); pF->Release();
    void** vt = *(void***)pU;

    typedef BOOL (__thiscall *fn_i)(void*,void*);
    typedef long (__thiscall *fn_gc)(void*,long*,long*);
    typedef long (__thiscall *fn_gb)(void*,long*,long*,long*,long*);
    typedef long (__thiscall *fn_st)(void*);
    typedef long (__thiscall *fn_sp)(void*);
    typedef long (__thiscall *fn_db)(void*);

    ((fn_i)vt[3])(pU, NULL);
    long nIn,nOut; ((fn_gc)vt[9])(pU,&nIn,&nOut);
    long mi,ma,pf,gr; ((fn_gb)vt[11])(pU,&mi,&ma,&pf,&gr);

    struct CB { void(__cdecl*a)(long,long); void(__cdecl*b)(double); long(__cdecl*c)(long,long,void*,double*); void*(__cdecl*d)(void*,long,long); };
    struct BI { long isIn; long ch; void* buf[2]; };
    CB cbs = {bswitch,srch,amsg,bsti};
    BI bi[2] = {{1,0,{0,0}},{0,0,{0,0}}};
    
    typedef long (__thiscall *fn_cb)(void*,BI*,long,long,CB*);
    ((fn_cb)vt[19])(pU, bi, 2, pf, &cbs);
    
    printf("\ncreateBuffers returned:\n");
    printf("  In0: buf[0]=%p buf[1]=%p\n", bi[0].buf[0], bi[0].buf[1]);
    printf("  Out0: buf[0]=%p buf[1]=%p\n", bi[1].buf[0], bi[1].buf[1]);
    printf("\nMAP_CHANNEL_BUFFER sent:\n");
    for (int i = 0; i < g_mapCount; i++)
        printf("  MAP[%d]: %p\n", i, g_mapAddrs[i]);
    
    printf("\nSame addr? In0: MAP=%p == buf[0]=%p? %s\n", 
           g_mapAddrs[0], bi[0].buf[0],
           g_mapAddrs[0] == bi[0].buf[0] ? "YES!!" : "NO - different!");
    
    // START and check data
    ((fn_st)vt[7])(pU);
    Sleep(500);
    printf("\nbufferSwitch: %ld\n", g_sc);
    
    // Check MAP address
    printf("MAP[0] first 16 bytes: ");
    __try {
        BYTE* m = (BYTE*)g_mapAddrs[0];
        for (int i = 0; i < 16; i++) printf("%02X ", m[i]);
    } __except(EXCEPTION_EXECUTE_HANDLER) { printf("[ACCESS VIOLATION]"); }
    printf("\n");
    
    // Check buf[0]
    printf("buf[0] first 4 floats: ");
    float* d = (float*)bi[0].buf[0];
    printf("%.6f %.6f %.6f %.6f\n", d[0], d[1], d[2], d[3]);
    
    ((fn_sp)vt[8])(pU);
    ((fn_db)vt[20])(pU);
    pU->Release();
    printf("Done!\n");
    return 0;
}
