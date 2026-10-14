# EnjoySync

轻量的 macOS / Windows 系统声音实时传输工具。使用 SonoBus 同源的 AOO 音频引擎，提供 IP / 域名直连、独立播放控制与后台运行。当前版本 **0.1.0，开发预览版**。

## 已实现

- macOS 14.2+ Core Audio Process Tap；Windows 11 Process Loopback。直接采集系统声音并排除 EnjoySync 自身播放，无需安装虚拟声卡。
- 可配置 UDP 监听端口（默认 51000），IPv4 / IPv6 独立监听，域名多个地址依次尝试。
- DTLS 1.2 / AES-128-GCM 加密，使用随机生成的 128-bit 连接密钥进行配对。控制消息和音频共用一个 UDP 端口。
- 稳定设备身份、重复连接复用、域名 / IP 别名合并、双方同时连接时收敛到单一会话。
- 独立心跳，意外断线约 5 秒判定离线；指数退避重连，主动断开后停止重试。
- 每台设备独立选择接收、播放、共享、音量和启动后自动连接；保存设备历史。
- 48 kHz 双声道，默认 Opus 192 kbps / 10 ms，支持切换 24-bit PCM；最低缓冲 20–200 ms，发生欠载时自动增加，稳定后逐步降低；AOO 动态重采样补偿时钟漂移。
- Windows 托盘；macOS 菜单栏。关闭窗口继续工作，macOS 后台可隐藏 Dock 图标。
- 深浅主题默认跟随系统，可在独立设置窗口切换；设备操作使用图标和悬停提示，音量实时显示百分比。

## 使用

1. 两台设备分别运行 EnjoySync。从菜单栏 / 托盘菜单的“设置…”或主窗口右上角的齿轮打开本机设置，复制发送方的连接密钥。接收方填写发送方的 IP / 域名、监听端口和该密钥。
2. 点击“添加并连接”。连接成功后，两端都会出现对方设备。
3. 在发送方对应的设备行中开启“共享”图标（向上箭头）。首次采集时，macOS 会请求系统音频录制权限。
4. 接收方保持“接收”和“播放”开启，即可听到声音。可以同时播放多个设备。
5. 设备操作图标悬停时显示名称，高亮表示已开启。“播放”只控制本机静音；关闭“接收”会停止订阅音频。“自动连接”在应用下次启动时生效。音量滑块旁显示当前百分比。
6. 关闭窗口会转为后台运行；单击托盘 / 菜单栏图标恢复窗口，右键打开菜单，选择“退出 EnjoySync”结束传输。macOS 的应用菜单栏也提供“设备”菜单。

本机名称、监听端口、连接密钥、输出设备、编码、缓冲和后台 Dock 行为均在独立设置窗口中管理。外观可选择“跟随系统”（默认）、“浅色”或“深色”，切换后立即生效并保存；主窗口支持 `⌘,`（macOS）或 `Ctrl+,`（Windows）打开设置。

支持分别填写主机和端口，也可以直接粘贴 `192.168.1.20:51000`、`computer.example.com:51000` 或 `[2001:db8::20]:51000`。地址中带端口时，以该端口为准，添加成功后会自动拆分回两个输入框。裸 IPv6（如 `2001:db8::20`）和带接口标识的 IPv6 链路本地地址（如 `fe80::1%en0`）使用独立端口栏。旧版本保存的 `IP:端口` 记录会在加载时自动修复。

公网直连需要对方地址可达并允许对应 UDP 端口；IPv4 NAT 通常需要端口映射。首版不包含中继或自动 NAT 穿透。

发送端原本的系统声音保持正常播放。此版本以实时转播为目标，不接管其他应用的播放时间，也不提供多扬声器统一时刻播放。

## 构建

GitHub 仓库的 **Actions → Build and test → 成功的运行 → Artifacts** 提供测试构建。Windows 下载 `EnjoySync-windows-x64`，解开下载文件及其中的 ZIP 后运行 `EnjoySync/EnjoySync.exe`，无需安装虚拟声卡或额外的 VC++ 运行库。压缩包包含使用说明与第三方许可证。

