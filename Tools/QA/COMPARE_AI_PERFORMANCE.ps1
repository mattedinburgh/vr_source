param(
    [Parameter(Mandatory = $true)]
    [string]$BaselineLog,
    [Parameter(Mandatory = $true)]
    [string]$CandidateLog,
    [double]$MaxMedianRegressionPct = 15.0,
    [double]$MaxP95RegressionPct = 20.0,
    [double]$MaxPathfindingP95RegressionPct = 25.0,
    [double]$MaxPathSearchMeanRegressionPct = 20.0,
    [double]$MaxCacheHitRateDropPctPoints = 10.0,
    [double]$MinTimingToleranceMs = 10.0,
    [double]$MaxDecisionMs = 1000.0,
    [switch]$FailOnRegression,
    [string]$JsonOutput = ""
)

$ErrorActionPreference = "Stop"

function Read-AIPerfSamples {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        throw "Performance log not found: $Path"
    }

    $samples = @()
    foreach ($line in [System.IO.File]::ReadLines((Resolve-Path $Path).Path)) {
        if ($line -notmatch '\[AI-PERF\]\s+(?<payload>.+)$') {
            continue
        }

        $metrics = @{}
        foreach ($match in [regex]::Matches($Matches.payload, '(?<key>[A-Za-z_]+)=(?<value>-?[0-9]+(?:\.[0-9]+)?)')) {
            $metrics[$match.Groups["key"].Value] = [double]$match.Groups["value"].Value
        }

        if (-not $metrics.ContainsKey("total_ms")) {
            continue
        }

        $samples += [pscustomobject]@{
            TotalMs = $metrics["total_ms"]
            ThreatBuildMs = if ($metrics.ContainsKey("threat_build_ms")) { $metrics["threat_build_ms"] } else { 0 }
            PathfindingMs = if ($metrics.ContainsKey("pathfinding_ms")) { $metrics["pathfinding_ms"] } else { 0 }
            ExposureMs = if ($metrics.ContainsKey("exposure_ms")) { $metrics["exposure_ms"] } else { 0 }
            ReactionMs = if ($metrics.ContainsKey("reaction_ms")) { $metrics["reaction_ms"] } else { 0 }
            GeometryMs = if ($metrics.ContainsKey("geometry_ms")) { $metrics["geometry_ms"] } else { 0 }
            PathSearches = if ($metrics.ContainsKey("path_searches")) { $metrics["path_searches"] } else { 0 }
            PathReuses = if ($metrics.ContainsKey("path_reuses")) { $metrics["path_reuses"] } else { 0 }
            CacheHits = if ($metrics.ContainsKey("cache_hits")) { $metrics["cache_hits"] } else { 0 }
            CacheMisses = if ($metrics.ContainsKey("cache_misses")) { $metrics["cache_misses"] } else { 0 }
            RouteHits = if ($metrics.ContainsKey("route_hits")) { $metrics["route_hits"] } else { 0 }
            RouteMisses = if ($metrics.ContainsKey("route_misses")) { $metrics["route_misses"] } else { 0 }
            BudgetEarlyOuts = if ($metrics.ContainsKey("budget_earlyouts")) { $metrics["budget_earlyouts"] } else { 0 }
            DetailedCandidates = if ($metrics.ContainsKey("detailed_candidates")) { $metrics["detailed_candidates"] } else { 0 }
            LookaheadNodes = if ($metrics.ContainsKey("lookahead_nodes")) { $metrics["lookahead_nodes"] } else { 0 }
        }
    }

    return @($samples)
}

function Get-Percentile {
    param(
        [double[]]$Values,
        [double]$Percentile
    )

    if (-not $Values -or $Values.Count -eq 0) { return 0.0 }
    $sorted = @($Values | Sort-Object)
    $rank = [Math]::Ceiling($Percentile * $sorted.Count) - 1
    if ($rank -lt 0) { $rank = 0 }
    if ($rank -ge $sorted.Count) { $rank = $sorted.Count - 1 }
    return [double]$sorted[$rank]
}

