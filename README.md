<h1 align="center">AviUtl2 FreeRenderFPS</h1>

<p align="center">
  <a href="README.zh-CN.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.en.md">English</a>
</p>

FreeRenderFPS is an x264guiEx-based AviUtl2 output plugin that samples the real
scene timeline at an output FPS independent of the project FPS. A 30 FPS
project can therefore be evaluated at 0.0, 0.5, 1.0, 1.5... project-frame
coordinates for a true 60 FPS output; it does not duplicate frames or merely
change container metadata.

## Release status

The current deliverable is **v1.0.0-rc1, not v1.0.0**. The recovered Release
x64 plugin builds and its settings/project-config crash is fixed. The
license-safe installer and portable package intentionally omit x264, FFmpeg,
muxers, and external audio tools because the exact staged binaries do not yet
have a complete, matching redistribution/source record. Several required FPS
matrix entries also have not been revalidated on the recovered RC binary.

Do not publish this candidate as v1.0.0. See [FINAL_REPORT.md](FINAL_REPORT.md)
and [the validation matrix](docs/VALIDATION_MATRIX.md).

## Installation

There is no production one-click download yet. For RC evaluation:

1. Download `AviUtl2-FreeRenderFPS-v1.0.0-rc1-setup.zip`.
2. Extract the complete ZIP.
3. Double-click `AviUtl2-FreeRenderFPS-v1.0.0-rc1-setup.exe`.
4. Restart AviUtl2 v2.1.4 x64.
5. Select `x264guiEx FreeRenderFPS`.

The RC installer installs only the plugin, formal INIs, presets, and notices.
Encoder/muxer tools must be supplied separately until a source-complete,
license-cleared runtime bundle is available.

Full language-specific instructions:

- [English](README.en.md)
- [简体中文](README.zh-CN.md)
- [日本語](README.ja.md)

## Evidence at a glance

| Conversion | Evidence | Status |
|---|---|---|
| 30 → 60 | 322/322 unique frames, 5.366667 s video/audio | PASS on the prior integrated build |
| 66 → 60 | 300/300 unique frames, 5.000000 s video/audio | PASS on the prior integrated build |
| 66 → 59.94, 66 → 24, 66 → 120, 120 → 60 | No retained recovered RC artifact | NOT REVALIDATED |

Supported host is currently limited to **AviUtl2 v2.1.4 x64**. The guarded
internal hook refuses unknown images/signatures.

## Credits and license

Based on [x264guiEx by rigaya](https://github.com/rigaya/x264guiEx). Project
code is provided under [LICENSE](LICENSE); third-party attribution and
redistribution status are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and [docs/RUNTIME_DEPENDENCIES.md](docs/RUNTIME_DEPENDENCIES.md).
