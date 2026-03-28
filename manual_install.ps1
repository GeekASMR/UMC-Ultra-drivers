$TargetDir = "C:\Program Files\UMC Ultra By ASMRTOP"
if (!(Test-Path $TargetDir)) { New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null }

# Stop processes if open
Stop-Process -Name "UMCControlPanel" -Force -ErrorAction SilentlyContinue

# Copy Build Artifacts
Copy-Item -Path "d:\Autigravity\UMCasio\build\bin\Release\BehringerASIO.dll" -Destination "$TargetDir\BehringerASIO.dll" -Force
Copy-Item -Path "d:\Autigravity\UMCasio\build\bin\Release\UMCControlPanel.exe" -Destination "$TargetDir\UMCControlPanel.exe" -Force

# Standard COM Registration
& regsvr32 /s "$TargetDir\BehringerASIO.dll"

# Inject into ASIO Keys manually per Steinberg protocol over User request
$AsioKey = "HKLM:\SOFTWARE\ASIO\UMC Ultra By ASMRTOP"
$AsioKey64 = "HKLM:\SOFTWARE\WOW6432Node\ASIO\UMC Ultra By ASMRTOP"

if (!(Test-Path $AsioKey)) { New-Item -Path $AsioKey -Force | Out-Null }
Set-ItemProperty -Path $AsioKey -Name "CLSID" -Value "{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}"
Set-ItemProperty -Path $AsioKey -Name "Description" -Value "UMC Ultra By ASMRTOP"

if (!(Test-Path $AsioKey64)) { New-Item -Path $AsioKey64 -Force | Out-Null }
Set-ItemProperty -Path $AsioKey64 -Name "CLSID" -Value "{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}"
Set-ItemProperty -Path $AsioKey64 -Name "Description" -Value "UMC Ultra By ASMRTOP"

Write-Host "Local DEV deployment synced flawlessly!"
