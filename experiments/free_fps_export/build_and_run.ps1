# Builds free_fps_export.aux2, retime_cli.exe and the tests, then runs them:
#   build/x64/retime_test.exe      pure re-timer unit tests (no AviUtl)
#   build/x64/host_stub_test.exe   plugin flow against a fake host (no window, no message box)
#   tests/crosscheck.py            C++ re-timer against tools/aup2_fps_upconvert.py
# Outputs go to experiments/free_fps_export/build/x64/ (git-ignored). Nothing is copied into any AviUtl folder.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File build_and_run.ps1

$ErrorActionPreference = 'Continue'

$exportDir = $PSScriptRoot
$sdkDir = Join-Path $exportDir 'sdk'
$outDir = Join-Path $exportDir 'build\x64'
$vcBuildDir = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build'

if (-not (Test-Path (Join-Path $vcBuildDir 'vcvarsall.bat'))) {
    Write-Output "vcvarsall.bat not found under $vcBuildDir"
    exit 1
}

$python = $null
$pythonCmd = Get-Command python -ErrorAction SilentlyContinue
if ($pythonCmd) {
    $python = $pythonCmd.Source
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$common = '/nologo /utf-8 /EHsc /std:c++17 /W4 /O2 /MT'
$lines = @(
    '@echo off',
    "call `"$vcBuildDir\vcvarsall.bat`" x64 >nul 2>nul",
    "cd /d `"$outDir`"",
    "cl $common /I`"$sdkDir`" /I`"$exportDir`" /c `"$exportDir\retime.cpp`" /Fo:retime.obj",
    'if errorlevel 1 (',
    '  echo BUILD FAILED: retime.obj',
    '  exit /b 1',
    ')',
    "cl $common /LD /I`"$sdkDir`" /I`"$exportDir`" `"$exportDir\free_fps_export.cpp`" retime.obj /Fo:free_fps_export_plugin.obj /link /OUT:free_fps_export.aux2",
    'if errorlevel 1 (',
    '  echo BUILD FAILED: free_fps_export.aux2',
    '  exit /b 1',
    ')',
    "cl $common /I`"$exportDir`" `"$exportDir\retime_cli.cpp`" retime.obj /Fo:retime_cli.obj /Fe:retime_cli.exe",
    'if errorlevel 1 (',
    '  echo BUILD FAILED: retime_cli.exe',
    '  exit /b 1',
    ')',
    "cl $common /I`"$exportDir`" `"$exportDir\tests\retime_test.cpp`" retime.obj /Fo:retime_test.obj /Fe:retime_test.exe",
    'if errorlevel 1 (',
    '  echo BUILD FAILED: retime_test.exe',
    '  exit /b 1',
    ')',
    "cl $common /I`"$sdkDir`" /I`"$exportDir`" `"$exportDir\tests\host_stub_test.cpp`" retime.obj /Fo:host_stub_test.obj /Fe:host_stub_test.exe",
    'if errorlevel 1 (',
    '  echo BUILD FAILED: host_stub_test.exe',
    '  exit /b 1',
    ')',
    'echo === retime_test ===',
    '.\retime_test.exe',
    'set RT=%ERRORLEVEL%',
    'echo retime_test_exit=%RT%',
    'echo === host_stub_test ===',
    '.\host_stub_test.exe',
    'set HS=%ERRORLEVEL%',
    'echo host_stub_test_exit=%HS%'
)
if ($python) {
    $lines += @(
        'echo === crosscheck ===',
        "`"$python`" -I `"$exportDir\tests\crosscheck.py`" --exe `"$outDir\retime_cli.exe`"",
        'set CC=%ERRORLEVEL%',
        'echo crosscheck_exit=%CC%'
    )
} else {
    $lines += 'echo crosscheck SKIPPED: python not found on PATH'
    $lines += 'set CC=0'
}
$lines += 'if not "%RT%"=="0" exit /b 1'
$lines += 'if not "%HS%"=="0" exit /b 1'
$lines += 'if not "%CC%"=="0" exit /b 1'
$lines += 'exit /b 0'

$batch = Join-Path $outDir 'build_and_run.cmd'
Set-Content -Path $batch -Value ($lines -join "`r`n") -Encoding ascii

& cmd.exe /c "`"$batch`"" | Out-Host
exit $LASTEXITCODE
