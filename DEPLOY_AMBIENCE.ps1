param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$GameRoot = Split-Path -Parent $ScriptRoot
$DataRoot = Join-Path $GameRoot "Data-Vengeance"
$SoundRoot = Join-Path $DataRoot "Sounds\VR_Ambience"
$ManifestPath = Join-Path $SoundRoot "VR_AMBIENCE_DEPLOY.json"

# Pin the exact Vengeance gamedir revision used by this source integration.
# When this pin is advanced later, the per-file SHA manifest still ensures
# only changed/new assets are transferred.
$VrRef = "4760ee14edf75b4a26886f3fa0493109b7fbf15e"
$Repo = "mattedinburgh/vr_gamedir"
$ApiBase = "https://api.github.com/repos/$Repo"
$RawBase = "https://raw.githubusercontent.com/$Repo/$VrRef"

Write-Host "Vengeance sector ambience deployment"
Write-Host "Source repo : $ScriptRoot"
Write-Host "Game root   : $GameRoot"
Write-Host "Game data   : $DataRoot"
Write-Host "Gamedir ref : $VrRef"
Write-Host ""

if (-not (Test-Path $GameRoot -PathType Container)) {
    throw "Game root not found: $GameRoot"
}

New-Item -ItemType Directory -Force -Path $DataRoot | Out-Null
New-Item -ItemType Directory -Force -Path $SoundRoot | Out-Null

$headers = @{ "User-Agent" = "VengeanceReloaded-AmbienceDeploy/1.0" }

function Get-RemoteFile {
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
            Invoke-WebRequest -UseBasicParsing -Headers $headers -Uri $Url -OutFile $Destination
            if ((Test-Path $Destination) -and ((Get-Item $Destination).Length -gt 0)) {
                return
            }
            throw "Downloaded file is empty."
        }
        catch {
            $last = $_
            if ($attempt -lt 3) { Start-Sleep -Seconds $attempt }
        }
    }

    throw "Failed to download $Url : $last"
}

$previous = @{}
if ((-not $Force) -and (Test-Path $ManifestPath)) {
    try {
        $doc = [System.IO.File]::ReadAllText($ManifestPath) | ConvertFrom-Json
        if ($null -ne $doc.Assets) {
            foreach ($property in $doc.Assets.PSObject.Properties) {
                $previous[$property.Name] = [string]$property.Value
            }
        }
    }
    catch {
        Write-Host ("Existing ambience manifest ignored: {0}" -f $_.Exception.Message) -ForegroundColor Yellow
        $previous = @{}
    }
}

# Query GitHub for current blob SHAs at the pinned revision.
$configApi = "$ApiBase/contents/Data-Vengeance/SectorAmbience.ini?ref=$VrRef"
$soundsApi = "$ApiBase/contents/Data-Vengeance/Sounds/VR_Ambience?ref=$VrRef"

$configNode = Invoke-RestMethod -UseBasicParsing -Headers $headers -Uri $configApi
$soundNodes = Invoke-RestMethod -UseBasicParsing -Headers $headers -Uri $soundsApi

$remote = New-Object "System.Collections.Generic.List[object]"
$remote.Add([pscustomobject]@{
    Key = "SectorAmbience.ini"
    Sha = [string]$configNode.sha
    Url = "$RawBase/Data-Vengeance/SectorAmbience.ini"
    Destination = Join-Path $DataRoot "SectorAmbience.ini"
})

foreach ($node in $soundNodes) {
    if ($node.type -ne "file") { continue }
    if (-not ([string]$node.name).ToLowerInvariant().EndsWith(".wav")) { continue }

    $name = [string]$node.name
    $remote.Add([pscustomobject]@{
        Key = "Sounds/VR_Ambience/$name"
        Sha = [string]$node.sha
        Url = "$RawBase/Data-Vengeance/Sounds/VR_Ambience/$name"
        Destination = Join-Path $SoundRoot $name
    })
}

if ($remote.Count -lt 2) {
    throw "Remote ambience catalog is unexpectedly empty; refusing incomplete deployment."
}

$downloaded = 0
$skipped = 0
$current = @{}

