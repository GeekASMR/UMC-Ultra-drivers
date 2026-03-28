$file = "E:\Antigravity\成品开发\UMC\UMC_Universal_Installer_V6.1_Build\setup.iss"
$c = Get-Content $file -Encoding Default
$c = $c -replace 'D:\\Autigravity\\sgin\\.+?\\gongkai\\', ''
if ($c -notmatch 'UMCControlPanel.exe') {
    $c = $c -replace 'Source: "libusb-1.0.dll"; DestDir: "{app}"; Flags: ignoreversion', "Source: `"libusb-1.0.dll`"; DestDir: `"{app}`"; Flags: ignoreversion`nSource: `"UMCControlPanel.exe`"; DestDir: `"{app}`"; Flags: ignoreversion"
    $c = $c -replace 'Tasks: hide_original', "Tasks: hide_original`nName: `"{commondesktop}\UMC Ultra 控制台`"; Filename: `"{app}\UMCControlPanel.exe`""
}
Set-Content $file -Value $c -Encoding Default
