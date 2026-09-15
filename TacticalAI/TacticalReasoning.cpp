#ifdef PRECOMPILEDHEADERS
	#include "AI All.h"
#else
	#include "ai.h"
	#include "AIInternals.h"
	#include "opplist.h"
	#include "Overhead.h"
	#include "Overhead Types.h"
	#include "PathAI.h"
	#include "Points.h"
	#include "Soldier Control.h"
	#include "los.h"
#endif

#include <string.h>

extern UINT32 guiTurnCnt;

typedef struct
{
	BOOLEAN fValid;
	UINT32 uiUniqueSoldierId;
	INT8 bTeam;
	UINT8 ubTask;
	INT32 sTargetGridNo;
	UINT8 ubTargetID;
	UINT32 uiExpiresTurn;
} AITASKRESERVATIONSLOT;

typedef struct
{
	BOOLEAN fValid;
	UINT32 uiUniqueSoldierId;
	AISHORTPLANSTATE Plan;
} AISHORTPLANSLOT;

static AITASKRESERVATIONSLOT gAITaskReservations[MAX_NUM_SOLDIERS];
static AISHORTPLANSLOT gAIShortPlans[MAX_NUM_SOLDIERS];

typedef struct
{
	BOOLEAN fValid;
	UINT32 uiUniqueSoldierId;
	INT32 sLastDecisionGridNo;
	UINT8 ubLastVisibleContacts;
	UINT8 ubLastDirectionMask;
	UINT16 usLastExposure;
	UINT32 uiLastReactionTurn;
} AICONTACTTRACKER;

static AICONTACTTRACKER gAIContactTracker[MAX_NUM_SOLDIERS];

static UINT8 AIKnowledgeAgeTurns(INT8 bKnowledge)
{
	switch (bKnowledge)
	{
	case SEEN_CURRENTLY:
	case SEEN_THIS_TURN:
	case HEARD_THIS_TURN:
		return 0;
	case SEEN_LAST_TURN:
	case HEARD_LAST_TURN:
		return 1;
	case SEEN_2_TURNS_AGO:
	case HEARD_2_TURNS_AGO:
		return 2;
	case SEEN_3_TURNS_AGO:
	case HEARD_3_TURNS_AGO:
		return 3;
	default:
		return 255;
	}
}

BOOLEAN AIBuildContactBelief(SOLDIERTYPE *pSoldier, UINT8 ubOpponentID, AICONTACTBELIEF *pBelief)
{
	if (!pBelief)
		return FALSE;

	memset(pBelief, 0, sizeof(AICONTACTBELIEF));
	pBelief->ubOpponentID = NOBODY;
	pBelief->sGridNo = NOWHERE;
	pBelief->bLevel = 0;
	pBelief->bKnowledge = NOT_HEARD_OR_SEEN;
	pBelief->ubSource = AI_BELIEF_SOURCE_NONE;
	pBelief->ubAgeTurns = 255;

	if (!pSoldier || ubOpponentID == NOBODY || ubOpponentID >= TOTAL_SOLDIERS)
		return FALSE;

	SOLDIERTYPE *pOpponent = MercPtrs[ubOpponentID];
	if (!pOpponent ||
		CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
		pSoldier->bSide == pOpponent->bSide)
	{
		return FALSE;
	}

	INT8 bKnowledge = Knowledge(pSoldier, ubOpponentID);
	if (bKnowledge == NOT_HEARD_OR_SEEN)
		return FALSE;

	INT32 sKnown = KnownLocation(pSoldier, ubOpponentID);
	if (TileIsOutOfBounds(sKnown))
		return FALSE;

	pBelief->ubOpponentID = ubOpponentID;
	pBelief->sGridNo = sKnown;
	pBelief->bLevel = KnownLevel(pSoldier, ubOpponentID);
	pBelief->bKnowledge = bKnowledge;
	pBelief->ubSource = UsePersonalKnowledge(pSoldier, ubOpponentID) ?
		AI_BELIEF_SOURCE_PERSONAL : AI_BELIEF_SOURCE_PUBLIC;

	INT32 iKnowledgeIndex = (INT32)bKnowledge - (INT32)OLDEST_HEARD_VALUE;
	if (iKnowledgeIndex >= 0 && iKnowledgeIndex < 10)
		pBelief->ubConfidence = (UINT8)__max(0, __min(100, ThreatPercent[iKnowledgeIndex]));
	else
		pBelief->ubConfidence = 0;

	pBelief->ubAgeTurns = AIKnowledgeAgeTurns(bKnowledge);
	pBelief->fDirectlyVisible =
		(PersonalKnowledge(pSoldier, ubOpponentID) == SEEN_CURRENTLY);

	return TRUE;
}

