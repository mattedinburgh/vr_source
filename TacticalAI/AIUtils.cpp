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

	// sevenfm: admins/green militia go prone only when wounded or under fire
	if( pSoldier->ubSoldierClass == SOLDIER_CLASS_ADMINISTRATOR ||
		pSoldier->ubSoldierClass == SOLDIER_CLASS_GREEN_MILITIA )
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
		if ( pSoldier->bTeam == ENEMY_TEAM && pSoldier->aiData.bAlertStatus == STATUS_RED )
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

	INT8 bSearchRadius = 0;
	switch (bKnowledge)
	{
	case SEEN_CURRENTLY:
	case SEEN_THIS_TURN:
		bSearchRadius = 0;
		break;
	case SEEN_LAST_TURN:
		bSearchRadius = 1;
		break;
	case HEARD_THIS_TURN:
		bSearchRadius = 2;
		break;
	case HEARD_LAST_TURN:
		bSearchRadius = 3;
		break;
	case HEARD_2_TURNS_AGO:
		bSearchRadius = 4;
		break;
	default:
		bSearchRadius = 3;
		break;
	}

	if (bSearchRadius <= 0)
		return sKnownSpot;

	UINT8 ubStartDirection = (UINT8)((pSoldier->ubID + 3 * pOpponent->ubID) % NUM_WORLD_DIRECTIONS);
	INT8 bPreferredDistance = 1 + (INT8)((pSoldier->ubID + pOpponent->ubID) % bSearchRadius);

	for (UINT8 ubTry = 0; ubTry < NUM_WORLD_DIRECTIONS; ubTry++)
	{
		UINT8 ubDirection = (ubStartDirection + ubTry) % NUM_WORLD_DIRECTIONS;
		INT32 sCandidate = sKnownSpot;

		for (INT8 bStep = 0; bStep < bPreferredDistance; bStep++)
		{
			INT32 sNext = NewGridNo(sCandidate, DirectionInc(ubDirection));
			if (sNext == sCandidate || TileIsOutOfBounds(sNext))
				break;
			sCandidate = sNext;
		}

		if (sCandidate != sKnownSpot &&
			!TileIsOutOfBounds(sCandidate) &&
			NewOKDestination(pSoldier, sCandidate, FALSE, bKnownLevel))
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

		BOOLEAN fCurrentContact = (*pbPersOL == SEEN_CURRENTLY || *pbPublOL == SEEN_CURRENTLY);
		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) || pSoldier->bSide == pOpponent->bSide ||
			(pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			(gTacticalStatus.bBoxingState == BOXING && pSoldier->IsBoxer() && !pOpponent->IsBoxer()) ||
			pOpponent->ubBodyType == CROW)
		{
			continue;
		}
		if (fCurrentContact && (!pOpponent->bActive || !pOpponent->bInSector || pOpponent->stats.bLife <= 0 || pOpponent->IsEmptyVehicle()))
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
					if (pSoldier->bTeam == ENEMY_TEAM && pOpponent->bTeam == ENEMY_TEAM)
					{
						fShareClear = AISameFireteam(pSoldier, pOpponent) ||
							PythSpacesAway(pSoldier->sGridNo, pOpponent->sGridNo) <= TACTICAL_RANGE / 3;
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

		BOOLEAN fCurrentContact = (*pbPersOL == SEEN_CURRENTLY || *pbPublOL == SEEN_CURRENTLY);
		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) || pSoldier->bSide == pOpponent->bSide ||
			(pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			(gTacticalStatus.bBoxingState == BOXING && pSoldier->IsBoxer() && !pOpponent->IsBoxer()) ||
			pOpponent->ubBodyType == CROW)
		{
			continue;
		}
		if (fCurrentContact && (!pOpponent->bActive || !pOpponent->bInSector || pOpponent->stats.bLife <= 0 || pOpponent->IsEmptyVehicle()))
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

		// if this opponent is not seen personally
		if (*pbPersOL != SEEN_CURRENTLY)
		{
			continue;			// next merc
		}

		// since we're dealing with seen people, use exact gridnos
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

		// if this opponent is not seen personally
		if (*pbPersOL != SEEN_CURRENTLY)
		{
			continue;			// next merc
		}

		// since we're dealing with seen people, use exact gridnos
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

		// if this PC is not visible to the soldier, then add a penalty to the distance
		// so that we weight in favour of visible mercs
		if ( pTargetSoldier->bTeam != pSoldier->bTeam && pSoldier->aiData.bOppList[ ubLoop ] != SEEN_CURRENTLY )
			continue;

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
			*pfClimbingNecessary = FALSE;
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
		// sector. Healthy fireteams now help their own element first; cross-element
		// help is allowed only locally, while one/two-man remnants remain free to
		// merge through the fireteam cohesion logic.
		if (pSoldier->bTeam == ENEMY_TEAM && pFriend->bTeam == ENEMY_TEAM &&
			!AISameFireteam(pSoldier, pFriend) &&
			AIFireteamAliveCount(pSoldier) > 2 &&
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > __max(6, DAY_VISION_RANGE / 3))
		{
			continue;
		}

		// CJC: restrict "last one to radio" to only if that guy saw us this turn or last turn

		// if this friend is not under fire, and isn't the last one to radio
		// sevenfm: also help if friend has more opponents than friends nearby
		/*if (!(	pFriend->aiData.bUnderFire ||
				(pFriend->ubID == gTacticalStatus.Team[pFriend->bTeam].ubLastMercToRadio && GuySawEnemyThisTurnOrBefore( pFriend ) ) ||
				CountSeenEnemiesLastTurn(pFriend) > CountNearbyFriendlies(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE/4) ) )*/
		// sevenfm: help if friend has more recently seen opponents than friends nearby
		if( !( CountSeenEnemiesLastTurn(pFriend) > CountNearbyFriends(pFriend, pFriend->sGridNo, DAY_VISION_RANGE/4) ) )
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

		BOOLEAN fCurrentContact = (*pbPersOL == SEEN_CURRENTLY || *pbPublOL == SEEN_CURRENTLY);
		if (fCurrentContact && (!pOpponent->bActive || !pOpponent->bInSector || pOpponent->stats.bLife <= 0 || pOpponent->IsEmptyVehicle()))
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
		INT32 iOpponentThreat = fCurrentContact ?
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

			// if this merc is inactive, at base, on assignment, dead, unconscious
			if (!pFriend || (pFriend->stats.bLife < OKLIFE))
				continue;		// next merc

			// if this merc is not on my side, then he's NOT one of my friends

			// WE CAN'T AFFORD TO CONSIDER THE ENEMY OF MY ENEMY MY FRIEND, HERE!
			// ONLY IF WE ARE ACTUALLY OFFICIALLY CO-OPERATING TOGETHER (SAME SIDE)
			if ( pFriend->aiData.bNeutral && !( pSoldier->ubCivilianGroup != NON_CIV_GROUP && pSoldier->ubCivilianGroup == pFriend->ubCivilianGroup ) )
			{
				continue;		// next merc
			}

			if ( pSoldier->bSide != pFriend->bSide )
				continue;		// next merc

			// Morale support is tactical, not sector-wide. Healthy enemy fireteams
			// should draw confidence primarily from their own element and nearby
			// cross-support, rather than from soldiers fighting on the far side of the map.
			if (pSoldier->bTeam == ENEMY_TEAM && pFriend->bTeam == ENEMY_TEAM &&
				!AISameFireteam(pSoldier, pFriend) &&
				AIFireteamAliveCount(pSoldier) > 2 &&
				PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > TACTICAL_RANGE / 2)
			{
				continue;
			}

			// THIS TEST IS INVALID IF A COMPUTER-TEAM IS PLAYING CO-OPERATIVELY
			// WITH A NON-COMPUTER TEAM SINCE THE OPPLISTS INVOLVED ARE NOT
			// UP-TO-DATE.	THIS SITUATION IS CURRENTLY NOT POSSIBLE IN HTH/DG.

			// ALSO NOTE THAT WE COUNT US AS OUR (BEST) FRIEND FOR THESE CALCULATIONS

			// subtract HEARD_2_TURNS_AGO (which is negative) to make values start at 0 and
			// be positive otherwise
			iPercent = ThreatPercent[pFriend->aiData.bOppList[pOpponent->ubID] - OLDEST_HEARD_VALUE];

			// reduce the percentage value based on how far away they are from the enemy, if they only hear him
			if ( pFriend->aiData.bOppList[ pOpponent->ubID ] <= HEARD_LAST_TURN )
			{
				iPercent -= PythSpacesAway( pSoldier->sGridNo, pFriend->sGridNo ) * 2;
				if ( iPercent <= 0 )
				{
					//ignore!
					continue;
				}
			}

			// Evaluate support against the location this friend actually knows, not the
			// opponent object's hidden live position.
			INT32 sFriendKnownOpponent = KnownLocation(pFriend, pOpponent->ubID);
			if (TileIsOutOfBounds(sFriendKnownOpponent))
			{
				continue;
			}
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

	// make idiot administrators more aggressive
	// sevenfm: also make civilians more aggressive
	if (pSoldier->ubSoldierClass == SOLDIER_CLASS_ADMINISTRATOR || pSoldier->bTeam == CIV_TEAM && !pSoldier->aiData.bNeutral)
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
	if (CountFriendsFlankSameSpot(pSoldier) == 0)
	{
		bMoraleCategory++;
	}

	INT32 sClosestOpponent = ClosestKnownOpponent(pSoldier, NULL, NULL);

	// if last attack of this soldier hit enemy - increase morale
	if( pSoldier->aiData.bLastAttackHit )
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
		GuySawEnemy(pSoldier) &&
			(InARoom(pSoldier->sGridNo, NULL) && pSoldier->pathing.bLevel == 0 || 
			CountFriendsInDirection(pSoldier, AIDirection(pSoldier->sGridNo, sClosestOpponent), PythSpacesAway(sClosestOpponent, pSoldier->sGridNo), FALSE) ||
			CountFriendsInDirectionFromSpot(pSoldier, sClosestOpponent, AIDirection(sClosestOpponent, pSoldier->sGridNo), PythSpacesAway(sClosestOpponent, pSoldier->sGridNo)) || 			
			CountNearbyFriends(pSoldier, pSoldier->sGridNo, TACTICAL_RANGE / 2)) &&
		(AICheckSpecialRole(pSoldier) || pSoldier->aiData.bOrders != SEEKENEMY && !pSoldier->aiData.bLastAttackHit) &&
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
	INT32	iThreatValue = 0;
	BOOLEAN fForCreature = CREATURE_OR_BLOODCAT( pMe );

	// If man is inactive, at base, on assignment, dead, unconscious
	if (!pEnemy->bActive || !pEnemy->bInSector || !pEnemy->stats.bLife)
	{
		// he's no threat at all, return a negative number
		iThreatValue = -999;
		return(iThreatValue);
	}

	// in boxing mode, let only a boxer be considered a threat.
	if ( (gTacticalStatus.bBoxingState == BOXING) && !(pEnemy->flags.uiStatusFlags & SOLDIER_BOXER) )
	{
		iThreatValue = -999;
		return( iThreatValue );
	}

	if (fForCreature)
	{
		// health (1-100)
		iThreatValue += pEnemy->stats.bLife;
		// bleeding (more attactive!) (1-100)
		iThreatValue += pEnemy->bBleeding;
		// decrease according to distance
		iThreatValue = (iThreatValue * 10) / (10 + PythSpacesAway( sMyGrid, pEnemy->sGridNo ) );

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

	// if I have a specifically defined spot where I'm at (sometime I don't!)	
	if (!TileIsOutOfBounds(sMyGrid))
	{
		// ADD 10% if man's already been shooting at me
		if (pEnemy->sLastTarget == sMyGrid)
		{
			iThreatValue += (iThreatValue / 10);
		}
		else
		{
			// ADD 5% if man's already facing me
			if (pEnemy->ubDirection == atan8(CenterX(pEnemy->sGridNo),CenterY(pEnemy->sGridNo),CenterX(sMyGrid),CenterY(sMyGrid)))
			{
				iThreatValue += (iThreatValue / 20);
			}
		}
	}

	// if this man is conscious
	if (pEnemy->stats.bLife >= OKLIFE)
	{
		// and we were told to reduce threat for my cover		
		if (ubReduceForCover && (!TileIsOutOfBounds(sMyGrid)))
		{
			// Reduce iThreatValue to same % as the chance HE has shoot through at ME
			//iThreatValue = (iThreatValue * ChanceToGetThrough( pEnemy, myGrid, FAKE, ACTUAL, TESTWALLS, 9999, M9PISTOL, NOT_FOR_LOS)) / 100;
			//iThreatValue = (iThreatValue * SoldierTo3DLocationChanceToGetThrough( pEnemy, myGrid, FAKE, ACTUAL, TESTWALLS, 9999, M9PISTOL, NOT_FOR_LOS)) / 100;
			iThreatValue = (iThreatValue * SoldierToLocationChanceToGetThrough( pEnemy, sMyGrid, pMe->pathing.bLevel, 0, pMe->ubID ) ) / 100;
		}
	}
	else
	{
		// if he's still something of a threat
		if (iThreatValue > 0)
		{
			// drastically reduce his threat value (divide by 5 to 18)
			iThreatValue /= (4 + (OKLIFE - pEnemy->stats.bLife));
		}
	}

	// threat value of any opponent can never drop below 1
	if (iThreatValue < 1)
	{
		iThreatValue = 1;
	}

	//sprintf(tempstr,"%s's iThreatValue = ",pEnemy->name);
	//NumMessage(tempstr,iThreatValue);

#ifdef BETAVERSION	// unnecessary for real release
	// NOTE: maximum is about 200 for a healthy Mike type with a mortar!
	if (iThreatValue > 250)
	{
		sprintf(tempstr,"CalcManThreatValue: WARNING - %d has a very high threat value of %d",pEnemy->ubID,iThreatValue);

#ifdef RECORDNET
		fprintf(NetDebugFile,"\t%s\n",tempstr);
#endif

#ifdef TESTVERSION
		PopMessage(tempstr);
#endif

	}
#endif

	return(iThreatValue);
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

		// hard code militia;
		case SOLDIER_CLASS_GREEN_MILITIA:
			bDifficulty = 2;
			break;

		case SOLDIER_CLASS_REG_MILITIA:
			bDifficulty = 3;
			break;

		case SOLDIER_CLASS_ELITE_MILITIA:
			bDifficulty = 4;
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


				if (pSoldier->aiData.bOppList[ pOpponent->ubID ] == SEEN_CURRENTLY)
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

				if (pSoldier->aiData.bOppList[ pOpponent->ubID ] == SEEN_CURRENTLY)
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

				if (pSoldier->aiData.bOppList[ pOpponent->ubID ] == SEEN_CURRENTLY)
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
			!pFriend->IsCowering() &&
			!pFriend->IsUnconscious())
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
			pFriend->stats.bLife >= OKLIFE &&
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

