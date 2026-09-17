param(
    [Parameter(Mandatory = $true)]
    [string[]]$CandidateRefs,
    [string]$CanonicalRef = "origin/install/all-2026-09-12",
    [string]$ExpectedCanonicalSha = "",
    [switch]$Fetch,
    [switch]$AllowStrategicChanges,
    [string[]]$AllowedStrategicPaths = @(),
    [switch]$AllowCrossStreamFileOverlap,
    [string[]]$AllowedCrossStreamPaths = @()
)

$ErrorActionPreference = "Stop"

function Resolve-GitExecutable {
    $command = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $fixed = @(
        (Join-Path $env:ProgramFiles "Git\cmd\git.exe"),
        (Join-Path ([Environment]::GetFolderPath("ProgramFilesX86")) "Git\cmd\git.exe")
    )
    foreach ($path in $fixed) {
        if ($path -and (Test-Path $path)) { return $path }
    }

    $desktopPattern = Join-Path $env:LOCALAPPDATA "GitHubDesktop\app-*\resources\app\git\cmd\git.exe"
    $desktopGit = Get-Item $desktopPattern -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1
    if ($desktopGit) { return $desktopGit.FullName }

    throw "Git executable not found. Install Git or GitHub Desktop."
}
$script:GitExe = Resolve-GitExecutable

function Invoke-Git {
    param([string[]]$Arguments, [string]$WorkingDirectory)
    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $script:GitExe -C $WorkingDirectory @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    return [pscustomobject]@{ Output = @($output); ExitCode = $exitCode }
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Write-Host "Git: $script:GitExe"
Write-Host "Repository: $repo"

if ($Fetch) {
    $fetch = Invoke-Git @("fetch", "origin", "--prune") $repo
    if ($fetch.ExitCode -ne 0) {
        throw "git fetch failed: $($fetch.Output -join [Environment]::NewLine)"
    }
    Write-Host "FETCH_OK"
}
else {
    Write-Host "FETCH_SKIPPED - using currently known refs."
}
$canonical = Invoke-Git @("rev-parse", "--verify", $CanonicalRef) $repo
if ($canonical.ExitCode -ne 0) { throw "Canonical ref not found: $CanonicalRef" }
$canonicalSha = ($canonical.Output | Select-Object -First 1).Trim()

if ($ExpectedCanonicalSha -and $canonicalSha -ne $ExpectedCanonicalSha) {
    throw "Canonical moved. Expected $ExpectedCanonicalSha but found $canonicalSha."
}

Write-Host "Canonical: $CanonicalRef @ $canonicalSha"

$candidates = @()
foreach ($candidateRef in $CandidateRefs) {
    $candidate = Invoke-Git @("rev-parse", "--verify", $candidateRef) $repo
    if ($candidate.ExitCode -ne 0) { throw "Candidate ref not found: $candidateRef" }
    $candidateSha = ($candidate.Output | Select-Object -First 1).Trim()

    $contained = Invoke-Git @("merge-base", "--is-ancestor", $candidateSha, $canonicalSha) $repo
    if ($contained.ExitCode -eq 0) {
        throw "Candidate already contained by canonical: $candidateRef @ $candidateSha"
    }

    $forwardPorted = Invoke-Git @("merge-base", "--is-ancestor", $canonicalSha, $candidateSha) $repo
    if ($forwardPorted.ExitCode -ne 0) {
        throw "FORWARD_PORT_REQUIRED before batch gate: $candidateRef @ $candidateSha"
    }

    $candidates += [pscustomobject]@{ Name = $candidateRef; Sha = $candidateSha }
}

$redundantIndexes = @()
for ($i = 0; $i -lt $candidates.Count; $i++) {
    for ($j = 0; $j -lt $candidates.Count; $j++) {
        if ($i -eq $j) { continue }

        if ($candidates[$i].Sha -eq $candidates[$j].Sha) {
            if ($i -gt $j) {
                $redundantIndexes += $i
                break
            }
            continue
        }

        $containedByCandidate = Invoke-Git @("merge-base", "--is-ancestor", $candidates[$i].Sha, $candidates[$j].Sha) $repo
        if ($containedByCandidate.ExitCode -eq 0) {
            Write-Host "REDUNDANT_CANDIDATE_CONTAINED: $($candidates[$i].Name) @ $($candidates[$i].Sha) is already contained by $($candidates[$j].Name) @ $($candidates[$j].Sha)"
            $redundantIndexes += $i
            break
        }
    }
}

if ($redundantIndexes.Count -gt 0) {
    $redundantSet = @($redundantIndexes | Sort-Object -Unique)
    $filtered = @()
    for ($i = 0; $i -lt $candidates.Count; $i++) {
        if ($redundantSet -notcontains $i) { $filtered += $candidates[$i] }
    }
    $candidates = $filtered
}
if ($candidates.Count -eq 0) {
    throw "No independent candidate remains after redundant-candidate consolidation."
}

Write-Host "Batch candidates pinned in order:"
$candidates | ForEach-Object { Write-Host "  $($_.Name) @ $($_.Sha)" }

$conflictGate = Join-Path $PSScriptRoot "CHECK_INTEGRATION_CONFLICTS.ps1"
if (-not (Test-Path $conflictGate)) {
    throw "Cross-stream conflict gate is missing: $conflictGate"
}
if ($AllowCrossStreamFileOverlap -and $AllowedCrossStreamPaths.Count -gt 0) {
    throw "Use either -AllowCrossStreamFileOverlap or -AllowedCrossStreamPaths, not both."
}

$conflictArgs = @{
    CandidateRefs = @($candidates | ForEach-Object { $_.Sha })
    CanonicalRef = $canonicalSha
    ExpectedCanonicalSha = $canonicalSha
}
if ($AllowedCrossStreamPaths.Count -gt 0) {
    $conflictArgs["AllowedSharedPaths"] = $AllowedCrossStreamPaths
}
if (-not $AllowCrossStreamFileOverlap) {
    $conflictArgs["FailOnSharedFiles"] = $true
}
& $conflictGate @conflictArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "BATCH_CROSS_STREAM_CONFLICT_SCAN_FAILED"
    exit $LASTEXITCODE
}
Write-Host "BATCH_CROSS_STREAM_CONFLICT_SCAN_OK"

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("vr-integration-batch-" + $PID)
if (Test-Path $tempRoot) { Remove-Item -Recurse -Force $tempRoot }
$add = Invoke-Git @("worktree", "add", "--detach", $tempRoot, $canonicalSha) $repo
if ($add.ExitCode -ne 0) {
    throw "Unable to create disposable worktree: $($add.Output -join [Environment]::NewLine)"
}