foreach ($asset in $remote) {
    $current[$asset.Key] = $asset.Sha

    $needsCopy = $Force -or (-not (Test-Path $asset.Destination))
    if (-not $needsCopy) {
        if (-not $previous.ContainsKey($asset.Key)) {
            $needsCopy = $true
        }
        elseif ($previous[$asset.Key] -ne $asset.Sha) {
            $needsCopy = $true
        }
    }

    if ($needsCopy) {
        Write-Host ("DEPLOY  {0}" -f $asset.Key)
        Get-RemoteFile -Url $asset.Url -Destination $asset.Destination
        $downloaded++
    }
    else {
        $skipped++
    }
}

# Remove only stale files previously managed by this script.  Never touch
# unrelated user/mod audio in the same directory.
foreach ($key in @($previous.Keys)) {
    if ($current.ContainsKey($key)) { continue }

    if ($key -eq "SectorAmbience.ini") {
        $stale = Join-Path $DataRoot "SectorAmbience.ini"
    }
    elseif ($key.StartsWith("Sounds/VR_Ambience/")) {
        $leaf = Split-Path -Leaf $key
        $stale = Join-Path $SoundRoot $leaf
    }
    else {
        continue
    }

    if (Test-Path $stale) {
        Write-Host ("REMOVE  {0}" -f $key)
        Remove-Item $stale -Force
    }
}

$manifest = [ordered]@{
    Format = 1
    Repository = $Repo
    Ref = $VrRef
    GeneratedUtc = [DateTime]::UtcNow.ToString("o")
    Assets = [ordered]@{}
}

foreach ($key in ($current.Keys | Sort-Object)) {
    $manifest.Assets[$key] = $current[$key]
}

$manifestJson = $manifest | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText($ManifestPath, $manifestJson, [System.Text.Encoding]::UTF8)

