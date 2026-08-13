#!/usr/bin/env python3
"""Analyze deterministic 66/1 -> 60/1 FreeRenderFPS validation output."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import subprocess
from fractions import Fraction
from pathlib import Path

import cv2
import numpy as np


PROJECT_RATE = 66
PROJECT_SCALE = 1
TARGET_RATE = 60
TARGET_SCALE = 1
PROJECT_FRAMES = 330


def probe(ffprobe: Path, video: Path) -> dict:
    command = [
        str(ffprobe), "-v", "error", "-count_frames",
        "-show_entries",
        "stream=index,codec_type,r_frame_rate,avg_frame_rate,time_base,duration,nb_frames,nb_read_frames,sample_rate",
        "-show_entries", "format=duration,size,bit_rate", "-of", "json",
        str(video),
    ]
    return json.loads(subprocess.check_output(command, text=True, encoding="utf-8"))


def stream_of(metadata: dict, kind: str) -> dict:
    return next(s for s in metadata["streams"] if s.get("codec_type") == kind)


def analyze(video: Path, output: Path, ffprobe: Path, summary: Path) -> None:
    metadata = probe(ffprobe, video)
    video_stream = stream_of(metadata, "video")
    audio_stream = stream_of(metadata, "audio")
    decoded_expected = int(video_stream.get("nb_read_frames") or video_stream["nb_frames"])

    capture = cv2.VideoCapture(str(video))
    if not capture.isOpened():
        raise RuntimeError(f"cannot decode {video}")

    frames: list[dict] = []
    previous_hash = ""
    previous_centroid_x = math.nan
    index = 0
    while True:
        ok, bgr = capture.read()
        if not ok:
            break
        gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
        digest = hashlib.md5(gray.tobytes()).hexdigest()
        # The fixture is a white circle on pure black. A moderate threshold
        # rejects H.264 ringing while preserving the antialiased object body.
        mask = gray >= 64
        ys, xs = np.nonzero(mask)
        if xs.size == 0:
            raise RuntimeError(f"no foreground detected at decoded frame {index}")
        weights = gray[ys, xs].astype(np.float64)
        centroid_x = float(np.average(xs, weights=weights))
        centroid_y = float(np.average(ys, weights=weights))
        coordinate = Fraction(index * PROJECT_RATE * TARGET_SCALE,
                              TARGET_RATE * PROJECT_SCALE)
        internal_time = coordinate * Fraction(PROJECT_SCALE, PROJECT_RATE)
        delta_x = centroid_x - previous_centroid_x if index else math.nan
        frames.append({
            "output_index": index,
            "project_rate": PROJECT_RATE,
            "project_scale": PROJECT_SCALE,
            "target_rate": TARGET_RATE,
            "target_scale": TARGET_SCALE,
            "output_fps": video_stream["avg_frame_rate"],
            "decoded_frame_count": decoded_expected,
            "video_duration": video_stream["duration"],
            "audio_duration": audio_stream["duration"],
            "expected_timeline_coordinate": f"{coordinate.numerator}/{coordinate.denominator}",
            "expected_coordinate_decimal": f"{float(coordinate):.12f}",
            # This is the expected builder input/time. No instrumentation was
            # active during the release encode, so do not label it observed.
            "expected_time_seconds": f"{float(internal_time):.12f}",
            "observed_internal_time": "",
            "frame_md5": digest,
            "adjacent_duplicate": int(index > 0 and digest == previous_hash),
            "bbox_x0": int(xs.min()),
            "bbox_y0": int(ys.min()),
            "bbox_x1": int(xs.max()),
            "bbox_y1": int(ys.max()),
            "centroid_x": f"{centroid_x:.9f}",
            "centroid_y": f"{centroid_y:.9f}",
            "centroid_delta_x": "" if index == 0 else f"{delta_x:.9f}",
        })
        previous_hash = digest
        previous_centroid_x = centroid_x
        index += 1
    capture.release()

    if index != decoded_expected:
        raise RuntimeError(f"OpenCV decoded {index}, ffprobe reported {decoded_expected}")

    fields = list(frames[0].keys())
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        writer.writerows(frames)

    deltas = np.array([float(row["centroid_delta_x"]) for row in frames[1:]])
    median = float(np.median(deltas))
    deviations = np.abs(deltas - median)
    # A threshold of 1.0 px is deliberately wider than expected AA/H.264
    # centroid noise, while still detecting a dropped-state double step.
    spike_indices = [int(i + 1) for i, d in enumerate(deviations) if d > 1.0]
    duplicate_count = sum(int(row["adjacent_duplicate"]) for row in frames)
    expected_count = math.ceil(
        PROJECT_FRAMES * PROJECT_SCALE * TARGET_RATE /
        (PROJECT_RATE * TARGET_SCALE)
    )
    result = {
        "project_rate": PROJECT_RATE,
        "project_scale": PROJECT_SCALE,
        "project_frames": PROJECT_FRAMES,
        "target_rate": TARGET_RATE,
        "target_scale": TARGET_SCALE,
        "expected_output_frames": expected_count,
        "decoded_frames": index,
        "output_fps": video_stream["avg_frame_rate"],
        "video_duration": float(video_stream["duration"]),
        "audio_duration": float(audio_stream["duration"]),
        "adjacent_duplicate_count": duplicate_count,
        "unique_frame_hashes": len({row["frame_md5"] for row in frames}),
        "centroid_delta_x_min": float(deltas.min()),
        "centroid_delta_x_max": float(deltas.max()),
        "centroid_delta_x_mean": float(deltas.mean()),
        "centroid_delta_x_median": median,
        "centroid_delta_x_stddev": float(deltas.std()),
        "cadence_spike_threshold_pixels": 1.0,
        "cadence_spike_count": len(spike_indices),
        "cadence_spike_indices": spike_indices,
        "video_sha256": hashlib.sha256(video.read_bytes()).hexdigest().upper(),
        "pass": (
            index == expected_count
            and video_stream["avg_frame_rate"] == "60/1"
            and duplicate_count == 0
            and len(spike_indices) == 0
            and abs(float(video_stream["duration"]) - float(audio_stream["duration"])) < 0.001
        ),
    }
    summary.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n",
                       encoding="utf-8")
    print(json.dumps(result, indent=2, ensure_ascii=False))
    if not result["pass"]:
        raise SystemExit(1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("video", type=Path)
    parser.add_argument("--output", type=Path, default=Path("66_to_60_results.tsv"))
    parser.add_argument("--summary", type=Path, default=Path("66_to_60_summary.json"))
    parser.add_argument(
        "--ffprobe", type=Path,
        default=Path(r"D:\FFmpeg-master\ffmpeg-2025-11-27-git-61b034a47c-full_build\bin\ffprobe.exe"),
    )
    args = parser.parse_args()
    analyze(args.video.resolve(), args.output.resolve(), args.ffprobe.resolve(),
            args.summary.resolve())


if __name__ == "__main__":
    main()

