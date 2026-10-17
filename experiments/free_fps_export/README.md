# free_fps_export: export the open project at a freely chosen FPS

`free_fps_export.cpp` builds `free_fps_export.aux2`, an AviUtl2 general plugin. It adds
**Edit > Free FPS Export...**. The plugin asks for a target FPS, an output file and an output plugin name,
then exports the current project at that rate. It uses only the public SDK (`sdk/plugin2.h`, copied from
`experiments/auto_export/sdk`). It is experimental, not a production plugin.

## Flow

1. Project path. The path comes from the project load and save callbacks (`PROJECT_FILE::get_project_file_path`,
   `plugin2.h` line 992). The callbacks are registered with `register_project_load_handler` (line 1044, also called
   on init) and `register_project_save_handler` (line 1048). A project with no path is refused: save it first.
   `EDIT_SECTION::get_project_file` (line 256) is not used, because the edit menu gives no edit section.
2. Dialog: target FPS as an exact rational (`66`, `60000/1001`; decimals such as `59.94` are refused), output file
   (Browse), output plugin name (default `x264guiEx FreeRenderFPS`), and the checkbox
   "Precise motion (exact speed for linear motion)" (default on; it passes `precise` to `retime()`, with no extra
   decimals).
3. Confirm. The SDK has no unsaved-changes flag, so the user is always asked whether to save the project to its own
   path first. No, or closing the prompt, aborts with no side effects.
4. `save_project_file(original)` once confirmed. Then the saved original is copied to
   `%TEMP%\FreeFpsExport\<timestamp>\src.aup2`. It is a file copy, not a save to temp.
5. `retime()` (`retime.cpp`, pure C++) writes `retimed.aup2` and `retimed.aup2.convert.json` in the same temp folder.
   A refusal stops the run. The message carries the line number.
6. `open_project_file(retimed.aup2, false)`, then `output_file(out, plugin, nullptr, nullptr)`.
7. The run waits for `CHANGE_EDIT_STATE` (SAVE, then EDIT) and checks that the output file exists, is not empty,
   and was written after the start.
8. `open_project_file(original, false)` on every outcome once the user has confirmed, success or failure. The
   original project is reopened, never the temp copy.
9. The temp folder is deleted after success and after failure. `FREE_FPS_EXPORT_KEEP_TEMP=1` keeps it for debugging.

The original project file is written only by the confirmed `save_project_file` in step 4. Every other file this
plugin creates is under `%TEMP%`, except the output file the user chose.

## Re-timer (`retime.cpp`, `retime.h`)

A C++ port of `tools/aup2_fps_upconvert.py` (`--target-fps` and `--factor`), with the same rules, refusals and
messages. The rules are in `tools/aup2_fps_upconvert.md`. Arithmetic is exact 64-bit rational with overflow checks.
An overflow is a refusal. Objects are grouped by (scene, layer), where the scene is the object's `scene=` line or
the nearest `[scene.N]` above it.

Precise mode: `retime(..., precise, precise_extra_decimals)` in `retime.h` and `retime_cli.exe --precise
[--precise-extra-decimals N]` (N from 0 to 4, needs `--precise`). It re-samples the trackbars of linear moves and
constant tracks at the new keyframes, and writes a `precise` object to the sidecar only in this mode. The plugin
passes `precise` from the dialog checkbox and always uses 0 extra decimals. Rules and limits are in
`tools/aup2_fps_upconvert.md`, "Precise mode".

Limits of the port:
- Digits are ASCII only. The Python tool accepts Unicode decimal digits in frame values.
- Frame-marker detection uses Unicode `\w` and `\s` through Windows character classes for BMP code points.
  Supplementary code points count as non-word. This is an approximation.
- Values beyond 64 bits are refused.
- The C++ and Python script-refusal texts are identical, including the `(pass --allow-frame-scripts ...)` hint. There is no known difference.

## Build and test

```
powershell -NoProfile -ExecutionPolicy Bypass -File experiments\free_fps_export\build_and_run.ps1
```

Outputs go to `experiments/free_fps_export/build/x64/` (git-ignored). The script runs:
- `retime_test.exe`: unit tests of the re-timer, using the fixtures in `experiments/66_to_60_validation`.
- `host_stub_test.exe`: the plugin flow against a fake host. The result message and the save prompt are replaced
  by recorders. The tests cover the no-path refusal, the save and cancel prompt, the original path reopened on
  success and on each failure, temp cleanup, and the keep-temp variable.
- `tests/crosscheck.py`: runs `retime_cli.exe` and `tools/aup2_fps_upconvert.py` on the same inputs and compares
  `.aup2` and `.convert.json` bytes. It covers the precise mode too; 160 of 160 pairs were byte-identical on
  2026-10-17.

Copy `build/x64/free_fps_export.aux2` into the AviUtl2 plugin folder yourself. The build never does this.

## Measured in AviUtl2 v2.1.12 (2026-10-17)

The 1.1.0 build was run from **Edit > Free FPS Export...** in AviUtl2 v2.1.12 for eight cases:
60→66, 72→60, 30→60000/1001, 66→24, 66→120, 120→60, 60→50 and 72→59. Results are in
`experiments/free_fps_matrix/run_20261017/` (`SUMMARY.md`, `summary.json`).

- Frame counts match the targets. Video and audio durations match (`av_duration`). No adjacent
  near-duplicate frames (`near_duplicates`). No zero-length motion steps and no reversals.
- The retimed motion check (`experiments/free_fps_matrix/analyze.py`, default `--timeline retimed`) passes all
  eight, with residuals of 0.146–0.192 px against a 1.0 px limit.
- The end of a linear motion moves with the retimed frame range. The end deviation is bounded by
  `|1/Rp − 1/Rt| + half an output frame`. Measured: +21.65 ms (1.30 output frames) for 30→60000/1001, bound
  about 25.0 ms; −26.5 ms (0.64 output frames) for 66→24, bound about 47 ms. See
  `tools/aup2_fps_upconvert.md`, "Motion endpoints". An earlier run that modelled motion on the original
  timeline reported those two as FAIL; that model is no longer used.
- Every output frame is rendered at its own time; none is a copy.
- Not yet reported: long-duration cumulative drift and audio-video sync (host-free tests and a 30-minute
  real-footage run are in progress).
- The `--precise` mode (dialog checkbox, default on) is implemented in the re-timer and is not in the 1.1.0 build.
  It re-samples linear-move keyframe values from the original curve at the new times. Its speed matches the original
  apart from rounding. Not yet run in AviUtl2; the plan is `experiments/free_fps_matrix/PRECISE_TEST_PLAN.zh-CN.md`.

## Not verified without AviUtl2

The 2026-10-17 run of eight cases in AviUtl2 v2.1.12 covered the success path of the items below. All eight
exports completed and produced the expected output. These items were exercised on success only, not on failure:
- the menu item, the dialog layout, the Browse dialog and the prompt box;
- the output file check (exists, not empty, written after the start);
- the event order of `CHANGE_EDIT_STATE` on success.

These still need a run in AviUtl2, and the host-stub test cannot show them:
- whether `save_project_file()` leaves the open project's path unchanged, and whether the save callback runs for it;
- the event order of the project load callback in the real host;
- whether the host still holds the retimed file when the temp folder is deleted (a failed delete is logged and the
  file is left behind);
- a run that times out with the host still busy: the original project cannot be reopened until the host returns
  to the edit state, and the result message says so;
- the `video.scale` default of 1 when the line is missing (an assumption in the Python tool too).
