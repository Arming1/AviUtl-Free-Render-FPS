#!/usr/bin/env python3
"""FreeRenderFPS FPS matrix driver for an AviUtl2 auto-export run.

prepare   Writes a run directory: one .aup2 copy per matrix case, the
          auto-export job file (AUTO_EXPORT_JOBS) and cases.json.
          FreeRenderFPS settings are injected only from a template project that
          the user saved once in AviUtl2 with the desired FreeRenderFPS config.
validate  Reads cases.json and the auto-export results TSV, runs
          tools/validate_freefps_output.py (python -I) on each exported MP4 and
          writes SUMMARY.md and summary.json into the run directory.

See tools/host_matrix.md for the manual steps between the two subcommands.
"""

from __future__ import annotations

import argparse
import copy
import datetime
import json
import math
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FIXTURE_DIR = REPO / "experiments" / "66_to_60_validation"
VALIDATOR = REPO / "tools" / "validate_freefps_output.py"
PLUGIN_NAME = "x264guiEx FreeRenderFPS"
CONFIG_KEY = "freerenderfps_config"
JOB_FILE_NAME = "auto_export_jobs.tsv"
RESULTS_FILE_NAME = JOB_FILE_NAME + ".results.tsv"
SCHEMA = 1
CHECK_ORDER = ["frame_count", "stream_rate", "av_duration", "no_pairing", "near_duplicates"]

RATIONAL_RE = re.compile(r"^(\d+)(?:/(\d+))?$")
SECTION_RE = re.compile(r"^\[([^\[\]]+)\]$")
FRAME_RE = re.compile(r"^(\d+),(\d+)$")


class ToolError(Exception):
    """Input or environment problem: message goes to stderr, exit code 2."""


@dataclass(frozen=True)
class Case:
    case_id: str
    label: str
    fixture: str       # key into FIXTURES
    project_fps: str   # rational; must equal the fixture's video.rate / video.scale
    target_fps: str    # rational; FreeFPS target (equal to project fps when OFF)
    enabled: bool


FIXTURES = {
    "30": "freerenderfps_30fps.aup2",
    "66": "freerenderfps_66fps.aup2",
    "66_60s": "freerenderfps_66fps_60s.aup2",
    "120": "freerenderfps_120fps.aup2",
}

CASES = (
    Case("off_66_66", "FreeFPS OFF 66 -> 66", "66", "66", "66", False),
    Case("30_60", "30 -> 60", "30", "30", "60", True),
    Case("66_60", "66 -> 60", "66", "66", "60", True),
    Case("66_5994", "66 -> 60000/1001", "66", "66", "60000/1001", True),
    Case("66_24", "66 -> 24", "66", "66", "24", True),
    Case("66_120", "66 -> 120", "66", "66", "120", True),
    Case("120_60", "120 -> 60", "120", "120", "60", True),
    Case("30_120", "30 -> 120", "30", "30", "120", True),
    Case("66_5994_60s", "66 -> 60000/1001, 60 s", "66_60s", "66", "60000/1001", True),
)


# ---------------------------------------------------------------- rationals

def parse_rational(text: str) -> Fraction:
    match = RATIONAL_RE.match(text)
    if not match:
        raise ToolError(f"not a rational frame rate: {text!r}")
    num, den = int(match.group(1)), int(match.group(2) or 1)
    if num <= 0 or den <= 0:
        raise ToolError(f"frame rate must be positive: {text!r}")
    return Fraction(num, den)


def split_rational(text: str) -> tuple[int, int]:
    if "/" in text:
        num, den = text.split("/", 1)
        return int(num), int(den)
    return int(text), 1


def expected_frames(project_frames: int, project_fps: str, target_fps: str) -> int:
    # Same rule as validate_freefps_output.py: ceil(project_frames * target / project).
    return math.ceil(Fraction(project_frames) * parse_rational(target_fps) / parse_rational(project_fps))


# ---------------------------------------------------------------- .aup2 text

def read_text_lines(path: Path) -> tuple[list[str], str]:
    """Return LF-normalised lines (no line terminators) and the file's EOL style."""
    text = path.read_bytes().decode("utf-8")
    eol = "\r\n" if "\r\n" in text else "\n"
    return text.replace("\r\n", "\n").split("\n"), eol


