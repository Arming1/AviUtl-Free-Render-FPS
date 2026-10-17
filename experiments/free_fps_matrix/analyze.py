#!/usr/bin/env python3
"""Check free-rate FreeRenderFPS exports against the fixture matrix.

Usage:
    python -I experiments/free_fps_matrix/analyze.py RUN_DIR [--label TEXT]
        [--only ID[,ID...]] [--timeline retimed|original|precise]
        [--ffmpeg PATH] [--ffprobe PATH]

RUN_DIR holds one MP4 per case, named <case_id>.mp4 (ids from cases.json).
For each selected case the script runs:

1. tools/validate_freefps_output.py with --project-fps = --target-fps = the
   case target and --project-frames = the expected output frame count;
2. a motion check of the moving object (see motion_check()).

--timeline picks the motion model that decides PASS/FAIL:
  retimed   (default) the project was retimed by tools/aup2_fps_upconvert.py
            (Free FPS Export). Motion runs over the retimed frame range s', e'.
  original  the source fixture's frame range is used unchanged. This is the
            plugin Hook path (dry_run_simulated samples, not retimed exports).
  precise   the --precise export (tools/aup2_fps_upconvert.py): the line follows the
            original path in original time, x = X_start + (X_end - X_start)·(t - s/Rp)/T,
            is held at X_start before the retimed start s'/Rt and is not clamped at X_end,
            so it runs on to the retimed last frame e'/Rt. The window s', e' is the retimed one.
The original-time model is always computed as a diagnostic, together with the
end-time deviation T' - T.

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
import types
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
RETIMING_TOOL = REPO / "tools" / "aup2_fps_upconvert.py"
CASES_PATH = HERE / "cases.json"

MOTION_TOL_PX = 1.0   # max |measured centroid x - model x(t)| allowed
MASK_DELTA = 30       # object pixels: |Y - frame median luma| > MASK_DELTA
STEP_EPS_PX = 0.05    # consecutive centroids closer than this are a zero step
TIMELINE_MODES = ("retimed", "original", "precise")
ENDPOINT_NOTE = "已知近似：直线移动终点对齐到重定时后的最后一帧，终点时间偏差 ≤ |1/源帧率 − 1/目标帧率| + 半个输出帧"
TIMELINE_TITLE = {
    "retimed": "重定时帧范围 (Free FPS Export)",
    "original": "原工程帧范围 (插件 Hook 路径样本, 未重定时)",
    "precise": "精确模式 (--precise): 沿原工程直线延续到重定时后最后一帧",
}
TIMELINE_FORMULA = {
    "retimed": ("起点 t_s = s'/目标帧率, T' = (e' - s')/目标帧率, s' = rhu(s·k), e' = rhu((e+1)·k) - 1, "
                "k = 目标帧率/源帧率, rhu = 四舍五入向上 (与 tools/aup2_fps_upconvert.py 的 rescale_frames 相同)"),
    "original": "起点 t_s = 0, T = (末帧 - 首帧)/源帧率",
    "precise": ("窗口 s'/目标帧率 .. e'/目标帧率 (s', e' 与 retimed 相同), 原工程 T = (e - s)/源帧率, "
                "x(t) 的原点为 s/源帧率"),
}
MODEL_FORMULA = {
    "retimed": "x(t) = W/2 + X_start + (X_end - X_start)·min(max(t - t_s, 0), T)/T, t = n / 目标帧率",
    "original": "x(t) = W/2 + X_start + (X_end - X_start)·min(max(t - t_s, 0), T)/T, t = n / 目标帧率",
    "precise": "x(t) = W/2 + X_start + (X_end - X_start)·(t - s/源帧率)/T, t = n / 目标帧率 (不截断, 对象开始前保持 X_start)",
}
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


@dataclass(frozen=True)
class Timeline:
    """Linear 直線移動 model over the motion window [start, end] on the output timeline.

    retimed / original: X reaches X_end at start + span seconds and holds there.
    precise: u(t) = (t - origin) / travel_span, not clamped. Before start the value is
    held at X_start; after end the line keeps going (extrapolated), as the --precise
    trackbar values do. origin = s/Rp and travel_span = T are in original-project time.
    """
    name: str
    start: Fraction  # seconds on the output timeline: start of the motion window
    span: Fraction   # seconds: length of the motion window (end - start)
    origin: Fraction | None = None       # precise only: output time at which u = 0
    travel_span: Fraction | None = None  # precise only: seconds for u = 0 -> 1
    extrapolate: bool = False            # precise only: do not clamp u at the window end

    @property
    def end(self) -> Fraction:
        return self.start + self.span

    @property
    def travel(self) -> Fraction:
        """Seconds for the travel X_start -> X_end (equal to span unless travel_span is set)."""
        return self.span if self.travel_span is None else self.travel_span

    def fraction(self, t: Fraction) -> Fraction:
        """Fraction of the X_start -> X_end travel at output time t (may exceed 1 when extrapolating)."""
        if t < self.start:
            return Fraction(0)
        origin = self.start if self.origin is None else self.origin
        if not self.extrapolate:
            t = min(t, self.end)
        return (t - origin) / self.travel


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


def load_retiming_tool():
    """Load tools/aup2_fps_upconvert.py so the frame rule is the tool's own code.

    The file is compiled from its bytes and run in a fresh module, so no
    __pycache__ entry is written into tools/.
    """
    if not RETIMING_TOOL.is_file():
        raise InputError(f"missing {RETIMING_TOOL}")
    module = types.ModuleType("aup2_fps_upconvert")
    module.__file__ = str(RETIMING_TOOL)
    exec(compile(RETIMING_TOOL.read_bytes(), str(RETIMING_TOOL), "exec"), module.__dict__)
    return module


def original_timeline(fixture: Fixture) -> Timeline:
    """Source project's own frame range: starts at t = 0, T = (last - first) / source rate."""
    return Timeline("original", Fraction(0), fixture.motion_span)


