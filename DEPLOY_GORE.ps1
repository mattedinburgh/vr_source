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
    "VR_BLOOD_IMPACT.STI"
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
