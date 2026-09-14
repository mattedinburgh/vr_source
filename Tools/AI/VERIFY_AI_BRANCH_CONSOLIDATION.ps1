param(
    [string]$Canonical = "origin/install/all-2026-09-12"
)

$ErrorActionPreference = "Stop"

$ArchivedBranches = @(
    "ai/ap-budgeting",
    "ai/combat-dispersion",
    "ai/combat-medic-rescue",
    "ai/covering-fire-cooperation",
    "ai/deidranna-doctrine",
    "ai/emergency-casualty-smoke",
    "ai/fireteam-cohesion",
    "ai/human-tactical-final",
    "ai/individual-self-preservation",
    "ai/legacy-core-modernization",
    "ai/local-advance-cooperation",
    "ai/no-weapon-self-preservation",
    "ai/radio-support-doctrine",
    "ai/range-aware-positioning",
    "ai/search-confidence-decay",
    "ai/shared-enemy-militia-brain",
    "ai/support-aware-withdrawal",
    "ai/target-allocation",
    "ai/team-coordination",
    "ai/utility-squad-planner",
    "ai/wound-self-preservation",
    "ai/wounded-tactical-withdrawal",
    "final-human-ai-modern-113",
    "integration/unified-ai-fireteams-doctrine-2026-09-14",
    "integration/unified-ai-framework-2026-09-14",
    "integration/unified-strategic-companion-2026-09-14",
    "consolidation/install-all-2026-09-14",
    "inactive/strategic-modernization"
)

function Test-Ancestor([string]$Older, [string]$Newer) {
    & git merge-base --is-ancestor $Older $Newer 2>$null
    return ($LASTEXITCODE -eq 0)
}

Write-Host "AI consolidation audit"
Write-Host "Canonical: $Canonical"

& git rev-parse --verify $Canonical *> $null
if ($LASTEXITCODE -ne 0) {
    throw "Canonical ref '$Canonical' is unavailable. Fetch origin with full history before running this audit."
}

$refs = @(& git for-each-ref --format="%(refname:short)" refs/remotes/origin/ai/ refs/remotes/origin/integration/ refs/remotes/origin/consolidation/ refs/remotes/origin/inactive/ refs/remotes/origin/final-human-ai-modern-113) |
    Where-Object { $_ -and $_ -notmatch '/HEAD$' } |
    Sort-Object -Unique

$fragments = @()

foreach ($ref in $refs) {
    $branch = $ref -replace '^origin/', ''

    if ($ArchivedBranches -contains $branch) {
        Write-Host ("ARCHIVE    {0}" -f $branch)
        continue
    }

    if (Test-Ancestor $ref $Canonical) {
        Write-Host ("INTEGRATED {0}" -f $branch)
        continue
    }

    if (Test-Ancestor $Canonical $ref) {
        $state = "AHEAD"
    }
    else {
        $state = "DIVERGED"
    }

    Write-Warning ("FRAGMENT {0,-8} {1}" -f $state, $branch)
    $fragments += $branch
}

if ($fragments.Count -gt 0) {
    Write-Host ""
    Write-Host "Unclassified AI fragmentation detected:"
    $fragments | ForEach-Object { Write-Host " - $_" }
    Write-Host ""
    Write-Host "Integrate the unique behaviour into install/all-2026-09-12 or classify the branch as archaeology in UNIFIED_AI_FRAMEWORK.md and this audit."
    exit 1
}

Write-Host ""
Write-Host "PASS: no unclassified AI integration branch exists outside $Canonical."
exit 0