def join_text_lines(lines: list[str], eol: str) -> bytes:
    return eol.join(lines).encode("utf-8")


def find_header(lines: list[str], section: str) -> int:
    for index, line in enumerate(lines):
        if line == f"[{section}]":
            return index
    raise ToolError(f"section [{section}] not found")


def section_end(lines: list[str], start: int) -> int:
    for index in range(start + 1, len(lines)):
        if SECTION_RE.match(lines[index]):
            return index
    return len(lines)


def set_value(lines: list[str], section: str, key: str, value: str) -> None:
    start = find_header(lines, section)
    end = section_end(lines, start)
    prefix = key + "="
    for index in range(start + 1, end):
        if lines[index].startswith(prefix):
            lines[index] = prefix + value
            return
    raise ToolError(f"[{section}] has no {key}= line to rewrite")


def scan_fixture(lines: list[str]) -> tuple[int, int, int]:
    """Return (video.rate, video.scale, frame count) from a fixture's text."""
    section = None
    rate = scale = None
    frame_max = None
    for line in lines:
        header = SECTION_RE.match(line)
        if header:
            section = header.group(1)
            continue
        key, sep, value = line.partition("=")
        if not sep or section is None:
            continue
        if section.startswith("scene.") and key == "video.rate":
            rate = int(value)
        elif section.startswith("scene.") and key == "video.scale":
            scale = int(value)
        elif section.isdigit() and key == "frame":
            match = FRAME_RE.match(value)
            if match:
                frame_max = max(frame_max if frame_max is not None else -1, int(match.group(2)))
    if rate is None or scale is None or frame_max is None:
        raise ToolError("fixture lacks video.rate/video.scale or a layer frame= range")
    return rate, scale, frame_max + 1


def read_template(path: Path) -> tuple[str, dict]:
    """Find the single freerenderfps_config line in a user-saved project.

    Returns (section name holding the line, parsed JSON). Refuses values with a
    backslash because the .aup2 text escaping for strings is not verified.
    """
    lines, _ = read_text_lines(path)
    section = None
    found: list[tuple[str, str]] = []
    for line in lines:
        header = SECTION_RE.match(line)
        if header:
            section = header.group(1)
            continue
        if line.startswith(CONFIG_KEY + "="):
            found.append((section, line[len(CONFIG_KEY) + 1:]))
    if len(found) != 1:
        raise ToolError(f"template {path} has {len(found)} {CONFIG_KEY} lines; expected exactly 1")
    section, value = found[0]
    if section is None:
        raise ToolError("template config line is not inside a section")
    if "\\" in value:
        raise ToolError("template config value contains a backslash (path or escaped text); "
                        "its .aup2 escaping is unverified. Save the template with empty path fields.")
    try:
        data = json.loads(value)
    except json.JSONDecodeError as exc:
        raise ToolError(f"template config is not valid JSON: {exc}") from exc
    video = data.get("video") if isinstance(data, dict) else None
    if not isinstance(video, dict) or not all(
            k in video for k in ("freefps_enable", "freefps_target_rate", "freefps_target_scale")):
        raise ToolError("template config lacks video.freefps_enable/target_rate/target_scale")
    return section, data


def config_line_for(data: dict, case: Case) -> str:
    data = copy.deepcopy(data)
    num, den = split_rational(case.target_fps)
    data["video"]["freefps_enable"] = 1 if case.enabled else 0
    data["video"]["freefps_target_rate"] = num
    data["video"]["freefps_target_scale"] = den
    value = json.dumps(data, ensure_ascii=False, separators=(",", ":"))
    if "\\" in value or "\n" in value:
        raise ToolError("generated config value would need escaping; refusing to write it")
    return f"{CONFIG_KEY}={value}"


# ---------------------------------------------------------------- prepare

