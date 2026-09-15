#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Strategic Operational AI.h"
#include "Campaign Strategic Telemetry.h"
#include "Strategic Movement.h"
#include "Strategic AI.h"
#include "Strategic Mines.h"
#include "Campaign Types.h"
#include "Game Clock.h"
#include "GameSettings.h"
#include "Queen Command.h"
#include "strategic.h"
#include "strategicmap.h"
#include "random.h"
#include "VRAnalytics.h"

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

static UINT16 VR_ReadFormationID( const ENEMYGROUP *pEnemy )
{
	if( !pEnemy )
		return 0;
	return (UINT16)pEnemy->ubFormationIDLo |
		((UINT16)pEnemy->ubFormationIDHi << 8);
}

static void VR_WriteFormationID( ENEMYGROUP *pEnemy, UINT16 usValue )
{
	if( !pEnemy )
		return;
	pEnemy->ubFormationIDLo = (UINT8)( usValue & 0xff );
	pEnemy->ubFormationIDHi = (UINT8)( ( usValue >> 8 ) & 0xff );
}

static UINT16 VR_ReadOperationalFlags( const ENEMYGROUP *pEnemy )
{
	if( !pEnemy )
		return 0;
	return (UINT16)pEnemy->ubOperationalFlagsLo |
		((UINT16)pEnemy->ubOperationalFlagsHi << 8);
}

static void VR_WriteOperationalFlags( ENEMYGROUP *pEnemy, UINT16 usValue )
{
	if( !pEnemy )
		return;
	pEnemy->ubOperationalFlagsLo = (UINT8)( usValue & 0xff );
	pEnemy->ubOperationalFlagsHi = (UINT8)( ( usValue >> 8 ) & 0xff );
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

	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
	const CHAR8 *szDecision = szEvent ? szEvent : "OPERATIONAL";
	unsigned long uiDecisionID = VRAnalyticsBeginDecision(
		VR_ANALYTICS_STRATEGIC,
		"formation",
		(unsigned int)pGroup->ubGroupID,
		szDecision );

	VRAnalyticsStateInt( uiDecisionID, "formation_id", (long)VR_ReadFormationID( pEnemy ) );
	VRAnalyticsStateInt( uiDecisionID, "sector", (long)SECTOR( pGroup->ubSectorX, pGroup->ubSectorY ) );
	VRAnalyticsStateInt( uiDecisionID, "target_sector", (long)pEnemy->ubOperationalTargetSectorID );
	VRAnalyticsStateInt( uiDecisionID, "home_sector", (long)pEnemy->ubOperationalHomeSectorID );
	VRAnalyticsStateInt( uiDecisionID, "group_size", (long)pGroup->ubGroupSize );
	VRAnalyticsStateInt( uiDecisionID, "supply", (long)pEnemy->ubOperationalSupply );
	VRAnalyticsStateInt( uiDecisionID, "morale", (long)pEnemy->ubOperationalMorale );
	VRAnalyticsStateInt( uiDecisionID, "intel_confidence", (long)pEnemy->ubOperationalIntelConfidence );
	VRAnalyticsStateInt( uiDecisionID, "operational_flags", (long)VR_ReadOperationalFlags( pEnemy ) );

	VRAnalyticsCommitDecision(
		uiDecisionID,
		VR_OperationalMissionName( pEnemy->ubOperationalMission ),
		(long)pEnemy->ubOperationalTargetSectorID,
		pScore ? (long)pScore->iTotal : 0L,
		VR_OperationalReasonName( pEnemy->ubOperationalLastDecisionReason ) );
}

void VR_EnsureEnemyFormationState( GROUP *pGroup )
{
	if( !VR_IsEnemyFormation( pGroup ) || VR_FormationStateIsInitialized( pGroup ) )
		return;

	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
	VR_WriteFormationID( pEnemy, VR_CreateFormationID( pGroup ) );
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
	VR_WriteOperationalFlags( pEnemy, 0 );
	pEnemy->ubOperationalLastKnownPlayerStrength = 0;
	pEnemy->ubOperationalLastKnownMilitiaStrength = 0;

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
}

