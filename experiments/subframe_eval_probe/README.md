# Phase 4 subframe evaluation output probe

This directory contains a debug-only AviUtl2 output-plugin prototype that requests integer frame 80 twice as
`BI_RGB`. It logs a full-frame FNV-1a hash and the non-black pixel bounding box
and centroid for both requests. The repeat exposes whether the host reuses an
integer-frame cache entry. The probe never edits the returned frame and does
not encode output.

Runtime status: AviUtl2 rejected the prototype at startup with its generic
duplicate/registration warning, so it was not deployed and none of its repeat-80
results are used as evidence. The validated Phase 4 run instead used the already
loaded Phase 1 `Render Pipeline Probe` (`render_probe.auo2`), producing
`baseline_stable.log` and `modified_frame80.log`. Cache conclusions are therefore
limited to those logs; see `docs/subframe_evaluation_test.md`.

`subframe_test_30fps.aup2` is a 30 FPS fixture containing one 100-pixel white
circle. Its X position moves linearly from -500 to +500 over frames 0 through
160. The Phase 3 no-op filter is attached so `OBJECT_INFO.frame/time` can be
correlated with the output hash.

The normal and modified runs must use the same project and output probe. The
only permitted difference is the one-shot debugger write to the earlier
internal `double` coordinate.
