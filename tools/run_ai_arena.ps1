param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$Map,
    [int]$SideA = 8,
    [int]$SideB = 8,
    [int]$Runs = 200,
    [uint32]$BaseSeed = 50000,
    [int]$MaxTeamTurns = 1200,
    [string]$Label = "overnight",
    [int]$BootstrapSlot = -1,
    [switch]$NoAnalyze
)

$ErrorActionPreference = "Stop"
$Map = $Map.ToUpperInvariant()

if ($SideA -lt 1 -or $SideA -gt 20 -or $SideB -lt 1 -or $SideB -gt 20) {
    throw "SideA and SideB must be between 1 and 20."
}
if ($Runs -lt 1) { throw "Runs must be at least 1." }

$labArg = "-ARENA=$Map,$SideA,$SideB,$Runs,$BaseSeed,$MaxTeamTurns,$Label"
if ($BootstrapSlot -ge 0) {
    $labArg += ",$BootstrapSlot"
}

Write-Host "Vengeance AI Battle Arena"
Write-Host "Map: $Map | Teams: $SideA v $SideB | Runs: $Runs | Seeds: $BaseSeed..$($BaseSeed + $Runs - 1)"
Write-Host "Label: $Label"
Write-Host "Launching: $Exe $labArg"

$p = Start-Process -FilePath $Exe -ArgumentList $labArg -WindowStyle Hidden -PassThru -Wait
if ($p.ExitCode -ne 0) {
    throw "Arena exited with code $($p.ExitCode). Check 'AI SelfPlay Batch.txt' and the Black Box logs."
}

Write-Host "Arena batch completed."

if (-not $NoAnalyze) {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        & python "$PSScriptRoot\analyze_selfplay.py"
        Write-Host "Analysis written to 'AI SelfPlay Analysis.txt'."
    } else {
        Write-Warning "Python not found. Arena TSV output is complete; analysis was skipped."
    }
}
