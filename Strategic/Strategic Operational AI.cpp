#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Strategic Operational AI.h"
#include "Strategic Movement.h"
#include "Strategic AI.h"
#include "Strategic Mines.h"
#include "Campaign Types.h"
#include "Game Clock.h"
#include "Queen Command.h"
#include "strategic.h"
#include "strategicmap.h"

extern ARMY_COMPOSITION gArmyComp[ MAX_ARMY_COMPOSITIONS ];
extern GARRISON_GROUP *gGarrisonGroup;
extern INT32 giGarrisonArraySize;
extern UINT8 NUM_ARMY_COMPOSITIONS;

#define VR_OPERATIONAL_MAGIC0 'O'
#define VR_OPERATIONAL_MAGIC1 'P'
#define VR_OPERATIONAL_MAGIC2 'S'

static UINT8 VR_ClampByte( INT32 value )
{
	if( value < 0 )
		return 0;
	if( value > 100 )
		return 100;
	return (UINT8)value;
}

static INT32 VR_Abs( INT32 value )
{
	return value < 0 ? -value : value;
}

static BOOLEAN VR_IsEnemyFormation( GROUP *pGroup )
{
	return pGroup && !pGroup->fPlayer && pGroup->pEnemyGroup;
}

static BOOLEAN VR_FormationStateIsInitialized( GROUP *pGroup )
{
	if( !VR_IsEnemyFormation( pGroup ) )
		return FALSE;

	return pGroup->pEnemyGroup->ubOperationalMagic0 == VR_OPERATIONAL_MAGIC0 &&
		pGroup->pEnemyGroup->ubOperationalMagic1 == VR_OPERATIONAL_MAGIC1 &&
		pGroup->pEnemyGroup->ubOperationalMagic2 == VR_OPERATIONAL_MAGIC2;
}

static UINT16 VR_CreateFormationID( GROUP *pGroup )
{
	UINT32 seed = GetWorldTotalMin();
	seed = (seed * 257U) ^ (UINT32)pGroup->ubGroupID ^ ((UINT32)pGroup->ubOriginalSector << 8);
	UINT16 id = (UINT16)( seed & 0xffffU );
	return id ? id : (UINT16)( pGroup->ubGroupID ? pGroup->ubGroupID : 1 );
}

static UINT8 VR_MissionFromLegacyIntention( UINT8 ubLegacyIntention )
{
	switch( ubLegacyIntention )
	{
		case PURSUIT:
			return VR_OPMISSION_INTERCEPT;
		case STAGING:
			return VR_OPMISSION_ATTACK;
		case PATROL:
			return VR_OPMISSION_PATROL;
		case REINFORCEMENTS:
			return VR_OPMISSION_REINFORCE;
		case ASSAULT:
			return VR_OPMISSION_ATTACK;
		case NO_INTENTIONS:
		default:
			return VR_OPMISSION_NONE;
	}
}

const CHAR8 *VR_OperationalMissionName( UINT8 ubMission )
{
	switch( ubMission )
	{
		case VR_OPMISSION_GARRISON: return "GARRISON";
		case VR_OPMISSION_PATROL: return "PATROL";
		case VR_OPMISSION_RECON: return "RECON";
		case VR_OPMISSION_ATTACK: return "ATTACK";
		case VR_OPMISSION_RAID: return "RAID";
		case VR_OPMISSION_REINFORCE: return "REINFORCE";
		case VR_OPMISSION_RELIEVE: return "RELIEVE";
		case VR_OPMISSION_INTERCEPT: return "INTERCEPT";
		case VR_OPMISSION_BLOCK_ROAD: return "BLOCK_ROAD";
		case VR_OPMISSION_ESCORT: return "ESCORT";
		case VR_OPMISSION_SUPPLY: return "SUPPLY";
		case VR_OPMISSION_RETREAT: return "RETREAT";
		case VR_OPMISSION_REGROUP: return "REGROUP";
		case VR_OPMISSION_RESERVE: return "RESERVE";
		case VR_OPMISSION_NONE:
		default: return "NONE";
	}
}

