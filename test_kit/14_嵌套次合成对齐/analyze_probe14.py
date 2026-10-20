#!/usr/bin/env python3
"""Analysis for probe 14 (nested sub-scene alignment), test_kit/14_嵌套次合成对齐.

Usage:
    python -I test_kit/14_嵌套次合成对齐/analyze_probe14.py [--dir DIR] [--ffmpeg PATH]
    python -I test_kit/14_嵌套次合成对齐/analyze_probe14.py --self-test

Reads expected.json (written by gen_probe14.py) and DIR/<name>.mp4 for every file of the probe (DIR defaults to the
out folder next to this script; names N1_66.mp4, N1_60g.mp4, N1_60l.mp4, ..., N4_66.mp4). Each video is decoded with
ffmpeg to 320x180 RGB. A frame shows cell i when the top-left patch has the colour of digit i % 6 and the centre
patch has the colour of digit i // 6 (the colours are the 6 RGB corners: red, green, blue, yellow, cyan, magenta).
Black = no cell. The centre square must be drawn on top of the background (the larger layer number is on top in
this host, see gen_probe14.py); a background over it makes the centre patch equal the background.

Checks (per case):
    66   every decoded frame equals the nested prediction (frame count and cell index). Must match exactly.
    60g  every cut (first frame showing a cell, end of playback) is within 1/2 target frame of its exact instant
         m x 60/66 (the cut frame minus m x 10/11). Tolerance 0: every such error is a multiple of 1/11 (because
         m x 10/11 has denominator 11 and the frame is an integer), so the nearest non-passing value is 6/11 > 1/2.
    60l  each cut is at the frame predicted by the local rule, and the cuts beyond 1/2 are the predicted ones, at
         least local_outlier_min of them (N1-N3). A 60l file that is right by the global rule fails here.
Per-frame differences of the 60g and 60l files to their predictions are printed as information only.

Exit codes: 0 all checks pass; 1 a check fails; 2 input error (missing file, undecodable video, no ffmpeg).
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from fractions import Fraction

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
EXPECTED_PATH = os.path.join(HERE, "expected.json")
OUT_DIR = os.path.join(HERE, "out")
WORK_DIR = os.path.join(HERE, "work")
DEFAULT_FFMPEG = r"D:\FFmpeg-master\ffmpeg-2025-11-27-git-61b034a47c-full_build\bin\ffmpeg.exe"

ANALYSIS_W, ANALYSIS_H = 320, 180
SYN_W, SYN_H = 1280, 720
# RGB bits (channel > 128) of each digit colour: red, green, blue, yellow, cyan, magenta
DIGIT_BITS = {(1, 0, 0): 0, (0, 1, 0): 1, (0, 0, 1): 2, (1, 1, 0): 3, (0, 1, 1): 4, (1, 0, 1): 5}
DIGIT_RGB = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255), (255, 0, 255)]
BLACK = "black"
BAD = "bad"
HALF = Fraction(1, 2)
TOL = Fraction(0)

EXIT_PASS, EXIT_FAIL, EXIT_INPUT = 0, 1, 2


class InputError(Exception):
    pass


def digit_of(rgb):
    bits = tuple(1 if v > 128 else 0 for v in rgb)
    if bits == (0, 0, 0):
        return BLACK
    return DIGIT_BITS.get(bits, BAD)


def decode_cells(path, ffmpeg):
    """Cell index (or None for no cell, or BAD) of every decoded frame."""
    cmd = [ffmpeg, "-v", "error", "-i", path, "-vf", "scale=%d:%d:flags=area" % (ANALYSIS_W, ANALYSIS_H),
           "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    proc = subprocess.run(cmd, capture_output=True)
    if proc.returncode != 0:
        raise InputError("ffmpeg failed on %s: %s" % (path, proc.stderr.decode("utf-8", "replace").strip()))
    frame_size = ANALYSIS_W * ANALYSIS_H * 3
    if not proc.stdout or len(proc.stdout) % frame_size:
        raise InputError("no whole frame decoded from %s" % path)
    frames = np.frombuffer(proc.stdout, dtype=np.uint8).reshape(-1, ANALYSIS_H, ANALYSIS_W, 3)
    n = len(frames)
    background = frames[:, 2:12, 2:12].reshape(n, -1, 3).mean(axis=1)
    centre = frames[:, 85:95, 155:165].reshape(n, -1, 3).mean(axis=1)
    cells = []
    for bg, ce in zip(background, centre):
        db, dc = digit_of(bg), digit_of(ce)
        if db == BLACK:
            cells.append(None if dc == BLACK else BAD)
        elif db == BAD or dc in (BLACK, BAD):
            cells.append(BAD)
        else:
            cells.append(db + 6 * dc)
    return cells


def cut_frame(seq, cell):
    """Frame of a cut: first frame showing `cell`; for the end of playback (cell None), the frame after the last
    frame that shows a cell. Same definition as gen_probe14.first_frame. None when the cell never appears."""
    if cell is None:
        shown = [n for n, c in enumerate(seq) if c is not None]
        return max(shown) + 1 if shown else None
    return seq.index(cell) if cell in seq else None


def mismatches(seq, expected):
    count = abs(len(seq) - len(expected))
    count += sum(1 for a, b in zip(seq, expected) if a != b)
    return count


def analyse_case(name, case, measured, k, local_min):
    """Returns (ok, lines) for one case."""
    lines = []
    ok = True
    ok66 = True
    frames = case["frames"]
    per_frame = case["per_frame"]
    for v, seq in measured.items():
        if len(seq) != frames[v]:
            ok = False
            lines.append("  %s: %d frames decoded, expected %d" % (v, len(seq), frames[v]))
            if v == "66":
                ok66 = False
        bad = mismatches(seq, per_frame[v])
        first = next((n for n, (a, b) in enumerate(zip(seq, per_frame[v])) if a != b), None)
        lines.append("  %s: per-frame mismatches vs prediction: %d%s" % (
            v, bad, "" if first is None else " (first at frame %d: got %r, predicted %r)"
            % (first, seq[first], per_frame[v][first])))
        if v == "66" and bad:
            ok66 = False
    ok = ok and ok66
    if "cuts" not in case:
        lines.append("  66 check: %s" % ("PASS" if ok66 else "FAIL"))
        return ok66, lines

    lines.append("  %-22s %8s %10s | %-10s %9s | %-10s %9s" % (
        "cut", "src(66)", "exact 60", "60g frame", "60g err", "60l frame", "60l err"))
    outliers = {"60g": 0, "60l": 0}
    predicted_outliers = {"60g": 0, "60l": 0}
    cut_ok = {"60g": True, "60l": True}
    for cut in case["cuts"]:
        m = cut["src_frame"]
        exact = Fraction(m) * k
        row = {}
        for v in ("60g", "60l"):
            n = cut_frame(measured[v], cut["cell"])
            if n is None:
                cut_ok[v] = False
                row[v] = (None, None)
                continue
            err = n - exact
            row[v] = (n, err)
            if abs(err) > HALF + TOL:
                outliers[v] += 1
            pred_err = Fraction(cut["variants"][v]["error"])
            if abs(pred_err) > HALF + TOL:
                predicted_outliers[v] += 1
            if v == "60g" and abs(err) > HALF + TOL:
                cut_ok["60g"] = False
            if v == "60l" and n != cut["variants"]["60l"]["frame"]:
                cut_ok["60l"] = False

        def cell_text(entry):
            n, err = entry
            if n is None:
                return "%10s %9s" % ("missing", "-")
            return "%10d %+9.4f" % (n, float(err))

        lines.append("  %-22s %8d %10.4f | %s | %s" % (
            cut["label"][:22], m, float(exact), cell_text(row["60g"]), cell_text(row["60l"])))
    g_ok = cut_ok["60g"] and ("60g" in measured)
    l_ok = cut_ok["60l"] and outliers["60l"] == predicted_outliers["60l"] and outliers["60l"] >= local_min
    if not cut_ok["60l"]:
        l_detail = "frames differ from the local prediction"
    elif outliers["60l"] != predicted_outliers["60l"]:
        l_detail = "beyond 1/2: %d measured, %d predicted" % (outliers["60l"], predicted_outliers["60l"])
    elif outliers["60l"] < local_min:
        l_detail = "only %d cuts beyond 1/2 (need %d)" % (outliers["60l"], local_min)
    else:
        l_detail = "%d cuts beyond 1/2 as predicted" % outliers["60l"]
    g_detail = ("all cuts within 1/2 frame" if g_ok
                else "a cut is missing or more than 1/2 frame away (%d beyond)" % outliers["60g"])
    lines.append("  60g cuts: %s (%s)" % ("PASS" if g_ok else "FAIL", g_detail))
    lines.append("  60l cuts: %s (%s)" % ("PASS" if l_ok else "FAIL", l_detail))
    lines.append("  66 check: %s" % ("PASS" if ok66 else "FAIL"))
    return ok and g_ok and l_ok, lines


def run(dir_path, expected_path, ffmpeg):
    try:
        with open(expected_path, encoding="utf-8") as fh:
            expected = json.load(fh)
    except (OSError, ValueError) as exc:
        print("INPUT ERROR: cannot read %s: %s" % (expected_path, exc))
        return EXIT_INPUT
    if not os.path.isfile(ffmpeg):
        print("INPUT ERROR: ffmpeg not found: %s" % ffmpeg)
        return EXIT_INPUT
    k = Fraction(expected["k"])
    local_min = expected["local_outlier_min"]
    videos = {}
    missing = []
    for name, case in expected["cases"].items():
        for v, aup2 in case["files"].items():
            path = os.path.join(dir_path, os.path.splitext(aup2)[0] + ".mp4")
            if not os.path.isfile(path):
                missing.append(path)
            videos[(name, v)] = path
    if missing:
        for path in missing:
            print("INPUT ERROR: missing %s" % path)
        return EXIT_INPUT
    all_ok = True
    for name, case in expected["cases"].items():
        measured = {}
        for v in case["files"]:
            try:
                measured[v] = decode_cells(videos[(name, v)], ffmpeg)
            except InputError as exc:
                print("INPUT ERROR: %s" % exc)
                return EXIT_INPUT
        print("%s: %s" % (name, case["title"]))
        ok, lines = analyse_case(name, case, measured, k, local_min)
        for line in lines:
            print(line)
        print("%s: %s" % (name, "PASS" if ok else "FAIL"))
        print("")
        all_ok = all_ok and ok
    print("VERDICT: %s" % ("PASS" if all_ok else "FAIL"))
    return EXIT_PASS if all_ok else EXIT_FAIL


def render(seq, path, ffmpeg, inner_on_top=True):
    """Synthetic 1280x720 video of a cell sequence (x264, CRF 16) drawn like the host: the background (layer 0) and
    the centre square (layer 1) of each cell; the larger layer number is drawn on top. inner_on_top=False gives the
    first-version layout (background over the centre square), which the host output showed: centre == background."""
    cmd = [ffmpeg, "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", "%dx%d" % (SYN_W, SYN_H),
           "-r", "60", "-i", "-", "-c:v", "libx264", "-preset", "veryfast", "-crf", "16", "-pix_fmt", "yuv420p", path]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    for cell in seq:
        img = np.zeros((SYN_H, SYN_W, 3), dtype=np.uint8)
        if cell is not None:
            background, centre = DIGIT_RGB[cell % 6], DIGIT_RGB[cell // 6]
            if inner_on_top:
                img[:, :] = background
                img[210:510, 490:790] = centre
            else:
                # background on top: the centre square is hidden, so the centre pixel equals the background
                img[:, :] = background
        proc.stdin.write(img.tobytes())
    proc.stdin.close()
    if proc.wait() != 0:
        raise RuntimeError("ffmpeg render failed: %s" % path)


def self_test(ffmpeg):
    """Renders synthetic videos from the expected sequences and checks the verdicts (0, 1 and 2 as expected)."""
    with open(EXPECTED_PATH, encoding="utf-8") as fh:
        expected = json.load(fh)
    os.makedirs(WORK_DIR, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="selftest_", dir=WORK_DIR)
    try:
        base = os.path.join(tmp, "base")
        os.makedirs(base)
        for name, case in expected["cases"].items():
            for v in case["files"]:
                render(case["per_frame"][v], os.path.join(base, "%s_%s.mp4" % (name, v)), ffmpeg)
        scenarios = []

        # 1: every video follows its own prediction -> 0
        scenarios.append(("all files as predicted", base, EXIT_PASS))

        # 2: N1_60g shows the local (60l) frames instead -> 1 (the 60g rule is not met)
        d = os.path.join(tmp, "local_as_global")
        shutil.copytree(base, d)
        render(expected["cases"]["N1"]["per_frame"]["60l"], os.path.join(d, "N1_60g.mp4"), ffmpeg)
        scenarios.append(("N1_60g carries the local frames", d, EXIT_FAIL))

        # 3: N1_66 shifted by one frame -> 1 (the 66 check fails)
        d = os.path.join(tmp, "shift66")
        shutil.copytree(base, d)
        seq = [None] + expected["cases"]["N1"]["per_frame"]["66"][:-1]
        render(seq, os.path.join(d, "N1_66.mp4"), ffmpeg)
        scenarios.append(("N1_66 shifted one frame", d, EXIT_FAIL))

        # 4: a video missing -> 2
        d = os.path.join(tmp, "missing")
        shutil.copytree(base, d)
        os.remove(os.path.join(d, "N2_60l.mp4"))
        scenarios.append(("N2_60l missing", d, EXIT_INPUT))

        # 5: the first-version drawing (background over the centre square) -> 1 at the 66 check, with the decoded
        # index a multiple of 7 (the cell digit read twice), as the host output showed
        d = os.path.join(tmp, "old_layout")
        shutil.copytree(base, d)
        render(expected["cases"]["N1"]["per_frame"]["66"], os.path.join(d, "N1_66.mp4"), ffmpeg, inner_on_top=False)
        scenarios.append(("N1_66 drawn with the old layout", d, EXIT_FAIL))

        results = []
        for title, d, want in scenarios:
            print("=" * 70)
            print("self-test: %s" % title)
            code = run(d, EXPECTED_PATH, ffmpeg)
            results.append((title, code, want))
        print("=" * 70)
        all_good = True
        for title, code, want in results:
            good = code == want
            all_good = all_good and good
            print("self-test %-32s exit %d (expected %d) %s" % (title, code, want, "OK" if good else "MISMATCH"))
        print("SELF-TEST: %s" % ("PASS" if all_good else "FAIL"))
        return EXIT_PASS if all_good else EXIT_FAIL
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Analyse probe 14 (nested sub-scene alignment).")
    parser.add_argument("--dir", default=OUT_DIR, help="folder with the exported .mp4 files (default: out/)")
    parser.add_argument("--expected", default=EXPECTED_PATH, help="expected.json written by gen_probe14.py")
    parser.add_argument("--ffmpeg", default=DEFAULT_FFMPEG, help="ffmpeg executable")
    parser.add_argument("--self-test", action="store_true", help="run synthetic checks instead (renders into work/)")
    args = parser.parse_args(argv)
    if args.self_test:
        if not os.path.isfile(args.ffmpeg):
            print("INPUT ERROR: ffmpeg not found: %s" % args.ffmpeg)
            return EXIT_INPUT
        return self_test(args.ffmpeg)
    return run(args.dir, args.expected, args.ffmpeg)


if __name__ == "__main__":
    sys.exit(main())
