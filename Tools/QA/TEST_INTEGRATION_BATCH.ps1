param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string[]]$CandidateRefs,
    [string]$CanonicalRef = "origin/install/all-2026-09-12",
    [string]$ExpectedCanonicalSha = "",
    [switch]$Fetch,
    [switch]$AllowStrategicChanges,
    [ValidateRange(0, 3600)]
    [int]$InterItemDelaySeconds = 60
)

$ErrorActionPreference = "Stop"
$script:GateExitCode = 1

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
    param(
        [string[]]$Arguments,
        [string]$WorkingDirectory
    )

    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $script:GitExe -C $WorkingDirectory @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }

    return [pscustomobject]@{
        Output = @($output)
        ExitCode = $exitCode
    }
}

function Fail-Gate {
    param(
        [int]$Code,
        [string]$Message
    )

    $script:GateExitCode = $Code
    throw $Message
}

function Remove-DisposableWorktree {
    param(
        [string]$Repository,
        [string]$Path
    )

    if ($Path -and (Test-Path $Path)) {
        & $script:GitExe -C $Repository worktree remove --force $Path 2>$null | Out-Null
    }
}

function New-DisposableWorktree {
    param(
        [string]$Repository,
        [string]$Path,
        [string]$BaseSha
    )

    if (Test-Path $Path) {
        Remove-Item -Recurse -Force $Path
    }

    $add = Invoke-Git @("worktree", "add", "--detach", $Path, $BaseSha) $Repository
    if ($add.ExitCode -ne 0) {
        Fail-Gate 30 "Unable to create disposable worktree: $($add.Output -join [Environment]::NewLine)"
    }

    [void](Invoke-Git @("config", "user.name", "VR Integration QA") $Path)
    [void](Invoke-Git @("config", "user.email", "qa@local.invalid") $Path)
}