void VR_SetFormationReserveRole( GROUP *pGroup, UINT8 ubReserveRole, UINT8 ubReason )
{
	VR_EnsureEnemyFormationState( pGroup );
	if( !VR_FormationStateIsInitialized( pGroup ) )
		return;

	pGroup->pEnemyGroup->ubOperationalReserveRole = ubReserveRole;
	pGroup->pEnemyGroup->ubOperationalLastDecisionReason = ubReason;
}

BOOLEAN VR_HoldFormationAsReserve( GROUP *pGroup, UINT8 ubReserveRole )
{
	if( !VR_OPERATIONAL_PERSISTENT_RESERVES_ENABLED || !VR_IsEnemyFormation( pGroup ) )
		return FALSE;

	VR_EnsureEnemyFormationState( pGroup );
	if( !VR_FormationStateIsInitialized( pGroup ) )
		return FALSE;

	RemovePGroupWaypoints( pGroup );
	pGroup->ubMoveType = ONE_WAY;
	pGroup->pEnemyGroup->ubIntention = NO_INTENTIONS;
	pGroup->pEnemyGroup->ubOperationalMission = VR_OPMISSION_RESERVE;
	pGroup->pEnemyGroup->ubOperationalReserveRole = ubReserveRole;
	pGroup->pEnemyGroup->ubOperationalTargetSectorID = (UINT8)SECTOR( pGroup->ubSectorX, pGroup->ubSectorY );
	pGroup->pEnemyGroup->ubOperationalLastDecisionReason = VR_OPREASON_REGROUP;
	VR_WriteOperationalFlags( pGroup->pEnemyGroup, (UINT16)( VR_ReadOperationalFlags( pGroup->pEnemyGroup ) & ~VR_OPFLAG_REGROUPING ) );

	VR_LogOperationalDecision( pGroup, "RESERVE_HOLD", NULL );
	return TRUE;
}

BOOLEAN VR_IsReadyOperationalReserve( GROUP *pGroup )
{
	if( !VR_OPERATIONAL_PERSISTENT_RESERVES_ENABLED || !VR_IsEnemyFormation( pGroup ) )
		return FALSE;

	VR_EnsureEnemyFormationState( pGroup );
	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;

	return pEnemy->ubOperationalMission == VR_OPMISSION_RESERVE &&
		!pGroup->fBetweenSectors &&
		pGroup->ubGroupSize > 0 &&
		pEnemy->ubOperationalSupply >= 50 &&
		pEnemy->ubOperationalMorale >= 50;
}

GROUP *VR_FindReadyOperationalReserve()
{
	GROUP *pBest = NULL;
	INT32 iBestReadiness = -1;

	GROUP *pGroup = gpGroupList;
	while( pGroup )
	{
		if( VR_IsReadyOperationalReserve( pGroup ) )
		{
			ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
			INT32 iReadiness =
				(INT32)pEnemy->ubOperationalSupply +
				(INT32)pEnemy->ubOperationalMorale +
				(INT32)pGroup->ubGroupSize * 3;

			if( iReadiness > iBestReadiness )
			{
				iBestReadiness = iReadiness;
				pBest = pGroup;
			}
		}

		pGroup = pGroup->next;
	}

	return pBest;
}

static UINT8 VR_EstimateStrengthWithConfidence( INT32 iObservedStrength, UINT8 ubConfidence )
{
	INT32 iEstimate = iObservedStrength;
	INT32 iError = ( 100 - ubConfidence ) / 5;

	if( iError > 0 )
		iEstimate += (INT32)Random( iError * 2 + 1 ) - iError;

	return VR_ClampByte( iEstimate );
}

