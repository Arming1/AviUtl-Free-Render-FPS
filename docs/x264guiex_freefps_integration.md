# x264guiEx-FreeFPS Phase 7 integration

## Status

Phase 7 MVP is implemented in `src/x264guiEx-FreeFPS` without modifying
`reference/x264guiEx`. It is an experimental AviUtl2 v2.1.4-only fork, not a
production release.

Runtime validation on 2026-08-11 succeeded for the supplied 30 fps test project:

- source: 30/1 fps, 161 project frames, duration 5.366667 s;
- target: 60/1 fps;
- encoded result: H.264 1920x1080, 60/1 fps, 322 decoded frames, duration
  5.366667 s;
- audio: AAC 44.1 kHz, duration 5.366667 s;
- all 322 decoded video frame MD5 hashes were unique;
- zero adjacent frames were equal, including zero equal `(2k, 2k+1)` pairs.

The last result is the important subframe check. Both members of each pair use
the same public integer AviUtl2 frame, but coordinates `k` and `k+0.5` produced
different decoded images. This is not a duplicated-frame or metadata-only
result.

Output artifact:

- `experiments/subframe_scheduler_test/phase7_freefps_60_true.mp4`
- SHA-256:
  `4BA43DB0D7940BCE5150163992C765FF6B8211A2A0911E2F5235478E80BBC145`

The earlier `phase7_freefps_60.mp4` is a deliberate disabled-mode control. It
contains the original 30/1 fps and 161 frames.

## Confirmed x264guiEx video path

The unchanged upstream path is:

`GetOutputPluginTable` -> `func_output2` -> `func_output` -> `video_output` ->
`video_output_inside` -> `enc_out` -> frame conversion -> writer thread -> x264
stdin.

Exact upstream locations:

- AviUtl2's `OUTPUT_INFO` fields and `func_get_video(int frame, DWORD format)`
  are declared in `reference/x264guiEx/x264guiEx/output2.h:35-84`.
- AviUtl2 builds alias `func_get_video_ex` to `func_get_video` in
  `reference/x264guiEx/x264guiEx/auo.h:38-49`.
- `func_output2` enters the existing output pipeline in
  `reference/x264guiEx/x264guiEx/x264guiEx.cpp:373-402`; `func_output` dispatches
  video/audio work in `:263-367`.
- `video_output` and `video_output_inside` reach `enc_out` in
  `reference/x264guiEx/x264guiEx/encode/auo_video.cpp:1248-1300`.
- `enc_out` originally sets `frames_to_enc = oip->n` in `:791-849`; its main
  loop is `:906-1006` and the actual source-frame request is `:975-979`.
- conversion and writer signaling are at `:986-991`; the writer thread sends
  the converted planes to x264 at `:704-734`.

The fork keeps that path. Its only frame-acquisition branch is in
`src/x264guiEx-FreeFPS/x264guiEx/encode/auo_video.cpp:978-989`. Disabled mode
executes the original `func_get_video_ex(i_frame, format)` branch.

## Frame count, x264 FPS, and duration

Upstream command construction is in
`reference/x264guiEx/x264guiEx/encode/auo_video.cpp:455-501`:

- `--frames` comes from `oip->n` at `:483-485`;
- `--fps` comes from reduced `oip->rate/oip->scale` at `:491-495`;
- the encoder loop separately consumes `oip->n`, so changing command metadata
  alone cannot implement FreeFPS.

For enabled mode, `FreeFpsSession` copies `OUTPUT_INFO` and changes only the
effective video rate, scale, and frame count. The complete existing x264,
audio, mux, logging, duration, and error paths receive that effective copy.
The host-owned `OUTPUT_INFO` is not modified.

For project rate `Rp/Sp`, target rate `Rt/St`, project sample count `N`, and
output sample index `i`:

```text
project_coordinate(i) = i * Rp * St / (Rt * Sp)
output_frames = ceil(N * Sp * Rt / (Rp * St))
public_frame(i) = floor(project_coordinate(i))
```

Factors are reduced with `gcd` before checked 64-bit multiplication. The
coordinate is calculated from the absolute sample index, not by repeatedly
adding a floating-point step. Conversion to `double` occurs only at the final
coordinate calculation.

The end rule is half-open source duration `[0, N * Sp / Rp)`: output samples
are emitted while their target time is within that duration. This is why the
161-frame 30 fps fixture produces `ceil(161 * 60 / 30) = 322` frames, with no
duration change.

Implementation: `freefps_scheduler.cpp:74-150` derives the rational count and
effective `OUTPUT_INFO`; `:169-182` maps the sample; `:209-233` performs the
request.

## Audio relationship

