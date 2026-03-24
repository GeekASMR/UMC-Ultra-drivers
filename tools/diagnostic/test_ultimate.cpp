// 终极测试: 用官方DLL的init+createBuffers, 然后自己调START IOCTL
// 看看是官方的start()做了什么额外操作, 还是init/createBuffers设了什么状态
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "setupapi.lib")

static const GUID G = {0x215A80EF,0x69BD,0x4D85,{0xAC,0x71,0x0C,0x6E,0xA6,0xE6,0xBE,0x17}};

typedef BOOL (WINAPI *PFN_DIO)(HANDLE,DWORD,LPVOID,DWORD,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
static PFN_DIO g_oDIO = NULL;
static HANDLE g_devHandle = NULL;
static HANDLE g_capturedEvents[3] = {};

BOOL WINAPI Hook_DIO(HANDLE h, DWORD code, LPVOID in, DWORD inSz,
    LPVOID out, DWORD outSz, LPDWORD ret, LPOVERLAPPED ov) {
    BOOL ok = g_oDIO(h, code, in, inSz, out, outSz, ret, ov);
    DWORD dt = (code>>16)&0xFFFF;
    if (dt == 0x8088) {
        if (!g_devHandle) g_devHandle = h;
        if (code == 0x80882880 && in && inSz==32) {
            g_capturedEvents[0] = (HANDLE)(uintptr_t)(*(UINT64*)((BYTE*)in+8));
            g_capturedEvents[1] = (HANDLE)(uintptr_t)(*(UINT64*)((BYTE*)in+16));
            g_capturedEvents[2] = (HANDLE)(uintptr_t)(*(UINT64*)((BYTE*)in+24));
        }
    }
    return ok;
}

static bool PatchIAT(HMODULE m, const char* d, const char* f, void* n, void** o) {
    ULONG sz; auto im=(PIMAGE_IMPORT_DESCRIPTOR)ImageDirectoryEntryToDataEx(m,TRUE,IMAGE_DIRECTORY_ENTRY_IMPORT,&sz,NULL);
    if(!im) return false;
    for(;im->Name;im++){
        if(_stricmp((char*)((BYTE*)m+im->Name),d)!=0) continue;
        auto ot=(PIMAGE_THUNK_DATA)((BYTE*)m+im->OriginalFirstThunk);
        auto th=(PIMAGE_THUNK_DATA)((BYTE*)m+im->FirstThunk);
        for(;ot->u1.AddressOfData;ot++,th++){
            if(ot->u1.Ordinal&IMAGE_ORDINAL_FLAG) continue;
            auto imp=(PIMAGE_IMPORT_BY_NAME)((BYTE*)m+ot->u1.AddressOfData);
            if(strcmp(imp->Name,f)==0){
                DWORD op; VirtualProtect(&th->u1.Function,8,PAGE_READWRITE,&op);
                *o=(void*)th->u1.Function; th->u1.Function=(ULONG_PTR)n;
                VirtualProtect(&th->u1.Function,8,op,&op); return true;
            }
        }
    }
    return false;
}

static volatile LONG g_sc = 0;
void __cdecl bsw(long i, long d) { InterlockedIncrement(&g_sc); }
void __cdecl srch(double r) {}
long __cdecl amsg(long s, long v, void* m, double* o) { if(s==1||s==7) return 1; return 0; }
void* __cdecl bsti(void* p, long i, long d) { InterlockedIncrement(&g_sc); return p; }

int main() {
    printf("=== Ultimate Test: Official init+createBuffers, OWN IOCTL start ===\n\n");
    
    HMODULE hDll = LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    const char* ds[] = {"kernel32.dll","api-ms-win-core-io-l1-1-0.dll"};
    for(auto d:ds){if(!g_oDIO)PatchIAT(hDll,d,"DeviceIoControl",(void*)Hook_DIO,(void**)&g_oDIO);}
    if(!g_oDIO) g_oDIO=(PFN_DIO)GetProcAddress(GetModuleHandleA("kernel32.dll"),"DeviceIoControl");
    
    typedef HRESULT(WINAPI*FnGCO)(REFCLSID,REFIID,LPVOID*);
    const CLSID cls={0x0351302f,0xb1f1,0x4a5d,{0x86,0x13,0x78,0x7f,0x77,0xc2,0x0e,0xa4}};
    IClassFactory*pF;((FnGCO)GetProcAddress(hDll,"DllGetClassObject"))(cls,IID_IClassFactory,(void**)&pF);
    IUnknown*pU;pF->CreateInstance(NULL,cls,(void**)&pU);pF->Release();
    void**vt=*(void***)pU;
    
    typedef BOOL(__thiscall*fi)(void*,void*);
    typedef long(__thiscall*fgc)(void*,long*,long*);
    typedef long(__thiscall*fgb)(void*,long*,long*,long*,long*);
    typedef long(__thiscall*fst)(void*);
    typedef long(__thiscall*fsp)(void*);
    typedef long(__thiscall*fdb)(void*);
    
    ((fi)vt[3])(pU,NULL);
    long nI,nO;((fgc)vt[9])(pU,&nI,&nO);
    long mi,ma,pf,gr;((fgb)vt[11])(pU,&mi,&ma,&pf,&gr);
    
    struct CB{void(__cdecl*a)(long,long);void(__cdecl*b)(double);long(__cdecl*c)(long,long,void*,double*);void*(__cdecl*d)(void*,long,long);};
    struct BI{long isIn;long ch;void*buf[2];};
    CB cbs={bsw,srch,amsg,bsti};
    BI bi[2]={{1,0,{0,0}},{0,0,{0,0}}};
    typedef long(__thiscall*fcb)(void*,BI*,long,long,CB*);
    ((fcb)vt[19])(pU,bi,2,pf,&cbs);
    
    printf("Device handle: %p\n", g_devHandle);
    printf("Events: %p %p %p\n", g_capturedEvents[0], g_capturedEvents[1], g_capturedEvents[2]);
    printf("Buffers: In=%p Out=%p\n", bi[0].buf[0], bi[1].buf[0]);
    
    // NOW: call START_STREAMING directly via our own IOCTL 
    // using the SAME device handle the official DLL is using!
    printf("\n--- Calling START via OUR OWN DeviceIoControl ---\n");
    OVERLAPPED ov = {}; 
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    DWORD br;
    BOOL ok = DeviceIoControl(g_devHandle, 0x808828C8, NULL, 0, NULL, 0, &br, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
        ok = GetOverlappedResult(g_devHandle, &ov, &br, TRUE);
    printf("START: %s\n", ok?"OK":"FAIL");
    
    // Wait and monitor
    Sleep(1000);
    printf("bufferSwitch: %ld (%.0f Hz)\n", g_sc, (double)g_sc);
    
    // Also try waiting for captured events ourselves  
    printf("Waiting our own 1s for events...\n");
    LONG prevSc = g_sc;
    DWORD t0 = GetTickCount();
    int evtCount = 0;
    while (GetTickCount()-t0 < 1000) {
        DWORD w = WaitForMultipleObjects(3, g_capturedEvents, FALSE, 10);
        if (w >= WAIT_OBJECT_0 && w < WAIT_OBJECT_0+3) evtCount++;
    }
    printf("Extra events caught: %d, extra bSwitch: %ld\n", evtCount, g_sc - prevSc);
    
    // Stop + cleanup
    DeviceIoControl(g_devHandle, 0x808828CC, NULL, 0, NULL, 0, &br, &ov);
    ((fdb)vt[20])(pU);
    pU->Release();
    printf("Done!\n");
    return 0;
}
