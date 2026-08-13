# FreeRenderFPS recovery and crash analysis

Status: read-only audit for the v1.0.0 recovery task (evidence snapshot taken
2026-08-13 before later shared-workspace recovery actions).  No source,
reference, `dist`, or `C:\ProgramData` file was changed while collecting this
evidence.  Because all agents share the workspace, later ProgramData changes
do not invalidate the recorded hashes/log contents; they do mean that a fresh
inventory must be taken before validation.

## Executive conclusion

The strongest directly reproducible defect is a packaging/runtime mismatch:
the SHA named as the last-known-good binary was installed from the Debug
output.  The SHA is
`1d1779e3eadac4bfebbae9506cfe8c57987db546e4ec67b875fa5f6a96616f83`, and it is
identical at `src\x264guiEx-FreeFPS\Debug\x64\x264guiEx-FreeFPS.auo2`,
`C:\ProgramData\aviutl2\Plugin\x264guiEx-FreeFPS.auo2`, and
`C:\ProgramData\aviutl2\x264guiEx-FreeFPS\x264guiEx-FreeFPS.auo2` (3,730,944
bytes).  An ASCII import/string scan of that binary finds
`ucrtbased.dll`, `VCRUNTIME140D.dll`, `VCRUNTIME140_1D.dll`, and
`MSVCP140D.dll`, but not the release CRT names.  The project file explicitly
selects `_DEBUG` and `MultiThreadedDebugDLL` for Debug x64
(`src/x264guiEx-FreeFPS/x264guiEx/x264guiEx.vcxproj:141-147`).

Both recorded project-save failures are access violations reported in
`ucrtbased.dll`, not in the release CRT:

* `C:\ProgramData\aviutl2\Log\aviutl2_2026-08-11_23-43-21-448.log`, at
  23:43:26: `table.func_save_project_config()` / `0xC0000005` / module
  `ucrtbased.dll`.
* `C:\ProgramData\aviutl2\Log\aviutl2_2026-08-11_23-47-43-291.log`, at
  23:47:52: the same callback, code, and module.

This establishes that a Debug CRT binary was executing the failing callback
and is the first release blocker.  It does **not** identify the exact invalid
pointer or prove that CRT code, rather than plugin data passed to CRT code, is
the original corruption.  The logs contain no stack, dump, or source offset.

The second material finding is environmental contamination.  The trusted
module list enables both `x264guiEx-FreeFPS.auo2` and `x264guiEx.auo2`
(`C:\ProgramData\aviutl2\module.ini`), while ProgramData contains direct and
nested FreeFPS copies and several copies of generic `x264guiEx.ini`,
`x264guiEx.conf`, and `x264guiEx_stg`.  This makes it possible for different
loaded copies to read different settings, and for a stale generic config or
staging directory to affect recovery.  It is strong contributing evidence,
but no log records which physical module path was loaded for either callback.

Therefore the supported root-cause statement is: **Debug CRT deployment is
proven; duplicate/stale module and config state is proven; the precise memory
operation that raised the access violation remains unproven.** A release fix
must remove both sources of ambiguity before any FPS regression result is
trusted.

## Evidence inventory