const CHAR8 *VR_OperationalReserveRoleName( UINT8 ubRole )
{
	switch( ubRole )
	{
		case VR_RESERVE_LOCAL: return "LOCAL";
		case VR_RESERVE_REGIONAL: return "REGIONAL";
		case VR_RESERVE_CENTRAL: return "CENTRAL";
		case VR_RESERVE_NONE:
		default: return "NONE";
	}
}

const CHAR8 *VR_OperationalReasonName( UINT8 ubReason )
{
	switch( ubReason )
	{
		case VR_OPREASON_FORMATION_CREATED: return "FORMATION_CREATED";
		case VR_OPREASON_LEGACY_ASSIGNMENT: return "LEGACY_ASSIGNMENT";
		case VR_OPREASON_TARGET_SCORE: return "TARGET_SCORE";
		case VR_OPREASON_ARRIVAL: return "ARRIVAL";
		case VR_OPREASON_RETREAT: return "RETREAT";
		case VR_OPREASON_REGROUP: return "REGROUP";
		case VR_OPREASON_LOW_SUPPLY: return "LOW_SUPPLY";
		case VR_OPREASON_RESUPPLIED: return "RESUPPLIED";
		case VR_OPREASON_CONTACT: return "CONTACT";
		case VR_OPREASON_INTEL_DECAY: return "INTEL_DECAY";
		case VR_OPREASON_NONE:
		default: return "NONE";
	}
}

void VR_LogOperationalDecision( GROUP *pGroup, const CHAR8 *szEvent, const VR_OPERATIONAL_SCORE *pScore )
{
	if( !VR_IsEnemyFormation( pGroup ) || !VR_FormationStateIsInitialized( pGroup ) )
		return;

	FILE *pFile = fopen( "Strategic Operational BlackBox.txt", "a" );
	if( !pFile )
		return;

	UINT32 totalMinutes = GetWorldTotalMin();
	UINT32 day = totalMinutes / 1440U + 1U;
	UINT32 hour = ( totalMinutes / 60U ) % 24U;
	UINT32 minute = totalMinutes % 60U;

	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
	fprintf( pFile,
		"[D%u %02u:%02u] F=%u G=%u EVENT=%s MISSION=%s RESERVE=%s POS=%c%d TARGET=%c%d HOME=%c%d SIZE=%u SUP=%u MORALE=%u INTEL=%u RETREATS=%u REASON=%s",
		(unsigned)day, (unsigned)hour, (unsigned)minute,
		(unsigned)pEnemy->usFormationID, (unsigned)pGroup->ubGroupID,
		szEvent ? szEvent : "UNKNOWN",
		VR_OperationalMissionName( pEnemy->ubOperationalMission ),
		VR_OperationalReserveRoleName( pEnemy->ubOperationalReserveRole ),
		pGroup->ubSectorY + 'A' - 1, pGroup->ubSectorX,
		SECTORY( pEnemy->ubOperationalTargetSectorID ) + 'A' - 1, SECTORX( pEnemy->ubOperationalTargetSectorID ),
		SECTORY( pEnemy->ubOperationalHomeSectorID ) + 'A' - 1, SECTORX( pEnemy->ubOperationalHomeSectorID ),
		(unsigned)pGroup->ubGroupSize,
		(unsigned)pEnemy->ubOperationalSupply,
		(unsigned)pEnemy->ubOperationalMorale,
		(unsigned)pEnemy->ubOperationalIntelConfidence,
		(unsigned)pEnemy->ubOperationalRetreatCount,
		VR_OperationalReasonName( pEnemy->ubOperationalLastDecisionReason ) );

	if( pScore )
	{
		fprintf( pFile,
			" SCORE=%ld [base=%ld ownership=%ld town=%ld mine=%ld sam=%ld mercRisk=%ld militiaRisk=%ld distance=%ld supplyRisk=%ld]",
			(long)pScore->iTotal,
			(long)pScore->iBasePriority,
			(long)pScore->iOwnershipValue,
			(long)pScore->iTownValue,
			(long)pScore->iMineValue,
			(long)pScore->iSAMValue,
			(long)pScore->iPlayerForceRisk,
			(long)pScore->iMilitiaRisk,
			(long)pScore->iDistanceCost,
			(long)pScore->iSupplyRisk );
	}

	fprintf( pFile, "\n" );
	fclose( pFile );
}

