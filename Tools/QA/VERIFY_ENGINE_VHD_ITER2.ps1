param(
    [string]$CandidateRef = "HEAD",
    [string]$CanonicalRef = "origin/install/all-2026-09-12"
)

$ErrorActionPreference = "Stop"

function Invoke-Git {
    param([string[]]$Arguments)
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & git -C $script:Repo @Arguments 2>&1
        $exit = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $saved
    }
    [pscustomobject]@{ Output = @($output); ExitCode = $exit }
}

function Require-Contains {
    param(
        [string]$Path,
        [string]$Needle,
        [string]$Label
    )
    $full = Join-Path $script:Repo $Path
    if (-not (Test-Path -LiteralPath $full)) {
        throw "Missing required file: $Path"
    }
    $text = Get-Content -LiteralPath $full -Raw
    if ($text.IndexOf($Needle, [System.StringComparison]::Ordinal) -lt 0) {
        throw "Invariant failed [$Label]: '$Needle' not found in $Path"
    }
    Write-Host "PASS [$Label]"
}

$script:Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

Write-Host "=== VHD Iteration 2 validation-only gate ==="
Write-Host "Repo: $script:Repo"

$fetch = Invoke-Git @("fetch", "origin", "install/all-2026-09-12", "--no-tags")
if ($fetch.ExitCode -ne 0) {
    throw "Could not refresh canonical branch: $($fetch.Output -join [Environment]::NewLine)"
}

$canonical = Invoke-Git @("rev-parse", "--verify", $CanonicalRef)
if ($canonical.ExitCode -ne 0) { throw "Canonical ref not found: $CanonicalRef" }
$canonicalSha = ($canonical.Output | Select-Object -First 1).Trim()

$candidate = Invoke-Git @("rev-parse", "--verify", $CandidateRef)
if ($candidate.ExitCode -ne 0) { throw "Candidate ref not found: $CandidateRef" }
$candidateSha = ($candidate.Output | Select-Object -First 1).Trim()

Write-Host "Canonical: $CanonicalRef @ $canonicalSha"
Write-Host "Candidate: $CandidateRef @ $candidateSha"

$ancestor = Invoke-Git @("merge-base", "--is-ancestor", $canonicalSha, $candidateSha)
if ($ancestor.ExitCode -ne 0) {
    throw "Candidate is not a descendant of the current canonical 2026-09-12 integration branch."
}
Write-Host "PASS [canonical ancestry]"

$diffCheck = Invoke-Git @("diff", "--check", "$canonicalSha...$candidateSha")
if ($diffCheck.ExitCode -ne 0) {
    throw "git diff --check failed: $($diffCheck.Output -join [Environment]::NewLine)"
}
Write-Host "PASS [diff whitespace]"

$changedResult = Invoke-Git @("diff", "--name-only", "$canonicalSha...$candidateSha")
if ($changedResult.ExitCode -ne 0) { throw "Could not enumerate Iteration 2 changes." }

$changed = @($changedResult.Output | Where-Object { $_ })
$allowed = @(
    ".github/workflows/vhd-iter2-validation.yml",
    "Tools/QA/VERIFY_ENGINE_VHD_ITER2.ps1",
    "docs/ENGINE_VHD_MODERNIZATION_2026.md"
)

$unexpected = @($changed | Where-Object { $allowed -notcontains $_ })
if ($unexpected.Count -gt 0) {
    Write-Host "Unexpected Iteration 2 paths:"
    $unexpected | ForEach-Object { Write-Host "  $_" }
    throw "Iteration 2 is validation-only. Runtime/source/data changes are not permitted on this branch."
}
Write-Host "PASS [validation-only change boundary]"

Require-Contains "Init.cpp" "RunVHDTileCacheSelfTest" "tile-cache startup self-test"
Require-Contains "Init.cpp" "RunVHDOcclusionMaskParitySelfTest" "occlusion startup self-test"

Require-Contains "TileEngine\Tile Cache.cpp" "uiDefaultBudgetMB = 128u" "128 MB default tile-cache budget"
Require-Contains "TileEngine\Tile Cache.cpp" "uiMinBudgetMB = 16u" "16 MB minimum tile-cache budget"
Require-Contains "TileEngine\Tile Cache.cpp" "uiMaxBudgetMB = 512u" "512 MB maximum tile-cache budget"
Require-Contains "TileEngine\Tile Cache.cpp" "FindLRUUnreferencedTile" "LRU eviction path"
Require-Contains "TileEngine\Tile Cache.cpp" "guiTileCacheResidentBytes" "resident-byte accounting"

Require-Contains "TileEngine\renderworld.cpp" "RunVHDOcclusionMaskParitySelfTest" "occlusion parity implementation"
Require-Contains "TileEngine\renderworld.cpp" "VR_VHD_RENDER_DIAGNOSTICS" "renderer diagnostics switch"

Require-Contains ".github\scripts\vhd-runtime-smoke.ps1" "vhd-native-contract-selftest.ok" "native-contract runtime marker"
Require-Contains ".github\scripts\vhd-runtime-smoke.ps1" "vhd-tile-cache-selftest.ok" "tile-cache runtime marker"
Require-Contains ".github\scripts\vhd-runtime-smoke.ps1" "vhd-occlusion-mask-parity-selftest.ok" "occlusion runtime marker"

$worlddefPath = Join-Path $script:Repo "TileEngine\worlddef.cpp"
$worlddef = Get-Content -LiteralPath $worlddefPath -Raw
$deletePos = $worlddef.IndexOf("DeleteTileCache();", [System.StringComparison]::Ordinal)
$freePos = $worlddef.IndexOf("FreeAllStructureFiles();", [System.StringComparison]::Ordinal)
if ($deletePos -lt 0 -or $freePos -lt 0 -or $deletePos -gt $freePos) {
    throw "Invariant failed [tile-cache shutdown order]: DeleteTileCache() must occur before FreeAllStructureFiles()."
}
Write-Host "PASS [tile-cache shutdown order]"

Write-Host ""
Write-Host "VHD Iteration 2 static gate passed."
Write-Host "No renderer/gameplay semantics were changed by this iteration."
