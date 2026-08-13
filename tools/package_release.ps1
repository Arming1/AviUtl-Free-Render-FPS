[CmdletBinding()]
param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64',
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\dist\AviUtl2-FreeRenderFPS-v1.0.0-rc1'),
    [string]$InstallerProject = (Join-Path $PSScriptRoot 'installer\FreeRenderFPS.Installer.csproj'),
    [switch]$SkipBuild,
    [switch]$SkipInstallerBuild,
    [switch]$KeepStaging,
    [switch]$RunSelfTest,
    [switch]$AllowDirtyStaging
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourceRoot = Join-Path $repoRoot 'src\x264guiEx-FreeFPS'
$projectFile = Join-Path $sourceRoot 'x264guiEx\x264guiEx.vcxproj'
$releaseOutput = Join-Path $sourceRoot "$Configuration\$Platform"
$stagingRoot = Join-Path $OutputRoot 'staging'
$payloadRoot = Join-Path $stagingRoot 'payload'
$pluginPayload = Join-Path $payloadRoot 'Plugin\x264guiEx-FreeRenderFPS'
$docsPayload = Join-Path $payloadRoot 'Docs'
$portableRoot = Join-Path $OutputRoot 'portable'
$installerPublish = Join-Path $OutputRoot 'setup'
$setupExeName = 'AviUtl2-FreeRenderFPS-v1.0.0-rc1-setup.exe'
$portableZip = Join-Path $OutputRoot 'AviUtl2-FreeRenderFPS-v1.0.0-rc1-portable.zip'
$setupZip = Join-Path $OutputRoot 'AviUtl2-FreeRenderFPS-v1.0.0-rc1-setup.zip'

function Write-Step([string]$Message) {
    Write-Host "[FreeRenderFPS package] $Message" -ForegroundColor Cyan
}

function Assert-Path([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Required $Description was not found: $Path"
    }
}

function Invoke-Native([string]$FilePath, [string[]]$ArgumentList) {
    Write-Step ("Running {0} {1}" -f $FilePath, ($ArgumentList -join ' '))
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw ("Command failed with exit code {0}: {1}" -f $LASTEXITCODE, $FilePath)
    }
}

