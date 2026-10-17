# Builds aux2_output_probe.aux2 (x64, static CRT). Output goes to
# experiments/aux2_output_probe/build/x64/ (git-ignored). Nothing is copied into
# any AviUtl folder.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1

$ErrorActionPreference = 'Continue'

$probeDir = $PSScriptRoot
$sdkDir = Join-Path $probeDir 'sdk'
$probeSource = Join-Path $probeDir 'probe.cpp'
$outDir = Join-Path $probeDir 'build\x64'
$vcBuildDir = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build'

if (-not (Test-Path (Join-Path $vcBuildDir 'vcvarsall.bat'))) {
    Write-Output "vcvarsall.bat not found under $vcBuildDir"
    exit 1
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$lines = @(
    '@echo off',
    "call `"$vcBuildDir\vcvarsall.bat`" x64 >nul 2>nul",
    "cd /d `"$outDir`"",
    "cl /nologo /utf-8 /EHsc /std:c++17 /W4 /O2 /MT /LD /I`"$sdkDir`" `"$probeSource`" /Fo:probe.obj /link /OUT:aux2_output_probe.aux2",
    'if errorlevel 1 (',
    '  echo BUILD FAILED: aux2_output_probe.aux2',
    '  exit /b 1',
    ')',
    'echo BUILD OK: aux2_output_probe.aux2',
    'exit /b 0'
)
$batch = Join-Path $outDir 'build.cmd'
Set-Content -Path $batch -Value ($lines -join "`r`n") -Encoding ascii

& cmd.exe /c "`"$batch`"" | Out-Host
exit $LASTEXITCODE
