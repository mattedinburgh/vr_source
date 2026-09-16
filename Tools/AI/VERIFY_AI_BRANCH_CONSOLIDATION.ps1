param(
    [string]$Canonical = "origin/install/all-2026-09-12"
)

$ErrorActionPreference = "Stop"

$RegistryPath = Join-Path $PSScriptRoot "AI_BRANCH_REGISTRY.json"

function Test-Ancestor([string]$Older, [string]$Newer) {
    & git merge-base --is-ancestor $Older $Newer 2>$null
    return ($LASTEXITCODE -eq 0)
}

function Get-CommitSha([string]$Ref) {
    # Missing archived refs are expected after branch cleanup. On some PowerShell
    # hosts native stderr becomes a terminating error when ErrorActionPreference is
    # Stop, so probe under Continue and interpret Git's exit code explicitly.
    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $value = & git rev-parse --verify $Ref 2>$null
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if ($exitCode -ne 0) {
        return $null
    }
    return ($value | Select-Object -First 1).Trim()
}

Write-Host "AI consolidation audit"
Write-Host "Canonical: $Canonical"

$canonicalSha = Get-CommitSha $Canonical
if (-not $canonicalSha) {
    throw "Canonical ref '$Canonical' is unavailable. Fetch origin with full history before running this audit."
}

if (-not (Test-Path $RegistryPath)) {
    throw "AI branch registry is missing: $RegistryPath"
}

$registry = Get-Content $RegistryPath -Raw | ConvertFrom-Json
if ($registry.canonical_branch -ne "install/all-2026-09-12") {
    throw "AI branch registry canonical branch does not match policy."
}

$registryByName = @{}
foreach ($entry in $registry.branches) {
    if ($registryByName.ContainsKey($entry.name)) {
        throw "Duplicate branch entry in AI registry: $($entry.name)"
    }
    $registryByName[$entry.name] = $entry
}

$failed = $false

Write-Host ""
Write-Host "Frozen historical AI branches"

foreach ($entry in $registry.branches) {
    $branch = [string]$entry.name
    $ref = "origin/$branch"
    $actualSha = Get-CommitSha $ref

    if (-not $actualSha) {
        # Deleting an obsolete archive branch is allowed and improves repository hygiene.
        Write-Host ("ABSENT     {0} [{1}]" -f $branch, $entry.classification)
        continue
    }

    if ($actualSha -ne [string]$entry.pinned_sha) {
        Write-Warning ("ARCHIVE MOVED {0}: pinned={1} actual={2}" -f $branch, $entry.pinned_sha, $actualSha)
        $failed = $true
        continue
    }

    Write-Host ("FROZEN     {0} [{1}]" -f $branch, $entry.classification)
}

Write-Host ""
Write-Host "Searching for unregistered AI integration lines"

$allRefs = @(
    & git for-each-ref --format="%(refname:short)" refs/remotes/origin/
) | Where-Object { $_ -and $_ -notmatch '/HEAD$' } | Sort-Object -Unique

# Branch names that imply AI architecture, tactical doctrine, Companion/Black Box integration,
# CQB or strategic-AI work. Non-AI feature branches are deliberately outside this audit.
$aiBranchPattern = '(?i)(^ai/|ai-|/ai|cqb|companion|strategic|consolidation/install-all)'

foreach ($ref in $allRefs) {
    $branch = $ref -replace '^origin/', ''

    if ($branch -eq "install/all-2026-09-12") {
        continue
    }

    if ($registryByName.ContainsKey($branch)) {
        continue
    }

    if ($branch -notmatch $aiBranchPattern) {
        continue
    }

    if (Test-Ancestor $ref $Canonical) {
        Write-Host ("INTEGRATED {0}" -f $branch)
        continue
    }

    Write-Warning ("UNREGISTERED AI FRAGMENT: {0}" -f $branch)
    $failed = $true
}

Write-Host ""
if ($failed) {
    Write-Host "FAIL: AI branch drift or an unregistered integration line was detected."
    Write-Host "Forward-port unique work into install/all-2026-09-12, then either delete the old branch or deliberately update AI_BRANCH_REGISTRY.json."
    exit 1
}

Write-Host "PASS: historical AI branches are frozen and no unregistered AI integration line exists."
exit 0