| Evidence | Observation | What it proves / does not prove |
| --- | --- | --- |
| Known-good hash | The hash above is the same in source Debug output and two ProgramData locations. | The deployed artifact is reproducible; its location/name does not make it a Release build. |
| PE imports/strings | Known-good binary contains debug CRT DLL names; `Release\x64\x264guiEx-FreeRenderFPS.auo2` (1,368,576 bytes, SHA-256 `4F22CD5011B9E0C4F1EDA2CD9B6A45D96CCE3FB2DB8404CB9A036F28F9B3C31E`) contains `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`, and `MSVCP140.dll` instead. | Debug versus Release is objectively distinguishable. The release artifact was not the artifact in the crash log’s deployment locations. |
| AviUtl2 logs | Two `func_save_project_config` SEHs are `0xC0000005` in `ucrtbased.dll`; plugin display text is x264guiEx 4.12. | Debug CRT was in the call path. It does not distinguish `conf_to_json`, the host callback, or a prior memory overwrite. |
| `module.ini` | Both FreeFPS and original x264guiEx are trusted. | Duplicate plugin families are enabled; it does not prove both callbacks ran in the same process. |
| ProgramData tree | Direct `Plugin\x264guiEx-FreeFPS.auo2`, nested `Plugin\x264guiEx-FreeFPS\x264guiEx-FreeFPS\...`, a separate `aviutl2\x264guiEx-FreeFPS\...` tree, and `x264guiEx-FreeFPS.accidental` exist. | Multiple candidate module/config roots are present. It does not reveal the host’s selection order. |
| Config files | Generic `x264guiEx.conf` is 24 bytes and says `cnf_ver=1`; generic `x264guiEx.ini` exists with `ini_ver=3`; `x264guiEx-FreeFPS.accidental` contains `stg_dir=..\\..\\..\\..\\..\\ProgramData\\aviutl2\\Plugin\\x264guiEx_stg`. | An old config version and a cross-tree staging pointer are present. Current existence of the INI means “missing” was historical or path-dependent, not a current universal fact. |
| Backups | `Backup\ExceptionBackup_*.aup2` files around both failures are 598-byte, structurally valid projects with no `freerenderfps_config` line. | The project-save result was not persisted in those backups. It cannot tell whether serialization or `set_param_string` failed first. |
| WER | `C:\ProgramData\Microsoft\Windows\WER\ReportArchive` has five AviUtl2 reports. They identify `aviutl2.exe` as the faulting module: three `APPCRASH` records with `80000004`, and two `BEX64` records with `c0000409`, data `7`. | These are host-level reports without a plugin stack. They must not be conflated with the plugin callback logs or treated as proof of the same fault. |

## Why the two callbacks are exposed to this state

`OUTPUT_PLUGIN_TABLE` advertises `FLAG_PROJECT_CONFIG` and installs
`func_config2`, `func_load_project_config`, and `func_save_project_config`
(`src/x264guiEx-FreeFPS/x264guiEx/x264guiEx.cpp:103-126`).  The SDK header says
the two project callbacks are invoked only when that flag is enabled and that
`PROJECT_FILE::set_param_string` receives UTF-8 (`output2.h:93-119`,
`project2.h:9-20`).

* `func_config` initializes `SYSTEM_DATA`, checks `get_init_success()`, calls
  `ensure_default_config_initialized`, and enters `ShowfrmConfig`
  (`x264guiEx.cpp:571-577`).  Thus an INI/path/runtime problem can prevent or
  alter settings initialization before the UI is shown; the function itself
  does not use the `PROJECT_FILE` ABI.
* `func_save_project_config` null-checks `project` and its setter, initializes
  settings, rejects an unsuccessful INI load, ensures `g_conf` is initialized,
  serializes with `guiEx_config::conf_to_json(&g_conf, -1)`, then passes
  `json_str.c_str()` to the host (`x264guiEx.cpp:189-201`).  The source has no
  SEH boundary around serialization or the host callback.  A crash report that
  names this wrapper cannot, by itself, say which of those two operations
  raised the exception.
* `init_SYSTEM_DATA` resolves the module path, creates `guiEx_settings`, and
  sets the global settings pointer (`x264guiEx.cpp:628-636`).  `init_CONF_GUIEX`
  zeroes the complete current `CONF_GUIEX`, reads defaults from settings, sets
  FreeFPS defaults, and marks the header initialized (`x264guiEx.cpp:646-670`).
  Consequently a wrong module directory changes the INI/CONF/STG files used by
  both callbacks.

The callback API declaration matches the checked-in AviUtl2 reference shape:
the same function-pointer order and UTF-8 string signatures are present in
`reference/x264guiEx/x264guiEx/output2.h` and `project2.h`.  An ABI mismatch is
not demonstrated by the current source; it remains a lower-ranked hypothesis.

## Module and configuration filename resolution

