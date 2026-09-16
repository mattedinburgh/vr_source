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

UINT8 Urgency[NUM_STATUS_STATES][NUM_MORALE_STATES] =
{
	{URGENCY_LOW,  URGENCY_LOW,  URGENCY_LOW,  URGENCY_LOW,  URGENCY_LOW}, // green
	{URGENCY_HIGH, URGENCY_MED,  URGENCY_MED,  URGENCY_LOW,  URGENCY_LOW}, // yellow
	{URGENCY_HIGH, URGENCY_MED,  URGENCY_MED,  URGENCY_MED,  URGENCY_MED}, // red
	{URGENCY_HIGH, URGENCY_HIGH, URGENCY_HIGH, URGENCY_MED,  URGENCY_MED}  // black
};

UINT16 MovementMode[LAST_MOVEMENT_ACTION + 1][NUM_URGENCY_STATES] =
{
	{WALKING,	 WALKING,  WALKING }, // AI_ACTION_NONE

	{WALKING,  WALKING,  WALKING }, // AI_ACTION_RANDOM_PATROL
	{RUNNING,  RUNNING,  RUNNING }, // AI_ACTION_SEEK_FRIEND
	{RUNNING,  RUNNING,  RUNNING }, // AI_ACTION_SEEK_OPPONENT
	{RUNNING,  RUNNING,  RUNNING }, // AI_ACTION_TAKE_COVER
	{WALKING,  RUNNING,  RUNNING }, // AI_ACTION_GET_CLOSER

	{WALKING,  WALKING,  WALKING }, // AI_ACTION_POINT_PATROL,
	{RUNNING,  RUNNING,  RUNNING }, // AI_ACTION_LEAVE_WATER_GAS,
	{WALKING,  SWATTING, RUNNING }, // AI_ACTION_SEEK_NOISE,
	{RUNNING,  RUNNING,  RUNNING }, // AI_ACTION_ESCORTED_MOVE,
	{WALKING,  RUNNING,  RUNNING }, // AI_ACTION_RUN_AWAY,

	{RUNNING,  RUNNING,  RUNNING }, // AI_ACTION_KNIFE_MOVE
	{WALKING,  WALKING,  WALKING }, // AI_ACTION_APPROACH_MERC
	{RUNNING,  RUNNING,  RUNNING }, // AI_ACTION_TRACK
	{RUNNING,	 RUNNING,  RUNNING },	// AI_ACTION_EAT 
	{WALKING,	 RUNNING,  RUNNING},	// AI_ACTION_PICKUP_ITEM

	{WALKING,	 WALKING,  WALKING},	// AI_ACTION_SCHEDULE_MOVE
	{WALKING,	 WALKING,  WALKING},	// AI_ACTION_WALK
	{WALKING,	 RUNNING,  RUNNING},	// withdraw
	{RUNNING,	 RUNNING,  RUNNING},	// flank left
	{RUNNING,	 RUNNING,  RUNNING},	// flank right
	{RUNNING,	 RUNNING,  RUNNING},	// AI_ACTION_MOVE_TO_CLIMB
};

INT8 OKToAttack(SOLDIERTYPE * pSoldier, int target)
{
	// can't shoot yourself
	if (target == pSoldier->sGridNo)
		return(NOSHOOT_MYSELF);

	if (WaterTooDeepForAttacks(pSoldier->sGridNo, pSoldier->pathing.bLevel))
		return(NOSHOOT_WATER);

	// make sure a weapon is in hand (FEB.8 ADDITION: tossable items are also OK)
	if (!WeaponInHand(pSoldier))
	{
		return(NOSHOOT_NOWEAPON);
	}

	// JUST PUT THIS IN ON JULY 13 TO TRY AND FIX OUT-OF-AMMO SITUATIONS

	if ( Item[pSoldier->inv[HANDPOS].usItem].usItemClass == IC_GUN)
	{
		if ( Item[pSoldier->inv[HANDPOS].usItem].cannon )
		{
			// look for another tank shell ELSEWHERE IN INVENTORY
			if ( FindLaunchable( pSoldier, pSoldier->inv[HANDPOS].usItem ) == NO_SLOT )
			//if ( !ItemHasAttachments( &(pSoldier->inv[HANDPOS]) ) )
			{
				return(NOSHOOT_NOLOAD);
			}
		}
		else if (pSoldier->inv[HANDPOS][0]->data.gun.ubGunShotsLeft == 0 /*SB*/ || 
			!(pSoldier->inv[HANDPOS][0]->data.gun.ubGunState & GS_CARTRIDGE_IN_CHAMBER) ||
			(pSoldier->IsValidSecondHandShotForReloadingPurposes( ) && 
			(pSoldier->inv[SECONDHANDPOS][0]->data.gun.ubGunShotsLeft == 0 || 
			!(pSoldier->inv[SECONDHANDPOS][0]->data.gun.ubGunState & GS_CARTRIDGE_IN_CHAMBER))))
		{
			return(NOSHOOT_NOAMMO);
		}
	}
	else if (Item[pSoldier->inv[HANDPOS].usItem].usItemClass == IC_LAUNCHER)
	{
		if ( FindLaunchable( pSoldier, pSoldier->inv[HANDPOS].usItem ) == NO_SLOT )
		//if ( !ItemHasAttachments( &(pSoldier->inv[HANDPOS]) ) )
		{
			return(NOSHOOT_NOLOAD);
		}
	}

	return(TRUE);
}

BOOLEAN ConsiderProne( SOLDIERTYPE * pSoldier )
{
	INT32		sOpponentGridNo;
	//INT8		bOpponentLevel;
	//INT32		iRange;

	// Enemy equipment class no longer controls tactical understanding. An enemy
	// administrator uses prone exactly as intelligently as an enemy elite; only
	// non-enemy legacy formations retain the old class-specific restraint.
	if( pSoldier->bTeam != ENEMY_TEAM &&
		(pSoldier->ubSoldierClass == SOLDIER_CLASS_ADMINISTRATOR ||
		 pSoldier->ubSoldierClass == SOLDIER_CLASS_GREEN_MILITIA) )
	{
		if( pSoldier->stats.bLife > 3*pSoldier->stats.bLifeMax/4 &&
			!pSoldier->aiData.bUnderFire )
		{
			return( FALSE );
		}
	}

	// sevenfm: other soldiers also check range change desire (DEFENSIVE/CUNNING will go prone even with AI morale = 4)
	if ( RangeChangeDesire(pSoldier) > 3 &&
		!pSoldier->aiData.bUnderFire &&
		pSoldier->stats.bLife > 3*pSoldier->stats.bLifeMax/4 )
	{
		return( FALSE );
	}

	// We don't want to go prone if there is a nearby enemy
	sOpponentGridNo = ClosestKnownOpponent( pSoldier, NULL, NULL );
	if( !TileIsOutOfBounds(sOpponentGridNo) && 
		PythSpacesAway( pSoldier->sGridNo, sOpponentGridNo ) < DAY_VISION_RANGE / 4 )
	{
		return( FALSE );
	}

	return( TRUE );
}

UINT8 StanceChange( SOLDIERTYPE * pSoldier, INT16 ubAttackAPCost )
{
	// consider crouching or going prone

	if (PTR_STANDING)
	{
		if (pSoldier->bActionPoints - ubAttackAPCost >= GetAPsCrouch(pSoldier, TRUE))
		{
			if ( (pSoldier->bActionPoints - ubAttackAPCost >= GetAPsCrouch(pSoldier, TRUE) + GetAPsProne(pSoldier, TRUE)) && IsValidStance( pSoldier, ANIM_PRONE ) && ConsiderProne( pSoldier ) )
			{
				return( ANIM_PRONE );
			}
			else if ( IsValidStance( pSoldier, ANIM_CROUCH ) )
			{
				return( ANIM_CROUCH );
			}
		}
	}
	else if (PTR_CROUCHED)
	{
		if ( (pSoldier->bActionPoints - ubAttackAPCost >= GetAPsProne(pSoldier, TRUE)) && IsValidStance( pSoldier, ANIM_PRONE ) && ConsiderProne( pSoldier ) )
		{
			return( ANIM_PRONE );
		}
	}
	return( 0 );
}

UINT8 ShootingStanceChange( SOLDIERTYPE * pSoldier, ATTACKTYPE * pAttack, INT8 bDesiredDirection )
{
	// Figure out the best stance for this attack

	// We don't want to go through a lot of complex calculations here,
	// just compare the chance of the bullet hitting if we are
	// standing, crouched, or prone

	UINT16	usRealAnimState, usBestAnimState;
	INT8		bBestStanceDiff=-1;
	INT8		bLoop, bStanceNum, bStanceDiff, bAPsAfterAttack, bCurAimTime, bSetScopeMode;
	UINT32	uiChanceOfDamage, uiBestChanceOfDamage, uiCurrChanceOfDamage;
	UINT32	uiStanceBonus, uiMinimumStanceBonusPerChange = 20 - 3 * pAttack->ubAimTime;
	INT32		iRange;

	bStanceNum = 0;
	uiCurrChanceOfDamage = 0;

	bSetScopeMode = pSoldier->bScopeMode;
	pSoldier->bScopeMode = pAttack->bScopeMode;
	bAPsAfterAttack = pSoldier->bActionPoints - MinAPsToAttack( pSoldier, pAttack->sTarget, ADDTURNCOST, pAttack->ubAimTime, 1);
	pSoldier->bScopeMode = bSetScopeMode;
	if (bAPsAfterAttack < GetAPsCrouch(pSoldier, TRUE))
	{
		return( 0 );
	}
	// Unfortunately, to get this to work, we have to fake the AI guy's
	// animation state so we get the right height values
	usRealAnimState = pSoldier->usAnimState;
	usBestAnimState = pSoldier->usAnimState;
	uiBestChanceOfDamage = 0;
	iRange = GetRangeInCellCoordsFromGridNoDiff( pSoldier->sGridNo, pAttack->sTarget );

	switch( gAnimControl[usRealAnimState].ubEndHeight )
	{
		// set a stance number comparable with our loop variable so we can easily compute
		// stance differences and thus AP cost
		case ANIM_STAND:
			bStanceNum = 0;
			break;
		case ANIM_CROUCH:
			bStanceNum = 1;
			break;
		case ANIM_PRONE:
			bStanceNum = 2;
			break;
	}
	for (bLoop = 0; bLoop < 3; bLoop++)
	{
		bStanceDiff = abs( bLoop - bStanceNum );
		if (bStanceDiff == 2 && bAPsAfterAttack < GetAPsCrouch(pSoldier, TRUE) + GetAPsProne(pSoldier, TRUE))
		{
			// can't consider this!
			continue;
		}

		switch( bLoop )
		{
			case 0:
				if ( !pSoldier->InternalIsValidStance( bDesiredDirection, ANIM_STAND ) )
				{
					continue;
				}
				pSoldier->usAnimState = STANDING;
				break;
			case 1:
				if ( !pSoldier->InternalIsValidStance( bDesiredDirection, ANIM_CROUCH ) )
				{
					continue;
				}
				pSoldier->usAnimState = CROUCHING;
				break;
			default:
				if ( !pSoldier->InternalIsValidStance( bDesiredDirection, ANIM_PRONE ) )
				{
					continue;
				}
				pSoldier->usAnimState = PRONE;
				break;
		}

		// Hack:	Assumes the cost to reach the target stance from the current stance is the same as going back.	Probably true.
		bCurAimTime = __min( bAPsAfterAttack - GetAPsToChangeStance( pSoldier, bStanceNum), pAttack->ubAimTime);
		// If can't fire at all from this stance, don't bother with the chances of hitting
		if (bCurAimTime < 0)
		{
			continue;
		}

		uiChanceOfDamage = SoldierToLocationChanceToGetThrough( pSoldier, pAttack->sTarget, pSoldier->bTargetLevel, pSoldier->bTargetCubeLevel, pAttack->ubOpponent ) * CalcChanceToHitGun( pSoldier, pAttack->sTarget, bCurAimTime, AIM_SHOT_TORSO ) / 100;
		if (uiChanceOfDamage > 0)
		{
			uiStanceBonus = 0;
			// artificially augment "chance of damage" to reflect penalty to be shot at various stances
			switch( pSoldier->usAnimState )
			{
				case CROUCHING:
					if (iRange > POINT_BLANK_RANGE + 10 * (AIM_PENALTY_TARGET_CROUCHED / 3))
					{
						uiStanceBonus = AIM_BONUS_CROUCHING;
					}
					else if (iRange > POINT_BLANK_RANGE)
					{
						// reduce chance to hit with distance to the prone/immersed target
						uiStanceBonus = 3 * ((iRange - POINT_BLANK_RANGE) / CELL_X_SIZE); // penalty -3%/tile
					}
					break;
				case PRONE:
					if (iRange <= MIN_PRONE_RANGE)
					{
						// HATE being prone this close!
						uiChanceOfDamage = 0;
					}
					else //if (iRange > POINT_BLANK_RANGE)
					{
						// reduce chance to hit with distance to the prone/immersed target
						uiStanceBonus = 3 * ((iRange - POINT_BLANK_RANGE) / CELL_X_SIZE); // penalty -3%/tile
					}
					break;
				default:
					break;
			}
			// reduce stance bonus according to how much we have to change stance to get there
			//uiStanceBonus = uiStanceBonus * (4 - bStanceDiff) / 4;
			uiChanceOfDamage += uiStanceBonus;
		}

		if (bStanceDiff == 0)
		{
			uiCurrChanceOfDamage = uiChanceOfDamage;
		}
		if (uiChanceOfDamage > uiBestChanceOfDamage )
		{
			uiBestChanceOfDamage = uiChanceOfDamage;
			usBestAnimState = pSoldier->usAnimState;
			bBestStanceDiff = bStanceDiff;
		}
	}

	pSoldier->usAnimState = usRealAnimState;

	// return 0 or the best height value to be at
	if (bBestStanceDiff == 0 || ((uiBestChanceOfDamage - uiCurrChanceOfDamage) / bBestStanceDiff) < uiMinimumStanceBonusPerChange)
	{
		// better off not changing our stance!
		return( 0 );
	}
	else
	{
		return( gAnimControl[ usBestAnimState ].ubEndHeight );
	}
}


UINT16 DetermineMovementMode( SOLDIERTYPE * pSoldier, INT8 bAction )
{
	// zombies always run if they know enemy location
	if (pSoldier->IsZombie() && IS_MERC_BODY_TYPE(pSoldier))
	{
		INT32 sClosestThreat = ClosestKnownOpponent(pSoldier, NULL, NULL);

		if (!TileIsOutOfBounds(sClosestThreat))
		{
			return RUNNING;
		}
		else
		{
			return WALKING;
		}
	}

	if ( pSoldier->flags.fUIMovementFast )
	{
		return( RUNNING );
	}
	else if ( CREATURE_OR_BLOODCAT( pSoldier ) )
	{
		if (pSoldier->aiData.bAlertStatus == STATUS_GREEN)
		{
			return( WALKING );
		}
		else
		{
			return( RUNNING );
		}
	}
	else if (pSoldier->ubBodyType == COW || pSoldier->ubBodyType == CROW)
	{
		return( WALKING );
	}
	else
	{
		if ( (pSoldier->aiData.fAIFlags & AI_CAUTIOUS) )
		{
			// if soldier is already crouched/prone, use SWATTING
			if( IS_MERC_BODY_TYPE(pSoldier ) &&
				(pSoldier->bTeam == ENEMY_TEAM || pSoldier->bTeam == MILITIA_TEAM) &&
				gAnimControl[ pSoldier->usAnimState ].ubEndHeight <= ANIM_CROUCH )
			{
				return SWATTING;
			}
			// use WALKING instead of RUNNING
			if( MovementMode[bAction][Urgency[pSoldier->aiData.bAlertStatus][pSoldier->aiData.bAIMorale]] == RUNNING )
			{
				return( WALKING );
			}

			// if not RUNNING, use default movement mode
			return MovementMode[bAction][Urgency[pSoldier->aiData.bAlertStatus][pSoldier->aiData.bAIMorale]];
		}
		else if ( bAction == AI_ACTION_SEEK_NOISE && pSoldier->bTeam == CIV_TEAM && !IS_MERC_BODY_TYPE( pSoldier ) )
		{
			return( WALKING );
		}
		else if ( (pSoldier->ubBodyType == HATKIDCIV || pSoldier->ubBodyType == KIDCIV) && (pSoldier->aiData.bAlertStatus == STATUS_GREEN) && Random( 10 ) == 0 )
		{
			return( KID_SKIPPING );
		}
		else
		{
			// sevenfm: movement mode tweaks
			INT32 sClosestThreat =	ClosestKnownOpponent( pSoldier, NULL, NULL );

			// use walking mode if no enemy known
			if (pSoldier->aiData.bAlertStatus < STATUS_RED &&
				TileIsOutOfBounds(sClosestThreat) &&
				!pSoldier->aiData.bUnderFire &&
				(bAction == AI_ACTION_SEEK_FRIEND || bAction == AI_ACTION_SEEK_NOISE || bAction == AI_ACTION_TAKE_COVER))
			{
				return WALKING;
			}

			// use swatting when blinded
			if (IS_MERC_BODY_TYPE(pSoldier) &&
				pSoldier->bBlindedCounter > 0)
			{
				return SWATTING;
			}

			if ( IS_MERC_BODY_TYPE( pSoldier ) &&
				pSoldier->aiData.bAlertStatus >= STATUS_YELLOW &&
				!InWaterGasOrSmoke( pSoldier, pSoldier->sGridNo ) &&
				!(pSoldier->flags.uiStatusFlags & SOLDIER_BOXER) &&				
				!TileIsOutOfBounds(sClosestThreat) &&
				(pSoldier->bTeam == ENEMY_TEAM || pSoldier->bTeam == MILITIA_TEAM) )
			{
				// determine max visible distance
				//INT16 sDistanceVisible = DistanceVisible( pSoldier, DIRECTION_IRRELEVANT, DIRECTION_IRRELEVANT, pSoldier->sGridNo, pSoldier->pathing.bLevel );
				INT16 sDistanceVisible = VISION_RANGE;

				// use running when in light at night (moving slowly in light at night is dangerous)
				if( NightTime() &&
					InLightAtNight( pSoldier->sGridNo, pSoldier->pathing.bLevel ) &&
					(bAction == AI_ACTION_SEEK_OPPONENT || 
					bAction == AI_ACTION_GET_CLOSER ||
					bAction == AI_ACTION_SEEK_FRIEND ||
					bAction == AI_ACTION_TAKE_COVER) &&
					pSoldier->bBreath > 25 )
				{
					return RUNNING;
				}

				// night swatting approach (more chance for AI to sneak unseen and get interrupt)
				if( NightTime() &&
					!InLightAtNight( pSoldier->sGridNo, pSoldier->pathing.bLevel) &&
					pSoldier->aiData.bAlertStatus == STATUS_RED &&
					pSoldier->aiData.bShock == 0 &&
					!GuySawEnemy(pSoldier) &&					
					CountNearbyFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 4) < 3 &&
					PythSpacesAway(pSoldier->sGridNo, sClosestThreat) < 3*sDistanceVisible/2 &&
					CountFriendsBlack(pSoldier) == 0 &&
					(bAction == AI_ACTION_SEEK_OPPONENT) )
				{					
					return SWATTING;
				}

				// use swatting/crawling for SEEK in RED state if soldier is already crouched/prone
				// (more chance for AI to sneak unseen and get interrupt)
				if( !InLightAtNight( pSoldier->sGridNo, pSoldier->pathing.bLevel) &&
					pSoldier->aiData.bAlertStatus == STATUS_RED &&
					pSoldier->aiData.bShock == 0 &&
					!GuySawEnemy(pSoldier) &&					
					CountNearbyFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 4) < 3 &&
					PythSpacesAway(pSoldier->sGridNo, sClosestThreat) < 3*sDistanceVisible/2 &&					
					CountFriendsBlack(pSoldier) == 0 &&
					RangeChangeDesire(pSoldier) < 4 &&					
					gAnimControl[ pSoldier->usAnimState ].ubEndHeight <= ANIM_CROUCH &&
					(bAction == AI_ACTION_SEEK_OPPONENT) )
				{
					return SWATTING;
				}

				// use swatting for taking cover
				if( pSoldier->aiData.bAlertStatus >= STATUS_RED &&
					PythSpacesAway( pSoldier->sGridNo, sClosestThreat ) > DAY_VISION_RANGE / 8 &&
					(pSoldier->aiData.bUnderFire && RangeChangeDesire(pSoldier) < 4 ||
					pSoldier->aiData.bShock > 2*RangeChangeDesire(pSoldier) ||
					pSoldier->aiData.bShock > 0 && gAnimControl[ pSoldier->usAnimState ].ubEndHeight == ANIM_PRONE) &&
					!pSoldier->aiData.bLastAttackHit &&
					( bAction == AI_ACTION_TAKE_COVER ) )
				{
					return SWATTING;
				}

				// use SWATTING/CRAWLING when under fire
				// (use suppression fire to slow down enemy)
				if( pSoldier->aiData.bAlertStatus >= STATUS_RED &&
					(pSoldier->aiData.bShock > RangeChangeDesire(pSoldier) && PythSpacesAway( pSoldier->sGridNo, sClosestThreat ) > DAY_VISION_RANGE / 2 ||
					pSoldier->aiData.bShock > 0 && gAnimControl[ pSoldier->usAnimState ].ubEndHeight == ANIM_PRONE && PythSpacesAway( pSoldier->sGridNo, sClosestThreat ) > DAY_VISION_RANGE / 4) &&
					PythSpacesAway(pSoldier->sGridNo, sClosestThreat) < 3*sDistanceVisible/2 &&
					gAnimControl[ pSoldier->usAnimState ].ubEndHeight <= ANIM_CROUCH &&
					!pSoldier->aiData.bLastAttackHit &&
					( bAction == AI_ACTION_SEEK_OPPONENT || 
					bAction == AI_ACTION_GET_CLOSER ||
					bAction == AI_ACTION_SEEK_FRIEND ||
					bAction == AI_ACTION_TAKE_COVER ) )
				{
					/*if( gAnimControl[ pSoldier->usAnimState ].ubEndHeight == ANIM_PRONE &&
						!InARoom( pSoldier->sGridNo, &usRoom ) )
						RangeChangeDesire(pSoldier) < 4 )
					{
						return CRAWLING;
					}*/
					return SWATTING;
				}

				// use SWATTING when in a room and seen enemy recently or under fire
				// (better cover for AI hiding behind windows)
				if ( InARoom( pSoldier->sGridNo, NULL ) &&
					pSoldier->aiData.bAlertStatus >= STATUS_YELLOW &&
					( pSoldier->aiData.bOrders == SNIPER ||
					pSoldier->aiData.bOrders == STATIONARY ||
					(GuySawEnemy(pSoldier) || pSoldier->aiData.bShock > 0 ) && RangeChangeDesire(pSoldier) < 4 ) &&
					PythSpacesAway( pSoldier->sGridNo, sClosestThreat ) > DAY_VISION_RANGE / 4 &&
					( bAction == AI_ACTION_SEEK_OPPONENT || 
					bAction == AI_ACTION_GET_CLOSER ||
					bAction == AI_ACTION_SEEK_FRIEND ||
					bAction == AI_ACTION_TAKE_COVER ||
					bAction == AI_ACTION_SEEK_NOISE ) )
				{
					return SWATTING;
				}

				// use swatting/crawling for snipers on roof or when under fire
				// (better cover for AI, especially if there are sandbags on roof)
				if( pSoldier->pathing.bLevel > 0 &&
					pSoldier->aiData.bAlertStatus >= STATUS_YELLOW &&					
					( pSoldier->aiData.bOrders == SNIPER ||
					pSoldier->aiData.bOrders == STATIONARY ||
					pSoldier->aiData.bShock > 0 && RangeChangeDesire(pSoldier) < 4 ) &&
					PythSpacesAway( pSoldier->sGridNo, sClosestThreat ) > DAY_VISION_RANGE / 4 &&
					( bAction == AI_ACTION_SEEK_OPPONENT || 
					bAction == AI_ACTION_GET_CLOSER ||
					bAction == AI_ACTION_SEEK_FRIEND ||
					bAction == AI_ACTION_TAKE_COVER ||
					bAction == AI_ACTION_SEEK_NOISE ) )
				{
					return SWATTING;
				}

				UINT8 ubStance = gAnimControl[pSoldier->usAnimState].ubEndHeight;

				// use running for taking cover when not under attack
				if (!pSoldier->aiData.bUnderFire &&
					bAction == AI_ACTION_TAKE_COVER &&
					pSoldier->bInitialActionPoints > APBPConstants[AP_MINIMUM] &&
					!TileIsOutOfBounds(sClosestThreat) &&
					(!InARoom(pSoldier->sGridNo, NULL) || PythSpacesAway(sClosestThreat, pSoldier->sGridNo) > DAY_VISION_RANGE * 2) &&
					pSoldier->aiData.bAIMorale >= MORALE_NORMAL &&
					pSoldier->bBreath > 25 &&
					pSoldier->pathing.bLevel == 0 &&
					pSoldier->aiData.bOrders != STATIONARY &&
					pSoldier->aiData.bOrders != SNIPER &&
					(ubStance > ANIM_PRONE || pSoldier->bActionPoints > APBPConstants[AP_MINIMUM]))
				{
					return RUNNING;
				}

				// use walking/swatting when flanking in realtime
				// (better cover at night, save BP in realtime at daytime)
				if(pSoldier->IsFlanking() && !gfTurnBasedAI)
				{
					if(NightTime())
					{
						return SWATTING;
					}
					if(pSoldier->bBreath < pSoldier->bBreathMax/2)
					{
						return WALKING;
					}					
				}
			}

			return( MovementMode[bAction][Urgency[pSoldier->aiData.bAlertStatus][pSoldier->aiData.bAIMorale]] );
		}
	}
}

void NewDest(SOLDIERTYPE *pSoldier, INT32 usGridNo)
{
	// sevenfm: disable
	/*BOOLEAN fSet = FALSE;

	if ( IS_MERC_BODY_TYPE( pSoldier ) && pSoldier->aiData.bAction == AI_ACTION_TAKE_COVER && (pSoldier->aiData.bOrders == DEFENSIVE || pSoldier->aiData.bOrders == CUNNINGSOLO || pSoldier->aiData.bOrders == CUNNINGAID ) && (SoldierDifficultyLevel( pSoldier ) >= 2) )
	{
		UINT16 usMovementMode;

		// getting real movement anim for someone who is going to take cover, not just considering
		usMovementMode = MovementMode[AI_ACTION_TAKE_COVER][Urgency[pSoldier->aiData.bAlertStatus][pSoldier->aiData.bAIMorale]];

		if ( usMovementMode != SWATTING )
		{
			// really want to look at path, see how far we could get on path while swatting
			if ( EnoughPoints( pSoldier, RecalculatePathCost( pSoldier, SWATTING ), 0, FALSE ) || (pSoldier->aiData.bLastAction == AI_ACTION_TAKE_COVER && pSoldier->usUIMovementMode == SWATTING ) )
			{
				pSoldier->usUIMovementMode = SWATTING;
			}
			else
			{
				pSoldier->usUIMovementMode = usMovementMode;
			}
		}
		else
		{
			pSoldier->usUIMovementMode = usMovementMode;
		}
		fSet = TRUE;
	}
	else
	{
		if ( AICombatTeam(pSoldier) && pSoldier->aiData.bAlertStatus == STATUS_RED )
		{
			switch( pSoldier->aiData.bAction )
			{

				case AI_ACTION_MOVE_TO_CLIMB:
				case AI_ACTION_RUN_AWAY:
					pSoldier->usUIMovementMode = DetermineMovementMode( pSoldier, pSoldier->aiData.bAction );
					fSet = TRUE;
					break;
				default:
					if ( !fSet )
					{
						pSoldier->usUIMovementMode = DetermineMovementMode( pSoldier, pSoldier->aiData.bAction );
						fSet = TRUE;
					}
					break;
			}

		}
		else
		{
			pSoldier->usUIMovementMode = DetermineMovementMode( pSoldier, pSoldier->aiData.bAction );
			fSet = TRUE;
		}

		if ( pSoldier->usUIMovementMode == SWATTING && !IS_MERC_BODY_TYPE( pSoldier ) )
		{
			pSoldier->usUIMovementMode = WALKING;
		}
	}*/

	// sevenfm: always use DetermineMovementMode
	pSoldier->usUIMovementMode = DetermineMovementMode( pSoldier, pSoldier->aiData.bAction );
	// check for non merc bodytypes
	if ( pSoldier->usUIMovementMode == SWATTING && !IS_MERC_BODY_TYPE( pSoldier ) )
	{
		pSoldier->usUIMovementMode = WALKING;
	}

	//pSoldier->EVENT_GetNewSoldierPath( pSoldier->pathing.sDestination, pSoldier->usUIMovementMode );
	// ATE: Using this more versatile version
	// Last parameter says whether to re-start the soldier's animation
	// This should be done if buddy was paused for fNoApstofinishMove...
	pSoldier->EVENT_InternalGetNewSoldierPath( usGridNo, pSoldier->usUIMovementMode , FALSE, pSoldier->flags.fNoAPToFinishMove );
}


