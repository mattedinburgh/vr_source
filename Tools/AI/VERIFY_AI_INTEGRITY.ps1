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
$cqbProject = Join-Path $tacticalAI "TacticalAI_VS2013.vcxproj"
$strategicDir = Join-Path $root "Strategic"
$movementHeader = Join-Path $strategicDir "Strategic Movement.h"
$operationalHeader = Join-Path $strategicDir "Strategic Operational AI.h"
$operationalSource = Join-Path $strategicDir "Strategic Operational AI.cpp"
$aiMain = Join-Path $tacticalAI "AIMain.cpp"

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
$cqbProjectText = Read-Text $cqbProject
$movementHeaderText = Read-Text $movementHeader
$operationalHeaderText = Read-Text $operationalHeader
$operationalSourceText = Read-Text $operationalSource
$aiMainText = Read-Text $aiMain

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
    "AIBuildContactBelief"              = "TacticalReasoning.cpp"
    "AIBuildPrimaryContactBelief"       = "TacticalReasoning.cpp"
    "AIBuildTacticalGeometry"           = "TacticalReasoning.cpp"
    "AIGeometryPositionScore"           = "TacticalReasoning.cpp"
    "AIPreferredFlankAction"            = "TacticalReasoning.cpp"
    "AIEvaluateTacticalPosition"        = "TacticalReasoning.cpp"
    "AIScoreTacticalPosition"           = "TacticalReasoning.cpp"
    "AIReserveTacticalTask"             = "TacticalReasoning.cpp"
    "AICountTacticalTaskReservations"   = "TacticalReasoning.cpp"
    "AIBeginShortPlan"                  = "TacticalReasoning.cpp"
    "AIGetShortPlan"                    = "TacticalReasoning.cpp"
    "AIObserveContactChange"            = "TacticalReasoning.cpp"
    "AIResetTacticalReasoningStateForLoad" = "TacticalReasoning.cpp"
    "FindGeometryBreakoutSpot"          = "FindLocations.cpp"
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

# 6. Active CQB module must remain integrated through the canonical decision hierarchy.
if (-not $cqbText.Contains("BOOLEAN VRCQB_IsRuntimeEnabled(void)")) {
    Fail "CQB module is missing its runtime gate."
}
if ($cqbText -notmatch "BOOLEAN\s+VRCQB_IsRuntimeEnabled\s*\(void\)\s*\{\s*return\s+TRUE\s*;\s*\}") {
    Fail "CQB runtime gate is not enabled after explicit activation approval."
}

$cqbAdapterDefs = [regex]::Matches(
    $cqbText,
    "(?m)^\s*INT8\s+VRCQB_DecideAction\s*\("
).Count
if ($cqbAdapterDefs -ne 1) {
    Fail "Expected exactly one VRCQB_DecideAction definition; found $cqbAdapterDefs."
}

$cqbAdapterDecls = Count-HeaderDeclarations "VRCQB_DecideAction" $cqbHeaderText
if ($cqbAdapterDecls -ne 1) {
    Fail "Expected exactly one VRCQB_DecideAction declaration in CQBBuildingDoctrine.h; found $cqbAdapterDecls."
}

$cqbCallCount = [regex]::Matches($decideText, "VRCQB_DecideAction\s*\(").Count
if ($cqbCallCount -ne 2) {
    Fail "Expected exactly two CQB runtime hooks in DecideAction.cpp (RED and BLACK); found $cqbCallCount."
}

if (-not $decideText.Contains('#include "CQBBuildingDoctrine.h"')) {
    Fail "DecideAction.cpp does not include the canonical CQB interface."
}
if (-not $cqbProjectText.Contains('ClCompile Include="CQBBuildingDoctrine.cpp"')) {
    Fail "CQBBuildingDoctrine.cpp is not compiled by TacticalAI_VS2013.vcxproj."
}
if (-not $cqbProjectText.Contains('ClCompile Include="TacticalReasoning.cpp"')) {
    Fail "TacticalReasoning.cpp is not compiled by TacticalAI_VS2013.vcxproj."
}
if (-not $cqbProjectText.Contains('ClInclude Include="CQBBuildingDoctrine.h"')) {
    Fail "CQBBuildingDoctrine.h is not registered in TacticalAI_VS2013.vcxproj."
}
if (-not $cqbText.Contains("VRAnalyticsBeginDecision") -or
    -not $cqbText.Contains("VRAnalyticsCommitDecision")) {
    Fail "Active CQB planner is not connected to canonical VRAnalytics telemetry."
}
if (-not $cqbText.Contains("AI_TASK_ENTRY_POINT") -or
    -not $cqbText.Contains("AI_TASK_ENTRY_SUPPORT") -or
    -not $cqbText.Contains("AI_SHORT_PLAN_CQB")) {
    Fail "CQB planner is no longer integrated with shared reservations/short-plan state."
}

