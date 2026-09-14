param(
    [Parameter(Mandatory=$true)]
    [string]$PreviewDir,

    [string[]]$Maps = @('A3','A8','A12','b13','f15'),

    [int]$SampleStep = 8
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
        $pairs += [pscustomobject]@{
            Pristine = $p
            Remaster = $r
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

    $pLuma = ($pairs | Measure-Object -Property { $_.Pristine.Luma } -Average).Average
    $rLuma = ($pairs | Measure-Object -Property { $_.Remaster.Luma } -Average).Average
    $pSat = ($pairs | Measure-Object -Property { $_.Pristine.Saturation } -Average).Average
    $rSat = ($pairs | Measure-Object -Property { $_.Remaster.Saturation } -Average).Average
    $pGreen = ($pairs | Measure-Object -Property { $_.Pristine.GreenDominance } -Average).Average
    $rGreen = ($pairs | Measure-Object -Property { $_.Remaster.GreenDominance } -Average).Average
    $pDark = ($pairs | Measure-Object -Property { $_.Pristine.DarkPct } -Average).Average
    $rDark = ($pairs | Measure-Object -Property { $_.Remaster.DarkPct } -Average).Average

    $dLuma = $rLuma - $pLuma
    $dSat = $rSat - $pSat
    $dGreen = $rGreen - $pGreen
    $dDark = $rDark - $pDark

    # Acceptance gates learned from the successful-but-visually-poor #99 pilot.
    # Dark sectors may not get darker, global saturation must stay controlled,
    # and no sector may acquire a blanket emerald cast.
    $darkMapReadable = ($pLuma -ge 55.0) -or ($dLuma -ge -0.5)
    $saturationControlled = $dSat -le 17.0
    $greenControlled = $dGreen -le 8.0
    $darkAreaControlled = $dDark -le 4.0

    $pass = $darkMapReadable -and $saturationControlled -and $greenControlled -and $darkAreaControlled
    if (-not $pass) { $failed = $true }

    $reason = @()
    if (-not $darkMapReadable) { $reason += 'dark map became darker' }
    if (-not $saturationControlled) { $reason += 'excess saturation increase' }
    if (-not $greenControlled) { $reason += 'excess green cast' }
    if (-not $darkAreaControlled) { $reason += 'too much dark-area growth' }

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
