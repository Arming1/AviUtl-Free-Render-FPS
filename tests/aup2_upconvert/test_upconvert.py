"""Tests for tools/aup2_fps_upconvert.py.

Run with: python -I tests/aup2_upconvert/test_upconvert.py   (or run.ps1 next to this file)
Temporary files go to $AUP2_TEST_TMP when set, otherwise a fresh tempfile directory.
Exit code is non-zero if any test fails.
"""

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import traceback
from fractions import Fraction

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


def write_with_eol(path, lf_text, eol):
    """Write LF-normalised text with `eol` ("\n" or "\r\n") as the line ending of every line."""
    write_bytes(path, lf_text.replace("\n", eol).encode("utf-8"))


def eol_style(data):
    """'crlf' when every newline is CRLF, 'lf' when none is, 'mixed' otherwise."""
    crlf = data.count(b"\r\n")
    if crlf == 0:
        return "lf"
    return "crlf" if crlf == data.count(b"\n") else "mixed"


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


def convert_target(in_path, out_path, target, force=False, extra=()):
    args = ["--in", in_path, "--out", out_path, "--target-fps", target]
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
    write_bytes(src, edit(read_text_lf(FIX_30)).encode("utf-8"))
    return d, src


def insert_effect(name, extra_line):
    """Replace the first 標準描画 effect name with `name` and add `extra_line` right after it."""
    def edit(text):
        return text.replace("effect.name=標準描画", "effect.name=%s\n%s" % (name, extra_line), 1)
    return edit


def line_number_of(path, needle):
    lines = read_text_lf(path).split("\n")
    hits = [i + 1 for i, line in enumerate(lines) if needle in line]
    assert len(hits) == 1, "expected one line with %r, got %r" % (needle, hits)
    return hits[0]


def line_changes(old_bytes, new_bytes):
    """Return {line_no: (old, new)} for lines whose text differs (CR removed); asserts the line count is the same
    and every line keeps its own line ending (LF stays LF, CRLF stays CRLF)."""
    old_lines = old_bytes.split(b"\n")
    new_lines = new_bytes.split(b"\n")
    assert len(old_lines) == len(new_lines), "line count changed: %d -> %d" % (len(old_lines), len(new_lines))
    changes = {}
    for i, (a, b) in enumerate(zip(old_lines, new_lines), 1):
        assert a.endswith(b"\r") == b.endswith(b"\r"), "line %d: line ending changed: %r -> %r" % (i, a, b)
        a, b = a.rstrip(b"\r"), b.rstrip(b"\r")
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
    # Both newline styles: the git index holds LF, a Windows checkout with core.autocrlf holds CRLF.
    for eol, style in (("\n", "lf"), ("\r\n", "crlf")):
        d = case_dir("t30_" + style)
        src = os.path.join(d, "in.aup2")
        write_with_eol(src, read_text_lf(FIX_30), eol)
        out = os.path.join(d, "out30.aup2")
        code, _, err = convert(src, out, 2)
        assert code == 0, "%s: exit %d: %s" % (style, code, err)
        changes = line_changes(read_bytes(src), read_bytes(out))
        expected = {
            3: ("file=D:\\AviUtl Free Render FPS\\experiments\\66_to_60_validation\\freerenderfps_30fps.aup2",
                expect_file_line(out)),
            11: ("video.rate=30", "video.rate=60"),
            36: ("frame=0,149", "frame=0,299"),
        }
        assert changes == expected, "%s: unexpected changed lines: %r" % (style, changes)
        assert eol_style(read_bytes(out)) == style, "%s: output must keep the input line endings" % style
        assert read_bytes(out).endswith(eol.encode("ascii")), "trailing newline must be preserved"


def test_2112_file_with_rate_reset_to_30_x2():
    d = case_dir("t2112")
    # Temp copies only: the real 2.1.12 fixture is never converted in place.
    as_30 = os.path.join(d, "from_30fps.aup2")
    shutil.copyfile(FIX_30, as_30)
    as_2112 = os.path.join(d, "from_2112.aup2")
    original = read_text_lf(FIX_2112)
    assert "video.rate=60\n" in original, "fixture layout changed"
    write_with_eol(as_2112, original.replace("video.rate=60\n", "video.rate=30\n", 1), "\r\n")  # AviUtl2 writes CRLF

    out_30 = os.path.join(d, "out_30.aup2")
    out_2112 = os.path.join(d, "out_2112.aup2")
    code, _, err = convert(as_30, out_30, 2)
    assert code == 0, "30fps copy: exit %d: %s" % (code, err)
    code, _, err = convert(as_2112, out_2112, 2)
    assert code == 0, "2.1.12 copy: exit %d: %s" % (code, err)

    for out in (out_30, out_2112):
        text = read_text_lf(out)
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


def make_intermediate_fixture(path, frame_line, base=FIX_30):
    """Fixture with a 4-point object; every 2-value trackbar gets 4 values."""
    lines = read_text_lf(base).split("\n")
    out = []
    for line in lines:
        if line.startswith("frame=0,"):
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
    text = read_text_lf(FIX_30).replace("video.rate=30\n", "video.rate=30\nkeyframe_hint=1\n", 1)
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2, "exit %d" % code
    assert "keyframe_hint" in err and "line 12" in err, err
    assert_no_output(out)


def test_refuse_script_effect():
    d = case_dir("ref_script")
    src = os.path.join(d, "src.aup2")
    text = read_text_lf(FIX_30).replace("Y=0.00,0.00,直線移動,0", "Y=obj.frame*2", 1)
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2, "exit %d" % code
    assert "script text" in err, err
    assert_no_output(out)


def test_refuse_counter_effect():
    d = case_dir("ref_counter")
    src = os.path.join(d, "src.aup2")
    text = read_text_lf(FIX_30).replace("effect.name=標準描画", "effect.name=カウンター", 1)
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2, "exit %d" % code
    assert "カウンター" in err, err
    assert_no_output(out)


def test_koma_ochi_one_passes_unchanged():
    d = case_dir("koma_one")
    src = os.path.join(d, "src.aup2")
    text = read_text_lf(FIX_30).replace("Group=1\n", "Group=1\nコマ落ち=1\n", 1)
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 0, "exit %d: %s" % (code, err)
    lines = read_text_lf(out).split("\n")
    i = lines.index("Group=1")
    assert lines[i + 1] == "コマ落ち=1", lines[i:i + 3]
    assert lines.count("コマ落ち=1") == 1, "コマ落ち=1 must appear once and unchanged"


def test_refuse_koma_ochi_not_a_positive_integer():
    # コマ落ち is an integer: 1 stays, 2 or more is converted only in a time control effect (see the 時間制御 tests);
    # zero, negative, decimal and trackbar-style values are refused wherever they are.
    values = ["0", "-1", "1.5", "0,10,直線移動,0"]
    for n, value in enumerate(values):
        d = case_dir("koma_refuse_%d" % n)
        src = os.path.join(d, "src.aup2")
        text = read_text_lf(FIX_30).replace("Group=1\n", "Group=1\nコマ落ち=%s\n" % value, 1)
        write_bytes(src, text.encode("utf-8"))
        line_no = text[:text.index("コマ落ち=")].count("\n") + 1
        out = os.path.join(d, "out.aup2")
        code, _, err = convert(src, out, 2)
        expected = "REFUSED: line %d: コマ落ち=%s (frame drop) must be a positive integer" % (line_no, value)
        assert code == 2, "exit %d" % code
        assert expected in err, err
        assert_no_output(out)


def test_refuse_unknown_trackbar_mode_and_pipe_payload():
    d = case_dir("ref_track")
    src = os.path.join(d, "mode.aup2")
    text = read_text_lf(FIX_30).replace("X=-200.00,200.00,直線移動,0", "X=-200.00,200.00,謎移動,0", 1)
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "mode_out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 2 and "謎移動" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)

    src = os.path.join(d, "pipe.aup2")
    text = read_text_lf(FIX_30).replace("X=-200.00,200.00,直線移動,0", "X=-200.00,200.00,瞬間移動,0|27", 1)
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
    ("scene", "video.scale"): "scene video.scale=",
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


# ---------------------------------------------------------------------------
# --target-fps: exact rational retiming
# ---------------------------------------------------------------------------

RE_OBJ_SECTION = re.compile(r"^\[(\d+)\]$")
RE_SHORT_NOTE = re.compile(r"^object at line (\d+) shortened to 1 frame$")
TARGETS = ("66", "60", "60000/1001", "24", "120", "59")


def parse_rate(text):
    """Exact scene rate (first video.rate / video.scale in the file) as a Fraction."""
    rate = scale = None
    for raw in text.split("\n"):
        line = raw.rstrip("\r")
        if rate is None and line.startswith("video.rate="):
            rate = int(line.partition("=")[2])
        elif scale is None and line.startswith("video.scale="):
            scale = int(line.partition("=")[2])
    return Fraction(rate, 1 if scale is None else scale)


def parse_objects(text):
    """{obj_no: {"layer", "frames", "line"}} for each object section that has a frame= line."""
    objects = {}
    section = None
    for number, raw in enumerate(text.split("\n"), 1):
        line = raw.rstrip("\r")
        if line.startswith("[") and line.endswith("]"):
            match = RE_OBJ_SECTION.match(line)
            section = int(match.group(1)) if match else None
            continue
        if section is None or "=" not in line:
            continue
        key, _, value = line.partition("=")
        if key == "layer":
            objects.setdefault(section, {})["layer"] = value
        elif key == "frame":
            entry = objects.setdefault(section, {})
            entry["frames"] = [int(x) for x in value.split(",")]
            entry["line"] = number
    return {no: obj for no, obj in objects.items() if "frames" in obj}


def shortened_lines(notes):
    found = set()
    for note in notes:
        match = RE_SHORT_NOTE.match(note)
        if match:
            found.add(int(match.group(1)))
    return found


def synthetic_project(name, base, rate, objects):
    """Write a project into a new case dir: the base fixture header with video.rate=rate, then one minimal
    object section per (layer, start, end). Returns (dir, src_path)."""
    text = read_text_lf(base)
    header, marker, _ = text.partition("[0]\n")
    assert marker, "fixture layout changed"
    header = re.sub(r"^video\.rate=\d+$", "video.rate=%d" % rate, header, flags=re.M)
    body = "".join("[%d]\nlayer=%d\nfocus=0\nframe=%d,%d\n" % (no, layer, start, end)
                   for no, (layer, start, end) in enumerate(objects))
    d = case_dir(name)
    src = os.path.join(d, "src.aup2")
    write_bytes(src, (header + body).encode("utf-8"))
    return d, src


def timing_layout(rate):
    """Objects (layer, start, end) for a synthetic project at `rate`.

    Layer 0: four touching pieces of about 0.4 s, three 1-frame objects about 0.5 s apart, then one
    long piece. Layer 1: one object spanning the whole timeline. Gaps and pieces scale with the rate.
    """
    piece = max(2, rate * 2 // 5)
    gap = max(2, rate // 2)
    objects = []
    start = 0
    for _ in range(4):
        objects.append((0, start, start + piece - 1))
        start += piece
    for _ in range(3):
        start += gap
        objects.append((0, start, start))
        start += 1
    start += gap
    objects.append((0, start, start + piece - 1))
    objects.append((1, 0, start + piece - 1))
    return objects


def check_retime_properties(src_path, out_path, report, label):
    """Check timing rules (a) to (d) using exact rates read from the two files.

    Returns (max start error, max end error) in seconds as Fractions. A 1-frame object that was shortened
    (listed in the notes) may have an end error up to 3/(2*Rt); every other boundary is within 1/(2*Rt).
    """
    src_text = read_text_lf(src_path)
    out_text = read_text_lf(out_path)
    rate_src, rate_dst = parse_rate(src_text), parse_rate(out_text)
    half = Fraction(1, 2) / rate_dst
    src_objs, out_objs = parse_objects(src_text), parse_objects(out_text)
    short = shortened_lines(report["notes"])
    assert set(src_objs) == set(out_objs), "%s: objects added or lost" % label
    max_start = max_end = Fraction(0)
    for no, src_obj in src_objs.items():
        out_obj = out_objs[no]
        line = src_obj["line"]
        assert len(src_obj["frames"]) == 2 and len(out_obj["frames"]) == 2, "%s line %d: not 2-valued" % (label, line)
        s, e = src_obj["frames"]
        s2, e2 = out_obj["frames"]
        assert e2 >= s2, "%s line %d: object vanished (%d,%d)" % (label, line, s2, e2)
        start_err = abs(Fraction(s2) / rate_dst - Fraction(s) / rate_src)
        end_err = abs(Fraction(e2 + 1) / rate_dst - Fraction(e + 1) / rate_src)
        assert start_err <= half, "%s line %d: start error %s s > 1/(2Rt)" % (label, line, start_err)
        end_limit = Fraction(3, 2) / rate_dst if line in short else half
        assert end_err <= end_limit, "%s line %d: end error %s s > %s s" % (label, line, end_err, end_limit)
        max_start = max(max_start, start_err)
        max_end = max(max_end, end_err)
    for a_no, a in src_objs.items():
        for b_no, b in src_objs.items():
            if a.get("layer") == b.get("layer") and a["frames"][1] + 1 == b["frames"][0]:
                ae = out_objs[a_no]["frames"][1]
                bs = out_objs[b_no]["frames"][0]
                assert ae + 1 == bs, "%s: touching lines %d and %d no longer touch (%d, %d)" % (
                    label, a["line"], b["line"], ae, bs)
    timeline_src = max(Fraction(o["frames"][1] + 1) / rate_src for o in src_objs.values())
    timeline_dst = max(Fraction(o["frames"][1] + 1) / rate_dst for o in out_objs.values())
    assert abs(timeline_dst - timeline_src) <= 1 / rate_dst, "%s: timeline end moved by more than 1/Rt" % label
    return max_start, max_end


def test_target_fps_exact_values_30fps_fixture():
    d = case_dir("tfps_30")
    out66 = os.path.join(d, "t66.aup2")
    code, _, err = convert_target(FIX_30, out66, "66")
    assert code == 0, "66: exit %d: %s" % (code, err)
    changes = line_changes(read_bytes(FIX_30), read_bytes(out66))
    assert set(changes) == {3, 11, 36}, "66: unexpected changed lines: %r" % changes
    assert changes[11] == ("video.rate=30", "video.rate=66")
    assert changes[36] == ("frame=0,149", "frame=0,329"), changes[36]

    out_ntsc = os.path.join(d, "t5994.aup2")
    code, _, err = convert_target(FIX_30, out_ntsc, "60000/1001")
    assert code == 0, "60000/1001: exit %d: %s" % (code, err)
    changes = line_changes(read_bytes(FIX_30), read_bytes(out_ntsc))
    assert set(changes) == {3, 11, 12, 36}, "60000/1001: unexpected changed lines: %r" % changes
    assert changes[11] == ("video.rate=30", "video.rate=60000")
    assert changes[12] == ("video.scale=1", "video.scale=1001")
    assert changes[36] == ("frame=0,149", "frame=0,299"), changes[36]
    report = read_report(out_ntsc)
    assert report["k"] == "2000/1001" and report["source_rate"] == "30/1", report
    assert report["target_rate"] == "60000/1001", report
    # end: 300 / (60000/1001) - 150 / 30 = 1/200 s exactly
    assert report["timing"]["max_end_error_s"] == "1/200", report["timing"]
    assert report["timing"]["max_start_error_s"] == "0", report["timing"]


def test_target_fps_synthetic_60_to_66_and_72_to_60():
    d, src = synthetic_project("tfps_60_66", FIX_30, 60, [(0, 0, 59), (0, 60, 119)])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "66")
    assert code == 0, "60->66: exit %d: %s" % (code, err)
    text = read_text_lf(out)
    objs = parse_objects(text)
    assert objs[0]["frames"] == [0, 65], objs
    assert objs[1]["frames"] == [66, 131], objs
    assert parse_rate(text) == Fraction(66)

    d, src = synthetic_project("tfps_72_60", FIX_30, 72, [(0, 0, 71)])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 0, "72->60: exit %d: %s" % (code, err)
    objs = parse_objects(read_text_lf(out))
    assert objs[0]["frames"] == [0, 59], objs


def test_target_fps_shortens_one_frame_object_and_notes_it():
    d = case_dir("tfps_short_120")
    src = os.path.join(d, "src120.aup2")
    write_bytes(src, read_text_lf(FIX_120).replace("frame=0,599", "frame=3,3", 1).encode("utf-8"))
    out = os.path.join(d, "out24.aup2")
    code, _, err = convert_target(src, out, "24")
    assert code == 0, "exit %d: %s" % (code, err)
    line = line_number_of(src, "frame=3,3")
    assert parse_objects(read_text_lf(out))[0]["frames"] == [1, 1]
    notes = read_report(out)["notes"]
    assert notes == ["object at line %d shortened to 1 frame" % line], notes
    assert read_report(out)["timing"]["objects"][0]["shortened"] is True


def test_target_fps_intermediate_collision_refused_with_line():
    d = case_dir("tfps_collide")
    src = os.path.join(d, "collide.aup2")
    make_intermediate_fixture(src, "frame=0,1,2,599", base=FIX_120)
    out = os.path.join(d, "collide_out.aup2")
    code, _, err = convert_target(src, out, "24")
    assert code == 2, "expected refusal, exit %d: %s" % (code, err)
    assert "line 36" in err and "collide" in err, err
    assert_no_output(out)


def test_target_fps_touching_neighbour_refused_when_shortened():
    d, src = synthetic_project("tfps_touch_refuse", FIX_120, 120, [(0, 0, 0), (0, 1, 1)])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "24")
    line = line_number_of(src, "frame=1,1")
    assert code == 2 and "line %d" % line in err and "touch" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_target_fps_rejections():
    d = case_dir("tfps_reject")
    out = os.path.join(d, "out.aup2")
    code, _, err = run_tool("--in", FIX_30, "--out", out, "--target-fps", "59.94")
    assert code == 2 and "59.94" in err and "decimal" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)

    code, _, err = run_tool("--in", FIX_30, "--out", out, "--target-fps", "0")
    assert code == 2 and "positive" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)

    code, _, err = run_tool("--in", FIX_30, "--out", out, "--factor", "2", "--target-fps", "66")
    assert code == 2 and "not allowed" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)

    code, _, err = run_tool("--in", FIX_30, "--out", out)
    assert code == 2 and "required" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_target_fps_inserts_missing_video_scale():
    text = read_text_lf(FIX_30).replace("video.scale=1\n", "", 1)
    d = case_dir("tfps_noscale")
    src = os.path.join(d, "noscale.aup2")
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60000/1001")
    assert code == 0, "exit %d: %s" % (code, err)
    out_text = read_text_lf(out)
    assert "video.rate=60000\nvideo.scale=1001\n" in out_text, out_text[:400]
    assert parse_rate(out_text) == Fraction(60000, 1001)
    assert any("has no video.scale" in n for n in read_report(out)["notes"]), read_report(out)["notes"]


def test_retime_timing_properties():
    sources = []
    for label, base, rate in (("30", FIX_30, 30), ("66", FIX_66, 66), ("120", FIX_120, 120)):
        sources.append(("fix%s" % label, base))
        _, syn = synthetic_project("syn%s" % label, base, rate, timing_layout(rate))
        sources.append(("syn%s" % label, syn))
    for label, src in sources:
        d_out = case_dir("out_" + label)
        for target in TARGETS:
            out = os.path.join(d_out, "out_%s.aup2" % target.replace("/", "_"))
            code, _, err = convert_target(src, out, target)
            assert code == 0, "%s -> %s: exit %d: %s" % (label, target, code, err)
            max_start, max_end = check_retime_properties(src, out, read_report(out), "%s->%s" % (label, target))
            print("  INFO %s -> %s: max start %.4f ms, max end %.4f ms, shortened %d" % (
                label, target, float(max_start) * 1000, float(max_end) * 1000,
                len(shortened_lines(read_report(out)["notes"]))))


def scene_project(name, rate, objects):
    """Write a project into a new case dir with one [scene.N] section per scene in `objects`.

    `objects` is a list of (scene, layer, start, end). Each object section sits under its scene's header and
    carries a scene= line, as in AviUtl2 files. Returns (dir, src_path).
    """
    d = case_dir(name)
    src = os.path.join(d, "src.aup2")
    parts = ["[project]\nversion=2010400\nfile=synthetic.aup2\ndisplay.scene=0\npreview.scene=0\n"]
    for scene in sorted({obj[0] for obj in objects}):
        parts.append("[scene.%d]\nscene=%d\nname=scene %d\nvideo.width=640\nvideo.height=360\nvideo.rate=%d\n"
                     "video.scale=1\naudio.rate=44100\ncursor.frame=0\ncursor.layer=0\npreview.frame=0\n"
                     "display.frame=0\ndisplay.layer=0\n" % (scene, scene, scene, rate))
        for no, (obj_scene, layer, start, end) in enumerate(objects):
            if obj_scene == scene:
                parts.append("[%d]\nlayer=%d\nscene=%d\nfocus=0\nframe=%d,%d\n" % (no, layer, scene, start, end))
    write_bytes(src, "".join(parts).encode("utf-8"))
    return d, src


def test_target_fps_same_layer_in_two_scenes_is_not_grouped():
    d, src = scene_project("tfps_two_scenes", 60, [(0, 0, 0, 59), (1, 0, 0, 59)])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "66")
    assert code == 0 and "REFUSED" not in err, "exit %d: %s" % (code, err)
    text = read_text_lf(out)
    objs = parse_objects(text)
    assert objs[0]["frames"] == [0, 65] and objs[1]["frames"] == [0, 65], objs
    assert read_report(out)["notes"] == [], read_report(out)["notes"]


def test_target_fps_touching_across_scenes_is_not_checked():
    # Under one timeline these two 1-frame objects would touch and the first would be shortened (refused).
    # In different scenes they are independent, so both convert and both shortened objects are noted.
    d, src = scene_project("tfps_two_scenes_short", 120, [(0, 0, 0, 0), (1, 0, 1, 1)])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "24")
    assert code == 0, "exit %d: %s" % (code, err)
    objs = parse_objects(read_text_lf(out))
    assert objs[0]["frames"] == [0, 0] and objs[1]["frames"] == [0, 0], objs
    notes = read_report(out)["notes"]
    assert sorted(notes) == sorted(["object at line %d shortened to 1 frame" % line
                                    for line in (objs[0]["line"], objs[1]["line"])]), notes


def test_target_fps_same_scene_overlap_after_rescaling_refused():
    d, src = scene_project("tfps_same_scene", 120, [(0, 0, 0, 0), (0, 0, 2, 2)])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "24")
    line = line_number_of(src, "frame=2,2")
    assert code == 2 and "line %d" % line in err and "overlaps" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_real_world_target_fps_66_and_50():
    if not os.path.isfile(REAL_WORLD):
        print("  SKIP real-world file not present")
        return
    before_hash = sha256_of(REAL_WORLD)
    input_dir = os.path.dirname(REAL_WORLD)
    before_listing = sorted(os.listdir(input_dir))
    old = read_bytes(REAL_WORLD)
    d = case_dir("real_tfps")
    for target in ("66", "50"):
        out = os.path.join(d, "real_%s.aup2" % target.replace("/", "_"))
        code, _, err = convert_target(REAL_WORLD, out, target)
        assert sha256_of(REAL_WORLD) == before_hash, "real-world input was modified"
        assert sorted(os.listdir(input_dir)) == before_listing, "output appeared next to real-world input"
        assert code == 0, "target %s: exit %d: %s" % (target, code, err)
        report = read_report(out)
        changes = line_changes(old, read_bytes(out))
        assert {c["line"] for c in report["changes"]} == set(changes), "json changes disagree with output"
        kinds = classify_changed_lines(old, changes)
        assert set(kinds) <= set(KIND_BY_SECTION_KEY.values()), kinds
        assert kinds.get("project file=") == 1, kinds
        assert parse_rate(read_text_lf(out)) == Fraction(target)
        max_start, max_end = check_retime_properties(REAL_WORLD, out, report, "real->%s" % target)
        print("  INFO real-world -> %s: %d changed lines, kinds %s" % (target, len(changes), kinds))
        print("  INFO real-world -> %s: max start %.4f ms, max end %.4f ms (report: %s / %s)" % (
            target, float(max_start) * 1000, float(max_end) * 1000,
            report["timing"]["max_start_error_s"], report["timing"]["max_end_error_s"]))


# ---------------------------------------------------------------------------
# --precise: trackbar values resampled from the exact original curve
# ---------------------------------------------------------------------------

RE_TRACK_LINE = re.compile(r"^-?\d+(?:\.\d+)?(?:,-?\d+(?:\.\d+)?)+,[^,]+,[^,]*$")
RE_ANY_SECTION = re.compile(r"^\[(\d+)(?:\.\d+)?\]$")
PRECISE_TARGETS = ("66", "24", "60000/1001", "50", "59", "120")
# Multi-point objects whose X keyframes lie on one straight line (no kinks): (name, base, frames, X values).
COLLINEAR_OBJECTS = (
    ("syn30", FIX_30, [0, 30, 90, 149], ["-200.00", "-188.00", "-164.00", "-140.40"]),
    ("syn66", FIX_66, [0, 50, 200, 329], ["-200.00", "-180.00", "-120.00", "-68.40"]),
    ("syn120", FIX_120, [0, 100, 300, 599], ["-200.00", "-160.00", "-80.00", "39.60"]),
)
# Tiny slack on top of the quantization bound (the checks themselves are exact rationals).
TOLERANCE = Fraction(1, 10 ** 9)


