# FreeRenderFPS RC installer

This directory contains the source for the managed Windows installer used by
the v1.1.0 package. It accepts only the formal adjacent plugin files
(`x264guiEx-FreeRenderFPS.aux2` plus the three `.ini` files), the exact-hash
audited x264/FFmpeg/L-SMASH runtime, `.stg` presets, manifests and
documentation. It rejects unknown executables, DLLs, debug artifacts, `.auo2`
output plugins and unapproved payload files.

Build a framework-dependent Windows executable locally:

```powershell
dotnet build .\tools\installer\FreeRenderFPS.Installer.csproj -c Release -p:Platform=x64
dotnet publish .\tools\installer\FreeRenderFPS.Installer.csproj -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -o .\tools\installer\publish
```

Put a package `payload` directory next to the published executable, or pass
`--source` explicitly. With no command, the executable performs `install` to
`%ProgramData%\aviutl2\Plugin\x264guiEx-FreeRenderFPS`. It never operates on
the original `x264guiEx` directory. `upgrade` creates a timestamped backup and
restores it if replacement fails; `rollback` restores the newest backup. User
`.conf` and `.stg` data are preserved by default; pass `--remove-user-data` for
an explicit destructive uninstall. `--non-interactive` and `--dry-run` are
suitable for deployment tests.

## Backup location

Backups, staging and swap folders are created beside the `Plugin` folder, in
`%ProgramData%\aviutl2\FreeRenderFPS-backups`, never inside `Plugin`. AviUtl2
loads plugins from `Plugin` and one level below it, so a copy of the `.aux2`
kept there would load as a duplicate. A failed cleanup therefore cannot leave a
loadable plugin copy under `Plugin`.

`rollback` searches `FreeRenderFPS-backups` and also the older
`Plugin\x264guiEx-FreeRenderFPS.backup-*` folders, and uses the newest one unless
`--backup <dir>` is given.

## Legacy `.auo2`

On install/upgrade, a legacy `x264guiEx-FreeRenderFPS.auo2` already present in
the target folder is kept in the timestamped backup and is not carried into the
new install, so AviUtl2 loads only the `.aux2`. Rollback accepts backups that
still hold the legacy `.auo2` and restores it.

## Legacy backups under Plugin

Default `install`, `upgrade`, `uninstall` and `rollback` never touch existing
`Plugin\x264guiEx-FreeRenderFPS.backup-*` folders. To move them out of the plugin
search path, run the opt-in command:

```powershell
FreeRenderFPS.Installer.exe --cleanup-legacy-backups --dry-run   # list only
FreeRenderFPS.Installer.exe --cleanup-legacy-backups             # move
```

Each folder is moved (not deleted) into `FreeRenderFPS-backups` with a
`-from-plugin-<stamp>` suffix, and every move is logged. The command cannot be
combined with another command. The installer never touches
`free_fps_export.aux2`.

Run the isolated test without touching ProgramData:

```powershell
dotnet run --project .\tools\installer\FreeRenderFPS.Installer.csproj -- --self-test
```

The self-test also covers: backups created outside `Plugin`; no `.aux2` or
`.auo2` under `Plugin` outside the install folder after install, upgrade,
uninstall, rollback and cleanup; legacy-location backup rollback; and
`--cleanup-legacy-backups` moving (not deleting) old folders.
