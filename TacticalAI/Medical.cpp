#ifdef PRECOMPILEDHEADERS
	#include "AI All.h"
#else
	#include "types.h"

	#include "Soldier Functions.h"
	#include "Points.h"
	#include "ai.h"
	#include "AIInternals.h"
	#include "Animation Control.h"
	#include "pathai.h"
	#include "overhead.h"
	#include "items.h"
	#include "Message.h"
	#include "Buildings.h"
	#include "worldman.h"
	#include "Assignments.h"
	// added by SANDRO
	#include "Soldier Profile.h"
	#include "GameSettings.h"
#endif

//forward declarations of common classes to eliminate includes
class OBJECTTYPE;
class SOLDIERTYPE;


extern BOOLEAN gfAutoBandageFailed;

//
// This file contains code devoted to the player AI-controlled medical system.	Maybe it
// can be used or adapted for the enemies too...
//

#define NOT_GOING_TO_DIE -1
#define NOT_GOING_TO_COLLAPSE -1

// can this grunt be bandaged by a teammate?
BOOLEAN CanCharacterBeAutoBandagedByTeammate( SOLDIERTYPE *pSoldier );

//c an this grunt help anyone else out?
BOOLEAN CanCharacterAutoBandageTeammate( SOLDIERTYPE *pSoldier );

BOOLEAN FindAutobandageClimbPoint( INT32 sDesiredGridNo, BOOLEAN fClimbUp )
{
	// checks for existance of location to climb up to building, not occupied by a medic
	BUILDING *	pBuilding;
	UINT8				ubNumClimbSpots;
	UINT8 ubLoop;
	UINT8				ubWhoIsThere;

	pBuilding = FindBuilding( sDesiredGridNo );
	if (!pBuilding)
	{
		return( FALSE );
	}

	ubNumClimbSpots = pBuilding->ubNumClimbSpots;

	for ( ubLoop = 0; ubLoop < ubNumClimbSpots; ubLoop++ )
	{
		ubWhoIsThere = WhoIsThere2( pBuilding->sUpClimbSpots[ ubLoop ], 1 );
		if ( ubWhoIsThere != NOBODY && !CanCharacterAutoBandageTeammate( MercPtrs[ ubWhoIsThere ] ) )
		{
			continue;
		}
		ubWhoIsThere = WhoIsThere2( pBuilding->sDownClimbSpots[ ubLoop ], 0 );
		if ( ubWhoIsThere != NOBODY && !CanCharacterAutoBandageTeammate( MercPtrs[ ubWhoIsThere ] ) )
		{
			continue;
		}
		return( TRUE );
	}

	return( FALSE );
}

BOOLEAN FullPatientCheck( SOLDIERTYPE * pPatient )
{
	UINT8						cnt;
	SOLDIERTYPE *		pSoldier;

	if ( CanCharacterAutoBandageTeammate( pPatient ) )
	{
		// can bandage self!
		return( TRUE );
	}

	if ( pPatient->pathing.bLevel != 0 )
	{	// look for a clear spot for jumping up

		// special "closest" search that ignores climb spots IF they are occupied by non-medics
		return( FindAutobandageClimbPoint( pPatient->sGridNo, TRUE ) );
	}
	else
	{
		// run though the list of chars on team
		cnt = gTacticalStatus.Team[ gbPlayerNum ].bFirstID;
		for ( pSoldier = MercPtrs[ cnt ]; cnt <= gTacticalStatus.Team[ gbPlayerNum ].bLastID; cnt++,pSoldier++)
		{
			// can this character help out?
			if ( CanCharacterAutoBandageTeammate( pSoldier ) == TRUE )
			{
				// can this guy path to the patient?
				if ( pSoldier->pathing.bLevel == 0 )
				{
					// do a regular path check
					if ( FindBestPath( pSoldier, pPatient->sGridNo, 0, WALKING, NO_COPYROUTE, PATH_THROUGH_PEOPLE ) )
					{
						return( TRUE );
					}
				}
				else
				{
					// if on different levels, assume okay
					return( TRUE );
				}
			}
		}
	}
	return( FALSE );
}