void VR_ReportOperationalIntel( UINT8 ubSectorID, UINT8 ubConfidence )
{
	UINT8 x = (UINT8)SECTORX( ubSectorID );
	UINT8 y = (UINT8)SECTORY( ubSectorID );
	SECTORINFO *pSector = &SectorInfo[ ubSectorID ];

	INT32 iObservedPlayerStrength = (INT32)PlayerMercsInSector( x, y, 0 ) * 8;
	INT32 iObservedMilitiaStrength =
		(INT32)pSector->ubNumberOfCivsAtLevel[ GREEN_MILITIA ] +
		(INT32)pSector->ubNumberOfCivsAtLevel[ REGULAR_MILITIA ] * 2 +
		(INT32)pSector->ubNumberOfCivsAtLevel[ ELITE_MILITIA ] * 3;

	GROUP *pGroup = gpGroupList;
	while( pGroup )
	{
		if( !pGroup->fPlayer && pGroup->pEnemyGroup )
		{
			VR_EnsureEnemyFormationState( pGroup );
			ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;

			INT32 iDistance = VR_Abs( (INT32)pGroup->ubSectorX - x ) +
				VR_Abs( (INT32)pGroup->ubSectorY - y );
			INT32 iDeliveredConfidence = (INT32)ubConfidence - iDistance * 2;
			if( iDeliveredConfidence < 10 )
				iDeliveredConfidence = 10;
			if( iDeliveredConfidence > 100 )
				iDeliveredConfidence = 100;

			pEnemy->ubOperationalLastKnownPlayerSectorID = ubSectorID;
			pEnemy->ubOperationalIntelConfidence = (UINT8)iDeliveredConfidence;
			pEnemy->ubOperationalLastKnownPlayerStrength =
				VR_EstimateStrengthWithConfidence( iObservedPlayerStrength, (UINT8)iDeliveredConfidence );
			pEnemy->ubOperationalLastKnownMilitiaStrength =
				VR_EstimateStrengthWithConfidence( iObservedMilitiaStrength, (UINT8)iDeliveredConfidence );
			pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_CONTACT;

			if( iDeliveredConfidence >= 60 )
				VR_WriteOperationalFlags( pEnemy, (UINT16)( VR_ReadOperationalFlags( pEnemy ) | VR_OPFLAG_RECENT_CONTACT ) );
			else
				VR_WriteOperationalFlags( pEnemy, (UINT16)( VR_ReadOperationalFlags( pEnemy ) & ~VR_OPFLAG_RECENT_CONTACT ) );

			VR_LogOperationalDecision( pGroup, "INTEL_REPORT", NULL );
		}

		pGroup = pGroup->next;
	}
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

	// No omniscience: threat comes only from the formation's last received intelligence snapshot.
	if( pGroup && pGroup->pEnemyGroup )
	{
		VR_EnsureEnemyFormationState( pGroup );
		ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
		if( pEnemy->ubOperationalLastKnownPlayerSectorID == ubSectorID &&
			pEnemy->ubOperationalIntelConfidence > 0 )
		{
			score.iPlayerForceRisk = -( (INT32)pEnemy->ubOperationalLastKnownPlayerStrength *
				pEnemy->ubOperationalIntelConfidence / 100 );
			score.iMilitiaRisk = -( (INT32)pEnemy->ubOperationalLastKnownMilitiaStrength *
				pEnemy->ubOperationalIntelConfidence / 100 );
		}
	}

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

	if( pEnemy->ubOperationalMission == VR_OPMISSION_REINFORCE )
	{
		UINT8 targetX = (UINT8)SECTORX( ubTargetSectorID );
		UINT8 targetY = (UINT8)SECTORY( ubTargetSectorID );
		if( StrategicMap[ CALCULATE_STRATEGIC_INDEX( targetX, targetY ) ].fEnemyControlled == FALSE )
			pEnemy->ubOperationalMission = VR_OPMISSION_RELIEVE;
	}

	pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_LEGACY_ASSIGNMENT;

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
		// A formation in direct contact radios a precise report; confidence degrades with dissemination distance.
		VR_ReportOperationalIntel( currentSector, 100 );
		pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_CONTACT;
	}

	if( pEnemy->ubOperationalMission == VR_OPMISSION_RETREAT )
	{
		pEnemy->ubOperationalMission = VR_OPMISSION_REGROUP;
		pEnemy->ubOperationalReserveRole = VR_RESERVE_LOCAL;
		VR_WriteOperationalFlags( pEnemy, (UINT16)( VR_ReadOperationalFlags( pEnemy ) | VR_OPFLAG_REGROUPING ) );
		pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_REGROUP;
	}
	else
	{
		pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_ARRIVAL;
	}

	if( currentSector == pEnemy->ubOperationalHomeSectorID )
		pEnemy->ubOperationalSupply = VR_ClampByte( pEnemy->ubOperationalSupply + 10 );

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

	VR_WriteOperationalFlags( pEnemy, (UINT16)( VR_ReadOperationalFlags( pEnemy ) | VR_OPFLAG_RETREATED_ONCE ) );
	pEnemy->ubOperationalMission = VR_OPMISSION_RETREAT;
	pEnemy->ubOperationalReserveRole = VR_RESERVE_NONE;
	pEnemy->ubOperationalMorale = VR_ClampByte( pEnemy->ubOperationalMorale - 12 );
	pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_RETREAT;

	VR_LogOperationalDecision( pGroup, "RETREAT", NULL );
}

