# 面向 AviUtl2 和 AviUtl 1.x 的 FreeRenderFPS

**简体中文** | [日本語](README.ja.md) | [English](README.en.md)

FreeRenderFPS 基于 x264guiEx，同时提供 AviUtl2 和旧版 AviUtl 1.x 的宿主适配器，
可让场景求值采样率独立于工程 FPS。例如 30 FPS 工程输出 60 FPS 时，会真实求值
0.0、0.5、1.0、1.5……工程帧坐标，而不是复制帧或只修改容器 FPS 元数据。

## 下载

**AviUtl2 v1.0.0 发布门禁已通过。** 包内包含源码可对应、固定哈希并完成许可证审计的
x264、LGPL FFmpeg 和 ISC L-SMASH runtime。完整 FPS 矩阵、setup 与 portable
仅依赖最终包的干净环境真实编码均已通过。

AviUtl 1.x 版本是面向 AviUtl 1.10 + ExEdit 0.92 的独立 Win32 构建，位于
`dist\AviUtl1-FreeRenderFPS-v1.0.0`，提供手动安装 portable ZIP；包内不包含
AviUtl 宿主本体。

## 一键安装

1. 完整解压 v1.0.0 setup ZIP。
2. 双击其中与 `payload` 相邻的 setup EXE。
3. 重启 AviUtl2。
4. 选择 `x264guiEx FreeRenderFPS`。

安装器默认安装到独立的
`%ProgramData%\aviutl2\Plugin\x264guiEx-FreeRenderFPS`，升级前创建时间戳
备份，并支持 uninstall/rollback。它会拒绝任何位于原版 `x264guiEx`
目录内的目标。高级选项可在终端运行 `--help` 查看。解压后的 `payload`
目录必须与 EXE 保持相邻。

## 手动安装

将 v1.0.0 portable ZIP 直接解压到 AviUtl2 根目录；包内已包含
`data\Plugin\x264guiEx-FreeRenderFPS`。必须保持正式 AUO2 与三个相邻 INI
同名同目录，
不要复制 Debug、PDB、Probe、watcher 或断点工具，也不要重命名正式文件。

### 旧版 AviUtl 1.x（Win32）

将 `AviUtl1-FreeRenderFPS-v1.0.0-portable.zip` 解压到 AviUtl 1.10 根目录，
并保持 `Plugins` 结构。`Plugins\x264guiEx-FreeRenderFPS.auo` 必须与三个同名
INI 放在一起；runtime 位于 `Plugins\exe_files`，预设位于
`Plugins\x264guiEx-FreeRenderFPS_stg`。不要把 AUO 重命名为旧的
`x264guiEx-FreeFPS.auo`，也不要同时加载旧副本。详见
[docs/LEGACY_AVIUTL1_SETUP.md](docs/LEGACY_AVIUTL1_SETUP.md)。

## 使用方法

1. 打开 `x264guiEx FreeRenderFPS` 设置。
2. 打开 `Free Render FPS` 标签页。
3. 勾选 `Enable Free Render FPS`。
4. 选择目标 FPS 预设。
5. 使用包内已审计 runtime 输出。

关闭 Free Render FPS 时走原始 x264guiEx 整数帧路径。开启时只改变视频求值
采样率和视频帧数，工程 FPS 不变，工程时长和音频采样数保持不变。

## FPS 预设

23.976、24、25、29.97、30、48、50、59.94、60、66、72、90、120 和
Custom。内部使用有理数 rate/scale；59.94 对应 60000/1001。

## AviUtl2 已测试转换

| 工程 → 目标 | 帧数 | 视频/音频时长 | 相邻重复帧 | 状态 |
|---|---:|---:|---:|---|
| 30 → 60 | 300 | 5.000000 / 5.000000 秒 | 0；300 帧全唯一 | PASS |
| 66 → 60 | 300 | 5.000000 / 5.000000 秒 | 0；300 帧全唯一 | 保留的集成产物 PASS |
| 66 → 59.94 | 300 | 5.005000 / 5.000000 秒 | 0；300 帧全唯一 | PASS |
| 66 → 24 | 120 | 5.000000 / 5.000000 秒 | 0；120 帧全唯一 | PASS |
| 66 → 120 | 600 | 5.000000 / 5.000000 秒 | 0；600 帧全唯一 | PASS |
| 120 → 60 | 300 | 5.000000 / 5.000000 秒 | 0；300 帧全唯一 | PASS |

