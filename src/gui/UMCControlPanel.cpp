#include <windows.h>
#include <shellapi.h>
#include <string>
#include <thread>
#include <cstdio>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include "../license/LicenseManager.h"
#include "OptimizerManager.h"

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_EXIT 1002
#define ID_TRAY_ACTIVATE 1003
#define ID_TRAY_HW_PANEL 1004

HINSTANCE g_hInstance;
HWND g_hwnd;
NOTIFYICONDATAW g_nid;
LicenseManager g_license;

void ShowContextMenu(HWND hwnd);
HICON ExtractOfficialIcon();

void UpdateASIORegistryDescription() {
    // Obsolete: Registration is now dynamically cleaned inside LicenseManager.h purifyReg callback!
}

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_ACTIVATE, L"ASIO Ultra Panel");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_HW_PANEL, L"硬件底层设置");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"完全退出 Ultra Panel");
    
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

// ==============================
// 顶级暗黑模式 UI 引擎 (超越原生 Win32 限制，采用全像素定位与平滑按钮)
// ==============================

HFONT g_hFontTitle = NULL;
HFONT g_hFontBody = NULL;

void RefreshUIPanelText(HWND hwndDlg) {
    bool activated = g_license.checkCachedActivation(true);
    HWND hBtn = GetDlgItem(hwndDlg, 202);
    if (hBtn) {
        if (activated) {
            SetWindowTextW(hBtn, L"查看授权");
        } else {
            SetWindowTextW(hBtn, L"更新授权");
        }
    }
}

