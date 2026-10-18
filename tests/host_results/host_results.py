"""Recheck the recorded AviUtl host measurements of test_kit 7 (track movement) and 8 (interpolation
motion with time control) and print Markdown.

CI cannot run the AviUtl host, so the per-frame centroids measured on the host are checked in under
test_kit/7_轨迹移动方式/data/ and test_kit/8_補間移動時間制御/data/ and recomputed here (standard library only):

  R1a.csv R1b.csv   same motion, without / with time control: must agree frame by frame
  R1c.csv R1d.csv   intermediate-point motion, without / with time control
  R1d.csv           compared with a constant-speed triangle model over the whole duration
  R2.csv            curve test; progress u = (x - 360) / 1200 every 10 frames
  R2_曲线.aup2       project file; its X curve line is printed
  R3a/R3b/R3c_30.csv and R3a/R3b/R3c_60.csv
                    the same circle rendered at 30 fps and 60 fps. R3a (plain 補間移動) follows keyframe
                    time: 60 fps frame n is compared at 30 fps time n/2. R3b and R3c (補間移動(時間制御),
                    R3c with a curve) follow object progress: 60 fps frame n (u = n/299) is compared at
                    30 fps position u*149, linear interpolation.

  test_kit/9_其他移动方式/data/*.csv (probe 9, frame, centroid_x, pixels, dx, run)
                    round 1: 往复运动_60 against x = 360 + 600 (1 - cos(f pi / 15)) at every frame, and random_60
                    frame m against random_30 frame m (m = 0..149): both prove the motions are per frame.
                    round 2: the 60/66 fps renders against the 30 fps ones at the same time (B1[2n] / random_30[n],
                    B2[11j] / random_30[5j], B3[2n] / 往复运动_30[n], B5[11j] / B4[5j], B8[2n] / B6[n]).
                    round 3 A: claims about the C files (object-local frame, intermediate points ignored,
                    decimal precision, reproducible); round 3 B: end-to-end E files against formulas or B files.
                    E5 is INFO only and does not decide the exit code.
                    A row with fewer than 1000 pixels is invalid (analyze_steps.py can take a grey spot for the
                    circle); a comparison that uses one is FAIL.

Exit code 1 when the R1a vs R1b maximum difference is >= 0.5 px, when the R3b progress-aligned maximum
difference is >= 0.5 px, or when a probe 9 comparison is FAIL; 2 when an input is missing.
Usage: python -I tests/host_results/host_results.py [--data-dir DIR] [--r3-data-dir DIR]
"""

import argparse
import csv
import math
import os
import statistics
import sys

THRESHOLD_PX = 0.5
FRAME_COUNT = 150          # frames 0..149 (video.rate=30, frame=0,149)
X_START = 360.0            # centroid x at the start of the R1 path
SEGMENT_PX = 1200.0        # 360 -> 1560 -> 360 -> 1560 is 3600 px
PERIOD_PX = 2 * SEGMENT_PX
CURVE_X_START = 360.0      # R2 progress u = (x - 360) / 1200
CURVE_SPAN_PX = 1200.0
R3_FRAMES_30 = 150         # R3 30 fps: frames 0..149
R3_FRAMES_60 = 300         # R3 60 fps: frames 0..299
R3_EXCLUDE_FIRST = 3       # R3 "n>=3" rows drop the first frames of the R3c start jump
R3_EXCL_LIMIT_PX = 10.0    # R3c n>=3 limit under linear interpolation (steep segment allowance)

DEFAULT_DATA_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), os.pardir, os.pardir,
    "test_kit", "7_轨迹移动方式", "data")
R3_DATA_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), os.pardir, os.pardir,
    "test_kit", "8_補間移動時間制御", "data")
R3_GROUPS = ["R3a", "R3b", "R3c"]
PROBE9_DATA_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), os.pardir, os.pardir,
    "test_kit", "9_其他移动方式", "data")
PROBE9_FILES = ["B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "random_30", "random_60", "往复运动_30", "往复运动_60",
                "C1", "C2", "C4", "C5", "C6", "C7", "C8", "C10", "E1", "E2_60", "E3_5994", "E4", "E5", "E6", "E7"]
