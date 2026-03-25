$iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if (!(Test-Path $iscc)) {
    $iscc = "C:\Program Files (x86)\Inno Setup 5\ISCC.exe"
}
Set-Location "E:\Antigravity\成品开发\UMC\UMC_Ultra_Installer_Build"
New-Item -ItemType Directory -Path "out" -Force
& $iscc "setup.iss"