// Enemy fireteam coordination. This state is sector-local and intentionally lives
// outside SOLDIERTYPE so it does not change the savegame structure.
#define AI_FIRETEAM_NONE 0
#define AI_FIRETEAM_TARGET 8
#define AI_FIRETEAM_MAX_NORMAL 9
#define AI_FIRETEAM_MAX_MERGED 11

static UINT8 gubAIFireteam[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIFireteamIdentity[MAX_NUM_SOLDIERS] = { 0 };
static UINT8 gubAINextFireteam = 1;
static INT16 gsAIFireteamSectorX = -1;
static INT16 gsAIFireteamSectorY = -1;
static INT8 gbAIFireteamSectorZ = -1;
static BOOLEAN gfAIFireteamsSeeded = FALSE;

static BOOLEAN AIEnemyFireteamEligible(SOLDIERTYPE *pSoldier)
{
	return pSoldier && pSoldier->bTeam == ENEMY_TEAM && pSoldier->bActive &&
		pSoldier->bInSector && pSoldier->stats.bLife > 0 &&
		!(pSoldier->usSoldierFlagMask & SOLDIER_POW);
}

static void AIResetFireteamsForSector(void)
{
	if (gsAIFireteamSectorX == gWorldSectorX && gsAIFireteamSectorY == gWorldSectorY &&
		gbAIFireteamSectorZ == gbWorldSectorZ)
		return;

	gsAIFireteamSectorX = gWorldSectorX;
	gsAIFireteamSectorY = gWorldSectorY;
	gbAIFireteamSectorZ = gbWorldSectorZ;
	gubAINextFireteam = 1;
	gfAIFireteamsSeeded = FALSE;
	for (UINT16 i = 0; i < MAX_NUM_SOLDIERS; ++i)
	{
		gubAIFireteam[i] = AI_FIRETEAM_NONE;
		guiAIFireteamIdentity[i] = 0;
	}
}

static UINT8 AIFireteamCountById(UINT8 ubFireteam, BOOLEAN fReadyOnly)
{
	if (ubFireteam == AI_FIRETEAM_NONE)
		return 0;

	UINT8 ubCount = 0;
	for (UINT16 iCounter = gTacticalStatus.Team[ENEMY_TEAM].bFirstID;
		iCounter <= gTacticalStatus.Team[ENEMY_TEAM].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!AIEnemyFireteamEligible(pFriend) || pFriend->ubID >= MAX_NUM_SOLDIERS ||
			guiAIFireteamIdentity[pFriend->ubID] != pFriend->uiUniqueSoldierIdValue ||
			gubAIFireteam[pFriend->ubID] != ubFireteam)
			continue;
		if (fReadyOnly && (pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed))
			continue;
		++ubCount;
	}
	return ubCount;
}

static INT32 AIFireteamDistanceToSpot(UINT8 ubFireteam, INT32 sSpot)
{
	INT32 iBest = 10000;
	for (UINT16 iCounter = gTacticalStatus.Team[ENEMY_TEAM].bFirstID;
		iCounter <= gTacticalStatus.Team[ENEMY_TEAM].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!AIEnemyFireteamEligible(pFriend) || pFriend->ubID >= MAX_NUM_SOLDIERS ||
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed ||
			guiAIFireteamIdentity[pFriend->ubID] != pFriend->uiUniqueSoldierIdValue ||
			gubAIFireteam[pFriend->ubID] != ubFireteam)
			continue;
		iBest = __min(iBest, PythSpacesAway(pFriend->sGridNo, sSpot));
	}
	return iBest;
}