void VR_RecordOperationalContact( GROUP *pGroup, UINT8 ubSectorID, UINT8 ubPlayerStrength, UINT8 ubMilitiaStrength, UINT8 ubConfidence )
{
	if( !VR_IsEnemyFormation( pGroup ) )
		return;

	VR_EnsureEnemyFormationState( pGroup );
	if( !VR_FormationStateIsInitialized( pGroup ) )
		return;

	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
	pEnemy->ubOperationalLastKnownPlayerSectorID = ubSectorID;
	pEnemy->ubOperationalIntelConfidence = VR_ClampByte( ubConfidence );

	if( ubPlayerStrength != VR_OPERATIONAL_STRENGTH_UNKNOWN )
		pEnemy->ubOperationalLastKnownPlayerStrength = VR_ClampByte( ubPlayerStrength );
	if( ubMilitiaStrength != VR_OPERATIONAL_STRENGTH_UNKNOWN )
		pEnemy->ubOperationalLastKnownMilitiaStrength = VR_ClampByte( ubMilitiaStrength );

	pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_CONTACT;
	if( pEnemy->ubOperationalIntelConfidence >= 60 )
		VR_WriteOperationalFlags( pEnemy, (UINT16)( VR_ReadOperationalFlags( pEnemy ) | VR_OPFLAG_RECENT_CONTACT ) );
	else
		VR_WriteOperationalFlags( pEnemy, (UINT16)( VR_ReadOperationalFlags( pEnemy ) & ~VR_OPFLAG_RECENT_CONTACT ) );

	VR_LogOperationalDecision( pGroup, "LOCAL_CONTACT", NULL );
}

GROUP *VR_FindReadyOperationalReserveForSector( UINT8 ubTargetSectorID )
{
	GROUP *pBest = NULL;
	INT32 iBestScore = -32767;
	UINT8 ubTargetX = (UINT8)SECTORX( ubTargetSectorID );
	UINT8 ubTargetY = (UINT8)SECTORY( ubTargetSectorID );

	GROUP *pGroup = gpGroupList;
	while( pGroup )
	{
		if( VR_IsReadyOperationalReserve( pGroup ) )
		{
			ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
			INT32 iDistance = VR_Abs( (INT32)pGroup->ubSectorX - ubTargetX ) +
				VR_Abs( (INT32)pGroup->ubSectorY - ubTargetY );
			INT32 iScore =
				(INT32)pEnemy->ubOperationalSupply +
				(INT32)pEnemy->ubOperationalMorale +
				(INT32)pGroup->ubGroupSize * 3 -
				iDistance * 8;

			if( iScore > iBestScore )
			{
				iBestScore = iScore;
				pBest = pGroup;
			}
		}
		pGroup = pGroup->next;
	}

	return pBest;
}

