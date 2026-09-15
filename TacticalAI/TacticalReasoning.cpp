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

#define AI_CONTACT_MEMORY_MAX_TURNS 12
#define AI_CONTACT_MEMORY_MIN_CONFIDENCE 18

typedef struct
{
	BOOLEAN fValid;
	UINT32 uiObserverIdentity;
	UINT32 uiOpponentIdentity;
	INT32 sLastKnownGridNo;
	INT8 bLevel;
	UINT8 ubSource;
	UINT8 ubBaseConfidence;
	UINT32 uiLastVisualEvidenceTurn;
	UINT32 uiLastCheckedTurn;
} AICONTACTMEMORYSLOT;

static AICONTACTMEMORYSLOT
	gAIContactMemory[MAX_NUM_SOLDIERS][MAX_NUM_SOLDIERS];

static void AIRecordContactMemory(
	SOLDIERTYPE *pSoldier, const AICONTACTBELIEF *pBelief);

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

	// Only visual knowledge refreshes exact contact memory. Heard information and
	// generic noises may corroborate a remembered sector later, but they do not
	// silently identify the unseen shooter.
	if (pBelief->bKnowledge > NOT_HEARD_OR_SEEN)
		AIRecordContactMemory(pSoldier, pBelief);

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

static UINT8 AIGeometryDirectionDelta(UINT8 ubA, UINT8 ubB)
{
	if (ubA >= NUM_WORLD_DIRECTIONS || ubB >= NUM_WORLD_DIRECTIONS)
		return NUM_WORLD_DIRECTIONS;

	UINT8 ubDelta = (UINT8)abs((INT32)ubA - (INT32)ubB);
	return __min(ubDelta, (UINT8)(NUM_WORLD_DIRECTIONS - ubDelta));
}

static UINT8 AIGeometryRotate(UINT8 ubDirection, INT8 bSteps)
{
	if (ubDirection >= NUM_WORLD_DIRECTIONS)
		return DIRECTION_IRRELEVANT;

	INT32 iDirection = (INT32)ubDirection + (INT32)bSteps;
	while (iDirection < 0)
		iDirection += NUM_WORLD_DIRECTIONS;
	while (iDirection >= NUM_WORLD_DIRECTIONS)
		iDirection -= NUM_WORLD_DIRECTIONS;
	return (UINT8)iDirection;
}

static UINT8 AIContactMemoryAge(const AICONTACTMEMORYSLOT *pSlot)
{
	if (!pSlot || !pSlot->fValid)
		return 255;

	if (guiTurnCnt < pSlot->uiLastVisualEvidenceTurn)
		return 255;

	UINT32 uiAge = guiTurnCnt - pSlot->uiLastVisualEvidenceTurn;
	return (UINT8)__min((UINT32)255, uiAge);
}

static UINT8 AIContactMemoryConfidence(const AICONTACTMEMORYSLOT *pSlot)
{
	const UINT8 ubAge = AIContactMemoryAge(pSlot);
	if (!pSlot || !pSlot->fValid || ubAge == 255 ||
		ubAge > AI_CONTACT_MEMORY_MAX_TURNS)
	{
		return 0;
	}

	INT32 iDecayPerTurn =
		(pSlot->ubSource == AI_BELIEF_SOURCE_PERSONAL) ? 6 : 9;
	INT32 iConfidence =
		(INT32)pSlot->ubBaseConfidence - iDecayPerTurn * (INT32)ubAge;

	return (UINT8)__max(0, __min(100, iConfidence));
}

