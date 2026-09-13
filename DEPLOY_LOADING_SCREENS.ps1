param(
    [string]$GameRoot = "",
    [switch]$SkipExe,
    [switch]$Launch
)

$ErrorActionPreference = "Stop"

$SourceRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = (Resolve-Path (Join-Path $SourceRoot "..")).Path
}

Write-Host ""
Write-Host "Vengeance loading-screen deployment v6"
Write-Host "Source repo : $SourceRoot"
Write-Host "Game root   : $GameRoot"
Write-Host ""

$vfs = Join-Path $GameRoot "vfs_config.Vengeance.ini"
if (-not (Test-Path $vfs)) {
    throw "Game root verification failed: '$vfs' was not found."
}

if (-not $SkipExe) {
    $builtExe = Join-Path $SourceRoot "bin\VS2013\JA2_EN_Release.exe"
    $gameExe = Join-Path $GameRoot "JA2_EN_Release.exe"

    if (Test-Path $builtExe) {
        Copy-Item $builtExe $gameExe -Force
        $srcHash = (Get-FileHash $builtExe -Algorithm SHA256).Hash
        $dstHash = (Get-FileHash $gameExe -Algorithm SHA256).Hash
        if ($srcHash -ne $dstHash) {
            throw "EXE verification failed after copy."
        }

        Write-Host "EXE deployed and SHA256 verified:"
        Write-Host "  $gameExe"
    }
    elseif (Test-Path $gameExe) {
        Write-Host "Built release executable was not found in the repository output folder."
        Write-Host "Keeping the existing game-root executable instead:"
        Write-Host "  $gameExe"
        Write-Host "Use a freshly built Release | Win32 EXE if engine-side loadscreen changes were made."
    }
    else {
        throw "Neither built nor game-root Release executable exists. Expected '$builtExe' or '$gameExe'."
    }
}

function Test-RealConflictPack([string]$Path) {
    if (-not (Test-Path $Path)) {
        return $false
    }

    $pngs = @(Get-ChildItem $Path -Recurse -File -Filter "*.png" -ErrorAction SilentlyContinue)
    return $pngs.Count -ge 100
}

function Find-GitExecutable {
    $cmd = Get-Command git -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source) {
        return $cmd.Source
    }

    $candidates = @(
        (Join-Path $env:ProgramFiles "Git\cmd\git.exe"),
        (Join-Path $env:ProgramFiles "Git\bin\git.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\Git\cmd\git.exe")
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return $candidate
        }
    }

    $desktopRoot = Join-Path $env:LOCALAPPDATA "GitHubDesktop"
    if (Test-Path $desktopRoot) {
        $desktopGit = Get-ChildItem $desktopRoot -Directory -Filter "app-*" -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName "resources\app\git\cmd\git.exe" } |
            Where-Object { Test-Path $_ } |
            Select-Object -First 1

        if ($desktopGit) {
            return $desktopGit
        }
    }

    return $null
}

# Prefer an already-present local release pack.  The game root is normally the
# vr_gamedir checkout, so requiring Git/network here is unnecessary and brittle.
$localCandidates = @(
    (Join-Path $GameRoot "Data-Vengeance\Loadscreens\RealConflict"),
    (Join-Path $GameRoot "vr_gamedir\Data-Vengeance\Loadscreens\RealConflict"),
    (Join-Path $SourceRoot "Data-Vengeance\Loadscreens\RealConflict"),
    (Join-Path (Split-Path -Parent $SourceRoot) "vr_gamedir\Data-Vengeance\Loadscreens\RealConflict")
)

$assetSource = $null
foreach ($candidate in $localCandidates) {
    if (Test-RealConflictPack $candidate) {
        $assetSource = (Resolve-Path $candidate).Path
        Write-Host ""
        Write-Host "Using local documentary loadscreen pack:"
        Write-Host "  $assetSource"
        break
    }
}

$tempRoot = $null
if (-not $assetSource) {
    $git = Find-GitExecutable
    if (-not $git) {
        throw @"
No local documentary loadscreen pack with at least 100 PNGs was found, and Git could
not be located. Git does NOT need to be on PATH for v6; the script also checks common
Git for Windows and GitHub Desktop locations.

Expected local pack:
  $GameRoot\Data-Vengeance\Loadscreens\RealConflict
"@
    }

    $tempRoot = Join-Path $env:TEMP "vr_gamedir_loadscreens_deploy"
    if (Test-Path $tempRoot) {
        Remove-Item $tempRoot -Recurse -Force
    }

    Write-Host ""
    Write-Host "Local pack not found; fetching release pack with:"
    Write-Host "  $git"

    & $git clone --depth 1 --filter=blob:none --sparse --branch "install/all-2026-09-12" "https://github.com/mattedinburgh/vr_gamedir.git" $tempRoot
    if ($LASTEXITCODE -ne 0) {
        throw "git clone failed with exit code $LASTEXITCODE."
    }

    & $git -C $tempRoot sparse-checkout set "Data-Vengeance/Loadscreens/RealConflict"
    if ($LASTEXITCODE -ne 0) {
        throw "git sparse-checkout failed with exit code $LASTEXITCODE."
    }

    $assetSource = Join-Path $tempRoot "Data-Vengeance\Loadscreens\RealConflict"
    if (-not (Test-RealConflictPack $assetSource)) {
        throw "Fetched release pack is missing or contains fewer than 100 PNGs: '$assetSource'."
    }
}

