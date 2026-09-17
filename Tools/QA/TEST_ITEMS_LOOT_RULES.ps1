$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$corpsePath = Join-Path $repo "Tactical\Rotting Corpses.cpp"
$savePath = Join-Path $repo "Tactical\Tactical Save.cpp"
$autoPath = Join-Path $repo "Strategic\Auto Resolve.cpp"
$corpse = [IO.File]::ReadAllText($corpsePath)
$save = [IO.File]::ReadAllText($savePath)
$auto = [IO.File]::ReadAllText($autoPath)
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
    $a=$Text.IndexOf($First); $b=$Text.IndexOf($Second,$a+1)
    Assert-True ($a -ge 0 -and $b -gt $a) $Label
}
Require-Text $corpse "gGameOptions.fEnemiesDropAllItems || !EnemyWasKilledByMilitia( pSoldier )" "drop-all overrides tactical militia-kill reduction"
Require-Text $corpse "if ( Random( 100 ) < 50 )" "tactical militia kill applies a 50 percent secondary reduction"
Require-Order $corpse "ReduceLootForMilitiaKill( pSoldier );" "EnsureMinimumEnemyLootDrop( pSoldier );" "militia reduction runs before minimum-loot recovery"
Require-Text $corpse "if ( pSoldier == NULL || pSoldier->bTeam != ENEMY_TEAM )" "minimum-loot recovery is limited to enemy corpses"
Require-Text $corpse "uiFallbackWeightTotal += uiDropWeight;" "minimum-loot fallback remains weighted by configured category rates"
Require-Text $corpse "pSoldier->inv[ iFallbackSlot ].fFlags &= ~OBJECT_UNDROPPABLE;" "minimum-loot fallback re-enables one eligible item"
Require-Text $auto "gpAR->ubBattleStatus == BATTLE_VICTORY && gpAR->ubMercs > 0" "merc-participating autoresolve victory is explicit"
Require-Text $auto "uiCorpseFlags |= ADD_DEAD_SOLDIER_PLAYER_AUTORESOLVE_LOOT;" "merc autoresolve victory gets reduced-loot provenance"
Require-Text $auto "uiCorpseFlags |= ADD_DEAD_SOLDIER_NO_LOOT;" "non-player autoresolve gets explicit no-loot provenance"
Require-Text $save "if ( uiFlags & ADD_DEAD_SOLDIER_NO_LOOT )" "no-loot provenance is honored during unloaded corpse serialization"
Require-Text $save "(uiFlags & ADD_DEAD_SOLDIER_PLAYER_AUTORESOLVE_LOOT) ? 25 : 75" "player autoresolve uses lower extra-loss chance than generic strategic death"
function Loot-Policy {
    param([string]$Mode, [bool]$MilitiaKill=$false, [bool]$MercParticipated=$false, [bool]$Victory=$true, [bool]$DropAll=$false)
    if ($Mode -eq 'tactical') {
        if ($DropAll) { return [pscustomobject]@{ExtraLoss=0; Minimum=$true; NoLoot=$false} }
        if ($MilitiaKill) { return [pscustomobject]@{ExtraLoss=50; Minimum=$true; NoLoot=$false} }
        return [pscustomobject]@{ExtraLoss=0; Minimum=$true; NoLoot=$false}
    }
    if ($Mode -eq 'autoresolve') {
        if ($Victory -and $MercParticipated) { return [pscustomobject]@{ExtraLoss=25; Minimum=$false; NoLoot=$false} }
        return [pscustomobject]@{ExtraLoss=100; Minimum=$false; NoLoot=$true}
    }
    throw "unknown mode"
}
$mercTactical = Loot-Policy -Mode tactical
$militiaTactical = Loot-Policy -Mode tactical -MilitiaKill $true
$militiaAuto = Loot-Policy -Mode autoresolve -MercParticipated $false -Victory $true
$mercAuto = Loot-Policy -Mode autoresolve -MercParticipated $true -Victory $true
$defeatAuto = Loot-Policy -Mode autoresolve -MercParticipated $true -Victory $false
Assert-True ($mercTactical.ExtraLoss -eq 0 -and $mercTactical.Minimum) "normal tactical enemy kill keeps normal loot plus minimum eligible drop"
Assert-True ($militiaTactical.ExtraLoss -eq 50 -and $militiaTactical.Minimum) "militia tactical kill is reduced but never intentionally empty when eligible loot exists"
Assert-True ($militiaAuto.NoLoot -and $militiaAuto.ExtraLoss -eq 100) "militia-only autoresolve victory yields no enemy loot"
Assert-True (-not $mercAuto.NoLoot -and $mercAuto.ExtraLoss -eq 25) "merc-participating autoresolve victory yields reduced loot"
Assert-True ($defeatAuto.NoLoot) "autoresolve defeat does not create victory loot"
$dropAll = Loot-Policy -Mode tactical -MilitiaKill $true -DropAll $true
Assert-True ($dropAll.ExtraLoss -eq 0) "global drop-all remains authoritative over militia reduction"
if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "ITEM_LOOT_RULES_FIXTURE_FAILED"
    foreach ($failure in $failures) { Write-Host "FAIL: $failure" }
    exit 65
}
Write-Host ""
Write-Host "ITEM_LOOT_RULES_FIXTURE_OK"
exit 0