try {
    [void](Invoke-Git @("config", "user.name", "VR Integration QA") $tempRoot)
    [void](Invoke-Git @("config", "user.email", "qa@local.invalid") $tempRoot)

    foreach ($candidate in $candidates) {
        $candidateRef = $candidate.Name
        $candidateSha = $candidate.Sha
        $before = Invoke-Git @("rev-parse", "HEAD") $tempRoot
        $beforeSha = ($before.Output | Select-Object -First 1).Trim()

        Write-Host "MERGING $candidateRef @ $candidateSha"
        $merge = Invoke-Git @(
            "merge", "--no-ff", "-m", "QA temporary merge: $candidateRef @ $candidateSha", $candidateSha
        ) $tempRoot

        if ($merge.ExitCode -ne 0) {
            Write-Host "BATCH_MERGE_CONFLICT: $candidateRef @ $candidateSha"
            $conflicts = Invoke-Git @("diff", "--name-only", "--diff-filter=U") $tempRoot
            $conflicts.Output | Where-Object { $_ } | ForEach-Object { Write-Host "  $_" }
            exit 20
        }

        $delta = Invoke-Git @("diff", "--name-only", "$beforeSha..HEAD") $tempRoot
        $deltaPaths = @($delta.Output | Where-Object { $_ })
        Write-Host "MERGED_OK: $candidateRef @ $candidateSha ($($deltaPaths.Count) files)"
    }
    $check = Invoke-Git @("-c", "core.whitespace=cr-at-eol", "diff", "--check", "$canonicalSha..HEAD") $tempRoot
    if ($check.ExitCode -ne 0) {
        Write-Host "BATCH_DIFF_CHECK_FAILED"
        $check.Output | ForEach-Object { Write-Host $_ }
        exit 21
    }

    $changed = Invoke-Git @("diff", "--name-only", "$canonicalSha..HEAD") $tempRoot
    if ($changed.ExitCode -ne 0) { throw "Unable to enumerate batch changes." }
    $changedPaths = @($changed.Output | Where-Object { $_ })

    $noise = @($changedPaths | Where-Object {
        $_ -match '(^|/)(bin|build|Debug|Release|ipch|\.vs)(/|$)' -or
        $_ -match '(\.vcxproj\.user|\.suo|\.obj|\.pdb|\.ilk|\.tlog|\.log)$'
    })
    if ($noise.Count -gt 0) {
        Write-Host "BATCH_GENERATED_OR_IDE_NOISE_BLOCKED"
        $noise | ForEach-Object { Write-Host "  $_" }
        exit 22
    }

    $strategic = @($changedPaths | Where-Object { $_ -match '^Strategic/' })
    if ($AllowStrategicChanges -and $AllowedStrategicPaths.Count -gt 0) {
        throw "Use either -AllowStrategicChanges or -AllowedStrategicPaths, not both."
    }

    if ($strategic.Count -gt 0 -and -not $AllowStrategicChanges) {
        $normalizedAllowlist = @($AllowedStrategicPaths | ForEach-Object { $_.Replace('\', '/').TrimStart('.', '/') } | Where-Object { $_ })
        $unexpectedStrategic = @($strategic | Where-Object { $normalizedAllowlist -notcontains $_ })
        if ($unexpectedStrategic.Count -gt 0) {
            Write-Host "BATCH_STRATEGIC_LAYER_CHANGE_BLOCKED"
            $unexpectedStrategic | ForEach-Object { Write-Host "  $_" }
            if ($normalizedAllowlist.Count -gt 0) {
                Write-Host "Strategic allowlist:"
                $normalizedAllowlist | ForEach-Object { Write-Host "  $_" }
            }
            exit 23
        }
        Write-Host "BATCH_STRATEGIC_LAYER_CHANGE_ALLOWLIST_OK"
        $strategic | ForEach-Object { Write-Host "  $_" }
    }
    if ($changedPaths.Count -gt 0) {
        $markerArgs = @("grep", "-n", "-E", "^(<<<<<<< .+|=======|>>>>>>> .+)$", "--") + $changedPaths
        $markers = Invoke-Git $markerArgs $tempRoot
        if ($markers.ExitCode -eq 0) {
            Write-Host "BATCH_MERGE_MARKERS_BLOCKED"
            $markers.Output | ForEach-Object { Write-Host $_ }
            exit 24
        }
        elseif ($markers.ExitCode -gt 1) {
            throw "git grep failed while checking merge markers."
        }
    }

    $ps1Paths = @($changedPaths | Where-Object { $_ -match '\.ps1$' })
    foreach ($relativePath in $ps1Paths) {
        $fullPath = Join-Path $tempRoot ($relativePath -replace '/', '\')
        $tokens = $null
        $errors = $null
        [void][System.Management.Automation.Language.Parser]::ParseFile(
            $fullPath, [ref]$tokens, [ref]$errors
        )
        if ($errors.Count -gt 0) {
            Write-Host "BATCH_POWERSHELL_PARSE_FAILED: $relativePath"
            $errors | Format-List
            exit 25
        }
    }
    $aiTouched = @($changedPaths | Where-Object {
        $_ -match '^(TacticalAI|ModularizedTacticalAI|Tools/AI)/'
    }).Count -gt 0

    if ($aiTouched) {
        $aiAudit = Join-Path $tempRoot "Tools\AI\VERIFY_AI_INTEGRITY.ps1"
        if (-not (Test-Path $aiAudit)) {
            throw "AI files changed but VERIFY_AI_INTEGRITY.ps1 is missing."
        }
        & $aiAudit -RepositoryRoot $tempRoot
        if ($LASTEXITCODE -ne 0) {
            Write-Host "BATCH_AI_INTEGRITY_FAILED"
            exit 26
        }
        Write-Host "BATCH_AI_INTEGRITY_OK"
    }

    Write-Host "BATCH_STATIC_GATE_OK"
    Write-Host "Combined changed files: $($changedPaths.Count)"
    Write-Host "BUILD_STILL_REQUIRED_BEFORE_PLAYTEST_READY"
    exit 0
}
finally {
    & $script:GitExe -C $repo worktree remove --force $tempRoot 2>$null | Out-Null
}
