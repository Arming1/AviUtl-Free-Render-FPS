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
   (Browse), output plugin name (default `x264guiEx FreeRenderFPS`).
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

Limits of the port:
- Digits are ASCII only. The Python tool accepts Unicode decimal digits in frame values.
- Frame-marker detection uses Unicode `\w` and `\s` through Windows character classes for BMP code points.
  Supplementary code points count as non-word. This is an approximation.
- Values beyond 64 bits are refused.
- The script-refusal text omits the Python tool's `(pass --allow-frame-scripts ...)` suffix, because the plugin has no
  such option.

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
  `.aup2` and `.convert.json` bytes.

Copy `build/x64/free_fps_export.aux2` into the AviUtl2 plugin folder yourself. The build never does this.

## Not verified without AviUtl2

These need a run in AviUtl2, and the host-stub test cannot show them:
- the menu item, the dialog layout, the Browse dialog and the prompt box;
- whether `save_project_file()` leaves the open project's path unchanged, and whether the save callback runs for it;
- the event order of `CHANGE_EDIT_STATE` and the project load callback in the real host;
- whether the host still holds the retimed file when the temp folder is deleted (a failed delete is logged and the
  file is left behind);
- a run that times out with the host still busy: the original project cannot be reopened until the host returns
  to the edit state, and the result message says so;
- the `video.scale` default of 1 when the line is missing (an assumption in the Python tool too).
