param(
    [Parameter(Mandatory = $true)]
    [string]$CandidateRef,
    [string]$CanonicalRef = "origin/install/all-2026-09-12",
    [string]$ExpectedCanonicalSha = "",
    [switch]$Fetch,
    [switch]$AllowStrategicChanges,
    [string[]]$AllowedStrategicPaths = @()
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
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($desktopGit) { return $desktopGit.FullName }

    throw "Git executable not found. Install Git or GitHub Desktop."
}
$script:GitExe = Resolve-GitExecutable

function Invoke-Git {
    param([string[]]$Arguments, [string]$WorkingDirectory = $PSScriptRoot)
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
    Write-Host "FETCH_SKIPPED - using currently known refs. Matt performs the final GitHub Desktop fetch/pull."
}
$canonical = Invoke-Git @("rev-parse", "--verify", $CanonicalRef) $repo
if ($canonical.ExitCode -ne 0) { throw "Canonical ref not found: $CanonicalRef" }
$canonicalSha = ($canonical.Output | Select-Object -First 1).Trim()

if ($ExpectedCanonicalSha -and $canonicalSha -ne $ExpectedCanonicalSha) {
    throw "Canonical moved. Expected $ExpectedCanonicalSha but found $canonicalSha. Re-run the gate."
}

$candidate = Invoke-Git @("rev-parse", "--verify", $CandidateRef) $repo
if ($candidate.ExitCode -ne 0) { throw "Candidate ref not found: $CandidateRef" }
$candidateSha = ($candidate.Output | Select-Object -First 1).Trim()

Write-Host "Canonical: $CanonicalRef @ $canonicalSha"
Write-Host "Candidate: $CandidateRef @ $candidateSha"
if (-not $ExpectedCanonicalSha) {
    Write-Host "PIN_THIS_CANONICAL_SHA=$canonicalSha"
}

$counts = Invoke-Git @("rev-list", "--left-right", "--count", "$canonicalSha...$candidateSha") $repo
if ($counts.ExitCode -ne 0) { throw "Unable to compare refs." }
Write-Host "Left/right unique commits: $($counts.Output -join ' ')"

if ($candidateSha -eq $canonicalSha) {
    Write-Host "NO_CANDIDATE_DELTA"
    exit 4
}
$alreadyIntegrated = Invoke-Git @("merge-base", "--is-ancestor", $candidateSha, $canonicalSha) $repo
if ($alreadyIntegrated.ExitCode -eq 0) {
    Write-Host "CANDIDATE_ALREADY_CONTAINED_IN_CANONICAL"
    exit 4
}