PROBE9_MIN_PIXELS = 1000   # rows below this are not a circle (see the module docstring)
PROBE9_ROUND2 = [          # (label, file A, A frame step, file B, B frame step, frames compared)
    ("B1[2n] vs random_30[n]", "B1", 2, "random_30", 1, 150),
    ("B2[11j] vs random_30[5j]", "B2", 11, "random_30", 5, 30),
    ("B3[2n] vs 往复运动_30[n]", "B3", 2, "往复运动_30", 1, 150),
    ("B5[11j] vs B4[5j]", "B5", 11, "B4", 5, 30),
    ("B8[2n] vs B6[n]", "B8", 2, "B6", 1, 150),
]


def load_centroids(path):
    with open(path, encoding="utf-8-sig", newline="") as fh:
        return {int(row["frame"]): float(row["centroid_x"])
                for row in csv.DictReader(fh) if row["centroid_x"]}


def max_diff(a, b):
    keys = sorted(set(a) & set(b))
    worst = max(keys, key=lambda k: abs(a[k] - b[k]))
    return abs(a[worst] - b[worst]), worst, len(keys)


def triangle(arc):
    """Centroid after travelling `arc` px along 360 -> 1560 -> 360 -> 1560 at constant speed."""
    seg = arc % PERIOD_PX
    return X_START + seg if seg <= SEGMENT_PX else X_START + PERIOD_PX - seg


def model_error(centroids):
    speed = (3 * SEGMENT_PX) / (FRAME_COUNT - 1)  # px per frame, constant over the whole clip
    worst_err, worst_frame = 0.0, 0
    for frame in range(FRAME_COUNT):
        err = abs(triangle(frame * speed) - centroids[frame])
        if err > worst_err:
            worst_err, worst_frame = err, frame
    return worst_err, worst_frame, speed


def read_x_line(path):
    with open(path, "rb") as fh:
        for raw in fh.read().split(b"\r\n"):
            if raw.startswith(b"X="):
                return raw.decode("utf-8", errors="replace")
    return None


def verdict(diff):
    return "PASS" if diff < THRESHOLD_PX else "FAIL"


def same_or_differs(diff):
    # R1c vs R1d is informational: R1d's midpoint timing is expected to be ignored by the host.
    return "SAME" if diff < THRESHOLD_PX else "DIFFERS"


def sample(series, frame):
    """Linear interpolation of a per-frame list at a fractional frame, clamped to the last frame."""
    if frame >= len(series) - 1:
        return series[-1]
    i = int(frame)
    t = frame - i
    return series[i] * (1.0 - t) + series[i + 1] * t


def progress_frame(n):
    """60 fps frame n has object progress u = n / 299, which is 30 fps frame u * 149."""
    return n * (R3_FRAMES_30 - 1) / (R3_FRAMES_60 - 1)


def time_frame(n):
    """60 fps frame n is at n / 60 s, which is 30 fps frame n / 2."""
    return n * 30.0 / 60.0


def r3_diffs(c30, c60, to_30_frame):
    """(n, |x60[n] - x30 at to_30_frame(n)|) for every 60 fps frame n."""
    s30 = [c30[i] for i in range(R3_FRAMES_30)]
    return [(n, abs(c60[n] - sample(s30, to_30_frame(n)))) for n in range(R3_FRAMES_60)]


def r3_summary(diffs):
    """(max diff, 60 fps frame of the max, median diff, number of diffs > THRESHOLD_PX)."""
    worst_n, worst = max(diffs, key=lambda p: p[1])
    values = [d for _, d in diffs]
    over = sum(1 for d in values if d > THRESHOLD_PX)
    return worst, worst_n, statistics.median(values), over


def load_probe9(path):
    """{frame: (centroid_x, pixels)} of one test_kit 9 csv."""
    with open(path, encoding="utf-8-sig", newline="") as fh:
        return {int(row["frame"]): (float(row["centroid_x"]), int(row["pixels"]))
                for row in csv.DictReader(fh) if row["centroid_x"]}


