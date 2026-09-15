param(
    [Parameter(Mandatory=$true)]
    [string]$PreviewDir,

    [string[]]$Maps = @('A3','A8','A12','b13','f15'),

    [int]$SampleStep = 8,

    [int]$PatchSize = 64,

    [double]$SevereDarkGrowth = 32.0,

    [double]$SevereEdgeGrowth = 18.0
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Find-PreviewFile {
    param(
        [string]$Dir,
        [string]$Map,
        [string]$Variant,
        [int]$Index
    )

    $stem = "{0}_{1}.dat_tactical_{2:D2}" -f $Map, $Variant, $Index
    $candidates = @(
        (Join-Path $Dir ($stem + '.png')),
        (Join-Path $Dir ($stem + '.bmp')),
        (Join-Path $Dir ($stem + '_overview.bmp')),
        (Join-Path $Dir ($stem + '_overview.png'))
    )

    foreach ($path in $candidates) {
        if (Test-Path -LiteralPath $path) { return $path }
    }

    # Windows paths are case-insensitive, but archived outputs may have inconsistent
    # casing. Fall back to a case-insensitive leaf-name scan.
    $wanted = [System.IO.Path]::GetFileNameWithoutExtension($stem)
    $files = Get-ChildItem -LiteralPath $Dir -File -ErrorAction SilentlyContinue |
        Where-Object {
            $_.BaseName -ieq $stem -or
            $_.BaseName -ieq ($stem + '_overview')
        }
    if ($files) { return $files[0].FullName }
    return $null
}

function Measure-MapImage {
    param([Parameter(Mandatory=$true)][string]$Path)

    $bmp = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        [double]$sumLuma = 0
        [double]$sumSat = 0
        [double]$sumGreen = 0
        [long]$dark = 0
        [long]$count = 0

        $step = [Math]::Max(1, $SampleStep)
        for ($y = 0; $y -lt $bmp.Height; $y += $step) {
            for ($x = 0; $x -lt $bmp.Width; $x += $step) {
                $c = $bmp.GetPixel($x, $y)
                $r = [int]$c.R
                $g = [int]$c.G
                $b = [int]$c.B

                $luma = ($r * 30.0 + $g * 59.0 + $b * 11.0) / 100.0
                $maxc = [Math]::Max($r, [Math]::Max($g, $b))
                $minc = [Math]::Min($r, [Math]::Min($g, $b))
                $sat = if ($maxc -gt 0) { (($maxc - $minc) * 100.0 / $maxc) } else { 0.0 }
                $green = $g - (($r + $b) / 2.0)

                $sumLuma += $luma
                $sumSat += $sat
                $sumGreen += $green
                if ($luma -lt 46.0) { $dark++ }
                $count++
            }
        }

        if ($count -eq 0) { throw "No pixels sampled from $Path" }

        [pscustomobject]@{
            Luma = $sumLuma / $count
            Saturation = $sumSat / $count
            GreenDominance = $sumGreen / $count
            DarkPct = 100.0 * $dark / $count
            Samples = $count
        }
    }
    finally {
        $bmp.Dispose()
    }
}


function Measure-Region {
    param(
        [Parameter(Mandatory=$true)][System.Drawing.Bitmap]$Bmp,
        [int]$X0, [int]$Y0, [int]$Width, [int]$Height
    )

    [double]$sumLuma = 0
    [double]$sumSat = 0
    [double]$sumEdge = 0
    [long]$dark = 0
    [long]$count = 0
    [long]$edgeCount = 0

    $step = [Math]::Max(1, [Math]::Min(4, $SampleStep))
    $x1 = [Math]::Min($Bmp.Width, $X0 + $Width)
    $y1 = [Math]::Min($Bmp.Height, $Y0 + $Height)

    for ($y = $Y0; $y -lt $y1; $y += $step) {
        for ($x = $X0; $x -lt $x1; $x += $step) {
            $c = $Bmp.GetPixel($x, $y)
            $r = [int]$c.R; $g = [int]$c.G; $b = [int]$c.B
            $l = ($r * 30.0 + $g * 59.0 + $b * 11.0) / 100.0
            $maxc = [Math]::Max($r, [Math]::Max($g, $b))
            $minc = [Math]::Min($r, [Math]::Min($g, $b))
            $sat = if ($maxc -gt 0) { (($maxc - $minc) * 100.0 / $maxc) } else { 0.0 }

            $sumLuma += $l
            $sumSat += $sat
            if ($l -lt 46.0) { $dark++ }
            $count++

            $nx = [Math]::Min($Bmp.Width - 1, $x + $step)
            $ny = [Math]::Min($Bmp.Height - 1, $y + $step)
            if ($nx -ne $x -or $ny -ne $y) {
                $cr = $Bmp.GetPixel($nx, $y)
                $cd = $Bmp.GetPixel($x, $ny)
                $lr = ($cr.R * 30.0 + $cr.G * 59.0 + $cr.B * 11.0) / 100.0
                $ld = ($cd.R * 30.0 + $cd.G * 59.0 + $cd.B * 11.0) / 100.0
                $sumEdge += ([Math]::Abs($l - $lr) + [Math]::Abs($l - $ld)) / 2.0
                $edgeCount++
            }
        }
    }

    [pscustomobject]@{
        Luma = if ($count) { $sumLuma / $count } else { 0.0 }
        Saturation = if ($count) { $sumSat / $count } else { 0.0 }
        DarkPct = if ($count) { 100.0 * $dark / $count } else { 0.0 }
        Edge = if ($edgeCount) { $sumEdge / $edgeCount } else { 0.0 }
    }
}

