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
Write-Host "Vengeance loading-screen deployment"
Write-Host "Source repo : $SourceRoot"
Write-Host "Game root   : $GameRoot"
Write-Host ""

$vfs = Join-Path $GameRoot "vfs_config.Vengeance.ini"
if (-not (Test-Path $vfs)) {
    throw "Game root verification failed: '$vfs' was not found. Run this script from the vr_source repo inside the Vengeance game folder, or pass -GameRoot."
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

    Write-Host "EXE deployed and verified:"
    Write-Host "  $gameExe"
}

$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) {
    throw "Git was not found in PATH. Install/use Git for Windows, then run this script again."
}

$tempRoot = Join-Path $env:TEMP "vr_gamedir_loadscreens_deploy"
if (Test-Path $tempRoot) {
    Remove-Item $tempRoot -Recurse -Force
}

Write-Host ""
Write-Host "Fetching the release loading-screen pack..."
& git clone --depth 1 --filter=blob:none --sparse --branch "install/all-2026-09-12" "https://github.com/mattedinburgh/vr_gamedir.git" $tempRoot
if ($LASTEXITCODE -ne 0) {
    throw "git clone failed with exit code $LASTEXITCODE."
}

& git -C $tempRoot sparse-checkout set "Data-Vengeance/Loadscreens/RealConflict"
if ($LASTEXITCODE -ne 0) {
    throw "git sparse-checkout failed with exit code $LASTEXITCODE."
}

$assetSource = Join-Path $tempRoot "Data-Vengeance\Loadscreens\RealConflict"
$assetDest = Join-Path $GameRoot "Data-Vengeance\Loadscreens\RealConflict"

if (-not (Test-Path $assetSource)) {
    throw "The release branch did not contain '$assetSource'."
}

New-Item -ItemType Directory -Path $assetDest -Force | Out-Null
Copy-Item (Join-Path $assetSource "*") $assetDest -Recurse -Force

$pngs = @(Get-ChildItem $assetDest -Recurse -File -Filter "*.png")
if ($pngs.Count -eq 0) {
    throw "Deployment completed but no PNG files were found in '$assetDest'."
}

$required = @(
    "TM\TM_001_1920x1080.png",
    "JM\JM_001_1920x1080.png",
    "AF\AF_001_1920x1080.png"
)

foreach ($relative in $required) {
    $full = Join-Path $assetDest $relative
    if (-not (Test-Path $full)) {
        throw "Required test asset is missing after deployment: '$full'."
    }
}

Write-Host ""
Write-Host "Loading-screen assets deployed successfully."
Write-Host "PNG files found: $($pngs.Count)"
Write-Host "Destination: $assetDest"
Write-Host ""
Write-Host "Verified test files:"
foreach ($relative in $required) {
    Write-Host "  $relative"
}
Write-Host ""
Write-Host "Deployment complete. Launch:"
Write-Host "  $GameRoot\JA2_EN_Release.exe"
Write-Host ""

try {
    Remove-Item $tempRoot -Recurse -Force
} catch {
    Write-Warning "Temporary checkout could not be removed: $tempRoot"
}
