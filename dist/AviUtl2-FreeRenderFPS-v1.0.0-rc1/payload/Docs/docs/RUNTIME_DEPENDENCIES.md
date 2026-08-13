# FreeRenderFPS v1.0.0-rc1 runtime dependency and redistribution audit

This is the release-time dependency audit for the current working tree (2026-08-13).
It is intentionally conservative: a binary whose exact license, provenance, or
corresponding source is not evidenced here is **not bundleable**.  Installer
acquisition means that a future installer may fetch a specific, checksum-pinned
build from an upstream source after checking that source's terms; it does not
authorize the installer to mirror an arbitrary download URL.

## Release decision

The plugin and checked-in source/data under the project's own notices can be
redistributed.  The staged `exe_files` directory is not release-cleared as a
whole.  In particular, the exact x264 and FFmpeg binaries have no corresponding
source/notice package in this tree, and the two L-SMASH-Works DLLs and the two
`check_*` helper DLLs have no sufficient license provenance.  They must not be
copied into a v1.0.0 installer until the conditions in this document and
`THIRD_PARTY_NOTICES.md` are met.

The recovery fixed the prior naming/path hazard. The Release build and adjacent
INIs now consistently use `x264guiEx-FreeRenderFPS`; runtime code derives the
`.ini`/`.conf` paths from the loaded module resolved by address. Tracked generic
`x264guiEx.ini` source templates and stale runtime `.conf` files are not release
payload names.

## Runtime evidence and lookup rules

* `DEFAULT_EXE_DIR` is `exe_files` (`src/x264guiEx-FreeFPS/x264guiEx/prm/auo_settings.h:114-115`).
  `find_exe_files` recursively scans both the AviUtl root and plugin `exe_files`
  directories, then prefix-matches a configured executable name
  (`src/x264guiEx-FreeFPS/x264guiEx/encode/auo_encode.cpp:95-123,253-269`).
  Consequently `x264_3223_x64.exe` can satisfy `x264.exe`, and
  `ffmpeg_audenc.exe` can satisfy `ffmpeg.exe`; this is an observed lookup rule,
  not a license grant.
* MP4, timecode-to-MP4, raw MP4, and MKV are configured as `remuxer.exe`,
  `timelineeditor.exe`, `muxer.exe`, and `mkvmerge.exe` respectively
  (`src/x264guiEx-FreeFPS/x264guiEx/x264guiEx.ini:34-155`).  A muxer is checked
  before output and failure is fatal for that selected mode
  (`src/x264guiEx-FreeFPS/x264guiEx/encode/auo_encode.cpp:282-300,791-811`).
* The INI declares nineteen external audio choices, including `ffmpeg.exe`,
  `qaac.exe`, `lame.exe`, `flac.exe`, and `opusenc.exe`
  (`src/x264guiEx-FreeFPS/x264guiEx/x264guiEx.ini:207-228`).  For the x264 build,
  the default external audio index is 15 (`prm/auo_settings.h:53-59`), which is
  the `AAC_FFMPEG` entry (`x264guiEx.ini:779-787`); the external executable is
  located/fallback-selected at `auo_encode.cpp:699-752`.
* The three built-in language resources are embedded in the plugin resource
  (`src/x264guiEx-FreeFPS/x264guiEx/auo_version.rc:31-33`).  Loose language
  files are only discovered when a matching `.lng` and `.ini` pair exists
  (`src/x264guiEx-FreeFPS/x264guiEx/frm/auo_mes.cpp:1045-1065`).
* Profiles are read from the configured `stg_dir`; the default is derived from
  the AUO path and persisted in the `.conf` file
  (`src/x264guiEx-FreeFPS/x264guiEx/prm/auo_settings.cpp:767-826`).  The tracked
  sample `x264guiEx.conf` currently points at a generic `x264guiEx_stg` path,
  so it must not silently be reused for side-by-side installation.

## Manifest

