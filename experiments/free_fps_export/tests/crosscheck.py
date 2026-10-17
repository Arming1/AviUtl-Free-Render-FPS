"""Cross-check the C++ re-timer (retime_cli.exe) against tools/aup2_fps_upconvert.py.

Inputs: every experiments/66_to_60_validation/*.aup2, experiments/free_fps_matrix/fixture_60fps.aup2 and
fixture_72fps.aup2, experiments/host_matrix/scene_rate_test.aup2, and the read-only real project
D:\\ARZv2.0.1\\ARZv2.0.1\\sources\\試験用動画.aup2 (copied into the work directory first).
Targets: --target-fps 66, 60, 50, 59, 24, 120, 60000/1001, 30000/1001 and --factor 2, 3.
Precise: --precise with --target-fps 66, 24, 60000/1001, 50, 59, 120 on every fixture, and 66 on the real project;
--precise-extra-decimals 2 with 66 on every input, and two refusals for it (when the Python tool has the option).
Skipped (with a message) while tools/aup2_fps_upconvert.py does not accept --precise or the option.
Refusal cases: synthetic inputs written to the work directory, one per refusal, each with its own arguments.

Each case runs both tools one after the other with the same absolute --out path. Exit codes, stderr (the
refusal text) and the .aup2 and .convert.json files must match exactly. The C++ and Python refusal texts are now
identical (no known difference); any difference is reported.

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

TARGETS = [["--target-fps", rate] for rate in ("66", "60", "50", "59", "24", "120", "60000/1001", "30000/1001")]
TARGETS += [["--factor", "2"], ["--factor", "3"]]

PRECISE_TARGETS = [["--target-fps", rate] for rate in ("66", "24", "60000/1001", "50", "59", "120")]
PRECISE_REAL_TARGET = ["--target-fps", "66"]

BASE_BODY = ["[0]", "layer=0", "frame=0,299"]


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
    ("coma_ochi", synthetic_text(BASE_BODY + ["コマ落ち=1"]), ["--target-fps", "66"]),
    ("pipe_payload", synthetic_text(effect_body("effect.name=標準描画", "X=0.00,100.00,直線移動,0|27")),
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


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    sys.stderr.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--exe", default=os.path.join(EXPERIMENT, "build", "x64", "retime_cli.exe"))
    parser.add_argument("--work", default=DEFAULT_WORK, help="scratch directory (emptied at start)")
    parser.add_argument("--keep", action="store_true", help="keep the work directory after the run")
    args = parser.parse_args(argv)

    exe = os.path.abspath(args.exe)
    work = os.path.abspath(args.work)
    if not os.path.isfile(exe):
        print("SKIP: retime_cli not built at %s (run build_and_run.ps1 first)" % exe)
        return 2
    if not os.path.isfile(PY_TOOL):
        print("SKIP: Python tool missing at %s" % PY_TOOL)
        return 2
    if not os.path.isfile(REAL_PROJECT):
        print("SKIP: real project missing at %s" % REAL_PROJECT)
        return 2
    if not python_tool_supports_target_fps():
        print("SKIP: tools/aup2_fps_upconvert.py has no --target-fps; the lead must rerun this check later")
        return 2

    shutil.rmtree(work, ignore_errors=True)
    synthetic_dir = os.path.join(work, "synthetic")
    os.makedirs(synthetic_dir)
    real_copy = os.path.join(work, "real_project.aup2")
    try:
        shutil.copyfile(REAL_PROJECT, real_copy)

        # (case label, input path, extra arguments, kind) for the cross product, then the refusal cases.
        cases = []
        for label, path in convert_inputs() + [("real ARZv2.0.1 project", real_copy)]:
            for target in TARGETS:
                cases.append((label, path, target))
        refusal_cases_extra = []
        if python_tool_supports_precise():
            for label, path in convert_inputs():
                for target in PRECISE_TARGETS:
                    cases.append((label, path, target + ["--precise"]))
            cases.append(("real ARZv2.0.1 project", real_copy, PRECISE_REAL_TARGET + ["--precise"]))
            if python_tool_has_option("--precise-extra-decimals"):
                for label, path in convert_inputs():
                    cases.append((label, path, ["--target-fps", "66", "--precise", "--precise-extra-decimals", "2"]))
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

    matches = sum(1 for row in rows if not row[4])
    converted = sum(1 for row in rows if row[3] == 0)
    refused = sum(1 for row in rows if row[3] != 0)
    mismatches = [row for row in rows if row[4]]

    print("%4s  %-40s %-24s %-9s %s" % ("No", "Input", "Arguments", "Result", "Detail"))
    for number, label, arg_text, py_rc, problems in rows:
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
    print("cleanup: real project copy removed; work directory %s"
          % ("kept at %s" % work if args.keep else "removed"))
    if not cleaned:
        print("cleanup: WARNING, some files remain")
    print("ALL MATCH" if not mismatches else "MISMATCH: %d case(s)" % len(mismatches))
    return 0 if not mismatches else 1


if __name__ == "__main__":
    sys.exit(main())
