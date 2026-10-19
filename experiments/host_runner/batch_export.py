#!/usr/bin/env python3
"""Export a list of projects one after another, each through host_export's separate-process path.

Usage:
    python -I experiments/host_runner/batch_export.py --host build/host/aviutl2/aviutl2.exe
        --list LIST.txt [--timeout 600] [--skip-existing]

LIST.txt is UTF-8 text with one job per line: PROJECT<TAB>OUTPUT. Lines starting with #
and blank lines are ignored. Relative paths are taken from the folder that holds LIST.txt.

Every job uses host_export.check_args, host_export.start_host and the job.ini / result.ini
helpers, so the rules are the same as for a single export: the project is read only, a
host without a data folder is refused, and an existing output is an error. With
--skip-existing an existing output is skipped instead and counted as skipped. Jobs run one
at a time; a failed job does not stop the batch.

Exit codes: 0 every job ok or skipped; 1 at least one job failed; 2 bad arguments, host
or list file.
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from pathlib import Path

# python -I leaves the script folder off sys.path, so add it before importing host_export.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import host_export  # noqa: E402

EXIT_OK = 0
EXIT_FAILED = 1
EXIT_USAGE = 2

STATUS_OK = "ok"
STATUS_FAILED = "失败"
STATUS_SKIPPED = "已跳过"


class ListError(Exception):
    pass


@dataclass
class Job:
    line_no: int
    project: Path
    output: Path


@dataclass
class Outcome:
    status: str
    elapsed: float | None = None  # None when the host was never started
    bytes: str = ""
    error: str = ""
    run_dir: Path | None = None


def fail_usage(message: str) -> int:
    print(f"错误: {message}", file=sys.stderr)
    return EXIT_USAGE


def read_list(list_path: Path) -> list[Job]:
    base = list_path.parent
    jobs: list[Job] = []
    seen_outputs: dict[Path, int] = {}
    for line_no, raw in enumerate(list_path.read_text(encoding="utf-8-sig").splitlines(), start=1):
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        parts = raw.split("\t")
        if len(parts) != 2 or not parts[0].strip() or not parts[1].strip():
            raise ListError(f"第 {line_no} 行格式错误（需要 工程路径<TAB>输出路径）: {raw!r}")
        job = Job(line_no, (base / parts[0].strip()).resolve(), (base / parts[1].strip()).resolve())
        if job.output in seen_outputs:
            raise ListError(f"第 {line_no} 行的输出与第 {seen_outputs[job.output]} 行相同: {job.output}")
        seen_outputs[job.output] = line_no
        jobs.append(job)
    return jobs


def run_job(host: Path, job: Job, timeout: int, skip_existing: bool) -> Outcome:
    if skip_existing and job.output.exists():
        return Outcome(STATUS_SKIPPED)
    error = host_export.check_args(host, job.project, job.output, timeout)
    if error:
        return Outcome(STATUS_FAILED, error=error)

    run_dir = host_export.new_run_dir()
    job_path = run_dir / "job.ini"
    result_path = run_dir / "result.ini"
    job_path.write_bytes(
        host_export.job_bytes(str(job.project), str(job.output), host_export.PLUGIN_NAME, timeout * 1000)
    )
    returncode, elapsed, timed_out = host_export.start_host(host, job_path, timeout)
    if timed_out:
        return Outcome(STATUS_FAILED, elapsed, error=f"超时（{timeout} s），host 已被结束", run_dir=run_dir)
    if not result_path.is_file():
        return Outcome(
            STATUS_FAILED, elapsed, error=f"没有生成 result.ini（host 退出码 {returncode}）", run_dir=run_dir
        )

    result = host_export.read_result(result_path)
    ok = result.get("ok", "")
    if ok != "1":
        return Outcome(
            STATUS_FAILED, elapsed, error=f"ok={ok!r}  result.ini error={result.get('error', '')!r}", run_dir=run_dir
        )
    if not job.output.is_file():
        return Outcome(STATUS_FAILED, elapsed, error="result.ini 说成功，但输出文件不存在", run_dir=run_dir)
    return Outcome(STATUS_OK, elapsed, bytes=result.get("bytes", ""), run_dir=run_dir)


def format_outcome(outcome: Outcome) -> str:
    if outcome.status == STATUS_SKIPPED:
        return "    已跳过（输出已存在）"
    if outcome.status == STATUS_OK:
        return f"    ok  耗时 {outcome.elapsed:.1f} s  bytes {outcome.bytes}"
    timing = "" if outcome.elapsed is None else f"耗时 {outcome.elapsed:.1f} s  "
    return f"    失败  {timing}error: {outcome.error}"


def main(argv: list[str] | None = None) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="Export a list of projects one after another (separate-process mode).")
    parser.add_argument("--host", type=Path, required=True, help="portable aviutl2.exe (with a data folder beside it)")
    parser.add_argument("--list", type=Path, required=True, help="UTF-8 list: PROJECT<TAB>OUTPUT per line")
    parser.add_argument("--timeout", type=int, default=600, help="seconds per job before the host is killed (default 600)")
    parser.add_argument("--skip-existing", action="store_true", help="skip a job whose output file already exists")
    args = parser.parse_args(argv)

    if args.timeout <= 0:
        return fail_usage("--timeout must be positive")
    host = args.host.resolve()
    if not host.is_file() or host.suffix.lower() != ".exe":
        return fail_usage(f"host exe not found: {host}")
    if not (host.parent / "data").is_dir():
        return fail_usage(f"no data folder beside {host}: refusing a host that would use ProgramData")
    list_path = args.list.resolve()
    if not list_path.is_file():
        return fail_usage(f"list file not found: {list_path}")
    try:
        jobs = read_list(list_path)
    except ListError as exc:
        return fail_usage(str(exc))
    if not jobs:
        return fail_usage(f"no jobs in {list_path}")

    print(f"host: {host}")
    print(f"清单: {list_path}  共 {len(jobs)} 个任务")
    results: list[tuple[Job, Outcome]] = []
    batch_started = time.monotonic()
    for index, job in enumerate(jobs, start=1):
        print(f"[{index}/{len(jobs)}] {job.project.name}  ->  {job.output}")
        outcome = run_job(host, job, args.timeout, args.skip_existing)
        results.append((job, outcome))
        print(format_outcome(outcome))
        if outcome.run_dir is not None:
            print(f"    运行目录: {outcome.run_dir}")
    total_elapsed = time.monotonic() - batch_started

    ok_count = sum(1 for _, o in results if o.status == STATUS_OK)
    failed = [(job, o) for job, o in results if o.status == STATUS_FAILED]
    skipped_count = sum(1 for _, o in results if o.status == STATUS_SKIPPED)
    print()
    print(f"汇总: 共 {len(results)} 个  成功 {ok_count}  失败 {len(failed)}  已跳过 {skipped_count}  总耗时 {total_elapsed:.1f} s")
    if failed:
        print("失败清单:")
        for job, outcome in failed:
            print(f"  {job.project.name}  ->  {job.output}")
            print(f"    {outcome.error}")
    return EXIT_FAILED if failed else EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