LRESULT CALLBACK CustomUIPanelProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            g_hFontTitle = CreateFontW(-24, 0, 0, 0, FW_BOLD, 0, 0, 0, GB2312_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
            g_hFontBody = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, GB2312_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");

            CreateWindowW(L"BUTTON", L"更新授权", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 30, 180, 170, 38, hwnd, (HMENU)202, g_hInstance, NULL);
            CreateWindowW(L"BUTTON", L"全局中枢状态", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 215, 180, 170, 38, hwnd, (HMENU)203, g_hInstance, NULL);
            CreateWindowW(L"BUTTON", L"系统通道深度隔离工具", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 30, 230, 355, 38, hwnd, (HMENU)204, g_hInstance, NULL);

            RefreshUIPanelText(hwnd);
            
            // 实时刷新倒计时/失效状态 (1000ms 刷新率)
            SetTimer(hwnd, 1, 1000, NULL);
            return 0;
        }
        
        case WM_TIMER: {
            if (wParam == 1) {
                RefreshUIPanelText(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        
        case WM_ERASEBKGND:
            return TRUE; // 防闪烁：告知系统已手动清理背景

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            HBRUSH bgBrush = CreateSolidBrush(RGB(22, 24, 28)); // Deep premium black
            FillRect(hdc, &clientRect, bgBrush);
            DeleteObject(bgBrush);
            
            SetBkMode(hdc, TRANSPARENT);
            
            bool activated = g_license.checkCachedActivation(true);
            int rem = g_license.getTrialMinutesRemaining();

            SelectObject(hdc, g_hFontTitle);
            SetTextColor(hdc, RGB(255, 255, 255));
            TextOutW(hdc, 30, 24, L"ASIO Ultra", 10);
            
            SelectObject(hdc, g_hFontBody);
            if (activated) {
                SetTextColor(hdc, RGB(0, 210, 120)); // Mint Green
                TextOutW(hdc, 175, 34, L"授权激活", 4);
            } else if (rem > 0) {
                SetTextColor(hdc, RGB(255, 170, 0)); // Warning Orange
                wchar_t badge[64];
                swprintf(badge, 64, L"体验模式 (%d MIN)", rem);
                TextOutW(hdc, 175, 34, badge, wcslen(badge));
            } else {
                SetTextColor(hdc, RGB(255, 60, 60)); // Error Red
                TextOutW(hdc, 175, 34, L"未激活", 3);
            }

            HPEN divPen = CreatePen(PS_SOLID, 1, RGB(40, 44, 48));
            HGDIOBJ oldPen = SelectObject(hdc, divPen);
            MoveToEx(hdc, 30, 70, NULL);
            LineTo(hdc, clientRect.right - 30, 70);
            SelectObject(hdc, oldPen);
            DeleteObject(divPen);

            SelectObject(hdc, g_hFontBody);
            RECT textRect = { 30, 90, clientRect.right - 30, 175 };
            
            if (activated) {
                SetTextColor(hdc, RGB(180, 190, 200));
                DrawTextW(hdc, L"▶ 虚拟通道音频链路已完全解锁\n▶ 您的全部功能均处于全量运作状态。您可以随时在此检查您的授权存根与版本信息。", -1, &textRect, DT_LEFT | DT_WORDBREAK);
            } else if (rem > 0) {
                SetTextColor(hdc, RGB(180, 190, 200));
                DrawTextW(hdc, L"▶ 核心音频驱动及系统交互沙盒正在体验期间运行。\n▶温馨提示：授权期终结后底层的音频传输将会自动挂起。为保障工作流始终顺畅，您可以随时补充并更新密钥激活。", -1, &textRect, DT_LEFT | DT_WORDBREAK);
            } else {
                SetTextColor(hdc, RGB(220, 140, 140));
                DrawTextW(hdc, L"▶ 为保障系统平稳，物理发声与拾音主干线现已切换为挂起拦截模式。\n▶点击下方获取激活密钥，所有声卡原生的全量极速音频流即可瞬间无缝回放。", -1, &textRect, DT_LEFT | DT_WORDBREAK);
            }
            
            EndPaint(hwnd, &ps);
            return 0;
        }

        // Removed WM_CTLCOLORSTATIC as STATIC strings are now dynamically drawn via Native WM_PAINT

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT lpDrawItem = (LPDRAWITEMSTRUCT)lParam;
            if (lpDrawItem->CtlType == ODT_BUTTON) {
                HDC hdc = lpDrawItem->hDC;
                RECT rect = lpDrawItem->rcItem;

                COLORREF bgColor = RGB(45, 48, 52); 
                if (lpDrawItem->itemState & ODS_SELECTED) {
                    bgColor = RGB(65, 70, 75); 
                }
                
                if (lpDrawItem->CtlID == 202) {
                    bgColor = (lpDrawItem->itemState & ODS_SELECTED) ? RGB(0, 95, 180) : RGB(0, 115, 205);
                }

                // Erase raw white corner artifacts by pre-filling the exact background color
                HBRUSH bgClearBrush = CreateSolidBrush(RGB(22, 24, 28));
                FillRect(hdc, &rect, bgClearBrush);
                DeleteObject(bgClearBrush);

                HBRUSH brush = CreateSolidBrush(bgColor);
                HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(30, 34, 38));
                HGDIOBJ oldPen = SelectObject(hdc, borderPen);
                HGDIOBJ oldBrush = SelectObject(hdc, brush);
                
                RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 12, 12); // Smooth 12px border radius
                
                SelectObject(hdc, oldBrush);
                SelectObject(hdc, oldPen);
                DeleteObject(brush);
                DeleteObject(borderPen);

                wchar_t btnText[128];
                GetWindowTextW(lpDrawItem->hwndItem, btnText, 128);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(255, 255, 255));
                SelectObject(hdc, g_hFontBody);
                DrawTextW(hdc, btnText, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            return TRUE;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == 202) { 
                g_license.showActivationDialog(hwnd);
                
                // 授权状态发生改变时，同步写入注册表给 DAW 内显使用
                UpdateASIORegistryDescription();
                
                // 刷新界面内显文本
                RefreshUIPanelText(hwnd);
            } else if (LOWORD(wParam) == 203) { 
                MessageBoxW(hwnd, L"硬件设置已由 ASMRTOP 全局智能代理全权接管。\n您的底层硬件已处于安全的脱机流控模式，无需也无法通过此入口直接操作物理层设备。", L"ASMRTOP 代理守卫网络", MB_ICONINFORMATION | MB_OK);
            } else if (LOWORD(wParam) == 204) {
                // 原生回归：不再以独立外部分支执行优化器，而是直接内嵌弹窗渲染优化器架构
                OptimizerManager::ShowDialog(hwnd);
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;

        case WM_DESTROY:
            // 不销毁全局字体句柄，防止多实例竞争时字体变回默认像素点阵格式
            break;

        default:
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

void ShowCustomDarkPanel(HWND parent) {
    const wchar_t* PANEL_CLASS = L"UMC_UltraDarkPanel";
    
    // 如果已经点出来了，绝对不允许多开，而是强制把它拉到最顶层
    HWND hExisting = FindWindowW(PANEL_CLASS, L"ASIO Ultra Control Panel");
    if (hExisting) {
        ShowWindow(hExisting, SW_RESTORE);
        SetForegroundWindow(hExisting);
        return;
    }
    WNDCLASSW wc = {};
    if (!GetClassInfoW(g_hInstance, PANEL_CLASS, &wc)) {
        wc.lpfnWndProc = CustomUIPanelProc;
        wc.hInstance = g_hInstance;
        wc.lpszClassName = PANEL_CLASS;
        wc.hIcon = ExtractOfficialIcon();
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(22, 24, 28)); 
        RegisterClassW(&wc);
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = 425; 
    int winH = 335; 
    int x = (screenW - winW) / 2;
    int y = (screenH - winH) / 2;

    HWND hPanel = CreateWindowExW(
        WS_EX_TOPMOST, PANEL_CLASS, L"ASIO Ultra Control Panel",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        x, y, winW, winH, NULL, NULL, g_hInstance, NULL
    );

    if (hPanel) {
        BOOL value = TRUE;
        DwmSetWindowAttribute(hPanel, 20, &value, sizeof(value)); 
        ShowWindow(hPanel, SW_SHOW);
        UpdateWindow(hPanel);
    }
}
// ==============================

// 动态黑金重绘引擎：截获官方图标并在内存中物理修改像素
HICON RecolorIconToBlackGold(HICON hInstIcon) {
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

HICON ExtractOfficialIcon() {
    // 全局通用化：不再通过硬编码路径窃取百灵达官方的 U 盾图标
    // 原生接入通用底座或资源文件 101 的极夜黑卡矢量图
    HMODULE hMod = GetModuleHandleW(NULL);
    HICON hIcon = LoadIconW(hMod, MAKEINTRESOURCEW(101)); 
    if (!hIcon) hIcon = LoadIconW(NULL, (LPCWSTR)IDI_SHIELD);
    return hIcon;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            g_nid.cbSize = sizeof(NOTIFYICONDATAW);
            g_nid.hWnd = hwnd;
            g_nid.uID = ID_TRAY_APP_ICON;
            g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
            g_nid.uCallbackMessage = WM_TRAYICON;
            g_nid.hIcon = ExtractOfficialIcon();
            wcscpy_s(g_nid.szTip, L"ASIO Ultra Control Panel");
            wcscpy_s(g_nid.szInfo, L"UMC 引擎已启动");
            wcscpy_s(g_nid.szInfoTitle, L"ASIO Ultra v7.0.0");
            g_nid.dwInfoFlags = NIIF_INFO;
            Shell_NotifyIconW(NIM_ADD, &g_nid);
            break;

        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP) {
                ShowContextMenu(hwnd);
            } else if (lParam == WM_LBUTTONDBLCLK) {
                PostMessageW(hwnd, WM_COMMAND, ID_TRAY_ACTIVATE, 0);
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
            } else if (LOWORD(wParam) == ID_TRAY_ACTIVATE) {
                ShowCustomDarkPanel(hwnd);
            } else if (LOWORD(wParam) == ID_TRAY_HW_PANEL) {
                const wchar_t* paths[] = {
                    L"C:\\Program Files\\Behringer\\UMC_Audio_Driver\\UMCAudioCplApp.exe",
                    L"C:\\Program Files\\BEHRINGER\\UMC_Audio_Driver\\x64\\UMCAudioCplApp.exe"
                };
                bool found = false;
                for (int i = 0; i < 2; i++) {
                    if (GetFileAttributesW(paths[i]) != INVALID_FILE_ATTRIBUTES) {
                        std::wstring execPath = paths[i];
                        std::wstring workingDir = execPath.substr(0, execPath.find_last_of(L"\\/"));
                        ShellExecuteW(NULL, L"open", paths[i], NULL, workingDir.c_str(), SW_SHOW);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    MessageBoxW(hwnd, L"未检测到官方原厂核心组件 (UMCAudioCplApp.exe)。\n请确认您是否安装了原厂 USB 驱动包。", L"安全阻断", MB_ICONINFORMATION | MB_OK);
                }
            }
            break;

        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // 【终极并发锁：防刺穿原子锁机制】
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"ASIOUltraControlPanel_Atomic_Mutex");
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS || err == ERROR_ACCESS_DENIED) {
        // 如果互斥量已完全占用，或者是由于不同用户权限引发的拒绝访问，立刻执行越权托盘唤醒通信！
        HWND hExistingDaemon = FindWindowW(L"ASIOUltraCPLClass", L"ASIO Ultra CPL Hidden"); 
        if (hExistingDaemon) {
            PostMessageW(hExistingDaemon, WM_COMMAND, ID_TRAY_ACTIVATE, 0);
        }
        HWND hPanel = FindWindowW(L"UMC_UltraDarkPanel", L"ASIO Ultra Control Panel");
        if (hPanel) {
            ShowWindow(hPanel, SW_RESTORE);
            SetForegroundWindow(hPanel);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0; // 退出当前所有重复挂载请求，杜绝多开
    }
    
    g_hInstance = hInstance;
    const wchar_t CLASS_NAME[] = L"ASIOUltraCPLClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = ExtractOfficialIcon();
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(
        0, CLASS_NAME, L"ASIO Ultra CPL Hidden",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL);

    if (g_hwnd == NULL) return 0;
    
    // 强制突破微软 UIPI 屏障 (User Interface Privilege Isolation)
    // 使得哪怕宿主 DAW 是最高管理员权限、或者是普通桌面用户权限启动，这道门槛统统被撕裂，互通无阻：
    ChangeWindowMessageFilterEx(g_hwnd, WM_COMMAND, MSGFLT_ALLOW, NULL);

    ShowWindow(g_hwnd, SW_HIDE);
    UpdateWindow(g_hwnd);

    // 每次启动托盘时强制同步一次注册表到宿主内显，实现 "控制台控制宿主" 效果
    UpdateASIORegistryDescription();

    PostMessageW(g_hwnd, WM_COMMAND, ID_TRAY_ACTIVATE, 0);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
