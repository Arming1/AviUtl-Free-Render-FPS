# Third-party reproducibility and release hash policy

This project separates source/build provenance from a particular release artifact.

## Provenance lock

`third_party/sources.lock.json` fixes the official source archive or commit, archive hashes and signature, exact patch hashes, toolchain package versions, build options, license mode, and reproducibility controls. A provenance-valid build is legitimate even when an upstream toolchain embeds a build timestamp or path.

## Release artifact lock

`third_party/manifests/THIRD_PARTY_RELEASE_ARTIFACTS.json` records the exact byte length and SHA-256 of every binary selected for one release candidate. Packaging and installation verify this manifest. These hashes describe the selected artifacts; they do not claim all future builds will produce the same bytes.

## Investigation result

- x264 became byte-for-byte reproducible across `D:\frfps-repro-a` and `D:\frfps-repro-b`. Both clean builds produced SHA-256 `52102BAF1A286FAFA2538D23CF409705114F85A340FFC91D2FB4AB49345FD30A`.
- FFmpeg retains toolchain/link nondeterminism across clean roots even after a fixed install prefix, `SOURCE_DATE_EPOCH`, path mapping, deterministic archives, and a fixed PE timestamp. It is provenance-locked and its selected release artifact is hash-locked, but it is not labelled byte-reproducible.
- L-SMASH CLI sources use `__DATE__` and `__TIME__` in `cli/muxer.c`, `cli/remuxer.c`, and `cli/timelineeditor.c`. The upstream Visual Studio CLIRelease projects also force debug information. `/Brepro` alone cannot remove either source-level build time or project-controlled PDB data. These binaries are provenance-locked and release-artifact hash-locked, not labelled byte-reproducible.

No hash is normalized or ignored during packaging. The separation only prevents a previous candidate's artifact hashes from being mistaken for a universal build-reproducibility promise.

## RC2a audio-input correction

The first RC2 candidate exposed `file` and `pipe`, but FFmpeg 8.1.2 resolves x264guiEx's stdin spelling (`-i -`) through the `fd:` protocol. RC2a therefore enables exactly `file,pipe,fd`. A one-second 48 kHz stereo WAV supplied through redirected standard input encoded 48 AAC packets and exited successfully. This changes the selected FFmpeg artifact hash only; the source archive, LGPL mode, native AAC implementation, and prohibition on GPL/nonfree components are unchanged.