| File, pattern, or class | Required for v1.0.0 | Evidence/source | License or provenance evidence | Redistribution status | v1.0.0 disposition |
|---|---|---|---|---|---|
| `x264guiEx-FreeRenderFPS.auo2` (x64 Release) | Yes | `src/x264guiEx-FreeFPS/Release/x64/x264guiEx-FreeRenderFPS.auo2`; formal name in `auo_version.h:31-48`; SHA-256 `66266D5D08DF368D067B3FF74C0815965BD5177E2FF9C4E04181D053EAFD5DD5` | FreeRenderFPS code is covered by the repository MIT notice; copied x264guiEx headers retain rigaya notices (`LICENSE`, source headers) | Permitted with the retained notices | **Bundle** the Release x64 plugin only. Never package Debug/PDB artifacts. |
| `x264guiEx-FreeRenderFPS.ini`, `.en.ini`, `.zh.ini` (formal adjacent INI files) | Yes: the base INI is required for settings; localized INIs are required when that language is selected | Source templates `src/x264guiEx-FreeFPS/x264guiEx/x264guiEx.ini`, `.en.ini`, `.zh.ini`; post-build rename in `x264guiEx.vcxproj:127-130`; parser at `prm/auo_settings.cpp:296-337,364-393` | Upstream x264guiEx README states MIT; preserve the original text and attribution. Translation-file authorship is not separately documented in this tree. | Conditional: exact upstream files may be redistributed with notices; edited/retranslated files require separate permission review | **Bundle only the formal renamed files**, unchanged, after the attribution check. Do not ship stale unrenamed files as the release contract. |
| Embedded `x264guiEx.ja.lng`, `x264guiEx.en.lng`, `x264guiEx.zh.lng` | Yes for the three built-in UI languages; no separate file is needed for the built-ins | Embedded as `EXE_DATA` in `auo_version.rc:31-33`; selected by `load_lng` in `x264guiEx.cpp:858-889` | x264guiEx source is MIT upstream, but the current tree has no separate translation license/credit manifest | Conditional only when retained inside the exact plugin and notices; loose copies are not cleared | **Keep embedded in the plugin**. Do not add loose `.lng` files to a release until translation permissions are recorded. Unknown custom language files are not bundleable. |
| `x264guiEx-FreeRenderFPS.conf` (user state) | No on first launch; created/updated by the plugin | `.conf` is derived beside the AUO and initialized/migrated by `guiEx_settings::check_inifile` (`prm/auo_settings.cpp:261-337,364-393`) | User configuration, not a third-party component | No redistribution question for a clean generated file; user data must be preserved | **Do not bundle** the tracked `x264guiEx.conf` as a default. Installer may create a clean file and must preserve an existing user's file under the FreeRenderFPS basename. |
| `*.stg` profile presets (Bluray, YouTube, anime, etc.) | Optional for first-run output; required only when a preset is selected or a saved profile is referenced | Source sets under `src/x264guiEx-FreeFPS/x264guiEx/stg/` and `x264guiEx-FreeFPS/x264guiEx/x264guiEx-FreeFPS/x264guiEx_stg/`; profile directory handling in `prm/auo_settings.cpp:767-826` | Profiles originate from x264guiEx; upstream README claims MIT for the project, but profile translations/assets have no separate manifest | Conditional: preserve upstream attribution and use one canonical set | **Bundle after path verification**, into a FreeRenderFPS-specific profile directory. The current generic `x264guiEx_stg` path is a side-by-side conflict/release blocker; do not copy it blindly. |
| `exe_files/x264_3223_x64.exe` (matched as `x264.exe`) | Yes for the default video path unless the user supplies another x264 | Present staged file; `--version` reports x264 `0.165.3223M 0480cb0`, GPL v2 or later, built 2025-09-16; SHA-256 `1C698A1363D5121C02CE2B8FDEA79148F8386E5CD5991EEBBD6B361EABEBDF98`; configured filename at `x264guiEx.ini:872-876` | x264's official licensing page offers GPL or commercial licensing; the binary itself reports GPL | GPL permits redistribution only with GPL terms and corresponding source/offer. The exact source, COPYING text, and build recipe are absent here | **Do not bundle current file.** Installer acquisition or a new vendor package must provide the exact source/configuration, GPL text, and checksum. |
| `exe_files/ffmpeg_audenc.exe` (matched as `ffmpeg.exe`) | Yes for the default external AAC audio path when audio is enabled; otherwise optional if the user selects another encoder | `--version` reports FFmpeg N-97021-gf39678b3a9, 32-bit static build; `--buildconf` shows `--enable-version3`, `--enable-lib*` audio libraries, and `--disable-gpl`; SHA-256 `02E45B2E90484301D69C05AEFAE5AE7DEF8114E4EA96242C329F04710AA111BA` | `-L` prints LGPL terms; FFmpeg's official guidance says optional GPL parts change the license and `--enable-version3` upgrades to LGPLv3. The exact linked-library notices/source are absent | Conditional LGPL redistribution requires the exact source, notices, and build information; static third-party libraries must also be audited | **Do not bundle current file.** Installer acquisition must use a source-complete, checksum-pinned LGPL-compliant build, or the user must provide `ffmpeg.exe`. |
| `exe_files/remuxer_x64.exe` (matched as `remuxer.exe`) | Yes for the default MP4 mux path when audio/video are combined | Version metadata: L-SMASH rev1484, copyright 2010-2020; `--version` identifies L-SMASH; SHA-256 `9A74238807F77B7DDF54D8B5574C7C57A7DF62B99E107B45C06EE6C9612D9F4E`; INI `SETTING_MUXER_MP4` | Official L-SMASH project states ISC and permits copying/distribution with notice | ISC redistribution is permitted with the copyright/license notice; exact binary-source provenance is still missing from this tree | **Installer acquisition / conditional bundle.** Bundle only after attaching the matching L-SMASH source/release and ISC notice. |
| `exe_files/muxer_x64.exe` (matched as `muxer.exe`) | Optional: raw MP4 mode (`SETTING_MUXER_MP4_RAW`) | L-SMASH rev1484 metadata; SHA-256 `BC07AD5D9A445C1B8FD74B7EDC71CAD7BBD909226D4F57CB1068A1370AB073E5`; INI `x264guiEx.ini:107-122` | L-SMASH ISC (official project page) | ISC with notice, subject to exact-source verification | **Installer acquisition / conditional bundle**; not needed for a standard MP4 path. |
| `exe_files/timelineeditor_x64.exe` (matched as `timelineeditor.exe`) | Optional: timecode/timeline-editor MP4 mode (`SETTING_MUXER_TC2MP4`) | L-SMASH rev1484 metadata; SHA-256 `121358105FF5CBA291EE666D4E7B06F2184509202516AF93858371BB2CA79869`; INI `x264guiEx.ini:76-90` | L-SMASH ISC (official project page) | ISC with notice, subject to exact-source verification | **Installer acquisition / conditional bundle**; FreeFPS-active mode disables AFS/timecode features, so it is not a core FreeFPS dependency. |
| `mkvmerge.exe`, `mplex.exe`, `mp4box.exe` and other configured muxers | Optional, only for the selected MKV/MPG/alternate profile | Names appear in `x264guiEx.ini:142-170` and command/path validation in `auo_encode.cpp:315-347,791-811`; no matching binaries are staged | License/provenance not established from this tree | **Unknown means not bundleable** | **Require user/installer acquisition** from the respective upstream project after a license/checksum check; never silently substitute an unknown binary. |
| `exe_files/LSMASHSource.dll` and `LSMASHSource_indexing.exe` | Optional helper tools; not used by the core x264 video/audio/mux invocation | Staged files report L-SMASH-Works r1282; descriptions identify Avisynth/VapourSynth and indexing; SHA-256 `612942AC919B26AD9BB0929B415D97FF840D834A9D9EBEE00FC62A453C8606FC` and `82C93AFD2C9CEDD09E757155118E5394BA84C94A3F8E0877A81EBCA76B4FD2A8` | L-SMASH-Works release lists FFmpeg, l-smash, zlib, and other dependencies, but the exact binary's complete license set/source is not present here | License set/provenance is incomplete; therefore not cleared | **Exclude from v1.0.0** unless a complete matching upstream package and notices are supplied. Installer acquisition may offer the upstream L-SMASH-Works package separately. |
| `exe_files/check_vc.dll`, `exe_files/check_dotnet.dll` | No core runtime requirement; setup/preflight helpers only | Staged files have no product, company, version, copyright, or source metadata; SHA-256 `4B556B8861FF80D51BC1B6EF73138ED86C71F4562F22D29ED595216A2E862AA4` and `7E4253C49A0A18406A8373F4A98F6C8B59BAF5988541E769D992E73DD99DFE8B` | **Unknown** | Unknown license means no redistribution permission can be presumed | **Exclude** until provenance/license is obtained. Do not copy them merely because they are present in the source tree. |
| External audio tools named by the INI (`NeroAacEnc.exe`, `qtaacenc.exe`, `lame.exe`, `ext_bs.exe`, `oggenc2.exe`, `qaac.exe`, `refalac.exe`, `mp4alsRM23.exe`, `flac.exe`, `fdkaac.exe`, `opusenc.exe`) | Optional, depending on the user's selected audio encoder | Names and command lines are data in `x264guiEx.ini:207-228,230-870`; none of these exact files is staged in the current `exe_files` directory | Mixed licenses; no exact binary/source/license evidence in this tree | Unknown per tool; no blanket redistribution permission | **Do not bundle.** Require user/installer acquisition with a per-tool notice and checksum. |
| Release Microsoft CRT (`VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`, `MSVCP140.dll`, and the Windows UCRT) | Yes for a Release `/MD` build; not part of the plugin payload | Release project uses `MultiThreadedDLL` (`x264guiEx.vcxproj:181-205,231-256`); recovery evidence records the release CRT import names in `docs/RECOVERY_ANALYSIS.md:55,189-191` | Microsoft Visual C++ Redistributable / Windows system component | Redistribute only under Microsoft's current redist terms; do not copy arbitrary DLLs | **Installer prerequisite/acquisition**, using Microsoft's official VC++ Redistributable. Debug CRT names (`ucrtbased.dll`, `VCRUNTIME140D.dll`, etc.) are release blockers. |
| .NET Framework 4.7.2 runtime | Required by the C++/CLI project target on machines without it | `docs/BUILD.md:25-27` records the target and test constraint | Microsoft .NET Framework terms | Use the official Microsoft installer; do not embed framework files | **Installer prerequisite/acquisition**. |
| AviUtl2 v2.1.4 x64 host | Yes for the validated guarded hook; not a FreeRenderFPS-owned component | `docs/BUILD.md:16-17`; hook validates the host image in `freefps_aviutl_hook.cpp` | Host project license/distribution is outside this plugin audit | Do not redistribute the host in the FreeRenderFPS package | **User/installer prerequisite**, exact supported version only. |
| Host `lua.dll`, `luaJIT.dll`, and other AviUtl2 files | Host-provided; not loaded as a FreeRenderFPS dependency | Files under `aviutl2_v2.1.4/`; host `credits.txt` contains Lua/LuaJIT notices | Host credits identify MIT/Lua-family notices, but this does not grant redistribution of AviUtl2 itself | Not part of this package | **Exclude** from FreeRenderFPS installer. |
| Debug `.pdb`, `.lib`, `.exp`, `.metagen`, `.recipe`, `Debug/` ZIPs and temporary outputs | No | Present under `src/x264guiEx-FreeFPS/Debug/` and build output | Build artifacts; no runtime license significance | Not release payload | **Exclude**. |

## Minimum installer/package contract

1. Include only the Release `.auo2`, formal adjacent INIs, the cleared source/data
   notices, and profiles whose destination is proven to be FreeRenderFPS-specific.
2. Treat x264, FFmpeg, every external muxer/audio encoder, L-SMASH-Works DLLs,
   and helper DLLs as separate acquisition decisions.  An installer must not
   silently copy the current staged binaries.
3. Pin every acquired binary by SHA-256 and retain its upstream version, source
   URL, license text, and corresponding-source/build configuration.  If any item
   is unknown, stop and leave it out of the package.
4. Install Microsoft VC++/.NET prerequisites from their official installers;
   never package Debug CRT DLLs or copied system DLLs.
5. Keep the original x264guiEx side-by-side.  Do not overwrite its INI, CONF, or
   `x264guiEx_stg` directory, and do not use the tracked generic `x264guiEx.conf`
   as the FreeRenderFPS default.
