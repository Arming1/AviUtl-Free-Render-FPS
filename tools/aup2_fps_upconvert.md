# aup2_fps_upconvert.py

Retimes an AviUtl2 project (`.aup2`, plain UTF-8 text) to a new scene frame rate. It writes a new file and never
modifies the input. Two ways to choose the target rate:

- `--factor K` (integer): target = scene rate × K, for example 30 → 60 with K=2.
- `--target-fps RATE` (exact): any positive rational rate, for example `66`, `24`, `120`, `59`, `60000/1001`.

Background and the field table: `docs/aup2_fps_upconvert_feasibility.md`.

## Usage

```
python -I tools/aup2_fps_upconvert.py --in IN.aup2 --out OUT.aup2 --factor K [--force] [--allow-frame-scripts]
python -I tools/aup2_fps_upconvert.py --in IN.aup2 --out OUT.aup2 --target-fps RATE [--force] [--allow-frame-scripts]
python -I tools/aup2_fps_upconvert.py --in IN.aup2 --out OUT.aup2 (--factor K | --target-fps RATE) [--precise [--precise-extra-decimals N]]
```

- Exactly one of `--factor` and `--target-fps` is required (argparse exits with code 2 if both or neither are given).
- `--factor K`: `K` is an integer of at least 2. For integer `K` the output is byte-identical to the earlier
  integer-only version of this tool.
- `--target-fps RATE`: an integer (`66`) or a fraction `numerator/denominator` (`60000/1001`), positive.
  Decimal values such as `59.94` are refused; write the exact fraction instead.
- Refuses if `OUT` exists (unless `--force`), if `OUT` is the same file as `IN`, or if the rate is invalid.
- Refusals exit with code 2, print `REFUSED: line N: reason` (or `REFUSED: reason` when no line applies) to stderr,
  and write nothing.
- On success it writes `OUT` and `OUT.aup2.convert.json` (the sidecar is `<OUT path>.convert.json`). The sidecar holds:
  - the input and output absolute paths with SHA-256;
  - `factor` (the integer, or `null` in `--target-fps` mode), `source_rate`, `target_rate` and `k`, each as an
    exact `n/d` string (for example `"30/1"`, `"2000/1001"`);
  - every changed line (`line`, `old`, `new`; an inserted line has `old: null` and `inserted: true`);
  - `timing`: the largest start and end error in seconds (exact `n/d` strings) and, per object, the start and end
    error and whether the object was shortened;
  - `notes`, a list that is empty when there is nothing to note;
  - `precise`, present only with `--precise` (see "Precise mode").
- `--allow-frame-scripts` converts despite frame-dependent script (see below). Each occurrence is printed to
  stderr as `WARNING: line N: ...` and also written to `notes`.
- `--precise` re-samples the trackbar values of linear moves so that their speed matches the original project. See
  "Precise mode". `--precise-extra-decimals N` (integer 0 to 4, default 0) writes re-sampled values with N more
  decimals and requires `--precise`.

## Rate arithmetic

Let `Rp = video.rate / video.scale` be the scene rate and `Rt = rt / st` the target rate. Then `k = Rt / Rp`.
All of it is exact (`fractions.Fraction`); no floats are used in the conversion.

- `--factor K`: `rt = video.rate × K` and `st = video.scale` (unchanged), so `k = K`.
- `--target-fps`: `rt / st` is the given fraction in lowest terms.

`round_half_up(x)` is the nearest integer to the exact value `x`, with halves rounded up (`floor(x + 1/2)`).

## What it changes

| Where | Change |
|---|---|
| `[project] file=` | Set to the absolute `OUT` path. |
| `[scene.N] video.rate=` | `rt`. |
| `[scene.N] video.scale=` | `st`, only when the value differs. If the line is missing, `video.scale=st` is inserted right after `video.rate` (see Scene requirements). |
| `[scene.N] cursor.frame`, `preview.frame`, `display.frame` | `round_half_up(v × k)` |
| `[N] frame=s,e` (object) | start `s' = round_half_up(s × k)`, end `e' = round_half_up((e+1) × k) − 1` (see Object rules). |
| `[N] frame=p0,p1,…,pn` (intermediate points) | `p0` uses the start rule, `pn` the end rule, each middle point `round_half_up(p × k)`. The result must be strictly increasing; otherwise the tool refuses. |
| `[N] effect` trackbar values (only with `--precise`) | Re-sampled at the new keyframes for eligible objects. Other objects are unchanged. See "Precise mode". |