BOOLEAN IsActionAffordable(SOLDIERTYPE *pSoldier, INT8 bAction)
{
	INT16	bMinPointsNeeded = 0;
	// sevenfm: r7972 fix
	INT8 bAPForStandUp = 0;
	INT8 bAPToLookAtWall = 0;

	//NumMessage("AffordableAction - Guy#",pSoldier->ubID);

	if( bAction == AI_ACTION_NONE )
	{
		bAction = pSoldier->aiData.bAction;
	}

	switch (bAction)
	{
		case AI_ACTION_NONE:                  // maintain current position & facing
			// no cost for doing nothing!
			break;
		case AI_ACTION_CHANGE_FACING:         // turn to face another direction
			bMinPointsNeeded = (INT8) GetAPsToLook( pSoldier );
			break;
		case AI_ACTION_RANDOM_PATROL:         // move towards a particular location
		case AI_ACTION_SEEK_FRIEND:           // move towards friend in trouble
		case AI_ACTION_SEEK_OPPONENT:         // move towards a reported opponent
		case AI_ACTION_TAKE_COVER:            // run for nearest cover from threat
		case AI_ACTION_GET_CLOSER:            // move closer to a strategic location
		case AI_ACTION_POINT_PATROL:          // move towards next patrol point
		case AI_ACTION_LEAVE_WATER_GAS:       // seek nearest spot of ungassed land
		case AI_ACTION_SEEK_NOISE:            // seek most important noise heard
		case AI_ACTION_ESCORTED_MOVE:         // go where told to by escortPlayer
		case AI_ACTION_RUN_AWAY:              // run away from nearby opponent(s)
		case AI_ACTION_APPROACH_MERC:
		case AI_ACTION_TRACK:
		case AI_ACTION_EAT:
		case AI_ACTION_SCHEDULE_MOVE:
		case AI_ACTION_WALK:
		case AI_ACTION_MOVE_TO_CLIMB:
			// for movement, must have enough APs to move at least 1 tile's worth
			bMinPointsNeeded = MinPtsToMove(pSoldier);
			break;
		case AI_ACTION_PICKUP_ITEM:           // grab things lying on the ground
			bMinPointsNeeded = __max( MinPtsToMove( pSoldier ), GetBasicAPsToPickupItem( pSoldier ) ); // SANDRO
			break;
		case AI_ACTION_OPEN_OR_CLOSE_DOOR:
		case AI_ACTION_UNLOCK_DOOR:
		case AI_ACTION_LOCK_DOOR:
			bMinPointsNeeded = MinPtsToMove(pSoldier);
			break;
		case AI_ACTION_DROP_ITEM:
			bMinPointsNeeded = GetBasicAPsToPickupItem( pSoldier ); // SANDRO
			break;
		case AI_ACTION_FIRE_GUN:              // shoot at nearby opponent
		case AI_ACTION_TOSS_PROJECTILE:       // throw grenade at/near opponent(s)
		case AI_ACTION_KNIFE_MOVE:            // preparing to stab adjacent opponent
		case AI_ACTION_THROW_KNIFE:
			// only FIRE_GUN currently actually pays extra turning costs!
			bMinPointsNeeded = MinAPsToAttack(pSoldier,pSoldier->aiData.usActionData,ADDTURNCOST,pSoldier->aiData.bAimTime);
#ifdef BETAVERSION
			if (ptsNeeded > pSoldier->bActionPoints)
			{
			/*
				sprintf(tempstr,"AI ERROR: %s has insufficient points for attack action %d at grid %d",
							pSoldier->name,pSoldier->aiData.bAction,pSoldier->aiData.usActionData);
				PopMessage(tempstr);
				*/
			}
#endif
			break;
		case AI_ACTION_PULL_TRIGGER:          // activate an adjacent panic trigger
			bMinPointsNeeded = APBPConstants[AP_PULL_TRIGGER];
			break;
		case AI_ACTION_USE_DETONATOR:         // grab detonator and set off bomb(s)
			bMinPointsNeeded = APBPConstants[AP_USE_REMOTE];
			break;
		case AI_ACTION_YELLOW_ALERT:          // tell friends opponent(s) heard
		case AI_ACTION_RED_ALERT:             // tell friends opponent(s) seen
		case AI_ACTION_CREATURE_CALL:				 // for now
			bMinPointsNeeded = APBPConstants[AP_RADIO];
			break;
		case AI_ACTION_CHANGE_STANCE:                // crouch
			bMinPointsNeeded = GetAPsCrouch(pSoldier, TRUE);
			break;
		case AI_ACTION_GIVE_AID:              // help injured/dying friend
			bMinPointsNeeded = 0;
			break;
		case AI_ACTION_CLIMB_ROOF:
			// sevenfm: r7972 fix
			bAPForStandUp = 0;
			bAPToLookAtWall = (FindDirectionForClimbing(pSoldier, pSoldier->sGridNo) == pSoldier->ubDirection) ? 0 : GetAPsToLook(pSoldier);

			// SANDRO - improved this a bit
			if (pSoldier->pathing.bLevel == 0)
			{
				if (PTR_CROUCHED) bAPForStandUp = (INT8)(GetAPsCrouch(pSoldier, TRUE));
				else if (PTR_PRONE) bAPForStandUp = GetAPsCrouch(pSoldier, TRUE) + GetAPsProne(pSoldier, TRUE);
				bMinPointsNeeded = GetAPsToClimbRoof(pSoldier, FALSE) + bAPForStandUp + bAPToLookAtWall;
			}
			else
			{
				if (!PTR_CROUCHED) bAPForStandUp = (INT8)(GetAPsCrouch(pSoldier, TRUE));
				bMinPointsNeeded = GetAPsToClimbRoof(pSoldier, TRUE) + bAPForStandUp + bAPToLookAtWall;
			}
			break;
		case AI_ACTION_COWER:
		case AI_ACTION_STOP_COWERING:
		case AI_ACTION_LOWER_GUN:
		case AI_ACTION_END_COWER_AND_MOVE:
		case AI_ACTION_TRAVERSE_DOWN:
		case AI_ACTION_OFFER_SURRENDER:
			bMinPointsNeeded = 0;
			break;
		case AI_ACTION_STEAL_MOVE: // added by SANDRO
			//bMinPointsNeeded = GetAPsToStealItem( pSoldier, NULL, pSoldier->aiData.usActionData );;
			break;

		case AI_ACTION_JUMP_WINDOW:
			if((UsingNewInventorySystem() == true) && pSoldier->inv[BPACKPOCKPOS].exists() == true)
				bMinPointsNeeded = GetAPsToJumpThroughWindows( pSoldier, TRUE );
			else
				bMinPointsNeeded = GetAPsToJumpFence( pSoldier, FALSE );
			break;
		case AI_ACTION_FREE_PRISONER:
			bMinPointsNeeded = APBPConstants[AP_HANDCUFF];
			break;
		case AI_ACTION_USE_SKILL:
			bMinPointsNeeded = 10;	// TODO
			break;
		case AI_ACTION_HANDLE_ITEM:
			bMinPointsNeeded = 0;
			break;
		default:
#ifdef BETAVERSION
			//NumMessage("AffordableAction - Illegal action type = ",pSoldier->aiData.bAction);
#endif
			break;
	}

	// check whether or not we can afford to do this action
	if (bMinPointsNeeded > pSoldier->bActionPoints)
	{
		return(FALSE);
	}
	else
	{
		return(TRUE);
	}
}

INT16 RandomFriendWithin(SOLDIERTYPE *pSoldier)
{
	UINT32		uiLoop;
	UINT16		usMaxDist;
	UINT8		ubFriendCount, ubFriendIDs[MAXMERCS], ubFriendID;
	UINT8		ubDirection;
	UINT8		ubDirsLeft;
	BOOLEAN		fDirChecked[8];
	BOOLEAN		fFound = FALSE;
	INT32		usDest, usOrigin;
	SOLDIERTYPE *pFriend;

	// obtain maximum roaming distance from soldier's origin
	usMaxDist = RoamingRange(pSoldier,&usOrigin);

	// if range is restricted, make sure origin is a legal gridno!
	if (((usOrigin < 0) || (usOrigin >= GRIDSIZE)))
	{
#ifdef BETAVERSION
		NameMessage(pSoldier,"has illegal origin, but his roaming range is restricted!",1000);
#endif
		return(FALSE);
	}

	ubFriendCount = 0;

	// build a list of the guynums of all active, eligible friendly mercs

	// go through each soldier, looking for "friends" (soldiers on same side)
	for (uiLoop = 0; uiLoop < guiNumMercSlots; uiLoop++)
	{
		pFriend = MercSlots[ uiLoop ];

		// if this merc is inactive, not in sector, or dead
		if (!pFriend)
		{
			continue;
		}

		// skip ourselves
		if (pFriend->ubID == pSoldier->ubID)
		{
			continue;
		}

		// if this man not neutral, but is on my side, OR if he is neutral, but
		// so am I, then he's a "friend" for the purposes of random visitations
		if ((!pFriend->aiData.bNeutral && (pSoldier->bSide == pFriend->bSide)) ||
			(pFriend->aiData.bNeutral && pSoldier->aiData.bNeutral))
		{
			// if we're not already neighbors
			if (SpacesAway(pSoldier->sGridNo,pFriend->sGridNo) > 1)
			{
		// remember his guynum, increment friend counter
			ubFriendIDs[ubFriendCount++] = pFriend->ubID;
			}
		}
	}


	while (ubFriendCount && !fFound)
	{
		// randomly select one of the remaining friends in the list
		ubFriendID = ubFriendIDs[PreRandom(ubFriendCount)];

		// if our movement range is NOT restricted, or this friend's within range
		// use distance - 1, because there must be at least 1 tile 1 space closer
		if (SpacesAway(usOrigin,Menptr[ubFriendID].sGridNo) - 1 <= usMaxDist)
		{
			// should be close enough, try to find a legal->pathing.sDestination within 1 tile

			// clear dirChecked flag for all 8 directions
			for (ubDirection = 0; ubDirection < 8; ubDirection++)
			{
				fDirChecked[ubDirection] = FALSE;
			}

			ubDirsLeft = 8;

			// examine all 8 spots around 'ubFriendID'
			// keep looking while directions remain and a satisfactory one not found
			while ((ubDirsLeft--) && !fFound)
			{
				// randomly select a direction which hasn't been 'checked' yet
				do
				{
					ubDirection = (UINT8) Random(8);
				}
				while (fDirChecked[ubDirection]);

				fDirChecked[ubDirection] = TRUE;

				// determine the gridno 1 tile away from current friend in this direction
				usDest = NewGridNo(Menptr[ubFriendID].sGridNo,DirectionInc(ubDirection));

				// if that's out of bounds, ignore it & check next direction
				if (usDest == Menptr[ubFriendID].sGridNo)
				{
					continue;
				}

				// if our movement range is NOT restricted
				if (SpacesAway(usOrigin,usDest) <= usMaxDist)
				{
					if (LegalNPCDestination(pSoldier,usDest,ENSURE_PATH,NOWATER, 0))
					{
						fFound = TRUE;			// found a spot
						pSoldier->aiData.usActionData = usDest;	// store this->pathing.sDestination
						pSoldier->pathing.bPathStored = TRUE;	// optimization - Ian
						break;					// stop checking in other directions
					}
				}
			}
		}

		if (!fFound)
		{
			ubFriendCount--;

			// if we hadn't already picked the last friend currently in the list
			if (ubFriendCount != ubFriendID)
			{
				ubFriendIDs[ubFriendID] = ubFriendIDs[ubFriendCount];
			}
		}
	}

	return(fFound);
}


INT32 RandDestWithinRange(SOLDIERTYPE *pSoldier)
{
	INT32 sRandDest = NOWHERE;
	INT32 usOrigin, usMaxDist;
	UINT8	ubTriesLeft;
	BOOLEAN fLimited = FALSE, fFound = FALSE;
	INT16 sMaxLeft, sMaxRight, sMaxUp, sMaxDown, sXRange, sYRange, sXOffset, sYOffset;
	INT16 sOrigX, sOrigY;
	INT16 sX, sY;
	//DBrot: More Rooms
	//UINT8	ubRoom = 0, ubTempRoom;
	UINT16 usRoom = 0, usTempRoom;
	sOrigX = sOrigY = -1;
	sMaxLeft = sMaxRight = sMaxUp = sMaxDown = sXRange = sYRange = -1;

	// Try to find a random->pathing.sDestination that's no more than maxDist away from
	// the given gridno of origin

	if (gfTurnBasedAI)
	{
		ubTriesLeft = 10;
	}
	else
	{
		ubTriesLeft = 1;
	}

	usMaxDist = RoamingRange(pSoldier,&usOrigin);

	if ( pSoldier->aiData.bOrders <= CLOSEPATROL && (pSoldier->bTeam == CIV_TEAM || pSoldier->ubProfile != NO_PROFILE ) )
	{
		// any other combo uses the default of ubRoom == 0, set above
		if ( !InARoom( pSoldier->aiData.sPatrolGrid[0], &usRoom ) )
		{
			usRoom = 0;
		}
	}

	// if the maxDist is truly a restriction
	if (usMaxDist < (MAXCOL - 1))
	{
		fLimited = TRUE;

		// determine maximum horizontal limits
		sOrigX = usOrigin % MAXCOL;
		sOrigY = usOrigin / MAXCOL;

		sMaxLeft	= min(usMaxDist, sOrigX);
		sMaxRight = min(usMaxDist,MAXCOL - (sOrigX + 1));

		// determine maximum vertical limits
		sMaxUp	= min(usMaxDist, sOrigY);
		sMaxDown = min(usMaxDist,MAXROW - (sOrigY + 1));

		sXRange = sMaxLeft + sMaxRight + 1;
		sYRange = sMaxUp + sMaxDown + 1;
	}

	if (pSoldier->ubBodyType == LARVAE_MONSTER)
	{
		// only crawl 1 tile, within our roaming range
		while ((ubTriesLeft--) && !fFound)
		{
			sXOffset = (INT16) Random( 3 ) - 1; // generates -1 to +1
			sYOffset = (INT16) Random( 3 ) - 1;

			if (fLimited)
			{
				sX = pSoldier->sGridNo % MAXCOL + sXOffset;
				sY = pSoldier->sGridNo / MAXCOL + sYOffset;
				if (sX < sOrigX - sMaxLeft || sX > sOrigX + sMaxRight)
				{
					continue;
				}
				if (sY < sOrigY - sMaxUp || sY > sOrigY + sMaxDown)
				{
					continue;
				}
				sRandDest = usOrigin + sXOffset + (MAXCOL * sYOffset);
			}
			else
			{
				sRandDest = usOrigin + sXOffset + (MAXCOL * sYOffset);
			}

			if (!LegalNPCDestination(pSoldier,sRandDest,ENSURE_PATH,NOWATER,0))
			{
				sRandDest = NOWHERE;
				continue;					// try again!
			}

			// passed all the tests,->pathing.sDestination is acceptable
			fFound = TRUE;
			pSoldier->pathing.bPathStored = TRUE;	// optimization - Ian
		}
	}
	else
	{
		// keep rolling random->pathing.sDestinations until one's satisfactory or retries used
		while ((ubTriesLeft--) && !fFound)
		{
			if (fLimited)
			{
				UINT8 ubTriesLeft2 = 128;
				do//dnl ch53 111009 This loop should increase search performance, but probably need some counter to prevent eventual endless loop
				{
					sXOffset = ((INT16)Random(sXRange)) - sMaxLeft;
					sYOffset = ((INT16)Random(sYRange)) - sMaxUp;
					sRandDest = usOrigin + sXOffset + (MAXCOL * sYOffset);
				}while(!GridNoOnVisibleWorldTile(sRandDest) && --ubTriesLeft2);
	#ifdef BETAVERSION
				if ((sRandDest < 0) || (sRandDest >= GRIDSIZE))
				{
					NumMessage("RandDestWithinRange: ERROR - Gridno out of range! = ",sRandDest);
					sRandDest = random(GRIDSIZE);
				}
	#endif
			}
			else
			{
				UINT8 ubTriesLeft2 = 128;
				do//dnl ch53 111009 This loop should increase search performance, but probably need some counter to prevent eventual endless loop
				{
					sRandDest = PreRandom(GRIDSIZE);
				}while(!GridNoOnVisibleWorldTile(sRandDest) && --ubTriesLeft2);
			}

			if ( usRoom && InARoom( sRandDest, &usTempRoom ) && usTempRoom != usRoom )
			{
				// outside of room available for patrol!
				sRandDest = NOWHERE;
				continue;
			}

			// sevenfm: avoid staying at doors
			if( pSoldier->aiData.bAlertStatus < STATUS_RED &&
				pSoldier->pathing.bLevel == 0 &&
				CheckDoorNearGridno(sRandDest) )
			{
				sRandDest = NOWHERE;
				continue;
			}

			// sevenfm: avoid going too close to known bombs
			if (FindBombNearby(pSoldier, sRandDest, BOMB_DETECTION_RANGE))
			{
				sRandDest = NOWHERE;
				continue;
			}

			// sevenfm: don't go too far from closest not SEEKENEMY friend if alert is not raised yet
			if( pSoldier->aiData.bAlertStatus < STATUS_RED &&
				DistanceToClosestNotSeekEnemyFriend(pSoldier, sRandDest) > DAY_VISION_RANGE )
			{
				continue;
			}

			if (!LegalNPCDestination(pSoldier,sRandDest,ENSURE_PATH,NOWATER,0))
			{
				sRandDest = NOWHERE;
				continue;					// try again!
			}

			// passed all the tests,->pathing.sDestination is acceptable
			fFound = TRUE;
			pSoldier->pathing.bPathStored = TRUE;	// optimization - Ian
		}
	}

	return(sRandDest); // defaults to NOWHERE
}

// Turn stale contact information into a small deterministic search offset.
// Different soldiers spread around the same last-known location instead of all
// converging on one exact grid. Current sightings remain exact.
INT32 AIStaleContactSearchSpot(SOLDIERTYPE *pSoldier, SOLDIERTYPE *pOpponent,
	INT32 sKnownSpot, INT8 bKnownLevel, INT8 bKnowledge)
{
	if (!pSoldier || !pOpponent || TileIsOutOfBounds(sKnownSpot))
		return sKnownSpot;

	UINT8 ubSearchRadius = 0;
	switch (bKnowledge)
	{
	case SEEN_CURRENTLY:
	case SEEN_THIS_TURN:
		return sKnownSpot;

	case SEEN_LAST_TURN:
		ubSearchRadius = 2;
		break;

	case HEARD_THIS_TURN:
		ubSearchRadius = 3;
		break;

	case HEARD_LAST_TURN:
		ubSearchRadius = 4;
		break;

	case SEEN_2_TURNS_AGO:
	case HEARD_2_TURNS_AGO:
		ubSearchRadius = 5;
		break;

	default:
		ubSearchRadius = 6;
		break;
	}

	// Stale information describes an uncertainty area, not a magic destination.
	// Prefer a covered observation point that can clear the likely sector while
	// preserving route safety and local support.
	INT32 sObservation = FindThreatSearchObservationSpot(
		pSoldier, sKnownSpot, bKnownLevel, ubSearchRadius);
	if (!TileIsOutOfBounds(sObservation))
		return sObservation;

	// Fallback for awkward geometry (roofs, tiny rooms, blocked areas): retain the
	// old deterministic spread so multiple soldiers still avoid piling onto one tile.
	UINT8 ubStartDirection =
		(UINT8)((pSoldier->ubID + 3 * pOpponent->ubID) % NUM_WORLD_DIRECTIONS);
	INT8 bPreferredDistance =
		1 + (INT8)((pSoldier->ubID + pOpponent->ubID) % __max((UINT8)1, ubSearchRadius));

	for (UINT8 ubTry = 0; ubTry < NUM_WORLD_DIRECTIONS; ++ubTry)
	{
		UINT8 ubDirection =
			(ubStartDirection + ubTry) % NUM_WORLD_DIRECTIONS;
		INT32 sCandidate = sKnownSpot;

		for (INT8 bStep = 0; bStep < bPreferredDistance; ++bStep)
		{
			INT32 sNext =
				NewGridNo(sCandidate, DirectionInc(ubDirection));
			if (sNext == sCandidate || TileIsOutOfBounds(sNext))
				break;
			sCandidate = sNext;
		}

		if (sCandidate != sKnownSpot &&
			!TileIsOutOfBounds(sCandidate) &&
			NewOKDestination(
				pSoldier, sCandidate, FALSE, bKnownLevel))
		{
			return sCandidate;
		}
	}

	return sKnownSpot;
}

INT32 ClosestReachableDisturbance(SOLDIERTYPE *pSoldier, BOOLEAN * pfChangeLevel)
{
	INT32		*psLastLoc, *pusNoiseGridNo;
	INT8		*pbLastLevel;
	INT32		sGridNo = -1;
	INT8		bLevel, bClosestLevel = -1;
	BOOLEAN		fClimbingNecessary, fClosestClimbingNecessary = FALSE;
	INT32		iPathCost;
	INT32		sClosestDisturbance = NOWHERE;
	UINT32		uiLoop;
	UINT32		closestConscious = NOWHERE, closestUnconscious = NOWHERE;
	INT32		iShortestPath = 1000;
	INT32		iShortestPathConscious = 1000, iShortestPathUnconscious = 1000;
	UINT8		*pubNoiseVolume;
	INT8		*pbNoiseLevel;
	INT8		*pbPersOL, *pbPublOL;
	INT8		bKnowledge = NOT_HEARD_OR_SEEN;
	INT32		sClimbGridNo;
	SOLDIERTYPE *pOpponent;
	SOLDIERTYPE	*pClosestOpponent = NULL;
	INT32		sDistToEnemy, sDistToClosestEnemy = 10000;

	// sevenfm: safety check
	if (pfChangeLevel)
	{
		*pfChangeLevel = FALSE;
	}

	pubNoiseVolume = &gubPublicNoiseVolume[pSoldier->bTeam];
	pusNoiseGridNo = &gsPublicNoiseGridNo[pSoldier->bTeam];
	pbNoiseLevel = &gbPublicNoiseLevel[pSoldier->bTeam];

	// hang pointers at start of this guy's personal and public opponent opplists
	//	pbPersOL = &pSoldier->aiData.bOppList[0];
	//	pbPublOL = &(gbPublicOpplist[pSoldier->bTeam][0]);
	//	psLastLoc = &(gsLastKnownOppLoc[pSoldier->ubID][0]);

	// look through this man's personal & public opplists for opponents known
	for (uiLoop = 0; uiLoop < guiNumMercSlots; uiLoop++)
	{
		pOpponent = MercSlots[uiLoop];

		if (!pOpponent)
		{
			continue;
		}

		pbPersOL = pSoldier->aiData.bOppList + pOpponent->ubID;
		pbPublOL = gbPublicOpplist[pSoldier->bTeam] + pOpponent->ubID;
		psLastLoc = gsLastKnownOppLoc[pSoldier->ubID] + pOpponent->ubID;
		pbLastLevel = gbLastKnownOppLevel[pSoldier->ubID] + pOpponent->ubID;

		if ((*pbPersOL == NOT_HEARD_OR_SEEN) && (*pbPublOL == NOT_HEARD_OR_SEEN))
		{
			continue;
		}

		BOOLEAN fThreatStateKnown =
			(*pbPersOL == SEEN_CURRENTLY) &&
			(LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0);
		// Current relation/existence is legal only while the contact is directly seen.
		// A stale memory remains a plausible hostile at the last known location.
		if (fThreatStateKnown &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 (gTacticalStatus.bBoxingState == BOXING && pSoldier->IsBoxer() && !pOpponent->IsBoxer()) ||
			 pOpponent->ubBodyType == CROW ||
			 !pOpponent->bActive || !pOpponent->bInSector ||
			 pOpponent->stats.bLife <= 0 || pOpponent->IsEmptyVehicle()))
		{
			continue;
		}

		// this is possible if get here from BLACK AI in one of those rare
		// instances when we couldn't get a meaningful shot off at a guy in sight
		/*if ((*pbPersOL == SEEN_CURRENTLY) && (pOpponent->stats.bLife >= OKLIFE))
		{
		// don't allow this to return any valid values, this guy remains a
		// serious threat and the last thing we want to do is approach him!
		return(NOWHERE);
		}*/

		// if personal knowledge is more up to date or at least equal
		if ((gubKnowledgeValue[*pbPublOL - OLDEST_HEARD_VALUE][*pbPersOL - OLDEST_HEARD_VALUE] > 0) ||
			(*pbPersOL == *pbPublOL))
		{
			// using personal knowledge, obtain opponent's "best guess" gridno
			sGridNo = *psLastLoc;
			bLevel = *pbLastLevel;
			bKnowledge = *pbPersOL;
		}
		else
		{
			// using public knowledge, obtain opponent's "best guess" gridno
			sGridNo = gsPublicLastKnownOppLoc[pSoldier->bTeam][pOpponent->ubID];
			bLevel = gbPublicLastKnownOppLevel[pSoldier->bTeam][pOpponent->ubID];
			bKnowledge = *pbPublOL;
		}

		// if we are standing at that gridno (!, obviously our info is old...)
		if (sGridNo == pSoldier->sGridNo)
		{
			continue;			// next merc
		}

		if (TileIsOutOfBounds(sGridNo))
		{
			// huh?
			continue;
		}

		// Search uncertainty grows as contact information becomes stale. Keep exact
		// current sightings untouched and distribute ordinary enemies around old contacts.
		if (AICombatTeam(pSoldier) && !pSoldier->IsZombie())
		{
			sGridNo = AIStaleContactSearchSpot(pSoldier, pOpponent, sGridNo, bLevel, bKnowledge);
		}

		// sevenfm: if soldier is zombie and he cannot climb, skip location
		if (pSoldier->IsZombie() && pSoldier->pathing.bLevel != bLevel && !gGameExternalOptions.fZombieCanClimb)
		{
			continue;
		}

		// sevenfm: zombies do not attack vehicles
		if (pSoldier->IsZombie() && (TANK(pOpponent) || (pOpponent->flags.uiStatusFlags & SOLDIER_VEHICLE)))
		{
			continue;
		}

		// sevenfm: when in deep water, skip opponents in deep water
		if (DeepWater(pSoldier->sGridNo, pSoldier->pathing.bLevel) && DeepWater(sGridNo, bLevel))
		{
			continue;
		}

		// sevenfm: if we found reachable enemy, check other enemies only if they are closer
		sDistToEnemy = PythSpacesAway(pSoldier->sGridNo, sGridNo);
		if (sDistToEnemy < sDistToClosestEnemy || TileIsOutOfBounds(sClosestDisturbance))
		{
			iPathCost = EstimatePathCostToLocation(pSoldier, sGridNo, bLevel, FALSE, &fClimbingNecessary, &sClimbGridNo);

			// Select from reachable believed locations. Hidden current consciousness of
			// stale contacts must not change which disturbance this soldier pursues.
			if (iPathCost != 0 &&
				(TileIsOutOfBounds(sClosestDisturbance) || iPathCost < iShortestPath))
			{
				if (fClimbingNecessary)
				{
					sClosestDisturbance = sClimbGridNo;
				}
				else
				{
					sClosestDisturbance = sGridNo;
				}

				pClosestOpponent = pOpponent;
				sDistToClosestEnemy = sDistToEnemy;
				iShortestPath = iPathCost;
				fClosestClimbingNecessary = fClimbingNecessary;
			}
		}
	}

	// if any "misc. noise" was also heard recently	
	if (!TileIsOutOfBounds(pSoldier->aiData.sNoiseGridno) && pSoldier->aiData.sNoiseGridno != sClosestDisturbance)
	{
		// test this gridno, too
		sGridNo = pSoldier->aiData.sNoiseGridno;
		bLevel = pSoldier->bNoiseLevel;

		// if we are there (at the noise gridno)
		if (sGridNo == pSoldier->sGridNo)
		{
			for (uiLoop = 0; uiLoop < guiNumMercSlots; uiLoop++)
			{
				pOpponent = MercSlots[uiLoop];
				if (pOpponent &&
					pSoldier->bSide == pOpponent->bSide &&
					pSoldier->ubID != pOpponent->ubID &&
					pSoldier->aiData.sNoiseGridno == pOpponent->aiData.sNoiseGridno)
				{
					// Reaching a personal noise location confirms it only for the local
					// tactical element. Legacy code cleared the same noise instantly for
					// every friendly in the sector, making separated groups share results
					// without radio or proximity.
					BOOLEAN fShareClear = TRUE;
					if (AICombatTeam(pSoldier) && pOpponent->bTeam == pSoldier->bTeam)
					{
						INT32 iShareDistance = PythSpacesAway(pSoldier->sGridNo, pOpponent->sGridNo);
						if (AISameFireteam(pSoldier, pOpponent))
							fShareClear = (iShareDistance <= TACTICAL_RANGE);
						else
							fShareClear = (iShareDistance <= TACTICAL_RANGE / 3);
					}
					if (fShareClear)
					{
						pOpponent->aiData.sNoiseGridno = NOWHERE;
						pOpponent->aiData.ubNoiseVolume = 0;
					}
				}
			}
			pSoldier->aiData.sNoiseGridno = NOWHERE;		// wipe it out, not useful anymore
			pSoldier->aiData.ubNoiseVolume = 0;
		}
		else
		{
			// get the AP cost to get to the location of the noise
			iPathCost = EstimatePathCostToLocation(pSoldier, sGridNo, bLevel, FALSE, &fClimbingNecessary, &sClimbGridNo);
			// if we can get there
			// sevenfm: only if we don't know enemy location or noise source is close and we have not seen enemy recently
			if (iPathCost != 0 &&
				!pSoldier->IsFlanking() &&
				(TileIsOutOfBounds(sClosestDisturbance) || iPathCost < iShortestPath && !GuySawEnemy(pSoldier)))
			{
				if (fClimbingNecessary)
				{
					sClosestDisturbance = sClimbGridNo;
				}
				else
				{
					sClosestDisturbance = sGridNo;
				}
				iShortestPath = iPathCost;
				fClosestClimbingNecessary = fClimbingNecessary;
			}
		}
	}


	// if any PUBLIC "misc. noise" was also heard recently	
	if (!TileIsOutOfBounds(*pusNoiseGridNo) && *pusNoiseGridNo != sClosestDisturbance)
	{
		// test this gridno, too
		sGridNo = *pusNoiseGridNo;
		bLevel = *pbNoiseLevel;

		// if we are not NEAR the noise gridno...
		if (pSoldier->pathing.bLevel != bLevel || PythSpacesAway(pSoldier->sGridNo, sGridNo) >= 6 || SoldierTo3DLocationLineOfSightTest(pSoldier, sGridNo, bLevel, 0, FALSE, NO_DISTANCE_LIMIT) == 0)
			// if we are NOT there (at the noise gridno)
			//	if (sGridNo != pSoldier->sGridNo)
		{
			// get the AP cost to get to the location of the noise
			iPathCost = EstimatePathCostToLocation(pSoldier, sGridNo, bLevel, FALSE, &fClimbingNecessary, &sClimbGridNo);
			// if we can get there
			// sevenfm: only if we don't know enemy location or noise source is close and we have not seen enemy recently
			if (iPathCost != 0 &&
				!pSoldier->IsFlanking() &&
				(TileIsOutOfBounds(sClosestDisturbance) || iPathCost < iShortestPath && !GuySawEnemy(pSoldier)))
			{
				if (fClimbingNecessary)
				{
					sClosestDisturbance = sClimbGridNo;
				}
				else
				{
					sClosestDisturbance = sGridNo;
				}
				iShortestPath = iPathCost;
				fClosestClimbingNecessary = fClimbingNecessary;
			}
		}
		else
		{
			// degrade our public noise a bit
			//dnl ch58 160813
			//*pusNoiseGridNo -= 2;
			if (*pubNoiseVolume > 1)
				(*pubNoiseVolume)--;
		}
	}

