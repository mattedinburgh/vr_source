param(
    [Parameter(Mandatory = $true)]
    [string]$RegistrationToken
)

$ErrorActionPreference = 'Stop'

$RepoUrl = 'https://github.com/mattedinburgh/vr_source'
$RunnerRoot = 'C:\VENGENCE\JA2_RENDER_RUNNER'
$RunnerName = "JA2-Render-$env:COMPUTERNAME"
$Labels = 'ja2-render'

function Test-Administrator {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Administrator)) {
    throw 'Run this installer from an Administrator PowerShell/Command Prompt. The runner is installed as a Windows service.'
}

Write-Host ''
Write-Host 'JA2 autonomous visual-QA runner setup'
Write-Host "Repository : $RepoUrl"
Write-Host "Runner     : $RunnerName"
Write-Host "Location   : $RunnerRoot"
Write-Host ''

if (-not (Test-Path 'C:\VENGENCE\Jagged Alliance 2\JA2_EN_Release.exe')) {
    throw 'Expected JA2 game root was not found at C:\VENGENCE\Jagged Alliance 2.'
}

New-Item -ItemType Directory -Force -Path $RunnerRoot | Out-Null

$existingConfig = Join-Path $RunnerRoot '.runner'
if (-not (Test-Path $existingConfig)) {
    $release = Invoke-RestMethod -Headers @{ 'User-Agent' = 'JA2-AutoRender-Setup' } -Uri 'https://api.github.com/repos/actions/runner/releases/latest'
    $asset = $release.assets | Where-Object { $_.name -match '^actions-runner-win-x64-.*\.zip$' } | Select-Object -First 1

    if (-not $asset) {
        throw 'Could not locate the current Windows x64 GitHub Actions runner package.'
    }

    $zip = Join-Path $env:TEMP $asset.name
    Write-Host "Downloading GitHub Actions runner $($asset.name)..."
    Invoke-WebRequest -UseBasicParsing -Uri $asset.browser_download_url -OutFile $zip

    Write-Host 'Extracting runner...'
    Expand-Archive -Path $zip -DestinationPath $RunnerRoot -Force
    Remove-Item -Force $zip

    Push-Location $RunnerRoot
    try {
        $configArgs = @('--url', $RepoUrl, '--token', $RegistrationToken, '--name', $RunnerName, '--labels', $Labels, '--work', '_work', '--unattended', '--replace')
        & '.\config.cmd' @configArgs
        if ($LASTEXITCODE -ne 0) {
            throw "GitHub runner configuration failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
}
else {
    Write-Host 'Runner is already configured; keeping the existing registration.'
}

Push-Location $RunnerRoot
try {
    if (Test-Path '.\svc.cmd') {
        & '.\svc.cmd' stop 2>$null
        & '.\svc.cmd' uninstall 2>$null
        & '.\svc.cmd' install
        if ($LASTEXITCODE -ne 0) {
            throw "Runner service installation failed with exit code $LASTEXITCODE."
        }
        & '.\svc.cmd' start
        if ($LASTEXITCODE -ne 0) {
            throw "Runner service start failed with exit code $LASTEXITCODE."
        }
    }
    else {
        throw 'svc.cmd is missing from the runner directory.'
    }
}
finally {
    Pop-Location
}

Write-Host ''
Write-Host 'SUCCESS.'
Write-Host 'The PC is now a persistent JA2 render worker.'
Write-Host 'When vr_source changes, GitHub can automatically build MapEditor,'
Write-Host 'render A3 in an isolated game sandbox, publish A3_latest.png,'
Write-Host 'and make it available for autonomous visual inspection.'
Write-Host ''