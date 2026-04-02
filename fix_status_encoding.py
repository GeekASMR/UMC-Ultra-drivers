import sys
import re

path = r'e:\Antigravity\成品开发\UMC\v7\setup.iss'
with open(path, 'rb') as f:
    content = f.read().decode('utf-8', 'ignore')

lines = content.split('\r\n')

for i in range(len(lines)):
    line = lines[i]
    if line.startswith('Name: "{commondesktop}\\ASIO Ultra'):
        lines[i] = 'Name: "{commondesktop}\\ASIO Ultra 控制台"; Filename: "{app}\\ASIOUltraControlPanel.exe"'
    elif 'StatusMsg:' in line and 'certutil' in line:
        pass # Not this line
    elif 'StatusMsg:' in line and 'time.reg' in line:
        lines[i] = 'Filename: "{cmd}"; Parameters: "/c """"{win}\\regedit.exe"" /s ""{tmp}\\time.reg"""""; StatusMsg: "正在导入系统底层签名安全授权证明..."; Flags: runhidden waituntilterminated'
    elif 'StatusMsg:' in line and 'pnputil' in line:
        lines[i] = 'Filename: "{cmd}"; Parameters: "/c ""pnputil.exe /add-driver ""{app}\\VirtualAudioRouter.inf"" /install"""; StatusMsg: "正在构建底层专用虚拟音频路由内核..."; Check: NeedsVirtualAudioRouter; Flags: runhidden waituntilterminated'
    elif 'StatusMsg:' in line and 'devcon' in line:
        lines[i] = 'Filename: "{cmd}"; Parameters: "/c """"{app}\\devcon.exe"" install ""{app}\\VirtualAudioRouter.inf"" ROOT\\VirtualAudioRouter"""; StatusMsg: "正在锁定虚拟硬件挂载端点与安全映射..."; Check: NeedsVirtualAudioRouter; Flags: runhidden waituntilterminated'
    elif 'StatusMsg:' in line and 'regsvr32' in line:
        # e.g., Filename: "{sys}\regsvr... Check: FoundBrand... StatusMsg: "..." 
        # let's just strip and reconstruct the statusmsg
        parts = line.split('StatusMsg:')
        if len(parts) == 2:
            left_part = parts[0]
            right_suffix = parts[1].split('"; Flags:')[1] if '"; Flags:' in parts[1] else ' runhidden waituntilterminated'
            lines[i] = left_part + 'StatusMsg: "正在挂载相关 ASIO 基础组件库..."; Flags:' + right_suffix
    elif 'uninstall.bat' in line and line.startswith('; '):
        lines[i] = '; 摒弃 uninstall.bat，采用 Inno Setup 纯原生执行'

with open(path, 'wb') as f:
    f.write(b'\xef\xbb\xbf')
    if lines[0].startswith('\ufeff'):
         lines[0] = lines[0][1:]
    f.write('\r\n'.join(lines).encode('utf-8'))

print("All remaining garbled icons and messages fixed!")
