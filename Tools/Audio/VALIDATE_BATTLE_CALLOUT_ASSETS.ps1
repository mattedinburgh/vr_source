param(
    [string]$ManifestPath = '',
    [string[]]$DataRoots = @(),
    [ValidateSet('None','P0','P1','P2','All')]
    [string]$Require = 'None',
    [string]$CoverageOutput = ''
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($ManifestPath)) { $ManifestPath = Join-Path $PSScriptRoot 'BATTLE_CALLOUT_RECORDING_MANIFEST.csv' }
if (-not (Test-Path -LiteralPath $ManifestPath)) {
    throw "Recording manifest not found: $ManifestPath"
}

if ($DataRoots.Count -eq 0) {
    $candidate = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path 'Data-Vengeance'
    if (Test-Path -LiteralPath $candidate) { $DataRoots = @($candidate) }
}
if ($DataRoots.Count -eq 0) { throw 'No Data-Vengeance root supplied or auto-detected.' }

$DataRoots = @($DataRoots | ForEach-Object { (Resolve-Path -LiteralPath $_).Path } | Select-Object -Unique)
$rows = @(Import-Csv -LiteralPath $ManifestPath)
if ($rows.Count -eq 0) { throw 'Recording manifest is empty.' }

$requiredColumns = @('priority','event','emotion','variant','sex','kind','subtitle','audio_path')
foreach ($column in $requiredColumns) {
    if (-not ($rows[0].PSObject.Properties.Name -contains $column)) { throw "Manifest missing column: $column" }
}
$coverage = foreach ($row in $rows) {
    $hits = New-Object System.Collections.Generic.List[string]
    foreach ($root in $DataRoots) {
        $relative = $row.audio_path -replace '[\\/]', [IO.Path]::DirectorySeparatorChar
        $full = Join-Path $root $relative
        if (Test-Path -LiteralPath $full -PathType Leaf) {
            $item = Get-Item -LiteralPath $full
            if ($item.Length -lt 4) { throw "Audio asset is empty/truncated: $full" }
            $stream = [IO.File]::OpenRead($full)
            try {
                $magic = New-Object byte[] 4
                [void]$stream.Read($magic, 0, 4)
            } finally { $stream.Dispose() }
            if ([Text.Encoding]::ASCII.GetString($magic) -ne 'OggS') { throw "Audio asset is not an OGG stream: $full" }
            $hits.Add($full)
        }
    }
    [pscustomobject]@{
        priority = $row.priority
        event = $row.event
        emotion = $row.emotion
        variant = [int]$row.variant
        sex = $row.sex
        kind = $row.kind
        subtitle = $row.subtitle
        audio_path = $row.audio_path
        present = ($hits.Count -gt 0)
        copies = $hits.Count
        resolved_path = ($hits -join ';')
    }
}

$voiceRoots = @($DataRoots | ForEach-Object { Join-Path $_ 'Voice\Battlefield' } | Where-Object { Test-Path $_ })
$allBankFiles = @($voiceRoots | ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -File -Filter '*.ogg' })
$knownFullPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($item in $coverage | Where-Object present) {
    foreach ($path in ($item.resolved_path -split ';')) { if ($path) { [void]$knownFullPaths.Add($path) } }
}
$unmapped = @($allBankFiles | Where-Object { -not $knownFullPaths.Contains($_.FullName) })
Write-Host "Battle-callout recording coverage"
Write-Host "Manifest rows: $($coverage.Count)"
foreach ($priority in @('P0','P1','P2')) {
    $group = @($coverage | Where-Object priority -eq $priority)
    $present = @($group | Where-Object present).Count
    Write-Host ("{0}: {1}/{2} present ({3:N1}%)" -f $priority,$present,$group.Count,(100.0 * $present / [Math]::Max(1,$group.Count)))
}
Write-Host "Battlefield-bank OGG files found: $($allBankFiles.Count)"
Write-Host "Manifest-unmapped battlefield OGG files: $($unmapped.Count)"

if ($CoverageOutput) {
    $coverage | Export-Csv -LiteralPath $CoverageOutput -NoTypeInformation -Encoding UTF8
    Write-Host "Coverage report: $CoverageOutput"
}

$rank = @{ None = -1; P0 = 0; P1 = 1; P2 = 2; All = 2 }
if ($Require -ne 'None') {
    $maxRank = $rank[$Require]
    $missingRequired = @($coverage | Where-Object { $rank[$_.priority] -le $maxRank -and -not $_.present })
    if ($missingRequired.Count -gt 0) {
        $sample = ($missingRequired | Select-Object -First 10 -ExpandProperty audio_path) -join ', '
        throw "Missing $($missingRequired.Count) required recording(s) through $Require. First missing: $sample"
    }
}

if ($unmapped.Count -gt 0) {
    Write-Host 'Unmapped existing bank examples:'
    $unmapped | Select-Object -First 15 -ExpandProperty FullName | ForEach-Object { Write-Host "  $_" }
}
Write-Host 'PASS: battle-callout asset manifest and data-root scan completed.'
