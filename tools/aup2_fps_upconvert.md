# aup2_fps_upconvert.py

Retimes an AviUtl2 project (`.aup2`, plain UTF-8 text) to a new scene frame rate. It writes a new file and never
modifies the input. Two ways to choose the target rate:

- `--factor K` (integer): target = scene rate × K, for example 30 → 60 with K=2.
- `--target-fps RATE` (exact): any positive rational rate, for example `66`, `24`, `120`, `59`, `60000/1001`.

A project may have several scenes (sub-scenes, called compositions in the GUI) with different frame rates. The scene
that is exported is the one named by `[project] display.scene`. The target rate is relative to that scene, and every
other scene follows it or is kept as is, as `--subscene-rate` says. See "Scenes and sub-scenes".

Background and the field table: `docs/aup2_fps_upconvert_feasibility.md`.

Two things go beyond renumbering frames: the `位置` of a `時間制御(オブジェクト)` effect is always recomputed (a two-point line when
that is exact, else a staircase of `瞬間移動` steps), and with `--precise` a track holds its end value after the original object ends.
See "Time control" and "Precise mode".

## Usage

```
python -I tools/aup2_fps_upconvert.py --in IN.aup2 --out OUT.aup2 --factor K [--force] [--allow-frame-scripts]
python -I tools/aup2_fps_upconvert.py --in IN.aup2 --out OUT.aup2 --target-fps RATE [--force] [--allow-frame-scripts]
python -I tools/aup2_fps_upconvert.py --in IN.aup2 --out OUT.aup2 (--factor K | --target-fps RATE) [--precise [--precise-extra-decimals N]]
python -I tools/aup2_fps_upconvert.py --in IN.aup2 --out OUT.aup2 (--factor K | --target-fps RATE) [--subscene-rate {auto,follow,keep}]
```

- Exactly one of `--factor` and `--target-fps` is required (argparse exits with code 2 if both or neither are given).
- `--factor K`: `K` is an integer of at least 2. For integer `K` the output is byte-identical to the earlier
  integer-only version of this tool. With several scenes `K` applies to the exported scene (see "Scenes and sub-scenes").
- `--target-fps RATE`: an integer (`66`) or a fraction `numerator/denominator` (`60000/1001`), positive.
  Decimal values such as `59.94` are refused; write the exact fraction instead.
- Refuses if `OUT` exists (unless `--force`), if `OUT` is the same file as `IN`, or if the rate is invalid.
- Refusals exit with code 2, print `REFUSED: line N: reason` (or `REFUSED: reason` when no line applies) to stderr,
  and write nothing.
- On success it writes `OUT` and `OUT.aup2.convert.json` (the sidecar is `<OUT path>.convert.json`). The sidecar holds:
  - the input and output absolute paths with SHA-256;
  - `factor` (the integer, or `null` in `--target-fps` mode), `source_rate`, `target_rate` and `k`, each as an
    exact `n/d` string (for example `"30/1"`, `"2000/1001"`). `source_rate` and `k` are those of the exported scene;
  - `subscene_rate` (`"auto"`, `"follow"` or `"keep"`) and `scenes`, one entry per scene (see "Scenes and sub-scenes").
    Both keys come right after `k`;
  - every changed line (`line`, `old`, `new`; an inserted line has `old: null` and `inserted: true`);
  - `timing`: the largest start and end error in seconds (exact `n/d` strings) and, per object, the start and end
    error and whether the object was shortened;
  - `notes`, a list that is empty when there is nothing to note;
  - `precise`, present only with `--precise` (see "Precise mode").
- `--subscene-rate {auto,follow,keep}` (default `auto`) says what happens to the scenes other than the exported one.
  See "Scenes and sub-scenes".
- `--allow-frame-scripts` converts despite frame-dependent script (see below). Each occurrence is printed to
  stderr as `WARNING: line N: ...` and also written to `notes`.
- `--precise` re-samples the trackbar values of linear moves so that their speed matches the original project. See
  "Precise mode". `--precise-extra-decimals N` (integer 0 to 4, default 0) writes re-sampled values with N more
  decimals and requires `--precise`.

## Rate arithmetic

Let `Rp = video.rate / video.scale` be the rate of a scene and `Rt = rt / st` the target rate. Then `k = Rt / Rp` of
that scene. All of it is exact (`fractions.Fraction`); no floats are used in the conversion. With one scene (or when all
scenes share one rate) `Rp` is the same everywhere. With several rates each followed scene has its own `Rp` and `k`.

- `--factor K`: `rt = video.rate × K` and `st = video.scale` (unchanged), both of the exported scene, so `k = K` for
  that scene.
- `--target-fps`: `rt / st` is the given fraction in lowest terms.

`round_half_up(x)` is the nearest integer to the exact value `x`, with halves rounded up (`floor(x + 1/2)`).

## What it changes