BOOLEAN CanAutoBandage( BOOLEAN fDoFullCheck )
{
	// returns false if we should stop being in auto-bandage mode
	UINT8					cnt;
	UINT8					ubMedics = 0, ubPatients = 0;
	SOLDIERTYPE * pSoldier;
	static UINT8	ubIDForFullCheck = NOBODY;

	// run though the list of chars on team
	cnt = gTacticalStatus.Team[ gbPlayerNum ].bFirstID;
	for ( pSoldier = MercPtrs[ cnt ]; cnt <= gTacticalStatus.Team[ gbPlayerNum ].bLastID; cnt++,pSoldier++)
	{
		// can this character help out?
		if( CanCharacterAutoBandageTeammate( pSoldier ) == TRUE )
		{
			// yep, up the number of medics in sector
			ubMedics++;
		}
	}

	if ( ubMedics == 0 )
	{
		// no one that can help
		return( FALSE );
	}

	cnt = gTacticalStatus.Team[ gbPlayerNum ].bFirstID;
	for ( pSoldier = MercPtrs[ cnt ]; cnt <= gTacticalStatus.Team[ gbPlayerNum ].bLastID; cnt++,pSoldier++)
	{
		// can this character be helped out by a teammate?
		if ( CanCharacterBeAutoBandagedByTeammate( pSoldier ) == TRUE )
		{
			// yep, up the number of patients awaiting treatment in sector
			ubPatients++;
			if (fDoFullCheck)
			{
				if ( ubIDForFullCheck == NOBODY )
				{
					// do this guy NEXT time around
					ubIDForFullCheck = cnt;
				}
				else if ( cnt == ubIDForFullCheck )
				{
					// test this guy
					if ( FullPatientCheck( pSoldier ) == FALSE )
					{
						// shit!
						gfAutoBandageFailed = TRUE;
						return( FALSE );
					}
					// set ID for full check to NOBODY; will be set to someone later in loop, or to
					// the first guy on the next pass
					ubIDForFullCheck = NOBODY;
				}
			}
		}
		// is this guy REACHABLE??
	}

	if ( ubPatients == 0 )
	{
		// there is no one to help
		return( FALSE );
	}
	else
	{
		// got someone that can help and help wanted
		return( TRUE );
	}
}


BOOLEAN CanCharacterAutoBandageTeammate( SOLDIERTYPE *pSoldier )
// can this soldier autobandage others in sector
{
	// if the soldier isn't active or in sector, we have problems..leave
	if ( !(pSoldier->bActive) || !(pSoldier->bInSector) || ( pSoldier->flags.uiStatusFlags & SOLDIER_VEHICLE ) || (pSoldier->bAssignment == VEHICLE ) )
	{
		return( FALSE );
	}

	// they must have oklife or more, not be collapsed, have some level of medical competence, and have a med kit of some sort
	if ( (pSoldier->stats.bLife >= OKLIFE) && !(pSoldier->bCollapsed) && (pSoldier->stats.bMedical > 0) && (FindObjClass( pSoldier, IC_MEDKIT ) != NO_SLOT) )
	{
		return( TRUE );
	}

	return( FALSE );
}


// can this soldier autobandage others in sector
BOOLEAN CanCharacterBeAutoBandagedByTeammate( SOLDIERTYPE *pSoldier )
{
	// if the soldier isn't active or in sector, we have problems..leave
	if ( !(pSoldier->bActive) || !(pSoldier->bInSector) || ( pSoldier->flags.uiStatusFlags & SOLDIER_VEHICLE ) || (pSoldier->bAssignment == VEHICLE ) )
	{
		return( FALSE );
	}

	if ( (pSoldier->stats.bLife > 0) && (pSoldier->bBleeding > 0) )
	{
		// someone's bleeding and not being given first aid!
		return( TRUE );
	}

	return( FALSE );
}

