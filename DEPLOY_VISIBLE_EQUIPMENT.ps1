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

function Get-XmlChildText {
    param(
        [Parameter(Mandatory=$true)]$Node,
        [Parameter(Mandatory=$true)][string]$Name
    )

    $child = $Node.SelectSingleNode("./$Name")
    if ($null -eq $child) { return "" }
    return [string]$child.InnerText
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
        $idText = Get-XmlChildText $node "uiIndex"
        $id = 0
        if (-not [int]::TryParse($idText, [ref]$id)) { continue }

        # AIMNAS item XML is sparse: optional elements such as szBRName are
        # legitimately omitted on some ITEM nodes. SelectSingleNode keeps the
        # reader StrictMode-safe and treats missing optional fields as empty.
        $record = [pscustomobject]@{
            Id = $id
            Name = Get-XmlChildText $node "szItemName"
            Long = Get-XmlChildText $node "szLongItemName"
            BR = Get-XmlChildText $node "szBRName"
            ItemClass = Get-XmlChildText $node "usItemClass"
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



# Expand the conservative vest+helmet catalog into the remaining equipment-only
# LOBOT layers. Body/clothing/gun layers stay native Vengeance; these are overlays only.

function Find-TargetEquipmentIdsByName {
    param(
        [Parameter(Mandatory=$true)]$TargetCatalog,
        [Parameter(Mandatory=$true)][string[]]$Names
    )

    $ids = New-Object "System.Collections.Generic.HashSet[int]"
    $missingNames = New-Object System.Collections.ArrayList

    foreach ($name in $Names) {
        $key = Normalize-EquipmentItemName $name
        $found = $false

        foreach ($field in @("Long","BR","Name")) {
            $table = $TargetCatalog.Lookups[$field]
            if (-not $table.ContainsKey($key)) { continue }

            foreach ($id in $table[$key]) {
                [void]$ids.Add([int]$id)
                $found = $true
            }
        }

        if (-not $found) {
            [void]$missingNames.Add($name)
        }
    }

    if ($missingNames.Count -gt 0) {
        throw "AIMv53 equipment names not found: $($missingNames -join ', ')"
    }

    return @($ids | Sort-Object)
}

function Expand-VengeanceEquipmentLayers {
    param(
        [Parameter(Mandatory=$true)][string]$Key,
        [Parameter(Mandatory=$true)][string]$GearRelative,
        [Parameter(Mandatory=$true)][string[]]$CatalogRelatives,
        [Parameter(Mandatory=$true)][string]$TargetBodyRelative,
        [Parameter(Mandatory=$true)][string]$TargetCatalogRelative,
        [Parameter(Mandatory=$true)][string[]]$UnsupportedAnimations
    )

    $layersToAdd = @(
        "legarmor",
        "facegear",
        "gasmask",
        "ears",
        "backpack",
        "legrig",
        "legrig_left",
        "knees"
    )

    $tempFiles = New-Object System.Collections.ArrayList

    try {
        $gearTemp = Join-Path $env:TEMP ("vr_lobot_" + $Key + "_gear_" + $PID + ".xml")
        [void]$tempFiles.Add($gearTemp)
        Get-UrlFile -Url "$UpstreamRaw/TableData/LogicalBodyTypes/$GearRelative" -Destination $gearTemp

        [xml]$gearDoc = [System.IO.File]::ReadAllText($gearTemp)

        $targetBodyPath = Join-Path $TableRoot $TargetBodyRelative
        [xml]$targetDoc = [System.IO.File]::ReadAllText($targetBodyPath)

        $addedLayers = 0
        $prunedSurfaces = 0

        foreach ($layerName in $layersToAdd) {
            $alreadyPresent = $targetDoc.SelectSingleNode("/LogicalAnimationSurfaces/Layer[@name='$layerName']")
            if ($null -ne $alreadyPresent) { continue }

            $sourceLayer = $gearDoc.SelectSingleNode("/LogicalAnimationSurfaces/Layer[@name='$layerName']")
            if ($null -eq $sourceLayer) {
                throw "Upstream $Key gear layer not found: $layerName"
            }

            $clone = $targetDoc.ImportNode($sourceLayer, $true)

            foreach ($surface in @($clone.SelectNodes(".//Surface"))) {
                $animationName = ""

                if ($surface.HasAttribute("animsurface")) {
                    $animationName = $surface.GetAttribute("animsurface")
                }
                elseif ($surface.HasAttribute("animstate")) {
                    $animationName = $surface.GetAttribute("animstate")
                }

                if ($animationName -and ($UnsupportedAnimations -contains $animationName)) {
                    [void]$surface.ParentNode.RemoveChild($surface)
                    $prunedSurfaces++
                }
            }

            foreach ($prop in @($clone.SelectNodes("./LayerProp"))) {
                if ($prop.SelectNodes("./Surface").Count -eq 0) {
                    [void]$clone.RemoveChild($prop)
                }
            }

            if ($clone.SelectNodes("./LayerProp").Count -gt 0) {
                [void]$targetDoc.DocumentElement.AppendChild($clone)
                $addedLayers++
            }
        }

        $neededSurfaceNames = New-Object "System.Collections.Generic.HashSet[string]" ([System.StringComparer]::Ordinal)

        foreach ($layerName in $layersToAdd) {
            $layer = $targetDoc.SelectSingleNode("/LogicalAnimationSurfaces/Layer[@name='$layerName']")
            if ($null -eq $layer) { continue }

            foreach ($surface in @($layer.SelectNodes(".//Surface"))) {
                [void]$neededSurfaceNames.Add($surface.GetAttribute("name"))
            }
        }

        $targetCatalogPath = Join-Path $TableRoot $TargetCatalogRelative
        $targetCatalogText = [System.IO.File]::ReadAllText($targetCatalogPath)

        $existingNames = New-Object "System.Collections.Generic.HashSet[string]" ([System.StringComparer]::Ordinal)
        foreach ($entry in [regex]::Matches($targetCatalogText, '(?is)<AnimSurface\b.*?/>')) {
            $nameMatch = [regex]::Match($entry.Value, '\bname="([^"]+)"')
            if ($nameMatch.Success) {
                [void]$existingNames.Add($nameMatch.Groups[1].Value)
            }
        }

        $upstreamDefinitions = @{}
        $catalogNumber = 0

        foreach ($catalogRelative in $CatalogRelatives) {
            $catalogNumber++
            $catalogTemp = Join-Path $env:TEMP ("vr_lobot_" + $Key + "_catalog_" + $catalogNumber + "_" + $PID + ".xml")
            [void]$tempFiles.Add($catalogTemp)

            Get-UrlFile -Url "$UpstreamRaw/TableData/LogicalBodyTypes/$catalogRelative" -Destination $catalogTemp
            $catalogText = [System.IO.File]::ReadAllText($catalogTemp)

            foreach ($entry in [regex]::Matches($catalogText, '(?is)<AnimSurface\b.*?/>')) {
                $nameMatch = [regex]::Match($entry.Value, '\bname="([^"]+)"')
                if ($nameMatch.Success) {
                    $upstreamDefinitions[$nameMatch.Groups[1].Value] = $entry.Value.Trim()
                }
            }
        }

        $append = New-Object System.Collections.ArrayList
        $missingDefinitions = New-Object System.Collections.ArrayList

        foreach ($surfaceName in @($neededSurfaceNames | Sort-Object)) {
            if ($existingNames.Contains($surfaceName)) { continue }

            if (-not $upstreamDefinitions.ContainsKey($surfaceName)) {
                [void]$missingDefinitions.Add($surfaceName)
                continue
            }

            [void]$append.Add($upstreamDefinitions[$surfaceName])
            [void]$existingNames.Add($surfaceName)
        }

        if ($missingDefinitions.Count -gt 0) {
            Write-Host "Missing upstream animation-surface definitions for ${Key}:" -ForegroundColor Red
            $missingDefinitions | Select-Object -First 30 | ForEach-Object { Write-Host "  $_" }
            throw "$Key equipment expansion is incomplete."
        }

        $targetDoc.Save($targetBodyPath)

        if ($append.Count -gt 0) {
            $nl = [Environment]::NewLine
            $targetCatalogText = $targetCatalogText.TrimEnd() + $nl + ($append -join $nl) + $nl
            [System.IO.File]::WriteAllText($targetCatalogPath, $targetCatalogText, (New-Object System.Text.UTF8Encoding($false)))
        }

        Write-Host ("{0} equipment layers added    : {1} (surfaces +{2}, unsupported pruned {3})" -f $Key, $addedLayers, $append.Count, $prunedSurfaces)
    }
    finally {
        foreach ($tempFile in $tempFiles) {
            if (Test-Path $tempFile) {
                Remove-Item $tempFile -Force -ErrorAction SilentlyContinue
            }
        }
    }
}

Write-Host "Expanding equipment-only LOBOT layers..."

$bodySpecs = @(
    [pscustomobject]@{
        Key = "RGM"
        Gear = "LBT_RGM/LogicalBodyType_RGM_gear.xml"
        Catalogs = @("LBT_RGM/AnimationSurfaces_LBT_RGM.xml","LBT_RGM/AnimationSurfaces_LBT_RGM_moregear.xml")
        TargetBody = "LBT_RGM/LogicalBodyType_RGM_VR_equipment.xml"
        TargetCatalog = "LBT_RGM/AnimationSurfaces_RGM_VR_equipment.xml"
        Unsupported = @("RGMBAYONET_S_P","RGMBAYONET_S_S","RGMCROUCH_D_RDY","RGMCROUCH_P_RDY","RGMCROUCH_R_RDY")
    },
    [pscustomobject]@{
        Key = "BGM"
        Gear = "LBT_BGM/LogicalBodyType_BGM_gear.xml"
        Catalogs = @("LBT_BGM/AnimationSurfaces_BGM.xml","LBT_BGM/AnimationSurfaces_BGM_moregear.xml")
        TargetBody = "LBT_BGM/LogicalBodyType_BGM_VR_equipment.xml"
        TargetCatalog = "LBT_BGM/AnimationSurfaces_BGM_VR_equipment.xml"
        Unsupported = @("BGMBAYONET_S_P","BGMBAYONET_S_S","BGMCROUCH_D_RDY","BGMCROUCH_P_RDY","BGMCROUCH_R_RDY")
    },
    [pscustomobject]@{
        Key = "RGF"
        Gear = "LBT_RGF/LogicalBodyType_RGF_gear.xml"
        Catalogs = @("LBT_RGF/AnimationSurfaces_LBT_RGF.xml","LBT_RGF/AnimationSurfaces_LBT_RGF_moregear.xml")
        TargetBody = "LBT_RGF/LogicalBodyType_RGF_VR_equipment.xml"
        TargetCatalog = "LBT_RGF/AnimationSurfaces_RGF_VR_equipment.xml"
        Unsupported = @("RGFBAYONET_S_P","RGFBAYONET_S_S","RGFCROUCH_D_RDY","RGFCROUCH_P_RDY","RGFCROUCH_R_RDY","RGFLOWKICK","RGFSPINKICK","RGF_LOOK","RGF_PULL","RGF_SPIT","RGF_SQUISH")
    }
)

foreach ($spec in $bodySpecs) {
    Expand-VengeanceEquipmentLayers -Key $spec.Key -GearRelative $spec.Gear -CatalogRelatives $spec.Catalogs -TargetBodyRelative $spec.TargetBody -TargetCatalogRelative $spec.TargetCatalog -UnsupportedAnimations $spec.Unsupported
}


# Add the matching 1.13 base-body and gun layers. These layers are used by the
# renderer only when the complete logical model for the current frame is
# available; Vengeance-native rendering remains the fallback for VR-only
# animations and AIMNAS weapons without a safe 1.13 visual mapping.
function Expand-VengeanceFullModelLayers {
    param(
        [Parameter(Mandatory=$true)][string]$Key,
        [Parameter(Mandatory=$true)][string]$BaseRelative,
        [Parameter(Mandatory=$true)][string]$GearRelative,
        [Parameter(Mandatory=$true)][string]$GunsRelative,
        [Parameter(Mandatory=$true)][string]$GunPaletteRelative,
        [Parameter(Mandatory=$true)][string[]]$CatalogRelatives,
        [Parameter(Mandatory=$true)][string]$TargetBodyRelative,
        [Parameter(Mandatory=$true)][string]$TargetCatalogRelative,
        [Parameter(Mandatory=$true)][string[]]$UnsupportedAnimations
    )

    $temps = New-Object System.Collections.ArrayList

    function Get-PrunedLayerProps {
        param(
            [Parameter(Mandatory=$true)][xml]$SourceDocument,
            [Parameter(Mandatory=$true)][string]$LayerName
        )

        $result = New-Object System.Collections.ArrayList
        $sourceLayer = $SourceDocument.SelectSingleNode("/LogicalAnimationSurfaces/Layer[@name='$LayerName']")
        if ($null -eq $sourceLayer) { return ,$result }

        foreach ($prop in @($sourceLayer.SelectNodes("./LayerProp"))) {
            $clone = $prop.CloneNode($true)

            foreach ($surface in @($clone.SelectNodes(".//Surface"))) {
                $animationName = ""
                if ($surface.HasAttribute("animsurface")) {
                    $animationName = $surface.GetAttribute("animsurface")
                }
                elseif ($surface.HasAttribute("animstate")) {
                    $animationName = $surface.GetAttribute("animstate")
                }

                if ($animationName -and ($UnsupportedAnimations -contains $animationName)) {
                    [void]$surface.ParentNode.RemoveChild($surface)
                }
            }

            if ($clone.SelectNodes(".//Surface").Count -gt 0) {
                [void]$result.Add($clone)
            }
        }

        return ,$result
    }

    function Append-SourceLayerProps {
        param(
            [Parameter(Mandatory=$true)][xml]$TargetDocument,
            [Parameter(Mandatory=$true)][xml]$SourceDocument,
            [Parameter(Mandatory=$true)][string]$LayerName
        )

        $props = @(Get-PrunedLayerProps -SourceDocument $SourceDocument -LayerName $LayerName)
        if ($props.Count -eq 0) { return 0 }

        $targetLayer = $TargetDocument.SelectSingleNode("/LogicalAnimationSurfaces/Layer[@name='$LayerName']")
        if ($null -eq $targetLayer) {
            $targetLayer = $TargetDocument.CreateElement("Layer")
            [void]$targetLayer.SetAttribute("name", $LayerName)
            [void]$TargetDocument.DocumentElement.AppendChild($targetLayer)
        }

        $added = 0
        foreach ($prop in $props) {
            [void]$targetLayer.AppendChild($TargetDocument.ImportNode($prop, $true))
            $added++
        }
        return $added
    }

    try {
        $sources = @{}
        foreach ($spec in @(
            @("base", $BaseRelative),
            @("gear", $GearRelative),
            @("guns", $GunsRelative),
            @("gunpal", $GunPaletteRelative)
        )) {
            $temp = Join-Path $env:TEMP ("vr_lobot_" + $Key + "_" + $spec[0] + "_" + $PID + ".xml")
            [void]$temps.Add($temp)
            Get-UrlFile -Url "$UpstreamRaw/TableData/LogicalBodyTypes/$($spec[1])" -Destination $temp
            [xml]$doc = [System.IO.File]::ReadAllText($temp)
            $sources[$spec[0]] = $doc
        }

        $targetBodyPath = Join-Path $TableRoot $TargetBodyRelative
        [xml]$targetDoc = [System.IO.File]::ReadAllText($targetBodyPath)

        $addedProps = 0

        # Preserve upstream precedence: specialised gear/gun filters before
        # the base body's empty/default filters.
        foreach ($layerName in @("legs","body","head","hands","arms")) {
            $addedProps += Append-SourceLayerProps -TargetDocument $targetDoc -SourceDocument $sources["gear"] -LayerName $layerName
            $addedProps += Append-SourceLayerProps -TargetDocument $targetDoc -SourceDocument $sources["base"] -LayerName $layerName
        }

        foreach ($layerName in @("blood","shadow")) {
            $addedProps += Append-SourceLayerProps -TargetDocument $targetDoc -SourceDocument $sources["base"] -LayerName $layerName
        }

        foreach ($layerName in @("gun","gunleft")) {
            $addedProps += Append-SourceLayerProps -TargetDocument $targetDoc -SourceDocument $sources["gunpal"] -LayerName $layerName
            $addedProps += Append-SourceLayerProps -TargetDocument $targetDoc -SourceDocument $sources["guns"] -LayerName $layerName
        }

        $neededSurfaceNames = New-Object "System.Collections.Generic.HashSet[string]" ([System.StringComparer]::Ordinal)
        foreach ($surface in @($targetDoc.SelectNodes("/LogicalAnimationSurfaces/Layer/LayerProp/Surface"))) {
            [void]$neededSurfaceNames.Add($surface.GetAttribute("name"))
        }

        $targetCatalogPath = Join-Path $TableRoot $TargetCatalogRelative
        $targetCatalogText = [System.IO.File]::ReadAllText($targetCatalogPath)

        $existingNames = New-Object "System.Collections.Generic.HashSet[string]" ([System.StringComparer]::Ordinal)
        foreach ($entry in [regex]::Matches($targetCatalogText, '(?is)<AnimSurface\b.*?/>')) {
            $nameMatch = [regex]::Match($entry.Value, '\bname="([^"]+)"')
            if ($nameMatch.Success) { [void]$existingNames.Add($nameMatch.Groups[1].Value) }
        }

        $upstreamDefinitions = @{}
        $catalogNumber = 0
        foreach ($catalogRelative in $CatalogRelatives) {
            $catalogNumber++
            $catalogTemp = Join-Path $env:TEMP ("vr_lobot_" + $Key + "_fullcat_" + $catalogNumber + "_" + $PID + ".xml")
            [void]$temps.Add($catalogTemp)
            Get-UrlFile -Url "$UpstreamRaw/TableData/LogicalBodyTypes/$catalogRelative" -Destination $catalogTemp
            $catalogText = [System.IO.File]::ReadAllText($catalogTemp)

            foreach ($entry in [regex]::Matches($catalogText, '(?is)<AnimSurface\b.*?/>')) {
                $nameMatch = [regex]::Match($entry.Value, '\bname="([^"]+)"')
                if ($nameMatch.Success) {
                    $upstreamDefinitions[$nameMatch.Groups[1].Value] = $entry.Value.Trim()
                }
            }
        }

        $append = New-Object System.Collections.ArrayList
        $missing = New-Object System.Collections.ArrayList
        foreach ($name in @($neededSurfaceNames | Sort-Object)) {
            if ($existingNames.Contains($name)) { continue }
            if (-not $upstreamDefinitions.ContainsKey($name)) {
                [void]$missing.Add($name)
                continue
            }

            [void]$append.Add($upstreamDefinitions[$name])
            [void]$existingNames.Add($name)
        }

        if ($missing.Count -gt 0) {
            Write-Host "Missing full-model animation definitions for ${Key}:" -ForegroundColor Red
            $missing | Select-Object -First 30 | ForEach-Object { Write-Host "  $_" }
            throw "$Key full logical model is incomplete."
        }

        $targetDoc.Save($targetBodyPath)
        if ($append.Count -gt 0) {
            $nl = [Environment]::NewLine
            $targetCatalogText = $targetCatalogText.TrimEnd() + $nl + ($append -join $nl) + $nl
            [System.IO.File]::WriteAllText($targetCatalogPath, $targetCatalogText, (New-Object System.Text.UTF8Encoding($false)))
        }

        Write-Host ("{0} full-model layer props     : +{1} (surface defs +{2})" -f $Key, $addedProps, $append.Count)
    }
    finally {
        foreach ($temp in $temps) {
            if (Test-Path $temp) { Remove-Item $temp -Force -ErrorAction SilentlyContinue }
        }
    }
}

Write-Host "Adding matched 1.13 body and weapon layers..."

$fullModelSpecs = @(
    [pscustomobject]@{
        Key = "RGM"
        Base = "LBT_RGM/LogicalBodyType_LBT_RGM.xml"
        Gear = "LBT_RGM/LogicalBodyType_RGM_gear.xml"
        Guns = "LBT_RGM/LogicalBodyType_RGM_guns.xml"
        GunPal = "LBT_RGM/LogicalBodyType_RGM_gun_paletteswaps.xml"
        Catalogs = @("LBT_RGM/AnimationSurfaces_LBT_RGM.xml","LBT_RGM/AnimationSurfaces_LBT_RGM_sawnoff.xml","LBT_RGM/AnimationSurfaces_LBT_RGM_moreguns.xml","LBT_RGM/AnimationSurfaces_LBT_RGM_moregear.xml","LBT_RGM/AnimationSurfaces_LBT_RGM_moremelee.xml")
        TargetBody = "LBT_RGM/LogicalBodyType_RGM_VR_equipment.xml"
        TargetCatalog = "LBT_RGM/AnimationSurfaces_RGM_VR_equipment.xml"
        Unsupported = @("RGMBAYONET_S_P","RGMBAYONET_S_S","RGMCROUCH_D_RDY","RGMCROUCH_P_RDY","RGMCROUCH_R_RDY")
    },
    [pscustomobject]@{
        Key = "BGM"
        Base = "LBT_BGM/LogicalBodyType_BGM.xml"
        Gear = "LBT_BGM/LogicalBodyType_BGM_gear.xml"
        Guns = "LBT_BGM/LogicalBodyType_BGM_guns.xml"
        GunPal = "LBT_BGM/LogicalBodyType_BGM_gun_paletteswaps.xml"
        Catalogs = @("LBT_BGM/AnimationSurfaces_BGM.xml","LBT_BGM/AnimationSurfaces_BGM_sawnoff.xml","LBT_BGM/AnimationSurfaces_BGM_moreguns.xml","LBT_BGM/AnimationSurfaces_BGM_moregear.xml","LBT_BGM/AnimationSurfaces_BGM_moremelee.xml")
        TargetBody = "LBT_BGM/LogicalBodyType_BGM_VR_equipment.xml"
        TargetCatalog = "LBT_BGM/AnimationSurfaces_BGM_VR_equipment.xml"
        Unsupported = @("BGMBAYONET_S_P","BGMBAYONET_S_S","BGMCROUCH_D_RDY","BGMCROUCH_P_RDY","BGMCROUCH_R_RDY")
    },
    [pscustomobject]@{
        Key = "RGF"
        Base = "LBT_RGF/LogicalBodyType_LBT_RGF.xml"
        Gear = "LBT_RGF/LogicalBodyType_RGF_gear.xml"
        Guns = "LBT_RGF/LogicalBodyType_RGF_guns.xml"
        GunPal = "LBT_RGF/LogicalBodyType_RGF_gun_paletteswaps.xml"
        Catalogs = @("LBT_RGF/AnimationSurfaces_LBT_RGF.xml","LBT_RGF/AnimationSurfaces_LBT_RGF_SAWNOFF.xml","LBT_RGF/AnimationSurfaces_LBT_RGF_moreguns.xml","LBT_RGF/AnimationSurfaces_LBT_RGF_moregear.xml","LBT_RGF/AnimationSurfaces_LBT_RGF_moremelee.xml")
        TargetBody = "LBT_RGF/LogicalBodyType_RGF_VR_equipment.xml"
        TargetCatalog = "LBT_RGF/AnimationSurfaces_RGF_VR_equipment.xml"
        Unsupported = @("RGFBAYONET_S_P","RGFBAYONET_S_S","RGFCROUCH_D_RDY","RGFCROUCH_P_RDY","RGFCROUCH_R_RDY","RGFLOWKICK","RGFSPINKICK","RGF_LOOK","RGF_PULL","RGF_SPIT","RGF_SQUISH")
    }
)

foreach ($spec in $fullModelSpecs) {
    Expand-VengeanceFullModelLayers -Key $spec.Key -BaseRelative $spec.Base -GearRelative $spec.Gear -GunsRelative $spec.Guns -GunPaletteRelative $spec.GunPal -CatalogRelatives $spec.Catalogs -TargetBodyRelative $spec.TargetBody -TargetCatalogRelative $spec.TargetCatalog -UnsupportedAnimations $spec.Unsupported
}

$logicalBodyTypesPath = Join-Path $TableRoot "LogicalBodyTypes.xml"
$logicalBodyTypesText = [System.IO.File]::ReadAllText($logicalBodyTypesPath)
$logicalBodyTypesText = $logicalBodyTypesText.Replace('cachesize="4096"', 'cachesize="8192"')
[System.IO.File]::WriteAllText($logicalBodyTypesPath, $logicalBodyTypesText, (New-Object System.Text.UTF8Encoding($false)))

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

    # AIMNAS-only equipment has no upstream 1.13 ID to translate. Resolve it
    # by item name from the *actual target table*, then map it to the nearest
    # stock 1.13 LOBOT silhouette. This avoids hard-coding AIMNAS uiIndex values.
    [xml]$filtersDoc = [System.IO.File]::ReadAllText($filtersPath)

    $zylonIds = Find-TargetEquipmentIdsByName $targetCatalog @(
        "Treated Zylon Combat Vest",
        "Coated Zylon Combat Vest"
    )
    $polyIds = Find-TargetEquipmentIdsByName $targetCatalog @(
        "Spec-18 MilEx Polyurethane Vest",
        "MilEx Polyurethane Vest",
        "Coated MilEx Polyurethane Vest"
    )
    $frackTacIds = Find-TargetEquipmentIdsByName $targetCatalog @("FrackTac Body Armor")
    $ballisticMaskIds = Find-TargetEquipmentIdsByName $targetCatalog @("Ballistic Face Mask Level IIIA")
    $kneePadIds = Find-TargetEquipmentIdsByName $targetCatalog @(
        "Knee Pads Urban Camo",
        "Knee Pads Desert Camo"
    )
    $gasMaskIds = Find-TargetEquipmentIdsByName $targetCatalog @(
        "Gas Mask Avon S10",
        "Gas Mask XM50"
    )
    $holsterIds = Find-TargetEquipmentIdsByName $targetCatalog @(
        "Revolver Holster",
        "Large Holster",
        "Shotgun holster",
        "SMG Leg Rig",
        "Throwing Knives Leg Rig",
        "12g Shotgun Rig",
        "3.11 Thigh Rig",
        "ETAC Large Holster",
        "ETAC Nylon Knife Sheath",
        "Drop Leg Holster",
        "Russian Pistol holster",
        "Large Modular Thigh Rig",
        "12g Shotgun Shells Leg Rig",
        "BP Black Kit Leg Rig",
        "SVD Leg Rig",
        "Sniper Magazines Leg Rig",
        "Arulcan Rocket Rifle Leg Rig",
        "60mm Shells Leg Rig",
        "Small BP Black Kit Leg Rig",
        "Grenade Launcher Holster",
        "Dual Holster Belt",
        "ETAC Revolver Holster",
        "ETAC Pistol Holster"
    )

    Add-IdsToNamedFilter $filtersDoc "ZylonVest" "VESTPOS" $zylonIds
    Add-IdsToNamedFilter $filtersDoc "KevlarVest" "VESTPOS" $polyIds
    Add-IdsToNamedFilter $filtersDoc "SpectraVest" "VESTPOS" $frackTacIds
    Add-IdsToNamedFilter $filtersDoc "SWATHelmet" "HELMETPOS" $ballisticMaskIds
    Add-IdsToNamedFilter $filtersDoc "Kneepads" "LEGPOS" $kneePadIds
    Add-IdsToNamedFilter $filtersDoc "Gasmask" "HEAD1POS" $gasMaskIds
    Add-IdsToNamedFilter $filtersDoc "Gasmask" "HEAD2POS" $gasMaskIds
    Add-IdsToNamedFilter $filtersDoc "Holster" "RTHIGHPOCKPOS" $holsterIds
    Add-IdsToNamedFilter $filtersDoc "LeftHolster" "LTHIGHPOCKPOS" $holsterIds
    $filtersDoc.Save($filtersPath)

    Write-Host ("Cross-reference ID rewrites   : {0}" -f $changedIds)
    Write-Host ("Unmatched upstream item IDs   : {0}" -f $unresolvedIds.Count)
    if ($unresolvedIds.Count -gt 0) {
        Write-Host ("  safely disabled (65535)     : {0}" -f ((@($unresolvedIds) | Sort-Object) -join ", "))
    }
    Write-Host "AIMNAS equipment mappings     : armour, mask, kneepads, gas masks, leg rigs/holsters"
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
            Write-Progress -Activity "Downloading visible equipment animation layers" -Status "$done / $($pending.Count)" -PercentComplete (($done * 100.0) / $pending.Count)
        }
    }
    finally {
        if ($client) { $client.Dispose() }
        if ($handler) { $handler.Dispose() }
        Write-Progress -Activity "Downloading visible equipment animation layers" -Completed
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


# Final structural gate: do not create the READY marker unless every intended
# full-model and equipment layer exists for every player body type. The runtime
# may still choose native Vengeance for a frame whose weapon/animation coverage
# is incomplete, but the deployed logical catalog itself must be complete.
$expectedEquipmentLayers = @(
    "blood","shadow","legs","legarmor","body","head","hands","arms",
    "vest","legrig","legrig_left","knees","backpack","gun","gunleft",
    "facegear","gasmask","ears","helmet"
)
$equipmentBodyFiles = @(
    "LBT_RGM/LogicalBodyType_RGM_VR_equipment.xml",
    "LBT_BGM/LogicalBodyType_BGM_VR_equipment.xml",
    "LBT_RGF/LogicalBodyType_RGF_VR_equipment.xml"
)
foreach ($relative in $equipmentBodyFiles) {
    $path = Join-Path $TableRoot $relative
    [xml]$doc = [System.IO.File]::ReadAllText($path)
    $present = New-Object "System.Collections.Generic.HashSet[string]" ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($layer in @($doc.SelectNodes("/LogicalAnimationSurfaces/Layer"))) {
        [void]$present.Add($layer.GetAttribute("name"))
    }

    $missingLayers = @($expectedEquipmentLayers | Where-Object { -not $present.Contains($_) })
    if ($missingLayers.Count -gt 0) {
        throw "Visible-equipment body catalog incomplete ($relative): missing $($missingLayers -join ', ')"
    }
}

$markerText = @"
Vengeance Reloaded visible tactical equipment
Catalog: mattedinburgh/vr_gamedir $VrRef
Source: 1dot13/gamedir $UpstreamRef Data/Anims/LOBOT art
Mode: hybrid (matched full 1.13 logical merc model; native Vengeance fallback when frame/weapon coverage is incomplete)
Layers: blood, shadow, legs, legarmor, body, head, hands, arms, vest, legrig, legrig_left, knees, backpack, gun, gunleft, facegear, gasmask, ears, helmet
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
Write-Host "Hybrid logical merc model enabled: matched 1.13 body/equipment/weapon layers with automatic native Vengeance fallback."
