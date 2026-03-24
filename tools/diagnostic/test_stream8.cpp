// test_stream8 - 使用 WAIT_FOR_BUFFER IOCTL 轮询
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#pragma comment(lib, "setupapi.lib")
static const GUID G={0x215A80EF,0x69BD,0x4D85,{0xAC,0x71,0x0C,0x6E,0xA6,0xE6,0xBE,0x17}};

#define TUSB_WAIT_FOR_BUFFER 0x808828F4
#define TUSB_START           0x808828C8
#define TUSB_STOP            0x808828C4

static HANDLE g_ovEvt=NULL;
static OVERLAPPED g_ov={};

bool io(HANDLE h,DWORD code,void*in,DWORD inSz,void*out=NULL,DWORD outSz=0,DWORD*pR=NULL){
    memset(&g_ov,0,sizeof(g_ov));g_ov.hEvent=g_ovEvt;
    DWORD br=0;BOOL ok=DeviceIoControl(h,code,in,inSz,out,outSz,&br,&g_ov);
    if(!ok&&GetLastError()==ERROR_IO_PENDING) ok=GetOverlappedResult(h,&g_ov,&br,TRUE);
    if(pR)*pR=br;return ok!=FALSE;
}

static volatile bool g_running=false;
static volatile LONG g_switchCount=0;

struct StreamCtx { HANDLE hDev; BYTE* dma[2]; DWORD bufBytes; };

DWORD WINAPI AudioThread(LPVOID param){
    StreamCtx* ctx=(StreamCtx*)param;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    
    // Dedicated OVERLAPPED for this thread
    OVERLAPPED ov={};
    ov.hEvent = CreateEventW(NULL,TRUE,FALSE,NULL);
    
    while(g_running){
        memset(&ov,0,sizeof(ov));
        ov.hEvent = ov.hEvent; // preserve
        
        // WAIT_FOR_BUFFER: blocks until next buffer period
        DWORD br=0;
        BOOL ok=DeviceIoControl(ctx->hDev, TUSB_WAIT_FOR_BUFFER, NULL,0,NULL,0,&br,&ov);
        if(!ok){
            DWORD err=GetLastError();
            if(err==ERROR_IO_PENDING) ok=GetOverlappedResult(ctx->hDev,&ov,&br,TRUE);
            else { break; } // error
        }
        if(ok) InterlockedIncrement(&g_switchCount);
    }
    return 0;
}

