"""Long-duration drift test for tools/aup2_fps_upconvert.py --target-fps (no AviUtl host needed).

Builds synthetic 30 and 60 minute projects (1 s picture objects, 1 s audio objects on the same start frames,
and prime-length objects), retimes each to several target rates with the tool, and checks the output
independently of the tool's own report:
- every object start and end boundary is within 1/(2*Rt) of its source time, and the error does not grow
  over time (linear fit slope and per-decile maxima);
- audio objects start on exactly the same output frame as their picture objects;
- the total duration error is below one target frame and at most 0.01 %;
- objects that touched in the input still touch, and no object disappears.
The straight-line speed error ((e'-s')/Rt versus (e-s)/Rp, half-open ends) is reported only, not failed.

Run with: python -I tests/aup2_upconvert/test_long_duration.py   (or run_long.ps1 next to this file)
Temporary files go to $AUP2_TEST_TMP when set, otherwise a fresh tempfile directory.
Exit code is non-zero if any check fails.
"""

import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import traceback
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
TOOL = os.path.join(REPO, "tools", "aup2_fps_upconvert.py")

SOURCE_RATES = [
    ("30", Fraction(30)),
    ("60", Fraction(60)),
    ("72", Fraction(72)),
    ("66", Fraction(66)),
    ("60000/1001", Fraction(60000, 1001)),
    ("24000/1001", Fraction(24000, 1001)),
]
TARGET_RATES = ["66", "60", "59", "50", "24", "120", "60000/1001", "30000/1001", "25"]
DURATION_MINUTES = (30, 60)
PRIME_FRAMES = (7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47)
TIME_LIMIT_REL = Fraction(1, 10000)   # 0.01 % total duration error
SPEED_LIMIT_REL = Fraction(1, 10000)  # 0.01 %, reported only
BAND_RATIO_LIMIT = 2.0                # first/middle/last decile maxima must agree within this factor

LAYER_PICTURE, LAYER_AUDIO, LAYER_PRIME = 0, 1, 2

RE_OBJECT_HDR = re.compile(r"^\[(\d+)\]$")
RE_SCENE_HDR = re.compile(r"^\[scene\.(\d+)\]$")

FAILURES = []


def read_bytes(path):
    with open(path, "rb") as fh:
        return fh.read()


def write_bytes(path, data):
    with open(path, "wb") as fh:
        fh.write(data)


def run_tool(*args):
    cmd = [sys.executable, "-I", TOOL] + list(args)
    proc = subprocess.run(cmd, capture_output=True)
    return proc.returncode, proc.stdout.decode("utf-8", "replace"), proc.stderr.decode("utf-8", "replace")


def frames_per_second(rate):
    """Nearest integer frame count for one second at this rate (60000/1001 -> 60, 24000/1001 -> 24)."""
    return math.floor(rate + Fraction(1, 2))


def build_objects(rate, seconds):
    """Return (objects, total_frames). objects is a list of (layer, start_frame, end_frame_inclusive).

    Object number = index in the list. Layer 0: 1 s picture objects touching end to end.
    Layer 1: audio objects with the same frames as the picture objects (same start frame).
    Layer 2: touching prime-length objects, cycling through PRIME_FRAMES, filling the same timeline.
    """
    n = frames_per_second(rate)
    total = seconds * n
    objs = []
    for i in range(seconds):
        s = i * n
        objs.append((LAYER_PICTURE, s, s + n - 1))
    for i in range(seconds):
        s = i * n
        objs.append((LAYER_AUDIO, s, s + n - 1))
    pos = 0
    k = 0
    while True:
        length = PRIME_FRAMES[k % len(PRIME_FRAMES)]
        if pos + length > total:
            break
        objs.append((LAYER_PRIME, pos, pos + length - 1))
        pos += length
        k += 1
    return objs, total


def project_text(rate, objs, src_path):
    """Minimal .aup2 in the layout used by experiments/66_to_60_validation fixtures (CRLF)."""
    lines = [
        "[project]",
        "version=2010400",
        "file=" + os.path.abspath(src_path),
        "display.scene=0",
        "preview.scene=0",
        "[scene.0]",
        "scene=0",
        "name=long duration %d/%d" % (rate.numerator, rate.denominator),
        "video.width=640",
        "video.height=360",
        "video.rate=%d" % rate.numerator,
        "video.scale=%d" % rate.denominator,
        "audio.rate=44100",
        "cursor.frame=0",
        "cursor.layer=0",
        "preview.frame=0",
        "display.frame=0",
        "display.layer=0",
    ]
    for no, (layer, s, e) in enumerate(objs):
        lines += [
            "[%d]" % no,
            "layer=%d" % layer,
            "frame=%d,%d" % (s, e),
            "[%d.0]" % no,
            "effect.name=図形",
            "図形の種類=円",
            "サイズ=48",
            "[%d.1]" % no,
            "effect.name=標準描画",
            "X=-200.00,200.00,直線移動,0",
            "Y=0.00,0.00,直線移動,0",
            "Group=1",
            "透明度=0.00",
        ]
    return ("\r\n".join(lines) + "\r\n").encode("utf-8")


