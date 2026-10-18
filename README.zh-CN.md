# 面向 AviUtl2 和 AviUtl 1.x 的 FreeRenderFPS

**简体中文** | [日本語](README.ja.md) | [English](README.en.md)

> **AI 辅助开发声明：** 本项目在 AI 辅助下开发。代码、文档和测试工具主要由 AI 编写（早期使用 OpenAI Codex，后续使用 Anthropic Claude / Claude Code）。作者负责策划、审查，并在 AviUtl2 实机上测试验证。

FreeRenderFPS 基于 x264guiEx，让场景在独立于工程 FPS 的时间点重新求值。例如 30 FPS 工程输出 60 FPS 时，会真实求值 0.0、0.5、1.0……工程帧坐标，而不是复制帧或只修改元数据。

## v1.3.0 要点

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

- **推荐：** `AviUtl2-FreeRenderFPS-v1.3.0-setup.zip`。完整解压后运行其中与 `payload` 相邻的 `setup` EXE，然后重启 AviUtl2。
- 手动安装：`AviUtl2-FreeRenderFPS-v1.3.0-portable.zip`，解压到 AviUtl2 根目录。

详细步骤、升级与卸载见 [docs/INSTALL.md](docs/INSTALL.md)。

## 怎么用

1. 在 AviUtl2 中打开 **编辑（Edit）> Free FPS Export...**。导出前请先把工程保存为文件。
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
