# Changelog

## [Unreleased]

### Added

- The frame-count move modes `反復移動`, `ランダム移動` and `移動量指定` (flag `4`, two frame points) are converted:
  `反復移動` and `ランダム移動` parameters are multiplied by k, and the `移動量指定` per-frame step is divided by k, written with the source's decimal count.
  `ランダム移動` is converted only for objects that start at frame 0. Objects that overlap a time control object, or whose values cannot be written exactly, are refused.
- A sub-scene played by one scene object in the exported scene is rescaled on that scene object's timeline when the rate ratio is not an integer, so its object boundaries land on the same output frames as the exported scene. Otherwise a WARNING is written, shown in the Free FPS Export dialog and in the completion message. Verified by probe 10 in AviUtl2 v2.1.12.

### Changed

- Free FPS Export can export without saving the project first. It writes a snapshot of the current project (including unsaved changes) to a temporary folder and exports it in a separate AviUtl2 window; the original window's project path and unsaved state do not change. **Export in a separate process** is checked by default, and forced on for a project that has never been saved. Exporting in the original window (checkbox off) still requires saving the project first, because switching projects discards unsaved changes. Verified by probe 11 in AviUtl2 v2.1.12.
- Frame-dependent effects `振動` and `ノイズ` are refused when the object's rate changes (k ≠ 1). `フレア` is refused at k ≠ 1 unless its `点滅` is the number 0. `手ぶれ` and `ラスター` are no longer refused: they are time-based and are converted unchanged (probe 13). Projects that use refused effects must remove them or export at the project's own rate.

### Fixed

- Scenes are identified by the `scene=` value inside `[scene.N]`, not by the section number N. Objects and `シーン=`
  refer to that value, and the two differ in real projects (for example `[scene.4]` with `scene=9`). Before, an object
  could get the rate plan of another scene when the scenes had different rates. A `display.scene` that names a
  different scene by value and by section number is refused as ambiguous.

### Validation

- Probe 10 (`test_kit/10_次合成对齐/`), AviUtl2 v2.1.12: with one scene object (P1), the exported scene and the sub-scene start on the same output frame at 66, 60 and 30 fps. With two scene objects (P2, not aligned), the sub-scene is one frame late at 60 fps and the WARNING is shown; at 30 fps it happens to match.
- Probe 11 (`test_kit/11_快照保存/`), AviUtl2 v2.1.12: `save_project_file` to another path writes a snapshot that includes unsaved changes, and does not change the project path or the unsaved state. Checked for a never-saved project (case A) and a saved project with unsaved changes (case B).
- Probe 9 (`test_kit/9_其他移动方式/`), AviUtl2 v2.1.12, two rounds: at the same instant, the largest position
  difference between the original and the converted project is 0.24 px.
- Probe 9 round 3, end to end (Free FPS Export, test plugin 7F3CC96B): E1–E4, E6, E7 pass (≤ 0.24 px against the original or the formula).
  `移動量指定` 30 → 59.94 (written as 2.002, host reads 2.00, 0.59 px at the last frame) failed; that step now uses the source's decimal count.
- Probe 13 (`test_kit/13_按帧滤镜/`), AviUtl2 v2.1.12, portable build, exported automatically with the same encoder: `手ぶれ` and `ラスター` change with time, not with the frame number; `フレア` with 点滅=0 does not change over time; `振動` uses a per-frame period with randomness. `ノイズ` was not measured. Removing all 8 `フレア` objects, or all 6 `手ぶれ` effects, from user project `10 3` (66 → 60) did not reduce the significant differences in any time range, so those differences come from misaligned sub-scenes and other causes.

## [1.3.1] — 2026-10-18

### Added

- **Export in a separate process** checkbox in the Free FPS Export dialog. When it is checked, the original AviUtl2
  window keeps its project. The plugin opens the re-timed copy in a second AviUtl2 window, exports it, closes that
  window, and the original window then shows the completion message with the encoder summary.