static void AISeedEnemyFireteams(void)
{
	AIResetFireteamsForSector();
	if (gfAIFireteamsSeeded)
		return;

	UINT8 ubMembers[MAX_NUM_SOLDIERS];
	BOOLEAN fAssigned[MAX_NUM_SOLDIERS] = { FALSE };
	UINT16 usCount = 0;
	for (UINT16 iCounter = gTacticalStatus.Team[ENEMY_TEAM].bFirstID;
		iCounter <= gTacticalStatus.Team[ENEMY_TEAM].bLastID && usCount < MAX_NUM_SOLDIERS; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (AIEnemyFireteamEligible(pFriend))
			ubMembers[usCount++] = pFriend->ubID;
	}

	if (usCount == 0)
	{
		gfAIFireteamsSeeded = TRUE;
		return;
	}

	UINT16 usGroups = (usCount <= 10) ? 1 : (usCount + AI_FIRETEAM_TARGET - 1) / AI_FIRETEAM_TARGET;
	UINT16 usRemaining = usCount;
	for (UINT16 usGroup = 0; usGroup < usGroups && usRemaining > 0; ++usGroup)
	{
		UINT16 usGroupsLeft = usGroups - usGroup;
		UINT16 usTarget = (usRemaining + usGroupsLeft - 1) / usGroupsLeft;
		// A small sector force of ten remains one coherent element; larger forces
		// are balanced into normal 6-9 man elements.
		if (usGroups == 1)
			usTarget = usRemaining;
		else
			usTarget = __min((UINT16)AI_FIRETEAM_MAX_NORMAL, usTarget);

		INT16 sSeedIndex = -1;
		for (UINT16 i = 0; i < usCount; ++i)
			if (!fAssigned[i]) { sSeedIndex = (INT16)i; break; }
		if (sSeedIndex < 0)
			break;

		UINT8 ubFireteam = gubAINextFireteam++;
		UINT8 ubSeedId = ubMembers[sSeedIndex];
		SOLDIERTYPE *pSeed = MercPtrs[ubSeedId];
		fAssigned[sSeedIndex] = TRUE;
		gubAIFireteam[ubSeedId] = ubFireteam;
		guiAIFireteamIdentity[ubSeedId] = pSeed->uiUniqueSoldierIdValue;
		--usRemaining;

		for (UINT16 usAdded = 1; usAdded < usTarget && usRemaining > 0; ++usAdded)
		{
			INT16 sBestIndex = -1;
			INT32 iBestDistance = 10000;

			for (UINT16 i = 0; i < usCount; ++i)
			{
				if (fAssigned[i])
					continue;

				SOLDIERTYPE *pCandidate = MercPtrs[ubMembers[i]];
				if (!pCandidate)
					continue;

				// Grow from the current fireteam footprint rather than measuring
				// every new member only from the original seed. This keeps the
				// element spatially connected even on irregular deployments.
				INT32 iCandidateDistance = 10000;

				for (UINT16 j = 0; j < usCount; ++j)
				{
					if (!fAssigned[j])
						continue;

					UINT8 ubAssignedId = ubMembers[j];
					if (gubAIFireteam[ubAssignedId] != ubFireteam)
						continue;

					SOLDIERTYPE *pMember = MercPtrs[ubAssignedId];
					if (!pMember)
						continue;

					INT32 iDistance = PythSpacesAway(
						pMember->sGridNo, pCandidate->sGridNo);

					// Same-elevation neighbours are preferred. A roof/ground
					// combination is still possible when no better cluster fit exists.
					if (pMember->pathing.bLevel != pCandidate->pathing.bLevel)
						iDistance += __max(6, DAY_VISION_RANGE / 3);

					iCandidateDistance = __min(iCandidateDistance, iDistance);
				}

				if (iCandidateDistance < iBestDistance)
				{
					iBestDistance = iCandidateDistance;
					sBestIndex = (INT16)i;
				}
			}

			if (sBestIndex < 0)
				break;

			UINT8 ubId = ubMembers[sBestIndex];
			fAssigned[sBestIndex] = TRUE;
			gubAIFireteam[ubId] = ubFireteam;
			guiAIFireteamIdentity[ubId] = MercPtrs[ubId]->uiUniqueSoldierIdValue;
			--usRemaining;
		}
	}
	gfAIFireteamsSeeded = TRUE;
}

static void AIEnsureEnemyFireteams(void)
{
	AISeedEnemyFireteams();
	for (UINT16 iCounter = gTacticalStatus.Team[ENEMY_TEAM].bFirstID;
		iCounter <= gTacticalStatus.Team[ENEMY_TEAM].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pSoldier = MercPtrs[iCounter];
		if (!AIEnemyFireteamEligible(pSoldier) || pSoldier->ubID >= MAX_NUM_SOLDIERS)
			continue;
		if (guiAIFireteamIdentity[pSoldier->ubID] == pSoldier->uiUniqueSoldierIdValue &&
			gubAIFireteam[pSoldier->ubID] != AI_FIRETEAM_NONE)
			continue;

		UINT8 ubBest = AI_FIRETEAM_NONE;
		INT32 iBest = 10000;
		for (UINT8 ubTeam = 1; ubTeam < gubAINextFireteam; ++ubTeam)
		{
			if (AIFireteamCountById(ubTeam, FALSE) >= AI_FIRETEAM_MAX_NORMAL)
				continue;
			INT32 iDistance = AIFireteamDistanceToSpot(ubTeam, pSoldier->sGridNo);
			if (iDistance < iBest) { iBest = iDistance; ubBest = ubTeam; }
		}
		if (ubBest == AI_FIRETEAM_NONE)
			ubBest = gubAINextFireteam++;
		gubAIFireteam[pSoldier->ubID] = ubBest;
		guiAIFireteamIdentity[pSoldier->ubID] = pSoldier->uiUniqueSoldierIdValue;
	}
}

static BOOLEAN AIAbsorbFireteamRemnant(SOLDIERTYPE *pSoldier)
{
	if (!AIEnemyFireteamEligible(pSoldier))
		return FALSE;
	AIEnsureEnemyFireteams();
	UINT8 ubOld = gubAIFireteam[pSoldier->ubID];
	UINT8 ubReady = AIFireteamCountById(ubOld, TRUE);
	if (ubReady == 0 || ubReady > 2)
		return FALSE;

	UINT8 ubOldTotal = AIFireteamCountById(ubOld, FALSE);
	UINT8 ubBest = AI_FIRETEAM_NONE;
	INT32 iBest = 10000;
	for (UINT8 ubTeam = 1; ubTeam < gubAINextFireteam; ++ubTeam)
	{
		if (ubTeam == ubOld || AIFireteamCountById(ubTeam, TRUE) < 3)
			continue;
		if (AIFireteamCountById(ubTeam, FALSE) + ubOldTotal > AI_FIRETEAM_MAX_MERGED)
			continue;
		INT32 iDistance = AIFireteamDistanceToSpot(ubTeam, pSoldier->sGridNo);
		if (iDistance < iBest) { iBest = iDistance; ubBest = ubTeam; }
	}
	if (ubBest == AI_FIRETEAM_NONE)
		return FALSE;

	for (UINT16 iCounter = gTacticalStatus.Team[ENEMY_TEAM].bFirstID;
		iCounter <= gTacticalStatus.Team[ENEMY_TEAM].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (AIEnemyFireteamEligible(pFriend) && pFriend->ubID < MAX_NUM_SOLDIERS &&
			guiAIFireteamIdentity[pFriend->ubID] == pFriend->uiUniqueSoldierIdValue &&
			gubAIFireteam[pFriend->ubID] == ubOld)
			gubAIFireteam[pFriend->ubID] = ubBest;
	}
	return TRUE;
}

