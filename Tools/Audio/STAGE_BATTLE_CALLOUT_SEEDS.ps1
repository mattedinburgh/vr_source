param(
    [Parameter(Mandatory=$true)] [string]$SourceDataRoot,
    [Parameter(Mandatory=$true)] [string]$OutputDataRoot,
    [string]$ManifestPath = ''
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ManifestPath)) { $ManifestPath = Join-Path $PSScriptRoot 'BATTLE_CALLOUT_RECORDING_MANIFEST.csv' }
$sourceRoot = (Resolve-Path -LiteralPath $SourceDataRoot).Path
if (-not (Test-Path -LiteralPath $ManifestPath)) { throw "Manifest not found: $ManifestPath" }
if (-not (Test-Path -LiteralPath $OutputDataRoot)) {
    New-Item -ItemType Directory -Path $OutputDataRoot -Force | Out-Null
}
$outputRoot = (Resolve-Path -LiteralPath $OutputDataRoot).Path

$rows = @(Import-Csv -LiteralPath $ManifestPath | Where-Object recording_status -eq 'reuse_seed')
if ($rows.Count -eq 0) { throw 'Manifest contains no reuse_seed rows.' }

$copied = 0
$missing = New-Object System.Collections.Generic.List[string]
foreach ($row in $rows) {
    if (-not $row.seed_audio_path) { throw "reuse_seed row has no seed_audio_path: $($row.audio_path)" }
    $seedRelative = $row.seed_audio_path -replace '[\\/]', [IO.Path]::DirectorySeparatorChar
    $targetRelative = $row.audio_path -replace '[\\/]', [IO.Path]::DirectorySeparatorChar
    $source = Join-Path $sourceRoot $seedRelative
    $target = Join-Path $outputRoot $targetRelative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        $missing.Add($row.seed_audio_path)
        continue
    }

    $targetDir = Split-Path -Parent $target
    if (-not (Test-Path -LiteralPath $targetDir)) {
        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
    }
    Copy-Item -LiteralPath $source -Destination $target -Force
    $copied++
}

if ($missing.Count -gt 0) {
    $sample = ($missing | Select-Object -First 10) -join ', '
    throw "Missing $($missing.Count) declared seed asset(s). First missing: $sample"
}
if ($copied -ne $rows.Count) { throw "Expected to stage $($rows.Count) seed rows, staged $copied." }

Write-Host "Staged $copied provenance-approved English seed recordings."
Write-Host "Source: Kenney Voiceover Pack / CC0 1.0"
Write-Host "Output Data-Vengeance root: $outputRoot"
