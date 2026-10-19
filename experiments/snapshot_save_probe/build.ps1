# Builds SnapshotSaveProbe.aux2 (x64, static CRT) and copies it to test_kit\11_快照保存 (see README.md).
# Output goes to experiments/snapshot_save_probe/build/x64/ (git-ignored). The SDK headers are read from
# experiments/free_fps_export/sdk/ (not copied). Nothing is installed into any AviUtl folder.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1

$ErrorActionPreference = 'Continue'

$probeDir = $PSScriptRoot
$repoDir = Split-Path -Parent (Split-Path -Parent $probeDir)
$sdkDir = Join-Path $repoDir 'experiments\free_fps_export\sdk'
$probeSource = Join-Path $probeDir 'snapshot_save_probe.cpp'
$outDir = Join-Path $probeDir 'build\x64'
$testKitDir = Join-Path $repoDir 'test_kit\11_快照保存'
$vcBuildDir = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build'

if (-not (Test-Path (Join-Path $vcBuildDir 'vcvarsall.bat'))) {
    Write-Output "vcvarsall.bat not found under $vcBuildDir"
    exit 1
}
if (-not (Test-Path (Join-Path $sdkDir 'plugin2.h'))) {
    Write-Output "plugin2.h not found under $sdkDir"
    exit 1
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$lines = @(
    '@echo off',
    "call `"$vcBuildDir\vcvarsall.bat`" x64 >nul 2>nul",
    "cd /d `"$outDir`"",
    "cl /nologo /utf-8 /EHsc /std:c++17 /W4 /O2 /MT /LD /I`"$sdkDir`" `"$probeSource`" /Fo:snapshot_save_probe.obj /link /OUT:SnapshotSaveProbe.aux2",
    'if errorlevel 1 (',
    '  echo BUILD FAILED: SnapshotSaveProbe.aux2',
    '  exit /b 1',
    ')',
    'echo BUILD OK: SnapshotSaveProbe.aux2',
    'exit /b 0'
)
$batch = Join-Path $outDir 'build.cmd'
Set-Content -Path $batch -Value ($lines -join "`r`n") -Encoding ascii

& cmd.exe /c "`"$batch`"" | Out-Host
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

# Copy the built plugin into the test kit (test_kit is not an AviUtl folder; install.ps1 does that step).
New-Item -ItemType Directory -Force -Path $testKitDir | Out-Null
$built = Join-Path $outDir 'SnapshotSaveProbe.aux2'
Copy-Item -LiteralPath $built -Destination (Join-Path $testKitDir 'SnapshotSaveProbe.aux2') -Force
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $testKitDir 'SnapshotSaveProbe.aux2')).Hash
Write-Output "COPIED to test_kit: SHA256 $hash"
exit 0
