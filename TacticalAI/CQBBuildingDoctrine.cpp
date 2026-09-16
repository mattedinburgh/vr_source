#include "AI All.h"
#include "CQBBuildingDoctrine.h"
#include "VRAnalytics.h"

#include <string.h>

extern UINT32 guiTurnCnt;
extern BOOLEAN FindWindowJumpDirection(SOLDIERTYPE *pSoldier, INT32 sGridNo, INT8 bDirection, INT8 *pbDirection);

#define VRCQB_LOCAL_SEARCH_RADIUS 6
#define VRCQB_ENTRY_SEARCH_RADIUS 8
#define VRCQB_INVALID_SCORE (-1000000)
#define VRCQB_PLAN_MEMORY_TURNS 2

typedef struct
{
	BOOLEAN fValid;
	UINT32 uiUniqueSoldierId;
	UINT32 uiTurnStamp;
	VRCQB_STATE eState;
	VRCQB_ROLE eRole;
	UINT16 usRoomNo;
	INT32 sEntryGridNo;
	INT32 sTargetGridNo;
} VRCQB_PLAN_SLOT;

static VRCQB_PLAN_SLOT gVRCQBPlan[TOTAL_SOLDIERS];
static UINT32 guiVRCQBLastTurnStamp = 0;

static INT32 VRCQBClamp(INT32 iValue, INT32 iMin, INT32 iMax)
{
	if (iValue < iMin) return iMin;
	if (iValue > iMax) return iMax;
	return iValue;
}

static UINT8 VRCQBClampU8(INT32 iValue)
{
	return (UINT8)VRCQBClamp(iValue, 0, 100);
}

static UINT32 VRCQBCurrentTurnStamp(void)
{
	return guiTurnCnt + 1;
}

static BOOLEAN VRCQBGetRoom(INT32 sGridNo, UINT16 *pusRoomNo)
{
	UINT16 usRoomNo = NO_ROOM;

	if (TileIsOutOfBounds(sGridNo) || !InARoom(sGridNo, &usRoomNo))
	{
		if (pusRoomNo) *pusRoomNo = NO_ROOM;
		return FALSE;
	}

	if (pusRoomNo) *pusRoomNo = usRoomNo;
	return TRUE;
}

static UINT8 VRCQBGetBuildingId(INT32 sGridNo)
{
	if (TileIsOutOfBounds(sGridNo) || !gubBuildingInfo)
		return NO_BUILDING;

	return gubBuildingInfo[sGridNo];
}

static BOOLEAN VRCQBSameStructure(INT32 sGridNoA, INT32 sGridNoB)
{
	UINT16 usRoomA = NO_ROOM;
	UINT16 usRoomB = NO_ROOM;
	const BOOLEAN fRoomA = VRCQBGetRoom(sGridNoA, &usRoomA);
	const BOOLEAN fRoomB = VRCQBGetRoom(sGridNoB, &usRoomB);

	if (fRoomA && fRoomB && usRoomA == usRoomB)
		return TRUE;

	const UINT8 ubBuildingA = VRCQBGetBuildingId(sGridNoA);
	const UINT8 ubBuildingB = VRCQBGetBuildingId(sGridNoB);
	return ubBuildingA != NO_BUILDING && ubBuildingA == ubBuildingB;
}

static BOOLEAN VRCQBIsAnchoredOrder(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier) return FALSE;

	return pSoldier->aiData.bOrders == STATIONARY ||
		pSoldier->aiData.bOrders == ONGUARD ||
		pSoldier->aiData.bOrders == SNIPER;
}

static BOOLEAN VRCQBGridAvailable(SOLDIERTYPE *pSoldier, INT32 sGridNo)
{
	if (!pSoldier || TileIsOutOfBounds(sGridNo))
		return FALSE;

	if (!NewOKDestination(pSoldier, sGridNo, FALSE, pSoldier->pathing.bLevel))
		return FALSE;

	const UINT8 ubOccupant = WhoIsThere2(sGridNo, pSoldier->pathing.bLevel);
	return ubOccupant == NOBODY || ubOccupant == pSoldier->ubID;
}

static UINT8 VRCQBCountFriendsNearSpot(SOLDIERTYPE *pSoldier, INT32 sGridNo, UINT8 ubDistance)
{
	if (!pSoldier || TileIsOutOfBounds(sGridNo))
		return 0;

	UINT8 ubCount = 0;
	for (UINT8 ubID = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		ubID <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++ubID)
	{
		SOLDIERTYPE *pFriend = MercPtrs[ubID];
		if (!pFriend || pFriend == pSoldier ||
			!pFriend->bActive || !pFriend->bInSector ||
			pFriend->stats.bLife < OKLIFE ||
			pFriend->bCollapsed || pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			pFriend->pathing.bLevel != pSoldier->pathing.bLevel)
		{
			continue;
		}

		if (PythSpacesAway(sGridNo, pFriend->sGridNo) <= ubDistance)
			++ubCount;
	}

	return ubCount;
}

static void VRCQBMantainPlanState(void)
{
	const UINT32 uiTurnStamp = VRCQBCurrentTurnStamp();

	if (guiVRCQBLastTurnStamp != 0 && uiTurnStamp < guiVRCQBLastTurnStamp)
	{
		memset(gVRCQBPlan, 0, sizeof(gVRCQBPlan));
	}

	guiVRCQBLastTurnStamp = uiTurnStamp;

	for (UINT16 uiIndex = 0; uiIndex < TOTAL_SOLDIERS; ++uiIndex)
	{
		VRCQB_PLAN_SLOT *pSlot = &gVRCQBPlan[uiIndex];
		if (!pSlot->fValid)
			continue;

		if (uiTurnStamp > pSlot->uiTurnStamp + VRCQB_PLAN_MEMORY_TURNS)
			memset(pSlot, 0, sizeof(VRCQB_PLAN_SLOT));
	}
}

static VRCQB_PLAN_SLOT *VRCQBPlanSlot(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->ubID >= TOTAL_SOLDIERS)
		return NULL;

	VRCQBMantainPlanState();

	VRCQB_PLAN_SLOT *pSlot = &gVRCQBPlan[pSoldier->ubID];
	if (pSlot->fValid && pSlot->uiUniqueSoldierId != pSoldier->uiUniqueSoldierIdValue)
		memset(pSlot, 0, sizeof(VRCQB_PLAN_SLOT));

	return pSlot;
}

static VRCQB_STATE VRCQBPreviousState(SOLDIERTYPE *pSoldier)
{
	VRCQB_PLAN_SLOT *pSlot = VRCQBPlanSlot(pSoldier);
	if (!pSlot || !pSlot->fValid)
		return VRCQB_STATE_NONE;

	return pSlot->eState;
}

static void VRCQBRememberAssessment(SOLDIERTYPE *pSoldier, const VRCQB_CONTEXT *pContext,
	const VRCQB_ASSESSMENT *pAssessment)
{
	if (!pSoldier || !pContext || !pAssessment)
		return;

	VRCQB_PLAN_SLOT *pSlot = VRCQBPlanSlot(pSoldier);
	if (!pSlot)
		return;

	pSlot->fValid = TRUE;
	pSlot->uiUniqueSoldierId = pSoldier->uiUniqueSoldierIdValue;
	pSlot->uiTurnStamp = VRCQBCurrentTurnStamp();
	pSlot->eState = pAssessment->eState;
	pSlot->eRole = pAssessment->eRole;
	pSlot->usRoomNo = pContext->usRoomNo;
	pSlot->sEntryGridNo = pAssessment->sEntryGridNo;
	pSlot->sTargetGridNo = pAssessment->sTargetGridNo;
}

// CQB/building doctrine is live on the canonical integration branch. Runtime
// callers still sit behind normal survival, disengagement, casualty, suppression
// and attack-priority gates in DecideAction. VERIFY_AI_INTEGRITY.ps1 enforces
// those RED/BLACK ordering and telemetry contracts on the canonical branch.
BOOLEAN VRCQB_IsRuntimeEnabled(void)
{
	return TRUE;
}

VRCQB_TRAINING_PROFILE VRCQB_GetTrainingProfile(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier)
		return VRCQB_TRAINING_SECURITY_BASIC;

	if (pSoldier->bTeam == ENEMY_TEAM)
	{
		switch (AIGetDoctrineProfile(pSoldier))
		{
		case AI_DOCTRINE_SECURITY:
			return VRCQB_TRAINING_SECURITY_BASIC;
		case AI_DOCTRINE_LINE:
			return AIHasLocalCommandSupport(pSoldier) ?
				VRCQB_TRAINING_LINE_COMMANDED : VRCQB_TRAINING_LINE_BASIC;
		case AI_DOCTRINE_VETERAN:
			return VRCQB_TRAINING_VETERAN;
		case AI_DOCTRINE_ELITE_MOBILE:
			return VRCQB_TRAINING_ELITE_MOBILE;
		case AI_DOCTRINE_ELITE_GUARD:
			return VRCQB_TRAINING_ELITE_GUARD;
		default:
			return VRCQB_TRAINING_LINE_BASIC;
		}
	}

	switch (AICompetenceTier(pSoldier))
	{
	case AI_COMPETENCE_BASIC:
		return VRCQB_TRAINING_SECURITY_BASIC;
	case AI_COMPETENCE_ELITE:
		return VRCQB_TRAINING_VETERAN;
	default:
		return VRCQB_TRAINING_LINE_BASIC;
	}
}

