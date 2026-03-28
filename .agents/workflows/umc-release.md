---
description: UMC Ultra 一键化编译、免杀防毒数字签名包装与 Github 云端秒发闭环流水线
---

// turbo-all

1. 重建图标渲染：从原版独立驱动截取深色极客原版 UI U盾，并映射至全局编译路径内硬挂载生成。
```powershell
cmd /c "cd /d d:\Autigravity\UMCasio && C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe extract.cs && extract.exe && copy /y black_umc.ico src\gui\black_umc.ico"
```

2. C++ 动态链接库及分离式环境提权优化器重编极速体（Release）
```powershell
cmd /c "cd /d d:\Autigravity\UMCasio && cmake --build build --config Release --target UMCControlPanel && cmake --build build --config Release --target UMCOptimizer && cmake --build build --config Release --target BehringerASIO"
```

3. 调用全局封装防毒脚本（含生成数字黑卡、注入合法 Root 注册机制、规避 SmartScreen，Inno Setup 秒装以及直接拉起 Github 端静默发新）。
```powershell
powershell -ExecutionPolicy Bypass -File d:\Autigravity\UMCasio\sign_and_build.ps1
```
