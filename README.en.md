# FreeRenderFPS for AviUtl2 and AviUtl 1.x

[简体中文](README.zh-CN.md) | [日本語](README.ja.md) | **English**

> **AI-assisted development notice:** This project was developed with AI assistance.
> The code, documentation and test tools were mainly written by AI (OpenAI Codex in
> the early stages, then Anthropic Claude / Claude Code). The author planned and
> reviewed the work and tested it on real AviUtl2 installations.

FreeRenderFPS provides host adapters for AviUtl2 and legacy AviUtl 1.x, based
on x264guiEx. It separates the scene-evaluation sampling rate from the project
FPS. For example, a 30 FPS project rendered at 60 FPS is evaluated at 0.0,
0.5, 1.0, 1.5... project-frame coordinates. It is not frame duplication and
not an FPS-metadata trick.

## v1.2.1

v1.2.1 is a patch release on v1.2.0; see [CHANGELOG.md](CHANGELOG.md).

- Free FPS Export has an **Encoder settings...** button that opens the x264guiEx
  settings window, and an encoder summary line. The container follows the output
  file extension.
- Free FPS Export encodes with the settings in the x264guiEx settings window. It
  only turns off the built-in Enable Free Render FPS switch for the export.
- The Free FPS Export dialog and the Free Render FPS tab support Chinese, Japanese
  and English. Error messages are still English.
- The Free Render FPS tab layout is fixed: the target FPS dropdown is no longer
  covered by its label.

### v1.2.0 features

v1.2.0 keeps the v1.1.0 single-plugin layout and adds Precise motion.

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
version number was not bumped. The built-in hook on AviUtl2 v2.1.4 still needs in-host validation. The AviUtl 1.x package is still v1.0.1 and needs
re-validation on AviUtl 1.10 + ExEdit 0.92.

## Download

**AviUtl2 v1.0.0 release gates pass.** It bundles source-corresponding, exact-hash
audited x264, LGPL FFmpeg and ISC L-SMASH runtime. The full FPS matrix and clean
setup/portable package-only encodes pass.

The legacy AviUtl 1.x package is a separate Win32 build for AviUtl 1.10 and
ExEdit 0.92. It is available at
`dist\AviUtl1-FreeRenderFPS-v1.0.1` and is distributed as a manual-install
portable ZIP; the AviUtl host itself is not included.

## One-click installation

1. Extract the complete `AviUtl2-FreeRenderFPS-v1.2.1-setup.zip`.
2. Run its adjacent `AviUtl2-FreeRenderFPS-v1.2.1-setup.exe`.
3. Restart AviUtl2.
4. Select `x264guiEx FreeRenderFPS`.

The console installer defaults to
`%ProgramData%\aviutl2\Plugin\x264guiEx-FreeRenderFPS`, creates timestamped
backups for upgrades, supports uninstall/rollback, and refuses any destination
inside the original `x264guiEx` tree. Run it from a terminal with `--help` for
advanced options. The extracted `payload` directory must stay beside the EXE.

## Manual installation

Extract `AviUtl2-FreeRenderFPS-v1.2.1-portable.zip` directly over the AviUtl2 root. It already
contains `data\Plugin\x264guiEx-FreeRenderFPS`. Keep
`x264guiEx-FreeRenderFPS.aux2` and its three adjacent INI files together. Do not
copy Debug, PDB, probe, watcher, or breakpoint artifacts.

### Legacy AviUtl 1.x (Win32)

Extract `AviUtl1-FreeRenderFPS-v1.0.1-portable.zip` over an AviUtl 1.10 root
with ExEdit 0.92. Keep `Plugins\x264guiEx-FreeRenderFPS.auo` and its three
same-basename INIs together. The package places the external runtime under
`Plugins\exe_files` and presets under `Plugins\x264guiEx-FreeRenderFPS_stg`.
Do not rename the AUO to the old `x264guiEx-FreeFPS.auo`, and do not load a
stale copy alongside it. See [docs/LEGACY_AVIUTL1_SETUP.md](docs/LEGACY_AVIUTL1_SETUP.md).

## Upgrading from v1.0.x

- The installer (install or upgrade) backs up the installed folder and removes
  the old `x264guiEx-FreeRenderFPS.auo2` from it; the copy stays in the
  timestamped backup. If you installed manually, delete the `.auo2` yourself.
  AviUtl2 must not load both `.auo2` and `.aux2`.
- Delete `free_fps_export.aux2` if you copied the experimental file earlier. It
  is superseded by Free FPS Export in the main plugin; the installer does not
  touch it.
