#!/usr/bin/env python3
"""Validate a FreeRenderFPS output video from its decoded pixels alone.

The video is decoded once through ffmpeg into 8-bit luma (optionally scaled
down to at most 640 px wide). Every frame gets a SHA-1 of its luma bytes, and
consecutive frames get a mean absolute difference (MAD, 0-255 scale). The
checks are described in tools/validate_freefps_output.md.

Requires ffmpeg/ffprobe 5.1 or newer (for -fps_mode) and numpy.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path
from typing import Iterable, Iterator

try:
    import numpy as np
except ImportError:  # pragma: no cover - environment problem
    sys.exit("error: numpy is required (python -m pip install numpy)")


REPORT_VERSION = 1
DECODE_MAX_WIDTH = 640
PASS, FAIL, NA, WARN = "PASS", "FAIL", "N/A", "WARN"
EXIT_OK, EXIT_FAIL, EXIT_ERROR = 0, 1, 2
RATIONAL_RE = re.compile(r"\d+(?:/\d+)?")
PRINT_LIMIT = 20


class ToolError(Exception):
    """Environment or input problem: message goes to stderr, exit code 2."""


@dataclass(frozen=True)
class Settings:
    project_fps: Fraction
    target_fps: Fraction
    project_frames: int
    project_duration: Fraction | None
    near_dup_threshold: float
    near_dup_relative: float
    pairing_min_ratio: float
    max_near_duplicates: int


@dataclass
class LumaScan:
    # frame_sha1[k] is the hash of output frame k.
    frame_sha1: list[str]
    # mad_by_boundary[i] (i >= 1) is the MAD between output frames i-1 and i.
    # Index 0 has no previous frame and is None.
    mad_by_boundary: list[float | None]


# ---------------------------------------------------------------- argument parsing

def parse_rational(text: str, option: str) -> Fraction:
    token = text.strip()
    if not RATIONAL_RE.fullmatch(token):
        raise argparse.ArgumentTypeError(
            f"{option} {text!r} is not an exact rational. Use an integer or "
            "numerator/denominator such as 60000/1001. Decimals such as 59.94 "
            "are rejected on purpose.")
    try:
        value = Fraction(token)
    except ZeroDivisionError:
        raise argparse.ArgumentTypeError(f"{option} {text!r} has a zero denominator") from None
    if value <= 0:
        raise argparse.ArgumentTypeError(f"{option} must be positive, got {text!r}")
    return value


def positive_int(text: str) -> int:
    if not re.fullmatch(r"\d+", text.strip()) or int(text) < 1:
        raise argparse.ArgumentTypeError(f"expected a positive integer, got {text!r}")
    return int(text)


def nonnegative_int(text: str) -> int:
    if not re.fullmatch(r"\d+", text.strip()):
        raise argparse.ArgumentTypeError(f"expected a non-negative integer, got {text!r}")
    return int(text)


def nonnegative_float(text: str) -> float:
    try:
        value = float(text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"expected a number, got {text!r}") from None
    if not value >= 0:
        raise argparse.ArgumentTypeError(f"expected a non-negative number, got {text!r}")
    return value


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Validate a FreeRenderFPS output MP4 from decoded frames.",
        epilog="See tools/validate_freefps_output.md for the checks and examples.",
    )
    parser.add_argument("video", type=Path, help="output video to validate")
    parser.add_argument("--project-fps", required=True,
                        type=lambda t: parse_rational(t, "--project-fps"),
                        help="project frame rate, e.g. 30 or 66")
    parser.add_argument("--target-fps", required=True,
                        type=lambda t: parse_rational(t, "--target-fps"),
                        help="requested output frame rate, e.g. 60 or 60000/1001")
    length = parser.add_mutually_exclusive_group(required=True)
    length.add_argument("--project-frames", type=positive_int,
                        help="project length in project frames")
    length.add_argument("--project-duration", metavar="SECONDS",
                        type=lambda t: parse_rational(t, "--project-duration"),
                        help="project length in seconds (exact rational)")
    parser.add_argument("--reference", type=Path,
                        help="FreeRenderFPS-OFF export of the same project at project fps "
                             "(informational only)")
    parser.add_argument("--near-dup-threshold", type=nonnegative_float, default=0.0,
                        help="absolute MAD floor for near-duplicates (default 0)")
    parser.add_argument("--near-dup-relative", type=nonnegative_float, default=0.1,
                        help="a boundary is a near-duplicate when its MAD is below "
                             "max(threshold, relative * median boundary MAD); default 0.1. "
                             "A fixed absolute threshold cannot separate low-motion test "
                             "scenes, whose real motion MAD can be well under 0.5")
    parser.add_argument("--pairing-min-ratio", type=nonnegative_float, default=0.25,
                        help="minimum in-group/out-of-group median MAD for no_pairing (default 0.25)")
    parser.add_argument("--max-near-duplicates", type=nonnegative_int, default=1,
                        help="allowed near-duplicate boundaries after the end clamp is ignored (default 1)")
    parser.add_argument("--json", type=Path, help="write all raw numbers and checks to this file")
    parser.add_argument("--ffmpeg", type=Path, help="ffmpeg executable (default: PATH)")
    parser.add_argument("--ffprobe", type=Path, help="ffprobe executable (default: PATH)")
    return parser


# ---------------------------------------------------------------- tool helpers

def fstr(value: Fraction) -> str:
    return f"{value.numerator}/{value.denominator}"


def parse_ffprobe_rate(text: str | None) -> Fraction | None:
    if not text:
        return None
    try:
        value = Fraction(text)
    except (ValueError, ZeroDivisionError):
        return None
    return value if value > 0 else None


def parse_seconds(text: str | None) -> Fraction | None:
    if text is None:
        return None
    try:
        return Fraction(text)
    except (ValueError, ZeroDivisionError):
        return None


def resolve_tool(name: str, override: Path | None) -> str:
    if override is not None:
        if not override.is_file():
            raise ToolError(f"--{name} does not exist: {override}")
        return str(override)
    found = shutil.which(name)
    if found is None:
        raise ToolError(f"'{name}' was not found on PATH. Install a full FFmpeg build "
                        f"and add its bin directory to PATH, or pass --{name} PATH.")
    return found


def run_capture(command: list[str]) -> bytes:
    try:
        proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    except OSError as exc:
        raise ToolError(f"cannot run {command[0]}: {exc}") from exc
    if proc.returncode != 0:
        detail = proc.stderr.decode("utf-8", "replace").strip()
        raise ToolError(f"{command[0]} failed with exit code {proc.returncode}: {detail}")
    return proc.stdout


def ffprobe_json(ffprobe: str, extra: list[str], path: Path) -> dict:
    out = run_capture([ffprobe, "-v", "error", *extra, "-of", "json", str(path)])
    return json.loads(out.decode("utf-8", "replace"))


def ffmpeg_version_line(ffmpeg: str) -> str:
    out = run_capture([ffmpeg, "-hide_banner", "-version"]).decode("utf-8", "replace")
    return out.splitlines()[0].strip()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


# ---------------------------------------------------------------- probing

def probe_media(ffprobe: str, path: Path) -> dict:
    data = ffprobe_json(ffprobe, ["-show_streams", "-show_format"], path)
    streams = data.get("streams", [])
    video_streams = [s for s in streams if s.get("codec_type") == "video"]
    audio_streams = [s for s in streams if s.get("codec_type") == "audio"]
    if not video_streams:
        raise ToolError(f"{path} has no video stream")
    stream = video_streams[0]

    counted = ffprobe_json(
        ffprobe,
        ["-count_frames", "-select_streams", "v:0", "-show_entries", "stream=nb_read_frames"],
        path,
    )
    counted_streams = counted.get("streams", [])
    nb_read = None
    if counted_streams and "nb_read_frames" in counted_streams[0]:
        nb_read = int(counted_streams[0]["nb_read_frames"])

    media = {
        "video": {
            "codec_name": stream.get("codec_name"),
            "pix_fmt": stream.get("pix_fmt"),
            "width": int(stream["width"]),
            "height": int(stream["height"]),
            "r_frame_rate": stream.get("r_frame_rate"),
            "avg_frame_rate": stream.get("avg_frame_rate"),
            "duration": stream.get("duration"),
            "nb_read_frames": nb_read,
        },
        "audio": None,
        "format_duration": data.get("format", {}).get("duration"),
    }
    if audio_streams:
        audio = audio_streams[0]
        media["audio"] = {
            "codec_name": audio.get("codec_name"),
            "sample_rate": audio.get("sample_rate"),
            "channels": audio.get("channels"),
            "duration": audio.get("duration"),
        }
    return media


# ---------------------------------------------------------------- decoding

def decode_geometry(width: int, height: int) -> tuple[int, int]:
    if width <= DECODE_MAX_WIDTH:
        return width, height
    return DECODE_MAX_WIDTH, max(1, round(height * DECODE_MAX_WIDTH / width))


def read_exact(stream, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size
    while remaining > 0:
        chunk = stream.read(remaining)
        if not chunk:
            break
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def iter_luma_frames(ffmpeg: str, path: Path, width: int, height: int) -> Iterator[bytes]:
    out_w, out_h = decode_geometry(width, height)
    command = [
        ffmpeg, "-hide_banner", "-v", "error", "-nostdin", "-noautorotate",
        "-i", str(path), "-map", "0:v:0", "-an", "-sn", "-dn",
    ]
    if (out_w, out_h) != (width, height):
        command += ["-vf", f"scale={out_w}:{out_h}:flags=area"]
    # passthrough: ffmpeg must not duplicate or drop frames while writing the
    # pipe. Duplication is exactly what this tool is meant to detect.
    command += ["-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "gray", "-"]
    frame_bytes = out_w * out_h

    with tempfile.TemporaryFile() as err:
        proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=err)
        assert proc.stdout is not None
        exhausted = False
        try:
            while True:
                buffer = read_exact(proc.stdout, frame_bytes)
                if not buffer:
                    break
                if len(buffer) != frame_bytes:
                    raise ToolError(f"truncated frame from ffmpeg: {len(buffer)} of {frame_bytes} bytes")
                yield buffer
            exhausted = True
        finally:
            # Only kill ffmpeg when the consumer stopped early or an error
            # was raised; after EOF, wait for it to exit normally.
            if not exhausted and proc.poll() is None:
                proc.kill()
            proc.stdout.close()
            returncode = proc.wait()
        if returncode != 0:
            err.seek(0)
            detail = err.read().decode("utf-8", "replace").strip()
            raise ToolError(f"ffmpeg decode of {path} failed with exit code {returncode}: {detail}")


def scan_luma(frames: Iterable[bytes]) -> LumaScan:
    hashes: list[str] = []
    mads: list[float | None] = []
    previous = None
    for buffer in frames:
        hashes.append(hashlib.sha1(buffer).hexdigest())
        current = np.frombuffer(buffer, dtype=np.uint8).astype(np.int16)
        mads.append(None if previous is None else float(np.abs(current - previous).mean()))
        previous = current
    return LumaScan(frame_sha1=hashes, mad_by_boundary=mads)


# ---------------------------------------------------------------- metrics

def pairing_score(mad: list[float | None], rate_ratio: Fraction, min_ratio: float) -> dict:
    if rate_ratio.denominator != 1 or rate_ratio < 2:
        return {"applicable": False,
                "reason": f"target/project ratio {fstr(rate_ratio)} is not an integer >= 2"}
    r = int(rate_ratio)
    boundaries = range(1, len(mad))
    in_group = [mad[i] for i in boundaries if i % r != 0]
    out_group = [mad[i] for i in boundaries if i % r == 0]
    if not in_group or not out_group:
        return {"applicable": False, "reason": "too few output frames for a pairing score"}
    in_median = float(np.median(in_group))
    out_median = float(np.median(out_group))
    return {
        "applicable": True,
        "r": r,
        "in_group_boundaries": len(in_group),
        "out_group_boundaries": len(out_group),
        "in_group_median_mad": in_median,
        "out_group_median_mad": out_median,
        "in_over_out": (in_median / out_median) if out_median > 0 else None,
        "min_ratio": min_ratio,
    }


def format_positions(positions: list[int]) -> str:
    if not positions:
        return "none"
    shown = ", ".join(f"{i} (frames {i - 1}-{i})" for i in positions[:PRINT_LIMIT])
    if len(positions) > PRINT_LIMIT:
        shown += f", ... (+{len(positions) - PRINT_LIMIT} more)"
    return shown


def add_check(checks: list[dict], name: str, status: str, detail: str, values: dict) -> None:
    checks.append({"name": name, "status": status, "detail": detail, "values": values})


# ---------------------------------------------------------------- analysis

def sha1_digest_count(hashes: list[str]) -> int:
    return len(set(hashes))


def analyse(video: Path, reference: Path | None, ffmpeg: str, ffprobe: str,
            settings: Settings) -> dict:
    media = probe_media(ffprobe, video)
    vid = media["video"]
    aud = media["audio"]
    target, project = settings.target_fps, settings.project_fps
    rate_ratio = target / project
    expected_frames = math.ceil(Fraction(settings.project_frames) * target / project)
    expected_duration = Fraction(expected_frames) / target
    decode_w, decode_h = decode_geometry(vid["width"], vid["height"])

    scan = scan_luma(iter_luma_frames(ffmpeg, video, vid["width"], vid["height"]))
    n = len(scan.frame_sha1)
    mad = scan.mad_by_boundary
    measured = [m for m in mad[1:] if m is not None]
    median_mad = float(np.median(measured)) if measured else 0.0
    threshold = max(settings.near_dup_threshold, settings.near_dup_relative * median_mad)

    exact = [i for i in range(1, n) if scan.frame_sha1[i] == scan.frame_sha1[i - 1]]
    near = [i for i in range(1, n) if mad[i] < threshold]
    end_ignored = 1 if n >= 2 and (n - 1) in near else 0
    near_counted = len(near) - end_ignored
    pairing = pairing_score(mad, rate_ratio, settings.pairing_min_ratio)
    boundary_mads = np.array(mad[1:], dtype=np.float64) if n >= 2 else np.array([])
    mad_stats = None
    if boundary_mads.size:
        mad_stats = {
            "min": float(boundary_mads.min()),
            "median": float(np.median(boundary_mads)),
            "mean": float(boundary_mads.mean()),
            "max": float(boundary_mads.max()),
        }

    checks: list[dict] = []

    ffprobe_count = vid["nb_read_frames"]
    frame_ok = n == expected_frames and ffprobe_count == n
    add_check(checks, "frame_count", PASS if frame_ok else FAIL,
              f"decoded {n}, expected {expected_frames}, ffprobe -count_frames {ffprobe_count}",
              {"decoded": n, "expected": expected_frames, "ffprobe_nb_read_frames": ffprobe_count})

    stream_rate = parse_ffprobe_rate(vid["r_frame_rate"])
    rate_ok = stream_rate is not None and stream_rate == target
    add_check(checks, "stream_rate", PASS if rate_ok else FAIL,
              f"r_frame_rate {vid['r_frame_rate']} vs target {fstr(target)}",
              {"r_frame_rate": vid["r_frame_rate"], "target": fstr(target)})

    video_duration = parse_seconds(vid["duration"])
    audio_duration = parse_seconds(aud["duration"]) if aud else None
    tolerance = 1 / target
    if aud is None:
        add_check(checks, "av_duration", NA, "no audio stream", {})
    elif video_duration is None or audio_duration is None:
        add_check(checks, "av_duration", FAIL, "video or audio duration not reported",
                  {"video_duration": vid["duration"], "audio_duration": aud["duration"]})
    else:
        delta = abs(video_duration - audio_duration)
        add_check(checks, "av_duration", PASS if delta < tolerance else FAIL,
                  f"video {float(video_duration):.6f} s, audio {float(audio_duration):.6f} s, "
                  f"|delta| {float(delta):.6f} s, tolerance < {float(tolerance):.6f} s",
                  {"video_duration": vid["duration"], "audio_duration": aud["duration"],
                   "abs_delta_seconds": float(delta), "tolerance_seconds": float(tolerance)})

    if pairing["applicable"]:
        in_med = pairing["in_group_median_mad"]
        out_med = pairing["out_group_median_mad"]
        if out_med <= 0:
            status = FAIL
            detail = "out-of-group median MAD is 0, so there is no motion to compare against"
        else:
            status = PASS if in_med >= settings.pairing_min_ratio * out_med else FAIL
            detail = (f"r={pairing['r']} in-group median MAD {in_med:.4f} vs out-of-group "
                      f"{out_med:.4f}, ratio {in_med / out_med:.4f}, need >= {settings.pairing_min_ratio}")
        add_check(checks, "no_pairing", status, detail, pairing)
    else:
        add_check(checks, "no_pairing", NA, pairing["reason"], pairing)

    near_ok = near_counted <= settings.max_near_duplicates
    # Advisory only: a slowly moving scene legitimately produces adjacent
    # frames that are identical or nearly so, so this cannot prove
    # duplication. Paired duplication is judged by no_pairing.
    add_check(checks, "near_duplicates", PASS if near_ok else WARN,
              f"{len(near)} boundaries with MAD < {threshold}; {end_ignored} at the final "
              f"boundary ignored (end clamp); {near_counted} counted, limit {settings.max_near_duplicates}",
              {"near_duplicate_boundaries": near, "ignored_final_boundary": end_ignored,
               "counted": near_counted, "limit": settings.max_near_duplicates})

    reference_info = None
    if reference is not None:
        ref_media = probe_media(ffprobe, reference)
        ref_vid = ref_media["video"]
        ref_scan = scan_luma(iter_luma_frames(ffmpeg, reference, ref_vid["width"], ref_vid["height"]))
        reference_info = {
            "path": str(reference.resolve()),
            "width": ref_vid["width"],
            "height": ref_vid["height"],
            "r_frame_rate": ref_vid["r_frame_rate"],
            "avg_frame_rate": ref_vid["avg_frame_rate"],
            "duration": ref_vid["duration"],
            "ffprobe_nb_read_frames": ref_vid["nb_read_frames"],
            "decoded_frames": len(ref_scan.frame_sha1),
            "unique_frame_hashes": sha1_digest_count(ref_scan.frame_sha1),
            "audio": ref_media["audio"],
        }

    overall = FAIL if any(c["status"] == FAIL for c in checks) else PASS

    return {
        "tool": {"name": Path(__file__).name, "report_version": REPORT_VERSION},
        "ffmpeg_version": ffmpeg_version_line(ffmpeg),
        "ffmpeg": ffmpeg,
        "ffprobe": ffprobe,
        "input": {
            "path": str(video.resolve()),
            "size_bytes": video.stat().st_size,
            "sha256": sha256_file(video),
        },
        "settings": {
            "project_fps": fstr(project),
            "target_fps": fstr(target),
            "project_frames": settings.project_frames,
            "project_duration_seconds": fstr(settings.project_duration) if settings.project_duration else None,
            "rate_ratio": fstr(rate_ratio),
            "near_dup_threshold": threshold,
            "near_dup_relative": settings.near_dup_relative,
            "median_boundary_mad": median_mad,
            "pairing_min_ratio": settings.pairing_min_ratio,
            "max_near_duplicates": settings.max_near_duplicates,
        },
        "expected": {
            "frames": expected_frames,
            "duration_seconds": fstr(expected_duration),
            "duration_seconds_decimal": f"{float(expected_duration):.6f}",
            "formula": "ceil(project_frames * target_fps / project_fps)",
        },
        "media": media,
        "decode": {
            "source_width": vid["width"],
            "source_height": vid["height"],
            "width": decode_w,
            "height": decode_h,
            "scaled": (decode_w, decode_h) != (vid["width"], vid["height"]),
            "pix_fmt": "gray",
            "fps_mode": "passthrough",
        },
        "measured": {
            "decoded_frames": n,
            "ffprobe_nb_read_frames": ffprobe_count,
            "unique_frame_hashes": sha1_digest_count(scan.frame_sha1),
            "exact_duplicate_boundaries": exact,
            "near_duplicate_boundaries": near,
            "near_duplicate_boundaries_ignored_final": end_ignored,
            "near_duplicate_boundaries_counted": near_counted,
            "mad_stats_over_boundaries": mad_stats,
            "pairing": pairing,
            "mad_by_boundary": mad,
            "frame_sha1": scan.frame_sha1,
        },
        "reference": reference_info,
        "checks": checks,
        "overall": overall,
    }


# ---------------------------------------------------------------- output

def render_text(report: dict) -> str:
    settings = report["settings"]
    expected = report["expected"]
    media = report["media"]
    vid = media["video"]
    aud = media["audio"]
    measured = report["measured"]
    lines = [
        f"Validation: {report['input']['path']}",
        f"ffmpeg:     {report['ffmpeg_version']}",
        f"Project:    {settings['project_fps']} fps, {settings['project_frames']} frames; "
        f"target {settings['target_fps']} fps (ratio {settings['rate_ratio']})",
        f"Expected:   {expected['frames']} frames, {expected['duration_seconds_decimal']} s",
        f"Video:      {vid['codec_name']} {vid['width']}x{vid['height']} {vid['pix_fmt']}, "
        f"r_frame_rate={vid['r_frame_rate']} avg_frame_rate={vid['avg_frame_rate']} "
        f"duration={vid['duration']} s",
    ]
    if aud:
        lines.append(f"Audio:      {aud['codec_name']} {aud['sample_rate']} Hz "
                     f"{aud['channels']} ch, duration={aud['duration']} s")
    else:
        lines.append("Audio:      none")
    decode = report["decode"]
    lines.append(f"Decode:     gray {decode['width']}x{decode['height']}"
                 f"{' (scaled from ' + str(decode['source_width']) + 'x' + str(decode['source_height']) + ')' if decode['scaled'] else ''}"
                 f", fps_mode=passthrough")
    lines.append("")
    lines.append("Measured")
    lines.append(f"  decoded frames                {measured['decoded_frames']}"
                 f"   (ffprobe -count_frames {measured['ffprobe_nb_read_frames']})")
    lines.append(f"  unique frame hashes           {measured['unique_frame_hashes']}")
    lines.append(f"  adjacent exact duplicates     {len(measured['exact_duplicate_boundaries'])}"
                 f"   at {format_positions(measured['exact_duplicate_boundaries'])}")
    lines.append(f"  adjacent near-duplicates      {len(measured['near_duplicate_boundaries'])}"
                 f"   (MAD < {settings['near_dup_threshold']}) at "
                 f"{format_positions(measured['near_duplicate_boundaries'])}")
    stats = measured["mad_stats_over_boundaries"]
    if stats:
        lines.append(f"  MAD over boundaries           min {stats['min']:.4f}  median {stats['median']:.4f}"
                     f"  mean {stats['mean']:.4f}  max {stats['max']:.4f}")
    pairing = measured["pairing"]
    if pairing["applicable"]:
        ratio = pairing["in_over_out"]
        ratio_text = f"{ratio:.4f}" if ratio is not None else "n/a (out-of-group median is 0)"
        lines.append(f"  pairing score (r={pairing['r']})  in-group median MAD "
                     f"{pairing['in_group_median_mad']:.4f} ({pairing['in_group_boundaries']} boundaries, i%r!=0)")
        lines.append(f"                                out-of-group median MAD "
                     f"{pairing['out_group_median_mad']:.4f} ({pairing['out_group_boundaries']} boundaries, i%r==0)")
        lines.append(f"                                in/out ratio {ratio_text}")
    else:
        lines.append(f"  pairing score                 N/A ({pairing['reason']})")
    lines.append("")
    lines.append("Checks")
    for check in report["checks"]:
        lines.append(f"  {check['status']:<4}  {check['name']:<15} {check['detail']}")
    reference = report["reference"]
    if reference:
        lines.append("")
        lines.append("Reference (informational, not a check)")
        lines.append(f"  {'':<12}{'frames':>8} {'unique':>8} {'r_frame_rate':>14} {'size':>10}")
        lines.append(f"  {'target':<12}{measured['decoded_frames']:>8} "
                     f"{measured['unique_frame_hashes']:>8} {vid['r_frame_rate']:>14} "
                     f"{str(vid['width']) + 'x' + str(vid['height']):>10}")
        lines.append(f"  {'reference':<12}{reference['decoded_frames']:>8} "
                     f"{reference['unique_frame_hashes']:>8} {reference['r_frame_rate']:>14} "
                     f"{str(reference['width']) + 'x' + str(reference['height']):>10}")
    lines.append("")
    lines.append(f"Overall: {report['overall']}")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        ffmpeg = resolve_tool("ffmpeg", args.ffmpeg)
        ffprobe = resolve_tool("ffprobe", args.ffprobe)
        if not args.video.is_file():
            raise ToolError(f"video does not exist: {args.video}")
        if args.reference is not None and not args.reference.is_file():
            raise ToolError(f"reference does not exist: {args.reference}")
        if args.project_duration is not None:
            exact_frames = args.project_duration * args.project_fps
            if exact_frames.denominator != 1:
                raise ToolError(f"--project-duration x --project-fps = {fstr(exact_frames)} "
                                "is not a whole number of project frames")
            project_frames = int(exact_frames)
        else:
            project_frames = args.project_frames
        settings = Settings(
            project_fps=args.project_fps,
            target_fps=args.target_fps,
            project_frames=project_frames,
            project_duration=args.project_duration,
            near_dup_threshold=args.near_dup_threshold,
            near_dup_relative=args.near_dup_relative,
            pairing_min_ratio=args.pairing_min_ratio,
            max_near_duplicates=args.max_near_duplicates,
        )
        report = analyse(args.video.resolve(), args.reference, ffmpeg, ffprobe, settings)
    except ToolError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_ERROR

    print(render_text(report))
    if args.json is not None:
        args.json.write_text(json.dumps(report, indent=2, ensure_ascii=True, allow_nan=False) + "\n",
                             encoding="utf-8")
    return EXIT_OK if report["overall"] == PASS else EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main())
