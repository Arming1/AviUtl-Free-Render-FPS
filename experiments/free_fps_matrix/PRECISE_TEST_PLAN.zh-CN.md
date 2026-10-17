# 精确模式（Precise motion）实测计划 (中文)

适用环境: AviUtl2 v2.1.12 (`D:\aviutl2 v2.1.12\aviutl2.exe`)。仓库根目录: `D:\AviUtl Free Render FPS`。
本计划在 `x264guiEx-FreeRenderFPS.aux2` 重新构建后执行。1.1.0 正式包不含精确模式, 必须先重新构建。

精确模式的规则见 `tools\aup2_fps_upconvert.md` 的 “Precise mode” 一节。要点:
- 关键帧位置不变; 直线移动 (`直線移動,0`) 与常量轨迹的关键帧值改为原曲线在新时刻的值, 因此速度与原工程一致;
- 超出原终点的最后一个输出帧可能多走一点 (30 → 60000/1001 终点 201.74, 即超出 1.74 px);
- 其他模式的对象不变, 并列在 `.convert.json` 的 `precise.skipped` 中。

---

## 一、重新构建插件

### 1.1 注意事项

- [ ] 1.1.1 关闭 AviUtl2。打包或替换插件时, 正在运行的宿主会占用 `.aux2`。
- [ ] 1.1.2 确认 `src\x264guiEx-FreeFPS\x264guiEx\freefps_export\` 中的源码是要测试的版本。当前工作区有未提交修改, 构建的是工作区内容。
- [ ] 1.1.3 **若要保留现有 1.1.0 包, 先另存**。`tools\package_release.ps1` 在开始时会删除已存在的 `dist\AviUtl2-FreeRenderFPS-v1.1.0` (除非加 `-AllowDirtyStaging`), 同名目录会被替换。另存命令:
  ```powershell
  Copy-Item -LiteralPath "D:\AviUtl Free Render FPS\dist\AviUtl2-FreeRenderFPS-v1.1.0" -Destination "D:\AviUtl Free Render FPS\dist\AviUtl2-FreeRenderFPS-v1.1.0-before-precise" -Recurse
  ```

### 1.2 方式 A: 打包脚本 (推荐)

- [ ] 1.2.1 在仓库根目录运行 (完整打包与安装器自测, 输出到 `dist\AviUtl2-FreeRenderFPS-v1.1.0`):
  ```powershell
  cd "D:\AviUtl Free Render FPS"
  powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\package_release.ps1 -RunSelfTest
  ```
- [ ] 1.2.2 脚本失败时 (例如缺少 MSBuild 或第三方 artifacts), 原样记录报错并回传, 不要绕过。
- [ ] 1.2.3 确认 `dist\AviUtl2-FreeRenderFPS-v1.1.0\` 已生成, 并记录 `x264guiEx-FreeRenderFPS.aux2` 的 SHA-256 (回传用):
  ```powershell
  Get-FileHash -Algorithm SHA256 -LiteralPath "D:\AviUtl Free Render FPS\src\x264guiEx-FreeFPS\x264guiEx\Release\x64\x264guiEx-FreeRenderFPS.aux2"
  ```

### 1.3 方式 B: 手动替换 aux2 (不跑打包脚本)

- [ ] 1.3.1 按 `docs\BUILD.md` 构建 Release x64 (与 README 中 Win32 命令对应的 x64 形式), 得到 `src\x264guiEx-FreeFPS\x264guiEx\Release\x64\x264guiEx-FreeRenderFPS.aux2`。
- [ ] 1.3.2 备份宿主中现有的插件文件:
  ```powershell
  Copy-Item -LiteralPath "C:\ProgramData\aviutl2\Plugin\x264guiEx-FreeRenderFPS\x264guiEx-FreeRenderFPS.aux2" -Destination "C:\ProgramData\aviutl2\FreeRenderFPS-backups\x264guiEx-FreeRenderFPS.aux2.before-precise"
  ```
  (目录不存在时先用 `New-Item -ItemType Directory` 创建。)
- [ ] 1.3.3 只替换这一个文件, 两个 INI 与其他文件不动:
  ```powershell
  Copy-Item -LiteralPath "D:\AviUtl Free Render FPS\src\x264guiEx-FreeFPS\x264guiEx\Release\x64\x264guiEx-FreeRenderFPS.aux2" -Destination "C:\ProgramData\aviutl2\Plugin\x264guiEx-FreeRenderFPS\x264guiEx-FreeRenderFPS.aux2" -Force
  ```

### 1.4 安装与启动检查

- [ ] 1.4.1 若用方式 A, 按 `experiments\free_fps_matrix\TEST_PLAN_v1.1.0.zh-CN.md` 第 2 节安装 `dist\AviUtl2-FreeRenderFPS-v1.1.0` 的 setup。安装器对 `.aux2` 的覆盖行为本计划未验证, 只想换 aux2 时用方式 B 更直接。
- [ ] 1.4.2 启动 `D:\aviutl2 v2.1.12\aviutl2.exe`, 确认输出插件 “x264guiEx FreeRenderFPS” 只出现一次。
- [ ] 1.4.3 打开 **编辑 > Free FPS Export...**, 确认对话框有复选框 **Precise motion (exact speed for linear motion)**, 且默认勾选。**若没有这个复选框, 停止并报告, 不要继续。**
- [ ] 1.4.4 记录对话框的完整文字 (标题、字段、复选框) 作为回传内容。

---

## 二、精确模式回归 (8 组用例)

用例定义与源 fixture 与 `experiments\free_fps_matrix\EXPORT_CHECKLIST.zh-CN.md` 相同。本计划使用新运行目录 `run_precise_20261017`, 不覆盖 `run_20261017` 的 1.1.0 结果。同日重跑时改为 `run_precise_20261017_2`。

### 2.1 准备运行目录与副本

- [ ] 2.1.1 创建运行目录和副本目录:
  ```powershell
  New-Item -ItemType Directory -Force -Path "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_precise_20261017\projects"
  ```
- [ ] 2.1.2 每个用例复制一份新副本 (插件确认后会把当前工程保存回它自己的路径, 所以不能打开仓库原件):
  ```powershell
  $R = "D:\AviUtl Free Render FPS\experiments\free_fps_matrix"
  $P = "$R\run_precise_20261017\projects"
  Copy-Item -LiteralPath "$R\fixture_60fps.aup2" -Destination "$P\60_to_66.aup2"
  Copy-Item -LiteralPath "$R\fixture_72fps.aup2" -Destination "$P\72_to_60.aup2"
  Copy-Item -LiteralPath "$R\..\66_to_60_validation\freerenderfps_30fps.aup2" -Destination "$P\30_to_5994.aup2"
  Copy-Item -LiteralPath "$R\..\66_to_60_validation\freerenderfps_66fps.aup2" -Destination "$P\66_to_24.aup2"
  Copy-Item -LiteralPath "$R\..\66_to_60_validation\freerenderfps_66fps.aup2" -Destination "$P\66_to_120.aup2"
  Copy-Item -LiteralPath "$R\..\66_to_60_validation\freerenderfps_120fps.aup2" -Destination "$P\120_to_60.aup2"
  Copy-Item -LiteralPath "$R\fixture_60fps.aup2" -Destination "$P\60_to_50.aup2"
  Copy-Item -LiteralPath "$R\fixture_72fps.aup2" -Destination "$P\72_to_59.aup2"
  ```
  (8 个副本都要重新复制, 不要复用上一次导出的副本。)

### 2.2 导出 (每组都勾选 Precise motion)

在 AviUtl2 中: 打开副本 > **编辑 > Free FPS Export...** > 目标 FPS 输入表中字符串 > 输出文件填 `run_precise_20261017\<case>.mp4` 的完整路径 > 确认 > 保存提示选 **是** (只保存副本)。**每次都确认 Precise motion 为勾选状态。**

| 序号 | 用例 | 目标 FPS 输入 | 期望帧数 | 期望时长 (s) | 输出文件 |
|---|---|---|---:|---|---|
| 1 | `60_to_66` | `66` | 330 | 5 | `run_precise_20261017\60_to_66.mp4` |
| 2 | `72_to_60` | `60` | 300 | 5 | `run_precise_20261017\72_to_60.mp4` |
| 3 | `30_to_5994` | `60000/1001` (不要输入 59.94) | 300 | 1001/200 (= 5.005) | `run_precise_20261017\30_to_5994.mp4` |
| 4 | `66_to_24` | `24` | 120 | 5 | `run_precise_20261017\66_to_24.mp4` |
| 5 | `66_to_120` | `120` | 600 | 5 | `run_precise_20261017\66_to_120.mp4` |
| 6 | `120_to_60` | `60` | 300 | 5 | `run_precise_20261017\120_to_60.mp4` |
| 7 | `60_to_50` | `50` | 250 | 5 | `run_precise_20261017\60_to_50.mp4` |
| 8 | `72_to_59` | `59` | 295 | 5 | `run_precise_20261017\72_to_59.mp4` |

- [ ] 2.2.1 每组导出成功, `<case>.mp4` 存在且非空。记录对话框的 Precise 勾选状态和导出结果 (成功或错误原文)。
- [ ] 2.2.2 每组结束后确认 AviUtl2 已重新打开的是 `run_precise_20261017\projects\<case>.aup2` 副本。
- [ ] 2.2.3 如果输出文件不在 `run_precise_20261017\` 中, 或仓库原件被改动, 立即停止并报告。

### 2.3 运行分析脚本三次 (判据 precise, 两种对比模型)

analyze.py 的参数 (见脚本文件头部 docstring): 位置参数 `RUN_DIR`; `--label TEXT`; `--only ID[,ID...]`; `--timeline retimed|original|precise` (默认 `retimed`); `--ffmpeg PATH`; `--ffprobe PATH`。输出为 `RUN_DIR\SUMMARY.md` 与 `RUN_DIR\summary.json`, 每次运行都会覆盖它们, 所以每次运行之后要改名。依赖 Python 3、numpy, 以及 ffmpeg/ffprobe (在 PATH 中, 或用 `--ffmpeg` / `--ffprobe` 指定)。

**精确模式的判据是 `precise` 模型 (2.3.1)。** `original` 与 `retimed` 只作对比参考 (2.3.2、2.3.3), 说明见 2.4。

- [ ] 2.3.1 用判据模型 `precise` 运行 (直线沿原工程时间走, 延续到重定时后最后一帧):
  ```powershell
  cd "D:\AviUtl Free Render FPS"
  python -I experiments\free_fps_matrix\analyze.py "experiments\free_fps_matrix\run_precise_20261017" --label precise --timeline precise
  ```
  记录退出码 (期望 `0`), 然后把输出改名:
  ```powershell
  $RUN = "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_precise_20261017"
  Copy-Item "$RUN\SUMMARY.md" "$RUN\SUMMARY_precise.md"
  Copy-Item "$RUN\summary.json" "$RUN\summary_precise.json"
  ```
- [ ] 2.3.2 对比参考: `original` 模型 (原工程时间, 到原终点后保持):
  ```powershell
  python -I experiments\free_fps_matrix\analyze.py "experiments\free_fps_matrix\run_precise_20261017" --label precise --timeline original
  Copy-Item "$RUN\SUMMARY.md" "$RUN\SUMMARY_original.md"
  Copy-Item "$RUN\summary.json" "$RUN\summary_original.json"
  ```
- [ ] 2.3.3 对比参考: 默认的 `retimed` 模型 (重定时帧范围 s'..e'), 同样改名:
  ```powershell
  python -I experiments\free_fps_matrix\analyze.py "experiments\free_fps_matrix\run_precise_20261017" --label precise --timeline retimed
  Copy-Item "$RUN\SUMMARY.md" "$RUN\SUMMARY_retimed.md"
  Copy-Item "$RUN\summary.json" "$RUN\summary_retimed.json"
  ```
- [ ] 2.3.4 每次运行都会重写 `analysis\` 下的质心 CSV: `residual_px` 为所选模型的残差, `residual_original_time_px` 为原工程时间诊断残差。`summary.json` 的 `motion.max_abs_residual_px` 为所选模型的残差, `original_time_check.max_abs_residual_px` 始终为原工程时间诊断值。

### 2.4 预期结果

判据是 `precise` 模型, 预期 **8/8 PASS**, 退出码 `0`。运动检查的残差上限为 1.0 px。

下表是按各模型的公式, 对精确模式的理想轨迹 (`precise` 模型本身生成的 8 组逐帧位置, 无取整) 计算的最大残差。**不是实测值**。`precise` 列为 0 是按定义的自洽结果 (脚本自测, 见 2.5)。`original` 与 `retimed` 列是按同一条理想轨迹与这两个模型比较得到的偏差, 用来说明两者为什么不是判据。

| 用例 | `precise` 模型 (判据) | `original` 模型 (对比) | `retimed` 模型 (对比) | 说明 |
|---|---:|---:|---:|---|
| `60_to_66` | 0.00 px | 0.12 px | 0.12 px | 应 PASS |
| `72_to_60` | 0.00 px | 0.00 px | 0.22 px | 应 PASS |
| `30_to_5994` | 0.00 px | **1.74 px** | **1.74 px** | 应 PASS。`original`/`retimed` 超限, 原因见 2.4.3 |
| `66_to_24` | 0.00 px | 0.00 px | **2.13 px** | 应 PASS。`retimed` 超限, 原因见 2.4.3 |
| `66_to_120` | 0.00 px | 0.55 px | 0.55 px | 应 PASS |
| `120_to_60` | 0.00 px | 0.00 px | 0.67 px | 应 PASS |
| `60_to_50` | 0.00 px | 0.00 px | 0.27 px | 应 PASS |
| `72_to_59` | 0.00 px | 0.00 px | 0.25 px | 应 PASS |

- [ ] 2.4.1 **判据为 `precise` 模型, 8 组都应 PASS**。实测残差会有宿主对 trackbar 值取小数的少量偏差, 但应远小于 1.0 px。若某组实测超过 1.0 px, 或在中间帧出现偏差, 视为异常并报告。
- [ ] 2.4.2 `30_to_5994` 的理想轨迹在最后 2 帧 (n = 298, 299, 时间 4.9716 s 与 4.9883 s) 超过原终点 X_end, 精确模式会继续延伸到 521.74 px (原终点 520.00 px)。若实测在这 2 帧与 `precise` 模型不符, 报告并附 `analysis\30_to_5994.centroids.csv`。
- [ ] 2.4.3 对比参考 (不是判据):
  - `original` 在 `30_to_5994` 上为 1.74 px: 原模型在原终点 T = 4.9667 s 之后保持 X_end, 而精确导出的直线继续走, 最后 2 帧超过原终点。精确导出的 e' 为 299 帧 (4.9883 s), 比 T 晚约 0.022 s, 所以这 2 帧是偏差来源。
  - `retimed` 在 `66_to_24` 上为 2.13 px: `retimed` 模型让直线在 s'..e' (0 .. 4.9583 s) 上匀速到达 X_end, 即速度按重定时后的时长改变。精确模式保持原工程速度, 原工程时长 T = 4.9848 s, 比重定时后的 4.9583 s 长 0.0265 s, 中间帧的位置随之偏离, 最大 2.13 px。
  - `retimed` 在 `30_to_5994` 上同为 1.74 px: 精确导出在 e' (4.9883 s) 处已超过 X_end 1.74 px, `retimed` 模型在 e' 处到达 X_end 并保持, 两者在最后 2 帧相差 1.74 px。这两个模型只在上述原因下超限, 不代表导出错误。
  - 若 `retimed` 与 `original` 在其他用例 (如 `60_to_66`、`66_to_120`) 也超限, 那只是两者低于 1 px 的小偏差, 本身不影响判据。
- [ ] 2.4.4 帧数与时长 (validator 部分) 应与 1.1.0 矩阵 (`run_20261017`) 相同。
- [ ] 2.4.5 退出码预期为 `0`。`2` 或 `1` 都需报告, 附 `SUMMARY_precise.md` 全文。
- [ ] 2.4.6 反向对照 (已推算, 无需导出): 用 `precise` 模型分析 1.1.0 非精确导出 `run_20261017`, `30_to_5994` 为 1.80 px、`66_to_24` 为 2.18 px (FAIL), 其余 6 组为 0.23 到 0.77 px (PASS)。这表明 `precise` 模型能把这两例与非精确导出区分开, 但其余 6 例的残差低于 1 px, 因此非精确与精确导出的区分主要依赖这两例。

### 2.5 模型自测 (脚本推算, 无需导出)

`analyze.py` 的 `precise` 模型用于判据前, 需要对 8 组用例各生成一条理想精确轨迹 (逐帧 `x(t) = X_start + (X_end − X_start)·(t − s/Rp)/T`, 窗口 s'/Rt..e'/Rt), 再用 `precise` 模型比较。结果应为 8 组残差全部为 0。这证明的是公式与脚本实现一致, 不证明宿主实际行为; 实际行为由 2.2 的导出与 2.3.1 的分析确认。

---

## 三、额外小数位验证 (`--precise-extra-decimals 2`)

插件对话框没有额外小数位选项, 它固定写 0 位。本步骤用命令行工具生成两个版本, 在 AviUtl2 中打开和导出, 检查宿主是否接受额外小数位。

### 3.1 命令行生成两个版本

- [ ] 3.1.1 生成 0 位版本与 2 位版本 (输入仍是仓库原件, 工具只读输入; 输出写到运行目录):
  ```powershell
  cd "D:\AviUtl Free Render FPS"
  New-Item -ItemType Directory -Force -Path "experiments\free_fps_matrix\run_precise_20261017\decimals"
  python -I tools\aup2_fps_upconvert.py --in experiments\free_fps_matrix\fixture_60fps.aup2 --out experiments\free_fps_matrix\run_precise_20261017\decimals\60_to_66_p0.aup2 --target-fps 66 --precise
  python -I tools\aup2_fps_upconvert.py --in experiments\free_fps_matrix\fixture_60fps.aup2 --out experiments\free_fps_matrix\run_precise_20261017\decimals\60_to_66_p2.aup2 --target-fps 66 --precise --precise-extra-decimals 2
  ```
- [ ] 3.1.2 两个命令都应退出码 0。记录两个 `.convert.json` 中 `precise.applied` 与 `precise.skipped` 的值。

### 3.2 文本检查 (不用 AviUtl2)

- [ ] 3.2.1 用记事本 (只读查看, **不要保存**) 打开两个 `.aup2`, 搜索 `直線移動,0`, 记录同一行在两个文件中的数值小数位数。2 位版本应比 0 位版本多 2 位。
  - 注意: 工程文件是 UTF-8 文本, 记事本需选择 UTF-8 编码打开。

### 3.3 在 AviUtl2 中打开并导出

- [ ] 3.3.1 在 AviUtl2 中依次打开 `60_to_66_p0.aup2` 与 `60_to_66_p2.aup2` (文件 > 開く)。**关闭时选择不保存。**
- [ ] 3.3.2 记录 GUI 中直线移动轨迹的数值显示几位小数 (两个版本分别记录)。
- [ ] 3.3.3 两个版本分别用 **普通导出** 导出 (不是 Free FPS Export, 且 **Enable Free Render FPS 为关闭**), 输出到:
  - `experiments\free_fps_matrix\run_precise_20261017\decimals\60_to_66_p0.mp4`
  - `experiments\free_fps_matrix\run_precise_20261017\decimals\60_to_66_p2.mp4`
  输出插件、码率等设置两次保持一致。
- [ ] 3.3.4 用 ffmpeg 生成逐帧哈希并比较 (ffmpeg 需在 PATH 中, 或用完整路径):
  ```powershell
  $D = "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_precise_20261017\decimals"
  ffmpeg -v error -i "$D\60_to_66_p0.mp4" -map 0:v:0 -f framemd5 "$D\p0.framemd5"
  ffmpeg -v error -i "$D\60_to_66_p2.mp4" -map 0:v:0 -f framemd5 "$D\p2.framemd5"
  fc.exe "$D\p0.framemd5" "$D\p2.framemd5"
  ```
  期望: 两文件的帧数都是 330。`fc.exe` 报告的每一段差异对应不同的帧, 请记录这些段的行号。

### 3.4 判定

- [ ] 3.4.1 若 AviUtl2 只按显示位数读取, 或忽略超出的小数, 两个版本应完全相同 (`fc.exe` 无差异)。若有差异, 记录差异帧的编号和 `fc.exe` 的输出。
- [ ] 3.4.2 无论结果如何, 都要记录: GUI 显示的小数位数、两个 `.aup2` 中同一行的文本、`fc.exe` 输出。不要根据这一步改动仓库文件。

---

## 四、30 分钟音画同步

本步骤按 `experiments\long_av_sync\README.zh-CN.md` 执行 (生成音频与工程、三组导出 66 / 50 / 59.94、运行 `analyze_sync.py`)。

- [ ] 4.1 导出时保持 **Precise motion** 勾选 (默认)。README 中没有这一项, 请在回传中写明每组的勾选状态。
- [ ] 4.2 README 的判定 (最大偏移、漂移斜率、时长相对误差、每个闪白与嘀声一一对应) 全部照做, 不要修改阈值。
- [ ] 4.3 若某组失败, 附上对应的 `pairs_*.csv`。

---

## 五、回传内容

请回传以下内容:

1. **构建**: 方式 A 或 B; `x264guiEx-FreeRenderFPS.aux2` 的 SHA-256; 对话框 Precise 复选框的文字与默认状态; 打包脚本的完整输出 (若用方式 A)。
2. **8 组导出**: 每组的 Precise 勾选状态、导出结果 (成功或错误原文)、输出文件名; 任何弹窗的原文。
3. **analyze.py**: `SUMMARY_precise.md` (判据) 的全文 (或至少总体结论一行), 以及 `SUMMARY_original.md`、`SUMMARY_retimed.md` 的总体结论; `summary_precise.json`、`summary_original.json`、`summary_retimed.json`; 三次运行的退出码。
4. **小数位**: 两个 `.aup2` 中 `直線移動,0` 那一行的文本 (两行); GUI 显示的小数位数; `fc.exe` 的输出; 两个 `.convert.json` 中的 `precise.applied` 与 `precise.skipped`。
5. **30 分钟**: `analyze_sync.py` 每组的完整终端输出, 每组导出时的 Precise 勾选状态与输出文件名; 若有失败, 附 `pairs_*.csv`。
6. **异常**: 任何与本计划预期不符的现象, 包括输出帧数不同、导出路径不是副本、或插件对话框没有 Precise 复选框。

---

## 不在本计划范围内

- 不运行打包脚本 (由用户按第一节执行)。
- 不打开或保存仓库原件 (`experiments\free_fps_matrix\*.aup2`、`experiments\66_to_60_validation\*.aup2`)。
- 不把 `--precise-extra-decimals` 的结果当作插件对话框的功能; 对话框只写 0 位。
- 不修改 `tools\aup2_fps_upconvert.py`、C++ 源码或测试。