#ifdef DEBUGDECISIONS	
	if (!TileIsOutOfBounds(sClosestDisturbance))
	{
		AINumMessage("CLOSEST DISTURBANCE is at gridno ", sClosestDisturbance);
	}
#endif

	// sevenfm: safety check
	if (pfChangeLevel)
	{
		*pfChangeLevel = fClosestClimbingNecessary;
	}

	return(sClosestDisturbance);
}

INT32 ClosestKnownOpponent(SOLDIERTYPE *pSoldier, INT32 * psGridNo, INT8 * pbLevel)
{
	INT32 *psLastLoc, sGridNo, sClosestOpponent = NOWHERE;
	UINT32 uiLoop;
	INT32 iRange, iClosestRange = 1500;
	INT8	*pbPersOL, *pbPublOL;
	INT8	bLevel, bClosestLevel;
	SOLDIERTYPE *pOpponent;
	SOLDIERTYPE *pClosestOpponent = NULL;

	bClosestLevel = -1;

	// NOTE: THIS FUNCTION ALLOWS RETURN OF UNCONSCIOUS AND UNREACHABLE OPPONENTS
	psLastLoc = &(gsLastKnownOppLoc[pSoldier->ubID][0]);

	// hang pointers at start of this guy's personal and public opponent opplists
	pbPersOL = &pSoldier->aiData.bOppList[0];
	pbPublOL = &(gbPublicOpplist[pSoldier->bTeam][0]);

	// look through this man's personal & public opplists for opponents known
	for (uiLoop = 0; uiLoop < guiNumMercSlots; uiLoop++)
	{
		pOpponent = MercSlots[uiLoop];

		if (!pOpponent)
		{
			continue;
		}

		pbPersOL = pSoldier->aiData.bOppList + pOpponent->ubID;
		pbPublOL = gbPublicOpplist[pSoldier->bTeam] + pOpponent->ubID;
		psLastLoc = gsLastKnownOppLoc[pSoldier->ubID] + pOpponent->ubID;

		if ((*pbPersOL == NOT_HEARD_OR_SEEN) && (*pbPublOL == NOT_HEARD_OR_SEEN))
		{
			continue;
		}

		BOOLEAN fThreatStateKnown =
			(*pbPersOL == SEEN_CURRENTLY) &&
			(LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0);
		// Current relation/existence is legal only while the contact is directly seen.
		// A stale memory remains a plausible hostile at the last known location.
		if (fThreatStateKnown &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 (gTacticalStatus.bBoxingState == BOXING && pSoldier->IsBoxer() && !pOpponent->IsBoxer()) ||
			 pOpponent->ubBodyType == CROW ||
			 !pOpponent->bActive || !pOpponent->bInSector ||
			 pOpponent->stats.bLife <= 0 || pOpponent->IsEmptyVehicle()))
		{
			continue;
		}

		// if personal knowledge is more up to date or at least equal
		if ((gubKnowledgeValue[*pbPublOL - OLDEST_HEARD_VALUE][*pbPersOL - OLDEST_HEARD_VALUE] > 0) ||
			(*pbPersOL == *pbPublOL))
		{
			// using personal knowledge, obtain opponent's "best guess" gridno
			sGridNo = gsLastKnownOppLoc[pSoldier->ubID][pOpponent->ubID];
			bLevel = gbLastKnownOppLevel[pSoldier->ubID][pOpponent->ubID];
		}
		else
		{
			// using public knowledge, obtain opponent's "best guess" gridno
			sGridNo = gsPublicLastKnownOppLoc[pSoldier->bTeam][pOpponent->ubID];
			bLevel = gbPublicLastKnownOppLevel[pSoldier->bTeam][pOpponent->ubID];
		}

		// if we are standing at that gridno(!, obviously our info is old...)
		if (sGridNo == pSoldier->sGridNo)
		{
			continue;			// next merc
		}

		// this function is used only for turning towards closest opponent or changing stance
		// as such, if they AI is in a building,
		// we should ignore people who are on the roof of the same building as the AI
		if ((bLevel != pSoldier->pathing.bLevel) && SameBuilding(pSoldier->sGridNo, sGridNo))
		{
			continue;
		}

		// I hope this will be good enough; otherwise we need a fractional/world-units-based 2D distance function
		//sRange = PythSpacesAway( pSoldier->sGridNo, sGridNo);
		iRange = GetRangeInCellCoordsFromGridNoDiff(pSoldier->sGridNo, sGridNo);

		// "Closest known" should be selected from believed locations. Do not let the
		// hidden current consciousness of a stale contact override geometric distance.
		if (sClosestOpponent == NOWHERE || iRange < iClosestRange)
		{
			iClosestRange = iRange;
			sClosestOpponent = sGridNo;
			bClosestLevel = bLevel;
			pClosestOpponent = pOpponent;
		}
	}

#ifdef DEBUGDECISIONS	
	if (!TileIsOutOfBounds(sClosestOpponent))
	{
		AINumMessage("CLOSEST OPPONENT is at gridno ", sClosestOpponent);
	}
#endif

	if (psGridNo)
	{
		*psGridNo = sClosestOpponent;
	}
	if (pbLevel)
	{
		*pbLevel = bClosestLevel;
	}
	return(sClosestOpponent);
}

INT32 ClosestSeenOpponent(SOLDIERTYPE *pSoldier, INT32 * psGridNo, INT8 * pbLevel)
{
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

		// if this merc is inactive, at base, on assignment, or dead
		if (!pOpponent)
		{
			continue;			// next merc
		}

		if (!ValidOpponent(pSoldier, pOpponent))
		{
			continue;
		}

		pbPersOL = pSoldier->aiData.bOppList + pOpponent->ubID;

		// Cached SEEN_CURRENTLY is not enough after smoke/cover breaks LOS.
		if (*pbPersOL != SEEN_CURRENTLY ||
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) <= 0)
		{
			continue;			// next merc
		}

		// since we're dealing with genuinely visible people, use exact gridnos
		sGridNo = pOpponent->sGridNo;
		bLevel = pOpponent->pathing.bLevel;

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

#ifdef DEBUGDECISIONS	
	if (!TileIsOutOfBounds(sClosestOpponent))
	{
		AINumMessage("CLOSEST OPPONENT is at gridno ",sClosestOpponent);
	}
#endif

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


// special variant with a minor twist
INT32 ClosestSeenOpponentWithRoof(SOLDIERTYPE *pSoldier, INT32 * psGridNo, INT8 * pbLevel)
{
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

		// if this merc is inactive, at base, on assignment, or dead
		if (!pOpponent)
		{
			continue;			// next merc
		}

		if (!ValidOpponent(pSoldier, pOpponent))
		{
			continue;
		}

		pbPersOL = pSoldier->aiData.bOppList + pOpponent->ubID;

		// Cached SEEN_CURRENTLY is not enough after smoke/cover breaks LOS.
		if (*pbPersOL != SEEN_CURRENTLY ||
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) <= 0)
		{
			continue;			// next merc
		}

		// since we're dealing with genuinely visible people, use exact gridnos
		sGridNo = pOpponent->sGridNo;
		bLevel = pOpponent->pathing.bLevel;

		// if we are standing at that gridno(!, obviously our info is old...)
		if (sGridNo == pSoldier->sGridNo)
		{
			continue;			// next merc
		}

		// special: allow zombies to also find opponents on a roof
		// otherwise they have will never decide to climb a roof while they are seeing an enemy
		// this function is used only for turning towards closest opponent or changing stance
		// as such, if they AI is in a building,
		// we should ignore people who are on the roof of the same building as the AI
		/*if ( !pSoldier->IsZombie() && (bLevel != pSoldier->pathing.bLevel) && SameBuilding( pSoldier->sGridNo, sGridNo ) )
		{
			continue;
		}*/

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

#ifdef DEBUGDECISIONS	
	if (!TileIsOutOfBounds(sClosestOpponent))
	{
		AINumMessage("CLOSEST OPPONENT is at gridno ",sClosestOpponent);
	}
#endif

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

INT32 ClosestPC( SOLDIERTYPE *pSoldier, INT32 * psDistance )
{
	// used by NPCs... find the closest PC

	// NOTE: skips EPCs!

	UINT8 ubLoop;
	SOLDIERTYPE		*pTargetSoldier;
	INT32					sMinDist = WORLD_MAX;
	INT32					sDist;
	INT32					sGridNo = NOWHERE;

	// Loop through all mercs on player team
	for (ubLoop = gTacticalStatus.Team[gbPlayerNum].bFirstID; ubLoop <= gTacticalStatus.Team[gbPlayerNum].bLastID; ubLoop++)
	{
		pTargetSoldier = Menptr + ubLoop;

		if (!pTargetSoldier->bActive || !pTargetSoldier->bInSector)
		{
			continue;
		}

		// if not conscious, skip him
		if (pTargetSoldier->stats.bLife < OKLIFE)
		{
		continue;
		}

		if ( AM_AN_EPC( pTargetSoldier ) )
		{
			continue;
		}

		sDist = PythSpacesAway(pSoldier->sGridNo,pTargetSoldier->sGridNo);

		// if this PC is not visible to the soldier, then add a penalty to the distance
		// so that we weight in favour of visible mercs
		if ( pTargetSoldier->bTeam != pSoldier->bTeam && pSoldier->aiData.bOppList[ ubLoop ] != SEEN_CURRENTLY )
		{
			sDist += 10;
		}

		if (sDist < sMinDist)
		{
			sMinDist = sDist;
			sGridNo = pTargetSoldier->sGridNo;
		}
	}

	if ( psDistance )
	{
		*psDistance = sMinDist;
	}

	return( sGridNo );
}

// Flugente: like ClosestPC(...), but does not account for covert or not visible mercs
INT32 ClosestUnDisguisedPC( SOLDIERTYPE *pSoldier, INT32 * psDistance )
{
	// used by NPCs... find the closest PC
	// NOTE: skips EPCs!

	UINT8 ubLoop;
	SOLDIERTYPE		*pTargetSoldier;
	INT32					sMinDist = WORLD_MAX;
	INT32					sDist;
	INT32					sGridNo = NOWHERE;

	// Loop through all mercs on player team
	ubLoop = gTacticalStatus.Team[ gbPlayerNum ].bFirstID;
	for ( ; ubLoop <= gTacticalStatus.Team[ gbPlayerNum ].bLastID; ubLoop++)
	{
		pTargetSoldier = Menptr + ubLoop;

		if (!pTargetSoldier->bActive || !pTargetSoldier->bInSector)
			continue;
				
		// if not conscious, skip him
		if (pTargetSoldier->stats.bLife < OKLIFE)
			continue;

		if ( AM_AN_EPC( pTargetSoldier ) )
			continue;

		if ( pTargetSoldier->usSoldierFlagMask & (SOLDIER_COVERT_CIV|SOLDIER_COVERT_SOLDIER) )
			continue;

		sDist = PythSpacesAway(pSoldier->sGridNo,pTargetSoldier->sGridNo);

		// This helper returns the target's live tile, so cached visibility is not enough.
		// Smoke/cover must immediately prevent use of the hidden current position.
		if (pTargetSoldier->bTeam != pSoldier->bTeam &&
			(pSoldier->aiData.bOppList[ubLoop] != SEEN_CURRENTLY ||
			 LOS_Raised(pSoldier, pTargetSoldier, CALC_FROM_ALL_DIRS) <= 0))
		{
			continue;
		}

		if (sDist < sMinDist)
		{
			sMinDist = sDist;
			sGridNo = pTargetSoldier->sGridNo;
		}
	}

	if ( psDistance )
	{
		*psDistance = sMinDist;
	}

	return( sGridNo );
}

INT32 FindClosestClimbPointAvailableToAI( SOLDIERTYPE * pSoldier, INT32 sStartGridNo, INT32 sDesiredGridNo, BOOLEAN fClimbUp )
{
	INT32 sGridNo;
	INT32	sRoamingOrigin;
	INT16	sRoamingRange;

	// sevenfm: safety check
	if(!pSoldier)
	{
		return NOWHERE;
	}

	if ( pSoldier->flags.uiStatusFlags & SOLDIER_PC )
	{
		sRoamingOrigin = pSoldier->sGridNo;
		sRoamingRange = MAX_ROAMING_RANGE;
	}
	else
	{
		sRoamingRange = RoamingRange( pSoldier, &sRoamingOrigin );
	}

	// since climbing necessary involves going an extra tile, we compare against 1 less than the roam range...
	// or add 1 to the distance to the climb point

	sGridNo = FindClosestClimbPoint( pSoldier, sStartGridNo, sDesiredGridNo, fClimbUp );


	if ( PythSpacesAway( sRoamingOrigin, sGridNo ) + 1 > sRoamingRange )
	{
		return( NOWHERE );
	}
	else
	{
		return( sGridNo );
	}
}

BOOLEAN ClimbingNecessary( SOLDIERTYPE * pSoldier, INT32 sDestGridNo, INT8 bDestLevel )
{
	if (pSoldier->pathing.bLevel == bDestLevel)
	{
		if ( (pSoldier->pathing.bLevel == 0) || ( gubBuildingInfo[ pSoldier->sGridNo ] == gubBuildingInfo[ sDestGridNo ] ) )
		{
			return( FALSE );
		}
		else // different buildings!
		{
			return( TRUE );
		}
	}
	else
	{
		return( TRUE );
	}
}

INT32 GetInterveningClimbingLocation( SOLDIERTYPE * pSoldier, INT32 sDestGridNo, INT8 bDestLevel, BOOLEAN * pfClimbingNecessary )
{
	if (pSoldier->pathing.bLevel == bDestLevel)
	{
		if ( (pSoldier->pathing.bLevel == 0) || ( gubBuildingInfo[ pSoldier->sGridNo ] == gubBuildingInfo[ sDestGridNo ] ) )
		{
			// on ground or same building... normal!
			*pfClimbingNecessary = FALSE;
			return( NOWHERE );
		}
		else
		{
			// different buildings!
			// yes, pass in same gridno twice... want closest climb-down spot for building we are on!
			*pfClimbingNecessary = TRUE;
			return( FindClosestClimbPointAvailableToAI( pSoldier, pSoldier->sGridNo, pSoldier->sGridNo, FALSE ) );
		}
	}
	else
	{
		*pfClimbingNecessary = TRUE;
		// different levels
		if (pSoldier->pathing.bLevel == 0)
		{
			// got to go UP onto building
			return( FindClosestClimbPointAvailableToAI( pSoldier, pSoldier->sGridNo, sDestGridNo, TRUE ) );
		}
		else
		{
			// got to go DOWN off building
			return( FindClosestClimbPointAvailableToAI( pSoldier, pSoldier->sGridNo, pSoldier->sGridNo, FALSE ) );
		}
	}
}

INT16 EstimatePathCostToLocation( SOLDIERTYPE * pSoldier, INT32 sDestGridNo, INT8 bDestLevel, BOOLEAN fAddCostAfterClimbingUp, BOOLEAN * pfClimbingNecessary, INT32 * psClimbGridNo )
{
	INT16	sPathCost;
	INT32 sClimbGridNo;

	if (pSoldier->pathing.bLevel == bDestLevel)
	{
		if ( (pSoldier->pathing.bLevel == 0) || ( gubBuildingInfo[ pSoldier->sGridNo ] == gubBuildingInfo[ sDestGridNo ] ) )
		{
			// on ground or same building... normal!
			sPathCost = EstimatePlotPath( pSoldier, sDestGridNo, FALSE, FALSE, FALSE, WALKING, FALSE, FALSE, 0);
			if (pfClimbingNecessary)
				*pfClimbingNecessary = FALSE;
			if (psClimbGridNo)
				*psClimbGridNo = NOWHERE;
		}
		else
		{
			// different buildings!
			// yes, pass in same gridno twice... want closest climb-down spot for building we are on!
			sClimbGridNo = FindClosestClimbPointAvailableToAI( pSoldier, sDestGridNo, pSoldier->sGridNo, FALSE );			
			if (TileIsOutOfBounds(sClimbGridNo))
			{
				sPathCost = 0;
			}
			else
			{
				sPathCost = PlotPath( pSoldier, sClimbGridNo, FALSE, FALSE, FALSE, WALKING, FALSE, FALSE, 0 );
				// sevenfm: check if we are already standing at climb gridno
				if (sPathCost != 0 || pSoldier->sGridNo == sClimbGridNo)
				{
					// add in cost of climbing down
					if (fAddCostAfterClimbingUp)
					{
						// add in cost of later climbing up, too
						sPathCost += APBPConstants[AP_CLIMBOFFROOF] + APBPConstants[AP_CLIMBROOF];
						// add in an estimate of getting there after climbing down
						sPathCost += (APBPConstants[AP_MOVEMENT_FLAT] + APBPConstants[AP_MODIFIER_WALK]) * PythSpacesAway( sClimbGridNo, sDestGridNo );
					}
					else
					{
						sPathCost += APBPConstants[AP_CLIMBOFFROOF];
						// add in an estimate of getting there after climbing down, *but not on top of roof*
						sPathCost += (APBPConstants[AP_MOVEMENT_FLAT] + APBPConstants[AP_MODIFIER_WALK]) * PythSpacesAway( sClimbGridNo, sDestGridNo ) / 2;
					}
					*pfClimbingNecessary = TRUE;
					*psClimbGridNo = sClimbGridNo;
				}
			}
		}
	}
	else
	{
		// sevenfm: check if zombie cannot climb
		if (pSoldier->IsZombie() && !gGameExternalOptions.fZombieCanClimb)
		{
			return 0;
		}

		// different levels
		if (pSoldier->pathing.bLevel == 0)
		{
			//got to go UP onto building
			sClimbGridNo = FindClosestClimbPointAvailableToAI(pSoldier, pSoldier->sGridNo, sDestGridNo, TRUE);
		}
		else
		{
			// got to go DOWN off building
			sClimbGridNo = FindClosestClimbPointAvailableToAI(pSoldier, sDestGridNo, pSoldier->sGridNo, FALSE);
		}
		
		if (TileIsOutOfBounds(sClimbGridNo))
		{
			sPathCost = 0;
		}
		else
		{
			sPathCost = PlotPath( pSoldier, sClimbGridNo, FALSE, FALSE, FALSE, WALKING, FALSE, FALSE, 0);
			// sevenfm: check if we are already standing at climb gridno
			if (sPathCost != 0 || pSoldier->sGridNo == sClimbGridNo)
			{
				// add in the cost of climbing up or down
				if (pSoldier->pathing.bLevel == 0)
				{
					// must climb up
					sPathCost += APBPConstants[AP_CLIMBROOF];
					if (fAddCostAfterClimbingUp)
					{
						// add to path a rough estimate of how far to go from the climb gridno to the friend
						// estimate walk cost
						sPathCost += (APBPConstants[AP_MOVEMENT_FLAT] + APBPConstants[AP_MODIFIER_WALK]) * PythSpacesAway( sClimbGridNo, sDestGridNo );
					}
				}
				else
				{
					// must climb down
					sPathCost += APBPConstants[AP_CLIMBOFFROOF];
					// add to path a rough estimate of how far to go from the climb gridno to the friend
					// estimate walk cost
					sPathCost += (APBPConstants[AP_MOVEMENT_FLAT] + APBPConstants[AP_MODIFIER_WALK]) * PythSpacesAway( sClimbGridNo, sDestGridNo );
				}
				*pfClimbingNecessary = TRUE;
				*psClimbGridNo = sClimbGridNo;
			}
		}
	}

	return( sPathCost );
}

BOOLEAN GuySawEnemy( SOLDIERTYPE * pSoldier, UINT8 ubMax )
{
	UINT8		uiLoop;
	SOLDIERTYPE *pOpponent;

	for (uiLoop = 0; uiLoop < guiNumMercSlots; uiLoop++)
	{
		pOpponent = MercSlots[ uiLoop ];

		// if this merc is inactive, at base, on assignment, or dead
		if (!pOpponent || !pOpponent->bActive || !pOpponent->bInSector)
		{
			continue;
		}

		// if this merc is neutral/on same side, he's not an opponent
		if ( CONSIDERED_NEUTRAL( pSoldier, pOpponent ) || (pSoldier->bSide == pOpponent->bSide) )
		{
			continue;
		}

		// if this guy SAW an enemy recently...
		if( pSoldier->aiData.bOppList[ pOpponent->ubID ] >= SEEN_CURRENTLY && 
			pSoldier->aiData.bOppList[ pOpponent->ubID ] <= ubMax )
		{
			return( TRUE );
		}
	}

	return( FALSE );
}

BOOLEAN GuyHeardEnemy(SOLDIERTYPE * pSoldier, UINT8 ubMax)
{
	UINT8		uiLoop;
	SOLDIERTYPE *pOpponent;

	for (uiLoop = 0; uiLoop < guiNumMercSlots; uiLoop++)
	{
		pOpponent = MercSlots[uiLoop];

		// if this merc is inactive, at base, on assignment, or dead
		if (!pOpponent || !pOpponent->bActive || !pOpponent->bInSector)
		{
			continue;
		}

		// if this merc is neutral/on same side, he's not an opponent
		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) || (pSoldier->bSide == pOpponent->bSide))
		{
			continue;
		}

		// if this guy HEARD an enemy recently...
		if (pSoldier->aiData.bOppList[pOpponent->ubID] >= ubMax &&
			pSoldier->aiData.bOppList[pOpponent->ubID] <= HEARD_THIS_TURN)
		{
			return(TRUE);
		}
	}

	return(FALSE);
}

