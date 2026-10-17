# 自由帧率导出清单 (8 个用例)

本清单用于在 AviUtl2 宿主中, 通过 **编辑 > Free FPS Export...** 逐个导出 8 个用例, 然后用 `analyze.py` 检查结果。
用例名、帧数、时长均以 `experiments\free_fps_matrix\cases.json` 为准。

仓库根目录: `D:\AviUtl Free Render FPS`

## 汇总表

| 序号 | 用例 (`<case>`) | 源 fixture (仓库原件) | 目标 FPS (输入字符串) | 期望帧 | 期望时长 (s) | 输出文件 |
|---|---|---|---|---|---|---|
| 1 | `60_to_66` | `fixture_60fps.aup2` | `66` | 330 | 5 | `run_20261017\60_to_66.mp4` |
| 2 | `72_to_60` | `fixture_72fps.aup2` | `60` | 300 | 5 | `run_20261017\72_to_60.mp4` |
| 3 | `30_to_5994` | `66_to_60_validation\freerenderfps_30fps.aup2` | `60000/1001` | 300 | 1001/200 (= 5.005) | `run_20261017\30_to_5994.mp4` |
| 4 | `66_to_24` | `66_to_60_validation\freerenderfps_66fps.aup2` | `24` | 120 | 5 | `run_20261017\66_to_24.mp4` |
| 5 | `66_to_120` | `66_to_60_validation\freerenderfps_66fps.aup2` | `120` | 600 | 5 | `run_20261017\66_to_120.mp4` |
| 6 | `120_to_60` | `66_to_60_validation\freerenderfps_120fps.aup2` | `60` | 300 | 5 | `run_20261017\120_to_60.mp4` |
| 7 | `60_to_50` | `fixture_60fps.aup2` | `50` | 250 | 5 | `run_20261017\60_to_50.mp4` |
| 8 | `72_to_59` | `fixture_72fps.aup2` | `59` | 295 | 5 | `run_20261017\72_to_59.mp4` |

完整路径前缀:
- `fixture_60fps.aup2` = `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\fixture_60fps.aup2`
- `fixture_72fps.aup2` = `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\fixture_72fps.aup2`
- `66_to_60_validation\...` = `D:\AviUtl Free Render FPS\experiments\66_to_60_validation\...`

---

## 一、准备