def probe9_model(series):
    """往复运动_60 against x = 360 + 600 (1 - cos(f pi / 15)) at every frame: (frames, max diff, frame of max, invalid rows)."""
    worst, worst_frame, invalid = None, None, 0
    for frame, (x, pixels) in sorted(series.items()):
        invalid += pixels < PROBE9_MIN_PIXELS
        diff = abs(x - (360.0 + 600.0 * (1.0 - math.cos(frame * math.pi / 15))))
        if worst is None or diff > worst:
            worst, worst_frame = diff, frame
    return len(series), worst, worst_frame, invalid


def probe9_pairs(a, b, steps):
    """(frames, max diff, frame of max in a, invalid rows used) over (frame in a, frame in b) steps.
    A row is invalid when it is missing or below PROBE9_MIN_PIXELS; a pair with a missing row has no difference."""
    worst, worst_frame, invalid = None, None, 0
    for frame_a, frame_b in steps:
        rows = [a.get(frame_a), b.get(frame_b)]
        invalid += sum(1 for row in rows if row is None or row[1] < PROBE9_MIN_PIXELS)
        if rows[0] is None or rows[1] is None:
            continue
        diff = abs(rows[0][0] - rows[1][0])
        if worst is None or diff > worst:
            worst, worst_frame = diff, frame_a
    return len(steps), worst, worst_frame, invalid


def probe9_verdict(worst, invalid):
    return "PASS" if invalid == 0 and worst is not None and worst < THRESHOLD_PX else "FAIL"


def probe9_section(p9):
    """(markdown lines, verdicts) of test_kit 9. p9 maps each name of PROBE9_FILES to its loaded rows."""
    lines = ["#### Probe 9 第 1 轮：按帧 / round 1: per-frame motion", "",
             "| 比较 Comparison | 帧数 Frames | 最大差 Max diff (px) | 最大差帧 At frame | 无效行 Invalid rows | 判定 Verdict |",
             "|---|---|---|---|---|---|"]
    verdicts = []

    def row(label, frames, worst, worst_frame, invalid):
        verdict = probe9_verdict(worst, invalid)
        verdicts.append(verdict)
        lines.append("| %s | %d | %.4f | %s | %d | %s |"
                     % (label, frames, worst, "-" if worst_frame is None else str(worst_frame), invalid, verdict))

    row("往复运动_60 vs 360 + 600(1-cos(fπ/15)), all frames", *probe9_model(p9["往复运动_60"]))
    row("random_60[m] vs random_30[m], m=0..149",
        *probe9_pairs(p9["random_60"], p9["random_30"], [(m, m) for m in range(150)]))
    lines += ["", "#### Probe 9 第 2 轮：同一时刻 / round 2: same moment, different fps", "",
              "| 比较 Comparison | 对数 Pairs | 最大差 Max diff (px) | 最大差帧 At 60/66 fps frame | 无效行 Invalid rows | 判定 Verdict |",
              "|---|---|---|---|---|---|"]
    for label, name_a, step_a, name_b, step_b, count in PROBE9_ROUND2:
        steps = [(step_a * i, step_b * i) for i in range(count)]
        frames, worst, worst_frame, invalid = probe9_pairs(p9[name_a], p9[name_b], steps)
        verdict = probe9_verdict(worst, invalid)
        verdicts.append(verdict)
        lines.append("| %s | %d | %.4f | %s | %d | %s |"
                     % (label, frames, worst, "-" if worst_frame is None else str(worst_frame), invalid, verdict))
    lines += ["", "Threshold: %.1f px. Rows with fewer than %d pixels are invalid; a comparison that uses one is FAIL."
              % (THRESHOLD_PX, PROBE9_MIN_PIXELS), ""]
    return lines, verdicts


def probe9_formula(series, frames, expected):
    """series against expected(frame) over frames: (frames, max diff, frame of max, invalid rows used).
    A frame missing from series is invalid and has no difference."""
    worst, worst_frame, invalid = None, None, 0
    for frame in frames:
        row = series.get(frame)
        invalid += row is None or row[1] < PROBE9_MIN_PIXELS
        if row is None:
            continue
        diff = abs(row[0] - expected(frame))
        if worst is None or diff > worst:
            worst, worst_frame = diff, frame
    return len(frames), worst, worst_frame, invalid


