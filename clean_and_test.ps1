$targetDirs = @("C:\Program Files\UMC Ultra By ASMRTOP", "C:\Program Files\ASIO Ultra By ASMRTOP", "C:\Program Files\ASIO Ultra")
foreach ($dir in $targetDirs) {
    if (Test-Path $dir) {
        $dlls = Get-ChildItem -Path $dir -Filter "*.dll"
        foreach ($dll in $dlls) {
            Write-Host "Unregistering $dll"
            regsvr32 /u /s $dll.FullName
        }
    }
}

Get-Process | Where-Object {$_.Name -match "UMCControlPanel|ASIOUltraControlPanel"} | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

foreach ($dir in $targetDirs) {
    if (Test-Path $dir) {
        Write-Host "Deleting $dir"
        Remove-Item -Path $dir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
Write-Host "Local cleanup OK!" -ForegroundColor Green

# Trigger build
.\sign_and_build.ps1
