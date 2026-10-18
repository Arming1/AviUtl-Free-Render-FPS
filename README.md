<h1 align="center">FreeRenderFPS for AviUtl2 and AviUtl 1.x</h1>

<p align="center">
  <a href="README.zh-CN.md">简体中文</a> |
  <a href="README.ja.md">日本語</a> |
  <a href="README.en.md">English</a>
</p>

> **AI-assisted development notice:** This project was developed with AI assistance.
> The code, documentation and test tools were mainly written by AI (OpenAI Codex in
> the early stages, then Anthropic Claude / Claude Code). The author planned and
> reviewed the work and tested it on real AviUtl2 installations.

FreeRenderFPS is an x264guiEx-based output plugin that re-evaluates the real scene at output-FPS time points independent of the project FPS, so a 30 FPS project can render genuine 60 FPS states. It does not copy frames or only change FPS metadata.

## v1.3.0 highlights

- `直線移動(時間制御)` (linear move, time control) works on any track; `補間移動(時間制御)` is accepted.
- Time-control edit curves (the `|` payload) and curved 位置 are kept unchanged.
- Converting large projects is much faster (a 35 MB project in about 3 seconds).
- GitHub Actions builds and tests every push and shows in-host measurement results.
- Very low target rates (for example 66 → 10) are refused when short objects would overlap. Not supported yet.

## Features

- Sub-scene rate: a sub-scene whose source rate is lower than the exported scene's keeps its own rate.
- Time control (時間制御(オブジェクト)) 位置 and `コマ落ち` are recomputed exactly in seconds.
- Precise motion (exact linear-motion speed) is on by default and its checkbox is hidden. Easing tracks `イージング（通常）@イージング` are accepted.

## Download and install

- **Recommended:** `AviUtl2-FreeRenderFPS-v1.3.0-setup.zip`. Extract all of it, run `AviUtl2-FreeRenderFPS-v1.3.0-setup.exe` beside its `payload` directory, then restart AviUtl2. Manual: `AviUtl2-FreeRenderFPS-v1.3.0-portable.zip`, extracted over the AviUtl2 root. See [docs/INSTALL.md](docs/INSTALL.md).

## Usage

In AviUtl2 v2.1.12, save the project, then choose **Edit > Free FPS Export...**. Set **Target FPS** (59.94 is 60000/1001), **Sub-scene frame rate** (Auto by default), and **Encoder settings...** for bitrate, preset, audio and container.

## Support

- **AviUtl2 v2.1.12:** Edit > Free FPS Export... gives free render FPS. The built-in hook is AviUtl2 v2.1.4 x64 only.
- **AviUtl 1.x:** x86 `.auo` package v1.0.1 for AviUtl 1.10 + ExEdit 0.92 only.

## Validation

- AviUtl2 v2.1.12, Free FPS Export: 8 in-host conversions with Precise motion (residual 0.12–0.23 px). 30-minute A/V sync at 66, 50 and 60000/1001 fps: all PASS (maximum offset 0.00–8.33 ms).
- AviUtl2 v2.1.4 x64 (v1.0.0 gates): 30 → 60, 66 → 60, 66 → 59.94, 66 → 24, 66 → 120 and 120 → 60 all PASS. AviUtl 1.x: 30 → 60 continuous motion only. Details: [docs/VALIDATION_MATRIX.md](docs/VALIDATION_MATRIX.md) and [GitHub Actions](https://github.com/Arming1/AviUtl-Free-Render-FPS/actions).

## Known limitations

Known limits (the last frame of a low-rate staircase or two-point line; 3-decimal 位置) are in [docs/KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md). Conversion rules are in [docs/RETIME_RULES.md](docs/RETIME_RULES.md). History: [CHANGELOG.md](CHANGELOG.md).

## License

Project code: [LICENSE](LICENSE). Third-party notices: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [docs/RUNTIME_DEPENDENCIES.md](docs/RUNTIME_DEPENDENCIES.md). Reuses [x264guiEx by rigaya](https://github.com/rigaya/x264guiEx).
