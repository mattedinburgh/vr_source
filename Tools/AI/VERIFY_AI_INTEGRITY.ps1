param(
    [string]$RepositoryRoot = "."
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path $RepositoryRoot).Path
$tacticalAI = Join-Path $root "TacticalAI"
$aiHeader = Join-Path $tacticalAI "ai.h"
$decideAction = Join-Path $tacticalAI "DecideAction.cpp"
$frameworkDoc = Join-Path $root "UNIFIED_AI_FRAMEWORK.md"
$cqbSource = Join-Path $tacticalAI "CQBBuildingDoctrine.cpp"
$cqbHeader = Join-Path $tacticalAI "CQBBuildingDoctrine.h"

function Fail([string]$Message) {
    Write-Error $Message
    $script:Failed = $true
}

function Read-Text([string]$Path) {
    if (-not (Test-Path $Path)) {
        Fail "Missing required file: $Path"
        return ""
    }
    return [System.IO.File]::ReadAllText($Path)
}

function Count-Definitions([string]$Name, [string[]]$Files) {
    $escaped = [regex]::Escape($Name)
    $pattern = "(?m)^\s*(?:BOOLEAN|UINT8|INT8|UINT16|INT16|UINT32|INT32|FLOAT|DOUBLE|double|void|int|CHAR8)\s+" + $escaped + "\s*\("
    $hits = @()

    foreach ($file in $Files) {
        $text = Read-Text $file
        $count = [regex]::Matches($text, $pattern).Count
        for ($i = 0; $i -lt $count; ++$i) {
            $hits += $file
        }
    }

    return ,$hits
}

function Count-HeaderDeclarations([string]$Name, [string]$Text) {
    $escaped = [regex]::Escape($Name)
    $pattern = "(?m)^\s*(?:extern\s+)?(?:BOOLEAN|UINT8|INT8|UINT16|INT16|UINT32|INT32|FLOAT|DOUBLE|double|void|int|CHAR8)\s+" + $escaped + "\s*\("
    return [regex]::Matches($Text, $pattern).Count
}

$script:Failed = $false

if (-not (Test-Path $tacticalAI)) {
    throw "TacticalAI directory not found under $root"
}

$cppFiles = @(Get-ChildItem $tacticalAI -Filter "*.cpp" -File | Select-Object -ExpandProperty FullName)
$headerText = Read-Text $aiHeader
$decideText = Read-Text $decideAction
$frameworkText = Read-Text $frameworkDoc
$cqbText = Read-Text $cqbSource
$cqbHeaderText = Read-Text $cqbHeader

Write-Host "Unified AI integrity audit"
Write-Host "Repository: $root"
Write-Host ""

# 1. No unresolved merge-conflict markers in the active AI/analytics surface.
$conflictFiles = @(
    Get-ChildItem $tacticalAI -Include "*.cpp","*.h" -File
    Get-Item (Join-Path $root "VRAnalytics.cpp") -ErrorAction SilentlyContinue
    Get-Item (Join-Path $root "VRAnalytics.h") -ErrorAction SilentlyContinue
) | Where-Object { $_ }

foreach ($file in $conflictFiles) {
    $text = Read-Text $file.FullName
    if ($text -match "(?m)^(<<<<<<< .+|=======|>>>>>>> .+)\s*$") {
        Fail "Unresolved merge-conflict marker found in $($file.FullName)"
    }
}

# 2. Alternate/obsolete AI diagnostic implementation must not return.
$forbiddenFiles = @(
    (Join-Path $tacticalAI "AI Diagnostics.cpp"),
    (Join-Path $tacticalAI "AI Diagnostics.h")
)
foreach ($file in $forbiddenFiles) {
    if (Test-Path $file) {
        Fail "Obsolete parallel AI diagnostics file exists: $file. Use VRAnalytics instead."
    }
}

$forbiddenSymbols = @(
    "AITraceBeginDecision",
    "AITraceCandidate",
    "AITraceReject",
    "AITraceSelect"
)
foreach ($symbol in $forbiddenSymbols) {
    foreach ($file in $cppFiles) {
        $text = Read-Text $file
        if ($text.Contains($symbol)) {
            Fail "Obsolete parallel telemetry symbol '$symbol' found in $file"
        }
    }
}

