[CmdletBinding()]
param(
    [string]$Configuration = 'Release',
    [string]$ReleaseLabel = 'v1.1.0',
    [string]$OutputRoot = '',
    [string]$ThirdPartyRoot = '',
    [string]$ThirdPartySourcesZip = '',
    [switch]$SkipBuild,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourceRoot = Join-Path $repoRoot 'src\x264guiEx-FreeFPS'
$projectFile = Join-Path $sourceRoot 'x264guiEx\x264guiEx.vcxproj'
$releaseOutput = Join-Path $sourceRoot "x264guiEx\$Configuration\Win32"
$packageDocs = Join-Path $PSScriptRoot 'aviutl1_package'
$artifactStem = "AviUtl1-FreeRenderFPS-$ReleaseLabel"
$expectedProductVersion = $ReleaseLabel -replace '^v', ''
$pluginName = 'x264guiEx-FreeRenderFPS'

# $PSScriptRoot is empty in param() defaults on Windows PowerShell 5.1 when the
# script runs via -File, so every path default is resolved here instead.
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $PSScriptRoot "..\dist\$artifactStem"
}
if (-not $ThirdPartyRoot) {
    $ThirdPartyRoot = Join-Path $PSScriptRoot '..\build\third_party\rc2a-fixed\artifacts'
}
if (-not $ThirdPartySourcesZip) {
    $ThirdPartySourcesZip = Join-Path $PSScriptRoot "..\dist\AviUtl2-FreeRenderFPS-$ReleaseLabel\AviUtl2-FreeRenderFPS-$ReleaseLabel-third-party-sources.zip"
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
$ThirdPartySourcesZip = [IO.Path]::GetFullPath($ThirdPartySourcesZip)
$ThirdPartyRoot = [IO.Path]::GetFullPath($ThirdPartyRoot)

function Write-Step([string]$Message) {
    Write-Host "[FreeRenderFPS AviUtl1 package] $Message" -ForegroundColor Cyan
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

function Assert-OutputRootAvailable([string]$Root) {
    if (-not (Test-Path -LiteralPath $Root)) { return }
    if (-not $Force) {
        throw "Output root already exists: $Root. Re-run with -Force to replace the $artifactStem release, or choose another -OutputRoot."
    }
    if ((Split-Path -Leaf $Root) -cne $artifactStem) {
        throw "Refusing to replace '$Root' with -Force: only a directory whose leaf name is exactly '$artifactStem' may be deleted."
    }
    $item = Get-Item -LiteralPath $Root -Force
    if (-not $item.PSIsContainer) {
        throw "Refusing to replace '$Root': it is not a directory."
    }
    # Remove-Item -Recurse can follow junctions into unrelated trees on
    # Windows PowerShell 5.1, so refuse any reparse point in the tree.
    $reparsePoints = @(Get-ChildItem -LiteralPath $Root -Force -Recurse | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint })
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $reparsePoints.Count -gt 0) {
        throw "Refusing to replace '$Root': it or its contents contain a reparse point."
    }
    Write-Step "Replacing existing release directory: $Root"
    Remove-Item -LiteralPath $Root -Recurse -Force
}