UINT8 AIFireteamId(SOLDIERTYPE *pSoldier)
{
	if (!AIEnemyFireteamEligible(pSoldier) || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return AI_FIRETEAM_NONE;
	AIEnsureEnemyFireteams();
	return gubAIFireteam[pSoldier->ubID];
}

UINT8 AIFireteamAliveCount(SOLDIERTYPE *pSoldier)
{
	return AIFireteamCountById(AIFireteamId(pSoldier), TRUE);
}

BOOLEAN AISameFireteam(SOLDIERTYPE *pSoldier, SOLDIERTYPE *pFriend)
{
	if (!pSoldier || !pFriend || pSoldier->bTeam != pFriend->bTeam)
		return FALSE;
	if (pSoldier->bTeam != ENEMY_TEAM)
		return TRUE;
	UINT8 ubMine = AIFireteamId(pSoldier);
	return ubMine != AI_FIRETEAM_NONE && ubMine == AIFireteamId(pFriend);
}

BOOLEAN AIFireteamShouldHoldReserve(SOLDIERTYPE *pSoldier, INT32 sContactSpot, UINT8 ubResponseLimit)
{
	if (!AIEnemyFireteamEligible(pSoldier) || TileIsOutOfBounds(sContactSpot))
		return FALSE;
	AIAbsorbFireteamRemnant(pSoldier);
	UINT8 ubMine = AIFireteamId(pSoldier);
	INT32 iMine = AIFireteamDistanceToSpot(ubMine, sContactSpot);
	UINT16 usCloserReady = 0;
	for (UINT8 ubTeam = 1; ubTeam < gubAINextFireteam; ++ubTeam)
	{
		if (ubTeam == ubMine)
			continue;
		UINT8 ubReady = AIFireteamCountById(ubTeam, TRUE);
		if (ubReady == 0)
			continue;
		INT32 iDistance = AIFireteamDistanceToSpot(ubTeam, sContactSpot);
		if (iDistance < iMine || (iDistance == iMine && ubTeam < ubMine))
			usCloserReady += ubReady;
	}
	return usCloserReady >= ubResponseLimit;
}

INT8 DecideFireteamCohesionAction(SOLDIERTYPE *pSoldier, BOOLEAN fCanMove)
{
	if (!fCanMove || !gfTurnBasedAI || !AIEnemyFireteamEligible(pSoldier) ||
		pSoldier->stats.bLife < OKLIFE || pSoldier->bCollapsed ||
		pSoldier->aiData.bUnderFire || pSoldier->aiData.bOppCnt > 0 ||
		GuySawEnemy(pSoldier, SEEN_LAST_TURN) || pSoldier->aiData.bOrders == STATIONARY ||
		pSoldier->aiData.bOrders == SNIPER)
		return AI_ACTION_NONE;

	UINT8 ubBefore = AIFireteamAliveCount(pSoldier);
	BOOLEAN fWasRemnant = (ubBefore > 0 && ubBefore <= 2);
	AIAbsorbFireteamRemnant(pSoldier);

	SOLDIERTYPE *pAnchor = NULL;
	INT32 iBest = 10000;
	BOOLEAN fEngagedAnchor = FALSE;
	for (UINT16 iCounter = gTacticalStatus.Team[ENEMY_TEAM].bFirstID;
		iCounter <= gTacticalStatus.Team[ENEMY_TEAM].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (!pFriend || pFriend == pSoldier || !AIEnemyFireteamEligible(pFriend) ||
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed || !AISameFireteam(pSoldier, pFriend))
			continue;
		BOOLEAN fEngaged = pFriend->aiData.bUnderFire || pFriend->aiData.bOppCnt > 0 || GuySawEnemy(pFriend, SEEN_LAST_TURN);
		INT32 iDistance = PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo);
		if (fEngaged && (!fEngagedAnchor || iDistance < iBest))
		{
			fEngagedAnchor = TRUE; pAnchor = pFriend; iBest = iDistance;
		}
		else if (!fEngagedAnchor && iDistance < iBest)
		{
			pAnchor = pFriend; iBest = iDistance;
		}
	}

	if (!pAnchor || (!fWasRemnant && !fEngagedAnchor))
		return AI_ACTION_NONE;
	if (iBest <= __max(8, DAY_VISION_RANGE / 2))
		return AI_ACTION_NONE;

	INT8 bReserveAP = fEngagedAnchor ?
		(GetAPsCrouch(pSoldier, TRUE) + GetAPsToLook(pSoldier)) : 0;
	UINT8 ubFlags = fEngagedAnchor ? FLAG_CAUTIOUS : 0;

	pSoldier->aiData.usActionData = InternalGoAsFarAsPossibleTowards(
		pSoldier, pAnchor->sGridNo, bReserveAP, AI_ACTION_SEEK_FRIEND, ubFlags);

	if (TileIsOutOfBounds(pSoldier->aiData.usActionData) ||
		pSoldier->aiData.usActionData == pSoldier->sGridNo)
	{
		return AI_ACTION_NONE;
	}

	if (!CheckNPCDestination(pSoldier, pSoldier->aiData.usActionData))
		return AI_ACTION_NONE;

	if (fEngagedAnchor)
	{
		UINT16 usCurrentExposure = AIKnownThreatExposure(
			pSoldier, pSoldier->sGridNo, pSoldier->pathing.bLevel);
		UINT16 usMoveExposure = AIKnownThreatExposure(
			pSoldier, pSoldier->aiData.usActionData, pSoldier->pathing.bLevel);

		if (usMoveExposure > usCurrentExposure + 150 &&
			!AnyCoverAtSpot(pSoldier, pSoldier->aiData.usActionData))
		{
			return AI_ACTION_NONE;
		}

		pSoldier->aiData.fAIFlags |= AI_CAUTIOUS;
	}

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
			pFriend->stats.bLife < OKLIFE &&
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
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > TACTICAL_RANGE)
		{
			continue;
		}

		if (pFriend->stats.bLife < OKLIFE)
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
// Friendly status is known to the team; opponent strength is derived only from
// personal/public JA2 knowledge and never from hidden sector totals.
UINT16 AIPerceivedFriendlyStrength(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier))
		return 0;

	UINT32 uiStrength = 0;

	for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; ++iCounter)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];
		if (pFriend &&
			pFriend->bActive &&
			pFriend->bInSector &&
			pFriend->stats.bLife >= OKLIFE &&
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) <= TACTICAL_RANGE)
		{
			uiStrength += 100;
		}
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

		// Do not use ValidOpponent() here: it checks actual current life/sector state.
		// Once a contact is known, relation filters are safe; hidden existence is not.
		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			pSoldier->bSide == pOpponent->bSide ||
			(pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			pOpponent->ubBodyType == CROW)
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
		uiStrength += ThreatPercent[bKnowledge - OLDEST_HEARD_VALUE];
	}

	return (UINT16)__min((UINT32)65535, uiStrength);
}

