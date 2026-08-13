# 66/1 -> 60/1 temporal resampling validation

`freerenderfps_66fps.aup2` is a deterministic five-second AviUtl2 fixture:

- 640x360, 66/1 project FPS, 330 project frames;
- one 48-pixel white circle on black;
- linear X motion from -200 to +200;
- no external media assets.

FreeRenderFPS must export 300 samples at 60/1 FPS. Sample `i` must evaluate
project coordinate `i * 66 / 60`, not merely drop selected integer frames.

After encoding, run:

```powershell
python .\analyze_66_to_60.py .\freerenderfps_66_to_60.mp4 `
  --output .\66_to_60_results.tsv `
  --summary .\66_to_60_summary.json
```

The analyzer records decoded hashes, bbox, intensity-weighted centroid,
centroid delta, rational coordinate and media durations for every frame. It
fails on duplicate frames, wrong rate/count, A/V drift, or a centroid-delta
deviation greater than one pixel from the median.
