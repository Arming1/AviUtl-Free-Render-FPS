#!/usr/bin/env python3
"""Measure audio/picture sync of a rendered MP4 against the long_av_sync markers.

Usage:
    python -I experiments/long_av_sync/analyze_sync.py MP4 --target-fps RATE
        [--ffmpeg PATH] [--ffprobe PATH] [--enc-tolerance-ms 20]
        [--max-slope-ms-per-min 1.0] [--pairs-csv PATH]
    python -I experiments/long_av_sync/analyze_sync.py --self-test
        [--ffmpeg PATH] [--ffprobe PATH]

RATE is a decimal or a fraction, e.g. 66, 50, 60000/1001.

Method
  Video: each frame is area-averaged to 32x18 gray and reduced to its mean
         luma. A flash is a rising edge across the midpoint between the median
         (black) and the maximum (white). Timestamp = frame pts (ffprobe), or
         index / RATE if pts is unavailable.
  Audio: decoded to 48 kHz mono s16. The peak |sample| of each 1 ms block is
         the envelope. A beep is a rising edge across 50 % of the global peak.
         Timestamp = block start, so resolution is 1 ms.
  Pairing: each flash is paired with the nearest beep within 0.5 s.
           offset = flash - beep. Positive means the picture is late.

Verdict (all must hold):
  1. max |offset| <= 0.5 / RATE + enc-tolerance-ms
  2. |drift slope| (least squares of offset vs time) <= max-slope-ms-per-min
  3. |(V - A) / A| <= 0.01 %, V and A being the video and audio stream durations
  4. every flash pairs with exactly one beep and every beep with one flash

Exit codes: 0 PASS, 1 FAIL, 2 tool or input error.
"""

from __future__ import annotations

import argparse
import array
import bisect
import csv
import json
import os
import statistics
import subprocess
import sys
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
AUDIO_RATE = 48000
BLOCK = 48  # 1 ms of audio at AUDIO_RATE
LUMA_W, LUMA_H = 32, 18
PAIR_WINDOW_S = 0.5
HEAD_TAIL_S = 60.0
MIN_FLASH_CONTRAST = 40.0  # luma levels, 0..255
MIN_BEEP_PEAK = 2000  # int16 units, about 6 % of full scale
REL_DURATION_LIMIT = 1e-4  # 0.01 %

# Self-test: 100 fps frame grid makes +10 ms exactly one frame.
SELFTEST_FPS = 100
SELFTEST_OFFSET_MS = 10.0
SELFTEST_TOLERANCE_MS = 2.0
SELFTEST_SECONDS = 60


class ToolError(Exception):
    pass


def parse_rate(text: str) -> float:
    try:
        value = float(Fraction(text))
    except (ValueError, ZeroDivisionError) as exc:
        raise argparse.ArgumentTypeError(f"bad rate: {text!r}") from exc
    if value <= 0:
        raise argparse.ArgumentTypeError(f"rate must be positive: {text!r}")
    return value


def run_capture(cmd: list[str]) -> str:
    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except FileNotFoundError as exc:
        raise ToolError(f"tool not found: {cmd[0]}") from exc
    if proc.returncode != 0:
        err = proc.stderr.decode("utf-8", errors="replace").strip()
        raise ToolError(f"{cmd[0]} exited {proc.returncode}: {err}")
    return proc.stdout.decode("utf-8", errors="replace")


def read_exact(stream, size: int) -> bytes:
    chunks = []
    got = 0
    while got < size:
        data = stream.read(size - got)
        if not data:
            break
        chunks.append(data)
        got += len(data)
    return b"".join(chunks)


def open_decoder(cmd: list[str]) -> subprocess.Popen:
    try:
        return subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except FileNotFoundError as exc:
        raise ToolError(f"tool not found: {cmd[0]}") from exc


def finish_decoder(proc: subprocess.Popen) -> None:
    err = proc.stderr.read().decode("utf-8", errors="replace").strip()
    proc.stdout.close()
    rc = proc.wait()
    if rc != 0:
        raise ToolError(f"decoder exited {rc}: {err}")


def probe_streams(ffprobe: str, path: str) -> dict[str, dict]:
    text = run_capture(
        [
            ffprobe,
            "-v", "error",
            "-show_entries", "stream=codec_type,start_time,duration",
            "-of", "json",
            path,
        ]
    )
    streams = {}
    for stream in json.loads(text).get("streams", []):
        kind = stream.get("codec_type")
        if kind in ("video", "audio") and kind not in streams:
            streams[kind] = stream
    for kind in ("video", "audio"):
        if kind not in streams:
            raise ToolError(f"no {kind} stream in {path}")
    return streams


