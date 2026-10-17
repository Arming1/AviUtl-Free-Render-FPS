# Builds and runs the host-free FreeFPS scheduler test for x64 and x86.
# Outputs go to tests/freefps_scheduler/build/<arch>/ (git-ignored).
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File build_and_run.ps1

$ErrorActionPreference = 'Continue'

$testDir = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $testDir '..\..')).Path
$x264Dir = Join-Path $repoRoot 'src\x264guiEx-FreeFPS\x264guiEx'
$encodeDir = Join-Path $x264Dir 'encode'
$auoCommonDir = Join-Path $repoRoot 'src\x264guiEx-FreeFPS\auoCommon'
$vcBuildDir = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build'

$sources = @(
    (Join-Path $testDir 'scheduler_test.cpp'),
    (Join-Path $testDir 'fake_host.cpp'),
    (Join-Path $encodeDir 'freefps_scheduler.cpp'),
    (Join-Path $encodeDir 'freefps_cache_workaround.cpp')
)
$quotedSources = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '

if (-not (Test-Path (Join-Path $vcBuildDir 'vcvarsall.bat'))) {
    Write-Output "vcvarsall.bat not found under $vcBuildDir"
    exit 1
}

function Invoke-Arch {
    param([string]$Arch)

    $outDir = Join-Path $testDir "build\$Arch"
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    # Generated batch file: build, then run only if the build succeeded.
    $lines = @(
        '@echo off',
        "call `"$vcBuildDir\vcvarsall.bat`" $Arch >nul 2>nul",
        "cd /d `"$outDir`"",
        "cl /nologo /EHsc /O2 /std:c++17 /W4 /I`"$x264Dir`" /I`"$encodeDir`" /I`"$auoCommonDir`" /I`"$testDir`" $quotedSources /Fe:scheduler_test.exe",
        'if errorlevel 1 (',
        "  echo BUILD FAILED for $Arch",
        '  exit /b 1',
        ')',
        '.\scheduler_test.exe',
        'echo test_exit=%ERRORLEVEL%',
        'exit /b %ERRORLEVEL%'
    )
    $batch = Join-Path $outDir 'build_and_run.cmd'
    Set-Content -Path $batch -Value ($lines -join "`r`n") -Encoding ascii

    Write-Host "===== $Arch ====="
    & cmd.exe /c "`"$batch`"" | Out-Host
    $code = $LASTEXITCODE
    if ($code -eq 0) {
        Write-Host "$Arch : ALL PASS"
    } else {
        Write-Host "$Arch : FAILED (exit $code)"
    }
    return [int]$code
}

$codes = @()
foreach ($arch in @('x64', 'x86')) {
    $codes += (Invoke-Arch -Arch $arch)
}

if (($codes | Where-Object { $_ -ne 0 }).Count -gt 0) {
    exit 1
}
exit 0
