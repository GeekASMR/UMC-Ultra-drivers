$ftp = "ftp://103.80.27.48/asio/upload_log.php"
$user = "geek_asmrtop_cn"
$pass = "5yifcKJkbKefm4JR"

$localFile = "C:\Users\Administrator\.gemini\antigravity\tmp\upload_log.php"

Write-Host "Uploading updated PHP handler to Server..."
$request = [System.Net.FtpWebRequest]::Create($ftp)
$request.Credentials = New-Object System.Net.NetworkCredential($user, $pass)
$request.Proxy = $null
$request.Method = [System.Net.WebRequestMethods+Ftp]::UploadFile
$request.UsePassive = $true

$fileStream = [System.IO.File]::OpenRead($localFile)
$ftpStream = $request.GetRequestStream()

$buffer = New-Object byte[] 10240
while (($read = $fileStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
    $ftpStream.Write($buffer, 0, $read)
}

$ftpStream.Close()
$fileStream.Close()

$response = $request.GetResponse()
Write-Host "Upload Status: $($response.StatusDescription)"
$response.Close()
