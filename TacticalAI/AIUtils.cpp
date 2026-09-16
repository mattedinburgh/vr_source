#ifdef PRECOMPILEDHEADERS
	#include "AI All.h"
#else
	#include "ai.h"
	#include "Weapons.h"
	#include "opplist.h"
	#include "Points.h"
	#include "PathAI.h"
	#include "WorldMan.h"
	#include "AIInternals.h"
	#include "Items.h"
	#include "message.h"
	#include "los.h"
	#include "assignments.h"
	#include "Soldier Functions.h"
	#include "Points.h"
	#include "GameSettings.h"
	#include "Buildings.h"
	#include "Soldier macros.h"
	#include "Render Fun.h"
	#include "strategicmap.h"
	#include "environment.h"
	#include "lighting.h"
	#include "Soldier Create.h"
	#include "SkillCheck.h"		// added by SANDRO
	#include "Vehicles.h"		// added by silversurfer
	// sevenfm:
	#include "Game Clock.h"
	#include "Rotting Corpses.h"
	#include "wcheck.h"
	#include "Drugs And Alcohol.h"
	#include "Sound Control.h"
	#include "SmokeEffects.h"
	#include "Structure Wrap.h"
	#include "Interface.h"
#endif

#include "Strategic Movement.h"
#include "VRAnalytics.h"

#include <map>

//////////////////////////////////////////////////////////////////////////////
// SANDRO - In this file, all APBPConstants[AP_CROUCH] and APBPConstants[AP_PRONE] were changed to GetAPsCrouch() and GetAPsProne()
//			On the bottom here, there are these functions made
//////////////////////////////////////////////////////////////////////

//
// CJC's DG->JA2 conversion notes
//
// Commented out:
//
// InWaterOrGas - gas stuff
// RoamingRange - point patrol stuff

extern UINT16 PickSoldierReadyAnimation( SOLDIERTYPE *pSoldier, BOOLEAN fEndReady, BOOLEAN fHipStance );
extern SECTOR_EXT_DATA	SectorExternalData[256][4];

//rain
extern INT8 gbCurrentRainIntensity;
extern BOOLEAN gfLightningInProgress;
extern BOOLEAN gfHaveSeenSomeone;
extern UINT8 ubRealAmbientLightLevel;
//end rain

// Planning-only shared threat picture. This never changes firearm target legality.
static INT32 AIPrimaryPlanningThreatSpot(
	SOLDIERTYPE *pSoldier, INT8 *pbLevel, UINT8 *pubConfidence)
{
	if (pbLevel) *pbLevel = 0;
	if (pubConfidence) *pubConfidence = 0;
	if (!pSoldier)
		return NOWHERE;

	// Enemy fireteams pick one primary tactical axis from the bounded local picture.
	// Priority depends only on legal report confidence plus known friendly geometry:
	// which believed contact currently constrains the element most if left unanswered.
	if (pSoldier->bTeam == ENEMY_TEAM)
	{
		INT32 sBestGrid = NOWHERE;
		INT8 bBestLevel = 0;
		UINT8 ubBestConfidence = 0;
		INT32 iBestScore = -1000000;

		for (UINT16 uiOpponent = 0; uiOpponent < TOTAL_SOLDIERS; ++uiOpponent)
		{
			SOLDIERTYPE *pOpponent = MercPtrs[uiOpponent];
			if (!pOpponent)
				continue;

			INT32 sReportedGrid = NOWHERE;
			INT8 bReportedLevel = 0;
			UINT8 ubConfidence = 0;
			INT8 bKnowledge = NOT_HEARD_OR_SEEN;
			if (!AIPlanningContactForOpponent(
				pSoldier, (UINT8)uiOpponent, &sReportedGrid, &bReportedLevel,
				&ubConfidence, &bKnowledge))
			{
				continue;
			}

			const BOOLEAN fDirectVisualContact =
				PersonalKnowledge(pSoldier, (UINT8)uiOpponent) == SEEN_CURRENTLY &&
				LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;
			if (fDirectVisualContact &&
				(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
				 pSoldier->bSide == pOpponent->bSide ||
				 !ValidOpponent(pSoldier, pOpponent)))
			{
				continue;
			}

			if (TileIsOutOfBounds(sReportedGrid))
				continue;

			INT32 iNearestFriend = 0x7FFFFFFF;
			INT32 iExposedFriends = 0;
			INT32 iCloseFriends = 0;

			for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
				iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
			{
				SOLDIERTYPE *pFriend = MercPtrs[iCounter];
				if (!pFriend || !pFriend->bActive || !pFriend->bInSector ||
					pFriend->stats.bLife < OKLIFE ||
					!AISameFireteam(pSoldier, pFriend))
				{
					continue;
				}

				INT32 iDistance = PythSpacesAway(pFriend->sGridNo, sReportedGrid);
				iNearestFriend = __min(iNearestFriend, iDistance);
				if (iDistance <= TACTICAL_RANGE / 2)
					++iCloseFriends;

				if (iDistance <= MAX_VISION_RANGE &&
					LocationToLocationLineOfSightTest(
						sReportedGrid, bReportedLevel,
						pFriend->sGridNo, pFriend->pathing.bLevel,
						TRUE, MAX_VISION_RANGE))
				{
					++iExposedFriends;
				}
			}

			if (iNearestFriend == 0x7FFFFFFF)
				iNearestFriend = PythSpacesAway(pSoldier->sGridNo, sReportedGrid);

			INT32 iScore = (INT32)ubConfidence * 5;
			iScore += 35 * iExposedFriends;
			iScore += 12 * iCloseFriends;
			iScore += __max(0, 80 - 4 * iNearestFriend);

			// Visually established reports outrank comparably dangerous anonymous noises.
			if (bKnowledge == SEEN_CURRENTLY)
				iScore += 25;
			else if (bKnowledge == SEEN_THIS_TURN)
				iScore += 15;
			else if (bKnowledge <= HEARD_THIS_TURN)
				iScore -= 10;

			if (iScore > iBestScore)
			{
				iBestScore = iScore;
				sBestGrid = sReportedGrid;
				bBestLevel = bReportedLevel;
				ubBestConfidence = ubConfidence;
			}
		}

		if (!TileIsOutOfBounds(sBestGrid))
		{
			if (pbLevel) *pbLevel = bBestLevel;
			if (pubConfidence) *pubConfidence = ubBestConfidence;
			return sBestGrid;
		}
	}

	INT8 bKnownLevel = 0;
	INT32 sKnown = ClosestKnownOpponent(pSoldier, NULL, &bKnownLevel);
	if (pbLevel) *pbLevel = bKnownLevel;
	return sKnown;
}
static BOOLEAN AIPersonallyConfirmedNonThreat(
	SOLDIERTYPE *pSoldier, SOLDIERTYPE *pOpponent)
{
	if (!pSoldier || !pOpponent ||
		PersonalKnowledge(pSoldier, pOpponent->ubID) != SEEN_CURRENTLY ||
		LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) <= 0)
	{
		return FALSE;
	}

	if (!ValidOpponent(pSoldier, pOpponent) ||
		(pOpponent->usSoldierFlagMask & SOLDIER_POW))
	{
		return TRUE;
	}

	return IS_MERC_BODY_TYPE(pOpponent) &&
		!pOpponent->IsZombie() &&
		pOpponent->IsUnconscious();
}

BOOLEAN AISelectKnownArtilleryTarget(SOLDIERTYPE *pSoldier, INT32 *psTargetGridNo)
{
	if (!pSoldier || !psTargetGridNo || !AICombatTeam(pSoldier) || gbWorldSectorZ > 0)
		return FALSE;

	*psTargetGridNo = NOWHERE;

	INT32 iStrikeRadius = __max(2, (INT32)gSkillTraitValues.usVOMortarRadius - 2);
	iStrikeRadius = __min(iStrikeRadius, TACTICAL_RANGE / 2);
	INT32 iFriendlySafetyRadius = __max(4, (INT32)gSkillTraitValues.usVOMortarRadius);
	iFriendlySafetyRadius = __min(iFriendlySafetyRadius, TACTICAL_RANGE / 2);

	INT32 iBestScore = 0;

	for (UINT16 uiCandidate = 0; uiCandidate < MAX_NUM_SOLDIERS; ++uiCandidate)
	{
		SOLDIERTYPE *pCandidate = MercPtrs[uiCandidate];
		if (!pCandidate || pCandidate == pSoldier)
			continue;

		INT32 sCandidateSpot = NOWHERE;
		INT8 bCandidateLevel = 0;
		INT8 bCandidateKnowledge = NOT_HEARD_OR_SEEN;
		UINT8 ubCandidateConfidence = 0;
		if (!AIPlanningContactForOpponent(
			pSoldier, pCandidate->ubID, &sCandidateSpot, &bCandidateLevel,
			&ubCandidateConfidence, &bCandidateKnowledge))
		{
			continue;
		}

		// Do not spend scarce indirect fire on very weak/old single contact reports.
		if (ubCandidateConfidence < 38 || TileIsOutOfBounds(sCandidateSpot))
			continue;

		const BOOLEAN fCandidateDirect =
			PersonalKnowledge(pSoldier, pCandidate->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pCandidate, CALC_FROM_ALL_DIRS) > 0;

		if (fCandidateDirect &&
			(CONSIDERED_NEUTRAL(pSoldier, pCandidate) ||
			 pSoldier->bSide == pCandidate->bSide ||
			 pCandidate->ubBodyType == CROW))
		{
			continue;
		}

		if (AIPersonallyConfirmedNonThreat(pSoldier, pCandidate))
			continue;

		BOOLEAN fFriendlyDanger = FALSE;
		for (UINT16 uiFriend = 0; uiFriend < MAX_NUM_SOLDIERS; ++uiFriend)
		{
			SOLDIERTYPE *pFriend = MercPtrs[uiFriend];
			if (!pFriend || !pFriend->bActive || !pFriend->bInSector ||
				pFriend->stats.bLife <= 0 ||
				pFriend->aiData.bNeutral ||
				pFriend->bSide != pSoldier->bSide ||
				(pSoldier->bTeam == ENEMY_TEAM && !AISameFireteam(pSoldier, pFriend)))
			{
				continue;
			}

			if (PythSpacesAway(pFriend->sGridNo, sCandidateSpot) <= iFriendlySafetyRadius)
			{
				fFriendlyDanger = TRUE;
				break;
			}

			if (pFriend->aiData.bAction >= FIRST_MOVEMENT_ACTION &&
				pFriend->aiData.bAction <= LAST_MOVEMENT_ACTION &&
				!TileIsOutOfBounds(pFriend->aiData.usActionData) &&
				PythSpacesAway(pFriend->aiData.usActionData, sCandidateSpot) <= iFriendlySafetyRadius)
			{
				fFriendlyDanger = TRUE;
				break;
			}
		}

		if (fFriendlyDanger)
			continue;

		INT32 iScore = 0;
		UINT8 ubCredibleContacts = 0;

		for (UINT16 uiOpponent = 0; uiOpponent < MAX_NUM_SOLDIERS; ++uiOpponent)
		{
			SOLDIERTYPE *pOpponent = MercPtrs[uiOpponent];
			if (!pOpponent || pOpponent == pSoldier)
				continue;

			INT32 sKnownSpot = NOWHERE;
			INT8 bKnownLevel = 0;
			INT8 bKnowledge = NOT_HEARD_OR_SEEN;
			UINT8 ubConfidence = 0;
			if (!AIPlanningContactForOpponent(
				pSoldier, pOpponent->ubID, &sKnownSpot, &bKnownLevel,
				&ubConfidence, &bKnowledge))
			{
				continue;
			}

			const BOOLEAN fOpponentDirect =
				PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
				LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;
			if (fOpponentDirect &&
				(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
				 pSoldier->bSide == pOpponent->bSide ||
				 pOpponent->ubBodyType == CROW))
			{
				continue;
			}

			if (AIPersonallyConfirmedNonThreat(pSoldier, pOpponent))
				continue;

			if (TileIsOutOfBounds(sKnownSpot) ||
				PythSpacesAway(sKnownSpot, sCandidateSpot) > iStrikeRadius)
			{
				continue;
			}

			iScore += ubConfidence;
			if (ubConfidence >= 50)
				++ubCredibleContacts;
		}

		// Require a cluster: local spotters can call the strike, but one speculative
		// remembered contact is not enough.
		if (ubCredibleContacts < 2)
			continue;

		iScore -= TerrainDensity(sCandidateSpot, bCandidateLevel, 2, FALSE);

		if (iScore > iBestScore)
		{
			iBestScore = iScore;
			*psTargetGridNo = sCandidateSpot;
		}
	}

	return !TileIsOutOfBounds(*psTargetGridNo);
}

static BOOLEAN AIEnemyResponderEligible(SOLDIERTYPE *pSoldier)
{
	return AIEnemyFireteamEligible(pSoldier) &&
		pSoldier->stats.bLife >= OKLIFE &&
		!pSoldier->bCollapsed &&
		!pSoldier->bBreathCollapsed &&
		!(pSoldier->usSoldierFlagMask & SOLDIER_POW) &&
		!(pSoldier->flags.uiStatusFlags & SOLDIER_COWERING) &&
		!AIDisengagementActive(pSoldier) &&
		!AIEscapeActive(pSoldier) &&
		!AIEnemyFixedMissionRole(pSoldier);
}

static BOOLEAN AIEnemyResponderEngagedAwayFromContact(SOLDIERTYPE *pSoldier, INT32 sContactSpot)
{
	if (!AIEnemyFireteamEligible(pSoldier) || TileIsOutOfBounds(sContactSpot) ||
		pSoldier->stats.bLife < OKLIFE || pSoldier->bCollapsed || pSoldier->bBreathCollapsed ||
		(pSoldier->usSoldierFlagMask & SOLDIER_POW))
	{
		return FALSE;
	}

	BOOLEAN fEngaged = pSoldier->aiData.bUnderFire ||
		pSoldier->aiData.bOppCnt > 0 ||
		GuySawEnemy(pSoldier, SEEN_LAST_TURN);
	if (!fEngaged)
		return FALSE;

	// Use only legitimate known-opponent information to decide whether this is a
	// separate fight. If the source of current fire is unknown, physical separation
	// from the response contact is enough to keep this element committed in place.
	INT32 sOwnContact = ClosestKnownOpponent(pSoldier, NULL, NULL);
	if (!TileIsOutOfBounds(sOwnContact))
		return PythSpacesAway(sOwnContact, sContactSpot) > TACTICAL_RANGE / 2;

	return PythSpacesAway(pSoldier->sGridNo, sContactSpot) >
		__max(6, DAY_VISION_RANGE / 4);
}

static BOOLEAN AIFireteamCommittedElsewhere(UINT8 ubFireteam, INT32 sContactSpot)
{
	if (ubFireteam == AI_FIRETEAM_NONE || TileIsOutOfBounds(sContactSpot))
		return FALSE;

	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!AIEnemyFireteamEligible(pFriend) || pFriend->ubID >= MAX_NUM_SOLDIERS ||
			guiAIFireteamIdentity[pFriend->ubID] != pFriend->uiUniqueSoldierIdValue ||
			gubAIFireteam[pFriend->ubID] != ubFireteam)
		{
			continue;
		}

		if (AIEnemyResponderEngagedAwayFromContact(pFriend, sContactSpot))
			return TRUE;
	}

	return FALSE;
}

static UINT8 AIFireteamDeployableCountById(UINT8 ubFireteam, INT32 sContactSpot)
{
	if (ubFireteam == AI_FIRETEAM_NONE || AIFireteamCommittedElsewhere(ubFireteam, sContactSpot))
		return 0;

	UINT8 ubCount = 0;
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!AIEnemyResponderEligible(pFriend) ||
			pFriend->ubID >= MAX_NUM_SOLDIERS ||
			guiAIFireteamIdentity[pFriend->ubID] != pFriend->uiUniqueSoldierIdValue ||
			gubAIFireteam[pFriend->ubID] != ubFireteam)
		{
			continue;
		}
		++ubCount;
	}
	return ubCount;
}

static INT32 AIFireteamDeployableDistanceToSpot(UINT8 ubFireteam, INT32 sSpot)
{
	if (AIFireteamCommittedElsewhere(ubFireteam, sSpot))
		return 10000;

	INT32 iBest = 10000;
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!AIEnemyResponderEligible(pFriend) ||
			pFriend->ubID >= MAX_NUM_SOLDIERS ||
			guiAIFireteamIdentity[pFriend->ubID] != pFriend->uiUniqueSoldierIdValue ||
			gubAIFireteam[pFriend->ubID] != ubFireteam)
		{
			continue;
		}
		iBest = __min(iBest, PythSpacesAway(pFriend->sGridNo, sSpot));
	}
	return iBest;
}

BOOLEAN AIFireteamShouldHoldReserve(SOLDIERTYPE *pSoldier, INT32 sContactSpot, UINT8 ubResponseLimit)
{
	if (!AIEnemyFireteamEligible(pSoldier) || TileIsOutOfBounds(sContactSpot))
		return FALSE;

	// Reserve allocation is a query, not a fireteam mutation. A viable one/two-man
	// remnant stays out of an independent QRF response and will perform the actual
	// reattachment through DecideFireteamCohesionAction on its own decision turn.
	if (AIFireteamRegroupingStrength(pSoldier) <= 2 &&
		AISelectFireteamRemnantDestination(pSoldier, NULL) != AI_FIRETEAM_NONE)
	{
		// Reserve allocation is queried frequently. Do not run full route/path
		// validation here; the cohesion action validates reachability immediately
		// before any remnant membership or escape state is changed.
		return TRUE;
	}

	// Fixed sentries and snipers do not consume a mobile response budget. They may
	// still fight normally if contact reaches their position, but they do not abandon
	// their mission merely because another element is responding.
	if (!AIEnemyResponderEligible(pSoldier))
		return TRUE;

	UINT8 ubMine = AIFireteamId(pSoldier);
	UINT8 ubMyReady = AIFireteamDeployableCountById(ubMine, sContactSpot);
	if (ubMyReady == 0)
		return TRUE;

	// A General's command group is a tactical reserve, not the default QRF. For a
	// remote contact, keep it intact whenever another coherent deployable element
	// can respond. Direct/local contact bypasses this helper in the caller, and if
	// every other element is committed or broken the command group still releases.
	if (pSoldier->bTeam == ENEMY_TEAM && AIFireteamHasSoldierFlag(ubMine, SOLDIER_VIP))
	{
		for (UINT8 ubTeam = 1; ubTeam < gubAINextFireteam; ++ubTeam)
		{
			if (ubTeam == ubMine || gbAIFireteamTeam[ubTeam] != pSoldier->bTeam)
				continue;
			if (AIFireteamDeployableCountById(ubTeam, sContactSpot) > 0)
				return TRUE;
		}
	}

	INT32 iMine = AIFireteamDeployableDistanceToSpot(ubMine, sContactSpot);
	UINT16 usCloserReady = 0;

	for (UINT8 ubTeam = 1; ubTeam < gubAINextFireteam; ++ubTeam)
	{
		if (ubTeam == ubMine || gbAIFireteamTeam[ubTeam] != pSoldier->bTeam)
			continue;

		UINT8 ubReady = AIFireteamDeployableCountById(ubTeam, sContactSpot);
		if (ubReady == 0)
			continue;

		INT32 iDistance = AIFireteamDeployableDistanceToSpot(ubTeam, sContactSpot);
		if (iDistance < iMine || (iDistance == iMine && ubTeam < ubMine))
			usCloserReady += ubReady;
	}

	// Response is now element-based rather than soldier-ID based. The nearest
	// deployable fireteam receives the first mission as a whole; we do not peel two
	// or three men away from it merely to hit an exact numerical budget.
	if (usCloserReady == 0)
		return FALSE;

	// If already-released elements satisfy the current response budget, this whole
	// fireteam stays in reserve for the next escalation.
	if (usCloserReady >= ubResponseLimit)
		return TRUE;

	// For later waves, release the next complete fireteam only when the response
	// budget calls for a meaningful fraction of that element. This permits a small
	// cohesion overrun while preventing a one-man budget increase from dragging an
	// entire fresh squad into the fight.
	UINT16 usNeeded = (UINT16)ubResponseLimit - usCloserReady;
	UINT16 usReleaseThreshold = (UINT16)__max(2,
		((INT32)ubMyReady + 1) / 2);

	return (usNeeded < usReleaseThreshold);
}

INT8 DecideFireteamCohesionAction(SOLDIERTYPE *pSoldier, BOOLEAN fCanMove)
{
	if (!fCanMove || !gfTurnBasedAI || !AIEnemyFireteamEligible(pSoldier) ||
		pSoldier->stats.bLife < OKLIFE ||
		pSoldier->bCollapsed ||
		pSoldier->bBreathCollapsed ||
		(pSoldier->flags.uiStatusFlags & SOLDIER_COWERING))
		return AI_ACTION_NONE;

	UINT8 ubBefore = AIFireteamRegroupingStrength(pSoldier);
	BOOLEAN fWasRemnant = (ubBefore > 0 && ubBefore <= 2);
	BOOLEAN fSmallUnitTeam = AISmallUnitTeamMode(pSoldier);
	UINT8 ubPlannedTarget = fWasRemnant ?
		AISelectFireteamRemnantDestination(pSoldier, NULL) : AI_FIRETEAM_NONE;
	BOOLEAN fRemnantCanReattach = (ubPlannedTarget != AI_FIRETEAM_NONE);
	BOOLEAN fEnemyRemnantCanReattach =
		pSoldier->bTeam == ENEMY_TEAM && fRemnantCanReattach;
	BOOLEAN fRecentlyReattached = AIRecentlyReattachedFireteamRemnant(pSoldier);

	// Militia withdrawal is handled by the militia disengagement/consolidation system.
	// Fireteam regrouping must never reinterpret an explicit player Retreat, nor should
	// a stale recent-reattachment marker pull a withdrawing militia soldier back inward.
	if (pSoldier->bTeam == MILITIA_TEAM &&
		(AIDisengagementActive(pSoldier) || AIEscapeActive(pSoldier)))
	{
		return AI_ACTION_NONE;
	}

	// Existing enemy break-contact intent normally owns the decision. The exception is
	// an enemy remnant that has a real local element it can physically attempt to join.
	if ((AIDisengagementActive(pSoldier) || AIEscapeActive(pSoldier)) &&
		!fEnemyRemnantCanReattach && !fRecentlyReattached)
	{
		return AI_ACTION_NONE;
	}

	// A player Hold order is authoritative for militia. Enemy fixed sentries/snipers may
	// still reattach after their element shatters; that is an autonomous faction behavior.
	if (pSoldier->bTeam == MILITIA_TEAM && pSoldier->aiData.bOrders == STATIONARY)
		return AI_ACTION_NONE;

	if ((pSoldier->aiData.bOrders == STATIONARY ||
		 (pSoldier->aiData.bOrders == SNIPER && !fSmallUnitTeam)) &&
		!fRemnantCanReattach && !fRecentlyReattached)
	{
		return AI_ACTION_NONE;
	}

	// Large elements do not abandon active firing positions just to tidy formation.
	// For a 2-5 man remnant, however, cohesion is survival: they may close on a
	// teammate through a safe route even while the local fight is active.
	if (!fSmallUnitTeam && !fRecentlyReattached && !fRemnantCanReattach &&
		(pSoldier->aiData.bUnderFire || pSoldier->aiData.bOppCnt > 0 ||
		 pSoldier->IsFlanking() || GuySawEnemy(pSoldier, SEEN_LAST_TURN)))
	{
		return AI_ACTION_NONE;
	}

	SOLDIERTYPE *pAnchor = NULL;
	INT32 iBest = 10000;
	BOOLEAN fEngagedAnchor = FALSE;
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier || !AIEnemyFireteamEligible(pFriend) ||
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			AIDisengagementActive(pFriend) || AIEscapeActive(pFriend))
			continue;

		BOOLEAN fCorrectElement = FALSE;
		if (fRemnantCanReattach)
		{
			fCorrectElement =
				pFriend->ubID < MAX_NUM_SOLDIERS &&
				guiAIFireteamIdentity[pFriend->ubID] == pFriend->uiUniqueSoldierIdValue &&
				gubAIFireteam[pFriend->ubID] == ubPlannedTarget;
		}
		else
		{
			fCorrectElement = AISameFireteam(pSoldier, pFriend);
		}
		if (!fCorrectElement)
			continue;

		BOOLEAN fEngaged = pFriend->aiData.bUnderFire || pFriend->aiData.bOppCnt > 0 ||
			GuySawEnemy(pFriend, SEEN_LAST_TURN);
		INT32 iDistance = PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo);
		if (fEngaged && (!fEngagedAnchor || iDistance < iBest))
		{
			fEngagedAnchor = TRUE;
			pAnchor = pFriend;
			iBest = iDistance;
		}
		else if (!fEngagedAnchor && iDistance < iBest)
		{
			pAnchor = pFriend;
			iBest = iDistance;
		}
	}

	if (!pAnchor || (!fSmallUnitTeam && !fWasRemnant && !fRecentlyReattached && !fEngagedAnchor))
		return AI_ACTION_NONE;

	INT32 iSupportBubble = fSmallUnitTeam ?
		__max(6, DAY_VISION_RANGE / 3) : __max(8, DAY_VISION_RANGE / 2);

	// Small remnants keep a tighter mutual-support bubble: separated enough not to
	// stack on one tile, close enough that no one fights an isolated private battle.
	if (iBest <= iSupportBubble)
	{
		if (fRemnantCanReattach)
			AIAbsorbFireteamRemnant(pSoldier);
		return AI_ACTION_NONE;
	}

	BOOLEAN fCautiousMove = fSmallUnitTeam || fEngagedAnchor || fRecentlyReattached ||
		fRemnantCanReattach || pSoldier->aiData.bUnderFire || pSoldier->aiData.bOppCnt > 0;
	INT8 bReserveAP = fCautiousMove ?
		(GetAPsCrouch(pSoldier, TRUE) + GetAPsToLook(pSoldier)) : 0;
	UINT8 ubFlags = fCautiousMove ? FLAG_CAUTIOUS : 0;

	pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(
		pSoldier, pAnchor->sGridNo, bReserveAP, AI_ACTION_SEEK_FRIEND, ubFlags);

	if (TileIsOutOfBounds(pSoldier->aiData.usActionData) ||
		pSoldier->aiData.usActionData == pSoldier->sGridNo)
	{
		return AI_ACTION_NONE;
	}

	if (!CheckNPCDestination(pSoldier, pSoldier->aiData.usActionData))
		return AI_ACTION_NONE;

	if (fCautiousMove &&
		!AIKnownRouteExposureAcceptable(
			pSoldier, pSoldier->aiData.usActionData, AI_ACTION_SEEK_FRIEND,
			120, 60, 80))
	{
		return AI_ACTION_NONE;
	}

	if (fCautiousMove)
	{
		UINT16 usCurrentExposure = AIKnownThreatExposure(
			pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
		UINT16 usMoveExposure = AIKnownThreatExposure(
			pSoldier, pSoldier->aiData.usActionData, pSoldier->pathing.bLevel);

		UINT16 usAllowedIncrease = fSmallUnitTeam ? 70 :
			((fRecentlyReattached || fRemnantCanReattach) ? 90 : 150);
		if (usMoveExposure > usCurrentExposure + usAllowedIncrease &&
			!AnyCoverAtSpot(pSoldier, pSoldier->aiData.usActionData))
		{
			return AI_ACTION_NONE;
		}
	}

	// Commit only after a legal, acceptably exposed move toward the selected element
	// exists. Failed pathing therefore leaves the old fireteam/retreat state intact.
	if (fRemnantCanReattach && !AIAbsorbFireteamRemnant(pSoldier))
		return AI_ACTION_NONE;

	if ((fRecentlyReattached || fRemnantCanReattach) && AIEscapeActive(pSoldier))
		AIClearEscapeState(pSoldier);

	if (fCautiousMove)
		pSoldier->aiData.fAIFlags |= AI_CAUTIOUS;

	return AI_ACTION_SEEK_FRIEND;
}

// Chunk 1: battlefield-situation awareness. These helpers expose information to
// later AI decisions but deliberately do not change actions on their own.
// Local calculations use TACTICAL_RANGE so they scale with JA2's existing AI
// distance model rather than inventing a real-world metre conversion.
UINT8 AIObservedRecentCasualties(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier))
		return 0;

	INT32 iLosses = CountCorpses(pSoldier, pSoldier->sGridNo, TACTICAL_RANGE, TRUE, TRUE);

	// A downed friendly is an immediate local casualty too. Friendly locations and
	// status are information the legacy AI already assumes to be available.
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (pFriend &&
			pFriend != pSoldier &&
			pFriend->bActive &&
			pFriend->bInSector &&
			pFriend->stats.bLife > 0 &&
			AIResponderKnowsCasualty(pSoldier, pFriend) &&
			(pFriend->stats.bLife < OKLIFE ||
			 pFriend->bCollapsed ||
			 pFriend->bBreathCollapsed) &&
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) <= TACTICAL_RANGE)
		{
			++iLosses;
		}
	}

	return (UINT8)__min((INT32)255, iLosses);
}

UINT8 AILocalCasualtyPercent(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier))
		return 0;

	INT32 iLosses = CountCorpses(pSoldier, pSoldier->sGridNo, TACTICAL_RANGE, TRUE, TRUE);
	INT32 iPresent = 0;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend ||
			!pFriend->bActive ||
			!pFriend->bInSector ||
			pFriend->stats.bLife <= 0 ||
			!AIResponderKnowsCasualty(pSoldier, pFriend) ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > TACTICAL_RANGE)
		{
			continue;
		}

		if (pFriend->stats.bLife < OKLIFE ||
			pFriend->bCollapsed ||
			pFriend->bBreathCollapsed)
			++iLosses;
		else
			++iPresent;
	}

	if (iLosses + iPresent == 0)
		return 0;

	return (UINT8)__min((INT32)100, (100 * iLosses) / (iLosses + iPresent));
}

UINT8 AIFriendlyCasualtyPercent(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier))
		return 0;

	// Ordinary morale, disengagement and battle-ratio decisions are local.
	// Sector-wide enemy losses belong only in the explicit true-last-survivor
	// check below; otherwise a remote fireteam would instantly inherit casualties
	// it never observed simply because another element was destroyed elsewhere.
	return AILocalCasualtyPercent(pSoldier);
}

// Strength is expressed in certainty points: 100 is one fully known combatant.
// Friendly strength is local-awareness bounded; opponent strength is derived only
// from personal/public JA2 knowledge and never from hidden sector totals.
UINT16 AIPerceivedFriendlyStrength(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier))
		return 0;

	UINT32 uiStrength = 0;

	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || !pFriend->bActive || !pFriend->bInSector ||
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW))
		{
			continue;
		}

		BOOLEAN fSameTeam = (pFriend->bTeam == pSoldier->bTeam);
		BOOLEAN fVisiblePlayerSupport =
			pSoldier->bTeam == MILITIA_TEAM && pFriend->bTeam == OUR_TEAM;
		if (!fSameTeam && !fVisiblePlayerSupport)
			continue;

		INT32 iDistance = PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo);
		if (iDistance > TACTICAL_RANGE)
			continue;

		if (fSameTeam)
		{
			if (!AIResponderKnowsCasualty(pSoldier, pFriend))
				continue;
		}
		else
		{
			// Player mercs contribute to militia force-ratio judgment only when their
			// presence is directly observable. They do not donate opponent knowledge.
			if (iDistance > 1 && LOS_Raised(pSoldier, pFriend, CALC_FROM_ALL_DIRS) <= 0)
				continue;
		}

		// Preserve the existing scale (100 = one fresh combatant), but assess actual
		// current combat power rather than treating every conscious body as identical.
		INT32 iReadiness = 100;

		if (pFriend->stats.bLifeMax > 0)
		{
			INT32 iLifePercent = (100 * pFriend->stats.bLife) / pFriend->stats.bLifeMax;
			if (iLifePercent < 50)
				iReadiness = iReadiness * 70 / 100;
			else if (iLifePercent < 75)
				iReadiness = iReadiness * 85 / 100;
		}

		if (pFriend->bBreath < 25)
			iReadiness = iReadiness * 70 / 100;
		else if (pFriend->bBreath < 50)
			iReadiness = iReadiness * 85 / 100;

		INT32 iShockPercent = ShockLevelPercent(pFriend);
		if (iShockPercent >= 75)
			iReadiness = iReadiness * 60 / 100;
		else if (iShockPercent >= 50)
			iReadiness = iReadiness * 75 / 100;
		else if (iShockPercent >= 25)
			iReadiness = iReadiness * 90 / 100;

		if (pFriend->flags.uiStatusFlags & SOLDIER_COWERING)
			iReadiness = iReadiness * 50 / 100;

		// Current combat power matters more than historical body count. Experienced,
		// accurate troops with good weapons and a defensible firing position should
		// correctly perceive that they can still dominate a battered opposing force.
		INT32 iCombatQuality = 100;
		iCombatQuality += ((INT32)pFriend->stats.bMarksmanship - 70) / 3;
		iCombatQuality += ((INT32)pFriend->stats.bExpLevel - 5) * 3;

		if (Item[pFriend->inv[HANDPOS].usItem].usItemClass & IC_WEAPON)
			iCombatQuality += ((INT32)Weapon[pFriend->inv[HANDPOS].usItem].ubDeadliness - 20) / 3;

		if (AnyCoverAtSpot(pFriend, pFriend->sGridNo))
			iCombatQuality += 10;
		if (SightCoverAtSpot(pFriend, pFriend->sGridNo, FALSE))
			iCombatQuality += 5;

		iCombatQuality = __max(85, __min(140, iCombatQuality));
		iReadiness = iReadiness * iCombatQuality / 100;

		// Do not let retreat state create a runaway feedback loop. A soldier who has
		// started disengaging is still armed and contributes covering fire until he
		// actually leaves the local fight. Distance naturally removes him afterwards.
		if (fSameTeam)
		{
			if (AIEscapeActive(pFriend))
				iReadiness = iReadiness * 60 / 100;
			else if (AIDisengagementActive(pFriend))
				iReadiness = iReadiness * 80 / 100;
		}

		uiStrength += (UINT32)__max(20, __min(140, iReadiness));
	}

	return (UINT16)__min((UINT32)65535, uiStrength);
}

UINT16 AIPerceivedEnemyStrength(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier))
		return 0;

	UINT32 uiStrength = 0;

	for (UINT16 uiLoop = 0; uiLoop < MAX_NUM_SOLDIERS; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercPtrs[uiLoop];
		if (!pOpponent || pOpponent == pSoldier)
			continue;

		INT8 bKnowledge = NOT_HEARD_OR_SEEN;
		INT32 sKnownSpot = NOWHERE;
		UINT8 ubContactConfidence = 0;

		if (!AIPlanningContactForOpponent(
			pSoldier, pOpponent->ubID, &sKnownSpot, NULL,
			&ubContactConfidence, &bKnowledge))
		{
			continue;
		}

		const BOOLEAN fDirectVisualContact =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;
		if (fDirectVisualContact &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW))
		{
			continue;
		}

		if (TileIsOutOfBounds(sKnownSpot) ||
			PythSpacesAway(pSoldier->sGridNo, sKnownSpot) > TACTICAL_RANGE)
		{
			continue;
		}

		UINT32 uiContactStrength = ubContactConfidence;

		// Only personal current sight can reveal that the contact is incapacitated.
		if (fDirectVisualContact &&
			IS_MERC_BODY_TYPE(pOpponent) &&
			!pOpponent->IsZombie() &&
			(pOpponent->stats.bLife < OKLIFE ||
			 (pOpponent->bCollapsed && pOpponent->bBreath < OKBREATH)))
		{
			uiContactStrength = __max((UINT32)10, uiContactStrength / 5);
		}

		uiStrength += uiContactStrength;
	}

	return (UINT16)__min((UINT32)65535, uiStrength);
}

static INT8 AIBattleSituationFromSnapshot(UINT32 uiFriends, UINT32 uiEnemies, UINT8 ubCasualties)
{
	// With no legitimate opponent knowledge there is no force-ratio assessment.
	if (uiEnemies == 0)
		return AI_BATTLE_UNKNOWN;

	// Historical losses matter, but they must not override the force that is still
	// standing in front of the player. A formation that retains superior current
	// combat power is not "losing" merely because half of its original roster died.
	if (uiFriends * 2 <= uiEnemies ||
		(ubCasualties >= 85 && uiFriends * 4 < uiEnemies * 5))
	{
		return AI_BATTLE_CATASTROPHIC;
	}

	if (uiFriends * 5 < uiEnemies * 4 ||
		(ubCasualties >= 65 && uiFriends * 10 < uiEnemies * 11))
	{
		return AI_BATTLE_LOSING;
	}

	if (uiFriends * 4 >= uiEnemies * 5 && ubCasualties < 75)
		return AI_BATTLE_WINNING;

	return AI_BATTLE_EVEN;
}

INT8 AIBattleSituation(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier))
		return AI_BATTLE_UNKNOWN;

	return AIBattleSituationFromSnapshot(
		AIPerceivedFriendlyStrength(pSoldier),
		AIPerceivedEnemyStrength(pSoldier),
		AIFriendlyCasualtyPercent(pSoldier));
}

BOOLEAN AIBuildTacticalDecisionContext(SOLDIERTYPE *pSoldier, AITACTICALDECISIONCONTEXT *pContext)
{
	if (!pContext)
		return FALSE;

	memset(pContext, 0, sizeof(AITACTICALDECISIONCONTEXT));
	pContext->sPrimaryThreat = NOWHERE;
	pContext->bBattleSituation = AI_BATTLE_UNKNOWN;
	pContext->ubPrimaryThreatAge = 255;

	if (!AICombatTeam(pSoldier))
		return FALSE;

	// Build one knowledge-safe snapshot so higher-level reasoners do not independently
	// rescan and reinterpret the same battlefield state during a single decision.
	UINT8 ubSharedPrimaryConfidence = 0;
	pContext->sPrimaryThreat =
		AIPrimaryPlanningThreatSpot(pSoldier, NULL, &ubSharedPrimaryConfidence);
	BOOLEAN fSharedPrimary =
		pSoldier->bTeam == ENEMY_TEAM &&
		!TileIsOutOfBounds(pContext->sPrimaryThreat) &&
		ubSharedPrimaryConfidence > 0;
	pContext->usPerceivedFriendlyStrength = AIPerceivedFriendlyStrength(pSoldier);
	pContext->usPerceivedEnemyStrength = AIPerceivedEnemyStrength(pSoldier);
	pContext->ubFriendlyCasualtyPercent = AIFriendlyCasualtyPercent(pSoldier);
	pContext->bBattleSituation = AIBattleSituationFromSnapshot(
		pContext->usPerceivedFriendlyStrength,
		pContext->usPerceivedEnemyStrength,
		pContext->ubFriendlyCasualtyPercent);
	pContext->iStress = AILocalStress(pSoldier);
	pContext->iPersonalRisk = AIPersonalRisk(pSoldier);
	pContext->iRiskTolerance = AIPersonalRiskTolerance(pSoldier);
	pContext->usKnownThreatExposure = AIKnownThreatExposure(
		pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
	pContext->ubNearbyOperationalFriends = AICountNearbyOperationalFriends(
		pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 4);

	AICONTACTBELIEF PrimaryBelief;
	if (AIBuildPrimaryContactBelief(pSoldier, pContext->sPrimaryThreat, &PrimaryBelief))
	{
		pContext->ubPrimaryThreatConfidence = PrimaryBelief.ubConfidence;
		pContext->ubPrimaryThreatAge = PrimaryBelief.ubAgeTurns;
		pContext->fPrimaryThreatPersonal =
			(PrimaryBelief.ubSource == AI_BELIEF_SOURCE_PERSONAL);
	}
	else if (fSharedPrimary && !TileIsOutOfBounds(pContext->sPrimaryThreat))
	{
		// Shared fireteam contact has no personal opponent identity here. Carry only
		// the communication-degraded confidence into the high-level plan.
		pContext->ubPrimaryThreatConfidence = ubSharedPrimaryConfidence;
		pContext->fPrimaryThreatPersonal = FALSE;
		if (ubSharedPrimaryConfidence >= 85)
			pContext->ubPrimaryThreatAge = 0;
		else if (ubSharedPrimaryConfidence >= 60)
			pContext->ubPrimaryThreatAge = 1;
		else if (ubSharedPrimaryConfidence >= 35)
			pContext->ubPrimaryThreatAge = 2;
		else
			pContext->ubPrimaryThreatAge = 3;
	}

	pContext->fHasCover = AnyCoverAtSpot(pSoldier, pSoldier->sGridNo);
	pContext->fUnderFire = pSoldier->aiData.bUnderFire;
	pContext->fIsolated = (pContext->ubNearbyOperationalFriends == 0);
	pContext->fHasLivePersonalContact = (pSoldier->aiData.bOppCnt > 0);
	pContext->fDisengaging = AIDisengagementActive(pSoldier);
	pContext->fEscaping = AIEscapeActive(pSoldier);

	if (!TileIsOutOfBounds(pContext->sPrimaryThreat))
	{
		pContext->fBadRange =
			(AIEngagementRangeModifier(pSoldier, pContext->sPrimaryThreat) < 0);
	}

	return TRUE;
}

BOOLEAN AISeverelyIsolated(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier))
		return FALSE;

	UINT16 usFriends = AIPerceivedFriendlyStrength(pSoldier);
	UINT16 usEnemies = AIPerceivedEnemyStrength(pSoldier);

	// One or two combat-capable soldiers facing at least as much known opposition
	// have effectively lost local mutual support.
	return (usEnemies > 0 && usFriends <= 200 && usEnemies >= usFriends);
}

BOOLEAN AILastSurvivorPressure(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier))
		return FALSE;

	UINT16 usLocalFriends = AIPerceivedFriendlyStrength(pSoldier);
	UINT16 usEnemies = AIPerceivedEnemyStrength(pSoldier);
	if (usEnemies == 0)
		return FALSE;

	UINT8 ubTeamReady = 0;
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (pFriend &&
			pFriend->bActive &&
			pFriend->bInSector &&
			pFriend->stats.bLife >= OKLIFE &&
			!pFriend->bCollapsed &&
			!pFriend->bBreathCollapsed &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW) &&
			!(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) &&
			!AIDisengagementActive(pFriend) &&
			!AIEscapeActive(pFriend))
		{
			++ubTeamReady;
		}
	}

	UINT8 ubLocalCasualties = AILocalCasualtyPercent(pSoldier);
	UINT8 ubKnownFriendlyLosses = ubLocalCasualties;
	if (pSoldier->bTeam == ENEMY_TEAM)
		ubKnownFriendlyLosses = __max(ubKnownFriendlyLosses, TeamPercentKilled(ENEMY_TEAM));

	// True last survivors: only one/two combat-capable soldiers remain on the team,
	// meaningful friendly losses have occurred, and there is no substantial local
	// allied support. This matters for militia fighting beside visible player mercs.
	UINT8 ubNearbySupport = AICountNearbyOperationalFriends(
		pSoldier, pSoldier->sGridNo, TACTICAL_RANGE / 2);
	if (ubTeamReady <= 2 && ubKnownFriendlyLosses >= 50 && ubNearbySupport <= 1)
		return TRUE;

	// Local remnant: one/two soldiers in this tactical element, with direct local
	// casualty evidence and at least equal known opposition. A separated two-man
	// patrol is therefore not mistaken for the last two men in the whole sector.
	if (usLocalFriends <= 200 &&
		usEnemies >= usLocalFriends &&
		ubLocalCasualties >= 50)
	{
		return TRUE;
	}

	return FALSE;
}

static BOOLEAN AIEscapeEstablishedForRout(SOLDIERTYPE *pSoldier);
static BOOLEAN AIDisengagementEstablishedForRout(SOLDIERTYPE *pSoldier);

UINT8 AILocalRoutPressure(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier))
		return 0;

	INT32 iPressure = 0;
	INT32 iRadius = __max(4, DAY_VISION_RANGE / 2);
	UINT8 ubEstablishedBreakers = 0;
	BOOLEAN fBreakingLeader = FALSE;
	BOOLEAN fStableLeader = FALSE;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend ||
			pFriend == pSoldier ||
			!pFriend->bActive ||
			!pFriend->bInSector ||
			!AISameFireteam(pSoldier, pFriend) ||
			pFriend->stats.bLife < OKLIFE ||
			pFriend->bCollapsed ||
			pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			pFriend->pathing.bLevel != pSoldier->pathing.bLevel ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > iRadius)
		{
			continue;
		}

		BOOLEAN fEscaping = AIEscapeEstablishedForRout(pFriend);
		BOOLEAN fDisengaging = AIDisengagementEstablishedForRout(pFriend);
		BOOLEAN fRunningAway = (pFriend->aiData.bAction == AI_ACTION_RUN_AWAY);
		BOOLEAN fCowering = (pFriend->flags.uiStatusFlags & SOLDIER_COWERING) != 0;
		UINT8 ubLeaderAuthority = AICommandAuthority(pFriend);
		BOOLEAN fLeader = ubLeaderAuthority >= 2;
		BOOLEAN fEstablishedBreak = fEscaping || fDisengaging;

		// Breaking friends exert social pressure only at local tactical scale.
		// Escape is the strongest signal; deliberate disengagement is weaker.
		if (fEscaping)
			iPressure += 30;
		else if (fDisengaging)
			iPressure += 12; // organized withdrawal is not panic
		else if (fRunningAway)
			iPressure += 10;
		else if (fCowering)
			iPressure += 8;

		if (fEstablishedBreak)
			++ubEstablishedBreakers;

		// A leader visibly abandoning the fight is especially destabilising.
		if (fLeader && (fEscaping || fDisengaging || fRunningAway))
		{
			// Watching a senior commander break is more destabilising than losing a
			// junior NCO, but rank never overrides the local/casualty gates above.
			iPressure += __min(18, 4 + (INT32)ubLeaderAuthority * 2);
			if (fEstablishedBreak)
				fBreakingLeader = TRUE;
		}
		// A nearby leader who is still holding together can slow a cascade. Higher
		// authority helps more, but cannot erase several established local breaks.
		else if (fLeader && !fCowering && !pFriend->aiData.bUnderFire)
		{
			iPressure -= __min(22, 6 + (INT32)ubLeaderAuthority * 2);
			fStableLeader = TRUE;
		}
	}

	// True morale collapse is deliberately nonlinear but rare. One frightened
	// soldier cannot trigger it. It needs multiple established local breaks,
	// meaningful losses and a fight that is already going badly. This lets a
	// platoon sometimes unravel quickly without turning every 20-30% casualty
	// battle into an easy automatic rout for the player.
	if (ubEstablishedBreakers >= 2)
	{
		UINT8 ubCasualties = AIFriendlyCasualtyPercent(pSoldier);
		INT8 bSituation = AIBattleSituation(pSoldier);
		BOOLEAN fCollapseConditions =
			(ubCasualties >= 25 &&
			 (bSituation == AI_BATTLE_LOSING || bSituation == AI_BATTLE_CATASTROPHIC)) ||
			(ubCasualties >= 45 && bSituation == AI_BATTLE_EVEN);

		if (fCollapseConditions && AILocalStress(pSoldier) >= 20)
		{
			INT32 iCascade = 10;
			iCascade += 5 * __min((INT32)2, (INT32)ubEstablishedBreakers - 1);
			if (fBreakingLeader)
				iCascade += 5;
			if (fStableLeader)
				iCascade -= 10;

			// Brave, confident and professional troops already have higher risk
			// tolerance. Reuse that resistance here instead of granting hidden
			// difficulty or accuracy bonuses.
			iCascade -= __max(0, (AIPersonalRiskTolerance(pSoldier) - 50) / 4);
			iPressure += __max(0, iCascade);
		}
	}

	return (UINT8)__max(0, __min(100, iPressure));
}

INT8 AIHopelessOddsModifier(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier))
		return 0;

	INT8 bModifier = 0;
	INT8 bSituation = AIBattleSituation(pSoldier);

	if (bSituation == AI_BATTLE_LOSING)
		bModifier -= 2;
	else if (bSituation == AI_BATTLE_CATASTROPHIC)
		bModifier -= 5;

	if (AISeverelyIsolated(pSoldier))
		bModifier -= 1;
	if (AILastSurvivorPressure(pSoldier))
		bModifier -= 2;

	return __max((INT8)-8, bModifier);
}

// Short-lived tactical disengagement/escape state. This is deliberately kept
// outside SOLDIERTYPE so the AI experiment does not alter savegame-compatible
// soldier data.
extern UINT32 guiTurnCnt;
static UINT8 gubAIEscapeIntent[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIEscapeIdentity[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIEscapeStartTurn[MAX_NUM_SOLDIERS] = { 0 };
// Full-sector rout requires sustained evidence of collapse. These transient arrays
// deliberately live outside SOLDIERTYPE so savegame layout remains untouched.
static UINT8 gubAIEscapeCollapseStreak[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIEscapeCollapseTurnStamp[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIEscapeCollapseIdentity[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIEscapeLastTurnStamp = 0;
static UINT8 gubAICompletedEnemyEscapes = 0;
static INT16 gsAIEscapeSectorX = -1;
static INT16 gsAIEscapeSectorY = -1;
static INT8 gbAIEscapeSectorZ = -1;

#define AI_ESCAPE_NORMAL_LIMIT 2
#define AI_ESCAPE_ABSOLUTE_LIMIT 3

static void AIMaintainEscapeTimeline(void);

static UINT8 AICountCommittedEnemyEscapes(SOLDIERTYPE *pExclude)
{
	AIMaintainEscapeTimeline();

	UINT8 ubCount = gubAICompletedEnemyEscapes;
	for (UINT16 ubID = 0; ubID < MAX_NUM_SOLDIERS; ++ubID)
	{
		if (pExclude && ubID == pExclude->ubID)
			continue;
		if (gubAIEscapeIntent[ubID] == 0 || guiAIEscapeIdentity[ubID] == 0)
			continue;

		// Only a live, matching in-sector soldier occupies an active escape ticket.
		// Completed traversals are counted separately, so dead runners and reused
		// tactical slots cannot permanently consume or accidentally erase the quota.
		SOLDIERTYPE *pRunner = MercPtrs[ubID];
		if (!pRunner || pRunner->bTeam != ENEMY_TEAM || !pRunner->bActive ||
			!pRunner->bInSector || pRunner->stats.bLife <= 0 ||
			pRunner->uiUniqueSoldierIdValue != guiAIEscapeIdentity[ubID])
		{
			continue;
		}

		if (ubCount < 255)
			++ubCount;
	}
	return ubCount;
}

static UINT8 AIEscapeIntentLimit(void)
{
	UINT8 ubLivingFighters = 0;
	for (UINT16 iCounter = gTacticalStatus.Team[ENEMY_TEAM].bFirstID;
		iCounter <= gTacticalStatus.Team[ENEMY_TEAM].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (pFriend && pFriend->bActive && pFriend->bInSector &&
			pFriend->stats.bLife >= OKLIFE &&
			!pFriend->bCollapsed && !pFriend->bBreathCollapsed &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW))
		{
			// Cowering, disengaging and already-escaping soldiers are still living
			// fighters for the purpose of opening the third runner slot. Temporary
			// local withdrawal must not make a sizeable force look like a three-man remnant.
			++ubLivingFighters;
		}
	}

	// Two runners is the normal ceiling. A third is reserved for true end-stage
	// collapse: either only three viable fighters remain or the enemy force has
	// already suffered overwhelming sector losses. Never permit more than three.
	if (ubLivingFighters <= 3 || TeamPercentKilled(ENEMY_TEAM) >= 75)
		return AI_ESCAPE_ABSOLUTE_LIMIT;

	return AI_ESCAPE_NORMAL_LIMIT;
}

static void AIMaintainEscapeTimeline(void)
{
	UINT32 uiTurnStamp = guiTurnCnt + 1;
	BOOLEAN fSectorChanged =
		gsAIEscapeSectorX != gWorldSectorX ||
		gsAIEscapeSectorY != gWorldSectorY ||
		gbAIEscapeSectorZ != gbWorldSectorZ;
	BOOLEAN fTimelineRollback =
		guiAIEscapeLastTurnStamp != 0 && uiTurnStamp < guiAIEscapeLastTurnStamp;

	if (fSectorChanged || fTimelineRollback)
	{
		for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
		{
			gubAIEscapeIntent[i] = 0;
			guiAIEscapeIdentity[i] = 0;
			guiAIEscapeStartTurn[i] = 0;
			gubAIEscapeCollapseStreak[i] = 0;
			guiAIEscapeCollapseTurnStamp[i] = 0;
			guiAIEscapeCollapseIdentity[i] = 0;
		}
		gubAICompletedEnemyEscapes = 0;
	}

	gsAIEscapeSectorX = gWorldSectorX;
	gsAIEscapeSectorY = gWorldSectorY;
	gbAIEscapeSectorZ = gbWorldSectorZ;
	guiAIEscapeLastTurnStamp = uiTurnStamp;
}

static void AIClearEscapeState(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	gubAIEscapeIntent[pSoldier->ubID] = 0;
	guiAIEscapeIdentity[pSoldier->ubID] = pSoldier->uiUniqueSoldierIdValue;
	guiAIEscapeStartTurn[pSoldier->ubID] = 0;
	gubAIEscapeCollapseStreak[pSoldier->ubID] = 0;
	guiAIEscapeCollapseTurnStamp[pSoldier->ubID] = 0;
	guiAIEscapeCollapseIdentity[pSoldier->ubID] = pSoldier->uiUniqueSoldierIdValue;

	// Regrouping or recovery can cancel escape after a soldier has already reached
	// a strategic map edge. Disarm a stale traversal quote as part of clearing the
	// enemy escape state so a successfully reattached soldier cannot still leave.
	if (pSoldier->bTeam == ENEMY_TEAM && pSoldier->ubProfile == NO_PROFILE &&
		pSoldier->ubQuoteActionID >= QUOTE_ACTION_ID_TRAVERSE_EAST &&
		pSoldier->ubQuoteActionID <= QUOTE_ACTION_ID_TRAVERSE_NORTH)
	{
		pSoldier->ubQuoteActionID = 0;
	}
}

BOOLEAN AIEscapeActive(SOLDIERTYPE *pSoldier)
{
	AIMaintainEscapeTimeline();

	if (!pSoldier || pSoldier->bTeam != ENEMY_TEAM || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return FALSE;

	if (guiAIEscapeIdentity[pSoldier->ubID] != pSoldier->uiUniqueSoldierIdValue)
		return FALSE;

	return (pSoldier->aiData.bAlertStatus >= STATUS_RED &&
		gubAIEscapeIntent[pSoldier->ubID] != 0);
}

void AIRegisterEnemyEscapeTraversal(SOLDIERTYPE *pSoldier)
{
	AIMaintainEscapeTimeline();

	if (!pSoldier || pSoldier->bTeam != ENEMY_TEAM || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	UINT8 ubID = pSoldier->ubID;
	if (gubAIEscapeIntent[ubID] == 0 ||
		guiAIEscapeIdentity[ubID] != pSoldier->uiUniqueSoldierIdValue)
	{
		return;
	}

	if (gubAICompletedEnemyEscapes < AI_ESCAPE_ABSOLUTE_LIMIT)
		++gubAICompletedEnemyEscapes;

	// The completed counter now owns this quota slot. Clear the per-soldier ticket
	// before TacticalRemoveSoldier can free/reuse the tactical ID.
	gubAIEscapeIntent[ubID] = 0;
	guiAIEscapeStartTurn[ubID] = 0;
}

static INT8 AIProfessionalismModifier(SOLDIERTYPE *pSoldier);
static BOOLEAN AIHasNearbyStableLeader(SOLDIERTYPE *pSoldier);
static UINT8 AIUpdateRecoveryStreak(SOLDIERTYPE *pSoldier, INT8 bSituation,
	UINT8 ubRoutPressure, BOOLEAN fLastSurvivor);
static void AIResetRecoveryStreak(SOLDIERTYPE *pSoldier);

static INT32 AIBoundedDecisionJitter(SOLDIERTYPE *pSoldier, UINT32 uiSalt, INT32 iAmplitude);
static INT32 AIBoundedElementJitter(SOLDIERTYPE *pSoldier, UINT32 uiSalt, INT32 iAmplitude);

// "Hold confidence" is the bridge between raw force ratio and human-like courage.
// It deliberately rewards a viable fighting position: nearby allies, leadership,
// cover, good troops, useful weapons and recent success. Stress, personal danger
// and an established local rout pull the other way. Historical casualties matter,
// but they are only one input; they never override current combat power by themselves.
static INT32 AIHoldGroundConfidence(SOLDIERTYPE *pSoldier, INT8 bSituation,
	UINT8 ubCasualties, BOOLEAN fLastSurvivor, UINT8 ubRoutPressure)
{
	if (!pSoldier)
		return 0;

	INT32 iConfidence = 50;

	switch (bSituation)
	{
	case AI_BATTLE_WINNING:      iConfidence += 28; break;
	case AI_BATTLE_EVEN:         iConfidence += 12; break;
	case AI_BATTLE_LOSING:       iConfidence -= 10; break;
	case AI_BATTLE_CATASTROPHIC: iConfidence -= 28; break;
	default:                      iConfidence -= 5; break;
	}

	// Training/experience and current weapon quality make troops more willing to
	// exploit an advantage without granting any hidden CTH/AP bonus.
	iConfidence += AIProfessionalismModifier(pSoldier);
	iConfidence += __max(-4, __min(10, ((INT32)pSoldier->stats.bMarksmanship - 60) / 4));
	iConfidence += __max(-3, __min(8, ((INT32)pSoldier->stats.bExpLevel - 4) * 2));
	if (AICheckHasGun(pSoldier))
	{
		iConfidence += __min(8, (INT32)AIGunDeadliness(pSoldier) / 7);
		if (AIGunAmmo(pSoldier) == 0)
			iConfidence -= 12;
	}

	if (AnyCoverAtSpot(pSoldier, pSoldier->sGridNo))
		iConfidence += 10;
	if (SightCoverAtSpot(pSoldier, pSoldier->sGridNo, FALSE))
		iConfidence += 5;

	UINT8 ubNearbyFriends = AICountNearbyOperationalFriends(
		pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 4);
	iConfidence += __min(15, (INT32)ubNearbyFriends * 4);
	if (AIHasNearbyStableLeader(pSoldier))
		iConfidence += 10;

	switch (pSoldier->aiData.bAIMorale)
	{
	case MORALE_HOPELESS:  iConfidence -= 18; break;
	case MORALE_WORRIED:   iConfidence -= 8; break;
	case MORALE_CONFIDENT: iConfidence += 8; break;
	case MORALE_FEARLESS:  iConfidence += 14; break;
	}

	if (pSoldier->aiData.bOrders == STATIONARY || pSoldier->aiData.bOrders == ONGUARD)
		iConfidence += 6;
	else if (pSoldier->aiData.bOrders == SEEKENEMY)
		iConfidence += 4;

	if (pSoldier->LastAttackHit() ||
		(pSoldier->usSoldierFlagMask2 & SOLDIER_SUCCESSFUL_ATTACK) ||
		pSoldier->LastTargetSuppressed())
	{
		iConfidence += 8;
	}

	iConfidence -= AILocalStress(pSoldier) / 4;
	INT32 iRiskExcess = AIPersonalRisk(pSoldier) - AIPersonalRiskTolerance(pSoldier);
	if (iRiskExcess > 0)
		iConfidence -= iRiskExcess / 2;

	iConfidence -= ubRoutPressure / 4;

	// Casualties erode confidence progressively, not as an on/off switch.
	if (ubCasualties > 40)
		iConfidence -= (ubCasualties - 40) / 4;

	if (fLastSurvivor)
		iConfidence -= 18;

	return __max(0, __min(100, iConfidence));
}

// Black Box v2: one omniscient team snapshot per tactical turn. This record is
// explicitly separate from the actor's perceived state below; it exists so the
// Companion can diagnose perception errors without leaking hidden information
// back into AI decisions.
static UINT32 guiVRFormationSnapshotTurn[256] = { 0 };
static INT16 gsVRFormationSnapshotSectorX = -1;
static INT16 gsVRFormationSnapshotSectorY = -1;
static INT8 gbVRFormationSnapshotSectorZ = -1;

static void VRTraceFormationSnapshot(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || !AICombatTeam(pSoldier) || !VRAnalyticsIsEnabled())
		return;

	INT32 iTeam = (INT32)pSoldier->bTeam;
	if (iTeam < 0 || iTeam >= 256)
		return;

	UINT32 uiTurnStamp = guiTurnCnt + 1;
	BOOLEAN fSectorChanged =
		gsVRFormationSnapshotSectorX != gWorldSectorX ||
		gsVRFormationSnapshotSectorY != gWorldSectorY ||
		gbVRFormationSnapshotSectorZ != gbWorldSectorZ;

	if (fSectorChanged)
	{
		for (UINT16 i = 0; i < 256; ++i)
			guiVRFormationSnapshotTurn[i] = 0;

		gsVRFormationSnapshotSectorX = gWorldSectorX;
		gsVRFormationSnapshotSectorY = gWorldSectorY;
		gbVRFormationSnapshotSectorZ = gbWorldSectorZ;
	}

	if (guiVRFormationSnapshotTurn[(UINT8)iTeam] == uiTurnStamp)
		return;
	guiVRFormationSnapshotTurn[(UINT8)iTeam] = uiTurnStamp;

	INT32 iLiving = 0;
	INT32 iReady = 0;
	INT32 iCowering = 0;
	INT32 iDisengaging = 0;
	INT32 iEscaping = 0;
	INT32 iLeaders = 0;
	INT32 iMoraleTotal = 0;
	INT32 iStressTotal = 0;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || !pFriend->bActive || !pFriend->bInSector ||
			pFriend->stats.bLife <= 0)
		{
			continue;
		}

		++iLiving;
		if (pFriend->flags.uiStatusFlags & SOLDIER_COWERING)
			++iCowering;
		if (AIDisengagementActive(pFriend))
			++iDisengaging;
		if (AIEscapeActive(pFriend))
			++iEscaping;
		if (AICheckIsLeader(pFriend))
			++iLeaders;

		if (pFriend->stats.bLife >= OKLIFE &&
			!pFriend->bCollapsed &&
			!pFriend->bBreathCollapsed &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW))
		{
			++iReady;
			iMoraleTotal += pFriend->aiData.bAIMorale;
			iStressTotal += AILocalStress(pFriend);
		}
	}

	VRAnalyticsTacticalFormationSnapshot(
		uiTurnStamp,
		pSoldier->bTeam,
		gWorldSectorX,
		gWorldSectorY,
		gbWorldSectorZ,
		iLiving,
		iReady,
		iCowering,
		iDisengaging,
		iEscaping,
		iLeaders,
		TeamPercentKilled(pSoldier->bTeam),
		iReady > 0 ? iMoraleTotal / iReady : 0,
		iReady > 0 ? iStressTotal / iReady : 0);
}

static const char* VREscapeReason(INT8 bSituation, UINT8 ubCasualties,
	BOOLEAN fLastSurvivor, INT32 iHoldConfidence, UINT8 ubCollapseStreak,
	BOOLEAN fShouldEscape)
{
	if (fShouldEscape)
	{
		if (fLastSurvivor)
			return "last_survivor_low_confidence";
		if (ubCasualties >= 90)
			return "near_annihilation_low_confidence";
		if (bSituation == AI_BATTLE_CATASTROPHIC)
			return "sustained_catastrophic_collapse";
		return "sustained_losing_collapse";
	}

	if (bSituation == AI_BATTLE_WINNING)
		return "battle_winning_hold";
	if (bSituation == AI_BATTLE_EVEN && ubCasualties < 90)
		return "even_battle_hold";
	if (iHoldConfidence >= 55)
		return "hold_confidence_high";
	if (ubCollapseStreak < 2)
		return "collapse_not_sustained";
	return "escape_gates_not_met";
}

static void VRTraceRetreatAssessment(
	SOLDIERTYPE *pSoldier,
	INT8 bSituation,
	UINT8 ubCasualties,
	BOOLEAN fLastSurvivor,
	UINT8 ubRoutPressure,
	INT32 iHoldConfidence,
	UINT8 ubCollapseStreak,
	BOOLEAN fShouldEscape)
{
	if (!pSoldier || !VRAnalyticsIsEnabled())
		return;

	VRTraceFormationSnapshot(pSoldier);

	UINT16 usFriends = AIPerceivedFriendlyStrength(pSoldier);
	UINT16 usEnemies = AIPerceivedEnemyStrength(pSoldier);
	INT32 iStress = AILocalStress(pSoldier);
	INT32 iRisk = AIPersonalRisk(pSoldier);
	INT32 iTolerance = AIPersonalRiskTolerance(pSoldier);
	INT32 iLifePercent = pSoldier->stats.bLifeMax > 0 ?
		(100 * pSoldier->stats.bLife) / pSoldier->stats.bLifeMax : 0;
	INT32 iEscapePressure = __min(150,
		(100 - iHoldConfidence) + (INT32)ubCollapseStreak * 8 +
		(INT32)ubRoutPressure / 5);

	VRAnalyticsTacticalRetreatAssessment(
		pSoldier->ubID,
		(unsigned long)(guiTurnCnt + 1),
		bSituation,
		usFriends,
		usEnemies,
		pSoldier->aiData.bOppCnt,
		ubCasualties,
		AILocalCasualtyPercent(pSoldier),
		iHoldConfidence,
		iStress,
		iRisk,
		iTolerance,
		ubRoutPressure,
		ubCollapseStreak,
		fLastSurvivor ? true : false,
		AICountNearbyOperationalFriends(
			pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 4),
		AIHasNearbyStableLeader(pSoldier) ? true : false,
		AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) ? true : false,
		SightCoverAtSpot(pSoldier, pSoldier->sGridNo, FALSE) ? true : false,
		pSoldier->aiData.bUnderFire ? true : false,
		iLifePercent,
		pSoldier->stats.bMarksmanship,
		pSoldier->stats.bExpLevel,
		AICheckHasGun(pSoldier) ? AIGunDeadliness(pSoldier) : 0,
		AICheckHasGun(pSoldier) ? AIGunAmmo(pSoldier) : 0,
		pSoldier->LastAttackHit() ? true : false,
		pSoldier->LastTargetSuppressed() ? true : false,
		AIEscapeActive(pSoldier) ? true : false);

	VRAnalyticsTacticalCandidate(
		pSoldier->ubID,
		"hold_ground",
		pSoldier->sGridNo,
		iHoldConfidence,
		iHoldConfidence,
		(iHoldConfidence >= 25 || bSituation == AI_BATTLE_WINNING ||
		 bSituation == AI_BATTLE_EVEN),
		"current_combat_power_position_and_cohesion");

	VRAnalyticsTacticalCandidate(
		pSoldier->ubID,
		"sector_escape",
		pSoldier->sGridNo,
		100 - iHoldConfidence,
		iEscapePressure,
		fShouldEscape,
		VREscapeReason(bSituation, ubCasualties, fLastSurvivor,
			iHoldConfidence, ubCollapseStreak, fShouldEscape));
}

static UINT8 AIUpdateEscapeCollapseStreak(SOLDIERTYPE *pSoldier, INT8 bSituation,
	UINT8 ubCasualties, BOOLEAN fLastSurvivor, UINT8 ubRoutPressure,
	INT32 iHoldConfidence)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return 0;

	UINT8 ubID = pSoldier->ubID;
	if (guiAIEscapeCollapseIdentity[ubID] != pSoldier->uiUniqueSoldierIdValue)
	{
		gubAIEscapeCollapseStreak[ubID] = 0;
		guiAIEscapeCollapseTurnStamp[ubID] = 0;
		guiAIEscapeCollapseIdentity[ubID] = pSoldier->uiUniqueSoldierIdValue;
	}

	UINT32 uiTurnStamp = guiTurnCnt + 1;
	if (guiAIEscapeCollapseTurnStamp[ubID] == uiTurnStamp)
		return gubAIEscapeCollapseStreak[ubID];

	guiAIEscapeCollapseTurnStamp[ubID] = uiTurnStamp;

	INT32 iStress = AILocalStress(pSoldier);
	BOOLEAN fCollapseSnapshot = FALSE;

	if (fLastSurvivor && iHoldConfidence < 30)
		fCollapseSnapshot = TRUE;
	else if (ubCasualties >= 90 && bSituation != AI_BATTLE_WINNING && iHoldConfidence < 35)
		fCollapseSnapshot = TRUE;
	else if (bSituation == AI_BATTLE_CATASTROPHIC &&
		iHoldConfidence < 35 &&
		(ubCasualties >= 40 || ubRoutPressure >= 55 || iStress >= 60))
	{
		fCollapseSnapshot = TRUE;
	}
	else if (bSituation == AI_BATTLE_LOSING &&
		iHoldConfidence < 25 &&
		ubCasualties >= 55 &&
		(ubRoutPressure >= 60 || iStress >= 55))
	{
		fCollapseSnapshot = TRUE;
	}

	if (fCollapseSnapshot)
		gubAIEscapeCollapseStreak[ubID] = __min((UINT8)4,
			(UINT8)(gubAIEscapeCollapseStreak[ubID] + 1));
	else if (bSituation == AI_BATTLE_WINNING || bSituation == AI_BATTLE_EVEN ||
		iHoldConfidence >= 45)
		gubAIEscapeCollapseStreak[ubID] = 0;
	else if (gubAIEscapeCollapseStreak[ubID] > 0)
		--gubAIEscapeCollapseStreak[ubID];

	return gubAIEscapeCollapseStreak[ubID];
}

static BOOLEAN AIShouldStartEscapeFromState(SOLDIERTYPE *pSoldier,
	INT8 bSituation, UINT8 ubCasualties, BOOLEAN fLastSurvivor,
	UINT8 ubRoutPressure, INT32 iHoldConfidence, UINT8 ubCollapseStreak)
{
	// A formation that still believes it can hold does not abandon the sector.
	// It may still take cover, fall back locally or enter organized disengagement.
	if (bSituation == AI_BATTLE_WINNING || iHoldConfidence >= 55)
		return FALSE;

	// Even fights should be fought out. Only near-annihilation can override this.
	if (bSituation == AI_BATTLE_EVEN && ubCasualties < 90)
		return FALSE;

	if (fLastSurvivor)
	{
		// A lone remnant does not instantly abandon the sector on the first bad
		// snapshot. Fireteam reattachment is checked before this function, so by
		// the time we get here no viable reachable element is available. Require
		// sustained collapse as well as genuinely hopeless local danger before the
		// last fighter becomes a sector runner.
		if (ubCasualties < 60 || iHoldConfidence >= 20)
			return FALSE;

		INT32 iStress = AILocalStress(pSoldier);
		INT32 iRisk = AIPersonalRisk(pSoldier);
		INT32 iTolerance = AIPersonalRiskTolerance(pSoldier);

		if (bSituation == AI_BATTLE_CATASTROPHIC)
		{
			return (ubCollapseStreak >= 2 &&
				(iStress >= 55 || iRisk >= iTolerance + 15));
		}

		if (bSituation == AI_BATTLE_LOSING)
		{
			return (ubCollapseStreak >= 3 &&
				iStress >= 60 &&
				iRisk >= iTolerance + 10);
		}

		return FALSE;
	}

	if (ubCasualties >= 90 && bSituation != AI_BATTLE_WINNING)
		return (iHoldConfidence < 30 && ubCollapseStreak >= 1);

	INT32 iRoutThreshold = 75 +
		(AIPersonalRiskTolerance(pSoldier) - 50) / 2 +
		AIBoundedDecisionJitter(pSoldier, 211u, 4);
	iRoutThreshold = __max(65, __min(90, iRoutThreshold));

	if (bSituation == AI_BATTLE_CATASTROPHIC)
	{
		INT32 iCasualtyThreshold = 55 + AIProfessionalismModifier(pSoldier) / 2 +
			AIBoundedDecisionJitter(pSoldier, 223u, 4);
		iCasualtyThreshold = __max(48, __min(68, iCasualtyThreshold));

		// Truly awful local danger can force a faster break, but otherwise a
		// catastrophic snapshot must persist into a second tactical turn.
		if (iHoldConfidence < 15 &&
			ubCasualties >= iCasualtyThreshold &&
			AIPersonalRisk(pSoldier) >= AIPersonalRiskTolerance(pSoldier) + 20)
		{
			return TRUE;
		}

		if (ubCollapseStreak >= 2 &&
			iHoldConfidence < 30 &&
			(ubCasualties >= iCasualtyThreshold ||
			 (ubRoutPressure >= iRoutThreshold && AILocalStress(pSoldier) >= 55)))
		{
			return TRUE;
		}
	}

	// Merely losing is not enough. Full escape requires a sustained multi-turn
	// collapse with heavy losses and strong social/stress evidence.
	INT32 iLosingEscapeThreshold = 68 + AIProfessionalismModifier(pSoldier) / 2 +
		AIBoundedDecisionJitter(pSoldier, 227u, 4);
	iLosingEscapeThreshold = __max(62, __min(80, iLosingEscapeThreshold));

	if (bSituation == AI_BATTLE_LOSING &&
		ubCollapseStreak >= 3 &&
		iHoldConfidence < 22 &&
		ubCasualties >= iLosingEscapeThreshold &&
		ubRoutPressure >= iRoutThreshold &&
		AILocalStress(pSoldier) >= 50)
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN AIShouldStartEscape(SOLDIERTYPE *pSoldier)
{
	// A force that already escaped from the previous sector has spent its
	// strategic retreat. Local fallback remains legal, but another map-edge
	// escape during this pursuit battle is not.
	if (EnemyRetreatLockedInSector((UINT8)gWorldSectorX, (UINT8)gWorldSectorY))
		return FALSE;
	if (!pSoldier || pSoldier->bTeam != ENEMY_TEAM || pSoldier->IsZombie() ||
		pSoldier->ubProfile != NO_PROFILE ||
		pSoldier->aiData.bAlertStatus < STATUS_RED ||
		pSoldier->aiData.bAttitude == ATTACKSLAYONLY ||
		TANK(pSoldier) ||
		(pSoldier->flags.uiStatusFlags & SOLDIER_VEHICLE) ||
		AM_A_ROBOT(pSoldier))
	{
		return FALSE;
	}

	// A remnant should try to become part of another viable element before it is
	// even considered a sector runner.
	if (AIRecentlyReattachedFireteamRemnant(pSoldier))
		return FALSE;
	if (AIFireteamRegroupingStrength(pSoldier) <= 2 && AICanAbsorbFireteamRemnant(pSoldier))
		return FALSE;

	if (AICountCommittedEnemyEscapes(pSoldier) >= AIEscapeIntentLimit())
		return FALSE;

	INT8 bSituation = AIBattleSituation(pSoldier);
	if (bSituation == AI_BATTLE_UNKNOWN)
		return FALSE;

	UINT8 ubCasualties = AIFriendlyCasualtyPercent(pSoldier);
	BOOLEAN fLastSurvivor = AILastSurvivorPressure(pSoldier);
	UINT8 ubRoutPressure = AILocalRoutPressure(pSoldier);
	INT32 iHoldConfidence = AIHoldGroundConfidence(pSoldier, bSituation,
		ubCasualties, fLastSurvivor, ubRoutPressure);
	UINT8 ubCollapseStreak = AIUpdateEscapeCollapseStreak(pSoldier, bSituation,
		ubCasualties, fLastSurvivor, ubRoutPressure, iHoldConfidence);

	return AIShouldStartEscapeFromState(pSoldier, bSituation, ubCasualties,
		fLastSurvivor, ubRoutPressure, iHoldConfidence, ubCollapseStreak);
}

static void AIUpdateEscapeStateFromSnapshot(SOLDIERTYPE *pSoldier, INT8 bSituation, UINT8 ubCasualties, BOOLEAN fLastSurvivor, UINT8 ubRoutPressure)
{
	AIMaintainEscapeTimeline();

	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	UINT8 ubID = pSoldier->ubID;
	if (guiAIEscapeIdentity[ubID] != pSoldier->uiUniqueSoldierIdValue)
	{
		gubAIEscapeIntent[ubID] = 0;
		guiAIEscapeIdentity[ubID] = pSoldier->uiUniqueSoldierIdValue;
		guiAIEscapeStartTurn[ubID] = 0;
	}

	if (EnemyRetreatLockedInSector((UINT8)gWorldSectorX, (UINT8)gWorldSectorY) ||
		pSoldier->bTeam != ENEMY_TEAM || pSoldier->IsZombie() ||
		pSoldier->ubProfile != NO_PROFILE ||
		pSoldier->aiData.bAlertStatus < STATUS_RED ||
		pSoldier->aiData.bAttitude == ATTACKSLAYONLY ||
		TANK(pSoldier) ||
		(pSoldier->flags.uiStatusFlags & SOLDIER_VEHICLE) ||
		AM_A_ROBOT(pSoldier))
	{
		gubAIEscapeIntent[ubID] = 0;
		guiAIEscapeStartTurn[ubID] = 0;
		return;
	}

	// Full escape takes longer to reverse than a local disengagement. Require
	// sustained stabilization; a nearby stable leader shortens, but does not
	// eliminate, that recovery period.
	if (gubAIEscapeIntent[ubID] != 0)
	{
		UINT8 ubRecoveryStreak = AIUpdateRecoveryStreak(pSoldier, bSituation,
			ubRoutPressure, fLastSurvivor);
		UINT8 ubRequiredRecovery = AIHasNearbyStableLeader(pSoldier) ? 2 : 3;

		if (ubRecoveryStreak >= ubRequiredRecovery)
		{
			gubAIEscapeIntent[ubID] = 0;
			guiAIEscapeStartTurn[ubID] = 0;
			AIResetRecoveryStreak(pSoldier);
			return;
		}
	}

	INT32 iHoldConfidence = AIHoldGroundConfidence(pSoldier, bSituation,
		ubCasualties, fLastSurvivor, ubRoutPressure);
	UINT8 ubCollapseStreak = AIUpdateEscapeCollapseStreak(pSoldier, bSituation,
		ubCasualties, fLastSurvivor, ubRoutPressure, iHoldConfidence);
	BOOLEAN fShouldEscape = AIShouldStartEscapeFromState(
		pSoldier, bSituation, ubCasualties, fLastSurvivor,
		ubRoutPressure, iHoldConfidence, ubCollapseStreak);

	VRTraceRetreatAssessment(
		pSoldier, bSituation, ubCasualties, fLastSurvivor,
		ubRoutPressure, iHoldConfidence, ubCollapseStreak, fShouldEscape);

	if (gubAIEscapeIntent[ubID] == 0 &&
		bSituation != AI_BATTLE_UNKNOWN &&
		fShouldEscape)
	{
		// Fireteam survival beats individual flight. Cohesion owns the actual
		// reassignment because it can validate a real movement route first; escape
		// logic only defers while such a local destination exists.
		if (AIRecentlyReattachedFireteamRemnant(pSoldier))
			return;
		if (AIFireteamRegroupingStrength(pSoldier) <= 2 && AICanAbsorbFireteamRemnant(pSoldier))
			return;

		// Escape is deliberately scarce. Once the local force has its two runners
		// (three only in true end-stage collapse), additional shaken soldiers must
		// withdraw tactically, regroup, or keep fighting instead of streaming off-map.
		if (AICountCommittedEnemyEscapes(pSoldier) >= AIEscapeIntentLimit())
			return;

		gubAIEscapeIntent[ubID] = 1;
		guiAIEscapeStartTurn[ubID] = guiTurnCnt + 1;
		AIResetRecoveryStreak(pSoldier);
	}
}

static UINT8 gubAIDisengageTurns[MAX_NUM_SOLDIERS] = { 0 };
static UINT8 gubAIForcedDisengageTurns[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIDisengageTurnStamp[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIDisengageIdentity[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIDisengageStartTurn[MAX_NUM_SOLDIERS] = { 0 };


// Ordinary tactical fallback is a one-time positional concession per soldier per
// sector fight. True disengagement/escape remains separate and may still continue
// when morale has genuinely collapsed.
static UINT8 gubAITacticalFallbackUsed[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAITacticalFallbackIdentity[MAX_NUM_SOLDIERS] = { 0 };

// Short sector-local memory for tactical movement. It prevents low-value
// A->B->A and A->B->C->A shuffling across cover, cohesion, flank and withdrawal
// actions while still allowing a genuinely safer emergency reversal.
static INT32 gsAICoverMoveFrom[MAX_NUM_SOLDIERS] = { 0 };
static INT32 gsAICoverMoveTo[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAICoverMoveTurn[MAX_NUM_SOLDIERS] = { 0 };
static INT32 gsAICoverMovePreviousFrom[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAICoverMovePreviousTurn[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAICoverMoveIdentity[MAX_NUM_SOLDIERS] = { 0 };
static INT16 gsAICoverMemorySectorX = -1;
static INT16 gsAICoverMemorySectorY = -1;
static INT8 gbAICoverMemorySectorZ = -1;
static UINT32 guiAICoverMemoryLastTurn = 0;

static UINT8 gubAIRecoveryStreak[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIRecoveryTurnStamp[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIRecoveryIdentity[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIDisengageLastTurnStamp = 0;
static INT16 gsAIDisengageSectorX = -1;
static INT16 gsAIDisengageSectorY = -1;
static INT8 gbAIDisengageSectorZ = -1;

void AIResetRetreatCoordinationStateForLoad(void)
{
	// These systems deliberately live outside SOLDIERTYPE for save compatibility.
	// A successful load must therefore invalidate them explicitly, including the
	// same-sector/same-turn quickload case that timestamp rollback cannot detect.
	gubAINextFireteam = 1;
	gfAIFireteamsSeeded = FALSE;
	gsAIFireteamSectorX = -1;
	gsAIFireteamSectorY = -1;
	gbAIFireteamSectorZ = -1;
	guiAIFireteamLastTurnStamp = 0;
	gsAIFireteamKnownMenInSector = -1;

	gsAIEscapeSectorX = -1;
	gsAIEscapeSectorY = -1;
	gbAIEscapeSectorZ = -1;
	guiAIEscapeLastTurnStamp = 0;
	gubAICompletedEnemyEscapes = 0;

	gsAIDisengageSectorX = -1;
	gsAIDisengageSectorY = -1;
	gbAIDisengageSectorZ = -1;
	guiAIDisengageLastTurnStamp = 0;

	gsAICoverMemorySectorX = -1;
	gsAICoverMemorySectorY = -1;
	gbAICoverMemorySectorZ = -1;
	guiAICoverMemoryLastTurn = 0;

	for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
	{
		gubAIFireteam[i] = AI_FIRETEAM_NONE;
		guiAIFireteamIdentity[i] = 0;
		guiAIFireteamRejoinUntilTurn[i] = 0;

		gubAIEscapeIntent[i] = 0;
		guiAIEscapeIdentity[i] = 0;
		guiAIEscapeStartTurn[i] = 0;
		gubAIEscapeCollapseStreak[i] = 0;
		guiAIEscapeCollapseTurnStamp[i] = 0;
		guiAIEscapeCollapseIdentity[i] = 0;

		gubAIDisengageTurns[i] = 0;
		gubAIForcedDisengageTurns[i] = 0;
		guiAIDisengageTurnStamp[i] = 0;
		guiAIDisengageIdentity[i] = 0;
		guiAIDisengageStartTurn[i] = 0;

		gubAITacticalFallbackUsed[i] = 0;
		guiAITacticalFallbackIdentity[i] = 0;
		gsAICoverMoveFrom[i] = NOWHERE;
		gsAICoverMoveTo[i] = NOWHERE;
		guiAICoverMoveTurn[i] = 0;
		guiAICoverMoveIdentity[i] = 0;

		gubAIRecoveryStreak[i] = 0;
		guiAIRecoveryTurnStamp[i] = 0;
		guiAIRecoveryIdentity[i] = 0;
	}

	for (UINT16 i = 0; i < 256; ++i)
		gbAIFireteamTeam[i] = -1;
}
static void AIMaintainDisengagementTimeline(void)
{
	UINT32 uiTurnStamp = guiTurnCnt + 1;
	BOOLEAN fSectorChanged =
		gsAIDisengageSectorX != gWorldSectorX ||
		gsAIDisengageSectorY != gWorldSectorY ||
		gbAIDisengageSectorZ != gbWorldSectorZ;
	BOOLEAN fTimelineRollback =
		guiAIDisengageLastTurnStamp != 0 && uiTurnStamp < guiAIDisengageLastTurnStamp;

	if (fSectorChanged || fTimelineRollback)
	{
		for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
		{
			gubAIDisengageTurns[i] = 0;
			gubAIForcedDisengageTurns[i] = 0;
			guiAIDisengageTurnStamp[i] = 0;
			guiAIDisengageIdentity[i] = 0;
			guiAIDisengageStartTurn[i] = 0;
			gubAITacticalFallbackUsed[i] = 0;
			guiAITacticalFallbackIdentity[i] = 0;
			gubAIRecoveryStreak[i] = 0;
			guiAIRecoveryTurnStamp[i] = 0;
			guiAIRecoveryIdentity[i] = 0;
		}
	}

	gsAIDisengageSectorX = gWorldSectorX;
	gsAIDisengageSectorY = gWorldSectorY;
	gbAIDisengageSectorZ = gbWorldSectorZ;
	guiAIDisengageLastTurnStamp = uiTurnStamp;
}

BOOLEAN AIHasUsedTacticalFallback(SOLDIERTYPE *pSoldier)
{
	AIMaintainDisengagementTimeline();

	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return FALSE;

	return guiAITacticalFallbackIdentity[pSoldier->ubID] == pSoldier->uiUniqueSoldierIdValue &&
		gubAITacticalFallbackUsed[pSoldier->ubID] != 0;
}

void AIRegisterTacticalFallback(SOLDIERTYPE *pSoldier)
{
	AIMaintainDisengagementTimeline();

	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	guiAITacticalFallbackIdentity[pSoldier->ubID] = pSoldier->uiUniqueSoldierIdValue;
	gubAITacticalFallbackUsed[pSoldier->ubID] = 1;
}

static void AIMaintainCoverMoveMemory(void)
{
	UINT32 uiTurnStamp = guiTurnCnt + 1;
	BOOLEAN fSectorChanged =
		gsAICoverMemorySectorX != gWorldSectorX ||
		gsAICoverMemorySectorY != gWorldSectorY ||
		gbAICoverMemorySectorZ != gbWorldSectorZ;
	BOOLEAN fRollback = guiAICoverMemoryLastTurn != 0 &&
		uiTurnStamp < guiAICoverMemoryLastTurn;

	if (fSectorChanged || fRollback)
	{
		for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
		{
			gsAICoverMoveFrom[i] = NOWHERE;
			gsAICoverMoveTo[i] = NOWHERE;
			guiAICoverMoveTurn[i] = 0;
			gsAICoverMovePreviousFrom[i] = NOWHERE;
			guiAICoverMovePreviousTurn[i] = 0;
			guiAICoverMoveIdentity[i] = 0;
		}
	}

	gsAICoverMemorySectorX = gWorldSectorX;
	gsAICoverMemorySectorY = gWorldSectorY;
	gbAICoverMemorySectorZ = gbWorldSectorZ;
	guiAICoverMemoryLastTurn = uiTurnStamp;
}

void AIRegisterCoverMoveIntent(SOLDIERTYPE *pSoldier, INT32 sFromGrid, INT32 sToGrid)
{
	AIMaintainCoverMoveMemory();
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS ||
		TileIsOutOfBounds(sFromGrid) || TileIsOutOfBounds(sToGrid) ||
		sFromGrid == sToGrid)
		return;

	UINT8 ubID = pSoldier->ubID;
	if (guiAICoverMoveIdentity[ubID] == pSoldier->uiUniqueSoldierIdValue &&
		guiAICoverMoveTurn[ubID] != 0)
	{
		gsAICoverMovePreviousFrom[ubID] = gsAICoverMoveFrom[ubID];
		guiAICoverMovePreviousTurn[ubID] = guiAICoverMoveTurn[ubID];
	}
	else
	{
		gsAICoverMovePreviousFrom[ubID] = NOWHERE;
		guiAICoverMovePreviousTurn[ubID] = 0;
	}

	gsAICoverMoveFrom[ubID] = sFromGrid;
	gsAICoverMoveTo[ubID] = sToGrid;
	guiAICoverMoveTurn[ubID] = guiTurnCnt + 1;
	guiAICoverMoveIdentity[ubID] = pSoldier->uiUniqueSoldierIdValue;
}

BOOLEAN AIShouldRejectCoverOscillation(SOLDIERTYPE *pSoldier, INT32 sCandidateGrid)
{
	AIMaintainCoverMoveMemory();
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS ||
		TileIsOutOfBounds(sCandidateGrid))
		return FALSE;

	UINT8 ubID = pSoldier->ubID;
	if (guiAICoverMoveIdentity[ubID] != pSoldier->uiUniqueSoldierIdValue ||
		guiAICoverMoveTurn[ubID] == 0)
		return FALSE;

	UINT32 uiNow = guiTurnCnt + 1;
	UINT32 uiAge = uiNow >= guiAICoverMoveTurn[ubID] ?
		uiNow - guiAICoverMoveTurn[ubID] : 99;
	UINT32 uiPreviousAge =
		guiAICoverMovePreviousTurn[ubID] != 0 && uiNow >= guiAICoverMovePreviousTurn[ubID] ?
		uiNow - guiAICoverMovePreviousTurn[ubID] : 99;

	BOOLEAN fImmediateReverse =
		uiAge <= 3 &&
		pSoldier->sGridNo == gsAICoverMoveTo[ubID] &&
		sCandidateGrid == gsAICoverMoveFrom[ubID];
	BOOLEAN fReturnToRecentPosition =
		uiPreviousAge <= 3 &&
		!TileIsOutOfBounds(gsAICoverMovePreviousFrom[ubID]) &&
		sCandidateGrid == gsAICoverMovePreviousFrom[ubID];

	if (!fImmediateReverse && !fReturnToRecentPosition)
		return FALSE;

	// Immediate survival always beats hysteresis.
	if (pSoldier->aiData.bUnderFire && ShockLevelPercent(pSoldier) >= 60)
		return FALSE;

	UINT16 usCurrentExposure = AIKnownThreatExposure(
		pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
	UINT16 usCandidateExposure = AIKnownThreatExposure(
		pSoldier, sCandidateGrid, pSoldier->pathing.bLevel);
	if (usCandidateExposure + 50 < usCurrentExposure)
		return FALSE;

	if (!AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) &&
		AnyCoverAtSpot(pSoldier, sCandidateGrid))
		return FALSE;

	return TRUE;
}

BOOLEAN AIDisengagementActive(SOLDIERTYPE *pSoldier)
{
	AIMaintainDisengagementTimeline();

	if (!AICombatTeam(pSoldier) || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return FALSE;

	if (guiAIDisengageIdentity[pSoldier->ubID] != pSoldier->uiUniqueSoldierIdValue)
		return FALSE;

	return (gubAIDisengageTurns[pSoldier->ubID] > 0 &&
		(pSoldier->aiData.bAlertStatus >= STATUS_RED ||
		 gubAIForcedDisengageTurns[pSoldier->ubID] > 0));
}

BOOLEAN AIForcedDisengagementActive(SOLDIERTYPE *pSoldier)
{
	AIMaintainDisengagementTimeline();

	if (!AICombatTeam(pSoldier) || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return FALSE;

	if (guiAIDisengageIdentity[pSoldier->ubID] != pSoldier->uiUniqueSoldierIdValue)
		return FALSE;

	return gubAIForcedDisengageTurns[pSoldier->ubID] > 0;
}

void AIForceDisengagementState(SOLDIERTYPE *pSoldier, UINT8 ubTurns)
{
	AIMaintainDisengagementTimeline();

	if (!pSoldier || !AICombatTeam(pSoldier) || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	UINT8 ubID = pSoldier->ubID;
	UINT32 uiTurnStamp = guiTurnCnt + 1;

	guiAIDisengageIdentity[ubID] = pSoldier->uiUniqueSoldierIdValue;
	guiAIDisengageTurnStamp[ubID] = uiTurnStamp;
	if (gubAIDisengageTurns[ubID] == 0)
		guiAIDisengageStartTurn[ubID] = uiTurnStamp;

	gubAIDisengageTurns[ubID] = __max(gubAIDisengageTurns[ubID], __max((UINT8)1, ubTurns));
	gubAIForcedDisengageTurns[ubID] = __max(gubAIForcedDisengageTurns[ubID], __max((UINT8)1, ubTurns));
	AIResetRecoveryStreak(pSoldier);
}

void AIClearDisengagementState(SOLDIERTYPE *pSoldier)
{
	AIMaintainDisengagementTimeline();

	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	UINT8 ubID = pSoldier->ubID;
	if (guiAIDisengageIdentity[ubID] == pSoldier->uiUniqueSoldierIdValue)
	{
		gubAIDisengageTurns[ubID] = 0;
		gubAIForcedDisengageTurns[ubID] = 0;
		guiAIDisengageTurnStamp[ubID] = 0;
		guiAIDisengageStartTurn[ubID] = 0;
	}

	AIResetRecoveryStreak(pSoldier);
}

static BOOLEAN AIHasNearbyStableLeader(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier))
		return FALSE;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier ||
			!pFriend->bActive || !pFriend->bInSector ||
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			pFriend->pathing.bLevel != pSoldier->pathing.bLevel ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > TACTICAL_RANGE / 2 ||
			AIDisengagementActive(pFriend) || AIEscapeActive(pFriend) ||
			pFriend->aiData.bUnderFire)
		{
			continue;
		}

		if (AICheckIsLeader(pFriend))
			return TRUE;
	}

	return FALSE;
}

static void AIResetRecoveryStreak(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	UINT8 ubID = pSoldier->ubID;
	gubAIRecoveryStreak[ubID] = 0;
	guiAIRecoveryTurnStamp[ubID] = 0;
	guiAIRecoveryIdentity[ubID] = pSoldier->uiUniqueSoldierIdValue;
}

static UINT8 AIUpdateRecoveryStreak(SOLDIERTYPE *pSoldier, INT8 bSituation,
	UINT8 ubRoutPressure, BOOLEAN fLastSurvivor)
{
	if (!AICombatTeam(pSoldier) || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return 0;

	UINT8 ubID = pSoldier->ubID;
	if (guiAIRecoveryIdentity[ubID] != pSoldier->uiUniqueSoldierIdValue)
	{
		gubAIRecoveryStreak[ubID] = 0;
		guiAIRecoveryTurnStamp[ubID] = 0;
		guiAIRecoveryIdentity[ubID] = pSoldier->uiUniqueSoldierIdValue;
	}

	UINT32 uiTurnStamp = guiTurnCnt + 1;
	if (guiAIRecoveryTurnStamp[ubID] == uiTurnStamp)
		return gubAIRecoveryStreak[ubID];

	guiAIRecoveryTurnStamp[ubID] = uiTurnStamp;

	BOOLEAN fStableSituation =
		(bSituation == AI_BATTLE_WINNING || bSituation == AI_BATTLE_EVEN) &&
		!fLastSurvivor &&
		!pSoldier->aiData.bUnderFire &&
		AILocalStress(pSoldier) < 35 &&
		AIPersonalRisk(pSoldier) < AIPersonalRiskTolerance(pSoldier) &&
		ubRoutPressure < 35;

	BOOLEAN fLocalSupport =
		AICountNearbyOperationalFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 4) >= 2 ||
		AIHasNearbyStableLeader(pSoldier) ||
		(AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) &&
		 (pSoldier->LastAttackHit() ||
		  (pSoldier->usSoldierFlagMask2 & SOLDIER_SUCCESSFUL_ATTACK) ||
		  pSoldier->LastTargetSuppressed()));

	if (fStableSituation && fLocalSupport)
		gubAIRecoveryStreak[ubID] = __min((UINT8)4, (UINT8)(gubAIRecoveryStreak[ubID] + 1));
	else if (bSituation == AI_BATTLE_LOSING || bSituation == AI_BATTLE_CATASTROPHIC ||
		fLastSurvivor || pSoldier->aiData.bUnderFire || ubRoutPressure >= 50)
		gubAIRecoveryStreak[ubID] = 0;
	else if (gubAIRecoveryStreak[ubID] > 0)
		--gubAIRecoveryStreak[ubID];

	return gubAIRecoveryStreak[ubID];
}

static BOOLEAN AIEscapeEstablishedForRout(SOLDIERTYPE *pSoldier)
{
	if (!AIEscapeActive(pSoldier) || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return FALSE;

	UINT32 uiTurnStamp = guiTurnCnt + 1;
	return (guiAIEscapeStartTurn[pSoldier->ubID] != 0 &&
		guiAIEscapeStartTurn[pSoldier->ubID] < uiTurnStamp);
}

static BOOLEAN AIDisengagementEstablishedForRout(SOLDIERTYPE *pSoldier)
{
	if (!AIDisengagementActive(pSoldier) || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return FALSE;

	UINT32 uiTurnStamp = guiTurnCnt + 1;
	return (guiAIDisengageStartTurn[pSoldier->ubID] != 0 &&
		guiAIDisengageStartTurn[pSoldier->ubID] < uiTurnStamp);
}


static BOOLEAN AIShouldStartDisengagementFromState(SOLDIERTYPE *pSoldier, INT8 bSituation, UINT8 ubCasualties, BOOLEAN fLastSurvivor, UINT8 ubRoutPressure)
{
	INT32 iHoldConfidence = AIHoldGroundConfidence(pSoldier, bSituation,
		ubCasualties, fLastSurvivor, ubRoutPressure);
	INT32 iStress = AILocalStress(pSoldier);
	INT32 iRisk = AIPersonalRisk(pSoldier);
	INT32 iTolerance = AIPersonalRiskTolerance(pSoldier);

	// Winning troops hold unless the individual is in genuinely acute danger.
	if (bSituation == AI_BATTLE_WINNING)
		return (iHoldConfidence < 30 && iRisk >= iTolerance + 25 && iStress >= 65);

	// Even fights are normally fought out. Tactical fallback remains available
	// independently, so disengagement is reserved for a local collapse.
	if (bSituation == AI_BATTLE_EVEN)
	{
		return (iHoldConfidence < 25 &&
			ubCasualties >= 55 &&
			ubRoutPressure >= 70 &&
			iStress >= 50 &&
			iRisk >= iTolerance + 10);
	}

	if (fLastSurvivor)
		return (iHoldConfidence < 35 && (iRisk >= iTolerance || iStress >= 55));

	if (bSituation == AI_BATTLE_CATASTROPHIC)
	{
		// A catastrophically beaten, isolated and uncovered element should not need
		// another persistence/rout gate before it is allowed to break contact.
		// This is organized disengagement only; sector escape still uses its stricter
		// collapse-streak logic below the tactical layer.
		BOOLEAN fCatastrophicLocalCollapse =
			ubCasualties >= 40 &&
			AISeverelyIsolated(pSoldier) &&
			!AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) &&
			iHoldConfidence < 50;
		if (fCatastrophicLocalCollapse)
			return TRUE;

		// A strong covered element may keep fighting even when the wider ratio is bad.
		// Otherwise organized disengagement is appropriate before full rout.
		if (iHoldConfidence >= 50 && iRisk < iTolerance + 15)
			return FALSE;

		return (iHoldConfidence < 45 &&
			(iRisk >= iTolerance ||
			 iStress >= 50 ||
			 ubRoutPressure >= 60 ||
			 ubCasualties >= 50));
	}

	INT32 iRoutThreshold = 55 +
		(AIPersonalRiskTolerance(pSoldier) - 50) / 2 +
		AIBoundedDecisionJitter(pSoldier, 239u, 4);
	iRoutThreshold = __max(45, __min(75, iRoutThreshold));

	if (bSituation == AI_BATTLE_LOSING)
	{
		if (iHoldConfidence >= 60)
			return FALSE;

		INT32 iDisengageThreshold = 48 + AIProfessionalismModifier(pSoldier) / 2 +
			AIBoundedDecisionJitter(pSoldier, 241u, 4);
		iDisengageThreshold = __max(42, __min(60, iDisengageThreshold));

		if (iHoldConfidence < 35 &&
			ubCasualties >= iDisengageThreshold &&
			(iStress >= 45 || iRisk >= iTolerance + 10))
		{
			return TRUE;
		}

		if (iHoldConfidence < 30 &&
			ubRoutPressure >= iRoutThreshold &&
			iStress >= 40 &&
			iRisk >= iTolerance)
		{
			return TRUE;
		}
	}

	if (ubCasualties >= 65 &&
		bSituation != AI_BATTLE_WINNING &&
		AISeverelyIsolated(pSoldier) &&
		iHoldConfidence < 30 &&
		iRisk >= iTolerance)
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN AIShouldStartDisengagement(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier) || pSoldier->IsZombie() ||
		pSoldier->ubProfile != NO_PROFILE ||
		pSoldier->aiData.bAlertStatus < STATUS_RED ||
		pSoldier->aiData.bAttitude == ATTACKSLAYONLY ||
		AIPerceivedEnemyStrength(pSoldier) == 0)
	{
		return FALSE;
	}

	INT8 bSituation = AIBattleSituation(pSoldier);
	UINT8 ubCasualties = AIFriendlyCasualtyPercent(pSoldier);
	BOOLEAN fLastSurvivor = AILastSurvivorPressure(pSoldier);

	// Hold is authoritative in ordinary combat. Only genuine catastrophic collapse
	// or true last-survivor pressure may elevate survival above a fixed mission.
	if (pSoldier->aiData.bOrders == STATIONARY &&
		bSituation != AI_BATTLE_CATASTROPHIC && !fLastSurvivor)
	{
		return FALSE;
	}

	return AIShouldStartDisengagementFromState(pSoldier, bSituation, ubCasualties,
		fLastSurvivor, AILocalRoutPressure(pSoldier));
}
BOOLEAN AIUpdateDisengagementState(SOLDIERTYPE *pSoldier)
{
	AIMaintainDisengagementTimeline();

	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return FALSE;

	UINT8 ubID = pSoldier->ubID;
	if (guiAIDisengageIdentity[ubID] != pSoldier->uiUniqueSoldierIdValue)
	{
		gubAIDisengageTurns[ubID] = 0;
		gubAIForcedDisengageTurns[ubID] = 0;
		guiAIDisengageTurnStamp[ubID] = 0;
		guiAIDisengageIdentity[ubID] = pSoldier->uiUniqueSoldierIdValue;
		guiAIDisengageStartTurn[ubID] = 0;
	}

	BOOLEAN fForcedMilitiaRetreat =
		pSoldier->bTeam == MILITIA_TEAM &&
		gubAIForcedDisengageTurns[ubID] > 0;

	if (!AICombatTeam(pSoldier) ||
		pSoldier->ubProfile != NO_PROFILE ||
		pSoldier->IsZombie() ||
		(pSoldier->aiData.bAlertStatus < STATUS_RED && !fForcedMilitiaRetreat))
	{
		gubAIDisengageTurns[ubID] = 0;
		gubAIForcedDisengageTurns[ubID] = 0;
		guiAIDisengageStartTurn[ubID] = 0;
		AIClearEscapeState(pSoldier);
		return FALSE;
	}
	// A remnant that has just successfully joined a functioning element gets a short
	// stabilization window. Otherwise the same casualty/rout snapshot can immediately
	// recreate disengagement on the next sub-decision. Never cancel a player-forced
	// militia Retreat merely because that soldier reattached shortly beforehand.
	if (AIRecentlyReattachedFireteamRemnant(pSoldier) &&
		!AIForcedDisengagementActive(pSoldier))
	{
		gubAIDisengageTurns[ubID] = 0;
		gubAIForcedDisengageTurns[ubID] = 0;
		guiAIDisengageTurnStamp[ubID] = 0;
		guiAIDisengageStartTurn[ubID] = 0;
		AIClearEscapeState(pSoldier);
		AIResetRecoveryStreak(pSoldier);
		return FALSE;
	}

	INT8 bSituation = AIBattleSituation(pSoldier);
	UINT8 ubCasualties = AIFriendlyCasualtyPercent(pSoldier);
	BOOLEAN fLastSurvivor = AILastSurvivorPressure(pSoldier);
	UINT8 ubRoutPressure = AILocalRoutPressure(pSoldier);

	// Escape is a higher survival state than disengagement. Update it before
	// checking ordinary tactical orders so a catastrophic last survivor can
	// abandon even a STATIONARY mission when survival has fully taken priority.
	AIUpdateEscapeStateFromSnapshot(pSoldier, bSituation, ubCasualties,
		fLastSurvivor, ubRoutPressure);

	INT32 iHoldConfidence = AIHoldGroundConfidence(pSoldier, bSituation,
		ubCasualties, fLastSurvivor, ubRoutPressure);
	BOOLEAN fStationaryHold =
		pSoldier->aiData.bOrders == STATIONARY &&
		bSituation != AI_BATTLE_CATASTROPHIC && !fLastSurvivor;
	BOOLEAN fShouldDisengage =
		!fStationaryHold &&
		bSituation != AI_BATTLE_UNKNOWN &&
		AIShouldStartDisengagementFromState(
			pSoldier, bSituation, ubCasualties, fLastSurvivor, ubRoutPressure);

	INT32 iDisengagePressure = __min(150,
		(100 - iHoldConfidence) +
		AILocalStress(pSoldier) / 4 +
		ubRoutPressure / 5);

	VRAnalyticsTacticalCandidate(
		pSoldier->ubID,
		"organized_disengagement",
		pSoldier->sGridNo,
		100 - iHoldConfidence,
		iDisengagePressure,
		fShouldDisengage,
		fStationaryHold ? "stationary_hold_order" :
		(fShouldDisengage ? "low_hold_confidence_and_local_pressure" :
		 "disengagement_gates_not_met"));

	if (fStationaryHold)
	{
		gubAIDisengageTurns[ubID] = 0;
		gubAIForcedDisengageTurns[ubID] = 0;
		guiAIDisengageStartTurn[ubID] = 0;
		return FALSE;
	}

	UINT32 uiTurnStamp = guiTurnCnt + 1;

	// Decay once per tactical turn, never once per AI sub-decision.
	if (guiAIDisengageTurnStamp[ubID] != uiTurnStamp)
	{
		guiAIDisengageTurnStamp[ubID] = uiTurnStamp;
		if (gubAIDisengageTurns[ubID] > 0)
		{
			--gubAIDisengageTurns[ubID];
			if (gubAIDisengageTurns[ubID] == 0)
				guiAIDisengageStartTurn[ubID] = 0;
		}
		if (gubAIForcedDisengageTurns[ubID] > 0)
			--gubAIForcedDisengageTurns[ubID];
	}

	if (gubAIDisengageTurns[ubID] > 0 && gubAIForcedDisengageTurns[ubID] == 0)
	{
		UINT8 ubRecoveryStreak = AIUpdateRecoveryStreak(pSoldier, bSituation,
			ubRoutPressure, fLastSurvivor);
		UINT8 ubRequiredRecovery = AIHasNearbyStableLeader(pSoldier) ? 1 : 2;

		if (ubRecoveryStreak >= ubRequiredRecovery)
		{
			gubAIDisengageTurns[ubID] = 0;
			guiAIDisengageStartTurn[ubID] = 0;
			AIResetRecoveryStreak(pSoldier);
			return FALSE;
		}
	}

	if (fShouldDisengage)
	{
		UINT8 ubDuration = (bSituation == AI_BATTLE_CATASTROPHIC || fLastSurvivor) ? 3 : 2;
		if (gubAIDisengageTurns[ubID] == 0)
		{
			guiAIDisengageStartTurn[ubID] = uiTurnStamp;
			AIResetRecoveryStreak(pSoldier);
		}
		gubAIDisengageTurns[ubID] = __max(gubAIDisengageTurns[ubID], ubDuration);
	}

	return (gubAIDisengageTurns[ubID] > 0);
}
BOOLEAN AIShouldAvoidAdvance(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier))
		return FALSE;
	if (AIEscapeActive(pSoldier) || AIDisengagementActive(pSoldier))
		return TRUE;


	INT8 bSituation = AIBattleSituation(pSoldier);

	if (bSituation == AI_BATTLE_CATASTROPHIC)
		return TRUE;

	if (bSituation == AI_BATTLE_LOSING &&
		(AISeverelyIsolated(pSoldier) || AILastSurvivorPressure(pSoldier)))
	{
		return TRUE;
	}

	// A heavily depleted one/two-man element should not initiate another advance
	// merely because the exact known force ratio happens to classify as EVEN.
	if (AILastSurvivorPressure(pSoldier) && bSituation != AI_BATTLE_WINNING)
		return TRUE;

	return FALSE;
}

// Exposure estimate for new human-tactical behaviour. Unlike EnemyCanAttackSpot(),
// this deliberately does not inspect an opponent's actual current life, position,
// equipment or AP. It reasons only from JA2 personal/public knowledge.
UINT16 AIKnownThreatExposure(SOLDIERTYPE *pSoldier, INT32 sSpot, INT8 bLevel)
{
	if (!AICombatTeam(pSoldier) || TileIsOutOfBounds(sSpot))
		return 0;

	UINT32 uiExposure = 0;
	for (UINT16 uiLoop = 0; uiLoop < MAX_NUM_SOLDIERS; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercPtrs[uiLoop];
		if (!pOpponent || pOpponent == pSoldier)
			continue;

		INT32 sKnownSpot = NOWHERE;
		INT8 bKnownLevel = 0;
		INT8 bKnowledge = NOT_HEARD_OR_SEEN;
		UINT8 ubConfidence = 0;

		if (!AIPlanningContactForOpponent(
			pSoldier, pOpponent->ubID, &sKnownSpot, &bKnownLevel,
			&ubConfidence, &bKnowledge))
		{
			continue;
		}

		const BOOLEAN fDirectVisualContact =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;
		if (fDirectVisualContact &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW))
		{
			continue;
		}

		if (TileIsOutOfBounds(sKnownSpot))
			continue;

		// Reported/stale contacts influence movement only through believed geometry and
		// confidence. No target weapon, AP, stance, wounds or current position is read.
		if (PythSpacesAway(sKnownSpot, sSpot) <= MAX_VISION_RANGE &&
			LocationToLocationLineOfSightTest(
				sKnownSpot, bKnownLevel, sSpot, bLevel,
				TRUE, MAX_VISION_RANGE))
		{
			uiExposure += ubConfidence;
		}
	}

	return (UINT16)__min((UINT32)65535, uiExposure);
}

BOOLEAN AIKnownRouteExposureAcceptable(
	SOLDIERTYPE *pSoldier, INT32 sDestination, INT8 bAction,
	UINT16 usPeakIncrease, UINT16 usUncoveredIncrease, UINT16 usAverageIncrease)
{
	if (!pSoldier || TileIsOutOfBounds(sDestination) ||
		sDestination == pSoldier->sGridNo)
	{
		return FALSE;
	}

	INT32 iPathSteps = FindBestPath(
		pSoldier, sDestination, pSoldier->pathing.bLevel,
		DetermineMovementMode(pSoldier, bAction), NO_COPYROUTE, 0);
	if (iPathSteps <= 0 || !guiPathingData)
		return FALSE;

	UINT16 usCurrentExposure = AIKnownThreatExposure(
		pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
	UINT32 uiExposureTotal = 0;
	UINT8 ubSamples = 0;
	INT32 sRouteSpot = pSoldier->sGridNo;
	INT32 iPathLimit = __min(iPathSteps, (INT32)MAX_PATH_DATA_LENGTH);

	for (INT32 iStep = 0; iStep < iPathLimit; ++iStep)
	{
		INT32 sNext = NewGridNo(
			sRouteSpot, DirectionInc((UINT8)guiPathingData[iStep]));
		if (sNext == sRouteSpot || TileIsOutOfBounds(sNext))
			return FALSE;

		sRouteSpot = sNext;
		INT32 iStepNo = iStep + 1;
		BOOLEAN fSample =
			(iStepNo == __max(1, iPathLimit / 3)) ||
			(iStepNo == __max(1, (iPathLimit * 2) / 3)) ||
			(iStepNo == iPathLimit);

		if (!fSample)
			continue;

		if (InGas(pSoldier, sRouteSpot) ||
			RedSmokeDanger(sRouteSpot, pSoldier->pathing.bLevel) ||
			FindBombNearby(pSoldier, sRouteSpot, BOMB_DETECTION_RANGE))
		{
			return FALSE;
		}

		UINT16 usExposure = AIKnownThreatExposure(
			pSoldier, sRouteSpot, pSoldier->pathing.bLevel);
		uiExposureTotal += usExposure;
		++ubSamples;

		if (usExposure > usCurrentExposure + usPeakIncrease)
			return FALSE;

		if (usExposure > usCurrentExposure + usUncoveredIncrease &&
			!InSmokeNearby(sRouteSpot, pSoldier->pathing.bLevel) &&
			!SightCoverAtSpot(pSoldier, sRouteSpot, FALSE))
		{
			return FALSE;
		}
	}

	if (ubSamples > 0 &&
		uiExposureTotal / ubSamples > (UINT32)usCurrentExposure + usAverageIncrease)
	{
		return FALSE;
	}

	return TRUE;
}

BOOLEAN AIShouldConsiderTacticalFallback(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier) || pSoldier->IsZombie() ||
		AIHasUsedTacticalFallback(pSoldier) ||
		pSoldier->aiData.bOrders == STATIONARY || AIShouldAvoidAdvance(pSoldier))
	{
		return FALSE;
	}

	AITACTICALDECISIONCONTEXT Context;
	if (!AIBuildTacticalDecisionContext(pSoldier, &Context) ||
		TileIsOutOfBounds(Context.sPrimaryThreat))
	{
		return FALSE;
	}

	// Do not shuffle a soldier who is currently succeeding from a sound position.
	if (!Context.fUnderFire &&
		Context.fHasCover &&
		(pSoldier->LastAttackHit() ||
		 (pSoldier->usSoldierFlagMask2 & SOLDIER_SUCCESSFUL_ATTACK)) &&
		Context.iStress < 25 &&
		!Context.fBadRange)
	{
		return FALSE;
	}

	// A soldier with a live personal contact from a defensible firing position
	// should normally exploit that position before making a generic fallback move.
	if (Context.fHasLivePersonalContact &&
		!Context.fUnderFire &&
		Context.fHasCover &&
		Context.iStress < 30 &&
		Context.iPersonalRisk + 10 < Context.iRiskTolerance &&
		!Context.fBadRange)
	{
		return FALSE;
	}

	INT32 iPressure = 0;

	// Ordinary contact pressure is not, by itself, a reason to step backwards.
	// The soldier should normally keep attacking/advancing unless his own position
	// has a concrete tactical problem that a fallback can actually solve.
	if (Context.bBattleSituation == AI_BATTLE_LOSING)
		iPressure += 1;
	if (Context.fUnderFire)
		iPressure += 1;
	if (!Context.fHasCover)
		iPressure += 2;
	if (Context.iPersonalRisk >= Context.iRiskTolerance + 10)
		iPressure += 2;
	else if (Context.iPersonalRisk >= Context.iRiskTolerance)
		iPressure += 1;
	if (Context.iStress >= 45)
		iPressure += 1;
	if (Context.fBadRange)
		iPressure += 1;
	if (Context.fIsolated)
		iPressure += 1;

	const BOOLEAN fConcreteFallbackNeed =
		!Context.fHasCover ||
		Context.iPersonalRisk >= Context.iRiskTolerance ||
		Context.iStress >= 45 ||
		Context.fBadRange ||
		Context.fIsolated;
	if (!fConcreteFallbackNeed)
		return FALSE;

	// Balanced fallback gate: normal troops keep fighting from workable positions,
	// while combined exposure/risk can still justify one positional concession.
	INT32 iThreshold = (Context.bBattleSituation == AI_BATTLE_WINNING) ? 5 : 4;
	if (pSoldier->aiData.bAttitude == AGGRESSIVE ||
		pSoldier->aiData.bAttitude == ATTACKSLAYONLY)
	{
		++iThreshold;
	}

	// SEEKENEMY already biases ordinary RED logic toward closing. Add resistance to
	// fallback only when the soldier is currently safe; exposed/high-risk seekers
	// must still be allowed to make their single sensible bound back to cover.
	if (pSoldier->aiData.bOrders == SEEKENEMY &&
		Context.fHasCover && Context.iPersonalRisk < Context.iRiskTolerance)
	{
		++iThreshold;
	}

	return (iPressure >= iThreshold);
}

// Human-like local combat stress. This deliberately affects tactical morale and
// behaviour rather than adding another direct CTH penalty: NCTH already accounts
// for injury, fatigue, morale and shock in the shooting calculation.
INT32 AILocalStress(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier) || pSoldier->stats.bLifeMax <= 0)
		return 0;

	INT32 iStress = (2 * ShockLevelPercent(pSoldier)) / 3;

	if (pSoldier->aiData.bUnderFire)
		iStress += 10;
	if (pSoldier->bBleeding > 0)
		iStress += __min((INT32)12, (INT32)pSoldier->bBleeding / 4);
	if (pSoldier->bBreath < 50)
		iStress += 5;
	if (pSoldier->bBreath < 25)
		iStress += 8;
	if (!AnyCoverAtSpot(pSoldier, pSoldier->sGridNo))
		iStress += 8;

	// Visible fresh friendly bodies make morale brittle, especially in a local fight.
	INT32 iFreshCorpses = CountCorpses(pSoldier, pSoldier->sGridNo,
		DAY_VISION_RANGE / 2, TRUE, TRUE);
	iStress += 12 * __min((INT32)3, iFreshCorpses);

	UINT8 ubNearbyFriends = AICountNearbyOperationalFriends(pSoldier, pSoldier->sGridNo,
		DAY_VISION_RANGE / 4);
	if (ubNearbyFriends == 0)
		iStress += 12;
	else if (ubNearbyFriends >= 3)
		iStress -= 8;

	// Small stabilising effects: success and effective team pressure help, but do
	// not erase severe suppression, wounds or casualties.
	if (pSoldier->LastAttackHit() ||
		(pSoldier->usSoldierFlagMask2 & SOLDIER_SUCCESSFUL_ATTACK))
		iStress -= 5;
	if (pSoldier->LastTargetSuppressed())
		iStress -= 5;

	if (pSoldier->aiData.bAttitude == ATTACKSLAYONLY)
		iStress -= 10;

	// Training changes composure only modestly; shock, wounds and isolation remain dominant.
	iStress -= AIProfessionalismModifier(pSoldier) / 2;

	return __max(0, __min(100, iStress));
}

// Individual danger assessment used by tactical self-preservation.
// This is intentionally separate from squad morale: morale says whether the fight
// looks winnable, while this score says how dangerous the soldier's own position is.
INT32 AIPersonalRisk(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->stats.bLifeMax <= 0)
		return 0;

	INT32 iHealthPercent = (100 * pSoldier->stats.bLife) / pSoldier->stats.bLifeMax;
	INT32 iRisk = 0;

	// Wounds matter increasingly as remaining health gets low.
	if (iHealthPercent < 75)
		iRisk += (75 - iHealthPercent) / 2;
	if (iHealthPercent < 50)
		iRisk += 10;
	if (iHealthPercent < 25)
		iRisk += 15;

	// Suppression, bleeding and exhaustion increase the urgency to preserve oneself.
	iRisk += ShockLevelPercent(pSoldier) / 3;
	iRisk += __min((INT32)15, (INT32)pSoldier->bBleeding / 5);
	if (pSoldier->bBreath < 25)
		iRisk += 8;

	// Immediate tactical danger.
	if (pSoldier->aiData.bUnderFire)
		iRisk += 10;
	if (!AnyCoverAtSpot(pSoldier, pSoldier->sGridNo))
		iRisk += 12;

	// Isolation raises risk; nearby conscious allies reduce it.
	UINT8 ubNearbyFriends = AICountNearbyOperationalFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 4);
	if (ubNearbyFriends == 0)
		iRisk += 15;
	else if (ubNearbyFriends == 1)
		iRisk += 7;
	else if (ubNearbyFriends >= 3)
		iRisk -= 5;

	return __max(0, __min(100, iRisk));
}

// Unit quality changes cohesion, not accuracy or action points.
static INT8 AIProfessionalismModifier(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier)
		return 0;

	// Enemy class no longer encodes training quality. Every live enemy combatant
	// represents the same exceptionally drilled force; class only changes resources.
	if (pSoldier->bTeam == ENEMY_TEAM)
		return 12;

	INT32 iModifier = 0;

	switch (pSoldier->ubSoldierClass)
	{
	case SOLDIER_CLASS_ADMINISTRATOR: iModifier -= 8; break;
	case SOLDIER_CLASS_ARMY: iModifier += 2; break;
	case SOLDIER_CLASS_ELITE: iModifier += 10; break;
	case SOLDIER_CLASS_GREEN_MILITIA: iModifier -= 6; break;
	case SOLDIER_CLASS_REG_MILITIA: break;
	case SOLDIER_CLASS_ELITE_MILITIA: iModifier += 7; break;
	}

	UINT8 ubAuthority = AICommandAuthority(pSoldier);
	if (ubAuthority >= 6)
		iModifier += 5;
	else if (ubAuthority >= 4)
		iModifier += 3;
	else if (ubAuthority >= 2)
		iModifier += 1;

	return (INT8)__max(-10, __min(15, iModifier));
}

// Doctrine is intentionally orthogonal to accuracy/AP. It describes how much
// initiative and coordination the soldier's formation plausibly possesses.
UINT8 AIGetDoctrineProfile(SOLDIERTYPE *pSoldier)
{
	// Doctrine now describes the mission, not intelligence/training. Every ENEMY_TEAM
	// combatant uses the same elite tactical brain. Fixed guards remain guard elements
	// so map assignments still matter; mobile troops use the same elite mobile doctrine
	// regardless of administrator/army/elite equipment class.
	if (!pSoldier || pSoldier->bTeam != ENEMY_TEAM)
		return AI_DOCTRINE_LINE;

	if ((pSoldier->usSoldierFlagMask & SOLDIER_VIP) ||
		(pSoldier->usSoldierFlagMask & SOLDIER_BODYGUARD) ||
		pSoldier->aiData.bOrders == STATIONARY ||
		pSoldier->aiData.bOrders == ONGUARD ||
		pSoldier->aiData.bOrders == SNIPER)
	{
		return AI_DOCTRINE_ELITE_GUARD;
	}

	return AI_DOCTRINE_ELITE_MOBILE;
}

BOOLEAN AIHasLocalCommandSupport(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier)
		return FALSE;
	if (!AICombatTeam(pSoldier))
		return TRUE;

	if (AICheckIsLeader(pSoldier))
		return TRUE;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pLeader = MercPtrs[iCounter];
		if (!pLeader || pLeader == pSoldier || !pLeader->bActive || !pLeader->bInSector ||
			pLeader->stats.bLife < OKLIFE || pLeader->bCollapsed || pLeader->bBreathCollapsed ||
			(pLeader->usSoldierFlagMask & SOLDIER_POW) ||
			(pLeader->flags.uiStatusFlags & SOLDIER_COWERING) ||
			pLeader->pathing.bLevel != pSoldier->pathing.bLevel ||
			AIDisengagementActive(pLeader) || AIEscapeActive(pLeader))
			continue;

		INT32 iDistance = PythSpacesAway(pSoldier->sGridNo, pLeader->sGridNo);
		UINT8 ubAuthority = AICommandAuthority(pLeader);
		if (ubAuthority >= 2)
		{
			// NCO command is fireteam-local. Lieutenants/Captains can coordinate a
			// neighbouring element at short range; Majors+ have a wider local command
			// radius. No rank grants sector-wide magical morale or information sharing.
			if (ubAuthority <= 3)
			{
				if (AISameFireteam(pSoldier, pLeader) &&
					iDistance <= __max(5, TACTICAL_RANGE / 3))
					return TRUE;
			}
			else if (ubAuthority <= 5)
			{
				if (iDistance <= TACTICAL_RANGE / 2 &&
					(AISameFireteam(pSoldier, pLeader) || iDistance <= TACTICAL_RANGE / 3))
					return TRUE;
			}
			else if (iDistance <= TACTICAL_RANGE)
			{
				return TRUE;
			}
		}

		// Militia do not always carry formal officer roles. Preserve the existing
		// experience hand-off, but keep it strictly local.
		if (pSoldier->bTeam == MILITIA_TEAM && iDistance <= TACTICAL_RANGE / 2)
		{
			if (pSoldier->ubSoldierClass == SOLDIER_CLASS_GREEN_MILITIA &&
				(pLeader->ubSoldierClass == SOLDIER_CLASS_REG_MILITIA ||
				 pLeader->ubSoldierClass == SOLDIER_CLASS_ELITE_MILITIA))
				return TRUE;
			if (pSoldier->ubSoldierClass == SOLDIER_CLASS_REG_MILITIA &&
				pLeader->ubSoldierClass == SOLDIER_CLASS_ELITE_MILITIA)
				return TRUE;
		}
	}

	return FALSE;
}

BOOLEAN AIAllowsComplexManeuver(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->bTeam != ENEMY_TEAM) return TRUE;

	// Intelligence is universal. The only remaining restriction is an explicit
	// mission role: a protected General commands while subordinates remain instead
	// of becoming the breach/utility specialist himself.
	if ((pSoldier->usSoldierFlagMask & SOLDIER_VIP) &&
		AICombatTeamOperationalCount(pSoldier) > 1)
	{
		return FALSE;
	}

	return TRUE;
}

static BOOLEAN AIHasOperationalGeneralInSector(void)
{
	for (UINT8 iCounter = gTacticalStatus.Team[ENEMY_TEAM].bFirstID;
		iCounter <= gTacticalStatus.Team[ENEMY_TEAM].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pGeneral = MercPtrs[iCounter];
		if (!pGeneral || !pGeneral->bActive || !pGeneral->bInSector ||
			pGeneral->stats.bLife < OKLIFE || pGeneral->bCollapsed || pGeneral->bBreathCollapsed ||
			(pGeneral->usSoldierFlagMask & SOLDIER_POW) ||
			!(pGeneral->usSoldierFlagMask & SOLDIER_VIP) ||
			(pGeneral->flags.uiStatusFlags & SOLDIER_COWERING) ||
			AIDisengagementActive(pGeneral) || AIEscapeActive(pGeneral))
		{
			continue;
		}
		return TRUE;
	}
	return FALSE;
}

BOOLEAN AIAllowsIndependentFlank(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->bTeam != ENEMY_TEAM) return TRUE;

	// Keep the General with the command element while there is anybody left to
	// command. If he is the last operational fighter, normal combat logic resumes.
	if ((pSoldier->usSoldierFlagMask & SOLDIER_VIP) &&
		AICombatTeamOperationalCount(pSoldier) > 1)
	{
		return FALSE;
	}

	if (pSoldier->bTeam == ENEMY_TEAM &&
		(pSoldier->usSoldierFlagMask & SOLDIER_BODYGUARD) &&
		AIHasOperationalGeneralInSector())
	{
		return FALSE;
	}
	return TRUE;
}

BOOLEAN AIAllowsProactiveSupport(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier) return FALSE;
	if (AIDisengagementActive(pSoldier) || AIEscapeActive(pSoldier)) return FALSE;
	// Every live enemy is trained to provide initiative-based local support.
	// Weapon/AP/LOS/risk constraints still determine what support is physically legal.
	return TRUE;
}

UINT8 AIDoctrineResponseLimit(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->bTeam != ENEMY_TEAM)
		return 4;

	UINT8 ubDoctrine = AIGetDoctrineProfile(pSoldier);
	UINT8 ubLimit = 4;
	switch (ubDoctrine)
	{
	case AI_DOCTRINE_SECURITY:     ubLimit = 2; break;
	case AI_DOCTRINE_LINE:         ubLimit = 4; break;
	case AI_DOCTRINE_VETERAN:      ubLimit = 5; break;
	case AI_DOCTRINE_ELITE_MOBILE: ubLimit = 6; break;
	case AI_DOCTRINE_ELITE_GUARD:  ubLimit = 4; break;
	}

	// ONCALL is the natural QRF order. SEEKENEMY has more freedom, but does not
	// empty a garrison as aggressively as a designated response element.
	if (pSoldier->aiData.bOrders == ONCALL)
		ubLimit += 2;
	else if (pSoldier->aiData.bOrders == SEEKENEMY)
		ubLimit += 1;

	// Senior officers can release a slightly larger local response. This is command
	// authority, not free reinforcements: the normal reserve budget and hard cap remain.
	UINT8 ubAuthority = AICommandAuthority(pSoldier);
	if (ubAuthority >= 5)
		ubLimit += 1;
	if (ubAuthority >= 7 && pSoldier->aiData.bOrders == ONCALL)
		ubLimit += 1;

	if (ubDoctrine == AI_DOCTRINE_SECURITY && ubLimit > 3)
		ubLimit = 3;

	return __min((UINT8)8, ubLimit);
}

INT8 AIDoctrineAnchorModifier(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->bTeam != ENEMY_TEAM)
		return 0;

	UINT8 ubDoctrine = AIGetDoctrineProfile(pSoldier);
	switch (ubDoctrine)
	{
	case AI_DOCTRINE_SECURITY:
		switch (pSoldier->aiData.bOrders)
		{
		case STATIONARY: return -6;
		case ONGUARD: return -5;
		case CLOSEPATROL:
		case POINTPATROL:
		case RNDPTPATROL: return -3;
		default: return -1;
		}

	case AI_DOCTRINE_LINE:
		if (pSoldier->aiData.bOrders == STATIONARY || pSoldier->aiData.bOrders == ONGUARD)
			return -2;
		if (pSoldier->aiData.bOrders == CLOSEPATROL)
			return -1;
		return 0;

	case AI_DOCTRINE_VETERAN:
		return (pSoldier->aiData.bOrders == STATIONARY) ? -1 : 0;

	case AI_DOCTRINE_ELITE_GUARD:
		return -3;

	default:
		return 0;
	}
}
// Individual willingness to accept danger. Personality and current morale change
// the threshold, but no ordinary attitude makes a soldier completely suicidal.
INT32 AIPersonalRiskTolerance(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier)
		return 50;

	INT32 iTolerance = 50;

	switch (pSoldier->aiData.bAttitude)
	{
	case DEFENSIVE:		iTolerance -= 10; break;
	case CUNNINGSOLO:
	case CUNNINGAID:	iTolerance -= 5; break;
	case BRAVESOLO:
	case BRAVEAID:		iTolerance += 8; break;
	case AGGRESSIVE:	iTolerance += 12; break;
	case ATTACKSLAYONLY:iTolerance += 20; break;
	}

	switch (pSoldier->aiData.bAIMorale)
	{
	case MORALE_HOPELESS:	iTolerance -= 20; break;
	case MORALE_WORRIED:	iTolerance -= 10; break;
	case MORALE_CONFIDENT:	iTolerance += 8; break;
	case MORALE_FEARLESS:	iTolerance += 15; break;
	}

	if (pSoldier->aiData.bOrders == SEEKENEMY)
		iTolerance += 5;

	iTolerance += AIProfessionalismModifier(pSoldier);

	// Enemy troops are universally brave and psychologically steady, but still
	// respect catastrophic danger, suppression and organized disengagement logic.
	if (pSoldier->bTeam == ENEMY_TEAM)
		return __max(70, __min(88, iTolerance));

	return __max(20, __min(85, iTolerance));
}

// Dynamic fireteam role suitability. These are not permanent classes: the score is
// recalculated from current weapon, position, wounds, fatigue and local stress, so a
// soldier can change from maneuver to support (or back) as the fight develops.
INT32 AISupportRoleScore(SOLDIERTYPE *pSoldier, INT32 sTargetSpot)
{
	if (!AICombatTeam(pSoldier) || !pSoldier->bActive || !pSoldier->bInSector ||
		pSoldier->stats.bLife < OKLIFE || pSoldier->bCollapsed || pSoldier->bBreathCollapsed ||
		(pSoldier->usSoldierFlagMask & SOLDIER_POW) ||
		(pSoldier->flags.uiStatusFlags & SOLDIER_COWERING) ||
		AIDisengagementActive(pSoldier) || AIEscapeActive(pSoldier) ||
		!AICheckHasGun(pSoldier) || AIGunAmmo(pSoldier) == 0)
	{
		return -10000;
	}

	if (TileIsOutOfBounds(sTargetSpot))
		sTargetSpot = AIPrimaryPlanningThreatSpot(pSoldier);

	INT32 iScore = 20;
	INT32 iGunRange = __max(1, (INT32)AIGunRange(pSoldier) / CELL_X_SIZE);
	UINT16 usLoadedAmmo = AIGunAmmo(pSoldier);

	if (AICheckIsMachinegunner(pSoldier))
		iScore += 35;
	if (AICheckIsSniper(pSoldier))
		iScore += 30;
	else if (AICheckIsMarksman(pSoldier))
		iScore += 18;
	if (AIGunAutofireCapable(pSoldier))
		iScore += 10;

	if (AICheckIsRadioOperator(pSoldier))
		iScore += 14;
	if (AICheckIsCommander(pSoldier))
		iScore += 12;
	else if (AICheckIsOfficer(pSoldier))
		iScore += 6;
	if (AICheckIsGLOperator(pSoldier))
		iScore += 12;
	if (AICheckIsMortarOperator(pSoldier))
		iScore += 10;

	iScore += __min((INT32)18, iGunRange / 2);
	iScore += __max(-8, __min(18, ((INT32)pSoldier->stats.bMarksmanship - 60) / 2));

	if (AnyCoverAtSpot(pSoldier, pSoldier->sGridNo))
		iScore += 16;
	if (SightCoverAtSpot(pSoldier, pSoldier->sGridNo, FALSE))
		iScore += 10;

	if (!TileIsOutOfBounds(sTargetSpot))
	{
		INT32 iDistance = PythSpacesAway(pSoldier->sGridNo, sTargetSpot);
		if (iDistance <= iGunRange)
			iScore += 12;
		else if (iDistance > iGunRange + iGunRange / 3)
			iScore -= 12;

		INT8 bRangePreference = AIEngagementRangeModifier(pSoldier, sTargetSpot);
		if (bRangePreference < 0)
			iScore += 6;
	}

	if (AICheckShortWeaponRange(pSoldier))
		iScore -= 15;

	// Fire-base value depends on ammunition actually ready in the gun. A nearly
	// empty LMG is still a support weapon, but it should not outrank a loaded rifle
	// as if it could sustain a burst. Once reloaded, the role score rises again.
	if (AIGunAutofireCapable(pSoldier))
	{
		if (usLoadedAmmo < 5)
			iScore -= AICheckIsMachinegunner(pSoldier) ? 30 : 18;
		else if (usLoadedAmmo < 10)
			iScore -= AICheckIsMachinegunner(pSoldier) ? 15 : 8;
		else if (usLoadedAmmo >= 20 && AICheckIsMachinegunner(pSoldier))
			iScore += 8;
	}
	else if (usLoadedAmmo <= 2)
	{
		iScore -= 8;
	}

	if (AICheckIsMedic(pSoldier))
		iScore -= 8;
	if (pSoldier->aiData.bUnderFire)
		iScore -= 10;

	iScore -= AILocalStress(pSoldier) / 4;
	iScore -= AIPersonalRisk(pSoldier) / 4;

	return __max(-100, __min(150, iScore));
}

INT32 AIManeuverRoleScore(SOLDIERTYPE *pSoldier, INT32 sTargetSpot)
{
	if (!AICombatTeam(pSoldier) || !pSoldier->bActive || !pSoldier->bInSector ||
		pSoldier->stats.bLife < OKLIFE || pSoldier->bCollapsed || pSoldier->bBreathCollapsed ||
		(pSoldier->usSoldierFlagMask & SOLDIER_POW) ||
		(pSoldier->flags.uiStatusFlags & SOLDIER_COWERING) ||
		AIDisengagementActive(pSoldier) || AIEscapeActive(pSoldier))
	{
		return -10000;
	}

	if (TileIsOutOfBounds(sTargetSpot))
		sTargetSpot = AIPrimaryPlanningThreatSpot(pSoldier);

	INT32 iHealthPercent = pSoldier->stats.bLifeMax > 0 ?
		(100 * pSoldier->stats.bLife) / pSoldier->stats.bLifeMax : 0;
	INT32 iScore = 20;

	iScore += (INT32)pSoldier->stats.bAgility / 6;
	iScore += (INT32)pSoldier->stats.bDexterity / 12;
	iScore += iHealthPercent / 6;
	iScore += (INT32)pSoldier->bBreath / 12;

	if (AICheckHasGun(pSoldier))
	{
		if (AIGunAmmo(pSoldier) == 0)
		{
			// An empty specialist gun does not make its owner the new assault man.
			// Reload/secondary-weapon logic should solve the ammunition problem first.
			iScore -= 20;
		}
		else if (AICheckShortWeaponRange(pSoldier))
		{
			iScore += 18;
		}
	}
	else if (FindAIUsableObjClass(pSoldier, IC_WEAPON) != NO_SLOT)
	{
		// A real melee weapon can justify closing distance, but should not outrank
		// a healthy rifleman merely because the legacy short-range helper treats no gun as short.
		iScore += 4;
	}
	else
	{
		iScore -= 40;
	}

	if (AICheckIsMachinegunner(pSoldier))
		iScore -= 32;
	if (AICheckIsSniper(pSoldier))
		iScore -= 35;
	else if (AICheckIsMarksman(pSoldier))
		iScore -= 18;
	if (AICheckIsMortarOperator(pSoldier))
		iScore -= 35;
	if (AICheckIsRadioOperator(pSoldier))
		iScore -= 24;
	if (AICheckIsCommander(pSoldier))
		iScore -= 18;
	else if (AICheckIsOfficer(pSoldier))
		iScore -= 8;
	if (AICheckIsGLOperator(pSoldier))
		iScore -= 12;
	if (AICheckIsMedic(pSoldier))
		iScore -= 10;

	FLOAT dScope = AIGunScopeMagFactor(pSoldier);
	if (dScope >= 4.0f)
		iScore -= 15;
	else if (dScope >= 2.0f)
		iScore -= 7;

	if (!TileIsOutOfBounds(sTargetSpot))
	{
		INT8 bRangePreference = AIEngagementRangeModifier(pSoldier, sTargetSpot);
		if (bRangePreference > 0)
			iScore += 8 * bRangePreference;
		else if (bRangePreference < 0)
			iScore += 8 * bRangePreference;
	}

	if (pSoldier->aiData.bUnderFire)
		iScore -= 15;
	iScore -= AILocalStress(pSoldier) / 3;
	iScore -= AIPersonalRisk(pSoldier) / 3;

	return __max(-100, __min(150, iScore));
}
// Score how much a candidate position creates a useful crossfire around a known contact.
// Positive scores favor roughly perpendicular/oblique angles; standing on the same axis
// as the rest of the fireteam is mildly discouraged. Only teammates with their own
// knowledge of essentially the same contact are considered.
INT32 AICrossfirePositionScore(SOLDIERTYPE *pSoldier, INT32 sCandidateSpot, INT32 sTargetSpot)
{
	if (!AICombatTeam(pSoldier) || TileIsOutOfBounds(sCandidateSpot) || TileIsOutOfBounds(sTargetSpot))
		return 0;

	// Crossfire geometry is an advanced coordination task. Ordinary line troops only
	// receive it while local command is intact; security troops do not improvise it.
	if (AICombatTeam(pSoldier) && !AIAllowsComplexManeuver(pSoldier))
		return 0;

	UINT8 ubCandidateDir = AIDirection(sTargetSpot, sCandidateSpot);
	if (ubCandidateDir == DIRECTION_IRRELEVANT)
		return 0;

	INT32 iBestAngleScore = -12;
	UINT8 ubRelevantFriends = 0;
	UINT8 ubSameAxisFriends = 0;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier || !pFriend->bActive || !pFriend->bInSector ||
			!AISameFireteam(pSoldier, pFriend) ||
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			AIDisengagementActive(pFriend) || AIEscapeActive(pFriend) ||
			!AICheckHasGun(pFriend) || AIGunAmmo(pFriend) == 0 ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > DAY_VISION_RANGE)
		{
			continue;
		}

		INT32 sFriendThreat = AIPrimaryPlanningThreatSpot(pFriend);
		if (TileIsOutOfBounds(sFriendThreat) || PythSpacesAway(sFriendThreat, sTargetSpot) > 3)
			continue;

		INT32 iFriendRange = __max(1, (INT32)AIGunRange(pFriend) / CELL_X_SIZE);
		if (PythSpacesAway(pFriend->sGridNo, sTargetSpot) > iFriendRange + iFriendRange / 4)
			continue;

		UINT8 ubFriendDir = AIDirection(sTargetSpot, pFriend->sGridNo);
		if (ubFriendDir == DIRECTION_IRRELEVANT)
			continue;

		INT32 iDelta = abs((INT32)ubCandidateDir - (INT32)ubFriendDir);
		iDelta = __min(iDelta, 8 - iDelta);
		INT32 iAngleScore = 0;
		switch (iDelta)
		{
		case 0: iAngleScore = -12; ++ubSameAxisFriends; break;
		case 1: iAngleScore = 4; break;
		case 2: iAngleScore = 18; break;
		case 3: iAngleScore = 28; break;
		default: iAngleScore = 12; break; // opposite sides: useful, but less ideal for friendly-fire geometry
		}

		++ubRelevantFriends;
		iBestAngleScore = __max(iBestAngleScore, iAngleScore);
	}

	if (ubRelevantFriends == 0)
		return 0;

	INT32 iScore = iBestAngleScore - 5 * __min((UINT8)2, ubSameAxisFriends);
	return __max(-20, __min(30, iScore));
}
// Local cooperation modifier for offensive movement.  Soldiers are more willing
// to advance when nearby teammates or teammates already engaging the same threat
// can support them, and less willing to push forward alone.
INT8 AIAdvanceSupportModifier(SOLDIERTYPE *pSoldier, INT32 sTargetSpot)
{
	if (!pSoldier)
		return 0;

	if (TileIsOutOfBounds(sTargetSpot))
		sTargetSpot = AIPrimaryPlanningThreatSpot(pSoldier);

	UINT8 ubNearbyFriends = 0;
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (pFriend && pFriend != pSoldier && pFriend->bActive && pFriend->bInSector &&
			pFriend->stats.bLife >= OKLIFE && !pFriend->bCollapsed && !pFriend->bBreathCollapsed &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW) &&
			!(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) &&
			!AIDisengagementActive(pFriend) && !AIEscapeActive(pFriend) &&
			AISameFireteam(pSoldier, pFriend) &&
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) <= DAY_VISION_RANGE / 4)
		{
			++ubNearbyFriends;
		}
	}
	INT32 iModifier = 0;
	BOOLEAN fSmallUnit = AISmallUnitTeamMode(pSoldier);

	if (ubNearbyFriends == 0)
		iModifier -= fSmallUnit ? 3 : 2;
	else if (ubNearbyFriends == 1)
		iModifier += fSmallUnit ? 2 : 0;
	else if (ubNearbyFriends == 2)
		iModifier += fSmallUnit ? 3 : 1;
	else if (ubNearbyFriends >= 3)
		iModifier += 3;

	if (!TileIsOutOfBounds(sTargetSpot))
	{
		// A teammate already in contact with this threat provides useful covering
		// pressure and makes a coordinated move less likely to become an isolated rush.
		if (CountFriendsBlack(pSoldier, sTargetSpot) > 0)
			iModifier += 1;

		if (AICheckWeOutnumberLocal(pSoldier, sTargetSpot))
			iModifier += 1;

		// Actual effective fire is more valuable than merely having friends nearby.
		// In sequential JA2 turns the shooter may already have spent his AP, but a
		// hit/suppression still creates the movement window the next teammate exploits.
		UINT8 ubEffectiveFire = AIFireteamEffectiveFireSupport(pSoldier, sTargetSpot);
		if (ubEffectiveFire == 1)
			iModifier += 2;
		else if (ubEffectiveFire == 2)
			iModifier += 3;
		else if (ubEffectiveFire >= 3)
			iModifier += 4;
	}

	return (INT8)__max(-3, __min(6, iModifier));
}

BOOLEAN AIAdvanceHasMutualSupport(SOLDIERTYPE *pSoldier, INT32 sAdvanceSpot, INT32 sTargetSpot, INT8 bTargetLevel)
{
	if (!AICombatTeam(pSoldier) ||
		TileIsOutOfBounds(sAdvanceSpot) ||
		TileIsOutOfBounds(sTargetSpot))
	{
		return TRUE;
	}

	if (sAdvanceSpot == pSoldier->sGridNo)
		return TRUE;

	UINT16 usCurrentExposure = AIKnownThreatExposure(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
	UINT16 usAdvanceExposure = AIKnownThreatExposure(pSoldier, sAdvanceSpot, pSoldier->pathing.bLevel);
	BOOLEAN fCurrentCover = AnyCoverAtSpot(pSoldier, pSoldier->sGridNo);
	BOOLEAN fAdvanceCover = AnyCoverAtSpot(pSoldier, sAdvanceSpot);
	INT32 iCurrentDist = PythSpacesAway(pSoldier->sGridNo, sTargetSpot);
	INT32 iAdvanceDist = PythSpacesAway(sAdvanceSpot, sTargetSpot);
	UINT8 ubDoctrine = AIGetDoctrineProfile(pSoldier);
	BOOLEAN fComplexDoctrine = AIAllowsComplexManeuver(pSoldier);
	UINT8 ubEffectiveFire = AIFireteamEffectiveFireSupport(pSoldier, sTargetSpot);

	// Lower-quality formations can still make sensible covered advances, but do not
	// independently solve exposed manoeuvre problems like a professional fireteam.
	if (AICombatTeam(pSoldier) && !fComplexDoctrine && iAdvanceDist + 2 < iCurrentDist)
	{
		if (ubDoctrine == AI_DOCTRINE_SECURITY &&
			((!fAdvanceCover && ubEffectiveFire == 0) ||
			 usAdvanceExposure > usCurrentExposure + (ubEffectiveFire > 0 ? 55 : 25)))
		{
			return FALSE;
		}

		if (ubDoctrine == AI_DOCTRINE_LINE &&
			((!fAdvanceCover && usAdvanceExposure >= usCurrentExposure && ubEffectiveFire == 0) ||
			 usAdvanceExposure > usCurrentExposure + (ubEffectiveFire >= 2 ? 120 : 80)))
		{
			return FALSE;
		}
	}

	// Fire-and-manoeuvre role separation. Two nearby soldiers may actively bound
	// toward essentially the same known contact. A third healthy soldier normally
	// stays in the firing line instead of joining a mass rush. This counts only
	// moves that materially close distance and only recent/current movement.
	if (iAdvanceDist + 2 < iCurrentDist &&
		!pSoldier->aiData.bUnderFire &&
		AIPersonalRisk(pSoldier) <= AIPersonalRiskTolerance(pSoldier))
	{
		UINT8 ubActiveMovers = 0;
		UINT8 ubSmallTeamReady = AICombatTeamOperationalCount(pSoldier);
		BOOLEAN fSmallTeam = ubSmallTeamReady >= 2 && ubSmallTeamReady <= 5;
		UINT8 ubMoverLimit = fSmallTeam ? (ubSmallTeamReady >= 4 ? 2 : 1) :
			(fComplexDoctrine ? 2 : 1);

		if (pSoldier->bTeam == ENEMY_TEAM && !fSmallTeam)
		{
			// Grandmaster-style bounding: the element size follows the board state,
			// never a random competence roll. Weak/no covering fire means one mover;
			// a protected local advantage can justify a three-man exploitation bound.
			if (ubEffectiveFire == 0 &&
				(!fAdvanceCover || usAdvanceExposure > usCurrentExposure + 40))
			{
				ubMoverLimit = 1;
			}
			else if (ubEffectiveFire >= 2 &&
				fAdvanceCover &&
				AILocalStress(pSoldier) < 20 &&
				AICheckWeOutnumberLocal(pSoldier, sTargetSpot))
			{
				ubMoverLimit = 3;
			}
		}
		else if (fComplexDoctrine && !fSmallTeam)
		{
			INT32 iMoverJitter = AIBoundedElementJitter(pSoldier,
				(UINT32)(sTargetSpot + 101), 6);
			if (iMoverJitter <= -4)
				ubMoverLimit = 1;
			else if (iMoverJitter >= 5 &&
				AILocalStress(pSoldier) < 20 &&
				AICheckWeOutnumberLocal(pSoldier, sTargetSpot))
				ubMoverLimit = 3;
		}

		// Capability-aware bounding: if enough healthier/more mobile nearby soldiers
		// are materially better maneuver candidates, this soldier remains part of the
		// support base instead of advancing merely because his turn happened first.
		INT32 iMyManeuverScore = AIManeuverRoleScore(pSoldier, sTargetSpot);
		UINT8 ubBetterMovers = 0;
		for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
			iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
		{
			SOLDIERTYPE *pCandidate = MercPtrs[iCounter];
			if (!pCandidate || pCandidate == pSoldier ||
				!pCandidate->bActive || !pCandidate->bInSector ||
				!AISameFireteam(pSoldier, pCandidate) ||
				pCandidate->stats.bLife < OKLIFE || pCandidate->bCollapsed || pCandidate->bBreathCollapsed ||
				(pCandidate->usSoldierFlagMask & SOLDIER_POW) ||
				(pCandidate->flags.uiStatusFlags & SOLDIER_COWERING) ||
				AIDisengagementActive(pCandidate) || AIEscapeActive(pCandidate) ||
				pCandidate->pathing.bLevel != pSoldier->pathing.bLevel ||
				pCandidate->bActionPoints <= 0 ||
				PythSpacesAway(pSoldier->sGridNo, pCandidate->sGridNo) > TACTICAL_RANGE / 2)
			{
				continue;
			}

			INT32 sCandidateThreat = AIPrimaryPlanningThreatSpot(pCandidate);
			if (TileIsOutOfBounds(sCandidateThreat) ||
				PythSpacesAway(sCandidateThreat, sTargetSpot) > 3)
			{
				continue;
			}

			// Choosing the best mover by weapon, mobility and stress is an advanced
			// NCO/fireteam behaviour; basic formations simply obey the mover cap.
			if (!fComplexDoctrine)
				continue;

			INT32 iCandidateScore = AIManeuverRoleScore(pCandidate, sTargetSpot);
			if (iCandidateScore > iMyManeuverScore + 4 ||
				(iCandidateScore >= iMyManeuverScore - 4 &&
				 pCandidate->ubID < pSoldier->ubID))
			{
				++ubBetterMovers;
				if (ubBetterMovers >= ubMoverLimit)
					return FALSE;
			}
		}
		for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
			iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
		{
			SOLDIERTYPE *pFriend = MercPtrs[iCounter];
			if (!pFriend ||
				pFriend == pSoldier ||
				!pFriend->bActive || !pFriend->bInSector ||
				!AISameFireteam(pSoldier, pFriend) ||
				pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || pFriend->bBreathCollapsed ||
				(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
				(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
				AIDisengagementActive(pFriend) || AIEscapeActive(pFriend) ||
				pFriend->pathing.bLevel != pSoldier->pathing.bLevel ||
				PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > TACTICAL_RANGE / 2)
			{
				continue;
			}

			INT32 sFriendThreat = AIPrimaryPlanningThreatSpot(pFriend);
			if (TileIsOutOfBounds(sFriendThreat) ||
				PythSpacesAway(sFriendThreat, sTargetSpot) > 3)
			{
				continue;
			}

			BOOLEAN fCurrentAdvance =
				pFriend->aiData.bAction == AI_ACTION_SEEK_OPPONENT ||
				pFriend->aiData.bAction == AI_ACTION_GET_CLOSER ||
				pFriend->aiData.bAction == AI_ACTION_FLANK_LEFT ||
				pFriend->aiData.bAction == AI_ACTION_FLANK_RIGHT;

			BOOLEAN fRecentAdvance =
				pFriend->bActionPoints < pFriend->bInitialActionPoints &&
				(pFriend->aiData.bLastAction == AI_ACTION_SEEK_OPPONENT ||
				 pFriend->aiData.bLastAction == AI_ACTION_GET_CLOSER ||
				 pFriend->aiData.bLastAction == AI_ACTION_FLANK_LEFT ||
				 pFriend->aiData.bLastAction == AI_ACTION_FLANK_RIGHT);

			INT32 sFrom = NOWHERE;
			INT32 sTo = NOWHERE;

			if (fCurrentAdvance &&
				!TileIsOutOfBounds(pFriend->aiData.usActionData) &&
				pFriend->aiData.usActionData != pFriend->sGridNo)
			{
				sFrom = pFriend->sGridNo;
				sTo = pFriend->aiData.usActionData;
			}
			else if (fRecentAdvance &&
				!TileIsOutOfBounds(pFriend->sLastTwoLocations[1]) &&
				pFriend->sLastTwoLocations[1] != pFriend->sGridNo)
			{
				sFrom = pFriend->sLastTwoLocations[1];
				sTo = pFriend->sGridNo;
			}

			if (TileIsOutOfBounds(sFrom) || TileIsOutOfBounds(sTo))
				continue;

			if (PythSpacesAway(sTo, sFriendThreat) + 1 <
				PythSpacesAway(sFrom, sFriendThreat))
			{
				++ubActiveMovers;
				if (ubActiveMovers >= ubMoverLimit)
					return FALSE;
			}
		}
	}

	// Cooperation should not paralyse ordinary movement. Only a move that clearly
	// increases exposure, or abandons cover while closing into the local fight,
	// needs somebody else in a credible covering position.
	BOOLEAN fExposureIncrease = (usAdvanceExposure > usCurrentExposure + 50);
	BOOLEAN fExposedCloseApproach =
		fCurrentCover &&
		!fAdvanceCover &&
		iAdvanceDist + 3 < iCurrentDist &&
		iAdvanceDist < TACTICAL_RANGE / 2;

	if (!fExposureIncrease && !fExposedCloseApproach)
		return TRUE;

	UINT8 ubSupporters = 0;
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend ||
			pFriend == pSoldier ||
			!pFriend->bActive ||
			!pFriend->bInSector ||
			!AISameFireteam(pSoldier, pFriend) ||
			pFriend->stats.bLife < OKLIFE ||
			pFriend->bCollapsed ||
			pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			AIDisengagementActive(pFriend) ||
			AIEscapeActive(pFriend) ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > DAY_VISION_RANGE ||
			!AICheckHasGun(pFriend) ||
			AIGunAmmo(pFriend) == 0)
		{
			continue;
		}

		// The covering soldier must independently know about essentially the same
		// contact. This prevents a hidden-information squad hive mind.
		INT32 sFriendThreat = AIPrimaryPlanningThreatSpot(pFriend);
		if (TileIsOutOfBounds(sFriendThreat) ||
			PythSpacesAway(sFriendThreat, sTargetSpot) > 3)
		{
			continue;
		}

		INT32 iFriendTargetDist = PythSpacesAway(pFriend->sGridNo, sTargetSpot);
		INT32 iFriendGunRange = __max(1, (INT32)AIGunRange(pFriend) / CELL_X_SIZE);
		if (iFriendTargetDist > iFriendGunRange + iFriendGunRange / 4)
			continue;

		// A nominal rifleman is not covering the bound if he has already spent the
		// AP needed to fire. Sequential JA2 turns make this distinction important:
		// only shooters who can still engage this contact count as a current fire base.
		INT16 sMinAttackAP = MinAPsToAttack(pFriend, sTargetSpot, ADDTURNCOST, 0, 1);
		if (sMinAttackAP <= 0 || pFriend->bActionPoints < sMinAttackAP)
			continue;

		if (!LocationToLocationLineOfSightTest(pFriend->sGridNo, pFriend->pathing.bLevel,
			sTargetSpot, bTargetLevel, TRUE, MAX_VISION_RANGE))
		{
			continue;
		}

		++ubSupporters;
		if (ubSupporters >= 2)
			break;
	}

	UINT8 ubSupportValue = (UINT8)__min((INT32)4,
		(INT32)ubSupporters + (INT32)ubEffectiveFire);
	BOOLEAN fSeverelyExposed =
		(usAdvanceExposure > usCurrentExposure + 150) ||
		(!fAdvanceCover && usAdvanceExposure >= 200);

	// Effective suppression can open a bound, but it never erases severe exposure:
	// open-ground moves still need more combined support than covered ones.
	if (fSeverelyExposed)
		return fAdvanceCover ? (ubSupportValue >= 1) : (ubSupportValue >= 2);

	if (ubSupportValue >= 1)
		return TRUE;

	// Unsupported improvisation belongs to experienced/mobile troops. Security and
	// uncommanded line infantry hold or seek another covered route instead.
	if (AICombatTeam(pSoldier) && !fComplexDoctrine)
		return FALSE;

	// A very bold soldier may make a modest unsupported dash, but not while
	// stressed and never into the severe-exposure case above.
	return ((pSoldier->aiData.bAttitude == AGGRESSIVE ||
		pSoldier->aiData.bAttitude == BRAVESOLO) &&
		AILocalStress(pSoldier) < 25);
}

static INT32 AIVisibleTargetNCTHQuality(SOLDIERTYPE *pSoldier, INT32 sTargetSpot)
{
	if (!pSoldier || TileIsOutOfBounds(sTargetSpot) || !AICheckHasGun(pSoldier))
		return -1;

	SOLDIERTYPE *pTarget = NULL;
	for (UINT32 uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercSlots[uiLoop];
		if (!pOpponent || pOpponent == pSoldier ||
			CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			pSoldier->bSide == pOpponent->bSide)
		{
			continue;
		}

		if (PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0 &&
			KnownLocation(pSoldier, pOpponent->ubID) == sTargetSpot)
		{
			pTarget = pOpponent;
			break;
		}
	}

	// Never query live target state from a stale/team-only contact.
	if (!pTarget)
		return -1;

	UINT16 usOldAttackingWeapon = pSoldier->usAttackingWeapon;
	INT8 bOldScopeMode = pSoldier->bScopeMode;
	INT8 bOldWeaponMode = pSoldier->bWeaponMode;
	UINT8 ubOldAttackingHand = pSoldier->ubAttackingHand;

	pSoldier->ubAttackingHand = HANDPOS;
	pSoldier->usAttackingWeapon = pSoldier->inv[HANDPOS].usItem;
	pSoldier->bWeaponMode = WM_NORMAL;

	std::map<INT8, OBJECTTYPE*> ObjList;
	GetScopeLists(pSoldier, &pSoldier->inv[HANDPOS], ObjList);

	INT32 iBestQuality = -1;
	UINT8 ubDirection = AIDirection(pSoldier->sGridNo, sTargetSpot);
	INT8 bFirstScopeMode =
		(gGameExternalOptions.ubAllowAlternativeWeaponHolding == 3 ?
		 USE_ALT_WEAPON_HOLD : USE_BEST_SCOPE);
	INT8 bLastScopeMode =
		(gGameExternalOptions.fScopeModes ? NUM_SCOPE_MODES - 1 : USE_BEST_SCOPE);

	// Movement evaluation must search the same sight choices as attack selection.
	// Otherwise a rifleman can move because USE_BEST_SCOPE is poor at close range
	// even though irons/another optic would produce a perfectly viable shot.
	for (INT8 bScopeMode = bFirstScopeMode; bScopeMode <= bLastScopeMode; ++bScopeMode)
	{
		if (bScopeMode == USE_ALT_WEAPON_HOLD)
		{
			if (Item[pSoldier->usAttackingWeapon].usItemClass & IC_THROWING_KNIFE)
				continue;

			// Match CalcBestShot()'s current eligibility rule exactly.
			if (IS_MERC_BODY_TYPE(pSoldier))
				continue;
		}
		else if (bScopeMode < USE_BEST_SCOPE || ObjList[bScopeMode] == NULL)
		{
			continue;
		}

		pSoldier->bScopeMode = bScopeMode;

		INT16 sMinAttackAP = MinAPsToAttack(pSoldier, sTargetSpot, ADDTURNCOST, 0, TRUE);
		if (sMinAttackAP <= 0 || sMinAttackAP > pSoldier->bActionPoints)
			continue;

		INT8 bAimLevels = CalcAimingLevelsAvailableWithAP(
			pSoldier, sTargetSpot,
			(INT8)__max(0, pSoldier->bActionPoints - sMinAttackAP));

		if (pSoldier->InternalIsValidStance(ubDirection, ANIM_STAND) &&
			(bScopeMode == USE_ALT_WEAPON_HOLD ||
			 !Weapon[pSoldier->usAttackingWeapon].HeavyGun ||
			 !Item[pSoldier->usAttackingWeapon].twohanded ||
			 !gGameExternalOptions.ubAllowAlternativeWeaponHolding))
		{
			iBestQuality = __max(iBestQuality, (INT32)AICalcChanceToHitGun(
				pSoldier, sTargetSpot, bAimLevels, AIM_SHOT_TORSO,
				pTarget->pathing.bLevel, STANDING));
		}

		// CalcBestShot() does not evaluate crouch/prone while using alternate
		// weapon holding, so keep the movement model identical.
		if (bScopeMode != USE_ALT_WEAPON_HOLD)
		{
			if (pSoldier->InternalIsValidStance(ubDirection, ANIM_CROUCH))
			{
				iBestQuality = __max(iBestQuality, (INT32)AICalcChanceToHitGun(
					pSoldier, sTargetSpot, bAimLevels, AIM_SHOT_TORSO,
					pTarget->pathing.bLevel, CROUCHING));
			}

			if (pSoldier->InternalIsValidStance(ubDirection, ANIM_PRONE))
			{
				iBestQuality = __max(iBestQuality, (INT32)AICalcChanceToHitGun(
					pSoldier, sTargetSpot, bAimLevels, AIM_SHOT_TORSO,
					pTarget->pathing.bLevel, PRONE));
			}
		}
	}

	pSoldier->usAttackingWeapon = usOldAttackingWeapon;
	pSoldier->bScopeMode = bOldScopeMode;
	pSoldier->bWeaponMode = bOldWeaponMode;
	pSoldier->ubAttackingHand = ubOldAttackingHand;

	return iBestQuality;
}

// Range-aware movement preference. Positive values mean closing distance is useful;
// negative values mean a scoped/long-range soldier is too close. Nominal weapon
// range is only an outer limit: NCTH practical hit quality defines the useful band.
INT8 AIEngagementRangeModifier(SOLDIERTYPE *pSoldier, INT32 sTargetSpot)
{
	if (!pSoldier || !AICheckHasGun(pSoldier))
		return 0;

	if (TileIsOutOfBounds(sTargetSpot))
		sTargetSpot = AIPrimaryPlanningThreatSpot(pSoldier);

	if (TileIsOutOfBounds(sTargetSpot))
		return 0;

	INT32 iDistance = PythSpacesAway(pSoldier->sGridNo, sTargetSpot);
	INT32 iGunRange = __max(1, (INT32)AIGunRange(pSoldier) / CELL_X_SIZE);
	FLOAT dScope = AIGunScopeMagFactor(pSoldier);
	INT32 iPreferredMinRange = 0;

	if (AICheckIsSniper(pSoldier))
		iPreferredMinRange = __max(12, iGunRange / 3);
	else if (AICheckIsMarksman(pSoldier))
		iPreferredMinRange = __max(10, iGunRange / 4);
	else if (dScope >= 4.0f)
		iPreferredMinRange = __max(8, (INT32)(dScope * 2.0f));
	else if (dScope >= 2.0f)
		iPreferredMinRange = 6;

	if (iPreferredMinRange > 0)
		iPreferredMinRange = __min(iPreferredMinRange, __max(6, iGunRange / 2));

	if (iPreferredMinRange > 0)
	{
		if (iDistance < __max(4, iPreferredMinRange / 2))
			return -3;
		if (iDistance < iPreferredMinRange)
			return -2;
	}

	// For a personally visible target, use the same NCTH estimator as attack logic.
	// A shot can therefore be 'inside range' yet still tell the soldier to close.
	INT32 iNCTHQuality = UsingNewCTHSystem() ?
		AIVisibleTargetNCTHQuality(pSoldier, sTargetSpot) : -1;
	if (iNCTHQuality >= 0)
	{
		if (iNCTHQuality < 10 && iDistance > __max(5, iPreferredMinRange))
			return 2;
		if (iNCTHQuality < 22 && iDistance > __max(5, iPreferredMinRange))
			return 1;
		if (iNCTHQuality >= 35)
			return 0;
	}

	// For stale/team-only contacts, do not inspect hidden target state. Estimate a
	// practical band from shooter skill, optics and nominal range instead.
	INT32 iPracticalPercent = 65 + __max(0, __min(20,
		((INT32)EffectiveMarksmanship(pSoldier) - 50) / 2));
	if (dScope >= 4.0f) iPracticalPercent += 10;
	else if (dScope >= 2.0f) iPracticalPercent += 5;
	if (AICheckIsSniper(pSoldier)) iPracticalPercent += 5;
	iPracticalPercent = __max(60, __min(95, iPracticalPercent));
	INT32 iPracticalRange = __max(5, iGunRange * iPracticalPercent / 100);

	if (iDistance > iGunRange + iGunRange / 4)
		return 2;
	if (iDistance > iPracticalRange + __max(2, iPracticalRange / 5))
		return 2;
	if (iDistance > iPracticalRange)
		return 1;

	return 0;
}

// Count nearby teammates who have just been engaging the same target area.
// This gives sequential JA2 AI a lightweight target reservation system without
// persistent squad state or hidden information.
UINT8 AITargetSaturation(SOLDIERTYPE *pSoldier, INT32 sTargetSpot)
{
	if (!pSoldier || TileIsOutOfBounds(sTargetSpot))
		return 0;

	UINT8 ubSaturation = 0;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; iCounter++)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier || !pFriend->bActive || !pFriend->bInSector ||
			!AISameFireteam(pSoldier, pFriend) ||
			pFriend->stats.bLife < OKLIFE ||
			pFriend->bCollapsed ||
			pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			AIDisengagementActive(pFriend) || AIEscapeActive(pFriend))
		{
			continue;
		}

		// Restrict coordination to the local fight. Distant teammates do not create
		// a sector-wide hive-mind reservation.
		if (PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > DAY_VISION_RANGE)
			continue;

		// sLastTarget records where this teammate actually aimed. Requiring a recent
		// fire action prevents stale target locations from reserving someone forever.
		if (!TileIsOutOfBounds(pFriend->sLastTarget) &&
			PythSpacesAway(pFriend->sLastTarget, sTargetSpot) <= 1 &&
			pFriend->bActionPoints < pFriend->bInitialActionPoints &&
			(pFriend->aiData.bAction == AI_ACTION_FIRE_GUN ||
			 pFriend->aiData.bLastAction == AI_ACTION_FIRE_GUN))
		{
			// A missed volley does not reserve the target. Spread fire only after this
			// teammate actually achieved an effect this turn (hit) or the currently
			// observed target is already collapsed/cowering from the engagement.
			BOOLEAN fEffectiveFire =
				pFriend->LastAttackHit() ||
				pFriend->LastTargetCollapsed() ||
				pFriend->LastTargetSuppressed();
			if (fEffectiveFire)
				ubSaturation++;
		}
	}

	return __min((UINT8)3, ubSaturation);
}

// Count fireteam members whose recent fire actually affected the same target
// area. This is the missing mover-side half of fire-and-manoeuvre: a hit,
// suppression or collapse creates a short tactical movement window for nearby
// teammates without revealing any information they do not otherwise possess.
UINT8 AIFireteamEffectiveFireSupport(SOLDIERTYPE *pSoldier, INT32 sTargetSpot)
{
	if (!pSoldier || !AICombatTeam(pSoldier))
		return 0;

	if (TileIsOutOfBounds(sTargetSpot))
		sTargetSpot = AIPrimaryPlanningThreatSpot(pSoldier);
	if (TileIsOutOfBounds(sTargetSpot))
		return 0;

	UINT8 ubSupport = 0;
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier || !pFriend->bActive || !pFriend->bInSector ||
			!AISameFireteam(pSoldier, pFriend) ||
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			AIDisengagementActive(pFriend) || AIEscapeActive(pFriend) ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > __max(8, DAY_VISION_RANGE))
		{
			continue;
		}

		BOOLEAN fRecentFire =
			pFriend->bActionPoints < pFriend->bInitialActionPoints &&
			(pFriend->aiData.bAction == AI_ACTION_FIRE_GUN ||
			 pFriend->aiData.bLastAction == AI_ACTION_FIRE_GUN);
		if (!fRecentFire || TileIsOutOfBounds(pFriend->sLastTarget) ||
			PythSpacesAway(pFriend->sLastTarget, sTargetSpot) > 3)
		{
			continue;
		}

		BOOLEAN fSuppressed = pFriend->LastTargetSuppressed();
		BOOLEAN fEffective = fSuppressed || pFriend->LastAttackHit() ||
			pFriend->LastTargetCollapsed();
		if (!fEffective)
			continue;

		// Suppression is the strongest movement-enabling result; ordinary hits still
		// matter, but several weak hits cannot create an unlimited bravery bonus.
		ubSupport = (UINT8)__min((INT32)3,
			(INT32)ubSupport + (fSuppressed ? 2 : 1));
		if (ubSupport >= 3)
			break;
	}

	return ubSupport;
}

// Basic fire-and-manoeuvre is ordinary unit behaviour, not an elite trick. The
// sophisticated parts (deep flank, exposed improvisation, breach doctrine) remain
// competence-gated; this helper only authorizes a local covered manoeuvre when the
// fireteam has a credible reason to act together.
BOOLEAN AIBasicFireteamManeuverReady(SOLDIERTYPE *pSoldier, INT32 sTargetSpot)
{
	if (!pSoldier || !AICombatTeam(pSoldier) ||
		AIDisengagementActive(pSoldier) || AIEscapeActive(pSoldier) ||
		AIFireteamCombatReadyCount(pSoldier) < 3)
	{
		return FALSE;
	}

	if (TileIsOutOfBounds(sTargetSpot))
		sTargetSpot = AIPrimaryPlanningThreatSpot(pSoldier);
	if (TileIsOutOfBounds(sTargetSpot))
		return FALSE;

	if (AILocalStress(pSoldier) >= 60 ||
		AIPersonalRisk(pSoldier) > AIPersonalRiskTolerance(pSoldier) + 10)
	{
		return FALSE;
	}

	AITACTICALGEOMETRY Geometry;
	if (!AIBuildTacticalGeometry(pSoldier, pSoldier->sGridNo, &Geometry) ||
		__max((INT32)Geometry.sLeftFlankOpportunity,
			(INT32)Geometry.sRightFlankOpportunity) < 5)
	{
		return FALSE;
	}

	UINT8 ubEffectiveFire = AIFireteamEffectiveFireSupport(pSoldier, sTargetSpot);
	INT32 iApproachPressure = AISharedApproachPressure(pSoldier, sTargetSpot);
	BOOLEAN fCommandSupport = AIHasLocalCommandSupport(pSoldier);

	// Top-tier fireteams do not need a doctrine permission flag to understand
	// fire-and-manoeuvre. Small elements still need a genuine enabling cue; larger
	// elements can organically establish a base of fire and a manoeuvre element.
	return ubEffectiveFire > 0 || iApproachPressure >= 20 || fCommandSupport ||
		AIFireteamCombatReadyCount(pSoldier) >= 4;
}

// Check whether this target is directly threatening a nearby ally who needs
// covering fire.  This uses only observed combat relationships (recent attackers
// and actual fire lanes), so it does not grant the AI hidden information.
BOOLEAN AIFriendNeedsCoveringFire(SOLDIERTYPE *pSoldier, UINT8 ubOpponentID)
{
	if (!pSoldier || ubOpponentID == NOBODY || !MercPtrs[ubOpponentID] ||
		AIDisengagementActive(pSoldier) || AIEscapeActive(pSoldier))
		return FALSE;
for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; iCounter++)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];

		if (!pFriend || pFriend == pSoldier || !pFriend->bActive || !pFriend->bInSector ||
			!AISameFireteam(pSoldier, pFriend) ||
			pFriend->stats.bLife < OKLIFE ||
			pFriend->bCollapsed ||
			pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > DAY_VISION_RANGE)
		{
			continue;
		}

		BOOLEAN fFriendInTrouble =
			pFriend->aiData.bUnderFire ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			ShockLevelPercent(pFriend) > 30 ||
			pFriend->stats.bLife < pFriend->stats.bLifeMax / 2 ||
			AIPersonalRisk(pFriend) > AIPersonalRiskTolerance(pFriend);

		if (!fFriendInTrouble)
			continue;

		// Require evidence that this particular opponent is the threat to the ally.
		if (pFriend->ubPreviousAttackerID == ubOpponentID ||
			pFriend->ubNextToPreviousAttackerID == ubOpponentID)
		{
			return TRUE;
		}
	}

	return FALSE;
}

static BOOLEAN AIRecentWithdrawal(SOLDIERTYPE *pFriend)
{
	if (!pFriend)
		return FALSE;

	BOOLEAN fCurrentWithdrawal =
		pFriend->aiData.bAction == AI_ACTION_WITHDRAW ||
		pFriend->aiData.bAction == AI_ACTION_RUN_AWAY;

	BOOLEAN fRecentWithdrawal =
		pFriend->bActionPoints < pFriend->bInitialActionPoints &&
		(pFriend->aiData.bLastAction == AI_ACTION_WITHDRAW ||
		 pFriend->aiData.bLastAction == AI_ACTION_RUN_AWAY);

	return fCurrentWithdrawal || fRecentWithdrawal;
}

static BOOLEAN AIEligibleWithdrawalCoverer(SOLDIERTYPE *pCandidate, SOLDIERTYPE *pRetreating)
{
	if (!pCandidate || !pRetreating ||
		pCandidate == pRetreating ||
		!AISameFireteam(pCandidate, pRetreating) ||
		!pCandidate->bActive || !pCandidate->bInSector ||
		pCandidate->stats.bLife < OKLIFE || pCandidate->bCollapsed || pCandidate->bBreathCollapsed ||
		(pCandidate->usSoldierFlagMask & SOLDIER_POW) ||
		(pCandidate->flags.uiStatusFlags & SOLDIER_COWERING) ||
		pCandidate->pathing.bLevel != pRetreating->pathing.bLevel ||
		PythSpacesAway(pCandidate->sGridNo, pRetreating->sGridNo) > DAY_VISION_RANGE ||
		pCandidate->bActionPoints != pCandidate->bInitialActionPoints ||
		AIDisengagementActive(pCandidate) ||
		AIEscapeActive(pCandidate) ||
		!AICheckHasGun(pCandidate) || AIGunAmmo(pCandidate) == 0)
	{
		return FALSE;
	}

	INT32 sThreat = AIPrimaryPlanningThreatSpot(pCandidate);
	if (TileIsOutOfBounds(sThreat))
		return FALSE;

	INT32 iRisk = AIPersonalRisk(pCandidate);
	INT32 iTolerance = AIPersonalRiskTolerance(pCandidate);

	// Nobody is ordered to play rear guard while his own position is already
	// becoming untenable.
	if (iRisk > iTolerance + 10 ||
		(pCandidate->aiData.bUnderFire && iRisk >= iTolerance))
	{
		return FALSE;
	}

	if (!AnyCoverAtSpot(pCandidate, pCandidate->sGridNo) &&
		(AILocalStress(pCandidate) >= 25 || iRisk + 10 >= iTolerance))
	{
		return FALSE;
	}

	return TRUE;
}

static INT32 AIBoundedDecisionJitter(SOLDIERTYPE *pSoldier, UINT32 uiSalt, INT32 iAmplitude)
{
	if (!pSoldier || iAmplitude <= 0)
		return 0;

	UINT32 uiValue = pSoldier->uiUniqueSoldierIdValue;
	uiValue ^= (guiTurnCnt + 1) * 2654435761u;
	uiValue ^= uiSalt * 2246822519u;
	uiValue ^= uiValue >> 13;
	uiValue *= 3266489917u;
	uiValue ^= uiValue >> 16;

	UINT32 uiSpan = (UINT32)(2 * iAmplitude + 1);
	return (INT32)(uiValue % uiSpan) - iAmplitude;
}

static INT32 AIBoundedElementJitter(SOLDIERTYPE *pSoldier, UINT32 uiSalt, INT32 iAmplitude)
{
	if (!pSoldier || iAmplitude <= 0)
		return 0;

	// Bound size is an element decision. Use the fireteam identity (or the team for
	// non-enemy combatants) so every soldier evaluating the same contact this turn
	// receives the same one/two/three-mover limit.
	UINT32 uiElement = (UINT32)(pSoldier->bTeam + 1);
	if (AICombatTeam(pSoldier))
	{
		UINT8 ubFireteam = AIFireteamId(pSoldier);
		if (ubFireteam != AI_FIRETEAM_NONE)
			uiElement = ((UINT32)(pSoldier->bTeam + 1) << 8) | ubFireteam;
	}

	UINT32 uiValue = uiElement * 2654435761u;
	uiValue ^= (guiTurnCnt + 1) * 2246822519u;
	uiValue ^= uiSalt * 3266489917u;
	uiValue ^= uiValue >> 13;
	uiValue *= 668265263u;
	uiValue ^= uiValue >> 16;

	UINT32 uiSpan = (UINT32)(2 * iAmplitude + 1);
	return (INT32)(uiValue % uiSpan) - iAmplitude;
}

static INT32 AIWithdrawalCoverScore(SOLDIERTYPE *pCandidate, SOLDIERTYPE *pRetreating)
{
	if (!AIEligibleWithdrawalCoverer(pCandidate, pRetreating))
		return -10000;

	INT32 iScore = 0;
	if (AnyCoverAtSpot(pCandidate, pCandidate->sGridNo))
		iScore += 30;
	if (SightCoverAtSpot(pCandidate, pCandidate->sGridNo, FALSE))
		iScore += 15;
	if (AICheckIsMachinegunner(pCandidate))
		iScore += 15;

	iScore += __max(0, 20 - AILocalStress(pCandidate) / 3);
	iScore += __max(0, 20 - AIPersonalRisk(pCandidate) / 4);
	iScore -= __min((INT32)20,
		PythSpacesAway(pCandidate->sGridNo, pRetreating->sGridNo));

	// Enemy rear guards are chosen by tactical merit, not by an artificial mistake
	// roll. Non-enemy AI keeps bounded variation among otherwise close candidates.
	if (pCandidate->bTeam != ENEMY_TEAM)
	{
		iScore += AIBoundedDecisionJitter(pCandidate,
			pRetreating->uiUniqueSoldierIdValue + 17u, 6);
	}

	return iScore;
}

BOOLEAN AIShouldHoldForWithdrawingFriend(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier) ||
		!pSoldier->bActive || !pSoldier->bInSector ||
		pSoldier->stats.bLife < OKLIFE ||
		pSoldier->bCollapsed ||
		pSoldier->bBreathCollapsed ||
		(pSoldier->usSoldierFlagMask & SOLDIER_POW) ||
		(pSoldier->flags.uiStatusFlags & SOLDIER_COWERING) ||
		AIDisengagementActive(pSoldier) ||
		AIEscapeActive(pSoldier) ||
		AILastSurvivorPressure(pSoldier))
	{
		return FALSE;
	}

	// Pick one deterministic local withdrawal to organize around. This prevents
	// an entire squad from becoming "coverers" for several buddies at once.
	SOLDIERTYPE *pRetreating = NULL;
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier ||
			!pFriend->bActive || !pFriend->bInSector ||
			!AISameFireteam(pSoldier, pFriend) ||
			pFriend->stats.bLife < OKLIFE ||
			pFriend->bCollapsed ||
			pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			pFriend->pathing.bLevel != pSoldier->pathing.bLevel ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > DAY_VISION_RANGE ||
			!AIRecentWithdrawal(pFriend))
		{
			continue;
		}

		BOOLEAN fBreakingContact =
			AIDisengagementActive(pFriend) ||
			AIEscapeActive(pFriend) ||
			pFriend->aiData.bUnderFire ||
			AIPersonalRisk(pFriend) > AIPersonalRiskTolerance(pFriend);

		if (!fBreakingContact)
			continue;

		if (!pRetreating || pFriend->ubID < pRetreating->ubID)
			pRetreating = pFriend;
	}

	if (!pRetreating || !AIEligibleWithdrawalCoverer(pSoldier, pRetreating))
		return FALSE;

	INT32 iMyScore = AIWithdrawalCoverScore(pSoldier, pRetreating);
	UINT8 ubBestID = pSoldier->ubID;
	INT32 iBestScore = iMyScore;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pCandidate = MercPtrs[iCounter];
		INT32 iScore = AIWithdrawalCoverScore(pCandidate, pRetreating);
		if (iScore > iBestScore ||
			(iScore == iBestScore && pCandidate && pCandidate->ubID < ubBestID))
		{
			iBestScore = iScore;
			ubBestID = pCandidate->ubID;
		}
	}

	return (ubBestID == pSoldier->ubID);
}

BOOLEAN AIFriendWithdrawingNeedsCover(SOLDIERTYPE *pSoldier, UINT8 ubOpponentID)
{
	if (!AIShouldHoldForWithdrawingFriend(pSoldier) ||
		ubOpponentID == NOBODY || !MercPtrs[ubOpponentID])
	{
		return FALSE;
	}

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier ||
			!pFriend->bActive || !pFriend->bInSector ||
			!AISameFireteam(pSoldier, pFriend) ||
			pFriend->stats.bLife < OKLIFE ||
			pFriend->bCollapsed ||
			pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			pFriend->pathing.bLevel != pSoldier->pathing.bLevel ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > DAY_VISION_RANGE ||
			!AIRecentWithdrawal(pFriend))
		{
			continue;
		}

		INT8 bKnowledge = PersonalKnowledge(pFriend, ubOpponentID);
		INT32 sThreat = NOWHERE;
		if (bKnowledge == SEEN_CURRENTLY ||
			bKnowledge == SEEN_THIS_TURN ||
			bKnowledge == SEEN_LAST_TURN ||
			bKnowledge == HEARD_THIS_TURN)
		{
			sThreat = KnownPersonalLocation(pFriend, ubOpponentID);
		}
		else if (pFriend->bTeam == ENEMY_TEAM)
		{
			// The withdrawing soldier may act on the fireteam's shared contact without
			// learning an opponent identity. The covering shooter still needs its own
			// legal knowledge of ubOpponentID before this helper can result in fire.
			INT32 sSharedThreat = NOWHERE;
			UINT8 ubSharedConfidence = 0;
			INT32 sShooterThreat = KnownLocation(pSoldier, ubOpponentID);
			if (AISharedFireteamContact(pFriend, &sSharedThreat, NULL, &ubSharedConfidence) &&
				ubSharedConfidence >= 50 &&
				!TileIsOutOfBounds(sShooterThreat) &&
				PythSpacesAway(sSharedThreat, sShooterThreat) <= 3)
			{
				sThreat = sSharedThreat;
			}
		}
		if (TileIsOutOfBounds(sThreat))
			continue;

		INT32 sFrom = NOWHERE;
		INT32 sTo = NOWHERE;
		if ((pFriend->aiData.bAction == AI_ACTION_WITHDRAW ||
			 pFriend->aiData.bAction == AI_ACTION_RUN_AWAY) &&
			!TileIsOutOfBounds(pFriend->aiData.usActionData))
		{
			sFrom = pFriend->sGridNo;
			sTo = pFriend->aiData.usActionData;
		}
		else if (!TileIsOutOfBounds(pFriend->sLastTwoLocations[1]))
		{
			sFrom = pFriend->sLastTwoLocations[1];
			sTo = pFriend->sGridNo;
		}

		if (!TileIsOutOfBounds(sFrom) && !TileIsOutOfBounds(sTo) &&
			PythSpacesAway(sTo, sThreat) > PythSpacesAway(sFrom, sThreat) + 1)
		{
			return TRUE;
		}
	}

	return FALSE;
}

BOOLEAN AIFriendAdvancingNeedsCover(SOLDIERTYPE *pSoldier, UINT8 ubOpponentID)
{
	if (!AICombatTeam(pSoldier) ||
		ubOpponentID == NOBODY ||
		!MercPtrs[ubOpponentID] ||
		AIDisengagementActive(pSoldier) ||
		AIEscapeActive(pSoldier))
	{
		return FALSE;
	}

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend ||
			pFriend == pSoldier ||
			!pFriend->bActive ||
			!pFriend->bInSector ||
			!AISameFireteam(pSoldier, pFriend) ||
			pFriend->stats.bLife < OKLIFE ||
			pFriend->bCollapsed ||
			pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			AIDisengagementActive(pFriend) ||
			AIEscapeActive(pFriend) ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > DAY_VISION_RANGE)
		{
			continue;
		}

		// The mover can use a local shared contact for coordination, but not for attack
		// authorization. The covering shooter still owns the exact opponent identity.
		INT8 bKnowledge = PersonalKnowledge(pFriend, ubOpponentID);
		INT32 sKnownThreat = NOWHERE;
		if (bKnowledge == SEEN_CURRENTLY ||
			bKnowledge == SEEN_THIS_TURN ||
			bKnowledge == SEEN_LAST_TURN ||
			bKnowledge == HEARD_THIS_TURN)
		{
			sKnownThreat = KnownPersonalLocation(pFriend, ubOpponentID);
		}
		else if (pFriend->bTeam == ENEMY_TEAM)
		{
			INT32 sSharedThreat = NOWHERE;
			UINT8 ubSharedConfidence = 0;
			INT32 sShooterThreat = KnownLocation(pSoldier, ubOpponentID);
			if (AISharedFireteamContact(pFriend, &sSharedThreat, NULL, &ubSharedConfidence) &&
				ubSharedConfidence >= 50 &&
				!TileIsOutOfBounds(sShooterThreat) &&
				PythSpacesAway(sSharedThreat, sShooterThreat) <= 3)
			{
				sKnownThreat = sSharedThreat;
			}
		}
		if (TileIsOutOfBounds(sKnownThreat))
			continue;

		BOOLEAN fCurrentAdvance =
			pFriend->aiData.bAction == AI_ACTION_SEEK_OPPONENT ||
			pFriend->aiData.bAction == AI_ACTION_GET_CLOSER ||
			pFriend->aiData.bAction == AI_ACTION_FLANK_LEFT ||
			pFriend->aiData.bAction == AI_ACTION_FLANK_RIGHT;

		BOOLEAN fRecentAdvance =
			pFriend->bActionPoints < pFriend->bInitialActionPoints &&
			(pFriend->aiData.bLastAction == AI_ACTION_SEEK_OPPONENT ||
			 pFriend->aiData.bLastAction == AI_ACTION_GET_CLOSER ||
			 pFriend->aiData.bLastAction == AI_ACTION_FLANK_LEFT ||
			 pFriend->aiData.bLastAction == AI_ACTION_FLANK_RIGHT);

		INT32 sFrom = NOWHERE;
		INT32 sTo = NOWHERE;

		if (fCurrentAdvance &&
			!TileIsOutOfBounds(pFriend->aiData.usActionData) &&
			pFriend->aiData.usActionData != pFriend->sGridNo)
		{
			sFrom = pFriend->sGridNo;
			sTo = pFriend->aiData.usActionData;
		}
		else if (fRecentAdvance &&
			!TileIsOutOfBounds(pFriend->sLastTwoLocations[1]) &&
			pFriend->sLastTwoLocations[1] != pFriend->sGridNo)
		{
			// JA2 retains the last movement locations until this soldier receives
			// control again, which lets later teammates in the sequential turn
			// recognise a bound that just finished.
			sFrom = pFriend->sLastTwoLocations[1];
			sTo = pFriend->sGridNo;
		}

		if (TileIsOutOfBounds(sFrom) || TileIsOutOfBounds(sTo))
			continue;

		INT32 iFromDist = PythSpacesAway(sFrom, sKnownThreat);
		INT32 iToDist = PythSpacesAway(sTo, sKnownThreat);
		if (iToDist + 1 >= iFromDist)
			continue;

		UINT16 usFromExposure = AIKnownThreatExposure(pFriend, sFrom, pFriend->pathing.bLevel);
		UINT16 usToExposure = AIKnownThreatExposure(pFriend, sTo, pFriend->pathing.bLevel);
		BOOLEAN fFlanking =
			pFriend->aiData.bAction == AI_ACTION_FLANK_LEFT ||
			pFriend->aiData.bAction == AI_ACTION_FLANK_RIGHT ||
			pFriend->aiData.bLastAction == AI_ACTION_FLANK_LEFT ||
			pFriend->aiData.bLastAction == AI_ACTION_FLANK_RIGHT;

		BOOLEAN fNeedsCover =
			usToExposure > usFromExposure + 25 ||
			(!AnyCoverAtSpot(pFriend, sTo) && iToDist < TACTICAL_RANGE / 2) ||
			(fFlanking && iToDist < TACTICAL_RANGE);

		if (fNeedsCover)
			return TRUE;
	}

	return FALSE;
}

// sevenfm: count nearby friend soldiers
UINT8 CountNearbyFriends( SOLDIERTYPE *pSoldier, INT32 sGridNo, UINT8 ubDistance )
{
	CHECKF(pSoldier);

	SOLDIERTYPE * pFriend;
	UINT8 ubFriendCount = 0;

	// Run through each friendly.
	for ( UINT8 iCounter = gTacticalStatus.Team[ pSoldier->bTeam ].bFirstID ; iCounter <= gTacticalStatus.Team[ pSoldier->bTeam ].bLastID ; iCounter ++ )
	{
		pFriend = MercPtrs[ iCounter ];
		// Make sure that character is alive, not too shocked, and conscious, and of higher experience level
		// than the character being suppressed.
		if (pFriend != pSoldier &&
			pFriend->bActive &&
			pFriend->bInSector &&
			pFriend->stats.bLife >= OKLIFE &&
			!pFriend->bCollapsed &&
			!pFriend->bBreathCollapsed &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW) &&
			PythSpacesAway( sGridNo, pFriend->sGridNo ) <= ubDistance )
		{
			ubFriendCount++;
		}
	}

	return ubFriendCount;
}

// count neutral civilians
UINT8 CountNearbyNeutrals(SOLDIERTYPE *pSoldier, INT32 sGridNo, INT16 sDistance)
{
	SOLDIERTYPE * pFriend;
	UINT8 ubFriendCount = 0;

	// safety check
	if (!pSoldier)
		return 0;

	for (UINT32 uiLoop = 0; uiLoop < guiNumMercSlots; uiLoop++)
	{
		pFriend = MercSlots[uiLoop];

		if (pFriend &&
			pFriend != pSoldier &&
			pFriend->bActive &&
			pFriend->stats.bLife >= OKLIFE &&
			CONSIDERED_NEUTRAL(pSoldier, pFriend) &&
			PythSpacesAway(sGridNo, pFriend->sGridNo) <= sDistance)
		{
			ubFriendCount++;
		}
	}

	return ubFriendCount;
}

UINT8 CountFriendsNotAlerted(SOLDIERTYPE *pSoldier)
{
	SOLDIERTYPE * pFriend;
	UINT8 ubFriendCount = 0;

	// Run through each friendly.
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID; iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; iCounter++)
	{
		pFriend = MercPtrs[iCounter];
		
		if (pFriend && 
			pFriend->bActive &&
			pFriend->stats.bLife >= OKLIFE &&
			(pSoldier->bTeam != CIV_TEAM || pSoldier->ubCivilianGroup != NON_CIV_GROUP && pFriend->ubCivilianGroup == pSoldier->ubCivilianGroup) &&
			pFriend->aiData.bAlertStatus < STATUS_RED)
		{
			ubFriendCount++;
		}
	}

	return ubFriendCount;
}

void AlertFriends(INT8 bTeam, UINT8 ubCivGroup)
{
	SOLDIERTYPE * pFriend;

	// Run through each friendly.
	for (UINT8 iCounter = gTacticalStatus.Team[bTeam].bFirstID; iCounter <= gTacticalStatus.Team[bTeam].bLastID; iCounter++)
	{
		pFriend = MercPtrs[iCounter];

		if (pFriend && 
			pFriend->bActive &&
			pFriend->stats.bLife >= OKLIFE &&
			(bTeam != CIV_TEAM || ubCivGroup != NON_CIV_GROUP && pFriend->ubCivilianGroup == ubCivGroup))
		{
			pFriend->aiData.bAlertStatus = max(pFriend->aiData.bAlertStatus, STATUS_RED);
		}
	}
}

UINT8 CountFriendsFlankSameSpot(SOLDIERTYPE *pSoldier, INT32 sSpot)
{
	CHECKF(pSoldier);

	SOLDIERTYPE * pFriend;
	UINT8 ubFriendCount = 0;
	UINT8 ubMaxDist = TACTICAL_RANGE / 2;
	UINT8 ubFlankLeft = 0;
	UINT8 ubFlankRight = 0;

	if (TileIsOutOfBounds(sSpot))
	{
		sSpot = AIPrimaryPlanningThreatSpot(pSoldier);
	}

	if (TileIsOutOfBounds(sSpot))
	{
		return 0;
	}

	// Run through each friendly.
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID; iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; iCounter++)
	{
		pFriend = MercPtrs[iCounter];

		if (pFriend &&
			pFriend != pSoldier &&
			pFriend->bActive &&
			pFriend->bInSector &&
			AISameFireteam(pSoldier, pFriend) &&
			pFriend->stats.bLife >= OKLIFE &&
			!pFriend->bCollapsed &&
			!pFriend->bBreathCollapsed &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW) &&
			!(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) &&
			!AIDisengagementActive(pFriend) && !AIEscapeActive(pFriend) &&
			pFriend->aiData.bAlertStatus == STATUS_RED &&
			pFriend->aiData.bOrders > ONGUARD)
		{
			// check if this friend flanks around the same spot
			if (pFriend->IsFlanking() &&
				!TileIsOutOfBounds(pFriend->lastFlankSpot) &&
				PythSpacesAway(pFriend->lastFlankSpot, sSpot) < ubMaxDist)
			{
				if (pFriend->flags.lastFlankLeft)
				{
					ubFlankLeft++;
				}
				else
				{
					ubFlankRight++;
				}
			}
		}
	}

	return ubFlankLeft + ubFlankRight;
}

UINT8 CountNearbyFriendsLastAttackHit( SOLDIERTYPE *pSoldier, INT32 sGridNo, UINT8 ubDistance )
{
	CHECKF(pSoldier);

	SOLDIERTYPE * pFriend;
	UINT8 ubFriendCount = 0;

	// Run through each friendly.
	for ( UINT8 iCounter = gTacticalStatus.Team[ pSoldier->bTeam ].bFirstID ; iCounter <= gTacticalStatus.Team[ pSoldier->bTeam ].bLastID ; iCounter ++ )
	{
		pFriend = MercPtrs[ iCounter ];

		if (pFriend != pSoldier &&
			pFriend->bActive &&
			pFriend->bInSector &&
			pFriend->stats.bLife >= OKLIFE &&
			!pFriend->bCollapsed &&
			!pFriend->bBreathCollapsed &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW) &&
			!(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) &&
			!AIDisengagementActive(pFriend) && !AIEscapeActive(pFriend) &&
			pFriend->aiData.bOrders > ONGUARD &&
			pFriend->aiData.bOrders != SNIPER &&
			PythSpacesAway( sGridNo, pFriend->sGridNo ) <= ubDistance &&
			(pFriend->aiData.bLastAttackHit ||
			 (pFriend->usSoldierFlagMask2 & SOLDIER_SUCCESSFUL_ATTACK)) )
		{
			ubFriendCount++;
		}
	}

	return ubFriendCount;
}

// check if gun that AI can use is scoped
BOOLEAN AIGunScoped(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	INT8 bWeaponIn;
	OBJECTTYPE *pObj;

	if ( TANK( pSoldier ) )
	{
		return FALSE;
	}

	bWeaponIn = FindAIUsableObjClass( pSoldier, IC_GUN );

	if (bWeaponIn == NO_SLOT)
	{
		return FALSE;
	}

	pObj = &pSoldier->inv[bWeaponIn];

	if( UsingNewCTHSystem() )
	{
		return NCTHIsScoped(pObj);
	}
	else
	{
		return IsScoped(pObj);
	}
}

BOOLEAN AIGunInHandScoped(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	if( UsingNewCTHSystem() == false && IsScoped(&pSoldier->inv[HANDPOS]) )
	{
		return TRUE;
	}

	if( UsingNewCTHSystem() == true && NCTHIsScoped(&pSoldier->inv[HANDPOS]) )
	{
		return TRUE;
	}
	return FALSE;
}

// return range for the AI gun
UINT16 AIGunRange(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	INT8 bWeaponIn;
	OBJECTTYPE *pObj;

	if ( TANK( pSoldier ) )
	{
		return 0;
	}

	bWeaponIn = FindAIUsableObjClass( pSoldier, IC_GUN );

	if (bWeaponIn == NO_SLOT)
	{
		return 0;
	}

	pObj = &pSoldier->inv[bWeaponIn];

	return GunRange(pObj, pSoldier);
}

UINT8 CountSeenEnemiesLastTurn( SOLDIERTYPE* pSoldier )
{
	CHECKF(pSoldier);

	UINT8	ubTeamLoop;
	UINT8	ubIDLoop;
	UINT8	cnt = 0;

	for( ubTeamLoop = 0; ubTeamLoop < MAXTEAMS; ubTeamLoop++ )
	{
		if( !gTacticalStatus.Team[ubTeamLoop].bTeamActive )
			continue;

		if( gTacticalStatus.Team[ ubTeamLoop ].bSide != pSoldier->bSide )
		{
			// consider guys in this team, which isn't on our side
			for( ubIDLoop = gTacticalStatus.Team[ ubTeamLoop ].bFirstID; ubIDLoop <= gTacticalStatus.Team[ ubTeamLoop ].bLastID; ubIDLoop++ )
			{
				// if this guy SAW an enemy recently...
				if( pSoldier->aiData.bOppList[ ubIDLoop ] >= SEEN_CURRENTLY &&
					pSoldier->aiData.bOppList[ ubIDLoop ] <= SEEN_LAST_TURN )
				{
					cnt++;
				}
			}
		}
	}

	return cnt;
}

INT32 ClosestSeenLastTurnOpponent(SOLDIERTYPE *pSoldier, INT32 * psGridNo, INT8 * pbLevel)
{
	CHECKF(pSoldier);

	INT32 sGridNo, sClosestOpponent = NOWHERE;
	UINT32 uiLoop;
	INT32 iRange, iClosestRange = 1500;
	INT8	*pbPersOL;
	INT8	bLevel, bClosestLevel;
	SOLDIERTYPE * pOpponent;

	bClosestLevel = -1;

	// look through this man's personal & public opplists for opponents known
	for (uiLoop = 0; uiLoop < guiNumMercSlots; uiLoop++)
	{
		pOpponent = MercSlots[ uiLoop ];

		if (!pOpponent)
		{
			continue;
		}

		pbPersOL = pSoldier->aiData.bOppList + pOpponent->ubID;

		// This helper includes current, this-turn and last-turn visual contacts.
		if (*pbPersOL < SEEN_CURRENTLY || *pbPersOL > SEEN_LAST_TURN)
		{
			continue;
		}

		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) || pSoldier->bSide == pOpponent->bSide ||
			(pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			pOpponent->ubBodyType == CROW)
		{
			continue;
		}

		if (*pbPersOL == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0)
		{
			if (!ValidOpponent(pSoldier, pOpponent))
				continue;
			sGridNo = pOpponent->sGridNo;
			bLevel = pOpponent->pathing.bLevel;
		}
		else
		{
			// A visual memory is still a memory: use the stored contact, not the
			// opponent object's hidden current position.
			sGridNo = gsLastKnownOppLoc[pSoldier->ubID][pOpponent->ubID];
			bLevel = gbLastKnownOppLevel[pSoldier->ubID][pOpponent->ubID];
		}

		// if we are standing at that gridno(!, obviously our info is old...)
		if (sGridNo == pSoldier->sGridNo)
		{
			continue;			// next merc
		}

		// this function is used only for turning towards closest opponent or changing stance
		// as such, if they AI is in a building,
		// we should ignore people who are on the roof of the same building as the AI
		if ( (bLevel != pSoldier->pathing.bLevel) && SameBuilding( pSoldier->sGridNo, sGridNo ) )
		{
			continue;
		}

		// I hope this will be good enough; otherwise we need a fractional/world-units-based 2D distance function
		//sRange = PythSpacesAway( pSoldier->sGridNo, sGridNo);
		iRange = GetRangeInCellCoordsFromGridNoDiff( pSoldier->sGridNo, sGridNo );

		if (iRange < iClosestRange)
		{
			iClosestRange = iRange;
			sClosestOpponent = sGridNo;
			bClosestLevel = bLevel;
		}
	}

	if (psGridNo)
	{
		*psGridNo = sClosestOpponent;
	}
	if (pbLevel)
	{
		*pbLevel = bClosestLevel;
	}
	return( sClosestOpponent );
}

// check if we have a prone sight cover from known enemies at spot
static BOOLEAN AIKnownThreatHasSightToSpot(SOLDIERTYPE *pSoldier, INT32 sSpot, BOOLEAN fUnlimited, UINT8 ubTargetStance, UINT8 ubTargetLOSPos)
{
	CHECKF(pSoldier);

	for (UINT32 uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercSlots[uiLoop];
		if (!pOpponent)
			continue;

		INT32 sThreatLoc = NOWHERE;
		INT8 bThreatLevel = 0;
		INT8 bKnowledge = NOT_HEARD_OR_SEEN;
		UINT8 ubConfidence = 0;
		if (!AIPlanningContactForOpponent(
			pSoldier, pOpponent->ubID, &sThreatLoc, &bThreatLevel,
			&ubConfidence, &bKnowledge))
		{
			continue;
		}

		const BOOLEAN fThreatStateKnown =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;

		if (fThreatStateKnown &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW ||
			 !ValidOpponent(pSoldier, pOpponent)))
		{
			continue;
		}

		if (TileIsOutOfBounds(sThreatLoc))
			continue;

		UINT16 usAdjustedSight;
		if (fThreatStateKnown)
		{
			INT16 sSightAdjustment =
				GetSightAdjustment(pOpponent, pSoldier, sSpot, pSoldier->pathing.bLevel, ubTargetStance);

			gbForceWeaponReady = true;
			UINT16 usSightLimit =
				pOpponent->GetMaxDistanceVisible(sSpot, pSoldier->pathing.bLevel, CALC_FROM_ALL_DIRS);
			gbForceWeaponReady = false;

			usAdjustedSight = max((UINT16)1,
				(UINT16)(usSightLimit + usSightLimit * sSightAdjustment / 100));
		}
		else
		{
			// Teammate reports and stale contacts use only reported confidence.
			usAdjustedSight = (UINT16)max(1,
				(MAX_VISION_RANGE * (INT32)ubConfidence) / 100);
		}

		if ((fUnlimited &&
			 LocationToLocationLineOfSightTest(sThreatLoc, bThreatLevel, sSpot, pSoldier->pathing.bLevel,
				 TRUE, NO_DISTANCE_LIMIT, STANDING_LOS_POS, ubTargetLOSPos)) ||
			(!fUnlimited &&
			 PythSpacesAway(sSpot, sThreatLoc) <= usAdjustedSight &&
			 LocationToLocationLineOfSightTest(sThreatLoc, bThreatLevel, sSpot, pSoldier->pathing.bLevel,
				 TRUE, usAdjustedSight, STANDING_LOS_POS, ubTargetLOSPos)))
		{
			return TRUE;
		}

		// Predict movement only for an opponent this soldier personally sees.
		if (fThreatStateKnown && gfTurnBasedAI)
		{
			for (UINT8 ubDirection = 0; ubDirection < NUM_WORLD_DIRECTIONS; ++ubDirection)
			{
				INT32 sTempGridNo = NewGridNo(sThreatLoc, DirectionInc(ubDirection));
				if (sTempGridNo == sThreatLoc)
					continue;

				UINT8 ubMovementCost = gubWorldMovementCosts[sTempGridNo][ubDirection][bThreatLevel];
				if (ubMovementCost >= TRAVELCOST_BLOCKED ||
					!NewOKDestination(pOpponent, sTempGridNo, FALSE, bThreatLevel))
				{
					continue;
				}

				if ((fUnlimited &&
					 LocationToLocationLineOfSightTest(sTempGridNo, bThreatLevel, sSpot, pSoldier->pathing.bLevel,
						 TRUE, NO_DISTANCE_LIMIT, STANDING_LOS_POS, ubTargetLOSPos)) ||
					(!fUnlimited &&
					 PythSpacesAway(sSpot, sTempGridNo) <= usAdjustedSight &&
					 LocationToLocationLineOfSightTest(sTempGridNo, bThreatLevel, sSpot, pSoldier->pathing.bLevel,
						 TRUE, usAdjustedSight, STANDING_LOS_POS, ubTargetLOSPos)))
				{
					return TRUE;
				}
			}
		}
	}

	return FALSE;
}

// TRUE means the checked spot provides sight cover from all known threats.
BOOLEAN ProneSightCoverAtSpot(SOLDIERTYPE *pSoldier, INT32 sSpot, BOOLEAN fUnlimited)
{
	return !AIKnownThreatHasSightToSpot(pSoldier, sSpot, fUnlimited, ANIM_PRONE, PRONE_LOS_POS);
}

BOOLEAN CrouchedSightCoverAtSpot(SOLDIERTYPE *pSoldier, INT32 sSpot, BOOLEAN fUnlimited)
{
	return !AIKnownThreatHasSightToSpot(pSoldier, sSpot, fUnlimited, ANIM_CROUCH, CROUCHED_LOS_POS);
}

BOOLEAN SightCoverAtSpot(SOLDIERTYPE *pSoldier, INT32 sSpot, BOOLEAN fUnlimited)
{
	return !AIKnownThreatHasSightToSpot(pSoldier, sSpot, fUnlimited, ANIM_STAND, STANDING_LOS_POS);
}

// Grade environmental/tactical destination hazards using only information the AI
// can legitimately know.  This is deliberately coarse: movement callers need a
// stable "worse / not worse" signal, not another expensive cover calculation.
BOOLEAN FindNearbyExplosiveStructure(INT32 sSpot, INT8 bLevel)
{
	if (TileIsOutOfBounds(sSpot))
		return FALSE;

	for (UINT8 ubDirection = 0; ubDirection < NUM_WORLD_DIRECTIONS; ++ubDirection)
	{
		INT32 sTempGridNo = NewGridNo(sSpot, DirectionInc(ubDirection));
		if (sTempGridNo != sSpot &&
			FindStructFlag(sTempGridNo, bLevel, STRUCTURE_EXPLOSIVE))
		{
			return TRUE;
		}
	}

	return FALSE;
}

UINT8 SpotDangerLevel(SOLDIERTYPE *pSoldier, INT32 sGridNo)
{
	if (!pSoldier || TileIsOutOfBounds(sGridNo))
		return 0;

	UINT8 ubLevel = 0;

	// Mild hazards: tactically undesirable, but never worth trapping a soldier over.
	if (Water(sGridNo, pSoldier->pathing.bLevel) && !pSoldier->IsFlanking())
	{
		ubLevel = 1;
	}

	// Once alerted, stepping into illumination at night is a meaningful exposure cost.
	if ((pSoldier->bTeam == ENEMY_TEAM ||
		 pSoldier->aiData.bAlertStatus >= STATUS_RED ||
		 pSoldier->ubSoldierClass == SOLDIER_CLASS_ELITE_MILITIA) &&
		(InLightAtNight(sGridNo, pSoldier->pathing.bLevel) ||
		 FindNearbyExplosiveStructure(sGridNo, pSoldier->pathing.bLevel)))
	{
		ubLevel = __max((UINT8)2, ubLevel);
	}

	// Severe terrain / area denial.
	if ((DeepWater(sGridNo, pSoldier->pathing.bLevel) && !pSoldier->IsFlanking()) ||
		RedSmokeDanger(sGridNo, pSoldier->pathing.bLevel))
	{
		ubLevel = __max((UINT8)3, ubLevel);
	}

	// Immediate hazards. FindBombNearby() only reacts to visible/detected armed bombs.
	if (InGas(pSoldier, sGridNo) ||
		FindBombNearby(pSoldier, sGridNo, BOMB_DETECTION_RANGE))
	{
		ubLevel = 4;
	}

	return ubLevel;
}

BOOLEAN CheckNPCDestination(SOLDIERTYPE *pSoldier, INT32 sGridNo)
{
	if (!pSoldier || TileIsOutOfBounds(sGridNo))
		return FALSE;

	const UINT8 ubCurrentDanger = SpotDangerLevel(pSoldier, pSoldier->sGridNo);
	const UINT8 ubTargetDanger = SpotDangerLevel(pSoldier, sGridNo);

	// Reject only a strictly worse destination.  Allow equal danger so a soldier
	// already caught in smoke/water/light can still move laterally toward an exit
	// instead of becoming artificially rooted in place.
	return (ubTargetDanger <= ubCurrentDanger);
}

BOOLEAN CheckDangerousDirection(SOLDIERTYPE *pSoldier, INT32 sSpot, INT8 bLevel)
{
	CHECKF(pSoldier);
	CHECKF(!TileIsOutOfBounds(sSpot));

	for (UINT32 uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercSlots[uiLoop];
		if (!pOpponent)
			continue;

		INT32 sThreatLoc = NOWHERE;
		INT8 bThreatLevel = 0;
		INT8 bKnowledge = NOT_HEARD_OR_SEEN;
		UINT8 ubConfidence = 0;
		if (!AIPlanningContactForOpponent(
			pSoldier, pOpponent->ubID, &sThreatLoc, &bThreatLevel,
			&ubConfidence, &bKnowledge))
		{
			continue;
		}

		const BOOLEAN fThreatStateKnown =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;

		if (fThreatStateKnown &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW ||
			 !ValidOpponent(pSoldier, pOpponent) ||
			 pOpponent->IsUnconscious() ||
			 pOpponent->IsEmptyVehicle()))
		{
			continue;
		}

		if (TileIsOutOfBounds(sThreatLoc))
			continue;

		UINT16 usAdjustedSight;
		if (fThreatStateKnown)
		{
			INT16 sSightAdjustment =
				GetSightAdjustment(pOpponent, pSoldier, sSpot, pSoldier->pathing.bLevel, ANIM_STAND);
			gbForceWeaponNotReady = true;
			UINT16 usSightLimit =
				pOpponent->GetMaxDistanceVisible(sSpot, pSoldier->pathing.bLevel, CALC_FROM_ALL_DIRS);
			gbForceWeaponNotReady = false;
			usAdjustedSight = max((UINT16)1,
				(UINT16)(usSightLimit + usSightLimit * sSightAdjustment / 100));
		}
		else
		{
			usAdjustedSight = (UINT16)max(1,
				(MAX_VISION_RANGE * (INT32)ubConfidence) / 100);
		}

		UINT8 ubDirection = AIDirection(sThreatLoc, sSpot);
		if (PythSpacesAway(sSpot, sThreatLoc) <= usAdjustedSight &&
			(CountCorpsesInDirection(pSoldier, sThreatLoc, ubDirection,
				max(usAdjustedSight, (UINT16)DAY_VISION_RANGE), FALSE, TRUE) ||
			 CountCorpsesInDirection(pSoldier, sThreatLoc, gOneCDirection[ubDirection],
				max(usAdjustedSight, (UINT16)DAY_VISION_RANGE), FALSE, TRUE) ||
			 CountCorpsesInDirection(pSoldier, sThreatLoc, gOneCCDirection[ubDirection],
				max(usAdjustedSight, (UINT16)DAY_VISION_RANGE), FALSE, TRUE)) &&
			!AnyCoverFromSpot(sSpot, bLevel, sThreatLoc, bThreatLevel) &&
			LocationToLocationLineOfSightTest(sThreatLoc, bThreatLevel, sSpot, bLevel,
				TRUE, usAdjustedSight, STANDING_LOS_POS, STANDING_LOS_POS))
		{
			return TRUE;
		}
	}

	return FALSE;
}

BOOLEAN NightLight( void )
{
	if (gubEnvLightValue >= NORMAL_LIGHTLEVEL_NIGHT - 3)
	{
		return TRUE;
	}

	return FALSE;
}

UINT8 CountTeamSeeSoldier( INT8 bTeam, SOLDIERTYPE *pSoldier )
{
	SOLDIERTYPE *pFriend;
	UINT16 cnt;
	UINT8 ubFriends = 0;

	CHECKF(pSoldier);

	if( bTeam >= MAXTEAMS )
	{
		return 0;
	}

	for ( cnt = gTacticalStatus.Team[ bTeam ].bFirstID; cnt <= gTacticalStatus.Team[ bTeam ].bLastID; cnt++ )
	{
		pFriend = MercPtrs[ cnt ];

		if( pFriend->bActive &&
			pFriend->bInSector &&
			pFriend->stats.bLife >= OKLIFE &&
			!pFriend->bCollapsed &&
			!pFriend->bBreathCollapsed &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW) )
		{
			INT8 bFriendKnowledge = pFriend->aiData.bOppList[pSoldier->ubID];
			if ((bFriendKnowledge == SEEN_CURRENTLY &&
				 LOS_Raised(pFriend, pSoldier, CALC_FROM_ALL_DIRS) > 0) ||
				bFriendKnowledge == SEEN_THIS_TURN)
			{
				ubFriends++;
			}
		}
	}	

	return ubFriends;
}

BOOLEAN EnemyCanSeeMe( SOLDIERTYPE *pSoldier )
{
	UINT32		uiLoop;
	SOLDIERTYPE *pOpponent;

	CHECKF( pSoldier );

	//loop through all the enemies and determine the cover
	for (uiLoop = 0; uiLoop<guiNumMercSlots; ++uiLoop)
	{
		pOpponent = MercSlots[ uiLoop ];

		// if this merc is inactive, at base, on assignment, dead, unconscious
		if (!pOpponent || !pOpponent->bActive || !pOpponent->bInSector || pOpponent->stats.bLife < OKLIFE)
		{
			continue;			// next merc
		}

		// if this man is neutral / on the same side, he's not an opponent
		if( CONSIDERED_NEUTRAL( pSoldier, pOpponent ) || (pSoldier->bSide == pOpponent->bSide))
		{
			continue;			// next merc
		}

		// if opponent is collapsed/breath collapsed
		if( pOpponent->bCollapsed || pOpponent->bBreathCollapsed )
		{
			continue;
		}

		// check if he is captured
		if(pOpponent->usSoldierFlagMask & SOLDIER_POW)
		{
			continue;
		}

		// check that we see opponent and he can see us
		if( (pSoldier->aiData.bOppList[ pOpponent->ubID ] == SEEN_CURRENTLY ||
			pSoldier->aiData.bOppList[ pOpponent->ubID ] == SEEN_THIS_TURN ||
			gbPublicOpplist[pSoldier->bTeam][ pOpponent->ubID ] == SEEN_CURRENTLY) &&
			pOpponent->aiData.bOppList[ pSoldier->ubID ] == SEEN_CURRENTLY &&
			LOS_Raised(pOpponent, pSoldier, CALC_FROM_ALL_DIRS) > 0 )
		{
			return TRUE;
		}
	}

	return FALSE;
}

BOOLEAN EnemyAlerted( SOLDIERTYPE *pSoldier )
{
	UINT32		uiLoop;
	SOLDIERTYPE *pOpponent;

	CHECKF( pSoldier );

	//loop through all the enemies and determine the cover
	for (uiLoop = 0; uiLoop<guiNumMercSlots; ++uiLoop)
	{
		pOpponent = MercSlots[ uiLoop ];

		// if this merc is inactive, at base, on assignment, dead, unconscious
		if (!pOpponent || !pOpponent->bActive || !pOpponent->bInSector || pOpponent->stats.bLife < OKLIFE)
		{
			continue;			// next merc
		}

		// if this man is neutral / on the same side, he's not an opponent
		if( CONSIDERED_NEUTRAL( pSoldier, pOpponent ) || (pSoldier->bSide == pOpponent->bSide))
		{
			continue;			// next merc
		}

		// if opponent is collapsed/breath collapsed
		if( pOpponent->bCollapsed || pOpponent->bBreathCollapsed )
		{
			continue;
		}

		// check if he is captured
		if(pOpponent->usSoldierFlagMask & SOLDIER_POW)
		{
			continue;
		}

		if( pOpponent->aiData.bAlertStatus >= STATUS_RED )
		{
			return TRUE;
		}
	}

	return FALSE;
}

// find alerted opponent
BOOLEAN TeamEnemyAlerted(INT8 bTeam)
{
	UINT32		uiLoop;
	SOLDIERTYPE *pOpponent;

	//loop through all the enemies
	for (uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		pOpponent = MercSlots[uiLoop];

		// if this merc is inactive, at base, on assignment, dead, unconscious
		if (!pOpponent || !pOpponent->bActive || !pOpponent->bInSector || pOpponent->stats.bLife < OKLIFE)
		{
			continue;			// next merc
		}

		if (!ValidTeamOpponent(bTeam, pOpponent))
		{
			continue;
		}

		// if opponent is collapsed/breath collapsed
		if (pOpponent->IsUnconscious())
		{
			continue;
		}

		if (pOpponent->aiData.bAlertStatus >= STATUS_RED)
		{
			return TRUE;
		}
	}

	return FALSE;
}

UINT32 CountSuspicionValue( SOLDIERTYPE *pSoldier )
{
	UINT32		uiLoop;
	SOLDIERTYPE *pOpponent;
	UINT32		uiValue;
	// Covert suspicion is observer-driven, but dozens of witnesses should not act like a psychic hive mind.
	// Keep the four strongest observer contributions and combine them with diminishing weight.
	UINT32		uiBestValue = 0;
	UINT32		uiSecondValue = 0;
	UINT32		uiThirdValue = 0;
	UINT32		uiFourthValue = 0;

	CHECKF( pSoldier );

	if( !pSoldier->bActive || !pSoldier->bInSector )
	{
		return 0;
	}

	// only new skill system
	if( !gGameOptions.fNewTraitSystem )
	{
		return 0;
	}

	//loop through all the enemies and determine the cover
	for (uiLoop = 0; uiLoop<guiNumMercSlots; ++uiLoop)
	{
		pOpponent = MercSlots[ uiLoop ];

		// if this merc is inactive, at base, on assignment, dead, unconscious
		if (!pOpponent || pOpponent->stats.bLife < OKLIFE)
		{
			continue;			// next merc
		}

		// if this man is neutral / on the same side, he's not an opponent
		if( CONSIDERED_NEUTRAL( pSoldier, pOpponent ) || (pSoldier->bSide == pOpponent->bSide))
		{
			continue;			// next merc
		}

		// creatures should not raise suspicion counter even if they are hostile to player
		if( pOpponent->bTeam == CREATURE_TEAM )
		{
			continue;
		}

		// if opponent is collapsed/breath collapsed
		if( pOpponent->bCollapsed || pOpponent->bBreathCollapsed )
		{
			continue;
		}

		// check if he is captured
		if(pOpponent->usSoldierFlagMask & SOLDIER_POW)
		{
			continue;
		}

		// check that this opponent sees us
		if( (pOpponent->aiData.bOppList[ pSoldier->ubID ] == SEEN_CURRENTLY &&
			LOS_Raised(pOpponent, pSoldier, CALC_FROM_ALL_DIRS) > 0) ||
			pOpponent->aiData.bAlertStatus >= STATUS_RED && 
			( pOpponent->aiData.bOppList[ pSoldier->ubID ] == SEEN_THIS_TURN || pOpponent->aiData.bOppList[ pSoldier->ubID ] == HEARD_THIS_TURN ) )
		{
			UINT8	ubCovertLevel = NUM_SKILL_TRAITS( pSoldier, COVERT_NT );
			INT16	sDistance = PythSpacesAway(pSoldier->sGridNo, pOpponent->sGridNo);
			INT8	bTownId = GetTownIdForSector( gWorldSectorX, gWorldSectorY );
			UINT8	ubSectorId = SECTOR(gWorldSectorX, gWorldSectorY);
			UINT8	ubSectorData = 0;

			ubSectorData = SectorExternalData[ubSectorId][gbWorldSectorZ].usCurfewValue;

			if ( NightLight() )			// suspicious at night
				ubSectorData = max( ubSectorData, 1 );
			if ( gbWorldSectorZ > 0 )	// underground we are always suspicious				
				ubSectorData = max( ubSectorData, 2 );

			// -----------------------------------------------------------------------------------------------------
			// calculate basic value 

			// Suspicion is reasoning over information already perceived, not a vision
			// bonus. Every enemy therefore scrutinizes the same evidence at the top
			// tactical level; non-enemy legacy AI keeps its configured difficulty tier.
			uiValue = 1 + ((pOpponent->bTeam == ENEMY_TEAM) ?
				4 : SoldierDifficultyLevel(pOpponent));
			// Command personnel scrutinise suspicious behaviour more effectively.
			if (HAS_SKILL_TRAIT( pOpponent, SQUADLEADER_NT ) )
			{
				uiValue += NUM_SKILL_TRAITS( pOpponent, SQUADLEADER_NT );
			}
			else if ( pOpponent->usSoldierFlagMask & SOLDIER_ENEMY_OFFICER )
			{
				uiValue += 1;
			}
			// bonus when using flashlight
			if ( pSoldier->GetBestEquippedFlashLightRange() > 0 )
			{
				uiValue++;
			}
			// Bleeding raises suspicion at range; severe bleeding is more noticeable and can expose at close range.
			if ( pSoldier->bBleeding > 0 )
			{
				uiValue += (pSoldier->bBleeding > MIN_BLEEDING_THRESHOLD) ? 2 : 1;
			}
			// bonus for soldier state
			if ( MercUnderTheInfluence( pSoldier ) ||
				GetDrunkLevel( pSoldier ) != SOBER )
			{
				uiValue += 1;
			}
			// bonus if enemy is alerted
			if ( pOpponent->aiData.bAlertStatus >= STATUS_RED )
			{
				uiValue += 2;
			}
			// bonus in combat
			if ( GuySawEnemy( pOpponent ) || pOpponent->aiData.bUnderFire )
			{
				uiValue += 2;
			}
			// bonus in capital			
			if ( bTownId == MEDUNA )	
			{
				uiValue += 2;
			}
			// wearing or carrying backpack is suspicious
			if ( UsingNewInventorySystem() && FindBackpackOnSoldier( pSoldier ) )
			{
				uiValue += 2;
			}
			// bonus if spotting
			if( pSoldier->IsSpotting() )
			{
				uiValue += 2;
			}
			// bonus if soldier is carrying item with sight bonus in main hand
			if( pSoldier->inv[HANDPOS].exists() &&
				( Item[ pSoldier->inv[HANDPOS].usItem ].dayvisionrangebonus > 0 || 
				  Item[ pSoldier->inv[HANDPOS].usItem ].brightlightvisionrangebonus > 0 || 
				  Item[ pSoldier->inv[HANDPOS].usItem ].nightvisionrangebonus > 0 || 
				  Item[ pSoldier->inv[HANDPOS].usItem ].cavevisionrangebonus > 0 ) )
			{
				uiValue += 2;
			}

			// -----------------------------------------------------------------------------------------------------
			// multipliers

			// Seeing several disguised people together is suspicious, but scale it with diminishing returns.
			// 1 spy = 100%, 2 = 150%, 3+ = 200% instead of multiplying linearly by the whole group.
			UINT8 ubSeenCovert = min(3, max(1, CountSeenCovertOpponents(pOpponent)));
			uiValue = uiValue * (100 + 50 * (ubSeenCovert - 1)) / 100;

			// Recent casualties make guards more wary, but do not let this become an unbounded multiplier.
			if( gTacticalStatus.ubArmyGuysKilled > 0 )
			{
				UINT32 uiCasualtyPercent = min(200, 100 + (UINT32)(10.0 * sqrt((DOUBLE)gTacticalStatus.ubArmyGuysKilled)));
				uiValue = uiValue * uiCasualtyPercent / 100;
			}			

			// Suspicious movement matters, but should build suspicion instead of doubling every stacked modifier.
			if ( pSoldier->bStealthMode || 
				gAnimControl[ pSoldier->usAnimState ].ubEndHeight != ANIM_STAND ||
				pSoldier->usAnimState == RUNNING )
			{
				uiValue = uiValue * 3 / 2;
			}

			// Soldier disguises invite more scrutiny than civilian disguises.
			if( pSoldier->usSoldierFlagMask & SOLDIER_COVERT_SOLDIER )
			{
				uiValue = uiValue * 3 / 2;
			}

			// Civilians are especially suspicious during a raised alert, but not four times more suspicious instantly.
			if( pSoldier->usSoldierFlagMask & SOLDIER_COVERT_CIV && pOpponent->aiData.bAlertStatus >= STATUS_RED )
			{
				uiValue = uiValue * 2;
			}

			// bonus if weapon raised
			if( WeaponReady(pSoldier) )
			{
				uiValue = uiValue * 2;
			}

			// Higher-rank uniforms are harder to impersonate convincingly, but avoid a raw x2/x3 multiplier.
			UINT8 ubUniformLevel = max(1, pSoldier->UniformLevel());
			uiValue = uiValue * (100 + 25 * (ubUniformLevel - 1)) / 100;

			// -----------------------------------------------------------------------------------------------------
			// some modifiers can reduce suspicion level

			// reduce 2-4 times if soldier has covert trait
			if ( HAS_SKILL_TRAIT( pSoldier, COVERT_NT ) )
			{
				uiValue = uiValue / (2 * NUM_SKILL_TRAITS( pSoldier, COVERT_NT ));
			}
			// for special NPCs even without covert trait
			else if( pSoldier->usSoldierFlagMask & SOLDIER_COVERT_CIV && pSoldier->usSoldierFlagMask & SOLDIER_COVERT_NPC_SPECIAL )
			{
				uiValue = uiValue / 2;
			}
			// if observer is drunk, he is less suspicious
			if( GetDrunkLevel(pOpponent) > SOBER && GetDrunkLevel(pOpponent) < HUNGOVER )
			{
				uiValue -= uiValue * GetDrunkLevel(pOpponent) * 25 / 100;
			}

			// finally reduce according to the distance, 4 times at max day vision range
			if( sDistance > DAY_VISION_RANGE / 4 )
			{
				uiValue = uiValue * (DAY_VISION_RANGE / 4) / sDistance;
			}
			// bonus to value is very close and alert is raised
			else if( pOpponent->aiData.bAlertStatus >= STATUS_RED  )
			{
				uiValue = uiValue * (DAY_VISION_RANGE / 4) / (max(sDistance, 1));
			}

			// -----------------------------------------------------------------------------------------------------
			// Insert this observer into the four strongest contributions.
			if ( uiValue >= uiBestValue )
			{
				uiFourthValue = uiThirdValue;
				uiThirdValue = uiSecondValue;
				uiSecondValue = uiBestValue;
				uiBestValue = uiValue;
			}
			else if ( uiValue >= uiSecondValue )
			{
				uiFourthValue = uiThirdValue;
				uiThirdValue = uiSecondValue;
				uiSecondValue = uiValue;
			}
			else if ( uiValue >= uiThirdValue )
			{
				uiFourthValue = uiThirdValue;
				uiThirdValue = uiValue;
			}
			else if ( uiValue > uiFourthValue )
			{
				uiFourthValue = uiValue;
			}
		}
	}

	return uiBestValue + uiSecondValue / 2 + uiThirdValue / 4 + uiFourthValue / 8;
}

BOOLEAN EnemySeenSoldierRecently( SOLDIERTYPE *pSoldier, UINT8 ubMax, BOOLEAN fOnlyAlerted )
{
	UINT32		uiLoop;
	SOLDIERTYPE *pOpponent;

	// loop through all the enemies
	for (uiLoop = 0; uiLoop<guiNumMercSlots; ++uiLoop)
	{
		pOpponent = MercSlots[ uiLoop ];

		// if this merc is inactive, at base, on assignment, dead, unconscious
		if (!pOpponent || pOpponent->stats.bLife < OKLIFE)
		{
			continue;			// next merc
		}

		// if this man is neutral / on the same side, he's not an opponent
		if( CONSIDERED_NEUTRAL( pSoldier, pOpponent ) || (pSoldier->bSide == pOpponent->bSide))
		{
			continue;			// next merc
		}

		// if opponent is collapsed/breath collapsed
		if( pOpponent->bCollapsed || pOpponent->bBreathCollapsed )
		{
			continue;
		}

		// check if he is captured
		if(pOpponent->usSoldierFlagMask & SOLDIER_POW)
		{
			continue;
		}

		if( fOnlyAlerted && pOpponent->aiData.bAlertStatus < STATUS_RED )
		{
			continue;
		}

		// check that this opponent sees us
		if( pOpponent->aiData.bOppList[ pSoldier->ubID ] >= SEEN_CURRENTLY && 
			pOpponent->aiData.bOppList[ pSoldier->ubID ] <= ubMax ||
			gbPublicOpplist[pOpponent->bTeam][pSoldier->ubID] >= SEEN_CURRENTLY &&
			gbPublicOpplist[pOpponent->bTeam][pSoldier->ubID] <= ubMax )
		{
			return( TRUE );
		}
	}

	return FALSE;
}

BOOLEAN EnemyHeardSoldierRecently( SOLDIERTYPE *pSoldier, UINT8 ubMax, BOOLEAN fOnlyAlerted )
{
	UINT32		uiLoop;
	SOLDIERTYPE *pOpponent;

	// loop through all the enemies
	for (uiLoop = 0; uiLoop<guiNumMercSlots; ++uiLoop)
	{
		pOpponent = MercSlots[ uiLoop ];

		// if this merc is inactive, at base, on assignment, dead, unconscious
		if (!pOpponent || pOpponent->stats.bLife < OKLIFE)
		{
			continue;			// next merc
		}

		// if this man is neutral / on the same side, he's not an opponent
		if( CONSIDERED_NEUTRAL( pSoldier, pOpponent ) || (pSoldier->bSide == pOpponent->bSide))
		{
			continue;			// next merc
		}

		// if opponent is collapsed/breath collapsed
		if( pOpponent->bCollapsed || pOpponent->bBreathCollapsed )
		{
			continue;
		}

		// check if he is captured
		if(pOpponent->usSoldierFlagMask & SOLDIER_POW)
		{
			continue;
		}

		if( fOnlyAlerted && pOpponent->aiData.bAlertStatus < STATUS_RED )
		{
			continue;
		}

		// check that this opponent sees us
		if( pOpponent->aiData.bOppList[ pSoldier->ubID ] <= HEARD_THIS_TURN && 
			pOpponent->aiData.bOppList[ pSoldier->ubID ] >= ubMax ||
			gbPublicOpplist[pOpponent->bTeam][pSoldier->ubID] <= HEARD_THIS_TURN &&
			gbPublicOpplist[pOpponent->bTeam][pSoldier->ubID] >= ubMax )
		{
			return( TRUE );
		}
	}

	return FALSE;
}

// count friends in black state or under fire
UINT8 CountTeamCombat( SOLDIERTYPE *pSoldier )
{
	CHECKF(pSoldier);

	SOLDIERTYPE * pFriend;
	UINT8 ubFriendCount = 0;

	// Run through each friendly.
	for ( UINT8 iCounter = gTacticalStatus.Team[ pSoldier->bTeam ].bFirstID ; iCounter <= gTacticalStatus.Team[ pSoldier->bTeam ].bLastID ; iCounter ++ )
	{
		pFriend = MercPtrs[ iCounter ];		

		// Make sure that character is alive, not too shocked, and conscious
		if( pFriend->bActive &&
			pFriend->bInSector &&
			pFriend->stats.bLife >= OKLIFE &&
			!pFriend->bCollapsed &&
			!pFriend->bBreathCollapsed &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW) )
		{
			if(	pFriend->aiData.bAlertStatus == STATUS_BLACK ||
				pFriend->aiData.bUnderFire )
			{
				ubFriendCount++;
			}
		}
	}

	return ubFriendCount;
}

UINT8 CountFriendsNeedHelp( SOLDIERTYPE *pSoldier )
{
	CHECKF(pSoldier);

	SOLDIERTYPE * pFriend;
	UINT8 ubFriendCount = 0;

	// Run through each friendly.
	for ( UINT8 iCounter = gTacticalStatus.Team[ pSoldier->bTeam ].bFirstID ; iCounter <= gTacticalStatus.Team[ pSoldier->bTeam ].bLastID ; iCounter ++ )
	{
		pFriend = MercPtrs[ iCounter ];		

		// Make sure that character is alive, not too shocked, and conscious
		if (pFriend != pSoldier && 
			pFriend->bActive &&
			pFriend->bInSector &&
			pFriend->stats.bLife >= OKLIFE &&
			!pFriend->bCollapsed &&
			!pFriend->bBreathCollapsed &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW))
		{
			//if( pFriend->aiData.bUnderFire || CountSeenEnemiesLastTurn(pFriend) > CountNearbyFriends(pFriend, pFriend->sGridNo, DAY_VISION_RANGE/4) )
			if( CountSeenEnemiesLastTurn(pFriend) > CountNearbyFriends(pFriend, pFriend->sGridNo, DAY_VISION_RANGE/4) )
			{
				ubFriendCount++;
			}
		}
	}

	return ubFriendCount;
}

BOOLEAN GuyKnowsEnemyPosition( SOLDIERTYPE * pSoldier )
{
	CHECKF(pSoldier);

	for (UINT32 uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercSlots[uiLoop];
		if (!pOpponent)
			continue;

		INT32 sKnownSpot = NOWHERE;
		INT8 bKnowledge = NOT_HEARD_OR_SEEN;
		if (!AIPlanningContactForOpponent(
			pSoldier, pOpponent->ubID, &sKnownSpot, NULL, NULL, &bKnowledge))
		{
			continue;
		}

		const BOOLEAN fDirectVisualContact =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;

		if (fDirectVisualContact &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW ||
			 !ValidOpponent(pSoldier, pOpponent)))
		{
			continue;
		}

		if (!TileIsOutOfBounds(sKnownSpot))
			return TRUE;
	}

	return FALSE;
}

BOOLEAN AICheckIsSniper(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	if( AIGunType(pSoldier) != GUN_SN_RIFLE )
	{
		return FALSE;
	}

	if( AIGunRange(pSoldier) / CELL_X_SIZE > DAY_VISION_RANGE &&
		AIGunScoped(pSoldier) &&
		pSoldier->stats.bMarksmanship > 90 && HAS_SKILL_TRAIT(pSoldier, SNIPER_NT) )
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN AICheckIsMarksman(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	if( AIGunType(pSoldier) != GUN_SN_RIFLE && 
		AIGunType(pSoldier) != GUN_RIFLE &&
		AIGunType(pSoldier) != GUN_AS_RIFLE )
	{
		return FALSE;
	}

	if( AIGunRange(pSoldier) / CELL_X_SIZE >= DAY_VISION_RANGE &&
		(AIGunScoped(pSoldier) || pSoldier->stats.bMarksmanship > 90 || HAS_SKILL_TRAIT(pSoldier, SNIPER_NT)) )
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN AICheckIsRadioOperator(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	if( pSoldier->CanUseRadio(FALSE) )
		return TRUE;

	return FALSE;
}

BOOLEAN AICheckIsMedic(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	if( !HAS_SKILL_TRAIT( pSoldier, DOCTOR_NT) )
	{
		return FALSE;
	}

	if( FindFirstAidKit( pSoldier ) != NO_SLOT ||
		FindMedKit( pSoldier ) != NO_SLOT )
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN AICheckIsMortarOperator(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	if ( pSoldier->HasMortar() )
	{
		return TRUE;
	}

	return FALSE;
}

UINT8 AIGetCommandRank(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || !AICombatTeam(pSoldier))
		return AI_RANK_NONE;

	if (pSoldier->bTeam == ENEMY_TEAM)
	{
		// Formal role flags are normalized when the sector/fireteam roster is ready.
		// Rank lookup must remain read-only; otherwise harmless AI queries can assign
		// officers while soldiers are still being created.
		if (pSoldier->usSoldierFlagMask & SOLDIER_VIP)
			return AI_RANK_GENERAL;

		if (pSoldier->usSoldierFlagMask & SOLDIER_ENEMY_OFFICER)
			return NUM_SKILL_TRAITS(pSoldier, SQUADLEADER_NT) > 1 ? AI_RANK_CAPTAIN : AI_RANK_LIEUTENANT;
	}

	// Experience-based EnemyRank.xml names are useful as an NCO ladder, but a high
	// experience level alone must never create a Lieutenant/Major/Colonel command role.
	// Formal officers are assigned above; ordinary veterans top out at Staff Sergeant.
	return (UINT8)__max((INT32)AI_RANK_RECRUIT,
		__min((INT32)AI_RANK_STAFF_SERGEANT, (INT32)pSoldier->stats.bExpLevel));
}

UINT8 AICommandAuthority(SOLDIERTYPE *pSoldier)
{
	switch (AIGetCommandRank(pSoldier))
	{
	case AI_RANK_CORPORAL:
	case AI_RANK_SPECIALIST:      return 1;
	case AI_RANK_SERGEANT:        return 2;
	case AI_RANK_STAFF_SERGEANT:  return 3;
	case AI_RANK_LIEUTENANT:      return 4;
	case AI_RANK_CAPTAIN:         return 5;
	case AI_RANK_MAJOR:           return 6;
	case AI_RANK_COLONEL:         return 7;
	case AI_RANK_GENERAL:         return 8;
	default:                      return 0;
	}
}

BOOLEAN AICheckIsNCO(SOLDIERTYPE *pSoldier)
{
	UINT8 ubRank = AIGetCommandRank(pSoldier);
	return ubRank >= AI_RANK_CORPORAL && ubRank <= AI_RANK_STAFF_SERGEANT;
}

BOOLEAN AICheckIsLeader(SOLDIERTYPE *pSoldier)
{
	return AICommandAuthority(pSoldier) >= 2;
}

BOOLEAN AICheckIsOfficer(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->bTeam != ENEMY_TEAM)
		return FALSE;
	EnsureEnemyCommandRoles();
	return (pSoldier->usSoldierFlagMask & (SOLDIER_ENEMY_OFFICER | SOLDIER_VIP)) != 0;
}

BOOLEAN AICheckIsGLOperator(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	if (!SoldierAI(pSoldier))
	{
		return FALSE;
	}

	// find GL
	INT8 bWeaponIn = FindAIUsableObjClass(pSoldier, IC_LAUNCHER);
	if (bWeaponIn != NO_SLOT &&
		(EnoughAmmo(pSoldier, FALSE, bWeaponIn) || FindAmmoToReload(pSoldier, bWeaponIn, NO_SLOT) != NO_SLOT))
	{
		return TRUE;
	}

	// check for attached GL
	INT8 bGunSlot = FindAIUsableObjClass(pSoldier, IC_GUN);
	INT8 bRealWeaponMode = pSoldier->bWeaponMode;
	pSoldier->bWeaponMode = WM_ATTACHED_GL;		// So that EnoughAmmo will check for a grenade not a bullet
	if (bGunSlot != NO_SLOT &&
		IsGrenadeLauncherAttached(&pSoldier->inv[bGunSlot]) &&
		(EnoughAmmo(pSoldier, FALSE, bGunSlot) || FindAmmoToReload(pSoldier, bGunSlot, NO_SLOT) != NO_SLOT))
	{
		pSoldier->bWeaponMode = bRealWeaponMode;
		return TRUE;
	}
	pSoldier->bWeaponMode = bRealWeaponMode;

	return FALSE;
}

BOOLEAN AICheckIsCommander(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->bTeam != ENEMY_TEAM)
		return FALSE;
	UINT8 rank = AIGetCommandRank(pSoldier);
	return rank == AI_RANK_GENERAL || rank == AI_RANK_CAPTAIN;
}

BOOLEAN AICheckIsMachinegunner(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	if( AIGunType(pSoldier) == GUN_LMG )
	{
		return TRUE;
	}
	return FALSE;
}

UINT16 AIGunType(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	INT8 bWeaponIn;

	bWeaponIn = FindAIUsableObjClass( pSoldier, IC_GUN );

	if (bWeaponIn == NO_SLOT)
	{
		return 0;
	}

	if(pSoldier->inv[bWeaponIn].exists())
	{
		return Weapon[pSoldier->inv[bWeaponIn].usItem].ubWeaponType;
	}
	return 0;
}

UINT16 AIGunAmmo(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	INT8 bWeaponIn = FindAIUsableObjClass(pSoldier, IC_GUN);

	if (bWeaponIn == NO_SLOT)
	{
		return 0;
	}

	if (pSoldier->inv[bWeaponIn].exists())
	{
		return pSoldier->inv[bWeaponIn][0]->data.gun.ubGunShotsLeft;
	}

	return 0;
}

BOOLEAN AIGunAutofireCapable(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	INT8 bWeaponIn = FindAIUsableObjClass(pSoldier, IC_GUN);

	if (bWeaponIn == NO_SLOT)
	{
		return FALSE;
	}

	if (pSoldier->inv[bWeaponIn].exists())
	{
		return IsGunAutofireCapable(&pSoldier->inv[bWeaponIn]);
	}

	return FALSE;
}

UINT8 AIGunDeadliness(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	INT8 bWeaponIn;

	bWeaponIn = FindAIUsableObjClass(pSoldier, IC_GUN);

	if (bWeaponIn == NO_SLOT)
	{
		return 0;
	}

	if (pSoldier->inv[bWeaponIn].exists())
	{
		return Weapon[pSoldier->inv[bWeaponIn].usItem].ubDeadliness;
	}

	return 0;
}

UINT16 AIGunClass(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	INT8 bWeaponIn;

	bWeaponIn = FindAIUsableObjClass(pSoldier, IC_GUN);

	if (bWeaponIn == NO_SLOT)
	{
		return 0;
	}

	if (pSoldier->inv[bWeaponIn].exists())
	{
		return Weapon[pSoldier->inv[bWeaponIn].usItem].ubWeaponClass;
	}
	return 0;
}

INT16 AIGunMinAPsToShoot(SOLDIERTYPE *pSoldier, BOOLEAN fRaiseCost)
{
	CHECKF(pSoldier);

	INT8 bWeaponIn;
	INT16 sShootAP;

	bWeaponIn = FindAIUsableObjClass(pSoldier, IC_GUN);

	if (bWeaponIn == NO_SLOT)
	{
		return 0;
	}

	// temporarily move to handpos to call MinAPsToAttack
	if (bWeaponIn != HANDPOS)
	{
		RearrangePocket(pSoldier, HANDPOS, bWeaponIn, TEMPORARILY);
	}

	//sShootAP = MinAPsToAttack(pSoldier, pSoldier->sLastTarget, ADDTURNCOST, 0, bWeaponIn != HANDPOS ? TRUE: FALSE);
	sShootAP = MinAPsToAttack(pSoldier, pSoldier->sLastTarget, ADDTURNCOST, 0, fRaiseCost);

	// return to original position
	if (bWeaponIn != HANDPOS)
	{
		RearrangePocket(pSoldier, HANDPOS, bWeaponIn, TEMPORARILY);
	}

	return sShootAP;
}

FLOAT AIGunScopeMagFactor(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier)
	{
		return 1.0f;
	}

	INT8 bWeaponIn;
	OBJECTTYPE *pObj;
	FLOAT BestFactor = 1.0;

	bWeaponIn = FindAIUsableObjClass(pSoldier, IC_GUN);

	if (bWeaponIn == NO_SLOT)
	{
		return 1.0f;
	}

	pObj = &pSoldier->inv[bWeaponIn];

	if (pObj->exists())
	{
		BestFactor = Item[pObj->usItem].scopemagfactor;

		for (attachmentList::iterator iter = (*pObj)[0]->attachments.begin(); iter != (*pObj)[0]->attachments.end(); ++iter)
		{
			if (iter->exists())
			{
				BestFactor = max(BestFactor, Item[iter->usItem].scopemagfactor);
			}
		}
	}

	return(BestFactor);
}

BOOLEAN CheckDoorAtGridno( UINT32 usGridNo )
{
	STRUCTURE *pStructure;

	pStructure = FindStructure( usGridNo, STRUCTURE_ANYDOOR );
	if ( pStructure != NULL)
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN CheckDoorNearGridno( UINT32 usGridNo )
{
	UINT8	ubMovementCost;
	INT32	sTempGridNo;
	UINT8	ubDirection;

	if( CheckDoorAtGridno(usGridNo) )
	{
		return TRUE;
	}

	for (ubDirection = 0; ubDirection < NUM_WORLD_DIRECTIONS; ubDirection++)
	{
		sTempGridNo = NewGridNo( usGridNo, DirectionInc( ubDirection ) );
		ubMovementCost = gubWorldMovementCosts[ sTempGridNo ][ ubDirection ][ 0 ];
		if ( IS_TRAVELCOST_DOOR( ubMovementCost ) )
		{
			return TRUE;
		}
	}

	return FALSE;
}

BOOLEAN FindBombNearby(SOLDIERTYPE *pSoldier, INT32 sGridNo, UINT8 ubDistance)
{
	UINT32	uiBombIndex;
	INT32	sCheckGridno;

	INT16 sMaxLeft, sMaxRight, sMaxUp, sMaxDown, sXOffset, sYOffset;

	// determine maximum horizontal limits
	sMaxLeft  = min( ubDistance, (sGridNo % MAXCOL));
	sMaxRight = min( ubDistance, MAXCOL - ((sGridNo % MAXCOL) + 1));

	// determine maximum vertical limits
	sMaxUp   = min( ubDistance, (sGridNo / MAXROW));
	sMaxDown = min( ubDistance, MAXROW - ((sGridNo / MAXROW) + 1));

	for (sYOffset = -sMaxUp; sYOffset <= sMaxDown; sYOffset++)
	{
		for (sXOffset = -sMaxLeft; sXOffset <= sMaxRight; sXOffset++)
		{
			sCheckGridno = sGridNo + sXOffset + (MAXCOL * sYOffset);

			if( TileIsOutOfBounds(sCheckGridno) )
			{
				continue;
			}

			// search all bombs that we can see
			for (uiBombIndex = 0; uiBombIndex < guiNumWorldBombs; uiBombIndex++)
			{
				if (gWorldBombs[uiBombIndex].fExists &&					
					gWorldItems[gWorldBombs[uiBombIndex].iItemIndex].sGridNo == sCheckGridno &&
					gWorldItems[gWorldBombs[uiBombIndex].iItemIndex].ubLevel == pSoldier->pathing.bLevel &&					
					gWorldItems[gWorldBombs[uiBombIndex].iItemIndex].bVisible == VISIBLE &&
					gWorldItems[gWorldBombs[uiBombIndex].iItemIndex].usFlags & WORLD_ITEM_ARMED_BOMB)
				{
					if (pSoldier->aiData.bNeutral ||
						pSoldier->aiData.bAlertStatus >= STATUS_RED ||
						!TileIsOutOfBounds(gWorldItems[gWorldBombs[uiBombIndex].iItemIndex].sGridNo) &&
						PythSpacesAway(pSoldier->sGridNo, gWorldItems[gWorldBombs[uiBombIndex].iItemIndex].sGridNo) <= (INT16)MAX_VISION_RANGE &&
						SoldierTo3DLocationLineOfSightTest(pSoldier, sCheckGridno, pSoldier->pathing.bLevel, 1, FALSE, CALC_FROM_WANTED_DIR))
					{
						return TRUE;
					}
				}
			}
		}
	}

	return FALSE;
}

BOOLEAN TeamKnowsSoldier( INT8 bTeam, UINT8 ubID )
{
	SOLDIERTYPE *pFriend;
	UINT16 cnt;

	if( bTeam >= MAXTEAMS || ubID == NOBODY )
	{
		return FALSE;
	}

	if( gbPublicOpplist[bTeam][ubID] != NOT_HEARD_OR_SEEN )
	{
		return TRUE;
	}

	for ( cnt = gTacticalStatus.Team[ bTeam ].bFirstID; cnt <= gTacticalStatus.Team[ bTeam ].bLastID; cnt++ )
	{
		pFriend = MercPtrs[ cnt ];

		if( pFriend->bActive &&
			pFriend->bInSector &&
			pFriend->stats.bLife >= OKLIFE &&
			!pFriend->bCollapsed &&
			!pFriend->bBreathCollapsed &&
			pFriend->aiData.bOppList[ ubID ] != NOT_HEARD_OR_SEEN )
		{
			return TRUE;
		}
	}	

	return FALSE;
}

INT16 DistanceToClosestNotSeekEnemyFriend( SOLDIERTYPE *pSoldier, INT32 sGridNo )
{
	CHECKF(pSoldier);

	INT16 sDistance = 0;
	SOLDIERTYPE * pFriend;

	// Run through each friendly.
	for ( UINT8 iCounter = gTacticalStatus.Team[ pSoldier->bTeam ].bFirstID ; iCounter <= gTacticalStatus.Team[ pSoldier->bTeam ].bLastID ; iCounter ++ )
	{
		pFriend = MercPtrs[ iCounter ];		

		// Make sure that character is alive, not too shocked, and conscious
		if( pFriend->bActive &&
			pFriend->bInSector &&
			pFriend != pSoldier &&
			pFriend->aiData.bOrders != SEEKENEMY &&
			pFriend->stats.bLife >= OKLIFE &&
			!pFriend->bCollapsed &&
			!pFriend->bBreathCollapsed &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW) )
		{
			if(	sDistance == 0 || PythSpacesAway(sGridNo, pFriend->sGridNo) < sDistance )
			{
				sDistance = PythSpacesAway(sGridNo, pFriend->sGridNo);
			}
		}
	}

	return sDistance;
}

BOOLEAN LastTargetCollapsed( SOLDIERTYPE *pSoldier )
{
	CHECKF(pSoldier);
	UINT8 ubTarget;

	if(TileIsOutOfBounds(pSoldier->sLastTarget))
	{
		return FALSE;
	}

	// since we don't know the level of target, check both ground and roof levels
	ubTarget = WhoIsThere2( pSoldier->sLastTarget, 0 );
	if( ubTarget == NOBODY )
	{
		ubTarget = WhoIsThere2( pSoldier->sLastTarget, 1 );
	}

	// if still cannot find somebody, return FALSE
	if( ubTarget == NOBODY )
	{
		return FALSE;
	}

	// Current collapse state is legitimate only if this soldier still personally
	// sees the occupant of the last-target tile. Otherwise the old tile is merely memory.
	if (PersonalKnowledge(pSoldier, ubTarget) != SEEN_CURRENTLY ||
		LOS_Raised(pSoldier, MercPtrs[ubTarget], CALC_FROM_ALL_DIRS) <= 0)
		return FALSE;

	if( MercPtrs[ubTarget]->stats.bLife < OKLIFE ||
		MercPtrs[ubTarget]->bCollapsed ||
		MercPtrs[ubTarget]->bBreathCollapsed )
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN LastTargetSuppressed( SOLDIERTYPE *pSoldier )
{
	CHECKF(pSoldier);
	UINT8 ubTarget;

	if(TileIsOutOfBounds(pSoldier->sLastTarget))
	{
		return FALSE;
	}

	// since we don't know the level of target, check both ground and roof levels
	ubTarget = WhoIsThere2( pSoldier->sLastTarget, 0 );
	if( ubTarget == NOBODY )
	{
		ubTarget = WhoIsThere2( pSoldier->sLastTarget, 1 );
	}

	// if still cannot find somebody, return FALSE
	if( ubTarget == NOBODY )
	{
		return FALSE;
	}

	// Suppression/cowering is visible state, not something inferred through an
	// old target tile after contact has been lost.
	if (PersonalKnowledge(pSoldier, ubTarget) != SEEN_CURRENTLY ||
		LOS_Raised(pSoldier, MercPtrs[ubTarget], CALC_FROM_ALL_DIRS) <= 0)
		return FALSE;

	if( CoweringShockLevel(MercPtrs[ubTarget]) )
	{
		return TRUE;
	}

	return FALSE;
}

// use soldier AI - merc bodytype, no robots/tanks/boxers/etc
BOOLEAN SoldierAI( SOLDIERTYPE *pSoldier )
{
	CHECKF(pSoldier);

	BOOLEAN fCivilian = (PTR_CIVILIAN && (pSoldier->ubCivilianGroup == NON_CIV_GROUP ||
		(pSoldier->aiData.bNeutral && gTacticalStatus.fCivGroupHostile[pSoldier->ubCivilianGroup] == CIV_GROUP_NEUTRAL) ||
		(pSoldier->ubBodyType >= FATCIV && pSoldier->ubBodyType <= CRIPPLECIV)));

	if(!IS_MERC_BODY_TYPE( pSoldier ) ||
		pSoldier->aiData.bNeutral ||
		fCivilian ||
		pSoldier->flags.uiStatusFlags & SOLDIER_BOXER ||
		TANK(pSoldier) ||
		pSoldier->flags.uiStatusFlags & SOLDIER_VEHICLE ||
		AM_A_ROBOT(pSoldier))
		return FALSE;

	return TRUE;
}

// danger percent based on distance to closest smoke effect
UINT8 RedSmokeDanger(INT32 sGridNo, INT8 bLevel)
{
	UINT32	uiCnt;
	INT32	sDist;
	INT32	sClosestDist;
	INT32	sMaxDist = min(gSkillTraitValues.usVOMortarRadius, DAY_VISION_RANGE / 2);
	INT32	sClosestSmoke = NOWHERE;
	UINT8	ubDangerPercent = 0;

	if (TileIsOutOfBounds(sGridNo))
	{
		return 0;
	}

	if (!gSkillTraitValues.fROAllowArtillery)
	{
		return 0;
	}

	// no artillery strike danger underground
	if (gbWorldSectorZ > 0)
	{
		return 0;
	}

	// check if artillery strike was ordered by any team
	if (!CheckArtilleryStrike())
	{
		return 0;
	}

	// no danger when in a building
	if (bLevel == 0 && CheckRoof(sGridNo))
	{
		return 0;
	}

	// deep water should be safe
	if (DeepWater(sGridNo, bLevel))
	{
		return 0;
	}

	// Dense local terrain can provide credible overhead/fragmentation protection.
	if (bLevel == 0 && TerrainDensity(sGridNo, bLevel, 2, FALSE) >= 20)
	{
		return 0;
	}

	//loop through all red smoke effects and find closest
	for (uiCnt = 0; uiCnt < guiNumSmokeEffects; uiCnt++)
	{
		if (gSmokeEffectData[uiCnt].fAllocated &&
			gSmokeEffectData[uiCnt].bType == SIGNAL_SMOKE_EFFECT &&
			!TileIsOutOfBounds(gSmokeEffectData[uiCnt].sGridNo))
		{
			sDist = PythSpacesAway(gSmokeEffectData[uiCnt].sGridNo, sGridNo);

			if (sClosestSmoke == NOWHERE || sDist < sClosestDist)
			{
				sClosestDist = sDist;
				sClosestSmoke = gSmokeEffectData[uiCnt].sGridNo;
			}
		}
	}

	// if we found red smoke, calculate danger percent based on distance
	// 0% at DAY_VISION_RANGE/2, 100% at zero range
	if (sClosestSmoke != NOWHERE)
	{
		ubDangerPercent = 100 * (sMaxDist - min(sMaxDist, sClosestDist)) / sMaxDist;
	}

	return ubDangerPercent;
}

BOOLEAN CheckRoof( INT32 sGridNo )
{
	if ( FindStructure( sGridNo, STRUCTURE_ROOF ) != NULL )
	{
		return TRUE;
	}

	return FALSE;
}

// check if artillery strike was ordered by any team
BOOLEAN CheckArtilleryStrike( void )
{
	UINT32	uiBombIndex;
	OBJECTTYPE *pObj;

	// search all bombs
	for (uiBombIndex = 0; uiBombIndex < guiNumWorldBombs; uiBombIndex++)
	{
		if (gWorldBombs[ uiBombIndex ].fExists &&
			gWorldItems[ gWorldBombs[ uiBombIndex ].iItemIndex ].usFlags & WORLD_ITEM_ARMED_BOMB )
		{
			pObj = &( gWorldItems[ gWorldBombs[uiBombIndex].iItemIndex ].object );

			if( pObj && pObj->exists() && (*pObj)[0]->data.ubWireNetworkFlag & ANY_ARTILLERY_FLAG )
			{
				return TRUE;
			}
		}
	}

	return FALSE;
}

UINT8 CountFriendsLastAttackHit(SOLDIERTYPE *pSoldier, INT32 sGridNo, INT16 sDistance)
{
	CHECKF(pSoldier);

	SOLDIERTYPE * pFriend;
	UINT8 ubFriendCount = 0;

	// Run through each friendly.
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID; iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; iCounter++)
	{
		pFriend = MercPtrs[iCounter];

		if (pFriend &&
			pFriend != pSoldier &&
			pFriend->bActive &&
			pFriend->bInSector &&
			AISameFireteam(pSoldier, pFriend) &&
			pFriend->stats.bLife >= OKLIFE &&
			!pFriend->bCollapsed &&
			!pFriend->bBreathCollapsed &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW) &&
			!(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) &&
			!AIDisengagementActive(pFriend) && !AIEscapeActive(pFriend) &&
			pFriend->aiData.bOrders > ONGUARD &&
			PythSpacesAway(sGridNo, pFriend->sGridNo) <= sDistance &&
			(pFriend->LastAttackHit() || pFriend->usSoldierFlagMask2 & SOLDIER_SUCCESSFUL_ATTACK || pFriend->LastTargetSuppressed()))
		{
			ubFriendCount++;
		}
	}

	return ubFriendCount;
}

BOOLEAN AICheckSuccessfulAttack(SOLDIERTYPE *pSoldier, BOOLEAN fGroup)
{
	CHECKF(pSoldier);

	if (pSoldier->LastAttackHit() && pSoldier->sLastTarget != NOWHERE || pSoldier->usSoldierFlagMask2 & SOLDIER_SUCCESSFUL_ATTACK)
	{
		return TRUE;
	}
	if (pSoldier->LastTargetCollapsed() || pSoldier->LastTargetSuppressed())
	{
		return TRUE;
	}
	if (fGroup && CountFriendsLastAttackHit(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 4))
	{
		return TRUE;
	}

	INT32 sClosestOpponent = AIPrimaryPlanningThreatSpot(pSoldier);
	if (fGroup &&
		!TileIsOutOfBounds(sClosestOpponent) &&
		CountFriendsLastAttackHit(pSoldier, sClosestOpponent, DAY_VISION_RANGE))
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN AICheckWeOutnumberSector(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	UINT8 ubNumFriends = 0;
	UINT8 ubNumOpponents = 0;

	for (UINT32 uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercSlots[uiLoop];
		if (!pOpponent)
			continue;

		if (pOpponent->bTeam == pSoldier->bTeam ||
			pOpponent->bSide == pSoldier->bSide)
		{
			if (pOpponent->bActive && pOpponent->bInSector &&
				pOpponent->stats.bLife >= OKLIFE &&
				!(pOpponent->usSoldierFlagMask & SOLDIER_POW))
			{
				++ubNumFriends;
			}
			continue;
		}

		INT32 sKnownSpot = NOWHERE;
		INT8 bKnowledge = NOT_HEARD_OR_SEEN;
		if (!AIPlanningContactForOpponent(
			pSoldier, pOpponent->ubID, &sKnownSpot, NULL, NULL, &bKnowledge))
		{
			continue;
		}

		const BOOLEAN fDirectVisualContact =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;

		if (fDirectVisualContact &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW ||
			 !ValidOpponent(pSoldier, pOpponent) ||
			 pOpponent->IsUnconscious() ||
			 (pOpponent->usSoldierFlagMask & SOLDIER_POW)))
		{
			continue;
		}

		if (TileIsOutOfBounds(sKnownSpot))
			continue;

		++ubNumOpponents;
	}

	return (ubNumOpponents > 0 && ubNumFriends > ubNumOpponents * 2);
}

BOOLEAN AICheckWeOutnumberPublic(SOLDIERTYPE *pSoldier, INT32 sSpot)
{
	CHECKF(pSoldier);
	if (TileIsOutOfBounds(sSpot))
	{
		return FALSE;
	}

	UINT8 ubFriends = AICountNearbyOperationalFriends(pSoldier, sSpot, TACTICAL_RANGE);
	UINT8 ubEnemies = CountPublicKnownEnemies(pSoldier, sSpot, TACTICAL_RANGE);

	if (ubEnemies > 0 && ubFriends > 2 * ubEnemies)
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN AICheckWeOutnumberLocal(SOLDIERTYPE *pSoldier, INT32 sSpot)
{
	CHECKF(pSoldier);
	if (TileIsOutOfBounds(sSpot))
	{
		return FALSE;
	}

	UINT8 ubFriends = 0;
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier || !pFriend->bActive || !pFriend->bInSector ||
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			AIDisengagementActive(pFriend) || AIEscapeActive(pFriend) ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > TACTICAL_RANGE / 2)
		{
			continue;
		}
		++ubFriends;
	}

	UINT8 ubEnemies = CountPublicKnownEnemies(pSoldier, sSpot, TACTICAL_RANGE);

	if (ubEnemies > 0 && ubFriends > 2 * ubEnemies)
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN AICheckWeOutnumber(SOLDIERTYPE *pSoldier, INT32 sSpot)
{
	CHECKF(pSoldier);
	if (TileIsOutOfBounds(sSpot))
	{
		return FALSE;
	}

	if (AICheckWeOutnumberPublic(pSoldier, sSpot) || AICheckWeOutnumberLocal(pSoldier, sSpot))
		//pSoldier->bTeam == ENEMY_TEAM && gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition < 10 && WeAttack(pSoldier->bTeam))
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN AICheckHasGun( SOLDIERTYPE *pSoldier )
{
	CHECKF(pSoldier);

	if( FindAIUsableObjClass( pSoldier, IC_GUN ) != NO_SLOT )
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN AICheckShortWeaponRange( SOLDIERTYPE *pSoldier )
{
	CHECKF(pSoldier);

	if( !AICheckHasGun( pSoldier ) )
	{
		return TRUE;
	}

	if( AIGunRange(pSoldier) < DAY_VISION_RANGE / 2 )
	{
		return TRUE;
	}

	return FALSE;
}

// check if we have any sight cover from known enemies at spot
BOOLEAN AnyCoverAtSpot( SOLDIERTYPE *pSoldier, INT32 sSpot )
{
	CHECKF(pSoldier);
	CHECKF(!TileIsOutOfBounds(sSpot));

	INT32 sClosestThreat = NOWHERE;
	INT8 bClosestThreatLevel = 0;
	INT32 iClosestRange = 0x7FFFFFFF;

	// First establish the nearest believed threat using the same planning-only
	// contact model used by the movement/risk system.
	for (UINT32 uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercSlots[uiLoop];
		if (!pOpponent)
			continue;

		INT32 sThreatLoc = NOWHERE;
		INT8 bThreatLevel = 0;
		INT8 bKnowledge = NOT_HEARD_OR_SEEN;
		if (!AIPlanningContactForOpponent(
			pSoldier, pOpponent->ubID, &sThreatLoc, &bThreatLevel, NULL, &bKnowledge))
		{
			continue;
		}

		const BOOLEAN fDirectVisualContact =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;
		if (fDirectVisualContact &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW ||
			 !ValidOpponent(pSoldier, pOpponent)))
		{
			continue;
		}

		if (TileIsOutOfBounds(sThreatLoc))
			continue;

		INT32 iRange = PythSpacesAway(sSpot, sThreatLoc);
		if (iRange < iClosestRange)
		{
			iClosestRange = iRange;
			sClosestThreat = sThreatLoc;
			bClosestThreatLevel = bThreatLevel;
		}
	}

	if (TileIsOutOfBounds(sClosestThreat))
		return FALSE;

	if (!AnyCoverFromSpot(
		sSpot, pSoldier->pathing.bLevel,
		sClosestThreat, bClosestThreatLevel))
	{
		return FALSE;
	}

	for (UINT32 uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercSlots[uiLoop];
		if (!pOpponent)
			continue;

		INT32 sThreatLoc = NOWHERE;
		INT8 bThreatLevel = 0;
		INT8 bKnowledge = NOT_HEARD_OR_SEEN;
		UINT8 ubConfidence = 0;
		if (!AIPlanningContactForOpponent(
			pSoldier, pOpponent->ubID, &sThreatLoc, &bThreatLevel,
			&ubConfidence, &bKnowledge))
		{
			continue;
		}

		const BOOLEAN fThreatStateKnown =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;

		if (fThreatStateKnown &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW ||
			 !ValidOpponent(pSoldier, pOpponent)))
		{
			continue;
		}

		if (TileIsOutOfBounds(sThreatLoc))
			continue;

		INT32 iVisibilityRange;
		if (fThreatStateKnown)
		{
			iVisibilityRange =
				pOpponent->GetMaxDistanceVisible(
					sSpot, pSoldier->pathing.bLevel, CALC_FROM_ALL_DIRS);
		}
		else
		{
			iVisibilityRange = max(1,
				(MAX_VISION_RANGE * (INT32)ubConfidence) / 100);
		}

		if (!AnyCoverFromSpot(
				sSpot, pSoldier->pathing.bLevel,
				sThreatLoc, bThreatLevel) &&
			PythSpacesAway(sSpot, sThreatLoc) <= iVisibilityRange &&
			LocationToLocationLineOfSightTest(
				sThreatLoc, bThreatLevel, sSpot, pSoldier->pathing.bLevel,
				TRUE, iVisibilityRange, STANDING_LOS_POS, PRONE_LOS_POS))
		{
			return FALSE;
		}

		// Predict one tile of movement only for an opponent personally observed now.
		if (fThreatStateKnown && gfTurnBasedAI)
		{
			for (UINT8 ubDirection = 0; ubDirection < NUM_WORLD_DIRECTIONS; ++ubDirection)
			{
				INT32 sTempGridNo =
					NewGridNo(sThreatLoc, DirectionInc(ubDirection));
				if (sTempGridNo == sThreatLoc)
					continue;

				UINT8 ubMovementCost =
					gubWorldMovementCosts[sTempGridNo][ubDirection][bThreatLevel];
				if (ubMovementCost >= TRAVELCOST_BLOCKED ||
					!NewOKDestination(pOpponent, sTempGridNo, FALSE, bThreatLevel))
				{
					continue;
				}

				if (!AnyCoverFromSpot(
						sSpot, pSoldier->pathing.bLevel,
						sTempGridNo, bThreatLevel) &&
					PythSpacesAway(sSpot, sTempGridNo) <= iVisibilityRange &&
					LocationToLocationLineOfSightTest(
						sTempGridNo, bThreatLevel, sSpot, pSoldier->pathing.bLevel,
						TRUE, iVisibilityRange, STANDING_LOS_POS, PRONE_LOS_POS))
				{
					return FALSE;
				}
			}
		}
	}

	return TRUE;
}

BOOLEAN AnyCoverFromSpot( INT32 sSpot, INT8 bLevel, INT32 sThreatLoc, INT8 bThreatLevel )
{
	UINT8	ubDirection;
	INT32	sCoverSpot;
	INT8	bCoverHeight;
	UINT8	ubMovementCost;

	if( TileIsOutOfBounds( sSpot ) || TileIsOutOfBounds(sThreatLoc) )
	{
		return FALSE;
	}

	ubDirection = atan8(CenterX(sSpot), CenterY(sSpot), CenterX(sThreatLoc), CenterY(sThreatLoc));
	sCoverSpot = NewGridNo( sSpot, DirectionInc( ubDirection ) );

	if (sCoverSpot == sSpot || TileIsOutOfBounds(sCoverSpot))
	{
		return FALSE;
	}

	if ( WhoIsThere2( sCoverSpot, bLevel ) != NOBODY )
	{
		return FALSE;
	}

	if (IsLocationSittableExcludingPeople(sCoverSpot, bLevel))
	{
		return FALSE;
	}

	// explosive structure cannot provide cover!
	if (FindStructFlag(sCoverSpot, bLevel, STRUCTURE_EXPLOSIVE))
	{
		return FALSE;
	}

	bCoverHeight = GetTallestStructureHeight( sCoverSpot, bLevel );

	if (bCoverHeight > 0 && StructureDensity(sCoverSpot, bLevel) >= 25)
	{
		return TRUE;
	}

	// check wall/door
	ubMovementCost = gubWorldMovementCosts[sCoverSpot][ubDirection][bLevel];
	if (ubMovementCost >= TRAVELCOST_BLOCKED && !LocationToLocationLineOfSightTest(sSpot, bLevel, sCoverSpot, bLevel, TRUE, NO_DISTANCE_LIMIT, PRONE_LOS_POS, PRONE_LOS_POS))
	{
		return(TRUE);
	}

	return FALSE;
}

// sevenfm: check if suppression is possible (count friends in the fire direction)
BOOLEAN CheckSuppressionDirection( SOLDIERTYPE *pSoldier, INT32 sTargetGridNo, INT8 bTargetLevel )
{
	SOLDIERTYPE * pFriend;
	UINT8 ubShootingDir;
	//UINT8 ubFriendDir;

	CHECKF(pSoldier);

	if(TileIsOutOfBounds(sTargetGridNo))
	{
		return FALSE;
	}

	ubShootingDir = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(sTargetGridNo),CenterY(sTargetGridNo));

	UINT32 uiLoop;

	for (uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		pFriend = MercSlots[ uiLoop ];

		if( pFriend &&
			pFriend != pSoldier &&
			pFriend->bActive &&
			pFriend->stats.bLife >= OKLIFE &&
			(pFriend->bSide == pSoldier->bSide || CONSIDERED_NEUTRAL(pSoldier, pFriend)) &&
			pFriend->pathing.bLevel == pSoldier->pathing.bLevel &&
			pFriend->pathing.bLevel == bTargetLevel &&
			ubShootingDir == atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(pFriend->sGridNo),CenterY(pFriend->sGridNo)) &&
			PythSpacesAway( pSoldier->sGridNo, pFriend->sGridNo) < 2 * DAY_VISION_RANGE &&
			AISoldierToSoldierChanceToGetThrough( pSoldier, pFriend ) > 0 &&
			LocationToLocationLineOfSightTest( pSoldier->sGridNo, pSoldier->pathing.bLevel, pFriend->sGridNo, pFriend->pathing.bLevel, TRUE, NO_DISTANCE_LIMIT) &&
			(gAnimControl[ pFriend->usAnimState ].ubHeight == ANIM_STAND || 
			pSoldier->bTeam == MILITIA_TEAM && pFriend->bTeam == CIV_TEAM && pFriend->aiData.bNeutral ||
			pFriend->bTeam == OUR_TEAM && gAnimControl[ pFriend->usAnimState ].ubHeight != ANIM_PRONE )	)
		{
			return FALSE;
		}
	}

	return TRUE;
}

BOOLEAN AICheckNVG( SOLDIERTYPE *pSoldier )
{
	CHECKF(pSoldier);

	if( Item[pSoldier->inv[HEAD1POS].usItem].nightvisionrangebonus > 0 ||
		Item[pSoldier->inv[HEAD1POS].usItem].cavevisionrangebonus > 0 ||
		Item[pSoldier->inv[HEAD2POS].usItem].nightvisionrangebonus > 0 ||
		Item[pSoldier->inv[HEAD2POS].usItem].cavevisionrangebonus > 0 )
	{
		return TRUE;
	}

	return FALSE;
}

INT8 AIEstimateInterruptLevel( SOLDIERTYPE *pSoldier )
{
	CHECKF(pSoldier);

	INT8 bLevel;

	bLevel = ( 20*EffectiveExpLevel( pSoldier ) + EffectiveAgility( pSoldier, FALSE ) + 15 ) / 30;

	bLevel = __max(0, bLevel - pSoldier->aiData.bShock / 4);

	if ( TANK( pSoldier ) )
	{
		bLevel /= 2;
	}

	return bLevel;
}

INT8 FindMaxEnemyInterruptLevel( SOLDIERTYPE *pSoldier, INT32 sGridNo, INT8 blevel, UINT8 ubDistance )
{
	CHECKF(pSoldier);

	INT8 bMaxInterruptLevel = 0;

	for (UINT32 uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercSlots[uiLoop];
		if (!pOpponent)
			continue;

		INT32 sThreatLoc = NOWHERE;
		INT8 bThreatLevel = 0;
		INT8 bKnowledge = NOT_HEARD_OR_SEEN;
		UINT8 ubConfidence = 0;
		if (!AIPlanningContactForOpponent(
			pSoldier, pOpponent->ubID, &sThreatLoc, &bThreatLevel,
			&ubConfidence, &bKnowledge))
		{
			continue;
		}

		const BOOLEAN fDirectVisualContact =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;

		if (fDirectVisualContact &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 !ValidOpponent(pSoldier, pOpponent) ||
			 pOpponent->IsUnconscious() ||
			 (pOpponent->usSoldierFlagMask & SOLDIER_POW)))
		{
			continue;
		}

		if (TileIsOutOfBounds(sThreatLoc) ||
			PythSpacesAway(sThreatLoc, sGridNo) > ubDistance ||
			bThreatLevel != blevel)
		{
			continue;
		}

		INT8 bInterruptLevel;
		if (fDirectVisualContact)
		{
			bInterruptLevel = AIEstimateInterruptLevel(pOpponent);
		}
		else
		{
			// Reported/stale threats use a neutral competent prior scaled only by
			// communication/contact confidence.
			bInterruptLevel = (INT8)__max(1,
				(6 * (INT32)ubConfidence + 50) / 100);
		}

		if (bInterruptLevel > bMaxInterruptLevel)
			bMaxInterruptLevel = bInterruptLevel;
	}

	return bMaxInterruptLevel;
}

UINT8 CountPublicKnownEnemies( SOLDIERTYPE *pSoldier, INT32 sGridNo, UINT8 ubDistance )
{
	CHECKF(pSoldier);

	UINT8 ubNum = 0;

	for (UINT32 uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercSlots[uiLoop];
		if (!pOpponent)
			continue;

		INT32 sThreatLoc = NOWHERE;
		INT8 bReportedKnowledge = NOT_HEARD_OR_SEEN;

		if (pSoldier->bTeam == ENEMY_TEAM)
		{
			// Enemy "public" counting is now fireteam-local. This restores coordinated
			// force estimates without repopulating the legacy sector-wide public opplist.
			if (!AISharedFireteamOpponentContact(
				pSoldier, pOpponent->ubID, &sThreatLoc, NULL, NULL,
				&bReportedKnowledge))
			{
				continue;
			}
		}
		else
		{
			bReportedKnowledge =
				gbPublicOpplist[pSoldier->bTeam][pOpponent->ubID];
			if (bReportedKnowledge == NOT_HEARD_OR_SEEN)
				continue;
			sThreatLoc =
				gsPublicLastKnownOppLoc[pSoldier->bTeam][pOpponent->ubID];
		}

		const BOOLEAN fDirectVisualContact =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;

		// Current relation/casualty state is legal only under this soldier's own sight.
		if (fDirectVisualContact &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 !ValidOpponent(pSoldier, pOpponent) ||
			 pOpponent->IsUnconscious() ||
			 (pOpponent->usSoldierFlagMask & SOLDIER_POW)))
		{
			continue;
		}

		if (TileIsOutOfBounds(sThreatLoc) ||
			PythSpacesAway(sThreatLoc, sGridNo) > ubDistance)
		{
			continue;
		}

		++ubNum;
	}

	return ubNum;
}

UINT8 CountSeenCovertOpponents( SOLDIERTYPE *pSoldier )
{
	CHECKF(pSoldier);

	UINT8	ubTeamLoop;
	UINT8	ubIDLoop;
	UINT8	cnt = 0;

	for( ubTeamLoop = 0; ubTeamLoop < MAXTEAMS; ubTeamLoop++ )
	{
		if( !gTacticalStatus.Team[ubTeamLoop].bTeamActive )
			continue;

		if( gTacticalStatus.Team[ ubTeamLoop ].bSide != pSoldier->bSide )
		{
			// consider guys in this team, which isn't on our side
			for( ubIDLoop = gTacticalStatus.Team[ ubTeamLoop ].bFirstID; ubIDLoop <= gTacticalStatus.Team[ ubTeamLoop ].bLastID; ubIDLoop++ )
			{
				// check that opponent is covert and we see him currently
				if( MercPtrs[ubIDLoop] &&
					pSoldier->aiData.bOppList[ubIDLoop] == SEEN_CURRENTLY &&
					LOS_Raised(pSoldier, MercPtrs[ubIDLoop], CALC_FROM_ALL_DIRS) > 0 &&
					(MercPtrs[ubIDLoop]->usSoldierFlagMask & (SOLDIER_COVERT_CIV|SOLDIER_COVERT_SOLDIER)) )
				{
					cnt++;
				}
			}
		}
	}

	return cnt;
}

UINT8 AIDirection(INT32 sSpot1, INT32 sSpot2)
{
	if(TileIsOutOfBounds(sSpot1) || TileIsOutOfBounds(sSpot2))
	{
		return DIRECTION_IRRELEVANT;
	}

	return atan8(CenterX(sSpot1),CenterY(sSpot1),CenterX(sSpot2),CenterY(sSpot2));
}

BOOLEAN ValidOpponent(SOLDIERTYPE* pSoldier, SOLDIERTYPE* pOpponent)
{
	if (!pSoldier || !pOpponent)
	{
		return FALSE;
	}

	if (!pOpponent->bActive ||
		!pOpponent->bInSector ||
		pOpponent->stats.bLife <= 0 ||
		CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
		pSoldier->bSide == pOpponent->bSide ||
		pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY ||
		pOpponent->IsEmptyVehicle() ||
		gTacticalStatus.bBoxingState == BOXING && pSoldier->IsBoxer() && !pOpponent->IsBoxer() ||
		pOpponent->ubBodyType == CROW)
	{
		return FALSE;
	}

	return TRUE;
}

BOOLEAN ValidTeamOpponent(INT8 bTeam, SOLDIERTYPE* pOpponent)
{
	if (bTeam >= MAXTEAMS || !pOpponent)
	{
		return FALSE;
	}

	if (!pOpponent->bActive ||
		!pOpponent->bInSector ||
		pOpponent->stats.bLife <= 0 ||
		CONSIDERED_NEUTRAL_TEAM(bTeam, pOpponent) ||
		gTacticalStatus.Team[bTeam].bSide == pOpponent->bSide ||
		pOpponent->IsEmptyVehicle())
	{
		return FALSE;
	}

	return TRUE;
}

INT16 VisionRange(void)
{
	INT16 sDist = MaxNormalDistanceVisible();
	INT8 bLightLevel = GetTimeOfDayAmbientLightLevel();

	sDist = sDist * gGameExternalOptions.ubBrightnessVisionMod[bLightLevel] / 100;

	// Adjust it based on weather...
	if ( guiEnvWeather & ( WEATHER_FORECAST_SHOWERS | WEATHER_FORECAST_THUNDERSHOWERS ) )
	{
		INT16 sWeatherPenalty = 0; // percent vision reduction 0-100%
		sWeatherPenalty = min(gGameExternalOptions.ubVisDistDecreasePerRainIntensity * gbCurrentRainIntensity, 100);

		sDist = (sDist * ( 100 - sWeatherPenalty )) / 100;
	}

	/*if( gfLightningInProgress )
	{
		sDist += sDist * ( ubRealAmbientLightLevel ) / 10;
	}*/

	return sDist;
}

INT16 DayVisionRange(void)
{
	INT16 sDist = MaxNormalDistanceVisible();
	INT8 bLightLevel = NORMAL_LIGHTLEVEL_DAY;

	sDist = sDist * gGameExternalOptions.ubBrightnessVisionMod[bLightLevel] / 100;

	return sDist;
}

INT16 NightVisionRange(void)
{
	INT16 sDist = MaxNormalDistanceVisible();
	INT8 bLightLevel = NORMAL_LIGHTLEVEL_NIGHT;

	sDist = sDist * gGameExternalOptions.ubBrightnessVisionMod[bLightLevel] / 100;

	return sDist;
}

BOOLEAN FindFenceAroundSpot(INT32 sSpot)
{
	if (TileIsOutOfBounds(sSpot))
	{
		return FALSE;
	}

	UINT8 ubDirection;
	INT32 sTempSpot;

	// check adjacent locations
	for (ubDirection = 0; ubDirection < NUM_WORLD_DIRECTIONS; ubDirection++)
	{
		sTempSpot = NewGridNo(sSpot, DirectionInc(ubDirection));

		if (sTempSpot != sSpot && IsCuttableWireFenceAtGridNo(sTempSpot))
		{
			return TRUE;
		}
	}

	return FALSE;
}

// How far around the contact a soldier should work before ending a flank.
// Cunning soldiers deliberately seek a deeper angle; ordinary troops settle for
// a quarter-turn.  Morale, fireteam role and danger can still abort the manoeuvre
// earlier through the higher-level flank logic.
UINT8 MinFlankDirections(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier)
		return 2;

	switch (pSoldier->aiData.bAttitude)
	{
	case CUNNINGAID:
	case CUNNINGSOLO:
		return 4;
	default:
		return 2;
	}
}

UINT8 FlankingDirection(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier)
	{
		return DIRECTION_IRRELEVANT;
	}

	if (!pSoldier->IsFlanking() || TileIsOutOfBounds(pSoldier->lastFlankSpot))
	{
		return DIRECTION_IRRELEVANT;
	}

	UINT8 ubDir = AIDirection(pSoldier->sGridNo, pSoldier->lastFlankSpot);

	// determine desired direction
	if (pSoldier->flags.lastFlankLeft)
	{
		return gTwoCCDirection[ubDir];
	}
	else
	{
		return gTwoCDirection[ubDir];
	}
}

BOOLEAN WeAttack(INT8 bTeam)
{
	if (bTeam >= MAXTEAMS)
	{
		return FALSE;
	}

	if (bTeam != ENEMY_TEAM)
	{
		return FALSE;
	}

	// check that every soldier has SEEKENEMY order
	SOLDIERTYPE * pFriend;

	// Run through each friendly.
	for (UINT8 iCounter = gTacticalStatus.Team[bTeam].bFirstID; iCounter <= gTacticalStatus.Team[bTeam].bLastID; iCounter++)
	{
		pFriend = MercPtrs[iCounter];

		if (pFriend &&
			pFriend->bActive &&
			pFriend->stats.bLife >= OKLIFE &&
			pFriend->aiData.bOrders != SEEKENEMY)
		{
			return FALSE;
		}
	}

	return TRUE;
}

UINT8 CountKnownEnemiesInDirection(SOLDIERTYPE *pSoldier, UINT8 ubDirection, INT16 sDistance, BOOLEAN fAdjacent)
{
	CHECKF(pSoldier);

	UINT8 ubNum = 0;

	for (UINT32 uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercSlots[uiLoop];
		if (!pOpponent)
			continue;

		INT32 sThreatLoc = NOWHERE;
		INT8 bThreatLevel = 0;
		INT8 bKnowledge = NOT_HEARD_OR_SEEN;
		if (!AIPlanningContactForOpponent(
			pSoldier, pOpponent->ubID, &sThreatLoc, &bThreatLevel, NULL, &bKnowledge))
		{
			continue;
		}

		const BOOLEAN fDirectVisualContact =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;

		if (fDirectVisualContact &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW ||
			 !ValidOpponent(pSoldier, pOpponent)))
		{
			continue;
		}

		if (TileIsOutOfBounds(sThreatLoc) ||
			PythSpacesAway(pSoldier->sGridNo, sThreatLoc) > sDistance)
		{
			continue;
		}

		UINT8 ubThreatDirection = AIDirection(pSoldier->sGridNo, sThreatLoc);
		if (ubThreatDirection != ubDirection &&
			(!fAdjacent ||
			 (ubThreatDirection != gOneCDirection[ubDirection] &&
			  ubThreatDirection != gOneCCDirection[ubDirection])))
		{
			continue;
		}

		++ubNum;
	}

	return ubNum;
}

INT8 Knowledge(SOLDIERTYPE *pSoldier, UINT8 ubOpponentID)
{
	if (!pSoldier || ubOpponentID == NOBODY)
	{
		return NOT_HEARD_OR_SEEN;
	}

	if (UsePersonalKnowledge(pSoldier, ubOpponentID))
	{
		return PersonalKnowledge(pSoldier, ubOpponentID);
	}

	return PublicKnowledge(pSoldier->bTeam, ubOpponentID);
}

INT32 KnownLocation(SOLDIERTYPE *pSoldier, UINT8 ubOpponentID)
{
	if (!pSoldier || ubOpponentID == NOBODY)
	{
		return NOWHERE;
	}

	if (UsePersonalKnowledge(pSoldier, ubOpponentID))
	{
		return KnownPersonalLocation(pSoldier, ubOpponentID);
	}

	return KnownPublicLocation(pSoldier->bTeam, ubOpponentID);
}

INT8 KnownLevel(SOLDIERTYPE *pSoldier, UINT8 ubOpponentID)
{
	if (!pSoldier || ubOpponentID == NOBODY)
	{
		return 0;
	}

	if (UsePersonalKnowledge(pSoldier, ubOpponentID))
	{
		return KnownPersonalLevel(pSoldier, ubOpponentID);
	}

	return KnownPublicLevel(pSoldier->bTeam, ubOpponentID);
}

BOOLEAN UsePersonalKnowledge(SOLDIERTYPE *pSoldier, UINT8 ubOpponentID)
{
	INT8		bPersonalKnowledge;
	INT8		bPublicKnowledge;

	if (!pSoldier || ubOpponentID == NOBODY)
	{
		return FALSE;
	}

	bPersonalKnowledge = PersonalKnowledge(pSoldier, ubOpponentID);
	bPublicKnowledge = PublicKnowledge(pSoldier->bTeam, ubOpponentID);

	if (gubKnowledgeValue[bPublicKnowledge - OLDEST_HEARD_VALUE][bPersonalKnowledge - OLDEST_HEARD_VALUE] > 0 ||
		bPersonalKnowledge != NOT_HEARD_OR_SEEN &&
		TileIsOutOfBounds(KnownPublicLocation(pSoldier->bTeam, ubOpponentID)) &&
		!TileIsOutOfBounds(KnownPersonalLocation(pSoldier, ubOpponentID)))
	{
		return TRUE;
	}

	return FALSE;
}

INT8 PersonalKnowledge(SOLDIERTYPE *pSoldier, UINT8 ubOpponentID)
{
	if (!pSoldier || ubOpponentID == NOBODY)
	{
		return NOT_HEARD_OR_SEEN;
	}

	return pSoldier->aiData.bOppList[ubOpponentID];
}

INT32 KnownPersonalLocation(SOLDIERTYPE *pSoldier, UINT8 ubOpponentID)
{
	if (!pSoldier || ubOpponentID == NOBODY)
	{
		return NOWHERE;
	}
	/*if(PersonalKnowledge(pSoldier, ubOpponentID) == NOT_HEARD_OR_SEEN)
	{
	return NOWHERE;
	}*/

	return gsLastKnownOppLoc[pSoldier->ubID][ubOpponentID];
}

INT8 KnownPersonalLevel(SOLDIERTYPE *pSoldier, UINT8 ubOpponentID)
{
	if (!pSoldier || ubOpponentID == NOBODY)
	{
		return 0;
	}

	return gbLastKnownOppLevel[pSoldier->ubID][ubOpponentID];
}

INT8 PublicKnowledge(UINT8 bTeam, UINT8 ubOpponentID)
{
	if (bTeam >= MAXTEAMS || ubOpponentID == NOBODY)
	{
		return NOT_HEARD_OR_SEEN;
	}

	return gbPublicOpplist[bTeam][ubOpponentID];
}

INT32 KnownPublicLocation(UINT8 bTeam, UINT8 ubOpponentID)
{
	if (bTeam >= MAXTEAMS || ubOpponentID == NOBODY)
	{
		return NOWHERE;
	}
	/*if (PublicKnowledge(bTeam, ubOpponentID) == NOT_HEARD_OR_SEEN)
	{
	return NOWHERE;
	}*/

	return gsPublicLastKnownOppLoc[bTeam][ubOpponentID];
}

INT8 KnownPublicLevel(UINT8 bTeam, UINT8 ubOpponentID)
{
	if (bTeam >= MAXTEAMS || ubOpponentID == NOBODY)
	{
		return 0;
	}

	return gbPublicLastKnownOppLevel[bTeam][ubOpponentID];
}

// check that current loaded sector is town
BOOLEAN AICheckTown(void)
{
	// determine sector name
	UINT8 ubTownID = GetTownIdForSector(gWorldSectorX, gWorldSectorY);

	// not underground
	if (gbWorldSectorZ > 0)
	{
		return FALSE;
	}
	// check town sector
	if (ubTownID == BLANK_SECTOR)
	{
		return FALSE;
	}

	return TRUE;
}

UINT8 AISectorType(void)
{
	UINT8	ubSectorID = SECTOR(gWorldSectorX, gWorldSectorY);
	SECTORINFO *pSector = &SectorInfo[ubSectorID];
	UINT8	ubSectorType = PLAINS;
	if (pSector)
	{
		ubSectorType = pSector->ubTraversability[THROUGH_STRATEGIC_MOVE];
	}
	return ubSectorType;
}

// check that current loaded sector is underground
BOOLEAN AICheckUnderground(void)
{
	// not underground
	if (gbWorldSectorZ > 0)
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN NorthSpot(INT32 sSpot, INT8 bLevel)
{
	if (TileIsOutOfBounds(sSpot))
		return FALSE;

	if (bLevel > 0)
		return FALSE;

	INT32 sNextSpot = NewGridNo(sSpot, DirectionInc(NORTHWEST));

	if (gubWorldMovementCosts[sSpot + DirectionInc(NORTHWEST)][NORTHWEST][0] == TRAVELCOST_OFF_MAP ||
		gubWorldMovementCosts[sNextSpot + DirectionInc(NORTHWEST)][NORTHWEST][0] == TRAVELCOST_OFF_MAP)
	{
		return TRUE;
	}

	return FALSE;
}

UINT8 TerrainDensity(INT32 sSpot, INT8 bLevel, UINT8 ubDistance, BOOLEAN fGrass)
{
	if (TileIsOutOfBounds(sSpot))
		return 0;

	INT16 sMaxLeft = min(ubDistance, (sSpot % MAXCOL));
	INT16 sMaxRight = min(ubDistance, MAXCOL - ((sSpot % MAXCOL) + 1));
	INT16 sMaxUp = min(ubDistance, (sSpot / MAXROW));
	INT16 sMaxDown = min(ubDistance, MAXROW - ((sSpot / MAXROW) + 1));
	INT32 sCountSpots = 0;
	INT32 sCountObstacles = 0;

	for (INT16 sYOffset = -sMaxUp; sYOffset <= sMaxDown; ++sYOffset)
	{
		for (INT16 sXOffset = -sMaxLeft; sXOffset <= sMaxRight; ++sXOffset)
		{
			INT32 sCheckSpot = sSpot + sXOffset + MAXCOL * sYOffset;
			if (TileIsOutOfBounds(sCheckSpot))
				continue;

			UINT16 usRoom1 = 0;
			UINT16 usRoom2 = 0;
			if (InARoom(sSpot, &usRoom1) != InARoom(sCheckSpot, &usRoom2) ||
				usRoom1 != usRoom2)
			{
				continue;
			}

			++sCountSpots;

			if (!IsLocationSittableExcludingPeople(sCheckSpot, bLevel))
			{
				++sCountObstacles;
				continue;
			}

			if (fGrass)
			{
				STRUCTURE *pCurrent = gpWorldLevelData[sCheckSpot].pStructureHead;
				INT16 sDesiredLevel = (bLevel > 0) ? STRUCTURE_ON_ROOF : STRUCTURE_ON_GROUND;

				if (pCurrent != NULL &&
					pCurrent->sCubeOffset == sDesiredLevel &&
					pCurrent->pDBStructureRef->pDBStructure->ubArmour == 4)
				{
					++sCountObstacles;
				}
			}
		}
	}

	return (sCountSpots > 0) ? (UINT8)(100 * sCountObstacles / sCountSpots) : 0;
}

UINT8 CountObstaclesNearSpot(INT32 sSpot, INT8 bLevel)
{
	if (TileIsOutOfBounds(sSpot))
	{
		return 0;
	}
	/*if (!IsLocationSittableExcludingPeople(sSpot, bLevel))
	{
	return FALSE;
	}*/

	UINT8	ubMovementCost;
	INT32	sTempGridNo;
	UINT8	ubDirection;
	UINT8	ubCount = 0;

	// check adjacent reachable tiles
	for (ubDirection = 0; ubDirection < NUM_WORLD_DIRECTIONS; ubDirection++)
	{
		sTempGridNo = NewGridNo(sSpot, DirectionInc(ubDirection));

		if (sTempGridNo != sSpot)
		{
			ubMovementCost = gubWorldMovementCosts[sTempGridNo][ubDirection][bLevel];

			if (ubMovementCost >= TRAVELCOST_BLOCKED || !IsLocationSittableExcludingPeople(sTempGridNo, bLevel))
			{
				ubCount++;
			}
		}
	}

	return ubCount;
}

BOOLEAN FindShadowAtSpot(INT32 sSpot, INT8 bLevel)
{
	if (TileIsOutOfBounds(sSpot))
	{
		return FALSE;
	}

	INT32 sNewGridNo = NewGridNo(sSpot, (UINT16)DirectionInc(NORTH));
	if (sNewGridNo != sSpot)
	{
		STRUCTURE* pStructure = gpWorldLevelData[sNewGridNo].pStructureHead;

		if (pStructure != NULL && StructureHeight(pStructure) > 1)
		{
			LEVELNODE* pShadowNode = NULL;

			if (!(pStructure->fFlags & STRUCTURE_BASE_TILE))
			{
				STRUCTURE* pBaseStructure = FindBaseStructure(pStructure);
				if (pBaseStructure != NULL)
				{
					pShadowNode = gpWorldLevelData[pBaseStructure->sGridNo].pShadowHead;
				}
			}
			else
			{
				pShadowNode = gpWorldLevelData[sNewGridNo].pShadowHead;
			}

			if (pShadowNode != NULL)
				return TRUE;
		}
	}

	return FALSE;
}

BOOLEAN AllowDeepWaterFlanking(SOLDIERTYPE *pSoldier)
{
	if (SoldierAI(pSoldier) &&
		AICombatTeam(pSoldier) &&
		pSoldier->aiData.bOrders == SEEKENEMY &&
		(pSoldier->aiData.bAttitude == CUNNINGSOLO || gGameOptions.fNewTraitSystem && HAS_SKILL_TRAIT(pSoldier, ATHLETICS_NT)) &&
		pSoldier->aiData.bAlertStatus >= STATUS_RED &&
		!pSoldier->aiData.bUnderFire &&
		!GuySawEnemy(pSoldier))
	{
		return TRUE;
	}

	return FALSE;
}

INT32	RandomizeLocation(INT32 sSpot, INT8 bLevel, UINT8 ubTimes, SOLDIERTYPE *pSightSoldier)
{
	if (TileIsOutOfBounds(sSpot))
	{
		return NOWHERE;
	}

	UINT8 ubDirection;
	UINT8 ubMovementCost;
	INT32 sTempSpot;
	INT32 sSpotArray[NUM_WORLD_DIRECTIONS + 1];
	UINT8 ubSpots;

	for (UINT8 ubCnt = 0; ubCnt < ubTimes; ubCnt++)
	{
		// store original location
		ubSpots = 1;
		sSpotArray[0] = sSpot;

		// find adjacent locations
		for (ubDirection = 0; ubDirection < NUM_WORLD_DIRECTIONS; ubDirection++)
		{
			sTempSpot = NewGridNo(sSpot, DirectionInc(ubDirection));

			if (sTempSpot != sSpot)
			{
				ubMovementCost = gubWorldMovementCosts[sTempSpot][ubDirection][bLevel];

				if (ubMovementCost < TRAVELCOST_BLOCKED &&
					IsLocationSittableExcludingPeople(sTempSpot, bLevel) &&
					(!pSightSoldier || SoldierToVirtualSoldierLineOfSightTest(pSightSoldier, sTempSpot, bLevel, ANIM_STAND, TRUE, NO_DISTANCE_LIMIT)))
				{
					sSpotArray[ubSpots] = sTempSpot;
					ubSpots++;
				}
			}
		}
		// find random location
		sSpot = sSpotArray[Random(ubSpots)];
		// stop if could not find any adjacent spot
		if (ubSpots < 2)
		{
			break;
		}
	}

	return sSpot;
}

INT32	RandomizeOpponentLocation(INT32 sSpot, SOLDIERTYPE *pOpponent, INT16 sMaxDistance)
{
	if (TileIsOutOfBounds(sSpot))
	{
		return NOWHERE;
	}

	INT8 bXOffset, bYOffset;
	INT32 sRandomSpot;

	if (sMaxDistance > 0)
	{
		for (INT cnt = 0; cnt < min(sMaxDistance * 2, 100); cnt++)
		{
			bXOffset = Random(sMaxDistance * 2 + 1) - sMaxDistance;
			bYOffset = Random(sMaxDistance * 2 + 1) - sMaxDistance;

			sRandomSpot = sSpot + bXOffset + (MAXCOL * bYOffset);

			if (!TileIsOutOfBounds(sRandomSpot) && NewOKDestination(pOpponent, sRandomSpot, FALSE, pOpponent->pathing.bLevel))
			{
				return sRandomSpot;
			}
		}
	}

	return sSpot;
}

BOOLEAN InSmoke(INT32 sGridNo, INT8 bLevel)
{
	if (TileIsOutOfBounds(sGridNo))
	{
		return FALSE;
	}

	if (gpWorldLevelData[sGridNo].ubExtFlags[bLevel] & (MAPELEMENT_EXT_SMOKE))
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN AICheckSpecialRole(SOLDIERTYPE *pSoldier)
{
	if (AICheckIsSniper(pSoldier) || AICheckIsMachinegunner(pSoldier) || AICheckIsMortarOperator(pSoldier) || AICheckIsRadioOperator(pSoldier) || AICheckIsCommander(pSoldier) || AICheckIsGLOperator(pSoldier))
		return TRUE;

	return FALSE;
}

BOOLEAN SafeSpot(SOLDIERTYPE *pSoldier, INT32 sSpot)
{
	if (!pSoldier)
		return FALSE;

	if (sSpot == NOWHERE)
		sSpot = pSoldier->sGridNo;

	if (TileIsOutOfBounds(sSpot))
		return FALSE;

	INT8 bLevel = pSoldier->pathing.bLevel;
	BOOLEAN fUnlimitedSightCover = SightCoverAtSpot(pSoldier, sSpot, TRUE);
	BOOLEAN fProneSightCover = ProneSightCoverAtSpot(pSoldier, sSpot, FALSE);
	BOOLEAN fAnyCover = AnyCoverAtSpot(pSoldier, sSpot);

	BOOLEAN fDefensible =
		fUnlimitedSightCover ||
		(fProneSightCover && fAnyCover) ||
		(InARoom(sSpot, NULL) && bLevel == 0 && (fAnyCover || fProneSightCover));

	if (!fDefensible)
		return FALSE;

	if (pSoldier->aiData.bUnderFire)
		return FALSE;

	// Use the same environmental danger model as movement selection.  A position
	// is not a true safe spot merely because it has cover if it is in gas, water,
	// red smoke, dangerous light, beside explosive scenery, near a known bomb, or
	// next to a fresh casualty the soldier can actually perceive.
	if (Water(sSpot, bLevel) || SpotDangerLevel(pSoldier, sSpot) > 0)
		return FALSE;

	if (AICorpseWarningKnown(pSoldier, sSpot, bLevel) > 0)
		return FALSE;

	return TRUE;
}

BOOLEAN AbortFinalSpot(SOLDIERTYPE *pSoldier, INT32 sSpot, INT8 bAction, INT32 sClosestDisturbance, INT8 bDisturbanceLevel, INT32& sDangerousSpot)
{
	sDangerousSpot = NOWHERE;

	if (!pSoldier || TileIsOutOfBounds(sSpot))
	{
		return FALSE;
	}	

	INT8	bOpponentLevel;
	INT32	sClosestOpponent = AIPrimaryPlanningThreatSpot(pSoldier, &bOpponentLevel);

	if (TileIsOutOfBounds(sClosestDisturbance))
	{
		sClosestDisturbance = sClosestOpponent;
		bDisturbanceLevel = bOpponentLevel;
	}

	if (TileIsOutOfBounds(sClosestDisturbance))
	{
		return FALSE;
	}

	INT8	bLevel = pSoldier->pathing.bLevel;
	BOOLEAN fSeekEnemy = (pSoldier->aiData.bOrders == SEEKENEMY);
	BOOLEAN fFlankingFriends = (CountFriendsFlankSameSpot(pSoldier, sClosestDisturbance) > 0);
	BOOLEAN fSuccessfulAttack = AICheckSuccessfulAttack(pSoldier, TRUE);
	BOOLEAN fFriendsBlack = (CountFriendsBlack(pSoldier, sClosestDisturbance) > 0);
	BOOLEAN fSafeSpot = SafeSpot(pSoldier);
	BOOLEAN fSightCover = SightCoverAtSpot(pSoldier, pSoldier->sGridNo, FALSE);

	// abort seek if we see bomb
	if (FindBombNearby(pSoldier, sSpot, BOMB_DETECTION_RANGE))
	{
		DebugAI(AI_MSG_INFO, pSoldier, String("found bomb! abort!"));
		return TRUE;
	}

	// abort if moving into red smoke
	if (RedSmokeDanger(sSpot, pSoldier->pathing.bLevel) &&
		!RedSmokeDanger(pSoldier->sGridNo, pSoldier->pathing.bLevel))
	{
		DebugAI(AI_MSG_INFO, pSoldier, String("moving into red smoke! abort!"));
		return TRUE;
	}

	// Do not choose a nominally useful destination that introduces a new
	// environmental mobility hazard. Leaving an existing hazard is handled by
	// the dedicated water/gas escape logic before ordinary RED movement.
	if ((InGas(pSoldier, sSpot) && !InGas(pSoldier, pSoldier->sGridNo)) ||
		(DeepWater(sSpot, pSoldier->pathing.bLevel) && !DeepWater(pSoldier->sGridNo, pSoldier->pathing.bLevel)))
	{
		DebugAI(AI_MSG_INFO, pSoldier, String("hazardous destination! abort!"));
		sDangerousSpot = sSpot;
		return TRUE;
	}

	// don't go into light at night (includes smoke check)
	if (InLightAtNight(sSpot, bLevel) &&
		!InLightAtNight(pSoldier->sGridNo, pSoldier->pathing.bLevel) &&
		!InSmoke(sSpot, bLevel) &&
		(pSoldier->aiData.bUnderFire || !fSeekEnemy || !fSightCover || AICorpseWarningKnown(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel) > 0) &&
		(fFlankingFriends || !fSuccessfulAttack || !fSeekEnemy) &&
		!fFriendsBlack)
	{
		DebugAI(AI_MSG_INFO, pSoldier, String("in light at night! abort!"));
		sDangerousSpot = sSpot;
		return TRUE;
	}

	// abort seeking when soldier sees fresh corpse
	if (fSafeSpot &&
		AICorpseWarningKnown(pSoldier, sSpot, bLevel) &&
		!InSmoke(sSpot, bLevel) &&
		!fFriendsBlack &&
		(fFlankingFriends || !fSuccessfulAttack || !fSeekEnemy ||
		 EnemyCanAttackSpot(pSoldier, sSpot, bLevel) ||
		 (InARoom(sSpot, NULL) && bLevel == 0)))
	{
		DebugAI(AI_MSG_INFO, pSoldier, String("fresh corpse! abort!"));

		if (!SightCoverAtSpot(pSoldier, sSpot, TRUE))
		{
			sDangerousSpot = sSpot;
		}
		return TRUE;
	}

	/*	
	BOOLEAN fSightCoverUnlimited = SightCoverAtSpot(pSoldier, pSoldier->sGridNo, ANIM_STAND, TRUE);
	BOOLEAN fProneSightCover = SightCoverAtSpot(pSoldier, pSoldier->sGridNo, ANIM_PRONE, FALSE);
	BOOLEAN fProneSightCoverUnlimited = SightCoverAtSpot(pSoldier, pSoldier->sGridNo, ANIM_PRONE, TRUE);
	BOOLEAN fAnyCover = AnyCoverAtSpot(pSoldier, pSoldier->sGridNo);
	BOOLEAN fDangerousSpot = DangerousSpot(pSoldier);	
	BOOLEAN fTerrainCover = TerrainCoverAtSpot(pSoldier, sSpot, pSoldier->pathing.bLevel);
	BOOLEAN fUnderAttack = AICheckSpotUnderAttack(pSoldier, pSoldier->sGridNo, FALSE);
	BOOLEAN fUnderSuccessfulAttack = AICheckSpotUnderAttack(pSoldier, pSoldier->sGridNo, TRUE);
	BOOLEAN fWeOutnumber = AICheckWeOutnumber(pSoldier, sClosestDisturbance);
	BOOLEAN fRushAttack = pSoldier->RushAttackAdvance();
	BOOLEAN fRetreat = pSoldier->RetreatActive();		
	BOOLEAN fTakenLargeHit = pSoldier->TakenLargeHit();	
	BOOLEAN fCanAttackEnemy = CanAttackEnemy(pSoldier, ANIM_STAND, FALSE);
	BOOLEAN fInARoom = (InARoom(pSoldier->sGridNo, NULL) && bLevel == 0);
	BOOLEAN fWeAttack = WeAttack(pSoldier->bTeam);
	INT8	bMorale = pSoldier->aiData.bAIMorale;
	*/
	return FALSE;
}

// needs prepared path before calling this function
BOOLEAN AbortPath(SOLDIERTYPE *pSoldier, INT8 bAction, INT32 sClosestDisturbance, INT8 bDisturbanceLevel, INT32& sDangerousSpot, INT32 &sLastSafeSpot)
{
	sDangerousSpot = NOWHERE;
	sLastSafeSpot = NOWHERE;

	if (!pSoldier)
	{
		return FALSE;
	}

	INT8	bOpponentLevel;
	INT32	sClosestOpponent = AIPrimaryPlanningThreatSpot(pSoldier, &bOpponentLevel);

	if (TileIsOutOfBounds(sClosestDisturbance))
	{
		sClosestDisturbance = sClosestOpponent;
		bDisturbanceLevel = bOpponentLevel;
	}

	if (TileIsOutOfBounds(sClosestDisturbance))
	{
		return FALSE;
	}

	INT8 bLevel = pSoldier->pathing.bLevel;
	BOOLEAN fSeekEnemy = (pSoldier->aiData.bOrders == SEEKENEMY);
	BOOLEAN fFlankingFriends = (CountFriendsFlankSameSpot(pSoldier, sClosestDisturbance) > 0);
	BOOLEAN fFriendsBlack = (CountFriendsBlack(pSoldier, sClosestDisturbance) > 0);
	BOOLEAN fSuccessfulAttack = AICheckSuccessfulAttack(pSoldier, TRUE);
	BOOLEAN fSafeSpot = SafeSpot(pSoldier);

	INT16 sLoop;
	INT32 sCheckGridNo = pSoldier->sGridNo;	

	for (sLoop = pSoldier->pathing.usPathIndex; sLoop < pSoldier->pathing.usPathDataSize; sLoop++)
	{
		sCheckGridNo = NewGridNo(sCheckGridNo, DirectionInc((UINT8)(pSoldier->pathing.usPathingData[sLoop])));

		// sevenfm: don't check fences
		if (IsJumpableFencePresentAtGridNo(sCheckGridNo))
		{
			continue;
		}

		// Reject paths that cross hazards even when the final destination itself
		// is safe. Legacy Vengeance only validated the endpoint, so a seek/help/
		// cover route could walk through a bomb, red smoke, gas or deep water.
		if (FindBombNearby(pSoldier, sCheckGridNo, BOMB_DETECTION_RANGE) ||
			(RedSmokeDanger(sCheckGridNo, bLevel) && !RedSmokeDanger(pSoldier->sGridNo, bLevel)) ||
			(InGas(pSoldier, sCheckGridNo) && !InGas(pSoldier, pSoldier->sGridNo)) ||
			(DeepWater(sCheckGridNo, bLevel) && !DeepWater(pSoldier->sGridNo, bLevel)))
		{
			DebugAI(AI_MSG_INFO, pSoldier, String("hazard on movement path! abort!"));
			sDangerousSpot = sCheckGridNo;
			return TRUE;
		}

		// don't go into light at night (includes smoke check)
		if (InLightAtNight(sCheckGridNo, bLevel) &&
			!InLightAtNight(pSoldier->sGridNo, pSoldier->pathing.bLevel) &&
			!InSmoke(sCheckGridNo, bLevel) &&
			(pSoldier->aiData.bUnderFire || !fSeekEnemy || !SightCoverAtSpot(pSoldier, sCheckGridNo, FALSE) || AICorpseWarningKnown(pSoldier, sCheckGridNo, bLevel) > 0) &&
			(fFlankingFriends || !fSuccessfulAttack || !fSeekEnemy) &&
			!fFriendsBlack)
		{
			DebugAI(AI_MSG_INFO, pSoldier, String("in light at night! abort!"));
			sDangerousSpot = sCheckGridNo;
			return TRUE;
		}

		// check for fresh corpses
		if (fSafeSpot &&
			AICorpseWarningKnown(pSoldier, sCheckGridNo, pSoldier->pathing.bLevel) &&
			!InSmoke(sCheckGridNo, pSoldier->pathing.bLevel) &&
			!fFriendsBlack &&
			(fFlankingFriends || !fSuccessfulAttack || !fSeekEnemy ||
			 EnemyCanAttackSpot(pSoldier, sCheckGridNo, bLevel) ||
			 (InARoom(sCheckGridNo, NULL) && bLevel == 0)))
		{
			DebugAI(AI_MSG_INFO, pSoldier, String("fresh corpse! abort!"));

			if (!SightCoverAtSpot(pSoldier, sCheckGridNo, TRUE) ||
				(InARoom(sCheckGridNo, NULL) && bLevel == 0))
			{
				sDangerousSpot = sCheckGridNo;
			}
			return TRUE;
		}

		// Preserve partial progress for callers that can stop short rather than
		// discard an otherwise safe approach when a later tile becomes dangerous.
		if (sCheckGridNo != pSoldier->sGridNo)
		{
			sLastSafeSpot = sCheckGridNo;
		}
	}

	/*	
	BOOLEAN fSightCover = SightCoverAtSpot(pSoldier, pSoldier->sGridNo, ANIM_STAND, FALSE);
	BOOLEAN fSightCoverUnlimited = SightCoverAtSpot(pSoldier, pSoldier->sGridNo, ANIM_STAND, TRUE);
	BOOLEAN fProneSightCover = SightCoverAtSpot(pSoldier, pSoldier->sGridNo, ANIM_PRONE, FALSE);
	BOOLEAN fProneSightCoverUnlimited = SightCoverAtSpot(pSoldier, pSoldier->sGridNo, ANIM_PRONE, TRUE);

	BOOLEAN fAnyCover = AnyCoverAtSpot(pSoldier, pSoldier->sGridNo);
	BOOLEAN fDangerousSpot = DangerousSpot(pSoldier);	
	
	BOOLEAN fInARoom = (InARoom(pSoldier->sGridNo, NULL) && bLevel == 0);

	BOOLEAN fWeAttack = WeAttack(pSoldier->bTeam);
	INT8	bMorale = pSoldier->aiData.bAIMorale;
	*/

	return FALSE;
}

// Same battlefield-warning intent as AICorpseWarningKnown(), but restricted to corpses
// this soldier can actually perceive.  This prevents unseen casualties elsewhere
// in the sector from leaking into movement, support and morale decisions.
UINT8 AICorpseWarningKnown(SOLDIERTYPE *pSoldier, INT32 sGridNo, INT8 bLevel)
{
	if (!pSoldier || TileIsOutOfBounds(sGridNo))
		return 0;

	UINT8 ubWarning = 0;
	for (INT32 cnt = 0; cnt < giNumRottingCorpse; ++cnt)
	{
		ROTTING_CORPSE *pCorpse = &(gRottingCorpse[cnt]);
		if (!pCorpse ||
			!pCorpse->fActivated ||
			pCorpse->def.ubType >= ROTTING_STAGE2 ||
			pCorpse->def.ubBodyType > REGFEMALE ||
			pCorpse->def.ubAIWarningValue <= ubWarning ||
			pCorpse->def.bLevel != bLevel ||
			TileIsOutOfBounds(pCorpse->def.sGridNo) ||
			PythSpacesAway(sGridNo, pCorpse->def.sGridNo) > CORPSE_WARNING_DIST)
		{
			continue;
		}

		if (!(pSoldier->bTeam == ENEMY_TEAM && CorpseEnemyTeam(pCorpse) ||
			  pSoldier->bTeam == MILITIA_TEAM && CorpseMilitiaTeam(pCorpse) ||
			  pSoldier->bTeam != ENEMY_TEAM && pSoldier->bTeam != MILITIA_TEAM))
		{
			continue;
		}

		if (!SoldierToVirtualSoldierLineOfSightTest(
			pSoldier, pCorpse->def.sGridNo, pCorpse->def.bLevel,
			ANIM_PRONE, TRUE, CALC_FROM_ALL_DIRS))
		{
			continue;
		}

		ubWarning = pCorpse->def.ubAIWarningValue;
	}

	return ubWarning;
}

BOOLEAN CorpseWarning(SOLDIERTYPE *pSoldier, INT32 sGridNo, INT8 bLevel)
{
	CHECKF(pSoldier);

	INT32			cnt;
	ROTTING_CORPSE *pCorpse;
	UINT8			ubDistance = CORPSE_WARNING_DIST;
	UINT8			ubWarning = 0;

	for (cnt = 0; cnt < giNumRottingCorpse; ++cnt)
	{
		pCorpse = &(gRottingCorpse[cnt]);

		if (pCorpse &&
			pCorpse->fActivated &&
			pCorpse->def.ubType < ROTTING_STAGE2 &&
			pCorpse->def.ubBodyType <= REGFEMALE &&
			pCorpse->def.ubAIWarningValue > ubWarning &&
			pCorpse->def.bLevel == bLevel &&
			!TileIsOutOfBounds(pCorpse->def.sGridNo) &&
			PythSpacesAway(sGridNo, pCorpse->def.sGridNo) <= ubDistance &&
			(pSoldier->bTeam == ENEMY_TEAM && CorpseEnemyTeam(pCorpse) || pSoldier->bTeam == MILITIA_TEAM && CorpseMilitiaTeam(pCorpse) || pSoldier->bTeam != ENEMY_TEAM && pSoldier->bTeam != MILITIA_TEAM))
			//(pSoldier->bTeam == ENEMY_TEAM && CorpseEnemyTeam(pCorpse) || pSoldier->bTeam == MILITIA_TEAM && CorpseMilitiaTeam(pCorpse) || pSoldier->bTeam == CIV_TEAM && !pSoldier->aiData.bNeutral))
		{
			ubWarning = pCorpse->def.ubAIWarningValue;
		}
	}

	return ubWarning;
}

INT32	CountCorpses(SOLDIERTYPE *pSoldier, INT32 sSpot, INT16 sDistance, BOOLEAN fCheckSight, BOOLEAN fFresh)
{
	CHECKF(pSoldier);

	INT32			cnt;
	ROTTING_CORPSE *pCorpse;
	BOOLEAN			fCorpseOFAlly;
	UINT16			usNum = 0;

	for (cnt = 0; cnt < giNumRottingCorpse; ++cnt)
	{
		pCorpse = &(gRottingCorpse[cnt]);

		if (pCorpse &&
			pCorpse->fActivated &&
			pCorpse->def.ubType < ROTTING_STAGE2 &&
			pCorpse->def.ubBodyType <= REGFEMALE &&
			(!fFresh || pCorpse->def.ubAIWarningValue > 0) &&
			!TileIsOutOfBounds(pCorpse->def.sGridNo) &&
			PythSpacesAway(sSpot, pCorpse->def.sGridNo) <= sDistance &&
			(pSoldier->bTeam == ENEMY_TEAM && CorpseEnemyTeam(pCorpse) || pSoldier->bTeam == MILITIA_TEAM && CorpseMilitiaTeam(pCorpse) || pSoldier->bTeam != ENEMY_TEAM && pSoldier->bTeam != MILITIA_TEAM) &&
			(!fCheckSight || SoldierToVirtualSoldierLineOfSightTest(pSoldier, pCorpse->def.sGridNo, pCorpse->def.bLevel, ANIM_PRONE, TRUE, CALC_FROM_ALL_DIRS)))
		{
			usNum++;
		}
	}

	return usNum;
}

INT32	CountCorpsesInDirection(SOLDIERTYPE *pSoldier, INT32 sSpot, UINT8 ubDirection, INT16 sDistance, BOOLEAN fCheckSight, BOOLEAN fFresh)
{

	INT32			cnt;
	ROTTING_CORPSE *pCorpse;
	BOOLEAN			fCorpseOFAlly;
	UINT16			usNum = 0;

	for (cnt = 0; cnt < giNumRottingCorpse; ++cnt)
	{
		pCorpse = &(gRottingCorpse[cnt]);

		if (pCorpse &&
			pCorpse->fActivated &&
			pCorpse->def.ubType < ROTTING_STAGE2 &&
			pCorpse->def.ubBodyType <= REGFEMALE &&
			(!fFresh || pCorpse->def.ubAIWarningValue > 0) &&
			!TileIsOutOfBounds(pCorpse->def.sGridNo) &&
			PythSpacesAway(sSpot, pCorpse->def.sGridNo) <= sDistance &&
			(pSoldier->bTeam == ENEMY_TEAM && CorpseEnemyTeam(pCorpse) || pSoldier->bTeam == MILITIA_TEAM && CorpseMilitiaTeam(pCorpse) || pSoldier->bTeam != ENEMY_TEAM && pSoldier->bTeam != MILITIA_TEAM) &&
			(!fCheckSight || SoldierToVirtualSoldierLineOfSightTest(pSoldier, pCorpse->def.sGridNo, pCorpse->def.bLevel, ANIM_PRONE, TRUE, CALC_FROM_ALL_DIRS)))
		{
			usNum++;
		}
	}

	return usNum;
}

BOOLEAN CorpseEnemyTeam(ROTTING_CORPSE *pCorpse)
{
	CHECKF(pCorpse);

	// check whether corpse has soldier's uniform
	for (UINT8 i = UNIFORM_ENEMY_ADMIN; i <= UNIFORM_ENEMY_ELITE; ++i)
	{
		if (COMPARE_PALETTEREP_ID(pCorpse->def.VestPal, gUniformColors[i].vest) && COMPARE_PALETTEREP_ID(pCorpse->def.PantsPal, gUniformColors[i].pants))
		{
			return TRUE;
		}
	}

	return FALSE;
}

BOOLEAN CorpseMilitiaTeam(ROTTING_CORPSE *pCorpse)
{
	CHECKF(pCorpse);

	// check whether corpse has soldier's uniform
	for (UINT8 i = UNIFORM_MILITIA_ROOKIE; i <= UNIFORM_MILITIA_ELITE; ++i)
	{
		if (COMPARE_PALETTEREP_ID(pCorpse->def.VestPal, gUniformColors[i].vest) && COMPARE_PALETTEREP_ID(pCorpse->def.PantsPal, gUniformColors[i].pants))
		{
			return TRUE;
		}
	}

	return FALSE;
}

BOOLEAN AICheckDefense(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	// only for enemy team
	if (!AICombatTeam(pSoldier))
	{
		return FALSE;
	}

	// SEEKENEMY should always try to attack
	if (pSoldier->aiData.bOrders == SEEKENEMY)
	{
		return FALSE;
	}

	// only try to defend in towns and underground
	if (!AICheckTown() && !AICheckUnderground())
	{
		return FALSE;
	}

	return TRUE;
}

BOOLEAN AICheckInterrupt(void)
{
	if (gTacticalStatus.ubTopMessageType == COMPUTER_INTERRUPT_MESSAGE ||
		gTacticalStatus.ubTopMessageType == PLAYER_INTERRUPT_MESSAGE ||
		gTacticalStatus.ubTopMessageType == MILITIA_INTERRUPT_MESSAGE)
	{
		return TRUE;
	}

	return FALSE;
}

// count friends under fire or with shock
UINT8 CountTeamUnderAttack(INT8 bTeam, INT32 sGridNo, INT16 sDistance)
{
	SOLDIERTYPE * pFriend;
	UINT8 ubFriendCount = 0;

	// safety check
	if (bTeam >= MAXTEAMS)
		return 0;

	// Run through each friendly.
	for (UINT8 iCounter = gTacticalStatus.Team[bTeam].bFirstID; iCounter <= gTacticalStatus.Team[bTeam].bLastID; iCounter++)
	{
		pFriend = MercPtrs[iCounter];

		if (pFriend &&
			pFriend->bActive &&
			pFriend->bInSector &&
			pFriend->stats.bLife >= OKLIFE &&
			!pFriend->bCollapsed &&
			!pFriend->bBreathCollapsed &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW) &&
			PythSpacesAway(sGridNo, pFriend->sGridNo) <= sDistance &&
			(pFriend->aiData.bUnderFire || pFriend->aiData.bShock > 0))
		{
			ubFriendCount++;
		}
	}

	return ubFriendCount;
}

// check if soldier should advance using sight cover 
BOOLEAN UseSightCoverAdvance(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	if (!SoldierAI(pSoldier))
	{
		return FALSE;
	}

	if (!AICombatTeam(pSoldier))
	{
		return FALSE;
	}

	if (pSoldier->aiData.bOrders == STATIONARY)
	{
		return FALSE;
	}

	// Every enemy understands sight-cover movement regardless of soldier class.
	if (pSoldier->bTeam == ENEMY_TEAM)
		return TRUE;

	switch (pSoldier->ubSoldierClass)
	{
	case SOLDIER_CLASS_ELITE:
	case SOLDIER_CLASS_ELITE_MILITIA:
		return TRUE;
		break;
	case SOLDIER_CLASS_ARMY:
	case SOLDIER_CLASS_REG_MILITIA:
		if (pSoldier->aiData.bUnderFire ||
			pSoldier->aiData.bShock > 0 ||
			AICheckDefense(pSoldier) ||
			AICorpseWarningKnown(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel) ||
			CountTeamUnderAttack(pSoldier->bTeam, pSoldier->sGridNo, DAY_VISION_RANGE) > 0 ||
			CountCorpses(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE, TRUE, TRUE) > 0)
		{
			return TRUE;
		}
		break;
	case SOLDIER_CLASS_ADMINISTRATOR:
	case SOLDIER_CLASS_GREEN_MILITIA:
		if (pSoldier->aiData.bUnderFire ||
			pSoldier->aiData.bShock > 0 ||
			AICorpseWarningKnown(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel) ||
			CountTeamUnderAttack(pSoldier->bTeam, pSoldier->sGridNo, DAY_VISION_RANGE) > 0 ||
			CountCorpses(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE, TRUE, TRUE) > 0)
		{
			return TRUE;
		}
		break;
	}

	if (pSoldier->aiData.bAttitude == DEFENSIVE ||
		pSoldier->aiData.bAttitude == CUNNINGSOLO ||
		pSoldier->aiData.bAttitude == CUNNINGAID)
	{
		return TRUE;
	}
	if (AILocalCasualtyPercent(pSoldier) > ArmyPercentKilledTolerance())
	{
		return TRUE;
	}

	return FALSE;
}

UINT8 ArmyPercentKilled(void)
{
	if (gTacticalStatus.Team[ENEMY_TEAM].bMenInSector + gTacticalStatus.ubArmyGuysKilled == 0)
	{
		return 0;
	}

	return 100 * gTacticalStatus.ubArmyGuysKilled / (gTacticalStatus.Team[ENEMY_TEAM].bMenInSector + gTacticalStatus.ubArmyGuysKilled);
}

UINT8 TeamPercentKilled(INT8 bTeam)
{
	if (bTeam == ENEMY_TEAM)
	{
		return ArmyPercentKilled();
	}
	return 0;
}

BOOLEAN TeamHighPercentKilled(INT8 bTeam)
{
	if (bTeam == ENEMY_TEAM && ArmyPercentKilled() > ArmyPercentKilledTolerance())
	{
		return TRUE;
	}

	return FALSE;
}

// decide how many soldiers can be killed before alarm will be raised
UINT8 ArmyPercentKilledTolerance(void)
{
	// 50% at day, 25% at night, 25-33% for restricted sectors
	return 100 / (2 + SectorCurfew(TRUE));
}

UINT8 SectorCurfew(BOOLEAN fNight)
{
	UINT8	ubSectorId = SECTOR(gWorldSectorX, gWorldSectorY);
	UINT8	ubSectorData = 0;

	ubSectorData = SectorExternalData[ubSectorId][gbWorldSectorZ].usCurfewValue;

	if (fNight && NightLight())			// suspicious at night
		ubSectorData = max(ubSectorData, 1);

	if (gbWorldSectorZ > 0)	// underground we are always suspicious				
		ubSectorData = max(ubSectorData, 2);

	return ubSectorData;
}

BOOLEAN FindObstacleNearSpot(INT32 sSpot, INT8 bLevel)
{
	if (TileIsOutOfBounds(sSpot))
	{
		return FALSE;
	}
	/*if( !IsLocationSittableExcludingPeople(sSpot, bLevel) )
	{
	return FALSE;
	}*/

	UINT8	ubMovementCost;
	INT32	sTempGridNo;
	UINT8	ubDirection;

	// check adjacent reachable tiles
	for (ubDirection = 0; ubDirection < NUM_WORLD_DIRECTIONS; ubDirection++)
	{
		sTempGridNo = NewGridNo(sSpot, DirectionInc(ubDirection));

		if (sTempGridNo != sSpot)
		{
			ubMovementCost = gubWorldMovementCosts[sTempGridNo][ubDirection][bLevel];

			if (ubMovementCost >= TRAVELCOST_BLOCKED || !IsLocationSittableExcludingPeople(sTempGridNo, bLevel))
			{
				return(TRUE);
			}
		}
	}

	return FALSE;
}

// check that enemy can see and attack at spot
BOOLEAN EnemyCanAttackSpot(SOLDIERTYPE *pSoldier, INT32 sSpot, INT8 bLevel)
{
	CHECKF(pSoldier);
	CHECKF(!TileIsOutOfBounds(sSpot));

	for (UINT32 uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercSlots[uiLoop];
		if (!pOpponent)
			continue;

		INT32 sThreatLoc = NOWHERE;
		INT8 bThreatLevel = 0;
		INT8 bKnowledge = NOT_HEARD_OR_SEEN;
		UINT8 ubConfidence = 0;
		if (!AIPlanningContactForOpponent(
			pSoldier, pOpponent->ubID, &sThreatLoc, &bThreatLevel,
			&ubConfidence, &bKnowledge))
		{
			continue;
		}

		const BOOLEAN fThreatStateKnown =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;

		if (fThreatStateKnown &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW))
		{
			continue;
		}

		if (TileIsOutOfBounds(sThreatLoc))
			continue;

		INT32 iAttackRange;
		if (fThreatStateKnown)
		{
			if (!ValidOpponent(pSoldier, pOpponent) ||
				pOpponent->IsUnconscious() || pOpponent->IsEmptyVehicle() ||
				!pOpponent->CanInterrupt())
			{
				continue;
			}

			if (!AICheckHasGun(pOpponent) &&
				PythSpacesAway(sThreatLoc, sSpot) > DAY_VISION_RANGE / 2)
			{
				continue;
			}

			iAttackRange = AICheckHasGun(pOpponent) ?
				AIGunRange(pOpponent) * 3 / 2 : DAY_VISION_RANGE / 2;
		}
		else
		{
			// No hidden weapon/AP/consciousness reads for a reported or stale contact.
			iAttackRange = max(DAY_VISION_RANGE / 4,
				(MAX_VISION_RANGE * (INT32)ubConfidence) / 100);
		}

		if (PythSpacesAway(sThreatLoc, sSpot) <= iAttackRange &&
			LocationToLocationLineOfSightTest(
				sThreatLoc, bThreatLevel, sSpot, bLevel,
				TRUE, MAX_VISION_RANGE))
		{
			return TRUE;
		}
	}

	return FALSE;
}

BOOLEAN	TerrainJungle(INT32 sSpot, INT8 bLevel)
{
	if (TileIsOutOfBounds(sSpot))
		return FALSE;

	UINT8 ubTerrainType = GetTerrainTypeForGrid(sSpot, bLevel);
	if (ubTerrainType == LOW_GRASS || ubTerrainType == HIGH_GRASS || ubTerrainType == FLAT_GROUND)
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN	TerrainDesert(INT32 sSpot, INT8 bLevel)
{
	if (TileIsOutOfBounds(sSpot))
		return FALSE;

	UINT8 ubTerrainType = GetTerrainTypeForGrid(sSpot, bLevel);
	if (ubTerrainType == DIRT_ROAD || ubTerrainType == TRAIN_TRACKS || ubTerrainType == FLAT_GROUND)
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN TerrainUrban(INT32 sSpot, INT8 bLevel)
{
	if (TileIsOutOfBounds(sSpot))
		return FALSE;

	UINT8 ubTerrainType = GetTerrainTypeForGrid(sSpot, bLevel);
	if (ubTerrainType == FLAT_FLOOR || ubTerrainType == PAVED_ROAD)
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN TerrainSnow(INT32 sSpot, INT8 bLevel)
{
	if (TileIsOutOfBounds(sSpot))
		return FALSE;

	UINT8 ubTerrainType = GetTerrainTypeForGrid(sSpot, bLevel);
	return FALSE;
}

BOOLEAN TerrainDark(INT32 sSpot, INT8 bLevel)
{
	if (TileIsOutOfBounds(sSpot))
		return FALSE;

	UINT8 ubLightLevel = LightTrueLevel(sSpot, bLevel);

	if (ubLightLevel >= NORMAL_LIGHTLEVEL_NIGHT - 3)
	{
		return TRUE;
	}

	return FALSE;
}

// Unified competence/friction adapter. Doctrine remains the authoritative training
// model; these helpers translate it into planner complexity/reliability without
// granting AP, CTH or hidden-information bonuses.
static UINT32 AIStableDecisionHash(SOLDIERTYPE *pSoldier, UINT32 uiSalt)
{
	if (!pSoldier)
		return uiSalt * 2246822519u;

	UINT32 uiValue = pSoldier->uiUniqueSoldierIdValue;
	uiValue ^= (guiTurnCnt + 1) * 2654435761u;
	uiValue ^= uiSalt * 2246822519u;
	uiValue ^= uiValue >> 13;
	uiValue *= 3266489917u;
	uiValue ^= uiValue >> 16;
	return uiValue;
}

INT8 AICompetenceTier(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier)
		return AI_COMPETENCE_BASIC;

	// Every live enemy uses the same top-end tactical reasoning. Unit identity is
	// expressed by equipment/mission role, not by deliberately dumbing decisions down.
	if (pSoldier->bTeam == ENEMY_TEAM)
		return AI_COMPETENCE_ELITE;

	switch (pSoldier->ubSoldierClass)
	{
	case SOLDIER_CLASS_ADMINISTRATOR:
	case SOLDIER_CLASS_GREEN_MILITIA:
		return AI_COMPETENCE_BASIC;
	case SOLDIER_CLASS_ELITE:
	case SOLDIER_CLASS_ELITE_MILITIA:
		return AI_COMPETENCE_ELITE;
	default:
		return AI_COMPETENCE_REGULAR;
	}
}

UINT8 AIPlannerReliability(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier)
		return 50;

	// Stress can change the correct decision, but it must not make enemy soldiers
	// randomly fail to execute a legal plan they already selected.
	if (pSoldier->bTeam == ENEMY_TEAM)
		return 100;

	INT32 iReliability = 76;
	switch (AICompetenceTier(pSoldier))
	{
	case AI_COMPETENCE_BASIC:   iReliability = 52; break;
	case AI_COMPETENCE_REGULAR: iReliability = 76; break;
	case AI_COMPETENCE_ELITE:   iReliability = 93; break;
	}

	if (pSoldier->bTeam == ENEMY_TEAM)
	{
		switch (AIGetDoctrineProfile(pSoldier))
		{
		case AI_DOCTRINE_VETERAN: iReliability -= 5; break;
		case AI_DOCTRINE_ELITE_GUARD: iReliability -= 2; break;
		default: break;
		}
	}

	if (pSoldier->aiData.bAIMorale == MORALE_HOPELESS)
		iReliability -= 20;
	else if (pSoldier->aiData.bAIMorale == MORALE_WORRIED)
		iReliability -= 10;
	else if (pSoldier->aiData.bAIMorale == MORALE_FEARLESS)
		iReliability += 3;

	iReliability -= __min((INT32)25, AILocalStress(pSoldier) / 4);
	if (pSoldier->aiData.bUnderFire)
		iReliability -= 5;

	return (UINT8)__max(20, __min(98, iReliability));
}

BOOLEAN AIAllowsPlanComplexity(SOLDIERTYPE *pSoldier, INT8 bComplexity, UINT32 uiSalt)
{
	if (!pSoldier || bComplexity <= AI_PLAN_BASIC)
		return TRUE;

	if (pSoldier->bTeam == ENEMY_TEAM)
	{
		// Coordinated reasoning is universal. Only explicit mission-role restrictions
		// may veto an advanced manoeuvre; there is no artificial failure roll.
		if (bComplexity >= AI_PLAN_ADVANCED && !AIAllowsComplexManeuver(pSoldier))
			return FALSE;
		return TRUE;
	}

	INT8 bTier = AICompetenceTier(pSoldier);
	INT32 iChance = AIPlannerReliability(pSoldier);

	if (bComplexity == AI_PLAN_COORDINATED)
	{
		if (bTier == AI_COMPETENCE_BASIC)
			iChance = __min(iChance, AIHasLocalCommandSupport(pSoldier) ? 45 : 30);
		else if (bTier == AI_COMPETENCE_REGULAR)
			iChance = __min(iChance, 82);
	}
	else
	{
		if (bTier == AI_COMPETENCE_BASIC)
			return FALSE;
		if (bTier == AI_COMPETENCE_REGULAR)
			iChance = __min(iChance, 45);
		else
			iChance = __min(iChance, 92);
	}

	return (INT32)(AIStableDecisionHash(pSoldier,
		uiSalt + 17u * (UINT32)bComplexity) % 100) < iChance;
}

INT32 AICompetenceUtilityNoise(SOLDIERTYPE *pSoldier, INT32 sCandidateSpot, UINT32 uiSalt)
{
	if (!pSoldier)
		return 0;

	// Grandmaster target: enemies do not select inferior positions because of an
	// artificial competence-noise roll. Real uncertainty is represented elsewhere.
	if (pSoldier->bTeam == ENEMY_TEAM)
		return 0;

	INT32 iAmplitude = 5;
	switch (AICompetenceTier(pSoldier))
	{
	case AI_COMPETENCE_BASIC:   iAmplitude = 24; break;
	case AI_COMPETENCE_REGULAR: iAmplitude = 11; break;
	case AI_COMPETENCE_ELITE:   iAmplitude = 4; break;
	}

	UINT32 uiValue = AIStableDecisionHash(pSoldier,
		uiSalt ^ (UINT32)(sCandidateSpot + 32768));
	return (INT32)(uiValue % (UINT32)(2 * iAmplitude + 1)) - iAmplitude;
}

UINT8 AILocalSmokeReserve(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier)
		return 0;

	UINT8 ubSmoke = 0;
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || !pFriend->bActive || !pFriend->bInSector ||
			pFriend->stats.bLife < OKLIFE ||
			(pSoldier->bTeam == ENEMY_TEAM && !AISameFireteam(pSoldier, pFriend)) ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > DAY_VISION_RANGE / 2)
		{
			continue;
		}

		if (FindThrowableGrenade(pFriend, EXPLOSV_SMOKE) != NO_SLOT)
			++ubSmoke;
	}

	return ubSmoke;
}

INT32 AIInferredReactionRisk(SOLDIERTYPE *pSoldier, INT32 sCandidateSpot, INT8 bLevel)
{
	if (!AICombatTeam(pSoldier) || TileIsOutOfBounds(sCandidateSpot))
		return 0;

	INT32 iRisk = 0;
	for (UINT16 uiLoop = 0; uiLoop < MAX_NUM_SOLDIERS; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercPtrs[uiLoop];
		if (!pOpponent || pOpponent == pSoldier)
			continue;

		INT32 sKnownSpot = NOWHERE;
		INT8 bKnownLevel = 0;
		INT8 bKnowledge = NOT_HEARD_OR_SEEN;
		UINT8 ubConfidence = 0;
		if (!AIPlanningContactForOpponent(
			pSoldier, pOpponent->ubID, &sKnownSpot, &bKnownLevel,
			&ubConfidence, &bKnowledge))
		{
			continue;
		}

		const BOOLEAN fPersonallySeeingNow =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;

		if (fPersonallySeeingNow &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide))
		{
			continue;
		}

		if (TileIsOutOfBounds(sKnownSpot) || bKnownLevel != bLevel)
			continue;

		if (PythSpacesAway(sKnownSpot, sCandidateSpot) > MAX_VISION_RANGE ||
			!LocationToLocationLineOfSightTest(
				sKnownSpot, bKnownLevel, sCandidateSpot, bLevel,
				TRUE, MAX_VISION_RANGE))
		{
			continue;
		}

		INT32 iContactRisk = 8 + (INT32)ubConfidence / 5;
		if (ubConfidence >= 85)
			iContactRisk += 12;
		else if (ubConfidence >= 60)
			iContactRisk += 6;

		if (fPersonallySeeingNow &&
			(pOpponent->aiData.bAction == AI_ACTION_FIRE_GUN ||
			 pOpponent->aiData.bLastAction == AI_ACTION_FIRE_GUN))
		{
			iContactRisk -= 8;
		}

		iRisk += __max(0, iContactRisk);
	}

	if (InSmoke(sCandidateSpot, bLevel))
		iRisk /= 3;

	return __min((INT32)120, iRisk);
}

// -----------------------------------------------------------------------------
// Layered squad tactical planner
// -----------------------------------------------------------------------------
// The legacy Vengeance/1.13 AI contains many strong local heuristics, but they
// historically compete by call order.  This lightweight planner gives those
// heuristics a common context: persistent squad intent, dynamic fireteam role,
// and a shared position utility score.  It deliberately uses only information
// available through the normal JA2 knowledge model.
static INT8 gbAITacticalIntentPlan[MAX_NUM_SOLDIERS] = { 0 };
static INT8 gbAITacticalRolePlan[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAITacticalPlanUntil[MAX_NUM_SOLDIERS] = { 0 };
static INT32 gsAITacticalPlanTarget[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAITacticalRoleUntil[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAITacticalPlanIdentity[MAX_NUM_SOLDIERS] = { 0 };

void AIResetTacticalPlannerStateForLoad(void)
{
	// Planner state is intentionally transient and is not serialized. Same-sector
	// quickloads must not inherit intent/role decisions from the abandoned future.
	AIResetTacticalReasoningStateForLoad();

	// The legacy ENEMY_TEAM public opponent list is a sector-wide exact-contact
	// channel. It is no longer authoritative for the local-hive-mind AI. Personal
	// memories remain serialized/restored normally; only the forbidden shared copy
	// is discarded so an old save cannot resurrect telepathic contact knowledge.
	memset(gbPublicOpplist[ENEMY_TEAM], NOT_HEARD_OR_SEEN,
		sizeof(gbPublicOpplist[ENEMY_TEAM]));
	for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
	{
		gsPublicLastKnownOppLoc[ENEMY_TEAM][i] = NOWHERE;
		gbPublicLastKnownOppLevel[ENEMY_TEAM][i] = 0;

		gbAITacticalIntentPlan[i] = AI_INTENT_HOLD;
		gbAITacticalRolePlan[i] = AI_ROLE_RESERVE;
		guiAITacticalPlanUntil[i] = 0;
		gsAITacticalPlanTarget[i] = NOWHERE;
		guiAITacticalRoleUntil[i] = 0;
		guiAITacticalPlanIdentity[i] = 0;
	}
}

static BOOLEAN AITacticalTargetChanged(UINT8 ubID, INT32 sTargetSpot)
{
	if (TileIsOutOfBounds(sTargetSpot) || TileIsOutOfBounds(gsAITacticalPlanTarget[ubID]))
		return (sTargetSpot != gsAITacticalPlanTarget[ubID]);

	return (PythSpacesAway(sTargetSpot, gsAITacticalPlanTarget[ubID]) > 5);
}

static UINT8 AIActiveManeuverCount(SOLDIERTYPE *pSoldier, INT32 sTargetSpot)
{
	if (!pSoldier)
		return 0;

	UINT8 ubCount = 0;
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier || !pFriend->bActive || !pFriend->bInSector ||
			(pSoldier->bTeam == ENEMY_TEAM && !AISameFireteam(pSoldier, pFriend)) ||
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed)
		{
			continue;
		}

		BOOLEAN fMover = pFriend->IsFlanking() ||
			pFriend->aiData.bAction == AI_ACTION_GET_CLOSER ||
			pFriend->aiData.bAction == AI_ACTION_SEEK_OPPONENT ||
			pFriend->aiData.bAction == AI_ACTION_FLANK_LEFT ||
			pFriend->aiData.bAction == AI_ACTION_FLANK_RIGHT;

		if (!fMover)
			continue;

		if (!TileIsOutOfBounds(sTargetSpot))
		{
			INT32 sFriendTarget = AIPrimaryPlanningThreatSpot(pFriend);
			if (!TileIsOutOfBounds(sFriendTarget) && PythSpacesAway(sFriendTarget, sTargetSpot) > 5)
				continue;
		}

		++ubCount;
	}

	return ubCount;
}

static INT8 AISharedIntentVote(SOLDIERTYPE *pSoldier, INT32 sTargetSpot, UINT32 uiNow)
{
	if (!pSoldier || TileIsOutOfBounds(sTargetSpot))
		return -1;

	UINT8 ubVotes[AI_INTENT_RESCUE + 1] = { 0 };
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier || !pFriend->bActive || !pFriend->bInSector ||
			(pSoldier->bTeam == ENEMY_TEAM && !AISameFireteam(pSoldier, pFriend)) ||
			pFriend->stats.bLife < OKLIFE || guiAITacticalPlanUntil[pFriend->ubID] < uiNow ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > DAY_VISION_RANGE)
		{
			continue;
		}

		INT32 sFriendTarget = gsAITacticalPlanTarget[pFriend->ubID];
		if (TileIsOutOfBounds(sFriendTarget) || PythSpacesAway(sFriendTarget, sTargetSpot) > 5)
			continue;

		INT8 bFriendIntent = gbAITacticalIntentPlan[pFriend->ubID];
		if (bFriendIntent >= AI_INTENT_HOLD && bFriendIntent <= AI_INTENT_RESCUE)
		{
			UINT8 ubWeight = (AICheckIsCommander(pFriend) || AICheckIsOfficer(pFriend)) ? 2 : 1;
			ubVotes[bFriendIntent] += ubWeight;
		}
	}

	INT8 bBestIntent = -1;
	UINT8 ubBestVotes = 0;
	for (INT8 bIntent = AI_INTENT_HOLD; bIntent <= AI_INTENT_RESCUE; ++bIntent)
	{
		if (ubVotes[bIntent] > ubBestVotes)
		{
			ubBestVotes = ubVotes[bIntent];
			bBestIntent = bIntent;
		}
	}

	// Enemy fireteams share intent almost immediately: one valid local plan is enough
	// to seed the element. Militia retain the more conservative two-vote threshold.
	if (pSoldier->bTeam == ENEMY_TEAM)
		return ubBestVotes >= 1 ? bBestIntent : -1;
	return ubBestVotes >= 2 ? bBestIntent : -1;
}

static UINT8 AIPlannedRoleCount(SOLDIERTYPE *pSoldier, INT32 sTargetSpot, INT8 bRole, UINT32 uiNow)
{
	if (!pSoldier)
		return 0;

	UINT8 ubCount = 0;
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier || !pFriend->bActive || !pFriend->bInSector ||
			(pSoldier->bTeam == ENEMY_TEAM && !AISameFireteam(pSoldier, pFriend)) ||
			pFriend->stats.bLife < OKLIFE || guiAITacticalRoleUntil[pFriend->ubID] < uiNow ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > DAY_VISION_RANGE)
		{
			continue;
		}

		if (!TileIsOutOfBounds(sTargetSpot))
		{
			INT32 sFriendTarget = gsAITacticalPlanTarget[pFriend->ubID];
			if (TileIsOutOfBounds(sFriendTarget) || PythSpacesAway(sFriendTarget, sTargetSpot) > 5)
				continue;
		}

		if (gbAITacticalRolePlan[pFriend->ubID] == bRole)
			++ubCount;
	}

	return ubCount;
}

INT8 AITacticalIntent(SOLDIERTYPE *pSoldier, INT32 sTargetSpot)
{
	if (!AICombatTeam(pSoldier))
		return AI_INTENT_HOLD;

	UINT8 ubID = pSoldier->ubID;
	if (ubID >= MAX_NUM_SOLDIERS)
		return AI_INTENT_HOLD;

	// Soldier slots are recycled by JA2. Never let a newly created actor inherit a
	// short-lived plan that belonged to the previous occupant of the same slot.
	if (guiAITacticalPlanIdentity[ubID] != pSoldier->uiUniqueSoldierIdValue)
	{
		gbAITacticalIntentPlan[ubID] = AI_INTENT_HOLD;
		gbAITacticalRolePlan[ubID] = AI_ROLE_RESERVE;
		guiAITacticalPlanUntil[ubID] = 0;
		gsAITacticalPlanTarget[ubID] = NOWHERE;
		guiAITacticalRoleUntil[ubID] = 0;
		guiAITacticalPlanIdentity[ubID] = pSoldier->uiUniqueSoldierIdValue;
	}

	if (TileIsOutOfBounds(sTargetSpot))
		sTargetSpot = AIPrimaryPlanningThreatSpot(pSoldier);


	UINT32 uiNow = guiTurnCnt + 1;
	AITACTICALDECISIONCONTEXT Context;
	if (!AIBuildTacticalDecisionContext(pSoldier, &Context))
		return AI_INTENT_HOLD;

	INT8 bSituation = Context.bBattleSituation;
	INT32 iStress = Context.iStress;
	INT32 iRisk = Context.iPersonalRisk;
	INT32 iTolerance = Context.iRiskTolerance;
	UINT16 usExposure = Context.usKnownThreatExposure;
	UINT8 ubEffectiveFireSupport = TileIsOutOfBounds(sTargetSpot) ? 0 :
		AIFireteamEffectiveFireSupport(pSoldier, sTargetSpot);
	INT32 iSharedApproachPressure = TileIsOutOfBounds(sTargetSpot) ? 0 :
		AISharedApproachPressure(pSoldier, sTargetSpot);
	BOOLEAN fBasicFireteamManeuver = TileIsOutOfBounds(sTargetSpot) ? FALSE :
		AIBasicFireteamManeuverReady(pSoldier, sTargetSpot);

	// Hard tactical emergencies immediately replace any previous plan.
	INT8 bEmergencyIntent = -1;
	if (AIEscapeActive(pSoldier) || AIDisengagementActive(pSoldier))
		bEmergencyIntent = AI_INTENT_DISENGAGE;
	else if (pSoldier->aiData.bUnderFire &&
		(ShockLevelPercent(pSoldier) >= 45 || iRisk > iTolerance || usExposure >= 180))
		bEmergencyIntent = AI_INTENT_FALLBACK;
	else if ((bSituation == AI_BATTLE_CATASTROPHIC ||
		(bSituation == AI_BATTLE_LOSING && AISeverelyIsolated(pSoldier))) &&
		pSoldier->aiData.bOrders != STATIONARY)
		bEmergencyIntent = AI_INTENT_FALLBACK;
	else if (AICheckIsMedic(pSoldier) && CountFriendsNeedHelp(pSoldier) > 0 &&
		!pSoldier->aiData.bUnderFire && iRisk + 10 < iTolerance)
		bEmergencyIntent = AI_INTENT_RESCUE;

	BOOLEAN fActiveCQBShortPlan = FALSE;

	if (bEmergencyIntent < 0)
	{
		AISHORTPLANSTATE ShortPlan;
		if (AIGetShortPlan(pSoldier, &ShortPlan))
		{
			// CQB owns the detailed room/entry plan. The generic intent layer must
			// preserve that commitment rather than translating it into HOLD/PRESS and
			// then cancelling it. Senior emergencies below may still supersede it.
			if (ShortPlan.ubType == AI_SHORT_PLAN_CQB)
			{
				fActiveCQBShortPlan =
					!TileIsOutOfBounds(ShortPlan.sTargetGridNo) &&
					(InARoom(pSoldier->sGridNo, NULL) ||
					 CheckDoorNearGridno((UINT32)pSoldier->sGridNo) ||
					 PythSpacesAway(pSoldier->sGridNo, ShortPlan.sTargetGridNo) <= TACTICAL_RANGE);

				if (!fActiveCQBShortPlan)
					AICancelShortPlan(pSoldier);
			}

			BOOLEAN fSameTarget =
				ShortPlan.ubType != AI_SHORT_PLAN_CQB &&
				((TileIsOutOfBounds(sTargetSpot) && TileIsOutOfBounds(ShortPlan.sTargetGridNo)) ||
				 (!TileIsOutOfBounds(sTargetSpot) && !TileIsOutOfBounds(ShortPlan.sTargetGridNo) &&
				  PythSpacesAway(sTargetSpot, ShortPlan.sTargetGridNo) <= 3));

			if (fSameTarget)
			{
				BOOLEAN fPlanStillValid = FALSE;
				switch (ShortPlan.ubType)
				{
				case AI_SHORT_PLAN_FLANK:
					fPlanStillValid =
						!TileIsOutOfBounds(sTargetSpot) &&
						!AIShouldAvoidAdvance(pSoldier) &&
						iStress < 55 &&
						iRisk <= iTolerance + 5;
					if (fPlanStillValid)
						return AI_INTENT_FLANK;
					break;

				case AI_SHORT_PLAN_FALLBACK:
					fPlanStillValid =
						pSoldier->aiData.bUnderFire ||
						iStress >= 30 ||
						iRisk + 5 >= iTolerance ||
						bSituation == AI_BATTLE_LOSING ||
						bSituation == AI_BATTLE_CATASTROPHIC;
					if (fPlanStillValid)
						return AI_INTENT_FALLBACK;
					break;

				case AI_SHORT_PLAN_DISENGAGE:
					fPlanStillValid = AIDisengagementActive(pSoldier) || AIEscapeActive(pSoldier);
					if (fPlanStillValid)
						return AI_INTENT_DISENGAGE;
					break;

				case AI_SHORT_PLAN_RESCUE:
					fPlanStillValid =
						AICheckIsMedic(pSoldier) &&
						CountFriendsNeedHelp(pSoldier) > 0 &&
						!pSoldier->aiData.bUnderFire &&
						iRisk <= iTolerance;
					if (fPlanStillValid)
						return AI_INTENT_RESCUE;
					break;

				default:
					break;
				}

				if (!fPlanStillValid)
					AICancelShortPlan(pSoldier);
			}
		}
	}

	if (bEmergencyIntent < 0 &&
		guiAITacticalPlanUntil[ubID] >= uiNow &&
		!AITacticalTargetChanged(ubID, sTargetSpot))
	{
		return gbAITacticalIntentPlan[ubID];
	}

	INT8 bIntent = AI_INTENT_HOLD;
	if (bEmergencyIntent >= 0)
	{
		bIntent = bEmergencyIntent;
	}
	else if (!TileIsOutOfBounds(sTargetSpot))
	{
		UINT8 ubNearbyFriends = CountNearbyFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 2);
		BOOLEAN fLocalAdvantage = AICheckWeOutnumberLocal(pSoldier, sTargetSpot) ||
			bSituation == AI_BATTLE_WINNING;
		BOOLEAN fCanPress = !AIShouldAvoidAdvance(pSoldier) &&
			iStress < (ubEffectiveFireSupport > 0 ? 60 : 55) &&
			iRisk <= iTolerance + (ubEffectiveFireSupport > 0 ? 10 : 5) &&
			(ubNearbyFriends > 0 || fLocalAdvantage);

		if (fCanPress && fLocalAdvantage)
		{
			// Tactical intelligence is universal. Personality affects risk/tempo, not
			// whether the soldier understands coordinated flanking.
			BOOLEAN fAdvancedFlank =
				AIAllowsPlanComplexity(pSoldier, AI_PLAN_COORDINATED,
					(UINT32)(sTargetSpot + 211));
			BOOLEAN fBasicFlank = fBasicFireteamManeuver &&
				(iSharedApproachPressure >= 30 || ubEffectiveFireSupport > 0) &&
				AIFireteamPreferredFlankAction(pSoldier, sTargetSpot) != AI_ACTION_NONE;

			if (!pSoldier->aiData.bUnderFire &&
				AIManeuverRoleScore(pSoldier, sTargetSpot) >= AISupportRoleScore(pSoldier, sTargetSpot) - 5 &&
				AIActiveManeuverCount(pSoldier, sTargetSpot) < 2 &&
				(fAdvancedFlank || fBasicFlank))
			{
				bIntent = AI_INTENT_FLANK;
			}
			else
			{
				bIntent = AI_INTENT_PRESS;
			}
		}
		else if (fCanPress && bSituation == AI_BATTLE_EVEN && ubNearbyFriends >= 2)
		{
			bIntent = AI_INTENT_PRESS;
		}
		else if (iStress >= 40 || iRisk + 10 >= iTolerance)
		{
			bIntent = AI_INTENT_FALLBACK;
		}
	}

	// Distributed squad blackboard: soldiers fighting the same contact bias toward
	// a common plan, while personal danger can still veto an aggressive consensus.
	INT8 bSharedIntent = AISharedIntentVote(pSoldier, sTargetSpot, uiNow);
	if (bEmergencyIntent < 0 && bSharedIntent >= AI_INTENT_HOLD)
	{
		if (bSharedIntent == AI_INTENT_FALLBACK || bSharedIntent == AI_INTENT_DISENGAGE)
		{
			if (bIntent != AI_INTENT_RESCUE && bSituation != AI_BATTLE_WINNING)
				bIntent = bSharedIntent;
		}
		else if (bIntent != AI_INTENT_FALLBACK && bIntent != AI_INTENT_DISENGAGE &&
			iRisk <= iTolerance + 5)
		{
			// The local enemy fireteam deliberately behaves like a shared tactical brain.
			// The shared target still came only from legal observation/communication.
			if (pSoldier->bTeam == ENEMY_TEAM ||
				bSharedIntent != AI_INTENT_FLANK ||
				fBasicFireteamManeuver ||
				AIAllowsPlanComplexity(pSoldier, AI_PLAN_COORDINATED, (UINT32)(sTargetSpot + 307)))
			{
				bIntent = bSharedIntent;
			}
		}
	}

	gbAITacticalIntentPlan[ubID] = bIntent;
	gsAITacticalPlanTarget[ubID] = sTargetSpot;
	// One extra turn of persistence prevents oscillation between equally plausible
	// plans. Emergencies above can still override this immediately.
	guiAITacticalPlanUntil[ubID] = uiNow + 1;

	UINT8 ubShortPlan = AI_SHORT_PLAN_NONE;
	switch (bIntent)
	{
	case AI_INTENT_FLANK: ubShortPlan = AI_SHORT_PLAN_FLANK; break;
	case AI_INTENT_FALLBACK: ubShortPlan = AI_SHORT_PLAN_FALLBACK; break;
	case AI_INTENT_DISENGAGE: ubShortPlan = AI_SHORT_PLAN_DISENGAGE; break;
	case AI_INTENT_RESCUE: ubShortPlan = AI_SHORT_PLAN_RESCUE; break;
	default: break;
	}

	if (ubShortPlan != AI_SHORT_PLAN_NONE)
	{
		AISHORTPLANSTATE ExistingPlan;
		BOOLEAN fKeepPlan = AIGetShortPlan(pSoldier, &ExistingPlan) &&
			ExistingPlan.ubType == ubShortPlan &&
			((TileIsOutOfBounds(sTargetSpot) && TileIsOutOfBounds(ExistingPlan.sTargetGridNo)) ||
			 (!TileIsOutOfBounds(sTargetSpot) && !TileIsOutOfBounds(ExistingPlan.sTargetGridNo) &&
			  PythSpacesAway(sTargetSpot, ExistingPlan.sTargetGridNo) <= 3));
		if (!fKeepPlan)
			AIBeginShortPlan(pSoldier, ubShortPlan, sTargetSpot, NOBODY, 2);
	}
	else if (!fActiveCQBShortPlan)
	{
		AICancelShortPlan(pSoldier);
	}

	return bIntent;
}

static BOOLEAN AIPreferredSuppressorCandidate(
	SOLDIERTYPE *pSoldier, INT32 sTargetSpot, UINT8 ubSuppressorLimit)
{
	if (!pSoldier || pSoldier->bTeam != ENEMY_TEAM ||
		TileIsOutOfBounds(sTargetSpot) || ubSuppressorLimit == 0 ||
		!AICheckHasGun(pSoldier) || !AIGunAutofireCapable(pSoldier) ||
		AIGunAmmo(pSoldier) < gGameExternalOptions.ubAISuppressionMinimumAmmo)
	{
		return FALSE;
	}

	INT32 iMyScore = AISupportRoleScore(pSoldier, sTargetSpot);
	UINT8 ubBetterCandidates = 0;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pCandidate = MercPtrs[iCounter];
		if (!pCandidate || pCandidate == pSoldier ||
			!pCandidate->bActive || !pCandidate->bInSector ||
			!AISameFireteam(pSoldier, pCandidate) ||
			pCandidate->stats.bLife < OKLIFE ||
			pCandidate->bCollapsed || pCandidate->bBreathCollapsed ||
			(pCandidate->usSoldierFlagMask & SOLDIER_POW) ||
			(pCandidate->flags.uiStatusFlags & SOLDIER_COWERING) ||
			AIDisengagementActive(pCandidate) || AIEscapeActive(pCandidate) ||
			!AICheckHasGun(pCandidate) || !AIGunAutofireCapable(pCandidate) ||
			AIGunAmmo(pCandidate) < gGameExternalOptions.ubAISuppressionMinimumAmmo)
		{
			continue;
		}

		INT32 sCandidateTarget = AIPrimaryPlanningThreatSpot(pCandidate);
		if (TileIsOutOfBounds(sCandidateTarget) ||
			PythSpacesAway(sCandidateTarget, sTargetSpot) > 3)
		{
			continue;
		}

		INT32 iCandidateScore = AISupportRoleScore(pCandidate, sTargetSpot);
		if (iCandidateScore > iMyScore ||
			(iCandidateScore == iMyScore && pCandidate->ubID < pSoldier->ubID))
		{
			++ubBetterCandidates;
			if (ubBetterCandidates >= ubSuppressorLimit)
				return FALSE;
		}
	}

	return TRUE;
}

INT8 AITacticalRole(SOLDIERTYPE *pSoldier, INT32 sTargetSpot)
{
	if (!AICombatTeam(pSoldier))
		return AI_ROLE_RESERVE;

	UINT8 ubID = pSoldier->ubID;
	if (TileIsOutOfBounds(sTargetSpot))
		sTargetSpot = AIPrimaryPlanningThreatSpot(pSoldier);

	INT8 bIntent = AITacticalIntent(pSoldier, sTargetSpot);
	INT32 iSupport = AISupportRoleScore(pSoldier, sTargetSpot);
	INT32 iManeuver = AIManeuverRoleScore(pSoldier, sTargetSpot);
	INT8 bRole = AI_ROLE_RESERVE;
	UINT32 uiNow = guiTurnCnt + 1;
	UINT8 ubPlannedFlankers = AIPlannedRoleCount(pSoldier, sTargetSpot, AI_ROLE_FLANKER, uiNow);
	UINT8 ubPlannedMovers = ubPlannedFlankers + AIPlannedRoleCount(pSoldier, sTargetSpot, AI_ROLE_MANEUVER, uiNow);
	UINT8 ubPlannedScreens = AIPlannedRoleCount(pSoldier, sTargetSpot, AI_ROLE_SCREEN, uiNow);
	UINT8 ubPlannedSupports = AIPlannedRoleCount(pSoldier, sTargetSpot, AI_ROLE_SUPPORT, uiNow) + ubPlannedScreens;
	UINT8 ubEffectiveFireSupport = AIFireteamEffectiveFireSupport(pSoldier, sTargetSpot);
	BOOLEAN fBasicFireteamManeuver = AIBasicFireteamManeuverReady(pSoldier, sTargetSpot);

	if (bIntent == AI_INTENT_RESCUE)
	{
		bRole = AICheckIsMedic(pSoldier) ? AI_ROLE_RESERVE : AI_ROLE_SCREEN;
	}
	else if (bIntent == AI_INTENT_DISENGAGE || bIntent == AI_INTENT_FALLBACK)
	{
		// Healthy long-range soldiers form the rear guard while more mobile soldiers
		// displace. This creates alternating bounds instead of a simultaneous rout.
		if (iSupport >= iManeuver + 5 && !pSoldier->aiData.bUnderFire && ubPlannedScreens < 2 &&
			AIAllowsPlanComplexity(pSoldier, AI_PLAN_COORDINATED, (UINT32)(sTargetSpot + 401)))
			bRole = AI_ROLE_SCREEN;
		else
			bRole = AI_ROLE_MANEUVER;
	}
	else if (AICheckIsMachinegunner(pSoldier) || AICheckIsSniper(pSoldier) ||
		AICheckIsMortarOperator(pSoldier) || iSupport >= iManeuver + 18)
	{
		bRole = AI_ROLE_SUPPORT;
	}
	else if ((bIntent == AI_INTENT_PRESS || bIntent == AI_INTENT_FLANK) &&
		AIFireteamCombatReadyCount(pSoldier) >= 3 &&
		ubPlannedSupports == 0 && ubEffectiveFireSupport == 0 &&
		AICheckHasGun(pSoldier) && iSupport >= iManeuver - 8)
	{
		// Establish a base of fire before assigning another mover. Sequential JA2
		// turns otherwise tend to send the first two reasonable soldiers forward
		// before anyone has actually created a covering-fire window.
		bRole = AI_ROLE_SUPPORT;
	}
	else if (bIntent == AI_INTENT_FLANK &&
		(fBasicFireteamManeuver ? (iManeuver >= iSupport - 5) : (iManeuver > iSupport)) &&
		ubPlannedFlankers < 2 &&
		AIActiveManeuverCount(pSoldier, sTargetSpot) + ubPlannedMovers < 3 &&
		(fBasicFireteamManeuver ||
		 AIAllowsPlanComplexity(pSoldier, AI_PLAN_COORDINATED, (UINT32)(sTargetSpot + 503))))
	{
		bRole = AI_ROLE_FLANKER;
	}
	else if (bIntent == AI_INTENT_PRESS && iManeuver >= iSupport - 5 &&
		ubPlannedMovers < 2 &&
		AIActiveManeuverCount(pSoldier, sTargetSpot) < 2)
	{
		bRole = AI_ROLE_MANEUVER;
	}
	else if (iSupport >= iManeuver)
	{
		bRole = AI_ROLE_SUPPORT;
	}

	// Convert implicit role-count coordination into explicit, fireteam-local task
	// claims. Movers, flankers and screens cannot silently duplicate each other, while
	// the best automatic-rifle/LMG support soldier owns the base-of-fire assignment.
	BOOLEAN fTaskReserved = TRUE;
	if (bRole == AI_ROLE_FLANKER)
		fTaskReserved = AIReserveTacticalTask(
			pSoldier, AI_TASK_FLANK, sTargetSpot, NOBODY, 2, 1);
	else if (bRole == AI_ROLE_MANEUVER)
		fTaskReserved = AIReserveTacticalTask(
			pSoldier, AI_TASK_MANEUVER, sTargetSpot, NOBODY, 2, 1);
	else if (bRole == AI_ROLE_SCREEN)
		fTaskReserved = AIReserveTacticalTask(
			pSoldier, AI_TASK_SCREEN, sTargetSpot, NOBODY, 2, 1);
	else if (bRole == AI_ROLE_SUPPORT &&
		pSoldier->bTeam == ENEMY_TEAM &&
		(bIntent == AI_INTENT_PRESS || bIntent == AI_INTENT_FLANK) &&
		!TileIsOutOfBounds(sTargetSpot) &&
		AICheckHasGun(pSoldier) &&
		AIGunAutofireCapable(pSoldier) &&
		AIGunAmmo(pSoldier) >= gGameExternalOptions.ubAISuppressionMinimumAmmo)
	{
		UINT8 ubSuppressorLimit =
			AIFireteamCombatReadyCount(pSoldier) >= 6 ? 2 : 1;
		// Compare the whole local element before claiming the job. Sequential turn
		// order must not let a mediocre rifleman steal the LMG's base-of-fire role.
		if (!AIPreferredSuppressorCandidate(
				pSoldier, sTargetSpot, ubSuppressorLimit) ||
			!AIReserveTacticalTask(
				pSoldier, AI_TASK_SUPPRESS, sTargetSpot, NOBODY,
				ubSuppressorLimit, 1))
		{
			AIReleaseTacticalTask(pSoldier);
		}
	}
	else
		AIReleaseTacticalTask(pSoldier);

	if (!fTaskReserved)
	{
		// Another capable teammate already owns this local responsibility. Fall back
		// to support rather than creating duplicate movers or rear guards.
		bRole = AI_ROLE_SUPPORT;
		AIReleaseTacticalTask(pSoldier);

		if (pSoldier->bTeam == ENEMY_TEAM &&
			(bIntent == AI_INTENT_PRESS || bIntent == AI_INTENT_FLANK) &&
			!TileIsOutOfBounds(sTargetSpot) &&
			AICheckHasGun(pSoldier) &&
			AIGunAutofireCapable(pSoldier) &&
			AIGunAmmo(pSoldier) >= gGameExternalOptions.ubAISuppressionMinimumAmmo)
		{
			UINT8 ubSuppressorLimit =
				AIFireteamCombatReadyCount(pSoldier) >= 6 ? 2 : 1;
			if (AIPreferredSuppressorCandidate(
					pSoldier, sTargetSpot, ubSuppressorLimit))
			{
				AIReserveTacticalTask(
					pSoldier, AI_TASK_SUPPRESS, sTargetSpot, NOBODY,
					ubSuppressorLimit, 1);
			}
		}
	}

	gbAITacticalRolePlan[ubID] = bRole;
	guiAITacticalRoleUntil[ubID] = uiNow + 1;
	return bRole;
}

static INT32 AIFuturePositionPotential(
	SOLDIERTYPE *pSoldier, INT32 sCandidateSpot, INT32 sTargetSpot,
	INT8 bIntent, INT8 bRole)
{
	if (!pSoldier || pSoldier->bTeam != ENEMY_TEAM ||
		TileIsOutOfBounds(sCandidateSpot))
	{
		return 0;
	}

	const INT8 bLevel = pSoldier->pathing.bLevel;
	const UINT16 usCandidateExposure =
		AIKnownThreatExposure(pSoldier, sCandidateSpot, bLevel);
	const INT32 iCandidateDistance = TileIsOutOfBounds(sTargetSpot) ?
		0 : PythSpacesAway(sCandidateSpot, sTargetSpot);

	INT32 iBestFollowup = -10000;
	UINT8 ubSafeOptions = 0;
	UINT8 ubCoveredOptions = 0;

	for (UINT8 ubDirection = 0; ubDirection < NUM_WORLD_DIRECTIONS; ++ubDirection)
	{
		INT32 sNext = NewGridNo(sCandidateSpot, DirectionInc(ubDirection));
		if (sNext == sCandidateSpot || TileIsOutOfBounds(sNext))
			continue;

		UINT8 ubMovementCost =
			gubWorldMovementCosts[sNext][ubDirection][bLevel];
		if (ubMovementCost >= TRAVELCOST_BLOCKED ||
			!IsLocationSittableExcludingPeople(sNext, bLevel))
		{
			continue;
		}

		if (InGas(pSoldier, sNext) ||
			RedSmokeDanger(sNext, bLevel) ||
			FindBombNearby(pSoldier, sNext, BOMB_DETECTION_RANGE))
		{
			continue;
		}

		UINT16 usExposure = AIKnownThreatExposure(pSoldier, sNext, bLevel);
		INT32 iReactionRisk = AIInferredReactionRisk(pSoldier, sNext, bLevel);
		BOOLEAN fCover = AnyCoverAtSpot(pSoldier, sNext);
		BOOLEAN fSightCover = SightCoverAtSpot(pSoldier, sNext, FALSE);

		INT32 iFollowup = 0;
		if (fCover) iFollowup += 14;
		if (fSightCover) iFollowup += 12;
		iFollowup -= __min((INT32)30, (INT32)usExposure / 7);
		iFollowup -= __min((INT32)24, iReactionRisk / 5);

		if (!TileIsOutOfBounds(sTargetSpot))
		{
			INT32 iNextDistance = PythSpacesAway(sNext, sTargetSpot);
			INT32 iProgress = iCandidateDistance - iNextDistance;

			if (bIntent == AI_INTENT_PRESS || bIntent == AI_INTENT_FLANK)
				iFollowup += __max(-12, __min(12, 4 * iProgress));
			else if (bIntent == AI_INTENT_FALLBACK ||
				bIntent == AI_INTENT_DISENGAGE)
				iFollowup += __max(-12, __min(12, -4 * iProgress));

			if (bRole == AI_ROLE_FLANKER)
			{
				INT32 iCrossfire =
					AICrossfirePositionScore(pSoldier, sNext, sTargetSpot);
				iFollowup += __max(-8, __min(12, iCrossfire / 2));
			}
		}

		const BOOLEAN fSafeContinuation =
			usExposure <= usCandidateExposure + 20 &&
			iReactionRisk <= 55;
		if (fSafeContinuation)
			++ubSafeOptions;
		if (fSafeContinuation && (fCover || fSightCover))
			++ubCoveredOptions;

		iBestFollowup = __max(iBestFollowup, iFollowup);
	}

	INT32 iOptionValue = 0;
	if (ubSafeOptions == 0)
		iOptionValue -= 22;
	else
		iOptionValue += __min((INT32)16, 4 * (INT32)ubSafeOptions);

	iOptionValue += __min((INT32)12, 4 * (INT32)ubCoveredOptions);
	if (bRole == AI_ROLE_FLANKER || bRole == AI_ROLE_MANEUVER)
		iOptionValue += __min((INT32)8, 2 * (INT32)ubSafeOptions);

	if (iBestFollowup > -10000)
		iOptionValue += __max(-14, __min(14, iBestFollowup / 3));

	return __max(-35, __min(40, iOptionValue));
}

INT32 AIUtilityPositionScore(SOLDIERTYPE *pSoldier, INT32 sCandidateSpot,
	INT32 sTargetSpot, INT8 bIntent, INT8 bRole)
{
	if (!AICombatTeam(pSoldier) || TileIsOutOfBounds(sCandidateSpot))
		return -10000;

	if (TileIsOutOfBounds(sTargetSpot))
		sTargetSpot = AIPrimaryPlanningThreatSpot(pSoldier);
	if (bIntent < AI_INTENT_HOLD || bIntent > AI_INTENT_RESCUE)
		bIntent = AITacticalIntent(pSoldier, sTargetSpot);
	if (bRole < AI_ROLE_SUPPORT || bRole > AI_ROLE_RESERVE)
		bRole = AITacticalRole(pSoldier, sTargetSpot);

	INT8 bMoveAction = AI_ACTION_GET_CLOSER;
	if (bIntent == AI_INTENT_FALLBACK || bIntent == AI_INTENT_DISENGAGE)
		bMoveAction = AI_ACTION_WITHDRAW;
	else if (bIntent == AI_INTENT_FLANK)
		bMoveAction = AI_ACTION_FLANK_LEFT;
	else if (bIntent == AI_INTENT_HOLD)
		bMoveAction = AI_ACTION_TAKE_COVER;

	AITACTICALPOSITIONFEATURES Features;
	if (!AIEvaluateTacticalPosition(
		pSoldier, sCandidateSpot, sTargetSpot,
		DetermineMovementMode(pSoldier, bMoveAction), &Features))
	{
		return -10000;
	}

	INT32 iScore = AIScoreTacticalPosition(
		pSoldier, &Features, sCandidateSpot, sTargetSpot, bIntent, bRole);

	// Second ply: value the legal option set this move creates after the likely
	// enemy response. This uses only map geometry and the bounded local threat picture.
	if (pSoldier->bTeam == ENEMY_TEAM)
		iScore += AIFuturePositionPotential(
			pSoldier, sCandidateSpot, sTargetSpot, bIntent, bRole);

	return __max(-250, __min(250, iScore));
}

INT32 AIPathExposureCost(SOLDIERTYPE *pSoldier, INT32 sDestination, UINT16 usMovementMode)
{
	if (!pSoldier || TileIsOutOfBounds(sDestination) || sDestination == pSoldier->sGridNo)
		return 0;

	INT16 sOldAPBudget = gubNPCAPBudget;
	UINT8 ubOldDistLimit = gubNPCDistLimit;
	gubNPCAPBudget = 0;
	gubNPCDistLimit = 0;

	// Use the non-copying route query: it gives us the full generated path through
	// guiPathingData without replacing the soldier's prepared execution route.
	INT32 iPathSteps = FindBestPath(pSoldier, sDestination, pSoldier->pathing.bLevel,
		usMovementMode, NO_COPYROUTE, 0);

	gubNPCAPBudget = sOldAPBudget;
	gubNPCDistLimit = ubOldDistLimit;

	if (iPathSteps <= 0 || !guiPathingData)
		return 10000;

	INT32 sPathSpot = pSoldier->sGridNo;
	INT32 iCost = 0;
	INT32 iExposedStreak = 0;
	INT32 iPathLimit = __min(iPathSteps, (INT32)MAX_PATH_DATA_LENGTH);

	for (INT32 iStep = 0; iStep < iPathLimit; ++iStep)
	{
		INT32 sNext = NewGridNo(
			sPathSpot, DirectionInc((UINT8)guiPathingData[iStep]));
		if (sNext == sPathSpot || TileIsOutOfBounds(sNext))
			return 10000;

		sPathSpot = sNext;

		// Environmental hazards are hard route penalties, not merely endpoint checks.
		if (InGas(pSoldier, sPathSpot) ||
			RedSmokeDanger(sPathSpot, pSoldier->pathing.bLevel) ||
			FindBombNearby(pSoldier, sPathSpot, BOMB_DETECTION_RANGE))
		{
			return 10000;
		}

		UINT16 usExposure = AIKnownThreatExposure(
			pSoldier, sPathSpot, pSoldier->pathing.bLevel);

		if (InSmoke(sPathSpot, pSoldier->pathing.bLevel))
			usExposure /= 3;

		if (usExposure > 0)
		{
			++iExposedStreak;
			iCost += __min((INT32)45, (INT32)usExposure / 8);

			// Richer route reasoning is intentionally allowed here. We sample inferred
			// reaction risk on every step, but never inspect hidden enemy AP/state.
			iCost += AIInferredReactionRisk(
				pSoldier, sPathSpot, pSoldier->pathing.bLevel) / 6;

			if (!SightCoverAtSpot(pSoldier, sPathSpot, FALSE))
				iCost += 6;
			if (!AnyCoverAtSpot(pSoldier, sPathSpot))
				iCost += 4;

			iCost += __min((INT32)16, 2 * iExposedStreak);

			if (InLightAtNight(sPathSpot, pSoldier->pathing.bLevel))
				iCost += 4;
		}
		else
		{
			iExposedStreak = 0;
		}

		// Battle-local experience also applies to the route itself. A destination
		// can be attractive while the direct path crosses the corner/doorway where
		// this fireteam was just surprised or had an approach rejected.
		iCost += __min(
			(INT32)18,
			AITacticalSetbackPenalty(pSoldier, sPathSpot) / 4);
	}

	return __min((INT32)700, iCost);
}
