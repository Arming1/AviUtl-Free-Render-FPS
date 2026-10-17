# Installing FreeRenderFPS v1.0.1

Close AviUtl2 before installation.

Supported hosts:

- AviUtl2 v2.1.4 x64: the built-in FreeRenderFPS hook (`Enable Free Render FPS`)
  is available.
- AviUtl2 v2.1.12 x64: use **Edit → Free FPS Export...** to get free render
  FPS. The built-in hook is not enabled on v2.1.12: with `Enable Free Render FPS`
  turned on, the output plugin rejects the host (fail closed). With it turned
  off, ordinary output still works.

## Plugin files

The AviUtl2 build is one general plugin, `x264guiEx-FreeRenderFPS.aux2`, placed
in `Plugin\x264guiEx-FreeRenderFPS\` together with its three adjacent INI files
(`x264guiEx-FreeRenderFPS.ini`, `.en.ini`, `.zh.ini`), `exe_files`, and the
`x264guiEx_stg` presets. It provides both the x264guiEx FreeRenderFPS output
plugin and the Edit menu item **Free FPS Export...**. Keep the `.aux2` and its
INI files together under the same name.

## Setup package

Extract the complete setup ZIP and keep the setup EXE beside its `payload`
directory. Run the EXE, then start AviUtl2. On first discovery, review the host
warning, trust `x264guiEx-FreeRenderFPS.aux2`, and restart AviUtl2 if requested.

The default destination is
`%ProgramData%\aviutl2\Plugin\x264guiEx-FreeRenderFPS`. Advanced isolated
installs can use `--target`. Run the installer with `--help` for upgrade,
uninstall, rollback, preservation, logging, dry-run and non-interactive options.

## Portable package

Extract the portable ZIP directly over the AviUtl2 root. It already contains:

`data\Plugin\x264guiEx-FreeRenderFPS`

Do not create a second root-level `Plugin` copy. Exactly one
`x264guiEx-FreeRenderFPS.aux2` should load.

## Free FPS Export (AviUtl2 v2.1.12)

After installing the plugin, open **Edit → Free FPS Export...** in AviUtl2
v2.1.12. It re-times the project with public SDK APIs and exports at any
rational FPS. The project must be saved to a file first.

This item replaces the separate experimental `free_fps_export.aux2`. Do not
install that file (see below).

## Upgrading from v1.0.x

- The installer (install or upgrade) backs up the installed folder and removes
  the old `x264guiEx-FreeRenderFPS.auo2` from it; the copy stays in the
  timestamped backup. If you installed manually, delete
  `Plugin\x264guiEx-FreeRenderFPS\x264guiEx-FreeRenderFPS.auo2` yourself.
  AviUtl2 must not load both the `.auo2` and the `.aux2`.
- Delete `free_fps_export.aux2` if you copied the experimental file earlier
  (for example `Plugin\free_fps_export\free_fps_export.aux2`). The installer does
  not touch it, and it duplicates the Free FPS Export item.
- Installer backups now go to `C:\ProgramData\aviutl2\FreeRenderFPS-backups`,
  outside `Plugin`, so AviUtl2 does not load them. Older backups
  `C:\ProgramData\aviutl2\Plugin\x264guiEx-FreeRenderFPS.backup-*` still sit in
  `Plugin` and each holds a copy of the plugin binary (for example the two
  `...backup-20260813-...` and `...backup-20260818-...` folders contain an extra
  `.auo2`). AviUtl2 reads plugins one folder below `Plugin`, so these copies can
  load as duplicates. Default install, upgrade and uninstall leave them alone.
  To move them into `FreeRenderFPS-backups` (moves only, nothing is deleted),
  run the installer with `--cleanup-legacy-backups` (add `--dry-run` to list them
  first). Rollback still finds them if you need to restore one.

## Verify

Compare the package with `SHA256SUMS.txt`. The runtime hashes are also locked
in `THIRD_PARTY_RELEASE_ARTIFACTS.json`. A mismatch or unsupported AviUtl2 host
must not be bypassed.