60 秒 66→59.94 长测输出 3597 帧，端点差仅 8.333 ms，无累计有理数漂移。
详见 [docs/VALIDATION_MATRIX.md](docs/VALIDATION_MATRIX.md)。

### AviUtl 1.x 实际验证

| 宿主/工程 → 目标 | 帧数 | 视频/音频时长 | 解码帧哈希 | 状态 |
|---|---:|---:|---|---|
| AviUtl 1.10 + ExEdit 0.92，30 → 60 | 300 | 5.000000 / 5.000000 秒 | 299 个唯一帧；末端一组重复 | 连续运动区间 PASS |

末端重复是五秒测试工程在最后项目帧处的边界钳制；连续运动区间没有按 30 FPS
成对输出。51.2 → 60、51.2 → 60000/1001 和 66 → 60 仍需在旧版宿主上重新生成
实际媒体后，才能宣称覆盖 AviUtl2 的完整矩阵。详见
[docs/LEGACY_AVIUTL1_VALIDATION.md](docs/LEGACY_AVIUTL1_VALIDATION.md)。

## 支持的 AviUtl2

目前仅支持 **AviUtl2 v2.1.4 x64**。启用前会校验宿主 image size、入口
RVA、时间轴构造器指令和调用关系；未知版本会被拒绝。关闭 FreeFPS 时普通
输出仍可使用。

## 支持的旧版 AviUtl 1.x

旧版适配器仅支持 **AviUtl 1.10 + ExEdit 0.92**，使用版本固定且受保护的
ExEdit detour，是 x86 `.auo` 输出插件。x264/FFmpeg/L-SMASH 作为独立子进程运行，
64 位 Windows 可以使用包内 x64 runtime。其他 AviUtl 或 ExEdit 二进制会被拒绝。

## 已知限制

- 内部 Hook 与 AviUtl2 版本严格绑定。
- 整数帧缓存目前使用 neighbor-frame eviction 兼容方案。
- 开启 FreeFPS 时禁用 AFS、关键帧预扫描和 timecode 输出。
- FFmpeg/L-SMASH 有完整来源可追溯性，但不会虚假宣称跨路径字节完全可复现。
- 已测试的 portable ZIP 需手动放到 `data\Plugin`；打包脚本已经修正。
- 安装器是最小控制台 EXE，不是图形向导。
- 旧版适配器的 ExEdit 子帧余数精度为 0.01 个工程帧，并使用受保护的整数帧缓存
  eviction；最后输出样本可能被钳制到项目末帧。
- 旧版包目前直接验证了 30 → 60，扩展矩阵仍待重新生成实际媒体。

详见 [docs/KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md)。

## 构建

安装 Visual Studio Desktop C++ 工作负载和 .NET Framework 4.7.2 引用程序集，
然后运行：

```powershell
.\tools\package_release.ps1 -RunSelfTest
```

脚本会执行 Release x64 构建、白名单许可证安全打包、安装器编译与隔离生命周期
自测，最后生成 setup/portable ZIP 和 SHA-256 清单。详见
[docs/BUILD.md](docs/BUILD.md)。

旧版 Win32 插件可使用同一项目构建：

```powershell
msbuild .\src\x264guiEx-FreeFPS\x264guiEx\x264guiEx.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=Win32
```

已打包的旧版输出位于 `dist\AviUtl1-FreeRenderFPS-v1.0.0`。

## 致谢

项目复用 [rigaya 的 x264guiEx](https://github.com/rigaya/x264guiEx) 的编码、
音频、封装、配置、日志和错误处理流程。

## 许可证

参见 [LICENSE](LICENSE)、[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
和 [docs/RUNTIME_DEPENDENCIES.md](docs/RUNTIME_DEPENDENCIES.md)。打包策略为
默认拒绝：来源或许可证不完整的外部二进制不会进入安装包。
