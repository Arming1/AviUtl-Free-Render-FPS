# 自由帧率导出验证汇总

- 标签: **FAKE/SIMULATED (existing samples, not host exports)**
- 运行目录: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\dry_run_simulated`
- 总体结论: **PASS**

判定标准:

- validator: `tools/validate_freefps_output.py`, `--project-fps` = `--target-fps` = 目标帧率, `--project-frames` = 期望输出帧数。
- 运动检查: 每帧用 luma 与中位数之差 > 30 的像素求质心 x。模型 x(t) = W/2 + X_start + (X_end - X_start)·min(t, T)/T, t = n / 目标帧率, T = (末帧 - 首帧) / 源帧率。残差上限 1.0 px; 相邻帧 |Δx| < 0.05 px 记为零步; 要求零反向、零零步、无缺失质心。

| 用例 | 源 fixture | 目标 FPS | 期望帧 | 期望时长 (s) | 实际帧 | validator | 运动最大残差 (px) | 零步 | 反向 | 结论 |
|---|---|---|---|---|---|---|---|---|---|---|
| 66_to_24 | `freerenderfps_66fps.aup2` | 24 | 120 | 5.0000 | 120 | PASS | 0.1428 | 0 | 0 | **PASS** |
| 66_to_120 | `freerenderfps_66fps.aup2` | 120 | 600 | 5.0000 | 600 | PASS | 0.1703 | 0 | 0 | **PASS** |
| 120_to_60 | `freerenderfps_120fps.aup2` | 60 | 300 | 5.0000 | 300 | PASS | 0.1766 | 0 | 0 | **PASS** |

## 细节

### 66_to_24

- validator 退出码 0, overall = PASS
  - N/A `no_pairing`: target/project ratio 1/1 is not an integer >= 2
- 运动: 状态 PASS, 帧 120, 运动段内帧 120, 无对象帧 0, 最大残差 0.1428 px, 零步 0, 反向 0
- 运动诊断: 模型斜率 80.2432 px/s, 线性拟合斜率 80.2451 px/s, 截距 120.0004 px, 拟合最大残差 0.1329 px, 纵向最大偏差 0.0725 px

### 66_to_120

- validator 退出码 0, overall = PASS
  - N/A `no_pairing`: target/project ratio 1/1 is not an integer >= 2
- 运动: 状态 PASS, 帧 600, 运动段内帧 599, 无对象帧 0, 最大残差 0.1703 px, 零步 0, 反向 0
- 运动诊断: 模型斜率 80.2432 px/s, 线性拟合斜率 80.2445 px/s, 截距 119.9950 px, 拟合最大残差 0.1662 px, 纵向最大偏差 0.0981 px

### 120_to_60

- validator 退出码 0, overall = PASS
  - N/A `no_pairing`: target/project ratio 1/1 is not an integer >= 2
- 运动: 状态 PASS, 帧 300, 运动段内帧 300, 无对象帧 0, 最大残差 0.1766 px, 零步 0, 反向 0
- 运动诊断: 模型斜率 80.1336 px/s, 线性拟合斜率 80.1357 px/s, 截距 119.9958 px, 拟合最大残差 0.1729 px, 纵向最大偏差 0.1054 px

## 假设与限制

- 运动模型假设 AviUtl 的直线移动在 [首帧, 末帧] 之间线性插值, 末值保持到之后。T 由 fixture 的 frame 行推出, 不是名义 5 s (例如 frame=0,299 时 T = 299/60 s)。这一点需要宿主实测确认。
- x0/x1 取 W/2 + X 值 (AviUtl 画面中心约定), 通过残差与拟合截距检验。
- 结论只覆盖 luma 像素导出的几何位置。不证明颜色、音频或编码器正确性。
