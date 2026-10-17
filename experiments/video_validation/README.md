# Legacy AviUtl1 output diagnosis

Inputs:

* `D:\\AviUtl Free Render FPS\\30 aviutl1 test.mp4`
* `D:\\AviUtl Free Render FPS\\30-60 aviutl1 test.mp4`

The first file is 30/1 video, 150 frames, 5 seconds.  Its decoded
`framemd5` stream contains 150 distinct frame hashes and no adjacent exact
duplicates.

The second file is 60/1 video, 300 frames, 5 seconds, with the same audio
duration and format as the first file.  Its decoded stream has 298 distinct
exact hashes and two exact adjacent duplicate pairs.  More importantly,
`signalstats` shows the repeated pattern expected when each 30 FPS scene
evaluation is emitted twice: most odd/even boundaries have a near-zero Y
difference, while the other boundaries contain the actual motion.  H.264
quantization means visual duplicates do not always have byte-identical
decoded hashes, so the Y-difference check is required in addition to
`framemd5`.

The old Win32 host adapter requested fractional coordinates but only logged
them; it left ExEdit's `FilterProcInfo->subframe` at zero.  The rebuilt
adapter writes the fractional centiframe at ExEdit 0.92 RVA `0x48180`, before
the filter dispatcher consumes it.  See `30.framemd5`, `60.framemd5`, and
`60.signalstats.txt` for the source evidence.