def read_text_lf(path):
    """Fixture text with CRLF turned into LF, so the tests can match whole LF-terminated lines."""
    return read_bytes(path).decode("utf-8").replace("\r\n", "\n")


def without_file_line(data):
    """Bytes with the [project] file= line removed (it holds the run's own output path)."""
    return b"\n".join(line for line in data.split(b"\n") if not line.startswith(b"file="))


def tracks_by_object(text):
    """{obj_no: {key: [value texts..., mode, param]}} for the trackbar lines of each object and its effects."""
    found = {}
    section = None
    for raw in text.split("\n"):
        line = raw.rstrip("\r")
        if line.startswith("[") and line.endswith("]"):
            match = RE_ANY_SECTION.match(line)
            section = int(match.group(1)) if match else None
            continue
        if section is None or "=" not in line:
            continue
        key, _, value = line.partition("=")
        if RE_TRACK_LINE.match(value):
            found.setdefault(section, {})[key] = value.split(",")
    return found


def multi_point_fixture(name, base, frames, x_values):
    """Copy of `base` where object 0 has one keyframe per entry of `frames`. X takes x_values; Y and Z keep
    their constant value at every keyframe. Returns (dir, src_path)."""
    out = []
    for line in read_text_lf(base).split("\n"):
        if line.startswith("frame=0,"):
            line = "frame=" + ",".join(str(f) for f in frames)
        elif line[:2] in ("X=", "Y=", "Z=") and line.endswith(",直線移動,0"):
            key = line[0]
            values = x_values if key == "X" else [line.split("=", 1)[1].split(",")[0]] * len(frames)
            line = "%s=%s,直線移動,0" % (key, ",".join(values))
        out.append(line)
    d = case_dir(name)
    src = os.path.join(d, "multi.aup2")
    write_bytes(src, "\n".join(out).encode("utf-8"))
    return d, src


def original_value(frames, values, t):
    """Original X curve: linear on each segment, held at the end values outside the keyframes."""
    if t > frames[-1]:
        return values[-1]
    if t < frames[0]:
        return values[0]
    i = 0
    while i < len(frames) - 2 and t > frames[i + 1]:
        i += 1
    return values[i] + (values[i + 1] - values[i]) * (t - frames[i]) / (frames[i + 1] - frames[i])


def original_slope(frames, values, t):
    i = 0
    while i < len(frames) - 2 and t > frames[i + 1]:
        i += 1
    return Fraction(values[i + 1] - values[i], frames[i + 1] - frames[i])


def new_value_at(frames, values, n):
    """Value at output frame n on the straight lines between the new keyframes."""
    for j in range(len(frames) - 1):
        if frames[j] < frames[j + 1] and frames[j] <= n <= frames[j + 1]:
            return values[j] + (values[j + 1] - values[j]) * Fraction(n - frames[j], frames[j + 1] - frames[j])
    raise AssertionError("output frame %d is not covered by the new keyframes" % n)


def precise_property_check(src, out, label, extra=0):
    """Compare the precise output with the original curve at t = n / k for every output frame n.

    Returns (max error for t <= last keyframe, max deviation for t > last keyframe, max relative speed error,
    max value-change error per segment, bound). The new keyframes are the rescaled ones plus the points where the hold
    at either end starts, so there are at least as many as the original has; the speed is compared only on segments
    that lie inside the original keyframe range.
    """
    src_text = read_text_lf(src)
    out_text = read_text_lf(out)
    rate_src, rate_dst = parse_rate(src_text), parse_rate(out_text)
    k = rate_dst / rate_src
    frames = parse_objects(src_text)[0]["frames"]
    src_parts = tracks_by_object(src_text)[0]["X"]
    values = [Fraction(x) for x in src_parts[:-2]]
    places = max(len(x.partition(".")[2]) for x in src_parts[:-2]) + extra
    new_frames = parse_objects(out_text)[0]["frames"]
    new_values = [Fraction(x) for x in tracks_by_object(out_text)[0]["X"][:-2]]
    assert len(new_frames) == len(new_values) >= len(frames), "%s: keyframe count %d -> %d / %d values" % (
        label, len(frames), len(new_frames), len(new_values))

    bound = Fraction(1, 2) * Fraction(1, 10 ** places)
    max_in = max_out = max_seg = Fraction(0)
    for n in range(new_frames[-1] + 1):
        t = Fraction(n) / k
        error = abs(new_value_at(new_frames, new_values, n) - original_value(frames, values, t))
        if t <= frames[-1]:
            max_in = max(max_in, error)
        else:
            max_out = max(max_out, error)
    assert max_in <= bound + TOLERANCE, "%s: error %s inside the keyframe range exceeds %s" % (
        label, float(max_in), float(bound))

    max_rel = Fraction(0)
    for j in range(len(new_frames) - 1):
        if new_frames[j] == new_frames[j + 1]:
            continue
        t0, t1 = Fraction(new_frames[j]) / k, Fraction(new_frames[j + 1]) / k
        if t0 < frames[0] or t1 > frames[-1]:
            continue  # at the hold the original is flat, so its speed is not compared
        mid = (t0 + t1) / 2
        orig = original_slope(frames, values, mid)
        assert orig != 0, "%s: test object must move" % label
        new_slope = (new_values[j + 1] - new_values[j]) / (t1 - t0)
        max_rel = max(max_rel, abs(new_slope - orig) / abs(orig))
        # Value change over the segment versus the original line: differs only by the two endpoint errors.
        max_seg = max(max_seg, abs((new_values[j + 1] - new_values[j]) - orig * (t1 - t0)))
    return max_in, max_out, max_rel, max_seg, bound


def test_precise_worked_examples_end_values():
    # After the original last keyframe a track holds its end value (AviUtl keeps the last value), so the new last frames
    # show the original end value and the line bends at the last frame that is still inside the original range.
    d = case_dir("precise_examples")
    out = os.path.join(d, "ntsc.aup2")
    code, _, err = convert_target(FIX_30, out, "60000/1001", extra=["--precise"])
    assert code == 0, "exit %d: %s" % (code, err)
    text = read_text_lf(out)
    # k = 2000/1001, e = 149, e * k = 297.70: m = 297 is the last frame on the line, X(297 / k = 148.6485) =
    # -200 + 400 * 148.6485 / 149 = 199.06; frames 298 and 299 show 200.00. Y and Z repeat their constant.
    assert parse_objects(text)[0]["frames"] == [0, 297, 298, 299], text[:400]
    assert "X=-200.00,199.06,200.00,200.00,直線移動,0" in text, "30 -> 60000/1001: the end must hold 200.00"
    assert "Y=0.00,0.00,0.00,0.00,直線移動,0" in text and "Z=0.00,0.00,0.00,0.00,直線移動,0" in text, text
    precise = read_report(out)["precise"]
    assert precise["applied"] == 1 and precise["skipped"] == [], precise
    assert [t["end_extrapolation"] for t in precise["tracks"]] == ["0.000000"] * 3, precise["tracks"]
    assert read_report(out)["notes"] == [], read_report(out)["notes"]

    # 30 -> 60: e * k = 298 is an integer, so only frame 298 is added (X(149) = 200.00 and frame 299 holds it).
    out = os.path.join(d, "t60.aup2")
    code, _, err = convert_target(FIX_30, out, "60", extra=["--precise"])
    assert code == 0, "exit %d: %s" % (code, err)
    text = read_text_lf(out)
    assert parse_objects(text)[0]["frames"] == [0, 298, 299], text[:400]
    assert "X=-200.00,200.00,200.00,直線移動,0" in text, text

    # 66 -> 24 ends before the original does (k < 1, e' / k = 327.25 < 329), so nothing is held: two keyframes as before.
    out = os.path.join(d, "t24.aup2")
    code, _, err = convert_target(FIX_66, out, "24", extra=["--precise"])
    assert code == 0, "exit %d: %s" % (code, err)
    text = read_text_lf(out)
    assert parse_objects(text)[0]["frames"] == [0, 119], text[:400]
    assert "X=-200.00,197.87,直線移動,0" in text, "66 -> 24: end value should be 197.87"
    assert read_report(out)["precise"]["tracks"][0]["end_extrapolation"] == "-2.130000"


def test_precise_property_values_and_speed():
    sources = [("fix30", FIX_30), ("fix66", FIX_66), ("fix120", FIX_120)]
    for name, base, frames, x_values in COLLINEAR_OBJECTS:
        _, src = multi_point_fixture(name + "_src", base, frames, x_values)
        sources.append((name, src))
    d = case_dir("precise_prop")
    worst_in = worst_out = worst_rel = worst_seg = Fraction(0)
    bound = None
    for label, src in sources:
        for target in PRECISE_TARGETS:
            name = "%s_%s" % (label, target.replace("/", "_"))
            out = os.path.join(d, name + ".aup2")
            code, _, err = convert_target(src, out, target, extra=["--precise"])
            assert code == 0, "%s -> %s: exit %d: %s" % (label, target, code, err)
            precise = read_report(out)["precise"]
            assert precise["applied"] == 1 and precise["skipped"] == [], "%s -> %s: %r" % (label, target, precise)
            max_in, max_out, max_rel, max_seg, bound = precise_property_check(src, out, "%s -> %s" % (label, target))
            # Every segment's value change may differ from the original line only by its two endpoint errors.
            assert max_seg <= 2 * bound + TOLERANCE, "%s -> %s: segment error %s above 2 x %s" % (
                label, target, float(max_seg), float(bound))
            # After the original last keyframe the original holds its end value and so does the new project.
            assert max_out <= bound + TOLERANCE, "%s -> %s: error %s after the original end exceeds %s" % (
                label, target, float(max_out), float(bound))
            worst_in, worst_out = max(worst_in, max_in), max(worst_out, max_out)
            worst_rel, worst_seg = max(worst_rel, max_rel), max(worst_seg, max_seg)
            print("  INFO %s -> %s: in-range error %.6f, beyond-end deviation %.6f, speed error %.6f%%" % (
                label, target, float(max_in), float(max_out), float(max_rel) * 100))
    print("  INFO worst over all: in-range error %.6f (bound %s), beyond-end deviation %.6f" % (
        float(worst_in), float(bound), float(worst_out)))
    print("  INFO worst segment value error %.6f (2 x bound %s)" % (float(worst_seg), float(2 * bound)))
    print("  INFO worst speed error %.6f%%; margin to the 0.01%% target: %.6f%%" % (
        float(worst_rel) * 100, float((Fraction(1, 10000) - worst_rel) * 100)))


def test_precise_skips_ineligible_objects_and_ignores_playback_range():
    d = case_dir("precise_skip")
    for label, old, new, what in (
            ("param", "X=-200.00,200.00,直線移動,0", "X=-200.00,200.00,直線移動,1", "parameter 1"),
            ("mode", "X=-200.00,200.00,直線移動,0", "X=-200.00,200.00,補間移動,0", "mode 補間移動")):
        src = os.path.join(d, label + ".aup2")
        write_bytes(src, read_text_lf(FIX_30).replace(old, new, 1).encode("utf-8"))
        line_x = line_number_of(src, new)
        out_precise = os.path.join(d, label + "_precise.aup2")
        out_plain = os.path.join(d, label + "_plain.aup2")
        code, _, err = convert_target(src, out_precise, "60000/1001", extra=["--precise"])
        assert code == 0, "%s: exit %d: %s" % (label, code, err)
        code, _, err = convert_target(src, out_plain, "60000/1001")
        assert code == 0, "%s plain: exit %d: %s" % (label, code, err)
        precise = read_report(out_precise)["precise"]
        assert precise["applied"] == 0, precise
        assert len(precise["skipped"]) == 1 and precise["skipped"][0]["line"] == 36, precise["skipped"]
        reason = precise["skipped"][0]["reason"]
        assert "line %d" % line_x in reason and what in reason, reason
        # The file= line holds each run's own output path, so it is compared separately.
        assert without_file_line(read_bytes(out_precise)) == without_file_line(read_bytes(out_plain)), \
            "%s: skipped object must keep the plain output" % label

    src = os.path.join(d, "range_const.aup2")
    text = read_text_lf(FIX_30).replace("Y=0.00,0.00,直線移動,0", "Y=0.00,0.00,再生範囲,0", 1)
    text = text.replace("Z=0.00,0.00,直線移動,0", "Z=5.50,5.50,補間移動,0", 1)
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "range_const_out.aup2")
    code, _, err = convert_target(src, out, "60000/1001", extra=["--precise"])
    assert code == 0, "exit %d: %s" % (code, err)
    out_text = read_text_lf(out)
    precise = read_report(out)["precise"]
    assert precise["applied"] == 1 and precise["skipped"] == [], precise
    assert "X=-200.00,199.06,200.00,200.00,直線移動,0" in out_text, "X must be resampled"
    assert "Y=0.00,0.00,再生範囲,0" in out_text, "再生範囲 must be kept as it is (two values, no keyframe values)"
    assert "Z=5.50,5.50,5.50,5.50,補間移動,0" in out_text, "a constant track repeats its value on every keyframe"


def test_precise_skips_zero_length_two_point_object():
    # frame=10,10 has no segment: --precise must skip it (not divide by zero) and keep the plain output.
    d = case_dir("precise_zero_len")
    src = os.path.join(d, "zero.aup2")
    text = read_text_lf(FIX_30).replace("frame=0,149", "frame=10,10", 1)
    text = text.replace("X=-200.00,200.00,直線移動,0", "X=0.00,0.00,直線移動,0", 1)
    text = text.replace("Y=0.00,0.00,直線移動,0", "Y=0.00,50.00,直線移動,0", 1)
    write_bytes(src, text.encode("utf-8"))
    line_frame = line_number_of(src, "frame=10,10")
    out_precise = os.path.join(d, "precise.aup2")
    out_plain = os.path.join(d, "plain.aup2")
    code, _, err = convert_target(src, out_precise, "60000/1001", extra=["--precise"])
    assert code == 0, "exit %d: %s" % (code, err)
    code, _, err = convert_target(src, out_plain, "60000/1001")
    assert code == 0, "plain: exit %d: %s" % (code, err)
    precise = read_report(out_precise)["precise"]
    assert precise["applied"] == 0 and precise["tracks"] == [], precise
    assert precise["skipped"] == [{"line": line_frame,
                                   "reason": "frame= start equals end; there is no segment to resample"}], precise
    assert without_file_line(read_bytes(out_precise)) == without_file_line(read_bytes(out_plain)), \
        "skipped zero-length object must keep the plain output"
    assert "Y=0.00,50.00,直線移動,0" in read_text_lf(out_precise), "changing track must be left unchanged"
    assert "precise" not in read_report(out_plain), sorted(read_report(out_plain))


def test_precise_extra_decimals_2_meets_speed_target_on_short_segments():
    name, base, frames, x_values = COLLINEAR_OBJECTS[0]
    d, src = multi_point_fixture(name + "_extra", base, frames, x_values)
    out = os.path.join(d, "extra2.aup2")
    code, _, err = convert_target(src, out, "60000/1001", extra=["--precise", "--precise-extra-decimals", "2"])
    assert code == 0, "exit %d: %s" % (code, err)
    text = read_text_lf(out)
    assert read_report(out)["precise"]["extra_decimals"] == 2, read_report(out)["precise"]
    for value in tracks_by_object(text)[0]["X"][:-2]:
        assert len(value.partition(".")[2]) == 4, "X values must have d + 2 = 4 decimals: %r" % value
    max_in, max_out, max_rel, max_seg, bound = precise_property_check(src, out, "syn30 -> 60000/1001 extra 2", extra=2)
    assert max_seg <= 2 * bound + TOLERANCE, "segment error %s above 2 x %s" % (float(max_seg), float(bound))
    assert max_rel <= Fraction(1, 10000), "speed error %.6f%% above 0.01 %%" % (float(max_rel) * 100)
    print("  INFO syn30 -> 60000/1001 with --precise-extra-decimals 2: in-range error %.6f (bound %s), "
          "speed error %.6f%%" % (float(max_in), float(bound), float(max_rel) * 100))


def test_precise_extra_decimals_0_is_the_default():
    name, base, frames, x_values = COLLINEAR_OBJECTS[0]
    d, src = multi_point_fixture(name + "_extra0", base, frames, x_values)
    out_default = os.path.join(d, "default.aup2")
    out_zero = os.path.join(d, "zero.aup2")
    code, _, err = convert_target(src, out_default, "60000/1001", extra=["--precise"])
    assert code == 0, "exit %d: %s" % (code, err)
    code, _, err = convert_target(src, out_zero, "60000/1001",
                                  extra=["--precise", "--precise-extra-decimals", "0"])
    assert code == 0, "exit %d: %s" % (code, err)
    assert without_file_line(read_bytes(out_default)) == without_file_line(read_bytes(out_zero)), \
        "N=0 output must equal the default output"
    assert read_report(out_default)["precise"] == read_report(out_zero)["precise"]
    assert "extra_decimals" not in read_report(out_zero)["precise"], read_report(out_zero)["precise"]


def test_precise_extra_decimals_refusals():
    d = case_dir("precise_extra_refuse")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(FIX_30, out, "60000/1001", extra=["--precise", "--precise-extra-decimals", "5"])
    assert code == 2 and "0 to 4" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)
    code, _, err = convert_target(FIX_30, out, "60000/1001", extra=["--precise-extra-decimals", "1"])
    assert code == 2 and "needs --precise" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_precise_note_when_quantization_error_is_large():
    d = case_dir("precise_note")
    src = os.path.join(d, "small.aup2")
    write_bytes(src, read_text_lf(FIX_30).replace("X=-200.00,200.00,直線移動,0", "X=0.00,1.00,直線移動,0", 1).encode("utf-8"))
    line_x = line_number_of(src, "X=0.00,1.00")
    out = os.path.join(d, "small_out.aup2")
    code, _, err = convert_target(src, out, "60000/1001", extra=["--precise"])
    assert code == 0, "exit %d: %s" % (code, err)
    notes = read_report(out)["notes"]
    assert len(notes) == 1 and notes[0].startswith("precise: trackbar at line %d " % line_x), notes
    assert "above 0.01%" in notes[0], notes[0]
    assert read_report(out)["precise"]["tracks"][0]["max_quant_error_pct"] != "0.000000"


def test_precise_off_by_default_adds_no_json_fields():
    d = case_dir("precise_off")
    out = os.path.join(d, "plain.aup2")
    code, _, err = convert_target(FIX_30, out, "60000/1001")
    assert code == 0, "exit %d: %s" % (code, err)
    assert "X=-200.00,200.00,直線移動,0" in read_text_lf(out), "trackbar must be unchanged"
    assert "precise" not in read_report(out), sorted(read_report(out))


def test_precise_real_world_66_read_only():
    if not os.path.isfile(REAL_WORLD):
        print("  SKIP real-world file not present")
        return
    before_hash = sha256_of(REAL_WORLD)
    input_dir = os.path.dirname(REAL_WORLD)
    before_listing = sorted(os.listdir(input_dir))
    d = case_dir("real_precise")
    out = os.path.join(d, "real_precise_66.aup2")
    code, _, err = convert_target(REAL_WORLD, out, "66", extra=["--precise"])
    assert sha256_of(REAL_WORLD) == before_hash, "real-world input was modified"
    assert sorted(os.listdir(input_dir)) == before_listing, "output appeared next to real-world input"
    assert code == 0, "exit %d: %s" % (code, err)
    precise = read_report(out)["precise"]
    print("  INFO real-world -> 66 precise: applied %d, skipped %d, tracks %d, notes %d" % (
        precise["applied"], len(precise["skipped"]), len(precise["tracks"]), len(read_report(out)["notes"])))
    for item in precise["skipped"]:
        print("  INFO skipped line %d: %s" % (item["line"], item["reason"]))


def rate_variant(name, rate, frames_text, base=FIX_30):
    """Copy of the 30 FPS fixture with video.rate=rate and its object at frame=frames_text. Returns (dir, src_path)."""
    text = read_text_lf(base).replace("video.rate=30", "video.rate=%d" % rate, 1)
    text = text.replace("frame=0,149", "frame=" + frames_text, 1)
    d = case_dir(name)
    src = os.path.join(d, "src.aup2")
    write_bytes(src, text.encode("utf-8"))
    return d, src


def precise_hold_check(src, out, label):
    """Every new frame n of the object shows the original X at n / k held to the keyframe range, within half a unit of
    the decimals; from the first frame whose time is at or after the original last keyframe, exactly the end value.
    Returns the new frame list."""
    src_text, out_text = read_text_lf(src), read_text_lf(out)
    k = parse_rate(out_text) / parse_rate(src_text)
    frames = parse_objects(src_text)[0]["frames"]
    src_parts = tracks_by_object(src_text)[0]["X"]
    values = [Fraction(x) for x in src_parts[:-2]]
    bound = Fraction(1, 2) * Fraction(1, 10 ** max(len(x.partition(".")[2]) for x in src_parts[:-2]))
    new_frames = parse_objects(out_text)[0]["frames"]
    new_values = [Fraction(x) for x in tracks_by_object(out_text)[0]["X"][:-2]]
    assert len(new_frames) == len(new_values), label
    assert all(b > a for a, b in zip(new_frames, new_frames[1:])), "%s: frames must increase: %r" % (label, new_frames)
    held = 0
    for n in range(new_frames[0], new_frames[-1] + 1):
        t = Fraction(n) / k
        want = original_value(frames, values, min(max(t, frames[0]), frames[-1]))
        got = new_value_at(new_frames, new_values, n)
        assert abs(got - want) <= bound + TOLERANCE, "%s: frame %d shows %s, the original shows %s" % (
            label, n, float(got), float(want))
        if t >= frames[-1]:
            held += 1
            assert got == values[-1], "%s: frame %d must show the end value %s, got %s" % (
                label, n, float(values[-1]), float(got))
        if t <= frames[0]:
            assert got == values[0], "%s: frame %d must show the start value" % (label, n)
    return new_frames, held


def test_precise_holds_the_end_value_frame_by_frame():
    d = case_dir("precise_hold")
    cases = [(FIX_30, "60", [0, 298, 299]), (FIX_30, "66", [0, 327, 328, 329]),
             (FIX_30, "60000/1001", [0, 297, 298, 299]), (FIX_66, "132", [0, 658, 659]),
             (FIX_66, "120", [0, 598, 599]), (FIX_120, "240", [0, 1198, 1199]),
             (FIX_66, "24", [0, 119]), (FIX_66, "66", [0, 329])]
    for src, target, want_frames in cases:
        out = os.path.join(d, "%s_%s.aup2" % (os.path.basename(src), target.replace("/", "_")))
        code, _, err = convert_target(src, out, target, extra=["--precise"])
        assert code == 0, "%s -> %s: exit %d: %s" % (os.path.basename(src), target, code, err)
        frames, held = precise_hold_check(src, out, "%s -> %s" % (os.path.basename(src), target))
        assert frames == want_frames, "%s -> %s: keyframes %r, expected %r" % (os.path.basename(src), target, frames,
                                                                               want_frames)
        print("  INFO %s -> %s: keyframes %s, %d held frames" % (os.path.basename(src), target, frames, held))
    # k < 1 whose last frame still lies after the original end: 10 -> 9, frames 0..4, e' = 4, e' / k = 4.44 > 4.
    # e * k = 3.6: m = 3 is on the line (X(3 / 0.9 = 3.33) = 133.33) and frame 4 holds 200.00.
    _, src = rate_variant("precise_hold_10_9", 10, "0,4")
    out = os.path.join(d, "10_9.aup2")
    code, _, err = convert_target(src, out, "9", extra=["--precise"])
    assert code == 0, "exit %d: %s" % (code, err)
    frames, held = precise_hold_check(src, out, "10 -> 9")
    assert frames == [0, 3, 4], frames
    assert "X=-200.00,133.33,200.00,直線移動,0" in read_text_lf(out), read_text_lf(out)


def test_precise_start_before_the_original_start_holds_the_start_value():
    # 25 -> 37 (k = 1.48), frames 5..20: s' = round_half_up(7.4) = 7 lies before the original start (7 / 1.48 = 4.73 < 5),
    # so frame 7 shows the start value -200.00 and frame 8 (the first with n / k >= 5) is on the line:
    # X(8 / 1.48 = 5.405) = -200 + 400 * 0.405 / 15 = -189.19. At the end e * k = 29.6, m = 29: X(29 / 1.48) = 189.19, frame 30 = 200.00.
    d, src = rate_variant("precise_start", 25, "5,20")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "37", extra=["--precise"])
    assert code == 0, "exit %d: %s" % (code, err)
    frames, held = precise_hold_check(src, out, "25 -> 37")
    assert frames == [7, 8, 29, 30], frames
    assert "X=-200.00,-189.19,189.19,200.00,直線移動,0" in read_text_lf(out), read_text_lf(out)
    # s * k = 7.5 rounds up to s' = 8, which is not before the original start (8 / 1.5 = 5.33 > 5): nothing is added at
    # the start. 20 -> 30, frames 5..19: e' = round_half_up(30) - 1 = 29, e * k = 28.5, so only m = 28 is added.
    d, src = rate_variant("precise_start_ok", 20, "5,19")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "30", extra=["--precise"])
    assert code == 0, "exit %d: %s" % (code, err)
    frames, _ = precise_hold_check(src, out, "20 -> 30")
    assert frames == [8, 28, 29], frames


