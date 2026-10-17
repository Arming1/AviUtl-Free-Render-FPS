# validate_freefps_output.py

Checks a FreeRenderFPS output MP4 from its decoded pixels. The video is decoded
once with ffmpeg into 8-bit gray (luma only, `-fps_mode passthrough`, scaled to
at most 640 px wide). Each frame gets a SHA-1 of its luma bytes. Each adjacent
pair of frames gets a mean absolute difference (MAD, 0-255 scale).

Requirements: ffmpeg and ffprobe 5.1 or newer (for `-fps_mode`), and numpy.
Run with `python -I` when the input files are not trusted.

## Usage

```
python -I tools/validate_freefps_output.py VIDEO \
    --project-fps 30 --target-fps 60 --project-frames 150 \
    [--project-duration SECONDS] [--reference REF.mp4] \
    [--near-dup-threshold 0] [--near-dup-relative 0.1] [--pairing-min-ratio 0.25] \
    [--max-near-duplicates 1] [--json OUT.json] \
    [--ffmpeg PATH] [--ffprobe PATH]
```

* `--project-fps` and `--target-fps` take exact rationals (`30`, `66`,
  `60000/1001`). Decimals such as `59.94` are rejected.
* Give exactly one of `--project-frames` (integer) or `--project-duration`
  (seconds, exact rational). The duration must be a whole number of project
  frames.
* `--reference` is optional and informational only. It is not a check.
* `--json` writes every raw number, plus the checks, to a file.

Exit codes: 0 when every check passes or is N/A, 1 when any check fails, 2 on
a tool or input error (message on stderr).

## Checks

| Check | Passes when |
|---|---|
| `frame_count` | decoded frames == `ceil(project_frames * target / project)`, and ffprobe `-count_frames` agrees |
| `stream_rate` | container `r_frame_rate` equals the target exactly |
| `av_duration` | `\|video − audio\|` duration < one target frame. N/A with no audio |
| `no_pairing` | only when target/project is an integer >= 2. The median MAD at boundaries with `i % r != 0` must be at least `pairing_min_ratio` × the median MAD at `i % r == 0`. N/A otherwise |
| `near_duplicates` | **advisory (PASS or WARN, never FAIL).** Counts adjacent boundaries with MAD < max(`near_dup_threshold`, `near_dup_relative` × median boundary MAD), ignoring the final boundary (end clamp); WARN when the count exceeds `max_near_duplicates` |

The overall result is FAIL if any check fails. Exact duplicate boundaries
(equal SHA-1) are reported but are not a check.

## Known limitations

* `near_duplicates` is advisory. A slowly moving scene legitimately produces
  adjacent frames that are identical or nearly so (the 60 s 66 → 59.94 fixture
  has thousands of boundaries below 0.02 MAD yet 3590 unique frames), so it
  cannot prove duplication. Duplication in fixed groups is judged by
  `no_pairing`. The original fixed 0.5 threshold failed every sample, including
  clean controls; see `experiments/video_validation/results_2026-10/SUMMARY.md`.
* `no_pairing` cannot run for non-integer ratios (for example 66 → 60).
* Hashing is luma-only. Frames that differ only in chroma count as exact
  duplicates. Full-frame yuv420p `framemd5` gives fewer duplicates.