def cmd_prepare(args: argparse.Namespace) -> int:
    run_dir = args.out_dir.resolve()
    if run_dir.exists() and any(run_dir.iterdir()):
        raise ToolError(f"run directory already exists and is not empty: {run_dir}")
    template = read_template(args.template.resolve()) if args.template else None
    if args.template is None:
        print("WARNING: no --template given. FreeRenderFPS settings are NOT injected; the copies "
              "keep whatever config the fixture has (none, so FreeFPS is OFF).", file=sys.stderr)

    (run_dir / "projects").mkdir(parents=True, exist_ok=True)
    (run_dir / "out").mkdir(exist_ok=True)
    job_lines = ["# project\toutput\toutput_plugin"]
    case_docs = []

    for case in CASES:
        fixture_path = FIXTURE_DIR / FIXTURES[case.fixture]
        lines, eol = read_text_lines(fixture_path)
        rate, scale, frames = scan_fixture(lines)
        if Fraction(rate, scale) != parse_rational(case.project_fps):
            raise ToolError(f"{case.case_id}: fixture {fixture_path.name} is {rate}/{scale} fps, "
                            f"matrix expects {case.project_fps}")
        project_copy = run_dir / "projects" / f"{case.case_id}.aup2"
        output_copy = run_dir / "out" / f"{case.case_id}.mp4"

        lines = [line for line in lines if not line.startswith(CONFIG_KEY + "=")]
        if template is not None:
            section, data = template
            start = find_header(lines, section)
            lines.insert(start + 1, config_line_for(data, case))
        set_value(lines, "project", "file", str(project_copy))
        set_value(lines, "scene.0", "output.file", str(output_copy))
        project_copy.write_bytes(join_text_lines(lines, eol))

        job_path = str(project_copy)
        output_path = str(output_copy)
        if "\t" in job_path or "\t" in output_path:
            raise ToolError("tab characters in paths would break the job file")
        job_lines.append(f"{job_path}\t{output_path}\t{PLUGIN_NAME}")

        frames_out = expected_frames(frames, case.project_fps, case.target_fps)
        target_num, target_den = split_rational(case.target_fps)
        case_docs.append({
            "id": case.case_id,
            "label": case.label,
            "fixture": fixture_path.name,
            "project_fps": case.project_fps,
            "target_fps": case.target_fps,
            "project_frames": frames,
            "expected_frames": frames_out,
            "expected_loaded_frame_max": frames - 1,
            "freefps_enable": case.enabled,
            "freefps_target_rate": target_num,
            "freefps_target_scale": target_den,
            "project_path": f"projects/{case.case_id}.aup2",
            "output_path": f"out/{case.case_id}.mp4",
        })

    job_file = run_dir / JOB_FILE_NAME
    job_file.write_bytes(("\n".join(job_lines) + "\n").encode("utf-8"))
    doc = {
        "schema": SCHEMA,
        "generated": datetime.datetime.now().isoformat(timespec="seconds"),
        "run_dir": str(run_dir),
        "job_file": str(job_file),
        "results_file": str(run_dir / RESULTS_FILE_NAME),
        "plugin_name": PLUGIN_NAME,
        "config_source": str(args.template.resolve()) if args.template else None,
        "config_injected": template is not None,
        "cases": case_docs,
    }
    (run_dir / "cases.json").write_bytes((json.dumps(doc, indent=2, ensure_ascii=True) + "\n").encode("utf-8"))

    print(f"run dir: {run_dir}")
    print(f"job file: {job_file}")
    print(f"config injected: {'yes' if template is not None else 'NO'}")
    print(f"{'case':<15} {'project':>8} {'target':>12} {'frames':>7} {'expected':>9}  freefps")
    for c in case_docs:
        print(f"{c['id']:<15} {c['project_fps']:>8} {c['target_fps']:>12} {c['project_frames']:>7} "
              f"{c['expected_frames']:>9}  {'on' if c['freefps_enable'] else 'off'}")
    return 0


# ---------------------------------------------------------------- validate

def path_key(text: str) -> str:
    return os.path.normcase(os.path.abspath(text))


def read_results(path: Path) -> tuple[list[dict], bool]:
    fake = False
    header: list[str] | None = None
    rows: list[dict] = []
    for line in path.read_bytes().decode("utf-8-sig").splitlines():
        if not line.strip():
            continue
        if line.startswith("#"):
            fake = fake or "FAKE" in line.upper()
            continue
        if header is None:
            header = line.split("\t")
            continue
        cols = line.split("\t", len(header) - 1)
        if len(cols) != len(header):
            raise ToolError(f"results row has {len(cols)} fields, header has {len(header)}: {line[:80]}")
        rows.append(dict(zip(header, cols)))
    if header is None:
        raise ToolError(f"results file has no header: {path}")
    return rows, fake


