import sys

path = r'e:\Antigravity\成品开发\UMC\v7\setup.iss'
with open(path, 'rb') as f:
    content = f.read().decode('utf-8', 'ignore')

lines = content.split('\r\n')

for i in range(len(lines)):
    if "'·ɷ'" in lines[i]:
        lines[i] = "    '目标声卡硬件勾选', '请勾选您正在使用的设备型号 (支持多选)',"
    elif "ǽѡµײͨעӦŽ΢롣ȫѡ豸ͨмȺѡ" in lines[i]:
        lines[i] = "    '安装程序将针对您所勾选的硬件型号注入底层 ASIO 对齐微码与虚拟通道路由阵列，多勾选不影响性能。',"
    elif "δ̽⵽κײ ASIO Ӳ˵㡿" in lines[i]:
        lines[i] = "    SoundCardPage.Add('【未探测到任何预期的底层 ASIO 硬件端点】');"
    elif "ע豸:" in lines[i]:
        lines[i] = "      SoundCardPage.Add('注入设备: ' + ScannedBrands[I]);"
    elif "̽Ŀڵ㡿" in lines[i]:
        lines[i] = "    WizardForm.WelcomeLabel2.Caption := WizardForm.WelcomeLabel2.Caption + #13#10#13#10 + '【已探明主脑物理目标节点】: ' + DetectedNameGlob;"
    elif "ǰΪ״̬ײ㲻漰κ ASIO ذ󶨷䡣" in lines[i]:
        lines[i] = "    WizardForm.WelcomeLabel2.Caption := WizardForm.WelcomeLabel2.Caption + #13#10#13#10 + '当前预置为通用沙盒离线状态，底层不涉及任何 ASIO 强绑定分发。';"
    elif "ǿоɱûıȨУ黺棬ʵ 100% ɾġáһΰװԴ֤" in lines[i]:
        lines[i] = "    // 强制猎杀用户本地的授权校验缓存，实现 100% 纯净删除“痕迹”，使得下一次安装会从零开始重新验证。"

with open(path, 'wb') as f:
    if not lines[0].startswith('\ufeff'):
        f.write(b'\xef\xbb\xbf')
    elif lines[0].startswith('\ufeff'):
        lines[0] = lines[0][1:]
        f.write(b'\xef\xbb\xbf')
    f.write('\r\n'.join(lines).encode('utf-8'))

print("Code section fixed successfully!")
