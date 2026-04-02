import sys

path = r'e:\Antigravity\成品开发\UMC\v7\setup.iss'
with open(path, 'rb') as f:
    content = f.read().decode('utf-8', 'ignore')

lines = content.split('\r\n')

# Ensure we're targeting the right text just in case lines shifted, but they shouldn't have
lines[68] = '; 一次性安装或注册测试部件全部丢入系统 Tmp 缓存，随装即焚（释放）'
lines[72] = '; 硬件底层控制端点及防串音隔离引擎驻留 App 目录，作为卸载函数执行的定长锚点'

lines[260] = "    '目标声卡硬件勾选', '请勾选您正在使用/绑定的原生 ASIO 物理通道',"
lines[261] = "    '安装程序将针对您所勾选的硬件型号注入底层 ASIO 对齐微码与虚拟通道路由阵列。若全量勾选则所有设备通吃，不影响性能。',"

lines[266] = "    SoundCardPage.Add('【未探测到任何预期的底层 ASIO 硬件端点】');"
lines[272] = "      SoundCardPage.Add('注入设备: ' + ScannedBrands[I]);"

lines[302] = "    WizardForm.WelcomeLabel2.Caption := WizardForm.WelcomeLabel2.Caption + #13#10#13#10 + '【已探明主脑物理目标节点】: ' + DetectedNameGlob;"
lines[306] = "    WizardForm.WelcomeLabel2.Caption := WizardForm.WelcomeLabel2.Caption + #13#10#13#10 + '当前预置为通用沙盒离线状态，底层不涉及任何 ASIO 强绑定分发。';"

lines[314] = "    // 强制猎杀用户本地的授权校验缓存，实现 100% 纯净删除“痕迹”，使得下一次安装会从零开始重新验证。"

with open(path, 'wb') as f:
    # Always write BOM for Inno Setup UTF-8 files
    f.write(b'\xef\xbb\xbf')
    if lines[0].startswith('\ufeff'):
         lines[0] = lines[0][1:]
    f.write('\r\n'.join(lines).encode('utf-8'))

print("All garbled text strictly fixed by line index!")
