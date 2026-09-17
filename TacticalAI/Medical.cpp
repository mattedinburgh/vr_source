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
	#include "LOS.h"
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
	if ( (pSoldier->stats.bLife >= OKLIFE) && !(pSoldier->bCollapsed) && (pSoldier->stats.bMedical > 0) && (FindBestFirstAidItem( pSoldier ) != NO_SLOT) )
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

// Is there a viable medic close enough to make an extraction worthwhile?
// Non-medics do not drag a casualty around merely to watch the bleed-out timer expire.
static BOOLEAN AIMedicalResponderReady( SOLDIERTYPE *pSoldier )
{
	return pSoldier &&
		pSoldier->bActive &&
		pSoldier->bInSector &&
		pSoldier->stats.bLife >= OKLIFE &&
		!pSoldier->bCollapsed &&
		!pSoldier->bBreathCollapsed &&
		!(pSoldier->usSoldierFlagMask & SOLDIER_POW);
}

// Friendly casualty awareness is local rather than sector-wide. A responder may
// act on a casualty he can directly see, one close enough to hear/notice, or a
// nearby member of his own fireteam whose status is plausibly shared by the element.
// This prevents medics from detecting cross-element casualties through walls/smoke.
BOOLEAN AIResponderKnowsCasualty( SOLDIERTYPE *pResponder, SOLDIERTYPE *pPatient )
{
	if ( !pResponder || !pPatient || pResponder->bTeam != pPatient->bTeam )
		return FALSE;

	INT32 iDistance = PythSpacesAway( pResponder->sGridNo, pPatient->sGridNo );
	if ( iDistance <= 2 )
		return TRUE;

	if ( LOS_Raised( pResponder, pPatient, CALC_FROM_ALL_DIRS ) > 0 )
		return TRUE;

	return AISameFireteam( pResponder, pPatient ) &&
		iDistance <= DAY_VISION_RANGE;
}

