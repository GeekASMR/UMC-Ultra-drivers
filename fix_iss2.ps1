$file = "E:\Antigravity\成品开发\UMC\UMC_Universal_Installer_V6.1_Build\setup.iss"
$c = Get-Content $file -Encoding Default
# Fixed the regex to handle any characters between sgin\ and gongkai\
$c = $c -replace 'D:\\Autigravity\\sgin\\[^\"]*?\\gongkai\\', ''

# Remove [Tasks] completely up to the next bracket
$c = $c -replace '(?s)\[Tasks\].*?\[Registry\]', '[Registry]'

# Remove [Registry] entirely 
$c = $c -replace '(?s)\[Registry\].*?\[Files\]', '[Files]'

# Add UMCControlPanel
if ($c -notmatch 'UMCControlPanel.exe') {
    $c = $c -replace 'Source: "libusb-1.0.dll"; DestDir: "{app}"; Flags: ignoreversion', "Source: `"libusb-1.0.dll`"; DestDir: `"{app}`"; Flags: ignoreversion`nSource: `"UMCControlPanel.exe`"; DestDir: `"{sys}`"; Flags: ignoreversion`nSource: `"UMCControlPanel.exe`"; DestDir: `"{app}`"; Flags: ignoreversion"
    
    # Overwrite the [Icons] block
    $c = $c -replace '(?s)\[Icons\].*?\[Run\]', "[Icons]`nName: `"{commondesktop}\UMC Ultra 控制台`"; Filename: `"{app}\UMCControlPanel.exe`"`n`n[Run]"
}

# Remove manage_win_audio references
$c = $c -replace '(?m)^.*manage_win_audio\.ps1.*$', ''

Set-Content $file -Value $c -Encoding Default