需要 CMake 3.25+、Git、Python 3 和 C++20 编译器。首次配置会下载固定版本的开源依赖，无需安装浏览器运行时。

macOS：Xcode 与 macOS 14.2+ SDK；Ninja 可选。

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-release --parallel 6
ctest --test-dir build-release --output-on-failure
open build-release/EnjoySync_artefacts/Release/EnjoySync.app
```

Windows：Visual Studio 2022 或更新版本、Desktop development with C++、近期 Windows SDK。

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
.\build\EnjoySync_artefacts\Release\EnjoySync.exe
```

可用 `FETCHCONTENT_SOURCE_DIR_JUCE`、`FETCHCONTENT_SOURCE_DIR_OPUS`、`FETCHCONTENT_SOURCE_DIR_AOO`、`FETCHCONTENT_SOURCE_DIR_MBEDTLS` 指定本地依赖；项目下 `.deps/JUCE`、`.deps/opus`、`.deps/aoo`、`.deps/mbedtls` 也会自动识别。

macOS Universal 构建可增加 `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`。CI 默认构建 macOS arm64 和 Windows x64；其他架构与 Windows 采集仍需对应真机验证。

## 配置与调试

- macOS：`~/Library/Application Support/EnjoySync/settings.json`
- Windows：`%APPDATA%\EnjoySync\settings.json`
- 使用 `--profile-dir /absolute/path` 可指定开发配置目录。
- 配置包含设备连接密钥。macOS 保存时限制为当前用户读写，采用临时文件原子替换。
- 默认将连接、断线原因、DTLS 错误码、UDP 错误 / 丢弃计数和网络线程停顿记录到配置目录下的 `logs/transport.log`，每 30 秒记录连接健康信息。macOS 默认路径为 `~/Library/Application Support/EnjoySync/logs/transport.log`，Windows 为 `%APPDATA%\EnjoySync\logs\transport.log`。启动时保留旧日志最后约 512 KiB。
- 设置 `ENJOYSYNC_TRACE=1` 可将同样的诊断信息同时输出到标准错误；日志不记录连接密钥、控制消息内容或音频。
- 监听端口改变后会重建会话；仅勾选自动连接的设备会自动恢复。

## 验证范围与初版边界

`enjoysync_tests` 使用真实 UDP / DTLS / AOO / Opus / PCM 链路和合成音频，验证设置持久化、身份去重、双向播放、静音、错误密钥、IPv4 / IPv6、心跳、重连、主动断开和同时拨号。UDP 中继故障注入覆盖零散丢包、短时双向中断、单向超时和旧会话尚存时的同端口重启；可运行 `enjoysync_tests --recovery` 单独验证恢复场景。测试不采集真实系统声音。

GitHub Actions 对 Windows x64 和 macOS arm64 执行 Release 编译及集成测试，结果以具体运行记录为准。系统录音授权、真实播放设备延迟、蓝牙、睡眠唤醒、声卡热插拔和跨公网连接需要真机验证。Windows 双机验收步骤见 `docs/windows-testing.md`。DRM 保护或独占音频路径可能无法被系统回环 API 采集。

当前每台设备最多保存 16 个对端，每台设备保存一个主要连接地址；通过新地址成功连接到已知设备后，会更新该设备的主要地址。已建立连接失联后，优先重试最近成功的地址；该尝试失败后重新解析域名。系统 DNS 查询运行在独立线程，退出时可能等待系统解析超时。

## 代码结构

- `src/core`：配置、设备身份、地址规范化。
- `src/net`：双栈 UDP、DTLS、连接状态机、重连。
- `src/audio`：AOO 编解码、缓冲、逐设备混音。
- `src/platform`：系统音频采集、平台桌面行为。
- `src/app`：应用生命周期、桌面界面与配置协调。
- `tests`：真实回环链路集成测试。
- `docs/architecture.md`：线程、协议与状态说明。

## 开源许可

EnjoySync 原创代码采用 **AGPL-3.0-or-later**。第三方组件保留各自许可，详见 `LICENSE` 和 `THIRD_PARTY_NOTICES.md`。本项目参考 SonoBus 的技术路线，独立实现应用逻辑，未复制 SonoBus 应用源码。