static void AIRecordContactMemory(
	SOLDIERTYPE *pSoldier, const AICONTACTBELIEF *pBelief)
{
	if (!pSoldier || !pBelief ||
		pSoldier->ubID >= MAX_NUM_SOLDIERS ||
		pBelief->ubOpponentID == NOBODY ||
		pBelief->ubOpponentID >= MAX_NUM_SOLDIERS ||
		TileIsOutOfBounds(pBelief->sGridNo) ||
		pBelief->bKnowledge <= NOT_HEARD_OR_SEEN)
	{
		return;
	}

	AICONTACTMEMORYSLOT *pSlot =
		&gAIContactMemory[pSoldier->ubID][pBelief->ubOpponentID];

	UINT8 ubBase = pBelief->ubConfidence;
	if (pBelief->ubSource == AI_BELIEF_SOURCE_PERSONAL)
		ubBase = (UINT8)__max((INT32)80, (INT32)ubBase);
	else
		ubBase = (UINT8)__max((INT32)65, (INT32)ubBase);

	memset(pSlot, 0, sizeof(AICONTACTMEMORYSLOT));
	pSlot->fValid = TRUE;
	pSlot->uiObserverIdentity = pSoldier->uiUniqueSoldierIdValue;
	pSlot->sLastKnownGridNo = pBelief->sGridNo;
	pSlot->bLevel = pBelief->bLevel;
	pSlot->ubSource = pBelief->ubSource;
	pSlot->ubBaseConfidence = ubBase;
	UINT8 ubEvidenceAge = pBelief->ubAgeTurns;
	if (ubEvidenceAge == 255)
		ubEvidenceAge = AIKnowledgeAgeTurns(pBelief->bKnowledge);
	pSlot->uiLastVisualEvidenceTurn =
		(guiTurnCnt >= ubEvidenceAge) ?
		(guiTurnCnt - ubEvidenceAge) : 0;
	pSlot->uiLastCheckedTurn = 0xFFFFFFFF;

	// Identity is bookkeeping only. It prevents a recycled soldier slot from
	// inheriting somebody else's remembered contact; it is never used as evidence.
	SOLDIERTYPE *pOpponent = MercPtrs[pBelief->ubOpponentID];
	pSlot->uiOpponentIdentity =
		pOpponent ? pOpponent->uiUniqueSoldierIdValue : 0;
}

static void AIRefreshThreatMemoryFromKnowledge(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	for (UINT16 i = 0; i < TOTAL_SOLDIERS && i < MAX_NUM_SOLDIERS; ++i)
	{
		SOLDIERTYPE *pOpponent = MercPtrs[i];
		if (!pOpponent ||
			CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			pSoldier->bSide == pOpponent->bSide)
		{
			continue;
		}

		INT8 bKnowledge = Knowledge(pSoldier, (UINT8)i);
		if (bKnowledge <= NOT_HEARD_OR_SEEN)
			continue;

		INT32 sKnown = KnownLocation(pSoldier, (UINT8)i);
		if (TileIsOutOfBounds(sKnown))
			continue;

		AICONTACTBELIEF Belief;
		memset(&Belief, 0, sizeof(Belief));
		Belief.ubOpponentID = (UINT8)i;
		Belief.sGridNo = sKnown;
		Belief.bLevel = KnownLevel(pSoldier, (UINT8)i);
		Belief.bKnowledge = bKnowledge;
		Belief.ubAgeTurns = AIKnowledgeAgeTurns(bKnowledge);
		Belief.ubSource = UsePersonalKnowledge(pSoldier, (UINT8)i) ?
			AI_BELIEF_SOURCE_PERSONAL : AI_BELIEF_SOURCE_PUBLIC;

		INT32 iKnowledgeIndex =
			(INT32)bKnowledge - (INT32)OLDEST_HEARD_VALUE;
		if (iKnowledgeIndex >= 0 && iKnowledgeIndex < 10)
			Belief.ubConfidence = (UINT8)__max(
				0, __min(100, ThreatPercent[iKnowledgeIndex]));

		AIRecordContactMemory(pSoldier, &Belief);
	}
}

