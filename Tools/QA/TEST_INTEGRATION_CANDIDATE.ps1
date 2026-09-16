param(
    [Parameter(Mandatory = $true)]
    [string]$CandidateRef,
    [string]$CanonicalRef = "origin/install/all-2026-09-12",
    [string]$ExpectedCanonicalSha = ""
)

$ErrorActionPreference = "Stop"

function Invoke-Git {
    param([string[]]$Arguments, [string]$WorkingDirectory = $PSScriptRoot)
    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & git -C $WorkingDirectory @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    return [pscustomobject]@{ Output = @($output); ExitCode = $exitCode }
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

$fetch = Invoke-Git @("fetch", "origin", "--prune") $repo
if ($fetch.ExitCode -ne 0) { throw "git fetch failed: $($fetch.Output -join [Environment]::NewLine)" }

$canonical = Invoke-Git @("rev-parse", "--verify", $CanonicalRef) $repo
if ($canonical.ExitCode -ne 0) { throw "Canonical ref not found: $CanonicalRef" }
$canonicalSha = ($canonical.Output | Select-Object -First 1).Trim()

if ($ExpectedCanonicalSha -and $canonicalSha -ne $ExpectedCanonicalSha) {
    throw "Canonical moved. Expected $ExpectedCanonicalSha but found $canonicalSha. Re-run the gate."
}
$candidate = Invoke-Git @("rev-parse", "--verify", $CandidateRef) $repo
if ($candidate.ExitCode -ne 0) { throw "Candidate ref not found: $CandidateRef" }
$candidateSha = ($candidate.Output | Select-Object -First 1).Trim()

$counts = Invoke-Git @("rev-list", "--left-right", "--count", "$CanonicalRef...$CandidateRef") $repo
if ($counts.ExitCode -ne 0) { throw "Unable to compare refs." }

Write-Host "Canonical: $CanonicalRef @ $canonicalSha"
Write-Host "Candidate: $CandidateRef @ $candidateSha"
Write-Host "Left/right unique commits: $($counts.Output -join ' ')"

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("vr-integration-gate-" + $PID)
if (Test-Path $tempRoot) { Remove-Item -Recurse -Force $tempRoot }

$add = Invoke-Git @("worktree", "add", "--detach", $tempRoot, $canonicalSha) $repo
if ($add.ExitCode -ne 0) { throw "Unable to create disposable worktree: $($add.Output -join [Environment]::NewLine)" }

$mergeExit = 1
try {
    $merge = Invoke-Git @("merge", "--no-commit", "--no-ff", $CandidateRef) $tempRoot
    $mergeExit = $merge.ExitCode
    $conflicts = Invoke-Git @("diff", "--name-only", "--diff-filter=U") $tempRoot
    $conflictPaths = @($conflicts.Output | Where-Object { $_ -and $_ -notmatch '^warning:' })

    if ($mergeExit -ne 0) {
        Write-Host "MERGE_CONFLICT"
        $conflictPaths | ForEach-Object { Write-Host "  $_" }
        exit 2
    }
    $check = Invoke-Git @("diff", "--check", $canonicalSha) $tempRoot
    if ($check.ExitCode -ne 0) {
        Write-Host "DIFF_CHECK_FAILED"
        $check.Output | ForEach-Object { Write-Host $_ }
        exit 3
    }

    $changed = Invoke-Git @("diff", "--name-only", $canonicalSha) $tempRoot
    Write-Host "MERGE_CLEAN"
    Write-Host "Changed files:"
    $changed.Output | ForEach-Object { if ($_){ Write-Host "  $_" } }

    $hot = @($changed.Output | Where-Object {
        $_ -match '^(TacticalAI|Tactical|Strategic|TileEngine)/'
    })
    if ($hot.Count -gt 0) {
        Write-Host "HOT_ZONE_REVIEW_REQUIRED"
        $hot | ForEach-Object { Write-Host "  $_" }
    }

    exit 0
}
finally {
    & git -C $repo worktree remove --force $tempRoot 2>$null | Out-Null
}
