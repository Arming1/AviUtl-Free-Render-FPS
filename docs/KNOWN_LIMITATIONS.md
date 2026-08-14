# Known limitations — v1.0.0

- FreeFPS supports AviUtl2 v2.1.4 x64 only. Its guarded private hook rejects
  unknown host image/signature combinations.
- AviUtl2's observed output cache uses an integer frame key. FreeFPS currently
  uses a neighbor-frame eviction workaround, which costs performance and is not
  a general cache-key extension.
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
