# FreeRenderFPS host matrix (AviUtl2 auto-export)

`tools/host_matrix.py` runs the FreeRenderFPS FPS matrix inside a real AviUtl2
through the experimental `auto_export.aux2` job runner, then validates each
exported MP4 with `tools/validate_freefps_output.py`.

Two commands wrap one manual step:

1. `prepare` writes a run directory (project copies, job file, `cases.json`).
2. You run the job list inside AviUtl2 (manual).
3. `validate` checks the results and writes `SUMMARY.md` and `summary.json`.

## Matrix

| Case id | Fixture | Project | Target | Expected frames |
|---|---|---|---|---:|
| `off_66_66` | 66 | 66 | FreeFPS OFF (66) | 330 |
| `30_60` | 30 | 30 | 60 | 300 |
| `66_60` | 66 | 66 | 60 | 300 |
| `66_5994` | 66 | 66 | 60000/1001 | 300 |
| `66_24` | 66 | 66 | 24 | 120 |
| `66_120` | 66 | 66 | 120 | 600 |
| `120_60` | 120 | 120 | 60 | 300 |
| `30_120` | 30 | 30 | 120 | 600 |
| `66_5994_60s` | 66 (60 s) | 66 | 60000/1001 | 3597 |

Fixtures are `experiments/66_to_60_validation/freerenderfps_{30,66,66fps_60s,120}fps.aup2`.
`prepare` refuses to run if a fixture's `video.rate/scale` differs from the case.
The fixtures themselves are never written.

## Prerequisites

- Windows, Python 3.9 or newer, and ffmpeg/ffprobe 5.1 or newer on PATH (or pass
  `--ffmpeg` / `--ffprobe`).
- numpy in the Python used to run the script (`python -m pip install numpy`).
  The script itself never installs anything.
- AviUtl2 v2.1.12 with the FreeRenderFPS package installed.

## Step 1: Install the plugins

1. Install the FreeRenderFPS package into AviUtl2 as in `docs/INSTALL.md`
   (the portable ZIP layout is `data\Plugin\x264guiEx-FreeRenderFPS`).
2. Copy `experiments\auto_export\build\x64\auto_export.aux2` into the AviUtl2
   plugin folder. If it is not built yet, run
   `powershell -NoProfile -ExecutionPolicy Bypass -File experiments\auto_export\build_and_run.ps1`.
   That script builds the `.aux2` and runs only the host-free stub test (no
   AviUtl2 window). It never copies anything into an AviUtl folder.

The scripts do not copy files into AviUtl2. Copy them yourself.

## Step 2: Save a FreeRenderFPS template project (once)

`freerenderfps_config` is not in any repository fixture, so the FreeRenderFPS
settings must come from a project you save in AviUtl2.

1. Open AviUtl2 and create or open any project at 30 fps.
2. Open the FreeRenderFPS output settings, enable FreeRenderFPS and set a target
   (for example 60/1). Leave any path fields (temporary files, stats, cmdex,
   ...) empty. `prepare` refuses a template whose config value contains a
   backslash, because the `.aup2` escaping for strings is not verified.
