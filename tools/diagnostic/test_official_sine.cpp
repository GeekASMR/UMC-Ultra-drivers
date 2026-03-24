// 用官方 DLL 输出正弦波，验证硬件输出是否正常
#include <windows.h>
#include <stdio.h>
#include <math.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#define PI 3.14159265358979

static double g_phase = 0;
static double g_phaseInc = 2.0 * PI * 440.0 / 48000.0;
static long g_bufSize = 0;
static void* g_outL[2] = {};
static void* g_outR[2] = {};
static int g_type = 0; // sample type
static volatile LONG g_count = 0;

void __cdecl bsw(long idx, long dp) {
    InterlockedIncrement(&g_count);
    if (!g_outL[idx] || !g_outR[idx]) return;
    
    for (long i = 0; i < g_bufSize; i++) {
        double s = sin(g_phase) * 0.3;
        g_phase += g_phaseInc;
        
        if (g_type == 18) { // ASIOSTInt32LSB
            ((int*)g_outL[idx])[i] = (int)(s * 2147483648.0);
            ((int*)g_outR[idx])[i] = (int)(s * 2147483648.0);
        } else if (g_type == 19) { // ASIOSTFloat32LSB
            ((float*)g_outL[idx])[i] = (float)s;
            ((float*)g_outR[idx])[i] = (float)s;
        } else { // try float anyway
            ((float*)g_outL[idx])[i] = (float)s;
            ((float*)g_outR[idx])[i] = (float)s;
        }
    }
}
void __cdecl sr(double r) { printf("SampleRate changed: %.0f\n", r); }
long __cdecl am(long s, long v, void* m, double* o) {
    if (s == 1 || s == 4 || s == 7) return 1;
    return 0;
}
void* __cdecl bsti(void* p, long idx, long dp) {
    bsw(idx, dp);
    return p;
}

