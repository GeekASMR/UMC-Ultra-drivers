$version = (Get-Content "d:\Autigravity\UMCasio\version.txt").Trim()
$verCommas = $version -replace '\.', ','

Write-Host "Syncing Version: $version"

# 1. Update UMCVersion.h for C++ Codebase
$hContent = @"
#pragma once
#define UMC_VERSION_STR `"$version`"
#define UMC_VERSION_WSTR L`"$version`"
"@
Set-Content "d:\Autigravity\UMCasio\src\UMCVersion.h" -Value $hContent -Encoding UTF8

# 2. Update BehringerASIO.rc Version Strings
$rcPath = "d:\Autigravity\UMCasio\BehringerASIO.rc"
$rcContent = Get-Content $rcPath -Encoding UTF8
$rcContent = $rcContent -replace 'VER_FILEVERSION\s+.*', "VER_FILEVERSION             $verCommas,0"
$rcContent = $rcContent -replace 'VER_FILEVERSION_STR\s+.*', "VER_FILEVERSION_STR         `"$version.0\0`""
$rcContent = $rcContent -replace 'VER_PRODUCTVERSION\s+.*', "VER_PRODUCTVERSION          $verCommas,0"
$rcContent = $rcContent -replace 'VER_PRODUCTVERSION_STR\s+.*', "VER_PRODUCTVERSION_STR      `"$version.0\0`""
Set-Content $rcPath -Value $rcContent -Encoding UTF8

# 3. Update Inno Setup config
$issDir = Resolve-Path "E:\Antigravity\*\UMC\UMC_Universal_Installer_V6.1_Build" | Select-Object -ExpandProperty Path
$issPath = Join-Path $issDir "setup.iss"
$issContent = Get-Content $issPath -Encoding UTF8
$issContent = $issContent -replace '#define\s+MyAppVersion\s+.*', "#define MyAppVersion `"$version`""
Set-Content $issPath -Value $issContent -Encoding UTF8

# 4. Update UMCControlPanel.cpp (GUI notification title)
$ctrlPath = "d:\Autigravity\UMCasio\src\gui\UMCControlPanel.cpp"
$ctrlContent = Get-Content $ctrlPath -Encoding UTF8
$ctrlContent = $ctrlContent -replace 'wcscpy_s\(g_nid\.szInfoTitle, L"UMC Ultra v\d+\.\d+\.\d+"\);', "wcscpy_s(g_nid.szInfoTitle, L`"UMC Ultra v$version`");"
Set-Content $ctrlPath -Value $ctrlContent -Encoding UTF8

# 5. Update Logger.h (Log initialization message)
$loggerPath = "d:\Autigravity\UMCasio\src\utils\Logger.h"
# Logger.h has issues with BOM/Encoding in tools, use ReadAllText carefully
$loggerContent = [System.IO.File]::ReadAllText($loggerPath)
$loggerContent = $loggerContent -replace '===== Logger Started \(v\d+\.\d+\.\d+\) =====', "===== Logger Started (v$version) ====="
[System.IO.File]::WriteAllText($loggerPath, $loggerContent)

Write-Host "Version sync logic fully applied globally."
