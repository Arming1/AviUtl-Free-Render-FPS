# AviUtl 1.x FreeRenderFPS v1.0.1

This is the Win32 build for AviUtl 1.10 with ExEdit 0.92. It reuses the
x264guiEx encoding, audio, muxing, configuration, and preset paths while
injecting the requested sub-frame time at the ExEdit timeline-evaluation
boundary. It is not frame duplication, frame dropping, metadata-only FPS
rewriting, or post-render transcoding.

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

## Installation

1. Exit AviUtl.
2. Extract `AviUtl1-FreeRenderFPS-v1.0.1-portable.zip` into the AviUtl 1.10
   root and preserve the included `Plugins` layout.
3. Keep `x264guiEx-FreeRenderFPS.auo` and its three matching `.ini` files
   together in `Plugins`.
4. The matching x264, FFmpeg, and L-SMASH runtimes are under
   `Plugins\exe_files`.
5. Restart AviUtl, select `x264guiEx FreeRenderFPS`, open `Free Render FPS`,
   and choose the target FPS.

Do not rename the `.auo` to the old `x264guiEx-FreeFPS.auo`, and do not leave a
second stale copy of the same plugin loaded. The `.conf` file is user state and
is intentionally absent from a clean package.

## Validation

The actual `30 → 60 FPS` output contains 300 video frames at 60/1 and lasts
5.000000 seconds. Audio is stereo AAC at 44.1 kHz with 217 frames and the same
5.000000-second duration. Decoded frame hashes contain 299 unique frames; the
only adjacent duplicate is at the project-end boundary. The continuously
moving portion is no longer emitted as 30 FPS pairs. See
`Docs\LEGACY_AVIUTL1_VALIDATION.md` for the measurements.

The 30 → 60 measurement above was made with v1.0.0. The v1.0.1 binary still
needs re-validation on AviUtl 1.10 + ExEdit 0.92.

## Support scope

The plugin is an x86 AviUtl 1.x output plugin and is deliberately pinned to the
official AviUtl 1.10 and ExEdit 0.92 images. The external encoders and muxers
run as child processes; x64 Windows can use the bundled x64 runtime binaries.
The AviUtl host itself is not included.

See `Docs\KNOWN_LIMITATIONS.md` and `LEGACY_AVIUTL1_SETUP.md` for details.