| Where | Change |
|---|---|
| `[project] file=` | Set to the absolute `OUT` path. |
| `[scene.N] video.rate=` | `rt` (every followed scene; a kept scene is not touched). |
| `[scene.N] video.scale=` | `st`, only when the value differs. If the line is missing, `video.scale=st` is inserted right after `video.rate` (see Scene requirements). |
| `[scene.N] cursor.frame`, `preview.frame`, `display.frame` | `round_half_up(v × k)`, with the `k` of that scene |
| `[N] frame=s,e` (object) | start `s' = round_half_up(s × k)`, end `e' = round_half_up((e+1) × k) − 1`, with the `k` of the object's scene (see Object rules). |
| `[N] frame=p0,p1,…,pn` (intermediate points) | `p0` uses the start rule, `pn` the end rule, each middle point `round_half_up(p × k)`. The result must be strictly increasing; otherwise the tool refuses. |
| `[N] effect` trackbar values (only with `--precise`) | Re-sampled at the new keyframes (plus the points of the end hold) for eligible objects; the object's `frame=` line can get up to four more points. Other objects are unchanged. See "Precise mode". |
| `[N.M] 位置=a,b,直線移動(時間制御),flag` (effect `時間制御(オブジェクト)`) | Recomputed exactly so that the controlled layers show the same content at the same time: a two-point line when that is exact, else a `瞬間移動` staircase (and the object's `frame=` and its constant trackbars get one point per step). Always, with or without `--precise`. See "Time control". |
| `[N.M] コマ落ち=K` (same effect, K ≥ 2) | `K × k` with a two-point line; `1` with a staircase (the staircase holds each segment). See "Time control". |
| `[N.M] 位置=a,b,直線移動(時間制御),flag\|curve` (same effect, with a `\|` payload) | Kept as written (the curve depends only on the progress inside the object); `コマ落ち` K ≥ 2 becomes `K × k` when exact, else the run is refused. See "Time control" and "Curve payloads and 直線移動(時間制御) tracks". |

A scene that is kept (see "Scenes and sub-scenes") and all objects and effects in it are copied byte-for-byte. Everything
else is copied byte-for-byte: encoding, BOM (kept if present), CRLF or LF per line, seconds-based values
(`audio.rate`, `再生位置`, tempo, `display.grid.*`), trackbar values (except `位置` of a time control effect and, with `--precise`,
the eligible tracks), `繰り返し`, `対象レイヤー数` and unknown lines.

For integer `K`, the object rules reduce to `s × K` and `(e+1) × K − 1`, and the scene frame keys to `v × K`, which is
the same as the earlier integer-only tool.

## Object rules

- **Start and end.** An object with `frame=s,e` (`e` inclusive) maps its start and end boundaries independently,
  each rounded half-up. Adjacent objects on the same layer that touched before (`e1 + 1 == s2`) still touch after,
  because they share the same boundary value.
- **Shortening.** If `e' < s'` (the object is shorter than one output frame after rescaling), the object becomes one
  frame (`e' = s'`) and `notes` gets `object at line N shortened to 1 frame`, where `N` is the `frame=` line. Only
  two-value objects are shortened; a multi-point object whose points collapse is refused instead.
- **Same-layer checks.** Each scene has its own timeline. An object belongs to the scene named by its `scene=` line;
  if it has none, to the `[scene.N]` section above it (the real-world file and the fixtures agree with this: every
  object's `scene=` matches the section it is under; objects 0 and 1 of the real-world file have no `scene=` and sit
  under `[scene.0]`). An object whose scene has no `[scene.N]` section is converted with the exported scene's `k`.
Objects are grouped by (scene, `layer=`). Within a group, the rewritten objects keep their order
  and never overlap. A pair that touched before must still touch after. If shortening breaks either rule (for example
  a 1-frame object whose neighbour's start rounds onto it), the tool refuses with the line of the later object. The
  tool does not move boundaries to keep such a pair consistent, because that would break the error bound below. Pairs
  that already overlap in the input are not checked.

## Timing error bounds

For each object, with `Rt` as above, `Rp` the rate of the object's own scene, and times in seconds:

- start error `|s'/Rt − s/Rp|`
- end error `|(e'+1)/Rt − (e+1)/Rp|`

| Object | Start error | End error |
|---|---|---|
| Not shortened | ≤ 1/(2·Rt) | ≤ 1/(2·Rt) |
| Shortened to 1 frame | ≤ 1/(2·Rt) | ≤ 3/(2·Rt) |

The shortened end can exceed 1/(2·Rt) because a 1-frame object is longer than its exact rescaled length. The `timing`
section of the sidecar reports the exact values. The project end moves by at most 1/(2·Rt), or 3/(2·Rt) when the last
object is shortened.

### Motion endpoints (measured, AviUtl2 v2.1.12)

The table above bounds the object's frame range. It does not bound when a linear motion reaches its end. AviUtl
interpolates the move from the object's first frame to its last frame. After retiming, the last frame is
`e' = round_half_up((e+1)·k) − 1`, so the motion ends at `e'/Rt` instead of `e/Rp`. With `k = Rt/Rp` and
`δ = e' − ((e+1)·k − 1)`, where `|δ| ≤ 1/2`:

    (e'/Rt − e/Rp) × Rt = (k − 1) + δ        (in output frames)

In seconds the end-time deviation is `e'/Rt − e/Rp = 1/Rp − 1/Rt + δ/Rt`, so

    |end-time deviation| ≤ |1/Rp − 1/Rt| + half an output frame (1/(2·Rt))

Here `Rp` is the source (project) rate and `Rt` the target rate. In output frames the bound is `|k − 1| + 1/2`.

Measured on 2026-10-17 with the 1.1.0 Free FPS Export, using the retimed motion model
(`experiments/free_fps_matrix/analyze.py`, default `--timeline retimed`; results in `experiments/free_fps_matrix/run_20261017/SUMMARY.md`):

| Case | k | End deviation (output frames) | Motion residual (px) |
|---|---|---:|---:|
| 60 → 66 | 11/10 | +0.10 | 0.156 |
| 72 → 60 | 5/6 | −0.17 | 0.146 |
| 30 → 60000/1001 | 2000/1001 | +1.30 | 0.146 |
| 66 → 24 | 4/11 | −0.64 | 0.156 |
| 66 → 120 | 20/11 | +0.82 | 0.192 |
| 120 → 60 | 1/2 | −0.50 | 0.146 |
| 60 → 50 | 5/6 | −0.17 | 0.161 |
| 72 → 59 | 59/72 | −0.18 | 0.188 |

All eight cases pass the motion check (residual limit 1.0 px). The largest measured end deviation is
+21.65 ms (+1.30 output frames) for 30 → 60000/1001. Its bound is about 25.0 ms (1.5 output frames). For 66 → 24
it is −26.5 ms (−0.64 output frames), with a bound of about 47 ms.

An earlier run modelled the motion on the original project timeline. It reported 66 → 24 (2.18 px) and
30 → 60000/1001 (1.80 px) as FAIL. That model ignored the endpoint shift above, so it is no longer used.

Every output frame is rendered at its own output time; none is a copy.

Long-duration timing is covered by host-free tests (see Tests). Audio-video sync over 30 minutes has not been run in
AviUtl2 yet; the kit is in `experiments/long_av_sync/`.

`--precise` is not in the 1.1.0 build. See the next section.

## Precise mode (`--precise`)

Without `--precise` no trackbar value is changed. Without it, the keyframe frames move but the values stay the same,
so a linear move changes speed by the ratio of the old and new durations. With `--precise`, the values of eligible
trackbars are re-sampled from the original curve at the new keyframe times, so a linear move keeps its original
speed, apart from rounding. After the original object ends, a track holds its end value (as in AviUtl), which needs one or two
more keyframes in the object (see "End hold").

```
python -I tools/aup2_fps_upconvert.py --in IN.aup2 --out OUT.aup2 --target-fps 66 --precise
python -I tools/aup2_fps_upconvert.py --in IN.aup2 --out OUT.aup2 --target-fps 66 --precise --precise-extra-decimals 2
```

Rules (`precise_track_problem`, `precise_keyframes`, `precise_resample` in `tools/aup2_fps_upconvert.py`):

- The rescaled keyframe frames (`frame=`) stay where they are. The list can get extra points (see "End hold"); nothing is
  merged or removed. Every trackbar of the object then has one value per keyframe.
- A trackbar is eligible when its move mode is `直線移動` with parameter `0` (`直線移動,0`), or when all of its values
  are equal (a constant track). `再生範囲` trackbars are left unchanged and do not block their object. The `位置`
  track of a `時間制御(オブジェクト)` effect does not take part: it is recomputed by its own rule (see "Time control"), is
  not counted in `applied`, is not listed in `skipped`, and does not block its object.
- For an eligible trackbar, the value at each new keyframe frame `f'` is the exact value of the original
  piecewise-linear curve at original frame `f'/k` (`k` as in "Rate arithmetic", the `k` of the object's scene), held at the end
  values: the curve at `s` for `f'/k < s` and at `e` for `f'/k > e`, with `s` and `e` the first and last original keyframe
  (`held_curve_value`). The arithmetic is exact (`fractions.Fraction`).
- Each value is rounded half away from zero to the decimal count of its own original line, plus
  `--precise-extra-decimals`. A track whose rounding error is above 0.01 % of its value range adds a note. This is
  not a refusal.
- An object is skipped, with all its values unchanged, if any other trackbar is not eligible (for example a
  trackbar with changing values in `加減速`, `補間移動` or `イージング`, or any trackbar whose value has a
  `|` payload; `直線移動` and `直線移動(時間制御)` are the exceptions, see "Curve payloads and 直線移動(時間制御) tracks"), or if
  its `frame=` is one frame (`frame=s,s`). Each skipped object is listed in `precise.skipped`
  with its `frame=` line and a reason. An object that holds a `時間制御(オブジェクト)` effect is left alone, not listed: its
  `frame=` list must stay two points (a staircase rewrites it itself, see "Time control").
- `--precise-extra-decimals N` must be an integer from 0 to 4 and needs `--precise`. Otherwise the tool refuses.

### End hold

AviUtl keeps the last value of a track after the object's last keyframe, and the object does not exist before its first frame. So
new frame `n` shows `v(min(max(n/k, s), e))` (`v` = the original curve). Where the new object lasts longer than the original
(`e'/k > e`, which is the rule for `k > 1.5`), the line must stop at `e` instead of running on, which takes keyframes of its own
(`precise_keyframes`):

- **End.** `m = floor(e × k)` is the last new frame whose time is not after `e`. The keyframes `m` and `m + 1` are added (only `m` when
  `e × k` is an integer, because `m` then shows `v(e)`); `m + 1` and every later keyframe show `v(e)`. `m` is kept between `s'` and `e'`.
- **Start.** If the new object starts before the original (`s'/k < s`, only possible when `s' < s × k`), its first frame shows `v(s)`.
  `n0 = ceil(s × k)` is the first frame on the curve and is added (with `n0 − 1` if that lies after `s'`).
- Nothing is merged, collinear points stay, and a one-frame object (`frame=s',s'`) gets none. The rescaled keyframes are never
  removed. With `k ≤ 1` the new object usually ends inside the original one and nothing is added (the output is then byte-identical
  to the version without the hold).

Examples (FIX_30 fixture, `X=-200.00,200.00` over `frame=0,149`): 30 → 60 gives `frame=0,298,299` and
`X=-200.00,200.00,200.00` (`Y` and `Z` repeat their constant); 30 → 60000/1001 gives `frame=0,297,298,299` and
`X=-200.00,199.06,200.00,200.00`; 66 → 24 stops at 197.87 (the output ends at 4.958 s, before the original 4.985 s) and keeps
`frame=0,119`.

Whole-frame keyframes cannot put the corner at `e × k` when that is not a whole frame: inside the segment `m..m + 1` the new line is
stretched. A layer shown at a fractional frame (a time control with `k` not an integer, see there) sees that.

`.convert.json` gets a `precise` object only with `--precise`:

- `enabled` (true), `extra_decimals` (only when N > 0), `applied` (number of re-sampled objects);
- `skipped`: a list of `{line, reason}` where `line` is the object's `frame=` line;
- `tracks`: one entry per re-sampled trackbar with `object_line`, `line`, `max_quant_error`,
  `max_quant_error_pct`, `end_extrapolation` (the written last value minus the original last value; the field keeps its name from
  before the end hold: it is `0.000000` when the object is held at the end value, and negative or positive only when the new
  object ends before the original does or the rounding moves it).

Accuracy. The speed error comes only from rounding to the decimal count. The worst reported cases from the
2026-10-17 checks are about 0.025 % for a short, low-amplitude segment at 2 decimals, and about 0.000076 % with
`--precise-extra-decimals 2`.

Limits:

- Verified only by the host-free tests (`tests/aup2_upconvert/test_upconvert.py`, which also compare every new frame with the
  original curve held at its ends) and by the C++/Python cross-check. Not yet run in AviUtl2 v2.1.12 (neither the hold nor the
  extra keyframes). The plan is in `experiments/free_fps_matrix/PRECISE_TEST_PLAN.zh-CN.md`.
- Objects of a kept scene are not processed and are not listed in `precise.skipped`.
- Not verified in AviUtl2: whether it accepts values with more decimals than its GUI shows, and whether it rounds
  them again when the project is read.
- Only linear moves and constant tracks become exact. Other effects keep their old values, so their speed changes as
  it did without `--precise`.
- The plugin's dialog has no extra-decimals option; it uses 0. The C++ port refuses with `arithmetic overflow` when
  64-bit exact arithmetic would overflow. The Python tool uses unbounded integers and does not. Only extreme inputs
  can differ.

## Time control (`時間制御(オブジェクト)`) and `コマ落ち`

A `時間制御(オブジェクト)` object sits on a lower layer number and controls the `対象レイヤー数` layers below it. Its effect
section holds `位置=a,b,直線移動(時間制御),flag`, `繰り返し=N`, `コマ落ち=K` and `対象レイヤー数=N`. Measured in AviUtl2 v2.1.12
(probe videos, frame by frame, error below 0.01 frame; see `docs/KNOWN_LIMITATIONS.md`):

- Let the time control object have `frame=s,e` and `L = e − s + 1`. The host clamps the two **end values** of `位置` to
  `[0, 100]` (`a_c`, `b_c`); it does not clamp frame by frame (`-50 → 100` over 30 frames behaves like `0 → 100`). For each timeline
  frame `f` of the object, `p(f) = a_c + (b_c − a_c) × (f − s) / (L − 1)` (`p = a_c` when `L = 1`) is a percentage of the
  object's **own** range, and the controlled layers show the timeline frame `τ(f) = s + p(f) / 100 × (L − 1)`.
- `コマ落ち=K`: the picture updates once every `K` project frames and shows the state of the first frame of the segment:
  at frame `f` the layers show `τ(g)` with `g = s + floor((f − s) / K) × K`. The segment origin is the start `s` of the time
  control object (not timeline frame 0). `K = 1` is no drop.
- `位置` can also be a `瞬間移動` track with many points (`frame=s,…,e` and `位置=v0,v1,…,vn,瞬間移動,0`): the value of point
  `i` holds on `[p_i, p_(i+1))`, a middle point takes effect at its own frame, and **the value of the last point is never shown**: the
  last frame shows the value of the point before it. The staircase below uses this.
- `繰り返し` is a plain number and does not depend on the frame rate.
- An ordinary `直線移動` track (not time control) with middle points works as usual.

What the new project must do: at the new frame `n` it is the original time `f = n / k` (a fraction of a frame). The original object
holds its last state after `e` and does not exist before `s`, so the original layers show `τ(g(f̂))` with `f̂ = min(max(f, s), e)`,
and the new layers must show `T'(n) = k × τ(g(f̂))` new frames. With `p'(n) = 100 × (T'(n) − s') / (L' − 1)` this is `s' + p'(n) / 100 × (L' − 1)`.

Rules (functions `parse_time_control_position`, `time_control_tau`, `retime_time_control`, `time_control_steps` in
`tools/aup2_fps_upconvert.py`; they apply to objects of followed scenes only, a kept scene is copied byte for byte):

- **Recognised** by `effect.name=時間制御(オブジェクト)`. Its `位置` line must be exactly two plain numbers, the mode
  `直線移動(時間制御)` and a flag (optionally followed by a `|` payload, the curve), and the object must have exactly two `frame=`
  points (`s`, `e`). Otherwise the tool refuses with `REFUSED: line N: time control 位置 must be a two-point 直線移動(時間制御) track`, `N`
  being the `位置` line (the `effect.name` line when there is no `位置` line). The mode `直線移動(時間制御)` is also accepted on
  the tracks of every other effect (see "Curve payloads and 直線移動(時間制御) tracks").
- **Curve** (a `位置` line with a `|` payload, `位置=0.000,100.000,直線移動(時間制御),0|0,0,1,0`): the A/B rewrite below is not
  done. The `位置` line is kept as written (values and payload), the frames are converted as for every object, and
  `コマ落ち` K = 1 stays. For K ≥ 2: when `K × k` and `s × k` are integers it becomes `K × k`; otherwise the tool refuses with
  `REFUSED: time control at line N has a curve and コマ落ち=K cannot be kept exact` (`N` = the `位置` line, no second `line`
  prefix). Note (one per curve object whose scene has `k ≠ 1`, in file order with the other time control notes):
  `time control at line N: 位置 has a curve; kept (object-relative, within one frame)` (`N` = the `frame=` line of the object),
  plus `; コマ落ち K -> K'` when `コマ落ち` was rewritten. A scene with `k = 1` is not touched and gets no note.
- **New range.** `s'`, `e'` follow the object rules above (start rounds `s × k`, end rounds `(e + 1) × k`, minus 1).
  `L' = e' − s' + 1`.
- **Scene with `k = 1`**: nothing is rewritten (`位置`, `コマ落ち` and the frame list stay as they are, no note).
- **Two-point line** (when it is exact; the line keeps the `frame=s',e'` list). Used when both hold:
  1. `K = 1`, or `K × k` and `s × k` are integers (the segments of `K' = K × k` frames then start on the same frames as the
     original ones, counted from `s' = s × k`);
  2. both `p'(s')` and `p'(e')` computed by the continued linear map are in `[0, 100]` (outside the host would clamp the end
     value and bend the line):

         shown(n) = k × τ_lin(n / k)     τ_lin(f) = s + p(f) / 100 × (L − 1)     (τ_lin = s when L = 1, f not held)
         p'(s') = 100 × (shown(s') − s') / (L' − 1)        p'(e') = 100 × (shown(e') − s') / (L' − 1)

     `p` here uses the clamped end values `a_c`, `b_c`. The values are rounded half away from zero to the decimal count of the
     original line (the larger of the two values, the same `quantize` as the precise mode, without `--precise-extra-decimals`)
     and written as `位置=p's,p'e,直線移動(時間制御),flag`. `コマ落ち=K` becomes `K × k` (`K = 1` stays). A forward block
     `0 → 100` keeps its values (`shown(n) = n`); `-50 → 150` is written as `0 → 100`.
  When `L' = 1` (one frame) the layers show `s'` whatever `p'` is: it is always a line, both values are written as the object's own
  percentage at the new frame's time, `p(s' / k)`, and `コマ落ち` is `K × k` when that is an integer, else `1` (nothing to drop).
- **Staircase** (otherwise): `位置` becomes a `瞬間移動` track with one point for every change. For each new frame
  `n = s'..e'` the value is `p'(n)` above with `f̂` and `g` (so the held end, the clamp to `[0, 100]` and the exact
  frame-drop segments are inside it), clamped to `[0, 100]` and rounded as above. The points are `s'`, every frame whose rounded value
  differs from the frame before, and `e'` (when it is not one already); the value of a point is the `p'` of its frame.
  `frame=` becomes `s',…,e'` (all the points), `位置=v0,…,vn,瞬間移動,0` (the key as in the file, flag `0`) and `コマ落ち=1` if it was
  `K ≥ 2` (the staircase holds each segment itself). The steps of a drop start at `ceil((s + j × K) × k)`.
  - At most **4096** points (first and last included); more is refused:
    `REFUSED: time control at line N needs more than 4096 steps` (`N` = the `位置` line, no second `line` prefix).
  - The frame list gets many points, so every other trackbar of the object must be constant (its value is repeated once per point,
    with its mode and payload; `再生範囲` tracks are skipped). Otherwise:
    `REFUSED: time control at line N: the object has other animated trackbars` (`N` = the `位置` line). This is checked before the point
    count.
  - The value of the last point is not shown. If `p'(e')` differs from `p'(e' − 1)` (after rounding) the last frame shows the step of
    `e' − 1`, and the note says so.
  - The result is not accepted as input again: its `位置` is a `瞬間移動` track, so a second conversion is refused with the
    two-point message above.
- **`コマ落ち`** must be a positive integer anywhere (`0`, negative numbers, decimals and the trackbar-style `a,b,mode,…` are refused
  with `REFUSED: line N: コマ落ち=<value> (frame drop) must be a positive integer`). `K ≥ 2` outside a time control effect is refused
  with `REFUSED: line N: コマ落ち=<value> (frame drop) is only converted inside a 時間制御(オブジェクト) effect`. In a time control effect
  every `K` converts: a non-integer `K × k` (and a segment origin that is not on a new frame) no longer refuses, it becomes a staircase.
  Other keys (`繰り返し`, `対象レイヤー数`) are not changed.
- **`notes`**: one entry per time control effect whose lines were rewritten, in file order, after the
  `object at line N shortened to 1 frame` entries and before the precise entries (`N` = the `frame=` line of the object, the
  numbers are the text of the file and of the new line). An effect whose lines did not change gets none.
  - line: `time control at line N: 位置 a,b -> a',b'`, plus `; コマ落ち K -> K'` when `コマ落ち` was rewritten;
  - staircase: `time control at line N: 位置 a,b -> M steps (瞬間移動)` (`M` = the number of points), plus `; コマ落ち K -> 1` when
    `K ≥ 2`, plus `; last frame shows the previous step` when the last frame cannot show its own value.
- **`--precise`**: a time control effect does not take part (it is already exact) and is not listed in `precise.skipped`. An
  object that holds a time control effect is not resampled at all (its `frame=` list must stay as it is), and is not listed.

Worked examples.

- 66 → 60 (`k = 10/11`), block `frame=588,599`, `位置=100.000,0.000,…`: `s' = 535`, `e' = 544`, `L' = 10`. Frame `n` (original `1.1 n`)
  must show `11870/11 − n`, so `p'(n) = 100 × (11870/11 − n − 535) / 9` = 101.01 (clamped to 100), 89.899, 78.788, 67.677, 56.566,
  45.455, 34.343, 23.232, 12.121, 1.010: a staircase of 10 points, `位置=100.000,89.899,…,1.010,瞬間移動,0` with `frame=535,536,…,544`
  and the note `... -> 10 steps (瞬間移動); last frame shows the previous step`. The forward block `0 → 100` of the same project stays a
  line (`p'(524) = 0`, `p'(534) = 100`) and is unchanged.
- 30 → 66 (`k = 11/5`), `コマ落ち=2`, block `frame=0,9`, `0 → 100`: `K' = 4.4` is not an integer, so it is a staircase. The segments of the
  original (frames 0, 2, 4, 6, 8) start at the new frames `ceil(4.4 j)` = 0, 5, 9, 14, 18: `frame=0,5,9,14,18,21`,
  `位置=0.000,20.952,41.905,62.857,83.810,83.810,瞬間移動,0`, `コマ落ち=1`.
- 30 → 60, `コマ落ち=2`, `0 → 100`: a line (`K' = 4`, `s × k = 0`), `コマ落ち=4`.

Error bounds. At every new frame the controlled layers show the original content time of the same moment, up to
the rounding of `位置` (half a unit of the last decimal, scaled by `(L' − 1) / 100` new frames; the staircase rounds each step
itself). Exceptions, all at the first or last frames of a block (the largest values seen in the test sweep are given):

- Staircase: the last frame shows the previous step when the note says so (the host never shows the last point). The deviation
  is at most one step; with a drop a step is a whole segment of `K × k` new frames (3.6 new frames for `K = 4` at 66 → 60).
- Staircase: `s'` and `e'` are rounded, so the wanted time of the first or last frame can lie a little outside `s'..e'`; the clamp
  to `[0, 100]` then gives the first or last frame (at most one new frame in the sweep).
- Line: a frame whose original time lies outside the original block (`n / k` past `e` or before `s`) follows the line instead of
  holding. Only the first and last frames are concerned (as many as `k` rounded up at the end); at the last frame the deviation is
  at most `k − 1/2` new frames (2.0 for 24 → 60; for `k = 2` the last frame of a forward block shows its own last frame, `e'`,
  where the original holds `2 e`). It does not show in a controlled track that holds its end value (a `--precise` conversion),
  only in content that moves with time (video).
- Whole-frame keyframes: a controlled layer's own track, shown at a fractional frame (reversed blocks and drops when `k` is not an
  integer), is linear between whole-frame keyframes. In the segment before an end hold (`m..m + 1` above) the original reaches its
  end value at `e × k`, the new track at `m + 1`, so the value is off inside that one segment (about 9 px of a 1200 px move in a 66 → 60 block of
  the test suite).

Verified by the host-free tests with a model of the measurements above (`host_tau`, `host_display` in
`tests/aup2_upconvert/test_upconvert.py`): 63 136 frames of 1 920 blocks (10 rate pairs × 8 `位置` × 6 frame ranges × 4 drops), the
layers below circle blocks, and the P6 probe at 66 → 132 (`test_kit/5_抽帧探针`). Not yet run in AviUtl2: the converted files
themselves. The measured pieces are the clamp of the end values, the segment origin of `コマ落ち`, and the `瞬間移動` track with
its unseen last point (`docs/KNOWN_LIMITATIONS.md`). `繰り返し` is only copied.

## Easing tracks (`イージング（通常）@イージング`)

The mode `イージング（通常）@イージング` (a script-defined move mode, for example
`X=-1917.00,0.00,イージング（通常）@イージング,0|27`) is on the whitelist. Evidence: the script
`C:\ProgramData\aviutl2\Script\@イージング.tra` (lines 1-9) computes the value only from the fractional part of
`obj.getpoint("index")` (the progress inside the object), the easing parameter and the two end values. It does not read the
frame number, the frame rate or the time. So it is handled like `直線移動`: the object's `frame=` points are rescaled by the
object rules (a multi-point object by the multi-point rule), and the values are kept. The end point is approximated in
the same way as for `直線移動` (see "Motion endpoints").

- The part after `|` (for example `27`, the easing parameter) is kept byte for byte, with any content. In every mode that
  is not `直線移動`, `直線移動(時間制御)` or this one a `|` payload is still refused (`trackbar '|' payload '27' is not on the whitelist`).
- The value count must still match the frame-point count (`trackbar has N values but object [M] has K frame points`).
- `イージング（補間移動）@イージング` is not on the whitelist: it reads `obj.getpoint("time")`, which has not been verified, so
  it is refused as `trackbar mode 'イージング（補間移動）@イージング' is not on the whitelist`.
- `--precise` does not re-sample these tracks. The existing skip texts apply: a track with a `|` payload gives
  `trackbar at line N has a '|' payload` (the payload check comes first, also for constant values), a track without payload
  and with changing values gives `trackbar at line N uses mode イージング（通常）@イージング with parameter P and changing
  values; only 直線移動,0 or constant values can be resampled exactly`. The object is listed in `precise.skipped` and keeps
  its values. A constant easing track without payload is eligible like any constant track and stays constant.
- The order of the checks is unchanged: mode whitelist, then payload, then value count.

## Frame-counting moves (`反復移動`, `ランダム移動`, `移動量指定`)

Measured in AviUtl2 v2.1.12 (`test_kit/9_其他移动方式/結果.zh-CN.md`; parameter at k = 2 and 2.2, step at k = 2). With
k = target rate / scene rate, the move is computed per frame:

- `反復移動` and `ランダム移動`: the parameter is multiplied by k (`X=-600.00,600.00,反復移動,4|15` at k = 2 is `4|30`).
- `移動量指定`: the second value is the move per frame and is divided by k (`X=-600.00,4.00,移動量指定,4` at k = 2 is `2.00`).

Only the measured form is converted: flag `4`, exactly two values and two `frame=` points (middle points are not verified).
The first failing check is reported, in this order: `移動量指定` takes no `|` and the other two need one; flag `4`;
two values and two frame points; for `反復移動` and `ランダム移動` the parameter is a number of at least 1, and for
`ランダム移動` an integer (`15.0` is 15). A `frame=` line that is missing counts as zero points.

For a scene with k ≠ 1 (k = 1 is left alone):

- A `時間制御(オブジェクト)` object of the same scene whose frames overlap the object refuses the line (not verified together).
- `ランダム移動`: parameter × k must be an integer; otherwise it is refused (for example 30 → 60000/1001 gives `30000/1001`).
- The new value is written with at most 6 decimals, trailing zeros dropped (`29.97003`, `1.818182`). For `反復移動` the
  error bound is E = |e − s| × 355/113 × F × |Q − P| / (2 × min(Q, P)²), where F is the object's new frame span, P the exact
  parameter and Q the written one. E above 1/2 refuses the line; so does a written value of 0 (the bound is then unbounded).
  Example: 10 fps (k = 1/3), object 0..899 (F = 299), `X=-960.00,960.00,反復移動,4|1` gives E ≈ 2.7 and is refused;
  a 0..59 object (F = 19) gives E ≈ 0.17 and is written as `4|0.333333`.
- `移動量指定`: with D = step / k and Q the written step, E = |Q − D| × F; above 1/2 it is refused.

`--precise` does not re-sample these tracks: the object is listed in `precise.skipped` with
`trackbar at line N uses mode M; frame-counting modes are not re-sampled`. The parameter is still converted.

## Curve payloads and `直線移動(時間制御)` tracks

Measured in AviUtl2 v2.1.12 (probes R1 and R2):

- An ordinary track `X=a,b,直線移動(時間制御),0` (two points, no payload) is frame by frame the same as `X=a,b,直線移動,0` (0.000 px).
- With middle points the mode ignores their times and runs at constant speed over the whole path; it depends only on the progress
  inside the object. A curve edited in the time control dialog is saved as a payload: `X=-600.00,600.00,直線移動(時間制御),0|1,0,1,0`
  (four normalised control point numbers after `|`). The host also keeps such a payload on plain `直線移動` rows after the move mode
  was switched (`Y=0.00,0.00,直線移動,0|1,0,1,0`), where it has no effect.
- Real projects use the mode on `拡大率` (映像再生), `基本出力`, `カメラ制御` and on `位置` of a time control effect.

Rules (`trackbar_parts`, `TRACK_MODES`, `TRACK_PAYLOAD_MODES`, `precise_track_problem` in `tools/aup2_fps_upconvert.py`):

- `直線移動(時間制御)` is on the whitelist for every trackbar of every effect. The frame list is converted as for any object and the
  values are kept, with two points or more (the value count must still match the frame-point count).
- `補間移動(時間制御)` is handled the same way (measured, probe R3: with or without a curve, with middle points, it follows the progress
  inside the object `u = f / (N − 1)` only; residual ≤ 0.38 px at 30 and 60 fps): on the whitelist, frames converted, values and
  payload kept, with two points or more.
- A `|` payload at the end of a trackbar is allowed in the modes `直線移動`, `直線移動(時間制御)`, `補間移動(時間制御)` and `イージング（通常）@イージング`. Its
  text is kept as it is, is not checked and may contain commas. `trackbar_parts` splits a value at the first `|` when the part before it
  is a trackbar; the parameter then holds the whole `0|1,0,1,0`. Other modes with a payload are refused as before (this now also applies
  to a comma payload such as `瞬間移動,0|1,0,1,0`, which was not recognised as a trackbar before).
- `--precise`, per trackbar (the first problem found in file order blocks the object, as before):
  - `直線移動`, with or without payload: resampled as before; a payload is appended after the flag unchanged
    (`X=-200.00,200.00,200.00,直線移動,0|1,0,1,0`).
  - `直線移動(時間制御)` with a payload, or with other than two values (even if constant): the object is skipped, reason
    `trackbar at line N uses mode 直線移動(時間制御) with a curve or keyframes; it is not re-sampled`.
  - `直線移動(時間制御)`, two values, no payload: resampled like `直線移動` (the mode text stays) when the object needs no extra keyframes.
    When the end hold or the start hold needs extra keyframes (they would be middle points, which the mode ignores), the object is skipped,
    reason `trackbar at line N uses mode 直線移動(時間制御); end hold needs keyframes this mode ignores`. This check comes after the
    per-track checks and uses the first such track of the object; a constant track counts too.
  - `補間移動(時間制御)`, always (payload or not, any value count, constant too; checked after the `直線移動(時間制御)` curve rule and
    instead of the generic `has a '|' payload` rule): the object is skipped, reason
    `trackbar at line N uses mode 補間移動(時間制御); it is not re-sampled`.
  - every other case follows the existing rules (`has a '|' payload` for the other modes, the zero-length reason, and so on).
- A time control effect `位置` curve is covered in "Time control" (kept as written).

## Scenes and sub-scenes

Every `[scene.N]` section has its own `video.rate`/`video.scale`. An object belongs to the scene named by its
`scene=` line, else to the `[scene.N]` section above it. A scene object (`effect.name=シーン`, parameters
`再生位置=0.000,5.000,再生範囲,0`, `再生速度=100.00`, `シーン=1`, `ループ再生=0`) in the main scene plays a sub-scene by time:
its parameters are in seconds and a percentage, do not depend on the frame rate, and are never rewritten.

**Exported scene.** It is the scene named by `[project] display.scene=` (0 when the line is absent). In `--factor` mode
the target rate is the rate of that scene times `K`. The exported scene always follows the target rate. If the scene does
not exist the tool refuses with `display.scene N does not exist` (no line number); a `display.scene` value that is not a
non-negative integer is refused with its line.

**Policy `--subscene-rate`** (default `auto`) for the other scenes `i`, with `R_i` the rate of scene `i` and `R_E` the rate
of the exported scene:

| Policy | Scene `i` |
|---|---|
| `auto` | follows when `R_i >= R_E` (exact comparison of the fractions), else is kept (a slower sub-scene is taken as a deliberate frame drop) |
| `follow` | always follows |
| `keep` | always kept |

- A scene that follows is converted like a single-scene project, but with its own ratio `k_i = Rt / R_i`: its
  `video.rate`/`video.scale` are written as the target rate (the same `rt`/`st` as for the exported scene), its
  `cursor.frame`, `preview.frame` and `display.frame`, the `frame=` of its objects, the touching and overlap checks, and
  the `--precise` re-sampling all use `k_i`.
- A scene that is kept stays at its own rate. The scene section, its objects and their effects are copied byte-for-byte.
  They are also not inspected: the refusals for scripts, trackbars, counters, `コマ落ち` and frame lists, the touching
  and overlap checks, and `--precise` skip them. (Scene keys and `video.rate`/`video.scale` of every scene are still read
  and checked, because the policy needs the rates.) Kept objects are not in `timing.objects` and not in the maximum
  errors, because their error is 0 by construction.
- When every scene has the same rate and the policy is `auto`, every scene follows and the output `.aup2` is byte-identical
  to the output of the version without this option. Only the sidecar has new keys.
- An object whose scene has no `[scene.N]` section is converted with the exported scene's `k`.
- A kept scene and the exported scene may now have different rates in one output. Whether AviUtl2 renders a sub-scene at
  its own rate when it is played by a scene object in a main scene at another rate is a property of the host, not
  checked by this tool.

Sidecar (`.convert.json`), right after `k`:

```
"subscene_rate": "auto",
"scenes": [
  {"scene": 0, "name": "Main", "source_rate": "30/1", "target_rate": "60/1", "k": "2/1", "policy": "follow", "exported": true},
  {"scene": 1, "name": "Sub",  "source_rate": "15/1", "target_rate": "15/1", "k": "1/1", "policy": "keep",   "exported": false}
]
```

- `scenes` has one entry per scene in ascending scene number (the order of the sections in a file written by AviUtl2).
  `name` is the value of the first `name=` line of the scene, or an empty string. For a kept scene `target_rate` equals
  `source_rate` and `k` is `1/1`.
- Top-level `source_rate` and `k` are those of the exported scene.
- For every kept scene `notes` gets `scene N kept at its own rate R` (`R` as `n/d`, for example `15/1`), in the scene
  order. The note `[scene.N] has no video.scale; ...` is only written for followed scenes.

Scene requirements:

- `video.rate` is required in every scene. If `video.scale` is missing, the tool reads it as 1; for a followed scene it
  inserts `video.scale=st` right after `video.rate`. The default of 1 is an assumption about the file format, not
  verified against AviUtl2. The fixtures and the real-world file used here all write `video.scale`. The note
  `[scene.N] has no video.scale; ...` is recorded when the default is used for a followed scene.

## Script in effects

Only effect values are checked. The frame-dependent markers are:

- `\bobj\.(frame|totalframe|index|rand)\b`
- `(?<![\w.])rand\s*\(`
- `\bmath\.random\s*\(`
- `\bmath\.randomseed\b`

In a `テキスト` effect only the text between `<?` and `?>` is scanned (an unterminated `<?` runs to the end of the
value). Text outside `<? ?>` is never scanned, so `random rand obj.x` in plain text converts. A literal backslash
escape such as `\n` is read as a newline first, so `<?\nobj.frame?>` is still caught. In every other effect the
whole value is scanned.

Time-based uses (`obj.time`, `obj.totaltime`, `obj.getvalue`) are not markers and pass through.

- By default a frame-dependent marker refuses (exit 2, `REFUSED: line N: script text uses frame-dependent marker ...`).
- With `--allow-frame-scripts` the conversion runs. Each occurrence prints
  `WARNING: line N: frame-dependent script marker ... frame-based animation will run K times faster and per-frame
  random sequences change` (`K` is `k`, written as a fraction when it is not an integer, and "slower" when `k < 1`)
  and is recorded in `notes`.

## What it refuses

- Both `--factor` and `--target-fps`, or neither (argparse, exit 2).
- `--factor` below 2, and `--target-fps` that is a decimal (such as `59.94`), zero, negative, or not `n` or `n/d`.
- A `[scene.N]` key that is not on the known list and whose name contains `frame`, `mark`, or `range`.
- A scene without `video.rate`.
- A `[project] display.scene` that is not a non-negative integer, or that names a scene that does not exist.
- A counter (`effect.name=カウンター`) with no `速度=` line, or with a `速度=` value that is not a plain number
  (`-?\d+(\.\d+)?`).
- A frame-dependent script marker in an effect value (see above), unless `--allow-frame-scripts` is given.
- A line `コマ落ち=<value>` that is not a positive integer, for example `コマ落ち=0`, `コマ落ち=1.5`, `コマ落ち=-1` or
  `コマ落ち=0,10,直線移動,0`: `REFUSED: line N: コマ落ち=<value> (frame drop) must be a positive integer`. `1` (no drop) is
  kept; `K ≥ 2` is converted only in a `時間制御(オブジェクト)` effect (see "Time control"); elsewhere it is refused with
  `... is only converted inside a 時間制御(オブジェクト) effect`.
- A `時間制御(オブジェクト)` effect whose `位置` is not exactly two numbers with `直線移動(時間制御)` and a flag (a `|` payload is
  allowed), or whose object does not have exactly two `frame=` points
  (`time control 位置 must be a two-point 直線移動(時間制御) track`).
- A time control staircase that needs more than 4096 points (`REFUSED: time control at line N needs more than 4096 steps`), or
  whose object has another trackbar with changing values
  (`REFUSED: time control at line N: the object has other animated trackbars`). `N` is the `位置` line. See "Time control". The old
  refusal `コマ落ち=K cannot be kept exact at the target rate` is gone: such a block becomes a staircase.
- A time control `位置` with a curve and `コマ落ち=K` (K ≥ 2) where `K × k` or `s × k` is not an integer
  (`REFUSED: time control at line N has a curve and コマ落ち=K cannot be kept exact`, `N` = the `位置` line).
- A trackbar whose move mode is not on the whitelist (`直線移動`, `直線移動(時間制御)`, `補間移動(時間制御)`, `補間移動`, `瞬間移動`, `移動無し`, `回転`,
  `再生範囲`, `イージング（通常）@イージング`), or that has a `|` payload in any mode but `直線移動`, `直線移動(時間制御)`, `補間移動(時間制御)` and
  `イージング（通常）@イージング` (the meaning of the payload is undocumented, so only those modes keep it): `REFUSED: line N:
  trackbar '|' payload '<text after the first |>' is not on the whitelist`. The payload may contain commas
  (`0|1,0,1,0`). `イージング（補間移動）@イージング` is refused (its script reads `obj.getpoint("time")`; not verified).
- A trackbar whose value count differs from its object's frame-point count (`再生範囲` is exempt).
- Frame lists that are non-integer, have fewer than 2 values, have a duplicate `frame=`, or are not strictly increasing.
- A multi-point frame list whose points collide or stop increasing after rescaling (for example `0,1,2,599` at 120 → 24
  FPS). The line is reported; points are never merged, so trackbar value counts keep matching.
- A shortened object that breaks touching or overlap with a neighbour on the same layer (see Object rules).
- Input that is not valid UTF-8.

The refusals about counters, scripts, `コマ落ち`, `時間制御(オブジェクト)`, trackbars, frame list values and the touching and
overlap checks apply to the objects and effects of followed scenes only (`コマ落ち` also to lines outside any object or
effect, except in a kept scene's own section); the content of a kept scene is not inspected, so its time control effects
are copied byte for byte. The refusals about `video.rate`/`video.scale`,
unknown scene keys, a duplicate `frame=` and a non-integer `scene=` apply to every scene.

The whitelists and deny-lists are the constants at the top of `tools/aup2_fps_upconvert.py`.

## Limits

- The tool changes frame numbers and rate only. It does not make the content fresher. A 30 FPS video
  object still plays its own source frames, so when the target is higher, some output frames repeat a source frame;
  when it is lower, some source frames are skipped.
- Per-frame random or noise effects, and any effect whose unit is not verified (for example `振動`), will
  produce different results at the new rate. The tool does not check these beyond the rules above.
- Script detection is a text match, not a parser. Only the markers listed above are found, so any other way of
  reading the frame number or a random value is not detected. Text values are read one line at a time, so a value that spans real
  newlines in the file is not joined; only escaped `\n` / `\r` inside one line are handled.
- The last trackbar point is moved to the last new frame (option 1 of the study, section 3). The final
  section can stretch by up to one original frame (about 33 ms at 30 FPS).
- Very short objects (under one output frame) are kept as one frame, so downscaling to a low rate can move their
  end by up to 3/(2·Rt). Touching short objects that cannot be kept consistent are refused.
- Whether AviUtl2 itself rescales positions when the scene rate is changed in the GUI has not been
  verified. This tool does not depend on that answer.
- Output has only been checked against the fixtures in `experiments/`, the synthetic projects in the test suite, and
  the real-world project as read-only input. Loading the output in AviUtl2 is a manual step.

## Export and validation

1. Open `OUT.aup2` in AviUtl2 and export normally with FreeRenderFPS **OFF**.
2. Validate the file with the output validator. Use the new scene rate `rt/st` for both the project and target FPS.
   The validator accepts exact fractions such as `60000/1001`:

```
python -I tools/validate_freefps_output.py OUT.mp4 --project-fps <rt/st> --target-fps <rt/st> --project-frames <frames>
```

For the 30 FPS fixture (`frame=0,149`, so 150 frames), `--factor 2` gives `--project-fps 60 --target-fps 60
--project-frames 300`. `--target-fps 66` gives `--project-fps 66 --target-fps 66 --project-frames 330`, and
`--target-fps 60000/1001` gives `--project-fps 60000/1001 --target-fps 60000/1001 --project-frames 300`.

## Tests

```
powershell -File tests/aup2_upconvert/run.ps1
powershell -File tests/aup2_upconvert/run_long.ps1
```

`tests/aup2_upconvert/test_upconvert.py` runs with plain `python -I`, prints PASS or FAIL per test, and exits
non-zero on failure. Its temporary files go to `$AUP2_TEST_TMP` when set, or to a fresh system temp directory otherwise.
It includes the `--precise` property checks and the worked-example end values, the end hold frame by frame (30 → 60, 66, 60000/1001,
66 → 132, 10 → 9, the start hold 25 → 37, multi-point objects), the time control values (hand-computed lines and staircases with
`コマ落ち` for 30 → 60, 30 → 66, 30 → 45, 30 → 10, 66 → 60 and 30 → 90, `fixtures/time_control_66.aup2`), a host model of the measurements
(`host_tau`, `host_display`) that compares every new frame with the original at the same moment (10 rate pairs × 8 `位置` × 6 frame
ranges × 4 drops, circle blocks with `--precise`, the P6 probe `test_kit/5_抽帧探针/P6_66fps_ARZ式12帧交替_rep1.aup2` at 66 → 132 when
present), their refusals (4096 points, other animated trackbars), the v1.2.6 cases (`直線移動(時間制御)` on ordinary tracks, payloads in the three
modes and their refusals elsewhere, `--precise` with payload and with the time control mode, time control curves and `コマ落ち`), and a check that projects without a time control effect, and
`--precise` outputs that need no hold, produce the same bytes as the previous version (`UNCHANGED_GOLDEN`).
`run_long.ps1` runs `tests/aup2_upconvert/test_long_duration.py` (30 and 60 minute synthetic projects, 108 cases of
30/60 minutes × 6 source rates × 9 targets). It checks boundary error, drift, total duration and audio/picture start
frames.
The timing-property test uses the 30, 66 and 120 FPS fixtures and synthetic projects, and checks the bounds above for
the targets 66, 60, 60000/1001, 24, 120 and 59. The real-world check reads `D:\ARZv2.0.1\ARZv2.0.1\sources\試験用動画.aup2`
in place (factor 2 and 3, and targets 66 and 50) and only ever writes to a temp directory.
