#ifdef PRECOMPILEDHEADERS
#include "AI All.h"
#else
#include "ai.h"
#include "AIInternals.h"
#include "Isometric utils.h"
#include "Points.h"
#include "overhead.h"
#include "opplist.h"
#include "items.h"
#include "Weapons.h"
#include "NPC.h"
#include "Soldier Functions.h"
#include "worldman.h"
#include "Scheduling.h"
#include "Message.h"
#include "Structure Wrap.h"
#include "Keys.h"
#include "pathai.h"
#include "Render Fun.h"
#include "Boxing.h"
//	#include "Air Raid.h"
#include "Soldier Profile.h"
#include "soldier profile type.h"
#include "Soldier macros.h"
#include "los.h"
#include "Buildings.h"
#include "strategicmap.h"
#include "Quests.h"
#include "Map Screen Interface Map.h"
#include "soldier ani.h"
#include "rotting corpses.h"
#include "GameSettings.h"
#include "Dialogue Control.h"
#endif
#include "connect.h"
#include "Text.h"
#include "Game Clock.h"			// sevenfm
#include "Rotting Corpses.h"	// sevenfm
#include "Map Edgepoints.h"	// Chunk 5 escape route planning
#include "MilitiaSquads.h"	// strategic militia retreat handoff

//////////////////////////////////////////////////////////////////////////////
// SANDRO - In this file, all APBPConstants[AP_CROUCH] and APBPConstants[AP_PRONE] were changed to GetAPsCrouch() and GetAPsProne()
//			On the bottom here, there are these functions made
//////////////////////////////////////////////////////////////////////

extern BOOLEAN gfHiddenInterrupt;
extern BOOLEAN gfUseAlternateQueenPosition;
extern UINT16 PickSoldierReadyAnimation( SOLDIERTYPE *pSoldier, BOOLEAN fEndReady, BOOLEAN fHipStance );
extern BOOLEAN WatchedLocLocationIsEmpty( INT32 sGridNo, INT8 bLevel, INT8 bTeam );
extern void IncrementWatchedLoc(UINT8 ubID, INT32 sGridNo, INT8 bLevel);

STR8 gStr8AlertStatus[] = { "Green", "Yellow", "Red", "Black" };
STR8 gStr8Attitude[] = { "DEFENSIVE", "BRAVESOLO", "BRAVEAID", "CUNNINGSOLO", "CUNNINGAID", "AGGRESSIVE", "MAXATTITUDES", "ATTACKSLAYONLY" };
STR8 gStr8Orders[] = { "STATIONARY", "ONGUARD", "CLOSEPATROL", "FARPATROL", "POINTPATROL", "ONCALL", "SEEKENEMY", "RNDPTPATROL", "SNIPER" };
STR8 gStr8Team[] = { "OUR_TEAM", "ENEMY_TEAM", "CREATURE_TEAM", "MILITIA_TEAM", "CIV_TEAM", "LAST_TEAM", "PLAYER_PLAN", "LAN_TEAM_ONE", "LAN_TEAM_TWO", "LAN_TEAM_THREE", "LAN_TEAM_FOUR" };
STR8 gStr8Class[] = { "SOLDIER_CLASS_NONE", "SOLDIER_CLASS_ADMINISTRATOR", "SOLDIER_CLASS_ELITE", "SOLDIER_CLASS_ARMY", "SOLDIER_CLASS_GREEN_MILITIA", "SOLDIER_CLASS_REG_MILITIA", "SOLDIER_CLASS_ELITE_MILITIA", "SOLDIER_CLASS_CREATURE", "SOLDIER_CLASS_MINER", "SOLDIER_CLASS_ZOMBIE" };
STR8 gStr8Knowledge[] = { "HEARD_3_TURNS_AGO", "HEARD_2_TURNS_AGO", "HEARD_LAST_TURN", "HEARD_THIS_TURN", "NOT_HEARD_OR_SEEN", "SEEN_CURRENTLY", "SEEN_THIS_TURN", "SEEN_LAST_TURN", "SEEN_2_TURNS_AGO", "SEEN_3_TURNS_AGO" };

extern UINT32 guiTurnCnt;

// Contact-local reinforcement pacing. Kept outside SOLDIERTYPE/savegames.
#define AI_RESPONSE_EPISODES 3
#define AI_RESPONSE_QUIET_TURNS 2

typedef struct
{
	INT32 sSpot;
	UINT32 uiStartTurn;
	UINT32 uiLastEvidenceTurn;
	INT16 sSectorX;
	INT16 sSectorY;
	INT8 bSectorZ;
	INT8 bTeam;
} AI_RESPONSE_EPISODE;

static AI_RESPONSE_EPISODE gAIEnemyResponse[AI_RESPONSE_EPISODES];
static BOOLEAN gfAIEnemyResponseInitialized = FALSE;
static UINT32 guiAIEnemyResponseLastTurnStamp = 0;

static void AIResetEnemyResponseEpisodes(void)
{
	for (UINT8 i = 0; i < AI_RESPONSE_EPISODES; ++i)
	{
		gAIEnemyResponse[i].sSpot = NOWHERE;
		gAIEnemyResponse[i].uiStartTurn = 0;
		gAIEnemyResponse[i].uiLastEvidenceTurn = 0;
		gAIEnemyResponse[i].sSectorX = -1;
		gAIEnemyResponse[i].sSectorY = -1;
		gAIEnemyResponse[i].bSectorZ = -1;
		gAIEnemyResponse[i].bTeam = -1;
	}
	gfAIEnemyResponseInitialized = TRUE;
}

static void AIMaintainEnemyResponseEpisodes(void)
{
	UINT32 uiTurnStamp = guiTurnCnt + 1;
	if (!gfAIEnemyResponseInitialized ||
		(guiAIEnemyResponseLastTurnStamp != 0 && uiTurnStamp < guiAIEnemyResponseLastTurnStamp))
		AIResetEnemyResponseEpisodes();

	guiAIEnemyResponseLastTurnStamp = uiTurnStamp;

	for (UINT8 i = 0; i < AI_RESPONSE_EPISODES; ++i)
	{
		AI_RESPONSE_EPISODE *pEpisode = &gAIEnemyResponse[i];
		if (TileIsOutOfBounds(pEpisode->sSpot))
			continue;

		BOOLEAN fWrongSector =
			pEpisode->sSectorX != gWorldSectorX ||
			pEpisode->sSectorY != gWorldSectorY ||
			pEpisode->bSectorZ != gbWorldSectorZ;
		BOOLEAN fQuietExpired =
			pEpisode->uiLastEvidenceTurn != 0 &&
			uiTurnStamp > pEpisode->uiLastEvidenceTurn + AI_RESPONSE_QUIET_TURNS;

		if (fWrongSector || fQuietExpired)
		{
			pEpisode->sSpot = NOWHERE;
			pEpisode->uiStartTurn = 0;
			pEpisode->uiLastEvidenceTurn = 0;
			pEpisode->sSectorX = -1;
			pEpisode->sSectorY = -1;
			pEpisode->bSectorZ = -1;
			pEpisode->bTeam = -1;
		}
	}
}

static INT8 AIGetEnemyResponseEpisode(INT8 bTeam, INT32 sContactSpot, BOOLEAN fRefreshEvidence)
{
	if ((bTeam != ENEMY_TEAM && bTeam != MILITIA_TEAM) || TileIsOutOfBounds(sContactSpot))
		return -1;

	AIMaintainEnemyResponseEpisodes();
	INT8 bBest = -1;
	INT32 iBestDistance = 10000;
	for (UINT8 i = 0; i < AI_RESPONSE_EPISODES; ++i)
	{
		AI_RESPONSE_EPISODE *pEpisode = &gAIEnemyResponse[i];
		if (TileIsOutOfBounds(pEpisode->sSpot) || pEpisode->bTeam != bTeam ||
			pEpisode->sSectorX != gWorldSectorX || pEpisode->sSectorY != gWorldSectorY ||
			pEpisode->bSectorZ != gbWorldSectorZ)
			continue;
		INT32 iDistance = PythSpacesAway(pEpisode->sSpot, sContactSpot);
		if (iDistance <= TACTICAL_RANGE / 2 && iDistance < iBestDistance)
		{
			iBestDistance = iDistance;
			bBest = (INT8)i;
		}
	}

	if (bBest < 0 && fRefreshEvidence)
	{
		UINT32 uiOldest = 0xFFFFFFFF;
		for (UINT8 i = 0; i < AI_RESPONSE_EPISODES; ++i)
		{
			AI_RESPONSE_EPISODE *pEpisode = &gAIEnemyResponse[i];
			if (TileIsOutOfBounds(pEpisode->sSpot)) { bBest = (INT8)i; break; }
			if (pEpisode->uiLastEvidenceTurn < uiOldest)
			{
				uiOldest = pEpisode->uiLastEvidenceTurn;
				bBest = (INT8)i;
			}
		}
		if (bBest >= 0)
		{
			UINT32 uiTurnStamp = guiTurnCnt + 1;
			AI_RESPONSE_EPISODE *pEpisode = &gAIEnemyResponse[bBest];
			pEpisode->sSpot = sContactSpot;
			pEpisode->uiStartTurn = uiTurnStamp;
			pEpisode->uiLastEvidenceTurn = uiTurnStamp;
			pEpisode->sSectorX = gWorldSectorX;
			pEpisode->sSectorY = gWorldSectorY;
			pEpisode->bSectorZ = gbWorldSectorZ;
			pEpisode->bTeam = bTeam;
		}
	}
	else if (bBest >= 0 && fRefreshEvidence)
	{
		AI_RESPONSE_EPISODE *pEpisode = &gAIEnemyResponse[bBest];
		pEpisode->uiLastEvidenceTurn = guiTurnCnt + 1;
		if (PythSpacesAway(pEpisode->sSpot, sContactSpot) > DAY_VISION_RANGE / 4)
			pEpisode->sSpot = sContactSpot;
	}
	return bBest;
}

static void AIEvaluateLocalResponseNeed(
	SOLDIERTYPE *pEngaged, UINT8 *pubPerceivedEnemies,
	UINT8 *pubDesiredResponse, INT32 *piUrgency)
{
	if (pubPerceivedEnemies)
		*pubPerceivedEnemies = 0;
	if (pubDesiredResponse)
		*pubDesiredResponse = 0;
	if (piUrgency)
		*piUrgency = 0;

	if (!pEngaged)
		return;

	UINT16 usPerceivedEnemyStrength = AIPerceivedEnemyStrength(pEngaged);
	UINT8 ubPerceivedEnemies = (UINT8)__max(1, __min(12,
		(INT32)(usPerceivedEnemyStrength + 99) / 100));
	UINT8 ubDesiredResponse = (UINT8)__min(14, __max(4,
		(INT32)ubPerceivedEnemies + 2));
	INT32 iUrgency = 0;

	INT8 bSituation = AIBattleSituation(pEngaged);
	UINT8 ubCasualties = AILocalCasualtyPercent(pEngaged);
	UINT8 ubRoutPressure = AILocalRoutPressure(pEngaged);

	if (bSituation == AI_BATTLE_CATASTROPHIC)
	{
		ubDesiredResponse = (UINT8)__min(14,
			__max((INT32)ubDesiredResponse, (INT32)ubPerceivedEnemies + 6));
		iUrgency = 40;
	}
	else if (bSituation == AI_BATTLE_LOSING)
	{
		ubDesiredResponse = (UINT8)__min(12,
			__max((INT32)ubDesiredResponse, (INT32)ubPerceivedEnemies + 4));
		iUrgency = 25;
	}
	else if (ubCasualties >= 25 || ubRoutPressure >= 45)
	{
		ubDesiredResponse = (UINT8)__min(11,
			__max((INT32)ubDesiredResponse, (INT32)ubPerceivedEnemies + 3));
		iUrgency = 15;
	}

	if (pubPerceivedEnemies)
		*pubPerceivedEnemies = ubPerceivedEnemies;
	if (pubDesiredResponse)
		*pubDesiredResponse = ubDesiredResponse;
	if (piUrgency)
		*piUrgency = iUrgency;
}

static UINT8 AIEnemyResponseLimitForContact(
	SOLDIERTYPE *pSoldier, INT32 sContactSpot, INT32 *piReinforcementUrgency)
{
	// The response budget belongs to the contact, not to whichever individual
	// responder happens to be taking his AI turn.  A responder's orders still
	// affect his willingness to move below, but cannot give two members of the
	// same fireteam different release budgets.
	const UINT8 ubDefaultResponse = 4;
	if (piReinforcementUrgency)
		*piReinforcementUrgency = 0;

	if (!AICombatTeam(pSoldier) ||
		TileIsOutOfBounds(sContactSpot) ||
		!gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition)
		return ubDefaultResponse;

	BOOLEAN fLocalEngagement = FALSE;
	SOLDIERTYPE *pResponseAnchor = NULL;
	UINT8 ubPerceivedEnemies = 0;
	UINT8 ubDesiredResponse = 0;
	INT32 iUrgency = 0;
	INT32 iBestDistance = 10000;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pEngaged = MercPtrs[iCounter];
		if (!pEngaged || !pEngaged->bActive || !pEngaged->bInSector ||
			pEngaged->stats.bLife < OKLIFE || pEngaged->bCollapsed || pEngaged->bBreathCollapsed ||
			(pEngaged->usSoldierFlagMask & SOLDIER_POW))
			continue;

		if (!pEngaged->aiData.bUnderFire &&
			pEngaged->aiData.bOppCnt == 0 &&
			!GuySawEnemy(pEngaged, SEEN_LAST_TURN))
			continue;

		INT32 iEngagedDistance = PythSpacesAway(pEngaged->sGridNo, sContactSpot);
		if (iEngagedDistance > TACTICAL_RANGE)
			continue;

		UINT8 ubElementEnemies = 0;
		UINT8 ubElementDesired = 0;
		INT32 iElementUrgency = 0;
		AIEvaluateLocalResponseNeed(
			pEngaged, &ubElementEnemies, &ubElementDesired, &iElementUrgency);

		// Keep force ratio, casualty state, rout state and doctrine from the same
		// local element.  This makes the response decision deterministic for every
		// potential responder evaluating the same contact.
		if (!fLocalEngagement ||
			ubElementDesired > ubDesiredResponse ||
			(ubElementDesired == ubDesiredResponse && iElementUrgency > iUrgency) ||
			(ubElementDesired == ubDesiredResponse && iElementUrgency == iUrgency &&
			 iEngagedDistance < iBestDistance))
		{
			fLocalEngagement = TRUE;
			pResponseAnchor = pEngaged;
			ubPerceivedEnemies = ubElementEnemies;
			ubDesiredResponse = ubElementDesired;
			iUrgency = iElementUrgency;
			iBestDistance = iEngagedDistance;
		}
	}

	if (!fLocalEngagement || !pResponseAnchor)
		return ubDefaultResponse;

	if (piReinforcementUrgency)
		*piReinforcementUrgency = iUrgency;

	INT8 bEpisode = AIGetEnemyResponseEpisode(pSoldier->bTeam, sContactSpot, TRUE);
	UINT32 uiElapsedTurns = 0;
	if (bEpisode >= 0)
	{
		AI_RESPONSE_EPISODE *pEpisode = &gAIEnemyResponse[bEpisode];
		UINT32 uiTurnStamp = guiTurnCnt + 1;
		uiElapsedTurns = (uiTurnStamp > pEpisode->uiStartTurn) ?
			(uiTurnStamp - pEpisode->uiStartTurn) : 0;
	}

	UINT8 ubInitialWave = (UINT8)__max(4, __min(7, (INT32)ubPerceivedEnemies));
	UINT8 ubWaveCap = (UINT8)__min((INT32)ubDesiredResponse,
		(INT32)ubInitialWave + 2 * (INT32)uiElapsedTurns);

	// Doctrine belongs to the element actually in contact.  Individual QRF members
	// may have different orders/classes, but those differences must not split a
	// fireteam into contradictory release/hold decisions.
	UINT8 ubDoctrineBaseline = AIDoctrineResponseLimit(pResponseAnchor);
	UINT8 ubDoctrine = AIGetDoctrineProfile(pResponseAnchor);
	if (ubDoctrine == AI_DOCTRINE_SECURITY &&
		pResponseAnchor->aiData.bOrders != ONCALL &&
		pResponseAnchor->aiData.bOrders != SEEKENEMY)
	{
		ubWaveCap = __min((UINT8)3, ubWaveCap);
	}
	else if (ubDoctrine == AI_DOCTRINE_ELITE_GUARD)
	{
		ubWaveCap = __min((UINT8)5, ubWaveCap);
	}

	return __max(ubDoctrineBaseline, ubWaveCap);
}

static INT8 DecideYellowRemoteRadioSupport(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || !gGameExternalOptions.bNewTacticalAIBehavior ||
		pSoldier->bTeam != ENEMY_TEAM || !SoldierAI(pSoldier) ||
		!gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition ||
		HAS_SKILL_TRAIT(pSoldier, RADIO_OPERATOR_NT) == 0 ||
		!pSoldier->CanUseSkill(SKILLS_RADIO_ARTILLERY, TRUE))
		return AI_ACTION_NONE;

	UINT32 uiSector = 0;
	INT32 sTarget = NOWHERE;
	if (!pSoldier->CanAnyArtilleryStrikeBeOrdered(&uiSector))
		return AI_ACTION_NONE;

	if (SectorJammed())
	{
		if (pSoldier->IsJamming())
		{
			pSoldier->usAISkillUse = SKILLS_RADIO_TURNOFF;
			pSoldier->aiData.usActionData = NOWHERE;
			return AI_ACTION_USE_SKILL;
		}
		return AI_ACTION_NONE;
	}

	if (AISelectKnownArtilleryTarget(pSoldier, &sTarget))
	{
		pSoldier->usAISkillUse = SKILLS_RADIO_ARTILLERY;
		pSoldier->aiData.usActionData = sTarget;
		return AI_ACTION_USE_SKILL;
	}

	return AI_ACTION_NONE;
}

// global status time counters to determine what takes the most time

#define CENTER_OF_RING 11237//dnl!!!

void LogDecideInfo(SOLDIERTYPE *pSoldier);
void LogKnowledgeInfo(SOLDIERTYPE *pSoldier);

INT8 ZombieDecideActionGreen(SOLDIERTYPE *pSoldier);
INT8 ZombieDecideActionYellow(SOLDIERTYPE *pSoldier);
INT8 ZombieDecideActionRed(SOLDIERTYPE *pSoldier);
INT8 ZombieDecideActionBlack(SOLDIERTYPE *pSoldier);

void DoneScheduleAction( SOLDIERTYPE * pSoldier )
{
	pSoldier->aiData.fAIFlags &= (~AI_CHECK_SCHEDULE);
	pSoldier->bAIScheduleProgress = 0;
	PostNextSchedule( pSoldier );
}

INT8 DecideActionSchedule( SOLDIERTYPE * pSoldier )
{
	SCHEDULENODE *		pSchedule;
	INT32							iScheduleIndex;
	UINT8							ubScheduleAction;
	INT32 usGridNo1, usGridNo2;
	INT16							sX, sY;
	INT8							bDirection;
	STRUCTURE *				pStructure;
	BOOLEAN						fDoUseDoor;
	DOOR_STATUS	*			pDoorStatus;

	pSchedule = GetSchedule( pSoldier->ubScheduleID );
	if (!pSchedule)
	{
		return( AI_ACTION_NONE );
	}

	if (pSchedule->usFlags & SCHEDULE_FLAGS_ACTIVE1)
	{
		iScheduleIndex = 0;
	}
	else if (pSchedule->usFlags & SCHEDULE_FLAGS_ACTIVE2)
	{
		iScheduleIndex = 1;
	}
	else if (pSchedule->usFlags & SCHEDULE_FLAGS_ACTIVE3)
	{
		iScheduleIndex = 2;
	}
	else if (pSchedule->usFlags & SCHEDULE_FLAGS_ACTIVE4)
	{
		iScheduleIndex = 3;
	}
	else
	{
		// error!
		return( AI_ACTION_NONE );
	}

	ubScheduleAction = pSchedule->ubAction[ iScheduleIndex ];
	usGridNo1 = pSchedule->usData1[ iScheduleIndex ];
	usGridNo2 = pSchedule->usData2[ iScheduleIndex ];

	// assume soldier is awake unless the action is a sleep
	pSoldier->aiData.fAIFlags &= ~(AI_ASLEEP);

	switch( ubScheduleAction )
	{
	case SCHEDULE_ACTION_LOCKDOOR:
		//Uses first gridno for locking door, then second to move to after door is locked.
		//It is possible that the second gridno will border the edge of the map, meaning that
		//the individual will walk off of the map.
		//If this is a "merchant", make sure that nobody occupies the building/room.

		switch( pSoldier->bAIScheduleProgress )
		{
		case 0: // move to gridno specified
			if (pSoldier->sGridNo == usGridNo1)
			{
				pSoldier->bAIScheduleProgress++;
				// fall through
			}
			else
			{
				pSoldier->aiData.usActionData = usGridNo1;
				pSoldier->sAbsoluteFinalDestination = pSoldier->aiData.usActionData;
				return( AI_ACTION_SCHEDULE_MOVE );
			}
			// fall through
		case 1:
			// start the door open: find the door...
			usGridNo1 = FindDoorAtGridNoOrAdjacent( usGridNo1 );
			
			if (TileIsOutOfBounds(usGridNo1))
			{
				// do nothing right now!
				return( AI_ACTION_NONE );
			}

			pDoorStatus = GetDoorStatus( usGridNo1 );
			if (pDoorStatus && pDoorStatus->ubFlags & DOOR_BUSY)
			{
				// do nothing right now!
				return( AI_ACTION_NONE );
			}

			pStructure = FindStructure( usGridNo1, STRUCTURE_ANYDOOR );
			if (pStructure == NULL)
			{
				fDoUseDoor = FALSE;
			}
			else
			{
				// action-specific tests to not handle the door
				fDoUseDoor = TRUE;

				if (pStructure->fFlags & STRUCTURE_OPEN)
				{
					// not only do we have to lock the door but
					// close it too!
					pSoldier->aiData.fAIFlags |= AI_LOCK_DOOR_INCLUDES_CLOSE;
				}
				else
				{
					DOOR * pDoor;

					pDoor = FindDoorInfoAtGridNo( usGridNo1 );
					if (pDoor)
					{
						if (pDoor->fLocked)
						{
							// door already locked!
							fDoUseDoor = FALSE;
						}
						else
						{
							pDoor->fLocked = TRUE;
						}
					}
					else
					{
						ScreenMsg( FONT_MCOLOR_LTYELLOW, MSG_BETAVERSION, L"Schedule involved locked door at %d but there's no lock there!", usGridNo1 );
						fDoUseDoor = FALSE;
					}
				}
			}

			if (fDoUseDoor)
			{
				pSoldier->aiData.usActionData = usGridNo1;
				return( AI_ACTION_LOCK_DOOR );
			}

			// the door is already in the desired state, or it doesn't exist!
			pSoldier->bAIScheduleProgress++;
			// fall through

		case 2:			
			if (pSoldier->sGridNo == usGridNo2 || TileIsOutOfBounds(pSoldier->sGridNo))
			{
				// NOWHERE indicates we were supposed to go off map and have done so
				DoneScheduleAction( pSoldier );
				
				if (!TileIsOutOfBounds(pSoldier->sGridNo))
				{
					pSoldier->aiData.sPatrolGrid[0] = pSoldier->sGridNo;
				}
			}
			else
			{
				if ( GridNoOnEdgeOfMap( usGridNo2, &bDirection ) )
				{
					// told to go to edge of map, so go off at that point!
					pSoldier->ubQuoteActionID = GetTraversalQuoteActionID( bDirection );
				}
				pSoldier->aiData.usActionData = usGridNo2;
				pSoldier->sAbsoluteFinalDestination = pSoldier->aiData.usActionData;
				return( AI_ACTION_SCHEDULE_MOVE );
			}
			break;
		}
		break;


	case SCHEDULE_ACTION_UNLOCKDOOR:
	case SCHEDULE_ACTION_OPENDOOR:
	case SCHEDULE_ACTION_CLOSEDOOR:
		//Uses first gridno for opening/closing/unlocking door, then second to move to after door is opened.
		//It is possible that the second gridno will border the edge of the map, meaning that
		//the individual will walk off of the map.
		switch( pSoldier->bAIScheduleProgress )
		{
		case 0: // move to gridno specified
			if (pSoldier->sGridNo == usGridNo1)
			{
				pSoldier->bAIScheduleProgress++;
				// fall through
			}
			else
			{
				pSoldier->aiData.usActionData = usGridNo1;
				pSoldier->sAbsoluteFinalDestination = pSoldier->aiData.usActionData;
				return( AI_ACTION_SCHEDULE_MOVE );
			}
			// fall through
		case 1:
			// start the door open: find the door...
			usGridNo1 = FindDoorAtGridNoOrAdjacent( usGridNo1 );
			
			if (TileIsOutOfBounds(usGridNo1))
			{
				// do nothing right now!
				return( AI_ACTION_NONE );
			}

			pDoorStatus = GetDoorStatus( usGridNo1 );
			if (pDoorStatus && pDoorStatus->ubFlags & DOOR_BUSY)
			{
				// do nothing right now!
				return( AI_ACTION_NONE );
			}

			pStructure = FindStructure( usGridNo1, STRUCTURE_ANYDOOR );
			if (pStructure == NULL)
			{
				fDoUseDoor = FALSE;
			}
			else
			{
				fDoUseDoor = TRUE;

				// action-specific tests to not handle the door
				switch( ubScheduleAction )
				{
				case SCHEDULE_ACTION_UNLOCKDOOR:
					if (pStructure->fFlags & STRUCTURE_OPEN)
					{
						// door is already open!
						fDoUseDoor = FALSE;
					}
					else
					{
						// set the door to unlocked
						DOOR * pDoor;

						pDoor = FindDoorInfoAtGridNo( usGridNo1 );
						if (pDoor)
						{
							if (pDoor->fLocked)
							{
								pDoor->fLocked = FALSE;
							}
							else
							{
								// door already unlocked!
								fDoUseDoor = FALSE;
							}
						}
						else
						{
							// WTF?  Warning time!
							ScreenMsg( FONT_MCOLOR_LTYELLOW, MSG_BETAVERSION, L"Schedule involved locked door at %d but there's no lock there!", usGridNo1 );
							fDoUseDoor = FALSE;
						}
					}
					break;
				case SCHEDULE_ACTION_OPENDOOR:
					if (pStructure->fFlags & STRUCTURE_OPEN)
					{
						// door is already open!
						fDoUseDoor = FALSE;
					}
					break;
				case SCHEDULE_ACTION_CLOSEDOOR:
					if ( !(pStructure->fFlags & STRUCTURE_OPEN) )
					{
						// door is already closed!
						fDoUseDoor = FALSE;
					}
					break;
				default:
					break;
				}
			}

			if (fDoUseDoor)
			{
				pSoldier->aiData.usActionData = usGridNo1;
				if (ubScheduleAction == SCHEDULE_ACTION_UNLOCKDOOR)
				{
					return( AI_ACTION_UNLOCK_DOOR );
				}
				else
				{
					return( AI_ACTION_OPEN_OR_CLOSE_DOOR );
				}
			}

			// the door is already in the desired state, or it doesn't exist!
			pSoldier->bAIScheduleProgress++;
			// fall through

		case 2:			
			if (pSoldier->sGridNo == usGridNo2 || TileIsOutOfBounds(pSoldier->sGridNo))
			{
				// NOWHERE indicates we were supposed to go off map and have done so
				DoneScheduleAction( pSoldier );				
				if (!TileIsOutOfBounds(pSoldier->sGridNo))
				{
					pSoldier->aiData.sPatrolGrid[0] = pSoldier->sGridNo;
				}
			}
			else
			{
				if ( GridNoOnEdgeOfMap( usGridNo2, &bDirection ) )
				{
					// told to go to edge of map, so go off at that point!
					pSoldier->ubQuoteActionID = GetTraversalQuoteActionID( bDirection );
				}
				pSoldier->aiData.usActionData = usGridNo2;
				pSoldier->sAbsoluteFinalDestination = pSoldier->aiData.usActionData;
				return( AI_ACTION_SCHEDULE_MOVE );
			}
			break;
		}
		break;

	case SCHEDULE_ACTION_GRIDNO:
		// Only uses the first gridno
		if ( pSoldier->sGridNo == usGridNo1 )
		{
			// done!
			DoneScheduleAction( pSoldier );			
			if (!TileIsOutOfBounds(pSoldier->sGridNo))
			{
				pSoldier->aiData.sPatrolGrid[0] = pSoldier->sGridNo;
			}
		}
		else
		{
			// move!
			pSoldier->aiData.usActionData = usGridNo1;
			pSoldier->sAbsoluteFinalDestination = pSoldier->aiData.usActionData;
			return( AI_ACTION_SCHEDULE_MOVE );
		}
		break;
	case SCHEDULE_ACTION_LEAVESECTOR:
		//Doesn't use any gridno data
		switch( pSoldier->bAIScheduleProgress )
		{
		case 0: // start the action

			pSoldier->aiData.usActionData = FindNearestEdgePoint( pSoldier->sGridNo );
			
			if (TileIsOutOfBounds(pSoldier->aiData.usActionData))
			{
				DoneScheduleAction( pSoldier );
				return( AI_ACTION_NONE );
			}

			if ( pSoldier->sGridNo == pSoldier->aiData.usActionData )
			{
				// time to go off the map
				pSoldier->bAIScheduleProgress++;
			}
			else
			{
				// move!
				pSoldier->sAbsoluteFinalDestination = pSoldier->aiData.usActionData;
				return( AI_ACTION_SCHEDULE_MOVE );
			}

			// fall through

		case 1: // near edge

			pSoldier->aiData.usActionData = FindNearbyPointOnEdgeOfMap( pSoldier, &bDirection );
			
			if (TileIsOutOfBounds(pSoldier->aiData.usActionData))
			{
				// what the heck??
				// ABORT!
				DoneScheduleAction( pSoldier );
			}
			else
			{
				pSoldier->ubQuoteActionID = GetTraversalQuoteActionID( bDirection );
				pSoldier->bAIScheduleProgress++;
				pSoldier->sAbsoluteFinalDestination = pSoldier->aiData.usActionData;
				return( AI_ACTION_SCHEDULE_MOVE );
			}
			break;

		case 2: // should now be done!
			DoneScheduleAction( pSoldier );
			break;

		default:
			break;
		}
		break;

	case SCHEDULE_ACTION_ENTERSECTOR:
		if ( pSoldier->ubProfile != NO_PROFILE && gMercProfiles[ pSoldier->ubProfile ].ubMiscFlags2 & PROFILE_MISC_FLAG2_DONT_ADD_TO_SECTOR )//Moa: changed 'ubMiscFlags' to 'ubMiscFlags2'
		{
			// ignore.
			DoneScheduleAction( pSoldier );
			break;
		}
		switch( pSoldier->bAIScheduleProgress )
		{
		case 0:
			sX = CenterX( pSoldier->sOffWorldGridNo );
			sY = CenterY( pSoldier->sOffWorldGridNo );
			pSoldier->EVENT_SetSoldierPosition( sX, sY );
			pSoldier->bInSector = TRUE;
			MoveSoldierFromAwayToMercSlot( pSoldier );
			pSoldier->aiData.usActionData = usGridNo1;
			pSoldier->bAIScheduleProgress++;
			pSoldier->sAbsoluteFinalDestination = pSoldier->aiData.usActionData;
			return( AI_ACTION_SCHEDULE_MOVE );
		case 1:
			if (pSoldier->sGridNo == usGridNo1)
			{
				DoneScheduleAction( pSoldier );
				
				if (!TileIsOutOfBounds(pSoldier->sGridNo))
				{
					pSoldier->aiData.sPatrolGrid[0] = pSoldier->sGridNo;
				}
			}
			else
			{
				pSoldier->aiData.usActionData = usGridNo1;
				pSoldier->sAbsoluteFinalDestination = pSoldier->aiData.usActionData;
				return( AI_ACTION_SCHEDULE_MOVE );
			}
			break;
		}
		break;

	case SCHEDULE_ACTION_WAKE:
		// Go to this position
		if (pSoldier->sGridNo == pSoldier->sInitialGridNo)
		{
			// th-th-th-that's it!
			DoneScheduleAction( pSoldier );
			pSoldier->aiData.sPatrolGrid[0] = pSoldier->sGridNo;
		}
		else
		{
			pSoldier->aiData.usActionData = pSoldier->sInitialGridNo;
			pSoldier->sAbsoluteFinalDestination = pSoldier->aiData.usActionData;
			return( AI_ACTION_SCHEDULE_MOVE );
		}
		break;

	case SCHEDULE_ACTION_SLEEP:
		// Go to this position
		if (pSoldier->sGridNo == usGridNo1)
		{
			// Sleep
			pSoldier->aiData.fAIFlags |= AI_ASLEEP;
			DoneScheduleAction( pSoldier );
			
			if (!TileIsOutOfBounds(pSoldier->sGridNo))
			{
				pSoldier->aiData.sPatrolGrid[0] = pSoldier->sGridNo;
			}
		}
		else
		{
			pSoldier->aiData.usActionData = usGridNo1;
			pSoldier->sAbsoluteFinalDestination = pSoldier->aiData.usActionData;
			return( AI_ACTION_SCHEDULE_MOVE );
		}
		break;
	}


	return( AI_ACTION_NONE );
}

INT8 DecideActionBoxerEnteringRing(SOLDIERTYPE *pSoldier)
{
	//DBrot: More Rooms
	//UINT8 ubRoom;
	UINT16 usRoom;
	INT32	sDesiredMercLoc;
	UINT8 ubDesiredMercDir;

	// boxer, should move into ring!
	if ( InARoom( pSoldier->sGridNo, &usRoom ))
	{
		if (usRoom == BOXING_RING)
		{
			// look towards nearest player
			sDesiredMercLoc = ClosestPC( pSoldier, NULL );
			
			if (!TileIsOutOfBounds(sDesiredMercLoc))
			{
				// see if we are facing this person
				ubDesiredMercDir = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(sDesiredMercLoc),CenterY(sDesiredMercLoc));

				// if not already facing in that direction,
				if ( pSoldier->ubDirection != ubDesiredMercDir && pSoldier->InternalIsValidStance( ubDesiredMercDir, gAnimControl[ pSoldier->usAnimState ].ubEndHeight ) )
				{

					pSoldier->aiData.usActionData = ubDesiredMercDir;

					return( AI_ACTION_CHANGE_FACING );
				}
			}
			return( AI_ACTION_ABSOLUTELY_NONE );
		}
		else
		{
			// move to starting spot
			pSoldier->aiData.usActionData = FindClosestBoxingRingSpot( pSoldier, TRUE );
			return( AI_ACTION_GET_CLOSER );
		}
	}

	return( AI_ACTION_ABSOLUTELY_NONE );
}

INT8 DecideActionNamedNPC( SOLDIERTYPE * pSoldier )
{
	INT32 sDesiredMercLoc;
	UINT8	ubDesiredMercDir;
	UINT8	ubDesiredMerc;
	INT32	sDesiredMercDist;

	// if a quote record has been set and we're not doing movement, then
	// it means we have to wait until someone is nearby and then see
	// to do...

	// is this person close enough to trigger event?
	if (pSoldier->ubQuoteRecord && pSoldier->ubQuoteActionID == QUOTE_ACTION_ID_TURNTOWARDSPLAYER )
	{
		sDesiredMercLoc = ClosestPC( pSoldier, &sDesiredMercDist );
		
		if (!TileIsOutOfBounds(sDesiredMercLoc))
		{
			if ( sDesiredMercDist <= NPC_TALK_RADIUS * 2)
			{
				pSoldier->ubQuoteRecord = 0;
				// see if this triggers a conversation/NPC record
				PCsNearNPC( pSoldier->ubProfile );
				// clear "handle every frame" flag
				pSoldier->aiData.fAIFlags &= (~AI_HANDLE_EVERY_FRAME);
				return( AI_ACTION_ABSOLUTELY_NONE );
			}

			// see if we are facing this person
			ubDesiredMercDir = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(sDesiredMercLoc),CenterY(sDesiredMercLoc));

			// if not already facing in that direction,
			if (pSoldier->ubDirection != ubDesiredMercDir && pSoldier->InternalIsValidStance( ubDesiredMercDir, gAnimControl[ pSoldier->usAnimState ].ubEndHeight ) )
			{

				pSoldier->aiData.usActionData = ubDesiredMercDir;

				return( AI_ACTION_CHANGE_FACING );
			}
		}

		// do nothing; we're looking at the PC or the NPC is far away
		return( AI_ACTION_ABSOLUTELY_NONE );

	}
	else
	{
		///////////////
		// CHECK TO SEE IF WE WANT TO GO UP TO PERSON AND SAY SOMETHING
		///////////////
		pSoldier->aiData.usActionData = NPCConsiderInitiatingConv( pSoldier, &ubDesiredMerc );
		
		if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
		{
			return( AI_ACTION_APPROACH_MERC );
		}
	}

	if ( pSoldier->IsAssassin() )
	{
		sDesiredMercLoc = ClosestPC( pSoldier, &sDesiredMercDist );
		
		if (!TileIsOutOfBounds(sDesiredMercLoc))
		{
			if ( sDesiredMercDist <= NPC_TALK_RADIUS * 2 )
			{
				AddToShouldBecomeHostileOrSayQuoteList( pSoldier->ubID );
				// now wait a bit!
				pSoldier->aiData.usActionData = 5000;
				return( AI_ACTION_WAIT );
			}
			else
			{
				pSoldier->aiData.usActionData = GoAsFarAsPossibleTowards( pSoldier, sDesiredMercLoc, AI_ACTION_APPROACH_MERC );
				
				if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
				{
					return( AI_ACTION_APPROACH_MERC );
				}
			}
		}
	}

	return( AI_ACTION_NONE );
}


INT8 DecideActionGreen(SOLDIERTYPE *pSoldier)
{
	DOUBLE iChance, iSneaky = 10;
	INT8  bInWater, bInDeepWater, bInGas;

	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen, orders = %d",pSoldier->aiData.bOrders));
	DebugAI(AI_MSG_START, pSoldier, String("[Green]"));
	LogDecideInfo(pSoldier);

	// sevenfm: disable stealth mode
	pSoldier->bStealthMode = FALSE;
	pSoldier->aiData.fAIFlags &= (~AI_CAUTIOUS);

	// sevenfm: initialize data
	pSoldier->bWeaponMode = WM_NORMAL;

	BOOLEAN fCivilian = (PTR_CIVILIAN && (pSoldier->ubCivilianGroup == NON_CIV_GROUP || pSoldier->aiData.bNeutral || (pSoldier->ubBodyType >= FATCIV && pSoldier->ubBodyType <= CRIPPLECIV) ) );
	BOOLEAN fCivilianOrMilitia = PTR_CIV_OR_MILITIA;

	gubNPCPathCount = 0;

	if ( gTacticalStatus.bBoxingState != NOT_BOXING )
	{
		if (pSoldier->flags.uiStatusFlags & SOLDIER_BOXER)
		{
			if ( gTacticalStatus.bBoxingState == PRE_BOXING )
			{
				return( DecideActionBoxerEnteringRing( pSoldier ) );
			}
			else
			{
				//DBrot: More Rooms
				//UINT8	ubRoom;
				UINT16 usRoom;
				UINT8 ubLoop;

				// boxer... but since in status green, it's time to leave the ring!
				if ( InARoom( pSoldier->sGridNo, &usRoom ))
				{
					if (usRoom == BOXING_RING)
					{
						for ( ubLoop = 0; ubLoop < NUM_BOXERS; ++ubLoop )
						{
							if (pSoldier->ubID == gubBoxerID[ ubLoop ])
							{
								// we should go back where we started
								pSoldier->aiData.usActionData = gsBoxerGridNo[ ubLoop ];
								return( AI_ACTION_GET_CLOSER );
							}
						}
						pSoldier->aiData.usActionData = FindClosestBoxingRingSpot( pSoldier, FALSE );
						return( AI_ACTION_GET_CLOSER );
					}
					else
					{
						// done!

						// Flugente: only do this if we are not boxing. Otherwise this might interfere with boxing scripts, as they temporariyl  set a PC under AI control (when leaaving the ring)
						if ( gTacticalStatus.bBoxingState == NOT_BOXING )
						{
							// WANNE: This should fix the bug if any merc are still under PC control. This could happen after boxing in SAN MONA.
							SOLDIERTYPE	*pTeamSoldier;
							for (INT8 bLoop=gTacticalStatus.Team[gbPlayerNum].bFirstID; bLoop <= gTacticalStatus.Team[gbPlayerNum].bLastID; bLoop++)
							{
								pTeamSoldier=MercPtrs[bLoop]; 

								if (pTeamSoldier->flags.uiStatusFlags & SOLDIER_PCUNDERAICONTROL)
									pTeamSoldier->flags.uiStatusFlags &= (~SOLDIER_PCUNDERAICONTROL);

								pTeamSoldier->DeleteBoxingFlag( );
							}
						}

						if (pSoldier->bTeam == gbPlayerNum || CountPeopleInBoxingRing() == 0)
						{
							TriggerEndOfBoxingRecord( pSoldier );
						}
					}
				}

				return( AI_ACTION_ABSOLUTELY_NONE );
			}
		}
		//else if ( (gTacticalStatus.bBoxingState == PRE_BOXING || gTacticalStatus.bBoxingState == BOXING) && ( PythSpacesAway( pSoldier->sGridNo, CENTER_OF_RING ) <= MaxDistanceVisible() ) )
		else if ( PythSpacesAway( pSoldier->sGridNo, CENTER_OF_RING ) <= MaxNormalDistanceVisible() )
		{
			UINT8 ubRingDir;
			// face ring!

			ubRingDir = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(CENTER_OF_RING),CenterY(CENTER_OF_RING));
			if ( gfTurnBasedAI || GetAPsToLook( pSoldier ) <= pSoldier->bActionPoints )
			{
				if ( pSoldier->ubDirection != ubRingDir )
				{
					pSoldier->aiData.usActionData = ubRingDir;
					return( AI_ACTION_CHANGE_FACING );
				}
			}
			return( AI_ACTION_NONE );
		}
	}

	if ( TANK( pSoldier ) )
	{
		return( AI_ACTION_NONE );
	}


	bInWater = Water( pSoldier->sGridNo, pSoldier->pathing.bLevel );
	bInDeepWater = DeepWater(pSoldier->sGridNo, pSoldier->pathing.bLevel);

	// check if standing in tear gas without a gas mask on
	bInGas = InGas( pSoldier, pSoldier->sGridNo );

	// if real-time, and not in the way, do nothing 90% of the time (for GUARDS!)
	// unless in water (could've started there), then we better swim to shore!

	if (fCivilian || (gGameExternalOptions.fAllNamedNpcsDecideAction && pSoldier->ubProfile != NO_PROFILE))
	{
		// special stuff for civs

		if (pSoldier->flags.uiStatusFlags & SOLDIER_COWERING)
		{
			// everything's peaceful again, stop cowering!!
			pSoldier->aiData.usActionData = ANIM_STAND;
			return( AI_ACTION_STOP_COWERING );
		}

		if (!gfTurnBasedAI)
		{
			// ******************
			// REAL TIME NPC CODE
			// ******************
			if (pSoldier->aiData.fAIFlags & AI_CHECK_SCHEDULE)
			{
				pSoldier->aiData.bAction = DecideActionSchedule( pSoldier );
				if (pSoldier->aiData.bAction != AI_ACTION_NONE)
				{
					return( pSoldier->aiData.bAction );
				}
			}

			if ( pSoldier->ubProfile != NO_PROFILE || pSoldier->IsAssassin() )
			{
				if ( pSoldier->ubProfile != NO_PROFILE )
					pSoldier->aiData.bAction = DecideActionNamedNPC( pSoldier );
				else
				{
					INT32 sDesiredMercDist;
					INT32 sDesiredMercLoc = ClosestUnDisguisedPC( pSoldier, &sDesiredMercDist );
		
					if (!TileIsOutOfBounds(sDesiredMercLoc))
					{
						if ( sDesiredMercDist <= NPC_TALK_RADIUS * 2 )
						{
							AddToShouldBecomeHostileOrSayQuoteList( pSoldier->ubID );
							// now wait a bit!
							pSoldier->aiData.usActionData = 5000;
							pSoldier->aiData.bAction = AI_ACTION_WAIT;
						}
						else
						{
							pSoldier->aiData.usActionData = GoAsFarAsPossibleTowards( pSoldier, sDesiredMercLoc, AI_ACTION_APPROACH_MERC );
				
							if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
							{
								pSoldier->aiData.bAction = AI_ACTION_APPROACH_MERC;
							}
						}
					}
				}

				if ( pSoldier->aiData.bAction != AI_ACTION_NONE )
				{
					return( pSoldier->aiData.bAction );
				}
				// can we act again? not for a minute since we were last spoken to/triggered a record
				if ( pSoldier->uiTimeSinceLastSpoke && (GetJA2Clock() < pSoldier->uiTimeSinceLastSpoke + 60000) )
				{
					return( AI_ACTION_NONE );
				}
				// turn off counter so we don't check it again
				pSoldier->uiTimeSinceLastSpoke = 0;
			}
		}

		// if not in the way, do nothing most of the time
		// unless in water (could've started there), then we better swim to shore!

		if (!(bInDeepWater) && PreRandom(5))
		{
			// don't do nuttin!
			return( AI_ACTION_NONE );
		}

	}

	// sevenfm: only if not raised alert yet
	if( !fCivilian &&
		pSoldier->bTeam == ENEMY_TEAM &&		
		SoldierAI(pSoldier) &&
		gGameExternalOptions.bNewTacticalAIBehavior )
	{
		// raise alert if found fresh corpse		
		if( !(pSoldier->usSoldierFlagMask & SOLDIER_RAISED_REDALERT) &&
			!gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition &&			
			pSoldier->aiData.bAlertStatus < STATUS_RED )
		{
			INT32				cnt;
			ROTTING_CORPSE *	pCorpse;

			for ( cnt = 0; cnt < giNumRottingCorpse; ++cnt )
			{
				pCorpse = &(gRottingCorpse[ cnt ] );

				if( pCorpse &&
					pCorpse->fActivated &&
					pCorpse->def.ubAIWarningValue > 0 &&
					!TileIsOutOfBounds(pCorpse->def.sGridNo) )
				{
					// sevenfm: test vision
					if( CorpseEnemyTeam(pCorpse) &&
						SoldierTo3DLocationLineOfSightTest( pSoldier, pCorpse->def.sGridNo, pCorpse->def.bLevel, 1, TRUE, CALC_FROM_WANTED_DIR ) )
					{
						ScreenMsg( MSG_FONT_YELLOW, MSG_INTERFACE, New113Message[MSG113_ENEMY_FOUND_DEAD_BODY]);
						//pCorpse->def.ubAIWarningValue=0;
						return( AI_ACTION_RED_ALERT );
					}
				}
			}
		}

		// sevenfm: raise alert if found bomb
		if( !(pSoldier->usSoldierFlagMask & SOLDIER_RAISED_REDALERT) &&
			!gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition &&
			pSoldier->aiData.bAlertStatus < STATUS_RED &&
			FindBombNearby(pSoldier, pSoldier->sGridNo, BOMB_DETECTION_RANGE))
		{
			ScreenMsg( FONT_ORANGE, MSG_INTERFACE, L"%s found bomb!", pSoldier->GetName() );
			return( AI_ACTION_RED_ALERT );
		}

		////////////////////////////////////////////////////////////////////////////
		// IF YOU SEE CAPTURED FRIENDS, FREE THEM!
		////////////////////////////////////////////////////////////////////////////

		// Flugente: if we see one of our buddies in handcuffs, its a clear sign of enemy activity!
		if ( gGameExternalOptions.fAllowPrisonerSystem )
		{
			UINT8 ubPerson = GetClosestFlaggedSoldierID( pSoldier, VISION_RANGE, ENEMY_TEAM, SOLDIER_POW, TRUE );

			if ( ubPerson != NOBODY)
			{	
				// raise alarm!
				return( AI_ACTION_RED_ALERT );
			}
		}

		// sevenfm: officer can come to inspect suspicious soldier
		if ( HAS_SKILL_TRAIT(pSoldier, SQUADLEADER_NT) )
		{
			UINT8 ubPerson = GetClosestFlaggedSoldierID( pSoldier, VISION_RANGE, OUR_TEAM, SOLDIER_COVERT_SOLDIER | SOLDIER_COVERT_CIV, TRUE );

			if( ubPerson != NOBODY &&
				pSoldier->pathing.bLevel == MercPtrs[ubPerson]->pathing.bLevel &&
				pSoldier->CanInspect( MercPtrs[ubPerson] ) &&
				Random(100) < MercPtrs[ubPerson]->SuspicionPercent() )
			{
				UINT8 ubFriendsNearby = CountNearbyFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE/4);
				UINT8 ubSoldierDifficulty = SoldierDifficultyLevel(pSoldier);				

				// come to investigate
				pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier, MercPtrs[ubPerson]->sGridNo, 0, AI_ACTION_SEEK_NOISE, 0);

				if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
				{
					// sevenfm: raise alert first if this soldier is not the person who raised alert last
					// chance to raise alert 30%
					if( pSoldier->bActionPoints >= APBPConstants[AP_RADIO] &&
						gTacticalStatus.Team[pSoldier->bTeam].bMenInSector > 1 &&
						pSoldier->ubID != gTacticalStatus.Team[pSoldier->bTeam].ubLastMercToRadio &&
						ubFriendsNearby < 1 + ubSoldierDifficulty &&
						Random(100) < 10 * ubSoldierDifficulty )
					{
						// call friends
						return( AI_ACTION_YELLOW_ALERT );
					}

					return(AI_ACTION_SEEK_NOISE);
				}
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// POINT PATROL: move towards next point unless getting a bit winded
	////////////////////////////////////////////////////////////////////////////

	// this takes priority over water/gas checks, so that point patrol WILL work
	// from island to island, and through gas covered areas, too
	if ((pSoldier->aiData.bOrders == POINTPATROL) && (pSoldier->bBreath >= 75))
	{
		if (PointPatrolAI(pSoldier))
		{
			if (!gfTurnBasedAI)
			{
				// wait after this...
				pSoldier->aiData.bNextAction = AI_ACTION_WAIT;
				pSoldier->aiData.usNextActionData = RealtimeDelay( pSoldier );
			}
			return(AI_ACTION_POINT_PATROL);
		}
		else
		{
			// Reset path count to avoid dedlok
			gubNPCPathCount = 0;
		}
	}

	if ((pSoldier->aiData.bOrders == RNDPTPATROL) && (pSoldier->bBreath >=75))
	{
		if (RandomPointPatrolAI(pSoldier))
		{
			if (!gfTurnBasedAI)
			{
				// wait after this...
				pSoldier->aiData.bNextAction = AI_ACTION_WAIT;
				pSoldier->aiData.usNextActionData = RealtimeDelay( pSoldier );
			}
			return(AI_ACTION_POINT_PATROL);
		}
		else
		{
			// Reset path count to avoid dedlok
			gubNPCPathCount = 0;
		}

	}

	////////////////////////////////////////////////////////////////////////////
	// WHEN LEFT IN WATER OR GAS, GO TO NEAREST REACHABLE SPOT OF UNGASSED LAND
	////////////////////////////////////////////////////////////////////////////

	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: get out of water and gas"));

	if (bInDeepWater || bInGas || FindBombNearby(pSoldier, pSoldier->sGridNo, BOMB_DETECTION_RANGE) || RedSmokeDanger(pSoldier->sGridNo, pSoldier->pathing.bLevel))
	{
		pSoldier->aiData.usActionData = FindNearestUngassedLand(pSoldier);
		
		if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
		{
			return(AI_ACTION_LEAVE_WATER_GAS);
		}
	}



	////////////////////////////////////////////////////////////////////////
	// REST IF RUNNING OUT OF BREATH
	////////////////////////////////////////////////////////////////////////

	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: rest if running out of breath"));
	// if our breath is running a bit low, and we're not in the way or in water
	if ((pSoldier->bBreath < 75) && !bInWater)
	{
		// take a breather for gods sake!
		// for realtime, AI will use a standard wait set outside of here
		pSoldier->aiData.usActionData = NOWHERE;
		return(AI_ACTION_NONE);
	}


	////////////////////////////////////////////////////////////////////////////
	// CLIMB A BUILDING
	////////////////////////////////////////////////////////////////////////////

	if (!fCivilian && pSoldier->aiData.bLastAction != AI_ACTION_CLIMB_ROOF && !is_networked)
	{
		iChance = 10 + pSoldier->aiData.bBypassToGreen;

		// set base chance and maximum seeking distance according to orders
		switch (pSoldier->aiData.bOrders)
		{
		case STATIONARY:     iChance *= 0; break;
		case ONGUARD:        iChance += 10; break;
		case ONCALL:                         break;
		case CLOSEPATROL:    iChance += -20; break;
		case RNDPTPATROL:
		case POINTPATROL:    iChance  = -30; break;
		case FARPATROL:      iChance += -40; break;
		case SEEKENEMY:      iChance += -30; break;
		case SNIPER:		 iChance += 70; break;
		}

		// modify for attitude
		switch (pSoldier->aiData.bAttitude)
		{
		case DEFENSIVE:      iChance *= 1.5;  break;
		case BRAVESOLO:      iChance /= 2;    break;
		case BRAVEAID:       iChance /= 2;   break;
		case CUNNINGSOLO:    iChance *= 1;    break;
		case CUNNINGAID:     iChance /= 1;   break;
		case AGGRESSIVE:     iChance /= 3;    break;
		case ATTACKSLAYONLY:									 break;
		}



		// reduce chance for any injury, less likely to hop up if hurt
		iChance -= (pSoldier->stats.bLifeMax - pSoldier->stats.bLife);

		// reduce chance if breath is down
		//iChance -= (100 - pSoldier->bBreath);         // don't care

		// This is the chance that we want to be on the roof.  If already there, invert the chance to see if we want back
		// down
		if (pSoldier->pathing.bLevel > 0)
		{
			iChance = 100 - iChance;
		}

		// sevenfm: stationary/snipers should not change level
		if ( pSoldier->aiData.bOrders == SNIPER || pSoldier->aiData.bOrders == STATIONARY )
		{
			iChance = 0;
		}

		if ((INT16) PreRandom(100) < iChance)
		{
			BOOLEAN fUp = FALSE;
			if ( pSoldier->pathing.bLevel == 0 )
			{
				fUp = TRUE;
			}
			else if (pSoldier->pathing.bLevel > 0 )
			{
				fUp = FALSE;
			}

			if ( CanClimbFromHere ( pSoldier, fUp ) )
			{
				DebugMsg ( TOPIC_JA2AI , DBG_LEVEL_3 , String("Soldier %d is climbing roof",pSoldier->ubID) );
				return( AI_ACTION_CLIMB_ROOF );
			}
			else
			{
				pSoldier->aiData.usActionData = FindClosestClimbPoint(pSoldier, fUp );
				// Added the check here because sniper militia who are locked inside of a building without keys
				// will still have a >100% chance to want to climb, which means an infinite loop.  In fact, any
				// time a move is desired, there probably also will be a need to check for a path.				
				if ( !TileIsOutOfBounds(pSoldier->aiData.usActionData) &&
					LegalNPCDestination(pSoldier,pSoldier->aiData.usActionData,ENSURE_PATH,WATEROK, 0 ))
				{
					return( AI_ACTION_MOVE_TO_CLIMB  );
				}
			}
		}
	}


	////////////////////////////////////////////////////////////////////////////
	// RANDOM PATROL:  determine % chance to start a new patrol route
	////////////////////////////////////////////////////////////////////////////
	if (!gubNPCPathCount) // try to limit pathing in Green AI
	{

		iChance = 25 + pSoldier->aiData.bBypassToGreen;

		// set base chance according to orders
		switch (pSoldier->aiData.bOrders)
		{
		case STATIONARY:     iChance += -20;  break;
		case ONGUARD:        iChance += -15;  break;
		case ONCALL:                          break;
		case CLOSEPATROL:    iChance += +15;  break;
		case RNDPTPATROL:
		case POINTPATROL:		iChance = 0; break;
			/*
			if ( !gfTurnBasedAI )
			{
			// realtime deadlock... increase chance!
			iChance = 110;// more than 100 in case person is defensive
			}
			else if ( pSoldier->bInitialActionPoints < pSoldier->bActionPoints ) // could be less because of carried-over points
			{
			// CJC: allow pt patrol guys to do a random move in case
			// of a deadlock provided they haven't done anything yet this turn
			iChance=   0;
			}
			break;
			*/
		case FARPATROL:      iChance += +25;  break;
		case SEEKENEMY:      iChance += -10;  break;
		case SNIPER:		iChance += -10;  break;
		}

		// modify chance of patrol (and whether it's a sneaky one) by attitude
		switch (pSoldier->aiData.bAttitude)
		{
		case DEFENSIVE:      iChance += -10;                 break;
		case BRAVESOLO:      iChance +=   5;                 break;
		case BRAVEAID:                                       break;
		case CUNNINGSOLO:    iChance +=   5;  iSneaky += 10; break;
		case CUNNINGAID:                      iSneaky +=  5; break;
		case AGGRESSIVE:     iChance +=  10;  iSneaky += -5; break;
		case ATTACKSLAYONLY: iChance +=  10;  iSneaky += -5; break;
		}

		// reduce chance for any injury, less likely to wander around when hurt
		iChance -= (pSoldier->stats.bLifeMax - pSoldier->stats.bLife);

		// reduce chance if breath is down, less likely to wander around when tired
		iChance -= (100 - pSoldier->bBreath);


		// if we're in water with land miles (> 25 tiles) away,
		// OR if we roll under the chance calculated
		if (bInWater || ((INT16) PreRandom(100) < iChance))
		{
			pSoldier->aiData.usActionData = RandDestWithinRange(pSoldier);
			
			if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
			{
				pSoldier->aiData.usActionData = GoAsFarAsPossibleTowards( pSoldier, pSoldier->aiData.usActionData, AI_ACTION_RANDOM_PATROL );
			}
			
			if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
			{
				if (!gfTurnBasedAI)
				{
					// wait after this...
					pSoldier->aiData.bNextAction = AI_ACTION_WAIT;
					pSoldier->aiData.usNextActionData = RealtimeDelay( pSoldier );
				}
				return(AI_ACTION_RANDOM_PATROL);
			}
		}
	}

	if (!gubNPCPathCount) // try to limit pathing in Green AI
	{
		////////////////////////////////////////////////////////////////////////////
		// SEEK FRIEND: determine %chance for man to pay a friendly visit
		////////////////////////////////////////////////////////////////////////////

		iChance = 25 + pSoldier->aiData.bBypassToGreen;

		// set base chance and maximum seeking distance according to orders
		switch (pSoldier->aiData.bOrders)
		{
		case STATIONARY:     iChance += -20; break;
		case ONGUARD:        iChance += -15; break;
		case ONCALL:                         break;
		case CLOSEPATROL:    iChance += +10; break;
		case RNDPTPATROL:
		case POINTPATROL:    iChance  = -10; break;
		case FARPATROL:      iChance += +20; break;
		case SEEKENEMY:      iChance += -10; break;
		case SNIPER:		  iChance += -10; break;
		}

		// modify for attitude
		switch (pSoldier->aiData.bAttitude)
		{
		case DEFENSIVE:                       break;
		case BRAVESOLO:      iChance /= 2;    break;  // loners
		case BRAVEAID:       iChance += 10;   break;  // friendly
		case CUNNINGSOLO:    iChance /= 2;    break;  // loners
		case CUNNINGAID:     iChance += 10;   break;  // friendly
		case AGGRESSIVE:                      break;
		case ATTACKSLAYONLY:									 break;
		}

		// reduce chance for any injury, less likely to wander around when hurt
		iChance -= (pSoldier->stats.bLifeMax - pSoldier->stats.bLife);

		// reduce chance if breath is down
		iChance -= (100 - pSoldier->bBreath);         // very likely to wait when exhausted


		if ((INT16) PreRandom(100) < iChance)
		{
			if (RandomFriendWithin(pSoldier))
			{
				if ( pSoldier->aiData.usActionData == GoAsFarAsPossibleTowards( pSoldier, pSoldier->aiData.usActionData, AI_ACTION_SEEK_FRIEND ) )
				{
					if (fCivilianOrMilitia && !gfTurnBasedAI)
					{
						// pause at the end of the walk!
						pSoldier->aiData.bNextAction = AI_ACTION_WAIT;
						pSoldier->aiData.usNextActionData = (UINT16) REALTIME_CIV_AI_DELAY;
					}

					return(AI_ACTION_SEEK_FRIEND);
				}
			}
		}
	}


	////////////////////////////////////////////////////////////////////////////
	// SNIPERS LIKE TO CROUCH (on roofs)
	////////////////////////////////////////////////////////////////////////////

	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: Snipers like to crouch, sniper = %d",pSoldier->sniper));
	// if not in water and not already crouched, try to crouch down first
	if (pSoldier->aiData.bOrders == SNIPER && !PTR_CROUCHED && IsValidStance( pSoldier, ANIM_CROUCH ) && pSoldier->pathing.bLevel == 1 )
	{
		if (!gfTurnBasedAI || (GetAPsToChangeStance( pSoldier, ANIM_CROUCH ) <= pSoldier->bActionPoints))
		{
			DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: Sniper is crouching"));
			pSoldier->aiData.usActionData = ANIM_CROUCH;
			pSoldier->sniper = 0;
			return(AI_ACTION_CHANGE_STANCE);
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// SNIPER - RAISE WEAPON TO SCAN AREA
	////////////////////////////////////////////////////////////////////////////

	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: Snipers like to raise weapons, sniper = %d",pSoldier->sniper));
	if ( pSoldier->aiData.bOrders == SNIPER && 
		pSoldier->sniper == 0 && 
		( pSoldier->pathing.bLevel == 1 || Random(100) < 40 ) && 
		(pSoldier->bBreath > 30 || GetBPCostPer10APsForGunHolding( pSoldier, TRUE ) < 20) )
	{
		if (!WeaponReady(pSoldier) && PickSoldierReadyAnimation(pSoldier, FALSE, FALSE) != INVALID_ANIMATION) // SANDRO - only call this if we are not in readied position yet
		{
			if (!gfTurnBasedAI || GetAPsToReadyWeapon( pSoldier, READY_RIFLE_CROUCH ) <= pSoldier->bActionPoints)
			{
				DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: Sniper is raising weapon, soldier = %d, sniper = %d",pSoldier->ubID,pSoldier->sniper));
				pSoldier->sniper = 1;
				DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: Sniper = %d",pSoldier->sniper));
				return(AI_ACTION_RAISE_GUN);
			}
		}
	}
	//else if ( pSoldier->sniper == 1 )
	//{
	//	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: Sniper is lowering weapon, sniper = %d",pSoldier->sniper));
	//	pSoldier->sniper = 0;
	//	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: Sniper = %d",pSoldier->sniper));
	//	return(AI_ACTION_LOWER_GUN);
	//}

	////////////////////////////////////////////////////////////////////////////
	// SANDRO - occasionally, allow regular soldiers to scan around too
	if ( (UsingNewCTHSystem() == false && IsScoped(&pSoldier->inv[HANDPOS])) || 
		 (UsingNewCTHSystem() == true && NCTHIsScoped(&pSoldier->inv[HANDPOS])) )
	{
		if (!WeaponReady(pSoldier))
		{
			if (PickSoldierReadyAnimation(pSoldier, FALSE, FALSE) != INVALID_ANIMATION &&
				(!gfTurnBasedAI || ((GetAPsToReadyWeapon( pSoldier, PickSoldierReadyAnimation( pSoldier, FALSE, FALSE ) ) ) <= pSoldier->bActionPoints)) &&
				 (pSoldier->bBreath > 30 || GetBPCostPer10APsForGunHolding( pSoldier, TRUE ) < 20) )
			{
				iChance = 25;
				if ( pSoldier->ubSoldierClass == SOLDIER_CLASS_ELITE_MILITIA || pSoldier->ubSoldierClass == SOLDIER_CLASS_ELITE )
					iChance += 15;
				else if ( pSoldier->ubSoldierClass == SOLDIER_CLASS_GREEN_MILITIA || pSoldier->ubSoldierClass == SOLDIER_CLASS_ADMINISTRATOR )
					iChance -= 15;
				if ( Random(100) < iChance ) 
				{
					DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: Soldier deciding to raise weapon with scope"));
					return(AI_ACTION_RAISE_GUN);
				}
			}
		}
		else // if the weapon is ready already, maybe unready it
		{
			iChance = 30;
			// is it a heavy gun? And we have energy cost for shooting enabled? 
			iChance += GetBPCostPer10APsForGunHolding( pSoldier ); // don't overexagerate yourself
			if ( Random(100) < iChance ) 
			{
				DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: Soldier deciding to lower weapon"));
				return(AI_ACTION_LOWER_GUN);
			}
		}
	}
	////////////////////////////////////////////////////////////////////////////


	////////////////////////////////////////////////////////////////////////////
	// LOOK AROUND: determine %chance for man to turn in place
	////////////////////////////////////////////////////////////////////////////

	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: Soldier deciding to turn"));
	if (!gfTurnBasedAI || GetAPsToLook( pSoldier ) <= pSoldier->bActionPoints)
	{
		// avoid 2 consecutive random turns in a row
		if (pSoldier->aiData.bLastAction != AI_ACTION_CHANGE_FACING)
		{
			iChance = 25 + pSoldier->aiData.bBypassToGreen;

			// set base chance according to orders
			if (pSoldier->aiData.bOrders == STATIONARY || pSoldier->aiData.bOrders == SNIPER)
				iChance += 25;

			if (pSoldier->aiData.bOrders == ONGUARD)
				iChance += 20;

			if (pSoldier->aiData.bAttitude == DEFENSIVE)
				iChance += 25;

			if ( pSoldier->aiData.bOrders == SNIPER && pSoldier->pathing.bLevel == 1)
				iChance += 35;

			if ( WeaponReady(pSoldier) ) // SANDRO - if readied weapon, make him more likely to turn around
				iChance += 30;

			if ((INT16)PreRandom(100) < iChance)
			{
				// roll random directions (stored in actionData) until different from current
				do
				{
					// if man has a LEGAL dominant facing, and isn't facing it, he will turn
					// back towards that facing 50% of the time here (normally just enemies)
					if ((pSoldier->aiData.bDominantDir >= 0) && (pSoldier->aiData.bDominantDir <= 8) &&
						(pSoldier->ubDirection != pSoldier->aiData.bDominantDir) && PreRandom(2) && pSoldier->aiData.bOrders != SNIPER )
					{
						pSoldier->aiData.usActionData = pSoldier->aiData.bDominantDir;
					}
					else
					{
						INT32 iNoiseValue;
						BOOLEAN fClimb;
						BOOLEAN fReachable;
						INT32 sNoiseGridNo = MostImportantNoiseHeard(pSoldier,&iNoiseValue, &fClimb, &fReachable);
						UINT8 ubNoiseDir;
						
						if (TileIsOutOfBounds(sNoiseGridNo) || 
							(ubNoiseDir = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(sNoiseGridNo),CenterY(sNoiseGridNo))
							) == pSoldier->ubDirection )
						
						{
							pSoldier->aiData.usActionData = PreRandom(8);
						}
						else
						{
							pSoldier->aiData.usActionData = ubNoiseDir;
						}
					}
				} while (pSoldier->aiData.usActionData == pSoldier->ubDirection);

				DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: Trying to turn - checking stance validity, sniper = %d",pSoldier->sniper));
				if ( pSoldier->InternalIsValidStance( (INT8) pSoldier->aiData.usActionData, gAnimControl[ pSoldier->usAnimState ].ubEndHeight ) )
				{

					if ( !gfTurnBasedAI )
					{
						// wait after this...
						pSoldier->aiData.bNextAction = AI_ACTION_WAIT;
						pSoldier->aiData.usNextActionData = RealtimeDelay( pSoldier );
					}

					DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: Soldier is turning"));
					return(AI_ACTION_CHANGE_FACING);
				}
			}
		}
	}


	////////////////////////////////////////////////////////////////////////////
	// NONE:
	////////////////////////////////////////////////////////////////////////////

	// by default, if everything else fails, just stands in place without turning
	// for realtime, regular AI guys will use a standard wait set outside of here
	pSoldier->aiData.usActionData = NOWHERE;
	return(AI_ACTION_NONE);
}

INT8 DecideActionYellow(SOLDIERTYPE *pSoldier)
{
	INT32 iDummy;
	UINT8 ubNoiseDir;
	INT32 sNoiseGridNo;
	INT32 iNoiseValue;
	INT32 iChance, iSneaky;
	INT32 sClosestFriend;
	BOOLEAN fCivilian = (PTR_CIVILIAN && (pSoldier->ubCivilianGroup == NON_CIV_GROUP || pSoldier->aiData.bNeutral || (pSoldier->ubBodyType >= FATCIV && pSoldier->ubBodyType <= CRIPPLECIV) ) );
	BOOLEAN fClimb;
	BOOLEAN fReachable;
	BOOLEAN fHoldRemoteReserve = FALSE;

	INT8  bInWater,bInGas;

	DebugAI(AI_MSG_START, pSoldier, String("[Yellow]"));
	LogDecideInfo(pSoldier);

	// sevenfm: disable stealth mode
	pSoldier->bStealthMode = FALSE;
	pSoldier->aiData.fAIFlags &= (~AI_CAUTIOUS);

	// sevenfm: initialize data
	pSoldier->bWeaponMode = WM_NORMAL;

	bInWater = DeepWater( pSoldier->sGridNo, pSoldier->pathing.bLevel );
	bInGas = InGas( pSoldier, pSoldier->sGridNo );

	if (!bInWater && !bInGas)
	{
		INT8 bRemoteSupport = DecideYellowRemoteRadioSupport(pSoldier);
		if (bRemoteSupport != AI_ACTION_NONE)
			return bRemoteSupport;
	}

	if (fCivilian || (gGameExternalOptions.fAllNamedNpcsDecideAction && pSoldier->ubProfile != NO_PROFILE))
	{
		if (pSoldier->flags.uiStatusFlags & SOLDIER_COWERING)
		{
			// everything's peaceful again, stop cowering!!
			pSoldier->aiData.usActionData = ANIM_STAND;
			return( AI_ACTION_STOP_COWERING );
		}
		if (!gfTurnBasedAI)
		{
			// ******************
			// REAL TIME NPC CODE
			// ******************
			if (pSoldier->ubProfile != NO_PROFILE || pSoldier->IsAssassin() )
			{
				if ( pSoldier->ubProfile != NO_PROFILE )
					pSoldier->aiData.bAction = DecideActionNamedNPC( pSoldier );
				else
				{
					INT32 sDesiredMercDist;
					INT32 sDesiredMercLoc = ClosestUnDisguisedPC( pSoldier, &sDesiredMercDist );

					// Flugente: if this guy is disguised, do not consider him
		
					if (!TileIsOutOfBounds(sDesiredMercLoc))
					{
						if ( sDesiredMercDist <= NPC_TALK_RADIUS * 2 )
						{
							AddToShouldBecomeHostileOrSayQuoteList( pSoldier->ubID );
							// now wait a bit!
							pSoldier->aiData.usActionData = 5000;
							pSoldier->aiData.bAction = AI_ACTION_WAIT;
						}
						else
						{
							pSoldier->aiData.usActionData = GoAsFarAsPossibleTowards( pSoldier, sDesiredMercLoc, AI_ACTION_APPROACH_MERC );
				
							if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
							{
								pSoldier->aiData.bAction = AI_ACTION_APPROACH_MERC;
							}
						}
					}
				}

				if ( pSoldier->aiData.bAction != AI_ACTION_NONE )
				{
					return( pSoldier->aiData.bAction );
				}
			}
		}
	}

	// determine the most important noise heard, and its relative value
	sNoiseGridNo = MostImportantNoiseHeard(pSoldier, &iNoiseValue, &fClimb, &fReachable);
	
	if( !fCivilian &&
		pSoldier->bTeam == ENEMY_TEAM &&
		SoldierAI(pSoldier) &&
		gGameExternalOptions.bNewTacticalAIBehavior )
	{
		////////////////////////////////////////////////////////////////////////////
		// RAISE ALERT IF SEE FRESH CORPSE
		////////////////////////////////////////////////////////////////////////////


		// && !gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition
		//if ( !(gTacticalStatus.uiFlags & TURNBASED) && (gTacticalStatus.uiFlags & INCOMBAT) )

		// raise alert if found fresh corpse
		// sevenfm: only if not raised alert yet
		if( pSoldier->aiData.bAlertStatus < STATUS_RED )
		{
			INT32				cnt;
			ROTTING_CORPSE *	pCorpse;

			for ( cnt = 0; cnt < giNumRottingCorpse; ++cnt )
			{
				pCorpse = &(gRottingCorpse[ cnt ] );

				if( pCorpse &&
					pCorpse->fActivated &&
					pCorpse->def.ubAIWarningValue > 0 &&
					!TileIsOutOfBounds(pCorpse->def.sGridNo) )
				{
					// sevenfm: test vision
					if( CorpseEnemyTeam(pCorpse) &&
						//PythSpacesAway( pSoldier->sGridNo, pCorpse->def.sGridNo ) <= DAY_VISION_RANGE / 2 &&
						SoldierTo3DLocationLineOfSightTest( pSoldier, pCorpse->def.sGridNo, pCorpse->def.bLevel, 1, TRUE, CALC_FROM_WANTED_DIR ) )
					{
						ScreenMsg( MSG_FONT_YELLOW, MSG_INTERFACE, New113Message[MSG113_ENEMY_FOUND_DEAD_BODY]);
						//pCorpse->def.ubAIWarningValue=0;
						return( AI_ACTION_RED_ALERT );
					}
				}
			}
		}

		// sevenfm: raise alert if found bomb
		if( !(pSoldier->usSoldierFlagMask & SOLDIER_RAISED_REDALERT) &&
			!gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition &&
			pSoldier->aiData.bAlertStatus < STATUS_RED &&
			FindBombNearby(pSoldier, pSoldier->sGridNo, BOMB_DETECTION_RANGE))
		{
			ScreenMsg( FONT_ORANGE, MSG_INTERFACE, L"%s found bomb!", pSoldier->GetName() );
			return( AI_ACTION_RED_ALERT );
		}

		////////////////////////////////////////////////////////////////////////////
		// IF YOU SEE CAPTURED FRIENDS, FREE THEM!
		////////////////////////////////////////////////////////////////////////////

		// Flugente: if we see one of our buddies in handcuffs, its a clear sign of enemy activity!
		if ( gGameExternalOptions.fAllowPrisonerSystem )
		{
			UINT8 ubPerson = GetClosestFlaggedSoldierID( pSoldier, VISION_RANGE, ENEMY_TEAM, SOLDIER_POW, TRUE );

			if ( ubPerson != NOBODY )
			{
				// sevenfm: raise alert first
				if ( !(pSoldier->usSoldierFlagMask & SOLDIER_RAISED_REDALERT) &&
					!gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition &&
					pSoldier->aiData.bAlertStatus < STATUS_RED )
				{
					// raise alarm!
					return( AI_ACTION_RED_ALERT );
				}

				// if we are close, we can release this guy
				// possible only if not handcuffed (binders can be opened, handcuffs not)
				if ( !HasItemFlag( (&(MercPtrs[ubPerson]->inv[HANDPOS]))->usItem, HANDCUFFS ) )
				{
					if ( PythSpacesAway(pSoldier->sGridNo, MercPtrs[ubPerson]->sGridNo) < 2 )
					{
						// see if we are facing this person
						UINT8 ubDesiredMercDir = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(MercPtrs[ubPerson]->sGridNo),CenterY(MercPtrs[ubPerson]->sGridNo));

						// if not already facing in that direction,
						if ( pSoldier->ubDirection != ubDesiredMercDir )
						{
							pSoldier->aiData.usActionData = ubDesiredMercDir;

							return( AI_ACTION_CHANGE_FACING );
						}
						//ScreenMsg( FONT_LTGREEN, MSG_INTERFACE, L"[%d] found handcuffed %d! Free prisoner!", pSoldier->ubID, ubPerson);
						return(AI_ACTION_FREE_PRISONER);
					}
					else
					{
						pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier, MercPtrs[ubPerson]->sGridNo, 20, AI_ACTION_SEEK_FRIEND, 0);

						if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
						{
							return(AI_ACTION_SEEK_FRIEND);
						}
					}
				}
			}
		}

		// sevenfm: officer can come to inspect suspicious soldier
		if ( HAS_SKILL_TRAIT(pSoldier, SQUADLEADER_NT) )
		{
			UINT8 ubPerson = GetClosestFlaggedSoldierID( pSoldier, VISION_RANGE, OUR_TEAM, SOLDIER_COVERT_SOLDIER | SOLDIER_COVERT_CIV, TRUE );

			if( ubPerson != NOBODY &&
				pSoldier->pathing.bLevel == MercPtrs[ubPerson]->pathing.bLevel &&
				pSoldier->CanInspect( MercPtrs[ubPerson] ) &&
				Random(100) < MercPtrs[ubPerson]->SuspicionPercent() )
			{
				UINT8 ubFriendsNearby = CountNearbyFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE/4);
				UINT8 ubSoldierDifficulty = SoldierDifficultyLevel(pSoldier);				

				// come to investigate
				pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier, MercPtrs[ubPerson]->sGridNo, 0, AI_ACTION_SEEK_NOISE, 0);

				if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
				{
					// sevenfm: raise alert first if this soldier is not the person who raised alert last
					// chance to raise alert 30%
					if( pSoldier->bActionPoints >= APBPConstants[AP_RADIO] &&
						gTacticalStatus.Team[pSoldier->bTeam].bMenInSector > 1 &&
						pSoldier->ubID != gTacticalStatus.Team[pSoldier->bTeam].ubLastMercToRadio &&
						ubFriendsNearby < 1 + ubSoldierDifficulty &&
						Random(100) < 10 * ubSoldierDifficulty )
					{
						// call friends
						return( AI_ACTION_YELLOW_ALERT );
					}

					return(AI_ACTION_SEEK_NOISE);
				}
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// WHEN LEFT IN WATER OR GAS, GO TO NEAREST REACHABLE SPOT OF UNGASSED LAND
	////////////////////////////////////////////////////////////////////////////

	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionYellow: get out of water and gas"));

	if (bInWater || bInGas || FindBombNearby(pSoldier, pSoldier->sGridNo, BOMB_DETECTION_RANGE) || RedSmokeDanger(pSoldier->sGridNo, pSoldier->pathing.bLevel))
	{
		pSoldier->aiData.usActionData = FindNearestUngassedLand(pSoldier);

		if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
		{
			return(AI_ACTION_LEAVE_WATER_GAS);
		}
	}

	if (TileIsOutOfBounds(sNoiseGridNo))
	{
		// then we have no business being under YELLOW status any more!
		return(AI_ACTION_NONE);
	}

	////////////////////////////////////////////////////////////////////////////
	// LOOK AROUND TOWARD NOISE: determine %chance for man to turn towards noise
	////////////////////////////////////////////////////////////////////////////

	// determine direction from this soldier in which the noise lies
	ubNoiseDir = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(sNoiseGridNo),CenterY(sNoiseGridNo));

	// if soldier is not already facing in that direction,
	// and the noise source is close enough that it could possibly be seen
	if ( !gfTurnBasedAI || GetAPsToLook( pSoldier ) <= pSoldier->bActionPoints )
	{
		if ((pSoldier->ubDirection != ubNoiseDir) && PythSpacesAway(pSoldier->sGridNo,sNoiseGridNo) <= pSoldier->GetMaxDistanceVisible(sNoiseGridNo) )
		{
			// set base chance according to orders
			if ((pSoldier->aiData.bOrders == STATIONARY) || (pSoldier->aiData.bOrders == ONGUARD) )
				iChance = 50;
			else           // all other orders
				iChance = 25;

			if (pSoldier->aiData.bAttitude == DEFENSIVE)
				iChance += 15;


			if ((INT16)PreRandom(100) < iChance && pSoldier->InternalIsValidStance( ubNoiseDir, gAnimControl[ pSoldier->usAnimState ].ubEndHeight ) )
			{
				pSoldier->aiData.usActionData = ubNoiseDir;

				if ( pSoldier->aiData.bOrders == SNIPER && 
					!WeaponReady(pSoldier) &&
					PickSoldierReadyAnimation(pSoldier, FALSE, FALSE) != INVALID_ANIMATION &&
					(pSoldier->bBreath > 25 || GetBPCostPer10APsForGunHolding( pSoldier, TRUE ) < 30))
				{
					if (!gfTurnBasedAI || GetAPsToReadyWeapon( pSoldier, READY_RIFLE_CROUCH ) <= pSoldier->bActionPoints)
					{
						pSoldier->aiData.bNextAction = AI_ACTION_RAISE_GUN;
					}
				}

				////////////////////////////////////////////////////////////////////////////
				// SANDRO - allow regular soldiers to raise scoped weapons to see farther away too
				if ( (UsingNewCTHSystem() == false && IsScoped(&pSoldier->inv[HANDPOS])) || 
					 (UsingNewCTHSystem() == true && NCTHIsScoped(&pSoldier->inv[HANDPOS])) )
				{
					if (!WeaponReady(pSoldier) && 
						PickSoldierReadyAnimation(pSoldier, FALSE, FALSE) != INVALID_ANIMATION &&
						(pSoldier->bBreath > 25 || GetBPCostPer10APsForGunHolding( pSoldier, TRUE ) < 30))
					{
						if (!gfTurnBasedAI || GetAPsToReadyWeapon( pSoldier, PickSoldierReadyAnimation( pSoldier, FALSE, FALSE ) ) <= pSoldier->bActionPoints)
						{
							if ( Random(100) < 35 ) 
							{
								pSoldier->aiData.bNextAction = AI_ACTION_RAISE_GUN;
							}
						}
					}
				}
				////////////////////////////////////////////////////////////////////////////
				
				return(AI_ACTION_CHANGE_FACING);
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// RADIO YELLOW ALERT: determine %chance to call others and report noise
	////////////////////////////////////////////////////////////////////////////

	// if we have the action points remaining to RADIO
	// (we never want NPCs to choose to radio if they would have to wait a turn)
	if ( !fCivilian && (pSoldier->bActionPoints >= APBPConstants[AP_RADIO]) &&
		(gTacticalStatus.Team[pSoldier->bTeam].bMenInSector > 1) )
	{
		// base chance depends on how much new info we have to radio to the others
		iChance = 5 * WhatIKnowThatPublicDont(pSoldier,FALSE);   // use 5 * for YELLOW alert

		// if I actually know something they don't and I ain't swimming (deep water)
		if (iChance && !DeepWater( pSoldier->sGridNo, pSoldier->pathing.bLevel ))
		{

			// CJC: this addition allows for varying difficulty levels for soldier types
			iChance += gbDiff[ DIFF_RADIO_RED_ALERT ][ SoldierDifficultyLevel( pSoldier ) ] / 2;

			// Alex: this addition replaces the sectorValue/2 in original JA
			//iChance += gsDiff[DIFF_RADIO_RED_ALERT][GameOption[ENEMYDIFFICULTY]] / 2;

			// modify base chance according to orders
			switch (pSoldier->aiData.bOrders)
			{
			case STATIONARY: iChance +=  20;  break;
			case ONGUARD:    iChance +=  15;  break;
			case ONCALL:     iChance +=  10;  break;
			case CLOSEPATROL:                 break;
			case RNDPTPATROL:
			case POINTPATROL:                 break;
			case FARPATROL:  iChance += -10;  break;
			case SEEKENEMY:  iChance += -20;  break;
			case SNIPER:		iChance += -10; break; //Madd: sniper contacts are supposed to be automatically reported
			}

			// modify base chance according to attitude
			switch (pSoldier->aiData.bAttitude)
			{
			case DEFENSIVE:  iChance +=  20;  break;
			case BRAVESOLO:  iChance += -10;  break;
			case BRAVEAID:                    break;
			case CUNNINGSOLO:iChance +=  -5;  break;
			case CUNNINGAID:                  break;
			case AGGRESSIVE: iChance += -20;  break;
			case ATTACKSLAYONLY: iChance = 0; break;
			}

			if ((INT16)PreRandom(100) < iChance)
			{
				return(AI_ACTION_YELLOW_ALERT);
			}
		}
	}

	if ( TANK( pSoldier ) )
	{
		return( AI_ACTION_NONE );
	}

	////////////////////////////////////////////////////////////////////////
	// REST IF RUNNING OUT OF BREATH
	////////////////////////////////////////////////////////////////////////

	// if our breath is running a bit low, and we're not in water
	if ((pSoldier->bBreath < 25) && !pSoldier->MercInWater())
	{
		// take a breather for gods sake!
		pSoldier->aiData.usActionData = NOWHERE;
		
		// is it a heavy gun? And we have energy cost for shooting enabled? 
		if ( WeaponReady(pSoldier) && GetBPCostPer10APsForGunHolding( pSoldier ) > 0 )
		{
			// unready
			return(AI_ACTION_LOWER_GUN); 
		}

		return(AI_ACTION_NONE);
	}

	// Hmmm, I don't think this check is doing what is intended.  But then I see no comment about what is intended.
	// However, civilians with no profile (and likely no weapons) do not need to be seeking out noises.  Most don't
	// even have the body type for it (can't climb or jump).
	//if ( !( pSoldier->bTeam == CIV_TEAM && pSoldier->ubProfile != NO_PROFILE && pSoldier->ubProfile != ELDIN ) )
	//if ( pSoldier->bTeam != CIV_TEAM || ( !pSoldier->aiData.bNeutral && pSoldier->ubProfile != ELDIN ) )
	// ADB: Eldin is the only neutral civilian who should be seeking out noises.  As the museum curator, he can be
	// available to talk to.  As the night watchman, he needs to look for thieves.
	bool onCivTeam = (pSoldier->bTeam == CIV_TEAM);
	bool isNamedCiv = (pSoldier->ubProfile != NO_PROFILE);
	bool isEldin = (pSoldier->ubProfile == ELDIN);//logically flipped from the original, isNotEldin == false is confusing
	// For purpose of seeking noise, cowardly civs are neutral, even if attacked by your thugs
	bool isNeutral = pSoldier->aiData.bNeutral || pSoldier->flags.uiStatusFlags & SOLDIER_COWERING; 
	if (
		(onCivTeam == false) || //true #1
		(onCivTeam == true && isNamedCiv == true && isNeutral == false) || //true #2
		(onCivTeam == true && isEldin == true)//true #3
		)
	{
		// IF WE ARE MILITIA/CIV IN REALTIME, CLOSE TO NOISE, AND CAN SEE THE SPOT WHERE THE NOISE CAME FROM, FORGET IT
		if ( fReachable && !fClimb && !gfTurnBasedAI && (pSoldier->bTeam == MILITIA_TEAM || pSoldier->bTeam == CIV_TEAM )&& PythSpacesAway( pSoldier->sGridNo, sNoiseGridNo ) < 5 )
		{
			if ( SoldierTo3DLocationLineOfSightTest( pSoldier, sNoiseGridNo, pSoldier->pathing.bLevel, 0, TRUE, 6 )	)
			{
				// set reachable to false so we don't investigate
				fReachable = FALSE;
				// forget about noise
				pSoldier->aiData.sNoiseGridno = NOWHERE;
				pSoldier->aiData.ubNoiseVolume = 0;
			}
		}

		////////////////////////////////////////////////////////////////////////////
		// SEEK NOISE
		////////////////////////////////////////////////////////////////////////////

		if ( fReachable )
		{
			// remember that noise value is negative, and closer to 0 => more important!
			iChance = 95 + (iNoiseValue / 3);
			iSneaky = 30;

			// increase

			// set base chance according to orders
			switch (pSoldier->aiData.bOrders)
			{
			case STATIONARY:     iChance += -20;  break;
			case ONGUARD:        iChance += -15;  break;
			case ONCALL:                          break;
			case CLOSEPATROL:    iChance += -10;  break;
			case RNDPTPATROL:
			case POINTPATROL:                     break;
			case FARPATROL:      iChance +=  10;  break;
			case SEEKENEMY:      iChance +=  25;  break;
			case SNIPER:		  iChance += -10; break;
			}

			// modify chance of patrol (and whether it's a sneaky one) by attitude
			switch (pSoldier->aiData.bAttitude)
			{
			case DEFENSIVE:      iChance += -10;  iSneaky +=  15;  break;
			case BRAVESOLO:      iChance +=  10;                   break;
			case BRAVEAID:       iChance +=   5;                   break;
			case CUNNINGSOLO:    iChance +=   5;  iSneaky +=  30;  break;
			case CUNNINGAID:                      iSneaky +=  30;  break;
			case AGGRESSIVE:     iChance +=  20;  iSneaky += -10;  break;
			case ATTACKSLAYONLY:	iChance +=  20;  iSneaky += -10;  break;
			}

			// reduce chance if breath is down, less likely to wander around when tired
			iChance -= (100 - pSoldier->bBreath);


			// Hearing the same gunshot should not make an entire sector converge on one point.
			// Troops very close to the sound can react immediately. Beyond that immediate
			// area, only a fireteam-sized response element actively investigates; other
			// guards/patrols remain alert in place until the contact spreads to them.
			if (!GuySawEnemy(pSoldier, SEEN_LAST_TURN) && !pSoldier->aiData.bUnderFire)
			{
				INT32 iResponseDistance = PythSpacesAway(pSoldier->sGridNo, sNoiseGridNo);
				INT32 iImmediateResponseRange = __max(6, DAY_VISION_RANGE / 4);

				if (iResponseDistance > iImmediateResponseRange)
				{
					INT32 iReinforcementUrgency = 0;
					UINT8 ubResponseLimit = AIEnemyResponseLimitForContact(
						pSoldier, sNoiseGridNo, &iReinforcementUrgency);

					// Enemy reinforcements are released as coherent fireteams.  Once the
					// nearer element fills the current response budget, the next element
					// stays in reserve instead of feeding individual soldiers into contact.
					if (AICombatTeam(pSoldier))
					{
						fHoldRemoteReserve = AIFireteamShouldHoldReserve(pSoldier, sNoiseGridNo, ubResponseLimit);
					}
					else
					{
						UINT8 ubCloserResponders = 0;
						for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
							iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
						{
							SOLDIERTYPE *pFriend = MercPtrs[iCounter];
							if (!pFriend || pFriend == pSoldier || !pFriend->bActive || !pFriend->bInSector ||
								pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || pFriend->bBreathCollapsed ||
								(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
								(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
								pFriend->aiData.bOrders == STATIONARY || pFriend->aiData.bOrders == SNIPER)
								continue;

							INT32 iFriendDistance = PythSpacesAway(pFriend->sGridNo, sNoiseGridNo);
							if (iFriendDistance < iResponseDistance ||
								(iFriendDistance == iResponseDistance && pFriend->ubID < pSoldier->ubID))
							{
								++ubCloserResponders;
								if (ubCloserResponders >= ubResponseLimit)
								{
									fHoldRemoteReserve = TRUE;
									break;
								}
							}
						}
					}

					// Distance matters even before a radio call: hearing a shot is evidence,
					// not an order for every soldier in earshot to sprint to its source.
					INT32 iDistancePenalty = 10 + 2 * (iResponseDistance - iImmediateResponseRange);
					iChance -= __min(65, iDistancePenalty);
					iChance += iReinforcementUrgency;

					switch (pSoldier->aiData.bOrders)
					{
					case ONGUARD:       iChance -= 20; break;
					case CLOSEPATROL:   iChance -= 12; break;
					case RNDPTPATROL:
					case POINTPATROL:   iChance -= 8;  break;
					case FARPATROL:     iChance -= 5;  break;
					case ONCALL:        iChance += 10; break;
					case SEEKENEMY:     iChance += 15; break;
					default: break;
					}

					if (fHoldRemoteReserve)
						iChance = 0;
				}
			}

			// sevenfm: stationary/snipers should not seek
			if ( pSoldier->aiData.bOrders == SNIPER || pSoldier->aiData.bOrders == STATIONARY )
			{
				iChance = 0;
			}

			if ((INT16) PreRandom(100) < iChance  )
			{
				INT32 sNoiseApproachSpot = sNoiseGridNo;
				BOOLEAN fUnconfirmedInvestigation =
					!fClimb &&
					!GuySawEnemy(pSoldier, SEEN_LAST_TURN) &&
					!pSoldier->aiData.bUnderFire;

				// Investigation is not an assault. A soldier responding to a reported
				// contact that he has not personally confirmed should establish a covered
				// perimeter/observation position instead of collapsing onto the exact
				// noise tile with the rest of the response element.
				if (!fClimb &&
					!GuySawEnemy(pSoldier, SEEN_LAST_TURN) &&
					!pSoldier->aiData.bUnderFire &&
					PythSpacesAway(pSoldier->sGridNo, sNoiseGridNo) > DAY_VISION_RANGE / 4)
				{
					INT32 sPerimeterSpot = FindAdvanceSpot(pSoldier, sNoiseGridNo,
						AI_ACTION_SEEK_NOISE, ADVANCE_SPOT_ANY_COVER, FALSE);

					if (!TileIsOutOfBounds(sPerimeterSpot))
					{
						sNoiseApproachSpot = sPerimeterSpot;
					}
				}

				if (fUnconfirmedInvestigation)
				{
					INT8 bInvestigationReserve =
						GetAPsCrouch(pSoldier, TRUE) + GetAPsToLook(pSoldier);
					pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(
						pSoldier, sNoiseApproachSpot, bInvestigationReserve,
						AI_ACTION_SEEK_NOISE, FLAG_CAUTIOUS);
					if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
						pSoldier->aiData.fAIFlags |= AI_CAUTIOUS;
				}
				else
				{
					pSoldier->aiData.usActionData = GoAsFarAsPossibleTowards(
						pSoldier, sNoiseApproachSpot, AI_ACTION_SEEK_NOISE);
				}
				
				if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
				{
					if ( fClimb )
					{
						// need to climb AND have enough APs to get there this turn
						// 0verhaul:  the Closest Noise call returns the location of a climb.  So 1) it's not necessary to
						// ask if we can climb from here.  And 2) It's not necessary to look for the climb point.  We already
						// have it.
						if ( pSoldier->sGridNo == sNoiseGridNo)
						{
							if (IsActionAffordable(pSoldier) && pSoldier->bActionPoints >= ( APBPConstants[AP_CLIMBROOF] + MinAPsToAttack( pSoldier, sNoiseGridNo, ADDTURNCOST,0)))
							{
								return( AI_ACTION_CLIMB_ROOF );
							}
						}
						else
						{
							pSoldier->aiData.usActionData = sNoiseGridNo;							
							return AI_ACTION_MOVE_TO_CLIMB;
						}
					}

					return(AI_ACTION_SEEK_NOISE);
				}
			}
		}

		////////////////////////////////////////////////////////////////////////////
		// SEEK FRIEND WHO LAST RADIOED IN TO REPORT NOISE
		////////////////////////////////////////////////////////////////////////////

		sClosestFriend = ClosestReachableFriendInTrouble(pSoldier, &fClimb);

		// if there is a friend alive & reachable who last radioed in		
		if (!TileIsOutOfBounds(sClosestFriend))
		{
			INT32 iFriendResponseUrgency = 0;
			if (AICombatTeam(pSoldier) &&
				!GuySawEnemy(pSoldier, SEEN_LAST_TURN) &&
				!pSoldier->aiData.bUnderFire)
			{
				// The reporting friend is the movement destination, not a new tactical
				// contact. Use the shared noise/contact location for reserve allocation so
				// different members of one fireteam cannot choose different reporters and
				// consequently disagree about whether their element has been released.
				INT32 sResponseContact = !TileIsOutOfBounds(sNoiseGridNo) ?
					sNoiseGridNo : sClosestFriend;
				UINT8 ubFriendResponseLimit = AIEnemyResponseLimitForContact(
					pSoldier, sResponseContact, &iFriendResponseUrgency);
				if (AIFireteamShouldHoldReserve(
					pSoldier, sResponseContact, ubFriendResponseLimit))
					fHoldRemoteReserve = TRUE;
			}

			// there a chance enemy soldier choose to go "help" his friend
			iChance = 50 - SpacesAway(pSoldier->sGridNo,sClosestFriend);
			iChance += iFriendResponseUrgency;
			iSneaky = 10;

			// set base chance according to orders
			switch (pSoldier->aiData.bOrders)
			{
			case STATIONARY:     iChance += -20;  break;
			case ONGUARD:        iChance += -15;  break;
			case ONCALL:         iChance +=  20;  break;
			case CLOSEPATROL:    iChance += -10;  break;
			case RNDPTPATROL:
			case POINTPATROL:    iChance += -10;  break;
			case FARPATROL:                       break;
			case SEEKENEMY:      iChance +=  10;  break;
			case SNIPER:		  iChance += -10; break;
			}

			// modify chance of patrol (and whether it's a sneaky one) by attitude
			switch (pSoldier->aiData.bAttitude)
			{
			case DEFENSIVE:      iChance += -10;  iSneaky +=  15;        break;
			case BRAVESOLO:                                              break;
			case BRAVEAID:       iChance +=  20;  iSneaky += -10;        break;
			case CUNNINGSOLO:					   iSneaky +=  30;		  break;
			case CUNNINGAID:     iChance +=  20;  iSneaky +=  20;        break;
			case AGGRESSIVE:     iChance += -20;  iSneaky += -20;        break;
			case ATTACKSLAYONLY: iChance += -20;  iSneaky += -20;        break;
			}

			// reduce chance if breath is down, less likely to wander around when tired
			iChance -= (100 - pSoldier->bBreath);

			// Soldiers outside the designated remote response element hold their post
			// instead of bypassing the noise cap by running to the reporting friend.
			if (fHoldRemoteReserve)
				iChance = 0;

			// sevenfm: stationary/snipers should not help
			if ( pSoldier->aiData.bOrders == SNIPER || pSoldier->aiData.bOrders == STATIONARY )
			{
				iChance = 0;
			}

			if ((INT16)PreRandom(100) < iChance)
			{
				BOOLEAN fCautiousHelp =
					!fClimb &&
					!GuySawEnemy(pSoldier, SEEN_LAST_TURN) &&
					!pSoldier->aiData.bUnderFire &&
					PreRandom(100) < __max(0, __min(80, iSneaky));

				if (fCautiousHelp)
				{
					INT8 bHelpReserve = GetAPsCrouch(pSoldier, TRUE) + GetAPsToLook(pSoldier);
					pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(
						pSoldier, sClosestFriend, bHelpReserve, AI_ACTION_SEEK_FRIEND, FLAG_CAUTIOUS);
					if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
						pSoldier->aiData.fAIFlags |= AI_CAUTIOUS;
				}
				else
				{
					pSoldier->aiData.usActionData = GoAsFarAsPossibleTowards(
						pSoldier, sClosestFriend, AI_ACTION_SEEK_FRIEND);
				}
				
				if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
				{
					if ( fClimb )//&& pSoldier->aiData.usActionData == sClosestFriend)
					{
						// need to climb AND have enough APs to get there this turn
						// 0verhaul:  Closest Friend call also returns the climb point if climbing is necessary.  So don't
						// climb the wrong building and don't search again
						if (pSoldier->sGridNo == sClosestFriend)
						{
							if (IsActionAffordable(pSoldier) )
							{
								return( AI_ACTION_CLIMB_ROOF );
							}
						}
						else
						{
							pSoldier->aiData.usActionData = sClosestFriend;							
							return( AI_ACTION_MOVE_TO_CLIMB  );
						}
					}

					return(AI_ACTION_SEEK_FRIEND);
				}
			}
		}

		////////////////////////////////////////////////////////////////////////////
		// TAKE BEST NEARBY COVER FROM THE NOISE GENERATING GRIDNO
		////////////////////////////////////////////////////////////////////////////

		if (!SkipCoverCheck ) // && gfTurnBasedAI) // only do in turnbased
		{
			// remember that noise value is negative, and closer to 0 => more important!
			iChance = 25;
			iSneaky = 30;

			// set base chance according to orders
			switch (pSoldier->aiData.bOrders)
			{
			case STATIONARY:     iChance +=  20;  break;
			case ONGUARD:        iChance +=  15;  break;
			case ONCALL:                          break;
			case CLOSEPATROL:    iChance +=  10;  break;
			case RNDPTPATROL:
			case POINTPATROL:                     break;
			case FARPATROL:      iChance +=  -5;  break;
			case SEEKENEMY:      iChance += -20;  break;
			case SNIPER:		  iChance +=  20; break;
			}

			// modify chance (and whether it's sneaky) by attitude
			switch (pSoldier->aiData.bAttitude)
			{
			case DEFENSIVE:      iChance +=  10;  iSneaky +=  15;  break;
			case BRAVESOLO:      iChance += -15;  iSneaky += -20;  break;
			case BRAVEAID:       iChance += -20;  iSneaky += -20;  break;
			case CUNNINGSOLO:    iChance +=  20;  iSneaky +=  30;  break;
			case CUNNINGAID:     iChance +=  15;  iSneaky +=  30;  break;
			case AGGRESSIVE:     iChance += -10;  iSneaky += -10;  break;
			case ATTACKSLAYONLY: iChance += -10;  iSneaky += -10;  break;
			}



			// reduce chance if breath is down, less likely to wander around when tired
			iChance -= (100 - pSoldier->bBreath);

			if ((INT16)PreRandom(100) < iChance)
			{
				pSoldier->aiData.bAIMorale = CalcMorale( pSoldier );
				pSoldier->aiData.usActionData = FindBestNearbyCover(pSoldier,pSoldier->aiData.bAIMorale,&iDummy);
				
				if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
				{
					return(AI_ACTION_TAKE_COVER);
				}
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// SWITCH TO GREEN: only relax once there is no useful reachable evidence.
	// Legacy Vengeance had a flat 50% chance to forget a valid noise every pass,
	// which made alerted soldiers oscillate between investigation and idle patrol.
	////////////////////////////////////////////////////////////////////////////
	if ((TileIsOutOfBounds(sNoiseGridNo) || !fReachable) && (INT16)PreRandom(100) < 50 )
	{
		// Skip YELLOW until new situation, 15% extra chance to do GREEN actions
		pSoldier->aiData.bBypassToGreen = 15;
		return(DecideActionGreen(pSoldier));
	}

	////////////////////////////////////////////////////////////////////////////
	// CROUCH IF NOT CROUCHING ALREADY
	////////////////////////////////////////////////////////////////////////////

	// if not in water and not already crouched, try to crouch down first
	if (!fCivilian && !PTR_CROUCHED && IsValidStance( pSoldier, ANIM_CROUCH ) )
	{
		if (!gfTurnBasedAI || GetAPsToChangeStance( pSoldier, ANIM_CROUCH ) <= pSoldier->bActionPoints)
		{
			////////////////////////////////////////////////////////////////////////////
			// SANDRO - raise weapon maybe
			if (!WeaponReady(pSoldier) && 
				PickSoldierReadyAnimation(pSoldier, FALSE, FALSE) != INVALID_ANIMATION &&
				pSoldier->ubDirection == ubNoiseDir && 
				(pSoldier->bBreath > 25 || GetBPCostPer10APsForGunHolding( pSoldier, TRUE ) < 30)) // if we are facing the direction of where the noise came from
			{
				if (!gfTurnBasedAI || (((GetAPsToReadyWeapon( pSoldier, PickSoldierReadyAnimation( pSoldier, FALSE, FALSE ) ) ) + GetAPsToChangeStance( pSoldier, ANIM_CROUCH )) <= pSoldier->bActionPoints))
				{
					if ( (UsingNewCTHSystem() == false && IsScoped(&pSoldier->inv[HANDPOS])) || 
						 (UsingNewCTHSystem() == true && NCTHIsScoped(&pSoldier->inv[HANDPOS])) )
					{
						pSoldier->aiData.bNextAction = AI_ACTION_RAISE_GUN;
					}
				}
			}
			////////////////////////////////////////////////////////////////////////////

			pSoldier->aiData.usActionData = ANIM_CROUCH;
			return(AI_ACTION_CHANGE_STANCE);
		}
	}
	else if (!fCivilian)
	{
		////////////////////////////////////////////////////////////////////////////
		// SANDRO - raise weapon maybe
		if (!(WeaponReady(pSoldier)) && 
			PickSoldierReadyAnimation(pSoldier, FALSE, FALSE) != INVALID_ANIMATION &&
			pSoldier->ubDirection == ubNoiseDir && 
			(pSoldier->bBreath > 25 || GetBPCostPer10APsForGunHolding( pSoldier, TRUE ) < 30)) // if we are facing the direction of where the noise came from
		{
			if (!gfTurnBasedAI || GetAPsToReadyWeapon( pSoldier, pSoldier->usAnimState ) <= pSoldier->bActionPoints)
			{
				if ( (UsingNewCTHSystem() == false && IsScoped(&pSoldier->inv[HANDPOS])) || 
					 (UsingNewCTHSystem() == true && NCTHIsScoped(&pSoldier->inv[HANDPOS])) )
				{
					if ( Random(100) < 35 ) 
					{
						return( AI_ACTION_RAISE_GUN );
					}
				}
			}
		}
		////////////////////////////////////////////////////////////////////////////	
	}

	////////////////////////////////////////////////////////////////////////////
	// DO NOTHING: Not enough points left to move, so save them for next turn
	////////////////////////////////////////////////////////////////////////////

	// by default, if everything else fails, just stands in place without turning
	pSoldier->aiData.usActionData = NOWHERE;
	return(AI_ACTION_NONE);
}


INT8 DecideActionRed(SOLDIERTYPE *pSoldier)
{
	INT8 bActionReturned;
	INT32 iDummy;
	INT32 iChance,sClosestOpponent,sClosestFriend;
	INT32 sClosestDisturbance, sDistVisible, sCheckGridNo;
	INT8 bClosestDisturbanceLevel = 0;
	UINT8 ubCanMove,ubOpponentDir;
	INT8 bInWater, bInDeepWater, bInGas;
	INT8 bSeekPts = 0, bHelpPts = 0, bHidePts = 0, bWatchPts = 0;
	INT8	bHighestWatchLoc;
	ATTACKTYPE BestThrow, BestShot;

	BOOLEAN fSeekClimb;
	BOOLEAN fHelpClimb;
	BOOLEAN fCivilian = (PTR_CIVILIAN && (pSoldier->ubCivilianGroup == NON_CIV_GROUP ||
		(pSoldier->aiData.bNeutral && gTacticalStatus.fCivGroupHostile[pSoldier->ubCivilianGroup] == CIV_GROUP_NEUTRAL) ||
		(pSoldier->ubBodyType >= FATCIV && pSoldier->ubBodyType <= CRIPPLECIV) ) );

	// sevenfm:
	BOOLEAN fDangerousSpot = FALSE;
	BOOLEAN fProneSightCover = FALSE;
	INT32	sOpponentGridNo;
	INT8	bOpponentLevel;
	INT32	sDangerousSpot;
	INT32	sLastSafeSpot;

	// WANNE: Headrock informed me that I should remove that because it needs a lot of CPU!
	// HEADROCK HAM B2.7: Calculate the overall tactical situation
	//INT16 ubOverallTacticalSituation = AssessTacticalSituation(pSoldier->bSide);

	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,String("DecideActionRed: soldier orders = %d",pSoldier->aiData.bOrders));
	DebugAI(AI_MSG_START, pSoldier, String("[Red]"));
	LogDecideInfo(pSoldier);

	pSoldier->bStealthMode = FALSE;
	pSoldier->aiData.fAIFlags &= (~AI_CAUTIOUS);

	// sevenfm: initialize data
	pSoldier->bWeaponMode = WM_NORMAL;

	// sevenfm: set stealth mode
	if( (pSoldier->aiData.bAttitude == DEFENSIVE || pSoldier->aiData.bAttitude == CUNNINGAID || pSoldier->aiData.bAttitude == CUNNINGSOLO ) )
	{
		INT32 sClosestThreat = ClosestKnownOpponent(pSoldier, NULL, NULL);
		if( !TileIsOutOfBounds(sClosestThreat) &&
			!( pSoldier->flags.uiStatusFlags & SOLDIER_VEHICLE ) &&
			!AM_A_ROBOT( pSoldier ) &&
			!(pSoldier->flags.uiStatusFlags & SOLDIER_BOXER) &&
			!TANK( pSoldier ) &&
			PythSpacesAway(pSoldier->sGridNo, sClosestThreat) < DAY_VISION_RANGE &&
			(NightTime() || InARoom(pSoldier->sGridNo, NULL) ) &&
			!pSoldier->aiData.bUnderFire &&
			!GuySawEnemy(pSoldier) &&
			!InWaterGasOrSmoke(pSoldier, pSoldier->sGridNo) )
		{
			pSoldier->bStealthMode = TRUE;
		}		
	}

	// if we have absolutely no action points, we can't do a thing under RED!
	if ( pSoldier->bActionPoints <= 0 ) //Action points can be negative
	{
		pSoldier->aiData.usActionData = NOWHERE;
		return(AI_ACTION_NONE);
	}

	// sevenfm: find closest opponent
	sClosestOpponent = ClosestKnownOpponent( pSoldier, &sOpponentGridNo, &bOpponentLevel );

	fProneSightCover = ProneSightCoverAtSpot(pSoldier, pSoldier->sGridNo);
	if( !fProneSightCover || pSoldier->aiData.bUnderFire )
	{
		fDangerousSpot = TRUE;
	}

	// can this guy move to any of the neighbouring squares ? (sets TRUE/FALSE)
	ubCanMove = (pSoldier->bActionPoints >= MinPtsToMove(pSoldier));

	// if we're an alerted enemy, and there are panic bombs or a trigger around
	if ( (!PTR_CIVILIAN || pSoldier->ubProfile == WARDEN) && ( ( gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition || (pSoldier->ubID == gTacticalStatus.ubTheChosenOne) || (pSoldier->ubProfile == WARDEN) ) &&
		(gTacticalStatus.fPanicFlags & (PANIC_BOMBS_HERE | PANIC_TRIGGERS_HERE ) ) ) )
	{
		if ( pSoldier->ubProfile == WARDEN && gTacticalStatus.ubTheChosenOne == NOBODY )
		{
			PossiblyMakeThisEnemyChosenOne( pSoldier );
		}

		// do some special panic AI decision making
		bActionReturned = PanicAI(pSoldier,ubCanMove);

		// if we decided on an action while in there, we're done
		if (bActionReturned != -1)
			return(bActionReturned);
	}

	if ( pSoldier->ubProfile != NO_PROFILE )
	{
		if ( (pSoldier->ubProfile == QUEEN || pSoldier->ubProfile == JOE) && ubCanMove )
		{
			if ( gWorldSectorX == 3 && gWorldSectorY == MAP_ROW_P && gbWorldSectorZ == 0 && !gfUseAlternateQueenPosition )
			{
				bActionReturned = HeadForTheStairCase( pSoldier );
				if ( bActionReturned != AI_ACTION_NONE )
				{
					return( bActionReturned );
				}
			}
		}
	}

	// determine if we happen to be in water (in which case we're in BIG trouble!)
	bInWater = Water( pSoldier->sGridNo, pSoldier->pathing.bLevel );
	bInDeepWater = DeepWater(pSoldier->sGridNo, pSoldier->pathing.bLevel);

	// check if standing in tear gas without a gas mask on
	bInGas = InGas( pSoldier, pSoldier->sGridNo );

	////////////////////////////////////////////////////////////////////////////
	// WHEN LEFT IN GAS, WEAR GAS MASK IF AVAILABLE AND NOT WORN
	////////////////////////////////////////////////////////////////////////////

	if ( !bInGas && (gWorldSectorX == TIXA_SECTOR_X && gWorldSectorY == TIXA_SECTOR_Y) )
	{
		// only chance if we happen to be caught with our gas mask off
		if ( PreRandom( 10 ) == 0 && WearGasMaskIfAvailable( pSoldier ) )
		{
			// reevaluate
			bInGas = InGasOrSmoke( pSoldier, pSoldier->sGridNo );
		}
	}

	//Only put mask on in gas
	if(bInGas && WearGasMaskIfAvailable(pSoldier))//dnl ch40 200909
		bInGas = InGasOrSmoke(pSoldier, pSoldier->sGridNo);

	////////////////////////////////////////////////////////////////////////////
	// WHEN IN GAS/DEEP WATER/AREA DANGER, MOVE TO SAFE LAND
	////////////////////////////////////////////////////////////////////////////

	// Offensive orders never override environmental self-preservation. The old
	// SEEKENEMY exception sent soldiers deeper toward contact while they were in
	// deep water; use the common safe-land search for every order instead.
	if (ubCanMove && (bInGas || bInDeepWater || FindBombNearby(pSoldier, pSoldier->sGridNo, BOMB_DETECTION_RANGE) || RedSmokeDanger(pSoldier->sGridNo, pSoldier->pathing.bLevel)))
	{
		pSoldier->aiData.usActionData = FindNearestUngassedLand(pSoldier);

		if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
		{
			return(AI_ACTION_LEAVE_WATER_GAS);
		}
	}

	if ( fCivilian && !( pSoldier->ubBodyType == COW || pSoldier->ubBodyType == CRIPPLECIV ) &&
		gTacticalStatus.bBoxingState == NOT_BOXING)
	{
		if ( FindAIUsableObjClass( pSoldier, IC_WEAPON ) == ITEM_NOT_FOUND )
		{
			// cower in fear!!
			if ( pSoldier->flags.uiStatusFlags & SOLDIER_COWERING )
			{
				if ( gfTurnBasedAI || gTacticalStatus.fEnemyInSector ) // battle!
				{
					// in battle!
					if ( pSoldier->aiData.bLastAction == AI_ACTION_COWER )
					{
						// do nothing
						pSoldier->aiData.usActionData = NOWHERE;
						return( AI_ACTION_NONE );
					}
					else
					{
						// set up next action to run away
						pSoldier->aiData.usNextActionData = FindSpotMaxDistFromOpponents( pSoldier );						
						if (!TileIsOutOfBounds(pSoldier->aiData.usNextActionData))
						{
							pSoldier->aiData.bNextAction = AI_ACTION_RUN_AWAY;
							pSoldier->aiData.usActionData = ANIM_STAND;
							return( AI_ACTION_STOP_COWERING );
						}
						else
						{
							return( AI_ACTION_NONE );
						}
					}
				}
				else
				{
					if ( pSoldier->aiData.bNewSituation == NOT_NEW_SITUATION )
					{
						// stop cowering, not in battle, timer expired
						// we have to turn off whatever is necessary to stop status red...
						pSoldier->aiData.bAlertStatus = STATUS_GREEN;
						return( AI_ACTION_STOP_COWERING );
					}
					else
					{
						return( AI_ACTION_NONE );
					}
				}
			}
			else
			{
				if ( gfTurnBasedAI || gTacticalStatus.fEnemyInSector )
				{
					// battle - cower!!!
					pSoldier->aiData.usActionData = ANIM_CROUCH;
					return( AI_ACTION_COWER );
				}
				else // not in battle, cower for a certain length of time
				{
					pSoldier->aiData.bNextAction = AI_ACTION_WAIT;
					pSoldier->aiData.usNextActionData = (UINT16) REALTIME_CIV_AI_DELAY;
					pSoldier->aiData.usActionData = ANIM_CROUCH;
					return( AI_ACTION_COWER );
				}
			}
		}
	}

	// sevenfm: before deciding anything, stop cowering
	if( !fCivilian &&
		SoldierAI(pSoldier) &&
		pSoldier->stats.bLife > OKLIFE &&
		!pSoldier->bCollapsed &&
		!pSoldier->bBreathCollapsed &&
		(pSoldier->usAnimState == COWERING || pSoldier->usAnimState == COWERING_PRONE) )
	{
		// Suppression cowering should persist until effective shock falls below the
		// engine's own cowering threshold. Standing up immediately made suppression
		// largely cosmetic and exposed pinned soldiers again on their next AI turn.
		if (CoweringShockLevel(pSoldier) || !ubCanMove)
		{
			pSoldier->aiData.usActionData = NOWHERE;
			return AI_ACTION_NONE;
		}
		return AI_ACTION_STOP_COWERING;
	}

	DebugMsg(TOPIC_JA2, DBG_LEVEL_3, "decideactionred: calculate morale before combat commitment");
	// Break-contact state must be evaluated before weapon scavenging, long-range
	// attack setup and sniper/mortar decisions. Otherwise a newly collapsing soldier
	// can spend the turn on an offensive action before the retreat system runs.
	pSoldier->aiData.bAIMorale = CalcMorale(pSoldier);
	if (AICombatTeam(pSoldier))
	{
		// A shattered enemy element first tries to attach to another viable fireteam.
		// This must precede disengagement state updates; otherwise the last one/two
		// soldiers can acquire escape intent before cohesion gets a chance to absorb them.
		INT8 bCohesionAction = DecideFireteamCohesionAction(pSoldier, ubCanMove);
		if (bCohesionAction != AI_ACTION_NONE)
			return bCohesionAction;

		INT8 bDisengageAction = DecideDisengagementAction(pSoldier, ubCanMove);
		if (bDisengageAction != AI_ACTION_NONE)
			return bDisengageAction;
	}

	// If we don't have a gun, enemy combatants may scavenge one when the local
	// situation makes that movement reasonable. Militia never loot ground/sector
	// items during a fight; they fight with the equipment they entered with.
	if (AICombatTeam(pSoldier) &&
		pSoldier->bTeam != MILITIA_TEAM &&
		ubCanMove &&
		!pSoldier->aiData.bNeutral &&
		gTacticalStatus.bBoxingState == NOT_BOXING &&
		!TANK( pSoldier ) &&
		!AM_A_ROBOT( pSoldier ) &&
		!InWaterOrGas(pSoldier, pSoldier->sGridNo) &&
		FindAIUsableObjClass( pSoldier, IC_GUN ) == ITEM_NOT_FOUND)
	{
		BOOLEAN fSafeToScavengeWeapon =
			!AIShouldAvoidAdvance(pSoldier) &&
			!pSoldier->aiData.bUnderFire &&
			(AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) ||
			 TileIsOutOfBounds(sClosestOpponent) ||
			 PythSpacesAway(pSoldier->sGridNo, sClosestOpponent) > TACTICAL_RANGE / 2);

		if (fSafeToScavengeWeapon)
		{
			pSoldier->aiData.bAction = SearchForItems( pSoldier, SEARCH_WEAPONS, pSoldier->inv[HANDPOS].usItem );
			if(pSoldier->aiData.bAction != AI_ACTION_NONE)
			{
				return( pSoldier->aiData.bAction );
			}
		}
	}

	////////////////////////////////////////////////////////////////////////
	// IF POSSIBLE, FIRE LONG RANGE WEAPONS AT TARGETS REPORTED BY RADIO
	////////////////////////////////////////////////////////////////////////

	// can't do this in realtime, because the player could be shooting a gun or whatever at the same time!
	if (gfTurnBasedAI && 
		!fCivilian && 
		!bInWater && 
		!bInGas && 
		!(pSoldier->flags.uiStatusFlags & SOLDIER_BOXER) && 
		pSoldier->bActionPoints == pSoldier->bInitialActionPoints &&
		CanNPCAttack(pSoldier) == TRUE)
	{
		BestThrow.ubPossible = FALSE;    // by default, assume Throwing isn't possible
		CheckIfTossPossible(pSoldier,&BestThrow);

		if (BestThrow.ubPossible)
		{
			// sevenfm: allow using mortars, grenade launchers, flares and non lethal grenades in RED state
			if (Item[pSoldier->inv[BestThrow.bWeaponIn].usItem].mortar ||
				//Item[pSoldier->inv[ BestThrow.bWeaponIn ].usItem].cannon ||
				Item[pSoldier->inv[BestThrow.bWeaponIn].usItem].rocketlauncher ||
				Item[pSoldier->inv[BestThrow.bWeaponIn].usItem].grenadelauncher ||
				Item[pSoldier->inv[BestThrow.bWeaponIn].usItem].flare ||
				Item[pSoldier->inv[BestThrow.bWeaponIn].usItem].usItemClass & IC_GRENADE)
			{
				// if firing mortar make sure we have room
				if (Item[pSoldier->inv[BestThrow.bWeaponIn].usItem].mortar)
				{
					DebugAI(AI_MSG_INFO, pSoldier, String("using mortar, check room to deploy"));

					ubOpponentDir = AIDirection(pSoldier->sGridNo, BestThrow.sTarget);

					// Get new gridno!
					sCheckGridNo = NewGridNo(pSoldier->sGridNo, DirectionInc(ubOpponentDir));

					if (!OKFallDirection(pSoldier, sCheckGridNo, pSoldier->pathing.bLevel, ubOpponentDir, pSoldier->usAnimState))
					{
						DebugAI(AI_MSG_INFO, pSoldier, String("no room to deploy mortar, check if we can move behind"));

						// try behind us, see if there's room to move back
						sCheckGridNo = NewGridNo(pSoldier->sGridNo, DirectionInc(gOppositeDirection[ubOpponentDir]));
						if (OKFallDirection(pSoldier, sCheckGridNo, pSoldier->pathing.bLevel, gOppositeDirection[ubOpponentDir], pSoldier->usAnimState))
						{
							// sevenfm: check if we can reach this gridno
							INT32 iPathCost = EstimatePlotPath(pSoldier, sCheckGridNo, FALSE, FALSE, FALSE, DetermineMovementMode(pSoldier, AI_ACTION_GET_CLOSER), pSoldier->bStealthMode, FALSE, 0);
							if (!AIShouldAvoidAdvance(pSoldier) &&
								iPathCost != 0 &&
								iPathCost + BestThrow.ubAPCost + GetAPsToLook(pSoldier) + GetAPsCrouch(pSoldier, FALSE) <= pSoldier->bActionPoints)
							{
								DebugAI(AI_MSG_INFO, pSoldier, String("moving backwards to have more room to deploy mortar"));
								pSoldier->aiData.usActionData = sCheckGridNo;

								DebugAI(AI_MSG_INFO, pSoldier, String("prepare next action throw at spot %d level %d aimtime %d", BestThrow.sTarget, BestThrow.bTargetLevel, BestThrow.ubAimTime));

								// if necessary, swap the usItem
								if (BestThrow.bWeaponIn != HANDPOS)
								{
									DebugAI(AI_MSG_INFO, pSoldier, String("rearrange pocket"));
									RearrangePocket(pSoldier, HANDPOS, BestThrow.bWeaponIn, FOREVER);
								}

								pSoldier->aiData.usNextActionData = BestThrow.sTarget;
								pSoldier->aiData.bNextTargetLevel = BestThrow.bTargetLevel;
								pSoldier->aiData.bAimTime = BestThrow.ubAimTime;

								pSoldier->aiData.bNextAction = AI_ACTION_TOSS_PROJECTILE;

								return AI_ACTION_GET_CLOSER;
							}
						}

						// can't fire!
						BestThrow.ubPossible = FALSE;
					}
				}

				// if still possible
				if (BestThrow.ubPossible)
				{
					DebugAI(AI_MSG_INFO, pSoldier, String("prepare throw at spot %d level %d aimtime %d", BestThrow.sTarget, BestThrow.bTargetLevel, BestThrow.ubAimTime));

					// if necessary, swap the usItem
					if (BestThrow.bWeaponIn != HANDPOS)
					{
						DebugAI(AI_MSG_INFO, pSoldier, String("rearrange pocket"));
						RearrangePocket(pSoldier, HANDPOS, BestThrow.bWeaponIn, FOREVER);
					}

					// sevenfm: correctly set weapon mode for attached GL
					if (IsGrenadeLauncherAttached(&pSoldier->inv[HANDPOS]))
					{
						DebugAI(AI_MSG_INFO, pSoldier, String("set attached GL mode"));
						pSoldier->bWeaponMode = WM_ATTACHED_GL;
					}

					// stand up before throwing if needed
					if (gAnimControl[pSoldier->usAnimState].ubEndHeight < BestThrow.ubStance &&
						pSoldier->InternalIsValidStance(AIDirection(pSoldier->sGridNo, BestThrow.sTarget), BestThrow.ubStance))
					{
						pSoldier->aiData.usActionData = BestThrow.ubStance;
						pSoldier->aiData.bNextAction = AI_ACTION_TOSS_PROJECTILE;
						pSoldier->aiData.usNextActionData = BestThrow.sTarget;
						pSoldier->aiData.bNextTargetLevel = BestThrow.bTargetLevel;
						pSoldier->aiData.bAimTime = BestThrow.ubAimTime;
						return AI_ACTION_CHANGE_STANCE;
					}
					else
					{
						pSoldier->aiData.usActionData = BestThrow.sTarget;
						pSoldier->bTargetLevel = BestThrow.bTargetLevel;
						pSoldier->aiData.bAimTime = BestThrow.ubAimTime;
					}

					return(AI_ACTION_TOSS_PROJECTILE);
				}
			}
		}
		else		// toss/throw/launch not possible
		{
			// WDS - Fix problem when there is no "best thrown" weapon (i.e., BestThrow.bWeaponIn == NO_SLOT)
			// if this dude has a longe-range weapon on him (longer than normal
			// sight range), and there's at least one other team-mate around, and
			// spotters haven't already been called for, then DO SO!

			if ((BestThrow.bWeaponIn != NO_SLOT) &&
				!AIDisengagementActive(pSoldier) && !AIEscapeActive(pSoldier) &&
				(CalcMaxTossRange(pSoldier, pSoldier->inv[BestThrow.bWeaponIn].usItem, TRUE) > MaxNormalDistanceVisible()) &&
				(AICombatTeam(pSoldier) ? AIFireteamCombatReadyCount(pSoldier) > 1 : gTacticalStatus.Team[pSoldier->bTeam].bMenInSector > 1) &&
				(gTacticalStatus.ubSpottersCalledForBy == NOBODY))
			{
				// then call for spotters!  Uses up the rest of his turn (whatever
				// that may be), but from now on, BLACK AI NPC may radio sightings!
				gTacticalStatus.ubSpottersCalledForBy = pSoldier->ubID;
				// HEADROCK HAM 3.1: This may be causing problems with HAM's lowered AP limit. From now on, we'll check
				// whether the soldier has more than 0 APs to begin with.
				if (pSoldier->bActionPoints > 0)
					pSoldier->bActionPoints = 0;

				pSoldier->aiData.usActionData = NOWHERE;
				return(AI_ACTION_NONE);
			}
		}

		// SNIPER!
		// sevenfm: set bAimShotLocation
		pSoldier->bAimShotLocation = AIM_SHOT_RANDOM;
		CheckIfShotPossible(pSoldier, &BestShot);
DebugMsg (TOPIC_JA2,DBG_LEVEL_3,String("decideactionred: is sniper shot possible? = %d, CTH = %d",BestShot.ubPossible,BestShot.ubChanceToReallyHit));

		// sevenfm: changed sniper shot min CTH to 25%
		if (BestShot.ubPossible && BestShot.ubChanceToReallyHit > 25 )
		{
			// then do it!  The functions have already made sure that we have a
			// pair of worthy opponents, etc., so we're not just wasting our time

			// if necessary, swap the usItem from holster into the hand position
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: sniper shot possible!");
			if (BestShot.bWeaponIn != HANDPOS)
				RearrangePocket(pSoldier,HANDPOS,BestShot.bWeaponIn,FOREVER);

			pSoldier->aiData.usActionData = BestShot.sTarget;
			//POSSIBLE STRUCTURE CHANGE PROBLEM. GOTTHARD 7/14/08
			pSoldier->aiData.bAimTime	= BestShot.ubAimTime;
			pSoldier->bScopeMode		= BestShot.bScopeMode;
			if( BestShot.ubChanceToReallyHit > 50 )
			{
				ScreenMsg( FONT_MCOLOR_LTYELLOW, MSG_INTERFACE, New113Message[ MSG113_SNIPER ] );
			}			
			return(AI_ACTION_FIRE_GUN );
		}
		else		// snipe not possible
		{
			// if this dude has a longe-range weapon on him (longer than normal
			// sight range), and there's at least one other team-mate around, and
			// spotters haven't already been called for, then DO SO!

			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: sniper shot not possible");
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,String("decideactionred: weapon in slot #%d",BestShot.bWeaponIn));
			// WDS - Fix problem when there is no "best shot" weapon (i.e., BestShot.bWeaponIn == NO_SLOT)
			if (BestShot.bWeaponIn != NO_SLOT) {
				OBJECTTYPE * gun = &pSoldier->inv[BestShot.bWeaponIn];
				DebugMsg(TOPIC_JA2, DBG_LEVEL_3, String("decideactionred: men in sector %d, ubspotters called by %d, nobody %d", gTacticalStatus.Team[pSoldier->bTeam].bMenInSector, gTacticalStatus.ubSpottersCalledForBy, NOBODY));

				if (!AIDisengagementActive(pSoldier) && !AIEscapeActive(pSoldier) &&
					GunRange(gun, pSoldier) > MaxNormalDistanceVisible() &&
					(IsScoped(gun) || pSoldier->aiData.bOrders == SNIPER) &&
					(AICombatTeam(pSoldier) ? AIFireteamCombatReadyCount(pSoldier) > 1 : gTacticalStatus.Team[pSoldier->bTeam].bMenInSector > 1) &&
					(gTacticalStatus.ubSpottersCalledForBy == NOBODY))
				{
					// then call for spotters!  Uses up the rest of his turn (whatever
					// that may be), but from now on, BLACK AI NPC may radio sightings!
					gTacticalStatus.ubSpottersCalledForBy = pSoldier->ubID;
					// HEADROCK HAM 3.1: This may be causing problems with HAM's lowered AP limit. From now on, we'll check
					// whether the soldier has more than 0 APs to begin with.
					if (pSoldier->bActionPoints > 0)
						pSoldier->bActionPoints = 0;

					DebugMsg(TOPIC_JA2, DBG_LEVEL_3, "decideactionred: calling for sniper spotters");

					pSoldier->aiData.usActionData = NOWHERE;
					return(AI_ACTION_NONE);
				}
			}			
		}
		//RELOADING

		// A nearby ally under pressure, withdrawing, or making an exposed bound
		// toward this exact opponent can create a specific covering-fire task.
		BOOLEAN fCoveringFireSupport = BestShot.ubPossible &&
			BestShot.ubOpponent != NOBODY &&
			(AIFriendNeedsCoveringFire(pSoldier, BestShot.ubOpponent) ||
			 AIFriendWithdrawingNeedsCover(pSoldier, BestShot.ubOpponent) ||
			 AIFriendAdvancingNeedsCover(pSoldier, BestShot.ubOpponent));
		BOOLEAN fDoctrineProactiveSupport = AIAllowsProactiveSupport(pSoldier);

		// WarmSteel - Because of suppression fire, we need enough ammo to even consider suppressing
		// This means we need to reload. Also reload if we're just plainly low on bullets.
		// sevenfm: no reloads for tanks
		if( BestShot.bWeaponIn != NO_SLOT &&
			!TANK(pSoldier) &&
			pSoldier->bActionPoints > APBPConstants[AP_MINIMUM] &&
			(fCoveringFireSupport || fDoctrineProactiveSupport &&
			 (!pSoldier->aiData.bUnderFire && !GuySawEnemy(pSoldier, SEEN_LAST_TURN) &&
			  (TileIsOutOfBounds(sClosestOpponent) || PythSpacesAway(pSoldier->sGridNo, sClosestOpponent) > TACTICAL_RANGE / 2) ||
			  AICheckIsMachinegunner(pSoldier) && Chance(25) || Chance(10))) &&
			IsGunAutofireCapable(&pSoldier->inv[BestShot.bWeaponIn]) &&
			Weapon[pSoldier->inv[BestShot.bWeaponIn].usItem].swapClips &&
			pSoldier->inv[BestShot.bWeaponIn][0]->data.gun.ubGunShotsLeft < gGameExternalOptions.ubAISuppressionMinimumAmmo &&
			GetMagSize(&pSoldier->inv[BestShot.bWeaponIn]) >= gGameExternalOptions.ubAISuppressionMinimumMagSize)
		{
			// HEADROCK HAM 5: Fixed an issue where no ammo was found, leading to a crash when overloading the
			// inventory vector (bAmmoSlot = -1...)
			INT8 bAmmoSlot = FindAmmoToReload( pSoldier, BestShot.bWeaponIn, NO_SLOT );
			if (bAmmoSlot > -1)
			{
				OBJECTTYPE * pAmmo = &(pSoldier->inv[bAmmoSlot]);
				if((*pAmmo)[0]->data.ubShotsLeft > pSoldier->inv[BestShot.bWeaponIn][0]->data.gun.ubGunShotsLeft && 
					GetAPsToReloadGunWithAmmo( pSoldier, &(pSoldier->inv[BestShot.bWeaponIn]), pAmmo ) <= (INT16)pSoldier->bActionPoints)
				{
					pSoldier->aiData.usActionData = BestShot.bWeaponIn;
					return AI_ACTION_RELOAD_GUN;
				}
			}
		}

		//SUPPRESSION FIRE

		// sevenfm: set bAimShotLocation
		//pSoldier->bAimShotLocation = AIM_SHOT_RANDOM;
		//CheckIfShotPossible(pSoldier, &BestShot); //WarmSteel - No longer returns 0 when there IS actually a chance to hit.

		// sevenfm: check that we have a clip to reload
		BOOLEAN fExtraClip = FALSE;
		if(BestShot.bWeaponIn != NO_SLOT)
		{
			INT8 bAmmoSlot = FindAmmoToReload( pSoldier, BestShot.bWeaponIn, NO_SLOT );
			if (bAmmoSlot != NO_SLOT)
			{
				fExtraClip = TRUE;
			}
		}

		// Security/ordinary line troops suppress reactively (direct contact, return fire,
		// or a specific covering-fire task). Veterans/elites may establish fire proactively.
		BOOLEAN fDoctrineSuppressionTask = fCoveringFireSupport ||
			pSoldier->aiData.bUnderFire ||
			GuySawEnemy(pSoldier, SEEN_LAST_TURN) ||
			fDoctrineProactiveSupport;

		//must have a small chance to hit and the opponent must be on the ground (can't suppress guys on the roof)
		// HEADROCK HAM BETA2.4: Adjusted this for a random chance to suppress regardless of chance. This augments
		// current revamp of suppression fire.

		// CHRISL: Changed from a simple flag to two externalized values for more modder control over AI suppression
		// WarmSteel - Don't *always* try to suppress when under 50 CTH
		if (fDoctrineSuppressionTask &&
			BestShot.ubPossible &&
			BestShot.bWeaponIn != NO_SLOT &&
			// check valid target
			!TileIsOutOfBounds(BestShot.sTarget) &&
			BestShot.ubOpponent != NOBODY &&
			MercPtrs[BestShot.ubOpponent] &&
			// check weapon/ammo requirements
			IsGunAutofireCapable(&pSoldier->inv[BestShot.bWeaponIn]) &&
			GetMagSize(&pSoldier->inv[BestShot.bWeaponIn]) >= gGameExternalOptions.ubAISuppressionMinimumMagSize &&
			pSoldier->inv[BestShot.bWeaponIn][0]->data.gun.ubGunShotsLeft >= gGameExternalOptions.ubAISuppressionMinimumAmmo &&
			// check soldier and weapon
			pSoldier->aiData.bOrders != SNIPER &&
			BestShot.ubFriendlyFireChance <= MIN_CHANCE_TO_ACCIDENTALLY_HIT_SOMEONE &&
			!pSoldier->IsFlanking() &&
			// check cover
			(AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) ||																				// safe position
			fCoveringFireSupport ||																				// cover a nearby ally's movement/withdrawal
			NightLight() && CountFriendsFlankSameSpot(pSoldier) && Chance(50) ||
			TANK(pSoldier) ||																		// tanks don't need cover
			pSoldier->aiData.bUnderFire && (pSoldier->ubPreviousAttackerID == BestShot.ubOpponent || pSoldier->ubNextToPreviousAttackerID == BestShot.ubOpponent) ||	// return fire
			Chance((BestShot.ubChanceToReallyHit + 100) / 2) ||											// 50% chance to fire without cover
			LocationToLocationLineOfSightTest(pSoldier->sGridNo, pSoldier->pathing.bLevel, BestShot.sTarget, BestShot.bTargetLevel, TRUE, MAX_VISION_RANGE)) &&		// can see target after turning
			// reduce chance to shoot if target is beyond weapon range
			(AICheckIsMachinegunner(pSoldier) ||
			fCoveringFireSupport ||
			TANK(pSoldier) ||
			AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) ||
			pSoldier->aiData.bUnderFire && (pSoldier->ubPreviousAttackerID == BestShot.ubOpponent || pSoldier->ubNextToPreviousAttackerID == BestShot.ubOpponent) ||	// return fire
			Chance(100 * (GunRange(&pSoldier->inv[BestShot.bWeaponIn], pSoldier) / CELL_X_SIZE) / PythSpacesAway(pSoldier->sGridNo, BestShot.sTarget))) &&
			// check that we have spare ammo
			(fExtraClip || pSoldier->inv[BestShot.bWeaponIn][0]->data.gun.ubGunShotsLeft >= gGameExternalOptions.ubAISuppressionMinimumMagSize))
		{
			// then do it!

			// if necessary, swap the usItem from holster into the hand position
			if (BestShot.bWeaponIn != HANDPOS)
			{
				RearrangePocket(pSoldier, HANDPOS, BestShot.bWeaponIn, FOREVER);
			}

			pSoldier->bTargetLevel = BestShot.bTargetLevel;
			pSoldier->aiData.bAimTime = BestShot.ubAimTime;
			pSoldier->bDoAutofire = 0;
			pSoldier->bDoBurst = 1;
			pSoldier->bScopeMode = BestShot.bScopeMode;

			INT16 ubBurstAPs = 0;
			FLOAT dTotalRecoil = 0;
			INT32 sActualAimAP;
			UINT8 ubAutoPenalty;
			INT16 sReserveAP = GetAPsProne(pSoldier, TRUE);
			UINT8 ubMinAuto = 5;

			if (BestShot.ubAimTime > 0 &&
				!UsingNewCTHSystem() &&
				Chance((100 - BestShot.ubChanceToReallyHit) * (100 - BestShot.ubChanceToReallyHit) / 100))
			{
				BestShot.ubAimTime = 0;
			}

			// reserve APs to hide if no cover or enemy is close
			if (!AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) || PythSpacesAway(pSoldier->sGridNo, BestShot.sTarget) < DAY_VISION_RANGE / 2)
			{
				sReserveAP = APBPConstants[AP_MINIMUM] / 2;
			}
			if (PythSpacesAway(pSoldier->sGridNo, BestShot.sTarget) > DAY_VISION_RANGE || AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) || pSoldier->aiData.bUnderFire)
			{
				ubMinAuto *= 2;
			}

			// Deliberate movement-support fire should be controlled, not a full
			// magazine dump just because the shooter happens to be behind cover.
			if (fCoveringFireSupport)
			{
				ubMinAuto = AICheckIsMachinegunner(pSoldier) ? 7 : 5;
			}

			sActualAimAP = CalcAPCostForAiming(pSoldier, BestShot.sTarget, (INT8)pSoldier->aiData.bAimTime);

			if (UsingNewCTHSystem() == true)
			{
				do
				{
					pSoldier->bDoAutofire++;
					dTotalRecoil += AICalcRecoilForShot(pSoldier, &(pSoldier->inv[BestShot.bWeaponIn]), pSoldier->bDoAutofire);
					ubBurstAPs = CalcAPsToAutofire(pSoldier->CalcActionPoints(), &(pSoldier->inv[BestShot.bWeaponIn]), pSoldier->bDoAutofire, pSoldier);
				} while (pSoldier->bActionPoints >= BestShot.ubAPCost + sActualAimAP + ubBurstAPs + sReserveAP &&
					pSoldier->inv[pSoldier->ubAttackingHand][0]->data.gun.ubGunShotsLeft >= pSoldier->bDoAutofire &&
					pSoldier->bDoAutofire <= 30 &&
					(dTotalRecoil <= 20.0f || pSoldier->bDoAutofire < ubMinAuto));
			}
			else
			{
				ubAutoPenalty = GetAutoPenalty(pSoldier, &pSoldier->inv[pSoldier->ubAttackingHand], gAnimControl[pSoldier->usAnimState].ubEndHeight == ANIM_PRONE);
				do
				{
					pSoldier->bDoAutofire++;
					ubBurstAPs = CalcAPsToAutofire(pSoldier->CalcActionPoints(), &(pSoldier->inv[BestShot.bWeaponIn]), pSoldier->bDoAutofire, pSoldier);
				} while (pSoldier->bActionPoints >= BestShot.ubAPCost + sActualAimAP + ubBurstAPs + sReserveAP &&
					pSoldier->inv[pSoldier->ubAttackingHand][0]->data.gun.ubGunShotsLeft >= pSoldier->bDoAutofire &&
					pSoldier->bDoAutofire <= 30 &&
					(ubAutoPenalty * pSoldier->bDoAutofire <= 80 || pSoldier->bDoAutofire < ubMinAuto));
			}

			pSoldier->bDoAutofire--;

			// Make sure we decided to fire at least one shot!
			ubBurstAPs = CalcAPsToAutofire(pSoldier->CalcActionPoints(), &(pSoldier->inv[BestShot.bWeaponIn]), pSoldier->bDoAutofire, pSoldier);

			// minimum 3 bullets
			if (pSoldier->bDoAutofire >= 3 && pSoldier->bActionPoints >= BestShot.ubAPCost + sActualAimAP + ubBurstAPs + sReserveAP)
			{
				if (gAnimControl[pSoldier->usAnimState].ubEndHeight != BestShot.ubStance &&
					IsValidStance(pSoldier, BestShot.ubStance))
				{
					pSoldier->aiData.bNextAction = AI_ACTION_FIRE_GUN;
					pSoldier->aiData.usNextActionData = BestShot.sTarget;
					pSoldier->aiData.bNextTargetLevel = BestShot.bTargetLevel;
					pSoldier->aiData.usActionData = BestShot.ubStance;

					ScreenMsg(FONT_MCOLOR_LTYELLOW, MSG_INTERFACE, New113Message[MSG113_SUPPRESSIONFIRE]);
					return(AI_ACTION_CHANGE_STANCE);
				}
				else
				{
					pSoldier->aiData.usActionData = BestShot.sTarget;

					ScreenMsg(FONT_MCOLOR_LTYELLOW, MSG_INTERFACE, New113Message[MSG113_SUPPRESSIONFIRE]);
					return(AI_ACTION_FIRE_GUN);
				}
			}
			else
			{
				pSoldier->bDoBurst = 0;
				pSoldier->bDoAutofire = 0;
			}
		}
		// suppression not possible, do something else

		// Flugente: trait skills
		// if we are a radio operator
		if (HAS_SKILL_TRAIT(pSoldier, RADIO_OPERATOR_NT) > 0 && pSoldier->CanUseSkill(SKILLS_RADIO_ARTILLERY, TRUE))
		{
			UINT32 tmp;
			INT32 skilltargetgridno = 0;

			// call reinforcements if we haven't yet done so
			if (!gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition && 
				MoreFriendsThanEnemiesinNearbysectors(pSoldier->bTeam, pSoldier->sSectorX, pSoldier->sSectorY, pSoldier->bSectorZ))
			{
				// if frequencies are jammed...
				if (SectorJammed())
				{
					// if we are jamming, turn it off, otherwise, bad luck...
					if (pSoldier->IsJamming())
					{
						pSoldier->usAISkillUse = SKILLS_RADIO_TURNOFF;
						pSoldier->aiData.usActionData = skilltargetgridno;
						return(AI_ACTION_USE_SKILL);
					}
				}
				// frequencies are clear, lets call for help
				else if (!(pSoldier->usSoldierFlagMask & SOLDIER_RAISED_REDALERT))
				{
					// raise alarm!
					return(AI_ACTION_RED_ALERT);
				}
			}
			// if we can't call in artillery, jam frequencies, so that the player can't use radio skills
			else if (pSoldier->CanAnyArtilleryStrikeBeOrdered(&tmp))
			{
				if (SectorJammed())
				{
					if (pSoldier->IsJamming())
					{
						pSoldier->usAISkillUse = SKILLS_RADIO_TURNOFF;
						pSoldier->aiData.usActionData = skilltargetgridno;
						return(AI_ACTION_USE_SKILL);
					}
				}
				else if (gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition &&
					AISelectKnownArtilleryTarget(pSoldier, &skilltargetgridno))
				{
					pSoldier->usAISkillUse = SKILLS_RADIO_ARTILLERY;
					pSoldier->aiData.usActionData = skilltargetgridno;
					return(AI_ACTION_USE_SKILL);
				}
			}
			else if (!pSoldier->IsJamming())
			{
				pSoldier->usAISkillUse = SKILLS_RADIO_JAM;
				pSoldier->aiData.usActionData = skilltargetgridno;
				return(AI_ACTION_USE_SKILL);
			}
		}
	}

	if( gGameExternalOptions.bNewTacticalAIBehavior )
	{
		////////////////////////////////////////////////////////////////////////////
		// IF YOU SEE CAPTURED FRIENDS, FREE THEM!
		////////////////////////////////////////////////////////////////////////////

		// Flugente: if we see one of our buddies captured, it is a clear sign of enemy activity!
		if ( gGameExternalOptions.fAllowPrisonerSystem && pSoldier->bTeam == ENEMY_TEAM )
		{
			UINT8 ubPerson = GetClosestFlaggedSoldierID( pSoldier, 20, ENEMY_TEAM, SOLDIER_POW, TRUE );

			if ( ubPerson != NOBODY )
			{
				// if we are close, we can release this guy
				// possible only if not handcuffed (binders can be opened, handcuffs not)
				if ( !HasItemFlag( (&(MercPtrs[ubPerson]->inv[HANDPOS]))->usItem, HANDCUFFS ) )
				{
					if ( PythSpacesAway(pSoldier->sGridNo, MercPtrs[ubPerson]->sGridNo) < 2 )
					{
						// see if we are facing this person
						UINT8 ubDesiredMercDir = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(MercPtrs[ubPerson]->sGridNo),CenterY(MercPtrs[ubPerson]->sGridNo));

						// if not already facing in that direction,
						if ( pSoldier->ubDirection != ubDesiredMercDir )
						{
							pSoldier->aiData.usActionData = ubDesiredMercDir;

							return( AI_ACTION_CHANGE_FACING );
						}

						return(AI_ACTION_FREE_PRISONER);
					}
					else
					{
						pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier, MercPtrs[ubPerson]->sGridNo, 20, AI_ACTION_SEEK_FRIEND, 0);

						if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
						{
							return(AI_ACTION_SEEK_FRIEND);
						}
					}
				}
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// WHEN IN THE LIGHT, GET OUT OF THERE!
	////////////////////////////////////////////////////////////////////////////
	/*if ( ubCanMove && InLightAtNight( pSoldier->sGridNo, pSoldier->pathing.bLevel ) && pSoldier->aiData.bOrders != STATIONARY )
	{
		//ddd for the enemy to run away from lighht
		if(gGameExternalOptions.bNewTacticalAIBehavior)
			pSoldier->aiData.bAction = AI_ACTION_LEAVE_WATER_GAS;
		//ddd

		pSoldier->aiData.usActionData = FindNearbyDarkerSpot( pSoldier );
		if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
		{
			// move as if leaving water or gas
			return( AI_ACTION_LEAVE_WATER_GAS );
		}
	}*/

	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: crouch and rest if running out of breath");
	////////////////////////////////////////////////////////////////////////
	// CROUCH & REST IF RUNNING OUT OF BREATH
	////////////////////////////////////////////////////////////////////////

	// if our breath is running a bit low, and we're not in water or under fire
	if ((pSoldier->bBreath < 25) && !bInWater && !pSoldier->aiData.bUnderFire)
	{
		// if not already crouched, try to crouch down first
		if (!fCivilian && !PTR_CROUCHED && IsValidStance( pSoldier, ANIM_CROUCH ) && gAnimControl[ pSoldier->usAnimState ].ubHeight != ANIM_PRONE)
		{
			if (!gfTurnBasedAI || GetAPsToChangeStance( pSoldier, ANIM_CROUCH ) <= pSoldier->bActionPoints)
			{
				pSoldier->aiData.usActionData = ANIM_CROUCH;

				return(AI_ACTION_CHANGE_STANCE);
			}
		}

		pSoldier->aiData.usActionData = NOWHERE;

		// is it a heavy gun? And we have energy cost for shooting enabled? 
		if ( WeaponReady(pSoldier) && GetBPCostPer10APsForGunHolding( pSoldier ) > 0 )
		{
			// unready
			return(AI_ACTION_LOWER_GUN); 
		}
		return(AI_ACTION_NONE);
	}


	// Emergency smoke can create the safe window needed for casualty treatment or
	// a heavily suppressed soldier's disengagement.
	if (AICombatTeam(pSoldier))
	{
		INT8 bSmokeAction = DecideEmergencyProtectionSmoke(pSoldier);
		if (bSmokeAction != AI_ACTION_NONE)
			return bSmokeAction;
	}

	// If several soldiers are packed together under fire, break the cluster before
	// ordinary withdrawal/offensive logic makes them easy grenade targets.
	if (ubCanMove && AICombatTeam(pSoldier))
	{
		INT8 bDisperseAction = DecideCombatDispersion(pSoldier);
		if (bDisperseAction != AI_ACTION_NONE)
			return bDisperseAction;
	}
	// A soldier whose own element is engaged closes back toward that element
	// before independently wandering into another fight. One/two-man remnants
	// are absorbed into the nearest viable element by this same helper.
	if (AICombatTeam(pSoldier))
	{
		INT8 bCohesionAction = DecideFireteamCohesionAction(pSoldier, ubCanMove);
		if (bCohesionAction != AI_ACTION_NONE)
			return bCohesionAction;
	}

	// If the local fight has collapsed, stop initiating attacks into superior known
	// opposition. This uses only Chunk 1 perceived knowledge and existing withdrawal/cover.
	if (AICombatTeam(pSoldier) && !AIDisengagementActive(pSoldier) && AIShouldAvoidAdvance(pSoldier))
	{
		INT8 bSurvivorAction = DecideHopelessSurvivorAction(pSoldier, ubCanMove);
		if (bSurvivorAction != AI_ACTION_NONE)
			return bSurvivorAction;
	}
	// Giving ground is a normal tactical option, not only a panic response.
	if (AICombatTeam(pSoldier) && !AIDisengagementActive(pSoldier))
	{
		INT8 bFallbackAction = DecideTacticalFallback(pSoldier, ubCanMove);
		if (bFallbackAction != AI_ACTION_NONE)
			return bFallbackAction;
	}
	// A medic gets first refusal on a viable battlefield casualty before self-aid.
	// The rescue routine already rejects the attempt if his own risk is too high.
	if (AICombatTeam(pSoldier) && AICheckIsMedic(pSoldier))
	{
		INT8 bMedicCasualtyAction = DecideCombatCasualtyResponse(pSoldier, ubCanMove);
		if (bMedicCasualtyAction != AI_ACTION_NONE)
			return bMedicCasualtyAction;
	}

	// A badly bleeding soldier stabilizes himself if no viable medic rescue pre-empted it.
	if (AICombatTeam(pSoldier))
	{
		INT8 bSelfAidAction = DecideEmergencySelfAid(pSoldier);
		if (bSelfAidAction != AI_ACTION_NONE)
			return bSelfAidAction;
	}


	// Tactical self-preservation: withdraw when this soldier's personal danger
	// exceeds what his personality and morale are willing to tolerate.
	if (gfTurnBasedAI &&
		AICombatTeam(pSoldier) &&
		!AIDisengagementActive(pSoldier) &&
		ubCanMove &&
		pSoldier->aiData.bOrders != STATIONARY &&
		pSoldier->stats.bLife >= OKLIFE &&
		pSoldier->aiData.bAIMorale != MORALE_HOPELESS &&
		AIPersonalRisk(pSoldier) > AIPersonalRiskTolerance(pSoldier) &&
		(pSoldier->aiData.bUnderFire ||
		 !AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) ||
		 AICountNearbyOperationalFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 4) == 0))
	{
		INT32 sWithdrawalThreat = ClosestKnownOpponent(pSoldier, NULL, NULL);
		if (!TileIsOutOfBounds(sWithdrawalThreat))
		{
			pSoldier->aiData.usActionData = FindRetreatSpot(pSoldier);
			if (TileIsOutOfBounds(pSoldier->aiData.usActionData))
				pSoldier->aiData.usActionData = FindFlankingSpot(pSoldier, sWithdrawalThreat, AI_ACTION_WITHDRAW);
			if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
			{
				return(AI_ACTION_WITHDRAW);
			}
		}
	}

	// Shared casualty policy: extraction first, then medic rescue or adjacent buddy aid.
	// The underlying routines retain their own route-exposure and personal-risk gates.
	if (AICombatTeam(pSoldier) && !AICheckIsMedic(pSoldier))
	{
		INT8 bCasualtyAction = DecideCombatCasualtyResponse(pSoldier, ubCanMove);
		if (bCasualtyAction != AI_ACTION_NONE)
			return bCasualtyAction;
	}
// WDS DEBUG - this will make all enemies run away (to test retreating into occupied sector bugs)
//	pSoldier->aiData.bAIMorale = MORALE_HOPELESS;

	// if a guy is feeling REALLY discouraged, he may continue to run like hell
	if ((pSoldier->aiData.bAIMorale == MORALE_HOPELESS) && ubCanMove)
	{
		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: run away");
		////////////////////////////////////////////////////////////////////////
		// RUN AWAY TO SPOT FARTHEST FROM KNOWN THREATS (ONLY IF MORALE HOPELESS)
		////////////////////////////////////////////////////////////////////////

		// look for best place to RUN AWAY to (farthest from the closest threat)
		pSoldier->aiData.usActionData = FindSpotMaxDistFromOpponents(pSoldier);
		
		if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
		{
			return(AI_ACTION_RUN_AWAY);
		}
	}


	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: radio red alert?");
	////////////////////////////////////////////////////////////////////////////
	// RADIO RED ALERT: determine %chance to call others and report contact
	////////////////////////////////////////////////////////////////////////////

	// if we're a computer merc, and we have the action points remaining to RADIO
	// (we never want NPCs to choose to radio if they would have to wait a turn)
	if ( !(pSoldier->usSoldierFlagMask & SOLDIER_RAISED_REDALERT) && !fCivilian && (pSoldier->bActionPoints >= APBPConstants[AP_RADIO]) && (gTacticalStatus.Team[pSoldier->bTeam].bMenInSector > 1) )
	{

		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: checking to radio red alert");

		// if there hasn't been an initial RED ALERT yet in this sector
		if ( !(gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition) || NeedToRadioAboutPanicTrigger() )
			// since I'm at STATUS RED, I obviously know we're being invaded!
			iChance = gbDiff[DIFF_RADIO_RED_ALERT][ SoldierDifficultyLevel( pSoldier ) ];
		else // subsequent radioing (only to update enemy positions, request help)
			// base chance depends on how much new info we have to radio to the others
			iChance = 10 * WhatIKnowThatPublicDont(pSoldier,FALSE);  // use 10 * for RED alert

		// if I actually know something they don't and I ain't swimming (deep water)
		if (iChance && !bInDeepWater)
		{
			// modify base chance according to orders
			switch (pSoldier->aiData.bOrders)
			{
			case STATIONARY:       iChance +=  20;  break;
			case ONGUARD:          iChance +=  15;  break;
			case ONCALL:           iChance +=  10;  break;
			case CLOSEPATROL:                       break;
			case RNDPTPATROL:
			case POINTPATROL:      iChance +=  -5;  break;
			case FARPATROL:        iChance += -10;  break;
			case SEEKENEMY:        iChance += -20;  break;
			case SNIPER:			  iChance += -10;  break; // Sniper contacts should be reported automatically
			}

			// modify base chance according to attitude
			switch (pSoldier->aiData.bAttitude)
			{
			case DEFENSIVE:        iChance +=  20;  break;
			case BRAVESOLO:        iChance += -10;  break;
			case BRAVEAID:                          break;
			case CUNNINGSOLO:      iChance +=  -5;  break;
			case CUNNINGAID:                        break;
			case AGGRESSIVE:       iChance += -20;  break;
			case ATTACKSLAYONLY:		iChance = 0;
			}

			if ( (gTacticalStatus.fPanicFlags & PANIC_TRIGGERS_HERE) && !gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition)
			{
				// ignore morale (which could be really high
			}
			else
			{
				// modify base chance according to morale
				switch (pSoldier->aiData.bAIMorale)
				{
				case MORALE_HOPELESS:  iChance *= 3;    break;
				case MORALE_WORRIED:   iChance *= 2;    break;
				case MORALE_NORMAL:                     break;
				case MORALE_CONFIDENT: iChance /= 2;    break;
				case MORALE_FEARLESS:  iChance /= 3;    break;
				}
			}

			if ((INT16) PreRandom(100) < iChance)
			{
				DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: decided to radio red alert");
				return(AI_ACTION_RED_ALERT);
			}
		}
	}

	// sevenfm: no Main Red AI for civilians
	if (!TANK(pSoldier) && !fCivilian)
	{
		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: main red ai");

		////////////////////////////////////////////////////////////////////////////
		// MAIN RED AI: Decide soldier's preference between SEEKING,HELPING & HIDING
		////////////////////////////////////////////////////////////////////////////

		// get the location of the closest reachable opponent
		sClosestDisturbance = ClosestReachableDisturbance(pSoldier, &fSeekClimb);
		// determine opponent level
		if (fSeekClimb)
		{
			if (pSoldier->pathing.bLevel > 0)
				bClosestDisturbanceLevel = 0;
			else
				bClosestDisturbanceLevel = 1;
		}
		else
		{
			bClosestDisturbanceLevel = pSoldier->pathing.bLevel;
		}
		sClosestFriend = ClosestReachableFriendInTrouble(pSoldier, &fHelpClimb);

		// sevenfm: avoid light if spot is dangerous
		if (ubCanMove &&
			InLightAtNight(pSoldier->sGridNo, pSoldier->pathing.bLevel) &&
			//pSoldier->aiData.bOrders != STATIONARY &&			
			!InSmoke(pSoldier->sGridNo, pSoldier->pathing.bLevel) &&
			(pSoldier->aiData.bUnderFire || pSoldier->aiData.bOrders != SEEKENEMY || !SightCoverAtSpot(pSoldier, pSoldier->sGridNo, FALSE) || AICorpseWarningKnown(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel) > 0) &&
			(CountFriendsFlankSameSpot(pSoldier, sClosestDisturbance) > 0 || !AICheckSuccessfulAttack(pSoldier, TRUE) || pSoldier->aiData.bOrders != SEEKENEMY) &&
			CountFriendsBlack(pSoldier, sClosestDisturbance) == 0)
		{
			pSoldier->aiData.usActionData = FindNearbyDarkerSpot(pSoldier);

			if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
			{
				// move as if leaving water or gas
				return(AI_ACTION_LEAVE_WATER_GAS);
			}
		}

		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: check to continue flanking");
		// possibly continue flanking
		bActionReturned = DecideContinueFlanking(pSoldier, sClosestDisturbance);
		if (bActionReturned != -1)
			return bActionReturned;		

		// stop end flanking approach if cannot find enemy or have contact with enemy or group has successful attack
		if (pSoldier->numFlanks == MAX_FLANKS_RED &&
			(TileIsOutOfBounds(sClosestDisturbance) ||
			pSoldier->aiData.bUnderFire ||
			GuySawEnemy(pSoldier) ||
			CountFriendsBlack(pSoldier, sClosestDisturbance) > 0))
		{
			pSoldier->numFlanks++;
		}

		// A completed flank can now be exploited as a cautious bound onto the flank
		// objective, subject to fireteam support and exposure checks.
		if (pSoldier->numFlanks == MAX_FLANKS_RED)
		{
			bActionReturned = DecideFinishFlanking(pSoldier, sClosestDisturbance, bClosestDisturbanceLevel);
			if (bActionReturned != -1)
				return bActionReturned;
		}

		// sevenfm: when we finished flanking, try to reach lastFlankSpot position
		// seek until we are close (DistanceVisible/2) and have line of sight to lastFlankSpot position
		// don't seek if we have seen enemy recently or under fire or have shock
		// don't seek if we have low AP (tired, wounded)
		/*if ( pSoldier->numFlanks == MAX_FLANKS_RED )
		{
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: stop flanking");

			// start end flank approach with full APs
			if( gfTurnBasedAI && pSoldier->bActionPoints < pSoldier->bInitialActionPoints )
			{
				return(AI_ACTION_END_TURN);
			}

			if( !TileIsOutOfBounds(tempGridNo) &&
				!GuySawEnemy(pSoldier) &&
				!pSoldier->aiData.bUnderFire &&
				!Water(pSoldier->sGridNo, pSoldier->pathing.bLevel) &&
				pSoldier->bInitialActionPoints >= APBPConstants[AP_MINIMUM] &&
				( PythSpacesAway( pSoldier->sGridNo, tempGridNo ) > MIN_FLANK_DIST_RED ||
				!LocationToLocationLineOfSightTest( pSoldier->sGridNo, pSoldier->pathing.bLevel, tempGridNo, pSoldier->pathing.bLevel, TRUE, CALC_FROM_ALL_DIRS) ) )
			{				
				pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier,tempGridNo,GetAPsCrouch( pSoldier, TRUE),AI_ACTION_SEEK_OPPONENT,0);

				// sevenfm: avoid going into water, gas or light
				if( !TileIsOutOfBounds(pSoldier->aiData.usActionData) &&
					!Water(pSoldier->aiData.usActionData, pSoldier->pathing.bLevel) &&
					!InGas( pSoldier, pSoldier->aiData.usActionData ) &&
					!InLightAtNight( pSoldier->aiData.usActionData, pSoldier->pathing.bLevel ) )
				{
					// if soldier can be seen at new position and he cannot be seen at his current position
					if ( LocationToLocationLineOfSightTest( pSoldier->aiData.usActionData, pSoldier->pathing.bLevel, tempGridNo, pSoldier->pathing.bLevel, TRUE, CALC_FROM_ALL_DIRS) &&
						!LocationToLocationLineOfSightTest( pSoldier->sGridNo, pSoldier->pathing.bLevel, tempGridNo, pSoldier->pathing.bLevel, TRUE, CALC_FROM_ALL_DIRS) )
					{
						// reserve APs for a possible crouch plus a shot
						INT32 sCautiousGridNo = InternalGoAsFarAsPossibleTowards(pSoldier, tempGridNo, (INT8) (MinAPsToAttack( pSoldier, tempGridNo, ADDTURNCOST,0) + GetAPsCrouch( pSoldier, TRUE) + GetAPsToLook(pSoldier)), AI_ACTION_SEEK_OPPONENT, FLAG_CAUTIOUS );

						if (!TileIsOutOfBounds(sCautiousGridNo))
						{
							pSoldier->aiData.usActionData = sCautiousGridNo;
							pSoldier->aiData.fAIFlags |= AI_CAUTIOUS;
							pSoldier->aiData.bNextAction = AI_ACTION_END_TURN;
							return(AI_ACTION_SEEK_OPPONENT);
						}
						return(AI_ACTION_SEEK_OPPONENT);
					}
					else
					{
						return(AI_ACTION_SEEK_OPPONENT);
					}
				}
				else
				{
					// if we cannot advance to spot, stop trying
					pSoldier->numFlanks++;
				}
			}
			else
			{	
				// stop
				pSoldier->numFlanks++;
			}
		}*/
		
		// Set watched location
		if (pSoldier->CheckInitialAP() &&
			pSoldier->bActionPoints >= APBPConstants[AP_MINIMUM] &&
			gfTurnBasedAI &&
			pSoldier->pathing.bLevel == 0 &&
			!pSoldier->aiData.bUnderFire &&
			!InLightAtNight(pSoldier->sGridNo, pSoldier->pathing.bLevel) &&
			SightCoverAtSpot(pSoldier, pSoldier->sGridNo, TRUE) &&
			!GuySawEnemy(pSoldier) &&
			!TileIsOutOfBounds(sClosestDisturbance) &&
			//!fSeekClimb &&
			PythSpacesAway(pSoldier->sGridNo, sClosestDisturbance) < DAY_VISION_RANGE &&
			(pSoldier->aiData.bOrders == STATIONARY || RangeChangeDesire(pSoldier) < 4) &&
			!SoldierToVirtualSoldierLineOfSightTest(pSoldier, sClosestDisturbance, pSoldier->pathing.bLevel, ANIM_STAND, TRUE, CALC_FROM_ALL_DIRS) &&
			CountFriendsBlack(pSoldier, sClosestDisturbance) == 0)
		{
			gubNPCAPBudget = 0;
			gubNPCDistLimit = 0;

			// check path to closest disturbance and find the point where enemy will appear in sight						
			if (FindBestPath(pSoldier, sClosestDisturbance, pSoldier->pathing.bLevel, RUNNING, COPYROUTE, PATH_IGNORE_PERSON_AT_DEST | PATH_THROUGH_PEOPLE))
			{
				INT16 sLoop;
				INT32 sLastSeenSpot = NOWHERE;

				sCheckGridNo = pSoldier->sGridNo;

				for (sLoop = pSoldier->pathing.usPathIndex; sLoop < pSoldier->pathing.usPathDataSize; sLoop++)
				{
					sCheckGridNo = NewGridNo(sCheckGridNo, DirectionInc((UINT8)(pSoldier->pathing.usPathingData[sLoop])));

					if (SoldierToVirtualSoldierLineOfSightTest(pSoldier, sCheckGridNo, pSoldier->pathing.bLevel, ANIM_STAND, TRUE, CALC_FROM_ALL_DIRS))
					{
						sLastSeenSpot = sCheckGridNo;
					}
				}

				// if found last seen spot
				if (!TileIsOutOfBounds(sLastSeenSpot))
				{
					IncrementWatchedLoc(pSoldier->ubID, sLastSeenSpot, pSoldier->pathing.bLevel);
				}
			}
			gubNPCAPBudget = 0;
		}

		// sevenfm: before anything else, if we are lying prone in a building and the enemy is close so he can be seen, try to crouch first
		if( gAnimControl[ pSoldier->usAnimState ].ubEndHeight == ANIM_PRONE &&
			IsValidStance( pSoldier, ANIM_CROUCH ) &&			
			pSoldier->bActionPoints == pSoldier->bInitialActionPoints &&			
			GetAPsToChangeStance( pSoldier, ANIM_CROUCH ) <= pSoldier->bActionPoints &&
			!TileIsOutOfBounds(sClosestOpponent) &&
			PythSpacesAway( pSoldier->sGridNo, sClosestOpponent ) <= DAY_VISION_RANGE/2 &&
			InARoom(pSoldier->sGridNo, NULL) )
		{
			// maybe raise weapon after crouching
			if( (PythSpacesAway( pSoldier->sGridNo, sClosestOpponent ) < (pSoldier->GetMaxDistanceVisible(sClosestOpponent) * 3) / 2 ||
				PreRandom( 4 ) == 0) )
			{
				// determine direction from this soldier to the closest opponent
				ubOpponentDir = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(sClosestOpponent),CenterY(sClosestOpponent));

				if( PickSoldierReadyAnimation( pSoldier, FALSE, FALSE ) != INVALID_ANIMATION &&
					AIGunInHandScoped(pSoldier) &&
					!WeaponReady(pSoldier) &&
					pSoldier->ubDirection == ubOpponentDir &&
					GetAPsToReadyWeapon( pSoldier, PickSoldierReadyAnimation( pSoldier, FALSE, FALSE ) ) + GetAPsToChangeStance( pSoldier, ANIM_CROUCH ) <= pSoldier->bActionPoints )
				{
					pSoldier->aiData.bNextAction = AI_ACTION_RAISE_GUN;
				}
			}

			pSoldier->aiData.usActionData = ANIM_CROUCH;
			return(AI_ACTION_CHANGE_STANCE);
		}		

		bActionReturned = DecideUseWirecutters(pSoldier);
		if (bActionReturned != -1)
			return bActionReturned;

		if (SoldierAI(pSoldier) &&
			pSoldier->CheckInitialAP() &&
			!pSoldier->aiData.bUnderFire &&
			pSoldier->pathing.bLevel == 0 &&
			pSoldier->aiData.bOrders == SEEKENEMY &&
			pSoldier->aiData.bAIMorale >= MORALE_CONFIDENT &&
			RangeChangeDesire(pSoldier) >= 4 &&
			!TileIsOutOfBounds(sClosestOpponent) &&
			PythSpacesAway(pSoldier->sGridNo, sClosestOpponent) > TACTICAL_RANGE / 4 &&
			(Chance(SoldierDifficultyLevel(pSoldier) * 10) + Chance(20 * CountThrowableGrenades(pSoldier, EXPLOSV_NORMAL, 10))) &&
			pSoldier->bActionPoints >= APBPConstants[AP_MINIMUM] &&
			FindFenceAroundSpot(pSoldier->sGridNo))
		{
			CheckTossOpponentFence(pSoldier, &BestThrow);

			if (BestThrow.ubPossible)
			{
				DebugAI(AI_MSG_INFO, pSoldier, String("prepare throw at spot %d level %d aimtime %d", BestThrow.sTarget, BestThrow.bTargetLevel, BestThrow.ubAimTime));

				// if necessary, swap the usItem from holster into the hand position
				if (BestThrow.bWeaponIn != HANDPOS)
				{
					DebugAI(AI_MSG_INFO, pSoldier, String("rearrange pocket"));
					RearrangePocket(pSoldier, HANDPOS, BestThrow.bWeaponIn, FOREVER);
				}

				// set grenade as delayed
				pSoldier->inv[HANDPOS][0]->data.sObjectFlag |= DELAYED_GRENADE_EXPLOSION;

				// stand up before throwing if needed
				if (gAnimControl[pSoldier->usAnimState].ubEndHeight < BestThrow.ubStance &&
					pSoldier->InternalIsValidStance(AIDirection(pSoldier->sGridNo, BestThrow.sTarget), BestThrow.ubStance))
				{
					pSoldier->aiData.usActionData = BestThrow.ubStance;
					pSoldier->aiData.bNextAction = AI_ACTION_TOSS_PROJECTILE;
					pSoldier->aiData.usNextActionData = BestThrow.sTarget;
					pSoldier->aiData.bNextTargetLevel = BestThrow.bTargetLevel;
					pSoldier->aiData.bAimTime = BestThrow.ubAimTime;
					return AI_ACTION_CHANGE_STANCE;
				}
				else
				{
					pSoldier->aiData.usActionData = BestThrow.sTarget;
					pSoldier->bTargetLevel = BestThrow.bTargetLevel;
					pSoldier->aiData.bAimTime = BestThrow.ubAimTime;
				}

				DebugAI(AI_MSG_INFO, pSoldier, String("throw grenade at spot %d level %d", BestThrow.sTarget, BestThrow.bTargetLevel));

				return(AI_ACTION_TOSS_PROJECTILE);
			}
		}

		// try to use grenade for special purpose
		bActionReturned = DecideUseGrenadeSpecial(pSoldier);
		if (bActionReturned != -1)
			return bActionReturned;

		// use smoke to cover movement when advancing to enemy
		bActionReturned = DecideSmokeCoverMovement(pSoldier, sClosestDisturbance);
		if (bActionReturned != -1)
			return bActionReturned;		

		// if we can move at least 1 square's worth
		// and have more APs than we want to reserve
		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,String("decideactionred: can we move? = %d, APs = %d",ubCanMove,pSoldier->bActionPoints));

		if (ubCanMove && pSoldier->bActionPoints > APBPConstants[MAX_AP_CARRIED])
		{
			DebugMsg(TOPIC_JA2, DBG_LEVEL_3, String("decideactionred: checking hide/seek/help/watch points... orders = %d, attitude = %d", pSoldier->aiData.bOrders, pSoldier->aiData.bAttitude));
			// calculate initial points for watch based on highest watch loc

			bWatchPts = GetHighestWatchedLocPoints(pSoldier->ubID);			
			if (bWatchPts <= 0)
			{
				// no watching
				bWatchPts = -99;
			}
			if (TileIsOutOfBounds(sClosestFriend))
			{
				bHelpPts = -99;
			}
			if (TileIsOutOfBounds(sClosestDisturbance))
			{
				bSeekPts = -99;
			}
			if (TileIsOutOfBounds(sClosestOpponent))
			{
				bHidePts = -99;
			}
			/*if (pSoldier->SkipCoverCheck())
			{
				if (fSafeSpot)
				{
					bHidePts = -99;
				}
				bSeekPts = -99;
				bHelpPts = -99;
			}*/
			if (pSoldier->usSkillCounter[SOLDIER_COUNTER_COVER])
			{
				bSeekPts = -99;
				bHelpPts = -99;
			}
			// can help only at the start of the turn
			if (!pSoldier->CheckInitialAP())
			{
				bHelpPts = -99;
			}
			// disable seek/help if soldier is lying prone in safe place and saw enemy recently and can attack him
			if (gAnimControl[pSoldier->usAnimState].ubEndHeight == ANIM_PRONE &&
				pSoldier->aiData.bAIMorale < MORALE_FEARLESS &&
				(AICheckSpecialRole(pSoldier) || pSoldier->aiData.bOrders != SEEKENEMY) &&
				SafeSpot(pSoldier, pSoldier->sGridNo) &&
				GuySawEnemy(pSoldier) &&
				!TileIsOutOfBounds(sClosestDisturbance) &&
				AIGunRange(pSoldier) >= PythSpacesAway(pSoldier->sGridNo, sClosestDisturbance) &&
				CountFriendsBlack(pSoldier, sClosestDisturbance) == 0)
			{
				bSeekPts = -99;
				bHelpPts = -99;
			}
			// no taking cover in deep water
			if (DeepWater(pSoldier->sGridNo, pSoldier->pathing.bLevel))
			{
				bHidePts = -99;
			}
			// sevenfm: stationary/snipers should not seek/help
			if (pSoldier->aiData.bOrders == SNIPER || pSoldier->aiData.bOrders == STATIONARY)
			{
				bSeekPts = -99;
				bHelpPts = -99;
			}
			// sevenfm: don't help if seen enemy recently or under fire
			if (GuySawEnemy(pSoldier) || pSoldier->aiData.bUnderFire)
			{
				bHelpPts = -99;
			}
			// special AI roles don't help
			if (AICheckSpecialRole(pSoldier))
			{
				bHelpPts = -99;
			}
			// sevenfm: disable seek/help when in building and seen enemy recently
			// check that closest reachable enemy is not in the same building
			if ((GuySawEnemy(pSoldier) && RangeChangeDesire(pSoldier) < 4 || CountSeenEnemiesLastTurn(pSoldier) > 0) &&
				InARoom(pSoldier->sGridNo, NULL) &&
				!TileIsOutOfBounds(sClosestDisturbance) &&
				!SameBuilding(pSoldier->sGridNo, sClosestDisturbance))
			{
				bSeekPts = -99;
				bHelpPts = -99;
			}
			if (!gfTurnBasedAI)
			{
				// don't search for cover
				bHidePts = -99;
			}			

			PrepareMainRedAIWeights(pSoldier, bSeekPts, bHelpPts, bHidePts, bWatchPts);


			// sevenfm: snipers and soldiers with scoped guns should decide watch more often
			if (AIGunScoped(pSoldier))
			{
				bWatchPts++;
			}
			if (AICheckIsSniper(pSoldier))
			{
				bWatchPts++;
			}

			// sevenfm: disable watching if soldier is under fire or in dangerous place
			// don't watch if some friends can see my closest opponent
			if (fDangerousSpot ||
				InLightAtNight(pSoldier->sGridNo, pSoldier->pathing.bLevel))
			{
				bWatchPts -= 10;
			}

			// sevenfm: don't watch when overcrowded and not in a building
			if (!InARoom(pSoldier->sGridNo, NULL))
			{
				bWatchPts -= CountNearbyFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 8);
			}

			// sevenfm: penalize watching if some friends see enemy at watched location
			bHighestWatchLoc = GetHighestVisibleWatchedLoc(pSoldier->ubID);
			if (bHighestWatchLoc != -1)
			{
				INT32 sWatchSpot = gsWatchedLoc[pSoldier->ubID][bHighestWatchLoc];
				INT8 bWatchLevel = gbWatchedLocLevel[pSoldier->ubID][bHighestWatchLoc];

				if (!TileIsOutOfBounds(sWatchSpot))
				{
					if (pSoldier->aiData.bOrders != STATIONARY && pSoldier->aiData.bOrders != SNIPER)
					{
						bWatchPts -= CountFriendsBlack(pSoldier, sWatchSpot);

						// penalize watching at night if soldier has no NVG and watched location is not in light
						if (NightLight() &&
							!InLightAtNight(sWatchSpot, bWatchLevel) &&
							!AICheckNVG(pSoldier) &&
							!InARoom(pSoldier->sGridNo, NULL))
						{
							bWatchPts -= 1;
						}

						if (AIGunRange(pSoldier) < PythSpacesAway(pSoldier->sGridNo, sWatchSpot))
						{
							bWatchPts -= 1;
						}
					}
				}

				// sevenfm: prefer hiding if soldier cannot interrupt
				if (pSoldier->CheckInitialAP() &&
					!pSoldier->CanInterrupt())
				{
					if (bHidePts > -90)
					{
						bWatchPts = min(bWatchPts, bHidePts - 1);
					}
				}
			}

			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,String("decideactionred: hide = %d, seek = %d, watch = %d, help = %d",bHidePts,bSeekPts,bWatchPts,bHelpPts));
			// while one of the three main RED REACTIONS remains viable
			while ((bSeekPts > -90) || (bHelpPts > -90) || (bHidePts > -90) )
			{
				DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: checking to seek");
				// if SEEKING is possible and at least as desirable as helping or hiding
				if ( ((bSeekPts > -90) && (bSeekPts >= bHelpPts) && (bSeekPts >= bHidePts) && (bSeekPts >= bWatchPts )) )
				{
					// sevenfm: disable help if seek decided to prevent AI from going back and forth
					bHelpPts = -99;

					// if there is an opponent reachable					
					// sevenfm: allow seeking in prone stance if we haven't seen enemy for several turns or someone already seen our closest enemy
					if (!TileIsOutOfBounds(sClosestDisturbance) &&
						( gAnimControl[ pSoldier->usAnimState ].ubHeight != ANIM_PRONE ||
						!GuySawEnemy(pSoldier) ||
						pSoldier->aiData.bUnderFire ||
						pSoldier->aiData.bOrders == SEEKENEMY ||
						CountFriendsBlack(pSoldier) > 0 ) )
					{
						DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: seek opponent");
						//////////////////////////////////////////////////////////////////////
						// SEEK CLOSEST DISTURBANCE: GO DIRECTLY TOWARDS CLOSEST KNOWN OPPONENT
						//////////////////////////////////////////////////////////////////////

						// try to move towards him
						/*pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier,sClosestDisturbance,GetAPsCrouch( pSoldier, TRUE),AI_ACTION_SEEK_OPPONENT,0);
						
						if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
						{
							// Check for a trap
							if ( !ArmySeesOpponents() )
							{
								if ( AICorpseWarningKnown(pSoldier, pSoldier->aiData.usActionData, pSoldier->pathing.bLevel) > 0 )
								{
									// abort! abort!
									pSoldier->aiData.usActionData = NOWHERE;
								}
							}
						}

						// if it's possible						
						if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
						{
							if (fSeekClimb)//&& pSoldier->aiData.usActionData == sClosestDisturbance)
							{
								// As mentioned in the next part, the sClosestDisturbance IS the climb point desired.  So the
								// check here should be "Am I already there?"  If so, THEN possibly climb.  This previous check
								// would have a soldier climbing any building, even if it was not the desired building.  So
								// WRONG WRONG WRONG
								if (pSoldier->sGridNo == sClosestDisturbance)
								{
									if (IsActionAffordable(pSoldier) && pSoldier->bActionPoints >= ( APBPConstants[AP_CLIMBROOF] + MinAPsToAttack( pSoldier, sClosestDisturbance, ADDTURNCOST,0)))
									{
										return( AI_ACTION_CLIMB_ROOF );
									}
								}
								else
								{
									// Do not overwrite the usActionData here.  If there's no nearby climb point, the action data
									// would become NOWHERE, and then the SEEK_ENEMY fallback would also fail.
									// In fact, sClosestDisturbance has ALREADY calculated the closest climb point when climbing is
									// necessary.  The returned grid # in sClosestDisturbance is that climb point.  So if climb is 
									// set, then use sClosestDisturbance as is.
									INT32 usClimbPoint = sClosestDisturbance;									
									if (!TileIsOutOfBounds(usClimbPoint))
									{
										pSoldier->aiData.usActionData = usClimbPoint;
										return( AI_ACTION_MOVE_TO_CLIMB  );
									}
								}
							}

							// sevenfm: possibly start RED flanking
							bActionReturned = DecideStartFlanking(pSoldier, sClosestDisturbance);
							if (bActionReturned != -1)
								return bActionReturned;

							// flanking not possible, use regular SEEK code
							// let's be a bit cautious about going right up to a location without enough APs to shoot
							if (PythSpacesAway(pSoldier->aiData.usActionData, sClosestDisturbance) < 5 ||
								SightCoverAtSpot(pSoldier, pSoldier->sGridNo, TRUE) &&
								!SightCoverAtSpot(pSoldier, pSoldier->aiData.usActionData, FALSE) &&
								PythSpacesAway(pSoldier->aiData.usActionData, sClosestDisturbance) < TACTICAL_RANGE / 2 &&
								CountFriendsBlack(pSoldier, sClosestDisturbance) == 0)
								//LocationToLocationLineOfSightTest( pSoldier->aiData.usActionData, pSoldier->pathing.bLevel, sClosestDisturbance, pSoldier->pathing.bLevel, TRUE, CALC_FROM_ALL_DIRS ) )
							{
								// reserve APs for a possible crouch plus a shot
								pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier, sClosestDisturbance, (INT8)(MinAPsToAttack(pSoldier, sClosestDisturbance, ADDTURNCOST, 0) + GetAPsCrouch(pSoldier, TRUE) + GetAPsToLook(pSoldier)), AI_ACTION_SEEK_OPPONENT, FLAG_CAUTIOUS);

								if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
								{
									pSoldier->aiData.fAIFlags |= AI_CAUTIOUS;
									pSoldier->aiData.bNextAction = AI_ACTION_END_TURN;
									return(AI_ACTION_SEEK_OPPONENT);
								}
							}

							return(AI_ACTION_SEEK_OPPONENT);
							break;
						}*/

						INT16 bReserveSeekAP = 0;
						UINT16 usMovementMode = DetermineMovementMode(pSoldier, AI_ACTION_SEEK_OPPONENT);
						INT32 sTestMoveSpot = InternalGoAsFarAsPossibleTowards(pSoldier, sClosestDisturbance, bReserveSeekAP, AI_ACTION_SEEK_OPPONENT, 0);

						if (!TileIsOutOfBounds(sClosestDisturbance) &&
							!TileIsOutOfBounds(sTestMoveSpot) &&
							PythSpacesAway(sTestMoveSpot, sClosestDisturbance) < (INT16)MAX_VISION_RANGE &&
							!SightCoverAtSpot(pSoldier, sTestMoveSpot, TRUE))
						{
							if (usMovementMode == RUNNING || usMovementMode == WALKING)
							{
								bReserveSeekAP = APBPConstants[AP_CHANGE_FACING] + GetAPsCrouch(pSoldier, TRUE);
								if (!SightCoverAtSpot(pSoldier, sTestMoveSpot, FALSE))
								{
									bReserveSeekAP += GetAPsProne(pSoldier, TRUE);
								}
							}
							else if (usMovementMode == SWATTING)
							{
								bReserveSeekAP = APBPConstants[AP_LOOK_CROUCHED] + GetAPsProne(pSoldier, TRUE);
							}
						}

						// sevenfm: sClosestDisturbance is the climb point
						// if we already there, then try to climb
						if (fSeekClimb && pSoldier->sGridNo == sClosestDisturbance)
						{
							// wait for next turn if turnbased (to climb with max APs)
							if (gfTurnBasedAI &&
								pSoldier->bActionPoints < pSoldier->bInitialActionPoints)
							{
								pSoldier->aiData.bNextAction = AI_ACTION_NONE;
								pSoldier->aiData.usNextActionData = 0;
								return(AI_ACTION_END_TURN);
							}

							// climb
							if (IsActionAffordable(pSoldier, AI_ACTION_CLIMB_ROOF))
							{
								pSoldier->aiData.bNextAction = AI_ACTION_NONE;
								pSoldier->aiData.usNextActionData = 0;
								return(AI_ACTION_CLIMB_ROOF);
							}

							// cannot climb at all
						}
						else
						{
							INT32 sAdvanceSpot = NOWHERE;

							// determine regular move spot
							INT32 sMoveSpot = InternalGoAsFarAsPossibleTowards(pSoldier, sClosestDisturbance, bReserveSeekAP, AI_ACTION_SEEK_OPPONENT, 0);

							// sevenfm: try to find advance spot with any cover
							if (gfTurnBasedAI &&
								!TileIsOutOfBounds(sMoveSpot) &&
								!TileIsOutOfBounds(sClosestOpponent) &&
								TileIsOutOfBounds(sAdvanceSpot) &&
								UseSightCoverAdvance(pSoldier) &&
								//PythSpacesAway(pSoldier->sGridNo, sClosestOpponent) < DAY_VISION_RANGE * 2 &&
								PythSpacesAway(sMoveSpot, sClosestOpponent) < (INT16)MAX_VISION_RANGE &&
								PythSpacesAway(pSoldier->sGridNo, sClosestOpponent) > DAY_VISION_RANGE / 2 &&
								(!ProneSightCoverAtSpot(pSoldier, sMoveSpot, FALSE) || InLightAtNight(sMoveSpot, pSoldier->pathing.bLevel) || AICorpseWarningKnown(pSoldier, sMoveSpot, pSoldier->pathing.bLevel)) &&
								!AnyCoverAtSpot(pSoldier, sMoveSpot))
							{
								sAdvanceSpot = FindAdvanceSpot(pSoldier, sClosestDisturbance, AI_ACTION_SEEK_OPPONENT, ADVANCE_SPOT_ANY_COVER, FALSE);

								if (!TileIsOutOfBounds(sAdvanceSpot))
								{
									// found any cover advance spot
								}
								else if (pSoldier->bActionPoints < pSoldier->bInitialActionPoints &&
									(pSoldier->bActionPoints < APBPConstants[AP_MINIMUM]) &&
									!pSoldier->aiData.bUnderFire &&
									AnyCoverAtSpot(pSoldier, pSoldier->sGridNo))
								{
									// if not already crouched, crouch down first
									if (gAnimControl[pSoldier->usAnimState].ubEndHeight > ANIM_CROUCH &&
										IsValidStance(pSoldier, ANIM_CROUCH) &&
										GetAPsToChangeStance(pSoldier, ANIM_CROUCH) <= pSoldier->bActionPoints &&
										!SightCoverAtSpot(pSoldier, pSoldier->sGridNo, TRUE))
									{
										pSoldier->aiData.usActionData = ANIM_CROUCH;
										pSoldier->aiData.bNextAction = AI_ACTION_END_TURN;
										pSoldier->aiData.usNextActionData = 0;
										return AI_ACTION_CHANGE_STANCE;
									}

									// if not prone, go prone first
									if (gAnimControl[pSoldier->usAnimState].ubEndHeight > ANIM_PRONE &&
										IsValidStance(pSoldier, ANIM_PRONE) &&
										GetAPsToChangeStance(pSoldier, ANIM_PRONE) <= pSoldier->bActionPoints &&
										!SightCoverAtSpot(pSoldier, pSoldier->sGridNo, FALSE))
									{
										pSoldier->aiData.usActionData = ANIM_PRONE;
										pSoldier->aiData.bNextAction = AI_ACTION_END_TURN;
										pSoldier->aiData.usNextActionData = 0;
										return AI_ACTION_CHANGE_STANCE;
									}

									return(AI_ACTION_END_TURN);
								}
							}

							// update path							
							if (!TileIsOutOfBounds(sAdvanceSpot))
							{
								pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier, sAdvanceSpot, 0, AI_ACTION_SEEK_OPPONENT, 0);
							}
							else
							{
								// failed to find any advance spot, use regular movement code								
								pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier, sClosestDisturbance, bReserveSeekAP, AI_ACTION_SEEK_OPPONENT, 0);
							}

							// if it's possible						
							if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
							{
								BOOLEAN fAbortSeek = FALSE;
								BOOLEAN fOvercrowded = FALSE;
								sDangerousSpot = NOWHERE;
								sLastSafeSpot = NOWHERE;

								fAbortSeek = AbortFinalSpot(pSoldier, pSoldier->aiData.usActionData, AI_ACTION_SEEK_OPPONENT, sClosestDisturbance, bClosestDisturbanceLevel, sDangerousSpot);

								// sevenfm: need to check path for fresh corpses
								if (!fAbortSeek)
								{
									fAbortSeek = AbortPath(pSoldier, AI_ACTION_SEEK_OPPONENT, sClosestDisturbance, bClosestDisturbanceLevel, sDangerousSpot, sLastSafeSpot);
									if (fAbortSeek && sLastSafeSpot != NOWHERE)
									{
										fAbortSeek = FALSE;
										pSoldier->aiData.usActionData = sLastSafeSpot;
									}
								}

								// If the final approach would substantially increase exposure,
								// require a teammate who can independently cover the same known
								// contact. Unsupported approaches can still convert into flanks
								// or other RED actions instead of becoming a suicidal straight rush.
								if (!fAbortSeek &&
									!fSeekClimb &&
									!AIAdvanceHasMutualSupport(pSoldier, pSoldier->aiData.usActionData, sClosestDisturbance, bClosestDisturbanceLevel))
								{
									fAbortSeek = TRUE;
								}

								// possibly start flanking
								bActionReturned = DecideStartFlanking(pSoldier, sClosestDisturbance, fAbortSeek);
								if (bActionReturned != -1)
									return bActionReturned;
									
								// if it's possible
								if (!fAbortSeek)
								{
									if (fSeekClimb)
									{
										return(AI_ACTION_SEEK_OPPONENT);
									}
									else
									{
										BOOLEAN fSkipCautiousMove = TRUE;
										BOOLEAN fSlowMovement = TRUE;

										// reserve APs for a possible crouch plus a shot
										INT8 bReserveAP = (INT8)(MinAPsToAttack(pSoldier, sClosestDisturbance, ADDTURNCOST, 0, TRUE) + GetAPsCrouch(pSoldier, TRUE));

										if (pSoldier->aiData.bUnderFire && !GuySawEnemy(pSoldier) ||
											CountFriendsBlack(pSoldier, sClosestDisturbance) > 0)
										{
											fSlowMovement = FALSE;
										}

										// determine cautious move spot
										INT32 sCautiousMoveSpot = InternalGoAsFarAsPossibleTowards(pSoldier, pSoldier->aiData.usActionData, bReserveAP, AI_ACTION_SEEK_OPPONENT, fSlowMovement ? FLAG_CAUTIOUS : 0);

										if (!TileIsOutOfBounds(sCautiousMoveSpot) &&
											(!TileIsOutOfBounds(sAdvanceSpot) ||
											InLightAtNight(sCautiousMoveSpot, pSoldier->pathing.bLevel) ||
											CountFriendsBlack(pSoldier, sClosestDisturbance) > 0 ||
											pSoldier->aiData.bUnderFire && !GuySawEnemy(pSoldier) ||
											FindBombNearby(pSoldier, sCautiousMoveSpot, BOMB_DETECTION_RANGE) ||
											AICorpseWarningKnown(pSoldier, sCautiousMoveSpot, pSoldier->pathing.bLevel) ||
											EnemyCanAttackSpot(pSoldier, sCautiousMoveSpot, pSoldier->pathing.bLevel) ||
											!SightCoverAtSpot(pSoldier, sCautiousMoveSpot, FALSE)))
										{
											fSkipCautiousMove = TRUE;
										}

										// let's be a bit cautious about going right up to a location without enough APs to shoot
										if (!fSkipCautiousMove &&
											!TileIsOutOfBounds(sCautiousMoveSpot) &&
											(PythSpacesAway(pSoldier->aiData.usActionData, sClosestDisturbance) < 5 ||
											SightCoverAtSpot(pSoldier, pSoldier->sGridNo, TRUE) &&
											!SightCoverAtSpot(pSoldier, pSoldier->aiData.usActionData, FALSE) &&
											PythSpacesAway(pSoldier->aiData.usActionData, sClosestDisturbance) < TACTICAL_RANGE / 2) &&
											CountFriendsBlack(pSoldier, sClosestDisturbance) == 0)
										{
											ubOpponentDir = AIDirection(pSoldier->sGridNo, sClosestDisturbance);

											// raise gun first
											if (PickSoldierReadyAnimation(pSoldier, FALSE, FALSE) != INVALID_ANIMATION &&
												pSoldier->ubDirection == ubOpponentDir &&
												(!gfTurnBasedAI || pSoldier->bActionPoints == pSoldier->bInitialActionPoints) &&
												gAnimControl[pSoldier->usAnimState].ubEndHeight == ANIM_STAND &&
												pSoldier->aiData.bLastAction != AI_ACTION_RAISE_GUN &&
												fSlowMovement &&
												!WeaponReady(pSoldier) &&
												(pSoldier->bBreath > OKBREATH * 2 || GetBPCostPer10APsForGunHolding(pSoldier, TRUE) < 30))
											{
												if (GetAPsToReadyWeapon(pSoldier, PickSoldierReadyAnimation(pSoldier, FALSE, FALSE)) <= pSoldier->bActionPoints)
												{
													pSoldier->aiData.bNextAction = AI_ACTION_SEEK_OPPONENT;
													pSoldier->aiData.usNextActionData = sCautiousMoveSpot;
													pSoldier->aiData.fAIFlags |= AI_CAUTIOUS;

													return AI_ACTION_RAISE_GUN;
												}
											}

											if (fSlowMovement)
											{
												pSoldier->aiData.fAIFlags |= AI_CAUTIOUS;
											}

											pSoldier->aiData.usActionData = sCautiousMoveSpot;
											pSoldier->aiData.bNextAction = AI_ACTION_END_TURN;
											pSoldier->aiData.usNextActionData = 0;

											return(AI_ACTION_SEEK_OPPONENT);
										}
										else
										{
											// not doing cautious move, seek opponent
											return(AI_ACTION_SEEK_OPPONENT);
										}
										// not climbing, trying cautious move, don't have enough APs to reserve for attack
									}
								}
								else
								{
									// seek aborted
								}

								// found enemy, movement is possible but seek aborted (InLightAtNight or found fresh corpse)
							}
							else
							{
								// found reachable disturbance but cannot find path
							}
						}
					}
					else
					{
						// no reachable disturbance or lying prone and cannot seek
					}

					// mark SEEKING as impossible for next time through while loop
					bSeekPts = -99;
					DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: couldn't seek");
				}

				DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: checking to watch");
				// if WATCHING is possible and at least as desirable as anything else
				if ((bWatchPts > -90) && (bWatchPts >= bSeekPts) && (bWatchPts >= bHelpPts) && (bWatchPts >= bHidePts ))
				{
					// take a look at our highest watch point... if it's still visible, turn to face it and then wait
					bHighestWatchLoc = GetHighestVisibleWatchedLoc( pSoldier->ubID );
					//sDistVisible =  DistanceVisible( pSoldier, DIRECTION_IRRELEVANT, DIRECTION_IRRELEVANT, gsWatchedLoc[ pSoldier->ubID ][ bHighestWatchLoc ] );

					if ( bHighestWatchLoc != -1 )
					{
						// see if we need turn to face that location
						ubOpponentDir = atan8( CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX( gsWatchedLoc[ pSoldier->ubID ][ bHighestWatchLoc ] ),CenterY( gsWatchedLoc[ pSoldier->ubID ][ bHighestWatchLoc ] ) );

						// if soldier is not already facing in that direction,
						// and the opponent is close enough that he could possibly be seen
						if( pSoldier->ubDirection != ubOpponentDir &&
							pSoldier->InternalIsValidStance( ubOpponentDir, gAnimControl[ pSoldier->usAnimState ].ubEndHeight ) &&
							pSoldier->bActionPoints >= GetAPsToLook(pSoldier)  )
						{
							// turn
							pSoldier->aiData.usActionData = ubOpponentDir;

							return(AI_ACTION_CHANGE_FACING);
						}

						// consider at least crouching
						if( gAnimControl[ pSoldier->usAnimState ].ubEndHeight == ANIM_STAND &&
							IsValidStance( pSoldier, ANIM_CROUCH ) &&
							pSoldier->bActionPoints >= GetAPsCrouch(pSoldier, TRUE) )
						{
							pSoldier->aiData.usActionData = ANIM_CROUCH;

							return(AI_ACTION_CHANGE_STANCE);
						}

						// possibly go prone, check that we'll have line of sight to standing enemy at watched location
						if (gAnimControl[ pSoldier->usAnimState ].ubEndHeight == ANIM_CROUCH &&
							IsValidStance( pSoldier, ANIM_PRONE ) &&
							pSoldier->bActionPoints >= GetAPsProne(pSoldier, TRUE) &&
							!InARoom(pSoldier->sGridNo, NULL) &&
							gfTurnBasedAI &&
							pSoldier->bActionPoints == pSoldier->bInitialActionPoints &&
							//LocationToLocationLineOfSightTestExt(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel, gsWatchedLoc[pSoldier->ubID][bHighestWatchLoc], gbWatchedLocLevel[pSoldier->ubID][bHighestWatchLoc], PRONE_LOS_POS, STANDING_LOS_POS))
							LocationToLocationLineOfSightTest(pSoldier->sGridNo, pSoldier->pathing.bLevel, gsWatchedLoc[pSoldier->ubID][bHighestWatchLoc], gbWatchedLocLevel[pSoldier->ubID][bHighestWatchLoc], TRUE, CALC_FROM_ALL_DIRS, PRONE_LOS_POS, STANDING_LOS_POS))
						{
							pSoldier->aiData.usActionData = ANIM_PRONE;

							return(AI_ACTION_CHANGE_STANCE);
						}

						// raise weapon if not raised
						if( PickSoldierReadyAnimation( pSoldier, FALSE, FALSE ) != INVALID_ANIMATION &&
							!WeaponReady(pSoldier) &&
							(pSoldier->bBreath > 15 || GetBPCostPer10APsForGunHolding( pSoldier, TRUE ) < 30) &&
							pSoldier->bActionPoints >= GetAPsToReadyWeapon( pSoldier, PickSoldierReadyAnimation( pSoldier, FALSE, FALSE ) ) )
						{
							return AI_ACTION_RAISE_GUN;
						}

						//return(AI_ACTION_END_TURN);
						return(AI_ACTION_NONE);
					}

					bWatchPts = -99;
					DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: couldn't watch");
				}

				DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: checking to help");
				// if HELPING is possible and at least as desirable as seeking or hiding
				if ((bHelpPts > -90) && (bHelpPts >= bSeekPts) && (bHelpPts >= bHidePts) && (bHelpPts >= bWatchPts ))
				{
					if (!TileIsOutOfBounds(sClosestFriend))
					{
						//////////////////////////////////////////////////////////////////////
						// GO DIRECTLY TOWARDS CLOSEST FRIEND UNDER FIRE OR WHO LAST RADIOED
						//////////////////////////////////////////////////////////////////////
						//pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier,sClosestFriend,GetAPsCrouch( pSoldier, TRUE), AI_ACTION_SEEK_OPPONENT,0);

						INT16 bReserveAP = GetAPsCrouch(pSoldier, TRUE);
						if (fHelpClimb || DetermineMovementMode(pSoldier, AI_ACTION_SEEK_FRIEND) == RUNNING)
						{
							bReserveAP = 0;
						}

						pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier, sClosestFriend, bReserveAP, AI_ACTION_SEEK_FRIEND, 0);

						sDangerousSpot = NOWHERE;
						sLastSafeSpot = NOWHERE;

						if (!TileIsOutOfBounds(pSoldier->aiData.usActionData) &&
							AbortFinalSpot(pSoldier, pSoldier->aiData.usActionData, AI_ACTION_SEEK_FRIEND, sClosestFriend, bClosestDisturbanceLevel, sDangerousSpot))
						{
							pSoldier->aiData.usActionData = NOWHERE;
						}

						if (!TileIsOutOfBounds(pSoldier->aiData.usActionData) &&
							AbortPath(pSoldier, AI_ACTION_SEEK_FRIEND, sClosestDisturbance, bClosestDisturbanceLevel, sDangerousSpot, sLastSafeSpot))
						{
							pSoldier->aiData.usActionData = NOWHERE;
						}
						
						// if new spot is safe to move
						if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
						{
							if (fHelpClimb)
							{
								// need to climb AND have enough APs to get there this turn
								if (pSoldier->sGridNo == sClosestFriend)
								{
									// wait for next turn to have full APs
									if (gfTurnBasedAI && pSoldier->bActionPoints < pSoldier->bInitialActionPoints)
									{
										return(AI_ACTION_END_TURN);
									}
									// climb
									if (IsActionAffordable(pSoldier, AI_ACTION_CLIMB_ROOF))
									{
										return(AI_ACTION_CLIMB_ROOF);
									}
									// don't have enough AP for climbing
								}
								else
								{
									// move to climbing spot
									return(AI_ACTION_SEEK_FRIEND);
								}
							}
							else
							{
								return(AI_ACTION_SEEK_FRIEND);
							}
						}
						else
						{
							// help aborted
						}
					}

					// mark helping as impossible for next time through while loop
					DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: couldn't help");
					bHelpPts = -99;
				}

				DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: checking to hide");
				// if HIDING is possible and at least as desirable as seeking or helping
				if ((bHidePts > -90) && (bHidePts >= bSeekPts) && (bHidePts >= bHelpPts) && (bHidePts >= bWatchPts ))
				{
					// disable help if hiding is preferred action
					bHelpPts = -99;

					// disable seek if soldier is in safe spot
					if ((fProneSightCover && AnyCoverAtSpot(pSoldier, pSoldier->sGridNo)) && 
						!pSoldier->aiData.bUnderFire && 
						!InLightAtNight(pSoldier->sGridNo, pSoldier->pathing.bLevel) && 
						AICorpseWarningKnown(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel) == 0)
					{
						bSeekPts = -99;
					}

					// if an opponent is known (not necessarily reachable or conscious)					
					if (!SkipCoverCheck && !TileIsOutOfBounds(sClosestOpponent))
					{
						//////////////////////////////////////////////////////////////////////
						// TAKE BEST NEARBY COVER FROM ALL KNOWN OPPONENTS
						//////////////////////////////////////////////////////////////////////
						pSoldier->aiData.usActionData = FindBestNearbyCover(pSoldier, pSoldier->aiData.bAIMorale, &iDummy);

						sDangerousSpot = NOWHERE;
						sLastSafeSpot = NOWHERE;

						if (!TileIsOutOfBounds(pSoldier->aiData.usActionData) &&
							AbortFinalSpot(pSoldier, pSoldier->aiData.usActionData, AI_ACTION_TAKE_COVER, sClosestDisturbance, bClosestDisturbanceLevel, sDangerousSpot))
						{
							// abort! abort!
							pSoldier->aiData.usActionData = NOWHERE;
						}

						// also check path for dangerous places
						if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
						{
							// try to move towards him
							INT32 sDestGridNo = InternalGoAsFarAsPossibleTowards(pSoldier, pSoldier->aiData.usActionData, 0, AI_ACTION_TAKE_COVER, 0);

							//if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
							if (!TileIsOutOfBounds(sDestGridNo))
							{
								sCheckGridNo = pSoldier->sGridNo;

								if (AbortPath(pSoldier, AI_ACTION_TAKE_COVER, sClosestDisturbance, bClosestDisturbanceLevel, sDangerousSpot, sLastSafeSpot))
								{
									pSoldier->aiData.usActionData = NOWHERE;
								}
							}
						}

						// sevenfm: moved APs to attack check and prone sight cover check to FindBestNearbyCover
						if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
						{
							return(AI_ACTION_TAKE_COVER);
						}
					}

					// mark HIDING as impossible for next time through while loop
					bHidePts = -99;
					DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: couldn't hide");
				}
			}
		}
		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: nothing to do!");
		////////////////////////////////////////////////////////////////////////////
		// NOTHING USEFUL POSSIBLE!  IF NPC IS CURRENTLY UNDER FIRE, TRY TO RUN AWAY
		////////////////////////////////////////////////////////////////////////////

		// if we're currently under fire (presumably, attacker is hidden)
		if (pSoldier->aiData.bUnderFire)
		{
			// sevenfm: only run away if morale is hopeless
			if (pSoldier->aiData.bAIMorale == MORALE_HOPELESS)
			{
				// look for best place to RUN AWAY to (farthest from the closest threat)
				pSoldier->aiData.usActionData = FindSpotMaxDistFromOpponents(pSoldier);
				
				if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
				{
					DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: run away!");
					return(AI_ACTION_RUN_AWAY);
				}
			}

			////////////////////////////////////////////////////////////////////////////
			// UNDER FIRE, DON'T WANNA/CAN'T RUN AWAY, SO CROUCH
			////////////////////////////////////////////////////////////////////////////

			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: crouch or go prone");
			// if not in water and not already crouched
			if (gAnimControl[pSoldier->usAnimState].ubHeight == ANIM_STAND && IsValidStance(pSoldier, ANIM_CROUCH))
			{
				if (!gfTurnBasedAI || GetAPsToChangeStance(pSoldier, ANIM_CROUCH) <= pSoldier->bActionPoints)
				{
					pSoldier->aiData.usActionData = ANIM_CROUCH;
					return(AI_ACTION_CHANGE_STANCE);
				}
			}
			else if (gAnimControl[pSoldier->usAnimState].ubHeight != ANIM_PRONE)
			{
				// maybe go prone
				if (PreRandom(2) == 0 && IsValidStance(pSoldier, ANIM_PRONE))
				{
					pSoldier->aiData.usActionData = ANIM_PRONE;
					return(AI_ACTION_CHANGE_STANCE);
				}
			}
		}
	}

	// sevenfm: civilians only run away
	if (fCivilian)
	{
		// look for best place to RUN AWAY to (farthest from the closest threat)
		pSoldier->aiData.usActionData = FindSpotMaxDistFromOpponents(pSoldier);

		if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
		{
			DebugMsg(TOPIC_JA2, DBG_LEVEL_3, "decideactionred: run away!");
			return(AI_ACTION_RUN_AWAY);
		}
	}


	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: look around towards opponent");
	////////////////////////////////////////////////////////////////////////////
	// LOOK AROUND TOWARD CLOSEST KNOWN OPPONENT, IF KNOWN
	////////////////////////////////////////////////////////////////////////////

	if (!gfTurnBasedAI || GetAPsToLook( pSoldier ) <= pSoldier->bActionPoints)
	{
		// determine the location of the known closest opponent
		// (don't care if he's conscious, don't care if he's reachable at all)
		sClosestOpponent = ClosestKnownOpponent(pSoldier, NULL, NULL);
		
		if (!TileIsOutOfBounds(sClosestOpponent))
		{
			// determine direction from this soldier to the closest opponent
			ubOpponentDir = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(sClosestOpponent),CenterY(sClosestOpponent));

			// if soldier is not already facing in that direction,
			// and the opponent is close enough that he could possibly be seen
			// note, have to change this to use the level returned from ClosestKnownOpponent
			sDistVisible = pSoldier->GetMaxDistanceVisible(sClosestOpponent, 0, CALC_FROM_ALL_DIRS );

			// sevenfm: better use increased range as we may have scope or enemy may be approaching
			sDistVisible = 3 * sDistVisible / 2;

			if( (pSoldier->ubDirection != ubOpponentDir) &&
				(PythSpacesAway(pSoldier->sGridNo,sClosestOpponent) <= sDistVisible) &&
				(gAnimControl[ pSoldier->usAnimState ].ubEndHeight > ANIM_PRONE || pSoldier->bActionPoints == pSoldier->bInitialActionPoints || TANK( pSoldier ) ) )
			{
				// set base chance according to orders
				if ((pSoldier->aiData.bOrders == STATIONARY) || (pSoldier->aiData.bOrders == ONGUARD))
					iChance = 50;
				else           // all other orders
					iChance = 25;

				if (pSoldier->aiData.bAttitude == DEFENSIVE)
					iChance += 25;

				if ( TANK( pSoldier ) )
				{
					iChance += 50;
				}

				if ((INT16)PreRandom(100) < iChance && pSoldier->InternalIsValidStance( ubOpponentDir, gAnimControl[ pSoldier->usAnimState ].ubEndHeight ) )
				{
					pSoldier->aiData.usActionData = ubOpponentDir;

					if ( pSoldier->aiData.bOrders == SNIPER && 
						!WeaponReady(pSoldier) && 
						PickSoldierReadyAnimation(pSoldier, FALSE, FALSE) != INVALID_ANIMATION &&
						(pSoldier->bBreath > 15 || GetBPCostPer10APsForGunHolding( pSoldier, TRUE ) < 30) )
					{
						if (!gfTurnBasedAI || GetAPsToReadyWeapon( pSoldier, READY_RIFLE_CROUCH ) <= pSoldier->bActionPoints)
						{
							pSoldier->aiData.bNextAction = AI_ACTION_RAISE_GUN;
						}
					}
					////////////////////////////////////////////////////////////////////////////
					// SANDRO - allow regular soldiers to raise scoped weapons to see rather away too
					else if ( (UsingNewCTHSystem() == false && IsScoped(&pSoldier->inv[HANDPOS])) || 
						 (UsingNewCTHSystem() == true && NCTHIsScoped(&pSoldier->inv[HANDPOS])) )
					{
						if (!(WeaponReady(pSoldier)) && 
							PickSoldierReadyAnimation(pSoldier, FALSE, FALSE) != INVALID_ANIMATION &&
							(pSoldier->bBreath > 15 || GetBPCostPer10APsForGunHolding( pSoldier, TRUE ) < 30))
						{
							if (!gfTurnBasedAI || GetAPsToReadyWeapon( pSoldier, READY_RIFLE_CROUCH ) <= pSoldier->bActionPoints)
							{
								if ( Random(100) < 35 ) 
								{
									pSoldier->aiData.bNextAction = AI_ACTION_RAISE_GUN;
								}
							}
						}
					}
					////////////////////////////////////////////////////////////////////////////

					return(AI_ACTION_CHANGE_FACING);
				}
			}
			////////////////////////////////////////////////////////////////////////////
			// SANDRO - allow regular soldiers to raise scoped weapons to see farther away too
			else if (pSoldier->ubDirection == ubOpponentDir && !WeaponReady(pSoldier) && PickSoldierReadyAnimation(pSoldier, FALSE, FALSE) != INVALID_ANIMATION)
			{
				if ((!gfTurnBasedAI || GetAPsToReadyWeapon( pSoldier, pSoldier->usAnimState ) <= pSoldier->bActionPoints) && 
					(pSoldier->bBreath > 15 || GetBPCostPer10APsForGunHolding( pSoldier, TRUE ) < 30))
				{
					if ( pSoldier->aiData.bOrders == SNIPER )
					{
						return AI_ACTION_RAISE_GUN;
					}
					else if ( (UsingNewCTHSystem() == false && IsScoped(&pSoldier->inv[HANDPOS])) || 
						 (UsingNewCTHSystem() == true && NCTHIsScoped(&pSoldier->inv[HANDPOS])) )
					{
						if ( Random(100) < 40 ) 
						{
							return AI_ACTION_RAISE_GUN;
						}
					}
					else
					{
						if ( Random(100) < 20 ) 
						{
							return AI_ACTION_RAISE_GUN;
						}
					}
				}
			}
			////////////////////////////////////////////////////////////////////////////
		}
	}

	if ( TANK( pSoldier ) )
	{
		// try turning in a random direction as we still can't see anyone.
		if (!gfTurnBasedAI || GetAPsToLook( pSoldier ) <= pSoldier->bActionPoints)
		{
			sClosestDisturbance = MostImportantNoiseHeard( pSoldier, NULL, NULL, NULL );
			
			if (!TileIsOutOfBounds(sClosestDisturbance))
			{
				ubOpponentDir = atan8( CenterX( pSoldier->sGridNo ), CenterY( pSoldier->sGridNo ), CenterX( sClosestDisturbance ), CenterY( sClosestDisturbance ) );
				if ( pSoldier->ubDirection == ubOpponentDir )
				{
					ubOpponentDir = (UINT8) PreRandom( NUM_WORLD_DIRECTIONS );
				}
			}
			else
			{
				ubOpponentDir = (UINT8) PreRandom( NUM_WORLD_DIRECTIONS );
			}

			if ( (pSoldier->ubDirection != ubOpponentDir) )
			{
				if ( (pSoldier->bActionPoints == pSoldier->bInitialActionPoints || (INT16)PreRandom(100) < 60) && pSoldier->InternalIsValidStance( ubOpponentDir, gAnimControl[ pSoldier->usAnimState ].ubEndHeight ) )
				{
					pSoldier->aiData.usActionData = ubOpponentDir;

					// limit turning a bit... if the last thing we did was also a turn, add a 60% chance of this being our last turn
					if ( pSoldier->aiData.bLastAction == AI_ACTION_CHANGE_FACING && PreRandom( 100 ) < 60 )
					{
						if ( gfTurnBasedAI )
						{
							pSoldier->aiData.bNextAction = AI_ACTION_END_TURN;
						}
						else
						{
							pSoldier->aiData.bNextAction = AI_ACTION_WAIT;
							pSoldier->aiData.usNextActionData = (UINT16) REALTIME_AI_DELAY;
						}
					}

					return(AI_ACTION_CHANGE_FACING);
				}
			}
		}

		// that's it for tanks
		return( AI_ACTION_NONE );
	}

	////////////////////////////////////////////////////////////////////////////
	// LEAVE THE SECTOR
	////////////////////////////////////////////////////////////////////////////

	// NOT IMPLEMENTED


	////////////////////////////////////////////////////////////////////////////
	// PICKUP A NEARBY ITEM THAT'S USEFUL
	////////////////////////////////////////////////////////////////////////////

	if ( ubCanMove &&
		 pSoldier->bTeam != MILITIA_TEAM &&
		 !pSoldier->aiData.bNeutral &&
		 (gfTurnBasedAI || pSoldier->bTeam == ENEMY_TEAM ) )
	{
		pSoldier->aiData.bAction = SearchForItems( pSoldier, SEARCH_GENERAL_ITEMS, pSoldier->inv[HANDPOS].usItem );

		if (pSoldier->aiData.bAction != AI_ACTION_NONE)
		{
			return( pSoldier->aiData.bAction );
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// SEEK CLOSEST FRIENDLY MEDIC
	////////////////////////////////////////////////////////////////////////////

	// NOT IMPLEMENTED


	////////////////////////////////////////////////////////////////////////////
	// GIVE FIRST AID TO A NEARBY INJURED/DYING FRIEND
	////////////////////////////////////////////////////////////////////////////
	// - must be BRAVEAID or CUNNINGAID (medic) ?

	// NOT IMPLEMENTED

	/* JULY 29, 1996 - Decided that this was a bad idea, after watching a civilian
	start a random patrol while 2 steps away from a hidden armed opponent...*/

	////////////////////////////////////////////////////////////////////////////
	// SWITCH TO GREEN: soldier does ordinary regular patrol, seeks friends
	////////////////////////////////////////////////////////////////////////////

	// if not in combat or under fire, and we COULD have moved, just chose not to	
	if ( (pSoldier->aiData.bAlertStatus != STATUS_BLACK) && !pSoldier->aiData.bUnderFire && ubCanMove && (!gfTurnBasedAI || pSoldier->bActionPoints >= pSoldier->bInitialActionPoints) && ( TileIsOutOfBounds(ClosestReachableDisturbance(pSoldier, &fSeekClimb))) )
	{
		// addition:  if soldier is bleeding then reduce bleeding and do nothing
		if ( pSoldier->bBleeding > MIN_BLEEDING_THRESHOLD )
		{
			// reduce bleeding by 1 point per AP (in RT, APs will get recalculated so it's okay)
			pSoldier->bBleeding = __max( 0, pSoldier->bBleeding - (pSoldier->bActionPoints/2) );
			pSoldier->bPoisonBleeding = max(0, pSoldier->bPoisonBleeding - __max( 0, pSoldier->bBleeding - (pSoldier->bActionPoints/2) ) );
			return( AI_ACTION_NONE ); // will end-turn/wait depending on whether we're in TB or realtime
		}

		// Skip RED until new situation/next turn, 30% extra chance to do GREEN actions
		pSoldier->aiData.bBypassToGreen = 30;
		return(DecideActionGreen(pSoldier));
	}


	////////////////////////////////////////////////////////////////////////////
	// CROUCH IF NOT CROUCHING ALREADY
	////////////////////////////////////////////////////////////////////////////

	// if not in water and not already crouched, try to crouch down first
	if (!fCivilian && !bInWater && (gAnimControl[ pSoldier->usAnimState ].ubHeight == ANIM_STAND) && IsValidStance( pSoldier, ANIM_CROUCH ) )
	{
		sClosestOpponent = ClosestKnownOpponent(pSoldier, NULL, NULL);

		//if ( ( !TileIsOutOfBounds(sClosestOpponent) && PythSpacesAway( pSoldier->sGridNo, sClosestOpponent ) < (MaxNormalDistanceVisible() * 3) / 2 ) || PreRandom( 4 ) == 0 )		
		if ( (!TileIsOutOfBounds(sClosestOpponent) && PythSpacesAway( pSoldier->sGridNo, sClosestOpponent ) < (pSoldier->GetMaxDistanceVisible(sClosestOpponent) * 3) / 2 ) || PreRandom( 4 ) == 0 )
		{
			if (!gfTurnBasedAI || GetAPsToChangeStance( pSoldier, ANIM_CROUCH ) <= pSoldier->bActionPoints)
			{
					////////////////////////////////////////////////////////////////////////////
					// SANDRO - allow regular soldiers to raise scoped weapons to see farther away too
					if (!gfTurnBasedAI || (GetAPsToReadyWeapon( pSoldier, READY_RIFLE_CROUCH ) + GetAPsToChangeStance( pSoldier, ANIM_CROUCH )) <= pSoldier->bActionPoints)
					{
						// determine direction from this soldier to the closest opponent
						ubOpponentDir = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(sClosestOpponent),CenterY(sClosestOpponent));

						if (!(WeaponReady(pSoldier)) && 
							PickSoldierReadyAnimation(pSoldier, FALSE, FALSE) != INVALID_ANIMATION &&
							pSoldier->ubDirection == ubOpponentDir )
						{
							if ( (UsingNewCTHSystem() == false && IsScoped(&pSoldier->inv[HANDPOS])) || 
									 (UsingNewCTHSystem() == true && NCTHIsScoped(&pSoldier->inv[HANDPOS])) )
							{
								if ( Random(100) < 40 ) 
								{
									pSoldier->aiData.bNextAction = AI_ACTION_RAISE_GUN;
								}
							}
						}
					}
					////////////////////////////////////////////////////////////////////////////


				pSoldier->aiData.usActionData = ANIM_CROUCH;
				return(AI_ACTION_CHANGE_STANCE);
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// IF UNDER FIRE, FACE THE MOST IMPORTANT NOISE WE KNOW AND GO PRONE
	////////////////////////////////////////////////////////////////////////////

	if ( !fCivilian && pSoldier->aiData.bUnderFire && pSoldier->bActionPoints >= (pSoldier->bInitialActionPoints - GetAPsToLook( pSoldier ) ) && IsValidStance( pSoldier, ANIM_PRONE ) )
	{
		sClosestDisturbance = MostImportantNoiseHeard( pSoldier, NULL, NULL, NULL );
		
		if (!TileIsOutOfBounds(sClosestDisturbance))
		{
			ubOpponentDir = atan8( CenterX( pSoldier->sGridNo ), CenterY( pSoldier->sGridNo ), CenterX( sClosestDisturbance ), CenterY( sClosestDisturbance ) );
			if ( pSoldier->ubDirection != ubOpponentDir )
			{
				if ( !gfTurnBasedAI || GetAPsToLook( pSoldier ) <= pSoldier->bActionPoints )
				{
					pSoldier->aiData.usActionData = ubOpponentDir;
					return( AI_ACTION_CHANGE_FACING );
				}
			}
			else if ( (!gfTurnBasedAI || GetAPsToChangeStance( pSoldier, ANIM_PRONE ) <= pSoldier->bActionPoints ) && pSoldier->InternalIsValidStance( ubOpponentDir, ANIM_PRONE ) )
			{
				// go prone, end turn
				pSoldier->aiData.bNextAction = AI_ACTION_END_TURN;
				pSoldier->aiData.usActionData = ANIM_PRONE;
				return( AI_ACTION_CHANGE_STANCE );
			}
		}
	}


	////////////////////////////////////////////////////////////////////////////
	// If sniper and nothing else to do then raise gun, and if that doesn't find somebody then goto yellow
	////////////////////////////////////////////////////////////////////////////
	if ( pSoldier->aiData.bOrders == SNIPER )
	{
		if ( pSoldier->sniper == 0 )
		{
			DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionRed: sniper raising gun..."));
			if ((!gfTurnBasedAI || GetAPsToReadyWeapon( pSoldier, READY_RIFLE_CROUCH ) <= pSoldier->bActionPoints) && 
				(pSoldier->bBreath > 15 || GetBPCostPer10APsForGunHolding( pSoldier, TRUE ) < 30))
			{
				if (!WeaponReady(pSoldier) && PickSoldierReadyAnimation(pSoldier, FALSE, FALSE) != INVALID_ANIMATION)
				{
					pSoldier->sniper = 1;
					return AI_ACTION_RAISE_GUN;
				}
			}
		}
		else
		{
			pSoldier->sniper = 0;
			return(DecideActionYellow(pSoldier));
		}
	}
	else if (!fCivilian)
	{
		////////////////////////////////////////////////////////////////////////////
		// SANDRO - raise weapon maybe
		if (!WeaponReady(pSoldier) && 
			PickSoldierReadyAnimation(pSoldier, FALSE, FALSE) != INVALID_ANIMATION &&
			(pSoldier->bBreath > 15 || GetBPCostPer10APsForGunHolding( pSoldier, TRUE ) < 30)) // if we are facing the direction of where the noise came from
		{
			if (!gfTurnBasedAI || GetAPsToReadyWeapon( pSoldier, pSoldier->usAnimState ) <= pSoldier->bActionPoints)
			{
				if ( (UsingNewCTHSystem() == false && IsScoped(&pSoldier->inv[HANDPOS])) || 
					 (UsingNewCTHSystem() == true && NCTHIsScoped(&pSoldier->inv[HANDPOS])) )
				{
					if ( Random(100) < 35 ) 
					{
						return( AI_ACTION_RAISE_GUN );
					}
				}
			}
		}
		////////////////////////////////////////////////////////////////////////////
	
	}

	////////////////////////////////////////////////////////////////////////////
	// DO NOTHING: Not enough points left to move, so save them for next turn
	////////////////////////////////////////////////////////////////////////////
	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionRed: do nothing at all..."));

	pSoldier->aiData.usActionData = NOWHERE;
	return(AI_ACTION_NONE);
}

// Flugente: dummies if we do not want to check for any conditions or taboos
BOOLEAN SoldierCondTrue(SOLDIERTYPE *pSoldier)			{ return TRUE; }
BOOLEAN SoldierCondFalse(SOLDIERTYPE *pSoldier)			{ return FALSE; }

INT8 DecideActionBlack(SOLDIERTYPE *pSoldier)
{
	INT32	iCoverPercentBetter, iOffense, iDefense, iChance;
	INT32	sBestCover = NOWHERE;
	INT32	sClosestOpponent = NOWHERE;
	INT32	sClosestSeenOpponent;
	INT32	sClosestDisturbance = NOWHERE;
	INT16	ubMinAPCost;
	UINT8	ubCanMove;
	INT8	bInWater, bInDeepWater, bInGas;
	INT8	bDirection;
	UINT8	ubBestAttackAction = AI_ACTION_NONE;
	INT8	bCanAttack;
	INT8	bActionReturned;
	INT8	bWeaponIn;
	BOOLEAN	fTryPunching = FALSE;

	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionBlack: soldier = %d, orders = %d, attitude = %d",pSoldier->ubID,pSoldier->aiData.bOrders,pSoldier->aiData.bAttitude));
	DebugAI(AI_MSG_START, pSoldier, String("[Black]"));
	LogDecideInfo(pSoldier);

	ATTACKTYPE BestShot, BestThrow, BestStab ,BestAttack;	//dnl ch69 150913
	BOOLEAN fCivilian = (PTR_CIVILIAN && (pSoldier->ubCivilianGroup == NON_CIV_GROUP || pSoldier->aiData.bNeutral || (pSoldier->ubBodyType >= FATCIV && pSoldier->ubBodyType <= CRIPPLECIV) ) );
	BOOLEAN fClimb;
	INT16	ubBurstAPs;
	UINT8	ubOpponentDir;
	INT32	sCheckGridNo;
	BOOLEAN fAllowCoverCheck = FALSE;	
	BOOLEAN fExtraClip = FALSE;			// sevenfm: extra clip to reload

	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack");

	// sevenfm: stop flanking when we see enemy
	if( pSoldier->numFlanks < MAX_FLANKS_RED  )
		pSoldier->numFlanks = 0;

	// sevenfm: disable stealth mode
	pSoldier->bStealthMode = FALSE;
	pSoldier->aiData.fAIFlags &= (~AI_CAUTIOUS);

	// sevenfm: initialize data
	pSoldier->bWeaponMode = WM_NORMAL;

	// if we have absolutely no action points, we can't do a thing under BLACK!
	if (!pSoldier->bActionPoints)
	{
		pSoldier->aiData.usActionData = NOWHERE;
		return(AI_ACTION_NONE);
	}

	// sevenfm: prepare data
	sClosestOpponent = ClosestKnownOpponent(pSoldier, NULL, NULL);
	sClosestSeenOpponent = ClosestSeenOpponent(pSoldier, NULL, NULL);

	if(SoldierAI(pSoldier))
	{
		sClosestDisturbance = ClosestReachableDisturbance(pSoldier, &fClimb);
	}	

	// can this guy move to any of the neighbouring squares ? (sets TRUE/FALSE)
	ubCanMove = (pSoldier->bActionPoints >= MinPtsToMove(pSoldier));

	if ( (pSoldier->bTeam == ENEMY_TEAM || pSoldier->ubProfile == WARDEN) && (gTacticalStatus.fPanicFlags & PANIC_TRIGGERS_HERE) && (gTacticalStatus.ubTheChosenOne == NOBODY) )
	{
		INT8 bPanicTrigger;

		bPanicTrigger = ClosestPanicTrigger( pSoldier );
		// if it's an alarm trigger and team is alerted, ignore it
		if ( !(gTacticalStatus.bPanicTriggerIsAlarm[ bPanicTrigger ] && gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition) && PythSpacesAway( pSoldier->sGridNo, gTacticalStatus.sPanicTriggerGridNo[ bPanicTrigger ] ) < 10)
		{
			PossiblyMakeThisEnemyChosenOne( pSoldier );
		}
	}

	// if this soldier is the "Chosen One" (enemies only)
	if (pSoldier->ubID == gTacticalStatus.ubTheChosenOne)
	{
		// do some special panic AI decision making
		bActionReturned = PanicAI(pSoldier,ubCanMove);

		// if we decided on an action while in there, we're done
		if (bActionReturned != -1)
			return(bActionReturned);
	}

	if ( pSoldier->ubProfile != NO_PROFILE )
	{
		// if they see enemies, the Queen will keep going to the staircase, but Joe will fight
		if ( (pSoldier->ubProfile == QUEEN) && ubCanMove )
		{
			if ( gWorldSectorX == 3 && gWorldSectorY == MAP_ROW_P && gbWorldSectorZ == 0 && !gfUseAlternateQueenPosition )
			{
				bActionReturned = HeadForTheStairCase( pSoldier );
				if ( bActionReturned != AI_ACTION_NONE )
				{
					return( bActionReturned );
				}
			}
		}
	}

	if ( pSoldier->flags.uiStatusFlags & SOLDIER_BOXER )
	{
		if ( gTacticalStatus.bBoxingState == PRE_BOXING )
		{
			return( DecideActionBoxerEnteringRing( pSoldier ) );
		}
		else if ( gTacticalStatus.bBoxingState == BOXING )
		{

			bInWater = FALSE;
			bInDeepWater = FALSE;
			bInGas = FALSE;

			// calculate our morale
			pSoldier->aiData.bAIMorale = CalcMorale(pSoldier);
			// and continue on...
		}
		else //????
		{
			return( AI_ACTION_NONE );
		}
	}
	else
	{
		// determine if we happen to be in water (in which case we're in BIG trouble!)
		bInWater = Water( pSoldier->sGridNo, pSoldier->pathing.bLevel );
		bInDeepWater = WaterTooDeepForAttacks( pSoldier->sGridNo, pSoldier->pathing.bLevel );

		// check if standing in tear gas without a gas mask on
		bInGas = InGas( pSoldier, pSoldier->sGridNo );

		// calculate our morale
		pSoldier->aiData.bAIMorale = CalcMorale(pSoldier);

		// Immediate environmental survival outranks higher-level tactical behaviour.
		// Do not throw rescue smoke, disperse, withdraw for formation reasons or start
		// a medic run while gassed, in deep water, collapsing from exhaustion, beside
		// a known bomb, or inside dangerous red smoke. The legacy emergency handling
		// immediately below already knows how to escape those hazards safely.
		BOOLEAN fImmediateEnvironmentalDanger =
			bInGas ||
			bInDeepWater ||
			pSoldier->bBreath < 5 ||
			FindBombNearby(pSoldier, pSoldier->sGridNo, BOMB_DETECTION_RANGE) ||
			RedSmokeDanger(pSoldier->sGridNo, pSoldier->pathing.bLevel);

		if (!fImmediateEnvironmentalDanger)
		{
			// Suppression panic is a real tactical state. Do not let a pinned soldier
			// start fallback, rescue, scavenging or attack setup until shock recovers.
			// Environmental escape remains higher priority and is handled outside this block.
			if (!fCivilian && SoldierAI(pSoldier) &&
				pSoldier->stats.bLife > OKLIFE &&
				!pSoldier->bCollapsed && !pSoldier->bBreathCollapsed &&
				(pSoldier->usAnimState == COWERING || pSoldier->usAnimState == COWERING_PRONE))
			{
				if (CoweringShockLevel(pSoldier) || !ubCanMove)
				{
					pSoldier->aiData.usActionData = NOWHERE;
					return AI_ACTION_NONE;
				}
				return AI_ACTION_STOP_COWERING;
			}

			// Emergency protection smoke is considered before movement/withdrawal so the
			// team can create concealment for a casualty or pinned soldier first.
			if (AICombatTeam(pSoldier))
			{
				INT8 bSmokeAction = DecideEmergencyProtectionSmoke(pSoldier);
				if (bSmokeAction != AI_ACTION_NONE)
					return bSmokeAction;
			}

			// Break local clusters under pressure before choosing ordinary attack/withdrawal.
			if (ubCanMove && AICombatTeam(pSoldier))
			{
				INT8 bDisperseAction = DecideCombatDispersion(pSoldier);
				if (bDisperseAction != AI_ACTION_NONE)
					return bDisperseAction;
			}
			// Maintain element cohesion before ordinary offensive movement. Soldiers
			// already under direct pressure are excluded inside the helper.
			if (AICombatTeam(pSoldier))
			{
				INT8 bCohesionAction = DecideFireteamCohesionAction(pSoldier, ubCanMove);
				if (bCohesionAction != AI_ACTION_NONE)
					return bCohesionAction;
			}

			// Persistent break-contact intent outranks ordinary fallback/attack setup.
			if (AICombatTeam(pSoldier))
			{
				INT8 bDisengageAction = DecideDisengagementAction(pSoldier, ubCanMove);
				if (bDisengageAction != AI_ACTION_NONE)
					return bDisengageAction;
			}

			// Hopeless local odds make survival/defence outrank another advance.
			if (AICombatTeam(pSoldier) && !AIDisengagementActive(pSoldier) && AIShouldAvoidAdvance(pSoldier))
			{
				INT8 bSurvivorAction = DecideHopelessSurvivorAction(pSoldier, ubCanMove);
				if (bSurvivorAction != AI_ACTION_NONE)
					return bSurvivorAction;
			}
			// Normal tactical fallback can concede ground before the situation becomes hopeless.
			if (AICombatTeam(pSoldier) && !AIDisengagementActive(pSoldier))
			{
				INT8 bFallbackAction = DecideTacticalFallback(pSoldier, ubCanMove);
				if (bFallbackAction != AI_ACTION_NONE)
					return bFallbackAction;
			}
			// Medics first attempt a safe casualty response; their rescue routine already
			// refuses suicidal runs, allowing self-aid to follow when necessary.
			if (AICombatTeam(pSoldier) && AICheckIsMedic(pSoldier))
			{
				INT8 bMedicCasualtyAction = DecideCombatCasualtyResponse(pSoldier, ubCanMove);
				if (bMedicCasualtyAction != AI_ACTION_NONE)
					return bMedicCasualtyAction;
			}

			if (AICombatTeam(pSoldier))
			{
				INT8 bSelfAidAction = DecideEmergencySelfAid(pSoldier);
				if (bSelfAidAction != AI_ACTION_NONE)
					return bSelfAidAction;
			}


			// Tactical self-preservation: individual danger can override aggression even
			// for a healthy soldier if he is badly suppressed, exposed and isolated.
			if (gfTurnBasedAI &&
				AICombatTeam(pSoldier) &&
				!AIDisengagementActive(pSoldier) &&
				ubCanMove &&
				pSoldier->aiData.bOrders != STATIONARY &&
				pSoldier->stats.bLife >= OKLIFE &&
				pSoldier->aiData.bAIMorale != MORALE_HOPELESS &&
				AIPersonalRisk(pSoldier) > AIPersonalRiskTolerance(pSoldier) &&
				(pSoldier->aiData.bUnderFire ||
				 !AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) ||
				 AICountNearbyOperationalFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 4) == 0) &&
				!TileIsOutOfBounds(sClosestOpponent))
			{
				pSoldier->aiData.usActionData = FindRetreatSpot(pSoldier);
				if (TileIsOutOfBounds(pSoldier->aiData.usActionData))
					pSoldier->aiData.usActionData = FindFlankingSpot(pSoldier, sClosestOpponent, AI_ACTION_WITHDRAW);
				if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
				{
					return(AI_ACTION_WITHDRAW);
				}
			}

			// Use the same casualty-response priority as RED so alert-state changes do
			// not silently reorder evacuation, medic rescue and adjacent buddy aid.
			if (AICombatTeam(pSoldier) && !AICheckIsMedic(pSoldier))
			{
				INT8 bCasualtyAction = DecideCombatCasualtyResponse(pSoldier, ubCanMove);
				if (bCasualtyAction != AI_ACTION_NONE)
					return bCasualtyAction;
			}

			////////////////////////////////////////////////////////////////////////////
		}

		// WHEN LEFT IN GAS, WEAR GAS MASK IF AVAILABLE AND NOT WORN
		////////////////////////////////////////////////////////////////////////////

		if ( !bInGas && (gWorldSectorX == TIXA_SECTOR_X && gWorldSectorY == TIXA_SECTOR_Y) )
		{
			// only chance if we happen to be caught with our gas mask off
			if ( PreRandom( 10 ) == 0 && WearGasMaskIfAvailable( pSoldier ) )
			{
				bInGas = FALSE;
			}
		}

	//Only put mask on in gas
	if(bInGas && WearGasMaskIfAvailable(pSoldier))//dnl ch40 200909
		bInGas = InGasOrSmoke(pSoldier, pSoldier->sGridNo);

		////////////////////////////////////////////////////////////////////////////
		// IF GASSED, OR REALLY TIRED (ON THE VERGE OF COLLAPSING), TRY TO RUN AWAY
		////////////////////////////////////////////////////////////////////////////

		// if we're desperately short on breath (it's OK if we're in water, though!)
		if (bInGas || (pSoldier->bBreath < 5))
		{
			// if soldier has enough APs left to move at least 1 square's worth
			if (ubCanMove)
			{
				// look for best place to RUN AWAY to (farthest from the closest threat)
				pSoldier->aiData.usActionData = FindSpotMaxDistFromOpponents(pSoldier);
				
				if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
				{
					return(AI_ACTION_RUN_AWAY);
				}
			}

			// REALLY tired and unable to escape: morale collapses. Difficulty should
			// improve decision quality, not magically refill breath or remove fear.
			pSoldier->aiData.bAIMorale = MORALE_HOPELESS;
		}

	}

	////////////////////////////////////////////////////////////////////////////
	// STUCK IN WATER OR GAS, NO COVER, GO TO NEAREST SPOT OF UNGASSED LAND
	////////////////////////////////////////////////////////////////////////////

	// Deep water is a mobility and combat liability. SEEKENEMY orders no longer
	// override self-preservation here; the common safe-land search below handles
	// all soldiers before they resume offensive movement.

	// if soldier in water/gas has enough APs left to move at least 1 square
	if ((bInDeepWater || bInGas || FindBombNearby(pSoldier, pSoldier->sGridNo, BOMB_DETECTION_RANGE) || RedSmokeDanger(pSoldier->sGridNo, pSoldier->pathing.bLevel)) && ubCanMove)
	{
		pSoldier->aiData.usActionData = FindNearestUngassedLand(pSoldier);
		
		if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
		{
			return(AI_ACTION_LEAVE_WATER_GAS);
		}

		// couldn't find ANY land within 25 tiles(!), this should never happen...

		// look for best place to RUN AWAY to (farthest from the closest threat)
		pSoldier->aiData.usActionData = FindSpotMaxDistFromOpponents(pSoldier);
		
		if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
		{
			return(AI_ACTION_RUN_AWAY);
		}

		// GIVE UP ON LIFE!  MERCS MUST HAVE JUST CORNERED A HELPLESS ENEMY IN A
		// GAS FILLED ROOM (OR IN WATER MORE THAN 25 TILES FROM NEAREST LAND...)
		pSoldier->aiData.bAIMorale = MORALE_HOPELESS;
	}

	// offer surrender?
#ifdef JA2UB
#else
	if( pSoldier->bTeam == ENEMY_TEAM && 
		pSoldier->bVisible == TRUE &&
		!( gTacticalStatus.fEnemyFlags & ENEMY_OFFERED_SURRENDER ) && 
		pSoldier->stats.bLife >= pSoldier->stats.bLifeMax / 2 &&
		!pSoldier->aiData.bUnderFire ) 
	{
		if ( gTacticalStatus.Team[ MILITIA_TEAM ].bMenInSector == 0 && gTacticalStatus.Team[ CREATURE_TEAM ].bMenInSector == 0 && NumPCsInSector() < 4 && gTacticalStatus.Team[ ENEMY_TEAM ].bMenInSector >= NumPCsInSector() * 3 )
		{
			//if( GetWorldDay() > STARTDAY_ALLOW_PLAYER_CAPTURE_FOR_RESCUE && !( gStrategicStatus.uiFlags & STRATEGIC_PLAYER_CAPTURED_FOR_RESCUE ) )
			{
				if ( gubQuest[ QUEST_HELD_IN_ALMA ] == QUESTNOTSTARTED || ( gubQuest[ QUEST_HELD_IN_ALMA ] == QUESTDONE && gubQuest[ QUEST_INTERROGATION ] == QUESTNOTSTARTED ) )
				{
					gTacticalStatus.fEnemyFlags |= ENEMY_OFFERED_SURRENDER;
					return( AI_ACTION_OFFER_SURRENDER );
				}
			}
		}
	}
#endif

	////////////////////////////////////////////////////////////////////////////
	// SOLDIER CAN ATTACK IF NOT IN WATER/GAS AND NOT DOING SOMETHING TOO FUNKY
	////////////////////////////////////////////////////////////////////////////


	// NPCs in water/tear gas without masks are not permitted to shoot/stab/throw
	if ((pSoldier->bActionPoints < 2) || bInDeepWater || bInGas || pSoldier->aiData.bRTPCombat == RTP_COMBAT_REFRAIN)
	{
		bCanAttack = FALSE;
	}
	else if (pSoldier->flags.uiStatusFlags & SOLDIER_BOXER)
	{
		bCanAttack = TRUE;
		fTryPunching = TRUE;
	}
	else
	{
		do
		{
			bCanAttack = CanNPCAttack(pSoldier);
			if (bCanAttack != TRUE)
			{
				if (fCivilian)
				{
					if ( ( bCanAttack == NOSHOOT_NOWEAPON) && !(pSoldier->flags.uiStatusFlags & SOLDIER_BOXER) && pSoldier->ubBodyType != COW && pSoldier->ubBodyType != CRIPPLECIV )
					{
						// cower in fear!!
						if ( pSoldier->flags.uiStatusFlags & SOLDIER_COWERING )
						{
							if ( pSoldier->aiData.bLastAction == AI_ACTION_COWER )
							{
								// do nothing
								pSoldier->aiData.usActionData = NOWHERE;
								return( AI_ACTION_NONE );
							}
							else
							{
								// set up next action to run away
								pSoldier->aiData.usNextActionData = FindSpotMaxDistFromOpponents( pSoldier );
								
								if (!TileIsOutOfBounds(pSoldier->aiData.usNextActionData))
								{
									pSoldier->aiData.bNextAction = AI_ACTION_RUN_AWAY;
									pSoldier->aiData.usActionData = ANIM_STAND;
									return( AI_ACTION_STOP_COWERING );
								}
								else
								{
									return( AI_ACTION_NONE );
								}
							}
						}
						else
						{
							// cower!!!
							pSoldier->aiData.usActionData = ANIM_CROUCH;
							return( AI_ACTION_COWER );
						}
					}
				}
				else if (bCanAttack == NOSHOOT_NOAMMO && ubCanMove && !pSoldier->aiData.bNeutral)
				{
					int handPOS;
					//CHRISL: We need to know which weapon has no ammo in case the soldier is holding a weapoin in SECONDHANDPOS
					if(pSoldier->inv[SECONDHANDPOS].exists() == true && pSoldier->inv[SECONDHANDPOS][0]->data.gun.ubGunShotsLeft == 0)
						handPOS = SECONDHANDPOS;
					else
						handPOS = HANDPOS;

					// Scavenge ammunition only when doing so is tactically reasonable. Under
					// direct pressure a human soldier first tries another carried weapon/sidearm
					// instead of running across the battlefield for loose ammunition.
					if (pSoldier->bTeam != MILITIA_TEAM &&
						(!AICombatTeam(pSoldier) ||
						 (!AIShouldAvoidAdvance(pSoldier) &&
						 !pSoldier->aiData.bUnderFire &&
						 (AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) ||
						  TileIsOutOfBounds(sClosestOpponent) ||
						  PythSpacesAway(pSoldier->sGridNo, sClosestOpponent) > TACTICAL_RANGE / 2))))
					{
						pSoldier->aiData.bAction = SearchForItems( pSoldier, SEARCH_AMMO, pSoldier->inv[handPOS].usItem );
					}
					else
					{
						pSoldier->aiData.bAction = AI_ACTION_NONE;
					}

					if (pSoldier->aiData.bAction == AI_ACTION_NONE)
					{
						// the current weapon appears is useless right now!
						// (since we got a return code of noammo, we know the hand usItem
						// is our gun)
						pSoldier->inv[handPOS].fFlags |= OBJECT_AI_UNUSABLE;
						// move the gun into another pocket...
						if (!AutoPlaceObject( pSoldier, &(pSoldier->inv[handPOS]), FALSE ) )
						{
							// If there's no room in his pockets for the useless gun, just throw it away
							return AI_ACTION_DROP_ITEM;
						}
					}
					else
					{
						return( pSoldier->aiData.bAction );
					}
				}
				// Human combatants do not become fearless merely because they lost their
				// weapon. Melee-capable soldiers can still fight through normal attack logic;
				// truly unarmed soldiers fall through to self-preservation behavior.
				else
				{
					bCanAttack = FALSE;
				}
			}
		} while( bCanAttack != TRUE && bCanAttack != FALSE );

		if (!bCanAttack)
		{
			pSoldier->aiData.bAIMorale = __min(pSoldier->aiData.bAIMorale, MORALE_WORRIED);

			if (!fCivilian &&
				(!AICombatTeam(pSoldier) || FindAIUsableObjClass(pSoldier, IC_WEAPON) != NO_SLOT))
			{
				// A carried melee weapon can still be a valid last resort. Truly unarmed
				// enemy/militia soldiers preserve themselves instead of suicidal punching.
				bCanAttack = TRUE;
				fTryPunching = TRUE;
			}
		}
	}

	// If we don't have a gun, look around for one only when scavenging is tactically
	// reasonable. Human combatants do not sprint into active fire just because a
	// weapon is lying on the ground somewhere nearby.
	if (FindAIUsableObjClass( pSoldier, IC_GUN ) == ITEM_NOT_FOUND &&
		ubCanMove &&
		pSoldier->bTeam != MILITIA_TEAM &&
		!pSoldier->aiData.bNeutral)
	{
		BOOLEAN fSafeToScavengeWeapon =
			!AICombatTeam(pSoldier) ||
			(!AIShouldAvoidAdvance(pSoldier) &&
			 !pSoldier->aiData.bUnderFire &&
			 (AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) ||
			  TileIsOutOfBounds(sClosestOpponent) ||
			  PythSpacesAway(pSoldier->sGridNo, sClosestOpponent) > TACTICAL_RANGE / 2));

		if (fSafeToScavengeWeapon)
		{
			pSoldier->aiData.bAction = SearchForItems( pSoldier, SEARCH_WEAPONS, pSoldier->inv[HANDPOS].usItem );
			if (pSoldier->aiData.bAction != AI_ACTION_NONE )
			{
				return( pSoldier->aiData.bAction );
			}
		}
	}

	DebugAI(AI_MSG_TOPIC, pSoldier, String("[use wirecutters to cut fence]"));	
	bActionReturned = DecideUseWirecutters(pSoldier);
	if (bActionReturned != -1)
		return bActionReturned;	

	DebugAI(AI_MSG_TOPIC, pSoldier, String("[Grenade for special purpose]"));
	// try to use regular grenade for special purpose
	// try to use grenade for special purpose
	bActionReturned = DecideUseGrenadeSpecial(pSoldier);
	if (bActionReturned != -1)
		return bActionReturned;

	bActionReturned = DecideSmokeCoverMovement(pSoldier, sClosestDisturbance);
	if (bActionReturned != -1)
		return bActionReturned;

	// Flugente: trait skills
	// if we are a radio operator
	if ( HAS_SKILL_TRAIT( pSoldier, RADIO_OPERATOR_NT ) > 0 && pSoldier->CanUseSkill(SKILLS_RADIO_ARTILLERY, TRUE) )
	{
		// check: would it be possible to call in artillery from neighbouring sectors?
		UINT32 tmp;
		INT32 skilltargetgridno = 0;
		// can we call in artillery?
		if ( pSoldier->CanAnyArtilleryStrikeBeOrdered(&tmp) )
		{
			// if frequencies are jammed...
			if ( SectorJammed() )
			{
				// if we are jamming, turn it off, otherwise, bad luck...
				if ( pSoldier->IsJamming() )
				{
					pSoldier->usAISkillUse = SKILLS_RADIO_TURNOFF;
					pSoldier->aiData.usActionData = skilltargetgridno;
					return(AI_ACTION_USE_SKILL);
				}
			}
			// frequencies are clear, order a strike
			else if ( AISelectKnownArtilleryTarget(pSoldier, &skilltargetgridno) )
			{
				pSoldier->usAISkillUse = SKILLS_RADIO_ARTILLERY;
				pSoldier->aiData.usActionData = skilltargetgridno;
				return(AI_ACTION_USE_SKILL);
			}
		}
		// no access to artillery... we can still call reinforcements if we haven't yet done so
		else if ( !gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition && MoreFriendsThanEnemiesinNearbysectors(pSoldier->bTeam, pSoldier->sSectorX, pSoldier->sSectorY, pSoldier->bSectorZ) )
		{
			// if frequencies are jammed...
			if ( SectorJammed() )
			{
				// if we are jamming, turn it off, otherwise, bad luck...
				if ( pSoldier->IsJamming() )
				{
					pSoldier->usAISkillUse = SKILLS_RADIO_TURNOFF;
					pSoldier->aiData.usActionData = skilltargetgridno;
					return(AI_ACTION_USE_SKILL);
				}
			}
			// frequencies are clear, lets call for help
			else if ( !(pSoldier->usSoldierFlagMask & SOLDIER_RAISED_REDALERT) )
			{
				// raise alarm!
				return( AI_ACTION_RED_ALERT );
			}
		}
		// if we can't call in artillery or reinforcements, then nobody else from our team can. So we better jam communications, so that the player cannot use these skills either
		else if ( !pSoldier->IsJamming() )
		{
			pSoldier->usAISkillUse = SKILLS_RADIO_JAM;
			pSoldier->aiData.usActionData = skilltargetgridno;
			return(AI_ACTION_USE_SKILL);
		}		
	}

	BestShot.ubPossible  = FALSE;	// by default, assume Shooting isn't possible
	BestThrow.ubPossible = FALSE;	// by default, assume Throwing isn't possible
	BestStab.ubPossible  = FALSE;	// by default, assume Stabbing isn't possible

	BestAttack.ubChanceToReallyHit = 0;

	// if we are able attack
	if (bCanAttack)
	{
		pSoldier->bAimShotLocation = AIM_SHOT_RANDOM;

		//////////////////////////////////////////////////////////////////////////
		// FIRE A GUN AT AN OPPONENT
		//////////////////////////////////////////////////////////////////////////
		DebugMsg(TOPIC_JA2, DBG_LEVEL_3, "FIRE A GUN AT AN OPPONENT");

		CheckIfShotPossible(pSoldier, &BestShot);

		BOOLEAN fBestShotTargetStateKnown =
			BestShot.ubPossible &&
			BestShot.ubOpponent != NOBODY &&
			MercPtrs[BestShot.ubOpponent] &&
			PersonalKnowledge(pSoldier, BestShot.ubOpponent) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, MercPtrs[BestShot.ubOpponent], CALC_FROM_ALL_DIRS) > 0;

		if (BestShot.ubFriendlyFireChance)	//dnl ch61 180813
		{
			// determine chance to shoot
			INT32 iChanceToShoot;

			iChanceToShoot = 100 - BestShot.ubFriendlyFireChance;
			iChanceToShoot = iChanceToShoot * iChanceToShoot / 100;

			if (Chance(100 - iChanceToShoot))
			{
				BestShot.ubPossible = FALSE;
			}
		}

		if (BestShot.ubPossible)
		{
			// if the selected opponent is not a threat (unconscious & !serviced)
			// (usually, this means all the guys we see are unconscious, but, on
			//  rare occasions, we may not be able to shoot a healthy guy, too)
			if (fBestShotTargetStateKnown &&
				(Menptr[BestShot.ubOpponent].stats.bLife < OKLIFE) &&
				!Menptr[BestShot.ubOpponent].bService &&
				(pSoldier->aiData.bAttitude != AGGRESSIVE || Chance((100 - BestShot.ubChanceToReallyHit) / 2)))
			{
				// get the location of the closest CONSCIOUS reachable opponent
				sClosestDisturbance = ClosestReachableDisturbance(pSoldier, &fClimb);

				// if we found one								
				if (!TileIsOutOfBounds(sClosestDisturbance))
				{
					// don't bother checking GRENADES/KNIVES, he can't have conscious targets
					// then make decision as if at alert status RED
					return DecideActionRed(pSoldier);
				}
				// else kill the guy, he could be the last opponent alive in this sector
			}

			// now we KNOW FOR SURE that we will do something (shoot, at least)
			NPCDoesAct(pSoldier);
			DebugMsg(TOPIC_JA2, DBG_LEVEL_3, "NPC decided to shoot (or something)");
		}

		//////////////////////////////////////////////////////////////////////////
		// THROW A TOSSABLE ITEM AT OPPONENT(S)
		// 	- HTH: THIS NOW INCLUDES FIRING THE GRENADE LAUNCHAR AND MORTAR!
		//////////////////////////////////////////////////////////////////////////

		// this looks for throwables, and sets BestThrow.ubPossible if it can be done
		CheckIfTossPossible(pSoldier,&BestThrow);

		if (BestThrow.ubPossible)
		{
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"good throw possible");
			if ( Item[pSoldier->inv[ BestThrow.bWeaponIn ].usItem].mortar )
			{
				ubOpponentDir = AIDirection(pSoldier->sGridNo, BestThrow.sTarget);

				// Get new gridno!
				sCheckGridNo = NewGridNo(pSoldier->sGridNo, (UINT16)DirectionInc(ubOpponentDir));

				if (!OKFallDirection(pSoldier, sCheckGridNo, pSoldier->pathing.bLevel, ubOpponentDir, pSoldier->usAnimState))
				{
					DebugAI(AI_MSG_INFO, pSoldier, String("no room to deploy mortar, check if we can move behind"));

					// try behind us, see if there's room to move back and we have enough AP to move and fire
					sCheckGridNo = NewGridNo(pSoldier->sGridNo, DirectionInc(gOppositeDirection[ubOpponentDir]));
					INT32 iPathCost = EstimatePlotPath(pSoldier, sCheckGridNo, FALSE, FALSE, FALSE, DetermineMovementMode(pSoldier, AI_ACTION_GET_CLOSER), FALSE, FALSE, 0);
					if (AIShouldAvoidAdvance(pSoldier) ||
						!OKFallDirection(pSoldier, sCheckGridNo, pSoldier->pathing.bLevel, gOppositeDirection[ubOpponentDir], pSoldier->usAnimState) ||
						iPathCost == 0 ||
						pSoldier->bActionPoints < BestThrow.ubAPCost + GetAPsToLook(pSoldier) + GetAPsCrouch(pSoldier, FALSE) + iPathCost)
					{
						BestThrow.ubPossible = FALSE;
					}
				}
			}

			if ( BestThrow.ubPossible )
			{
				// now we KNOW FOR SURE that we will do something (throw, at least)
				NPCDoesAct(pSoldier);
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// GO STAB AN OPPONENT WITH A KNIFE
		//////////////////////////////////////////////////////////////////////////

		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"GO STAB AN OPPONENT WITH A KNIFE");
		// if soldier has a knife in his hand
		bWeaponIn = FindAIUsableObjClass( pSoldier, (IC_BLADE | IC_THROWING_KNIFE) );

		// if the soldier does have a usable knife somewhere
		// 0verhaul:  And is not a tank!
		if (bWeaponIn != NO_SLOT && !TANK( pSoldier))
		{
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"try to stab");
			BestStab.bWeaponIn = bWeaponIn;
			// if it's in his holster, swap it into his hand temporarily
			if (bWeaponIn != HANDPOS)
			{
				DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionblack: about to rearrange pocket before stab check");
				RearrangePocket(pSoldier,HANDPOS,bWeaponIn,TEMPORARILY);
			}

			// get the minimum cost to attack with this knife
			ubMinAPCost = MinAPsToAttack(pSoldier,pSoldier->sLastTarget,DONTADDTURNCOST,0,0);

			// if we can afford the minimum AP cost to stab with/throw this knife weapon
			if (pSoldier->bActionPoints >= ubMinAPCost)
			{
				// NB throwing knife in hand now
				if ( Item[ pSoldier->inv[HANDPOS].usItem ].usItemClass & IC_THROWING_KNIFE )
				{
					// throwing knife code works like shooting

					// look around for a worthy target (which sets BestStab.ubPossible)
					CalcBestShot(pSoldier,&BestStab);

					if (BestStab.ubPossible)
					{
						// if the selected opponent is not a threat (unconscious & !serviced)
						// (usually, this means all the guys we see are unconscious, but, on
						//  rare occasions, we may not be able to shoot a healthy guy, too)
						if ((Menptr[BestStab.ubOpponent].stats.bLife < OKLIFE) &&
							!Menptr[BestStab.ubOpponent].bService)
						{
							// don't throw a knife at him.
							BestStab.ubPossible = FALSE;
						}

						// now we KNOW FOR SURE that we will do something (shoot, at least)
						NPCDoesAct(pSoldier);
						DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"NPC decided to shoot (2)");
					}
				}
				else
				{
					//sprintf((CHAR *)tempstr,"%s - ubMinAPCost = %d",pSoldier->name,ubMinAPCost);
					//PopMessage(tempstr);
					// then look around for a worthy target (which sets BestStab.ubPossible)
					CalcBestStab(pSoldier,&BestStab, TRUE);

					if (BestStab.ubPossible)
					{
						INT32 sAttackDist = PythSpacesAway(pSoldier->sGridNo, BestStab.sTarget);
						INT32 sMaxStabAttackDist = DAY_VISION_RANGE / 8;
						// sevenfm: limit HTH attacks when target is not very close
						if( sAttackDist > sMaxStabAttackDist )
						{
							BestStab.iAttackValue = BestStab.iAttackValue * sMaxStabAttackDist / sAttackDist;
						}

						if ( !HAS_SKILL_TRAIT( pSoldier, MELEE_NT ) )
						{
							BestStab.iAttackValue /= 4;
						}

						// now we KNOW FOR SURE that we will do something (stab, at least)
						NPCDoesAct(pSoldier);
						DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"NPC decided to stab");
					}
				}

			}

			// if it was in his holster, swap it back into his holster for now
			if (bWeaponIn != HANDPOS)
			{
				DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"about to rearrange pocket after stab check");
				RearrangePocket(pSoldier,HANDPOS,bWeaponIn,TEMPORARILY);
			}
		}
		/////////////////////////////////////////////////////////////////////////////////////////////////////
		// SANDRO - even if we don't have any blade, calculate how much damage we could do unarmed
		else if ( !TANK( pSoldier) )
		{
			bWeaponIn = FindAIUsableObjClass( pSoldier, IC_PUNCH );
			if (bWeaponIn == NO_SLOT) // if no punch-type weapon found, just calculate it with empty hands
			{
				bWeaponIn = FindEmptySlotWithin( pSoldier, HANDPOS, NUM_INV_SLOTS );
			}
			if (bWeaponIn != NO_SLOT)
			{
				DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"try to punch");
				BestStab.bWeaponIn = bWeaponIn;
				// if it's in his holster, swap it into his hand temporarily
				if (bWeaponIn != HANDPOS)
				{
					DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionblack: about to rearrange pocket before punch check");
					RearrangePocket(pSoldier,HANDPOS,bWeaponIn,TEMPORARILY);
				}

				// get the minimum cost to attack with punch
				ubMinAPCost = MinAPsToAttack(pSoldier,pSoldier->sLastTarget,DONTADDTURNCOST,0,0);
				// if we can afford the minimum AP cost to punch
				if (pSoldier->bActionPoints >= ubMinAPCost)
				{
					// then look around for a worthy target (which sets BestStab.ubPossible)
					CalcBestStab(pSoldier,&BestStab, FALSE);

					if (BestStab.ubPossible)
					{
						INT32 sAttackDist = PythSpacesAway(pSoldier->sGridNo, BestStab.sTarget);
						INT32 sMaxStabAttackDist = DAY_VISION_RANGE / 8;
						// sevenfm: limit HTH attacks when target is not very close
						if( sAttackDist > sMaxStabAttackDist )
						{
							//BestStab.iAttackValue /= 2;
							BestStab.iAttackValue = BestStab.iAttackValue * sMaxStabAttackDist / sAttackDist;
						}

						// if we are not specialized, reduce the attack attractiveness generally
						if( !HAS_SKILL_TRAIT( pSoldier, MARTIAL_ARTS_NT ) && 
							!HAS_SKILL_TRAIT( pSoldier, MARTIALARTS_OT ) && 
							!HAS_SKILL_TRAIT( pSoldier, HANDTOHAND_OT ) )
						{
							BestStab.iAttackValue /= 4; 
						}
						else
						{
							// if too far and not having APs for at least 2 hits
							if( (CalcTotalAPsToAttack( pSoldier,BestStab.sTarget,ADDTURNCOST, 0 ) + ApsToPunch( pSoldier )) > pSoldier->bActionPoints )
							{
								BestStab.iAttackValue /= 2;
							}
						}

						// now we KNOW FOR SURE that we will do something (stab, at least)
						NPCDoesAct(pSoldier);
						DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"NPC decided to punch");
					}

				}	
				// if it was in his holster, swap it back into his holster for now
				if (bWeaponIn != HANDPOS)
				{
					DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"about to rearrange pocket after punch check");
					RearrangePocket(pSoldier,HANDPOS,bWeaponIn,TEMPORARILY);
				}		
			}
		}
		/////////////////////////////////////////////////////////////////////////////////////////////////////

		// sevenfm: check that we have a clip to reload		
		if(BestShot.ubPossible && BestShot.bWeaponIn != NO_SLOT)
		{
			INT8 bAmmoSlot = FindAmmoToReload( pSoldier, BestShot.bWeaponIn, NO_SLOT );
			if (bAmmoSlot != NO_SLOT)
			{
				fExtraClip = TRUE;
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// CHOOSE THE BEST TYPE OF ATTACK OUT OF THOSE FOUND TO BE POSSIBLE
		//////////////////////////////////////////////////////////////////////////
		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"CHOOSE THE BEST TYPE OF ATTACK OUT OF THOSE FOUND TO BE POSSIBLE");

		// sevenfm: special code to attack zombies, disable shooting since we cannot kill lying zombie with bullets
		if (BestShot.ubPossible &&
			fBestShotTargetStateKnown &&
			BestShot.ubOpponent != NOBODY &&
			MercPtrs[BestShot.ubOpponent] &&
			MercPtrs[BestShot.ubOpponent]->IsZombie() &&
			gAnimControl[MercPtrs[BestShot.ubOpponent]->usAnimState].ubEndHeight == ANIM_PRONE &&
			gGameExternalOptions.fZombieOnlyHeadshotsWork &&
			BestStab.ubPossible &&
			Item[pSoldier->inv[BestStab.bWeaponIn].usItem].usItemClass & IC_BLADE)
		{
			BestShot.ubPossible = FALSE;
		}

		if (BestShot.ubPossible)
		{
			BestAttack.iAttackValue = BestShot.iAttackValue;
			ubBestAttackAction = AI_ACTION_FIRE_GUN;
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"best action = fire gun");

			// sevenfm: limit HTH/melee attacks if not specialist and the target is not very close
			if( BestStab.ubPossible && 
				!TileIsOutOfBounds(BestStab.sTarget) &&
				SpacesAway(pSoldier->sGridNo, BestStab.sTarget) > 1 &&
				!(Item[ pSoldier->inv[BestStab.bWeaponIn].usItem ].usItemClass & IC_THROWING_KNIFE) &&				
				!HAS_SKILL_TRAIT( pSoldier, MARTIAL_ARTS_NT ) &&
				!HAS_SKILL_TRAIT( pSoldier, MARTIALARTS_OT ) &&
				!HAS_SKILL_TRAIT( pSoldier, HANDTOHAND_OT ) &&
				!HAS_SKILL_TRAIT( pSoldier, MELEE_NT ) )
			{
				BestStab.ubPossible = FALSE;
			}
		}
		else
		{
			BestAttack.iAttackValue = 0;
		}

		if (BestStab.ubPossible &&
			(!AIShouldAvoidAdvance(pSoldier) ||
			 (Item[pSoldier->inv[BestStab.bWeaponIn].usItem].usItemClass & IC_THROWING_KNIFE) ||
			 SpacesAway(pSoldier->sGridNo, BestStab.sTarget) <= 1) &&
			((BestStab.iAttackValue > BestAttack.iAttackValue) || (ubBestAttackAction == AI_ACTION_NONE)))
		{
			BestAttack.iAttackValue = BestStab.iAttackValue;
			if ( Item[ pSoldier->inv[BestStab.bWeaponIn].usItem ].usItemClass & IC_THROWING_KNIFE )
			{
				ubBestAttackAction = AI_ACTION_THROW_KNIFE;
				DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"best action = throw knife");
			}
			else if ( Item[ pSoldier->inv[BestStab.bWeaponIn].usItem ].usItemClass & IC_BLADE ) // SANDRO - check specifically for blade attack
			{
				ubBestAttackAction = AI_ACTION_KNIFE_MOVE;
				DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"best action = move to stab");
			}
			////////////////////////////////////////////////////////////////////////////////////
			// SANDRO - added a chance to try to steal merc's gun from hands
			else
			{
				if (AIDetermineStealingWeaponAttempt( pSoldier, MercPtrs[BestStab.ubOpponent] ) == TRUE)
				{
					ubBestAttackAction = AI_ACTION_STEAL_MOVE;
					DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"best action = move to steal weapon");
				}
				else
				{
					ubBestAttackAction = AI_ACTION_KNIFE_MOVE;
					DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"best action = move to punch");
				}
			}
			////////////////////////////////////////////////////////////////////////////////////
		}

		if (BestThrow.ubPossible &&
			((BestThrow.iAttackValue > BestAttack.iAttackValue) || (ubBestAttackAction == AI_ACTION_NONE)) &&
			!(TANK(pSoldier) && ubBestAttackAction == AI_ACTION_FIRE_GUN && BestShot.ubChanceToReallyHit > 20 && Random(2)))
			//dnl ch64 290813 tank always had better chance to fire from cannon so this will increase probability to use machinegun too
		{
			ubBestAttackAction = AI_ACTION_TOSS_PROJECTILE;
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"best action = throw something");
		}

		if ( ( ubBestAttackAction == AI_ACTION_NONE ) && fTryPunching && !AIShouldAvoidAdvance(pSoldier) )
		{
			// nothing (else) to attack with so let's try hand-to-hand
			bWeaponIn = FindObj( pSoldier, NOTHING, HANDPOS, NUM_INV_SLOTS );

			if (bWeaponIn != NO_SLOT)
			{
				BestStab.bWeaponIn = bWeaponIn;
				// if it's in his holster, swap it into his hand temporarily
				if (bWeaponIn != HANDPOS)
				{
					DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionblack: swap knife into hand");
					RearrangePocket(pSoldier,HANDPOS,bWeaponIn,TEMPORARILY);
				}

				// get the minimum cost to attack by HTH
				ubMinAPCost = MinAPsToAttack(pSoldier,pSoldier->sLastTarget,DONTADDTURNCOST,0,0);

				// if we can afford the minimum AP cost to use HTH combat
				if (pSoldier->bActionPoints >= ubMinAPCost)
				{
					// then look around for a worthy target (which sets BestStab.ubPossible)
					CalcBestStab(pSoldier,&BestStab, FALSE);

					if (BestStab.ubPossible)
					{
						// now we KNOW FOR SURE that we will do something (stab, at least)
						NPCDoesAct(pSoldier);
						ubBestAttackAction = AI_ACTION_KNIFE_MOVE;
						DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"NPC decided to move to stab");
					}
				}

				// if it was in his holster, swap it back into his holster for now
				if (bWeaponIn != HANDPOS)
				{
					DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionblack: about to put knife away");
					RearrangePocket(pSoldier,HANDPOS,bWeaponIn,TEMPORARILY);
				}
			}
		}

		// copy the information on the best action selected into BestAttack struct
		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"copy the information on the best action selected into BestAttack struct");
		switch (ubBestAttackAction)
		{
		case AI_ACTION_FIRE_GUN:
			memcpy(&BestAttack,&BestShot,sizeof(BestAttack));
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack: best attack = firing a gun");
			break;

		case AI_ACTION_TOSS_PROJECTILE:
			memcpy(&BestAttack,&BestThrow,sizeof(BestAttack));
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack: best attack = tossing a grenade");
			break;

		case AI_ACTION_THROW_KNIFE:
		case AI_ACTION_KNIFE_MOVE:
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack: best attack = stab with a knife");
			memcpy(&BestAttack,&BestStab,sizeof(BestAttack));
			break;
		case AI_ACTION_STEAL_MOVE: // added by SANDRO
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack: best attack = try to steal weapon");
			memcpy(&BestAttack,&BestStab,sizeof(BestAttack));
			break;

		default:
			// set to empty
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack: best attack = no good attack");
			memset( &BestAttack, 0, sizeof( BestAttack ) );
			break;
		}
	}

	// NB a desire of 4 or more is only achievable by brave/aggressive guys with high morale
	UINT16 usRange = BestAttack.bWeaponIn==NO_SLOT ? 0 : GetModifiedGunRange(pSoldier->inv[BestAttack.bWeaponIn].usItem);//dnl ch69 150913

	BOOLEAN fBestAttackTargetStateKnown =
		ubBestAttackAction != AI_ACTION_NONE &&
		BestAttack.ubOpponent != NOBODY &&
		MercPtrs[BestAttack.ubOpponent] &&
		PersonalKnowledge(pSoldier, BestAttack.ubOpponent) == SEEN_CURRENTLY &&
		LOS_Raised(pSoldier, MercPtrs[BestAttack.ubOpponent], CALC_FROM_ALL_DIRS) > 0;

	// BLACK AI already receives target-value bonuses for covering a teammate's
	// bound/withdrawal inside CalcBestShot(). Preserve that task through the later
	// legacy attack-vs-cover arbitration as well, otherwise a support gunner can
	// select the right threat and then discard the shot for an unrelated cover tile.
	BOOLEAN fBestAttackCoveringTask =
		ubBestAttackAction == AI_ACTION_FIRE_GUN &&
		BestAttack.ubOpponent != NOBODY &&
		MercPtrs[BestAttack.ubOpponent] &&
		(AIFriendNeedsCoveringFire(pSoldier, BestAttack.ubOpponent) ||
		 AIFriendWithdrawingNeedsCover(pSoldier, BestAttack.ubOpponent) ||
		 AIFriendAdvancingNeedsCover(pSoldier, BestAttack.ubOpponent));

	// sevenfm: black climb
	// don't climb if there are enemies close (count all enemies, not only the current target)
	INT32 sClosestThreat = ClosestKnownOpponent(pSoldier, NULL, NULL);

	if( (pSoldier->bTeam == ENEMY_TEAM || pSoldier->bTeam== MILITIA_TEAM ) && 
		!(pSoldier->flags.uiStatusFlags & SOLDIER_BOXER ) &&
		!TANK( pSoldier ) &&
		!(pSoldier->flags.uiStatusFlags & SOLDIER_VEHICLE) &&
		!AM_A_ROBOT(pSoldier) &&
		!AIShouldAvoidAdvance(pSoldier) &&
		ubBestAttackAction == AI_ACTION_FIRE_GUN &&
		BestAttack.ubChanceToReallyHit < 30 &&
		BestAttack.ubChanceToReallyHit > 1 &&
		//BestAttack.bTargetLevel || MercPtrs[BestAttack.ubOpponent]->pathing.bLevel
		RangeChangeDesire(pSoldier) < 4 &&
		pSoldier->aiData.bOrders != STATIONARY &&
		pSoldier->aiData.bOrders != SNIPER &&
		!pSoldier->aiData.bUnderFire &&
		!TileIsOutOfBounds(sClosestThreat) &&
		// Climbing spends a turn exposed. Only reposition vertically when the closest
		// known threat is far enough away that the climb is not a point-blank escape.
		PythSpacesAway(pSoldier->sGridNo, sClosestThreat) > DAY_VISION_RANGE/2 &&
		PythSpacesAway(pSoldier->sGridNo, sClosestThreat) < 3*(usRange/CELL_X_SIZE)/2 &&
		usRange/CELL_X_SIZE > DAY_VISION_RANGE/2 &&
		pSoldier->pathing.bLevel == 0 &&
		PreRandom(100) > 100 / (1+BestAttack.bTargetLevel+CountNearbyFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE/4)) &&
		CountNearbyFriendsOnRoof(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE/8) == 0 &&
		//pSoldier->bActionPoints == pSoldier->bInitialActionPoints &&
		pSoldier->bActionPoints > APBPConstants[AP_MINIMUM] &&
		ubCanMove )
	{
		INT8 newdirection;
		if ( FindHeigherLevel( pSoldier, pSoldier->sGridNo, pSoldier->ubDirection, &newdirection ) )
		{
			pSoldier->aiData.bAction = AI_ACTION_CLIMB_ROOF;
			if ( IsActionAffordable(pSoldier) )
			{
				return( AI_ACTION_CLIMB_ROOF );
			}
		}
	}

	if (SoldierAI(pSoldier) &&
		gfTurnBasedAI &&
		!gfHiddenInterrupt &&
		!gTacticalStatus.fInterruptOccurred &&
		pSoldier->bActionPoints >= APBPConstants[AP_MINIMUM] &&
		pSoldier->bActionPoints == pSoldier->bInitialActionPoints &&
		!pSoldier->aiData.bUnderFire &&
		pSoldier->pathing.bLevel == 0 &&
		pSoldier->aiData.bOrders == SEEKENEMY &&
		!AIShouldAvoidAdvance(pSoldier) &&
		pSoldier->aiData.bAIMorale >= MORALE_CONFIDENT &&
		RangeChangeDesire(pSoldier) >= 4 &&
		!TileIsOutOfBounds(sClosestOpponent) &&
		PythSpacesAway(pSoldier->sGridNo, sClosestOpponent) > TACTICAL_RANGE / 4 &&
		(ubBestAttackAction == AI_ACTION_NONE || ubBestAttackAction == AI_ACTION_FIRE_GUN && Random(25) > (UINT8)BestAttack.ubChanceToReallyHit) &&
		(Chance(10 * SoldierDifficultyLevel(pSoldier) + 10 * (CountThrowableGrenades(pSoldier, EXPLOSV_NORMAL, 10)))) &&
		FindFenceAroundSpot(pSoldier->sGridNo))
	{
		CheckTossOpponentFence(pSoldier, &BestThrow);

		if (BestThrow.ubPossible)
		{
			DebugAI(AI_MSG_INFO, pSoldier, String("prepare throw at spot %d level %d aimtime %d", BestThrow.sTarget, BestThrow.bTargetLevel, BestThrow.ubAimTime));

			// if necessary, swap the usItem from holster into the hand position
			if (BestThrow.bWeaponIn != HANDPOS)
			{
				DebugAI(AI_MSG_INFO, pSoldier, String("rearrange pocket"));
				RearrangePocket(pSoldier, HANDPOS, BestThrow.bWeaponIn, FOREVER);
			}

			// set grenade as delayed
			pSoldier->inv[HANDPOS][0]->data.sObjectFlag |= DELAYED_GRENADE_EXPLOSION;

			// stand up before throwing if needed
			if (gAnimControl[pSoldier->usAnimState].ubEndHeight < BestThrow.ubStance &&
				pSoldier->InternalIsValidStance(AIDirection(pSoldier->sGridNo, BestThrow.sTarget), BestThrow.ubStance))
			{
				pSoldier->aiData.usActionData = BestThrow.ubStance;
				pSoldier->aiData.bNextAction = AI_ACTION_TOSS_PROJECTILE;
				pSoldier->aiData.usNextActionData = BestThrow.sTarget;
				pSoldier->aiData.bNextTargetLevel = BestThrow.bTargetLevel;
				pSoldier->aiData.bAimTime = BestThrow.ubAimTime;
				return AI_ACTION_CHANGE_STANCE;
			}
			else
			{
				pSoldier->aiData.usActionData = BestThrow.sTarget;
				pSoldier->bTargetLevel = BestThrow.bTargetLevel;
				pSoldier->aiData.bAimTime = BestThrow.ubAimTime;
			}
			DebugAI(AI_MSG_INFO, pSoldier, String("throw grenade at spot %d level %d", BestThrow.sTarget, BestThrow.bTargetLevel));

			return(AI_ACTION_TOSS_PROJECTILE);
		}
	}

	// Legacy Vengeance had a second "decide to advance" block here that never
	// actually moved. It randomly discarded a valid shot or enabled cover based
	// on target shock and nearby-friend count. Real movement is handled later by
	// the supported GET_CLOSER logic; attack-vs-cover is evaluated below.

	// sevenfm: allow to take cover
	if ((pSoldier->bActionPoints == pSoldier->bInitialActionPoints || pSoldier->usSoldierFlagMask2 & SOLDIER_ATTACKED_THIS_TURN) &&
		ubBestAttackAction == AI_ACTION_FIRE_GUN &&
		ubCanMove &&
		!gfHiddenInterrupt &&
		//!SkipCoverCheck &&
		!(pSoldier->flags.uiStatusFlags & SOLDIER_BOXER) &&
		pSoldier->aiData.bAIMorale < MORALE_FEARLESS &&
		RangeChangeDesire(pSoldier) < 4 &&
		!AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) &&
		BestAttack.ubChanceToReallyHit < 25 &&
		Chance(100 - BestAttack.ubChanceToReallyHit) &&
		!TileIsOutOfBounds(sClosestOpponent) &&
		PythSpacesAway(pSoldier->sGridNo, sClosestOpponent) > TACTICAL_RANGE / 4)
	{
		fAllowCoverCheck = TRUE;
	}

	/*if ( (pSoldier->bActionPoints == pSoldier->bInitialActionPoints) &&
		 (ubBestAttackAction == AI_ACTION_FIRE_GUN) && 
		 (pSoldier->aiData.bShock == 0) && 
		 (pSoldier->stats.bLife >= pSoldier->stats.bLifeMax / 2) && 
		 (BestAttack.ubChanceToReallyHit < 30) && 
		 (PythSpacesAway( pSoldier->sGridNo, BestAttack.sTarget ) > usRange / CELL_X_SIZE ) && 
		 (RangeChangeDesire( pSoldier ) >= 4) )
	{
		// okay, really got to wonder about this... could taking cover be an option?
		if (ubCanMove && pSoldier->aiData.bOrders != STATIONARY && !gfHiddenInterrupt &&
			!(pSoldier->flags.uiStatusFlags & SOLDIER_BOXER) )
		{
			// make militia a bit more cautious
			// 3 (UINT16) CONVERSIONS HERE TO AVOID ERRORS.  GOTTHARD 7/15/08
			if ( ( (pSoldier->bTeam == MILITIA_TEAM) && ((INT16)(PreRandom( 20 )) > BestAttack.ubChanceToReallyHit) )
				|| ( (pSoldier->bTeam != MILITIA_TEAM) && ((INT16)(PreRandom( 40 )) > BestAttack.ubChanceToReallyHit) ) )
			{
				//ScreenMsg( FONT_MCOLOR_LTYELLOW, MSG_TESTVERSION, L"AI %d allowing cover check, chance to hit is only %d, at range %d", BestAttack.ubChanceToReallyHit, PythSpacesAway( pSoldier->sGridNo, BestAttack.sTarget ) );
				// maybe taking cover would be better!
				fAllowCoverCheck = TRUE;
				if ( (INT16)(PreRandom( 10 )) > BestAttack.ubChanceToReallyHit )
				{
					DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack: can't hit so screw the attack");
					// screw the attack!
					ubBestAttackAction = AI_ACTION_NONE;
				}
			}
		}

	}*/


	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"LOOK FOR SOME KIND OF COVER BETTER THAN WHAT WE HAVE NOW");
	////////////////////////////////////////////////////////////////////////////
	// LOOK FOR SOME KIND OF COVER BETTER THAN WHAT WE HAVE NOW
	////////////////////////////////////////////////////////////////////////////

	// if soldier has enough APs left to move at least 1 square's worth,
	// and either he can't attack any more, or his attack did wound someone
	iCoverPercentBetter = 0;

	if ( (ubCanMove && 
		!SkipCoverCheck && 
		!gfHiddenInterrupt &&
		(ubBestAttackAction == AI_ACTION_NONE || pSoldier->aiData.bLastAttackHit) &&
		//(pSoldier->bTeam != gbPlayerNum || pSoldier->aiData.fAIFlags & AI_RTP_OPTION_CAN_SEEK_COVER) &&
		!(pSoldier->flags.uiStatusFlags & SOLDIER_BOXER) )
		|| fAllowCoverCheck )
	{
		sBestCover = FindBestNearbyCover(pSoldier,pSoldier->aiData.bAIMorale,&iCoverPercentBetter);
	}

	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack: DECIDE BETWEEN ATTACKING AND DEFENDING (TAKING COVER)");
	//////////////////////////////////////////////////////////////////////////
	// IF NECESSARY, DECIDE BETWEEN ATTACKING AND DEFENDING (TAKING COVER)
	//////////////////////////////////////////////////////////////////////////

	// if both are possible	
	if ((ubBestAttackAction != AI_ACTION_NONE) && ( !TileIsOutOfBounds(sBestCover)))
	{
		// gotta compare their merits and select the more desirable option
		iOffense = BestAttack.ubChanceToReallyHit;
		iDefense = iCoverPercentBetter;

		// based on how we feel about the situation, decide whether to attack first
		switch (pSoldier->aiData.bAIMorale)
		{
		case MORALE_FEARLESS:
			iOffense += iOffense / 2;	// increase 50%
			break;

		case MORALE_CONFIDENT:
			iOffense += iOffense / 4;	// increase 25%
			break;

		case MORALE_NORMAL:
			break;

		case MORALE_WORRIED:
			iDefense += iDefense / 4;	// increase 25%
			break;

		case MORALE_HOPELESS:
			iDefense += iDefense / 2;	// increase 50%
			break;
		}


		// smart guys more likely to try to stay alive, dolts more likely to shoot!
		if (pSoldier->stats.bWisdom >= 50) //Madd: reduced the wisdom required to want to live...
			iDefense += 10;
		else if (pSoldier->stats.bWisdom < 30)
			iDefense -= 10;

		// some orders are more offensive, others more defensive
		if (pSoldier->aiData.bOrders == SEEKENEMY)
			iOffense += 10;
		else if ((pSoldier->aiData.bOrders == STATIONARY) || (pSoldier->aiData.bOrders == ONGUARD) || pSoldier->aiData.bOrders == SNIPER )
			iDefense += 10;

		// A specific fire-and-manoeuvre task is more valuable than an ordinary shot,
		// especially for a soldier well suited to the support role. This remains a
		// preference rather than a forced attack: personal danger can still make the
		// defensive score win.
		if (fBestAttackCoveringTask)
		{
			INT32 iSupportScore = AISupportRoleScore(pSoldier, BestAttack.sTarget);
			INT32 iTaskBonus = 15;
			if (iSupportScore >= 80)
				iTaskBonus += 15;
			else if (iSupportScore >= 55)
				iTaskBonus += 8;

			INT32 iRisk = AIPersonalRisk(pSoldier);
			INT32 iTolerance = AIPersonalRiskTolerance(pSoldier);
			if (pSoldier->aiData.bUnderFire || iRisk > iTolerance)
				iTaskBonus /= 2;

			iOffense += __max(5, iTaskBonus);
		}

		switch (pSoldier->aiData.bAttitude)
		{
		case DEFENSIVE:		iDefense += 30; break;
		case BRAVESOLO:		iDefense -= 0; break;
		case BRAVEAID:			iDefense -= 0; break;
		case CUNNINGSOLO:	iDefense += 20; break;
		case CUNNINGAID:		iDefense += 20; break;
		case AGGRESSIVE:		iOffense += 10; break;
		case ATTACKSLAYONLY:iOffense += 30; break;
		}

		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack: if his defensive instincts win out, forget all about the attack");
		// if his defensive instincts win out, forget all about the attack
		if (iDefense > iOffense)
			ubBestAttackAction = AI_ACTION_NONE;
	}


	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,String("DecideActionBlack: is attack still desirable?  ubBestAttackAction = %d",ubBestAttackAction));

	// if attack is still desirable (meaning it's also preferred to taking cover)
	if (ubBestAttackAction != AI_ACTION_NONE)
	{
		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack: attack is still desirable (meaning it's also preferred to taking cover)");
		// if we wanted to be REALLY mean, we could look at chance to hit and decide whether
		// to shoot at the head...

		// default settings
		//POSSIBLE STRUCTURE CHANGE PROBLEM, NOT CURRENTLY CHANGED. GOTTHARD 7/14/08		
		pSoldier->aiData.bAimTime = BestAttack.ubAimTime;
		pSoldier->bScopeMode = BestAttack.bScopeMode;
		pSoldier->bDoBurst			= 0;

		// HEADROCK HAM 3.6: bAimTime represents how MANY aiming levels are used, not how much APs they cost necessarily.
		INT16 sActualAimAP = CalcAPCostForAiming( pSoldier, BestAttack.sTarget, (INT8)pSoldier->aiData.bAimTime );

		if (ubBestAttackAction == AI_ACTION_FIRE_GUN)
		{
			if(!TANK(pSoldier))
			{
				// sevenfm: dynamically decide shot location
				if (fBestAttackTargetStateKnown && BestAttack.ubOpponent != NOBODY)
				{
					UINT32	uiRoll;
					UINT8	ubChanceLegs = 0;
					UINT8	ubChanceHead = 0;
					INT16	sDistance = PythSpacesAway(pSoldier->sGridNo, MercPtrs[BestAttack.ubOpponent]->sGridNo);
					INT16	sMaxDistance = (DAY_VISION_RANGE / 2);
					UINT8	ubRealCTH = (UINT8)BestAttack.ubChanceToReallyHit;
					UINT32	uiCTGT_Legs, uiCTGT_Torso, uiCTGT_Head;					

					uiCTGT_Legs = SoldierToSoldierBodyPartChanceToGetThrough( pSoldier, MercPtrs[BestAttack.ubOpponent], AIM_SHOT_LEGS );
					uiCTGT_Torso = SoldierToSoldierBodyPartChanceToGetThrough( pSoldier, MercPtrs[BestAttack.ubOpponent], AIM_SHOT_TORSO );
					uiCTGT_Head = SoldierToSoldierBodyPartChanceToGetThrough( pSoldier, MercPtrs[BestAttack.ubOpponent], AIM_SHOT_HEAD );

					// check legs/head only if target is not prone
					if( gAnimControl[ MercPtrs[BestAttack.ubOpponent]->usAnimState ].ubEndHeight != ANIM_PRONE )
					{
						// check if leg shot is possible
						if( uiCTGT_Legs > 0 )
						{
							// basic chance to shoot legs
							ubChanceLegs = 15;

							// don't shoot legs at close distance
							if( sDistance < sMaxDistance )
							{
								ubChanceLegs = ubChanceLegs * sDistance / sMaxDistance;						
							}
							// when using NCTH system, shoot legs more often
							else if( UsingNewCTHSystem() )
							{
								ubChanceLegs += 15;
							}
							// don't waste bullets shooting at legs with low CTH
							ubChanceLegs = ubChanceLegs * ubRealCTH / 100;
						}

						// check if head shot is possible
						if( uiCTGT_Head > 0 )
						{
							// basic chance to shoot head
							ubChanceHead = 6;

							// snipers shoot at heads more often
							if( HAS_SKILL_TRAIT( pSoldier, SNIPER_NT ) )
							{
								ubChanceHead += 5 * NUM_SKILL_TRAITS( pSoldier, SNIPER_NT );
							}

							// if we just hit our enemy, aim at head if have good CTH
							if( pSoldier->sLastTarget == BestAttack.sTarget && pSoldier->aiData.bLastAttackHit )
							{
								ubChanceHead += ubRealCTH / 4;
							}

							if (MercPtrs[BestAttack.ubOpponent]->IsZombie())
							{
								if (gGameExternalOptions.fZombieOnlyHeadshotsWork)
									ubChanceHead += 50;
								else if (gGameExternalOptions.fZombieOnlyHeadShotsPermanentlyKill)
									ubChanceHead += 25;
							}

							// don't waste bullets shooting at heads with low CTH
							ubChanceHead = ubChanceHead * (100 + ubRealCTH) / 200;
						}
					}					

					// randomly decide hit location
					uiRoll = PreRandom( 100 );					

					if (uiRoll < ubChanceLegs)
					{
						pSoldier->bAimShotLocation = AIM_SHOT_LEGS;
					}
					else if (uiRoll > 100 - ubChanceHead)
					{
						pSoldier->bAimShotLocation = AIM_SHOT_HEAD;
					}
					else
					{
						pSoldier->bAimShotLocation = AIM_SHOT_TORSO;
					}

					// maybe switch to torso
					if( pSoldier->bAimShotLocation == AIM_SHOT_LEGS &&
						uiCTGT_Torso > uiCTGT_Legs * 2 )
					{
						pSoldier->bAimShotLocation = AIM_SHOT_TORSO;
					}
					// maybe switch to head (check that target is not prone)
					if( pSoldier->bAimShotLocation == AIM_SHOT_TORSO &&
						uiCTGT_Head > uiCTGT_Torso * 2 &&
						gAnimControl[ MercPtrs[BestAttack.ubOpponent]->usAnimState ].ubEndHeight != ANIM_PRONE )
					{
						pSoldier->bAimShotLocation = AIM_SHOT_HEAD;
					}
				}
			}

			//////////////////////////////////////////////////////////////////////////
			// IF ENOUGH APs TO BURST, RANDOM CHANCE OF DOING SO
			//////////////////////////////////////////////////////////////////////////

			if (IsGunBurstCapable( &pSoldier->inv[BestAttack.bWeaponIn], FALSE, pSoldier ) &&
				(!fBestAttackTargetStateKnown || !(Menptr[BestShot.ubOpponent].stats.bLife < OKLIFE)) && // only suppress visible downed targets
				pSoldier->inv[BestAttack.bWeaponIn][0]->data.gun.ubGunShotsLeft > 1 &&
				(pSoldier->bTeam != gbPlayerNum || pSoldier->aiData.bRTPCombat == RTP_COMBAT_AGGRESSIVE) )
			{
				DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack: ENOUGH APs TO BURST, RANDOM CHANCE OF DOING SO");

				ubBurstAPs = CalcAPsToBurst( pSoldier->CalcActionPoints(), &(pSoldier->inv[BestAttack.bWeaponIn]), pSoldier );

				// HEADROCK HAM 3.6: Use Actual Aiming Time.
				if (pSoldier->bActionPoints >= BestAttack.ubAPCost + sActualAimAP + ubBurstAPs )
				{
					// Base chance of bursting is 25% if best shot was +0 aim, down to 8% at +4
					if ( TANK( pSoldier ) )
					{
						iChance = 100;
					}
					else
					{
						iChance = (25 / max((BestAttack.ubAimTime + 1),1));
						switch (pSoldier->aiData.bAttitude)
						{
							case DEFENSIVE:		iChance += -5; break;
							case BRAVESOLO:		iChance +=  5; break;
							case BRAVEAID:		iChance +=  5; break;
							case CUNNINGSOLO:	iChance +=  0; break;
							case CUNNINGAID:	iChance +=  0; break;
							case AGGRESSIVE:	iChance += 10; break;
							case ATTACKSLAYONLY:iChance += 30; break;
						}

						// SANDRO: more likely to burst when firing from hip
						if ( BestAttack.bScopeMode == USE_ALT_WEAPON_HOLD && Item[pSoldier->inv[BestAttack.bWeaponIn].usItem].twohanded )
							iChance += 40;

						// CHRISL: Changed from a simple flag to two externalized values for more modder control over AI suppression
						if ( GetMagSize(&pSoldier->inv[BestAttack.bWeaponIn], 0) >= gGameExternalOptions.ubAISuppressionMinimumMagSize && 
							pSoldier->inv[BestAttack.bWeaponIn][0]->data.gun.ubGunShotsLeft >= gGameExternalOptions.ubAISuppressionMinimumAmmo )
							iChance += 20;

						// increase chance based on proximity and difficulty of enemy
						if ( PythSpacesAway( pSoldier->sGridNo, BestAttack.sTarget ) < 15 )
						{
							DebugMsg(TOPIC_JA2AI,DBG_LEVEL_3,String("DecideActionBlack: check chance to burst"));
							iChance += ( 15 - PythSpacesAway( pSoldier->sGridNo, BestAttack.sTarget ) ) * ( 1 + SoldierDifficultyLevel( pSoldier ) );
							if ( pSoldier->aiData.bAttitude == ATTACKSLAYONLY )
							{
								// increase it more!
								iChance += 5 * ( 15 - PythSpacesAway( pSoldier->sGridNo, BestAttack.sTarget ) );
							}
						}

						// Close range favours a controlled burst, but difficulty no longer
						// forces one with a +100 override. Better troops get a bounded bonus
						// and still respect aim quality, ammo state and the normal fire-mode roll.
						if (PythSpacesAway(pSoldier->sGridNo, BestAttack.sTarget) < 10 &&
							gGameOptions.ubDifficultyLevel > DIF_LEVEL_EASY)
						{
							iChance += 15 + 5 * SoldierDifficultyLevel(pSoldier);
							if (BestAttack.ubChanceToReallyHit >= 25)
								iChance += 15;
							if (pSoldier->inv[BestAttack.bWeaponIn][0]->data.gun.ubGunShotsLeft >=
								gGameExternalOptions.ubAISuppressionMinimumAmmo)
								iChance += 10;
						}
					}

					if ( (INT32) PreRandom( 100 ) < iChance)
					{
						BestAttack.ubAPCost += ubBurstAPs + sActualAimAP;//dnl ch58 130913
						// check for spread burst possibilities
						if (pSoldier->aiData.bAttitude != ATTACKSLAYONLY)
						{
							CalcSpreadBurst( pSoldier, BestAttack.sTarget, BestAttack.bTargetLevel );
						}
						//dnl ch58 130913 return aiming for burst
						pSoldier->bDoBurst = 1;
						pSoldier->bDoAutofire = 0;
					}
				}
			}

			if (IsGunAutofireCapable( &pSoldier->inv[BestAttack.bWeaponIn] ) &&
				(!fBestAttackTargetStateKnown || !(Menptr[BestShot.ubOpponent].stats.bLife < OKLIFE)) && // only suppress visible downed targets
				(( pSoldier->inv[BestAttack.bWeaponIn][0]->data.gun.ubGunShotsLeft > 1 &&
				!pSoldier->bDoBurst ) || Weapon[pSoldier->inv[BestAttack.bWeaponIn].usItem].NoSemiAuto) )
			{
				DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack: ENOUGH APs TO AUTOFIRE, RANDOM CHANCE OF DOING SO");
L_NEWAIM:
				FLOAT dTotalRecoil = 0.0f;
				pSoldier->bDoAutofire = 0;
				if(UsingNewCTHSystem() == true){
					do
					{
						pSoldier->bDoAutofire++;
						dTotalRecoil += AICalcRecoilForShot( pSoldier, &(pSoldier->inv[BestShot.bWeaponIn]), pSoldier->bDoAutofire );
						ubBurstAPs = CalcAPsToAutofire( pSoldier->CalcActionPoints(), &(pSoldier->inv[BestShot.bWeaponIn]), pSoldier->bDoAutofire, pSoldier );
					}
					while(	pSoldier->bActionPoints >= BestShot.ubAPCost + ubBurstAPs + sActualAimAP && pSoldier->inv[ BestAttack.bWeaponIn ][0]->data.gun.ubGunShotsLeft >= pSoldier->bDoAutofire && dTotalRecoil <= 10.0f );//dnl ch64 260813 pSoldier->ubAttackingHand is wrong because decision is to use BestAttack.bWeaponIn
				} else {
					do
					{
						pSoldier->bDoAutofire++;
						ubBurstAPs = CalcAPsToAutofire( pSoldier->CalcActionPoints(), &(pSoldier->inv[BestAttack.bWeaponIn]), pSoldier->bDoAutofire, pSoldier );
					}
					while(	pSoldier->bActionPoints >= BestAttack.ubAPCost + ubBurstAPs + sActualAimAP &&
						pSoldier->inv[ BestAttack.bWeaponIn ][0]->data.gun.ubGunShotsLeft >= pSoldier->bDoAutofire &&
						//dnl ch64 130913 pSoldier->ubAttackingHand is wrong because decision is to use BestAttack.bWeaponIn, also missing sActualAimTime
						// sevenfm limit max auto penalty if target has shock (suppressed)
						GetAutoPenalty(pSoldier, &pSoldier->inv[ BestAttack.bWeaponIn ], gAnimControl[ pSoldier->usAnimState ].ubEndHeight == ANIM_PRONE)*pSoldier->bDoAutofire <= __max(BestAttack.ubChanceToReallyHit, 40 + 80 / (2 + (fBestAttackTargetStateKnown ? MercPtrs[BestAttack.ubOpponent]->aiData.bShock : 0))) ); 
						//GetAutoPenalty(&pSoldier->inv[ BestAttack.bWeaponIn ], gAnimControl[ pSoldier->usAnimState ].ubEndHeight == ANIM_PRONE)*pSoldier->bDoAutofire <= 80);//dnl ch64 130913 pSoldier->ubAttackingHand is wrong because decision is to use BestAttack.bWeaponIn, also missing sActualAimTime
				}

				pSoldier->bDoAutofire--;
				// sevenfm: prefer bullets over aim (for suppression) only when soldier is under fire or has AGGRESSIVE attitude
				if(!UsingNewCTHSystem() &&
					pSoldier->bDoAutofire < 3 &&
					pSoldier->aiData.bAimTime > 0 &&
					fExtraClip &&
					pSoldier->aiData.bOrders != SNIPER &&
					BestAttack.ubChanceToReallyHit < 5 &&
					(!fBestAttackTargetStateKnown || !CoweringShockLevel(MercPtrs[BestAttack.ubOpponent])) &&
					(pSoldier->aiData.bUnderFire || pSoldier->aiData.bAttitude == AGGRESSIVE) &&
					pSoldier->inv[BestAttack.bWeaponIn][0]->data.gun.ubGunShotsLeft >= 3)//dnl ch69 130913 let try increase autofire rate for aim cost
				{
					pSoldier->aiData.bAimTime--;
					sActualAimAP = CalcAPCostForAiming(pSoldier, BestAttack.sTarget, (INT8)pSoldier->aiData.bAimTime);
					goto L_NEWAIM;
				}
				if (pSoldier->bDoAutofire > 0)
				{
					ubBurstAPs = CalcAPsToAutofire( pSoldier->CalcActionPoints(), &(pSoldier->inv[BestAttack.bWeaponIn]), pSoldier->bDoAutofire, pSoldier );

					if (pSoldier->bActionPoints >= BestAttack.ubAPCost + sActualAimAP + ubBurstAPs )
					{
						// Base chance of bursting is 25% if best shot was +0 aim, down to 8% at +4
						if ( TANK( pSoldier ) )
						{
							iChance = 100;
						}
						else
						{
							iChance = (100 / max((BestAttack.ubAimTime + 1),1));
							switch (pSoldier->aiData.bAttitude)
							{
							case DEFENSIVE:		iChance += -5; break;
							case BRAVESOLO:		iChance +=  5; break;
							case BRAVEAID:		iChance +=  5; break;
							case CUNNINGSOLO:	iChance +=  0; break;
							case CUNNINGAID:	iChance +=  0; break;
							case AGGRESSIVE:	iChance += 10; break;
							case ATTACKSLAYONLY:iChance += 30; break;
							}

							// SANDRO: more likely to burst when firing from hip
							if ( BestAttack.bScopeMode == USE_ALT_WEAPON_HOLD && Item[pSoldier->inv[BestAttack.bWeaponIn].usItem].twohanded )
								iChance += 40;

							// CHRISL: Changed from a simple flag to two externalized values for more modder control over AI suppression
							if ( GetMagSize(&pSoldier->inv[BestAttack.bWeaponIn], 0) >= gGameExternalOptions.ubAISuppressionMinimumMagSize &&
								pSoldier->inv[BestAttack.bWeaponIn][0]->data.gun.ubGunShotsLeft >= gGameExternalOptions.ubAISuppressionMinimumAmmo )
								iChance += 30;

							if ( bInGas )
								iChance += 50; //Madd: extra chance of going nuts and autofiring if stuck in gas

							// increase chance based on proximity and difficulty of enemy
							if ( PythSpacesAway( pSoldier->sGridNo, BestAttack.sTarget ) < 15 )
							{
								DebugMsg(TOPIC_JA2AI,DBG_LEVEL_3,String("DecideActionBlack: check chance to autofire"));
								iChance += ( 15 - PythSpacesAway( pSoldier->sGridNo, BestAttack.sTarget ) ) * ( 1 + SoldierDifficultyLevel( pSoldier ) );
								if ( pSoldier->aiData.bAttitude == ATTACKSLAYONLY )
								{
									// increase it more!
									iChance += 5 * ( 15 - PythSpacesAway( pSoldier->sGridNo, BestAttack.sTarget ) );
								}
							}
							// Close-range autofire is attractive, not mandatory. This keeps Elite
							// troops aggressive without making difficulty synonymous with ammo waste.
							if (PythSpacesAway(pSoldier->sGridNo, BestAttack.sTarget) < 10 &&
								gGameOptions.ubDifficultyLevel > DIF_LEVEL_EASY)
							{
								iChance += 15 + 5 * SoldierDifficultyLevel(pSoldier);
								if (BestAttack.ubChanceToReallyHit >= 25)
									iChance += 15;
								if (pSoldier->inv[BestAttack.bWeaponIn][0]->data.gun.ubGunShotsLeft >=
									gGameExternalOptions.ubAISuppressionMinimumAmmo)
									iChance += 10;
							}
						}

						if ((INT32) PreRandom( 100 ) < iChance || Weapon[pSoldier->inv[BestAttack.bWeaponIn].usItem].NoSemiAuto)
						{
							//dnl ch69 140913 return aiming for autofire with halfautofire fix
							pSoldier->bDoBurst = 1;
							INT16 ubHalfBurstAPs = 256;
							if(pSoldier->inv[BestAttack.bWeaponIn][0]->data.gun.ubGunShotsLeft < 4)
								iChance = 0;
							else
							{
								ubHalfBurstAPs = CalcAPsToAutofire(pSoldier->CalcActionPoints(), &pSoldier->inv[BestAttack.bWeaponIn], 4, pSoldier);
								if(Weapon[pSoldier->inv[BestAttack.bWeaponIn].usItem].NoSemiAuto)
									iChance = 35;
							}
							if((INT32)PreRandom(100) < iChance && pSoldier->bActionPoints > (2 * BestAttack.ubAPCost + ubHalfBurstAPs + sActualAimAP))
							{
								// Try short autofire to enhance chance of hitting
								pSoldier->bDoAutofire = 4;
								BestAttack.ubAPCost += ubHalfBurstAPs + sActualAimAP;
//SendFmtMsg("HALF-Auto=%d ubAPCost=%d iChance=%d ubBurstAPs=%d,%d", pSoldier->bDoAutofire, BestAttack.ubAPCost, iChance, ubHalfBurstAPs, sActualAimTime);
							}
							else
							{
								BestAttack.ubAPCost += ubBurstAPs + sActualAimAP;
//SendFmtMsg("FULL-Auto=%d ubAPCost=%d iChance=%d ubBurstAPs=%d,%d", pSoldier->bDoAutofire, BestAttack.ubAPCost, iChance, ubBurstAPs, sActualAimTime);
							}
						}
						else
						{
							pSoldier->bDoAutofire = 0;
							pSoldier->bDoBurst = 0;
						}
					}
				}
			}

			if (!pSoldier->bDoBurst)
			{
				pSoldier->aiData.bAimTime	= BestAttack.ubAimTime;
				pSoldier->bDoBurst			= 0;
				pSoldier->bDoAutofire		= 0;
			}

			// IF WAY OUT OF EFFECTIVE RANGE TRY TO ADVANCE RESERVING ENOUGH AP FOR A SHOT IF NOT ACTED YET
			if (
				!TileIsOutOfBounds(BestAttack.sTarget) &&				
				pSoldier->bActionPoints == pSoldier->bInitialActionPoints &&
				pSoldier->bActionPoints > BestAttack.ubAPCost &&
				AIEngagementRangeModifier(pSoldier, BestAttack.sTarget) > 0 &&
				!AIShouldAvoidAdvance(pSoldier) &&
				(AIAdvanceSupportModifier(pSoldier, BestAttack.sTarget) >= 0 ||
				 ((pSoldier->aiData.bAttitude == AGGRESSIVE || pSoldier->aiData.bAttitude == BRAVESOLO) &&
				  AILocalStress(pSoldier) < 25)) &&
				pSoldier->aiData.bShock < 2 * RangeChangeDesire(pSoldier) && 
				pSoldier->stats.bLife > pSoldier->stats.bLifeMax / 2 && 
				// sevenfm: increased to 10-40 depending on target shock
				(BestAttack.ubChanceToReallyHit < 10 +
				 (fBestAttackTargetStateKnown ? MercPtrs[BestAttack.ubOpponent]->aiData.bShock : 0)) &&
				// sevenfm: advance when too far or target is cowering or hit
				(	PythSpacesAway( pSoldier->sGridNo, BestAttack.sTarget ) > usRange / (CELL_X_SIZE) ||
					(fBestAttackTargetStateKnown && CoweringShockLevel(MercPtrs[BestAttack.ubOpponent])) ||
					pSoldier->aiData.bLastAttackHit ) &&
				pSoldier->aiData.bOrders > ONGUARD &&
				pSoldier->aiData.bOrders != SNIPER &&
				// elites should not advance
				RangeChangeDesire(pSoldier) >= 3 + SoldierDifficultyLevel( pSoldier ) / 2 &&
				pSoldier->aiData.bOppCnt <= 5 - SoldierDifficultyLevel( pSoldier ) &&
				// only when standing
				gAnimControl[ pSoldier->usAnimState ].ubEndHeight > ANIM_CROUCH &&
				// only short range weapons
				usRange / (CELL_X_SIZE) < DAY_VISION_RANGE &&
				!gfHiddenInterrupt
				)
			{
				INT8 bReserveAP = (INT8) (MinAPsToAttack( pSoldier, BestAttack.sTarget, ADDTURNCOST, BestAttack.ubAimTime, TRUE ) + GetAPsCrouch( pSoldier, TRUE) + GetAPsToLook(pSoldier));

				pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier, BestAttack.sTarget, bReserveAP, AI_ACTION_GET_CLOSER, 0 );

				BOOLEAN fAbortAdvance = FALSE;
				INT32 sAdvanceDanger = NOWHERE;
				INT32 sLastSafeAdvance = NOWHERE;
				if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
				{
					fAbortAdvance = AbortFinalSpot(pSoldier, pSoldier->aiData.usActionData,
						AI_ACTION_GET_CLOSER, BestAttack.sTarget, BestAttack.bTargetLevel, sAdvanceDanger);
					if (!fAbortAdvance)
					{
						fAbortAdvance = AbortPath(pSoldier, AI_ACTION_GET_CLOSER, BestAttack.sTarget,
							BestAttack.bTargetLevel, sAdvanceDanger, sLastSafeAdvance);
					}
				}

				if (!fAbortAdvance &&
					!TileIsOutOfBounds(pSoldier->aiData.usActionData) &&
					PythSpacesAway(pSoldier->aiData.usActionData, BestAttack.sTarget) < PythSpacesAway(pSoldier->sGridNo, BestAttack.sTarget) &&
					LocationToLocationLineOfSightTest( pSoldier->aiData.usActionData, pSoldier->pathing.bLevel, BestAttack.sTarget, BestAttack.bTargetLevel, TRUE, CALC_FROM_ALL_DIRS ) &&
					AIAdvanceHasMutualSupport(pSoldier, pSoldier->aiData.usActionData, BestAttack.sTarget, BestAttack.bTargetLevel))
				{
					return( AI_ACTION_GET_CLOSER );
				}
			}

			// IF WAY OUT OF EFFECTIVE RANGE TRY TO ADVANCE RESERVING ENOUGH AP FOR A SHOT IF NOT ACTED YET
			/*if ((pSoldier->bActionPoints > BestAttack.ubAPCost) &&
				(pSoldier->aiData.bShock == 0) && 
				(pSoldier->stats.bLife >= pSoldier->stats.bLifeMax / 2) && 
				(BestAttack.ubChanceToReallyHit < 8) &&
				(PythSpacesAway( pSoldier->sGridNo, BestAttack.sTarget ) > usRange / CELL_X_SIZE ) && 
				(RangeChangeDesire( pSoldier ) >= 3) ) // Cunning and above
			{
				sClosestOpponent = Menptr[BestShot.ubOpponent].sGridNo;
				if (!TileIsOutOfBounds(sClosestOpponent))
				{
					// temporarily make merc get closer reserving enough for expected cost of shot
					USHORT tgrd = pSoldier->aiData.sPatrolGrid[0];
					INT8 oldOrders = pSoldier->aiData.bOrders;
					pSoldier->aiData.sPatrolGrid[0] = pSoldier->sGridNo;
					pSoldier->aiData.bOrders = CLOSEPATROL;
					pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards( pSoldier, sClosestOpponent, BestAttack.ubAPCost, AI_ACTION_GET_CLOSER, 0 );
					pSoldier->aiData.sPatrolGrid[0] = tgrd;
					pSoldier->aiData.bOrders = oldOrders;

					if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
					{
						pSoldier->aiData.usActionData = pSoldier->sGridNo ;
						pSoldier->pathing.sFinalDestination = pSoldier->aiData.usActionData;

						pSoldier->aiData.bNextAction = AI_ACTION_FIRE_GUN;
						pSoldier->aiData.usNextActionData = BestAttack.sTarget;
						pSoldier->aiData.bNextTargetLevel = BestAttack.bTargetLevel;
						return( AI_ACTION_GET_CLOSER );
					}
				}
			}*/

			//////////////////////////////////////////////////////////////////////////
			// IF NOT CROUCHED & WILL STILL HAVE ENOUGH APs TO DO THIS SAME BEST
			// ATTACK AFTER A STANCE CHANGE, CONSIDER CHANGING STANCE
			//////////////////////////////////////////////////////////////////////////



			// HEADROCK HAM 4: No longer necessary to do here. The conditions above already handle this, specifically
			// WITHOUT messing with the BestAttack.ubAimTime variable, since that can apply now even when bursting
			// or autofiring!!
			/*
			if (BestAttack.ubAimTime == BURSTING)
			{
				pSoldier->aiData.bAimTime			= 0;
				pSoldier->bDoBurst			= 1;
				pSoldier->bDoAutofire		= 0;
			}
			else if(BestAttack.ubAimTime >= AUTOFIRING)
			{
				pSoldier->aiData.bAimTime			= 0;
				pSoldier->bDoBurst			= 1;
				pSoldier->bDoAutofire		= BestAttack.ubAimTime-AUTOFIRING;

				BestAttack.ubAimTime = AUTOFIRING;
			}*/

			/*
			else // defaults already set
			{
			pSoldier->aiData.bAimTime			= BestAttack.ubAimTime;
			pSoldier->bDoBurst			= 0;
			}
			*/

		}
		else if (ubBestAttackAction == AI_ACTION_THROW_KNIFE)
		{
			if (BestAttack.bWeaponIn != HANDPOS && gAnimControl[ pSoldier->usAnimState ].ubEndHeight == ANIM_STAND )
			{
				// we had better make sure we lower our gun first!

				pSoldier->aiData.bAction = AI_ACTION_LOWER_GUN;
				pSoldier->aiData.usActionData = 0;

				// queue up attack for after we lower weapon if any
				pSoldier->aiData.bNextAction = AI_ACTION_THROW_KNIFE;
				pSoldier->aiData.usNextActionData = BestAttack.sTarget;
				pSoldier->aiData.bNextTargetLevel = BestAttack.bTargetLevel;
			}

		}
		else if (ubBestAttackAction == AI_ACTION_KNIFE_MOVE)
		{
			// sevenfm: don't change aim time calculated in CalsBestStab
			pSoldier->aiData.bAimTime = BestAttack.ubAimTime;

			// sevenfm: dynamically decide stab location
			if( fBestAttackTargetStateKnown && BestAttack.ubOpponent != NOBODY )
			{
				UINT32	uiRoll;
				UINT8	ubChanceHead = 0;
				UINT8	ubRealCTH = (UINT8)BestAttack.ubChanceToReallyHit;

				// by default stab at torso
				pSoldier->bAimShotLocation = AIM_SHOT_TORSO;

				// attack to head randomly
				if( gAnimControl[ MercPtrs[BestAttack.ubOpponent]->usAnimState ].ubEndHeight != ANIM_PRONE )
				{	
					ubChanceHead = 6;

					if( HAS_SKILL_TRAIT(pSoldier, MARTIAL_ARTS_NT))
					{
						ubChanceHead += 5 * NUM_SKILL_TRAITS(pSoldier, MARTIAL_ARTS_NT);
					}

					ubChanceHead += ubRealCTH / 2;

					ubChanceHead = ubChanceHead * ubRealCTH / 100;
				}

				// randomly decide hit location
				uiRoll = PreRandom( 100 );					

				if (uiRoll > 100 - ubChanceHead)
				{
					pSoldier->bAimShotLocation = AIM_SHOT_HEAD;
				}
				else
				{
					pSoldier->bAimShotLocation = AIM_SHOT_TORSO;
				}
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// OTHERWISE, JUST GO AHEAD & ATTACK!
		//////////////////////////////////////////////////////////////////////////
		DebugAI(AI_MSG_INFO, pSoldier, String("Attack!"));

		//dnl ch64 270813 must be as below RearrangePocket with FOREVER will screw already decided BURST or AUTOFIRE
		INT8 bDoBurst = pSoldier->bDoBurst;
		UINT8 bDoAutofire = pSoldier->bDoAutofire;
		// swap weapon to hand if necessary
		if (BestAttack.bWeaponIn != HANDPOS)
		{
			DebugAI(AI_MSG_INFO, pSoldier, String("swap weapon into hand from %d", BestAttack.bWeaponIn));
			RearrangePocket(pSoldier, HANDPOS, BestAttack.bWeaponIn, FOREVER);
		}
		if (ubBestAttackAction == AI_ACTION_FIRE_GUN && bDoBurst == 1)//dnl ch64 270813
		{
			DebugAI(AI_MSG_INFO, pSoldier, String("using burst/autofire"));

			pSoldier->bDoAutofire = bDoAutofire;
			pSoldier->bDoBurst = bDoBurst;
			if (bDoAutofire > 1)
				pSoldier->bWeaponMode = WM_AUTOFIRE;
			else
				pSoldier->bWeaponMode = WM_BURST;
		}

		DebugAI(AI_MSG_INFO, pSoldier, String("prepare attack at target %d level %d aim %d ap %d cth %d opponent %d", BestAttack.sTarget, BestAttack.bTargetLevel, BestAttack.ubAimTime, BestAttack.ubAPCost, BestAttack.ubChanceToReallyHit, BestAttack.ubOpponent));

		if (ubBestAttackAction == AI_ACTION_FIRE_GUN)
		{
			if (gAnimControl[pSoldier->usAnimState].ubEndHeight != BestAttack.ubStance  &&
				IsValidStance(pSoldier, BestAttack.ubStance))
			{
				pSoldier->aiData.bNextAction = AI_ACTION_FIRE_GUN;
				pSoldier->aiData.usNextActionData = BestAttack.sTarget;
				pSoldier->aiData.bNextTargetLevel = BestAttack.bTargetLevel;
				pSoldier->aiData.usActionData = BestAttack.ubStance;

				DebugAI(AI_MSG_INFO, pSoldier, String("Change stance before shooting"));
				return(AI_ACTION_CHANGE_STANCE);
			}
			else
			{
				pSoldier->aiData.usActionData = BestAttack.sTarget;
				pSoldier->bTargetLevel = BestAttack.bTargetLevel;
				return(AI_ACTION_FIRE_GUN);
			}
		}
		else if (ubBestAttackAction == AI_ACTION_TOSS_PROJECTILE)
		{
			DebugAI(AI_MSG_INFO, pSoldier, String("toss attack, disable burst/autofire"));
			pSoldier->bDoBurst = 0;
			pSoldier->bDoAutofire = 0;

			// if firing mortar make sure we have room
			if (Item[pSoldier->inv[BestAttack.bWeaponIn].usItem].mortar)
			{
				DebugAI(AI_MSG_INFO, pSoldier, String("using mortar, check room to deploy"));

				ubOpponentDir = AIDirection(pSoldier->sGridNo, BestAttack.sTarget);

				// Get new gridno!
				sCheckGridNo = NewGridNo(pSoldier->sGridNo, DirectionInc(ubOpponentDir));

				if (!OKFallDirection(pSoldier, sCheckGridNo, pSoldier->pathing.bLevel, ubOpponentDir, pSoldier->usAnimState))
				{
					DebugAI(AI_MSG_INFO, pSoldier, String("no room to deploy mortar, check if we can move behind"));

					// try behind us, see if there's room to move back
					sCheckGridNo = NewGridNo(pSoldier->sGridNo, DirectionInc(gOppositeDirection[ubOpponentDir]));
					if (OKFallDirection(pSoldier, sCheckGridNo, pSoldier->pathing.bLevel, gOppositeDirection[ubOpponentDir], pSoldier->usAnimState))
					{
						// sevenfm: check if we can reach this gridno
						INT32 iPathCost = EstimatePlotPath(pSoldier, sCheckGridNo, FALSE, FALSE, FALSE, DetermineMovementMode(pSoldier, AI_ACTION_GET_CLOSER), pSoldier->bStealthMode, FALSE, 0);
						if (!AIShouldAvoidAdvance(pSoldier) &&
							iPathCost != 0 &&
							iPathCost + BestAttack.ubAPCost + GetAPsToLook(pSoldier) + GetAPsCrouch(pSoldier, FALSE) <= pSoldier->bActionPoints)
						{
							DebugAI(AI_MSG_INFO, pSoldier, String("moving backwards to have more room to deploy mortar"));
							pSoldier->aiData.usActionData = sCheckGridNo;

							DebugAI(AI_MSG_INFO, pSoldier, String("prepare next action throw at spot %d level %d aimtime %d", BestAttack.sTarget, BestAttack.bTargetLevel, BestAttack.ubAimTime));

							pSoldier->aiData.usNextActionData = BestAttack.sTarget;
							pSoldier->aiData.bNextTargetLevel = BestAttack.bTargetLevel;
							pSoldier->aiData.bAimTime = BestAttack.ubAimTime;

							pSoldier->aiData.bNextAction = AI_ACTION_TOSS_PROJECTILE;

							return AI_ACTION_GET_CLOSER;
						}
					}

					// can't fire!
					BestAttack.ubPossible = FALSE;
				}
			}

			// if still possible
			if (BestAttack.ubPossible)
			{
				DebugAI(AI_MSG_INFO, pSoldier, String("prepare throw at spot %d level %d aimtime %d", BestAttack.sTarget, BestAttack.bTargetLevel, BestAttack.ubAimTime));

				// sevenfm: correctly set weapon mode for attached GL
				if (IsGrenadeLauncherAttached(&pSoldier->inv[HANDPOS]))
				{
					DebugAI(AI_MSG_INFO, pSoldier, String("set attached GL mode"));
					pSoldier->bWeaponMode = WM_ATTACHED_GL;
				}

				// stand up before throwing if needed
				if (gAnimControl[pSoldier->usAnimState].ubEndHeight < BestAttack.ubStance &&
					pSoldier->InternalIsValidStance(AIDirection(pSoldier->sGridNo, BestAttack.sTarget), BestAttack.ubStance))
				{
					pSoldier->aiData.usActionData = BestAttack.ubStance;
					pSoldier->aiData.bNextAction = AI_ACTION_TOSS_PROJECTILE;
					pSoldier->aiData.usNextActionData = BestAttack.sTarget;
					pSoldier->aiData.bNextTargetLevel = BestAttack.bTargetLevel;
					pSoldier->aiData.bAimTime = BestAttack.ubAimTime;
					return AI_ACTION_CHANGE_STANCE;
				}
				else
				{
					pSoldier->aiData.usActionData = BestAttack.sTarget;
					pSoldier->bTargetLevel = BestAttack.bTargetLevel;
					pSoldier->aiData.bAimTime = BestAttack.ubAimTime;
				}

				return(AI_ACTION_TOSS_PROJECTILE);
			}
		}
		// other attacks
		else
		{
			pSoldier->aiData.usActionData = BestAttack.sTarget;
			pSoldier->bTargetLevel = BestAttack.bTargetLevel;
			return(ubBestAttackAction);
		}
	}

	// end of tank AI
	if ( TANK( pSoldier ) )
	{
		return( AI_ACTION_NONE );
	}

	// try to make boxer close if possible
	if (pSoldier->flags.uiStatusFlags & SOLDIER_BOXER )
	{
		if ( ubCanMove )
		{
			sClosestOpponent = ClosestSeenOpponent(pSoldier, NULL, NULL);
			
			if (!TileIsOutOfBounds(sClosestOpponent))
			{
				// temporarily make boxer have orders of CLOSEPATROL rather than STATIONARY
				// And make him patrol the ring, not his usual place
				// so he has a good roaming range
				USHORT tgrd = pSoldier->aiData.sPatrolGrid[0];
				pSoldier->aiData.sPatrolGrid[0] = pSoldier->sGridNo;
				pSoldier->aiData.bOrders = CLOSEPATROL;
				pSoldier->aiData.usActionData = GoAsFarAsPossibleTowards( pSoldier, sClosestOpponent, AI_ACTION_GET_CLOSER );
				pSoldier->aiData.sPatrolGrid[0] = tgrd;
				pSoldier->aiData.bOrders = STATIONARY;
				
				if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
				{
					// truncate path to 1 step
					pSoldier->aiData.usActionData = pSoldier->sGridNo + DirectionInc( (UINT8) pSoldier->pathing.usPathingData[0] );
					pSoldier->pathing.sFinalDestination = pSoldier->aiData.usActionData;
					pSoldier->aiData.bNextAction = AI_ACTION_END_TURN;
					return( AI_ACTION_GET_CLOSER );
				}
			}
		}
		// otherwise do nothing
		return( AI_ACTION_NONE );
	}

	////////////////////////////////////////////////////////////////////////////
	// IF A LOCATION WITH BETTER COVER IS AVAILABLE & REACHABLE, GO FOR IT!
	////////////////////////////////////////////////////////////////////////////
	
	if (!TileIsOutOfBounds(sBestCover))
	{
		//ScreenMsg( FONT_MCOLOR_LTYELLOW, MSG_TESTVERSION, L"AI %d taking cover, morale %d, from %d to %d", pSoldier->ubID, pSoldier->aiData.bAIMorale, pSoldier->sGridNo, sBestCover );
		pSoldier->aiData.usActionData = sBestCover;
		// sevenfm: disabled
		/*if(!TileIsOutOfBounds(sClosestOpponent))//dnl ch58 150913 After taking cover change facing toward recent target or closest enemy, currently such turn not charge APs and seems because AI is still in moving animation from take cover action
		{
			if(!TileIsOutOfBounds(pSoldier->sLastTarget))
				sClosestOpponent = pSoldier->sLastTarget;
			pSoldier->aiData.bNextAction = AI_ACTION_CHANGE_FACING;
			pSoldier->aiData.usNextActionData = atan8(CenterX(sBestCover),CenterY(sBestCover),CenterX(sClosestOpponent),CenterY(sClosestOpponent));
		}*/
		return(AI_ACTION_TAKE_COVER);
	}


	////////////////////////////////////////////////////////////////////////////
	// IF THINGS ARE REALLY HOPELESS, OR UNARMED, TRY TO RUN AWAY
	////////////////////////////////////////////////////////////////////////////

	// if soldier has enough APs left to move at least 1 square's worth
	//if ( ubCanMove && (pSoldier->bTeam != gbPlayerNum || pSoldier->aiData.fAIFlags & AI_RTP_OPTION_CAN_RETREAT) )
	if (ubCanMove && pSoldier->bTeam != gbPlayerNum)
	{
		if ((pSoldier->aiData.bAIMorale == MORALE_HOPELESS) || !bCanAttack)
		{
			// look for best place to RUN AWAY to (farthest from the closest threat)
			//pSoldier->aiData.usActionData = RunAway( pSoldier );
			pSoldier->aiData.usActionData = FindSpotMaxDistFromOpponents(pSoldier);
			
			if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
			{
				return(AI_ACTION_RUN_AWAY);
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// IF SPOTTERS HAVE BEEN CALLED FOR, AND WE HAVE SOME NEW SIGHTINGS, RADIO!
	////////////////////////////////////////////////////////////////////////////

	// if we're a computer merc, and we have the action points remaining to RADIO
	// (we never want NPCs to choose to radio if they would have to wait a turn)
	// and we're not swimming in deep water, and somebody has called for spotters
	// and we see the location of at least 2 opponents
	if ( !(pSoldier->usSoldierFlagMask & SOLDIER_RAISED_REDALERT) &&
		!AIDisengagementActive(pSoldier) && !AIEscapeActive(pSoldier) &&
		(gTacticalStatus.ubSpottersCalledForBy != NOBODY) &&
		MercPtrs[gTacticalStatus.ubSpottersCalledForBy] &&
		AISameFireteam(pSoldier, MercPtrs[gTacticalStatus.ubSpottersCalledForBy]) &&
		(pSoldier->bActionPoints >= APBPConstants[AP_RADIO]) &&
		(pSoldier->aiData.bOppCnt > 1) && !fCivilian &&
		(AICombatTeam(pSoldier) ? AIFireteamCombatReadyCount(pSoldier) > 1 : gTacticalStatus.Team[pSoldier->bTeam].bMenInSector > 1) && !bInDeepWater)
	{
		// base chance depends on how much new info we have to radio to the others
		iChance = 25 * WhatIKnowThatPublicDont(pSoldier,TRUE);	// just count them

		// if I actually know something they don't
		if (iChance)
		{
			if ((INT16)PreRandom(100) < iChance)
			{
				return(AI_ACTION_RED_ALERT);
			}
		}
	}


	////////////////////////////////////////////////////////////////////////////
	// CROUCH IF NOT CROUCHING ALREADY
	////////////////////////////////////////////////////////////////////////////

	// if not in water and not already crouched, try to crouch down first
	if (!gfTurnBasedAI || GetAPsToChangeStance( pSoldier, ANIM_CROUCH ) <= pSoldier->bActionPoints)
	{
		if ( !fCivilian && !gfHiddenInterrupt && IsValidStance( pSoldier, ANIM_CROUCH ) && ubBestAttackAction != AI_ACTION_KNIFE_MOVE && ubBestAttackAction != AI_ACTION_KNIFE_STAB && ubBestAttackAction != AI_ACTION_STEAL_MOVE) // SANDRO - if knife attack don't crouch
		{
			// determine the location of the known closest opponent
			// (don't care if he's conscious, don't care if he's reachable at all)			
			// SANDRO - don't crouch if in close combat distance (we got penalties that way)
			if (PythSpacesAway(pSoldier->sGridNo, sClosestSeenOpponent) > 1 )
			{
				pSoldier->aiData.usActionData = StanceChange( pSoldier, BestAttack.ubAPCost );
				if (pSoldier->aiData.usActionData != 0)
				{
					if (pSoldier->aiData.usActionData == ANIM_PRONE)
					{
						// we might want to turn before lying down!
						if ( (!gfTurnBasedAI || GetAPsToLook( pSoldier ) <= pSoldier->bActionPoints - GetAPsToChangeStance( pSoldier, (INT8) pSoldier->aiData.usActionData )) &&
							(((pSoldier->aiData.bAIMorale > MORALE_HOPELESS) || ubCanMove) && !AimingGun(pSoldier)) )
						{
							// if we have a closest seen opponent						
							if (!TileIsOutOfBounds(sClosestSeenOpponent))
							{
								bDirection = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(sClosestSeenOpponent),CenterY(sClosestSeenOpponent));

								// if we're not facing towards him
								if (pSoldier->ubDirection != bDirection)
								{
									if ( pSoldier->InternalIsValidStance( bDirection, (INT8) pSoldier->aiData.usActionData) )
									{
										// change direction, THEN change stance!
										pSoldier->aiData.bNextAction = AI_ACTION_CHANGE_STANCE;
										pSoldier->aiData.usNextActionData = pSoldier->aiData.usActionData;
										pSoldier->aiData.usActionData = bDirection;

										return(AI_ACTION_CHANGE_FACING);
									}
									else if ( (pSoldier->aiData.usActionData == ANIM_PRONE) && (pSoldier->InternalIsValidStance( bDirection, ANIM_CROUCH) ) )
									{
										// we shouldn't go prone, since we can't turn to shoot
										pSoldier->aiData.usActionData = ANIM_CROUCH;
										pSoldier->aiData.bNextAction = AI_ACTION_END_TURN;
										return( AI_ACTION_CHANGE_STANCE );
									}
								}
								// else we are facing in the right direction

							}
							// else we don't know any enemies
						}

						// we don't want to turn
					}
					pSoldier->aiData.bNextAction = AI_ACTION_END_TURN;
					return( AI_ACTION_CHANGE_STANCE );
				}
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// TURN TO FACE CLOSEST KNOWN OPPONENT (IF NOT FACING THERE ALREADY)
	////////////////////////////////////////////////////////////////////////////

	if (!gfTurnBasedAI || GetAPsToLook( pSoldier ) <= pSoldier->bActionPoints)
	{
		// hopeless guys shouldn't waste their time this way, UNLESS they CAN move
		// but chose not to to get this far (which probably means they're cornered)
		// ALSO, don't bother turning if we're already aiming a gun
		if ( !gfHiddenInterrupt && ((pSoldier->aiData.bAIMorale > MORALE_HOPELESS) || ubCanMove) && !AimingGun(pSoldier))
		{
			// determine the location of the known closest opponent
			// (don't care if he's conscious, don't care if he's reachable at all)


			sClosestOpponent = ClosestSeenOpponent(pSoldier, NULL, NULL);
			// if we have a closest reachable opponent			
			if (!TileIsOutOfBounds(sClosestOpponent))
			{
				//if(!TileIsOutOfBounds(pSoldier->sLastTarget))//dnl ch58 150913
					//sClosestOpponent = pSoldier->sLastTarget;
				bDirection = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(sClosestOpponent),CenterY(sClosestOpponent));

				// if we're not facing towards him
				if ( pSoldier->ubDirection != bDirection && pSoldier->InternalIsValidStance( bDirection, gAnimControl[ pSoldier->usAnimState ].ubEndHeight ) )
				{
					pSoldier->aiData.usActionData = bDirection;

					return(AI_ACTION_CHANGE_FACING);
				}
			}
		}
	}

	// if a militia has absofreaking nothing else to do, maybe they should radio in a report!

	////////////////////////////////////////////////////////////////////////////
	// RADIO RED ALERT: determine %chance to call others and report contact
	////////////////////////////////////////////////////////////////////////////

	if ( !(pSoldier->usSoldierFlagMask & SOLDIER_RAISED_REDALERT) && pSoldier->bTeam == MILITIA_TEAM && (pSoldier->bActionPoints >= APBPConstants[AP_RADIO]) && (gTacticalStatus.Team[pSoldier->bTeam].bMenInSector > 1) )
	{

		// if there hasn't been an initial RED ALERT yet in this sector
		if ( !(gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition) || NeedToRadioAboutPanicTrigger() )
		{     // since I'm at STATUS RED, I obviously know we're being invaded!
			DebugMsg(TOPIC_JA2AI,DBG_LEVEL_3,String("DecideActionBlack: check chance to radio contact"));
			iChance = gbDiff[DIFF_RADIO_RED_ALERT][ SoldierDifficultyLevel( pSoldier ) ];
		}
		else // subsequent radioing (only to update enemy positions, request help)
			// base chance depends on how much new info we have to radio to the others
			iChance = 10 * WhatIKnowThatPublicDont(pSoldier,FALSE);  // use 10 * for RED alert

		// if I actually know something they don't and I ain't swimming (deep water)
		if (iChance && !bInDeepWater)
		{
			// modify base chance according to orders
			switch (pSoldier->aiData.bOrders)
			{
			case STATIONARY:       iChance +=  20;  break;
			case ONGUARD:          iChance +=  15;  break;
			case ONCALL:           iChance +=  10;  break;
			case CLOSEPATROL:                       break;
			case RNDPTPATROL:
			case POINTPATROL:      iChance +=  -5;  break;
			case FARPATROL:        iChance += -10;  break;
			case SEEKENEMY:        iChance += -20;  break;
			case SNIPER:			  iChance += -10;  break;
			}

			// modify base chance according to attitude
			switch (pSoldier->aiData.bAttitude)
			{
			case DEFENSIVE:        iChance +=  20;  break;
			case BRAVESOLO:        iChance += -10;  break;
			case BRAVEAID:                          break;
			case CUNNINGSOLO:      iChance +=  -5;  break;
			case CUNNINGAID:                        break;
			case AGGRESSIVE:       iChance += -20;  break;
			case ATTACKSLAYONLY:		iChance = 0;
			}

			if (gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition)
			{
				// ignore morale (which could be really high)
			}
			else
			{
				// modify base chance according to morale
				switch (pSoldier->aiData.bAIMorale)
				{
				case MORALE_HOPELESS:  iChance *= 3;    break;
				case MORALE_WORRIED:   iChance *= 2;    break;
				case MORALE_NORMAL:                     break;
				case MORALE_CONFIDENT: iChance /= 2;    break;
				case MORALE_FEARLESS:  iChance /= 3;    break;
				}
			}

			// reduce chance because we're in combat
			iChance /= 2;

			if ((INT16) PreRandom(100) < iChance)
			{
				return(AI_ACTION_RED_ALERT);
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// LEAVE THE SECTOR
	////////////////////////////////////////////////////////////////////////////

	// NOT IMPLEMENTED

	////////////////////////////////////////////////////////////////////////////
	// DO NOTHING: Not enough points left to move, so save them for next turn
	////////////////////////////////////////////////////////////////////////////

	// by default, if everything else fails, just stand in place and wait
	pSoldier->aiData.usActionData = NOWHERE;
	return(AI_ACTION_NONE);

}

void DecideAlertStatus( SOLDIERTYPE *pSoldier )
{
	INT8	bOldStatus;
	INT32	iDummy;
	BOOLEAN fClimbDummy,fReachableDummy;

	// THE FOUR (4) POSSIBLE ALERT STATUSES ARE:
	// GREEN - No one seen, no suspicious noise heard, go about regular duties
	// YELLOW - Suspicious noise was heard personally or radioed in by buddy
	// RED - Either saw opponents in person, or definite contact had been radioed
	// BLACK - Currently has one or more opponents in sight

	// save the man's previous status

	if (pSoldier->flags.uiStatusFlags & SOLDIER_MONSTER)
	{
		CreatureDecideAlertStatus( pSoldier );
		return;
	}

	bOldStatus = pSoldier->aiData.bAlertStatus;

	// determine the current alert status for this category of man
	//if (!(pSoldier->flags.uiStatusFlags & SOLDIER_PC))
	{
		if (pSoldier->aiData.bOppCnt > 0)        // opponent(s) in sight
		{
			pSoldier->aiData.bAlertStatus = STATUS_BLACK;
			CheckForChangingOrders( pSoldier );
		}
		else                        // no opponents are in sight
		{
			switch (bOldStatus)
			{
			case STATUS_BLACK:
				// then drop back to RED status
				pSoldier->aiData.bAlertStatus = STATUS_RED;
				break;

			case STATUS_RED:
				// RED can never go back down below RED, only up to BLACK
				break;

			case STATUS_YELLOW:
				// Distant team awareness alone is not personal RED alert; direct fire is.
				if (!PTR_CIVILIAN && pSoldier->aiData.bUnderFire)
				{
					pSoldier->aiData.bAlertStatus = STATUS_RED;
				}
				else
				{
					// if we are NOT aware of any uninvestigated noises right now
					// and we are not currently in the middle of an action
					// (could still be on his way heading to investigate a noise!)					
					if (( TileIsOutOfBounds(MostImportantNoiseHeard(pSoldier,&iDummy,&fClimbDummy,&fReachableDummy))) && !pSoldier->aiData.bActionInProgress)
					{
						// then drop back to GREEN status
						pSoldier->aiData.bAlertStatus = STATUS_GREEN;
						CheckForChangingOrders( pSoldier );
					}
				}
				break;

			case STATUS_GREEN:
				// Distant team awareness alone is not personal RED alert; direct fire is.
				if (!PTR_CIVILIAN && pSoldier->aiData.bUnderFire)
				{
					pSoldier->aiData.bAlertStatus = STATUS_RED;
				}
				else
				{
					// if we ARE aware of any uninvestigated noises right now					
					if (!TileIsOutOfBounds(MostImportantNoiseHeard(pSoldier,&iDummy,&fClimbDummy,&fReachableDummy)))
					{
						// then move up to YELLOW status
						pSoldier->aiData.bAlertStatus = STATUS_YELLOW;
					}
				}
				break;
			}
			// otherwise, RED stays RED, YELLOW stays YELLOW, GREEN stays GREEN
		}
	}

#if 0
	else
	{
		if (pSoldier->bOppCnt > 0)
		{
			pSoldier->aiData.bAlertStatus = STATUS_BLACK; // opponent(s) in sight
		}
		else
		{
			pSoldier->aiData.bAlertStatus = STATUS_RED;         // enemy sector

			/*
			// wow, JA1 stuff...
			// good guys all have a built-in, magic, "enemy detecting radar"...
			if (Status.enemies)
			pSoldier->aiData.bAlertStatus = STATUS_RED;         // enemy sector
			else
			{
			pSoldier->aiData.bAlertStatus = STATUS_GREEN;       // secured sector

			// if he just dropped back from alert status, and it's a GUARD
			if ((oldStatus >= STATUS_RED) && (pSoldier->manCategory == MAN_GUARD))
			{			
			if (TileIsOutOfBounds(pSoldier->whereIWas))       // not assigned to any trees
			// FUTURE ENHANCEMENT: Look for unguarded trees with tappers
			pSoldier->orders = ONCALL;
			else                                 // assigned to trees
			// FUTURE ENHANCEMENT: If his tree is now tapperless, go ONCALL
			pSoldier->orders = CLOSEPATROL;         // go back to his tree area

			// turn off any existing bypass to Green and its "hyper-activity"
			pSoldier->bypassToGreen = FALSE;

			// turn off the "inTheWay" flag, may have been set during TurnBased
			pSoldier->inTheWay = FALSE;

			// make the guard put his gun away if he has it drawn
			HandleNoMoreTarget(pSoldier);
			}
			}
			*/
		}
	}
#endif

	if ( gTacticalStatus.bBoxingState == NOT_BOXING )
	{

		// if the man's alert status has changed in any way
		if (pSoldier->aiData.bAlertStatus != bOldStatus)
		{
			// HERE ARE TRYING TO AVOID NPCs SHUFFLING BACK & FORTH BETWEEN RED & BLACK
			// if either status is < RED (ie. anything but RED->BLACK && BLACK->RED)
			if ((bOldStatus < STATUS_RED) || (pSoldier->aiData.bAlertStatus < STATUS_RED))
			{
				// force a NEW action decision on next pass through HandleManAI()
				SetNewSituation( pSoldier );
			}

			// if this guy JUST discovered that there were opponents here for sure...
			if ((bOldStatus < STATUS_RED) && (pSoldier->aiData.bAlertStatus >= STATUS_RED))
			{
				CheckForChangingOrders(pSoldier);
			}
		}
		else   // status didn't change
		{
			// only do this stuff in TB
			// if a guy on status GREEN or YELLOW is running low on breath
			if (((pSoldier->aiData.bAlertStatus == STATUS_GREEN)  && (pSoldier->bBreath < 75)) ||
				((pSoldier->aiData.bAlertStatus == STATUS_YELLOW) && (pSoldier->bBreath < 50)))
			{
				// as long as he's not in water (standing on a bridge is OK)
				if (!pSoldier->MercInWater())
				{
					// force a NEW decision so that he can get some rest
					SetNewSituation( pSoldier );

					// current action will be canceled. if noise is no longer important					
					if ((pSoldier->aiData.bAlertStatus == STATUS_YELLOW) &&
						( TileIsOutOfBounds(MostImportantNoiseHeard(pSoldier,&iDummy,&fClimbDummy,&fReachableDummy))))
					{
						// then drop back to GREEN status
						pSoldier->aiData.bAlertStatus = STATUS_GREEN;
						CheckForChangingOrders( pSoldier );
					}
				}
			}
		}
	}
	else if (gTacticalStatus.bBoxingState == DISQUALIFIED ||
		gTacticalStatus.bBoxingState == WON_ROUND ||
		gTacticalStatus.bBoxingState == LOST_ROUND)
	{
		pSoldier->aiData.bAlertStatus = STATUS_GREEN;
	}

}

// ------------------------------ ZOMBIE AI --------------------------
INT8 ZombieDecideActionGreen(SOLDIERTYPE *pSoldier)
{
	DOUBLE iChance, iSneaky = 10;
	INT8  bInWater;

	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen, orders = %d",pSoldier->aiData.bOrders));
	DebugAI(AI_MSG_START, pSoldier, String("[Green Zombie]"));
	DebugAI(AI_MSG_INFO, pSoldier, String("AP=%d/%d %s %s %s %s %s", pSoldier->bActionPoints, pSoldier->bInitialActionPoints, gStr8AlertStatus[pSoldier->aiData.bAlertStatus], gStr8Orders[pSoldier->aiData.bOrders], gStr8Attitude[pSoldier->aiData.bAttitude], gStr8Team[pSoldier->bTeam], gStr8Class[pSoldier->ubSoldierClass]));
	DebugAI(AI_MSG_INFO, pSoldier, String("Health %d/%d Breath %d/%d Shock %d Tolerance %d AI Morale %d", pSoldier->stats.bLife, pSoldier->stats.bLifeMax, pSoldier->bBreath, pSoldier->bBreathMax, pSoldier->aiData.bShock, CalcSuppressionTolerance(pSoldier), pSoldier->aiData.bAIMorale));

	gubNPCPathCount = 0;

	bInWater = Water( pSoldier->sGridNo, pSoldier->pathing.bLevel );

	if ( bInWater && PreRandom( 2 ) )
	{
		// don't do nuttin!
		return( AI_ACTION_NONE );
	}

	// if real-time, and not in the way, do nothing 90% of the time (for GUARDS!)
	// unless in water (could've started there), then we better swim to shore!

	////////////////////////////////////////////////////////////////////////////
	// POINT PATROL: move towards next point unless getting a bit winded
	////////////////////////////////////////////////////////////////////////////

	// this takes priority over water/gas checks, so that point patrol WILL work
	// from island to island, and through gas covered areas, too
	if ((pSoldier->aiData.bOrders == POINTPATROL) && (pSoldier->bBreath >= 25))
	{
		if (PointPatrolAI(pSoldier))
		{
			if (!gfTurnBasedAI)
			{
				// wait after this...
				pSoldier->aiData.bNextAction = AI_ACTION_WAIT;
				pSoldier->aiData.usNextActionData = RealtimeDelay( pSoldier );
			}
			return(AI_ACTION_POINT_PATROL);
		}
		else
		{
			// Reset path count to avoid dedlok
			gubNPCPathCount = 0;
		}
	}

	if ((pSoldier->aiData.bOrders == RNDPTPATROL) && (pSoldier->bBreath >=25))
	{
		if (RandomPointPatrolAI(pSoldier))
		{
			if (!gfTurnBasedAI)
			{
				// wait after this...
				pSoldier->aiData.bNextAction = AI_ACTION_WAIT;
				pSoldier->aiData.usNextActionData = RealtimeDelay( pSoldier );
			}
			return(AI_ACTION_POINT_PATROL);
		}
		else
		{
			// Reset path count to avoid dedlok
			gubNPCPathCount = 0;
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// WHEN LEFT IN WATER OR GAS, GO TO NEAREST REACHABLE SPOT OF UNGASSED LAND
	////////////////////////////////////////////////////////////////////////////

	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("ZombieDecideActionGreen: get out of water and gas"));

	if ( bInWater )
	{
		pSoldier->aiData.usActionData = FindNearestUngassedLand(pSoldier);

		if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
		{
			return(AI_ACTION_LEAVE_WATER_GAS);
		}
	}

	////////////////////////////////////////////////////////////////////////
	// REST IF RUNNING OUT OF BREATH
	////////////////////////////////////////////////////////////////////////

	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("ZombieDecideActionGreen: rest if running out of breath"));
	// if our breath is running a bit low, and we're not in the way or in water
	if ((pSoldier->bBreath < 25) && !bInWater)
	{
		// take a breather for gods sake!
		// for realtime, AI will use a standard wait set outside of here
		pSoldier->aiData.usActionData = NOWHERE;
		return(AI_ACTION_NONE);
	}


	////////////////////////////////////////////////////////////////////////////
	// CLIMB A BUILDING
	////////////////////////////////////////////////////////////////////////////

	if ( gGameExternalOptions.fZombieCanClimb && pSoldier->aiData.bLastAction != AI_ACTION_CLIMB_ROOF && !is_networked )
	{
		iChance = 10 + pSoldier->aiData.bBypassToGreen;

		// set base chance and maximum seeking distance according to orders
		switch (pSoldier->aiData.bOrders)
		{
		case STATIONARY:     iChance *= 0; break;
		case ONGUARD:        iChance += 10; break;
		case ONCALL:                         break;
		case CLOSEPATROL:    iChance += -20; break;
		case RNDPTPATROL:
		case POINTPATROL:    iChance  = -30; break;
		case FARPATROL:      iChance += -40; break;
		case SEEKENEMY:      iChance += -30; break;
		case SNIPER:		 iChance += 70; break;
		}

		// modify for attitude
		switch (pSoldier->aiData.bAttitude)
		{
		case DEFENSIVE:      iChance *= 1.5;  break;
		case BRAVESOLO:      iChance /= 2;    break;
		case BRAVEAID:       iChance /= 2;   break;
		case CUNNINGSOLO:    iChance *= 1;    break;
		case CUNNINGAID:     iChance /= 1;   break;
		case AGGRESSIVE:     iChance /= 3;    break;
		case ATTACKSLAYONLY:									 break;
		}

		// This is the chance that we want to be on the roof.  If already there, invert the chance to see if we want back
		// down
		if (pSoldier->pathing.bLevel > 0)
		{
			iChance = 100 - iChance;
		}

		if ((INT16) PreRandom(100) < iChance)
		{
			BOOLEAN fUp = FALSE;
			if ( pSoldier->pathing.bLevel == 0 )
			{
				fUp = TRUE;
			}
			else if (pSoldier->pathing.bLevel > 0 )
			{
				fUp = FALSE;
			}

			if ( CanClimbFromHere ( pSoldier, fUp ) )
			{
				DebugMsg ( TOPIC_JA2AI , DBG_LEVEL_3 , String("Zombie %d is climbing roof",pSoldier->ubID) );
				return( AI_ACTION_CLIMB_ROOF );
			}
			else
			{
				pSoldier->aiData.usActionData = FindClosestClimbPoint(pSoldier, fUp );
				// Added the check here because sniper militia who are locked inside of a building without keys
				// will still have a >100% chance to want to climb, which means an infinite loop.  In fact, any
				// time a move is desired, there probably also will be a need to check for a path.				
				if ( !TileIsOutOfBounds(pSoldier->aiData.usActionData) &&
					LegalNPCDestination(pSoldier,pSoldier->aiData.usActionData,ENSURE_PATH,WATEROK, 0 ))
				{
					return( AI_ACTION_MOVE_TO_CLIMB  );
				}
			}
		}
	}


	////////////////////////////////////////////////////////////////////////////
	// RANDOM PATROL:  determine % chance to start a new patrol route
	////////////////////////////////////////////////////////////////////////////
	if (!gubNPCPathCount) // try to limit pathing in Green AI
	{
		iChance = 25 + pSoldier->aiData.bBypassToGreen;

		// sevenfm: limit chance
		if( pSoldier->bBreathMax > 0 )
			iChance = iChance * pSoldier->bBreath / pSoldier->bBreathMax;

		// if we're in water with land miles (> 25 tiles) away,
		// OR if we roll under the chance calculated
		if (bInWater || ((INT16) PreRandom(100) < iChance))
		{
			pSoldier->aiData.usActionData = RandDestWithinRange(pSoldier);

			if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
			{
				pSoldier->aiData.usActionData = GoAsFarAsPossibleTowards( pSoldier, pSoldier->aiData.usActionData, AI_ACTION_RANDOM_PATROL );
			}

			if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
			{
				if (!gfTurnBasedAI)
				{
					// wait after this...
					pSoldier->aiData.bNextAction = AI_ACTION_WAIT;
					pSoldier->aiData.usNextActionData = RealtimeDelay( pSoldier );
				}
				return(AI_ACTION_RANDOM_PATROL);
			}
		}
	}

	if (!gubNPCPathCount) // try to limit pathing in Green AI
	{
		////////////////////////////////////////////////////////////////////////////
		// SEEK FRIEND: determine %chance for man to pay a friendly visit
		////////////////////////////////////////////////////////////////////////////

		iChance = 25 + pSoldier->aiData.bBypassToGreen;

		// set base chance and maximum seeking distance according to orders
		switch (pSoldier->aiData.bOrders)
		{
		case STATIONARY:     iChance += -20; break;
		case ONGUARD:        iChance += -15; break;
		case ONCALL:                         break;
		case CLOSEPATROL:    iChance += +10; break;
		case RNDPTPATROL:
		case POINTPATROL:    iChance  = -10; break;
		case FARPATROL:      iChance += +20; break;
		case SEEKENEMY:      iChance += -10; break;
		case SNIPER:		 iChance += -10; break;
		}

		// modify for attitude
		switch (pSoldier->aiData.bAttitude)
		{
		case DEFENSIVE:                       break;
		case BRAVESOLO:      iChance /= 2;    break;  // loners
		case BRAVEAID:       iChance += 10;   break;  // friendly
		case CUNNINGSOLO:    iChance /= 2;    break;  // loners
		case CUNNINGAID:     iChance += 10;   break;  // friendly
		case AGGRESSIVE:                      break;
		case ATTACKSLAYONLY:				  break;
		}

		if ((INT16) PreRandom(100) < iChance)
		{
			if (RandomFriendWithin(pSoldier))
			{
				if ( pSoldier->aiData.usActionData == GoAsFarAsPossibleTowards( pSoldier, pSoldier->aiData.usActionData, AI_ACTION_SEEK_FRIEND ) )
				{
					if ( !gfTurnBasedAI)
					{
						// pause at the end of the walk!
						pSoldier->aiData.bNextAction = AI_ACTION_WAIT;
						pSoldier->aiData.usNextActionData = (UINT16) REALTIME_CIV_AI_DELAY;
					}

					return(AI_ACTION_SEEK_FRIEND);
				}
			}
		}
	}


	////////////////////////////////////////////////////////////////////////////
	// LOOK AROUND: determine %chance for man to turn in place
	////////////////////////////////////////////////////////////////////////////

	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("ZombieDecideActionGreen: Soldier deciding to turn"));
	if (GetAPsToLook( pSoldier ) <= pSoldier->bActionPoints)
	{
		// avoid 2 consecutive random turns in a row
		if (pSoldier->aiData.bLastAction != AI_ACTION_CHANGE_FACING)
		{
			iChance = 25 + pSoldier->aiData.bBypassToGreen;

			// set base chance according to orders
			if (pSoldier->aiData.bOrders == STATIONARY || pSoldier->aiData.bOrders == SNIPER)
				iChance += 25;

			if (pSoldier->aiData.bOrders == ONGUARD)
				iChance += 20;

			if (pSoldier->aiData.bAttitude == DEFENSIVE)
				iChance += 25;

			if ( pSoldier->aiData.bOrders == SNIPER && pSoldier->pathing.bLevel == 1)
				iChance += 35;

			if ((INT16)PreRandom(100) < iChance)
			{
				// roll random directions (stored in actionData) until different from current
				do
				{
					// if man has a LEGAL dominant facing, and isn't facing it, he will turn
					// back towards that facing 50% of the time here (normally just enemies)
					if ((pSoldier->aiData.bDominantDir >= 0) && (pSoldier->aiData.bDominantDir <= 8) &&
						(pSoldier->ubDirection != pSoldier->aiData.bDominantDir) && PreRandom(2) && pSoldier->aiData.bOrders != SNIPER )
					{
						pSoldier->aiData.usActionData = pSoldier->aiData.bDominantDir;
					}
					else
					{
						INT32 iNoiseValue;
						BOOLEAN fClimb;
						BOOLEAN fReachable;
						INT32 sNoiseGridNo = MostImportantNoiseHeard(pSoldier,&iNoiseValue, &fClimb, &fReachable);
						UINT8 ubNoiseDir;

						if (TileIsOutOfBounds(sNoiseGridNo) || 
							(ubNoiseDir = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(sNoiseGridNo),CenterY(sNoiseGridNo))
							) == pSoldier->ubDirection )

						{
							pSoldier->aiData.usActionData = PreRandom(8);
						}
						else
						{
							pSoldier->aiData.usActionData = ubNoiseDir;
						}
					}
				} while (pSoldier->aiData.usActionData == pSoldier->ubDirection);

				DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: Trying to turn - checking stance validity"));
				if ( pSoldier->InternalIsValidStance( (INT8) pSoldier->aiData.usActionData, gAnimControl[ pSoldier->usAnimState ].ubEndHeight ) )
				{

					if ( !gfTurnBasedAI )
					{
						// wait after this...
						pSoldier->aiData.bNextAction = AI_ACTION_WAIT;
						pSoldier->aiData.usNextActionData = RealtimeDelay( pSoldier );
					}

					DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionGreen: Soldier is turning"));
					return(AI_ACTION_CHANGE_FACING);
				}
			}
		}
	}


	////////////////////////////////////////////////////////////////////////////
	// NONE:
	////////////////////////////////////////////////////////////////////////////

	// by default, if everything else fails, just stands in place without turning
	// for realtime, regular AI guys will use a standard wait set outside of here
	pSoldier->aiData.usActionData = NOWHERE;
	return(AI_ACTION_NONE);
}

INT8 ZombieDecideActionYellow(SOLDIERTYPE *pSoldier)
{
	UINT8 ubNoiseDir;
	INT32 sNoiseGridNo;
	INT32 iNoiseValue;
	INT32 iChance;
	BOOLEAN fClimb;
	BOOLEAN fReachable;

	DebugAI(AI_MSG_START, pSoldier, String("[Yellow Zombie]"));
	DebugAI(AI_MSG_INFO, pSoldier, String("AP=%d/%d %s %s %s %s %s", pSoldier->bActionPoints, pSoldier->bInitialActionPoints, gStr8AlertStatus[pSoldier->aiData.bAlertStatus], gStr8Orders[pSoldier->aiData.bOrders], gStr8Attitude[pSoldier->aiData.bAttitude], gStr8Team[pSoldier->bTeam], gStr8Class[pSoldier->ubSoldierClass]));
	DebugAI(AI_MSG_INFO, pSoldier, String("Health %d/%d Breath %d/%d Shock %d Tolerance %d AI Morale %d", pSoldier->stats.bLife, pSoldier->stats.bLifeMax, pSoldier->bBreath, pSoldier->bBreathMax, pSoldier->aiData.bShock, CalcSuppressionTolerance(pSoldier), pSoldier->aiData.bAIMorale));

	// determine the most important noise heard, and its relative value
	sNoiseGridNo = MostImportantNoiseHeard(pSoldier,&iNoiseValue, &fClimb, &fReachable);

	if (TileIsOutOfBounds(sNoiseGridNo))
	{
		// then we have no business being under YELLOW status any more!
		return(AI_ACTION_NONE);
	}	

	////////////////////////////////////////////////////////////////////////////
	// LOOK AROUND TOWARD NOISE: determine %chance for man to turn towards noise
	////////////////////////////////////////////////////////////////////////////

	// determine direction from this soldier in which the noise lies
	ubNoiseDir = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(sNoiseGridNo),CenterY(sNoiseGridNo));

	// if soldier is not already facing in that direction,
	// and the noise source is close enough that it could possibly be seen
	if ( GetAPsToLook( pSoldier ) <= pSoldier->bActionPoints )
	{
		if ((pSoldier->ubDirection != ubNoiseDir) && PythSpacesAway(pSoldier->sGridNo,sNoiseGridNo) <= pSoldier->GetMaxDistanceVisible(sNoiseGridNo) )
		{
			// set base chance according to orders
			if ((pSoldier->aiData.bOrders == STATIONARY) || (pSoldier->aiData.bOrders == ONGUARD) )
				iChance = 50;
			else           // all other orders
				iChance = 25;

			if (pSoldier->aiData.bAttitude == DEFENSIVE)
				iChance += 15;


			if ((INT16)PreRandom(100) < iChance && pSoldier->InternalIsValidStance( ubNoiseDir, gAnimControl[ pSoldier->usAnimState ].ubEndHeight ) )
			{
				pSoldier->aiData.usActionData = ubNoiseDir;

				return(AI_ACTION_CHANGE_FACING);
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// RADIO YELLOW ALERT: determine %chance to call others and report noise
	////////////////////////////////////////////////////////////////////////////

	// if we have the action points remaining to RADIO
	// (we never want NPCs to choose to radio if they would have to wait a turn)
	if ( (pSoldier->bActionPoints >= APBPConstants[AP_RADIO]) && (gTacticalStatus.Team[pSoldier->bTeam].bMenInSector > 1) )
	{
		// base chance depends on how much new info we have to radio to the others
		iChance = 5 * WhatIKnowThatPublicDont(pSoldier,FALSE);   // use 5 * for YELLOW alert

		// if I actually know something they don't and I ain't swimming (deep water)
		if (iChance && !DeepWater( pSoldier->sGridNo, pSoldier->pathing.bLevel ))
		{
			// CJC: this addition allows for varying difficulty levels for soldier types
			iChance += gbDiff[ DIFF_RADIO_RED_ALERT ][ SoldierDifficultyLevel( pSoldier ) ] / 2;

			// Alex: this addition replaces the sectorValue/2 in original JA
			//iChance += gsDiff[DIFF_RADIO_RED_ALERT][GameOption[ENEMYDIFFICULTY]] / 2;

			// modify base chance according to orders
			switch (pSoldier->aiData.bOrders)
			{
			case STATIONARY: iChance +=  20;  break;
			case ONGUARD:    iChance +=  15;  break;
			case ONCALL:     iChance +=  10;  break;
			case CLOSEPATROL:                 break;
			case RNDPTPATROL:
			case POINTPATROL:                 break;
			case FARPATROL:  iChance += -10;  break;
			case SEEKENEMY:  iChance += -20;  break;
			case SNIPER:	 iChance += -10;  break; //Madd: sniper contacts are supposed to be automatically reported
			}

			// modify base chance according to attitude
			switch (pSoldier->aiData.bAttitude)
			{
			case DEFENSIVE:  iChance +=  20;  break;
			case BRAVESOLO:  iChance += -10;  break;
			case BRAVEAID:                    break;
			case CUNNINGSOLO:iChance +=  -5;  break;
			case CUNNINGAID:                  break;
			case AGGRESSIVE: iChance += -20;  break;
			case ATTACKSLAYONLY: iChance = 0; break;
			}

			if ((INT16)PreRandom(100) < iChance)
			{
				return(AI_ACTION_YELLOW_ALERT);
			}
		}
	}

	////////////////////////////////////////////////////////////////////////
	// REST IF RUNNING OUT OF BREATH
	////////////////////////////////////////////////////////////////////////

	// if our breath is running a bit low, and we're not in water
	if ((pSoldier->bBreath < 25) && !pSoldier->MercInWater())
	{
		// take a breather for gods sake!
		pSoldier->aiData.usActionData = NOWHERE;
		return(AI_ACTION_NONE);
	}

	pSoldier->aiData.usActionData = GoAsFarAsPossibleTowards(pSoldier,sNoiseGridNo,AI_ACTION_SEEK_NOISE);

	if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
	{
		if ( fClimb )
		{
			if ( pSoldier->sGridNo == sNoiseGridNo)
			{
				if (IsActionAffordable(pSoldier, pSoldier->aiData.bAction))
				{
					return( AI_ACTION_CLIMB_ROOF );
				}
				else if ( gfTurnBasedAI && pSoldier->bActionPoints < pSoldier->bInitialActionPoints )
				{
					return AI_ACTION_NONE;
				}
			}
			else
			{
				pSoldier->aiData.usActionData = sNoiseGridNo;							
				return( AI_ACTION_MOVE_TO_CLIMB  );
			}
		}
		else
		{
			return(AI_ACTION_SEEK_NOISE);
		}					
	}

	////////////////////////////////////////////////////////////////////////////
	// SWITCH TO GREEN: determine if soldier acts as if nothing at all was wrong
	////////////////////////////////////////////////////////////////////////////
	if ((INT16)PreRandom(100) < 50 )
	{
		// Skip YELLOW until new situation, 15% extra chance to do GREEN actions
		pSoldier->aiData.bBypassToGreen = 15;
		return(ZombieDecideActionGreen(pSoldier));
	}

	////////////////////////////////////////////////////////////////////////////
	// DO NOTHING: Not enough points left to move, so save them for next turn
	////////////////////////////////////////////////////////////////////////////

	// by default, if everything else fails, just stands in place without turning
	pSoldier->aiData.usActionData = NOWHERE;
	return(AI_ACTION_NONE);
}


INT8 ZombieDecideActionRed(SOLDIERTYPE *pSoldier)
{
	INT32 iDummy;
	INT32 iChance,sClosestOpponent,sClosestFriend;
	INT32 sClosestDisturbance, sDistVisible;
	INT32 sMostImportantNoise;
	INT32 sClosestThreat = NOWHERE;
	UINT8 ubCanMove,ubOpponentDir;
	INT8 bInWater, bInDeepWater;
	INT8 bSeekPts = 0, bHelpPts = 0, bWatchPts = 0;
	BOOLEAN fClimb;	

	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,String("ZombieDecideActionRed: soldier orders = %d",pSoldier->aiData.bOrders));
	DebugAI(AI_MSG_START, pSoldier, String("[Red Zombie]"));
	DebugAI(AI_MSG_INFO, pSoldier, String("AP=%d/%d %s %s %s %s %s", pSoldier->bActionPoints, pSoldier->bInitialActionPoints, gStr8AlertStatus[pSoldier->aiData.bAlertStatus], gStr8Orders[pSoldier->aiData.bOrders], gStr8Attitude[pSoldier->aiData.bAttitude], gStr8Team[pSoldier->bTeam], gStr8Class[pSoldier->ubSoldierClass]));
	DebugAI(AI_MSG_INFO, pSoldier, String("Health %d/%d Breath %d/%d Shock %d Tolerance %d AI Morale %d", pSoldier->stats.bLife, pSoldier->stats.bLifeMax, pSoldier->bBreath, pSoldier->bBreathMax, pSoldier->aiData.bShock, CalcSuppressionTolerance(pSoldier), pSoldier->aiData.bAIMorale));

	// if we have absolutely no action points, we can't do a thing under RED!
	if (!pSoldier->bActionPoints)
	{
		pSoldier->aiData.usActionData = NOWHERE;
		return(AI_ACTION_NONE);
	}

	// can this guy move to any of the neighbouring squares ? (sets TRUE/FALSE)
	ubCanMove = (pSoldier->bActionPoints >= MinPtsToMove(pSoldier));

	// determine if we happen to be in water (in which case we're in BIG trouble!)
	bInWater = Water( pSoldier->sGridNo, pSoldier->pathing.bLevel );
	bInDeepWater = DeepWater( pSoldier->sGridNo, pSoldier->pathing.bLevel );

	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: crouch and rest if running out of breath");
	////////////////////////////////////////////////////////////////////////
	// CROUCH & REST IF RUNNING OUT OF BREATH
	////////////////////////////////////////////////////////////////////////

	// if our breath is running a bit low, and we're not in water or under fire
	if ((pSoldier->bBreath < 25) && !bInWater && !pSoldier->aiData.bUnderFire)
	{		
		pSoldier->aiData.usActionData = NOWHERE;
		return(AI_ACTION_NONE);
	}

	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: main red ai");
	////////////////////////////////////////////////////////////////////////////
	// MAIN RED AI
	////////////////////////////////////////////////////////////////////////////

	// get the location of the closest reachable opponent
	sClosestDisturbance = ClosestReachableDisturbance(pSoldier, &fClimb);
	sClosestOpponent = ClosestKnownOpponent(pSoldier, NULL, NULL);
	sMostImportantNoise = MostImportantNoiseHeard( pSoldier, NULL, NULL, NULL );

	sClosestThreat = sClosestDisturbance;
	if(TileIsOutOfBounds( sClosestThreat ))
	{
		sClosestThreat = sClosestOpponent;
	}
	if( TileIsOutOfBounds( sClosestThreat ) )
	{
		sClosestThreat = sMostImportantNoise;
	}

	// if we can move at least 1 square's worth
	// and have more APs than we want to reserve
	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,String("decideactionred: can we move? = %d, APs = %d",ubCanMove,pSoldier->bActionPoints));

	if( ubCanMove &&
		pSoldier->bActionPoints > APBPConstants[MAX_AP_CARRIED] &&
		!gfHiddenInterrupt )
	{
		// if there is an opponent reachable					
		if (!TileIsOutOfBounds(sClosestDisturbance) )
		{
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: seek opponent");
			//////////////////////////////////////////////////////////////////////
			// SEEK CLOSEST DISTURBANCE: GO DIRECTLY TOWARDS CLOSEST KNOWN OPPONENT
			//////////////////////////////////////////////////////////////////////

			if (fClimb && pSoldier->sGridNo == sClosestDisturbance)
			{
				// wait for next turn if turnbased (to climb with max APs)
				if( gfTurnBasedAI &&
					pSoldier->bActionPoints < pSoldier->bInitialActionPoints )
				{
					pSoldier->aiData.bNextAction = AI_ACTION_NONE;
					return( AI_ACTION_END_TURN );
				}

				// climb
				if (IsActionAffordable(pSoldier, AI_ACTION_CLIMB_ROOF))
				{
					pSoldier->aiData.bNextAction = AI_ACTION_NONE;
					return( AI_ACTION_CLIMB_ROOF );
				}
				// cannot climb at all
			}

			// try to move towards him
			pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier, sClosestDisturbance, 0, AI_ACTION_SEEK_OPPONENT, 0);

			// if it's possible						
			if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
			{
				return(AI_ACTION_SEEK_OPPONENT);
			}
		}

		// if no opponent - go to closest friend
		sClosestFriend = ClosestReachableFriendInTrouble(pSoldier, &fClimb );

		if (!TileIsOutOfBounds(sClosestFriend) )
		{
			//////////////////////////////////////////////////////////////////////
			// GO DIRECTLY TOWARDS CLOSEST FRIEND UNDER FIRE OR WHO LAST RADIOED
			//////////////////////////////////////////////////////////////////////			

			if ( fClimb && pSoldier->sGridNo == sClosestFriend )
			{
				// wait for next turn to have full APs
				if( gfTurnBasedAI && pSoldier->bActionPoints < pSoldier->bInitialActionPoints )
				{
					return( AI_ACTION_END_TURN);
				}
				// climb
				if( IsActionAffordable(pSoldier, AI_ACTION_CLIMB_ROOF) )
				{
					return( AI_ACTION_CLIMB_ROOF );
				}
				// don't have enough AP for climbing
			}

			// try to move towards him
			pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier, sClosestFriend, 0, AI_ACTION_SEEK_FRIEND, 0);

			// if it's possible						
			if ( !TileIsOutOfBounds(pSoldier->aiData.usActionData) )
			{
				return(AI_ACTION_SEEK_FRIEND);
			}
		}

		// if cannot seek or help - hide
		if( pSoldier->bActionPoints == pSoldier->bInitialActionPoints &&
			(GuySawEnemy(pSoldier) || pSoldier->aiData.bUnderFire) )
		{
			pSoldier->aiData.usActionData = FindBestNearbyCover(pSoldier,pSoldier->aiData.bAIMorale,&iDummy);
			if( !TileIsOutOfBounds(pSoldier->aiData.usActionData) )
			{
				return AI_ACTION_TAKE_COVER;
			}
		}
	}

	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: look around towards opponent");
	////////////////////////////////////////////////////////////////////////////
	// LOOK AROUND TOWARD CLOSEST KNOWN OPPONENT, IF KNOWN
	////////////////////////////////////////////////////////////////////////////

	if( GetAPsToLook( pSoldier ) <= pSoldier->bActionPoints &&
		!TileIsOutOfBounds(sClosestThreat) )
	{
		// determine direction from this soldier to the closest opponent
		ubOpponentDir = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(sClosestThreat),CenterY(sClosestThreat));

		// if soldier is not already facing in that direction,
		// and the opponent is close enough that he could possibly be seen
		// note, have to change this to use the level returned from ClosestKnownOpponent
		sDistVisible = pSoldier->GetMaxDistanceVisible(sClosestThreat, 0, CALC_FROM_ALL_DIRS );

		if( pSoldier->ubDirection != ubOpponentDir &&
			PythSpacesAway(pSoldier->sGridNo,sClosestThreat) <= sDistVisible )
		{
			iChance = 25;

			if( (INT16)PreRandom(100) < iChance &&
				pSoldier->InternalIsValidStance( ubOpponentDir, gAnimControl[ pSoldier->usAnimState ].ubEndHeight ) )
			{
				pSoldier->aiData.usActionData = ubOpponentDir;

				return(AI_ACTION_CHANGE_FACING);
			}
		}			
	}

	////////////////////////////////////////////////////////////////////////////
	// SWITCH TO GREEN: soldier does ordinary regular patrol, seeks friends
	////////////////////////////////////////////////////////////////////////////

	// if not in combat or under fire, and we COULD have moved, just chose not to	
	if( (pSoldier->aiData.bAlertStatus != STATUS_BLACK) &&
		!pSoldier->aiData.bUnderFire &&
		ubCanMove &&
		pSoldier->bActionPoints == pSoldier->bInitialActionPoints &&
		TileIsOutOfBounds(sClosestDisturbance) )
	{
		// Skip RED until new situation/next turn, 30% extra chance to do GREEN actions
		pSoldier->aiData.bBypassToGreen = 30;
		return(ZombieDecideActionGreen(pSoldier) );
	}

	////////////////////////////////////////////////////////////////////////////
	// IF UNDER FIRE, FACE THE MOST IMPORTANT NOISE WE KNOW
	////////////////////////////////////////////////////////////////////////////

	if( pSoldier->aiData.bUnderFire &&
		pSoldier->bActionPoints == pSoldier->bInitialActionPoints &&
		GetAPsToLook( pSoldier ) <= pSoldier->bActionPoints &&
		!TileIsOutOfBounds( sMostImportantNoise ) )
	{
		ubOpponentDir = atan8( CenterX( pSoldier->sGridNo ), CenterY( pSoldier->sGridNo ), CenterX( sMostImportantNoise ), CenterY( sMostImportantNoise ) );
		if ( pSoldier->ubDirection != ubOpponentDir )
		{
			if ( GetAPsToLook( pSoldier ) <= pSoldier->bActionPoints )
			{
				pSoldier->aiData.usActionData = ubOpponentDir;
				return( AI_ACTION_CHANGE_FACING );
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// DO NOTHING: Not enough points left to move, so save them for next turn
	////////////////////////////////////////////////////////////////////////////
	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionRed: do nothing at all..."));

	pSoldier->aiData.usActionData = NOWHERE;
	return(AI_ACTION_NONE);
}

INT8 ZombieDecideActionBlack(SOLDIERTYPE *pSoldier)
{
	INT32		sClosestOpponent;
	INT32		sClosestDisturbance;
	INT16		ubMinAPCost;
	UINT8		ubCanMove;
	INT8		bInWater,bInDeepWater;
	INT8		bDirection;
	UINT8		ubBestAttackAction = AI_ACTION_NONE;
	INT8		bCanAttack;
	INT8		bWeaponIn;
	BOOLEAN		fClimb;
	INT32		iDummy;

	DebugMsg(TOPIC_JA2,DBG_LEVEL_3,String("DecideActionBlack: soldier = %d, orders = %d, attitude = %d",pSoldier->ubID,pSoldier->aiData.bOrders,pSoldier->aiData.bAttitude));
	DebugAI(AI_MSG_START, pSoldier, String("[Black Zombie]"));
	DebugAI(AI_MSG_INFO, pSoldier, String("AP=%d/%d %s %s %s %s %s", pSoldier->bActionPoints, pSoldier->bInitialActionPoints, gStr8AlertStatus[pSoldier->aiData.bAlertStatus], gStr8Orders[pSoldier->aiData.bOrders], gStr8Attitude[pSoldier->aiData.bAttitude], gStr8Team[pSoldier->bTeam], gStr8Class[pSoldier->ubSoldierClass]));
	DebugAI(AI_MSG_INFO, pSoldier, String("Health %d/%d Breath %d/%d Shock %d Tolerance %d AI Morale %d", pSoldier->stats.bLife, pSoldier->stats.bLifeMax, pSoldier->bBreath, pSoldier->bBreathMax, pSoldier->aiData.bShock, CalcSuppressionTolerance(pSoldier), pSoldier->aiData.bAIMorale));

	ATTACKTYPE BestStab, BestAttack;
	BOOLEAN fAllowCoverCheck = FALSE;

	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack");

	// if we have absolutely no action points, we can't do a thing under BLACK!
	if (!pSoldier->bActionPoints)
	{
		pSoldier->aiData.usActionData = NOWHERE;
		return(AI_ACTION_NONE);
	}

	// can this guy move to any of the neighbouring squares ? (sets TRUE/FALSE)
	ubCanMove = (pSoldier->bActionPoints >= MinPtsToMove(pSoldier));

	// determine if we happen to be in water (in which case we're in BIG trouble!)
	bInWater = Water( pSoldier->sGridNo, pSoldier->pathing.bLevel );
	bInDeepWater = WaterTooDeepForAttacks( pSoldier->sGridNo, pSoldier->pathing.bLevel );

	// calculate our morale
	pSoldier->aiData.bAIMorale = CalcMorale(pSoldier);

	////////////////////////////////////////////////////////////////////////////
	// STUCK IN WATER OR GAS, NO COVER, GO TO NEAREST SPOT OF UNGASSED LAND
	////////////////////////////////////////////////////////////////////////////

	// if soldier in water/gas has enough APs left to move at least 1 square
	if ( bInDeepWater && ubCanMove)
	{
		pSoldier->aiData.usActionData = FindNearestUngassedLand(pSoldier);

		if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
		{
			return(AI_ACTION_LEAVE_WATER_GAS);
		}

		// couldn't find ANY land within 25 tiles(!), this should never happen...
		// look for best place to RUN AWAY to (farthest from the closest threat)
		pSoldier->aiData.usActionData = FindSpotMaxDistFromOpponents(pSoldier);

		if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
		{
			return(AI_ACTION_RUN_AWAY);
		}
	}


	////////////////////////////////////////////////////////////////////////////
	// SOLDIER CAN ATTACK IF NOT IN WATER/GAS AND NOT DOING SOMETHING TOO FUNKY
	////////////////////////////////////////////////////////////////////////////

	// zombie can always attack
	bCanAttack = TRUE;

	// NPCs in water/tear gas without masks are not permitted to shoot/stab/throw
	if ((pSoldier->bActionPoints < 2) || bInDeepWater)
	{
		bCanAttack = FALSE;
	}

	BestStab.ubPossible  = FALSE;	// by default, assume Stabbing isn't possible

	// if we are able attack
	if (bCanAttack)
	{
		pSoldier->bAimShotLocation = AIM_SHOT_TORSO;

		// nothing (else) to attack with so let's try hand-to-hand
		bWeaponIn = FindObj( pSoldier, NOTHING, HANDPOS, NUM_INV_SLOTS );

		if (bWeaponIn != NO_SLOT)
		{
			BestStab.bWeaponIn = bWeaponIn;
			// if it's in his holster, swap it into his hand temporarily
			if (bWeaponIn != HANDPOS)
			{
				DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionblack: swap knife into hand");
				RearrangePocket(pSoldier,HANDPOS,bWeaponIn,TEMPORARILY);
			}

			// get the minimum cost to attack by HTH
			ubMinAPCost = MinAPsToAttack(pSoldier,pSoldier->sLastTarget,DONTADDTURNCOST,0,0);
			//ScreenMsg( FONT_ORANGE, MSG_INTERFACE, L"[%d] min AP to attack %d", pSoldier->ubID, ubMinAPCost);

			// if we can afford the minimum AP cost to use HTH combat
			if (pSoldier->bActionPoints >= ubMinAPCost)
			{
				// then look around for a worthy target (which sets BestStab.ubPossible)
				CalcBestStab(pSoldier,&BestStab, FALSE);

				if (BestStab.ubPossible && pSoldier->ubSoldierClass == SOLDIER_CLASS_ZOMBIE)
				{
					//ScreenMsg( FONT_LTGREEN, MSG_INTERFACE, L"[%d] can attack %s", pSoldier->ubID, MercPtrs[BestStab.ubOpponent]->GetName());
					// now we KNOW FOR SURE that we will do something (stab, at least)
					NPCDoesAct(pSoldier);
					ubBestAttackAction = AI_ACTION_KNIFE_MOVE;
					DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"NPC decided to move to stab");
				}
			}

			// if it was in his holster, swap it back into his holster for now
			if (bWeaponIn != HANDPOS)
			{
				DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionblack: about to put knife away");
				RearrangePocket(pSoldier,HANDPOS,bWeaponIn,TEMPORARILY);
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// CHOOSE THE BEST TYPE OF ATTACK OUT OF THOSE FOUND TO BE POSSIBLE
		//////////////////////////////////////////////////////////////////////////
		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"CHOOSE THE BEST TYPE OF ATTACK OUT OF THOSE FOUND TO BE POSSIBLE");

		// copy the information on the best action selected into BestAttack struct
		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"copy the information on the best action selected into BestAttack struct");
		switch (ubBestAttackAction)
		{
		case AI_ACTION_KNIFE_MOVE:
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack: best attack = stab");
			memcpy(&BestAttack,&BestStab,sizeof(BestAttack));
			break;

		default:
			// set to empty
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack: best attack = no good attack");
			memset( &BestAttack, 0, sizeof( BestAttack ) );
			break;
		}
	}

	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,String("DecideActionBlack: is attack still desirable?  ubBestAttackAction = %d",ubBestAttackAction));

	// if attack is still desirable (meaning it's also preferred to taking cover)
	if (ubBestAttackAction != AI_ACTION_NONE)
	{
		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"DecideActionBlack: attack is still desirable (meaning it's also preferred to taking cover)");

		pSoldier->aiData.bAimTime = BestAttack.ubAimTime;

		// sevenfm: dynamically decide stab location
		if( BestAttack.ubOpponent != NOBODY )
		{
			UINT32	uiRoll;
			UINT8	ubChanceHead = 0;
			UINT8	ubRealCTH = (UINT8)BestAttack.ubChanceToReallyHit;

			// attack to head randomly
			if( gAnimControl[ MercPtrs[BestAttack.ubOpponent]->usAnimState ].ubEndHeight != ANIM_PRONE )
			{	
				ubChanceHead = 6;

				/*if( HAS_SKILL_TRAIT(pSoldier, MARTIAL_ARTS_NT))
				{
				ubChanceHead += 5 * NUM_SKILL_TRAITS(pSoldier, MARTIAL_ARTS_NT);
				}*/

				ubChanceHead += ubRealCTH / 2;

				ubChanceHead = ubChanceHead * ubRealCTH / 100;
			}

			// randomly decide hit location
			uiRoll = PreRandom( 100 );					

			if (uiRoll > 100 - ubChanceHead)
			{
				pSoldier->bAimShotLocation = AIM_SHOT_HEAD;
			}
			else
			{
				pSoldier->bAimShotLocation = AIM_SHOT_TORSO;
			}

			/*switch (pSoldier->bAimShotLocation)
			{
			case AIM_SHOT_RANDOM:
			ScreenMsg(FONT_LTGREEN, MSG_INTERFACE, L"[%d] stabs at %s RANDOM CTH %d", pSoldier->ubID, MercPtrs[BestAttack.ubOpponent]->GetName(), BestAttack.ubChanceToReallyHit);
			break;
			case AIM_SHOT_HEAD:
			ScreenMsg(FONT_LTGREEN, MSG_INTERFACE, L"[%d] stabs at %s HEAD CTH %d", pSoldier->ubID, MercPtrs[BestAttack.ubOpponent]->GetName(), BestAttack.ubChanceToReallyHit);
			break;
			case AIM_SHOT_TORSO:
			ScreenMsg(FONT_LTGREEN, MSG_INTERFACE, L"[%d] stabs at %s TORSO CTH %d", pSoldier->ubID, MercPtrs[BestAttack.ubOpponent]->GetName(), BestAttack.ubChanceToReallyHit);
			break;
			case AIM_SHOT_LEGS:
			ScreenMsg(FONT_LTGREEN, MSG_INTERFACE, L"[%d] stabs at %s LEGS CTH %d", pSoldier->ubID, MercPtrs[BestAttack.ubOpponent]->GetName(), BestAttack.ubChanceToReallyHit);
			break;
			case AIM_SHOT_GLAND: 
			ScreenMsg(FONT_LTGREEN, MSG_INTERFACE, L"[%d] stabs at %s gland! CTH %d", pSoldier->ubID, MercPtrs[BestAttack.ubOpponent]->GetName(), BestAttack.ubChanceToReallyHit);
			break;
			default:
			ScreenMsg(FONT_LTGREEN, MSG_INTERFACE, L"[%d] stabs at %s unknown! CTH %d", pSoldier->ubID, MercPtrs[BestAttack.ubOpponent]->GetName(), BestAttack.ubChanceToReallyHit);
			}*/
		}

		// swap weapon to hand if necessary
		if (BestAttack.bWeaponIn != HANDPOS)
		{
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionblack: swap weapon into hand");
			RearrangePocket(pSoldier,HANDPOS,BestAttack.bWeaponIn,FOREVER);
		}

		pSoldier->aiData.usActionData = BestAttack.sTarget;
		pSoldier->bTargetLevel = BestAttack.bTargetLevel;

		//ScreenMsg( FONT_LTRED, MSG_INTERFACE, L"[%d] attacks %s", pSoldier->ubID, MercPtrs[BestAttack.ubOpponent]->GetName());
		return(ubBestAttackAction);
	}

	if( ubCanMove &&
		!gfHiddenInterrupt &&
		pSoldier->bActionPoints > APBPConstants[MAX_AP_CARRIED] )
	{
		sClosestDisturbance = ClosestReachableDisturbance(pSoldier, &fClimb);
		//ScreenMsg(FONT_ORANGE, MSG_INTERFACE, L"[%d] cannot find disturbance!", pSoldier->ubID);

		// if there is an opponent reachable					
		if (!TileIsOutOfBounds(sClosestDisturbance))
		{
			//ScreenMsg(FONT_ORANGE, MSG_INTERFACE, L"[%d] found disturbance %d", pSoldier->ubID, sClosestDisturbance);
			//BeginMultiPurposeLocator(sClosestDisturbance, pSoldier->pathing.bLevel, FALSE);
			DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"decideactionred: seek opponent");
			//////////////////////////////////////////////////////////////////////
			// SEEK CLOSEST DISTURBANCE: GO DIRECTLY TOWARDS CLOSEST KNOWN OPPONENT
			//////////////////////////////////////////////////////////////////////

			if (fClimb && pSoldier->sGridNo == sClosestDisturbance)
			{
				// wait for next turn if turnbased (to climb with max APs)
				if( gfTurnBasedAI &&
					pSoldier->bActionPoints < pSoldier->bInitialActionPoints )
				{
					pSoldier->aiData.bNextAction = AI_ACTION_NONE;
					return( AI_ACTION_END_TURN );
				}

				// climb
				if (IsActionAffordable(pSoldier, AI_ACTION_CLIMB_ROOF))
				{
					pSoldier->aiData.bNextAction = AI_ACTION_NONE;
					return( AI_ACTION_CLIMB_ROOF );
				}
				// cannot climb at all
			}

			// try to move towards him
			pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(pSoldier, sClosestDisturbance, 0, AI_ACTION_SEEK_OPPONENT, 0);

			// if it's possible
			if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
			{
				return(AI_ACTION_SEEK_OPPONENT);
			}
		}

		// if cannot seek or help - hide
		if( pSoldier->bActionPoints == pSoldier->bInitialActionPoints &&
			(pSoldier->aiData.bUnderFire) )
		{
			pSoldier->aiData.usActionData = FindBestNearbyCover(pSoldier,pSoldier->aiData.bAIMorale,&iDummy);
			if( !TileIsOutOfBounds(pSoldier->aiData.usActionData) )
			{
				return AI_ACTION_TAKE_COVER;
			}
		}
	}

	// get the location of the closest reachable opponent
	/*INT32	targetGridNo = -1;
	INT8	targetbLevel =  0;
	sClosestOpponent = ClosestSeenOpponentWithRoof(pSoldier, &targetGridNo, &targetbLevel);

	if( pSoldier->ubSoldierClass == SOLDIER_CLASS_ZOMBIE &&
	!gfHiddenInterrupt )
	{
	// sevenfm: not in hidden interrupt
	if( !TileIsOutOfBounds(sClosestOpponent) &&
	! ( (targetbLevel != pSoldier->pathing.bLevel) && SameBuilding( pSoldier->sGridNo, targetGridNo ) ) )
	{
	//////////////////////////////////////////////////////////////////////
	// GO DIRECTLY TOWARDS CLOSEST KNOWN OPPONENT
	//////////////////////////////////////////////////////////////////////

	// try to move towards him
	pSoldier->aiData.usActionData = GoAsFarAsPossibleTowards(pSoldier,sClosestOpponent,AI_ACTION_GET_CLOSER);

	// if it's possible			
	if (!TileIsOutOfBounds(sClosestOpponent))
	{
	if (targetbLevel != pSoldier->pathing.bLevel && gGameExternalOptions.fZombieCanClimb )
	return(AI_ACTION_MOVE_TO_CLIMB);
	else
	{
	// Flugente: if on the same level and there is a jumpable window here, jump through it
	if ( gGameExternalOptions.fZombieCanJumpWindows && targetbLevel == pSoldier->pathing.bLevel && targetbLevel == 0 )
	{
	// determine if there is a jumpable window in the direction to our target
	// if yes, and we are not facing it, face it now
	// if yes, and we are facing it, jump
	// if no, go on, nothing to see here
	// determine direction of our target
	INT8 targetdirection = (INT8)GetDirectionToGridNoFromGridNo(pSoldier->sGridNo, sClosestOpponent);

	// determine if there is a jumpable window here, in the direction of our target
	// store old direction for this check
	INT tmpdirection = pSoldier->ubDirection;
	pSoldier->ubDirection = targetdirection;

	INT8 windowdirection = DIRECTION_IRRELEVANT;
	if ( FindWindowJumpDirection(pSoldier, pSoldier->sGridNo, pSoldier->ubDirection, &windowdirection) && targetdirection == windowdirection )
	{
	pSoldier->ubDirection = tmpdirection;

	// are we already looking in that direction?
	if ( pSoldier->ubDirection == targetdirection )
	{
	// jump through the window
	return(AI_ACTION_JUMP_WINDOW);
	}
	else
	{
	// look into that direction
	if ( pSoldier->InternalIsValidStance( targetdirection, gAnimControl[ pSoldier->usAnimState ].ubEndHeight ) )
	{
	pSoldier->aiData.usActionData = targetdirection;
	return(AI_ACTION_CHANGE_FACING);
	}

	}
	}

	pSoldier->ubDirection = tmpdirection;
	}

	return(AI_ACTION_SEEK_OPPONENT);
	}
	}
	}
	// The situation mentioned above happens...
	else if ( (targetbLevel != pSoldier->pathing.bLevel) && SameBuilding( pSoldier->sGridNo, targetGridNo ) )
	{
	if ( gGameExternalOptions.fZombieCanClimb )
	{
	if (!TileIsOutOfBounds(targetGridNo) )
	{
	// need to climb AND have enough APs to get there this turn
	BOOLEAN fUp = TRUE;
	if (pSoldier->pathing.bLevel > 0 )
	fUp = FALSE;

	if ( pSoldier->bActionPoints > GetAPsToClimbRoof ( pSoldier, fUp ) )
	{
	pSoldier->aiData.usActionData = targetGridNo;//FindClosestClimbPoint(pSoldier, fUp );

	// Necessary test: can we climb up at this position? It might happen that our target is directly above us, then we'll have to move
	INT8 newdirection;
	if ( ( fUp && FindHeigherLevel( pSoldier, pSoldier->sGridNo, pSoldier->ubDirection, &newdirection ) ) || ( !fUp && FindLowerLevel( pSoldier, pSoldier->sGridNo, pSoldier->ubDirection, &newdirection ) ) )
	{							
	return( AI_ACTION_CLIMB_ROOF );
	}
	else
	{
	return(AI_ACTION_SEEK_OPPONENT);
	}
	}
	}	
	}
	}
	}*/

	////////////////////////////////////////////////////////////////////////////
	// TURN TO FACE CLOSEST KNOWN OPPONENT (IF NOT FACING THERE ALREADY)
	////////////////////////////////////////////////////////////////////////////

	if( GetAPsToLook( pSoldier ) <= pSoldier->bActionPoints &&
		!gfHiddenInterrupt )
	{
		sClosestOpponent = ClosestSeenOpponent(pSoldier, NULL, NULL);
		// if we have a closest reachable opponent			
		if (!TileIsOutOfBounds(sClosestOpponent))
		{
			bDirection = atan8(CenterX(pSoldier->sGridNo),CenterY(pSoldier->sGridNo),CenterX(sClosestOpponent),CenterY(sClosestOpponent));

			// if we're not facing towards him
			if( pSoldier->ubDirection != bDirection && 
				pSoldier->InternalIsValidStance( bDirection, gAnimControl[ pSoldier->usAnimState ].ubEndHeight ) )
			{
				pSoldier->aiData.usActionData = bDirection;

				return(AI_ACTION_CHANGE_FACING);
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// DO NOTHING: Not enough points left to move, so save them for next turn
	////////////////////////////////////////////////////////////////////////////

	// by default, if everything else fails, just stand in place and wait
	pSoldier->aiData.usActionData = NOWHERE;
	return(AI_ACTION_NONE);
}

INT8 ZombieDecideAction( SOLDIERTYPE *pSoldier )
{
	INT8 bAction = AI_ACTION_NONE;

	switch (pSoldier->aiData.bAlertStatus)
	{
	case STATUS_GREEN:
		bAction = ZombieDecideActionGreen(pSoldier);
		break;

	case STATUS_YELLOW:
		bAction = ZombieDecideActionYellow(pSoldier);
		break;

	case STATUS_RED:
		bAction = ZombieDecideActionRed(pSoldier);
		break;

	case STATUS_BLACK:
		bAction = ZombieDecideActionBlack(pSoldier);
		break;
	}

	return(bAction);
}

void ZombieDecideAlertStatus( SOLDIERTYPE *pSoldier )
{
	INT8	bOldStatus;
	INT32	iDummy;
	BOOLEAN	fClimbDummy, fReachableDummy;

	// THE FOUR (4) POSSIBLE ALERT STATUSES ARE:
	// GREEN - No one sensed, no suspicious noise heard, go about doing regular stuff
	// YELLOW - Suspicious noise was heard personally
	// RED - Either saw OPPONENTS in person, or definite contact had been called
	// BLACK - Currently has one or more OPPONENTS in sight

	// save the man's previous status
	bOldStatus = pSoldier->aiData.bAlertStatus;

	// determine the current alert status for this category of man
	if (pSoldier->aiData.bOppCnt > 0)		// opponent(s) in sight
	{		
		pSoldier->aiData.bAlertStatus = STATUS_BLACK;
	}
	else // no opponents are in sight
	{
		switch (bOldStatus)
		{
		case STATUS_BLACK:
			// then drop back to RED status
			pSoldier->aiData.bAlertStatus = STATUS_RED;
			break;

		case STATUS_RED:
			// RED can never go back down below RED, only up to BLACK
			// TODO: perhaps zombies might forget their target?	They are pretty dumb after all...
			break;

		case STATUS_YELLOW:
			// if all enemies have been RED alerted, or we're under fire
			if (gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition || pSoldier->aiData.bUnderFire)
			{
				pSoldier->aiData.bAlertStatus = STATUS_RED;
			}
			else
			{
				// if we are NOT aware of any uninvestigated noises right now
				// and we are not currently in the middle of an action
				// (could still be on his way heading to investigate a noise!)					
				if (( TileIsOutOfBounds(MostImportantNoiseHeard(pSoldier,&iDummy,&fClimbDummy,&fReachableDummy))) && !pSoldier->aiData.bActionInProgress)
				{
					// then drop back to GREEN status
					pSoldier->aiData.bAlertStatus = STATUS_GREEN;
				}
			}
			break;

		case STATUS_GREEN:
			// if all enemies have been RED alerted, or we're under fire
			if (gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition || pSoldier->aiData.bUnderFire)
			{
				pSoldier->aiData.bAlertStatus = STATUS_RED;
			}
			else
			{
				// if we ARE aware of any uninvestigated noises right now					
				if ( !TileIsOutOfBounds(MostImportantNoiseHeard(pSoldier,&iDummy,&fClimbDummy,&fReachableDummy)))
				{
					// then move up to YELLOW status
					pSoldier->aiData.bAlertStatus = STATUS_YELLOW;
				}
			}
			break;
		}
		// otherwise, RED stays RED, YELLOW stays YELLOW, GREEN stays GREEN
	}

	// if the creatures alert status has changed in any way
	if (pSoldier->aiData.bAlertStatus != bOldStatus)
	{
		// HERE ARE TRYING TO AVOID NPCs SHUFFLING BACK & FORTH BETWEEN RED & BLACK
		// if either status is < RED (ie. anything but RED->BLACK && BLACK->RED)
		if ((bOldStatus < STATUS_RED) || (pSoldier->aiData.bAlertStatus < STATUS_RED))
		{
			// force a NEW action decision on next pass through HandleManAI()
			SetNewSituation( pSoldier );
		}

		// if this guy JUST discovered that there were opponents here for sure...
		if ((bOldStatus < STATUS_RED) && (pSoldier->aiData.bAlertStatus >= STATUS_RED))
		{
			// might want to make custom to let them go anywhere
			CheckForChangingOrders(pSoldier);
		}
	}
	else	// status didn't change
	{
		// if a guy on status GREEN or YELLOW is running low on breath
		if (((pSoldier->aiData.bAlertStatus == STATUS_GREEN)	&& (pSoldier->bBreath < 75)) ||
			((pSoldier->aiData.bAlertStatus == STATUS_YELLOW) && (pSoldier->bBreath < 50)))
		{
			// as long as he's not in water (standing on a bridge is OK)
			if (!pSoldier->MercInWater())
			{
				// force a NEW decision so that he can get some rest
				SetNewSituation( pSoldier );

				// current action will be canceled. if noise is no longer important				
				if ((pSoldier->aiData.bAlertStatus == STATUS_YELLOW) &&
					(TileIsOutOfBounds(MostImportantNoiseHeard(pSoldier,&iDummy,&fClimbDummy,&fReachableDummy))))
				{
					// then drop back to GREEN status
					pSoldier->aiData.bAlertStatus = STATUS_GREEN;
					CheckForChangingOrders(pSoldier);
				}
			}
		}
	}
}

INT8 DecideStartFlanking(SOLDIERTYPE *pSoldier, INT32 sClosestDisturbance, BOOLEAN fAbortSeek)
{
	UINT8 ubNearbyFireteamClose = 0;
	UINT8 ubNearbyFireteamSupport = 0;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier ||
			!pFriend->bActive || !pFriend->bInSector ||
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			!AISameFireteam(pSoldier, pFriend))
		{
			continue;
		}

		INT32 iDistance = PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo);
		if (iDistance <= DAY_VISION_RANGE / 4)
			++ubNearbyFireteamClose;
		if (iDistance <= DAY_VISION_RANGE / 2)
			++ubNearbyFireteamSupport;
	}

	if (pSoldier->numFlanks == 0 &&
		pSoldier->bActionPoints >= APBPConstants[AP_MINIMUM] &&
		pSoldier->CheckInitialAP() &&
		(pSoldier->aiData.bAttitude == CUNNINGAID || pSoldier->aiData.bAttitude == CUNNINGSOLO ||
		(pSoldier->aiData.bAttitude == BRAVESOLO || pSoldier->aiData.bAttitude == BRAVEAID) && ubNearbyFireteamClose > 2) &&
		AICombatTeam(pSoldier) &&
		!AIShouldAvoidAdvance(pSoldier) &&
		AIAllowsIndependentFlank(pSoldier) &&
		pSoldier->ubSoldierClass != SOLDIER_CLASS_ADMINISTRATOR &&
		!AICheckSpecialRole(pSoldier) &&		
		gAnimControl[pSoldier->usAnimState].ubHeight != ANIM_PRONE &&
		(!pSoldier->aiData.bUnderFire ||
		 (fAbortSeek &&
		  ubNearbyFireteamSupport >= 2 &&
		  (InSmokeNearby(pSoldier->sGridNo, pSoldier->pathing.bLevel) ||
		   SightCoverAtSpot(pSoldier, pSoldier->sGridNo, FALSE)))) &&
		pSoldier->pathing.bLevel == 0 &&
		!Water(pSoldier->sGridNo, pSoldier->pathing.bLevel) &&
		(pSoldier->aiData.bOrders == SEEKENEMY || pSoldier->aiData.bOrders == FARPATROL || pSoldier->aiData.bOrders == CLOSEPATROL && NightTime()) &&
		pSoldier->bActionPoints >= APBPConstants[AP_MINIMUM] &&
		PythSpacesAway(pSoldier->sGridNo, sClosestDisturbance) > MIN_FLANK_DIST &&
		(PythSpacesAway(pSoldier->sGridNo, sClosestDisturbance) < MAX_FLANK_DIST || fAbortSeek) &&
		(!GuySawEnemy(pSoldier) || ubNearbyFireteamClose > 2 || fAbortSeek) &&
		(fAbortSeek || CountFriendsBetweenMeAndSpotFromSpot(pSoldier, sClosestDisturbance) > 0 || NightTime() || ubNearbyFireteamClose > 2))
	{
		// Dynamic role deconfliction: a rifleman/marksman in a strong support posture
		// should not abandon the fire base merely because flanking is otherwise legal.
		// Require a clear support advantage and nearby teammates before suppressing the flank.
		if (ubNearbyFireteamSupport >= 2 &&
			AISupportRoleScore(pSoldier, sClosestDisturbance) >
			AIManeuverRoleScore(pSoldier, sClosestDisturbance) + 15)
		{
			return -1;
		}

		UINT8 ubFriends, ubFriendsLeft, ubFriendsRight;
		UINT8 ubDirection = AIDirection(sClosestDisturbance, pSoldier->sGridNo);

		ubFriends = CountFriendsInDirectionFromSpot(pSoldier, sClosestDisturbance, ubDirection, VISION_RANGE * 2);
		ubFriendsLeft = CountFriendsInDirectionFromSpot(pSoldier, sClosestDisturbance, gOneCDirection[ubDirection], VISION_RANGE * 2) +
			CountFriendsInDirectionFromSpot(pSoldier, sClosestDisturbance, gTwoCDirection[ubDirection], VISION_RANGE * 2);
		ubFriendsRight = CountFriendsInDirectionFromSpot(pSoldier, sClosestDisturbance, gOneCCDirection[ubDirection], VISION_RANGE * 2) +
			CountFriendsInDirectionFromSpot(pSoldier, sClosestDisturbance, gTwoCCDirection[ubDirection], VISION_RANGE * 2);

		UINT8 ubActiveLeftFlankers = 0;
		UINT8 ubActiveRightFlankers = 0;
		for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
			iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
		{
			SOLDIERTYPE *pFriend = MercPtrs[iCounter];
			if (!pFriend || pFriend == pSoldier || !pFriend->bActive || !pFriend->bInSector ||
				!AISameFireteam(pSoldier, pFriend) ||
				pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || pFriend->bBreathCollapsed ||
				(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
				!pFriend->IsFlanking() ||
				TileIsOutOfBounds(pFriend->lastFlankSpot) ||
				PythSpacesAway(pFriend->lastFlankSpot, sClosestDisturbance) > TACTICAL_RANGE / 2)
			{
				continue;
			}

			if (pFriend->flags.lastFlankLeft)
				++ubActiveLeftFlankers;
			else
				++ubActiveRightFlankers;
		}
		UINT8 ubActiveFlankers = ubActiveLeftFlankers + ubActiveRightFlankers;
		UINT8 ubFlankLimit = 2;

		if (AIFireteamCombatReadyCount(pSoldier) >= 7 &&
			AICheckWeOutnumberLocal(pSoldier, sClosestDisturbance) &&
			AILocalStress(pSoldier) < 25)
		{
			ubFlankLimit = 3;
		}

		// Keep a genuine support base. Most elements commit only two flankers;
		// a large, locally superior and composed element may commit a third.
		if (ubActiveFlankers >= ubFlankLimit)
			return -1;

		BOOLEAN fLeftFlankPossible = FALSE;
		BOOLEAN fRightFlankPossible = FALSE;

		if (ubFriendsLeft < ubFriends)
		{
			fLeftFlankPossible = TRUE;
		}

		if (ubFriendsRight < ubFriends)
		{
			fRightFlankPossible = TRUE;
		}

		INT32 sFlankingSpot = NOWHERE;
		INT8 bAction = AI_ACTION_NONE;

		// decide flanking direction
		if (fLeftFlankPossible && !fRightFlankPossible)
		{
			bAction = AI_ACTION_FLANK_LEFT;
		}
		else if (fRightFlankPossible && !fLeftFlankPossible)
		{
			bAction = AI_ACTION_FLANK_RIGHT;
		}
		else if (fLeftFlankPossible && fRightFlankPossible)
		{
			// Deconflict flank commitments first. If both sides are equally committed,
			// prefer the side with fewer friendly bodies already occupying that arc.
			if (ubActiveLeftFlankers < ubActiveRightFlankers)
				bAction = AI_ACTION_FLANK_LEFT;
			else if (ubActiveRightFlankers < ubActiveLeftFlankers)
				bAction = AI_ACTION_FLANK_RIGHT;
			else if (ubFriendsLeft < ubFriendsRight)
				bAction = AI_ACTION_FLANK_LEFT;
			else if (ubFriendsRight < ubFriendsLeft)
				bAction = AI_ACTION_FLANK_RIGHT;
			else if (Random(6) < 3)
				bAction = AI_ACTION_FLANK_LEFT;
			else
				bAction = AI_ACTION_FLANK_RIGHT;
		}

		// if left or right flanking is possible, search for flanking spot
		if (bAction != AI_ACTION_NONE)
		{
			pSoldier->aiData.usActionData = FindFlankingSpot(pSoldier, sClosestDisturbance, bAction);

			if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))
			{
				if (bAction == AI_ACTION_FLANK_LEFT)
					pSoldier->flags.lastFlankLeft = TRUE;
				else
					pSoldier->flags.lastFlankLeft = FALSE;

				//if ( pSoldier->lastFlankSpot != sClosestDisturbance)
				//pSoldier->numFlanks=0;

				pSoldier->origDir = GetDirectionFromGridNo(sClosestDisturbance, pSoldier);
				pSoldier->lastFlankSpot = sClosestDisturbance;
				pSoldier->numFlanks++;

				// sevenfm: change orders when starting to flank
				if (pSoldier->aiData.bOrders == CLOSEPATROL)
				{
					pSoldier->aiData.bOrders = FARPATROL;
				}

				return(bAction);
			}
		}
	}

	return -1;
}

static UINT32 guiAITacticalVariationTurn[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAITacticalVariationIdentity[MAX_NUM_SOLDIERS] = { 0 };
static INT8 gbAITacticalSeekBias[MAX_NUM_SOLDIERS] = { 0 };
static INT8 gbAITacticalHelpBias[MAX_NUM_SOLDIERS] = { 0 };
static INT8 gbAITacticalHideBias[MAX_NUM_SOLDIERS] = { 0 };
static INT8 gbAITacticalWatchBias[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAITacticalVariationLastTurnStamp = 0;

static void AIApplyTacticalPreferenceVariation(SOLDIERTYPE *pSoldier,
	INT8 &bSeekPts, INT8 &bHelpPts, INT8 &bHidePts, INT8 &bWatchPts)
{
	UINT32 uiCurrentTurnStamp = guiTurnCnt + 1;
	if (guiAITacticalVariationLastTurnStamp != 0 &&
		uiCurrentTurnStamp < guiAITacticalVariationLastTurnStamp)
	{
		for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
		{
			guiAITacticalVariationTurn[i] = 0;
			guiAITacticalVariationIdentity[i] = 0;
			gbAITacticalSeekBias[i] = 0;
			gbAITacticalHelpBias[i] = 0;
			gbAITacticalHideBias[i] = 0;
			gbAITacticalWatchBias[i] = 0;
		}
	}
	guiAITacticalVariationLastTurnStamp = uiCurrentTurnStamp;

	if (!AICombatTeam(pSoldier) || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	// Once a soldier is genuinely breaking contact or personally overwhelmed,
	// keep survival behaviour deterministic. Variability is only for choosing
	// among tactically reasonable options while the unit is still functioning.
	if (AIEscapeActive(pSoldier) ||
		AIDisengagementActive(pSoldier) ||
		AILocalStress(pSoldier) >= 70 ||
		AIPersonalRisk(pSoldier) > AIPersonalRiskTolerance(pSoldier) + 15)
	{
		return;
	}

	UINT8 ubID = pSoldier->ubID;
	UINT32 uiTurnStamp = guiTurnCnt + 1;

	if (guiAITacticalVariationIdentity[ubID] != pSoldier->uiUniqueSoldierIdValue ||
		guiAITacticalVariationTurn[ubID] != uiTurnStamp)
	{
		guiAITacticalVariationIdentity[ubID] = pSoldier->uiUniqueSoldierIdValue;
		guiAITacticalVariationTurn[ubID] = uiTurnStamp;

		gbAITacticalSeekBias[ubID] = 0;
		gbAITacticalHelpBias[ubID] = 0;
		gbAITacticalHideBias[ubID] = 0;
		gbAITacticalWatchBias[ubID] = 0;

		// One bounded tactical inclination per turn. Existing morale, orders,
		// personality, danger and hard safety checks remain more important.
		switch (PreRandom(7))
		{
		case 0: // push
			gbAITacticalSeekBias[ubID] = 2;
			gbAITacticalHideBias[ubID] = -1;
			break;
		case 1: // cautious hold
			gbAITacticalSeekBias[ubID] = -1;
			gbAITacticalHideBias[ubID] = 2;
			break;
		case 2: // overwatch
			gbAITacticalSeekBias[ubID] = -1;
			gbAITacticalWatchBias[ubID] = 2;
			break;
		case 3: // support
			gbAITacticalHelpBias[ubID] = 2;
			gbAITacticalSeekBias[ubID] = -1;
			break;
		case 4: // active defence
			gbAITacticalHideBias[ubID] = 1;
			gbAITacticalWatchBias[ubID] = 1;
			break;
		case 5: // manoeuvre
			gbAITacticalSeekBias[ubID] = 1;
			gbAITacticalWatchBias[ubID] = 1;
			break;
		default: // balanced
			break;
		}

		// Under direct fire the random layer must never manufacture reckless
		// aggression; safety logic still decides whether movement is acceptable.
		if (pSoldier->aiData.bUnderFire && gbAITacticalSeekBias[ubID] > 0)
			gbAITacticalSeekBias[ubID] = 0;
	}

	if (bSeekPts > -90) bSeekPts += gbAITacticalSeekBias[ubID];
	if (bHelpPts > -90) bHelpPts += gbAITacticalHelpBias[ubID];
	if (bHidePts > -90) bHidePts += gbAITacticalHideBias[ubID];
	if (bWatchPts > -90) bWatchPts += gbAITacticalWatchBias[ubID];
}

void PrepareMainRedAIWeights(SOLDIERTYPE *pSoldier, INT8 &bSeekPts, INT8 &bHelpPts, INT8 &bHidePts, INT8 &bWatchPts)
{
	// modify RED movement tendencies according to morale
	switch (pSoldier->aiData.bAIMorale)
	{
	case MORALE_HOPELESS:  bSeekPts = -99; bHelpPts = -99; bHidePts = +2; bWatchPts = -99; break;
	case MORALE_WORRIED:   bSeekPts += -2; bHelpPts += 0; bHidePts += +2; bWatchPts += 1; break;
	case MORALE_NORMAL:    bSeekPts += 0; bHelpPts += 0; bHidePts += 0; bWatchPts += 0; break;
	case MORALE_CONFIDENT: bSeekPts += +1; bHelpPts += 0; bHidePts += -1; bWatchPts += 0; break;
	case MORALE_FEARLESS:  bSeekPts += +1; bHelpPts += 0; bHidePts = -1; bWatchPts += 0; break;
	}

	// modify tendencies according to orders
	switch (pSoldier->aiData.bOrders)
	{
	case STATIONARY:   bSeekPts += -1; bHelpPts += -1; bHidePts += +1; bWatchPts += +1; break;
	case ONGUARD:      bSeekPts += -1; bHelpPts += 0; bHidePts += +1; bWatchPts += +1; break;
	case CLOSEPATROL:  bSeekPts += 0; bHelpPts += 0; bHidePts += 0; bWatchPts += 0; break;
	case RNDPTPATROL:  bSeekPts += 0; bHelpPts += 0; bHidePts += 0; bWatchPts += 0; break;
	case POINTPATROL:  bSeekPts += 0; bHelpPts += 0; bHidePts += 0; bWatchPts += 0; break;
	case FARPATROL:    bSeekPts += 0; bHelpPts += 0; bHidePts += 0; bWatchPts += 0; break;
	case ONCALL:       bSeekPts += 0; bHelpPts += +1; bHidePts += -1; bWatchPts += 0; break;
	case SEEKENEMY:    bSeekPts += +1; bHelpPts += 0; bHidePts += -1; bWatchPts += -1; break;
	case SNIPER:		bSeekPts += -1; bHelpPts += 0; bHidePts += +1; bWatchPts += +1; break;
	}

	// Defensive mission persistence: reported/public contact is not enough reason
	// for guards and snipers to abandon assigned tactical positions. They become
	// alert, face the threat and improve cover, but only direct/recent contact or
	// incoming fire grants freedom to actively seek the enemy.
	if (AICombatTeam(pSoldier) &&
		!pSoldier->aiData.bUnderFire &&
		pSoldier->aiData.bOppCnt == 0 &&
		!GuySawEnemy(pSoldier, SEEN_LAST_TURN))
	{
		switch (pSoldier->aiData.bOrders)
		{
		case STATIONARY:
		case SNIPER:
			bSeekPts = -99;
			bHelpPts = -99;
			if (bHidePts > -90) bHidePts += 3;
			if (bWatchPts > -90) bWatchPts += 4;
			break;
		case ONGUARD:
			if (bSeekPts > -90) bSeekPts -= 5;
			if (bHelpPts > -90) bHelpPts -= 3;
			if (bHidePts > -90) bHidePts += 2;
			if (bWatchPts > -90) bWatchPts += 3;
			break;
		case CLOSEPATROL:
			if (bSeekPts > -90) bSeekPts -= 3;
			if (bHelpPts > -90) bHelpPts -= 1;
			if (bWatchPts > -90) bWatchPts += 2;
			break;
		default:
			break;
		}
	}

	// Formation doctrine reinforces existing map orders. Security and elite guards
	// are mission-anchored; mobile elites retain operational freedom.
	INT8 bDoctrineAnchor = AIDoctrineAnchorModifier(pSoldier);
	if (bDoctrineAnchor < 0 && bSeekPts > -90)
	{
		bSeekPts += bDoctrineAnchor;
		if (bWatchPts > -90)
			bWatchPts += __min((INT8)3, (INT8)((-bDoctrineAnchor + 1) / 2));
		if (bHidePts > -90 && bDoctrineAnchor <= -3)
			bHidePts += 1;
	}

	// modify tendencies according to attitude
	switch (pSoldier->aiData.bAttitude)
	{
	case DEFENSIVE:     bSeekPts += -1; bHelpPts += 0; bHidePts += +2; bWatchPts += +1; break;
	case BRAVESOLO:     bSeekPts += +1; bHelpPts += -1; bHidePts += -1; bWatchPts += -1; break;
	case BRAVEAID:      bSeekPts += +1; bHelpPts += +1; bHidePts += -1; bWatchPts += -1; break;
	case CUNNINGSOLO:   bSeekPts += 1; bHelpPts += -1; bHidePts += +1; bWatchPts += 0; break;
	case CUNNINGAID:    bSeekPts += 1; bHelpPts += +1; bHidePts += +1; bWatchPts += 0; break;
	case AGGRESSIVE:    bSeekPts += +1; bHelpPts += 0; bHidePts += -1; bWatchPts += 0; break;
	case ATTACKSLAYONLY:bSeekPts += +1; bHelpPts += 0; bHidePts += -1; bWatchPts += 0; break;
	}

	// Battlefield odds affect willingness to seek contact before normal support/range
	// preferences are applied. Hopeless survivors do not initiate another advance.
	if (AICombatTeam(pSoldier) && bSeekPts > -90)
	{
		INT8 bOddsModifier = AIHopelessOddsModifier(pSoldier);
		if (AIShouldAvoidAdvance(pSoldier))
		{
			bSeekPts = -99;
			if (bHidePts > -90) bHidePts += 3;
			if (bWatchPts > -90) bWatchPts += 2;
		}
		else
		{
			bSeekPts += bOddsModifier;
			if (bOddsModifier < 0 && bHidePts > -90) bHidePts += 1;
		}
	}

	// Local cooperation: advancing with nearby support is desirable; isolated
	// advances are discouraged.  This changes preference rather than forbidding
	// movement, so brave/aggressive soldiers can still push when circumstances justify it.
	if (AICombatTeam(pSoldier) && bSeekPts > -90)
	{
		INT8 bSupportModifier = AIAdvanceSupportModifier(pSoldier);
		bSeekPts += bSupportModifier;

		if (bSupportModifier < 0)
		{
			if (bHidePts > -90)
				bHidePts += 1;
			if (bWatchPts > -90)
				bWatchPts += 1;
		}

		// Weapon/optic role now influences movement. Scoped rifles and marksmen stop
		// treating every known opponent as a reason to close distance.
		INT8 bRangeModifier = AIEngagementRangeModifier(pSoldier);
		bSeekPts += bRangeModifier;
		if (bRangeModifier < 0 && bWatchPts > -90)
			bWatchPts += 1;
	}

	// Dynamic specialist posture. When this soldier is materially better suited
	// to supporting the element than maneuvering, make holding/observing somewhat
	// more attractive. This remains a soft preference: SEEK stays available, and
	// incoming fire cancels the bias so specialists can react normally.
	if (AICombatTeam(pSoldier) &&
		!pSoldier->aiData.bUnderFire &&
		AICheckSpecialRole(pSoldier) &&
		bSeekPts > -90)
	{
		INT32 sRoleTarget = ClosestKnownOpponent(pSoldier, NULL, NULL);
		INT32 iSupportScore = AISupportRoleScore(pSoldier, sRoleTarget);
		INT32 iManeuverScore = AIManeuverRoleScore(pSoldier, sRoleTarget);

		if (iSupportScore > iManeuverScore + 15)
		{
			bSeekPts -= 2;
			if (bWatchPts > -90) bWatchPts += 2;
			if (bHidePts > -90) bHidePts += 1;
		}

		if (iSupportScore > iManeuverScore + 40)
		{
			bSeekPts -= 1;
			if (bWatchPts > -90) bWatchPts += 1;
		}
	}

	// Break ties and near-ties between otherwise sensible RED choices. This is
	// deliberately applied after deterministic tactical modifiers so randomness
	// cannot resurrect actions that safety/morale/order logic disabled.
	AIApplyTacticalPreferenceVariation(pSoldier, bSeekPts, bHelpPts, bHidePts, bWatchPts);
}

INT8 DecideContinueFlanking(SOLDIERTYPE *pSoldier, INT32 sClosestDisturbance)
{
	// Loss of local command ends an uncommanded regular's complex flank; elites and
	// veteran/cunning troops can continue independently.
	if (AICombatTeam(pSoldier) && !AIAllowsIndependentFlank(pSoldier))
	{
		pSoldier->numFlanks = MAX_FLANKS_RED;
		return -1;
	}

	if (AIShouldAvoidAdvance(pSoldier))
	{
		// End the manoeuvre cleanly; ordinary RED logic can now hold/fallback instead.
		pSoldier->numFlanks = MAX_FLANKS_RED;
		return -1;
	}

	ATTACKTYPE BestThrow;	
	INT32 tempGridNo;

	if (TileIsOutOfBounds(sClosestDisturbance))
		tempGridNo = pSoldier->lastFlankSpot;
	else
		tempGridNo = sClosestDisturbance;

	// continue flanking
	if (pSoldier->IsFlanking() &&
		gAnimControl[pSoldier->usAnimState].ubHeight != ANIM_PRONE &&
		!pSoldier->aiData.bUnderFire)
	{
		DebugMsg(TOPIC_JA2, DBG_LEVEL_3, "decideactionred: continue flanking");
		INT16 currDir = GetDirectionFromGridNo(tempGridNo, pSoldier);
		INT16 origDir = pSoldier->origDir;
		pSoldier->numFlanks += 1;
		if (pSoldier->flags.lastFlankLeft)
		{
			if (origDir > currDir)
				origDir -= 8;

			// stop flanking condition
			if ((currDir - origDir) >= MinFlankDirections(pSoldier))
			{
				pSoldier->numFlanks = MAX_FLANKS_RED;
			}
			else
			{
				pSoldier->aiData.usActionData = FindFlankingSpot(pSoldier, tempGridNo, AI_ACTION_FLANK_LEFT);

				if (!TileIsOutOfBounds(pSoldier->aiData.usActionData)) //&& (currDir - origDir) < 2 )
				{
					return AI_ACTION_FLANK_LEFT;
				}
				else
				{
					// wait for next turn if turnbased
					if (gfTurnBasedAI &&
						pSoldier->bActionPoints < pSoldier->bInitialActionPoints)
					{
						DebugAI(AI_MSG_INFO, pSoldier, String("cannot flank, wait for the next turn"));
						return(AI_ACTION_END_TURN);
					}

					// check if we can cut fence here						
					if (pSoldier->pathing.bLevel == 0 &&
						!TileIsOutOfBounds(pSoldier->lastFlankSpot) &&
						pSoldier->aiData.bOrders == SEEKENEMY &&
						//WeAttack(pSoldier->bTeam) &&
						pSoldier->bActionPoints >= GetAPsToCutFence(pSoldier) + GetAPsToLook(pSoldier) &&
						FindFenceAroundSpot(pSoldier->sGridNo))
					{
						// Flanking can exploit a fence only if this soldier actually carries
						// wirecutters. Legacy Vengeance spawned an undroppable tool here.
						INT8 bWirecutterSlot = FindWirecutters(pSoldier);

						if (bWirecutterSlot != NO_SLOT)
						{
							DebugAI(AI_MSG_INFO, pSoldier, String("found wirecutter, check if we can find fence to cut"));

							UINT8 ubDir = AIDirection(pSoldier->sGridNo, pSoldier->lastFlankSpot);
							UINT8 ubDesiredDir;
							UINT8 ubCheckDir;
							INT32 sNewSpot;
							INT32 sNextSpot;

							// determine desired direction
							if (pSoldier->flags.lastFlankLeft)
								ubDesiredDir = gTwoCCDirection[ubDir];
							else
								ubDesiredDir = gTwoCDirection[ubDir];

							// cannot cut fence diagonally
							if (ubDesiredDir % 2 == 0)
							{
								ubCheckDir = ubDesiredDir;
								sNewSpot = NewGridNo(pSoldier->sGridNo, DirectionInc(ubCheckDir));
								sNextSpot = NewGridNo(sNewSpot, DirectionInc(ubCheckDir));

								if (sNewSpot != pSoldier->sGridNo &&
									sNextSpot != sNewSpot &&
									IsCuttableWireFenceAtGridNo(sNewSpot) &&
									IsLocationSittableExcludingPeople(sNextSpot, pSoldier->pathing.bLevel))
								{
									DebugAI(AI_MSG_INFO, pSoldier, String("found cuttable fence at %d", sNewSpot));
									if (pSoldier->ubDirection == ubCheckDir)
									{
										RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
										pSoldier->aiData.usActionData = sNewSpot;
										return AI_ACTION_HANDLE_ITEM;
									}
									else if (pSoldier->InternalIsValidStance(ubCheckDir, gAnimControl[pSoldier->usAnimState].ubEndHeight))
									{
										DebugAI(AI_MSG_INFO, pSoldier, String("turn before cutting fence"));
										RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
										pSoldier->aiData.usActionData = ubCheckDir;
										pSoldier->aiData.bNextAction = AI_ACTION_HANDLE_ITEM;
										pSoldier->aiData.usNextActionData = sNewSpot;
										return AI_ACTION_CHANGE_FACING;
									}
								}
							}
							// try adjacent directions
							else
							{
								ubCheckDir = gOneCDirection[ubDesiredDir];
								sNewSpot = NewGridNo(pSoldier->sGridNo, DirectionInc(ubCheckDir));
								sNextSpot = NewGridNo(sNewSpot, DirectionInc(ubCheckDir));

								if (sNewSpot != pSoldier->sGridNo &&
									sNextSpot != sNewSpot &&
									IsCuttableWireFenceAtGridNo(sNewSpot) &&
									IsLocationSittableExcludingPeople(sNextSpot, pSoldier->pathing.bLevel))
								{
									DebugAI(AI_MSG_INFO, pSoldier, String("found cuttable fence at %d", sNewSpot));
									if (pSoldier->ubDirection == ubCheckDir)
									{
										RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
										pSoldier->aiData.usActionData = sNewSpot;
										return AI_ACTION_HANDLE_ITEM;
									}
									else if (pSoldier->InternalIsValidStance(ubCheckDir, gAnimControl[pSoldier->usAnimState].ubEndHeight))
									{
										DebugAI(AI_MSG_INFO, pSoldier, String("turn before cutting fence"));
										RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
										pSoldier->aiData.usActionData = ubCheckDir;
										pSoldier->aiData.bNextAction = AI_ACTION_HANDLE_ITEM;
										pSoldier->aiData.usNextActionData = sNewSpot;
										return AI_ACTION_CHANGE_FACING;
									}
								}

								ubCheckDir = gOneCCDirection[ubDesiredDir];
								sNewSpot = NewGridNo(pSoldier->sGridNo, DirectionInc(ubCheckDir));
								sNextSpot = NewGridNo(sNewSpot, DirectionInc(ubCheckDir));

								if (sNewSpot != pSoldier->sGridNo &&
									sNextSpot != sNewSpot &&
									IsCuttableWireFenceAtGridNo(sNewSpot) &&
									IsLocationSittableExcludingPeople(sNextSpot, pSoldier->pathing.bLevel))
								{
									DebugAI(AI_MSG_INFO, pSoldier, String("found cuttable fence at %d", sNewSpot));
									if (pSoldier->ubDirection == ubCheckDir)
									{
										RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
										pSoldier->aiData.usActionData = sNewSpot;
										return AI_ACTION_HANDLE_ITEM;
									}
									else if (pSoldier->InternalIsValidStance(ubCheckDir, gAnimControl[pSoldier->usAnimState].ubEndHeight))
									{
										DebugAI(AI_MSG_INFO, pSoldier, String("turn before cutting fence"));
										RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
										pSoldier->aiData.usActionData = ubCheckDir;
										pSoldier->aiData.bNextAction = AI_ACTION_HANDLE_ITEM;
										pSoldier->aiData.usNextActionData = sNewSpot;
										return AI_ACTION_CHANGE_FACING;
									}
								}
							}
						}
					}

					if (pSoldier->aiData.bOrders == SEEKENEMY &&
						//WeAttack(pSoldier->bTeam)) //&&
						Chance(20 * CountThrowableGrenades(pSoldier, EXPLOSV_NORMAL, 10)))
					{
						CheckTossFlankFence(pSoldier, &BestThrow);

						if (BestThrow.ubPossible)
						{
							DebugAI(AI_MSG_INFO, pSoldier, String("prepare throw at spot %d level %d aimtime %d", BestThrow.sTarget, BestThrow.bTargetLevel, BestThrow.ubAimTime));

							// if necessary, swap the usItem from holster into the hand position
							if (BestThrow.bWeaponIn != HANDPOS)
							{
								DebugAI(AI_MSG_INFO, pSoldier, String("rearrange pocket"));
								RearrangePocket(pSoldier, HANDPOS, BestThrow.bWeaponIn, FOREVER);
							}

							// set grenade as delayed
							pSoldier->inv[HANDPOS][0]->data.sObjectFlag |= DELAYED_GRENADE_EXPLOSION;

							// stand up before throwing if needed
							if (gAnimControl[pSoldier->usAnimState].ubEndHeight < BestThrow.ubStance &&
								pSoldier->InternalIsValidStance(AIDirection(pSoldier->sGridNo, BestThrow.sTarget), BestThrow.ubStance))
							{
								pSoldier->aiData.usActionData = BestThrow.ubStance;
								pSoldier->aiData.bNextAction = AI_ACTION_TOSS_PROJECTILE;
								pSoldier->aiData.usNextActionData = BestThrow.sTarget;
								pSoldier->aiData.bNextTargetLevel = BestThrow.bTargetLevel;
								pSoldier->aiData.bAimTime = BestThrow.ubAimTime;
								return AI_ACTION_CHANGE_STANCE;
							}
							else
							{
								pSoldier->aiData.usActionData = BestThrow.sTarget;
								pSoldier->bTargetLevel = BestThrow.bTargetLevel;
								pSoldier->aiData.bAimTime = BestThrow.ubAimTime;
							}

							DebugAI(AI_MSG_INFO, pSoldier, String("throw grenade at spot %d level %d", BestThrow.sTarget, BestThrow.bTargetLevel));

							return(AI_ACTION_TOSS_PROJECTILE);
						}
					}

					pSoldier->numFlanks = MAX_FLANKS_RED;
				}
			}
		}
		else
		{
			if (origDir < currDir)
				origDir += 8;

			// stop flanking condition
			if ((origDir - currDir) >= MinFlankDirections(pSoldier))
			{
				pSoldier->numFlanks = MAX_FLANKS_RED;
			}
			else
			{
				pSoldier->aiData.usActionData = FindFlankingSpot(pSoldier, tempGridNo, AI_ACTION_FLANK_RIGHT);

				if (!TileIsOutOfBounds(pSoldier->aiData.usActionData))//&& (origDir - currDir) < 2 )
				{
					return AI_ACTION_FLANK_RIGHT;
				}
				else
				{
					// wait for next turn if turnbased
					if (gfTurnBasedAI &&
						pSoldier->bActionPoints < pSoldier->bInitialActionPoints)
					{
						DebugAI(AI_MSG_INFO, pSoldier, String("cannot flank, wait for the next turn"));
						return(AI_ACTION_END_TURN);
					}

					// check if we can cut fence here						
					if (pSoldier->pathing.bLevel == 0 &&
						!TileIsOutOfBounds(pSoldier->lastFlankSpot) &&
						pSoldier->aiData.bOrders == SEEKENEMY &&
						//WeAttack(pSoldier->bTeam) &&
						pSoldier->bActionPoints >= GetAPsToCutFence(pSoldier) + GetAPsToLook(pSoldier) &&
						FindFenceAroundSpot(pSoldier->sGridNo))
					{
						// Flanking can exploit a fence only if this soldier actually carries
						// wirecutters. Legacy Vengeance spawned an undroppable tool here.
						INT8 bWirecutterSlot = FindWirecutters(pSoldier);

						if (bWirecutterSlot != NO_SLOT)
						{
							DebugAI(AI_MSG_INFO, pSoldier, String("found wirecutter, check if we can find fence to cut"));

							UINT8 ubDir = AIDirection(pSoldier->sGridNo, pSoldier->lastFlankSpot);
							UINT8 ubFlankDir;
							UINT8 ubCheckDir;
							INT32 sNewSpot;
							INT32 sNextSpot;

							// determine desired direction
							if (pSoldier->flags.lastFlankLeft)
								ubFlankDir = gTwoCCDirection[ubDir];
							else
								ubFlankDir = gTwoCDirection[ubDir];

							// cannot cut fence diagonally
							if (ubFlankDir % 2 == 0)
							{
								ubCheckDir = ubFlankDir;
								sNewSpot = NewGridNo(pSoldier->sGridNo, DirectionInc(ubCheckDir));
								sNextSpot = NewGridNo(sNewSpot, DirectionInc(ubCheckDir));

								if (sNewSpot != pSoldier->sGridNo &&
									sNextSpot != sNewSpot &&
									IsCuttableWireFenceAtGridNo(sNewSpot) &&
									IsLocationSittableExcludingPeople(sNextSpot, pSoldier->pathing.bLevel))
								{
									DebugAI(AI_MSG_INFO, pSoldier, String("found cuttable fence at %d", sNewSpot));
									if (pSoldier->ubDirection == ubCheckDir)
									{
										RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
										pSoldier->aiData.usActionData = sNewSpot;
										return AI_ACTION_HANDLE_ITEM;
									}
									else if (pSoldier->InternalIsValidStance(ubCheckDir, gAnimControl[pSoldier->usAnimState].ubEndHeight))
									{
										DebugAI(AI_MSG_INFO, pSoldier, String("turn before cutting fence"));
										RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
										pSoldier->aiData.usActionData = ubCheckDir;
										pSoldier->aiData.bNextAction = AI_ACTION_HANDLE_ITEM;
										pSoldier->aiData.usNextActionData = sNewSpot;
										return AI_ACTION_CHANGE_FACING;
									}
								}
							}
							// try adjacent directions
							else
							{
								ubCheckDir = gOneCDirection[ubFlankDir];
								sNewSpot = NewGridNo(pSoldier->sGridNo, DirectionInc(ubCheckDir));
								sNextSpot = NewGridNo(sNewSpot, DirectionInc(ubCheckDir));

								if (sNewSpot != pSoldier->sGridNo &&
									sNextSpot != sNewSpot &&
									IsCuttableWireFenceAtGridNo(sNewSpot) &&
									IsLocationSittableExcludingPeople(sNextSpot, pSoldier->pathing.bLevel))
								{
									DebugAI(AI_MSG_INFO, pSoldier, String("found cuttable fence at %d", sNewSpot));
									if (pSoldier->ubDirection == ubCheckDir)
									{
										RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
										pSoldier->aiData.usActionData = sNewSpot;
										return AI_ACTION_HANDLE_ITEM;
									}
									else if (pSoldier->InternalIsValidStance(ubCheckDir, gAnimControl[pSoldier->usAnimState].ubEndHeight))
									{
										DebugAI(AI_MSG_INFO, pSoldier, String("turn before cutting fence"));
										RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
										pSoldier->aiData.usActionData = ubCheckDir;
										pSoldier->aiData.bNextAction = AI_ACTION_HANDLE_ITEM;
										pSoldier->aiData.usNextActionData = sNewSpot;
										return AI_ACTION_CHANGE_FACING;
									}
								}

								ubCheckDir = gOneCCDirection[ubFlankDir];
								sNewSpot = NewGridNo(pSoldier->sGridNo, DirectionInc(ubCheckDir));
								sNextSpot = NewGridNo(sNewSpot, DirectionInc(ubCheckDir));

								if (sNewSpot != pSoldier->sGridNo &&
									sNextSpot != sNewSpot &&
									IsCuttableWireFenceAtGridNo(sNewSpot) &&
									IsLocationSittableExcludingPeople(sNextSpot, pSoldier->pathing.bLevel))
								{
									DebugAI(AI_MSG_INFO, pSoldier, String("found cuttable fence at %d", sNewSpot));
									if (pSoldier->ubDirection == ubCheckDir)
									{
										RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
										pSoldier->aiData.usActionData = sNewSpot;
										return AI_ACTION_HANDLE_ITEM;
									}
									else if (pSoldier->InternalIsValidStance(ubCheckDir, gAnimControl[pSoldier->usAnimState].ubEndHeight))
									{
										DebugAI(AI_MSG_INFO, pSoldier, String("turn before cutting fence"));
										RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
										pSoldier->aiData.usActionData = ubCheckDir;
										pSoldier->aiData.bNextAction = AI_ACTION_HANDLE_ITEM;
										pSoldier->aiData.usNextActionData = sNewSpot;
										return AI_ACTION_CHANGE_FACING;
									}
								}
							}
						}
					}

					if (pSoldier->aiData.bOrders == SEEKENEMY &&
						//WeAttack(pSoldier->bTeam)) &&
						Chance(20 * CountThrowableGrenades(pSoldier, EXPLOSV_NORMAL, 10)))
					{
						CheckTossFlankFence(pSoldier, &BestThrow);

						if (BestThrow.ubPossible)
						{
							DebugAI(AI_MSG_INFO, pSoldier, String("prepare throw at spot %d level %d aimtime %d", BestThrow.sTarget, BestThrow.bTargetLevel, BestThrow.ubAimTime));

							// if necessary, swap the usItem from holster into the hand position
							if (BestThrow.bWeaponIn != HANDPOS)
							{
								DebugAI(AI_MSG_INFO, pSoldier, String("rearrange pocket"));
								RearrangePocket(pSoldier, HANDPOS, BestThrow.bWeaponIn, FOREVER);
							}

							// set grenade as delayed
							pSoldier->inv[HANDPOS][0]->data.sObjectFlag |= DELAYED_GRENADE_EXPLOSION;

							// stand up before throwing if needed
							if (gAnimControl[pSoldier->usAnimState].ubEndHeight < BestThrow.ubStance &&
								pSoldier->InternalIsValidStance(AIDirection(pSoldier->sGridNo, BestThrow.sTarget), BestThrow.ubStance))
							{
								pSoldier->aiData.usActionData = BestThrow.ubStance;
								pSoldier->aiData.bNextAction = AI_ACTION_TOSS_PROJECTILE;
								pSoldier->aiData.usNextActionData = BestThrow.sTarget;
								pSoldier->aiData.bNextTargetLevel = BestThrow.bTargetLevel;
								pSoldier->aiData.bAimTime = BestThrow.ubAimTime;
								return AI_ACTION_CHANGE_STANCE;
							}
							else
							{
								pSoldier->aiData.usActionData = BestThrow.sTarget;
								pSoldier->bTargetLevel = BestThrow.bTargetLevel;
								pSoldier->aiData.bAimTime = BestThrow.ubAimTime;
							}

							DebugAI(AI_MSG_INFO, pSoldier, String("throw grenade at spot %d level %d", BestThrow.sTarget, BestThrow.bTargetLevel));

							return(AI_ACTION_TOSS_PROJECTILE);
						}
					}

					pSoldier->numFlanks = MAX_FLANKS_RED;
				}
			}
		}
	}

	return -1;
}

// After completing the lateral flank arc, cautiously close onto the original
// flank objective.  The old Vengeance implementation for this phase was disabled;
// this version uses the newer knowledge, hazard and fireteam-support checks.
INT8 DecideFinishFlanking(SOLDIERTYPE *pSoldier, INT32 sClosestDisturbance, INT8 bDisturbanceLevel)
{
	if (!pSoldier || pSoldier->numFlanks != MAX_FLANKS_RED)
		return -1;

	// A completed flank is no longer worth exploiting if the local situation changed.
	if (AIShouldAvoidAdvance(pSoldier) ||
		pSoldier->aiData.bUnderFire ||
		GuySawEnemy(pSoldier) ||
		AILocalStress(pSoldier) >= 50 ||
		pSoldier->stats.bLife < OKLIFE ||
		pSoldier->bBreath < 35)
	{
		pSoldier->numFlanks++;
		return -1;
	}

	if (gfTurnBasedAI && pSoldier->bActionPoints < pSoldier->bInitialActionPoints)
		return AI_ACTION_END_TURN;

	INT32 sObjective = pSoldier->lastFlankSpot;
	if (TileIsOutOfBounds(sObjective))
	{
		pSoldier->numFlanks++;
		return -1;
	}

	// If the currently relevant disturbance has moved far away from the contact this
	// flank was built around, do not blindly finish an obsolete manoeuvre.
	if (!TileIsOutOfBounds(sClosestDisturbance) &&
		PythSpacesAway(sObjective, sClosestDisturbance) > TACTICAL_RANGE / 2)
	{
		pSoldier->numFlanks++;
		return -1;
	}

	// We have reached a useful flank angle/position. Hand control back to normal RED AI.
	if (PythSpacesAway(pSoldier->sGridNo, sObjective) <= MIN_FLANK_DIST &&
		LocationToLocationLineOfSightTest(
			pSoldier->sGridNo, pSoldier->pathing.bLevel,
			sObjective, bDisturbanceLevel, TRUE, MAX_VISION_RANGE))
	{
		pSoldier->numFlanks++;
		return -1;
	}

	INT8 bReserveAP = GetAPsCrouch(pSoldier, TRUE) + GetAPsToLook(pSoldier);
	INT32 sAdvance = InternalGoAsFarAsPossibleTowards(
		pSoldier, sObjective, bReserveAP, AI_ACTION_SEEK_OPPONENT, FLAG_CAUTIOUS);

	if (TileIsOutOfBounds(sAdvance) || sAdvance == pSoldier->sGridNo)
	{
		pSoldier->numFlanks++;
		return -1;
	}

	if (!CheckNPCDestination(pSoldier, sAdvance) ||
		InGas(pSoldier, sAdvance) ||
		Water(sAdvance, pSoldier->pathing.bLevel))
	{
		pSoldier->numFlanks++;
		return -1;
	}

	// The flank completion must still behave like fire-and-manoeuvre, not a solo rush.
	if (!AIAdvanceHasMutualSupport(
		pSoldier, sAdvance, sObjective, bDisturbanceLevel))
	{
		pSoldier->numFlanks++;
		return -1;
	}

	UINT16 usCurrentExposure = AIKnownThreatExposure(
		pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
	UINT16 usAdvanceExposure = AIKnownThreatExposure(
		pSoldier, sAdvance, pSoldier->pathing.bLevel);

	// Even with support, do not leave useful cover for a sharply more exposed square.
	if (usAdvanceExposure > usCurrentExposure + 150 &&
		AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) &&
		!AnyCoverAtSpot(pSoldier, sAdvance))
	{
		pSoldier->numFlanks++;
		return -1;
	}

	pSoldier->aiData.usActionData = sAdvance;
	pSoldier->aiData.fAIFlags |= AI_CAUTIOUS;
	pSoldier->aiData.bNextAction = AI_ACTION_END_TURN;
	return AI_ACTION_SEEK_OPPONENT;
}

INT8 DecideUseWirecutters(SOLDIERTYPE *pSoldier)
{
	INT32 sOpponentGridNo;
	INT8 bOpponentLevel;
	INT32 sClosestOpponent = ClosestKnownOpponent(pSoldier, &sOpponentGridNo, &bOpponentLevel);
	INT8 bWirecutterSlot = FindWirecutters(pSoldier);

	if (bWirecutterSlot != NO_SLOT &&
		SoldierAI(pSoldier) &&
		!AIShouldAvoidAdvance(pSoldier) &&
		(pSoldier->CheckInitialAP() || gfTurnBasedAI) &&
		!pSoldier->aiData.bUnderFire &&
		pSoldier->pathing.bLevel == 0 &&
		pSoldier->aiData.bOrders == SEEKENEMY &&
		pSoldier->aiData.bAIMorale >= MORALE_CONFIDENT &&
		!TileIsOutOfBounds(sClosestOpponent) &&
		PythSpacesAway(pSoldier->sGridNo, sClosestOpponent) > TACTICAL_RANGE / 4 &&
		Chance(SoldierDifficultyLevel(pSoldier) * 20) &&
		pSoldier->bActionPoints >= GetAPsToCutFence(pSoldier) + GetAPsToLook(pSoldier) &&
		FindFenceAroundSpot(pSoldier->sGridNo))
	{
		UINT8 ubDesiredDir = AIDirection(pSoldier->sGridNo, sClosestOpponent);
		UINT8 ubCheckDir;
		INT32 sNewSpot;
		INT32 sNextSpot;
		INT32 sPathCost, sNewPathCost;
		INT32 sOriginalGridNo;

		// cannot cut fence diagonally
		if (ubDesiredDir % 2 == 0)
		{
			ubCheckDir = ubDesiredDir;
			sNewSpot = NewGridNo(pSoldier->sGridNo, DirectionInc(ubCheckDir));
			sNextSpot = NewGridNo(sNewSpot, DirectionInc(ubCheckDir));

			if (sNewSpot != pSoldier->sGridNo &&
				sNextSpot != sNewSpot &&
				IsCuttableWireFenceAtGridNo(sNewSpot) &&
				IsLocationSittable(sNextSpot, pSoldier->pathing.bLevel))
			{
				// check if cutting the fence improves situation
				sPathCost = EstimatePlotPath(pSoldier, sClosestOpponent, FALSE, FALSE, FALSE, RUNNING, pSoldier->bStealthMode, FALSE, 0);
				sOriginalGridNo = pSoldier->sGridNo;
				pSoldier->sGridNo = sNewSpot;
				sNewPathCost = EstimatePlotPath(pSoldier, sClosestOpponent, FALSE, FALSE, FALSE, RUNNING, pSoldier->bStealthMode, FALSE, 0);
				pSoldier->sGridNo = sOriginalGridNo;

				if (sNewPathCost > 0 && (sPathCost == 0 || sPathCost > sNewPathCost && sPathCost - sNewPathCost > APBPConstants[AP_MAXIMUM]))
				{
					if (pSoldier->ubDirection == ubCheckDir)
					{
						RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
						pSoldier->aiData.usActionData = sNewSpot;
						return AI_ACTION_HANDLE_ITEM;
					}
					else if (pSoldier->InternalIsValidStance(ubCheckDir, gAnimControl[pSoldier->usAnimState].ubEndHeight))
					{
						DebugAI(AI_MSG_INFO, pSoldier, String("turn before cutting fence"));
						RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
						pSoldier->aiData.usActionData = ubCheckDir;
						pSoldier->aiData.bNextAction = AI_ACTION_HANDLE_ITEM;
						pSoldier->aiData.usNextActionData = sNewSpot;
						return AI_ACTION_CHANGE_FACING;
					}
				}
			}
		}
		// try adjacent directions
		else
		{
			ubCheckDir = gOneCDirection[ubDesiredDir];
			sNewSpot = NewGridNo(pSoldier->sGridNo, DirectionInc(ubCheckDir));
			sNextSpot = NewGridNo(sNewSpot, DirectionInc(ubCheckDir));

			if (sNewSpot != pSoldier->sGridNo &&
				sNextSpot != sNewSpot &&
				IsCuttableWireFenceAtGridNo(sNewSpot) &&
				IsLocationSittable(sNextSpot, pSoldier->pathing.bLevel))
			{
				// check if cutting the fence improves situation
				sPathCost = EstimatePlotPath(pSoldier, sClosestOpponent, FALSE, FALSE, FALSE, RUNNING, pSoldier->bStealthMode, FALSE, 0);
				sOriginalGridNo = pSoldier->sGridNo;
				pSoldier->sGridNo = sNewSpot;
				sNewPathCost = EstimatePlotPath(pSoldier, sClosestOpponent, FALSE, FALSE, FALSE, RUNNING, pSoldier->bStealthMode, FALSE, 0);
				pSoldier->sGridNo = sOriginalGridNo;

				if (sNewPathCost > 0 && (sPathCost == 0 || sPathCost > sNewPathCost && sPathCost - sNewPathCost > APBPConstants[AP_MAXIMUM]))
				{
					if (pSoldier->ubDirection == ubCheckDir)
					{
						RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
						pSoldier->aiData.usActionData = sNewSpot;
						return AI_ACTION_HANDLE_ITEM;
					}
					else if (pSoldier->InternalIsValidStance(ubCheckDir, gAnimControl[pSoldier->usAnimState].ubEndHeight))
					{
						RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
						pSoldier->aiData.usActionData = ubCheckDir;
						pSoldier->aiData.bNextAction = AI_ACTION_HANDLE_ITEM;
						pSoldier->aiData.usNextActionData = sNewSpot;
						return AI_ACTION_CHANGE_FACING;
					}
				}
			}

			ubCheckDir = gOneCCDirection[ubDesiredDir];
			sNewSpot = NewGridNo(pSoldier->sGridNo, DirectionInc(ubCheckDir));
			sNextSpot = NewGridNo(sNewSpot, DirectionInc(ubCheckDir));

			if (sNewSpot != pSoldier->sGridNo &&
				sNextSpot != sNewSpot &&
				IsCuttableWireFenceAtGridNo(sNewSpot) &&
				IsLocationSittable(sNextSpot, pSoldier->pathing.bLevel))
			{
				// check if cutting the fence improves situation
				sPathCost = EstimatePlotPath(pSoldier, sClosestOpponent, FALSE, FALSE, FALSE, RUNNING, pSoldier->bStealthMode, FALSE, 0);
				sOriginalGridNo = pSoldier->sGridNo;
				pSoldier->sGridNo = sNewSpot;
				sNewPathCost = EstimatePlotPath(pSoldier, sClosestOpponent, FALSE, FALSE, FALSE, RUNNING, pSoldier->bStealthMode, FALSE, 0);
				pSoldier->sGridNo = sOriginalGridNo;

				if (sNewPathCost > 0 && (sPathCost == 0 || sPathCost > sNewPathCost && sPathCost - sNewPathCost > APBPConstants[AP_MAXIMUM]))
				{
					if (pSoldier->ubDirection == ubCheckDir)
					{
						RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
						pSoldier->aiData.usActionData = sNewSpot;
						return AI_ACTION_HANDLE_ITEM;
					}
					else if (pSoldier->InternalIsValidStance(ubCheckDir, gAnimControl[pSoldier->usAnimState].ubEndHeight))
					{
						RearrangePocket(pSoldier, HANDPOS, bWirecutterSlot, FOREVER);
						pSoldier->aiData.usActionData = ubCheckDir;
						pSoldier->aiData.bNextAction = AI_ACTION_HANDLE_ITEM;
						pSoldier->aiData.usNextActionData = sNewSpot;
						return AI_ACTION_CHANGE_FACING;
					}
				}
			}
		}
	}

	return -1;
}

INT8 DecideUseGrenadeSpecial(SOLDIERTYPE *pSoldier)
{
	ATTACKTYPE BestThrow;

	DebugAI(AI_MSG_TOPIC, pSoldier, String("[Grenade for special purpose]"));		
	if (gfTurnBasedAI &&
		!AIShouldAvoidAdvance(pSoldier) &&
		!gfHiddenInterrupt &&
		!gTacticalStatus.fInterruptOccurred &&
		pSoldier->bActionPoints >= APBPConstants[AP_MINIMUM] &&
		pSoldier->bActionPoints == pSoldier->bInitialActionPoints &&
		pSoldier->aiData.bOrders != STATIONARY &&
		pSoldier->aiData.bAIMorale >= MORALE_CONFIDENT &&
		Chance(20 * SoldierDifficultyLevel(pSoldier) + 10 * CountThrowableGrenades(pSoldier, EXPLOSV_NORMAL, 10)))
	{
		CheckTossGrenadeSpecial(pSoldier, &BestThrow);

		if (BestThrow.ubPossible  && Chance(BestThrow.iAttackValue))
		{
			DebugAI(AI_MSG_INFO, pSoldier, String("prepare throw at spot %d level %d aimtime %d", BestThrow.sTarget, BestThrow.bTargetLevel, BestThrow.ubAimTime));

			// if necessary, swap the usItem from holster into the hand position
			if (BestThrow.bWeaponIn != HANDPOS)
			{
				DebugAI(AI_MSG_INFO, pSoldier, String("rearrange pocket"));
				RearrangePocket(pSoldier, HANDPOS, BestThrow.bWeaponIn, FOREVER);
			}

			INT16 sTooCloseDistance = DAY_VISION_RANGE / 4;
			if (PythSpacesAway(pSoldier->sGridNo, BestThrow.sTarget) < sTooCloseDistance ||
				CountNearbyFriends(pSoldier, BestThrow.sTarget, sTooCloseDistance) > 0 ||
				CountNearbyNeutrals(pSoldier, BestThrow.sTarget, sTooCloseDistance) > 0)
			{
				// too close to soldier or any friend, set grenade as delayed
				if (Explosive[Item[pSoldier->inv[HANDPOS].usItem].ubClassIndex].ubType == EXPLOSV_NORMAL)
				{
					pSoldier->inv[HANDPOS][0]->data.sObjectFlag |= DELAYED_GRENADE_EXPLOSION;
				}
			}

			// stand up before throwing if needed
			if (gAnimControl[pSoldier->usAnimState].ubEndHeight < BestThrow.ubStance &&
				pSoldier->InternalIsValidStance(AIDirection(pSoldier->sGridNo, BestThrow.sTarget), BestThrow.ubStance))
			{
				pSoldier->aiData.usActionData = BestThrow.ubStance;
				pSoldier->aiData.bNextAction = AI_ACTION_TOSS_PROJECTILE;
				pSoldier->aiData.usNextActionData = BestThrow.sTarget;
				pSoldier->aiData.bNextTargetLevel = BestThrow.bTargetLevel;
				pSoldier->aiData.bAimTime = BestThrow.ubAimTime;
				return AI_ACTION_CHANGE_STANCE;
			}
			else
			{
				pSoldier->aiData.usActionData = BestThrow.sTarget;
				pSoldier->bTargetLevel = BestThrow.bTargetLevel;
				pSoldier->aiData.bAimTime = BestThrow.ubAimTime;
			}

			DebugAI(AI_MSG_INFO, pSoldier, String("throw grenade at spot %d level %d", BestThrow.sTarget, BestThrow.bTargetLevel));

			return(AI_ACTION_TOSS_PROJECTILE);
		}
	}

	return -1;
}

INT8 DecideSmokeCoverMovement(SOLDIERTYPE *pSoldier, INT32 sClosestDisturbance)
{
	DebugAI(AI_MSG_TOPIC, pSoldier, String("[Smoke to cover movement]"));

	// Movement smoke is a coordinated support task. Emergency casualty/self-
	// protection smoke remains available to lower-quality troops elsewhere.
	if (pSoldier && AICombatTeam(pSoldier) && !AIAllowsProactiveSupport(pSoldier))
		return -1;

	ATTACKTYPE BestThrow;

	// try to use smoke to cover movement
	if (gfTurnBasedAI &&
		SoldierAI(pSoldier) &&
		FindThrowableGrenade(pSoldier, EXPLOSV_SMOKE) != NO_SLOT &&
		pSoldier->bActionPoints >= APBPConstants[AP_MINIMUM] &&
		pSoldier->bActionPoints == pSoldier->bInitialActionPoints &&
		!TileIsOutOfBounds(sClosestDisturbance) &&
		pSoldier->aiData.bAIMorale >= MORALE_CONFIDENT &&
		!AICheckIsSniper(pSoldier) &&
		!AICheckIsMachinegunner(pSoldier) &&
		pSoldier->aiData.bOrders != STATIONARY &&
		!AICheckSuccessfulAttack(pSoldier, TRUE) &&
		(pSoldier->aiData.bUnderFire ||
		CountSeenEnemiesLastTurn(pSoldier) > AICountNearbyOperationalFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 2) ||
		CountTeamUnderAttack(pSoldier->bTeam, pSoldier->sGridNo, DAY_VISION_RANGE) > CountFriendsLastAttackHit(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE) ||
		CountCorpses(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE, TRUE, TRUE) > AICountNearbyOperationalFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE)) &&
		(InSmoke(pSoldier->sGridNo, pSoldier->pathing.bLevel) ||
		Chance(SoldierDifficultyLevel(pSoldier) * 10) ||
		Chance(AIFriendlyCasualtyPercent(pSoldier)) ||
		Chance(10 * CountTeamUnderAttack(pSoldier->bTeam, pSoldier->sGridNo, DAY_VISION_RANGE)) ||
		Chance(10 * CountCorpses(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE, TRUE, TRUE))))
	{
		gubNPCAPBudget = 0;
		gubNPCDistLimit = 0;

		BestThrow.ubPossible = FALSE;

		// check path to closest disturbance
		if (FindBestPath(pSoldier, sClosestDisturbance, pSoldier->pathing.bLevel, RUNNING, COPYROUTE, 0))
		{
			DebugAI(AI_MSG_INFO, pSoldier, String("found path to %d, path size %d ", sClosestDisturbance, pSoldier->pathing.usPathDataSize));

			INT32 sCheckGridNo = pSoldier->sGridNo;
			INT32 sSmokeSpot = NOWHERE;

			for (INT16 sLoop = pSoldier->pathing.usPathIndex; sLoop < pSoldier->pathing.usPathDataSize; sLoop++)
			{
				sCheckGridNo = NewGridNo(sCheckGridNo, DirectionInc((UINT8)(pSoldier->pathing.usPathingData[sLoop])));

				// find last dangerous spot or last spot seen by enemy if rushing
				if (!TileIsOutOfBounds(sCheckGridNo) &&
					PythSpacesAway(pSoldier->sGridNo, sCheckGridNo) < TACTICAL_RANGE / 2 &&
					PythSpacesAway(pSoldier->sGridNo, sCheckGridNo) > TACTICAL_RANGE / 4 &&
					!Water(sCheckGridNo, pSoldier->pathing.bLevel) &&
					!InSmokeNearby(sCheckGridNo, pSoldier->pathing.bLevel) &&
					/*(pSoldier->RushAttackPrepare() ||
					fSectorAttack ||
					AICorpseWarningKnown(pSoldier, sCheckGridNo, pSoldier->pathing.bLevel) ||
					InLightAtNight(sCheckGridNo, pSoldier->pathing.bLevel)) &&*/
					!SightCoverAtSpot(pSoldier, sCheckGridNo, FALSE))
				{
					CheckTossGrenadeAt(pSoldier, &BestThrow, sCheckGridNo, pSoldier->pathing.bLevel, EXPLOSV_SMOKE);
					if (BestThrow.ubPossible)
					{
						sSmokeSpot = sCheckGridNo;
					}
				}
			}

			if (!TileIsOutOfBounds(sSmokeSpot))
			{
				DebugAI(AI_MSG_INFO, pSoldier, String("found smoke spot %d ", sSmokeSpot));
				CheckTossGrenadeAt(pSoldier, &BestThrow, sSmokeSpot, pSoldier->pathing.bLevel, EXPLOSV_SMOKE);
			}

			// found throw spot
			if (BestThrow.ubPossible)
			{
				DebugAI(AI_MSG_INFO, pSoldier, String("prepare throw at spot %d level %d aimtime %d", BestThrow.sTarget, BestThrow.bTargetLevel, BestThrow.ubAimTime));

				// if necessary, swap the usItem from holster into the hand position
				if (BestThrow.bWeaponIn != HANDPOS)
				{
					DebugAI(AI_MSG_INFO, pSoldier, String("rearrange pocket"));
					RearrangePocket(pSoldier, HANDPOS, BestThrow.bWeaponIn, FOREVER);
				}

				// stand up before throwing if needed
				if (gAnimControl[pSoldier->usAnimState].ubEndHeight < BestThrow.ubStance &&
					pSoldier->InternalIsValidStance(AIDirection(pSoldier->sGridNo, BestThrow.sTarget), BestThrow.ubStance))
				{
					pSoldier->aiData.usActionData = BestThrow.ubStance;
					pSoldier->aiData.bNextAction = AI_ACTION_TOSS_PROJECTILE;
					pSoldier->aiData.usNextActionData = BestThrow.sTarget;
					pSoldier->aiData.bNextTargetLevel = BestThrow.bTargetLevel;
					pSoldier->aiData.bAimTime = BestThrow.ubAimTime;
					return AI_ACTION_CHANGE_STANCE;
				}

				pSoldier->aiData.usActionData = BestThrow.sTarget;
				pSoldier->bTargetLevel = BestThrow.bTargetLevel;
				pSoldier->aiData.bAimTime = BestThrow.ubAimTime;

				return AI_ACTION_TOSS_PROJECTILE;
			}
		}
		gubNPCAPBudget = 0;
	}

	return -1;
}

// Emergency smoke for casualty protection and emergency disengagement.  This is
// deliberately more selective than ordinary movement smoke: it requires a critical
// casualty or a heavily suppressed soldier who is actually exposed to known enemy fire.
INT8 DecideEmergencyProtectionSmoke(SOLDIERTYPE *pSoldier)
{
	if (!gfTurnBasedAI || !pSoldier || !AICombatTeam(pSoldier) ||
		!SoldierAI(pSoldier) || pSoldier->stats.bLife < OKLIFE ||
		pSoldier->bCollapsed || pSoldier->bBreathCollapsed ||
		(pSoldier->usSoldierFlagMask & SOLDIER_POW) ||
		(pSoldier->flags.uiStatusFlags & SOLDIER_COWERING) ||
		AIEscapeActive(pSoldier) || AIShouldStartEscape(pSoldier) ||
		pSoldier->bActionPoints < APBPConstants[AP_MINIMUM] ||
		FindThrowableGrenade(pSoldier, EXPLOSV_SMOKE) == NO_SLOT)
	{
		return AI_ACTION_NONE;
	}

	SOLDIERTYPE *pBestProtected = NULL;
	INT32 iBestValue = 0;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; iCounter++)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || !pFriend->bActive || !pFriend->bInSector || pFriend->stats.bLife <= 0)
			continue;

		if (InSmokeNearby(pFriend->sGridNo, pFriend->pathing.bLevel))
			continue;

		// Current 1.13 smoke-targeting logic avoids water. Do the same here so
		// emergency protection smoke is not wasted on a water tile.
		if (Water(pFriend->sGridNo, pFriend->pathing.bLevel))
			continue;

		INT32 iDistance = PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo);
		if (iDistance > DAY_VISION_RANGE / 2)
			continue;

		BOOLEAN fSameElement = AISameFireteam(pSoldier, pFriend);
		if (AICombatTeam(pSoldier) &&
			!fSameElement &&
			iDistance > DAY_VISION_RANGE / 4)
		{
			continue;
		}

		UINT16 usKnownExposure = AIKnownThreatExposure(
			pSoldier, pFriend->sGridNo, pFriend->pathing.bLevel);

		BOOLEAN fCriticalCasualty = pFriend->stats.bLife < OKLIFE && pFriend->bBleeding > 0;
		BOOLEAN fSevereCasualty = pFriend->stats.bLife < pFriend->stats.bLifeMax / 2 && pFriend->bBleeding > 0;
		BOOLEAN fPinned = pFriend->aiData.bUnderFire && ShockLevelPercent(pFriend) >= 50;
		BOOLEAN fEmergencySelf = pFriend == pSoldier && pFriend->aiData.bUnderFire &&
			AIPersonalRisk(pFriend) > AIPersonalRiskTolerance(pFriend);

		// A long-range shooter can pin an exposed element before shock reaches the
		// generic emergency threshold. If this actor knows where the recent attacker
		// was reported and the lane is genuinely long, smoke may be used proactively
		// to break LOS. No hidden position/weapon information is consulted.
		BOOLEAN fLongRangeFireLane = FALSE;
		if (pFriend->aiData.bUnderFire && usKnownExposure > 0)
		{
			UINT8 ubAttacker = pFriend->ubPreviousAttackerID;
			if (ubAttacker == NOBODY)
				ubAttacker = pFriend->ubNextToPreviousAttackerID;

			if (ubAttacker != NOBODY && MercPtrs[ubAttacker] &&
				Knowledge(pSoldier, ubAttacker) != NOT_HEARD_OR_SEEN)
			{
				INT32 sAttackerSpot = KnownLocation(pSoldier, ubAttacker);
				if (!TileIsOutOfBounds(sAttackerSpot) &&
					PythSpacesAway(pFriend->sGridNo, sAttackerSpot) >= DAY_VISION_RANGE)
				{
					fLongRangeFireLane = TRUE;
				}
			}
		}

		if (!fCriticalCasualty && !fSevereCasualty && !fPinned &&
			!fEmergencySelf && !fLongRangeFireLane)
		{
			continue;
		}

		// Do not spend smoke on somebody whom the acting soldier does not believe is
		// exposed to enemy fire. This keeps the behaviour information-fair.
		if (usKnownExposure == 0)
			continue;

		INT32 iValue = 0;
		if (fSameElement)
			iValue += 15;
		if (fCriticalCasualty)
			iValue += 110;
		else if (fSevereCasualty)
			iValue += 70;

		if (fPinned)
			iValue += 55;
		if (fEmergencySelf)
			iValue += 35;
		if (fLongRangeFireLane)
			iValue += 45;
		if (!AnyCoverAtSpot(pFriend, pFriend->sGridNo))
			iValue += 20;
		iValue += __min((INT32)20, (INT32)pFriend->bBleeding / 2);
		iValue -= iDistance * 2;

		if (iValue > iBestValue)
		{
			iBestValue = iValue;
			pBestProtected = pFriend;
		}
	}

	if (!pBestProtected || iBestValue < 45)
		return AI_ACTION_NONE;

	ATTACKTYPE BestThrow;
	BestThrow.ubPossible = FALSE;
	CheckTossGrenadeAt(pSoldier, &BestThrow, pBestProtected->sGridNo,
		pBestProtected->pathing.bLevel, EXPLOSV_SMOKE);

	if (!BestThrow.ubPossible)
		return AI_ACTION_NONE;

	// Commit to the smoke only after the normal throw calculation says the trajectory
	// and AP cost are valid. Smoke is non-lethal, so protecting the casualty's own tile
	// is intentional and creates concealment for treatment or withdrawal.
	if (BestThrow.bWeaponIn != HANDPOS)
		RearrangePocket(pSoldier, HANDPOS, BestThrow.bWeaponIn, FOREVER);

	pSoldier->bTargetLevel = BestThrow.bTargetLevel;
	pSoldier->aiData.bAimTime = BestThrow.ubAimTime;

	if (gAnimControl[pSoldier->usAnimState].ubEndHeight < BestThrow.ubStance &&
		pSoldier->InternalIsValidStance(AIDirection(pSoldier->sGridNo, BestThrow.sTarget), BestThrow.ubStance))
	{
		pSoldier->aiData.usActionData = BestThrow.ubStance;
		pSoldier->aiData.bNextAction = AI_ACTION_TOSS_PROJECTILE;
		pSoldier->aiData.usNextActionData = BestThrow.sTarget;
		pSoldier->aiData.bNextTargetLevel = BestThrow.bTargetLevel;
		return AI_ACTION_CHANGE_STANCE;
	}

	pSoldier->aiData.usActionData = BestThrow.sTarget;
	return AI_ACTION_TOSS_PROJECTILE;
}

static BOOLEAN AIGetEscapeEdgeArray(INT8 bDirection, INT32 **ppsEdgepoints, UINT16 *pusSize)
{
	if (!ppsEdgepoints || !pusSize)
		return FALSE;

	switch (bDirection)
	{
	case NORTHWEST: // north
		*ppsEdgepoints = gps1stNorthEdgepointArray;
		*pusSize = gus1stNorthEdgepointArraySize;
		break;
	case NORTHEAST: // east
		*ppsEdgepoints = gps1stEastEdgepointArray;
		*pusSize = gus1stEastEdgepointArraySize;
		break;
	case SOUTHEAST: // south
		*ppsEdgepoints = gps1stSouthEdgepointArray;
		*pusSize = gus1stSouthEdgepointArraySize;
		break;
	case SOUTHWEST: // west
		*ppsEdgepoints = gps1stWestEdgepointArray;
		*pusSize = gus1stWestEdgepointArraySize;
		break;
	default:
		return FALSE;
	}

	return (*ppsEdgepoints != NULL && *pusSize > 0);
}

static INT32 AIClosestKnownThreatSpotForEscape(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier)
		return NOWHERE;

	INT32 sBestSpot = NOWHERE;
	INT32 iBestDistance = 0x7FFFFFFF;

	for (UINT16 uiLoop = 0; uiLoop < MAX_NUM_SOLDIERS; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercPtrs[uiLoop];
		if (!pOpponent || pOpponent == pSoldier)
			continue;

		INT8 bKnowledge = Knowledge(pSoldier, pOpponent->ubID);
		if (bKnowledge == NOT_HEARD_OR_SEEN)
			continue;

		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			pSoldier->bSide == pOpponent->bSide ||
			pOpponent->ubBodyType == CROW)
		{
			continue;
		}

		INT32 sKnownSpot = KnownLocation(pSoldier, pOpponent->ubID);
		if (TileIsOutOfBounds(sKnownSpot))
			continue;

		INT32 iDistance = PythSpacesAway(pSoldier->sGridNo, sKnownSpot);
		if (iDistance < iBestDistance)
		{
			iBestDistance = iDistance;
			sBestSpot = sKnownSpot;
		}
	}

	return sBestSpot;
}

static INT32 AINearestUsableOuterEdgepoint(SOLDIERTYPE *pSoldier, INT8 bDirection)
{
	INT32 *psEdgepoints = NULL;
	UINT16 usSize = 0;
	if (!AIGetEscapeEdgeArray(bDirection, &psEdgepoints, &usSize))
		return NOWHERE;

	INT32 sBestSpot = NOWHERE;
	INT32 iBestDistance = 0x7FFFFFFF;

	for (UINT16 usIndex = 0; usIndex < usSize; ++usIndex)
	{
		INT32 sSpot = psEdgepoints[usIndex];
		if (TileIsOutOfBounds(sSpot) ||
			sSpot == pSoldier->sGridNo ||
			sSpot == pSoldier->pathing.sBlackList)
		{
			continue;
		}

		INT8 bActualDirection = -1;
		if (!GridNoOnEdgeOfMap(sSpot, &bActualDirection) || bActualDirection != bDirection)
			continue;

		if (sSpot != pSoldier->sGridNo &&
			!NewOKDestination(pSoldier, sSpot, TRUE, pSoldier->pathing.bLevel))
		{
			continue;
		}

		INT32 iDistance = PythSpacesAway(pSoldier->sGridNo, sSpot);
		if (iDistance < iBestDistance)
		{
			iBestDistance = iDistance;
			sBestSpot = sSpot;
		}
	}

	return sBestSpot;
}

static BOOLEAN AIEscapeEndpointUnsafe(SOLDIERTYPE *pSoldier, INT32 sSpot)
{
	if (TileIsOutOfBounds(sSpot))
		return TRUE;

	if (InGas(pSoldier, sSpot) ||
		WaterTooDeepForAttacks(sSpot, pSoldier->pathing.bLevel) ||
		RedSmokeDanger(sSpot, pSoldier->pathing.bLevel) ||
		FindBombNearby(pSoldier, sSpot, BOMB_DETECTION_RANGE))
	{
		return TRUE;
	}

	return FALSE;
}

static BOOLEAN AIEvaluateEscapeRoute(SOLDIERTYPE *pSoldier, INT32 sTarget,
	INT32 *piPathSteps, UINT16 *pusPeakExposure, UINT16 *pusAverageExposure)
{
	if (!pSoldier || TileIsOutOfBounds(sTarget))
		return FALSE;

	INT32 iPathSteps = 0;
	if (sTarget != pSoldier->sGridNo)
	{
		iPathSteps = FindBestPath(pSoldier, sTarget, pSoldier->pathing.bLevel,
			RUNNING, NO_COPYROUTE, 0);
		if (iPathSteps == 0)
			return FALSE;
	}

	UINT16 usCurrentExposure = AIKnownThreatExposure(pSoldier,
		pSoldier->sGridNo, pSoldier->pathing.bLevel);
	UINT16 usPeakExposure = usCurrentExposure;
	UINT32 uiExposureTotal = 0;
	UINT8 ubSamples = 0;
	INT32 sRouteSpot = pSoldier->sGridNo;

	// NO_COPYROUTE leaves the first MAX_PATH_LIST_SIZE path directions in
	// guiPathingData without replacing the soldier's active route. Sample four
	// points at most; escape is rare, so this gives useful route awareness without
	// turning every AI decision into dozens of LOS checks.
	for (INT32 iStep = 0; iStep < iPathSteps && iStep < MAX_PATH_LIST_SIZE; ++iStep)
	{
		INT32 sNextSpot = NewGridNo(sRouteSpot,
			DirectionInc((UINT8)guiPathingData[iStep]));
		if (sNextSpot == sRouteSpot || TileIsOutOfBounds(sNextSpot))
			return FALSE;

		sRouteSpot = sNextSpot;

		INT32 iStepNumber = iStep + 1;
		BOOLEAN fSample =
			(iStepNumber == __max(1, iPathSteps / 4)) ||
			(iStepNumber == __max(1, iPathSteps / 2)) ||
			(iStepNumber == __max(1, (iPathSteps * 3) / 4)) ||
			(iStepNumber == iPathSteps) ||
			(iStepNumber == MAX_PATH_LIST_SIZE);

		if (!fSample)
			continue;

		if (AIEscapeEndpointUnsafe(pSoldier, sRouteSpot))
			return FALSE;

		UINT16 usExposure = AIKnownThreatExposure(pSoldier,
			sRouteSpot, pSoldier->pathing.bLevel);
		usPeakExposure = __max(usPeakExposure, usExposure);
		uiExposureTotal += usExposure;
		++ubSamples;

		// A route that temporarily exposes the soldier to several more known
		// threats than his current position is not a sensible break-contact route.
		if (usExposure > usCurrentExposure + 200)
			return FALSE;
	}

	if (piPathSteps)
		*piPathSteps = iPathSteps;
	if (pusPeakExposure)
		*pusPeakExposure = usPeakExposure;
	if (pusAverageExposure)
		*pusAverageExposure = ubSamples ?
			(UINT16)(uiExposureTotal / ubSamples) : usCurrentExposure;

	return TRUE;
}

static BOOLEAN AIEscapeTargetStillSafe(SOLDIERTYPE *pSoldier, INT32 sSpot, INT8 bDirection)
{
	INT8 bCheckedDirection = bDirection;
	if (!EscapeDirectionIsValid(&bCheckedDirection) ||
		bCheckedDirection != bDirection ||
		AIEscapeEndpointUnsafe(pSoldier, sSpot))
	{
		return FALSE;
	}

	if (sSpot != pSoldier->sGridNo)
	{
		INT32 iPathCost = PlotPath(pSoldier, sSpot, NO_COPYROUTE, NO_PLOT,
			TEMPORARY, RUNNING, NOT_STEALTH, FORWARD, 0);
		if (iPathCost == 0)
			return FALSE;
	}

	UINT16 usCurrentExposure = AIKnownThreatExposure(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
	UINT16 usTargetExposure = AIKnownThreatExposure(pSoldier, sSpot, pSoldier->pathing.bLevel);
	if (usTargetExposure > usCurrentExposure + 200)
		return FALSE;

	INT32 sKnownThreat = AIClosestKnownThreatSpotForEscape(pSoldier);
	if (!TileIsOutOfBounds(sKnownThreat))
	{
		INT32 iCurrentDistance = PythSpacesAway(pSoldier->sGridNo, sKnownThreat);
		INT32 iTargetDistance = PythSpacesAway(sSpot, sKnownThreat);
		if (iTargetDistance + 2 < iCurrentDistance &&
			usTargetExposure >= usCurrentExposure)
		{
			return FALSE;
		}
	}

	return TRUE;
}

static INT32 AISelectEscapeEdge(SOLDIERTYPE *pSoldier, INT8 *pbBestDirection)
{
	if (!pSoldier || !pbBestDirection)
		return NOWHERE;

	static const INT8 bDirections[4] = { NORTHWEST, NORTHEAST, SOUTHEAST, SOUTHWEST };
	INT32 sKnownThreat = AIClosestKnownThreatSpotForEscape(pSoldier);
	INT32 iCurrentThreatDistance = TileIsOutOfBounds(sKnownThreat) ? 0 :
		PythSpacesAway(pSoldier->sGridNo, sKnownThreat);
	UINT16 usCurrentExposure = AIKnownThreatExposure(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);

	INT32 sBestSpot = NOWHERE;
	INT8 bBestDirection = -1;
	INT32 iBestScore = -0x7FFFFFFF;

	for (UINT8 ubIndex = 0; ubIndex < 4; ++ubIndex)
	{
		INT8 bDirection = bDirections[ubIndex];
		INT8 bCheckedDirection = bDirection;
		if (!EscapeDirectionIsValid(&bCheckedDirection) || bCheckedDirection != bDirection)
			continue;

		INT32 sSpot = AINearestUsableOuterEdgepoint(pSoldier, bDirection);
		if (TileIsOutOfBounds(sSpot) || AIEscapeEndpointUnsafe(pSoldier, sSpot))
			continue;

		INT32 iPathSteps = 0;
		UINT16 usPeakRouteExposure = 0;
		UINT16 usAverageRouteExposure = 0;
		if (!AIEvaluateEscapeRoute(pSoldier, sSpot, &iPathSteps,
			&usPeakRouteExposure, &usAverageRouteExposure))
		{
			continue;
		}

		UINT16 usExposure = AIKnownThreatExposure(pSoldier, sSpot, pSoldier->pathing.bLevel);
		INT32 iThreatDistance = TileIsOutOfBounds(sKnownThreat) ? 0 :
			PythSpacesAway(sSpot, sKnownThreat);

		// Do not call a route "escape" if its endpoint is both more exposed and
		// materially closer to the last known threat. In that case hiding is safer.
		if (!TileIsOutOfBounds(sKnownThreat) &&
			iThreatDistance + 2 < iCurrentThreatDistance &&
			usExposure >= usCurrentExposure)
		{
			continue;
		}

		if (usExposure > usCurrentExposure + 200)
			continue;

		INT32 iScore = -(iPathSteps * 20) -
			((INT32)usExposure * 8) -
			((INT32)usAverageRouteExposure * 7) -
			((INT32)usPeakRouteExposure * 5);
		if (!TileIsOutOfBounds(sKnownThreat))
			iScore += (iThreatDistance - iCurrentThreatDistance) * 20;
		if (InLightAtNight(sSpot, pSoldier->pathing.bLevel))
			iScore -= 40;

		if (iScore > iBestScore)
		{
			iBestScore = iScore;
			sBestSpot = sSpot;
			bBestDirection = bDirection;
		}
	}

	*pbBestDirection = bBestDirection;
	return sBestSpot;
}

static BOOLEAN AIMilitiaShouldStrategicallyRetreat(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->bTeam != MILITIA_TEAM || gbWorldSectorZ != 0 ||
		pSoldier->stats.bLife < OKLIFE || pSoldier->bCollapsed ||
		pSoldier->aiData.bAlertStatus < STATUS_RED)
	{
		return FALSE;
	}

	INT8 bSituation = AIBattleSituation(pSoldier);
	UINT8 ubCasualties = AIFriendlyCasualtyPercent(pSoldier);
	UINT8 ubRoutPressure = AILocalRoutPressure(pSoldier);
	INT32 iStress = AILocalStress(pSoldier);

	// Strategic retreat is rarer than tactical disengagement. A militia force
	// should first give ground and consolidate; it abandons the sector only after
	// the fight has clearly broken down.
	if (AILastSurvivorPressure(pSoldier) && ubCasualties >= 50)
		return TRUE;

	if (bSituation == AI_BATTLE_CATASTROPHIC)
	{
		INT32 iCasualtyThreshold = 30 + (AIPersonalRiskTolerance(pSoldier) - 50) / 5;
		iCasualtyThreshold = __max(27, __min(35, iCasualtyThreshold));

		if (ubCasualties >= iCasualtyThreshold &&
			(iStress >= 25 || ubRoutPressure >= 35))
		{
			return TRUE;
		}

		if (ubCasualties >= 50)
			return TRUE;
	}

	if (bSituation == AI_BATTLE_LOSING &&
		ubCasualties >= 45 &&
		ubRoutPressure >= 50 &&
		iStress >= 30)
	{
		return TRUE;
	}

	return FALSE;
}

static INT8 AIMilitiaStrategicDirection(INT16 sTargetX, INT16 sTargetY)
{
	if (sTargetX > gWorldSectorX)
		return NORTHEAST;	// east edge
	if (sTargetX < gWorldSectorX)
		return SOUTHWEST;	// west edge
	if (sTargetY > gWorldSectorY)
		return SOUTHEAST;	// south edge
	if (sTargetY < gWorldSectorY)
		return NORTHWEST;	// north edge
	return -1;
}

static INT8 DecideMilitiaStrategicRetreatAction(SOLDIERTYPE *pSoldier, BOOLEAN fCanMove)
{
	if (!AIMilitiaShouldStrategicallyRetreat(pSoldier) ||
		!fCanMove || pSoldier->bActionPoints != pSoldier->bInitialActionPoints)
	{
		return AI_ACTION_NONE;
	}

	INT16 sTargetX = 0;
	INT16 sTargetY = 0;
	if (!FindMilitiaStrategicRetreatSector(gWorldSectorX, gWorldSectorY, &sTargetX, &sTargetY))
		return AI_ACTION_NONE;

	INT8 bDirection = AIMilitiaStrategicDirection(sTargetX, sTargetY);
	if (bDirection == -1)
		return AI_ACTION_NONE;

	INT8 bCheckedDirection = bDirection;
	if (!EscapeDirectionIsValid(&bCheckedDirection) || bCheckedDirection != bDirection)
		return AI_ACTION_NONE;

	// If already on the correct edge, arm normal AI traversal. The traversal
	// handler has a militia-specific branch that transfers one strategic militia
	// member plus his equipment instead of processing him as a dead enemy soldier.
	INT8 bCurrentEdge = -1;
	if (GridNoOnEdgeOfMap(pSoldier->sGridNo, &bCurrentEdge) && bCurrentEdge == bDirection)
	{
		INT8 bExitDirection = -1;
		INT32 sExitPoint = FindNearbyPointOnEdgeOfMap(pSoldier, &bExitDirection);
		if (!TileIsOutOfBounds(sExitPoint) && bExitDirection == bDirection)
		{
			pSoldier->ubQuoteActionID = GetTraversalQuoteActionID(bDirection);
			pSoldier->aiData.usActionData = sExitPoint;
			return AI_ACTION_RUN_AWAY;
		}
	}

	INT32 sEdgeSpot = AINearestUsableOuterEdgepoint(pSoldier, bDirection);
	if (TileIsOutOfBounds(sEdgeSpot) || AIEscapeEndpointUnsafe(pSoldier, sEdgeSpot))
		return AI_ACTION_NONE;

	INT32 iPathSteps = 0;
	UINT16 usPeakExposure = 0;
	UINT16 usAverageExposure = 0;
	if (!AIEvaluateEscapeRoute(pSoldier, sEdgeSpot, &iPathSteps,
		&usPeakExposure, &usAverageExposure))
	{
		return AI_ACTION_NONE;
	}

	// Do not arm traversal while still crossing the map; the existing AI movement
	// code may split long routes across several turns.
	pSoldier->ubQuoteActionID = 0;
	pSoldier->aiData.usActionData = sEdgeSpot;
	return AI_ACTION_RUN_AWAY;
}
static BOOLEAN gfAIEscapePlanInitialized[MAX_NUM_SOLDIERS] = { FALSE };
static UINT32 guiAIEscapePlanIdentity[MAX_NUM_SOLDIERS] = { 0 };
static INT32 gsAIEscapeTarget[MAX_NUM_SOLDIERS] = { 0 };
static INT8 gbAIEscapeDirection[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIEscapeNoRouteTurn[MAX_NUM_SOLDIERS] = { 0 };
static UINT8 gubAIEscapeBlockedTurns[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIEscapeBlockedTurnStamp[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIEscapePlanLastTurnStamp = 0;
extern UINT32 guiTurnCnt;

static void AIMaintainEscapePlanTimeline(void)
{
	UINT32 uiTurnStamp = guiTurnCnt + 1;
	if (guiAIEscapePlanLastTurnStamp != 0 &&
		uiTurnStamp < guiAIEscapePlanLastTurnStamp)
	{
		for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
		{
			gfAIEscapePlanInitialized[i] = FALSE;
			guiAIEscapePlanIdentity[i] = 0;
			gsAIEscapeTarget[i] = NOWHERE;
			gbAIEscapeDirection[i] = -1;
			guiAIEscapeNoRouteTurn[i] = 0;
			gubAIEscapeBlockedTurns[i] = 0;
			guiAIEscapeBlockedTurnStamp[i] = 0;
		}
	}
	guiAIEscapePlanLastTurnStamp = uiTurnStamp;
}

static void AIResetEscapePlan(SOLDIERTYPE *pSoldier)
{
	AIMaintainEscapePlanTimeline();

	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	UINT8 ubID = pSoldier->ubID;
	gfAIEscapePlanInitialized[ubID] = TRUE;
	guiAIEscapePlanIdentity[ubID] = pSoldier->uiUniqueSoldierIdValue;
	gsAIEscapeTarget[ubID] = NOWHERE;
	gbAIEscapeDirection[ubID] = -1;
	guiAIEscapeNoRouteTurn[ubID] = 0;
	gubAIEscapeBlockedTurns[ubID] = 0;
	guiAIEscapeBlockedTurnStamp[ubID] = 0;
}

static void AIResetEscapeBlockedTurns(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	gubAIEscapeBlockedTurns[pSoldier->ubID] = 0;
	guiAIEscapeBlockedTurnStamp[pSoldier->ubID] = 0;
}

static UINT8 AIRegisterEscapeBlockedTurn(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return 0;

	UINT8 ubID = pSoldier->ubID;
	UINT32 uiTurnStamp = guiTurnCnt + 1;
	if (guiAIEscapeBlockedTurnStamp[ubID] != uiTurnStamp)
	{
		guiAIEscapeBlockedTurnStamp[ubID] = uiTurnStamp;
		if (gubAIEscapeBlockedTurns[ubID] < 255)
			++gubAIEscapeBlockedTurns[ubID];
	}

	return gubAIEscapeBlockedTurns[ubID];
}

static BOOLEAN AIShouldCapitulate(SOLDIERTYPE *pSoldier, UINT8 ubBlockedTurns)
{
	if (!pSoldier ||
		pSoldier->bTeam != ENEMY_TEAM ||
		!AIEscapeActive(pSoldier) ||
		pSoldier->stats.bLife < OKLIFE ||
		pSoldier->bCollapsed ||
		(pSoldier->usSoldierFlagMask & SOLDIER_POW) ||
		pSoldier->aiData.bOppCnt <= 0 ||
		!gGameExternalOptions.fAllowPrisonerSystem ||
		!gGameExternalOptions.fEnemyCanSurrender)
	{
		return FALSE;
	}

	INT32 iTolerance = AIPersonalRiskTolerance(pSoldier);
	UINT8 ubRequiredBlockedTurns = (iTolerance >= 65) ? 3 : 2;
	if (ubBlockedTurns < ubRequiredBlockedTurns)
		return FALSE;

	INT8 bSituation = AIBattleSituation(pSoldier);
	UINT8 ubCasualties = AIFriendlyCasualtyPercent(pSoldier);
	UINT8 ubRoutPressure = AILocalRoutPressure(pSoldier);
	INT32 iStress = AILocalStress(pSoldier);

	if (AILastSurvivorPressure(pSoldier))
		return TRUE;

	if (ubCasualties >= 75 && bSituation != AI_BATTLE_WINNING)
		return TRUE;

	if (bSituation == AI_BATTLE_CATASTROPHIC && iStress >= 25)
		return TRUE;

	if (bSituation == AI_BATTLE_LOSING &&
		ubCasualties >= 50 &&
		ubRoutPressure >= 50 &&
		iStress >= 40)
	{
		return TRUE;
	}

	return FALSE;
}

static INT8 AITryCapitulation(SOLDIERTYPE *pSoldier, UINT8 ubBlockedTurns)
{
	if (!AIShouldCapitulate(pSoldier, ubBlockedTurns))
		return AI_ACTION_NONE;

	// Reuse 1.13's existing enemy-prisoner state rather than inventing a second
	// surrender system. POWs stop acting, stop counting as active enemies and are
	// processed by the normal post-battle prisoner/equipment pipeline.
	pSoldier->usSoldierFlagMask |= SOLDIER_POW;
	RemoveManAsTarget(pSoldier);

	pSoldier->ubQuoteActionID = 0;
	pSoldier->aiData.bNextAction = AI_ACTION_NONE;
	pSoldier->aiData.usNextActionData = NOWHERE;
	pSoldier->aiData.usActionData = ANIM_CROUCH;
	AIResetEscapePlan(pSoldier);

	return AI_ACTION_COWER;
}

static INT8 AIHandleBlockedEscape(SOLDIERTYPE *pSoldier, BOOLEAN fCanMove)
{
	INT8 bSurvivalAction = DecideHopelessSurvivorAction(pSoldier, fCanMove);
	if (bSurvivalAction != AI_ACTION_NONE)
	{
		AIResetEscapeBlockedTurns(pSoldier);
		return bSurvivalAction;
	}

	UINT8 ubBlockedTurns = AIRegisterEscapeBlockedTurn(pSoldier);
	INT8 bCapitulation = AITryCapitulation(pSoldier, ubBlockedTurns);
	if (bCapitulation != AI_ACTION_NONE)
		return bCapitulation;

	return AI_ACTION_NONE;
}

INT8 DecideEscapeAction(SOLDIERTYPE *pSoldier, BOOLEAN fCanMove)
{
	AIMaintainEscapePlanTimeline();

	if (!pSoldier || pSoldier->bTeam != ENEMY_TEAM || !AIEscapeActive(pSoldier))
		return AI_ACTION_NONE;

	if (pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return AI_ACTION_NONE;

	UINT8 ubID = pSoldier->ubID;
	if (!gfAIEscapePlanInitialized[ubID] ||
		guiAIEscapePlanIdentity[ubID] != pSoldier->uiUniqueSoldierIdValue)
	{
		AIResetEscapePlan(pSoldier);
	}

	if (!fCanMove || pSoldier->bActionPoints != pSoldier->bInitialActionPoints)
		return AI_ACTION_NONE;

	// Tactical edge traversal is only safe for the surface strategic map. Underground
	// battles keep the escape intent; if local withdrawal/cover also fails for
	// repeated turns, a catastrophically beaten soldier may capitulate.
	if (gbWorldSectorZ != 0)
		return AIHandleBlockedEscape(pSoldier, fCanMove);

	// If we are already on any valid strategic edge, leave from here instead of
	// crossing the map just to reach an older cached plan.
	INT8 bCurrentEdge = -1;
	if (GridNoOnEdgeOfMap(pSoldier->sGridNo, &bCurrentEdge))
	{
		INT8 bCheckedCurrentEdge = bCurrentEdge;
		if (EscapeDirectionIsValid(&bCheckedCurrentEdge) &&
			bCheckedCurrentEdge == bCurrentEdge)
		{
			INT8 bExitDirection = -1;
			INT32 sExitPoint = FindNearbyPointOnEdgeOfMap(pSoldier, &bExitDirection);
			INT8 bCheckedExitDirection = bExitDirection;
			if (!TileIsOutOfBounds(sExitPoint) &&
				bExitDirection == bCurrentEdge &&
				EscapeDirectionIsValid(&bCheckedExitDirection) &&
				bCheckedExitDirection == bExitDirection)
			{
				AIResetEscapeBlockedTurns(pSoldier);
				pSoldier->ubQuoteActionID = GetTraversalQuoteActionID(bExitDirection);
				pSoldier->aiData.usActionData = sExitPoint;
				return AI_ACTION_RUN_AWAY;
			}
		}
	}

	// Validate the cached route before committing another turn to it.
	if (!TileIsOutOfBounds(gsAIEscapeTarget[ubID]) &&
		gbAIEscapeDirection[ubID] != -1 &&
		!AIEscapeTargetStillSafe(pSoldier, gsAIEscapeTarget[ubID], gbAIEscapeDirection[ubID]))
	{
		gsAIEscapeTarget[ubID] = NOWHERE;
		gbAIEscapeDirection[ubID] = -1;
	}

	UINT32 uiTurnStamp = guiTurnCnt + 1;
	if (TileIsOutOfBounds(gsAIEscapeTarget[ubID]))
	{
		if (guiAIEscapeNoRouteTurn[ubID] == uiTurnStamp)
			return AIHandleBlockedEscape(pSoldier, fCanMove);

		INT8 bDirection = -1;
		INT32 sTarget = AISelectEscapeEdge(pSoldier, &bDirection);
		if (TileIsOutOfBounds(sTarget) || bDirection == -1)
		{
			guiAIEscapeNoRouteTurn[ubID] = uiTurnStamp;
			return AIHandleBlockedEscape(pSoldier, fCanMove);
		}

		gsAIEscapeTarget[ubID] = sTarget;
		gbAIEscapeDirection[ubID] = bDirection;
		guiAIEscapeNoRouteTurn[ubID] = 0;
		AIResetEscapeBlockedTurns(pSoldier);
	}

	// Quote traversal is deliberately not armed yet. A long path can exceed JA2's
	// fixed path buffer; arming traversal early could otherwise remove a soldier
	// before he physically reaches the map edge.
	if (pSoldier->ubQuoteActionID >= QUOTE_ACTION_ID_TRAVERSE_EAST &&
		pSoldier->ubQuoteActionID <= QUOTE_ACTION_ID_TRAVERSE_NORTH)
	{
		pSoldier->ubQuoteActionID = 0;
	}

	AIResetEscapeBlockedTurns(pSoldier);
	pSoldier->aiData.usActionData = gsAIEscapeTarget[ubID];
	return AI_ACTION_RUN_AWAY;
}

static UINT8 gubMilitiaConsolidationAnchor[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiMilitiaConsolidationIdentity[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiMilitiaConsolidationUntilTurn[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiMilitiaConsolidationLastTurnStamp = 0;

static BOOLEAN AIValidMilitiaConsolidationAnchor(SOLDIERTYPE *pSoldier, SOLDIERTYPE *pFriend)
{
	if (!pSoldier || !pFriend ||
		pSoldier->bTeam != MILITIA_TEAM ||
		pFriend->bTeam != MILITIA_TEAM ||
		pFriend == pSoldier ||
		!pFriend->bActive || !pFriend->bInSector ||
		pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || pFriend->bBreathCollapsed ||
		(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
		(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
		pFriend->pathing.bLevel != pSoldier->pathing.bLevel ||
		pFriend->aiData.bUnderFire ||
		AIDisengagementActive(pFriend) ||
		PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > TACTICAL_RANGE)
	{
		return FALSE;
	}

	// Strongpoints need actual local mass, not a lone survivor in good cover.
	if (AICountNearbyOperationalFriends(pFriend, pFriend->sGridNo, DAY_VISION_RANGE / 4) < 2)
		return FALSE;

	return TRUE;
}

static SOLDIERTYPE *AISelectMilitiaConsolidationAnchor(SOLDIERTYPE *pSoldier)
{
	UINT32 uiTurnStamp = guiTurnCnt + 1;
	if (guiMilitiaConsolidationLastTurnStamp != 0 &&
		uiTurnStamp < guiMilitiaConsolidationLastTurnStamp)
	{
		for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
		{
			gubMilitiaConsolidationAnchor[i] = NOBODY;
			guiMilitiaConsolidationIdentity[i] = 0;
			guiMilitiaConsolidationUntilTurn[i] = 0;
		}
	}
	guiMilitiaConsolidationLastTurnStamp = uiTurnStamp;

	if (!pSoldier || pSoldier->bTeam != MILITIA_TEAM || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return NULL;

	UINT8 ubID = pSoldier->ubID;

	if (guiMilitiaConsolidationIdentity[ubID] != pSoldier->uiUniqueSoldierIdValue)
	{
		gubMilitiaConsolidationAnchor[ubID] = NOBODY;
		guiMilitiaConsolidationUntilTurn[ubID] = 0;
		guiMilitiaConsolidationIdentity[ubID] = pSoldier->uiUniqueSoldierIdValue;
	}

	if (guiMilitiaConsolidationUntilTurn[ubID] >= uiTurnStamp &&
		gubMilitiaConsolidationAnchor[ubID] != NOBODY &&
		MercPtrs[gubMilitiaConsolidationAnchor[ubID]] &&
		AIValidMilitiaConsolidationAnchor(pSoldier, MercPtrs[gubMilitiaConsolidationAnchor[ubID]]))
	{
		return MercPtrs[gubMilitiaConsolidationAnchor[ubID]];
	}

	SOLDIERTYPE *pBest = NULL;
	INT32 iBestScore = -10000;

	for (UINT8 iCounter = gTacticalStatus.Team[MILITIA_TEAM].bFirstID;
		iCounter <= gTacticalStatus.Team[MILITIA_TEAM].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!AIValidMilitiaConsolidationAnchor(pSoldier, pFriend))
			continue;

		UINT8 ubSupport = AICountNearbyOperationalFriends(pFriend, pFriend->sGridNo, DAY_VISION_RANGE / 4);
		UINT16 usExposure = AIKnownThreatExposure(pSoldier, pFriend->sGridNo, pFriend->pathing.bLevel);
		UINT8 ubAdjacent = NumberOfTeamMatesAdjacent(pFriend, pFriend->sGridNo);
		INT32 iDistance = PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo);

		INT32 iScore = 22 * __min((INT32)4, (INT32)ubSupport);
		if (AnyCoverAtSpot(pFriend, pFriend->sGridNo))
			iScore += 25;
		if (SightCoverAtSpot(pFriend, pFriend->sGridNo, FALSE))
			iScore += 15;
		if (AICheckIsOfficer(pFriend) || AICheckIsCommander(pFriend))
			iScore += 12;

		iScore -= __min((INT32)40, (INT32)usExposure / 8);
		iScore -= __min((INT32)20, iDistance / 2);

		// Avoid turning a strongpoint into an obvious grenade cluster.
		if (ubAdjacent >= 3)
			iScore -= 18;

		// Bounded variability: close alternatives can win, obviously bad positions cannot.
		iScore += (INT32)PreRandom(21) - 10;

		if (iScore > iBestScore)
		{
			iBestScore = iScore;
			pBest = pFriend;
		}
	}

	if (pBest)
	{
		gubMilitiaConsolidationAnchor[ubID] = pBest->ubID;
		// Hold the choice briefly so repeated AI sub-decisions do not thrash.
		guiMilitiaConsolidationUntilTurn[ubID] = uiTurnStamp + 1;
	}
	else
	{
		gubMilitiaConsolidationAnchor[ubID] = NOBODY;
		guiMilitiaConsolidationUntilTurn[ubID] = 0;
	}

	return pBest;
}

static INT8 DecideMilitiaDefensiveConsolidation(SOLDIERTYPE *pSoldier, BOOLEAN fCanMove)
{
	if (!pSoldier || pSoldier->bTeam != MILITIA_TEAM ||
		!fCanMove || pSoldier->bActionPoints != pSoldier->bInitialActionPoints ||
		pSoldier->stats.bLife < OKLIFE || pSoldier->bCollapsed)
	{
		return AI_ACTION_NONE;
	}

	INT8 bSituation = AIBattleSituation(pSoldier);
	UINT8 ubCasualties = AIFriendlyCasualtyPercent(pSoldier);
	UINT8 ubRoutPressure = AILocalRoutPressure(pSoldier);

	BOOLEAN fBadlyLosing =
		bSituation == AI_BATTLE_CATASTROPHIC ||
		(bSituation == AI_BATTLE_LOSING &&
		 (ubCasualties >= 25 || AILocalStress(pSoldier) >= 35 || ubRoutPressure >= 40));

	if (!fBadlyLosing)
		return AI_ACTION_NONE;

	UINT8 ubCurrentSupport = AICountNearbyOperationalFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 4);
	UINT16 usCurrentExposure = AIKnownThreatExposure(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
	BOOLEAN fCurrentDefensible =
		ubCurrentSupport >= 2 &&
		AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) &&
		(usCurrentExposure < 120 || SightCoverAtSpot(pSoldier, pSoldier->sGridNo, FALSE));

	// Once the militia reaches a defensible local concentration, hold the line.
	// Do not keep bouncing around the map just because the global fight is bad.
	if (fCurrentDefensible)
		return AI_ACTION_NONE;

	SOLDIERTYPE *pAnchor = AISelectMilitiaConsolidationAnchor(pSoldier);
	if (!pAnchor)
		return AI_ACTION_NONE;

	INT32 sMove = InternalGoAsFarAsPossibleTowards(
		pSoldier, pAnchor->sGridNo, 0, AI_ACTION_SEEK_FRIEND, 0);

	if (TileIsOutOfBounds(sMove) || sMove == pSoldier->sGridNo)
		return AI_ACTION_NONE;

	UINT16 usMoveExposure = AIKnownThreatExposure(pSoldier, sMove, pSoldier->pathing.bLevel);
	UINT8 ubMoveSupport = AICountNearbyOperationalFriends(pSoldier, sMove, DAY_VISION_RANGE / 4);

	// Consolidation may accept a small temporary exposure increase if it clearly
	// buys local support, but never a dramatic run into a known kill zone.
	if (usMoveExposure > usCurrentExposure + 80 &&
		ubMoveSupport <= ubCurrentSupport)
	{
		return AI_ACTION_NONE;
	}

	pSoldier->aiData.usActionData = sMove;
	return AI_ACTION_SEEK_FRIEND;
}

INT8 DecideDisengagementAction(SOLDIERTYPE *pSoldier, BOOLEAN fCanMove)
{
	if (!AICombatTeam(pSoldier))
		return AI_ACTION_NONE;

	BOOLEAN fDisengaging = AIUpdateDisengagementState(pSoldier);
	BOOLEAN fEscaping = AIEscapeActive(pSoldier);

	// Do not reuse an old edge plan if this soldier recovered and only later
	// entered a new escape episode.
	if (pSoldier->bTeam == ENEMY_TEAM && !fEscaping)
		AIResetEscapePlan(pSoldier);

	if (!fDisengaging && !fEscaping)
		return AI_ACTION_NONE;

	// Militia normally consolidate inside the sector. Only a genuinely collapsing
	// force with a valid reinforced fallback is allowed to abandon the sector.
	if (pSoldier->bTeam == MILITIA_TEAM && fDisengaging)
	{
		INT8 bStrategicRetreat = DecideMilitiaStrategicRetreatAction(pSoldier, fCanMove);
		if (bStrategicRetreat != AI_ACTION_NONE)
			return bStrategicRetreat;

		INT8 bConsolidateAction = DecideMilitiaDefensiveConsolidation(pSoldier, fCanMove);
		if (bConsolidateAction != AI_ACTION_NONE)
			return bConsolidateAction;
	}

	INT8 bEscapeAction = DecideEscapeAction(pSoldier, fCanMove);
	if (bEscapeAction != AI_ACTION_NONE)
		return bEscapeAction;

	// If a nearby buddy has just broken contact, one suitable soldier remains
	// temporarily as rear guard. He keeps the disengagement state, but skips his
	// own withdrawal movement this turn so ordinary fire/suppression can cover
	// the buddy. Immediate personal danger or full escape disqualifies him inside
	// AIShouldHoldForWithdrawingFriend().
	if (fDisengaging && !fEscaping &&
		fCanMove &&
		pSoldier->bActionPoints == pSoldier->bInitialActionPoints &&
		AIShouldHoldForWithdrawingFriend(pSoldier))
	{
		return AI_ACTION_NONE;
	}

	// Escape remains a persistent intent even after contact is broken. If there is
	// no movement this decision (for example AP already spent), allow normal
	// defensive fire rather than restarting ordinary seek/advance behaviour.
	if (!fDisengaging)
		return AI_ACTION_NONE;

	if (!fCanMove || pSoldier->bActionPoints != pSoldier->bInitialActionPoints)
		return AI_ACTION_NONE;

	INT32 sThreat = ClosestKnownOpponent(pSoldier, NULL, NULL);
	if (TileIsOutOfBounds(sThreat))
	{
		// Contact has been broken. Stay disengaged briefly instead of immediately
		// seeking the stale contact again.
		return AI_ACTION_NONE;
	}

	UINT16 usCurrentExposure = AIKnownThreatExposure(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
	INT32 iCurrentSupport = AICountNearbyOperationalFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 2);

	INT32 sFallback = FindRetreatSpot(pSoldier);
	if (TileIsOutOfBounds(sFallback))
		sFallback = FindFlankingSpot(pSoldier, sThreat, AI_ACTION_WITHDRAW);
	if (!TileIsOutOfBounds(sFallback))
	{
		UINT16 usFallbackExposure = AIKnownThreatExposure(pSoldier, sFallback, pSoldier->pathing.bLevel);
		INT32 iFallbackSupport = AICountNearbyOperationalFriends(pSoldier, sFallback, DAY_VISION_RANGE / 2);
		BOOLEAN fCurrentSightCover = SightCoverAtSpot(pSoldier, pSoldier->sGridNo, FALSE);
		BOOLEAN fFallbackSightCover = SightCoverAtSpot(pSoldier, sFallback, FALSE);

		if (usFallbackExposure <= usCurrentExposure ||
			(fFallbackSightCover && !fCurrentSightCover) ||
			iFallbackSupport > iCurrentSupport)
		{
			pSoldier->aiData.usActionData = sFallback;
			return AI_ACTION_WITHDRAW;
		}
	}

	INT32 iCoverPercentBetter = 0;
	INT32 sCover = FindBestNearbyCover(pSoldier, pSoldier->aiData.bAIMorale, &iCoverPercentBetter);
	if (!TileIsOutOfBounds(sCover) && sCover != pSoldier->sGridNo)
	{
		UINT16 usCoverExposure = AIKnownThreatExposure(pSoldier, sCover, pSoldier->pathing.bLevel);
		if (usCoverExposure <= usCurrentExposure)
		{
			pSoldier->aiData.usActionData = sCover;
			return AI_ACTION_TAKE_COVER;
		}
	}

	return AI_ACTION_NONE;
}
INT8 DecideTacticalFallback(SOLDIERTYPE *pSoldier, BOOLEAN fCanMove)
{
	if (!AICombatTeam(pSoldier) || !fCanMove || pSoldier->IsZombie() ||
		!AIShouldConsiderTacticalFallback(pSoldier) ||
		pSoldier->bActionPoints != pSoldier->bInitialActionPoints)
	{
		return AI_ACTION_NONE;
	}

	INT32 sThreat = ClosestKnownOpponent(pSoldier, NULL, NULL);
	if (TileIsOutOfBounds(sThreat))
		return AI_ACTION_NONE;

	INT32 sFallback = FindRetreatSpot(pSoldier);
	if (TileIsOutOfBounds(sFallback))
		sFallback = FindFlankingSpot(pSoldier, sThreat, AI_ACTION_WITHDRAW);
	if (TileIsOutOfBounds(sFallback))
		return AI_ACTION_NONE;

	BOOLEAN fCurrentCover = AnyCoverAtSpot(pSoldier, pSoldier->sGridNo);
	BOOLEAN fFallbackCover = AnyCoverAtSpot(pSoldier, sFallback);
	BOOLEAN fCurrentSightCover = SightCoverAtSpot(pSoldier, pSoldier->sGridNo, FALSE);
	BOOLEAN fFallbackSightCover = SightCoverAtSpot(pSoldier, sFallback, FALSE);
	UINT16 usCurrentExposure = AIKnownThreatExposure(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
	UINT16 usFallbackExposure = AIKnownThreatExposure(pSoldier, sFallback, pSoldier->pathing.bLevel);
	INT32 iCurrentSupport = AICountNearbyOperationalFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 2);
	INT32 iFallbackSupport = AICountNearbyOperationalFriends(pSoldier, sFallback, DAY_VISION_RANGE / 2);
	INT32 iCurrentDistance = PythSpacesAway(pSoldier->sGridNo, sThreat);
	INT32 iFallbackDistance = PythSpacesAway(sFallback, sThreat);

	INT32 iGain = 0;
	if (fFallbackCover && !fCurrentCover) iGain += 25;
	if (!fFallbackCover && fCurrentCover) iGain -= 25;
	if (fFallbackSightCover && !fCurrentSightCover) iGain += 20;
	if (!fFallbackSightCover && fCurrentSightCover) iGain -= 20;

	// Lower exposure to known contacts is the strongest positional improvement.
	if (usFallbackExposure < usCurrentExposure)
		iGain += __min((INT32)35, (INT32)(usCurrentExposure - usFallbackExposure) / 3);
	else if (usFallbackExposure > usCurrentExposure)
		iGain -= __min((INT32)35, (INT32)(usFallbackExposure - usCurrentExposure) / 3);

	iGain += 8 * (__min(iFallbackSupport, 3) - __min(iCurrentSupport, 3));

	// Extra separation is valuable when the current weapon/optic wants more standoff.
	if (AIEngagementRangeModifier(pSoldier, sThreat) < 0 && iFallbackDistance > iCurrentDistance)
		iGain += __min((INT32)20, 3 * (iFallbackDistance - iCurrentDistance));

	// Under direct pressure, a modest improvement is enough; otherwise require a
	// clearly better position so the AI does not shuffle backwards every turn.
	INT32 iRequiredGain = (pSoldier->aiData.bUnderFire || !fCurrentCover ||
		AILocalStress(pSoldier) >= 35) ? 8 : 18;

	if (iGain < iRequiredGain)
		return AI_ACTION_NONE;

	pSoldier->aiData.usActionData = sFallback;
	return AI_ACTION_WITHDRAW;
}
INT8 DecideHopelessSurvivorAction(SOLDIERTYPE *pSoldier, BOOLEAN fCanMove)
{
	if (!AICombatTeam(pSoldier) || !fCanMove || pSoldier->IsZombie() ||
		!AIShouldAvoidAdvance(pSoldier) ||
		pSoldier->bActionPoints != pSoldier->bInitialActionPoints)
	{
		return AI_ACTION_NONE;
	}

	INT32 sThreat = ClosestKnownOpponent(pSoldier, NULL, NULL);
	if (TileIsOutOfBounds(sThreat))
		return AI_ACTION_NONE;

	// First try an existing bounded tactical withdrawal. This does not flee the
	// sector; it simply increases separation while preferring cover and support.
	if (pSoldier->aiData.bOrders != STATIONARY)
	{
		INT32 sFallback = FindRetreatSpot(pSoldier);
	if (TileIsOutOfBounds(sFallback))
		sFallback = FindFlankingSpot(pSoldier, sThreat, AI_ACTION_WITHDRAW);
		if (!TileIsOutOfBounds(sFallback))
		{
			UINT16 usCurrentExposure = AIKnownThreatExposure(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
			UINT16 usFallbackExposure = AIKnownThreatExposure(pSoldier, sFallback, pSoldier->pathing.bLevel);

			if (usFallbackExposure <= usCurrentExposure)
			{
				pSoldier->aiData.usActionData = sFallback;
				return AI_ACTION_WITHDRAW;
			}
		}
	}

	// If withdrawal has no acceptable route (or orders forbid leaving the post),
	// improve the defensive position instead of charging superior known opposition.
	INT32 iCoverPercentBetter = 0;
	INT32 sCover = FindBestNearbyCover(pSoldier, pSoldier->aiData.bAIMorale, &iCoverPercentBetter);
	if (!TileIsOutOfBounds(sCover) && sCover != pSoldier->sGridNo)
	{
		UINT16 usCurrentExposure = AIKnownThreatExposure(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
		UINT16 usCoverExposure = AIKnownThreatExposure(pSoldier, sCover, pSoldier->pathing.bLevel);
		if (usCoverExposure <= usCurrentExposure)
		{
			pSoldier->aiData.usActionData = sCover;
			return AI_ACTION_TAKE_COVER;
		}
	}

	return AI_ACTION_NONE;
}
// Break dangerous clusters under fire. Existing cover scoring already dislikes
// adjacent teammates; this decision makes that preference urgent when several soldiers
// are packed together and the local group is taking fire.
INT8 DecideCombatDispersion(SOLDIERTYPE *pSoldier)
{
	if (!gfTurnBasedAI || !pSoldier || !AICombatTeam(pSoldier) ||
		!SoldierAI(pSoldier) || pSoldier->stats.bLife < OKLIFE ||
		pSoldier->bCollapsed || pSoldier->bBreathCollapsed ||
		(pSoldier->usSoldierFlagMask & SOLDIER_POW) ||
		(pSoldier->flags.uiStatusFlags & SOLDIER_COWERING) ||
		pSoldier->IsFlanking() ||
		AIDisengagementActive(pSoldier) ||
		AIEscapeActive(pSoldier) || AIShouldStartEscape(pSoldier))
	{
		return AI_ACTION_NONE;
	}

	UINT8 ubAdjacent = NumberOfTeamMatesAdjacent(pSoldier, pSoldier->sGridNo);
	if (ubAdjacent < 2)
		return AI_ACTION_NONE;

	BOOLEAN fLocalPressure = pSoldier->aiData.bUnderFire;
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID && !fLocalPressure; iCounter++)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier || !pFriend->bActive || !pFriend->bInSector ||
			pFriend->stats.bLife < OKLIFE ||
			pFriend->bCollapsed ||
			pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING))
		{
			continue;
		}

		if (PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) <= 2 &&
			(pFriend->aiData.bUnderFire || ShockLevelPercent(pFriend) >= 30))
		{
			fLocalPressure = TRUE;
		}
	}

	if (!fLocalPressure)
		return AI_ACTION_NONE;

	INT32 iCoverPercentBetter = 0;
	INT32 sDisperseSpot = FindBestNearbyCover(pSoldier, pSoldier->aiData.bAIMorale, &iCoverPercentBetter);
	if (TileIsOutOfBounds(sDisperseSpot))
		return AI_ACTION_NONE;

	UINT8 ubNewAdjacent = NumberOfTeamMatesAdjacent(pSoldier, sDisperseSpot);
	if (ubNewAdjacent >= ubAdjacent)
		return AI_ACTION_NONE;

	// Do not break a cluster by moving from a protected tile into a position the
	// known enemy can attack. Dispersion is useful only if it is not tactically worse.
	if (AIKnownThreatExposure(pSoldier, sDisperseSpot, pSoldier->pathing.bLevel) >
		AIKnownThreatExposure(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel))
	{
		return AI_ACTION_NONE;
	}

	pSoldier->aiData.usActionData = sDisperseSpot;
	return AI_ACTION_TAKE_COVER;
}

extern UINT32 guiTurnCnt;
extern UINT32 guiReinforceTurn;
extern UINT32 guiArrived;

void LogDecideInfo(SOLDIERTYPE *pSoldier)
{
	DebugAI(AI_MSG_INFO, pSoldier, String("Turn num %d aware %d", guiTurnCnt, gTacticalStatus.Team[pSoldier->bTeam].bAwareOfOpposition));
	DebugAI(AI_MSG_INFO, pSoldier, String("current team %d interrupt (top message) %d interrupt occurred %d", gTacticalStatus.ubCurrentTeam, AICheckInterrupt(), gTacticalStatus.fInterruptOccurred));
	DebugAI(AI_MSG_INFO, pSoldier, String("AP=%d/%d %s %s %s %s %s", pSoldier->bActionPoints, pSoldier->bInitialActionPoints, gStr8AlertStatus[pSoldier->aiData.bAlertStatus], gStr8Orders[pSoldier->aiData.bOrders], gStr8Attitude[pSoldier->aiData.bAttitude], gStr8Team[pSoldier->bTeam], gStr8Class[pSoldier->ubSoldierClass]));
	DebugAI(AI_MSG_INFO, pSoldier, String("Health %d/%d Breath %d/%d Shock %d Tolerance %d AI Morale %d Morale %d", pSoldier->stats.bLife, pSoldier->stats.bLifeMax, pSoldier->bBreath, pSoldier->bBreathMax, pSoldier->aiData.bShock, CalcSuppressionTolerance(pSoldier), pSoldier->aiData.bAIMorale, pSoldier->aiData.bMorale));
	DebugAI(AI_MSG_INFO, pSoldier, String("Spot %d level %d opponents %d", pSoldier->sGridNo, pSoldier->pathing.bLevel, pSoldier->aiData.bOppCnt));
	DebugAI(AI_MSG_INFO, pSoldier, String("ubServiceCount %d ubServicePartner %d fDoingSurgery %d", pSoldier->ubServiceCount, pSoldier->ubServicePartner, pSoldier->fDoingSurgery));
	if (pSoldier->IsCowering())
	{
		DebugAI(AI_MSG_INFO, pSoldier, String("Cowering"));
	}
	if (pSoldier->IsGivingAid())
	{
		DebugAI(AI_MSG_INFO, pSoldier, String("Giving aid"));
	}
	//CHAR8 str8[1024];

	// show watched locations
	INT8	bLoop;
	for (bLoop = 0; bLoop < NUM_WATCHED_LOCS; bLoop++)
	{
		if (!TileIsOutOfBounds(gsWatchedLoc[pSoldier->ubID][bLoop]))
		{
			DebugAI(AI_MSG_INFO, pSoldier, String("Watched location %d level %d points %d", gsWatchedLoc[pSoldier->ubID][bLoop], gbWatchedLocLevel[pSoldier->ubID][bLoop], gubWatchedLocPoints[pSoldier->ubID][bLoop]));
		}
	}

	LogKnowledgeInfo(pSoldier);

	DebugAI(AI_MSG_INFO, pSoldier, String("What I know %d", WhatIKnowThatPublicDont(pSoldier, FALSE)));
	DebugAI(AI_MSG_INFO, pSoldier, String("Has Gun %d, Short range weapon %d, Gun Range %d, Gun Ammo %d, Gun Scoped %d ", AICheckHasGun(pSoldier), AICheckShortWeaponRange(pSoldier), AIGunRange(pSoldier), AIGunAmmo(pSoldier), AIGunScoped(pSoldier)));
	//DebugAI(AI_MSG_INFO, pSoldier, String("StopSeek %d RushAttack %d RetreatCounter %d", pSoldier->StopSeekValue(), pSoldier->RushAttackValue(), pSoldier->RetreatValue()));
}

void LogKnowledgeInfo(SOLDIERTYPE *pSoldier)
{
	//CHAR8 str8[1024];
	//memset(str8, 0, 1024 * sizeof(char));

	// show public opponents
	for (UINT16 oppID = 0; oppID < MAX_NUM_SOLDIERS; oppID++)
	{
		if (gbPublicOpplist[pSoldier->bTeam][oppID] != NOT_HEARD_OR_SEEN &&
			!MercPtrs[oppID]->aiData.bNeutral)
		{
			//wcstombs(str8, MercPtrs[oppID]->GetName(), wcslen(MercPtrs[oppID]->GetName())+1);
			//wcstombs(str8, MercPtrs[oppID]->GetName(), 1024 - 1);
			DebugAI(AI_MSG_INFO, pSoldier, String("public opponent [%d] knowledge %s gridno %d level %d", oppID, gStr8Knowledge[gbPublicOpplist[pSoldier->bTeam][oppID] - OLDEST_HEARD_VALUE], gsPublicLastKnownOppLoc[pSoldier->bTeam][oppID], gbPublicLastKnownOppLevel[pSoldier->bTeam][oppID]));
			//swprintf( pStrInfo, L"%s[%d] %s %s\n", pStrInfo, oppID, MercPtrs[oppID]->GetName(), SeenStr(gbPublicOpplist[pSoldier->bTeam][oppID]) );
		}
	}
	// show personal opponents
	for (UINT16 oppID = 0; oppID < MAX_NUM_SOLDIERS; oppID++)
	{
		if (pSoldier->aiData.bOppList[oppID] != NOT_HEARD_OR_SEEN &&
			!MercPtrs[oppID]->aiData.bNeutral)
		{
			//wcstombs(str8, MercPtrs[oppID]->GetName(), wcslen(MercPtrs[oppID]->GetName())+1);
			//wcstombs(str8, MercPtrs[oppID]->GetName(), 1024 - 1);
			DebugAI(AI_MSG_INFO, pSoldier, String("personal opponent [%d] knowledge %s gridno %d level %d", oppID, gStr8Knowledge[pSoldier->aiData.bOppList[oppID] - OLDEST_HEARD_VALUE], gsLastKnownOppLoc[pSoldier->ubID][oppID], gbLastKnownOppLevel[pSoldier->ubID][oppID]));
			//swprintf( pStrInfo, L"%s[%d] %s %s\n", pStrInfo, oppID, MercPtrs[oppID]->GetName(), SeenStr(pSoldier->aiData.bOppList[oppID]) );
		}
	}
}
