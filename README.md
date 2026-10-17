# FreeRenderFPS for AviUtl2 and AviUtl 1.x

[简体中文](README.zh-CN.md) | [日本語](README.ja.md) | [English](README.en.md)

FreeRenderFPS is an x264guiEx-based output plugin with AviUtl2 and legacy
AviUtl 1.x host adapters. It evaluates the real scene timeline at an output FPS
independent from the project FPS. A 30 FPS project can therefore render genuine
60 FPS states at coordinates 0.0, 0.5, 1.0, 1.5… It does not copy frames or
merely change FPS metadata.

## v1.2.0

v1.2.0 keeps the v1.1.0 single-plugin layout and adds Precise motion; see
[CHANGELOG.md](CHANGELOG.md).

- Adds **Precise motion** (exact speed for linear motion) to Free FPS Export. The
  checkbox is on by default.
- `x264guiEx-FreeRenderFPS.aux2` contains the output plugin and Edit >
  **Free FPS Export...**, which re-times the scene through the public AviUtl2
  SDK. Any rational output FPS works on AviUtl2 v2.1.12.
- The built-in Enable Free Render FPS hook still supports AviUtl2 v2.1.4 x64
  only. Other hosts get a message that points to Free FPS Export.
- The installer migrates legacy `.auo2` installs to `.aux2` and keeps backups
  outside `Plugin`. `--cleanup-legacy-backups` moves old backup folders out of
  `Plugin` (opt-in; it never deletes).
- Adds the `.aup2` retimer tool, host-free tests and validation tools.

The release gates and measurements below are v1.0.0 results. Free FPS Export was
checked in-host on AviUtl2 v2.1.12 on 2026-10-17: 8 conversions with Precise motion
enabled (frame counts equal the expected values, no duplicate or reversed frames,
motion residual 0.12–0.23 px against a 1.0 px limit), and a 30-minute A/V sync run
at 66, 50 and 60000/1001 fps (all PASS; maximum offset 0.00 ms at 66 and 50 fps,
±8.33 ms, half a frame, at 60000/1001). The tested binary was
`x264guiEx-FreeRenderFPS.aux2` (version resource 1.1.0, SHA256 114C3070…6076B,
repacked 2026-10-17 17:52, contains Precise motion); the first 1.1.0 build did
not contain precise mode. That build's contents are the 1.2.0 feature set; only the
version number was not bumped. The v1.2.0 installer packages have not been rebuilt
yet. The built-in hook on AviUtl2 v2.1.4 still needs in-host validation. The AviUtl 1.x package is still v1.0.1 and needs
re-validation on AviUtl 1.10 + ExEdit 0.92.

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
