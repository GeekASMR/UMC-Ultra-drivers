// 完整流测试: init -> createBuffers -> start -> 2s -> stop
#include <windows.h>
#include <stdio.h>
#include <math.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

static volatile LONG g_sc = 0;
static void* g_inBuf0 = NULL;

void __cdecl my_bsw(long idx, long dp) { InterlockedIncrement(&g_sc); }
void __cdecl my_sr(double r) {}
long __cdecl my_am(long s, long v, void* m, double* o) {
    if (s == 1 || s == 4 || s == 7) return 1;
    return 0;
}
void* __cdecl my_bsti(void* p, long idx, long dp) {
    InterlockedIncrement(&g_sc);
    return p;
}

int main() {
    printf("=== Full Stream Test with v5 DLL ===\n\n");
    
    HMODULE hDll = LoadLibraryW(L"D:\\UMCasio\\build\\bin\\Release\\BehringerASIO.dll");
    if (!hDll) { printf("Load failed\n"); return 1; }
    
    typedef HRESULT (WINAPI *FnGCO)(REFCLSID, REFIID, LPVOID*);
    const CLSID clsid = {0xa1b2c3d4, 0xe5f6, 0x7890, {0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90}};
    IClassFactory* pF; ((FnGCO)GetProcAddress(hDll, "DllGetClassObject"))(clsid, IID_IClassFactory, (void**)&pF);
    IUnknown* pU; pF->CreateInstance(NULL, clsid, (void**)&pU); pF->Release();
    void** vt = *(void***)pU;
    
    typedef BOOL (__thiscall *fn_init)(void*, void*);
    typedef long (__thiscall *fn_getCh)(void*, long*, long*);
    typedef long (__thiscall *fn_getBuf)(void*, long*, long*, long*, long*);
    typedef long (__thiscall *fn_start)(void*);
    typedef long (__thiscall *fn_stop)(void*);
    typedef long (__thiscall *fn_dispose)(void*);
    
    if (!((fn_init)vt[3])(pU, NULL)) { printf("init fail\n"); return 1; }
    
    long nIn, nOut;
    ((fn_getCh)vt[9])(pU, &nIn, &nOut);
    long minB, maxB, prefB, gran;
    ((fn_getBuf)vt[11])(pU, &minB, &maxB, &prefB, &gran);
    printf("Ch: %ld/%ld  Buf: %ld\n", nIn, nOut, prefB);
    
    struct CB {
        void (__cdecl *bufferSwitch)(long, long);
        void (__cdecl *sampleRateChanged)(double);
        long (__cdecl *asioMessage)(long, long, void*, double*);
        void* (__cdecl *bufferSwitchTimeInfo)(void*, long, long);
    };
    CB cbs = { my_bsw, my_sr, my_am, my_bsti };
    
    struct BI { long isInput; long channelNum; void* buffers[2]; };
    BI bi[4];
    memset(bi, 0, sizeof(bi));
    bi[0].isInput = 1; bi[0].channelNum = 0;  // In 1
    bi[1].isInput = 1; bi[1].channelNum = 1;  // In 2
    bi[2].isInput = 0; bi[2].channelNum = 0;  // Out 1
    bi[3].isInput = 0; bi[3].channelNum = 1;  // Out 2
    
    typedef long (__thiscall *fn_cb)(void*, BI*, long, long, CB*);
    long cbr = ((fn_cb)vt[19])(pU, bi, 4, prefB, &cbs);
    printf("createBuffers: %ld\n", cbr);
    
    if (cbr == 0) {
        g_inBuf0 = bi[0].buffers[0];
        printf("In0: %p/%p  Out0: %p/%p\n", bi[0].buffers[0], bi[0].buffers[1],
               bi[2].buffers[0], bi[2].buffers[1]);
        
        printf("\nStarting...\n");
        g_sc = 0;
        long sr = ((fn_start)vt[7])(pU);
        printf("start: %ld\n", sr);
        
        if (sr == 0) {
            for (int t = 0; t < 5; t++) {
                Sleep(400);
                float* d = (float*)g_inBuf0;
                printf("[%dms] bSwitch=%ld  In0=%.6f %.6f %.6f\n",
                       (t+1)*400, g_sc, d[0], d[1], d[2]);
            }
            printf("\nTotal: %ld switches in 2s (%.1f Hz)\n", g_sc, g_sc/2.0);
        }
        
        ((fn_stop)vt[8])(pU);
        ((fn_dispose)vt[20])(pU);
    }
    
    pU->Release();
    FreeLibrary(hDll);
    printf("Done!\n");
    return 0;
}
