param(
    [string]$GameRoot = "",
    [switch]$Strict,
    [switch]$SkipAssetCheck
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2

$SourceRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

function Resolve-GameRoot {
    param([string]$ExplicitRoot, [string]$StartRoot)

    if ($ExplicitRoot) {
        return (Resolve-Path $ExplicitRoot).Path
    }

    $candidate = $StartRoot
    for ($i = 0; $i -lt 5 -and $candidate; $i++) {
        if ((Test-Path (Join-Path $candidate "Data-AIMv53")) -and
            (Test-Path (Join-Path $candidate "Data-Vengeance"))) {
            return (Resolve-Path $candidate).Path
        }
        $candidate = Split-Path -Parent $candidate
    }

    throw "Could not locate game root. Pass -GameRoot explicitly."
}
$GameRoot = Resolve-GameRoot $GameRoot $SourceRoot
$ItemsPath = Join-Path $GameRoot "Data-AIMv53\TableData\Items\Items.xml"
$ArmourPath = Join-Path $GameRoot "Data-AIMv53\TableData\Items\Armours.xml"
$LobotRoot = Join-Path $GameRoot "Data-Vengeance\TableData\LogicalBodyTypes"
$FiltersPath = Join-Path $LobotRoot "Filters.xml"

foreach ($required in @($ItemsPath, $ArmourPath, $FiltersPath)) {
    if (-not (Test-Path $required -PathType Leaf)) {
        throw "Required visible-equipment input is missing: $required"
    }
}

[xml]$itemsDoc = [IO.File]::ReadAllText($ItemsPath)
[xml]$armourDoc = [IO.File]::ReadAllText($ArmourPath)
[xml]$filtersDoc = [IO.File]::ReadAllText($FiltersPath)

$armourClassByIndex = @{}
foreach ($entry in $armourDoc.ARMOURLIST.ARMOUR) {
    $armourClassByIndex[[int]$entry.uiIndex] = [int]$entry.ubArmourClass
}

function Get-XmlInt {
    param($Node, [string]$Name, [int]$Default = 0)
    $child = $Node.SelectSingleNode("./$Name")
    if ($null -eq $child) { return $Default }
    $value = 0
    if ([int]::TryParse([string]$child.InnerText, [ref]$value)) { return $value }
    return $Default
}

function Get-XmlText {
    param($Node, [string]$Name)
    $child = $Node.SelectSingleNode("./$Name")
    if ($null -eq $child) { return "" }
    return [string]$child.InnerText
}

$slotSpecs = @(
    [pscustomobject]@{ Class = 0; Layer = "helmet";   SlotTag = "HELMETPOS"; Label = "helmet" },
    [pscustomobject]@{ Class = 1; Layer = "vest";     SlotTag = "VESTPOS";   Label = "vest" },
    [pscustomobject]@{ Class = 2; Layer = "legarmor"; SlotTag = "LEGPOS";    Label = "leggings" }
)
$armourItems = New-Object System.Collections.ArrayList
foreach ($item in $itemsDoc.ITEMLIST.ITEM) {
    $itemClass = Get-XmlInt $item "usItemClass"
    if (($itemClass -band 0x00000800) -eq 0) { continue }

    $classIndexNode = $item.SelectSingleNode("./ubClassIndex")
    if ($null -eq $classIndexNode) { continue }
    $classIndex = Get-XmlInt $item "ubClassIndex"
    if (-not $armourClassByIndex.ContainsKey($classIndex)) { continue }

    $slotClass = $armourClassByIndex[$classIndex]
    if ($slotClass -notin 0,1,2) { continue }
    if ((Get-XmlInt $item "NotInEditor") -ne 0) { continue }

    [void]$armourItems.Add([pscustomobject]@{
        Id = Get-XmlInt $item "uiIndex"
        Name = Get-XmlText $item "szLongItemName"
        ShortName = Get-XmlText $item "szItemName"
        SlotClass = $slotClass
        ClassIndex = $classIndex
    })
}

$filterNodes = @{}
foreach ($filter in $filtersDoc.Filters.Filter) {
    $filterNodes[[string]$filter.name] = $filter
}
function Get-FilterPositiveIds {
    param(
        [string]$FilterName,
        [string]$SlotTag,
        [System.Collections.Generic.HashSet[string]]$Visited
    )

    $result = New-Object "System.Collections.Generic.HashSet[int]"
    if (-not $filterNodes.ContainsKey($FilterName)) { return $result }
    if (-not $Visited.Add($FilterName)) { return $result }

    $filter = $filterNodes[$FilterName]
    foreach ($node in @($filter.SelectNodes(".//$SlotTag"))) {
        if ($node.HasAttribute("not")) { continue }
        foreach ($token in ([string]$node.InnerText -split '[,\s]+')) {
            $id = 0
            if ([int]::TryParse($token, [ref]$id) -and $id -ne 65535) {
                [void]$result.Add($id)
            }
        }
    }

    foreach ($ref in @($filter.SelectNodes(".//FILTER"))) {
        $nested = Get-FilterPositiveIds ([string]$ref.InnerText).Trim() $SlotTag $Visited
        foreach ($id in $nested) { [void]$result.Add($id) }
    }

    return $result
}
$bodySpecs = @(
    [pscustomobject]@{ Key = "RGM"; Body = "LBT_RGM\LogicalBodyType_RGM_VR_equipment.xml"; Catalog = "LBT_RGM\AnimationSurfaces_RGM_VR_equipment.xml" },
    [pscustomobject]@{ Key = "BGM"; Body = "LBT_BGM\LogicalBodyType_BGM_VR_equipment.xml"; Catalog = "LBT_BGM\AnimationSurfaces_BGM_VR_equipment.xml" },
    [pscustomobject]@{ Key = "RGF"; Body = "LBT_RGF\LogicalBodyType_RGF_VR_equipment.xml"; Catalog = "LBT_RGF\AnimationSurfaces_RGF_VR_equipment.xml" }
)

$failures = New-Object System.Collections.ArrayList
$rows = New-Object System.Collections.ArrayList

foreach ($bodySpec in $bodySpecs) {
    $bodyPath = Join-Path $LobotRoot $bodySpec.Body
    $catalogPath = Join-Path $LobotRoot $bodySpec.Catalog
    [xml]$bodyDoc = [IO.File]::ReadAllText($bodyPath)
    $catalogText = [IO.File]::ReadAllText($catalogPath)

    # Vengeance animation catalogs are XML fragments rather than single-root
    # documents, so parse the self-contained AnimSurface records directly.
    $catalog = @{}
    foreach ($entry in [regex]::Matches($catalogText, '(?is)<AnimSurface\b.*?/>')) {
        $nameMatch = [regex]::Match($entry.Value, '\bname="([^"]+)"')
        $fileMatch = [regex]::Match($entry.Value, '\bfile="([^"]+)"')
        if ($nameMatch.Success -and $fileMatch.Success) {
            $catalog[$nameMatch.Groups[1].Value] = [pscustomobject]@{
                name = $nameMatch.Groups[1].Value
                file = $fileMatch.Groups[1].Value
            }
        }
    }

    foreach ($slot in $slotSpecs) {
        $layer = $bodyDoc.SelectSingleNode("/LogicalAnimationSurfaces/Layer[@name='$($slot.Layer)']")
        $mappedIds = New-Object "System.Collections.Generic.HashSet[int]"
        $propsByItem = @{}
        if ($null -ne $layer) {
            foreach ($prop in @($layer.LayerProp)) {
                $filterName = [string]$prop.filter
                $visited = New-Object "System.Collections.Generic.HashSet[string]"
                $ids = Get-FilterPositiveIds $filterName $slot.SlotTag $visited
                foreach ($id in $ids) {
                    [void]$mappedIds.Add($id)
                    if (-not $propsByItem.ContainsKey($id)) {
                        $propsByItem[$id] = New-Object System.Collections.ArrayList
                    }
                    [void]$propsByItem[$id].Add($prop)
                }
            }
        }

        $slotItems = @($armourItems | Where-Object { $_.SlotClass -eq $slot.Class })
        foreach ($item in $slotItems) {
            $mapped = $mappedIds.Contains($item.Id)
            $stand = $false
            $crouch = $false
            $prone = $false
            $missingSurface = $false
            $missingAsset = $false

            if ($propsByItem.ContainsKey($item.Id)) {
                foreach ($prop in $propsByItem[$item.Id]) {
                    foreach ($surface in @($prop.SelectNodes("./Surface"))) {
                        $anim = [string]$surface.animsurface
                        if ($anim -in @("$($bodySpec.Key)STANDING", "$($bodySpec.Key)NOTHING_STD")) { $stand = $true }
                        if ($anim -in @("$($bodySpec.Key)CROUCHING", "$($bodySpec.Key)NOTHING_CROUCH")) { $crouch = $true }
                        if ($anim -in @("$($bodySpec.Key)PRONE", "$($bodySpec.Key)HANDGUN_PRONE")) { $prone = $true }

                        $surfaceName = [string]$surface.name
                        if (-not $catalog.ContainsKey($surfaceName)) {
                            $missingSurface = $true
                            continue
                        }
                        if (-not $SkipAssetCheck) {
                            $assetRelative = ([string]$catalog[$surfaceName].file).Replace("\", [IO.Path]::DirectorySeparatorChar)
                            $assetPath = Join-Path (Join-Path $GameRoot "Data-Vengeance") $assetRelative
                            if (-not (Test-Path $assetPath -PathType Leaf)) {
                                $missingAsset = $true
                            }
                        }
                    }
                }
            }

            $ok = (-not $mapped) -or ($stand -and $crouch -and $prone -and (-not $missingSurface) -and (-not $missingAsset))
            if ($mapped -and (-not $ok)) {
                [void]$failures.Add("$($bodySpec.Key)/$($slot.Label) #$($item.Id) $($item.Name)")
            }

            [void]$rows.Add([pscustomobject]@{
                Body = $bodySpec.Key
                Slot = $slot.Label
                Id = $item.Id
                Item = $item.Name
                Mapped = $mapped
                Stand = $stand
                Crouch = $crouch
                Prone = $prone
                SurfaceDefs = -not $missingSurface
                Assets = -not $missingAsset
                OK = $ok
            })
        }
    }
}
Write-Host ""
Write-Host "Visible armour coverage audit"
Write-Host "Game root: $GameRoot"
Write-Host ("Armour items audited: {0}" -f $armourItems.Count)
Write-Host ""

$summary = $rows | Group-Object Body,Slot | ForEach-Object {
    $group = @($_.Group)
    [pscustomobject]@{
        BodySlot = $_.Name
        Items = $group.Count
        Mapped = @($group | Where-Object Mapped).Count
        Stand = @($group | Where-Object Stand).Count
        Crouch = @($group | Where-Object Crouch).Count
        Prone = @($group | Where-Object Prone).Count
        MappedOK = @($group | Where-Object { $_.Mapped -and $_.OK }).Count
        Gaps = @($group | Where-Object { $_.Mapped -and (-not $_.OK) }).Count
    }
}
$summary | Format-Table -AutoSize

if ($failures.Count -gt 0) {
    Write-Host ""
    Write-Host "Mapped-item coverage gaps:" -ForegroundColor Yellow
    $rows | Where-Object { $_.Mapped -and (-not $_.OK) } | Format-Table Body,Slot,Id,Item,Stand,Crouch,Prone,SurfaceDefs,Assets -AutoSize
}

Write-Host ""
Write-Host ("Result: {0} mapped-item coverage gap(s)" -f $failures.Count)
if ($Strict -and $failures.Count -gt 0) { exit 1 }
exit 0
