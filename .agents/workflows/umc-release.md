---
description: UMC Ultra v7 全平台架构：多品牌编译、独立双端 Github 闭环发版流水线
---

这是经过实战检验淬炼出的最新一代 (V7) ASIO Ultra 完整发布工作流。它会自动处理 CMake 全矩阵动态依赖构建、本地提权测试环境清场、SHA256 微软硬件级黑卡数字证书注入、Inno Setup 打包，并兵分两路将产品无缝空投至**两个完全不同的 Github 仓库**。

// turbo-all

1. **环境纯净清退与全系重编封装**
使用最新优化过的核心入口点执行一键编译。该环节会自动调用底层的 `sign_and_build.ps1` 执行数字签名，并最终通过 Inno Setup 输出合法的 `ASIOUltra_V7.x.x_Setup_Signed.exe` 安装包。
```powershell
powershell -ExecutionPolicy Bypass -File "d:\Autigravity\UMCasio\clean_and_test.ps1"
```

2. **核心源码提交及本地大文件爆点预防**
确保 `git filter-branch` 或新加的大日志（如800MB的代理日志）已被脱掉。利用巨型 `postBuffer` 保证本地 HTTP 推送对代理 VPN 极度友好、防止握手截断。
```powershell
git config --global http.postBuffer 524288000
git config --global core.compression 0
git add -A
git commit -m "chore(release): Build and release v7 ASIO Ultra unified driver architecture"
```

3. **主源库分发：GeekASMR/UMC-Ultra-drivers**
此处**携源码推流**。注意：发布时的标题和描述说明**必须极简干练**。标题只写软件名和版本号（例如 `ASIO Ultra v7.1.5`），绝对不要写花哨文案或大标题。让用户轻松看懂，不要繁冗。
```powershell
git push -u origin master -f
gh release create v7.1.5 "e:\Antigravity\成品开发\UMC\v7\out\ASIOUltra_V7.1.5_Setup_Signed.exe" --title "ASIO Ultra v7.1.5" -F "d:\Autigravity\UMCasio\v7.1.5_clean_release_notes.txt"
```

4. **外部分发库部署：GeekASMR/ASIO-Ultra-drivers**
此处用于最终用户的直传下载更新。**绝对不能**携源码推送。同样，此处命名规则也是极致简洁干练，必须去除非必要的大标题及表情包内容（如禁用星空旗号等花哨描述），只允许单纯的 `ASIO Ultra v7.1.5`。
```powershell
gh release create v7.1.5 "e:\Antigravity\成品开发\UMC\v7\out\ASIOUltra_V7.1.5_Setup_Signed.exe" --repo GeekASMR/ASIO-Ultra-drivers --title "ASIO Ultra v7.1.5" -F "d:\Autigravity\UMCasio\v7.1.5_clean_release_notes.txt"
```