def half_up(x):
    """Nearest integer to an exact value, halves up (the rule the tool uses for frame numbers)."""
    return int((Fraction(x) + Fraction(1, 2)) // 1)


def test_precise_multi_point_object_keeps_inner_points_and_holds_the_end():
    d = case_dir("precise_hold_multi")
    for name, base, frames, x_values in COLLINEAR_OBJECTS:
        _, src = multi_point_fixture(name + "_hold", base, frames, x_values)
        for target in ("60", "66", "60000/1001", "120", "50", "24"):
            out = os.path.join(d, "%s_%s.aup2" % (name, target.replace("/", "_")))
            code, _, err = convert_target(src, out, target, extra=["--precise"])
            assert code == 0, "%s -> %s: exit %d: %s" % (name, target, code, err)
            new_frames, _ = precise_hold_check(src, out, "%s -> %s" % (name, target))
            k = parse_rate(read_text_lf(out)) / parse_rate(read_text_lf(src))
            # The rescaled keyframes are all still there (inner ones included) and only extra points were added.
            rescaled = [half_up(frames[0] * k)] + [half_up(f * k) for f in frames[1:-1]] + [
                half_up((frames[-1] + 1) * k) - 1]
            assert set(rescaled) <= set(new_frames), "%s -> %s: %r lost from %r" % (name, target, rescaled, new_frames)
            assert len(new_frames) <= len(rescaled) + 4, (name, target, rescaled, new_frames)
            tracks = {key: v for key, v in tracks_by_object(read_text_lf(out))[0].items() if key in ("X", "Y", "Z")}
            assert len(tracks) == 3 and all(len(v) - 2 == len(new_frames) for v in tracks.values()),                 "every track needs one value per keyframe"


def test_precise_leaves_an_object_with_a_time_control_effect_alone():
    # The frame= list of a time control object must stay as it is, so --precise skips it: it is neither resampled nor
    # listed as skipped, and its other animated trackbar keeps its values.
    d = case_dir("precise_tc_object")
    text = read_text_lf(FIX_30)
    text += ("[1]\nlayer=1\nfocus=0\nframe=0,9\n[1.0]\n" + tc_effect(TC_FORWARD, "1")
             + "[1.1]\neffect.name=標準描画\nX=0.00,100.00,直線移動,0\n")
    src = os.path.join(d, "src.aup2")
    write_bytes(src, text.encode("utf-8"))
    plain, precise = os.path.join(d, "plain.aup2"), os.path.join(d, "precise.aup2")
    for out, extra in ((plain, ()), (precise, ("--precise",))):
        code, _, err = convert_target(src, out, "66", extra=extra)
        assert code == 0, "exit %d: %s" % (code, err)
    plain_lines, precise_lines = read_text_lf(plain).split("\n"), read_text_lf(precise).split("\n")
    assert plain_lines[plain_lines.index("[1]"):] == precise_lines[precise_lines.index("[1]"):], \
        "--precise changed the time control object"
    assert "X=0.00,100.00,直線移動,0" in precise_lines and "frame=0,21" in precise_lines, precise_lines
    report = read_report(precise)["precise"]
    assert report["applied"] == 1 and report["skipped"] == [], report
    assert all(t["object_line"] == line_in_section(text, "[0]", "frame=") for t in report["tracks"]), report["tracks"]


def test_precise_hold_changes_only_the_expected_lines():
    # These two --precise outputs were byte-identical to a fixed digest before the hold (UNCHANGED_GOLDEN); with the
    # hold they differ from the plain output only in the frame= line and the trackbar lines of the one resampled object.
    # 30 -> 66 (k = 2.2): e * k = 327.8, so frames 327 (on the line), 328 and 329 (held) are used.
    d = case_dir("precise_diff")
    for src, x_old, x_new in ((FIX_30, "X=-200.00,200.00,直線移動,0", "X=-200.00,199.02,200.00,200.00,直線移動,0"),
                              (SAME_30_30, "X=-400.00,400.00,直線移動,0", "X=-400.00,398.05,400.00,400.00,直線移動,0")):
        label = os.path.basename(src)
        out_plain = os.path.join(d, "plain_" + label)
        out_precise = os.path.join(d, "precise_" + label)
        code, _, err = convert_target(src, out_plain, "66")
        assert code == 0, err
        code, _, err = convert_target(src, out_precise, "66", extra=["--precise"])
        assert code == 0, err
        plain = without_file_line(read_bytes(out_plain)).decode("utf-8").replace("\r\n", "\n").split("\n")
        precise = without_file_line(read_bytes(out_precise)).decode("utf-8").replace("\r\n", "\n").split("\n")
        assert len(plain) == len(precise), label
        diff = {a: b for a, b in zip(plain, precise) if a != b}
        assert diff == {
            "frame=0,329": "frame=0,327,328,329",
            x_old: x_new,
            "Y=0.00,0.00,直線移動,0": "Y=0.00,0.00,0.00,0.00,直線移動,0",
            "Z=0.00,0.00,直線移動,0": "Z=0.00,0.00,0.00,0.00,直線移動,0",
        }, "%s: %r" % (label, diff)


# ---------------------------------------------------------------------------
# --subscene-rate: per-scene rate policy for multi-scene projects (v1.2.2)
# ---------------------------------------------------------------------------

SUBSCENE_DIR = os.path.join(HERE, "fixtures")
MIXED_30_60 = os.path.join(SUBSCENE_DIR, "mixed_30_60.aup2")  # main 30 (シーン into scene 1), scene 1 60 (line move)
MIXED_30_15 = os.path.join(SUBSCENE_DIR, "mixed_30_15.aup2")  # same, scene 1 at 15 (frame-dropped)
SAME_30_30 = os.path.join(SUBSCENE_DIR, "same_30_30.aup2")    # two scenes already at one rate
NESTED_30_60_15 = os.path.join(SUBSCENE_DIR, "nested_30_60_15.aup2")  # 30 -> scene 1 (60) -> scene 2 (15)
DISPLAY_SCENE_1 = os.path.join(SUBSCENE_DIR, "display_scene_1.aup2")  # display.scene=1; scene 0 at 60, scene 1 at 30
SUBSCENE_KEYS = ["scene", "name", "source_rate", "target_rate", "k", "policy", "exported"]
# sha256 of the .aup2 without its file= line, as written by the previous version (git HEAD tools/aup2_fps_upconvert.py)
# with --factor 2. Auto policy on single-rate projects must still produce these bytes.
SAME_RATE_GOLDEN = (
    (FIX_30, "96da95125921e302bc6ba6bd658fd51d3273b49ffaacbab5b296f3c21438d33d"),
    (FIX_66, "cb69154ded1046133403ebdb33f195317c76b70c73ada6e5fdd5dadf4a1beffe"),
    (FIX_120, "0ea72580df58684f51aa05afd7e41ee7cd77e6ea8ffb45b0dc31ed9135c41e10"),
    (SAME_30_30, "e819a4e2612e7976ffcbfffa62808231d5294e4282c0baaac436d325c940195b"),
)


def line_in_section(text, section, prefix):
    """1-based number of the only line starting with prefix inside [section] of an LF-normalised text."""
    current = None
    hits = []
    for no, line in enumerate(text.split("\n"), 1):
        if line.startswith("[") and line.endswith("]"):
            current = line
        elif current == section and line.startswith(prefix):
            hits.append(no)
    assert len(hits) == 1, "expected one %r line in %s, got %r" % (prefix, section, hits)
    return hits[0]


def edit_at(src_text, section, prefix, new_line):
    """{line_no: (old, new)} for the one line of `section` starting with prefix, rewritten to new_line."""
    no = line_in_section(src_text, section, prefix)
    return {no: (src_text.split("\n")[no - 1], new_line)}


def file_edit(src_text, out):
    return edit_at(src_text, "[project]", "file=", expect_file_line(out))


def merged(*edits):
    result = {}
    for edit in edits:
        for no, pair in edit.items():
            assert no not in result, "two edits for line %d" % no
            result[no] = pair
    return result


def assert_subscene_report(report, policy, scenes, notes):
    """scenes: list of (scene, name, source_rate, target_rate, k, policy, exported) in scene order."""
    keys = list(report)
    at = keys.index("k")
    assert keys[at + 1:at + 3] == ["subscene_rate", "scenes"], "sidecar key order: %r" % keys
    assert report["subscene_rate"] == policy, report["subscene_rate"]
    assert report["scenes"] == [dict(zip(SUBSCENE_KEYS, row)) for row in scenes], report["scenes"]
    assert report["notes"] == notes, report["notes"]


def test_subscene_auto_follows_60_scene_when_main_is_30():
    d = case_dir("sub_auto_30_60")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(MIXED_30_60, out, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    src = read_text_lf(MIXED_30_60)
    changes = line_changes(read_bytes(MIXED_30_60), read_bytes(out))
    # Scene 1 is 60 -> 60 (k = 1), so none of its lines change.
    expected = merged(file_edit(src, out),
                      edit_at(src, "[scene.0]", "video.rate=", "video.rate=60"),
                      edit_at(src, "[0]", "frame=", "frame=0,299"))
    assert changes == expected, "unexpected changed lines: %r" % changes
    report = read_report(out)
    assert report["source_rate"] == "30/1" and report["target_rate"] == "60/1" and report["k"] == "2/1", report
    assert_subscene_report(report, "auto", [
        (0, "主合成", "30/1", "60/1", "2/1", "follow", True),
        (1, "次合成1", "60/1", "60/1", "1/1", "follow", False),
    ], [])


def test_subscene_auto_keeps_15_scene_at_its_own_rate():
    d = case_dir("sub_auto_30_15")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(MIXED_30_15, out, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    src = read_text_lf(MIXED_30_15)
    changes = line_changes(read_bytes(MIXED_30_15), read_bytes(out))
    # Scene 1 (15) is kept: none of its lines may change, including its cursor.frame and its object frame.
    expected = merged(file_edit(src, out),
                      edit_at(src, "[scene.0]", "video.rate=", "video.rate=60"),
                      edit_at(src, "[0]", "frame=", "frame=0,299"))
    assert changes == expected, "unexpected changed lines: %r" % changes
    assert_subscene_report(read_report(out), "auto", [
        (0, "主合成", "30/1", "60/1", "2/1", "follow", True),
        (1, "次合成1", "15/1", "15/1", "1/1", "keep", False),
    ], ["scene 1 kept at its own rate 15/1"])


def test_subscene_follow_rescales_15_scene_by_4():
    d = case_dir("sub_follow_30_15")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(MIXED_30_15, out, "60", extra=("--subscene-rate", "follow"))
    assert code == 0, "exit %d: %s" % (code, err)
    src = read_text_lf(MIXED_30_15)
    changes = line_changes(read_bytes(MIXED_30_15), read_bytes(out))
    expected = merged(file_edit(src, out),
                      edit_at(src, "[scene.0]", "video.rate=", "video.rate=60"),
                      edit_at(src, "[0]", "frame=", "frame=0,299"),
                      edit_at(src, "[scene.1]", "video.rate=", "video.rate=60"),
                      edit_at(src, "[scene.1]", "cursor.frame=", "cursor.frame=536"),  # 134 * 4
                      edit_at(src, "[1]", "frame=", "frame=0,599"))  # end = (149 + 1) * 4 - 1
    assert changes == expected, "unexpected changed lines: %r" % changes
    assert_subscene_report(read_report(out), "follow", [
        (0, "主合成", "30/1", "60/1", "2/1", "follow", True),
        (1, "次合成1", "15/1", "60/1", "4/1", "follow", False),
    ], [])


def test_subscene_keep_holds_60_scene_when_target_is_120():
    d = case_dir("sub_keep_30_60")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(MIXED_30_60, out, "120", extra=("--subscene-rate", "keep"))
    assert code == 0, "exit %d: %s" % (code, err)
    src = read_text_lf(MIXED_30_60)
    changes = line_changes(read_bytes(MIXED_30_60), read_bytes(out))
    expected = merged(file_edit(src, out),
                      edit_at(src, "[scene.0]", "video.rate=", "video.rate=120"),
                      edit_at(src, "[0]", "frame=", "frame=0,599"))  # end = (149 + 1) * 4 - 1
    assert changes == expected, "unexpected changed lines: %r" % changes
    assert_subscene_report(read_report(out), "keep", [
        (0, "主合成", "30/1", "120/1", "4/1", "follow", True),
        (1, "次合成1", "60/1", "60/1", "1/1", "keep", False),
    ], ["scene 1 kept at its own rate 60/1"])


def test_subscene_nested_auto_follows_60_and_keeps_15():
    d = case_dir("sub_nested")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(NESTED_30_60_15, out, "120")
    assert code == 0, "exit %d: %s" % (code, err)
    src = read_text_lf(NESTED_30_60_15)
    changes = line_changes(read_bytes(NESTED_30_60_15), read_bytes(out))
    # Scene 1 (60, follow, k = 2): both of its objects and its cursor. Scene 2 (15, keep): nothing.
    expected = merged(file_edit(src, out),
                      edit_at(src, "[scene.0]", "video.rate=", "video.rate=120"),
                      edit_at(src, "[0]", "frame=", "frame=0,599"),
                      edit_at(src, "[scene.1]", "video.rate=", "video.rate=120"),
                      edit_at(src, "[scene.1]", "cursor.frame=", "cursor.frame=268"),  # 134 * 2
                      edit_at(src, "[1]", "frame=", "frame=0,299"),
                      edit_at(src, "[2]", "frame=", "frame=0,299"))
    assert changes == expected, "unexpected changed lines: %r" % changes
    assert_subscene_report(read_report(out), "auto", [
        (0, "主合成", "30/1", "120/1", "4/1", "follow", True),
        (1, "次合成1", "60/1", "120/1", "2/1", "follow", False),
        (2, "次合成2", "15/1", "15/1", "1/1", "keep", False),
    ], ["scene 2 kept at its own rate 15/1"])


def test_subscene_nested_precise_resamples_follow_scene_only():
    d = case_dir("sub_nested_precise")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(NESTED_30_60_15, out, "120", extra=("--precise",))
    assert code == 0, "exit %d: %s" % (code, err)
    src = read_text_lf(NESTED_30_60_15)
    changes = line_changes(read_bytes(NESTED_30_60_15), read_bytes(out))
    # Scene 1 object [1] is a straight line (k = 2), 0..149 -> 0..299. The original ends at 149 = 298 / 2, so the line
    # is used up to new frame 298 (X = 400.00) and frame 299 holds it: keyframes 0, 298, 299. Y and Z repeat 0.00.
    expected = merged(file_edit(src, out),
                      edit_at(src, "[scene.0]", "video.rate=", "video.rate=120"),
                      edit_at(src, "[0]", "frame=", "frame=0,599"),
                      edit_at(src, "[scene.1]", "video.rate=", "video.rate=120"),
                      edit_at(src, "[scene.1]", "cursor.frame=", "cursor.frame=268"),
                      edit_at(src, "[1]", "frame=", "frame=0,298,299"),
                      edit_at(src, "[1.1]", "X=", "X=-400.00,400.00,400.00,直線移動,0"),
                      edit_at(src, "[1.1]", "Y=", "Y=0.00,0.00,0.00,直線移動,0"),
                      edit_at(src, "[1.1]", "Z=", "Z=0.00,0.00,0.00,直線移動,0"),
                      edit_at(src, "[2]", "frame=", "frame=0,299"))
    assert changes == expected, "unexpected changed lines: %r" % changes
    precise = read_report(out)["precise"]
    obj1_frame = line_in_section(src, "[1]", "frame=")
    obj3_frame = line_in_section(src, "[3]", "frame=")
    x_line = line_in_section(src, "[1.1]", "X=")
    assert precise["applied"] == 1, precise
    assert precise["skipped"] == [], "keep scene 2 must not be listed as skipped: %r" % precise["skipped"]
    assert all(t["object_line"] == obj1_frame for t in precise["tracks"]), precise["tracks"]
    assert x_line in [t["line"] for t in precise["tracks"]], precise["tracks"]
    assert not any(t["object_line"] == obj3_frame for t in precise["tracks"]), "keep scene 2 was resampled"


def test_subscene_display_scene_1_sets_k_and_source_rate():
    d = case_dir("sub_display_1")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(DISPLAY_SCENE_1, out, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    src = read_text_lf(DISPLAY_SCENE_1)
    changes = line_changes(read_bytes(DISPLAY_SCENE_1), read_bytes(out))
    # Scene 1 (the displayed one, 30) gives source_rate and k = 2. Scene 0 (60) follows with k = 1.
    expected = merged(file_edit(src, out),
                      edit_at(src, "[scene.1]", "video.rate=", "video.rate=60"),
                      edit_at(src, "[scene.1]", "cursor.frame=", "cursor.frame=268"),
                      edit_at(src, "[1]", "frame=", "frame=0,299"))
    assert changes == expected, "unexpected changed lines: %r" % changes
    report = read_report(out)
    assert report["source_rate"] == "30/1" and report["target_rate"] == "60/1" and report["k"] == "2/1", report
    assert_subscene_report(report, "auto", [
        (0, "主合成", "60/1", "60/1", "1/1", "follow", False),
        (1, "次合成1", "30/1", "60/1", "2/1", "follow", True),
    ], [])


def test_subscene_factor_mode_uses_display_scene_rate():
    d = case_dir("sub_display_factor")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(DISPLAY_SCENE_1, out, 2)
    assert code == 0, "exit %d: %s" % (code, err)
    report = read_report(out)
    assert report["factor"] == 2 and report["source_rate"] == "30/1" and report["target_rate"] == "60/1", report
    assert_subscene_report(report, "auto", [
        (0, "主合成", "60/1", "60/1", "1/1", "follow", False),
        (1, "次合成1", "30/1", "60/1", "2/1", "follow", True),
    ], [])


def test_subscene_display_scene_missing_refused():
    d = case_dir("sub_display_missing")
    src = os.path.join(d, "src.aup2")
    write_bytes(src, read_bytes(DISPLAY_SCENE_1).replace(b"display.scene=1", b"display.scene=7", 1))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 2 and "REFUSED" in err and "display.scene 7 does not exist" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_subscene_default_policy_is_auto():
    d = case_dir("sub_default")
    out_default = os.path.join(d, "default.aup2")
    out_auto = os.path.join(d, "auto.aup2")
    code, _, err = convert_target(MIXED_30_15, out_default, "60")
    assert code == 0, "default: exit %d: %s" % (code, err)
    code, _, err = convert_target(MIXED_30_15, out_auto, "60", extra=("--subscene-rate", "auto"))
    assert code == 0, "auto: exit %d: %s" % (code, err)
    assert without_file_line(read_bytes(out_default)) == without_file_line(read_bytes(out_auto)), \
        "default and --subscene-rate auto must write the same .aup2"
    assert read_report(out_default)["subscene_rate"] == "auto", read_report(out_default)
    assert read_report(out_default)["scenes"] == read_report(out_auto)["scenes"]


def test_subscene_rate_unknown_value_is_usage_error():
    d = case_dir("sub_bad_policy")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(MIXED_30_60, out, "60", extra=("--subscene-rate", "bogus"))
    assert code == 2 and "invalid choice" in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_same_rate_auto_output_matches_previous_version():
    d = case_dir("same_rate_golden")
    for path, digest in SAME_RATE_GOLDEN:
        out = os.path.join(d, os.path.basename(path))
        code, _, err = convert(path, out, 2)
        assert code == 0, "%s: exit %d: %s" % (path, code, err)
        got = hashlib.sha256(without_file_line(read_bytes(out))).hexdigest()
        assert got == digest, "%s: output differs from the previous version (sha256 %s)" % (path, got)


# ---------------------------------------------------------------------------
# 時間制御(オブジェクト) and コマ落ち (v1.2.2)
# ---------------------------------------------------------------------------

TIME_CONTROL_66 = os.path.join(HERE, "fixtures", "time_control_66.aup2")  # 66 fps, three adjacent time control objects
TC_MODE = "直線移動(時間制御)"
TC_POSITION_REASON = "time control 位置 must be a two-point 直線移動(時間制御) track"
TC_FORWARD = "0.000,100.000"
TC_REVERSED = "100.000,0.000"
RE_PROJECT_SECTION = re.compile(r"^\[(\d+)(?:\.(\d+))?\]$")
TC_EFFECT_NAME = "時間制御(オブジェクト)"
STEP_MODE = "瞬間移動"  # a staircase 位置 is a 瞬間移動 track
# sha256 of the .aup2 without its file= line, as written by the previous version (git HEAD tools/aup2_fps_upconvert.py).
# Projects without a time control effect must still produce these bytes, and no notes. Two --precise entries are not
# here: FIX_30 and SAME_30_30 at 66 FPS end after the original (k = 2.2), where --precise now holds the end value
# (test_precise_hold_changes_only_the_expected_lines checks their differences).
UNCHANGED_GOLDEN = (
    (FIX_30, ("--factor", "3"), "54b3584bb02b973b391573ed48ceefbbce39ac29ba8587de1fbd97864ef01bea"),
    (FIX_30, ("--target-fps", "66"), "78210086b1104dec5433ca890770b4c0a8c2eee5821d745dcc4b297b7d8b35ef"),
    (FIX_30, ("--target-fps", "60000/1001"), "463ebf2c3bf5e9a441eacc234416e6e6684a061585bc52095839b9606752ea98"),
    (FIX_30, ("--target-fps", "24"), "f2159b420176632b0b444c82d821959ad385b6eb789098503d0b8d9376ca266d"),
    (FIX_30, ("--target-fps", "24", "--precise", "--precise-extra-decimals", "2"), "160e354f4c3a7688a3cc07f73a1248374c037783873bce86f91cb51c35e85935"),
    (FIX_66, ("--factor", "3"), "078e68e1c175f485c3f2dea7739c6c5ee54c76c8c7977dc9b9ccbbbe50bc7945"),
    (FIX_66, ("--target-fps", "66"), "ed42225bfd7a683f166c5639de83204a17354457a18c7ea042a2897f255017e9"),
    (FIX_66, ("--target-fps", "60000/1001"), "3d395a3e0e5031597613b06c0541616d2df0cbd02d0afbba659646cd7025af6f"),
    (FIX_66, ("--target-fps", "24"), "50273a60da83a085fd0e7657598a7c641f4ac500274cf6903b68eb9a6afb3a8d"),
    (FIX_66, ("--target-fps", "66", "--precise"), "ed42225bfd7a683f166c5639de83204a17354457a18c7ea042a2897f255017e9"),
    (FIX_66, ("--target-fps", "24", "--precise", "--precise-extra-decimals", "2"), "154f3a084b79d12d7d3956ff2ed58390ca271997ec10596e1b4d147995ba34fe"),
    (FIX_120, ("--factor", "3"), "d5fcd93a89b1359cb106dcb6b34589ca4dfd56c6950f4c7ce718d99bb0785601"),
    (FIX_120, ("--target-fps", "66"), "a4c6a9705bba8e96bca612138f424ad0b1d2ffe135f5bdf49ab96cb253e931e0"),
    (FIX_120, ("--target-fps", "60000/1001"), "b931cc3f1386316f2e34e4acffc97a01e1ec8220f019a568d2114fc9dc1bfe88"),
    (FIX_120, ("--target-fps", "24"), "d5120decfbb53d0133ccc7e88dc13936da5ea07dd7c53e788ed93209b2d07b94"),
    (FIX_120, ("--target-fps", "66", "--precise"), "ba08f7c7a55c850e5d150e765c66ebf772266c2bfe0874165e23d28d6c268b65"),
    (FIX_120, ("--target-fps", "24", "--precise", "--precise-extra-decimals", "2"), "0a024a71651cbd4e2f4333731311b569ed349afce6ee0e3c02151fbd120172a7"),
    (SAME_30_30, ("--factor", "3"), "d22aca47d672584ba53530b83b29d5c1babbc4a58c57153f21365b4b58c4106e"),
    (SAME_30_30, ("--target-fps", "66"), "4a58b222777fbdf00f71cd7eab562a5a894ecd1a2c5ca8d3289d8890073ade30"),
    (SAME_30_30, ("--target-fps", "60000/1001"), "7277890efcd960fce2924a2542afc9fe2d05ca9c1bcd303339ea8822e8c43b25"),
    (SAME_30_30, ("--target-fps", "24"), "42bfe1c4564d3f3d528305f403bb477fa8370138eeeed03d4540c37e0028821a"),
    (SAME_30_30, ("--target-fps", "24", "--precise", "--precise-extra-decimals", "2"), "1ee06d92895d0be67293e309160695e400ec64c4142d8b22acbf3fb4d64f6367"),
)


def tc_effect(position=TC_FORWARD, drop="1", mode=TC_MODE, flag="0"):
    """Effect section lines of a 時間制御(オブジェクト); `position` is the 'a,b' part of the 位置 value."""
    return ("effect.name=時間制御(オブジェクト)\n位置=%s,%s,%s\n繰り返し=1\nコマ落ち=%s\n対象レイヤー数=1\n"
            % (position, mode, flag, drop))


def tc_project(name, rate, blocks):
    """Write a project into a new case dir: one object per (frame text, effect text) in `blocks`, object [n] on
    layer n with its effect at [n.0]. Returns (dir, src_path, text)."""
    parts = ["[project]\nversion=2011200\nfile=synthetic.aup2\ndisplay.scene=0\npreview.scene=0\n"
             "[scene.0]\nscene=0\nname=tc\nvideo.width=640\nvideo.height=360\nvideo.rate=%d\nvideo.scale=1\n"
             "audio.rate=44100\ncursor.frame=0\ncursor.layer=0\npreview.frame=0\ndisplay.frame=0\ndisplay.layer=0\n"
             % rate]
    for no, (frames, effect) in enumerate(blocks):
        parts.append("[%d]\nlayer=%d\nfocus=0\nframe=%s\n[%d.0]\n%s" % (no, no, frames, no, effect))
    text = "".join(parts)
    d = case_dir(name)
    src = os.path.join(d, "src.aup2")
    write_bytes(src, text.encode("utf-8"))
    return d, src, text


def line_text_value(text, section, prefix):
    """Value after the '=' of the only line starting with prefix inside [section] of an LF-normalised text."""
    return text.split("\n")[line_in_section(text, section, prefix) - 1].partition("=")[2]


def tc_check(name, rate, target, cases, extra=()):
    """Convert a tc_project at `rate` to the integer rate `target` and compare every changed line and the notes.

    cases: one (frames, position, drop, new_frames, new_position, new_drop[, steps]) per object. Positions are the 'a,b'
    text; the callers compute the expected values by hand. Without `steps` the object keeps a two-point line, written as
    `位置=new_position,直線移動(時間制御),0`. With `steps` (the end of the note: '' or '; last frame shows the previous
    step') it is a staircase: new_frames lists all its points, new_position holds one value per point and is written as
    `位置=new_position,瞬間移動,0`, and a コマ落ち K >= 2 becomes 1. Returns the sidecar report.
    """
    blocks = [(case[0], tc_effect(case[1], case[2])) for case in cases]
    d, src, text = tc_project(name, rate, blocks)
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, str(target), extra=extra)
    assert code == 0, "exit %d: %s" % (code, err)
    edits = [file_edit(text, out), edit_at(text, "[scene.0]", "video.rate=", "video.rate=%d" % target)]
    notes = []
    for no, case in enumerate(cases):
        frames, position, drop, new_frames, new_position, new_drop = case[:6]
        steps = case[6] if len(case) > 6 else None
        line = line_in_section(text, "[%d]" % no, "frame=")
        edits.append(edit_at(text, "[%d]" % no, "frame=", "frame=" + new_frames))
        if steps is not None:
            assert new_drop == "1", "a staircase has no frame drop"
            edits.append(edit_at(text, "[%d.0]" % no, "位置=", "位置=%s,%s,0" % (new_position, STEP_MODE)))
            note = "time control at line %d: 位置 %s -> %d steps (%s)" % (
                line, position, len(new_position.split(",")), STEP_MODE)
            if drop != "1":
                edits.append(edit_at(text, "[%d.0]" % no, "コマ落ち=", "コマ落ち=1"))
                note += "; コマ落ち %s -> 1" % drop
            notes.append(note + steps)
            continue
        if new_position != position:
            edits.append(edit_at(text, "[%d.0]" % no, "位置=", "位置=%s,%s,0" % (new_position, TC_MODE)))
        if new_drop != drop:
            edits.append(edit_at(text, "[%d.0]" % no, "コマ落ち=", "コマ落ち=" + new_drop))
        if new_position != position or new_drop != drop:
            note = "time control at line %d: 位置 %s -> %s" % (line, position, new_position)
            if new_drop != drop:
                note += "; コマ落ち %s -> %s" % (drop, new_drop)
            notes.append(note)
    changes = line_changes(read_bytes(src), read_bytes(out))
    assert changes == merged(*edits), "unexpected changed lines: %r" % changes
    report = read_report(out)
    assert report["notes"] == notes, report["notes"]
    return report


def text_of(value, places=3):
    """value rounded half away from zero to `places` decimals, written like the tool does (independent of the tool)."""
    scaled = Fraction(value) * 10 ** places
    units = int(scaled + Fraction(1, 2)) if scaled >= 0 else -int(-scaled + Fraction(1, 2))
    digits = str(abs(units)).rjust(places + 1, "0")
    return ("-" if units < 0 else "") + digits[:-places] + "." + digits[-places:]


def parse_project(text):
    """{obj_no: {"layer": int, "frames": [int], "effects": [{"keys": {key: value}}]}} of an LF-normalised project text."""
    objects = {}
    current = None
    for raw in text.split("\n"):
        line = raw.rstrip("\r")
        match = RE_PROJECT_SECTION.match(line)
        if match:
            obj = objects.setdefault(int(match.group(1)), {"layer": None, "frames": None, "effects": []})
            if match.group(2) is None:
                current = obj
            else:
                current = {"keys": {}}
                obj["effects"].append(current)
            continue
        if line.startswith("["):
            current = None
            continue
        if current is None or "=" not in line:
            continue
        key, _, value = line.partition("=")
        if "keys" in current:
            current["keys"][key] = value
        elif key == "layer":
            current["layer"] = int(value)
        elif key == "frame":
            current["frames"] = [int(x) for x in value.split(",")]
    return {no: obj for no, obj in objects.items() if obj["frames"] is not None}


def track_of(value):
    """(values, mode, param) of a trackbar value text, or None when it is not one."""
    if not RE_TRACK_LINE.match(value):
        return None
    parts = value.split(",")
    return [Fraction(x) for x in parts[:-2]], parts[-2], parts[-1]


def host_track_value(frames, values, mode, t):
    """Value of a trackbar at the (real) timeline frame t, as measured in AviUtl2 v2.1.12.

    瞬間移動: point i holds on [frames[i], frames[i+1]) and the last point's value is never shown (the last frame shows
    the one before). Every other mode is taken as 直線移動: linear between the keyframes, the end values outside them.
    """
    t = Fraction(t)
    if mode == STEP_MODE:
        if t >= frames[-1]:
            return values[-2]
        return next(values[i] for i in range(len(frames) - 1) if t < frames[i + 1])
    if t <= frames[0]:
        return values[0]
    if t >= frames[-1]:
        return values[-1]
    i = next(i for i in range(len(frames) - 1) if t <= frames[i + 1])
    return values[i] + (values[i + 1] - values[i]) * (t - frames[i]) / (frames[i + 1] - frames[i])


def time_control_effect(obj):
    for effect in obj["effects"]:
        if effect["keys"].get("effect.name") == TC_EFFECT_NAME:
            return effect["keys"]
    return None


def host_tau(obj, t):
    """Timeline frame shown by the layers below the time control object `obj` at the (real) timeline frame t.

    The object holds its end state after its last frame (and its start state before the first). 直線移動(時間制御) has
    its two end values clamped to 0..100 first (per frame it is not clamped); 瞬間移動 is a staircase. With コマ落ち K
    the segments of K frames are counted from the object's first frame.
    """
    keys = time_control_effect(obj)
    frames = obj["frames"]
    s, e = frames[0], frames[-1]
    length = e - s + 1
    values, mode, _ = track_of(keys["位置"])
    drop = int(keys.get("コマ落ち", "1"))
    f = min(max(Fraction(t), s), e)
    if drop > 1:
        f = s + ((f - s) // drop) * drop
    if mode == TC_MODE:
        a, b = (min(max(v, 0), 100) for v in values)
        p = a if length == 1 else a + (b - a) * (f - s) / (length - 1)
    else:
        p = min(max(host_track_value(frames, values, mode, f), 0), 100)
    return s + p * (length - 1) / 100


def host_display(project, t, tracked=("X", "Y", "Z"), times=None):
    """{(obj_no, key): value} of the tracked trackbars of every object alive at the (real) timeline frame t.

    An object lives on [first frame, last frame + 1). A layer below a live time control object (within its
    対象レイヤー数) is evaluated at the time that object shows, not at t; when `times` is a dict it receives that time
    for every object.
    """
    t = Fraction(t)
    controls = [obj for obj in project.values() if time_control_effect(obj) is not None]
    shown = {}
    for no, obj in project.items():
        if time_control_effect(obj) is not None:
            continue
        when = t
        for control in controls:
            frames = control["frames"]
            reach = int(time_control_effect(control).get("対象レイヤー数", "1"))
            if control["layer"] < obj["layer"] <= control["layer"] + reach and frames[0] <= t < frames[-1] + 1:
                when = host_tau(control, t)
        if not obj["frames"][0] <= when < obj["frames"][-1] + 1:
            continue
        if times is not None:
            times[no] = when
        for effect in obj["effects"]:
            for key in tracked:
                track = track_of(effect["keys"].get(key, ""))
                if track:
                    values, mode, _ = track
                    shown[(no, key)] = host_track_value(obj["frames"], values, mode, when)
    return shown


def tau_linear(s, e, a, b, drop, f):
    """The original rule restated for the checks (not held to s..e): the timeline frame shown at the real frame f.

    End values clamped to 0..100, p linear in f also beyond s..e, K-frame segments counted from s.
    """
    length = e - s + 1
    a_c, b_c = (min(max(v, Fraction(0)), Fraction(100)) for v in (a, b))
    f = Fraction(f)
    if drop > 1:
        f = s + ((f - s) // drop) * drop
    p = a_c if length == 1 else a_c + (b_c - a_c) * (f - s) / (length - 1)
    return s + p * (length - 1) / 100


def check_time_control_equivalence(src_text, out_text, k, label):
    """Every new frame n of every time control block shows the same time as the original at n / k.

    The time shown is compared in new frames: host_tau(new block, n) against k * host_tau(original block, n / k). Allowed
    differences: the rounding of 位置 (half a unit of the last decimal, scaled to new frames); in a staircase the last
    frame, which shows the previous step because the host never shows the last point, and a wanted time outside the new
    block (up to half an original frame past either end), which the clamp to 0..100 turns into its first or last frame;
    and, in a two-point line, the frames whose time lies outside the original block, where the line continues instead of
    holding (it is compared with the continued line there). Also checks which form was chosen: a two-point line exactly
    when the rule says so.
    Returns (frames compared, staircase blocks, largest deviation at such frames in new frames).
    """
    src, out = parse_project(src_text), parse_project(out_text)
    compared = staircases = 0
    outside = Fraction(0)
    for no, new_obj in sorted(out.items()):
        new_keys = time_control_effect(new_obj)
        if new_keys is None:
            continue
        old_obj, old_keys = src[no], time_control_effect(src[no])
        s, e = old_obj["frames"][0], old_obj["frames"][-1]
        s2, e2 = new_obj["frames"][0], new_obj["frames"][-1]
        length2 = e2 - s2 + 1
        a_text, b_text = old_keys["位置"].split(",")[:2]
        a, b = Fraction(a_text), Fraction(b_text)
        places = max(len(a_text.partition(".")[2]), len(b_text.partition(".")[2]))
        drop = int(old_keys.get("コマ落ち", "1"))
        values, mode, _ = track_of(new_keys["位置"])
        if length2 == 1:
            continue
        ends = [100 * (k * tau_linear(s, e, a, b, 1, Fraction(n) / k) - s2) / (length2 - 1) for n in (s2, e2)]
        exact = (drop == 1 or ((drop * k).denominator == 1 and (s * k).denominator == 1)) \
            and all(0 <= p <= 100 for p in ends)
        where = "%s: block %d (%d..%d -> %d..%d, 位置 %s,%s, コマ落ち %d)" % (label, no, s, e, s2, e2, a_text, b_text, drop)
        assert (mode == TC_MODE) == exact, "%s: expected %s, got %s" % (where, "a line" if exact else "steps", mode)
        new_drop = int(new_keys.get("コマ落ち", "1"))
        assert new_drop == (drop * k if mode == TC_MODE and drop > 1 else 1), "%s: コマ落ち %d" % (where, new_drop)
        bound = Fraction(1, 2) / 10 ** places * (length2 - 1) / 100 + Fraction(1, 10 ** 9)
        if mode == STEP_MODE:
            staircases += 1
            assert len(values) == len(new_obj["frames"]) and len(values) <= 4096, where
            assert all(0 <= v <= 100 for v in values), where
        for n in range(s2, e2 + 1):
            shown = host_tau(new_obj, n)
            f = Fraction(n) / k
            wanted = k * host_tau(old_obj, f)
            if mode == TC_MODE:
                line = k * tau_linear(s, e, a, b, drop, f)
                assert abs(shown - line) <= bound, "%s: frame %d shows %s, the line is %s" % (
                    where, n, float(shown), float(line))
                if s <= f <= e:
                    assert abs(shown - wanted) <= bound, "%s: frame %d shows %s, wanted %s" % (
                        where, n, float(shown), float(wanted))
                else:
                    outside = max(outside, abs(shown - wanted))
            elif n == e2 and values[-1] != values[-2]:
                assert shown == host_tau(new_obj, n - 1), where
                step = abs(k * host_tau(old_obj, Fraction(n - 1) / k) - wanted)
                assert abs(shown - wanted) <= step + bound, where
            else:
                # The host clamps 位置 to 0..100, so a wanted time outside the new block shows its first or last frame.
                reachable = min(max(wanted, s2), e2)
                outside = max(outside, abs(wanted - reachable))
                assert abs(shown - reachable) <= bound, "%s: frame %d shows %s, wanted %s" % (
                    where, n, float(shown), float(wanted))
            compared += 1
    return compared, staircases, outside


def check_display_equivalence(src_text, out_text, k, label, tolerance=Fraction(2, 100)):
    """Every object shows the same X, Y and Z at new frame n as the original at n / k (the controlled layers included).

    Not compared: a layer right below a staircase block at that block's last frame (the host does not show the last
    point); a layer right below a time control block at a frame whose wanted time lies outside the new block (the clamp
    of 位置 to 0..100 turns it into the first or last frame); a layer that the new project shows at a time that is not
    a whole frame (its keyframes are whole frames, so inside the last segment before the hold the new line is
    stretched against the original: that is what whole-frame keyframes can do; 位置 is rounded to 3 decimals, so a
    time within 0.002 frame of a whole frame counts as whole); and a key that only one of the two projects has at that
    moment (an object that starts or ends up to half a new frame early or late).
    Returns (values compared, values not compared for lack of a partner).
    """
    src, out = parse_project(src_text), parse_project(out_text)
    exempt = set()
    for obj in out.values():
        keys = time_control_effect(obj)
        if keys is not None:
            values, mode, _ = track_of(keys["位置"])
            if mode == STEP_MODE and values[-1] != values[-2]:
                exempt.add((obj["layer"] + 1, obj["frames"][-1]))
            for n in range(obj["frames"][0], obj["frames"][-1] + 1):
                wanted = k * host_tau(src[next(no for no, o in out.items() if o is obj)], Fraction(n) / k)
                if not obj["frames"][0] <= wanted <= obj["frames"][-1]:
                    exempt.add((obj["layer"] + 1, n))
    compared = lonely = 0
    for n in range(0, max(obj["frames"][-1] for obj in out.values()) + 1):
        times = {}
        shown_new = host_display(out, n, times=times)
        shown_old = host_display(src, Fraction(n) / k)
        for key in set(shown_new) | set(shown_old):
            if key not in shown_new or key not in shown_old:
                lonely += 1
                continue
            if (out[key[0]]["layer"], n) in exempt or abs(times[key[0]] - round(times[key[0]])) > Fraction(2, 1000):
                continue
            assert abs(shown_new[key] - shown_old[key]) <= tolerance, "%s: object %d %s at frame %d: new %s, original %s" % (
                label, key[0], key[1], n, float(shown_new[key]), float(shown_old[key]))
            compared += 1
    return compared, lonely


def circle_project(name, rate, blocks):
    """A project built like the P6 probe: per block a time control object on layer 1 and a circle (X -600 -> 600) on
    layer 2 with the same frames. blocks: (frames text, position 'a,b', drop). Returns (dir, src_path, text)."""
    parts = ["[project]\nversion=2011200\nfile=synthetic.aup2\ndisplay.scene=0\npreview.scene=0\n"
             "[scene.0]\nscene=0\nname=circles\nvideo.width=640\nvideo.height=360\nvideo.rate=%d\nvideo.scale=1\n"
             "audio.rate=44100\ncursor.frame=0\ncursor.layer=0\npreview.frame=0\ndisplay.frame=0\ndisplay.layer=0\n"
             % rate]
    for i, (frames, position, drop) in enumerate(blocks):
        parts.append("[%d]\nlayer=1\nframe=%s\n[%d.0]\n%s" % (2 * i, frames, 2 * i, tc_effect(position, drop)))
        parts.append("[%d]\nlayer=2\nframe=%s\n[%d.0]\neffect.name=標準描画\nX=-600.00,600.00,直線移動,0\n"
                     "Y=0.00,0.00,直線移動,0\nZ=0.00,0.00,直線移動,0\n" % (2 * i + 1, frames, 2 * i + 1))
    text = "".join(parts)
    d = case_dir(name)
    src = os.path.join(d, "src.aup2")
    write_bytes(src, text.encode("utf-8"))
    return d, src, text


def tc_refused(name, rate, target, blocks, expected, extra=()):
    """Convert and expect a refusal: exit 2, `expected(text)` appears in stderr, nothing is written."""
    d, src, text = tc_project(name, rate, blocks)
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, str(target), extra=extra)
    message = expected(text)
    assert code == 2 and message in err, "exit %d, expected %r in: %s" % (code, message, err)
    assert_no_output(out)


def test_time_control_arz_style_66_to_60_values():
    d = case_dir("tc_66_60")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(TIME_CONTROL_66, out, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    src = read_text_lf(TIME_CONTROL_66)
    # k = 10/11. Block [1] frames 576..587 (L = 12, 0 -> 100): s' = round_half_up(5760/11 = 523.64) = 524,
    #   e' = round_half_up(5880/11 = 534.55) - 1 = 534, L' = 11. It shows its own frame, shown(n) = n, so
    #   p'(524) = 0 and p'(534) = 100 * (534 - 524) / 10 = 100: a two-point line, 位置 unchanged (no note).
    # Block [2] frames 588..599, 100 -> 0 (tau(f) = 1187 - f): s' = round_half_up(5880/11 = 534.55) = 535,
    #   e' = round_half_up(6000/11 = 545.45) - 1 = 544, L' = 10. Frame n shows original frame 1.1 n (inside the block for
    #   535..544), so the layers must show 10/11 * (1187 - 1.1 n) = 11870/11 - n and p'(n) = 100 * (11870/11 - n - 535) / 9:
    #   101.01 (clamped to 100.000) at 535, 89.899, 78.788, 67.677, 56.566, 45.455, 34.343, 23.232, 12.121, 1.010 at 544.
    #   p' changes at every frame, so the staircase has 10 points; the last one (544) differs from 543 and the host does
    #   not show the last point: the note says so.
    # Block [3] frames 600..611, 20 -> 80 (p = 20 + 60 (f - 600) / 11, tau = 602.2 + 0.6 (f - 600)):
    #   s' = round_half_up(6000/11 = 545.45) = 545, e' = round_half_up(6120/11 = 556.36) - 1 = 555, L' = 11,
    #   shown(n) = 10/11 * (242.2 + 0.66 n) = 2422/11 + 0.6 n, p'(545) = 10 * (24/11) = 21.818, p'(555) = 10 * (90/11) = 81.818:
    #   both in 0..100, so a two-point line.
    # Controlled object [0] 576..611: s' = 524, e' = round_half_up(6120/11) - 1 = 555.
    steps = ["100.000", "89.899", "78.788", "67.677", "56.566", "45.455", "34.343", "23.232", "12.121", "1.010"]
    expected = merged(file_edit(src, out),
                      edit_at(src, "[scene.0]", "video.rate=", "video.rate=60"),
                      edit_at(src, "[0]", "frame=", "frame=524,555"),
                      edit_at(src, "[1]", "frame=", "frame=524,534"),
                      edit_at(src, "[2]", "frame=", "frame=" + ",".join(str(n) for n in range(535, 545))),
                      edit_at(src, "[2.0]", "位置=", "位置=%s,%s,0" % (",".join(steps), STEP_MODE)),
                      edit_at(src, "[3]", "frame=", "frame=545,555"),
                      edit_at(src, "[3.0]", "位置=", "位置=21.818,81.818,%s,0" % TC_MODE))
    changes = line_changes(read_bytes(TIME_CONTROL_66), read_bytes(out))
    assert changes == expected, "unexpected changed lines: %r" % changes
    report = read_report(out)
    assert report["notes"] == [
        "time control at line %d: 位置 100.000,0.000 -> 10 steps (瞬間移動); last frame shows the previous step"
        % line_in_section(src, "[2]", "frame="),
        "time control at line %d: 位置 20.000,80.000 -> 21.818,81.818" % line_in_section(src, "[3]", "frame="),
    ], report["notes"]
    assert {c["line"] for c in report["changes"]} == set(changes), "json changes disagree with output"
    compared, staircases, _ = check_time_control_equivalence(src, read_text_lf(out), Fraction(10, 11), "arz 66 -> 60")
    assert staircases == 1 and compared == 11 + 10 + 11, (compared, staircases)


def test_time_control_30_to_60_forward_and_reversed():
    # Block frames 30..34 (L = 5): s' = 60, e' = round_half_up(35 * 2) - 1 = 69, L' = 10.
    # 0 -> 100 shows its own frame, shown(n) = n, p'(60) = 0, p'(69) = 100 * 9 / 9 = 100: a line, unchanged.
    # 100 -> 0 shows s + e - f = 64 - f, so frame n (original n / 2, inside the block up to n = 68) must show
    #   128 - n and p'(n) = 100 * (128 - n - 60) / 9 = 100 * (68 - n) / 9: 88.889, 77.778, ..., 11.111, 0.000 for n = 60..68.
    #   Frame 69 is original 34.5, after the block, which holds frame 34 (shows 30 = 2 * 30 = 60): p' = 0.000, the same as
    #   68. A change at every frame up to 68 gives the points 60..68, and 69 is added as the last: 10 points, and the last
    #   frame shows the same as before, so there is no note about it.
    tc_check("tc_30_60", 30, 60, [
        ("30,34", TC_FORWARD, "1", "60,69", TC_FORWARD, "1"),
        ("30,34", TC_REVERSED, "1", "60,61,62,63,64,65,66,67,68,69",
         "88.889,77.778,66.667,55.556,44.444,33.333,22.222,11.111,0.000,0.000", "1", ""),
    ])


def test_time_control_30_to_66_forward_and_reversed():
    # Block frames 30..59 (L = 30), k = 11/5: s' = round_half_up(66) = 66, e' = round_half_up(60 * 11/5 = 132) - 1 = 131,
    # L' = 66. 0 -> 100: shown(n) = n, p'(66) = 0 and p'(131) = 100: a line, unchanged.
    # 100 -> 0 (tau = 89 - f): frame n shows 11/5 * (89 - 5 n / 11) = 979/5 - n while n / k <= 59, i.e. n <= 129;
    #   p'(n) = 100 * (979/5 - n - 66) / 65 = 100 * (129.8 - n) / 65 for n = 66..129: 98.154 ... 1.231.
    #   Frames 130 and 131 hold the last original frame (shows 30 = 66 in new frames): 0.000. Points 66..130, then 131 added.
    values = [text_of(100 * (Fraction(1298, 10) - n) / 65) for n in range(66, 130)] + ["0.000", "0.000"]
    assert values[0] == "98.154" and values[63] == "1.231", values
    tc_check("tc_30_66", 30, 66, [
        ("30,59", TC_FORWARD, "1", "66,131", TC_FORWARD, "1"),
        ("30,59", TC_REVERSED, "1", ",".join(str(n) for n in range(66, 132)), ",".join(values), "1", ""),
    ])


def test_time_control_66_to_60_reversed_block():
    # The ARZ block 100 -> 0 on frames 576..587 (L = 12), k = 10/11: s' = 524, e' = 534, L' = 11 (524..534).
    # tau = 1163 - f; frame n (original 1.1 n, inside up to n = 533) shows 10/11 * (1163 - 1.1 n) = 11630/11 - n:
    #   p'(n) = 100 * (11630/11 - n - 524) / 10 = 92.727 at 524, then 82.727, ..., 2.727 at 533.
    #   Frame 534 is original 587.4, after the block: it holds frame 587 (shows 576 = 523.64 new frames), so
    #   p' = 100 * (523.64 - 524) / 10 = -3.64, clamped to 0.000. 11 points; the last differs from 533, so the note says so.
    steps = ["92.727", "82.727", "72.727", "62.727", "52.727", "42.727", "32.727", "22.727", "12.727", "2.727", "0.000"]
    tc_check("tc_66_60_rev", 66, 60, [
        ("576,587", TC_REVERSED, "1", ",".join(str(n) for n in range(524, 535)), ",".join(steps), "1",
         "; last frame shows the previous step")])


def test_time_control_position_decimals_and_rounding():
    # Places are the largest decimal count of the two values, halves round away from zero. 30 -> 60, frames 30..59
    # (L = 30), s' = 60, e' = 119, L' = 60 (denominator 59).
    # 20.0 -> 80.0: tau = 35.8 + 0.6 (f - 30), shown(n) = 35.6 + 0.6 n, p'(60) = 100 * 11.6 / 59 = 19.661 -> 19.7,
    #   p'(119) = 100 * 47 / 59 = 79.661 -> 79.7.
    # -50 -> 150: the host clamps the two end values (measured: it behaves like 0 -> 100), so shown(n) = n,
    #   p'(60) = 0 and p'(119) = 100 and the line becomes 0,100.
    tc_check("tc_dec", 30, 60, [
        ("30,59", "20.0,80.0", "1", "60,119", "19.7,79.7", "1"),
        ("30,59", "-50,150", "1", "60,119", "0,100", "1"),
    ])
    # Ties. 30 -> 90 (k = 3), frames 0..2 (L = 3): s' = 0, e' = round_half_up(9) - 1 = 8, L' = 9 (denominator 8).
    # 10 -> 90: p = 10 + 40 f, tau = 0.2 + 0.8 f, shown(n) = 0.6 + 0.8 n, p'(0) = 100 * 0.6 / 8 = 7.5 -> 8,
    #   p'(8) = 100 * 7 / 8 = 87.5 -> 88.
    # 10 -> 50: tau = 0.2 + 0.4 f, shown(n) = 0.6 + 0.4 n, p'(0) = 7.5 -> 8, p'(8) = 100 * 3.8 / 8 = 47.5 -> 48.
    tc_check("tc_ties", 30, 90, [
        ("0,2", "10,90", "1", "0,8", "8,88", "1"),
        ("0,2", "10,50", "1", "0,8", "8,48", "1"),
    ])


def test_time_control_single_frame_object_and_single_frame_result():
    # L = 1 (frames 10..10): p is constant a and the layers show frame s. 30 -> 60: s' = 20,
    # e' = round_half_up(22) - 1 = 21, L' = 2, shown(n) = k * s = 20 for both frames, so
    # p'(20) = p'(21) = 100 * (20 - 20) / 1 = 0 whatever a and b are.
    tc_check("tc_len1", 30, 60, [("10,10", "30.000,70.000", "1", "20,21", "0.000,0.000", "1")])
    # L' = 1: 120 -> 24 (k = 1/5), frames 11..13: s' = round_half_up(2.2) = 2, e' = round_half_up(14/5 = 2.8) - 1 = 2.
    # The result is one frame (it shows frame s' whatever p' is); both values are p(s' / k) = p(10) =
    # 0 + 100 * (10 - 11) / 2 = -50, the object's own percent at the time of the new frame.
    report = tc_check("tc_result_len1", 120, 24, [("11,13", TC_FORWARD, "1", "2,2", "-50.000,-50.000", "1")])
    assert not any("shortened" in n for n in report["notes"]), report["notes"]


def test_time_control_same_rate_is_unchanged():
    # k = 1: frames, 位置 and コマ落ち stay as they are (the values are already written with their own decimals),
    # and there is no note.
    d, src, text = tc_project("tc_same", 30, [("30,34", tc_effect(TC_REVERSED, "3")),
                                              ("0,59", tc_effect("12.50,-7.25", "1"))])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "30")
    assert code == 0, "exit %d: %s" % (code, err)
    assert without_file_line(read_bytes(out)) == without_file_line(read_bytes(src)), "k = 1 changed the project"
    assert read_report(out)["notes"] == [], read_report(out)["notes"]


def test_time_control_frame_drop_scales_with_the_rate():
    # K x k with K = 2: 30 -> 60 gives 4, 30 -> 90 gives 6. Frames 0..9 with 0 -> 100: shown(n) = n, 位置 unchanged, and
    # s x k = 0 is an integer, so the two-point line stays exact.
    report = tc_check("tc_drop_60", 30, 60, [("0,9", TC_FORWARD, "2", "0,19", TC_FORWARD, "4")])
    assert report["notes"][0].endswith(": 位置 0.000,100.000 -> 0.000,100.000; コマ落ち 2 -> 4"), report["notes"]
    tc_check("tc_drop_90", 30, 90, [("0,9", TC_FORWARD, "2", "0,29", TC_FORWARD, "6")])
    # K = 3 at 30 -> 60 is 6; K = 2 at 30 -> 75 is 5; K = 2 at 30 -> 15 is 1 (halved rate, k = 1/2).
    tc_check("tc_drop_3", 30, 60, [("0,9", TC_FORWARD, "3", "0,19", TC_FORWARD, "6")])
    tc_check("tc_drop_75", 30, 75, [("0,9", TC_FORWARD, "2", "0,24", TC_FORWARD, "5")])
    tc_check("tc_drop_15", 30, 15, [("0,9", TC_FORWARD, "2", "0,4", TC_FORWARD, "1")])
    # K = 1 is not touched.
    tc_check("tc_drop_one", 30, 60, [("0,9", TC_FORWARD, "1", "0,19", TC_FORWARD, "1")])
    # 100 -> 0 cannot stay a line (p'(e') is below 0), so it becomes a staircase and the drop is absorbed by it:
    # 30 -> 60, frames 30..34, K = 2: the segments start at the new frames 60, 64, 68 (original 30, 32, 34) and show
    # 64 - g = 34, 32, 30 (68, 64, 60 in new frames), p' = 100 * (T' - 60) / 9 = 88.889, 44.444, 0.000. Frame 69 holds
    # frame 34, so the staircase is 60, 64, 68, plus 69 as the last point. コマ落ち 2 -> 1.
    tc_check("tc_drop_rev", 30, 60, [("30,34", TC_REVERSED, "2", "60,64,68,69", "88.889,44.444,0.000,0.000", "1", "")])


def test_time_control_frame_drop_not_exact_becomes_a_staircase():
    # K x k is not an integer, or the segment origin s x k is not on a frame: the line cannot show the right frames, so the
    # object becomes a staircase (コマ落ち 1). The steps start at ceil((s + j K) x k).
    # 30 -> 66, K = 2, frames 0..9 (0 -> 100): k = 11/5, s' = 0, e' = round_half_up(22) - 1 = 21, L' = 22. The segments of
    #   original frames 0, 2, 4, 6, 8 start at the new frames ceil(4.4 j) = 0, 5, 9, 14, 18 (and show 0, 2, 4, 6, 8 =
    #   0, 4.4, 8.8, 13.2, 17.6 new frames), p' = 100 * T' / 21 = 0.000, 20.952, 41.905, 62.857, 83.810. Frames 20 and 21 are
    #   after the block and hold frame 9 (segment 8): the same 83.810, so 21 is added as the last point.
    boundaries = [-((-Fraction(2 * j * 11, 5)) // 1) for j in range(5)]
    assert boundaries == [0, 5, 9, 14, 18], boundaries
    tc_check("tc_drop_66", 30, 66, [
        ("0,9", TC_FORWARD, "2", "0,5,9,14,18,21", "0.000,20.952,41.905,62.857,83.810,83.810", "1", "")])
    # 30 -> 45, K = 2, frames 1..8: s x k = 1.5 is not an integer even though K x k = 3 is. s' = 2, e' = round_half_up(13.5) - 1 = 13,
    #   L' = 12. The segments (original 1, 3, 5, 7) start at ceil(1.5 + 3 j) = 2, 5, 8, 11 and show 1.5 g new frames,
    #   p' = 100 * (1.5 g - 2) / 11 = -4.5 (clamped to 0.000), 22.727, 50.000, 77.273; 13 is added as the last point.
    tc_check("tc_drop_45", 30, 45, [
        ("1,8", TC_FORWARD, "2", "2,5,8,11,13", "0.000,22.727,50.000,77.273,77.273", "1", "")])
    # 30 -> 10, K = 2, frames 0..9: k = 1/3, s' = 0, e' = round_half_up(10/3) - 1 = 2, L' = 3. Frame n is original 3 n:
    #   segments start at 0, 2 (n = 1: original 3, segment 2), and n = 2 is original 6 (segment 6): T' = 0, 2/3, 2,
    #   p' = 0.000, 33.333, 100.000. Three points, the last differs from the one before: the note says so.
    tc_check("tc_drop_10", 30, 10, [
        ("0,9", TC_FORWARD, "2", "0,1,2", "0.000,33.333,100.000", "1", "; last frame shows the previous step")])
    # The same rules checked frame by frame with the host model: 30 -> 50 with K = 4 (K x k = 20/3), 66 -> 60 with K = 2 and
    # 66 -> 132 with K = 3 (a line: K x k = 6 and s x k = 0 are integers).
    for rate, target, k, drop, frames in ((30, 50, Fraction(5, 3), "4", "0,9"), (66, 60, Fraction(10, 11), "2", "576,587"),
                                          (66, 132, Fraction(2), "3", "0,11")):
        for position in (TC_FORWARD, TC_REVERSED):
            d, src, text = tc_project("tc_drop_model_%d_%d_%s" % (rate, target, position[0]), rate,
                                      [(frames, tc_effect(position, drop))])
            out = os.path.join(d, "out.aup2")
            code, _, err = convert_target(src, out, str(target))
            assert code == 0, "exit %d: %s" % (code, err)
            check_time_control_equivalence(text, read_text_lf(out), k, "%d -> %d K=%s %s" % (rate, target, drop, position))


def test_time_control_frame_drop_invalid_values_refused():
    for n, value in enumerate(["0", "-1", "1.5", "00x", "0,10,直線移動,0", "+2"]):
        tc_refused("tc_drop_bad_%d" % n, 30, 60, [("0,9", tc_effect(TC_FORWARD, value))],
                   lambda t, value=value: "REFUSED: line %d: コマ落ち=%s (frame drop) must be a positive integer"
                                          % (line_in_section(t, "[0.0]", "コマ落ち="), value))


def test_frame_drop_above_one_outside_time_control_is_refused():
    d = case_dir("koma_two_plain")
    src = os.path.join(d, "src.aup2")
    text = read_text_lf(FIX_30).replace("Group=1\n", "Group=1\nコマ落ち=2\n", 1)
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    expected = ("REFUSED: line %d: コマ落ち=2 (frame drop) is only converted inside a 時間制御(オブジェクト) effect"
                % (text[:text.index("コマ落ち=")].count("\n") + 1))
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_time_control_position_shapes_are_refused():
    def check(name, position_value, frames="0,9"):
        effect = "effect.name=時間制御(オブジェクト)\n位置=%s\n繰り返し=1\nコマ落ち=1\n対象レイヤー数=1\n" % position_value
        tc_refused(name, 30, 60, [(frames, effect)],
                   lambda t: "REFUSED: line %d: %s" % (line_in_section(t, "[0.0]", "位置="), TC_POSITION_REASON))

    check("tc_bad_three_points", "0.000,50.000,100.000,%s,0" % TC_MODE)
    check("tc_bad_payload_other_mode", "0.000,100.000,直線移動,0|27")
    check("tc_bad_plain_mode", "0.000,100.000,直線移動,0")
    check("tc_bad_other_mode", "0.000,100.000,補間移動,0")
    check("tc_bad_one_value", "50")
    check("tc_bad_non_numeric", "a,100.000,%s,0" % TC_MODE)
    check("tc_bad_short", "0.000,100.000,%s" % TC_MODE)
    # Two-point 位置 on an object with three frame= points.
    check("tc_bad_frames_three", "0.000,100.000,%s,0" % TC_MODE, frames="0,5,9")
    # A missing 位置 line is reported on the effect.name line.
    tc_refused("tc_bad_missing", 30, 60,
               [("0,9", "effect.name=時間制御(オブジェクト)\n繰り返し=1\nコマ落ち=1\n対象レイヤー数=1\n")],
               lambda t: "REFUSED: line %d: %s" % (line_in_section(t, "[0.0]", "effect.name="), TC_POSITION_REASON))


def test_time_control_mode_is_accepted_outside_time_control_effect():
    # 直線移動(時間制御) on an ordinary track (any effect, any trackbar): frames are rescaled, values are kept.
    d = case_dir("tc_mode_elsewhere")
    src = os.path.join(d, "src.aup2")
    text = read_text_lf(FIX_30).replace("X=-200.00,200.00,直線移動,0", "X=0.000,100.000,%s,0" % TC_MODE, 1)
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 0, "exit %d: %s" % (code, err)
    changes = line_changes(read_bytes(src), read_bytes(out))
    assert set(changes) <= {3, 11, 36}, changes
    assert changes[36] == ("frame=0,149", "frame=0,299"), changes
    assert "X=0.000,100.000,%s,0" % TC_MODE in read_text_lf(out).split("\n")
    assert read_report(out)["notes"] == [], read_report(out)["notes"]


def test_time_control_in_kept_scene_is_untouched():
    d = case_dir("tc_kept_scene")
    text = read_text_lf(MIXED_30_15)
    bad = ("[2]\nlayer=1\nscene=1\nfocus=0\nframe=0,29\n[2.0]\neffect.name=時間制御(オブジェクト)\n"
           "位置=0.000,50.000,100.000,%s,0\n繰り返し=1\nコマ落ち=3\n対象レイヤー数=1\n" % TC_MODE)
    assert text.count("[plugin.0]\n") == 1
    text = text.replace("[plugin.0]\n", bad + "[plugin.0]\n", 1)
    src = os.path.join(d, "src.aup2")
    write_bytes(src, text.encode("utf-8"))
    # Auto: scene 1 (15) is kept below the 30 -> 60 target, so its time control is copied as it is.
    out = os.path.join(d, "kept.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    expected = merged(file_edit(text, out),
                      edit_at(text, "[scene.0]", "video.rate=", "video.rate=60"),
                      edit_at(text, "[0]", "frame=", "frame=0,299"))
    changes = line_changes(read_bytes(src), read_bytes(out))
    assert changes == expected, "unexpected changed lines: %r" % changes
    assert read_report(out)["notes"] == ["scene 1 kept at its own rate 15/1"], read_report(out)["notes"]
    # Follow: scene 1 follows (k = 4), so the same object is looked at: コマ落ち=3 would become 12, but the
    # three-point 位置 is refused.
    out = os.path.join(d, "follow.aup2")
    code, _, err = convert_target(src, out, "60", extra=("--subscene-rate", "follow"))
    expected = "REFUSED: line %d: %s" % (line_in_section(text, "[2.0]", "位置="), TC_POSITION_REASON)
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_time_control_uses_the_ratio_of_its_own_scene():
    # 30 -> 120 with scene 1 at 60 following (auto). Both objects: frames 0..29, 0 -> 100, コマ落ち=2.
    # Scene 1 (k = 2): s' = 0, e' = round_half_up(30 * 2) - 1 = 59, shown(n) = n so 位置 stays, コマ落ち 2 x 2 = 4.
    # Scene 0 (k = 4): s' = 0, e' = 119, 位置 stays, コマ落ち 2 x 4 = 8.
    d = case_dir("tc_two_rates")
    text = read_text_lf(MIXED_30_60)
    block = "[%d]\nlayer=1\nscene=%d\nfocus=0\nframe=0,29\n[%d.0]\n" + tc_effect(TC_FORWARD, "2")
    marker = "[plugin.0]\n"
    assert text.count(marker) == 1 and text.count("[scene.1]\n") == 1
    text = text.replace(marker, (block % (2, 1, 2)) + marker, 1)
    text = text.replace("[scene.1]\n", (block % (3, 0, 3)) + "[scene.1]\n", 1)
    src = os.path.join(d, "src.aup2")
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "120")
    assert code == 0, "exit %d: %s" % (code, err)
    out_text = read_text_lf(out)
    assert line_text_value(out_text, "[2]", "frame=") == "0,59", out_text
    assert line_text_value(out_text, "[2.0]", "位置=") == "0.000,100.000,%s,0" % TC_MODE
    assert line_text_value(out_text, "[2.0]", "コマ落ち=") == "4"
    assert line_text_value(out_text, "[3]", "frame=") == "0,119", out_text
    assert line_text_value(out_text, "[3.0]", "位置=") == "0.000,100.000,%s,0" % TC_MODE
    assert line_text_value(out_text, "[3.0]", "コマ落ち=") == "8"
    notes = read_report(out)["notes"]
    assert notes == [
        "time control at line %d: 位置 0.000,100.000 -> 0.000,100.000; コマ落ち 2 -> 8"
        % line_in_section(text, "[3]", "frame="),
        "time control at line %d: 位置 0.000,100.000 -> 0.000,100.000; コマ落ち 2 -> 4"
        % line_in_section(text, "[2]", "frame="),
    ], notes


def test_time_control_precise_leaves_time_control_alone():
    d = case_dir("tc_precise")
    text = read_text_lf(FIX_30)
    assert text.endswith("\n")
    text += "[1]\nlayer=1\nfocus=0\nframe=0,149\n[1.0]\n" + tc_effect(TC_FORWARD, "2")
    text += "[2]\nlayer=2\nfocus=0\nframe=0,149\n[2.0]\n" + tc_effect(TC_REVERSED, "1")
    src = os.path.join(d, "src.aup2")
    write_bytes(src, text.encode("utf-8"))
    plain = os.path.join(d, "plain.aup2")
    precise = os.path.join(d, "precise.aup2")
    for out, extra in ((plain, ()), (precise, ("--precise",))):
        code, _, err = convert_target(src, out, "60", extra=extra)
        assert code == 0, "exit %d: %s" % (code, err)
    # k = 2, frames 0..149 (L = 150): s' = 0, e' = 299, L' = 300. [1] 0 -> 100 with K = 2 stays a line (shown(n) = n,
    # s x k = 0), コマ落ち 2 x 2 = 4. [2] 100 -> 0 is a staircase of 300 points: frame n shows 298 - n up to n = 298 and
    # frame 299 holds it (0), so the values run 99.666 ... 0.000, 0.000 and 299 is added as the last point.
    plain_lines = read_text_lf(plain).split("\n")
    assert "位置=0.000,100.000,%s,0" % TC_MODE in plain_lines and "コマ落ち=4" in plain_lines, plain_lines
    steps = [text_of(100 * Fraction(298 - n, 299)) for n in range(0, 299)] + ["0.000"]
    assert steps[0] == "99.666" and steps[298] == "0.000", steps[:2]
    assert "位置=%s,%s,0" % (",".join(steps), STEP_MODE) in plain_lines
    assert "frame=" + ",".join(str(n) for n in range(300)) in plain_lines
    report = read_report(precise)
    obj_line = line_in_section(text, "[0]", "frame=")
    assert report["precise"]["applied"] == 1 and report["precise"]["skipped"] == [], report["precise"]
    assert all(t["object_line"] == obj_line for t in report["precise"]["tracks"]), report["precise"]["tracks"]
    # Object [0] is resampled (0..298 on the line, 299 held); the time control objects are not touched by --precise.
    precise_lines = read_text_lf(precise).split("\n")
    assert "frame=0,298,299" in precise_lines, precise_lines
    assert plain_lines[plain_lines.index("[1]"):] == precise_lines[precise_lines.index("[1]"):], \
        "--precise changed a time control object"
    notes = read_report(plain)["notes"]
    assert len(notes) == 2 and notes[0].endswith("位置 0.000,100.000 -> 0.000,100.000; コマ落ち 2 -> 4"), notes
    assert notes[1].endswith("位置 100.000,0.000 -> 300 steps (瞬間移動)"), notes


def test_time_control_matches_the_original_at_every_frame():
    """Property: for every pair of rates, 位置 and コマ落ち, each new frame shows the same time as the original at the same
    moment (check_time_control_equivalence): within the rounding of 位置, except the documented cases."""
    pairs = [(30, 60), (30, 66), (66, 60), (60, 30), (24, 60), (30, 45), (120, 24), (60, 50), (66, 132), (30, 15)]
    positions = ["0.000,100.000", "100.000,0.000", "20.000,80.000", "-50.0,150.0", "75,25", "0,50", "100.0,40.0",
                 "30.5,-20"]
    frame_sets = [(0, 29), (30, 34), (576, 587), (50, 50), (7, 100), (1, 8)]
    drops = ["1", "2", "3", "4"]
    compared = staircases = lines = 0
    worst_outside = Fraction(0)
    for rate, target in pairs:
        cases = [(position, frames, drop) for position in positions for frames in frame_sets for drop in drops]
        blocks = [("%d,%d" % frames, tc_effect(position, drop)) for position, frames, drop in cases]
        d, src, text = tc_project("tc_prop_%d_%d" % (rate, target), rate, blocks)
        out = os.path.join(d, "out.aup2")
        code, _, err = convert_target(src, out, str(target))
        assert code == 0, "%d -> %d: exit %d: %s" % (rate, target, code, err)
        got, steps, outside = check_time_control_equivalence(text, read_text_lf(out), Fraction(target, rate),
                                                              "%d -> %d" % (rate, target))
        compared, staircases, lines = compared + got, staircases + steps, lines + len(cases) - steps
        worst_outside = max(worst_outside, outside)
    print("  INFO time control property: %d frames compared, %d staircases, %d two-point lines; largest deviation "
          "outside the original range %.3f new frames" % (compared, staircases, lines, float(worst_outside)))
    assert compared > 20000 and staircases > 400 and lines > 200, (compared, staircases, lines)


def test_time_control_staircase_limit_is_4096_points():
    # 30 -> 60, 100 -> 0: L frames give L' = 2 L points (p' changes by 100 / (L' - 1) at every frame, never less than 0.001).
    # L = 2048 is 4096 points: allowed. L = 2049 would need 4098: refused, nothing written.
    d, src, text = tc_project("tc_limit_ok", 30, [("0,2047", tc_effect(TC_REVERSED))])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    out_text = read_text_lf(out)
    assert len(line_text_value(out_text, "[0]", "frame=").split(",")) == 4096
    assert len(line_text_value(out_text, "[0.0]", "位置=").split(",")) == 4096 + 2
    assert read_report(out)["notes"][0].endswith("位置 100.000,0.000 -> 4096 steps (瞬間移動)"), read_report(out)["notes"]
    tc_refused("tc_limit_over", 30, 60, [("0,2048", tc_effect(TC_REVERSED))],
               lambda t: "REFUSED: time control at line %d needs more than 4096 steps" % line_in_section(t, "[0.0]", "位置="))


def test_time_control_staircase_other_trackbars():
    # A staircase gives the object many frame= points, so every other trackbar of the object needs a value for each
    # of them. A constant one repeats its value, an animated one cannot be extended: refused.
    other = "[0.1]\neffect.name=標準描画\nX=%s,直線移動,0\nY=7.50,7.50,補間移動,0\n"
    tc_refused("tc_other_animated", 30, 60, [("30,34", tc_effect(TC_REVERSED) + other % "0.00,100.00")],
               lambda t: "REFUSED: time control at line %d: the object has other animated trackbars"
                         % line_in_section(t, "[0.0]", "位置="))
    d, src, text = tc_project("tc_other_const", 30, [("30,34", tc_effect(TC_REVERSED) + other % "5.00,5.00")])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    out_text = read_text_lf(out)
    assert line_text_value(out_text, "[0.1]", "X=") == ",".join(["5.00"] * 10) + ",直線移動,0", out_text
    assert line_text_value(out_text, "[0.1]", "Y=") == ",".join(["7.50"] * 10) + ",補間移動,0", out_text
    assert len(line_text_value(out_text, "[0]", "frame=").split(",")) == 10
    # A two-point line leaves the frame= list at two points, so an animated trackbar is fine there and stays as it is.
    d, src, text = tc_project("tc_other_line", 30, [("30,34", tc_effect(TC_FORWARD) + other % "0.00,100.00")])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    out_text = read_text_lf(out)
    assert line_text_value(out_text, "[0.1]", "X=") == "0.00,100.00,直線移動,0", out_text
    assert line_text_value(out_text, "[0]", "frame=") == "60,69"


def test_time_control_controlled_layers_show_the_same_values():
    # P6 style (a time control object on layer 1, a circle with X -600 -> 600 below it, 12 frames per block, 100 -> 0 and
    # 0 -> 100 alternating), converted with --precise: every X is the original X at the same moment, except the last frame
    # of a staircase block that differs from the one before it (host limit, see the notes).
    blocks = []
    for i in range(6):
        blocks.append(("%d,%d" % (12 * i, 12 * i + 11), TC_FORWARD if i % 2 == 0 else TC_REVERSED, "1" if i < 4 else "2"))
    for rate, target in ((66, 132), (66, 60), (30, 66), (30, 60), (60, 50), (66, 66 * 3)):
        d, src, text = circle_project("circles_%d_%d" % (rate, target), rate, blocks)
        out = os.path.join(d, "out.aup2")
        code, _, err = convert_target(src, out, str(target), extra=("--precise",))
        assert code == 0, "%d -> %d: exit %d: %s" % (rate, target, code, err)
        compared, lonely = check_display_equivalence(text, read_text_lf(out), Fraction(target, rate),
                                                     "%d -> %d" % (rate, target))
        print("  INFO circles %d -> %d: %d values compared, %d without a partner" % (rate, target, compared, lonely))
        assert compared > 50, compared  # fewer where k is not an integer: the layer then shows fractional times


def test_time_control_p6_probe_66_to_132_matches_the_original():
    probe = os.path.join(REPO, "test_kit", "5_抽帧探针", "P6_66fps_ARZ式12帧交替_rep1.aup2")
    if not os.path.isfile(probe):
        print("  SKIP P6 probe not present")
        return
    before = sha256_of(probe)
    d = case_dir("p6_132")
    out = os.path.join(d, "p6_132.aup2")
    code, _, err = convert_target(probe, out, "132", extra=("--precise",))
    assert code == 0, "exit %d: %s" % (code, err)
    assert sha256_of(probe) == before, "the probe was modified"
    src_text, out_text = read_text_lf(probe), read_text_lf(out)
    compared, staircases, outside = check_time_control_equivalence(src_text, out_text, Fraction(2), "P6 66 -> 132")
    values, lonely = check_display_equivalence(src_text, out_text, Fraction(2), "P6 66 -> 132")
    notes = read_report(out)["notes"]
    print("  INFO P6 66 -> 132: %d blocks frames compared (%d staircases, forward blocks deviate up to %.1f new frame at the "
          "last frame), %d X/Y/Z values compared, %d without a partner, %d notes" % (
              compared, staircases, float(outside), values, lonely, len(notes)))
    assert staircases == 25 and compared == 50 * 24 and lonely == 0, (compared, staircases, lonely)
    assert all("last frame shows" not in n for n in notes), "100 -> 0 at k = 2 ends on the same step"
    assert sum("24 steps" in n for n in notes) == 25, notes


def test_projects_without_time_control_match_previous_version():
    d = case_dir("unchanged_golden")
    for number, (path, args, digest) in enumerate(UNCHANGED_GOLDEN):
        out = os.path.join(d, "o%d_%s" % (number, os.path.basename(path)))
        code, _, err = run_tool("--in", path, "--out", out, *args)
        assert code == 0, "%s %s: exit %d: %s" % (path, args, code, err)
        got = hashlib.sha256(without_file_line(read_bytes(out))).hexdigest()
        assert got == digest, "%s %s: output differs from the previous version (sha256 %s)" % (path, args, got)
        assert read_report(out)["notes"] == [], read_report(out)["notes"]


# ---------------------------------------------------------------------------
# イージング（通常）@イージング: whitelisted, '|' payload kept, not re-sampled by --precise
# ---------------------------------------------------------------------------

EASING_MODE = "イージング（通常）@イージング"
EASING_INTERPOLATED_MODE = "イージング（補間移動）@イージング"  # reads obj.getpoint("time"); not verified, still refused
LINEAR_X = "X=-200.00,200.00,直線移動,0"


def easing_variant(name, edits):
    """Copy of the 30 FPS fixture with `edits` ((old, new) pairs applied to the LF text). Returns (dir, src, text)."""
    d = case_dir(name)
    src = os.path.join(d, "src.aup2")
    text = read_text_lf(FIX_30)
    for old, new in edits:
        assert text.count(old) == 1, old
        text = text.replace(old, new, 1)
    write_bytes(src, text.encode("utf-8"))
    return d, src, text


def test_easing_mode_and_payload_pass_unchanged():
    x_old = "X=-200.00,200.00,%s,0|27" % EASING_MODE
    edits = [(LINEAR_X, x_old), ("Y=0.00,0.00,直線移動,0", "Y=0.00,0.00,%s,0|27" % EASING_MODE),
             ("Z=0.00,0.00,直線移動,0", "Z=0.00,0.00,%s,0" % EASING_MODE)]
    d, src, text = easing_variant("easing_pass", edits)
    for target, rate, last in (("66", 66, 329), ("60000/1001", 60000, 299)):
        out = os.path.join(d, "out_%s.aup2" % rate)
        code, _, err = convert_target(src, out, target)
        assert code == 0, "exit %d: %s" % (code, err)
        # Only file=, video.rate, video.scale (60000/1001 only) and frame= change; the three easing lines, payload
        # included, are copied as they are. 66: e' = round_half_up(150 * 2.2) - 1 = 329; 60000/1001: round_half_up(299.7) - 1 = 299.
        changes = line_changes(read_bytes(src), read_bytes(out))
        assert set(changes) <= {3, 11, 12, 36}, changes
        assert changes[36][1] == "frame=0,%d" % last, changes
        out_lines = read_text_lf(out).split("\n")
        for line in ("X=-200.00,200.00,%s,0|27" % EASING_MODE, "Y=0.00,0.00,%s,0|27" % EASING_MODE,
                     "Z=0.00,0.00,%s,0" % EASING_MODE):
            assert out_lines.count(line) == 1, line
        assert read_report(out)["notes"] == [], read_report(out)["notes"]


def test_easing_multi_point_frames_rescaled_values_kept():
    # Four points, easing between each pair of end values: frames follow the multi-point rule, values stay.
    d = case_dir("easing_multi")
    base = os.path.join(d, "base.aup2")
    make_intermediate_fixture(base, "frame=0,10,20,29")
    text = read_text_lf(base)
    old = "X=-200.00,-200.00,200.00,200.00,直線移動,0"
    assert text.count(old) == 1
    src = os.path.join(d, "src.aup2")
    write_bytes(src, text.replace(old, "X=-200.00,-200.00,200.00,200.00,%s,0|27" % EASING_MODE).encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    assert code == 0, "exit %d: %s" % (code, err)
    changes = line_changes(read_bytes(src), read_bytes(out))
    assert set(changes) <= {3, 11, 36}, changes
    assert changes[36] == ("frame=0,10,20,29", "frame=0,20,40,59"), changes
    assert "X=-200.00,-200.00,200.00,200.00,%s,0|27" % EASING_MODE in read_text_lf(out).split("\n")
    # The value count still has to match the number of frame points.
    src2 = os.path.join(d, "count.aup2")
    write_bytes(src2, text.replace(old, "X=-200.00,200.00,%s,0|27" % EASING_MODE).encode("utf-8"))
    out2 = os.path.join(d, "count_out.aup2")
    code, _, err = convert(src2, out2, 2)
    expected = "REFUSED: line %d: trackbar has 2 values but object [0] has 4 frame points" % line_number_of(
        src2, "X=-200.00,200.00,%s" % EASING_MODE)
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out2)


def test_easing_interpolated_version_and_other_payloads_still_refused():
    d, src, text = easing_variant("easing_interp", [(LINEAR_X, "X=-200.00,200.00,%s,0|27" % EASING_INTERPOLATED_MODE)])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    expected = "REFUSED: line %d: trackbar mode '%s' is not on the whitelist" % (
        line_number_of(src, EASING_INTERPOLATED_MODE), EASING_INTERPOLATED_MODE)
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out)
    # The payload is kept for the easing mode only: every other mode with a payload is still refused.
    for n, mode in enumerate(("補間移動", "瞬間移動", "移動無し", "回転", "再生範囲")):
        d, src, text = easing_variant("easing_payload_%d" % n, [(LINEAR_X, "X=-200.00,200.00,%s,0|27" % mode)])
        out = os.path.join(d, "out.aup2")
        code, _, err = convert(src, out, 2)
        expected = "REFUSED: line %d: trackbar '|' payload '27' is not on the whitelist" % line_number_of(src, "|27")
        assert code == 2 and expected in err, "%s: exit %d: %s" % (mode, code, err)
        assert_no_output(out)


def test_easing_precise_skips_the_object_and_keeps_values():
    # With a payload the existing "'|' payload" skip text applies; without one and with changing values, the existing
    # "mode ... with parameter ... and changing values" text. A constant easing track is eligible (a constant stays).
    d = case_dir("easing_precise")
    cases = (
        ("payload", [(LINEAR_X, "X=-200.00,200.00,%s,0|27" % EASING_MODE)], "has a '|' payload"),
        ("nopayload", [(LINEAR_X, "X=-200.00,200.00,%s,0" % EASING_MODE)],
         "uses mode %s with parameter 0 and changing values; only 直線移動,0 or constant values can be "
         "resampled exactly" % EASING_MODE),
    )
    for label, edits, reason_part in cases:
        _, src, text = easing_variant("easing_precise_" + label, edits)
        out_precise = os.path.join(d, label + "_precise.aup2")
        out_plain = os.path.join(d, label + "_plain.aup2")
        code, _, err = convert_target(src, out_precise, "60000/1001", extra=["--precise"])
        assert code == 0, "%s: exit %d: %s" % (label, code, err)
        code, _, err = convert_target(src, out_plain, "60000/1001")
        assert code == 0, "%s plain: exit %d: %s" % (label, code, err)
        precise = read_report(out_precise)["precise"]
        x_line = line_number_of(src, "X=-200.00,200.00,%s" % EASING_MODE)
        assert precise["applied"] == 0 and precise["tracks"] == [], precise
        assert precise["skipped"] == [{"line": 36, "reason": "trackbar at line %d %s" % (x_line, reason_part)}], \
            precise["skipped"]
        assert without_file_line(read_bytes(out_precise)) == without_file_line(read_bytes(out_plain)), \
            "%s: skipped object must keep the plain output" % label
    _, src, text = easing_variant("easing_precise_const", [("Z=0.00,0.00,直線移動,0", "Z=0.00,0.00,%s,0" % EASING_MODE)])
    out = os.path.join(d, "const_precise.aup2")
    code, _, err = convert_target(src, out, "60000/1001", extra=["--precise"])
    assert code == 0, "exit %d: %s" % (code, err)
    precise = read_report(out)["precise"]
    assert precise["applied"] == 1 and precise["skipped"] == [], precise
    assert "Z=0.00,0.00,0.00,0.00,%s,0" % EASING_MODE in read_text_lf(out).split("\n"), \
        "a constant easing track keeps its mode and repeats its value on every keyframe"


# ---------------------------------------------------------------------------
# v1.2.6: general 直線移動(時間制御) tracks and '|' payload (curve) tracks
# ---------------------------------------------------------------------------

CURVE = "1,0,1,0"  # the host's normalised curve points; the tool treats the text as opaque
LINEAR_PAYLOAD = "直線移動,0|" + CURVE


def lf_lines(path):
    return read_text_lf(path).split("\n")


def lines_without_file(path):
    return [line for line in lf_lines(path) if not line.startswith("file=")]


def test_tc_mode_ordinary_track_two_and_multi_point():
    # Two points and three points on an ordinary 標準描画 track: the frames follow the object rules (30 -> 60:
    # 0,9 -> 0,19 and 0,5,9 -> 0,10,19), the values stay.
    for n, (frames, values, new_frames) in enumerate((("0,9", "0.000,100.000", "0,19"),
                                                      ("0,5,9", "0.000,30.000,100.000", "0,10,19"))):
        track = "X=%s,%s,0" % (values, TC_MODE)
        d, src, text = tc_project("tc_ordinary_%d" % n, 30, [(frames, "effect.name=標準描画\n%s\n" % track)])
        out = os.path.join(d, "out.aup2")
        code, _, err = convert_target(src, out, "60")
        assert code == 0, "exit %d: %s" % (code, err)
        changes = line_changes(read_bytes(src), read_bytes(out))
        assert changes == merged(file_edit(text, out), edit_at(text, "[scene.0]", "video.rate=", "video.rate=60"),
                                 edit_at(text, "[0]", "frame=", "frame=" + new_frames)), changes
        assert read_report(out)["notes"] == [], read_report(out)["notes"]


def test_tc_mode_ordinary_track_count_mismatch_refused():
    d, src, text = tc_project("tc_ordinary_count", 30, [("0,5,9", "effect.name=標準描画\nX=0.000,100.000,%s,0\n" % TC_MODE)])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    expected = "REFUSED: line %d: trackbar has 2 values but object [0] has 3 frame points" % line_in_section(
        text, "[0.0]", "X=")
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_payload_kept_for_the_three_modes_and_comma_payloads():
    # 直線移動, 直線移動(時間制御) and イージング（通常）@イージング keep any '|' payload byte for byte, commas included
    # (the host writes 'X=-600.00,600.00,直線移動(時間制御),0|1,0,1,0'); the value count is still checked.
    groups = (["X=-200.00,200.00,直線移動,0|%s" % CURVE,
               "Y=0.00,0.00,%s,0|%s" % (TC_MODE, CURVE),
               "Z=0.00,0.00,%s,0|%s" % (EASING_MODE, CURVE)],
              ["X=-200.00,200.00,直線移動,0|text without meaning",
               "Y=0.00,0.00,%s,0|" % TC_MODE])
    for n, mode_lines in enumerate(groups):
        edits = [(LINEAR_X, mode_lines[0]), ("Y=0.00,0.00,直線移動,0", mode_lines[1])]
        if len(mode_lines) == 3:
            edits.append(("Z=0.00,0.00,直線移動,0", mode_lines[2]))
        d, src, text = easing_variant("payload_three_%d" % n, edits)
        out = os.path.join(d, "out.aup2")
        code, _, err = convert_target(src, out, "66")
        assert code == 0, "exit %d: %s" % (code, err)
        out_lines = lf_lines(out)
        for line in mode_lines:
            assert out_lines.count(line) == 1, line
        changes = line_changes(read_bytes(src), read_bytes(out))
        assert set(changes) <= {3, 11, 12, 36}, changes
        assert read_report(out)["notes"] == [], read_report(out)["notes"]
        # Same lines but with a precise run: the objects are resampled or skipped, never refused.
        out_p = os.path.join(d, "out_precise.aup2")
        code, _, err = convert_target(src, out_p, "66", extra=["--precise"])
        assert code == 0, "precise: exit %d: %s" % (code, err)
    # The value count is checked also with a comma payload.
    d, src, text = easing_variant("payload_count", [(LINEAR_X, "X=-200.00,-200.00,200.00,直線移動,0|%s" % CURVE)])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert(src, out, 2)
    expected = "REFUSED: line %d: trackbar has 3 values but object [0] has 2 frame points" % line_number_of(src, "X=-200.00,-200.00")
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_payload_other_modes_still_refused_with_comma_payload():
    for n, mode in enumerate(("補間移動", "瞬間移動", "移動無し", "回転", "再生範囲", "加減速移動")):
        d, src, text = easing_variant("payload_other_%d" % n, [(LINEAR_X, "X=-200.00,200.00,%s,0|%s" % (mode, CURVE))])
        out = os.path.join(d, "out.aup2")
        code, _, err = convert(src, out, 2)
        if mode == "加減速移動":  # not a whitelisted mode: the mode text is refused first
            expected = "REFUSED: line %d: trackbar mode '%s' is not on the whitelist" % (line_number_of(src, "|" + CURVE), mode)
        else:
            expected = "REFUSED: line %d: trackbar '|' payload '%s' is not on the whitelist" % (
                line_number_of(src, "|" + CURVE), CURVE)
        assert code == 2 and expected in err, "%s: exit %d: %s" % (mode, code, err)
        assert_no_output(out)


def test_precise_linear_payload_is_resampled_and_kept():
    # 直線移動 with a payload is resampled like one without; the payload is appended after the flag.
    for target in ("60", "24", "60000/1001"):
        d, src, _ = easing_variant("prec_payload_" + target.replace("/", "_"), [(LINEAR_X, "X=-200.00,200.00,%s" % LINEAR_PAYLOAD)])
        _, base, _ = easing_variant("prec_payload_base_" + target.replace("/", "_"), [])
        out, out_base = os.path.join(d, "out.aup2"), os.path.join(d, "base.aup2")
        for path, source in ((out, src), (out_base, base)):
            code, _, err = convert_target(source, path, target, extra=["--precise"])
            assert code == 0, "%s: exit %d: %s" % (target, code, err)
        got, want = lines_without_file(out), lines_without_file(out_base)
        x_lines = [i for i, line in enumerate(want) if line.startswith("X=") and line.endswith(",直線移動,0")]
        assert len(x_lines) == 1, x_lines
        want[x_lines[0]] += "|" + CURVE
        assert got == want, "%s: payload track differs from the track without payload" % target
        precise, base_precise = read_report(out)["precise"], read_report(out_base)["precise"]
        assert precise["applied"] == 1 and precise["skipped"] == [], precise
        assert precise["tracks"] == base_precise["tracks"], precise["tracks"]


def test_precise_tc_mode_two_points_resampled_without_extra_keyframes():
    # 30 -> 24: the new object ends inside the original, so no keyframe is added and the two end values are resampled
    # exactly like 直線移動 (the mode text stays 直線移動(時間制御)).
    d, src, _ = easing_variant("prec_tc_two", [(LINEAR_X, "X=-200.00,200.00,%s,0" % TC_MODE)])
    _, base, _ = easing_variant("prec_tc_two_base", [])
    out, out_base = os.path.join(d, "out.aup2"), os.path.join(d, "base.aup2")
    for path, source in ((out, src), (out_base, base)):
        code, _, err = convert_target(source, path, "24", extra=["--precise"])
        assert code == 0, "exit %d: %s" % (code, err)
    got, want = lines_without_file(out), lines_without_file(out_base)
    index = [i for i, line in enumerate(want) if line.startswith("X=") and line.endswith(",直線移動,0")]
    assert len(index) == 1
    want[index[0]] = want[index[0]].replace(",直線移動,0", ",%s,0" % TC_MODE)
    assert got == want
    assert "frame=0,119" in got, got
    precise = read_report(out)["precise"]
    assert precise["applied"] == 1 and precise["skipped"] == [], precise
    assert precise["tracks"] == read_report(out_base)["precise"]["tracks"]


def test_precise_tc_mode_needing_keyframes_curve_or_multi_point_is_skipped():
    d = case_dir("prec_tc_skip")
    multi = os.path.join(d, "multi.aup2")
    make_intermediate_fixture(multi, "frame=0,10,20,29")
    multi_text = read_text_lf(multi)
    x_multi = "X=-200.00,-200.00,200.00,200.00,直線移動,0"
    assert multi_text.count(x_multi) == 1
    multi_src = os.path.join(d, "multi_tc.aup2")
    write_bytes(multi_src, multi_text.replace(x_multi, x_multi.replace("直線移動", TC_MODE)).encode("utf-8"))
    cases = [
        # (source, target, reason prefix)
        ("hold", easing_variant("prec_tc_skip_hold", [(LINEAR_X, "X=-200.00,200.00,%s,0" % TC_MODE)])[1], "60",
         "uses mode %s; end hold needs keyframes this mode ignores" % TC_MODE),
        ("curve", easing_variant("prec_tc_skip_curve", [(LINEAR_X, "X=-200.00,200.00,%s,0|%s" % (TC_MODE, CURVE))])[1], "24",
         "uses mode %s with a curve or keyframes; it is not re-sampled" % TC_MODE),
        ("curve60", easing_variant("prec_tc_skip_curve60", [(LINEAR_X, "X=-200.00,200.00,%s,0|%s" % (TC_MODE, CURVE))])[1], "60",
         "uses mode %s with a curve or keyframes; it is not re-sampled" % TC_MODE),
        ("multi", multi_src, "24", "uses mode %s with a curve or keyframes; it is not re-sampled" % TC_MODE),
        ("constant_curve", easing_variant("prec_tc_skip_const", [("Y=0.00,0.00,直線移動,0", "Y=0.00,0.00,%s,0|%s" % (TC_MODE, CURVE))])[1],
         "24", "uses mode %s with a curve or keyframes; it is not re-sampled" % TC_MODE),
    ]
    for label, src, target, reason in cases:
        out_p, out_n = os.path.join(d, label + "_p.aup2"), os.path.join(d, label + "_n.aup2")
        for path, extra in ((out_p, ["--precise"]), (out_n, [])):
            code, _, err = convert_target(src, path, target, extra=extra)
            assert code == 0, "%s: exit %d: %s" % (label, code, err)
        report = read_report(out_p)["precise"]
        text = read_text_lf(src)
        tc_line = [i + 1 for i, line in enumerate(text.split("\n")) if "," + TC_MODE + "," in line]
        assert len(tc_line) == 1, tc_line
        frame_line = line_in_section(text, "[0]", "frame=")
        assert report["applied"] == 0 and report["tracks"] == [], (label, report)
        assert report["skipped"] == [{"line": frame_line, "reason": "trackbar at line %d %s" % (tc_line[0], reason)}], \
            (label, report["skipped"])
        assert lines_without_file(out_p) == lines_without_file(out_n), "%s: skipped object must keep the plain output" % label


def tc_curve_effect(position="0.000,100.000", drop="1", curve=CURVE):
    return tc_effect(position, drop, flag="0|" + curve)


def test_time_control_curve_is_kept():
    # 位置 with a curve: no A/B rewrite (a reversed block would otherwise become a staircase); the line and the curve
    # stay as written, the frames are rescaled, and one note says so (N = the frame= line of the object).
    for target, rate in (("60", 30), ("66", 30), ("24", 30)):
        for position in (TC_FORWARD, TC_REVERSED, "-50.000,150.000", "20.000,80.000"):
            blocks = [("0,9", tc_curve_effect(position)), ("20,35", tc_curve_effect(position, curve="0,0,1,1"))]
            d, src, text = tc_project("tc_curve_%s_%s" % (target, position.replace(",", "_")), rate, blocks)
            out = os.path.join(d, "out.aup2")
            code, _, err = convert_target(src, out, target)
            assert code == 0, "%s %s: exit %d: %s" % (target, position, code, err)
            changes = line_changes(read_bytes(src), read_bytes(out))
            out_lines = lf_lines(out)
            for no in (0, 1):
                section = "[%d.0]" % no
                assert text.split("\n")[line_in_section(text, section, "位置=") - 1] == \
                    out_lines[line_in_section(text, section, "位置=") - 1], "位置 line changed"
                assert line_in_section(text, section, "位置=") not in changes
                assert line_in_section(text, section, "コマ落ち=") not in changes
            assert set(changes) == {line_in_section(text, "[project]", "file="),
                                    line_in_section(text, "[scene.0]", "video.rate="),
                                    line_in_section(text, "[0]", "frame="), line_in_section(text, "[1]", "frame=")}, changes
            notes = read_report(out)["notes"]
            assert notes == ["time control at line %d: 位置 has a curve; kept (object-relative, within one frame)"
                             % line_in_section(text, "[%d]" % no, "frame=") for no in (0, 1)], notes


def test_time_control_curve_frame_drop():
    # K = 1 stays. K >= 2 becomes K x k when K x k and s x k are integers, else the run is refused (position line N).
    def convert_blocks(name, rate, target, frames, drop):
        d, src, text = tc_project(name, rate, [(frames, tc_curve_effect(TC_REVERSED, drop))])
        out = os.path.join(d, "out.aup2")
        code, _, err = convert_target(src, out, target)
        return src, text, out, code, err

    # 30 -> 60 (k = 2): K = 2 -> 4, and a start that is not 0 is fine (s x k = 10).
    for frames in ("0,9", "5,14"):
        src, text, out, code, err = convert_blocks("tc_curve_drop_ok_" + frames.replace(",", "_"), 30, "60", frames, "2")
        assert code == 0, "exit %d: %s" % (code, err)
        assert lf_lines(out)[line_in_section(text, "[0.0]", "コマ落ち=") - 1] == "コマ落ち=4"
        assert read_report(out)["notes"] == [
            "time control at line %d: 位置 has a curve; kept (object-relative, within one frame); コマ落ち 2 -> 4"
            % line_in_section(text, "[0]", "frame=")], read_report(out)["notes"]
        assert lf_lines(out)[line_in_section(text, "[0.0]", "位置=") - 1] == "位置=100.000,0.000,%s,0|%s" % (TC_MODE, CURVE)
    # 30 -> 66 (k = 11/5): K = 5 and s = 5 -> K x k = 11, s x k = 11 are integers.
    src, text, out, code, err = convert_blocks("tc_curve_drop_ok_66", 30, "66", "5,14", "5")
    assert code == 0, "exit %d: %s" % (code, err)
    assert lf_lines(out)[line_in_section(text, "[0.0]", "コマ落ち=") - 1] == "コマ落ち=11"
    # Refused: K x k = 22/5 (K = 2), and s x k = 11/5 (s = 1) with K x k = 11.
    for n, (frames, drop) in enumerate((("0,9", "2"), ("1,10", "5"))):
        src, text, out, code, err = convert_blocks("tc_curve_drop_bad_%d" % n, 30, "66", frames, drop)
        expected = "REFUSED: time control at line %d has a curve and コマ落ち=%s cannot be kept exact" % (
            line_in_section(text, "[0.0]", "位置="), drop)
        assert code == 2 and expected in err, "exit %d: %s" % (code, err)
        assert_no_output(out)
    # Without a コマ落ち line the drop is 1 and nothing is refused.
    d, src, text = tc_project("tc_curve_no_drop", 30, [("0,9", "effect.name=時間制御(オブジェクト)\n位置=0.000,100.000,%s,0|%s\n"
                                                               "繰り返し=1\n対象レイヤー数=1\n" % (TC_MODE, CURVE))])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "66")
    assert code == 0, "exit %d: %s" % (code, err)


def test_time_control_curve_precise_and_same_rate():
    d, src, text = tc_project("tc_curve_precise", 30, [("0,9", tc_curve_effect(TC_REVERSED, "2")),
                                                       ("0,9", tc_effect(TC_FORWARD, "1"))])
    # precise: the curve object is not resampled and not listed, like any time control object.
    out_p, out_n = os.path.join(d, "p.aup2"), os.path.join(d, "n.aup2")
    for path, extra in ((out_p, ["--precise"]), (out_n, [])):
        code, _, err = convert_target(src, path, "60", extra=extra)
        assert code == 0, "exit %d: %s" % (code, err)
    assert lines_without_file(out_p) == lines_without_file(out_n)
    assert read_report(out_p)["precise"]["skipped"] == [] and read_report(out_p)["precise"]["applied"] == 0
    # Same rate (k = 1): nothing is rewritten and there is no note.
    out_same = os.path.join(d, "same.aup2")
    code, _, err = convert_target(src, out_same, "30")
    assert code == 0, "exit %d: %s" % (code, err)
    assert read_report(out_same)["notes"] == [], read_report(out_same)["notes"]
    assert set(line_changes(read_bytes(src), read_bytes(out_same))) == {line_in_section(text, "[project]", "file=")}
    # A curve that is not a clean two-point 位置 is still refused with the position reason.
    tc_refused("tc_curve_three_points", 30, 60,
               [("0,9", tc_effect("0.000,50.000,100.000", "1", flag="0|" + CURVE))],
               lambda t: "REFUSED: line %d: %s" % (line_in_section(t, "[0.0]", "位置="), TC_POSITION_REASON))


INTERP_TC_MODE = "補間移動(時間制御)"


def test_interp_tc_mode_passes_keeps_payload_and_multi_point():
    # 補間移動(時間制御): frames follow the object rules, values and payload are kept (two points, curve, three points).
    cases = (("0,9", "0.000,100.000", "0", "0,19"),
             ("0,9", "0.000,100.000", "0|0,0,1,0", "0,19"),
             ("0,9", "100.000,115.000", "0|", "0,19"),
             ("0,5,9", "0.000,30.000,100.000", "0|%s" % CURVE, "0,10,19"))
    for n, (frames, values, flag, new_frames) in enumerate(cases):
        track = "拡大率=%s,%s,%s" % (values, INTERP_TC_MODE, flag)
        d, src, text = tc_project("interp_tc_%d" % n, 30, [(frames, "effect.name=標準描画\n%s\n" % track)])
        out = os.path.join(d, "out.aup2")
        code, _, err = convert_target(src, out, "60")
        assert code == 0, "exit %d: %s" % (code, err)
        changes = line_changes(read_bytes(src), read_bytes(out))
        assert changes == merged(file_edit(text, out), edit_at(text, "[scene.0]", "video.rate=", "video.rate=60"),
                                 edit_at(text, "[0]", "frame=", "frame=" + new_frames)), changes
        assert track in lf_lines(out) and read_report(out)["notes"] == [], read_report(out)["notes"]
    # The value count is checked, also with a comma payload.
    d, src, text = tc_project("interp_tc_count", 30, [("0,5,9", "effect.name=標準描画\n拡大率=0.000,100.000,%s,0|%s\n" % (INTERP_TC_MODE, CURVE))])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    expected = "REFUSED: line %d: trackbar has 2 values but object [0] has 3 frame points" % line_in_section(text, "[0.0]", "拡大率=")
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_precise_interp_tc_mode_always_skips():
    d = case_dir("prec_interp_tc")
    for label, flag, target in (("plain", "0", "24"), ("plain60", "0", "60"), ("payload", "0|%s" % CURVE, "24"),
                                ("constant", "0", "24")):
        values = "-200.00,-200.00" if label == "constant" else "-200.00,200.00"
        _, src, text = easing_variant("prec_interp_tc_" + label, [(LINEAR_X, "X=%s,%s,%s" % (values, INTERP_TC_MODE, flag))])
        out_p, out_n = os.path.join(d, label + "_p.aup2"), os.path.join(d, label + "_n.aup2")
        for path, extra in ((out_p, ["--precise"]), (out_n, [])):
            code, _, err = convert_target(src, path, target, extra=extra)
            assert code == 0, "%s: exit %d: %s" % (label, code, err)
        report = read_report(out_p)["precise"]
        x_line = line_number_of(src, INTERP_TC_MODE)
        assert report["applied"] == 0 and report["tracks"] == [], (label, report)
        assert report["skipped"] == [{"line": line_in_section(text, "[0]", "frame="), "reason":
                                      "trackbar at line %d uses mode %s; it is not re-sampled" % (x_line, INTERP_TC_MODE)}], \
            (label, report["skipped"])
        assert lines_without_file(out_p) == lines_without_file(out_n), "%s: skipped object must keep the plain output" % label


# ---------------------------------------------------------------------------
# Frame-counting trackbars: 反復移動, ランダム移動, 移動量指定 (test_kit/9_其他移动方式, AviUtl2 v2.1.12)
# ---------------------------------------------------------------------------

FM_REPEAT = "反復移動"
FM_RANDOM = "ランダム移動"
FM_STEP = "移動量指定"


def fm_project(name, rate, tracks, frames="0,119"):
    """tc_project with one 標準描画 object (object [0], frames `frames`) whose effect holds the trackbar lines `tracks`."""
    return tc_project(name, rate, [(frames, "effect.name=標準描画\n" + "".join(track + "\n" for track in tracks))])


def fm_track_line(text, prefix):
    """1-based line number of the trackbar starting with prefix ('X=', 'Y=' or 'Z=') in the effect of object [0]."""
    return line_in_section(text, "[0.0]", prefix)


def test_frame_mode_repeat_30_to_60_doubles_the_parameter():
    # k = 2, frames 0,119 -> 0,239. Y and Z are 0.00,0.00 (E = 0) and are rewritten like X, each with its own note.
    d, src, text = fm_project("fm_repeat_60", 30, ["X=-600.00,600.00,%s,4|15" % FM_REPEAT,
                                                  "Y=0.00,0.00,%s,4|15" % FM_REPEAT,
                                                  "Z=0.00,0.00,%s,4|15" % FM_REPEAT])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    expected = merged(file_edit(text, out), edit_at(text, "[scene.0]", "video.rate=", "video.rate=60"),
                      edit_at(text, "[0]", "frame=", "frame=0,239"),
                      edit_at(text, "[0.0]", "X=", "X=-600.00,600.00,%s,4|30" % FM_REPEAT),
                      edit_at(text, "[0.0]", "Y=", "Y=0.00,0.00,%s,4|30" % FM_REPEAT),
                      edit_at(text, "[0.0]", "Z=", "Z=0.00,0.00,%s,4|30" % FM_REPEAT))
    assert line_changes(read_bytes(src), read_bytes(out)) == expected
    assert read_report(out)["notes"] == ["trackbar at line %d: %s parameter 15 -> 30" % (fm_track_line(text, p), FM_REPEAT)
                                         for p in ("X=", "Y=", "Z=")], read_report(out)["notes"]


def test_frame_mode_repeat_30_to_60000_1001_writes_29_97003_within_tolerance():
    # k = 2000/1001: the exact parameter is 15 * 2000/1001 = 30000/1001 = 29.970029970..., written 29.97003 (6 decimals,
    # trailing zero dropped). Error bound E = |600 - (-600)| * 355/113 * 239 * |29.97003 - 30000/1001| / (2 * (30000/1001)^2)
    # with |29.97003 - 30000/1001| = 2.997e-8 gives E = 1.503e-05 <= 0.5, so it is accepted. Frames 0,119 -> 0,239.
    d, src, text = fm_project("fm_repeat_ntsc", 30, ["X=-600.00,600.00,%s,4|15" % FM_REPEAT])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60000/1001")
    assert code == 0, "exit %d: %s" % (code, err)
    expected = merged(file_edit(text, out), edit_at(text, "[scene.0]", "video.rate=", "video.rate=60000"),
                      edit_at(text, "[scene.0]", "video.scale=", "video.scale=1001"),
                      edit_at(text, "[0]", "frame=", "frame=0,239"),
                      edit_at(text, "[0.0]", "X=", "X=-600.00,600.00,%s,4|29.97003" % FM_REPEAT))
    assert line_changes(read_bytes(src), read_bytes(out)) == expected
    assert read_report(out)["notes"] == ["trackbar at line %d: %s parameter 15 -> 29.97003"
                                         % (fm_track_line(text, "X="), FM_REPEAT)], read_report(out)["notes"]


def test_frame_mode_repeat_error_bound_refuses_a_parameter_that_rounds_too_far():
    # 10 fps (k = 1/3), object 0..899 -> 0..299 (span F = 299). Parameter 1 is written as 0.333333, |Q - P2| = 3.33e-7,
    # min(Q, P2) = Q. E = 1920 * 355/113 * 299 * 3.33e-7 / (2 * 0.333333^2) = 2.705 > 0.5, so it is refused.
    d, src, text = fm_project("fm_repeat_bound", 30, ["X=-960.00,960.00,%s,4|1" % FM_REPEAT], frames="0,899")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "10")
    expected = "REFUSED: line %d: %s parameter 1 cannot be written with 6 decimals within 0.5" % (
        fm_track_line(text, "X="), FM_REPEAT)
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out)
    # The same parameter on a 2 second object (0..59 -> 0..19, F = 19): E = 0.17 <= 0.5, accepted as 0.333333.
    d, src, text = fm_project("fm_repeat_bound_short", 30, ["X=-960.00,960.00,%s,4|1" % FM_REPEAT], frames="0,59")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "10")
    assert code == 0, "exit %d: %s" % (code, err)
    assert "X=-960.00,960.00,%s,4|0.333333" % FM_REPEAT in read_text_lf(out).split("\n")


