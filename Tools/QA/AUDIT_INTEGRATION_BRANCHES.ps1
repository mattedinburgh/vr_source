param(
    [string]$CanonicalRef = "origin/install/all-2026-09-12",
    [string[]]$ExcludePrefixes = @("archive/", "master", "playtest/"),
    [switch]$RemoteRefs
)

$ErrorActionPreference = "Stop"

function Resolve-GitExecutable {
    $command = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $fixed = @(
        (Join-Path $env:ProgramFiles "Git\cmd\git.exe"),
        (Join-Path ([Environment]::GetFolderPath("ProgramFilesX86")) "Git\cmd\git.exe")
    )
    foreach ($path in $fixed) {
        if ($path -and (Test-Path $path)) { return $path }
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

    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $script:GitExe -C $WorkingDirectory @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }

    return [pscustomobject]@{
        Output = @($output)
        ExitCode = $exitCode
    }
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$canonicalResult = Invoke-Git @("rev-parse", "--verify", $CanonicalRef) $repo
if ($canonicalResult.ExitCode -ne 0) {
    throw "Canonical ref not found: $CanonicalRef"
}
$canonicalSha = ($canonicalResult.Output | Select-Object -First 1).Trim()

Write-Host "Canonical: $CanonicalRef @ $canonicalSha"
Write-Host "Read-only audit: no fetch, merge, checkout, reset, build, or push is performed."

$refNamespace = "refs/heads"
if ($RemoteRefs) {
    $refNamespace = "refs/remotes/origin"
}

$listResult = Invoke-Git @("for-each-ref", "--format=%(refname:short)", $refNamespace) $repo
if ($listResult.ExitCode -ne 0) {
    throw "Unable to list branches in $refNamespace."
}

$refs = @($listResult.Output | Where-Object { $_ })
$rows = @()

foreach ($ref in $refs) {
    if ($ref -match "/HEAD$") { continue }

    $branch = $ref -replace "^origin/", ""
    if ($branch -eq "install/all-2026-09-12") { continue }

    $excluded = $false
    foreach ($prefix in $ExcludePrefixes) {
        if ($branch -eq $prefix -or $branch.StartsWith($prefix)) {
            $excluded = $true
            break
        }
    }
    if ($excluded) { continue }

    $shaResult = Invoke-Git @("rev-parse", "--verify", $ref) $repo
    if ($shaResult.ExitCode -ne 0) { continue }
    $sha = ($shaResult.Output | Select-Object -First 1).Trim()

    $countsResult = Invoke-Git @("rev-list", "--left-right", "--count", "$CanonicalRef...$ref") $repo
    if ($countsResult.ExitCode -ne 0) { continue }

    $parts = (($countsResult.Output | Select-Object -First 1) -split "\s+")
    if ($parts.Count -lt 2) { continue }
    $behind = [int]$parts[0]
    $ahead = [int]$parts[1]

    if ($sha -eq $canonicalSha) {
        $relation = "ALIGNED"
    }
    elseif ($behind -eq 0 -and $ahead -gt 0) {
        $relation = "AHEAD"
    }
    elseif ($behind -gt 0 -and $ahead -eq 0) {
        $relation = "CONTAINED"
    }
    else {
        $relation = "DIVERGED"
    }

    $diffResult = Invoke-Git @("diff", "--name-only", "$CanonicalRef...$ref") $repo
    $paths = @()
    if ($diffResult.ExitCode -eq 0) {
        $paths = @($diffResult.Output | Where-Object { $_ })
    }

    $hot = @($paths | Where-Object {
        $_ -match "^(TacticalAI|ModularizedTacticalAI|Tactical|TileEngine|Strategic|Laptop|Utils)/"
    }).Count

    $strategic = @($paths | Where-Object { $_ -match "^Strategic/" }).Count

    $noise = @($paths | Where-Object {
        $_ -match "(^|/)(bin|build|Debug|Release|ipch|\.vs)(/|$)" -or
        $_ -match "(\.vcxproj\.user|\.suo|\.obj|\.pdb|\.ilk|\.tlog|\.log)$"
    }).Count

    switch ($relation) {
        "ALIGNED"   { $recommendation = "No delta" }
        "CONTAINED" { $recommendation = "Already represented by canonical history" }
        "DIVERGED"  { $recommendation = "Forward-port onto canonical before gate" }
        "AHEAD"     { $recommendation = "Run candidate gate" }
        default     { $recommendation = "Review" }
    }

    if ($strategic -gt 0) {
        $recommendation += "; strategic change requires explicit approval"
    }
    if ($noise -gt 0) {
        $recommendation += "; remove generated/IDE noise"
    }

    $rows += [pscustomobject]@{
        Branch = $branch
        Relation = $relation
        Ahead = $ahead
        Behind = $behind
        Files = $paths.Count
        Hot = $hot
        Strategic = $strategic
        Noise = $noise
        Recommendation = $recommendation
    }
}

$rows = @($rows | Sort-Object Relation, Branch)
$rows | Format-Table -AutoSize

Write-Host ""
Write-Host "Summary:"
foreach ($status in @("AHEAD", "DIVERGED", "ALIGNED", "CONTAINED")) {
    $count = @($rows | Where-Object { $_.Relation -eq $status }).Count
    Write-Host ("  {0,-10} {1}" -f $status, $count)
}

$blocked = @($rows | Where-Object {
    $_.Relation -eq "DIVERGED" -or $_.Strategic -gt 0 -or $_.Noise -gt 0
}).Count
Write-Host "  BLOCKED/RISK $blocked"

exit 0