// Physical casualty extraction.  Medics retain the existing direct-treatment logic;
// this routine lets another squadmate pull an exposed casualty into cover so the medic
// can stabilize them without the entire team making a suicidal rush into the fire lane.
INT8 DecideCombatCasualtyEvacuation( SOLDIERTYPE *pSoldier )
{
	if ( !AICombatTeam( pSoldier ) || !AIMedicalResponderReady( pSoldier ) ||
		pSoldier->aiData.bAIMorale == MORALE_HOPELESS )
		return AI_ACTION_NONE;

	// An explicit stationary/hold assignment outranks a voluntary rescue run.
	// Adjacent emergency aid is handled separately and remains available.
	if ( pSoldier->aiData.bOrders == STATIONARY && !pSoldier->IsDraggingBleedoutCasualty() )
		return AI_ACTION_NONE;

	if ( pSoldier->IsDraggingBleedoutCasualty() )
	{
		SOLDIERTYPE *pPatient = MercPtrs[pSoldier->ubDraggedCasualtyID];
		if ( !pPatient || AIEscapeActive( pSoldier ) || AIShouldStartEscape( pSoldier ) ||
			AIPersonalRisk( pSoldier ) > AIPersonalRiskTolerance( pSoldier ) )
		{
			pSoldier->StopDraggingBleedoutCasualty();
			return AI_ACTION_NONE;
		}

		UINT16 usPatientExposure = AIKnownThreatExposure( pSoldier, pPatient->sGridNo, pPatient->pathing.bLevel );
		BOOLEAN fPatientScreened = InSmokeNearby( pPatient->sGridNo, pPatient->pathing.bLevel );
		BOOLEAN fPatientCovered = AnyCoverAtSpot( pSoldier, pPatient->sGridNo );

		if ( usPatientExposure == 0 || fPatientScreened ||
			(fPatientCovered && !pPatient->aiData.bUnderFire) )
		{
			pSoldier->StopDraggingBleedoutCasualty();
			return AI_ACTION_NONE;
		}

		INT32 sEvacGrid = FindRetreatSpot( pSoldier );
		if ( TileIsOutOfBounds( sEvacGrid ) )
		{
			pSoldier->StopDraggingBleedoutCasualty();
			return AI_ACTION_NONE;
		}

		pSoldier->usUIMovementMode = WALKING;
		pSoldier->aiData.usActionData = sEvacGrid;
		return AI_ACTION_GET_CLOSER;
	}
	else if ( pSoldier->ubDraggedCasualtyID != NOBODY )
	{
		pSoldier->StopDraggingBleedoutCasualty();
	}

	if ( AICheckIsMedic( pSoldier ) || AIDisengagementActive( pSoldier ) || AIEscapeActive( pSoldier ) || AIShouldStartEscape( pSoldier ) )
		return AI_ACTION_NONE;

	INT32 iRescuerRisk = AIPersonalRisk( pSoldier );
	if ( iRescuerRisk > AIPersonalRiskTolerance( pSoldier ) )
		return AI_ACTION_NONE;

	// Preserve scarce tactical roles when an ordinary rifleman can perform the same
	// extraction. This is a preference, not a prohibition: a truly urgent bleed-out
	// can still outweigh the role cost and make a specialist perform the rescue.
	INT32 iRescuerRoleCost = 0;
	if ( AICheckIsCommander( pSoldier ) )
		iRescuerRoleCost += 35;
	else if ( AICheckIsOfficer( pSoldier ) )
		iRescuerRoleCost += 22;
	if ( AICheckIsRadioOperator( pSoldier ) )
		iRescuerRoleCost += 20;
	if ( AICheckIsMortarOperator( pSoldier ) )
		iRescuerRoleCost += 25;
	if ( AICheckIsMachinegunner( pSoldier ) )
		iRescuerRoleCost += 15;
	if ( pSoldier->usSoldierFlagMask & SOLDIER_BODYGUARD )
		iRescuerRoleCost += 12;
	iRescuerRoleCost = __min( 45, iRescuerRoleCost );

	SOLDIERTYPE *pBestPatient = NULL;
	INT32 sBestApproachGrid = NOWHERE;
	INT32 iBestScore = -100000;

	for ( UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter )
	{
		SOLDIERTYPE *pPatient = MercPtrs[iCounter];
		if ( !pPatient || pPatient == pSoldier || !pPatient->bActive || !pPatient->bInSector ||
			(pPatient->usSoldierFlagMask & SOLDIER_POW) ||
			!IsCarryableLivingCasualty( pPatient ) ||
			pPatient->pathing.bLevel != pSoldier->pathing.bLevel || pPatient->ubServiceCount > 0 )
			continue;

		if ( !AIResponderKnowsCasualty( pSoldier, pPatient ) )
			continue;

		if ( pPatient->ubDraggedByID != NOBODY )
		{
			SOLDIERTYPE *pOther = MercPtrs[pPatient->ubDraggedByID];
			if ( pOther && pOther->ubDraggedCasualtyID == pPatient->ubID &&
				pOther->IsDraggingBleedoutCasualty() )
			{
				continue;
			}

			if ( pOther && pOther->ubDraggedCasualtyID == pPatient->ubID )
				pOther->StopDraggingBleedoutCasualty();
			else
				pPatient->ubDraggedByID = NOBODY;
		}

		// Extraction has independent value: active bleed-out is stabilized on
		// pickup, while any exposed unconscious casualty benefits from being pulled
		// into cover. Do not require a separate medic merely to authorize the move.
		INT32 iDistanceToPatient = PythSpacesAway( pSoldier->sGridNo, pPatient->sGridNo );
		BOOLEAN fSameElement = AISameFireteam( pSoldier, pPatient );
		if ( !fSameElement && iDistanceToPatient > 3 )
			continue;
		if ( iDistanceToPatient > 6 )
			continue;

		UINT16 usPatientExposure = AIKnownThreatExposure( pSoldier, pPatient->sGridNo, pPatient->pathing.bLevel );
		if ( usPatientExposure == 0 && !pPatient->aiData.bUnderFire )
			continue;

		UINT8 ubDirection = 0;
		INT32 sAdjustedGrid = NOWHERE;
		INT32 sApproachGrid = FindAdjacentGridEx( pSoldier, pPatient->sGridNo,
			&ubDirection, &sAdjustedGrid, FALSE, FALSE );
		if ( TileIsOutOfBounds( sApproachGrid ) )
			continue;

		// Do not run to an approach square from which the actual pickup is
		// impossible because a wall/closed door separates rescuer and casualty.
		UINT8 ubDragDirection = AIDirection( sApproachGrid, pPatient->sGridNo );
		if ( ubDragDirection == DIRECTION_IRRELEVANT ||
			gubWorldMovementCosts[pPatient->sGridNo][ubDragDirection][pSoldier->pathing.bLevel] >= TRAVELCOST_BLOCKED )
		{
			continue;
		}

		INT32 iPathSteps = 1;
		if ( sApproachGrid != pSoldier->sGridNo )
		{
			gubNPCAPBudget = 0;
			gubNPCDistLimit = 0;
			iPathSteps = FindBestPath( pSoldier, sApproachGrid, pSoldier->pathing.bLevel,
				RUNNING, NO_COPYROUTE, PATH_THROUGH_PEOPLE );
		}
		if ( iPathSteps == 0 )
			continue;

		INT32 iPathExposure = 0;
		INT32 sCheckGrid = pSoldier->sGridNo;
		for ( INT32 iStep = 0; sApproachGrid != pSoldier->sGridNo &&
			iStep < iPathSteps && iStep < MAX_PATH_LIST_SIZE; ++iStep )
		{
			sCheckGrid = NewGridNo( sCheckGrid, DirectionInc( (UINT8)guiPathingData[iStep] ) );
			if ( TileIsOutOfBounds( sCheckGrid ) )
				break;

			if ( AIKnownThreatExposure( pSoldier, sCheckGrid, pSoldier->pathing.bLevel ) > 0 )
			{
				if ( InSmokeNearby( sCheckGrid, pSoldier->pathing.bLevel ) )
					iPathExposure += 1;
				else
					iPathExposure += AnyCoverAtSpot( pSoldier, sCheckGrid ) ? 3 : 7;
			}
		}

		if ( iPathExposure >= 22 )
			continue;

		// Triage urgency rises as an active bleed-out countdown approaches zero.
		// Ordinary unconscious/collapsed casualties remain valid extraction targets
		// when exposed, but do not automatically outrank somebody who is dying now.
		INT32 iUrgency = 70;
		if ( pPatient->ubBleedoutState == BLEEDOUT_ACTIVE && IsBleedoutCasualty( pPatient ) )
			iUrgency = 160 - 18 * pPatient->ubBleedoutTurns;
		else if ( pPatient->stats.bLife < OKLIFE )
			iUrgency = 100;

		if ( pPatient->aiData.bUnderFire )
			iUrgency += 20;
		if ( fSameElement )
			iUrgency += 20;

		INT32 iScore = iUrgency - 4 * iDistanceToPatient - iPathExposure -
			iRescuerRisk / 2 - iRescuerRoleCost;
		if ( iScore > iBestScore )
		{
			iBestScore = iScore;
			pBestPatient = pPatient;
			sBestApproachGrid = sApproachGrid;
		}
	}

	gubNPCAPBudget = 0;
	gubNPCDistLimit = 0;

	if ( !pBestPatient || iBestScore < 20 )
		return AI_ACTION_NONE;

	if ( SpacesAway( pSoldier->sGridNo, pBestPatient->sGridNo ) == 1 )
	{
		if ( !pSoldier->StartDraggingBleedoutCasualty( pBestPatient, TRUE ) )
			return AI_ACTION_NONE;

		INT32 sEvacGrid = FindRetreatSpot( pSoldier );
		if ( TileIsOutOfBounds( sEvacGrid ) )
		{
			pSoldier->StopDraggingBleedoutCasualty();
			return AI_ACTION_NONE;
		}

		pSoldier->usUIMovementMode = WALKING;
		pSoldier->aiData.usActionData = sEvacGrid;
		return AI_ACTION_GET_CLOSER;
	}

	pSoldier->usUIMovementMode = RUNNING;
	pSoldier->aiData.usActionData = sBestApproachGrid;
	return AI_ACTION_GET_CLOSER;
}