INT8 FindBestPatient( SOLDIERTYPE * pSoldier, BOOLEAN * pfDoClimb )
{
	UINT8						cnt, cnt2;
	INT32						bBestPriority = 0, sBestAdjGridNo = NOWHERE;
	INT32						sPatientGridNo = NOWHERE, sBestPatientGridNo = NOWHERE;
	INT16						sShortestPath = 1000, sPathCost, sOtherMedicPathCost;
	SOLDIERTYPE *		pPatient;
	SOLDIERTYPE *		pBestPatient = NULL;
	SOLDIERTYPE *		pOtherMedic;
	INT8						bPatientPriority;
	UINT8						ubDirection;
	INT32 sAdjustedGridNo, sAdjacentGridNo, sOtherAdjacentGridNo;
	INT32						sClimbGridNo, sBestClimbGridNo = NOWHERE, sShortestClimbPath = 1000;
	BOOLEAN					fClimbingNecessary;

	gubGlobalPathFlags = PATH_THROUGH_PEOPLE;

	// search for someone who needs aid
	cnt = gTacticalStatus.Team[ OUR_TEAM ].bFirstID;
	for ( pPatient = MercPtrs[ cnt ]; cnt <= gTacticalStatus.Team[ OUR_TEAM ].bLastID; cnt++,pPatient++)
	{
		if ( !(pPatient->bActive) || !(pPatient->bInSector) )
		{
			continue; // NEXT!!!
		}

		if (pPatient->stats.bLife > 0 && pPatient->bBleeding && pPatient->ubServiceCount == 0)
		{
			if (pPatient->stats.bLife < OKLIFE)
			{
				bPatientPriority = 3;
			}
			else if (pPatient->stats.bLife < OKLIFE * 2)
			{
				bPatientPriority = 2;
			}
			else
			{
				bPatientPriority = 1;
			}

			if (bPatientPriority >= bBestPriority)
			{
				if ( !ClimbingNecessary( pSoldier, pPatient->sGridNo, pPatient->pathing.bLevel ) )
				{

					sPatientGridNo = pPatient->sGridNo;
					sAdjacentGridNo = FindAdjacentGridEx( pSoldier, sPatientGridNo, &ubDirection, &sAdjustedGridNo, FALSE, FALSE );
					if ( sAdjacentGridNo == -1 && gAnimControl[ pPatient->usAnimState ].ubEndHeight == ANIM_PRONE )
					{
						// prone; could be the base tile is inaccessible but the rest isn't...
						for ( cnt2 = 0; cnt2 < NUM_WORLD_DIRECTIONS; cnt2++ )
						{
							sPatientGridNo = pPatient->sGridNo + DirectionInc( cnt2 );
							if ( WhoIsThere2( sPatientGridNo, pPatient->pathing.bLevel ) == pPatient->ubID )
							{
								// patient is also here, try this location
								sAdjacentGridNo = FindAdjacentGridEx( pSoldier, sPatientGridNo, &ubDirection, &sAdjustedGridNo, FALSE, FALSE );
								if ( sAdjacentGridNo != -1 )
								{
									break;
								}
							}
						}
					}

					if (sAdjacentGridNo != -1)
					{
						if (sAdjacentGridNo == pSoldier->sGridNo)
						{
							sPathCost = 1;
						}
						else
						{
							sPathCost = PlotPath( pSoldier, sAdjacentGridNo, FALSE, FALSE, FALSE, RUNNING, FALSE, FALSE, 0);
						}

						if ( sPathCost != 0 )
						{
							// we can get there... can anyone else?

							if ( pPatient->ubAutoBandagingMedic != NOBODY && pPatient->ubAutoBandagingMedic != pSoldier->ubID )
							{
								// only switch to this patient if our distance is closer than
								// the other medic's
								pOtherMedic = MercPtrs[ pPatient->ubAutoBandagingMedic ];
								sOtherAdjacentGridNo = FindAdjacentGridEx( pOtherMedic, sPatientGridNo, &ubDirection, &sAdjustedGridNo, FALSE, FALSE );
								if (sOtherAdjacentGridNo != -1)
								{

									if (sOtherAdjacentGridNo == pOtherMedic->sGridNo)
									{
										sOtherMedicPathCost = 1;
									}
									else
									{
										sOtherMedicPathCost = PlotPath( pOtherMedic, sOtherAdjacentGridNo, FALSE, FALSE, FALSE, RUNNING, FALSE, FALSE, 0);
									}

									if (sPathCost >= sOtherMedicPathCost)
									{
										// this patient is best served by the merc moving to them now
										continue;
									}
								}
							}

							if (bPatientPriority == bBestPriority)
							{
								// compare path distances
								if ( sPathCost > sShortestPath )
								{
									continue;
								}
							}


							sShortestPath = sPathCost;
							pBestPatient = pPatient;
							sBestPatientGridNo = sPatientGridNo;
							bBestPriority = bPatientPriority;
							sBestAdjGridNo = sAdjacentGridNo;

						}
					}

				}
				else
				{
					sClimbGridNo = NOWHERE;
					// see if guy on another building etc and we need to climb somewhere
					sPathCost = EstimatePathCostToLocation( pSoldier, pPatient->sGridNo, pPatient->pathing.bLevel, FALSE, &fClimbingNecessary, &sClimbGridNo );
					// if we can get there
					if ( sPathCost != 0 && fClimbingNecessary && sPathCost < sShortestClimbPath )
					{
						sBestClimbGridNo = sClimbGridNo;
						sShortestClimbPath = sPathCost;
					}

				}

			}

		}
	}

	gubGlobalPathFlags = 0;

	if (pBestPatient)
	{
		if (pBestPatient->ubAutoBandagingMedic != NOBODY)
		{
			// cancel that medic
			DebugAI(AI_MSG_INFO, MercPtrs[pBestPatient->ubAutoBandagingMedic], String("CancelAIAction: medic: find patient"));
			CancelAIAction( MercPtrs[ pBestPatient->ubAutoBandagingMedic ], TRUE );
		}
		pBestPatient->ubAutoBandagingMedic = pSoldier->ubID;
		*pfDoClimb = FALSE;
		if ( CardinalSpacesAway( pSoldier->sGridNo, sBestPatientGridNo ) == 1 )
		{
			pSoldier->aiData.usActionData = sBestPatientGridNo;
			return( AI_ACTION_GIVE_AID );
		}
		else
		{
			pSoldier->aiData.usActionData = sBestAdjGridNo;
			return( AI_ACTION_GET_CLOSER );
		}
	}	
	else if (!TileIsOutOfBounds(sBestClimbGridNo))
	{
		*pfDoClimb = TRUE;
		pSoldier->aiData.usActionData = sBestClimbGridNo;
		return( AI_ACTION_MOVE_TO_CLIMB );
	}
	else
	{
		return( AI_ACTION_NONE );
	}
}