static BOOLEAN AIUsableContactMemory(
	SOLDIERTYPE *pSoldier, UINT8 ubOpponentID,
	AICONTACTMEMORYSLOT **ppSlot, UINT8 *pubConfidence)
{
	if (ppSlot) *ppSlot = NULL;
	if (pubConfidence) *pubConfidence = 0;

	if (!pSoldier ||
		pSoldier->ubID >= MAX_NUM_SOLDIERS ||
		ubOpponentID >= MAX_NUM_SOLDIERS)
	{
		return FALSE;
	}

	AICONTACTMEMORYSLOT *pSlot =
		&gAIContactMemory[pSoldier->ubID][ubOpponentID];

	if (!pSlot->fValid ||
		pSlot->uiObserverIdentity != pSoldier->uiUniqueSoldierIdValue ||
		TileIsOutOfBounds(pSlot->sLastKnownGridNo))
	{
		return FALSE;
	}

	UINT8 ubConfidence = AIContactMemoryConfidence(pSlot);
	if (ubConfidence < AI_CONTACT_MEMORY_MIN_CONFIDENCE)
	{
		memset(pSlot, 0, sizeof(AICONTACTMEMORYSLOT));
		pSlot->sLastKnownGridNo = NOWHERE;
		return FALSE;
	}

	// If the soldier has reached and can inspect the remembered location without
	// reacquiring the opponent, sharply retire that hypothesis instead of pacing
	// back to the same empty tile forever.
	if (Knowledge(pSoldier, ubOpponentID) == NOT_HEARD_OR_SEEN &&
		pSlot->bLevel == pSoldier->pathing.bLevel &&
		PythSpacesAway(pSoldier->sGridNo, pSlot->sLastKnownGridNo) <= 4 &&
		SoldierTo3DLocationLineOfSightTest(
			pSoldier, pSlot->sLastKnownGridNo, pSlot->bLevel,
			0, FALSE, NO_DISTANCE_LIMIT) > 0 &&
		pSlot->uiLastCheckedTurn != guiTurnCnt)
	{
		pSlot->uiLastCheckedTurn = guiTurnCnt;

		if (ubConfidence <= 45)
		{
			memset(pSlot, 0, sizeof(AICONTACTMEMORYSLOT));
			pSlot->sLastKnownGridNo = NOWHERE;
			return FALSE;
		}

		pSlot->ubBaseConfidence =
			(UINT8)__max(0, (INT32)pSlot->ubBaseConfidence - 35);
		ubConfidence = AIContactMemoryConfidence(pSlot);
	}

	if (ppSlot) *ppSlot = pSlot;
	if (pubConfidence) *pubConfidence = ubConfidence;
	return TRUE;
}

BOOLEAN AIBuildThreatMemoryCue(
	SOLDIERTYPE *pSoldier, AITHREATMEMORYCUE *pCue)
{
	if (!pCue)
		return FALSE;

	memset(pCue, 0, sizeof(AITHREATMEMORYCUE));
	pCue->sGridNo = NOWHERE;
	pCue->bLevel = 0;
	pCue->ubDirection = DIRECTION_IRRELEVANT;
	pCue->ubAgeTurns = 255;

	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return FALSE;

	AIRefreshThreatMemoryFromKnowledge(pSoldier);

	INT32 iBestScore = -1000000;
	UINT8 ubBestOpponent = NOBODY;

	for (UINT16 i = 0; i < TOTAL_SOLDIERS && i < MAX_NUM_SOLDIERS; ++i)
	{
		// Normal legal knowledge is stronger than memory and should be handled by
		// the ordinary JA2 opponent/noise logic.
		if (Knowledge(pSoldier, (UINT8)i) != NOT_HEARD_OR_SEEN)
			continue;

		AICONTACTMEMORYSLOT *pSlot = NULL;
		UINT8 ubConfidence = 0;
		if (!AIUsableContactMemory(
			pSoldier, (UINT8)i, &pSlot, &ubConfidence))
		{
			continue;
		}

		INT32 iDistance =
			PythSpacesAway(pSoldier->sGridNo, pSlot->sLastKnownGridNo);
		INT32 iScore =
			(INT32)ubConfidence * 3 - __min(80, iDistance * 2);

		if (pSlot->ubSource == AI_BELIEF_SOURCE_PERSONAL)
			iScore += 20;

		if (iScore > iBestScore)
		{
			iBestScore = iScore;
			ubBestOpponent = (UINT8)i;
			pCue->sGridNo = pSlot->sLastKnownGridNo;
			pCue->bLevel = pSlot->bLevel;
			pCue->ubDirection =
				AIDirection(pSoldier->sGridNo, pSlot->sLastKnownGridNo);
			pCue->ubConfidence = ubConfidence;
			pCue->ubAgeTurns = AIContactMemoryAge(pSlot);
		}
	}

	if (ubBestOpponent == NOBODY || TileIsOutOfBounds(pCue->sGridNo))
		return FALSE;

	// Count other remembered contacts supporting roughly the same sector. This is
	// evidence of an area of concern, not evidence that those opponents are there now.
	for (UINT16 i = 0; i < TOTAL_SOLDIERS && i < MAX_NUM_SOLDIERS; ++i)
	{
		AICONTACTMEMORYSLOT *pSlot = NULL;
		UINT8 ubConfidence = 0;
		if (!AIUsableContactMemory(
			pSoldier, (UINT8)i, &pSlot, &ubConfidence))
		{
			continue;
		}

		UINT8 ubDir =
			AIDirection(pSoldier->sGridNo, pSlot->sLastKnownGridNo);
		if (AIGeometryDirectionDelta(ubDir, pCue->ubDirection) <= 1 &&
			pCue->ubMatchedMemories < 255)
		{
			++pCue->ubMatchedMemories;
		}
	}

	return TRUE;
}