def probe9_round3_section(p9):
    """(markdown lines, verdicts) of probe 9 round 3. E5 is INFO: its row is printed but adds no verdict."""
    def c1(f): return 360.0 + 600.0 * (1.0 - math.cos((f - 10) * math.pi / 15))
    def c2(f): return 360.0 + 4.0 * (f - 10)                       # X = -600 + 4(f-10) in project coordinates
    def c7(f): return 360.0 + 600.0 * (1.0 - math.cos(f * math.pi / 7.123457))
    def c8(f): return 360.0 + 4.00 * f                             # X = -600 + 4.00 f in project coordinates
    def e2(f): return 360.0 + 600.0 * (1.0 - math.cos(math.pi * (f / 60) * 30 / 7))
    def e3(f): return 360.0 + 600.0 * (1.0 - math.cos(math.pi * (f * 1001 / 60000) * 30 / 7))
    def e5(f): return 360.0 + 120.0 * f * 1001 / 60000             # X = -600 + 120 f 1001/60000 in project coordinates
    def e7(f): return 360.0 + 600.0 * (1.0 - math.cos(math.pi * (f / 132) * 66 / 15.4))

    head = ["| 比较 Comparison | 帧数 Frames | 最大差 Max diff (px) | 最大差帧 At frame | 无效行 Invalid rows | 判定 Verdict |",
            "|---|---|---|---|---|---|"]
    verdicts = []

    def row(label, result, verdict=None):
        frames, worst, worst_frame, invalid = result
        if verdict is None:
            verdict = probe9_verdict(worst, invalid)
            verdicts.append(verdict)
        lines.append("| %s | %d | %.4f | %s | %d | %s |"
                     % (label, frames, worst, "-" if worst_frame is None else str(worst_frame), invalid, verdict))

    lines = ["#### Probe 9 第 3 轮 A：结论 / round 3 A: claims", "",
             "Claims: 对象内帧号 / 忽略中间点 / 小数精度 / 可复现 (object-local frame, intermediate points ignored, "
             "decimal precision, reproducible). PASS means the measurement agrees with the claim.", ""] + head
    row("C1 vs 360 + 600(1-cos((f-10)π/15)), f=10..149", probe9_formula(p9["C1"], range(10, 150), c1))
    row("C2 vs X = -600 + 4(f-10), f=10..149", probe9_formula(p9["C2"], range(10, 150), c2))
    row("C4[f] vs 往复运动_30[f], f=0..149",
        probe9_pairs(p9["C4"], p9["往复运动_30"], [(f, f) for f in range(150)]))
    row("C5[f] vs random_30[f], f=0..149",
        probe9_pairs(p9["C5"], p9["random_30"], [(f, f) for f in range(150)]))
    row("C6[f] vs B6[f], f=0..149",
        probe9_pairs(p9["C6"], p9["B6"], [(f, f) for f in range(150)]))
    row("C7 vs 360 + 600(1-cos(fπ/7.123457)), f=0..149", probe9_formula(p9["C7"], range(150), c7))
    row("C8 vs X = -600 + 4.00f, f=0..299", probe9_formula(p9["C8"], range(300), c8))
    row("C10[f] vs random_30[f], f=0..149",
        probe9_pairs(p9["C10"], p9["random_30"], [(f, f) for f in range(150)]))
    lines += ["", "#### Probe 9 第 3 轮 B：端到端 / round 3 B: end to end", ""] + head
    row("E1[f] vs B1[f], f=0..299", probe9_pairs(p9["E1"], p9["B1"], [(f, f) for f in range(300)]))
    row("E2_60 vs 360 + 600(1-cos(π(f/60)30/7)), f=0..299", probe9_formula(p9["E2_60"], range(300), e2))
    row("E3_5994 vs 360 + 600(1-cos(π t 30/7)), t = f·1001/60000, f=0..299",
        probe9_formula(p9["E3_5994"], range(300), e3))
    row("E4[f] vs B8[f], f=0..299", probe9_pairs(p9["E4"], p9["B8"], [(f, f) for f in range(300)]))
    row("E6[2m] vs B2[m], m=0..329", probe9_pairs(p9["E6"], p9["B2"], [(2 * m, m) for m in range(330)]))
    row("E7 vs 360 + 600(1-cos(π(f/132)66/15.4)), f=0..659", probe9_formula(p9["E7"], range(660), e7))
    row("E5 vs X = -600 + 120·f·1001/60000, f=0..299", probe9_formula(p9["E5"], range(300), e5),
        "INFO（已知偏差，转换器现在拒绝此情况）")
    lines += ["", "Threshold: %.1f px. Round 3 A and B are judged like rounds 1 and 2; E5 is INFO and does not "
              "decide the exit code." % THRESHOLD_PX, ""]
    return lines, verdicts


