param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [int[]]$Slots = @(),
    [string[]]$Maps = @(),
    [int]$Runs = 100,
    [uint32]$BaseSeed = 50000,
    [int]$MaxTeamTurns = 1200,
    [string]$Label = "current",
    [switch]$Analyze
)

$ErrorActionPreference = "Stop"

$fixtures = @()
foreach ($slot in $Slots) { $fixtures += [string]$slot }
foreach ($map in $Maps) { if ($map) { $fixtures += $map.ToUpperInvariant() } }

if ($fixtures.Count -eq 0) {
    throw "Provide at least one -Maps value (for example A9,B13) or -Slots value."
}

foreach ($fixture in $fixtures) {
    $labArg = "-SELFPLAY=$fixture,$Runs,$BaseSeed,$MaxTeamTurns,$Label"
    Write-Host "Self-play fixture $fixture :: $labArg"
    $p = Start-Process -FilePath $Exe -ArgumentList $labArg -WindowStyle Hidden -PassThru -Wait
    if ($p.ExitCode -ne 0) {
        throw "Self-play fixture $fixture exited with code $($p.ExitCode)"
    }
}

if ($Analyze) {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        & python "$PSScriptRoot\analyze_selfplay.py"
    } else {
        Write-Warning "Python not found; TSV output was still produced."
    }
}