- The checkbox is checked automatically when the project uses `SceneMotionBlur_K` (`MotionBlur_K.aux2`), because
  switching projects crashes AviUtl2 with that filter. It is unchecked otherwise and the flow is the same as 1.3.0.

### Validation

- AviUtl2 v2.1.12: a project with `SceneMotionBlur_K` exported 66 → 132 fps in a separate process, and the original
  window did not crash.
- AviUtl2 v2.1.12: a project without that filter exported 66 → 60 with the checkbox unchecked.
- The conversion rules are unchanged (Python/C++ cross-check 3630/3630 identical).

## [1.3.0] — 2026-10-18

### Added

- `直線移動(時間制御)` and `補間移動(時間制御)` are accepted on any effect track, with two or more points. Values and
  `|` curve payloads are kept byte for byte. Frames are converted like `直線移動`.
- 時間制御(オブジェクト) `位置` with a curve keeps its curve and values; only the frames are converted.
- `test_kit` (host test projects, probes, scripts and checklists) is in the repository.

### Changed

- Precise motion handles the time-control modes: a two-point `直線移動(時間制御)` with no payload is re-sampled
  when no end hold is needed; `直線移動` with a payload is re-sampled and keeps the payload. Other time-control
  tracks, and objects with a curve or more than two points, are skipped and listed in `precise.skipped`.
- 時間制御 `位置` with a curve: `コマ落ち=K` (K ≥ 2) is kept only when `K × k` and the controlled seconds are integers.
  Otherwise the conversion is refused with a message naming the line.

### Fixed

- C++ retimer: the time-control staircase looks up objects per object. A 35 MB project went from minutes to 2.7 s.
- C++ retimer matches the Python rules for time-control modes and curve payloads (cross-check 3630/3630, plus 56 real
  projects byte-identical).

### Validation

- Probes R1–R3 in AviUtl2 v2.1.12 (`test_kit/7_轨迹移动方式/`, `test_kit/8_補間移動時間制御/`):
  - R1a and R1b (`直線移動` and `直線移動(時間制御)`, two points, no curve) give identical frames (160 of 160, 0.000 px).
  - R1d: middle-point times are ignored and the move runs at constant speed over the path (max error 0.19 px).
  - R2: a curve edited in 時間制御編集 is stored as the payload `|1,0,1,0`.
  - R3: `補間移動(時間制御)` follows the progress inside the object, with residual ≤ 0.38 px at 30 and 60 fps
    (figure from `tools/aup2_fps_upconvert.md`; the result table in `test_kit/8_補間移動時間制御/` is not yet filled in).
- CI runs the Python and C++ tests, the cross-check and the plugin build. Actions writes run summaries, and the R1/R2
  host results are recomputed from the committed per-frame data (`tests/host_results/`).
- Local corpus of 138 projects: 121 converted before this release, and about 10 of the remaining 17 are expected to
  convert now. Not yet confirmed in AviUtl2.
- Known limit: at very low targets (for example 66 → 10) an object shorter than one output frame can overlap its
  neighbour after rounding, and the conversion is refused. See `docs/KNOWN_LIMITATIONS.md`.

## [1.2.5] — 2026-10-18

> 1.2.2 was an internal test version number and was never publicly released.

### Added

- Free FPS Export accepts projects whose scenes have different frame rates. Version 1.2.1
  refuses them.
- **Subscene rate** option in the Free FPS Export dialog, with three choices:
  - **Auto** (default): the exported scene always follows the target rate. Another scene
    follows the target rate when its source rate is at least the exported scene's source
    rate. A scene with a lower source rate keeps its own rate, so an intentional frame-rate
    reduction (for example 15 fps inside a 30 fps project) is kept.
  - **Follow all**: every scene is re-timed to the target rate.
  - **Keep all**: only the exported scene is re-timed; every other scene keeps its own rate.
