# 🔧 Behringer UMC ASIO 开发日志

> 本文件记录所有里程碑，方便恢复上下文

---

## 项目状态概览

- **当前方案**: ✅ 纯原生直连模式（完全脱离官方 DLL）
- **状态**: 稳定运行，Studio One / DAW 性能表正常
- **核心架构**: `DAW ←(ASIO)→ BehringerASIO.dll ←(IOCTL)→ umc_audio.sys ←(USB)→ 硬件`

---

## 里程碑记录

### M1 - 2026-03-22 16:58 - 开始攻克输出 DMA

**目标**: 搞清楚官方 DLL 在播放时做了哪些不同的操作

**现有线索**:
- `ioctl_trace3.log` 记录了 init → createBuffers → start 流程
- trace3 中 start() 只看到 `START_STREAMING (0x808828C8)`
- **没有看到 `ENABLE_STREAM (0x808828C0)`** — 因为 spy3 等待时间太短 (500ms)
- 我们的 `TusbAudioDirect::start()` 调用了 ENABLE_STREAM，但输出仍被清零
- 我们的轮询线程用 ctrlPage 轮询，**没有调用 `WAIT_FOR_BUFFER (0x808828F4)`**

**关键差异（待验证）**:
1. 官方 DLL 是否调用了 WAIT_FOR_BUFFER？频率如何？
2. ENABLE_STREAM 的参数是否一致？
3. 官方 DLL 是否通过 bufferSwitch callback 写入数据（而非直接写 DMA）？
4. 官方 DLL 的 ctrlPage 使用模式

**行动**:
- 创建 `ioctl_spy4.cpp` — 增强版 spy，在播放正弦波过程中记录完整 IOCTL 序列
- 重点: hook 到 ENABLE_STREAM + WAIT_FOR_BUFFER 的参数和调用模式

**文件变更**:
- 新增 `ioctl_spy4.cpp` — 完整播放过程 IOCTL 追踪工具
- 新增 `ioctl_spy4.exe` — 编译成功 (165 KB, x64)
- 新增 `DEVLOG.md` (本文件)

**编译结果**: ✅ 成功 (2026-03-22 17:05)

### M1.1 - 运行 spy4 前的安全检查

**spy4 做了什么（安全）**:
1. 加载官方 `umc_audioasio_x64.dll` 到自身进程
2. 用 IAT Patch 拦截该 DLL 对 `DeviceIoControl` / `CreateEventW` 等的调用
3. 调用官方 DLL 的 ASIO 接口 (init→createBuffers→start→播放3秒→stop)
4. 在 bufferSwitch 回调中向输出缓冲区写入正弦波
5. 记录所有 IOCTL 调用参数和 DMA 缓冲区状态

**spy4 不做的事（不会蓝屏）**:
- ❌ 不修改内核驱动
- ❌ 不直接调用 DeviceIoControl（只拦截官方 DLL 的调用）
- ❌ 不安装任何驱动
- ❌ 不修改系统文件
- ❌ 不做 kernel hook

---

### 2026-03-22：突破性进展，完整抓取原生播放流程！

在编写 `ioctl_spy5` 工具深度探究"官方 DLL 到底执行了何种动作"时，我们查明了之前探针在 0.2 秒后闪退的原因：我们的代码在 `asioMessage(kAsioSupportsTimeInfo)` 响应该功能支持，**但向回调结构传参时漏传了 `bufferSwitchTimeInfo` (置为了 NULL)**。官方驱动的内部工作线程在循环触发时遇到了空指针引发越界异常 `0xC0000005` 并悄无声息地强制中断了宿主进程。

修复并在该回调中注入 32-bit (最高幅度) 的正弦波代码后，我们取得了圆满结果：

1. **`START_STREAMING` 后立即触发了 `ENABLE_STREAM`**。它的 4 字节入参内容是 `00 02 00 00` (0x200)，精准匹配 **512 大小的 BufferSize**
2. 内部线程以每秒 93-94 次（恰为 `48000/512`）完美连续送出 **`WAIT_FOR_BUFFER`**
3. **控制页 (`ctrlPage`)** 前 4 字节作为内部计数器，与 `WAIT_FOR_BUFFER` 轮数 **100% 同步**
4. **DMA 没有被清零！** 输出端的 DMA 内存保留并处理了非零的样本信号

---

### 🏆 2026-03-22 20:24 — 纯原生直连 ASIO 驱动完美验证通过！

**状态**: ✅ 输入输出双向完美，CPU 正常，零拷贝 DMA 直连

经过数小时的逆向工程、反复排雷和精密调试，我们完全攻克了 Behringer UMC TUSBAUDIO 原生直连的所有障碍，成功构建了一套**彻底抛弃官方 `umc_audioasio_x64.dll` 的纯原生 ASIO 驱动**。

**核心突破 — "阴阳融合"时序方案**:

轮询线程的三步严格顺序是整个驱动的灵魂所在：

```
1. WaitForSingleObject(m_eventsAuto[0])  ← 先挂起等硬件中断（保护 Input ADC）
2. m_callback(bufferIndex)               ← DAW 渲染音频写入 Output DMA
3. WAIT_FOR_BUFFER (ACK IOCTL)           ← 告诉内核"已处理完毕"（防止 Output DMA 被清零）
```

| 组合 | Input | Output | 原因 |
|------|-------|--------|------|
| 仅 WAIT_FOR_BUFFER | ❌ 异常 | ✅ 正常 | WFB 扰乱 ADC 但保活 DAC |
| 仅 WaitForSingleObject | ✅ 正常 | ❌ 无声 | Event 不扰乱 ADC 但内核无 ACK 清零输出 |
| **先 Event 后 WFB ACK** | ✅ 正常 | ✅ 正常 | **完美融合！** |

