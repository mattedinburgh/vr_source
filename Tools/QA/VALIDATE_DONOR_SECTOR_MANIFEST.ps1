param(
    [Parameter(Mandatory = $true)]
    [string]$ManifestPath,
    [switch]$AllowUnresolved
)

$ErrorActionPreference = "Stop"

$requiredChecks = @(
    "map.world_items",
    "map.soldier_placements",
    "map.schedules",
    "map.exit_grids",
    "map.doors_locks_keys",
    "map.room_numbers",
    "map.entry_points_edgepoints",
    "rpg.npc_rpc_locations",
    "rpg.dialogue_grid_refs",
    "rpg.quest_facts_actions",
    "external.sector_grid_room_refs",
    "world.alt_sectors",
    "world.underground_links",
    "items.translation",
    "assets.tileset_sti_jsd",
    "provenance.source_and_rights"
)
$requiredTests = @(
    "regression.quest_npc",
    "regression.items",
    "regression.entry_exit",
    "regression.save_load"
)

$allowedCheckStatuses = @(
    "verified_unchanged",
    "migrated",
    "rebased",
    "translated",
    "not_applicable"
)

if (-not (Test-Path $ManifestPath)) {
    throw "Donor manifest not found: $ManifestPath"
}

try {
    $manifest = Get-Content -Raw -Path $ManifestPath | ConvertFrom-Json
}
catch {
    throw "Invalid donor manifest JSON: $($_.Exception.Message)"
}
$errors = New-Object System.Collections.Generic.List[string]

function Add-Error {
    param([string]$Message)
    $errors.Add($Message)
}

function Require-Text {
    param([object]$Object, [string]$PropertyName)
    $property = $Object.PSObject.Properties[$PropertyName]
    if (-not $property -or [string]::IsNullOrWhiteSpace([string]$property.Value)) {
        Add-Error "Missing required text field: $PropertyName"
        return ""
    }
    return ([string]$property.Value).Trim()
}

$schemaVersion = Require-Text $manifest "schema_version"
$targetSector = Require-Text $manifest "target_sector"
$canonicalRef = Require-Text $manifest "canonical_ref"
$canonicalSha = Require-Text $manifest "canonical_sha"
$riskClass = Require-Text $manifest "risk_class"

if ($schemaVersion -ne "1") {
    Add-Error "Unsupported schema_version '$schemaVersion'. Expected 1."
}
if ($canonicalRef -notin @("install/all-2026-09-12", "origin/install/all-2026-09-12")) {
    Add-Error "canonical_ref must resolve to install/all-2026-09-12."
}

if ($canonicalSha -notmatch '^[0-9a-fA-F]{40}$') {
    Add-Error "canonical_sha must be a full 40-character Git SHA."
}

if ($riskClass -notin @("Low", "Medium", "High", "Protected")) {
    Add-Error "risk_class must be Low, Medium, High, or Protected."
}

$donorProperty = $manifest.PSObject.Properties["donor"]
if (-not $donorProperty -or -not $donorProperty.Value) {
    Add-Error "Missing donor object."
}
else {
    [void](Require-Text $manifest.donor "mod")
    [void](Require-Text $manifest.donor "version")
    [void](Require-Text $manifest.donor "sector_or_file")
}

$checksProperty = $manifest.PSObject.Properties["checks"]
$checks = if ($checksProperty) { @($checksProperty.Value) } else { @() }
if ($checks.Count -eq 0) {
    Add-Error "Manifest has no dependency checks."
}
$checksById = @{}
foreach ($check in $checks) {
    $id = Require-Text $check "id"
    $status = Require-Text $check "status"
    if (-not $id) { continue }

    if ($checksById.ContainsKey($id)) {
        Add-Error "Duplicate dependency check id: $id"
        continue
    }
    $checksById[$id] = $check

    if ($status -notin $allowedCheckStatuses) {
        Add-Error "Invalid status '$status' for dependency check '$id'."
    }

    $evidenceProperty = $check.PSObject.Properties["evidence"]
    $evidence = if ($evidenceProperty) { @($evidenceProperty.Value) } else { @() }
    $evidence = @($evidence | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) })
    if ($evidence.Count -eq 0) {
        Add-Error "Dependency check '$id' has no evidence."
    }

    if ($status -eq "not_applicable") {
        $rationale = Require-Text $check "rationale"
        if (-not $rationale) {
            Add-Error "Dependency check '$id' is not_applicable without rationale."
        }
    }
}
foreach ($required in $requiredChecks) {
    if (-not $checksById.ContainsKey($required)) {
        Add-Error "Missing required dependency check: $required"
    }
}

$unresolvedProperty = $manifest.PSObject.Properties["unresolved"]
$unresolved = if ($unresolvedProperty) { @($unresolvedProperty.Value) } else { @() }
if (-not $unresolvedProperty) {
    Add-Error "Manifest must include an unresolved array, even when empty."
}
elseif ($unresolved.Count -gt 0 -and -not $AllowUnresolved) {
    Add-Error "Manifest contains $($unresolved.Count) unresolved dependency item(s)."
}

$testsProperty = $manifest.PSObject.Properties["tests"]
$tests = if ($testsProperty) { @($testsProperty.Value) } else { @() }
$testsById = @{}
foreach ($test in $tests) {
    $id = Require-Text $test "id"
    $status = Require-Text $test "status"
    if (-not $id) { continue }

    if ($testsById.ContainsKey($id)) {
        Add-Error "Duplicate regression test id: $id"
        continue
    }
    $testsById[$id] = $test
    if ($status -notin @("passed", "failed", "not_run", "not_applicable")) {
        Add-Error "Invalid regression test status '$status' for '$id'."
    }

    $evidenceProperty = $test.PSObject.Properties["evidence"]
    $evidence = if ($evidenceProperty) { @($evidenceProperty.Value) } else { @() }
    $evidence = @($evidence | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) })
    if ($status -eq "passed" -and $evidence.Count -eq 0) {
        Add-Error "Passed regression test '$id' has no evidence."
    }
}

foreach ($required in $requiredTests) {
    if (-not $testsById.ContainsKey($required)) {
        Add-Error "Missing required regression test: $required"
    }
    elseif ([string]$testsById[$required].status -ne "passed") {
        Add-Error "Required regression test '$required' is not passed."
    }
}

if ($riskClass -in @("High", "Protected")) {
    $reviewProperty = $manifest.PSObject.Properties["manual_review"]
    if (-not $reviewProperty -or -not $reviewProperty.Value) {
        Add-Error "$riskClass risk sector requires manual_review."
    }
    elseif ($manifest.manual_review.approved -ne $true) {
        Add-Error "$riskClass risk sector requires manual_review.approved=true."
    }
}
Write-Host "Donor sector manifest QA"
Write-Host "Target sector: $targetSector"
Write-Host "Risk class:    $riskClass"
Write-Host "Canonical:     $canonicalRef @ $canonicalSha"
Write-Host "Checks:        $($checks.Count)"
Write-Host "Tests:         $($tests.Count)"
Write-Host "Unresolved:    $($unresolved.Count)"
if ($errors.Count -gt 0) {
    Write-Host ""
    Write-Host "DONOR_SECTOR_MANIFEST_QA_FAILED"
    $errors | ForEach-Object { Write-Host "  ERROR: $_" }
    exit 51
}

Write-Host "DONOR_SECTOR_MANIFEST_QA_OK"
exit 0