function Find-LocalHotspots {
    param(
        [Parameter(Mandatory=$true)][string]$PristinePath,
        [Parameter(Mandatory=$true)][string]$RemasterPath
    )

    $p = [System.Drawing.Bitmap]::FromFile($PristinePath)
    $r = [System.Drawing.Bitmap]::FromFile($RemasterPath)
    try {
        if ($p.Width -ne $r.Width -or $p.Height -ne $r.Height) {
            throw "Visual-QA pair dimensions differ: $PristinePath vs $RemasterPath"
        }

        $hits = @()
        $patch = [Math]::Max(24, $PatchSize)
        for ($y = 0; $y -lt $p.Height; $y += $patch) {
            for ($x = 0; $x -lt $p.Width; $x += $patch) {
                $ps = Measure-Region -Bmp $p -X0 $x -Y0 $y -Width $patch -Height $patch
                $rs = Measure-Region -Bmp $r -X0 $x -Y0 $y -Width $patch -Height $patch
                $dDark = $rs.DarkPct - $ps.DarkPct
                $dEdge = $rs.Edge - $ps.Edge

                # Local artifact oracle: a previously quiet patch acquiring both a
                # large dark mass and much stronger edges is not a harmless palette
                # change. It requires review even when global averages look healthy.
                if ($dDark -ge $SevereDarkGrowth -and $dEdge -ge $SevereEdgeGrowth) {
                    $hits += [pscustomobject]@{
                        X = $x; Y = $y
                        DeltaDark = [Math]::Round($dDark, 2)
                        DeltaEdge = [Math]::Round($dEdge, 2)
                        PristineDark = [Math]::Round($ps.DarkPct, 2)
                        RemasterDark = [Math]::Round($rs.DarkPct, 2)
                    }
                }
            }
        }
        return $hits
    }
    finally {
        $p.Dispose()
        $r.Dispose()
    }
}

$results = @()
$failed = $false