def retimed_timeline(fixture: Fixture, target: Fraction) -> tuple[Timeline, dict]:
    """Motion over the frame range that tools/aup2_fps_upconvert.py writes for this object.

    k = target / source rate. s', e' come from the tool's rescale_frames(), the same
    function convert() uses for every object. start = s'/target, span = (e' - s')/target.
    """
    tool = load_retiming_tool()
    k = target / fixture.rate
    try:
        frames, shortened = tool.rescale_frames([fixture.first_frame, fixture.last_frame], k, 0)
    except tool.Refusal as exc:
        raise InputError(f"{fixture.path.name}: retiming refused: {exc.reason}")
    new_first, new_last = frames
    if shortened or new_last <= new_first:
        raise InputError(f"{fixture.path.name}: retimed object has no motion span ({new_first}..{new_last})")
    timeline = Timeline("retimed", Fraction(new_first) / target, Fraction(new_last - new_first) / target)
    return timeline, {"k": fstr(k), "first_frame": new_first, "last_frame": new_last}


def precise_timeline(fixture: Fixture, retimed: Timeline) -> Timeline:
    """Motion model of the --precise export (see Timeline).

    The window [s'/Rt, e'/Rt] is the retimed one. The line is the original line in original
    time: origin s/Rp, travel T = (e - s)/Rp, the same as original_timeline() for s = 0.
    """
    return Timeline("precise", retimed.start, retimed.span,
                    origin=Fraction(fixture.first_frame) / fixture.rate,
                    travel_span=fixture.motion_span, extrapolate=True)


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