int main(){
    printf("=== Stream v8 - WAIT_FOR_BUFFER Poll ===\n\n");
    
    HANDLE evt_auto[3], evt_man[6];
    for(int i=0;i<3;i++) evt_auto[i]=CreateEventW(NULL,FALSE,FALSE,NULL);
    for(int i=0;i<6;i++) evt_man[i]=CreateEventW(NULL,TRUE,FALSE,NULL);
    
    HDEVINFO di=SetupDiGetClassDevsW(&G,NULL,NULL,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA id={};id.cbSize=sizeof(id);
    SetupDiEnumDeviceInterfaces(di,NULL,&G,0,&id);
    DWORD rs;SetupDiGetDeviceInterfaceDetailW(di,&id,NULL,0,&rs,NULL);
    auto*dt=(SP_DEVICE_INTERFACE_DETAIL_DATA_W*)calloc(1,rs);
    dt->cbSize=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    SetupDiGetDeviceInterfaceDetailW(di,&id,dt,rs,NULL,NULL);
    HANDLE h=CreateFileW(dt->DevicePath,GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
    free(dt);SetupDiDestroyDeviceInfoList(di);
    g_ovEvt=CreateEventW(NULL,TRUE,FALSE,NULL);
    
    DWORD br;BYTE tmp[1040];
    io(h,0x80882004,NULL,0,tmp,24);
    io(h,0x80882820,NULL,0,tmp,4);
    io(h,0x808820C4,NULL,0,tmp,1040);
    DWORD mode=0;io(h,0x80882804,&mode,4);
    
    BYTE*ctrl=(BYTE*)VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    BYTE cb[32]={};
    *(UINT64*)&cb[0]=(UINT64)(uintptr_t)ctrl;
    *(UINT64*)&cb[8]=(UINT64)(uintptr_t)evt_auto[0];
    *(UINT64*)&cb[16]=(UINT64)(uintptr_t)evt_auto[1];
    *(UINT64*)&cb[24]=(UINT64)(uintptr_t)evt_auto[2];
    io(h,0x80882880,cb,32);
    
    BYTE config[292];io(h,0x80882808,NULL,0,config,292);
    BYTE chList[8200]={};io(h,0x8088280C,NULL,0,chList,8200,&br);
    DWORD numIn=*(DWORD*)&chList[0], numOut=*(DWORD*)&chList[4100];
    for(DWORD i=0;i<numIn;i++){BYTE info[108];io(h,0x80882810,&chList[4+i*16],16,info,108);}
    for(DWORD i=0;i<numOut;i++){BYTE info[108];io(h,0x80882810,&chList[4104+i*16],16,info,108);}
    io(h,0x80882808,NULL,0,config,292);
    
    DWORD bufSz=128,bufBytes=bufSz*4;
    io(h,0x80882824,&bufSz,4);
    
    BYTE*dma[2];
    for(int i=0;i<2;i++) dma[i]=(BYTE*)VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    
    io(h,0x80882840,&chList[4],16);
    BYTE mp[24]={};
    *(DWORD*)&mp[0]=*(DWORD*)&chList[12];*(DWORD*)&mp[4]=*(DWORD*)&chList[16];
    *(DWORD*)&mp[8]=bufBytes;*(DWORD*)&mp[12]=32;
    *(UINT64*)&mp[16]=(UINT64)(uintptr_t)dma[0];
    io(h,0x808828A0,mp,24);
    
    io(h,0x80882840,&chList[4104],16);
    *(DWORD*)&mp[0]=*(DWORD*)&chList[4112];*(DWORD*)&mp[4]=*(DWORD*)&chList[4116];
    *(UINT64*)&mp[16]=(UINT64)(uintptr_t)dma[1];
    io(h,0x808828A0,mp,24);
    
    printf("Ch: %u/%u  dma=%p/%p\n", numIn, numOut, dma[0], dma[1]);
    
    // START
    io(h, TUSB_START, NULL, 0);
    printf("Started!\n");
    
    // Launch poll thread with WAIT_FOR_BUFFER
    g_running = true;
    StreamCtx ctx = {h, {dma[0],dma[1]}, bufBytes};
    HANDLE hThread = CreateThread(NULL, 0, AudioThread, &ctx, 0, NULL);
    
    Sleep(2000);
    
    printf("\n=== Results (2s) ===\n");
    printf("WAIT_FOR_BUFFER count: %ld (%.1f Hz, expected 750)\n", g_switchCount, g_switchCount/2.0);
    
    // Check data
    printf("In0 hex: %02X%02X%02X%02X %02X%02X%02X%02X\n",
           dma[0][3],dma[0][2],dma[0][1],dma[0][0],
           dma[0][7],dma[0][6],dma[0][5],dma[0][4]);
    printf("ctrl[0..7]: %02X%02X%02X%02X %02X%02X%02X%02X\n",
           ctrl[3],ctrl[2],ctrl[1],ctrl[0], ctrl[7],ctrl[6],ctrl[5],ctrl[4]);
    
    // STOP
    g_running = false;
    // Cancel the pending WAIT_FOR_BUFFER
    CancelIo(h);
    WaitForSingleObject(hThread, 2000);
    io(h, TUSB_STOP, NULL, 0);
    
    printf("Done!\n");
    VirtualFree(dma[0],0,MEM_RELEASE); VirtualFree(dma[1],0,MEM_RELEASE);
    VirtualFree(ctrl,0,MEM_RELEASE);
    CloseHandle(h);
    return 0;
}
