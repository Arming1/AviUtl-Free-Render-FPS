# Free FPS Export 安装说明（实验版，已被取代）

> **已被取代**：Free FPS Export 现在随主插件 `x264guiEx-FreeRenderFPS.aux2` 一起安装，
> 不再需要单独安装 `free_fps_export.aux2`。请按 [docs/INSTALL.md](../../docs/INSTALL.md)
> 安装主插件。
>
> 如果你以前复制过 `free_fps_export.aux2`，请删除它（见第 4 节）。否则编辑菜单中的
> Free FPS Export 可能重复出现。

适用版本：AviUtl2 v2.1.12 (x64)。关闭 AviUtl2 之后再进行安装。

本说明只保留使用时的要点（第 1–3 节）和删除旧文件的步骤（第 4 节）。

---

## 1. 检查菜单是否可用

启动 AviUtl2 后，点击菜单 **编辑（Edit）**，应该能看到一项：

**Free FPS Export...**

该菜单项由 `C:\ProgramData\aviutl2\Plugin\x264guiEx-FreeRenderFPS\x264guiEx-FreeRenderFPS.aux2`
提供。如果没有这一项，请检查该文件是否存在，并确认已经重启 AviUtl2。

---

## 2. 测试导出时使用哪个输出插件

本机 `C:\ProgramData\aviutl2\Plugin` 中的输出插件有以下几个：

| 文件 | 说明 |
|---|---|
| `x264guiEx-FreeRenderFPS\x264guiEx-FreeRenderFPS.aux2` | 同一个文件同时提供 H.264 输出插件 `x264guiEx FreeRenderFPS`（可输出 MP4，有音频处理）和 Free FPS Export 菜单，**推荐** |
| `x265guiEx.auo2` | H.265 输出，也可输出 MP4，可作为备选 |
| `gif_output.auo2` | GIF 输出，没有音频，**不要**用于本测试 |

推荐使用 **x264guiEx FreeRenderFPS**，输出 MP4 并带音频。

要点：

- 本插件自己的重定时功能（FreeRenderFPS 自带的钩子）在 v2.1.12 上不会启用。但只要不勾选 **Enable Free Render FPS**（启用自由渲染帧率），它就能正常导出。
- 任何能正常输出 MP4 的输出插件都可以使用，因为 Free FPS Export 会自己完成帧率重定时。
- 对话框中默认的输出插件名称是 `x264guiEx FreeRenderFPS`。请与 AviUtl2 输出列表中显示的名称核对一致；如果不一致，请手动填写列表中的准确名称。

注意：`C:\ProgramData\aviutl2\Plugin\` 下还有名为 `x264guiEx-FreeRenderFPS.backup-20260813-...` 和 `x264guiEx-FreeRenderFPS.backup-20260818-...` 的备份文件夹，里面各有一份旧版插件文件（`x264guiEx-FreeRenderFPS.auo2`）。AviUtl2 会读取 `Plugin` 下一层子文件夹中的插件，因此这些副本可能造成重复项。是否把这些备份文件夹移出 `Plugin` 文件夹由你自己决定；本说明没有要求也没有执行任何此类操作。

---

## 3. 使用流程和限制

- 使用前，项目必须已经保存为文件。如果项目从未保存，插件会拒绝运行，请先保存。
- 每次导出前，插件都会询问是否先保存当前项目。选择“否”或关闭提示框，会直接中止，不产生任何改动。
- 导出过程中，插件会临时打开一个经过重定时的副本进行导出。导出结束后（无论成功或失败），它会重新打开你的**原始项目**，不会停留在副本上。
- 副本和中间文件放在 `%TEMP%\FreeFpsExport` 下。导出结束后这些文件会被自动删除。如果想保留以便排查问题，可以在启动 AviUtl2 前设置环境变量 `FREE_FPS_EXPORT_KEEP_TEMP=1`。
- 插件界面文字目前只有英文。
- 这是实验性功能。在真实 AviUtl2 中，菜单项、对话框、保存行为、事件顺序等尚未全部验证，请先用一个不重要的测试项目试用。

---

## 4. 删除旧的 free_fps_export.aux2

如果你以前按本说明复制过 `C:\ProgramData\aviutl2\Plugin\free_fps_export\free_fps_export.aux2`，请删除该文件。
如果 `free_fps_export` 文件夹里已经没有其他文件，也可以一并删除该文件夹。删除后重启 AviUtl2，
Free FPS Export 仍然可用，只是由主插件 `x264guiEx-FreeRenderFPS.aux2` 提供。
