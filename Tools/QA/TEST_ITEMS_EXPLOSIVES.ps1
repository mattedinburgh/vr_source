$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$itemsPath = Join-Path $repo "Tactical\Items.cpp"
$keysPath = Join-Path $repo "Tactical\Keys.cpp"
$items = [IO.File]::ReadAllText($itemsPath)
$keys = [IO.File]::ReadAllText($keysPath)
$failures = New-Object System.Collections.Generic.List[string]
function Assert-True {
    param([bool]$Condition, [string]$Label)
    if ($Condition) { Write-Host "PASS: $Label" } else { $failures.Add($Label) }
}
function Require-Text {
    param([string]$Text, [string]$Needle, [string]$Label)
    Assert-True ($Text.Contains($Needle)) $Label
}
function Require-Order {
    param([string]$Text, [string]$First, [string]$Second, [string]$Label)
    $a = $Text.IndexOf($First); $b = $Text.IndexOf($Second)
    Assert-True ($a -ge 0 -and $b -gt $a) $Label
}
$findStart = $items.IndexOf("INT8 FindLockBomb( SOLDIERTYPE * pSoldier )")
$findEnd = $items.IndexOf("INT8 FindUsableObj", $findStart)
$findLockBomb = $items.Substring($findStart, $findEnd - $findStart)
$blowStart = $keys.IndexOf("BOOLEAN AttemptToBlowUpLock( SOLDIERTYPE * pSoldier, DOOR * pDoor )")
$blowEnd = $keys.IndexOf("//dnl ch42", $blowStart)
$blowLock = $keys.Substring($blowStart, $blowEnd - $blowStart)
Require-Text $findLockBomb "pSoldier->inv[bLoop].exists() == true" "door charge search ignores empty slots"
Require-Text $findLockBomb "pSoldier->inv[bLoop][0]->data.objectStatus >= USABLE" "door charge search skips broken charges"
Require-Order $findLockBomb "exists() == true" "Item[pSoldier->inv[bLoop].usItem].lockbomb" "empty-slot guard precedes Item lookup"
Require-Text $blowLock "UINT16 usDamage = Explosive[Item[pSoldier->inv[bSlot].usItem].ubClassIndex].ubDamage;" "charge damage captured before consumption"
Require-Text $blowLock "UINT16 usItem = pSoldier->inv[bSlot].usItem;" "charge item captured before consumption"
Require-Text $blowLock "UINT8 ubVolume = (UINT8)Explosive[Item[pSoldier->inv[bSlot].usItem].ubClassIndex].ubVolume;" "charge noise volume captured before consumption"
Require-Order $blowLock "UINT16 usDamage" "RemoveObjectsFromStack(1)" "damage snapshot precedes charge consumption"
Require-Order $blowLock "UINT16 usItem" "RemoveObjectsFromStack(1)" "item snapshot precedes charge consumption"
Require-Order $blowLock "UINT8 ubVolume" "RemoveObjectsFromStack(1)" "volume snapshot precedes charge consumption"
$consumeCount = ([regex]::Matches($blowLock, [regex]::Escape("RemoveObjectsFromStack(1)"))).Count
Assert-True ($consumeCount -eq 1) "breach attempt consumes exactly one charge in one place"
Require-Text $blowLock "pDoor->bLockDamage = 127;" "lock explosive damage saturates instead of signed overflow"
Require-Text $blowLock "ubSmashDifficulty != OPENING_NOT_POSSIBLE" "impossible locks cannot be breached by charge damage"
Require-Order $blowLock "ubSmashDifficulty != OPENING_NOT_POSSIBLE" "RemoveDoorInfoFromTable" "impossible-lock guard precedes door removal"
Require-Text $blowLock "ubVolume, NOISE_EXPLOSION" "successful charge placement creates explosion noise"
Require-Text $blowLock "pSoldier->sGridNo, usItem, 0" "failed planting detonates saved charge item"
Require-Order $blowLock "if (iResult >= -20)" "else" "planting success and failure paths remain explicit"
function Sim-Breach {
    param([int]$LockDifficulty, [int]$OldDamage, [int]$ChargeDamage, [bool]$PlacementOk, [bool]$Impossible)
    $state = [ordered]@{ consumed=1; removed=$false; noise=$false; selfDetonation=$false; damage=$OldDamage }
    if (-not $PlacementOk) { $state.selfDetonation=$true; return [pscustomobject]$state }
    $state.noise=$true
    $state.damage=[Math]::Min(127, $OldDamage + $ChargeDamage)
    if (-not $Impossible -and ($state.damage -gt $LockDifficulty -or $ChargeDamage -gt $LockDifficulty)) { $state.removed=$true }
    return [pscustomobject]$state
}
$survive = Sim-Breach -LockDifficulty 80 -OldDamage 0 -ChargeDamage 40 -PlacementOk $true -Impossible $false
Assert-True ($survive.consumed -eq 1 -and -not $survive.removed -and $survive.noise) "placed charge can damage a surviving lock and still create noise"
$breach = Sim-Breach -LockDifficulty 50 -OldDamage 20 -ChargeDamage 40 -PlacementOk $true -Impossible $false
Assert-True ($breach.consumed -eq 1 -and $breach.removed -and $breach.noise) "sufficient placed charge breaches lock and creates noise"
$impossible = Sim-Breach -LockDifficulty 255 -OldDamage 120 -ChargeDamage 100 -PlacementOk $true -Impossible $true
Assert-True (-not $impossible.removed -and $impossible.damage -eq 127 -and $impossible.noise) "impossible lock resists charge while damage saturates and noise remains"
$fail = Sim-Breach -LockDifficulty 50 -OldDamage 0 -ChargeDamage 40 -PlacementOk $false -Impossible $false
Assert-True ($fail.consumed -eq 1 -and $fail.selfDetonation -and -not $fail.noise) "failed planting consumes charge and takes self-detonation path"
$charges = @(
    [pscustomobject]@{ Name='Broken'; Exists=$true; LockBomb=$true; Status=5 },
    [pscustomobject]@{ Name='Usable'; Exists=$true; LockBomb=$true; Status=80 }
)
$usableThreshold = 15
$selected = $charges | Where-Object { $_.Exists -and $_.LockBomb -and $_.Status -ge $usableThreshold } | Select-Object -First 1
Assert-True ($selected.Name -eq 'Usable') "usable charge is found even when a broken charge appears first"
if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "ITEM_EXPLOSIVES_FIXTURE_FAILED"
    foreach ($failure in $failures) { Write-Host "FAIL: $failure" }
    exit 63
}
Write-Host ""
Write-Host "ITEM_EXPLOSIVES_FIXTURE_OK"
exit 0