def stream_duration(stream: dict) -> float | None:
    value = stream.get("duration")
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def video_timestamps(ffprobe: str, path: str) -> list[float] | None:
    text = run_capture(
        [
            ffprobe,
            "-v", "error",
            "-select_streams", "v:0",
            "-show_entries", "frame=best_effort_timestamp_time",
            "-of", "csv=p=0",
            path,
        ]
    )
    stamps = []
    for line in text.splitlines():
        token = line.strip().rstrip(",")
        if not token:
            continue
        if token == "N/A":
            return None
        stamps.append(float(token))
    return stamps


def luma_per_frame(ffmpeg: str, path: str) -> list[float]:
    size = LUMA_W * LUMA_H
    cmd = [
        ffmpeg, "-v", "error", "-nostdin", "-i", path, "-an",
        "-vf", f"scale={LUMA_W}:{LUMA_H}:flags=area,format=gray",
        "-f", "rawvideo", "-",
    ]
    proc = open_decoder(cmd)
    values = []
    while True:
        buf = read_exact(proc.stdout, size)
        if len(buf) < size:
            break
        values.append(sum(buf) / size)
    finish_decoder(proc)
    if not values:
        raise ToolError("no decodable video frames")
    return values


def audio_envelope(ffmpeg: str, path: str) -> tuple[array.array, int]:
    """Peak |sample| per 1 ms block, and the number of decoded samples."""
    cmd = [
        ffmpeg, "-v", "error", "-nostdin", "-i", path, "-vn",
        "-ac", "1", "-ar", str(AUDIO_RATE), "-f", "s16le", "-",
    ]
    proc = open_decoder(cmd)
    envelope = array.array("i")
    total = 0
    chunk = AUDIO_RATE  # one second per read
    while True:
        buf = read_exact(proc.stdout, chunk * 2)
        count = len(buf) // 2
        if count == 0:
            break
        samples = array.array("h")
        samples.frombytes(buf[: count * 2])
        if sys.byteorder == "big":
            samples.byteswap()
        for start in range(0, count, BLOCK):
            envelope.append(max(map(abs, samples[start:start + BLOCK])))
        total += count
        if count < chunk:
            break
    finish_decoder(proc)
    if total == 0:
        raise ToolError("no decodable audio samples")
    return envelope, total


def rising_indices(values, threshold: float) -> list[int]:
    found = []
    above = False
    for index, value in enumerate(values):
        now = value >= threshold
        if now and not above:
            found.append(index)
        above = now
    return found


def detect_flashes(luma: list[float], stamps: list[float]) -> list[float]:
    black = statistics.median(luma)
    white = max(luma)
    if white - black < MIN_FLASH_CONTRAST:
        raise ToolError(
            f"no flash contrast (median {black:.1f}, max {white:.1f}); "
            "is this the long_av_sync render?"
        )
    threshold = (black + white) / 2.0
    return [stamps[i] for i in rising_indices(luma, threshold)]


def detect_beeps(envelope: array.array) -> list[float]:
    peak = max(envelope)
    if peak < MIN_BEEP_PEAK:
        raise ToolError(f"no beep found (peak |sample| {peak})")
    threshold = 0.5 * peak
    return [index / 1000.0 for index in rising_indices(envelope, threshold)]


def pair_events(flashes: list[float], beeps: list[float]):
    """Return (pairs, unmatched_flashes, unmatched_beeps, duplicates).

    pairs holds (beep_time_s, offset_ms, flash_time_s), offset = flash - beep.
    """
    pairs = []
    unmatched_flashes = 0
    for flash in flashes:
        pos = bisect.bisect_left(beeps, flash)
        candidates = [k for k in (pos - 1, pos) if 0 <= k < len(beeps)]
        if not candidates:
            unmatched_flashes += 1
            continue
        best = min(candidates, key=lambda k: abs(beeps[k] - flash))
        if abs(beeps[best] - flash) > PAIR_WINDOW_S:
            unmatched_flashes += 1
            continue
        pairs.append((best, beeps[best], (flash - beeps[best]) * 1000.0, flash))
    used = {index for index, _, _, _ in pairs}
    duplicates = len(pairs) - len(used)
    unmatched_beeps = len(beeps) - len(used)
    return pairs, unmatched_flashes, unmatched_beeps, duplicates


def slope_ms_per_min(times_s: list[float], offsets_ms: list[float]) -> float:
    xs = [t / 60.0 for t in times_s]
    mean_x = sum(xs) / len(xs)
    mean_y = sum(offsets_ms) / len(offsets_ms)
    sxx = sum((x - mean_x) ** 2 for x in xs)
    sxy = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, offsets_ms))
    return sxy / sxx if sxx > 0 else 0.0


