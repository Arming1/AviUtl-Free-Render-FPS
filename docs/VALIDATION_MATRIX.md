# FreeRenderFPS v1.0.0 validation matrix

Date: 2026-08-13  
Host: AviUtl2 v2.1.4 x64  
Final AUO2 version: `1.0.0`  
Final AUO2 SHA-256: `A9A60EEB4A92073B7CBA539433821E7E18DAF69D714A4CA9857ED45B8054B29B`

Frame identity was measured after decoding. Container FPS metadata alone was
not accepted as temporal-sampling evidence.

## Output matrix

| Project → target | Output frames | Video FPS | Video duration | Audio duration | Unique decoded frames | Adjacent equal | Result |
|---|---:|---|---:|---:|---:|---:|---|
| FreeFPS OFF, 66 → 66 | 330 | 66/1 | 5.000000 s | 5.000000 s | — | — | PASS |
| 30 → 60 | 300 | 60/1 | 5.000000 s | 5.000000 s | 300 | 0 | PASS |
| 66 → 60 | 300 | 60/1 | 5.000000 s | 5.000000 s | 300 | 0 | PASS |
| 66 → 59.94 | 300 | 60000/1001 | 5.005000 s | 5.000000 s | 300 | 0 | PASS |
| 66 → 24 | 120 | 24/1 | 5.000000 s | 5.000000 s | 120 | 0 | PASS |
| 66 → 120 | 600 | 120/1 | 5.000000 s | 5.000000 s | 600 | 0 | PASS |
| 120 → 60 | 300 | 60/1 | 5.000000 s | 5.000000 s | 300 | 0 | PASS |

The 60-second 66 → 60000/1001 test produced 3597 frames and 60.008333 s
video against 60.000000 s audio. The +8.333 ms endpoint delta is below one
target-frame interval and follows the documented ceiling boundary rule. It
contained 3590 unique decoded frames and seven adjacent equal images on the
simple linear-motion fixture.

## Final package-only smoke tests

| Path | Package use | Result |
|---|---|---|
| Setup EXE | non-interactive install into a fresh isolated host; one AUO2 loaded | H.264 150 frames at 30/1, AAC 217 frames, both 5.000000 s — PASS |
| Portable ZIP | extracted directly over a fresh isolated AviUtl2 root | `data\Plugin` layout, one AUO2 loaded; same real encode — PASS |

Retained final media:

- `experiments/66_to_60_validation/v100_setup_clean_30_off.mp4`
- `experiments/66_to_60_validation/v100_portable_clean_30_off.mp4`

The full FreeFPS matrix was run on the same release code and audited runtime
before the final version-resource-only rebuild. The final `1.0.0` package then
passed both package-only compatibility encodes. RC1 was not overwritten.
