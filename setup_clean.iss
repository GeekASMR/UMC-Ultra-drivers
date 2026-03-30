; UMC Ultra ASIO Driver - Inno Setup Script
; V6.2.1 Production Build

#define MyAppName "UMC Ultra By ASMRTOP"
#define MyAppVersion "6.2.1"
#define MyAppPublisher "ASMRTOP Studio"
#define MyAppURL "https://geek.asmrtop.cn/asio/"

[Setup]
AppId={{221F98B7-432B-35CC-86D2-C0F3381F5ADD}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes
DisableWelcomePage=no
DisableFinishedPage=no
CloseApplications=force
RestartApplications=no
OutputDir=out
OutputBaseFilename=UMCUltra_V{#MyAppVersion}_Setup
Compression=lzma2/ultra64
SolidCompression=yes
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
WizardImageFile=wizard_image.bmp
WizardSmallImageFile=wizard_small.bmp
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=UMC Ultra 鍏ㄧ郴鍒楅€氱敤 ASIO 椹卞姩
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
ButtonNext=下一步 >
ButtonInstall=安装
ButtonBack=< 上一步
ButtonCancel=取消
ButtonFinish=完成
ClickFinish=点击“完成”退出安装向导。
FinishedHeadingLabel=安装完成        
FinishedLabel={#MyAppName} v{#MyAppVersion} 已成功部署！%n%n请打开您的宿主软件，在 ASIO 设备列表中选择“UMC Ultra By ASMRTOP”即可使用。
InstallingLabel=正在安装 {#MyAppName}，请稍候...
ExitSetupTitle=退出安装
ExitSetupMessage=安装尚未完成。确定要退出吗？

[Files]
Source: "VirtualAudioRouter.sys"; DestDir: "{app}"; Flags: ignoreversion
Source: "VirtualAudioRouter.inf"; DestDir: "{app}"; Flags: ignoreversion
Source: "virtualaudiorouter.cat"; DestDir: "{app}"; Flags: ignoreversion
Source: "time.reg"; DestDir: "{app}"; Flags: ignoreversion
Source: "devcon.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "install.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "uninstall.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "BehringerASIO.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "libusb-1.0.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "UMCControlPanel.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{commondesktop}\UMC Ultra 控制台"; Filename: "{app}\UMCControlPanel.exe"

[Run]
Filename: "{cmd}"; Parameters: "/c ""{app}\install.bat"""; StatusMsg: "Installing Virtual Audio Kernel..."; Flags: runhidden waituntilterminated
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\BehringerASIO.dll"""; WorkingDir: "{app}"; StatusMsg: "Registering UMC ASIO Platform..."; Flags: runhidden waituntilterminated

[UninstallRun]
Filename: "{cmd}"; Parameters: "/c ""{app}\uninstall.bat"""; Flags: runhidden waituntilterminated; RunOnceId: "UninstallDriver"
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\BehringerASIO.dll"""; WorkingDir: "{app}"; Flags: runhidden waituntilterminated; RunOnceId: "UnregASIO"

[Code]
procedure KillDAWProcesses();
var
  ResultCode: Integer;
begin
  Exec('taskkill.exe', '/F /IM "Studio One.exe"', '', 0, ewWaitUntilTerminated, ResultCode);
  Exec('taskkill.exe', '/F /IM "Studio One 8.exe"', '', 0, ewWaitUntilTerminated, ResultCode);
  Exec('taskkill.exe', '/F /IM "StudioOne.exe"', '', 0, ewWaitUntilTerminated, ResultCode);
  Exec('taskkill.exe', '/F /IM "Studio One Pro.exe"', '', 0, ewWaitUntilTerminated, ResultCode);
  Exec('taskkill.exe', '/F /IM "REAPER.exe"', '', 0, ewWaitUntilTerminated, ResultCode);   
  Exec('taskkill.exe', '/F /IM "FL64.exe"', '', 0, ewWaitUntilTerminated, ResultCode);     
  Exec('taskkill.exe', '/F /IM "Pro Tools.exe"', '', 0, ewWaitUntilTerminated, ResultCode);
  Exec('taskkill.exe', '/F /IM "Bitwig Studio.exe"', '', 0, ewWaitUntilTerminated, ResultCode);
  Exec('cmd.exe', '/c wmic process where "name like ''%%Studio%%One%%''" call terminate', '', 0, ewWaitUntilTerminated, ResultCode);
  Sleep(2000);
end;

function InitializeSetup(): Boolean;
begin
  KillDAWProcesses();
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    RegWriteStringValue(HKLM64, 'SOFTWARE\ASIO\UMC ASIO Driver', 'CLSID', '{0351302F-B1F1-4A5D-8613-787F77C20EA4}');
    RegWriteStringValue(HKLM64, 'SOFTWARE\ASIO\UMC ASIO Driver', 'Description', 'UMC ASIO Driver');
    RegWriteStringValue(HKLM32, 'SOFTWARE\ASIO\UMC ASIO Driver', 'CLSID', '{0351302F-B1F1-4A5D-8613-787F77C20EA4}');
    RegWriteStringValue(HKLM32, 'SOFTWARE\ASIO\UMC ASIO Driver', 'Description', 'UMC ASIO Driver');
  end;
end;
