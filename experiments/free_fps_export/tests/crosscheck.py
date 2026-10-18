"""Cross-check the C++ re-timer (retime_cli.exe) against tools/aup2_fps_upconvert.py.

Inputs: every experiments/66_to_60_validation/*.aup2, experiments/free_fps_matrix/fixture_60fps.aup2 and
fixture_72fps.aup2, experiments/host_matrix/scene_rate_test.aup2, and the read-only real project
D:\\ARZv2.0.1\\ARZv2.0.1\\sources\\試験用動画.aup2 (copied into the work directory first).
Targets: --target-fps 66, 60, 50, 59, 24, 120, 60000/1001, 30000/1001 and --factor 2, 3.
Precise: --precise with --target-fps 66, 24, 60000/1001, 50, 59, 120 on every fixture, and 66 on the real project;
--precise-extra-decimals 2 with 66 on every input, and two refusals for it (when the Python tool has the option).
Skipped (with a message) while tools/aup2_fps_upconvert.py does not accept --precise or the option.
Refusal cases: synthetic inputs written to the work directory, one per refusal, each with its own arguments.
Subscene rate (when the Python tool has --subscene-rate): --subscene-rate auto/follow/keep with and without --precise on
every single-scene input above (targets 66 and --factor 2), and on the multi-scene inputs: tests/aup2_upconvert/fixtures/
*.aup2, the read-only host sample C:\\Users\\gaoha\\Desktop\\test 30 and 30or60.aup2 (copied first) and the synthetic
multi-scene projects of this file (main 30 + sub 15, no video.scale, NTSC rates, display.scene=1, orphan scene=, kept
scene with content that a followed scene would refuse, display.scene refusals), each with nine targets, five precise
targets and --precise-extra-decimals 2, for all three policies.

Time control (時間制御(オブジェクト) and コマ落ち, always on): the read-only ARZ template
D:\\ARZv2.0.1\\ARZv2.0.1\\template\\15_mer1\\.aup2 (copied first) and every probe in test_kit/5_抽帧探针/*.aup2
(read in place) at 60, 66, 120, 132 and 60000/1001 with --precise off and on; synthetic projects of this
file (blocks, コマ落ち scaling, single frames, decimals, two effects on one object, sub scenes, and one project per
refusal) at the nine targets and the five precise targets, and the sub scenes with the three --subscene-rate policies.
Held ends and steps: the ph_* projects (--precise keeps the last value; extra keyframes at the ends) and the tcs_*
projects (reversed blocks, clamped end values, コマ落ち whose K x k or s x k is not an integer: 瞬間移動 steps; constant
and changing trackbars next to steps; the 4096 point limit; a scene at k = 1) run at every target, precise off and on.

Time control mode and payloads (always on): the tcm_* synthetic projects (直線移動(時間制御) on ordinary tracks of any effect,
'|' payloads such as 0|1,0,1,0 on 直線移動, 直線移動(時間制御) and the easing mode, precise skips and re-sampling) and the
tc_curve_* projects (a curve in the 位置 row, コマ落ち K x k or the refusal) at the nine targets and the five precise targets.
User projects (read-only, copied first, skipped when absent): C:\\Users\\gaoha\\Desktop\\10 3.aup2, 神.aup2 and every
D:\\ARZv2.0.1\\**\\output.aup2 at --target-fps 60, 66, 120 and --factor 2, with --precise off and on.

Frame-counting modes (反復移動, ランダム移動, 移動量指定; always on): every .aup2 in test_kit/9_其他移动方式/ (read in
place: the 底稿 M_底稿_30 and M_底稿_60 and B1-B8, not the out\ subfolder) at 60, 66, 120, 132, 60000/1001, 24 and 2x, precise
off and on. Synthetic 30 fps projects: the refusals (a '|' payload on 移動量指定 or none on the other two, flag not 4, three
values or three frame points, parameter 0.5, abc, ランダム 15.4, ランダム 30 at 60000/1001, the error bound with a 30000-frame
object, a time control object overlapping the row) and the accepted conversions (反復移動, ランダム移動 and 移動量指定 at 60,
66, 60000/1001 and 24, precise off and on, with a zero Y/Z step of 移動量指定 that must stay unchanged).

Each case runs both tools one after the other with the same absolute --out path. Exit codes, stderr (the
refusal text) and the .aup2 and .convert.json files must match exactly. The C++ and Python refusal texts are now
identical (no known difference except a コマ落ち beyond 64 bits, which the port refuses and Python converts); any difference
is reported. Easing (イージング（通常）@イージング with a '|' payload, precise skips) is covered by the ease_* synthetic projects.

Usage: python -I tests/crosscheck.py [--exe PATH] [--work DIR] [--keep]
Exit code: 0 when all cases match, 1 on any mismatch, 2 when the setup is missing.
The work directory is emptied at the start and deleted at the end unless --keep is given. The default is
experiments/free_fps_export/build/crosscheck (git-ignored); pass a dedicated directory with --work.
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXPERIMENT = os.path.abspath(os.path.join(HERE, ".."))
REPO = os.path.abspath(os.path.join(EXPERIMENT, "..", ".."))
PY_TOOL = os.path.join(REPO, "tools", "aup2_fps_upconvert.py")
VALIDATION_DIR = os.path.join(REPO, "experiments", "66_to_60_validation")
MATRIX_DIR = os.path.join(REPO, "experiments", "free_fps_matrix")
HOST_MATRIX_DIR = os.path.join(REPO, "experiments", "host_matrix")
REAL_PROJECT = r"D:\ARZv2.0.1\ARZv2.0.1\sources\試験用動画.aup2"
DEFAULT_WORK = os.path.join(EXPERIMENT, "build", "crosscheck")
JSON_SUFFIX = ".convert.json"
MULTI_SCENE_DIR = os.path.join(REPO, "tests", "aup2_upconvert", "fixtures")
HOST_SAMPLE = r"C:\Users\gaoha\Desktop\test 30 and 30or60.aup2"
ARZ_TEMPLATE = r"D:\ARZv2.0.1\ARZv2.0.1\template\15_mer1\.aup2"
PROBE_DIR = os.path.join(REPO, "test_kit", "5_抽帧探针")
FRAME_MODE_DIR = os.path.join(REPO, "test_kit", "9_其他移动方式")

SUBSCENE_POLICIES = ("auto", "follow", "keep")

# Read-only user projects (copied into the work directory first; absent files are skipped).
USER_PROJECTS = [r"C:\Users\gaoha\Desktop\10 3.aup2", r"C:\Users\gaoha\Desktop\神.aup2"]
ARZ_ROOT = r"D:\ARZv2.0.1"
USER_TARGETS = [["--target-fps", "60"], ["--target-fps", "66"], ["--target-fps", "120"], ["--factor", "2"]]

TARGETS = [["--target-fps", rate] for rate in ("66", "60", "50", "59", "24", "120", "60000/1001", "30000/1001")]
TARGETS += [["--factor", "2"], ["--factor", "3"]]

PRECISE_TARGETS = [["--target-fps", rate] for rate in ("66", "24", "60000/1001", "50", "59", "120")]
PRECISE_REAL_TARGET = ["--target-fps", "66"]
TIME_CONTROL_TARGETS = [["--target-fps", rate] for rate in ("60", "66", "120", "132", "60000/1001")]

BASE_BODY = ["[0]", "layer=0", "frame=0,299"]

# Single-scene inputs: policy x precise on a short target list (the policy only adds the sidecar keys there).
POLICY_SINGLE_TARGETS = [["--target-fps", "66"], ["--factor", "2"]]
# Multi-scene inputs: nine targets, five precise targets and extra decimals, for every policy.
MULTI_TARGETS = [["--target-fps", rate] for rate in ("66", "60", "24", "12", "6", "120", "60000/1001")]
MULTI_TARGETS += [["--factor", "2"], ["--factor", "3"]]
MULTI_PRECISE_TARGETS = [["--target-fps", rate] for rate in ("66", "60", "24", "60000/1001", "120")]
MULTI_EXTRA_DECIMALS = ["--target-fps", "66", "--precise", "--precise-extra-decimals", "2"]


def scene_section(no, name, rate, scale, objects, cursor=33, extra=()):
    """One [scene.N] section and its objects. name/scale None: the line is left out."""
    lines = ["[scene.%d]" % no, "scene=%d" % no]
    if name is not None:
        lines.append("name=%s" % name)
    lines += ["label=", "video.width=640", "video.height=360", "video.rate=%d" % rate]
    if scale is not None:
        lines.append("video.scale=%d" % scale)
    lines += ["audio.rate=44100", "cursor.frame=%d" % cursor, "cursor.layer=0", "preview.frame=%d" % (cursor + 7),
              "display.frame=%d" % (cursor + 11), "display.layer=0"]
    return lines + list(extra) + list(objects)


def object_lines(no, frame, scene=None, layer=0, tracks=(), effect="標準描画", extra=()):
    lines = ["[%d]" % no, "layer=%d" % layer]
    if scene is not None:
        lines.append("scene=%d" % scene)
    lines += ["frame=%s" % frame, "[%d.0]" % no, "effect.name=%s" % effect]
    return lines + list(tracks) + list(extra)


SCENE_EFFECT_OBJECT = ["[0]", "layer=0", "frame=0,149", "[0.0]", "effect.name=シーン",
                       "再生位置=0.000,5.000,再生範囲,0", "再生速度=100.00", "シーン=1", "ループ再生=0"]


def multi_text(project_lines, *scenes):
    lines = ["[project]", "version=2011200", "file=C:\\synthetic\\src.aup2"] + list(project_lines)
    for scene in scenes:
        lines += scene
    return "\n".join(lines) + "\n"


def multi_scene_texts():
    """(name, aup2 text): synthetic multi-scene projects. Several are refused for some policies and converted for others."""
    track3 = ["X=0.00,50.00,-25.00,直線移動,0"]
    main_objects = SCENE_EFFECT_OBJECT + object_lines(1, "10,40,100", layer=1, tracks=track3) + \
        object_lines(2, "5,5", layer=2, tracks=["X=3.00,3.00,直線移動,0"])
    sub15_objects = object_lines(3, "0,74", scene=1, tracks=["X=0.00,100.00,直線移動,0", "Y=5.00,9.00,補間移動,0"]) + \
        object_lines(4, "75,89", scene=1, tracks=["X=-1.00,1.00,直線移動,0"])
    texts = {}
    texts["ms_30_15"] = multi_text(["display.scene=0"], scene_section(0, "主合成", 30, 1, main_objects),
                                   scene_section(1, "次合成1", 15, 1, sub15_objects, cursor=17))
    texts["ms_30_60_precise"] = multi_text(
        ["display.scene=0", "display.scene.list.label="], scene_section(0, "主合成", 30, 1, main_objects),
        scene_section(1, "次合成1", 60, 1, object_lines(3, "0,299", scene=1, tracks=["X=0.00,100.00,直線移動,0"]) +
                      object_lines(4, "300,479", scene=1, tracks=["X=-1.00,1.00,直線移動,0"]), cursor=134))
    texts["ms_noscale"] = multi_text(
        ["display.scene=0"], scene_section(0, "m", 30, None, main_objects),
        scene_section(1, "s15", 15, None, sub15_objects),
        scene_section(2, None, 60, None, object_lines(5, "0,59", scene=2, tracks=["X=0.00,10.00,直線移動,0"])))
    ntsc = lambda no, rate, scale: scene_section(no, "r%d" % no, rate, scale, object_lines(10 + no, "0,29", scene=no))
    texts["ms_ntsc_file_order"] = multi_text(
        ["display.scene=0"], scene_section(0, "ntsc", 30000, 1001, object_lines(10, "0,29", scene=0)),
        ntsc(3, 60000, 2002), ntsc(1, 30, 1), ntsc(2, 2997, 100), ntsc(4, 120000, 1001))
    texts["ms_display1"] = multi_text(
        ["display.scene=1", "display.scene.list.label="],
        scene_section(0, "a", 30, 1, object_lines(1, "0,29", scene=0, tracks=["X=0.00,10.00,直線移動,0"])),
        scene_section(1, "b", 60, 1, object_lines(2, "0,59", scene=1, tracks=["X=0.00,10.00,直線移動,0"])),
        scene_section(2, "c", 120, 1, object_lines(3, "0,119", scene=2, tracks=["X=0.00,10.00,直線移動,0"])))
    texts["ms_display_first_wins"] = multi_text(
        ["display.scene=2", "display.scene=0"], scene_section(0, "a", 30, 1, object_lines(1, "0,29", scene=0)),
        scene_section(2, "c", 15, 1, object_lines(3, "0,14", scene=2)))
    texts["ms_orphan_scene_object"] = multi_text(
        ["display.scene=0"], scene_section(0, "a", 30, 1, object_lines(1, "0,29") + object_lines(2, "3,9", scene=7)),
        scene_section(1, "b", 15, 1, object_lines(3, "0,14", scene=1)))
    texts["ms_only_scene_1_display1"] = "\n".join(
        ["[project]", "version=2011200", "file=C:\\synthetic\\src.aup2", "display.scene=1"] +
        scene_section(1, "only", 30, 1, object_lines(1, "0,29")) + object_lines(2, "0,9")) + "\n"
    texts["ms_duplicate_name_and_rate"] = multi_text(
        ["display.scene=0"], scene_section(0, "first", 30, 1, object_lines(1, "0,29")),
        scene_section(1, "a", 7, 1, object_lines(2, "0,29", scene=1), extra=["name=b", "video.rate=60"]))
    # A kept scene (15 FPS next to the 30 FPS main scene) holds what a followed scene would refuse.
    kept_bad = object_lines(3, "0,x", scene=1, tracks=["X=0,1,2,謎移動,0|27", "Y=0,1,直線移動,0"],
                            extra=["Z=obj.frame"]) + \
        object_lines(4, "0,9", scene=1, effect="カウンター") + ["コマ落ち=2"]
    texts["ms_kept_holds_refusals"] = multi_text(
        ["display.scene=0"], scene_section(0, "a", 30, 1, object_lines(1, "0,29")),
        scene_section(1, "b", 15, 1, kept_bad, extra=["コマ落ち=2"]))
    texts["ms_kept_bad_frame_order"] = multi_text(
        ["display.scene=0"], scene_section(0, "a", 30, 1, object_lines(1, "0,29")),
        scene_section(1, "b", 15, 1, object_lines(3, "5,3", scene=1)))
    texts["ms_column_in_main"] = multi_text(
        ["display.scene=0"], scene_section(0, "a", 30, 1, object_lines(1, "0,29", extra=["コマ落ち=2"])),
        scene_section(1, "b", 15, 1, object_lines(3, "0,14", scene=1)))
    texts["ms_column_in_project"] = multi_text(
        ["display.scene=0", "コマ落ち=2"], scene_section(0, "a", 30, 1, object_lines(1, "0,29")),
        scene_section(1, "b", 15, 1, object_lines(3, "0,14", scene=1)))
    texts["ms_layers_overlap_in_followed_sub"] = multi_text(
        ["display.scene=0"], scene_section(0, "a", 30, 1, object_lines(1, "0,29")),
        scene_section(1, "b", 30, 1, object_lines(3, "3,3", scene=1) + object_lines(4, "5,9", scene=1)))
    texts["ms_shortened_in_sub"] = multi_text(
        ["display.scene=0"], scene_section(0, "a", 30, 1, object_lines(1, "0,29")),
        scene_section(1, "b", 60, 1, object_lines(3, "2,2", scene=1, tracks=["X=0.00,4.00,直線移動,0"])))
    texts["ms_counter_in_followed"] = multi_text(
        ["display.scene=0"], scene_section(0, "a", 30, 1, object_lines(1, "0,29", effect="カウンター", extra=["速度=2.5"])),
        scene_section(1, "b", 60, 1, object_lines(3, "0,59", scene=1, effect="カウンター", extra=["速度=1"])))
    texts["ms_display_missing"] = multi_text(
        ["display.scene=5"], scene_section(0, "a", 30, 1, object_lines(1, "0,29")),
        scene_section(1, "b", 15, 1, object_lines(3, "0,14", scene=1)))
    texts["ms_display_default_missing"] = multi_text(
        [], scene_section(1, "b", 15, 1, object_lines(3, "0,14", scene=1)))
    texts["ms_display_not_number"] = multi_text(
        ["display.scene=abc"], scene_section(0, "a", 30, 1, object_lines(1, "0,29")))
    texts["ms_scene_without_rate"] = multi_text(
        ["display.scene=0"], scene_section(0, "a", 30, 1, object_lines(1, "0,29")),
        ["[scene.1]", "scene=1", "name=norate"])
    return list(texts.items())


def synthetic_text(body, rate=60, scene_extra=()):
    """A minimal .aup2 at the given scene rate: [project], [scene.0] and the given body lines."""
    lines = ["[project]", "version=2010400", "file=C:\\synthetic\\src.aup2", "display.scene=0",
             "preview.scene=0", "[scene.0]", "scene=0", "video.width=640", "video.height=360",
             "video.rate=%d" % rate, "video.scale=1", "audio.rate=44100", "cursor.frame=0", "cursor.layer=0",
             "preview.frame=0", "display.frame=0", "display.layer=0"]
    lines += list(scene_extra) + list(body)
    return "\n".join(lines) + "\n"


def effect_body(*lines):
    return BASE_BODY + ["[0.0]"] + list(lines)


# (case name, aup2 text, extra arguments). Each one must be refused by both tools.
REFUSAL_CASES = [
    ("coma_ochi", synthetic_text(BASE_BODY + ["コマ落ち=2"]), ["--target-fps", "66"]),
    ("coma_ochi_zero", synthetic_text(BASE_BODY + ["コマ落ち=0"]), ["--target-fps", "66"]),
    ("pipe_payload", synthetic_text(effect_body("effect.name=標準描画", "X=0.00,100.00,補間移動,0|27")),
     ["--target-fps", "66"]),
    ("pipe_payload_curve_other_mode", synthetic_text(effect_body("effect.name=標準描画", "X=0.00,100.00,瞬間移動,0|1,0,1,0")),
     ["--target-fps", "66"]),
    ("tc_curve_drop_not_exact", synthetic_text(
        ["[0]", "layer=0", "frame=0,59", "[0.0]", "effect.name=時間制御(オブジェクト)",
         "位置=0.000,100.000,直線移動(時間制御),0|0,0,1,0", "繰り返し=1", "コマ落ち=2", "対象レイヤー数=1",
         "[1]", "layer=1", "frame=0,59", "[1.0]", "effect.name=標準描画"], rate=30),
     ["--target-fps", "66"]),
    ("unknown_trackbar_mode", synthetic_text(effect_body("effect.name=標準描画", "X=0.00,100.00,未知移動,0")),
     ["--target-fps", "66"]),
    ("unknown_scene_frame_key", synthetic_text(BASE_BODY, scene_extra=["custom.frame.x=1"]),
     ["--target-fps", "66"]),
    ("text_frame_script", synthetic_text(effect_body("effect.name=テキスト", "テキスト=<?=obj.frame?>")),
     ["--target-fps", "66"]),
    ("counter_without_speed", synthetic_text(effect_body("effect.name=カウンター", "数値=0")), ["--target-fps", "66"]),
    ("counter_speed_not_number", synthetic_text(effect_body("effect.name=カウンター", "速度=abc")),
     ["--target-fps", "66"]),
    # 60 -> 24: frame=0,1,2 becomes 0,0,0.
    ("intermediate_collision", synthetic_text(["[0]", "layer=0", "frame=0,1,2"]), ["--target-fps", "24"]),
    # 60 -> 12: both objects shrink onto frame 0 on the same layer.
    ("same_layer_overlap", synthetic_text(["[0]", "layer=0", "frame=0,0", "[1]", "layer=0", "frame=2,5"]),
     ["--target-fps", "12"]),
    ("decimal_target", synthetic_text(BASE_BODY), ["--target-fps", "59.94"]),
]


def convert_inputs():
    """(label, path) for the fixture inputs. The real project is added by main() from its copy."""
    paths = sorted(glob.glob(os.path.join(VALIDATION_DIR, "*.aup2")))
    paths += [os.path.join(MATRIX_DIR, "fixture_60fps.aup2"), os.path.join(MATRIX_DIR, "fixture_72fps.aup2"),
              os.path.join(HOST_MATRIX_DIR, "scene_rate_test.aup2")]
    return [(os.path.relpath(path, REPO).replace(os.sep, "/"), path) for path in paths]


TC_MODE = "直線移動(時間制御)"


def tc_row(a, b, flag="0"):
    return "%s,%s,%s,%s" % (a, b, TC_MODE, flag)


def tc_object(no, layer, frames, row, drop="1", scene=None, effect_lines=None):
    """A 時間制御(オブジェクト) object. effect_lines replaces the whole effect body after effect.name."""
    lines = ["[%d]" % no, "layer=%d" % layer]
    if scene is not None:
        lines.append("scene=%d" % scene)
    lines += ["frame=%s" % frames, "[%d.0]" % no, "effect.name=時間制御(オブジェクト)"]
    if effect_lines is None:
        effect_lines = ["位置=" + row, "繰り返し=1", "コマ落ち=" + drop, "対象レイヤー数=1"]
    return lines + list(effect_lines)


def controlled_object(no, layer, frames, scene=None, tracks=()):
    return object_lines(no, frames, scene=scene, layer=layer, tracks=tracks)


def time_control_texts():
    """(name, aup2 text): synthetic projects for 時間制御(オブジェクト) and コマ落ち. Several are refused at some targets."""
    texts = {}
    track = ["X=0.00,100.00,直線移動,0"]
    blocks = []
    for i, (a, b) in enumerate((("0.000", "100.000"), ("100.000", "0.000"), ("20.000", "80.000"),
                                ("0.000", "100.000"), ("100.000", "0.000"))):
        blocks += tc_object(10 + i, 0, "%d,%d" % (12 * i, 12 * i + 11), tc_row(a, b))
    texts["tc_arz_blocks_30"] = synthetic_text(blocks + controlled_object(1, 1, "0,59", tracks=track), rate=30)
    texts["tc_arz_blocks_66"] = synthetic_text(blocks + controlled_object(1, 1, "0,59", tracks=track), rate=66)
    drops = tc_object(1, 0, "0,59", tc_row("0.000", "100.000"), "2") + \
        tc_object(2, 1, "0,59", tc_row("100.000", "0.000"), "3") + \
        tc_object(3, 2, "0,59", tc_row("10.000", "90.000"), "1") + \
        controlled_object(4, 3, "0,59")
    texts["tc_drop_30"] = synthetic_text(drops, rate=30)
    texts["tc_drop_66"] = synthetic_text(drops, rate=66)
    texts["tc_drop_leading_zero"] = synthetic_text(
        tc_object(1, 0, "0,59", tc_row("0.000", "100.000"), "02") + controlled_object(2, 1, "0,59"), rate=30)
    singles = tc_object(1, 0, "2,3", tc_row("0.000", "100.000")) + tc_object(2, 1, "5,5", tc_row("30.000", "60.000")) + \
        tc_object(3, 2, "7,9", tc_row("100.000", "0.000"), "2") + controlled_object(4, 3, "0,12")
    texts["tc_single_frames_30"] = synthetic_text(singles, rate=30)
    texts["tc_single_frames_60"] = synthetic_text(singles, rate=60)
    texts["tc_decimals"] = synthetic_text(
        tc_object(1, 0, "0,29", tc_row("0", "100")) + tc_object(2, 1, "30,59", tc_row("12.5", "87.25")) +
        tc_object(3, 2, "60,89", tc_row("-50.0", "150.0")) + tc_object(4, 3, "90,119", tc_row("-0.5", "0.25", "1")) +
        controlled_object(5, 4, "0,119"), rate=30)
    texts["tc_constant_and_precise"] = synthetic_text(
        tc_object(1, 0, "0,99", tc_row("25.000", "25.000")) + tc_object(2, 1, "0,99", tc_row("0.000", "100.000")) +
        controlled_object(3, 2, "0,99", tracks=track), rate=30)
    two = tc_object(1, 0, "0,59", None, effect_lines=[
        "位置=" + tc_row("0.000", "100.000"), "繰り返し=1", "コマ落ち=2", "対象レイヤー数=1",
        "[1.1]", "effect.name=時間制御(オブジェクト)", "位置=" + tc_row("100.000", "0.000"), "繰り返し=1", "コマ落ち=4",
        "対象レイヤー数=1"]) + controlled_object(2, 1, "0,59")
    texts["tc_two_effects_one_object"] = synthetic_text(two, rate=30)
    texts["tc_reordered_objects"] = synthetic_text(
        tc_object(5, 0, "12,23", tc_row("100.000", "0.000"), "2") + tc_object(2, 0, "0,11", tc_row("0.000", "100.000")) +
        controlled_object(9, 1, "0,23"), rate=30)
    ease = "イージング（通常）@イージング"
    ease_rows = [
        object_lines(1, "0,29", layer=0, tracks=["X=0.00,100.00,%s,0|27" % ease]),
        object_lines(2, "30,59", layer=1, tracks=["X=0.00,100.00,%s,0" % ease]),
        object_lines(3, "0,10,59", layer=2, tracks=["X=0.00,50.00,100.00,%s,3|-5" % ease]),
        object_lines(4, "0,29", layer=3, tracks=["X=5.00,5.00,%s,0" % ease, "Y=1.00,1.00,直線移動,0"]),
        object_lines(5, "30,59", layer=4, tracks=["X=5.00,5.00,%s,0|27" % ease]),
    ]
    texts["ease_all_kinds"] = synthetic_text([line for rows in ease_rows for line in rows], rate=30)
    for index, rows in enumerate(ease_rows, 1):
        texts["ease_single_%d" % index] = synthetic_text(rows, rate=30)
    texts["ease_move_refused"] = synthetic_text(
        object_lines(1, "0,29", tracks=["X=0.00,100.00,イージング（補間移動）@イージング,0"]), rate=30)
    texts["ease_payload_other_mode"] = synthetic_text(object_lines(1, "0,29", tracks=["X=0.00,100.00,補間移動,0|27"]), rate=30)
    texts["ease_count_mismatch"] = synthetic_text(
        object_lines(1, "0,29", tracks=["X=0.00,50.00,100.00,%s,0|27" % ease]), rate=30)
    texts["tc_flag_one"] = synthetic_text(
        tc_object(1, 0, "0,59", tc_row("0.000", "100.000", "1")) + controlled_object(2, 1, "0,59"), rate=30)
    # 直線移動(時間制御) on ordinary tracks of any effect and '|' payloads (kept as written; --precise rules).
    ease_mode = "イージング（通常）@イージング"
    curve = "0|1,0,1,0"
    tcm_rows = [
        object_lines(1, "0,29", layer=0, tracks=["X=-600.00,600.00,%s,0" % TC_MODE]),
        object_lines(2, "30,59", layer=1, tracks=["X=-600.00,600.00,%s,%s" % (TC_MODE, curve)]),
        object_lines(3, "0,10,59", layer=2, tracks=["X=0.00,50.00,100.00,%s,0" % TC_MODE]),
        object_lines(4, "0,29", layer=3, tracks=["Y=0.00,0.00,直線移動,%s" % curve, "X=1.00,2.00,直線移動,%s" % curve]),
        object_lines(5, "30,59", layer=4, tracks=["X=3.00,4.00,%s,%s" % (ease_mode, curve)]),
        object_lines(6, "0,29", layer=5, tracks=["拡大率=100.000,150.000,%s,0" % TC_MODE], effect="映像再生"),
        object_lines(7, "0,29", layer=6, tracks=["X=5.00,5.00,%s,0" % TC_MODE, "Y=0.00,100.00,直線移動,0"]),
        object_lines(8, "0,29", layer=7, tracks=["X=0.00,100.00,直線移動,0", "Y=0.00,100.00,%s,0" % TC_MODE]),
    ]
    texts["tcm_all_kinds"] = synthetic_text([line for rows in tcm_rows for line in rows], rate=30)
    texts["tcm_all_kinds_66"] = synthetic_text([line for rows in tcm_rows for line in rows], rate=66)
    for index, rows in enumerate(tcm_rows, 1):
        texts["tcm_single_%d" % index] = synthetic_text(rows, rate=30)
        texts["tcm_single_66_%d" % index] = synthetic_text(
            [row.replace("0,29", "0,65").replace("30,59", "66,131").replace("0,10,59", "0,20,131") for row in rows], rate=66)
    texts["tcm_count_mismatch"] = synthetic_text(
        object_lines(1, "0,29", tracks=["X=0.00,50.00,100.00,%s,0" % TC_MODE]), rate=30)
    texts["tcm_unknown_mode_suffix"] = synthetic_text(
        object_lines(1, "0,29", tracks=["X=0.00,50.00,%s謎,0" % TC_MODE]), rate=30)
    # A comma-free payload on every mode, and a '|' inside a value that is not a trackbar.
    texts["tcm_pipe_inside_text"] = synthetic_text(
        object_lines(1, "0,29", tracks=["X=0.00,100.00,直線移動,0|27", "Y=1,2,3,4|5,6"]), rate=30)
    # 時間制御(オブジェクト) with a curve in 位置 (kept as written) and コマ落ち K x k.
    cv = "0|0,0,1,0"
    curves = tc_object(1, 0, "0,59", tc_row("0.000", "100.000", cv)) + \
        tc_object(2, 1, "0,59", tc_row("100.000", "0.000", cv), "2") + \
        tc_object(3, 2, "2,59", tc_row("10.000", "90.000", cv), "4") + \
        controlled_object(4, 3, "0,59", tracks=["X=0.00,100.00,直線移動,0"])
    texts["tc_curve_30"] = synthetic_text(curves, rate=30)
    texts["tc_curve_60"] = synthetic_text(curves, rate=60)
    texts["tc_curve_66"] = synthetic_text(curves, rate=66)
    texts["tc_curve_no_drop"] = synthetic_text(
        tc_object(1, 0, "0,59", tc_row("0.000", "100.000", cv)) + controlled_object(2, 1, "0,59"), rate=30)
    texts["tc_curve_single_frame"] = synthetic_text(
        tc_object(1, 0, "5,5", tc_row("0.000", "100.000", cv), "3") + controlled_object(2, 1, "0,12"), rate=30)
    texts["tc_curve_mixed_with_lines"] = synthetic_text(
        tc_object(1, 0, "0,59", tc_row("0.000", "100.000", cv)) + tc_object(2, 1, "0,59", tc_row("0.000", "100.000")) +
        tc_object(3, 2, "0,59", tc_row("100.000", "0.000"), "2") + controlled_object(4, 3, "0,59"), rate=30)
    texts["tc_curve_other_row"] = synthetic_text(
        tc_object(1, 0, "0,59", None, effect_lines=["位置=" + tc_row("0.000", "100.000", cv), "繰り返し=1", "コマ落ち=1",
                                                    "対象レイヤー数=1", "Y=7.00,7.00,%s,%s" % (TC_MODE, cv)]) +
        controlled_object(2, 1, "0,59"), rate=30)
    texts["tc_curve_subscene"] = multi_text(
        ["display.scene=0"], scene_section(0, "main", 30, 1, SCENE_EFFECT_OBJECT),
        scene_section(1, "sub", 15, 1, tc_object(3, 0, "0,74", tc_row("100.000", "0.000", cv), "2", scene=1) +
                      controlled_object(4, 1, "0,74", scene=1)))
    # Multi-scene: a 15 FPS sub scene with a time control object (kept under auto, followed under follow).
    sub_tc = tc_object(3, 0, "0,74", tc_row("100.000", "0.000"), "2", scene=1) + \
        controlled_object(4, 1, "0,74", scene=1)
    texts["tc_subscene"] = multi_text(["display.scene=0"], scene_section(0, "main", 30, 1, SCENE_EFFECT_OBJECT),
                                      scene_section(1, "sub", 15, 1, sub_tc))
    sub_bad = tc_object(3, 0, "0,30,74", "0.000,50.000,100.000,%s,0" % TC_MODE, "2", scene=1)
    texts["tc_subscene_bad_kept"] = multi_text(["display.scene=0"], scene_section(0, "main", 30, 1, SCENE_EFFECT_OBJECT),
                                               scene_section(1, "sub", 15, 1, sub_bad))
    texts["tc_subscene_60_sub_30"] = multi_text(
        ["display.scene=0"], scene_section(0, "main", 60, 1, SCENE_EFFECT_OBJECT),
        scene_section(1, "sub", 30, 1, tc_object(3, 0, "0,59", tc_row("100.000", "0.000"), "3", scene=1) +
                      controlled_object(4, 1, "0,59", scene=1)))

    # --precise holds the last value (and the first one) of an object: keyframes are added at the ends.
    ph_tracks = ["X=0.00,100.00,直線移動,0", "Y=5.00,5.00,補間移動,0"]
    ph_objects = object_lines(1, "0,29", layer=0, tracks=ph_tracks) + \
        object_lines(2, "1,10", layer=1, tracks=["X=0.00,90.00,直線移動,0"]) + \
        object_lines(3, "3,12", layer=2, tracks=["X=-20.50,90.25,直線移動,0"]) + \
        object_lines(4, "7,29", layer=3, tracks=["X=100.00,-100.00,直線移動,0"]) + \
        object_lines(5, "0,10,29", layer=4, tracks=["X=0.00,50.00,100.00,直線移動,0", "Z=1.0,1.0,1.0,直線移動,0"]) + \
        object_lines(6, "5,5", layer=5, tracks=["X=0.00,10.00,直線移動,0"]) + \
        object_lines(7, "2,3", layer=6, tracks=["X=0.00,100.00,直線移動,0"]) + \
        object_lines(8, "11,40", layer=7, tracks=["X=0.00,100.00,直線移動,0"]) + \
        object_lines(9, "4,20,33", layer=8, tracks=["X=3.00,-4.00,9.50,直線移動,0"])
    for rate in (30, 60, 66, 24, 72):
        texts["ph_linear_%d" % rate] = synthetic_text(ph_objects, rate=rate)
    texts["ph_with_time_control"] = synthetic_text(
        tc_object(1, 0, "0,29", None, effect_lines=[
            "位置=" + tc_row("0.000", "100.000"), "繰り返し=1", "コマ落ち=1", "対象レイヤー数=1",
            "[1.1]", "effect.name=標準描画", "X=0.00,100.00,直線移動,0"]) +
        controlled_object(2, 1, "0,29", tracks=["X=0.00,100.00,直線移動,0"]), rate=30)
    texts["ph_skipped_object_still_skipped"] = synthetic_text(
        object_lines(1, "0,29", tracks=["X=0.00,100.00,補間移動,0"]) +
        object_lines(2, "0,29", layer=1, tracks=["X=0.00,100.00,直線移動,0"]), rate=30)

    # Steps (瞬間移動) for a time control that a two-point line cannot give.
    reversed_drops = []
    for i, drop in enumerate(("1", "2", "3", "4", "5", "7")):
        reversed_drops += tc_object(10 + i, 0, "%d,%d" % (30 * i, 30 * i + 29), tc_row("100.000", "0.000"), drop)
    texts["tcs_reversed_drops_30"] = synthetic_text(reversed_drops + controlled_object(1, 1, "0,179"), rate=30)
    texts["tcs_reversed_drops_66"] = synthetic_text(reversed_drops + controlled_object(1, 1, "0,179"), rate=66)
    texts["tcs_reversed_drops_60"] = synthetic_text(reversed_drops + controlled_object(1, 1, "0,179"), rate=60)
    origin = tc_object(10, 0, "1,20", tc_row("0.000", "100.000"), "5") + \
        tc_object(11, 1, "3,30", tc_row("100.000", "0.000"), "4") + \
        tc_object(12, 2, "7,50", tc_row("20.000", "80.000"), "3") + \
        tc_object(13, 3, "5,5", tc_row("0.000", "100.000"), "2") + \
        controlled_object(1, 4, "0,59")
    texts["tcs_origin_offsets_30"] = synthetic_text(origin, rate=30)
    texts["tcs_origin_offsets_66"] = synthetic_text(origin, rate=66)
    clamps = tc_object(10, 0, "0,29", tc_row("-50.000", "100.000")) + \
        tc_object(11, 1, "0,29", tc_row("0.000", "150.000")) + \
        tc_object(12, 2, "0,29", tc_row("-20.000", "-5.000")) + \
        tc_object(13, 3, "0,29", tc_row("120.000", "130.000")) + \
        tc_object(14, 4, "0,29", tc_row("30.000", "30.000")) + \
        tc_object(15, 5, "0,29", tc_row("150", "-50"), "2") + \
        controlled_object(1, 6, "0,29")
    texts["tcs_clamped_ends_30"] = synthetic_text(clamps, rate=30)
    texts["tcs_clamped_ends_66"] = synthetic_text(clamps, rate=66)
    steps_head = ["[0]", "layer=0", "frame=0,11", "[0.0]", "effect.name=時間制御(オブジェクト)",
                  "位置=" + tc_row("100.000", "0.000")]
    tail = ["繰り返し=1", "コマ落ち=1", "対象レイヤー数=1"]
    texts["tcs_next_to_constant_trackbar"] = synthetic_text(
        steps_head + tail + ["X=5.0,5.00,補間移動,0"] + controlled_object(1, 1, "0,11"), rate=30)
    texts["tcs_next_to_changing_trackbar"] = synthetic_text(
        steps_head + tail + ["X=0.00,10.00,直線移動,0"] + controlled_object(1, 1, "0,11"), rate=30)
    texts["tcs_next_to_range_row"] = synthetic_text(
        steps_head + tail + ["R=0,11,再生範囲,1"] + controlled_object(1, 1, "0,11"), rate=30)
    texts["tcs_line_next_to_changing_trackbar"] = synthetic_text(
        ["[0]", "layer=0", "frame=0,11", "[0.0]", "effect.name=時間制御(オブジェクト)",
         "位置=" + tc_row("0.000", "100.000")] + tail + ["X=0.00,10.00,直線移動,0"] + controlled_object(1, 1, "0,11"),
        rate=30)
    texts["tcs_no_drop_row"] = synthetic_text(
        steps_head + ["繰り返し=1", "対象レイヤー数=1"] + controlled_object(1, 1, "0,11"), rate=30)
    texts["tcs_one_frame_drops"] = synthetic_text(
        tc_object(10, 0, "2,3", tc_row("0.000", "100.000"), "4") + tc_object(11, 1, "6,7", tc_row("100.000", "0.000"), "2") +
        tc_object(12, 2, "9,9", tc_row("30.000", "60.000"), "3") + controlled_object(1, 3, "0,12"), rate=30)
    texts["tcs_two_effects_line_and_steps"] = synthetic_text(
        tc_object(1, 0, "0,59", None, effect_lines=[
            "位置=" + tc_row("20.000", "80.000"), "繰り返し=1", "コマ落ち=2", "対象レイヤー数=1",
            "[1.1]", "effect.name=時間制御(オブジェクト)", "位置=" + tc_row("100.000", "0.000"), "繰り返し=1", "コマ落ち=4",
            "対象レイヤー数=1"]) + controlled_object(2, 1, "0,59"), rate=30)
    texts["tcs_two_constant_effects_steps"] = synthetic_text(
        tc_object(1, 0, "0,59", None, effect_lines=[
            "位置=" + tc_row("25.000", "25.000"), "繰り返し=1", "コマ落ち=1", "対象レイヤー数=1",
            "[1.1]", "effect.name=時間制御(オブジェクト)", "位置=" + tc_row("100.000", "0.000"), "繰り返し=1", "コマ落ち=3",
            "対象レイヤー数=1"]) + controlled_object(2, 1, "0,59"), rate=30)
    texts["tcs_step_limit_long_block"] = synthetic_text(
        tc_object(1, 0, "0,9999", tc_row("100.000", "0.000"), "1") + controlled_object(2, 1, "0,9999"), rate=30)
    texts["tcs_step_limit_8185"] = synthetic_text(
        tc_object(1, 0, "0,8185", tc_row("0.000", "100.000"), "2") + controlled_object(2, 1, "0,8185"), rate=30)
    texts["tcs_step_limit_8189"] = synthetic_text(
        tc_object(1, 0, "0,8189", tc_row("0.000", "100.000"), "2") + controlled_object(2, 1, "0,8189"), rate=30)
    texts["tcs_k1_scene"] = synthetic_text(
        tc_object(1, 0, "0,29", tc_row("-50.000", "150.000"), "3") + tc_object(2, 1, "30,30", tc_row("30.000", "60.000"), "2") +
        controlled_object(3, 2, "0,59"), rate=60)
    texts["tcs_sub_scene_steps"] = multi_text(
        ["display.scene=0"], scene_section(0, "main", 30, 1, SCENE_EFFECT_OBJECT),
        scene_section(1, "sub", 15, 1, tc_object(3, 0, "0,74", tc_row("100.000", "0.000"), "2", scene=1) +
                      tc_object(5, 1, "75,89", tc_row("0.000", "100.000"), "3", scene=1) +
                      controlled_object(4, 2, "0,89", scene=1)))

    # One project per refusal (each one is converted or refused by the Python tool at every target). Not here: a コマ落ち
    # beyond 64 bits (Python has big integers and converts it, the port refuses it; see retime_test.cpp).
    base = controlled_object(9, 5, "0,59")
    bad = {
        "tc_bad_three_point_frame": tc_object(1, 0, "0,30,59", tc_row("0.000", "100.000")),
        "tc_bad_three_positions": tc_object(1, 0, "0,59", "0.000,50.000,100.000,%s,0" % TC_MODE),
        "tc_bad_payload": tc_object(1, 0, "0,59", tc_row("0.000", "100.000", "0|27")),
        "tc_bad_other_mode": tc_object(1, 0, "0,59", "0.000,100.000,直線移動,0"),
        "tc_bad_one_position": tc_object(1, 0, "0,59", "50"),
        "tc_bad_no_position": tc_object(1, 0, "0,59", None, effect_lines=["繰り返し=1", "コマ落ち=1", "対象レイヤー数=1"]),
        "tc_bad_text_position": tc_object(1, 0, "0,59", "a,b,%s,0" % TC_MODE),
        "tc_bad_drop_zero": tc_object(1, 0, "0,59", tc_row("0.000", "100.000"), "0"),
        "tc_bad_drop_double_zero": tc_object(1, 0, "0,59", tc_row("0.000", "100.000"), "00"),
        "tc_bad_drop_negative": tc_object(1, 0, "0,59", tc_row("0.000", "100.000"), "-2"),
        "tc_bad_drop_decimal": tc_object(1, 0, "0,59", tc_row("0.000", "100.000"), "1.5"),
        "tc_bad_drop_plus": tc_object(1, 0, "0,59", tc_row("0.000", "100.000"), "+2"),
        "tc_bad_drop_empty": tc_object(1, 0, "0,59", tc_row("0.000", "100.000"), ""),
        "tc_bad_drop_track": tc_object(1, 0, "0,59", tc_row("0.000", "100.000"), "0,10,直線移動,0"),
        "tc_bad_drop_five": tc_object(1, 0, "0,59", tc_row("0.000", "100.000"), "5"),
        "tc_bad_drop_seven": tc_object(1, 0, "0,59", tc_row("0.000", "100.000"), "7"),
        "tc_bad_drop_in_other_effect": object_lines(1, "0,59", extra=["コマ落ち=2"]),
        "tc_bad_drop_in_object": ["[1]", "layer=0", "frame=0,59", "コマ落ち=3"],
        "tc_bad_mode_elsewhere": object_lines(1, "0,59", tracks=["X=0.000,100.000,%s,0" % TC_MODE]),
        "tc_bad_mode_second_row": tc_object(1, 0, "0,59", None, effect_lines=[
            "位置=" + tc_row("0.000", "100.000"), "Y=0.000,1.000,%s,0" % TC_MODE, "繰り返し=1", "コマ落ち=1"]),
        "tc_bad_marker_in_time_control": tc_object(1, 0, "0,59", None, effect_lines=[
            "位置=" + tc_row("0.000", "100.000"), "Z=obj.frame"]),
    }
    for name, body in bad.items():
        texts[name] = synthetic_text(list(body) + base, rate=30)
    texts["tc_bad_drop_in_scene_section"] = synthetic_text(
        tc_object(1, 0, "0,59", tc_row("0.000", "100.000")) + base, rate=30, scene_extra=["コマ落ち=2"])
    texts["tc_bad_two_problems_first_reported"] = synthetic_text(
        tc_object(1, 0, "0,59", tc_row("0.000", "100.000"), "5") + tc_object(2, 1, "0,30,59", tc_row("0.000", "100.000")) +
        base, rate=30)
    return list(texts.items())


def time_control_cases(template_copy, synthetic_dir):
    """The (label, path, arguments) cases of the time control rules; see the module docstring."""
    cases = []
    inputs = []
    if template_copy:
        inputs.append(("ARZ template 15_mer1", template_copy))
    probes = sorted(glob.glob(os.path.join(PROBE_DIR, "*.aup2")))
    inputs += [("probe " + os.path.basename(path), path) for path in probes]
    for label, path in inputs:
        for target in TIME_CONTROL_TARGETS:
            cases.append((label, path, target))
            cases.append((label, path, target + ["--precise"]))
    for name, text in time_control_texts():
        path = os.path.join(synthetic_dir, name + ".aup2")
        with open(path, "wb") as fh:
            fh.write(text.encode("utf-8"))
        label = "synthetic " + name
        for target in TARGETS:
            cases.append((label, path, target))
        for target in PRECISE_TARGETS:
            cases.append((label, path, target + ["--precise"]))
        if name.startswith(("tc_subscene", "tcs_sub_scene", "tc_curve_subscene")) and python_tool_has_option("--subscene-rate"):
            for policy in SUBSCENE_POLICIES:
                for target in TIME_CONTROL_TARGETS:
                    cases.append((label, path, target + ["--subscene-rate", policy]))
                    cases.append((label, path, target + ["--subscene-rate", policy, "--precise"]))
    return cases


FRAME_MODE_TARGETS = [["--target-fps", rate] for rate in ("60", "66", "120", "132", "60000/1001", "24")]
FRAME_MODE_TARGETS += [["--factor", "2"]]
FRAME_MODE_RANGE = "0,149"


def fm_targets(*rates):
    return [["--target-fps", rate] for rate in rates]


def frame_mode_cases(synthetic_dir):
    """The (label, path, arguments) cases of 反復移動, ランダム移動 and 移動量指定; see the module docstring."""
    cases = []
    inputs = sorted(glob.glob(os.path.join(FRAME_MODE_DIR, "*.aup2")))
    if not inputs:
        print("SKIP frame-mode inputs: no .aup2 in %s" % FRAME_MODE_DIR)
    for path in inputs:
        label = "9 probe " + os.path.basename(path)
        for target in FRAME_MODE_TARGETS:
            cases.append((label, path, target))
            cases.append((label, path, target + ["--precise"]))

    def write(name, text):
        path = os.path.join(synthetic_dir, name + ".aup2")
        with open(path, "wb") as fh:
            fh.write(text.encode("utf-8"))
        return "synthetic " + name, path

    def one_object(*tracks, frames=FRAME_MODE_RANGE):
        return synthetic_text(object_lines(1, frames, tracks=list(tracks)), rate=30)

    overlap = tc_object(1, 0, "0,59", tc_row("0.000", "100.000")) + \
        object_lines(2, "10,40", layer=1, tracks=["X=0.00,100.00,反復移動,4|15"])
    refusals = [
        ("fm_pipe_on_move", one_object("X=0.00,100.00,移動量指定,4|2"), ["--target-fps", "60"]),
        ("fm_no_pipe_hanpuku", one_object("X=0.00,100.00,反復移動,4"), ["--target-fps", "60"]),
        ("fm_no_pipe_random", one_object("X=0.00,100.00,ランダム移動,4"), ["--target-fps", "60"]),
        ("fm_flag_3_hanpuku", one_object("X=0.00,100.00,反復移動,3|15"), ["--target-fps", "60"]),
        ("fm_flag_0_move", one_object("X=0.00,4.00,移動量指定,0"), ["--target-fps", "60"]),
        ("fm_three_values", one_object("X=0.00,50.00,100.00,反復移動,4|15"), ["--target-fps", "60"]),
        ("fm_three_frames", one_object("X=0.00,100.00,反復移動,4|15", frames="0,10,59"), ["--target-fps", "60"]),
        ("fm_param_half", one_object("X=0.00,100.00,反復移動,4|0.5"), ["--target-fps", "60"]),
        ("fm_param_abc", one_object("X=0.00,100.00,反復移動,4|abc"), ["--target-fps", "60"]),
        ("fm_random_15_4", one_object("X=0.00,100.00,ランダム移動,4|15.4"), ["--target-fps", "60"]),
        ("fm_random_30_to_60000_1001", one_object("X=0.00,100.00,ランダム移動,4|30"),
         ["--target-fps", "60000/1001"]),
        ("fm_error_bound", synthetic_text(object_lines(1, "0,29999", tracks=["X=-100000.00,100000.00,反復移動,4|7"]),
                                          rate=30), ["--target-fps", "60000/1001"]),
        ("fm_overlaps_time_control", synthetic_text(overlap, rate=30), ["--target-fps", "60"]),
    ]
    for name, text, extra in refusals:
        label, path = write(name, text)
        cases.append((label, path, extra))

    accepted = [
        ("fm_accept_hanpuku", one_object("X=-600.00,600.00,反復移動,4|15", "Y=0.00,0.00,反復移動,4|15"),
         fm_targets("60", "66", "60000/1001", "24")),
        ("fm_accept_random_15", one_object("X=-600.00,600.00,ランダム移動,4|15"), fm_targets("60", "66", "24")),
        ("fm_accept_random_1001", one_object("X=-600.00,600.00,ランダム移動,4|1001"), fm_targets("60", "60000/1001")),
        ("fm_accept_move", one_object("X=-600.00,4.00,移動量指定,4", "Y=0.00,0.00,移動量指定,4", "Z=0.00,0.00,移動量指定,4"),
         fm_targets("60", "66", "60000/1001", "24")),
    ]
    for name, text, targets in accepted:
        label, path = write(name, text)
        for target in targets:
            cases.append((label, path, target))
            cases.append((label, path, target + ["--precise"]))
    return cases


def user_project_sources():
    """(label, path) of the read-only user projects that exist: the desktop samples and every ARZ output.aup2."""
    found = [(os.path.splitext(os.path.basename(path))[0], path) for path in USER_PROJECTS if os.path.isfile(path)]
    for path in USER_PROJECTS:
        if not os.path.isfile(path):
            print("SKIP user project: %s is missing" % path)
    pattern = os.path.join(ARZ_ROOT, "**", "output.aup2")
    for index, path in enumerate(sorted(glob.glob(pattern, recursive=True)), 1):
        found.append(("ARZ output.aup2 #%d" % index, path))
    if not os.path.isdir(ARZ_ROOT):
        print("SKIP user projects: %s is missing" % ARZ_ROOT)
    return found


def user_project_cases(work):
    """Copies of the user projects (read-only originals) at 60, 66, 120 and 2x, precise off and on."""
    cases = []
    for index, (label, path) in enumerate(user_project_sources()):
        copy = os.path.join(work, "user_project_%d.aup2" % index)
        shutil.copyfile(path, copy)
        for target in USER_TARGETS:
            cases.append((label, copy, target))
            cases.append((label, copy, target + ["--precise"]))
    return cases


def run_tool(prefix, in_path, out_path, args):
    """Run one tool. Returns (exit code, stderr bytes, .aup2 bytes or None, .convert.json bytes or None)."""
    clean_outputs(out_path)
    proc = subprocess.run(prefix + ["--in", in_path, "--out", out_path] + args, capture_output=True)
    return proc.returncode, proc.stderr, read_optional(out_path), read_optional(out_path + JSON_SUFFIX)


def read_optional(path):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as fh:
        return fh.read()


def clean_outputs(out_path):
    for path in (out_path, out_path + JSON_SUFFIX):
        if os.path.exists(path):
            os.remove(path)


def python_tool_supports_target_fps():
    proc = subprocess.run([sys.executable, "-I", PY_TOOL, "--help"], capture_output=True)
    return proc.returncode == 0 and b"--target-fps" in proc.stdout


def python_tool_has_option(option):
    proc = subprocess.run([sys.executable, "-I", PY_TOOL, "--help"], capture_output=True)
    return proc.returncode == 0 and option.encode("ascii") in proc.stdout


def python_tool_supports_precise():
    return python_tool_has_option("--precise")


def show_line(data):
    if data is None:
        return "<no line>"
    return repr(data.decode("utf-8", "backslashreplace"))


def first_difference(a, b):
    """Describe the first differing line of two byte strings (None means the file is missing)."""
    if a is None or b is None:
        return "file missing for %s" % ("c++" if b is None else "python")
    a_lines, b_lines = a.split(b"\n"), b.split(b"\n")
    for i in range(max(len(a_lines), len(b_lines))):
        x = a_lines[i] if i < len(a_lines) else None
        y = b_lines[i] if i < len(b_lines) else None
        if x != y:
            return "line %d: python %s | c++ %s" % (i + 1, show_line(x), show_line(y))
    return "no line differs"


def compare_case(in_path, args, case_dir, exe):
    """Run both tools on one case. Returns (problems, python exit code); no problems means a match."""
    out_path = os.path.join(case_dir, "retimed.aup2")
    py = run_tool([sys.executable, "-I", PY_TOOL], in_path, out_path, args)
    cpp = run_tool([exe], in_path, out_path, args)
    problems = []
    if py[0] != cpp[0]:
        problems.append("exit codes differ (python %d, c++ %d)" % (py[0], cpp[0]))
    if py[1] != cpp[1]:
        problems.append("refusal text differs: " + first_difference(py[1], cpp[1]))
    if py[2] != cpp[2]:
        problems.append(".aup2 differs: " + first_difference(py[2], cpp[2]))
    if py[3] != cpp[3]:
        problems.append(".convert.json differs: " + first_difference(py[3], cpp[3]))
    return problems, py[0]


def subscene_cases(real_copy, work, synthetic_dir):
    """The (label, path, arguments) cases of --subscene-rate; see the module docstring.
    real_copy is None when the real project is absent."""
    cases = []
    real_inputs = [("real ARZv2.0.1 project", real_copy)] if real_copy else []
    # Single-scene inputs: the policy changes nothing in the .aup2; the sidecar keys must still match.
    for label, path in convert_inputs() + real_inputs:
        for policy in SUBSCENE_POLICIES:
            for target in POLICY_SINGLE_TARGETS:
                for precise in ([], ["--precise"]):
                    cases.append((label, path, target + ["--subscene-rate", policy] + precise))

    # Multi-scene inputs: the Python tests' fixtures, the host sample (copied) and synthetic projects.
    multi = [(os.path.relpath(path, REPO).replace(os.sep, "/"), path)
             for path in sorted(glob.glob(os.path.join(MULTI_SCENE_DIR, "*.aup2")))]
    if os.path.isfile(HOST_SAMPLE):
        host_copy = os.path.join(work, "host_sample.aup2")
        shutil.copyfile(HOST_SAMPLE, host_copy)
        multi.append(("host sample test 30 and 30or60", host_copy))
    else:
        print("SKIP host sample: %s is missing" % HOST_SAMPLE)
    for name, text in multi_scene_texts():
        path = os.path.join(synthetic_dir, name + ".aup2")
        with open(path, "wb") as fh:
            fh.write(text.encode("utf-8"))
        multi.append(("synthetic " + name, path))
    for label, path in multi:
        for policy in SUBSCENE_POLICIES:
            option = ["--subscene-rate", policy]
            for target in MULTI_TARGETS:
                cases.append((label, path, target + option))
            for target in MULTI_PRECISE_TARGETS:
                cases.append((label, path, target + option + ["--precise"]))
            cases.append((label, path, MULTI_EXTRA_DECIMALS + option))
    return cases


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    sys.stderr.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--exe", default=os.path.join(EXPERIMENT, "build", "x64", "retime_cli.exe"))
    parser.add_argument("--work", default=DEFAULT_WORK, help="scratch directory (emptied at start)")
    parser.add_argument("--keep", action="store_true", help="keep the work directory after the run")
    parser.add_argument("--summary-only", action="store_true", help="print only mismatches and the counts")
    args = parser.parse_args(argv)

    exe = os.path.abspath(args.exe)
    work = os.path.abspath(args.work)
    if not os.path.isfile(exe):
        print("SKIP: retime_cli not built at %s (run build_and_run.ps1 first)" % exe)
        return 2
    if not os.path.isfile(PY_TOOL):
        print("SKIP: Python tool missing at %s" % PY_TOOL)
        return 2
    have_real = os.path.isfile(REAL_PROJECT)
    if not have_real:
        print("SKIP real project (not found)")
    if not python_tool_supports_target_fps():
        print("SKIP: tools/aup2_fps_upconvert.py has no --target-fps; the lead must rerun this check later")
        return 2

    shutil.rmtree(work, ignore_errors=True)
    synthetic_dir = os.path.join(work, "synthetic")
    os.makedirs(synthetic_dir)
    real_copy = os.path.join(work, "real_project.aup2")
    try:
        if have_real:
            shutil.copyfile(REAL_PROJECT, real_copy)
        real_inputs = [("real ARZv2.0.1 project", real_copy)] if have_real else []

        # (case label, input path, extra arguments, kind) for the cross product, then the refusal cases.
        cases = []
        for label, path in convert_inputs() + real_inputs:
            for target in TARGETS:
                cases.append((label, path, target))
        refusal_cases_extra = []
        if python_tool_supports_precise():
            for label, path in convert_inputs():
                for target in PRECISE_TARGETS:
                    cases.append((label, path, target + ["--precise"]))
            if have_real:
                cases.append(("real ARZv2.0.1 project", real_copy, PRECISE_REAL_TARGET + ["--precise"]))
            if python_tool_has_option("--precise-extra-decimals"):
                for label, path in convert_inputs():
                    cases.append((label, path, ["--target-fps", "66", "--precise", "--precise-extra-decimals", "2"]))
                if have_real:
                    cases.append(("real ARZv2.0.1 project", real_copy,
                                  ["--target-fps", "66", "--precise", "--precise-extra-decimals", "2"]))
                extra_refusals = [
                    ("extra_decimals_without_precise", ["--target-fps", "66", "--precise-extra-decimals", "2"]),
                    ("extra_decimals_above_4", ["--target-fps", "66", "--precise", "--precise-extra-decimals", "5"]),
                ]
                refusal_cases_extra.extend((name, synthetic_text(BASE_BODY), args) for name, args in extra_refusals)
            else:
                print("SKIP precise --precise-extra-decimals cases: tools/aup2_fps_upconvert.py has no such option yet")
        else:
            print("SKIP precise cases: tools/aup2_fps_upconvert.py has no --precise yet; rerun this check later")
        if python_tool_has_option("--subscene-rate"):
            cases.extend(subscene_cases(real_copy if have_real else None, work, synthetic_dir))
        else:
            print("SKIP subscene-rate cases: tools/aup2_fps_upconvert.py has no --subscene-rate yet; rerun this check later")
        template_copy = None
        if os.path.isfile(ARZ_TEMPLATE):
            template_copy = os.path.join(work, "arz_template_15_mer1.aup2")
            shutil.copyfile(ARZ_TEMPLATE, template_copy)
        else:
            print("SKIP ARZ template: %s is missing" % ARZ_TEMPLATE)
        cases.extend(time_control_cases(template_copy, synthetic_dir))
        cases.extend(frame_mode_cases(synthetic_dir))
        cases.extend(user_project_cases(work))
        for name, text, extra in REFUSAL_CASES + refusal_cases_extra:
            path = os.path.join(synthetic_dir, name + ".aup2")
            with open(path, "wb") as fh:
                fh.write(text.encode("utf-8"))
            cases.append(("synthetic " + name, path, extra))

        rows = []
        for number, (label, in_path, extra) in enumerate(cases, 1):
            case_dir = os.path.join(work, "case_%03d" % number)
            os.makedirs(case_dir)
            problems, py_rc = compare_case(in_path, extra, case_dir, exe)
            rows.append((number, label, " ".join(extra), py_rc, problems))
    finally:
        if os.path.exists(real_copy):
            os.remove(real_copy)
        for user_copy in glob.glob(os.path.join(work, "user_project_*.aup2")):
            os.remove(user_copy)
        template_path = os.path.join(work, "arz_template_15_mer1.aup2")
        if os.path.exists(template_path):
            os.remove(template_path)

    matches = sum(1 for row in rows if not row[4])
    converted = sum(1 for row in rows if row[3] == 0)
    refused = sum(1 for row in rows if row[3] != 0)
    mismatches = [row for row in rows if row[4]]

    print("%4s  %-40s %-24s %-9s %s" % ("No", "Input", "Arguments", "Result", "Detail"))
    for number, label, arg_text, py_rc, problems in rows:
        if args.summary_only and not problems:
            continue
        if problems:
            result, detail = "MISMATCH", problems[0]
        else:
            result = "match"
            detail = "converted" if py_rc == 0 else "refused (exit %d)" % py_rc
        print("%4d  %-40s %-24s %-9s %s" % (number, label, arg_text, result, detail))
    print()
    print("cases: %d (converted %d, refused %d)" % (len(rows), converted, refused))
    print("matches: %d" % matches)
    print("mismatches: %d" % len(mismatches))
    for number, label, arg_text, _, problems in mismatches:
        print("MISMATCH case %d: %s %s" % (number, label, arg_text))
        for problem in problems:
            print("  " + problem)

    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    cleaned = not os.path.exists(real_copy) and (args.keep or not os.path.exists(work))
    print("cleanup: real project and template copies removed; work directory %s"
          % ("kept at %s" % work if args.keep else "removed"))
    if not cleaned:
        print("cleanup: WARNING, some files remain")
    print("ALL MATCH" if not mismatches else "MISMATCH: %d case(s)" % len(mismatches))
    return 0 if not mismatches else 1


if __name__ == "__main__":
    sys.exit(main())
