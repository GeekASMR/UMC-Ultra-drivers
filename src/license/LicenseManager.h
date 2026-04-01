/*
 * LicenseManager.h - ASIO Ultra 在线激活 + 离线缓存授权系统
 *
 * 流程:
 *   1. 首次安装: 自动开始 7 天试用期
 *   2. 试用期内: 正常使用全部功能
 *   3. 试用到期: init() 弹出激活窗口, 用户输入 License Key
 *   4. 激活: POST(key, machineId) → 服务器验签 → 本地缓存 token
 *   5. 后续启动: 读取本地缓存 token, 离线验证有效性
 *
 * 服务端: https://asmrtop.cn/api/activate
 */

#pragma once
#include <windows.h>
#include <shellapi.h>
#include <thread>
#include <winhttp.h>
#include <wincrypt.h>
#include <fstream>
#include <mutex>
#include <cstdarg>
#include <vector>
#include <string>
#include <cstdio>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include <ctime>
#include "../UMCVersion.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")

#include <utility>
#include <string>
#include <winhttp.h>
#include <string>
#include <winhttp.h>

// =========================================================================
// 编译期字符串加密混淆 (XOR String) - 防止静态逆向提取明文
// =========================================================================
template <typename IS> struct XorA;
template <size_t... I> 
struct XorA<std::index_sequence<I...>> {
    char data[sizeof...(I)];
    constexpr XorA(const char* s) : data{ (char)(s[I] ^ 0x69)... } {}
};

#define XCRYPT(s) ([]() -> std::string { \
    constexpr XorA<std::make_index_sequence<sizeof(s)>> xs(s); \
    std::string r(sizeof(s)-1, '\0'); \
    for(size_t i=0; i<sizeof(s)-1; ++i) r[i] = xs.data[i] ^ 0x69; \
    return r; \
}())

template <typename IS> struct XorW;
template <size_t... I> 
struct XorW<std::index_sequence<I...>> {
    wchar_t data[sizeof...(I)];
    constexpr XorW(const wchar_t* s) : data{ (wchar_t)(s[I] ^ 0x69)... } {}
};

#define WXCRYPT(s) ([]() -> std::wstring { \
    constexpr XorW<std::make_index_sequence<sizeof(s)/sizeof(wchar_t)>> xs(s); \
    std::wstring r(sizeof(s)/sizeof(wchar_t)-1, L'\0'); \
    for(size_t i=0; i<sizeof(s)/sizeof(wchar_t)-1; ++i) r[i] = xs.data[i] ^ 0x69; \
    return r; \
}())

#define LIC_REG_KEY    XCRYPT("SOFTWARE\\ASMRTOP\\ASIOUltra").c_str()
#define LIC_SERVER_HOST WXCRYPT(L"geek.asmrtop.cn").c_str()
#define LIC_SERVER_PATH WXCRYPT(L"/asio/activate.php").c_str()
#define LIC_VERIFY_SALT XCRYPT("UMC_ULTRA_2026_ASMRTOP_SEC").c_str()
#define LIC_VERIFY_PATH WXCRYPT(L"/asio/verify.php").c_str()
#define LIC_TRIAL_MINUTES 60

class LicenseManager {
public:
    enum Status {
        ACTIVE,       // 已激活
        TRIAL,        // 试用中
        EXPIRED,      // 试用已到期 / 授权已过期
        ERROR_STATE   // 内部错误
    };

    // =========================================================================
    // 反调试与防御沙箱 (Anti-Debug)
    // =========================================================================
    __forceinline void CheckEnvironment() {
        if (IsDebuggerPresent()) {
            ExitProcess(0); // 发现逆向调试器立马物理拔断电源，保护内存安全
        }
    }

    // =========================================================================
    // 主检查入口
    // =========================================================================
    Status check(int* trialDaysLeft = nullptr) {
        CheckEnvironment(); // 调用抗调试保护

        // 1. 检查本地缓存的激活信息
        if (checkCachedActivation()) {
            return ACTIVE;
        }

        // 2. 检查试用期
        int remaining = getTrialMinutesRemaining();
        if (trialDaysLeft) *trialDaysLeft = remaining;
        if (remaining > 0) {
            return TRIAL;
        }

        return EXPIRED;
    }

