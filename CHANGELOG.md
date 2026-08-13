# Changelog

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