Write-Host ""
function Test-DeployedAmbience {
    param(
        [Parameter(Mandatory=$true)][string]$IniPath,
        [Parameter(Mandatory=$true)][string]$Root
    )

    if (-not (Test-Path $IniPath)) {
        throw "Ambience validation failed: missing $IniPath"
    }

    $lines = Get-Content -LiteralPath $IniPath
    $sections = @{}
    $current = ""

    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+?)\]\s*
            $current = $Matches[1]
            $sections[$current] = $true
        }
    }

    $assetRefs = 0
    $profileRefs = 0
    $errors = New-Object System.Collections.Generic.List[string]
    $current = ""

    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+?)\]\s*
            $current = $Matches[1]
            continue
        }

        if ($line -notmatch '^\s*([^;][^=]*?)\s*=\s*(.*?)\s*

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) {
            $current = $Matches[1]
            $sections[$current] = $true
        }
    }

    $assetRefs = 0
    $profileRefs = 0
    $errors = New-Object System.Collections.Generic.List[string]
    $current = ""

    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+?)\]\s*) {
            $current = $Matches[1]
            continue
        }

        if ($line -notmatch '^\s*([^;][^=]*?)\s*=\s*(.*?)\s*) { continue }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) {
            $current = $Matches[1]
            continue
        }

        if ($line -notmatch '^\s*([^;][^=]*?)\s*=\s*(.*?)\s*) { continue }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) {
            $current = $Matches[1]
            $sections[$current] = $true
        }
    }

    $assetRefs = 0
    $profileRefs = 0
    $errors = New-Object System.Collections.Generic.List[string]
    $current = ""

    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+?)\]\s*) {
            $current = $Matches[1]
            continue
        }

        if ($line -notmatch '^\s*([^;][^=]*?)\s*=\s*(.*?)\s*) { continue }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) { continue }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) {
            $current = $Matches[1]
            $sections[$current] = $true
        }
    }

    $assetRefs = 0
    $profileRefs = 0
    $errors = New-Object System.Collections.Generic.List[string]
    $current = ""

    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+?)\]\s*) {
            $current = $Matches[1]
            continue
        }

        if ($line -notmatch '^\s*([^;][^=]*?)\s*=\s*(.*?)\s*) { continue }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) {
            $current = $Matches[1]
            continue
        }

        if ($line -notmatch '^\s*([^;][^=]*?)\s*=\s*(.*?)\s*) { continue }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) {
            $current = $Matches[1]
            $sections[$current] = $true
        }
    }

    $assetRefs = 0
    $profileRefs = 0
    $errors = New-Object System.Collections.Generic.List[string]
    $current = ""

    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+?)\]\s*) {
            $current = $Matches[1]
            continue
        }

        if ($line -notmatch '^\s*([^;][^=]*?)\s*=\s*(.*?)\s*) { continue }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) {
            $current = $Matches[1]
            $sections[$current] = $true
        }
    }

    $assetRefs = 0
    $profileRefs = 0
    $errors = New-Object System.Collections.Generic.List[string]
    $current = ""

    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+?)\]\s*) {
            $current = $Matches[1]
            continue
        }

        if ($line -notmatch '^\s*([^;][^=]*?)\s*=\s*(.*?)\s*) { continue }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) {
            $current = $Matches[1]
            continue
        }

        if ($line -notmatch '^\s*([^;][^=]*?)\s*=\s*(.*?)\s*) { continue }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) {
            $current = $Matches[1]
            $sections[$current] = $true
        }
    }

    $assetRefs = 0
    $profileRefs = 0
    $errors = New-Object System.Collections.Generic.List[string]
    $current = ""

    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+?)\]\s*) {
            $current = $Matches[1]
            continue
        }

        if ($line -notmatch '^\s*([^;][^=]*?)\s*=\s*(.*?)\s*) { continue }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) { continue }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) {
            $current = $Matches[1]
            $sections[$current] = $true
        }
    }

    $assetRefs = 0
    $profileRefs = 0
    $errors = New-Object System.Collections.Generic.List[string]
    $current = ""

    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+?)\]\s*) {
            $current = $Matches[1]
            continue
        }

        if ($line -notmatch '^\s*([^;][^=]*?)\s*=\s*(.*?)\s*) { continue }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) {
            $current = $Matches[1]
            continue
        }

        if ($line -notmatch '^\s*([^;][^=]*?)\s*=\s*(.*?)\s*) { continue }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
) {
            $current = $Matches[1]
            $sections[$current] = $true
        }
    }

    $assetRefs = 0
    $profileRefs = 0
    $errors = New-Object System.Collections.Generic.List[string]
    $current = ""

    foreach ($line in $lines) {
        if ($line -match '^\s*\[(.+?)\]\s*) {
            $current = $Matches[1]
            continue
        }

        if ($line -notmatch '^\s*([^;][^=]*?)\s*=\s*(.*?)\s*) { continue }

        $key = $Matches[1].Trim()
        $value = $Matches[2].Trim()
        if (-not $value) { continue }

        if ($key -match '_(LOOP|SOUND_\d+)) {
            $assetRefs++
            $relative = $value -replace '/', '\'
            if ($relative -match '^(?i)(Sounds|AMBIENT)\\') {
                $relative = "Data-Vengeance\$relative"
            }

            $path = Join-Path $Root $relative
            if (-not (Test-Path -LiteralPath $path)) {
                $errors.Add("Missing asset: [$current] $key = $value")
                continue
            }

            if ([IO.Path]::GetExtension($path) -ieq ".wav") {
                $stream = [IO.File]::OpenRead($path)
                try {
                    if ($stream.Length -lt 12) {
                        $errors.Add("Invalid WAV (too short): $value")
                    }
                    else {
                        $header = New-Object byte[] 12
                        [void]$stream.Read($header, 0, 12)
                        $riff = [Text.Encoding]::ASCII.GetString($header, 0, 4)
                        $wave = [Text.Encoding]::ASCII.GetString($header, 8, 4)
                        if ($riff -ne "RIFF" -or $wave -ne "WAVE") {
                            $errors.Add("Invalid WAV header: $value")
                        }
                    }
                }
                finally {
                    $stream.Dispose()
                }
            }
        }

        if ($current -eq "SECTOR_OVERRIDES" -or $current -eq "TILESET_PROFILES") {
            $profileRefs++
            if (-not $sections.ContainsKey("PROFILE_$value")) {
                $errors.Add("Missing profile: [$current] $key = $value")
            }
        }
    }

    if ($errors.Count -gt 0) {
        $errors | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Red }
        throw "Ambience deployment validation failed with $($errors.Count) error(s)."
    }

    Write-Host ("Validated ambience deployment: {0} asset refs, {1} profile refs, 0 missing." -f $assetRefs, $profileRefs)
}

Test-DeployedAmbience -IniPath (Join-Path $DataRoot "SectorAmbience.ini") -Root $GameRoot

Write-Host ("Ambience deployment complete: {0} deployed, {1} unchanged." -f $downloaded, $skipped)
Write-Host ("Manifest: {0}" -f $ManifestPath)