function Get-Aggregate {
    param([object[]]$Samples)

    if (-not $Samples -or $Samples.Count -eq 0) {
        throw "No [AI-PERF] samples were found."
    }

    $total = @($Samples | ForEach-Object { [double]$_.TotalMs })
    $path = @($Samples | ForEach-Object { [double]$_.PathfindingMs })
    $searches = @($Samples | ForEach-Object { [double]$_.PathSearches })
    $hits = [double](($Samples | Measure-Object CacheHits -Sum).Sum)
    $misses = [double](($Samples | Measure-Object CacheMisses -Sum).Sum)
    $routeHits = [double](($Samples | Measure-Object RouteHits -Sum).Sum)
    $routeMisses = [double](($Samples | Measure-Object RouteMisses -Sum).Sum)

    $cacheDenom = $hits + $misses
    $routeDenom = $routeHits + $routeMisses

    return [pscustomobject]@{
        Samples = $Samples.Count
        TotalMeanMs = [Math]::Round((($total | Measure-Object -Average).Average), 2)
        TotalMedianMs = [Math]::Round((Get-Percentile $total 0.50), 2)
        TotalP95Ms = [Math]::Round((Get-Percentile $total 0.95), 2)
        TotalMaxMs = [Math]::Round((($total | Measure-Object -Maximum).Maximum), 2)
        PathfindingMeanMs = [Math]::Round((($path | Measure-Object -Average).Average), 2)
        PathfindingP95Ms = [Math]::Round((Get-Percentile $path 0.95), 2)
        PathSearchMean = [Math]::Round((($searches | Measure-Object -Average).Average), 2)
        PathSearchP95 = [Math]::Round((Get-Percentile $searches 0.95), 2)
        CacheHitRatePct = if ($cacheDenom -gt 0) { [Math]::Round(100.0 * $hits / $cacheDenom, 2) } else { 0.0 }
        RouteHitRatePct = if ($routeDenom -gt 0) { [Math]::Round(100.0 * $routeHits / $routeDenom, 2) } else { 0.0 }
        BudgetEarlyOuts = [double](($Samples | Measure-Object BudgetEarlyOuts -Sum).Sum)
        DetailedCandidates = [double](($Samples | Measure-Object DetailedCandidates -Sum).Sum)
        LookaheadNodes = [double](($Samples | Measure-Object LookaheadNodes -Sum).Sum)
    }
}

function Get-RegressionPct {
    param([double]$Baseline, [double]$Candidate)

    if ($Baseline -le 0) {
        if ($Candidate -le 0) { return 0.0 }
        return [double]::PositiveInfinity
    }
    return 100.0 * ($Candidate - $Baseline) / $Baseline
}

$baselineSamples = Read-AIPerfSamples $BaselineLog
$candidateSamples = Read-AIPerfSamples $CandidateLog

if ($baselineSamples.Count -eq 0) {
    throw "Baseline contains no [AI-PERF] samples: $BaselineLog"
}
if ($candidateSamples.Count -eq 0) {
    throw "Candidate contains no [AI-PERF] samples: $CandidateLog"
}

$baseline = Get-Aggregate $baselineSamples
$candidate = Get-Aggregate $candidateSamples

$medianReg = Get-RegressionPct $baseline.TotalMedianMs $candidate.TotalMedianMs
$p95Reg = Get-RegressionPct $baseline.TotalP95Ms $candidate.TotalP95Ms
$pathP95Reg = Get-RegressionPct $baseline.PathfindingP95Ms $candidate.PathfindingP95Ms
$searchReg = Get-RegressionPct $baseline.PathSearchMean $candidate.PathSearchMean
$cacheDrop = $baseline.CacheHitRatePct - $candidate.CacheHitRatePct

