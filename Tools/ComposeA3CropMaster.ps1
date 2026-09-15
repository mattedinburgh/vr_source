param(
    [Parameter(Mandatory=$true)]
    [string]$DataMapsTilesRoot
)

$ErrorActionPreference = 'Stop'

function Read-U16([byte[]]$Bytes, [int]$Offset) {
    return [int][System.BitConverter]::ToUInt16($Bytes, $Offset)
}

function Read-I16([byte[]]$Bytes, [int]$Offset) {
    return [int][System.BitConverter]::ToInt16($Bytes, $Offset)
}

function Read-U32([byte[]]$Bytes, [int]$Offset) {
    return [uint32][System.BitConverter]::ToUInt32($Bytes, $Offset)
}

function Read-B1TCFrame {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][int]$FrameIndex
    )

    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 8 -or [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'B1TC') {
        throw "Not a B1TC file: $Path"
    }

    $version = Read-U16 $bytes 4
    $count = Read-U16 $bytes 6
    if ($version -ne 1) {
        throw "Unsupported B1TC version $version in $Path"
    }
    if ($FrameIndex -lt 0 -or $FrameIndex -ge $count) {
        throw "Frame $FrameIndex is outside 0..$($count-1) in $Path"
    }

    $entry = 8 + ($FrameIndex * 16)
    $ox = Read-I16 $bytes ($entry + 0)
    $oy = Read-I16 $bytes ($entry + 2)
    $w = Read-U16 $bytes ($entry + 4)
    $h = Read-U16 $bytes ($entry + 6)
    $src = Read-U32 $bytes ($entry + 8)
    $len = Read-U32 $bytes ($entry + 12)
    $expected = [uint32]$w * [uint32]$h * 4

    if ($len -ne $expected) {
        throw "Frame payload mismatch in $Path frame ${FrameIndex}: $len vs $expected"
    }
    if ([uint64]$src + [uint64]$len -gt [uint64]$bytes.Length) {
        throw "Frame payload exceeds file length in $Path frame $FrameIndex"
    }

    [byte[]]$rgba = New-Object byte[] $len
    [System.Array]::Copy($bytes, [int]$src, $rgba, 0, [int]$len)

    return [pscustomobject]@{
        Source = [System.IO.Path]::GetFileName($Path)
        SourceFrame = $FrameIndex + 1
        OffsetX = [int16]$ox
        OffsetY = [int16]$oy
        Width = [uint16]$w
        Height = [uint16]$h
        Data = $rgba
    }
}

function Write-B1TC {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][object[]]$Frames
    )

    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $parent | Out-Null

    $stream = New-Object System.IO.FileStream(
        $Path,
        [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None
    )
    $writer = New-Object System.IO.BinaryWriter($stream)

    try {
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes('B1TC'))
        $writer.Write([uint16]1)
        $writer.Write([uint16]$Frames.Count)

        [uint32]$payloadOffset = 8 + ($Frames.Count * 16)
        foreach ($frame in $Frames) {
            $writer.Write([int16]$frame.OffsetX)
            $writer.Write([int16]$frame.OffsetY)
            $writer.Write([uint16]$frame.Width)
            $writer.Write([uint16]$frame.Height)
            $writer.Write([uint32]$payloadOffset)
            $writer.Write([uint32]$frame.Data.Length)
            $payloadOffset += [uint32]$frame.Data.Length
        }

        foreach ($frame in $Frames) {
            $writer.Write([byte[]]$frame.Data)
        }
    }
    finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

$farmKit = Join-Path $DataMapsTilesRoot 'FarmKit'
$tileset38 = Join-Path $DataMapsTilesRoot 'Tilesets\38'

$selection = @(
    # Frames 1-4: mature/tall field.  gA3CropStripA and dense beds use these.
    @{ File = 'CROPS_THICK_TALL.b1tc'; Frame = 0  },
    @{ File = 'CROPS_THICK_TALL.b1tc'; Frame = 5  },
    @{ File = 'CROPS_THICK_TALL.b1tc'; Frame = 10 },
    @{ File = 'CROPS_THICK_TALL.b1tc'; Frame = 15 },

    # Frames 5-6: medium-height crop rows.
    @{ File = 'CROPS_MEDIUM_ROWS.b1tc'; Frame = 1  },
    @{ File = 'CROPS_MEDIUM_ROWS.b1tc'; Frame = 9  },

    # Frames 7-8: young/low crop, giving the second field a different age.
    @{ File = 'CROPS_LOW_ROWS.b1tc'; Frame = 3  },
    @{ File = 'CROPS_LOW_ROWS.b1tc'; Frame = 14 },

    # Frame 9: dry/harvested crop used for deliberate harvest breaks.
    @{ File = 'CROPS_DRY_DEAD.b1tc'; Frame = 5 },

    # Frame 10: tropical broadleaf accent for mixed plots and field edges.
    @{ File = 'CROPS_TROPICAL_BROADLEAF.b1tc'; Frame = 7 }
)

$frames = @()
foreach ($pick in $selection) {
    $source = Join-Path $farmKit $pick.File
    if (-not (Test-Path $source)) {
        throw "Missing FarmKit source: $source"
    }
    $frames += Read-B1TCFrame -Path $source -FrameIndex ([int]$pick.Frame)
}

$target = Join-Path $tileset38 'VR_CROP_MASTER.b1tc'
Write-B1TC -Path $target -Frames $frames

Write-Host "A3 composite crop master written: $target"
for ($i = 0; $i -lt $frames.Count; $i++) {
    $f = $frames[$i]
    Write-Host ("  {0,2}: {1} frame {2}  {3}x{4} off {5},{6}" -f
        ($i + 1), $f.Source, $f.SourceFrame, $f.Width, $f.Height, $f.OffsetX, $f.OffsetY)
}
