# Known limitations — v1.0.0-rc1

## Release status

This candidate is not a production v1.0.0 release. The plugin recovery and
installer lifecycle are validated, but the redistributable runtime set and the
complete recovered-build FPS matrix are not.

## Host compatibility

FreeFPS is enabled only for AviUtl2 v2.1.4 x64. The hook checks PE image size,
entry RVA, timeline-builder bytes, and its ordinary caller relationship. A
mismatch fails closed. These guards reduce accidental damage but do not make a
private binary hook version-independent.

## Cache workaround

AviUtl2's observed output cache keys the public integer frame and does not
include the injected double coordinate. When consecutive samples reuse one
integer frame with different coordinates, the scheduler requests a neighboring
integer frame before requesting the target again. This is a compatibility
workaround, not a production cache-key extension.

Consequences:

- extra host renders and lower performance;
- projects with only one frame cannot use eviction;
- other undiscovered caches may still behave differently;
- AFS, x264guiEx keyframe pre-scan, and timecode output are disabled while
  FreeFPS is active.

## Thread/request association

The hook uses a generation-tagged request context protected by SRW locks and
rejects overlapping requests. It avoids a bare global `current_time`, but the
current design permits only one active render session and depends on the
observed v2.1.4 output → timeline-builder sequence.

## Runtime tools

The RC packages do not bundle x264, FFmpeg, muxers, external audio encoders,
L-SMASH-Works tools, `check_vc.dll`, or `check_dotnet.dll`. Exact matching
source/provenance and notices are incomplete. Consequently the RC installer can
install the plugin but cannot guarantee an immediately usable encoding stack.

## Validation gaps

Retained integrated outputs pass 30→60 and 66→60 temporal/cadence checks. The
recovered `v1.0.0-rc1` binary has not completed a clean-package export for:

- FreeFPS OFF;
- 66→59.94;
- 66→24;
- 66→120;
- 120→60.

The installer filesystem lifecycle passes in an isolated sandbox, but a full
clean-machine test using only the final package is blocked by the missing
license-cleared runtime set.

## Installer scope

The current installer is a minimal console EXE, not a graphical path picker.
Its default ProgramData path is automatic; advanced destinations use
`--install-dir`. It requires the extracted `payload` directory beside the EXE.
Upgrade, uninstall, rollback, preservation flags, dry-run, and logging are
implemented.

## Original x264guiEx

Formal binary, INI, project-config key, binary-config identity, and last-output
profile names are isolated. The installer refuses to target the original
`x264guiEx` directory. This was validated at the filesystem level and during
host discovery, but a full two-plugin encoding regression remains required for
v1.0.0.