INT32 AIMemoryNoiseRelevance(
	SOLDIERTYPE *pSoldier, INT32 sNoiseGridNo, INT8 bNoiseLevel)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS ||
		TileIsOutOfBounds(sNoiseGridNo))
	{
		return 0;
	}

	AIRefreshThreatMemoryFromKnowledge(pSoldier);

	UINT8 ubNoiseDir = AIDirection(pSoldier->sGridNo, sNoiseGridNo);
	if (ubNoiseDir >= NUM_WORLD_DIRECTIONS)
		return 0;

	INT32 iBest = 0;

	for (UINT16 i = 0; i < TOTAL_SOLDIERS && i < MAX_NUM_SOLDIERS; ++i)
	{
		// If the opponent is currently visible, this is not a memory inference.
		if (PersonalKnowledge(pSoldier, (UINT8)i) == SEEN_CURRENTLY)
			continue;

		AICONTACTMEMORYSLOT *pSlot = NULL;
		UINT8 ubConfidence = 0;
		if (!AIUsableContactMemory(
			pSoldier, (UINT8)i, &pSlot, &ubConfidence))
		{
			continue;
		}

		UINT8 ubMemoryDir =
			AIDirection(pSoldier->sGridNo, pSlot->sLastKnownGridNo);
		UINT8 ubDelta =
			AIGeometryDirectionDelta(ubMemoryDir, ubNoiseDir);

		INT32 iScore = 0;
		if (ubDelta == 0)
			iScore += 38;
		else if (ubDelta == 1)
			iScore += 24;
		else if (ubDelta == 2)
			iScore += 8;
		else
			continue;

		INT32 iEvidenceDistance =
			PythSpacesAway(pSlot->sLastKnownGridNo, sNoiseGridNo);
		if (iEvidenceDistance <= 3)
			iScore += 24;
		else if (iEvidenceDistance <= __max(4, TACTICAL_RANGE / 3))
			iScore += 15;
		else if (iEvidenceDistance <= __max(6, TACTICAL_RANGE / 2))
			iScore += 8;

		if (pSlot->bLevel == bNoiseLevel)
			iScore += 6;
		else
			iScore /= 2;

		iScore = (iScore * (INT32)ubConfidence) / 100;
		iBest = __max(iBest, iScore);
	}

	// Corroboration should materially affect investigation priority without turning
	// a noise into precise opponent knowledge.
	return __min(60, iBest);
}

static INT32 AIGeometryDirectionalThreat(const AITACTICALGEOMETRY *pGeometry, UINT8 ubDirection)
{
	if (!pGeometry || ubDirection >= NUM_WORLD_DIRECTIONS)
		return 0;

	const UINT8 ubCW = AIGeometryRotate(ubDirection, 1);
	const UINT8 ubCCW = AIGeometryRotate(ubDirection, -1);

	return (INT32)pGeometry->usThreatPressure[ubDirection] * 2 +
		(INT32)pGeometry->usThreatPressure[ubCW] +
		(INT32)pGeometry->usThreatPressure[ubCCW];
}

static INT32 AIGeometryDirectionalSupport(const AITACTICALGEOMETRY *pGeometry, UINT8 ubDirection)
{
	if (!pGeometry || ubDirection >= NUM_WORLD_DIRECTIONS)
		return 0;

	const UINT8 ubCW = AIGeometryRotate(ubDirection, 1);
	const UINT8 ubCCW = AIGeometryRotate(ubDirection, -1);

	return (INT32)pGeometry->usFriendlyPressure[ubDirection] * 2 +
		(INT32)pGeometry->usFriendlyPressure[ubCW] +
		(INT32)pGeometry->usFriendlyPressure[ubCCW];
}

static INT32 AIGeometryDirectionalDanger(const AITACTICALGEOMETRY *pGeometry, UINT8 ubDirection)
{
	return AIGeometryDirectionalThreat(pGeometry, ubDirection) -
		AIGeometryDirectionalSupport(pGeometry, ubDirection) / 3;
}

