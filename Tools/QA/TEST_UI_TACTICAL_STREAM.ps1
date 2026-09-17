param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

function Assert-Contains([string]$Text, [string]$Needle, [string]$Message) {
    if (-not $Text.Contains($Needle)) { throw $Message }
}

$panelsPath = Join-Path $RepoRoot 'Tactical\Interface Panels.cpp'
$quotesPath = Join-Path $RepoRoot 'Tactical\Civ Quotes.cpp'
$buttonPath = Join-Path $RepoRoot 'Standard Gaming Platform\Button System.cpp'

$panels = [IO.File]::ReadAllText($panelsPath)
$quotes = [IO.File]::ReadAllText($quotesPath)
$buttons = [IO.File]::ReadAllText($buttonPath)

# QuickButton renderer contract: disabled buttons do not honor CLICKED_ON, so the
# active ATK/WDR command must remain enabled while its opposite is disabled.
Assert-Contains $buttons 'if(b->uiFlags & BUTTON_ENABLED )' 'QuickButton renderer contract changed; re-audit command pressed-state logic.'
Assert-Contains $panels 'if (fCommandActive)' 'Missing active-command visual-state branch.'
Assert-Contains $panels 'if (ubCommand == AI_PLAYER_COMMAND_ATTACK) EnableButton(iAttackButton); else DisableButton(iAttackButton);' 'ATK active visual state is not renderer-safe.'
Assert-Contains $panels 'if (ubCommand == AI_PLAYER_COMMAND_WITHDRAW) EnableButton(iWithdrawButton); else DisableButton(iWithdrawButton);' 'WDR active visual state is not renderer-safe.'
Assert-Contains $panels '!AIPlayerTeamCommandActive() && AIStartPlayerTeamCommand(AI_PLAYER_COMMAND_ATTACK)' 'ATK callback can restart an already-active command.'
Assert-Contains $panels '!AIPlayerTeamCommandActive() && AIStartPlayerTeamCommand(AI_PLAYER_COMMAND_WITHDRAW)' 'WDR callback can restart an already-active command.'
Assert-Contains $panels 'SetAICommandButtonPressed(iSpeedButton, AIPlayerCommandFastForward());' 'FAST visual state is not synchronized to the real fast-forward state.'

# English contextual library: all 23 semantic callout classes need a live pool,
# and the former Spanish pools must not survive in this block.
$poolMatches = [regex]::Matches($quotes, 'static const CHAR16 \* const gAICombatLines_[A-Z_]+\[\]')
if ($poolMatches.Count -ne 23) { throw "Expected 23 English contextual line pools, found $($poolMatches.Count)." }
$pickerMatches = [regex]::Matches($quotes, 'case AI_BATTLE_CALL_[A-Z_]+: pLines = gAICombatLines_[A-Z_]+;')
if ($pickerMatches.Count -ne 23) { throw "Expected 23 live callout pool mappings, found $($pickerMatches.Count)." }

$poolStart = $quotes.IndexOf('// VR battlefield communication -------------------------------------------------')
$poolEnd = $quotes.IndexOf('enum AI_BATTLE_EMOTION', $poolStart)
if ($poolStart -lt 0 -or $poolEnd -le $poolStart) { throw 'Could not locate contextual callout library boundaries.' }
$poolBlock = $quotes.Substring($poolStart, $poolEnd - $poolStart)
if ($poolBlock.Contains('\u00A1') -or $poolBlock.Contains('\u00F3') -or $poolBlock.Contains('\u00E9')) {
    throw 'Spanish escaped text remains in the live contextual callout library.'
}
Assert-Contains $quotes 'const CHAR16 *zPoolLine = PickAICombatLineText( ubCallout );' 'Contextual line pools are declared but not wired into live popup selection.'
Assert-Contains $quotes 'if ( pCiv->bTeam == ENEMY_TEAM || pCiv->bTeam == MILITIA_TEAM )' 'English-only enemy/militia voice fallback guard missing.'

Write-Host 'PASS: UI / Tactical Information / Audio / Localization source invariants.'
