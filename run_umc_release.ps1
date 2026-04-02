powershell -ExecutionPolicy Bypass -File "d:\Autigravity\UMCasio\clean_and_test.ps1"
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed" ; exit $LASTEXITCODE }

git config --global http.postBuffer 524288000
git config --global core.compression 0
git add -A
git commit -m "chore(release): Build and release v7 ASIO Ultra unified driver architecture"

git push -u origin master -f

gh release create v7.0.0 "E:\Antigravity\成品开发\UMC\v7\out\ASIOUltra_V7.0.0_Setup_Signed.exe" --title "UMC Ultra v7.0.0 - 核心重构纯净版" -F "d:\Autigravity\UMCasio\release_v7_notes.txt"

gh release create v7.0.0 "E:\Antigravity\成品开发\UMC\v7\out\ASIOUltra_V7.0.0_Setup_Signed.exe" --repo GeekASMR/ASIO-Ultra-drivers --title "ASIO Ultra v7.0.0" -F "d:\Autigravity\UMCasio\asio_ultra_notes.txt"

Write-Output "UMC Ultra v7 Release Workflow Completed Successfully."
