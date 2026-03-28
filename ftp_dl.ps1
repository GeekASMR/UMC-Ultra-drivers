$ftp = "ftp://103.80.27.48/asio/upload_log.php"
$user = "geek_asmrtop_cn"
$pass = "5yifcKJkbKefm4JR"

$request = [System.Net.FtpWebRequest]::Create($ftp)
$request.Credentials = New-Object System.Net.NetworkCredential($user, $pass)
$request.Proxy = $null
$request.Method = [System.Net.WebRequestMethods+Ftp]::DownloadFile
$request.UsePassive = $true

try {
    $response = $request.GetResponse()
    $reader = New-Object System.IO.StreamReader($response.GetResponseStream())
    $content = $reader.ReadToEnd()
    Write-Host "--- UPLOAD LOG PHP ---"
    Write-Host $content
} catch {
    Write-Host "Failed: $_"
}
