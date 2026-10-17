# FreeRenderFPS for AviUtl2 and AviUtl 1.x

[简体中文](README.zh-CN.md) | [日本語](README.ja.md) | [English](README.en.md)

FreeRenderFPS is an x264guiEx-based output plugin with AviUtl2 and legacy
AviUtl 1.x host adapters. It evaluates the real scene timeline at an output FPS
independent from the project FPS. A 30 FPS project can therefore render genuine
60 FPS states at coordinates 0.0, 0.5, 1.0, 1.5… It does not copy frames or
merely change FPS metadata.

## v1.0.1

v1.0.1 is a maintenance release; see [CHANGELOG.md](CHANGELOG.md).

- AviUtl 1.x: release builds no longer write the per-frame diagnostic log
  `freerenderfps_aviutl1_probe.log`.
- AviUtl 1.x: the host copy-frame flag is ignored while FreeRenderFPS is
  active, so the previous image is never resent.
- Both hosts: hook installation and removal suspend threads safely.
- A second concurrent export is rejected without unhooking the active one.
- Cache-eviction renders are reused: 30 → 60 needs one render per output frame
  (previously 1.5).
- The AviUtl 1.x package no longer contains a default `.conf`.

The release gates and measurements below are v1.0.0 results. The 1.0.1 binaries
still need in-host re-validation on AviUtl2 v2.1.4 and AviUtl 1.10 + ExEdit 0.92.

Release gates passed by v1.0.0 on AviUtl2 v2.1.4 x64:

- FreeFPS OFF compatibility output passes.
- 30 → 60, 66 → 60, 66 → 59.94, 66 → 24, 66 → 120 and 120 → 60 pass.
- 30 → 60 and 66 → 60 each produce 300/300 unique decoded frames.
- Setup and portable package-only clean-host H.264/AAC encodes pass.
- Runtime source/build provenance and exact release artifact hashes are locked.

See [the final report](FINAL_REPORT.md), [validation matrix](docs/VALIDATION_MATRIX.md),
and [installation guide](docs/INSTALL.md).

The AviUtl2 build is the single plugin `x264guiEx-FreeRenderFPS.aux2`. It
provides the x264guiEx FreeRenderFPS output plugin and the Edit menu item
**Free FPS Export...**, which gives free render FPS on AviUtl2 v2.1.12. The
built-in FreeRenderFPS hook supports only AviUtl2 v2.1.4 x64; unknown host
signatures fail closed. Upgrade notes (old `.auo2`, `free_fps_export.aux2`, and
backup folders) are in [docs/INSTALL.md](docs/INSTALL.md).

The AviUtl 1.x adapter supports official AviUtl 1.10 + ExEdit 0.92 only and is
distributed as an x86 `.auo` manual-install package at
`dist\AviUtl1-FreeRenderFPS-v1.0.1`. Its current direct media validation covers
30 → 60 with real continuous-motion sub-frame evaluation and preserved audio
duration; the remaining legacy FPS matrix cases are explicitly pending.

Project code is under [LICENSE](LICENSE). Third-party notices and redistribution
details are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[docs/RUNTIME_DEPENDENCIES.md](docs/RUNTIME_DEPENDENCIES.md).
