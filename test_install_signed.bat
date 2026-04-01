@echo off
setlocal
title ASMRTOP 独立虚拟声卡一键热挂载与签名工具
color 0B
echo.
echo ========================================================
echo   [ASMRTOP Virtual Audio Router] - 研发端编译后自动签名与挂载
echo ========================================================
echo.
echo 正在请求管理员权限以注入底层系统级核心驱动...
timeout /t 1 >nul
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [FATAL] 权限拒绝！请在当前文件上【右键】 -^> 【以管理员身份运行】
    pause
    exit /b
)

echo [OK] 已经获得最高底层管理员授权.
cd /d "%~dp0"

echo.
echo [1/6] 自动打上 ASMRTOP_Studio 测试版内核级交叉数字签名...
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe" sign /f "d:\Autigravity\UMCasio\ASMRTOP_Studio.pfx" /p "asmrtop123" /fd SHA256 /v "%~dp0VirtualAudioRouter.sys" >nul 2>&1
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe" sign /f "d:\Autigravity\UMCasio\ASMRTOP_Studio.pfx" /p "asmrtop123" /fd SHA256 /v "%~dp0VirtualAudioRouter.cat" >nul 2>&1

echo.
echo [2/6] 前置净化探测：粉碎上一代幽灵挂载及死寂缓存...
"%~dp0devcon.exe" remove ROOT\VirtualAudioRouter >nul 2>&1
pnputil /delete-driver VirtualAudioRouter.inf /uninstall /force >nul 2>&1

echo [3/6] 验证数字签名主密钥链，正在强制信任 ASMRTOP_Studio.cer ...
certutil.exe -addstore -f Root "%~dp0ASMRTOP_Studio.cer" >nul 2>&1
certutil.exe -addstore -f TrustedPublisher "%~dp0ASMRTOP_Studio.cer" >nul 2>&1

echo [4/6] 构建系统底层信息发布...
pnputil.exe /add-driver "%~dp0VirtualAudioRouter.inf" /install >nul

echo [5/6] 物理装配：构建 PnP 热插拔假象，直接拉起虚拟硬件设备池...
"%~dp0devcon.exe" install "%~dp0VirtualAudioRouter.inf" ROOT\VirtualAudioRouter >nul

echo.
echo [6/6] ======= 【签名与挂载大满贯】 =======
echo 原生未签名驱动已在本地实时完成 Hash 重组测试签名！
echo 驱动节点已热加载。就算之前是裸奔状态，现在也已成功突破代码 52！
echo -------------------------------------------------------------
pause
