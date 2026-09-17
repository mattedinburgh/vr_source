$ErrorActionPreference = 'Stop'
$failures = New-Object System.Collections.Generic.List[string]

function Assert-True([bool]$condition, [string]$message) {
    if ($condition) { Write-Host "PASS: $message" }
    else { $failures.Add($message) }
}

function Apply-RagTreatment([int]$basePossible, [int]$ragPoints) {
    $possible = [int][Math]::Floor(($basePossible * 2 + 2) / 3.0)
    $actual = $possible
    $medCost = $actual * 2
    if ($medCost -gt $ragPoints) {
        $actual = [int][Math]::Floor($ragPoints / 2.0)
        $medCost = $actual * 2
    }
    [pscustomobject]@{ Possible=$possible; Actual=$actual; Cost=$medCost }
}

$full = Apply-RagTreatment 30 100
Assert-True ($full.Possible -eq 20) 'rag throughput is two-thirds of normal treatment'
Assert-True ($full.Cost -eq 40) 'rag material cost is two condition points per treatment point'
Assert-True (($full.Possible * 3) -eq (30 * 2)) 'rag throughput corresponds to 50 percent more AP/time'

$short = Apply-RagTreatment 30 11
Assert-True ($short.Actual -eq 5) 'odd rag condition cannot buy a fractional treatment point'
Assert-True ($short.Cost -eq 10) 'rag consumption never exceeds available condition'
Assert-True (($short.Cost / $short.Actual) -eq 2) 'rag half-efficiency ratio survives shortage clamping'

function Advance-GrenadeAim([int]$shownAim, [int]$maxAim) {
    if ($shownAim -lt 0) { $shownAim = 0 }
    if ($shownAim -ge $maxAim) { return $maxAim }
    $futureAim = [Math]::Min($shownAim + 1, $maxAim)
    return $futureAim
}

$aim = 0
1..12 | ForEach-Object { $aim = Advance-GrenadeAim $aim 4 }
Assert-True ($aim -eq 4) 'extra grenade aim increments clamp at maximum instead of wrapping'
Assert-True ((Advance-GrenadeAim -3 4) -eq 1) 'negative grenade aim state is normalized before the next increment'
Assert-True ((Advance-GrenadeAim 4 4) -eq 4) 'grenade selector remains stable when already at maximum'

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "ITEM_RAG_GRENADE_FIXTURE_FAILED"
    foreach ($failure in $failures) { Write-Host "FAIL: $failure" }
    exit 66
}

Write-Host ""
Write-Host "ITEM_RAG_GRENADE_FIXTURE_OK"
exit 0