$redStart = $decideText.IndexOf("INT8 DecideActionRed(SOLDIERTYPE *pSoldier)")
$blackStart = $decideText.IndexOf("INT8 DecideActionBlack(SOLDIERTYPE *pSoldier)")
if ($redStart -lt 0 -or $blackStart -le $redStart) {
    Fail "Could not isolate RED/BLACK decision functions for CQB ordering audit."
}
else {
    $redText = $decideText.Substring($redStart, $blackStart - $redStart)
    $redCQB = $redText.IndexOf("VRCQB_DecideAction")
    foreach ($senior in @(
        "DecideDisengagementAction",
        "DecideTacticalFallback",
        "DecideCombatCasualtyResponse"
    )) {
        $seniorPos = $redText.IndexOf($senior)
        if ($seniorPos -lt 0 -or $redCQB -lt 0 -or $seniorPos -gt $redCQB) {
            Fail "RED CQB hook no longer sits below senior '$senior' logic."
        }
    }

    $blackText = $decideText.Substring($blackStart)
    $blackCQB = $blackText.IndexOf("VRCQB_DecideAction")
    $attackPriority = $blackText.IndexOf("if (ubBestAttackAction != AI_ACTION_NONE)")
    if ($blackCQB -lt 0 -or $attackPriority -lt 0 -or $attackPriority -gt $blackCQB) {
        Fail "BLACK CQB hook no longer preserves desirable immediate attacks."
    }
}

# 7. Strategic operational AI must preserve raw-save layout and knowledge fairness.
if (-not $movementHeaderText.Contains("VR_ENEMYGROUP_SAVE_LAYOUT_MUST_BE_29")) {
    Fail "Strategic Movement.h is missing the 29-byte ENEMYGROUP save-layout compile guard."
}
foreach ($field in @(
    "ubFormationIDLo",
    "ubFormationIDHi",
    "ubOperationalFlagsLo",
    "ubOperationalFlagsHi"
)) {
    if (-not $movementHeaderText.Contains($field)) {
        Fail "Strategic Movement.h is missing save-compatible byte field '$field'."
    }
}
if ($movementHeaderText.Contains("UINT16 usFormationID") -or
    $movementHeaderText.Contains("UINT16 usOperationalFlags")) {
    Fail "ENEMYGROUP reintroduced aligned UINT16 fields into legacy raw-save bytes."
}
if (-not $operationalSourceText.Contains("VR_ReadFormationID") -or
    -not $operationalSourceText.Contains("VR_WriteFormationID") -or
    -not $operationalSourceText.Contains("VR_ReadOperationalFlags") -or
    -not $operationalSourceText.Contains("VR_WriteOperationalFlags")) {
    Fail "Strategic operational state no longer uses byte-pair accessors."
}
if ($operationalSourceText.Contains("->usFormationID") -or
    $operationalSourceText.Contains("->usOperationalFlags")) {
    Fail "Strategic operational source bypasses save-compatible byte-pair accessors."
}
$operationalGatePattern = "(?m)^\s*#define\s+VR_OPERATIONAL_DECISION_LOOP_ENABLED\s+0\s*$"
if (-not [regex]::IsMatch($operationalHeaderText, $operationalGatePattern)) {
    Fail "Operational strategic movement gate was enabled without explicit integration approval."
}
if ($operationalSourceText.Contains("Strategic Operational BlackBox.txt")) {
    Fail "Strategic AI reintroduced a parallel black-box file instead of shared VRAnalytics."
}
if (-not $operationalSourceText.Contains("VRAnalyticsBeginDecision") -or
    -not $operationalSourceText.Contains("VRAnalyticsCommitDecision")) {
    Fail "Strategic operational AI is not connected to shared VRAnalytics."
}

$scoreStart = $operationalSourceText.IndexOf("INT32 VR_ScoreOperationalTarget")
$scoreEnd = $operationalSourceText.IndexOf("UINT8 VR_FindBestOperationalTarget", $scoreStart + 1)
if ($scoreStart -lt 0 -or $scoreEnd -le $scoreStart) {
    Fail "Could not isolate operational target scorer for knowledge-fairness audit."
}
else {
    $scoreText = $operationalSourceText.Substring($scoreStart, $scoreEnd - $scoreStart)
    if ($scoreText.Contains("PlayerMercsInSector") -or
        $scoreText.Contains("CountAllMilitiaInSector")) {
        Fail "Operational target scorer reads live player/militia presence instead of formation knowledge."
    }
}

if (-not $aiMainText.Contains("VR_RegisterTacticalRetreatSoldier") -or
    -not $aiMainText.Contains("fPersistentEnemyRetreat")) {
    Fail "Tactical map-edge escape is no longer wired to persistent strategic retreat formations."
}
if (-not $aiMainText.Contains("if( !fPersistentEnemyRetreat )") -or
    -not $aiMainText.Contains("QueueEnemyRetreatConflict")) {
    Fail "Persistent tactical-retreat handoff lost its legacy fail-safe/pursuit path."
}

# 8. No tracked duplicate canonical architecture document in TacticalAI under an old name.
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
