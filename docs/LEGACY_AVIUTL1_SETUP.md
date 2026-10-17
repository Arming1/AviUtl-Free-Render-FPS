# AviUtl 1.x recovery build

This note applies to the Win32 build in
`src/x264guiEx-FreeFPS/x264guiEx/Release/Win32`.

## Settings error recovery

Use the four files with the exact matching basename:

```text
x264guiEx-FreeRenderFPS.auo
x264guiEx-FreeRenderFPS.ini
x264guiEx-FreeRenderFPS.en.ini
x264guiEx-FreeRenderFPS.zh.ini
```

Place them beside the existing AviUtl/x264guiEx files, then restart AviUtl.
Do not rename the `.auo` to `x264guiEx-FreeFPS.auo`; AviUtl 1.x loads the
companion INI by replacing the plugin extension with `.ini`. The old
`x264guiEx-FreeFPS.auo` shown in the recovery report is not the current
FreeRenderFPS build and must not be loaded alongside this one.

For compatibility with an existing legacy x264guiEx installation, the plugin
also accepts a generic `x264guiEx.ini` when its same-basename INI is absent.
The same-basename file remains the preferred and supported arrangement.

## Validation status

The Win32 module is an x86 AviUtl 1.x output plugin and its configuration
callback has been exercised without AviUtl: both size discovery and buffer
serialization pass, including the generic-INI fallback. The Win64 build
continues to produce the AviUtl2 `.auo2` module.

The AviUtl 1.x host adapter is version-pinned to the official AviUtl 1.10 /
ExEdit 0.92 images. It keeps a guarded render-preparation detour that counts
evaluations (so a cache-bypassed request fails instead of emitting a stale
image) and injects the requested fractional frame at ExEdit's filter-dispatch
boundary. ExEdit stores that value as a centiframe remainder in
`FilterProcInfo + 0x114` (0..99), so the legacy path has a maximum quantization
error of 0.01 source frame. This is real scene/timeline evaluation at the
requested output time; it is not frame duplication, frame dropping, metadata
rewriting, or post-render transcoding.

Release builds do not write a diagnostic log. The per-request trace
`freerenderfps_aviutl1_probe.log` (written to AviUtl's working directory) is
compiled in only when `FREEFPS_AVIUTL1_PROBE_LOG=1` is defined for a local
diagnostic build; such a build must not be distributed. Builds before this
change wrote that log on every request.

While FreeRenderFPS is active, the host's copy-frame flag is ignored: the
encoder loop index is an output sample, not a project frame, and every sample
is freshly evaluated.

The output video must still be rendered with this rebuilt `.auo` before
claiming parity. The
supported host combination is specifically AviUtl 1.10 + ExEdit 0.92; other
legacy binaries are rejected by image layout, file size, and SHA-256 checks.
