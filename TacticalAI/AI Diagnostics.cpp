#ifdef PRECOMPILEDHEADERS
#include "AI All.h"
#else
#include "ai.h"
#include "AI Diagnostics.h"
#include "Overhead.h"
#include "Game Clock.h"
#include "Campaign Tactical Telemetry.h"
#include <stdio.h>
#include <string.h>
#endif

#include "AI Diagnostics.h"
#include <stdio.h>
#include <string.h>

static BOOLEAN gfAITraceEnabled = TRUE;
static UINT32 guiAITraceDecisionSerial = 0;
static UINT32 guiAITraceCurrentDecision[MAX_NUM_SOLDIERS] = { 0 };

static void AITraceSanitize(const CHAR8 *pIn, CHAR8 *pOut, UINT32 uiSize)
{
	if (!pOut || uiSize == 0)
		return;
	if (!pIn)
	{
		pOut[0] = '-';
		if (uiSize > 1) pOut[1] = '\0';
		return;
	}

	UINT32 j = 0;
	for (UINT32 i = 0; pIn[i] && j + 1 < uiSize; ++i)
	{
		CHAR8 ch = pIn[i];
		if (ch == '\t' || ch == '\r' || ch == '\n')
			ch = ' ';
		pOut[j++] = ch;
	}
	pOut[j] = '\0';
}

static void AITraceEnsureHeader()
{
	if (!gfAITraceEnabled)
		return;

	FILE *fp = fopen("Campaign Tactical Decisions.tsv", "a+");
	if (!fp)
		return;

	fseek(fp, 0, SEEK_END);
	if (ftell(fp) == 0)
	{
		fprintf(fp,
			"schema_version\tframework_version\tsession_id\tbattle_id\tturn_id\tdecision_id\tworld_min\tday\thour\tminute\tsector_x\tsector_y\tsector_z\tevent\tsource\tactor_team\tactor_id\tactor_class\tcompetence\treliability\tintent\trole\ttarget_grid\taction\tcandidate_grid\tscore\trunner_up\texposure\troute_cost\tsupport\tcrossfire\treaction_risk\ttarget_saturation\tlife\tap\tbreath\tshock\tstress\tpersonal_risk\trisk_tolerance\tbattle_situation\trout_pressure\tsmoke_reserve\tfriction_changed\treason\n");
	}
	fclose(fp);
}

static void AITraceWrite(SOLDIERTYPE *pSoldier, UINT32 uiDecisionID,
	const CHAR8 *pEvent, const CHAR8 *pSource, INT32 sTargetSpot,
	INT8 bIntent, INT8 bRole, INT8 bAction, INT32 sCandidateGrid,
	INT32 iScore, INT32 iRunnerUpScore, INT32 iRouteCost,
	INT32 iSupport, INT32 iCrossfire, BOOLEAN fFrictionChangedChoice,
	const CHAR8 *pReason)
{
	if (!gfAITraceEnabled || !pSoldier || !AICombatTeam(pSoldier))
		return;

	AITraceEnsureHeader();
	FILE *fp = fopen("Campaign Tactical Decisions.tsv", "a");
	if (!fp)
		return;

	CHAR8 zReason[384];
	CHAR8 zSource[96];
	AITraceSanitize(pReason, zReason, sizeof(zReason));
	AITraceSanitize(pSource, zSource, sizeof(zSource));

	INT32 iExposure = AIKnownThreatExposure(pSoldier,
		TileIsOutOfBounds(sCandidateGrid) ? pSoldier->sGridNo : sCandidateGrid,
		pSoldier->pathing.bLevel);
	INT32 iReaction = AIInferredReactionRisk(pSoldier,
		TileIsOutOfBounds(sCandidateGrid) ? pSoldier->sGridNo : sCandidateGrid,
		pSoldier->pathing.bLevel);
	INT32 iSaturation = TileIsOutOfBounds(sTargetSpot) ? 0 : AITargetSaturation(pSoldier, sTargetSpot);

	fprintf(fp,
		"%u\t%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%d\t%d\t%d\t%s\t%s\t%d\t%d\t%d\t%d\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n",
		VR_AI_COMPANION_SCHEMA_VERSION,
		VR_AI_FRAMEWORK_VERSION,
		VR_TacticalTelemetrySessionID(),
		VR_TacticalTelemetryBattleID(),
		VR_TacticalTelemetryTurnID(),
		uiDecisionID,
		GetWorldTotalMin(),
		GetWorldDay(),
		GetWorldHour(),
		GetWorldMinutesInDay() % 60,
		gWorldSectorX, gWorldSectorY, gbWorldSectorZ,
		pEvent ? pEvent : "-",
		zSource,
		pSoldier->bTeam,
		pSoldier->ubID,
		pSoldier->ubSoldierClass,
		AICompetenceTier(pSoldier),
		AIPlannerReliability(pSoldier),
		bIntent,
		bRole,
		sTargetSpot,
		bAction,
		sCandidateGrid,
		iScore,
		iRunnerUpScore,
		iExposure,
		iRouteCost,
		iSupport,
		iCrossfire,
		iReaction,
		iSaturation,
		pSoldier->stats.bLife,
		pSoldier->bActionPoints,
		pSoldier->bBreath,
		pSoldier->aiData.bShock,
		AILocalStress(pSoldier),
		AIPersonalRisk(pSoldier),
		AIPersonalRiskTolerance(pSoldier),
		AIBattleSituation(pSoldier),
		AILocalRoutPressure(pSoldier),
		AILocalSmokeReserve(pSoldier),
		fFrictionChangedChoice ? 1 : 0,
		zReason);
	fclose(fp);

	// Keep the human-readable Companion concise: record final decisions and outcomes,
	// while candidates/rejections stay in the machine-readable forensic stream.
	if ((pEvent && strcmp(pEvent, "SELECT") == 0) ||
		(pEvent && strcmp(pEvent, "OUTCOME") == 0))
	{
		FILE *fc = fopen("Campaign AI Companion.txt", "a");
		if (fc)
		{
			fprintf(fc,
				"[TACTICAL_DECISION][S%u][B%u][T%u][D%u][%c%d] actor=%u class=%u competence=%d event=%s source=%s intent=%d role=%d target=%d action=%d grid=%d score=%d reason=%s\n",
				VR_TacticalTelemetrySessionID(), VR_TacticalTelemetryBattleID(),
				VR_TacticalTelemetryTurnID(), uiDecisionID,
				'A' + gWorldSectorY - 1, gWorldSectorX,
				pSoldier->ubID, pSoldier->ubSoldierClass, AICompetenceTier(pSoldier),
				pEvent, zSource, bIntent, bRole, sTargetSpot, bAction,
				sCandidateGrid, iScore, zReason);
			fclose(fc);
		}
	}
}