BOOLEAN VRCQB_GetTrainingModel(SOLDIERTYPE *pSoldier, VRCQB_TRAINING_MODEL *pModel)
{
	if (!pModel)
		return FALSE;

	memset(pModel, 0, sizeof(VRCQB_TRAINING_MODEL));
	pModel->eProfile = VRCQB_GetTrainingProfile(pSoldier);

	switch (pModel->eProfile)
	{
	case VRCQB_TRAINING_SECURITY_BASIC:
		pModel->uiCapabilities = VRCQB_CAP_FATAL_FUNNEL_AWARE;
		pModel->ubAssaultSkill = 28;
		pModel->ubHoldSkill = 52;
		pModel->ubSectorDiscipline = 32;
		pModel->ubReplanSkill = 24;
		pModel->ubHesitationChance = 28;
		pModel->ubThresholdMistakeChance = 32;
		pModel->ubCoordinationBreakChance = 35;
		pModel->ubPreferredClearTeamSize = 1;
		pModel->ubMaxCoordinatedMovers = 1;
		break;

	case VRCQB_TRAINING_LINE_BASIC:
		pModel->uiCapabilities =
			VRCQB_CAP_FATAL_FUNNEL_AWARE |
			VRCQB_CAP_TWO_MAN_ENTRY |
			VRCQB_CAP_SECTOR_DECONFLICTION;
		pModel->ubAssaultSkill = 52;
		pModel->ubHoldSkill = 60;
		pModel->ubSectorDiscipline = 52;
		pModel->ubReplanSkill = 42;
		pModel->ubHesitationChance = 18;
		pModel->ubThresholdMistakeChance = 18;
		pModel->ubCoordinationBreakChance = 22;
		pModel->ubPreferredClearTeamSize = 2;
		pModel->ubMaxCoordinatedMovers = 1;
		break;

	case VRCQB_TRAINING_LINE_COMMANDED:
		pModel->uiCapabilities =
			VRCQB_CAP_FATAL_FUNNEL_AWARE |
			VRCQB_CAP_TWO_MAN_ENTRY |
			VRCQB_CAP_SECTOR_DECONFLICTION |
			VRCQB_CAP_REAR_SECURITY |
			VRCQB_CAP_PROACTIVE_SUPPORT |
			VRCQB_CAP_DEFENSE_IN_DEPTH;
		pModel->ubAssaultSkill = 66;
		pModel->ubHoldSkill = 70;
		pModel->ubSectorDiscipline = 68;
		pModel->ubReplanSkill = 58;
		pModel->ubHesitationChance = 12;
		pModel->ubThresholdMistakeChance = 11;
		pModel->ubCoordinationBreakChance = 14;
		pModel->ubPreferredClearTeamSize = 2;
		pModel->ubMaxCoordinatedMovers = 2;
		break;

	case VRCQB_TRAINING_VETERAN:
		pModel->uiCapabilities =
			VRCQB_CAP_FATAL_FUNNEL_AWARE |
			VRCQB_CAP_TWO_MAN_ENTRY |
			VRCQB_CAP_SECTOR_DECONFLICTION |
			VRCQB_CAP_REAR_SECURITY |
			VRCQB_CAP_ALTERNATE_ENTRY |
			VRCQB_CAP_DYNAMIC_REPLAN |
			VRCQB_CAP_PROACTIVE_SUPPORT |
			VRCQB_CAP_DEFENSE_IN_DEPTH |
			VRCQB_CAP_LOCAL_COUNTERATTACK;
		pModel->ubAssaultSkill = 82;
		pModel->ubHoldSkill = 82;
		pModel->ubSectorDiscipline = 84;
		pModel->ubReplanSkill = 80;
		pModel->ubHesitationChance = 7;
		pModel->ubThresholdMistakeChance = 6;
		pModel->ubCoordinationBreakChance = 8;
		pModel->ubPreferredClearTeamSize = 2;
		pModel->ubMaxCoordinatedMovers = 2;
		break;

	case VRCQB_TRAINING_ELITE_MOBILE:
		pModel->uiCapabilities =
			VRCQB_CAP_FATAL_FUNNEL_AWARE |
			VRCQB_CAP_TWO_MAN_ENTRY |
			VRCQB_CAP_SECTOR_DECONFLICTION |
			VRCQB_CAP_REAR_SECURITY |
			VRCQB_CAP_ALTERNATE_ENTRY |
			VRCQB_CAP_DYNAMIC_REPLAN |
			VRCQB_CAP_PROACTIVE_SUPPORT |
			VRCQB_CAP_DEFENSE_IN_DEPTH |
			VRCQB_CAP_LOCAL_COUNTERATTACK |
			VRCQB_CAP_COMPLEX_ROOM_FLOW;
		pModel->ubAssaultSkill = 94;
		pModel->ubHoldSkill = 84;
		pModel->ubSectorDiscipline = 94;
		pModel->ubReplanSkill = 94;
		pModel->ubHesitationChance = 3;
		pModel->ubThresholdMistakeChance = 2;
		pModel->ubCoordinationBreakChance = 4;
		pModel->ubPreferredClearTeamSize = 2;
		pModel->ubMaxCoordinatedMovers = 3;
		break;

	case VRCQB_TRAINING_ELITE_GUARD:
		pModel->uiCapabilities =
			VRCQB_CAP_FATAL_FUNNEL_AWARE |
			VRCQB_CAP_TWO_MAN_ENTRY |
			VRCQB_CAP_SECTOR_DECONFLICTION |
			VRCQB_CAP_REAR_SECURITY |
			VRCQB_CAP_DYNAMIC_REPLAN |
			VRCQB_CAP_PROACTIVE_SUPPORT |
			VRCQB_CAP_DEFENSE_IN_DEPTH |
			VRCQB_CAP_LOCAL_COUNTERATTACK;
		pModel->ubAssaultSkill = 78;
		pModel->ubHoldSkill = 96;
		pModel->ubSectorDiscipline = 95;
		pModel->ubReplanSkill = 88;
		pModel->ubHesitationChance = 4;
		pModel->ubThresholdMistakeChance = 3;
		pModel->ubCoordinationBreakChance = 5;
		pModel->ubPreferredClearTeamSize = 2;
		pModel->ubMaxCoordinatedMovers = 2;
		break;

	default:
		return FALSE;
	}

	return TRUE;
}

BOOLEAN VRCQB_HasCapability(const VRCQB_TRAINING_MODEL *pModel, UINT32 uiCapability)
{
	if (!pModel || uiCapability == VRCQB_CAP_NONE)
		return FALSE;

	return (pModel->uiCapabilities & uiCapability) == uiCapability;
}

static void VRCQBKnownThreatSummary(SOLDIERTYPE *pSoldier, VRCQB_CONTEXT *pContext)
{
	if (!pSoldier || !pContext)
		return;

	for (UINT16 uiID = 0; uiID < TOTAL_SOLDIERS; ++uiID)
	{
		SOLDIERTYPE *pOpponent = MercPtrs[uiID];
		if (!pOpponent || pOpponent == pSoldier)
			continue;

		const INT8 bKnowledge = Knowledge(pSoldier, (UINT8)uiID);
		if (bKnowledge == NOT_HEARD_OR_SEEN)
			continue;

		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			pSoldier->bSide == pOpponent->bSide)
		{
			continue;
		}

		const INT32 sKnownSpot = KnownLocation(pSoldier, (UINT8)uiID);
		const INT8 bKnownLevel = KnownLevel(pSoldier, (UINT8)uiID);
		if (TileIsOutOfBounds(sKnownSpot))
			continue;

		++pContext->ubKnownThreats;

		UINT16 usThreatRoom = NO_ROOM;
		const BOOLEAN fThreatInRoom = VRCQBGetRoom(sKnownSpot, &usThreatRoom);
		const UINT8 ubThreatBuilding = VRCQBGetBuildingId(sKnownSpot);

		if (fThreatInRoom)
			pContext->fKnownThreatIndoor = TRUE;

		if (pContext->fInsideRoom && fThreatInRoom &&
			usThreatRoom == pContext->usRoomNo &&
			bKnownLevel == pSoldier->pathing.bLevel)
		{
			pContext->fKnownThreatInSameRoom = TRUE;
		}

		if (pContext->ubBuildingID != NO_BUILDING &&
			ubThreatBuilding != NO_BUILDING &&
			pContext->ubBuildingID == ubThreatBuilding)
		{
			pContext->fKnownThreatInSameBuilding = TRUE;
		}
	}
}

static INT32 VRCQBScorePositionInternal(SOLDIERTYPE *pSoldier,
	const VRCQB_CONTEXT *pContext, const VRCQB_TRAINING_MODEL *pModel,
	VRCQB_STATE eState, VRCQB_ROLE eRole, INT32 sCandidateGridNo,
	BOOLEAN fDetailed);

static void VRCQBInsertTopCandidate(INT32 sGridNo, INT32 sAuxGridNo, INT32 iScore,
	INT32 *psGrid, INT32 *psAux, INT32 *piScore, UINT8 ubCount)
{
	for (UINT8 ubSlot = 0; ubSlot < ubCount; ++ubSlot)
	{
		if (iScore <= piScore[ubSlot])
			continue;

		for (INT8 bMove = (INT8)ubCount - 1; bMove > (INT8)ubSlot; --bMove)
		{
			piScore[bMove] = piScore[bMove - 1];
			psGrid[bMove] = psGrid[bMove - 1];
			psAux[bMove] = psAux[bMove - 1];
		}

		piScore[ubSlot] = iScore;
		psGrid[ubSlot] = sGridNo;
		psAux[ubSlot] = sAuxGridNo;
		return;
	}
}

static BOOLEAN VRCQBFindBestEntry(SOLDIERTYPE *pSoldier, VRCQB_CONTEXT *pContext,
	const VRCQB_TRAINING_MODEL *pModel, INT32 *psEntry, INT32 *psFoothold)
{
	if (!pSoldier || !pContext || !pModel || !psEntry || !psFoothold)
		return FALSE;

	*psEntry = NOWHERE;
	*psFoothold = NOWHERE;

	INT32 sSearchCenter = !TileIsOutOfBounds(pContext->sPrimaryKnownThreat) ?
		pContext->sPrimaryKnownThreat : pSoldier->sGridNo;

	const INT16 sCenterX = (INT16)(sSearchCenter % MAXCOL);
	const INT16 sCenterY = (INT16)(sSearchCenter / MAXCOL);

	const UINT8 ubTopCount = 4;
	INT32 sTopFoothold[ubTopCount] = { NOWHERE, NOWHERE, NOWHERE, NOWHERE };
	INT32 sTopEntry[ubTopCount] = { NOWHERE, NOWHERE, NOWHERE, NOWHERE };
	INT32 iTopScore[ubTopCount] =
		{ VRCQB_INVALID_SCORE, VRCQB_INVALID_SCORE, VRCQB_INVALID_SCORE, VRCQB_INVALID_SCORE };

	for (INT16 sYOffset = -VRCQB_ENTRY_SEARCH_RADIUS;
		sYOffset <= VRCQB_ENTRY_SEARCH_RADIUS; ++sYOffset)
	{
		if (AIPlanningHardBudgetExceeded(pSoldier))
			break;
		const INT16 sY = sCenterY + sYOffset;
		if (sY < 0 || sY >= MAXROW)
			continue;

		for (INT16 sXOffset = -VRCQB_ENTRY_SEARCH_RADIUS;
			sXOffset <= VRCQB_ENTRY_SEARCH_RADIUS; ++sXOffset)
		{
			const INT16 sX = sCenterX + sXOffset;
			if (sX < 0 || sX >= MAXCOL)
				continue;

			const INT32 sDoor = sY * MAXCOL + sX;
			if (!CheckDoorAtGridno((UINT32)sDoor))
				continue;

			const INT32 iDoorDistance = PythSpacesAway(pSoldier->sGridNo, sDoor);

			for (UINT8 ubDirection = 0; ubDirection < NUM_WORLD_DIRECTIONS; ++ubDirection)
			{
				const INT32 sInside = NewGridNo(sDoor, DirectionInc(ubDirection));
				if (sInside == sDoor || !VRCQBGridAvailable(pSoldier, sInside))
					continue;

				UINT16 usCandidateRoom = NO_ROOM;
				if (!VRCQBGetRoom(sInside, &usCandidateRoom))
					continue;

				const UINT8 ubCandidateBuilding = VRCQBGetBuildingId(sInside);
				if (pContext->ubThreatBuildingID != NO_BUILDING &&
					ubCandidateBuilding != pContext->ubThreatBuildingID)
					continue;

				if (pContext->fInsideRoom &&
					usCandidateRoom == pContext->usRoomNo &&
					!pContext->fKnownThreatInSameRoom)
					continue;

				INT32 iScore = VRCQBScorePositionInternal(
					pSoldier, pContext, pModel, VRCQB_STATE_ASSAULT,
					VRCQB_ROLE_POINT, sInside, FALSE);

				iScore -= iDoorDistance * 2;
				if (!VRCQB_HasCapability(pModel, VRCQB_CAP_ALTERNATE_ENTRY))
					iScore -= iDoorDistance * 4;

				VRCQBInsertTopCandidate(
					sInside, sDoor, iScore,
					sTopFoothold, sTopEntry, iTopScore, ubTopCount);
			}
		}
	}

	INT32 iBestDetailed = VRCQB_INVALID_SCORE;
	for (UINT8 ubSlot = 0; ubSlot < ubTopCount; ++ubSlot)
	{
		if (AIPlanningHardBudgetExceeded(pSoldier))
			break;
		if (TileIsOutOfBounds(sTopFoothold[ubSlot]))
			continue;

		INT32 iDetailed = VRCQBScorePositionInternal(
			pSoldier, pContext, pModel, VRCQB_STATE_ASSAULT,
			VRCQB_ROLE_POINT, sTopFoothold[ubSlot], TRUE);

		iDetailed -= PythSpacesAway(pSoldier->sGridNo, sTopEntry[ubSlot]) * 2;
		if (!VRCQB_HasCapability(pModel, VRCQB_CAP_ALTERNATE_ENTRY))
			iDetailed -= PythSpacesAway(pSoldier->sGridNo, sTopEntry[ubSlot]) * 4;

		if (iDetailed > iBestDetailed)
		{
			iBestDetailed = iDetailed;
			*psEntry = sTopEntry[ubSlot];
			*psFoothold = sTopFoothold[ubSlot];
		}
	}

	return !TileIsOutOfBounds(*psFoothold);
}