BOOLEAN AIBuildTacticalGeometry(SOLDIERTYPE *pSoldier, INT32 sAnchorGridNo,
	AITACTICALGEOMETRY *pGeometry)
{
	if (!pGeometry)
		return FALSE;

	memset(pGeometry, 0, sizeof(AITACTICALGEOMETRY));
	pGeometry->ubPrimaryThreatDir = DIRECTION_IRRELEVANT;
	pGeometry->ubSecondaryThreatDir = DIRECTION_IRRELEVANT;
	pGeometry->ubSafestDirection = DIRECTION_IRRELEVANT;
	pGeometry->ubStrongestFriendlyDir = DIRECTION_IRRELEVANT;

	if (!pSoldier || TileIsOutOfBounds(sAnchorGridNo))
		return FALSE;

	AIRefreshThreatMemoryFromKnowledge(pSoldier);

	// Opponent geometry is belief-bound. Stale/heard contacts contribute less
	// pressure through the normal ThreatPercent-derived belief confidence.
	for (UINT16 i = 0; i < TOTAL_SOLDIERS; ++i)
	{
		AICONTACTBELIEF Belief;
		if (!AIBuildContactBelief(pSoldier, (UINT8)i, &Belief))
			continue;

		UINT8 ubDir = AIDirection(sAnchorGridNo, Belief.sGridNo);
		if (ubDir >= NUM_WORLD_DIRECTIONS)
			continue;

		const INT32 iDistance = __max(1, PythSpacesAway(sAnchorGridNo, Belief.sGridNo));
		const INT32 iDistanceWeight = __max(25, __min(100, 120 - 4 * iDistance));
		INT32 iPressure = ((INT32)Belief.ubConfidence * iDistanceWeight) / 100;

		if (Belief.fDirectlyVisible)
		{
			iPressure += 20;
			++pGeometry->ubVisibleContacts;
			pGeometry->ubVisibleDirectionMask |= (UINT8)(1 << ubDir);
		}

		if (Belief.bLevel == pSoldier->pathing.bLevel)
			iPressure += 5;

		iPressure = __max(1, __min(180, iPressure));
		pGeometry->usThreatPressure[ubDir] = (UINT16)__min(
			65535, (INT32)pGeometry->usThreatPressure[ubDir] + iPressure);
		++pGeometry->ubKnownContacts;
	}

	// Once normal JA2 knowledge expires, retain only low-confidence directional
	// pressure from the last visually established location. It may influence
	// caution/search/flank choice but is too weak to authorize an attack.
	for (UINT16 i = 0; i < TOTAL_SOLDIERS && i < MAX_NUM_SOLDIERS; ++i)
	{
		if (Knowledge(pSoldier, (UINT8)i) != NOT_HEARD_OR_SEEN)
			continue;

		AICONTACTMEMORYSLOT *pSlot = NULL;
		UINT8 ubConfidence = 0;
		if (!AIUsableContactMemory(
			pSoldier, (UINT8)i, &pSlot, &ubConfidence))
		{
			continue;
		}

		UINT8 ubDir =
			AIDirection(sAnchorGridNo, pSlot->sLastKnownGridNo);
		if (ubDir >= NUM_WORLD_DIRECTIONS)
			continue;

		INT32 iPressure = __max(4, (INT32)ubConfidence / 3);
		if (pSlot->ubSource == AI_BELIEF_SOURCE_PUBLIC)
			iPressure = (3 * iPressure) / 4;

		pGeometry->usThreatPressure[ubDir] = (UINT16)__min(
			65535, (INT32)pGeometry->usThreatPressure[ubDir] + iPressure);
		pGeometry->ubMemoryDirectionMask |= (UINT8)(1 << ubDir);
		if (pGeometry->ubRememberedContacts < 255)
			++pGeometry->ubRememberedContacts;
	}

	// Friendly geometry is intentionally local. Combat teams coordinate only with
	// their fireteam so this does not become a sector-wide hive mind.
	for (UINT8 ubID = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		ubID <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++ubID)
	{
		SOLDIERTYPE *pFriend = MercPtrs[ubID];
		if (!pFriend || pFriend == pSoldier ||
			!pFriend->bActive || !pFriend->bInSector ||
			pFriend->stats.bLife < OKLIFE ||
			pFriend->bCollapsed || pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			pFriend->pathing.bLevel != pSoldier->pathing.bLevel)
		{
			continue;
		}

		if (AICombatTeam(pSoldier) && !AISameFireteam(pSoldier, pFriend))
			continue;

		const INT32 iDistance = PythSpacesAway(sAnchorGridNo, pFriend->sGridNo);
		if (iDistance > DAY_VISION_RANGE)
			continue;

		UINT8 ubDir = AIDirection(sAnchorGridNo, pFriend->sGridNo);
		if (ubDir >= NUM_WORLD_DIRECTIONS)
			continue;

		INT32 iPressure = __max(15, 90 - 3 * iDistance);
		if (AICheckHasGun(pFriend) && AIGunAmmo(pFriend) > 0)
			iPressure += 10;
		if (AICheckIsLeader(pFriend))
			iPressure += 5;

		pGeometry->usFriendlyPressure[ubDir] = (UINT16)__min(
			65535, (INT32)pGeometry->usFriendlyPressure[ubDir] + iPressure);
	}

	INT32 iPrimary = -1;
	INT32 iSecondary = -1;
	INT32 iStrongestFriendly = -1;
	UINT8 ubStrongThreatSectors = 0;

	for (UINT8 ubDir = 0; ubDir < NUM_WORLD_DIRECTIONS; ++ubDir)
	{
		const INT32 iThreat = pGeometry->usThreatPressure[ubDir];
		const INT32 iFriendly = pGeometry->usFriendlyPressure[ubDir];

		if (iThreat >= 20)
		{
			pGeometry->ubThreatDirectionMask |= (UINT8)(1 << ubDir);
			++ubStrongThreatSectors;
		}
		if (iFriendly >= 20)
			pGeometry->ubFriendlyDirectionMask |= (UINT8)(1 << ubDir);

		if (iThreat > iPrimary)
		{
			iSecondary = iPrimary;
			pGeometry->ubSecondaryThreatDir = pGeometry->ubPrimaryThreatDir;
			iPrimary = iThreat;
			pGeometry->ubPrimaryThreatDir = ubDir;
		}
		else if (iThreat > iSecondary)
		{
			iSecondary = iThreat;
			pGeometry->ubSecondaryThreatDir = ubDir;
		}

		if (iFriendly > iStrongestFriendly)
		{
			iStrongestFriendly = iFriendly;
			pGeometry->ubStrongestFriendlyDir = ubDir;
		}
	}

	if ((pGeometry->ubKnownContacts == 0 &&
		 pGeometry->ubRememberedContacts == 0) ||
		iPrimary <= 0)
	{
		pGeometry->ubPrimaryThreatDir = DIRECTION_IRRELEVANT;
		pGeometry->ubSecondaryThreatDir = DIRECTION_IRRELEVANT;
		return FALSE;
	}

	INT32 iLowestDanger = 0x7fffffff;
	for (UINT8 ubDir = 0; ubDir < NUM_WORLD_DIRECTIONS; ++ubDir)
	{
		const INT32 iDanger = AIGeometryDirectionalDanger(pGeometry, ubDir);
		if (iDanger < iLowestDanger)
		{
			iLowestDanger = iDanger;
			pGeometry->ubSafestDirection = ubDir;
		}
	}

	if (pGeometry->ubSecondaryThreatDir < NUM_WORLD_DIRECTIONS &&
		iSecondary >= 20 &&
		AIGeometryDirectionDelta(
			pGeometry->ubPrimaryThreatDir,
			pGeometry->ubSecondaryThreatDir) >= 2)
	{
		pGeometry->fMultiAngleThreat = TRUE;
	}

	const UINT8 ubOpposite =
		AIGeometryRotate(pGeometry->ubPrimaryThreatDir, NUM_WORLD_DIRECTIONS / 2);
	const INT32 iRearThreat =
		AIGeometryDirectionalThreat(pGeometry, ubOpposite);
	const INT32 iRearSupport =
		AIGeometryDirectionalSupport(pGeometry, ubOpposite);
	pGeometry->sRearSafety = (INT16)__max(-100, __min(100,
		70 - iRearThreat / 5 + iRearSupport / 8));

	UINT8 ubVisibleSectors = 0;
	BOOLEAN fVisibleWideSeparation = FALSE;
	for (UINT8 ubA = 0; ubA < NUM_WORLD_DIRECTIONS; ++ubA)
	{
		if (!(pGeometry->ubVisibleDirectionMask & (1 << ubA)))
			continue;

		++ubVisibleSectors;
		for (UINT8 ubB = ubA + 1; ubB < NUM_WORLD_DIRECTIONS; ++ubB)
		{
			if (!(pGeometry->ubVisibleDirectionMask & (1 << ubB)))
				continue;

			if (AIGeometryDirectionDelta(ubA, ubB) >= 3)
				fVisibleWideSeparation = TRUE;
		}
	}

	// Remembered/heard contacts may make a soldier cautious, but the stronger
	// "I am being enveloped" conclusion needs personally visible geometry.
	pGeometry->fEncirclementPressure =
		fVisibleWideSeparation &&
		((ubVisibleSectors >= 3) ||
		 (ubVisibleSectors >= 2 && ubStrongThreatSectors >= 3));

	const UINT8 ubLeft =
		AIGeometryRotate(pGeometry->ubPrimaryThreatDir, -2);
	const UINT8 ubRight =
		AIGeometryRotate(pGeometry->ubPrimaryThreatDir, 2);

	const INT32 iLeftDanger = AIGeometryDirectionalDanger(pGeometry, ubLeft);
	const INT32 iRightDanger = AIGeometryDirectionalDanger(pGeometry, ubRight);

	pGeometry->sLeftFlankOpportunity = (INT16)__max(-100, __min(100,
		55 + iPrimary / 8 -
		iLeftDanger / 5 -
		(INT32)pGeometry->usFriendlyPressure[ubLeft] / 4));

	pGeometry->sRightFlankOpportunity = (INT16)__max(-100, __min(100,
		55 + iPrimary / 8 -
		iRightDanger / 5 -
		(INT32)pGeometry->usFriendlyPressure[ubRight] / 4));

	return TRUE;
}

