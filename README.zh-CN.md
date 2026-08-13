# AviUtl2 FreeRenderFPS

**简体中文** | [日本語](README.ja.md) | [English](README.en.md)

FreeRenderFPS 是基于 x264guiEx 的 AviUtl2 输出插件，可让场景求值采样率
独立于工程 FPS。例如 30 FPS 工程输出 60 FPS 时，会真实求值 0.0、0.5、
1.0、1.5……工程帧坐标，而不是复制帧或只修改容器 FPS 元数据。

## 下载

**v1.0.0 尚未发布。** 当前产物为 `v1.0.0-rc1` 许可证安全候选包。
由于现有外部编码器、封装器和音频工具缺少与二进制完全对应的源码/来源记录，
RC 包主动排除了这些文件；不能把它描述为完整的普通用户正式版。

## 一键安装

1. 完整解压 `AviUtl2-FreeRenderFPS-v1.0.0-rc1-setup.zip`。
2. 双击 `AviUtl2-FreeRenderFPS-v1.0.0-rc1-setup.exe`。
3. 重启 AviUtl2。
4. 选择 `x264guiEx FreeRenderFPS`。

安装器默认安装到独立的
`%ProgramData%\aviutl2\Plugin\x264guiEx-FreeRenderFPS`，升级前创建时间戳
备份，并支持 uninstall/rollback。它会拒绝任何位于原版 `x264guiEx`
目录内的目标。高级选项可在终端运行 `--help` 查看。解压后的 `payload`
目录必须与 EXE 保持相邻。

## 手动安装

高级用户可解压 portable ZIP，将 `Plugin\x264guiEx-FreeRenderFPS` 复制到
AviUtl2 的 `Plugin` 目录。必须保持正式 AUO2 与三个相邻 INI 同名同目录，
不要复制 Debug、PDB、Probe、watcher 或断点工具，也不要重命名正式文件。

## 使用方法

1. 打开 `x264guiEx FreeRenderFPS` 设置。
2. 打开 `Free Render FPS` 标签页。
3. 勾选 `Enable Free Render FPS`。
4. 选择目标 FPS 预设。
5. 配置用户自行提供的 x264/音频/封装工具后输出。

关闭 Free Render FPS 时走原始 x264guiEx 整数帧路径。开启时只改变视频求值
采样率和视频帧数，工程 FPS 不变，工程时长和音频采样数保持不变。

## FPS 预设

23.976、24、25、29.97、30、48、50、59.94、60、66、72、90、120 和
Custom。内部使用有理数 rate/scale；59.94 对应 60000/1001。

## 已测试转换

| 工程 → 目标 | 帧数 | 视频/音频时长 | 相邻重复帧 | 状态 |
|---|---:|---:|---:|---|
| 30 → 60 | 322 | 5.366667 / 5.366667 秒 | 0；322 帧全唯一 | 保留的集成产物 PASS |
| 66 → 60 | 300 | 5.000000 / 5.000000 秒 | 0；300 帧全唯一 | 保留的集成产物 PASS |
| 66 → 59.94 | — | — | — | 恢复后 RC 未重新验证 |
| 66 → 24 | — | — | — | 恢复后 RC 未重新验证 |
| 66 → 120 | — | — | — | 恢复后 RC 未重新验证 |
| 120 → 60 | — | — | — | 恢复后 RC 未重新验证 |

保留视频证明集成代码线确实进行了子帧场景求值，但不能替代缺失的干净安装包
回归矩阵。详见 [docs/VALIDATION_MATRIX.md](docs/VALIDATION_MATRIX.md)。

## 支持的 AviUtl2

目前仅支持 **AviUtl2 v2.1.4 x64**。启用前会校验宿主 image size、入口
RVA、时间轴构造器指令和调用关系；未知版本会被拒绝。关闭 FreeFPS 时普通
输出仍可使用。

## 已知限制

- 内部 Hook 与 AviUtl2 版本严格绑定。
- 整数帧缓存目前使用 neighbor-frame eviction 兼容方案。
- 开启 FreeFPS 时禁用 AFS、关键帧预扫描和 timecode 输出。
- RC 不包含尚未完成再分发审计的外部编码/封装/音频工具。
- 干净安装后的完整输出测试和四组 FPS 转换仍未完成。
- 安装器是最小控制台 EXE，不是图形向导。

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

## 致谢

项目复用 [rigaya 的 x264guiEx](https://github.com/rigaya/x264guiEx) 的编码、
音频、封装、配置、日志和错误处理流程。

## 许可证

参见 [LICENSE](LICENSE)、[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
和 [docs/RUNTIME_DEPENDENCIES.md](docs/RUNTIME_DEPENDENCIES.md)。打包策略为
默认拒绝：来源或许可证不完整的外部二进制不会进入安装包。
