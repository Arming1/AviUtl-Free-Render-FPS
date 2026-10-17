# AviUtl 1.x package notes

## v1.0.1 changes

- Release builds no longer write the per-frame diagnostic log
  `freerenderfps_aviutl1_probe.log`. The trace is compiled only into local
  diagnostic builds.
- While FreeRenderFPS is active, the host's copy-frame flag is ignored, so a
  previous image is never resent in place of a new frame.
- Thread suspension around hook installation and removal is safer. Thread IDs
  are collected before any thread is suspended, and a patch is refused (after
  retries) while a thread is executing inside the bytes being rewritten. If the
  hook cannot be restored safely, the pass-through detour stays installed.
- Cache-eviction renders are reused, so 30 → 60 needs one render per output
  frame instead of 1.5.
- The package contains no default `.conf`.

The package is a manual-install ZIP because legacy AviUtl has no supported
portable installer contract comparable to AviUtl2. Extract it over an AviUtl
1.10 installation with ExEdit 0.92.

The formal plugin files are:

```text
Plugins\x264guiEx-FreeRenderFPS.auo
Plugins\x264guiEx-FreeRenderFPS.ini
Plugins\x264guiEx-FreeRenderFPS.en.ini
Plugins\x264guiEx-FreeRenderFPS.zh.ini
Plugins\x264guiEx-FreeRenderFPS_stg\...
Plugins\exe_files\...
```

The old `x264guiEx-FreeFPS.auo` name must not be used for this build. AviUtl
loads the matching settings file by replacing the `.auo` extension with `.ini`,
so the basename is part of the installation contract. Existing user `.conf`
files must be preserved; the package deliberately contains no default `.conf`.

The Win32 module accepts the formal same-basename INIs and retains the generic
`x264guiEx.ini` fallback for recovery from an older x264guiEx installation. The
same-basename arrangement is preferred.

The host adapter validates the official AviUtl 1.10 / ExEdit 0.92 image size,
entry point, timestamp, file size, and SHA-256 before installing its guarded
detours. Other legacy binaries are rejected rather than patched speculatively.
