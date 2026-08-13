# Phase 5 subframe scheduler test

This directory contains a build-specific, debug-only proof of concept. It does
not encode video and does not change the project FPS or encoder metadata.

`subframe_scheduler_probe.auo2` requests 60 unique public integer frames
`0..59`. The experimental watcher maps request ordinal `i` to internal double
coordinate `i * 0.5` by changing the private input arguments of the internal
timeline-state builder before it executes. It does not write the local state at
RVA `0x2663c5` and never writes `OBJECT_INFO.time`.

Three additional requests of integer frame 80 are cache diagnostics mapped to
`80.0`, `80.5`, and `80.0`; they are not counted as output samples.

The watcher is restricted to the verified AviUtl2 v2.1.4 image and checks
instruction-byte signatures before arming hardware execution breakpoints. It
refuses overlapping output callbacks, unexpected request order, unexpected
direct callers, occupied debug-register slots, or failed readback.

Usage:

    subframe_scheduler_watch.exe <pid> <watch-log> observe
    subframe_scheduler_watch.exe <pid> <watch-log> schedule

The first mode records the real output-to-timeline call relationship without
modifying process memory. The second enables the guarded mapper. Do not run the
watcher on an AviUtl2 instance with unsaved work.

## Phase 5 result

The v2.1.4 proof succeeded for 60 unique output requests. The final run mapped
integer requests `0..59` to internal coordinates `0.0..29.5`; all 60 reached
the guarded timeline-state input. `OBJECT_INFO.time` advanced by `1/60` second,
while `OBJECT_INFO.frame` remained the truncated integer coordinate. Adjacent
samples produced different full-frame hashes and intermediate object positions.

The cache diagnostic also found an important limitation: after the first
`func_get_video(80)`, two repeated requests for frame 80 bypassed timeline-state
construction and returned the same cached image. This prototype is therefore
not a production scheduler and does not claim that arbitrary subframes work
through every AviUtl2 cache.

Final evidence:

- `phase5_schedule_watch_final.log`: request/timeline association and guarded
  input writes.
- `phase5_schedule_output_final.log`: full-frame hashes, bounding boxes, and
  centroids.
- `phase5_schedule_object_info_final.log`: filter-observed frame/time values.
- `phase5_sample_results_final.tsv`: joined 60-sample result table.

The final watcher verifies the actual ordinary call site at RVA `0x2657e3`
(return RVA `0x2657e8`) plus the output-bridge and timeline-builder signatures.
All locations remain build-specific experimental evidence, not production
addresses.
