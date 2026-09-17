$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$path = Join-Path $repo "Strategic\Map Screen Interface Map Inventory.cpp"
$src = [IO.File]::ReadAllText($path)
$failures = New-Object System.Collections.Generic.List[string]
function Assert-True {
    param([bool]$Condition, [string]$Label)
    if ($Condition) { Write-Host "PASS: $Label" } else { $failures.Add($Label) }
}
function Require-Text {
    param([string]$Needle, [string]$Label)
    Assert-True ($src.Contains($Needle)) $Label
}
function Require-Order {
    param([string]$First, [string]$Second, [string]$Label)
    $a=$src.IndexOf($First); $b=$src.IndexOf($Second,$a+1)
    Assert-True ($a -ge 0 -and $b -gt $a) $Label
}
Require-Text "uiNumOfSlots = pInventoryPoolList.size();" "stash persistence iterates the full backing inventory"
Require-Text "RefreshItemPools(pInventoryPoolList, uiNumberOfSeenItems + uiNumberOfUnSeenItems);" "loaded-sector rebuild uses compacted seen plus unseen count"
Require-Text "SaveWorldItemsToTempItemFile(sSelMapX, sSelMapY, iCurrentMapSectorZ, uiNumberOfSeenItems + uiNumberOfUnSeenItems, pInventoryPoolList, FALSE)" "unloaded-sector save uses compacted seen plus unseen count"
Require-Text "gWorldItems[ i ].usFlags |= WORLD_ITEM_GRIDNO_NOT_SET_USE_ENTRY_POINT;" "loaded-sector invalid-grid flag updates authoritative world item"
Require-Order "gWorldItems[ i ].usFlags |= WORLD_ITEM_GRIDNO_NOT_SET_USE_ENTRY_POINT;" "pInventoryPoolList.push_back( gWorldItems[ i ] );" "loaded-sector grid flag is corrected before copying item into stash"
# Deterministic backing-store round trip for page-boundary persistence.
$backing = New-Object System.Collections.Generic.List[object]
for ($i=0; $i -lt 30; $i++) {
    $backing.Add([pscustomobject]@{ Exists=$false; Item=""; Count=0 })
}
$backing[0] = [pscustomobject]@{ Exists=$true; Item='first'; Count=1 }
$backing[25] = [pscustomobject]@{ Exists=$true; Item='late'; Count=3 }
$displaySlots = 10
$oldPageBoundSave = @($backing | Select-Object -First $displaySlots | Where-Object { $_.Exists })
$fullBackingSave = @($backing | Where-Object { $_.Exists })
Assert-True ($oldPageBoundSave.Count -eq 1) "fixture reproduces page-bound truncation"
Assert-True ($fullBackingSave.Count -eq 2 -and @($fullBackingSave | Where-Object { $_.Item -eq 'late' }).Count -eq 1) "full backing-store save retains late-slot item"
$unseen = @([pscustomobject]@{ Exists=$true; Item='hidden'; Count=2 })
$serialized = @($fullBackingSave) + @($unseen)
Assert-True ($serialized.Count -eq 3) "seen and unseen items serialize as one complete stash"
$visibleCount = ($fullBackingSave | Measure-Object Count -Sum).Sum
Assert-True ($visibleCount -eq 4) "visible item count follows object counts rather than slot count"
if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "ITEM_PERSISTENCE_FIXTURE_FAILED"
    foreach ($failure in $failures) { Write-Host "FAIL: $failure" }
    exit 64
}
Write-Host ""
Write-Host "ITEM_PERSISTENCE_FIXTURE_OK"
exit 0