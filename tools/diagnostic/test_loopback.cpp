// Loopback: 直接复制 input DMA -> output DMA，验证 output 路径
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#pragma comment(lib, "setupapi.lib")
static const GUID G={0x215A80EF,0x69BD,0x4D85,{0xAC,0x71,0x0C,0x6E,0xA6,0xE6,0xBE,0x17}};
static HANDLE g_ovEvt=NULL; static OVERLAPPED g_ov={};
#define PI 3.14159265358979
bool io(HANDLE h,DWORD c,void*in,DWORD iSz,void*out=NULL,DWORD oSz=0){
    memset(&g_ov,0,sizeof(g_ov));g_ov.hEvent=g_ovEvt;DWORD br=0;
    BOOL ok=DeviceIoControl(h,c,in,iSz,out,oSz,&br,&g_ov);
    if(!ok&&GetLastError()==ERROR_IO_PENDING) ok=GetOverlappedResult(h,&g_ov,&br,TRUE);
    return ok!=FALSE;
}
int main(){
    printf("=== Loopback + Sine Test ===\n");
    printf("Phase 1: Sine on BOTH halves every cycle\n");
    printf("Phase 2: Loopback (In1->Out1/Out2)\n\n");
    
    HANDLE ea[3];
    for(int i=0;i<3;i++) ea[i]=CreateEventW(NULL,FALSE,FALSE,NULL);
    HDEVINFO di=SetupDiGetClassDevsW(&G,NULL,NULL,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA id={};id.cbSize=sizeof(id);
    SetupDiEnumDeviceInterfaces(di,NULL,&G,0,&id);
    DWORD rs;SetupDiGetDeviceInterfaceDetailW(di,&id,NULL,0,&rs,NULL);
    auto*dt=(SP_DEVICE_INTERFACE_DETAIL_DATA_W*)calloc(1,rs);
    dt->cbSize=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    SetupDiGetDeviceInterfaceDetailW(di,&id,dt,rs,NULL,NULL);
    HANDLE hD=CreateFileW(dt->DevicePath,GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
    free(dt);SetupDiDestroyDeviceInfoList(di);
    g_ovEvt=CreateEventW(NULL,TRUE,FALSE,NULL);
    
    BYTE tmp[1040];
    io(hD,0x80882004,NULL,0,tmp,24); io(hD,0x80882820,NULL,0,tmp,4);
    io(hD,0x808820C4,NULL,0,tmp,1040);
    DWORD mode=0;io(hD,0x80882804,&mode,4);
    BYTE*ctrl=(BYTE*)VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    BYTE cb[32]={};
    *(UINT64*)&cb[0]=(UINT64)(uintptr_t)ctrl;
    *(UINT64*)&cb[8]=(UINT64)(uintptr_t)ea[0];
    *(UINT64*)&cb[16]=(UINT64)(uintptr_t)ea[1];
    *(UINT64*)&cb[24]=(UINT64)(uintptr_t)ea[2];
    io(hD,0x80882880,cb,32);
    BYTE config[292];io(hD,0x80882808,NULL,0,config,292);
    BYTE chList[8200]={};io(hD,0x8088280C,NULL,0,chList,8200);
    DWORD numIn=*(DWORD*)&chList[0],numOut=*(DWORD*)&chList[4100];
    for(DWORD i=0;i<numIn;i++){BYTE x[108];io(hD,0x80882810,&chList[4+i*16],16,x,108);}
    for(DWORD i=0;i<numOut;i++){BYTE x[108];io(hD,0x80882810,&chList[4104+i*16],16,x,108);}
    io(hD,0x80882808,NULL,0,config,292);
    
    DWORD bufSz=128,bufBytes=bufSz*4;
    io(hD,0x80882824,&bufSz,4);
    
    // Allocate DMA buffers
    BYTE*dmaIn0=(BYTE*)VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    BYTE*dmaOut0=(BYTE*)VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    BYTE*dmaOut1=(BYTE*)VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    
    // SELECT + MAP In0
    io(hD,0x80882840,&chList[4],16);
    BYTE mp[24]={};
    *(DWORD*)&mp[0]=*(DWORD*)&chList[12];*(DWORD*)&mp[4]=*(DWORD*)&chList[16];
    *(DWORD*)&mp[8]=bufBytes;*(DWORD*)&mp[12]=32;*(UINT64*)&mp[16]=(UINT64)(uintptr_t)dmaIn0;
    io(hD,0x808828A0,mp,24);
    
    // SELECT + MAP Out0
    io(hD,0x80882840,&chList[4104],16);
    *(DWORD*)&mp[0]=*(DWORD*)&chList[4112];*(DWORD*)&mp[4]=*(DWORD*)&chList[4116];
    *(UINT64*)&mp[16]=(UINT64)(uintptr_t)dmaOut0;
    io(hD,0x808828A0,mp,24);
    
    // SELECT + MAP Out1
    io(hD,0x80882840,&chList[4120],16);
    *(DWORD*)&mp[0]=*(DWORD*)&chList[4128];*(DWORD*)&mp[4]=*(DWORD*)&chList[4132];
    *(UINT64*)&mp[16]=(UINT64)(uintptr_t)dmaOut1;
    io(hD,0x808828A0,mp,24);
    
    io(hD,0x808828C8,NULL,0); // START
    io(hD,0x808828C0,&bufSz,4); // ENABLE
    
    // === Phase 1: Sine wave, write full 64KB (both halves), 2 sec ===
    printf("Phase 1: Sine wave on FULL DMA buffer (2s)...\n");
    printf("  Listen for clean 440Hz tone\n");
    double phase = 0;
    double phaseInc = 2.0 * PI * 440.0 / 48000.0;
    DWORD lastCtr = *(volatile DWORD*)ctrl;
    
    // Pre-fill entire DMA area with sine
    for(DWORD i=0; i < 0x10000/4; i++){
        double s = sin(phase) * 0.3;
        int32_t val = (int32_t)(s * 2147483648.0);
        ((int32_t*)dmaOut0)[i] = val;
        ((int32_t*)dmaOut1)[i] = val;
        phase += phaseInc;
    }
    
    // Keep refilling while playing
    for(int c=0;c<750;c++){
        WaitForSingleObject(ea[0], 100);
        DWORD curCtr = *(volatile DWORD*)ctrl;
        if(curCtr == lastCtr) { c--; continue; }
        lastCtr = curCtr;
        
        // Refill the entire buffer continuously
        for(DWORD i=0; i < bufSz*2; i++){
            double s = sin(phase) * 0.3;
            int32_t val = (int32_t)(s * 2147483648.0);
            ((int32_t*)dmaOut0)[i] = val;
            ((int32_t*)dmaOut1)[i] = val;
            phase += phaseInc;
        }
        if((c+1) % 375 == 0) printf("  %d sec\n", (c+1)/375);
    }
    
    // === Phase 2: Loopback ===
    printf("\nPhase 2: Loopback In0 -> Out0/Out1 (2s)...\n");
    printf("  Speak into mic or make noise\n");
    for(int c=0;c<750;c++){
        WaitForSingleObject(ea[0], 100);
        DWORD curCtr = *(volatile DWORD*)ctrl;
        if(curCtr == lastCtr) { c--; continue; }
        lastCtr = curCtr;
        
        // Copy entire input DMA to both output DMAs
        memcpy(dmaOut0, dmaIn0, bufSz*4*2);
        memcpy(dmaOut1, dmaIn0, bufSz*4*2);
        
        if((c+1) % 375 == 0) printf("  %d sec\n", (c+1)/375);
    }
    
    printf("\nDone.\n");
    io(hD,0x808828C4,NULL,0);
    io(hD,0x80882844,&chList[4],16);
    io(hD,0x80882844,&chList[4104],16);
    io(hD,0x80882844,&chList[4120],16);
    VirtualFree(dmaIn0,0,MEM_RELEASE);
    VirtualFree(dmaOut0,0,MEM_RELEASE);
    VirtualFree(dmaOut1,0,MEM_RELEASE);
    VirtualFree(ctrl,0,MEM_RELEASE);
    CloseHandle(hD);
    return 0;
}
