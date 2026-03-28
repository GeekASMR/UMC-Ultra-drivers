# Stop running processes
Stop-Process -Name "Studio One" -Force -ErrorAction SilentlyContinue
Stop-Process -Name "UMCControlPanel" -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

$Sys32Dir = "$env:windir\System32"
$SysWoW64Dir = "$env:windir\SysWOW64"

# Source Binaries
$SourceDll = "d:\Autigravity\UMCasio\build\bin\Release\BehringerASIO.dll"
$SourceExe = "d:\Autigravity\UMCasio\build\bin\Release\UMCControlPanel.exe"

# Copy to System32 (64-bit host)
Copy-Item -Path $SourceDll -Destination "$Sys32Dir\BehringerASIO.dll" -Force
Copy-Item -Path $SourceExe -Destination "$Sys32Dir\UMCControlPanel.exe" -Force

# Standard COM Registration from the System directory
& regsvr32 /s "$Sys32Dir\BehringerASIO.dll"

# If compiled as 32-bit as well (Optional but highly recommended if 32-bit Host needed)
# Since the primary workspace creates 64-bit, we deploy standard mapping.

$AsioKey = "HKLM:\SOFTWARE\ASIO\UMC Ultra By ASMRTOP"
$AsioKey64 = "HKLM:\SOFTWARE\WOW6432Node\ASIO\UMC Ultra By ASMRTOP"

# Rebind ASIO Keys to strict SYSTEM32 physical paths mimicking pristine system drivers
if (!(Test-Path $AsioKey)) { New-Item -Path $AsioKey -Force | Out-Null }
Set-ItemProperty -Path $AsioKey -Name "CLSID" -Value "{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}"
Set-ItemProperty -Path $AsioKey -Name "Description" -Value "UMC Ultra By ASMRTOP"

if (!(Test-Path $AsioKey64)) { New-Item -Path $AsioKey64 -Force | Out-Null }
Set-ItemProperty -Path $AsioKey64 -Name "CLSID" -Value "{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}"
Set-ItemProperty -Path $AsioKey64 -Name "Description" -Value "UMC Ultra By ASMRTOP"

Write-Host "Files cleanly deployed and forcibly COM-mapped into pristine C:\Windows\System32 core!"
