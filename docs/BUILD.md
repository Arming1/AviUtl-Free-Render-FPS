# Building AviUtl2 FreeRenderFPS v1.0.0-rc1

## Supported build target

- Windows x64
- AviUtl2 SDK target (`AVIUTL_TARGET_VER=2`)
- Visual Studio Desktop development with C++
- MSVC v143-compatible toolset
- .NET Framework 4.7.2 reference assemblies for the C++/CLI settings UI

The validated host for the private hook is AviUtl2 v2.1.4 x64 only.

## Plugin build

From the repository root:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  '.\src\x264guiEx-FreeFPS\x264guiEx.sln' `
  /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m:1 /nodeReuse:false
```

Output:

```text
src\x264guiEx-FreeFPS\Release\x64\x264guiEx-FreeRenderFPS.auo2
src\x264guiEx-FreeFPS\Release\x64\x264guiEx-FreeRenderFPS.ini
src\x264guiEx-FreeFPS\Release\x64\x264guiEx-FreeRenderFPS.en.ini
src\x264guiEx-FreeFPS\Release\x64\x264guiEx-FreeRenderFPS.zh.ini
```

The recovered RC1 build completed with 0 errors and 7 existing upstream
warnings. Its plugin SHA-256 is:

```text
66266D5D08DF368D067B3FF74C0815965BD5177E2FF9C4E04181D053EAFD5DD5
```

`dumpbin /dependents` must show Release runtime names such as `MSVCP140.dll`,
`VCRUNTIME140.dll`, and `VCRUNTIME140_1.dll`; any `ucrtbased.dll`,
`VCRUNTIME140D.dll`, or `MSVCP140D.dll` is a release blocker.

## Package build

```powershell
.\tools\package_release.ps1 -RunSelfTest
```

The script:

1. performs a clean Release x64 plugin build;
2. stages only the formal AUO2, adjacent INIs, presets, and notices;
3. rejects EXE/DLL/PDB/Debug/probe files from the payload;
4. compiles the installer with Visual Studio Roslyn;
5. runs install → upgrade → uninstall → rollback in an isolated temporary tree;
6. creates setup and portable ZIPs;
7. writes SHA-256 manifests.

The installer EXE is intentionally accompanied by a `payload` directory inside
the setup ZIP. Extract the whole ZIP before running it.

## Missing runtime policy

The packaging script deliberately excludes the currently staged x264, FFmpeg,
muxer, external audio, L-SMASH-Works, and helper DLL binaries. Their precise
source/provenance/notice bundle is incomplete. A formal v1.0.0 package must
either include a checksum-pinned, source-complete and license-compliant runtime
set, or implement verified acquisition from official upstream sources.

See [RUNTIME_DEPENDENCIES.md](RUNTIME_DEPENDENCIES.md).

## Required release checks

- settings open/close with the Release plugin;
- project-config callback round trip;
- clean package install and launch;
- FreeFPS disabled export;
- the complete FPS matrix in [VALIDATION_MATRIX.md](VALIDATION_MATRIX.md);
- no Debug/probe binaries in `dist`;
- installer upgrade/uninstall/rollback and original x264guiEx coexistence;
- SHA-256 values regenerated after the last source change.

Do not rename RC artifacts to v1.0.0 until every release blocker is closed.
