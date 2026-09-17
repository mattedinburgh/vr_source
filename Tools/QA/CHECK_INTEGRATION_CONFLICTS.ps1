param(
    [Parameter(Mandatory = $true)]
    [string[]]$CandidateRefs,
    [string]$CanonicalRef = "origin/install/all-2026-09-12",
    [string]$ExpectedCanonicalSha = "",
    [switch]$FailOnSharedFiles,
    [string]$JsonOutput = ""
)

$ErrorActionPreference = "Stop"

function Resolve-GitExecutable {
    $command = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $fixed = @(
        (Join-Path $env:ProgramFiles "Git\cmd\git.exe"),
        (Join-Path ([Environment]::GetFolderPath("ProgramFilesX86")) "Git\cmd\git.exe")
    )
    foreach ($candidate in $fixed) {
        if ($candidate -and (Test-Path $candidate)) { return $candidate }
    }

    $desktopPattern = Join-Path $env:LOCALAPPDATA "GitHubDesktop\app-*\resources\app\git\cmd\git.exe"
    $desktopGit = Get-Item $desktopPattern -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($desktopGit) { return $desktopGit.FullName }

    throw "Git executable not found. Install Git or GitHub Desktop."
}

$script:GitExe = Resolve-GitExecutable

function Invoke-Git {
    param([string[]]$Arguments, [string]$WorkingDirectory)
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $script:GitExe -C $WorkingDirectory @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $saved
    }
    return [pscustomobject]@{ Output = @($output); ExitCode = $exitCode }
}

