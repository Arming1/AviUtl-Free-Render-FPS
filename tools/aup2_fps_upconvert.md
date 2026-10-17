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
  - `notes`, a list that is empty when there is nothing to note.
- `--allow-frame-scripts` converts despite frame-dependent script (see below). Each occurrence is printed to
  stderr as `WARNING: line N: ...` and also written to `notes`.

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
```

`tests/aup2_upconvert/test_upconvert.py` runs with plain `python -I`, prints PASS or FAIL per test, and exits
non-zero on failure. Its temporary files go to `$AUP2_TEST_TMP` when set, or to a fresh system temp directory otherwise.
The timing-property test uses the 30, 66 and 120 FPS fixtures and synthetic projects, and checks the bounds above for
the targets 66, 60, 60000/1001, 24, 120 and 59. The real-world check reads `D:\ARZv2.0.1\ARZv2.0.1\sources\試験用動画.aup2`
in place (factor 2 and 3, and targets 66 and 50) and only ever writes to a temp directory.
