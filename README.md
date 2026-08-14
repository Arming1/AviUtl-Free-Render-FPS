# AviUtl2 FreeRenderFPS

[简体中文](README.zh-CN.md) | [日本語](README.ja.md) | [English](README.en.md)

FreeRenderFPS is an x264guiEx-based AviUtl2 output plugin that evaluates the
real scene timeline at an output FPS independent from the project FPS. A 30 FPS
project can therefore render genuine 60 FPS states at coordinates 0.0, 0.5,
1.0, 1.5… It does not copy frames or merely change FPS metadata.

## v1.0.0

Release gates pass on AviUtl2 v2.1.4 x64:

- FreeFPS OFF compatibility output passes.
- 30 → 60, 66 → 60, 66 → 59.94, 66 → 24, 66 → 120 and 120 → 60 pass.
- 30 → 60 and 66 → 60 each produce 300/300 unique decoded frames.
- Setup and portable package-only clean-host H.264/AAC encodes pass.
- Runtime source/build provenance and exact release artifact hashes are locked.

See [the final report](FINAL_REPORT.md), [validation matrix](docs/VALIDATION_MATRIX.md),
and [installation guide](docs/INSTALL.md).

Only AviUtl2 v2.1.4 x64 is supported. Unknown host signatures fail closed.

Project code is under [LICENSE](LICENSE). Third-party notices and redistribution
details are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[docs/RUNTIME_DEPENDENCIES.md](docs/RUNTIME_DEPENDENCIES.md).
