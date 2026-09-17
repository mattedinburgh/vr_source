param(
    [string]$GameDirectory = "",
    [int]$StartupSeconds = 8
)

$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($GameDirectory)) {
    $GameDirectory = (Resolve-Path (Join-Path $RepoRoot '..')).Path
}

$ExePath = Join-Path $GameDirectory 'JA2_EN_Release.exe'
if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "Release executable not found: $ExePath"
}

if ($StartupSeconds -lt 1) {
    throw 'StartupSeconds must be at least 1.'
}

$CommandLine = '"' + $ExePath + '" /NOSOUND /WINDOW'
$ProcessId = 0

try {
    $CreateResult = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
        CommandLine = $CommandLine
        CurrentDirectory = $GameDirectory
    }

    if ($CreateResult.ReturnValue -ne 0) {
        throw "Win32_Process.Create failed with return value $($CreateResult.ReturnValue)."
    }

    $ProcessId = [int]$CreateResult.ProcessId
    Start-Sleep -Seconds $StartupSeconds

    $Process = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($null -eq $Process) {
        throw "JA2 exited during the first $StartupSeconds seconds of startup."
    }

    $Win32Process = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId"
    if ($null -eq $Win32Process -or $Win32Process.CommandLine -notmatch '/NOSOUND') {
        throw 'JA2 startup smoke did not retain the required /NOSOUND launch mode.'
    }

    Write-Host "PASS: JA2 remained alive for $StartupSeconds seconds after silent windowed startup."
    Write-Host "PASS: runtime command line contains /NOSOUND."
    Write-Host 'ITEM_RUNTIME_STARTUP_OK'
}
finally {
    if ($ProcessId -gt 0 -and (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)) {
        & taskkill.exe /PID $ProcessId /T /F | Out-Null
    }
}