The current fork’s `get_auo_path` obtains the module containing the address of
the overloaded helper through `GetModuleHandleEx(...FROM_ADDRESS...)`, then
gets that module’s full path (`src/x264guiEx-FreeFPS/x264guiEx/prm/auo_util.h:93-103`).
`guiEx_settings::initialize` appends `.conf` and `.ini` to that path, chooses a
language variant if present, and calls `check_inifile`
(`src/x264guiEx-FreeFPS/x264guiEx/prm/auo_settings.cpp:261-347`).  `check_inifile`
requires INI version >= 2 and migrates an old `cnf_ver` to UTF-8
(`auo_settings.cpp:364-393`); failure leaves `get_init_success()` false and
the normal error text says the INI is absent or old (`auo_settings.cpp:396-412`).

The upstream reference instead uses `GetModuleHandleA/W(AUO_NAME)` directly
(`reference/x264guiEx/x264guiEx/prm/auo_util.h:94-103`).  That is an important
implementation difference to validate, but neither the logs nor a module
snapshot prove that the address-based helper selected the wrong copy.  With
duplicate FreeFPS paths present, the loaded module path must be recorded in a
debugger or temporary diagnostic before assigning causality.

The source still uses the generic basenames `.ini`, `.conf`, and `_stg`
(`src/x264guiEx-FreeFPS/x264guiEx/prm/auo_settings.cpp:53-54`), even though
`auo_version.h` names the plugin `x264guiEx-FreeRenderFPS`.  The vcxproj post
build step copies files named `x264guiEx-FreeRenderFPS.ini` while the runtime
derives the name from the loaded module path; this packaging detail must be
checked in the actual Release package.  The current ProgramData tree has
generic files in several roots, including the accidental config whose
`stg_dir` points back to the original plugin’s generic staging directory.

## `CONF_VIDEO`, layout, and serialization

The fork adds three fields at the end of `CONF_VIDEO`:
`freefps_enable`, `freefps_target_rate`, and `freefps_target_scale`
(`src/x264guiEx-FreeFPS/x264guiEx/prm/auo_conf.h:219-240`).  They are part of
the containing `CONF_GUIEX` (`auo_conf.h:400-411`).  Block sizes and offsets
are generated from the current C++ types (`src/x264guiEx-FreeFPS/x264guiEx/prm/auo_conf.cpp:43-57`),
so a current binary’s JSON path is not inherently an old-struct memcpy.

The JSON path explicitly writes and reads all three fields with defaults
(`auo_conf.cpp:232-254`, `325-355`).  `conf_to_json` first calls
`build_cmd_from_conf`, then writes the video/audio/mux/other blocks and emits a
single line when `indent < 0` (`auo_conf.cpp:455-508`).  `json_to_conf` zeroes the
current struct and catches `std::exception`, but this is C++ exception handling,
not a handler for Windows SEH (`auo_conf.cpp:511-577`).

The loader accepts JSON when a staging file starts with `{`; otherwise it uses
the legacy binary loader (`auo_conf.cpp:590-625`).  The legacy path reads a
32-byte name, a file-reported size, and block offsets/sizes before copying
(`auo_conf.cpp:627-675`).  It checks the block count but does not visibly
validate that the reported allocation size, each block offset, and each block
length remain inside the file before `memcpy`.  This is a concrete robustness
gap for malformed or stale binary `.stg` files, not proof that it caused the
project callback crash.  The inspected normal preset `.stg` files begin with
`{` and are JSON; the generic legacy files still need a clean migration test.

## Ranked hypotheses

1. **Debug CRT / mixed runtime (high confidence; directly evidenced).** The
   deployed last-known-good hash is a Debug build and both callback SEHs name
   `ucrtbased.dll`.  Release is configured as `NDEBUG` + `MultiThreadedDLL`
   (`x264guiEx.vcxproj:182-192`, `232-242`) and has different CRT imports.  This
   explains why an apparently successful frame-output binary can fail when
   exercising UI/JSON/project-save paths, but it does not identify the bad
   pointer.
2. **Duplicate modules plus stale/shared config (high as a recovery
   contributor; causality not proven).** `module.ini`, duplicate binaries, the
   old `cnf_ver=1`, and the accidental cross-tree `stg_dir` are all observed.
   A path-dependent `guiEx_settings` instance can therefore load different
   INI/CONF/STG state.  No current evidence records which one was loaded in the
   failing process.
