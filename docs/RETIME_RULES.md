# Free FPS Export retime rules (v1.3.1)

One table of what the retimer does with each object, track and scene. `k` is the rate of the object's scene times
ratio: `k = Rt / Rp`. `Rp` is the source rate, `Rt` the target rate. "Precise" means `--precise` (on in the plugin).
Full specification, formulas and sidecar fields: [tools/aup2_fps_upconvert.md](../tools/aup2_fps_upconvert.md).

| Object / track | Handling | Precise mode | Error / limits | Evidence |
|---|---|---|---|---|
| Object `frame=s,e` | Start `round_half_up(s·k)`, end `round_half_up((e+1)·k) − 1`. Touching neighbours stay touching. If `e' < s'` the object becomes one frame (noted). | Frames unchanged | Start ≤ 1/(2·Rt); end ≤ 1/(2·Rt), or ≤ 3/(2·Rt) when shortened | spec "Object rules", "Timing error bounds"; host matrix 8/8 PASS (`experiments/free_fps_matrix/run_20261017/`) |
| Multi-point `frame=p0,…,pn` | `p0` start rule, `pn` end rule, middle points `round_half_up(p·k)`. Must stay strictly increasing. | Points are never merged or removed | Collapsing points are refused | spec "What it changes", "What it refuses" |
| `直線移動` (`直線移動,0`) | Values kept; without precise the speed scales with the duration | Re-sampled at the new keyframes, end hold adds up to 2 keyframes when needed | Motion ends at `e'/Rt`: deviation ≤ \|1/Rp − 1/Rt\| + 1/(2·Rt). Measured −0.64 to +1.30 output frames | spec "Motion endpoints"; precise matrix residual 0.125–0.226 px (`experiments/free_fps_matrix/run_20261017_precise/`) |
| `直線移動`, other parameter | Values kept, frames converted | Eligible only if all values are equal; otherwise object skipped and listed in `precise.skipped` | Speed not exact without precise | spec "Precise mode" |
| `直線移動(時間制御)`, two points, no payload (any track) | Frames converted, values kept | Re-sampled like `直線移動` when no end hold is needed; otherwise object skipped | Frame by frame equal to `直線移動` (R1a vs R1b, 0.000 px) | `test_kit/7_轨迹移动方式/结果.zh-CN.md` |
| `直線移動(時間制御)`, more than two points | Frames converted, values kept. Middle-point times are ignored; the move runs at constant speed over the whole path | Object skipped | Max error 0.19 px (R1d, value from probe report) | `test_kit/7_轨迹移动方式/结果.zh-CN.md` |
| `直線移動(時間制御)` with `\|` curve payload | Payload kept byte for byte, not checked, values kept | Object skipped | Curve depends only on progress inside the object | R2 (`\|1,0,1,0`) |
| `補間移動(時間制御)` (any points, payload or not) | Frames converted, values and payload kept | Object always skipped | Residual ≤ 0.38 px at 30 and 60 fps | probe R3 (`test_kit/8_補間移動時間制御/`; result table not yet committed) |
| `補間移動` (ordinary) | On the whitelist: frames converted, values kept | Eligible only if `直線移動,0` or constant | Not measured in AviUtl2 in this repo | `TRACK_MODES` in `tools/aup2_fps_upconvert.py` |
| `瞬間移動`, `移動無し`, `回転` | On the whitelist: frames converted, values kept | Eligible only if constant | Ordinary track not measured (only the time-control staircase is) | `TRACK_MODES` |
| `再生範囲` | Seconds based, not rescaled; exempt from value-count check | Does not block the object | — | `TRACK_COUNT_EXEMPT_MODES`, `PRECISE_EXEMPT_MODES` |
| `イージング（通常）@イージング` | Accepted. Values and `\|` payload (e.g. `27`) kept | Constant tracks eligible; changing tracks skip the object | Value depends only on progress (`@イージング.tra`) | spec "Easing tracks" |
| `イージング（補間移動）@イージング` | Refused: not on the whitelist | — | Reads `obj.getpoint("time")`, not verified | spec "Easing tracks" |
| `ランダム移動`, `反復移動`, `移動量指定`, any mode not in `TRACK_MODES` | Refused (not on the whitelist) | — | Not verified in AviUtl2 | `TRACK_MODES`; spec "What it refuses" |
| `\|` payload in any other mode | Refused | — | — | spec "What it refuses" |
| 時間制御(オブジェクト) `位置`, two points, line (A) | Recomputed in seconds: two-point line with `frame=s',e'`; `コマ落ち` becomes `K × k` | Left alone (not listed) | Host clamps only the two end values to 0..100 | probes Q4a/Q4b/Q4c (`test_kit/5_抽帧探针/`); spec "Time control" |
| 時間制御 `位置`, staircase (B) | `瞬間移動` track, one point per change, `コマ落ち` → 1. At most 4096 points. Other trackbars must be constant | Left alone | Last point never shown: last frame shows the previous step (and k < 1 case) | Q4a/Q4b; spec "Staircase" |
| 時間制御 `位置`, with curve | Kept as written (values and payload); frames converted | Left alone | Object-relative, within one frame. `コマ落ち` K ≥ 2 kept only when `K·k` and `s·k` are integers, else refused | spec "Time control" (curve) |
| `コマ落ち=1` | Kept unchanged | — | — | spec "Time control" |
| `コマ落ち=K` (K ≥ 2) in 時間制御, exact | Line: `K·k`. Staircase: `1` | — | Segments start at the time control's own start `s`, not frame 0 | Q4c |
| `コマ落ち=K` (K ≥ 2) in 時間制御, `K·k` not integer | Staircase; segment starts `ceil((s + jK)·k)` | — | Staircase limits as above | P7 (30 → 66, drop 2; error ≤ 0.22 px) |
| `コマ落ち` = 0, negative, decimal, `a,b,mode` | Refused: `must be a positive integer` | — | `0` crashed the host when typed by hand | spec "What it refuses" |
| `コマ落ち=K` (K ≥ 2) outside 時間制御 | Refused | — | — | spec "What it refuses" |
| `カウンター` (`速度=`) | Must be a plain number (`-?\d+(\.\d+)?`), passed through | — | Time-based; not rescaled | spec "What it refuses" |
| シーン object (`effect.name=シーン`) | `再生位置` (seconds), `再生速度` (%) unchanged | — | — | spec "Scenes and sub-scenes"; v1.2.5 |
| Scene frame rate policy (`[project] display.scene`, `--subscene-rate`) | Exported scene always follows the target. `auto`: other scene follows if `R_i ≥ R_E`, else kept. `follow` / `keep` explicit. Kept scenes are copied byte for byte | Kept scenes are not inspected or re-sampled | Whether AviUtl2 plays a kept sub-scene at its own rate is a host property, not checked. Which sub-scene frame is shown is not verified | v1.2.5 A/B/C cases (`test_kit/4_次合成/`) |
| `cursor.frame`, `preview.frame`, `display.frame` | `round_half_up(v·k)` with the scene's `k` | — | — | spec "What it changes" |
| `video.rate`, `video.scale` | Followed scene: set to `rt/st`. Missing `video.scale` is read as 1 and written | — | Default 1 is an assumption, not verified | spec "Scene requirements" |
| Frame-dependent script (`obj.frame`, `obj.totalframe`, `obj.index`, `obj.rand`, `rand(`, `math.random(`, `math.randomseed`) | Refused by default. With `--allow-frame-scripts`: converted, warning and note | — | Such animation runs `k` times faster; per-frame random sequences change | spec "Script in effects" |
| Short object overlap (low target rate) | Shortened to one frame; if that breaks touching or overlap with a neighbour on the same layer, refused with the later object's line | — | Tool never moves boundaries to fix it. See KNOWN_LIMITATIONS | spec "Object rules"; host-free tests (`tests/aup2_upconvert/`) |
| `繰り返し`, `対象レイヤー数`, seconds-based values (`audio.rate`, tempo, `display.grid.*`), unknown lines | Copied unchanged | — | — | spec "What it changes" |

## When the tool refuses

Refusals print `REFUSED: line N: reason` to stderr, exit with code 2 and write no output file.

- **Frame-dependent script.** Remove the marker from the effect text, or convert with `--allow-frame-scripts` and accept that the animation runs `k` times faster.
- **`コマ落ち` not a positive integer.** Set it to `1` (no drop) or a whole number of 2 or more. Drops other than in 時間制御(オブジェクト) are not converted.
- **時間制御 `位置` not two plain numbers with `直線移動(時間制御)`, or object not two frame points.** Fix the source project so the effect has exactly that form.
- **More than 4096 staircase points, or other animated trackbars.** Make the other trackbars constant. The tool has no option for the 4096 limit; shorten the block in the source project.
- **Unlisted move mode (`ランダム移動` etc.) or `イージング（補間移動）@イージング`.** The tool does not convert them. Changing the mode is a change to the content, so it is up to the user.
- **Overlap or collapse of short objects.** The tool does not move boundaries. Use a higher target rate, or adjust the short object in the source project.
- **Invalid rate** (for example `59.94`). Write the exact fraction, for example `60000/1001`.
- **`display.scene` does not exist.** Fix `[project] display.scene` to an existing scene number.