function Assert-ReleaseAuo([string]$Path, [string]$ExpectedVersion) {
    $actualVersion = (Get-Item -LiteralPath $Path).VersionInfo.ProductVersion
    if ($actualVersion -ne $ExpectedVersion) {
        throw "Release gate failed (version): expected ProductVersion $ExpectedVersion from ReleaseLabel $ReleaseLabel, found '$actualVersion' in $Path"
    }

    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40) {
        throw "Release gate failed (PE): file is too small to be a PE image: $Path"
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0 -or ($peOffset + 6) -gt $bytes.Length -or
        $bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
        $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
        throw "Release gate failed (PE): missing PE\0\0 signature in $Path"
    }
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    if ($machine -ne 0x14c) {
        throw ("Release gate failed (PE): machine is 0x{0:X4}, expected x86 0x014C in {1}" -f $machine, $Path)
    }

    # Latin-1 maps every byte to one char, so byte patterns can be searched as text.
    $image = [Text.Encoding]::GetEncoding(28591).GetString($bytes)
    $forbidden = @(
        [PSCustomObject]@{ Needle = 'probe installed'; Comparison = [StringComparison]::Ordinal; Reason = 'ASCII "probe installed" (diagnostic trace compiled in)' },
        [PSCustomObject]@{ Needle = 'request generation='; Comparison = [StringComparison]::Ordinal; Reason = 'ASCII "request generation=" (diagnostic trace compiled in)' },
        [PSCustomObject]@{ Needle = [Text.Encoding]::GetEncoding(28591).GetString([Text.Encoding]::Unicode.GetBytes('freerenderfps_aviutl1_probe.log')); Comparison = [StringComparison]::Ordinal; Reason = 'UTF-16LE "freerenderfps_aviutl1_probe.log" (diagnostic trace compiled in)' },
        [PSCustomObject]@{ Needle = 'ucrtbased.dll'; Comparison = [StringComparison]::OrdinalIgnoreCase; Reason = 'Debug CRT import ucrtbased.dll' },
        [PSCustomObject]@{ Needle = 'VCRUNTIME140D.dll'; Comparison = [StringComparison]::OrdinalIgnoreCase; Reason = 'Debug CRT import VCRUNTIME140D.dll' },
        [PSCustomObject]@{ Needle = 'MSVCP140D.dll'; Comparison = [StringComparison]::OrdinalIgnoreCase; Reason = 'Debug CRT import MSVCP140D.dll' }
    )
    foreach ($entry in $forbidden) {
        if ($image.IndexOf($entry.Needle, $entry.Comparison) -ge 0) {
            throw "Release gate failed: $($entry.Reason) found in $Path"
        }
    }
}

