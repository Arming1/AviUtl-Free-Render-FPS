# Third-party runtime audit for v1.0.0-rc2

Date: 2026-08-13

## Release runtime decision

RC2 embeds only five binaries rebuilt from locked official source. It does not
copy anything from ProgramData, the historical developer `exe_files`, RC1, or
the original x264guiEx installation. Unknown helpers, L-SMASH-Works, QAAC,
FDK-AAC, MKVToolNix and other optional tools remain excluded.

| Component | Exact source | License | Release build | SHA-256 |
|---|---|---|---|---|
| x264 | VideoLAN commit `0480cb05fa188d37ae87e8f4fd8f1aea3711f7ee` | GPL-2.0-or-later | GCC 16.2, static CLI, all bit depths/chroma, L-SMASH 2.16.1 MP4 | `5C7698A2CA7CEC9BED5DCEEA4B5D5FFA681EDD86DEF4372B259EC6473FD8D1C5` |
| FFmpeg | official signed 8.1.2 archive | LGPL-2.1-or-later | minimal static native-AAC executable; GPL/nonfree/version3 disabled; no external libraries | `87C18AABF07D01D92CBA7E874D3BF4D82C8EED9969FEE65F63FA68757FC136A0` |
| L-SMASH muxer | commit `a3e0b47dd4f1bbb7980d1f92cac93e761925c65a` (rev1484) | ISC | MSVC v143 CLIRelease x64, static CRT | `8E1A9E815070D617D1BB4F74B3F877D1EC3D3652F27FD78EBBBEC646A7C9813B` |
| L-SMASH remuxer | same | ISC | same | `7051E2579C586CEF0358D3E014E7F33DD94B2FC935A89458EFFD9D55DE3AF778` |
| L-SMASH timeline editor | same | ISC | same | `FC2BF9A64377839DCEE04CE97ED37AE6D70AD318292B59D072EEB2B5509969CA` |

## Source verification

- FFmpeg archive SHA-256: `464BEB5E7BF0C311E68B45AE2F04E9CC2AF88851ABB4082231742A74D97B524C`.
- FFmpeg detached signature: good signature from fingerprint
  `FCF986EA15E6E293A5644F10B4322F04D67658D8`.
- L-SMASH official commit archive SHA-256:
  `11DCFE35160CE8B5D31372D8BF77EAB004E31C025F22D4783B4306419C178890`.
- x264 source bundle SHA-256:
  `2EE0208201A22E4637462C566B0FC853DA85DBDA77533B8B7389A7F38FB1DA04`,
  produced with `git archive` from the verified VideoLAN commit.
- All exact URLs, commits, patch hashes, toolchain versions and expected output
  hashes are machine-readable in `third_party/sources.lock.json`.

## FFmpeg hard gate

Runtime `ffmpeg.exe -buildconf` contains native AAC and the required WAV/PCM
path. It contains neither `--enable-gpl` nor `--enable-nonfree`; therefore this
specific executable is an LGPL-2.1-or-later configuration, not a GPL or
nonfree configuration.

## Command-line compatibility regression

The audited tools completed the x264guiEx command shape independently:

1. x264 encoded 60 raw I420 frames at 60/1 to MP4;
2. FFmpeg encoded a 48 kHz stereo WAV with native AAC using
   `-c:a aac -aac_coder twoloop`;
3. L-SMASH remuxer combined the video and audio outputs.

Final regression MP4 SHA-256:
`5642690CD703E3251D50F1B409974F60E063B42722DEA2156D73BC3169DE24B3`.

This proves tool-chain compatibility; it is not a substitute for the pending
clean AviUtl2 FPS matrix.
