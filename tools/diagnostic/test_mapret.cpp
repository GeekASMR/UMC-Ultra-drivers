// 检查 MAP_CHANNEL_BUFFER 是否返回真正的 DMA 地址
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdint.h>
#pragma comment(lib, "setupapi.lib")
static const GUID G={0x215A80EF,0x69BD,0x4D85,{0xAC,0x71,0x0C,0x6E,0xA6,0xE6,0xBE,0x17}};

int main(){
    printf("=== MAP_CHANNEL_BUFFER Return Value Test ===\n\n");
    HANDLE ea[3];for(int i=0;i<3;i++)ea[i]=CreateEventW(NULL,FALSE,FALSE,NULL);
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
    
    HANDLE ovEvt=CreateEventW(NULL,TRUE,FALSE,NULL);
    OVERLAPPED ov={};
    auto doIO=[&](DWORD code,void*in,DWORD iSz,void*out,DWORD oSz)->DWORD{
        memset(&ov,0,sizeof(ov));ov.hEvent=ovEvt;DWORD br=0;
        BOOL ok=DeviceIoControl(hD,code,in,iSz,out,oSz,&br,&ov);
        if(!ok&&GetLastError()==ERROR_IO_PENDING)ok=GetOverlappedResult(hD,&ov,&br,TRUE);
        return br;
    };
    
    BYTE tmp[1040];
    doIO(0x80882004,NULL,0,tmp,24);
    doIO(0x80882820,NULL,0,tmp,4);
    doIO(0x808820C4,NULL,0,tmp,1040);
    DWORD mode=0;doIO(0x80882804,&mode,4,NULL,0);
    BYTE*ctrl=(BYTE*)VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    BYTE cb[32]={};*(UINT64*)&cb[0]=(UINT64)(uintptr_t)ctrl;
    *(UINT64*)&cb[8]=(UINT64)(uintptr_t)ea[0];*(UINT64*)&cb[16]=(UINT64)(uintptr_t)ea[1];*(UINT64*)&cb[24]=(UINT64)(uintptr_t)ea[2];
    doIO(0x80882880,cb,32,NULL,0);
    BYTE config[292];doIO(0x80882808,NULL,0,config,292);
    BYTE chList[8200]={};doIO(0x8088280C,NULL,0,chList,8200);
    DWORD numOut=*(DWORD*)&chList[4100];
    for(DWORD i=0;i<*(DWORD*)&chList[0];i++){BYTE x[108];doIO(0x80882810,&chList[4+i*16],16,x,108);}
    for(DWORD i=0;i<numOut;i++){BYTE x[108];doIO(0x80882810,&chList[4104+i*16],16,x,108);}
    doIO(0x80882808,NULL,0,config,292);
    DWORD bufSz=128,bufBytes=bufSz*4;
    doIO(0x80882824,&bufSz,4,NULL,0);
    
    // SELECT output channel 0
    doIO(0x80882840,&chList[4104],16,NULL,0);
    
    // Allocate our buffer
    BYTE* myBuf = (BYTE*)VirtualAlloc(NULL, 0x10000, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    printf("Our buffer addr: 0x%llX\n", (UINT64)(uintptr_t)myBuf);
    
    // MAP_CHANNEL_BUFFER - pass SAME buffer as both in AND out (24 bytes + space)
    BYTE mapBuf[64] = {};
    *(DWORD*)&mapBuf[0] = *(DWORD*)&chList[4112];  // chId
    *(DWORD*)&mapBuf[4] = *(DWORD*)&chList[4116];  // type
    *(DWORD*)&mapBuf[8] = bufBytes;
    *(DWORD*)&mapBuf[12] = 32;
    *(UINT64*)&mapBuf[16] = (UINT64)(uintptr_t)myBuf;
    
    printf("\nMAP input (before IOCTL):\n");
    for(int i=0;i<24;i++) printf("%02X ",mapBuf[i]);
    printf("\nAddr in buf: 0x%llX\n", *(UINT64*)&mapBuf[16]);
    
    // Call with output buffer too!
    BYTE outBuf[64] = {0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC};
    memcpy(outBuf, mapBuf, 24);
    DWORD retBytes = doIO(0x808828A0, mapBuf, 24, outBuf, 64);
    
    printf("\nMAP returned %u bytes\n", retBytes);
    printf("Input buf after IOCTL:\n");
    for(int i=0;i<24;i++) printf("%02X ",mapBuf[i]);
    printf("\nAddr after: 0x%llX\n", *(UINT64*)&mapBuf[16]);
    
    printf("\nOutput buf:\n");
    for(int i=0;i<32;i++) printf("%02X ",outBuf[i]);
    printf("\n");
    
    if(retBytes > 0) {
        printf("\n!!! IOCTL RETURNED DATA! Possible mapped address!\n");
        printf("Returned addr: 0x%llX\n", *(UINT64*)&outBuf[16]);
    }
    
    // Also check: did the kernel modify our input buffer?
    if(*(UINT64*)&mapBuf[16] != (UINT64)(uintptr_t)myBuf) {
        printf("\n!!! KERNEL MODIFIED INPUT BUFFER! New addr: 0x%llX\n", *(UINT64*)&mapBuf[16]);
    }
    
    // Check if kernel wrote something to our buffer already
    printf("\nOur buffer first 32 bytes:\n");
    for(int i=0;i<32;i++) printf("%02X ",myBuf[i]);
    printf("\n");
    
    doIO(0x80882844,&chList[4104],16,NULL,0);
    VirtualFree(myBuf,0,MEM_RELEASE);
    VirtualFree(ctrl,0,MEM_RELEASE);
    CloseHandle(hD);
    printf("\nDone!\n");
    return 0;
}
