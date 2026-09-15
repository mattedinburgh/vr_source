param(
    [string]$GameRoot = "C:\\VENGENCE\\Jagged Alliance 2",
    [string]$MetadataRoot = "",
    [string]$OutputDirectory = "",
    [int]$MaxRangerLevels = 2,
    [int]$WatchedBonusPerPoint = 25,
    [int]$MaxWatchedPoints = 4
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($MetadataRoot)) { $MetadataRoot = Join-Path $GameRoot "Data-Vengeance\Tilesets\AdditionalProperties" }
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $env:TEMP "vr_combat_camo_audit" }

$ja2Options = Join-Path $GameRoot "Data-Vengeance\Ja2_Options.INI"
$skillSettings = Join-Path $GameRoot "Data-Vengeance\Skills_Settings.INI"
$backgrounds = Join-Path $GameRoot "Data-Vengeance\TableData\Backgrounds.xml"

foreach ($required in @($MetadataRoot, $ja2Options, $skillSettings, $backgrounds)) {
    if (!(Test-Path -LiteralPath $required)) { throw "Required camouflage audit input not found: $required" }
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

function Read-IniInteger {
    param([string]$Path, [string]$Name)
    $text = Get-Content -LiteralPath $Path -Raw
    $m = [regex]::Match($text, "(?im)^\s*" + [regex]::Escape($Name) + "\s*=\s*(-?\d+)\s*(?:;.*)?$")
    if (!$m.Success) { throw "INI key '$Name' not found in $Path" }
    return [int]$m.Groups[1].Value
}

function Trunc-Div {
    param([int]$Numerator, [int]$Denominator)
    if ($Denominator -eq 0) { throw "division by zero" }
    return [int][Math]::Truncate([double]$Numerator / [double]$Denominator)
}

function Get-CamoAdjustment {
    param(
        [int]$Effectiveness, [int]$Stance, [int]$StanceModifier,
        [int]$WoodAffinity, [int]$DesertAffinity, [int]$UrbanAffinity, [int]$SnowAffinity,
        [bool]$MixedPerfectCamo
    )

    $effectiveStance = [Math]::Max(1, $Stance - $StanceModifier)
    $scalerBase = -(7 - $effectiveStance)
    $scaler = Trunc-Div ($Effectiveness * $scalerBase) 6

    $woodCamo = 0; $desertCamo = 0; $urbanCamo = 0; $snowCamo = 0
    if ($MixedPerfectCamo) {
        $woodCamo = 100; $desertCamo = 100; $urbanCamo = 100; $snowCamo = 100
    }
    else {
        $bestName = "wood"; $bestValue = $WoodAffinity
        if ($DesertAffinity -gt $bestValue) { $bestName = "desert"; $bestValue = $DesertAffinity }
        if ($UrbanAffinity -gt $bestValue) { $bestName = "urban"; $bestValue = $UrbanAffinity }
        if ($SnowAffinity -gt $bestValue) { $bestName = "snow"; $bestValue = $SnowAffinity }
        switch ($bestName) {
            "wood" { $woodCamo = 100 }
            "desert" { $desertCamo = 100 }
            "urban" { $urbanCamo = 100 }
            "snow" { $snowCamo = 100 }
        }
    }

    $result = 0
    $result += Trunc-Div ((Trunc-Div ($woodCamo * $scaler) 100) * $WoodAffinity) 100
    $result += Trunc-Div ((Trunc-Div ($desertCamo * $scaler) 100) * $DesertAffinity) 100
    $result += Trunc-Div ((Trunc-Div ($urbanCamo * $scaler) 100) * $UrbanAffinity) 100
    $result += Trunc-Div ((Trunc-Div ($snowCamo * $scaler) 100) * $SnowAffinity) 100

    return [Math]::Max(-100, [Math]::Min(0, $result))
}

$baseEffectiveness = Read-IniInteger $ja2Options "COVER_SYSTEM_CAMOUFLAGE_EFFECTIVENESS"
$stanceEffectiveness = Read-IniInteger $ja2Options "COVER_SYSTEM_STANCE_EFFECTIVENESS"
$movementEffectiveness = Read-IniInteger $ja2Options "COVER_SYSTEM_MOVEMENT_EFFECTIVENESS"
$stealthEffectiveness = Read-IniInteger $ja2Options "COVER_SYSTEM_STEALTH_EFFECTIVENESS"
$rangerBonus = Read-IniInteger $skillSettings "CAMO_EFFECTIVENESS_BONUS_PERCENT"

[xml]$backgroundXml = Get-Content -LiteralPath $backgrounds -Raw
$backgroundCamoValues = @()
foreach ($node in $backgroundXml.SelectNodes("//camo")) {
    $v = 0
    if ([int]::TryParse($node.InnerText.Trim(), [ref]$v)) { $backgroundCamoValues += $v }
}
$maxBackgroundCamo = if ($backgroundCamoValues.Count -gt 0) { ($backgroundCamoValues | Measure-Object -Maximum).Maximum } else { 0 }
$maxEffectiveness = [Math]::Max(-100, [Math]::Min(100, $baseEffectiveness + $maxBackgroundCamo + $rangerBonus * $MaxRangerLevels))

$stances = @(
    [pscustomobject]@{Name="standing";Value=6},
    [pscustomobject]@{Name="crouch";Value=3},
    [pscustomobject]@{Name="prone";Value=1}
)

$rows = New-Object System.Collections.Generic.List[object]

foreach ($file in (Get-ChildItem -LiteralPath $MetadataRoot -Filter *.xml -File | Sort-Object Name)) {
    [xml]$x = Get-Content -LiteralPath $file.FullName -Raw
    $n = $x.ADDITIONALTILEPROPERTIES
    if ($null -eq $n) { continue }

    $wood = [int]$n.bWoodCamoAffinity
    $desert = [int]$n.bDesertCamoAffinity
    $urban = [int]$n.bUrbanCamoAffinity
    $snow = [int]$n.bSnowCamoAffinity
    $stanceModifier = [int]$n.bCamoStanceModifer

    foreach ($stance in $stances) {
        $single = Get-CamoAdjustment $maxEffectiveness $stance.Value $stanceModifier $wood $desert $urban $snow $false
        $mixed = Get-CamoAdjustment $maxEffectiveness $stance.Value $stanceModifier $wood $desert $urban $snow $true
        $tracked1 = [Math]::Min(0, $mixed + $WatchedBonusPerPoint)
        $trackedMax = [Math]::Min(0, $mixed + $WatchedBonusPerPoint * $MaxWatchedPoints)

        $rows.Add([pscustomobject]@{
            Tile = $file.Name
            Stance = $stance.Name
            StanceModifier = $stanceModifier
            WoodAffinity = $wood
            DesertAffinity = $desert
            UrbanAffinity = $urban
            SnowAffinity = $snow
            AffinitySum = $wood + $desert + $urban + $snow
            MaxEffectiveness = $maxEffectiveness
            SinglePerfectCamoAdj = $single
            MixedPerfectCamoAdj = $mixed
            NominalSightPercentInitial = [Math]::Max(0, 100 + $mixed)
            NominalSightPercentAfter1Watch = [Math]::Max(0, 100 + $tracked1)
            NominalSightPercentAfterMaxWatch = [Math]::Max(0, 100 + $trackedMax)
            InitialMinus100 = ($mixed -le -100)
            StillMinus100After1Watch = ($tracked1 -le -100)
        })
    }
}

$csv = Join-Path $OutputDirectory "camouflage_visibility_matrix.csv"
$rows | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding UTF8

$worstInitial = $rows | Sort-Object MixedPerfectCamoAdj | Select-Object -First 1
$worstTracked = $rows | Sort-Object NominalSightPercentAfter1Watch | Select-Object -First 1
$initialMinus100 = @($rows | Where-Object InitialMinus100)
$trackedMinus100 = @($rows | Where-Object StillMinus100After1Watch)

$summary = [pscustomobject]@{
    MetadataFiles = @($rows | Select-Object -ExpandProperty Tile -Unique).Count
    BaseCamoEffectiveness = $baseEffectiveness
    MaxBackgroundCamo = $maxBackgroundCamo
    RangerBonusPerLevel = $rangerBonus
    MaxRangerLevels = $MaxRangerLevels
    MaxCombinedCamoEffectiveness = $maxEffectiveness
    StanceEffectiveness = $stanceEffectiveness
    MovementEffectiveness = $movementEffectiveness
    StealthEffectiveness = $stealthEffectiveness
    WatchedBonusPerPoint = $WatchedBonusPerPoint
    MaxWatchedPoints = $MaxWatchedPoints
    InitialMinus100Cases = $initialMinus100.Count
    AfterOneWatchMinus100Cases = $trackedMinus100.Count
    WorstInitialTile = $worstInitial.Tile
    WorstInitialStance = $worstInitial.Stance
    WorstInitialAdjustment = $worstInitial.MixedPerfectCamoAdj
    WorstInitialNominalSightPercent = $worstInitial.NominalSightPercentInitial
    WorstAfterOneWatchTile = $worstTracked.Tile
    WorstAfterOneWatchStance = $worstTracked.Stance
    WorstAfterOneWatchNominalSightPercent = $worstTracked.NominalSightPercentAfter1Watch
    MatrixCsv = $csv
}

$summaryJson = Join-Path $OutputDirectory "camouflage_visibility_summary.json"
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryJson -Encoding UTF8
$summary | Format-List | Out-String | Write-Host

if ($trackedMinus100.Count -gt 0) {
    Write-Warning "Tracked-target camouflage still reaches -100 after one watched-location point."
    exit 2
}

Write-Host "PASS: one watched-location point prevents absolute camouflage disappearance in the audited matrix."
Write-Host "Note: nominal 0% still has the engine minimum one-tile LOS floor."