void VR_EnsureEnemyFormationState( GROUP *pGroup )
{
	if( !VR_IsEnemyFormation( pGroup ) || VR_FormationStateIsInitialized( pGroup ) )
		return;

	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
	pEnemy->usFormationID = VR_CreateFormationID( pGroup );
	pEnemy->ubOperationalMagic0 = VR_OPERATIONAL_MAGIC0;
	pEnemy->ubOperationalMagic1 = VR_OPERATIONAL_MAGIC1;
	pEnemy->ubOperationalMagic2 = VR_OPERATIONAL_MAGIC2;
	pEnemy->ubOperationalMission = VR_MissionFromLegacyIntention( pEnemy->ubIntention );
	pEnemy->ubOperationalReserveRole = VR_RESERVE_NONE;
	pEnemy->ubOperationalSupply = 100;
	pEnemy->ubOperationalMorale = 75;
	pEnemy->ubOperationalIntelConfidence = 20;
	pEnemy->ubOperationalTargetSectorID = (UINT8)SECTOR( pGroup->ubSectorX, pGroup->ubSectorY );
	pEnemy->ubOperationalHomeSectorID = pGroup->ubOriginalSector;
	pEnemy->ubOperationalLastKnownPlayerSectorID = 0xff;
	pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_FORMATION_CREATED;
	pEnemy->ubOperationalRetreatCount = 0;
	pEnemy->usOperationalFlags = 0;
	pEnemy->usOperationalDecisionStamp = (UINT16)( GetWorldTotalMin() & 0xffffU );

	VR_LogOperationalDecision( pGroup, "CREATE", NULL );
}

void VR_EnsureAllEnemyFormationStates()
{
	GROUP *pGroup = gpGroupList;
	while( pGroup )
	{
		if( !pGroup->fPlayer && pGroup->pEnemyGroup )
			VR_EnsureEnemyFormationState( pGroup );
		pGroup = pGroup->next;
	}
}

void VR_SetFormationMission( GROUP *pGroup, UINT8 ubMission, UINT8 ubReason )
{
	VR_EnsureEnemyFormationState( pGroup );
	if( !VR_FormationStateIsInitialized( pGroup ) )
		return;

	pGroup->pEnemyGroup->ubOperationalMission = ubMission;
	pGroup->pEnemyGroup->ubOperationalLastDecisionReason = ubReason;
	pGroup->pEnemyGroup->usOperationalDecisionStamp = (UINT16)( GetWorldTotalMin() & 0xffffU );
}

void VR_SetFormationReserveRole( GROUP *pGroup, UINT8 ubReserveRole, UINT8 ubReason )
{
	VR_EnsureEnemyFormationState( pGroup );
	if( !VR_FormationStateIsInitialized( pGroup ) )
		return;

	pGroup->pEnemyGroup->ubOperationalReserveRole = ubReserveRole;
	pGroup->pEnemyGroup->ubOperationalLastDecisionReason = ubReason;
	pGroup->pEnemyGroup->usOperationalDecisionStamp = (UINT16)( GetWorldTotalMin() & 0xffffU );
}

