# ASMRTOP Universal Build & Sign Pipeline

# Paths
$signtool = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe"
$pfx = "d:\Autigravity\UMCasio\ASMRTOP_Studio.pfx"
$cer = "d:\Autigravity\UMCasio\ASMRTOP_Studio.cer"
$pfxPass = "asmrtop123"

$installerDir = @(Resolve-Path "E:\Antigravity\*\UMC\v7" | Select-Object -ExpandProperty Path)[0]

$buildDir = "d:\Autigravity\UMCasio\build"

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  ASMRTOP V7.0 Autobuild System Started   " -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

# Step 0: Compile all targets via CMake
Write-Host "[0] Starting CMake Compilation..." -ForegroundColor Green
Set-Location "d:\Autigravity\UMCasio"
cmake -S . -B $buildDir -DCMAKE_BUILD_TYPE=Release
cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "FATAL ERROR: CMake build failed!" -ForegroundColor Red
    Exit
}

$brands = @("BEHRINGER", "AUDIENT", "SSL", "MACKIE", "TASCAM", "YAMAHA", "MOTU", "PRESONUS", "FOCUSRITE", "ZOOM", "ART", "ROLAND", "MAUDIO", "UAD_VOLT", "FENDER")

# Step 1: Sign Binaries
Write-Host "[1] Starting SHA256 Code Sign..." -ForegroundColor Green
foreach ($brand in $brands) {
    $dllPath = Join-Path $buildDir "bin\Release\ASMRTOP_${brand}.dll"
    if (Test-Path $dllPath) {
        & $signtool sign /f $pfx /p $pfxPass /fd SHA256 $dllPath
        # Step 3: Copy Signed Binaries
        Copy-Item $dllPath -Destination (Join-Path $installerDir "ASMRTOP_${brand}.dll") -Force
    } else {
        Write-Host "WARNING: Target DLL missing: $dllPath" -ForegroundColor Yellow
    }
}

$exePath = Join-Path $buildDir "bin\Release\ASIOUltraControlPanel.exe"
if (Test-Path $exePath) {
    & $signtool sign /f $pfx /p $pfxPass /fd SHA256 $exePath
    Copy-Item $exePath -Destination (Join-Path $installerDir "ASIOUltraControlPanel.exe") -Force
}

# Step 4: Run ISCC compiler
Write-Host "[4] Inno Setup Packaging..." -ForegroundColor Green
Set-Location $installerDir
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" "setup.iss"
if ($LASTEXITCODE -ne 0) {
    Write-Host "FATAL ERROR: Inno Setup failed!" -ForegroundColor Red
    Exit
}

# Fetch current global version dynamically
$appVer = Get-Content "d:\Autigravity\UMCasio\version.txt" -Encoding UTF8
$appVer = $appVer.Trim()

# Step 5: Sign the Output Installer!
Write-Host "[5] Signing final Setup Installer..." -ForegroundColor Green
$installerOut = Join-Path $installerDir "out\ASIOUltra_V$($appVer)_Setup.exe"
$installerSignedOut = Join-Path $installerDir "out\ASIOUltra_V$($appVer)_Setup_Signed.exe"
if (Test-Path $installerOut) {
    & $signtool sign /f $pfx /p $pfxPass /fd SHA256 $installerOut
    if ($LASTEXITCODE -eq 0) {
        Rename-Item -Path $installerOut -NewName "ASIOUltra_V$($appVer)_Setup_Signed.exe" -Force
        Write-Host "Successfully signed and renamed to _Signed.exe" -ForegroundColor Green
    }
} else {
    Write-Host "WARNING: Installer not found at $installerOut" -ForegroundColor Yellow
}

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host " V7.0 Autobuild Complete!                 " -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