// Combat medic behaviour for enemy AI.  Unlike autobandage, this runs during a
// firefight and therefore refuses rescues that would expose the medic to excessive risk.
// The decision is re-evaluated every turn, so a medic can wait for suppression/smoke
// instead of committing to a suicidal run.
// Non-medics do not become roaming battlefield doctors. A soldier with basic
// medical skill and a medkit may, however, stabilize a critically downed member
// of his own fireteam who is already adjacent when local pressure permits it.
INT8 DecideEmergencyBuddyAid(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || !AICombatTeam(pSoldier) || AICheckIsMedic(pSoldier) ||
		pSoldier->stats.bMedical <= 0 ||
		pSoldier->stats.bLife < OKLIFE || pSoldier->bCollapsed ||
		pSoldier->aiData.bUnderFire ||
		AIEscapeActive(pSoldier) || AIDisengagementActive(pSoldier) ||
		pSoldier->aiData.bAIMorale == MORALE_HOPELESS ||
		pSoldier->bActionPoints < GetAPsToBeginFirstAid(pSoldier))
	{
		return AI_ACTION_NONE;
	}

	INT8 bMedKitSlot = FindObjClass(pSoldier, IC_MEDKIT);
	if (bMedKitSlot == NO_SLOT)
		return AI_ACTION_NONE;

	// Do not stop to treat somebody in an open, known fire lane unless the local
	// position is already screened by smoke or physical cover.
	if (AIKnownThreatExposure(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel) > 0 &&
		!InSmokeNearby(pSoldier->sGridNo, pSoldier->pathing.bLevel) &&
		!AnyCoverAtSpot(pSoldier, pSoldier->sGridNo))
	{
		return AI_ACTION_NONE;
	}

	SOLDIERTYPE *pBestPatient = NULL;
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pPatient = MercPtrs[iCounter];
		if (!pPatient || pPatient == pSoldier ||
			!pPatient->bActive || !pPatient->bInSector ||
			!AISameFireteam(pSoldier, pPatient) ||
			pPatient->stats.bLife <= 0 || pPatient->stats.bLife >= OKLIFE ||
			pPatient->bBleeding <= 0 || pPatient->ubServiceCount > 0 ||
			pPatient->pathing.bLevel != pSoldier->pathing.bLevel ||
			CardinalSpacesAway(pSoldier->sGridNo, pPatient->sGridNo) != 1)
		{
			continue;
		}

		if (!pBestPatient ||
			pPatient->stats.bLife < pBestPatient->stats.bLife ||
			(pPatient->stats.bLife == pBestPatient->stats.bLife &&
			 pPatient->bBleeding > pBestPatient->bBleeding))
		{
			pBestPatient = pPatient;
		}
	}

	if (!pBestPatient)
		return AI_ACTION_NONE;

	if (bMedKitSlot != HANDPOS)
	{
		pSoldier->bSlotItemTakenFrom = bMedKitSlot;
		SwapObjs(pSoldier, HANDPOS, bMedKitSlot, TRUE);
	}

	pSoldier->aiData.usActionData = pBestPatient->sGridNo;
	return AI_ACTION_GIVE_AID;
}

