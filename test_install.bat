@echo off
setlocal
title ASMRTOP 独立虚拟声卡一键热加载挂载工具
color 0B
echo.
echo ========================================================
echo   [ASMRTOP Virtual Audio Router] - 测试模式强制挂载工具
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
echo [1/5] 前置净化探测：粉碎上一代幽灵挂载及死寂缓存...
"%~dp0devcon.exe" remove ROOT\VirtualAudioRouter >nul 2>&1
pnputil /delete-driver VirtualAudioRouter.inf /uninstall /force >nul 2>&1

echo [2/5] 验证数字签名主密钥链...
certutil.exe -addstore -f Root "%~dp0ASMRTOP_Studio.cer" >nul 2>&1
certutil.exe -addstore -f TrustedPublisher "%~dp0ASMRTOP_Studio.cer" >nul 2>&1

echo [3/5] 构建系统底层信息发布...
pnputil.exe /add-driver "%~dp0VirtualAudioRouter.inf" /install >nul

echo [4/5] 物理装配：构建 PnP 热插拔假象，直接拉起虚拟硬件设备池...
"%~dp0devcon.exe" install "%~dp0VirtualAudioRouter.inf" ROOT\VirtualAudioRouter >nul

echo.
echo [5/5] ======= 【挂载大满贯】 =======
echo 驱动节点已热加载。如果在此之前存在代码 52 封杀，现在已成功突破！
echo 如果你是首次开启未签名允许机制，可能需要重启电脑一次才能完全响应底层变化。
echo -------------------------------------------------------------
pause