INT32 AIGeometryPositionScore(SOLDIERTYPE *pSoldier,
	const AITACTICALGEOMETRY *pGeometry, INT32 sCandidateSpot,
	INT32 sTargetSpot, INT8 bIntent, INT8 bRole)
{
	if (!pSoldier || !pGeometry || TileIsOutOfBounds(sCandidateSpot) ||
		pGeometry->ubPrimaryThreatDir >= NUM_WORLD_DIRECTIONS)
	{
		return 0;
	}

	if (sCandidateSpot == pSoldier->sGridNo)
		return 0;

	UINT8 ubMoveDir = AIDirection(pSoldier->sGridNo, sCandidateSpot);
	if (ubMoveDir >= NUM_WORLD_DIRECTIONS)
		return 0;

	const INT32 iDanger = AIGeometryDirectionalDanger(pGeometry, ubMoveDir);
	const INT32 iSupport = AIGeometryDirectionalSupport(pGeometry, ubMoveDir);
	INT32 iScore = -iDanger / 12 + iSupport / 20;

	if (bIntent == AI_INTENT_FALLBACK || bIntent == AI_INTENT_DISENGAGE)
	{
		if (pGeometry->ubSafestDirection < NUM_WORLD_DIRECTIONS)
		{
			const UINT8 ubDelta = AIGeometryDirectionDelta(
				ubMoveDir, pGeometry->ubSafestDirection);
			iScore += __max(-12, 24 - 8 * (INT32)ubDelta);
		}

		const UINT8 ubRear =
			AIGeometryRotate(pGeometry->ubPrimaryThreatDir, NUM_WORLD_DIRECTIONS / 2);
		if (AIGeometryDirectionDelta(ubMoveDir, ubRear) <= 1)
			iScore += pGeometry->sRearSafety / 4;

		if (pGeometry->fEncirclementPressure)
			iScore += 10;
	}
	else if (bIntent == AI_INTENT_FLANK || bRole == AI_ROLE_FLANKER)
	{
		const UINT8 ubLeft =
			AIGeometryRotate(pGeometry->ubPrimaryThreatDir, -2);
		const UINT8 ubRight =
			AIGeometryRotate(pGeometry->ubPrimaryThreatDir, 2);
		const UINT8 ubLeftDelta = AIGeometryDirectionDelta(ubMoveDir, ubLeft);
		const UINT8 ubRightDelta = AIGeometryDirectionDelta(ubMoveDir, ubRight);

		INT32 iLeft = pGeometry->sLeftFlankOpportunity -
			12 * (INT32)ubLeftDelta;
		INT32 iRight = pGeometry->sRightFlankOpportunity -
			12 * (INT32)ubRightDelta;
		iScore += __max(iLeft, iRight) / 3;
	}
	else if (bIntent == AI_INTENT_PRESS)
	{
		// Pressing straight into the strongest known fire sector is legal, but it
		// should need cover/support advantages elsewhere in the utility model.
		const UINT8 ubDelta = AIGeometryDirectionDelta(
			ubMoveDir, pGeometry->ubPrimaryThreatDir);
		if (ubDelta == 0)
			iScore -= __min((INT32)18, iDanger / 15);
		else if (ubDelta == 1)
			iScore -= __min((INT32)10, iDanger / 20);
	}

	if (bRole == AI_ROLE_SCREEN && pGeometry->ubSafestDirection < NUM_WORLD_DIRECTIONS)
	{
		const UINT8 ubRear =
			AIGeometryRotate(pGeometry->ubPrimaryThreatDir, NUM_WORLD_DIRECTIONS / 2);
		if (AIGeometryDirectionDelta(ubMoveDir, ubRear) <= 1)
			iScore += 8;
	}

	if (!TileIsOutOfBounds(sTargetSpot) &&
		bRole == AI_ROLE_SUPPORT &&
		pGeometry->ubStrongestFriendlyDir < NUM_WORLD_DIRECTIONS)
	{
		// Support should preserve the base of fire rather than chase the same open
		// flank as the maneuver element.
		if (AIGeometryDirectionDelta(
			ubMoveDir, pGeometry->ubStrongestFriendlyDir) <= 1)
			iScore += 5;
	}

	return __max(-45, __min(45, iScore));
}

