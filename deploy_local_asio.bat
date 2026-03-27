@echo off
NET SESSION >nul 2>&1
if %errorLevel% NEQ 0 (
    echo [ERROR] Please right click and Run as Administrator!
    pause
    exit /b 1
)

echo [INFO] Stopping Studio One...
taskkill /f /im "Studio One.exe" >nul 2>&1

echo [INFO] Unregistering old ASIO Driver...
regsvr32 /u /s "C:\Windows\System32\BehringerASIO.dll"

echo [INFO] Copying new ASIO Driver (Fixed encoding)...
copy /y "d:\Autigravity\UMCasio\build\bin\Release\BehringerASIO.dll" "C:\Windows\System32\BehringerASIO.dll"

echo [INFO] Registering new ASIO Driver...
regsvr32 /s "C:\Windows\System32\BehringerASIO.dll"

echo [INFO] Restoring Official UMC ASIO Driver in Registry...
reg add "HKLM\SOFTWARE\ASIO\UMC ASIO Driver" /v CLSID /d "{0351302F-B1F1-4A5D-8613-787F77C20EA4}" /t REG_SZ /f
reg add "HKLM\SOFTWARE\ASIO\UMC ASIO Driver" /v Description /d "UMC ASIO Driver" /t REG_SZ /f
reg add "HKLM\SOFTWARE\WOW6432Node\ASIO\UMC ASIO Driver" /v CLSID /d "{0351302F-B1F1-4A5D-8613-787F77C20EA4}" /t REG_SZ /f
reg add "HKLM\SOFTWARE\WOW6432Node\ASIO\UMC ASIO Driver" /v Description /d "UMC ASIO Driver" /t REG_SZ /f

echo [SUCCESS] Done! Encoding fixed and Official Driver restored.
pause