BOOLEAN AIBuildPrimaryContactBelief(SOLDIERTYPE *pSoldier, INT32 sPreferredGridNo, AICONTACTBELIEF *pBelief)
{
	if (!pBelief)
		return FALSE;

	memset(pBelief, 0, sizeof(AICONTACTBELIEF));
	pBelief->ubOpponentID = NOBODY;
	pBelief->sGridNo = NOWHERE;
	pBelief->ubAgeTurns = 255;

	if (!pSoldier)
		return FALSE;

	INT32 iBestScore = -1000000;
	AICONTACTBELIEF Best;

	for (UINT16 i = 0; i < TOTAL_SOLDIERS; ++i)
	{
		AICONTACTBELIEF Candidate;
		if (!AIBuildContactBelief(pSoldier, (UINT8)i, &Candidate))
			continue;

		INT32 iDistance = TileIsOutOfBounds(sPreferredGridNo) ?
			PythSpacesAway(pSoldier->sGridNo, Candidate.sGridNo) :
			PythSpacesAway(sPreferredGridNo, Candidate.sGridNo);

		// Prefer the contact matching the caller's already-selected legal threat,
		// then fresher/more certain contacts. No hidden current target state is read.
		INT32 iScore = (INT32)Candidate.ubConfidence * 4 - iDistance * 12;
		if (!TileIsOutOfBounds(sPreferredGridNo) && iDistance <= 1)
			iScore += 500;
		if (Candidate.fDirectlyVisible)
			iScore += 120;

		if (iScore > iBestScore)
		{
			iBestScore = iScore;
			Best = Candidate;
		}
	}

	if (iBestScore == -1000000)
		return FALSE;

	*pBelief = Best;
	return TRUE;
}