foreach ($map in $Maps) {
    $pairs = @()

    for ($i = 1; $i -le 6; $i++) {
        $pristine = Find-PreviewFile -Dir $PreviewDir -Map $map -Variant 'PRISTINE' -Index $i
        $remaster = Find-PreviewFile -Dir $PreviewDir -Map $map -Variant 'REMASTERED' -Index $i

        if (-not $pristine -or -not $remaster) {
            Write-Warning "Missing visual QA pair: map=$map view=$i pristine=$pristine remaster=$remaster"
            continue
        }

        $p = Measure-MapImage -Path $pristine
        $r = Measure-MapImage -Path $remaster
        $hotspots = @(Find-LocalHotspots -PristinePath $pristine -RemasterPath $remaster)
        $pairs += [pscustomobject]@{
            Pristine = $p
            Remaster = $r
            Hotspots = $hotspots
            HotspotCount = $hotspots.Count
        }
    }

    if ($pairs.Count -ne 6) {
        $failed = $true
        $results += [pscustomobject]@{
            Map = $map
            Views = $pairs.Count
            Verdict = 'FAIL'
            Reason = 'missing tactical views'
        }
        continue
    }

    $pLuma = ($pairs | ForEach-Object { $_.Pristine.Luma } | Measure-Object -Average).Average
    $rLuma = ($pairs | ForEach-Object { $_.Remaster.Luma } | Measure-Object -Average).Average
    $pSat = ($pairs | ForEach-Object { $_.Pristine.Saturation } | Measure-Object -Average).Average
    $rSat = ($pairs | ForEach-Object { $_.Remaster.Saturation } | Measure-Object -Average).Average
    $pGreen = ($pairs | ForEach-Object { $_.Pristine.GreenDominance } | Measure-Object -Average).Average
    $rGreen = ($pairs | ForEach-Object { $_.Remaster.GreenDominance } | Measure-Object -Average).Average
    $pDark = ($pairs | ForEach-Object { $_.Pristine.DarkPct } | Measure-Object -Average).Average
    $rDark = ($pairs | ForEach-Object { $_.Remaster.DarkPct } | Measure-Object -Average).Average

    $dLuma = $rLuma - $pLuma
    $dSat = $rSat - $pSat
    $dGreen = $rGreen - $pGreen
    $dDark = $rDark - $pDark
    $localHotspots = @($pairs | ForEach-Object { $_.Hotspots })
    $localHotspotCount = ($pairs | Measure-Object -Property HotspotCount -Sum).Sum

    # Global gates catch palette drift; local hotspots catch concentrated artifacts
    # that global averages can completely hide (e.g. an invented freestanding
    # structural sprite in otherwise unchanged open ground).
    # Dark sectors may not get darker, global saturation must stay controlled,
    # and no sector may acquire a blanket emerald cast.
    $darkMapReadable = ($pLuma -ge 55.0) -or ($dLuma -ge -0.5)
    $saturationControlled = $dSat -le 17.0
    $greenControlled = $dGreen -le 8.0
    $darkAreaControlled = $dDark -le 4.0

    $localStructureReadable = ($localHotspotCount -eq 0)
    $pass = $darkMapReadable -and $saturationControlled -and $greenControlled -and $darkAreaControlled -and $localStructureReadable
    if (-not $pass) { $failed = $true }

    $reason = @()
    if (-not $darkMapReadable) { $reason += 'dark map became darker' }
    if (-not $saturationControlled) { $reason += 'excess saturation increase' }
    if (-not $greenControlled) { $reason += 'excess green cast' }
    if (-not $darkAreaControlled) { $reason += 'too much dark-area growth' }
    if (-not $localStructureReadable) { $reason += "local contrast/structure hotspots=$localHotspotCount" }

    $results += [pscustomobject]@{
        Map = $map
        Views = 6
        PristineLuma = [Math]::Round($pLuma, 2)
        RemasterLuma = [Math]::Round($rLuma, 2)
        DeltaLuma = [Math]::Round($dLuma, 2)
        PristineSaturation = [Math]::Round($pSat, 2)
        RemasterSaturation = [Math]::Round($rSat, 2)
        DeltaSaturation = [Math]::Round($dSat, 2)
        PristineGreen = [Math]::Round($pGreen, 2)
        RemasterGreen = [Math]::Round($rGreen, 2)
        DeltaGreen = [Math]::Round($dGreen, 2)
        PristineDarkPct = [Math]::Round($pDark, 2)
        RemasterDarkPct = [Math]::Round($rDark, 2)
        DeltaDarkPct = [Math]::Round($dDark, 2)
        LocalHotspots = $localHotspotCount
        HotspotPreview = (($localHotspots | Select-Object -First 8 | ForEach-Object {
            "($($_.X),$($_.Y)) dark+$($_.DeltaDark) edge+$($_.DeltaEdge)"
        }) -join ' ')
        Verdict = if ($pass) { 'PASS' } else { 'FAIL' }
        Reason = if ($pass) { 'balanced visual change' } else { ($reason -join '; ') }
    }
}

$csv = Join-Path $PreviewDir 'map_factory_visual_qa.csv'
$json = Join-Path $PreviewDir 'map_factory_visual_qa.json'
$results | Export-Csv -NoTypeInformation -Encoding UTF8 $csv
$results | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 $json
$results | Format-Table -AutoSize | Out-String | Write-Host

if ($failed) {
    Write-Error "Map Factory visual QA failed. See $csv"
    exit 2
}

Write-Host "Map Factory visual QA passed for all requested maps."
