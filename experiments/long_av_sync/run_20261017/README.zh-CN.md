# 30 分钟音画同步验证 (2026-10-17)

- 环境: AviUtl2 v2.1.12 x64, 通过 Free FPS Export 导出 (66 / 50 / 60000/1001 三档)。插件: `x264guiEx-FreeRenderFPS.aux2`, 版本资源 1.1.0, SHA-256 `114C307016679E852674DF7A974889CACF4E340449DA475578C3A1A259F6076B`, 1,532,928 字节, 来自 2026-10-17 17:52 重新打包的 `dist\AviUtl2-FreeRenderFPS-v1.1.0` (首版 1.1.0 构建不含精确模式)。
- 实测所用插件即用户已安装的插件 (`C:\ProgramData\aviutl2\Plugin\x264guiEx-FreeRenderFPS\`), 哈希与上面的 dist 文件一致。
- 分析: 用 `test_kit\分析\一键分析.ps1` 的长视频部分 (analyze_sync.py) 生成, 本目录为结果文本 long_66.txt、long_50.txt、long_5994.txt (原文件名 长视频_*.txt)。
- 视频文件 (mp4) 未入库, 仅保留文本结果。
- 结论汇总见 `docs/VALIDATION_MATRIX.md`。
