$user = "geek_asmrtop_cn"
$pass = "5yifcKJkbKefm4JR"

$localFile = "d:\Autigravity\UMCasio\ASIO-Ultra.html"
Write-Host "Target: $localFile"

# Upload safely
$ftp1 = "ftp://103.80.27.48/ASIO-Ultra.html"
$req1 = [System.Net.FtpWebRequest]::Create($ftp1)
$req1.Credentials = New-Object System.Net.NetworkCredential($user, $pass)
$req1.Proxy = $null
$req1.Method = [System.Net.WebRequestMethods+Ftp]::UploadFile
$fs1 = [System.IO.File]::OpenRead($localFile)
$rs1 = $req1.GetRequestStream()
$fs1.CopyTo($rs1)
$rs1.Close()
$fs1.Close()

$ftp2 = "ftp://103.80.27.48/umc_ultra.html"
$req2 = [System.Net.FtpWebRequest]::Create($ftp2)
$req2.Credentials = New-Object System.Net.NetworkCredential($user, $pass)
$req2.Proxy = $null
$req2.Method = [System.Net.WebRequestMethods+Ftp]::UploadFile
$fs2 = [System.IO.File]::OpenRead($localFile)
$rs2 = $req2.GetRequestStream()
$fs2.CopyTo($rs2)
$rs2.Close()
$fs2.Close()

Write-Host "Uploaded successfully!"
