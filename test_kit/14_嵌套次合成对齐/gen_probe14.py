#!/usr/bin/env python3
"""Probe 14 (nested sub-scene alignment): write the .aup2 files and expected.json next to this script.

Usage:
    python -I test_kit/14_嵌套次合成对齐/gen_probe14.py

Writes N1_66.aup2, N1_60g.aup2, N1_60l.aup2, the same for N2 and N3, N4_66.aup2, expected.json and
jobs_probe14.txt (batch_export.py list for the 10 files, outputs under out/). Deterministic.

Every 60 fps frame= value is computed here with fractions.Fraction. tools/aup2_fps_upconvert.py is NOT imported;
its output is compared with the 60l files afterwards (see 测试清单.zh-CN.md).

Variants:
    66   the source, 66 fps, as written here
    60g  "global" rule: every scene is shifted on the timeline of its scene object, through every level
    60l  "local" rule (today's converter): only a scene whose scene object is in the exported scene is shifted;
         any other scene is rescaled plainly from its own origin

Rule (k = 60/66 = 10/11, round_half_up(x) = floor(x + 1/2)), with the exported scene D = Dn = 0:
    D_X  = D_M + S - P          (S: first frame of the scene object in scene M, P: its position in frames of X)
    Dn_X = round_half_up((S + D_M) k) - P k
    start s -> round_half_up((s + D_X) k) - Dn_X
    end   e -> round_half_up((e + 1 + D_X) k) - 1 - Dn_X
"""

import json
import math
import os
from fractions import Fraction

SRC_RATE = 66
TGT_RATE = 60
K = Fraction(TGT_RATE, SRC_RATE)  # 10/11
W, H = 1280, 720
CELL_LEN = 6
N_CELLS = 36  # 6 x 6 pairs of colours: background digit = i % 6, inner digit = i // 6
BG_SIZE = 1400  # full-frame square (covers 1280x720 around the centre)
INNER_SIZE = 300
MAIN = 0
SCENE_NAMES = {0: "主合成", 1: "次合成1", 2: "次合成2", 3: "次合成3"}
DIGIT_HEX = ["ff0000", "00ff00", "0000ff", "ffff00", "00ffff", "ff00ff"]
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "out")

# Text blocks copied from test_kit/10_次合成对齐/P1_对齐.aup2 (scene object video block) and
# test_kit/13_按帧滤镜/F_底稿_30.aup2 (図形 + 標準描画 with static trackbars).
VIDEO_BLOCK = [
    "effect.name=映像再生", "X=0.00", "Y=0.00", "Z=0.00", "Group=1",
    "中心X=0.00", "中心Y=0.00", "中心Z=0.00", "Group3=1",
    "X軸回転=0.00", "Y軸回転=0.00", "Z軸回転=0.00", "Group2=1",
    "拡大率=100.000", "縦横比=0.000", "透明度=0.00", "合成モード=通常",
    "音量=100.00", "左右=0.00",
]
PLACE_BLOCK = [
    "effect.name=標準描画", "X=0.00,0.00,直線移動,0", "Y=0.00,0.00,直線移動,0", "Z=0.00,0.00,直線移動,0",
    "Group=1", "中心X=0.00", "中心Y=0.00", "中心Z=0.00", "Group3=1",
    "X軸回転=0.00", "Y軸回転=0.00", "Z軸回転=0.00", "Group2=1",
    "拡大率=100.000", "縦横比=0.000", "透明度=0.00", "合成モード=通常",
]


def player(scene, layer, frame, played, pos):
    """A scene object: in scene `scene` at layer `layer`, frame=(first, last) in scene `scene`'s frames,
    plays scene `played` from its frame `pos` (a Fraction of seconds)."""
    return {"scene": scene, "layer": layer, "frame": frame, "played": played, "pos": pos}