def test_frame_mode_random_30_to_66_scales_the_integer_parameter():
    # k = 11/5: 15 -> 33 exactly. Frames 0,119 -> 0,263. A parameter written as 15.0 is an integer (Fraction) and gives 30.
    d, src, text = fm_project("fm_random_66", 30, ["X=-600.00,600.00,%s,4|15" % FM_RANDOM,
                                                  "Y=0.00,0.00,%s,4|15" % FM_RANDOM,
                                                  "Z=0.00,0.00,%s,4|15" % FM_RANDOM])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "66")
    assert code == 0, "exit %d: %s" % (code, err)
    expected = merged(file_edit(text, out), edit_at(text, "[scene.0]", "video.rate=", "video.rate=66"),
                      edit_at(text, "[0]", "frame=", "frame=0,263"),
                      edit_at(text, "[0.0]", "X=", "X=-600.00,600.00,%s,4|33" % FM_RANDOM),
                      edit_at(text, "[0.0]", "Y=", "Y=0.00,0.00,%s,4|33" % FM_RANDOM),
                      edit_at(text, "[0.0]", "Z=", "Z=0.00,0.00,%s,4|33" % FM_RANDOM))
    assert line_changes(read_bytes(src), read_bytes(out)) == expected
    d, src, text = fm_project("fm_random_15_0", 30, ["X=-600.00,600.00,%s,4|15.0" % FM_RANDOM])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    assert "X=-600.00,600.00,%s,4|30" % FM_RANDOM in read_text_lf(out).split("\n")
    assert read_report(out)["notes"] == ["trackbar at line %d: %s parameter 15.0 -> 30" % (fm_track_line(text, "X="), FM_RANDOM)]