INT32 ClosestReachableFriendInTrouble(SOLDIERTYPE *pSoldier, BOOLEAN * pfClimbingNecessary)
{
	UINT32 uiLoop;
	INT32 sPathCost, sClosestFriend = NOWHERE, sShortestPath = 1000, sClimbGridNo;
	BOOLEAN fClimbingNecessary, fClosestClimbingNecessary = FALSE;
	SOLDIERTYPE *pFriend;
	UINT8 ubMyFireteamReady = AICombatTeam(pSoldier) ?
		AIFireteamCombatReadyCount(pSoldier) : 0;

	// civilians don't really have any "friends", so they don't bother with this
	if (PTR_CIVILIAN)
	{
		return(NOWHERE);
	}

	// consider every friend of this soldier (locations assumed to be known)
	for (uiLoop = 0; uiLoop < guiNumMercSlots; uiLoop++)
	{
		pFriend = MercSlots[ uiLoop ];

		// if this merc is inactive, at base, on assignment, or dead
		if (!pFriend)
		{
			continue;			// next merc
		}

		// if this merc is neutral or NOT on the same side, he's not a friend
		if (pFriend->aiData.bNeutral || (pSoldier->bSide != pFriend->bSide))
		{
			continue;			// next merc
		}

		// if this "friend" is actually US
		if (pFriend->ubID == pSoldier->ubID)
		{
			continue;			// next merc
		}

		// Legacy Vengeance searched the entire side for a friend in trouble. That
		// let a distant firefight pull otherwise coherent enemy elements across the
		// sector. Healthy fireteams share contact detail inside the element; another
		// element may receive help only from directly observable/local distress.
		BOOLEAN fCrossElement = FALSE;
		BOOLEAN fCrossElementLocalHelp = FALSE;
		if (AICombatTeam(pSoldier) && pFriend->bTeam == pSoldier->bTeam &&
			!AISameFireteam(pSoldier, pFriend))
		{
			fCrossElement = TRUE;
			INT32 iFriendDistance = PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo);
			if (ubMyFireteamReady > 2 &&
				iFriendDistance > __max(6, DAY_VISION_RANGE / 3))
			{
				continue;
			}

			BOOLEAN fFriendObservable =
				iFriendDistance <= 1 ||
				LOS_Raised(pSoldier, pFriend, CALC_FROM_ALL_DIRS) > 0;
			if (!fFriendObservable)
				continue;

			// Do not read another element's private opponent list. Visible suppression,
			// shock, wounds or collapse are sufficient reasons for local assistance.
			fCrossElementLocalHelp =
				pFriend->aiData.bUnderFire ||
				ShockLevelPercent(pFriend) >= 30 ||
				pFriend->bBleeding > 0 ||
				pFriend->stats.bLife < pFriend->stats.bLifeMax ||
				pFriend->bCollapsed || pFriend->bBreathCollapsed;
		}

		// Same-fireteam members may coordinate from their shared element picture.
		// Cross-element help uses only the observable distress gate above and never
		// falls through to the other element's private opponent list.
		if (fCrossElement)
		{
			if (!fCrossElementLocalHelp)
				continue;
		}
		else if (!(CountSeenEnemiesLastTurn(pFriend) >
			AICountNearbyOperationalFriends(pFriend, pFriend->sGridNo, DAY_VISION_RANGE / 4)))
		{
			continue;			// next merc
		}

		// if we're already neighbors
		if (SpacesAway(pSoldier->sGridNo,pFriend->sGridNo) == 1)
		{
			continue;			// next merc
		}

		// get the AP cost to go to this friend's gridno
		sPathCost = EstimatePathCostToLocation( pSoldier, pFriend->sGridNo, pFriend->pathing.bLevel, TRUE, &fClimbingNecessary, &sClimbGridNo );

		// sevenfm: try to help friend that have more opponents than nearby friends
		// disabled
		//sPathCost = sPathCost * (1 + CountNearbyFriendlies(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE/4)) / (1 + CountSeenEnemiesLastTurn(pFriend));

		// if we can get there
		if (sPathCost != 0)
		{
			//sprintf(tempstr,"Path cost to friend %s's location is %d",pFriend->name,pathCost);
			//PopMessage(tempstr);

			if (sPathCost < sShortestPath)
			{
				if (fClimbingNecessary)
				{
					sClosestFriend = sClimbGridNo;
				}
				else
				{
					sClosestFriend = pFriend->sGridNo;
				}

				sShortestPath = sPathCost;
				fClosestClimbingNecessary = fClimbingNecessary;
			}
		}
	}

#ifdef DEBUGDECISIONS	
	if (!TileIsOutOfBounds(sClosestFriend))
	{
		AINumMessage("CLOSEST FRIEND is at gridno ",sClosestFriend);
	}
#endif

	*pfClimbingNecessary = fClosestClimbingNecessary;
	return(sClosestFriend);
}

INT16 DistanceToClosestFriend( SOLDIERTYPE * pSoldier )
{
	// find the distance to the closest person on the same team
	UINT8 ubLoop;
	SOLDIERTYPE		*pTargetSoldier;
	INT16					sMinDist = 1000;
	INT16					sDist;

	// Loop through all mercs on player team
	ubLoop = gTacticalStatus.Team[ pSoldier->bTeam ].bFirstID;

	for ( ; ubLoop <= gTacticalStatus.Team[ pSoldier->bTeam ].bLastID; ubLoop++ )
	{
		if (ubLoop == pSoldier->ubID)
		{
			// same guy - continue!
			continue;
		}

		pTargetSoldier = Menptr + ubLoop;


		if (!CONSIDERED_ALLIES(pSoldier, pTargetSoldier))
		{
			continue;
		}


		if ( pSoldier->bActive && pSoldier->bInSector )
		{
			if (!pTargetSoldier->bActive || !pTargetSoldier->bInSector)
			{
				continue;
			}
			// if not conscious, skip him
			else if (pTargetSoldier->stats.bLife < OKLIFE)
			{
				continue;
			}
		}
		else
		{
			// compare sector #s
			if ( (pSoldier->sSectorX != pTargetSoldier->sSectorX) ||
				(pSoldier->sSectorY != pTargetSoldier->sSectorY) ||
				(pSoldier->bSectorZ != pTargetSoldier->bSectorZ) )
			{
				continue;
			}
			else if (pTargetSoldier->stats.bLife < OKLIFE)
			{
				continue;
			}
			else
			{
				// well there's someone who could be near
				return( 1 );
			}
		}

		sDist = SpacesAway(pSoldier->sGridNo,pTargetSoldier->sGridNo);

		if (sDist < sMinDist)
		{
			sMinDist = sDist;
		}
	}

	return( sMinDist );
}

BOOLEAN InWaterGasOrSmoke( SOLDIERTYPE *pSoldier, INT32 sGridNo )
{
	if (WaterTooDeepForAttacks( sGridNo, pSoldier->pathing.bLevel ))
	{
		return(TRUE);
	}

	// smoke
	if (gpWorldLevelData[sGridNo].ubExtFlags[ pSoldier->pathing.bLevel ] & MAPELEMENT_EXT_SMOKE)
	{
		return TRUE;
	}

	return InGas( pSoldier, sGridNo );
}

BOOLEAN InGasOrSmoke( SOLDIERTYPE *pSoldier, INT32 sGridNo )
{
	// smoke
	if (gpWorldLevelData[sGridNo].ubExtFlags[ pSoldier->pathing.bLevel ] & (MAPELEMENT_EXT_SMOKE|MAPELEMENT_EXT_SIGNAL_SMOKE) )	
		return TRUE;

	return InGas(pSoldier,sGridNo);
}


INT16 InWaterOrGas(SOLDIERTYPE *pSoldier, INT32 sGridNo)
{
	if (WaterTooDeepForAttacks( sGridNo, pSoldier->pathing.bLevel ))
	{
		return(TRUE);
	}

	return (INT16)InGas( pSoldier, sGridNo );
}

BOOLEAN InGasSpot(SOLDIERTYPE *pSoldier, INT32 sGridNo, INT8 bLevel)
{
	CHECKF(pSoldier);

	if (TileIsOutOfBounds(sGridNo))
		return FALSE;

	// tear gas
	if ((gpWorldLevelData[sGridNo].ubExtFlags[bLevel] & MAPELEMENT_EXT_TEARGAS) &&
		!DoesSoldierWearGasMask(pSoldier))
	{
		return(TRUE);
	}
	// fire/creature/mustard gas
	// sevenfm: avoid mustard gas even when wearing gas mask
	if (gpWorldLevelData[sGridNo].ubExtFlags[bLevel] & (MAPELEMENT_EXT_BURNABLEGAS | MAPELEMENT_EXT_CREATUREGAS | MAPELEMENT_EXT_MUSTARDGAS))
	{
		return(TRUE);
	}
	return FALSE;
}

BOOLEAN InWater(SOLDIERTYPE *pSoldier, INT32 sGridNo)
{
	CHECKF(pSoldier);

	if (TileIsOutOfBounds(sGridNo))
		return FALSE;

	if (Water(sGridNo, pSoldier->pathing.bLevel))
	{
		return TRUE;
	}

	return(FALSE);
}

BOOLEAN InGas(SOLDIERTYPE *pSoldier, INT32 sGridNo)
{
	CHECKF(pSoldier);

	if (TileIsOutOfBounds(sGridNo))
		return FALSE;

	if (InGasSpot(pSoldier, sGridNo, pSoldier->pathing.bLevel))
	{
		return TRUE;
	}

	//WarmSteel - One square away from gas is still considered in gas, because it could expand any moment.
	//Note: this only works for gas that expands with one tile, but hey it's better than nothing!
	int iNeighbourGridNo;
	for (int iDir = 0; iDir < NUM_WORLD_DIRECTIONS; ++iDir)
	{
		iNeighbourGridNo = sGridNo + DirectionInc(iDir);

		if (!TileIsOutOfBounds(iNeighbourGridNo) && InGasSpot(pSoldier, iNeighbourGridNo, pSoldier->pathing.bLevel))
		{
			return TRUE;
		}
	}

	return(FALSE);
}

BOOLEAN WearGasMaskIfAvailable( SOLDIERTYPE * pSoldier )
{
	INT8		bSlot, bNewSlot;

	bSlot = FindGasMask( pSoldier );
	if ( bSlot == NO_SLOT )
	{
		return( FALSE );
	}
	if ( bSlot == HEAD1POS || bSlot == HEAD2POS || bSlot == HELMETPOS )
	{
		return( FALSE );
	}
	if ( pSoldier->inv[ HEAD1POS ].exists() == false )
	{
		bNewSlot = HEAD1POS;
	}
	else if ( pSoldier->inv[ HEAD2POS ].exists() == false )
	{
		bNewSlot = HEAD2POS;
	}
	else
	{
		// screw it, going in position 1 anyhow
		bNewSlot = HEAD1POS;
	}

	RearrangePocket( pSoldier, bSlot, bNewSlot, TRUE );
	return( TRUE );
}

BOOLEAN InLightAtNight( INT32 sGridNo, INT8 bLevel )
{
	UINT8 ubBackgroundLightLevel;

	// do not consider us to be "in light" if we're in an underground sector
	if ( gbWorldSectorZ > 0 )
	{
		return( FALSE );
	}

	ubBackgroundLightLevel = GetTimeOfDayAmbientLightLevel();

	if ( ubBackgroundLightLevel < NORMAL_LIGHTLEVEL_DAY + 2 )
	{
		// don't consider it nighttime, too close to daylight levels
		return( FALSE );
	}
	
	// could've been placed here, ignore the light
	//UINT16 usRoom;
	//UINT16 usOriginalRoom;
	//if (InARoom(sGridNo, usRoom) && InARoom(pSoldier->aiData.sPatrolGrid[0], &usOriginalRoom) && usRoom == usOriginalRoom && (pSoldier->aiData.bOrders == STATIONARY))
	// sevenfm: always check light in a room
	/*if (InARoom(sGridNo, NULL))
	{
		return( FALSE );
	}*/

	// NB light levels are backwards, so a lower light level means the
	// spot in question is BRIGHTER

	if ( LightTrueLevel( sGridNo, bLevel ) < ubBackgroundLightLevel )
	{
		return( TRUE );
	}

	return( FALSE );
}

// sevenfm: new AI morale calculation
INT8 CalcMorale(SOLDIERTYPE *pSoldier)
{
	UINT32 uiLoop, uiLoop2;
	INT32 iOurTotalThreat = 0, iTheirTotalThreat = 0;
	INT16 sOppThreatValue, sFrndThreatValue, sMorale;
	INT32 iPercent;
	INT8	bMostRecentOpplistValue;
	INT8 bMoraleCategory;
	UINT8 *pSeenOpp; //,*friendOlPtr;
	INT8	*pbPersOL, *pbPublOL;
	SOLDIERTYPE *pOpponent,*pFriend;

	// zombies always have high morale
	if (pSoldier->IsZombie())
	{
		return MORALE_FEARLESS;
	}

	UINT8 ubMyFireteamReady = AICombatTeam(pSoldier) ?
		AIFireteamCombatReadyCount(pSoldier) : 0;

	// An enemy with no usable weapon should prioritize self-preservation instead of
	// becoming fearless and charging an armed opponent with hands or a knife.
	if ( pSoldier->bTeam == ENEMY_TEAM || !pSoldier->aiData.bNeutral )
	{
		if ( FindAIUsableObjClass( pSoldier, IC_WEAPON ) == NO_SLOT )
		{
			return( MORALE_HOPELESS );
		}
	}
	// sevenfm: neutrals always have low AI morale even if they have weapons (so they run from enemy)
	if( pSoldier->aiData.bNeutral )
	{
		return( MORALE_WORRIED );
	}

	// hang pointers to my personal opplist, my team's public opplist, and my
	// list of previously seen opponents
	pSeenOpp	= (UINT8 *) &(gbSeenOpponents[pSoldier->ubID][0]);

	// loop through every one of my possible opponents
	for (uiLoop = 0; uiLoop < guiNumMercSlots; uiLoop++)
	{
		pOpponent = MercSlots[ uiLoop ];

		if (!pOpponent)
			continue;

		pbPersOL = pSoldier->aiData.bOppList + pOpponent->ubID;
		pbPublOL = gbPublicOpplist[pSoldier->bTeam] + pOpponent->ubID;
		pSeenOpp = (UINT8 *)gbSeenOpponents[pSoldier->ubID] + pOpponent->ubID;

		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) || pSoldier->bSide == pOpponent->bSide ||
			(pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			pOpponent->ubBodyType == CROW)
		{
			continue;
		}

		BOOLEAN fThreatStateKnown =
			(*pbPersOL == SEEN_CURRENTLY) &&
			(LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0);
		if (fThreatStateKnown && (!pOpponent->bActive || !pOpponent->bInSector || pOpponent->stats.bLife <= 0 || pOpponent->IsEmptyVehicle()))
		{
			continue;
		}

		// if this opponent is unknown to me personally AND unknown to my team, too
		if ((*pbPersOL == NOT_HEARD_OR_SEEN) && (*pbPublOL == NOT_HEARD_OR_SEEN))
		{
			// if I have never seen him before anywhere in this sector, either
			if (!(*pSeenOpp))
				continue;		// next merc

			// have seen him in the past, so he remains something of a threat
			bMostRecentOpplistValue = 0;		// uses the free slot for 0 opplist
		}
		else		 // decide which opplist is more current
		{
			// if personal knowledge is more up to date or at least equal
			if ((gubKnowledgeValue[*pbPublOL - OLDEST_HEARD_VALUE][*pbPersOL - OLDEST_HEARD_VALUE] > 0) || (*pbPersOL == *pbPublOL))
				bMostRecentOpplistValue = *pbPersOL;		// use personal
			else
				bMostRecentOpplistValue = *pbPublOL;		// use public
		}

		iPercent = ThreatPercent[bMostRecentOpplistValue - OLDEST_HEARD_VALUE];

		// A stale contact contributes according to remembered certainty, not hidden
		// current wounds/AP/weapon state. Current contacts keep the detailed threat model.
		INT32 iOpponentThreat = fThreatStateKnown ?
			CalcManThreatValue(pOpponent,pSoldier->sGridNo,FALSE,pSoldier) : 100;
		if (iOpponentThreat < 1)
			iOpponentThreat = 1;
		sOppThreatValue = (iPercent * iOpponentThreat) / 100;

		//sprintf(tempstr,"Known opponent %s, opplist status %d, percent %d, threat = %d",
		//			ExtMen[pOpponent->ubID].name,ubMostRecentOpplistValue,ubPercent,sOppThreatValue);
		//PopMessage(tempstr);

		// ADD this to their running total threatValue (decreases my MORALE)
		iTheirTotalThreat += sOppThreatValue;
		//NumMessage("Their TOTAL threat now = ",sTheirTotalThreat);

		// NOW THE FUN PART: SINCE THIS OPPONENT IS KNOWN TO ME IN SOME WAY,
		// ANY FRIENDS OF MINE THAT KNOW ABOUT HIM BOOST MY MORALE.	SO, LET'S GO
		// THROUGH THEIR PERSONAL OPPLISTS AND CHECK WHICH OF MY FRIENDS KNOW
		// SOMETHING ABOUT HIM AND WHAT THEIR THREAT VALUE TO HIM IS.

		for (uiLoop2 = 0; uiLoop2 < guiNumMercSlots; uiLoop2++)
		{
			pFriend = MercSlots[ uiLoop2 ];

			// Morale support comes from soldiers who can still contribute to the fight.
			// A downed/captured/cowering body nearby is a casualty signal, not covering power.
			if (!pFriend ||
				!pFriend->bActive ||
				!pFriend->bInSector ||
				pFriend->stats.bLife < OKLIFE ||
				pFriend->bCollapsed ||
				pFriend->bBreathCollapsed ||
				(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
				(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
				AIDisengagementActive(pFriend) || AIEscapeActive(pFriend))
			{
				continue;
			}

			// if this merc is not on my side, then he's NOT one of my friends

			// WE CAN'T AFFORD TO CONSIDER THE ENEMY OF MY ENEMY MY FRIEND, HERE!
			// ONLY IF WE ARE ACTUALLY OFFICIALLY CO-OPERATING TOGETHER (SAME SIDE)
			if ( pFriend->aiData.bNeutral && !( pSoldier->ubCivilianGroup != NON_CIV_GROUP && pSoldier->ubCivilianGroup == pFriend->ubCivilianGroup ) )
			{
				continue;		// next merc
			}

			if ( pSoldier->bSide != pFriend->bSide )
				continue;		// next merc

			BOOLEAN fVisibleExternalSupport = FALSE;
			INT32 iFriendDistance = PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo);

			// Morale support is tactical, not sector-wide. Same-fireteam status does not
			// magically provide confidence across the map: a newly reattached remnant must
			// physically close on its destination element before gaining its full support.
			if (AICombatTeam(pSoldier) && pFriend->bTeam == pSoldier->bTeam)
			{
				if (AISameFireteam(pSoldier, pFriend))
				{
					if (iFriendDistance > TACTICAL_RANGE)
						continue;
				}
				else if (ubMyFireteamReady > 2 && iFriendDistance > TACTICAL_RANGE / 2)
				{
					continue;
				}
			}
			else if (AICombatTeam(pSoldier))
			{
				// A militia soldier may draw confidence from a nearby visible player merc,
				// but never from that merc's private opponent list.
				if (pSoldier->bTeam != MILITIA_TEAM || pFriend->bTeam != OUR_TEAM ||
					iFriendDistance > TACTICAL_RANGE / 2 ||
					(iFriendDistance > 1 && LOS_Raised(pSoldier, pFriend, CALC_FROM_ALL_DIRS) <= 0))
				{
					continue;
				}
				fVisibleExternalSupport = TRUE;
			}

			INT32 sFriendKnownOpponent = NOWHERE;
			if (fVisibleExternalSupport)
			{
				// Use only the militia soldier's own certainty and contact location.
				iPercent = ThreatPercent[bMostRecentOpplistValue - OLDEST_HEARD_VALUE];
				sFriendKnownOpponent = KnownLocation(pSoldier, pOpponent->ubID);
			}
			else
			{
				// Same-team support may use the friend's personal opponent knowledge.
				iPercent = ThreatPercent[pFriend->aiData.bOppList[pOpponent->ubID] - OLDEST_HEARD_VALUE];

				if ( pFriend->aiData.bOppList[ pOpponent->ubID ] <= HEARD_LAST_TURN )
				{
					iPercent -= iFriendDistance * 2;
					if ( iPercent <= 0 )
						continue;
				}

				sFriendKnownOpponent = KnownLocation(pFriend, pOpponent->ubID);
			}

			if (TileIsOutOfBounds(sFriendKnownOpponent))
				continue;

			sFrndThreatValue = (iPercent * CalcManThreatValue(pFriend, sFriendKnownOpponent, FALSE, pSoldier)) / 100;

			//sprintf(tempstr,"Known by friend %s, opplist status %d, percent %d, threat = %d",
			//		 ExtMen[pFriend->ubID].name,pFriend->aiData.bOppList[pOpponent->ubID],ubPercent,sFrndThreatValue);
			//PopMessage(tempstr);

			// ADD this to our running total threatValue (increases my MORALE)
			// We multiply by sOppThreatValue to PRO-RATE this based on opponent's
			// threat value to ME personally.	Divide later by sum of them all.
			iOurTotalThreat += sOppThreatValue * sFrndThreatValue;
		}

		// this could get slow if I have a lot of friends...
		//KeepInterfaceGoing();
	}


	// if they are no threat whatsoever
	if (!iTheirTotalThreat)
		sMorale = 500;		// our morale is just incredible
	else
	{
		// now divide sOutTotalThreat by sTheirTotalThreat to get the REAL value
		iOurTotalThreat /= iTheirTotalThreat;

		// calculate the morale (100 is even, < 100 is us losing, > 100 is good)
		sMorale = (INT16) ((100 * iOurTotalThreat) / iTheirTotalThreat);
	}

	// 1.13 officer morale intent, localized for Vengeance: leadership helps troops
	// who are actually within command distance instead of every enemy on the map.
	if (pSoldier->bTeam == ENEMY_TEAM && gGameExternalOptions.fEnemyRoles && gGameExternalOptions.fEnemyOfficers)
	{
		EnsureEnemyCommandRoles();
		UINT8 officerType = HighestEnemyOfficerNearSoldier(pSoldier, max(6, TACTICAL_RANGE / 2));
		if (officerType > OFFICER_NONE)
			sMorale = (INT16)(sMorale * (1.0f + gGameExternalOptions.dEnemyOfficerMoraleModifier * officerType));
	}

	if (sMorale <= 25)				// odds 1:4 or worse
		bMoraleCategory = MORALE_HOPELESS;
	else if (sMorale <= 50)		 // odds between 1:4 and 1:2
		bMoraleCategory = MORALE_WORRIED;
	else if (sMorale <= 150)		// odds between 1:2 and 3:2
		bMoraleCategory = MORALE_NORMAL;
	else if (sMorale <= 300)		// odds between 3:2 and 3:1
		bMoraleCategory = MORALE_CONFIDENT;
	else							// odds better than 3:1
		bMoraleCategory = MORALE_FEARLESS;

	// Doctrine removes the legacy administrator morale boost; hostile civilian
	// behaviour keeps its existing boost.
	if (pSoldier->bTeam == CIV_TEAM && !pSoldier->aiData.bNeutral)
	{
		bMoraleCategory += 2;
	}

	// SEEKENEMY soldiers are more aggressive
	if (pSoldier->aiData.bOrders == SEEKENEMY)
	{
		bMoraleCategory++;
	}

	// if have good health
	if( pSoldier->stats.bLife > pSoldier->stats.bLifeMax/2 )
	{
		bMoraleCategory++;
	}
	// bad health
	if( pSoldier->stats.bLife < pSoldier->stats.bLifeMax/4 )
	{
		bMoraleCategory--;
	}
	// good breath
	if( pSoldier->bBreath > 50 )
	{
		bMoraleCategory++;
	}
	// bad breath
	if( pSoldier->bBreath < 25 )
	{
		bMoraleCategory--;
	}
	// if not under fire - attack
	if( !pSoldier->aiData.bUnderFire )
	{
		bMoraleCategory++;
	}

	// count friends that flank around the same spot
	if (CountFriendsFlankSameSpot(pSoldier) > 0)
	{
		bMoraleCategory++;
	}

	INT32 sClosestOpponent = ClosestKnownOpponent(pSoldier, NULL, NULL);

	// if last attack of this soldier hit enemy - increase morale
	if( pSoldier->aiData.bLastAttackHit ||
		(pSoldier->usSoldierFlagMask2 & SOLDIER_SUCCESSFUL_ATTACK) )
	{
		bMoraleCategory++;
	}

	// if some friend hit enemy - increase morale
	if( CountNearbyFriendsLastAttackHit(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE/4 ) > 0 )
	{
		bMoraleCategory++;
	}

	// bonus if last target is unconscious or suppressed
	if( LastTargetCollapsed(pSoldier) )
	{
		bMoraleCategory++;
	}
	if( LastTargetSuppressed(pSoldier) )
	{
		bMoraleCategory++;
	}

	// bonus if weapon range is short
	if (AICheckHasGun(pSoldier) &&
		(GuySawEnemy(pSoldier, SEEN_LAST_TURN) || pSoldier->aiData.bUnderFire) &&
		AICheckShortWeaponRange(pSoldier))
	{
		bMoraleCategory++;
	}

	// limit AI morale depending on morale and suppression shock
	/*if( pSoldier->aiData.bShock )
	{
		bMoraleCategory = __min(bMoraleCategory, (20 + pSoldier->aiData.bMorale - 20*__min(3, pSoldier->aiData.bShock/5)) / 20);
	}*/

	// limit AI morale when soldier is under heavy fire
	if (ShockLevelPercent(pSoldier) > 75)
		bMoraleCategory = min(bMoraleCategory, MORALE_CONFIDENT);
	else if (ShockLevelPercent(pSoldier) > 50)
		bMoraleCategory = min(bMoraleCategory, MORALE_NORMAL);

	// limit AI morale if can attack enemy from spot
	if (AICombatTeam(pSoldier) &&
		!TileIsOutOfBounds(sClosestOpponent) &&
		AICheckHasGun(pSoldier) &&
		!AICheckShortWeaponRange(pSoldier) &&
		PythSpacesAway(pSoldier->sGridNo, sClosestOpponent) <= AIGunRange(pSoldier) &&
		AIEngagementRangeModifier(pSoldier, sClosestOpponent) <= 0 &&
		GuySawEnemy(pSoldier) &&
			(InARoom(pSoldier->sGridNo, NULL) && pSoldier->pathing.bLevel == 0 || 
			CountFriendsInDirection(pSoldier, AIDirection(pSoldier->sGridNo, sClosestOpponent), PythSpacesAway(sClosestOpponent, pSoldier->sGridNo), FALSE) ||
			CountFriendsInDirectionFromSpot(pSoldier, sClosestOpponent, AIDirection(sClosestOpponent, pSoldier->sGridNo), PythSpacesAway(sClosestOpponent, pSoldier->sGridNo)) || 			
			AICountNearbyOperationalFriends(pSoldier, pSoldier->sGridNo, TACTICAL_RANGE / 2)) &&
		(AICheckSpecialRole(pSoldier) ||
			pSoldier->aiData.bOrders != SEEKENEMY &&
			!pSoldier->aiData.bLastAttackHit &&
			!(pSoldier->usSoldierFlagMask2 & SOLDIER_SUCCESSFUL_ATTACK)) &&
		AnyCoverAtSpot(pSoldier, pSoldier->sGridNo))
	{
		bMoraleCategory = min(bMoraleCategory, MORALE_CONFIDENT);
	}

	// prevent hopeless morale when not under attack
	if (bMoraleCategory == MORALE_HOPELESS && !pSoldier->aiData.bUnderFire)
	{
		bMoraleCategory = MORALE_WORRIED;
	}

	// Wounds and local combat stress progressively increase self-preservation.
	// Tactical success, aggressive orders or personality can still matter, but they
	// should not make a badly wounded or psychologically overwhelmed soldier fearless.
	if (AICombatTeam(pSoldier) && pSoldier->stats.bLifeMax > 0)
	{
		const INT32 iHealthPercent = (100 * pSoldier->stats.bLife) / pSoldier->stats.bLifeMax;

		if (iHealthPercent < 15)
			bMoraleCategory = min(bMoraleCategory, MORALE_HOPELESS);
		else if (iHealthPercent < 25)
			bMoraleCategory = min(bMoraleCategory, MORALE_WORRIED);
		else if (iHealthPercent < 50)
			bMoraleCategory = min(bMoraleCategory, MORALE_NORMAL);

		INT32 iStress = AILocalStress(pSoldier);
		if (iStress >= 80)
			bMoraleCategory = min(bMoraleCategory, MORALE_HOPELESS);
		else if (iStress >= 58)
			bMoraleCategory = min(bMoraleCategory, MORALE_WORRIED);
		else if (iStress >= 35)
			bMoraleCategory = min(bMoraleCategory, MORALE_NORMAL);
	}

	// check limits
	bMoraleCategory = max(bMoraleCategory, MORALE_HOPELESS);
	bMoraleCategory = min(bMoraleCategory, MORALE_FEARLESS);

	return(bMoraleCategory);
}

