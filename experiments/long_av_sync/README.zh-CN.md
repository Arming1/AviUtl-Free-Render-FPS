# 30 分钟长视频音画同步测试

目的：在 AviUtl2 v2.1.12 中用「编辑 > Free FPS Export...」把 60 FPS 工程导出为 66、50、59.94 FPS 的 30 分钟视频，然后用分析脚本测量每次闪白与嘀声之间的音画偏移，检查偏移是否稳定、时长是否一致。

## 文件

| 文件 | 说明 | 是否被 .gitignore 忽略 |
| --- | --- | --- |
| `make_beeps.ps1` | 用 ffmpeg 生成 `beeps_30min.wav` 的脚本 | 否（会提交） |
| `beeps_30min.wav` | 30 分钟、48 kHz、单声道、16 bit 嘀声音频（约 173 MB） | 是（`*.wav`），需本地生成 |
| `make_fixture.py` | 生成测试工程 `long_sync_60fps.aup2` | 否（会提交） |
| `long_sync_60fps.aup2` | 60 FPS、30 分钟（108000 帧）测试工程 | 否（会提交） |
| `analyze_sync.py` | 分析导出的 MP4，输出音画偏移与判定；`--self-test` 做自测 | 否（会提交） |
| `out/` | 导出目录与自测文件 | 是（`**/out/`） |

工程的 `output.file` 指向 `experiments\long_av_sync\out\long_sync_60fps.mp4`。
`make_fixture.py` 会检查这一点，如果路径不在 `out\` 下会拒绝生成。

## 时间轴

- 图层 1：音声再生对象，播放 `beeps_30min.wav`，帧 0 到 107999。
- 图层 2：每秒一个白色全屏背景（图形 > 背景），持续 2 帧，起点为帧 `s*60`（s = 0 到 1799）。
- 嘀声在每个整秒开始（20 ms, 1 kHz），与闪白同时发生。

## 操作步骤

### 0. 准备（只需一次）

1. 安装 ffmpeg，并确保 `ffmpeg` 与 `ffprobe` 在 PATH 中，或记下它们的完整路径。
2. 生成音频（约 12 秒，文件约 173 MB，已被忽略）：

   ```powershell
   cd "D:\AviUtl Free Render FPS\experiments\long_av_sync"
   powershell -NoProfile -ExecutionPolicy Bypass -File .\make_beeps.ps1
   ```

   如果 ffmpeg 不在 PATH 中，加上 `-FFmpeg "C:\...\ffmpeg.exe"`。
3. 生成工程：

   ```powershell
   python -I .\make_fixture.py
   ```

   工程里保存的是本仓库的绝对路径。如果仓库目录被移动，需要重新运行这一步。

### 1. 复制并打开工程

1. 把 `long_sync_60fps.aup2` 复制到 AviUtl2 的运行目录（用于运行 AviUtl2 的目录，不要直接在仓库里打开）。
2. 启动 AviUtl2 v2.1.12，用「文件 > 開く」打开复制后的 `long_sync_60fps.aup2`。
3. 确认时间轴为 60 FPS，长度 108000 帧（30 分钟）。音频与闪白在时间轴上应对齐。

### 2. 三组导出（每组约 30 分钟）

依次在「编辑 > Free FPS Export...」中选择目标帧率并导出，输出文件名按下表命名，建议放在 `experiments\long_av_sync\out\` 下：

| 组 | 目标帧率 | 输出文件名 |
| --- | --- | --- |
| 1 | 66 | `long_66.mp4` |
| 2 | 50 | `long_50.mp4` |
| 3 | 59.94（60000/1001） | `long_5994.mp4` |

每组导出后，记下导出对话框里实际使用的帧率与文件名。

### 3. 分析

在仓库根目录（`D:\AviUtl Free Render FPS`）运行：

```powershell
python -I experiments\long_av_sync\analyze_sync.py "experiments\long_av_sync\out\long_66.mp4" --target-fps 66 --pairs-csv "experiments\long_av_sync\out\pairs_66.csv"
python -I experiments\long_av_sync\analyze_sync.py "experiments\long_av_sync\out\long_50.mp4" --target-fps 50 --pairs-csv "experiments\long_av_sync\out\pairs_50.csv"
python -I experiments\long_av_sync\analyze_sync.py "experiments\long_av_sync\out\long_5994.mp4" --target-fps 60000/1001 --pairs-csv "experiments\long_av_sync\out\pairs_5994.csv"
```

如果 ffmpeg 不在 PATH 中，加上 `--ffmpeg "...\ffmpeg.exe" --ffprobe "...\ffprobe.exe"`。

脚本输出的内容：

- 偏移的最大值、最小值、最大绝对值、均值（正值表示画面晚于声音）；
- 首 1 分钟与末 1 分钟的平均偏移，用于检测漂移；
- 线性回归斜率（ms/min）；
- 视频与音频的时长差与相对误差；
- 四项判定：
  1. 最大 |偏移| ≤ 半个目标帧 + 编码容差（默认 20 ms），例如 66 FPS 时为 7.6 + 20 = 27.6 ms；
  2. |斜率| ≤ 1.0 ms/min（30 分钟内约 30 ms）；
  3. |(V − A) / A| ≤ 0.01 %；
  4. 每个闪白与每个嘀声一一对应。

退出码：0 表示全部通过，1 表示有判定失败，2 表示输入或工具错误。

### 4. 发回结果

请把以下内容发回：

1. 每组分析脚本的完整终端输出（中文报告部分）；
2. 每组导出时的帧率与输出文件名；
3. 如有失败，附上 `pairs_*.csv`（视频与音频都不必发送）。

### 自测（可选，验证脚本本身）

```powershell
python -I .\analyze_sync.py --self-test
```

脚本会生成 60 秒、100 FPS 的合成 MP4，其闪白比嘀声晚 10 ms，然后检查测得的均值是否在 10 ± 2 ms 之内。
当前结果：测得 +10.00 ms，PASS。

## 未在 AviUtl2 中验证的假设

以下内容没有在 AviUtl2 v2.1.12 中验证，都需要用户在第一次运行时确认：

1. **音声再生对象的写法。** 仓库和样本中没有现成的音频文件对象。`make_fixture.py` 写入的是 `effect.name=音声再生`、`ファイル=<绝对路径>`、`音量=100.00`、`左右=0.00`。其中 `音量` 与 `左右` 参照了 `D:\ARZv2.0.1\ARZv2.0.1\sources\試験用動画.aup2` 中的 映像再生 对象；该样本里**没有**文件路径键，所以 `ファイル=` 这个键本身是猜测。如果宿主不认识该键，导出结果会没有声音，分析脚本会报 `no beep found`。
2. **背景图形的写法。** 闪白使用 `図形の種類=背景`（与样本中的 背景 对象一致），`サイズ=100` 等参数照抄样本，是否铺满全屏未在宿主中确认。
3. **工程音频采样率。** 工程写为 `audio.rate=48000`，与音频文件一致，以避免重采样误差。这是本测试的选择，不是宿主要求。
4. **未包含输出插件配置。** 工程里没有 `[plugin.0]` 块，导出设置完全由「编辑 > Free FPS Export...」对话框决定。
5. **分析方法。** 闪白的时间戳取自帧的 pts，嘀声的时间戳取自 1 ms 块的起点，分辨率为 1 ms。判定阈值（编码容差 20 ms、漂移 1.0 ms/min）是本脚本的默认值，并非来自 AviUtl2 或 x264guiEx 的规格。