- Installer backups now go to `C:\ProgramData\aviutl2\FreeRenderFPS-backups`,
  outside `Plugin`. Older `x264guiEx-FreeRenderFPS.backup-*` folders in
  `C:\ProgramData\aviutl2\Plugin` each hold a copy of the plugin binary (the
  older ones contain a `.auo2`). AviUtl2 reads plugins one folder below `Plugin`,
  so these copies can load as duplicates. Default install, upgrade and uninstall
  leave them alone. To move them into `FreeRenderFPS-backups` (nothing is
  deleted), run the installer with `--cleanup-legacy-backups`; add `--dry-run` to
  list them first.

## Usage

1. Open `x264guiEx FreeRenderFPS` settings.
2. Open the `Free Render FPS` tab.
3. Enable `Enable Free Render FPS`.
4. Choose the target FPS preset.
5. Export using the bundled audited runtime.

Turning Free Render FPS off follows the original x264guiEx integer-frame path.
FreeFPS mode changes the effective video FPS/frame count while retaining the
project duration and original audio sample count.

## Free FPS Export (AviUtl2 v2.1.12)

The same `x264guiEx-FreeRenderFPS.aux2` adds **Free FPS Export...** to the
AviUtl2 **Edit** menu. On AviUtl2 v2.1.12, use it for free render FPS: it
re-times the project with public SDK APIs and exports at any rational FPS. Save
the project to a file before exporting.

## FPS presets

23.976, 24, 25, 29.97, 30, 48, 50, 59.94, 60, 66, 72, 90, 120, and Custom.
The scheduler stores rational rate/scale values; 59.94 is 60000/1001.

## AviUtl2 tested conversions

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

### Legacy AviUtl 1.x validation

| Host/project → target | Frames | Video/audio duration | Decoded frame hashes | Status |
|---|---:|---:|---|---|
| AviUtl 1.10 + ExEdit 0.92, 30 → 60 | 300 | 5.000000 / 5.000000 s | 299 unique; one endpoint pair | PASS for continuous motion |

The endpoint pair is the final project-frame clamp in the five-second fixture;
the continuously moving portion is not emitted as 30 FPS pairs. The remaining
51.2 → 60, 51.2 → 60000/1001, and 66 → 60 cases still need fresh legacy-host
media before claiming the full AviUtl2 matrix for AviUtl 1.x. See
[docs/LEGACY_AVIUTL1_VALIDATION.md](docs/LEGACY_AVIUTL1_VALIDATION.md).

## Supported AviUtl2

The built-in hook supports only **AviUtl2 v2.1.4 x64**; on v2.1.12 use Free FPS
Export. FreeFPS validates the host image size, entry RVA,
timeline-builder bytes, and caller relationship before enabling. Unknown builds
are rejected; ordinary output remains available with FreeFPS disabled.

## Supported legacy AviUtl 1.x

The legacy adapter supports **AviUtl 1.10 + ExEdit 0.92** only. It is an x86
`.auo` output plugin with version-pinned, guarded ExEdit detours. The external
x264/FFmpeg/L-SMASH programs are separate child processes and may be x64 on a
64-bit Windows system. Other AviUtl or ExEdit binaries are rejected.

## Known limitations

- The private internal hook is version-specific.
- Integer host caches are handled with a neighbor-frame eviction workaround.
- FreeFPS mode disables AFS, x264guiEx keyframe pre-scan, and timecode output.
- FFmpeg and L-SMASH are provenance reproducible but are not falsely claimed
  byte-for-byte reproducible across different build roots.
- The installer is a minimal console EXE, not a graphical wizard.
- The legacy adapter quantizes ExEdit's sub-frame remainder to 0.01 source
  frame, uses the guarded integer-cache eviction workaround, and can clamp the
  final output sample to the final project frame.
- The legacy package is manually installed and currently has direct media
  validation for 30 → 60; the extended legacy matrix is still pending.

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

The legacy plugin can be rebuilt with the same project using `Release|Win32`:

```powershell
msbuild .\src\x264guiEx-FreeFPS\x264guiEx\x264guiEx.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=Win32
```

The packaged legacy output is kept under
`dist\AviUtl1-FreeRenderFPS-v1.0.1`.

## Credits

FreeRenderFPS reuses [x264guiEx by rigaya](https://github.com/rigaya/x264guiEx)
for encoding, audio, mux, configuration, logging, and error-handling flows.

## License

See [LICENSE](LICENSE), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and
[docs/RUNTIME_DEPENDENCIES.md](docs/RUNTIME_DEPENDENCIES.md). The package
policy is fail-closed: an external binary with incomplete provenance is not
bundled.
