# FreeRenderFPS v1.1.0 实机测试计划 (中文)

适用环境: AviUtl2 v2.1.12 (`D:\aviutl2 v2.1.12\aviutl2.exe`), 数据/插件目录 `C:\ProgramData\aviutl2\Plugin`。
仓库根目录: `D:\AviUtl Free Render FPS`。

安装包位于 `D:\AviUtl Free Render FPS\dist\AviUtl2-FreeRenderFPS-v1.1.0\`:
- setup ZIP: `AviUtl2-FreeRenderFPS-v1.1.0-setup.zip` (解压后含 `AviUtl2-FreeRenderFPS-v1.1.0-setup.exe` 与 `payload\`)
- portable ZIP: `AviUtl2-FreeRenderFPS-v1.1.0-portable.zip`
- 哈希清单: `SHA256SUMS.txt`

**不要使用桌面快捷方式**: 它指向 beta42 (错误版本)。请直接运行 `D:\aviutl2 v2.1.12\aviutl2.exe`。

---

## 1. 安装前清理

- [ ] 1.1 关闭 AviUtl2 (安装器要求先关闭宿主)。
- [ ] 1.2 检查 `C:\ProgramData\aviutl2\Plugin` 下是否有以下文件夹, 有则删除整个文件夹:
  - `C:\ProgramData\aviutl2\Plugin\aux2_output_probe\` (一次性探针, 当前存在, 应删除)
  - `C:\ProgramData\aviutl2\Plugin\free_fps_export\` (当前不存在则跳过; 若存在也要删除)
- [ ] 1.3 检查 `C:\ProgramData\aviutl2\Plugin` 下是否还有 `x264guiEx-FreeRenderFPS.backup-*` 旧备份文件夹。当前有两个:
  - `x264guiEx-FreeRenderFPS.backup-20260813-054308224Z-14264`
  - `x264guiEx-FreeRenderFPS.backup-20260818-090645662Z-46416`

  这两个文件夹内含额外的 `.auo2`, 会被 AviUtl2 当作重复插件加载。推荐处理方式 (只移动, 不删除):
  - [ ] 1.3a 先预览 (`<安装包目录>` 为第 2.2 步解压出的目录, 即 `D:\测试\FRF-v1.1.0-setup\`):
    ```powershell
    & "D:\测试\FRF-v1.1.0-setup\AviUtl2-FreeRenderFPS-v1.1.0-setup.exe" --cleanup-legacy-backups --dry-run
    ```
    确认输出列出的正是上面两个文件夹。
  - [ ] 1.3b 确认无误后执行移动:
    ```powershell
    & "D:\测试\FRF-v1.1.0-setup\AviUtl2-FreeRenderFPS-v1.1.0-setup.exe" --cleanup-legacy-backups
    ```
    两个文件夹会被移入 `C:\ProgramData\aviutl2\FreeRenderFPS-backups`, 名称带 `-from-plugin-<时间戳>` 后缀。
  - [ ] 1.3c 若不想使用安装器, 可手动把这两个文件夹剪切到 `C:\ProgramData\aviutl2\FreeRenderFPS-backups\` (需先手动创建该目录)。
- [ ] 1.4 不要安装或保留 `free_fps_export.aux2`。v1.1.0 的 Free FPS Export 已内置于 `x264guiEx-FreeRenderFPS.aux2`。

---

## 2. 安装 v1.1.0

### 2a. 安装包 (推荐)

- [ ] 2.1 在 `D:\AviUtl Free Render FPS\dist\AviUtl2-FreeRenderFPS-v1.1.0\` 中找到 setup ZIP (`AviUtl2-FreeRenderFPS-v1.1.0-setup.zip`)。
- [ ] 2.2 完整解压到一个新目录 `D:\测试\FRF-v1.1.0-setup\` (目录需为空或新建), 不要只取出 EXE; `payload` 目录必须与 `AviUtl2-FreeRenderFPS-v1.1.0-setup.exe` 相邻。
- [ ] 2.3 校验哈希: 将 setup ZIP 的 SHA-256 与 `SHA256SUMS.txt` 中 `AviUtl2-FreeRenderFPS-v1.1.0-setup.zip` 一行对比:
  ```powershell
  Get-FileHash -Algorithm SHA256 -LiteralPath "D:\AviUtl Free Render FPS\dist\AviUtl2-FreeRenderFPS-v1.1.0\AviUtl2-FreeRenderFPS-v1.1.0-setup.zip"
  ```
  不一致则停止, 不要安装。
- [ ] 2.4 运行 `AviUtl2-FreeRenderFPS-v1.1.0-setup.exe` (双击或在终端中运行), 按提示完成安装。

**期望结果:**
- 旧的 `x264guiEx-FreeRenderFPS.auo2` 被备份到 `C:\ProgramData\aviutl2\FreeRenderFPS-backups\` 下的时间戳文件夹中。
- `C:\ProgramData\aviutl2\Plugin\x264guiEx-FreeRenderFPS\` 中只剩 `x264guiEx-FreeRenderFPS.aux2` 与三个同名 INI (`.ini`, `.en.ini`, `.zh.ini`), 以及 `exe_files`、`x264guiEx_stg`。
- 不存在任何 `.auo2` 位于 `Plugin` 下 (见第 5 节检查命令)。

### 2b. 手动安装 (备用, 不推荐)

仅在安装器不可用时使用。注意: 本机的数据目录是 `C:\ProgramData\aviutl2\`, 不是 `D:\aviutl2 v2.1.12\data`, 所以不要直接解压 portable ZIP 到 AviUtl2 根目录 (那样会进入错误的数据目录)。

- [ ] 2b.1 删除 `C:\ProgramData\aviutl2\Plugin\x264guiEx-FreeRenderFPS\x264guiEx-FreeRenderFPS.auo2`, 并剪切到 `C:\ProgramData\aviutl2\FreeRenderFPS-backups\` 中保留 (目录不存在则先创建)。
- [ ] 2b.2 先用 `SHA256SUMS.txt` 校验 `AviUtl2-FreeRenderFPS-v1.1.0-portable.zip` 的哈希, 然后从其中取出 `data\Plugin\x264guiEx-FreeRenderFPS\` 整个文件夹 (以 ZIP 内实际内容为准), 复制到 `C:\ProgramData\aviutl2\Plugin\`。
- [ ] 2b.3 确认 `Plugin` 根目录下不再有第二份 `x264guiEx-FreeRenderFPS` 相关插件文件。

---

## 3. 启动检查

- [ ] 3.1 双击 `D:\aviutl2 v2.1.12\aviutl2.exe` 启动。遇到首次发现插件的宿主警告时, 阅读后确认 (信任 `x264guiEx-FreeRenderFPS.aux2`), 如要求重启则重启。
- [ ] 3.2 **输出插件列表**: 打开文件导出对话框, 输出插件下拉中 "x264guiEx FreeRenderFPS" **只出现一次**。若出现两次或出现 "FreeFPS probe output", 停止并报告。
- [ ] 3.3 **Edit 菜单**: 菜单 **编辑 (Edit) > Free FPS Export...** 存在。
- [ ] 3.4 **设置对话框**: 打开 x264guiEx FreeRenderFPS 的设置, 原有设置 (码率、预设等) 保留, 对话框能正常打开和关闭。
- [ ] 3.5 **Free Render FPS 标签页**: 出现新的提示文字, 记录其原文。
- [ ] 3.6 **宿主不支持提示**: 在 Free Render FPS 标签页中勾选 **Enable Free Render FPS**, 然后用普通的文件导出 (不是 Free FPS Export) 导出一个副本项目到 `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\smoke_check.mp4`。
  - 期望: 弹出中英双语的"不支持宿主"提示, 导出被拒绝, 不生成文件。
  - 记录提示的完整原文 (若与预期不同, 也要原样记录)。
- [ ] 3.7 **关闭开关**: 完成 3.6 后, 取消勾选 **Enable Free Render FPS**。第 4 节所有导出前都必须为关闭状态。

---

## 4. 自由帧率导出 8 组用例

用例定义、源 fixture、期望帧数与时长以 `experiments\free_fps_matrix\EXPORT_CHECKLIST.zh-CN.md` 为准 (汇总表与 `cases.json`)。以下只列出 v1.1.0 与该清单的差异, 以本计划为准:

1. **菜单位置**: 菜单是 **编辑 > Free FPS Export...**, 由 `x264guiEx-FreeRenderFPS.aux2` 提供。不需要安装 `free_fps_export.aux2`; 清单第二节中引用 `experiments\free_fps_export\INSTALL.zh-CN.md` 与 `dist\AviUtl2-FreeRenderFPS-v1.0.1\...` 的步骤已过时, 请忽略。
2. **对话框只有两项**: 目标 FPS 与输出文件。清单中的 "f. 输出插件" 步骤不存在, 跳过 (插件固定为 x264guiEx FreeRenderFPS)。
3. **先复制, 再导出**: 插件会在导出前把当前项目保存回它自己的路径。清单 "一、准备" 中的复制命令必须每个用例执行一次, 且只打开 `projects\` 下的副本, 不要打开仓库原件。
4. **导出后检查副本路径**: 每个用例结束后, 确认 AviUtl2 已重新打开的项目标题与路径是 `...\run_20261017\projects\<case>.aup2`, 而不是临时文件或原件路径。若不是, 立即停止并报告。
5. **开关必须关闭**: 每个用例导出前确认 **Enable Free Render FPS** 为关闭 (第 3.7 步)。
6. **输出文件**: 只使用 `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\<case>.mp4`。文件不在该目录中时停止并报告。
7. **目标 FPS 输入**: 按清单原样输入 (例如 `60000/1001`, 不要输入 `59.94`)。若对话框拒绝分数输入, 停止并报告, 不要改成小数。
8. **同日重跑**: 目录改为 `run_20261017_2`, 并同步修改复制与输出路径。

清单的 8 个用例为: `60_to_66`、`72_to_60`、`30_to_5994`、`66_to_24`、`66_to_120`、`120_to_60`、`60_to_50`、`72_to_59`。

- [ ] 4.0 运行目录预先创建:
  ```powershell
  New-Item -ItemType Directory -Force -Path "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects"
  ```
- [ ] 4.1 清单 "三、注意事项" 中的 `dry_run_simulated\` 不使用。
- [ ] 4.2 每个用例完成后, 在 `run_20261017\` 中确认 `<case>.mp4` 存在且非空, 并确认 AviUtl2 已重新打开副本。
- [ ] 4.3 全部 8 个用例完成后, 对话框、提示或错误的原文需一并记录 (见第 5 节)。

---

## 5. 验证与回传

### 5a. 检查安装状态 (在 PowerShell 中执行)

- [ ] 5.1 确认 `Plugin` 下 (含一层子文件夹) 只有一个 `.aux2`, 且没有 `.auo2`:
  ```powershell
  Get-ChildItem -LiteralPath 'C:\ProgramData\aviutl2\Plugin' -Recurse -Depth 1 -File |
    Where-Object { $_.Extension -in '.aux2','.auo2' } | Select-Object FullName
  ```
  期望只输出 `...\Plugin\x264guiEx-FreeRenderFPS\x264guiEx-FreeRenderFPS.aux2`。
- [ ] 5.2 确认 `Plugin` 下已无 `x264guiEx-FreeRenderFPS.backup-*` 文件夹, 且没有 `aux2_output_probe\`。
- [ ] 5.3 确认备份目录存在于 Plugin 之外:
  ```powershell
  Get-ChildItem -LiteralPath 'C:\ProgramData\aviutl2\FreeRenderFPS-backups'
  ```
  若目录不存在, 说明本次安装与移动未生效, 需报告。

### 5b. 运行分析脚本

- [ ] 5.4 在仓库根目录执行 (ffmpeg/ffprobe 不在 PATH 时追加 `--ffmpeg "<ffmpeg.exe>" --ffprobe "<ffprobe.exe>"`):
  ```powershell
  cd "D:\AviUtl Free Render FPS"
  python -I experiments\free_fps_matrix\analyze.py "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017"
  ```
- [ ] 5.5 记录退出码 (`0` 全部通过, `1` 有用例失败或缺少 MP4, `2` 输入或环境错误)。
- [ ] 5.6 只重跑部分用例时加 `--only 60_to_66,72_to_60` 之类参数。

### 5c. 回传内容

请回传以下内容:

1. `run_20261017\SUMMARY.md` 的全文 (或至少 "总体结论" 一行) 与 `summary.json`。
2. analyze.py 的退出码。
3. 任何 FAIL / MISSING 用例的 "细节" 段。
4. 所有对话框的完整文字 (包括 3.5 的标签页提示、3.6 的不支持提示、导出错误)。
5. 第 5.1 至 5.3 的命令输出。
6. 3.2 中输出插件列表里 "x264guiEx FreeRenderFPS" 的出现次数。
7. 任何与本计划预期不符的现象 (包括导出后项目路径不是副本)。

---

## 6. 回滚

如果安装后出现问题, 先关闭 AviUtl2, 再执行:

- [ ] 6.1 回滚到最新备份:
  ```powershell
  & "D:\测试\FRF-v1.1.0-setup\AviUtl2-FreeRenderFPS-v1.1.0-setup.exe" rollback --non-interactive
  ```
- [ ] 6.2 回滚到指定备份 (备份可在 `C:\ProgramData\aviutl2\FreeRenderFPS-backups\` 中选择, 也可能是 `Plugin` 下较早的 `backup-*` 文件夹):
  ```powershell
  & "D:\测试\FRF-v1.1.0-setup\AviUtl2-FreeRenderFPS-v1.1.0-setup.exe" rollback --non-interactive --backup "C:\ProgramData\aviutl2\FreeRenderFPS-backups\<备份文件夹名>"
  ```
- [ ] 6.3 需要完全卸载但保留用户设置时:
  ```powershell
  & "D:\测试\FRF-v1.1.0-setup\AviUtl2-FreeRenderFPS-v1.1.0-setup.exe" uninstall --non-interactive
  ```
  默认保留 `.conf` 与 `.stg`; 只有加 `--remove-user-data` 才会删除用户数据, 本计划不要加该参数。
- [ ] 6.4 回滚后重新执行第 5.1 步的检查, 并重新启动 AviUtl2 确认插件列表。

查看所有选项: `& "D:\测试\FRF-v1.1.0-setup\AviUtl2-FreeRenderFPS-v1.1.0-setup.exe" --help`。

---

## 不在本计划范围内

- 不修改桌面快捷方式 (如需修正, 由用户自行指向 `D:\aviutl2 v2.1.12\aviutl2.exe`)。
- 不在 `Plugin` 下保留任何备份副本。
- 不执行 `dry_run_simulated\` 中的模拟结果。