3. Save the project as `experiments\host_matrix\template_freefps.aup2`. Do not
   save over a fixture in `experiments\66_to_60_validation\`.
4. Check the saved file: it must contain exactly one line that starts with
   `freerenderfps_config=`, and that line must be inside a `[section]` header.
   Confirm it contains `freefps_enable` and no backslash.

`prepare` copies that line into each case's project copy, sets
`freefps_enable`, `freefps_target_rate` and `freefps_target_scale` per case,
and places it in the same section as in the template. It also rewrites the
`file=` line of `[project]` and the `output.file=` line of `[scene.0]` to the copy
paths. Nothing else in the fixture changes.

## Step 3: Prepare the run

```powershell
python -I tools\host_matrix.py prepare --out-dir experiments\host_matrix\run_20261017 --template experiments\host_matrix\template_freefps.aup2
```

Output: `projects\<case>.aup2`, `auto_export_jobs.tsv`, `cases.json` and an
empty `out\`. The job file uses absolute paths and the plugin name
`x264guiEx FreeRenderFPS`. The command prints `config injected: yes` when a
template was given. Without `--template`, it prints a warning and writes
`config_injected: false`. In that case all cases run with FreeFPS OFF, so the
run is not meaningful for the matrix.

## Step 4: Start AviUtl2 with the job file

Set the environment variable in the same PowerShell session, then start AviUtl2
from it. Do not use `setx`, which changes the user environment permanently:

```powershell
$env:AUTO_EXPORT_JOBS = "D:\AviUtl Free Render FPS\experiments\host_matrix\run_20261017\auto_export_jobs.tsv"
& "<path to AviUtl2 folder>\AviUtl2.exe"
```

The variable must be set before AviUtl2 starts. If it is unset, the plugin reads
`auto_export_jobs.txt` next to `auto_export.aux2`.

## Step 5: Run the job list

1. In AviUtl2, choose **Edit > Auto Export > Run Job List**.
2. Wait until the run ends. Each job waits for the edit state, opens the
   project, starts the export with `x264guiEx FreeRenderFPS`, and waits for the
   save to finish. The whole matrix can take a long time, and the 60 s case
   takes the longest.
3. Do not start a second run while one is active. The second command is ignored.
4. If a job times out, the run stops and the remaining jobs are `SKIPPED`. A
   timed-out export cannot be cancelled. Note this in the report.
5. The run is not automatic. Only the menu command starts it.

Results are written to `experiments\host_matrix\run_20261017\auto_export_jobs.tsv.results.tsv`
(UTF-8, header first). Columns: `index status project output plugin rate scale frame_max width height output_bytes elapsed_ms reason`.

Close AviUtl2 afterwards. The plugin does not verify what happens to unsaved
changes in the open project, so start from a clean state.

## Step 6: Validate

```powershell
python -I tools\host_matrix.py validate --run-dir experiments\host_matrix\run_20261017
```

Add `--ffmpeg <path>` and `--ffprobe <path>` if they are not on PATH.
`--results <file>` overrides the results path recorded in `cases.json`.

For each case, `validate`:

- checks that the results row has status `OK` (otherwise the overall is `EXPORT_FAILED`);
- checks that the host loaded the expected project (`rate/scale` match the
  project fps and `frame_max` equals the fixture's last frame), labelled
  `host_loaded_project`;
- checks that the output file exists and is not empty (`output_file`);
- runs `python -I tools\validate_freefps_output.py` with the project fps, target
  fps and project frame count, writing `validation\<case>.json` and
  `validation\<case>.txt`.

Outputs: `SUMMARY.md` (table and details) and `summary.json`. The exit code is
0 only when every case is `PASS`. Otherwise it is 1.

Overall values: `PASS`, `FAIL` (a check failed), `EXPORT_FAILED` (status not
`OK`), `ERROR` (validator error), `NOT_RUN` (no results row) and `INCOMPLETE`
(no failures, but some case has no row).

The validator's `near_duplicates` check is advisory (WARN never fails the case).
See `tools/validate_freefps_output.md`.

## Dry run without AviUtl2

```powershell
python -I tools\host_matrix.py prepare --out-dir experiments\host_matrix\run_dryrun
python -I tools\host_matrix.py validate --run-dir experiments\host_matrix\run_dryrun --results experiments\host_matrix\run_dryrun\FAKE_auto_export_jobs.tsv.results.tsv
```

The dry run needs no template and no AviUtl2. Its results file is generated and
labelled `FAKE` in its first line, and `SUMMARY.md` says so. It reuses the
`rc2a_clean_*.mp4` samples from `experiments/66_to_60_validation/`. It proves
only the validator and report path, not an AviUtl2 export. The 30 → 120 case
has no sample, so it reports `NOT_RUN` and the overall is `INCOMPLETE`.

## What is manual and what is not verified

Manual: Steps 1, 2, 4 and 5 (copying plugins, saving the template, setting the
environment variable and clicking the menu command).

Not verified against a real AviUtl2 host (see `experiments/auto_export/README.md`):

- the menu callback and the thread timer both run on the main thread;
- `get_edit_state()` is safe from the `CHANGE_EDIT_STATE` notification thread;
- the project load callback fires for `open_project_file()`, and `get_edit_info()`
  afterwards reflects the newly opened scene;
- the `.aup2` text form of `freerenderfps_config` (escaping and section placement) is
  not verified against a real AviUtl2 save. This is why `prepare` copies the
  line from a template that AviUtl2 itself saved, and refuses backslashes.
  If the first real run disagrees, check the template's saved line first.