// Combat medic behaviour for enemy and militia AI. Unlike autobandage, this runs during a
// firefight and therefore refuses rescues that would expose the medic to excessive risk.
// The decision is re-evaluated every turn, so a medic can wait for suppression/smoke
// instead of committing to a suicidal run.
// Non-medics do not become roaming battlefield doctors. A soldier with basic
// medical skill and a medkit may, however, stabilize himself during a real lull or
// a critically downed fireteam mate who is already adjacent.
INT8 DecideCombatCasualtyResponse(SOLDIERTYPE *pSoldier, BOOLEAN fCanMove)
{
	if (!pSoldier || !AICombatTeam(pSoldier))
		return AI_ACTION_NONE;

	// Extraction comes first: if a non-medic can safely pull an exposed casualty
	// into cover, do that before asking the medic to cross the same fire lane.
	if (fCanMove)
	{
		INT8 bEvacAction = DecideCombatCasualtyEvacuation(pSoldier);
		if (bEvacAction != AI_ACTION_NONE)
			return bEvacAction;
	}

	// Medics own deliberate battlefield rescue. Ordinary soldiers are limited to
	// immediate adjacent stabilization and never become roaming improvised medics.
	if (AICheckIsMedic(pSoldier))
	{
		INT8 bMedicAction = DecideCombatMedicRescue(pSoldier);
		if (bMedicAction != AI_ACTION_NONE)
			return bMedicAction;
	}
	else
	{
		INT8 bBuddyAidAction = DecideEmergencyBuddyAid(pSoldier);
		if (bBuddyAidAction != AI_ACTION_NONE)
			return bBuddyAidAction;
	}

	return AI_ACTION_NONE;
}