function Assert-AuditedPayload([string]$Root) {
    $allowedRuntime = @('x264.exe', 'ffmpeg.exe', 'muxer.exe', 'remuxer.exe', 'timelineeditor.exe')
    $forbiddenExtensions = @('.conf', '.dll', '.sys', '.ocx', '.lib', '.exp', '.pdb', '.ilk', '.obj', '.metagen', '.recipe')
    $violations = @()
    Get-ChildItem -LiteralPath $Root -File -Recurse | ForEach-Object {
        $relative = (Get-RelativePathCompat $Root $_.FullName).Replace('\', '/')
        $extension = $_.Extension.ToLowerInvariant()
        $isAllowedRuntime = ($relative -eq "Plugins/exe_files/$($_.Name)") -and ($allowedRuntime -contains $_.Name)
        $isAllowedPlugin = $relative -eq "Plugins/$pluginName.auo"
        if ($forbiddenExtensions -contains $extension -or
            ($extension -eq '.exe' -and -not $isAllowedRuntime) -or
            ($extension -eq '.auo' -and -not $isAllowedPlugin)) {
            $violations += $_.FullName
        }
    }
    if ($violations.Count -gt 0) {
        throw "Package contains an unaudited file:`n$($violations -join "`n")"
    }
}

function Assert-PortableZip([string]$ZipPath, [int]$ExpectedFileCount) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $names = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
    } finally {
        $zip.Dispose()
    }
    $fileEntries = @($names | Where-Object { -not $_.EndsWith('/') })
    if ($fileEntries.Count -ne $ExpectedFileCount) {
        throw "Portable ZIP entry count mismatch: expected $ExpectedFileCount payload files, found $($fileEntries.Count) in $ZipPath"
    }
    $confEntries = @($fileEntries | Where-Object { $_.EndsWith('.conf', [StringComparison]::OrdinalIgnoreCase) })
    if ($confEntries.Count -gt 0) {
        throw "Portable ZIP contains a .conf entry: $($confEntries -join ', ')"
    }
    if ($fileEntries -cnotcontains "Plugins/$pluginName.auo") {
        throw "Portable ZIP is missing Plugins/$pluginName.auo: $ZipPath"
    }
}

function Write-ChecksumManifests([string]$Root) {
    $records = @(Get-FileHashRecord $Root | Where-Object { $_.Path -cne 'SHA256SUMS.md' -and $_.Path -cne 'SHA256SUMS.txt' })
    # Ordinal sort keeps the manifest order independent of the host culture.
    [Array]::Sort([object[]]$records, [Comparison[object]]{ param($a, $b) [string]::CompareOrdinal($a.Path, $b.Path) })

    $markdown = New-Object System.Collections.Generic.List[string]
    $markdown.Add("# AviUtl1 FreeRenderFPS $ReleaseLabel SHA-256 manifest")
    $markdown.Add('')
    $markdown.Add('| Relative path | SHA-256 | Bytes |')
    $markdown.Add('|---|---|---:|')
    $text = New-Object System.Collections.Generic.List[string]
    foreach ($record in $records) {
        $markdown.Add("| ``$($record.Path)`` | ``$($record.SHA256)`` | $($record.Bytes) |")
        $text.Add("$($record.SHA256)  $($record.Path)")
    }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllLines((Join-Path $Root 'SHA256SUMS.md'), [string[]]$markdown.ToArray(), $utf8NoBom)
    [IO.File]::WriteAllLines((Join-Path $Root 'SHA256SUMS.txt'), [string[]]$text.ToArray(), $utf8NoBom)
}

Assert-Path $sourceRoot 'source tree'
Assert-Path $projectFile 'x264guiEx project'
Assert-Path $ThirdPartyRoot 'verified third-party artifact directory'
Assert-Path $ThirdPartySourcesZip 'third-party sources ZIP'
Assert-Path $packageDocs 'AviUtl1 package documentation directory'

Assert-OutputRootAvailable $OutputRoot

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
        throw 'MSBuild with the Visual Studio C++ workload was not found. Install it, or run with -SkipBuild after producing a Release Win32 output.'
    }
    $msbuildPath = if ($msbuild.PSObject.Properties.Name -contains 'Source') { $msbuild.Source } else { $msbuild.FullName }
    # Force a full rebuild so a version header change cannot leave stale RC output.
    Invoke-Native $msbuildPath @($projectFile, '/t:Rebuild', "/p:Configuration=$Configuration", '/p:Platform=Win32', '/m', '/nodeReuse:false')
}

Assert-Path $releaseOutput 'Win32 Release output directory'
$releaseAuo = Join-Path $releaseOutput "$pluginName.auo"
Assert-Path $releaseAuo 'Win32 Release plugin'
Assert-ReleaseAuo $releaseAuo $expectedProductVersion
foreach ($suffix in @('.ini', '.en.ini', '.zh.ini')) {
    Assert-Path (Join-Path $releaseOutput "$pluginName$suffix") "Win32 Release settings file $pluginName$suffix"
}

$payloadRoot = Join-Path $OutputRoot 'payload'
$pluginRoot = Join-Path $payloadRoot 'Plugins'
$exeRoot = Join-Path $pluginRoot 'exe_files'
$docsRoot = Join-Path $payloadRoot 'Docs'
New-Item -ItemType Directory -Force -Path $OutputRoot, $payloadRoot, $pluginRoot, $exeRoot, $docsRoot | Out-Null

foreach ($name in @("$pluginName.auo", "$pluginName.ini", "$pluginName.en.ini", "$pluginName.zh.ini")) {
    Copy-Exact (Join-Path $releaseOutput $name) (Join-Path $pluginRoot $name)
}

$presetSource = Join-Path $sourceRoot 'x264guiEx\x264guiEx_stg'
Assert-Path $presetSource 'preset directory'
Get-ChildItem -LiteralPath $presetSource -File -Recurse | ForEach-Object {
    if ($_.Extension -ne '.stg') { throw "Unexpected non-.stg profile in source: $($_.FullName)" }
}
Copy-DirectorySafe $presetSource (Join-Path $pluginRoot "${pluginName}_stg")

$thirdPartyManifest = Join-Path $repoRoot 'third_party\manifests\THIRD_PARTY_MANIFEST.json'
$thirdPartyArtifactManifest = Join-Path $repoRoot 'third_party\manifests\THIRD_PARTY_RELEASE_ARTIFACTS.json'
$thirdPartyNotices = Join-Path $repoRoot 'THIRD_PARTY_NOTICES.txt'
Assert-Path $thirdPartyManifest 'third-party manifest'
Assert-Path $thirdPartyArtifactManifest 'third-party release artifact manifest'
Assert-Path $thirdPartyNotices 'plain-text third-party notices'
$manifestData = Get-Content -LiteralPath $thirdPartyManifest -Raw | ConvertFrom-Json
foreach ($name in @('x264.exe', 'ffmpeg.exe', 'muxer.exe', 'remuxer.exe', 'timelineeditor.exe')) {
    $entries = @($manifestData.components | Where-Object { [IO.Path]::GetFileName($_.file) -eq $name })
    if ($entries.Count -ne 1) { throw "THIRD_PARTY_MANIFEST.json must list exactly one component for $name" }
    $source = Join-Path $ThirdPartyRoot $name
    Assert-Path $source "audited runtime $name"
    $expectedHash = $entries[0].sha256
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $expectedHash) {
        throw "Audited runtime hash mismatch for $name"
    }
    $staged = Join-Path $exeRoot $name
    Copy-Exact $source $staged
    if ((Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash -ne $expectedHash) {
        throw "Staged runtime hash mismatch for $name"
    }
}
Copy-Exact $thirdPartyManifest (Join-Path $exeRoot 'THIRD_PARTY_MANIFEST.json')
Copy-Exact $thirdPartyArtifactManifest (Join-Path $exeRoot 'THIRD_PARTY_RELEASE_ARTIFACTS.json')
Copy-Exact $thirdPartyNotices (Join-Path $exeRoot 'THIRD_PARTY_NOTICES.txt')

# Known limitations and validation are the shared repo docs. Note that
# docs/KNOWN_LIMITATIONS.md currently describes the AviUtl2 build.
Copy-Exact (Join-Path $repoRoot 'docs\KNOWN_LIMITATIONS.md') (Join-Path $docsRoot 'KNOWN_LIMITATIONS.md')
Copy-Exact (Join-Path $repoRoot 'docs\LEGACY_AVIUTL1_VALIDATION.md') (Join-Path $docsRoot 'LEGACY_AVIUTL1_VALIDATION.md')
Copy-Exact (Join-Path $repoRoot 'LICENSE') (Join-Path $payloadRoot 'LICENSE')
Copy-Exact (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md') (Join-Path $payloadRoot 'THIRD_PARTY_NOTICES.md')
Copy-Exact (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.txt') (Join-Path $payloadRoot 'THIRD_PARTY_NOTICES.txt')

$packageNotes = Join-Path $packageDocs 'LEGACY_AVIUTL1_SETUP.md'
$packageReadmes = @('README.en.md', 'README.ja.md', 'README.zh-CN.md')
Copy-Exact $packageNotes (Join-Path $payloadRoot 'LEGACY_AVIUTL1_SETUP.md')
Copy-Exact $packageNotes (Join-Path $docsRoot 'LEGACY_AVIUTL1_SETUP.md')
foreach ($name in $packageReadmes) {
    Copy-Exact (Join-Path $packageDocs $name) (Join-Path $payloadRoot $name)
}

Assert-AuditedPayload $payloadRoot
$payloadFileCount = @(Get-ChildItem -LiteralPath $payloadRoot -File -Recurse).Count

Copy-Exact $packageNotes (Join-Path $OutputRoot 'LEGACY_AVIUTL1_SETUP.md')
foreach ($name in $packageReadmes) {
    Copy-Exact (Join-Path $packageDocs $name) (Join-Path $OutputRoot $name)
}

$sourcesZip = Join-Path $OutputRoot "$artifactStem-third-party-sources.zip"
Copy-Exact $ThirdPartySourcesZip $sourcesZip
if ((Get-FileHash -LiteralPath $sourcesZip -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $ThirdPartySourcesZip -Algorithm SHA256).Hash) {
    throw "Third-party sources ZIP copy does not match its source: $ThirdPartySourcesZip"
}

$portableZip = Join-Path $OutputRoot "$artifactStem-portable.zip"
Compress-Archive -Path (Join-Path $payloadRoot '*') -DestinationPath $portableZip -CompressionLevel Optimal
Assert-PortableZip $portableZip $payloadFileCount

Write-ChecksumManifests $OutputRoot

$stagedAuo = Join-Path $pluginRoot "$pluginName.auo"
Write-Step "Package complete: $OutputRoot"
Write-Step ("AUO SHA-256: {0}" -f (Get-FileHash -LiteralPath $stagedAuo -Algorithm SHA256).Hash)
Write-Step ("AUO ProductVersion: {0}" -f (Get-Item -LiteralPath $stagedAuo).VersionInfo.ProductVersion)
Write-Step "Payload file count: $payloadFileCount"
