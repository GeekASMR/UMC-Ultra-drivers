import sys

path = r'e:\Antigravity\成品开发\UMC\v7\setup.iss'
with open(path, 'rb') as f:
    content = f.read().decode('utf-8', 'ignore')

lines = content.split('\r\n')

start_idx = -1
end_idx = -1
for i, line in enumerate(lines):
    if line.startswith('VersionInfoCompany='):
        start_idx = i
    if line.startswith('[Files]'):
        end_idx = i
        break

if start_idx != -1 and end_idx != -1:
    new_text = '''VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=ASIO Ultra 全系列通用 ASIO 驱动
VersionInfoProductName={#MyAppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
SetupAppTitle=安装 - {#MyAppName}
SetupWindowTitle=安装 - {#MyAppName} v{#MyAppVersion}
WelcomeLabel1=欢迎安装 {#MyAppName}
WelcomeLabel2=即将安装 {#MyAppName} v{#MyAppVersion}。%n%n本驱动为您提供极速低延迟 ASIO 引擎与虚拟音频路由。%n%n安装时将自动关闭宿主软件，请先保存工程。
ClickNext=点击“下一步”继续，或点击“取消”退出安装。
ReadyLabel1=准备安装
ReadyLabel2a=点击“安装”开始部署内核引擎。
ReadyLabel2b=点击“安装”开始部署内核引擎。
PreparingDesc=安装程序正在准备安装 {#MyAppName}。
WizardPreparing=准备安装
PreviousInstallNotCompleted=底层核心检测到您前次有一个未完成的安装或卸载操作留下的强绑定文件锁，可能导致本次安装失败。%n%n建议您先重启一次电脑后再重新运行本安装程序。如果您执意继续，请忽略此提示。
PrepareToInstallNeedsRestart=为了确保底层驱动无缝注入，并防止全局全新的音频内核冲突，安装程序需要在继续之前重启您的电脑。%n%n系统重启后将自动开始后续的安装。%n%n请记得保存好您后台未终止的编曲工程文件，需要现在重启吗？
YesRadio=是，请立即重启电脑以继续（推荐）
NoRadio=否，我稍后再手动重启电脑
ButtonNext=下一步 >
ButtonInstall=安装
ButtonBack=< 上一步
ButtonCancel=取消
ButtonFinish=完成
ClickFinish=点击“完成”退出安装向导。
FinishedHeadingLabel=安装完成        
FinishedLabel={#MyAppName} v{#MyAppVersion} 已成功部署！%n%n请打开您的宿主软件，在 ASIO 设备列表中选择 ASIO Ultra By ASMRTOP 即可使用，它将自动侦测并驱动您电脑上的所有原生声卡设备。
InstallingLabel=正在安装 {#MyAppName}，请稍候...
ExitSetupTitle=退出安装
ExitSetupMessage=安装尚未完成。确定要退出吗？

'''
    
    final_lines = lines[:start_idx] + new_text.split('\n') + lines[end_idx:]
    
    with open(path, 'wb') as f:
        # Write exact UTF-8 w/ BOM
        f.write(b'\xef\xbb\xbf')
        # Skip original BOM if present in final_lines[0]
        if final_lines[0].startswith('\ufeff'):
            final_lines[0] = final_lines[0][1:]
        
        f.write('\r\n'.join(final_lines).encode('utf-8'))
    print('Fixed successfully!!')