def test_frame_mode_random_non_integer_after_k_and_non_integer_parameter_refused():
    # 30 -> 60000/1001: 15 * 2000/1001 = 30000/1001 is not an integer, so the move is refused (not verified).
    d, src, text = fm_project("fm_random_ntsc", 30, ["X=-600.00,600.00,%s,4|15" % FM_RANDOM])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60000/1001")
    expected = ("REFUSED: line %d: %s parameter 15 becomes 30000/1001 at the target rate; a non-integer parameter is not "
                "verified" % (fm_track_line(text, "X="), FM_RANDOM))
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out)
    # 15.4 is not an integer parameter at all: refused by the parameter rule, whatever the rate.
    d, src, text = fm_project("fm_random_decimal", 30, ["X=-600.00,600.00,%s,4|15.4" % FM_RANDOM])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    expected = "REFUSED: line %d: %s parameter must be an integer, got '15.4'" % (fm_track_line(text, "X="), FM_RANDOM)
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_frame_mode_step_30_to_60_halves_the_step_and_keeps_zero_steps():
    # k = 2: 4.00 -> 2.00 (the written text keeps the source's 2 decimals). Y and Z 0.00 stay as they are, without a note.
    d, src, text = fm_project("fm_step_60", 30, ["X=-600.00,4.00,%s,4" % FM_STEP, "Y=0.00,0.00,%s,4" % FM_STEP,
                                                "Z=0.00,0.00,%s,4" % FM_STEP])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    expected = merged(file_edit(text, out), edit_at(text, "[scene.0]", "video.rate=", "video.rate=60"),
                      edit_at(text, "[0]", "frame=", "frame=0,239"),
                      edit_at(text, "[0.0]", "X=", "X=-600.00,2.00,%s,4" % FM_STEP))
    assert line_changes(read_bytes(src), read_bytes(out)) == expected
    assert read_report(out)["notes"] == ["trackbar at line %d: %s step 4.00 -> 2.00" % (fm_track_line(text, "X="), FM_STEP)]


