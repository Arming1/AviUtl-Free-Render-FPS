#!/usr/bin/env python3
"""Frame-difference analysis for probe 13 (test_kit/13_按帧滤镜): frame-based or time-based filters.

Usage:
    python -I experiments/host_runner/analyze_probe13.py

Reads test_kit/13_按帧滤镜/out/<name>.mp4 (written by batch_export.py), decodes each video to
320x180 RGB and compares frames with the mean absolute difference (MAD): 0-255 scale, averaged
over all pixels and the 3 channels. Writes:
    test_kit/13_按帧滤镜/data/probe13_mad.csv      comparison,frame,mad
    test_kit/13_按帧滤镜/data/probe13_summary.csv  comparison,pairs,median,max,count_over_2
    test_kit/13_按帧滤镜/data/probe13_verdict.csv  effect,A_median,B_median,C_median,D30_median,E_median,noise,verdict

count_over_2 is the number of pairs with MAD > 2. It is informational only and not used for verdicts.
Verdicts and periodicity cues go to stdout.

Comparisons (n on the 30 fps side, m on the 60 fps side):
    base_A   F_60[m]   vs F_30[m]     m = 0..149   (noise baseline)
    base_B   F_60[2n]  vs F_30[n]     n = 0..149   (noise baseline)
    A        E_60[m]   vs E_30[m]     m = 0..149   same frame number
    B        E_60[2n]  vs E_30[n]     n = 0..149   same moment
    C        E_60c[2n] vs E_30[n]     n = 0..149   same moment, 60c = converted 60 fps file
    D_30     E_30[n]   vs E_30[n-1]   n = 1..149   change between consecutive frames
    D_60     E_60[m]   vs E_60[m-1]   m = 1..299   whole 60 fps clip
    E        E_30[n]   vs F_30[n]     n = 0..149   visibility against the plain circle

Noise N = max over both baseline comparisons (max MAD). Each filter is judged by the medians of
A, B, C, E and by the largest D value, using ratios (no fixed MAD cut-off):
    B <= 5N and A >= 20*B                         -> 按秒
    A <= 5N and B >= 20*A                         -> 按帧号
    C <= 5N and B >= 20*C                         -> 换算后按秒
    E <= 5N                                       -> 不可见，探针无效
    all D values <= 5N                            -> 不动
    otherwise                                     -> 不确定
The rules are checked in this order; the first match is the verdict.
"""

from __future__ import annotations

import csv
import subprocess
import sys
from collections import Counter
from pathlib import Path

import numpy as np

REPO_ROOT = Path(__file__).resolve().parents[2]
KIT_DIR = REPO_ROOT / "test_kit" / "13_按帧滤镜"
OUT_DIR = KIT_DIR / "out"
DATA_DIR = KIT_DIR / "data"
MAD_CSV = DATA_DIR / "probe13_mad.csv"
SUMMARY_CSV = DATA_DIR / "probe13_summary.csv"
VERDICT_CSV = DATA_DIR / "probe13_verdict.csv"

W, H = 320, 180
FRAME_BYTES = W * H * 3
N30 = 150  # 30 fps clips have 150 frames; n = 0..149
N60 = 300  # 60 fps clips have 300 frames; 2n goes up to 298, and D_60 uses the whole clip
COUNT_OVER = 2.0  # informational column in the summary only
NOISE_RATIO = 5.0  # "same as noise" when a median (or a max) is <= 5 N
SEPARATION = 20.0  # "clearly different" when one median is >= 20 x the other
DOMINANT_TOP = 5  # frames listed for the periodicity cue
MAX_LAG = 40  # autocorrelation lags checked

# (key, video base name); videos are <base>_30, <base>_60, <base>_60换算
FILTERS = [
    ("E1", "E1_振動"),
    ("E2", "E2_手ぶれ"),
    ("E3", "E3_ラスター"),
    ("E4", "E4_フレア"),
    ("E1b", "E1b_振動"),  # vibration above the circle (circle at layer 1, filter at layer 2)
    ("E4b", "E4b_フレア"),  # flare with 光芒変化速度 5.0 / 5.0 / 2.5
]
BASE_NAME = "F_底稿"