def mean_or_none(values: list[float]) -> float | None:
    return sum(values) / len(values) if values else None


def analyze(ffmpeg: str, ffprobe: str, path: str, target_fps: float,
            enc_tolerance_ms: float, max_slope: float):
    streams = probe_streams(ffprobe, path)
    v_stream, a_stream = streams["video"], streams["audio"]

    stamps = video_timestamps(ffprobe, path)
    luma = luma_per_frame(ffmpeg, path)
    if stamps is None or len(stamps) != len(luma):
        stamps = [i / target_fps for i in range(len(luma))]
        stamp_source = "index/RATE (ffprobe pts unavailable or mismatched)"
    else:
        stamp_source = "ffprobe pts"

    flashes = detect_flashes(luma, stamps)
    envelope, audio_samples = audio_envelope(ffmpeg, path)
    beeps = detect_beeps(envelope)

    pairs, unmatched_flashes, unmatched_beeps, duplicates = pair_events(flashes, beeps)
    if len(pairs) < 2:
        raise ToolError(f"too few matched pairs ({len(pairs)})")

    offsets = [offset for _, _, offset, _ in pairs]
    pair_times = [beep_time for _, beep_time, _, _ in pairs]
    video_s = stream_duration(v_stream) or len(luma) / target_fps
    audio_s = stream_duration(a_stream) or audio_samples / AUDIO_RATE
    rel_error = (video_s - audio_s) / audio_s

    head = [o for t, o in zip(pair_times, offsets) if t < HEAD_TAIL_S]
    tail = [o for t, o in zip(pair_times, offsets) if t >= video_s - HEAD_TAIL_S]
    slope = slope_ms_per_min(pair_times, offsets)
    max_abs = max(abs(o) for o in offsets)
    tolerance_ms = 0.5 * 1000.0 / target_fps + enc_tolerance_ms

    checks = [
        ("max |offset| <= half target frame + encode tolerance",
         max_abs <= tolerance_ms, f"{max_abs:.2f} ms <= {tolerance_ms:.2f} ms"),
        ("|drift slope| <= limit",
         abs(slope) <= max_slope, f"{slope:+.3f} ms/min, limit {max_slope} ms/min"),
        ("|(V - A) / A| <= 0.01 %",
         abs(rel_error) <= REL_DURATION_LIMIT, f"{rel_error * 100:+.4f} %"),
        ("flash/beep one-to-one",
         unmatched_flashes == 0 and unmatched_beeps == 0 and duplicates == 0,
         f"unmatched flashes {unmatched_flashes}, beeps {unmatched_beeps}, duplicates {duplicates}"),
    ]
    result = {
        "file": path,
        "target_fps": target_fps,
        "stamp_source": stamp_source,
        "frames_decoded": len(luma),
        "frames_expected": round(video_s * target_fps),
        "flashes": len(flashes),
        "beeps": len(beeps),
        "pairs": len(pairs),
        "offset_ms": {
            "max_signed": max(offsets),
            "min_signed": min(offsets),
            "max_abs": max_abs,
            "mean": sum(offsets) / len(offsets),
            "head_1min_mean": mean_or_none(head),
            "tail_1min_mean": mean_or_none(tail),
            "slope_ms_per_min": slope,
        },
        "duration_s": {
            "video": video_s,
            "audio": audio_s,
            "rel_error_pct": rel_error * 100.0,
            "video_start": float(v_stream.get("start_time") or 0.0),
            "audio_start": float(a_stream.get("start_time") or 0.0),
        },
        "tolerance_ms": tolerance_ms,
        "checks": [{"name": n, "pass": p, "detail": d} for n, p, d in checks],
        "pass": all(p for _, p, _ in checks),
    }
    return result, pairs


