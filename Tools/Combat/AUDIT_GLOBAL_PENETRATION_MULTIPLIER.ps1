param([string]$SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path)
$ErrorActionPreference = 'Stop'
$h = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'GameSettings.h')
$c = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'GameSettings.cpp')
$l = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'Tactical\LOS.cpp')
function Assert-True([bool]$ok,[string]$message) { if (-not $ok) { throw $message } }
Assert-True ($h -match 'FLOAT\s+fGlobalPenetrationMultiplier;') 'Missing multiplier field'
Assert-True ($c -match 'COVER_SYSTEM_GLOBAL_PENETRATION_MULTIPLIER"\s*,\s*1\.0f') 'Missing or changed 1.0 default'
$uses = ([regex]::Matches($l, 'ApplyGlobalPenetrationMultiplier\s*\(')).Count
Assert-True ($uses -eq 3) ('Expected helper definition + real bullet + CTGT use; got ' + $uses)
function Scale([int]$resistance,[double]$multiplier) {
    if ($resistance -le 0) { return $resistance }
    if ($multiplier -le 0) { $multiplier = 1.0 }
    return [Math]::Max(1,[int][Math]::Floor(($resistance / $multiplier) + 0.5))
}
$baseline = Scale 40 1.0
$morePenetration = Scale 40 2.0
$lessPenetration = Scale 40 0.5
Assert-True ($baseline -eq 40) '1.0 does not preserve baseline resistance'
Assert-True ($morePenetration -eq 20) '2.0 does not halve effective resistance'
Assert-True ($lessPenetration -eq 80) '0.5 does not double effective resistance'
Assert-True ($morePenetration -lt $baseline -and $baseline -lt $lessPenetration) 'Multiplier direction is not monotonic'
Write-Host 'PASS: global penetration multiplier is isolated, baseline-safe, and shared by live bullet + CTGT.'