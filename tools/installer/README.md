# FreeRenderFPS RC installer

This directory contains the source for the managed Windows installer used by
the v1.0.0 package. It accepts only the four formal adjacent plugin files, the
exact-hash audited x264/FFmpeg/L-SMASH runtime, `.stg` presets, manifests and
documentation. It rejects unknown executables, DLLs, debug artifacts and
unapproved payload files.

Build a framework-dependent Windows executable locally:

```powershell
dotnet build .\tools\installer\FreeRenderFPS.Installer.csproj -c Release -p:Platform=x64
dotnet publish .\tools\installer\FreeRenderFPS.Installer.csproj -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -o .\tools\installer\publish
```

Put a package `payload` directory next to the published executable, or pass
`--source` explicitly. With no command, the executable performs `install` to
`%ProgramData%\aviutl2\Plugin\x264guiEx-FreeRenderFPS`. It never operates on
the original `x264guiEx` directory. `upgrade` creates a timestamped sibling
backup and restores it if replacement fails; `rollback` restores the newest
backup. User `.conf` and `.stg` data are preserved by default; pass
`--remove-user-data` for an explicit destructive uninstall. `--non-interactive`
and `--dry-run` are suitable for deployment tests.

Run the isolated test without touching ProgramData:

```powershell
dotnet run --project .\tools\installer\FreeRenderFPS.Installer.csproj -- --self-test
```