static INT32 VRCQBFindBestLocalPosition(SOLDIERTYPE *pSoldier,
	const VRCQB_CONTEXT *pContext, const VRCQB_TRAINING_MODEL *pModel,
	VRCQB_STATE eState, VRCQB_ROLE eRole)
{
	if (!pSoldier || !pContext || !pModel)
		return NOWHERE;

	const INT16 sCenterX = (INT16)(pSoldier->sGridNo % MAXCOL);
	const INT16 sCenterY = (INT16)(pSoldier->sGridNo / MAXCOL);

	const UINT8 ubTopCount = 3;
	INT32 sTopSpot[ubTopCount] = { NOWHERE, NOWHERE, NOWHERE };
	INT32 sUnused[ubTopCount] = { NOWHERE, NOWHERE, NOWHERE };
	INT32 iTopScore[ubTopCount] =
		{ VRCQB_INVALID_SCORE, VRCQB_INVALID_SCORE, VRCQB_INVALID_SCORE };

	for (INT16 sYOffset = -VRCQB_LOCAL_SEARCH_RADIUS;
		sYOffset <= VRCQB_LOCAL_SEARCH_RADIUS; ++sYOffset)
	{
		if (AIPlanningHardBudgetExceeded(pSoldier))
			break;
		const INT16 sY = sCenterY + sYOffset;
		if (sY < 0 || sY >= MAXROW)
			continue;

		for (INT16 sXOffset = -VRCQB_LOCAL_SEARCH_RADIUS;
			sXOffset <= VRCQB_LOCAL_SEARCH_RADIUS; ++sXOffset)
		{
			const INT16 sX = sCenterX + sXOffset;
			if (sX < 0 || sX >= MAXCOL)
				continue;

			const INT32 sCandidate = sY * MAXCOL + sX;
			if (!VRCQBGridAvailable(pSoldier, sCandidate))
				continue;

			if ((pContext->fInsideRoom || pContext->ubBuildingID != NO_BUILDING) &&
				!VRCQBSameStructure(pSoldier->sGridNo, sCandidate))
				continue;

			const INT32 iScore = VRCQBScorePositionInternal(
				pSoldier, pContext, pModel, eState, eRole, sCandidate, FALSE);

			VRCQBInsertTopCandidate(
				sCandidate, NOWHERE, iScore,
				sTopSpot, sUnused, iTopScore, ubTopCount);
		}
	}

	INT32 sBestSpot = NOWHERE;
	INT32 iBestDetailed = VRCQB_INVALID_SCORE;
	for (UINT8 ubSlot = 0; ubSlot < ubTopCount; ++ubSlot)
	{
		if (AIPlanningHardBudgetExceeded(pSoldier))
			break;
		if (TileIsOutOfBounds(sTopSpot[ubSlot]))
			continue;

		const INT32 iDetailed = VRCQBScorePositionInternal(
			pSoldier, pContext, pModel, eState, eRole, sTopSpot[ubSlot], TRUE);

		if (iDetailed > iBestDetailed)
		{
			iBestDetailed = iDetailed;
			sBestSpot = sTopSpot[ubSlot];
		}
	}

	return sBestSpot;
}

