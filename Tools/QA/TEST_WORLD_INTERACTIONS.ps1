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
$keys = 'Tactical\Keys.cpp'
$handleItems = 'Tactical\Handle Items.cpp'
$ani = 'Tactical\Soldier Ani.cpp'
$soldier = 'Tactical\Soldier Control.cpp'
$cursors = 'Tactical\UI Cursors.cpp'
Assert-Contains $doors 'ShouldAutoOpenDoorAfterLockManipulation' 'Missing lock/open separation helper.'
Assert-Contains $doors 'pDoor->ubLockID >= NUM_LOCKS' 'Missing malformed lock bounds guard.'
Assert-Contains $keys 'pDoor->ubLockID == LOCK_UNOPENABLE || pDoor->ubLockID >= NUM_LOCKS' 'Lock manipulation can still index beyond the Vengeance LockTable.'
Assert-Contains $keys 'pDoor == NULL || pDoor->ubLockID >= NUM_LOCKS' 'Key locking/unlocking can still accept unsupported lock IDs.'
Assert-Contains $keys 'pDoor == NULL || pDoor->ubLockID == LOCK_UNOPENABLE || pDoor->ubLockID >= NUM_LOCKS' 'Breaching charges can still be consumed on an invalid lock ID.'
Assert-Contains $keys 'pDoor->ubTrapID == NO_TRAP || pDoor->ubTrapID >= NUM_DOOR_TRAPS' 'Door untrapping can still accept no-trap or out-of-range trap IDs.'
Assert-Contains $keys 'pDoor->ubTrapID >= NUM_DOOR_TRAPS' 'Door trap examination/trigger paths lost trap-ID bounds validation.'
Assert-Contains $keys 'Sanitize corrupt/custom trap IDs because callers inspect DoorTrapTable immediately after this returns.' 'Invalid door trap IDs are not sanitized before callers inspect DoorTrapTable.'
Assert-Contains $keys 'pDoor->bPerceivedTrapped = DOOR_PERCEIVED_UNTRAPPED;' 'Invalid door trap state is not normalized safely.'
Assert-Contains $doors 'OurNoise( pSoldier->ubID, pSoldier->aiData.sPendingActionData2' 'Door noise is not emitted from the interacted door grid.'
Assert-Contains $doors '!pSoldier->ubDoorOpeningNoise && pSoldier->bVisible == -1' 'Noisy unseen doors can still suppress their animation.'
Assert-NotContains $ani 'MakeNoise( pSoldier->ubID, pSoldier->sGridNo' 'Legacy soldier-grid door noise is still active.'
Assert-NotContains $soldier '#include "Handle Items.h"' 'Non-PCH Soldier Control reintroduced the Handle Items include cycle.'
Assert-NotContains $cursors '#include "Handle Items.h"' 'Non-PCH UI Cursors reintroduced the Handle Items include cycle.'
Assert-Contains $handleItems 'return GetFirstItemWithFlag( &usRecoveredItem, uiFortificationFlag );' 'Dismantle UI validity can advertise a fortification with no recoverable material.'
Assert-Contains $handleItems 'BOOLEAN RemoveFortification( INT32 sGridNo, UINT16* pRecoveredItem )' 'Fortification removal no longer returns the exact recovered material.'
Assert-Contains $handleItems 'if ( !GetFirstItemWithFlag( &usRecoveredItem, uiRemovedFlag ) )' 'Fortification can be removed before its recovery item is validated.'
Assert-Contains $soldier 'RemoveFortification( this->sMTActionGridNo, &usRecoveredItem )' 'Multi-turn removal lost exact material recovery.'
Assert-Contains $soldier 'BuildFortification( this->sMTActionGridNo, Item[ pObj->usItem ].usItemFlag, this->ubDirection )' 'Fortification build lost actor orientation.'
Assert-Contains $soldier 'IsRemovableFortificationAtGridNo( this->sMTActionGridNo )' 'Fortification removal lost completion revalidation.'
Assert-Contains $soldier 'BOOLEAN fActionCompleted = FALSE;' 'Multi-turn completion is not transactional.'
Assert-Contains $soldier 'if ( !fActionCompleted )' 'Failed world mutations can still be marked complete.'
Assert-Contains $soldier 'CancelMultiTurnAction(FALSE);' 'Failed world mutations no longer cancel cleanly.'
Write-Host 'PASS: world interactions source invariants'
