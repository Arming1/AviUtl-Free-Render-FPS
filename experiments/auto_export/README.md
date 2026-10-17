# auto_export: export job runner (AviUtl2 general plugin)

`auto_export.cpp` builds `auto_export.aux2`, a general plugin that runs a list of
export jobs through the public SDK (`sdk/plugin2.h`, `sdk/logger2.h`). It is a
regression-test driver for output plugins. It is experimental and is not a
production plugin.

Public API used: `RegisterPlugin`, `InitializeLogger`, `GetCommonPluginTable`,
`UninitializePlugin`; `HOST_APP_TABLE::create_edit_handle`,
`register_edit_menu_param`, `register_event_listener(CHANGE_EDIT_STATE)`,
`register_project_load_handler`; `EDIT_HANDLE::open_project_file`,
`output_file`, `get_edit_info`, `get_edit_state`.

## Usage

1. Build: `powershell -NoProfile -ExecutionPolicy Bypass -File experiments\auto_export\build_and_run.ps1`
   (outputs go to `experiments/auto_export/build/x64/`, git-ignored).
2. Copy `build/x64/auto_export.aux2` into the AviUtl2 plugin folder yourself. The
   build script never copies it.
3. Write a job file (UTF-8, optional BOM, LF or CRLF):

   ```
   # project<TAB>output<TAB>output_plugin
   D:\tests\case30.aup2	D:\tests\out\case30.mp4	x264guiEx
   D:\tests\case60.aup2	D:\tests\out\case60.mp4	x264guiEx
   ```

   Blank lines and lines starting with `#` are ignored. Any other line that does
   not have exactly three non-empty tab-separated fields rejects the whole file.
4. Point the `AUTO_EXPORT_JOBS` environment variable at the job file before
   starting AviUtl2. If it is unset, `auto_export_jobs.txt` next to the `.aux2`
   is used.
5. In AviUtl2, choose Edit > Auto Export > Run Job List. A second command while a
   run is active is ignored.

## Results

`<job file>.results.tsv` is rewritten at the start of each run (UTF-8, header
line first):

`index  status  project  output  plugin  rate  scale  frame_max  width  height  output_bytes  elapsed_ms  reason`

- `status`: `OK`, `FAILED` or `SKIPPED`.
- `rate`, `scale`, `width`, `height`, `frame_max` come from `get_edit_info()`
  after the job's project load callback. `frame_max` is `EDIT_INFO::frame_max`
  (the last frame that holds an object), not a rendered frame count.
- `output_bytes` is the output file size, or `-` if the file is missing.

## Job flow and limits

Each job runs on the host main thread, one step per 100 ms thread timer tick:

1. Wait for the edit state to be `EDIT` (60 s limit).
2. `open_project_file(project, show_confirm=false)`. A `false` return fails the job.
3. Wait for the project load callback and check that the loaded path matches
   (30 s limit).
4. `output_file(output, plugin, nullptr, nullptr)`. A `false` return fails the job.
5. Wait for `CHANGE_EDIT_STATE` to report `SAVE` and then `EDIT`. The job is
   `OK` only if the output file then exists with a non-zero size. Start is
   limited to 30 s and the whole export to 1 h. A start or export timeout cannot
   be cancelled, so the run stops and the remaining jobs are `SKIPPED`.

Limits:

- The output plugin's own success code is not visible to this plugin. Success is
  judged from the output file alone.
- `show_confirm=false` is used so that no dialog blocks automation. What happens
  to unsaved changes in the current project is not verified.
- The run is started from a menu item (manual). There is no automatic start.

## Host-free test

`tests/host_stub_test.cpp` includes `auto_export.cpp` directly and drives it with
a fake host, `EDIT_HANDLE` and `PROJECT_FILE`. It pumps the thread message queue
so the real timer runs. No AviUtl window is created. It covers: a two-job run with
the second command ignored while active; synchronous and asynchronous export
events; open, load, output and missing-file failures; a load-path mismatch;
export start and export timeouts; a busy host; malformed job files; UTF-8, BOM and
CRLF handling; and the default job-file path. It was also checked against three
mutated plugin copies (path check removed, output-size check loosened, skip
step removed); each mutation failed the expected checks.

## Not verified against a real AviUtl2 host

The host-free test does not prove these, which need a run in AviUtl2:

- the menu callback and the thread timer both run on the main thread;
- `get_edit_state()` is safe from the `CHANGE_EDIT_STATE` notification thread;
- the project load callback fires for `open_project_file()`, and `get_edit_info()`
  afterwards reflects the newly opened scene.

The SDK headers used are the mirror revision recorded in `sdk/SOURCE.md`, not the
older copy under `reference/aviutl2_sdk/`. The `CHANGE_EDIT_STATE` event used here
is not in the `reference/` copy.
