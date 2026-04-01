import subprocess
import re

content = """## v7.1.4
- **网络与激活机制修复**：强制启用 TLS 1.2 网络协议，解决部分老旧 Windows 10 及 Windows 7 系统的激活失败问题。
- **状态同步优化**：控制面板增加同步校验机制。若管理员在后台重置或解绑设备，本地控制面板会即时更新状态，避免显示异常。

## v7.1.3
- **平台同步优化**：多品牌编译、独立双端闭环发版流水线优化。

## v7.1.1
- **MOTU 兼容性修复**：修复注册表中特定 MOTU 设备的关键字过滤规则失效导致代理回退的问题。

## v7.1.0
- **卸载清理优化**：改进驱动卸载逻辑，自动扫描并清理系统内遗留的驱动专属注册表项，确保卸载后环境纯净。
- **设备识别与隐藏**：采用智能白名单验证系统，自动过滤并隐藏虚拟声卡或无效音频硬件，提升路由分配准确度。
- **宿主设备命名统一**：规范化音频宿主软件 (DAW) 中的设备显示名称，统一采用无痕式命名，防止受到其它厂商名称干扰。
- **原生通信底层**：重构并采用纯本地 Windows API 发起网络请求，避免被安全软件错误拦截或出现网络误判。

## v7.0.9
- **稳定性调优**：细节修复与底层逻辑常规维护更新。

## v7.0.8
- **底层音频引擎优化**：完全重写 WDM 与 ASIO 共享内存的同步机制，提升缓冲自适应填充的速度和精确度。
- **超低延迟爆音修复**：显著减少在 128 Buffer Size 等极低延迟下的细微爆音和中断现象，实现音频对位平滑输出。

## v7.0.7
- **通道立体声防错位**：修复了输入总数为奇数的音频接口（例如带独立对讲通道的设备），在缩混时可能导致的左右声道错位问题。通过自动补充虚拟占位符，始终确保虚拟通道的精确对齐。

## v7.0.6
- **硬件兼容提升**：新增支持 Avid MBOX Studio 等音频接口，改进相关通信链路，确保不干扰其官方驱动的独立运行。

## v7.0.5
- **诊断日志同步**：修复由于驱动名称变更后导致的调试数据及性能日志上传失败的错误，以确保后续技术支持跟进。

## v7.0.4 
- **系统初始化增强**：修复了在宿主软件权限受限降级时，偶发的初始化失败问题，大幅提升即插即用的成功率。
- **系统接口兼容**：解决了老旧版本通道遗留占用问题，允许同时稳定接管多种不同品牌的设备信号。

## v7.0.3
- **稳定性调优**：细节修复与底层逻辑常规维护更新。

## v7.0.2
- **环境独立精简**：移除对外部 VC++ 运行库的重度依赖，减少安装包体积，支持跨系统环境一键独立静默安装。
- **老款声卡防崩溃**：修复在载入含有特殊标志位的早期特定声卡配置时引发的控制面板崩溃。
- **隔离系统集成**：控制面板集成通道隔离能力，支持一键接管系统音频路径。
- **强效防抖抗杂音**：提升不同采样率混合播放下的系统时钟同步机制。
- **多平台广泛兼容**：原生深度支持 UMC、Audient、SSL、Mackie、MOTU 等专业接口。

## v7.0.1
- **首次重大重构发布**：架构初步确立与核心代理模块初步上线测试。
"""

notes = {}
current_ver = None
current_text = []

for line in content.split('\n'):
    m = re.match(r'^##\s+(v\d+\.\d+\.\d+)', line.strip())
    if m:
        if current_ver:
            notes[current_ver] = '\n'.join(current_text).strip()
        current_ver = m.group(1)
        current_text = []
    else:
        if current_ver:
            current_text.append(line)

if current_ver:
    notes[current_ver] = '\n'.join(current_text).strip()

repos = ['GeekASMR/ASIO-Ultra-drivers', 'GeekASMR/UMC-Ultra-drivers']

for repo in repos:
    print(f"Fetching releases for {repo}...")
    try:
        out = subprocess.check_output(['gh', 'release', 'list', '--repo', repo, '--limit', '100'], encoding='utf-8')
    except Exception as e:
        print("Failed to list", repo, e)
        continue
    
    tags = re.findall(r'(v7\.\d+\.\d+)', out)
    tags = list(set(tags))
    
    for tag in tags:
        print(f"Updating {repo} - {tag}")
        note_text = notes.get(tag, "- 提升系统稳定性和内核兼容性。")
        
        # Write note to temp file
        with open('temp_note.txt', 'w', encoding='utf-8') as f:
            f.write(note_text)
        
        # Call gh edit
        try:
            subprocess.check_call(['gh', 'release', 'edit', tag, '--title', f'ASIO Ultra {tag}', '--notes-file', 'temp_note.txt', '--repo', repo])
        except Exception as e:
            print(f"Failed to edit {tag}: {e}")