INT8 AIBattleSituation(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier))
		return AI_BATTLE_UNKNOWN;

	UINT32 uiFriends = AIPerceivedFriendlyStrength(pSoldier);
	UINT32 uiEnemies = AIPerceivedEnemyStrength(pSoldier);

	// With no legitimate opponent knowledge there is no force-ratio assessment.
	if (uiEnemies == 0)
		return AI_BATTLE_UNKNOWN;

	UINT8 ubCasualties = AIFriendlyCasualtyPercent(pSoldier);

	if (uiFriends * 2 <= uiEnemies ||
		(ubCasualties >= 75 && uiFriends <= uiEnemies))
	{
		return AI_BATTLE_CATASTROPHIC;
	}

	if (uiFriends * 5 < uiEnemies * 4 || ubCasualties >= 50)
		return AI_BATTLE_LOSING;

	if (uiFriends * 4 >= uiEnemies * 5 && ubCasualties < 40)
		return AI_BATTLE_WINNING;

	return AI_BATTLE_EVEN;
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
		if (pFriend && pFriend->bActive && pFriend->bInSector && pFriend->stats.bLife >= OKLIFE)
			++ubTeamReady;
	}

	UINT8 ubLocalCasualties = AILocalCasualtyPercent(pSoldier);
	UINT8 ubKnownFriendlyLosses = ubLocalCasualties;
	if (pSoldier->bTeam == ENEMY_TEAM)
		ubKnownFriendlyLosses = __max(ubKnownFriendlyLosses, TeamPercentKilled(ENEMY_TEAM));

	// True last survivors: only one/two combat-capable soldiers remain on the team,
	// and meaningful friendly losses have actually occurred.
	if (ubTeamReady <= 2 && ubKnownFriendlyLosses >= 50)
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
			pFriend->pathing.bLevel != pSoldier->pathing.bLevel ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > iRadius)
		{
			continue;
		}

		BOOLEAN fEscaping = AIEscapeEstablishedForRout(pFriend);
		BOOLEAN fDisengaging = AIDisengagementEstablishedForRout(pFriend);
		BOOLEAN fRunningAway = (pFriend->aiData.bAction == AI_ACTION_RUN_AWAY);
		BOOLEAN fLeader = AICheckIsOfficer(pFriend) || AICheckIsCommander(pFriend);
		BOOLEAN fEstablishedBreak = fEscaping || fDisengaging;

		// Breaking friends exert social pressure only at local tactical scale.
		// Escape is the strongest signal; deliberate disengagement is weaker.
		if (fEscaping)
			iPressure += 35;
		else if (fDisengaging)
			iPressure += 20;
		else if (fRunningAway)
			iPressure += 15;

		if (fEstablishedBreak)
			++ubEstablishedBreakers;

		// A leader visibly abandoning the fight is especially destabilising.
		if (fLeader && (fEscaping || fDisengaging || fRunningAway))
		{
			iPressure += 10;
			if (fEstablishedBreak)
				fBreakingLeader = TRUE;
		}
		// A nearby leader who is still holding together can slow a cascade, but
		// cannot erase several nearby soldiers already breaking contact.
		else if (fLeader && !pFriend->aiData.bUnderFire)
		{
			iPressure -= 15;
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

static void AIClearEscapeState(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	gubAIEscapeIntent[pSoldier->ubID] = 0;
	guiAIEscapeIdentity[pSoldier->ubID] = pSoldier->uiUniqueSoldierIdValue;
	guiAIEscapeStartTurn[pSoldier->ubID] = 0;
}

BOOLEAN AIEscapeActive(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->bTeam != ENEMY_TEAM || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return FALSE;

	if (guiAIEscapeIdentity[pSoldier->ubID] != pSoldier->uiUniqueSoldierIdValue)
		return FALSE;

	return (pSoldier->aiData.bAlertStatus >= STATUS_RED &&
		gubAIEscapeIntent[pSoldier->ubID] != 0);
}

static INT8 AIProfessionalismModifier(SOLDIERTYPE *pSoldier);
static BOOLEAN AIHasNearbyStableLeader(SOLDIERTYPE *pSoldier);
static UINT8 AIUpdateRecoveryStreak(SOLDIERTYPE *pSoldier, INT8 bSituation,
	UINT8 ubRoutPressure, BOOLEAN fLastSurvivor);
static void AIResetRecoveryStreak(SOLDIERTYPE *pSoldier);

static BOOLEAN AIShouldStartEscapeFromState(SOLDIERTYPE *pSoldier, INT8 bSituation, UINT8 ubCasualties, BOOLEAN fLastSurvivor, UINT8 ubRoutPressure)
{
	// Escape is intentionally much rarer than disengagement. A bad local position is
	// not enough: the soldier needs evidence that the fight itself is collapsing.
	if (fLastSurvivor)
		return TRUE;

	if (ubCasualties >= 75 && bSituation != AI_BATTLE_WINNING)
		return TRUE;

	INT32 iRoutThreshold = 60 +
		(AIPersonalRiskTolerance(pSoldier) - 50) / 2;
	iRoutThreshold = __max(45, __min(75, iRoutThreshold));

	if (bSituation == AI_BATTLE_CATASTROPHIC)
	{
		INT32 iCasualtyThreshold = 30 + AIProfessionalismModifier(pSoldier) / 2;
		iCasualtyThreshold = __max(25, __min(38, iCasualtyThreshold));

		if (ubCasualties >= iCasualtyThreshold)
			return TRUE;

		if (AISeverelyIsolated(pSoldier) &&
			AILocalStress(pSoldier) >= 50 &&
			AIPersonalRisk(pSoldier) >= AIPersonalRiskTolerance(pSoldier))
		{
			return TRUE;
		}

		// A catastrophic fight can become a rout even before the raw casualty
		// threshold if several nearby comrades are already breaking contact.
		if (ubRoutPressure >= __max(40, iRoutThreshold - 10) &&
			AILocalStress(pSoldier) >= 25)
		{
			return TRUE;
		}
	}

	// In a merely losing fight, social collapse can push a soldier from
	// disengagement into full escape, but only after substantial losses.
	INT32 iLosingEscapeThreshold = 40 + AIProfessionalismModifier(pSoldier) / 2;
	iLosingEscapeThreshold = __max(35, __min(48, iLosingEscapeThreshold));

	if (bSituation == AI_BATTLE_LOSING &&
		ubCasualties >= iLosingEscapeThreshold &&
		ubRoutPressure >= iRoutThreshold &&
		AILocalStress(pSoldier) >= 30)
	{
		return TRUE;
	}

	return FALSE;
}

BOOLEAN AIShouldStartEscape(SOLDIERTYPE *pSoldier)
{
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

	INT8 bSituation = AIBattleSituation(pSoldier);
	if (bSituation == AI_BATTLE_UNKNOWN)
		return FALSE;

	return AIShouldStartEscapeFromState(pSoldier, bSituation,
		AIFriendlyCasualtyPercent(pSoldier), AILastSurvivorPressure(pSoldier),
		AILocalRoutPressure(pSoldier));
}

static void AIUpdateEscapeStateFromSnapshot(SOLDIERTYPE *pSoldier, INT8 bSituation, UINT8 ubCasualties, BOOLEAN fLastSurvivor, UINT8 ubRoutPressure)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return;

	UINT8 ubID = pSoldier->ubID;
	if (guiAIEscapeIdentity[ubID] != pSoldier->uiUniqueSoldierIdValue)
	{
		gubAIEscapeIntent[ubID] = 0;
		guiAIEscapeIdentity[ubID] = pSoldier->uiUniqueSoldierIdValue;
		guiAIEscapeStartTurn[ubID] = 0;
	}

	if (pSoldier->bTeam != ENEMY_TEAM || pSoldier->IsZombie() ||
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

	if (gubAIEscapeIntent[ubID] == 0 &&
		bSituation != AI_BATTLE_UNKNOWN &&
		AIShouldStartEscapeFromState(pSoldier, bSituation, ubCasualties, fLastSurvivor, ubRoutPressure))
	{
		gubAIEscapeIntent[ubID] = 1;
		guiAIEscapeStartTurn[ubID] = guiTurnCnt + 1;
		AIResetRecoveryStreak(pSoldier);
	}
}

static UINT8 gubAIDisengageTurns[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIDisengageTurnStamp[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIDisengageIdentity[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIDisengageStartTurn[MAX_NUM_SOLDIERS] = { 0 };

static UINT8 gubAIRecoveryStreak[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIRecoveryTurnStamp[MAX_NUM_SOLDIERS] = { 0 };
static UINT32 guiAIRecoveryIdentity[MAX_NUM_SOLDIERS] = { 0 };

BOOLEAN AIDisengagementActive(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier) || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return FALSE;

	if (guiAIDisengageIdentity[pSoldier->ubID] != pSoldier->uiUniqueSoldierIdValue)
		return FALSE;

	return (pSoldier->aiData.bAlertStatus >= STATUS_RED &&
		gubAIDisengageTurns[pSoldier->ubID] > 0);
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
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed ||
			pFriend->pathing.bLevel != pSoldier->pathing.bLevel ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > TACTICAL_RANGE / 2 ||
			AIDisengagementActive(pFriend) || AIEscapeActive(pFriend) ||
			pFriend->aiData.bUnderFire)
		{
			continue;
		}

		if (AICheckIsCommander(pFriend) || AICheckIsOfficer(pFriend))
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
		CountNearbyFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 4) >= 2 ||
		AIHasNearbyStableLeader(pSoldier) ||
		(AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) &&
		 (pSoldier->LastAttackHit() || pSoldier->LastTargetSuppressed()));

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
	if (bSituation == AI_BATTLE_CATASTROPHIC || fLastSurvivor)
		return TRUE;

	INT32 iRoutThreshold = 40 +
		(AIPersonalRiskTolerance(pSoldier) - 50) / 2;
	iRoutThreshold = __max(25, __min(60, iRoutThreshold));

	if (bSituation == AI_BATTLE_LOSING)
	{
		INT32 iDisengageThreshold = 30 + AIProfessionalismModifier(pSoldier) / 2;
		iDisengageThreshold = __max(25, __min(38, iDisengageThreshold));

		if (ubCasualties >= iDisengageThreshold)
			return TRUE;

		if (AILocalStress(pSoldier) >= 35 &&
			AIPersonalRisk(pSoldier) >= AIPersonalRiskTolerance(pSoldier))
		{
			return TRUE;
		}

		if (ubRoutPressure >= iRoutThreshold &&
			(ubCasualties >= 20 || AILocalStress(pSoldier) >= 25))
		{
			return TRUE;
		}
	}

	// Even a nominally even fight can locally unravel when casualties are already
	// meaningful and multiple nearby comrades are visibly breaking contact.
	INT32 iEvenBreakThreshold = 30 + AIProfessionalismModifier(pSoldier) / 2;
	iEvenBreakThreshold = __max(25, __min(38, iEvenBreakThreshold));

	if (bSituation == AI_BATTLE_EVEN &&
		ubCasualties >= iEvenBreakThreshold &&
		ubRoutPressure >= __min(70, iRoutThreshold + 15) &&
		AILocalStress(pSoldier) >= 25)
	{
		return TRUE;
	}

	if (ubCasualties >= 50 &&
		bSituation != AI_BATTLE_WINNING &&
		AISeverelyIsolated(pSoldier))
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
		pSoldier->aiData.bOrders == STATIONARY ||
		pSoldier->aiData.bAttitude == ATTACKSLAYONLY ||
		AIPerceivedEnemyStrength(pSoldier) == 0)
	{
		return FALSE;
	}

	INT8 bSituation = AIBattleSituation(pSoldier);
	UINT8 ubCasualties = AIFriendlyCasualtyPercent(pSoldier);
	BOOLEAN fLastSurvivor = AILastSurvivorPressure(pSoldier);
	return AIShouldStartDisengagementFromState(pSoldier, bSituation, ubCasualties,
		fLastSurvivor, AILocalRoutPressure(pSoldier));
}
BOOLEAN AIUpdateDisengagementState(SOLDIERTYPE *pSoldier)
{
	if (!pSoldier || pSoldier->ubID >= MAX_NUM_SOLDIERS)
		return FALSE;

	UINT8 ubID = pSoldier->ubID;
	if (guiAIDisengageIdentity[ubID] != pSoldier->uiUniqueSoldierIdValue)
	{
		gubAIDisengageTurns[ubID] = 0;
		guiAIDisengageTurnStamp[ubID] = 0;
		guiAIDisengageIdentity[ubID] = pSoldier->uiUniqueSoldierIdValue;
		guiAIDisengageStartTurn[ubID] = 0;
	}

	if (!AICombatTeam(pSoldier) ||
		pSoldier->ubProfile != NO_PROFILE ||
		pSoldier->IsZombie() || pSoldier->aiData.bAlertStatus < STATUS_RED)
	{
		gubAIDisengageTurns[ubID] = 0;
		guiAIDisengageStartTurn[ubID] = 0;
		AIClearEscapeState(pSoldier);
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

	if (pSoldier->aiData.bOrders == STATIONARY)
	{
		gubAIDisengageTurns[ubID] = 0;
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
	}

	if (gubAIDisengageTurns[ubID] > 0)
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

	if (bSituation != AI_BATTLE_UNKNOWN &&
		AIShouldStartDisengagementFromState(pSoldier, bSituation, ubCasualties,
			fLastSurvivor, ubRoutPressure))
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

		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			pSoldier->bSide == pOpponent->bSide ||
			(pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			pOpponent->ubBodyType == CROW)
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

BOOLEAN AIShouldConsiderTacticalFallback(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier) || pSoldier->IsZombie() ||
		pSoldier->aiData.bOrders == STATIONARY || AIShouldAvoidAdvance(pSoldier))
	{
		return FALSE;
	}

	INT32 sThreat = ClosestKnownOpponent(pSoldier, NULL, NULL);
	if (TileIsOutOfBounds(sThreat))
		return FALSE;

	// Do not shuffle a soldier who is currently succeeding from a sound position.
	if (!pSoldier->aiData.bUnderFire &&
		AnyCoverAtSpot(pSoldier, pSoldier->sGridNo) &&
		pSoldier->LastAttackHit() &&
		AILocalStress(pSoldier) < 25 &&
		AIEngagementRangeModifier(pSoldier, sThreat) >= 0)
	{
		return FALSE;
	}

	INT32 iPressure = 0;
	INT8 bSituation = AIBattleSituation(pSoldier);

	if (bSituation == AI_BATTLE_LOSING)
		iPressure += 2;
	if (pSoldier->aiData.bUnderFire)
		iPressure += 2;
	if (!AnyCoverAtSpot(pSoldier, pSoldier->sGridNo))
		iPressure += 2;
	if (AIPersonalRisk(pSoldier) + 10 >= AIPersonalRiskTolerance(pSoldier))
		iPressure += 2;
	if (AILocalStress(pSoldier) >= 35)
		iPressure += 1;
	if (AIEngagementRangeModifier(pSoldier, sThreat) < 0)
		iPressure += 1;
	if (CountNearbyFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 4) == 0)
		iPressure += 1;

	// A winning group gives ground only under clear immediate pressure.
	INT32 iThreshold = (bSituation == AI_BATTLE_WINNING) ? 4 : 3;
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

	UINT8 ubNearbyFriends = CountNearbyFriends(pSoldier, pSoldier->sGridNo,
		DAY_VISION_RANGE / 4);
	if (ubNearbyFriends == 0)
		iStress += 12;
	else if (ubNearbyFriends >= 3)
		iStress -= 8;

	// Small stabilising effects: success and effective team pressure help, but do
	// not erase severe suppression, wounds or casualties.
	if (pSoldier->LastAttackHit())
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
	UINT8 ubNearbyFriends = CountNearbyFriends(pSoldier, pSoldier->sGridNo, DAY_VISION_RANGE / 4);
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

	if (AICheckIsCommander(pSoldier))
		iModifier += 5;
	else if (AICheckIsOfficer(pSoldier))
		iModifier += 3;

	return (INT8)__max(-10, __min(15, iModifier));
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

	return __max(20, __min(85, iTolerance));
}

