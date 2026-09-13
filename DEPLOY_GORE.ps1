param([string]$GameRoot = "")
$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($GameRoot)) {
    $GameRoot = Split-Path -Parent $RepoRoot
}

$SourceDir = Join-Path $RepoRoot "deploy\gore"
$TargetDir = Join-Path $GameRoot "Data-Vengeance\Tilecache"
$Files = @(
    "VR_GORE_SPRAY_SMALL.STI",
    "VR_GORE_SPRAY_MEDIUM.STI",
    "VR_GORE_SPRAY_HEAVY.STI",
    "VR_BLOOD_IMPACT.STI",
    "VR_BLOOD_DECAL_FAR.STI",
    "VR_BLOOD_DECAL_MID.STI",
    "VR_BLOOD_DECAL_NEAR.STI",
    "VR_FATAL_FALL_FORWARD.STI",
    "VR_FATAL_FALL_BACK.STI",
    "VR_FATAL_FALL_LEFT.STI",
    "VR_FATAL_FALL_RIGHT.STI",
    "VR_FATAL_CRUMPLE.STI",
    "VR_FATAL_HEAD_GIB.STI",
    "VR_FATAL_ARM_GIB.STI",
    "VR_FATAL_LEG_GIB.STI",
    "VR_FATAL_TORSO_GIB.STI",
    "VR_FATAL_CHUNKS.STI"
)

Write-Host "Source    : $SourceDir"
Write-Host "Game root : $GameRoot"
Write-Host "Target    : $TargetDir"

if (!(Test-Path $SourceDir)) { throw "Missing bundled assets: $SourceDir" }
if (!(Test-Path $GameRoot)) { throw "Game root not found: $GameRoot" }
if (!(Test-Path $TargetDir)) { New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null }

foreach ($File in $Files) {
    $Source = Join-Path $SourceDir $File
    $Target = Join-Path $TargetDir $File
    if (!(Test-Path $Source)) { throw "Missing bundled asset: $Source" }

    Copy-Item -LiteralPath $Source -Destination $Target -Force

    $SourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    $TargetHash = (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash
    if ($SourceHash -ne $TargetHash) { throw "Verification failed: $File" }

    Write-Host "[OK] $File"
}

Write-Host "Directional gore assets deployed successfully."
