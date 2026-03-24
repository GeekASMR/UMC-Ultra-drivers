// 测试 ASIO 驱动加载
#include <windows.h>
#include <objbase.h>
#include <cstdio>

// Our CLSID
static const GUID CLSID_BehringerASIO =
    { 0xa1b2c3d4, 0xe5f6, 0x7890, { 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x90 } };

// Official CLSID
static const GUID CLSID_Official =
    { 0x0351302f, 0xb1f1, 0x4a5d, { 0x86, 0x13, 0x78, 0x7f, 0x77, 0xc2, 0x0e, 0xa4 } };

typedef HRESULT (__stdcall *DllGetClassObjectFunc)(REFCLSID, REFIID, LPVOID*);

int main() {
    printf("=== ASIO Driver Load Test ===\n");
    
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
    // 直接加载 DLL
    printf("Loading DLL...\n");
    HMODULE hDll = LoadLibraryA("D:\\UMCasio\\build\\bin\\Release\\BehringerASIO.dll");
    if (!hDll) {
        printf("LoadLibrary FAILED: %lu\n", GetLastError());
        return 1;
    }
    printf("DLL loaded at %p\n", hDll);
    
    // 获取 DllGetClassObject
    auto pFunc = (DllGetClassObjectFunc)GetProcAddress(hDll, "DllGetClassObject");
    if (!pFunc) {
        printf("GetProcAddress failed\n");
        FreeLibrary(hDll);
        return 1;
    }
    printf("DllGetClassObject at %p\n", pFunc);
    
    // 测试用官方 CLSID 获取 ClassFactory
    printf("\nTesting with OFFICIAL CLSID...\n");
    IClassFactory* pFactory = nullptr;
    HRESULT hr = pFunc(CLSID_Official, IID_IClassFactory, (void**)&pFactory);
    printf("DllGetClassObject(Official): hr=0x%08X\n", hr);
    
    if (SUCCEEDED(hr) && pFactory) {
        printf("Got ClassFactory, creating instance...\n");
        IUnknown* pObj = nullptr;
        hr = pFactory->CreateInstance(NULL, IID_IUnknown, (void**)&pObj);
        printf("CreateInstance: hr=0x%08X, pObj=%p\n", hr, pObj);
        
        if (pObj) {
            // 尝试调用 init
            // IASIO 接口布局: 查询接口后需要调用 init(sysHandle)
            // vtable: QueryInterface, AddRef, Release, init, getDriverName, ...
            printf("Object created successfully!\n");
            
            // 简单调用 getDriverName (vtable index 4)
            typedef void (__stdcall *GetDriverNameFunc)(void*, char*);
            void** vtable = *(void***)pObj;
            // The IASIO vtable starts after IUnknown (3 funcs)
            // init is at index 3, getDriverName at index 4
            auto getDriverName = (GetDriverNameFunc)vtable[4];
            
            char name[64] = {};
            printf("Calling getDriverName...\n");
            __try {
                getDriverName(pObj, name);
                printf("Driver name: '%s'\n", name);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                printf("getDriverName CRASHED! Exception: 0x%08X\n", GetExceptionCode());
            }
            
            // Try init
            typedef int (__stdcall *InitFunc)(void*, void*);
            auto initFunc = (InitFunc)vtable[3];
            printf("\nCalling init(NULL)...\n");
            __try {
                int result = initFunc(pObj, NULL);
                printf("init() returned: %d\n", result);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                printf("init() CRASHED! Exception: 0x%08X\n", GetExceptionCode());
            }
            
            pObj->Release();
        }
        pFactory->Release();
    }
    
    // 测试用我们自己的 CLSID
    printf("\nTesting with OUR CLSID...\n");
    hr = pFunc(CLSID_BehringerASIO, IID_IClassFactory, (void**)&pFactory);
    printf("DllGetClassObject(Ours): hr=0x%08X\n", hr);
    if (pFactory) pFactory->Release();
    
    FreeLibrary(hDll);
    CoUninitialize();
    printf("\nDone.\n");
    return 0;
}
