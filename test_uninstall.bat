@echo off
setlocal
title ASMRTOP 独立虚拟声卡一键拔除工具
color 0C
echo.
echo ========================================================
echo   [ASMRTOP Virtual Audio Router] - 强行拔除虚拟节点清理
echo ========================================================
echo.
echo 正在请求管理员权限以清理底层系统级核心驱动...
timeout /t 1 >nul
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [FATAL] 权限拒绝！请在当前文件上【右键】 -^> 【以管理员身份运行】
    pause
    exit /b
)

echo [OK] 已放行底层清理权限.
cd /d "%~dp0"

echo.
echo [1/3] 执行热拔插卸载：拔除所有的 VirtualAudioRouter 设备实例
"%~dp0devcon.exe" remove ROOT\VirtualAudioRouter

echo [2/3] 从系统 PnP 缓存仓中彻底抹除虚拟驱动核心 inf，永久净化：
pnputil /delete-driver VirtualAudioRouter.inf /uninstall /force

echo.
echo [3/3] ======= 【拔除全量完成】 =======
echo 所有的路由分线已安全摘除，原厂系统层纯净恢复！
echo.
pause
