// Hook CreateThread 看看官方 start() 在什么时候创建线程
#include <windows.h>
#include <stdio.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

typedef BOOL(WINAPI*PFN_DIO)(HANDLE,DWORD,LPVOID,DWORD,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
typedef HANDLE(WINAPI*PFN_CT)(LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE,LPVOID,DWORD,LPDWORD);
typedef BOOL(WINAPI*PFN_SE)(HANDLE);
static PFN_DIO g_oDIO=NULL;
static PFN_CT g_oCT=NULL;
static PFN_SE g_oSE=NULL;
static HANDLE g_devH=NULL;

HANDLE WINAPI HookCT(LPSECURITY_ATTRIBUTES sa,SIZE_T stack,LPTHREAD_START_ROUTINE fn,LPVOID arg,DWORD flags,LPDWORD tid){
    HANDLE h=g_oCT(sa,stack,fn,arg,flags,tid);
    DWORD t=tid?*tid:0;
    printf("[THREAD] CreateThread(fn=%p, arg=%p, flags=0x%X) = %p tid=%lu\n",fn,arg,flags,h,t);
    return h;
}

BOOL WINAPI HookSE(HANDLE h){
    printf("[EVENT] SetEvent(%p)\n",h);
    return g_oSE(h);
}

BOOL WINAPI HookDIO(HANDLE h,DWORD c,LPVOID in,DWORD inSz,LPVOID out,DWORD outSz,LPDWORD ret,LPOVERLAPPED ov){
    DWORD dt=(c>>16)&0xFFFF;
    if(dt==0x8088){
        if(!g_devH)g_devH=h;
        DWORD func=(c>>2)&0xFFF;
        if(func>=2600) printf("[IO-PRE] 0x%08X Func=%u in=%u\n",c,func,inSz);
    }
    BOOL ok=g_oDIO(h,c,in,inSz,out,outSz,ret,ov);
    if(dt==0x8088){
        DWORD func=(c>>2)&0xFFF;
        DWORD err=ok?0:GetLastError();
        if(func>=2600) printf("[IO-POST] 0x%08X ok=%d err=%lu\n",c,ok,err);
        if(!ok)SetLastError(err);
    }
    return ok;
}

static bool Patch(HMODULE m,const char*d,const char*f,void*n,void**o){
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
    printf("=== Thread + Event + IOCTL order in start() ===\n\n");
    HMODULE hDll=LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    const char*ds[]={"kernel32.dll","api-ms-win-core-io-l1-1-0.dll",
        "api-ms-win-core-synch-l1-1-0.dll","api-ms-win-core-synch-l1-2-0.dll",
        "api-ms-win-core-processthreads-l1-1-0.dll","api-ms-win-core-processthreads-l1-1-1.dll"};
    for(auto d:ds){
        if(!g_oDIO)Patch(hDll,d,"DeviceIoControl",(void*)HookDIO,(void**)&g_oDIO);
        if(!g_oCT)Patch(hDll,d,"CreateThread",(void*)HookCT,(void**)&g_oCT);
        if(!g_oSE)Patch(hDll,d,"SetEvent",(void*)HookSE,(void**)&g_oSE);
    }
    if(!g_oDIO)g_oDIO=(PFN_DIO)GetProcAddress(GetModuleHandleA("kernel32.dll"),"DeviceIoControl");
    if(!g_oCT)g_oCT=(PFN_CT)GetProcAddress(GetModuleHandleA("kernel32.dll"),"CreateThread");
    if(!g_oSE)g_oSE=(PFN_SE)GetProcAddress(GetModuleHandleA("kernel32.dll"),"SetEvent");
    
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
    
    printf("\n=== Calling official start() ===\n");
    long sr2=((fst)vt[7])(pU);
    printf("start() = %ld\n", sr2);
    
    Sleep(200);
    printf("bSwitch: %ld\n", g_sc);
    
    ((fsp)vt[8])(pU);
    ((fdb)vt[20])(pU);
    pU->Release();
    return 0;
}
