param(
    [string]$CanonicalRef = "origin/install/all-2026-09-12",
    [string]$RegistryPath = (Join-Path $PSScriptRoot "RELEASE_STREAM_REGISTRY.json"),
    [string[]]$CandidateRefs = @(),
    [switch]$GateRelease,
    [switch]$ShowUnregisteredDiverged,
    [string]$ExpectedCanonicalSha = "",
    [string]$JsonReport = ""
)

$ErrorActionPreference = "Stop"
$validStates = @("ready", "active", "blocked", "parked", "superseded")

function Resolve-GitExecutable {
    $command = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $desktopPattern = Join-Path $env:LOCALAPPDATA "GitHubDesktop\app-*\resources\app\git\cmd\git.exe"
    $desktopGit = Get-Item $desktopPattern -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1
    if ($desktopGit) { return $desktopGit.FullName }
    throw "Git executable not found."
}

$script:GitExe = Resolve-GitExecutable
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
function Invoke-Git {
    param([string[]]$Arguments)
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $script:GitExe -C $repo @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $saved }
    return [pscustomobject]@{ Output = @($output); ExitCode = $exitCode }
}

function Normalize-BranchName {
    param([string]$Name)
    if (-not $Name) { return "" }
    return ($Name.Trim() -replace '^origin/', '')
}

$canonicalResult = Invoke-Git @("rev-parse", "--verify", $CanonicalRef)
if ($canonicalResult.ExitCode -ne 0) { throw "Canonical ref not found: $CanonicalRef" }
$canonicalSha = ($canonicalResult.Output | Select-Object -First 1).Trim()
if ($ExpectedCanonicalSha -and $canonicalSha -ne $ExpectedCanonicalSha) {
    Write-Host "RELEASE_GATE_BLOCK Canonical moved: expected $ExpectedCanonicalSha but found $canonicalSha"
    exit 60
}

if (-not (Test-Path $RegistryPath)) { throw "Release registry missing: $RegistryPath" }
$registry = Get-Content -Raw -Path $RegistryPath | ConvertFrom-Json
if (-not $registry.streams) { throw "Release registry has no streams array." }
$registryByBranch = @{}
foreach ($entry in @($registry.streams)) {
    $branch = Normalize-BranchName $entry.branch
    if (-not $branch) { throw "Release registry contains a stream without a branch." }
    if ($registryByBranch.ContainsKey($branch)) { throw "Duplicate release registry branch: $branch" }
    if ($validStates -notcontains [string]$entry.state) {
        throw "Invalid release state '$($entry.state)' for $branch"
    }
    if (-not [string]$entry.stream) { throw "Release registry branch $branch has no stream name." }
    if (-not [string]$entry.reason) { throw "Release registry branch $branch has no reason." }
    $registryByBranch[$branch] = $entry
}

$listResult = Invoke-Git @("for-each-ref", "--format=%(refname:short)", "refs/remotes/origin")
if ($listResult.ExitCode -ne 0) { throw "Unable to list origin refs." }
$remoteRefs = @($listResult.Output | Where-Object { $_ -and $_ -notmatch '/HEAD$' })
$rows = @()

foreach ($ref in $remoteRefs) {
    $branch = Normalize-BranchName $ref
    if ($branch -eq (Normalize-BranchName $CanonicalRef)) { continue }
    if ($branch -match '^(archive/|playtest/)' -or $branch -in @("master", "super-master")) { continue }

    $countsResult = Invoke-Git @("rev-list", "--left-right", "--count", "$CanonicalRef...$ref")
    if ($countsResult.ExitCode -ne 0) { continue }
    $parts = (($countsResult.Output | Select-Object -First 1) -split '\s+')
    if ($parts.Count -lt 2) { continue }
    $behind = [int]$parts[0]; $ahead = [int]$parts[1]
    if ($ahead -eq 0 -and $behind -eq 0) { $relation = "ALIGNED" }
    elseif ($behind -eq 0 -and $ahead -gt 0) { $relation = "AHEAD" }
    elseif ($behind -gt 0 -and $ahead -eq 0) { $relation = "CONTAINED" }
    else { $relation = "DIVERGED" }

    $entry = $null
    if ($registryByBranch.ContainsKey($branch)) { $entry = $registryByBranch[$branch] }
    $state = if ($entry) { [string]$entry.state } else { "unregistered" }
    $stream = if ($entry) { [string]$entry.stream } else { "" }
    $reason = if ($entry) { [string]$entry.reason } else { "" }

    $rows += [pscustomobject]@{
        Branch = $branch
        Relation = $relation
        Ahead = $ahead
        Behind = $behind
        State = $state
        Stream = $stream
        Reason = $reason
    }
}