$loadscreenDest = Join-Path $GameRoot "Data-Vengeance\Loadscreens"
$nestedDest = Join-Path $loadscreenDest "RealConflict"

New-Item -ItemType Directory -Path $loadscreenDest -Force | Out-Null

# Keep the original hierarchy for compatibility when the source is not already
# the destination.  Avoid copying a directory recursively onto itself.
$assetSourceResolved = (Resolve-Path $assetSource).Path.TrimEnd('\')
$nestedResolved = $null
if (Test-Path $nestedDest) {
    $nestedResolved = (Resolve-Path $nestedDest).Path.TrimEnd('\')
}

if (-not $nestedResolved -or $assetSourceResolved -ne $nestedResolved) {
    New-Item -ItemType Directory -Path $nestedDest -Force | Out-Null
    Copy-Item (Join-Path $assetSource "*") $nestedDest -Recurse -Force
}

# Flatten the pack into the already-established Loadscreens directory.  The
# source selector prefers these RC_* names because this VFS location is known
# to work in stock Vengeance.
Get-ChildItem $loadscreenDest -File -Filter "RC_*_1920x1080.png" -ErrorAction SilentlyContinue |
    Remove-Item -Force

$sourcePngs = @(Get-ChildItem $assetSource -Recurse -File -Filter "*.png")
if ($sourcePngs.Count -lt 100) {
    throw "Only $($sourcePngs.Count) PNGs were found; expected at least 100."
}

$pngSignature = @(137,80,78,71,13,10,26,10)
$validCount = 0
$rgbCount = 0

foreach ($src in $sourcePngs) {
    # Read through IHDR so we can verify both PNG validity and engine-safe
    # true-colour encoding without requiring Python/Pillow on the user's PC.
    $header = @(Get-Content -LiteralPath $src.FullName -Encoding Byte -TotalCount 26)

    $valid = ($header.Count -ge 26)
    if ($valid) {
        for ($i = 0; $i -lt 8; $i++) {
            if ($header[$i] -ne $pngSignature[$i]) {
                $valid = $false
                break
            }
        }
    }

    if (-not $valid) {
        throw "Invalid PNG or Git pointer detected: '$($src.FullName)'"
    }

    # PNG IHDR: byte 24 = bit depth, byte 25 = colour type.
    # 8-bit RGB true-colour is bit depth 8, colour type 2.
    if ($header[24] -ne 8 -or $header[25] -ne 2) {
        throw "Loadscreen is not 8-bit-per-channel RGB true-colour PNG: '$($src.FullName)' (bitDepth=$($header[24]), colorType=$($header[25]))."
    }
    $rgbCount++

    $flatName = "RC_" + $src.Name
    $flatPath = Join-Path $loadscreenDest $flatName
    Copy-Item $src.FullName $flatPath -Force

    $srcHash = (Get-FileHash $src.FullName -Algorithm SHA256).Hash
    $dstHash = (Get-FileHash $flatPath -Algorithm SHA256).Hash
    if ($srcHash -ne $dstHash) {
        throw "Flat deployment SHA256 verification failed: '$flatPath'"
    }

    $validCount++
}

$requiredFlat = @(
    "RC_TM_001_1920x1080.png",
    "RC_JM_001_1920x1080.png",
    "RC_AF_001_1920x1080.png"
)

foreach ($name in $requiredFlat) {
    $full = Join-Path $loadscreenDest $name
    if (-not (Test-Path $full)) {
        throw "Required flat VFS test asset is missing: '$full'."
    }
}

Write-Host ""
Write-Host "SUCCESS: documentary loading screens deployed."
Write-Host "Validated RGB PNGs : $rgbCount"
Write-Host "Flattened PNGs     : $validCount"
Write-Host "Active directory   :"
Write-Host "  $loadscreenDest"
Write-Host ""
Write-Host "Verified:"
foreach ($name in $requiredFlat) {
    Write-Host "  $name"
}
Write-Host ""
Write-Host "Launch exactly:"
Write-Host "  $GameRoot\JA2_EN_Release.exe"
Write-Host ""

if ($tempRoot -and (Test-Path $tempRoot)) {
    try {
        Remove-Item $tempRoot -Recurse -Force
    } catch {
        Write-Warning "Temporary checkout could not be removed: $tempRoot"
    }
}

if ($Launch) {
    Write-Host "Launching Vengeance..."
    Start-Process -FilePath (Join-Path $GameRoot "JA2_EN_Release.exe") -WorkingDirectory $GameRoot
}
