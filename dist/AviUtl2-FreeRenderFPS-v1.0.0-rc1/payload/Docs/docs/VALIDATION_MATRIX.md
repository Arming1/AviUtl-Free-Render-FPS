# FreeRenderFPS validation matrix

Date: 2026-08-13  
Candidate: v1.0.0-rc1  
Supported host claim: AviUtl2 v2.1.4 x64 only

## Status vocabulary

- **PASS**: retained artifact or executed check supports every listed metric.
- **PASS (lifecycle only)**: installer filesystem behavior passed; this does not
  mean an encode completed from that package.
- **NOT REVALIDATED**: the task names the conversion as historically successful,
  but no recovered RC1 artifact/log supports a fresh metrics claim.
- **BLOCKED**: a prerequisite required by the release definition is absent.

## Recovery and host checks

| Check | Result | Evidence |
|---|---|---|
| Backup before recovery | PASS | Workspace and ProgramData ZIPs plus manifest in the Codex checkpoint directory; hashes recorded in `FINAL_REPORT.md` |
| Release x64 rebuild | PASS | 0 errors, 7 existing warnings; plugin SHA-256 `66266D5D08DF368D067B3FF74C0815965BD5177E2FF9C4E04181D053EAFD5DD5` |
| Debug runtime absent | PASS | `dumpbin /dependents` contains Release CRT names; no `ucrtbased`, `VCRUNTIME140D`, or `MSVCP140D` |
| Plugin appears once | PASS | Clean ProgramData `Plugin` enumeration contained only `x264guiEx-FreeRenderFPS` during recovery test |
| Settings open / close | PASS | Release plugin UI opened and closed without exception after the fixed config identity was rebuilt |
| Project config callback save/load | PASS | `project_config_roundtrip.exe`: `save=PASS load=PASS key=freerenderfps_config bytes=1300 single_line=PASS fields=PASS` |
| Project save / reopen / resave | PASS | `recovery_release_roundtrip.aup2` was saved, reopened, and saved again without the prior structured exception |
| FreeFPS OFF output | BLOCKED | No retained recovered-RC1 encode artifact |

## Temporal-output matrix

| Project → target | Project frames | Output frames | Video FPS | Video duration | Audio duration | Adjacent duplicates | Unique frames | Cadence/crash | Result |
|---|---:|---:|---|---:|---:|---:|---:|---|---|
| 30 → 60 | 161 | 322 | 60/1 | 5.366667 s | 5.366667 s | 0 | 322 | no reported crash; every `(2k,2k+1)` differs | **PASS on retained integrated artifact** |
| 66 → 60 | 330 | 300 | 60/1 | 5.000000 s | 5.000000 s | 0 | 300 | 0 cadence spikes; no crash | **PASS on retained integrated artifact** |
| 66 → 59.94 | — | — | — | — | — | — | — | no recovered metrics | **NOT REVALIDATED** |
| 66 → 24 | — | — | — | — | — | — | — | no recovered metrics | **NOT REVALIDATED** |
| 66 → 120 | — | — | — | — | — | — | — | no recovered metrics | **NOT REVALIDATED** |
| 120 → 60 | — | — | — | — | — | — | — | no recovered metrics | **NOT REVALIDATED** |

Retained media:

- `experiments/subframe_scheduler_test/phase7_freefps_60_true.mp4`, SHA-256
  `4BA43DB0D7940BCE5150163992C765FF6B8211A2A0911E2F5235478E80BBC145`.
- `experiments/66_to_60_validation/freerenderfps_66_to_60_v3.mp4`, SHA-256
  `E88A7C3EE307CBBF2E0566F70EC07A94797D0304E0746EF377ABEFEB7A5A92AF`.
- Machine-readable 66→60 metrics:
  `experiments/66_to_60_validation/66_to_60_summary.json`.

The two PASS rows validate the integrated FreeRenderFPS code line, including
true changing scene states, but were produced before the final recovery-only
identity/version edits. The final RC1 plugin itself was rebuilt and callback/UI
checked, not fully re-encoded.

## Installer matrix

| Scenario | Result | Scope |
|---|---|---|
| Clean install | PASS (lifecycle only) | Formal four plugin files installed from an isolated package payload |
| Upgrade | PASS (lifecycle only) | Timestamped backup created; replacement file installed; config and presets preserved |
| Uninstall | PASS (lifecycle only) | Runtime removed; requested config/presets retained |
| Rollback | PASS (lifecycle only) | Latest validated backup restored |
| Original x264guiEx coexistence | PASS (filesystem) | Sentinel in original directory remained unchanged through the complete test |
| Clean package → real encode | BLOCKED | License-cleared external encoder/audio/mux runtime set is absent |

## Release decision

The required matrix is incomplete. In accordance with the user's release
blocker rules, **v1.0.0 must not be published**. RC1 is suitable for continued
license/runtime integration and the missing final regression runs.
