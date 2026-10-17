# FreeFPS scheduler host-free tests

Plain C++ `main` (no framework) that exercises `FreeFpsSession`
(`src/x264guiEx-FreeFPS/x264guiEx/encode/freefps_scheduler.cpp`) without AviUtl.
The real hooks (`freefps_aviutl_hook.cpp` / `freefps_aviutl1_hook.cpp`, which
patch the host executable) are replaced by `fake_host.cpp`, which implements the
same `freefps_hook_*` functions declared in `freefps_host_hook.h`. x64 builds the
AviUtl2 path and x86 the AviUtl 1.x path of the scheduler.

## Files

- `fake_host.h` / `fake_host.cpp`: fake host. Frame cache keyed by integer frame
  with configurable depth; a cache hit does not run the builder; a builder run
  evaluates the active request's target coordinate. Each frame buffer encodes its
  coordinate in the first 8 bytes and a position-dependent pattern elsewhere, so
  `fake_verify_frame` detects stale, truncated or misplaced content.
- `scheduler_test.cpp`: the checks (one PASS/FAIL line each).
- `build_and_run.ps1`: compiles and runs the test for x64 and x86.
- `build/<arch>/`: build output (git-ignored).

## Checks

1. Rate matrix (cache depth 1, PA64 at 64x36, plus RGB24 at 63x36): output count
   and every output's content at its own coordinate `i * Pr * Ts / (Tr * Ps)`.
   Also enforces a host-render budget per case (e.g. 30 -> 60 must cost exactly
   one render per output now that eviction renders are reused).
2. Disabled mode: passthrough, no hook install.
3. A second enabled session is rejected with `another render session` and does
   not disturb the first session's hook.
4. Hook install failure: `initialize` fails and the session slot is released.
5. Cache depth 2: never returns a stale frame; returns null on cache collision.
6. One-frame project: output 1 fails with the documented `neighbor` limitation.

## Run

From any checkout, in PowerShell:

    powershell -NoProfile -ExecutionPolicy Bypass -File tests\freefps_scheduler\build_and_run.ps1

Requires Visual Studio 2022/18 Community with the x64 and x86 MSVC toolchains at
the path set in the script. Exit status is non-zero if either architecture fails
to build or any check fails.