def test_frame_mode_step_30_to_66_two_decimals_refused():
    # k = 11/5: 4.00 / k = 20/11 = 1.8181818..., written with the 2 decimals of 4.00 as 1.82. Frames 0,149 -> 0,329 (F = 329).
    # |1.82 - 20/11| = 1/550, E = 329/550 = 0.598 > 0.5, so it is refused.
    d, src, text = fm_project("fm_step_66_2dp", 30, ["X=-600.00,4.00,%s,4" % FM_STEP], frames="0,149")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "66")
    expected = "REFUSED: line %d: %s step 4.00 cannot be written with 2 decimals within 0.5" % (fm_track_line(text, "X="), FM_STEP)
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_frame_mode_step_30_to_66_three_decimals_within_tolerance():
    # k = 11/5: 4.000 / k = 20/11, written with the 3 decimals of 4.000 as 1.818. |1.818 - 20/11| = 1/5500, with F = 329
    # E = 329/5500 = 0.0598 <= 0.5, accepted. Y and Z 0.00 are unchanged.
    d, src, text = fm_project("fm_step_66_3dp", 30, ["X=-600.00,4.000,%s,4" % FM_STEP, "Y=0.00,0.00,%s,4" % FM_STEP,
                                                     "Z=0.00,0.00,%s,4" % FM_STEP], frames="0,149")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "66")
    assert code == 0, "exit %d: %s" % (code, err)
    expected = merged(file_edit(text, out), edit_at(text, "[scene.0]", "video.rate=", "video.rate=66"),
                      edit_at(text, "[0]", "frame=", "frame=0,329"),
                      edit_at(text, "[0.0]", "X=", "X=-600.00,1.818,%s,4" % FM_STEP))
    assert line_changes(read_bytes(src), read_bytes(out)) == expected
    assert read_report(out)["notes"] == ["trackbar at line %d: %s step 4.000 -> 1.818" % (fm_track_line(text, "X="), FM_STEP)]


