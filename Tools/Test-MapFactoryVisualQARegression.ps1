param(
    [string]$ScratchDir = (Join-Path $env:TEMP 'VR_MapVisualQA_Regression')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$qa = Join-Path $PSScriptRoot 'Test-MapFactoryVisualQA.ps1'
if (-not (Test-Path -LiteralPath $qa)) {
    throw "Visual QA script not found: $qa"
}

if (Test-Path -LiteralPath $ScratchDir) {
    Remove-Item -Recurse -Force $ScratchDir
}
New-Item -ItemType Directory -Force -Path $ScratchDir | Out-Null

function New-BaseMap {
    $bmp = New-Object System.Drawing.Bitmap 640,320
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::FromArgb(172,145,92))

    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(92,78,61))),55,45,155,95)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(112,97,74))),58,48,149,89)

    $tree = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(78,105,62))
    foreach ($xy in @(@(300,60),@(410,90),@(500,210),@(250,240))) {
        $g.FillEllipse($tree,$xy[0]-12,$xy[1]-22,24,44)
    }
    $tree.Dispose()

    $road = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(120,105,82)),12
    $g.DrawLine($road,0,260,640,260)
    $road.Dispose()
    $g.Dispose()
    return $bmp
}

function Add-CaseMutation {
    param(
        [System.Drawing.Bitmap]$Bmp,
        [string]$Case,
        [int]$View
    )
    $g = [System.Drawing.Graphics]::FromImage($Bmp)

    if ($Case -eq 'palette') {
        $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(24,220,125,65))
        $g.FillRectangle($brush,0,0,$Bmp.Width,$Bmp.Height)
        $brush.Dispose()
    }
    elseif ($Case -eq 'texture') {
        $rng = New-Object System.Random (1000 + $View)
        $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(145,122,78)),1
        for ($n=0; $n -lt 650; $n++) {
            $x=$rng.Next(0,640); $y=$rng.Next(0,255)
            if ($x -ge 50 -and $x -le 215 -and $y -ge 45 -and $y -le 145) { continue }
            $g.DrawLine($pen,$x,$y,$x+1,$y)
        }
        $pen.Dispose()
    }
    elseif ($Case -eq 'recolor_existing') {
        $b1=New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(58,49,41))
        $b2=New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(83,66,50))
        $g.FillRectangle($b1,55,45,155,95)
        $g.FillRectangle($b2,62,52,141,81)
        $p=New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(150,120,75)),3
        $g.DrawLine($p,70,90,195,90)
        $p.Dispose(); $b1.Dispose(); $b2.Dispose()
    }
    elseif ($Case -eq 'artifact') {
        $b=New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(39,31,25))
        $g.FillRectangle($b,330,155,42,71)
        $p=New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(195,155,92)),3
        $g.DrawRectangle($p,337,163,27,56)
        $p2=New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(122,92,59)),2
        $g.DrawLine($p2,350,163,350,219)
        $b.Dispose(); $p.Dispose(); $p2.Dispose()
    }
    elseif ($Case -eq 'artifact_small') {
        $b=New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(39,31,25))
        $g.FillRectangle($b,340,170,26,49)
        $p=New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(195,155,92)),2
        $g.DrawRectangle($p,345,176,15,37)
        $p2=New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(122,92,59)),1
        $g.DrawLine($p2,352,176,352,213)
        $b.Dispose(); $p.Dispose(); $p2.Dispose()
    }

    $g.Dispose()
}

$cases = @(
    [pscustomobject]@{ Name='palette'; ExpectedFail=$false },
    [pscustomobject]@{ Name='texture'; ExpectedFail=$false },
    [pscustomobject]@{ Name='recolor_existing'; ExpectedFail=$false },
    [pscustomobject]@{ Name='artifact'; ExpectedFail=$true },
    [pscustomobject]@{ Name='artifact_small'; ExpectedFail=$true }
)

foreach ($case in $cases) {
    $dir = Join-Path $ScratchDir $case.Name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null

    for ($i=1; $i -le 6; $i++) {
        $p = New-BaseMap
        $r = $p.Clone()

        $pPath = Join-Path $dir ("A3_PRISTINE.dat_tactical_{0:D2}_overview.png" -f $i)
        $rPath = Join-Path $dir ("A3_REMASTERED.dat_tactical_{0:D2}_overview.png" -f $i)
        $p.Save($pPath,[System.Drawing.Imaging.ImageFormat]::Png)
        Add-CaseMutation -Bmp $r -Case $case.Name -View $i
        $r.Save($rPath,[System.Drawing.Imaging.ImageFormat]::Png)
        $p.Dispose(); $r.Dispose()
    }
}

$failed = $false
$rows = @()
foreach ($case in $cases) {
    $dir = Join-Path $ScratchDir $case.Name
    $args = @(
        '-NoProfile','-ExecutionPolicy','Bypass','-File',$qa,
        '-PreviewDir',$dir,'-Maps','A3'
    )
    $proc = Start-Process -FilePath 'powershell.exe' -ArgumentList $args -Wait -PassThru -WindowStyle Hidden
    $actualFail = $proc.ExitCode -ne 0
    $ok = $actualFail -eq $case.ExpectedFail
    if (-not $ok) { $failed = $true }
    $rows += [pscustomobject]@{
        Case=$case.Name
        Expected=if($case.ExpectedFail){'FAIL'}else{'PASS'}
        Actual=if($actualFail){'FAIL'}else{'PASS'}
        Regression=if($ok){'PASS'}else{'FAIL'}
    }
}

$rows | Format-Table -AutoSize | Out-String | Write-Host
if ($failed) {
    Write-Error 'Map visual QA regression benchmark failed.'
    exit 2
}

Write-Host 'Map visual QA regression benchmark passed.'
