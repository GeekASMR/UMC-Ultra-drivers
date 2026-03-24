// 用官方 DLL 输出正弦波
#include <windows.h>
#include <stdio.h>
#include <math.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#define PI 3.14159265358979

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
        if(g_type==18){((int*)g_outL[idx])[i]=(int)(s*2147483648.0);((int*)g_outR[idx])[i]=(int)(s*2147483648.0);}
        else{((float*)g_outL[idx])[i]=(float)s;((float*)g_outR[idx])[i]=(float)s;}
    }
}
void __cdecl sr(double r){}
long __cdecl am(long s,long v,void*m,double*o){if(s==1||s==4||s==7)return 1;return 0;}
void* __cdecl bsti(void*p,long idx,long dp){bsw(idx,dp);return p;}

int main(){
    printf("=== Official DLL Sine Test ===\n\n");
    HMODULE hDll=LoadLibraryW(L"C:\\Program Files\\Behringer\\UMC_Audio_Driver\\x64\\umc_audioasio_x64.dll");
    if(!hDll){printf("Load failed\n");return 1;}
    printf("Official DLL loaded\n");
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
    typedef long(__thiscall*fn_getChInfo)(void*,void*);
    if(!((fn_init)vt[3])(pU,NULL)){printf("init fail\n");return 1;}
    long nIn,nOut; ((fn_getCh)vt[9])(pU,&nIn,&nOut);
    long minB,maxB,prefB,gran; ((fn_getBuf)vt[11])(pU,&minB,&maxB,&prefB,&gran);
    g_bufSize=prefB;
    struct CI{long ch;long isInput;long isActive;long group;long type;char name[32];};
    CI ci={};ci.ch=0;ci.isInput=0;
    ((fn_getChInfo)vt[18])(pU,&ci);
    g_type=ci.type;
    printf("Ch:%ld/%ld Buf:%ld OutType:%ld('%s')\n",nIn,nOut,prefB,ci.type,ci.name);
    struct CB{void*a;void*b;void*c;void*d;};
    CB cbs={(void*)bsw,(void*)sr,(void*)am,(void*)bsti};
    struct BI{long isInput;long channelNum;void*buffers[2];};
    BI bi[2]={};
    bi[0].isInput=0;bi[0].channelNum=0;
    bi[1].isInput=0;bi[1].channelNum=1;
    typedef long(__thiscall*fn_cb)(void*,BI*,long,long,CB*);
    long r=((fn_cb)vt[19])(pU,bi,2,prefB,&cbs);
    printf("createBuffers:%ld\n",r);
    g_outL[0]=bi[0].buffers[0];g_outL[1]=bi[0].buffers[1];
    g_outR[0]=bi[1].buffers[0];g_outR[1]=bi[1].buffers[1];
    printf("OutL:%p/%p OutR:%p/%p\n",g_outL[0],g_outL[1],g_outR[0],g_outR[1]);
    g_count=0;
    r=((fn_start)vt[7])(pU);
    printf("start:%ld\n\nPlaying 440Hz for 3s...\n",r);
    for(int t=0;t<6;t++){Sleep(500);printf("  [%dms] cb=%ld\n",(t+1)*500,g_count);}
    ((fn_stop)vt[8])(pU);
    ((fn_dispose)vt[20])(pU);
    pU->Release();
    printf("\nDone!\n");
    return 0;
}
