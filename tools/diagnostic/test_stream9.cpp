// test_stream9 - 完整直连: START + ENABLE_STREAM + WAIT_FOR_BUFFER
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#pragma comment(lib, "setupapi.lib")
static const GUID G={0x215A80EF,0x69BD,0x4D85,{0xAC,0x71,0x0C,0x6E,0xA6,0xE6,0xBE,0x17}};

static HANDLE g_ovEvt=NULL;
static OVERLAPPED g_ov={};
bool io(HANDLE h,DWORD code,void*in,DWORD inSz,void*out=NULL,DWORD outSz=0){
    memset(&g_ov,0,sizeof(g_ov));g_ov.hEvent=g_ovEvt;DWORD br=0;
    BOOL ok=DeviceIoControl(h,code,in,inSz,out,outSz,&br,&g_ov);
    if(!ok&&GetLastError()==ERROR_IO_PENDING) ok=GetOverlappedResult(h,&g_ov,&br,TRUE);
    return ok!=FALSE;
}

static volatile LONG g_count=0;
static volatile bool g_run=false;
static HANDLE g_hDev=NULL;

DWORD WINAPI PollThread(LPVOID){
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    HANDLE evt=CreateEventW(NULL,TRUE,FALSE,NULL);
    while(g_run){
        OVERLAPPED wov={}; wov.hEvent=evt;
        DWORD br=0;
        BOOL ok=DeviceIoControl(g_hDev, 0x808828F4, NULL,0,NULL,0,&br,&wov);
        if(!ok&&GetLastError()==ERROR_IO_PENDING)
            ok=GetOverlappedResult(g_hDev,&wov,&br,TRUE);
        if(ok) InterlockedIncrement(&g_count);
    }
    CloseHandle(evt);
    return 0;
}

int main(){
    printf("=== Stream v9 - START + ENABLE + WAIT ===\n\n");
    
    HANDLE evt_auto[3],evt_man[6];
    for(int i=0;i<3;i++) evt_auto[i]=CreateEventW(NULL,FALSE,FALSE,NULL);
    for(int i=0;i<6;i++) evt_man[i]=CreateEventW(NULL,TRUE,FALSE,NULL);
    
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
    
    DWORD br;BYTE tmp[1040];
    io(g_hDev,0x80882004,NULL,0,tmp,24);
    io(g_hDev,0x80882820,NULL,0,tmp,4);
    io(g_hDev,0x808820C4,NULL,0,tmp,1040);
    DWORD mode=0;io(g_hDev,0x80882804,&mode,4);
    
    BYTE*ctrl=(BYTE*)VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    BYTE cb[32]={};
    *(UINT64*)&cb[0]=(UINT64)(uintptr_t)ctrl;
    *(UINT64*)&cb[8]=(UINT64)(uintptr_t)evt_auto[0];
    *(UINT64*)&cb[16]=(UINT64)(uintptr_t)evt_auto[1];
    *(UINT64*)&cb[24]=(UINT64)(uintptr_t)evt_auto[2];
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
    
    // SELECT + MAP In0
    io(g_hDev,0x80882840,&chList[4],16);
    BYTE mp[24]={};
    *(DWORD*)&mp[0]=*(DWORD*)&chList[12];*(DWORD*)&mp[4]=*(DWORD*)&chList[16];
    *(DWORD*)&mp[8]=bufBytes;*(DWORD*)&mp[12]=32;
    *(UINT64*)&mp[16]=(UINT64)(uintptr_t)dma[0];
    io(g_hDev,0x808828A0,mp,24);
    
    // SELECT + MAP Out0
    io(g_hDev,0x80882840,&chList[4104],16);
    *(DWORD*)&mp[0]=*(DWORD*)&chList[4112];*(DWORD*)&mp[4]=*(DWORD*)&chList[4116];
    *(UINT64*)&mp[16]=(UINT64)(uintptr_t)dma[1];
    io(g_hDev,0x808828A0,mp,24);
    
    printf("ctrl=%p dma=%p/%p\n", ctrl, dma[0], dma[1]);
    
    // === 关键序列 ===
    // 1. START_STREAMING
    io(g_hDev, 0x808828C8, NULL, 0);
    printf("START: OK\n");
    
    // 2. ENABLE_STREAM (0x808828C0) with buffer size!
    io(g_hDev, 0x808828C0, &bufSz, 4);
    printf("ENABLE(bufSz=%u): OK\n", bufSz);
    
    // 3. Start poll thread
    g_run=true;
    HANDLE ht=CreateThread(NULL,0,PollThread,NULL,0,NULL);
    
    printf("Running 2s...\n");
    Sleep(2000);
    
    printf("\n=== RESULTS ===\n");
    printf("WAIT_FOR_BUFFER count: %ld (%.1f Hz, expected 375)\n", g_count, g_count/2.0);
    
    BYTE*d=dma[0];
    printf("In0 hex: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
           d[3],d[2],d[1],d[0], d[7],d[6],d[5],d[4],
           d[11],d[10],d[9],d[8], d[15],d[14],d[13],d[12]);
    
    // Check buf[1] (second half of double buffer)
    d=dma[0]+bufBytes;
    printf("In0[1]: %02X%02X%02X%02X %02X%02X%02X%02X\n",
           d[3],d[2],d[1],d[0], d[7],d[6],d[5],d[4]);
    
    printf("ctrl[0..7]: %02X%02X%02X%02X %02X%02X%02X%02X\n",
           ctrl[3],ctrl[2],ctrl[1],ctrl[0], ctrl[7],ctrl[6],ctrl[5],ctrl[4]);
    
    // STOP
    g_run=false; CancelIo(g_hDev);
    WaitForSingleObject(ht,2000);
    io(g_hDev, 0x808828C4, NULL, 0);
    
    // DESELECT
    io(g_hDev, 0x80882844, &chList[4], 16);
    io(g_hDev, 0x80882844, &chList[4104], 16);
    
    printf("Done!\n");
    VirtualFree(dma[0],0,MEM_RELEASE);VirtualFree(dma[1],0,MEM_RELEASE);
    VirtualFree(ctrl,0,MEM_RELEASE);CloseHandle(g_hDev);
    return 0;
}
