$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$failures = New-Object System.Collections.Generic.List[string]

function Require-Text {
    param([string]$RelativePath, [string]$Needle, [string]$Label)
    $path = Join-Path $repo $RelativePath
    if (-not (Test-Path $path)) {
        $failures.Add("$Label - missing file: $RelativePath")
        return
    }
    $text = [System.IO.File]::ReadAllText($path)
    if (-not $text.Contains($Needle)) {
        $failures.Add("$Label - invariant not found in $RelativePath")
    } else {
        Write-Host "PASS: $Label"
    }
}
Require-Text "Strategic\Map Screen Interface Map Inventory.cpp" "uiNumOfSlots = pInventoryPoolList.size();" "sector stash saves full backing inventory"
Require-Text "Strategic\Map Screen Interface Map Inventory.cpp" "gWorldItems[ i ].usFlags |= WORLD_ITEM_GRIDNO_NOT_SET_USE_ENTRY_POINT;" "loaded-sector stash updates world items"
Require-Text "Strategic\Map Screen Interface Map Inventory.h" "INT32 sGridNo=-1" "unknown stash placement uses minus-one sentinel"
Require-Text "Strategic\Map Screen Interface Map Inventory.cpp" "for ( UINT8 ubWave = 0; ubWave < 5; ++ubWave )" "ammo distribution uses fair waves"
Require-Text "Strategic\Map Screen Interface Map Inventory.cpp" "TopUpGunFromSector( pGun, x, 1 )" "ammo shortage primes one shootable gun per unarmed merc"
Require-Text "Strategic\Map Screen Interface Map Inventory.cpp" "UINT32 uiRoundsLoaded = PrimeEmptySectorMercGuns();" "shootability is prioritized before full gun top-up"
Require-Text "Strategic\Map Screen Interface Map Inventory.cpp" "ubWaveTarget = (UINT8)__min( (UINT32)demand.ubMaxMags," "ammo reserve target is per demand"
Require-Text "Strategic\Map Screen Interface Map Inventory.cpp" "GetMagSize( pGun, x )" "ammo demand uses effective attachment-modified magazine capacity"
Require-Text "Strategic\Map Screen Interface Map Inventory.cpp" "fallbackMag.ubMagType >= AMMO_BOX" "bulk ammo cannot become a ready replacement magazine"
Require-Text "Strategic\Map Screen Interface Map Inventory.cpp" "PoolObjectForSectorLoadout( &newMag );" "failed spare-mag placement returns ammo to sector pool"
Require-Text "Strategic\Map Screen Interface Map Inventory.cpp" "SectorLoadoutAmmoTypeLess( (UINT8)sBestFullType, ubCurrentType )" "partial gun upgrades only to a strictly better ammo type"
Require-Text "Strategic\Map Screen Interface Map Inventory.cpp" "CountSectorAmmoRounds( ubCalibre, (UINT8)sBestFullType ) >= usMagSize" "partial gun replacement requires a complete better load"
Require-Text "Strategic\Map Screen Interface Map Inventory.cpp" "PoolObjectForSectorLoadout( &oldAmmo );" "successful ammo upgrade returns old partial load to pool"
Require-Text "Strategic\Map Screen Interface Map Inventory.cpp" "bGunStatus >= USABLE" "ammo distribution excludes physically broken guns"
Require-Text "Strategic\Map Screen Interface Map Inventory.cpp" "!SectorLoadoutGunIsUsable( pGun, (UINT8)x )" "broken guns do not create spare-ammo demand"