CASES = [
    {
        "name": "N1",
        "title": "two levels: main -> scene 1 (main frame 96) -> scene 2 (scene-1 frame 7, position 0)",
        "players": [player(MAIN, 0, (96, 318), 1, Fraction(0)), player(1, 0, (7, 222), 2, Fraction(0))],
        "cell_scenes": [2],
    },
    {
        "name": "N2",
        "title": "three levels: main -> scene 1 (main frame 96) -> scene 2 (scene-1 frame 7) -> scene 3 (scene-2 frame 13)",
        "players": [player(MAIN, 0, (96, 331), 1, Fraction(0)), player(1, 0, (7, 235), 2, Fraction(0)),
                    player(2, 0, (13, 228), 3, Fraction(0))],
        "cell_scenes": [3],
    },
    {
        "name": "N3",
        "title": "two levels, position 0.500 s (33 frames) on the scene object that plays scene 2; the main player ends with scene 1 (scene-1 frame 189, main frame 285)",
        "players": [player(MAIN, 0, (96, 285), 1, Fraction(0)), player(1, 0, (7, 189), 2, Fraction(1, 2))],
        "cell_scenes": [2],
    },
    {
        "name": "N4",
        "title": "scene 1 played by two main objects (not alignable); scene 2 played by one object in scene 1",
        "players": [player(MAIN, 0, (96, 318), 1, Fraction(0)), player(MAIN, 1, (330, 552), 1, Fraction(0)),
                    player(1, 0, (7, 222), 2, Fraction(0))],
        "cell_scenes": [2],
        "only_66": True,
    },
]


def rhu(x):
    """round_half_up: nearest integer, halves up (exact for Fraction)."""
    return math.floor(Fraction(x) + Fraction(1, 2))


def src_pos(p):
    """Position of a scene object in source frames (must be a whole frame at 66 fps)."""
    f = p["pos"] * SRC_RATE
    assert f.denominator == 1 and f >= 0, p
    return int(f)


