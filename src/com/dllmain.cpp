/*
 * DLL Main - COM DLL 入口点
 *
 * 处理 DLL 加载/卸载、COM 对象导出和 ASIO 驱动注册
 */

#include <windows.h>
#include <objbase.h>
#include <strsafe.h>
#include "ClassFactory.h"
#include "../driver/BehringerASIO.h"
#include "../utils/Logger.h"
#include "../utils/CrashHandler.h"

#define LOG_MODULE "DllMain"

// Module handle
HMODULE g_hModule = nullptr;

// DLL reference count
static volatile LONG g_dllRefCount = 0;

#include "../AsioTargets.h"

// Driver info for registration dynamically injected via compiler directives
#define DRIVER_NAME   g_CurrentTarget.brandPrefix
#define DRIVER_DESC   g_CurrentTarget.brandPrefix
#define DRIVER_CLSID  g_CurrentTarget.clsidStr

//-------------------------------------------------------------------
// DLL Entry Point
//-------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            g_hModule = hinstDLL;
            DisableThreadLibraryCalls(hinstDLL);
            CrashHandler::init();
            break;

        case DLL_PROCESS_DETACH:
            CrashHandler::uninit();
            break;
    }
    return TRUE;
}

//-------------------------------------------------------------------
// COM DLL Exports
//-------------------------------------------------------------------

// Official Behringer UMC ASIO CLSID (for backward compatibility with DAW cached configs)
static const GUID CLSID_OfficialBehringer =
    { 0x0351302f, 0xb1f1, 0x4a5d, { 0x86, 0x13, 0x78, 0x7f, 0x77, 0xc2, 0x0e, 0xa4 } };

// DllGetClassObject - Called by COM to get the class factory
thread_local GUID g_RequestedCLSID = {0};

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    g_RequestedCLSID = rclsid;
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    // Accept both our CLSID and the official Behringer CLSID
    if (rclsid != CLSID_BehringerASIO && rclsid != CLSID_OfficialBehringer) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    ClassFactory* pFactory = new ClassFactory();
    if (!pFactory) return E_OUTOFMEMORY;

    HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();  // Balance the creation ref

    return hr;
}

// DllCanUnloadNow - Called by COM to check if DLL can be unloaded
STDAPI DllCanUnloadNow() {
    if (g_dllRefCount == 0 && ClassFactory::s_serverLocks == 0)
        return S_OK;
    return S_FALSE;
}

//-------------------------------------------------------------------
// Registration Helpers
//-------------------------------------------------------------------

// Helper to write a registry key value
static HRESULT RegSetString(HKEY hKey, const char* valueName, const char* value) {
    LONG result = RegSetValueExA(hKey, valueName, 0, REG_SZ,
                                  (const BYTE*)value, (DWORD)(strlen(value) + 1));
    return (result == ERROR_SUCCESS) ? S_OK : HRESULT_FROM_WIN32(result);
}

// Dynamically probes the registry for the true host ASIO name and appends " Ultra"
static void GetDynamicAsioName(char* outName, size_t maxLen) {
    strncpy(outName, DRIVER_NAME, maxLen); // Strict Fallback
    
    HKEY hKeyAsio;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &hKeyAsio) == ERROR_SUCCESS) {
        char subKeyName[256];
        DWORD index = 0;
        DWORD nameLen = sizeof(subKeyName);
        while (RegEnumKeyExA(hKeyAsio, index, subKeyName, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            bool isBlacklisted = false;
            const char* blacklist[] = {"Ultra", "ASMRTOP", "WDM2VST", "Virtual"};
            for (int b = 0; b < 4; b++) {
                if (strstr(subKeyName, blacklist[b])) { isBlacklisted = true; break; }
            }
            if (!isBlacklisted && g_CurrentTarget.searchKeyword && strstr(subKeyName, g_CurrentTarget.searchKeyword)) {
                // Remove trailing ASIO if present to keep it clean (e.g. "MOTU Audio ASIO" -> "MOTU Audio Ultra")
                char cleanName[256];
                strncpy(cleanName, subKeyName, sizeof(cleanName));
                char* pAsio = strstr(cleanName, " ASIO");
                if (pAsio) *pAsio = '\0';
                
                snprintf(outName, maxLen, "%s Ultra", cleanName);
                break;
            }
            index++;
            nameLen = sizeof(subKeyName);
        }
        RegCloseKey(hKeyAsio);
    }
}

