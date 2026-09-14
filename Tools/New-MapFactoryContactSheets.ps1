param(
    [Parameter(Mandatory=$true)]
    [string]$PreviewDir,

    [string[]]$Maps = @('A3','A8','A12','b13','f15'),

    [int]$ColumnWidth = 480
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Find-PreviewFile {
    param([string]$Dir,[string]$Map,[string]$Variant,[int]$Index)

    $stem = "{0}_{1}.dat_tactical_{2:D2}" -f $Map, $Variant, $Index
    $patterns = @(
        $stem + '.png',
        $stem + '.bmp',
        $stem + '_overview.png',
        $stem + '_overview.bmp'
    )

    foreach ($name in $patterns) {
        $path = Join-Path $Dir $name
        if (Test-Path -LiteralPath $path) { return $path }
    }

    $hit = Get-ChildItem -LiteralPath $Dir -File -ErrorAction SilentlyContinue |
        Where-Object {
            $_.BaseName -ieq $stem -or $_.BaseName -ieq ($stem + '_overview')
        } | Select-Object -First 1
    if ($hit) { return $hit.FullName }
    return $null
}

foreach ($map in $Maps) {
    $rows = @()

    for ($i = 1; $i -le 6; $i++) {
        $pPath = Find-PreviewFile -Dir $PreviewDir -Map $map -Variant 'PRISTINE' -Index $i
        $rPath = Find-PreviewFile -Dir $PreviewDir -Map $map -Variant 'REMASTERED' -Index $i
        if (-not $pPath -or -not $rPath) {
            Write-Warning "Skipping contact sheet for $map: missing view $i"
            $rows = @()
            break
        }

        $p = [System.Drawing.Image]::FromFile($pPath)
        $r = [System.Drawing.Image]::FromFile($rPath)
        $height = [Math]::Max(
            [int][Math]::Round($p.Height * $ColumnWidth / [double]$p.Width),
            [int][Math]::Round($r.Height * $ColumnWidth / [double]$r.Width)
        )

        $rows += [pscustomobject]@{
            Index = $i
            PPath = $pPath
            RPath = $rPath
            Height = $height
        }
        $p.Dispose()
        $r.Dispose()
    }

    if ($rows.Count -ne 6) { continue }

    $headerHeight = 32
    $totalHeight = $headerHeight + (($rows | Measure-Object -Property Height -Sum).Sum)
    $sheet = New-Object -TypeName System.Drawing.Bitmap -ArgumentList @(
        ($ColumnWidth * 2),
        [int]$totalHeight,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
    )
    $g = [System.Drawing.Graphics]::FromImage($sheet)
    $font = New-Object -TypeName System.Drawing.Font -ArgumentList @(
        'Arial', 12, [System.Drawing.FontStyle]::Bold
    )
    $small = New-Object -TypeName System.Drawing.Font -ArgumentList @('Arial', 9)
    $white = [System.Drawing.Brushes]::White
    $black = [System.Drawing.Brushes]::Black

    try {
        $g.Clear([System.Drawing.Color]::Black)
        $g.DrawString("$map - PRISTINE", $font, $white, 8, 6)
        $g.DrawString("$map - REMASTER", $font, $white, $ColumnWidth + 8, 6)

        $y = $headerHeight
        foreach ($row in $rows) {
            $p = [System.Drawing.Image]::FromFile($row.PPath)
            $r = [System.Drawing.Image]::FromFile($row.RPath)
            try {
                $pH = [int][Math]::Round($p.Height * $ColumnWidth / [double]$p.Width)
                $rH = [int][Math]::Round($r.Height * $ColumnWidth / [double]$r.Width)
                $g.DrawImage($p, 0, $y, $ColumnWidth, $pH)
                $g.DrawImage($r, $ColumnWidth, $y, $ColumnWidth, $rH)

                $label = "view {0}" -f $row.Index
                $g.FillRectangle($black, 4, $y + 4, 52, 18)
                $g.DrawString($label, $small, $white, 7, $y + 5)
                $g.FillRectangle($black, $ColumnWidth + 4, $y + 4, 52, 18)
                $g.DrawString($label, $small, $white, $ColumnWidth + 7, $y + 5)
            }
            finally {
                $p.Dispose()
                $r.Dispose()
            }
            $y += $row.Height
        }

        $out = Join-Path $PreviewDir ($map + '_contact.png')
        $sheet.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
        Write-Host "Wrote $out"
    }
    finally {
        $g.Dispose()
        $sheet.Dispose()
        $font.Dispose()
        $small.Dispose()
    }
}