def decode(name: str) -> np.ndarray:
    path = OUT_DIR / f"{name}.mp4"
    if not path.is_file():
        sys.exit(f"missing video: {path}  (run batch_export.py first)")
    cmd = [
        "ffmpeg", "-v", "error", "-i", str(path), "-an",
        "-vf", f"scale={W}:{H}", "-pix_fmt", "rgb24",
        "-fps_mode", "passthrough", "-f", "rawvideo", "-",
    ]
    proc = subprocess.run(cmd, capture_output=True)
    if proc.returncode != 0:
        sys.exit(f"ffmpeg failed for {path}: {proc.stderr.decode('utf-8', 'replace')}")
    if len(proc.stdout) % FRAME_BYTES:
        sys.exit(f"unexpected rawvideo size for {path}")
    return np.frombuffer(proc.stdout, np.uint8).reshape(-1, H, W, 3)


def load(name: str, need: int) -> np.ndarray:
    frames = decode(name)
    if len(frames) < need:
        sys.exit(f"{name}: {len(frames)} frames, need at least {need}")
    print(f"  loaded {name}: {len(frames)} frames")
    return frames


def mad(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.abs(a.astype(np.int16) - b.astype(np.int16)).mean())


def compare(name: str, a_frames: np.ndarray, b_frames: np.ndarray, pairs: list[tuple[int, int, int]]):
    """pairs: (index into a, index into b, frame label). Returns (name, frame label, MAD) rows."""
    return [(name, label, mad(a_frames[ia], b_frames[ib])) for ia, ib, label in pairs]


def summarize(name: str, rows: list[tuple[str, int, float]]) -> tuple:
    values = np.array([r[2] for r in rows])
    return (name, len(values), float(np.median(values)), float(values.max()), int((values > COUNT_OVER).sum()))


def verdict(med: dict[str, float], d_max: float, noise: float) -> str:
    """Ratio rules from the probe 13 brief; see the module docstring for the order."""
    n5 = NOISE_RATIO * noise
    a, b, c, e = med["A"], med["B"], med["C"], med["E"]
    if b <= n5 and a >= SEPARATION * b:
        return "按秒"
    if a <= n5 and b >= SEPARATION * a:
        return "按帧号"
    if c <= n5 and b >= SEPARATION * c:
        return "换算后按秒"
    if e <= n5:
        return "不可见，探针无效"
    if d_max <= n5:
        return "不动"
    return "不确定"


def autocorr(x: np.ndarray, max_lag: int) -> np.ndarray:
    centered = x - x.mean()
    denom = float((centered * centered).sum())
    if denom == 0:
        return np.zeros(max_lag + 1)
    return np.array([1.0] + [float((centered[:-k] * centered[k:]).sum()) / denom for k in range(1, max_lag + 1)])


