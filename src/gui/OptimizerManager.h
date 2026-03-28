#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <winreg.h>

#pragma comment(lib, "comctl32.lib")

struct AsioItem {
    std::wstring name;
    std::wstring clsid;
    std::wstring desc;
    bool hidden;
};

struct StudioOneItem {
    std::wstring name; // e.g. "Studio One 6"
    std::wstring path;
    bool hidden;
};

class OptimizerManager {
public:
    static void ShowDialog(HWND parent);
    static void ScanSystem();
    static void ApplyChanges();

    static std::vector<AsioItem> s_asioItems;
    static std::vector<StudioOneItem> s_s1Items;
    
    static HWND s_hAsioList;
    static HWND s_hS1List;
};
