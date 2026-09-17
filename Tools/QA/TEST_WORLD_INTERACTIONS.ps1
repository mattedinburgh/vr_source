param([string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path)
$ErrorActionPreference = 'Stop'
function Assert-Contains([string]$Path,[string]$Needle,[string]$Message) {
  $text = [IO.File]::ReadAllText((Join-Path $RepoRoot $Path))
  if (-not $text.Contains($Needle)) { throw $Message }
}
function Assert-NotContains([string]$Path,[string]$Needle,[string]$Message) {
  $text = [IO.File]::ReadAllText((Join-Path $RepoRoot $Path))
  if ($text.Contains($Needle)) { throw $Message }
}
$doors = 'Tactical\Handle Doors.cpp'
$ani = 'Tactical\Soldier Ani.cpp'
$soldier = 'Tactical\Soldier Control.cpp'
$cursors = 'Tactical\UI Cursors.cpp'
Assert-Contains $doors 'ShouldAutoOpenDoorAfterLockManipulation' 'Missing lock/open separation helper.'
Assert-Contains $doors 'pDoor->ubLockID >= NUM_LOCKS' 'Missing malformed lock bounds guard.'
Assert-Contains $doors 'OurNoise( pSoldier->ubID, pSoldier->aiData.sPendingActionData2' 'Door noise is not emitted from the interacted door grid.'
Assert-Contains $doors '!pSoldier->ubDoorOpeningNoise && pSoldier->bVisible == -1' 'Noisy unseen doors can still suppress their animation.'
Assert-NotContains $ani 'MakeNoise( pSoldier->ubID, pSoldier->sGridNo' 'Legacy soldier-grid door noise is still active.'
Assert-NotContains $soldier '#include "Handle Items.h"' 'Non-PCH Soldier Control reintroduced the Handle Items include cycle.'
Assert-NotContains $cursors '#include "Handle Items.h"' 'Non-PCH UI Cursors reintroduced the Handle Items include cycle.'
Assert-Contains $soldier 'BuildFortification( this->sMTActionGridNo, Item[ pObj->usItem ].usItemFlag, this->ubDirection )' 'Fortification build lost actor orientation.'
Assert-Contains $soldier 'IsRemovableFortificationAtGridNo( this->sMTActionGridNo )' 'Fortification removal lost completion revalidation.'
Write-Host 'PASS: world interactions source invariants'
