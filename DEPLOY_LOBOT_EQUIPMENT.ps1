param(
    [string]$GameRoot = (Split-Path -Parent $PSScriptRoot),
    [switch]$Force
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$PinnedCommit = "cd3fec5665825d7f6c05ba0093fd238891b17d45"
$ArchiveUrl = "https://codeload.github.com/1dot13/source/zip/$PinnedCommit"
$DestData = Join-Path $GameRoot "Data-Vengeance"
$DestLobot = Join-Path $DestData "Anims\LOBOT"
$DestTable = Join-Path $DestData "TableData\LogicalBodyTypes"
$DestPalettes = Join-Path $DestData "Palettes"
$Marker = Join-Path $DestTable ".vr_lobot_source_commit.txt"

if (-not (Test-Path $DestData)) {
    throw "Data-Vengeance was not found under: $GameRoot"
}

$alreadyInstalled = $false
if ((Test-Path $Marker) -and -not $Force) {
    $installedCommit = (Get-Content $Marker -Raw).Trim()
    if ($installedCommit -eq $PinnedCommit -and (Test-Path $DestLobot)) {
        $stiCount = @(Get-ChildItem $DestLobot -Recurse -File -Filter *.sti -ErrorAction SilentlyContinue).Count
        if ($stiCount -ge 6000) {
            $alreadyInstalled = $true
            Write-Host "LOBOT asset pack already present ($stiCount STI files). Reapplying VR compatibility patches only."
        }
    }
}

$tempRoot = Join-Path $env:TEMP ("vr-lobot-" + $PinnedCommit)
$zipPath = $tempRoot + ".zip"

try {
    if (-not $alreadyInstalled) {
        if (Test-Path $tempRoot) { Remove-Item $tempRoot -Recurse -Force }
        if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

        Write-Host "Downloading pinned JA2 1.13 LOBOT asset snapshot..."
        Invoke-WebRequest -UseBasicParsing -Uri $ArchiveUrl -OutFile $zipPath

        Write-Host "Extracting LOBOT assets..."
        Expand-Archive -Path $zipPath -DestinationPath $tempRoot -Force

        $sourceRoot = Get-ChildItem $tempRoot -Directory |
            Where-Object { Test-Path (Join-Path $_.FullName "gamedir\Data\Anims\LOBOT") } |
            Select-Object -First 1

        if ($null -eq $sourceRoot) {
            throw "Pinned archive did not contain gamedir\Data\Anims\LOBOT."
        }

        $srcData = Join-Path $sourceRoot.FullName "gamedir\Data"
        $srcLobot = Join-Path $srcData "Anims\LOBOT"
        $srcTable = Join-Path $srcData "TableData\LogicalBodyTypes"
        $srcPalettes = Join-Path $srcData "Palettes"

        New-Item -ItemType Directory -Path $DestLobot -Force | Out-Null
        New-Item -ItemType Directory -Path $DestTable -Force | Out-Null
        New-Item -ItemType Directory -Path $DestPalettes -Force | Out-Null

        Copy-Item (Join-Path $srcLobot "*") $DestLobot -Recurse -Force
        Copy-Item (Join-Path $srcTable "*") $DestTable -Recurse -Force

        foreach ($paletteName in @(
            "grayscale.act",
            "guns_AK.stp",
            "guns_universal.stp",
            "guns_universal_v2.stp",
            "guns_v2_paletteswap.act",
            "guns_v2_paletteswap.stp"
        )) {
            $srcPalette = Join-Path $srcPalettes $paletteName
            if (Test-Path $srcPalette) {
                Copy-Item $srcPalette (Join-Path $DestPalettes $paletteName) -Force
            }
        }
    }

    # The pinned upstream snapshot references two palettes that are not actually
    # present in that repository revision. Native STI palettes are a safe fallback.
    $palettesXml = Join-Path $DestTable "Palettes.xml"
    if (Test-Path $palettesXml) {
        $txt = Get-Content $palettesXml -Raw
        $txt = [regex]::Replace($txt, '(?m)^\s*<Palette\s+name\s*=\s*"hats"[^\r\n]*\r?\n?', '')
        $txt = [regex]::Replace($txt, '(?m)^\s*<Palette\s+name\s*=\s*"universal_camo"[^\r\n]*\r?\n?', '')
        Set-Content -Path $palettesXml -Value $txt -Encoding UTF8
    }

    Get-ChildItem $DestTable -Recurse -File -Filter *.xml | ForEach-Object {
        $txt = Get-Content $_.FullName -Raw
        $txt = [regex]::Replace($txt, '\s+palette\s*=\s*"(?:hats|universal_camo)"', '')
        Set-Content -Path $_.FullName -Value $txt -Encoding UTF8
    }

    # Animation states/surfaces added after the Vengeance animation enum set.
    # Remove only mappings to those enums; all supported LOBOT equipment remains.
    $unsupported = @(
        "SIDE_STEP_CROUCH_RIFLE","SIDE_STEP_CROUCH_PISTOL","SIDE_STEP_CROUCH_DUAL",
        "CROUCHEDMOVE_RIFLE_READY","CROUCHEDMOVE_PISTOL_READY","CROUCHEDMOVE_DUAL_READY",
        "CRYO_DEATH","CRYO_DEATH_CROUCHED",
        "BAYONET_STAB_STANDING_VS_STANDING","BAYONET_STAB_STANDING_VS_PRONE",
        "RGMBAYONET_S_S","RGMBAYONET_S_P","BGMBAYONET_S_S","BGMBAYONET_S_P","RGFBAYONET_S_S","RGFBAYONET_S_P",
        "CRYO_EXPLODE","CRYO_EXPLODE_CROUCHED",
        "ARMED_CAR_READY","ARMED_CAR_SHOOT","ARMED_CAR_DIE",
        "RGMSIDESTEP_CROUCH_R_RDY","RGMSIDESTEP_CROUCH_P_RDY","RGMSIDESTEP_CROUCH_D_RDY",
        "BGMSIDESTEP_CROUCH_R_RDY","BGMSIDESTEP_CROUCH_P_RDY","BGMSIDESTEP_CROUCH_D_RDY",
        "RGFSIDESTEP_CROUCH_R_RDY","RGFSIDESTEP_CROUCH_P_RDY","RGFSIDESTEP_CROUCH_D_RDY",
        "RGMCROUCH_R_RDY","RGMCROUCH_P_RDY","RGMCROUCH_D_RDY",
        "BGMCROUCH_R_RDY","BGMCROUCH_P_RDY","BGMCROUCH_D_RDY",
        "RGFCROUCH_R_RDY","RGFCROUCH_P_RDY","RGFCROUCH_D_RDY"
    )
    Get-ChildItem $DestTable -Recurse -File -Filter *.xml | ForEach-Object {
        $lines = Get-Content $_.FullName
        $filtered = foreach ($line in $lines) {
            $drop = $false
            if ($line -match '<Surface\\b') {
                foreach ($animationName in $unsupported) {
                    if ($line.Contains('animstate="' + $animationName + '"') -or
                        $line.Contains('animsurface="' + $animationName + '"')) {
                        $drop = $true
                        break
                    }
                }
            }
            if (-not $drop) { $line }
        }
        Set-Content -Path $_.FullName -Value $filtered -Encoding UTF8
    }

    # Extend the compatible 2022 filters to Vengeance's later equipment IDs.
    $filtersPath = Join-Path $DestTable "Filters.xml"
    if (-not (Test-Path $filtersPath)) {
        throw "Filters.xml was not installed."
    }

    $filters = Get-Content $filtersPath -Raw

    $filters = $filters.Replace(
        '<LEGPOS op="in">838, 294</LEGPOS>',
        '<LEGPOS op="in">838, 294, 2520, 2521</LEGPOS>'
    )

    $filters = $filters.Replace(
        '<HEAD1POS op="in">213</HEAD1POS>',
        '<HEAD1POS op="in">213, 2701, 2702</HEAD1POS>'
    )
    $filters = $filters.Replace(
        '<HEAD2POS op="in">213</HEAD2POS>',
        '<HEAD2POS op="in">213, 2701, 2702</HEAD2POS>'
    )

    $extraHolsters = "1200, 1641, 1642, 1643, 1644, 1648, 1649, 1653, 1660, 1665, 1677, 1690, 2603, 2607, 2616, 2617, 2618, 2625, 2626, 2631, 2644, 2679, 2686, 2687"
    $filters = $filters.Replace(
        '<RTHIGHPOCKPOS op="in">1091, 1092, 1093</RTHIGHPOCKPOS>',
        '<RTHIGHPOCKPOS op="in">1091, 1092, 1093, ' + $extraHolsters + '</RTHIGHPOCKPOS>'
    )
    $filters = $filters.Replace(
        '<LTHIGHPOCKPOS op="in">1091, 1092, 1093</LTHIGHPOCKPOS>',
        '<LTHIGHPOCKPOS op="in">1091, 1092, 1093, ' + $extraHolsters + '</LTHIGHPOCKPOS>'
    )

    # Later Vengeance armour mapped to the nearest existing LOBOT silhouette.
    $filters = $filters.Replace(
        '<VESTPOS op="in">161, 162, 163, 283</VESTPOS>',
        '<VESTPOS op="in">161, 162, 163, 283, 2539, 2540</VESTPOS>'
    )
    $filters = $filters.Replace(
        '<VESTPOS op="in">164, 165, 166, 196, 197, 198, 812, 824, 836, 840, 842, 843, 844, 847, 848, 849, 856, 857, 858, 865, 866, 867, 1103, 1104, 1105, 1108, 1109, 1110, 1117, 1118, 1119, 1126, 1127, 1128, 1138, 1139, 1140, 1143, 1144, 1145, 1152, 1153, 1154, 1161, 1162, 1163</VESTPOS>',
        '<VESTPOS op="in">164, 165, 166, 196, 197, 198, 812, 824, 836, 840, 842, 843, 844, 847, 848, 849, 856, 857, 858, 865, 866, 867, 1103, 1104, 1105, 1108, 1109, 1110, 1117, 1118, 1119, 1126, 1127, 1128, 1138, 1139, 1140, 1143, 1144, 1145, 1152, 1153, 1154, 1161, 1162, 1163, 2527, 2528, 2529</VESTPOS>'
    )
    $filters = $filters.Replace(
        '<VESTPOS op="in">167, 168, 169, 295, 803, 806, 815, 818, 821, 827, 830, 833, 850, 851, 859, 860, 868, 869, 1111, 1112, 1120, 1121, 1129, 1130, 1146, 1147, 1155, 1156, 1164, 1165</VESTPOS>',
        '<VESTPOS op="in">167, 168, 169, 295, 803, 806, 815, 818, 821, 827, 830, 833, 850, 851, 859, 860, 868, 869, 1111, 1112, 1120, 1121, 1129, 1130, 1146, 1147, 1155, 1156, 1164, 1165, 2523</VESTPOS>'
    )
    $filters = $filters.Replace(
        '<HELMETPOS op="in">176, 177, 178, 179, 180, 181, 182, 293, 302, 801, 804, 810, 813, 816, 819, 822, 825, 828, 831, 834, 839, 1100, 1135</HELMETPOS>',
        '<HELMETPOS op="in">176, 177, 178, 179, 180, 181, 182, 293, 302, 801, 804, 810, 813, 816, 819, 822, 825, 828, 831, 834, 839, 1100, 1135, 2531</HELMETPOS>'
    )

    Set-Content -Path $filtersPath -Value $filters -Encoding UTF8

    $stiCount = @(Get-ChildItem $DestLobot -Recurse -File -Filter *.sti -ErrorAction SilentlyContinue).Count
    $xmlCount = @(Get-ChildItem $DestTable -Recurse -File -Filter *.xml -ErrorAction SilentlyContinue).Count

    if ($stiCount -lt 6000) {
        throw "LOBOT deployment incomplete: only $stiCount STI files found."
    }
    if ($xmlCount -lt 20) {
        throw "LOBOT deployment incomplete: only $xmlCount XML files found."
    }

    New-Item -ItemType Directory -Path $DestTable -Force | Out-Null
    Set-Content -Path $Marker -Value $PinnedCommit -Encoding ASCII

    # Also create the runtime readiness marker used by the integrated renderer.
    # Keep the legacy marker above for incremental/revision checks.
    $runtimeMarker = Join-Path $DestLobot "VR_EQUIPMENT.READY"
    Set-Content -Path $runtimeMarker -Value ("Legacy LOBOT deployment " + $PinnedCommit) -Encoding ASCII

    Write-Host ""
    Write-Host "LOBOT equipment graphics deployed successfully."
    Write-Host "Game root : $GameRoot"
    Write-Host "STI files : $stiCount"
    Write-Host "XML files : $xmlCount"
    Write-Host "Source pin: $PinnedCommit"
    Write-Host ""
    Write-Host "Build JA2_EN_Release.exe from the updated source branch, deploy the EXE to the game root, and start a tactical sector."
}
finally {
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force -ErrorAction SilentlyContinue }
    if (Test-Path $tempRoot) { Remove-Item $tempRoot -Recurse -Force -ErrorAction SilentlyContinue }
}