INT8 DecideEmergencySelfAid(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier) || !AIMedicalResponderReady(pSoldier) ||
		pSoldier->stats.bMedical <= 0 ||
		pSoldier->bBleeding <= 0 ||
		pSoldier->aiData.bUnderFire || pSoldier->aiData.bOppCnt > 0 ||
		AIEscapeActive(pSoldier) || AIDisengagementActive(pSoldier) ||
		pSoldier->aiData.bAIMorale == MORALE_HOPELESS ||
		pSoldier->bActionPoints < GetAPsToBeginFirstAid(pSoldier))
	{
		return AI_ACTION_NONE;
	}

	INT32 iHealthPercent = (pSoldier->stats.bLifeMax > 0) ?
		(100 * pSoldier->stats.bLife) / pSoldier->stats.bLifeMax : 100;

	if (pSoldier->bBleeding < 15 && iHealthPercent >= 60)
		return AI_ACTION_NONE;

	INT8 bMedKitSlot = FindBestFirstAidItem( pSoldier );
	if (bMedKitSlot == NO_SLOT)
		return AI_ACTION_NONE;

	BOOLEAN fScreened = InSmokeNearby(pSoldier->sGridNo, pSoldier->pathing.bLevel);
	BOOLEAN fDefensible = SafeSpot(pSoldier, pSoldier->sGridNo) ||
		(fScreened && AnyCoverAtSpot(pSoldier, pSoldier->sGridNo));
	if (!fDefensible)
		return AI_ACTION_NONE;

	if (AIKnownThreatExposure(pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel) > 100 && !fScreened)
		return AI_ACTION_NONE;

	if (bMedKitSlot != HANDPOS)
	{
		pSoldier->bSlotItemTakenFrom = bMedKitSlot;
		SwapObjs(pSoldier, HANDPOS, bMedKitSlot, TRUE);
	}

	pSoldier->aiData.usActionData = pSoldier->sGridNo;
	return AI_ACTION_GIVE_AID;
}

