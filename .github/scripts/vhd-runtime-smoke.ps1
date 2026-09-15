param(
    [Parameter(Mandatory=$true)]
    [string]$ExePath,
    [string]$GameRoot = 'C:\VENGENCE\Jagged Alliance 2',
    [int]$SmokeSeconds = 20
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $ExePath)) {
    throw "VHD executable not found: $ExePath"
}
if (-not (Test-Path $GameRoot)) {
    throw "Installed Vengeance root not found: $GameRoot"
}

$runAttempt = if ($env:GITHUB_RUN_ATTEMPT) { $env:GITHUB_RUN_ATTEMPT } else { '1' }
$smokeRoot = Join-Path $env:RUNNER_TEMP ("VHD_SMOKE_" + $env:GITHUB_RUN_ID + "_" + $runAttempt)
$logOut = Join-Path $env:GITHUB_WORKSPACE 'vhd-runtime-smoke-logs'

# Safely reclaim old VHD smoke sandboxes from prior runs. Remove junctions first
# so cleanup can never recurse into the real game-data directories.
Get-ChildItem -LiteralPath $env:RUNNER_TEMP -Directory -Filter 'VHD_SMOKE_*' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -ne $smokeRoot } |
    ForEach-Object {
        $oldRoot = $_.FullName
        Get-ChildItem -LiteralPath $oldRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.LinkType -eq 'Junction' } |
            ForEach-Object { & cmd.exe /c rmdir "$($_.FullName)" | Out-Null }
        Remove-Item -LiteralPath $oldRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

if (Test-Path $smokeRoot) {
    throw "Refusing to reuse an existing VHD smoke directory: $smokeRoot"
}
New-Item -ItemType Directory -Path $smokeRoot | Out-Null

if (Test-Path $logOut) {
    Remove-Item $logOut -Recurse -Force
}
New-Item -ItemType Directory -Path $logOut | Out-Null

# Copy root files only. This leaves the normal install untouched.
Get-ChildItem -LiteralPath $GameRoot -File | ForEach-Object {
    if ($_.Name -notmatch '^JA2_.*\.exe$') {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $smokeRoot $_.Name) -Force
    }
}

# Reuse large, read-only game data through directory junctions.
$readOnlyDirs = Get-ChildItem -LiteralPath $GameRoot -Directory |
    Where-Object { $_.Name -eq 'Data' -or $_.Name -like 'Data-*' -or $_.Name -eq 'Shaders' }

foreach ($dir in $readOnlyDirs) {
    $link = Join-Path $smokeRoot $dir.Name
    New-Item -ItemType Junction -Path $link -Target $dir.FullName | Out-Null
}

# Keep profile/settings writes local to the smoke directory.
$smokeProfile = Join-Path $smokeRoot 'Profiles\UserProfile_Vengeance'
New-Item -ItemType Directory -Path $smokeProfile -Force | Out-Null
$installedProfile = Join-Path $GameRoot 'Profiles\UserProfile_Vengeance'
if (Test-Path $installedProfile) {
    Get-ChildItem -LiteralPath $installedProfile -File -Filter '*.ini' | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $smokeProfile $_.Name) -Force
    }
}

Copy-Item -LiteralPath $ExePath -Destination (Join-Path $smokeRoot 'JA2_EN_Release.exe') -Force

# Force an unobtrusive window in the isolated copy.
$ddraw = Join-Path $smokeRoot 'ddraw.ini'
if (Test-Path $ddraw) {
    $cfg = Get-Content -LiteralPath $ddraw -Raw
    $cfg = $cfg -replace '(?m)^fullscreen=.*$', 'fullscreen=false'
    $cfg = $cfg -replace '(?m)^windowed=.*$', 'windowed=true'
    $cfg = $cfg -replace '(?m)^width=.*$', 'width=1280'
    $cfg = $cfg -replace '(?m)^height=.*$', 'height=720'
    $cfg = $cfg -replace '(?m)^savesettings=.*$', 'savesettings=0'

    try {
        Add-Type -AssemblyName System.Windows.Forms
        $secondary = [System.Windows.Forms.Screen]::AllScreens |
            Where-Object { -not $_.Primary } |
            Select-Object -First 1
        if ($secondary) {
            $posX = $secondary.Bounds.X + 20
            $posY = $secondary.Bounds.Y + 20
            $cfg = $cfg -replace '(?m)^posX=.*$', ("posX=" + $posX)
            $cfg = $cfg -replace '(?m)^posY=.*$', ("posY=" + $posY)
        }
    }
    catch {
        Write-Host "Secondary-monitor detection unavailable; cnc-ddraw will center the smoke window."
    }

    Set-Content -LiteralPath $ddraw -Value $cfg -Encoding ASCII
}

