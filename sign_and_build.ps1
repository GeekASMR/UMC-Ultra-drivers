# Paths
$signtool = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe"
$pfx = "d:\Autigravity\UMCasio\ASMRTOP_Studio.pfx"
$cer = "d:\Autigravity\UMCasio\ASMRTOP_Studio.cer"
$pfxPass = "asmrtop123"

# Wildcard Path for Chinese folders
$installerDir = Resolve-Path "E:\Antigravity\*\UMC\UMC_Universal_Installer_Build" | Select-Object -ExpandProperty Path

# Files to Sign natively
$filesToSign = @(
    "d:\Autigravity\UMCasio\build\bin\Release\BehringerASIO.dll",
    "d:\Autigravity\UMCasio\build\bin\Release\UMCControlPanel.exe"
)

# Step 1: Sign Binaries
foreach ($file in $filesToSign) {
    & $signtool sign /f $pfx /p $pfxPass /fd SHA256 $file
}

# Step 2: Extract & Patch Setup.iss
$issFile = Join-Path $installerDir "setup.iss"
Copy-Item $cer (Join-Path $installerDir "ASMRTOP_Studio.cer") -Force
$issContent = Get-Content $issFile -Encoding UTF8

if ($issContent -notmatch 'ASMRTOP_Studio.cer') {
    # Replace [Files] adding the .cer file
    $issContent = $issContent -replace 'Source: "VirtualAudioRouter\.sys"', "Source: `"ASMRTOP_Studio.cer`"; DestDir: `"{app}`"; Flags: ignoreversion`nSource: `"VirtualAudioRouter.sys`""
    # Replace [Run] adding the certutil
    $issContent = $issContent -replace '\[Run\]', "[Run]`nFilename: `"{sys}\certutil.exe`"; Parameters: `"-addstore -f Root `"`"{app}\ASMRTOP_Studio.cer`"`"`"; Flags: runhidden waituntilterminated"
    Set-Content $issFile -Value $issContent -Encoding UTF8
}

# Step 3: Copy Signed Binaries
Copy-Item "d:\Autigravity\UMCasio\build\bin\Release\BehringerASIO.dll" -Destination (Join-Path $installerDir "BehringerASIO.dll") -Force
Copy-Item "d:\Autigravity\UMCasio\build\bin\Release\UMCControlPanel.exe" -Destination (Join-Path $installerDir "UMCControlPanel.exe") -Force

# Step 4: Run ISCC compiler
Set-Location $installerDir
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" "setup.iss"

# Fetch current global version dynamically
$appVer = Get-Content "d:\Autigravity\UMCasio\version.txt" -Encoding UTF8
$appVer = $appVer.Trim()

# Step 5: Sign the Output Installer!
$installerOut = Join-Path $installerDir "out\UMCUltra_V$($appVer)_Setup.exe"
& $signtool sign /f $pfx /p $pfxPass /fd SHA256 $installerOut

# Step 6: Push strictly to GitHub
Set-Location "d:\Autigravity\UMCasio"
gh release upload v$appVer $installerOut --clobber