INT8 DecideEmergencyBuddyAid(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier) || !AIMedicalResponderReady(pSoldier) ||
		AICheckIsMedic(pSoldier) ||
		pSoldier->stats.bMedical <= 0 ||
		pSoldier->aiData.bUnderFire ||
		AIEscapeActive(pSoldier) || AIDisengagementActive(pSoldier) ||
		pSoldier->aiData.bAIMorale == MORALE_HOPELESS ||
		pSoldier->bActionPoints < GetAPsToBeginFirstAid(pSoldier))
	{
		return AI_ACTION_NONE;
	}

	INT8 bMedKitSlot = FindBestFirstAidItem( pSoldier );
	if (bMedKitSlot == NO_SLOT)
		return AI_ACTION_NONE;

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
			(pPatient->usSoldierFlagMask & SOLDIER_POW) ||
			!AISameFireteam(pSoldier, pPatient) ||
			pPatient->stats.bLife <= 0 || pPatient->stats.bLife >= OKLIFE ||
			pPatient->bBleeding <= 0 || pPatient->ubServiceCount > 0 ||
			pPatient->pathing.bLevel != pSoldier->pathing.bLevel ||
			CardinalSpacesAway(pSoldier->sGridNo, pPatient->sGridNo) != 1)
		{
			continue;
		}

		BOOLEAN fPatientBleedout = IsBleedoutCasualty(pPatient) && pPatient->ubBleedoutState == BLEEDOUT_ACTIVE;
		BOOLEAN fBestBleedout = pBestPatient && IsBleedoutCasualty(pBestPatient) &&
			pBestPatient->ubBleedoutState == BLEEDOUT_ACTIVE;

		if (!pBestPatient ||
			(fPatientBleedout && !fBestBleedout) ||
			(fPatientBleedout && fBestBleedout && pPatient->ubBleedoutTurns < pBestPatient->ubBleedoutTurns) ||
			(fPatientBleedout == fBestBleedout && pPatient->stats.bLife < pBestPatient->stats.bLife) ||
			(fPatientBleedout == fBestBleedout && pPatient->stats.bLife == pBestPatient->stats.bLife &&
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
	if (!AICombatTeam(pSoldier) || !AIMedicalResponderReady(pSoldier) ||
		!AICheckIsMedic(pSoldier) ||
		AIDisengagementActive(pSoldier) ||
		AIEscapeActive(pSoldier) || AIShouldStartEscape(pSoldier) ||
		pSoldier->aiData.bAIMorale == MORALE_HOPELESS)
	{
		return AI_ACTION_NONE;
	}

	INT8 bMedKitSlot = FindBestFirstAidItem( pSoldier );
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

	UINT8 ubDoctrine = AIGetDoctrineProfile(pSoldier);
	BOOLEAN fCommanded = (pSoldier->bTeam == ENEMY_TEAM) ? AIHasLocalCommandSupport(pSoldier) : TRUE;
	INT32 iMaxRescueDistance = DAY_VISION_RANGE / 2;
	INT32 iMaxPathExposure = 28;
	INT32 iMinRescueValue = 15;

	// Stationary/hold medics may treat an adjacent casualty, but do not leave their
	// assigned position for a roaming rescue. This keeps player militia commands
	// authoritative while preserving immediate lifesaving aid.
	if (pSoldier->aiData.bOrders == STATIONARY)
		iMaxRescueDistance = 1;

	// Security medics provide local first aid rather than assault-rescue. Ordinary
	// uncommanded line medics are somewhat more cautious; veterans/elites keep the
	// full existing rescue envelope.
	if (pSoldier->bTeam == ENEMY_TEAM && ubDoctrine == AI_DOCTRINE_SECURITY)
	{
		iMaxRescueDistance = __max(4, DAY_VISION_RANGE / 4);
		iMaxPathExposure = 16;
		iMinRescueValue = 28;
	}
	else if (pSoldier->bTeam == ENEMY_TEAM && ubDoctrine == AI_DOCTRINE_LINE && !fCommanded)
	{
		iMaxRescueDistance = __max(6, DAY_VISION_RANGE / 3);
		iMaxPathExposure = 22;
		iMinRescueValue = 20;
	}

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; iCounter++)
	{
		SOLDIERTYPE *pPatient = MercPtrs[iCounter];
		if (!pPatient || pPatient == pSoldier || !pPatient->bActive || !pPatient->bInSector ||
			(pPatient->usSoldierFlagMask & SOLDIER_POW) ||
			pPatient->stats.bLife <= 0 || pPatient->bBleeding <= 0 || pPatient->ubServiceCount > 0)
		{
			continue;
		}

		if (!AIResponderKnowsCasualty(pSoldier, pPatient))
			continue;

		// Do not make the medic chase a casualty while another squadmate is
		// actively extracting them. Stale links are discarded defensively.
		if (pPatient->ubDraggedByID != NOBODY)
		{
			SOLDIERTYPE *pRescuer = MercPtrs[pPatient->ubDraggedByID];
			if (pRescuer && pRescuer->ubDraggedCasualtyID == pPatient->ubID)
			{
				if (pRescuer->IsDraggingBleedoutCasualty())
					continue;
				pRescuer->StopDraggingBleedoutCasualty();
			}
			else
			{
				pPatient->ubDraggedByID = NOBODY;
			}
		}

		INT32 iUrgency = 30;
		if (pPatient->stats.bLife < OKLIFE)
			iUrgency = 100;
		else if (pPatient->stats.bLife < OKLIFE * 2)
			iUrgency = 75;
		else if (pPatient->stats.bLife < pPatient->stats.bLifeMax / 2)
			iUrgency = 55;

		iUrgency += __min((INT32)20, (INT32)pPatient->bBleeding / 2);

		// Bleed-out timer is the decisive urgency signal for an incapacitated casualty.
		// Extraction may stabilize an active bleed-out first, but a medic should still
		// strongly prioritize any untreated critical patient who remains in this pool.
		if (IsBleedoutCasualty(pPatient) && pPatient->ubBleedoutState == BLEEDOUT_ACTIVE)
		{
			if (pPatient->ubBleedoutTurns <= 2)
				iUrgency += 70;
			else if (pPatient->ubBleedoutTurns <= 4)
				iUrgency += 50;
			else if (pPatient->ubBleedoutTurns <= 6)
				iUrgency += 25;
		}

		if (pPatient->aiData.bUnderFire)
			iUrgency += 10;

		BOOLEAN fSameElement = AISameFireteam(pSoldier, pPatient);
		INT32 iPatientDistance = PythSpacesAway(pSoldier->sGridNo, pPatient->sGridNo);

		// Combat medics primarily serve their own fireteam. Cross-element rescues remain
		// possible when the casualty is nearby or genuinely critical.
		if (!fSameElement &&
			AICombatTeam(pSoldier) &&
			pPatient->stats.bLife >= OKLIFE &&
			iPatientDistance > TACTICAL_RANGE / 3)
		{
			continue;
		}

		if (fSameElement)
			iUrgency += 15;
		else if (AICombatTeam(pSoldier))
			iUrgency -= 10;

		UINT8 ubDirection = 0;
		INT32 sAdjustedGrid = NOWHERE;
		INT32 sApproachGrid = FindAdjacentGridEx(pSoldier, pPatient->sGridNo,
			&ubDirection, &sAdjustedGrid, FALSE, FALSE);
		if (TileIsOutOfBounds(sApproachGrid))
			continue;

		INT32 iDistance = PythSpacesAway(pSoldier->sGridNo, sApproachGrid);
		if (iDistance > iMaxRescueDistance)
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

		UINT8 ubSupport = AICountNearbyOperationalFriends(pSoldier, pPatient->sGridNo, DAY_VISION_RANGE / 4);
		BOOLEAN fDestinationAttackable =
			(AIKnownThreatExposure(pSoldier, sApproachGrid, pSoldier->pathing.bLevel) > 0);
		BOOLEAN fDestinationCovered = AnyCoverAtSpot(pSoldier, sApproachGrid);
		BOOLEAN fDestinationScreened = InSmokeNearby(sApproachGrid, pSoldier->pathing.bLevel);

		// Absolute veto: do not cross a long exposed fire lane or enter an exposed,
		// attackable casualty position without somebody nearby to support the rescue.
		if (iPathExposure >= iMaxPathExposure ||
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
		if (iRescueValue < iMinRescueValue || iRescueValue <= iBestRescueValue)
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

	bSlot = FindBestFirstAidItem( pSoldier );
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
