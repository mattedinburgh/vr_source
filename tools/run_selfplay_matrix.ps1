param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][int[]]$Slots,
    [int]$Runs = 100,
    [uint32]$BaseSeed = 50000,
    [int]$MaxTeamTurns = 1200,
    [string]$Label = "current",
    [switch]$Analyze
)

$ErrorActionPreference = "Stop"

foreach ($slot in $Slots) {
    $labArg = "-SELFPLAY=$slot,$Runs,$BaseSeed,$MaxTeamTurns,$Label"
    Write-Host "Self-play fixture $slot :: $labArg"
    $p = Start-Process -FilePath $Exe -ArgumentList $labArg -WindowStyle Hidden -PassThru -Wait
    if ($p.ExitCode -ne 0) {
        throw "Self-play fixture $slot exited with code $($p.ExitCode)"
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