def periodicity_cue(label: str, labels: list[int], values: np.ndarray, threshold: float, fps: int) -> list[str]:
    lines = [f"  {label}（帧号单位 = 该视频自己的帧，{fps} fps）："]
    over = [lab for lab, v in zip(labels, values) if v > threshold]
    lines.append(f"    MAD > 5N({threshold:.4f}) 的帧 {len(over)} 个: {over[:40]}{' ...' if len(over) > 40 else ''}")
    if len(over) >= 2:
        gaps = Counter(int(b - a) for a, b in zip(over, over[1:]))
        lines.append(f"    相邻大变化的间隔（帧数: 次数）: {dict(sorted(gaps.items()))}")
    top = sorted(zip(labels, values), key=lambda t: -t[1])[:DOMINANT_TOP]
    lines.append("    MAD 最大的 %d 帧: %s" % (DOMINANT_TOP, ", ".join(f"{lab}({v:.2f})" for lab, v in top)))
    if len(values) > MAX_LAG + 1:
        ac = autocorr(values, MAX_LAG)
        best = sorted(range(2, MAX_LAG + 1), key=lambda k: -ac[k])[:3]
        lines.append("    自相关最高的滞后（帧）: " + ", ".join(f"{k}({ac[k]:.2f})" for k in best))
    return lines


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    all_rows: list[tuple[str, int, float]] = []
    summaries: list[tuple] = []

    print("== 对照组（噪声基线）")
    base30 = load(f"{BASE_NAME}_30", N30)
    base60 = load(f"{BASE_NAME}_60", N60)
    base_a = compare("base_A", base60, base30, [(m, m, m) for m in range(N30)])
    base_b = compare("base_B", base60, base30, [(2 * n, n, n) for n in range(N30)])
    for rows in (base_a, base_b):
        all_rows += rows
        summaries.append(summarize(rows[0][0], rows))
    noise = max(max(r[2] for r in base_a), max(r[2] for r in base_b))
    threshold = NOISE_RATIO * noise
    print(f"  噪声上限 N = {noise:.4f}，5N = {threshold:.4f}")

    results: list[tuple[str, dict[str, float], float, str]] = []
    notes: list[str] = []
    cues: list[str] = []
    for key, base in FILTERS:
        print(f"== {key} {base}")
        e30 = load(f"{base}_30", N30)
        e60 = load(f"{base}_60", N60)
        e60c = load(f"{base}_60换算", N60)
        rows = {
            "A": compare(f"{key}_A_same_frame", e60, e30, [(m, m, m) for m in range(N30)]),
            "B": compare(f"{key}_B_same_time", e60, e30, [(2 * n, n, n) for n in range(N30)]),
            "C": compare(f"{key}_C_converted_time", e60c, e30, [(2 * n, n, n) for n in range(N30)]),
            "D_30": compare(f"{key}_D_consec_30", e30, e30, [(n, n - 1, n) for n in range(1, N30)]),
            "D_60": compare(f"{key}_D_consec_60", e60, e60, [(m, m - 1, m) for m in range(1, N60)]),
            "E": compare(f"{key}_E_visible", e30, base30, [(n, n, n) for n in range(N30)]),
        }
        med: dict[str, float] = {}
        mx: dict[str, float] = {}
        for tag, group in rows.items():
            all_rows += group
            summaries.append(summarize(group[0][0], group))
            med[tag] = float(np.median([r[2] for r in group]))
            mx[tag] = max(r[2] for r in group)
            s = summaries[-1]
            print(f"  {tag:<4} {s[0]:<26} pairs {s[1]:>3}  median {s[2]:7.3f}  max {s[3]:8.3f}  >2: {s[4]}")
        d_max = max(mx["D_30"], mx["D_60"])
        results.append((key, med, d_max, verdict(med, d_max, noise)))
        if mx["E"] <= noise:
            notes.append(f"{key}: E 最大差 {mx['E']:.4f} 不超过噪声上限 N，逐帧与底稿无差异（检查滤镜是否生效）")
        elif mx["E"] <= threshold:
            notes.append(f"{key}: E 最大差 {mx['E']:.4f} 高于 N 但不超过 5N，效果弱")
        else:
            notes.append(f"{key}: E 最大差 {mx['E']:.4f}，超过 5N，滤镜可见")
        cues.append(f"{key} 的周期线索:")
        cues += periodicity_cue(
            f"{key} D_30 (E_30 相邻帧差)", [r[1] for r in rows["D_30"]],
            np.array([r[2] for r in rows["D_30"]]), threshold, 30,
        )
        cues += periodicity_cue(
            f"{key} D_60 (E_60 相邻帧差)", [r[1] for r in rows["D_60"]],
            np.array([r[2] for r in rows["D_60"]]), threshold, 60,
        )
        del e30, e60, e60c

    with MAD_CSV.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("comparison,frame,mad\n")
        for name, frame, value in all_rows:
            fh.write(f"{name},{frame},{value:.4f}\n")
    with SUMMARY_CSV.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write("comparison,pairs,median,max,count_over_2\n")
        for name, pairs, med_v, mx_v, over in summaries:
            fh.write(f"{name},{pairs},{med_v:.4f},{mx_v:.4f},{over}\n")
    with VERDICT_CSV.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.writer(fh, lineterminator="\n")
        writer.writerow(["effect", "A_median", "B_median", "C_median", "D30_median", "E_median", "noise", "verdict"])
        for key, med, _d_max, text in results:
            writer.writerow([key, f"{med['A']:.4f}", f"{med['B']:.4f}", f"{med['C']:.4f}",
                             f"{med['D_30']:.4f}", f"{med['E']:.4f}", f"{noise:.4f}", text])

    print()
    print(f"噪声上限 N = {noise:.4f}，5N = {threshold:.4f}")
    for key, _med, _d_max, text in results:
        print(f"结论 {key}: {text}")
    for note in notes:
        print(f"注: {note}")
    print()
    print("\n".join(cues))
    print()
    print(f"written: {MAD_CSV}")
    print(f"written: {SUMMARY_CSV}")
    print(f"written: {VERDICT_CSV}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