function Copy-Exact([string]$Source, [string]$Destination) {
    Assert-Path $Source 'payload source file'
    $parent = Split-Path -Parent $Destination
    if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Copy-DirectorySafe([string]$Source, [string]$Destination) {
    Assert-Path $Source 'payload source directory'
    Get-ChildItem -LiteralPath $Source -Force -Recurse | ForEach-Object {
        if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing to package a reparse point: $($_.FullName)"
        }
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | Copy-Item -Destination $Destination -Recurse -Force
}

function Assert-NoThirdPartyBinaries([string]$Root) {
    $forbiddenExtensions = @('.exe', '.dll', '.sys', '.ocx', '.so', '.dylib', '.lib', '.exp', '.pdb', '.metagen', '.recipe')
    $forbiddenNames = @('x264.exe', 'ffmpeg.exe', 'ffmpeg_audenc.exe', 'x264_3223_x64.exe', 'mkvmerge.exe', 'mplex.exe', 'mp4box.exe', 'muxer_x64.exe', 'remuxer_x64.exe', 'timelineeditor_x64.exe', 'LSMASHSource.dll', 'LSMASHSource_indexing.exe', 'check_vc.dll', 'check_dotnet.dll')
    $violations = @()
    Get-ChildItem -LiteralPath $Root -File -Recurse | ForEach-Object {
        if ($forbiddenExtensions -contains $_.Extension.ToLowerInvariant() -or $forbiddenNames -contains $_.Name) {
            $violations += $_.FullName
        }
    }
    if ($violations.Count -gt 0) {
        throw "License-safe package contains prohibited runtime binaries:`n$($violations -join "`n")"
    }
}

function Get-RelativePathCompat([string]$Root, [string]$Path) {
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    $rootUri = [Uri]$rootFull
    $pathUri = [Uri][IO.Path]::GetFullPath($Path)
    return [Uri]::UnescapeDataString($rootUri.MakeRelativeUri($pathUri).ToString()).Replace('/', [IO.Path]::DirectorySeparatorChar)
}

function Get-FileHashRecord([string]$Root) {
    Get-ChildItem -LiteralPath $Root -File -Recurse | Sort-Object FullName | ForEach-Object {
        $relative = (Get-RelativePathCompat $Root $_.FullName).Replace('\', '/')
        $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
        [PSCustomObject]@{ Path = $relative; SHA256 = $hash.Hash; Bytes = $_.Length }
    }
}

function Write-Manifest([string]$Path, [object[]]$Records) {
    $lines = @(
        '# FreeRenderFPS v1.0.0-rc1 package manifest',
        '',
        'This manifest intentionally excludes encoder, muxer, audio-tool, helper-DLL, and system-runtime binaries whose license/provenance is unresolved. Acquire those separately under the documented dependency policy.',
        '',
        '| Relative path | SHA-256 | Bytes |',
        '|---|---|---:|'
    )
    foreach ($record in $Records) {
        $lines += "| ``$($record.Path)`` | ``$($record.SHA256)`` | $($record.Bytes) |"
    }
    Set-Content -LiteralPath $Path -Value $lines -Encoding UTF8
}

function Remove-IfExists([string]$Path) {
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Recurse -Force }
}

Assert-Path $sourceRoot 'source tree'
Assert-Path $projectFile 'x264guiEx project'
Assert-Path $InstallerProject 'installer project'

if (-not $AllowDirtyStaging) { Remove-IfExists $OutputRoot }
New-Item -ItemType Directory -Force -Path $stagingRoot, $pluginPayload, $docsPayload, $portableRoot, $installerPublish | Out-Null

if (-not $SkipBuild) {
    $msbuild = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($null -eq $msbuild) {
        $vsCandidates = @(
            (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\*\*\MSBuild\Current\Bin\MSBuild.exe'),
            (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\*\MSBuild\Current\Bin\MSBuild.exe'),
            (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2019\*\MSBuild\Current\Bin\MSBuild.exe')
        ) | ForEach-Object { Get-ChildItem -Path $_ -File -ErrorAction SilentlyContinue } | Sort-Object FullName -Descending
        $msbuild = $vsCandidates | Select-Object -First 1
    }
    if ($null -eq $msbuild) {
        throw 'MSBuild with the Visual Studio C++ workload was not found. Install it, or run with -SkipBuild after producing a Release x64 output.'
    }
    $msbuildPath = if ($msbuild.PSObject.Properties.Name -contains 'Source') { $msbuild.Source } else { $msbuild.FullName }
    Invoke-Native $msbuildPath @($projectFile, '/t:Build', "/p:Configuration=$Configuration", "/p:Platform=$Platform", '/m')
}

Assert-Path $releaseOutput 'Release x64 output directory'
$requiredReleaseFiles = @(
    'x264guiEx-FreeRenderFPS.auo2',
    'x264guiEx-FreeRenderFPS.ini',
    'x264guiEx-FreeRenderFPS.en.ini',
    'x264guiEx-FreeRenderFPS.zh.ini'
)
foreach ($name in $requiredReleaseFiles) {
    Copy-Exact (Join-Path $releaseOutput $name) (Join-Path $pluginPayload $name)
}

$presetSource = Join-Path $sourceRoot 'x264guiEx\x264guiEx_stg'
if (Test-Path -LiteralPath $presetSource) {
    Get-ChildItem -LiteralPath $presetSource -File -Recurse | ForEach-Object {
        if ($_.Extension -ne '.stg') { throw "Unexpected non-.stg profile in source: $($_.FullName)" }
    }
    Copy-DirectorySafe $presetSource (Join-Path $pluginPayload 'x264guiEx_stg')
}

$repoRootDocs = @(
    (Join-Path $repoRoot 'LICENSE'),
    (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md'),
    (Join-Path $repoRoot 'README.en.md'),
    (Join-Path $repoRoot 'README.zh-CN.md'),
    (Join-Path $repoRoot 'README.ja.md')
)
$repoNestedDocs = @(
    (Join-Path $repoRoot 'docs\RUNTIME_DEPENDENCIES.md'),
    (Join-Path $repoRoot 'docs\KNOWN_LIMITATIONS.md'),
    (Join-Path $repoRoot 'docs\VALIDATION_MATRIX.md')
)
foreach ($doc in $repoRootDocs) {
    Copy-Exact $doc (Join-Path $docsPayload ([IO.Path]::GetFileName($doc)))
}
foreach ($doc in $repoNestedDocs) {
    Copy-Exact $doc (Join-Path (Join-Path $docsPayload 'docs') ([IO.Path]::GetFileName($doc)))
}

Assert-NoThirdPartyBinaries $payloadRoot
$records = @(Get-FileHashRecord $payloadRoot)
Write-Manifest (Join-Path $stagingRoot 'SHA256SUMS.md') $records
Copy-Exact (Join-Path $stagingRoot 'SHA256SUMS.md') (Join-Path $portableRoot 'SHA256SUMS.md')
Copy-DirectorySafe $payloadRoot $portableRoot

if (-not $SkipInstallerBuild) {
    Remove-IfExists $installerPublish
    New-Item -ItemType Directory -Force -Path $installerPublish | Out-Null
    $setupExe = Join-Path $installerPublish $setupExeName
    $cscCandidates = @(Get-ChildItem -Path (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\*\*\MSBuild\Current\Bin\Roslyn\csc.exe') -File -ErrorAction SilentlyContinue | Sort-Object FullName -Descending)
    $csc = $cscCandidates | Select-Object -First 1
    if ($null -eq $csc) {
        throw 'Visual Studio Roslyn csc.exe was not found. Install Visual Studio Build Tools, or run with -SkipInstallerBuild.'
    }
    Invoke-Native $csc.FullName @('/nologo', '/nullable:enable', '/langversion:latest', '/target:exe', "/win32manifest:$PSScriptRoot\installer\app.manifest", "/out:$setupExe", (Join-Path $PSScriptRoot 'installer\Program.cs'))
    Copy-DirectorySafe $payloadRoot (Join-Path $installerPublish 'payload')
    Copy-Exact (Join-Path $stagingRoot 'SHA256SUMS.md') (Join-Path $installerPublish 'SHA256SUMS.md')
    Copy-Exact $setupExe (Join-Path $OutputRoot $setupExeName)
    Copy-DirectorySafe $payloadRoot (Join-Path $OutputRoot 'payload')
}

if ($RunSelfTest) {
    $cscCandidates = @(Get-ChildItem -Path (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\*\*\MSBuild\Current\Bin\Roslyn\csc.exe') -File -ErrorAction SilentlyContinue | Sort-Object FullName -Descending)
    $csc = $cscCandidates | Select-Object -First 1
    if ($null -eq $csc) { throw 'Visual Studio Roslyn csc.exe was not found for installer self-test.' }
    $selfTestExe = Join-Path $stagingRoot 'FreeRenderFPS.Installer.selftest.exe'
    Invoke-Native $csc.FullName @('/nologo', '/nullable:enable', '/langversion:latest', '/target:exe', "/out:$selfTestExe", (Join-Path $PSScriptRoot 'installer\Program.cs'))
    Invoke-Native $selfTestExe @('--self-test')
}

$missing = @(
    'x264.exe',
    'ffmpeg.exe',
    'remuxer.exe/muxer.exe',
    'mkvmerge.exe/mplex.exe/other audio encoders',
    'Microsoft VC++ runtime and .NET Framework'
)
Set-Content -LiteralPath (Join-Path $OutputRoot 'MISSING_THIRD_PARTY_TOOLS.txt') -Value @(
    'These dependencies are intentionally absent from the license-safe RC package.',
    'Acquire each tool separately, record its exact version/hash/source/license, and install it under the user-selected AviUtl2 plugin runtime policy.',
    '',
    ($missing | ForEach-Object { '- ' + $_ })
) -Encoding UTF8

Copy-Exact (Join-Path $OutputRoot 'MISSING_THIRD_PARTY_TOOLS.txt') (Join-Path $portableRoot 'MISSING_THIRD_PARTY_TOOLS.txt')
Copy-Exact (Join-Path $OutputRoot 'MISSING_THIRD_PARTY_TOOLS.txt') (Join-Path $installerPublish 'MISSING_THIRD_PARTY_TOOLS.txt')

if (Test-Path -LiteralPath $portableZip) { Remove-Item -LiteralPath $portableZip -Force }
if (Test-Path -LiteralPath $setupZip) { Remove-Item -LiteralPath $setupZip -Force }
Compress-Archive -Path (Join-Path $portableRoot '*') -DestinationPath $portableZip -CompressionLevel Optimal
Compress-Archive -Path (Join-Path $installerPublish '*') -DestinationPath $setupZip -CompressionLevel Optimal

# The ZIPs are the distributable views. Keep one adjacent payload beside the
# top-level setup EXE, and remove duplicate expanded setup/portable trees.
Remove-IfExists $portableRoot
Remove-IfExists $installerPublish

if (-not $KeepStaging) {
    Remove-IfExists $stagingRoot
}
$allArtifacts = @(Get-FileHashRecord $OutputRoot)
Write-Manifest (Join-Path $OutputRoot 'SHA256SUMS.md') $allArtifacts
$checksumLines = Get-ChildItem -LiteralPath $OutputRoot -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object Name | ForEach-Object {
    $hash = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
    "$($hash.Hash)  $($_.Name)"
}
Set-Content -LiteralPath (Join-Path $OutputRoot 'SHA256SUMS.txt') -Value $checksumLines -Encoding ascii
Write-Step "Package complete: $OutputRoot"
Write-Step 'Third-party encoder/muxer/runtime binaries were not embedded.'