INT8 DecideCombatMedicRescue(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || !AICombatTeam(pSoldier) || !AICheckIsMedic(pSoldier) ||
		pSoldier->stats.bLife < OKLIFE || pSoldier->bCollapsed ||
		AIEscapeActive(pSoldier) || AIShouldStartEscape(pSoldier) ||
		pSoldier->aiData.bAIMorale == MORALE_HOPELESS)
	{
		return AI_ACTION_NONE;
	}

	INT8 bMedKitSlot = FindObjClass(pSoldier, IC_MEDKIT);
	if (bMedKitSlot == NO_SLOT)
		return AI_ACTION_NONE;

	// Hard anti-suicide gate: a medic already beyond his own accepted risk level
	// preserves himself instead of attempting a rescue.
	INT32 iMedicRisk = AIPersonalRisk(pSoldier);
	INT32 iMedicTolerance = AIPersonalRiskTolerance(pSoldier);
	if (iMedicRisk > iMedicTolerance)
		return AI_ACTION_NONE;

	SOLDIERTYPE *pBestPatient = NULL;
	INT32 sBestPatientGrid = NOWHERE;
	INT32 sBestApproachGrid = NOWHERE;
	INT32 iBestRescueValue = 0;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; iCounter++)
	{
		SOLDIERTYPE *pPatient = MercPtrs[iCounter];
		if (!pPatient || pPatient == pSoldier || !pPatient->bActive || !pPatient->bInSector ||
			pPatient->stats.bLife <= 0 || pPatient->bBleeding <= 0 || pPatient->ubServiceCount > 0)
		{
			continue;
		}

		INT32 iUrgency = 30;
		if (pPatient->stats.bLife < OKLIFE)
			iUrgency = 100;
		else if (pPatient->stats.bLife < OKLIFE * 2)
			iUrgency = 75;
		else if (pPatient->stats.bLife < pPatient->stats.bLifeMax / 2)
			iUrgency = 55;

		iUrgency += __min((INT32)20, (INT32)pPatient->bBleeding / 2);
		if (pPatient->aiData.bUnderFire)
			iUrgency += 10;

		BOOLEAN fSameElement = AISameFireteam(pSoldier, pPatient);
		INT32 iPatientDistance = PythSpacesAway(pSoldier->sGridNo, pPatient->sGridNo);

		// Enemy medics primarily serve their own fireteam. Cross-element rescues remain
		// possible when the casualty is nearby or genuinely critical.
		if (!fSameElement &&
			pSoldier->bTeam == ENEMY_TEAM &&
			pPatient->stats.bLife >= OKLIFE &&
			iPatientDistance > TACTICAL_RANGE / 3)
		{
			continue;
		}

		if (fSameElement)
			iUrgency += 15;
		else if (pSoldier->bTeam == ENEMY_TEAM)
			iUrgency -= 10;

		UINT8 ubDirection = 0;
		INT32 sAdjustedGrid = NOWHERE;
		INT32 sApproachGrid = FindAdjacentGridEx(pSoldier, pPatient->sGridNo,
			&ubDirection, &sAdjustedGrid, FALSE, FALSE);
		if (TileIsOutOfBounds(sApproachGrid))
			continue;

		INT32 iDistance = PythSpacesAway(pSoldier->sGridNo, sApproachGrid);
		if (iDistance > DAY_VISION_RANGE / 2)
			continue;

		gubNPCAPBudget = 0;
		gubNPCDistLimit = 0;
		INT32 iPathSteps = FindBestPath(pSoldier, sApproachGrid, pSoldier->pathing.bLevel,
			RUNNING, NO_COPYROUTE, PATH_THROUGH_PEOPLE);
		if (iPathSteps == 0)
		{
			continue;
		}

		// Inspect the candidate route without replacing the soldier's active route.
		// Exposed tiles that known enemies can attack are heavily penalized; a long
		// open sprint is therefore rejected even for a dying ally.
		INT32 iPathExposure = 0;
		INT32 sCheckGrid = pSoldier->sGridNo;
		for (INT32 iStep = 0; iStep < iPathSteps && iStep < MAX_PATH_LIST_SIZE; ++iStep)
		{
			sCheckGrid = NewGridNo(sCheckGrid,
				DirectionInc((UINT8)guiPathingData[iStep]));
			if (TileIsOutOfBounds(sCheckGrid))
				break;

			if (AIKnownThreatExposure(pSoldier, sCheckGrid, pSoldier->pathing.bLevel) > 0)
			{
				if (InSmokeNearby(sCheckGrid, pSoldier->pathing.bLevel))
					iPathExposure += 1;
				else
					iPathExposure += AnyCoverAtSpot(pSoldier, sCheckGrid) ? 3 : 7;
			}
		}

		UINT8 ubSupport = CountNearbyFriends(pSoldier, pPatient->sGridNo, DAY_VISION_RANGE / 4);
		BOOLEAN fDestinationAttackable =
			(AIKnownThreatExposure(pSoldier, sApproachGrid, pSoldier->pathing.bLevel) > 0);
		BOOLEAN fDestinationCovered = AnyCoverAtSpot(pSoldier, sApproachGrid);
		BOOLEAN fDestinationScreened = InSmokeNearby(sApproachGrid, pSoldier->pathing.bLevel);

		// Absolute veto: do not cross a long exposed fire lane or enter an exposed,
		// attackable casualty position without somebody nearby to support the rescue.
		if (iPathExposure >= 28 ||
			(fDestinationAttackable && !fDestinationCovered && !fDestinationScreened && ubSupport == 0))
		{
			continue;
		}

		INT32 iRescueRisk = iMedicRisk + iDistance * 2 + iPathExposure;
		if (fDestinationAttackable && !fDestinationScreened)
			iRescueRisk += 12;
		if (!fDestinationCovered && !fDestinationScreened)
			iRescueRisk += 10;
		if (fDestinationScreened)
			iRescueRisk -= 6;
		if (pPatient->aiData.bUnderFire)
			iRescueRisk += 8;
		iRescueRisk -= 5 * __min((INT32)ubSupport, 3);

		INT32 iRescueValue = iUrgency - iRescueRisk;
		if (iRescueValue < 15 || iRescueValue <= iBestRescueValue)
			continue;

		pBestPatient = pPatient;
		sBestPatientGrid = sAdjustedGrid;
		sBestApproachGrid = sApproachGrid;
		iBestRescueValue = iRescueValue;
	}

	if (!pBestPatient)
		return AI_ACTION_NONE;

	// Adjacent: commit to treatment. Keep the gun available until this point so a
	// medic moving toward a casualty does not run through combat holding a medkit.
	if (pSoldier->sGridNo == sBestApproachGrid)
	{
		// Do not select GIVE_AID when the action executor would reject it for lack
		// of AP and immediately end this AI soldier's turn. If treatment cannot
		// begin now, fall back to normal combat logic and reconsider next turn.
		if (pSoldier->bActionPoints < GetAPsToBeginFirstAid(pSoldier))
			return AI_ACTION_NONE;

		if (bMedKitSlot != HANDPOS)
		{
			pSoldier->bSlotItemTakenFrom = bMedKitSlot;
			SwapObjs(pSoldier, HANDPOS, bMedKitSlot, TRUE);
		}
		pSoldier->aiData.usActionData = sBestPatientGrid;
		return AI_ACTION_GIVE_AID;
	}

	pSoldier->usUIMovementMode = RUNNING;
	pSoldier->aiData.usActionData = sBestApproachGrid;
	return AI_ACTION_GET_CLOSER;
}

