# FreeRenderFPS v1.0.0 validation matrix

Date: 2026-08-13  
Host: AviUtl2 v2.1.4 x64  
Final AUO2 version: `1.0.0`  
Final AUO2 SHA-256: `A9A60EEB4A92073B7CBA539433821E7E18DAF69D714A4CA9857ED45B8054B29B`

Frame identity was measured after decoding. Container FPS metadata alone was
not accepted as temporal-sampling evidence.

## Output matrix

| Project → target | Output frames | Video FPS | Video duration | Audio duration | Unique decoded frames | Adjacent equal | Result |
|---|---:|---|---:|---:|---:|---:|---|
| FreeFPS OFF, 66 → 66 | 330 | 66/1 | 5.000000 s | 5.000000 s | — | — | PASS |
| 30 → 60 | 300 | 60/1 | 5.000000 s | 5.000000 s | 300 | 0 | PASS |
| 66 → 60 | 300 | 60/1 | 5.000000 s | 5.000000 s | 300 | 0 | PASS |
| 66 → 59.94 | 300 | 60000/1001 | 5.005000 s | 5.000000 s | 300 | 0 | PASS |
| 66 → 24 | 120 | 24/1 | 5.000000 s | 5.000000 s | 120 | 0 | PASS |
| 66 → 120 | 600 | 120/1 | 5.000000 s | 5.000000 s | 600 | 0 | PASS |
| 120 → 60 | 300 | 60/1 | 5.000000 s | 5.000000 s | 300 | 0 | PASS |

The 60-second 66 → 60000/1001 test produced 3597 frames and 60.008333 s
video against 60.000000 s audio. The +8.333 ms endpoint delta is below one
target-frame interval and follows the documented ceiling boundary rule. It
contained 3590 unique decoded frames and seven adjacent equal images on the
simple linear-motion fixture.

## Final package-only smoke tests

| Path | Package use | Result |
|---|---|---|
| Setup EXE | non-interactive install into a fresh isolated host; one AUO2 loaded | H.264 150 frames at 30/1, AAC 217 frames, both 5.000000 s — PASS |
| Portable ZIP | extracted directly over a fresh isolated AviUtl2 root | `data\Plugin` layout, one AUO2 loaded; same real encode — PASS |

Retained final media:

- `experiments/66_to_60_validation/v100_setup_clean_30_off.mp4`
- `experiments/66_to_60_validation/v100_portable_clean_30_off.mp4`

The full FreeFPS matrix was run on the same release code and audited runtime
before the final version-resource-only rebuild. The final `1.0.0` package then
passed both package-only compatibility encodes. RC1 was not overwritten.

## AviUtl2 v2.1.12 — Free FPS Export（精确运动模式），2026-10-17

Host: AviUtl2 v2.1.12 x64  
Plugin: `x264guiEx-FreeRenderFPS.aux2`, version resource 1.1.0, SHA-256 `114C307016679E852674DF7A974889CACF4E340449DA475578C3A1A259F6076B`, 1,532,928 bytes, from the `dist\AviUtl2-FreeRenderFPS-v1.1.0` package repacked on 2026-10-17 17:52 (the first 1.1.0 build did not contain the precise mode). Exported through Edit > Free FPS Export...  
The tested plugin is the one the user has installed at `C:\ProgramData\aviutl2\Plugin\x264guiEx-FreeRenderFPS\`; its hash matches the dist file above.

### 运动 (8 组)

| 用例 | 目标 FPS | 期望/实际帧 | 运动最大残差 (px) | 零步 | 反向 | 速度误差 | 结论 |
|---|---|---|---:|---:|---:|---:|---|
| 60_to_66 | 66 | 330/330 | 0.1425 | 0 | 0 | 0.0031% | PASS |
| 72_to_60 | 60 | 300/300 | 0.1477 | 0 | 0 | 0.0069% | PASS |
| 30_to_5994 | 60000/1001 | 300/300 | 0.1431 | 0 | 0 | 0.0043% | PASS |
| 66_to_24 | 24 | 120/120 | 0.1351 | 0 | 0 | 0.0017% | PASS |
| 66_to_120 | 120 | 600/600 | 0.2262 | 0 | 0 | 0.0046% | PASS |
| 120_to_60 | 60 | 300/300 | 0.1331 | 0 | 0 | 0.0004% | PASS |
| 60_to_50 | 50 | 250/250 | 0.1247 | 0 | 0 | 0.0032% | PASS |
| 72_to_59 | 59 | 295/295 | 0.1657 | 0 | 0 | 0.0057% | PASS |

速度误差 = |线性拟合斜率 − 原工程斜率| / 原工程斜率。在 `SUMMARY_precise.md` 的 "运动诊断" 中，原工程斜率对应 "模型斜率"，线性拟合斜率对应 "线性拟合斜率"。残差上限 1.0 px，零步阈值 |Δx| < 0.05 px。

同一批视频若用旧的（非精确）`retimed` 运动模型判定，30_to_5994（1.7528 px）和 66_to_24（2.1256 px）FAIL。这与精确导出的预期一致：输出的运动会越过重定时后的终点继续延伸。因此可以确认导出确实走了精确模式。

### 30 分钟音画同步 (1800 s)

| 目标 FPS | 解码/期望帧 | 最大 \|偏移\| | 漂移斜率 | 时长相对误差 | 结论 |
|---|---:|---:|---:|---:|---|
| 66 | 118800/118800 | 0.00 ms | 0.000 ms/min | +0.0000% | PASS |
| 50 | 90000/90000 | 0.00 ms | 0.000 ms/min | +0.0000% | PASS |
| 60000/1001 | 107892/107892 | 8.33 ms | −0.004 ms/min | −0.0001% | PASS |

1800 对闪白/嘀声一一配对。59.94 的 ±8.33 ms 是半个输出帧内的取整结果；均值 +0.01 ms，没有漂移。

### 证据

- `experiments/free_fps_matrix/run_20261017_precise/`: `SUMMARY_precise.md`, `summary_precise.json`, `8组_precise.log`, `SUMMARY_retimed.md`, `SUMMARY_original.md`, `validator/*.validator.json`
- `experiments/long_av_sync/run_20261017/`: `long_66.txt`, `long_50.txt`, `long_5994.txt`

视频文件未入库。