$ammoFixture = Join-Path $PSScriptRoot "TEST_ITEMS_AMMO_ALLOCATION.ps1"
& "$PSHOME\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $ammoFixture
if ($LASTEXITCODE -ne 0) {
    $failures.Add("deterministic ammo allocation fixture failed")
} else {
    Write-Host "PASS: deterministic attachment/shortage ammo fixture"
}
Require-Text "Tactical\Rotting Corpses.cpp" "static void ReduceLootForMilitiaKill" "militia tactical kills use reduced loot"
Require-Text "Tactical\Rotting Corpses.cpp" "EnemyItemMinimumDropWeight" "minimum loot respects category rates"
Require-Text "Tactical\Tactical Save.h" "ADD_DEAD_SOLDIER_NO_LOOT" "non-player autoresolve supports explicit zero loot"
Require-Text "Tactical\Tactical Save.h" "ADD_DEAD_SOLDIER_PLAYER_AUTORESOLVE_LOOT" "merc autoresolve supports reduced loot"
Require-Text "Tactical\Soldier Control.cpp" "uiPossible = (uiPossible * 2 + 2) / 3;" "rag treatment uses two-thirds throughput"
Require-Text "Tactical\Soldier Control.cpp" "uiMedcost = uiActual * 2;" "rag material efficiency is halved"
Require-Text "Strategic\Auto Resolve.cpp" "uiPossible = (uiPossible * 2 + 2) / 3;" "autoresolve rag throughput matches tactical"
Require-Text "Tactical\UI Cursors.cpp" "if ( pSoldier->aiData.bShownAimTime >= maxAimLevels )" "grenade aim selector clamps at maximum"
Require-Text "Tactical\UI Cursors.cpp" "bFutureAim = __min( bFutureAim, maxAimLevels );" "grenade aim increment cannot wrap"
Require-Text "TileEngine\physics.cpp" "Smoke and gas grenades should simply be neutralized by water." "water neutralizes smoke and gas throws"
Require-Text "TileEngine\physics.cpp" "Explosive[Item[pObject->Obj.usItem].ubClassIndex].ubType == EXPLOSV_FLASHBANG" "underwater delayed detonation includes flashbang"
Require-Text "TileEngine\physics.cpp" "Water( sTargetSpot, ubTargetLevel )" "throw force is water-aware"
Require-Text "TileEngine\Explosion Control.cpp" "!Water(sGridNo, bLevel)" "post-explosion smoke is blocked on water"
Require-Text "Tactical\Items.cpp" "pSoldier->inv[bLoop][0]->data.objectStatus >= USABLE" "door breaching ignores unusable charges"
Require-Text "Tactical\Keys.cpp" "UINT16 usDamage = Explosive[Item[pSoldier->inv[bSlot].usItem].ubClassIndex].ubDamage;" "door charge damage is saved before consumption"
Require-Text "Tactical\Keys.cpp" "LockTable[pDoor->ubLockID].ubSmashDifficulty != OPENING_NOT_POSSIBLE" "door charges respect impossible locks"
Require-Text "Tactical\Keys.cpp" "pSoldier->bOverTerrainType, ubVolume, NOISE_EXPLOSION" "door charges generate explosion noise"

$explosivesFixture = Join-Path $PSScriptRoot "TEST_ITEMS_EXPLOSIVES.ps1"
& "$PSHOME\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $explosivesFixture
if ($LASTEXITCODE -ne 0) {
    $failures.Add("deterministic explosives fixture failed")
} else {
    Write-Host "PASS: deterministic shaped-charge/explosives fixture"
}

$lootFixture = Join-Path $PSScriptRoot "TEST_ITEMS_LOOT_RULES.ps1"
& "$PSHOME\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $lootFixture
if ($LASTEXITCODE -ne 0) {
    $failures.Add("deterministic loot provenance fixture failed")
} else {
    Write-Host "PASS: deterministic tactical/autoresolve loot-rule matrix"
}

$persistenceFixture = Join-Path $PSScriptRoot "TEST_ITEMS_PERSISTENCE.ps1"
& "$PSHOME\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $persistenceFixture
if ($LASTEXITCODE -ne 0) {
    $failures.Add("deterministic persistence fixture failed")
} else {
    Write-Host "PASS: deterministic inventory persistence fixture"
}

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "ITEM_STREAM_INVARIANTS_FAILED"
    foreach ($failure in $failures) { Write-Host "FAIL: $failure" }
    exit 61
}

Write-Host ""
Write-Host "ITEM_STREAM_INVARIANTS_OK"
exit 0