INT32 CalcManThreatValue( SOLDIERTYPE *pEnemy, INT32 sMyGrid, UINT8 ubReduceForCover, SOLDIERTYPE * pMe )
{
	if (!pEnemy)
		return -999;

	// Current combat state is legal only under direct personal observation. A stale
	// contact must not reveal current AP, HP, weapon, armour, shock, breath, bleeding,
	// assignment, stance or whether the merc secretly left the sector.
	const BOOLEAN fPersonallyObservesThreatState =
		pMe &&
		PersonalKnowledge(pMe, pEnemy->ubID) == SEEN_CURRENTLY &&
		LOS_Raised(pMe, pEnemy, CALC_FROM_ALL_DIRS) > 0;

	const BOOLEAN fKnowledgeBoundEstimate =
		pMe && AICombatTeam(pMe) && !fPersonallyObservesThreatState;

	if (fKnowledgeBoundEstimate)
	{
		INT8 bKnowledge = Knowledge(pMe, pEnemy->ubID);
		if (bKnowledge == NOT_HEARD_OR_SEEN)
			return 1;

		INT32 sKnownGrid = KnownLocation(pMe, pEnemy->ubID);
		INT8 bKnownLevel = KnownLevel(pMe, pEnemy->ubID);
		INT32 iCertainty = ThreatPercent[bKnowledge - OLDEST_HEARD_VALUE];

		// Neutral competent-combatant prior. Confidence changes urgency; hidden live
		// statistics never do. This intentionally errs on the side of respecting a
		// stale threat rather than magically knowing that the target is wounded/down.
		INT32 iThreatValue = 35 + (65 * iCertainty) / 100;

		if (!TileIsOutOfBounds(sMyGrid) && !TileIsOutOfBounds(sKnownGrid))
		{
			INT32 iDistance = PythSpacesAway(sMyGrid, sKnownGrid);
			iThreatValue = (iThreatValue * 18) / (18 + iDistance / 2);

			if (ubReduceForCover)
			{
				BOOLEAN fBelievedLine =
					LocationToLocationLineOfSightTest(
						sKnownGrid, bKnownLevel,
						sMyGrid, pMe->pathing.bLevel,
						TRUE, MAX_VISION_RANGE);
				if (!fBelievedLine)
					iThreatValue = iThreatValue * 40 / 100;
				else if (AnyCoverAtSpot(pMe, sMyGrid))
					iThreatValue = iThreatValue * 75 / 100;
			}
		}

		return __max(1, iThreatValue);
	}

	INT32 iThreatValue = 0;
	BOOLEAN fForCreature = CREATURE_OR_BLOODCAT( pMe );

	// Live-state branch: direct current sight (or legacy non-combat callers) may use
	// the target's actual state.
	if (!pEnemy->bActive || !pEnemy->bInSector || !pEnemy->stats.bLife)
	{
		return -999;
	}

	if ( (gTacticalStatus.bBoxingState == BOXING) && !(pEnemy->flags.uiStatusFlags & SOLDIER_BOXER) )
	{
		return -999;
	}

	if (fForCreature)
	{
		iThreatValue += pEnemy->stats.bLife;
		iThreatValue += pEnemy->bBleeding;
		iThreatValue = (iThreatValue * 10) / (10 + PythSpacesAway( sMyGrid, pEnemy->sGridNo ) );
	}
	else
	{
		iThreatValue += EffectiveExpLevel(pEnemy);
		iThreatValue += 25 * pEnemy->CalcActionPoints() / APBPConstants[AP_MAXIMUM];
		iThreatValue += 25 * pEnemy->bActionPoints / APBPConstants[AP_MAXIMUM] / 2;
		iThreatValue += (pEnemy->stats.bLife / 10);

		if (pEnemy->bAssignment < ON_DUTY )
		{
			iThreatValue += ArmourPercent( pEnemy ) / 4;
			iThreatValue += (pEnemy->stats.bMarksmanship / 5);
			if ( Item[ pEnemy->inv[HANDPOS].usItem ].usItemClass & IC_WEAPON )
				iThreatValue += Weapon[pEnemy->inv[HANDPOS].usItem].ubDeadliness;
		}

		iThreatValue -= (pEnemy->bBleeding / 5);
		iThreatValue -= ((100 - pEnemy->bBreath) / 10);
		iThreatValue -= pEnemy->aiData.bShock;
	}

	if (!TileIsOutOfBounds(sMyGrid) && fPersonallyObservesThreatState)
	{
		if (pEnemy->sLastTarget == sMyGrid)
			iThreatValue += (iThreatValue / 10);
		else if (pEnemy->ubDirection ==
			atan8(CenterX(pEnemy->sGridNo), CenterY(pEnemy->sGridNo),
				CenterX(sMyGrid), CenterY(sMyGrid)))
			iThreatValue += (iThreatValue / 20);
	}

	if (pEnemy->stats.bLife >= OKLIFE)
	{
		if (ubReduceForCover && (!TileIsOutOfBounds(sMyGrid)))
		{
			iThreatValue = (iThreatValue * SoldierToLocationChanceToGetThrough(
				pEnemy, sMyGrid, pMe->pathing.bLevel, 0, pMe->ubID )) / 100;
		}
	}
	else if (iThreatValue > 0)
	{
		iThreatValue /= (4 + (OKLIFE - pEnemy->stats.bLife));
	}

	if (iThreatValue < 1)
		iThreatValue = 1;

#ifdef BETAVERSION
	if (iThreatValue > 250)
	{
		sprintf(tempstr,"CalcManThreatValue: WARNING - %d has a very high threat value of %d",pEnemy->ubID,iThreatValue);
#ifdef RECORDNET
		fprintf(NetDebugFile,"\\t%s\\n",tempstr);
#endif
#ifdef TESTVERSION
		PopMessage(tempstr);
#endif
	}
#endif

	return iThreatValue;
}

// sevenfm: ONGUARD, POINTPATROL, RNDPTPATROL - max roaming if seen enemy recently or under fire
// STATIONARY/SNIPER - 5 tiles roaming
// other orders - max roaming if enemy position is known
INT16 RoamingRange(SOLDIERTYPE *pSoldier, INT32 * pusFromGridNo)
{
	BOOLEAN fOppPosKnown = FALSE;
	BOOLEAN fInCombat = FALSE;
	BOOLEAN fRedAlert = FALSE;
	//BOOLEAN fFriendsNeedHelp = FALSE;

	// sevenfm: in case we want to call this for player mercs
	if ( pSoldier->flags.uiStatusFlags & SOLDIER_PC )
	{
		*pusFromGridNo = pSoldier->sGridNo;
		return MAX_ROAMING_RANGE;
	}

	// sevenfm: for zombies, allow max range
	if (pSoldier->IsZombie())
	{
		*pusFromGridNo = pSoldier->sGridNo;
		return MAX_ROAMING_RANGE;
	}

	if ( CREATURE_OR_BLOODCAT( pSoldier ) )
	{
		if ( pSoldier->aiData.bAlertStatus == STATUS_BLACK )
		{
			*pusFromGridNo = pSoldier->sGridNo; // from current position!
			return(MAX_ROAMING_RANGE);
		}
	}
	if ( pSoldier->aiData.bOrders == POINTPATROL || pSoldier->aiData.bOrders == RNDPTPATROL )
	{
		// roam near NEXT PATROL POINT, not from where merc starts out
		*pusFromGridNo = pSoldier->aiData.sPatrolGrid[pSoldier->aiData.bNextPatrolPnt];
	}
	else
	{
		// roam around where mercs started
		//*pusFromGridNo = pSoldier->sInitialGridNo;
		*pusFromGridNo = pSoldier->aiData.sPatrolGrid[0];
	}

	if( GuyKnowsEnemyPosition(pSoldier) )
	{
		fOppPosKnown = TRUE;
	}
	if( pSoldier->aiData.bAlertStatus >= STATUS_RED )
	{
		fRedAlert = TRUE;
	}
	if( pSoldier->aiData.bUnderFire || GuySawEnemy(pSoldier))
	{
		fInCombat = TRUE;
	}

	switch (pSoldier->aiData.bOrders)
	{
		// JA2 GOLD: give non-NPCs a 5 tile roam range for cover in combat when being shot at
		case STATIONARY:	if( pSoldier->ubProfile != NO_PROFILE || !fInCombat )
							{
								return( 0 );
							}
							else
							{
								return( 5 );
							}		
		case ONGUARD:		if( !fInCombat )
							{
								return( 5 );
							}
							else
							{
								return( MAX_ROAMING_RANGE );
							}
		case CLOSEPATROL:	if( !fOppPosKnown )
							{
								return( 15 );
							}
							else
							{
								return( MAX_ROAMING_RANGE );
							}
		case POINTPATROL:	if( !fOppPosKnown )
							{
								// from nextPatrolGrid, not whereIWas
								return( 10 );
							}
							else
							{
								return( MAX_ROAMING_RANGE );
							}
		case RNDPTPATROL:	if( !fOppPosKnown )
							{
								// from nextPatrolGrid, not whereIWas
								return( 10 );
							}
							else
							{
								return( MAX_ROAMING_RANGE );
							}
		case FARPATROL:		if( !fOppPosKnown )
							{
								return( 25 );
							}
							else
							{
								return( MAX_ROAMING_RANGE );
							}
		case ONCALL:		if( !fOppPosKnown )
							{
								return( 10 );
							}
							else
							{
								return( MAX_ROAMING_RANGE );
							}
		case SEEKENEMY:		*pusFromGridNo = pSoldier->sGridNo; // from current position!
							return(MAX_ROAMING_RANGE);
		case SNIPER:		return ( 5 );
		default:
#ifdef BETAVERSION
			sprintf(tempstr,"%s has invalid orders = %d",pSoldier->GetName(),pSoldier->aiData.bOrders);
			PopMessage(tempstr);
#endif
			return(0);
	}
}

/*INT16 RoamingRange(SOLDIERTYPE *pSoldier, INT32 * pusFromGridNo)
{
	BOOL OppPosKnown = FALSE;
	if ( CREATURE_OR_BLOODCAT( pSoldier ) )
	{
		if ( pSoldier->aiData.bAlertStatus == STATUS_BLACK )
		{
			*pusFromGridNo = pSoldier->sGridNo; // from current position!
			return(MAX_ROAMING_RANGE);
		}
	}
	if ( pSoldier->aiData.bOrders == POINTPATROL || pSoldier->aiData.bOrders == RNDPTPATROL )
	{
		// roam near NEXT PATROL POINT, not from where merc starts out
		*pusFromGridNo = pSoldier->aiData.sPatrolGrid[pSoldier->aiData.bNextPatrolPnt];
	}
	else
	{
		// roam around where mercs started
		//*pusFromGridNo = pSoldier->sInitialGridNo;
		*pusFromGridNo = pSoldier->aiData.sPatrolGrid[0];
	}

	//Do we know about any opponent?
	for(UINT16 oppID = 0; oppID < MAX_NUM_SOLDIERS; oppID++)
	{
		if ( pSoldier->aiData.bOppList[oppID]  !=  NOT_HEARD_OR_SEEN &&  gbPublicOpplist[pSoldier->bTeam][oppID] != NOT_HEARD_OR_SEEN)
		{
			OppPosKnown = TRUE;
			break;
		}
	}

	//TODO: Externalize if people want?
	BOOL fLessRestrictiveRoaming = TRUE;

	switch (pSoldier->aiData.bOrders)
	{
		// JA2 GOLD: give non-NPCs a 5 tile roam range for cover in combat when being shot at
		case STATIONARY:			if (pSoldier->ubProfile != NO_PROFILE || (pSoldier->aiData.bAlertStatus < STATUS_BLACK && !(pSoldier->aiData.bUnderFire)))
									{
										return( 0 );
									}
									else
									{
										return( 5 );
									}		
		case ONGUARD:				return( 5 );
		case CLOSEPATROL:			if (pSoldier->aiData.bAlertStatus < STATUS_RED)
													{
														return( 5 );
													}
													else
													{
														if(!OppPosKnown || !fLessRestrictiveRoaming)
														{
															return( 15 );
														}
														else
														{
															return( 30 );
															//return( MAX_ROAMING_RANGE );
														}
													}
		case POINTPATROL:			if (pSoldier->aiData.bAlertStatus < STATUS_RED)
													{
														return( 10 );
													}
													else
													{
														if(!OppPosKnown || !fLessRestrictiveRoaming)
														{
															return( 20 );
														}
														else
														{
															return( 40 );
															//return( MAX_ROAMING_RANGE );
														}
													}	 // from nextPatrolGrid, not whereIWas
		case RNDPTPATROL:			if (pSoldier->aiData.bAlertStatus < STATUS_RED)
													{
														return( 10 );
													}
													else
													{
														if(!OppPosKnown || !fLessRestrictiveRoaming)
														{
															return( 20 );
														}
														else
														{
															//return( 40 );
															return( MAX_ROAMING_RANGE );
														}
													}// from nextPatrolGrid, not whereIWas
		case FARPATROL:				if (pSoldier->aiData.bAlertStatus < STATUS_RED)
													{
														return( 15 );
													}
													else
													{
														if(!OppPosKnown || !fLessRestrictiveRoaming)
														{
															return( 30 );
														}
														else
														{
															return( MAX_ROAMING_RANGE );
														}
													}
		case ONCALL:					if (pSoldier->aiData.bAlertStatus < STATUS_RED)
													{
														return( 10 );
													}
													else
													{
														if(!OppPosKnown || !fLessRestrictiveRoaming)
														{
															return( 30 );
														}
														else
														{
															//return(50);
															return( MAX_ROAMING_RANGE );
														}
													}
		case SEEKENEMY:				*pusFromGridNo = pSoldier->sGridNo; // from current position!
													return(MAX_ROAMING_RANGE);
		case SNIPER:				return ( 5 );
		default:
#ifdef BETAVERSION
			sprintf(tempstr,"%s has invalid orders = %d",pSoldier->GetName(),pSoldier->aiData.bOrders);
			PopMessage(tempstr);
#endif
			return(0);
	}
}*/


void RearrangePocket(SOLDIERTYPE *pSoldier, INT8 bPocket1, INT8 bPocket2, UINT8 bPermanent)
{
	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"RearrangePocket");
	// NB there's no such thing as a temporary swap for now...
	// 0verhaul:  There is now!  If not permanent, don't lose weapon ready status because the
	// weapon will be restored after the trial situation is finished.
	//SwapObjs( &(pSoldier->inv[bPocket1]), &(pSoldier->inv[bPocket2]) );
	SwapObjs( pSoldier, bPocket1, bPocket2, bPermanent );

	DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"RearrangePocket done");
}

BOOLEAN FindBetterSpotForItem( SOLDIERTYPE * pSoldier, INT8 bSlot )
{
	// looks for a place in the slots to put an item in a hand or armour
	// position, and moves it there.
	if (bSlot >= BIGPOCKSTART)
	{
		return( FALSE );
	}
	if (pSoldier->inv[bSlot].exists() == false)
	{
		// well that's just fine then!
		return( TRUE );
	}

	if(FitsInSmallPocket(&pSoldier->inv[bSlot]) == false)
	{
		// then we're looking for a big pocket
		bSlot = FindEmptySlotWithin( pSoldier, BIGPOCKSTART, MEDPOCKFINAL );
	}
	else
	{
		// try a small pocket first
		bSlot = FindEmptySlotWithin( pSoldier, SMALLPOCKSTART, NUM_INV_SLOTS );
		if (bSlot == NO_SLOT)
		{
			bSlot = FindEmptySlotWithin( pSoldier, BIGPOCKSTART, MEDPOCKFINAL );
		}
	}
	if (bSlot == NO_SLOT)
	{
		return( FALSE );
	}
    DebugMsg (TOPIC_JA2,DBG_LEVEL_3,"findbetterspotforitem: swapping items");
	RearrangePocket(pSoldier, HANDPOS, bSlot, FOREVER );		
	return( TRUE );
}

UINT8 GetTraversalQuoteActionID( INT8 bDirection )
{
	switch( bDirection )
	{
		case NORTHEAST: // east
			return( QUOTE_ACTION_ID_TRAVERSE_EAST );

		case SOUTHEAST: // south
			return( QUOTE_ACTION_ID_TRAVERSE_SOUTH );

		case SOUTHWEST: // west
			return( QUOTE_ACTION_ID_TRAVERSE_WEST );

		case NORTHWEST: // north
			return( QUOTE_ACTION_ID_TRAVERSE_NORTH );

		default:
			return( 0 );
	}
}

UINT8 SoldierDifficultyLevel( SOLDIERTYPE * pSoldier )
{
	INT8 bDifficultyBase;
	INT8 bDifficulty;

	DebugMsg(TOPIC_JA2AI,DBG_LEVEL_3,String("SoldierDifficultyLevel"));
	// difficulty modifier ranges from 0 to 100
	// and we want to end up with a number between 0 and 4 (4=hardest)
	// to a base of 1, divide by 34 to get a range from 1 to 3
	bDifficultyBase = 1 + ( CalcDifficultyModifier( pSoldier->ubSoldierClass ) / 34 );

	switch( pSoldier->ubSoldierClass )
	{
		case SOLDIER_CLASS_ADMINISTRATOR:
			bDifficulty = bDifficultyBase - 1;
			break;

		case SOLDIER_CLASS_ARMY:
			bDifficulty = bDifficultyBase;
			break;

		case SOLDIER_CLASS_ELITE:
			bDifficulty = bDifficultyBase + 1;
			break;

		// Militia use the same AI-skill tier progression as enemy human troops.
		// CalcDifficultyModifier() already applies the intended militia-specific
		// game-difficulty/progress/location scaling, so do not hard-code their AI level.
		case SOLDIER_CLASS_GREEN_MILITIA:
			bDifficulty = bDifficultyBase - 1;
			break;

		case SOLDIER_CLASS_REG_MILITIA:
			bDifficulty = bDifficultyBase;
			break;

		case SOLDIER_CLASS_ELITE_MILITIA:
			bDifficulty = bDifficultyBase + 1;
			break;

		case SOLDIER_CLASS_ZOMBIE:
			bDifficulty = bDifficultyBase;
			break;

		default:
			if (pSoldier->bTeam == CREATURE_TEAM)
			{
				bDifficulty = bDifficultyBase + pSoldier->pathing.bLevel / 4;
			}
			else // civ...
			{
				bDifficulty = (bDifficultyBase + pSoldier->pathing.bLevel / 4) - 1;
			}
			break;

	}

	bDifficulty = __max( bDifficulty, 0 );
	bDifficulty = __min( bDifficulty, 4 );

	return( (UINT8) bDifficulty );
}

BOOLEAN ValidCreatureTurn( SOLDIERTYPE * pCreature, INT8 bNewDirection )
{
	INT8	bDirChange;
	INT8	bTempDir;
	INT8	bLoop;
	BOOLEAN	fFound;

	bDirChange = (INT8) QuickestDirection( pCreature->ubDirection, bNewDirection );

	for( bLoop = 0; bLoop < 2; bLoop++ )
	{
		fFound = TRUE;

		bTempDir = pCreature->ubDirection;

		do
		{

			bTempDir += bDirChange;
			if (bTempDir < NORTH)
			{
				bTempDir = NORTHWEST;
			}
			else if (bTempDir > NORTHWEST)
			{
				bTempDir = NORTH;
			}
			if (!pCreature->InternalIsValidStance( bTempDir, ANIM_STAND ))
			{
				fFound = FALSE;
				break;
			}

		} while ( bTempDir != bNewDirection );

		if ( fFound )
		{
			break;
		}
		else if ( bLoop > 0 )
		{
			// can't find a dir!
			return( FALSE );
		}
		else
		{
			// try the other direction
			bDirChange = bDirChange * -1;
		}
	}

	return( TRUE );
}

INT32 RangeChangeDesire( SOLDIERTYPE * pSoldier )
{
	INT32 iRangeFactorMultiplier;

	// Morale controls willingness to accept risk, not desired engagement range.
	// For human AI, confidence above NORMAL no longer creates an automatic urge
	// to move closer; fragile morale can still reduce willingness to advance.
	if (AICombatTeam(pSoldier))
	{
		switch (pSoldier->aiData.bAIMorale)
		{
		case MORALE_HOPELESS: iRangeFactorMultiplier = -1; break;
		case MORALE_WORRIED:  iRangeFactorMultiplier = 0; break;
		default:              iRangeFactorMultiplier = 1; break;
		}
	}
	else
	{
		iRangeFactorMultiplier = pSoldier->aiData.bAIMorale - 1;
	}

	INT8 bBonus = 0;
	if ( !AICheckHasGun(pSoldier) )
	{
		// Truly unarmed soldiers should preserve themselves. A knife/melee specialist
		// still needs to close distance, so distinguish that case from no weapon at all.
		if (AICombatTeam(pSoldier) && FindAIUsableObjClass(pSoldier, IC_WEAPON) == NO_SLOT)
			bBonus = -2;
		else
			bBonus = 2;
	}
	// bonus if weapon range is short
	else if( GuySawEnemy(pSoldier, SEEN_LAST_TURN) && AICheckShortWeaponRange(pSoldier) )
	{
		bBonus = 1;
	}

	switch (pSoldier->aiData.bAttitude)
	{
	//case DEFENSIVE:		iRangeFactorMultiplier +=	__max(-1, bBonus); break;
	case DEFENSIVE:		iRangeFactorMultiplier +=	__max(0, bBonus); break;
	case BRAVESOLO:		iRangeFactorMultiplier +=	__max(2, bBonus); break;
	case BRAVEAID:		iRangeFactorMultiplier +=	__max(2, bBonus); break;
	case CUNNINGSOLO:	iRangeFactorMultiplier +=	__max(0, bBonus); break;
	case CUNNINGAID:	iRangeFactorMultiplier +=	__max(0, bBonus); break;
	case ATTACKSLAYONLY:
	case AGGRESSIVE:	iRangeFactorMultiplier +=	__max(1, bBonus); break;
	}

	if ( (pSoldier->aiData.bOrders == SEEKENEMY || !AICombatTeam(pSoldier)) &&
		gTacticalStatus.bConsNumTurnsWeHaventSeenButEnemyDoes > 0 )
	{
		iRangeFactorMultiplier += gTacticalStatus.bConsNumTurnsWeHaventSeenButEnemyDoes;
	}
	
	return( iRangeFactorMultiplier );
}

BOOLEAN ArmySeesOpponents( void )
{
	INT32				cnt;
	SOLDIERTYPE *		pSoldier;

	for ( cnt = gTacticalStatus.Team[ ENEMY_TEAM ].bFirstID; cnt <= gTacticalStatus.Team[ ENEMY_TEAM ].bLastID; cnt++ )
	{
		pSoldier = MercPtrs[ cnt ];

		if ( pSoldier->bActive && pSoldier->bInSector && pSoldier->stats.bLife >= OKLIFE && pSoldier->aiData.bOppCnt > 0 )
		{
			return( TRUE );
		}
	}

	return( FALSE );
}

#ifdef DEBUGDECISIONS
void AIPopMessage ( STR16 str )
{
	DebugAI(str);
}

void AIPopMessage ( const STR8	str )
{
	STR tempstr;
	sprintf( tempstr,"%s", str);
	DebugAI(tempstr);
}

void AINumMessage(const STR8	str, INT32 num)
{
	STR tempstr;
	sprintf( tempstr,"%s %d", str, num);
	DebugAI(tempstr);
}

void AINameMessage(SOLDIERTYPE * pSoldier,const STR8	str,INT32 num)
{
	STR tempstr;
	sprintf( tempstr,"%d %s %d",pSoldier->GetName() , str, num);
	DebugAI( tempstr );
}
#endif
/////////////////////////////////////////////////////////////////////////////////////////////////
// HEADROCK:
//
// The following function(s) are part of my half-assed attempt to have the AI analyze the tactical
// situation, by comparing (known) squad sizes, the state of all combatants, and the orders of all
// friendlies. The idea is to return a value called "TacticalSituation" which can tell a combatant
// whether he should try to undertake a smarter course of action.
/////////////////////////////////////////////////////////////////////////////////////////////////
/*
INT16 AssessTacticalSituation( INT8 bTeam )
{
	UINT16 ubFriendlyTeamTacticalValue = 0;
	UINT16 ubEnemyTeamTacticalValue = 0;
	UINT8 ubSoldierTacticalThreat;
	INT16 ubTacticalSituation;
	UINT16 cnt;
	SOLDIERTYPE * pSoldier;
	
	// begin loop through all MERCs.
	for ( cnt = gTacticalStatus.Team[ OUR_TEAM ].bFirstID; cnt <= gTacticalStatus.Team[ OUR_TEAM ].bLastID; cnt++ )
	{
		pSoldier = MercPtrs[ cnt ];
		ubSoldierTacticalThreat = CalcStraightThreatValue( pSoldier );
		// Player-controlled Mercs are 1.5 times more threatening than AIs
		if (pSoldier->flags.uiStatusFlags & SOLDIER_PC)
			ubSoldierTacticalThreat = (UINT8)((float)ubSoldierTacticalThreat * 1.5);
		
		// Assess Threat
		if (bTeam == OUR_TEAM || bTeam == MILITIA_TEAM)
		{
			// Friendly!
			ubFriendlyTeamTacticalValue += ubSoldierTacticalThreat;
		}
		else
		{
			// Enemy!
			if ( TeamSeesOpponent( ENEMY_TEAM, pSoldier ) )
				ubEnemyTeamTacticalValue += ubSoldierTacticalThreat;

		}
	}

	// begin loop through all Militia.
	for ( cnt = gTacticalStatus.Team[ MILITIA_TEAM ].bFirstID; cnt <= gTacticalStatus.Team[ MILITIA_TEAM ].bLastID; cnt++ )
	{
		pSoldier = MercPtrs[ cnt ];
		ubSoldierTacticalThreat = CalcStraightThreatValue( pSoldier );
		
		// Assess Threat
		if (bTeam == OUR_TEAM || bTeam == MILITIA_TEAM)
		{
			// Friendly!
			ubFriendlyTeamTacticalValue += ubSoldierTacticalThreat;
		}
		else
		{
			// Enemy!
			if ( TeamSeesOpponent( ENEMY_TEAM, pSoldier ) )
				ubEnemyTeamTacticalValue += ubSoldierTacticalThreat;
		}
	}	

	// begin loop through all Enemies.
	for ( cnt = gTacticalStatus.Team[ ENEMY_TEAM ].bFirstID; cnt <= gTacticalStatus.Team[ ENEMY_TEAM ].bLastID; cnt++ )
	{
		pSoldier = MercPtrs[ cnt ];
		ubSoldierTacticalThreat = CalcStraightThreatValue( pSoldier );
		
		// Assess Threat

		if (bTeam == ENEMY_TEAM)
		{
			// Friendly!
			ubFriendlyTeamTacticalValue += ubSoldierTacticalThreat;
		}
		else
		{
			// Enemy!
			if ( TeamSeesOpponent( OUR_TEAM, pSoldier ) || TeamSeesOpponent ( MILITIA_TEAM, pSoldier) )
				ubEnemyTeamTacticalValue += ubSoldierTacticalThreat;
		}
		
	}

	ubTacticalSituation = ubEnemyTeamTacticalValue - ubFriendlyTeamTacticalValue;

	return (ubTacticalSituation);


}
*/

