"""Recheck the recorded AviUtl host measurements of test_kit 7 (track movement) and print Markdown.

CI cannot run the AviUtl host, so the per-frame centroids measured on the host are checked in under
test_kit/7_轨迹移动方式/data/ and recomputed here (standard library only):

  R1a.csv R1b.csv   same motion, without / with time control: must agree frame by frame
  R1c.csv R1d.csv   intermediate-point motion, without / with time control
  R1d.csv           compared with a constant-speed triangle model over the whole duration
  R2.csv            curve test; progress u = (x - 360) / 1200 every 10 frames
  R2_曲线.aup2       project file; its X curve line is printed

Exit code 1 when the R1a vs R1b maximum difference is >= 0.5 px; 2 when an input is missing.
Usage: python -I tests/host_results/host_results.py [--data-dir DIR]
"""

import argparse
import csv
import os
import sys

THRESHOLD_PX = 0.5
FRAME_COUNT = 150          # frames 0..149 (video.rate=30, frame=0,149)
X_START = 360.0            # centroid x at the start of the R1 path
SEGMENT_PX = 1200.0        # 360 -> 1560 -> 360 -> 1560 is 3600 px
PERIOD_PX = 2 * SEGMENT_PX
CURVE_X_START = 360.0      # R2 progress u = (x - 360) / 1200
CURVE_SPAN_PX = 1200.0

DEFAULT_DATA_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), os.pardir, os.pardir,
    "test_kit", "7_轨迹移动方式", "data")


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


def build_report(data_dir):
    """Return (markdown_text, exit_code)."""
    names = ["R1a", "R1b", "R1c", "R1d", "R2"]
    paths = {n: os.path.join(data_dir, n + ".csv") for n in names}
    aup2 = os.path.join(data_dir, "R2_曲线.aup2")
    for path in list(paths.values()) + [aup2]:
        if not os.path.isfile(path):
            raise FileNotFoundError(path)
    c = {n: load_centroids(paths[n]) for n in names}

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

    exit_code = 1 if diff_ab >= THRESHOLD_PX else 0
    return "\n".join(lines) + "\n", exit_code


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--data-dir", default=DEFAULT_DATA_DIR)
    args = parser.parse_args(argv)
    try:
        report, code = build_report(os.path.normpath(args.data_dir))
    except FileNotFoundError as exc:
        print("missing input: %s" % exc, file=sys.stderr)
        return 2
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stdout.write(report)
    return code


if __name__ == "__main__":
    sys.exit(main())
