[CmdletBinding()]
param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64',
    [string]$ReleaseLabel = 'v1.0.1',
    # Path defaults are resolved below: Windows PowerShell 5.1 leaves
    # $PSScriptRoot empty while evaluating param() defaults under -File.
    [string]$OutputRoot = '',
    [string]$ThirdPartyRoot = '',
    [string]$InstallerProject = '',
    [switch]$SkipBuild,
    [switch]$SkipInstallerBuild,
    [switch]$KeepStaging,
    [switch]$RunSelfTest,
    [switch]$AllowDirtyStaging
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $OutputRoot) { $OutputRoot = Join-Path $PSScriptRoot "..\dist\AviUtl2-FreeRenderFPS-$ReleaseLabel" }
# The audited runtime set locked by THIRD_PARTY_MANIFEST.json (rc2a FFmpeg
# with the fd protocol). build\third_party\artifacts holds an older FFmpeg.
if (-not $ThirdPartyRoot) { $ThirdPartyRoot = Join-Path $PSScriptRoot '..\build\third_party\rc2a-fixed\artifacts' }
if (-not $InstallerProject) { $InstallerProject = Join-Path $PSScriptRoot 'installer\FreeRenderFPS.Installer.csproj' }

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourceRoot = Join-Path $repoRoot 'src\x264guiEx-FreeFPS'
$projectFile = Join-Path $sourceRoot 'x264guiEx\x264guiEx.vcxproj'
$releaseOutput = Join-Path $sourceRoot "x264guiEx\$Configuration\$Platform"
$stagingRoot = Join-Path $OutputRoot 'staging'
$payloadRoot = Join-Path $stagingRoot 'payload'
$pluginPayload = Join-Path $payloadRoot 'Plugin\x264guiEx-FreeRenderFPS'
$docsPayload = Join-Path $payloadRoot 'Docs'
$portableRoot = Join-Path $OutputRoot 'portable'
$installerPublish = Join-Path $OutputRoot 'setup'
$artifactStem = "AviUtl2-FreeRenderFPS-$ReleaseLabel"
$setupExeName = "$artifactStem-setup.exe"
$portableZip = Join-Path $OutputRoot "$artifactStem-portable.zip"
$setupZip = Join-Path $OutputRoot "$artifactStem-setup.zip"
$sourcesZip = Join-Path $OutputRoot "$artifactStem-third-party-sources.zip"

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