$rows = @($rows | Sort-Object State, Relation, Branch)
$pendingRows = @($rows | Where-Object { $_.Relation -in @("AHEAD", "DIVERGED") })
$unregisteredAhead = @($pendingRows | Where-Object { $_.State -eq "unregistered" -and $_.Relation -eq "AHEAD" })
$readyAhead = @($pendingRows | Where-Object { $_.State -eq "ready" -and $_.Relation -eq "AHEAD" })
$readyDiverged = @($pendingRows | Where-Object { $_.State -eq "ready" -and $_.Relation -eq "DIVERGED" })
$historicalDiverged = @($pendingRows | Where-Object { $_.State -eq "unregistered" -and $_.Relation -eq "DIVERGED" })
$displayPendingRows = if ($ShowUnregisteredDiverged) { $pendingRows } else { @($pendingRows | Where-Object { -not ($_.State -eq "unregistered" -and $_.Relation -eq "DIVERGED") }) }
$candidateNames = @($CandidateRefs | ForEach-Object { Normalize-BranchName $_ } | Where-Object { $_ } | Select-Object -Unique)
$omittedReady = @($readyAhead | Where-Object { $candidateNames -notcontains $_.Branch })
$nonReadyCandidates = @()
$unknownCandidates = @()

foreach ($candidate in $candidateNames) {
    $row = $rows | Where-Object { $_.Branch -eq $candidate } | Select-Object -First 1
    if (-not $row) {
        $unknownCandidates += $candidate
        continue
    }
    if ($row.State -ne "ready" -or $row.Relation -ne "AHEAD") {
        $nonReadyCandidates += $candidate
    }
}

Write-Host "Canonical: $CanonicalRef @ $canonicalSha"
Write-Host "Release registry: $RegistryPath"
Write-Host ""
Write-Host "Pending branch inventory:"
if ($displayPendingRows.Count -eq 0) {
    Write-Host "  none"
}
else {
    $displayPendingRows | Select-Object Branch, Relation, Ahead, Behind, State, Stream, Reason | Format-Table -AutoSize
}
if (-not $ShowUnregisteredDiverged -and $historicalDiverged.Count -gt 0) {
    Write-Host ("  Historical/unregistered DIVERGED refs hidden: " + $historicalDiverged.Count + " (use -ShowUnregisteredDiverged to list)")
}

Write-Host ""
Write-Host "Release-ready AHEAD streams: $($readyAhead.Count)"
foreach ($row in $readyAhead) { Write-Host "  READY $($row.Branch) [$($row.Stream)]" }
foreach ($row in $readyDiverged) { Write-Host "  BLOCKED_READY_DIVERGED $($row.Branch) [$($row.Stream)]" }
foreach ($row in $unregisteredAhead) { Write-Host "  UNREGISTERED_AHEAD $($row.Branch)" }
if ($JsonReport) {
    $report = [pscustomobject]@{
        canonical_ref = $CanonicalRef
        canonical_sha = $canonicalSha
        pending = $pendingRows
        ready_ahead = $readyAhead
        ready_diverged = $readyDiverged
        unregistered_ahead = $unregisteredAhead
        historical_unregistered_diverged = $historicalDiverged
        requested_candidates = $candidateNames
        omitted_ready = $omittedReady
        non_ready_candidates = $nonReadyCandidates
        unknown_candidates = $unknownCandidates
    }
    $report | ConvertTo-Json -Depth 8 | Set-Content -Path $JsonReport -Encoding UTF8
}

if ($GateRelease) {
    if ($unregisteredAhead.Count -gt 0) {
        Write-Host "RELEASE_GATE_BLOCK Release inventory incomplete: unregistered AHEAD branches exist."
        exit 62
    }
    if ($readyDiverged.Count -gt 0) {
        Write-Host "RELEASE_GATE_BLOCK Release-ready branches are diverged and must be forward-ported before release."
        exit 64
    }
    if ($unknownCandidates.Count -gt 0) {
        Write-Host ("RELEASE_GATE_BLOCK Unknown release candidates: " + ($unknownCandidates -join ", "))
        exit 63
    }
    if ($nonReadyCandidates.Count -gt 0) {
        Write-Host ("RELEASE_GATE_BLOCK Candidates are not registry-ready AHEAD tips: " + ($nonReadyCandidates -join ", "))
        exit 63
    }
    if ($omittedReady.Count -gt 0) {
        Write-Host ("RELEASE_GATE_BLOCK Release would omit ready streams: " + (($omittedReady | ForEach-Object { $_.Branch }) -join ", "))
        exit 61
    }
    Write-Host "RELEASE_INVENTORY_GATE_OK"
}

exit 0
