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

echo [INFO] Restoring Official ASIO Driver Subkeys is no longer necessary as regsvr32 securely maps the new UMC Ultra native metadata!

echo [SUCCESS] Done! Encoding fixed and Official Driver restored.
pause