3. **Legacy `CONF_VIDEO`/STG compatibility or malformed data (medium).** The
   struct changed and the legacy loader has insufficient visible bounds checks.
   The project callback uses JSON, however, and normal inspected presets are
   JSON, so this is not a demonstrated callback cause.
4. **Project callback boundary or pointer lifetime (medium-low).** The source
   passes a temporary string’s `c_str()` to `set_param_string`; this is valid
   for a synchronous callback, but the header does not state whether the host
   copies before returning.  A host ABI or lifetime problem could explain why
   backups contain no saved key.  The checked-in declarations match the SDK
   reference, and no host stack is available, so this remains unconfirmed.
5. **Module-path helper difference (low-medium).** Address-based resolution
   differs from upstream name-based resolution.  It is worth instrumenting in
   a duplicate tree, but there is no evidence that it selected the wrong
   module here.
6. **WER host failures as the same bug (low / separate evidence).** WER names
   `aviutl2.exe` and records `80000004` or `c0000409` at host offsets.  Without
   a dump linking those offsets to this plugin, they cannot be merged with the
   `ucrtbased.dll` callback events.

## Recovery, fix, and validation checklist

1. Preserve the hash and file metadata above.  Inventory the loaded module
   path in a debugger or host diagnostic; do not infer it from a directory
   listing.
2. Build and package **Release x64 only** (`NDEBUG`, `/MD` / `MultiThreadedDLL`)
   and verify the PE imports contain no `ucrtbased.dll`, `VCRUNTIME140D.dll`,
   `VCRUNTIME140_1D.dll`, or `MSVCP140D.dll`.  Give the artifact the formal
   `x264guiEx-FreeRenderFPS.auo2` name/version and record its SHA-256.
3. Make a clean test root containing one formal FreeRenderFPS copy.  Keep the
   original x264guiEx only for an explicit side-by-side test; otherwise disable
   it.  Move (do not destructively delete) duplicate/staged copies out of the
   plugin search path and preserve them as rollback backups.
4. Ensure the FreeRenderFPS module, INI, CONF, and STG paths resolve to one
   directory.  Do not allow `stg_dir` to point to generic
   `Plugin\\x264guiEx_stg`; migrate old `cnf_ver=1` settings only after a copy
   is preserved.  Confirm the runtime-selected filenames, not merely the files
   copied by an installer.
5. On a clean AviUtl2 v2.1.4 x64 instance, run this recovery sequence: start;
   open settings; close settings; create a project; save; exit; reopen; save
   again; then export with FreeFPS OFF.  Any failure stops FPS testing.
6. Add temporary diagnostic breakpoints/logging around
   `func_save_project_config`: entry, after `ensure_default_config_initialized`,
   after `conf_to_json` (record JSON length), immediately before and after
   `set_param_string`, and record the module path plus CRT imports.  This is the
   minimum probe that separates serializer failure from the host callback.
7. Test project persistence by checking for one `freerenderfps_config` key in
   the saved `.aup2`, closing/reopening, and saving again.  Compare with a
   project containing only the ordinary x264guiEx `config` key; the fork’s
   legacy import is intentionally limited to payloads containing FreeFPS fields
   (`x264guiEx.cpp:168-176`).
8. Exercise JSON preset save/load and legacy `.stg` migration with valid,
   truncated, oversized, and bad-offset inputs.  The legacy loader must return
   an error, never read outside the file, and never raise SEH.
9. Only after steps 1-8 pass, run the required FPS matrix (30→60, 66→60,
   66→59.94, 66→24, 66→120, 120→60) and record frame count, duration, audio
   duration, cadence/duplicates, and crash status.
10. If a Release-only clean test still faults, collect a full user-mode dump
    with symbols and the exact loaded module path.  The current logs are not
    sufficient to assert the failing source instruction.

## Claims deliberately not made

This audit does not claim that `set_param_string` is definitely the faulting
call, that `conf_to_json` definitely corrupts memory, that the address-based
module lookup definitely selected a stale copy, or that the WER `BEX64` reports
are the same incident.  Those claims require a dump/stack or a controlled
clean-room reproduction.  The evidence does establish the Debug CRT
deployment and the duplicate/stale recovery state, both of which must be
removed before v1.0.0 can be considered releasable.
