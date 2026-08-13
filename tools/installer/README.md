# FreeRenderFPS RC installer

This directory contains the source for the managed Windows installer used by
the v1.0.0-rc1 package. It is deliberately license-safe: the
installer accepts only the four formal adjacent plugin files (one AUO2 and
three INIs), `.stg` presets, and documentation. It rejects `exe_files`, DLLs,
encoders, muxers, debug artifacts, and unknown binaries. Users must acquire
those third-party tools separately under their own license terms.

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
