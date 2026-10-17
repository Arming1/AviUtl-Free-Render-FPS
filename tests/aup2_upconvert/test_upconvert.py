"""Tests for tools/aup2_fps_upconvert.py.

Run with: python -I tests/aup2_upconvert/test_upconvert.py   (or run.ps1 next to this file)
Temporary files go to $AUP2_TEST_TMP when set, otherwise a fresh tempfile directory.
Exit code is non-zero if any test fails.
"""

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
TOOL = os.path.join(REPO, "tools", "aup2_fps_upconvert.py")
FIXTURES = os.path.join(REPO, "experiments", "66_to_60_validation")
FIX_30 = os.path.join(FIXTURES, "freerenderfps_30fps.aup2")
FIX_66 = os.path.join(FIXTURES, "freerenderfps_66fps.aup2")
FIX_120 = os.path.join(FIXTURES, "freerenderfps_120fps.aup2")
FIX_2112 = os.path.join(REPO, "experiments", "host_matrix", "scene_rate_test.aup2")
REAL_WORLD = r"D:\ARZv2.0.1\ARZv2.0.1\sources\試験用動画.aup2"

FAILURES = []
WORK = None


def sha256_of(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


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


def convert(in_path, out_path, factor=2, force=False, extra=()):
    args = ["--in", in_path, "--out", out_path, "--factor", str(factor)]
    if force:
        args.append("--force")
    args.extend(extra)
    return run_tool(*args)


def read_report(out_path):
    with open(out_path + ".convert.json", "r", encoding="utf-8") as fh:
        return json.load(fh)


def fixture_variant(name, edit):
    """Write an edited copy of the 30 FPS fixture into a new case dir. Returns (dir, src_path)."""
    d = case_dir(name)
    src = os.path.join(d, "src.aup2")
    write_bytes(src, edit(read_bytes(FIX_30).decode("utf-8")).encode("utf-8"))
    return d, src


def insert_effect(name, extra_line):
    """Replace the first 標準描画 effect name with `name` and add `extra_line` right after it."""
    def edit(text):
        return text.replace("effect.name=標準描画", "effect.name=%s\n%s" % (name, extra_line), 1)
    return edit


def line_number_of(path, needle):
    lines = read_bytes(path).decode("utf-8").split("\n")
    hits = [i + 1 for i, line in enumerate(lines) if needle in line]
    assert len(hits) == 1, "expected one line with %r, got %r" % (needle, hits)
    return hits[0]


def line_changes(old_bytes, new_bytes):
    """Return {line_no: (old, new)} for lines that differ; asserts structure is identical."""
    old_lines = old_bytes.split(b"\n")
    new_lines = new_bytes.split(b"\n")
    assert len(old_lines) == len(new_lines), "line count changed: %d -> %d" % (len(old_lines), len(new_lines))
    changes = {}
    for i, (a, b) in enumerate(zip(old_lines, new_lines), 1):
        if a != b:
            changes[i] = (a.decode("utf-8"), b.decode("utf-8"))
    return changes


def assert_no_output(out_path):
    assert not os.path.exists(out_path), "refused run must not write %s" % out_path
    assert not os.path.exists(out_path + ".convert.json"), "refused run must not write json"


def case_dir(name):
    path = os.path.join(WORK, name)
    os.makedirs(path)
    return path


def expect_file_line(out_path):
    return "file=" + os.path.abspath(out_path)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_30fps_x2_changes_only_expected_lines():
    d = case_dir("t30")
    out = os.path.join(d, "out30.aup2")
    code, _, err = convert(FIX_30, out, 2)
    assert code == 0, "exit %d: %s" % (code, err)
    changes = line_changes(read_bytes(FIX_30), read_bytes(out))
    expected = {
        3: ("file=D:\\AviUtl Free Render FPS\\experiments\\66_to_60_validation\\freerenderfps_30fps.aup2",
            expect_file_line(out)),
        11: ("video.rate=30", "video.rate=60"),
        36: ("frame=0,149", "frame=0,299"),
    }
    assert changes == expected, "unexpected changed lines: %r" % changes
    assert b"\r" not in read_bytes(out), "LF input must stay LF"
    assert read_bytes(out).endswith(b"\n"), "trailing newline must be preserved"


def test_2112_file_with_rate_reset_to_30_x2():
    d = case_dir("t2112")
    # Temp copies only: the real 2.1.12 fixture is never converted in place.
    as_30 = os.path.join(d, "from_30fps.aup2")
    shutil.copyfile(FIX_30, as_30)
    as_2112 = os.path.join(d, "from_2112.aup2")
    original = read_bytes(FIX_2112)
    assert b"video.rate=60\r\n" in original, "fixture layout changed"
    write_bytes(as_2112, original.replace(b"video.rate=60\r\n", b"video.rate=30\r\n", 1))

    out_30 = os.path.join(d, "out_30.aup2")
    out_2112 = os.path.join(d, "out_2112.aup2")
    code, _, err = convert(as_30, out_30, 2)
    assert code == 0, "30fps copy: exit %d: %s" % (code, err)
    code, _, err = convert(as_2112, out_2112, 2)
    assert code == 0, "2.1.12 copy: exit %d: %s" % (code, err)

    for out in (out_30, out_2112):
        text = read_bytes(out).decode("utf-8")
        assert "video.rate=60" in text, "video.rate not 60 in %s" % out
        assert "frame=0,299" in text, "frame not 0,299 in %s" % out

    out_bytes = read_bytes(out_2112)
    out_lines = out_bytes.split(b"\r\n")
    assert b"version=2011200" in out_lines, "2.1.12 version line lost"
    assert out_lines.count(b"label=") == 1, "scene label= line must pass through"
    assert out_lines.count(b"display.scene.list.label=") == 1, "project label= line must pass through"
    assert out_bytes.count(b"\r\n") == out_bytes.count(b"\n"), "CRLF not preserved"
    changes = line_changes(read_bytes(as_2112), out_bytes)
    old_contents = sorted(old.rstrip("\r") for old, _ in changes.values())
    assert len(changes) == 3, "unexpected changed lines: %r" % changes
    assert old_contents[0].startswith("file="), old_contents
    assert old_contents[1:] == ["frame=0,149", "video.rate=30"], old_contents


def test_66_and_120_fixtures_x2():
    d = case_dir("t66_120")
    out66 = os.path.join(d, "out66.aup2")
    code, _, err = convert(FIX_66, out66, 2)
    assert code == 0, "66: exit %d: %s" % (code, err)
    changes = line_changes(read_bytes(FIX_66), read_bytes(out66))
    assert set(changes) == {3, 11, 36}, "66: unexpected changed lines: %r" % changes
    assert changes[11] == ("video.rate=66", "video.rate=132")
    assert changes[36] == ("frame=0,329", "frame=0,659")  # end = (329+1)*2-1

    out120 = os.path.join(d, "out120.aup2")
    code, _, err = convert(FIX_120, out120, 2)
    assert code == 0, "120: exit %d: %s" % (code, err)
    changes = line_changes(read_bytes(FIX_120), read_bytes(out120))
    assert set(changes) == {3, 11, 36}, "120: unexpected changed lines: %r" % changes
    assert changes[11] == ("video.rate=120", "video.rate=240")
    assert changes[36] == ("frame=0,599", "frame=0,1199")  # end = (599+1)*2-1


def make_intermediate_fixture(path, frame_line):
    """30 FPS fixture with a 4-point object; every 2-value trackbar gets 4 values."""
    lines = read_bytes(FIX_30).decode("utf-8").split("\n")
    out = []
    for line in lines:
        if line == "frame=0,149":
            out.append(frame_line)
        elif line.endswith(",直線移動,0") and line.count(",") == 3:
            key, _, rest = line.partition("=")
            a, b, mode, param = rest.split(",")
            out.append("%s=%s,%s,%s,%s,%s,%s" % (key, a, a, b, b, mode, param))
        else:
            out.append(line)
    text = "\n".join(out)
    write_bytes(path, text.encode("utf-8"))


def test_intermediate_points_rescaled():
    d = case_dir("tmid")
    src = os.path.join(d, "mid.aup2")
    make_intermediate_fixture(src, "frame=0,10,20,29")
    out = os.path.join(d, "mid_out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 0, "exit %d: %s" % (code, err)
    changes = line_changes(read_bytes(src), read_bytes(out))
    frame_changes = {k: v for k, v in changes.items() if v[0].startswith("frame=")}
    assert frame_changes == {36: ("frame=0,10,20,29", "frame=0,20,40,59")}, frame_changes
    assert set(changes) <= {3, 11, 36}, "only rate/file/frame lines may change: %r" % changes


def test_intermediate_points_non_increasing_refused():
    d = case_dir("tmid_bad")
    src = os.path.join(d, "bad.aup2")
    make_intermediate_fixture(src, "frame=0,20,10,29")
    out = os.path.join(d, "bad_out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2, "expected refusal, exit %d" % code
    assert "line 36" in err and "strictly increasing" in err, err
    assert_no_output(out)


def test_real_world_read_only_check():
    if not os.path.isfile(REAL_WORLD):
        print("  SKIP real-world file not present")
        return
    before_hash = sha256_of(REAL_WORLD)
    input_dir = os.path.dirname(REAL_WORLD)
    before_listing = sorted(os.listdir(input_dir))
    d = case_dir("real")
    out = os.path.join(d, "real_x2.aup2")
    code, _, err = convert(REAL_WORLD, out, 2)
    assert sha256_of(REAL_WORLD) == before_hash, "real-world input was modified"
    assert sorted(os.listdir(input_dir)) == before_listing, "output appeared next to real-world input"
    if code == 0:
        print("  INFO real-world file converted (%s)" % os.path.basename(out))
        assert os.path.isfile(out + ".convert.json")
    else:
        assert code == 2 and "REFUSED" in err, "unexpected failure: exit %d: %s" % (code, err)
        print("  INFO real-world file refused: %s" % err.strip())
        assert_no_output(out)


def test_refuse_unknown_scene_frame_key():
    d = case_dir("ref_scene_key")
    src = os.path.join(d, "src.aup2")
    text = read_bytes(FIX_30).decode("utf-8").replace("video.rate=30\n", "video.rate=30\nkeyframe_hint=1\n", 1)
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2, "exit %d" % code
    assert "keyframe_hint" in err and "line 12" in err, err
    assert_no_output(out)


def test_refuse_script_effect():
    d = case_dir("ref_script")
    src = os.path.join(d, "src.aup2")
    text = read_bytes(FIX_30).decode("utf-8").replace("Y=0.00,0.00,直線移動,0", "Y=obj.frame*2", 1)
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2, "exit %d" % code
    assert "script text" in err, err
    assert_no_output(out)


def test_refuse_counter_effect():
    d = case_dir("ref_counter")
    src = os.path.join(d, "src.aup2")
    text = read_bytes(FIX_30).decode("utf-8").replace("effect.name=標準描画", "effect.name=カウンター", 1)
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2, "exit %d" % code
    assert "カウンター" in err, err
    assert_no_output(out)


def test_refuse_koma_ochi_flag():
    d = case_dir("ref_koma")
    src = os.path.join(d, "src.aup2")
    text = read_bytes(FIX_30).decode("utf-8").replace("Group=1\n", "Group=1\nコマ落ち=1\n", 1)
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2, "exit %d" % code
    assert "コマ落ち=1" in err, err
    assert_no_output(out)


def test_refuse_unknown_trackbar_mode_and_pipe_payload():
    d = case_dir("ref_track")
    src = os.path.join(d, "mode.aup2")
    text = read_bytes(FIX_30).decode("utf-8").replace("X=-200.00,200.00,直線移動,0", "X=-200.00,200.00,謎移動,0", 1)
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "mode_out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2 and "謎移動" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)

    src = os.path.join(d, "pipe.aup2")
    text = read_bytes(FIX_30).decode("utf-8").replace("X=-200.00,200.00,直線移動,0", "X=-200.00,200.00,直線移動,0|27", 1)
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "pipe_out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2 and "'|' payload" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_refuse_out_exists_without_force():
    d = case_dir("ref_exists")
    out = os.path.join(d, "existing.aup2")
    write_bytes(out, b"keep me")
    code, _, err = convert(FIX_30, out, 2)
    assert code == 2 and "exists" in err, "exit %d: %s" % (code, err)
    assert read_bytes(out) == b"keep me", "existing output was overwritten"
    assert not os.path.exists(out + ".convert.json")

    code, _, err = convert(FIX_30, out, 2, force=True)
    assert code == 0, "--force: exit %d: %s" % (code, err)
    assert read_bytes(out) != b"keep me", "--force did not overwrite"


def test_refuse_out_equals_in():
    d = case_dir("ref_same")
    src = os.path.join(d, "same.aup2")
    shutil.copyfile(FIX_30, src)
    before = sha256_of(src)
    code, _, err = convert(src, src, 2, force=True)
    assert code == 2, "exit %d: %s" % (code, err)
    assert sha256_of(src) == before, "input modified"


def test_refuse_factor_one():
    d = case_dir("ref_k1")
    out = os.path.join(d, "k1.aup2")
    code, _, err = convert(FIX_30, out, 1)
    assert code == 2, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_input_never_modified_by_conversion():
    d = case_dir("ro")
    before = sha256_of(FIX_30)
    out = os.path.join(d, "ro.aup2")
    code, _, _ = convert(FIX_30, out, 2)
    assert code == 0
    assert sha256_of(FIX_30) == before, "input fixture was modified"


COUNTER_NOTE = "カウンター: time-based (速度 = counts per second), passed through unchanged (lines %d)"


def test_counter_passes_through_and_is_noted():
    d, src = fixture_variant("cnt_ok", insert_effect("カウンター", "速度=1.0"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 0, "exit %d: %s" % (code, err)
    changes = line_changes(read_bytes(src), read_bytes(out))
    assert set(changes) == {3, 11, 36}, "counter must not change: %r" % changes
    counter_line = line_number_of(src, "effect.name=カウンター")
    assert read_report(out)["notes"] == [COUNTER_NOTE % counter_line], read_report(out)["notes"]


def test_counter_non_numeric_speed_refused():
    d, src = fixture_variant("cnt_bad", insert_effect("カウンター", "速度=abc"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2 and "速度" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_text_plain_words_outside_script_pass():
    d, src = fixture_variant("txt_plain", insert_effect("テキスト", "テキスト=random rand obj.x"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 0, "exit %d: %s" % (code, err)
    assert set(line_changes(read_bytes(src), read_bytes(out))) == {3, 11, 36}
    assert read_report(out)["notes"] == [] and "WARNING" not in err


def test_text_time_based_script_passes():
    d, src = fixture_variant("txt_time", insert_effect(
        "テキスト", "テキスト=<?obj.rz=obj.time*360+obj.totaltime+obj.getvalue(\"rz\")?>"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 0 and "WARNING" not in err, "exit %d: %s" % (code, err)
    assert set(line_changes(read_bytes(src), read_bytes(out))) == {3, 11, 36}


def test_text_frame_script_refused_by_default():
    d, src = fixture_variant("txt_frame", insert_effect("テキスト", "テキスト=<?=obj.frame?>"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    line = line_number_of(src, "テキスト=<?=obj.frame?>")
    assert code == 2 and "line %d" % line in err and "obj.frame" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_text_frame_script_allowed_with_warning():
    def edit(text):
        text = text.replace("effect.name=標準描画", "effect.name=テキスト\nテキスト=<?=obj.frame?>", 1)
        return text.replace("effect.name=図形", "effect.name=テキスト\nテキスト=<?rand(1,10)?>", 1)
    d, src = fixture_variant("txt_allow", edit)
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2, extra=["--allow-frame-scripts"])
    assert code == 0, "exit %d: %s" % (code, err)
    line_frame = line_number_of(src, "テキスト=<?=obj.frame?>")
    line_rand = line_number_of(src, "テキスト=<?rand(1,10)?>")
    assert "WARNING: line %d:" % line_frame in err and "'obj.frame'" in err, err
    assert "WARNING: line %d:" % line_rand in err and "'rand('" in err, err
    assert err.count("WARNING: line") == 2, err
    assert "per-frame random sequences change" in err and "2 times faster" in err, err
    notes = read_report(out)["notes"]
    warn_notes = [n for n in notes if n.startswith("WARNING: ")]
    assert len(warn_notes) == 2, notes
    assert any(n.startswith("WARNING: line %d:" % line_frame) for n in warn_notes), warn_notes
    assert any(n.startswith("WARNING: line %d:" % line_rand) for n in warn_notes), warn_notes
    assert set(line_changes(read_bytes(src), read_bytes(out))) == {3, 11, 36}


def test_text_rand_call_in_script_refused():
    d, src = fixture_variant("txt_rand", insert_effect("テキスト", "テキスト=<?rand(1,10)?>"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2 and "rand(" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_text_math_random_refused_by_default_and_warned_when_allowed():
    d, src = fixture_variant("txt_math", insert_effect("テキスト", "テキスト=<?=math.random(1,9)?>"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    line = line_number_of(src, "テキスト=<?=math.random(1,9)?>")
    assert code == 2 and "line %d" % line in err and "math.random(" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)

    out = os.path.join(d, "out_allow.aup2")
    code, _, err = convert(src, out, 2, extra=["--allow-frame-scripts"])
    assert code == 0, "exit %d: %s" % (code, err)
    assert "WARNING: line %d:" % line in err and "'math.random('" in err, err
    notes = read_report(out)["notes"]
    assert any(n.startswith("WARNING: line %d:" % line) for n in notes), notes


def test_text_escaped_newline_inside_script_is_scanned():
    d, src = fixture_variant("txt_nl", insert_effect("テキスト", "テキスト=<?\\nobj.frame?>"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2, "escaped newline before obj.frame must still refuse: exit %d: %s" % (code, err)
    assert_no_output(out)

    d, src = fixture_variant("txt_nl_unterminated", insert_effect("テキスト", "テキスト=<?obj.frame"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2, "unterminated <? script must still be scanned: exit %d: %s" % (code, err)

    d, src = fixture_variant("txt_nl_plain", insert_effect("テキスト", "テキスト=plain\\nobj.frame rand"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 0, "escaped newline outside script must pass: exit %d: %s" % (code, err)


KIND_BY_SECTION_KEY = {
    ("project", "file"): "project file=",
    ("scene", "video.rate"): "scene video.rate=",
    ("scene", "cursor.frame"): "scene frame-unit key",
    ("scene", "preview.frame"): "scene frame-unit key",
    ("scene", "display.frame"): "scene frame-unit key",
    ("object", "frame"): "object frame=",
}


def classify_changed_lines(old_bytes, changes):
    """Return {kind: count} for changed lines. Fails on any change outside KIND_BY_SECTION_KEY."""
    section = None
    sections = []
    for line in old_bytes.decode("utf-8").split("\n"):
        line = line.rstrip("\r")
        if line.startswith("[") and line.endswith("]"):
            if line == "[project]":
                section = "project"
            elif line.startswith("[scene."):
                section = "scene"
            elif line[1:-1].isdigit():
                section = "object"
            else:
                section = "effect"
        sections.append((section, line))
    counts = {}
    for line_no, (old, _) in sorted(changes.items()):
        section, body = sections[line_no - 1]
        key = body.partition("=")[0]
        kind = KIND_BY_SECTION_KEY.get((section, key))
        assert kind is not None, "unexpected change at line %d (%s): %r" % (line_no, section, body)
        counts[kind] = counts.get(kind, 0) + 1
    return counts


def test_real_world_changed_lines_by_kind():
    if not os.path.isfile(REAL_WORLD):
        print("  SKIP real-world file not present")
        return
    before_hash = sha256_of(REAL_WORLD)
    old = read_bytes(REAL_WORLD)
    d = case_dir("real_kinds")
    for factor in (2, 3):
        out = os.path.join(d, "real_x%d.aup2" % factor)
        code, _, err = convert(REAL_WORLD, out, factor)
        assert code == 0, "factor %d: exit %d: %s" % (factor, code, err)
        assert sha256_of(REAL_WORLD) == before_hash, "real-world input was modified"
        report = read_report(out)
        changes = line_changes(old, read_bytes(out))
        assert {c["line"] for c in report["changes"]} == set(changes), "json changes disagree with output"
        kinds = classify_changed_lines(old, changes)
        assert set(kinds) <= set(KIND_BY_SECTION_KEY.values()), kinds
        assert kinds.get("project file=") == 1, kinds
        assert any(n.startswith("カウンター: time-based") for n in report["notes"]), report["notes"]
        print("  INFO factor %d: %d changed lines, by kind: %s" % (factor, len(changes), kinds))


TESTS = [
    test_30fps_x2_changes_only_expected_lines,
    test_2112_file_with_rate_reset_to_30_x2,
    test_66_and_120_fixtures_x2,
    test_intermediate_points_rescaled,
    test_intermediate_points_non_increasing_refused,
    test_refuse_unknown_scene_frame_key,
    test_refuse_script_effect,
    test_refuse_counter_effect,
    test_refuse_koma_ochi_flag,
    test_refuse_unknown_trackbar_mode_and_pipe_payload,
    test_refuse_out_exists_without_force,
    test_refuse_out_equals_in,
    test_refuse_factor_one,
    test_input_never_modified_by_conversion,
    test_real_world_read_only_check,
    test_counter_passes_through_and_is_noted,
    test_counter_non_numeric_speed_refused,
    test_text_plain_words_outside_script_pass,
    test_text_time_based_script_passes,
    test_text_frame_script_refused_by_default,
    test_text_frame_script_allowed_with_warning,
    test_text_rand_call_in_script_refused,
    test_text_escaped_newline_inside_script_is_scanned,
    test_text_math_random_refused_by_default_and_warned_when_allowed,
    test_real_world_changed_lines_by_kind,
]


def main():
    global WORK
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    base = os.environ.get("AUP2_TEST_TMP")
    if base:
        os.makedirs(base, exist_ok=True)
    WORK = tempfile.mkdtemp(prefix="aup2_upconvert_", dir=base)
    try:
        for fixture in (FIX_30, FIX_66, FIX_120, FIX_2112, TOOL):
            if not os.path.isfile(fixture):
                print("FAIL missing fixture %s" % fixture)
                return 1
        for test in TESTS:
            name = test.__name__
            try:
                test()
            except Exception:
                FAILURES.append(name)
                print("FAIL %s" % name)
                traceback.print_exc()
            else:
                print("PASS %s" % name)
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
    print("%d/%d passed" % (len(TESTS) - len(FAILURES), len(TESTS)))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
