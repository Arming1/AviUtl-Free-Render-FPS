# aup2_fps_upconvert.py

Converts an AviUtl2 project (`.aup2`, plain UTF-8 text) to K times its scene frame rate, for example
30 FPS to 60 FPS (K=2). It writes a new file and never modifies the input.

Background and the field table: `docs/aup2_fps_upconvert_feasibility.md`.

## Usage

```
python -I tools/aup2_fps_upconvert.py --in IN.aup2 --out OUT.aup2 --factor K [--force]
```

- `K` is an integer of at least 2.
- Refuses if `OUT` exists (unless `--force`), if `OUT` is the same file as `IN`, or if `K < 2`.
- Refusals exit with code 2, print `REFUSED: line N: reason` (or `REFUSED: reason` when no line applies) to stderr,
  and write nothing.
- On success it writes `OUT` and `OUT.aup2.convert.json` (the sidecar is `<OUT path>.convert.json`).
  The sidecar holds the input and output absolute paths with SHA-256, the factor, and every changed line
  (`line`, `old`, `new`).

## What it changes

| Where | Change |
|---|---|
| `[project] file=` | Set to the absolute `OUT` path. |
| `[scene.N] video.rate=R` | `R*K`. `video.scale` is unchanged. |
| `[scene.N] cursor.frame`, `preview.frame`, `display.frame` | `*K` |
| `[N] frame=s,e` (object) | `s*K`, `(e+1)*K-1` |
| `[N] frame=s,v1,...,e` (intermediate points) | `s*K`, each `v*K`, `(e+1)*K-1`. The list must be strictly increasing before and after. |

Everything else is copied byte-for-byte: encoding, BOM (kept if present), CRLF or LF per line, seconds-based
values (`audio.rate`, `再生位置`, tempo), trackbar values, and unknown lines.

Scene requirements: all `[scene.N]` sections must share the same `video.rate` and `video.scale`, otherwise the tool refuses.

## What it refuses

- A `[scene.N]` key that is not on the known list and whose name contains `frame`, `mark`, or `range`.
- An effect with `effect.name=カウンター`.
- An effect value containing script text (`obj.`, `rand`, `local `, `function `).
- A line `コマ落ち=1`.
- A trackbar whose move mode is not on the whitelist (`直線移動`, `補間移動`, `瞬間移動`, `移動無し`, `回転`, `再生範囲`), or
  that has a `|` payload (none are whitelisted, because the meaning is undocumented).
- A trackbar whose value count differs from its object's frame-point count (`再生範囲` is exempt).
- Frame lists that are non-integer, have fewer than 2 values, have a duplicate `frame=`, or are not strictly increasing.
- Input that is not valid UTF-8, or scenes with different `video.rate`/`video.scale`.

The whitelists and deny-lists are the constants at the top of `tools/aup2_fps_upconvert.py`.

## Limits

- The tool changes frame numbers and rate only. It does not make the content fresher. A 30 FPS video
  object still plays its own source frames, so every other output frame repeats a source frame.
- Per-frame random or noise effects, and any effect whose unit is not verified (for example `振動`), will
  produce different results at the higher rate. The tool does not check these beyond the rules above.
- The last trackbar point is moved to the last new frame (option 1 of the study, section 3). The final
  section can stretch by up to one original frame (about 33 ms at 30 FPS).
- Only integer K is supported.
- Whether AviUtl2 itself rescales positions when the scene rate is changed in the GUI has not been
  verified. This tool does not depend on that answer.
- Output has only been checked against the fixtures in `experiments/` and the test suite. Loading the
  output in AviUtl2 is a manual step.

## Export and validation

1. Open `OUT.aup2` in AviUtl2 and export normally with FreeRenderFPS **OFF**.
2. Validate the file with the output validator, using the new rate `R*K` for both the project and target FPS:

```
python -I tools/validate_freefps_output.py OUT.mp4 --project-fps <R*K> --target-fps <R*K> --project-frames <frames*K>
```

For the 30 FPS fixture (`frame=0,149`, so 150 frames) at K=2, this is `--project-fps 60 --target-fps 60 --project-frames 300`.

## Tests

```
powershell -File tests/aup2_upconvert/run.ps1
```

`tests/aup2_upconvert/test_upconvert.py` runs with plain `python -I`, prints PASS or FAIL per test, and exits
non-zero on failure. Its temporary files go to `$AUP2_TEST_TMP` when set, or to a fresh system temp directory otherwise.
The real-world check reads `D:\ARZv2.0.1\ARZv2.0.1\sources\試験用動画.aup2` in place and only ever writes to a temp directory.
