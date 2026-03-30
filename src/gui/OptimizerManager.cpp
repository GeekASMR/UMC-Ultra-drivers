#include "OptimizerManager.h"
#include <shlwapi.h>
#include <iostream>
#include <vector>
#include <dwmapi.h>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

std::vector<AsioItem> OptimizerManager::s_asioItems;
std::vector<StudioOneItem> OptimizerManager::s_s1Items;
HWND OptimizerManager::s_hAsioList = NULL;
HWND OptimizerManager::s_hS1List = NULL;

#include <string>
#include <regex>
#include <fstream>
#include <shlobj.h>

// Helper: Get all installed Studio One AudioEngine settings files
static std::vector<std::wstring> GetStudioOneSettingsFiles() {
    std::vector<std::wstring> files;
    wchar_t appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
        std::wstring preSonusPath = std::wstring(appdata) + L"\\PreSonus";
        WIN32_FIND_DATAW fdw;
        HANDLE hFind = FindFirstFileW((preSonusPath + L"\\Studio One *").c_str(), &fdw);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (fdw.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    std::wstring dirName = fdw.cFileName;
                    std::wstring fullPath = preSonusPath + L"\\" + dirName + L"\\x64\\AudioEngine.settings";
                    if (GetFileAttributesW(fullPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        files.push_back(fullPath);
                    }
                }
            } while (FindNextFileW(hFind, &fdw));
            FindClose(hFind);
        }
    }
    return files;
}

static std::string ReadFileUtf8(const std::wstring& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (in) {
        std::string contents;
        in.seekg(0, std::ios::end);
        contents.resize(in.tellg());
        in.seekg(0, std::ios::beg);
        in.read(&contents[0], contents.size());
        in.close();
        return contents;
    }
    return "";
}

static void WriteFileUtf8(const std::wstring& path, const std::string& contents) {
    std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (out) {
        out.write(contents.data(), contents.size());
        out.close();
    }
}

static std::vector<std::wstring> GetS1HiddenClsids() {
    std::vector<std::wstring> hidden_clsids;
    auto files = GetStudioOneSettingsFiles();
    std::regex failedRgx("failedDevices=\"([^\"]+)\"");
    for (const auto& f : files) {
        std::string utf8 = ReadFileUtf8(f);
        std::smatch match;
        if (std::regex_search(utf8, match, failedRgx)) {
            std::string found = match[1].str();
            size_t pos = 0;
            while ((pos = found.find("{")) != std::string::npos) {
                size_t end = found.find("}", pos);
                if (end != std::string::npos) {
                    std::string clsidStr = found.substr(pos, end - pos + 1);
                    std::wstring wClsid(clsidStr.begin(), clsidStr.end());
                    hidden_clsids.push_back(wClsid);
                    found.erase(0, end + 1);
                } else break;
            }
        }
    }
    return hidden_clsids;
}

static void readAsioRoot(HKEY hRoot, bool isLegacyHiddenPath, std::vector<AsioItem>& items, DWORD viewFlag, const std::vector<std::wstring>& s1HiddenClsids) {
    std::wstring basePath = isLegacyHiddenPath ? L"SOFTWARE\\ASIO_HIDDEN" : L"SOFTWARE\\ASIO";
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, basePath.c_str(), 0, KEY_READ | viewFlag, &hKey) == ERROR_SUCCESS) {
        wchar_t subName[256];
        DWORD ind = 0;
        while (true) {
            DWORD len = 256;
            if (RegEnumKeyExW(hKey, ind++, subName, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            
            std::wstring fpath = basePath + L"\\" + subName;
            HKEY hSub;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, fpath.c_str(), 0, KEY_READ | viewFlag, &hSub) == ERROR_SUCCESS) {
                wchar_t clsid[256] = {0}, desc[256] = {0};
                DWORD clen = sizeof(clsid), dlen = sizeof(desc);
                RegQueryValueExW(hSub, L"CLSID", 0, NULL, (BYTE*)clsid, &clen);
                RegQueryValueExW(hSub, L"Description", 0, NULL, (BYTE*)desc, &dlen);
                
                std::wstring sn = subName;
                bool dup = false;
                for (auto& item : items) {
                    if (item.name == sn) { dup = true; break; }
                }
                
                if (!dup && sn != L"UMC Ultra" && sn != L"UMC Ultra By ASMRTOP") {
                    bool isEffectivelyHidden = isLegacyHiddenPath;
                    if (!isLegacyHiddenPath) {
                        for (const auto& hc : s1HiddenClsids) {
                            if (_wcsicmp(hc.c_str(), clsid) == 0) {
                                isEffectivelyHidden = true;
                                break;
                            }
                        }
                    }
                    items.push_back({sn, clsid, desc, isEffectivelyHidden});
                }
                RegCloseKey(hSub);
            }
        }
        RegCloseKey(hKey);
    }
}