UINT32 AITraceBeginDecision(SOLDIERTYPE *pSoldier, const CHAR8 *pSource,
	INT32 sTargetSpot, INT8 bIntent, INT8 bRole)
{
	if (!pSoldier || !AICombatTeam(pSoldier))
		return 0;

	++guiAITraceDecisionSerial;
	if (!guiAITraceDecisionSerial)
		++guiAITraceDecisionSerial;

	if (pSoldier->ubID < MAX_NUM_SOLDIERS)
		guiAITraceCurrentDecision[pSoldier->ubID] = guiAITraceDecisionSerial;

	AITraceWrite(pSoldier, guiAITraceDecisionSerial, "DECISION_BEGIN", pSource,
		sTargetSpot, bIntent, bRole, AI_ACTION_NONE, pSoldier->sGridNo,
		0, 0, 0, CountNearbyFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 3),
		0, FALSE, "planner decision context snapshot");
	return guiAITraceDecisionSerial;
}

void AITraceCandidate(SOLDIERTYPE *pSoldier, UINT32 uiDecisionID, const CHAR8 *pSource,
	INT8 bAction, INT32 sCandidateGrid, INT32 iScore, INT32 iRouteCost,
	INT32 iSupport, INT32 iCrossfire, const CHAR8 *pReason)
{
	INT32 sTarget = ClosestKnownOpponent(pSoldier, NULL, NULL);
	AITraceWrite(pSoldier, uiDecisionID, "CANDIDATE", pSource, sTarget,
		AITacticalIntent(pSoldier, sTarget), AITacticalRole(pSoldier, sTarget),
		bAction, sCandidateGrid, iScore, 0, iRouteCost, iSupport, iCrossfire,
		FALSE, pReason);
}

void AITraceReject(SOLDIERTYPE *pSoldier, UINT32 uiDecisionID, const CHAR8 *pSource,
	INT8 bAction, INT32 sCandidateGrid, const CHAR8 *pReason)
{
	INT32 sTarget = ClosestKnownOpponent(pSoldier, NULL, NULL);
	AITraceWrite(pSoldier, uiDecisionID, "REJECT", pSource, sTarget,
		AITacticalIntent(pSoldier, sTarget), AITacticalRole(pSoldier, sTarget),
		bAction, sCandidateGrid, 0, 0, 0, 0, 0, FALSE, pReason);
}

void AITraceSelect(SOLDIERTYPE *pSoldier, UINT32 uiDecisionID, const CHAR8 *pSource,
	INT8 bAction, INT32 sCandidateGrid, INT32 iScore, INT32 iRunnerUpScore,
	BOOLEAN fFrictionChangedChoice, const CHAR8 *pReason)
{
	INT32 sTarget = ClosestKnownOpponent(pSoldier, NULL, NULL);
	AITraceWrite(pSoldier, uiDecisionID, "SELECT", pSource, sTarget,
		AITacticalIntent(pSoldier, sTarget), AITacticalRole(pSoldier, sTarget),
		bAction, sCandidateGrid, iScore, iRunnerUpScore, 0,
		CountNearbyFriends(pSoldier, sCandidateGrid, DAY_VISION_RANGE / 3),
		TileIsOutOfBounds(sTarget) ? 0 : AICrossfirePositionScore(pSoldier, sCandidateGrid, sTarget),
		fFrictionChangedChoice, pReason);
}

void AITraceOutcome(SOLDIERTYPE *pSoldier, UINT32 uiDecisionID, const CHAR8 *pOutcome,
	INT32 iValue1, INT32 iValue2, const CHAR8 *pReason)
{
	if (!pSoldier)
		return;
	INT32 sTarget = ClosestKnownOpponent(pSoldier, NULL, NULL);
	CHAR8 zReason[384];
	sprintf(zReason, "%s; value1=%d value2=%d", pReason ? pReason : "-", iValue1, iValue2);
	AITraceWrite(pSoldier, uiDecisionID, "OUTCOME", pOutcome, sTarget,
		AITacticalIntent(pSoldier, sTarget), AITacticalRole(pSoldier, sTarget),
		pSoldier->aiData.bAction, pSoldier->aiData.usActionData, iValue1, iValue2,
		0, 0, 0, FALSE, zReason);
}

UINT32 AITraceCurrentDecision(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return 0;
	return guiAITraceCurrentDecision[pSoldier->ubID];
}
