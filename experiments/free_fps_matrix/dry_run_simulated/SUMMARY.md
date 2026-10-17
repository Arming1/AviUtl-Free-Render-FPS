# 自由帧率导出验证汇总

- 标签: **FAKE/SIMULATED (existing samples, not host exports)**
- 运行目录: `D:\AviUtl Free Render FPS\experiments\free_fps_matrix\dry_run_simulated`
- 运动时间模型 (`--timeline original`): **原工程帧范围 (插件 Hook 路径样本, 未重定时)**
- 总体结论: **PASS**

判定标准:

- validator: `tools/validate_freefps_output.py`, `--project-fps` = `--target-fps` = 目标帧率, `--project-frames` = 期望输出帧数。
- 运动检查: 每帧用 luma 与中位数之差 > 30 的像素求质心 x。模型 x(t) = W/2 + X_start + (X_end - X_start)·min(max(t - t_s, 0), T)/T, t = n / 目标帧率; 起点 t_s = 0, T = (末帧 - 首帧)/源帧率。残差上限 1.0 px; 相邻帧 |Δx| < 0.05 px 记为零步; 要求零反向、零零步、无缺失质心。

| 用例 | 源 fixture | 目标 FPS | 期望帧 | 期望时长 (s) | 实际帧 | validator | 运动最大残差 (px) | 零步 | 反向 | 结论 |
|---|---|---|---|---|---|---|---|---|---|---|
| 66_to_24 | `freerenderfps_66fps.aup2` | 24 | 120 | 5.0000 | 120 | PASS | 0.1428 | 0 | 0 | **PASS** |
| 66_to_120 | `freerenderfps_66fps.aup2` | 120 | 600 | 5.0000 | 600 | PASS | 0.1703 | 0 | 0 | **PASS** |
| 120_to_60 | `freerenderfps_120fps.aup2` | 60 | 300 | 5.0000 | 300 | PASS | 0.1766 | 0 | 0 | **PASS** |

## 终点时间偏差 (诊断)

已知近似：直线移动终点对齐到重定时后的最后一帧，终点时间偏差 ≤ |1/源帧率 − 1/目标帧率| + 半个输出帧。

偏差 = T' − T: T 为原工程时间 (末帧 − 首帧)/源帧率, T' 为重定时后 (e' − s')/目标帧率; 正值表示重定时后运动更长。理论上限 = |1/Rp − 1/Rt| + 1/(2·Rt) (Rp 源帧率, Rt 目标帧率; 起点为 0 时起点项为 0)。这是诊断项, 不影响 PASS 判定。原工程时间模型残差按 `original` 模型单独计算, 不参与判定。

| 用例 | k | 重定时帧 s'..e' | T 原工程 (s) | T' 重定时 (s) | 偏差 T'−T (s) | 偏差 (输出帧) | 理论上限 (s) | 理论上限 (输出帧) | 在上限内 | 原工程时间模型最大残差 (px) |
|---|---|---|---|---|---|---|---|---|---|---|
| 66_to_24 | 4/11 | 0..119 | 4.9848 | 4.9583 | -0.0265 | -0.64 | 0.04735 | 1.14 | 是 | 0.1428 |
| 66_to_120 | 20/11 | 0..599 | 4.9848 | 4.9917 | +0.0068 | +0.82 | 0.01098 | 1.32 | 是 | 0.1703 |
| 120_to_60 | 1/2 | 0..299 | 4.9917 | 4.9833 | -0.0083 | -0.50 | 0.01667 | 1.00 | 是 | 0.1766 |

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

- 运动模型假设 AviUtl 的直线移动在 [起点, 起点 + T] 之间线性插值, 末值保持到之后。`retimed` 模式下起点与 T' 由重定时后的帧范围 s', e' 推出 (规则与 tools/aup2_fps_upconvert.py 相同), 不是名义 5 s。这一点需要宿主实测确认。
- `original` 模式用原工程 frame= 行推出的 T = (末帧 - 首帧)/源帧率。它只适用于插件 Hook 路径样本 (dry_run_simulated), 这些样本没有经过重定时导出。
- x0/x1 取 W/2 + X 值 (AviUtl 画面中心约定), 通过残差与拟合截距检验。
- 结论只覆盖 luma 像素导出的几何位置。不证明颜色、音频或编码器正确性。
