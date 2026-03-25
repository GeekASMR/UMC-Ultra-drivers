@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cl /EHsc /O2 ioctl_spy5.cpp /link ole32.lib oleaut32.lib dbghelp.lib