// HEADROCK: Function to check whether a team can see the specified soldier.
BOOLEAN TeamSeesOpponent( INT8 bTeam, SOLDIERTYPE * pOpponent )
{
	SOLDIERTYPE * pSoldier;
	UINT16 cnt;

	// This assertion can be safely removed, assuming the program does what it should. It simply checks
	// whether the "opponent" is on the same team being checked. That should be avoided when calling this
	// function.
	//Assert( pOpponent->bTeam != bTeam );

	// We're checking Merc/Militia visibility
	if (bTeam == OUR_TEAM || bTeam == MILITIA_TEAM )
	{
		for ( cnt = gTacticalStatus.Team[ MILITIA_TEAM ].bFirstID; cnt <= gTacticalStatus.Team[ MILITIA_TEAM ].bLastID; cnt++ )
		{
			pSoldier = MercPtrs[ cnt ];

			if (pSoldier->bActive && pSoldier->bInSector && pSoldier->stats.bLife >= OKLIFE)
			{


				if (pSoldier->aiData.bOppList[ pOpponent->ubID ] == SEEN_CURRENTLY &&
					LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0)
					return ( TRUE );
			}
		}
		for ( cnt = gTacticalStatus.Team[ OUR_TEAM ].bFirstID; cnt <= gTacticalStatus.Team[ OUR_TEAM ].bLastID; cnt++ )
		{
			pSoldier = MercPtrs[ cnt ];

			if (pSoldier->bActive && pSoldier->bInSector && pSoldier->stats.bLife >= OKLIFE)
			{
				// This assertion can be safely removed, assuming the program does what it should. It simply checks
				// whether the "opponent" is on the same team being checked. That should be avoided when calling this
				// function.
				//Assert( pOpponent->bSide != bSide );

				if (pSoldier->aiData.bOppList[ pOpponent->ubID ] == SEEN_CURRENTLY &&
					LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0)
					return ( TRUE );
			}
		}		
		
		return ( FALSE );
	}
	// Check enemy visibility
	else if (bTeam == ENEMY_TEAM)
	{
		for ( cnt = gTacticalStatus.Team[ ENEMY_TEAM ].bFirstID; cnt <= gTacticalStatus.Team[ ENEMY_TEAM ].bLastID; cnt++ )
		{
			pSoldier = MercPtrs[ cnt ];

			if (pSoldier->bActive && pSoldier->bInSector && pSoldier->stats.bLife >= OKLIFE)
			{
				// This assertion can be safely removed, assuming the program does what it should. It simply checks
				// whether the "opponent" is on the same team being checked. That should be avoided when calling this
				// function.
				//Assert( pOpponent->bSide != bSide );

				if (pSoldier->aiData.bOppList[ pOpponent->ubID ] == SEEN_CURRENTLY &&
					LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0)
					return ( TRUE );
			}
		}	
		return ( FALSE );
	}

	else
		return (FALSE);

}

// HEADROCK: Function to assess an enemy's threat value without "me" argument.
INT32 CalcStraightThreatValue( SOLDIERTYPE *pEnemy )
{
	INT32	iThreatValue = 0;

	// If man is inactive, at base, on assignment, dead, unconscious
	if (!pEnemy->bActive || !pEnemy->bInSector || !pEnemy->stats.bLife )
	{
		// he's no threat at all, return a negative number
		iThreatValue = 0;
		return(iThreatValue);
	}

	else
	{
		// ADD twice the man's level (2-20)
		iThreatValue += EffectiveExpLevel(pEnemy); // SANDRO - find precise effective exp level

		// ADD man's total action points (10-35)
		// sevenfm: r7810 fix
		//iThreatValue += pEnemy->CalcActionPoints();
		iThreatValue += 25 * pEnemy->CalcActionPoints() / APBPConstants[AP_MAXIMUM];

		// ADD 1/2 of man's current action points (4-17)
		// sevenfm: r7810 fix
		//iThreatValue += (pEnemy->bActionPoints / 2);
		iThreatValue += 25 * pEnemy->bActionPoints / APBPConstants[AP_MAXIMUM] / 2;

		// ADD 1/10 of man's current health (0-10)
		iThreatValue += (pEnemy->stats.bLife / 10);

		if (pEnemy->bAssignment < ON_DUTY )
		{
			// ADD 1/4 of man's protection percentage (0-25)
			iThreatValue += ArmourPercent( pEnemy ) / 4;

			// ADD 1/5 of man's marksmanship skill (0-20)
			iThreatValue += (pEnemy->stats.bMarksmanship / 5);

			if ( Item[ pEnemy->inv[HANDPOS].usItem ].usItemClass & IC_WEAPON )
			{
				// ADD the deadliness of the item(weapon) he's holding (0-50)
				iThreatValue += Weapon[pEnemy->inv[HANDPOS].usItem].ubDeadliness;
			}
		}

		// SUBTRACT 1/5 of man's bleeding (0-20)
		iThreatValue -= (pEnemy->bBleeding / 5);

		// SUBTRACT 1/10 of man's breath deficiency (0-10)
		iThreatValue -= ((100 - pEnemy->bBreath) / 10);

		// SUBTRACT man's current shock value
		iThreatValue -= pEnemy->aiData.bShock;
	}

	// if this man is conscious
	if (pEnemy->stats.bLife < OKLIFE)
	{
		// if he's still something of a threat
		if (iThreatValue > 0)
		{
			// drastically reduce his threat value (divide by 5 to 18)
			iThreatValue /= (4 + (OKLIFE - pEnemy->stats.bLife));
		}
	}

	// threat value of any opponent can never drop below 1
	if (iThreatValue < 0)
	{
		iThreatValue = 0;
	}

	return(iThreatValue);
}

// Flugente: get the id of the closest soldier with a specific flag that we can currently see
UINT8 GetClosestFlaggedSoldierID( SOLDIERTYPE * pSoldier, INT16 aRange, UINT8 auTeam, UINT32 aFlag, BOOLEAN fCheckSight )
{
	UINT8 id = NOBODY;

	UINT32				uiLoop;
	BOOLEAN				fRangeRestricted = FALSE, fFound = FALSE;
	SOLDIERTYPE *		pFriend;
	INT16				range = aRange;

	// go through each soldier, looking for "friends" (soldiers on same side)
	for (uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		pFriend = MercSlots[ uiLoop ];

		// if this merc is inactive, not in sector, or dead
		if (!pFriend)
			continue;
		
		if ( auTeam != pFriend->bTeam )
			continue;

		// check for flag
		if ( !(pFriend->usSoldierFlagMask & aFlag) )
			continue;
				
		// skip ourselves
		if (pFriend->ubID == pSoldier->ubID)
			continue;

		// this is not for tanks
		if (TANK(pFriend))
			continue;

		// skip if this guy is dead
		if (pFriend->stats.bLife < OKLIFE)
			continue;
				
		// if we're not already neighbors
		if (SpacesAway(pSoldier->sGridNo, pFriend->sGridNo) < range)
		{
			// can we see this guy?
			if ( !fCheckSight || SoldierTo3DLocationLineOfSightTest( pSoldier, pFriend->sGridNo, pSoldier->pathing.bLevel, 3, TRUE, CALC_FROM_WANTED_DIR ) )
			{
				range = SpacesAway(pSoldier->sGridNo,pFriend->sGridNo);
				id = pFriend->ubID;
			}
		}
	}
		
	return id;
}

// sevenfm: additional functions used for AI

BOOLEAN InSmokeNearby(INT32 sGridNo, INT8 bLevel)
{
	if (TileIsOutOfBounds(sGridNo))
		return FALSE;

	if (gpWorldLevelData[sGridNo].ubExtFlags[bLevel] & MAPELEMENT_EXT_SMOKE)
		return TRUE;

	for (UINT8 ubDirection = 0; ubDirection < NUM_WORLD_DIRECTIONS; ++ubDirection)
	{
		INT32 sTempGridNo = NewGridNo(sGridNo, DirectionInc(ubDirection));
		if (sTempGridNo == sGridNo)
			continue;

		UINT8 ubMovementCost = gubWorldMovementCosts[sTempGridNo][ubDirection][bLevel];
		if (ubMovementCost < TRAVELCOST_BLOCKED &&
			(gpWorldLevelData[sTempGridNo].ubExtFlags[bLevel] & MAPELEMENT_EXT_SMOKE))
		{
			return TRUE;
		}
	}

	return FALSE;
}

INT16 MaxNormalVisionDistance( void )
{
	if( NightTime() )
	{
		return gGameExternalOptions.ubStraightSightRange * STRAIGHT_RATIO;
	}
	return gGameExternalOptions.ubStraightSightRange * 2 * STRAIGHT_RATIO;
}

// sevenfm: check friendly soldiers between me and noise gridno
// count only friends that are active and not stationary/onguard/sniper
UINT8 CountFriendsInDirection(SOLDIERTYPE *pSoldier, UINT8 ubDirection, INT16 sDistance, BOOLEAN fCheckSight)
{
	SOLDIERTYPE * pFriend;
	UINT8 ubFriends = 0;

	CHECKF(pSoldier);

	if (ubDirection == DIRECTION_IRRELEVANT)
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
			AIDirection(pSoldier->sGridNo, pFriend->sGridNo) == ubDirection &&
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) <= sDistance &&
			(!fCheckSight || LocationToLocationLineOfSightTest(pSoldier->sGridNo, pSoldier->pathing.bLevel, pFriend->sGridNo, pFriend->pathing.bLevel, TRUE, MAX_VISION_RANGE)))
		{
			ubFriends++;
		}
	}

	return ubFriends;
}

UINT8 CountFriendsInDirectionFromSpot(SOLDIERTYPE *pSoldier, INT32 sSpot, UINT8 ubDirection, INT16 sDistance)
{
	SOLDIERTYPE * pFriend;
	UINT8 ubFriends = 0;

	CHECKF(pSoldier);

	if (ubDirection == DIRECTION_IRRELEVANT)
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
			AIDirection(sSpot, pFriend->sGridNo) == ubDirection &&
			PythSpacesAway(sSpot, pFriend->sGridNo) <= sDistance)
		{
			ubFriends++;
		}
	}

	return ubFriends;
}

// sevenfm: check friendly soldiers between me and noise gridno
// count only friends that are active and not stationary/onguard/sniper
UINT8 CountFriendsBetweenMeAndSpotFromSpot(SOLDIERTYPE *pSoldier, INT32 sTargetGridNo)
{
	SOLDIERTYPE * pFriend;
	UINT8 ubFriendDir, ubMyDir;
	UINT8 ubFriends = 0;

	CHECKF(pSoldier);

	if (TileIsOutOfBounds(sTargetGridNo))
	{
		return 0;
	}

	ubMyDir = AIDirection(sTargetGridNo, pSoldier->sGridNo);

	// Run through each friendly.
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID; iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; iCounter++)
	{
		pFriend = MercPtrs[iCounter];

		if (!pFriend)
		{
			continue;
		}

		ubFriendDir = AIDirection(sTargetGridNo, pFriend->sGridNo);

		if (pFriend != pSoldier &&
			pFriend->bActive &&
			pFriend->bInSector &&
			AISameFireteam(pSoldier, pFriend) &&
			pFriend->stats.bLife >= OKLIFE &&
			!pFriend->bCollapsed &&
			!pFriend->bBreathCollapsed &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW) &&
			!(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) &&
			!AIDisengagementActive(pFriend) && !AIEscapeActive(pFriend) &&
			pFriend->stats.bLife >= pFriend->stats.bLifeMax / 2 &&
			pFriend->aiData.bOrders > ONGUARD &&
			(ubFriendDir == ubMyDir || ubFriendDir == gOneCDirection[ubMyDir] || ubFriendDir == gOneCCDirection[ubMyDir]) &&
			PythSpacesAway(sTargetGridNo, pFriend->sGridNo) < PythSpacesAway(sTargetGridNo, pSoldier->sGridNo))
		{
			ubFriends++;
		}
	}

	return ubFriends;
}

// count mobile friends that are in BLACK state and not in a dangerous place or have 3/4 APs or hit enemy recently
// this is mostly used to check if we can cross dangerous area (in light at night or fresh corpses)
UINT8 CountFriendsBlack( SOLDIERTYPE *pSoldier, INT32 sClosestOpponent )
{
	CHECKF(pSoldier);

	SOLDIERTYPE * pFriend;
	UINT8 ubFriendCount = 0;
	INT32 sFriendClosestOpponent;

	// by default, use closest known opponent
	if( sClosestOpponent == NOWHERE )
	{
		sClosestOpponent = ClosestKnownOpponent( pSoldier, NULL, NULL );
	}

	if(TileIsOutOfBounds(sClosestOpponent))
	{
		return 0;
	}

	// Run through each friendly.
	for ( UINT8 iCounter = gTacticalStatus.Team[ pSoldier->bTeam ].bFirstID ; iCounter <= gTacticalStatus.Team[ pSoldier->bTeam ].bLastID ; iCounter ++ )
	{
		pFriend = MercPtrs[ iCounter ];		

		// Make sure that character is alive, not too shocked, and conscious
		if (pFriend != pSoldier && 
			pFriend->bActive &&
			pFriend->bInSector &&
			AISameFireteam(pSoldier, pFriend) &&
			pFriend->stats.bLife >= OKLIFE &&
			!(pFriend->usSoldierFlagMask & SOLDIER_POW) &&
			!pFriend->IsCowering() &&
			!pFriend->IsUnconscious() &&
			!AIDisengagementActive(pFriend) &&
			!AIEscapeActive(pFriend))
		{
			sFriendClosestOpponent = ClosestSeenOpponent( pFriend, NULL, NULL );
			if(!TileIsOutOfBounds(sFriendClosestOpponent) &&
				PythSpacesAway( sClosestOpponent, sFriendClosestOpponent ) < DAY_VISION_RANGE / 4 &&
				pFriend->aiData.bAlertStatus == STATUS_BLACK &&
				pFriend->stats.bLife > pFriend->stats.bLifeMax / 2 &&
				pFriend->bInitialActionPoints > APBPConstants[AP_MINIMUM] &&
				(pFriend->bActionPoints == pFriend->bInitialActionPoints ||
				pFriend->LastAttackHit() ||
				pFriend->usSoldierFlagMask2 & SOLDIER_SUCCESSFUL_ATTACK ||
				pFriend->LastTargetSuppressed() ||
				!AICorpseWarningKnown(pFriend, pFriend->sGridNo, pFriend->pathing.bLevel) && !InLightAtNight(pFriend->sGridNo, pFriend->pathing.bLevel) && !pFriend->aiData.bUnderFire))
			{
				ubFriendCount++;
			}
		}
	}

	return ubFriendCount;
}

// sevenfm: count nearby friend soldiers (on roof)
UINT8 CountNearbyFriendsOnRoof( SOLDIERTYPE *pSoldier, INT32 sGridNo, UINT8 ubDistance )
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
			PythSpacesAway( sGridNo, pFriend->sGridNo ) <= ubDistance &&
			pFriend->pathing.bLevel > 0)
		{
			ubFriendCount++;
		}
	}

	return ubFriendCount;
}

BOOLEAN AICombatTeam(SOLDIERTYPE *pSoldier)
{
	return pSoldier && (pSoldier->bTeam == ENEMY_TEAM || pSoldier->bTeam == MILITIA_TEAM);
}

UINT8 AICountNearbyOperationalFriends(SOLDIERTYPE *pSoldier, INT32 sGridNo, UINT8 ubDistance)
{
	if (!pSoldier || TileIsOutOfBounds(sGridNo))
		return 0;

	UINT8 ubCount = 0;
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier || !pFriend->bActive || !pFriend->bInSector)
			continue;

		BOOLEAN fOperationalAlly = (pFriend->bTeam == pSoldier->bTeam);
		BOOLEAN fVisiblePlayerSupport =
			pSoldier->bTeam == MILITIA_TEAM && pFriend->bTeam == OUR_TEAM;
		if (!fOperationalAlly && !fVisiblePlayerSupport)
			continue;

		INT32 iDistance = PythSpacesAway(sGridNo, pFriend->sGridNo);
		if (iDistance > ubDistance)
			continue;

		// Player merc support is physical/observable support, not shared knowledge.
		// Do not let militia count a merc through walls merely because he is nearby.
		if (fVisiblePlayerSupport && iDistance > 1 &&
			LOS_Raised(pSoldier, pFriend, CALC_FROM_ALL_DIRS) <= 0)
		{
			continue;
		}

		if (pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			(fOperationalAlly && (AIDisengagementActive(pFriend) || AIEscapeActive(pFriend))))
		{
			continue;
		}
		++ubCount;
	}

	return ubCount;
}

// Escape intent is defined later in this file; remnant absorption needs to cancel
// a stale sector-flight state immediately when the soldier successfully rejoins.
static void AIClearEscapeState(SOLDIERTYPE *pSoldier);

// Shared enemy/militia fireteam coordination. This state is sector-local and intentionally lives
// outside SOLDIERTYPE so it does not change the savegame structure.
#define AI_FIRETEAM_NONE 0
#define AI_FIRETEAM_TARGET 8
#define AI_FIRETEAM_MAX_NORMAL 9
#define AI_FIRETEAM_MAX_MERGED 10

extern UINT32 guiTurnCnt;

static UINT8 gubAIFireteam[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIFireteamIdentity[MAX_NUM_SOLDIERS] = { 0 };
// Fireteam IDs are globally unique inside the tactical sector. This records
// which combat team owns each ID so enemy and militia coordination never mix.
static INT8 gbAIFireteamTeam[256] = { 0 };
// After a shattered one/two-man element is absorbed into another fireteam, keep
// that decision sticky for a couple of turns. This prevents the same survivor
// from immediately converting local isolation into full sector escape before he
// has had a fair chance to close on his new element.
static UINT32 guiAIFireteamRejoinUntilTurn[MAX_NUM_SOLDIERS] = { 0 };
static UINT8 gubAINextFireteam = 1;
static INT16 gsAIFireteamSectorX = -1;
static INT16 gsAIFireteamSectorY = -1;
static INT8 gbAIFireteamSectorZ = -1;
static BOOLEAN gfAIFireteamsSeeded = FALSE;
static UINT32 guiAIFireteamLastTurnStamp = 0;
static INT16 gsAIFireteamKnownMenInSector = -1;

static BOOLEAN AIEnemyFireteamEligible(SOLDIERTYPE *pSoldier)
{
	return pSoldier && AICombatTeam(pSoldier) && pSoldier->bActive &&
		pSoldier->bInSector && pSoldier->stats.bLife > 0 &&
		!(pSoldier->usSoldierFlagMask & SOLDIER_POW);
}

// When only a handful of operational fighters remain, treat them as one local
// tactical element. This shares team-state/formation logic only; opponent
// knowledge remains bounded by the existing personal/public knowledge checks.
static UINT8 AICombatTeamOperationalCount(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || !AICombatTeam(pSoldier))
		return 0;

	UINT8 ubCount = 0;
	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || !pFriend->bActive || !pFriend->bInSector ||
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			AIDisengagementActive(pFriend) || AIEscapeActive(pFriend))
		{
			continue;
		}
		++ubCount;
	}
	return ubCount;
}

static BOOLEAN AISmallUnitTeamMode(SOLDIERTYPE *pSoldier)
{
	UINT8 ubOperational = AICombatTeamOperationalCount(pSoldier);
	return ubOperational >= 2 && ubOperational <= 5;
}

static BOOLEAN AIEnemyFixedMissionRole(SOLDIERTYPE *pSoldier)
{
	return pSoldier &&
		(pSoldier->aiData.bOrders == STATIONARY ||
		 pSoldier->aiData.bOrders == ONGUARD ||
		 pSoldier->aiData.bOrders == SNIPER);
}

static void AIResetFireteamsForSector(void)
{
	UINT32 uiTurnStamp = guiTurnCnt + 1;
	BOOLEAN fTurnRollback =
		(guiAIFireteamLastTurnStamp != 0 && uiTurnStamp < guiAIFireteamLastTurnStamp);

	if (!fTurnRollback &&
		gsAIFireteamSectorX == gWorldSectorX && gsAIFireteamSectorY == gWorldSectorY &&
		gbAIFireteamSectorZ == gbWorldSectorZ)
	{
		guiAIFireteamLastTurnStamp = uiTurnStamp;
		return;
	}

	gsAIFireteamSectorX = gWorldSectorX;
	gsAIFireteamSectorY = gWorldSectorY;
	gbAIFireteamSectorZ = gbWorldSectorZ;
	guiAIFireteamLastTurnStamp = uiTurnStamp;
	gsAIFireteamKnownMenInSector = -1;
	gubAINextFireteam = 1;
	gfAIFireteamsSeeded = FALSE;
	for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
	{
		gubAIFireteam[i] = AI_FIRETEAM_NONE;
		guiAIFireteamIdentity[i] = 0;
		guiAIFireteamRejoinUntilTurn[i] = 0;
	}
	for (UINT16 i = 0; i < 256; ++i)
		gbAIFireteamTeam[i] = -1;
}

static UINT8 AIFireteamCountById(UINT8 ubFireteam, BOOLEAN fReadyOnly)
{
	if (ubFireteam == AI_FIRETEAM_NONE) return 0;
	UINT8 ubCount = 0;
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!AIEnemyFireteamEligible(pFriend) || pFriend->ubID >= MAX_NUM_SOLDIERS ||
			guiAIFireteamIdentity[pFriend->ubID] != pFriend->uiUniqueSoldierIdValue ||
			gubAIFireteam[pFriend->ubID] != ubFireteam) continue;
		if (fReadyOnly && (pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed ||
			pFriend->bBreathCollapsed || (pFriend->usSoldierFlagMask & SOLDIER_POW))) continue;
		++ubCount;
	}
	return ubCount;
}

static UINT8 AIFireteamOperationalCountById(UINT8 ubFireteam)
{
	if (ubFireteam == AI_FIRETEAM_NONE) return 0;
	UINT8 ubCount = 0;
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!AIEnemyFireteamEligible(pFriend) || pFriend->ubID >= MAX_NUM_SOLDIERS ||
			guiAIFireteamIdentity[pFriend->ubID] != pFriend->uiUniqueSoldierIdValue ||
			gubAIFireteam[pFriend->ubID] != ubFireteam || pFriend->stats.bLife < OKLIFE ||
			pFriend->bCollapsed || pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			AIDisengagementActive(pFriend) || AIEscapeActive(pFriend)) continue;
		++ubCount;
	}
	return ubCount;
}

static BOOLEAN AIFireteamRegroupableMember(SOLDIERTYPE *pMember)
{
	return AIEnemyFireteamEligible(pMember) && pMember->ubID < MAX_NUM_SOLDIERS &&
		pMember->stats.bLife >= OKLIFE && !pMember->bCollapsed && !pMember->bBreathCollapsed &&
		!(pMember->usSoldierFlagMask & SOLDIER_POW);
}

static UINT8 AIFireteamRegroupableCountById(UINT8 ubFireteam)
{
	if (ubFireteam == AI_FIRETEAM_NONE)
		return 0;

	UINT8 ubCount = 0;
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pMember = MercPtrs[iCounter];
		if (!AIFireteamRegroupableMember(pMember) ||
			guiAIFireteamIdentity[pMember->ubID] != pMember->uiUniqueSoldierIdValue ||
			gubAIFireteam[pMember->ubID] != ubFireteam)
		{
			continue;
		}
		++ubCount;
	}
	return ubCount;
}
static UINT8 AIFireteamRegroupingStrength(SOLDIERTYPE *pSoldier)
{
	if (!AIEnemyFireteamEligible(pSoldier))
		return 0;

	return AIFireteamRegroupableCountById(AIFireteamId(pSoldier));
}

static BOOLEAN AIFireteamHasSoldierFlag(UINT8 ubFireteam, UINT32 uiFlag)
{
	if (ubFireteam == AI_FIRETEAM_NONE)
		return FALSE;

	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pMember = MercPtrs[iCounter];
		if (!AIEnemyFireteamEligible(pMember) || pMember->ubID >= MAX_NUM_SOLDIERS ||
			guiAIFireteamIdentity[pMember->ubID] != pMember->uiUniqueSoldierIdValue ||
			gubAIFireteam[pMember->ubID] != ubFireteam)
		{
			continue;
		}
		if (pMember->usSoldierFlagMask & uiFlag)
			return TRUE;
	}
	return FALSE;
}

static INT32 AIFireteamDistanceToSpot(UINT8 ubFireteam, INT32 sSpot)
{
	INT32 iBest = 10000;
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!AIEnemyFireteamEligible(pFriend) || pFriend->ubID >= MAX_NUM_SOLDIERS ||
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || pFriend->bBreathCollapsed ||
			guiAIFireteamIdentity[pFriend->ubID] != pFriend->uiUniqueSoldierIdValue ||
			gubAIFireteam[pFriend->ubID] != ubFireteam) continue;
		iBest = __min(iBest, PythSpacesAway(pFriend->sGridNo, sSpot));
	}
	return iBest;
}