def print_report(result: dict) -> None:
    o = result["offset_ms"]
    d = result["duration_s"]

    def fmt(value):
        return "n/a" if value is None else f"{value:+.2f}"

    print(f"文件: {result['file']}")
    print(f"目标帧率: {result['target_fps']:.4f}  时间戳来源: {result['stamp_source']}")
    print(f"视频帧数: 解码 {result['frames_decoded']} / 期望 {result['frames_expected']}")
    print(f"闪白数: {result['flashes']}  嘀声数: {result['beeps']}  配对数: {result['pairs']}")
    print("音画偏移 (ms, 正值 = 画面晚于声音):")
    print(f"  最大值 {fmt(o['max_signed'])}  最小值 {fmt(o['min_signed'])}  最大绝对值 {o['max_abs']:.2f}")
    print(f"  均值 {fmt(o['mean'])}  首1分钟均值 {fmt(o['head_1min_mean'])}  末1分钟均值 {fmt(o['tail_1min_mean'])}")
    print(f"  线性回归斜率 {o['slope_ms_per_min']:+.3f} ms/min")
    print("时长 (s):")
    print(f"  视频 {d['video']:.3f} (start {d['video_start']:.3f})  音频 {d['audio']:.3f} (start {d['audio_start']:.3f})")
    print(f"  相对误差 (V-A)/A = {d['rel_error_pct']:+.4f} %")
    print("判定:")
    for check in result["checks"]:
        mark = "PASS" if check["pass"] else "FAIL"
        print(f"  [{mark}] {check['name']}: {check['detail']}")
    print(f"总判定: {'PASS' if result['pass'] else 'FAIL'}")


def write_pairs_csv(path: str, pairs) -> None:
    with open(path, "w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["beep_time_s", "flash_time_s", "offset_ms"])
        for _, beep_time, offset, flash_time in pairs:
            writer.writerow([f"{beep_time:.3f}", f"{flash_time:.6f}", f"{offset:.3f}"])


def run_self_test(ffmpeg: str, ffprobe: str) -> int:
    out_dir = os.path.join(HERE, "out")
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, f"selftest_offset{int(SELFTEST_OFFSET_MS)}ms_{SELFTEST_FPS}fps.mp4")
    # Beeps start at whole seconds. Flash frames sit at 10 ms..30 ms into each
    # second, so the first flash frame is at k + 0.010 s: picture 10 ms late.
    video_filter = "geq=lum='if(between(mod(T,1),0.005,0.025),235,16)':cb=128:cr=128"
    audio_src = "aevalsrc='0.8*sin(2*PI*1000*t)*lt(mod(t,1),0.02)':s=48000:d=60:c=mono"
    cmd = [
        ffmpeg, "-v", "error", "-y",
        "-f", "lavfi", "-i", f"color=c=black:s=320x180:r={SELFTEST_FPS}:d={SELFTEST_SECONDS}",
        "-f", "lavfi", "-i", audio_src,
        "-map", "0:v", "-map", "1:a",
        "-vf", video_filter,
        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18",
        "-c:a", "aac", "-b:a", "192k",
        "-t", str(SELFTEST_SECONDS),
        path,
    ]
    run_capture(cmd)
    result, pairs = analyze(ffmpeg, ffprobe, path, float(SELFTEST_FPS),
                            enc_tolerance_ms=20.0, max_slope=1.0)
    print_report(result)
    mean = result["offset_ms"]["mean"]
    ok = abs(mean - SELFTEST_OFFSET_MS) <= SELFTEST_TOLERANCE_MS
    print(f"自测: 期望 +{SELFTEST_OFFSET_MS:.1f} ms, 测得均值 {mean:+.2f} ms, "
          f"容差 ±{SELFTEST_TOLERANCE_MS} ms -> {'PASS' if ok else 'FAIL'}")
    print(f"合成文件: {path}")
    return 0 if ok else 1


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    parser.add_argument("mp4", nargs="?", help="rendered MP4 to analyse")
    parser.add_argument("--target-fps", type=parse_rate, help="target output rate, e.g. 66 or 60000/1001")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument("--enc-tolerance-ms", type=float, default=20.0,
                        help="allowed encoder/container delay added to half a frame (default 20)")
    parser.add_argument("--max-slope-ms-per-min", type=float, default=1.0,
                        help="allowed drift slope (default 1.0 ms/min, 30 ms over 30 min)")
    parser.add_argument("--pairs-csv", help="write per-beep offsets to this CSV")
    parser.add_argument("--self-test", action="store_true",
                        help="generate a 60 s MP4 with a known +10 ms offset and check the analyser")
    args = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")

    try:
        if args.self_test:
            return run_self_test(args.ffmpeg, args.ffprobe)
        if not args.mp4 or args.target_fps is None:
            parser.error("MP4 and --target-fps are required (or use --self-test)")
        if not os.path.isfile(args.mp4):
            print(f"input not found: {args.mp4}", file=sys.stderr)
            return 2
        result, pairs = analyze(args.ffmpeg, args.ffprobe, args.mp4, args.target_fps,
                                args.enc_tolerance_ms, args.max_slope_ms_per_min)
    except ToolError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    print_report(result)
    if args.pairs_csv:
        write_pairs_csv(args.pairs_csv, pairs)
        print(f"pairs written to {args.pairs_csv}")
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
