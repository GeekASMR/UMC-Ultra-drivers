# UMC Ultra ASIO Driver v6.2.1 工作进展与架构升级记录
📅 **修改日期:** 2026-03-27

---

## 🚀 1. 已完成的重大架构重构

### 1.1 安装器与环境解耦 (Installer Refactor)
- **纯净版底层卸载与安装挂载**：`install.bat` 与 `uninstall.bat` 已经完全剔除花里胡哨的冗余拷贝操作，仅保留纯粹的硬件底层设备流（`VirtualAudioRouter.sys` 的 `devcon` 挂载与卸载）。
- **COM 核心隔离注册**：ASIO 的动态链接库（`BehringerASIO.dll`）的 `regsvr32` 注册强制提升至 Inno Setup 的 64 位静默模式自动接管，再也不会产生由 32 位代理拦截产生的 `0x800700C1` 注册失败。

### 1.2 命名空间与状态脱敏 (Driver Namespace Rebrand)
- **全局宿主显性名单**：注册表的 `Description` 全面锁死并清洗为极其干净的 `UMC Ultra`，以确保更新老版本的用户能够平滑继承原有的激活。
- **内部精准动态状态显示**：在 DAW 的 ASIO 参数表内（宿主向底层询问详情时）：
  - 激活状态：动态显示为 `UMC Ultra`
  - 试用状态：动态显示为 `UMC Ultra By ASMRTOP(trial)`
  - 过期状态：精准显示为 `UMC Ultra By ASMRTOP(Expired)`

### 1.3 【软隔离】机制的实施 (Soft-Isolation Anti-Piracy Framework)
**为了彻底解决旧版未激活时，用户哪怕只想弹一次激活码后续也会锁死（导致必须重启宿主）的重大 Bug，我们完全推翻了 `ASIOFalse` 的粗暴拒载方式，采取了国际顶尖声卡的“惩罚性软拦截”：**
- **移除硬锁 (Removed Hard Block)**：在 `BehringerASIO::init()` 中，取消了 `return ASIOFalse;`，向所有宿主一律返回初始化成功信号，防止被 DAW 打入黑名单。
- **底层消音切片 (Audio Mute Penalty)**：在 `BehringerASIO::onBufferSwitch`（每秒高频回调）的闭环最末端，若检测到未授权，暴力执行大面积物理静音 `memset(..., 0)`。
*(需手动编译验证该策略：相关更改附着在桌面生成的草稿中，当前正由技术人员集成进 VS)*

---

## 📌 2. 遗留锁与本地极速调试工具
由于系统占用与开发环境中各类文件句柄锁定的相互冲突问题（譬如 IDE 锁定 `BehringerASIO.cpp` 或宿主未完全关闭），在不破坏用户开发状态的前提下，为用户专属配发了本地防封锁强制更新探针：
- **`一键测试本机挂载V6.2.1.bat`（置于桌面）**：该特制工具能一步到位杀死相关的锁存进程 (Studio One)，洗脱过期注册表键值，并强行向 Win11 最底层系统路径注入 `System32` 并重连 `regsvr32`，以此加速未来任意一行原生 C++ 核心代码的单测重载！

---

## 🔮 3. 未来规划构想 (S-Tier Control Panel)
下一步，我们计划为 UMC Ultra 脱离宿主束缚，打造一个绝对独立的 **「UMC Control Panel」**（极客暗黑风，Electron / Web UI 渲染级或原生 C++ Win32 结合无边框贴网程序）。
- **完全接管 `controlPanel()`**：当用户在宿主中点击【控制面板】时，不再显示系统破旧的 `MessageBox` 对话框，而是瞬间开启豪华的外部常驻端面。
- **全栈式大功能整合**：实现包含**剩余试用时长、加密机器码授权直连输入、高级采样率切换、甚至 WDM 音系统静音开关**等完全商用的全栈操作管理！
- 不开启宿主也能通过**系统右下角小图标 (System Tray)** 直切激活环境，彻底击垮宿主闭环！
