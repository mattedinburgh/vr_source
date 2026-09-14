param(
    [int]$Workers = 12,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$GameRoot = Split-Path -Parent $ScriptRoot
$DataRoot = Join-Path $GameRoot "Data-Vengeance"
$TableRoot = Join-Path $DataRoot "TableData\LogicalBodyTypes"
$PaletteRoot = Join-Path $DataRoot "Palettes"
$AnimRoot = Join-Path $DataRoot "Anims\LOBOT"
$Marker = Join-Path $AnimRoot "VR_EQUIPMENT.READY"

# Pin the matching Vengeance LOBOT catalog too. Source + catalog + upstream art
# must form one reproducible deployment set.
$VrRef = "e2c9caa9a1f1b63c04d3633fd26b1af5d254eaf8"
$VrRaw = "https://raw.githubusercontent.com/mattedinburgh/vr_gamedir/$VrRef/Data-Vengeance/TableData/LogicalBodyTypes"
# Pin the external art revision so the same Vengeance commit always resolves the
# same filenames and bytes. Do not deploy against a moving upstream master.
$UpstreamRef = "bdcf501e6b4db072933357a71f97243b7ab759e1"
$UpstreamRaw = "https://raw.githubusercontent.com/1dot13/gamedir/$UpstreamRef/Data"
$Upstream113Raw = "https://raw.githubusercontent.com/1dot13/gamedir/$UpstreamRef/Data-1.13"
$VrGameRaw = "https://raw.githubusercontent.com/mattedinburgh/vr_gamedir/$VrRef/Data-AIMv53"
$UpstreamApi = "https://api.github.com/repos/1dot13/gamedir"

Write-Host "Vengeance visible-equipment deployment"
Write-Host "Source repo : $ScriptRoot"
Write-Host "Game root   : $GameRoot"
Write-Host ""

if (-not (Test-Path $GameRoot -PathType Container)) {
    throw "Game root not found: $GameRoot"
}

New-Item -ItemType Directory -Force -Path $TableRoot | Out-Null
New-Item -ItemType Directory -Force -Path $PaletteRoot | Out-Null
New-Item -ItemType Directory -Force -Path $AnimRoot | Out-Null

# Never leave a stale marker behind if deployment is interrupted.
if (Test-Path $Marker) {
    Remove-Item $Marker -Force
}

$configFiles = @(
    "Layers.xml",
    "Palettes.xml",
    "Filters.xml",
    "LogicalBodyTypes.xml",
    "AnimationSurfaces.xml",
    "LBT_RGM/LogicalBodyType_RGM_VR_equipment.xml",
    "LBT_RGM/AnimationSurfaces_RGM_VR_equipment.xml",
    "LBT_BGM/LogicalBodyType_BGM_VR_equipment.xml",
    "LBT_BGM/AnimationSurfaces_BGM_VR_equipment.xml",
    "LBT_RGF/LogicalBodyType_RGF_VR_equipment.xml",
    "LBT_RGF/AnimationSurfaces_RGF_VR_equipment.xml"
)

function Get-UrlFile {
    param(
        [Parameter(Mandatory=$true)][string]$Url,
        [Parameter(Mandatory=$true)][string]$Destination
    )

    $parent = Split-Path -Parent $Destination
    if ($parent) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }

    $last = $null
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Destination
            if ((Test-Path $Destination) -and ((Get-Item $Destination).Length -gt 0)) {
                return
            }
            throw "Downloaded file is empty."
        }
        catch {
            $last = $_
            if ($attempt -lt 3) {
                Start-Sleep -Seconds $attempt
            }
        }
    }
    throw "Failed to download $Url : $last"
}