void VR_RecordLegacyAssignment( GROUP *pGroup, UINT8 ubTargetSectorID, UINT8 ubLegacyIntention )
{
	VR_OnEnemyGroupAssigned( pGroup, ubTargetSectorID, ubLegacyIntention );
}

void VR_RecordFormationArrival( GROUP *pGroup )
{
	VR_OnEnemyGroupArrived( pGroup );
}

void VR_CompleteRetreatInSector( UINT8 ubSectorX, UINT8 ubSectorY )
{
	GROUP *pGroup = gpGroupList;
	while( pGroup )
	{
		if( VR_IsEnemyFormation( pGroup ) &&
			!pGroup->fBetweenSectors &&
			pGroup->ubSectorX == ubSectorX &&
			pGroup->ubSectorY == ubSectorY )
		{
			VR_EnsureEnemyFormationState( pGroup );
			if( pGroup->pEnemyGroup->ubOperationalMission == VR_OPMISSION_RETREAT )
				VR_OnEnemyGroupArrived( pGroup );
		}
		pGroup = pGroup->next;
	}
}

BOOLEAN VR_RegisterTacticalRetreatSoldier(
	UINT8 ubSourceX, UINT8 ubSourceY,
	UINT8 ubDestX, UINT8 ubDestY,
	UINT8 ubAdmins, UINT8 ubTroops, UINT8 ubElites )
{
	UINT16 usIncoming = (UINT16)ubAdmins + (UINT16)ubTroops + (UINT16)ubElites;
	if( usIncoming == 0 )
		return FALSE;

	UINT8 ubSourceSector = (UINT8)SECTOR( ubSourceX, ubSourceY );
	UINT8 ubDestSector = (UINT8)SECTOR( ubDestX, ubDestY );
	GROUP *pRetreatGroup = NULL;

	GROUP *pGroup = gpGroupList;
	while( pGroup )
	{
		if( VR_IsEnemyFormation( pGroup ) &&
			!pGroup->fBetweenSectors &&
			pGroup->ubSectorX == ubDestX &&
			pGroup->ubSectorY == ubDestY )
		{
			VR_EnsureEnemyFormationState( pGroup );
			ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
			if( pEnemy->ubOperationalMission == VR_OPMISSION_RETREAT &&
				pEnemy->ubOperationalHomeSectorID == ubSourceSector &&
				(UINT16)pGroup->ubGroupSize + usIncoming <= (UINT16)gGameExternalOptions.iMaxEnemyGroupSize )
			{
				pRetreatGroup = pGroup;
				break;
			}
		}
		pGroup = pGroup->next;
	}

	if( pRetreatGroup )
	{
		pRetreatGroup->pEnemyGroup->ubNumAdmins += ubAdmins;
		pRetreatGroup->pEnemyGroup->ubNumTroops += ubTroops;
		pRetreatGroup->pEnemyGroup->ubNumElites += ubElites;
		pRetreatGroup->ubGroupSize = (UINT8)( pRetreatGroup->ubGroupSize + usIncoming );
		pRetreatGroup->pEnemyGroup->ubOperationalTargetSectorID = ubDestSector;
		return TRUE;
	}

	GROUP *pNew = CreateNewEnemyGroupDepartingFromSector(
		ubDestSector, ubAdmins, ubTroops, ubElites );
	if( !pNew )
		return FALSE;

	// Preserve the origin as the formation home before operational state is initialized.
	pNew->ubOriginalSector = ubSourceSector;
	pNew->ubMoveType = ONE_WAY;
	VR_EnsureEnemyFormationState( pNew );
	pNew->pEnemyGroup->ubOperationalTargetSectorID = ubDestSector;
	VR_OnEnemyGroupRetreated( pNew );
	return TRUE;
}

