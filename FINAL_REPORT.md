# AviUtl2 FreeRenderFPS final recovery report

Date: 2026-08-13  
Outcome: **recovery completed; v1.0.0 release withheld; v1.0.0-rc1 produced**

## Executive decision

The damaged plugin environment was recovered, the crashes were root-caused and
fixed, a clean Release x64 plugin was produced, and a guarded installer/package
pipeline now passes its isolated lifecycle test. However, the attached task
explicitly forbids publishing v1.0.0 when the runtime dependency set is unclear
or key FPS validation is missing. Both conditions remain true. Therefore no
artifact is labeled as production v1.0.0.

## Recovery backups

Before recovery, the workspace and ProgramData state were copied to reversible
checkpoints:

| Backup | Bytes | SHA-256 |
|---|---:|---|
| `AviUtl2-FreeRenderFPS-recovery-workspace-20260813-120251.zip` | 78,204,633 | `32DA55D0DFE5D5CD2A212A545B34509D8231B8EDDAB5EE09DE26C78BC82A1743` |
| `AviUtl2-FreeRenderFPS-recovery-programdata-20260813-120251.zip` | 119,963,992 | `3B3865CF9F5CDB40849F09BC2270383B8EA623B4304B887BE323F49882E423B7` |
| `AviUtl2-FreeRenderFPS-recovery-manifest-20260813-120251.txt` | 1,479 | `0328916EB497895FC8002592191BDF89307DA152F531110FEA5926354C710AD6` |

The polluted live Plugin tree and the stale direct ProgramData FreeFPS tree were
moved into the checkpoint area, not deleted. Current ProgramData `Plugin`
enumeration contains one formal `x264guiEx-FreeRenderFPS` directory.

## Crash root cause

The failures were not one generic runtime problem; four concrete defects
combined:

1. The deployed AUO2 was a Debug x64 build (SHA-256 beginning `1D1779...`) with
   `ucrtbased.dll`, `VCRUNTIME140D.dll`, and `MSVCP140D.dll` dependencies. Both
   recorded project-save exceptions were access violations in `ucrtbased.dll`.
2. The fork still identified itself as `x264guiEx.auo2` for module lookup and
   could resolve the original plugin's resource/config directory when both were
   present.
3. The fork reused generic project/config identities, while the project-save
   callback could serialize a zero-initialized global configuration.
4. During formalization, a long config identity was assigned to the legacy
   32-byte `CONF_GUIEX_HEADER::conf_name` buffer. `sprintf_s` invoked the invalid
   parameter handler, reproducing the settings crash. The binary identity is now
   the bounded `FreeRenderFPS ConfigFile v1`; the long JSON identity stays in a
   dynamically sized string.

Detailed evidence and rejected hypotheses are in
[docs/RECOVERY_ANALYSIS.md](docs/RECOVERY_ANALYSIS.md).

## Fixes

- Formal display/binary name: `x264guiEx FreeRenderFPS` /
  `x264guiEx-FreeRenderFPS.auo2`.
- Module self-location uses `GetModuleHandleEx(...FROM_ADDRESS...)`.
- Dedicated INIs, project key `freerenderfps_config`, JSON/binary config
  identities, and last-output profile name.
- Config initialization before project save; guarded import of only legacy JSON
  that actually contains FreeFPS fields.
- Release-only build; no Debug CRT or PDB in packages.
- Rational FPS UI and scheduler, version guard, request association, and cache
  workaround kept in focused modules.
- Research probes remain outside the formal payload.

## Formal Release build candidate

| Property | Value |
|---|---|
| Version | `1.0.0-rc1` |
| Configuration | Release x64 |
| Result | 0 errors, 7 existing warnings |
| Binary bytes | 1,368,576 |
| Plugin SHA-256 | `66266D5D08DF368D067B3FF74C0815965BD5177E2FF9C4E04181D053EAFD5DD5` |
| Runtime imports | Release MSVC/UCRT + .NET Framework; no Debug CRT |
| Supported host | AviUtl2 v2.1.4 x64 only |

Project-config callback regression result:

```text
save=PASS load=PASS key=freerenderfps_config bytes=1300 single_line=PASS fields=PASS
```

Host recovery checks passed for plugin uniqueness, settings open/close, project
save, reopen, and resave. A FreeFPS-disabled real encode was not retained.

## Installer implementation

`tools/installer/Program.cs` implements install, upgrade, uninstall, rollback,
dry-run, log, preservation flags, and an isolated self-test. It defaults to the
dedicated ProgramData plugin directory; refuses targets named/containing the
original `x264guiEx`; rejects reparse points and disallowed payload binaries;
creates timestamped backups; and rolls back failed replacements.

The self-test passed this sequence:

```text
install -> upgrade (config/preset preserved) -> uninstall -> rollback
```

The original x264guiEx sentinel remained unchanged. This is a filesystem
lifecycle PASS, not a clean-machine encoding PASS.

## Runtime dependencies and release blocker

The exact staged x264 binary reports GPLv2-or-later, but its corresponding
source/COPYING/build recipe is absent. The exact static FFmpeg build and linked
third-party notices/source are incomplete. Matching source/provenance is also
missing for the L-SMASH CLI build, L-SMASH-Works helpers, and `check_*` DLLs.

The RC package therefore excludes all such binaries. This is legally safer but
means a normal user cannot yet download the package and immediately encode.
See [docs/RUNTIME_DEPENDENCIES.md](docs/RUNTIME_DEPENDENCIES.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Output validation

Retained integrated artifacts prove:

- 30→60: 322 frames, 60/1 FPS, 5.366667 s video/audio, 322 unique frames,
  zero adjacent duplicates.
- 66→60: 300 frames, 60/1 FPS, 5.000000 s video/audio, 300 unique frames,
  zero adjacent duplicates and zero detected cadence spikes.

The recovered RC1 lacks new artifacts for 66→59.94, 66→24, 66→120, and
120→60. It also lacks the required final FreeFPS-OFF export. Honest statuses and
media hashes are recorded in [docs/VALIDATION_MATRIX.md](docs/VALIDATION_MATRIX.md).

## Package artifacts

The reproducible script creates license-safe RC artifacts under:

```text
dist\AviUtl2-FreeRenderFPS-v1.0.0-rc1\
```

It emits a setup EXE accompanied by its payload, a setup ZIP, a portable ZIP,
and SHA-256 manifests. The exact final artifact hashes are generated in
`SHA256SUMS.txt`. These are evaluation artifacts, not v1.0.0 downloads.

Final RC1 artifact hashes from that manifest:

| Artifact | SHA-256 |
|---|---|
| `AviUtl2-FreeRenderFPS-v1.0.0-rc1-setup.exe` | `3A60D4A0471905CFEAAFB2DD7A558965CD2646B1611FCC45547009109478D7A2` |
| `AviUtl2-FreeRenderFPS-v1.0.0-rc1-setup.zip` | `A3FBAE6CEA1C38988C2E5727D6756D35D141099AB50BD0ADE738E2D2C0F5BFE3` |
| `AviUtl2-FreeRenderFPS-v1.0.0-rc1-portable.zip` | `815F410EAB649A790808D64FDEEE98AF3A2F533286C12D250A88CB75B2EF73A6` |

## Repository status

The repository root is organized with production code under `src`, research
under `experiments`, release tooling under `tools`, and technical/release
documents under `docs`. `reference` and the local AviUtl2 host are ignored and
were not modified. A root Git repository was initialized after artifact
generation so the intended source, documents, tests, and RC artifacts can be
reviewed as one local initial commit.

## Remaining v1.0.0 blockers

1. Supply or automatically acquire a checksum-pinned, source-complete,
   redistribution-compliant encoder/audio/mux runtime set.
2. Run clean-install real encodes using only the final package.
3. Revalidate FreeFPS OFF and the complete requested FPS matrix on the final
   binary.
4. Complete two-plugin real encoding coexistence testing.
5. Only then rename/sign/tag artifacts as v1.0.0.

Until those items pass, the correct result is **v1.0.0-rc1, release withheld**.