// Dynamic fireteam role suitability. These are not permanent classes: the score is
// recalculated from current weapon, position, wounds, fatigue and local stress, so a
// soldier can change from maneuver to support (or back) as the fight develops.
INT32 AISupportRoleScore(SOLDIERTYPE *pSoldier, INT32 sTargetSpot)
{
	if (!AICombatTeam(pSoldier) || !pSoldier->bActive || !pSoldier->bInSector ||
		pSoldier->stats.bLife < OKLIFE || pSoldier->bCollapsed ||
		!AICheckHasGun(pSoldier) || AIGunAmmo(pSoldier) == 0)
	{
		return -10000;
	}

	if (TileIsOutOfBounds(sTargetSpot))
		sTargetSpot = ClosestKnownOpponent(pSoldier, NULL, NULL);

	INT32 iScore = 20;
	INT32 iGunRange = __max(1, (INT32)AIGunRange(pSoldier) / CELL_X_SIZE);

	if (AICheckIsMachinegunner(pSoldier))
		iScore += 35;
	if (AICheckIsSniper(pSoldier))
		iScore += 30;
	else if (AICheckIsMarksman(pSoldier))
		iScore += 18;
	if (AIGunAutofireCapable(pSoldier))
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
		pSoldier->stats.bLife < OKLIFE || pSoldier->bCollapsed ||
		(pSoldier->usSoldierFlagMask & SOLDIER_POW) ||
		(pSoldier->flags.uiStatusFlags & SOLDIER_COWERING) ||
		AIDisengagementActive(pSoldier) || AIEscapeActive(pSoldier))
	{
		return -10000;
	}

	if (TileIsOutOfBounds(sTargetSpot))
		sTargetSpot = ClosestKnownOpponent(pSoldier, NULL, NULL);

	INT32 iHealthPercent = pSoldier->stats.bLifeMax > 0 ?
		(100 * pSoldier->stats.bLife) / pSoldier->stats.bLifeMax : 0;
	INT32 iScore = 20;

	iScore += (INT32)pSoldier->stats.bAgility / 6;
	iScore += (INT32)pSoldier->stats.bDexterity / 12;
	iScore += iHealthPercent / 6;
	iScore += (INT32)pSoldier->bBreath / 12;

	if (AICheckHasGun(pSoldier))
	{
		if (AICheckShortWeaponRange(pSoldier))
			iScore += 18;
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
	if (AICheckIsCommander(pSoldier))
		iScore -= 10;
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
			pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed ||
			!AICheckHasGun(pFriend) || AIGunAmmo(pFriend) == 0 ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > DAY_VISION_RANGE)
		{
			continue;
		}

		INT32 sFriendThreat = ClosestKnownOpponent(pFriend, NULL, NULL);
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
			pFriend->stats.bLife >= OKLIFE && !pFriend->bCollapsed &&
			AISameFireteam(pSoldier, pFriend) &&
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) <= DAY_VISION_RANGE / 4)
		{
			++ubNearbyFriends;
		}
	}
	INT32 iModifier = 0;

	if (ubNearbyFriends == 0)
		iModifier -= 2;
	else if (ubNearbyFriends == 2)
		iModifier += 1;
	else if (ubNearbyFriends >= 3)
		iModifier += 2;

	if (!TileIsOutOfBounds(sTargetSpot))
	{
		// A teammate already in contact with this threat provides useful covering
		// pressure and makes a coordinated move less likely to become an isolated rush.
		if (CountFriendsBlack(pSoldier, sTargetSpot) > 0)
			iModifier += 1;

		if (AICheckWeOutnumberLocal(pSoldier, sTargetSpot))
			iModifier += 1;
	}

	return (INT8)__max(-3, __min(3, iModifier));
}

static INT32 AIBoundedDecisionJitter(SOLDIERTYPE *pSoldier, UINT32 uiSalt, INT32 iAmplitude);

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

	// Fire-and-manoeuvre role separation. Two nearby soldiers may actively bound
	// toward essentially the same known contact. A third healthy soldier normally
	// stays in the firing line instead of joining a mass rush. This counts only
	// moves that materially close distance and only recent/current movement.
	if (iAdvanceDist + 2 < iCurrentDist &&
		!pSoldier->aiData.bUnderFire &&
		AIPersonalRisk(pSoldier) <= AIPersonalRiskTolerance(pSoldier))
	{
		UINT8 ubActiveMovers = 0;
		UINT8 ubMoverLimit = 2;
		INT32 iMoverJitter = AIBoundedDecisionJitter(pSoldier,
			(UINT32)(sTargetSpot + 101), 6);

		// Most fireteams use two movers. Sometimes a cautious element sends one;
		// occasionally a locally superior, low-stress element pushes three.
		if (iMoverJitter <= -4)
			ubMoverLimit = 1;
		else if (iMoverJitter >= 5 &&
			AILocalStress(pSoldier) < 20 &&
			AICheckWeOutnumberLocal(pSoldier, sTargetSpot))
			ubMoverLimit = 3;

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
				pCandidate->stats.bLife < OKLIFE || pCandidate->bCollapsed ||
				pCandidate->pathing.bLevel != pSoldier->pathing.bLevel ||
				pCandidate->bActionPoints <= 0 ||
				PythSpacesAway(pSoldier->sGridNo, pCandidate->sGridNo) > TACTICAL_RANGE / 2)
			{
				continue;
			}

			INT32 sCandidateThreat = ClosestKnownOpponent(pCandidate, NULL, NULL);
			if (TileIsOutOfBounds(sCandidateThreat) ||
				PythSpacesAway(sCandidateThreat, sTargetSpot) > 3)
			{
				continue;
			}

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
				pFriend->stats.bLife < OKLIFE || pFriend->bCollapsed ||
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

		if (!LocationToLocationLineOfSightTest(pFriend->sGridNo, pFriend->pathing.bLevel,
			sTargetSpot, bTargetLevel, TRUE, MAX_VISION_RANGE))
		{
			continue;
		}

		++ubSupporters;
		if (ubSupporters >= 2)
			break;
	}

	BOOLEAN fSeverelyExposed =
		(usAdvanceExposure > usCurrentExposure + 150) ||
		(!fAdvanceCover && usAdvanceExposure >= 200);

	if (fSeverelyExposed)
		return fAdvanceCover ? (ubSupporters >= 1) : (ubSupporters >= 2);

	if (ubSupporters >= 1)
		return TRUE;

	// A very bold soldier may make a modest unsupported dash, but not while
	// stressed and never into the severe-exposure case above.
	return ((pSoldier->aiData.bAttitude == AGGRESSIVE ||
		pSoldier->aiData.bAttitude == BRAVESOLO) &&
		AILocalStress(pSoldier) < 25);
}

// Range-aware movement preference. Positive values mean closing distance is useful;
// negative values mean a scoped/long-range soldier is already too close for the
// role his current weapon is best suited to. Weapon range is converted to tiles
// to match the rest of the tactical AI distance calculations.
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

	// Dedicated long-range roles should preserve substantially more standoff.
	if (AICheckIsSniper(pSoldier))
		iPreferredMinRange = __max(12, iGunRange / 3);
	else if (AICheckIsMarksman(pSoldier))
		iPreferredMinRange = __max(10, iGunRange / 4);
	else if (dScope >= 4.0f)
		iPreferredMinRange = __max(8, (INT32)(dScope * 2.0f));
	else if (dScope >= 2.0f)
		iPreferredMinRange = 6;

	// Never demand a minimum range that consumes most of the weapon's usable range.
	if (iPreferredMinRange > 0)
		iPreferredMinRange = __min(iPreferredMinRange, __max(6, iGunRange / 2));

	if (iPreferredMinRange > 0)
	{
		if (iDistance < __max(4, iPreferredMinRange / 2))
			return -3;
		if (iDistance < iPreferredMinRange)
			return -2;
	}

	// Closing distance is useful only when the target is genuinely outside the
	// current gun's effective range. Being inside range is not a reason to rush.
	if (iDistance > iGunRange + iGunRange / 4)
		return 2;
	if (iDistance > iGunRange)
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
			pFriend->stats.bLife < OKLIFE)
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
			(pFriend->aiData.bAction == AI_ACTION_FIRE_GUN ||
			 (pFriend->aiData.bLastAction == AI_ACTION_FIRE_GUN &&
			  pFriend->bActionPoints < pFriend->bInitialActionPoints)))
		{
			ubSaturation++;
		}
	}

	return __min((UINT8)3, ubSaturation);
}

