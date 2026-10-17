# 面向 AviUtl2 和 AviUtl 1.x 的 FreeRenderFPS

**简体中文** | [日本語](README.ja.md) | [English](README.en.md)

FreeRenderFPS 基于 x264guiEx，同时提供 AviUtl2 和旧版 AviUtl 1.x 的宿主适配器，
可让场景求值采样率独立于工程 FPS。例如 30 FPS 工程输出 60 FPS 时，会真实求值
0.0、0.5、1.0、1.5……工程帧坐标，而不是复制帧或只修改容器 FPS 元数据。

## v1.1.0 更新

v1.1.0 将自由帧率导出放入单一 AviUtl2 插件文件，详见 [CHANGELOG.md](CHANGELOG.md)：

- `x264guiEx-FreeRenderFPS.aux2` 同时包含输出插件和“编辑 > **Free FPS Export...**”菜单项。后者通过 AviUtl2 公开 SDK 重新求值场景，在 AviUtl2 v2.1.12 上可使用任意有理输出帧率。
- 内置的“启用 Free Render FPS”Hook 仍只支持 AviUtl2 v2.1.4 x64；其他宿主会显示提示，指向 Free FPS Export。
- 安装器可将旧版 `.auo2` 安装迁移为 `.aux2`，备份保存在 `Plugin` 之外。`--cleanup-legacy-backups` 可把旧备份文件夹移出 `Plugin`（需显式启用，且只移动不删除）。
- 新增 `.aup2` 帧率转换工具、宿主无关测试和验证工具。

下方的发布门禁和旧版 Hook 实测表格是 v1.0.0 的结果。1.1.0 的 Free FPS Export 已在 AviUtl2
v2.1.12 上实测 8 组（见“Free FPS Export 实测”）。1.1.0 的内置 Hook 尚待在 AviUtl2 v2.1.4 上实测。
AviUtl 1.x 包仍为 v1.0.1，尚待在 AviUtl 1.10 + ExEdit 0.92 上重新实测。

## 下载

**AviUtl2 v1.0.0 发布门禁已通过。** 包内包含源码可对应、固定哈希并完成许可证审计的
x264、LGPL FFmpeg 和 ISC L-SMASH runtime。完整 FPS 矩阵、setup 与 portable
仅依赖最终包的干净环境真实编码均已通过。

AviUtl 1.x 版本是面向 AviUtl 1.10 + ExEdit 0.92 的独立 Win32 构建，位于
`dist\AviUtl1-FreeRenderFPS-v1.0.1`，提供手动安装 portable ZIP；包内不包含
AviUtl 宿主本体。

## 一键安装

1. 完整解压 v1.1.0 setup ZIP。
2. 双击其中与 `payload` 相邻的 setup EXE。
3. 重启 AviUtl2。
4. 选择 `x264guiEx FreeRenderFPS`。

安装器默认安装到独立的
`%ProgramData%\aviutl2\Plugin\x264guiEx-FreeRenderFPS`，升级前创建时间戳
备份，并支持 uninstall/rollback。它会拒绝任何位于原版 `x264guiEx`
目录内的目标。高级选项可在终端运行 `--help` 查看。解压后的 `payload`
目录必须与 EXE 保持相邻。

## 手动安装

将 v1.1.0 portable ZIP 直接解压到 AviUtl2 根目录；包内已包含
`data\Plugin\x264guiEx-FreeRenderFPS`。必须保持正式 `x264guiEx-FreeRenderFPS.aux2`
与三个相邻 INI 同名同目录，不要复制 Debug、PDB、Probe、watcher 或断点工具，
也不要重命名正式文件。

### 旧版 AviUtl 1.x（Win32）

将 `AviUtl1-FreeRenderFPS-v1.0.1-portable.zip` 解压到 AviUtl 1.10 根目录，
并保持 `Plugins` 结构。`Plugins\x264guiEx-FreeRenderFPS.auo` 必须与三个同名
INI 放在一起；runtime 位于 `Plugins\exe_files`，预设位于
`Plugins\x264guiEx-FreeRenderFPS_stg`。不要把 AUO 重命名为旧的
`x264guiEx-FreeFPS.auo`，也不要同时加载旧副本。详见
[docs/LEGACY_AVIUTL1_SETUP.md](docs/LEGACY_AVIUTL1_SETUP.md)。

## 从 v1.0.x 升级

- 安装器在安装/升级时，会把插件文件夹中旧的 `x264guiEx-FreeRenderFPS.auo2`
  备份后移除；手动安装的请自行删除它。AviUtl2 不能同时加载 `.auo2` 与 `.aux2`。
- 如果复制过实验版 `free_fps_export.aux2`，请删除。它已被下面的 Free FPS Export
  取代，安装器不会处理它。
