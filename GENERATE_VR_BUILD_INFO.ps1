param(
    [Parameter(Mandatory=$true)][string]$RepoRoot,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [string]$Configuration = 'unknown',
    [string]$Platform = 'unknown',
    [string]$TargetName = 'unknown'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Escape-CppString([string]$Value) {
    if ($null -eq $Value) { return '' }
    $sb = New-Object System.Text.StringBuilder
    foreach ($ch in $Value.ToCharArray()) {
        $code = [int][char]$ch
        if ($ch -eq '\\') { [void]$sb.Append('\\\\') }
        elseif ($ch -eq '"') { [void]$sb.Append('\\"') }
        elseif ($code -lt 32 -or $code -gt 126) { [void]$sb.Append('?') }
        else { [void]$sb.Append($ch) }
    }
    return $sb.ToString()
}

$RepoRoot = [IO.Path]::GetFullPath($RepoRoot)
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null

$git = Get-Command git.exe -ErrorAction SilentlyContinue
if (-not $git) { $git = Get-Command git -ErrorAction SilentlyContinue }

$branch = 'unknown'
$commit = 'unknown'
$shortCommit = 'unknown'
$dirty = 0
$fingerprint = 'unknown'
$recent = @()

if ($git) {
    try {
        $commit = (& $git.Source -C $RepoRoot rev-parse HEAD 2>$null).Trim()
        $shortCommit = (& $git.Source -C $RepoRoot rev-parse --short=12 HEAD 2>$null).Trim()
        if ($env:GITHUB_REF_NAME) {
            $branch = $env:GITHUB_REF_NAME
        } else {
            $branch = (& $git.Source -C $RepoRoot rev-parse --abbrev-ref HEAD 2>$null).Trim()
        }

        $status = @(& $git.Source -C $RepoRoot status --porcelain=v1 2>$null)
        if ($status.Count -gt 0) { $dirty = 1 }

        $diffText = ((& $git.Source -C $RepoRoot diff --binary HEAD -- . 2>$null) | Out-String)

        # git diff does not include untracked files. Hash their contents as well
        # so two different local builds with the same untracked filenames cannot
        # accidentally share a provenance fingerprint.
        $untracked = @(& $git.Source -C $RepoRoot ls-files --others --exclude-standard 2>$null)
        $untrackedEvidence = New-Object System.Collections.Generic.List[string]
        foreach ($relativePath in $untracked) {
            $fullPath = Join-Path $RepoRoot $relativePath
            if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
                $fileHash = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToLowerInvariant()
                $untrackedEvidence.Add($relativePath + ':' + $fileHash)
            } else {
                $untrackedEvidence.Add($relativePath + ':non-file')
            }
        }

        $fingerprintMaterial = (($status -join "`n") + "`n" + $diffText + "`n" + ($untrackedEvidence -join "`n"))
        $sha256 = [Security.Cryptography.SHA256]::Create()
        try {
            $bytes = [Text.Encoding]::UTF8.GetBytes($fingerprintMaterial)
            $fingerprint = ([BitConverter]::ToString($sha256.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
        } finally {
            $sha256.Dispose()
        }

        $recent = @(& $git.Source -C $RepoRoot log -n 8 --pretty=format:'%h %s' 2>$null)
    } catch {
        $branch = 'metadata-error'
        $fingerprint = 'metadata-error'
    }
}

while ($recent.Count -lt 8) { $recent += '' }
if ($recent.Count -gt 8) { $recent = $recent[0..7] }

$generatedAt = (Get-Date).ToUniversalTime().ToString('o')
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('// Auto-generated before compiling JA2. Do not edit or commit this file.')
$lines.Add('#ifndef VR_BUILD_INFO_GENERATED_H')
$lines.Add('#define VR_BUILD_INFO_GENERATED_H')
$lines.Add('#define VR_BUILD_BRANCH "' + (Escape-CppString $branch) + '"')
$lines.Add('#define VR_BUILD_COMMIT "' + (Escape-CppString $commit) + '"')
$lines.Add('#define VR_BUILD_COMMIT_SHORT "' + (Escape-CppString $shortCommit) + '"')
$lines.Add('#define VR_BUILD_DIRTY ' + $dirty)
$lines.Add('#define VR_BUILD_SOURCE_FINGERPRINT "' + (Escape-CppString $fingerprint) + '"')
$lines.Add('#define VR_BUILD_GENERATED_AT "' + (Escape-CppString $generatedAt) + '"')
$lines.Add('#define VR_BUILD_CONFIGURATION "' + (Escape-CppString $Configuration) + '"')
$lines.Add('#define VR_BUILD_PLATFORM "' + (Escape-CppString $Platform) + '"')
$lines.Add('#define VR_BUILD_TARGET "' + (Escape-CppString $TargetName) + '"')
$count = (@($recent | Where-Object { $_ -ne '' })).Count
$lines.Add('#define VR_BUILD_RECENT_CHANGE_COUNT ' + $count)
for ($i = 0; $i -lt 8; $i++) {
    $lines.Add('#define VR_BUILD_RECENT_CHANGE_' + ($i + 1) + ' "' + (Escape-CppString ([string]$recent[$i])) + '"')
}
$lines.Add('#endif')

[IO.File]::WriteAllLines($OutputPath, $lines, (New-Object Text.UTF8Encoding($false)))
Write-Host "Generated build provenance: $OutputPath"
Write-Host "  branch=$branch"
Write-Host "  commit=$shortCommit"
Write-Host "  dirty=$dirty"
Write-Host "  fingerprint=$fingerprint"