INT32 VR_ScoreOperationalTarget( GROUP *pGroup, UINT8 ubSectorID, VR_OPERATIONAL_SCORE *pBreakdown )
{
	VR_OPERATIONAL_SCORE score;
	memset( &score, 0, sizeof( score ) );

	UINT8 x = (UINT8)SECTORX( ubSectorID );
	UINT8 y = (UINT8)SECTORY( ubSectorID );
	SECTORINFO *pSector = &SectorInfo[ ubSectorID ];

	if( pSector->ubGarrisonID != NO_GARRISON && pSector->ubGarrisonID < giGarrisonArraySize )
	{
		UINT8 composition = gGarrisonGroup[ pSector->ubGarrisonID ].ubComposition;
		if( composition < NUM_ARMY_COMPOSITIONS )
			score.iBasePriority = gArmyComp[ composition ].bPriority;
	}

	if( StrategicMap[ CALCULATE_STRATEGIC_INDEX( x, y ) ].fEnemyControlled == FALSE )
		score.iOwnershipValue = 30;

	if( pSector->ubTraversability[ 4 ] == TOWN )
		score.iTownValue = 15;

	if( IsThereAMineInThisSector( x, y ) )
		score.iMineValue = 25;

	if( IsThisSectorASAMSector( x, y, 0 ) )
		score.iSAMValue = 20;

	score.iPlayerForceRisk = -(INT32)PlayerMercsInSector( x, y, 0 ) * 8;
	score.iMilitiaRisk = -(INT32)(
		pSector->ubNumberOfCivsAtLevel[ GREEN_MILITIA ] +
		pSector->ubNumberOfCivsAtLevel[ REGULAR_MILITIA ] * 2 +
		pSector->ubNumberOfCivsAtLevel[ ELITE_MILITIA ] * 3 );

	if( pGroup )
	{
		INT32 distance = VR_Abs( (INT32)pGroup->ubSectorX - x ) + VR_Abs( (INT32)pGroup->ubSectorY - y );
		score.iDistanceCost = -( distance * 3 );

		if( pGroup->pEnemyGroup )
		{
			VR_EnsureEnemyFormationState( pGroup );
			if( pGroup->pEnemyGroup->ubOperationalSupply < 50 )
				score.iSupplyRisk = -( 50 - pGroup->pEnemyGroup->ubOperationalSupply );
		}
	}

	score.iTotal = score.iBasePriority + score.iOwnershipValue + score.iTownValue +
		score.iMineValue + score.iSAMValue + score.iPlayerForceRisk +
		score.iMilitiaRisk + score.iDistanceCost + score.iSupplyRisk;

	if( pBreakdown )
		*pBreakdown = score;

	return score.iTotal;
}

UINT8 VR_FindBestOperationalTarget( GROUP *pGroup, INT32 *piBestScore )
{
	UINT8 bestSector = (UINT8)SECTOR( pGroup ? pGroup->ubSectorX : 1, pGroup ? pGroup->ubSectorY : 1 );
	INT32 bestScore = -32767;

	for( INT32 sector = 0; sector < 256; ++sector )
	{
		VR_OPERATIONAL_SCORE score;
		INT32 value = VR_ScoreOperationalTarget( pGroup, (UINT8)sector, &score );

		// Operational targets must either matter to the Queen or be held by the player.
		if( score.iBasePriority <= 0 && score.iOwnershipValue <= 0 )
			continue;

		if( value > bestScore )
		{
			bestScore = value;
			bestSector = (UINT8)sector;
		}
	}

	if( piBestScore )
		*piBestScore = bestScore;

	return bestSector;
}

void VR_OnEnemyGroupAssigned( GROUP *pGroup, UINT8 ubTargetSectorID, UINT8 ubLegacyIntention )
{
	VR_EnsureEnemyFormationState( pGroup );
	if( !VR_FormationStateIsInitialized( pGroup ) )
		return;

	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
	pEnemy->ubOperationalTargetSectorID = ubTargetSectorID;
	pEnemy->ubOperationalMission = VR_MissionFromLegacyIntention( ubLegacyIntention );
	pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_LEGACY_ASSIGNMENT;
	pEnemy->usOperationalDecisionStamp = (UINT16)( GetWorldTotalMin() & 0xffffU );

	if( pEnemy->ubOperationalMission == VR_OPMISSION_REINFORCE )
	{
		INT32 distance = VR_Abs( (INT32)pGroup->ubSectorX - SECTORX( ubTargetSectorID ) ) +
			VR_Abs( (INT32)pGroup->ubSectorY - SECTORY( ubTargetSectorID ) );

		if( distance <= 2 )
			pEnemy->ubOperationalReserveRole = VR_RESERVE_LOCAL;
		else if( distance <= 5 )
			pEnemy->ubOperationalReserveRole = VR_RESERVE_REGIONAL;
		else
			pEnemy->ubOperationalReserveRole = VR_RESERVE_CENTRAL;
	}
	else
	{
		pEnemy->ubOperationalReserveRole = VR_RESERVE_NONE;
	}

	VR_OPERATIONAL_SCORE score;
	VR_ScoreOperationalTarget( pGroup, ubTargetSectorID, &score );
	VR_LogOperationalDecision( pGroup, "ASSIGN", &score );

	INT32 bestScore = 0;
	UINT8 bestTarget = VR_FindBestOperationalTarget( pGroup, &bestScore );
	if( bestTarget != ubTargetSectorID && bestScore > score.iTotal )
	{
		VR_OPERATIONAL_SCORE bestBreakdown;
		VR_ScoreOperationalTarget( pGroup, bestTarget, &bestBreakdown );

		UINT8 originalTarget = pEnemy->ubOperationalTargetSectorID;
		pEnemy->ubOperationalTargetSectorID = bestTarget;
		pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_TARGET_SCORE;
		VR_LogOperationalDecision( pGroup, "BETTER_TARGET_AVAILABLE", &bestBreakdown );
		pEnemy->ubOperationalTargetSectorID = originalTarget;
		pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_LEGACY_ASSIGNMENT;
	}
}