void OptimizerManager::ScanSystem() {
    s_asioItems.clear();
    s_s1Items.clear();

    // 1. Scan ASIO (both architectures defensively)
    auto s1Hidden = GetS1HiddenClsids();
    readAsioRoot(HKEY_LOCAL_MACHINE, false, s_asioItems, KEY_WOW64_64KEY, s1Hidden);
    readAsioRoot(HKEY_LOCAL_MACHINE, false, s_asioItems, KEY_WOW64_32KEY, s1Hidden);
    readAsioRoot(HKEY_LOCAL_MACHINE, true, s_asioItems, KEY_WOW64_64KEY, s1Hidden);
    readAsioRoot(HKEY_LOCAL_MACHINE, true, s_asioItems, KEY_WOW64_32KEY, s1Hidden);

    // 2. Scan Studio One / Studio Pro 8 locations (Simulated recursive PS sweep)
    const wchar_t* drives[] = { L"C:\\", L"D:\\", L"E:\\", L"F:\\" };
    for (int i=0; i<4; i++) {
        std::wstring pf = std::wstring(drives[i]) + L"Program Files\\PreSonus";
        if (GetFileAttributesW(pf.c_str()) != INVALID_FILE_ATTRIBUTES) {
            WIN32_FIND_DATAW fdw;
            HANDLE hFind = FindFirstFileW((pf + L"\\*").c_str(), &fdw);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (fdw.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        std::wstring dirName = fdw.cFileName;
                        if (dirName != L"." && dirName != L"..") {
                            std::wstring fullPath = pf + L"\\" + dirName;
                            std::wstring dllNormal = fullPath + L"\\Plugins\\windowsaudio.dll";
                            std::wstring dllBak = fullPath + L"\\Plugins\\windowsaudio.dll.bak";
                            
                            bool hasNormal = (GetFileAttributesW(dllNormal.c_str()) != INVALID_FILE_ATTRIBUTES);
                            bool hasBak = (GetFileAttributesW(dllBak.c_str()) != INVALID_FILE_ATTRIBUTES);
                            
                            if (hasNormal || hasBak) {
                                s_s1Items.push_back({ dirName, fullPath, !hasNormal && hasBak });
                            }
                        }
                    }
                } while (FindNextFileW(hFind, &fdw));
                FindClose(hFind);
            }
        }
    }
    
    // Check Registry for other installations
    HKEY hReg;
    const wchar_t* keys[] = { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall" };
    for (int k=0; k<2; k++) {
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keys[k], 0, KEY_READ, &hReg) == ERROR_SUCCESS) {
            wchar_t subName[256];
            DWORD ind = 0;
            while (true) {
                DWORD len = 256;
                if (RegEnumKeyExW(hReg, ind++, subName, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
                HKEY hSub;
                if (RegOpenKeyExW(hReg, subName, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
                    wchar_t disp[256]={0}, loc[512]={0};
                    DWORD s1 = sizeof(disp), s2 = sizeof(loc);
                    if (RegQueryValueExW(hSub, L"DisplayName", 0, NULL, (BYTE*)disp, &s1) == ERROR_SUCCESS && 
                        RegQueryValueExW(hSub, L"InstallLocation", 0, NULL, (BYTE*)loc, &s2) == ERROR_SUCCESS) {
                        
                        std::wstring sDisp = disp;
                        if (sDisp.find(L"Studio One") != std::wstring::npos) {
                            std::wstring fullPath = loc;
                            while (!fullPath.empty() && (fullPath.back() == L'\\' || fullPath.back() == L'"')) fullPath.pop_back();
                            if (fullPath.front() == L'"') fullPath.erase(0, 1);
                            
                            std::wstring dllNormal = fullPath + L"\\Plugins\\windowsaudio.dll";
                            std::wstring dllBak = fullPath + L"\\Plugins\\windowsaudio.dll.bak";
                            bool hasNormal = (GetFileAttributesW(dllNormal.c_str()) != INVALID_FILE_ATTRIBUTES);
                            bool hasBak = (GetFileAttributesW(dllBak.c_str()) != INVALID_FILE_ATTRIBUTES);
                            
                            if ((hasNormal || hasBak) && fullPath.length() > 5) {
                                // Prevent duplicates
                                bool dup = false;
                                for (auto& item : s_s1Items) {
                                    if (item.path == fullPath) dup = true;
                                }
                                if (!dup) {
                                    s_s1Items.push_back({ sDisp, fullPath, !hasNormal && hasBak });
                                }
                            }
                        }
                    }
                    RegCloseKey(hSub);
                }
            }
            RegCloseKey(hReg);
        }
    }
}

void OptimizerManager::ApplyChanges() {
    // Note: Applying requires UAC. If the UI runs as Admin (like DAW context), it succeeds directly.
    // If not, it will silently fail or we need to restart as Admin.
    // Handle ASIO hiding using Studio One's XML failedDevices blacklist mechanism,
    // leaving physical registry completely intact for ASIO Link Pro.
    std::vector<std::string> clsidsToHide;
    
    for (int i=0; i<ListView_GetItemCount(s_hAsioList); i++) {
        bool wantsHidden = ListView_GetCheckState(s_hAsioList, i);
        std::wstring sn = s_asioItems[i].name;
        std::wstring sc = s_asioItems[i].clsid;
        std::wstring sd = s_asioItems[i].desc;
        
        // Cleanse CLSID string of any trailing nulls or garbage from registry buffer
        std::wstring pureSc = sc;
        size_t ppos = pureSc.find(L'{');
        size_t pend = pureSc.find(L'}');
        if (ppos != std::wstring::npos && pend != std::wstring::npos && pend > ppos) {
            pureSc = pureSc.substr(ppos, pend - ppos + 1);
        }
        
        if (wantsHidden && !pureSc.empty()) {
            clsidsToHide.push_back(std::string(pureSc.begin(), pureSc.end()));
        }

        // Always rigidly enforce physical registry is fully visible in global WOW64 layers
        // Copy to standard ASIO from ASIO_HIDDEN if necessary, and delete legacy ASIO_HIDDEN keys
        std::wstring src64 = L"SOFTWARE\\ASIO_HIDDEN\\" + sn;
        std::wstring src32 = L"SOFTWARE\\WOW6432Node\\ASIO_HIDDEN\\" + sn;
        std::wstring dest64 = L"SOFTWARE\\ASIO\\" + sn;
        std::wstring dest32 = L"SOFTWARE\\WOW6432Node\\ASIO\\" + sn;
        
        DWORD views[] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
        std::wstring dests[] = { dest64, dest32 };
        std::wstring srcs[] = { src64, src32 };
        
        for(int v=0; v<2; v++) {
            HKEY hDest;
            if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, dests[v].c_str(), 0, NULL, 0, KEY_WRITE | views[v], NULL, &hDest, NULL) == ERROR_SUCCESS) {
                if (!pureSc.empty()) RegSetValueExW(hDest, L"CLSID", 0, REG_SZ, (BYTE*)pureSc.c_str(), (pureSc.length()+1)*2);
                if (!sd.empty()) RegSetValueExW(hDest, L"Description", 0, REG_SZ, (BYTE*)sd.c_str(), (sd.length()+1)*2);
                RegCloseKey(hDest);
            }
            SHDeleteKeyW(HKEY_LOCAL_MACHINE, srcs[v].c_str());
        }
        s_asioItems[i].hidden = wantsHidden;
    }

    // Securely overwrite Studio One XML settings
    auto files = GetStudioOneSettingsFiles();
    std::regex attRgx("<Attributes([^>]+)>");
    std::regex failedRgx("failedDevices=\"([^\"]+)\"");

    for (const auto& f : files) {
        std::string utf8 = ReadFileUtf8(f);
        if (utf8.empty()) continue;

        std::smatch matchAtt;
        if (std::regex_search(utf8, matchAtt, attRgx)) {
            std::string attStr = matchAtt[1].str();
            
            std::vector<std::string> existingFailed;
            std::smatch matchFail;
            if (std::regex_search(attStr, matchFail, failedRgx)) {
                std::string found = matchFail[1].str();
                size_t pos = 0;
                while ((pos = found.find("{")) != std::string::npos) {
                    size_t end = found.find("}", pos);
                    if (end != std::string::npos) {
                        existingFailed.push_back(found.substr(pos, end - pos + 1));
                        found.erase(0, end + 1);
                    } else break;
                }
            }

            // Strip CLSIDs that are managed by our UI from the blacklists
            std::vector<std::string> newFailed;
            for (const auto& ef : existingFailed) {
                bool managed = false;
                for (auto& item : s_asioItems) {
                    std::wstring p1 = item.clsid;
                    size_t cp = p1.find(L'{'); size_t ce = p1.find(L'}');
                    if (cp != std::wstring::npos && ce != std::wstring::npos) p1 = p1.substr(cp, ce - cp + 1);
                    std::string narrowSc(p1.begin(), p1.end());
                    if (_stricmp(ef.c_str(), narrowSc.c_str()) == 0) {
                        managed = true; break;
                    }
                }
                if (!managed && ef.length() == 38) newFailed.push_back(ef);
            }
            // Add clsids our UI designates as hidden
            for (const auto& hd : clsidsToHide) {
                if (hd.length() == 38) newFailed.push_back(hd);
            }

            std::string failStr = "";
            for (size_t i=0; i<newFailed.size(); ++i) {
                failStr += newFailed[i];
                if (i < newFailed.size() - 1) failStr += ",";
            }

            if (std::regex_search(attStr, matchFail, failedRgx)) {
                if (failStr.empty()) {
                    attStr = std::regex_replace(attStr, failedRgx, "");
                } else {
                    attStr = std::regex_replace(attStr, failedRgx, "failedDevices=\"" + failStr + "\"");
                }
            } else {
                if (!failStr.empty()) {
                    attStr += " failedDevices=\"" + failStr + "\"";
                }
            }
            
            utf8 = std::regex_replace(utf8, attRgx, "<Attributes" + attStr + ">");
            WriteFileUtf8(f, utf8);
        }
    }

    for (int i=0; i<ListView_GetItemCount(s_hS1List); i++) {
        bool wantsHidden = ListView_GetCheckState(s_hS1List, i);
        if (s_s1Items[i].hidden != wantsHidden) {
            std::wstring normal = s_s1Items[i].path + L"\\Plugins\\windowsaudio.dll";
            std::wstring bak = s_s1Items[i].path + L"\\Plugins\\windowsaudio.dll.bak";
            
            if (wantsHidden) {
                if (!MoveFileW(normal.c_str(), bak.c_str())) {
                    MoveFileExW(normal.c_str(), bak.c_str(), MOVEFILE_DELAY_UNTIL_REBOOT | MOVEFILE_REPLACE_EXISTING);
                }
            } else {
                if (!MoveFileW(bak.c_str(), normal.c_str())) {
                    MoveFileExW(bak.c_str(), normal.c_str(), MOVEFILE_DELAY_UNTIL_REBOOT | MOVEFILE_REPLACE_EXISTING);
                }
            }
            s_s1Items[i].hidden = wantsHidden;
        }
    }
}

