param(
    [string]$GameRoot,
    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = $PSScriptRoot
if (-not $GameRoot) {
    $candidate = Split-Path -Parent $RepoRoot
    if (Test-Path (Join-Path $candidate 'vfs_config.Vengeance.ini')) {
        $GameRoot = $candidate
    } else {
        $GameRoot = (Get-Location).Path
    }
}
$GameRoot = [IO.Path]::GetFullPath($GameRoot)

if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $GameRoot 'BlackBoxBundles'
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$work = Join-Path $env:TEMP ("VR_BlackBox_" + $stamp + "_" + $PID)
$zip = Join-Path $OutputDirectory ("Vengeance_BlackBox_" + $stamp + ".zip")
New-Item -ItemType Directory -Force -Path $work | Out-Null
$collectionErrors = New-Object System.Collections.Generic.List[string]
$zipPartial = $null

function Copy-EvidenceFile {
    param([string]$Path, [string]$Subdir = '')
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    $destDir = if ($Subdir) { Join-Path $work $Subdir } else { $work }
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    try {
        Copy-Item -LiteralPath $Path -Destination (Join-Path $destDir ([IO.Path]::GetFileName($Path))) -Force -ErrorAction Stop
    } catch {
        $collectionErrors.Add(("COPY FAILED: {0} :: {1}" -f $Path, $_.Exception.Message))
    }
}

function Copy-NewestMatches {
    param([string]$Pattern, [int]$Count = 5, [string]$Subdir = '')
    Get-ChildItem -Path $GameRoot -Filter $Pattern -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First $Count |
        ForEach-Object { Copy-EvidenceFile -Path $_.FullName -Subdir $Subdir }
}

try {
# Core recorder evidence. Preserve current and rotated runs.
@(
    'VR_BlackBox.jsonl',
    'VR_Companion_Report.md',
    'VR_Companion_Summary.json',
    'VR_Analytics_Experiment.txt',
    'BlackBox_LastRun.log',
    'BlackBox_PreviousRun.log',
    'BlackBox_PreviousRun_2.log',
    'BlackBox_PreviousRun_3.log',
    'BlackBox_PreviousRun_4.log',
    'BlackBox_Hang_LastRun.log',
    'BlackBox_Hang_PreviousRun.log',
    'BlackBox_Hang_PreviousRun_2.log',
    'BlackBox_Hang_PreviousRun_3.log',
    'BlackBox_Hang_PreviousRun_4.log',
    'BlackBox_Hang_LastRun.dmp',
    'BlackBox_Hang_PreviousRun.dmp',
    'BlackBox_Hang_PreviousRun_2.dmp',
    'BlackBox_Hang_PreviousRun_3.dmp',
    'BlackBox_Hang_PreviousRun_4.dmp',
    'LiveLog.txt',
    'debug.txt',
    'Log.txt',
    'AiDebug.txt',
    'AnimDebug.txt',
    'PhysicsDebug.txt'
) | ForEach-Object { Copy-EvidenceFile (Join-Path $GameRoot $_) }

# Timestamped fatal-crash evidence.
Copy-NewestMatches -Pattern 'Crash Report_*.txt' -Count 8 -Subdir 'crashes'
Copy-NewestMatches -Pattern 'Vengeance-Crash-*.dmp' -Count 8 -Subdir 'crashes'
Copy-NewestMatches -Pattern '*-Crash-*.dmp' -Count 8 -Subdir 'crashes'

# Small configuration files that materially affect reproducibility.
$configCandidates = @(
    'vfs_config.Vengeance.ini',
    'Ja2.ini',
    'Ja2_Settings.INI',
    'Data-Vengeance\Ja2_Options.INI',
    'Data-Vengeance\CTHConstants.ini',
    'Data-Vengeance\Skills_Settings.INI',
    'Data-Vengeance\Mod_Settings.ini'
)
foreach ($relative in $configCandidates) {
    $path = Join-Path $GameRoot $relative
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $safeName = ($relative -replace '[\\/:*?"<>|]', '_')
        try {
            Copy-Item -LiteralPath $path -Destination (Join-Path $work $safeName) -Force -ErrorAction Stop
        } catch {
            $collectionErrors.Add(("CONFIG COPY FAILED: {0} :: {1}" -f $path, $_.Exception.Message))
        }
    }
}

# Machine/build identity. This is diagnostic metadata only; no usernames,
# environment-variable dump, saves, screenshots, or arbitrary user files.
$envFile = Join-Path $work 'environment.txt'
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("Captured: " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff zzz'))
$lines.Add("GameRoot: " + $GameRoot)
$lines.Add("RepoRoot: " + $RepoRoot)
$lines.Add("OS: " + [Environment]::OSVersion.VersionString)
$lines.Add("PowerShell: " + $PSVersionTable.PSVersion.ToString())
$lines.Add("ProcessArchitecture: " + $(if ([Environment]::Is64BitOperatingSystem) { '64-bit OS' } else { '32-bit OS' }))

$exeCandidates = @(
    (Join-Path $GameRoot 'JA2_EN_Release.exe'),
    (Join-Path $GameRoot 'JA2_EN_Release_WithDebugInfo.exe'),
    (Join-Path $RepoRoot 'bin\VS2013\JA2_EN_Release.exe')
)
$exe = $exeCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if ($exe) {
    $fi = Get-Item -LiteralPath $exe
    $hash = Get-FileHash -LiteralPath $exe -Algorithm SHA256
    $lines.Add("Executable: " + $fi.FullName)
    $lines.Add("ExecutableSize: " + $fi.Length)
    $lines.Add("ExecutableModified: " + $fi.LastWriteTime.ToString('o'))
    $lines.Add("ExecutableSHA256: " + $hash.Hash)
}

$git = Get-Command git -ErrorAction SilentlyContinue
if ($git -and (Test-Path (Join-Path $RepoRoot '.git'))) {
    try {
        $branchName = (& git -C $RepoRoot rev-parse --abbrev-ref HEAD 2>$null).Trim()
        $commit = (& git -C $RepoRoot rev-parse HEAD 2>$null).Trim()
        $status = (& git -C $RepoRoot status --short 2>$null)
        $lines.Add("GitBranch: " + $branchName)
        $lines.Add("GitCommit: " + $commit)
        $lines.Add("GitDirty: " + $(if ($status) { 'yes' } else { 'no' }))
        if ($status) {
            $lines.Add('GitStatus:')
            foreach ($s in $status) { $lines.Add('  ' + $s) }
        }
    } catch {
        $lines.Add("GitMetadataError: " + $_.Exception.Message)
    }
}
$lines | Set-Content -LiteralPath $envFile -Encoding UTF8

if ($collectionErrors.Count -gt 0) {
    $collectionErrors | Set-Content -LiteralPath (Join-Path $work 'collection_errors.txt') -Encoding UTF8
}

# File manifest with hashes makes truncated/corrupt bundles obvious.
$manifest = Join-Path $work 'manifest.csv'
$manifestRows = Get-ChildItem -Path $work -Recurse -File |
    Where-Object { $_.FullName -ne $manifest } |
    ForEach-Object {
        $h = Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256
        [pscustomobject]@{
            RelativePath = $_.FullName.Substring($work.Length + 1)
            Bytes = $_.Length
            Modified = $_.LastWriteTime.ToString('o')
            SHA256 = $h.Hash
        }
    }
$manifestRows | Export-Csv -LiteralPath $manifest -NoTypeInformation -Encoding UTF8

$zipPartial = Join-Path $OutputDirectory ("Vengeance_BlackBox_" + $stamp + ".partial.zip")
if (Test-Path -LiteralPath $zipPartial) { Remove-Item -LiteralPath $zipPartial -Force }
Compress-Archive -Path (Join-Path $work '*') -DestinationPath $zipPartial -CompressionLevel Optimal

if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Move-Item -LiteralPath $zipPartial -Destination $zip -Force

Write-Host ''
Write-Host 'Vengeance black-box evidence bundle created:'
Write-Host "  $zip"
if ($collectionErrors.Count -gt 0) {
    Write-Warning ("Bundle completed with {0} skipped/copy-failed file(s). See collection_errors.txt inside the ZIP." -f $collectionErrors.Count)
}
Write-Host ''
Write-Host 'Upload this ZIP when reporting a crash, freeze, save/load failure, or sector-transition problem.'
}
finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    if ($zipPartial -and (Test-Path -LiteralPath $zipPartial)) {
        Remove-Item -LiteralPath $zipPartial -Force -ErrorAction SilentlyContinue
    }
}
