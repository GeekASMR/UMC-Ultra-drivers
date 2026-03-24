// test_stream10 - 检测 buffer switch
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdint.h>
#pragma comment(lib, "setupapi.lib")
static const GUID G={0x215A80EF,0x69BD,0x4D85,{0xAC,0x71,0x0C,0x6E,0xA6,0xE6,0xBE,0x17}};
static HANDLE g_ovEvt=NULL; static OVERLAPPED g_ov={};
bool io(HANDLE h,DWORD c,void*in,DWORD iSz,void*out=NULL,DWORD oSz=0){
    memset(&g_ov,0,sizeof(g_ov));g_ov.hEvent=g_ovEvt;DWORD br=0;
    BOOL ok=DeviceIoControl(h,c,in,iSz,out,oSz,&br,&g_ov);
    if(!ok&&GetLastError()==ERROR_IO_PENDING) ok=GetOverlappedResult(h,&g_ov,&br,TRUE);
    return ok!=FALSE;
}
static volatile LONG g_count=0;
static volatile bool g_run=false;
static HANDLE g_hDev=NULL;
static volatile DWORD* g_ctrlPtr=NULL;

DWORD WINAPI Poll(LPVOID){
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_TIME_CRITICAL);
    HANDLE evt=CreateEventW(NULL,TRUE,FALSE,NULL);
    DWORD last=*g_ctrlPtr;
    while(g_run){
        OVERLAPPED wov={};wov.hEvent=evt;DWORD br=0;
        BOOL ok=DeviceIoControl(g_hDev,0x808828F4,NULL,0,NULL,0,&br,&wov);
        if(!ok&&GetLastError()==ERROR_IO_PENDING) ok=GetOverlappedResult(g_hDev,&wov,&br,TRUE);
        DWORD cur=*g_ctrlPtr;
        if(cur!=last){InterlockedIncrement(&g_count);last=cur;}
    }
    CloseHandle(evt); return 0;
}

int main(){
    printf("=== v10 ===\n");
    HANDLE ea[3],em[6];
    for(int i=0;i<3;i++) ea[i]=CreateEventW(NULL,FALSE,FALSE,NULL);
    for(int i=0;i<6;i++) em[i]=CreateEventW(NULL,TRUE,FALSE,NULL);
    HDEVINFO di=SetupDiGetClassDevsW(&G,NULL,NULL,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA id={};id.cbSize=sizeof(id);
    SetupDiEnumDeviceInterfaces(di,NULL,&G,0,&id);
    DWORD rs;SetupDiGetDeviceInterfaceDetailW(di,&id,NULL,0,&rs,NULL);
    auto*dt=(SP_DEVICE_INTERFACE_DETAIL_DATA_W*)calloc(1,rs);
    dt->cbSize=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    SetupDiGetDeviceInterfaceDetailW(di,&id,dt,rs,NULL,NULL);
    g_hDev=CreateFileW(dt->DevicePath,GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
    free(dt);SetupDiDestroyDeviceInfoList(di);
    g_ovEvt=CreateEventW(NULL,TRUE,FALSE,NULL);
    
    BYTE tmp[1040];
    io(g_hDev,0x80882004,NULL,0,tmp,24);io(g_hDev,0x80882820,NULL,0,tmp,4);
    io(g_hDev,0x808820C4,NULL,0,tmp,1040);
    DWORD mode=0;io(g_hDev,0x80882804,&mode,4);
    BYTE*ctrl=(BYTE*)VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    g_ctrlPtr=(volatile DWORD*)ctrl;
    BYTE cb[32]={};
    *(UINT64*)&cb[0]=(UINT64)(uintptr_t)ctrl;
    *(UINT64*)&cb[8]=(UINT64)(uintptr_t)ea[0];
    *(UINT64*)&cb[16]=(UINT64)(uintptr_t)ea[1];
    *(UINT64*)&cb[24]=(UINT64)(uintptr_t)ea[2];
    io(g_hDev,0x80882880,cb,32);
    BYTE config[292];io(g_hDev,0x80882808,NULL,0,config,292);
    BYTE chList[8200]={};io(g_hDev,0x8088280C,NULL,0,chList,8200);
    DWORD numIn=*(DWORD*)&chList[0],numOut=*(DWORD*)&chList[4100];
    for(DWORD i=0;i<numIn;i++){BYTE info[108];io(g_hDev,0x80882810,&chList[4+i*16],16,info,108);}
    for(DWORD i=0;i<numOut;i++){BYTE info[108];io(g_hDev,0x80882810,&chList[4104+i*16],16,info,108);}
    io(g_hDev,0x80882808,NULL,0,config,292);
    
    DWORD bufSz=128,bufBytes=bufSz*4;
    io(g_hDev,0x80882824,&bufSz,4);
    BYTE*dma[2];
    for(int i=0;i<2;i++) dma[i]=(BYTE*)VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    io(g_hDev,0x80882840,&chList[4],16);
    BYTE mp[24]={};
    *(DWORD*)&mp[0]=*(DWORD*)&chList[12];*(DWORD*)&mp[4]=*(DWORD*)&chList[16];
    *(DWORD*)&mp[8]=bufBytes;*(DWORD*)&mp[12]=32;*(UINT64*)&mp[16]=(UINT64)(uintptr_t)dma[0];
    io(g_hDev,0x808828A0,mp,24);
    io(g_hDev,0x80882840,&chList[4104],16);
    *(DWORD*)&mp[0]=*(DWORD*)&chList[4112];*(DWORD*)&mp[4]=*(DWORD*)&chList[4116];
    *(UINT64*)&mp[16]=(UINT64)(uintptr_t)dma[1];
    io(g_hDev,0x808828A0,mp,24);
    
    io(g_hDev,0x808828C8,NULL,0); // START
    io(g_hDev,0x808828C0,&bufSz,4); // ENABLE
    g_run=true;
    HANDLE ht=CreateThread(NULL,0,Poll,NULL,0,NULL);
    
    // Monitor
    DWORD prev=0;
    for(int i=0;i<5;i++){
        Sleep(400);
        DWORD cur=*g_ctrlPtr;
        int32_t* s=(int32_t*)dma[0];
        float f0=(float)s[0]/2147483648.0f;
        printf("[%dms] ctrl=%u (+%d) bs=%ld | In0=%.4f\n",
               (i+1)*400, cur, (int)(cur-prev), g_count, f0);
        prev=cur;
    }
    
    printf("\nTotal bSwitch: %ld (%.1f Hz)\n", g_count, g_count/2.0);
    g_run=false;CancelIo(g_hDev);WaitForSingleObject(ht,2000);
    io(g_hDev,0x808828C4,NULL,0);
    io(g_hDev,0x80882844,&chList[4],16);
    io(g_hDev,0x80882844,&chList[4104],16);
    VirtualFree(dma[0],0,MEM_RELEASE);VirtualFree(dma[1],0,MEM_RELEASE);
    VirtualFree(ctrl,0,MEM_RELEASE);CloseHandle(g_hDev);
    printf("Done!\n");return 0;
}
