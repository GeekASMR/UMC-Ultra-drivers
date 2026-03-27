@echo off
echo [UMC Ultra] Force Updating DLL...
taskkill /f /im "Studio One.exe" >nul 2>&1
move /Y C:\Windows\System32\BehringerASIO.dll C:\Windows\System32\BehringerASIO_old_%RANDOM%.dll >nul 2>&1
copy /y d:\Autigravity\UMCasio\build\bin\Release\BehringerASIO.dll C:\Windows\System32\BehringerASIO.dll
regsvr32 /s C:\Windows\System32\BehringerASIO.dll
exit