int main() {
    printf("=== Official DLL Sine Output Test ===\n\n");
    
    // Load official DLL
    HMODULE hDll = LoadLibraryW(L"C:\\Program Files\\Behringer\\UMC\\umc_audioasio_x64.dll");
    if (!hDll) {
        // Try alternate paths
        hDll = LoadLibraryW(L"C:\\Program Files (x86)\\Behringer\\UMC\\umc_audioasio_x64.dll");
    }
    if (!hDll) {
        // Search in system directories
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(L"C:\\Windows\\System32\\umc_audio*.dll", &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            wchar_t path[512];
            swprintf(path, 512, L"C:\\Windows\\System32\\%s", fd.cFileName);
            hDll = LoadLibraryW(path);
            FindClose(hFind);
        }
    }
    if (!hDll) {
        printf("Cannot find official Behringer ASIO DLL!\n");
        printf("Searching...\n");
        // Try registry
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO\\UMC ASIO Driver", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            WCHAR clsid[128]; DWORD sz = sizeof(clsid);
            RegQueryValueExW(hKey, L"CLSID", NULL, NULL, (LPBYTE)clsid, &sz);
            printf("Official CLSID: %ls\n", clsid);
            
            WCHAR keyPath[256];
            swprintf(keyPath, 256, L"SOFTWARE\\Classes\\CLSID\\%s\\InprocServer32", clsid);
            HKEY hKey2;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &hKey2) == ERROR_SUCCESS) {
                WCHAR dllPath[512]; sz = sizeof(dllPath);
                RegQueryValueExW(hKey2, NULL, NULL, NULL, (LPBYTE)dllPath, &sz);
                printf("DLL path: %ls\n", dllPath);
                hDll = LoadLibraryW(dllPath);
                RegCloseKey(hKey2);
            }
            RegCloseKey(hKey);
        }
    }
    
    if (!hDll) { printf("FAILED to load official DLL\n"); return 1; }
    printf("Official DLL loaded: %p\n", hDll);
    
    typedef HRESULT (WINAPI *FnGCO)(REFCLSID, REFIID, LPVOID*);
    auto pGCO = (FnGCO)GetProcAddress(hDll, "DllGetClassObject");
    if (!pGCO) { printf("No DllGetClassObject\n"); return 1; }
    
    // Official CLSID
    const CLSID clsid = {0x0351302f, 0xb1f1, 0x4a5d, {0x86, 0x13, 0x78, 0x7f, 0x77, 0xc2, 0x0e, 0xa4}};
    IClassFactory* pF = NULL;
    pGCO(clsid, IID_IClassFactory, (void**)&pF);
    if (!pF) { printf("No factory\n"); return 1; }
    IUnknown* pU = NULL;
    pF->CreateInstance(NULL, clsid, (void**)&pU);
    pF->Release();
    if (!pU) { printf("No instance\n"); return 1; }
    void** vt = *(void***)pU;
    
    typedef BOOL (__thiscall *fn_init)(void*, void*);
    typedef void (__thiscall *fn_getName)(void*, char*);
    typedef long (__thiscall *fn_getCh)(void*, long*, long*);
    typedef long (__thiscall *fn_getBuf)(void*, long*, long*, long*, long*);
    typedef long (__thiscall *fn_start)(void*);
    typedef long (__thiscall *fn_stop)(void*);
    typedef long (__thiscall *fn_dispose)(void*);
    typedef long (__thiscall *fn_getChInfo)(void*, void*);
    
    if (!((fn_init)vt[3])(pU, NULL)) { printf("init failed\n"); return 1; }
    char name[64]; ((fn_getName)vt[4])(pU, name);
    printf("Driver: %s\n", name);
    
    long nIn, nOut; ((fn_getCh)vt[9])(pU, &nIn, &nOut);
    long minB, maxB, prefB, gran; ((fn_getBuf)vt[11])(pU, &minB, &maxB, &prefB, &gran);
    printf("Ch: %ld/%ld  Buf: %ld\n", nIn, nOut, prefB);
    g_bufSize = prefB;
    
    // Get output channel type
    struct CI { long ch; long isInput; long isActive; long group; long type; char name[32]; };
    CI ci = {}; ci.ch = 0; ci.isInput = 0;
    ((fn_getChInfo)vt[18])(pU, &ci);
    g_type = ci.type;
    printf("Out0: name='%s' type=%ld\n", ci.name, ci.type);
    
    struct CB { void*a; void*b; void*c; void*d; };
    CB cbs = { (void*)bsw, (void*)sr, (void*)am, (void*)bsti };
    struct BI { long isInput; long channelNum; void* buffers[2]; };
    BI bi[2] = {};
    bi[0].isInput = 0; bi[0].channelNum = 0; // Out L
    bi[1].isInput = 0; bi[1].channelNum = 1; // Out R
    
    typedef long (__thiscall *fn_cb)(void*, BI*, long, long, CB*);
    long r = ((fn_cb)vt[19])(pU, bi, 2, prefB, &cbs);
    printf("createBuffers: %ld\n", r);
    if (r != 0) { printf("FAILED\n"); return 1; }
    
    g_outL[0] = bi[0].buffers[0]; g_outL[1] = bi[0].buffers[1];
    g_outR[0] = bi[1].buffers[0]; g_outR[1] = bi[1].buffers[1];
    printf("OutL: %p/%p  OutR: %p/%p\n", g_outL[0], g_outL[1], g_outR[0], g_outR[1]);
    
    g_count = 0;
    r = ((fn_start)vt[7])(pU);
    printf("start: %ld\n\n", r);
    
    printf("Playing 440Hz sine through official DLL for 3 seconds...\n");
    for (int t = 0; t < 6; t++) {
        Sleep(500);
        printf("  [%dms] callbacks=%ld\n", (t+1)*500, g_count);
    }
    
    ((fn_stop)vt[8])(pU);
    ((fn_dispose)vt[20])(pU);
    pU->Release();
    printf("\nDone! Did you hear a clean 440Hz tone?\n");
    return 0;
}