def parse_project(data):
    """Return (rate, objects) from a .aup2 file. objects is a list indexed by object number."""
    text = data.decode("utf-8")
    scene = None
    section = None
    rate_num = rate_den = None
    layers = {}
    frames = {}
    for line in text.split("\n"):
        line = line.rstrip("\r")
        if line.startswith("[") and line.endswith("]"):
            m_obj = RE_OBJECT_HDR.match(line)
            m_scene = RE_SCENE_HDR.match(line)
            if m_obj:
                section = ("object", int(m_obj.group(1)))
            else:
                section = ("other", line)
            if m_scene:
                scene = int(m_scene.group(1))
            continue
        if "=" not in line or section is None:
            continue
        key, _, value = line.partition("=")
        if section[0] == "object":
            no = section[1]
            if key == "layer":
                if no in layers:
                    raise AssertionError("duplicate layer= in object [%d]" % no)
                layers[no] = int(value)
            elif key == "frame":
                if no in frames:
                    raise AssertionError("duplicate frame= in object [%d]" % no)
                parts = value.split(",")
                if len(parts) != 2:
                    raise AssertionError("object [%d] frame= has %d values, expected 2" % (no, len(parts)))
                frames[no] = (int(parts[0]), int(parts[1]))
        elif scene == 0:
            if key == "video.rate":
                rate_num = int(value)
            elif key == "video.scale":
                rate_den = int(value)
    if rate_num is None or rate_den is None:
        raise AssertionError("[scene.0] has no video.rate/video.scale")
    if set(layers) != set(frames):
        raise AssertionError("objects without both layer= and frame=: %r" % sorted(set(layers) ^ set(frames)))
    objs = []
    for no in range(len(layers)):
        if no not in layers:
            raise AssertionError("object numbers are not 0..N-1 (missing %d)" % no)
        s, e = frames[no]
        objs.append((layers[no], s, e))
    if len(objs) != len(layers):
        raise AssertionError("object numbers are not 0..N-1")
    return Fraction(rate_num, rate_den), objs