def test_frame_mode_step_30_to_60000_1001_two_decimals_refused():
    # k = 2000/1001: 4 / k = 2.002, written 2.00 with 2 decimals. Frames 0,149 -> 0,299 (F = 299): E = 0.002 * 299 = 0.598 > 0.5.
    d, src, text = fm_project("fm_step_ntsc_2dp", 30, ["X=-600.00,4.00,%s,4" % FM_STEP], frames="0,149")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60000/1001")
    expected = "REFUSED: line %d: %s step 4.00 cannot be written with 2 decimals within 0.5" % (fm_track_line(text, "X="), FM_STEP)
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_frame_mode_step_written_with_no_decimals_when_source_has_none():
    # k = 2: 4 / 2 = 2, written with the 0 decimals of the source text "4" as 2. E = 0.
    d, src, text = fm_project("fm_step_int", 30, ["X=-600.00,4,%s,4" % FM_STEP])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    assert "X=-600.00,2,%s,4" % FM_STEP in read_text_lf(out).split("\n")
    assert read_report(out)["notes"] == ["trackbar at line %d: %s step 4 -> 2" % (fm_track_line(text, "X="), FM_STEP)]


def test_frame_mode_random_starting_after_frame_0_refused_and_same_rate_kept():
    # Round 3 C3: the random path of ランダム移動 depends on the start frame, so an object starting at frame 10 is refused
    # at k = 2 (checked after the time control overlap rule, before the integer rule).
    d, src, text = fm_project("fm_random_start_10", 30, ["X=-600.00,600.00,%s,4|15" % FM_RANDOM], frames="10,149")
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    expected = ("REFUSED: line %d: %s is accepted only for objects that start at frame 0 (its random path depends on the "
                "start frame)" % (fm_track_line(text, "X="), FM_RANDOM))
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out)
    # At the same rate (k = 1) the line is left alone, so the start frame is not checked.
    out = os.path.join(d, "same.aup2")
    code, _, err = convert_target(src, out, "30")
    assert code == 0, "exit %d: %s" % (code, err)
    assert set(line_changes(read_bytes(src), read_bytes(out))) == {line_in_section(text, "[project]", "file=")}
    assert read_report(out)["notes"] == [], read_report(out)["notes"]


def test_frame_mode_repeat_30_to_66_writes_a_non_integer_parameter():
    # k = 11/5: 7 * 11/5 = 77/5 = 15.4 exactly, so it is written as 15.4 (not rounded).
    d, src, text = fm_project("fm_repeat_66_15_4", 30, ["X=-600.00,600.00,%s,4|7" % FM_REPEAT])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "66")
    assert code == 0, "exit %d: %s" % (code, err)
    assert "X=-600.00,600.00,%s,4|15.4" % FM_REPEAT in read_text_lf(out).split("\n")
    assert read_report(out)["notes"] == ["trackbar at line %d: %s parameter 7 -> 15.4" % (fm_track_line(text, "X="), FM_REPEAT)]


def test_frame_mode_refusals_name_the_first_failing_rule():
    cases = (
        (["X=-600.00,4.00,%s,4|1" % FM_STEP], "0,119", "%s takes no '|' payload, got '4|1'" % FM_STEP),
        (["X=-600.00,600.00,%s,4" % FM_REPEAT], "0,119", "%s needs a '|' parameter, got '4'" % FM_REPEAT),
        (["X=-600.00,600.00,%s,4" % FM_RANDOM], "0,119", "%s needs a '|' parameter, got '4'" % FM_RANDOM),
        (["X=-600.00,600.00,%s,3|15" % FM_REPEAT], "0,119", "%s flag must be 4, got '3'" % FM_REPEAT),
        (["X=-600.00,4.00,%s,0" % FM_STEP], "0,119", "%s flag must be 4, got '0'" % FM_STEP),
        (["X=-600.00,600.00,%s,4|15" % FM_REPEAT], "0,59,119",
         "%s is accepted only with two frame points and two values (middle points are not verified)" % FM_REPEAT),
        (["X=-600.00,0.00,600.00,%s,4|15" % FM_REPEAT], "0,119",
         "%s is accepted only with two frame points and two values (middle points are not verified)" % FM_REPEAT),
        (["X=-600.00,600.00,%s,4|0.5" % FM_REPEAT], "0,119",
         "%s parameter must be a number of at least 1, got '0.5'" % FM_REPEAT),
        (["X=-600.00,600.00,%s,4|abc" % FM_REPEAT], "0,119",
         "%s parameter must be a number of at least 1, got 'abc'" % FM_REPEAT),
    )
    for n, (tracks, frames, message) in enumerate(cases):
        d, src, text = fm_project("fm_refuse_%d" % n, 30, tracks, frames=frames)
        out = os.path.join(d, "out.aup2")
        code, _, err = convert_target(src, out, "60")
        expected = "REFUSED: line %d: %s" % (fm_track_line(text, "X="), message)
        assert code == 2 and expected in err, "case %d: exit %d: %s" % (n, code, err)
        assert_no_output(out)
    # An object without a frame= line is refused by the same frame-point rule.
    d, src, text = fm_project("fm_refuse_no_frame", 30, ["X=-600.00,600.00,%s,4|15" % FM_REPEAT])
    no_frame_text = text.replace("frame=0,119\n", "", 1)  # removing the line shifts the X line up by one
    no_frame = os.path.join(d, "no_frame.aup2")
    write_bytes(no_frame, no_frame_text.encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(no_frame, out, "60")
    expected = ("REFUSED: line %d: %s is accepted only with two frame points and two values (middle points are not verified)"
                % (fm_track_line(no_frame_text, "X="), FM_REPEAT))
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_frame_mode_overlapping_time_control_refused_and_apart_accepted():
    # Two 時間制御(オブジェクト) objects on frames 0..119 and 30..149 (layers 0 and 1) and a 反復移動 object on 60..179
    # (layer 2) of the same scene: refused, and the message names the frame= line of the earliest time control object.
    blocks = [("0,119", tc_effect(TC_FORWARD)), ("30,149", tc_effect(TC_FORWARD)),
              ("60,179", "effect.name=標準描画\nX=-600.00,600.00,%s,4|15\n" % FM_REPEAT)]
    d, src, text = tc_project("fm_tc_overlap", 30, blocks)
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    expected = ("REFUSED: line %d: %s is not verified together with 時間制御(オブジェクト) (the time control object at line %d "
                "overlaps it)" % (line_in_section(text, "[2.0]", "X="), FM_REPEAT, line_in_section(text, "[0]", "frame=")))
    assert code == 2 and expected in err, "exit %d: %s" % (code, err)
    assert_no_output(out)
    # Frames 120..239 do not overlap 0..119: accepted, both objects are retimed.
    blocks = [("0,119", tc_effect(TC_FORWARD)), ("120,239", "effect.name=標準描画\nX=-600.00,600.00,%s,4|15\n" % FM_REPEAT)]
    d, src, text = tc_project("fm_tc_apart", 30, blocks)
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    expected = merged(file_edit(text, out), edit_at(text, "[scene.0]", "video.rate=", "video.rate=60"),
                      edit_at(text, "[0]", "frame=", "frame=0,239"),
                      edit_at(text, "[1]", "frame=", "frame=240,479"),
                      edit_at(text, "[1.0]", "X=", "X=-600.00,600.00,%s,4|30" % FM_REPEAT))
    assert line_changes(read_bytes(src), read_bytes(out)) == expected
    assert read_report(out)["notes"] == ["trackbar at line %d: %s parameter 15 -> 30" % (line_in_section(text, "[1.0]", "X="),
                                                                                      FM_REPEAT)]


def test_frame_mode_same_rate_is_unchanged():
    # k = 1: the lines are copied, with no note (the phase-1 checks still pass for these valid lines).
    d, src, text = fm_project("fm_same_30", 30, ["X=-600.00,600.00,%s,4|15" % FM_REPEAT, "Y=0.00,4.00,%s,4" % FM_STEP,
                                                 "Z=0.00,0.00,%s,4|15" % FM_RANDOM])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "30")
    assert code == 0, "exit %d: %s" % (code, err)
    assert set(line_changes(read_bytes(src), read_bytes(out))) == {line_in_section(text, "[project]", "file=")}
    assert read_report(out)["notes"] == [], read_report(out)["notes"]


def test_frame_mode_in_kept_scene_is_not_checked():
    # Scene 1 (15 fps) is kept at its own rate by the auto policy, so its frame-counting line is copied and not checked:
    # ランダム移動 with 15.4 would be refused in a followed scene. Scene 0 (the exported one) is followed: 反復移動 15.4 x 2 = 30.8.
    head = "[project]\nversion=2010400\nfile=synthetic.aup2\ndisplay.scene=0\npreview.scene=0\n"

    def scene(no, rate):
        return ("[scene.%d]\nscene=%d\nname=s%d\nvideo.width=640\nvideo.height=360\nvideo.rate=%d\nvideo.scale=1\n"
                "audio.rate=44100\ncursor.frame=0\ncursor.layer=0\npreview.frame=0\ndisplay.frame=0\ndisplay.layer=0\n"
                % (no, no, no, rate))

    text = (head + scene(0, 30) + "[0]\nlayer=0\nframe=0,119\n[0.0]\neffect.name=標準描画\nX=-600.00,600.00,%s,4|15.4\n"
            % FM_REPEAT + scene(1, 15) + "[1]\nlayer=0\nscene=1\nframe=0,59\n[1.0]\neffect.name=標準描画\n"
            "X=-600.00,600.00,%s,4|15.4\n" % FM_RANDOM)
    d = case_dir("fm_kept_scene")
    src = os.path.join(d, "src.aup2")
    write_bytes(src, text.encode("utf-8"))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    expected = merged(file_edit(text, out), edit_at(text, "[scene.0]", "video.rate=", "video.rate=60"),
                      edit_at(text, "[0]", "frame=", "frame=0,239"),
                      edit_at(text, "[0.0]", "X=", "X=-600.00,600.00,%s,4|30.8" % FM_REPEAT))
    assert line_changes(read_bytes(src), read_bytes(out)) == expected
    assert read_report(out)["notes"] == ["scene 1 kept at its own rate 15/1",
                                         "trackbar at line %d: %s parameter 15.4 -> 30.8"
                                         % (line_in_section(text, "[0.0]", "X="), FM_REPEAT)], read_report(out)["notes"]


