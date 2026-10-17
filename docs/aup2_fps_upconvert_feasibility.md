# .aup2 FPS up-convert tool: feasibility study

Date: 2026-10-17. Scope: the public plain-text `.aup2` project format only. No AviUtl executable or DLL was
analysed; AviUtl2 was not run. Nothing from third-party projects is copied here; only short field shapes are quoted.

## 0. Summary (中文)

- 结论: **部分可行 (partly feasible)**。把 `.aup2` 复制一份、把场景帧率乘 k、把所有"以帧为单位"的字段乘 k，
  对"纯关键帧/直线移动/普通滤镜"的工程是可行且可校验的；它得到的是 AviUtl2 自己在每个输出帧重新求值的真 60 FPS。
- 不能安全处理的东西必须拒绝或警告: 脚本文本里的帧号、按帧步进的效果（随机/噪点/粒子/计数器）、未知插件的帧字段、
  `|` 后面含义未公开的参数。
- 问题 5: AviUtl2 的"场景设置"确实能改帧率（官方更新履历 + 公开 SDK 的 `set_scene_frame_rate`），但**没有任何公开资料说明它会重排
  对象位置**。我无法运行 AviUtl2 所以不能证实。没有证据表明它会换算，因此这个工具**暂时不能判定为多余**；
  请用户用 2 分钟手动实验确认（见 §7）。
- 最大风险: (a) 末端点取整造成最多 1 个原帧的时间偏差，(b) 逐帧步进的效果在 60 FPS 下"看起来不同"，
  (c) 工程里有未知字段/插件字段无法判断是帧还是秒，(d) 写出的文件 AviUtl2 加载失败而我们无法在本机验证。

## 1. Where the frame rate lives

Per scene, not per project. `[project]` has only `version`, `file`, `display.scene`, `preview.scene`
(`experiments/66_to_60_validation/freerenderfps_30fps.aup2:1-5`). Each `[scene.N]` has its own
`video.rate` / `video.scale` (rational rate/scale) and `audio.rate` (sample rate)
(`freerenderfps_30fps.aup2:11-13`; the real project has four scenes, each with rate 60/scale 1 at
`試験用動画.aup2:11-12, 97-98, 3465-3466, 4254-4255`). Project `version=2010400` in the fixtures (`:2`),
`version=2003200` in the older third-party file (`試験用動画.aup2:2`), so 2.1.12 reads older files.

Fixtures differ only in rate, `name`, `output.file` and the single `frame=` end value:
30 FPS `frame=0,149` (`freerenderfps_30fps.aup2:36`), 66 FPS `frame=0,329` (`freerenderfps_66fps.aup2:36`),
120 FPS `frame=0,599` (`freerenderfps_120fps.aup2:36`), i.e. 5 s each with end = rate*5-1. This confirms the
convention end = (frames) - 1, inclusive, and that the fixture generator did not write a "length" field: the
scene length is implied by the last object end (the SDK exposes it as `frame_max`, `plugin2.h:147`).

Per-scene fields tied to the rate:

| Key | Example | Unit | Rule |
|---|---|---|---|
| `video.rate` | `30` (`freerenderfps_30fps.aup2:11`) | numerator of rate/scale | `rate' = rate*k`, keep `scale` (30000/1001 -> 60000/1001 exact) |
| `video.scale` | `1` (`:12`) | denominator | unchanged |
| `audio.rate` | `44100` (`:13`) | Hz | unchanged (time based) |
| `cursor.frame` | `400` (`試験用動画.aup2:15`) | frame | `*k` |
| `preview.frame` | `0` (`:17`) | frame | `*k` |
| `display.frame` | `0` (`:18`) | frame (timeline view start) | `*k` (UI only) |
| `display.zoom` | `7500` (`:20`-ish) | UI scale | unchanged (units unverified, UI only) |
| `display.tempo.bpm/beat/start/offset` | `165.000000`, `3`, `0.000000` (`:29-31`) | BPM = per minute; start/offset presumably seconds | unchanged (time based; grid only; units of `start`/`offset` not verified) |
| range selection / markers | not present in any file I have | frames (SDK `get_mark_frame_list`, `plugin2.h:658`; changelog beta49 says range selection is saved in the project) | key names unknown -> refuse or warn on any unknown scene key containing `frame`/`mark`/`range` |

