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

FreeRenderFPS is currently available as a release candidate for AviUtl2 v2.1.4 x64.

The core project-FPS / output-FPS decoupling feature is working and available for testing. Conversions such as 30 → 60 FPS and 66 → 60 FPS have passed previous integrated validation while preserving the original project duration.

Some additional FPS combinations are still being revalidated on the current RC build, so this release is not yet considered v1.0.0 stable.

The installer currently includes the plugin and required configuration files only. External encoders and muxing tools are not bundled and must be provided separately.

Bug reports and real-world testing are welcome.

For detailed validation and development status, see FINAL_REPORT.md and docs/VALIDATION_MATRIX.md.

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
