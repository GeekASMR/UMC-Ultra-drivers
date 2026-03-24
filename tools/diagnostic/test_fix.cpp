// 快速验证: 修复后的 buffer index 是否正确
#include <windows.h>
#include <stdio.h>
#include <math.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

static volatile LONG g_sc = 0;
static float g_lastSample = 0;
static int g_discontinuities = 0;
static int g_sameCount = 0;
static void* g_inBuf[2] = {};
static long g_bufSize = 0;

// 检查每次 bufferSwitch 时数据是否在变化
void __cdecl my_bsw(long idx, long dp) {
    LONG count = InterlockedIncrement(&g_sc);
    if (g_inBuf[idx]) {
        float* buf = (float*)g_inBuf[idx];
        float s = buf[0];
        if (count > 2 && fabsf(s - g_lastSample) < 1e-10f) {
            g_sameCount++;
        }
        // 检查数据是否为零
        if (s == 0.0f) g_discontinuities++;
        g_lastSample = s;
    }
}
void __cdecl my_sr(double r) {}
long __cdecl my_am(long s, long v, void* m, double* o) {
    if (s == 1 || s == 4 || s == 7) return 1;
    return 0;
}
void* __cdecl my_bsti(void* p, long idx, long dp) {
    my_bsw(idx, dp);
    return p;
}

int main() {
    printf("=== Post-fix Buffer Test ===\n\n");
    HMODULE hDll = LoadLibraryW(L"D:\\UMCasio\\build\\bin\\Release\\BehringerASIO.dll");
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
    ((fn_init)vt[3])(pU, NULL);
    long nIn, nOut; ((fn_getCh)vt[9])(pU, &nIn, &nOut);
    long minB, maxB, prefB, gran; ((fn_getBuf)vt[11])(pU, &minB, &maxB, &prefB, &gran);
    g_bufSize = prefB;
    struct CB { void*a; void*b; void*c; void*d; };
    CB cbs = { (void*)my_bsw, (void*)my_sr, (void*)my_am, (void*)my_bsti };
    struct BI { long isInput; long channelNum; void* buffers[2]; };
    BI bi[4] = {};
    bi[0].isInput=1; bi[0].channelNum=0;
    bi[1].isInput=1; bi[1].channelNum=1;
    bi[2].isInput=0; bi[2].channelNum=0;
    bi[3].isInput=0; bi[3].channelNum=1;
    typedef long (__thiscall *fn_cb)(void*, BI*, long, long, CB*);
    ((fn_cb)vt[19])(pU, bi, 4, prefB, &cbs);
    g_inBuf[0] = bi[0].buffers[0];
    g_inBuf[1] = bi[0].buffers[1];
    printf("In0 buf: %p / %p\n", g_inBuf[0], g_inBuf[1]);
    g_sc = 0; g_sameCount = 0; g_discontinuities = 0;
    ((fn_start)vt[7])(pU);
    for (int t = 0; t < 5; t++) {
        Sleep(400);
        float* d0 = (float*)g_inBuf[0];
        float* d1 = (float*)g_inBuf[1];
        printf("[%dms] bs=%ld same=%d zero=%d | b0=%.6f b1=%.6f\n",
               (t+1)*400, g_sc, g_sameCount, g_discontinuities,
               d0[0], d1[0]);
    }
    printf("\nTotal: %ld switches, %d same-data, %d zeros (%.0f Hz)\n",
           g_sc, g_sameCount, g_discontinuities, g_sc/2.0);
    printf("Quality: %s\n",
           g_sameCount < 5 ? "GOOD - data changes each switch" : "BAD - stale data");
    ((fn_stop)vt[8])(pU);
    ((fn_dispose)vt[20])(pU);
    pU->Release();
    FreeLibrary(hDll);
    printf("Done!\n");
    return 0;
}
