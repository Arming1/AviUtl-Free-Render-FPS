# Building FreeRenderFPS v1.0.1

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
src\x264guiEx-FreeFPS\Release\x64\x264guiEx-FreeRenderFPS.aux2
src\x264guiEx-FreeFPS\Release\x64\x264guiEx-FreeRenderFPS.ini
src\x264guiEx-FreeFPS\Release\x64\x264guiEx-FreeRenderFPS.en.ini
src\x264guiEx-FreeFPS\Release\x64\x264guiEx-FreeRenderFPS.zh.ini
```

The single `.aux2` is a general AviUtl2 plugin. It registers the x264guiEx
FreeRenderFPS output plugin (`register_output_plugin`) and the Edit menu item
**Free FPS Export...**. The x64 build no longer produces `.auo2`; the Win32
AviUtl 1.x `.auo` is unchanged.

The recovered RC1 build (its `.auo2` output, before the single-`.aux2` format)
completed with 0 errors and 7 existing upstream warnings. Its plugin SHA-256 is:

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
2. stages only the formal AUX2, adjacent INIs, presets, and notices;
3. rejects EXE/DLL/PDB/Debug/probe files and any `.auo2` from the payload; a
   `.aux2` is accepted only directly under the plugin folder;
4. compiles the installer with Visual Studio Roslyn;
5. runs install → upgrade → uninstall → rollback in an isolated temporary tree,
   including the legacy `.auo2` migration (the file is kept in the backup and
   restored by rollback);
6. creates setup and portable ZIPs;
7. writes SHA-256 manifests.

The installer EXE is intentionally accompanied by a `payload` directory inside
the setup ZIP. Extract the whole ZIP before running it.

### AviUtl 1.x package

Build the AviUtl2 package first (it produces the third-party sources ZIP that
both packages share), then run:

    .\tools\package_aviutl1_release.ps1

It rebuilds the Release Win32 plugin, rejects a version mismatch, probe-log
code, debug CRT imports, unaudited binaries, and any `.conf`, then writes
`dist\AviUtl1-FreeRenderFPS-<label>` with a portable ZIP, an expanded `payload`
and SHA-256 manifests. It refuses to overwrite an existing release folder.

## Third-party runtime build

Run `tools/build_third_party.ps1` against the pinned MSYS2 and Visual Studio
toolchains. It verifies official source archives, applies the recorded patches,
builds x264/FFmpeg/L-SMASH, and validates the selected release hashes. The
minimal LGPL FFmpeg configuration includes `file`, `pipe`, and `fd`; `fd` is
required because FFmpeg 8.1.2 resolves x264guiEx's `-i -` input through it.

Source/build provenance is locked independently from a specific release
artifact hash. Only x264 currently passes cross-root byte-for-byte comparison;
FFmpeg and L-SMASH remain artifact-hash locked without a false reproducibility
claim. See `THIRD_PARTY_REPRODUCIBILITY.md`.

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

The v1.0.0 identity, corrected portable layout, installer lifecycle, and final
setup/portable package-only smoke encodes have been validated. v1.0.1 packages
are produced by the same scripts; their in-host validation is pending.