BOOLEAN AIEvaluateTacticalPosition(SOLDIERTYPE *pSoldier, INT32 sCandidateSpot,
	INT32 sTargetSpot, UINT16 usMovementMode, AITACTICALPOSITIONFEATURES *pFeatures)
{
	if (!pFeatures)
		return FALSE;

	memset(pFeatures, 0, sizeof(AITACTICALPOSITIONFEATURES));
	if (!pSoldier || TileIsOutOfBounds(sCandidateSpot))
		return FALSE;

	pFeatures->usCurrentExposure =
		AIKnownThreatExposure(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
	pFeatures->usCandidateExposure =
		AIKnownThreatExposure(pSoldier, sCandidateSpot, pSoldier->pathing.bLevel);
	pFeatures->sExposureDelta = (INT16)__max(-32767, __min(32767,
		(INT32)pFeatures->usCurrentExposure - (INT32)pFeatures->usCandidateExposure));

	pFeatures->fCover = AnyCoverAtSpot(pSoldier, sCandidateSpot);
	pFeatures->fSightCover = SightCoverAtSpot(pSoldier, sCandidateSpot, FALSE);
	pFeatures->fProneCover = ProneSightCoverAtSpot(pSoldier, sCandidateSpot, FALSE);
	pFeatures->fSmoke = InSmoke(sCandidateSpot, pSoldier->pathing.bLevel);
	pFeatures->ubSupport = CountNearbyFriends(
		pSoldier, sCandidateSpot, DAY_VISION_RANGE / 3);
	pFeatures->ubAdjacentFriends =
		NumberOfTeamMatesAdjacent(pSoldier, sCandidateSpot);
	pFeatures->sReactionRisk = (INT16)__min(32767,
		AIInferredReactionRisk(pSoldier, sCandidateSpot, pSoldier->pathing.bLevel));
	if (usMovementMode != 0)
	{
		pFeatures->sPathExposure = (INT16)__min(32767,
			AIPathExposureCost(pSoldier, sCandidateSpot, usMovementMode));
	}

	if (!TileIsOutOfBounds(sTargetSpot))
	{
		INT32 iCurrentDistance = PythSpacesAway(pSoldier->sGridNo, sTargetSpot);
		INT32 iCandidateDistance = PythSpacesAway(sCandidateSpot, sTargetSpot);
		pFeatures->sMissionProgress = (INT16)__max(-32767, __min(32767,
			iCurrentDistance - iCandidateDistance));

		if (AICompetenceTier(pSoldier) >= AI_COMPETENCE_REGULAR &&
			AIAllowsPlanComplexity(pSoldier, AI_PLAN_COORDINATED,
				(UINT32)(sCandidateSpot + 31)))
		{
			pFeatures->sCrossfire = (INT16)__max(-32767, __min(32767,
				AICrossfirePositionScore(pSoldier, sCandidateSpot, sTargetSpot)));
		}

		if (AICheckHasGun(pSoldier))
		{
			INT32 iGunRange = __max(1, (INT32)AIGunRange(pSoldier) / CELL_X_SIZE);
			INT32 iIdealRange = iGunRange / 2;
			if (AICheckShortWeaponRange(pSoldier))
				iIdealRange = __max(2, iGunRange / 3);
			pFeatures->sRangeError = (INT16)__min(32767,
				abs(iCandidateDistance - iIdealRange));
		}
	}

	return TRUE;
}

INT32 AIScoreTacticalPosition(SOLDIERTYPE *pSoldier, const AITACTICALPOSITIONFEATURES *pFeatures,
	INT32 sCandidateSpot, INT32 sTargetSpot, INT8 bIntent, INT8 bRole)
{
	if (!pSoldier || !pFeatures || TileIsOutOfBounds(sCandidateSpot))
		return -10000;

	INT32 iScore = 0;

	iScore += __max(-70, __min(70, (INT32)pFeatures->sExposureDelta / 3));
	if (pFeatures->fCover) iScore += 22;
	if (pFeatures->fSightCover) iScore += 18;
	if (pFeatures->fProneCover) iScore += 8;
	if (!pFeatures->fCover && pFeatures->usCandidateExposure > 0) iScore -= 28;

	iScore += 6 * __min((UINT8)3, pFeatures->ubSupport);
	if (pFeatures->ubSupport == 0) iScore -= 14;
	if (pFeatures->ubAdjacentFriends > 1)
		iScore -= 12 * (pFeatures->ubAdjacentFriends - 1);

	switch (bIntent)
	{
	case AI_INTENT_PRESS:
		iScore += __max(-30, __min(30, 5 * (INT32)pFeatures->sMissionProgress));
		break;
	case AI_INTENT_FLANK:
		iScore += __max(-15, __min(18, 3 * (INT32)pFeatures->sMissionProgress));
		break;
	case AI_INTENT_FALLBACK:
	case AI_INTENT_DISENGAGE:
		iScore += __max(-30, __min(35, -5 * (INT32)pFeatures->sMissionProgress));
		break;
	default:
		break;
	}

	if (bRole == AI_ROLE_FLANKER)
		iScore += 2 * pFeatures->sCrossfire;
	else if (bRole == AI_ROLE_MANEUVER)
		iScore += pFeatures->sCrossfire;
	else if (bRole == AI_ROLE_SUPPORT && pFeatures->sCrossfire < 0)
		iScore += pFeatures->sCrossfire / 2;

	if (!TileIsOutOfBounds(sTargetSpot) && AICheckHasGun(pSoldier))
	{
		INT32 iRangeError = pFeatures->sRangeError;
		if (bRole == AI_ROLE_SUPPORT || bRole == AI_ROLE_SCREEN)
		{
			INT32 iCandidateDistance = PythSpacesAway(sCandidateSpot, sTargetSpot);
			INT32 iGunRange = __max(1, (INT32)AIGunRange(pSoldier) / CELL_X_SIZE);
			INT32 iSupportRange = __max(2, (3 * iGunRange) / 4);
			iRangeError = abs(iCandidateDistance - iSupportRange);
		}
		iScore -= __min((INT32)24, iRangeError * 2);
	}

	if (pFeatures->fSmoke)
	{
		if (bIntent == AI_INTENT_FALLBACK || bIntent == AI_INTENT_DISENGAGE ||
			pSoldier->aiData.bUnderFire)
			iScore += 14;
		else if (bRole == AI_ROLE_SUPPORT)
			iScore -= 6;
	}

	if (pSoldier->aiData.bUnderFire &&
		pFeatures->usCandidateExposure < pFeatures->usCurrentExposure)
	{
		iScore += 12;
	}

	iScore -= pFeatures->sReactionRisk / 3;

	// Route quality is now a first-class spatial consideration. Keep the weight
	// deliberately bounded so a slightly riskier but much better destination can win.
	if (pFeatures->sPathExposure > 0)
	{
		if (pFeatures->sPathExposure >= 10000)
			return -10000;
		iScore -= __min((INT32)45, (INT32)pFeatures->sPathExposure / 8);
	}

	iScore += AICompetenceUtilityNoise(pSoldier, sCandidateSpot,
		(UINT32)(sTargetSpot + 173));

	return __max(-250, __min(250, iScore));
}

static BOOLEAN AISameTaskTarget(const AITASKRESERVATIONSLOT *pSlot,
	UINT8 ubTask, INT32 sTargetGridNo, UINT8 ubTargetID)
{
	if (!pSlot || !pSlot->fValid || pSlot->ubTask != ubTask)
		return FALSE;

	if (ubTargetID != NOBODY && pSlot->ubTargetID != NOBODY)
		return pSlot->ubTargetID == ubTargetID;

	if (!TileIsOutOfBounds(sTargetGridNo) && !TileIsOutOfBounds(pSlot->sTargetGridNo))
		return PythSpacesAway(sTargetGridNo, pSlot->sTargetGridNo) <= 3;

	return ubTargetID == pSlot->ubTargetID;
}

static BOOLEAN AIValidReservationOwner(SOLDIERTYPE *pSoldier, UINT8 ubOwnerID,
	const AITASKRESERVATIONSLOT *pSlot)
{
	if (!pSoldier || !pSlot || !pSlot->fValid || ubOwnerID >= MAX_NUM_SOLDIERS)
		return FALSE;

	SOLDIERTYPE *pOwner = MercPtrs[ubOwnerID];
	if (!pOwner || !pOwner->bActive || !pOwner->bInSector ||
		pOwner->stats.bLife < OKLIFE || pOwner->bCollapsed ||
		pOwner->uiUniqueSoldierIdValue != pSlot->uiUniqueSoldierId ||
		pOwner->bTeam != pSoldier->bTeam)
	{
		return FALSE;
	}

	if (AICombatTeam(pSoldier) && !AISameFireteam(pSoldier, pOwner))
		return FALSE;

	if (pSlot->uiExpiresTurn < guiTurnCnt + 1)
		return FALSE;

	return TRUE;
}

UINT8 AICountTacticalTaskReservations(SOLDIERTYPE *pSoldier, UINT8 ubTask,
	INT32 sTargetGridNo, UINT8 ubTargetID)
{
	if (!pSoldier || ubTask == AI_TASK_NONE)
		return 0;

	UINT8 ubCount = 0;
	for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
	{
		const AITASKRESERVATIONSLOT *pSlot = &gAITaskReservations[i];
		if (!AIValidReservationOwner(pSoldier, (UINT8)i, pSlot))
			continue;
		if (AISameTaskTarget(pSlot, ubTask, sTargetGridNo, ubTargetID))
			++ubCount;
	}
	return ubCount;
}

BOOLEAN AIReserveTacticalTask(SOLDIERTYPE *pSoldier, UINT8 ubTask, INT32 sTargetGridNo,
	UINT8 ubTargetID, UINT8 ubMaxOwners, UINT8 ubTurns)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS || ubTask == AI_TASK_NONE)
		return FALSE;

	AITASKRESERVATIONSLOT *pMine = &gAITaskReservations[pSoldier->ubID];

	if (pMine->fValid &&
		pMine->uiUniqueSoldierId == pSoldier->uiUniqueSoldierIdValue &&
		AISameTaskTarget(pMine, ubTask, sTargetGridNo, ubTargetID) &&
		pMine->uiExpiresTurn >= guiTurnCnt + 1)
	{
		pMine->uiExpiresTurn = guiTurnCnt + __max((UINT8)1, ubTurns);
		return TRUE;
	}

	UINT8 ubOwners = AICountTacticalTaskReservations(
		pSoldier, ubTask, sTargetGridNo, ubTargetID);
	if (ubMaxOwners > 0 && ubOwners >= ubMaxOwners)
		return FALSE;

	memset(pMine, 0, sizeof(AITASKRESERVATIONSLOT));
	pMine->fValid = TRUE;
	pMine->uiUniqueSoldierId = pSoldier->uiUniqueSoldierIdValue;
	pMine->bTeam = pSoldier->bTeam;
	pMine->ubTask = ubTask;
	pMine->sTargetGridNo = sTargetGridNo;
	pMine->ubTargetID = ubTargetID;
	pMine->uiExpiresTurn = guiTurnCnt + __max((UINT8)1, ubTurns);
	return TRUE;
}