def build_report(data_dir, r3_dir=R3_DATA_DIR, p9_dir=PROBE9_DATA_DIR):
    """Return (markdown_text, exit_code)."""
    names = ["R1a", "R1b", "R1c", "R1d", "R2"]
    paths = {n: os.path.join(data_dir, n + ".csv") for n in names}
    aup2 = os.path.join(data_dir, "R2_曲线.aup2")
    r3_names = [g + suffix for g in R3_GROUPS for suffix in ("_30", "_60")]
    r3_paths = {n: os.path.join(r3_dir, n + ".csv") for n in r3_names}
    p9_paths = {n: os.path.join(p9_dir, n + ".csv") for n in PROBE9_FILES}
    for path in list(paths.values()) + list(r3_paths.values()) + list(p9_paths.values()) + [aup2]:
        if not os.path.isfile(path):
            raise FileNotFoundError(path)
    c = {n: load_centroids(paths[n]) for n in names}
    r3 = {n: load_centroids(path) for n, path in r3_paths.items()}
    p9 = {n: load_probe9(path) for n, path in p9_paths.items()}

    lines = ["### 宿主实测复算 / Host measurement recheck", "",
             "Source: `test_kit/7_轨迹移动方式/data/` (per-frame centroid of the host render)", ""]

    diff_ab, frame_ab, n_ab = max_diff(c["R1a"], c["R1b"])
    diff_cd, frame_cd, n_cd = max_diff(c["R1c"], c["R1d"])
    lines += ["#### R1 时间控制等价 / time-control equivalence", "",
              "| 比较 Pair | 帧数 Frames | 最大差 Max diff (px) | 最大差帧 At frame | 判定 Verdict |",
              "|---|---|---|---|---|",
              "| R1a vs R1b | %d | %.4f | %d | %s |" % (n_ab, diff_ab, frame_ab, verdict(diff_ab)),
              "| R1c vs R1d | %d | %.4f | %d | %s |" % (n_cd, diff_cd, frame_cd, same_or_differs(diff_cd)),
              "", "Threshold: %.1f px. Only R1a vs R1b decides the exit code; R1c vs R1d is reported as "
              "SAME or DIFFERS." % THRESHOLD_PX, ""]

    err, err_frame, speed = model_error(c["R1d"])
    lines += ["#### R1d 与全程匀速模型 / R1d vs constant-speed model", "",
              "| 模型 Model | 速度 Speed (px/frame) | 最大误差 Max error (px) | 最大误差帧 At frame | 判定 Verdict |",
              "|---|---|---|---|---|",
              "| 360 -> 1560 -> 360 -> 1560 over %d frames | %.4f | %.3f | %d | %s |"
              % (FRAME_COUNT, speed, err, err_frame, verdict(err)),
              ""]

    lines += ["#### R2 曲线进度 / R2 curve progress", "",
              "| 帧 Frame | x (px) | u = (x-360)/1200 |", "|---|---|---|"]
    frames = list(range(0, FRAME_COUNT, 10)) + [FRAME_COUNT - 1]
    for frame in frames:
        x = c["R2"][frame]
        lines.append("| %d | %.2f | %.4f |" % (frame, x, (x - CURVE_X_START) / CURVE_SPAN_PX))
    lines.append("")

    x_line = read_x_line(aup2)
    lines += ["#### R2 曲线载荷 / R2 curve payload (`R2_曲线.aup2`)", ""]
    if x_line is None:
        lines.append("X line not found in the project file.")
    else:
        fields = x_line.split(",")
        lines += ["| 项目 Item | 值 Value |", "|---|---|",
                  "| X 行 raw line | `%s` |" % x_line.replace("|", "\\|"),
                  "| X 范围 range | %s .. %s |" % (fields[0][2:], fields[1]),
                  "| 曲线 curve (last four fields) | `%s` |" % ",".join(fields[-4:])]
    lines.append("")

    # (group, alignment, 60 fps frame -> 30 fps frame, first 60 fps frame kept, limit or None = report only)
    r3_cases = [
        ("R3a", "time n/2", time_frame, 0, None),
        ("R3a", "time n/2", time_frame, R3_EXCLUDE_FIRST, None),
        ("R3b", "progress u*149", progress_frame, 0, THRESHOLD_PX),
        ("R3c", "progress u*149", progress_frame, 0, None),
        ("R3c", "progress u*149", progress_frame, R3_EXCLUDE_FIRST, R3_EXCL_LIMIT_PX),
    ]
    lines += ["#### R3 補間移動 进度与时间对齐 / R3 progress vs time alignment", "",
              "Source: `test_kit/8_補間移動時間制御/data/` (per-frame centroid, 30 fps vs 60 fps renders)", "",
              "| 组 Group | 对齐 Alignment | 范围 Frames | 帧数 Count | 最大差 Max diff (px) | 最大差帧 At 60fps frame "
              "| 中位差 Median (px) | >0.5 px 帧数 | 判定 Verdict |",
              "|---|---|---|---|---|---|---|---|---|"]
    r3_worst = {}
    for group, align, to_30_frame, first, limit in r3_cases:
        diffs = r3_diffs(r3[group + "_30"], r3[group + "_60"], to_30_frame)[first:]
        worst, worst_n, median, over = r3_summary(diffs)
        r3_worst[(group, first)] = worst
        judged = "INFO" if limit is None else ("PASS" if worst < limit else "FAIL")
        lines.append("| %s | %s | %s | %d | %.4f | %d | %.4f | %d | %s |"
                     % (group, align, "all" if first == 0 else "n>=%d" % first, len(diffs),
                        worst, worst_n, median, over, judged))
    lines += ["",
              "R3a and R3c all-frame rows are report-only. R3b (all frames) is judged against %.1f px and decides "
              "the exit code. R3c n>=%d is judged against %.1f px under linear interpolation (steep start segment) "
              "and does not decide the exit code. Median and >0.5 px counts are reported for every row."
              % (THRESHOLD_PX, R3_EXCLUDE_FIRST, R3_EXCL_LIMIT_PX), ""]

    p9_lines, p9_verdicts = probe9_section(p9)
    r3_p9_lines, r3_p9_verdicts = probe9_round3_section(p9)
    p9_verdicts += r3_p9_verdicts
    lines += ["### 其他移动方式 / Other motion methods (test_kit 9)", "",
              "Source: `test_kit/9_其他移动方式/data/` (per-frame centroid of the host render)", ""] + p9_lines + r3_p9_lines

    exit_code = 1 if diff_ab >= THRESHOLD_PX or r3_worst[("R3b", 0)] >= THRESHOLD_PX or "FAIL" in p9_verdicts else 0
    return "\n".join(lines) + "\n", exit_code


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--data-dir", default=DEFAULT_DATA_DIR)
    parser.add_argument("--r3-data-dir", default=R3_DATA_DIR)
    args = parser.parse_args(argv)
    try:
        report, code = build_report(os.path.normpath(args.data_dir), os.path.normpath(args.r3_data_dir))
    except FileNotFoundError as exc:
        print("missing input: %s" % exc, file=sys.stderr)
        return 2
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stdout.write(report)
    return code


if __name__ == "__main__":
    sys.exit(main())