function Assert-AuditedPayload([string]$Root) {
    $allowedRuntime = @('x264.exe', 'ffmpeg.exe', 'muxer.exe', 'remuxer.exe', 'timelineeditor.exe')
    $forbiddenExtensions = @('.dll', '.sys', '.ocx', '.so', '.dylib', '.lib', '.exp', '.pdb', '.metagen', '.recipe')
    $violations = @()
    Get-ChildItem -LiteralPath $Root -File -Recurse | ForEach-Object {
        $relative = (Get-RelativePathCompat $Root $_.FullName).Replace('\', '/')
        $isAllowedRuntime = $relative -match '^Plugin/x264guiEx-FreeRenderFPS/exe_files/([^/]+)$' -and $allowedRuntime -contains $_.Name
        if ($forbiddenExtensions -contains $_.Extension.ToLowerInvariant() -or ($_.Extension -eq '.exe' -and -not $isAllowedRuntime)) {
            $violations += $_.FullName
        }
    }
    if ($violations.Count -gt 0) {
        throw "Package contains an unaudited binary:`n$($violations -join "`n")"
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
        "# FreeRenderFPS $ReleaseLabel package manifest",
        '',
        'Only the hash-locked x264, FFmpeg and L-SMASH runtime set listed in THIRD_PARTY_MANIFEST.json is embedded.',
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
Assert-Path $ThirdPartyRoot 'verified third-party artifact directory'

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
    # A formal package must not reuse stale resource output after a version
    # header change; force a full rebuild instead of relying on RC dependency
    # tracking through auo_version.rc.
    Invoke-Native $msbuildPath @($projectFile, '/t:Rebuild', "/p:Configuration=$Configuration", "/p:Platform=$Platform", '/m')
}

Assert-Path $releaseOutput 'Release x64 output directory'
$releaseAuo = Join-Path $releaseOutput 'x264guiEx-FreeRenderFPS.auo2'
Assert-Path $releaseAuo 'Release x64 output plugin'
$expectedProductVersion = $ReleaseLabel -replace '^v', ''
$actualProductVersion = (Get-Item -LiteralPath $releaseAuo).VersionInfo.ProductVersion
if ($actualProductVersion -ne $expectedProductVersion) {
    throw "Release plugin version mismatch: expected $expectedProductVersion from ReleaseLabel, found $actualProductVersion in $releaseAuo"
}
$requiredReleaseFiles = @(
    'x264guiEx-FreeRenderFPS.auo2',
    'x264guiEx-FreeRenderFPS.ini',
    'x264guiEx-FreeRenderFPS.en.ini',
    'x264guiEx-FreeRenderFPS.zh.ini'
)
foreach ($name in $requiredReleaseFiles) {
    Copy-Exact (Join-Path $releaseOutput $name) (Join-Path $pluginPayload $name)
}

$thirdPartyManifest = Join-Path $repoRoot 'third_party\manifests\THIRD_PARTY_MANIFEST.json'
$thirdPartyArtifactManifest = Join-Path $repoRoot 'third_party\manifests\THIRD_PARTY_RELEASE_ARTIFACTS.json'
$thirdPartyNotices = Join-Path $repoRoot 'THIRD_PARTY_NOTICES.txt'
Assert-Path $thirdPartyManifest 'third-party manifest'
Assert-Path $thirdPartyArtifactManifest 'third-party release artifact manifest'
Assert-Path $thirdPartyNotices 'plain-text third-party notices'
$manifestData = Get-Content -LiteralPath $thirdPartyManifest -Raw | ConvertFrom-Json
foreach ($entry in $manifestData.components) {
    $name = [IO.Path]::GetFileName($entry.file)
    $source = Join-Path $ThirdPartyRoot $name
    Assert-Path $source "audited runtime $name"
    $actual = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if ($actual -ne $entry.sha256) { throw "Audited runtime hash mismatch for $name" }
    Copy-Exact $source (Join-Path $pluginPayload "exe_files\$name")
}
Copy-Exact $thirdPartyManifest (Join-Path $pluginPayload 'THIRD_PARTY_MANIFEST.json')
Copy-Exact $thirdPartyArtifactManifest (Join-Path $pluginPayload 'THIRD_PARTY_RELEASE_ARTIFACTS.json')
Copy-Exact $thirdPartyNotices (Join-Path $pluginPayload 'THIRD_PARTY_NOTICES.txt')

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
    (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.txt'),
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

Assert-AuditedPayload $payloadRoot
$records = @(Get-FileHashRecord $payloadRoot)
Write-Manifest (Join-Path $stagingRoot 'SHA256SUMS.md') $records
Copy-Exact (Join-Path $stagingRoot 'SHA256SUMS.md') (Join-Path $portableRoot 'SHA256SUMS.md')
# AviUtl2 v2 stores user-installed plugins below data\Plugin.  Keep release
# documentation at the ZIP root, but make the plugin tree directly extractable
# over a clean AviUtl2 directory instead of requiring a manual Plugin ->
# data\Plugin relocation.
Copy-DirectorySafe (Join-Path $payloadRoot 'Plugin') (Join-Path $portableRoot 'data\Plugin')
Copy-DirectorySafe (Join-Path $payloadRoot 'Docs') (Join-Path $portableRoot 'Docs')

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

if (Test-Path -LiteralPath $portableZip) { Remove-Item -LiteralPath $portableZip -Force }
if (Test-Path -LiteralPath $setupZip) { Remove-Item -LiteralPath $setupZip -Force }
if (Test-Path -LiteralPath $sourcesZip) { Remove-Item -LiteralPath $sourcesZip -Force }
Compress-Archive -Path (Join-Path $portableRoot '*') -DestinationPath $portableZip -CompressionLevel Optimal
Compress-Archive -Path (Join-Path $installerPublish '*') -DestinationPath $setupZip -CompressionLevel Optimal

$sourceBundle = Join-Path $stagingRoot 'third-party-sources'
New-Item -ItemType Directory -Force -Path (Join-Path $sourceBundle 'archives'), (Join-Path $sourceBundle 'patches'), (Join-Path $sourceBundle 'licenses') | Out-Null
$sourceArchives = @(
    'x264-0480cb05fa188d37ae87e8f4fd8f1aea3711f7ee.tar.gz',
    'ffmpeg-8.1.2.tar.xz',
    'ffmpeg-8.1.2.tar.xz.asc',
    'l-smash-a3e0b47.tar.gz'
)
foreach ($archive in $sourceArchives) {
    Copy-Exact (Join-Path $repoRoot "build\source-cache\$archive") (Join-Path $sourceBundle "archives\$archive")
}
Copy-Exact (Join-Path $repoRoot 'third_party\sources.lock.json') (Join-Path $sourceBundle 'sources.lock.json')
Copy-Exact (Join-Path $repoRoot 'tools\build_third_party.ps1') (Join-Path $sourceBundle 'build_third_party.ps1')
Copy-Exact (Join-Path $repoRoot 'third_party\patches\l-smash-gcc16.patch') (Join-Path $sourceBundle 'patches\l-smash-gcc16.patch')
Copy-Exact (Join-Path $repoRoot 'third_party\patches\l-smash-vs2022.patch') (Join-Path $sourceBundle 'patches\l-smash-vs2022.patch')
Copy-Exact (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.txt') (Join-Path $sourceBundle 'THIRD_PARTY_NOTICES.txt')
Copy-Exact (Join-Path $repoRoot 'docs\THIRD_PARTY_RUNTIME_AUDIT.md') (Join-Path $sourceBundle 'BUILD_AND_SOURCE_CORRESPONDENCE.md')
Copy-Exact 'D:\frfps-third-party-work\x264\COPYING' (Join-Path $sourceBundle 'licenses\GPL-2.0-x264.txt')
Copy-Exact 'D:\frfps-third-party-work\ffmpeg-8.1.2\COPYING.LGPLv2.1' (Join-Path $sourceBundle 'licenses\LGPL-2.1-FFmpeg.txt')
Copy-Exact 'D:\frfps-third-party-work\l-smash\LICENSE' (Join-Path $sourceBundle 'licenses\ISC-L-SMASH.txt')
Compress-Archive -Path (Join-Path $sourceBundle '*') -DestinationPath $sourcesZip -CompressionLevel Optimal

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
Write-Step 'Embedded runtime set was sourced only from the verified third-party artifact directory.'