- A scene plan line in the dialog lists each scene's source rate, target rate, policy and
  whether it is the exported scene.
- The exported scene is `display.scene` in `[project]` (default 0).
- `tools/aup2_fps_upconvert.py --subscene-rate {auto,follow,keep}` (default `auto`), with the
  same option in the C++ re-timer. The `.convert.json` sidecar adds `subscene_rate` and a
  `scenes` list (`scene`, `name`, `source_rate`, `target_rate`, `k`, `policy`, `exported`),
  and one `notes` line per kept scene.
- Script easing tracks `イージング（通常）@イージング` are accepted. The `|` payload is kept byte for byte.
  Precise motion skips objects with such tracks. `イージング（補間移動）@イージング` is still refused.

### Changed

- Scene objects (`シーン`) keep their playback position (seconds) and playback speed (%).
  They are not rescaled.
- A followed scene uses `k = target rate / source rate` for object frames, cursor, preview and
  display frames, overlap checks and Precise motion. A kept scene is copied byte for byte.
- If every scene has the same rate and the policy is Auto, the output `.aup2` is byte-identical
  to earlier versions. Only the new sidecar keys are added.
- `コマ落ち` (frame drop in 時間制御(オブジェクト)) is a number, not a switch. `コマ落ち=1` (no drop) is converted
  and the line is kept unchanged. In a time control effect of a followed scene, `コマ落ち=K` (K ≥ 2) becomes K × k
  when that is an integer. Otherwise the block is written as a staircase (see below) instead of being refused.
  K ≥ 2 elsewhere and `コマ落ち=0` are still refused.