BOOLEAN VRCQB_BuildContext(SOLDIERTYPE *pSoldier, VRCQB_CONTEXT *pContext)
{
	if (!pSoldier || !pContext)
		return FALSE;

	memset(pContext, 0, sizeof(VRCQB_CONTEXT));
	pContext->sPrimaryKnownThreat = NOWHERE;
	pContext->sMemoryThreat = NOWHERE;
	pContext->bMemoryThreatLevel = pSoldier->pathing.bLevel;
	pContext->sPreferredEntry = NOWHERE;
	pContext->sPreferredFoothold = NOWHERE;
	pContext->sPreferredFallback = NOWHERE;
	pContext->usRoomNo = NO_ROOM;
	pContext->usThreatRoomNo = NO_ROOM;
	pContext->ubBuildingID = NO_BUILDING;
	pContext->ubThreatBuildingID = NO_BUILDING;
	pContext->bPrimaryKnownThreatLevel = pSoldier->pathing.bLevel;

	pContext->fInsideRoom = VRCQBGetRoom(pSoldier->sGridNo, &pContext->usRoomNo);
	pContext->fOnRoof = pSoldier->pathing.bLevel > 0;
	pContext->fUnderFire = pSoldier->aiData.bUnderFire;
	pContext->fNearEntry = CheckDoorNearGridno((UINT32)pSoldier->sGridNo);
	pContext->ubBuildingID = VRCQBGetBuildingId(pSoldier->sGridNo);
	pContext->ubLocalFriends = AICountNearbyOperationalFriends(
		pSoldier, pSoldier->sGridNo, __max(4, TACTICAL_RANGE / 3));
	pContext->fHasLocalSupport = pContext->ubLocalFriends > 0;

	pContext->sPrimaryKnownThreat = ClosestKnownOpponent(
		pSoldier, NULL, &pContext->bPrimaryKnownThreatLevel);

	if (!TileIsOutOfBounds(pContext->sPrimaryKnownThreat))
	{
		pContext->fKnownThreatIndoor = VRCQBGetRoom(
			pContext->sPrimaryKnownThreat, &pContext->usThreatRoomNo);
		pContext->ubThreatBuildingID =
			VRCQBGetBuildingId(pContext->sPrimaryKnownThreat);
	}

	VRCQBKnownThreatSummary(pSoldier, pContext);

	// Once exact JA2 knowledge decays, retain only a weaker building-search
	// hypothesis. It can sustain SECURE/HOLD behaviour but never authorize an
	// assault, attack or counterattack against an unseen remembered opponent.
	AITHREATMEMORYCUE MemoryCue;
	if (AIBuildThreatMemoryCue(pSoldier, &MemoryCue) &&
		MemoryCue.ubConfidence >= 30 &&
		!TileIsOutOfBounds(MemoryCue.sGridNo))
	{
		pContext->sMemoryThreat = MemoryCue.sGridNo;
		pContext->bMemoryThreatLevel = MemoryCue.bLevel;
		pContext->ubMemoryThreatConfidence = MemoryCue.ubConfidence;

		UINT16 usMemoryRoom = NO_ROOM;
		const BOOLEAN fMemoryIndoor =
			VRCQBGetRoom(MemoryCue.sGridNo, &usMemoryRoom);
		const UINT8 ubMemoryBuilding =
			VRCQBGetBuildingId(MemoryCue.sGridNo);

		pContext->fMemoryThreatInSameRoom =
			pContext->fInsideRoom &&
			fMemoryIndoor &&
			MemoryCue.bLevel == pSoldier->pathing.bLevel &&
			usMemoryRoom == pContext->usRoomNo;

		pContext->fMemoryThreatInSameBuilding =
			pContext->ubBuildingID != NO_BUILDING &&
			ubMemoryBuilding != NO_BUILDING &&
			ubMemoryBuilding == pContext->ubBuildingID;
	}

	const UINT16 usCurrentExposure = AIKnownThreatExposure(
		pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
	pContext->fEntryExposed =
		pContext->fNearEntry &&
		(usCurrentExposure >= 35 || !AnyCoverAtSpot(pSoldier, pSoldier->sGridNo));

	VRCQB_TRAINING_MODEL Model;
	if (VRCQB_GetTrainingModel(pSoldier, &Model))
	{
		VRCQBFindBestEntry(pSoldier, pContext, &Model,
			&pContext->sPreferredEntry, &pContext->sPreferredFoothold);

		pContext->sPreferredFallback = VRCQBFindBestLocalPosition(
			pSoldier, pContext, &Model,
			VRCQB_STATE_DELAY_FALLBACK, VRCQB_ROLE_HOLD);
		pContext->fFallbackAvailable =
			!TileIsOutOfBounds(pContext->sPreferredFallback) &&
			pContext->sPreferredFallback != pSoldier->sGridNo;
	}

	pContext->fValid =
		pContext->fInsideRoom ||
		pContext->ubBuildingID != NO_BUILDING ||
		pContext->fKnownThreatIndoor ||
		pContext->fNearEntry;

	return pContext->fValid;
}

static INT32 VRCQBScorePositionInternal(SOLDIERTYPE *pSoldier, const VRCQB_CONTEXT *pContext,
	const VRCQB_TRAINING_MODEL *pModel, VRCQB_STATE eState, VRCQB_ROLE eRole,
	INT32 sCandidateGridNo, BOOLEAN fDetailed)
{
	if (!pSoldier || !pContext || !pModel ||
		!VRCQBGridAvailable(pSoldier, sCandidateGridNo))
	{
		return VRCQB_INVALID_SCORE;
	}

	INT32 iScore = 0;

	if (AnyCoverAtSpot(pSoldier, sCandidateGridNo))
		iScore += 32;

	if (fDetailed)
	{
		const UINT16 usExposure = AIKnownThreatExposure(
			pSoldier, sCandidateGridNo, pSoldier->pathing.bLevel);
		iScore -= __min((INT32)80, (INT32)usExposure / 2);
	}
	else if (!AnyCoverAtSpot(pSoldier, sCandidateGridNo))
	{
		// Cheap first-pass proxy. Detailed known-threat exposure is deliberately
		// deferred until only the strongest candidates remain.
		iScore -= 10;
	}

	const UINT8 ubCloseFriends = VRCQBCountFriendsNearSpot(
		pSoldier, sCandidateGridNo, 1);
	const INT32 iCrowdingPenalty =
		6 + pModel->ubSectorDiscipline / 5;
	iScore -= ubCloseFriends * iCrowdingPenalty;

	const UINT8 ubSupportFriends = VRCQBCountFriendsNearSpot(
		pSoldier, sCandidateGridNo, __max(3, TACTICAL_RANGE / 4));
	iScore += __min((INT32)18, (INT32)ubSupportFriends * 5);

	const BOOLEAN fDoorTile = CheckDoorAtGridno((UINT32)sCandidateGridNo);
	const BOOLEAN fNearDoor = CheckDoorNearGridno((UINT32)sCandidateGridNo);

	const INT32 iThresholdDiscipline = VRCQBClamp(
		(INT32)AIPlannerReliability(pSoldier) -
		(INT32)pModel->ubThresholdMistakeChance, 0, 100);

	if (fDoorTile)
	{
		// Awareness is broad, mastery is not. Basic troops know a doorway is bad,
		// but stress/training quality determine how strongly the planner avoids it.
		const INT32 iDoorPenalty =
			VRCQB_HasCapability(pModel, VRCQB_CAP_FATAL_FUNNEL_AWARE) ?
			35 + (55 * iThresholdDiscipline) / 100 :
			20 + (25 * iThresholdDiscipline) / 100;
		iScore -= iDoorPenalty;
	}
	else if (fNearDoor)
	{
		INT32 iNearDoorPenalty = 4 + (12 * iThresholdDiscipline) / 100;
		if (eState == VRCQB_STATE_ASSAULT && eRole == VRCQB_ROLE_POINT)
			iNearDoorPenalty /= 2;
		else if (eRole == VRCQB_ROLE_COVER)
			iNearDoorPenalty = __max((INT32)2, iNearDoorPenalty / 2);

		iScore -= iNearDoorPenalty;
	}

	if (!TileIsOutOfBounds(pContext->sPrimaryKnownThreat))
	{
		const INT32 iThreatDistance =
			PythSpacesAway(sCandidateGridNo, pContext->sPrimaryKnownThreat);

		if (eState == VRCQB_STATE_DELAY_FALLBACK)
			iScore += __min((INT32)42, iThreatDistance * 4);
		else if (eState == VRCQB_STATE_ASSAULT)
			iScore += __max((INT32)0, 28 - abs(iThreatDistance - 4) * 4);
		else if (eState == VRCQB_STATE_HOLD || eState == VRCQB_STATE_SECURE)
			iScore += __min((INT32)20, iThreatDistance * 2);

		if (fDetailed &&
			VRCQB_HasCapability(pModel, VRCQB_CAP_SECTOR_DECONFLICTION))
		{
			const INT32 iCrossfire = AICrossfirePositionScore(
				pSoldier, sCandidateGridNo, pContext->sPrimaryKnownThreat);
			iScore += VRCQBClamp(iCrossfire / 5, -10, 20);
		}
	}

	else if (!TileIsOutOfBounds(pContext->sMemoryThreat) &&
		(pContext->fMemoryThreatInSameRoom ||
		 pContext->fMemoryThreatInSameBuilding) &&
		(eState == VRCQB_STATE_HOLD ||
		 eState == VRCQB_STATE_SECURE))
	{
		const INT32 iMemoryDistance =
			PythSpacesAway(sCandidateGridNo, pContext->sMemoryThreat);

		// Secure/search positions observe the remembered area from a short
		// standoff rather than marching onto the old exact tile.
		iScore += __max((INT32)0,
			22 - 4 * abs(iMemoryDistance - 4));

		if (fDetailed &&
			LocationToLocationLineOfSightTest(
				sCandidateGridNo, pSoldier->pathing.bLevel,
				pContext->sMemoryThreat, pContext->bMemoryThreatLevel,
				TRUE, CALC_FROM_ALL_DIRS))
		{
			iScore += 12;
		}

		if (iMemoryDistance <= 1)
			iScore -= 24;
	}

	if ((eState == VRCQB_STATE_HOLD ||
		 eState == VRCQB_STATE_SECURE ||
		 eState == VRCQB_STATE_DELAY_FALLBACK) &&
		VRCQBSameStructure(pSoldier->sGridNo, sCandidateGridNo))
	{
		iScore += 10;
	}

	if (eState == VRCQB_STATE_DELAY_FALLBACK && fNearDoor)
		iScore -= 18;

	if (eState == VRCQB_STATE_HOLD &&
		VRCQB_HasCapability(pModel, VRCQB_CAP_DEFENSE_IN_DEPTH) &&
		!fNearDoor)
	{
		iScore += 8;
	}

	if (fDetailed)
	{
		AITACTICALGEOMETRY Geometry;
		if (AIBuildTacticalGeometry(
			pSoldier, pSoldier->sGridNo, &Geometry))
		{
			INT8 bIntent = AI_INTENT_HOLD;
			INT8 bRole = AI_ROLE_SUPPORT;

			if (eState == VRCQB_STATE_ASSAULT ||
				eState == VRCQB_STATE_COUNTERATTACK)
				bIntent = AI_INTENT_PRESS;
			else if (eState == VRCQB_STATE_DELAY_FALLBACK)
				bIntent = AI_INTENT_FALLBACK;

			switch (eRole)
			{
			case VRCQB_ROLE_POINT:
				bRole = AI_ROLE_MANEUVER;
				break;
			case VRCQB_ROLE_SECURITY:
			case VRCQB_ROLE_HOLD:
				bRole = AI_ROLE_SCREEN;
				break;
			case VRCQB_ROLE_RESERVE:
				bRole = AI_ROLE_RESERVE;
				break;
			default:
				bRole = AI_ROLE_SUPPORT;
				break;
			}

			const INT32 sGeometryThreat =
				!TileIsOutOfBounds(pContext->sPrimaryKnownThreat) ?
				pContext->sPrimaryKnownThreat :
				pContext->sMemoryThreat;

			const INT32 iGeometry = AIGeometryPositionScore(
				pSoldier, &Geometry, sCandidateGridNo,
				sGeometryThreat, bIntent, bRole);

			// CQB has additional doorway/room geometry of its own, so shared
			// battlefield geometry is influential but not allowed to dominate it.
			iScore += (3 * iGeometry) / 4;
		}
	}

	iScore -= PythSpacesAway(pSoldier->sGridNo, sCandidateGridNo);

	// Preserve believable imperfection using the existing deterministic competence
	// noise rather than a new random source.
	iScore += AICompetenceUtilityNoise(
		pSoldier, sCandidateGridNo,
		7100u + (UINT32)eState * 31u + (UINT32)eRole * 7u);

	return iScore;
}


INT32 VRCQB_ScorePosition(SOLDIERTYPE *pSoldier, const VRCQB_CONTEXT *pContext,
	const VRCQB_TRAINING_MODEL *pModel, VRCQB_STATE eState, VRCQB_ROLE eRole,
	INT32 sCandidateGridNo)
{
	return VRCQBScorePositionInternal(
		pSoldier, pContext, pModel, eState, eRole, sCandidateGridNo, TRUE);
}

static BOOLEAN VRCQBShouldCounterattack(SOLDIERTYPE *pSoldier,
	const VRCQB_CONTEXT *pContext, const VRCQB_TRAINING_MODEL *pModel)
{
	if (!pSoldier || !pContext || !pModel ||
		!VRCQB_HasCapability(pModel, VRCQB_CAP_LOCAL_COUNTERATTACK) ||
		!pContext->fKnownThreatInSameRoom)
	{
		return FALSE;
	}

	const INT8 bBattle = AIBattleSituation(pSoldier);
	if (bBattle == AI_BATTLE_LOSING || bBattle == AI_BATTLE_CATASTROPHIC)
		return FALSE;

	if (AIPersonalRisk(pSoldier) > AIPersonalRiskTolerance(pSoldier) + 10)
		return FALSE;

	const VRCQB_STATE ePrevious = VRCQBPreviousState(pSoldier);
	return ePrevious == VRCQB_STATE_HOLD ||
		ePrevious == VRCQB_STATE_SECURE ||
		ePrevious == VRCQB_STATE_COUNTERATTACK;
}

static VRCQB_ROLE VRCQBSelectRole(SOLDIERTYPE *pSoldier,
	const VRCQB_CONTEXT *pContext, const VRCQB_TRAINING_MODEL *pModel,
	VRCQB_STATE eState)
{
	if (!pSoldier || !pContext || !pModel)
		return VRCQB_ROLE_NONE;

	switch (eState)
	{
	case VRCQB_STATE_ASSAULT:
		if (pModel->eProfile == VRCQB_TRAINING_SECURITY_BASIC)
			return VRCQB_ROLE_COVER;

		if (pContext->fHasLocalSupport &&
			AISupportRoleScore(pSoldier, pContext->sPrimaryKnownThreat) >
			AIManeuverRoleScore(pSoldier, pContext->sPrimaryKnownThreat) + 8)
		{
			return VRCQB_ROLE_SUPPORT;
		}
		return VRCQB_ROLE_POINT;

	case VRCQB_STATE_HOLD:
		if (VRCQB_HasCapability(pModel, VRCQB_CAP_REAR_SECURITY) &&
			pContext->fNearEntry && pContext->ubLocalFriends >= 2)
			return VRCQB_ROLE_SECURITY;
		return VRCQB_ROLE_HOLD;

	case VRCQB_STATE_SECURE:
		if (VRCQB_HasCapability(pModel, VRCQB_CAP_REAR_SECURITY) &&
			pContext->ubLocalFriends > 0)
			return VRCQB_ROLE_SECURITY;
		return VRCQB_ROLE_COVER;

	case VRCQB_STATE_DELAY_FALLBACK:
		if (pContext->fHasLocalSupport &&
			AISupportRoleScore(pSoldier, pContext->sPrimaryKnownThreat) >
			AIManeuverRoleScore(pSoldier, pContext->sPrimaryKnownThreat))
			return VRCQB_ROLE_COVER;
		return VRCQB_ROLE_HOLD;

	case VRCQB_STATE_COUNTERATTACK:
		return VRCQB_ROLE_POINT;

	default:
		return VRCQB_ROLE_NONE;
	}
}

BOOLEAN VRCQB_Assess(SOLDIERTYPE *pSoldier, const VRCQB_CONTEXT *pContext,
	VRCQB_ASSESSMENT *pAssessment)
{
	if (!pSoldier || !pContext || !pAssessment || !pContext->fValid)
		return FALSE;

	memset(pAssessment, 0, sizeof(VRCQB_ASSESSMENT));
	pAssessment->eState = VRCQB_STATE_NONE;
	pAssessment->eRole = VRCQB_ROLE_NONE;
	pAssessment->eReason = VRCQB_REASON_NONE;
	pAssessment->sAnchorGridNo = pSoldier->sInitialGridNo;
	pAssessment->sEntryGridNo = pContext->sPreferredEntry;
	pAssessment->sTargetGridNo = pSoldier->sGridNo;
	pAssessment->sFallbackGridNo = pContext->sPreferredFallback;

	VRCQB_TRAINING_MODEL Model;
	if (!VRCQB_GetTrainingModel(pSoldier, &Model))
		return FALSE;

	pAssessment->eTrainingProfile = Model.eProfile;
	pAssessment->uiCapabilities = Model.uiCapabilities;
	pAssessment->ubPlannerReliability = AIPlannerReliability(pSoldier);
	pAssessment->ubMaxCoordinatedMovers = Model.ubMaxCoordinatedMovers;
	pAssessment->iRiskScore = AIPersonalRisk(pSoldier);

	const UINT8 ubAssaultReadiness = VRCQBClampU8(
		((INT32)Model.ubAssaultSkill + (INT32)pAssessment->ubPlannerReliability) / 2 -
		AILocalStress(pSoldier) / 5 -
		Model.ubHesitationChance / 3 -
		Model.ubCoordinationBreakChance / 5);

	if (AIDisengagementActive(pSoldier) || AIShouldAvoidAdvance(pSoldier))
	{
		pAssessment->eState = VRCQB_STATE_DELAY_FALLBACK;
		pAssessment->eReason = VRCQB_REASON_DISENGAGEMENT;
	}
	else if (pContext->fFallbackAvailable &&
		(pContext->fUnderFire || AILocalStress(pSoldier) >= 45) &&
		pAssessment->iRiskScore >= AIPersonalRiskTolerance(pSoldier) + 10)
	{
		pAssessment->eState = VRCQB_STATE_DELAY_FALLBACK;
		pAssessment->eReason = VRCQB_REASON_HIGH_RISK_FALLBACK;
	}
	else if (VRCQBShouldCounterattack(pSoldier, pContext, &Model))
	{
		pAssessment->eState = VRCQB_STATE_COUNTERATTACK;
		pAssessment->eReason = VRCQB_REASON_LOCAL_COUNTERATTACK;
	}
	else if (VRCQBIsAnchoredOrder(pSoldier))
	{
		pAssessment->eState = VRCQB_STATE_HOLD;
		pAssessment->eReason = VRCQB_REASON_ANCHORED_HOLD;
	}
	else if (pContext->fKnownThreatInSameRoom)
	{
		pAssessment->eState = VRCQB_STATE_HOLD;
		pAssessment->eReason = VRCQB_REASON_THREAT_IN_ROOM;
	}
	else if (pContext->fKnownThreatInSameBuilding)
	{
		if (Model.eProfile == VRCQB_TRAINING_ELITE_GUARD)
		{
			pAssessment->eState = VRCQB_STATE_HOLD;
			pAssessment->eReason = VRCQB_REASON_THREAT_IN_BUILDING;
		}
		else if (Model.eProfile == VRCQB_TRAINING_SECURITY_BASIC)
		{
			pAssessment->eState = VRCQB_STATE_HOLD;
			pAssessment->eReason = VRCQB_REASON_SECURITY_REFUSES_COMPLEX_ASSAULT;
		}
		else if (!pContext->fHasLocalSupport &&
			(Model.eProfile == VRCQB_TRAINING_LINE_BASIC ||
			 Model.eProfile == VRCQB_TRAINING_LINE_COMMANDED))
		{
			pAssessment->eState = VRCQB_STATE_HOLD;
			pAssessment->eReason = VRCQB_REASON_INSUFFICIENT_ENTRY_SUPPORT;
		}
		else if (ubAssaultReadiness < 40)
		{
			pAssessment->eState = VRCQB_STATE_HOLD;
			pAssessment->eReason = VRCQB_REASON_ASSAULT_HESITATION;
		}
		else
		{
			pAssessment->eState = VRCQB_STATE_ASSAULT;
			pAssessment->eReason = VRCQB_REASON_THREAT_IN_BUILDING;
		}
	}
	else if (pContext->fKnownThreatIndoor &&
		!TileIsOutOfBounds(pContext->sPreferredFoothold))
	{
		if (Model.eProfile == VRCQB_TRAINING_SECURITY_BASIC)
		{
			pAssessment->eState = VRCQB_STATE_HOLD;
			pAssessment->eReason = VRCQB_REASON_SECURITY_REFUSES_COMPLEX_ASSAULT;
		}
		else if (!pContext->fHasLocalSupport &&
			(Model.eProfile == VRCQB_TRAINING_LINE_BASIC ||
			 Model.eProfile == VRCQB_TRAINING_LINE_COMMANDED))
		{
			pAssessment->eState = VRCQB_STATE_HOLD;
			pAssessment->eReason = VRCQB_REASON_INSUFFICIENT_ENTRY_SUPPORT;
		}
		else if (ubAssaultReadiness < 40)
		{
			pAssessment->eState = VRCQB_STATE_HOLD;
			pAssessment->eReason = VRCQB_REASON_ASSAULT_HESITATION;
		}
		else
		{
			pAssessment->eState = VRCQB_STATE_ASSAULT;
			pAssessment->eReason = VRCQB_REASON_APPROACH_INDOOR_THREAT;
		}
	}
	else if ((pContext->fMemoryThreatInSameRoom ||
			  pContext->fMemoryThreatInSameBuilding) &&
			 pContext->ubMemoryThreatConfidence >= 30)
	{
		pAssessment->eState = VRCQB_STATE_SECURE;
		pAssessment->eReason = VRCQB_REASON_UNRESOLVED_CONTACT_MEMORY;
	}
	else if (pContext->fInsideRoom || pContext->ubBuildingID != NO_BUILDING)
	{
		const VRCQB_STATE ePrevious = VRCQBPreviousState(pSoldier);
		if (ePrevious == VRCQB_STATE_ASSAULT ||
			ePrevious == VRCQB_STATE_COUNTERATTACK ||
			pContext->ubKnownThreats > 0)
		{
			pAssessment->eState = VRCQB_STATE_SECURE;
			pAssessment->eReason = VRCQB_REASON_SECURE_FOOTHOLD;
		}
		else
		{
			pAssessment->eState = VRCQB_STATE_HOLD;
			pAssessment->eReason = VRCQB_REASON_ANCHORED_HOLD;
		}
	}

	pAssessment->eRole = VRCQBSelectRole(
		pSoldier, pContext, &Model, pAssessment->eState);

	switch (pAssessment->eState)
	{
	case VRCQB_STATE_ASSAULT:
		if (!TileIsOutOfBounds(pContext->sPreferredFoothold))
			pAssessment->sTargetGridNo = pContext->sPreferredFoothold;
		break;

	case VRCQB_STATE_DELAY_FALLBACK:
		if (!TileIsOutOfBounds(pContext->sPreferredFallback))
			pAssessment->sTargetGridNo = pContext->sPreferredFallback;
		break;

	case VRCQB_STATE_HOLD:
	case VRCQB_STATE_SECURE:
	case VRCQB_STATE_COUNTERATTACK:
	{
		const INT32 sBestLocal = VRCQBFindBestLocalPosition(
			pSoldier, pContext, &Model,
			pAssessment->eState, pAssessment->eRole);
		if (!TileIsOutOfBounds(sBestLocal))
			pAssessment->sTargetGridNo = sBestLocal;
		break;
	}

	default:
		break;
	}

	pAssessment->usKnownThreatExposure = AIKnownThreatExposure(
		pSoldier, pAssessment->sTargetGridNo, pSoldier->pathing.bLevel);
	pAssessment->iPositionScore = VRCQB_ScorePosition(
		pSoldier, pContext, &Model,
		pAssessment->eState, pAssessment->eRole,
		pAssessment->sTargetGridNo);

	const UINT8 ubSkill =
		(pAssessment->eState == VRCQB_STATE_ASSAULT ||
		 pAssessment->eState == VRCQB_STATE_COUNTERATTACK) ?
		Model.ubAssaultSkill : Model.ubHoldSkill;

	INT32 iEffectiveSkill =
		((INT32)ubSkill + (INT32)pAssessment->ubPlannerReliability) / 2 -
		AILocalStress(pSoldier) / 5;

	if (pAssessment->eState == VRCQB_STATE_ASSAULT ||
		pAssessment->eState == VRCQB_STATE_COUNTERATTACK)
	{
		iEffectiveSkill -= Model.ubHesitationChance / 4;
		if (AILocalStress(pSoldier) >= 45)
			iEffectiveSkill -= Model.ubCoordinationBreakChance / 4;
	}

	pAssessment->ubEffectiveSkill = VRCQBClampU8(iEffectiveSkill);

	pAssessment->ubConfidence = VRCQBClampU8(
		((INT32)pAssessment->ubEffectiveSkill + (INT32)Model.ubReplanSkill) / 2 -
		(pContext->fEntryExposed ? Model.ubThresholdMistakeChance / 4 : 0));

	if (pAssessment->eReason == VRCQB_REASON_UNRESOLVED_CONTACT_MEMORY)
	{
		// Training determines execution quality, but weak memory cannot magically
		// become high-confidence knowledge merely because the soldier is elite.
		pAssessment->ubConfidence = (UINT8)__min(
			(INT32)pAssessment->ubConfidence,
			30 + (INT32)pContext->ubMemoryThreatConfidence / 2);
	}

	VRCQBRememberAssessment(pSoldier, pContext, pAssessment);
	return pAssessment->eState != VRCQB_STATE_NONE;
}


static UINT8 VRCQBCountCommittedMovers(SOLDIERTYPE *pSoldier,
	const VRCQB_ASSESSMENT *pAssessment)
{
	if (!pSoldier || !pAssessment)
		return 0;

	VRCQBMantainPlanState();

	UINT8 ubCount = 0;
	for (UINT8 ubID = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		ubID <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++ubID)
	{
		if (ubID == pSoldier->ubID || ubID >= TOTAL_SOLDIERS)
			continue;

		SOLDIERTYPE *pFriend = MercPtrs[ubID];
		if (!pFriend || !pFriend->bActive || !pFriend->bInSector ||
			pFriend->stats.bLife < OKLIFE || !AISameFireteam(pSoldier, pFriend))
		{
			continue;
		}

		VRCQB_PLAN_SLOT *pSlot = &gVRCQBPlan[ubID];
		if (!pSlot->fValid ||
			pSlot->uiUniqueSoldierId != pFriend->uiUniqueSoldierIdValue ||
			pSlot->uiTurnStamp != VRCQBCurrentTurnStamp())
		{
			continue;
		}

		if (pSlot->eState != VRCQB_STATE_ASSAULT &&
			pSlot->eState != VRCQB_STATE_COUNTERATTACK)
		{
			continue;
		}

		if (!TileIsOutOfBounds(pAssessment->sEntryGridNo) &&
			!TileIsOutOfBounds(pSlot->sEntryGridNo) &&
			pAssessment->sEntryGridNo != pSlot->sEntryGridNo)
		{
			continue;
		}

		++ubCount;
	}

	return ubCount;
}


static INT8 VRCQBTryProactiveEntrySmoke(SOLDIERTYPE *pSoldier,
	const VRCQB_CONTEXT *pContext, const VRCQB_ASSESSMENT *pAssessment,
	const VRCQB_TRAINING_MODEL *pModel, UINT32 uiDecision)
{
	if (!pSoldier || !pContext || !pAssessment || !pModel ||
		!VRCQB_HasCapability(pModel, VRCQB_CAP_PROACTIVE_SUPPORT) ||
		(pAssessment->eState != VRCQB_STATE_ASSAULT &&
		 pAssessment->eState != VRCQB_STATE_COUNTERATTACK) ||
		!pContext->fEntryExposed ||
		TileIsOutOfBounds(pContext->sPrimaryKnownThreat))
	{
		return AI_ACTION_NONE;
	}

	// Keep scarce smoke in reserve unless the element is already taking fire.
	if (AILocalSmokeReserve(pSoldier) <= 1 && !pSoldier->aiData.bUnderFire)
		return AI_ACTION_NONE;

	INT32 sSmokeGrid = !TileIsOutOfBounds(pContext->sPreferredEntry) ?
		pContext->sPreferredEntry : pContext->sPreferredFoothold;
	if (TileIsOutOfBounds(sSmokeGrid) ||
		Water(sSmokeGrid, pSoldier->pathing.bLevel) ||
		InSmokeNearby(sSmokeGrid, pSoldier->pathing.bLevel) ||
		AIRecentTossSaturation(pSoldier, sSmokeGrid, pSoldier->pathing.bLevel) > 0)
	{
		return AI_ACTION_NONE;
	}

	ATTACKTYPE BestThrow;
	memset(&BestThrow, 0, sizeof(BestThrow));
	CheckTossGrenadeAt(pSoldier, &BestThrow, sSmokeGrid,
		pSoldier->pathing.bLevel, EXPLOSV_SMOKE);
	if (!BestThrow.ubPossible)
		return AI_ACTION_NONE;

	// Entry smoke is a fireteam resource, not a private CQB side effect. Claim the
	// same local smoke task used by the rest of the tactical planner so two soldiers
	// do not independently spend smoke on the same threshold.
	if (!AIReserveTacticalTask(
		pSoldier, AI_TASK_SMOKE,
		sSmokeGrid, NOBODY, 1, 1))
	{
		return AI_ACTION_NONE;
	}

	// Smoke is the first step of the entry sequence. Preserve the CQB commitment
	// through a stance change/throw so the next decision can continue the entry plan.
	const INT32 sCQBPlanTarget =
		!TileIsOutOfBounds(pAssessment->sTargetGridNo) ?
		pAssessment->sTargetGridNo : sSmokeGrid;
	AIBeginShortPlan(
		pSoldier, AI_SHORT_PLAN_CQB,
		sCQBPlanTarget, NOBODY, 2);

	if (BestThrow.bWeaponIn != HANDPOS)
		RearrangePocket(pSoldier, HANDPOS, BestThrow.bWeaponIn, FOREVER);

	if (gAnimControl[pSoldier->usAnimState].ubEndHeight < BestThrow.ubStance &&
		pSoldier->InternalIsValidStance(
			AIDirection(pSoldier->sGridNo, BestThrow.sTarget), BestThrow.ubStance))
	{
		pSoldier->aiData.usActionData = BestThrow.ubStance;
		pSoldier->aiData.bNextAction = AI_ACTION_TOSS_PROJECTILE;
		pSoldier->aiData.usNextActionData = BestThrow.sTarget;
		pSoldier->aiData.bNextTargetLevel = BestThrow.bTargetLevel;
		pSoldier->aiData.bAimTime = BestThrow.ubAimTime;

		if (uiDecision)
		{
			VRAnalyticsStateInt(uiDecision, "selected_action", AI_ACTION_CHANGE_STANCE);
			VRAnalyticsStateInt(uiDecision, "cqb_support_action", EXPLOSV_SMOKE);
			VRAnalyticsCandidate(uiDecision, "cqb_entry_smoke", BestThrow.sTarget,
				pAssessment->iPositionScore, pAssessment->iPositionScore, true,
				"prepare proactive smoke for exposed CQB entry");
			VRAnalyticsCommitDecision(uiDecision, "cqb_entry_smoke", BestThrow.sTarget,
				pAssessment->iPositionScore, "prepare proactive smoke for exposed CQB entry");
		}
		return AI_ACTION_CHANGE_STANCE;
	}

	pSoldier->aiData.usActionData = BestThrow.sTarget;
	pSoldier->bTargetLevel = BestThrow.bTargetLevel;
	pSoldier->aiData.bAimTime = BestThrow.ubAimTime;

	if (uiDecision)
	{
		VRAnalyticsStateInt(uiDecision, "selected_action", AI_ACTION_TOSS_PROJECTILE);
		VRAnalyticsStateInt(uiDecision, "cqb_support_action", EXPLOSV_SMOKE);
		VRAnalyticsCandidate(uiDecision, "cqb_entry_smoke", BestThrow.sTarget,
			pAssessment->iPositionScore, pAssessment->iPositionScore, true,
			"proactive smoke for exposed CQB entry");
		VRAnalyticsCommitDecision(uiDecision, "cqb_entry_smoke", BestThrow.sTarget,
			pAssessment->iPositionScore, "proactive smoke for exposed CQB entry");
	}

	return AI_ACTION_TOSS_PROJECTILE;
}

static INT8 VRCQBTryAlternateWindowEntry(SOLDIERTYPE *pSoldier,
	const VRCQB_CONTEXT *pContext, const VRCQB_ASSESSMENT *pAssessment,
	const VRCQB_TRAINING_MODEL *pModel, INT32 sDesiredSpot, UINT32 uiDecision)
{
	if (!pSoldier || !pContext || !pAssessment || !pModel ||
		!VRCQB_HasCapability(pModel, VRCQB_CAP_ALTERNATE_ENTRY) ||
		(pAssessment->eState != VRCQB_STATE_ASSAULT &&
		 pAssessment->eState != VRCQB_STATE_COUNTERATTACK) ||
		pSoldier->pathing.bLevel != 0 ||
		TileIsOutOfBounds(sDesiredSpot))
	{
		return AI_ACTION_NONE;
	}

	const INT8 bDirections[4] = { NORTH, EAST, SOUTH, WEST };
	const INT8 bOldDirection = pSoldier->ubDirection;
	INT8 bBestDirection = -1;
	INT32 sBestLanding = NOWHERE;
	INT32 iBestScore = VRCQB_INVALID_SCORE;

	for (UINT8 i = 0; i < 4; ++i)
	{
		const INT8 bDirection = bDirections[i];
		INT8 bWindowDirection = DIRECTION_IRRELEVANT;

		// The legacy helper also consults the soldier's facing, so evaluate each
		// cardinal window without leaving a temporary facing change behind.
		pSoldier->ubDirection = bDirection;
		const BOOLEAN fWindow = FindWindowJumpDirection(
			pSoldier, pSoldier->sGridNo, bDirection, &bWindowDirection);
		pSoldier->ubDirection = bOldDirection;

		if (!fWindow || bWindowDirection != bDirection)
			continue;

		const INT32 sLanding = NewGridNo(
			pSoldier->sGridNo, (UINT16)DirectionInc((UINT8)bDirection));
		if (TileIsOutOfBounds(sLanding))
			continue;

		INT32 iScore = -4 * PythSpacesAway(sLanding, sDesiredSpot);
		if (pContext->ubThreatBuildingID != NO_BUILDING &&
			VRCQBGetBuildingId(sLanding) == pContext->ubThreatBuildingID)
		{
			iScore += 80;
		}
		if (VRCQBSameStructure(sLanding, sDesiredSpot))
			iScore += 25;

		// Alternate entry is useful only if it does not amount to jumping into a
		// clearly worse known kill zone.
		iScore -= __min((INT32)60,
			(INT32)AIKnownThreatExposure(
				pSoldier, sLanding, pSoldier->pathing.bLevel) / 6);

		if (iScore > iBestScore)
		{
			iBestScore = iScore;
			bBestDirection = bDirection;
			sBestLanding = sLanding;
		}
	}

	pSoldier->ubDirection = bOldDirection;
	if (bBestDirection < 0 || TileIsOutOfBounds(sBestLanding))
		return AI_ACTION_NONE;

	// Window entry returns before the normal doorway reservation bridge below, so it
	// must claim its own threshold here. Different windows can be used in parallel,
	// but two point men cannot independently commit to the same landing.
	if (!AIReserveTacticalTask(
		pSoldier, AI_TASK_ENTRY_POINT,
		sBestLanding, NOBODY, 1, 1))
	{
		return AI_ACTION_NONE;
	}

	AIBeginShortPlan(
		pSoldier, AI_SHORT_PLAN_CQB,
		sBestLanding, NOBODY, 2);

	// A deliberate entry faces the opening first. This also prevents the jump
	// executor from selecting a different adjacent window from stale facing.
	if (pSoldier->ubDirection != bBestDirection)
	{
		if (!pSoldier->InternalIsValidStance(
			bBestDirection, gAnimControl[pSoldier->usAnimState].ubEndHeight))
		{
			return AI_ACTION_NONE;
		}

		pSoldier->aiData.usActionData = bBestDirection;
		if (uiDecision)
		{
			VRAnalyticsStateInt(uiDecision, "selected_action", AI_ACTION_CHANGE_FACING);
			VRAnalyticsStateInt(uiDecision, "cqb_window_entry", 1);
			VRAnalyticsCandidate(uiDecision, "cqb_window_entry", sBestLanding,
				iBestScore, iBestScore, true, "face selected alternate window entry");
			VRAnalyticsCommitDecision(uiDecision, "cqb_window_entry", sBestLanding,
				iBestScore, "face selected alternate window entry");
		}
		return AI_ACTION_CHANGE_FACING;
	}

	// BeginSoldierClimbWindow() normally follows the active path direction. CQB
	// selected this window explicitly, so discard only the remaining cached path
	// cursor and let the native executor use the facing we just validated.
	pSoldier->pathing.usPathIndex = pSoldier->pathing.usPathDataSize;
	pSoldier->aiData.usActionData = sBestLanding;
	if (uiDecision)
	{
		VRAnalyticsStateInt(uiDecision, "selected_action", AI_ACTION_JUMP_WINDOW);
		VRAnalyticsStateInt(uiDecision, "cqb_window_entry", 1);
		VRAnalyticsCandidate(uiDecision, "cqb_window_entry", sBestLanding,
			iBestScore, iBestScore, true, "use alternate window entry");
		VRAnalyticsCommitDecision(uiDecision, "cqb_window_entry", sBestLanding,
			iBestScore, "use alternate window entry");
	}
	return AI_ACTION_JUMP_WINDOW;
}


static UINT32 VRCQBTraceBegin(SOLDIERTYPE *pSoldier,
	const VRCQB_CONTEXT *pContext, const VRCQB_ASSESSMENT *pAssessment)
{
	UINT32 uiDecision = (UINT32)VRAnalyticsBeginDecision(
		VR_ANALYTICS_TACTICAL, "soldier", pSoldier ? pSoldier->ubID : 0, "cqb_building");

	if (!uiDecision || !pSoldier || !pContext || !pAssessment)
		return uiDecision;

	VRAnalyticsStateInt(uiDecision, "cqb_state", (INT32)pAssessment->eState);
	VRAnalyticsStateInt(uiDecision, "cqb_role", (INT32)pAssessment->eRole);
	VRAnalyticsStateInt(uiDecision, "cqb_reason", (INT32)pAssessment->eReason);
	VRAnalyticsStateInt(uiDecision, "cqb_profile", (INT32)pAssessment->eTrainingProfile);
	VRAnalyticsStateInt(uiDecision, "cqb_confidence", pAssessment->ubConfidence);
	VRAnalyticsStateInt(uiDecision, "cqb_effective_skill", pAssessment->ubEffectiveSkill);
	VRAnalyticsStateInt(uiDecision, "cqb_planner_reliability", pAssessment->ubPlannerReliability);
	VRAnalyticsStateInt(uiDecision, "cqb_room", pContext->usRoomNo);
	VRAnalyticsStateInt(uiDecision, "cqb_threat_room", pContext->usThreatRoomNo);
	VRAnalyticsStateInt(uiDecision, "cqb_building", pContext->ubBuildingID);
	VRAnalyticsStateInt(uiDecision, "cqb_threat_building", pContext->ubThreatBuildingID);
	VRAnalyticsStateInt(uiDecision, "cqb_known_threats", pContext->ubKnownThreats);
	VRAnalyticsStateInt(uiDecision, "cqb_local_friends", pContext->ubLocalFriends);
	VRAnalyticsStateInt(uiDecision, "cqb_entry", pAssessment->sEntryGridNo);
	VRAnalyticsStateInt(uiDecision, "cqb_target", pAssessment->sTargetGridNo);
	VRAnalyticsStateInt(uiDecision, "cqb_fallback", pAssessment->sFallbackGridNo);
	VRAnalyticsStateInt(uiDecision, "cqb_risk", pAssessment->iRiskScore);
	VRAnalyticsStateInt(uiDecision, "cqb_target_exposure", pAssessment->usKnownThreatExposure);

	AITACTICALGEOMETRY Geometry;
	if (AIBuildTacticalGeometry(pSoldier, pSoldier->sGridNo, &Geometry))
	{
		VRAnalyticsStateInt(uiDecision, "cqb_geometry_primary_threat_dir", Geometry.ubPrimaryThreatDir);
		VRAnalyticsStateInt(uiDecision, "cqb_geometry_safest_dir", Geometry.ubSafestDirection);
		VRAnalyticsStateInt(uiDecision, "cqb_geometry_left_flank", Geometry.sLeftFlankOpportunity);
		VRAnalyticsStateInt(uiDecision, "cqb_geometry_right_flank", Geometry.sRightFlankOpportunity);
		VRAnalyticsStateInt(uiDecision, "cqb_geometry_rear_safety", Geometry.sRearSafety);
		VRAnalyticsStateInt(uiDecision, "cqb_geometry_remembered_contacts", Geometry.ubRememberedContacts);
		VRAnalyticsStateInt(uiDecision, "cqb_geometry_memory_sector_mask", Geometry.ubMemoryDirectionMask);
		VRAnalyticsStateInt(uiDecision, "cqb_geometry_corroborated_cues", Geometry.ubCorroboratedCues);
		VRAnalyticsStateInt(uiDecision, "cqb_geometry_corroborated_sector_mask", Geometry.ubCorroboratedDirectionMask);
		VRAnalyticsStateInt(uiDecision, "cqb_geometry_encirclement", Geometry.fEncirclementPressure ? 1 : 0);
	}

	return uiDecision;
}

static INT8 VRCQBTraceNoAction(SOLDIERTYPE *pSoldier, UINT32 uiDecision,
	const VRCQB_ASSESSMENT *pAssessment, INT32 sGridNo, INT32 iScore,
	const CHAR8 *pReason)
{
	if (uiDecision)
	{
		VRAnalyticsStateInt(uiDecision, "selected_action", AI_ACTION_NONE);
		VRAnalyticsCandidate(uiDecision, "cqb_building", sGridNo,
			iScore, iScore, false, pReason);
		VRAnalyticsCommitDecision(uiDecision, "cqb_building",
			sGridNo, iScore, pReason);
	}

	(void)pSoldier;
	(void)pAssessment;
	return AI_ACTION_NONE;
}

INT8 VRCQB_DecideAction(SOLDIERTYPE *pSoldier, BOOLEAN fCanMove, BOOLEAN fAllowAssault)
{
	if (!VRCQB_IsRuntimeEnabled() || !pSoldier ||
		!gGameExternalOptions.bNewTacticalAIBehavior ||
		!gfTurnBasedAI ||
		pSoldier->bTeam != ENEMY_TEAM ||
		!SoldierAI(pSoldier) ||
		pSoldier->aiData.bNeutral ||
		pSoldier->stats.bLife < OKLIFE ||
		pSoldier->bCollapsed || pSoldier->bBreathCollapsed ||
		(pSoldier->usSoldierFlagMask & SOLDIER_POW) ||
		(pSoldier->flags.uiStatusFlags & SOLDIER_BOXER) ||
		(pSoldier->flags.uiStatusFlags & SOLDIER_VEHICLE) ||
		TANK(pSoldier) || AM_A_ROBOT(pSoldier) || pSoldier->IsZombie() ||
		!fCanMove ||
		pSoldier->bActionPoints != pSoldier->bInitialActionPoints)
	{
		return AI_ACTION_NONE;
	}

	VRCQB_CONTEXT Context;
	if (!VRCQB_BuildContext(pSoldier, &Context))
	{
		VRCQB_InvalidateSoldierPlan(pSoldier);
		return AI_ACTION_NONE;
	}

	VRCQB_ASSESSMENT Assessment;
	if (!VRCQB_Assess(pSoldier, &Context, &Assessment))
	{
		VRCQB_InvalidateSoldierPlan(pSoldier);
		return AI_ACTION_NONE;
	}

	UINT32 uiDecision = VRCQBTraceBegin(pSoldier, &Context, &Assessment);

	VRCQB_TRAINING_MODEL Model;
	if (!VRCQB_GetTrainingModel(pSoldier, &Model))
	{
		VRCQB_InvalidateSoldierPlan(pSoldier);
		return VRCQBTraceNoAction(pSoldier, uiDecision, &Assessment,
			pSoldier->sGridNo, 0, "training model unavailable");
	}

	const BOOLEAN fAggressiveCQB =
		Assessment.eState == VRCQB_STATE_ASSAULT ||
		Assessment.eState == VRCQB_STATE_COUNTERATTACK;

	if (fAggressiveCQB && !fAllowAssault)
	{
		VRCQB_InvalidateSoldierPlan(pSoldier);
		return VRCQBTraceNoAction(pSoldier, uiDecision, &Assessment,
			pSoldier->sGridNo, Assessment.iPositionScore,
			"immediate combat action retained priority");
	}

	if (fAggressiveCQB && !AICheckHasGun(pSoldier))
	{
		VRCQB_InvalidateSoldierPlan(pSoldier);
		return VRCQBTraceNoAction(pSoldier, uiDecision, &Assessment,
			pSoldier->sGridNo, Assessment.iPositionScore,
			"no firearm for deliberate CQB assault");
	}

	UINT8 ubRequiredConfidence = 35;
	switch (Assessment.eState)
	{
	case VRCQB_STATE_ASSAULT:        ubRequiredConfidence = 48; break;
	case VRCQB_STATE_COUNTERATTACK:  ubRequiredConfidence = 60; break;
	case VRCQB_STATE_SECURE:         ubRequiredConfidence = 38; break;
	case VRCQB_STATE_DELAY_FALLBACK: ubRequiredConfidence = 30; break;
	case VRCQB_STATE_HOLD:           ubRequiredConfidence = 30; break;
	default: break;
	}

	// Elite mobile elements can maintain clearing momentum from one known room
	// into the next. This affects willingness to continue a valid plan only; it
	// never changes AP, sight, accuracy or opponent knowledge.
	if (Assessment.eState == VRCQB_STATE_ASSAULT &&
		Context.fKnownThreatInSameBuilding &&
		VRCQB_HasCapability(&Model, VRCQB_CAP_COMPLEX_ROOM_FLOW))
	{
		const VRCQB_STATE ePrevious = VRCQBPreviousState(pSoldier);
		if (ePrevious == VRCQB_STATE_ASSAULT || ePrevious == VRCQB_STATE_SECURE)
			ubRequiredConfidence = (UINT8)__max((INT32)38,
				(INT32)ubRequiredConfidence - 8);
	}

	if (Assessment.ubConfidence < ubRequiredConfidence)
	{
		VRCQB_InvalidateSoldierPlan(pSoldier);
		return VRCQBTraceNoAction(pSoldier, uiDecision, &Assessment,
			pSoldier->sGridNo, Assessment.iPositionScore,
			"CQB confidence below action threshold");
	}

	// Commanded/experienced elements may deliberately obscure an exposed entry
	// before committing movers. Basic security troops keep the simpler hold/cover
	// behaviour and never gain this capability implicitly.
	if (fAggressiveCQB)
	{
		INT8 bSmokeAction = VRCQBTryProactiveEntrySmoke(
			pSoldier, &Context, &Assessment, &Model, uiDecision);
		if (bSmokeAction != AI_ACTION_NONE)
			return bSmokeAction;
	}

	VRCQB_STATE eMovementState = Assessment.eState;
	VRCQB_ROLE eMovementRole = Assessment.eRole;
	INT32 sDesiredSpot = Assessment.sTargetGridNo;
	INT8 bAction = AI_ACTION_NONE;
	const CHAR8 *pActionReason = VRCQB_ReasonName(Assessment.eReason);

	if (fAggressiveCQB)
	{
		UINT8 ubCommittedMovers =
			VRCQBCountCommittedMovers(pSoldier, &Assessment);

		// A soldier selected as SUPPORT in a two-man-capable element does not
		// follow the point man through the same threshold. He establishes a local
		// cover position, leaving the doorway/foothold to the maneuver role.
		if (Assessment.eRole == VRCQB_ROLE_SUPPORT &&
			VRCQB_HasCapability(&Model, VRCQB_CAP_TWO_MAN_ENTRY) &&
			Context.fHasLocalSupport)
		{
			eMovementState = VRCQB_STATE_HOLD;
			eMovementRole = VRCQB_ROLE_SUPPORT;
			sDesiredSpot = VRCQBFindBestLocalPosition(
				pSoldier, &Context, &Model, eMovementState, eMovementRole);
			bAction = AI_ACTION_TAKE_COVER;
			pActionReason = "CQB two-man entry: support covers point";
		}
		else if (ubCommittedMovers >= Assessment.ubMaxCoordinatedMovers)
		{
			// Do not join a doorway queue. Convert excess movers into local
			// support/hold positions instead of allowing legacy seek logic to
			// send the whole fireteam through the same entry.
			eMovementState = VRCQB_STATE_HOLD;
			eMovementRole = VRCQB_ROLE_SUPPORT;
			sDesiredSpot = VRCQBFindBestLocalPosition(
				pSoldier, &Context, &Model, eMovementState, eMovementRole);
			bAction = AI_ACTION_TAKE_COVER;
			pActionReason = "CQB mover budget: establish support";
		}
		else
		{
			// Veteran/mobile elements can use a suitable adjacent window as an
			// alternate entry instead of feeding the same doorway. This is an
			// explicit capability; basic/ordinary line troops do not attempt it.
			INT8 bWindowAction = VRCQBTryAlternateWindowEntry(
				pSoldier, &Context, &Assessment, &Model, sDesiredSpot, uiDecision);
			if (bWindowAction != AI_ACTION_NONE)
				return bWindowAction;

			bAction = AI_ACTION_SEEK_OPPONENT;
		}
	}
	else if (Assessment.eState == VRCQB_STATE_DELAY_FALLBACK)
	{
		bAction = AI_ACTION_WITHDRAW;
	}
	else if (Assessment.eState == VRCQB_STATE_HOLD ||
		Assessment.eState == VRCQB_STATE_SECURE)
	{
		bAction = AI_ACTION_TAKE_COVER;
	}

	if (bAction != AI_ACTION_NONE && !TileIsOutOfBounds(sDesiredSpot))
	{
		// CQB participates in the same fireteam-local task board as outdoor maneuver.
		// AIReserveTacticalTask() replaces this soldier's old claim only after the new
		// CQB claim succeeds, so merely evaluating CQB cannot erase a valid role task.
		const INT32 sTaskTarget =
			!TileIsOutOfBounds(Assessment.sEntryGridNo) ?
			Assessment.sEntryGridNo : sDesiredSpot;

		if (fAggressiveCQB && eMovementRole == VRCQB_ROLE_POINT)
		{
			if (!AIReserveTacticalTask(
				pSoldier, AI_TASK_ENTRY_POINT,
				sTaskTarget, NOBODY, 1, 1))
			{
				// Another point man already owns this threshold. Become the support
				// element instead of forming a doorway queue.
				eMovementState = VRCQB_STATE_HOLD;
				eMovementRole = VRCQB_ROLE_SUPPORT;
				sDesiredSpot = VRCQBFindBestLocalPosition(
					pSoldier, &Context, &Model,
					eMovementState, eMovementRole);
				bAction = AI_ACTION_TAKE_COVER;
				pActionReason = "CQB entry claimed: establish support";
				if (!AIReserveTacticalTask(
					pSoldier, AI_TASK_ENTRY_SUPPORT,
					sTaskTarget, NOBODY, 2, 1))
				{
					eMovementRole = VRCQB_ROLE_RESERVE;
					sDesiredSpot = VRCQBFindBestLocalPosition(
						pSoldier, &Context, &Model,
						VRCQB_STATE_HOLD, eMovementRole);
					bAction = AI_ACTION_TAKE_COVER;
					pActionReason = "CQB entry support saturated: hold reserve";
					AIReleaseTacticalTask(pSoldier);
				}
			}
		}
		else if (fAggressiveCQB ||
			eMovementRole == VRCQB_ROLE_SUPPORT ||
			eMovementRole == VRCQB_ROLE_COVER ||
			eMovementRole == VRCQB_ROLE_SECURITY)
		{
			if (!AIReserveTacticalTask(
				pSoldier, AI_TASK_ENTRY_SUPPORT,
				sTaskTarget, NOBODY, 2, 1))
			{
				eMovementState = VRCQB_STATE_HOLD;
				eMovementRole = VRCQB_ROLE_RESERVE;
				sDesiredSpot = VRCQBFindBestLocalPosition(
					pSoldier, &Context, &Model,
					eMovementState, eMovementRole);
				bAction = AI_ACTION_TAKE_COVER;
				pActionReason = "CQB support saturated: hold local reserve";
				AIReleaseTacticalTask(pSoldier);
			}
		}
	}

	if (bAction == AI_ACTION_NONE ||
		TileIsOutOfBounds(sDesiredSpot) ||
		sDesiredSpot == pSoldier->sGridNo)
	{
		return VRCQBTraceNoAction(pSoldier, uiDecision, &Assessment,
			pSoldier->sGridNo, Assessment.iPositionScore,
			"CQB position already satisfactory");
	}

	INT32 iCurrentScore = VRCQB_ScorePosition(
		pSoldier, &Context, &Model, eMovementState, eMovementRole,
		pSoldier->sGridNo);
	INT32 iDesiredScore = VRCQB_ScorePosition(
		pSoldier, &Context, &Model, eMovementState, eMovementRole,
		sDesiredSpot);

	if (eMovementState == VRCQB_STATE_HOLD ||
		eMovementState == VRCQB_STATE_SECURE)
	{
		INT32 iRequiredGain = 10 - (INT32)Assessment.ubEffectiveSkill / 20;
		iRequiredGain = __max((INT32)4, iRequiredGain);

		if (iDesiredScore < iCurrentScore + iRequiredGain)
		{
			return VRCQBTraceNoAction(pSoldier, uiDecision, &Assessment,
				pSoldier->sGridNo, iCurrentScore,
				"CQB reposition gain too small");
		}
	}

	INT16 sReserveAP = 0;
	UINT8 ubMoveFlags = 0;
	if (bAction == AI_ACTION_TAKE_COVER)
	{
		sReserveAP = (INT16)(GetAPsCrouch(pSoldier, TRUE) + GetAPsToLook(pSoldier));
		ubMoveFlags = FLAG_CAUTIOUS;
	}
	else if (bAction == AI_ACTION_SEEK_OPPONENT)
	{
		sReserveAP = (INT16)GetAPsToLook(pSoldier);
	}

	INT32 sMoveSpot = InternalGoAsFarAsPossibleTowards(
		pSoldier, sDesiredSpot, sReserveAP, bAction, ubMoveFlags);

	if (TileIsOutOfBounds(sMoveSpot) || sMoveSpot == pSoldier->sGridNo)
	{
		AIRegisterTacticalSetback(
			pSoldier, AI_SETBACK_ROUTE,
			sDesiredSpot, 28, 2);
		AIReleaseTacticalTask(pSoldier);
		VRCQB_InvalidateSoldierPlan(pSoldier);
		return VRCQBTraceNoAction(pSoldier, uiDecision, &Assessment,
			pSoldier->sGridNo, iCurrentScore,
			"CQB route unavailable");
	}

	UINT16 usPeakIncrease = 145;
	UINT16 usUncoveredIncrease = 70;
	UINT16 usAverageIncrease = 90;

	if (eMovementState == VRCQB_STATE_ASSAULT ||
		eMovementState == VRCQB_STATE_COUNTERATTACK)
	{
		usPeakIncrease = (UINT16)__min((INT32)220,
			120 + (INT32)Assessment.ubEffectiveSkill);
		usUncoveredIncrease = (UINT16)__min((INT32)115,
			60 + (INT32)Assessment.ubEffectiveSkill / 2);
		usAverageIncrease = (UINT16)__min((INT32)130,
			75 + (INT32)Assessment.ubEffectiveSkill / 2);
	}
	else if (eMovementState == VRCQB_STATE_DELAY_FALLBACK)
	{
		usPeakIncrease = 200;
		usUncoveredIncrease = 110;
		usAverageIncrease = 130;
	}

	if (!AIKnownRouteExposureAcceptable(
		pSoldier, sMoveSpot, bAction,
		usPeakIncrease, usUncoveredIncrease, usAverageIncrease))
	{
		AIRegisterTacticalSetback(
			pSoldier, AI_SETBACK_CQB_ENTRY,
			sMoveSpot, 58, 4);
		AIReleaseTacticalTask(pSoldier);
		VRCQB_InvalidateSoldierPlan(pSoldier);
		return VRCQBTraceNoAction(pSoldier, uiDecision, &Assessment,
			pSoldier->sGridNo, iCurrentScore,
			"CQB route exposure rejected");
	}

	pSoldier->aiData.usActionData = sMoveSpot;
	if (bAction == AI_ACTION_SEEK_OPPONENT)
		pSoldier->sAbsoluteFinalDestination = sDesiredSpot;

	// The CQB module keeps its rich room/entry assessment, while the common short-plan
	// layer now owns the generic "continue this tactical commitment unless invalidated"
	// semantics used by the rest of the AI.
	AIBeginShortPlan(
		pSoldier, AI_SHORT_PLAN_CQB,
		sDesiredSpot, NOBODY, 2);

	if (uiDecision)
	{
		VRAnalyticsStateInt(uiDecision, "selected_action", bAction);
		VRAnalyticsStateInt(uiDecision, "cqb_actual_move", sMoveSpot);
		VRAnalyticsStateInt(uiDecision, "cqb_desired_move", sDesiredSpot);
		VRAnalyticsStateInt(uiDecision, "cqb_current_score", iCurrentScore);
		VRAnalyticsStateInt(uiDecision, "cqb_desired_score", iDesiredScore);
		VRAnalyticsStateInt(uiDecision, "cqb_current_setback_penalty",
			AITacticalSetbackPenalty(pSoldier, pSoldier->sGridNo));
		VRAnalyticsStateInt(uiDecision, "cqb_desired_setback_penalty",
			AITacticalSetbackPenalty(pSoldier, sDesiredSpot));
		VRAnalyticsCandidate(uiDecision, "cqb_building", sMoveSpot,
			iDesiredScore, iDesiredScore, true, pActionReason);
		VRAnalyticsCommitDecision(uiDecision, "cqb_building",
			sMoveSpot, iDesiredScore, pActionReason);
	}

	return bAction;
}

const CHAR8 *VRCQB_StateName(VRCQB_STATE eState)
{
	static const CHAR8 *pNames[VRCQB_STATE_MAX] =
	{
		"none", "assault", "hold", "delay_fallback", "counterattack", "secure"
	};
	return (eState >= 0 && eState < VRCQB_STATE_MAX) ? pNames[eState] : "unknown";
}

const CHAR8 *VRCQB_RoleName(VRCQB_ROLE eRole)
{
	static const CHAR8 *pNames[VRCQB_ROLE_MAX] =
	{
		"none", "point", "cover", "support", "security", "hold", "reserve"
	};
	return (eRole >= 0 && eRole < VRCQB_ROLE_MAX) ? pNames[eRole] : "unknown";
}

const CHAR8 *VRCQB_ProfileName(VRCQB_TRAINING_PROFILE eProfile)
{
	static const CHAR8 *pNames[VRCQB_TRAINING_MAX] =
	{
		"security_basic", "line_basic", "line_commanded",
		"veteran", "elite_mobile", "elite_guard"
	};
	return (eProfile >= 0 && eProfile < VRCQB_TRAINING_MAX) ?
		pNames[eProfile] : "unknown";
}

const CHAR8 *VRCQB_ReasonName(VRCQB_REASON eReason)
{
	static const CHAR8 *pNames[VRCQB_REASON_MAX] =
	{
		"none",
		"anchored_hold",
		"threat_in_room",
		"threat_in_building",
		"approach_indoor_threat",
		"high_risk_fallback",
		"disengagement",
		"secure_foothold",
		"local_counterattack",
		"security_refuses_complex_assault",
		"insufficient_entry_support",
		"assault_hesitation",
		"unresolved_contact_memory"
	};
	return (eReason >= 0 && eReason < VRCQB_REASON_MAX) ?
		pNames[eReason] : "unknown";
}

void VRCQB_InvalidateSoldierPlan(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->ubID >= TOTAL_SOLDIERS)
		return;

	memset(&gVRCQBPlan[pSoldier->ubID], 0, sizeof(VRCQB_PLAN_SLOT));

	// CQB owns invalidation of its shared short-plan wrapper. Do not cancel a
	// fallback/disengage/rescue plan that may already have superseded CQB.
	AISHORTPLANSTATE SharedPlan;
	if (AIGetShortPlan(pSoldier, &SharedPlan) &&
		SharedPlan.ubType == AI_SHORT_PLAN_CQB)
	{
		AICancelShortPlan(pSoldier);
	}
}

void VRCQB_ResetTransientState(void)
{
	memset(gVRCQBPlan, 0, sizeof(gVRCQBPlan));
	guiVRCQBLastTurnStamp = 0;
}
