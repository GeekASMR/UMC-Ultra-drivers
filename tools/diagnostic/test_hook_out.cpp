// Hook 官方 DLL: 捕获 output channel 的 MAP_CHANNEL_BUFFER 精确参数
#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#define PI 3.14159265358979

// Hook DeviceIoControl
typedef BOOL(WINAPI*pDevIo)(HANDLE,DWORD,LPVOID,DWORD,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
static pDevIo g_origDevIo=NULL;
static BYTE g_trampoline[64]={};

BOOL WINAPI HookDevIo(HANDLE h,DWORD code,LPVOID inBuf,DWORD inSz,
                       LPVOID outBuf,DWORD outSz,LPDWORD ret,LPOVERLAPPED ov){
    // Only log interesting IOCTLs (not WFB which fires millions of times)
    if(code != 0x808828F4) { // skip WAIT_FOR_BUFFER
        DWORD func=(code>>2)&0xFFF;
        const char* name="?";
        switch(code){
            case 0x80882840: name="SELECT_CHANNEL"; break;
            case 0x808828A0: name="MAP_CHANNEL_BUFFER"; break;
            case 0x808828C8: name="START_STREAMING"; break;
            case 0x808828C0: name="ENABLE_STREAM"; break;
            case 0x808828C4: name="STOP_STREAMING"; break;
            case 0x80882824: name="SET_BUFFER_SIZE"; break;
            case 0x80882880: name="SET_CALLBACKS"; break;
            case 0x80882844: name="DESELECT_CHANNEL"; break;
        }
        printf("[IOCTL] 0x%08X (%s) in=%u out=%u\n", code, name, inSz, outSz);
        if(inBuf && inSz>0 && inSz<=64){
            printf("  IN: ");
            for(DWORD i=0;i<inSz;i++) printf("%02X ",((BYTE*)inBuf)[i]);
            printf("\n");
            if(code==0x808828A0 && inSz==24){ // MAP_CHANNEL_BUFFER
                DWORD chId=*(DWORD*)((BYTE*)inBuf);
                DWORD type=*(DWORD*)((BYTE*)inBuf+4);
                DWORD bsz=*(DWORD*)((BYTE*)inBuf+8);
                DWORD bits=*(DWORD*)((BYTE*)inBuf+12);
                UINT64 addr=*(UINT64*)((BYTE*)inBuf+16);
                printf("  -> chId=0x%X type=0x%X bufSz=%u bits=%u addr=0x%llX\n",
                       chId,type,bsz,bits,addr);
            }
            if(code==0x80882840 && inSz==16){ // SELECT_CHANNEL
                printf("  -> raw16: ");
                for(int i=0;i<4;i++) printf("[%d]=0x%08X ", i, ((DWORD*)inBuf)[i]);
                printf("\n");
            }
        }
    }
    return g_origDevIo(h,code,inBuf,inSz,outBuf,outSz,ret,ov);
}

void installHook(){
    HMODULE hK=GetModuleHandleW(L"kernel32.dll");
    if(!hK) hK=GetModuleHandleW(L"kernelbase.dll");
    g_origDevIo=(pDevIo)GetProcAddress(hK,"DeviceIoControl");
    DWORD old;
    VirtualProtect(g_origDevIo,16,PAGE_EXECUTE_READWRITE,&old);
    memcpy(g_trampoline,g_origDevIo,16);
    // Build trampoline: original bytes + jmp back
    BYTE*t=g_trampoline;
    *(UINT16*)(t+16)=0x25FF; *(UINT32*)(t+18)=0;
    *(UINT64*)(t+22)=(UINT64)((BYTE*)g_origDevIo+16);
    DWORD old2;VirtualProtect(g_trampoline,64,PAGE_EXECUTE_READWRITE,&old2);
    g_origDevIo=(pDevIo)(void*)g_trampoline;
    // Patch original: jmp to hook
    BYTE*p=(BYTE*)GetProcAddress(hK,"DeviceIoControl");
    *(UINT16*)p=0x25FF; *(UINT32*)(p+2)=0;
    *(UINT64*)(p+6)=(UINT64)&HookDevIo;
    VirtualProtect(p,16,old,&old2);
    printf("Hook installed\n");
}

static double g_phase=0, g_phaseInc=2.0*PI*440.0/48000.0;
static long g_bufSize=0;
static void* g_outL[2]={};
static void* g_outR[2]={};
static int g_type=0;
static volatile LONG g_count=0;

void __cdecl bsw(long idx,long dp){
    InterlockedIncrement(&g_count);
    if(!g_outL[idx])return;
    for(long i=0;i<g_bufSize;i++){
        double s=sin(g_phase)*0.3; g_phase+=g_phaseInc;
        ((int*)g_outL[idx])[i]=(int)(s*2147483648.0);
        ((int*)g_outR[idx])[i]=(int)(s*2147483648.0);
    }
}
void __cdecl sr(double r){}
long __cdecl am(long s,long v,void*m,double*o){if(s==1||s==4||s==7)return 1;return 0;}
void* __cdecl bsti(void*p,long idx,long dp){bsw(idx,dp);return p;}

int main(){
    printf("=== Official DLL Hooked Output Test ===\n\n");
    installHook();
    
    HMODULE hDll=LoadLibraryW(L"C:\\Program Files\\Behringer\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    if(!hDll){printf("Load fail\n");return 1;}
    typedef HRESULT(WINAPI*FnGCO)(REFCLSID,REFIID,LPVOID*);
    auto pGCO=(FnGCO)GetProcAddress(hDll,"DllGetClassObject");
    const CLSID clsid={0x0351302f,0xb1f1,0x4a5d,{0x86,0x13,0x78,0x7f,0x77,0xc2,0x0e,0xa4}};
    IClassFactory*pF; pGCO(clsid,IID_IClassFactory,(void**)&pF);
    IUnknown*pU; pF->CreateInstance(NULL,clsid,(void**)&pU); pF->Release();
    void**vt=*(void***)pU;
    typedef BOOL(__thiscall*fn_init)(void*,void*);
    typedef long(__thiscall*fn_getCh)(void*,long*,long*);
    typedef long(__thiscall*fn_getBuf)(void*,long*,long*,long*,long*);
    typedef long(__thiscall*fn_start)(void*);
    typedef long(__thiscall*fn_stop)(void*);
    typedef long(__thiscall*fn_dispose)(void*);
    
    printf("\n--- init ---\n");
    ((fn_init)vt[3])(pU,NULL);
    long nIn,nOut; ((fn_getCh)vt[9])(pU,&nIn,&nOut);
    long minB,maxB,prefB,gran; ((fn_getBuf)vt[11])(pU,&minB,&maxB,&prefB,&gran);
    g_bufSize=prefB; g_type=18;
    
    struct CB{void*a;void*b;void*c;void*d;};
    CB cbs={(void*)bsw,(void*)sr,(void*)am,(void*)bsti};
    struct BI{long isInput;long channelNum;void*buffers[2];};
    BI bi[2]={};
    bi[0].isInput=0;bi[0].channelNum=0;
    bi[1].isInput=0;bi[1].channelNum=1;
    typedef long(__thiscall*fn_cb)(void*,BI*,long,long,CB*);
    
    printf("\n--- createBuffers (output only) ---\n");
    ((fn_cb)vt[19])(pU,bi,2,prefB,&cbs);
    g_outL[0]=bi[0].buffers[0];g_outL[1]=bi[0].buffers[1];
    g_outR[0]=bi[1].buffers[0];g_outR[1]=bi[1].buffers[1];
    
    printf("\n--- start ---\n");
    g_count=0;
    ((fn_start)vt[7])(pU);
    Sleep(1000);
    printf("\n[1s] callbacks=%ld\n",g_count);
    
    printf("\n--- stop ---\n");
    ((fn_stop)vt[8])(pU);
    ((fn_dispose)vt[20])(pU);
    pU->Release();
    printf("Done!\n");
    return 0;
}