static LRESULT CALLBACK OptDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_INITDIALOG || msg == WM_CREATE) {
        // Init List Views
        // ...
        return TRUE;
    }
    return FALSE;
}

// 动态黑金重绘引擎：截获官方图标并在内存中物理修改像素
static HICON RecolorIconToBlackGold(HICON hInstIcon) {
    if (!hInstIcon) return NULL;
    ICONINFO ii;
    if (!GetIconInfo(hInstIcon, &ii)) return hInstIcon;

    BITMAP bmp;
    GetObject(ii.hbmColor, sizeof(BITMAP), &bmp);
    int width = bmp.bmWidth;
    int height = bmp.bmHeight;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; 
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hbm32 = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    HGDIOBJ oldBmp = SelectObject(hdcMem, hbm32);

    DrawIconEx(hdcMem, 0, 0, hInstIcon, width, height, 0, NULL, DI_NORMAL);

    unsigned char* pixels = (unsigned char*)pBits;
    for (int i = 0; i < width * height * 4; i += 4) {
        int b = pixels[i];
        int g = pixels[i+1];
        int r = pixels[i+2];

        if (r == 0 && g == 0 && b == 0) continue;

        int brightness = (r + g + b) / 3;
        
        if (brightness > 180) { 
            pixels[i]   = 240;   
            pixels[i+1] = 240; 
            pixels[i+2] = 240; 
        } else if (brightness > 10) { 
            pixels[i]   = 20; 
            pixels[i+1] = 20; 
            pixels[i+2] = 20;
        }
    }

    SelectObject(hdcMem, oldBmp);

    ICONINFO newIi = {0};
    newIi.fIcon = TRUE;
    newIi.hbmMask = ii.hbmMask;
    newIi.hbmColor = hbm32;
    HICON hGoldIcon = CreateIconIndirect(&newIi);

    DeleteObject(hbm32);
    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return hGoldIcon ? hGoldIcon : hInstIcon;
}

