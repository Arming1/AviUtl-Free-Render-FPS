# AviUtl 1.x validation record

Date: 2026-08-20  
Host: AviUtl 1.10 + ExEdit 0.92, Win32  
Plugin: `x264guiEx-FreeRenderFPS.auo`  

## Actual media checked

| File | Video | Audio | Duration |
|---|---|---|---:|
| `D:\AviUtl Free Render FPS\test3 30.mp4` | 150 frames, 30/1 | AAC 44.1 kHz stereo, 217 frames | 5.000000 s |
| `D:\AviUtl Free Render FPS\test3 30-60.mp4` | 300 frames, 60/1 | AAC 44.1 kHz stereo, 217 frames | 5.000000 s |

The 30 FPS reference has 150 unique decoded frame hashes and no adjacent exact
duplicates. The 60 FPS output has 299 unique decoded hashes and one adjacent
exact duplicate at decoded frames 298/299. `signalstats` reports non-zero Y
difference throughout the continuously moving portion; the two near-zero
values are the first-frame baseline and the final endpoint duplicate.

The result proves that the output is no longer made by emitting each 30 FPS
scene result twice. The final pair is the expected project-end clamping boundary
for this five-second fixture; it is not a repeated pair throughout the motion.

Raw analysis files are retained beside the project under
`experiments\video_validation\test3_30.framemd5`,
`test3_60.framemd5`, and `test3_60.signalstats.txt`.

## Scope of this record

This record validates the corrected 30 → 60 legacy path and audio duration
preservation. The 51.2 → 60, 51.2 → 60000/1001, and 66 → 60 cases remain
additional matrix cases to run on the user's legacy host before claiming the
same coverage as the AviUtl2 release matrix.
