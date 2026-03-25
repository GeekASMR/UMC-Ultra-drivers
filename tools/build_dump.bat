@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /EHsc /O2 /I"d:\Autigravity\UMCasio\src" /I"d:\Autigravity\UMCasio\libusb\include" d:\Autigravity\UMCasio\tools\usb_descriptor_dump.cpp /Fe:d:\Autigravity\UMCasio\tools\usb_descriptor_dump.exe /link "d:\Autigravity\UMCasio\libusb\VS2022\MS64\dll\libusb-1.0.lib"
