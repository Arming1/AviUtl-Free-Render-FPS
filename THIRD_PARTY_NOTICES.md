# Third-party notices and license evidence

This file records the license evidence used by the v1.0.0 runtime audit.  It is
not a substitute for shipping the complete license/source package required by a
third-party license.  A binary with incomplete provenance is treated as
**not bundleable**.

## Project and copied source

* FreeRenderFPS-specific code is under the repository MIT notice in `LICENSE`.
* The forked x264guiEx source retains rigaya's MIT headers (for example,
  `src/x264guiEx-FreeFPS/x264guiEx/auo.h:4-24` and
  `src/x264guiEx-FreeFPS/auoCommon/rgy_chapter.cpp:4-24`).  The upstream project
  also identifies itself as MIT in its README: [rigaya/x264guiEx](https://github.com/rigaya/x264guiEx).
  Preserve the copyright and permission text in source and binary distributions.
* The AviUtl output-plugin header carries Kenkun's MIT notice in
  `src/x264guiEx-FreeFPS/x264guiEx/output.h:7-27`; it is also summarized in the
  repository `LICENSE`.
* `tinyxml2` is the permissive Lee Thomason notice reproduced at
  `src/x264guiEx-FreeFPS/tinyxml2/tinyxml2.cpp:1-22` (commercial use and
  redistribution permitted, with the three attribution/altered-source
  conditions).  The complete notice is reproduced in `LICENSE`.
* `nlohmann/json` headers carry SPDX MIT notices for Niels Lohmann in
  `src/x264guiEx-FreeFPS/json/json.hpp` and `json_fwd.hpp`; the repository
  `LICENSE` preserves that attribution.

These source components may be shipped with the plugin, provided that the
original notices remain present.  They do not grant permission for the separate
encoder/muxer binaries below.

## Runtime binaries

### x264 (`x264_3223_x64.exe`)

The staged binary reports:

* x264 `0.165.3223M 0480cb0`;
* `x264 license: GPL version 2 or later`;
* SHA-256 `1C698A1363D5121C02CE2B8FDEA79148F8386E5CD5991EEBBD6B361EABEBDF98`.

The x264 project documents GPL and commercial licensing at
[x264 licensing](https://x264.org/licensing/), and the public source mirror is
marked GPL-2.0 at [mirror/x264](https://github.com/mirror/x264).  GPL permits
redistribution, but an executable distribution must carry the GPL terms and a
valid corresponding-source offer (or source) and must identify modifications.
The current tree has no matching x264 source snapshot, COPYING file, or build
recipe for this exact binary.  Therefore this exact file is **not cleared for
bundling**.  A future package must either add that complete source/notice set or
have the installer acquire a source-complete, checksum-pinned build.

### FFmpeg (`ffmpeg_audenc.exe`, selected as `ffmpeg.exe`)

The staged binary reports FFmpeg `N-97021-gf39678b3a9`, built as a static 32-bit
audio-only build.  Its `--buildconf` includes `--enable-version3`, disables GPL,
and enables several external audio libraries; `-L` prints the LGPL notice.  Its
SHA-256 is
`02E45B2E90484301D69C05AEFAE5AE7DEF8114E4EA96242C329F04710AA111BA`.

FFmpeg's [official legal guidance](https://www.ffmpeg.org/legal.html) says that
the base is LGPLv2.1-or-later, optional GPL components change the result to GPL,
and version-3 components require the corresponding LGPLv3 terms.  The [FFmpeg
license reference](https://ffmpeg.org/doxygen/trunk/md_LICENSE.html) also
requires auditing external libraries enabled in a build.  The exact source
snapshot, external-library notices, and build/source package for this staged
static executable are absent.  It is therefore **not bundleable** for v1.0.0.
Installer acquisition must provide the exact source/configuration and all LGPL,
BSD, and other component notices, or the user must supply `ffmpeg.exe`.

### L-SMASH command-line tools

`muxer_x64.exe`, `remuxer_x64.exe`, and `timelineeditor_x64.exe` identify
themselves as L-SMASH rev1484.  Their hashes are, respectively:

* muxer: `BC07AD5D9A445C1B8FD74B7EDC71CAD7BBD909226D4F57CB1068A1370AB073E5`;
* remuxer: `9A74238807F77B7DDF54D8B5574C7C57A7DF62B99E107B45C06EE6C9612D9F4E`;
* timeline editor: `121358105FF5CBA291EE666D4E7B06F2184509202516AF93858371BB2CA79869`.

The [official L-SMASH project page](https://l-smash.github.io/l-smash/) states
the ISC license and reproduces its required copyright/permission text; the
[official repository](https://github.com/l-smash/l-smash) shows the same ISC
notice.  Redistribution is allowed when that notice is included, but this tree
does not contain the matching rev1484 source/release package.  Treat these tools
as **installer-acquisition/conditional-bundle** items until the exact source and
notice are attached.  The current FreeFPS active path does not require the
timeline editor because AFS/timecode features are disabled while FreeFPS is
enabled.

### L-SMASH-Works DLLs

`LSMASHSource.dll` and `LSMASHSource_indexing.exe` report L-SMASH-Works r1282,
with hashes `612942AC919B26AD9BB0929B415D97FF840D834A9D9EBEE00FC62A453C8606FC`
and `82C93AFD2C9CEDD09E757155118E5394BA84C94A3F8E0877A81EBCA76B4FD2A8`.
The [L-SMASH-Works repository](https://github.com/HomeOfAviSynthPlusEvolution/L-SMASH-Works)
lists FFmpeg, l-smash, zlib, and other build dependencies, but the exact binary
and complete dependency/license manifest are not in this tree.  Do not infer an
ISC-only license for these files.  They are **not bundleable** until a matching
upstream release and all component notices/source obligations are supplied.

### Unknown helper DLLs

`check_vc.dll` (SHA-256
`4B556B8861FF80D51BC1B6EF73138ED86C71F4562F22D29ED595216A2E862AA4`) and
`check_dotnet.dll` (SHA-256
`7E4253C49A0A18406A8373F4A98F6C8B59BAF5988541E769D992E73DD99DFE8B`) have no
product, company, version, copyright, or source information in the current
tree.  Their licenses are **unknown**; they must be excluded from v1.0.0 unless
the upstream author supplies redistribution permission and source/provenance.

## Platform runtimes

The Release project uses `/MD` (`MultiThreadedDLL`) and therefore expects the
Microsoft Visual C++ runtime.  Install the official package from [Microsoft's
latest supported VC++ redistributable page](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist),
under Microsoft's terms; do not copy individual system DLLs into the plugin
folder.  The project also targets .NET Framework 4.7.2 (`docs/BUILD.md:25-27`);
use Microsoft's [official .NET Framework 4.7.2 download](https://dotnet.microsoft.com/download/dotnet-framework/net472)
when needed.  Debug CRT DLLs are never valid release dependencies.

The AviUtl2 host, its `lua.dll`/`luaJIT.dll`, and host credits are outside this
plugin's redistribution scope.  The host files under `aviutl2_v2.1.4/` must not
be copied into the FreeRenderFPS installer.

## Compliance checklist before bundling any external binary

1. Record upstream project, exact version/build, source URL, SHA-256, and build
   options.
2. Include the complete license text and required copyright/attribution.
3. For GPL/LGPL components, include corresponding source (or a legally valid
   written offer) and build/configuration information; audit statically linked
   libraries as well.
4. Confirm that the binary's actual license is not merely the license of its
   project name.  If provenance or license is unknown, leave the file out.

