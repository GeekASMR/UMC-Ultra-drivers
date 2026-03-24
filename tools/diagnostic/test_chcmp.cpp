#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
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
    BYTE ctrl[256]={};
    HANDLE ea[3]; for(int i=0;i<3;i++) ea[i]=CreateEventW(NULL,FALSE,FALSE,NULL);
    BYTE cb[32]={};*(UINT64*)&cb[0]=(UINT64)(uintptr_t)ctrl;
    *(UINT64*)&cb[8]=(UINT64)(uintptr_t)ea[0];*(UINT64*)&cb[16]=(UINT64)(uintptr_t)ea[1];*(UINT64*)&cb[24]=(UINT64)(uintptr_t)ea[2];
    io(hD,0x80882880,cb,32);
    BYTE config[292];io(hD,0x80882808,NULL,0,config,292);
    BYTE chList[8200]={};io(hD,0x8088280C,NULL,0,chList,8200);
    DWORD numIn=*(DWORD*)&chList[0],numOut=*(DWORD*)&chList[4100];
    
    printf("=== Channel List Layout ===\n");
    printf("numIn=%u numOut=%u\n\n", numIn, numOut);
    
    // Show output channel entries
    printf("Output channels (raw 16-byte entries):\n");
    for(DWORD i=0;i<numOut&&i<5;i++){
        BYTE* entry = &chList[4104 + i*16];
        DWORD d0=*(DWORD*)&entry[0], d1=*(DWORD*)&entry[4];
        DWORD d2=*(DWORD*)&entry[8], d3=*(DWORD*)&entry[12];
        printf("  Out[%u]: [0]=0x%08X [1]=0x%08X [2]=0x%08X [3]=0x%08X\n",
               i, d0, d1, d2, d3);
        
        // What we'd send to MAP_CHANNEL_BUFFER
        printf("         -> MAP would use: chId=0x%X type=0x%X\n", d2, d3);
    }
    
    // Compare with official: chId=0x52 type=0x117
    printf("\nOfficial DLL used: Out0: chId=0x52 type=0x117\n");
    printf("                   Out1: chId=0x53 type=0x117\n");
    
    // SELECT uses full 16-byte entry
    printf("\nOur SELECT would send: ");
    for(int i=0;i<16;i++) printf("%02X ", chList[4104+i]);
    printf("\n");
    printf("Official SELECT sent:  01 00 00 00 14 01 00 00 52 00 00 00 17 01 00 00\n");
    
    CloseHandle(hD);
    return 0;
}
