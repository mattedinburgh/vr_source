param(
    [string]$GameRoot = "",
    [switch]$SkipExe
)

$ErrorActionPreference = "Stop"

$SourceRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = (Resolve-Path (Join-Path $SourceRoot "..")).Path
}

Write-Host ""
Write-Host "Vengeance loading-screen deployment v4"
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

    if (-not (Test-Path $builtExe)) {
        throw "Release executable not found: '$builtExe'. Build Release | Win32 first."
    }

    Copy-Item $builtExe $gameExe -Force
    $srcHash = (Get-FileHash $builtExe -Algorithm SHA256).Hash
    $dstHash = (Get-FileHash $gameExe -Algorithm SHA256).Hash
    if ($srcHash -ne $dstHash) {
        throw "EXE verification failed after copy."
    }

    Write-Host "EXE deployed and SHA256 verified:"
    Write-Host "  $gameExe"
}

$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) {
    throw "Git was not found in PATH."
}

$tempRoot = Join-Path $env:TEMP "vr_gamedir_loadscreens_deploy"
if (Test-Path $tempRoot) {
    Remove-Item $tempRoot -Recurse -Force
}

Write-Host ""
Write-Host "Fetching release loading-screen pack..."
& git clone --depth 1 --filter=blob:none --sparse --branch "install/all-2026-09-12" "https://github.com/mattedinburgh/vr_gamedir.git" $tempRoot
if ($LASTEXITCODE -ne 0) {
    throw "git clone failed with exit code $LASTEXITCODE."
}

& git -C $tempRoot sparse-checkout set "Data-Vengeance/Loadscreens/RealConflict"
if ($LASTEXITCODE -ne 0) {
    throw "git sparse-checkout failed with exit code $LASTEXITCODE."
}

$assetSource = Join-Path $tempRoot "Data-Vengeance\Loadscreens\RealConflict"
$loadscreenDest = Join-Path $GameRoot "Data-Vengeance\Loadscreens"
$nestedDest = Join-Path $loadscreenDest "RealConflict"

if (-not (Test-Path $assetSource)) {
    throw "Release branch did not contain '$assetSource'."
}

New-Item -ItemType Directory -Path $loadscreenDest -Force | Out-Null
New-Item -ItemType Directory -Path $nestedDest -Force | Out-Null

# Keep the original hierarchy for compatibility.
Copy-Item (Join-Path $assetSource "*") $nestedDest -Recurse -Force

# Also flatten the pack into the already-established Loadscreens directory.
# This deliberately bypasses any ambiguity around newly introduced nested VFS folders.
Get-ChildItem $loadscreenDest -File -Filter "RC_*_1920x1080.png" -ErrorAction SilentlyContinue |
    Remove-Item -Force

$sourcePngs = @(Get-ChildItem $assetSource -Recurse -File -Filter "*.png")
if ($sourcePngs.Count -eq 0) {
    throw "No PNG files were fetched from the release pack."
}

$pngSignature = @(137,80,78,71,13,10,26,10)
$validCount = 0

foreach ($src in $sourcePngs) {
    $bytes = @(Get-Content -LiteralPath $src.FullName -Encoding Byte -TotalCount 8)
    $valid = ($bytes.Count -eq 8)
    if ($valid) {
        for ($i = 0; $i -lt 8; $i++) {
            if ($bytes[$i] -ne $pngSignature[$i]) {
                $valid = $false
                break
            }
        }
    }
    if (-not $valid) {
        throw "Invalid PNG or Git pointer detected: '$($src.FullName)'"
    }

    $flatName = "RC_" + $src.Name
    $flatPath = Join-Path $loadscreenDest $flatName
    Copy-Item $src.FullName $flatPath -Force

    $destBytes = @(Get-Content -LiteralPath $flatPath -Encoding Byte -TotalCount 8)
    if ($destBytes.Count -ne 8) {
        throw "Flat deployment verification failed: '$flatPath'"
    }
    for ($i = 0; $i -lt 8; $i++) {
        if ($destBytes[$i] -ne $pngSignature[$i]) {
            throw "Flat deployed file is not a valid PNG: '$flatPath'"
        }
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
Write-Host "Valid PNGs flattened into active Loadscreens directory: $validCount"
Write-Host "Active directory:"
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

try {
    Remove-Item $tempRoot -Recurse -Force
} catch {
    Write-Warning "Temporary checkout could not be removed: $tempRoot"
}