def run_validator(args: argparse.Namespace, video: Path, case: dict, validation_dir: Path) -> tuple[int, dict | None, str]:
    json_path = validation_dir / f"{case['id']}.json"
    command = [sys.executable, "-I", str(VALIDATOR), str(video),
               "--project-fps", case["project_fps"],
               "--target-fps", case["target_fps"],
               "--project-frames", str(case["project_frames"]),
               "--json", str(json_path)]
    if args.ffmpeg:
        command += ["--ffmpeg", str(args.ffmpeg)]
    if args.ffprobe:
        command += ["--ffprobe", str(args.ffprobe)]
    completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    (validation_dir / f"{case['id']}.txt").write_text(completed.stdout + completed.stderr, encoding="utf-8")
    report = None
    if completed.returncode in (0, 1) and json_path.is_file():
        report = json.loads(json_path.read_text(encoding="utf-8"))
    return completed.returncode, report, completed.stderr.strip()


def evaluate_case(args: argparse.Namespace, run_dir: Path, validation_dir: Path,
                  case: dict, row: dict | None) -> dict:
    entry: dict = {"id": case["id"], "label": case["label"], "project_fps": case["project_fps"],
                   "target_fps": case["target_fps"], "project_frames": case["project_frames"],
                   "expected_frames": case["expected_frames"], "export_status": "NOT_RUN",
                   "export_reason": "", "checks": [], "overall": "NOT_RUN"}
    if row is None:
        entry["export_reason"] = "no row in results file"
        return entry
    entry["export_status"] = row.get("status", "")
    entry["export_reason"] = row.get("reason", "")
    if row.get("status") != "OK":
        entry["overall"] = "EXPORT_FAILED"
        return entry

    checks: list[dict] = []
    # The host must have loaded the project this case refers to.
    try:
        loaded = Fraction(int(row["rate"]), int(row["scale"])) == parse_rational(case["project_fps"])
        loaded = loaded and int(row["frame_max"]) == case["expected_loaded_frame_max"]
        detail = (f"rate={row['rate']}/{row['scale']} frame_max={row['frame_max']} "
                  f"expected project {case['project_fps']} fps, frame_max={case['expected_loaded_frame_max']}")
    except (KeyError, ValueError) as exc:
        loaded = False
        detail = f"could not read rate/scale/frame_max from results row: {exc}"
    checks.append({"name": "host_loaded_project", "status": "PASS" if loaded else "FAIL", "detail": detail})

    video = run_dir / case["output_path"]
    size = video.stat().st_size if video.is_file() else 0
    if size <= 0:
        checks.append({"name": "output_file", "status": "FAIL", "detail": f"missing or empty: {video}"})
        entry["checks"] = checks
        entry["overall"] = "FAIL"
        return entry
    checks.append({"name": "output_file", "status": "PASS", "detail": f"{size} bytes"})

    code, report, stderr = run_validator(args, video, case, validation_dir)
    if report is None:
        entry["checks"] = checks
        entry["overall"] = "ERROR"
        entry["export_reason"] = f"validator exit {code}: {stderr}"
        return entry
    for check in report["checks"]:
        checks.append({"name": check["name"], "status": check["status"], "detail": check["detail"]})
    entry["checks"] = checks
    if any(c["status"] == "FAIL" for c in checks):
        entry["overall"] = "FAIL"
    else:
        entry["overall"] = "PASS"
    return entry


