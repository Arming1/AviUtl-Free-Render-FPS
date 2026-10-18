# Known limitations — v1.2.5

- Multi-scene projects (Free FPS Export, 1.2.5 rules; 1.2.1 refuses them): scenes may have
  different `video.rate` values. The exported scene (`display.scene` in `[project]`, default 0)
  always follows the target rate. In **Auto** (default), another scene follows the target rate
  when its source rate is at least the exported scene's source rate. A scene with a lower source
  rate keeps its own rate, so its hold steps stay visible (for example a 15 fps scene inside a
  30 fps project exported at 60 fps moves every 4 output frames). **Follow all** and **Keep all**
  are the other choices. Followed scenes are re-timed like the exported scene. Kept scenes are
  copied unchanged. Scene (`シーン`) objects keep their playback position (seconds) and playback
  speed (%) unchanged.
- `コマ落ち` (frame drop, 時間制御(オブジェクト)) is a number with −/+ buttons, not a switch. `コマ落ち=1` (no drop)
  is converted unchanged. In a time control effect of a followed scene, `コマ落ち=K` (K ≥ 2) becomes K × k (k = target
  rate / source rate) when that is an integer. Otherwise the block is written as a staircase (see the next item). K ≥ 2
  elsewhere, `コマ落ち=0` (a hand-typed `0` crashed the host in the user's test) and the trackbar-style form are refused.
  Measured in AviUtl2 (probes P9/P10 for the unit, Q4c for the origin, `test_kit/5_抽帧探针/`): K counts project frames,
  and each drop segment starts at the time control object's own start, not at timeline frame 0.
- Time control (時間制御(オブジェクト)) 位置 (a two-point 直線移動(時間制御) track) is recomputed exactly in seconds for
  followed scenes, with or without Precise motion. When the new end values fall outside 0..100, or a drop is not an
  integer multiple, the block is written as a `瞬間移動` staircase: one point per changed step, at most 4096 points, and
  the object's other trackbars must be constant. Measured in AviUtl2 (probes Q4a/Q4b/Q4c): the host clamps only the two
  end values of 位置 (not frame by frame), and in a `瞬間移動` track a middle point takes effect at its own frame while
  the **last point is never shown**. Limits of this rule:
  - With k < 1 (a lower target rate), the last frame of a staircase shows the previous step.
  - The two-point line form can show its last frame up to about one extra new frame (at most k − 1/2 new frames) when the
    time control reaches past its own end.
  - 位置 is rounded to 3 decimals (the decimal count of the original line), so each position has a quantisation of 0.001 %
    of the controlled range.
- Script easing `イージング（通常）@イージング` is accepted (its value depends only on the progress inside the object).
  The `|` payload (for example `27`) is kept byte for byte. Precise motion skips an object that has such a track and
  lists it in `precise.skipped`. `イージング（補間移動）@イージング` is still refused: it reads `obj.getpoint("time")`,
  which is not verified.
- `直線移動(時間制御)` (linear move, time control) is accepted on any effect track, not only 時間制御(オブジェクト)
  `位置`. Two points and multi-point tracks convert with values unchanged. The host ignores the times of the middle
  points and moves at a constant speed over the whole path, and the converter does the same (probe R1d: the maximum
  error is 0.19 px). Measured: R1a (`直線移動`) and R1b (`直線移動(時間制御)`, no curve) give identical frames (0.000 px).
- The `|` curve payload (for example `|1,0,1,0`, the normalised control points written by 時間制御編集) is kept byte for
  byte on `直線移動`, `直線移動(時間制御)` and `イージング（通常）@イージング` tracks. The payload is not checked. Other
  modes that carry a payload are still refused.
  - Curves are not re-sampled. The curve only depends on progress inside the object, so the converter keeps the
    payload as is.
  - Precise motion skips a `直線移動(時間制御)` track that has a payload or more than two points, and lists it in
    `precise.skipped`. A two-point `直線移動(時間制御)` with no payload is re-sampled, unless it needs an end hold: then
    the object is skipped, because this mode ignores the extra keyframes.
- 時間制御(オブジェクト) `位置` with a curve is not rewritten: the value and payload are kept and the object frames are
  converted. The position is object-relative and the error is within one frame. With a curve, `コマ落ち=K` (K ≥ 2) is kept
  only when K × k and the controlled seconds are integers. Otherwise the block is refused.
  `コマ落ち=0` is still refused, because a hand-typed `0` crashed the host.
- Still refused, not verified in AviUtl2: 補間移動, ランダム移動, 反復移動, 移動量指定 and other track modes that are not
  listed above.
- Not verified in AviUtl2: at which sub-scene frame a kept lower-rate scene is shown at a given
  main-scene time (rounded down or nearest). This only decides where the hold steps start in Keep
  mode. It does not change the frame count or the step size. The check is in
  `test_kit/4_次合成/` (see `测试清单.zh-CN.md` there and `experiments/subscene_rate/analyze_steps.py`).
- The built-in FreeFPS hook supports AviUtl2 v2.1.4 x64 only. Its guarded private hook rejects
  unknown host image/signature combinations.
- AviUtl2's observed output cache uses an integer frame key. FreeFPS uses a
  neighbor-frame eviction workaround. Since 1.0.1 the eviction render is reused
  when the next sample needs exactly that integer coordinate, so integer ratios
  such as 30 → 60 cost one render per output frame; other ratios (for example
  30 → 120 or 66 → 120) still pay part of an extra render. It is not a general
  cache-key extension.
- A one-frame project cannot use the neighbor eviction workaround.
- FreeFPS mode disables AFS, x264guiEx keyframe pre-scan, and timecode output.
- The request context permits one active render session and depends on the
  observed v2.1.4 output-to-timeline-builder sequence.
- FFmpeg and L-SMASH are source/build-provenance locked, but cross-root
  byte-for-byte reproducibility is not claimed. The exact release binaries are
  still SHA-256 locked.
- The installer is a minimal console EXE rather than a graphical wizard.
- Side-by-side use must keep the original x264guiEx and FreeRenderFPS in
  separate directories; the installer refuses the original x264guiEx target.
- The AviUtl 1.x adapter supports official AviUtl 1.10 + ExEdit 0.92 only. Its
  sub-frame precision is 0.01 project frame, and the last output sample may be
  clamped to the final project frame.
- Free FPS Export (AviUtl2 v2.1.12, 1.1.0) was run in AviUtl2 on 2026-10-17 for
  eight cases (60→66, 72→60, 30→60000/1001, 66→24, 66→120, 120→60, 60→50, 72→59).
  Frame counts, video/audio durations, adjacent near-duplicate frames and motion
  steps match the targets. The retimed motion check passes all eight, with
  residuals of 0.146–0.192 px against a 1.0 px limit. Results are in
  `experiments/free_fps_matrix/run_20261017/`.
- Without precise motion, Free FPS Export rounds each object's last frame to the new
  rate, so a linear motion ends at `e'/Rt` instead of `e/Rp`. The end-time deviation
  is bounded by `|1/Rp − 1/Rt| + half an output frame`. Measured: +21.65 ms (1.30
  output frames) for 30→60000/1001, bound about 25.0 ms; −26.5 ms (0.64 output
  frames) for 66→24, bound about 47 ms. See `tools/aup2_fps_upconvert.md`, "Motion
  endpoints".
- Long-duration timing: host-free tests pass for 108 cases (30 and 60 minutes × 6
  source rates × 9 targets). Boundary error is at most half an output frame, drift is
  not cumulative (maximum 0.008 frame), total duration relative error is at most
  0.000463 %, and objects that start on the same frame keep the same start frame.
  Audio-video sync over 30 minutes was run in AviUtl2 v2.1.12 on 2026-10-17 at 66, 50 and
  60000/1001 fps (all PASS; see `experiments/long_av_sync/run_20261017/`). The kit is in
  `experiments/long_av_sync/`.
- Precise motion (`--precise`) is in 1.2.0 and was run in
  AviUtl2 v2.1.12 on 2026-10-17 (8 conversions, `experiments/free_fps_matrix/run_20261017_precise/`).
  The first 1.1.0 build did not contain it. It re-samples the
  trackbar values of linear moves and constant tracks from the original curve, so
  the speed matches the original. Limits: other effects are unchanged and their
  objects are listed in `precise.skipped`. In 1.2.0 the last frame could end past the original
  endpoint (30→60000/1001 ended at 201.74, +1.74 px) or short of it (66→24 ended at
  197.87). 1.2.5 holds the end value after the original end instead of extrapolating
  (`tools/aup2_fps_upconvert.md`, "End hold"). The P6 export (66 → 132, end hold on) ran in AviUtl2 with a per-frame
  error of at most 0.27 px.
  In 1.2.5 the dialog hides the Precise motion checkbox and always uses it. The checkbox and the `[debug]` settings
  (`extra_decimals`, `keep_temp`) appear only with `free_fps_export.ini` next to the plugin DLL and `[debug] enabled=1`.
  Not verified in AviUtl2: extra decimals (`extra_decimals` in debug mode, or `--precise-extra-decimals` on the
  command line) and whether AviUtl2 rounds the values again. The C++ port refuses on 64-bit
  overflow; the Python tool does not. Plan: `experiments/free_fps_matrix/PRECISE_TEST_PLAN.zh-CN.md`.
- The built-in FreeFPS hook (v2.1.4) has not been validated with the 1.1.0 binary.
  The AviUtl 1.x package is still 1.0.1 and has not been re-validated. The
  measured AviUtl 1.x matrices are from 1.0.0.
- Free FPS Export error messages are still English, even though the dialog and the
  Free Render FPS tab are localized to Chinese, Japanese and English.
