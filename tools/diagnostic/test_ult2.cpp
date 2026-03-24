// 终极测试2: 用官方DLL的init+createBuffers, 然后自己调 START + WAIT_FOR_BUFFER
// 关键: 用官方DLL的设备句柄和OVERLAPPED结构
#include <windows.h>
#include <stdio.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

typedef BOOL(WINAPI*PFN_DIO)(HANDLE,DWORD,LPVOID,DWORD,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
static PFN_DIO g_oDIO=NULL;
static HANDLE g_devH=NULL;
static LPOVERLAPPED g_dllOv=NULL; // 官方DLL的OVERLAPPED指针

BOOL WINAPI Hook(HANDLE h,DWORD c,LPVOID in,DWORD inSz,LPVOID out,DWORD outSz,LPDWORD ret,LPOVERLAPPED ov){
    BOOL ok=g_oDIO(h,c,in,inSz,out,outSz,ret,ov);
    if(((c>>16)&0xFFFF)==0x8088){
        if(!g_devH){g_devH=h; g_dllOv=ov;}
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
    printf("=== Ultimate Test 2: Official init+createBuffers, OUR start+poll ===\n\n");
    HMODULE hDll=LoadLibraryW(L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    const char*ds[]={"kernel32.dll","api-ms-win-core-io-l1-1-0.dll"};
    for(auto d:ds){if(!g_oDIO)Patch(hDll,d,"DeviceIoControl",(void*)Hook,(void**)&g_oDIO);}
    if(!g_oDIO)g_oDIO=(PFN_DIO)GetProcAddress(GetModuleHandleA("kernel32.dll"),"DeviceIoControl");
    
    typedef HRESULT(WINAPI*FnGCO)(REFCLSID,REFIID,LPVOID*);
    const CLSID cls={0x0351302f,0xb1f1,0x4a5d,{0x86,0x13,0x78,0x7f,0x77,0xc2,0x0e,0xa4}};
    IClassFactory*pF;((FnGCO)GetProcAddress(hDll,"DllGetClassObject"))(cls,IID_IClassFactory,(void**)&pF);
    IUnknown*pU;pF->CreateInstance(NULL,cls,(void**)&pU);pF->Release();
    void**vt=*(void***)pU;
    
    typedef BOOL(__thiscall*fi)(void*,void*);
    typedef long(__thiscall*fgc)(void*,long*,long*);
    typedef long(__thiscall*fgb)(void*,long*,long*,long*,long*);
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
    
    printf("DevH=%p  DllOV=%p\n", g_devH, g_dllOv);
    printf("Bufs: in=%p out=%p\n", bi[0].buf[0], bi[1].buf[0]);
    
    // 自己调 START_STREAMING 用官方DLL的句柄
    printf("\n--- OUR START_STREAMING ---\n");
    OVERLAPPED ourOv={}; ourOv.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);
    DWORD br;
    BOOL ok = g_oDIO(g_devH, 0x808828C8, NULL,0,NULL,0,&br,&ourOv);
    if(!ok&&GetLastError()==ERROR_IO_PENDING) ok=GetOverlappedResult(g_devH,&ourOv,&br,TRUE);
    printf("START: %s\n", ok?"OK":"FAIL");
    
    // 自己的 WAIT_FOR_BUFFER 轮询线程
    printf("Polling WAIT_FOR_BUFFER for 1s...\n");
    DWORD t0=GetTickCount();
    int waitCount=0;
    OVERLAPPED waitOv={}; waitOv.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);
    
    while(GetTickCount()-t0 < 1000){
        memset(&waitOv,0,sizeof(waitOv));
        waitOv.hEvent=waitOv.hEvent; // oops, this got zeroed
        // Fix: save hEvent
        break; // need to fix
    }
    
    // Use a proper event
    HANDLE waitEvt = CreateEventW(NULL,TRUE,FALSE,NULL);
    t0=GetTickCount();
    while(GetTickCount()-t0 < 1000){
        OVERLAPPED wov={};
        wov.hEvent = waitEvt;
        ResetEvent(waitEvt);
        br=0;
        ok = g_oDIO(g_devH, 0x808828F4, NULL,0,NULL,0,&br,&wov);
        if(!ok){
            DWORD err=GetLastError();
            if(err==ERROR_IO_PENDING){
                // This should BLOCK until buffer ready
                ok=GetOverlappedResult(g_devH,&wov,&br,TRUE);
                if(ok) waitCount++;
            } else {
                printf("WAIT_FOR_BUFFER error: %lu\n", err);
                break;
            }
        } else {
            waitCount++; // completed immediately
        }
        if(waitCount<=3) printf("  WAIT returned (count=%d, elapsed=%lums)\n", waitCount, GetTickCount()-t0);
        if(waitCount > 1000) { printf("  Not blocking! (1000 immediate returns)\n"); break; }
    }
    
    printf("WAIT count: %d in %lums (%.1f Hz)\n", waitCount, GetTickCount()-t0, waitCount*1000.0/(GetTickCount()-t0));
    printf("bufferSwitch: %ld\n", g_sc);
    
    // Check data
    if(bi[0].buf[0]){
        BYTE*d=(BYTE*)bi[0].buf[0];
        printf("In0: %02X%02X%02X%02X %02X%02X%02X%02X\n",d[3],d[2],d[1],d[0],d[7],d[6],d[5],d[4]);
    }
    
    // STOP
    g_oDIO(g_devH, 0x808828C4, NULL,0,NULL,0,&br,&ourOv);
    ((fdb)vt[20])(pU);
    pU->Release();
    printf("Done!\n");
    return 0;
}
