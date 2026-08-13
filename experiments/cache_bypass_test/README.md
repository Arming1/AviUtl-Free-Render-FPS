# Phase 6 cache bypass test

This directory contains a build-specific, debug-only cache experiment for
AviUtl2 v2.1.4. It does not encode video, alter project FPS, or patch module
code.

The output probe performs 60 unique warm-up requests, followed by one of the
controlled target sequences on the same public integer frame 80:

- sequence A: `80.0, 80.5, 80.0`
- sequence B: `80.5, 80.0, 80.5`

The output filename selects the experiment:

- `baseline_A.log` / `baseline_B.log`: no mid-run cache action.
- `resize_A.log` / `resize_B.log`: alternate public output buffer sizes 1/2.
- `evict_A.log` / `evict_B.log`: request integer frame 79 between recorded
  frame-80 samples while the public video buffer size is one.

The watcher is the Phase 5 guarded watcher extended with sequence and eviction
arguments. It checks the AviUtl2 image and instruction signatures before using
hardware execution breakpoints:

    cache_bypass_watch.exe <pid> <log> schedule A direct
    cache_bypass_watch.exe <pid> <log> schedule B direct
    cache_bypass_watch.exe <pid> <log> schedule A evict
    cache_bypass_watch.exe <pid> <log> schedule B evict

The eviction requests are diagnostics, not output samples. The three recorded
samples all call `func_get_video(80)`.

## Result

The baseline collides. In sequence A, only the first `80.0` request reaches
timeline construction and all three hashes are `875555f88e3def2b`. In sequence
B, only the first `80.5` request reaches timeline construction and all three
hashes are `e8cc2a2b6ddd098f`. The first sampled coordinate therefore decides
the cached result for later requests of public frame 80.

`cache_hit_A_watch.log` dynamically observes the hit-only path at AviUtl2
v2.1.4 RVA `0x221aba`. Both its input key and cached node key are the four-byte
integer `80`; the timeline builder is not called after either hit.

Changing `func_set_buffer_size` from 1 to 2 and back does not reliably evict
the current frame. `resize_A.log` remains stale; the builder activity observed
for target 1 is a prefetch of integer frame 81.

The debug-only eviction sequence succeeds. With video buffer size one and an
unrecorded request for integer frame 79 between samples, sequence A produces
hashes `875555f88e3def2b, e8cc2a2b6ddd098f, 875555f88e3def2b`; sequence B
produces the reverse. Every target reaches timeline construction and the
OBJECT_INFO probe reports `2.6666666666666665` for 80.0 and
`2.6833333333333331` for 80.5.

This proves sequential no-stale subframe evaluation for the same public
integer frame. It does **not** prove that 80.0 and 80.5 coexist as two entries
inside AviUtl2's cache, and neighbor-frame eviction is not a production design.

Canonical data is in `phase6_results.tsv`; exact OBJECT_INFO callbacks are in
`phase6_object_info_excerpt.log`. The early `baseline_A.log` and
`baseline_A_watch.log` files were produced before a filename-mode parser fix;
use `baseline_A_final*` or the later `cache_hit_A*` run instead.

Do not use this experiment on an AviUtl2 process with unsaved work. All RVAs
and byte signatures are experimental evidence for v2.1.4, not production hook
locations.
