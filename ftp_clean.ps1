$ftp = "ftp://103.80.27.48/asio/cleaner.php"
$user = "geek_asmrtop_cn"
$pass = "5yifcKJkbKefm4JR"

$localFile = "d:\Autigravity\UMCasio\server\web\cleaner.php"

# Upload
Write-Host "Uploading cleaner..."
$request = [System.Net.FtpWebRequest]::Create($ftp)
$request.Credentials = New-Object System.Net.NetworkCredential($user, $pass)
$request.Proxy = $null
$request.Method = [System.Net.WebRequestMethods+Ftp]::UploadFile
$request.UsePassive = $true
$fileStream = [System.IO.File]::OpenRead($localFile)
$ftpStream = $request.GetRequestStream()
$buffer = New-Object byte[] 10240
while (($read = $fileStream.Read($buffer, 0, $buffer.Length)) -gt 0) { $ftpStream.Write($buffer, 0, $read) }
$ftpStream.Close(); $fileStream.Close()
$response = $request.GetResponse(); Write-Host "Upload Status: $($response.StatusDescription)"; $response.Close()

# Execute via HTTP to trigger PHP logic remotely
Write-Host "Executing remote cleaner... "
$httpResponse = Invoke-WebRequest -Uri "https://geek.asmrtop.cn/asio/cleaner.php" -UseBasicParsing
Write-Host $httpResponse.Content

# Delete
Write-Host "Deleting remote cleaner..."
$reqDel = [System.Net.FtpWebRequest]::Create($ftp)
$reqDel.Credentials = New-Object System.Net.NetworkCredential($user, $pass)
$reqDel.Method = [System.Net.WebRequestMethods+Ftp]::DeleteFile
$resDel = $reqDel.GetResponse()
Write-Host "Delete Status: $($resDel.StatusDescription)"
$resDel.Close()
