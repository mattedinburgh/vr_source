$ErrorActionPreference = "Stop"

$childPowerShell = (Get-Command powershell.exe -ErrorAction Stop).Source
$perfScript = Join-Path $PSScriptRoot "COMPARE_AI_PERFORMANCE.ps1"
$donorScript = Join-Path $PSScriptRoot "VALIDATE_DONOR_SECTOR_MANIFEST.ps1"
$candidateScript = Join-Path $PSScriptRoot "TEST_INTEGRATION_CANDIDATE.ps1"
$batchScript = Join-Path $PSScriptRoot "TEST_INTEGRATION_BATCH.ps1"
$conflictScript = Join-Path $PSScriptRoot "CHECK_INTEGRATION_CONFLICTS.ps1"

foreach ($requiredScript in @($perfScript, $donorScript, $candidateScript, $batchScript, $conflictScript)) {
    if (-not (Test-Path $requiredScript)) {
        throw "Required QA tool missing: $requiredScript"
    }
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("vr-qa-tool-selftest-" + $PID)
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null

function New-AIPerfLine {
    param(
        [int]$TotalMs,
        [int]$PathMs,
        [int]$PathSearches,
        [int]$CacheHits,
        [int]$CacheMisses
    )
    return "x [AI-PERF] total_ms=$TotalMs threat_build_ms=5 pathfinding_ms=$PathMs exposure_ms=10 reaction_ms=5 geometry_ms=5 contacts=2 detailed_candidates=8 path_searches=$PathSearches path_reuses=2 route_hits=6 route_misses=2 budget_earlyouts=0 exposure_calls=4 reaction_calls=3 cache_hits=$CacheHits cache_misses=$CacheMisses shared_contact_hits=1 shared_contact_misses=0 geometry_hits=2 geometry_misses=1 lookahead_nodes=2"
}
function Assert-ExitCode {
    param(
        [string]$Name,
        [int]$Expected,
        [int]$Actual
    )
    if ($Actual -ne $Expected) {
        throw "$Name expected exit $Expected but received $Actual"
    }
    Write-Host "SELFTEST_OK $Name exit=$Actual"
}

try {
    $baselineLog = Join-Path $tempRoot "perf-baseline.log"
    $goodLog = Join-Path $tempRoot "perf-good.log"
    $badLog = Join-Path $tempRoot "perf-bad.log"

    @(
        (New-AIPerfLine 100 40 8 8 2),
        (New-AIPerfLine 120 45 9 9 1)
    ) | Set-Content -Path $baselineLog -Encoding UTF8

    @(
        (New-AIPerfLine 105 41 8 8 2),
        (New-AIPerfLine 125 47 10 9 1)
    ) | Set-Content -Path $goodLog -Encoding UTF8

    @(
        (New-AIPerfLine 240 100 20 2 8),
        (New-AIPerfLine 1200 500 30 2 8)
    ) | Set-Content -Path $badLog -Encoding UTF8
    & $childPowerShell -NoProfile -ExecutionPolicy Bypass -File $perfScript -BaselineLog $baselineLog -CandidateLog $goodLog -FailOnRegression *> $null
    Assert-ExitCode "performance-good" 0 $LASTEXITCODE

    & $childPowerShell -NoProfile -ExecutionPolicy Bypass -File $perfScript -BaselineLog $baselineLog -CandidateLog $badLog -FailOnRegression *> $null
    Assert-ExitCode "performance-bad" 41 $LASTEXITCODE

    $checkIds = @(
        "map.world_items", "map.soldier_placements", "map.schedules",
        "map.exit_grids", "map.doors_locks_keys", "map.room_numbers",
        "map.entry_points_edgepoints", "rpg.npc_rpc_locations",
        "rpg.dialogue_grid_refs", "rpg.quest_facts_actions",
        "external.sector_grid_room_refs", "world.alt_sectors",
        "world.underground_links", "items.translation",
        "assets.tileset_sti_jsd", "provenance.source_and_rights"
    )
    $testIds = @(
        "regression.quest_npc", "regression.items",
        "regression.entry_exit", "regression.save_load"
    )

    $checks = @($checkIds | ForEach-Object {
        [pscustomobject]@{ id = $_; status = "verified_unchanged"; evidence = @("self-test") }
    })
    $tests = @($testIds | ForEach-Object {
        [pscustomobject]@{ id = $_; status = "passed"; evidence = @("self-test") }
    })
    $manifest = [pscustomobject]@{
        schema_version = "1"
        target_sector = "A1"
        canonical_ref = "origin/install/all-2026-09-12"
        canonical_sha = "0123456789abcdef0123456789abcdef01234567"
        risk_class = "Low"
        donor = [pscustomobject]@{
            mod = "REDUX 3"
            version = "self-test"
            sector_or_file = "A1.dat"
        }
        checks = $checks
        unresolved = @()
        tests = $tests
    }

    $validManifest = Join-Path $tempRoot "donor-valid.json"
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -Path $validManifest -Encoding UTF8
    & $childPowerShell -NoProfile -ExecutionPolicy Bypass -File $donorScript -ManifestPath $validManifest *> $null
    Assert-ExitCode "donor-valid" 0 $LASTEXITCODE

    $manifest.risk_class = "Protected"
    $manifest.checks = @($manifest.checks | Where-Object { $_.id -ne "rpg.dialogue_grid_refs" })
    $manifest.unresolved = @("unrebased test grid")
    $manifest.tests[0].status = "not_run"

    $invalidManifest = Join-Path $tempRoot "donor-invalid.json"
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -Path $invalidManifest -Encoding UTF8
    & $childPowerShell -NoProfile -ExecutionPolicy Bypass -File $donorScript -ManifestPath $invalidManifest *> $null
    Assert-ExitCode "donor-invalid" 51 $LASTEXITCODE

    $policyRepo = Join-Path $tempRoot "candidate-policy-repo"
    New-Item -ItemType Directory -Force -Path (Join-Path $policyRepo "Tools\QA") | Out-Null
    Copy-Item -Path $candidateScript -Destination (Join-Path $policyRepo "Tools\QA\TEST_INTEGRATION_CANDIDATE.ps1")
    Copy-Item -Path $batchScript -Destination (Join-Path $policyRepo "Tools\QA\TEST_INTEGRATION_BATCH.ps1")
    Copy-Item -Path $conflictScript -Destination (Join-Path $policyRepo "Tools\QA\CHECK_INTEGRATION_CONFLICTS.ps1")

    $gitExe = (Get-Command git.exe -ErrorAction SilentlyContinue).Source
    if (-not $gitExe) {
        $desktopPattern = Join-Path $env:LOCALAPPDATA "GitHubDesktop\app-*\resources\app\git\cmd\git.exe"
        $gitExe = (Get-Item $desktopPattern -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1).FullName
    }
    if (-not $gitExe) { throw "Git executable not found for candidate-policy self-test." }

    & $gitExe -C $policyRepo init -q
    if ($LASTEXITCODE -ne 0) { throw "Unable to initialize candidate-policy fixture repository." }
    & $gitExe -C $policyRepo config user.email "qa-selftest@local.invalid"
    & $gitExe -C $policyRepo config user.name "VR QA Self Test"
    "baseline" | Set-Content -Path (Join-Path $policyRepo "README.md") -Encoding ASCII
    New-Item -ItemType Directory -Force -Path (Join-Path $policyRepo "Strategic") | Out-Null
    "base" | Set-Content -Path (Join-Path $policyRepo "Strategic\Allowed.cpp") -Encoding ASCII
    & $gitExe -C $policyRepo add .
    & $gitExe -C $policyRepo commit -q -m "baseline"
    if ($LASTEXITCODE -ne 0) { throw "Unable to commit candidate-policy baseline." }
    & $gitExe -C $policyRepo branch -M canonical
    $canonicalSha = (& $gitExe -C $policyRepo rev-parse HEAD).Trim()
    & $gitExe -C $policyRepo checkout -q -b candidate
    "candidate" | Set-Content -Path (Join-Path $policyRepo "Strategic\Allowed.cpp") -Encoding ASCII
    & $gitExe -C $policyRepo add Strategic/Allowed.cpp
    & $gitExe -C $policyRepo commit -q -m "strategic fixture change"
    if ($LASTEXITCODE -ne 0) { throw "Unable to commit candidate-policy fixture change." }

    $fixtureGate = Join-Path $policyRepo "Tools\QA\TEST_INTEGRATION_CANDIDATE.ps1"
    & $childPowerShell -NoProfile -ExecutionPolicy Bypass -File $fixtureGate -CandidateRef candidate -CanonicalRef canonical -ExpectedCanonicalSha $canonicalSha *> $null
    Assert-ExitCode "strategic-default-block" 7 $LASTEXITCODE

    & $childPowerShell -NoProfile -ExecutionPolicy Bypass -File $fixtureGate -CandidateRef candidate -CanonicalRef canonical -ExpectedCanonicalSha $canonicalSha -AllowedStrategicPaths "Strategic\Wrong.cpp" *> $null
    Assert-ExitCode "strategic-wrong-allowlist-block" 7 $LASTEXITCODE

    & $childPowerShell -NoProfile -ExecutionPolicy Bypass -File $fixtureGate -CandidateRef candidate -CanonicalRef canonical -ExpectedCanonicalSha $canonicalSha -AllowedStrategicPaths ".\Strategic\Allowed.cpp" *> $null
    Assert-ExitCode "strategic-exact-allowlist" 0 $LASTEXITCODE

    & $childPowerShell -NoProfile -ExecutionPolicy Bypass -File $fixtureGate -CandidateRef candidate -CanonicalRef canonical -ExpectedCanonicalSha $canonicalSha -AllowStrategicChanges *> $null
    Assert-ExitCode "strategic-explicit-all" 0 $LASTEXITCODE

    $fixtureBatchGate = Join-Path $policyRepo "Tools\QA\TEST_INTEGRATION_BATCH.ps1"
    & $childPowerShell -NoProfile -ExecutionPolicy Bypass -File $fixtureBatchGate -CandidateRefs candidate -CanonicalRef canonical -ExpectedCanonicalSha $canonicalSha *> $null
    Assert-ExitCode "batch-strategic-default-block" 23 $LASTEXITCODE

    & $childPowerShell -NoProfile -ExecutionPolicy Bypass -File $fixtureBatchGate -CandidateRefs candidate -CanonicalRef canonical -ExpectedCanonicalSha $canonicalSha -AllowedStrategicPaths "Strategic\Wrong.cpp" *> $null
    Assert-ExitCode "batch-strategic-wrong-allowlist-block" 23 $LASTEXITCODE

    & $childPowerShell -NoProfile -ExecutionPolicy Bypass -File $fixtureBatchGate -CandidateRefs candidate -CanonicalRef canonical -ExpectedCanonicalSha $canonicalSha -AllowedStrategicPaths ".\Strategic\Allowed.cpp" *> $null
    Assert-ExitCode "batch-strategic-exact-allowlist" 0 $LASTEXITCODE

    & $childPowerShell -NoProfile -ExecutionPolicy Bypass -File $fixtureBatchGate -CandidateRefs candidate -CanonicalRef canonical -ExpectedCanonicalSha $canonicalSha -AllowStrategicChanges *> $null
    Assert-ExitCode "batch-strategic-explicit-all" 0 $LASTEXITCODE

    Write-Host "QA_TOOLING_SELF_TEST_OK"
}
finally {
    if (Test-Path $tempRoot) {
        Remove-Item -Recurse -Force $tempRoot
    }
}

exit 0
