# Known limitations — v1.1.0

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
- Free FPS Export rounds each object's last frame to the new rate, so a linear
  motion ends at `e'/Rt` instead of `e/Rp`. The end-time deviation is bounded by
  `|1/Rp − 1/Rt| + half an output frame`. Measured: +21.65 ms (1.30 output frames)
  for 30→60000/1001, bound about 25.0 ms; −26.5 ms (0.64 output frames) for 66→24,
  bound about 47 ms. See `tools/aup2_fps_upconvert.md`, "Motion endpoints".
- Long-duration cumulative drift and audio-video sync are not yet reported. They
  are being checked with host-free tests and a 30-minute real-footage run.
- A speed-error target of ≤ 0.01% needs the `--precise` mode, which is being
  implemented and is not in the 1.1.0 build. It re-samples linear-move keyframe
  values from the original curve at the new times (plugin dialog default: on).
  Its end is extrapolated by at most about one output frame.
- The built-in FreeFPS hook (v2.1.4) has not been validated with the 1.1.0 binary.
  The AviUtl 1.x package is still 1.0.1 and has not been re-validated. The
  measured AviUtl 1.x matrices are from 1.0.0.
