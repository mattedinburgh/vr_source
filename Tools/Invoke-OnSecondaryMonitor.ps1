param(
    [Parameter(Mandatory=$true)]
    [string]$FilePath,

    [string[]]$ArgumentList,

    [string]$WorkingDirectory,

    [switch]$AllowPrimaryFallback
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'VRMapDisplayRouting.ps1')

$requireSecondary = -not $AllowPrimaryFallback
$rc = Invoke-VRProcessOnSecondary -FilePath $FilePath -ArgumentList $ArgumentList -WorkingDirectory $WorkingDirectory -RequireSecondary:$requireSecondary
exit $rc