static HICON ExtractOfficialIcon() {
    const wchar_t* paths[] = {
        L"C:\\Program Files\\Behringer\\UMC_Audio_Driver\\UMCAudioCplApp.exe",
        L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\UMCAudioCplApp.exe"
    };
    for (int i = 0; i < 2; i++) {
        if (GetFileAttributesW(paths[i]) != INVALID_FILE_ATTRIBUTES) {
            HICON hIcon = ExtractIconW(GetModuleHandle(NULL), paths[i], 0);
            if (hIcon && (uintptr_t)hIcon > 1) {
                return RecolorIconToBlackGold(hIcon);
            }
        }
    }
    return LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
}

// Window creation logic
void OptimizerManager::ShowDialog(HWND parent) {
    ScanSystem();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
        switch (m) {
            case WM_CREATE: {
                INITCOMMONCONTROLSEX icex;
                icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
                icex.dwICC = ICC_LISTVIEW_CLASSES;
                InitCommonControlsEx(&icex);

                HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, GB2312_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");                

                s_hAsioList = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_NOCOLUMNHEADER|WS_BORDER, 20, 40, 360, 120, h, NULL, NULL, NULL);
                ListView_SetExtendedListViewStyle(s_hAsioList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
                
                SendMessageW(s_hAsioList, LVM_SETBKCOLOR, 0, RGB(22, 24, 28));
                SendMessageW(s_hAsioList, LVM_SETTEXTBKCOLOR, 0, RGB(22, 24, 28));
                SendMessageW(s_hAsioList, LVM_SETTEXTCOLOR, 0, RGB(200, 210, 220));

                LVCOLUMNW col={0}; col.mask = LVCF_WIDTH; col.cx = 320; 
                SendMessageW(s_hAsioList, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
                
                for (int i=0; i<s_asioItems.size(); i++) {
                    LVITEMW it={0}; it.mask=LVIF_TEXT; it.iItem=i; it.pszText=(LPWSTR)s_asioItems[i].name.c_str();
                    SendMessageW(s_hAsioList, LVM_INSERTITEMW, 0, (LPARAM)&it);
                    ListView_SetCheckState(s_hAsioList, i, s_asioItems[i].hidden);
                }

                s_hS1List = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_NOCOLUMNHEADER|WS_BORDER, 20, 200, 360, 90, h, NULL, NULL, NULL);
                ListView_SetExtendedListViewStyle(s_hS1List, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
                
                SendMessageW(s_hS1List, LVM_SETBKCOLOR, 0, RGB(22, 24, 28));
                SendMessageW(s_hS1List, LVM_SETTEXTBKCOLOR, 0, RGB(22, 24, 28));
                SendMessageW(s_hS1List, LVM_SETTEXTCOLOR, 0, RGB(200, 210, 220));

                SendMessageW(s_hS1List, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);

                for (int i=0; i<s_s1Items.size(); i++) {
                    LVITEMW it={0}; it.mask=LVIF_TEXT; it.iItem=i; it.pszText=(LPWSTR)s_s1Items[i].name.c_str();
                    SendMessageW(s_hS1List, LVM_INSERTITEMW, 0, (LPARAM)&it);
                    ListView_SetCheckState(s_hS1List, i, s_s1Items[i].hidden);
                }

                HWND hBtnOK = CreateWindowW(L"BUTTON", L"应用并保存", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON|BS_OWNERDRAW, 240, 295, 140, 36, h, (HMENU)IDOK, NULL, NULL);
                
                SendMessageW(s_hAsioList, WM_SETFONT, (WPARAM)hFont, FALSE);
                SendMessageW(s_hS1List, WM_SETFONT, (WPARAM)hFont, FALSE);

                // 注入专属的 Uxtheme 黑客调用，强制内部原生滚动条渲染遵循 Dark 模式下的 Explorer 色盘
                SetWindowTheme(s_hAsioList, L"DarkMode_Explorer", NULL);
                SetWindowTheme(s_hS1List, L"DarkMode_Explorer", NULL);

                break;
            }
            case WM_COMMAND:
                if (LOWORD(w) == IDOK) {
                    OptimizerManager::ApplyChanges();
                    MessageBoxW(h, L"系统环境优化项已成功生效。\n请重启您的宿主软件 (DAW) 使其重载通道。", L"UMC Ultra 净化成功", MB_ICONINFORMATION);
                    DestroyWindow(h);
                }
                break;
            case WM_DRAWITEM: {
                LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)l;
                if (di->CtlID == IDOK) {
                    HDC hdc = di->hDC;
                    RECT r = di->rcItem;
                    HBRUSH b = CreateSolidBrush(RGB(10, 110, 210));
                    FillRect(hdc, &r, b);
                    DeleteObject(b);
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(255,255,255));
                    HFONT bf = CreateFontW(-16, 0, 0, 0, FW_BOLD, 0, 0, 0, GB2312_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
                    SelectObject(hdc, bf);
                    DrawTextW(hdc, L"应用并保存", -1, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                    DeleteObject(bf);
                    return TRUE;
                }
                break;
            }
            case WM_ERASEBKGND:
                return TRUE;
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(h, &ps);
                RECT r; GetClientRect(h, &r);
                HBRUSH b = CreateSolidBrush(RGB(22, 24, 28));
                FillRect(hdc, &r, b);
                DeleteObject(b);

                // Paint static texts manually or via CTLCOLORSTATIC depending on system. Quick hack is to just let default handle for child controls or paint text here directly.
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(180, 190, 200));
                HFONT f = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, GB2312_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
                SelectObject(hdc, f);
                std::wstring text1 = L"隐藏宿主中其他多余 ASIO 驱动卡（独占控制权）:";
                std::wstring text2 = L"屏蔽宿主 (Studio One) 中的 Windows Audio 保底通道:";
                TextOutW(hdc, 20, 18, text1.c_str(), text1.length());
                TextOutW(hdc, 20, 178, text2.c_str(), text2.length());
                DeleteObject(f);

                EndPaint(h, &ps);
                return 0;
            }
            case WM_DESTROY:
                PostQuitMessage(0);
                break;
        }
        return DefWindowProcW(h, m, w, l);
    };
    
    HICON hOfficialIcon = ExtractOfficialIcon();
    
    wc.hIcon = hOfficialIcon;
    wc.lpszClassName = L"UMC_Optimizer";
    RegisterClassW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"UMC_Optimizer", L"通道环境深度净化",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        (sw - 400)/2, (sh - 380)/2, 400, 380, parent, NULL, NULL, NULL);

    BOOL val = TRUE;
    DwmSetWindowAttribute(hDlg, 19, &val, sizeof(val));
    DwmSetWindowAttribute(hDlg, 20, &val, sizeof(val));
    SetWindowPos(hDlg, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    ShowWindow(hDlg, SW_SHOW);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}
