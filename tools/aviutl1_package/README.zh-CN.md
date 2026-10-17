# AviUtl 1.x FreeRenderFPS v1.0.1

这是面向 AviUtl 1.10 + ExEdit 0.92 的 Win32 版本。它复用 x264guiEx
的编码、音频、封装、配置和预设流程，并在 ExEdit 的时间轴求值边界注入
子帧时间；不是复制帧、丢帧、只修改容器 FPS 或后处理转码。

## v1.0.1 变更

- 发布版本不再写入逐帧诊断日志 `freerenderfps_aviutl1_probe.log`。诊断跟踪
  只编译进本地诊断构建。
- FreeRenderFPS 启用期间，忽略宿主的复制帧标志，因此不会用前一张图像替代新帧。
- 安装和移除钩子时的线程挂起更安全：在挂起任何线程之前先收集线程 ID；若线程
  正在被改写的字节范围内执行，则拒绝打补丁（重试后失败）。若无法安全恢复钩子，
  则保留直通 detour。
- 复用缓存淘汰时的渲染结果，30 → 60 时每个输出帧只需一次渲染，而不是 1.5 次。
- 包内不含默认 `.conf`。

## 安装

1. 退出 AviUtl。
2. 将 `AviUtl1-FreeRenderFPS-v1.0.1-portable.zip` 解压到 AviUtl 1.10
   的根目录，保持包内的 `Plugins` 目录结构。
3. 确认以下四个文件位于 `Plugins` 同一目录：
   `x264guiEx-FreeRenderFPS.auo`、三个同名 `.ini` 文件。
4. `Plugins\exe_files` 中是本包对应的 x264、FFmpeg、L-SMASH runtime。
5. 重启 AviUtl，在输出插件中选择 `x264guiEx FreeRenderFPS`，打开
   `Free Render FPS` 并选择目标 FPS。

不要把 `.auo` 重命名为旧的 `x264guiEx-FreeFPS.auo`，也不要让旧插件和
本插件同时以同一功能副本加载。`.conf` 是用户状态文件，不包含在干净包中。

## 已验证

本次实际输出 `30 → 60 FPS`：300 个视频帧、60/1、5.000000 秒；音频为
44.1 kHz 双声道 AAC、217 帧、5.000000 秒。解码后的 300 帧中有 299 个
唯一哈希，仅在项目末端边界出现一组相邻重复帧；连续运动区间不再按 30 FPS
成对重复。完整数据见 `Docs\LEGACY_AVIUTL1_VALIDATION.md`。

上述 30 → 60 的测量使用的是 v1.0.0。v1.0.1 构建仍需在 AviUtl 1.10 +
ExEdit 0.92 上重新验证。

## 支持范围

插件是 x86 AviUtl 1.x 输出插件，宿主版本严格限制为官方 AviUtl 1.10 和
ExEdit 0.92。外部编码器/封装器作为独立子进程运行，x64 Windows 可使用包内
x64 runtime；不要求把 AviUtl 本体打包进来。

更多限制见 `Docs\KNOWN_LIMITATIONS.md` 和 `LEGACY_AVIUTL1_SETUP.md`。
