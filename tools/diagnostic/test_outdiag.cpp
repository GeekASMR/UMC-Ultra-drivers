// 诊断: 内核是否覆盖输出 DMA?  数据格式是否正确?
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <math.h>
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
int main(){
    printf("=== Output DMA Diagnostics ===\n\n");
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
    io(hD,0x80882004,NULL,0,tmp,24);io(hD,0x80882820,NULL,0,tmp,4);
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
    BYTE*dmaOut=(BYTE*)VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    
    io(hD,0x80882840,&chList[4104],16);
    BYTE mp[24]={};
    *(DWORD*)&mp[0]=*(DWORD*)&chList[4112];*(DWORD*)&mp[4]=*(DWORD*)&chList[4116];
    *(DWORD*)&mp[8]=bufBytes;*(DWORD*)&mp[12]=32;*(UINT64*)&mp[16]=(UINT64)(uintptr_t)dmaOut;
    io(hD,0x808828A0,mp,24);
    
    // Test 1: 写 pattern, 看内核是否覆盖 (流停止状态)
    printf("Test1: Write pattern BEFORE start...\n");
    for(int i=0;i<16;i++) ((int32_t*)dmaOut)[i] = 0xDEAD0000 + i;
    
    printf("  Before: ");
    for(int i=0;i<4;i++) printf("%08X ", ((uint32_t*)dmaOut)[i]);
    printf("\n");
    
    Sleep(100);
    printf("  After 100ms: ");
    for(int i=0;i<4;i++) printf("%08X ", ((uint32_t*)dmaOut)[i]);
    bool changed = false;
    for(int i=0;i<16;i++) if(((int32_t*)dmaOut)[i] != (int32_t)(0xDEAD0000+i)) changed=true;
    printf(" %s\n", changed?"CHANGED!":"unchanged");
    
    // START streaming
    io(hD,0x808828C8,NULL,0);
    io(hD,0x808828C0,&bufSz,4);
    
    // Test 2: 流运行中，写 pattern 看是否被覆盖
    printf("\nTest2: Write pattern DURING stream...\n");
    for(int i=0;i<bufSz*2;i++) ((int32_t*)dmaOut)[i] = 0xBEEF0000 + i;
    printf("  Before: ");
    for(int i=0;i<4;i++) printf("%08X ", ((uint32_t*)dmaOut)[i]);
    printf("\n");
    
    // 等 2 个 cycle
    WaitForSingleObject(ea[0], 100);
    WaitForSingleObject(ea[0], 100);
    
    printf("  After 2 cycles: ");
    for(int i=0;i<4;i++) printf("%08X ", ((uint32_t*)dmaOut)[i]);
    changed = false;
    int changedCount=0;
    for(int i=0;i<bufSz*2;i++) if(((int32_t*)dmaOut)[i] != (int32_t)(0xBEEF0000+i)) changedCount++;
    printf(" changed=%d/%d\n", changedCount, bufSz*2);
    
    // Test 3: 官方 DLL 是怎么写输入的？看看输入 channel 的 DMA
    // 看 output DMA 是否也被内核写入了音频数据
    printf("\nTest3: Read back output DMA as float...\n");
    for(int cycle=0;cycle<5;cycle++){
        WaitForSingleObject(ea[0], 100);
        DWORD ctr = *(volatile DWORD*)ctrl;
        int32_t* buf0 = (int32_t*)dmaOut;
        int32_t* buf1 = (int32_t*)(dmaOut + bufBytes);
        float f0 = (float)buf0[0] / 2147483648.0f;
        float f1 = (float)buf1[0] / 2147483648.0f;
        printf("  ctr=%u buf0[0]=%08X(%.6f) buf1[0]=%08X(%.6f)\n",
               ctr, (uint32_t)buf0[0], f0, (uint32_t)buf1[0], f1);
    }
    
    // Test 4: 检查 output DMA 的完整 hex dump
    printf("\nTest4: Output DMA hex dump (first 64 bytes):\n");
    BYTE* p = dmaOut;
    for(int row=0;row<4;row++){
        printf("  +%03X: ", row*16);
        for(int i=0;i<16;i++) printf("%02X ", p[row*16+i]);
        printf("\n");
    }
    printf("  buf[1] start (+%03X):\n", bufBytes);
    p = dmaOut + bufBytes;
    for(int row=0;row<2;row++){
        printf("  +%03X: ", bufBytes+row*16);
        for(int i=0;i<16;i++) printf("%02X ", p[row*16+i]);
        printf("\n");
    }
    
    io(hD,0x808828C4,NULL,0);
    io(hD,0x80882844,&chList[4104],16);
    VirtualFree(dmaOut,0,MEM_RELEASE);
    VirtualFree(ctrl,0,MEM_RELEASE);
    CloseHandle(hD);
    printf("\nDone!\n");
    return 0;
}
