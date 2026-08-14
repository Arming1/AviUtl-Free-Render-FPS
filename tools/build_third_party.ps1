[CmdletBinding()]
param(
    [string]$Msys2Root = 'D:\frfps-msys64',
    [string]$WorkRoot = 'D:\frfps-third-party-work-replay',
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\build\third_party'),
    [switch]$SkipBuild,
    [switch]$RequireReleaseHashes,
    [string]$ArtifactManifest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$lockPath = Join-Path $repoRoot 'third_party\sources.lock.json'
$sourceCache = Join-Path $repoRoot 'build\source-cache'
$artifactRoot = Join-Path $OutputRoot 'artifacts'
$logRoot = Join-Path $OutputRoot 'logs'
$bash = Join-Path $Msys2Root 'usr\bin\bash.exe'
$lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
$sourceDateEpoch = [string]$lock.reproducibility.sourceDateEpoch

function Write-Step([string]$Message) {
    Write-Host "[third-party] $Message" -ForegroundColor Cyan
}

function Assert-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing ${Description}: $Path"
    }
}

function Assert-Hash([string]$Path, [string]$Expected) {
    Assert-File $Path 'hash-locked file'
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actual -ne $Expected) {
        throw "SHA-256 mismatch for $Path. Expected $Expected; actual $actual"
    }
}

function Invoke-Native([string]$FilePath, [string[]]$Arguments, [string]$LogName) {
    $log = Join-Path $logRoot $LogName
    Write-Step ("{0} {1}" -f $FilePath, ($Arguments -join ' '))
    $savedPreference = $ErrorActionPreference
    try {
        # Windows PowerShell promotes native stderr lines to ErrorRecord objects.
        # Do not treat normal git/compiler progress as a terminating exception;
        # the native process exit code remains the authoritative result.
        $ErrorActionPreference = 'Continue'
        & $FilePath @Arguments 2>&1 | Tee-Object -FilePath $log
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedPreference
    }
    if ($exitCode -ne 0) {
        throw "Command failed with exit code $exitCode. See $log"
    }
}

function Convert-ToMsysPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full -notmatch '^([A-Za-z]):\\(.*)$') { throw "Not a drive-qualified Windows path: $full" }
    return '/' + $Matches[1].ToLowerInvariant() + '/' + $Matches[2].Replace('\', '/')
}

function Find-MSBuild {
    $command = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($null -ne $command) { return $command.Source }
    $candidates = @(
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    throw 'Visual Studio MSBuild with the v143 C++ toolset was not found.'
}

function Confirm-GitCommit([string]$Directory, [string]$Expected) {
    $actual = (& git -C $Directory rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $actual -ne $Expected) {
        throw "Git commit mismatch in $Directory. Expected $Expected; actual $actual"
    }
}

function Apply-PatchOnce([string]$Directory, [string]$PatchPath, [string]$Stem) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & git -C $Directory apply --check $PatchPath 2>$null
        $forwardExit = $LASTEXITCODE
        if ($forwardExit -eq 0) {
            Invoke-Native git @('-C', $Directory, 'apply', $PatchPath) "apply-$Stem.log"
            return
        }
        & git -C $Directory apply --reverse --check $PatchPath 2>$null
        $reverseExit = $LASTEXITCODE
        if ($reverseExit -eq 0) {
            Write-Step "Patch already applied: $PatchPath"
            return
        }
    }
    finally {
        $ErrorActionPreference = $savedPreference
    }
    throw "Patch is neither applicable nor already applied: $PatchPath"
}

New-Item -ItemType Directory -Force -Path $sourceCache, $artifactRoot, $logRoot, $WorkRoot | Out-Null
Assert-File $bash 'isolated MSYS2 bash'

$ffmpeg = $lock.sources | Where-Object component -eq 'FFmpeg'
$lsmash = $lock.sources | Where-Object component -eq 'L-SMASH'
$x264 = $lock.sources | Where-Object component -eq 'x264'
$ffmpegArchive = Join-Path $sourceCache $ffmpeg.archive
$ffmpegSignature = $ffmpegArchive + '.asc'
$lsmashArchive = Join-Path $sourceCache $lsmash.archive
$x264Archive = Join-Path $sourceCache $x264.archive

