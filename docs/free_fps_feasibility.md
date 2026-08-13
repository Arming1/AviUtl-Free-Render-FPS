# Free output/render FPS feasibility (Phase 1)

## Classification

**C — requires hooking or internal patching (minimum required class).**

This is a boundary classification, not a claim that a stable hook has already
been found. The supplied public SDK is insufficient for true subframe scene
evaluation. If a later, separately scoped internal investigation finds no
usable renderer seam, the implementation would need to be reclassified as
**D — requires modifying AviUtl2 itself**.

Classes A and B are ruled out for the stated requirement because every public
whole-scene render request found takes an integer frame index:

- output pull: `OUTPUT_INFO::func_get_video(int frame, DWORD format)`
  (`reference/aviutl2_sdk/output2.h:48-57`);
- general plugin render task:
  `EDIT_HANDLE::rendering_scene_video(int frame, ...)`
  (`reference/aviutl2_sdk/plugin2.h:771-797`).

Neither interface has a timestamp, rational subframe coordinate, independent
sampling-rate argument, or render-context override.

## Requirement-to-evidence proof

The target is a 30 FPS project rendered at 60 genuinely distinct scene times,
without changing the project FPS, changing metadata only, or duplicating
frames.

For output sample `j` at desired rate `R_out`, the required scene evaluation
time is:

```text
t(j) = j / R_out
```

At 60 FPS this includes `1/60`, `3/60`, and other times between the 30 FPS
project's integer frame times. The public callback can express only an integer
`frame` (`output2.h:48-57`). Calling frame `0` twice does not distinguish `t=0`
from `t=1/60`; calling frame `1` identifies the next existing project frame at
`t=1/30`. The necessary coordinate is therefore absent.

x264guiEx confirms how a real output plugin uses the contract:

1. `enc_out` sets `frames_to_enc = oip->n` and increments local `i_frame`
   (`reference/x264guiEx/x264guiEx/encode/auo_video.cpp:848-907`).
2. It passes that integer to `func_get_video_ex`, which aliases to AviUtl2's
   `func_get_video` in target-2 builds
   (`encode/auo_video.cpp:970-980`;
   `reference/x264guiEx/x264guiEx/auo.h:38-49`).
3. It passes `oip->rate/oip->scale` to x264 as `--fps`
   (`encode/auo_video.cpp:491-495`).
4. The writer thread sends those returned frames to encoder stdin
   (`encode/auo_video.cpp:704-733`).

There is no second host-render FPS anywhere in this chain.

## Why the apparent public alternatives do not satisfy the goal

| Alternative | Public evidence | Result |
|---|---|---|
| Set encoder/container FPS to 60 | An output plugin controls its encoder command; x264guiEx derives `--fps` from `oip->rate/scale` (`encode/auo_video.cpp:491-495`). | Metadata/duration change only unless new images exist. Rejected by requirement. |
| Request each integer frame twice | `func_get_video` accepts an integer index only (`output2.h:48-57`). | Duplicate samples. Rejected by requirement. |
| Request out of order or prefetch more | `func_set_buffer_size` controls buffer count (`output2.h:79-83`); AFS may request `frame + 1` (`encode/afs_client.h:243-282`). | Changes scheduling/cache behavior, not temporal coordinates. |
| Use `rendering_scene_video` from a common plugin | It also takes `int frame` (`plugin2.h:771-797`). | Same integer grid; no unusual public-API escape hatch. |
| Use media time APIs | `func_time_to_frame(double)` is an input-plugin callback and cache-by-time addresses a media file (`input2.h:116-122`; `cache2.h:186-192`). | Source-media lookup, not composed project evaluation. |
| Use filter `OBJECT_INFO::time` | Time is reported to a filter during an already selected evaluation (`filter2.h:327-338`). | Observation, not a scene render request. |
| Temporarily call `set_scene_frame_rate(60,1)` | Public editor operation exists (`plugin2.h:366-370`). | Changes project/scene FPS, explicitly forbidden; it is not an output-only render context. |
| Interpolate inside the encoder plugin | The output plugin can retain returned images, but the SDK provides no host subframe evaluation (`output2.h:36-83`). | Could synthesize frames, but would be a separate interpolation algorithm, not AviUtl2 rendering the scene at independent times. Outside Phase 1 and not proof of public render-FPS separation. |

## Frame count and duration consequences

The host supplies `OUTPUT_INFO::n`; the SDK does not publish its calculation
(`reference/aviutl2_sdk/output2.h:36-46`). x264guiEx treats it as the source-loop
count and calculates nominal duration from `n * scale / rate`
(`reference/x264guiEx/x264guiEx/encode/auo_encode.cpp:857-867,1715-1732`).

For true 30→60 rendering at equal duration, a solution would need both:

- a new output count approximately `n_out = duration * R_out`; and
- a way to render sample `j` at `j/R_out` independently of the project integer
  frame grid.

The first can be computed by plugin code. The second is the missing capability
and is why changing only loop count or encoder FPS is insufficient.

## What Phase 1 establishes vs. leaves unknown

Established from exact source:

- project/scene FPS is rational `rate/scale`
  (`reference/aviutl2_sdk/plugin2.h:132-138`);
- output receives one FPS pair and host-selected frame count
  (`reference/aviutl2_sdk/output2.h:36-46`);
- public scene image requests are integer-indexed
  (`output2.h:48-57`; `plugin2.h:771-797`);
- x264guiEx loops over those integer indices and forwards the same FPS to its
  encoder (`encode/auo_video.cpp:848-997,491-495`);
- project config persistence contains settings, not render timing: public
  `PROJECT_FILE` has only parameter/path operations
  (`reference/x264guiEx/x264guiEx/project2.h:9-42`), and x264guiEx uses it for a
  JSON `config` value (`reference/x264guiEx/x264guiEx/x264guiEx.cpp:148-176`).

Unknown without a later internal/reverse-engineering phase:

- the private AviUtl2 symbols behind `func_get_video`;
- whether the internal renderer already accepts a continuous time value or only
  an integer frame;
- whether a stable, safe internal hook can create an alternate render context;
- how caches, scripts, particles, audio, and stateful effects behave under
  subframe evaluation;
- whether internal intervention can preserve project state and export-range
  semantics reliably.

These unknowns are intentionally not filled with assumptions.

## Probe status and stopping point

The minimal `experiments/render_probe` output plugin is buildable and has been
compiled successfully as a 64-bit `render_probe.auo2`. It makes a small set of
unique, valid, non-monotonic integer requests and logs:

- reported FPS numerator/denominator and decimal value;
- `OUTPUT_INFO::n`;
- request index and implied timestamp;
- callback/request order and null/non-null result.

The probe cannot test fractional sampling because the API cannot express it.
Its runtime purpose is narrower: confirm that the host honors non-monotonic
integer requests and record the exact `OUTPUT_INFO` values for full-range and
selected-range exports. No interpolation or production plugin has been
implemented.

The correct Phase 1 stopping point is therefore: **public API path rejected for
true independent render FPS; internal renderer investigation is a separate next
phase and has not been started here.**