def analyze(src_objs, src_rate, out_objs, out_rate, report):
    """Compare retimed objects with the source. Returns (metrics, failures).

    Times are exact Fractions in seconds. Boundaries use half-open ends: end time = (last frame + 1) / rate.
    `report` is the tool's .convert.json dict, or None to skip the cross-check against it.
    """
    fails = []
    m = {}
    if len(out_objs) != len(src_objs):
        return m, ["object count changed: %d in, %d out" % (len(src_objs), len(out_objs))]
    bound = Fraction(1, 2) / out_rate  # half of one target frame

    # 1. Boundary errors and drift.
    rows = []  # (t_src_seconds, err_seconds, obj_no, which)
    max_start = max_end = Fraction(0)
    worst = None
    for no, (layer, s, e) in enumerate(src_objs):
        ol, os_, oe = out_objs[no]
        if ol != layer:
            fails.append("object %d moved from layer %d to %d" % (no, layer, ol))
        for which, sf, of in (("start", s, os_), ("end", e + 1, oe + 1)):
            t = Fraction(sf) / src_rate
            err = Fraction(of) / out_rate - t
            rows.append((t, err, no, which))
            if which == "start":
                max_start = max(max_start, abs(err))
            else:
                max_end = max(max_end, abs(err))
            if abs(err) > bound and worst is None:
                worst = (no, which, err)
    max_err = max(max_start, max_end)
    m["max_boundary_s"] = max_err
    m["max_boundary_frames"] = max_err * out_rate
    m["bound_frames"] = Fraction(1, 2)
    if max_err > bound:
        no, which, err = worst
        fails.append("boundary error %s s (%.3f target frames) > 1/(2Rt) at object %d %s"
                     % (float(err), float(err * out_rate), no, which))
    if report is not None:
        tool = report["timing"]
        if Fraction(tool["max_start_error_s"]) != max_start:
            fails.append("tool max_start_error_s %s != independent %s" % (tool["max_start_error_s"], max_start))
        if Fraction(tool["max_end_error_s"]) != max_end:
            fails.append("tool max_end_error_s %s != independent %s" % (tool["max_end_error_s"], max_end))

    total_src = max(e + 1 for _, _, e in src_objs)
    span = float(Fraction(total_src) / src_rate)
    xs = [float(t) for t, _, _, _ in rows]
    ys = [float(err) for _, err, _, _ in rows]
    mx = sum(xs) / len(xs)
    my = sum(ys) / len(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    m["slope_ppm"] = slope * 1e6
    m["drift_span_frames"] = slope * span * float(out_rate)
    if abs(slope * span) > float(bound):
        fails.append("fitted drift %.6f s over the span exceeds half a target frame" % (slope * span))

    bands = {"first10": [], "middle10": [], "last10": []}
    for t, err, _, _ in rows:
        f = float(t) / span
        if f < 0.1:
            bands["first10"].append(abs(float(err)))
        elif 0.45 <= f < 0.55:
            bands["middle10"].append(abs(float(err)))
        elif f >= 0.9:
            bands["last10"].append(abs(float(err)))
    band_max = {k: max(v) for k, v in bands.items()}
    m["band_max_frames"] = {k: v * float(out_rate) for k, v in band_max.items()}
    hi, lo = max(band_max.values()), min(band_max.values())
    if hi > 0 and (lo <= 0 or hi / lo > BAND_RATIO_LIMIT):
        fails.append("per-decile maxima differ too much: %r" % band_max)

    # 2. Total duration.
    total_out = max(oe + 1 for _, _, oe in out_objs)
    t_src = Fraction(total_src) / src_rate
    t_out = Fraction(total_out) / out_rate
    diff = abs(t_out - t_src)
    m["total_err_frames"] = diff * out_rate
    m["total_rel_pct"] = float(diff / t_src) * 100
    if diff >= Fraction(1) / out_rate:
        fails.append("total duration error %s s is not below one target frame" % float(diff))
    if diff / t_src > TIME_LIMIT_REL:
        fails.append("total duration relative error %.6f %% > 0.01 %%" % (float(diff / t_src) * 100))

    # 3. Touching neighbours on each layer, and no zero-length object.
    touch_pairs = 0
    for layer in (LAYER_PICTURE, LAYER_AUDIO, LAYER_PRIME):
        nos = sorted((no for no, o in enumerate(src_objs) if o[0] == layer), key=lambda n: src_objs[n][1])
        for a, b in zip(nos, nos[1:]):
            if src_objs[a][2] + 1 != src_objs[b][1]:
                continue
            touch_pairs += 1
            if out_objs[a][2] + 1 != out_objs[b][1]:
                fails.append("objects %d and %d touched in the input but not in the output (layer %d)"
                             % (a, b, layer))
    m["touch_pairs"] = touch_pairs
    for no, (_, os_, oe) in enumerate(out_objs):
        if oe < os_:
            fails.append("object %d has no frames in the output" % no)
            break

    # 4. Audio/picture sync: same start frame and time for each picture object and its audio partner.
    audio_by_start = {s: no for no, (layer, s, _) in enumerate(src_objs) if layer == LAYER_AUDIO}
    av_max = Fraction(0)
    av_pairs = 0
    for no, (layer, s, e) in enumerate(src_objs):
        if layer != LAYER_PICTURE:
            continue
        audio = audio_by_start.get(s)
        if audio is None:
            fails.append("picture object %d has no audio partner" % no)
            continue
        av_pairs += 1
        _, ps, pe = out_objs[no]
        _, as_, ae = out_objs[audio]
        if ps != as_:
            fails.append("audio %d starts at output frame %d, picture %d at %d" % (audio, as_, no, ps))
        av_max = max(av_max, abs(Fraction(as_ - ps) / out_rate), abs(Fraction(ae - pe) / out_rate))
    m["av_pairs"] = av_pairs
    m["av_max_s"] = av_max

    # 5. Straight-line speed error of the 1 s picture objects (reported only).
    speed_max = Fraction(0)
    for no, (layer, s, e) in enumerate(src_objs):
        if layer != LAYER_PICTURE:
            continue
        _, os_, oe = out_objs[no]
        len_src = Fraction(e - s + 1) / src_rate
        len_out = Fraction(oe - os_ + 1) / out_rate
        speed_max = max(speed_max, abs(len_out - len_src) / len_src)
    m["speed_max_rel"] = speed_max
    return m, fails


def exact_mapping_k2(objs):
    return [(layer, 2 * s, 2 * (e + 1) - 1) for layer, s, e in objs]


def self_check():
    """The checker must accept an exact x2 mapping and must reject a one-frame boundary move."""
    objs, _ = build_objects(Fraction(30), 2)
    good = exact_mapping_k2(objs)
    _, fails = analyze(objs, Fraction(30), good, Fraction(60), None)
    assert not fails, "exact mapping rejected: %r" % fails
    bad = list(good)
    layer, s, e = bad[0]
    bad[0] = (layer, s + 1, e)
    _, fails = analyze(objs, Fraction(30), bad, Fraction(60), None)
    assert any("boundary" in f for f in fails), "one-frame move not detected: %r" % fails


def safe_name(name):
    return name.replace("/", "_")


def run_case(work, minutes, src_name, src_rate, target_text, objs, total, src_path):
    """Convert one source file to one target and check it. Returns (metrics, failures, seconds)."""
    t0 = time.perf_counter()
    target = Fraction(target_text)
    out_path = os.path.join(work, "out_%dmin_%s_to_%s.aup2" % (minutes, safe_name(src_name), safe_name(target_text)))
    code, _, err = run_tool("--in", src_path, "--out", out_path, "--target-fps", target_text)
    if code != 0:
        return {}, ["tool exit %d: %s" % (code, err.strip())], time.perf_counter() - t0
    report_path = out_path + ".convert.json"
    with open(report_path, "r", encoding="utf-8") as fh:
        report = json.load(fh)
    out_rate, out_objs = parse_project(read_bytes(out_path))
    if out_rate != target:
        return {}, ["output rate %s != target %s" % (out_rate, target)], time.perf_counter() - t0
    if report["target_rate"] != "%d/%d" % (target.numerator, target.denominator):
        return {}, ["report target_rate %s" % report["target_rate"]], time.perf_counter() - t0
    metrics, fails = analyze(objs, src_rate, out_objs, out_rate, report)
    os.remove(out_path)
    os.remove(report_path)
    return metrics, fails, time.perf_counter() - t0


def fmt_row(minutes, src_name, target_text, m, seconds, fails):
    if not m:
        return "%4d %-11s -> %-11s  FAILED: %s" % (minutes, src_name, target_text, "; ".join(fails))
    return ("%4d %-11s -> %-11s maxBound %.3e s (%.4f fr) slope %+.3f ppm drift %+.5f fr  "
            "bands %.3f/%.3f/%.3f fr  totErr %.5f fr %.6f%%  AVmax %.1e s  speedMax %.4f%%  %6.1f s  %s") % (
        minutes, src_name, target_text,
        float(m["max_boundary_s"]), float(m["max_boundary_frames"]),
        m["slope_ppm"], m["drift_span_frames"],
        m["band_max_frames"]["first10"], m["band_max_frames"]["middle10"], m["band_max_frames"]["last10"],
        float(m["total_err_frames"]), m["total_rel_pct"],
        float(m["av_max_s"]), float(m["speed_max_rel"]) * 100, seconds,
        "OK" if not fails else "FAIL: " + "; ".join(fails))


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    base = os.environ.get("AUP2_TEST_TMP")
    if base:
        os.makedirs(base, exist_ok=True)
    work = tempfile.mkdtemp(prefix="aup2_long_", dir=base)
    print("work dir: %s" % work)
    started = time.perf_counter()
    try:
        if not os.path.isfile(TOOL):
            print("FAIL missing tool %s" % TOOL)
            return 1
        try:
            self_check()
            print("PASS self_check (checker accepts exact mapping, rejects one-frame move)")
        except AssertionError:
            FAILURES.append("self_check")
            print("FAIL self_check")
            traceback.print_exc()

        rows = []
        speed_over = []
        for minutes in DURATION_MINUTES:
            seconds = minutes * 60
            for src_name, src_rate in SOURCE_RATES:
                objs, total = build_objects(src_rate, seconds)
                src_path = os.path.join(work, "src_%dmin_%s.aup2" % (minutes, safe_name(src_name)))
                write_bytes(src_path, project_text(src_rate, objs, src_path))
                for target_text in TARGET_RATES:
                    name = "%dmin %s -> %s" % (minutes, src_name, target_text)
                    try:
                        m, fails, secs = run_case(work, minutes, src_name, src_rate, target_text,
                                                  objs, total, src_path)
                    except Exception as exc:  # report and keep going
                        m, fails, secs = {}, ["exception: %r" % exc], 0.0
                    if m and float(m["speed_max_rel"]) > float(SPEED_LIMIT_REL):
                        speed_over.append(name)
                    line = fmt_row(minutes, src_name, target_text, m, secs, fails)
                    print(line)
                    if fails:
                        FAILURES.append(name)
                    rows.append((name, m, fails))
                os.remove(src_path)
        print("")
        total_secs = time.perf_counter() - started
        print("combinations: %d, failed: %d, total runtime %.1f s" % (len(rows), sum(1 for r in rows if r[2]), total_secs))
        speeds = [float(m["speed_max_rel"]) * 100 for _, m, _ in rows if m]
        if speeds:
            print("speed error (reported only, limit 0.01 %%): max %.4f %%, combos above limit %d of %d"
                  % (max(speeds), len(speed_over), len(rows)))
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if FAILURES:
        print("FAILED: %d" % len(FAILURES))
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
