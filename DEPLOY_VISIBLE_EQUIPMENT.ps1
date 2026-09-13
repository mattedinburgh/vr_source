param(
    [int]$Workers = 12,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$GameRoot = Split-Path -Parent $ScriptRoot
$DataRoot = Join-Path $GameRoot "Data-Vengeance"
$TableRoot = Join-Path $DataRoot "TableData\LogicalBodyTypes"
$AnimRoot = Join-Path $DataRoot "Anims\LOBOT"
$Marker = Join-Path $AnimRoot "VR_EQUIPMENT.READY"

# Pin the matching Vengeance LOBOT catalog too. Source + catalog + upstream art
# must form one reproducible deployment set.
$VrRef = "c0b69f2834d94c6c7614b17ad3443bc272555306"
$VrRaw = "https://raw.githubusercontent.com/mattedinburgh/vr_gamedir/$VrRef/Data-Vengeance/TableData/LogicalBodyTypes"
# Pin the external art revision so the same Vengeance commit always resolves the
# same filenames and bytes. Do not deploy against a moving upstream master.
$UpstreamRef = "bdcf501e6b4db072933357a71f97243b7ab759e1"
$UpstreamRaw = "https://raw.githubusercontent.com/1dot13/gamedir/$UpstreamRef/Data"
$UpstreamApi = "https://api.github.com/repos/1dot13/gamedir"

Write-Host "Vengeance visible-equipment deployment"
Write-Host "Source repo : $ScriptRoot"
Write-Host "Game root   : $GameRoot"
Write-Host ""

if (-not (Test-Path $GameRoot -PathType Container)) {
    throw "Game root not found: $GameRoot"
}

New-Item -ItemType Directory -Force -Path $TableRoot | Out-Null
New-Item -ItemType Directory -Force -Path $AnimRoot | Out-Null

# Never leave a stale marker behind if deployment is interrupted.
if (Test-Path $Marker) {
    Remove-Item $Marker -Force
}

$configFiles = @(
    "Layers.xml",
    "Filters.xml",
    "LogicalBodyTypes.xml",
    "AnimationSurfaces.xml",
    "LBT_RGM/LogicalBodyType_RGM_VR_equipment.xml",
    "LBT_RGM/AnimationSurfaces_RGM_VR_equipment.xml",
    "LBT_BGM/LogicalBodyType_BGM_VR_equipment.xml",
    "LBT_BGM/AnimationSurfaces_BGM_VR_equipment.xml",
    "LBT_RGF/LogicalBodyType_RGF_VR_equipment.xml",
    "LBT_RGF/AnimationSurfaces_RGF_VR_equipment.xml"
)

function Get-UrlFile {
    param(
        [Parameter(Mandatory=$true)][string]$Url,
        [Parameter(Mandatory=$true)][string]$Destination
    )

    $parent = Split-Path -Parent $Destination
    if ($parent) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }

    $last = $null
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Destination
            if ((Test-Path $Destination) -and ((Get-Item $Destination).Length -gt 0)) {
                return
            }
            throw "Downloaded file is empty."
        }
        catch {
            $last = $_
            if ($attempt -lt 3) {
                Start-Sleep -Seconds $attempt
            }
        }
    }
    throw "Failed to download $Url : $last"
}

