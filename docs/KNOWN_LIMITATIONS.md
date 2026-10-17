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
- The 1.1.0 AviUtl2 binary has not yet been validated inside AviUtl2 v2.1.12
  (Free FPS Export) or v2.1.4 (built-in hook); the AviUtl 1.x package is still
  1.0.1 and has not been re-validated. The measured matrices are from 1.0.0.
