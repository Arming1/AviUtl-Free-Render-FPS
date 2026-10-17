#!/usr/bin/env python3
"""Check free-rate FreeRenderFPS exports against the fixture matrix.

Usage:
    python -I experiments/free_fps_matrix/analyze.py RUN_DIR [--label TEXT]
        [--only ID[,ID...]] [--ffmpeg PATH] [--ffprobe PATH]

RUN_DIR holds one MP4 per case, named <case_id>.mp4 (ids from cases.json).
For each selected case the script runs:

1. tools/validate_freefps_output.py with --project-fps = --target-fps = the
   case target and --project-frames = the expected output frame count;
2. a motion check of the moving object (see motion_check()).

Outputs: RUN_DIR/SUMMARY.md (Chinese), RUN_DIR/summary.json and raw per-case
files under RUN_DIR/analysis/.

Exit codes: 0 all selected cases pass, 1 some case fails or is missing,
2 input or environment error.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import subprocess
import sys
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path

try:
    import numpy as np
except ImportError:  # pragma: no cover - environment problem
    sys.exit("error: numpy is required (python -m pip install numpy)")


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
VALIDATOR = REPO / "tools" / "validate_freefps_output.py"
CASES_PATH = HERE / "cases.json"

MOTION_TOL_PX = 1.0   # max |measured centroid x - model x(t)| allowed
MASK_DELTA = 30       # object pixels: |Y - frame median luma| > MASK_DELTA
STEP_EPS_PX = 0.05    # consecutive centroids closer than this are a zero step
EXIT_OK, EXIT_FAIL, EXIT_ERROR = 0, 1, 2


class InputError(Exception):
    """Bad input or environment: message goes to stderr, exit code 2."""


@dataclass(frozen=True)
class Fixture:
    path: Path
    width: int
    height: int
    rate: Fraction
    first_frame: int
    last_frame: int
    x_start: Fraction
    x_end: Fraction
    y_start: Fraction

    @property
    def frames(self) -> int:
        return self.last_frame - self.first_frame + 1

    @property
    def motion_span(self) -> Fraction:
        # Time at which the trackbar reaches its end value (last keyframe frame).
        return Fraction(self.last_frame - self.first_frame) / self.rate


def fstr(value: Fraction) -> str:
    return f"{value.numerator}/{value.denominator}" if value.denominator != 1 else str(value.numerator)


def parse_fixture(path: Path) -> Fixture:
    if not path.is_file():
        raise InputError(f"fixture not found: {path}")
    fields: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if "=" in raw and not raw.startswith("["):
            key, _, value = raw.partition("=")
            fields.setdefault(key, value)  # first occurrence is the object/scene value

    def need(key: str) -> str:
        if key not in fields:
            raise InputError(f"{path}: missing '{key}='")
        return fields[key]

    first, last = (int(p) for p in need("frame").split(",")[:2])
    x_start, x_end = (Fraction(p) for p in need("X").split(",")[:2])
    return Fixture(
        path=path,
        width=int(need("video.width")),
        height=int(need("video.height")),
        rate=Fraction(need("video.rate")),
        first_frame=first,
        last_frame=last,
        x_start=x_start,
        x_end=x_end,
        y_start=Fraction(need("Y").split(",")[0]),
    )


def load_cases() -> dict:
    if not CASES_PATH.is_file():
        raise InputError(f"missing {CASES_PATH}")
    return json.loads(CASES_PATH.read_text(encoding="utf-8"))


def check_case_consistency(case: dict, fixture: Fixture) -> None:
    """cases.json must agree with the fixture it names and with the ceil formula."""
    src_fps = Fraction(case["source_fps"])
    tgt_fps = Fraction(case["target_fps"])
    if fixture.rate != src_fps:
        raise InputError(f"{case['id']}: fixture rate {fstr(fixture.rate)} != source_fps {case['source_fps']}")
    if fixture.frames != case["source_frames"]:
        raise InputError(f"{case['id']}: fixture has {fixture.frames} frames, cases.json says {case['source_frames']}")
    expected = math.ceil(Fraction(case["source_frames"]) * tgt_fps / src_fps)
    if expected != case["expected_frames"]:
        raise InputError(f"{case['id']}: ceil formula gives {expected}, cases.json says {case['expected_frames']}")
    if Fraction(expected) / tgt_fps != Fraction(case["expected_duration"]):
        raise InputError(f"{case['id']}: expected_duration mismatch")


def probe_dimensions(ffprobe: str, path: Path) -> tuple[int, int]:
    proc = subprocess.run(
        [ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
         "-of", "json", str(path)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        raise InputError(f"ffprobe failed on {path.name}: {proc.stderr.decode('utf-8', 'replace').strip()}")
    stream = json.loads(proc.stdout.decode("utf-8", "replace"))["streams"][0]
    return int(stream["width"]), int(stream["height"])


def decode_gray(ffmpeg: str, path: Path, width: int, height: int) -> np.ndarray:
    command = [
        ffmpeg, "-hide_banner", "-v", "error", "-nostdin", "-noautorotate",
        "-i", str(path), "-map", "0:v:0", "-an", "-sn", "-dn",
        "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "gray", "-",
    ]
    proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        detail = proc.stderr.decode("utf-8", "replace").strip()
        raise InputError(f"ffmpeg decode of {path.name} failed: {detail}")
    size = width * height
    if len(proc.stdout) % size != 0:
        raise InputError(f"{path.name}: decoded byte count is not a multiple of one {width}x{height} frame")
    return np.frombuffer(proc.stdout, dtype=np.uint8).reshape(-1, height, width)


def motion_check(frames: np.ndarray, fixture: Fixture, target: Fraction) -> tuple[dict, list[dict]]:
    """Compare the moving object's per-frame centroid with the analytic motion.

    Object mask per frame: |Y - median(Y)| > MASK_DELTA. Centroid is the mean
    pixel position in continuous coordinates (pixel index + 0.5).
    Model (AviUtl centre convention, to be confirmed by the fit diagnostics):
        x0 = W/2 + X_start,  x1 = W/2 + X_end,  y = H/2 + Y_start
        x(t) = x0 + (x1 - x0) * min(t, T) / T,   t = n / target,  T = fixture.motion_span
    Only frames with t <= T are used for monotonicity, zero-step and the
    linear-fit diagnostics; frames after T are held at x1 by the model.
    """
    n = frames.shape[0]
    h, w = frames.shape[1], frames.shape[2]
    xx, yy = np.meshgrid(np.arange(w, dtype=np.float64) + 0.5, np.arange(h, dtype=np.float64) + 0.5)
    cx = np.full(n, np.nan)
    cy = np.full(n, np.nan)
    counts = np.zeros(n, dtype=np.int64)
    for i in range(n):
        frame = frames[i]
        median = float(np.median(frame))
        mask = np.abs(frame.astype(np.int16) - median) > MASK_DELTA
        counts[i] = int(mask.sum())
        if counts[i] == 0:
            continue
        cx[i] = float(xx[mask].mean())
        cy[i] = float(yy[mask].mean())

    if fixture.motion_span <= 0:
        raise InputError("fixture motion span must be positive")
    x0 = fixture.width / 2 + float(fixture.x_start)
    x1 = fixture.width / 2 + float(fixture.x_end)
    y0 = fixture.height / 2 + float(fixture.y_start)
    span = fixture.motion_span
    times = [Fraction(i) / target for i in range(n)]
    model = np.array([x0 + (x1 - x0) * float(min(t, span) / span) for t in times])
    in_motion = [i for i in range(n) if times[i] <= span]
    in_idx = np.array(in_motion, dtype=np.int64)

    resid = cx - model
    finite = np.isfinite(resid)
    max_resid = float(np.max(np.abs(resid[finite]))) if finite.any() else float("nan")
    y_resid = float(np.max(np.abs(cy[finite] - y0))) if finite.any() else float("nan")

    sign = 1.0 if x1 > x0 else -1.0
    steps = [cx[i] - cx[i - 1] for i in in_motion[1:]]
    reversals = sum(1 for d in steps if d * sign < 0)
    zero_steps = sum(1 for d in steps if abs(d) < STEP_EPS_PX)
    no_centroid = int((~np.isfinite(cx)).sum())

    fit_slope = fit_intercept = fit_max_resid = None
    if len(in_motion) >= 2 and np.isfinite(cx[in_idx]).all():
        t_in = np.array([float(times[i]) for i in in_motion])
        x_in = cx[in_idx]
        fit_slope, fit_intercept = (float(v) for v in np.polyfit(t_in, x_in, 1))
        fit_max_resid = float(np.max(np.abs(x_in - (fit_intercept + fit_slope * t_in))))

    checks_ok = (
        n >= 2 and no_centroid == 0 and math.isfinite(max_resid)
        and max_resid <= MOTION_TOL_PX and reversals == 0 and zero_steps == 0
    )
    result = {
        "status": "PASS" if checks_ok else "FAIL",
        "decoded_frames": n,
        "frames_without_object": no_centroid,
        "in_motion_frames": len(in_motion),
        "max_abs_residual_px": max_resid,
        "tolerance_px": MOTION_TOL_PX,
        "max_abs_residual_y_px": y_resid,
        "reversals": reversals,
        "zero_steps": zero_steps,
        "strictly_monotonic": reversals == 0 and zero_steps == 0,
        "model": {"x0": x0, "x1": x1, "y0": y0, "T_seconds": float(span), "slope_px_per_s": (x1 - x0) / float(span)},
        "fit": {"slope_px_per_s": fit_slope, "intercept_px": fit_intercept, "max_abs_residual_px": fit_max_resid},
    }
    rows = []
    for i in range(n):
        rows.append({
            "frame": i,
            "t_seconds": float(times[i]),
            "in_motion": times[i] <= span,
            "centroid_x": "" if not math.isfinite(cx[i]) else round(cx[i], 4),
            "centroid_y": "" if not math.isfinite(cy[i]) else round(cy[i], 4),
            "object_pixels": int(counts[i]),
            "model_x": round(float(model[i]), 4),
            "residual_px": "" if not math.isfinite(resid[i]) else round(float(resid[i]), 4),
        })
    return result, rows


def run_validator(case: dict, video: Path, json_path: Path, ffmpeg: str | None, ffprobe: str | None) -> dict:
    target = case["target_fps"]
    command = [
        sys.executable, "-I", str(VALIDATOR), str(video),
        "--project-fps", target, "--target-fps", target,
        "--project-frames", str(case["expected_frames"]),
        "--json", str(json_path),
    ]
    if ffmpeg:
        command += ["--ffmpeg", ffmpeg]
    if ffprobe:
        command += ["--ffprobe", ffprobe]
    proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    result = {"exit_code": proc.returncode, "overall": None, "checks": []}
    if json_path.is_file():
        report = json.loads(json_path.read_text(encoding="utf-8"))
        result["overall"] = report.get("overall")
        result["checks"] = [
            {"name": c["name"], "status": c["status"], "detail": c["detail"]} for c in report.get("checks", [])
        ]
    if proc.returncode not in (0, 1):
        result["error"] = proc.stderr.decode("utf-8", "replace").strip()[-800:]
    return result


def analyse_case(case: dict, run_dir: Path, analysis_dir: Path, ffmpeg: str, ffprobe: str,
                 ffmpeg_override: str | None, ffprobe_override: str | None) -> dict:
    fixture = parse_fixture(REPO / case["source_fixture"])
    check_case_consistency(case, fixture)
    target = Fraction(case["target_fps"])
    video = run_dir / f"{case['id']}.mp4"
    entry = {
        "id": case["id"],
        "source_fixture": case["source_fixture"],
        "source_fps": case["source_fps"],
        "source_frames": case["source_frames"],
        "target_fps": case["target_fps"],
        "expected_frames": case["expected_frames"],
        "expected_duration": case["expected_duration"],
        "video": str(video),
        "status": "MISSING",
        "validator": None,
        "motion": None,
    }
    if not video.is_file():
        return entry

    width, height = probe_dimensions(ffprobe, video)
    if (width, height) != (fixture.width, fixture.height):
        entry["motion"] = {"status": "FAIL", "detail": f"resolution {width}x{height} != fixture {fixture.width}x{fixture.height}"}
    else:
        frames = decode_gray(ffmpeg, video, width, height)
        motion, rows = motion_check(frames, fixture, target)
        entry["motion"] = motion
        with (analysis_dir / f"{case['id']}.centroids.csv").open("w", newline="", encoding="utf-8") as fh:
            writer = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)

    entry["validator"] = run_validator(case, video, analysis_dir / f"{case['id']}.validator.json",
                                       ffmpeg_override, ffprobe_override)
    validator_ok = entry["validator"]["overall"] == "PASS"
    motion_ok = entry["motion"]["status"] == "PASS"
    entry["status"] = "PASS" if validator_ok and motion_ok else "FAIL"
    return entry


def render_summary(result: dict) -> str:
    lines = [
        f"# 自由帧率导出验证汇总",
        "",
        f"- 标签: **{result['label']}**",
        f"- 运行目录: `{result['run_dir']}`",
        f"- 总体结论: **{result['overall']}**",
        "",
        "判定标准:",
        "",
        "- validator: `tools/validate_freefps_output.py`, `--project-fps` = `--target-fps` = 目标帧率, `--project-frames` = 期望输出帧数。",
        f"- 运动检查: 每帧用 luma 与中位数之差 > {MASK_DELTA} 的像素求质心 x。"
        f"模型 x(t) = W/2 + X_start + (X_end - X_start)·min(t, T)/T, t = n / 目标帧率, T = (末帧 - 首帧) / 源帧率。"
        f"残差上限 {MOTION_TOL_PX} px; 相邻帧 |Δx| < {STEP_EPS_PX} px 记为零步; 要求零反向、零零步、无缺失质心。",
        "",
        "| 用例 | 源 fixture | 目标 FPS | 期望帧 | 期望时长 (s) | 实际帧 | validator | 运动最大残差 (px) | 零步 | 反向 | 结论 |",
        "|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    for case in result["cases"]:
        exp_dur = Fraction(case["expected_duration"])
        motion = case.get("motion") or {}
        validator = case.get("validator") or {}
        decoded = motion.get("decoded_frames", "-")
        resid = motion.get("max_abs_residual_px")
        resid_text = f"{resid:.4f}" if isinstance(resid, float) and math.isfinite(resid) else "-"
        validator_text = validator.get("overall") or "-"
        lines.append(
            f"| {case['id']} | `{Path(case['source_fixture']).name}` | {case['target_fps']} | "
            f"{case['expected_frames']} | {float(exp_dur):.4f} | {decoded} | "
            f"{validator_text} | {resid_text} | {motion.get('zero_steps', '-')} | "
            f"{motion.get('reversals', '-')} | **{case['status']}** |"
        )
    lines += ["", "## 细节", ""]
    for case in result["cases"]:
        lines.append(f"### {case['id']}")
        if case["status"] == "MISSING":
            lines += ["", "未找到导出文件, 未检查。", ""]
            continue
        validator = case["validator"]
        lines.append("")
        lines.append(f"- validator 退出码 {validator['exit_code']}, overall = {validator['overall']}")
        for check in validator["checks"]:
            if check["status"] != "PASS":
                lines.append(f"  - {check['status']} `{check['name']}`: {check['detail']}")
        if "error" in validator:
            lines.append(f"  - validator stderr: `{validator['error']}`")
        motion = case["motion"]
        if "max_abs_residual_px" in motion:
            fit = motion["fit"]
            lines.append(
                f"- 运动: 状态 {motion['status']}, 帧 {motion['decoded_frames']}, 运动段内帧 {motion['in_motion_frames']}, "
                f"无对象帧 {motion['frames_without_object']}, 最大残差 {motion['max_abs_residual_px']:.4f} px, "
                f"零步 {motion['zero_steps']}, 反向 {motion['reversals']}")
            if fit["slope_px_per_s"] is not None:
                lines.append(
                    f"- 运动诊断: 模型斜率 {motion['model']['slope_px_per_s']:.4f} px/s, "
                    f"线性拟合斜率 {fit['slope_px_per_s']:.4f} px/s, 截距 {fit['intercept_px']:.4f} px, "
                    f"拟合最大残差 {fit['max_abs_residual_px']:.4f} px, 纵向最大偏差 {motion['max_abs_residual_y_px']:.4f} px")
        else:
            lines.append(f"- 运动: {motion.get('detail', motion)}")
        lines.append("")
    lines += [
        "## 假设与限制",
        "",
        "- 运动模型假设 AviUtl 的直线移动在 [首帧, 末帧] 之间线性插值, 末值保持到之后。"
        "T 由 fixture 的 frame 行推出, 不是名义 5 s (例如 frame=0,299 时 T = 299/60 s)。这一点需要宿主实测确认。",
        "- x0/x1 取 W/2 + X 值 (AviUtl 画面中心约定), 通过残差与拟合截距检验。",
        "- 结论只覆盖 luma 像素导出的几何位置。不证明颜色、音频或编码器正确性。",
        "",
    ]
    return "\n".join(lines)


def parse_only(text: str | None, known: list[str]) -> list[str]:
    if not text:
        return known
    selected = [t.strip() for t in text.split(",") if t.strip()]
    unknown = [t for t in selected if t not in known]
    if unknown:
        raise InputError(f"unknown case id(s): {', '.join(unknown)}")
    return [k for k in known if k in selected]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Check free-rate FreeRenderFPS exports.")
    parser.add_argument("run_dir", type=Path, help="directory holding <case_id>.mp4 files")
    parser.add_argument("--label", default="REAL", help="label written into SUMMARY.md and summary.json")
    parser.add_argument("--only", help="comma-separated case ids to check (default: all)")
    parser.add_argument("--ffmpeg", type=Path, help="ffmpeg executable (default: PATH)")
    parser.add_argument("--ffprobe", type=Path, help="ffprobe executable (default: PATH)")
    args = parser.parse_args(argv)

    try:
        run_dir = args.run_dir.resolve()
        if not run_dir.is_dir():
            raise InputError(f"run dir not found: {run_dir}")
        cases_doc = load_cases()
        by_id = {c["id"]: c for c in cases_doc["cases"]}
        selected = parse_only(args.only, [c["id"] for c in cases_doc["cases"]])
        ffmpeg = str(args.ffmpeg) if args.ffmpeg else "ffmpeg"
        ffprobe = str(args.ffprobe) if args.ffprobe else "ffprobe"
        analysis_dir = run_dir / "analysis"
        analysis_dir.mkdir(parents=True, exist_ok=True)
        results = [
            analyse_case(by_id[cid], run_dir, analysis_dir, ffmpeg, ffprobe,
                         str(args.ffmpeg) if args.ffmpeg else None,
                         str(args.ffprobe) if args.ffprobe else None)
            for cid in selected
        ]
    except (InputError, OSError, json.JSONDecodeError, KeyError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_ERROR

    all_pass = all(r["status"] == "PASS" for r in results)
    result = {
        "label": args.label,
        "run_dir": str(run_dir),
        "overall": "PASS" if all_pass else "FAIL",
        "selected_cases": selected,
        "cases": results,
    }
    (run_dir / "summary.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
    (run_dir / "SUMMARY.md").write_text(render_summary(result), encoding="utf-8")
    for r in results:
        print(f"{r['id']:<12} {r['status']}")
    print(f"overall {result['overall']}  (see {run_dir / 'SUMMARY.md'})")
    return EXIT_OK if all_pass else EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main())
