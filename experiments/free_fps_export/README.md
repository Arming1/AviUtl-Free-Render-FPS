# free_fps_export: export the open project at a freely chosen FPS

`free_fps_export.cpp` builds `free_fps_export.aux2`, an AviUtl2 general plugin. It adds
**Edit > Free FPS Export...**. The plugin asks for a target FPS, an output file and an output plugin name,
then exports the current project at that rate. It uses only the public SDK (`sdk/plugin2.h`, copied from
`experiments/auto_export/sdk`). It is experimental, not a production plugin.

## Flow

1. Dialog: target FPS as an exact rational (`66`, `60000/1001`; decimals such as `59.94` are refused),
   output file (Browse), output plugin name (default `x264guiEx FreeRenderFPS`).
2. `save_project_file()` the current project to `%TEMP%\FreeFpsExport\<timestamp>\src.aup2`.
3. `retime()` (`retime.cpp`, pure C++) writes `retimed.aup2` and `retimed.aup2.convert.json` beside it.
   A refusal stops the run before the project is changed. The message carries the line number.
4. `open_project_file(retimed.aup2, false)`, then `output_file(out, plugin, nullptr, nullptr)`.
5. The run waits for `CHANGE_EDIT_STATE` (SAVE, then EDIT) and checks that the output file exists, is not
   empty, and was written after the start.
6. `open_project_file(src.aup2, false)` restores the project. The result message is shown.

Once the retimed project has been opened, every outcome restores `src.aup2`. The original project file is never
written. Temp files are kept after the run for diagnosis.

## Re-timer (`retime.cpp`, `retime.h`)

A C++ port of `tools/aup2_fps_upconvert.py` (`--target-fps` and `--factor`), with the same rules, refusals and
messages. The rules are in `tools/aup2_fps_upconvert.md`. Arithmetic is exact 64-bit rational with overflow checks.
An overflow is a refusal. Objects are grouped by (scene, layer), where the scene is the object's `scene=` line or
the nearest `[scene.N]` above it.

Limits of the port:
- Digits are ASCII only. The Python tool accepts Unicode decimal digits in frame values.
- Frame-marker detection uses Unicode `\w` and `\s` through Windows character classes for BMP code points.
  Supplementary code points count as non-word. This is an approximation.
- Values beyond 64 bits are refused.

## Build and test

```
powershell -NoProfile -ExecutionPolicy Bypass -File experiments\free_fps_export\build_and_run.ps1
```

Outputs go to `experiments/free_fps_export/build/x64/` (git-ignored). The script runs:
- `retime_test.exe`: unit tests of the re-timer, using the fixtures in `experiments/66_to_60_validation`.
- `host_stub_test.exe`: the plugin flow against a fake host. A recorder replaces the result message box. Failure
  cases check the restore behaviour.
- `tests/crosscheck.py`: runs `retime_cli.exe` and `tools/aup2_fps_upconvert.py` on the same inputs and compares
  `.aup2` and `.convert.json` bytes.

Copy `build/x64/free_fps_export.aux2` into the AviUtl2 plugin folder yourself. The build never does this.

## Not verified without AviUtl2

These need a run in AviUtl2, and the host-stub test cannot show them:
- the menu item, the dialog layout and the Browse dialog;
- whether `save_project_file()` leaves the open project's file path unchanged;
- the event order of `CHANGE_EDIT_STATE` and the project load callback in the real host;
- the restore behaviour. After a restore the open project's path is the temp `src.aup2`, not the user's
  original file. A later save from AviUtl2 therefore goes to the temp copy. This follows the brief and needs a
  decision;
- the `video.scale` default of 1 when the line is missing (an assumption in the Python tool too).