function Get-UpstreamLobotPathMap {
    param(
        [Parameter(Mandatory=$true)][string[]]$BodyDirs
    )

    $headers = @{ "User-Agent" = "VengeanceReloaded-LOBOT-Port/1.1" }
    $lobotRoot = Invoke-RestMethod -UseBasicParsing -Headers $headers -Uri "$UpstreamApi/contents/Data/Anims/LOBOT?ref=$UpstreamRef"

    $dirSha = @{}
    foreach ($item in $lobotRoot) {
        if ($item.type -eq "dir") {
            $dirSha[$item.name.ToUpperInvariant()] = $item.sha
        }
    }

    $map = New-Object "System.Collections.Generic.Dictionary[string,string]" ([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($bodyDir in $BodyDirs) {
        $dirKey = $bodyDir.ToUpperInvariant()
        if (-not $dirSha.ContainsKey($dirKey)) {
            throw "Upstream LOBOT directory not found: $bodyDir"
        }

        $sha = $dirSha[$dirKey]
        $tree = Invoke-RestMethod -UseBasicParsing -Headers $headers -Uri "$UpstreamApi/git/trees/$($sha)?recursive=1"
        if ($tree.truncated) {
            throw "GitHub returned a truncated LOBOT tree for $bodyDir; refusing an incomplete deployment."
        }

        foreach ($node in $tree.tree) {
            if ($node.type -ne "blob") { continue }

            $actualRelative = "Anims\LOBOT\$bodyDir\" + $node.path.Replace("/", "\")
            $map[$actualRelative] = $actualRelative
        }
    }

    return ,$map
}

Write-Host "Refreshing Vengeance LOBOT configuration..."
foreach ($relative in $configFiles) {
    $urlRel = $relative.Replace("\", "/")
    $destination = Join-Path $TableRoot $relative
    Get-UrlFile -Url "$VrRaw/$urlRel" -Destination $destination
}


# Cross-reference the upstream 1.13 item IDs used by Filters.xml against the
# actual AIMv53 item table used by this Vengeance install.  Never assume an
# upstream numeric uiIndex still means the same object.
function Normalize-EquipmentItemName {
    param([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) { return "" }

    $s = $Value.Trim().ToLowerInvariant().Normalize([Text.NormalizationForm]::FormD)
    $builder = New-Object Text.StringBuilder
    foreach ($ch in $s.ToCharArray()) {
        if ([Globalization.CharUnicodeInfo]::GetUnicodeCategory($ch) -ne [Globalization.UnicodeCategory]::NonSpacingMark) {
            [void]$builder.Append($ch)
        }
    }

    $s = $builder.ToString().Replace("&", " and ")
    $s = [regex]::Replace($s, '[^a-z0-9]+', ' ')
    return [regex]::Replace($s.Trim(), '\s+', ' ')
}

function Read-EquipmentItemCatalog {
    param([Parameter(Mandatory=$true)][string]$Path)

    [xml]$doc = [System.IO.File]::ReadAllText($Path)
    $byId = @{}
    $lookups = @{
        Name = @{}
        Long = @{}
        BR = @{}
    }

    foreach ($node in $doc.ITEMLIST.ITEM) {
        $id = 0
        if (-not [int]::TryParse([string]$node.uiIndex, [ref]$id)) { continue }

        $record = [pscustomobject]@{
            Id = $id
            Name = [string]$node.szItemName
            Long = [string]$node.szLongItemName
            BR = [string]$node.szBRName
            ItemClass = [string]$node.usItemClass
        }
        $byId[$id] = $record

        foreach ($spec in @(
            @("Name", $record.Name),
            @("Long", $record.Long),
            @("BR", $record.BR)
        )) {
            $key = Normalize-EquipmentItemName $spec[1]
            if (-not $key) { continue }
            $table = $lookups[$spec[0]]
            if (-not $table.ContainsKey($key)) {
                $table[$key] = New-Object System.Collections.ArrayList
            }
            if (-not $table[$key].Contains($id)) {
                [void]$table[$key].Add($id)
            }
        }
    }

    return [pscustomobject]@{
        ById = $byId
        Lookups = $lookups
    }
}

function Test-EquipmentItemSemanticMatch {
    param($A, $B)
    if ($null -eq $A -or $null -eq $B) { return $false }

    foreach ($pair in @(
        @($A.Long, $B.Long),
        @($A.BR, $B.BR),
        @($A.Name, $B.Name)
    )) {
        $left = Normalize-EquipmentItemName $pair[0]
        $right = Normalize-EquipmentItemName $pair[1]
        if ($left -and $left -eq $right) { return $true }
    }
    return $false
}

function Resolve-EquipmentTargetIds {
    param(
        [int]$SourceId,
        $SourceCatalog,
        $TargetCatalog
    )

    if (-not $SourceCatalog.ById.ContainsKey($SourceId)) {
        # Target-only AIMNAS/Vengeance IDs are deliberately left untouched.
        return @($SourceId)
    }

    $src = $SourceCatalog.ById[$SourceId]
    if ($TargetCatalog.ById.ContainsKey($SourceId) -and
        (Test-EquipmentItemSemanticMatch $src $TargetCatalog.ById[$SourceId])) {
        return @($SourceId)
    }

    $candidates = New-Object "System.Collections.Generic.HashSet[int]"
    foreach ($spec in @(
        @("Long", $src.Long),
        @("BR", $src.BR),
        @("Name", $src.Name)
    )) {
        $key = Normalize-EquipmentItemName $spec[1]
        if (-not $key) { continue }
        $table = $TargetCatalog.Lookups[$spec[0]]
        if ($table.ContainsKey($key)) {
            foreach ($id in $table[$key]) { [void]$candidates.Add([int]$id) }
        }
    }

    if ($candidates.Count -eq 0) { return @() }

    $bestScore = -1
    $best = New-Object System.Collections.ArrayList
    foreach ($id in $candidates) {
        $dst = $TargetCatalog.ById[$id]
        $score = 0
        if ((Normalize-EquipmentItemName $src.Long) -and
            (Normalize-EquipmentItemName $src.Long) -eq (Normalize-EquipmentItemName $dst.Long)) { $score += 16 }
        if ((Normalize-EquipmentItemName $src.BR) -and
            (Normalize-EquipmentItemName $src.BR) -eq (Normalize-EquipmentItemName $dst.BR)) { $score += 8 }
        if ((Normalize-EquipmentItemName $src.Name) -and
            (Normalize-EquipmentItemName $src.Name) -eq (Normalize-EquipmentItemName $dst.Name)) { $score += 4 }
        if ($src.ItemClass -and $src.ItemClass -eq $dst.ItemClass) { $score += 2 }

        if ($score -gt $bestScore) {
            $bestScore = $score
            $best.Clear()
            [void]$best.Add([int]$id)
        }
        elseif ($score -eq $bestScore) {
            [void]$best.Add([int]$id)
        }
    }

    return @($best | Sort-Object -Unique)
}

function Add-IdsToNamedFilter {
    param(
        [Parameter(Mandatory=$true)][xml]$Document,
        [Parameter(Mandatory=$true)][string]$FilterName,
        [Parameter(Mandatory=$true)][string]$TagName,
        [Parameter(Mandatory=$true)][int[]]$Ids
    )

    $filter = $Document.Filters.Filter | Where-Object { $_.name -eq $FilterName } | Select-Object -First 1
    if ($null -eq $filter) { throw "LOBOT filter not found: $FilterName" }

    $node = $filter.SelectSingleNode(".//$TagName")
    if ($null -eq $node) { throw "LOBOT criterion $TagName not found in filter $FilterName" }

    $all = New-Object "System.Collections.Generic.HashSet[int]"
    foreach ($token in ([string]$node.InnerText -split '[,\s]+')) {
        $n = 0
        if ([int]::TryParse($token, [ref]$n)) { [void]$all.Add($n) }
    }
    foreach ($id in $Ids) { [void]$all.Add([int]$id) }
    $node.InnerText = (@($all) | Sort-Object) -join ", "
}

Write-Host "Cross-referencing 1.13 equipment IDs against AIMv53..."

$sourceItemsTemp = Join-Path $env:TEMP "vr_lobot_source_items_$PID.xml"
$targetItemsTemp = $null
try {
    Get-UrlFile -Url "$Upstream113Raw/TableData/Items/Items.xml" -Destination $sourceItemsTemp

    $targetItemsPath = Join-Path $GameRoot "Data-AIMv53\TableData\Items\Items.xml"
    if (-not (Test-Path $targetItemsPath)) {
        $targetItemsTemp = Join-Path $env:TEMP "vr_lobot_target_items_$PID.xml"
        Get-UrlFile -Url "$VrGameRaw/TableData/Items/Items.xml" -Destination $targetItemsTemp
        $targetItemsPath = $targetItemsTemp
        Write-Host "AIMv53 item table source       : pinned vr_gamedir"
    }
    else {
        Write-Host "AIMv53 item table source       : local game install"
    }

    $sourceCatalog = Read-EquipmentItemCatalog $sourceItemsTemp
    $targetCatalog = Read-EquipmentItemCatalog $targetItemsPath

    $filtersPath = Join-Path $TableRoot "Filters.xml"
    $filtersText = [System.IO.File]::ReadAllText($filtersPath)
    $inventoryTags = @(
        "HELMETPOS","VESTPOS","LEGPOS","HEAD1POS","HEAD2POS","HANDPOS","SECONDHANDPOS",
        "VESTPOCKPOS","LTHIGHPOCKPOS","RTHIGHPOCKPOS","CPACKPOCKPOS","BPACKPOCKPOS",
        "GUNSLINGPOCKPOS","KNIFEPOCKPOS",
        "HELMETPOSATTACHMENT0","HELMETPOSATTACHMENT1","HELMETPOSATTACHMENT2","HELMETPOSATTACHMENT3",
        "LEGPOSATTACHMENT0","LEGPOSATTACHMENT1","LEGPOSATTACHMENT2","LEGPOSATTACHMENT3",
        "VESTPOSATTACHMENT0","VESTPOSATTACHMENT1","VESTPOSATTACHMENT2","VESTPOSATTACHMENT3"
    )
    $tagAlternation = ($inventoryTags | ForEach-Object { [regex]::Escape($_) }) -join "|"
    $criterionPattern = "(?is)<(?<tag>$tagAlternation)(?<attrs>\b[^>]*)>(?<ids>[^<]*)</\k<tag>>"

    $changedIds = 0
    $unresolvedIds = New-Object "System.Collections.Generic.HashSet[int]"
    $filtersText = [regex]::Replace($filtersText, $criterionPattern, {
        param($m)

        $mapped = New-Object "System.Collections.Generic.HashSet[int]"
        foreach ($token in ($m.Groups["ids"].Value -split '[,\s]+')) {
            $sourceId = 0
            if (-not [int]::TryParse($token, [ref]$sourceId)) { continue }

            $resolved = @(Resolve-EquipmentTargetIds $sourceId $sourceCatalog $targetCatalog)
            if ($resolved.Count -eq 0) {
                [void]$unresolvedIds.Add($sourceId)
                [void]$mapped.Add(65535)
                $changedIds++
                continue
            }

            foreach ($targetId in $resolved) {
                [void]$mapped.Add([int]$targetId)
                if ([int]$targetId -ne $sourceId) { $changedIds++ }
            }
        }

        $newIds = (@($mapped) | Sort-Object) -join ", "
        return "<$($m.Groups["tag"].Value)$($m.Groups["attrs"].Value)>$newIds</$($m.Groups["tag"].Value)>"
    })

    [System.IO.File]::WriteAllText($filtersPath, $filtersText, (New-Object System.Text.UTF8Encoding($false)))

    # AIMNAS-only equipment has no upstream 1.13 ID to translate.  Map those
    # items explicitly to the nearest stock LOBOT silhouette.
    [xml]$filtersDoc = [System.IO.File]::ReadAllText($filtersPath)
    Add-IdsToNamedFilter $filtersDoc "ZylonVest" "VESTPOS" @(2539,2540)
    Add-IdsToNamedFilter $filtersDoc "KevlarVest" "VESTPOS" @(2527,2528,2529)
    Add-IdsToNamedFilter $filtersDoc "SpectraVest" "VESTPOS" @(2523)
    Add-IdsToNamedFilter $filtersDoc "SWATHelmet" "HELMETPOS" @(2531)
    $filtersDoc.Save($filtersPath)

    Write-Host ("Cross-reference ID rewrites   : {0}" -f $changedIds)
    Write-Host ("Unmatched upstream item IDs   : {0}" -f $unresolvedIds.Count)
    if ($unresolvedIds.Count -gt 0) {
        Write-Host ("  safely disabled (65535)     : {0}" -f ((@($unresolvedIds) | Sort-Object) -join ", "))
    }
    Write-Host "AIMNAS armour mappings        : FrackTac, Polyurethane, Zylon, Ballistic Mask"
}
finally {
    if (Test-Path $sourceItemsTemp) { Remove-Item $sourceItemsTemp -Force -ErrorAction SilentlyContinue }
    if ($targetItemsTemp -and (Test-Path $targetItemsTemp)) { Remove-Item $targetItemsTemp -Force -ErrorAction SilentlyContinue }
}


# LOBOT LayerProp palette attributes are not cosmetic metadata: 1.13 uses
# these palette tables to recolour equipment sprites.  Without them the raw
# STI colours (typically orange/brown debug-looking tones) are rendered.
$paletteFiles = @(
    "Hats.stp",
    "guns_universal.stp",
    "guns_AK.stp",
    "guns_universal_v2.stp",
    "guns_v2_paletteswap.act",
    "grayscale.act",
    "WoodlandCamo.act",
    "UrbanlandCamo.act",
    "DesertlandCamo.act",
    "BlueHats.act",
    "GreenHats.act",
    "swat_blue.act",
    "guns_v2_paletteswap_wood_to_dark.act",
    "Hats_guardian_vest.act",
    "guns_universal_v2_tan_fix.act"
)

Write-Host "Refreshing 1.13 LOBOT palette tables..."
foreach ($palette in $paletteFiles) {
    Get-UrlFile -Url "$UpstreamRaw/Palettes/$palette" -Destination (Join-Path $PaletteRoot $palette)
}

$surfaceCatalogs = @(
    "LBT_RGM/AnimationSurfaces_RGM_VR_equipment.xml",
    "LBT_BGM/AnimationSurfaces_BGM_VR_equipment.xml",
    "LBT_RGF/AnimationSurfaces_RGF_VR_equipment.xml"
)

$assetPaths = New-Object "System.Collections.Generic.HashSet[string]" ([System.StringComparer]::OrdinalIgnoreCase)

foreach ($catalog in $surfaceCatalogs) {
    $catalogPath = Join-Path $TableRoot $catalog
    $text = [System.IO.File]::ReadAllText($catalogPath)

    # These files are parsed as external XML entities inside the parent
    # <AnimSurfaces> element. They must therefore be XML fragments containing
    # only <AnimSurface .../> entries, not standalone XML documents with their
    # own declaration or <AnimSurfaces> root.
    if ($text -match '(?i)<\?xml' -or $text -match '(?i)<\s*/?\s*AnimSurfaces\s*>') {
        throw "Invalid LBT external entity catalog (standalone XML wrapper found): $catalog"
    }

    foreach ($match in [regex]::Matches($text, 'file="([^"]+)"')) {
        $relative = $match.Groups[1].Value
        if ($relative -like "Anims\LOBOT\*") {
            [void]$assetPaths.Add($relative)
        }
    }
}

if ($assetPaths.Count -eq 0) {
    throw "No LOBOT equipment assets were found in the deployed catalogs."
}

Write-Host "Resolving upstream LOBOT filename casing..."
$bodyDirs = New-Object "System.Collections.Generic.HashSet[string]" ([System.StringComparer]::OrdinalIgnoreCase)
foreach ($relative in $assetPaths) {
    $parts = $relative -split '\\'
    if ($parts.Length -lt 4) {
        throw "Unexpected LOBOT asset path: $relative"
    }
    [void]$bodyDirs.Add($parts[2])
}

$upstreamPathMap = Get-UpstreamLobotPathMap -BodyDirs @($bodyDirs)
$unresolved = New-Object System.Collections.ArrayList
$caseCorrections = 0

foreach ($relative in $assetPaths) {
    if (-not $upstreamPathMap.ContainsKey($relative)) {
        [void]$unresolved.Add($relative)
        continue
    }

    $xmlUrlCase = $relative -replace '(?i)\.STI$', '.sti'
    if ($upstreamPathMap[$relative] -cne $xmlUrlCase) {
        $caseCorrections++
    }
}

if ($unresolved.Count -gt 0) {
    Write-Host ""
    Write-Host "References absent from upstream 1.13 LOBOT art:" -ForegroundColor Red
    $unresolved | Select-Object -First 30 | ForEach-Object { Write-Host "  $_" }
    if ($unresolved.Count -gt 30) {
        Write-Host ("  ... and {0} more" -f ($unresolved.Count - 30))
    }
    throw "Visible-equipment catalog contains unresolved upstream assets."
}

Write-Host ("Upstream case corrections   : {0}" -f $caseCorrections)

$pending = New-Object System.Collections.ArrayList
foreach ($relative in $assetPaths) {
    $destination = Join-Path $DataRoot $relative
    if ($Force -or -not (Test-Path $destination) -or (Get-Item $destination).Length -eq 0) {
        [void]$pending.Add($relative)
    }
}

Write-Host ("Equipment surfaces referenced : {0}" -f $assetPaths.Count)
Write-Host ("Equipment surfaces to fetch   : {0}" -f $pending.Count)

if ($pending.Count -gt 0) {
    Add-Type -AssemblyName System.Net.Http
    $handler = New-Object System.Net.Http.HttpClientHandler
    $client = New-Object System.Net.Http.HttpClient($handler)
    $client.Timeout = [TimeSpan]::FromMinutes(5)
    $client.DefaultRequestHeaders.UserAgent.ParseAdd("VengeanceReloaded-LOBOT-Port/1.0")

    try {
        for ($offset = 0; $offset -lt $pending.Count; $offset += $Workers) {
            $lastIndex = [Math]::Min($pending.Count - 1, $offset + $Workers - 1)
            $batch = @($pending[$offset..$lastIndex])
            $jobs = @()

            foreach ($relative in $batch) {
                if (-not $upstreamPathMap.ContainsKey($relative)) {
                    throw "No upstream path resolved for $relative"
                }
                $urlRel = $upstreamPathMap[$relative].Replace("\", "/")
                $url = "$UpstreamRaw/$urlRel"
                $destination = Join-Path $DataRoot $relative
                $parent = Split-Path -Parent $destination
                New-Item -ItemType Directory -Force -Path $parent | Out-Null

                $jobs += [pscustomobject]@{
                    Relative = $relative
                    Url = $url
                    Destination = $destination
                    Task = $client.GetByteArrayAsync($url)
                }
            }

            foreach ($job in $jobs) {
                $ok = $false
                $last = $null

                try {
                    $bytes = $job.Task.GetAwaiter().GetResult()
                    if ($bytes.Length -le 0) { throw "Empty response" }
                    [System.IO.File]::WriteAllBytes($job.Destination, $bytes)
                    $ok = $true
                }
                catch {
                    $last = $_
                }

                if (-not $ok) {
                    # Parallel request failed: retry this one conservatively.
                    for ($attempt = 1; $attempt -le 3 -and -not $ok; $attempt++) {
                        try {
                            $bytes = $client.GetByteArrayAsync($job.Url).GetAwaiter().GetResult()
                            if ($bytes.Length -le 0) { throw "Empty response" }
                            [System.IO.File]::WriteAllBytes($job.Destination, $bytes)
                            $ok = $true
                        }
                        catch {
                            $last = $_
                            if ($attempt -lt 3) { Start-Sleep -Seconds $attempt }
                        }
                    }
                }

                if (-not $ok) {
                    throw "Failed to download $($job.Relative): $last"
                }
            }

            $done = [Math]::Min($pending.Count, $lastIndex + 1)
            Write-Progress -Activity "Downloading visible armour animation layers" -Status "$done / $($pending.Count)" -PercentComplete (($done * 100.0) / $pending.Count)
        }
    }
    finally {
        if ($client) { $client.Dispose() }
        if ($handler) { $handler.Dispose() }
        Write-Progress -Activity "Downloading visible armour animation layers" -Completed
    }
}

Write-Host "Verifying deployed assets..."
$missing = New-Object System.Collections.ArrayList
$totalBytes = [int64]0
$paletteBytes = [int64]0

foreach ($palette in $paletteFiles) {
    $palettePath = Join-Path $PaletteRoot $palette
    if (-not (Test-Path $palettePath) -or (Get-Item $palettePath).Length -eq 0) {
        [void]$missing.Add("Palettes\$palette")
    }
    else {
        $paletteBytes += (Get-Item $palettePath).Length
    }
}
foreach ($relative in $assetPaths) {
    $destination = Join-Path $DataRoot $relative
    if (-not (Test-Path $destination) -or (Get-Item $destination).Length -eq 0) {
        [void]$missing.Add($relative)
    }
    else {
        $totalBytes += (Get-Item $destination).Length
    }
}

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "Missing files:" -ForegroundColor Red
    $missing | Select-Object -First 30 | ForEach-Object { Write-Host "  $_" }
    if ($missing.Count -gt 30) {
        Write-Host ("  ... and {0} more" -f ($missing.Count - 30))
    }
    throw "Visible-equipment deployment incomplete. Enable marker was NOT created."
}

$markerText = @"
Vengeance Reloaded visible tactical equipment
Catalog: mattedinburgh/vr_gamedir $VrRef
Source: 1dot13/gamedir $UpstreamRef Data/Anims/LOBOT art
Mode: overlay-only (native Vengeance body + 1.13 helmet/vest armour layers)
Assets: $($assetPaths.Count)
AssetBytes: $totalBytes
Palettes: $($paletteFiles.Count)
PaletteBytes: $paletteBytes
"@
[System.IO.File]::WriteAllText($Marker, $markerText, [System.Text.Encoding]::ASCII)

Write-Host ""
Write-Host ("Pinned Vengeance catalog      : {0}" -f $VrRef)
Write-Host ("Pinned upstream revision      : {0}" -f $UpstreamRef)
Write-Host "VISIBLE EQUIPMENT ASSETS VERIFIED"
Write-Host ("Files    : {0}" -f $assetPaths.Count)
Write-Host ("Palettes : {0}" -f $paletteFiles.Count)
Write-Host ("Size     : {0:N1} MiB" -f (($totalBytes + $paletteBytes) / 1MB))
Write-Host "Marker: $Marker"
Write-Host ""
Write-Host "Rebuild/run the current install/all-2026-09-12 source. Helmets and torso armour are now eligible for tactical rendering."
