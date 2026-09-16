param(
    [string]$SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
)

$ErrorActionPreference = "Stop"
$weapons = Join-Path $SourceRoot "Tactical\Weapons.cpp"
if (-not (Test-Path -LiteralPath $weapons)) {
    throw "Weapons.cpp not found: $weapons"
}

$text = Get-Content -LiteralPath $weapons -Raw
$fail = New-Object System.Collections.Generic.List[string]

function Require([string]$pattern, [string]$message) {
    if ($text -notmatch $pattern) { $script:fail.Add($message) }
}

function Forbid([string]$pattern, [string]$message) {
    if ($text -match $pattern) { $script:fail.Add($message) }
}

Require 'UINT32\s+CalcNewChanceToHitGun\s*\(' 'CalcNewChanceToHitGun is missing.'
Require 'UINT32\s+AICalcChanceToHitGun\s*\(' 'AICalcChanceToHitGun is missing.'
Forbid '__max\s*\(\s*1\s*,\s*CalcChanceToHitGun' 'Hidden 1% pre-NCTH autofire floor returned.'
Require 'MAX_EFFECTIVE_RANGE_MULTIPLIER' 'NCTH effective-range limiter is missing.'
Require 'uiChance\s*=\s*1\s*;\s*\/\/|uiChance\s*=\s*1\s*;' 'Current 1.13 out-of-range AI token chance is missing.'
Require 'IRON_SIGHT_PERFORMANCE_BONUS\)\s*\/\s*100\.0f' 'Iron-sight aperture math lost floating-point division.'
Require 'LaserActive\(\)' 'AI NCTH laser handling is not gated by an active laser.'
Require '!AICombatTeam\(pSoldier\)' 'Hidden human-AI difficulty/class CTH exclusion is missing.'
Require 'gNCTHWorkingDiagnostic' 'NCTH diagnostic instrumentation is missing.'

if ($fail.Count -gt 0) {
    Write-Host "NCTH CORE AUDIT: FAIL" -ForegroundColor Red
    $fail | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host "NCTH CORE AUDIT: PASS" -ForegroundColor Green
Write-Host "Verified:"
Write-Host " - one modern NCTH core path is present"
Write-Host " - no hidden pre-NCTH autofire CTH floor"
Write-Host " - effective-range token suppression behavior remains"
Write-Host " - iron-sight and laser parity safeguards remain"
Write-Host " - human enemy/militia hidden difficulty CTH bonuses remain disabled"
exit 0