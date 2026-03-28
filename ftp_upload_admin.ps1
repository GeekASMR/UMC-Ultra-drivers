$ftp = "ftp://103.80.27.48/asio/admin.php"
$user = "geek_asmrtop_cn"
$pass = "5yifcKJkbKefm4JR"
$localFile = "d:\Autigravity\UMCasio\server\web\admin.php"

$request = [System.Net.FtpWebRequest]::Create($ftp)
$request.Credentials = New-Object System.Net.NetworkCredential($user, $pass)
$request.Proxy = $null
$request.Method = [System.Net.WebRequestMethods+Ftp]::UploadFile
$request.UsePassive = $true

$fileContents = [System.IO.File]::ReadAllBytes($localFile)
$request.ContentLength = $fileContents.Length

try {
    $stream = $request.GetRequestStream()
    $stream.Write($fileContents, 0, $fileContents.Length)
    $stream.Close()
    $response = $request.GetResponse()
    Write-Host "Upload Complete. Status: $($response.StatusDescription)"
    $response.Close()
} catch {
    Write-Host "FTP Upload Failed: $_"
}