Scenes are referenced by index: object blocks carry `scene=N` (e.g. 92 objects with `scene=1`,
`試験用動画.aup2`), and the `シーン` effect has `シーン=1` (`試験用動画.aup2:67`-ish; also `output.aup2:972594`).

## 2. Frames vs seconds: field table

Observed encodings (fixtures + a read-only look at third-party projects in `D:\ARZv2.0.1`; the larger one,
~1M lines, had 170 819 two-element `frame=` lines, 46 three-element, 25 five-element).

| Field | Encoding (evidence) | Unit | Rescale rule | Safe? |
|---|---|---|---|---|
| Object `frame=s,e` | `frame=0,149` (`freerenderfps_30fps.aup2:36`), `frame=0,599` (`試験用動画.aup2:34`) | frames, inclusive | `s' = s*k`, `e' = (e+1)*k - 1` | yes |
| Object midpoints | `frame=5952,6240,6264,6288,6335` (`output.aup2:384712`, third-party): first = start, last = end, middle = **absolute timeline frames** | frames | each middle `m' = m*k` (start `s*k`, end `(e+1)*k-1`) | yes (see rounding) |
| Moving trackbar, no midpoints | `X=-200.00,200.00,直線移動,0` (`freerenderfps_30fps.aup2:47`) | value,value,mode,param | values unchanged | yes |
| Trackbar with midpoints | `拡大率=100.000,60.000,70.000,80.000,100.000,イージング（通常）@イージング,0\|27` (`output.aup2:384722`): value count == frame-list count (checked: 50 387 of 50 387 two-point and 27 of 32 multi-point cases; the 5 exceptions are 2-value non-keyframe items such as `再生範囲`, below) | value per point | values unchanged; the point count stays equal to the frame-list count | yes |
| Static trackbar | `Y=0.00` | value | unchanged | yes |
| Move mode names | `直線移動`, `直線移動(時間制御)`, `補間移動(時間制御)`, `イージング（通常）@イージング` (script based, third-party), mode numbers + accel/decel bits per community note | -- | mode name and its integer param copied verbatim | see below |
| `|` suffix after the mode param | `0\|27`, `0\|0,0,1,0` (third-party) | unknown | **meaning not documented**; copy verbatim and refuse if mode is not on an allow-list | no (unverified) |
| 時間制御 (time control) | `位置=0.000,100.000,直線移動(時間制御),0` in `時間制御(オブジェクト)` with `繰り返し`, `コマ落ち`, `対象レイヤー数` (`output.aup2:247905-247909`, third-party) | 0..100 looks like percent of the controlled objects' span (unverified) | values unchanged only if they are normalised; unit not verified | partial |
| `シーン` effect `再生位置=0.000,10.000,再生範囲,0` | `試験用動画.aup2:64` (and `output.aup2:972592`) | **seconds** (3 decimals; community note: playback position moved from frames to seconds in AviUtl2, https://scrapbox.io/Garech/ - unverified personal memo) | unchanged | yes |
| `動画ファイル` / `映像再生` `再生位置` | `再生位置=0.000,5.818,再生範囲,0` (third-party) | seconds | unchanged | yes |
| `再生速度` | `100.00` | percent | unchanged | yes |
| `シーン=N`, `ループ再生` | index / flag | -- | unchanged | yes |
| `object.layer`, `group=`, `focus=` | integers | -- | unchanged | yes |
| `効果 enable/disable` (`effect.disable=1`) | flag | -- | unchanged | yes |
| `カウンター` `速度=1.0`, `初期値=0.0` | `試験用動画.aup2:3859-3860` | probably per-second increment (**unverified**) | cannot decide | warn |
| `振動` `周期=1`, `ランダムに強さを変える` | `output.aup2` third-party | likely time based; random per frame (unverified) | cannot decide | warn |
| Script-text fields (`obj.frame`, `obj.time`, `rand`, Lua in script objects/effects) | none present in the two files I scanned (`grep obj\.(frame\|time\|totalframe)` returned 0 hits in both) | arbitrary | **not rewritable** | refuse |
| Other plugin effects (`@ANM1` etc.) | `斜めブラインド@ANM1` x1056 (third-party) | unknown | numeric params copied verbatim | warn |
| Audio offset / audio objects | `音量`, `左右`; audio object playback via `再生位置` seconds | seconds | unchanged | yes |
| Layer/group references (`対象レイヤー数`) | integer layer count | layers | unchanged | yes |

SDK semantics (read-only headers):
- `OBJECT_LAYER_FRAME{layer,start,end}` ints (`experiments/auto_export/sdk/plugin2.h:60-64`); section frames are ints
  (`get_object_section_frame`, `:423`); `get_object_track_value(..., double frame, ...)` accepts a fractional frame
  position (`:434`); `TRACK_INFO` exposes `mode`, `param[]`, `accelerate`, `decelerate`, `twopoint`, `timecontrol`
  (`:91-99`). `SCENE_INFO` has `rate,scale` (`:143`) and int `frame`/`frame_max`.
- `reference/aviutl2_sdk/filter2.h:323-348`: `rate,scale` per scene, int `frame`/`frame_total`, and *double*
  `time`/`time_total` seconds for the object. Native filters that read `time` stay correct after conversion;
  filters that read `frame` get k-times more frames.

## 3. Rounding policy

- Start: `s' = s*k` (exact). Midpoints: `m' = m*k` (exact). End: `e' = (e+1)*k - 1`, so the covered duration is identical
  (`(e+1)/fps`), as the object spans `(e-s+1)` frames.
- The last trackbar point sits on the last frame. In the original the last value is reached at `e/fps`; at the new
  rate the object's last frame is `(e+1)k-1`, i.e. `1/fps - 1/(k*fps)` later. Two ways:
  1. **Simple**: put the last point at `e'`. Last section stretches by at most 1 original frame (<= 33 ms at 30 FPS). Motion shape is
     preserved to within one original frame. Always valid; no point added.
  2. **Exact**: add a duplicate point at `e*k` holding the last value, so the final `k-1` frames are flat. Exact in time
     but changes the point count of *every* trackbar in the object, and what `|N` params and easing scripts do with an extra
     section is unverified. Make this an opt-in flag and default to option 1.
- Keep strictly increasing integer frame lists: for `k >= 2` and original strictly increasing, `m*k` is strictly increasing. Midpoint
  == end case: original `e` maps to `e'` ((e+1)k-1 > e*k for k>=2), still increasing.
- Non-integer k (e.g. 30 -> 59.94): not a frame-scaling; require integer `k` (`rate' = rate*k`, same `scale`).

## 4. What stays different after conversion

"Real 60 FPS" is exactly what AviUtl2 renders at each new time. The following do not look the same as at 30 FPS or are not
meaningfully fresher:

- Effects stepping per frame (random/noise per frame, `振動` with random strength, particles, `コマ落ち` time-control option,
  scripts that call `rand`, `obj.index`/`obj.frame`): the number of random draws doubles. Visual rate of flicker and strength
  changes; some plugin results are seeded by frame number so the pattern is entirely different.
- `カウンター` or any script that counts frames: unit is unverified; if it counts per frame it runs 2x fast.
- Script text with frame numbers: not rewritable (§2). Time-based `obj.time` is fine.
- `時間制御` with `コマ落ち=1` (frame drop on, `output.aup2:247908`): intentional stepping at the old rate. After conversion
  it still steps with the old *time* quantum only if the step is defined in seconds; if defined in frames it now steps 2x finer.
  Unverified; refuse or warn.
- Video sources: a 30 FPS video object plays on its own source frame clock. At 60 FPS every other output frame will show the same
  source frame (unless the user enables the v2.1.1 "frame rate adjustment" option for video files, changelog v2.1.1, unverified for
  its blend method). Only synthesised content (shapes, text, camera, animations) becomes truly fresh.
- Sub-scenes (`シーン` objects) are evaluated at their own scene's time; if scenes have different rates, scale all by the same `k`
  or the cross-scene time mapping (undocumented) may change. Refuse mixed rates unless `--allow-mixed-rates`.
- Audio: sample rate unchanged; any audio-effect parameter defined in frames, if one exists, is unknown.
- Preview/cursor/range markers: cosmetic, scaled.
- Any plugin whose effect stores a frame number in a free-text or numeric param that the tool cannot recognise.

## 5. Does AviUtl2 already offer this? (question 5)

- Yes, scene frame rate is editable: "シーンリストからシーン選択して右クリック＞シーンの設定で画面サイズ（解像度）やフレームレートなど変更できます"
  (user post: https://x.com/kenji1nu/status/1942466144171938256, unofficial; verified by search snippet only).
  Changelog confirms a scene settings dialog and its frame-rate cap fix: 2.00 beta3 "シーンの設定のフレームレートの上限値を修正";
  beta44 list selectors/presets (https://docs.aviutl2.jp/changelog).
- Public API: `set_scene_frame_rate(int rate, int scale)` with the comment "シーンのフレームレートを設定します" and "シーンの操作は現状Undoに非対応です"
  (`experiments/auto_export/sdk/plugin2.h:378-383`). The comment says nothing about moving objects.
- **No document I could find says whether object `frame=` values or midpoints are rescaled.** The changelog (https://docs.aviutl2.jp/changelog)
  has no entry about converting positions on frame-rate change; the only related lines are for video-file speed change (beta3) and
  the video-file frame-rate adjustment option (v2.1.1). The official docs index (https://docs.aviutl2.jp/) does not describe it.
  I did not run AviUtl2 (forbidden), so **unverified**. The conservative reading is: frame numbers are integers in the file, the dialog
  presumably only changes `video.rate/scale`, and the timeline plays faster per frame (an object of 150 frames becomes 2.5 s at 60).
- Consequence: the tool is **not provably unnecessary**. The cheap decisive test is manual (§7).
- Even if AviUtl2 did rescale, that would be a black box with unknown rounding and unknown handling of `|` params, time control and
  other scenes. The tool would still be useful as a deterministic, auditable operation on a copy.

## 6. Verdict and minimal tool design

**Partly feasible.** The core operation (rate x k, positions x k) is a text transform on a documented-by-observation format with verifiable output;
the refusal rules cover what we cannot prove.

Inputs: `--in PROJECT.aup2 --out COPY.aup2 --factor K` (integer K >= 2; or `--target-rate`, required to be an integer multiple per scene).
Output: a new file (never in place), `output.file` path cleared or retargeted to `<name>_xK.mp4`, `[project] file=` set to the new path, and a
sidecar `COPY.aup2.convert.json` (counts per rule, warnings, hash of input).
Encoding: read/write as bytes, preserve BOM, CRLF (the third-party file starts `[project]\r\n`) and unknown lines verbatim.

Rewrite rules: exactly the table in §2, applied by section type:
1. `[scene.N]`: `video.rate *= K`, `cursor.frame`, `preview.frame`, `display.frame` `*= K`.
2. `[<id>]` object: `frame=` list per §3 (default option 1).
3. Nothing in `[<id>.<n>]` effect sections changes unless the key is on a frame-unit allow-list (currently empty; all verified numeric params are seconds, percent or
   normalised).

Refusal (exit non-zero, no output) when:
- scenes have different `video.rate/scale` and `--allow-mixed-rates` is absent;
- any effect name or key matches the deny-list: script objects/effects carrying text (any value containing `obj.`, `rand`, `frame`, `local `, `function `), `時間制御` with
  `コマ落ち=1`, `カウンター`, `パーティクル`/particle-like names, unknown `@script` effects (warn instead of refuse with `--force`);
- a trackbar item has a value-count that does not equal the frame-list count and its mode is not `再生範囲`;
- the mode is not on an allow-list (`直線移動`, `補間移動`, `瞬間移動`, `移動無し`, `回転`, `時間制御`-families flagged as warn) or has a `|` payload not on an allow-list;
- the new rate exceeds the scene-dialog cap (value unknown: beta3 "fixes the frame-rate upper limit"; the 120 FPS fixture is a known-good size);
- the line structure is not understood (duplicate `frame=`, missing `[scene.0]`, non-integer frame, `frame=` list not strictly increasing);
- unknown scene keys containing `frame`, `mark`, `range`.

What the tool must never do: touch `reference/`, an AviUtl install directory, or the user's source `.aup2`.

## 7. Test plan

Unit / format (no AviUtl2):
1. Round-trip: convert `freerenderfps_30fps.aup2` with K=2; expect exactly `video.rate=60` and `frame=0,299`, all other lines identical
   (diff test). Repeat with the 66 fixture K=2 -> `video.rate=132`, `frame=0,659`; the 120 FPS fixture K=1 is identity.
2. Conversion of `試験用動画.aup2` (read in place, output to the scratch directory) with K=2: every `frame=` pair doubles per the rule, scene rate 120,
   `再生位置=0.000,10.000,再生範囲,0` unchanged, tool refuses/warns for `カウンター` (22 uses). Hash of input is unchanged afterwards.
3. Synthetic midpoint case (hand-written from the observed `frame=a,b,c` / `v0,v1,v2,mode,param` shape): point counts, strict monotonicity.
4. Negative cases per refusal rule above; fuzzed files (missing section, CRLF/LF mixes, BOM).

Host (requires the user to open the copy in AviUtl2 2.1.12 and export; I cannot do it):
5. 2-minute manual probe for question 5: open the 30 FPS fixture, in シーンの設定 change 30 -> 60, save, and read `frame=` and `X=` lines. If `frame=0,149` became `0,299`
   the tool is redundant for the end points; if unchanged, the tool is needed.
6. Export `freerenderfps_30fps` x2 copy normally (FreeRenderFPS OFF, plain 60 FPS). Validate:
   `python -I tools\validate_freefps_output.py OUT.mp4 --project-fps 60 --target-fps 60 --project-frames 300` -> expected `frame_count` 300, `stream_rate` 60/1,
   `no_pairing` N/A (ratio 1), `near_duplicates` should PASS or WARN.
   For the bouncing-circle fixture (X from -200 to 200 linearly, `freerenderfps_30fps.aup2:47`), additionally compare circle centre
   X per frame between this output and `rc2a_clean_30_to_60.mp4` (the plugin's true-60 path): both should be linear in time; a mismatch of the final frame by
   <= 1/30 s is the expected §3 option-1 difference.
7. Run the 66 fixture K=2 -> 132 FPS: `--project-fps 132 --target-fps 132 --project-frames 660`.
8. With the real project, check the final frame/duration only (`av_duration` check) and spot-check by eye that nothing animates 2x fast.

Open questions I could not answer from public material:
- Meaning of `|N` and `|a,b,c,d` suffixes after the mode parameter (`0|27`, `0|0,0,1,0`) -> treat as opaque.
- Units of `カウンター` and `振動` parameters; whether `コマ落ち` quantises in frames or seconds.
- Whether the scene settings dialog rescales positions (not documented, not tested).
- Maximum accepted `video.rate`; exact handling of markers and range selection keys.
- Whether AviUtl2 tolerates a hand-edited file: the loader behaviour is only inferred (every fixture was written by AviUtl2 itself, `version=2010400`).

## Sources

- Local: `experiments/66_to_60_validation/freerenderfps_{30,66,120}fps.aup2`, `experiments/host_matrix/run_dryrun/projects/{30_60,66_60}.aup2`,
  `experiments/auto_export/sdk/plugin2.h`, `reference/aviutl2_sdk/filter2.h`, `tools/validate_freefps_output.md`.
- Third-party read-only: `D:\ARZv2.0.1\ARZv2.0.1\sources\試験用動画.aup2` and `D:\ARZv2.0.1\ARZv2.0.1\output\speunki...\output.aup2`.
- Web: https://docs.aviutl2.jp/changelog ; https://docs.aviutl2.jp/ ; https://x.com/kenji1nu/status/1942466144171938256 (unofficial) ;
  https://scrapbox.io/Garech/AviUtl%E2%86%92AviUtl2_EXO%E3%83%95%E3%82%A1%E3%82%A4%E3%83%AB%E3%81%AE%E4%BA%92%E6%8F%9B%E6%80%A7%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6
  (personal memo: move parameter shape, "動画の再生位置はフレーム管理から秒数管理に変わり", time-control script note; unverified).