1. **建立运行目录和副本目录。** 运行目录为 `experiments\free_fps_matrix\run_<日期>\`。
   日期取运行当天, 格式为 `YYYYMMDD`, 今天即 `run_20261017`。同一天重跑时改为 `run_20261017_2`, 以免覆盖上一轮结果。
   运行目录只放 MP4、副本和 analyze.py 的输出, 不要放入仓库原件。
   在 PowerShell 中执行 (在仓库根目录外也可以, 路径是绝对路径):

   ```powershell
   New-Item -ItemType Directory -Force -Path "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects"
   ```

   使用 `projects\` 子目录存放副本。`analyze.py` 只读取运行目录下的 `<case>.mp4`, 子目录不影响检查。

2. **安装。** 按 `experiments\free_fps_export\INSTALL.zh-CN.md` 安装 `free_fps_export.aux2` 和 `x264guiEx FreeRenderFPS` 输出插件。
   (注意: 该文件目前尚未生成, 安装前需要先确认它已存在。)
   分析步骤还需要 Python 3、numpy, 以及 ffmpeg 和 ffprobe (在 PATH 中, 或用 `--ffmpeg` / `--ffprobe` 指定)。

3. **宿主内预检。** `fixture_60fps.aup2` 与 `fixture_72fps.aup2` 尚未在 AviUtl2 中加载过 (见 `fixtures.md`)。
   正式导出前先打开第 1 个副本 (`60_to_66`), 确认:
   - 项目帧率为 `60`, 时间线最后一帧为第 299 帧 (共 300 帧);
   - 圆形对象的 X 轨迹为直线移动, 起点 -200, 终点 200, 终点位于第 299 帧。

   如果不符, 停止, 不要继续导出, 报告给助手。

---

## 二、每个用例的导出步骤

每个用例都使用一份**新的副本**。原因见下一节 "三、注意事项"。

通用步骤 (每个用例都要做):

a. 复制源 fixture 到 `projects\<case>.aup2` (见下面的复制命令)。
b. 在 AviUtl2 中打开该副本 (不要打开仓库原件)。
c. 菜单 **编辑 > Free FPS Export...**。
d. **目标 FPS**: 原样输入表中的字符串 (不要输入 `59.94`, 插件会拒绝小数)。
e. **输出文件**: 点击 Browse 或直接输入表中的完整路径, 文件名必须是 `<case>.mp4`。
f. **输出插件**: 保持默认 `x264guiEx FreeRenderFPS`。
g. 点击确定。保存提示出现时选 **是** (只会保存这份副本, 不会改动仓库原件)。
h. 等待完成。确认结果提示成功, 并且输出文件存在且非空。
i. 确认 AviUtl2 已重新打开这份副本 (插件会在结束时重新打开原项目)。

**复制命令 (PowerShell, 每个用例执行一次):**

```powershell
Copy-Item -LiteralPath "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\fixture_60fps.aup2" -Destination "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\60_to_66.aup2"
Copy-Item -LiteralPath "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\fixture_72fps.aup2" -Destination "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\72_to_60.aup2"
Copy-Item -LiteralPath "D:\AviUtl Free Render FPS\experiments\66_to_60_validation\freerenderfps_30fps.aup2" -Destination "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\30_to_5994.aup2"
Copy-Item -LiteralPath "D:\AviUtl Free Render FPS\experiments\66_to_60_validation\freerenderfps_66fps.aup2" -Destination "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\66_to_24.aup2"
Copy-Item -LiteralPath "D:\AviUtl Free Render FPS\experiments\66_to_60_validation\freerenderfps_66fps.aup2" -Destination "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\66_to_120.aup2"
Copy-Item -LiteralPath "D:\AviUtl Free Render FPS\experiments\66_to_60_validation\freerenderfps_120fps.aup2" -Destination "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\120_to_60.aup2"
Copy-Item -LiteralPath "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\fixture_60fps.aup2" -Destination "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\60_to_50.aup2"
Copy-Item -LiteralPath "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\fixture_72fps.aup2" -Destination "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\72_to_59.aup2"
```

### 用例 1: `60_to_66`
- 副本: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\60_to_66.aup2`
- 目标 FPS 输入: `66`
- 输出文件: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\60_to_66.mp4`
- 期望: 330 帧, 时长 5 s (330 / 66)
- [ ] 导出成功, `60_to_66.mp4` 存在且非空, 项目已重新打开为副本

### 用例 2: `72_to_60`
- 副本: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\72_to_60.aup2`
- 目标 FPS 输入: `60`
- 输出文件: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\72_to_60.mp4`
- 期望: 300 帧, 时长 5 s (300 / 60)
- [ ] 导出成功, `72_to_60.mp4` 存在且非空, 项目已重新打开为副本

### 用例 3: `30_to_5994`
- 副本: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\30_to_5994.aup2`
- 目标 FPS 输入: `60000/1001` (精确分数, 不要输入 `59.94`)
- 输出文件: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\30_to_5994.mp4`
- 期望: 300 帧, 时长 1001/200 s (= 5.005 s; 150 × (60000/1001) / 30 = 299.70, 向上取整为 300)
- [ ] 导出成功, `30_to_5994.mp4` 存在且非空, 项目已重新打开为副本

### 用例 4: `66_to_24`
- 副本: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\66_to_24.aup2`
- 目标 FPS 输入: `24`
- 输出文件: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\66_to_24.mp4`
- 期望: 120 帧, 时长 5 s (120 / 24)
- [ ] 导出成功, `66_to_24.mp4` 存在且非空, 项目已重新打开为副本

### 用例 5: `66_to_120`
- 副本: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\66_to_120.aup2`
- 目标 FPS 输入: `120`
- 输出文件: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\66_to_120.mp4`
- 期望: 600 帧, 时长 5 s (600 / 120)
- [ ] 导出成功, `66_to_120.mp4` 存在且非空, 项目已重新打开为副本

### 用例 6: `120_to_60`
- 副本: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\120_to_60.aup2`
- 目标 FPS 输入: `60`
- 输出文件: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\120_to_60.mp4`
- 期望: 300 帧, 时长 5 s (300 / 60)
- [ ] 导出成功, `120_to_60.mp4` 存在且非空, 项目已重新打开为副本

### 用例 7: `60_to_50`
- 副本: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\60_to_50.aup2`
- 目标 FPS 输入: `50`
- 输出文件: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\60_to_50.mp4`
- 期望: 250 帧, 时长 5 s (250 / 50)
- [ ] 导出成功, `60_to_50.mp4` 存在且非空, 项目已重新打开为副本

### 用例 8: `72_to_59`
- 副本: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\projects\72_to_59.aup2`
- 目标 FPS 输入: `59`
- 输出文件: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\72_to_59.mp4`
- 期望: 295 帧, 时长 5 s (295 / 59)
- [ ] 导出成功, `72_to_59.mp4` 存在且非空, 项目已重新打开为副本

---

## 三、注意事项

1. **每个用例都打开新副本, 不要打开仓库原件。** 插件在确认后会先执行 `save_project_file`, 把当前打开的项目保存回它自己的路径 (见 `experiments\free_fps_export\README.md` 第 3-4 步)。如果直接打开 `experiments\free_fps_matrix\*.aup2` 或 `experiments\66_to_60_validation\*.aup2`, 保存会改写仓库中的 fixture 文件。因此:
   - 只打开 `projects\` 下的副本;
   - 同一个源 fixture 被多个用例使用时 (例如 `fixture_60fps` 用于 `60_to_66` 与 `60_to_50`), 每个用例都重新复制一份, 不要复用上一次的副本。
   分析脚本 `analyze.py` 读取的是仓库中的原件 (由 `cases.json` 的 `source_fixture` 指定), 副本被宿主保存后的内容不影响期望值的计算。

2. **输出路径以对话框为准。** 副本和原件中的 `output.file=` 指向 `experiments\free_fps_matrix\out\...` (该目录目前不存在) 或 `66_to_60_validation\` 中已有的 MP4 (例如 `rc2a_clean_30_to_60.mp4`)。本清单不使用这些路径。
   插件调用 `output_file` 时使用的是对话框中填写的输出文件 (见 `README.md` 第 6 步)。
   **如果导出出现的文件不在运行目录中, 或者 `66_to_60_validation\` 中的已有文件被修改, 立即停止并报告。**
   不需要手动创建 `out\` 目录。

3. **FreeRenderFPS 开关必须关闭。** 使用 `x264guiEx FreeRenderFPS` 作为输出插件时, 打开 AviUtl2 的 **Free Render FPS** 标签, 确认 **Enable Free Render FPS** 为关闭 (参见 `dist\AviUtl2-FreeRenderFPS-v1.1.0\payload\Docs\README.en.md` 第 93 行)。
   如果开关为开, 两条路径都会处理帧率, 结果不可用于判定。每次导出前都检查一次。

4. **不要使用 `dry_run_simulated\`。** 该目录的 `SUMMARY.md` 标明为模拟数据 (FAKE/SIMULATED), 不是宿主导出。

5. **不要修改 `cases.json`、`analyze.py`、fixture 原件。** 发现问题时停止并报告。

---

## 四、全部导出完成后: 运行分析

在仓库根目录 `D:\AviUtl Free Render FPS` 执行:

```powershell
python -I experiments\free_fps_matrix\analyze.py "D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017"
```

- `-I` 是隔离模式, 不加载脚本目录和当前目录中的模块, 不要去掉。
- 如果 ffmpeg / ffprobe 不在 PATH 中, 追加 `--ffmpeg "<ffmpeg.exe 路径>" --ffprobe "<ffprobe.exe 路径>"`。
- 只重跑部分用例时, 追加 `--only 60_to_66,72_to_60` 之类的参数 (用例 id 用英文逗号分隔)。

退出码: `0` 全部通过, `1` 有用例失败或缺少 MP4, `2` 输入或环境错误 (错误信息打印到 stderr)。

**结果位置:**
- `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\SUMMARY.md` (中文汇总, 主要结果)
- `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\summary.json`
- `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\run_20261017\analysis\` (每个用例的质心 CSV 和 validator JSON)

**完成后, 请告诉助手:** 运行目录路径、analyze.py 的退出码、以及 `SUMMARY.md` 中的 "总体结论" 行。如有任何用例为 FAIL 或 MISSING, 把对应的 "细节" 段一并提供。
