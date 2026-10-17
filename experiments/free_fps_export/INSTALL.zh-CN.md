# Free FPS Export 安装说明（实验版）

本说明面向非程序员，按步骤操作即可。这是实验插件，不是正式发布版。

适用版本：AviUtl2 v2.1.12 (x64)。关闭 AviUtl2 之后再进行安装。

---

## 1. 要安装的文件

安装的是一个文件：

`experiments\free_fps_export\build\x64\free_fps_export.aux2`

- 文件大小：348,672 字节（约 340 KB）
- 修改日期：2026/10/17 14:49:49

如果这个文件不存在，请先在仓库目录中运行下面的命令生成它：

```
powershell -NoProfile -ExecutionPolicy Bypass -File experiments\free_fps_export\build_and_run.ps1
```

生成的文件是 `.aux2`（AviUtl2 通用插件），不是 `.auo2`（输出插件）。

---

## 2. 复制到哪里

在你的电脑上，AviUtl2 的插件文件夹是：

`C:\ProgramData\aviutl2\Plugin`

请在这个文件夹里新建一个子文件夹，名字随意，例如 `free_fps_export`，然后把文件复制进去。最终路径应为：

`C:\ProgramData\aviutl2\Plugin\free_fps_export\free_fps_export.aux2`

### 我是怎么判断的

我只读地检查了以下位置，没有写入、复制、移动或删除任何东西：

- `D:\aviutl2 v2.1.12\`：里面有 `aviutl2.exe`，也有一个 `Plugin` 文件夹，但这个 `Plugin` 文件夹里只有 SDK 文档、`lwinput.aui2` 和少量说明文件，没有任何 `.auo2` 输出插件。这里**没有** `data\Plugin` 文件夹，所以不是便携版的目录结构。
- `C:\ProgramData\aviutl2\`：这里有 `Plugin`、`Default`、`Backup`、`Language`、`Log` 等文件夹。`Plugin` 里有大量实际在用的插件，例如 `lwinput.aui2`、`import_midi_tempos.aux2`、`TextEditor.aux2`，以及 `lsmash.ini` 和 `lwi` 缓存（今天还有修改记录）。这说明 AviUtl2 的数据文件夹就是 `C:\ProgramData\aviutl2`。
- SDK 说明 `D:\aviutl2 v2.1.12\Plugin\aviutl2_plugin_sdk.txt` 写明：插件放在 `Plugin` 文件夹下，或者 `Plugin` 下面的一层子文件夹中都会被读取。本项目文档 `docs/INSTALL.md` 也使用同样的做法，即 `...\Plugin\x264guiEx-FreeRenderFPS\` 子文件夹。

所以本文推荐放在 `C:\ProgramData\aviutl2\Plugin\free_fps_export\` 下。

注意：

- 不要复制到 `D:\aviutl2 v2.1.12\Plugin\`。该文件夹不是这台机器上实际使用的插件目录。
- 桌面上有一个快捷方式 `aviutl2.exe - 快捷方式.lnk`，它指向 `D:\2026 objects\aviutl2beta42\aviutl2.exe`，这是另一个版本。不要把文件放到那个版本的文件夹里。

---

## 3. 重启 AviUtl2 并信任插件

1. 复制完成后，启动 AviUtl2。
2. 如果出现关于插件的警告或信任提示，请阅读后再确认。`docs/INSTALL.md` 中对同类插件的提示是：审阅宿主警告，信任该 AUO2 文件，并在要求时重启 AviUtl2。本插件是 `.aux2`，我没有在本机看到实际弹出的提示框，因此具体文字以你看到的为准。
3. 如果 AviUtl2 要求重启，请关闭后重新打开。

---

## 4. 检查插件是否已加载

启动 AviUtl2 后，点击菜单 **编辑（Edit）**，应该能看到一项：

**Free FPS Export...**

如果没有这一项，说明插件没有加载。请检查文件路径是否正确，并确认已经重启 AviUtl2。

---

## 5. 测试导出时使用哪个输出插件

本机 `C:\ProgramData\aviutl2\Plugin` 中的输出插件（`.auo2`）有以下几个：

| 文件 | 说明 |
|---|---|
| `x264guiEx-FreeRenderFPS\x264guiEx-FreeRenderFPS.auo2` | H.264 输出，可输出 MP4，有音频处理，**推荐** |
| `x265guiEx.auo2` | H.265 输出，也可输出 MP4，可作为备选 |
| `gif_output.auo2` | GIF 输出，没有音频，**不要**用于本测试 |

推荐使用 **x264guiEx-FreeRenderFPS**，输出 MP4 并带音频。

要点：

- 本插件自己的重定时功能（FreeRenderFPS 自带的钩子）在 v2.1.12 上不会启用。但只要不勾选 **Enable Free Render FPS**（启用自由渲染帧率），它就能正常导出。
- 任何能正常输出 MP4 的输出插件都可以使用，因为 Free FPS Export 会自己完成帧率重定时。
- 对话框中默认的输出插件名称是 `x264guiEx FreeRenderFPS`。请与 AviUtl2 输出列表中显示的名称核对一致；如果不一致，请手动填写列表中的准确名称。

注意：`C:\ProgramData\aviutl2\Plugin\` 下还有名为 `x264guiEx-FreeRenderFPS.backup-20260813-...` 和 `x264guiEx-FreeRenderFPS.backup-20260818-...` 的备份文件夹，里面各有一份 `x264guiEx-FreeRenderFPS.auo2`。`docs/INSTALL.md` 要求同一时间只应有一个 AUO2 生效。如果输出插件列表中出现重复项，请由你自己决定是否把这些备份文件夹移出 `Plugin` 文件夹；本安装说明没有要求也没有执行任何此类操作。

---

## 6. 使用流程和限制

- 使用前，项目必须已经保存为文件。如果项目从未保存，插件会拒绝运行，请先保存。
- 每次导出前，插件都会询问是否先保存当前项目。选择“否”或关闭提示框，会直接中止，不产生任何改动。
- 导出过程中，插件会临时打开一个经过重定时的副本进行导出。导出结束后（无论成功或失败），它会重新打开你的**原始项目**，不会停留在副本上。
- 副本和中间文件放在 `%TEMP%\FreeFpsExport` 下。导出结束后这些文件会被自动删除。如果想保留以便排查问题，可以在启动 AviUtl2 前设置环境变量 `FREE_FPS_EXPORT_KEEP_TEMP=1`。
- 插件界面文字目前只有英文。
- 这是实验插件。在真实 AviUtl2 中，菜单项、对话框、保存行为、事件顺序等尚未全部验证，请先用一个不重要的测试项目试用。

---

## 7. 卸载

直接删除刚才复制的文件即可：

`C:\ProgramData\aviutl2\Plugin\free_fps_export\free_fps_export.aux2`

如果你新建的文件夹 `free_fps_export` 里已经没有其他文件，也可以一并删除该文件夹。删除后重启 AviUtl2，编辑菜单中的 **Free FPS Export...** 就会消失。
