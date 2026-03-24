// 对比: 1) 我们的 START IOCTL vs 2) 官方 start() 方法
// 看事件是在 start() 还是 START_IOCTL 后触发
#include <windows.h>
#include <stdio.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

typedef BOOL(WINAPI*PFN_DIO)(HANDLE,DWORD,LPVOID,DWORD,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
static PFN_DIO g_oDIO=NULL;
static HANDLE g_devH=NULL, g_captEvt[3]={};

static int g_ioctlAfterStart = 0;
static bool g_trackAfterStart = false;

BOOL WINAPI Hook_DIO(HANDLE h,DWORD c,LPVOID in,DWORD inSz,LPVOID out,DWORD outSz,LPDWORD ret,LPOVERLAPPED ov){
    BOOL ok=g_oDIO(h,c,in,inSz,out,outSz,ret,ov);
    DWORD dt=(c>>16)&0xFFFF;
    if(dt==0x8088){
        if(!g_devH)g_devH=h;
        if(c==0x80882880&&in&&inSz==32){
            g_captEvt[0]=(HANDLE)(uintptr_t)(*(UINT64*)((BYTE*)in+8));
            g_captEvt[1]=(HANDLE)(uintptr_t)(*(UINT64*)((BYTE*)in+16));
            g_captEvt[2]=(HANDLE)(uintptr_t)(*(UINT64*)((BYTE*)in+24));
        }
        DWORD func=(c>>2)&0xFFF;
        if(g_trackAfterStart){
            g_ioctlAfterStart++;
            DWORD br=ret?*ret:0;
            printf("  [POST-START IOCTL #%d] 0x%08X Func=%u in=%u out=%u ret=%u\n", 
                   g_ioctlAfterStart, c, func, inSz, outSz, br);
        }
        if(c==0x808828C8){
            printf("[CAPTURED START_STREAMING] by official start()\n");
            g_trackAfterStart = true;
        }
    }
    return ok;
}

static bool PatchIAT(HMODULE m,const char*d,const char*f,void*n,void**o){
    ULONG sz;auto im=(PIMAGE_IMPORT_DESCRIPTOR)ImageDirectoryEntryToDataEx(m,TRUE,IMAGE_DIRECTORY_ENTRY_IMPORT,&sz,NULL);
    if(!im)return false;
    for(;im->Name;im++){if(_stricmp((char*)((BYTE*)m+im->Name),d)!=0)continue;
    auto ot=(PIMAGE_THUNK_DATA)((BYTE*)m+im->OriginalFirstThunk);auto th=(PIMAGE_THUNK_DATA)((BYTE*)m+im->FirstThunk);
    for(;ot->u1.AddressOfData;ot++,th++){if(ot->u1.Ordinal&IMAGE_ORDINAL_FLAG)continue;
    auto imp=(PIMAGE_IMPORT_BY_NAME)((BYTE*)m+ot->u1.AddressOfData);
    if(strcmp(imp->Name,f)==0){DWORD op;VirtualProtect(&th->u1.Function,8,PAGE_READWRITE,&op);
    *o=(void*)th->u1.Function;th->u1.Function=(ULONG_PTR)n;VirtualProtect(&th->u1.Function,8,op,&op);return true;}}}
    return false;
}

static volatile LONG g_sc=0;
void __cdecl bsw(long i,long d){InterlockedIncrement(&g_sc);}
void __cdecl sr(double r){}
long __cdecl am(long s,long v,void*m,double*o){if(s==1||s==7)return 1;return 0;}
void* __cdecl bsti(void*p,long i,long d){InterlockedIncrement(&g_sc);return p;}

int main(){
    HMODULE hDll=LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    const char*ds[]={"kernel32.dll","api-ms-win-core-io-l1-1-0.dll"};
    for(auto d:ds){if(!g_oDIO)PatchIAT(hDll,d,"DeviceIoControl",(void*)Hook_DIO,(void**)&g_oDIO);}
    if(!g_oDIO)g_oDIO=(PFN_DIO)GetProcAddress(GetModuleHandleA("kernel32.dll"),"DeviceIoControl");
    
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
    CB cbs={bsw,sr,am,bsti};
    BI bi[2]={{1,0,{0,0}},{0,0,{0,0}}};
    typedef long(__thiscall*fcb)(void*,BI*,long,long,CB*);
    ((fcb)vt[19])(pU,bi,2,pf,&cbs);
    
    printf("Events: %p %p %p\n", g_captEvt[0], g_captEvt[1], g_captEvt[2]);
    
    printf("\n=== Calling official start() ===\n");
    g_ioctlAfterStart = 0;
    g_trackAfterStart = false;
    long startResult = ((fst)vt[7])(pU);
    printf("start() returned: %ld\n", startResult);
    printf("IOCTLs after START_STREAMING: %d\n", g_ioctlAfterStart);
    
    // Quick check
    Sleep(500);
    printf("bSwitch after 0.5s: %ld\n", g_sc);
    
    ((fsp)vt[8])(pU);
    ((fdb)vt[20])(pU);
    pU->Release();
    return 0;
}