Upstream `check_audio_length` compares
`oip->n * oip->scale / oip->rate` with `audio_n/audio_rate` in
`reference/x264guiEx/x264guiEx/encode/auo_audio.cpp:160-217`. Actual audio
extraction runs independently to `audio_n` at `:546-640`. Other duration
consumers include `get_duration` in `encode/auo_encode.cpp:1715-1733` and mux
duration handling in `encode/auo_mux.cpp:302-310`.

The effective FreeFPS values preserve the exact video duration, while
`audio_n`, `audio_rate`, and the audio callback remain unchanged. The runtime
test confirmed equal 5.366667 s video and audio duration.

## Scheduler and request association

Each frame request carries:

- monotonically increasing generation ID;
- output sample index;
- requested public integer frame;
- target double coordinate;
- active state.

`freefps_hook_begin_request` publishes the complete context under an SRW lock.
The timeline-builder worker may run on another thread; the detour only injects
when the context is active and the builder's integer frame matches. The request
is cleared immediately after `func_get_video` returns, including failure paths.
Overlapping requests fail closed. Disabled mode never installs the hook or
publishes a context.

This is deliberately not a single global `current_time`: the double is always
paired with frame identity and a generation. The Phase 7 MVP permits only one
active output session.

## Timeline input hook

The hook is confined to `freefps_aviutl_hook.*`. It detours the entry of the
previously verified timeline-state builder and supplies its already-existing
private `flag + double coordinate` input path. It does not write
`OBJECT_INFO.time` and does not use the Phase 4 local stack store.

The trampoline copies complete instructions from the entry and changes the
arguments only for a matching active request. The original entry bytes are
restored when the output session ends. The trampoline allocation is retained
to avoid racing an already-entered worker.

This remains private-hook code and is separated from the public output layer.

## Cache workaround

`freefps_cache_workaround.*` encapsulates the Phase 6 neighbor-frame eviction.
When consecutive output samples map to the same integer frame but different
double coordinates, it requests an in-range neighbor first and then the target
frame. Each request must actually hit the timeline builder; otherwise export
fails with a cache-collision error. AviUtl2's requested buffer count is reduced
to one when supported.

This is an experimental workaround, not a general cache solution. It adds
render work, assumes the observed v2.1.4 cache behavior, and may interact with
stateful filters. It is intentionally isolated so a later cache invalidation or
subframe-aware key can replace it.

## Version protection

FreeFPS refuses to start unless all v2.1.4 checks pass:

- x64 PE image;
- image size `0x527000`;
- entry RVA `0x2b6ebc`;
- executable file size `5,228,544` bytes;
- known complete prologue bytes at timeline-builder RVA `0x2662d0`;
- known call bytes at the standard caller RVA `0x2657e3`;
- decoded relative call target equals the timeline-builder RVA.

Addresses are module-relative, never ASLR-adjusted absolute addresses. A
signature mismatch disables FreeFPS and shows an error. The original output
path remains usable when the checkbox is off.

These guards make the patch fail closed; they do not make it
version-independent. A future implementation should locate and validate the
input path by a multi-instruction signature plus caller/data-flow checks.

## Configuration and UI

The fork appends three fields to `CONF_VIDEO`:

- `freefps_enable` (default false);
- `freefps_target_rate` (default 60);
- `freefps_target_scale` (default 1).

They use the existing JSON/profile/project persistence paths in
`prm/auo_conf.cpp:251-253,340-342`. The minimal UI is on the x264 `扩展` tab and
shows a checkbox plus rational numerator/denominator controls. Its form binding
is in `frm/frmConfig.cpp:1754-1756,1940-1942`.

## Build and runtime record

Build used Visual Studio MSBuild 18.8.2, `Debug|x64`. Because the machine lacks
the .NET Framework 4.8 reference assemblies, only the fork's project target was
changed to 4.7.2. Build completed with 0 errors and 8 pre-existing-style
warnings.

Built plugin:

- `src/x264guiEx-FreeFPS/Debug/x64/x264guiEx-FreeFPS.auo2`
- SHA-256:
  `1D1779E3EADAC4BFEBBAE9506CFE8C57987DB546E4EC67B875FA5F6A96616F83`

The plugin was loaded side-by-side with the original x264guiEx. The original
plugin file was not replaced.

## Known Phase 7 limitations

- AviUtl2 v2.1.4 x64 only.
- Neighbor-frame cache eviction is slow and not a production cache design.
- One active FreeFPS output session; no concurrent exports.
- AFS, AviUtl keyframe pre-scan, and AUO timecode output are disabled in
  FreeFPS mode because they make unscheduled frame requests.
- The request association relies on the observed synchronous lifetime of
  `func_get_video`; filters that defer scene evaluation beyond its return are
  not supported.
- No multi-version signatures, updater, installer, or polished UI.
- The experimental UI is functional but visually overlaps part of the legacy
  Ex-settings layout.
- The implementation has only been validated with the supplied scheduler test
  project. Wider filter/media compatibility testing is still required.

