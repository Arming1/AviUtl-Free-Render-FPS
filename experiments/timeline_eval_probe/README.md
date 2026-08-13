# Timeline Eval Probe (no-op)

This experimental AviUtl2 video filter logs the public `FILTER_PROC_VIDEO`
callback's `SCENE_INFO` and `OBJECT_INFO` timing fields. It does not read,
replace, or modify image data and always returns success.

Its purpose is limited to Phase 2 runtime observation:

- correlate output-plugin integer frame requests with object-filter evaluation;
- record the actual integer `OBJECT_INFO::frame` and double
  `OBJECT_INFO::time` supplied by AviUtl2;
- record callback thread IDs and call order;
- distinguish "the output callback returned a buffer" from "the object's
  filter chain was actually invoked".

The log is written next to the loaded `.auf2` as `timeline_eval_probe.log`.
This probe is not an internal hook and does not claim subframe control.

## Build

From a Visual Studio x64 developer prompt:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The output is `build/timeline_eval_probe.auf2`.

A separate Phase 3 instrumentation build can enable
`-DPHASE3_DEBUG_BREAK=ON`. It executes `__debugbreak()` once, after logging the
first valid `OBJECT_INFO` address and before returning from the first
video-filter callback. This is intended only for attaching a debugger and
installing a hardware write breakpoint on `&OBJECT_INFO::time`; ordinary builds
leave the option off and never execute that breakpoint.

## Run

1. Keep exactly one copy of the probe in AviUtl2's plugin load path.
2. Add `FRFPS Timeline Eval Probe 7F3A9C42` to a visible object.
3. Move the timeline cursor or run the Render Pipeline Probe output.
4. Inspect `timeline_eval_probe.log` next to the loaded plugin.

During the Phase 2 run, leaving an older binary named
`timeline_eval_probe.auf2.disabled` in the plugin directory caused AviUtl2 to
report duplicate effect keys. Moving that file outside the load path and keeping
one `.auf2` resolved registration. A suffix after `.auf2` must therefore not be
relied on as a safe disable mechanism for this test environment.