INT8 DecideAutoBandage( SOLDIERTYPE * pSoldier )
{
	INT8					bSlot;
	BOOLEAN				fDoClimb;


	if (pSoldier->stats.bMedical == 0 || pSoldier->ubServicePartner != NOBODY)
	{
		// don't/can't make decision
		return( AI_ACTION_NONE );
	}

	bSlot = FindObjClass( pSoldier, IC_MEDKIT );
	if (bSlot == NO_SLOT)
	{
		// no medical kit!
		return( AI_ACTION_NONE );
	}

	if (pSoldier->bBleeding)
	{
		// heal self first!
		pSoldier->aiData.usActionData = pSoldier->sGridNo;
		if (bSlot != HANDPOS)
		{
			pSoldier->bSlotItemTakenFrom = bSlot;

			SwapObjs( pSoldier, HANDPOS, bSlot, TRUE );
			/*
			memset( &TempObj, 0, sizeof( OBJECTTYPE ) );
			// move the med kit out to temp obj
			SwapObjs( &TempObj, &(pSoldier->inv[bSlot]) );
			// swap the med kit with whatever was in the hand
			SwapObjs( &TempObj, &(pSoldier->inv[HANDPOS]) );
			// replace whatever was in the hand somewhere in inventory
			AutoPlaceObject( pSoldier, &TempObj, FALSE );
			*/
		}
		return( AI_ACTION_GIVE_AID );
	}

//	pSoldier->aiData.usActionData = FindClosestPatient( pSoldier );
	pSoldier->aiData.bAction = FindBestPatient( pSoldier, &fDoClimb );
	if (pSoldier->aiData.bAction != AI_ACTION_NONE)
	{
		pSoldier->usUIMovementMode = RUNNING;
		if (bSlot != HANDPOS)
		{
			pSoldier->bSlotItemTakenFrom = bSlot;

			SwapObjs( pSoldier, HANDPOS, bSlot, TRUE );
		}
		return( pSoldier->aiData.bAction );
	}

	// do nothing
	return( AI_ACTION_NONE );
}

