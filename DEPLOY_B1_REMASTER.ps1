param(
    [string]$GameRoot = "C:\\VENGENCE\\Jagged Alliance 2"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$Repo = "mattedinburgh/vr_gamedir"
$Branch = "install/all-2026-09-12"
$ApiUrl = "https://api.github.com/repos/$Repo/contents/Data-Maps-Tiles/Tilesets/50?ref=install%2Fall-2026-09-12"
$Headers = @{ "User-Agent" = "JA2-Vengeance-B1-Remaster-Deployer" }
$TargetDir = Join-Path $GameRoot "Data-Maps-Tiles\\Tilesets\\50"

Write-Host ""
Write-Host "=== Vengeance B1 remaster deployment ==="
Write-Host "Game root : $GameRoot"
Write-Host "Source    : $Repo / $Branch"
Write-Host ""

if (-not (Test-Path $GameRoot)) {
    throw "Game root does not exist: $GameRoot"
}

$Exe = Join-Path $GameRoot "JA2_EN_Release.exe"
if (-not (Test-Path $Exe)) {
    throw "JA2_EN_Release.exe not found in game root: $GameRoot"
}

New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null

Write-Host "Reading B1 asset manifest from GitHub..."
$Listing = Invoke-RestMethod -Uri $ApiUrl -Headers $Headers -UseBasicParsing

$Assets = @(
    $Listing | Where-Object {
        ($_.name -match '^B1_.*\\.(sti|jsd|b1tc)$') -or
        ($_.name -match '^(Oil_Debris|Oil_decal)\\.b1tc$')
    } | Sort-Object name
)

if ($Assets.Count -lt 54) {
    throw "Expected at least 54 B1 remaster files, found only $($Assets.Count). Deployment aborted."
}

$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$BackupDir = Join-Path $GameRoot "B1_Remaster_Backup\\$Stamp"
$Existing = Get-ChildItem -Path $TargetDir -File -ErrorAction SilentlyContinue |
    Where-Object {
        ($_.Name -match '^B1_.*\\.(sti|jsd|b1tc)$') -or
        ($_.Name -match '^(Oil_Debris|Oil_decal)\\.b1tc$')
    }

if ($Existing.Count -gt 0) {
    New-Item -ItemType Directory -Path $BackupDir -Force | Out-Null
    foreach ($File in $Existing) {
        Copy-Item -LiteralPath $File.FullName -Destination (Join-Path $BackupDir $File.Name) -Force
    }
    Write-Host "Backed up $($Existing.Count) existing remaster files to:"
    Write-Host "  $BackupDir"
}

Write-Host "Deploying $($Assets.Count) B1 files..."

$Downloaded = 0
foreach ($Asset in $Assets) {
    $Destination = Join-Path $TargetDir $Asset.name
    $Temporary = "$Destination.download"

    if (Test-Path $Temporary) {
        Remove-Item -LiteralPath $Temporary -Force
    }

    Write-Host ("  [{0,2}/{1}] {2}" -f ($Downloaded + 1), $Assets.Count, $Asset.name)

    Invoke-WebRequest -Uri $Asset.download_url -Headers $Headers -OutFile $Temporary -UseBasicParsing

    $ActualLength = (Get-Item -LiteralPath $Temporary).Length
    if ($ActualLength -ne [int64]$Asset.size) {
        Remove-Item -LiteralPath $Temporary -Force -ErrorAction SilentlyContinue
        throw "Size verification failed for $($Asset.name): expected $($Asset.size), got $ActualLength"
    }

    Move-Item -LiteralPath $Temporary -Destination $Destination -Force
    $Downloaded++
}

$Required = @(
    "B1_T_SAND1.STI", "B1_T_SAND1.b1tc",
    "B1_T_SAND3.STI", "B1_T_SAND3.b1tc",
    "B1_TRPGRAS.STI", "B1_TRPGRAS.b1tc",
    "B1_TRPGRAS2.STI", "B1_TRPGRAS2.b1tc",
    "B1_TRPGRAS3.STI", "B1_TRPGRAS3.b1tc",
    "B1_TRPGRAS4.STI", "B1_TRPGRAS4.b1tc",
    "B1_T_TRAIL.STI", "B1_T_TRAIL.b1tc",
    "B1_TR_WATER.STI", "B1_TR_WATER.b1tc",
    "B1_TRWATER2.STI", "B1_TRWATER2.b1tc",
    "B1_GRASS1.STI", "B1_GRASS1.b1tc",
    "B1_BUILD_31.STI", "B1_BUILD_31.b1tc",
    "B1_BUILD_35.STI", "B1_BUILD_35.b1tc",
    "B1_BUILD_36.STI", "B1_BUILD_36.b1tc",
    "B1_BUILD_40.STI", "B1_BUILD_40.b1tc",
    "B1_ROADTLE2.STI", "B1_ROADTLE2.b1tc",
    "B1_WELFLOR1.STI", "B1_WELFLOR1.b1tc",
    "B1_WELFLOR2.STI", "B1_WELFLOR2.b1tc",
    "B1_WELFLOR3.STI", "B1_WELFLOR3.b1tc",
    "B1_P-FLOOR3.STI", "B1_P-FLOOR3.b1tc",
    "B1_W-ROOF2.sti", "B1_W-ROOF2.b1tc",
    "B1_Rooffan.sti", "B1_Rooffan.b1tc",
    "B1_Oil_OROOF.sti", "B1_Oil_OROOF.b1tc"
)

$Missing = @()
foreach ($Name in $Required) {
    if (-not (Test-Path (Join-Path $TargetDir $Name))) {
        $Missing += $Name
    }
}

if ($Missing.Count -gt 0) {
    Write-Host ""
    Write-Host "DEPLOYMENT FAILED. Missing required files:"
    $Missing | ForEach-Object { Write-Host "  $_" }
    throw "$($Missing.Count) required B1 files are missing after deployment."
}

$Marker = Join-Path $GameRoot "B1_REMASTER_DEPLOYED.txt"
$MarkerText = @"
B1 remaster deployed successfully
Source repository: $Repo
Source branch: $Branch
Files deployed: $Downloaded
Target: $TargetDir
Deployment time: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
"@
$MarkerText | Set-Content -LiteralPath $Marker -Encoding ASCII

Write-Host ""
Write-Host "SUCCESS: B1 remaster assets deployed and verified."
Write-Host "Files deployed: $Downloaded"
Write-Host "Target        : $TargetDir"
Write-Host "Marker        : $Marker"
Write-Host ""
Write-Host "Launch JA2_EN_Release.exe and enter B1."
Write-Host "The black box should no longer report ASSET MISSING for B1_*."
