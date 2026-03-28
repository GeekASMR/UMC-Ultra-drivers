$subjectName = "ASMRTOP Studio"
$cert = New-SelfSignedCertificate -Subject "CN=$subjectName" -Type CodeSigningCert -CertStoreLocation Cert:\CurrentUser\My
$pwd = ConvertTo-SecureString -String "asmrtop123" -Force -AsPlainText
$pfxPath = "d:\Autigravity\UMCasio\ASMRTOP_Studio.pfx"
$cerPath = "d:\Autigravity\UMCasio\ASMRTOP_Studio.cer"

Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $pwd
Export-Certificate -Cert $cert -FilePath $cerPath

Write-Host "Certificates generated: $pfxPath, $cerPath"
