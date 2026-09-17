param(
    [string]$RepoRoot = '',
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepoRoot)) { $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path }
if ([string]::IsNullOrWhiteSpace($OutputPath)) { $OutputPath = Join-Path $PSScriptRoot 'BATTLE_CALLOUT_RECORDING_MANIFEST.csv' }
$quotesPath = Join-Path $RepoRoot 'Tactical\Civ Quotes.cpp'
$quotes = [IO.File]::ReadAllText($quotesPath)

$eventMap = [ordered]@{
    CONTACT='contact'; ADVANCE='advance'; TAKE_COVER='take_cover'; FLANK_LEFT='flank_left';
    FLANK_RIGHT='flank_right'; WITHDRAW='withdraw'; REGROUP='regroup'; RALLY='rally';
    SUPPRESS='suppress'; GRENADE='grenade'; SMOKE='smoke'; HEAVY_WEAPON='heavy_weapon';
    MEDIC='medic'; RELOAD='reload'; OUT_OF_AMMO='out_of_ammo'; CASUALTY='casualty';
    INCOMING='incoming'; SEARCH='search'; REINFORCE='reinforce'; VEHICLE='vehicle';
    CIVILIAN='civilian'; HOLD='hold'; TARGET_DOWN='target_down'
}

$seedMap = @{
    'Fire in the hole!' = @{ Male='Male\war_fire_in_the_hole.ogg'; Female='Female\war_fire_in_the_hole.ogg' }
    'Get down!'         = @{ Male='Male\war_get_down.ogg';         Female='Female\war_get_down.ogg' }
    'Hold!'             = @{ Male='Male\hold.ogg';                 Female='Female\war_hold.ogg' }
    'Medic!'            = @{ Male='Male\war_medic.ogg';            Female='Female\war_medic.ogg' }
    'Reloading!'        = @{ Male='Male\war_reloading.ogg';        Female='Female\war_reloading.ogg' }
    'RPG!'              = @{ Male='Male\war_rpg.ogg';              Female='Female\war_rpg.ogg' }
    'Suppressing fire!' = @{ Male='Male\war_suppressing_fire.ogg'; Female='Female\war_supressing_fire.ogg' }
}
function Get-Priority([string]$event) {
    if ($event -in @('grenade','medic','casualty','incoming','withdraw','out_of_ammo','take_cover')) { return 'P0' }
    if ($event -in @('contact','reload','suppress','target_down','smoke','heavy_weapon','vehicle','civilian')) { return 'P1' }
    return 'P2'
}
$rows = New-Object System.Collections.Generic.List[object]
function Add-RecordingRows([string]$event, [string]$emotion, [int]$variant, [string]$subtitle, [string]$kind) {
    foreach ($sex in @('Male','Female')) {
        $numberSuffix = if ($variant -eq 0) { '' } else { ' ' + ($variant - 1) }
        $relativePath = "Voice\Battlefield\$sex\${event}__${emotion}${numberSuffix}.ogg"
        $canReuseSeed = $emotion -eq 'controlled' -and $kind -eq 'pool' -and $seedMap.ContainsKey($subtitle)
        $seedPath = if ($canReuseSeed) { $seedMap[$subtitle][$sex] } else { '' }
        $rows.Add([pscustomobject]@{
            priority = Get-Priority $event
            event = $event
            emotion = $emotion
            variant = $variant
            sex = $sex
            kind = $kind
            subtitle = $subtitle
            audio_path = $relativePath
            recording_status = if ($canReuseSeed) { 'reuse_seed' } else { 'recording_required' }
            seed_audio_path = $seedPath
            seed_source = if ($canReuseSeed) { 'Kenney Voiceover Pack' } else { '' }
            seed_license = if ($canReuseSeed) { 'CC0 1.0' } else { '' }
        })
    }
}

foreach ($entry in $eventMap.GetEnumerator()) {
    $symbol = $entry.Key
    $event = $entry.Value
    $pattern = "(?s)static const CHAR16 \* const gAICombatLines_${symbol}\[\]\s*\{(?<body>.*?)\};"
    $match = [regex]::Match($quotes, $pattern)
    if (-not $match.Success) { throw "Missing callout pool $symbol" }
    $lineMatches = [regex]::Matches($match.Groups['body'].Value, 'L"\\"(?<text>.*?)\\""')
    if ($lineMatches.Count -ne 10) { throw "Expected 10 variants for $symbol, found $($lineMatches.Count)" }
    for ($i = 0; $i -lt $lineMatches.Count; $i++) {
        Add-RecordingRows $event 'controlled' $i $lineMatches[$i].Groups['text'].Value 'pool'
    }
}
$overrides = @(
    @('take_cover','panicked',0,'Get down!'),
    @('take_cover','distressed',0,'We need cover!'),
    @('withdraw','panicked',0,'Get me out of here!'),
    @('withdraw','panicked',1,"We're being overrun!"),
    @('withdraw','distressed',0,'Fall back, now!'),
    @('withdraw','angry',0,'Back! Move!'),
    @('rally','distressed',0,'Stay with us!'),
    @('suppress','angry',0,'Keep their heads down!'),
    @('medic','panicked',0,'Please help me!'),
    @('medic','panicked',1,"Don't leave me!"),
    @('medic','panicked',2,'Medic! Please!'),
    @('medic','distressed',0,'Help me!'),
    @('medic','distressed',1,'I need a medic!'),
    @('medic','angry',0,'Medic, now!'),
    @('out_of_ammo','panicked',0,"I'm out! Cover me!"),
    @('casualty','panicked',0,'Oh God!'),
    @('casualty','panicked',1,'Please help me!'),
    @('casualty','panicked',2,"I don't want to die!"),
    @('casualty','panicked',3,'Mother!'),
    @('casualty','distressed',0,"I'm hit!"),
    @('casualty','distressed',1,'Help me!'),
    @('casualty','angry',0,"Damn it, I'm hit!"),
    @('incoming','panicked',0,"They're all over us!"),
    @('incoming','distressed',0,"We're taking fire!"),
    @('target_down','angry',0,'Got one!')
)
foreach ($o in $overrides) { Add-RecordingRows $o[0] $o[1] ([int]$o[2]) $o[3] 'emotional_override' }

if ($rows.Count -ne 510) { throw "Expected 510 recording rows, generated $($rows.Count)" }
$duplicates = $rows | Group-Object audio_path | Where-Object Count -gt 1
if ($duplicates) { throw "Duplicate audio paths in generated manifest: $($duplicates.Name -join ', ')" }
$rows | Sort-Object priority,event,emotion,variant,sex | Export-Csv -LiteralPath $OutputPath -NoTypeInformation -Encoding UTF8
Write-Host "Generated $($rows.Count) rows -> $OutputPath"