static void AISeedEnemyFireteams(void)
{
	AIResetFireteamsForSector();

	// Role normalization belongs at a stable roster/fireteam boundary, not inside
	// rank queries. This also picks up genuine mid-battle roster growth before a
	// reinforcement is attached to an element.
	EnsureEnemyCommandRoles();

	if (gfAIFireteamsSeeded) return;

	const INT8 bCombatTeams[2] = { ENEMY_TEAM, MILITIA_TEAM };
	for (UINT8 ubSide = 0; ubSide < 2; ++ubSide)
	{
		INT8 bTeam = bCombatTeams[ubSide];
		UINT8 ubMembers[MAX_NUM_SOLDIERS];
		BOOLEAN fAssigned[MAX_NUM_SOLDIERS] = { FALSE };
		UINT16 usCount = 0;

		for (UINT16 iCounter = gTacticalStatus.Team[bTeam].bFirstID;
			iCounter <= gTacticalStatus.Team[bTeam].bLastID && usCount < MAX_NUM_SOLDIERS; ++iCounter)
		{
			SOLDIERTYPE *pFriend = MercPtrs[iCounter];
			if (AIEnemyFireteamEligible(pFriend)) ubMembers[usCount++] = pFriend->ubID;
		}
		if (usCount == 0) continue;

		UINT16 usGroups = (usCount <= 10) ? 1 : (usCount + AI_FIRETEAM_TARGET - 1) / AI_FIRETEAM_TARGET;
		UINT16 usRemaining = usCount;
		for (UINT16 usGroup = 0; usGroup < usGroups && usRemaining > 0; ++usGroup)
		{
			UINT16 usGroupsLeft = usGroups - usGroup;
			UINT16 usTarget = (usRemaining + usGroupsLeft - 1) / usGroupsLeft;
			if (usGroups == 1) usTarget = usRemaining;
			else usTarget = __min((UINT16)AI_FIRETEAM_MAX_NORMAL, usTarget);

			INT16 sSeedIndex = -1;
			INT32 iBestSeedScore = -100000;
			for (UINT16 i = 0; i < usCount; ++i)
			{
				if (fAssigned[i]) continue;
				SOLDIERTYPE *pCandidate = MercPtrs[ubMembers[i]];
				if (!pCandidate) continue;

				// Build each element around the best remaining leader. Rank has a strong
				// preference, while experience breaks ties. Fixed sentries are slightly
				// disfavoured as mobile-team seeds so they keep their map mission.
				INT32 iSeedScore = (INT32)AICommandAuthority(pCandidate) * 100 +
					(INT32)pCandidate->stats.bExpLevel * 4;
				if (AIEnemyFixedMissionRole(pCandidate)) iSeedScore -= 12;
				if (iSeedScore > iBestSeedScore)
				{
					iBestSeedScore = iSeedScore;
					sSeedIndex = (INT16)i;
				}
			}
			if (sSeedIndex < 0) break;

			UINT8 ubFireteam = gubAINextFireteam++;
			gbAIFireteamTeam[ubFireteam] = bTeam;
			UINT8 ubSeedId = ubMembers[sSeedIndex];
			SOLDIERTYPE *pSeed = MercPtrs[ubSeedId];
			BOOLEAN fSeedFixedMission = AIEnemyFixedMissionRole(pSeed);
			fAssigned[sSeedIndex] = TRUE;
			gubAIFireteam[ubSeedId] = ubFireteam;
			guiAIFireteamIdentity[ubSeedId] = pSeed->uiUniqueSoldierIdValue;
			--usRemaining;

			for (UINT16 usAdded = 1; usAdded < usTarget && usRemaining > 0; ++usAdded)
			{
				INT16 sBestIndex = -1;
				INT32 iBestDistance = 10000;
				BOOLEAN fHasLeader = FALSE, fHasMedic = FALSE, fHasMachinegunner = FALSE, fHasRadio = FALSE;

				for (UINT16 j = 0; j < usCount; ++j)
				{
					if (!fAssigned[j]) continue;
					UINT8 ubAssignedId = ubMembers[j];
					if (gubAIFireteam[ubAssignedId] != ubFireteam) continue;
					SOLDIERTYPE *pMember = MercPtrs[ubAssignedId];
					if (!pMember) continue;
					fHasLeader = fHasLeader || AICheckIsLeader(pMember);
					fHasMedic = fHasMedic || AICheckIsMedic(pMember);
					fHasMachinegunner = fHasMachinegunner || AICheckIsMachinegunner(pMember);
					fHasRadio = fHasRadio || AICheckIsRadioOperator(pMember);
				}

				for (UINT16 i = 0; i < usCount; ++i)
				{
					if (fAssigned[i]) continue;
					SOLDIERTYPE *pCandidate = MercPtrs[ubMembers[i]];
					if (!pCandidate) continue;
					INT32 iCandidateDistance = 10000;
					for (UINT16 j = 0; j < usCount; ++j)
					{
						if (!fAssigned[j]) continue;
						UINT8 ubAssignedId = ubMembers[j];
						if (gubAIFireteam[ubAssignedId] != ubFireteam) continue;
						SOLDIERTYPE *pMember = MercPtrs[ubAssignedId];
						if (!pMember) continue;
						INT32 iDistance = PythSpacesAway(pMember->sGridNo, pCandidate->sGridNo);
						if (pMember->pathing.bLevel != pCandidate->pathing.bLevel) iDistance += __max(6, DAY_VISION_RANGE / 3);
						iCandidateDistance = __min(iCandidateDistance, iDistance);
					}

					INT32 iRolePenalty = 0;
					if (pSeed && (pSeed->usSoldierFlagMask & SOLDIER_VIP))
					{
						// The General's bodyguards are structural members of the command group,
						// not merely nearby soldiers. Make role affinity dominate spawn distance;
						// cohesion/pathing still governs their actual movement afterwards.
						if (pCandidate->usSoldierFlagMask & SOLDIER_BODYGUARD)
							iCandidateDistance = __min(iCandidateDistance, 4);
						else
							iCandidateDistance += 12;
					}
					else if (pCandidate->usSoldierFlagMask & SOLDIER_BODYGUARD)
					{
						iRolePenalty += 18;
					}
					if (AIEnemyFixedMissionRole(pCandidate) != fSeedFixedMission) iRolePenalty += 8;
					if (fHasLeader && AICheckIsLeader(pCandidate)) iRolePenalty += 4;
					if (fHasMedic && AICheckIsMedic(pCandidate)) iRolePenalty += 4;
					if (fHasMachinegunner && AICheckIsMachinegunner(pCandidate)) iRolePenalty += 4;
					if (fHasRadio && AICheckIsRadioOperator(pCandidate)) iRolePenalty += 4;
					iCandidateDistance += __min(12, iRolePenalty);
					if (iCandidateDistance < iBestDistance) { iBestDistance = iCandidateDistance; sBestIndex = (INT16)i; }
				}
				if (sBestIndex < 0) break;
				UINT8 ubId = ubMembers[sBestIndex];
				fAssigned[sBestIndex] = TRUE;
				gubAIFireteam[ubId] = ubFireteam;
				guiAIFireteamIdentity[ubId] = MercPtrs[ubId]->uiUniqueSoldierIdValue;
				--usRemaining;
			}
		}
	}

	gfAIFireteamsSeeded = TRUE;
	gsAIFireteamKnownMenInSector = gTacticalStatus.Team[ENEMY_TEAM].bMenInSector +
		gTacticalStatus.Team[MILITIA_TEAM].bMenInSector;
}

static INT32 AIFireteamJoinDistance(UINT8 ubFireteam, SOLDIERTYPE *pCandidate)
{
	if (ubFireteam == AI_FIRETEAM_NONE || !pCandidate || gbAIFireteamTeam[ubFireteam] != pCandidate->bTeam) return 10000;
	INT32 iBest = 10000;
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!AIEnemyFireteamEligible(pFriend) || pFriend->ubID >= MAX_NUM_SOLDIERS ||
			guiAIFireteamIdentity[pFriend->ubID] != pFriend->uiUniqueSoldierIdValue ||
			gubAIFireteam[pFriend->ubID] != ubFireteam) continue;
		INT32 iDistance = PythSpacesAway(pFriend->sGridNo, pCandidate->sGridNo);
		if (pFriend->pathing.bLevel != pCandidate->pathing.bLevel) iDistance += __max(6, DAY_VISION_RANGE / 3);
		iBest = __min(iBest, iDistance);
	}
	return iBest;
}

static INT32 AIFireteamRoleOverlapPenalty(UINT8 ubFireteam, SOLDIERTYPE *pCandidate)
{
	if (ubFireteam == AI_FIRETEAM_NONE || !pCandidate || gbAIFireteamTeam[ubFireteam] != pCandidate->bTeam) return 10000;
	BOOLEAN fHasLeader = FALSE, fHasMedic = FALSE, fHasMachinegunner = FALSE, fHasRadio = FALSE;
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pMember = MercPtrs[iCounter];
		if (!AIEnemyFireteamEligible(pMember) || pMember->ubID >= MAX_NUM_SOLDIERS ||
			guiAIFireteamIdentity[pMember->ubID] != pMember->uiUniqueSoldierIdValue ||
			gubAIFireteam[pMember->ubID] != ubFireteam) continue;
		fHasLeader = fHasLeader || AICheckIsLeader(pMember);
		fHasMedic = fHasMedic || AICheckIsMedic(pMember);
		fHasMachinegunner = fHasMachinegunner || AICheckIsMachinegunner(pMember);
		fHasRadio = fHasRadio || AICheckIsRadioOperator(pMember);
	}
	INT32 iPenalty = 0;
	if (fHasLeader && AICheckIsLeader(pCandidate)) iPenalty += 4;
	if (fHasMedic && AICheckIsMedic(pCandidate)) iPenalty += 4;
	if (fHasMachinegunner && AICheckIsMachinegunner(pCandidate)) iPenalty += 4;
	if (fHasRadio && AICheckIsRadioOperator(pCandidate)) iPenalty += 4;
	return __min(12, iPenalty);
}

static INT32 AIFireteamMissionRolePenalty(UINT8 ubFireteam, SOLDIERTYPE *pCandidate)
{
	if (ubFireteam == AI_FIRETEAM_NONE || !pCandidate || gbAIFireteamTeam[ubFireteam] != pCandidate->bTeam) return 10000;
	UINT8 ubFixed = 0, ubMobile = 0;
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pMember = MercPtrs[iCounter];
		if (!AIEnemyFireteamEligible(pMember) || pMember->ubID >= MAX_NUM_SOLDIERS ||
			guiAIFireteamIdentity[pMember->ubID] != pMember->uiUniqueSoldierIdValue ||
			gubAIFireteam[pMember->ubID] != ubFireteam) continue;
		if (AIEnemyFixedMissionRole(pMember)) ++ubFixed; else ++ubMobile;
	}
	if (ubFixed == 0 && ubMobile == 0) return 0;
	BOOLEAN fCandidateFixed = AIEnemyFixedMissionRole(pCandidate);
	if (fCandidateFixed && ubMobile > ubFixed) return 8;
	if (!fCandidateFixed && ubFixed > ubMobile) return 8;
	return 0;
}

static void AIEnsureEnemyFireteams(void)
{
	AISeedEnemyFireteams();
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pSoldier = MercPtrs[iCounter];
		if (!AIEnemyFireteamEligible(pSoldier) || pSoldier->ubID >= MAX_NUM_SOLDIERS) continue;
		if (guiAIFireteamIdentity[pSoldier->ubID] == pSoldier->uiUniqueSoldierIdValue &&
			gubAIFireteam[pSoldier->ubID] != AI_FIRETEAM_NONE) continue;
		UINT8 ubBest = AI_FIRETEAM_NONE;
		INT32 iBest = 10000;
		for (UINT8 ubTeam = 1; ubTeam < gubAINextFireteam; ++ubTeam)
		{
			if (gbAIFireteamTeam[ubTeam] != pSoldier->bTeam) continue;
			if (AIFireteamCountById(ubTeam, FALSE) >= AI_FIRETEAM_MAX_NORMAL) continue;
			INT32 iDistance = AIFireteamJoinDistance(ubTeam, pSoldier);
			BOOLEAN fGeneralTeam = AIFireteamHasSoldierFlag(ubTeam, SOLDIER_VIP);
			if (pSoldier->usSoldierFlagMask & SOLDIER_BODYGUARD)
				iDistance = fGeneralTeam ? __min(iDistance, 4) : iDistance + 24;
			else if (fGeneralTeam)
				iDistance += 8;
			iDistance += AIFireteamRoleOverlapPenalty(ubTeam, pSoldier);
			iDistance += AIFireteamMissionRolePenalty(ubTeam, pSoldier);
			if (iDistance < iBest) { iBest = iDistance; ubBest = ubTeam; }
		}
		if (ubBest == AI_FIRETEAM_NONE) { ubBest = gubAINextFireteam++; gbAIFireteamTeam[ubBest] = pSoldier->bTeam; }
		gubAIFireteam[pSoldier->ubID] = ubBest;
		guiAIFireteamIdentity[pSoldier->ubID] = pSoldier->uiUniqueSoldierIdValue;
	}
	gsAIFireteamKnownMenInSector = gTacticalStatus.Team[ENEMY_TEAM].bMenInSector +
		gTacticalStatus.Team[MILITIA_TEAM].bMenInSector;
}

static BOOLEAN AIFireteamPredominantlyFixed(UINT8 ubFireteam)
{
	UINT8 ubFixed = 0, ubMobile = 0;
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pMember = MercPtrs[iCounter];
		if (!AIEnemyFireteamEligible(pMember) || pMember->ubID >= MAX_NUM_SOLDIERS ||
			guiAIFireteamIdentity[pMember->ubID] != pMember->uiUniqueSoldierIdValue ||
			gubAIFireteam[pMember->ubID] != ubFireteam || pMember->stats.bLife < OKLIFE ||
			pMember->bCollapsed || pMember->bBreathCollapsed) continue;
		if (AIEnemyFixedMissionRole(pMember)) ++ubFixed; else ++ubMobile;
	}
	return ubFixed > ubMobile;
}

static INT32 AIFireteamMergeDistance(UINT8 ubFirst, UINT8 ubSecond)
{
	if (ubFirst == AI_FIRETEAM_NONE || ubSecond == AI_FIRETEAM_NONE ||
		gbAIFireteamTeam[ubFirst] != gbAIFireteamTeam[ubSecond])
	{
		return 10000;
	}

	// AIAbsorbFireteamRemnant() reassigns every regroupable member of ubFirst, not
	// only the soldier whose turn triggered the merge. Score the destination by the
	// worst remnant member's nearest viable destination fighter so a scattered pair
	// cannot merge merely because one survivor happens to be close.
	INT32 iWorstNearest = 0;
	UINT8 ubFirstMembers = 0;
	for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
	{
		SOLDIERTYPE *pFirst = MercPtrs[i];
		if (!AIFireteamRegroupableMember(pFirst) ||
			guiAIFireteamIdentity[pFirst->ubID] != pFirst->uiUniqueSoldierIdValue ||
			gubAIFireteam[pFirst->ubID] != ubFirst)
		{
			continue;
		}

		INT32 iNearest = 10000;
		for (UINT16 j = 0; j < MAX_NUM_SOLDIERS; ++j)
		{
			SOLDIERTYPE *pSecond = MercPtrs[j];
			if (!AIEnemyFireteamEligible(pSecond) || pSecond->ubID >= MAX_NUM_SOLDIERS ||
				guiAIFireteamIdentity[pSecond->ubID] != pSecond->uiUniqueSoldierIdValue ||
				gubAIFireteam[pSecond->ubID] != ubSecond || pSecond->stats.bLife < OKLIFE ||
				pSecond->bCollapsed || pSecond->bBreathCollapsed ||
				(pSecond->usSoldierFlagMask & SOLDIER_POW) ||
				(pSecond->flags.uiStatusFlags & SOLDIER_COWERING) ||
				AIDisengagementActive(pSecond) || AIEscapeActive(pSecond))
			{
				continue;
			}

			INT32 iDistance = PythSpacesAway(pFirst->sGridNo, pSecond->sGridNo);
			if (pFirst->pathing.bLevel != pSecond->pathing.bLevel)
				iDistance += __max(6, DAY_VISION_RANGE / 3);
			iNearest = __min(iNearest, iDistance);
		}

		if (iNearest >= 10000)
			return 10000;

		iWorstNearest = __max(iWorstNearest, iNearest);
		++ubFirstMembers;
	}

	return ubFirstMembers > 0 ? iWorstNearest : 10000;
}

static INT32 AIFireteamRemnantDestinationPenalty(UINT8 ubFireteam)
{
	UINT8 ubOperational = 0;
	UINT8 ubUnderFire = 0;
	UINT8 ubShaken = 0;
	BOOLEAN fStableLeader = FALSE;

	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pMember = MercPtrs[iCounter];
		if (!AIFireteamRegroupableMember(pMember) ||
			guiAIFireteamIdentity[pMember->ubID] != pMember->uiUniqueSoldierIdValue ||
			gubAIFireteam[pMember->ubID] != ubFireteam)
		{
			continue;
		}

		BOOLEAN fBreaking =
			(pMember->flags.uiStatusFlags & SOLDIER_COWERING) ||
			AIDisengagementActive(pMember) || AIEscapeActive(pMember);

		if (fBreaking)
			++ubShaken;
		else
			++ubOperational;

		if (pMember->aiData.bUnderFire)
			++ubUnderFire;

		if (!fBreaking && !pMember->aiData.bUnderFire &&
			AICheckIsLeader(pMember))
		{
			fStableLeader = TRUE;
		}
	}

	INT32 iPenalty = 0;
	// Prefer elements that still have several usable rifles and intact leadership.
	iPenalty -= __min((INT32)10, (INT32)ubOperational * 2);
	if (fStableLeader)
		iPenalty -= 8;

	// Avoid attaching a remnant to another element that is itself collapsing unless
	// no healthier destination exists. This is a preference, not a hard prohibition.
	iPenalty += (INT32)ubUnderFire * 2;
	iPenalty += (INT32)ubShaken * 4;
	return iPenalty;
}
static UINT8 AISelectFireteamRemnantDestination(SOLDIERTYPE *pSoldier, UINT8 *pubOld)
{
	if (pubOld)
		*pubOld = AI_FIRETEAM_NONE;
	if (!AIEnemyFireteamEligible(pSoldier))
		return AI_FIRETEAM_NONE;

	UINT8 ubOld = AIFireteamId(pSoldier);
	UINT8 ubRegroupable = AIFireteamRegroupingStrength(pSoldier);
	if (ubOld == AI_FIRETEAM_NONE || ubRegroupable == 0 || ubRegroupable > 2)
		return AI_FIRETEAM_NONE;

	UINT8 ubBest = AI_FIRETEAM_NONE;
	INT32 iBest = 10000;
	BOOLEAN fOldFixed = AIFireteamPredominantlyFixed(ubOld);

	for (UINT8 ubTeam = 1; ubTeam < gubAINextFireteam; ++ubTeam)
	{
		if (gbAIFireteamTeam[ubTeam] != pSoldier->bTeam || ubTeam == ubOld)
			continue;

		UINT8 ubTargetOperational = AIFireteamOperationalCountById(ubTeam);
		UINT8 ubTargetRegroupable = AIFireteamRegroupableCountById(ubTeam);
		if (ubTargetOperational == 0 || ubTargetRegroupable == 0)
			continue;

		// Capacity is based on all conscious/movable members, not just the ones who
		// happen to be calm and fighting this exact turn. This prevents a suppressed
		// six-man element from looking like a one-man team and producing a huge merge.
		if (ubTargetRegroupable + ubRegroupable < 3 ||
			ubTargetRegroupable + ubRegroupable > AI_FIRETEAM_MAX_MERGED)
		{
			continue;
		}

		INT32 iDistance = AIFireteamMergeDistance(ubOld, ubTeam);
		if (iDistance >= 10000 || iDistance > TACTICAL_RANGE)
			continue;

		BOOLEAN fTargetFixed = AIFireteamPredominantlyFixed(ubTeam);

		INT32 iScore = iDistance + AIFireteamRemnantDestinationPenalty(ubTeam);
		// Mission-compatible elements remain strongly preferred, but a shattered
		// fixed sentry/sniper pair may attach to a viable mobile element rather than
		// abandoning the sector simply because no other fixed element survived.
		if (fTargetFixed != fOldFixed)
			iScore += fOldFixed ? 18 : 8;

		// Among local eligible elements, a nearby panicking element should not beat a
		// slightly farther cohesive team with usable fighters and intact leadership.
		if (iScore < iBest)
		{
			iBest = iScore;
			ubBest = ubTeam;
		}
	}

	if (ubBest != AI_FIRETEAM_NONE && pubOld)
		*pubOld = ubOld;
	return ubBest;
}

static BOOLEAN AIFireteamRemnantDestinationReachable(UINT8 ubOld, UINT8 ubBest);

static BOOLEAN AICanAbsorbFireteamRemnant(SOLDIERTYPE *pSoldier)
{
	UINT8 ubOld = AI_FIRETEAM_NONE;
	UINT8 ubBest = AISelectFireteamRemnantDestination(pSoldier, &ubOld);
	return ubBest != AI_FIRETEAM_NONE &&
		ubOld != AI_FIRETEAM_NONE &&
		AIFireteamRemnantDestinationReachable(ubOld, ubBest);
}

static BOOLEAN AIFireteamRemnantDestinationReachable(UINT8 ubOld, UINT8 ubBest)
{
	if (ubOld == AI_FIRETEAM_NONE || ubBest == AI_FIRETEAM_NONE)
		return FALSE;

	// This check is intentionally deferred until an actual merge attempt. Fireteam
	// scoring is queried frequently, while full pathfinding is expensive. Every
	// regroupable remnant member must be able to reach at least one operational
	// fighter in the destination element before membership/escape state is changed.
	for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
	{
		SOLDIERTYPE *pFirst = MercPtrs[i];
		if (!AIFireteamRegroupableMember(pFirst) ||
			guiAIFireteamIdentity[pFirst->ubID] != pFirst->uiUniqueSoldierIdValue ||
			gubAIFireteam[pFirst->ubID] != ubOld)
		{
			continue;
		}

		BOOLEAN fReachable = FALSE;
		for (UINT16 j = 0; j < MAX_NUM_SOLDIERS && !fReachable; ++j)
		{
			SOLDIERTYPE *pSecond = MercPtrs[j];
			if (!AIEnemyFireteamEligible(pSecond) || pSecond->ubID >= MAX_NUM_SOLDIERS ||
				guiAIFireteamIdentity[pSecond->ubID] != pSecond->uiUniqueSoldierIdValue ||
				gubAIFireteam[pSecond->ubID] != ubBest || pSecond->stats.bLife < OKLIFE ||
				pSecond->bCollapsed || pSecond->bBreathCollapsed ||
				(pSecond->usSoldierFlagMask & SOLDIER_POW) ||
				(pSecond->flags.uiStatusFlags & SOLDIER_COWERING) ||
				AIDisengagementActive(pSecond) || AIEscapeActive(pSecond))
			{
				continue;
			}

			if (pFirst->pathing.bLevel == pSecond->pathing.bLevel &&
				PythSpacesAway(pFirst->sGridNo, pSecond->sGridNo) <= 1)
			{
				fReachable = TRUE;
				break;
			}

			BOOLEAN fClimbingNecessary = FALSE;
			INT32 sClimbGridNo = NOWHERE;
			INT16 sPathCost = EstimatePathCostToLocation(
				pFirst, pSecond->sGridNo, pSecond->pathing.bLevel, TRUE,
				&fClimbingNecessary, &sClimbGridNo);
			if (sPathCost > 0)
				fReachable = TRUE;
		}

		if (!fReachable)
			return FALSE;
	}

	return TRUE;
}
static BOOLEAN AIAbsorbFireteamRemnant(SOLDIERTYPE *pSoldier)
{
	UINT8 ubOld = AI_FIRETEAM_NONE;
	UINT8 ubBest = AISelectFireteamRemnantDestination(pSoldier, &ubOld);
	if (ubBest == AI_FIRETEAM_NONE || ubOld == AI_FIRETEAM_NONE)
		return FALSE;
	if (!AIFireteamRemnantDestinationReachable(ubOld, ubBest))
		return FALSE;

	BOOLEAN fOldFixed = AIFireteamPredominantlyFixed(ubOld);
	BOOLEAN fTargetFixed = AIFireteamPredominantlyFixed(ubBest);
	UINT32 uiRejoinUntil = guiTurnCnt + 3;
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (AIFireteamRegroupableMember(pFriend) &&
			guiAIFireteamIdentity[pFriend->ubID] == pFriend->uiUniqueSoldierIdValue &&
			gubAIFireteam[pFriend->ubID] == ubOld)
		{
			gubAIFireteam[pFriend->ubID] = ubBest;
			guiAIFireteamRejoinUntilTurn[pFriend->ubID] = uiRejoinUntil;

			// If the only viable receiving element is mobile, a shattered fixed remnant
			// explicitly abandons its old post and becomes an on-call member of the new
			// element. This avoids bookkeeping that says "merged" while the soldier
			// remains rooted to the destroyed position.
			if (pFriend->bTeam == ENEMY_TEAM && fOldFixed && !fTargetFixed &&
				(pFriend->aiData.bOrders == STATIONARY || pFriend->aiData.bOrders == SNIPER))
			{
				pFriend->aiData.bOrders = ONCALL;
			}

			// Reattachment supersedes an old break-contact/sector-flight decision.
			AIClearDisengagementState(pFriend);
			AIClearEscapeState(pFriend);
		}
	}
	return TRUE;
}
static BOOLEAN AIRecentlyReattachedFireteamRemnant(SOLDIERTYPE *pSoldier)
{
	if (!AIEnemyFireteamEligible(pSoldier) || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return FALSE;

	if (guiAIFireteamIdentity[pSoldier->ubID] != pSoldier->uiUniqueSoldierIdValue ||
		guiAIFireteamRejoinUntilTurn[pSoldier->ubID] < guiTurnCnt + 1)
	{
		return FALSE;
	}

	// The sticky period is meaningful only while the merged element still contains
	// another operational fighter. If that destination fireteam collapses immediately,
	// release the survivor back to normal disengagement/escape logic rather than
	// trapping him in a three-turn rejoin state with nobody left to join.
	return AIFireteamOperationalCountById(AIFireteamId(pSoldier)) >= 2;
}

UINT8 AIFireteamId(SOLDIERTYPE *pSoldier)
{
	if (!AIEnemyFireteamEligible(pSoldier) || pSoldier->ubID >= MAX_NUM_SOLDIERS) return AI_FIRETEAM_NONE;
	AIResetFireteamsForSector();
	INT16 sKnownCombatMen = gTacticalStatus.Team[ENEMY_TEAM].bMenInSector +
		gTacticalStatus.Team[MILITIA_TEAM].bMenInSector;
	if (guiAIFireteamIdentity[pSoldier->ubID] == pSoldier->uiUniqueSoldierIdValue &&
		gubAIFireteam[pSoldier->ubID] != AI_FIRETEAM_NONE &&
		gsAIFireteamKnownMenInSector == sKnownCombatMen) return gubAIFireteam[pSoldier->ubID];
	AIEnsureEnemyFireteams();
	return gubAIFireteam[pSoldier->ubID];
}

UINT8 AIFireteamAliveCount(SOLDIERTYPE *pSoldier)
{
	return AIFireteamCountById(AIFireteamId(pSoldier), TRUE);
}

UINT8 AIFireteamCombatReadyCount(SOLDIERTYPE *pSoldier)
{
	if (!AIEnemyFireteamEligible(pSoldier)) return 0;
	if (AISmallUnitTeamMode(pSoldier))
		return AICombatTeamOperationalCount(pSoldier);
	UINT8 ubFireteam = AIFireteamId(pSoldier);
	if (ubFireteam == AI_FIRETEAM_NONE) return 0;
	UINT8 ubCount = 0;
	for (UINT16 iCounter = 0; iCounter < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!AIEnemyFireteamEligible(pFriend) || pFriend->ubID >= MAX_NUM_SOLDIERS ||
			guiAIFireteamIdentity[pFriend->ubID] != pFriend->uiUniqueSoldierIdValue ||
			gubAIFireteam[pFriend->ubID] != ubFireteam || pFriend->stats.bLife < OKLIFE ||
			pFriend->bCollapsed || pFriend->bBreathCollapsed || (pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) || AIDisengagementActive(pFriend) || AIEscapeActive(pFriend)) continue;
		++ubCount;
	}
	return ubCount;
}

BOOLEAN AISameFireteam(SOLDIERTYPE *pSoldier, SOLDIERTYPE *pFriend)
{
	if (!pSoldier || !pFriend || pSoldier->bTeam != pFriend->bTeam) return FALSE;
	if (!AICombatTeam(pSoldier)) return TRUE;

	// Two to five remaining fighters stop acting like unrelated mini-squads.
	// This affects support/cohesion only; it does not donate hidden enemy knowledge.
	if (AISmallUnitTeamMode(pSoldier) && pFriend->bActive && pFriend->bInSector &&
		pFriend->stats.bLife > 0 && !(pFriend->usSoldierFlagMask & SOLDIER_POW))
	{
		return TRUE;
	}

	UINT8 ubMine = AIFireteamId(pSoldier);
	return ubMine != AI_FIRETEAM_NONE && ubMine == AIFireteamId(pFriend);
}

