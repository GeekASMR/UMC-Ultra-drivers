$user = "geek_asmrtop_cn"
$pass = "5yifcKJkbKefm4JR"

function Upload-File {
    param($localFile, $ftpUrl)
    Write-Host "Uploading to $ftpUrl ..."
    $req = [System.Net.FtpWebRequest]::Create($ftpUrl)
    $req.Credentials = New-Object System.Net.NetworkCredential($user, $pass)
    $req.Proxy = $null
    $req.Method = [System.Net.WebRequestMethods+Ftp]::UploadFile
    $fs = [System.IO.File]::OpenRead($localFile)
    $rs = $req.GetRequestStream()
    $fs.CopyTo($rs)
    $rs.Close()
    $fs.Close()
    Write-Host "Success: $ftpUrl"
}

Upload-File "d:\Autigravity\UMCasio\ASIO-Ultra.html" "ftp://103.80.27.48/ASIO-Ultra.html"
Upload-File "d:\Autigravity\UMCasio\server\web\asmrtap.php" "ftp://103.80.27.48/asio/asmrtap.php"
Upload-File "d:\Autigravity\UMCasio\server\web\db_patch.php" "ftp://103.80.27.48/asio/db_patch.php"

Write-Host "Executing HTTP database patch..."
$patchResult = Invoke-WebRequest -Uri "https://geek.asmrtop.cn/asio/db_patch.php" -UseBasicParsing
Write-Host "Patch Output: $($patchResult.Content)"

Write-Host "Cleaning up patch file..."
$delReq = [System.Net.FtpWebRequest]::Create("ftp://103.80.27.48/asio/db_patch.php")
$delReq.Credentials = New-Object System.Net.NetworkCredential($user, $pass)
$delReq.Method = [System.Net.WebRequestMethods+Ftp]::DeleteFile
try { $delReq.GetResponse().Close() } catch {}
Write-Host "All done!"
