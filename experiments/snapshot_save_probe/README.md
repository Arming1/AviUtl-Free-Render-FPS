# snapshot_save_probe: does save_project_file() change the current project path?

A one-off host probe (test only, not production). It answers two questions about
`EDIT_HANDLE::save_project_file(path)` (`experiments/free_fps_export/sdk/plugin2.h`, the
`save_project_file` member, comment "自動バックアップと同じ処理で保存されます"):

1. After the project is saved to another file, does the current project path
   (`PROJECT_FILE::get_project_file_path`, `EDIT_SECTION::get_project_file`) change?
2. Does the project save handler (`register_project_save_handler`) fire?

The window title asterisk (unsaved state) is judged by the tester from the title bar.

## Files

- `snapshot_save_probe.cpp`: plugin source. Plugin name `SnapshotSaveProbe`.
- `build.ps1`: builds `build/x64/SnapshotSaveProbe.aux2` (git-ignored) and copies it to
  `test_kit/11_快照保存/`. Nothing is installed into any AviUtl folder.
- `../../test_kit/11_快照保存/`: install and uninstall scripts and the manual test checklist.

## What the plugin does

- Registers project load and save handlers. Each call records `get_project_file_path()` and a count.
- Registers `Edit > Snapshot Save Probe` with `register_edit_menu_param`. On click:
  1. Reads the project path inside `call_edit_section_param` (`EDIT_SECTION::get_project_file`).
  2. Clears the save counter, creates `%TEMP%\SnapshotSaveProbe`, and calls
     `save_project_file(%TEMP%\SnapshotSaveProbe\snapshot.aup2)` outside any section.
  3. Reads the path again, the save count and the path seen by the save handler, and the snapshot file
     (existence, size, last write time).
  4. Shows a MessageBox and appends the same text (UTF-8, with timestamp) to
     `%TEMP%\SnapshotSaveProbe\result.txt`.

It does not open or export any project.

## Call-context rules used (plugin2.h)

- `EDIT_SECTION::get_project_file` is marked `(call_read_section利用不可)`, so it is called inside
  `call_edit_section_param`. The brief named this as an `EDIT_HANDLE` member; in the header it is a member of
  `EDIT_SECTION` (struct starts at line 168) and takes `EDIT_HANDLE* edit`.
- `save_project_file` is marked `参照ロック、編集ロック状態では利用出来ません`, so it is called with no
  section open. `register_edit_menu_param` is documented as calling back without an edit section, which is the
  context used here.

## Build

```
powershell -NoProfile -ExecutionPolicy Bypass -File experiments\snapshot_save_probe\build.ps1
```

Uses `vcvarsall.bat` from Visual Studio 18 Community, `cl /LD /MT /W4 /O2`, the same as `aux2_output_probe`.
SDK headers are read from `experiments/free_fps_export/sdk/` (not copied).

## Test

See `test_kit/11_快照保存/测试清单.zh-CN.md`.