// DllRegisterServer - Register the ASIO driver in the Windows registry
STDAPI DllRegisterServer() {
    try {
        HKEY hKey = nullptr;
        HKEY hSubKey = nullptr;
        DWORD dwDisp;

        char modulePath[MAX_PATH];
        GetModuleFileNameA(g_hModule, modulePath, MAX_PATH);

        Logger::getInstance().init();
        LOG_INFO(LOG_MODULE, "Registering ASIO driver: %s", modulePath);
        
        char dynamicName[256];
        GetDynamicAsioName(dynamicName, sizeof(dynamicName));

        // 1. Register COM class
        //    HKCR\CLSID\{...}\InprocServer32
        char clsidKey[256];
        StringCchPrintfA(clsidKey, 256, "CLSID\\%s", DRIVER_CLSID);

        LONG result = RegCreateKeyExA(HKEY_CLASSES_ROOT, clsidKey, 0, nullptr,
                                       REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                                       &hKey, &dwDisp);
        if (result != ERROR_SUCCESS) {
            LOG_ERROR(LOG_MODULE, "Failed to create CLSID key: %ld", result);
            return HRESULT_FROM_WIN32(result);
        }

        // Set default value to driver name
        RegSetString(hKey, nullptr, dynamicName);
        RegSetString(hKey, "Description", dynamicName);

        // Create InprocServer32 subkey
        result = RegCreateKeyExA(hKey, "InprocServer32", 0, nullptr,
                                  REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                                  &hSubKey, &dwDisp);
        if (result == ERROR_SUCCESS) {
            RegSetString(hSubKey, nullptr, modulePath);
            RegSetString(hSubKey, "ThreadingModel", "Apartment");
            RegCloseKey(hSubKey);
        }

        RegCloseKey(hKey);

        // 2. Register ASIO driver
        //    HKLM\SOFTWARE\ASIO\<DriverName>
        char asioKey[256];
        StringCchPrintfA(asioKey, 256, "SOFTWARE\\ASIO\\%s", dynamicName);

        result = RegCreateKeyExA(HKEY_LOCAL_MACHINE, asioKey, 0, nullptr,
                                  REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                                  &hKey, &dwDisp);
        if (result != ERROR_SUCCESS) {
            LOG_ERROR(LOG_MODULE, "Failed to create ASIO key: %ld", result);
            return HRESULT_FROM_WIN32(result);
        }

        RegSetString(hKey, "CLSID", DRIVER_CLSID);
        RegSetString(hKey, "Description", dynamicName);
        RegCloseKey(hKey);

        LOG_INFO(LOG_MODULE, "ASIO driver registered successfully AS: %s", dynamicName);
        return S_OK;
    }
    catch (...) {
        OutputDebugStringA("DllRegisterServer: C++ exception caught\n");
        return E_UNEXPECTED;
    }
}

// DllUnregisterServer - Remove the ASIO driver registration
STDAPI DllUnregisterServer() {
    try {
        Logger::getInstance().init();
        LOG_INFO(LOG_MODULE, "Unregistering ASIO driver");
        
        char dynamicName[256];
        GetDynamicAsioName(dynamicName, sizeof(dynamicName));

        // 1. Remove dynamically resolved ASIO key
        char asioKey[256];
        StringCchPrintfA(asioKey, 256, "SOFTWARE\\ASIO\\%s", dynamicName);
        RegDeleteKeyA(HKEY_LOCAL_MACHINE, asioKey);
        
        // Also enthusiastically wipe the fallback name just in case the dynamic state changed
        StringCchPrintfA(asioKey, 256, "SOFTWARE\\ASIO\\%s", DRIVER_NAME);
        RegDeleteKeyA(HKEY_LOCAL_MACHINE, asioKey);
        
        // Clean up legacy UMC Ultra bug footprint if present for this CLSID (clean uninstall logic)
        RegDeleteKeyA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO\\UMC Ultra");

        // 2. Remove COM registration
        char clsidKey[256];
        
        // Remove InprocServer32 first (must delete subkeys before parent)
        StringCchPrintfA(clsidKey, 256, "CLSID\\%s\\InprocServer32", DRIVER_CLSID);
        RegDeleteKeyA(HKEY_CLASSES_ROOT, clsidKey);
        
        StringCchPrintfA(clsidKey, 256, "CLSID\\%s", DRIVER_CLSID);
        RegDeleteKeyA(HKEY_CLASSES_ROOT, clsidKey);

        LOG_INFO(LOG_MODULE, "ASIO driver unregistered");
        return S_OK;
    }
    catch (...) {
        OutputDebugStringA("DllUnregisterServer: C++ exception caught\n");
        return E_UNEXPECTED;
    }
}