def render_summary(run_dir: Path, results_file: Path, fake: bool, overall: str, entries: list[dict]) -> str:
    names = ["host_loaded_project", "output_file"] + CHECK_ORDER
    lines = ["# FreeRenderFPS host matrix summary", ""]
    if fake:
        lines += ["**FAKE RESULTS: this run used a generated results file, not an AviUtl2 export. "
                  "It only proves the validator and report path.**", ""]
    lines += [f"Run directory: `{run_dir}`", f"Results file: `{results_file}`", "",
              f"Overall: **{overall}**", "",
              "| Case | Project -> target | Expected frames | Export | "
              + " | ".join(names) + " | Overall |",
              "|---|---|---:|---|" + "---|" * len(names) + "---|"]
    for entry in entries:
        by_name = {c["name"]: c["status"] for c in entry["checks"]}
        cells = [by_name.get(name, "-") for name in names]
        lines.append(f"| {entry['label']} | {entry['project_fps']} -> {entry['target_fps']} | "
                     f"{entry['expected_frames']} | {entry['export_status']} | " + " | ".join(cells)
                     + f" | **{entry['overall']}** |")
    lines += ["", "## Details", ""]
    for entry in entries:
        lines.append(f"### {entry['label']} (`{entry['id']}`)")
        if entry["export_reason"]:
            lines.append(f"- export: {entry['export_reason']}")
        for check in entry["checks"]:
            detail = check["detail"].replace("|", "\\|").replace("\n", " ")
            lines.append(f"- {check['name']}: {check['status']} - {detail}")
        if not entry["checks"]:
            lines.append("- no checks run")
        lines.append("")
    return "\n".join(lines) + "\n"


def cmd_validate(args: argparse.Namespace) -> int:
    run_dir = args.run_dir.resolve()
    cases_path = run_dir / "cases.json"
    if not cases_path.is_file():
        raise ToolError(f"cases.json not found in {run_dir}; run prepare first")
    doc = json.loads(cases_path.read_text(encoding="utf-8"))
    results_file = args.results.resolve() if args.results else Path(doc["results_file"])
    if not results_file.is_file():
        raise ToolError(f"results file not found: {results_file} "
                        "(run Edit > Auto Export > Run Job List in AviUtl2 first)")
    rows, fake = read_results(results_file)
    by_output = {path_key(row.get("output", "")): row for row in rows}
    validation_dir = run_dir / "validation"
    validation_dir.mkdir(exist_ok=True)

    entries = []
    for case in doc["cases"]:
        row = by_output.get(path_key(str(run_dir / case["output_path"])))
        entries.append(evaluate_case(args, run_dir, validation_dir, case, row))

    statuses = [e["overall"] for e in entries]
    if all(s == "PASS" for s in statuses):
        overall = "PASS"
    elif any(s in ("FAIL", "EXPORT_FAILED", "ERROR") for s in statuses):
        overall = "FAIL"
    else:
        overall = "INCOMPLETE"

    (run_dir / "SUMMARY.md").write_bytes(
        render_summary(run_dir, results_file, fake, overall, entries).encode("utf-8"))
    summary = {"schema": SCHEMA, "overall": overall, "fake_results": fake,
               "results_file": str(results_file), "run_dir": str(run_dir), "cases": entries}
    (run_dir / "summary.json").write_bytes((json.dumps(summary, indent=2, ensure_ascii=True) + "\n").encode("utf-8"))

    for entry in entries:
        print(f"{entry['id']:<15} export={entry['export_status']:<7} overall={entry['overall']}")
    print(f"overall: {overall}" + ("  (FAKE results)" if fake else ""))
    print(f"wrote {run_dir / 'SUMMARY.md'} and {run_dir / 'summary.json'}")
    return 0 if overall == "PASS" else 1


# ---------------------------------------------------------------- main

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    prep = sub.add_parser("prepare", help="write project copies, job file and cases.json")
    prep.add_argument("--out-dir", type=Path, required=True, help="new run directory")
    prep.add_argument("--template", type=Path,
                      help="project .aup2 saved in AviUtl2 with the FreeRenderFPS config to copy")
    prep.set_defaults(func=cmd_prepare)

    val = sub.add_parser("validate", help="validate exported MP4s and write SUMMARY.md")
    val.add_argument("--run-dir", type=Path, required=True)
    val.add_argument("--results", type=Path,
                     help="results TSV (default: the one recorded in cases.json)")
    val.add_argument("--ffmpeg", type=Path, help="passed to the validator")
    val.add_argument("--ffprobe", type=Path, help="passed to the validator")
    val.set_defaults(func=cmd_validate)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except ToolError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