    // =========================================================================
    // 在线激活
    // =========================================================================
    bool activate(const std::string& licenseKey) {
        std::string machineId = getMachineId();

        // 构造 JSON 请求
        char body[512];
        snprintf(body, sizeof(body),
            "{\"key\":\"%s\",\"machine_id\":\"%s\"}",
            licenseKey.c_str(), machineId.c_str());

        // POST 到激活服务器
        std::string response;
        if (!httpPost(LIC_SERVER_HOST, LIC_SERVER_PATH, body, response)) {
            return false;
        }

        // 解析响应
        std::string status = jsonVal(response, "status");
        std::string token  = jsonVal(response, "token");
        std::string expiry = jsonVal(response, "expiry");

        if (status != "ok" || token.empty()) {
            return false;
        }

        // 验证 token 完整性
        std::string expected = computeToken(licenseKey, machineId, expiry);
        if (token != expected) {
            return false;
        }

        // 写入注册表缓存
        cacheActivation(licenseKey, machineId, expiry, token);
        return true;
    }

    // =========================================================================
    // 机器指纹
    // =========================================================================
    std::string getMachineId() {
        DWORD volSerial = 0;
        GetVolumeInformationA("C:\\", nullptr, 0, &volSerial,
                              nullptr, nullptr, nullptr, 0);

        char compName[MAX_COMPUTERNAME_LENGTH + 1] = {};
        DWORD nameSize = sizeof(compName);
        GetComputerNameA(compName, &nameSize);

        char raw[256];
        snprintf(raw, sizeof(raw), "UMCA-%08X-%s", volSerial, compName);

        return sha256(raw).substr(0, 16);
    }

