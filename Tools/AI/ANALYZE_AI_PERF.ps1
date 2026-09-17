param(
    [string]$Path = "Logs\AI_Performance.txt",
    [int]$Top = 12,
    [string]$CsvPath = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Path))
{
    Write-Error "AI performance log not found: $Path"
    exit 2
}

$records = New-Object System.Collections.Generic.List[object]
foreach ($line in Get-Content -LiteralPath $Path)
{
    if ($line -notmatch '\[AI-PERF\]')
    {
        continue
    }

    $values = @{}
    foreach ($match in [regex]::Matches($line, '\b([a-z_]+)=(-?\d+)\b'))
    {
        $values[$match.Groups[1].Value] = [int64]$match.Groups[2].Value
    }

    if (-not $values.ContainsKey('total_ms'))
    {
        continue
    }

    $row = [ordered]@{}
    foreach ($key in $values.Keys)
    {
        $row[$key] = $values[$key]
    }
    $records.Add([pscustomobject]$row)
}

if ($records.Count -eq 0)
{
    Write-Error "No parseable [AI-PERF] records found in: $Path"
    exit 3
}

function Get-PerfValue([object]$Record, [string]$Name)
{
    $property = $Record.PSObject.Properties[$Name]
    if ($null -eq $property)
    {
        return [int64]0
    }
    return [int64]$property.Value
}

function Get-Percentile([double[]]$Values, [double]$Percentile)
{
    if ($Values.Count -eq 0)
    {
        return 0.0
    }

    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 1)
    {
        return [double]$sorted[0]
    }

    $rank = ($Percentile / 100.0) * ($sorted.Count - 1)
    $lower = [int][math]::Floor($rank)
    $upper = [int][math]::Ceiling($rank)
    if ($lower -eq $upper)
    {
        return [double]$sorted[$lower]
    }

    $fraction = $rank - $lower
    return [double]$sorted[$lower] +
        (([double]$sorted[$upper] - [double]$sorted[$lower]) * $fraction)
}

function Get-Rate([int64]$Hits, [int64]$Misses)
{
    $total = $Hits + $Misses
    if ($total -le 0)
    {
        return 0.0
    }
    return 100.0 * $Hits / $total
}

Write-Host "AI performance summary"
Write-Host "Log: $Path"
Write-Host ("Decisions: {0}" -f $records.Count)

$timingFields = @(
    'total_ms',
    'pathfinding_ms',
    'threat_build_ms',
    'exposure_ms',
    'reaction_ms',
    'geometry_ms'
)

$timingRows = foreach ($field in $timingFields)
{
    [double[]]$values = @($records | ForEach-Object { [double](Get-PerfValue $_ $field) })
    [pscustomobject]@{
        Metric = $field
        Avg = [math]::Round((($values | Measure-Object -Average).Average), 2)
        P50 = [math]::Round((Get-Percentile $values 50), 2)
        P95 = [math]::Round((Get-Percentile $values 95), 2)
        P99 = [math]::Round((Get-Percentile $values 99), 2)
        Max = [math]::Round((($values | Measure-Object -Maximum).Maximum), 2)
    }
}

Write-Host ""
Write-Host "Latency distribution (ms)"
$timingRows | Format-Table -AutoSize

$estimateHits = [int64](($records | ForEach-Object { Get-PerfValue $_ 'estimate_hits' } | Measure-Object -Sum).Sum)
$estimateMisses = [int64](($records | ForEach-Object { Get-PerfValue $_ 'estimate_misses' } | Measure-Object -Sum).Sum)
$routeHits = [int64](($records | ForEach-Object { Get-PerfValue $_ 'route_hits' } | Measure-Object -Sum).Sum)
$routeMisses = [int64](($records | ForEach-Object { Get-PerfValue $_ 'route_misses' } | Measure-Object -Sum).Sum)
$cacheHits = [int64](($records | ForEach-Object { Get-PerfValue $_ 'cache_hits' } | Measure-Object -Sum).Sum)
$cacheMisses = [int64](($records | ForEach-Object { Get-PerfValue $_ 'cache_misses' } | Measure-Object -Sum).Sum)
$sharedHits = [int64](($records | ForEach-Object { Get-PerfValue $_ 'shared_contact_hits' } | Measure-Object -Sum).Sum)
$sharedMisses = [int64](($records | ForEach-Object { Get-PerfValue $_ 'shared_contact_misses' } | Measure-Object -Sum).Sum)
$geometryHits = [int64](($records | ForEach-Object { Get-PerfValue $_ 'geometry_hits' } | Measure-Object -Sum).Sum)
$geometryMisses = [int64](($records | ForEach-Object { Get-PerfValue $_ 'geometry_misses' } | Measure-Object -Sum).Sum)

