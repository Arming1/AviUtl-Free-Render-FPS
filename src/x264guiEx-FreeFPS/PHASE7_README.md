# x264guiEx-FreeFPS Phase 7 MVP

This directory is an experimental fork of the unmodified
`reference/x264guiEx`. It reuses x264guiEx's encoder, audio, mux, configuration,
logging, and error handling and changes only how video samples are scheduled and
obtained from AviUtl2.

Build (x64 Debug):

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  '.\x264guiEx.sln' /m /p:Configuration=Debug /p:Platform=x64
```

Output: `Debug/x64/x264guiEx-FreeFPS.auo2`.

Usage on the tested host:

1. Install side-by-side with the original x264guiEx.
2. Choose `x264guiEx-FreeFPS (Phase 7 experimental)` as the output plugin.
3. Open x264 settings, select the `扩展` tab, enable Free Render FPS, and set
   a rational target rate such as `60 / 1` or `60000 / 1001`.
4. Export normally.

The hook will refuse unknown AviUtl2 builds. Disabled mode follows the original
x264guiEx path.

This is not production-ready. It supports only the validated AviUtl2 v2.1.4
x64 executable and uses neighbor-frame eviction as a temporary cache bypass.
AFS, keyframe pre-scan, and AUO timecode output are disabled while FreeFPS is
active. See `docs/x264guiex_freefps_integration.md` for evidence and limits.