function Get-IntegrationArea {
    param([string]$Path)

    if ($Path -match '^(TacticalAI|ModularizedTacticalAI)/') { return "Tactical AI" }
    if ($Path -match '^Tactical/') { return "Tactical / combat" }
    if ($Path -match '^Strategic/') { return "Strategic / campaign" }
    if ($Path -match '^(TileEngine|SGP|Standard Gaming Platform)/') { return "Rendering / tile engine" }
    if ($Path -match '^(Laptop|Interface)/') { return "UI / laptop" }
    if ($Path -match '^Utils/') { return "Shared utilities" }
    if ($Path -match '^(Tools/QA|\.github/workflows)/') { return "Integration / QA tooling" }
    if ($Path -match '^Tools/AI/') { return "AI tooling" }
    if ($Path -match '(?i)(^|/)(maps?|world)(/|$)|\.dat$') { return "Maps / world data" }
    if ($Path -match '(?i)(TableData/Items|Weapons\.xml|Ammo|Attachments|Explosives)') { return "Items / weapons / explosives" }
    if ($Path -match '(?i)(sounds?|audio|\.wav$|\.ogg$|\.mp3$)') { return "Audio / localization" }
    if ($Path -match '(?i)(MercProfiles|Profiles|NPCData|GameInit\.lua|AltSectors)') { return "RPG / scripted world data" }

    $first = ($Path -split '/')[0]
    if ($first) { return "Other: $first" }
    return "Other"
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$canonical = Invoke-Git @("rev-parse", "--verify", $CanonicalRef) $repo
if ($canonical.ExitCode -ne 0) { throw "Canonical ref not found: $CanonicalRef" }
$canonicalSha = ($canonical.Output | Select-Object -First 1).Trim()

if ($ExpectedCanonicalSha -and $canonicalSha -ne $ExpectedCanonicalSha) {
    throw "Canonical moved. Expected $ExpectedCanonicalSha but found $canonicalSha."
}

$pathOwners = @{}
$areaOwners = @{}
$candidateRows = @()

foreach ($candidateRef in $CandidateRefs) {
    $candidate = Invoke-Git @("rev-parse", "--verify", $candidateRef) $repo
    if ($candidate.ExitCode -ne 0) { throw "Candidate ref not found: $candidateRef" }
    $candidateSha = ($candidate.Output | Select-Object -First 1).Trim()

    $diff = Invoke-Git @("diff", "--name-only", "$canonicalSha..$candidateSha") $repo
    if ($diff.ExitCode -ne 0) { throw "Unable to diff $candidateRef against canonical." }
    $paths = @($diff.Output | Where-Object { $_ } | Sort-Object -Unique)

    $areas = @()
    foreach ($relativePath in $paths) {
        if (-not $pathOwners.ContainsKey($relativePath)) {
            $pathOwners[$relativePath] = New-Object System.Collections.Generic.List[string]
        }
        if (-not $pathOwners[$relativePath].Contains($candidateRef)) {
            $pathOwners[$relativePath].Add($candidateRef)
        }

        $area = Get-IntegrationArea $relativePath
        $areas += $area
        if (-not $areaOwners.ContainsKey($area)) {
            $areaOwners[$area] = New-Object System.Collections.Generic.List[string]
        }
        if (-not $areaOwners[$area].Contains($candidateRef)) {
            $areaOwners[$area].Add($candidateRef)
        }
    }

    $candidateRows += [pscustomobject]@{
        Candidate = $candidateRef
        Sha = $candidateSha
        Files = $paths.Count
        Areas = (@($areas | Sort-Object -Unique) -join "; ")
    }
}

Write-Host "Canonical: $CanonicalRef @ $canonicalSha"
Write-Host "Candidate deltas:"
$candidateRows | Format-Table -AutoSize

$sharedFiles = @()
foreach ($entry in $pathOwners.GetEnumerator() | Sort-Object Key) {
    $owners = @($entry.Value | Sort-Object -Unique)
    if ($owners.Count -gt 1) {
        $sharedFiles += [pscustomobject]@{
            Path = $entry.Key
            Candidates = $owners
        }
    }
}

$sharedAreas = @()
foreach ($entry in $areaOwners.GetEnumerator() | Sort-Object Key) {
    $owners = @($entry.Value | Sort-Object -Unique)
    if ($owners.Count -gt 1) {
        $sharedAreas += [pscustomobject]@{
            Area = $entry.Key
            Candidates = $owners
        }
    }
}

if ($sharedFiles.Count -gt 0) {
    Write-Host ""
    Write-Host "CROSS_STREAM_SHARED_FILE_OVERLAP"
    foreach ($item in $sharedFiles) {
        Write-Host ("  {0} <- {1}" -f $item.Path, ($item.Candidates -join ", "))
    }
}
else {
    Write-Host "NO_CROSS_STREAM_SHARED_FILES"
}

if ($sharedAreas.Count -gt 0) {
    Write-Host ""
    Write-Host "CROSS_STREAM_SHARED_AREA_REVIEW"
    foreach ($item in $sharedAreas) {
        Write-Host ("  {0} <- {1}" -f $item.Area, ($item.Candidates -join ", "))
    }
}

if ($JsonOutput) {
    $report = [pscustomobject]@{
        GeneratedUtc = (Get-Date).ToUniversalTime().ToString("o")
        CanonicalRef = $CanonicalRef
        CanonicalSha = $canonicalSha
        Candidates = $candidateRows
        SharedFiles = $sharedFiles
        SharedAreas = $sharedAreas
    }
    $jsonPath = [System.IO.Path]::GetFullPath((Join-Path $repo $JsonOutput))
    $jsonDir = Split-Path -Parent $jsonPath
    if ($jsonDir -and -not (Test-Path $jsonDir)) {
        New-Item -ItemType Directory -Force -Path $jsonDir | Out-Null
    }
    $report | ConvertTo-Json -Depth 8 | Set-Content -Path $jsonPath -Encoding UTF8
    Write-Host "CONFLICT_REPORT_JSON=$jsonPath"
}

if ($FailOnSharedFiles -and $sharedFiles.Count -gt 0) {
    Write-Host "CROSS_STREAM_FILE_OVERLAP_BLOCKED"
    exit 31
}

Write-Host "CROSS_STREAM_CONFLICT_SCAN_OK"
exit 0