- 安装器的备份现在放在 `C:\ProgramData\aviutl2\FreeRenderFPS-backups`，不在 `Plugin` 内。
  `C:\ProgramData\aviutl2\Plugin` 下较早的 `x264guiEx-FreeRenderFPS.backup-*` 文件夹
  仍在原处，每个都含一份插件文件（较早的含 `.auo2`）。AviUtl2 会读取 `Plugin` 下一层
  子文件夹中的插件，这些副本可能被当作重复插件加载。默认的安装、升级、卸载不会动它们。
  如需把它们移入 `FreeRenderFPS-backups`（只移动，不删除），请运行安装器的
  `--cleanup-legacy-backups`；加 `--dry-run` 可先列出。

## 使用方法

1. 打开 `x264guiEx FreeRenderFPS` 设置。
2. 打开 `Free Render FPS` 标签页。
3. 勾选 `Enable Free Render FPS`。
4. 选择目标 FPS 预设。
5. 使用包内已审计 runtime 输出。

关闭 Free Render FPS 时走原始 x264guiEx 整数帧路径。开启时只改变视频求值
采样率和视频帧数，工程 FPS 不变，工程时长和音频采样数保持不变。

## Free FPS Export（AviUtl2 v2.1.12）

同一个 `x264guiEx-FreeRenderFPS.aux2` 还会在 AviUtl2 的 **编辑（Edit）** 菜单中加入
**Free FPS Export...**。在 AviUtl2 v2.1.12 上请用它获得自由渲染帧率：插件用公开 SDK
接口重新排布工程时间并导出，可输出任意有理数 FPS。导出前需先把工程保存为文件。2026-10-17 在 v2.1.12 上实测 8 组，见下文“Free FPS Export 实测”；按重定时后的运动模型，8 组运动检查全部通过（残差 0.146–0.192 px）。长时长累积漂移与音画同步仍在验证中。

## FPS 预设

23.976、24、25、29.97、30、48、50、59.94、60、66、72、90、120 和
Custom。内部使用有理数 rate/scale；59.94 对应 60000/1001。

## Free FPS Export 实测（AviUtl2 v2.1.12，1.1.0）

2026-10-17 通过 **编辑 > Free FPS Export...** 实测 8 组。结果在
`experiments/free_fps_matrix/run_20261017/`（SUMMARY.md）。

| 工程 → 目标 | 帧数 | 视频/音频时长 (秒) | 相邻近似重复帧 | 终点偏差（输出帧） | 运动残差 (px) | 状态 |
|---|---:|---:|---:|---:|---:|---|
| 60 → 66 | 330 | 5.000000 / 5.000000 | 0 | +0.10 | 0.156 | PASS |
| 72 → 60 | 300 | 5.000000 / 5.000000 | 0 | −0.17 | 0.146 | PASS |
| 30 → 59.94 (60000/1001) | 300 | 5.005000 / 5.005011 | 0 | +1.30 | 0.146 | PASS |
| 66 → 24 | 120 | 5.000000 / 5.000000 | 0 | −0.64 | 0.156 | PASS |
| 66 → 120 | 600 | 5.000000 / 5.000000 | 0 | +0.82 | 0.192 | PASS |
| 120 → 60 | 300 | 5.000000 / 5.000000 | 0 | −0.50 | 0.146 | PASS |
| 60 → 50 | 250 | 5.000000 / 5.000000 | 0 | −0.17 | 0.161 | PASS |
| 72 → 59 | 295 | 5.000000 / 5.000000 | 0 | −0.18 | 0.188 | PASS |

终点偏差是运动到达终点的时刻与原工程相比的差，以输出帧计。上限为 |1/源帧率 − 1/目标帧率| + 半个输出帧。
30 → 59.94 实测 +21.65 ms（1.30 输出帧，上限约 25.0 ms），66 → 24 实测 −26.5 ms（0.64 输出帧，上限约 47 ms）。运动模型按重定时后的帧范围计算；早期按原工程时间计算的模型把 66 → 24 和
30 → 59.94 判为 FAIL，已弃用。详见 [tools/aup2_fps_upconvert.md](tools/aup2_fps_upconvert.md)。

## 旧版 AviUtl2 Hook 实测（v1.0.0）

下表为 v1.0.0 内置 Hook 的结果，不代表 1.1.0 Free FPS Export。

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

内置 Hook 目前仅支持 **AviUtl2 v2.1.4 x64**；在 AviUtl2 v2.1.12 上请使用 Free FPS Export。
启用前会校验宿主 image size、入口
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

已打包的旧版输出位于 `dist\AviUtl1-FreeRenderFPS-v1.0.1`。

## 致谢

项目复用 [rigaya 的 x264guiEx](https://github.com/rigaya/x264guiEx) 的编码、
音频、封装、配置、日志和错误处理流程。

## 许可证

参见 [LICENSE](LICENSE)、[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
和 [docs/RUNTIME_DEPENDENCIES.md](docs/RUNTIME_DEPENDENCIES.md)。打包策略为
默认拒绝：来源或许可证不完整的外部二进制不会进入安装包。
