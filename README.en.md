# AviUtl2 FreeRenderFPS

[简体中文](README.zh-CN.md) | [日本語](README.ja.md) | **English**

FreeRenderFPS is an AviUtl2 output plugin based on x264guiEx. It separates the
scene-evaluation sampling rate from the project FPS. For example, a 30 FPS
project rendered at 60 FPS is evaluated at 0.0, 0.5, 1.0, 1.5... project-frame
coordinates. It is not frame duplication and not an FPS-metadata trick.

## Download

**v1.0.0 release gates pass.** It bundles source-corresponding, exact-hash
audited x264, LGPL FFmpeg and ISC L-SMASH runtime. The full FPS matrix and clean
setup/portable package-only encodes pass.

## One-click installation

1. Extract the complete v1.0.0 setup ZIP.
2. Run its adjacent setup EXE.
3. Restart AviUtl2.
4. Select `x264guiEx FreeRenderFPS`.

The console installer defaults to
`%ProgramData%\aviutl2\Plugin\x264guiEx-FreeRenderFPS`, creates timestamped
backups for upgrades, supports uninstall/rollback, and refuses any destination
inside the original `x264guiEx` tree. Run it from a terminal with `--help` for
advanced options. The extracted `payload` directory must stay beside the EXE.

## Manual installation

Extract the v1.0.0 portable ZIP directly over the AviUtl2 root. It already
contains `data\Plugin\x264guiEx-FreeRenderFPS`. Keep the four formal adjacent
files together. Do not copy Debug, PDB, probe, watcher, or breakpoint artifacts.

## Usage

1. Open `x264guiEx FreeRenderFPS` settings.
2. Open the `Free Render FPS` tab.
3. Enable `Enable Free Render FPS`.
4. Choose the target FPS preset.
5. Export using the bundled audited runtime.

Turning Free Render FPS off follows the original x264guiEx integer-frame path.
FreeFPS mode changes the effective video FPS/frame count while retaining the
project duration and original audio sample count.

## FPS presets

23.976, 24, 25, 29.97, 30, 48, 50, 59.94, 60, 66, 72, 90, 120, and Custom.
The scheduler stores rational rate/scale values; 59.94 is 60000/1001.

## Tested conversions

| Project → target | Frames | Video/audio duration | Adjacent duplicates | Status |
|---|---:|---:|---:|---|
| 30 → 60 | 300 | 5.000000 / 5.000000 s | 0; 300 unique | PASS |
| 66 → 60 | 300 | 5.000000 / 5.000000 s | 0; 300 unique | PASS |
| 66 → 59.94 | 300 | 5.005000 / 5.000000 s | 0; 300 unique | PASS |
| 66 → 24 | 120 | 5.000000 / 5.000000 s | 0; 120 unique | PASS |
| 66 → 120 | 600 | 5.000000 / 5.000000 s | 0; 600 unique | PASS |
| 120 → 60 | 300 | 5.000000 / 5.000000 s | 0; 300 unique | PASS |

The 60-second 66→59.94 test produced 3597 frames and only 8.333 ms endpoint
delta, with no accumulated rational drift. See
[docs/VALIDATION_MATRIX.md](docs/VALIDATION_MATRIX.md).

## Supported AviUtl2

Only **AviUtl2 v2.1.4 x64**. FreeFPS validates the host image size, entry RVA,
timeline-builder bytes, and caller relationship before enabling. Unknown builds
are rejected; ordinary output remains available with FreeFPS disabled.

## Known limitations

- The private internal hook is version-specific.
- Integer host caches are handled with a neighbor-frame eviction workaround.
- FreeFPS mode disables AFS, x264guiEx keyframe pre-scan, and timecode output.
- FFmpeg and L-SMASH are provenance reproducible but are not falsely claimed
  byte-for-byte reproducible across different build roots.
- The installer is a minimal console EXE, not a graphical wizard.

See [docs/KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md).

## Build

Use Visual Studio with the Desktop C++ workload and .NET Framework 4.7.2
reference assemblies, then run:

```powershell
.\tools\package_release.ps1 -RunSelfTest
```

The script performs a Release x64 build, stages an allowlisted license-safe
payload, compiles the installer, runs its isolated lifecycle test, produces the
setup/portable ZIPs, and writes SHA-256 manifests. See
[docs/BUILD.md](docs/BUILD.md).

## Credits

FreeRenderFPS reuses [x264guiEx by rigaya](https://github.com/rigaya/x264guiEx)
for encoding, audio, mux, configuration, logging, and error-handling flows.

## License

See [LICENSE](LICENSE), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and
[docs/RUNTIME_DEPENDENCIES.md](docs/RUNTIME_DEPENDENCIES.md). The package
policy is fail-closed: an external binary with incomplete provenance is not
bundled.
