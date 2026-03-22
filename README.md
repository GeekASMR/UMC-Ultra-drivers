# Behringer UMC ASIO Driver — 纯原生直连模式

百灵达 UMC 系列声卡的自定义原生 ASIO 驱动程序。  
**完全绕过官方 DLL**，通过 IOCTL 直连内核驱动 `umc_audio.sys`，实现零拷贝 DMA 音频传输。

## 支持的设备

| 设备 | 输入 | 输出 | 最大采样率 | MIDI |
|------|------|------|-----------|------|
| UMC22 | 2 | 2 | 48kHz | ❌ |
| UMC202HD | 2 | 2 | 192kHz | ❌ |
| UMC204HD | 2 | 4 | 192kHz | ❌ |
| UMC404HD | 4 | 4 | 192kHz | ✅ |

## 特性

- 🎵 **低延迟 ASIO 音频 I/O** — 最小 8 采样缓冲
- 🔗 **纯原生直连** — 通过 TUSBAUDIO IOCTL 直接与内核驱动通信
- 🧠 **阴阳融合时序** — Event 等待保护 ADC + WFB ACK 保活 DAC
- ⚡ **零拷贝 DMA** — 直接读写硬件 DMA 缓冲区
- 📊 支持 8/16/32/64/128/256/512/1024/2048 帧缓冲区 (pow2)
- 🎛️ 支持 44.1kHz ~ 192kHz 采样率
- 🔌 自动设备检测
- 📝 完整的日志系统

## 架构

```
DAW Application (Studio One, Cubase, etc.)
    ↓ ASIO Interface (COM / bufferSwitch)
BehringerASIO.dll  ← 纯原生，不依赖官方 DLL
    ↓ TUSBAUDIO IOCTL (直连内核驱动)
umc_audio.sys  (Thesycon USB Audio 内核驱动)
    ↓ USB Audio Class
Behringer UMC Hardware
```

## 构建

### 要求

- Windows 10/11
- CMake 3.16+
- Visual Studio 2019/2022
- Windows SDK

### 编译步骤

```bash
# 配置
cmake -B build -G "Visual Studio 17 2022" -A x64

# 编译
cmake --build build --config Release

# 注册驱动 (需要管理员权限)
regsvr32 build\bin\Release\BehringerASIO.dll
```

### 注册/注销

```bash
# 注册 (需要管理员权限)
regsvr32 BehringerASIO.dll

# 注销
regsvr32 /u BehringerASIO.dll
```

## 核心技术

### 阴阳融合时序

轮询线程的三步严格顺序是整个驱动的灵魂：

```
1. WaitForSingleObject(Event)   ← 挂起等硬件中断（保护 Input ADC）
2. m_callback(bufferIndex)      ← DAW 渲染音频写入 Output DMA
3. WAIT_FOR_BUFFER (IOCTL ACK)  ← 告诉内核"已处理"（防止 Output DMA 清零）
```

### ASIO ABI 兼容性

`getSamplePosition` 使用 Thesycon `struct{hi, lo}` 二进制格式传递 64 位值，
而非标准 `long long`，确保与 DAW 的采样位置和时间戳解析完全兼容。

### 官方 API 集成

通过 `TUsbAudioApi` 动态加载官方 API DLL 来查询硬件支持的缓冲区大小列表，
确保 `getBufferSize` 返回的选项与硬件能力匹配（最小 8 采样）。

## 文件结构

```
src/
├── asio/           # ASIO SDK 接口定义
├── driver/         # 驱动核心实现
│   ├── BehringerASIO.*     # ASIO 驱动主类
│   ├── TusbAudioDirect.*   # TUSBAUDIO IOCTL 直连引擎
│   └── TUsbAudioApi.h      # 官方 API DLL 接口
├── com/            # COM 注册
│   ├── dllmain.cpp         # DLL 入口点
│   └── ClassFactory.*      # COM 类工厂
└── utils/          # 工具类
    └── Logger.h            # 日志系统

工具文件:
├── test_asio_spy.cpp       # 官方 ASIO 驱动行为截获工具
├── test_tusbaudio.cpp      # 官方 API 功能测试
└── ioctl_spy*.cpp          # IOCTL 抓包系列工具 (v1-v5)
```

## 日志

驱动日志文件位于: `%TEMP%\BehringerASIO.log`

## 许可

本项目使用 ASIO SDK 接口定义，需遵守 Steinberg ASIO SDK 许可协议。