---

### 🐛 2026-03-23 03:24 — Studio One CPU 100% 问题终极修复！

**状态**: ✅ CPU 性能表正常（与官方驱动完全一致）

**问题**: Studio One 的 ASIO 性能监视器始终显示 CPU 100%，但实际音频处理正常（所有插件 0.0ms）。

**调试过程 — 尝试过但无效的方案**:

| # | 尝试 | 结果 |
|---|------|------|
| 1 | systemTime 用合成时钟（samplePos 推算） | ❌ 100% |
| 2 | 移除 `kSystemTimeValid` 标志 | ❌ 100% |
| 3 | systemTime 用 `QueryPerformanceCounter` | ❌ 100% |
| 4 | systemTime 用 `timeGetTime() * 1000000` | ❌ 100% |
| 5 | 不支持 `kAsioSupportsTimeInfo`，只用 `bufferSwitch` | ❌ 100% |
| 6 | `outputReady` 返回 `ASE_NotPresent` | ❌ 100% |
| 7 | `directProcess` 从 `ASIOFalse` 改为 `ASIOTrue` | ❌ 100% |
| 8 | `getLatencies` 匹配官方值（+72/+136 采样） | ❌ 100% |
| 9 | WFB 改为非阻塞（防止双倍周期） | ❌ 100%（但验证了回调频率 374.9Hz = 完美） |

**关键转折 — `test_asio_spy.cpp` 间谍工具**:

编写了一个加载官方 ASIO DLL 的间谍工具，截获了官方驱动的所有回调参数和返回值。发现了致命差异：

```
官方驱动 getSamplePosition 返回:
  sPos = 1099511627776  (= 256 << 32)    ← hi:lo struct 格式！
  tStamp = -3718894946179..              ← 同样是 hi:lo 格式

我们的驱动返回:
  sPos = 256                              ← 普通 long long
```

**根因**: ASIO ABI 二进制格式不兼容

Thesycon 官方 TUSBAUDIO 驱动使用 `struct{unsigned long hi; unsigned long lo}` 格式存储 64 位值，
但我们的代码使用标准 `long long` 格式。由于 x86 小端序的差异：

```
              内存布局 (8 字节)
            [offset 0-3]    [offset 4-7]
struct{hi,lo}:  hi (高32位)     lo (低32位)
long long:      低32位          高32位

当我们写入 long long = 256:
  内存: [00,01,00,00, 00,00,00,00]
  DAW 按 struct 读: hi=256, lo=0  → 解析为 256 × 2^32 = 天文数字！

正确写法 (hi=0, lo=256):
  内存: [00,00,00,00, 00,01,00,00]
  DAW 按 struct 读: hi=0, lo=256  → 解析为 256 ✅
```

**修复**: `getSamplePosition` 通过 `unsigned long*` 分别写入 hi/lo 两个 32 位分量。

**间谍工具同时发现的其他差异**:

| 功能 | 官方驱动 | 我们需要匹配 |
|------|---------|------------|
| `future(kAsioSupportsTimeInfo)` | `ASE_NotPresent` (-1000) | ✅ 已修复为 ASE_InvalidParameter |
| `outputReady()` | `ASE_NotPresent` (-1000) | ✅ 已修复为 ASE_NotPresent |
| 回调方式 | `bufferSwitch` (plain) | ✅ 已修复，不用 bufferSwitchTimeInfo |
| `directProcess` | `ASIOTrue` | ✅ 已修复 |
| `getLatencies` | in=bufSize+72, out=bufSize+136 | ✅ 已修复 |
| `getBufferSize` | min=8, max=2048, gran=-1 | ✅ 已修复，通过官方 API 查询 |

---

## 已解决的所有 Bug

1. ~~输出 DMA 被内核清零~~ → 发现需要 `WAIT_FOR_BUFFER` 作为 ACK 保活
2. ~~输入 ADC 采样异常~~ → 发现 `WAIT_FOR_BUFFER` 会干扰 ADC，必须先等 Event
3. ~~CPU 100% 死锁空转~~ → 使用 `m_eventsAuto[0]` 挂起线程，零 CPU 空转
4. ~~OVERLAPPED 竞态条件~~ → 轮询线程使用独立的 Event 和 OVERLAPPED 结构
5. ~~Studio One 性能表 100%~~ → ASIO ABI hi:lo 格式修复 (`getSamplePosition`)
6. ~~WFB OVERLAPPED 内存损坏~~ → WFB 超时从 50ms 改为非阻塞 + CancelIoEx
7. ~~CancelIo 无法跨线程取消~~ → 升级为 `CancelIoEx`

## 文件索引

| 文件 | 用途 |
|------|------|
| `src/driver/BehringerASIO.cpp` | ASIO 驱动主类（纯原生直连模式）|
| `src/driver/TusbAudioDirect.cpp` | TUSBAUDIO IOCTL 直连引擎 |
| `src/driver/TUsbAudioApi.h` | 官方 API DLL 动态加载接口 |
| `test_asio_spy.cpp` | 官方 ASIO 驱动行为截获工具 ⭐关键调试工具 |
| `test_tusbaudio.cpp` | 官方 API 功能测试 |
| `ioctl_spy5.cpp` | v5 完整播放 IOCTL + Event 追踪 |
| `ioctl_spy4.cpp` | v4 完整播放追踪 |
| `ioctl_spy3.cpp` | v3 + 内存分配追踪 |
| `ioctl_spy2.cpp` | v2 改进版 |
| `ioctl_spy.cpp` | v1 基础 IOCTL 抓包 |