void VR_OnEnemyGroupArrived( GROUP *pGroup )
{
	VR_EnsureEnemyFormationState( pGroup );
	if( !VR_FormationStateIsInitialized( pGroup ) )
		return;

	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
	UINT8 currentSector = (UINT8)SECTOR( pGroup->ubSectorX, pGroup->ubSectorY );

	if( PlayerMercsInSector( pGroup->ubSectorX, pGroup->ubSectorY, 0 ) > 0 )
	{
		pEnemy->ubOperationalLastKnownPlayerSectorID = currentSector;
		pEnemy->ubOperationalIntelConfidence = 100;
		pEnemy->usOperationalFlags |= VR_OPFLAG_RECENT_CONTACT;
		pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_CONTACT;
	}

	if( pEnemy->ubOperationalMission == VR_OPMISSION_RETREAT )
	{
		pEnemy->ubOperationalMission = VR_OPMISSION_REGROUP;
		pEnemy->ubOperationalReserveRole = VR_RESERVE_LOCAL;
		pEnemy->usOperationalFlags |= VR_OPFLAG_REGROUPING;
		pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_REGROUP;
	}
	else
	{
		pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_ARRIVAL;
	}

	if( currentSector == pEnemy->ubOperationalHomeSectorID )
		pEnemy->ubOperationalSupply = VR_ClampByte( pEnemy->ubOperationalSupply + 10 );

	pEnemy->usOperationalDecisionStamp = (UINT16)( GetWorldTotalMin() & 0xffffU );
	VR_LogOperationalDecision( pGroup, "ARRIVE", NULL );
}

void VR_OnEnemyGroupRetreated( GROUP *pGroup )
{
	VR_EnsureEnemyFormationState( pGroup );
	if( !VR_FormationStateIsInitialized( pGroup ) )
		return;

	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
	if( pEnemy->ubOperationalRetreatCount < 255 )
		++pEnemy->ubOperationalRetreatCount;

	pEnemy->usOperationalFlags |= VR_OPFLAG_RETREATED_ONCE;
	pEnemy->ubOperationalMission = VR_OPMISSION_RETREAT;
	pEnemy->ubOperationalReserveRole = VR_RESERVE_NONE;
	pEnemy->ubOperationalMorale = VR_ClampByte( pEnemy->ubOperationalMorale - 12 );
	pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_RETREAT;
	pEnemy->usOperationalDecisionStamp = (UINT16)( GetWorldTotalMin() & 0xffffU );

	VR_LogOperationalDecision( pGroup, "RETREAT", NULL );
}

