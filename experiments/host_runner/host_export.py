#!/usr/bin/env python3
"""Export one .aup2 with a portable AviUtl2 copy, using the separate-process path of the plugin.

Usage:
    python -I experiments/host_runner/host_export.py --host build/host/aviutl2/aviutl2.exe
        --project PROJECT.aup2 --output OUT.mp4 [--timeout 600]

The project is passed as "retimed" in job.ini, so it is opened as is: it is read,
not saved and not modified. The host is started with FREE_FPS_EXPORT_JOB=<job.ini>
(freefps_export.cpp, detect_child_job). The plugin inside the host runs output_file
with the plugin name "x264guiEx FreeRenderFPS" (x264guiEx.cpp, output_plugin_table.name),
writes result.ini beside job.ini and ends the host process.

job.ini and result.ini are UTF-16LE with a BOM (create_unicode_ini in freefps_export.cpp),
so non-ASCII paths survive GetPrivateProfileStringW.

Refuses a host without a data folder next to the exe: such a host would use the
user's ProgramData folder. Refuses an existing output file. Run dirs are created under
build/host/runs/<timestamp>/.

Exit codes: 0 export ok and output file present; 1 export failed (timeout, no or bad
result.ini, output missing); 2 bad arguments or environment.
"""

from __future__ import annotations

import argparse
import configparser
import os
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
RUNS_DIR = REPO_ROOT / "build" / "host" / "runs"
JOB_ENV = "FREE_FPS_EXPORT_JOB"
PLUGIN_NAME = "x264guiEx FreeRenderFPS"

EXIT_OK = 0
EXIT_FAILED = 1
EXIT_USAGE = 2


def fail_usage(message: str) -> int:
    print(f"错误: {message}", file=sys.stderr)
    return EXIT_USAGE


def job_bytes(retimed: str, output: str, plugin: str, timeout_ms: int) -> bytes:
    lines = [
        "[job]",
        f"retimed={retimed}",
        f"output={output}",
        f"plugin={plugin}",
        f"timeout_ms={timeout_ms}",
    ]
    return b"\xff\xfe" + ("\r\n".join(lines) + "\r\n").encode("utf-16-le")


def read_result(path: Path) -> dict[str, str]:
    raw = path.read_bytes()
    text = raw.decode("utf-16") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("utf-8-sig")
    parser = configparser.ConfigParser(interpolation=None)
    parser.read_string(text)
    return dict(parser["result"]) if parser.has_section("result") else {}


def new_run_dir() -> Path:
    RUNS_DIR.mkdir(parents=True, exist_ok=True)
    base = time.strftime("%Y%m%d-%H%M%S")
    for n in range(100):
        name = base if n == 0 else f"{base}-{n}"
        candidate = RUNS_DIR / name
        try:
            candidate.mkdir()
            return candidate
        except FileExistsError:
            continue
    raise OSError(f"cannot create a run dir under {RUNS_DIR}")


def main(argv: list[str] | None = None) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="Export a project with a portable AviUtl2 (separate-process mode).")
    parser.add_argument("--host", type=Path, required=True, help="portable aviutl2.exe (with a data folder beside it)")
    parser.add_argument("--project", type=Path, required=True, help=".aup2 to export (read only)")
    parser.add_argument("--output", type=Path, required=True, help="output MP4 (must not exist yet)")
    parser.add_argument("--timeout", type=int, default=600, help="seconds before the host is killed (default 600)")
    args = parser.parse_args(argv)

    if args.timeout <= 0:
        return fail_usage("--timeout must be positive")
    host = args.host.resolve()
    if not host.is_file() or host.suffix.lower() != ".exe":
        return fail_usage(f"host exe not found: {host}")
    if not (host.parent / "data").is_dir():
        return fail_usage(f"no data folder beside {host}: refusing a host that would use ProgramData")
    project = args.project.resolve()
    if not project.is_file():
        return fail_usage(f"project not found: {project}")
    output = args.output.resolve()
    if output.exists():
        return fail_usage(f"output already exists, remove it first: {output}")
    if not output.parent.is_dir():
        return fail_usage(f"output folder not found: {output.parent}")
    for label, value in (("project", str(project)), ("output", str(output))):
        if "\r" in value or "\n" in value:
            return fail_usage(f"{label} path contains a line break")

    run_dir = new_run_dir()
    job_path = run_dir / "job.ini"
    result_path = run_dir / "result.ini"
    job_path.write_bytes(job_bytes(str(project), str(output), PLUGIN_NAME, args.timeout * 1000))
    print(f"运行目录: {run_dir}")
    print(f"host: {host}")

    env = os.environ.copy()
    env[JOB_ENV] = str(job_path)
    started = time.monotonic()
    proc = subprocess.Popen([str(host)], cwd=str(host.parent), env=env)
    timed_out = False
    try:
        proc.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        proc.kill()
        proc.wait()
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
    elapsed = time.monotonic() - started
    print(f"host 进程结束: 退出码 {proc.returncode}    耗时 {elapsed:.1f} s")
    if timed_out:
        print(f"超时（{args.timeout} s），已结束 host 进程。", file=sys.stderr)

    if not result_path.is_file():
        print(f"没有生成 result.ini: {result_path}", file=sys.stderr)
        return EXIT_FAILED
    result = read_result(result_path)
    ok = result.get("ok", "")
    print(f"ok: {ok}")
    print(f"output: {result.get('output', '')}")
    print(f"bytes: {result.get('bytes', '')}")
    print(f"encoder: {result.get('encoder', '')}")
    print(f"error: {result.get('error', '')}")

    out_exists = output.is_file()
    print(f"输出文件存在: {out_exists}" + (f"    大小 {output.stat().st_size} 字节" if out_exists else ""))
    if timed_out or ok != "1" or not out_exists:
        return EXIT_FAILED
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
