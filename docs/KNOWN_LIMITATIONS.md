# Known limitations — v1.3.1

Conversion rules are in [RETIME_RULES.md](RETIME_RULES.md). Only limits that still hold are listed here.

## Retimer (Free FPS Export)

- **Low target rates.** When the target rate is very low (for example 66 → 10), an object shorter than one output frame
  is shortened to one frame. After rounding it can overlap its neighbour on the same layer. The conversion is refused. This is not supported yet.
- **Short objects.** A shortened object's end can move by up to 3/(2·Rt). The tool never moves boundaries to keep a touching pair consistent.
- **Unlisted move modes.** `イージング（補間移動）@イージング` is refused, not verified in AviUtl2.
- **Frame-count moves** (`反復移動`, `ランダム移動`, `移動量指定`) are converted only in the form measured in AviUtl2: flag `4`, two frame points,
  and no time control object over the same frames. `ランダム移動` also needs the object to start at frame 0 (its random path depends on the start frame),
  and is refused when the parameter is not an integer after × k (for example 30 → 59.94).
  `反復移動` parameters are written with up to 6 decimals; the host reads at least 5 (C7, E3). `移動量指定` steps are written with the source's decimal count,
  because the host reads only that track's precision (C8, E5); a step that cannot be written within 0.5 is refused (for example 30 → 59.94 with `4.00`).
- **Ordinary `補間移動` and `瞬間移動` tracks** are accepted by the converter but have not been measured in AviUtl2 as ordinary tracks.
- **Frame-dependent scripts** are refused unless `--allow-frame-scripts` is given. The animation then runs `k` times faster
  and per-frame random sequences change.
- **Content is not changed.** Higher rates repeat source frames and lower rates skip them. Per-frame random effects produce different results.
- **Time control staircase.** The last point of a `瞬間移動` track is never shown, so with k < 1 the last frame shows the previous step.
- **Time control line.** The two-point line can show its last frame up to about k − 1/2 new frames late when the time control reaches past its own end.
- **Time control 位置** is rounded to the decimal count of the original line, so each position has a small quantisation step.
- **Curves.** The `|` payload is kept as written and not checked. A time-control `位置` with a curve is object-relative, so its error is within one frame.
- **Precise motion** skips objects with a track it cannot re-sample exactly (for example a changing `加減速`, `補間移動`, `イージング`, or any `補間移動(時間制御)`).
  The skipped objects are listed in `precise.skipped`. Their values stay unchanged.
- **`--precise-extra-decimals`** (and the `extra_decimals` debug setting) are not verified in AviUtl2. The plugin dialog uses 0.
- **C++ and Python overflow.** The C++ retimer refuses on 64-bit arithmetic overflow. The Python tool does not. Only extreme inputs can differ.
- **Without precise motion**, a linear move ends at `e'/Rt` instead of `e/Rp`. The end-time deviation is at most `|1/Rp − 1/Rt| + half an output frame`.
- **Converted time-control projects** have not yet been run in AviUtl2 as whole files. The local corpus of 138 projects (121 converted before 1.3.0, about 10 more expected) is also not yet confirmed in AviUtl2.
- **Sub-scenes.** Whether AviUtl2 plays a kept sub-scene at its own rate inside a main scene at another rate is a host property. It is not checked by the tool. The sub-scene frame shown at a given main-scene time (round down or nearest) is not verified.

## Plugin and runtime

- The built-in FreeFPS timeline hook supports AviUtl2 v2.1.4 x64 only. Its guarded private hook rejects unknown host image/signature combinations.
  It has not been validated with the 1.1.0 binary. The request context permits one active render session.
- AviUtl2's output cache uses an integer frame key. Non-integer ratios such as 30 → 120 or 66 → 120 still pay part of an extra render.
  A one-frame project cannot use the neighbour eviction workaround.
- FreeFPS mode disables AFS, x264guiEx keyframe pre-scan, and timecode output.
- **Separate-process export** (Export in a separate process). A second AviUtl2 window opens and may take focus. Do not close it
  before the export finishes. The plugin ends that window when the export is done, so its settings are not saved.
- FFmpeg and L-SMASH are source/build-provenance locked, but cross-root byte-for-byte reproducibility is not claimed.
- The installer is a minimal console EXE rather than a graphical wizard.
- Side-by-side use must keep the original x264guiEx and FreeRenderFPS in separate directories; the installer refuses the original x264guiEx target.
- Free FPS Export error messages are still English, even though the dialog and the Free Render FPS tab are localized.

## AviUtl 1.x

- The AviUtl 1.x adapter supports official AviUtl 1.10 + ExEdit 0.92 only. Its sub-frame precision is 0.01 project frame,
  and the last output sample may be clamped to the final project frame.
- The AviUtl 1.x package is still 1.0.1 and has not been re-validated.