void AIReleaseTacticalTask(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	memset(&gAITaskReservations[pSoldier->ubID], 0, sizeof(AITASKRESERVATIONSLOT));
	gAITaskReservations[pSoldier->ubID].sTargetGridNo = NOWHERE;
	gAITaskReservations[pSoldier->ubID].ubTargetID = NOBODY;
}

BOOLEAN AIBeginShortPlan(SOLDIERTYPE *pSoldier, UINT8 ubPlanType, INT32 sTargetGridNo,
	UINT8 ubTargetID, UINT8 ubTurns)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS ||
		ubPlanType == AI_SHORT_PLAN_NONE)
	{
		return FALSE;
	}

	AISHORTPLANSLOT *pSlot = &gAIShortPlans[pSoldier->ubID];
	memset(pSlot, 0, sizeof(AISHORTPLANSLOT));
	pSlot->fValid = TRUE;
	pSlot->uiUniqueSoldierId = pSoldier->uiUniqueSoldierIdValue;
	pSlot->Plan.ubType = ubPlanType;
	pSlot->Plan.ubStep = 0;
	pSlot->Plan.sTargetGridNo = sTargetGridNo;
	pSlot->Plan.ubTargetID = ubTargetID;
	pSlot->Plan.uiExpiresTurn = guiTurnCnt + __max((UINT8)1, ubTurns);
	return TRUE;
}

