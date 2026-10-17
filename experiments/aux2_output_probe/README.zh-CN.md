# aux2_output_probe: 通用插件能否注册输出插件

这是一个一次性的探针插件，用于验证：一个通用插件（`.aux2`）能否通过
`HOST_APP_TABLE::register_output_plugin` 注册输出插件，并且能否被
`EDIT_HANDLE::output_file()` 选中。

如果验证通过，之后就可以把 x264guiEx 输出插件和 Free FPS Export 菜单放进同一个 `.aux2`。

注意：本探针不是正式插件，只用于测试。

## 文件

- `probe.cpp`：插件源码
- `sdk/`：从 `experiments/auto_export/sdk/` 原样复制的 SDK 头文件（见 `sdk/SOURCE.md`）
- `build.ps1`：构建脚本（输出到 `build/x64/`，已被 git 忽略，不会复制到任何 AviUtl 目录）
- `build/x64/aux2_output_probe.aux2`：构建产物

## 安装（手动测试前）

1. 把 `experiments/aux2_output_probe/build/x64/aux2_output_probe.aux2` 复制到：

   ```
   C:\ProgramData\aviutl2\Plugin\aux2_output_probe\aux2_output_probe.aux2
   ```

   如果 `aux2_output_probe` 文件夹不存在，请先创建。
2. 重启 AviUtl2 v2.1.12。

## 手动测试步骤

请逐项测试，并把每一项的结果（通过/不通过，以及看到的内容）告诉我。

### (1) 输出插件列表中能看到 "FreeFPS probe output"

打开 AviUtl2 的文件输出（导出）对话框，查看输出插件下拉列表（菜单位置以你的界面为准）。
应能看到 **FreeFPS probe output**。

### (2) 设置按钮显示非零的 dll_hinst

在输出插件列表中选择 **FreeFPS probe output**，点击其设置（config）按钮。
应弹出一个消息框，内容类似：

```
probe config: dll_hinst=00007FF...
```

`dll_hinst` 的值应为非零。如果显示为 `0000000000000000`，请告诉我。

### (3) 正常导出会生成 .txt，且数值合理

用 **FreeFPS probe output** 正常导出一个项目（任选一个有画面和声音的项目，导出为 `.txt`）。
导出完成后，打开生成的 `.txt` 文件，应看到类似以下内容（数值取决于项目）：

```
w=1920
h=1080
rate=30
scale=1
n=90
audio_rate=44100
audio_n=...
flag=3
video0_ok=1
```

请告诉我 `w`、`h`、`rate`、`scale`、`n`、`audio_rate`、`audio_n`、`flag`、`video0_ok` 的实际值。
其中 `flag=3` 表示同时包含视频（1）和音频（2）。

### (4) Edit 菜单运行探针导出

在菜单中选择 **Edit > FreeFPS probe > Run probe export**（编辑 > FreeFPS probe > Run probe export）。

- 应弹出消息框：`output_file returned true`，下面一行是 `%TEMP%\freefps_probe_out.txt` 的完整路径。
- 导出完成后，`%TEMP%\freefps_probe_out.txt` 应存在，内容格式同第 (3) 项。

请告诉我：消息框显示 true 还是 false，以及该文件是否生成。
（`%TEMP%` 可在资源管理器地址栏输入 `%TEMP%` 打开。）

### (5) 卸载

1. 关闭 AviUtl2。
2. 删除文件夹 `C:\ProgramData\aviutl2\Plugin\aux2_output_probe\`（或至少删除其中的 `aux2_output_probe.aux2`）。
3. 如需清理测试输出，删除 `%TEMP%\freefps_probe_out.txt` 以及第 (3) 项导出的 `.txt` 文件。
4. 重新启动 AviUtl2，确认 "FreeFPS probe output" 和 "FreeFPS probe" 菜单已消失。

## 请报告

请分别回复以下四项的结果：

1. "FreeFPS probe output" 是否出现在输出插件列表中：是 / 否
2. 设置按钮消息中的 `dll_hinst` 数值：____
3. 正常导出的 `.txt` 内容（w、h、rate、scale、n、audio_rate、audio_n、flag、video0_ok）：____
4. Run probe export 消息框显示：true / false；`%TEMP%\freefps_probe_out.txt` 是否生成：是 / 否
