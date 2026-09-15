param(
    [Parameter(Mandatory=$false)]
    [string]$GameRoot = "C:\\VENGENCE\\Jagged Alliance 2",

    [Parameter(Mandatory=$false)]
    [string]$TilesetRoot = "",

    [Parameter(Mandatory=$false)]
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($TilesetRoot)) {
    $TilesetRoot = Join-Path $GameRoot "Data-Maps-Tiles\Tilesets"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $env:TEMP "vr_combat_geometry_audit"
}

if (!(Test-Path -LiteralPath $TilesetRoot -PathType Container)) {
    throw "Tileset root not found: $TilesetRoot"
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

# Mirrors TileEngine/structure.h and TileEngine/structure.cpp.
# Resistance is NOT thickness: a projectile is charged this resistance again
# while its ray remains inside subsequent occupied 5x5 JSD profile cells.
$materials = @{
    0  = @("Nothing", 0)
    1  = @("Dry timber / wood wall", 25)
    2  = @("Furniture wood / plywood", 20)
    3  = @("Live wood", 30)
    4  = @("Light vegetation", 3)
    5  = @("Upholstered furniture", 10)
    6  = @("Porcelain", 47)
    7  = @("Cactus / hay / bamboo", 10)
    8  = @("Unused 1", 0)
    9  = @("Unused 2", 0)
    10 = @("Unused 3", 0)
    11 = @("Stone masonry", 55)
    12 = @("Concrete, non-reinforced", 63)
    13 = @("Concrete, reinforced", 70)
    14 = @("Rock", 85)
    15 = @("Rubber", 9)
    16 = @("Sand", 40)
    17 = @("Cloth", 1)
    18 = @("Sandbag", 40)
    19 = @("Unused 5", 0)
    20 = @("Unused 6", 0)
    21 = @("Light metal", 37)
    22 = @("Thicker metal", 57)
    23 = @("Heavy metal", 85)
    24 = @("Indestructible stone", 127)
    25 = @("Indestructible metal", 127)
    26 = @("Thicker metal with screen", 57)
}

# Known flags used only for classification in this audit.
$STRUCTURE_WALL = 0x00010000
$STRUCTURE_DOOR = 0x00080000

function Add-Count {
    param([hashtable]$Table, [int]$Key)
    if (!$Table.ContainsKey($Key)) { $Table[$Key] = 0 }
    $Table[$Key]++
}

function Get-ProfileThinDepth {
    param(
        [byte[]]$Bytes,
        [int]$TileOffset,
        [int]$Z
    )

    # DB_STRUCTURE_TILE::Shape is 5 x 5 bytes beginning at +4.
    # Each byte contains four vertical occupancy bits.
    $maxAlongX = 0
    $maxAlongY = 0
    $mask = 1 -shl $Z

    for ($y = 0; $y -lt 5; $y++) {
        $count = 0
        for ($x = 0; $x -lt 5; $x++) {
            $shape = $Bytes[$TileOffset + 4 + $x * 5 + $y]
            if (($shape -band $mask) -ne 0) { $count++ }
        }
        if ($count -gt $maxAlongX) { $maxAlongX = $count }
    }

    for ($x = 0; $x -lt 5; $x++) {
        $count = 0
        for ($y = 0; $y -lt 5; $y++) {
            $shape = $Bytes[$TileOffset + 4 + $x * 5 + $y]
            if (($shape -band $mask) -ne 0) { $count++ }
        }
        if ($count -gt $maxAlongY) { $maxAlongY = $count }
    }

    if ($maxAlongX -eq 0 -or $maxAlongY -eq 0) { return 0 }
    return [Math]::Min($maxAlongX, $maxAlongY)
}

$records = New-Object System.Collections.Generic.List[object]
$errors = New-Object System.Collections.Generic.List[object]
$jsdFiles = Get-ChildItem -LiteralPath $TilesetRoot -Recurse -File -Filter *.jsd

foreach ($file in $jsdFiles) {
    try {
        [byte[]]$bytes = [IO.File]::ReadAllBytes($file.FullName)
        if ($bytes.Length -lt 16) { throw "File shorter than JSD header" }

        $id = [Text.Encoding]::ASCII.GetString($bytes, 0, 4)
        if ($id -ne "J2SD") { throw "Unexpected JSD id '$id'" }

        $numberOfImages = [BitConverter]::ToUInt16($bytes, 4)
        $storedStructures = [BitConverter]::ToUInt16($bytes, 6)
        $declaredDataSize = [BitConverter]::ToUInt16($bytes, 8)
        $headerFlags = $bytes[10]
        $tileLocs = [BitConverter]::ToUInt16($bytes, 14)

        $offset = 16

        # STRUCTURE_FILE_CONTAINS_AUXIMAGEDATA = 0x01.
        # AuxObjectData is 16 bytes, RelTileLoc is 2 bytes.
        if (($headerFlags -band 0x01) -ne 0) {
            $offset += 16 * $numberOfImages
            $offset += 2 * $tileLocs
        }

        $structureDataStart = $offset

        for ($i = 0; $i -lt $storedStructures; $i++) {
            if ($offset + 16 -gt $bytes.Length) { throw "DB_STRUCTURE header overflow at structure $i" }

            $material = [int]$bytes[$offset]
            $density = [int]$bytes[$offset + 2]
            $tileCount = [int]$bytes[$offset + 3]
            $flags = [BitConverter]::ToUInt32($bytes, $offset + 4)
            $structureNumber = [BitConverter]::ToUInt16($bytes, $offset + 8)
            $wallOrientation = [int]$bytes[$offset + 10]
            $offset += 16

            $occupied = 0
            $sliceDepthCounts = @{ 1=0; 2=0; 3=0; 4=0; 5=0 }

            for ($tile = 0; $tile -lt $tileCount; $tile++) {
                if ($offset + 32 -gt $bytes.Length) { throw "DB_STRUCTURE_TILE overflow at structure $i tile $tile" }

                for ($x = 0; $x -lt 5; $x++) {
                    for ($y = 0; $y -lt 5; $y++) {
                        $shape = $bytes[$offset + 4 + $x * 5 + $y]
                        for ($z = 0; $z -lt 4; $z++) {
                            if (($shape -band (1 -shl $z)) -ne 0) { $occupied++ }
                        }
                    }
                }

                for ($z = 0; $z -lt 4; $z++) {
                    $thinDepth = Get-ProfileThinDepth -Bytes $bytes -TileOffset $offset -Z $z
                    if ($thinDepth -gt 0) { $sliceDepthCounts[$thinDepth]++ }
                }

                $offset += 32
            }

            $matName = if ($materials.ContainsKey($material)) { $materials[$material][0] } else { "Unknown material $material" }
            $matResistance = if ($materials.ContainsKey($material)) { [int]$materials[$material][1] } else { -1 }

            $records.Add([pscustomobject]@{
                RelativePath       = $file.FullName.Substring($TilesetRoot.Length).TrimStart('\')
                FileName           = $file.Name
                StructureNumber    = $structureNumber
                Material           = $material
                MaterialName       = $matName
                MaterialResistance = $matResistance
                Density            = $density
                TileCount          = $tileCount
                OccupiedCubes      = $occupied
                ThinSlices1        = $sliceDepthCounts[1]
                ThinSlices2        = $sliceDepthCounts[2]
                ThinSlices3        = $sliceDepthCounts[3]
                ThinSlices4        = $sliceDepthCounts[4]
                ThinSlices5        = $sliceDepthCounts[5]
                IsWall             = (($flags -band $STRUCTURE_WALL) -ne 0)
                IsDoor             = (($flags -band $STRUCTURE_DOOR) -ne 0)
                WallOrientation    = $wallOrientation
                FlagsHex           = ('0x{0:X8}' -f $flags)
            })
        }

        if (($offset - $structureDataStart) -ne $declaredDataSize) {
            # Keep this informational rather than fatal: historical files can contain
            # auxiliary/header differences while still being accepted by the engine.
            $errors.Add([pscustomobject]@{
                File = $file.FullName
                Error = "Parsed structure bytes $($offset - $structureDataStart) != declared $declaredDataSize"
            })
        }
    }
    catch {
        $errors.Add([pscustomobject]@{ File=$file.FullName; Error=$_.Exception.Message })
    }
}

$structureCsv = Join-Path $OutputDirectory "jsd_structure_geometry.csv"
$records | Sort-Object Material, RelativePath, StructureNumber |
    Export-Csv -LiteralPath $structureCsv -NoTypeInformation -Encoding UTF8

$summary = foreach ($group in ($records | Group-Object Material | Sort-Object { [int]$_.Name })) {
    $material = [int]$group.Name
    $rows = $group.Group
    $matName = if ($materials.ContainsKey($material)) { $materials[$material][0] } else { "Unknown material $material" }
    $matResistance = if ($materials.ContainsKey($material)) { [int]$materials[$material][1] } else { -1 }

    [pscustomobject]@{
        Material           = $material
        MaterialName       = $matName
        MaterialResistance = $matResistance
        Structures         = $rows.Count
        Tiles              = ($rows | Measure-Object TileCount -Sum).Sum
        Walls              = ($rows | Where-Object IsWall).Count
        Doors              = ($rows | Where-Object IsDoor).Count
        DensityMin         = ($rows | Measure-Object Density -Minimum).Minimum
        DensityMax         = ($rows | Measure-Object Density -Maximum).Maximum
        Density100         = ($rows | Where-Object { $_.Density -eq 100 }).Count
        ThinSlices1        = ($rows | Measure-Object ThinSlices1 -Sum).Sum
        ThinSlices2        = ($rows | Measure-Object ThinSlices2 -Sum).Sum
        ThinSlices3        = ($rows | Measure-Object ThinSlices3 -Sum).Sum
        ThinSlices4        = ($rows | Measure-Object ThinSlices4 -Sum).Sum
        ThinSlices5        = ($rows | Measure-Object ThinSlices5 -Sum).Sum
    }
}

$summaryCsv = Join-Path $OutputDirectory "jsd_material_summary.csv"
$summary | Export-Csv -LiteralPath $summaryCsv -NoTypeInformation -Encoding UTF8

$errorCsv = Join-Path $OutputDirectory "jsd_geometry_errors.csv"
$errors | Export-Csv -LiteralPath $errorCsv -NoTypeInformation -Encoding UTF8

Write-Host "JSD penetration geometry audit complete."
Write-Host "  Root:       $TilesetRoot"
Write-Host "  Files:      $($jsdFiles.Count)"
Write-Host "  Structures: $($records.Count)"
Write-Host "  Warnings:   $($errors.Count)"
Write-Host "  Structures: $structureCsv"
Write-Host "  Summary:    $summaryCsv"
Write-Host "  Warnings:   $errorCsv"
