# 面向 AviUtl2 和 AviUtl 1.x 的 FreeRenderFPS

**简体中文** | [日本語](README.ja.md) | [English](README.en.md)

> **AI 辅助开发声明：** 本项目在 AI 辅助下开发。代码、文档和测试工具主要由 AI 编写（早期使用 OpenAI Codex，后续使用 Anthropic Claude / Claude Code）。作者负责策划、审查，并在 AviUtl2 实机上测试验证。

> **开发状态：预览版（Beta）。** 版本号虽然是 1.x，但仍处于测试阶段。目前只在 AviUtl2 v2.1.12 上验证过（2.1.13、2.1.14 等新版本尚未验证），已验证的内容与结果整理在 [test_kit/](test_kit/) 中；AviUtl 1.x 适配器只用简单工程测试过。遇到无法精确转换的写法时，插件会拒绝并说明原因，而不是输出可能错误的视频。用于重要作品之前，请务必检查导出结果。问题与建议欢迎通过 Issues 或 X 反馈。

FreeRenderFPS 基于 x264guiEx，让场景在独立于工程 FPS 的时间点重新求值。例如 30 FPS 工程输出 60 FPS 时，会真实求值 0.0、0.5、1.0……工程帧坐标，而不是复制帧或只修改元数据。

## v1.3.1 要点

- Free FPS Export 新增“在独立进程中导出”复选框：原窗口不切换工程，另开一个 AviUtl2 窗口导出后自动关闭，避免含 SceneMotionBlur_K 的工程在切换时崩溃。
- 工程使用 SceneMotionBlur_K（MotionBlur_K.aux2）时，该复选框会自动勾选。
- 工程用到第三方插件或脚本时，建议尽量勾选“在独立进程中导出”，避免切换工程时卡死或闪退。

- 直線移動(時間制御) 可用于任意轨道；補間移動(時間制御) 放行。
- 时间控制的编辑曲线（行尾 `|` 参数）和“位置”带曲线的情况均原样保留。
- 大工程转换显著提速（35 MB 工程约 3 秒）。
- GitHub Actions 每次推送自动编译、测试，并展示宿主实测结果。
- 目标帧率过低（如 66 → 10）时，短对象会重叠，此类转换会被拒绝，暂不支持。

## 功能（1.2.5 起）

- 次合成逐场景帧率：源帧率低于被导出场景的次合成可保留原帧率。
- 时间控制与丢帧按秒精确换算。
- 精确运动（Precise motion）默认开启，对话框中已隐藏复选框。
- 放行 `イージング（通常）@イージング`。

## 下载与安装

- **推荐：** `AviUtl2-FreeRenderFPS-v1.3.1-setup.zip`。完整解压后运行其中与 `payload` 相邻的 `setup` EXE，然后重启 AviUtl2。
- 手动安装：`AviUtl2-FreeRenderFPS-v1.3.1-portable.zip`，解压到 AviUtl2 根目录。

详细步骤、升级与卸载见 [docs/INSTALL.md](docs/INSTALL.md)。

## 怎么用

1. 在 AviUtl2 中打开 **编辑（Edit）> Free FPS Export...**。不必先保存工程：勾选“在独立进程中导出”（默认勾选，从未保存的工程强制勾选）时，插件会把当前工程（含未保存的改动）存为快照，在另一个 AviUtl2 窗口中导出，原窗口的工程不变。取消勾选则在原窗口导出，此时需先保存工程。
2. 设置目标帧率、次合成帧率（默认“自动（推荐）”）。
3. 通过“编码设置...”选择 x264guiEx 编码参数，然后导出。

## 支持范围

- **AviUtl2 v2.1.12：** 使用 Free FPS Export。
- **内置 Hook：** 仅支持 AviUtl2 v2.1.4 x64。
- **AviUtl 1.x：** 使用 v1.0.1 包（AviUtl 1.10 + ExEdit 0.92，Win32）。

## 验证

- 2026-10-17 在 AviUtl2 v2.1.12 上实测精确运动的 8 组转换，全部通过（零重复帧，运动残差不超过 0.23 px）。
- 30 分钟音画同步 3 段（66、50、60000/1001 fps）全部通过。
- 完整记录见 [docs/VALIDATION_MATRIX.md](docs/VALIDATION_MATRIX.md)，CI 结果见 [GitHub Actions](https://github.com/Arming1/AviUtl-Free-Render-FPS/actions)。

## 已知限制

低帧率台阶的末帧、两点直线的末帧、位置小数 3 位等限制见 [docs/KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md) 与 [docs/RETIME_RULES.md](docs/RETIME_RULES.md)。

## 历史

逐版本更新内容见 [CHANGELOG.md](CHANGELOG.md)。

## 许可证

参见 [LICENSE](LICENSE) 与 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