def test_frame_mode_precise_skips_the_object_and_still_converts_the_parameter():
    d, src, text = fm_project("fm_precise", 30, ["X=-600.00,600.00,%s,4|15" % FM_REPEAT,
                                                "Y=0.00,0.00,%s,4|15" % FM_REPEAT])
    out_p = os.path.join(d, "precise.aup2")
    out_n = os.path.join(d, "plain.aup2")
    code, _, err = convert_target(src, out_p, "60", extra=["--precise"])
    assert code == 0, "exit %d: %s" % (code, err)
    code, _, err = convert_target(src, out_n, "60")
    assert code == 0, "exit %d: %s" % (code, err)
    report = read_report(out_p)["precise"]
    x_line = fm_track_line(text, "X=")
    assert report["applied"] == 0 and report["tracks"] == [], report
    assert report["skipped"] == [{"line": line_in_section(text, "[0]", "frame="), "reason":
                                  "trackbar at line %d uses mode %s; frame-counting modes are not re-sampled"
                                  % (x_line, FM_REPEAT)}], report["skipped"]
    assert lines_without_file(out_p) == lines_without_file(out_n), "a skipped object keeps the plain output"
    assert "X=-600.00,600.00,%s,4|30" % FM_REPEAT in lines_without_file(out_p)


# ---------------------------------------------------------------------------
# Sub-scene alignment (scene objects), frame-dependent effects and scene ids
# ---------------------------------------------------------------------------

PROJECT_HEAD = "[project]\nversion=2010400\nfile=synthetic.aup2\ndisplay.scene=%d\npreview.scene=0\n"
FRAME_EFFECT_NAMES = ["手ぶれ", "振動", "ラスター", "フレア", "ノイズ"]
FRAME_EFFECT_TEXT = ("REFUSED: line %d: effect '%s' depends on the frame number or a per-frame random value "
                     "and is not verified at another frame rate; refused")
ALIGN_WARNING = ("WARNING: scene %d cannot be aligned to the exported scene (%s); object boundaries inside it may be "
                 "one frame off at this rate")
ALIGN_NOTE = "scene %d is rescaled on the timeline of its scene object at line %d"
# sha256 of the integer-k output (66 -> 132, USER_CASE below) written by the version before scene ids and alignment.
ALIGN_INTEGER_GOLDEN = "3a23a8ebcb30d39e1288d6d036e620c2bed4455393521cd474be3c0e62d360a6"


def scene_object(played, position="0.000,5.819,再生範囲,0", speed="100.00", loop="0"):
    """Body of a シーン effect section playing scene `played`; a line whose value is None is left out."""
    body = "effect.name=シーン\n"
    if position is not None:
        body += "再生位置=%s\n" % position
    if speed is not None:
        body += "再生速度=%s\n" % speed
    body += "シーン=%s\n" % played
    if loop is not None:
        body += "ループ再生=%s\n" % loop
    return body


def scene_section(number, scene_id, rate):
    """[scene.N] section; without scene_id it has no scene= line and its id is N."""
    head = "[scene.%d]\n" % number
    if scene_id is not None:
        head += "scene=%d\n" % scene_id
    ident = number if scene_id is None else scene_id
    return head + ("name=scene %d\nvideo.width=640\nvideo.height=360\nvideo.rate=%d\nvideo.scale=1\naudio.rate=44100\n"
                   "cursor.frame=0\ncursor.layer=0\npreview.frame=0\ndisplay.frame=0\ndisplay.layer=0\n" % (ident, rate))


def align_project(name, sections, objects, display_scene=0):
    """Write a project into a new case dir. Returns (dir, src_path, text).

    sections: (section number, scene id or None, video.rate) in file order. objects: (scene id, layer, frames, effect
    bodies); the object number is its index. Each object is written with its scene= line right after the section of its
    scene id, followed by its effect sections (each body is the text after its [n.j] header).
    """
    parts = [PROJECT_HEAD % display_scene]
    placed = set()  # an object is written once, after the first section of its scene id
    for number, scene_id, rate in sections:
        parts.append(scene_section(number, scene_id, rate))
        ident = number if scene_id is None else scene_id
        for no, (obj_scene, layer, frames, effects) in enumerate(objects):
            if obj_scene == ident and no not in placed:
                placed.add(no)
                parts.append("[%d]\nlayer=%d\nscene=%d\nfocus=0\nframe=%s\n" % (no, layer, obj_scene, frames))
                for j, body in enumerate(effects):
                    parts.append("[%d.%d]\n%s" % (no, j, body))
    assert len(placed) == len(objects), "an object has no section with its scene id"
    text = "".join(parts)
    d = case_dir(name)
    src = os.path.join(d, "src.aup2")
    write_bytes(src, text.encode("utf-8"))
    return d, src, text


def frames_of(path):
    return {no: obj["frames"] for no, obj in parse_objects(read_text_lf(path)).items()}


# The 66 fps case of the user report: scene 0 has a scene object at frames 480,863 playing scene 1 and a 552,575 object;
# scene 1 (66 fps) has 0,71 and 72,143. Objects 2 and 3 are scene 1's.
USER_CASE_SECTIONS = [(0, None, 66), (1, None, 66)]


def user_case_objects(player_body, player_frames="480,863"):
    return [
        (0, 6, player_frames, [player_body]),
        (0, 11, "552,575", []),
        (1, 0, "0,71", []),
        (1, 0, "72,143", []),
    ]


def test_scene_object_aligns_the_sub_scene_user_case():
    d, src, text = align_project("align_user", USER_CASE_SECTIONS, user_case_objects(scene_object(1)))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 0 and err == "", "exit %d: %s" % (code, err)
    # Scene 1 is shown from frame 480 of scene 0, so its frames are rounded on that timeline: frame 72 starts at 66
    # (plain rounding gives 65, one frame before the scene object's own change at 502).
    assert frames_of(out) == {0: [436, 784], 1: [502, 523], 2: [0, 65], 3: [66, 130]}, frames_of(out)
    player_line = line_in_section(text, "[0]", "frame=")
    assert read_report(out)["notes"] == [ALIGN_NOTE % (1, player_line)], read_report(out)["notes"]


def test_two_scene_objects_playing_one_scene_warn_and_keep_plain_rounding():
    objects = user_case_objects(scene_object(1)) + [
        (0, 7, "100,200", [scene_object(1, position="0.000,1.000,再生範囲,0")])]
    d, src, _ = align_project("align_two_players", USER_CASE_SECTIONS, objects)
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    warning = ALIGN_WARNING % (1, "it is played by 2 scene objects")
    assert code == 0 and warning in err, "exit %d: %s" % (code, err)
    assert read_report(out)["notes"] == [warning], read_report(out)["notes"]
    assert frames_of(out)[2] == [0, 64] and frames_of(out)[3] == [65, 130], frames_of(out)


def test_scene_object_rules_name_the_first_failed_rule():
    user = USER_CASE_SECTIONS
    plain = {2: [0, 64], 3: [65, 130]}
    cases = [
        ("R2 player outside the exported scene", [(0, None, 66), (1, None, 66), (2, None, 66)],
         [(0, 6, "480,863", []), (0, 11, "552,575", []), (2, 6, "480,863", [scene_object(1)]),
          (1, 0, "0,71", []), (1, 0, "72,143", [])],
         "its scene object is not in the exported scene", {3: [0, 64], 4: [65, 130]}),
        ("R3 three frame points", user, user_case_objects(scene_object(1), player_frames="480,600,863"),
         "its scene object has more than two frame points", plain),
        ("R4 speed 200", user, user_case_objects(scene_object(1, speed="200.00")),
         "its scene object does not play at speed 100 without loop", plain),
        ("R4 loop 1", user, user_case_objects(scene_object(1, loop="1")),
         "its scene object does not play at speed 100 without loop", plain),
        ("R5 position 0.010", user, user_case_objects(scene_object(1, position="0.010")),
         "its scene object starts at a position that is not a whole frame at both rates", plain),
        ("R5 negative position", user, user_case_objects(scene_object(1, position="-0.010")),
         "its scene object starts at a position that is not a whole frame at both rates", plain),
        ("R5 trackbar in another mode", user, user_case_objects(scene_object(1, position="0.000,5.819,直線移動,0")),
         "its scene object starts at a position that is not a whole frame at both rates", plain),
        ("R6 time control in the scene", user, user_case_objects(scene_object(1)) + [(1, 3, "0,71", [tc_effect()])],
         "it contains a time control object", plain),
    ]
    for n, (label, sections, objects, reason, expected) in enumerate(cases):
        d, src, _ = align_project("align_rule_%d" % n, sections, objects)
        out = os.path.join(d, "out.aup2")
        code, _, err = convert_target(src, out, "60")
        warning = ALIGN_WARNING % (1, reason)
        assert code == 0 and warning in err, "%s: exit %d: %s" % (label, code, err)
        notes = read_report(out)["notes"]
        assert [n for n in notes if not n.startswith("time control")] == [warning], "%s: %r" % (label, notes)
        got = frames_of(out)
        assert {no: got[no] for no in expected} == expected, "%s: %r" % (label, got)


def test_scene_object_with_integer_k_is_not_aligned_and_matches_the_previous_output():
    d, src, _ = align_project("align_integer", USER_CASE_SECTIONS, user_case_objects(scene_object(1)))
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "132")
    assert code == 0 and err == "", "exit %d: %s" % (code, err)
    assert frames_of(out) == {0: [960, 1727], 1: [1104, 1151], 2: [0, 143], 3: [144, 287]}, frames_of(out)
    assert read_report(out)["notes"] == [], read_report(out)["notes"]
    got = hashlib.sha256(without_file_line(read_bytes(out))).hexdigest()
    assert got == ALIGN_INTEGER_GOLDEN, "output differs from the previous version (sha256 %s)" % got


def test_scene_object_precise_shifts_the_keyframes_of_the_aligned_scene():
    d, src, text = align_project("align_precise", USER_CASE_SECTIONS, [
        (0, 6, "480,863", [scene_object(1)]),
        (0, 11, "552,575", []),
        (1, 0, "0,71", ["effect.name=標準描画\nX=0.00,100.00,直線移動,0\n"]),
    ])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60", extra=("--precise",))
    assert code == 0 and err == "", "exit %d: %s" % (code, err)
    # Scene 1 frame v is shown at scene 0 frame 480 + v. The keyframes are those of the plain rescale on the shifted
    # timeline: 0, 1 and 64 (the line is held at its end value from 64 on) and 65 (the last frame).
    assert frames_of(out)[2] == [0, 1, 64, 65], frames_of(out)[2]
    out_text = read_text_lf(out)
    assert line_text_value(out_text, "[2.0]", "X=") == "0.00,0.99,98.59,100.00,直線移動,0", \
        line_text_value(out_text, "[2.0]", "X=")
    assert read_report(out)["notes"] == [ALIGN_NOTE % (1, line_in_section(text, "[0]", "frame="))], \
        read_report(out)["notes"]


def test_frame_dependent_effects_are_refused_when_k_is_not_one():
    for n, name in enumerate(FRAME_EFFECT_NAMES):
        d = case_dir("frame_effect_refused_%d" % n)
        src = os.path.join(d, "src.aup2")
        text = read_text_lf(FIX_30).replace("effect.name=標準描画", "effect.name=%s" % name, 1)
        write_bytes(src, text.encode("utf-8"))
        out = os.path.join(d, "out.aup2")
        code, _, err = convert(src, out, 2)
        line = line_number_of(src, "effect.name=%s" % name)
        assert code == 2 and err.strip() == FRAME_EFFECT_TEXT % (line, name), "%s: exit %d: %s" % (name, code, err)
        assert_no_output(out)


def test_frame_dependent_effects_pass_at_the_same_rate_and_in_kept_scenes():
    for n, name in enumerate(FRAME_EFFECT_NAMES):
        d = case_dir("frame_effect_same_%d" % n)
        src = os.path.join(d, "src.aup2")
        text = read_text_lf(FIX_30).replace("effect.name=標準描画", "effect.name=%s" % name, 1)
        write_bytes(src, text.encode("utf-8"))
        out = os.path.join(d, "out.aup2")
        code, _, err = convert_target(src, out, "30")
        assert code == 0 and err == "", "%s: exit %d: %s" % (name, code, err)
        assert "effect.name=%s" % name in read_text_lf(out), name
    d, src, _ = align_project("frame_effect_kept", [(0, None, 30), (1, None, 15)],
                              [(0, 0, "0,119", []), (1, 0, "0,59", ["effect.name=手ぶれ\n"])])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 0 and err == "", "exit %d: %s" % (code, err)
    assert "effect.name=手ぶれ" in read_text_lf(out) and frames_of(out)[1] == [0, 59], frames_of(out)
    assert read_report(out)["notes"] == ["scene 1 kept at its own rate 15/1"], read_report(out)["notes"]


# Scene ids: [scene.1] is scene 2 and [scene.2] is scene 1 (rates 15 and 60); scene 0 is the exported 30 fps scene.
ID_SECTIONS = [(0, 0, 30), (1, 2, 15), (2, 1, 60)]
ID_OBJECTS = [(0, 0, "0,119", []), (2, 0, "0,59", []), (1, 0, "0,59", [])]


def test_scene_id_selects_the_plan_of_each_object():
    d, src, _ = align_project("ids_swapped", ID_SECTIONS, ID_OBJECTS)
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60", extra=("--subscene-rate", "follow"))
    assert code == 0 and err == "", "exit %d: %s" % (code, err)
    # Scene 2 (15 fps, k = 4) and scene 1 (60 fps, k = 1): the objects written with scene=2 and scene=1 follow them.
    assert frames_of(out) == {0: [0, 239], 1: [0, 239], 2: [0, 59]}, frames_of(out)
    assert_subscene_report(read_report(out), "follow", [
        (0, "scene 0", "30/1", "60/1", "2/1", "follow", True),
        (1, "scene 1", "60/1", "60/1", "1/1", "follow", False),
        (2, "scene 2", "15/1", "60/1", "4/1", "follow", False),
    ], [])


def test_scene_id_object_uses_its_scene_when_the_section_number_differs():
    d, src, _ = align_project("ids_5", [(0, 0, 30), (1, 5, 15)], [(0, 0, "0,119", []), (5, 0, "0,59", [])])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    assert code == 0 and err == "", "exit %d: %s" % (code, err)
    # Scene 5 is kept at 15 fps; an object that wrongly took the exported plan would become 0,239.
    assert frames_of(out) == {0: [0, 239], 1: [0, 59]}, frames_of(out)
    assert_subscene_report(read_report(out), "auto", [
        (0, "scene 0", "30/1", "60/1", "2/1", "follow", True),
        (5, "scene 5", "15/1", "15/1", "1/1", "keep", False),
    ], ["scene 5 kept at its own rate 15/1"])


def test_display_scene_naming_another_section_is_ambiguous():
    d, src, text = align_project("ids_ambiguous", ID_SECTIONS, ID_OBJECTS, display_scene=1)
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60")
    line = line_in_section(text, "[project]", "display.scene=")
    assert code == 2 and err.strip() == ("REFUSED: line %d: display.scene 1 is ambiguous: [scene.1] has scene=2" % line), \
        "exit %d: %s" % (code, err)
    assert_no_output(out)


def test_scene_id_must_be_a_non_negative_integer_and_unique():
    d, src, text = align_project("ids_bad_value", [(0, 0, 30), (1, 1, 15)], [(0, 0, "0,119", []), (1, 0, "0,59", [])])
    out = os.path.join(d, "out.aup2")
    edited = text.replace("[scene.1]\nscene=1\n", "[scene.1]\nscene=abc\n", 1)
    assert edited != text
    src_bad = os.path.join(d, "bad.aup2")
    write_bytes(src_bad, edited.encode("utf-8"))
    code, _, err = convert_target(src_bad, out, "60")
    line = line_in_section(edited, "[scene.1]", "scene=")
    assert code == 2 and err.strip() == ("REFUSED: line %d: scene= in [scene.1] must be a non-negative integer, got 'abc'"
                                         % line), "exit %d: %s" % (code, err)
    assert_no_output(out)

    d2, _, text2 = align_project("ids_dup_line", [(0, 0, 30), (1, 0, 15)], [(0, 0, "0,119", [])])
    code, _, err = convert_target(os.path.join(d2, "src.aup2"), os.path.join(d2, "out.aup2"), "60")
    line = line_in_section(text2, "[scene.1]", "scene=")
    assert code == 2 and err.strip() == ("REFUSED: line %d: scene id 0 is used by more than one [scene.N] section"
                                         % line), "exit %d: %s" % (code, err)

    d3, _, _ = align_project("ids_dup_default", [(0, 1, 30), (1, None, 15)], [(1, 0, "0,119", [])])
    code, _, err = convert_target(os.path.join(d3, "src.aup2"), os.path.join(d3, "out.aup2"), "60")
    assert code == 2 and err.strip() == "REFUSED: scene id 1 is used by more than one [scene.N] section", \
        "exit %d: %s" % (code, err)


def test_section_without_scene_line_has_the_section_number_as_id():
    d, src, _ = align_project("ids_default", [(0, None, 30), (1, None, 15)], [(0, 0, "0,119", []), (1, 0, "0,59", [])])
    out = os.path.join(d, "out.aup2")
    code, _, err = convert_target(src, out, "60", extra=("--subscene-rate", "follow"))
    assert code == 0 and err == "", "exit %d: %s" % (code, err)
    assert frames_of(out) == {0: [0, 239], 1: [0, 239]}, frames_of(out)
    assert_subscene_report(read_report(out), "follow", [
        (0, "scene 0", "30/1", "60/1", "2/1", "follow", True),
        (1, "scene 1", "15/1", "60/1", "4/1", "follow", False),
    ], [])


TESTS = [
    test_target_fps_exact_values_30fps_fixture,
    test_target_fps_synthetic_60_to_66_and_72_to_60,
    test_target_fps_shortens_one_frame_object_and_notes_it,
    test_target_fps_intermediate_collision_refused_with_line,
    test_target_fps_touching_neighbour_refused_when_shortened,
    test_target_fps_rejections,
    test_target_fps_inserts_missing_video_scale,
    test_target_fps_same_layer_in_two_scenes_is_not_grouped,
    test_target_fps_touching_across_scenes_is_not_checked,
    test_target_fps_same_scene_overlap_after_rescaling_refused,
    test_retime_timing_properties,
    test_real_world_target_fps_66_and_50,
    test_30fps_x2_changes_only_expected_lines,
    test_2112_file_with_rate_reset_to_30_x2,
    test_66_and_120_fixtures_x2,
    test_intermediate_points_rescaled,
    test_intermediate_points_non_increasing_refused,
    test_refuse_unknown_scene_frame_key,
    test_refuse_script_effect,
    test_refuse_counter_effect,
    test_koma_ochi_one_passes_unchanged,
    test_refuse_koma_ochi_not_a_positive_integer,
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
    test_precise_worked_examples_end_values,
    test_precise_property_values_and_speed,
    test_precise_skips_ineligible_objects_and_ignores_playback_range,
    test_precise_skips_zero_length_two_point_object,
    test_precise_note_when_quantization_error_is_large,
    test_precise_off_by_default_adds_no_json_fields,
    test_precise_extra_decimals_2_meets_speed_target_on_short_segments,
    test_precise_extra_decimals_0_is_the_default,
    test_precise_extra_decimals_refusals,
    test_precise_real_world_66_read_only,
    test_precise_holds_the_end_value_frame_by_frame,
    test_precise_start_before_the_original_start_holds_the_start_value,
    test_precise_multi_point_object_keeps_inner_points_and_holds_the_end,
    test_precise_leaves_an_object_with_a_time_control_effect_alone,
    test_precise_hold_changes_only_the_expected_lines,
    test_subscene_auto_follows_60_scene_when_main_is_30,
    test_subscene_auto_keeps_15_scene_at_its_own_rate,
    test_subscene_follow_rescales_15_scene_by_4,
    test_subscene_keep_holds_60_scene_when_target_is_120,
    test_subscene_nested_auto_follows_60_and_keeps_15,
    test_subscene_nested_precise_resamples_follow_scene_only,
    test_subscene_display_scene_1_sets_k_and_source_rate,
    test_subscene_factor_mode_uses_display_scene_rate,
    test_subscene_display_scene_missing_refused,
    test_subscene_default_policy_is_auto,
    test_subscene_rate_unknown_value_is_usage_error,
    test_same_rate_auto_output_matches_previous_version,
    test_time_control_arz_style_66_to_60_values,
    test_time_control_30_to_60_forward_and_reversed,
    test_time_control_30_to_66_forward_and_reversed,
    test_time_control_66_to_60_reversed_block,
    test_time_control_position_decimals_and_rounding,
    test_time_control_single_frame_object_and_single_frame_result,
    test_time_control_same_rate_is_unchanged,
    test_time_control_frame_drop_scales_with_the_rate,
    test_time_control_frame_drop_not_exact_becomes_a_staircase,
    test_time_control_frame_drop_invalid_values_refused,
    test_frame_drop_above_one_outside_time_control_is_refused,
    test_time_control_position_shapes_are_refused,
    test_time_control_mode_is_accepted_outside_time_control_effect,
    test_time_control_in_kept_scene_is_untouched,
    test_time_control_uses_the_ratio_of_its_own_scene,
    test_time_control_precise_leaves_time_control_alone,
    test_time_control_matches_the_original_at_every_frame,
    test_time_control_staircase_limit_is_4096_points,
    test_time_control_staircase_other_trackbars,
    test_time_control_controlled_layers_show_the_same_values,
    test_time_control_p6_probe_66_to_132_matches_the_original,
    test_projects_without_time_control_match_previous_version,
    test_easing_mode_and_payload_pass_unchanged,
    test_easing_multi_point_frames_rescaled_values_kept,
    test_easing_interpolated_version_and_other_payloads_still_refused,
    test_easing_precise_skips_the_object_and_keeps_values,
    test_tc_mode_ordinary_track_two_and_multi_point,
    test_tc_mode_ordinary_track_count_mismatch_refused,
    test_payload_kept_for_the_three_modes_and_comma_payloads,
    test_payload_other_modes_still_refused_with_comma_payload,
    test_precise_linear_payload_is_resampled_and_kept,
    test_precise_tc_mode_two_points_resampled_without_extra_keyframes,
    test_precise_tc_mode_needing_keyframes_curve_or_multi_point_is_skipped,
    test_time_control_curve_is_kept,
    test_time_control_curve_frame_drop,
    test_time_control_curve_precise_and_same_rate,
    test_interp_tc_mode_passes_keeps_payload_and_multi_point,
    test_precise_interp_tc_mode_always_skips,
    test_frame_mode_repeat_30_to_60_doubles_the_parameter,
    test_frame_mode_repeat_30_to_60000_1001_writes_29_97003_within_tolerance,
    test_frame_mode_repeat_error_bound_refuses_a_parameter_that_rounds_too_far,
    test_frame_mode_random_30_to_66_scales_the_integer_parameter,
    test_frame_mode_random_non_integer_after_k_and_non_integer_parameter_refused,
    test_frame_mode_step_30_to_60_halves_the_step_and_keeps_zero_steps,
    test_frame_mode_step_30_to_66_two_decimals_refused,
    test_frame_mode_step_30_to_66_three_decimals_within_tolerance,
    test_frame_mode_step_30_to_60000_1001_two_decimals_refused,
    test_frame_mode_step_written_with_no_decimals_when_source_has_none,
    test_frame_mode_random_starting_after_frame_0_refused_and_same_rate_kept,
    test_frame_mode_repeat_30_to_66_writes_a_non_integer_parameter,
    test_frame_mode_refusals_name_the_first_failing_rule,
    test_frame_mode_overlapping_time_control_refused_and_apart_accepted,
    test_frame_mode_same_rate_is_unchanged,
    test_frame_mode_in_kept_scene_is_not_checked,
    test_frame_mode_precise_skips_the_object_and_still_converts_the_parameter,
    test_scene_object_aligns_the_sub_scene_user_case,
    test_two_scene_objects_playing_one_scene_warn_and_keep_plain_rounding,
    test_scene_object_rules_name_the_first_failed_rule,
    test_scene_object_with_integer_k_is_not_aligned_and_matches_the_previous_output,
    test_scene_object_precise_shifts_the_keyframes_of_the_aligned_scene,
    test_frame_dependent_effects_are_refused_when_k_is_not_one,
    test_frame_dependent_effects_pass_at_the_same_rate_and_in_kept_scenes,
    test_scene_id_selects_the_plan_of_each_object,
    test_scene_id_object_uses_its_scene_when_the_section_number_differs,
    test_display_scene_naming_another_section_is_ambiguous,
    test_scene_id_must_be_a_non_negative_integer_and_unique,
    test_section_without_scene_line_has_the_section_number_as_id,
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
        for fixture in (FIX_30, FIX_66, FIX_120, FIX_2112, TOOL, MIXED_30_60, MIXED_30_15, SAME_30_30,
                        NESTED_30_60_15, DISPLAY_SCENE_1, TIME_CONTROL_66):
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