if (-not (Test-Path -LiteralPath $ffmpegArchive)) { Invoke-WebRequest -Uri $ffmpeg.officialSource -OutFile $ffmpegArchive }
if (-not (Test-Path -LiteralPath $ffmpegSignature)) { Invoke-WebRequest -Uri $ffmpeg.signature -OutFile $ffmpegSignature }
if (-not (Test-Path -LiteralPath $lsmashArchive)) { Invoke-WebRequest -Uri $lsmash.officialArchive -OutFile $lsmashArchive }
Assert-Hash $ffmpegArchive $ffmpeg.archiveSha256
Assert-Hash $ffmpegSignature $ffmpeg.signatureSha256
Assert-Hash $lsmashArchive $lsmash.archiveSha256

$x264Dir = Join-Path $WorkRoot 'x264'
$lsmashDir = Join-Path $WorkRoot 'l-smash'
$ffmpegDir = Join-Path $WorkRoot 'ffmpeg-8.1.2'
if (-not (Test-Path -LiteralPath (Join-Path $x264Dir '.git'))) {
    Invoke-Native git @('clone', '--no-checkout', $x264.officialRepository, $x264Dir) 'clone-x264.log'
}
Invoke-Native git @('-C', $x264Dir, 'fetch', '--force', 'origin', $x264.commit) 'fetch-x264.log'
Invoke-Native git @('-C', $x264Dir, 'checkout', '--detach', '--force', $x264.commit) 'checkout-x264.log'
Confirm-GitCommit $x264Dir $x264.commit
Invoke-Native git @('-C', $x264Dir, 'archive', '--format=tar.gz', "--prefix=x264-$($x264.commit)/", '-o', $x264Archive, $x264.commit) 'archive-x264.log'
Assert-Hash $x264Archive $x264.archiveSha256

if (-not (Test-Path -LiteralPath (Join-Path $lsmashDir 'L-SMASH.sln'))) {
    $extractRoot = Join-Path $WorkRoot 'l-smash-extract'
    if (Test-Path -LiteralPath $extractRoot) { Remove-Item -LiteralPath $extractRoot -Recurse -Force }
    New-Item -ItemType Directory -Path $extractRoot | Out-Null
    Invoke-Native tar @('-xf', $lsmashArchive, '-C', $extractRoot) 'extract-lsmash.log'
    $extracted = Get-ChildItem -LiteralPath $extractRoot -Directory | Select-Object -First 1
    if ($null -eq $extracted -or $extracted.Name -notmatch [regex]::Escape($lsmash.commit)) {
        throw 'L-SMASH archive root does not identify the locked commit.'
    }
    Move-Item -LiteralPath $extracted.FullName -Destination $lsmashDir
    Remove-Item -LiteralPath $extractRoot -Force
    Invoke-Native git @('-C', $lsmashDir, 'init') 'init-lsmash.log'
    Invoke-Native git @('-C', $lsmashDir, 'add', '--all') 'index-lsmash.log'
}
foreach ($patch in $lsmash.patches) {
    $patchPath = Join-Path $repoRoot $patch.path
    Assert-Hash $patchPath $patch.sha256
    Apply-PatchOnce $lsmashDir $patchPath ([IO.Path]::GetFileNameWithoutExtension($patch.path))
}

if (-not (Test-Path -LiteralPath (Join-Path $ffmpegDir 'configure'))) {
    Invoke-Native tar @('-xf', $ffmpegArchive, '-C', $WorkRoot) 'extract-ffmpeg.log'
}

