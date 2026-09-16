param(
    [string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}

$fileMan = Join-Path $RepoRoot "Standard Gaming Platform\FileMan.cpp"
if (-not (Test-Path $fileMan -PathType Leaf)) {
    throw "FileMan.cpp not found at '$fileMan'"
}

$text = [IO.File]::ReadAllText($fileMan)
$failures = New-Object System.Collections.Generic.List[string]

function Require-Pattern {
    param([string]$Pattern, [string]$Description)
    if (-not [regex]::IsMatch($text, $Pattern, [Text.RegularExpressions.RegexOptions]::Singleline)) {
        [void]$failures.Add($Description)
    }
}

Require-Pattern 'const bool fTruncate\s*=\s*fCreateAlways\s*\|\|\s*fTruncateExisting\s*;' 'FILE_CREATE_ALWAYS / FILE_TRUNCATE_EXISTING must request VFS truncation.'
Require-Pattern 'fCreateNew\s*&&\s*getVFS\(\)->fileExists\(path\)' 'FILE_CREATE_NEW must reject an already-existing VFS file.'
Require-Pattern 'const bool fCreateWhenMissing\s*=.*fCreateNew.*fCreateAlways.*fOpenAlways' 'VFS write-open creation flags are not mapped explicitly.'
Require-Pattern 'if\(puiBytesWritten\).*\*puiBytesWritten\s*=\s*0\s*;' 'Zero-byte FileWrite must tolerate a null bytes-written pointer.'

$indexMatches = [regex]::Matches($text, 's_mapFiles\s*\[\s*pFile\s*\]')
if ($indexMatches.Count -ne 2) {
    [void]$failures.Add("Expected exactly 2 s_mapFiles[pFile] registration writes; found $($indexMatches.Count). Lookup paths must use find().")
}

$findMatches = [regex]::Matches($text, 's_mapFiles\.find\(pFile\)')
if ($findMatches.Count -lt 5) {
    [void]$failures.Add("Expected non-mutating lookup in FileRead/FileWrite/FileSeek/FileGetPos/FileCheckEndOfFile.")
}
Require-Pattern 'uiOptions\s*&\s*FILE_ACCESS_READWRITE.*FILE_ACCESS_READWRITE' 'VFS bridge must explicitly reject unsafe FILE_ACCESS_READWRITE handles.'

$readWriteCallers = Get-ChildItem $RepoRoot -Recurse -File -Include *.cpp,*.h | Where-Object { $_.FullName -notmatch '\\.git\\|\\ext\\' } | Select-String -Pattern 'FileOpen\s*\([^\r\n]*FILE_ACCESS_READWRITE'
if ($readWriteCallers) {
    [void]$failures.Add("Live FILE_ACCESS_READWRITE caller(s) remain; split them into explicit read/write ownership phases.")
}

if ($failures.Count -gt 0) {
    Write-Host "FileMan VFS semantics: FAILED" -ForegroundColor Red
    $failures | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host "FileMan VFS semantics: PASS" -ForegroundColor Green
Write-Host "  legacy create/truncate mapping present"
Write-Host "  invalid-handle lookups are non-mutating"
Write-Host "  zero-byte writes are null-safe"
exit 0