$forwardPorted = Invoke-Git @("merge-base", "--is-ancestor", $canonicalSha, $candidateSha) $repo
if ($forwardPorted.ExitCode -ne 0) {
    Write-Host "FORWARD_PORT_REQUIRED"
    Write-Host "Candidate is not based on the pinned canonical tip. Rebase/merge the current canonical baseline into the stream first."
    exit 5
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("vr-integration-gate-" + $PID)
if (Test-Path $tempRoot) { Remove-Item -Recurse -Force $tempRoot }

$add = Invoke-Git @("worktree", "add", "--detach", $tempRoot, $canonicalSha) $repo
if ($add.ExitCode -ne 0) {
    throw "Unable to create disposable worktree: $($add.Output -join [Environment]::NewLine)"
}

try {
    $merge = Invoke-Git @("merge", "--no-commit", "--no-ff", $candidateSha) $tempRoot
    $conflicts = Invoke-Git @("diff", "--name-only", "--diff-filter=U") $tempRoot
    $conflictPaths = @($conflicts.Output | Where-Object { $_ -and $_ -notmatch '^warning:' })

    if ($merge.ExitCode -ne 0) {
        Write-Host "MERGE_CONFLICT"
        $conflictPaths | ForEach-Object { Write-Host "  $_" }
        exit 2
    }
    $check = Invoke-Git @("-c", "core.whitespace=cr-at-eol", "diff", "--check", $canonicalSha) $tempRoot
    if ($check.ExitCode -ne 0) {
        Write-Host "DIFF_CHECK_FAILED"
        $check.Output | ForEach-Object { Write-Host $_ }
        exit 3
    }

    $changed = Invoke-Git @("diff", "--name-only", $canonicalSha) $tempRoot
    if ($changed.ExitCode -ne 0) { throw "Unable to enumerate changed files." }
    $changedPaths = @($changed.Output | Where-Object { $_ })

    Write-Host "MERGE_CLEAN"
    Write-Host "Changed files: $($changedPaths.Count)"
    $changedPaths | ForEach-Object { Write-Host "  $_" }

    $noise = @($changedPaths | Where-Object {
        $_ -match '(^|/)(bin|build|Debug|Release|ipch|\.vs)(/|$)' -or
        $_ -match '(\.vcxproj\.user|\.suo|\.obj|\.pdb|\.ilk|\.tlog|\.log)$'
    })
    if ($noise.Count -gt 0) {
        Write-Host "GENERATED_OR_IDE_NOISE_BLOCKED"
        $noise | ForEach-Object { Write-Host "  $_" }
        exit 6
    }

    $strategic = @($changedPaths | Where-Object { $_ -match '^Strategic/' })
    if ($AllowStrategicChanges -and $AllowedStrategicPaths.Count -gt 0) {
        throw "Use either -AllowStrategicChanges or -AllowedStrategicPaths, not both."
    }

    if ($strategic.Count -gt 0 -and -not $AllowStrategicChanges) {
        $normalizedAllowlist = @($AllowedStrategicPaths | ForEach-Object { $_.Replace('\', '/').TrimStart('.', '/') } | Where-Object { $_ })
        $unexpectedStrategic = @($strategic | Where-Object { $normalizedAllowlist -notcontains $_ })
        if ($unexpectedStrategic.Count -gt 0) {
            Write-Host "STRATEGIC_LAYER_CHANGE_BLOCKED"
            $unexpectedStrategic | ForEach-Object { Write-Host "  $_" }
            if ($normalizedAllowlist.Count -gt 0) {
                Write-Host "Strategic allowlist:"
                $normalizedAllowlist | ForEach-Object { Write-Host "  $_" }
            }
            exit 7
        }
        Write-Host "STRATEGIC_LAYER_CHANGE_ALLOWLIST_OK"
        $strategic | ForEach-Object { Write-Host "  $_" }
    }
    $markerArgs = @("grep", "-n", "-E", "^(<<<<<<< .+|=======|>>>>>>> .+)$", "--") + $changedPaths
    if ($changedPaths.Count -gt 0) {
        $markers = Invoke-Git $markerArgs $tempRoot
        if ($markers.ExitCode -eq 0) {
            Write-Host "MERGE_MARKERS_BLOCKED"
            $markers.Output | ForEach-Object { Write-Host $_ }
            exit 8
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
            Write-Host "POWERSHELL_PARSE_FAILED: $relativePath"
            $errors | Format-List
            exit 9
        }
    }

    $hot = @($changedPaths | Where-Object {
        $_ -match '^(TacticalAI|Tactical|Strategic|TileEngine|Laptop|Utils)/'
    })
    if ($hot.Count -gt 0) {
        Write-Host "HOT_ZONE_REVIEW_REQUIRED"
        $hot | ForEach-Object { Write-Host "  $_" }
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
            Write-Host "AI_INTEGRITY_FAILED"
            exit 10
        }
        Write-Host "AI_INTEGRITY_OK"
    }

    if ($strategic.Count -gt 0 -and $AllowStrategicChanges) {
        Write-Host "STRATEGIC_LAYER_CHANGE_EXPLICITLY_ALLOWED_ALL"
    }

    Write-Host "PREMERGE_STATIC_GATE_OK"
    Write-Host "BUILD_STILL_REQUIRED_BEFORE_PLAYTEST_READY"
    exit 0
}
finally {
    & $script:GitExe -C $repo worktree remove --force $tempRoot 2>$null | Out-Null
}
