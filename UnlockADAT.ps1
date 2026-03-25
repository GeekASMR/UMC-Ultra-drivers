$basePath = "HKLM:\SYSTEM\CurrentControlSet\Enum\USB\VID_1397&PID_0503\F2E70F04\Device Parameters\Config\SoundDeviceProfiles"

function Add-Profile($dir, $sd, $name, $ch0, $ch1, $guidBase) {
    $path = "$basePath\$dir\$sd"
    New-Item -Path $path -Force | Out-Null
    Set-ItemProperty -Path $path -Name "DisplayName" -Value $name -Type String
    Set-ItemProperty -Path $path -Name "PinCategoryGuid" -Value "{DFF21FE3-F70F-11D0-B917-00A0C9223196}" -Type String
    Set-ItemProperty -Path $path -Name "PinNameGuid" -Value "{9b9c4d20-c75a-484d-89c9-$guidBase}" -Type String
    Set-ItemProperty -Path $path -Name "Type" -Value 0 -Type DWord

    New-Item -Path "$path\Loc_00" -Force | Out-Null
    Set-ItemProperty -Path "$path\Loc_00" -Name "ChannelIndex" -Value $ch0 -Type DWord

    New-Item -Path "$path\Loc_01" -Force | Out-Null
    Set-ItemProperty -Path "$path\Loc_01" -Name "ChannelIndex" -Value $ch1 -Type DWord
}

Write-Host "Unlocking ADAT WDM Profiles..."

# ADAT Outputs (Channels 12-19)
Add-Profile "plb" "sd8"  "ADAT OUT 1-2" 12 13 "dda76c7c1ad1"
Add-Profile "plb" "sd9"  "ADAT OUT 3-4" 14 15 "dda76c7c1ad2"
Add-Profile "plb" "sd10" "ADAT OUT 5-6" 16 17 "dda76c7c1ad3"
Add-Profile "plb" "sd11" "ADAT OUT 7-8" 18 19 "dda76c7c1ad4"

# ADAT Inputs (Channels 10-17)
Add-Profile "rec" "sd15" "ADAT IN 1-2" 10 11 "dda76c7c1ae1"
Add-Profile "rec" "sd16" "ADAT IN 3-4" 12 13 "dda76c7c1ae2"
Add-Profile "rec" "sd17" "ADAT IN 5-6" 14 15 "dda76c7c1ae3"
Add-Profile "rec" "sd18" "ADAT IN 7-8" 16 17 "dda76c7c1ae4"

Write-Host "Registry entries created successfully."

Write-Host "Restarting Behringer Audio Device via PnPUtil..."
# Find the device instance ID dynamically
$dev = Get-PnpDevice -FriendlyName "*UMC 1820*" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($dev) {
    Disable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
    Enable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "Device restarted."
} else {
    Write-Host "Could not find UMC 1820 device to restart automatically. Please unplug and replug the USB cable."
}

Write-Host "Done! Check your Windows Sound Control Panel."