// Check whether this target is directly threatening a nearby ally who needs
// covering fire.  This uses only observed combat relationships (recent attackers
// and actual fire lanes), so it does not grant the AI hidden information.
BOOLEAN AIFriendNeedsCoveringFire(SOLDIERTYPE *pSoldier, UINT8 ubOpponentID)
{
	if (!pSoldier || ubOpponentID == NOBODY || !MercPtrs[ubOpponentID])
		return FALSE;
for (UINT8 iCounter = gTacticalStatus.Team[pSoldier->bTeam].bFirstID;
		iCounter <= gTacticalStatus.Team[pSoldier->bTeam].bLastID; iCounter++)
	{
		SOLDIERTYPE *pFriend = MercPtrs[iCounter];

		if (!pFriend || pFriend == pSoldier || !pFriend->bActive || !pFriend->bInSector ||
			!AISameFireteam(pSoldier, pFriend) ||
			pFriend->stats.bLife < OKLIFE ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > DAY_VISION_RANGE / 2)
		{
			continue;
		}

		BOOLEAN fFriendInTrouble =
			pFriend->aiData.bUnderFire ||
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
		pCandidate->stats.bLife < OKLIFE || pCandidate->bCollapsed ||
		(pCandidate->usSoldierFlagMask & SOLDIER_POW) ||
		(pCandidate->flags.uiStatusFlags & SOLDIER_COWERING) ||
		pCandidate->pathing.bLevel != pRetreating->pathing.bLevel ||
		PythSpacesAway(pCandidate->sGridNo, pRetreating->sGridNo) > DAY_VISION_RANGE ||
		pCandidate->bActionPoints != pCandidate->bInitialActionPoints ||
		AIEscapeActive(pCandidate) ||
		!AICheckHasGun(pCandidate) || AIGunAmmo(pCandidate) == 0)
	{
		return FALSE;
	}

	INT32 sThreat = ClosestKnownOpponent(pCandidate, NULL, NULL);
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

	// Close candidates should not always resolve to the same rear guard. The
	// jitter is stable for this tactical turn, so repeated AI checks do not thrash.
	iScore += AIBoundedDecisionJitter(pCandidate,
		pRetreating->uiUniqueSoldierIdValue + 17u, 6);

	return iScore;
}

BOOLEAN AIShouldHoldForWithdrawingFriend(SOLDIERTYPE *pSoldier)
{
	if (!AICombatTeam(pSoldier) ||
		!pSoldier->bActive || !pSoldier->bInSector ||
		pSoldier->stats.bLife < OKLIFE ||
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
			pFriend->pathing.bLevel != pSoldier->pathing.bLevel ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > DAY_VISION_RANGE ||
			!AIRecentWithdrawal(pFriend))
		{
			continue;
		}

		INT8 bKnowledge = PersonalKnowledge(pFriend, ubOpponentID);
		if (bKnowledge != SEEN_CURRENTLY &&
			bKnowledge != SEEN_THIS_TURN &&
			bKnowledge != SEEN_LAST_TURN &&
			bKnowledge != HEARD_THIS_TURN)
		{
			continue;
		}

		INT32 sThreat = KnownPersonalLocation(pFriend, ubOpponentID);
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
			(pFriend->usSoldierFlagMask & SOLDIER_POW) ||
			(pFriend->flags.uiStatusFlags & SOLDIER_COWERING) ||
			AIDisengagementActive(pFriend) ||
			AIEscapeActive(pFriend) ||
			PythSpacesAway(pSoldier->sGridNo, pFriend->sGridNo) > DAY_VISION_RANGE)
		{
			continue;
		}

		// Require recent personal knowledge of this exact opponent. The covering
		// soldier can react to what his teammate is visibly doing, but the mover
		// must have his own recent contact rather than borrowing omniscient sector data.
		INT8 bKnowledge = PersonalKnowledge(pFriend, ubOpponentID);
		if (bKnowledge != SEEN_CURRENTLY &&
			bKnowledge != SEEN_THIS_TURN &&
			bKnowledge != SEEN_LAST_TURN &&
			bKnowledge != HEARD_THIS_TURN)
		{
			continue;
		}

		INT32 sKnownThreat = KnownPersonalLocation(pFriend, ubOpponentID);
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
		if (pFriend != pSoldier && pFriend->bActive && pFriend->stats.bLife >= OKLIFE &&
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
			pFriend->stats.bLife >= OKLIFE &&
			pFriend->aiData.bOrders > ONGUARD &&
			pFriend->aiData.bOrders != SNIPER &&
			PythSpacesAway( sGridNo, pFriend->sGridNo ) <= ubDistance &&
			pFriend->aiData.bLastAttackHit )
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

		if (*pbPersOL == SEEN_CURRENTLY)
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

		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			pSoldier->bSide == pOpponent->bSide ||
			(pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			pOpponent->ubBodyType == CROW)
		{
			continue;
		}

		const BOOLEAN fCurrentContact =
			(PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY ||
			 PublicKnowledge(pSoldier->bTeam, pOpponent->ubID) == SEEN_CURRENTLY);

		if (fCurrentContact && !ValidOpponent(pSoldier, pOpponent))
			continue;

		INT32 sThreatLoc = KnownLocation(pSoldier, pOpponent->ubID);
		INT8 bThreatLevel = KnownLevel(pSoldier, pOpponent->ubID);
		if (TileIsOutOfBounds(sThreatLoc))
			continue;

		UINT16 usAdjustedSight;
		if (fCurrentContact)
		{
			// Current sight can legitimately use the observer's actual vision state.
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
		if (fCurrentContact && gfTurnBasedAI)
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
	if ((pSoldier->aiData.bAlertStatus >= STATUS_RED ||
		 pSoldier->ubSoldierClass == SOLDIER_CLASS_ELITE) &&
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

		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			pSoldier->bSide == pOpponent->bSide ||
			(pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			pOpponent->ubBodyType == CROW)
		{
			continue;
		}

		const BOOLEAN fCurrentContact =
			(PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY ||
			 PublicKnowledge(pSoldier->bTeam, pOpponent->ubID) == SEEN_CURRENTLY);

		if (fCurrentContact &&
			(!ValidOpponent(pSoldier, pOpponent) || pOpponent->IsUnconscious() || pOpponent->IsEmptyVehicle()))
		{
			continue;
		}

		INT32 sThreatLoc = KnownLocation(pSoldier, pOpponent->ubID);
		INT8 bThreatLevel = KnownLevel(pSoldier, pOpponent->ubID);
		if (TileIsOutOfBounds(sThreatLoc))
			continue;

		UINT16 usAdjustedSight;
		if (fCurrentContact)
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
			!pFriend->bBreathCollapsed )
		{
			if (pFriend->aiData.bOppList[ pSoldier->ubID ] == SEEN_CURRENTLY ||
				pFriend->aiData.bOppList[ pSoldier->ubID ] == SEEN_THIS_TURN )
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
			pOpponent->aiData.bOppList[ pSoldier->ubID ] == SEEN_CURRENTLY )
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
	UINT32		uiTotalValue = 0;

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
		if( pOpponent->aiData.bOppList[ pSoldier->ubID ] == SEEN_CURRENTLY || 
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

			uiValue = 1 + SoldierDifficultyLevel( pOpponent );
			// add bonus for squad leader
			if (HAS_SKILL_TRAIT( pOpponent, SQUADLEADER_NT ) )
			{
				uiValue += NUM_SKILL_TRAITS( pOpponent, SQUADLEADER_NT );
			}
			// bonus when using flashlight
			if ( pSoldier->GetBestEquippedFlashLightRange() > 0 )
			{
				uiValue++;
			}
			// bonus if bleeding
			if ( pSoldier->bBleeding > 0 )
			{
				uiValue++;
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
				Item[ pSoldier->inv[HANDPOS].usItem ].dayvisionrangebonus > 0 || 
				Item[ pSoldier->inv[HANDPOS].usItem ].brightlightvisionrangebonus > 0 || 
				Item[ pSoldier->inv[HANDPOS].usItem ].nightvisionrangebonus > 0 || 
				Item[ pSoldier->inv[HANDPOS].usItem ].cavevisionrangebonus > 0 )
			{
				uiValue += 2;
			}

			// -----------------------------------------------------------------------------------------------------
			// multipliers

			// bonus if observing soldier sees more than one covert soldier
			//uiValue = uiValue * (200 - 100 / max(1, CountSeenCovertOpponents(pOpponent))) / 100;
			uiValue = uiValue * max(1, CountSeenCovertOpponents(pOpponent));

			// bonus depending on number of army men already killed
			if( gTacticalStatus.ubArmyGuysKilled > 0 )
			{
				uiValue = uiValue *(UINT32) sqrt((DOUBLE) gTacticalStatus.ubArmyGuysKilled);
			}			

			// bonus for suspicious movement mode
			if ( pSoldier->bStealthMode || 
				gAnimControl[ pSoldier->usAnimState ].ubEndHeight != ANIM_STAND ||
				pSoldier->usAnimState == RUNNING )
			{
				uiValue = uiValue * 2;
			}			

			// soldier spies cause more suspicion (this can be compensated by skill)
			if( pSoldier->usSoldierFlagMask & SOLDIER_COVERT_SOLDIER )
			{
				uiValue = uiValue * 2;
			}

			// increase if spy is civilian and alert is raised
			if( pSoldier->usSoldierFlagMask & SOLDIER_COVERT_CIV && pOpponent->aiData.bAlertStatus >= STATUS_RED )
			{
				uiValue = uiValue * 4;
			}

			// bonus if weapon raised
			if( WeaponReady(pSoldier) )
			{
				uiValue = uiValue * 2;
			}

			// bonus from uniform type (admin - 1, regular - 2, elite - 3)
			uiValue = uiValue * pSoldier->UniformLevel();

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

			uiTotalValue += uiValue;
		}
	}

	return uiTotalValue;
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
			!pFriend->bBreathCollapsed )
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
			pFriend->stats.bLife >= OKLIFE)
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

		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			pSoldier->bSide == pOpponent->bSide ||
			(pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			pOpponent->ubBodyType == CROW)
		{
			continue;
		}

		// Only a current contact may disappear because of its live engine state.
		if (bKnowledge == SEEN_CURRENTLY && !ValidOpponent(pSoldier, pOpponent))
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