def object_centroids(frames: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Per-frame centroid (x, y) and pixel count of the moving object; NaN where no object pixel.

    Object mask per frame: |Y - median(Y)| > MASK_DELTA. Centroid is the mean
    pixel position in continuous coordinates (pixel index + 0.5).
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
    return cx, cy, counts


def motion_check(cx: np.ndarray, cy: np.ndarray, counts: np.ndarray, fixture: Fixture,
                 target: Fraction, timeline: Timeline) -> tuple[dict, list[dict]]:
    """Compare the moving object's per-frame centroid with the linear model of `timeline`.

    Model (AviUtl centre convention, to be confirmed by the fit diagnostics):
        x0 = W/2 + X_start,  x1 = W/2 + X_end,  y = H/2 + Y_start
        x(t) = x0 + (x1 - x0) * timeline.fraction(t),  t = n / target
    For retimed / original, fraction = min(max(t - start, 0), span) / span, so frames after
    the end are held at x1. For precise the fraction is not clamped (see Timeline).
    Only frames with start <= t <= end are used for monotonicity, zero-step and the
    linear-fit diagnostics.
    """
    n = cx.shape[0]
    x0 = fixture.width / 2 + float(fixture.x_start)
    x1 = fixture.width / 2 + float(fixture.x_end)
    y0 = fixture.height / 2 + float(fixture.y_start)
    span = timeline.span
    if span <= 0:
        raise InputError("motion span must be positive")
    times = [Fraction(i) / target for i in range(n)]
    model = np.array([x0 + (x1 - x0) * float(timeline.fraction(t)) for t in times])
    in_motion = [i for i in range(n) if timeline.start <= times[i] <= timeline.end]
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
        "model": {"timeline": timeline.name, "start_s": float(timeline.start), "x0": x0, "x1": x1, "y0": y0,
                  "T_seconds": float(timeline.travel), "slope_px_per_s": (x1 - x0) / float(timeline.travel)},
        "fit": {"slope_px_per_s": fit_slope, "intercept_px": fit_intercept, "max_abs_residual_px": fit_max_resid},
    }
    rows = []
    for i in range(n):
        rows.append({
            "frame": i,
            "t_seconds": float(times[i]),
            "in_motion": timeline.start <= times[i] <= timeline.end,
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
                 ffmpeg_override: str | None, ffprobe_override: str | None, timeline_mode: str) -> dict:
    fixture = parse_fixture(REPO / case["source_fixture"])
    check_case_consistency(case, fixture)
    target = Fraction(case["target_fps"])
    retimed, retime_info = retimed_timeline(fixture, target)
    original = original_timeline(fixture)
    precise = precise_timeline(fixture, retimed)
    primary = {"retimed": retimed, "original": original, "precise": precise}[timeline_mode]
    delta = retimed.span - original.span
    # Theoretical bound on |T' - T|. With e' = rhu((e+1)k) - 1 and s' = rhu(s*k), the end term
    # gives e'/Rt - e/Rp = (1/Rp - 1/Rt) + delta_e/Rt with |delta_e| <= 1/2; the start term adds
    # -delta_s/Rt, which is 0 when s = 0 (all fixtures start at frame 0).
    start_half = Fraction(1, 2) if fixture.first_frame != 0 else Fraction(0)
    bound = abs(1 / fixture.rate - 1 / target) + (Fraction(1, 2) + start_half) / target
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
        "timeline": timeline_mode,
        "endpoint": {
            "k": retime_info["k"],
            "retimed_first_frame": retime_info["first_frame"],
            "retimed_last_frame": retime_info["last_frame"],
            "T_original_s": float(original.span),
            "T_retimed_s": float(retimed.span),
            "delta_s": float(delta),
            "delta_original_frames": float(delta * fixture.rate),
            "delta_output_frames": float(delta * target),
            "bound_s": float(bound),
            "bound_output_frames": float(bound * target),
            "within_bound": abs(delta) <= bound,
        },
        "status": "MISSING",
        "validator": None,
        "motion": None,
        "original_time_check": None,
    }
    if not video.is_file():
        return entry

    width, height = probe_dimensions(ffprobe, video)
    if (width, height) != (fixture.width, fixture.height):
        entry["motion"] = {"status": "FAIL", "detail": f"resolution {width}x{height} != fixture {fixture.width}x{fixture.height}"}
    else:
        frames = decode_gray(ffmpeg, video, width, height)
        cx, cy, counts = object_centroids(frames)
        motion, rows = motion_check(cx, cy, counts, fixture, target, primary)
        diagnostic, diagnostic_rows = motion_check(cx, cy, counts, fixture, target, original)
        entry["motion"] = motion
        entry["original_time_check"] = {
            "max_abs_residual_px": diagnostic["max_abs_residual_px"],
            "T_seconds": diagnostic["model"]["T_seconds"],
        }
        for row, diag_row in zip(rows, diagnostic_rows):
            row["model_x_original_time"] = diag_row["model_x"]
            row["residual_original_time_px"] = diag_row["residual_px"]
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
    mode = result["timeline"]
    lines = [
        f"# 自由帧率导出验证汇总",
        "",
        f"- 标签: **{result['label']}**",
        f"- 运行目录: `{result['run_dir']}`",
        f"- 运动时间模型 (`--timeline {mode}`): **{TIMELINE_TITLE[mode]}**",
        f"- 总体结论: **{result['overall']}**",
        "",
        "判定标准:",
        "",
        "- validator: `tools/validate_freefps_output.py`, `--project-fps` = `--target-fps` = 目标帧率, `--project-frames` = 期望输出帧数。",
        f"- 运动检查: 每帧用 luma 与中位数之差 > {MASK_DELTA} 的像素求质心 x。"
        f"模型 {MODEL_FORMULA[mode]}; {TIMELINE_FORMULA[mode]}。"
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
    lines += ["", "## 终点时间偏差 (诊断)", "", f"{ENDPOINT_NOTE}。", "",
              "偏差 = T' − T: T 为原工程时间 (末帧 − 首帧)/源帧率, T' 为重定时后 (e' − s')/目标帧率; "
              "正值表示重定时后运动更长。理论上限 = |1/Rp − 1/Rt| + 1/(2·Rt) (Rp 源帧率, Rt 目标帧率; "
              "起点为 0 时起点项为 0)。这是诊断项, 不影响 PASS 判定。"
              "原工程时间模型残差按 `original` 模型单独计算, 不参与判定。", "",
              "| 用例 | k | 重定时帧 s'..e' | T 原工程 (s) | T' 重定时 (s) | 偏差 T'−T (s) | 偏差 (输出帧) | 理论上限 (s) | 理论上限 (输出帧) | 在上限内 | 原工程时间模型最大残差 (px) |",
              "|---|---|---|---|---|---|---|---|---|---|---|"]
    for case in result["cases"]:
        ep = case["endpoint"]
        diag = case.get("original_time_check") or {}
        diag_resid = diag.get("max_abs_residual_px")
        diag_text = f"{diag_resid:.4f}" if isinstance(diag_resid, float) and math.isfinite(diag_resid) else "-"
        lines.append(
            f"| {case['id']} | {ep['k']} | {ep['retimed_first_frame']}..{ep['retimed_last_frame']} | "
            f"{ep['T_original_s']:.4f} | {ep['T_retimed_s']:.4f} | {ep['delta_s']:+.4f} | "
            f"{ep['delta_output_frames']:+.2f} | {ep['bound_s']:.5f} | {ep['bound_output_frames']:.2f} | "
            f"{'是' if ep['within_bound'] else '否'} | {diag_text} |")
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
        "- 运动模型假设 AviUtl 的直线移动在 [起点, 起点 + T] 之间线性插值, 末值保持到之后。"
        "`retimed` 模式下起点与 T' 由重定时后的帧范围 s', e' 推出 (规则与 tools/aup2_fps_upconvert.py 相同), "
        "不是名义 5 s。这一点需要宿主实测确认。",
        "- `original` 模式用原工程 frame= 行推出的 T = (末帧 - 首帧)/源帧率。它只适用于插件 Hook 路径样本 "
        "(dry_run_simulated), 这些样本没有经过重定时导出。",
        "- `precise` 模式对应 `--precise` 导出: 直线沿原工程时间走 (速度与原工程相同), 不在原终点截断, "
        "一直延续到重定时后对象的最后一帧 e'/目标帧率。非精确导出 (如 1.1.0 矩阵) 在该模式下预期 FAIL。",
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
    parser.add_argument("--timeline", choices=TIMELINE_MODES, default="retimed",
                        help="motion model for PASS/FAIL: retimed (Free FPS Export, default), "
                             "original (plugin Hook samples, not retimed) or precise (--precise export)")
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
                         str(args.ffprobe) if args.ffprobe else None, args.timeline)
            for cid in selected
        ]
    except (InputError, OSError, json.JSONDecodeError, KeyError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_ERROR

    all_pass = all(r["status"] == "PASS" for r in results)
    result = {
        "label": args.label,
        "run_dir": str(run_dir),
        "timeline": args.timeline,
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