# 3. Critical unified-AI functions must have exactly one definition and live in their owner file.
$ownedDefinitions = [ordered]@{
    "AIGetDoctrineProfile"              = "AIUtils.cpp"
    "AIGetCommandRank"                  = "AIUtils.cpp"
    "AIFireteamId"                      = "AIUtils.cpp"
    "AITacticalIntent"                  = "AIUtils.cpp"
    "AITacticalRole"                    = "AIUtils.cpp"
    "AIUtilityPositionScore"            = "AIUtils.cpp"
    "AIKnownRouteExposureAcceptable"    = "AIUtils.cpp"
    "DecideFireteamCohesionAction"      = "AIUtils.cpp"
    "DecideDisengagementAction"         = "DecideAction.cpp"
    "DecideSuppressionResponse"         = "DecideAction.cpp"
    "DecideCombatCasualtyResponse"      = "Medical.cpp"
    "DecideCombatMedicRescue"           = "Medical.cpp"
    "DecideEmergencyBuddyAid"           = "Medical.cpp"
    "DecideEmergencySelfAid"            = "Medical.cpp"
}

foreach ($entry in $ownedDefinitions.GetEnumerator()) {
    $name = $entry.Key
    $expected = $entry.Value
    $hits = @(Count-Definitions $name $cppFiles)

    if ($hits.Count -ne 1) {
        Fail "Expected exactly one definition of '$name'; found $($hits.Count): $($hits -join ', ')"
        continue
    }

    $actual = Split-Path $hits[0] -Leaf
    if ($actual -ne $expected) {
        Fail "Definition of '$name' is in $actual; canonical owner is $expected"
    }

    $declCount = Count-HeaderDeclarations $name $headerText
    if ($declCount -ne 1) {
        Fail "Expected exactly one public declaration of '$name' in TacticalAI/ai.h; found $declCount"
    }
}

# 4. Planner telemetry adapters must exist once, locally, and feed the shared VRAnalytics stream.
$plannerAdapters = @(
    "VRPlannerTraceBeginDecision",
    "VRPlannerTraceCandidate",
    "VRPlannerTraceReject",
    "VRPlannerTraceSelect"
)

foreach ($name in $plannerAdapters) {
    $escaped = [regex]::Escape($name)
    $pattern = "(?m)^\s*static\s+(?:UINT32|void)\s+" + $escaped + "\s*\("
    $count = [regex]::Matches($decideText, $pattern).Count
    if ($count -ne 1) {
        Fail "Expected one local planner adapter '$name' in DecideAction.cpp; found $count"
    }
}

if (-not $decideText.Contains("VRAnalyticsBeginDecision")) {
    Fail "DecideAction.cpp planner adapters are not connected to VRAnalyticsBeginDecision"
}
if (-not $decideText.Contains("VRAnalyticsCommitDecision")) {
    Fail "DecideAction.cpp planner adapters are not connected to VRAnalyticsCommitDecision"
}

# 5. Canonical architecture declaration must remain explicit.
if (-not $frameworkText.Contains("install/all-2026-09-12")) {
    Fail "UNIFIED_AI_FRAMEWORK.md no longer names install/all-2026-09-12 as canonical"
}
if ($frameworkText -notmatch "(?i)single") {
    Fail "UNIFIED_AI_FRAMEWORK.md no longer clearly describes a single-source AI architecture"
}

# 6. Dormant CQB module must remain compiled-but-disabled until explicit activation review.
if (-not $cqbText.Contains("BOOLEAN VRCQB_IsRuntimeEnabled(void)")) {
    Fail "Dormant CQB module is missing its runtime gate."
}
if ($cqbText -notmatch "BOOLEAN\s+VRCQB_IsRuntimeEnabled\s*\(void\)\s*\{\s*return\s+FALSE\s*;\s*\}") {
    Fail "CQB runtime gate no longer hard-returns FALSE. Activation requires explicit integration review."
}
if ($decideText.Contains("VRCQB_")) {
    Fail "DecideAction.cpp references VRCQB_* while CQB is classified as dormant staging."
}

# 7. No tracked duplicate canonical architecture document in TacticalAI under an old name.
$obsoleteDocs = @(
    (Join-Path $tacticalAI "Human_Tactical_Planner.md"),
    (Join-Path $tacticalAI "Deidranna_Doctrine.md")
)
foreach ($doc in $obsoleteDocs) {
    if (Test-Path $doc) {
        Fail "Obsolete branch-era AI architecture document is active in canonical tree: $doc"
    }
}

Write-Host ""
if ($script:Failed) {
    Write-Host "FAIL: unified AI integrity problems were detected."
    exit 1
}

Write-Host "PASS: one AI interface, one owner per critical behaviour, one telemetry path, no merge markers."
exit 0