BOOLEAN AICheckIsOfficer(SOLDIERTYPE *pSoldier)
{
	CHECKF(pSoldier);

	if( HAS_SKILL_TRAIT(pSoldier, SQUADLEADER_NT) )
	{
		return TRUE;
	}

	return FALSE;
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
	CHECKF(pSoldier);

	if( NUM_SKILL_TRAITS( pSoldier, SQUADLEADER_NT ) > 1 )
	{
		return TRUE;
	}

	return FALSE;
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

	// now check target state
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

	// now check target state
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
			pFriend->aiData.bOrders > ONGUARD &&
			PythSpacesAway(sGridNo, pFriend->sGridNo) <= sDistance &&
			(pFriend->LastAttackHit() != NOWHERE || pFriend->usSoldierFlagMask2 & SOLDIER_SUCCESSFUL_ATTACK || pFriend->LastTargetSuppressed()))
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

	UINT32	uiLoop;
	SOLDIERTYPE *pOpponent;

	UINT8	ubNumFriends = 0;
	UINT8	ubNumOpponents = 0;

	// loop through all soldiers in sector
	for (uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		pOpponent = MercSlots[uiLoop];

		// if this merc is inactive, at base, on assignment, dead, unconscious
		if (!pOpponent || pOpponent->stats.bLife < OKLIFE)
		{
			continue;
		}

		if (ValidOpponent(pSoldier, pOpponent))
		{
			ubNumOpponents++;
		}

		if (pOpponent->bTeam == pSoldier->bTeam || pOpponent->bSide == pSoldier->bSide)
		{
			ubNumFriends++;
		}
	}

	if (ubNumFriends > ubNumOpponents * 2)
		return TRUE;

	return FALSE;
}

BOOLEAN AICheckWeOutnumberPublic(SOLDIERTYPE *pSoldier, INT32 sSpot)
{
	CHECKF(pSoldier);
	if (TileIsOutOfBounds(sSpot))
	{
		return FALSE;
	}

	UINT8 ubFriends = CountNearbyFriends(pSoldier, sSpot, TACTICAL_RANGE);
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

	UINT8 ubFriends = CountNearbyFriends(pSoldier, pSoldier->sGridNo, TACTICAL_RANGE / 2);
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

		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			pSoldier->bSide == pOpponent->bSide ||
			(pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			pOpponent->ubBodyType == CROW)
		{
			continue;
		}

		const BOOLEAN fCurrentContact =
			(PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY ||
			 PublicKnowledge(pSoldier->bTeam, pOpponent->ubID) == SEEN_CURRENTLY);
		if (fCurrentContact && !ValidOpponent(pSoldier, pOpponent))
			continue;

		INT32 sThreatLoc = KnownLocation(pSoldier, pOpponent->ubID);
		INT8 bThreatLevel = KnownLevel(pSoldier, pOpponent->ubID);
		if (TileIsOutOfBounds(sThreatLoc))
			continue;

		INT32 iVisibilityRange;
		if (fCurrentContact)
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
		if (fCurrentContact && gfTurnBasedAI)
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

	UINT32		uiLoop;
	SOLDIERTYPE *pOpponent;
	INT8		bMaxInterruptLevel = 0;
	INT8		bInterruptLevel;

	INT32		sThreatLoc;
	INT8		iThreatLevel;

	UINT8 ubNum = 0;

	// loop through all the enemies
	for (uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		pOpponent = MercSlots[ uiLoop ];

		// if this merc is inactive, at base, on assignment, dead, unconscious
		if (!pOpponent || pOpponent->stats.bLife < OKLIFE)
		{
			continue;
		}

		// if this man is neutral / on the same side, he's not an opponent
		if( CONSIDERED_NEUTRAL( pSoldier, pOpponent ) || (pSoldier->bSide == pOpponent->bSide))
		{
			continue;
		}

		// check if he is captured
		if(pOpponent->usSoldierFlagMask & SOLDIER_POW)
		{
			continue;
		}

		// check personal/public knowledge
		if( pSoldier->aiData.bOppList[pOpponent->ubID] == NOT_HEARD_OR_SEEN &&
			gbPublicOpplist[pSoldier->bTeam][pOpponent->ubID] == NOT_HEARD_OR_SEEN )
		{
			continue;
		}

		sThreatLoc = gsPublicLastKnownOppLoc[pSoldier->bTeam][pOpponent->ubID];
		iThreatLevel = gbPublicLastKnownOppLevel[pSoldier->bTeam][pOpponent->ubID];

		// use personal knowledge if possible
		if( pSoldier->aiData.bOppList[pOpponent->ubID] != NOT_HEARD_OR_SEEN )
		{
			sThreatLoc = gsLastKnownOppLoc[pSoldier->ubID][pOpponent->ubID];
			iThreatLevel = gbLastKnownOppLevel[pSoldier->ubID][pOpponent->ubID];
		}

		// if for some reason known location is bad - skip
		if( TileIsOutOfBounds(sThreatLoc) )
		{
			continue;
		}

		// check distance
		if( PythSpacesAway(sThreatLoc, sGridNo ) > ubDistance )
		{
			continue;
		}

		// check level
		if( iThreatLevel != blevel )
		{
			continue;
		}

		bInterruptLevel = AIEstimateInterruptLevel(pOpponent);
		if( bInterruptLevel > bMaxInterruptLevel )
		{
			bMaxInterruptLevel = bInterruptLevel;
		}
	}

	return bMaxInterruptLevel;
}

UINT8 CountPublicKnownEnemies( SOLDIERTYPE *pSoldier, INT32 sGridNo, UINT8 ubDistance )
{
	CHECKF(pSoldier);

	UINT32		uiLoop;
	SOLDIERTYPE *pOpponent;

	INT32		sThreatLoc;
	INT8		iThreatLevel;

	UINT8 ubNum = 0;

	// loop through all the enemies
	for (uiLoop = 0; uiLoop < guiNumMercSlots; ++uiLoop)
	{
		pOpponent = MercSlots[ uiLoop ];

		// if this merc is inactive, at base, on assignment, dead, unconscious
		if (!pOpponent || pOpponent->stats.bLife < OKLIFE)
		{
			continue;
		}

		// if this man is neutral / on the same side, he's not an opponent
		if( CONSIDERED_NEUTRAL( pSoldier, pOpponent ) || (pSoldier->bSide == pOpponent->bSide))
		{
			continue;
		}

		// check if he is captured
		if(pOpponent->usSoldierFlagMask & SOLDIER_POW)
		{
			continue;
		}

		sThreatLoc = gsPublicLastKnownOppLoc[pSoldier->bTeam][pOpponent->ubID];
		iThreatLevel = gbPublicLastKnownOppLevel[pSoldier->bTeam][pOpponent->ubID];

		// check distance
		if( PythSpacesAway(sThreatLoc, sGridNo ) > ubDistance )
		{
			continue;
		}

		// check public knowledge
		if( gbPublicOpplist[pSoldier->bTeam][pOpponent->ubID] != NOT_HEARD_OR_SEEN )
		{
			ubNum ++;
		}
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
				if( pSoldier->aiData.bOppList[ubIDLoop] == SEEN_CURRENTLY &&
					MercPtrs[ubIDLoop]->usSoldierFlagMask & (SOLDIER_COVERT_CIV|SOLDIER_COVERT_SOLDIER) )
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

		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) || pSoldier->bSide == pOpponent->bSide ||
			(pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			pOpponent->ubBodyType == CROW)
		{
			continue;
		}

		if (bKnowledge == SEEN_CURRENTLY && !ValidOpponent(pSoldier, pOpponent))
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
		(fFlankingFriends || !fSuccessfulAttack || !fSeekEnemy || EnemyCanAttackSpot(pSoldier, sSpot, bLevel) || InARoom(sSpot, NULL) && bLevel == 0 || AICorpseWarningKnown(pSoldier, sSpot, bLevel)))
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
			(fFlankingFriends || !fSuccessfulAttack || !fSeekEnemy || EnemyCanAttackSpot(pSoldier, sCheckGridNo, bLevel) || InARoom(sCheckGridNo, NULL) && bLevel == 0 || AICorpseWarningKnown(pSoldier, sCheckGridNo, bLevel)))
		{
			DebugAI(AI_MSG_INFO, pSoldier, String("fresh corpse! abort!"));

			if (!SightCoverAtSpot(pSoldier, sCheckGridNo, TRUE) || InARoom(sCheckGridNo, NULL) && bLevel == 0 || AICorpseWarningKnown(pSoldier, sCheckGridNo, bLevel))
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
	if (pSoldier->bTeam != ENEMY_TEAM)
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
			pFriend->stats.bLife >= OKLIFE &&
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

	if (pSoldier->bTeam != ENEMY_TEAM)
	{
		return FALSE;
	}

	if (pSoldier->aiData.bOrders == STATIONARY)
	{
		return FALSE;
	}

	switch (pSoldier->ubSoldierClass)
	{
	case SOLDIER_CLASS_ELITE:
		return TRUE;
		break;
	case SOLDIER_CLASS_ARMY:
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

		if (CONSIDERED_NEUTRAL(pSoldier, pOpponent) ||
			pSoldier->bSide == pOpponent->bSide ||
			(pSoldier->aiData.bAttitude == ATTACKSLAYONLY && pOpponent->ubProfile != SLAY) ||
			pOpponent->ubBodyType == CROW)
		{
			continue;
		}

		INT32 sThreatLoc = KnownLocation(pSoldier, pOpponent->ubID);
		INT8 bThreatLevel = KnownLevel(pSoldier, pOpponent->ubID);
		if (TileIsOutOfBounds(sThreatLoc))
			continue;

		const BOOLEAN fCurrentContact =
			(PersonalKnowledge(pSoldier, pOpponent->ubID) == SEEN_CURRENTLY ||
			 PublicKnowledge(pSoldier->bTeam, pOpponent->ubID) == SEEN_CURRENTLY);

		INT32 iAttackRange;
		if (fCurrentContact)
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