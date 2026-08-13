# AviUtl2 FreeRenderFPS architecture

## Scope

FreeRenderFPS reuses x264guiEx for encoder process management, video format
conversion, audio extraction, muxing, presets, UI, logging, and error handling.
Its production-code changes are restricted mainly to video sampling,
configuration identity, and a guarded AviUtl2 v2.1.4 internal hook.

## Render path

```text
AviUtl2 OUTPUT_INFO
  -> FreeFpsSession (derive target rate/count)
  -> x264guiEx enc_out loop (target sample index)
  -> rational coordinate mapper
  -> optional neighbor-frame cache eviction
  -> generation-tagged request context
  -> func_get_video(integer public frame)
  -> guarded timeline-builder detour
  -> private double coordinate input
  -> AviUtl2 scene/object/effect evaluation
  -> converted pixel buffer
  -> original x264 stdin writer
```

For project rate/scale `Pr/Ps` and target rate/scale `Tr/Ts`, sample `i` maps to:

```text
project_coordinate = i * Pr * Ts / (Tr * Ps)
```

Factors are reduced before multiplication and the integer output frame count is
the ceiling of the rational same-duration count:

```text
output_frames = ceil(project_frames * Ps * Tr / (Pr * Ts))
```

This preserves the source interval `[0, project_frames * Ps / Pr)` under the
same half-open boundary rule used by the implementation.

## Video metadata and duration

FreeFPS creates a local effective `OUTPUT_INFO`; it does not alter the host's
project structure. The effective copy carries target `rate`, `scale`, and `n`,
so the original x264guiEx command builder, duration calculation, and video loop
receive a consistent target stream. The host audio rate/sample count remain the
source values. Turning FreeFPS off returns the untouched source `OUTPUT_INFO`.

## Timeline injection

The public API still receives only an integer frame. A v2.1.4-specific detour
at the timeline-state builder changes its existing private flag/double input,
before object evaluation; it does not write `OBJECT_INFO.time` after the fact.
Host validation checks:

- x64 PE machine;
- image size `0x527000` and file size 5,228,544;
- entry RVA `0x2b6ebc`;
- expected timeline-builder prologue at RVA `0x2662d0`;
- expected call at RVA `0x2657e3` and its resolved target relationship.

These are centralized compatibility guards, not claims of stable public ABI.

## Request context and cleanup

Each request records generation, output sample index, public integer frame,
double coordinate, and active state. SRW locks protect both the active session
and hook context. Overlapping requests are rejected. The request is cleared
after `func_get_video` returns, and the hook is removed when the session ends;
the executable trampoline is intentionally retained to avoid freeing memory
that a racing pre-existing caller might still use.

## Cache compatibility

The observed AviUtl2 cache maps hash only a four-byte integer key. Therefore
two coordinates such as 80.0 and 80.5 collide if the public frame is 80. The RC
encapsulates a neighbor-frame eviction fallback in
`freefps_cache_workaround.*`. It is replaceable by a future task-aware cache
identity or isolated render context.

## Configuration isolation

The formal plugin uses:

- display name `x264guiEx FreeRenderFPS`;
- binary `x264guiEx-FreeRenderFPS.auo2`;
- adjacent FreeRenderFPS INIs;
- project key `freerenderfps_config`;
- JSON identity `x264guiEx FreeRenderFPS ConfigFile v1 json`;
- fixed binary-config identity `FreeRenderFPS ConfigFile v1` (under the
  32-byte legacy field limit);
- a FreeRenderFPS-specific last-output profile name.

Module resource/config lookup resolves the loaded module by function address,
so the fork cannot accidentally bind to an original `x264guiEx.auo2` instance.

## Packaging boundary

Formal packages include only the Release plugin, three adjacent INIs, optional
profiles, notices, and documentation. Research/probe tools stay under
`experiments/` or development-only directories. External encoder/mux/audio
binaries cross a separate license/provenance boundary and are omitted until
their exact redistribution obligations are met.