function Get-UpstreamLobotPathMap {
    param(
        [Parameter(Mandatory=$true)][string[]]$BodyDirs
    )

    $headers = @{ "User-Agent" = "VengeanceReloaded-LOBOT-Port/1.1" }
    $lobotRoot = Invoke-RestMethod -UseBasicParsing -Headers $headers -Uri "$UpstreamApi/contents/Data/Anims/LOBOT?ref=$UpstreamRef"

    $dirSha = @{}
    foreach ($item in $lobotRoot) {
        if ($item.type -eq "dir") {
            $dirSha[$item.name.ToUpperInvariant()] = $item.sha
        }
    }

    $map = New-Object "System.Collections.Generic.Dictionary[string,string]" ([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($bodyDir in $BodyDirs) {
        $dirKey = $bodyDir.ToUpperInvariant()
        if (-not $dirSha.ContainsKey($dirKey)) {
            throw "Upstream LOBOT directory not found: $bodyDir"
        }

        $sha = $dirSha[$dirKey]
        $tree = Invoke-RestMethod -UseBasicParsing -Headers $headers -Uri "$UpstreamApi/git/trees/$($sha)?recursive=1"
        if ($tree.truncated) {
            throw "GitHub returned a truncated LOBOT tree for $bodyDir; refusing an incomplete deployment."
        }

        foreach ($node in $tree.tree) {
            if ($node.type -ne "blob") { continue }

            $actualRelative = "Anims\LOBOT\$bodyDir\" + $node.path.Replace("/", "\")
            $map[$actualRelative] = $actualRelative
        }
    }

    return ,$map
}

Write-Host "Refreshing Vengeance LOBOT configuration..."
foreach ($relative in $configFiles) {
    $urlRel = $relative.Replace("\", "/")
    $destination = Join-Path $TableRoot $relative
    Get-UrlFile -Url "$VrRaw/$urlRel" -Destination $destination
}

$surfaceCatalogs = @(
    "LBT_RGM/AnimationSurfaces_RGM_VR_equipment.xml",
    "LBT_BGM/AnimationSurfaces_BGM_VR_equipment.xml",
    "LBT_RGF/AnimationSurfaces_RGF_VR_equipment.xml"
)

$assetPaths = New-Object "System.Collections.Generic.HashSet[string]" ([System.StringComparer]::OrdinalIgnoreCase)

foreach ($catalog in $surfaceCatalogs) {
    $catalogPath = Join-Path $TableRoot $catalog
    $text = [System.IO.File]::ReadAllText($catalogPath)

    # These files are parsed as external XML entities inside the parent
    # <AnimSurfaces> element. They must therefore be XML fragments containing
    # only <AnimSurface .../> entries, not standalone XML documents with their
    # own declaration or <AnimSurfaces> root.
    if ($text -match '(?i)<\?xml' -or $text -match '(?i)<\s*/?\s*AnimSurfaces\s*>') {
        throw "Invalid LBT external entity catalog (standalone XML wrapper found): $catalog"
    }

    foreach ($match in [regex]::Matches($text, 'file="([^"]+)"')) {
        $relative = $match.Groups[1].Value
        if ($relative -like "Anims\LOBOT\*") {
            [void]$assetPaths.Add($relative)
        }
    }
}

if ($assetPaths.Count -eq 0) {
    throw "No LOBOT equipment assets were found in the deployed catalogs."
}

Write-Host "Resolving upstream LOBOT filename casing..."
$bodyDirs = New-Object "System.Collections.Generic.HashSet[string]" ([System.StringComparer]::OrdinalIgnoreCase)
foreach ($relative in $assetPaths) {
    $parts = $relative -split '\\'
    if ($parts.Length -lt 4) {
        throw "Unexpected LOBOT asset path: $relative"
    }
    [void]$bodyDirs.Add($parts[2])
}

$upstreamPathMap = Get-UpstreamLobotPathMap -BodyDirs @($bodyDirs)
$unresolved = New-Object System.Collections.ArrayList
$caseCorrections = 0

foreach ($relative in $assetPaths) {
    if (-not $upstreamPathMap.ContainsKey($relative)) {
        [void]$unresolved.Add($relative)
        continue
    }

    $xmlUrlCase = $relative -replace '(?i)\.STI$', '.sti'
    if ($upstreamPathMap[$relative] -cne $xmlUrlCase) {
        $caseCorrections++
    }
}

if ($unresolved.Count -gt 0) {
    Write-Host ""
    Write-Host "References absent from upstream 1.13 LOBOT art:" -ForegroundColor Red
    $unresolved | Select-Object -First 30 | ForEach-Object { Write-Host "  $_" }
    if ($unresolved.Count -gt 30) {
        Write-Host ("  ... and {0} more" -f ($unresolved.Count - 30))
    }
    throw "Visible-equipment catalog contains unresolved upstream assets."
}

Write-Host ("Upstream case corrections   : {0}" -f $caseCorrections)

$pending = New-Object System.Collections.ArrayList
foreach ($relative in $assetPaths) {
    $destination = Join-Path $DataRoot $relative
    if ($Force -or -not (Test-Path $destination) -or (Get-Item $destination).Length -eq 0) {
        [void]$pending.Add($relative)
    }
}

Write-Host ("Equipment surfaces referenced : {0}" -f $assetPaths.Count)
Write-Host ("Equipment surfaces to fetch   : {0}" -f $pending.Count)

if ($pending.Count -gt 0) {
    Add-Type -AssemblyName System.Net.Http
    $handler = New-Object System.Net.Http.HttpClientHandler
    $client = New-Object System.Net.Http.HttpClient($handler)
    $client.Timeout = [TimeSpan]::FromMinutes(5)
    $client.DefaultRequestHeaders.UserAgent.ParseAdd("VengeanceReloaded-LOBOT-Port/1.0")

    try {
        for ($offset = 0; $offset -lt $pending.Count; $offset += $Workers) {
            $lastIndex = [Math]::Min($pending.Count - 1, $offset + $Workers - 1)
            $batch = @($pending[$offset..$lastIndex])
            $jobs = @()

            foreach ($relative in $batch) {
                if (-not $upstreamPathMap.ContainsKey($relative)) {
                    throw "No upstream path resolved for $relative"
                }
                $urlRel = $upstreamPathMap[$relative].Replace("\", "/")
                $url = "$UpstreamRaw/$urlRel"
                $destination = Join-Path $DataRoot $relative
                $parent = Split-Path -Parent $destination
                New-Item -ItemType Directory -Force -Path $parent | Out-Null

                $jobs += [pscustomobject]@{
                    Relative = $relative
                    Url = $url
                    Destination = $destination
                    Task = $client.GetByteArrayAsync($url)
                }
            }

            foreach ($job in $jobs) {
                $ok = $false
                $last = $null

                try {
                    $bytes = $job.Task.GetAwaiter().GetResult()
                    if ($bytes.Length -le 0) { throw "Empty response" }
                    [System.IO.File]::WriteAllBytes($job.Destination, $bytes)
                    $ok = $true
                }
                catch {
                    $last = $_
                }

                if (-not $ok) {
                    # Parallel request failed: retry this one conservatively.
                    for ($attempt = 1; $attempt -le 3 -and -not $ok; $attempt++) {
                        try {
                            $bytes = $client.GetByteArrayAsync($job.Url).GetAwaiter().GetResult()
                            if ($bytes.Length -le 0) { throw "Empty response" }
                            [System.IO.File]::WriteAllBytes($job.Destination, $bytes)
                            $ok = $true
                        }
                        catch {
                            $last = $_
                            if ($attempt -lt 3) { Start-Sleep -Seconds $attempt }
                        }
                    }
                }

                if (-not $ok) {
                    throw "Failed to download $($job.Relative): $last"
                }
            }

            $done = [Math]::Min($pending.Count, $lastIndex + 1)
            Write-Progress -Activity "Downloading visible armour animation layers" -Status "$done / $($pending.Count)" -PercentComplete (($done * 100.0) / $pending.Count)
        }
    }
    finally {
        if ($client) { $client.Dispose() }
        if ($handler) { $handler.Dispose() }
        Write-Progress -Activity "Downloading visible armour animation layers" -Completed
    }
}

Write-Host "Verifying deployed assets..."
$missing = New-Object System.Collections.ArrayList
$totalBytes = [int64]0
foreach ($relative in $assetPaths) {
    $destination = Join-Path $DataRoot $relative
    if (-not (Test-Path $destination) -or (Get-Item $destination).Length -eq 0) {
        [void]$missing.Add($relative)
    }
    else {
        $totalBytes += (Get-Item $destination).Length
    }
}

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Host "Missing files:" -ForegroundColor Red
    $missing | Select-Object -First 30 | ForEach-Object { Write-Host "  $_" }
    if ($missing.Count -gt 30) {
        Write-Host ("  ... and {0} more" -f ($missing.Count - 30))
    }
    throw "Visible-equipment deployment incomplete. Enable marker was NOT created."
}

$markerText = @"
Vengeance Reloaded visible tactical equipment
Catalog: mattedinburgh/vr_gamedir $VrRef
Source: 1dot13/gamedir $UpstreamRef Data/Anims/LOBOT art
Mode: overlay-only (native Vengeance body + 1.13 helmet/vest armour layers)
Assets: $($assetPaths.Count)
Bytes: $totalBytes
"@
[System.IO.File]::WriteAllText($Marker, $markerText, [System.Text.Encoding]::ASCII)

Write-Host ""
Write-Host ("Pinned Vengeance catalog      : {0}" -f $VrRef)
Write-Host ("Pinned upstream revision      : {0}" -f $UpstreamRef)
Write-Host "VISIBLE EQUIPMENT ASSETS VERIFIED"
Write-Host ("Files : {0}" -f $assetPaths.Count)
Write-Host ("Size  : {0:N1} MiB" -f ($totalBytes / 1MB))
Write-Host "Marker: $Marker"
Write-Host ""
Write-Host "Rebuild/run the current install/all-2026-09-12 source. Helmets and torso armour are now eligible for tactical rendering."
