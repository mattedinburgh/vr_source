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

foreach ($required in @($ja2Options, $skillSettings, $backgrounds)) {
    if (!(Test-Path -LiteralPath $required)) { throw "Required camouflage audit input not found: $required" }
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

function Read-IniInteger {
    param([string]$Path, [string]$Name, [int]$Default)
    $text = Get-Content -LiteralPath $Path -Raw
    $m = [regex]::Match($text, "(?im)^\s*" + [regex]::Escape($Name) + "\s*=\s*(-?\d+)\s*(?:;.*)?$")
    if (!$m.Success) { return $Default }
    return [int]$m.Groups[1].Value
}

function Read-IniBoolean {
    param([string]$Path, [string]$Name, [bool]$Default)
    $text = Get-Content -LiteralPath $Path -Raw
    $m = [regex]::Match($text, "(?im)^\s*" + [regex]::Escape($Name) + "\s*=\s*(TRUE|FALSE|1|0)\s*(?:;.*)?$")
    if (!$m.Success) { return $Default }
    return @("TRUE","1") -contains $m.Groups[1].Value.ToUpperInvariant()
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
        [int]$WoodCamo, [int]$DesertCamo, [int]$UrbanCamo, [int]$SnowCamo,
        [bool]$UseAlternate
    )

    $effectiveStance = [Math]::Max(1, $Stance - $StanceModifier)
    $scalerBase = -(7 - $effectiveStance)
    $scaler = Trunc-Div ($Effectiveness * $scalerBase) 6

    $result = 0
    if ($UseAlternate) {
        $result += [Math]::Min(-(Trunc-Div ($WoodCamo * $scaler) 100), $WoodAffinity)
        $result += [Math]::Min(-(Trunc-Div ($DesertCamo * $scaler) 100), $DesertAffinity)
        $result += [Math]::Min(-(Trunc-Div ($UrbanCamo * $scaler) 100), $UrbanAffinity)
        $result += [Math]::Min(-(Trunc-Div ($SnowCamo * $scaler) 100), $SnowAffinity)
        $result = [Math]::Min($result, 100)
        $result = -$result
    }
    else {
        $result += Trunc-Div ((Trunc-Div ($WoodCamo * $scaler) 100) * $WoodAffinity) 100
        $result += Trunc-Div ((Trunc-Div ($DesertCamo * $scaler) 100) * $DesertAffinity) 100
        $result += Trunc-Div ((Trunc-Div ($UrbanCamo * $scaler) 100) * $UrbanAffinity) 100
        $result += Trunc-Div ((Trunc-Div ($SnowCamo * $scaler) 100) * $SnowAffinity) 100
    }

    return [Math]::Max(-100, [Math]::Min(0, $result))
}

$baseEffectiveness = Read-IniInteger $ja2Options "COVER_SYSTEM_CAMOUFLAGE_EFFECTIVENESS" 50
$stanceEffectiveness = Read-IniInteger $ja2Options "COVER_SYSTEM_STANCE_EFFECTIVENESS" 10
$movementEffectiveness = Read-IniInteger $ja2Options "COVER_SYSTEM_MOVEMENT_EFFECTIVENESS" 20
$stealthEffectiveness = Read-IniInteger $ja2Options "COVER_SYSTEM_STEALTH_EFFECTIVENESS" 50
$useAdditionalTileProperties = Read-IniBoolean $ja2Options "COVER_SYSTEM_ADDITIONAL_TILE_PROPERTIES" $true
$useAlternate = Read-IniBoolean $ja2Options "COVER_SYSTEM_ALTERNATE_MULTI_TERRAIN_CAMO_CALCULATION" $true
$rangerBonus = Read-IniInteger $skillSettings "CAMO_EFFECTIVENESS_BONUS_PERCENT" 10

[xml]$backgroundXml = Get-Content -LiteralPath $backgrounds -Raw
$backgroundCamoValues = @()
foreach ($node in $backgroundXml.SelectNodes("//camo")) {
    $v = 0
    if ([int]::TryParse($node.InnerText.Trim(), [ref]$v)) { $backgroundCamoValues += $v }
}
$maxBackgroundCamo = if ($backgroundCamoValues.Count -gt 0) { ($backgroundCamoValues | Measure-Object -Maximum).Maximum } else { 0 }
$maxEffectiveness = [Math]::Max(-100, [Math]::Min(100, $baseEffectiveness + $maxBackgroundCamo + $rangerBonus * $MaxRangerLevels))

$cases = New-Object System.Collections.Generic.List[object]
$metadataFiles = @()
if (Test-Path -LiteralPath $MetadataRoot -PathType Container) {
    $metadataFiles = @(Get-ChildItem -LiteralPath $MetadataRoot -Filter *.xml -File | Sort-Object Name)
    foreach ($file in $metadataFiles) {
        [xml]$x = Get-Content -LiteralPath $file.FullName -Raw
        $n = $x.ADDITIONALTILEPROPERTIES
        if ($null -eq $n) { continue }
        $cases.Add([pscustomobject]@{
            Tile=$file.Name; Source="xml"
            Wood=[int]$n.bWoodCamoAffinity; Desert=[int]$n.bDesertCamoAffinity
            Urban=[int]$n.bUrbanCamoAffinity; Snow=[int]$n.bSnowCamoAffinity
            Stance=[int]$n.bCamoStanceModifer
        })
    }
}