    // =========================================================================
    // 激活对话框 (从 controlPanel 调用)
    // =========================================================================
    bool showActivationDialog(HWND parent = nullptr) {
        // 使用 Win32 Dialog 模板 (内存中创建, 无需 .rc 文件)
        // 布局: 标题 + 机器码显示 + 密钥输入框 + 激活按钮 + 取消按钮
        struct DlgData {
            LicenseManager* mgr;
            bool activated;
        } data = { this, false };

        // --- 在内存中构造 DLGTEMPLATE ---
        #pragma pack(push, 4)
        struct {
            DLGTEMPLATE dlg;
            WORD menu, cls, title;
            // 后续跟控件...
        } tmpl = {};
        #pragma pack(pop)

        // 使用 DialogBoxIndirectParam 太复杂, 改用简洁的 MessageBox 交互方式
        std::string machineId = getMachineId();

        int trialLeft = getTrialMinutesRemaining();

        Status st = check();
        if (st == ACTIVE) {
            char key[128] = {}, expiry[64] = {};
            HKEY hKey;
            if (RegOpenKeyExA(HKEY_CURRENT_USER, LIC_REG_KEY, 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS) {
                DWORD sz = sizeof(key);
                RegQueryValueExA(hKey, "LicenseKey", 0, 0, (BYTE*)key, &sz);
                sz = sizeof(expiry);
                RegQueryValueExA(hKey, "Expiry", 0, 0, (BYTE*)expiry, &sz);

                // --- 强制进行一次网络同步验证 (解决后台解绑后客户端状态滞后问题) ---
                if (strlen(key) >= 4) {
                    char body[512];
                    snprintf(body, sizeof(body), "{\"key\":\"%s\",\"machine_id\":\"%s\"}", key, machineId.c_str());
                    std::string response;
                    if (httpPost(LIC_SERVER_HOST, LIC_VERIFY_PATH, body, response)) {
                        if (jsonVal(response, "status") == "invalid") {
                            // 后台已被清空/解绑，立刻物理清除本地缓存并转为过期模式
                            RegDeleteValueA(hKey, "Token");
                            RegDeleteValueA(hKey, "Expiry");
                            st = EXPIRED; 
                            s_activated = false;
                        }
                    }
                }
                RegCloseKey(hKey);
            }
            
            if (st == ACTIVE) {
                wchar_t wMsg[1024];
                swprintf(wMsg, sizeof(wMsg)/sizeof(wchar_t),
                    L"ASIO Ultra  v%s\n\n"
                    L"许可状态: 已激活 ✓\n"
                    L"许可密钥: %S\n"
                    L"有效期至: %S\n"
                    L"机器码: %S",
                    UMC_VERSION_WSTR, key, expiry, machineId.c_str());
                wchar_t wTitle[256];
                swprintf(wTitle, sizeof(wTitle)/sizeof(wchar_t), L"ASIO Ultra Professional v%s", UMC_VERSION_WSTR);
                MessageBoxW(parent, wMsg, wTitle, MB_ICONINFORMATION);
                return true;
            }
        }

        if (st == TRIAL) {
            wchar_t wMsg[1024];
            swprintf(wMsg, sizeof(wMsg)/sizeof(wchar_t),
                L"ASIO Ultra  v%s\n\n"
                L"当前状态: 试用中 (剩余 %d 分钟)\n"
                L"机器码: %S\n\n"
                L"如需激活，请点击「是」输入许可密钥。\n"
                L"点击「否」继续试用。",
                UMC_VERSION_WSTR, trialLeft, machineId.c_str());
            wchar_t wTitle[256];
            swprintf(wTitle, sizeof(wTitle)/sizeof(wchar_t), L"ASIO Ultra Professional v%s", UMC_VERSION_WSTR);
            if (MessageBoxW(parent, wMsg, wTitle, MB_YESNO | MB_ICONQUESTION) != IDYES) {
                // 用户选择继续试用，允许通过！
                return true;
            }
        } else { // st == EXPIRED
            wchar_t wMsg[1024];
            swprintf(wMsg, sizeof(wMsg)/sizeof(wchar_t),
                L"ASIO Ultra  v%s\n\n"
                L"试用期已到期!\n"
                L"机器码: %S\n\n"
                L"请输入许可密钥以继续使用。\n",
                UMC_VERSION_WSTR, machineId.c_str());
            wchar_t wTitle[256];
            swprintf(wTitle, sizeof(wTitle)/sizeof(wchar_t), L"ASIO Ultra Professional v%s", UMC_VERSION_WSTR);
            MessageBoxW(parent, wMsg, wTitle, MB_ICONWARNING);
        }

        // 输入密钥 (使用简易输入框)
        bool activated = promptAndActivate(parent);
        
        // 如果过期且没激活成功，才不让过
        if (st == EXPIRED && !activated) {
            return false;
        }
        
        return true;
    }

private:
    // =========================================================================
    // 简易密钥输入弹窗
    // =========================================================================
    bool promptAndActivate(HWND parent) {
        s_instance = this;
        s_activated = false;

        if (!s_bgBrush) {
            s_bgBrush = CreateSolidBrush(RGB(22, 24, 28));
            s_editBrush = CreateSolidBrush(RGB(35, 38, 42));
        }

        // 注册窗口类
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.hInstance = GetModuleHandleW(L"BehringerASIO.dll");
        if (!wc.hInstance) wc.hInstance = GetModuleHandle(nullptr);

        WNDCLASSEXW tmpWc = {};
        tmpWc.cbSize = sizeof(WNDCLASSEXW);
        if (!GetClassInfoExW(wc.hInstance, L"UMC_LIC_INPUT", &tmpWc)) {
            wc.lpfnWndProc = inputWndProc;
            wc.lpszClassName = L"UMC_LIC_INPUT";
            wc.hbrBackground = s_bgBrush;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            RegisterClassExW(&wc);
        }

        wchar_t wTitleInput[256];
        swprintf(wTitleInput, sizeof(wTitleInput)/sizeof(wchar_t), L"ASIO Ultra Professional v%s - 授权激活", UMC_VERSION_WSTR);
        HWND hWnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
            L"UMC_LIC_INPUT", wTitleInput,
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 360, 200,
            nullptr, nullptr, wc.hInstance, nullptr);

        if (!hWnd) {
            DWORD err = GetLastError();
            wchar_t errBuf[256];
            swprintf(errBuf, 256, L"内部界面加载失败！\n\n类存在: %d\n错误码: %lu\n请联系技术支持。",
                GetClassInfoExW(wc.hInstance, L"UMC_LIC_INPUT", &tmpWc), err);
            MessageBoxW(nullptr, errBuf, L"错误", MB_ICONERROR);
            return false;
        }

        // 强行向下兼容应用 Windows 深色原生标题栏 (修复旧版 Win10 标题栏局部黑块渲染 Bug)
        BOOL value = TRUE;
        DwmSetWindowAttribute(hWnd, 19, &value, sizeof(value)); 
        DwmSetWindowAttribute(hWnd, 20, &value, sizeof(value));

        // 居中
        RECT rc;
        GetWindowRect(hWnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hWnd, HWND_TOPMOST, (sx - w) / 2, (sy - h) / 2, 0, 0, SWP_NOSIZE);

        // 创建控件
        HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            GB2312_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");

        auto makeCtrl = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                            int x, int y, int cw, int ch, int id) {
            HWND hC = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                x, y, cw, ch, hWnd, (HMENU)(INT_PTR)id, wc.hInstance, nullptr);
            SendMessageW(hC, WM_SETFONT, (WPARAM)hFont, TRUE);
            return hC;
        };

        makeCtrl(L"STATIC", L"请输入许可密钥:", 0, 15, 15, 310, 20, 100);
        HWND hEdit = makeCtrl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 15, 40, 310, 26, 101);
        makeCtrl(L"STATIC", L"格式: UMCA-XXXX-XXXX-XXXX", 0, 15, 72, 250, 18, 102);
        makeCtrl(L"BUTTON", L"激活", BS_DEFPUSHBUTTON | BS_OWNERDRAW, 150, 110, 80, 32, IDOK);
        makeCtrl(L"BUTTON", L"取消", BS_OWNERDRAW, 245, 110, 80, 32, IDCANCEL);
        makeCtrl(L"BUTTON", L"去购买卡密", BS_OWNERDRAW, 15, 110, 85, 32, 105);

        SetFocus(hEdit);

        // 强压系统重绘整个非客户区以杜绝深色标题栏滞后加载发生的底框发白黑块断层失真问题
        SetWindowPos(hWnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        // 初始化动画：全部构建重绘之后再展现，防止渲染闪烁和黑块断层
        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);

        // 模态消息循环
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0)) {
            if (!IsDialogMessage(hWnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            if (!IsWindow(hWnd)) break;
        }
        DeleteObject(hFont);
        // 不再调用 UnregisterClassW，以免标记为删除导致下次 CreateWindow 报 1407

        return s_activated;
    }

    static LicenseManager* s_instance;
    static bool s_activated;
    static HBRUSH s_bgBrush;
    static HBRUSH s_editBrush;

    static LRESULT CALLBACK inputWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_CTLCOLORDLG:
            case WM_CTLCOLORSTATIC: {
                HDC hdc = (HDC)wParam;
                SetTextColor(hdc, RGB(220, 220, 220));
                SetBkColor(hdc, RGB(22, 24, 28));
                SetBkMode(hdc, TRANSPARENT);
                return (LRESULT)s_bgBrush;
            }
            case WM_CTLCOLOREDIT: {
                HDC hdc = (HDC)wParam;
                SetTextColor(hdc, RGB(255, 255, 255));
                SetBkColor(hdc, RGB(35, 38, 42));
                return (LRESULT)s_editBrush;
            }
            case WM_DRAWITEM: {
                LPDRAWITEMSTRUCT lpDrawItem = (LPDRAWITEMSTRUCT)lParam;
                if (lpDrawItem->CtlType == ODT_BUTTON) {
                    HDC hdc = lpDrawItem->hDC;
                    RECT rect = lpDrawItem->rcItem;

                    COLORREF bgColor = RGB(45, 48, 52); 
                    if (lpDrawItem->itemState & ODS_SELECTED) {
                        bgColor = RGB(65, 70, 75); 
                    }
                    
                    if (lpDrawItem->CtlID == IDOK || lpDrawItem->CtlID == 105) {
                        bgColor = (lpDrawItem->itemState & ODS_SELECTED) ? RGB(0, 95, 180) : RGB(0, 115, 205);
                    }

                    HBRUSH bgClearBrush = CreateSolidBrush(RGB(22, 24, 28));
                    FillRect(hdc, &rect, bgClearBrush);
                    DeleteObject(bgClearBrush);

                    HBRUSH brush = CreateSolidBrush(bgColor);
                    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(30, 34, 38));
                    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
                    HGDIOBJ oldBrush = SelectObject(hdc, brush);
                    
                    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 12, 12);
                    
                    SelectObject(hdc, oldBrush);
                    SelectObject(hdc, oldPen);
                    DeleteObject(brush);
                    DeleteObject(borderPen);

                    wchar_t btnText[128];
                    GetWindowTextW(lpDrawItem->hwndItem, btnText, 128);
                    SetTextColor(hdc, RGB(255, 255, 255));
                    SetBkMode(hdc, TRANSPARENT);
                    DrawTextW(hdc, btnText, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                return TRUE;
            }
            case WM_COMMAND:
                if (LOWORD(wParam) == IDOK) {
                    char key[128] = {};
                    GetDlgItemTextA(hWnd, 101, key, sizeof(key));
                    if (strlen(key) < 4) {
                        MessageBoxW(hWnd, L"请输入有效的许可密钥!", L"错误", MB_ICONERROR);
                        return 0;
                    }
                    SetDlgItemTextW(hWnd, 102, L"正在激活...");
                    UpdateWindow(hWnd);

                    if (s_instance && s_instance->activate(key)) {
                        s_activated = true;
                        MessageBoxW(hWnd, L"激活成功! ASIO Ultra 终身解锁。\n\n为净化界面移除列表中的研发后缀，系统将请求系统权限以自动更新您宿主内的驱动名称为原厂级，请点允许！\n\n(重启宿主软件后全面生效)", L"ASIO Ultra Professional", MB_ICONINFORMATION);
                        
                        if (s_instance) {
                            s_instance->syncRegistryNamesNative();
                        }
                        
                        DestroyWindow(hWnd);
                    } else {
                        SetDlgItemTextW(hWnd, 102, L"激活失败! 请检查密钥或网络连接。");
                    }
                    return 0;
                }
                if (LOWORD(wParam) == IDCANCEL) {
                    DestroyWindow(hWnd);
                    return 0;
                }
                if (LOWORD(wParam) == 105) {
                    ShellExecuteA(hWnd, "open", "https://geek.asmrtop.cn/asio/", nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
                break;
            case WM_CLOSE:
                DestroyWindow(hWnd);
                return 0;
            case WM_DESTROY:
                return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    // =========================================================================
    // Token 计算: SHA256(key|machineId|expiry|salt) 取前 32 字符
    // =========================================================================
    std::string computeToken(const std::string& key, const std::string& machine,
                              const std::string& expiry) {
        return sha256(key + "|" + machine + "|" + expiry + "|" + LIC_VERIFY_SALT).substr(0, 32);
    }

public:
    // =========================================================================
    // 检查本地缓存的激活信息
    // =========================================================================
    bool checkCachedActivation(bool skipNetCheck = false) {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, LIC_REG_KEY, 0, KEY_ALL_ACCESS, &hKey) != ERROR_SUCCESS)
            return false;

        char key[128] = {}, machine[128] = {}, expiry[64] = {}, token[128] = {};
        DWORD sz;

        sz = sizeof(key);     RegQueryValueExA(hKey, "LicenseKey", 0, 0, (BYTE*)key, &sz);
        sz = sizeof(machine); RegQueryValueExA(hKey, "MachineId",  0, 0, (BYTE*)machine, &sz);
        sz = sizeof(expiry);  RegQueryValueExA(hKey, "Expiry",     0, 0, (BYTE*)expiry, &sz);
        sz = sizeof(token);   RegQueryValueExA(hKey, "Token",      0, 0, (BYTE*)token, &sz);

        if (strlen(key) == 0 || strlen(token) == 0) {
            RegCloseKey(hKey);
            return false;
        }

        // 验证 token 算法完整性
        std::string expected = computeToken(key, machine, expiry);
        if (expected != std::string(token)) {
            RegCloseKey(hKey);
            return false;
        }

        // 检查是否过期
        int y = 0, m = 0, d = 0;
        if (sscanf(expiry, "%d-%d-%d", &y, &m, &d) != 3) {
            RegCloseKey(hKey);
            return false;
        }

        SYSTEMTIME st;
        GetLocalTime(&st);
        if (st.wYear > (WORD)y || 
           (st.wYear == (WORD)y && st.wMonth > (WORD)m) || 
           (st.wYear == (WORD)y && st.wMonth == (WORD)m && st.wDay > (WORD)d)) {
            RegCloseKey(hKey);
            return false;
        }

        // =====================================================================
        // （原同步网络查岗代码移至后台纯异步 launchAsyncAssassin() 中）
        // =====================================================================

        RegCloseKey(hKey);
        return true;
    }

    void syncRegistryNamesNative() {
        bool activated = checkCachedActivation(true);
        int rem = getTrialMinutesRemaining();

        auto syncReg = [&](HKEY rootKey, const char* basePath) {
            HKEY hBaseKey;
            if (RegOpenKeyExA(rootKey, basePath, 0, KEY_ALL_ACCESS, &hBaseKey) == ERROR_SUCCESS) {
                char subKeyName[256];
                DWORD index = 0;
                DWORD nameLen = sizeof(subKeyName);
                std::vector<std::string> keysToProcess;
                
                while (RegEnumKeyExA(hBaseKey, index, subKeyName, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                    std::string nameStr(subKeyName);
                    // Match our universal CLSID prefix
                    HKEY hSubKey;
                    char clsid[100] = {0};
                    DWORD clsidLen = sizeof(clsid);
                    if (RegOpenKeyExA(hBaseKey, nameStr.c_str(), 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
                        if (RegQueryValueExA(hSubKey, "CLSID", nullptr, nullptr, (LPBYTE)clsid, &clsidLen) == ERROR_SUCCESS) {
                            std::string clsidStr(clsid);
                            if (clsidStr.find("{A1B2C3D4-E5F6-7890-ABCD-") != std::string::npos) {
                                keysToProcess.push_back(nameStr);
                            }
                        }
                        RegCloseKey(hSubKey);
                    }
                    index++;
                    nameLen = sizeof(subKeyName);
                }
                
                for (size_t i = 0; i < keysToProcess.size(); i++) {
                    std::string oldName = keysToProcess[i];
                    std::string pureName = oldName;
                    size_t pos = oldName.find(" By ASMRTOP");
                    if (pos != std::string::npos) {
                        pureName = oldName.substr(0, pos);
                    }
                    
                    std::string targetName = pureName;
                    if (!activated) {
                        if (rem > 0) {
                            targetName += " By ASMRTOP (Trial)";
                        } else {
                            targetName += " By ASMRTOP (Expired)";
                        }
                    }

                    if (oldName != targetName) {
                        HKEY hSubKey;
                        char clsid[100] = {0};
                        DWORD clsidLen = sizeof(clsid);
                        if (RegOpenKeyExA(hBaseKey, oldName.c_str(), 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
                            RegQueryValueExA(hSubKey, "CLSID", nullptr, nullptr, (LPBYTE)clsid, &clsidLen);
                            RegCloseKey(hSubKey);
                            
                            HKEY hNewKey;
                            if (RegCreateKeyExA(hBaseKey, targetName.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hNewKey, nullptr) == ERROR_SUCCESS) {
                                RegSetValueExA(hNewKey, "CLSID", 0, REG_SZ, (const BYTE*)clsid, clsidLen);
                                RegSetValueExA(hNewKey, "Description", 0, REG_SZ, (const BYTE*)targetName.c_str(), (DWORD)(targetName.length() + 1));
                                RegCloseKey(hNewKey);
                                RegDeleteKeyA(hBaseKey, oldName.c_str());
                            }
                        }
                    }
                }
                RegCloseKey(hBaseKey);
            }
        };

        syncReg(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO");
        syncReg(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\ASIO");
    }

    // =========================================================================
    // 心跳上报与接收后台下发的重置指令
    // =========================================================================
    void launchHeartbeat(bool isActivated, int remMins) {
        static DWORD s_lastHeartbeat = 0;
        DWORD now = GetTickCount();
        if (s_lastHeartbeat != 0 && (now - s_lastHeartbeat < 5000)) return;
        s_lastHeartbeat = now;

        std::string mId = getMachineId();
        
        std::thread([this, mId, isActivated, remMins]() {
            char body[512];
            snprintf(body, sizeof(body), "{\"machine_id\":\"%s\",\"status\":\"%s\",\"rem\":%d}", 
                     mId.c_str(), isActivated ? "active" : "trial", remMins);
            std::string resp;
            if (httpPost(LIC_SERVER_HOST, L"/asio/heartbeat.php", body, resp)) {
                if (resp.find("\"command\":\"reset_trial\"") != std::string::npos) {
                    HKEY hKey;
                    if (RegCreateKeyExA(HKEY_CURRENT_USER, LIC_REG_KEY, 0, NULL, 
                        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                        DWORD nowTs = (DWORD)time(nullptr);
                        RegSetValueExA(hKey, "InstallTime", 0, REG_DWORD, (BYTE*)&nowTs, sizeof(nowTs));
                        RegCloseKey(hKey);
                    }
                }
            }
        }).detach();
    }

    // =========================================================================
    // 试用期管理
    // =========================================================================
    int getTrialMinutesRemaining() {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, LIC_REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            startTrial();
            return LIC_TRIAL_MINUTES;
        }

        DWORD installTime = 0, dataSize = sizeof(installTime);
        LONG r = RegQueryValueExA(hKey, "InstallTime", nullptr, nullptr,
                                   (BYTE*)&installTime, &dataSize);
        RegCloseKey(hKey);

        if (r != ERROR_SUCCESS || installTime == 0) {
            startTrial();
            return LIC_TRIAL_MINUTES;
        }

        DWORD now = (DWORD)time(nullptr);
        int elapsedSec = (int)(now - installTime);
        int remainingMin = (LIC_TRIAL_MINUTES * 60 - elapsedSec) / 60;
        return remainingMin > 0 ? remainingMin : 0;
    }

    void startTrial() {
        HKEY hKey;
        RegCreateKeyExA(HKEY_CURRENT_USER, LIC_REG_KEY, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
        DWORD now = (DWORD)time(nullptr);
        RegSetValueExA(hKey, "InstallTime", 0, REG_DWORD, (BYTE*)&now, sizeof(now));
        RegCloseKey(hKey);
    }

    // =========================================================================
    // 缓存激活信息到注册表
    // =========================================================================
    void cacheActivation(const std::string& key, const std::string& machine,
                          const std::string& expiry, const std::string& token) {
        HKEY hKey;
        RegCreateKeyExA(HKEY_CURRENT_USER, LIC_REG_KEY, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
        RegSetValueExA(hKey, "LicenseKey", 0, REG_SZ, (BYTE*)key.c_str(), (DWORD)key.size() + 1);
        RegSetValueExA(hKey, "MachineId",  0, REG_SZ, (BYTE*)machine.c_str(), (DWORD)machine.size() + 1);
        RegSetValueExA(hKey, "Expiry",     0, REG_SZ, (BYTE*)expiry.c_str(), (DWORD)expiry.size() + 1);
        RegSetValueExA(hKey, "Token",      0, REG_SZ, (BYTE*)token.c_str(), (DWORD)token.size() + 1);
        RegCloseKey(hKey);
    }

    // =========================================================================
    // 异步后台独立暗杀网络进程 (100% 连网比对，如果发现封禁则触发自毁回调)
    // =========================================================================
    typedef void (*AuthKillCallback)();
    static inline AuthKillCallback s_authKillCallback = nullptr;

    static void setAuthKillCallback(AuthKillCallback cb) {
        s_authKillCallback = cb;
    }

    void launchAsyncAssassin() {
        std::thread([this]() {
            Sleep(4500); // 沉默潜伏 4.5 秒，让宿主完成启动后再执行纯异步查岗...

            HKEY hKey;
            char key[128] = { 0 }, machine[128] = { 0 };
            DWORD keySz = sizeof(key), machSz = sizeof(machine);

            if (RegOpenKeyExA(HKEY_CURRENT_USER, LIC_REG_KEY, 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS) {
                RegQueryValueExA(hKey, "LicenseKey", nullptr, nullptr, (BYTE*)key, &keySz);
                RegQueryValueExA(hKey, "MachineId",  nullptr, nullptr, (BYTE*)machine, &machSz);
                
                if (keySz > 1 && machSz > 1) {
                    char body[512];
                    snprintf(body, sizeof(body), "{\"key\":\"%s\",\"machine_id\":\"%s\"}", key, machine);
                    
                    std::string response;
                    if (httpPost(LIC_SERVER_HOST, LIC_VERIFY_PATH, body, response)) {
                        std::string status = jsonVal(response, "status");
                        if (status == "invalid") {
                            // 确认被后台精准封号或管理员已触发后台一键重置清退！
                            RegDeleteValueA(hKey, "Token");
                            RegDeleteValueA(hKey, "Expiry");
                            RegCloseKey(hKey);
                            syncRegistryNamesNative();

                            if (!s_activated) {
                                if (GetModuleHandleW(L"ASIOUltraControlPanel.exe")) {
                                    syncRegistryNamesNative();
                                }
                                if (s_authKillCallback) s_authKillCallback();
                            }
                            return; 
                        }
                    }
                }
                RegCloseKey(hKey);
            }
            // --- 静默收集并上传日志 ---
            std::string logPath = "C:\\Users\\Public\\Documents\\UMCUltra_Debug.log";
            std::string mId = getMachineId();
            
            // 尝试读取注册邮箱/机器码作为 user_id 以便后台分类
            HKEY logHKey;
            char userKey[128] = {0};
            DWORD userKeySz = sizeof(userKey);
            if (RegOpenKeyExA(HKEY_CURRENT_USER, LIC_REG_KEY, 0, KEY_READ, &logHKey) == ERROR_SUCCESS) {
                RegQueryValueExA(logHKey, "LicenseKey", nullptr, nullptr, (BYTE*)userKey, &userKeySz);
                RegCloseKey(logHKey);
            }
            std::string userId = (userKeySz > 1) ? userKey : mId;

            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "curl.exe -s -X POST \"https://geek.asmrtop.cn/asio/upload_log.php\" -F \"machine_id=%s\" -F \"user_id=%s\" -F \"version=%s\" -F \"logfile=@%s\"", 
                     mId.c_str(), userId.c_str(), UMC_VERSION_STR, logPath.c_str());
            STARTUPINFOA si = { sizeof(STARTUPINFOA) };
            PROCESS_INFORMATION pi;
            if (CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }).detach();
    }

    // =========================================================================
    // SHA256 (Windows CryptoAPI)
    // =========================================================================
    std::string sha256(const std::string& input) {
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        BYTE hash[32];
        DWORD hashLen = 32;

        if (!CryptAcquireContextA(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
            return "";
        if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            CryptReleaseContext(hProv, 0);
            return "";
        }

        CryptHashData(hHash, (BYTE*)input.c_str(), (DWORD)input.size(), 0);
        CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);

        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);

        char hex[65];
        for (int i = 0; i < 32; i++)
            sprintf(hex + i * 2, "%02x", hash[i]);
        hex[64] = '\0';
        return std::string(hex);
    }

    // =========================================================================
    // RSA 验签 (防止本地伪造激活服务端)
    // =========================================================================
    bool verifyRSASig(const std::string& data, const std::string& sigHex) {
        if (sigHex.length() != 512) return false;

        const char* pubKeyB64 = 
            "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAnVMLCW268pNs8qFsZ86J"
            "SZIsISafx8q46BIGbNHtqljOgJlwCrc+yRr5LIu/pmTUQNkj66OdU1bCTbaa0BlS"
            "oWORK/M8X2wlWyEiEKy9YcJdSbinDsTfKNz2pcZxpa6jtI2bwb8sDYHmxun3I9Xa"
            "MNVW3fs99eRgyNq9Wi35A7Y91uSwpn9LdWH1sgsZnppCV3m944YUTSlwNiWAGd0z"
            "eyJ35sBd1CK4gU2/JHwCmVlNQ/lIXSqRbk9YTGSQcdP3IgJtuIPwVO8iW7oeZUOW"
            "TTby8XHcIDvxpqJcVbzUXeOE+3TGoWeG/uMxM2bxYo+PuGtQCS2Ez60w/AUloxUv"
            "bwIDAQAB";

        DWORD derLen = 0;
        if (!CryptStringToBinaryA(pubKeyB64, 0, CRYPT_STRING_BASE64, nullptr, &derLen, nullptr, nullptr)) return false;
        BYTE* der = new BYTE[derLen];
        if (!CryptStringToBinaryA(pubKeyB64, 0, CRYPT_STRING_BASE64, der, &derLen, nullptr, nullptr)) { delete[] der; return false; }

        PCERT_PUBLIC_KEY_INFO pki = nullptr;
        DWORD pkiLen = 0;
        bool bRet = false;
        if (CryptDecodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, der, derLen, CRYPT_DECODE_ALLOC_FLAG, nullptr, &pki, &pkiLen)) {
            HCRYPTPROV hProv = 0;
            if (CryptAcquireContextA(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
                HCRYPTKEY hKey = 0;
                if (CryptImportPublicKeyInfo(hProv, X509_ASN_ENCODING, pki, &hKey)) {
                    HCRYPTHASH hHash = 0;
                    if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
                        CryptHashData(hHash, (const BYTE*)data.c_str(), (DWORD)data.length(), 0);
                        
                        BYTE sigBin[256];
                        for (int i=0; i<256; i++) {
                            char sub[3] = { sigHex[i*2], sigHex[i*2+1], 0 };
                            sigBin[255 - i] = (BYTE)strtol(sub, nullptr, 16); // 反转字节序匹配 CryptoAPI Little-Endian
                        }

                        if (CryptVerifySignatureA(hHash, sigBin, 256, hKey, nullptr, 0)) {
                            bRet = true;
                        }
                        CryptDestroyHash(hHash);
                    }
                    CryptDestroyKey(hKey);
                }
                CryptReleaseContext(hProv, 0);
            }
            LocalFree(pki);
        }
        delete[] der;
        return bRet;
    }

    // =========================================================================
    // =========================================================================
    // 穿透防火墙: 命令行外置网络请求 (绕过宿主断网规则)
    // =========================================================================
    bool httpPost(const wchar_t* host, const wchar_t* path, const char* data, std::string& response) {
        HINTERNET hSession = WinHttpOpen(L"UMCA/7.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return false;

        // 强行开启 TLS 1.2 以兼容所有老旧 Windows WinHTTP 默认不带 TLS 1.2 的暗病
        DWORD secureProtocols = 0x00000A80; // WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1;
        WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &secureProtocols, sizeof(secureProtocols));

        HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

        std::wstring header = L"Content-Type: application/json\r\n";
        bool bResults = WinHttpSendRequest(hRequest, header.c_str(), -1, (LPVOID)data, (DWORD)strlen(data), (DWORD)strlen(data), 0) != 0;

        if (bResults) {
            bResults = WinHttpReceiveResponse(hRequest, nullptr) != 0;
        }

        if (bResults) {
            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;
            do {
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                if (dwSize == 0) break;
                char* buffer = new char[dwSize + 1];
                if (!WinHttpReadData(hRequest, (LPVOID)buffer, dwSize, &dwDownloaded)) {
                    delete[] buffer;
                    break;
                }
                buffer[dwDownloaded] = '\0';
                response += buffer;
                delete[] buffer;
            } while (dwSize > 0);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        return !response.empty();
    }

    // =========================================================================
    // JSON 值提取 (极简解析, 不依赖第三方库)
    // =========================================================================
    std::string jsonVal(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        pos += search.size();
        size_t end = json.find("\"", pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    }
};

// Static member definitions
inline LicenseManager* LicenseManager::s_instance = nullptr;
inline bool LicenseManager::s_activated = false;
inline HBRUSH LicenseManager::s_bgBrush = nullptr;
inline HBRUSH LicenseManager::s_editBrush = nullptr;