$checks = @()
$checks += [pscustomobject]@{
    Metric = "total median ms"
    Baseline = $baseline.TotalMedianMs
    Candidate = $candidate.TotalMedianMs
    Delta = [Math]::Round($candidate.TotalMedianMs - $baseline.TotalMedianMs, 2)
    RegressionPct = [Math]::Round($medianReg, 2)
    Pass = (($candidate.TotalMedianMs - $baseline.TotalMedianMs) -le $MinTimingToleranceMs) -or ($medianReg -le $MaxMedianRegressionPct)
}
$checks += [pscustomobject]@{
    Metric = "total p95 ms"
    Baseline = $baseline.TotalP95Ms
    Candidate = $candidate.TotalP95Ms
    Delta = [Math]::Round($candidate.TotalP95Ms - $baseline.TotalP95Ms, 2)
    RegressionPct = [Math]::Round($p95Reg, 2)
    Pass = (($candidate.TotalP95Ms - $baseline.TotalP95Ms) -le $MinTimingToleranceMs) -or ($p95Reg -le $MaxP95RegressionPct)
}
$checks += [pscustomobject]@{
    Metric = "pathfinding p95 ms"
    Baseline = $baseline.PathfindingP95Ms
    Candidate = $candidate.PathfindingP95Ms
    Delta = [Math]::Round($candidate.PathfindingP95Ms - $baseline.PathfindingP95Ms, 2)
    RegressionPct = [Math]::Round($pathP95Reg, 2)
    Pass = (($candidate.PathfindingP95Ms - $baseline.PathfindingP95Ms) -le $MinTimingToleranceMs) -or ($pathP95Reg -le $MaxPathfindingP95RegressionPct)
}
$checks += [pscustomobject]@{
    Metric = "path searches mean"
    Baseline = $baseline.PathSearchMean
    Candidate = $candidate.PathSearchMean
    Delta = [Math]::Round($candidate.PathSearchMean - $baseline.PathSearchMean, 2)
    RegressionPct = [Math]::Round($searchReg, 2)
    Pass = $searchReg -le $MaxPathSearchMeanRegressionPct
}
$checks += [pscustomobject]@{
    Metric = "cache hit rate pct"
    Baseline = $baseline.CacheHitRatePct
    Candidate = $candidate.CacheHitRatePct
    Delta = [Math]::Round($candidate.CacheHitRatePct - $baseline.CacheHitRatePct, 2)
    RegressionPct = 0.0
    Pass = $cacheDrop -le $MaxCacheHitRateDropPctPoints
}
$checks += [pscustomobject]@{
    Metric = "max decision ms"
    Baseline = $baseline.TotalMaxMs
    Candidate = $candidate.TotalMaxMs
    Delta = [Math]::Round($candidate.TotalMaxMs - $baseline.TotalMaxMs, 2)
    RegressionPct = [Math]::Round((Get-RegressionPct $baseline.TotalMaxMs $candidate.TotalMaxMs), 2)
    Pass = $candidate.TotalMaxMs -le $MaxDecisionMs
}

Write-Host "AI performance regression comparison"
Write-Host "Baseline:  $BaselineLog ($($baseline.Samples) samples)"
Write-Host "Candidate: $CandidateLog ($($candidate.Samples) samples)"
Write-Host ""
$checks | Format-Table Metric, Baseline, Candidate, Delta, RegressionPct, Pass -AutoSize

Write-Host ""
Write-Host ("Baseline cache hit rate: {0}% | Candidate: {1}%" -f $baseline.CacheHitRatePct, $candidate.CacheHitRatePct)
Write-Host ("Baseline route hit rate: {0}% | Candidate: {1}%" -f $baseline.RouteHitRatePct, $candidate.RouteHitRatePct)
Write-Host ("Budget early-outs: baseline {0} | candidate {1}" -f $baseline.BudgetEarlyOuts, $candidate.BudgetEarlyOuts)
Write-Host ("Detailed candidates: baseline {0} | candidate {1}" -f $baseline.DetailedCandidates, $candidate.DetailedCandidates)
Write-Host ("Lookahead nodes: baseline {0} | candidate {1}" -f $baseline.LookaheadNodes, $candidate.LookaheadNodes)

$failedChecks = @($checks | Where-Object { -not $_.Pass })

if ($JsonOutput) {
    $report = [pscustomobject]@{
        GeneratedUtc = (Get-Date).ToUniversalTime().ToString("o")
        BaselineLog = (Resolve-Path $BaselineLog).Path
        CandidateLog = (Resolve-Path $CandidateLog).Path
        Baseline = $baseline
        Candidate = $candidate
        Checks = $checks
        Passed = ($failedChecks.Count -eq 0)
    }
    $jsonPath = [System.IO.Path]::GetFullPath($JsonOutput)
    $jsonDir = Split-Path -Parent $jsonPath
    if ($jsonDir -and -not (Test-Path $jsonDir)) {
        New-Item -ItemType Directory -Force -Path $jsonDir | Out-Null
    }
    $report | ConvertTo-Json -Depth 8 | Set-Content -Path $jsonPath -Encoding UTF8
    Write-Host "PERF_REPORT_JSON=$jsonPath"
}

if ($failedChecks.Count -gt 0) {
    Write-Host ""
    Write-Host "AI_PERFORMANCE_REGRESSION_DETECTED"
    $failedChecks | ForEach-Object { Write-Host ("  FAIL: {0}" -f $_.Metric) }
    if ($FailOnRegression) {
        exit 41
    }
}
else {
    Write-Host "AI_PERFORMANCE_REGRESSION_GATE_OK"
}

exit 0

