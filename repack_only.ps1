$signtool = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe"
$pfx = "d:\Autigravity\UMCasio\ASMRTOP_Studio.pfx"
$pfxPass = "asmrtop123"
$installerDir = @(Resolve-Path "E:\Antigravity\*\UMC\v7" | Select-Object -ExpandProperty Path)[0]
$appVer = (Get-Content -Path "d:\Autigravity\UMCasio\version.txt" | Select-Object -First 1).Trim()

Write-Host "Repackaging with Inno Setup..." -ForegroundColor Cyan
Set-Location $installerDir
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" "setup.iss"

Write-Host "Signing final Setup Installer..." -ForegroundColor Cyan
$installerOut = Join-Path $installerDir "out\ASIOUltra_V$($appVer)_Setup.exe"
$installerSignedOut = Join-Path $installerDir "out\ASIOUltra_V$($appVer)_Setup_Signed.exe"

# 强制删除旧的签名文件，防止 Rename-Item 报错
if (Test-Path $installerSignedOut) { Remove-Item -Path $installerSignedOut -Force }

& $signtool sign /f $pfx /p $pfxPass /fd SHA256 $installerOut

if ($LASTEXITCODE -eq 0) {
    Rename-Item -Path $installerOut -NewName "ASIOUltra_V$($appVer)_Setup_Signed.exe" -Force
    Write-Host "Successfully repackaged and signed to $installerSignedOut" -ForegroundColor Green
} else {
    Write-Error "Signtool Failed"
}