void VR_DecayOperationalIntelHourly()
{
	GROUP *pGroup = gpGroupList;
	while( pGroup )
	{
		if( VR_IsEnemyFormation( pGroup ) )
		{
			VR_EnsureEnemyFormationState( pGroup );
			ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;

			if( pEnemy->ubOperationalIntelConfidence > 0 )
			{
				UINT8 decay = ( VR_ReadOperationalFlags( pEnemy ) & VR_OPFLAG_RECENT_CONTACT ) ? 1 : 3;
				pEnemy->ubOperationalIntelConfidence =
					( pEnemy->ubOperationalIntelConfidence > decay ) ?
					pEnemy->ubOperationalIntelConfidence - decay : 0;

				if( pEnemy->ubOperationalIntelConfidence < 60 )
					VR_WriteOperationalFlags( pEnemy, (UINT16)( VR_ReadOperationalFlags( pEnemy ) & ~VR_OPFLAG_RECENT_CONTACT ) );
				if( pEnemy->ubOperationalIntelConfidence == 0 )
					pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_INTEL_DECAY;
			}
		}
		pGroup = pGroup->next;
	}
}

void VR_UpdateOperationalReadinessHourly()
{
	GROUP *pGroup = gpGroupList;

	while( pGroup )
	{
		if( VR_IsEnemyFormation( pGroup ) )
		{
			VR_EnsureEnemyFormationState( pGroup );
			ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;

			// Keep persistent mission state aligned with legacy VR intentions while migration is incremental.
			if( pEnemy->ubOperationalMission == VR_OPMISSION_NONE && pEnemy->ubIntention != NO_INTENTIONS )
				pEnemy->ubOperationalMission = VR_MissionFromLegacyIntention( pEnemy->ubIntention );

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

			VR_WriteOperationalFlags( pEnemy, (UINT16)( VR_ReadOperationalFlags( pEnemy ) & ~( VR_OPFLAG_SUPPLY_LOW | VR_OPFLAG_SUPPLY_CRITICAL ) ) );
			if( pEnemy->ubOperationalSupply < 40 )
				VR_WriteOperationalFlags( pEnemy, (UINT16)( VR_ReadOperationalFlags( pEnemy ) | VR_OPFLAG_SUPPLY_LOW ) );
			if( pEnemy->ubOperationalSupply < 20 )
			{
				VR_WriteOperationalFlags( pEnemy, (UINT16)( VR_ReadOperationalFlags( pEnemy ) | VR_OPFLAG_SUPPLY_CRITICAL ) );
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
				VR_WriteOperationalFlags( pEnemy, (UINT16)( VR_ReadOperationalFlags( pEnemy ) & ~VR_OPFLAG_REGROUPING ) );
				pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_REGROUP;
				VR_LogOperationalDecision( pGroup, "REGROUP_COMPLETE", NULL );
			}
		}

		pGroup = pGroup->next;
	}
}

void VR_TraceOperationalRecommendationsHourly()
{
	GROUP *pGroup = gpGroupList;
	while( pGroup )
	{
		if( VR_IsEnemyFormation( pGroup ) )
		{
			VR_EnsureEnemyFormationState( pGroup );
			INT32 iBestScore = 0;
			UINT8 ubBestTarget = VR_FindBestOperationalTarget( pGroup, &iBestScore );
			VR_OPERATIONAL_SCORE score;
			VR_ScoreOperationalTarget( pGroup, ubBestTarget, &score );

			ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
			UINT8 ubOldTarget = pEnemy->ubOperationalTargetSectorID;
			UINT8 ubOldReason = pEnemy->ubOperationalLastDecisionReason;
			pEnemy->ubOperationalTargetSectorID = ubBestTarget;
			pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_TARGET_SCORE;
			VR_LogOperationalDecision( pGroup, "RECOMMEND", &score );
			pEnemy->ubOperationalTargetSectorID = ubOldTarget;
			pEnemy->ubOperationalLastDecisionReason = ubOldReason;
		}
		pGroup = pGroup->next;
	}
}

void VR_HourlyOperationalUpdate()
{
	VR_DecayOperationalIntelHourly();
	VR_UpdateOperationalReadinessHourly();
	VR_TraceOperationalRecommendationsHourly();
}