if (-not $SkipBuild) {
    $lsmashMsys = Convert-ToMsysPath $lsmashDir
    $x264Msys = Convert-ToMsysPath $x264Dir
    $ffmpegMsys = Convert-ToMsysPath $ffmpegDir
    $workMsys = Convert-ToMsysPath $WorkRoot
    $jobs = [Math]::Max(1, [Math]::Min(4, [Environment]::ProcessorCount))
    $buildScript = @"
set -euo pipefail
export PATH=/ucrt64/bin:/usr/bin
export SOURCE_DATE_EPOCH='$sourceDateEpoch'
export ZERO_AR_DATE=1
cd '$lsmashMsys'
./configure --prefix='$workMsys/lsmash-prefix' --extra-cflags='-O2 -msse4.2 -ffile-prefix-map=$workMsys=/usr/src/freerenderfps -fdebug-prefix-map=$workMsys=/usr/src/freerenderfps' --extra-ldflags='-static -static-libgcc -Wl,--no-insert-timestamp'
make -j$jobs
make install
export PKG_CONFIG_PATH='$workMsys/lsmash-prefix/lib/pkgconfig'
cd '$x264Msys'
./configure --prefix='$workMsys/x264-prefix' --enable-static --bit-depth=all --chroma-format=all --extra-cflags='-O2 -ffile-prefix-map=$workMsys=/usr/src/freerenderfps -fdebug-prefix-map=$workMsys=/usr/src/freerenderfps' --extra-ldflags='-static -static-libgcc -Wl,--no-insert-timestamp'
make -j$jobs
make install
strip --strip-all '$workMsys/x264-prefix/bin/x264.exe'
cd '$ffmpegMsys'
./configure --prefix='/opt/freerenderfps-runtime' --arch=x86_64 --target-os=mingw32 --disable-autodetect --disable-debug --disable-doc --disable-network --disable-avdevice --disable-swscale --disable-ffplay --disable-ffprobe --disable-sdl2 --disable-everything --enable-ffmpeg --enable-protocol=file,pipe,fd --enable-demuxer=wav --enable-decoder=pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_f64le --enable-encoder=aac --enable-muxer=ipod,adts --enable-filter=aformat,aresample,anull --enable-swresample --enable-static --disable-shared --extra-cflags='-O2 -ffile-prefix-map=$workMsys=/usr/src/freerenderfps -fdebug-prefix-map=$workMsys=/usr/src/freerenderfps' --extra-ldflags='-static -static-libgcc -Wl,--no-insert-timestamp'
make -j$jobs
make DESTDIR='$workMsys/ffmpeg-stage' install
"@
    Invoke-Native $bash @('-lc', $buildScript) 'msys2-build.log'

    $msbuild = Find-MSBuild
    $sln = Join-Path $lsmashDir 'L-SMASH.sln'
    Invoke-Native $msbuild @((Join-Path $lsmashDir 'windows\dllexportgen.csproj'), '/t:Build', '/p:Configuration=CLIRelease', '/p:Platform=x64', '/m') 'msvc-dllexportgen.log'
    $generator = Join-Path $lsmashDir 'windows\bin\x64\CLIRelease\dllexportgen.exe'
    Invoke-Native $generator @($lsmashDir + '\') 'msvc-dllexportgen-run.log'
    $savedCL = $env:CL
    $savedLink = $env:LINK
    $env:CL = ('/Brepro /experimental:deterministic /pathmap:{0}=C:\src\l-smash' -f $lsmashDir)
    $env:LINK = '/Brepro /DEBUG:NONE /PDBALTPATH:%_PDB%'
    try {
    $deterministicMsbuild = @('/p:PlatformToolset=v143', '/p:BuildProjectReferences=false', '/p:LinkIncremental=false', '/p:GenerateDebugInformation=false', '/p:DebugInformationFormat=None', '/p:ProgramDatabaseFile=', '/p:WholeProgramOptimization=false')
    Invoke-Native $msbuild (@((Join-Path $lsmashDir 'L-SMASH.vcxproj'), '/t:Rebuild', '/p:Configuration=CLIRelease', '/p:Platform=x64') + $deterministicMsbuild + @('/m')) 'msvc-lsmash.log'
    # Build the shared CLI support project explicitly. Its CLIRelease pre-build
    # event writes the locked revision macros used by each tool's .rc file.
    Invoke-Native $msbuild (@((Join-Path $lsmashDir 'cli\cli.vcxproj'), '/t:Rebuild', '/p:Configuration=CLIRelease', '/p:Platform=x64') + $deterministicMsbuild + @("/p:SolutionDir=$lsmashDir\", '/m')) 'msvc-cli.log'
    foreach ($project in 'muxer', 'remuxer', 'timelineeditor') {
        Invoke-Native $msbuild (@((Join-Path $lsmashDir "cli\$project.vcxproj"), '/t:Rebuild', '/p:Configuration=CLIRelease', '/p:Platform=x64') + $deterministicMsbuild + @("/p:SolutionDir=$lsmashDir\", '/m')) "msvc-$project.log"
    }
    }
    finally {
        $env:CL = $savedCL
        $env:LINK = $savedLink
    }

    Copy-Item -LiteralPath (Join-Path $WorkRoot 'x264-prefix\bin\x264.exe') -Destination (Join-Path $artifactRoot 'x264.exe') -Force
    Copy-Item -LiteralPath (Join-Path $WorkRoot 'ffmpeg-stage\opt\freerenderfps-runtime\bin\ffmpeg.exe') -Destination (Join-Path $artifactRoot 'ffmpeg.exe') -Force
    foreach ($name in 'muxer.exe', 'remuxer.exe', 'timelineeditor.exe') {
        Copy-Item -LiteralPath (Join-Path $lsmashDir "x64\CLIRelease\$name") -Destination (Join-Path $artifactRoot $name) -Force
    }
}

foreach ($artifact in $lock.artifactShape) {
    $path = Join-Path $artifactRoot $artifact.file
    Assert-File $path 'built runtime artifact'
    if ($artifact.minimumBytes -and (Get-Item -LiteralPath $path).Length -lt [long]$artifact.minimumBytes) {
        throw "Built artifact is unexpectedly small: $path"
    }
}

if ($RequireReleaseHashes -or $ArtifactManifest) {
    if (-not $ArtifactManifest) {
        $ArtifactManifest = Join-Path $repoRoot 'third_party\manifests\THIRD_PARTY_RELEASE_ARTIFACTS.json'
    }
    Assert-File $ArtifactManifest 'release artifact manifest'
    $releaseArtifacts = Get-Content -LiteralPath $ArtifactManifest -Raw | ConvertFrom-Json
    foreach ($artifact in $releaseArtifacts.artifacts) {
        $path = Join-Path $artifactRoot $artifact.file
        Assert-Hash $path $artifact.sha256
        if ((Get-Item -LiteralPath $path).Length -ne [long]$artifact.bytes) {
            throw "Byte-size mismatch for release artifact: $path"
        }
    }
}

$savedPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $x264Text = (& (Join-Path $artifactRoot 'x264.exe') --version 2>&1 | Out-String)
    $ffmpegText = (& (Join-Path $artifactRoot 'ffmpeg.exe') -hide_banner -buildconf 2>&1 | Out-String)
    $lsmashText = (& (Join-Path $artifactRoot 'remuxer.exe') --version 2>&1 | Out-String)
}
finally {
    $ErrorActionPreference = $savedPreference
}
if ($x264Text -notmatch '0480cb0' -or $x264Text -notmatch 'GPL version 2 or later' -or $x264Text -notmatch 'lsmash 2\.16\.1') {
    throw 'x264 semantic version/license/L-SMASH gate failed.'
}
if ($ffmpegText -match '--enable-nonfree' -or $ffmpegText -match '--enable-gpl' -or $ffmpegText -notmatch '--enable-encoder=aac') {
    throw 'FFmpeg license/configuration gate failed.'
}
if ($lsmashText -notmatch 'rev1484' -or $lsmashText -notmatch 'a3e0b47') {
    throw 'L-SMASH version gate failed.'
}

$manifest = foreach ($file in Get-ChildItem -LiteralPath $artifactRoot -File | Sort-Object Name) {
    [PSCustomObject]@{
        file = $file.Name
        bytes = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputRoot 'build-manifest.json') -Encoding UTF8
Write-Step "Validated artifacts: $artifactRoot"
Write-Step 'FFmpeg is LGPL-2.1-or-later configuration; GPL and nonfree are disabled.'