Everything else is copied byte-for-byte: encoding, BOM (kept if present), CRLF or LF per line, seconds-based values
(`audio.rate`, `再生位置`, tempo, `display.grid.*`), trackbar values, and unknown lines.

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
  under `[scene.0]`). Objects are grouped by (scene, `layer=`). Within a group, the rewritten objects keep their order
  and never overlap. A pair that touched before must still touch after. If shortening breaks either rule (for example
  a 1-frame object whose neighbour's start rounds onto it), the tool refuses with the line of the later object. The
  tool does not move boundaries to keep such a pair consistent, because that would break the error bound below. Pairs
  that already overlap in the input are not checked.

## Timing error bounds

For each object, with `Rp` and `Rt` as above and times in seconds:

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
speed, apart from rounding.

```
python -I tools/aup2_fps_upconvert.py --in IN.aup2 --out OUT.aup2 --target-fps 66 --precise
python -I tools/aup2_fps_upconvert.py --in IN.aup2 --out OUT.aup2 --target-fps 66 --precise --precise-extra-decimals 2
```

Rules (`precise_track_problem`, `precise_resample` in `tools/aup2_fps_upconvert.py`):

- Keyframe frames (`frame=`) are not changed. Only trackbar values are.
- A trackbar is eligible when its move mode is `直線移動` with parameter `0` (`直線移動,0`), or when all of its values
  are equal (a constant track). `再生範囲` trackbars are left unchanged and do not block their object.
- For an eligible trackbar, the value at each new keyframe frame `f'` is the exact value of the original
  piecewise-linear curve at original frame `f'/k` (`k` as in "Rate arithmetic"). After the original last keyframe the
  last segment is extended linearly. The arithmetic is exact (`fractions.Fraction`).
- Each value is rounded half away from zero to the decimal count of its own original line, plus
  `--precise-extra-decimals`. A track whose rounding error is above 0.01 % of its value range adds a note. This is
  not a refusal.
- An object is skipped, with all its values unchanged, if any other trackbar is not eligible (for example a
  trackbar with changing values in `加減速`, `補間移動`, `時間制御` or `イージング`, or any trackbar whose value has a
  `|` payload), or if its `frame=` is one frame (`frame=s,s`). Each skipped object is listed in `precise.skipped`
  with its `frame=` line and a reason.
- `--precise-extra-decimals N` must be an integer from 0 to 4 and needs `--precise`. Otherwise the tool refuses.

Endpoints. The value at the new last keyframe is the original curve at that time, so the motion does not always end
at the original endpoint. For 66 → 24 it stops at 197.87 (the output ends at 4.958 s, before the original 4.985 s).
For 30 → 60000/1001 it ends at 201.74, which is 1.74 px past the original 200 because the output's last frame is
after the original end and the last segment is extended.

`.convert.json` gets a `precise` object only with `--precise`:

- `enabled` (true), `extra_decimals` (only when N > 0), `applied` (number of re-sampled objects);
- `skipped`: a list of `{line, reason}` where `line` is the object's `frame=` line;
- `tracks`: one entry per re-sampled trackbar with `object_line`, `line`, `max_quant_error`,
  `max_quant_error_pct`, `end_extrapolation`.

Accuracy. The speed error comes only from rounding to the decimal count. The worst reported cases from the
2026-10-17 checks are about 0.025 % for a short, low-amplitude segment at 2 decimals, and about 0.000076 % with
`--precise-extra-decimals 2`.

Limits:

- Verified only by the host-free tests (`tests/aup2_upconvert/test_upconvert.py`) and by the C++/Python cross-check
  (160 of 160 outputs byte-identical). Not yet run in AviUtl2 v2.1.12. The plan is in
  `experiments/free_fps_matrix/PRECISE_TEST_PLAN.zh-CN.md`.
- Not verified in AviUtl2: whether it accepts values with more decimals than its GUI shows, and whether it rounds
  them again when the project is read.
- Only linear moves and constant tracks become exact. Other effects keep their old values, so their speed changes as
  it did without `--precise`.
- The plugin's dialog has no extra-decimals option; it uses 0. The C++ port refuses with `arithmetic overflow` when
  64-bit exact arithmetic would overflow. The Python tool uses unbounded integers and does not. Only extreme inputs
  can differ.

## Scene requirements

- All `[scene.N]` sections must share one `video.rate`/`video.scale`, otherwise the tool refuses.
- `video.rate` is required. If `video.scale` is missing, the tool reads it as 1 and inserts `video.scale=st` right
  after `video.rate`. The default of 1 is an assumption about the file format, not verified against AviUtl2. The fixtures and the
  real-world file used here all write `video.scale`. The note `[scene.N] has no video.scale; ...` is recorded when the
  default is used.

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
- A scene without `video.rate`, or scenes whose `video.rate`/`video.scale` differ.
- A counter (`effect.name=カウンター`) with no `速度=` line, or with a `速度=` value that is not a plain number
  (`-?\d+(\.\d+)?`).
- A frame-dependent script marker in an effect value (see above), unless `--allow-frame-scripts` is given.
- A line `コマ落ち=1`.
- A trackbar whose move mode is not on the whitelist (`直線移動`, `補間移動`, `瞬間移動`, `移動無し`, `回転`, `再生範囲`), or
  that has a `|` payload (none are whitelisted, because the meaning is undocumented).
- A trackbar whose value count differs from its object's frame-point count (`再生範囲` is exempt).
- Frame lists that are non-integer, have fewer than 2 values, have a duplicate `frame=`, or are not strictly increasing.
- A multi-point frame list whose points collide or stop increasing after rescaling (for example `0,1,2,599` at 120 → 24
  FPS). The line is reported; points are never merged, so trackbar value counts keep matching.
- A shortened object that breaks touching or overlap with a neighbour on the same layer (see Object rules).
- Input that is not valid UTF-8.

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
It includes the `--precise` property checks and the worked-example end values.
`run_long.ps1` runs `tests/aup2_upconvert/test_long_duration.py` (30 and 60 minute synthetic projects, 108 cases of
30/60 minutes × 6 source rates × 9 targets). It checks boundary error, drift, total duration and audio/picture start
frames.
The timing-property test uses the 30, 66 and 120 FPS fixtures and synthetic projects, and checks the bounds above for
the targets 66, 60, 60000/1001, 24, 120 and 59. The real-world check reads `D:\ARZv2.0.1\ARZv2.0.1\sources\試験用動画.aup2`
in place (factor 2 and 3, and targets 66 and 50) and only ever writes to a temp directory.