// SANDRO - added a function
BOOLEAN DoctorIsPresent( SOLDIERTYPE * pPatient, BOOLEAN fOnDoctorAssignmentCheck )
{
	SOLDIERTYPE *	pMedic = NULL;
	UINT8			cnt;
	INT8			bSlot;
	BOOLEAN			fDoctorHasBeenFound = FALSE;

	cnt = gTacticalStatus.Team[ OUR_TEAM ].bFirstID;
	for ( pMedic = MercPtrs[ cnt ]; cnt <= gTacticalStatus.Team[ OUR_TEAM ].bLastID; cnt++,pMedic++)
	{
		if ( !(pMedic->bActive) || !(pMedic->bInSector) || ( pMedic->flags.uiStatusFlags & SOLDIER_VEHICLE ) || (pMedic->bAssignment == VEHICLE ) )
		{
			// is nowhere around!
			continue; // NEXT!!!
		}

		if ( pPatient->ubID == pMedic->ubID )
		{
			// cannot make surgery on self!
			continue; // NEXT!!!		
		}
		if ( fOnDoctorAssignmentCheck && pMedic->bAssignment != DOCTOR )
		{
			// not on the right assignment!
			continue; // NEXT!!!
		}

		bSlot = FindMedKit( pMedic );
		if (bSlot == NO_SLOT)
		{
			// no medical kit!
			continue; // NEXT!!!
		}

		if (pMedic->stats.bLife > OKLIFE && !(pMedic->bCollapsed) && pMedic->stats.bMedical > 0 && (NUM_SKILL_TRAITS( pMedic, DOCTOR_NT ) >= gSkillTraitValues.ubDONumberTraitsNeededForSurgery))
		{
			fDoctorHasBeenFound = TRUE;
		}
	}

	return( fDoctorHasBeenFound );
}
