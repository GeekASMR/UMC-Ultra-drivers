#include <windows.h>
#include <stdio.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

int main() {
    printf("=== Test BehringerASIO DLL v5 ===\n\n");
    
    HMODULE hDll = LoadLibraryW(L"D:\\UMCasio\\build\\bin\\Release\\BehringerASIO.dll");
    if (!hDll) { printf("Load failed: %lu\n", GetLastError()); return 1; }
    printf("DLL loaded: %p\n", hDll);
    
    typedef HRESULT (WINAPI *FnGCO)(REFCLSID, REFIID, LPVOID*);
    auto pGCO = (FnGCO)GetProcAddress(hDll, "DllGetClassObject");
    if (!pGCO) { printf("No DllGetClassObject\n"); return 1; }
    
    const CLSID clsid = {0xa1b2c3d4, 0xe5f6, 0x7890, {0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90}};
    IClassFactory* pF = NULL;
    HRESULT hr = pGCO(clsid, IID_IClassFactory, (void**)&pF);
    printf("GetClassObject: 0x%08X\n", hr);
    if (FAILED(hr)) return 1;
    
    IUnknown* pU = NULL;
    hr = pF->CreateInstance(NULL, clsid, (void**)&pU);
    pF->Release();
    printf("CreateInstance: 0x%08X\n", hr);
    if (!pU) return 1;
    
    void** vt = *(void***)pU;
    
    // init (vt[3])
    typedef BOOL (__thiscall *fn_init)(void*, void*);
    BOOL initOk = ((fn_init)vt[3])(pU, NULL);
    printf("init(): %s\n", initOk ? "OK" : "FAIL");
    
    if (initOk) {
        // getDriverName (vt[4])
        typedef void (__thiscall *fn_getName)(void*, char*);
        char name[64] = {};
        ((fn_getName)vt[4])(pU, name);
        printf("DriverName: %s\n", name);
        
        // getDriverVersion (vt[5])
        typedef long (__thiscall *fn_getVer)(void*);
        long ver = ((fn_getVer)vt[5])(pU);
        printf("Version: %ld\n", ver);
        
        // getChannels (vt[9])
        typedef long (__thiscall *fn_getCh)(void*, long*, long*);
        long nIn = 0, nOut = 0;
        ((fn_getCh)vt[9])(pU, &nIn, &nOut);
        printf("Channels: %ld in / %ld out\n", nIn, nOut);
        
        // getBufferSize (vt[11])
        typedef long (__thiscall *fn_getBuf)(void*, long*, long*, long*, long*);
        long minB, maxB, prefB, gran;
        ((fn_getBuf)vt[11])(pU, &minB, &maxB, &prefB, &gran);
        printf("Buffer: min=%ld max=%ld pref=%ld gran=%ld\n", minB, maxB, prefB, gran);
        
        // getSampleRate (vt[14])
        typedef long (__thiscall *fn_getRate)(void*, double*);
        double rate;
        ((fn_getRate)vt[14])(pU, &rate);
        printf("SampleRate: %.0f Hz\n", rate);
        
        // getChannelInfo (vt[18])
        typedef long (__thiscall *fn_getChInfo)(void*, void*);
        struct { long ch; long isInput; long isActive; long group; long type; char name[32]; } ci;
        for (int i = 0; i < 4 && i < nIn; i++) {
            memset(&ci, 0, sizeof(ci));
            ci.ch = i; ci.isInput = 1;
            ((fn_getChInfo)vt[18])(pU, &ci);
            printf("  In%d: name='%s' type=%ld active=%ld\n", i, ci.name, ci.type, ci.isActive);
        }
        for (int i = 0; i < 4 && i < nOut; i++) {
            memset(&ci, 0, sizeof(ci));
            ci.ch = i; ci.isInput = 0;
            ((fn_getChInfo)vt[18])(pU, &ci);
            printf("  Out%d: name='%s' type=%ld\n", i, ci.name, ci.type);
        }
    }
    
    pU->Release();
    FreeLibrary(hDll);
    printf("\nDone!\n");
    return 0;
}