def fmt3(x):
    """Non-negative Fraction seconds as a 3-decimal text (round half up), as the trackbar text."""
    units = rhu(Fraction(x) * 1000)
    return "%d.%03d" % (units // 1000, units % 1000)


def map_span(k, span, d, dn):
    """Map an inclusive (start, end) span of a scene's frames to the target frames of that scene."""
    s, e = span
    a = rhu((s + d) * k) - dn
    b = rhu((e + 1 + d) * k) - 1 - dn
    assert b >= a, (span, d, dn)
    return int(a), int(b)


def scene_shifts(case, variant):
    """(D, Dn) per scene id for a variant; the exported scene is always (0, 0)."""
    scenes = {MAIN} | {p["played"] for p in case["players"]}
    if variant == "66":
        return {s: (Fraction(0), Fraction(0)) for s in scenes}
    k = K
    shift = {MAIN: (Fraction(0), Fraction(0))}
    pending = list(case["players"])
    played = [p["played"] for p in case["players"]]
    assert len(played) == len(set(played)), "60g/60l need one scene object per played scene"
    while pending:
        progressed = False
        for p in list(pending):
            parent = p["scene"]
            if parent not in shift:
                continue
            s_frame = p["frame"][0]
            pos_frames = src_pos(p)
            pt = Fraction(pos_frames) * k
            assert pt.denominator == 1, "P x k must be whole: %r" % (p,)
            d_parent, _ = shift[parent]
            if variant == "60g" or parent == MAIN:
                d = d_parent + s_frame - pos_frames
                dn = rhu((s_frame + d_parent) * k) - pt
            else:
                # local: a scene whose scene object is not in the exported scene is rescaled plainly
                d, dn = Fraction(0), Fraction(0)
            shift[p["played"]] = (d, dn)
            pending.remove(p)
            progressed = True
        assert progressed, "scene object chain has a loop or an unknown parent"
    return shift


def write_objects(case, variant):
    """Players and cells of a variant with their written frames: (players, cells, shifts)."""
    shifts = scene_shifts(case, variant)
    k = Fraction(1) if variant == "66" else K
    players = []
    for p in case["players"]:
        d, dn = shifts[p["scene"]]
        players.append(dict(p, w=map_span(k, p["frame"], d, dn)))
    cells = []
    for c in case["cell_scenes"]:
        d, dn = shifts[c]
        for i in range(N_CELLS):
            span = (i * CELL_LEN, i * CELL_LEN + CELL_LEN - 1)
            cells.append({"scene": c, "i": i, "src": span, "w": map_span(k, span, d, dn)})
    return players, cells, shifts


def predict(case, variant):
    """Host model: the frame sequence (cell index or None) of the exported scene, one entry per output frame.

    A scene object at scene-coordinate t plays its scene from t - start + P (target frames: P x k).
    Returns (number of output frames, per-frame list)."""
    k = Fraction(1) if variant == "66" else K
    players, cells, _ = write_objects(case, variant)

    def active(sid, t):
        found = set()
        for p in players:
            if p["scene"] == sid and p["w"][0] <= t <= p["w"][1]:
                pt = Fraction(src_pos(p)) * k
                assert pt.denominator == 1
                found |= active(p["played"], t - p["w"][0] + int(pt))
        for c in cells:
            if c["scene"] == sid and c["w"][0] <= t <= c["w"][1]:
                found.add(c["i"])
        return found

    n_out = max(p["w"][1] for p in players if p["scene"] == MAIN) + 1
    seq = []
    for t in range(n_out):
        found = active(MAIN, t)
        assert len(found) <= 1, (case["name"], variant, t, found)
        seq.append(next(iter(found)) if found else None)
    return n_out, seq


def first_frame(seq, cell):
    """Cut frame in a sequence: first frame showing `cell`; for the end cut (cell None), the frame after the last
    frame that shows a cell. analyze_probe14.py uses the same definition on the decoded frames."""
    if cell is None:
        return max(n for n, c in enumerate(seq) if c is not None) + 1
    return seq.index(cell)


def cut_list(case, shifts_src):
    """Cuts of a case with a single chain: one per cell start that is visible, plus the end of the playback."""
    cell_scene = case["cell_scenes"][0]
    last = [p for p in case["players"] if p["played"] == cell_scene][0]
    pos = src_pos(last)
    length = last["frame"][1] - last["frame"][0] + 1
    d_src = shifts_src[cell_scene][0]
    cuts = []
    for i in range(N_CELLS):
        cs, ce = i * CELL_LEN, i * CELL_LEN + CELL_LEN - 1
        if ce < pos or cs > pos + length - 1:
            continue
        vs = max(cs, pos)  # a cell that starts before the playback position is visible from the position
        cuts.append({"label": "cell %d start" % i + (" (playback start)" if vs > cs else ""),
                     "cell": i, "scene_frame": vs, "src_frame": int(vs + d_src)})
    cuts.append({"label": "end of playback", "cell": None, "scene_frame": pos + length,
                 "src_frame": int(pos + length + d_src)})
    return cuts


def local_closed_form(name, scene_frame):
    """Independent closed form of the local (60l) frame of a cut, from the rules above (N1..N3 only)."""
    s103 = rhu(Fraction(103) * K)
    if name == "N1":
        return s103 + rhu(Fraction(scene_frame) * K)
    if name == "N2":
        return s103 + rhu(Fraction(13) * K) + rhu(Fraction(scene_frame) * K)
    if name == "N3":
        return s103 + rhu(Fraction(scene_frame) * K) - int(Fraction(33) * K)
    raise KeyError(name)


def global_closed_form(src_frame):
    return rhu(Fraction(src_frame) * K)


def err_text(err):
    return str(err)


def render_aup2(case, variant, file_name):
    """Text of one .aup2 for a variant (CRLF, UTF-8 without BOM, as probe 10)."""
    rate = SRC_RATE if variant == "66" else TGT_RATE
    players, cells, _ = write_objects(case, variant)
    scenes = sorted({MAIN} | {p["played"] for p in case["players"]} | {p["scene"] for p in case["players"]})
    lines = ["[project]", "version=2011200", "file=" + os.path.join(HERE, file_name),
             "display.scene=0", "preview.scene=0", "display.scene.list.label="]
    n = -1  # object numbers start at [0]: every host-written project in test_kit does (n += 1 before each use)

    def scene_header(sid):
        return [
            "[scene.%d]" % sid, "scene=%d" % sid, "name=%s" % SCENE_NAMES[sid], "label=",
            "video.width=%d" % W, "video.height=%d" % H, "video.rate=%d" % rate, "video.scale=1",
            "audio.rate=44100", "cursor.frame=0", "cursor.layer=2", "preview.frame=0", "display.frame=0",
            "display.layer=0", "display.zoom=20000", "display.order=%d" % sid, "display.camera=",
            "display.grid.x=16,-16", "display.grid.y=16,-16", "display.grid.width=200",
            "display.grid.height=200", "display.grid.step=200.000000", "display.grid.range=10000.000000",
            "display.tempo.bpm=660.000000", "display.tempo.beat=4", "display.tempo.start=0.000000",
            "display.tempo.offset=0.000000",
        ]

    for sid in scenes:
        lines += scene_header(sid)
        for p, w in [(p, p["w"]) for p in players if p["scene"] == sid]:
            n += 1
            a, b = w
            src_len = p["frame"][1] - p["frame"][0] + 1
            lines += ["[%d]" % n, "layer=%d" % p["layer"]]
            if sid != MAIN:
                lines.append("scene=%d" % sid)
            lines += ["frame=%d,%d" % (a, b), "[%d.0]" % n, "effect.name=シーン",
                      "再生位置=%s,%s,再生範囲,0" % (fmt3(p["pos"]), fmt3(p["pos"] + Fraction(src_len, SRC_RATE))),
                      "再生速度=100.00", "シーン=%d" % p["played"], "ループ再生=0",
                      "[%d.1]" % n] + VIDEO_BLOCK
        if sid in case["cell_scenes"]:
            for c in [c for c in cells if c["scene"] == sid]:
                # the larger layer number is drawn on top (probe 13 E1b: the filter at layer=2 acts on the circle at
                # layer=1). The centre square must therefore be the larger layer: with the background at the larger
                # layer it covers the centre square (test_kit/14 outputs of 2026-10-20: centre pixel == background).
                for layer, size, digit in ((1, INNER_SIZE, c["i"] // 6), (0, BG_SIZE, c["i"] % 6)):
                    n += 1
                    a, b = c["w"]
                    lines += ["[%d]" % n, "layer=%d" % layer, "scene=%d" % sid, "frame=%d,%d" % (a, b),
                              "[%d.0]" % n, "effect.name=図形", "図形の種類=四角形", "サイズ=%d" % size,
                              "縦横比=0.00", "ライン幅=4000", "色=%s" % DIGIT_HEX[digit], "角を丸くする=0",
                              "[%d.1]" % n] + PLACE_BLOCK
    return "\r\n".join(lines) + "\r\n"


def case_output(case):
    """Per-case expectations: frames and per-frame sequences per variant, and the cuts (60 variants)."""
    out = {"title": case["title"], "files": {}, "frames": {}, "per_frame": {}}
    variants = ["66"] if case.get("only_66") else ["66", "60g", "60l"]
    for v in variants:
        out["files"][v] = "%s_%s.aup2" % (case["name"], v)
        n_out, seq = predict(case, v)
        out["frames"][v] = n_out
        out["per_frame"][v] = seq
    if case.get("only_66"):
        out["expected_warning"] = ("WARNING: scene 2 cannot be aligned to the exported scene (its scene object is in "
                                   "scene 1, which is not aligned to the exported scene); object boundaries inside it "
                                   "may be one frame off at this rate")
        return out
    shifts_src = scene_shifts(case, "60g")
    cuts = []
    for c in cut_list(case, shifts_src):
        m = c["src_frame"]
        entry = {"label": c["label"], "cell": c["cell"], "src_frame": m,
                 "src_time_s": "%d/%d" % (m, SRC_RATE), "src_time_s_float": round(m / SRC_RATE, 6),
                 "target_exact": str(Fraction(m) * K), "target_exact_float": round(float(Fraction(m) * K), 6),
                 "variants": {}}
        for v in variants:
            n = first_frame(out["per_frame"][v], c["cell"])
            k = Fraction(1) if v == "66" else K
            err = n - Fraction(m) * k
            entry["variants"][v] = {"frame": n, "error": err_text(err), "error_float": round(float(err), 6)}
        # independent closed forms
        assert entry["variants"]["60g"]["frame"] == global_closed_form(m), (case["name"], c)
        assert entry["variants"]["60l"]["frame"] == local_closed_form(case["name"], c["scene_frame"]), (case["name"], c)
        cuts.append(entry)
    out["cuts"] = cuts
    return out


def discrimination(name, cuts):
    """Check the probe's claim: 60g keeps every cut within 1/2 frame, 60l puts several cuts further away."""
    half = Fraction(1, 2)
    g = [Fraction(c["variants"]["60g"]["error"]) for c in cuts]
    l_ = [Fraction(c["variants"]["60l"]["error"]) for c in cuts]
    assert all(abs(x) <= half for x in g), (name, "60g cut outside 1/2 frame")
    outliers = sum(1 for x in l_ if abs(x) > half)
    assert outliers >= 3, (name, "60l gives fewer than 3 cuts beyond 1/2 frame", outliers)
    return outliers


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    expected = {
        "probe": "14_嵌套次合成对齐",
        "source_rate": SRC_RATE,
        "target_rate": TGT_RATE,
        "k": str(K),
        "cell_frames": CELL_LEN,
        "cells": N_CELLS,
        "size": [W, H],
        "tolerance": "0 (every error is a multiple of 1/11, see analyze_probe14.py)",
        "local_outlier_min": 3,
        "cases": {},
    }
    jobs = ["# probe 14: test_kit/14_嵌套次合成对齐/<name>.aup2 -> test_kit/14_嵌套次合成对齐/out/<name>.mp4 (TAB separated)"]
    print("%-4s %-16s %s" % ("case", "file", "frames"))
    for case in CASES:
        name = case["name"]
        result = case_output(case)
        expected["cases"][name] = result
        for v, file_name in result["files"].items():
            text = render_aup2(case, v, file_name)
            with open(os.path.join(HERE, file_name), "wb") as fh:
                fh.write(text.encode("utf-8"))
            jobs.append("%s\t%s" % (os.path.join(HERE, file_name), os.path.join(OUT_DIR, os.path.splitext(file_name)[0] + ".mp4")))
            print("%-4s %-16s frames=%d" % (name, file_name, result["frames"][v]))
    # discrimination table (N1..N3)
    for name in ("N1", "N2", "N3"):
        cuts = expected["cases"][name]["cuts"]
        outliers = discrimination(name, cuts)
        print("")
        print("%s: 60l cuts with |error| > 1/2: %d of %d" % (name, outliers, len(cuts)))
        print("%-22s %-10s %-22s %-22s" % ("cut", "src(66)", "60g frame / err", "60l frame / err"))
        for c in cuts:
            g = c["variants"]["60g"]
            l_ = c["variants"]["60l"]
            print("%-22s %-10s %-22s %-22s" % (
                c["label"][:22], "%d" % c["src_frame"],
                "%d / %+.4f" % (g["frame"], g["error_float"]),
                "%d / %+.4f" % (l_["frame"], l_["error_float"])))
    with open(os.path.join(HERE, "expected.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(expected, fh, ensure_ascii=False, indent=1)
        fh.write("\n")
    with open(os.path.join(HERE, "jobs_probe14.txt"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(jobs) + "\n")
    print("")
    print("wrote expected.json and jobs_probe14.txt")


if __name__ == "__main__":
    main()
