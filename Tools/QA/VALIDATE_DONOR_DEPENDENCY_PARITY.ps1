param(
    [Parameter(Mandatory = $true)]
    [string]$ReportPath,
    [string[]]$Sector = @(),
    [switch]$RequireParity
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ReportPath)) {
    throw "Donor dependency parity report not found: $ReportPath"
}

try {
    $report = Get-Content -Raw -Path $ReportPath | ConvertFrom-Json
}
catch {
    throw "Invalid donor dependency parity JSON: $($_.Exception.Message)"
}

$errors = New-Object System.Collections.Generic.List[string]

function Add-Error {
    param([string]$Message)
    $errors.Add($Message)
}

function Require-Integer {
    param([object]$Object, [string]$PropertyName)
    $property = $Object.PSObject.Properties[$PropertyName]
    if (-not $property) {
        Add-Error "Missing required integer field: $PropertyName"
        return 0
    }
    $value = 0
    if (-not [int]::TryParse([string]$property.Value, [ref]$value) -or $value -lt 0) {
        Add-Error "Invalid non-negative integer field '$PropertyName': $($property.Value)"
        return 0
    }
    return $value
}

function Require-Text {
    param([object]$Object, [string]$PropertyName, [string]$Context)
    $property = $Object.PSObject.Properties[$PropertyName]
    if (-not $property -or [string]::IsNullOrWhiteSpace([string]$property.Value)) {
        Add-Error "$Context missing required text field: $PropertyName"
        return ""
    }
    return ([string]$property.Value).Trim()
}

$selectedDeclared = Require-Integer $report "selected"
$parityDeclared = Require-Integer $report "parity"
$rebaseDeclared = Require-Integer $report "rebase_required"
$failedDeclared = Require-Integer $report "failed"

$resultsProperty = $report.PSObject.Properties["results"]
$results = if ($resultsProperty) { @($resultsProperty.Value) } else { @() }
if (-not $resultsProperty) {
    Add-Error "Missing results array."
}
if ($results.Count -eq 0) {
    Add-Error "Parity report contains no sector results."
}

$seen = @{}
$parityCount = 0
$rebaseCount = 0
$failedCount = 0
$rows = @()
foreach ($result in $results) {
    $sectorName = Require-Text $result "sector" "Result"
    $status = Require-Text $result "status" "Result '$sectorName'"
    [void](Require-Text $result "donor_map" "Result '$sectorName'")
    [void](Require-Text $result "baseline_snapshot" "Result '$sectorName'")
    [void](Require-Text $result "donor_snapshot" "Result '$sectorName'")

    if ($sectorName) {
        $sectorKey = $sectorName.ToUpperInvariant()
        if ($seen.ContainsKey($sectorKey)) {
            Add-Error "Duplicate sector result: $sectorName"
        }
        else {
            $seen[$sectorKey] = $result
        }
    }

    $componentsProperty = $result.PSObject.Properties["differing_components"]
    $components = if ($componentsProperty) { @($componentsProperty.Value) } else { @() }
    $components = @($components | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) })

    $errorProperty = $result.PSObject.Properties["error"]
    $errorText = if ($errorProperty -and $null -ne $errorProperty.Value) { ([string]$errorProperty.Value).Trim() } else { "" }

    switch ($status) {
        "PARITY" {
            $parityCount++
            if ($components.Count -ne 0) {
                Add-Error "Sector '$sectorName' is PARITY but lists differing_components."
            }
            if ($errorText) {
                Add-Error "Sector '$sectorName' is PARITY but contains an error: $errorText"
            }
        }
        "REBASE_REQUIRED" {
            $rebaseCount++
            if ($components.Count -eq 0) {
                Add-Error "Sector '$sectorName' is REBASE_REQUIRED without differing_components."
            }
            if ($errorText) {
                Add-Error "Sector '$sectorName' is REBASE_REQUIRED but contains an error: $errorText"
            }
        }
        default {
            $failedCount++
            Add-Error "Sector '$sectorName' has unsupported status '$status'."
        }
    }

    $rows += [pscustomobject]@{
        Sector = $sectorName
        Status = $status
        DifferingComponents = ($components -join ",")
    }
}

if ($selectedDeclared -ne $results.Count) {
    Add-Error "selected=$selectedDeclared does not match results count $($results.Count)."
}
if ($parityDeclared -ne $parityCount) {
    Add-Error "parity=$parityDeclared does not match computed parity count $parityCount."
}
if ($rebaseDeclared -ne $rebaseCount) {
    Add-Error "rebase_required=$rebaseDeclared does not match computed rebase count $rebaseCount."
}
if ($failedDeclared -ne $failedCount) {
    Add-Error "failed=$failedDeclared does not match computed failed count $failedCount."
}
if ($failedDeclared -gt 0) {
    Add-Error "Parity report declares $failedDeclared failed sector(s)."
}

$requested = @($Sector | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | ForEach-Object { $_.Trim().ToUpperInvariant() } | Select-Object -Unique)
foreach ($requestedSector in $requested) {
    if (-not $seen.ContainsKey($requestedSector)) {
        Add-Error "Requested sector '$requestedSector' is absent from the parity report."
        continue
    }
    if ($RequireParity -and ([string]$seen[$requestedSector].status -ne "PARITY")) {
        $diffs = @($seen[$requestedSector].differing_components) -join ","
        Add-Error "Requested sector '$requestedSector' is not dependency-parity safe; status=$($seen[$requestedSector].status), differing_components=$diffs"
    }
}

if ($RequireParity -and $requested.Count -eq 0) {
    foreach ($row in $rows) {
        if ($row.Status -ne "PARITY") {
            Add-Error "Sector '$($row.Sector)' is not dependency-parity safe; status=$($row.Status), differing_components=$($row.DifferingComponents)"
        }
    }
}

Write-Host "Donor dependency parity QA"
Write-Host "Report:          $ReportPath"
Write-Host "Sectors:         $($results.Count)"
Write-Host "Parity:          $parityCount"
Write-Host "Rebase required: $rebaseCount"
Write-Host "Failed:          $failedCount"
foreach ($row in $rows) {
    if ($row.Status -eq "PARITY") {
        Write-Host "  PARITY_READY $($row.Sector)"
    }
    elseif ($row.Status -eq "REBASE_REQUIRED") {
        Write-Host "  REBASE_REQUIRED $($row.Sector) [$($row.DifferingComponents)]"
    }
}

if ($errors.Count -gt 0) {
    Write-Host ""
    Write-Host "DONOR_DEPENDENCY_PARITY_QA_FAILED"
    $errors | ForEach-Object { Write-Host "  ERROR: $_" }
    exit 52
}

Write-Host "DONOR_DEPENDENCY_PARITY_QA_OK"
exit 0
