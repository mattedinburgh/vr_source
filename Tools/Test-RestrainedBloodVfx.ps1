param(
    [string]$GameRoot = "",
    [switch]$SkipAssetCheck
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2

$SourceRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$SourcePath = Join-Path $SourceRoot "Tactical\Soldier Control.cpp"
if (-not (Test-Path $SourcePath -PathType Leaf)) {
    throw "Soldier Control.cpp not found: $SourcePath"
}

if (-not $GameRoot) {
    $candidate = Split-Path -Parent $SourceRoot
    if ((Test-Path (Join-Path $candidate "Data-Vengeance")) -and
        (Test-Path (Join-Path $candidate "Data-AIMv53"))) {
        $GameRoot = $candidate
    }
}

$text = [IO.File]::ReadAllText($SourcePath)
$results = New-Object System.Collections.ArrayList
function Add-Check {
    param([string]$Name, [bool]$Pass, [string]$Detail)
    [void]$results.Add([pscustomobject]@{ Check=$Name; Pass=$Pass; Detail=$Detail })
}
Add-Check "Blood-and-gore option guard" `
    $text.Contains('!gGameSettings.fOptions[ TOPTION_BLOOD_N_GORE ]') `
    "Custom spray helper obeys the player Blood & Gore option."
Add-Check "No custom directional blood trail" `
    (-not $text.Contains('DropVRDirectionalBloodTrail')) `
    "Persistent blood/trails stay owned by the core damage system."
Add-Check "No custom persistent blood decals" `
    (-not $text.Contains('VR_BLOOD_DECAL_')) `
    "Removed near/mid/far custom decal family stays removed."
Add-Check "Ordinary impact mist capped" `
    $text.Contains('__min( 45, sGoreChance )') `
    "Surviving gunshot custom-mist chance remains capped at 45%."
Add-Check "Heavy secondary spray rare" `
    $text.Contains('fHeavyImpact && Random( 100 ) < 20') `
    "Heavy surviving hits add a second small layer only 20% of the time."
Add-Check "Dismemberment severity gate" `
    $text.Contains('sDamage >= 35 && ubWeaponImpact >= 30') `
    "Gunfire dismemberment still requires severe damage and weapon impact."
Add-Check "Fatal mist capped" `
    $text.Contains('__min( 50, sFatalMistChance )') `
    "Non-dismemberment fatal mist chance remains capped at 50%."

$fatalStart = $text.IndexOf('static BOOLEAN HandleVRFatalGunshotReaction')
$fatalEnd = $text.IndexOf('void SOLDIERTYPE::EVENT_SoldierGotHit', $fatalStart)
$fatalBlock = if ($fatalStart -ge 0 -and $fatalEnd -gt $fatalStart) {
    $text.Substring($fatalStart, $fatalEnd - $fatalStart)
} else { "" }
Add-Check "Fatal dispatcher isolated" `
    ($fatalBlock.Length -gt 0) `
    "Fatal gunshot reaction function can be inspected as one policy block."
Add-Check "Gunfire never body-explodes" `
    ($fatalBlock.Length -gt 0 -and -not $fatalBlock.Contains('EVENT_InitNewSoldierAnim( BODYEXPLODING')) `
    "Gunfire fatal dispatcher does not select BODYEXPLODING."
Add-Check "No medium/chunk spray stacking" `
    (-not $fatalBlock.Contains('VR_GORE_SPRAY_MEDIUM.STI') -and -not $fatalBlock.Contains('VR_FATAL_CHUNKS.STI')) `
    "Fatal variants do not reintroduce the old stacked medium/chunk spray packages."

if (-not $SkipAssetCheck) {
    if (-not $GameRoot) { throw "Game root not found; pass -GameRoot or use -SkipAssetCheck." }
    $matches = [regex]::Matches($text, 'TILECACHE\\\\(VR_[A-Z0-9_]+\.STI)')
    $assets = @($matches | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    $missing = @($assets | Where-Object { -not (Test-Path (Join-Path $GameRoot ("Data-Vengeance\TileCache\" + $_)) -PathType Leaf) })
    Add-Check "Referenced VFX assets present" ($missing.Count -eq 0) `
        $(if ($missing.Count -eq 0) { "$($assets.Count) referenced custom STI files present." } else { "Missing: " + ($missing -join ', ') })
}

$results | Format-Table -AutoSize
$failures = @($results | Where-Object { -not $_.Pass })
Write-Host ""
Write-Host ("Restrained blood/VFX QA: {0} check(s), {1} failure(s)" -f $results.Count, $failures.Count)
if ($failures.Count -gt 0) { exit 1 }
exit 0