INT8 AIPreferredFlankAction(SOLDIERTYPE *pSoldier, INT32 sTargetSpot)
{
	if (!pSoldier || TileIsOutOfBounds(sTargetSpot))
		return AI_ACTION_NONE;

	AITACTICALGEOMETRY Geometry;
	if (!AIBuildTacticalGeometry(pSoldier, pSoldier->sGridNo, &Geometry))
		return AI_ACTION_NONE;

	if (Geometry.fEncirclementPressure ||
		Geometry.ubPrimaryThreatDir >= NUM_WORLD_DIRECTIONS)
	{
		return AI_ACTION_NONE;
	}

	const INT32 iLeft = Geometry.sLeftFlankOpportunity;
	const INT32 iRight = Geometry.sRightFlankOpportunity;

	if (__max(iLeft, iRight) < -10)
		return AI_ACTION_NONE;

	if (iLeft >= iRight + 5)
		return AI_ACTION_FLANK_LEFT;
	if (iRight >= iLeft + 5)
		return AI_ACTION_FLANK_RIGHT;

	return AI_ACTION_NONE;
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

	AITACTICALGEOMETRY Geometry;
	if (AIBuildTacticalGeometry(pSoldier, pSoldier->sGridNo, &Geometry))
	{
		INT8 bGeometryIntent = AI_INTENT_HOLD;
		INT8 bGeometryRole = AI_ROLE_SUPPORT;
		// The caller-specific intent/role are applied in AIScoreTacticalPosition.
		// This base geometry term captures only directional safety/support.
		pFeatures->sGeometryScore = (INT16)AIGeometryPositionScore(
			pSoldier, &Geometry, sCandidateSpot, sTargetSpot,
			bGeometryIntent, bGeometryRole);
	}
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

	AITACTICALGEOMETRY Geometry;
	if (AIBuildTacticalGeometry(pSoldier, pSoldier->sGridNo, &Geometry))
	{
		iScore += AIGeometryPositionScore(
			pSoldier, &Geometry, sCandidateSpot, sTargetSpot, bIntent, bRole);
	}
	else
	{
		iScore += pFeatures->sGeometryScore;
	}

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
	memset(gAIContactMemory, 0, sizeof(gAIContactMemory));

	for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
	{
		gAITaskReservations[i].sTargetGridNo = NOWHERE;
		gAITaskReservations[i].ubTargetID = NOBODY;
		gAIShortPlans[i].Plan.sTargetGridNo = NOWHERE;
		gAIShortPlans[i].Plan.ubTargetID = NOBODY;
		gAIContactTracker[i].sLastDecisionGridNo = NOWHERE;
		for (UINT16 j = 0; j < MAX_NUM_SOLDIERS; ++j)
			gAIContactMemory[i][j].sLastKnownGridNo = NOWHERE;
	}
}
