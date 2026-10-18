#!/usr/bin/env python3
"""Check the frame-hold steps of a white circle in an exported MP4.

Usage:
    python -I experiments/subscene_rate/analyze_steps.py VIDEO.mp4
        [--expect follow|keep] [--step N] [--csv PATH]
        [--ffmpeg PATH] [--ffprobe PATH]

The export is decoded with ffmpeg (gray plane only). Per frame the script takes
the centroid x of the moving white circle (pixels with |Y - median(Y)| > 30).
Frames without a circle are reported and skipped.

Consecutive frames with the same centroid form a "run" (one position held for
that many output frames). The script prints the runs, the run-length
histogram, the frame-to-frame differences and the step uniformity.

--expect follow  every frame moves: interior runs have length 1 (--step 1)
--expect keep    the sub-scene is kept at its own rate: interior runs have
                 length N (--step N, e.g. 4 for a 15 fps scene in a 60 fps output)
Both modes also require the steps between positions to keep one sign (no
reversal) and to be uniform: max - min <= max(0.5 px, 2 % of the mean step).
The first and last runs are not checked for length: they may be shorter than N
(the hold may start part-way) or longer (a stay segment before the first or
after the last move, for example a sub-scene that holds its end). Only the
interior runs must have exactly N frames.

Exit codes: 0 PASS (or no --expect), 1 FAIL, 2 input or environment error.
Output text is Chinese, like tools in experiments/free_fps_matrix/.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import subprocess
import sys
from pathlib import Path

try:
    import numpy as np
except ImportError:  # pragma: no cover - environment problem
    sys.exit("error: numpy is required (python -m pip install numpy)")


MASK_DELTA = 30         # object pixels: |Y - median(Y)| > MASK_DELTA
SAME_EPS_PX = 0.5       # centroids closer than this are the same position
STEP_TOL_PX = 0.5       # absolute tolerance for step uniformity
STEP_TOL_REL = 0.02     # relative tolerance for step uniformity
SHOW_RUNS = 40          # runs printed on stdout (full list via --csv)
SHOW_DIFFS = 30         # frame differences printed on stdout
EXIT_OK, EXIT_FAIL, EXIT_ERROR = 0, 1, 2


class InputError(Exception):
    """Bad input or environment: message goes to stderr, exit code 2."""


def probe_dimensions(ffprobe: str, path: Path) -> tuple[int, int]:
    proc = subprocess.run(
        [ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
         "-of", "json", str(path)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        raise InputError(f"ffprobe failed on {path.name}: {proc.stderr.decode('utf-8', 'replace').strip()}")
    stream = json.loads(proc.stdout.decode("utf-8", "replace"))["streams"][0]
    return int(stream["width"]), int(stream["height"])


def gray_frames(ffmpeg: str, path: Path, width: int, height: int):
    """Yield the Y plane of each decoded frame, one at a time (no full-video buffer)."""
    command = [
        ffmpeg, "-hide_banner", "-v", "error", "-nostdin", "-noautorotate",
        "-i", str(path), "-map", "0:v:0", "-an", "-sn", "-dn",
        "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "gray", "-",
    ]
    size = width * height
    proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        while True:
            buf = proc.stdout.read(size)
            if len(buf) < size:
                break
            yield np.frombuffer(buf, dtype=np.uint8).reshape(height, width)
    finally:
        proc.stdout.close()
        err = proc.stderr.read().decode("utf-8", "replace").strip()
        rc = proc.wait()
    if rc != 0:
        raise InputError(f"ffmpeg decode of {path.name} failed: {err}")


def centroid_x(frame: np.ndarray, xs: np.ndarray) -> tuple[float, int]:
    """Centroid x (pixel index + 0.5) of the object, NaN if there is none; plus pixel count."""
    median = float(np.median(frame))
    mask = np.abs(frame.astype(np.int16) - median) > MASK_DELTA
    count = int(mask.sum())
    if count == 0:
        return math.nan, 0
    return float(xs[mask].mean()), count


def build_runs(frame_ids: list[int], xs: list[float]) -> list[dict]:
    """Group consecutive present frames whose centroid x is the same position."""
    runs: list[dict] = []
    for fid, x in zip(frame_ids, xs):
        if runs and fid == runs[-1]["end"] + 1 and abs(x - runs[-1]["x0"]) < SAME_EPS_PX:
            runs[-1]["end"] = fid
            runs[-1]["xs"].append(x)
            continue
        runs.append({"start": fid, "end": fid, "x0": x, "xs": [x]})
    for run in runs:
        run["length"] = run["end"] - run["start"] + 1
        run["x"] = sum(run["xs"]) / len(run["xs"])
    return runs


def judge(runs: list[dict], expect: str, step: int) -> tuple[bool, list[str]]:
    reasons: list[str] = []
    if len(runs) < 3:
        return False, [f"位置段太少（{len(runs)} 段），无法判断"]
    interior = runs[1:-1]
    bad_interior = [r for r in interior if r["length"] != step]
    if bad_interior:
        sample = ", ".join(f"帧{r['start']}-{r['end']}({r['length']}帧)" for r in bad_interior[:5])
        reasons.append(f"中间段的长度不都是 {step} 帧：{sample}"
                       + (" ..." if len(bad_interior) > 5 else ""))
    steps = [runs[i + 1]["x"] - runs[i]["x"] for i in range(len(runs) - 1)]
    mean = sum(steps) / len(steps)
    spread = max(steps) - min(steps)
    tol = max(STEP_TOL_PX, STEP_TOL_REL * abs(mean))
    if not (all(s > 0 for s in steps) or all(s < 0 for s in steps)):
        reasons.append("位置变化方向有反转")
    if spread > tol:
        reasons.append(f"相邻位置步长不匀速：最大 {max(steps):.3f} 最小 {min(steps):.3f} px，"
                       f"差 {spread:.3f} > 容差 {tol:.3f} px")
    if expect == "follow" and step != 1:
        reasons.append("follow 的 --step 应为 1")
    return not reasons, reasons


def main(argv: list[str] | None = None) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="Frame-hold step check for the white circle.")
    parser.add_argument("video", type=Path, help="exported MP4")
    parser.add_argument("--expect", choices=("follow", "keep"),
                        help="follow: every frame new; keep: a new position every --step frames")
    parser.add_argument("--step", type=int, default=1,
                        help="frames per interior position (follow 1, keep 4 for 15 fps in 60 fps); "
                             "the first and last runs (stay segments) may be any length; default 1")
    parser.add_argument("--csv", type=Path, help="write per-frame x / dx / run id to this CSV")
    parser.add_argument("--ffmpeg", type=Path, help="ffmpeg executable (default: PATH)")
    parser.add_argument("--ffprobe", type=Path, help="ffprobe executable (default: PATH)")
    args = parser.parse_args(argv)

    try:
        video = args.video.resolve()
        if not video.is_file():
            raise InputError(f"video not found: {video}")
        if args.step < 1:
            raise InputError("--step must be at least 1")
        ffmpeg = str(args.ffmpeg) if args.ffmpeg else "ffmpeg"
        ffprobe = str(args.ffprobe) if args.ffprobe else "ffprobe"
        width, height = probe_dimensions(ffprobe, video)
        xs_grid = np.broadcast_to(np.arange(width, dtype=np.float64) + 0.5, (height, width))
        total = 0
        frame_ids: list[int] = []
        xs: list[float] = []
        per_frame: list[tuple[int, float, int]] = []
        for fid, frame in enumerate(gray_frames(ffmpeg, video, width, height)):
            total += 1
            x, count = centroid_x(frame, xs_grid)
            per_frame.append((fid, x, count))
            if count:
                frame_ids.append(fid)
                xs.append(x)
    except (InputError, OSError, json.JSONDecodeError, KeyError, IndexError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_ERROR

    missing = [fid for fid, x, _ in per_frame if not math.isfinite(x)]
    print(f"文件: {video}")
    print(f"尺寸: {width}x{height}    解码帧数: {total}    有圆的帧: {len(frame_ids)}    无圆的帧: {len(missing)}")
    if missing:
        print(f"无圆的帧范围: {missing[0]} .. {missing[-1]}（仅统计有圆的帧）")
    if not frame_ids:
        print("没有检测到白色圆，无法分析。", file=sys.stderr)
        return EXIT_ERROR

    runs = build_runs(frame_ids, xs)
    print(f"\n位置段（同一位置连续出现的帧数）：共 {len(runs)} 段")
    print(f"{'段':>4} {'帧范围':>14} {'长度':>4} {'质心 x (px)':>13} {'与上一段的步长 (px)':>20}")
    for i, run in enumerate(runs[:SHOW_RUNS]):
        step_txt = "" if i == 0 else f"{run['x'] - runs[i - 1]['x']:+.3f}"
        print(f"{i + 1:>4} {run['start']:>6}-{run['end']:<7} {run['length']:>4} {run['x']:>13.3f} {step_txt:>20}")
    if len(runs) > SHOW_RUNS:
        print(f"... 共 {len(runs)} 段，只显示前 {SHOW_RUNS} 段（完整数据用 --csv）")

    hist: dict[int, int] = {}
    for run in runs:
        hist[run["length"]] = hist.get(run["length"], 0) + 1
    print("段长分布（长度: 段数）: " + ", ".join(f"{k}: {v}" for k, v in sorted(hist.items())))

    diffs = [(frame_ids[i], xs[i] - xs[i - 1]) for i in range(1, len(xs)) if frame_ids[i] == frame_ids[i - 1] + 1]
    print(f"\n逐帧差分 dx（前 {min(SHOW_DIFFS, len(diffs))} 个，单位 px）：")
    print("  " + " ".join(f"{d:+.2f}" for _, d in diffs[:SHOW_DIFFS]))
    zero = sum(1 for _, d in diffs if abs(d) < SAME_EPS_PX)
    print(f"相邻帧 dx 近似为 0 的次数: {zero} / {len(diffs)}")

    if args.csv:
        with args.csv.open("w", newline="", encoding="utf-8") as fh:
            writer = csv.writer(fh)
            writer.writerow(["frame", "centroid_x", "pixels", "dx", "run"])
            run_of: dict[int, int] = {}
            for i, run in enumerate(runs):
                for fid in range(run["start"], run["end"] + 1):
                    run_of[fid] = i + 1
            dx_of = {fid: d for fid, d in diffs}
            for fid, x, count in per_frame:
                writer.writerow([fid, "" if not math.isfinite(x) else f"{x:.4f}", count,
                                 f"{dx_of[fid]:.4f}" if fid in dx_of else "", run_of.get(fid, "")])
        print(f"逐帧数据已写入: {args.csv}")

    if args.expect is None:
        return EXIT_OK
    ok, reasons = judge(runs, args.expect, args.step)
    print(f"\n判定（--expect {args.expect} --step {args.step}）: {'PASS' if ok else 'FAIL'}")
    for reason in reasons:
        print(f"  - {reason}")
    return EXIT_OK if ok else EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main())
