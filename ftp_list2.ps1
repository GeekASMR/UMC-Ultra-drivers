$ftp = "ftp://103.80.27.48/asio/"
$user = "geek_asmrtop_cn"
$pass = "5yifcKJkbKefm4JR"

$request = [System.Net.FtpWebRequest]::Create($ftp)
$request.Credentials = New-Object System.Net.NetworkCredential($user, $pass)
$request.Proxy = $null
$request.Method = [System.Net.WebRequestMethods+Ftp]::ListDirectoryDetails
$request.UsePassive = $true
try {
    $response = $request.GetResponse()
    $reader = New-Object System.IO.StreamReader($response.GetResponseStream())
    $files = $reader.ReadToEnd()
    Write-Host "--- FTP ASIO Directory ---"
    Write-Host $files
} catch {
    Write-Host "Failed: $_"
}