- 時間制御(オブジェクト) `位置` (a two-point 直線移動(時間制御) track) is recomputed exactly in seconds for followed
  scenes, with or without Precise motion. When the new end values fall outside 0..100 (the host clamps only the end
  values) or a drop is not an integer multiple, the block becomes a `瞬間移動` staircase with one point per step
  (at most 4096 points; the object's other trackbars must be constant), so the controlled frames stay exact.
- Precise motion holds the end value after the original end instead of extrapolating past it. Extra keyframes are
  added where the new object would outlive the original (see `tools/aup2_fps_upconvert.md`, "End hold").
- The Free FPS Export dialog hides the **Precise motion** checkbox. Precise motion is always on in normal mode.
  A `free_fps_export.ini` next to the plugin DLL with `[debug] enabled=1` shows the checkbox again and adds
  `[debug]` settings: `precise`, `extra_decimals` (0 to 4) and `keep_temp` (keeps `retimed.aup2` and `.convert.json`).
  The dialog title then shows `[Debug]`. The example file is `free_fps_export.ini.example` in `test_kit`.

### Validation status

- Sub-scene rate policy: the A/B/C cases of `test_kit/4_次合成/` passed in AviUtl2. The step checker is
  `experiments/subscene_rate/analyze_steps.py`.
- Time control and frame-drop probes (`test_kit/5_抽帧探针/`) ran in AviUtl2: the host clamps only the end values of
  位置, the `コマ落ち` segment starts at the time control's own start, and in a `瞬間移動` track the last point is never
  shown. P6 (66 → 132) per-frame error is at most 0.27 px. P7 (30 → 66, drop 2) segment starts match `ceil(4.4 j)` and
  the error is at most 0.22 px. ARZ (66 → 132) minimum PSNR rose from 32.2 dB to 41.5 dB.
- Precise motion end hold and its extra keyframes were run in AviUtl2: the P6 export (66 → 132) had the end hold on
  and its per-frame error is at most 0.27 px.
- Not yet run in AviUtl2: the converted time control files as a whole, and `--precise-extra-decimals` values above 0.
  See `docs/KNOWN_LIMITATIONS.md`.

## [1.2.1] — 2026-10-17

Patch release on 1.2.0: Free FPS Export encoding settings, localization and
Free Render FPS tab layout fixes.

### Added

- **Encoder settings...** button in the Free FPS Export dialog. It opens the same
  x264guiEx settings window as the output settings, and changes apply immediately.
- Encoder summary line in the dialog, for example
  `Encoder: CRF 20 / medium / AAC (ffmpeg) 192kbps / mp4`. The container follows
  the output file extension.
- Chinese, Japanese and English localization for the Free Render FPS tab in the
  output settings window and for the Free FPS Export dialog. Error messages are
  still English.

### Changed

- If the Free FPS Export output file name has no recognized extension, the default
  extension is appended when you press OK.
- Free FPS Export encodes with the bitrate, preset, audio and container settings in
  the x264guiEx settings window. Export only turns off the built-in Enable Free
  Render FPS switch and does not change the saved settings.
- The host notice on the Free Render FPS tab is now bold.

### Fixed

- Free Render FPS tab layout is sized from measured text widths. The target FPS
  dropdown is no longer covered by its label (60 FPS was shown as 0 FPS), the
  description text no longer overflows, and the large empty area below the
  controls is removed.

### Tools

- The `test_kit` install and rollback scripts request administrator rights
  automatically. These are test tools only.

### Validation status

- The interface was confirmed on AviUtl2 v2.1.12 (2026-10-17).
- No new conversion or A/V sync runs are recorded for 1.2.1.

## [1.2.0] — 2026-10-17

### Added

- Precise motion for Free FPS Export. The **Precise motion (exact speed for linear motion)** checkbox is on by
  default. With it, the trackbar values of linear moves (`直線移動,0`) and constant tracks are re-sampled from the
  original curve at the new keyframe times, so the speed of a linear move matches the original project. Keyframe
  frames are unchanged. Other objects are left as they are and listed in the `precise.skipped` section of the
  `.convert.json` sidecar.
- `tools/aup2_fps_upconvert.py --precise` and `--precise-extra-decimals N` (0 to 4, needs `--precise`), with the same
  options in the C++ re-timer (`retime_cli.exe`). The C++ and Python outputs are byte-identical in the
  cross-check (160 of 160 pairs on 2026-10-17).
- Long-duration test (`tests/aup2_upconvert/test_long_duration.py`, `run_long.ps1`): 108 host-free cases of 30 and
  60 minutes, checking boundary error, drift, total duration and audio/picture start frames.
- 30-minute audio-video sync kit (`experiments/long_av_sync/`). It has been run in AviUtl2 v2.1.12 (see Verification below).

### Changed

- Documentation: the precise mode is described in `tools/aup2_fps_upconvert.md`, `docs/KNOWN_LIMITATIONS.md`,
  `experiments/free_fps_export/README.md` and `README.zh-CN.md`. Earlier notes that it was "in progress" are removed.

### Validation status

- Precise mode is verified by host-free tests, the C++/Python cross-check and in-host runs on AviUtl2 v2.1.12
  (see Verification below).
- Not yet verified in AviUtl2: whether values with extra decimals are accepted and kept on load.
- Known limit: the last output frame of a linear move may end 1.74 px past the original endpoint (30 → 60000/1001),
  or short of it (66 → 24 ends at 197.87).

### Verification

- In-host precise-mode matrix on AviUtl2 v2.1.12 (2026-10-17): all eight cases PASS (60→66, 72→60, 30→60000/1001,
  66→24, 66→120, 120→60, 60→50, 72→59). Frame counts match, motion residuals are 0.125–0.226 px against a 1.0 px
  limit, with zero stalls and zero reversals, and speed error is at most 0.007% against the original project slope.
- The same videos fail the older non-precise `retimed` motion model at 30→60000/1001 and 66→24 (1.75 px and 2.13 px),
  which confirms that the export took the precise path.
- 30-minute audio-video sync passes at 66, 50 and 60000/1001 fps: max |offset| ≤ 8.33 ms, drift ≤ 0.004 ms/min,
  duration error ≤ 0.0001%, and all 1800 flash/beep pairs matched one-to-one.
- Test binary: the user's installed plugin (`C:\ProgramData\aviutl2\Plugin\x264guiEx-FreeRenderFPS\x264guiEx-FreeRenderFPS.aux2`), SHA-256 `114C307016679E852674DF7A974889CACF4E340449DA475578C3A1A259F6076B`, identical to `dist\AviUtl2-FreeRenderFPS-v1.1.0`.
- Details and evidence: `docs/VALIDATION_MATRIX.md` (section "AviUtl2 v2.1.12 — Free FPS Export（精确运动模式）"),
  `experiments/free_fps_matrix/run_20261017_precise/` and `experiments/long_av_sync/run_20261017/`.

## [1.1.0] — 2026-10-17

Free render FPS on AviUtl2 through a single plugin file, plus installer and
validation-tool updates.

### Added

- `x264guiEx-FreeRenderFPS.aux2` now contains both the x264guiEx FreeRenderFPS
  output plugin and the Edit > **Free FPS Export...** menu item. The export path
  re-times the scene through the public AviUtl2 SDK, so any rational output FPS
  works on AviUtl2 v2.1.12. The built-in timeline hook still supports only
  AviUtl2 v2.1.4 x64.
- `.aup2` retimer tool (`tools/aup2_fps_upconvert.py`) with host-free tests in
  `tests/aup2_upconvert`, plus validation tools (`tools/validate_freefps_output.py`,
  `tools/host_matrix.py`).
- `--cleanup-legacy-backups` installer option (opt-in) that moves old
  `Plugin\x264guiEx-FreeRenderFPS.backup-*` folders into `FreeRenderFPS-backups`.
  It only moves folders and never deletes them.

### Changed

- On unsupported AviUtl2 hosts, exporting with Enable Free Render FPS shows a
  message pointing to Edit > Free FPS Export and aborts; it does not render at
  project FPS.
- The installer migrates an installed legacy `.auo2` to `.aux2` on upgrade. The
  old file is kept in a timestamped backup, and backups are kept outside the
  `Plugin` folder.

### Validation status

- Host-free tests (aup2 up-convert and the FreeRenderFPS scheduler, x64 and x86)
  pass.
- In-host Free FPS Export matrix on AviUtl2 v2.1.12 (2026-10-17, eight cases:
  60→66, 72→60, 30→60000/1001, 66→24, 66→120, 120→60, 60→50, 72→59). Frame
  counts, video/audio durations, adjacent near-duplicate frames and motion steps
  match the targets. The retimed motion check passes all eight, with residuals of
  0.146–0.192 px against a 1.0 px limit. An earlier run modelled motion on the
  original project timeline and reported 66→24 and 30→60000/1001 as FAIL; the
  retimed model is the correct one for Free FPS Export. The largest motion end
  deviation is 1.30 output frames (30→60000/1001, +21.65 ms), bounded by
  |1/Rp − 1/Rt| + half an output frame. See
  `tools/aup2_fps_upconvert.md`. Results: `experiments/free_fps_matrix/run_20261017/`.
  Long-duration drift and audio-video sync are not yet reported.

## [1.0.1] — 2026-10-17

Maintenance release for the AviUtl2 and AviUtl 1.x builds.

### Fixed

- AviUtl 1.x: release builds no longer append `freerenderfps_aviutl1_probe.log`
  on every frame request; the diagnostic trace is compiled only with
  `FREEFPS_AVIUTL1_PROBE_LOG=1`.
- AviUtl 1.x: the host copy-frame flag is ignored while FreeRenderFPS is active.
  It was queried with the output sample index and could resend the previous
  image.
- Both hosts: installing or removing the timeline hook no longer allocates
  memory while other threads are suspended, and refuses to patch while a thread
  is executing the bytes being rewritten.
- A second concurrent export is rejected without removing the active export's
  hook.
- The AviUtl 1.x package no longer ships a default `x264guiEx-FreeRenderFPS.conf`,
  so extracting it cannot overwrite user settings.

### Changed

- Cache-eviction renders are reused once when the next sample needs exactly
  that integer coordinate: 30 → 60 now costs one host render per output frame
  instead of 1.5, and 30 → 120 drops from 1.75 to 1.25.

### Added

- Host-free scheduler test harness in `tests/freefps_scheduler` (x64 and x86).
- `tools/package_aviutl1_release.ps1` for the AviUtl 1.x package.

### Validation status

- Release x64/Win32 builds and the host-free harness pass. The in-host FPS
  matrix (AviUtl2 v2.1.4; AviUtl 1.10 + ExEdit 0.92) has not yet been re-run on
  the 1.0.1 binaries; `docs/VALIDATION_MATRIX.md` and
  `docs/LEGACY_AVIUTL1_VALIDATION.md` record the 1.0.0 results.

## [1.0.0] — 2026-08-13

- Added audited x264, LGPL FFmpeg and ISC L-SMASH runtime with separate source
  provenance and release-artifact locks.
- Fixed FFmpeg standard-input AAC support by enabling the required `fd` protocol.
- Fixed portable layout to extract directly into AviUtl2 `data\Plugin`.
- Fixed package input selection so formal builds cannot silently reuse a stale
  RC1 AUO2; the packager now verifies the AUO2 ProductVersion against the
  requested release label.
- Passed the full FPS matrix, 60-second rational-rate test, installer lifecycle
  self-test, and final setup/portable package-only real H.264/AAC encodes.

## [1.0.0-rc1] — 2026-08-13

Release-candidate recovery; **not a production v1.0.0 release**.

### Fixed

- Replaced the deployed Debug build with a Release x64 build and removed Debug
  CRT dependencies.
- Fixed module self-location so the fork no longer resolves original
  `x264guiEx.auo2` resources/configuration.
- Isolated binary/INI/project configuration identities and the last-output
  profile name.
- Fixed zero-initialized project-save serialization.
- Fixed the settings crash caused by placing an overlong identity in the
  32-byte legacy `CONF_GUIEX_HEADER::conf_name` field.
- Formalized the binary/display names as `x264guiEx-FreeRenderFPS.auo2` and
  `x264guiEx FreeRenderFPS`.

### Added

- Free Render FPS UI tab with common rational FPS presets and Custom rate/scale.
- Rational same-duration scheduler and generation-tagged render request context.
- Guarded AviUtl2 v2.1.4 x64 timeline-input detour.
- Encapsulated neighbor-frame cache workaround.
- License-safe install/upgrade/uninstall/rollback console installer.
- Reproducible Release package script, setup/portable ZIPs, hashes, runtime
  audit, third-party notices, trilingual documentation, and validation matrix.

### Validated

- Release x64 build: 0 errors, 7 existing warnings.
- Plugin project-config callback save/load round trip.
- Host settings open/close and project save/reopen/resave recovery checks.
- Installer filesystem lifecycle, backup/rollback, and original x264guiEx
  non-modification in an isolated sandbox.
- Retained integrated outputs for true 30→60 and 66→60 evaluation.

### Release blockers

- Exact redistributable x264/FFmpeg/mux/audio runtime package is not cleared.
- Clean-package encoding is therefore incomplete.
- 66→59.94, 66→24, 66→120, and 120→60 have not been revalidated on RC1.
- FreeFPS-disabled output and full side-by-side encoding need final regression.

## [0.1.0-alpha]

Initial research integration that proved AviUtl2 v2.1.4 can evaluate genuine
non-integer timeline coordinates and that x264guiEx can encode target-rate
outputs. Its Debug binary and generic configuration identity are preserved only
as recovery evidence and must not be distributed.
