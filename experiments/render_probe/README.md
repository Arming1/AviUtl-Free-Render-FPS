# AviUtl2 render probe

This Phase 1 output plugin records the `OUTPUT_INFO` frame rate and frame count,
then requests a small set of valid, unique frame indices in non-monotonic order.
It does not encode video, interpolate images, duplicate frames, or change project
settings.

The request order is `0`, last frame, midpoint, then frame `1`, with invalid or
duplicate candidates omitted, followed by one diagnostic repeat of the last-frame
request (or the first request if the range has only one frame). Each request logs
its integer frame index, implied timestamp (`frame * scale / rate`), call order,
returned buffer address, a full-frame sample hash, callback duration, and whether
`func_get_video` returned a non-null buffer.
The repeated frame is never encoded or presented as a higher-FPS result.

## Build

From a Visual Studio x64 developer prompt:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The output is `build/render_probe.auo2`.

## Run

1. Copy `render_probe.auo2` to an AviUtl2 plugin directory or drag it into the
   AviUtl2 preview to install it.
2. Open a project with visible time-dependent motion.
3. Choose the Render Pipeline Probe output plugin and save a `.log` file.
4. Inspect the log for `video_request_begin` / `video_request_end` events.

The probe can establish how the public callback behaves for integer indices. It
cannot request a fractional frame or timestamp because `func_get_video` has no
such parameter.