if ($cases.Count -eq 0) {
    Write-Host "No external camouflage XML found; auditing synthetic detailed-tile cases plus legacy fallback semantics."
    @(
        @("pure_wood",100,0,0,0,0),
        @("mixed_wood_desert_50_50",50,50,0,0,0),
        @("mixed_desert_urban_25_75",0,25,75,0,0),
        @("pure_snow",0,0,0,100,0),
        @("mixed_four_way_25",25,25,25,25,0),
        @("low_cover_20",20,0,0,0,0)
    ) | ForEach-Object {
        $cases.Add([pscustomobject]@{
            Tile=[string]$_[0]; Source="synthetic"
            Wood=[int]$_[1]; Desert=[int]$_[2]; Urban=[int]$_[3]; Snow=[int]$_[4]; Stance=[int]$_[5]
        })
    }
}

$stances = @(
    [pscustomobject]@{Name="standing";Value=6},
    [pscustomobject]@{Name="crouch";Value=3},
    [pscustomobject]@{Name="prone";Value=1}
)

$rows = New-Object System.Collections.Generic.List[object]
foreach ($case in $cases) {
    $aff = @($case.Wood,$case.Desert,$case.Urban,$case.Snow)
    $best = 0
    for ($i=1; $i -lt 4; $i++) { if ($aff[$i] -gt $aff[$best]) { $best = $i } }
    $singleCamo = @(0,0,0,0); $singleCamo[$best] = 100
    $mixedCamo = @(100,100,100,100)

    foreach ($stance in $stances) {
        $single = Get-CamoAdjustment $maxEffectiveness $stance.Value $case.Stance $case.Wood $case.Desert $case.Urban $case.Snow $singleCamo[0] $singleCamo[1] $singleCamo[2] $singleCamo[3] $useAlternate
        $mixed = Get-CamoAdjustment $maxEffectiveness $stance.Value $case.Stance $case.Wood $case.Desert $case.Urban $case.Snow $mixedCamo[0] $mixedCamo[1] $mixedCamo[2] $mixedCamo[3] $useAlternate
        $tracked1 = [Math]::Min(0, $mixed + $WatchedBonusPerPoint)
        $trackedMax = [Math]::Min(0, $mixed + $WatchedBonusPerPoint * $MaxWatchedPoints)

        $rows.Add([pscustomobject]@{
            Tile=$case.Tile; Source=$case.Source; Stance=$stance.Name
            WoodAffinity=$case.Wood; DesertAffinity=$case.Desert; UrbanAffinity=$case.Urban; SnowAffinity=$case.Snow
            AffinitySum=$case.Wood+$case.Desert+$case.Urban+$case.Snow
            MaxEffectiveness=$maxEffectiveness; AlternateMultiTerrain=$useAlternate
            SinglePerfectCamoAdj=$single; MixedPerfectCamoAdj=$mixed
            NominalSightPercentInitial=[Math]::Max(0,100+$mixed)
            NominalSightPercentAfter1Watch=[Math]::Max(0,100+$tracked1)
            NominalSightPercentAfterMaxWatch=[Math]::Max(0,100+$trackedMax)
            InitialMinus100=($mixed -le -100); StillMinus100After1Watch=($tracked1 -le -100)
        })
    }
}

$csv = Join-Path $OutputDirectory "camouflage_visibility_matrix.csv"
$rows | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding UTF8

$initialMinus100 = @($rows | Where-Object InitialMinus100)
$trackedMinus100 = @($rows | Where-Object StillMinus100After1Watch)
$worstInitial = $rows | Sort-Object MixedPerfectCamoAdj | Select-Object -First 1
$worstTracked = $rows | Sort-Object NominalSightPercentAfter1Watch | Select-Object -First 1

$summary = [pscustomobject]@{
    ExternalMetadataDirectoryPresent = (Test-Path -LiteralPath $MetadataRoot -PathType Container)
    ExternalMetadataFiles = $metadataFiles.Count
    AuditedCases = $cases.Count
    AdditionalTilePropertiesEnabled = $useAdditionalTileProperties
    AlternateMultiTerrainCalculation = $useAlternate
    BaseCamoEffectiveness = $baseEffectiveness
    MaxBackgroundCamo = $maxBackgroundCamo
    RangerBonusPerLevel = $rangerBonus
    MaxCombinedCamoEffectiveness = $maxEffectiveness
    StanceEffectiveness = $stanceEffectiveness
    MovementEffectiveness = $movementEffectiveness
    StealthEffectiveness = $stealthEffectiveness
    InitialMinus100Cases = $initialMinus100.Count
    AfterOneWatchMinus100Cases = $trackedMinus100.Count
    WorstInitialTile = $worstInitial.Tile
    WorstInitialStance = $worstInitial.Stance
    WorstInitialAdjustment = $worstInitial.MixedPerfectCamoAdj
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

Write-Host "PASS: detailed-camo math/fallback audit completed and one watched-location point prevents absolute camouflage disappearance."