BOOLEAN AIGetShortPlan(SOLDIERTYPE *pSoldier, AISHORTPLANSTATE *pPlan)
{
	if (!pSoldier || !pPlan || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return FALSE;

	AISHORTPLANSLOT *pSlot = &gAIShortPlans[pSoldier->ubID];
	if (!pSlot->fValid ||
		pSlot->uiUniqueSoldierId != pSoldier->uiUniqueSoldierIdValue ||
		pSlot->Plan.uiExpiresTurn < guiTurnCnt + 1)
	{
		memset(pSlot, 0, sizeof(AISHORTPLANSLOT));
		return FALSE;
	}

	*pPlan = pSlot->Plan;
	return TRUE;
}

void AIAdvanceShortPlan(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	AISHORTPLANSLOT *pSlot = &gAIShortPlans[pSoldier->ubID];
	if (!pSlot->fValid ||
		pSlot->uiUniqueSoldierId != pSoldier->uiUniqueSoldierIdValue)
	{
		return;
	}

	if (pSlot->Plan.ubStep < 255)
		++pSlot->Plan.ubStep;
}

void AICancelShortPlan(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	memset(&gAIShortPlans[pSoldier->ubID], 0, sizeof(AISHORTPLANSLOT));
	gAIShortPlans[pSoldier->ubID].Plan.sTargetGridNo = NOWHERE;
	gAIShortPlans[pSoldier->ubID].Plan.ubTargetID = NOBODY;
}

static BOOLEAN AIThreatMaskHasWideSeparation(UINT8 ubMask)
{
	for (UINT8 a = 0; a < NUM_WORLD_DIRECTIONS; ++a)
	{
		if (!(ubMask & (1 << a)))
			continue;

		for (UINT8 b = a + 1; b < NUM_WORLD_DIRECTIONS; ++b)
		{
			if (!(ubMask & (1 << b)))
				continue;

			UINT8 ubDiff = (UINT8)abs((INT32)a - (INT32)b);
			ubDiff = __min(ubDiff, (UINT8)(NUM_WORLD_DIRECTIONS - ubDiff));
			if (ubDiff >= 3)
				return TRUE;
		}
	}

	return FALSE;
}

BOOLEAN AIObserveContactChange(SOLDIERTYPE *pSoldier, AICONTACTCHANGE *pChange)
{
	if (!pChange)
		return FALSE;

	memset(pChange, 0, sizeof(AICONTACTCHANGE));
	pChange->sPreviousGridNo = NOWHERE;

	if (!pSoldier || !AICombatTeam(pSoldier) || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return FALSE;

	UINT8 ubVisible = 0;
	UINT8 ubMask = 0;

	for (UINT16 i = 0; i < TOTAL_SOLDIERS; ++i)
	{
		SOLDIERTYPE *pOpponent = MercPtrs[i];
		if (!pOpponent || pOpponent == pSoldier ||
			CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			pOpponent->bSide == pSoldier->bSide)
		{
			continue;
		}

		// Surprise is based only on personal, current sight. Public/radio knowledge
		// can shape normal tactics but cannot create a fake "I just saw them" event.
		if (PersonalKnowledge(pSoldier, pOpponent->ubID) != SEEN_CURRENTLY)
			continue;
		if (LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) <= 0)
			continue;

		INT32 sKnown = KnownPersonalLocation(pSoldier, pOpponent->ubID);
		if (TileIsOutOfBounds(sKnown))
			continue;

		++ubVisible;
		UINT8 ubDir = AIDirection(pSoldier->sGridNo, sKnown);
		if (ubDir < NUM_WORLD_DIRECTIONS)
			ubMask |= (UINT8)(1 << ubDir);
	}

	AICONTACTTRACKER *pTracker = &gAIContactTracker[pSoldier->ubID];

	if (!pTracker->fValid ||
		pTracker->uiUniqueSoldierId != pSoldier->uiUniqueSoldierIdValue ||
		TileIsOutOfBounds(pTracker->sLastDecisionGridNo))
	{
		memset(pTracker, 0, sizeof(AICONTACTTRACKER));
		pTracker->fValid = TRUE;
		pTracker->uiUniqueSoldierId = pSoldier->uiUniqueSoldierIdValue;
		pTracker->sLastDecisionGridNo = pSoldier->sGridNo;
		pTracker->ubLastVisibleContacts = ubVisible;
		pTracker->ubLastDirectionMask = ubMask;
		pTracker->usLastExposure =
			AIKnownThreatExposure(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
		pTracker->uiLastReactionTurn = 0xFFFFFFFF;
		return FALSE;
	}

	pChange->ubVisibleContacts = ubVisible;
	pChange->ubDirectionMask = ubMask;
	pChange->ubNewContacts =
		(ubVisible > pTracker->ubLastVisibleContacts) ?
		(ubVisible - pTracker->ubLastVisibleContacts) : 0;
	pChange->sPreviousGridNo = pTracker->sLastDecisionGridNo;
	pChange->fMovedSinceLastDecision =
		(pTracker->sLastDecisionGridNo != pSoldier->sGridNo);
	pChange->fMultiAngleThreat = AIThreatMaskHasWideSeparation(ubMask);
	pChange->usCurrentExposure =
		AIKnownThreatExposure(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);

	BOOLEAN fDirectionWorsened =
		ubMask != pTracker->ubLastDirectionMask &&
		pChange->fMultiAngleThreat;
	BOOLEAN fExposureWorsened =
		pChange->usCurrentExposure >= pTracker->usLastExposure + 40;

	pChange->fSurprise =
		pChange->fMovedSinceLastDecision &&
		pChange->ubNewContacts > 0 &&
		(ubVisible >= 2 || pChange->fMultiAngleThreat ||
		 pChange->usCurrentExposure >= 140);

	pChange->fEncirclementPressure =
		ubVisible >= 3 &&
		pChange->fMultiAngleThreat &&
		(pChange->fMovedSinceLastDecision || fDirectionWorsened || fExposureWorsened) &&
		pChange->usCurrentExposure >= 100;

	BOOLEAN fReaction =
		(pChange->fSurprise || pChange->fEncirclementPressure) &&
		pTracker->uiLastReactionTurn != guiTurnCnt;

	pTracker->sLastDecisionGridNo = pSoldier->sGridNo;
	pTracker->ubLastVisibleContacts = ubVisible;
	pTracker->ubLastDirectionMask = ubMask;
	pTracker->usLastExposure = pChange->usCurrentExposure;

	if (fReaction)
		pTracker->uiLastReactionTurn = guiTurnCnt;

	return fReaction;
}

void AIResetTacticalReasoningStateForLoad(void)
{
	memset(gAITaskReservations, 0, sizeof(gAITaskReservations));
	memset(gAIShortPlans, 0, sizeof(gAIShortPlans));
	memset(gAIContactTracker, 0, sizeof(gAIContactTracker));

	for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
	{
		gAITaskReservations[i].sTargetGridNo = NOWHERE;
		gAITaskReservations[i].ubTargetID = NOBODY;
		gAIShortPlans[i].Plan.sTargetGridNo = NOWHERE;
		gAIShortPlans[i].Plan.ubTargetID = NOBODY;
		gAIContactTracker[i].sLastDecisionGridNo = NOWHERE;
	}
}