$env:VR_VHD_RENDER_SCALE = '2'
$env:VR_VHD_NATIVE_CONTRACT_TEST = '1'
$env:VR_VHD_TILE_CACHE_TEST = '1'
$contractMarker = Join-Path $smokeRoot 'vhd-native-contract-selftest.ok'
$tileCacheMarker = Join-Path $smokeRoot 'vhd-tile-cache-selftest.ok'

$exe = Join-Path $smokeRoot 'JA2_EN_Release.exe'
$started = Get-Date
Write-Host "Starting isolated VHD 2x smoke test from: $smokeRoot"

$proc = Start-Process -FilePath $exe -WorkingDirectory $smokeRoot -PassThru -WindowStyle Minimized
$survived = $false

try {
    $deadline = (Get-Date).AddSeconds($SmokeSeconds)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $proc.Refresh()
        if ($proc.HasExited) {
            throw "VHD exited during startup smoke test. ExitCode=$($proc.ExitCode)"
        }

        $title = $proc.MainWindowTitle
        if ($title -match '(?i)fatal\s+error|\berror\b') {
            throw "VHD displayed an error dialog during startup: $title"
        }
    }
    if (-not (Test-Path -LiteralPath $contractMarker)) {
        throw "VHD native asset contract self-test did not complete during startup."
    }
    Write-Host "VHD native asset contract self-test passed."
    if (-not (Test-Path -LiteralPath $tileCacheMarker)) {
        throw "VHD tile cache self-test did not complete during startup."
    }
    Write-Host "VHD tile cache residency self-test passed."

    $survived = $true
    $proc.Refresh()
    Write-Host "VHD remained alive for $SmokeSeconds seconds. MainWindowHandle=$($proc.MainWindowHandle)"
}
finally {
    try {
        $proc.Refresh()
        if (-not $proc.HasExited) {
            Stop-Process -Id $proc.Id -Force
        }
    }
    catch {
        Write-Host "Process cleanup: $($_.Exception.Message)"
    }

    # Collect compact diagnostics only. Large JA2 logs can be hundreds of MB;
    # keep at most the last 2 MiB of each text log and skip large dump files.
    $diagFiles = @()
    $diagFiles += Get-ChildItem -LiteralPath $smokeRoot -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in @('.log','.txt','.dmp') -or $_.Name -match 'BlackBox|Crash|error' }

    if (Test-Path $smokeProfile) {
        $diagFiles += Get-ChildItem -LiteralPath $smokeProfile -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.Extension -in @('.log','.txt','.dmp') -or $_.Name -match 'BlackBox|Crash|error' }
    }

    foreach ($diag in $diagFiles) {
        $safeName = ($diag.FullName.Substring($smokeRoot.Length).TrimStart('\') -replace '[\\/:*?"<>| ]','_')
        $dest = Join-Path $logOut $safeName

        if ($diag.Extension -ieq '.dmp') {
            if ($diag.Length -le 20MB) {
                Copy-Item -LiteralPath $diag.FullName -Destination $dest -Force
            }
            else {
                "Skipped large dump: $($diag.FullName) size=$($diag.Length)" |
                    Add-Content -LiteralPath (Join-Path $logOut 'skipped-large-files.txt')
            }
            continue
        }

        if ($diag.Length -le 2MB) {
            Copy-Item -LiteralPath $diag.FullName -Destination $dest -Force
        }
        else {
            $fs = [System.IO.File]::Open($diag.FullName, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
            try {
                $tailBytes = [Math]::Min([Int64](2MB), $fs.Length)
                $fs.Seek(-$tailBytes, [System.IO.SeekOrigin]::End) | Out-Null
                $buffer = New-Object byte[] $tailBytes
                [void]$fs.Read($buffer, 0, $buffer.Length)
                [System.IO.File]::WriteAllBytes($dest, $buffer)
            }
            finally {
                $fs.Dispose()
            }
        }
    }

    # Remove junctions explicitly with rmdir; never recurse through them.
    Get-ChildItem -LiteralPath $smokeRoot -Directory |
        Where-Object { $_.LinkType -eq 'Junction' } |
        ForEach-Object {
            & cmd.exe /c rmdir "$($_.FullName)" | Out-Null
        }

    # With junctions removed, deleting the sandbox cannot touch the installed game.
    Remove-Item -LiteralPath $smokeRoot -Recurse -Force -ErrorAction SilentlyContinue

    @(
        "started=$($started.ToString('o'))"
        "duration_seconds=$SmokeSeconds"
        "survived=$survived"
        "render_scale=2"
        "game_root=$GameRoot"
        "smoke_root=$smokeRoot"
    ) | Set-Content -LiteralPath (Join-Path $logOut 'summary.txt') -Encoding ASCII
}

if (-not $survived) {
    exit 1
}

Write-Host 'VHD 2x isolated startup smoke test passed.'