BOOLEAN AISharedFireteamContact(SOLDIERTYPE *pSoldier, INT32 *psGridNo,
	INT8 *pbLevel, UINT8 *pubConfidence)
{
	if (psGridNo) *psGridNo = NOWHERE;
	if (pbLevel) *pbLevel = 0;
	if (pubConfidence) *pubConfidence = 0;

	if (!pSoldier || !AICombatTeam(pSoldier) ||
		!pSoldier->bActive || !pSoldier->bInSector ||
		pSoldier->ubID >= MAX_NUM_SOLDIERS)
	{
		return FALSE;
	}

	const INT32 iCommRadius = __max(8, DAY_VISION_RANGE);
	const UINT8 ubMaxRelayHops = 2;
	UINT8 ubCommHops[MAX_NUM_SOLDIERS];
	for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
		ubCommHops[i] = 255;
	ubCommHops[pSoldier->ubID] = 0;

	// Build a small connected communication graph. Information can relay through at
	// most two healthy fireteam members, so a cohesive local element shares a picture
	// while separated elements never become a sector-wide telepathic network.
	for (UINT8 ubHop = 0; ubHop < ubMaxRelayHops; ++ubHop)
	{
		for (UINT8 ubCandidateID = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
			ubCandidateID <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++ubCandidateID)
		{
			SOLDIERTYPE *pCandidate = MercPtrs[ubCandidateID];
			if (!pCandidate || pCandidate->ubID >= MAX_NUM_SOLDIERS ||
				ubCommHops[pCandidate->ubID] != 255 ||
				!pCandidate->bActive || !pCandidate->bInSector ||
				pCandidate->stats.bLife < OKLIFE || pCandidate->bCollapsed ||
				pCandidate->bBreathCollapsed ||
				(pCandidate->usSoldierFlagMask & SOLDIER_POW) ||
				(pCandidate->flags.uiStatusFlags & SOLDIER_COWERING) ||
				AIDisengagementActive(pCandidate) || AIEscapeActive(pCandidate) ||
				!AISameFireteam(pSoldier, pCandidate))
			{
				continue;
			}

			for (UINT8 ubRelayID = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
				ubRelayID <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++ubRelayID)
			{
				SOLDIERTYPE *pRelay = MercPtrs[ubRelayID];
				if (!pRelay || pRelay->ubID >= MAX_NUM_SOLDIERS ||
					ubCommHops[pRelay->ubID] != ubHop ||
					!pRelay->bActive || !pRelay->bInSector ||
					pRelay->stats.bLife < OKLIFE || pRelay->bCollapsed ||
					pRelay->bBreathCollapsed ||
					(pRelay->usSoldierFlagMask & SOLDIER_POW) ||
					(pRelay->flags.uiStatusFlags & SOLDIER_COWERING) ||
					!AISameFireteam(pSoldier, pRelay))
				{
					continue;
				}

				if (PythSpacesAway(pRelay->sGridNo, pCandidate->sGridNo) <= iCommRadius)
				{
					ubCommHops[pCandidate->ubID] = ubHop + 1;
					break;
				}
			}
		}
	}

	INT32 sBestGrid = NOWHERE;
	INT8 bBestLevel = 0;
	UINT8 ubBestConfidence = 0;
	INT32 iBestScore = -1000000;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend->ubID >= MAX_NUM_SOLDIERS ||
			ubCommHops[pFriend->ubID] == 255 ||
			ubCommHops[pFriend->ubID] > ubMaxRelayHops ||
			!pFriend->bActive || !pFriend->bInSector ||
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed ||
			pFriend->bBreathCollapsed ||
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING))
		{
			continue;
		}

		for (UINT16 uiOpponent = 0; uiOpponent < TOTAL_SOLDIERS; ++uiOpponent)
		{
			SOLDIERTYPE *pOpponent = MercPtrs[uiOpponent];
			if (!pOpponent)
				continue;

			INT8 bKnowledge = PersonalKnowledge(pFriend, (UINT8)uiOpponent);
			INT32 iConfidence = 0;
			// Once contact is stale/heard, never inspect the target's current active,
			// in-sector, neutral, side, health or action state. Those are hidden facts.
			// For a genuinely current sighting the normal current relation is legal.
			if (bKnowledge == SEEN_CURRENTLY)
			{
				if (!pOpponent->bActive || !pOpponent->bInSector ||
					CONSIDERED_NEUTRAL(pFriend, pOpponent) ||
					pFriend->bSide == pOpponent->bSide)
				{
					continue;
				}
				iConfidence = 100;
			}
			else if (bKnowledge == SEEN_THIS_TURN)
				iConfidence = 90;
			else if (bKnowledge == SEEN_LAST_TURN)
				iConfidence = 70;
			else if (bKnowledge == SEEN_2_TURNS_AGO)
				iConfidence = 50;
			else if (bKnowledge == HEARD_THIS_TURN)
				iConfidence = 55;
			else if (bKnowledge == HEARD_LAST_TURN)
				iConfidence = 38;
			else if (bKnowledge == HEARD_2_TURNS_AGO)
				iConfidence = 22;
			else
				continue;

			INT32 sKnownGrid = KnownPersonalLocation(pFriend, (UINT8)uiOpponent);
			if (TileIsOutOfBounds(sKnownGrid))
				continue;

			INT8 bKnownLevel = KnownPersonalLevel(pFriend, (UINT8)uiOpponent);
			iConfidence -= 8 * (INT32)ubCommHops[pFriend->ubID];
			iConfidence -= __min((INT32)12,
				PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) / 2);
			if (bKnownLevel != pSoldier->pathing.bLevel)
				iConfidence -= 8;
			iConfidence = __max(1, __min(100, iConfidence));

			INT32 iScore = iConfidence * 4 -
				4 * (INT32)ubCommHops[pFriend->ubID];
			if (iScore > iBestScore)
			{
				iBestScore = iScore;
				sBestGrid = sKnownGrid;
				bBestLevel = bKnownLevel;
				ubBestConfidence = (UINT8)iConfidence;
			}
		}
	}

	if (TileIsOutOfBounds(sBestGrid))
		return FALSE;

	if (psGridNo) *psGridNo = sBestGrid;
	if (pbLevel) *pbLevel = bBestLevel;
	if (pubConfidence) *pubConfidence = ubBestConfidence;
	return TRUE;
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

		INT8 bCandidateKnowledge = Knowledge(pSoldier, pCandidate->ubID);
		if (bCandidateKnowledge != SEEN_CURRENTLY &&
			bCandidateKnowledge != SEEN_THIS_TURN &&
			bCandidateKnowledge != SEEN_LAST_TURN &&
			bCandidateKnowledge != SEEN_2_TURNS_AGO &&
			bCandidateKnowledge != HEARD_THIS_TURN &&
			bCandidateKnowledge != HEARD_LAST_TURN &&
			bCandidateKnowledge != HEARD_2_TURNS_AGO)
		{
			continue;
		}

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

		INT32 sCandidateSpot = KnownLocation(pSoldier, pCandidate->ubID);
		if (TileIsOutOfBounds(sCandidateSpot))
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

			// Artillery is ordered against an area, not an instantaneous bullet path.
			// Protect a friendly's already-committed movement destination as well as his
			// current tile so support is not called onto an advancing/withdrawing element.
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

			INT8 bKnowledge = Knowledge(pSoldier, pOpponent->ubID);
			if (bKnowledge != SEEN_CURRENTLY &&
				bKnowledge != SEEN_THIS_TURN &&
				bKnowledge != SEEN_LAST_TURN &&
				bKnowledge != SEEN_2_TURNS_AGO &&
				bKnowledge != HEARD_THIS_TURN &&
				bKnowledge != HEARD_LAST_TURN &&
				bKnowledge != HEARD_2_TURNS_AGO)
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

			INT32 sKnownSpot = KnownLocation(pSoldier, pOpponent->ubID);
			if (TileIsOutOfBounds(sKnownSpot) ||
				PythSpacesAway(sKnownSpot, sCandidateSpot) > iStrikeRadius)
			{
				continue;
			}

			INT32 iCertainty = ThreatPercent[bKnowledge - OLDEST_HEARD_VALUE];
			iScore += iCertainty;
			if (iCertainty >= 50)
				++ubCredibleContacts;
		}

		// Artillery is a scarce area weapon: require at least two credible reported
		// contacts, not one speculative/stale enemy location.
		if (ubCredibleContacts < 2)
			continue;

		// Dense local terrain reduces expected effect, consistent with RedSmokeDanger().
		iScore -= TerrainDensity(sCandidateSpot, 0, 2, FALSE);

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

		INT8 bKnowledge = Knowledge(pSoldier, pOpponent->ubID);
		if (bKnowledge == NOT_HEARD_OR_SEEN)
			continue;

		const BOOLEAN fDirectVisualContact =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;
		// Current relation state is hidden after contact is lost. Remembered hostility
		// persists until direct observation establishes a change.
		if (fDirectVisualContact &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW))
		{
			continue;
		}

		INT32 sKnownSpot = KnownLocation(pSoldier, pOpponent->ubID);
		if (TileIsOutOfBounds(sKnownSpot) ||
			PythSpacesAway(pSoldier->sGridNo, sKnownSpot) > TACTICAL_RANGE)
		{
			continue;
		}

		// ThreatPercent already encodes JA2's confidence in seen/heard information:
		// current sight is strongest; stale contacts count progressively less.
		UINT32 uiContactStrength = ThreatPercent[bKnowledge - OLDEST_HEARD_VALUE];

		// A personally observed incapacitated human is still a residual threat because
		// he may recover or be revived, but he should not count like an active rifleman.
		// Public/stale contacts keep their normal uncertainty weight.
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
	pContext->sPrimaryThreat = ClosestKnownOpponent(pSoldier, NULL, NULL);
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

		INT8 bKnowledge = Knowledge(pSoldier, pOpponent->ubID);
		if (bKnowledge == NOT_HEARD_OR_SEEN)
			continue;

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

		INT32 sKnownSpot = KnownLocation(pSoldier, pOpponent->ubID);
		if (TileIsOutOfBounds(sKnownSpot))
			continue;

		INT8 bKnownLevel = KnownLevel(pSoldier, pOpponent->ubID);
		INT32 iCertainty = ThreatPercent[bKnowledge - OLDEST_HEARD_VALUE];

		// Stale/heard contacts still influence caution, but only if their last-known
		// line could plausibly cover the position within the engine's vision scale.
		if (PythSpacesAway(sKnownSpot, sSpot) <= MAX_VISION_RANGE &&
			LocationToLocationLineOfSightTest(sKnownSpot, bKnownLevel, sSpot, bLevel, TRUE, MAX_VISION_RANGE))
		{
			uiExposure += iCertainty;
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
		sTargetSpot = ClosestKnownOpponent(pSoldier, NULL, NULL);
	if (TileIsOutOfBounds(sTargetSpot) && pSoldier->bTeam == ENEMY_TEAM)
		AISharedFireteamContact(pSoldier, &sTargetSpot, NULL, NULL);

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
		sTargetSpot = ClosestKnownOpponent(pSoldier, NULL, NULL);
	if (TileIsOutOfBounds(sTargetSpot) && pSoldier->bTeam == ENEMY_TEAM)
		AISharedFireteamContact(pSoldier, &sTargetSpot, NULL, NULL);

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

		INT32 sFriendThreat = ClosestKnownOpponent(pFriend, NULL, NULL);
		if (TileIsOutOfBounds(sFriendThreat) && pFriend->bTeam == ENEMY_TEAM)
			AISharedFireteamContact(pFriend, &sFriendThreat, NULL, NULL);
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
		sTargetSpot = ClosestKnownOpponent(pSoldier, NULL, NULL);

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

			INT32 sCandidateThreat = ClosestKnownOpponent(pCandidate, NULL, NULL);
			if (TileIsOutOfBounds(sCandidateThreat) && pCandidate->bTeam == ENEMY_TEAM)
				AISharedFireteamContact(pCandidate, &sCandidateThreat, NULL, NULL);
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

			INT32 sFriendThreat = ClosestKnownOpponent(pFriend, NULL, NULL);
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
		INT32 sFriendThreat = ClosestKnownOpponent(pFriend, NULL, NULL);
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
		sTargetSpot = ClosestKnownOpponent(pSoldier, NULL, NULL);

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
		sTargetSpot = ClosestKnownOpponent(pSoldier, NULL, NULL);
	if (TileIsOutOfBounds(sTargetSpot) && pSoldier->bTeam == ENEMY_TEAM)
		AISharedFireteamContact(pSoldier, &sTargetSpot, NULL, NULL);
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
		sTargetSpot = ClosestKnownOpponent(pSoldier, NULL, NULL);
	if (TileIsOutOfBounds(sTargetSpot) && pSoldier->bTeam == ENEMY_TEAM)
		AISharedFireteamContact(pSoldier, &sTargetSpot, NULL, NULL);
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

	INT32 sThreat = ClosestKnownOpponent(pCandidate, NULL, NULL);
	if (TileIsOutOfBounds(sThreat) && pCandidate->bTeam == ENEMY_TEAM)
		AISharedFireteamContact(pCandidate, &sThreat, NULL, NULL);
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
		sSpot = ClosestKnownOpponent(pSoldier, NULL, NULL);
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

		INT8 bKnowledge = Knowledge(pSoldier, pOpponent->ubID);
		if (bKnowledge == NOT_HEARD_OR_SEEN)
			continue;

		const BOOLEAN fThreatStateKnown =
			(PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY) &&
			(LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0);
		if (fThreatStateKnown &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW))
		{
			continue;
		}

		if (fThreatStateKnown && !ValidOpponent(pSoldier, pOpponent))
			continue;

		INT32 sThreatLoc = KnownLocation(pSoldier, pOpponent->ubID);
		INT8 bThreatLevel = KnownLevel(pSoldier, pOpponent->ubID);
		if (TileIsOutOfBounds(sThreatLoc))
			continue;

		UINT16 usAdjustedSight;
		if (fThreatStateKnown)
		{
			// Personal sight can legitimately use the observer's actual vision state.
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
			// Stale/heard contacts have a believed firing/observation sector, not access
			// to hidden current optics, stance, breath, wounds or weapon-ready state.
			INT32 iCertainty = ThreatPercent[bKnowledge - OLDEST_HEARD_VALUE];
			usAdjustedSight = (UINT16)max(1, (MAX_VISION_RANGE * iCertainty) / 100);
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

		// Predict a one-tile reposition only for a currently observed opponent. Doing
		// this for stale contacts lets hidden current movement/body state leak into cover.
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

		INT8 bKnowledge = Knowledge(pSoldier, pOpponent->ubID);
		if (bKnowledge == NOT_HEARD_OR_SEEN)
			continue;

		const BOOLEAN fThreatStateKnown =
			(PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY) &&
			(LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0);
		if (fThreatStateKnown &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW))
		{
			continue;
		}

		if (fThreatStateKnown &&
			(!ValidOpponent(pSoldier, pOpponent) || pOpponent->IsUnconscious() || pOpponent->IsEmptyVehicle()))
		{
			continue;
		}

		INT32 sThreatLoc = KnownLocation(pSoldier, pOpponent->ubID);
		INT8 bThreatLevel = KnownLevel(pSoldier, pOpponent->ubID);
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
			INT32 iCertainty = ThreatPercent[bKnowledge - OLDEST_HEARD_VALUE];
			usAdjustedSight = (UINT16)max(1, (MAX_VISION_RANGE * iCertainty) / 100);
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

		INT8 bKnowledge = Knowledge(pSoldier, pOpponent->ubID);
		if (bKnowledge == NOT_HEARD_OR_SEEN)
			continue;

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

		// Only a current contact may disappear because of its live engine state.
		if (fDirectVisualContact && !ValidOpponent(pSoldier, pOpponent))
			continue;

		if (!TileIsOutOfBounds(KnownLocation(pSoldier, pOpponent->ubID)))
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

	INT32 sClosestOpponent = ClosestKnownOpponent(pSoldier, NULL, NULL);
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

	// Sector strength must reflect what this soldier/team can actually know.
	// Friendly condition is legitimate team information; enemy condition is not.
	for (UINT32 uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercSlots[uiLoop];
		if (!pOpponent)
			continue;

		if (pOpponent->bTeam == pSoldier->bTeam || pOpponent->bSide == pSoldier->bSide)
		{
			if (pOpponent->bActive && pOpponent->bInSector && pOpponent->stats.bLife >= OKLIFE &&
				!(pOpponent->usSoldierFlagMask & SOLDIER_POW))
			{
				++ubNumFriends;
			}
			continue;
		}

		INT8 bKnowledge = Knowledge(pSoldier, pOpponent->ubID);
		if (bKnowledge == NOT_HEARD_OR_SEEN)
			continue;

		const BOOLEAN fDirectVisualContact =
			(PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY) &&
			(LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0);
		if (fDirectVisualContact &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW))
		{
			continue;
		}

		// Only direct current observation can remove a known contact because of live
		// casualty/capture/sector state. Stale contacts remain possible threats until
		// knowledge itself expires.
		if (fDirectVisualContact &&
			(!ValidOpponent(pSoldier, pOpponent) ||
			 pOpponent->IsUnconscious() ||
			 (pOpponent->usSoldierFlagMask & SOLDIER_POW)))
		{
			continue;
		}

		if (TileIsOutOfBounds(KnownLocation(pSoldier, pOpponent->ubID)))
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

	INT32 sOpponentSpot;
	INT8 bOpponentLevel;
	INT32 sClosestOpponent = ClosestKnownOpponent(pSoldier, &sOpponentSpot, &bOpponentLevel);
	if (TileIsOutOfBounds(sClosestOpponent))
		return FALSE;

	// There must at least be physical cover from the closest believed threat.
	if (!AnyCoverFromSpot(sSpot, pSoldier->pathing.bLevel, sOpponentSpot, bOpponentLevel))
		return FALSE;

	for (UINT32 uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		SOLDIERTYPE *pOpponent = MercSlots[uiLoop];
		if (!pOpponent)
			continue;

		INT8 bKnowledge = Knowledge(pSoldier, pOpponent->ubID);
		if (bKnowledge == NOT_HEARD_OR_SEEN)
			continue;

		const BOOLEAN fThreatStateKnown =
			(PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY) &&
			(LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0);
		if (fThreatStateKnown &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW))
		{
			continue;
		}
		if (fThreatStateKnown && !ValidOpponent(pSoldier, pOpponent))
			continue;

		INT32 sThreatLoc = KnownLocation(pSoldier, pOpponent->ubID);
		INT8 bThreatLevel = KnownLevel(pSoldier, pOpponent->ubID);
		if (TileIsOutOfBounds(sThreatLoc))
			continue;

		INT32 iVisibilityRange;
		if (fThreatStateKnown)
		{
			iVisibilityRange = pOpponent->GetMaxDistanceVisible(sSpot, pSoldier->pathing.bLevel, CALC_FROM_ALL_DIRS);
		}
		else
		{
			INT32 iCertainty = ThreatPercent[bKnowledge - OLDEST_HEARD_VALUE];
			iVisibilityRange = max(1, (MAX_VISION_RANGE * iCertainty) / 100);
		}

		if (!AnyCoverFromSpot(sSpot, pSoldier->pathing.bLevel, sThreatLoc, bThreatLevel) &&
			PythSpacesAway(sSpot, sThreatLoc) <= iVisibilityRange &&
			LocationToLocationLineOfSightTest(sThreatLoc, bThreatLevel, sSpot, pSoldier->pathing.bLevel,
				TRUE, iVisibilityRange, STANDING_LOS_POS, PRONE_LOS_POS))
		{
			return FALSE;
		}

		// Only an actually observed opponent gets one-tile movement prediction.
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

				if (!AnyCoverFromSpot(sSpot, pSoldier->pathing.bLevel, sTempGridNo, bThreatLevel) &&
					PythSpacesAway(sSpot, sTempGridNo) <= iVisibilityRange &&
					LocationToLocationLineOfSightTest(sTempGridNo, bThreatLevel, sSpot, pSoldier->pathing.bLevel,
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

		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) || pSoldier->bSide == pOpponent->bSide)
			continue;

		INT8 bKnowledge = Knowledge(pSoldier, pOpponent->ubID);
		if (bKnowledge == NOT_HEARD_OR_SEEN)
			continue;

		const BOOLEAN fDirectVisualContact =
			(PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY) &&
			(LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0);

		if (fDirectVisualContact &&
			(!ValidOpponent(pSoldier, pOpponent) ||
			 pOpponent->IsUnconscious() ||
			 (pOpponent->usSoldierFlagMask & SOLDIER_POW)))
		{
			continue;
		}

		INT32 sThreatLoc = KnownLocation(pSoldier, pOpponent->ubID);
		INT8 bThreatLevel = KnownLevel(pSoldier, pOpponent->ubID);
		if (TileIsOutOfBounds(sThreatLoc) ||
			PythSpacesAway(sThreatLoc, sGridNo) > ubDistance ||
			bThreatLevel != blevel)
		{
			continue;
		}

		INT8 bInterruptLevel;
		if (fDirectVisualContact)
		{
			// Current direct observation permits the real combat-state estimate.
			bInterruptLevel = AIEstimateInterruptLevel(pOpponent);
		}
		else
		{
			// An unseen contact must not reveal hidden experience, agility or shock.
			// Use a neutral competent-soldier prior and reduce it as information ages.
			INT32 iCertainty = ThreatPercent[bKnowledge - OLDEST_HEARD_VALUE];
			bInterruptLevel = (INT8)__max(1, (6 * iCertainty + 50) / 100);
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

		// Public enemy counts must be driven by public knowledge, not by the hidden
		// current HP/capture/sector state of an opponent whose contact is stale.
		INT8 bPublicKnowledge = gbPublicOpplist[pSoldier->bTeam][pOpponent->ubID];
		if (bPublicKnowledge == NOT_HEARD_OR_SEEN)
			continue;

		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			pSoldier->bSide == pOpponent->bSide)
		{
			continue;
		}

		// If this soldier personally sees the target right now, live state is known
		// and may invalidate the threat. Team-only knowledge does not grant that.
		if (PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0 &&
			(!ValidOpponent(pSoldier, pOpponent) ||
			 pOpponent->IsUnconscious() ||
			 (pOpponent->usSoldierFlagMask & SOLDIER_POW)))
		{
			continue;
		}

		INT32 sThreatLoc = gsPublicLastKnownOppLoc[pSoldier->bTeam][pOpponent->ubID];
		if (TileIsOutOfBounds(sThreatLoc))
			continue;

		if (PythSpacesAway(sThreatLoc, sGridNo) > ubDistance)
			continue;

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

	UINT32		uiLoop;
	SOLDIERTYPE *pOpponent;

	INT32		sThreatLoc;
	INT8		iThreatLevel;

	UINT8		ubNum = 0;

	// loop through all the enemies
	for (uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		pOpponent = MercSlots[uiLoop];

		if (!pOpponent)
		{
			continue;
		}

		INT8 bKnowledge = Knowledge(pSoldier, pOpponent->ubID);
		if (bKnowledge == NOT_HEARD_OR_SEEN)
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

		if (fDirectVisualContact && !ValidOpponent(pSoldier, pOpponent))
		{
			continue;
		}

		sThreatLoc = KnownLocation(pSoldier, pOpponent->ubID);
		iThreatLevel = KnownLevel(pSoldier, pOpponent->ubID);

		if (TileIsOutOfBounds(sThreatLoc))
		{
			continue;
		}

		if (PythSpacesAway(pSoldier->sGridNo, sThreatLoc) > sDistance)
		{
			continue;
		}

		if (AIDirection(pSoldier->sGridNo, sThreatLoc) != ubDirection &&
			(!fAdjacent || AIDirection(pSoldier->sGridNo, sThreatLoc) != gOneCDirection[ubDirection] && AIDirection(pSoldier->sGridNo, sThreatLoc) != gOneCCDirection[ubDirection]))
		{
			continue;
		}

		ubNum++;
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

	INT32	sOpponentGridNo;
	INT8	bOpponentLevel;
	INT32	sClosestOpponent = ClosestKnownOpponent(pSoldier, &sOpponentGridNo, &bOpponentLevel);

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

	INT32	sOpponentGridNo;
	INT8	bOpponentLevel;
	INT32	sClosestOpponent = ClosestKnownOpponent(pSoldier, &sOpponentGridNo, &bOpponentLevel);

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

		INT8 bKnowledge = Knowledge(pSoldier, pOpponent->ubID);
		if (bKnowledge == NOT_HEARD_OR_SEEN)
			continue;

		const BOOLEAN fThreatStateKnown =
			(PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY) &&
			(LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0);
		if (fThreatStateKnown &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide ||
			 (pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			 pOpponent->ubBodyType == CROW))
		{
			continue;
		}

		INT32 sThreatLoc = KnownLocation(pSoldier, pOpponent->ubID);
		INT8 bThreatLevel = KnownLevel(pSoldier, pOpponent->ubID);
		if (TileIsOutOfBounds(sThreatLoc))
			continue;

		INT32 iAttackRange;
		if (fThreatStateKnown)
		{
			if (!ValidOpponent(pSoldier, pOpponent) || pOpponent->IsUnconscious() || pOpponent->IsEmptyVehicle())
				continue;

			if (!pOpponent->CanInterrupt())
				continue;

			// For an observed opponent we legitimately know whether his current weapon
			// can threaten the tile.
			if (!AICheckHasGun(pOpponent) && PythSpacesAway(sThreatLoc, sSpot) > DAY_VISION_RANGE / 2)
				continue;

			iAttackRange = AICheckHasGun(pOpponent) ? AIGunRange(pOpponent) * 3 / 2 : DAY_VISION_RANGE / 2;
		}
		else
		{
			// For a stale contact, represent uncertainty through the knowledge age.
			// Do not inspect hidden current weapon, AP, shock, stance or consciousness.
			INT32 iCertainty = ThreatPercent[bKnowledge - OLDEST_HEARD_VALUE];
			iAttackRange = max(DAY_VISION_RANGE / 4, (MAX_VISION_RANGE * iCertainty) / 100);
		}

		if (PythSpacesAway(sThreatLoc, sSpot) <= iAttackRange &&
			LocationToLocationLineOfSightTest(sThreatLoc, bThreatLevel, sSpot, bLevel, TRUE, MAX_VISION_RANGE))
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

		INT8 bKnowledge = Knowledge(pSoldier, pOpponent->ubID);
		if (bKnowledge == NOT_HEARD_OR_SEEN)
			continue;

		const BOOLEAN fPersonallySeeingNow =
			PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY &&
			LOS_Raised(pSoldier, pOpponent, CALC_FROM_ALL_DIRS) > 0;
		if (fPersonallySeeingNow &&
			(CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			 pSoldier->bSide == pOpponent->bSide))
		{
			continue;
		}

		INT32 sKnownSpot = KnownLocation(pSoldier, pOpponent->ubID);
		INT8 bKnownLevel = KnownLevel(pSoldier, pOpponent->ubID);
		if (TileIsOutOfBounds(sKnownSpot) || bKnownLevel != bLevel)
			continue;

		if (PythSpacesAway(sKnownSpot, sCandidateSpot) > MAX_VISION_RANGE ||
			!LocationToLocationLineOfSightTest(sKnownSpot, bKnownLevel,
				sCandidateSpot, bLevel, TRUE, MAX_VISION_RANGE))
		{
			continue;
		}

		INT32 iContactRisk = 8 + ThreatPercent[bKnowledge - OLDEST_HEARD_VALUE] / 5;
		if (bKnowledge == SEEN_CURRENTLY || bKnowledge == SEEN_THIS_TURN)
			iContactRisk += 12;
		else if (bKnowledge == SEEN_LAST_TURN)
			iContactRisk += 6;

		if (fPersonallySeeingNow &&
			(pOpponent->aiData.bAction == AI_ACTION_FIRE_GUN ||
			 pOpponent->aiData.bLastAction == AI_ACTION_FIRE_GUN))
		{
			// A visible opponent who has just committed to firing is less likely to
			// interrupt the candidate move immediately. Never infer this from stale/public contact.
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
			INT32 sFriendTarget = ClosestKnownOpponent(pFriend, NULL, NULL);
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
		sTargetSpot = ClosestKnownOpponent(pSoldier, NULL, NULL);

	// If this soldier has no personal/public contact, use the fireteam's recent
	// legally observed contact as a planning objective. This does not authorize fire.
	if (TileIsOutOfBounds(sTargetSpot) && pSoldier->bTeam == ENEMY_TEAM)
		AISharedFireteamContact(pSoldier, &sTargetSpot, NULL, NULL);

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

		INT32 sCandidateTarget = ClosestKnownOpponent(pCandidate, NULL, NULL);
		if (TileIsOutOfBounds(sCandidateTarget))
			AISharedFireteamContact(pCandidate, &sCandidateTarget, NULL, NULL);
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
		sTargetSpot = ClosestKnownOpponent(pSoldier, NULL, NULL);
	if (TileIsOutOfBounds(sTargetSpot) && pSoldier->bTeam == ENEMY_TEAM)
		AISharedFireteamContact(pSoldier, &sTargetSpot, NULL, NULL);

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

INT32 AIUtilityPositionScore(SOLDIERTYPE *pSoldier, INT32 sCandidateSpot,
	INT32 sTargetSpot, INT8 bIntent, INT8 bRole)
{
	if (!AICombatTeam(pSoldier) || TileIsOutOfBounds(sCandidateSpot))
		return -10000;

	if (TileIsOutOfBounds(sTargetSpot))
		sTargetSpot = ClosestKnownOpponent(pSoldier, NULL, NULL);
	if (TileIsOutOfBounds(sTargetSpot) && pSoldier->bTeam == ENEMY_TEAM)
		AISharedFireteamContact(pSoldier, &sTargetSpot, NULL, NULL);
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

	return AIScoreTacticalPosition(
		pSoldier, &Features, sCandidateSpot, sTargetSpot, bIntent, bRole);
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
