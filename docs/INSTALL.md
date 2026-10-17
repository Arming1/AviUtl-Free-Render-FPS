# Installing FreeRenderFPS v1.0.1

Supported host: AviUtl2 v2.1.4 x64. Close AviUtl2 before installation.

## Setup package

Extract the complete setup ZIP and keep the setup EXE beside its `payload`
directory. Run the EXE, then start AviUtl2. On first discovery, review the host
warning, trust `x264guiEx-FreeRenderFPS.auo2`, and restart AviUtl2 if requested.

The default destination is
`%ProgramData%\aviutl2\Plugin\x264guiEx-FreeRenderFPS`. Advanced isolated
installs can use `--target`. Run the installer with `--help` for upgrade,
uninstall, rollback, preservation, logging, dry-run and non-interactive options.

## Portable package

Extract the portable ZIP directly over the AviUtl2 root. It already contains:

`data\Plugin\x264guiEx-FreeRenderFPS`

Do not create a second root-level `Plugin` copy. Exactly one AUO2 should load.

## Verify

Compare the package with `SHA256SUMS.txt`. The runtime hashes are also locked
in `THIRD_PARTY_RELEASE_ARTIFACTS.json`. A mismatch or unsupported AviUtl2 host
must not be bypassed.
