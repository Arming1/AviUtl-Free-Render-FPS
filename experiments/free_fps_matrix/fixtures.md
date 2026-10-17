# 自由帧率验证用 fixture 说明

**警告: 本目录下的 `fixture_60fps.aup2` 与 `fixture_72fps.aup2` 尚未在 AviUtl2 宿主中加载过。它们只是按现有文本推导的副本, 加载与导出结果均未验证。**

## 已有 fixture 的实测值

| 文件 | video.rate | frame 行 | 帧数 | X 轨迹 (trackbar) | 分辨率 |
|---|---|---|---|---|---|
| `66_to_60_validation/freerenderfps_30fps.aup2` | 30 | `0,149` | 150 (5 s) | `-200.00,200.00,直線移動,0` | 640x360 |
| `66_to_60_validation/freerenderfps_66fps.aup2` | 66 | `0,329` | 330 (5 s) | 同上 | 640x360 |
| `66_to_60_validation/freerenderfps_120fps.aup2` | 120 | `0,599` | 600 (5 s) | 同上 | 640x360 |

三个文件的对象 (図形: 圆, 大小 48, 白色) 与轨迹完全相同, 只有 `video.rate`、`frame=`、`name=`、`file=`、`output.file=` 不同。

## 新 fixture 与 66 fixture 的差异

以 `66_to_60_validation/freerenderfps_66fps.aup2` 为基础, 只改两行:

**`fixture_60fps.aup2`** (`diff` 输出):

```
3c3
< file=D:\AviUtl Free Render FPS\experiments\66_to_60_validation\freerenderfps_66fps.aup2
---
> file=D:\AviUtl Free Render FPS\experiments\free_fps_matrix\fixture_60fps.aup2
11c11
< video.rate=66
---
> video.rate=60
14c14
< output.file=D:\AviUtl Free Render FPS\experiments\66_to_60_validation\freerenderfps_66_to_60.mp4
---
> output.file=D:\AviUtl Free Render FPS\experiments\free_fps_matrix\out\fixture_60fps.mp4
36c36
< frame=0,329
---
> frame=0,299
```

**`fixture_72fps.aup2`** (`diff` 输出):

```
3c3
< file=D:\AviUtl Free Render FPS\experiments\66_to_60_validation\freerenderfps_66fps.aup2
---
> file=D:\AviUtl Free Render FPS\experiments\free_fps_matrix\fixture_72fps.aup2
11c11
< video.rate=66
---
> video.rate=72
14c14
< output.file=D:\AviUtl Free Render FPS\experiments\66_to_60_validation\freerenderfps_66_to_60.mp4
---
> output.file=D:\AviUtl Free Render FPS\experiments\free_fps_matrix\out\fixture_72fps.mp4
36c36
< frame=0,329
---
> frame=0,359
```

除上述 4 行外, 其余行 (包括 `name=`) 与 66 fixture 逐字相同。

## 需要注意的几点

1. **导出路径已改为专用目录, 不会覆盖已有样本。** `output.file=` 指向 `experiments\free_fps_matrix\out\fixture_60fps.mp4` 与 `...\out\fixture_72fps.mp4`。`out\` 目录目前不存在, 没有创建。宿主导出前需要先建目录。`file=` 已改为各自 fixture 的绝对路径。
2. **`name=` 仍是 66 的值。** 只是元数据, 不影响渲染, 尚未修改。
3. **"5 秒" 的含义。** 按指令 frame 行取 `0,299` 与 `0,359`。时间线长度是 300 / 360 帧, 即 5 s。但 AviUtl 的直線移動在 [首帧, 末帧] 之间插值, 因此 X 轨迹在第 299 帧 (60 fps 时 299/60 s ≈ 4.983 s) 到达 200, 第 359 帧 (72 fps 时 ≈ 4.986 s) 到达 200。`analyze.py` 的运动模型按这个约定计算 T。这一约定目前只由 dry run 数据间接支持, 需要宿主实测确认。
4. 两个新文件都保留了 UTF-8 无 BOM 与 LF 行尾, 和 66 fixture 相同 (已用 `grep -c $'\r'` 确认无 CR)。

## 对应用例

- `fixture_60fps.aup2`: 用例 `60_to_66` 与 `60_to_50` 的源。
- `fixture_72fps.aup2`: 用例 `72_to_60` 与 `72_to_59` 的源。
- `30_to_5994` 用 `30fps` 原始 fixture, `66_to_24` 与 `66_to_120` 用 `66fps`, `120_to_60` 用 `120fps`。

具体帧数公式与期望值见 `cases.json`。
