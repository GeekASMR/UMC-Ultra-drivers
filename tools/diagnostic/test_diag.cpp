// 诊断: 检查 buffer index 和数据连续性
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
    printf("=== Buffer Diagnosis ===\n\n");
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
    BYTE*dma=(BYTE*)VirtualAlloc(NULL,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    io(hD,0x80882840,&chList[4],16);
    BYTE mp[24]={};
    *(DWORD*)&mp[0]=*(DWORD*)&chList[12];*(DWORD*)&mp[4]=*(DWORD*)&chList[16];
    *(DWORD*)&mp[8]=bufBytes;*(DWORD*)&mp[12]=32;*(UINT64*)&mp[16]=(UINT64)(uintptr_t)dma;
    io(hD,0x808828A0,mp,24);
    
    io(hD,0x808828C8,NULL,0);
    io(hD,0x808828C0,&bufSz,4);
    
    // 分析 ctrl 页面的完整结构
    printf("=== Ctrl Page Analysis ===\n");
    Sleep(100);
    printf("Before streaming (100ms):\n");
    for(int i=0;i<16;i++) printf("  ctrl[%d]=%u (0x%08X)\n", i, ((DWORD*)ctrl)[i], ((DWORD*)ctrl)[i]);
    
    // 分析 WAIT_FOR_BUFFER 的实际阻塞行为
    printf("\n=== WAIT_FOR_BUFFER Timing ===\n");
    HANDLE wEvt=CreateEventW(NULL,TRUE,FALSE,NULL);
    LARGE_INTEGER freq,t0,t1;
    QueryPerformanceFrequency(&freq);
    
    DWORD lastCtr=*(volatile DWORD*)ctrl;
    int waitCount=0;
    
    // 等10次计数器变化，记录每次的 WAIT_FOR_BUFFER 调用次数
    for(int cycle=0; cycle<10; cycle++){
        int wfbCalls=0;
        QueryPerformanceCounter(&t0);
        while(1){
            OVERLAPPED wov={};wov.hEvent=wEvt;DWORD br=0;
            DeviceIoControl(hD,0x808828F4,NULL,0,NULL,0,&br,&wov);
            if(GetLastError()==ERROR_IO_PENDING) GetOverlappedResult(hD,&wov,&br,TRUE);
            wfbCalls++;
            DWORD cur=*(volatile DWORD*)ctrl;
            if(cur!=lastCtr){
                QueryPerformanceCounter(&t1);
                double ms = (double)(t1.QuadPart-t0.QuadPart)*1000.0/freq.QuadPart;
                
                // 检查两个 buffer half 的数据
                int32_t* buf0=(int32_t*)(dma);
                int32_t* buf1=(int32_t*)(dma+bufBytes);
                float f0 = (float)buf0[0] / 2147483648.0f;
                float f1 = (float)buf1[0] / 2147483648.0f;
                
                printf("  cycle%d: ctr=%u->%u (delta=%u) wfb_calls=%d %.2fms | buf0=%.6f buf1=%.6f\n",
                       cycle, lastCtr, cur, cur-lastCtr, wfbCalls, ms, f0, f1);
                lastCtr=cur;
                break;
            }
        }
    }
    
    // 检查连续两个 cycle 的数据是否不同
    printf("\n=== Data Continuity Check ===\n");
    DWORD prevCtr=*(volatile DWORD*)ctrl;
    int32_t prevSample=0;
    for(int i=0;i<20;i++){
        while(*(volatile DWORD*)ctrl==prevCtr) { /* spin */ }
        DWORD cur=*(volatile DWORD*)ctrl;
        int bufIdx = cur & 1;
        int32_t* buf = (bufIdx==0) ? (int32_t*)dma : (int32_t*)(dma+bufBytes);
        float f = (float)buf[0] / 2147483648.0f;
        
        // Also check the OTHER half
        int32_t* otherBuf = (bufIdx==0) ? (int32_t*)(dma+bufBytes) : (int32_t*)dma;
        float fOther = (float)otherBuf[0] / 2147483648.0f;
        
        printf("  ctr=%4u idx=%d buf[idx]=%.6f buf[other]=%.6f %s\n",
               cur, bufIdx, f, fOther,
               (buf[0]==prevSample)?"SAME!":"ok");
        prevSample=buf[0];
        prevCtr=cur;
    }
    
    // 检查 ctrl 页面的完整 DWORD 布局
    printf("\n=== Ctrl Page After Stream ===\n");
    for(int i=0;i<16;i++) printf("  ctrl[%d]=%u (0x%08X)\n", i, ((DWORD*)ctrl)[i], ((DWORD*)ctrl)[i]);
    
    // Stop
    CancelIo(hD);
    io(hD,0x808828C4,NULL,0);
    io(hD,0x80882844,&chList[4],16);
    CloseHandle(wEvt);
    VirtualFree(dma,0,MEM_RELEASE);VirtualFree(ctrl,0,MEM_RELEASE);
    CloseHandle(hD);
    printf("\nDone!\n");
    return 0;
}