$cacheRows = @(
    [pscustomobject]@{ Cache = 'estimate'; Hits = $estimateHits; Misses = $estimateMisses; HitPct = [math]::Round((Get-Rate $estimateHits $estimateMisses), 2) },
    [pscustomobject]@{ Cache = 'route'; Hits = $routeHits; Misses = $routeMisses; HitPct = [math]::Round((Get-Rate $routeHits $routeMisses), 2) },
    [pscustomobject]@{ Cache = 'overall'; Hits = $cacheHits; Misses = $cacheMisses; HitPct = [math]::Round((Get-Rate $cacheHits $cacheMisses), 2) },
    [pscustomobject]@{ Cache = 'shared_contact'; Hits = $sharedHits; Misses = $sharedMisses; HitPct = [math]::Round((Get-Rate $sharedHits $sharedMisses), 2) },
    [pscustomobject]@{ Cache = 'geometry'; Hits = $geometryHits; Misses = $geometryMisses; HitPct = [math]::Round((Get-Rate $geometryHits $geometryMisses), 2) }
)

Write-Host ""
Write-Host "Cache efficiency"
$cacheRows | Format-Table -AutoSize

$counterFields = @(
    'path_searches',
    'path_reuses',
    'budget_earlyouts',
    'detailed_candidates',
    'lookahead_nodes',
    'contacts'
)

$counterRows = foreach ($field in $counterFields)
{
    [double[]]$values = @($records | ForEach-Object { [double](Get-PerfValue $_ $field) })
    [pscustomobject]@{
        Metric = $field
        Avg = [math]::Round((($values | Measure-Object -Average).Average), 2)
        P95 = [math]::Round((Get-Percentile $values 95), 2)
        Max = [math]::Round((($values | Measure-Object -Maximum).Maximum), 2)
        Sum = [int64](($values | Measure-Object -Sum).Sum)
    }
}

Write-Host ""
Write-Host "Decision-work counters"
$counterRows | Format-Table -AutoSize

Write-Host ""
Write-Host ("Slowest {0} decisions" -f ([math]::Min($Top, $records.Count)))
$records |
    Sort-Object { Get-PerfValue $_ 'total_ms' } -Descending |
    Select-Object -First $Top |
    ForEach-Object {
        [pscustomobject]@{
            total_ms = Get-PerfValue $_ 'total_ms'
            path_ms = Get-PerfValue $_ 'pathfinding_ms'
            exposure_ms = Get-PerfValue $_ 'exposure_ms'
            geometry_ms = Get-PerfValue $_ 'geometry_ms'
            path_searches = Get-PerfValue $_ 'path_searches'
            candidates = Get-PerfValue $_ 'detailed_candidates'
            earlyouts = Get-PerfValue $_ 'budget_earlyouts'
            team = Get-PerfValue $_ 'team'
            soldier = Get-PerfValue $_ 'soldier'
            uid = Get-PerfValue $_ 'uid'
            turn = Get-PerfValue $_ 'turn'
            grid = Get-PerfValue $_ 'grid'
        }
    } | Format-Table -AutoSize

if ($CsvPath)
{
    $records | Export-Csv -LiteralPath $CsvPath -NoTypeInformation
    Write-Host ""
    Write-Host "Parsed decision rows exported to: $CsvPath"
}
