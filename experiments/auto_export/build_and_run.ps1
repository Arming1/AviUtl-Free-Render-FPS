# Builds auto_export.aux2 and the host-free test, then runs the test.
# Outputs go to experiments/auto_export/build/x64/ (git-ignored). Nothing is
# copied into any AviUtl folder.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File build_and_run.ps1

$ErrorActionPreference = 'Continue'

$exportDir = $PSScriptRoot
$sdkDir = Join-Path $exportDir 'sdk'
$pluginSource = Join-Path $exportDir 'auto_export.cpp'
$testSource = Join-Path $exportDir 'tests\host_stub_test.cpp'
$outDir = Join-Path $exportDir 'build\x64'
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
    "cl /nologo /utf-8 /EHsc /std:c++17 /W4 /O2 /LD /I`"$sdkDir`" `"$pluginSource`" /Fo:auto_export_plugin.obj /link /OUT:auto_export.aux2",
    'if errorlevel 1 (',
    '  echo BUILD FAILED: auto_export.aux2',
    '  exit /b 1',
    ')',
    "cl /nologo /utf-8 /EHsc /std:c++17 /W4 /O2 /I`"$sdkDir`" `"$testSource`" /Fo:host_stub_test.obj /Fe:host_stub_test.exe",
    'if errorlevel 1 (',
    '  echo BUILD FAILED: host_stub_test.exe',
    '  exit /b 1',
    ')',
    '.\host_stub_test.exe',
    'echo test_exit=%ERRORLEVEL%',
    'exit /b %ERRORLEVEL%'
)
$batch = Join-Path $outDir 'build_and_run.cmd'
Set-Content -Path $batch -Value ($lines -join "`r`n") -Encoding ascii

& cmd.exe /c "`"$batch`"" | Out-Host
exit $LASTEXITCODE