void VR_HourlyOperationalUpdate()
{
	GROUP *pGroup = gpGroupList;

	while( pGroup )
	{
		if( !pGroup->fPlayer && pGroup->pEnemyGroup )
		{
			VR_EnsureEnemyFormationState( pGroup );
			ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;

			// Keep persistent mission state aligned with legacy VR intentions while migration is incremental.
			if( pEnemy->ubOperationalMission == VR_OPMISSION_NONE && pEnemy->ubIntention != NO_INTENTIONS )
				pEnemy->ubOperationalMission = VR_MissionFromLegacyIntention( pEnemy->ubIntention );

			if( pEnemy->ubOperationalIntelConfidence > 0 )
			{
				UINT8 decay = ( pEnemy->usOperationalFlags & VR_OPFLAG_RECENT_CONTACT ) ? 1 : 3;
				pEnemy->ubOperationalIntelConfidence =
					( pEnemy->ubOperationalIntelConfidence > decay ) ?
					pEnemy->ubOperationalIntelConfidence - decay : 0;

				if( pEnemy->ubOperationalIntelConfidence < 60 )
					pEnemy->usOperationalFlags &= ~VR_OPFLAG_RECENT_CONTACT;
			}

			UINT8 currentSector = (UINT8)SECTOR( pGroup->ubSectorX, pGroup->ubSectorY );
			BOOLEAN fFriendlySector = StrategicMap[ CALCULATE_STRATEGIC_INDEX( pGroup->ubSectorX, pGroup->ubSectorY ) ].fEnemyControlled;

			if( pGroup->fBetweenSectors )
			{
				pEnemy->ubOperationalSupply = VR_ClampByte( pEnemy->ubOperationalSupply - 1 );
			}
			else if( currentSector == pEnemy->ubOperationalHomeSectorID )
			{
				UINT8 oldSupply = pEnemy->ubOperationalSupply;
				pEnemy->ubOperationalSupply = VR_ClampByte( pEnemy->ubOperationalSupply + 5 );
				if( oldSupply < 50 && pEnemy->ubOperationalSupply >= 50 )
				{
					pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_RESUPPLIED;
					VR_LogOperationalDecision( pGroup, "RESUPPLIED", NULL );
				}
			}
			else if( fFriendlySector &&
				( SectorInfo[ currentSector ].ubGarrisonID != NO_GARRISON ||
				  SectorInfo[ currentSector ].ubTraversability[ 4 ] == TOWN ||
				  IsThereAMineInThisSector( pGroup->ubSectorX, pGroup->ubSectorY ) ||
				  IsThisSectorASAMSector( pGroup->ubSectorX, pGroup->ubSectorY, 0 ) ) )
			{
				pEnemy->ubOperationalSupply = VR_ClampByte( pEnemy->ubOperationalSupply + 2 );
			}

			pEnemy->usOperationalFlags &= ~( VR_OPFLAG_SUPPLY_LOW | VR_OPFLAG_SUPPLY_CRITICAL );
			if( pEnemy->ubOperationalSupply < 40 )
				pEnemy->usOperationalFlags |= VR_OPFLAG_SUPPLY_LOW;
			if( pEnemy->ubOperationalSupply < 20 )
			{
				pEnemy->usOperationalFlags |= VR_OPFLAG_SUPPLY_CRITICAL;
				pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_LOW_SUPPLY;
			}

			if( pEnemy->ubOperationalSupply < 25 )
				pEnemy->ubOperationalMorale = VR_ClampByte( pEnemy->ubOperationalMorale - 2 );
			else if( pEnemy->ubOperationalSupply > 60 && pEnemy->ubOperationalMorale < 85 )
				pEnemy->ubOperationalMorale = VR_ClampByte( pEnemy->ubOperationalMorale + 1 );

			if( pEnemy->ubOperationalMission == VR_OPMISSION_REGROUP &&
				pEnemy->ubOperationalSupply >= 70 && pEnemy->ubOperationalMorale >= 65 )
			{
				pEnemy->ubOperationalMission = VR_OPMISSION_RESERVE;
				pEnemy->ubOperationalReserveRole = VR_RESERVE_LOCAL;
				pEnemy->usOperationalFlags &= ~VR_OPFLAG_REGROUPING;
				pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_REGROUP;
				VR_LogOperationalDecision( pGroup, "REGROUP_COMPLETE", NULL );
			}
		}

		pGroup = pGroup->next;
	}
}