function Test-StaticDelta {
    param(
        [string]$Worktree,
        [string]$BaselineSha,
        [string]$Prefix,
        [switch]$AllowStrategic
    )

    $check = Invoke-Git @("-c", "core.whitespace=cr-at-eol", "diff", "--check", "$BaselineSha..HEAD") $Worktree
    if ($check.ExitCode -ne 0) {
        Write-Host ($Prefix + "_DIFF_CHECK_FAILED")
        $check.Output | ForEach-Object { Write-Host $_ }
        Fail-Gate 21 ($Prefix + " diff check failed.")
    }

    $changed = Invoke-Git @("diff", "--name-only", "$BaselineSha..HEAD") $Worktree
    if ($changed.ExitCode -ne 0) {
        Fail-Gate 31 "Unable to enumerate changed files."
    }

    $changedPaths = @($changed.Output | Where-Object { $_ })
    $existingPaths = @($changedPaths | Where-Object {
        Test-Path (Join-Path $Worktree ($_ -replace '/', '\'))
    })

    $noise = @($changedPaths | Where-Object {
        $_ -match '(^|/)(bin|build|Debug|Release|ipch|\.vs)(/|$)' -or
        $_ -match '(\.vcxproj\.user|\.suo|\.obj|\.pdb|\.ilk|\.tlog|\.log)$'
    })
    if ($noise.Count -gt 0) {
        Write-Host ($Prefix + "_GENERATED_OR_IDE_NOISE_BLOCKED")
        $noise | ForEach-Object { Write-Host "  $_" }
        Fail-Gate 22 ($Prefix + " generated/IDE noise blocked.")
    }

    $strategic = @($changedPaths | Where-Object { $_ -match '^Strategic/' })
    if ($strategic.Count -gt 0 -and -not $AllowStrategic) {
        Write-Host ($Prefix + "_STRATEGIC_LAYER_CHANGE_BLOCKED")
        $strategic | ForEach-Object { Write-Host "  $_" }
        Fail-Gate 23 ($Prefix + " strategic-layer changes blocked.")
    }

    if ($existingPaths.Count -gt 0) {
        $markerArgs = @("grep", "-n", "-E", "^(<<<<<<< .+|=======|>>>>>>> .+)$", "--") + $existingPaths
        $markers = Invoke-Git $markerArgs $Worktree
        if ($markers.ExitCode -eq 0) {
            Write-Host ($Prefix + "_MERGE_MARKERS_BLOCKED")
            $markers.Output | ForEach-Object { Write-Host $_ }
            Fail-Gate 24 ($Prefix + " merge markers blocked.")
        }
        elseif ($markers.ExitCode -gt 1) {
            Fail-Gate 32 "git grep failed while checking merge markers."
        }
    }

    $ps1Paths = @($existingPaths | Where-Object { $_ -match '\.ps1$' })
    foreach ($relativePath in $ps1Paths) {
        $fullPath = Join-Path $Worktree ($relativePath -replace '/', '\')
        $tokens = $null
        $errors = $null
        [void][System.Management.Automation.Language.Parser]::ParseFile(
            $fullPath, [ref]$tokens, [ref]$errors
        )
        if ($errors.Count -gt 0) {
            Write-Host ($Prefix + "_POWERSHELL_PARSE_FAILED: " + $relativePath)
            $errors | Format-List
            Fail-Gate 25 ($Prefix + " PowerShell parse failed.")
        }
    }

    $hot = @($changedPaths | Where-Object {
        $_ -match '^(TacticalAI|Tactical|Strategic|TileEngine|Laptop|Utils)/'
    })
    if ($hot.Count -gt 0) {
        Write-Host ($Prefix + "_HOT_ZONE_REVIEW_REQUIRED")
        $hot | ForEach-Object { Write-Host "  $_" }
    }

    $aiTouched = @($changedPaths | Where-Object {
        $_ -match '^(TacticalAI|ModularizedTacticalAI|Tools/AI)/'
    }).Count -gt 0

    if ($aiTouched) {
        $aiAudit = Join-Path $Worktree "Tools\AI\VERIFY_AI_INTEGRITY.ps1"
        if (-not (Test-Path $aiAudit)) {
            Fail-Gate 26 "AI files changed but VERIFY_AI_INTEGRITY.ps1 is missing."
        }

        & $aiAudit -RepositoryRoot $Worktree
        if ($LASTEXITCODE -ne 0) {
            Write-Host ($Prefix + "_AI_INTEGRITY_FAILED")
            Fail-Gate 26 ($Prefix + " AI integrity failed.")
        }

        Write-Host ($Prefix + "_AI_INTEGRITY_OK")
    }

    if ($strategic.Count -gt 0 -and $AllowStrategic) {
        Write-Host ($Prefix + "_STRATEGIC_LAYER_CHANGE_EXPLICITLY_ALLOWED")
    }

    return ,$changedPaths
}

function Invoke-IndividualCandidateGate {
    param(
        [string]$Repository,
        [string]$CanonicalSha,
        [pscustomobject]$Candidate,
        [int]$Index,
        [switch]$AllowStrategic
    )

    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("vr-integration-candidate-" + $PID + "-" + $Index)

    try {
        New-DisposableWorktree -Repository $Repository -Path $tempRoot -BaseSha $CanonicalSha

        Write-Host "CANDIDATE_GATE_START: $($Candidate.Name) @ $($Candidate.Sha)"
        $merge = Invoke-Git @(
            "merge", "--no-ff", "-m",
            "QA individual gate: $($Candidate.Name) @ $($Candidate.Sha)",
            $Candidate.Sha
        ) $tempRoot

        if ($merge.ExitCode -ne 0) {
            Write-Host "CANDIDATE_MERGE_CONFLICT: $($Candidate.Name) @ $($Candidate.Sha)"
            $conflicts = Invoke-Git @("diff", "--name-only", "--diff-filter=U") $tempRoot
            $conflicts.Output | Where-Object { $_ } | ForEach-Object { Write-Host "  $_" }
            Fail-Gate 20 "Individual candidate merge conflict."
        }

        $changedPaths = @(Test-StaticDelta -Worktree $tempRoot -BaselineSha $CanonicalSha -Prefix "CANDIDATE" -AllowStrategic:$AllowStrategic)
        Write-Host "CANDIDATE_STATIC_GATE_OK: $($Candidate.Name) @ $($Candidate.Sha) ($($changedPaths.Count) files)"
    }
    finally {
        Remove-DisposableWorktree -Repository $Repository -Path $tempRoot
    }
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Write-Host "Git: $script:GitExe"
Write-Host "Repository: $repo"

$batchRoot = $null
$finalExitCode = 0

try {
    if ($Fetch) {
        $fetch = Invoke-Git @("fetch", "origin", "--prune") $repo
        if ($fetch.ExitCode -ne 0) {
            Fail-Gate 33 "git fetch failed: $($fetch.Output -join [Environment]::NewLine)"
        }
        Write-Host "FETCH_OK"
    }
    else {
        Write-Host "FETCH_SKIPPED - using currently known refs. Matt performs the final GitHub Desktop fetch/pull."
    }

    $canonical = Invoke-Git @("rev-parse", "--verify", $CanonicalRef) $repo
    if ($canonical.ExitCode -ne 0) {
        Fail-Gate 34 "Canonical ref not found: $CanonicalRef"
    }

    $canonicalSha = ($canonical.Output | Select-Object -First 1).Trim()

    if ($ExpectedCanonicalSha -and $canonicalSha -ne $ExpectedCanonicalSha) {
        Fail-Gate 35 "Canonical moved. Expected $ExpectedCanonicalSha but found $canonicalSha. Re-run the gate."
    }

    Write-Host "Canonical: $CanonicalRef @ $canonicalSha"
    if (-not $ExpectedCanonicalSha) {
        Write-Host "PIN_THIS_CANONICAL_SHA=$canonicalSha"
    }

    $candidates = @()
    $seenShas = @{}

    foreach ($candidateRef in $CandidateRefs) {
        $candidate = Invoke-Git @("rev-parse", "--verify", $candidateRef) $repo
        if ($candidate.ExitCode -ne 0) {
            Fail-Gate 36 "Candidate ref not found: $candidateRef"
        }

        $candidateSha = ($candidate.Output | Select-Object -First 1).Trim()

        if ($seenShas.ContainsKey($candidateSha)) {
            Fail-Gate 37 "Duplicate candidate SHA in batch: $candidateRef @ $candidateSha"
        }
        $seenShas[$candidateSha] = $true

        if ($candidateSha -eq $canonicalSha) {
            Fail-Gate 4 "NO_CANDIDATE_DELTA: $candidateRef"
        }

        $contained = Invoke-Git @("merge-base", "--is-ancestor", $candidateSha, $canonicalSha) $repo
        if ($contained.ExitCode -eq 0) {
            Fail-Gate 4 "Candidate already contained by canonical: $candidateRef @ $candidateSha"
        }

        $forwardPorted = Invoke-Git @("merge-base", "--is-ancestor", $canonicalSha, $candidateSha) $repo
        if ($forwardPorted.ExitCode -ne 0) {
            Fail-Gate 5 "FORWARD_PORT_REQUIRED before integration gate: $candidateRef @ $candidateSha"
        }

        $counts = Invoke-Git @("rev-list", "--left-right", "--count", "$canonicalSha...$candidateSha") $repo
        if ($counts.ExitCode -ne 0) {
            Fail-Gate 38 "Unable to compare refs for $candidateRef."
        }

        $candidates += [pscustomobject]@{
            Name = $candidateRef
            Sha = $candidateSha
            Counts = ($counts.Output -join ' ')
        }
    }

    Write-Host "Batch candidates pinned in order:"
    $candidates | ForEach-Object {
        Write-Host "  $($_.Name) @ $($_.Sha) [left/right: $($_.Counts)]"
    }

    $batchRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("vr-integration-batch-" + $PID)
    New-DisposableWorktree -Repository $repo -Path $batchRoot -BaseSha $canonicalSha

    for ($i = 0; $i -lt $candidates.Count; $i++) {
        $candidate = $candidates[$i]
        $itemNumber = $i + 1

        Write-Host "BATCH_ITEM_START $itemNumber/$($candidates.Count): $($candidate.Name) @ $($candidate.Sha)"

        Invoke-IndividualCandidateGate -Repository $repo -CanonicalSha $canonicalSha -Candidate $candidate -Index $itemNumber -AllowStrategic:$AllowStrategicChanges

        $before = Invoke-Git @("rev-parse", "HEAD") $batchRoot
        $beforeSha = ($before.Output | Select-Object -First 1).Trim()

        Write-Host "BATCH_MERGING $itemNumber/$($candidates.Count): $($candidate.Name) @ $($candidate.Sha)"
        $merge = Invoke-Git @(
            "merge", "--no-ff", "-m",
            "QA temporary merge: $($candidate.Name) @ $($candidate.Sha)",
            $candidate.Sha
        ) $batchRoot

        if ($merge.ExitCode -ne 0) {
            Write-Host "BATCH_MERGE_CONFLICT: $($candidate.Name) @ $($candidate.Sha)"
            $conflicts = Invoke-Git @("diff", "--name-only", "--diff-filter=U") $batchRoot
            $conflicts.Output | Where-Object { $_ } | ForEach-Object { Write-Host "  $_" }
            Fail-Gate 20 "Batch merge conflict."
        }

        $delta = Invoke-Git @("diff", "--name-only", "$beforeSha..HEAD") $batchRoot
        $deltaPaths = @($delta.Output | Where-Object { $_ })
        Write-Host "BATCH_ITEM_OK $itemNumber/$($candidates.Count): $($candidate.Name) @ $($candidate.Sha) ($($deltaPaths.Count) files)"

        if ($i -lt ($candidates.Count - 1) -and $InterItemDelaySeconds -gt 0) {
            Write-Host "BATCH_INTER_ITEM_DELAY: waiting $InterItemDelaySeconds seconds before next item."
            Start-Sleep -Seconds $InterItemDelaySeconds
        }
    }

    $combinedPaths = @(Test-StaticDelta -Worktree $batchRoot -BaselineSha $canonicalSha -Prefix "BATCH" -AllowStrategic:$AllowStrategicChanges)

    Write-Host "BATCH_STATIC_GATE_OK"
    Write-Host "Combined changed files: $($combinedPaths.Count)"
    Write-Host "BUILD_STILL_REQUIRED_BEFORE_PLAYTEST_READY"
}
catch {
    Write-Host "INTEGRATION_GATE_FAILED"
    Write-Host $_.Exception.Message
    $finalExitCode = $script:GateExitCode
}
finally {
    if ($batchRoot) {
        Remove-DisposableWorktree -Repository $repo -Path $batchRoot
    }
}

exit $finalExitCode
