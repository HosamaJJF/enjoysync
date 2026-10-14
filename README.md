# EnjoySync

轻量的跨平台（macOS / Windows）系统声音实时传输工具。

底层采用与 SonoBus 同源的 AOO 音频引擎，支持局域网或公网 IP / 域名直连。**无需安装虚拟声卡**，开箱即用。

## 特性

- **免虚拟声卡采集**：基于 macOS 14.2+ `Core Audio Process Tap` 与 Windows 11 `Process Loopback` 直接抓取系统声音，自动排除 EnjoySync 自身播放的音频，避免回声。
- **低延迟与抗抖动**：48 kHz 双声道，默认使用 Opus（192 kbps / 10 ms），支持切换无损 24-bit PCM。内置 20–200 ms 自适应缓冲与 AOO 动态重采样，自动补偿两端时钟漂移。
- **单端口加密直连**：控制信令与音频流共用单个 UDP 端口（默认 `51000`），支持 IPv4 / IPv6 双栈与域名解析。采用 DTLS 1.2（AES-128-GCM）加密，通过 128-bit 密钥配对。
- **稳健的连接管理**：支持断线自动重连（指数退避）、设备身份去重、多地址自动寻路以及双向同时拨号收敛。
- **独立路由与轻量驻留**：支持多设备同时收发，每台设备可独立控制发送、接收、静音、音量与开机自连。支持托盘（Windows）与菜单栏（macOS，可隐藏 Dock 图标）后台驻留。
- **开机自启动**：在“设置 → 通用”中开启后，登录当前系统账户时自动启动 EnjoySync；开关立即生效。

## 快速上手

1. **获取密钥**：在两台设备上启动 EnjoySync，打开右上角“设置”（或按 `⌘,` / `Ctrl+,`），复制发送端的**连接密钥**。
2. **添加设备**：在接收端填入发送端的 IP / 域名（支持直接粘贴 `IP:端口` 格式）、端口和密钥，点击**添加并连接**。
3. **开始传输**：连接成功后，在发送端的设备列表中点亮**共享**图标（向上箭头，macOS 首次使用需授权系统录音权限）；接收端保持**接收**与**播放**开启即可听到声音。

> **关于连接与使用：**
>
> - **公网 / 跨网段连接**：不包含中继服务器与自动 NAT 穿透。跨公网直连需确保对端 UDP 端口可达。
>
> - **播放机制**：发送端开启共享后，其本机依然会正常播放声音。本项目定位于“低延迟实时转播”，不接管系统播放时钟，也不提供多扬声器高精度同步（Multi-room Sync）。
>
> - **采集限制**：受系统底层 API 限制，独占模式（Exclusive Mode）或带有 DRM 保护的音频流可能无法被采集。

## 下载与构建

测试版构建可直接从 GitHub Actions 的 **Build and test → Artifacts** 中下载（Windows 解压后直接运行 `EnjoySync.exe`，无需额外安装虚拟声卡或 VC++ 运行库）。

### 从源码构建

**环境要求**：CMake 3.25+、Git、Python 3、C++20 编译器。首次配置会自动拉取固定版本的开源依赖（也可将依赖放入 `.deps/JUCE`、`opus`、`aoo`、`mbedtls` 目录，或通过 `FETCHCONTENT_SOURCE_DIR_<NAME>` 指定本地路径）。

**macOS**（需 Xcode 与 macOS 14.2+ SDK）：

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-release --parallel 6
ctest --test-dir build-release --output-on-failure
codesign --force --deep --sign - build-release/EnjoySync_artefacts/Release/EnjoySync.app
open build-release/EnjoySync_artefacts/Release/EnjoySync.app

```

*(注：如需构建 Universal 通用二进制，可追加 `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`)*

**Windows**（需 Visual Studio 2022+ 及 C++ 桌面开发组件）：

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
.\build\EnjoySync_artefacts\Release\EnjoySync.exe

```

## 配置与调试

配置文件与传输日志默认保存在系统应用数据目录下：

- **macOS**：`~/Library/Application Support/EnjoySync/`
- **Windows**：`%APPDATA%\EnjoySync\`

“开机自启动”由系统启动项管理，设置窗口会在打开时读取注册状态。macOS 如显示“待批准”，请在系统设置的“通用 → 登录项”中允许 EnjoySync。Windows 便携版启用后请保留程序所在路径；移动程序后需重新关闭并开启此选项。如果在 Windows 系统设置或任务管理器中禁用了启动项，还需在那里重新启用。

**常用调试手段**：

- **自定义配置目录**：启动时传入 `--profile-dir /path/to/dir` 可隔离测试配置。
- **查看日志**：连接状态、DTLS 错误码及丢包统计默认记录在配置目录的 `logs/transport.log` 中（不包含密钥与音频内容）。
- **终端实时输出**：设置环境变量 `ENJOYSYNC_TRACE=1` 可将诊断信息同步输出至 `stderr`。
- **网络恢复测试**：运行 `enjoysync_tests --recovery` 可通过内置的 UDP 故障注入测试丢包、断流与同端口会话重建。Windows 双机测试流程详见 `docs/windows-testing.md`。

## 目录结构

- `src/core`：配置持久化、设备身份管理、地址解析与规范化
- `src/net`：双栈 UDP、DTLS 加密通道、连接状态机与重连策略
- `src/audio`：AOO 编解码封装、自适应缓冲、多设备混音
- `src/platform`：macOS / Windows 原生系统音频采集与桌面集成
- `src/app`：应用生命周期、GUI 界面与状态协调
- `tests`：基于真实回环链路的集成测试
- `docs/architecture.md`：线程模型、通信协议与状态机设计说明

## 开源协议

EnjoySync 原创代码基于 **AGPL-3.0-or-later** 协议开源。项目设计参考了 SonoBus 的技术路线并独立实现。第三方依赖库保留其各自许可，详见 `LICENSE` 与 `THIRD_PARTY_NOTICES.md`。
