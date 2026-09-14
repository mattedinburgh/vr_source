#include <string.h>
#include "Strategic Operational AI.h"
#include "Strategic Movement.h"
#include "Campaign Types.h"
#include "strategicmap.h"
#include "Strategic Mines.h"
#include "VRAnalytics.h"

#define VR_OPERATIONAL_MAGIC0 'O'
#define VR_OPERATIONAL_MAGIC1 'P'
#define VR_OPERATIONAL_MAGIC2 'S'

static BOOLEAN VR_IsEnemyFormation( const GROUP *pGroup )
{
	return pGroup && VR_IsEnemyStrategicGroup( pGroup ) && pGroup->pEnemyGroup;
}

static UINT8 VR_MissionFromLegacyIntention( UINT8 ubLegacyIntention )
{
	switch( ubLegacyIntention )
	{
		case PURSUIT:        return VR_OPMISSION_INTERCEPT;
		case STAGING:        return VR_OPMISSION_ATTACK;
		case PATROL:         return VR_OPMISSION_PATROL;
		case REINFORCEMENTS: return VR_OPMISSION_REINFORCE;
		case ASSAULT:        return VR_OPMISSION_ATTACK;
		case NO_INTENTIONS:
		default:             return VR_OPMISSION_NONE;
	}
}

BOOLEAN VR_FormationStateIsInitialized( const GROUP *pGroup )
{
	if( !VR_IsEnemyFormation( pGroup ) )
		return FALSE;

	const ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
	return pEnemy->usFormationID != 0 &&
		pEnemy->ubOperationalMagic0 == VR_OPERATIONAL_MAGIC0 &&
		pEnemy->ubOperationalMagic1 == VR_OPERATIONAL_MAGIC1 &&
		pEnemy->ubOperationalMagic2 == VR_OPERATIONAL_MAGIC2 &&
		pEnemy->ubOperationalMission < VR_OPMISSION_COUNT &&
		pEnemy->ubOperationalReserveRole < VR_RESERVE_COUNT;
}

static BOOLEAN VR_FormationIDInUse( UINT16 usFormationID, const GROUP *pExcept )
{
	for( GROUP *pGroup = gpGroupList; pGroup; pGroup = pGroup->next )
	{
		if( pGroup == pExcept || !VR_IsEnemyFormation( pGroup ) )
			continue;

		if( VR_FormationStateIsInitialized( pGroup ) &&
			pGroup->pEnemyGroup->usFormationID == usFormationID )
		{
			return TRUE;
		}
	}

	return FALSE;
}

static UINT16 VR_CreateFormationID( GROUP *pGroup )
{
	// Build a stable active-campaign identity from immutable/long-lived group data,
	// then resolve the rare collision against currently active formations.
	UINT16 usCandidate =
		(UINT16)( ( (UINT16)pGroup->ubOriginalSector << 8 ) |
			(UINT16)pGroup->ubGroupID );

	if( usCandidate == 0 )
		usCandidate = 1;

	const UINT16 usStart = usCandidate;
	while( VR_FormationIDInUse( usCandidate, pGroup ) )
	{
		++usCandidate;
		if( usCandidate == 0 )
			usCandidate = 1;

		if( usCandidate == usStart )
			return (UINT16)( pGroup->ubGroupID ? pGroup->ubGroupID : 1 );
	}

	return usCandidate;
}

void VR_EnsureEnemyFormationState( GROUP *pGroup )
{
	if( !VR_OPERATIONAL_STATE_FOUNDATION_ENABLED ||
		!VR_IsEnemyFormation( pGroup ) ||
		VR_FormationStateIsInitialized( pGroup ) )
	{
		return;
	}

	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
	pEnemy->usFormationID = VR_CreateFormationID( pGroup );
	pEnemy->ubOperationalMagic0 = VR_OPERATIONAL_MAGIC0;
	pEnemy->ubOperationalMagic1 = VR_OPERATIONAL_MAGIC1;
	pEnemy->ubOperationalMagic2 = VR_OPERATIONAL_MAGIC2;
	pEnemy->ubOperationalMission = VR_MissionFromLegacyIntention( pEnemy->ubIntention );
	pEnemy->ubOperationalReserveRole = VR_RESERVE_NONE;
	pEnemy->ubOperationalSupply = 100;
	pEnemy->ubOperationalMorale = 75;

	// No free knowledge: a new/legacy formation starts without an operational
	// player-location report. Later intelligence code must explicitly supply it.
	pEnemy->ubOperationalIntelConfidence = 0;
	pEnemy->ubOperationalTargetSectorID =
		(UINT8)SECTOR( pGroup->ubSectorX, pGroup->ubSectorY );
	pEnemy->ubOperationalHomeSectorID = pGroup->ubOriginalSector;
	pEnemy->ubOperationalLastKnownPlayerSectorID = 0xff;
	pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_FORMATION_CREATED;
	pEnemy->ubOperationalRetreatCount = 0;
	pEnemy->usOperationalFlags = 0;
	pEnemy->ubOperationalLastKnownPlayerStrength = VR_OPERATIONAL_STRENGTH_UNKNOWN;
	pEnemy->ubOperationalLastKnownMilitiaStrength = VR_OPERATIONAL_STRENGTH_UNKNOWN;
}

void VR_EnsureAllEnemyFormationStates()
{
	for( GROUP *pGroup = gpGroupList; pGroup; pGroup = pGroup->next )
		VR_EnsureEnemyFormationState( pGroup );
}

void VR_SyncFormationMissionFromLegacy( GROUP *pGroup )
{
	VR_EnsureEnemyFormationState( pGroup );
	if( !VR_FormationStateIsInitialized( pGroup ) )
		return;

	pGroup->pEnemyGroup->ubOperationalMission =
		VR_MissionFromLegacyIntention( pGroup->pEnemyGroup->ubIntention );
	pGroup->pEnemyGroup->ubOperationalLastDecisionReason =
		VR_OPREASON_LEGACY_ASSIGNMENT;
}

void VR_RecordLegacyAssignment( GROUP *pGroup, UINT8 ubTargetSectorID, UINT8 ubLegacyIntention )
{
	VR_EnsureEnemyFormationState( pGroup );
	if( !VR_FormationStateIsInitialized( pGroup ) )
		return;

	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
	pEnemy->ubOperationalMission = VR_MissionFromLegacyIntention( ubLegacyIntention );
	pEnemy->ubOperationalTargetSectorID = ubTargetSectorID;
	pEnemy->ubOperationalReserveRole = VR_RESERVE_NONE;
	pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_LEGACY_ASSIGNMENT;
}

static UINT8 VR_ClampOperationalValue( UINT8 ubValue )
{
	if( ubValue == VR_OPERATIONAL_STRENGTH_UNKNOWN )
		return ubValue;
	return ubValue > 100 ? 100 : ubValue;
}

void VR_RecordOperationalContact( GROUP *pObserver, UINT8 ubSectorID,
	UINT8 ubObservedPlayerStrength, UINT8 ubObservedMilitiaStrength, UINT8 ubConfidence )
{
	VR_EnsureEnemyFormationState( pObserver );
	if( !VR_FormationStateIsInitialized( pObserver ) )
		return;

	ENEMYGROUP *pEnemy = pObserver->pEnemyGroup;
	const UINT8 ubClampedConfidence = ubConfidence > 100 ? 100 : ubConfidence;

	pEnemy->ubOperationalLastKnownPlayerSectorID = ubSectorID;
	pEnemy->ubOperationalIntelConfidence = ubClampedConfidence;
	if( ubObservedPlayerStrength != VR_OPERATIONAL_STRENGTH_UNKNOWN )
		pEnemy->ubOperationalLastKnownPlayerStrength =
			VR_ClampOperationalValue( ubObservedPlayerStrength );
	if( ubObservedMilitiaStrength != VR_OPERATIONAL_STRENGTH_UNKNOWN )
		pEnemy->ubOperationalLastKnownMilitiaStrength =
			VR_ClampOperationalValue( ubObservedMilitiaStrength );

	pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_CONTACT;
	if( ubClampedConfidence >= 60 )
		pEnemy->usOperationalFlags |= VR_OPFLAG_RECENT_CONTACT;
	else
		pEnemy->usOperationalFlags &= ~VR_OPFLAG_RECENT_CONTACT;

	const unsigned long uiDecision = VRAnalyticsBeginDecision(
		VR_ANALYTICS_STRATEGIC, "enemy_formation",
		(unsigned int)pEnemy->usFormationID, "operational_intel_update" );
	if( uiDecision )
	{
		VRAnalyticsStateInt( uiDecision, "observer_group", pObserver->ubGroupID );
		VRAnalyticsStateInt( uiDecision, "reported_sector", ubSectorID );
		VRAnalyticsStateInt( uiDecision, "confidence", ubClampedConfidence );
		VRAnalyticsStateInt( uiDecision, "player_strength",
			pEnemy->ubOperationalLastKnownPlayerStrength );
		VRAnalyticsStateInt( uiDecision, "militia_strength",
			pEnemy->ubOperationalLastKnownMilitiaStrength );
		VRAnalyticsCommitDecision( uiDecision, "record_local_report",
			ubSectorID, ubClampedConfidence, "direct/local strategic contact" );
	}
}

void VR_DecayOperationalIntelHourly()
{
	for( GROUP *pGroup = gpGroupList; pGroup; pGroup = pGroup->next )
	{
		if( !VR_FormationStateIsInitialized( pGroup ) )
			continue;

		ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
		if( pEnemy->ubOperationalIntelConfidence == 0 )
			continue;

		const UINT8 ubOldConfidence = pEnemy->ubOperationalIntelConfidence;
		pEnemy->ubOperationalIntelConfidence =
			ubOldConfidence > 8 ? (UINT8)( ubOldConfidence - 8 ) : 0;

		if( pEnemy->ubOperationalIntelConfidence < 60 )
			pEnemy->usOperationalFlags &= ~VR_OPFLAG_RECENT_CONTACT;

		if( pEnemy->ubOperationalIntelConfidence == 0 )
			pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_INTEL_DECAY;
	}
}

static INT32 VR_OperationalAbs( INT32 iValue )
{
	return iValue < 0 ? -iValue : iValue;
}

INT32 VR_ScoreOperationalTarget( GROUP *pGroup, UINT8 ubSectorID, VR_OPERATIONAL_SCORE *pBreakdown )
{
	VR_OPERATIONAL_SCORE Score;
	memset( &Score, 0, sizeof( Score ) );

	if( !pGroup || !VR_IsEnemyFormation( pGroup ) )
	{
		if( pBreakdown )
			*pBreakdown = Score;
		return Score.iTotal;
	}

	VR_EnsureEnemyFormationState( pGroup );
	const UINT8 ubX = (UINT8)SECTORX( ubSectorID );
	const UINT8 ubY = (UINT8)SECTORY( ubSectorID );
	SECTORINFO *pSector = &SectorInfo[ ubSectorID ];
	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;

	// Static/administrative facts are legitimate Queen knowledge.
	if( pSector->ubGarrisonID != NO_GARRISON )
		Score.iBasePriority = 15;
	if( StrategicMap[ CALCULATE_STRATEGIC_INDEX( ubX, ubY ) ].fEnemyControlled == FALSE )
		Score.iOwnershipValue = 30;
	if( pSector->ubTraversability[ 4 ] == TOWN )
		Score.iTownValue = 15;
	if( IsThereAMineInThisSector( ubX, ubY ) )
		Score.iMineValue = 25;
	if( IsThisSectorASAMSector( ubX, ubY, 0 ) )
		Score.iSAMValue = 20;

	// Dynamic force risk is knowledge-bound. Never query current player/militia
	// presence here; only consume this formation's decaying operational report.
	if( pEnemy->ubOperationalIntelConfidence > 0 &&
		pEnemy->ubOperationalLastKnownPlayerSectorID == ubSectorID )
	{
		if( pEnemy->ubOperationalLastKnownPlayerStrength != VR_OPERATIONAL_STRENGTH_UNKNOWN )
			Score.iPlayerForceRisk = -(
				(INT32)pEnemy->ubOperationalLastKnownPlayerStrength *
				(INT32)pEnemy->ubOperationalIntelConfidence / 100 );

		if( pEnemy->ubOperationalLastKnownMilitiaStrength != VR_OPERATIONAL_STRENGTH_UNKNOWN )
			Score.iMilitiaRisk = -(
				(INT32)pEnemy->ubOperationalLastKnownMilitiaStrength *
				(INT32)pEnemy->ubOperationalIntelConfidence / 100 );
	}

	const INT32 iDistance =
		VR_OperationalAbs( (INT32)pGroup->ubSectorX - (INT32)ubX ) +
		VR_OperationalAbs( (INT32)pGroup->ubSectorY - (INT32)ubY );
	Score.iDistanceCost = -( iDistance * 3 );

	if( pEnemy->ubOperationalSupply < 50 )
		Score.iSupplyRisk = -( 50 - (INT32)pEnemy->ubOperationalSupply );

	Score.iTotal =
		Score.iBasePriority +
		Score.iOwnershipValue +
		Score.iTownValue +
		Score.iMineValue +
		Score.iSAMValue +
		Score.iPlayerForceRisk +
		Score.iMilitiaRisk +
		Score.iDistanceCost +
		Score.iSupplyRisk;

	if( pBreakdown )
		*pBreakdown = Score;
	return Score.iTotal;
}

UINT8 VR_FindBestOperationalTarget( GROUP *pGroup, INT32 *piBestScore )
{
	if( !pGroup || !VR_IsEnemyFormation( pGroup ) )
	{
		if( piBestScore )
			*piBestScore = 0;
		return 0xff;
	}

	UINT8 ubBestSector = (UINT8)SECTOR( pGroup->ubSectorX, pGroup->ubSectorY );
	INT32 iBestScore = -32767;

	for( INT32 iSector = 0; iSector < 256; ++iSector )
	{
		VR_OPERATIONAL_SCORE Score;
		const INT32 iScore =
			VR_ScoreOperationalTarget( pGroup, (UINT8)iSector, &Score );

		// Ignore empty wilderness merely because it is geographically close.
		const INT32 iStrategicValue =
			Score.iBasePriority + Score.iOwnershipValue + Score.iTownValue +
			Score.iMineValue + Score.iSAMValue;
		if( iStrategicValue <= 0 )
			continue;

		if( iScore > iBestScore )
		{
			iBestScore = iScore;
			ubBestSector = (UINT8)iSector;
		}
	}

	if( piBestScore )
		*piBestScore = iBestScore;
	return ubBestSector;
}

UINT16 VR_GetFormationID( GROUP *pGroup )
{
	VR_EnsureEnemyFormationState( pGroup );
	return VR_FormationStateIsInitialized( pGroup ) ?
		pGroup->pEnemyGroup->usFormationID : 0;
}

UINT8 VR_GetFormationMission( GROUP *pGroup )
{
	VR_EnsureEnemyFormationState( pGroup );
	return VR_FormationStateIsInitialized( pGroup ) ?
		pGroup->pEnemyGroup->ubOperationalMission : VR_OPMISSION_NONE;
}

UINT8 VR_GetFormationReserveRole( GROUP *pGroup )
{
	VR_EnsureEnemyFormationState( pGroup );
	return VR_FormationStateIsInitialized( pGroup ) ?
		pGroup->pEnemyGroup->ubOperationalReserveRole : VR_RESERVE_NONE;
}

const CHAR8 *VR_OperationalMissionName( UINT8 ubMission )
{
	switch( ubMission )
	{
		case VR_OPMISSION_GARRISON:   return "GARRISON";
		case VR_OPMISSION_PATROL:     return "PATROL";
		case VR_OPMISSION_RECON:      return "RECON";
		case VR_OPMISSION_ATTACK:     return "ATTACK";
		case VR_OPMISSION_RAID:       return "RAID";
		case VR_OPMISSION_REINFORCE:  return "REINFORCE";
		case VR_OPMISSION_RELIEVE:    return "RELIEVE";
		case VR_OPMISSION_INTERCEPT:  return "INTERCEPT";
		case VR_OPMISSION_BLOCK_ROAD: return "BLOCK_ROAD";
		case VR_OPMISSION_ESCORT:     return "ESCORT";
		case VR_OPMISSION_SUPPLY:     return "SUPPLY";
		case VR_OPMISSION_RETREAT:    return "RETREAT";
		case VR_OPMISSION_REGROUP:    return "REGROUP";
		case VR_OPMISSION_RESERVE:    return "RESERVE";
		case VR_OPMISSION_NONE:
		default:                      return "NONE";
	}
}

const CHAR8 *VR_OperationalReserveRoleName( UINT8 ubRole )
{
	switch( ubRole )
	{
		case VR_RESERVE_LOCAL:    return "LOCAL";
		case VR_RESERVE_REGIONAL: return "REGIONAL";
		case VR_RESERVE_CENTRAL:  return "CENTRAL";
		case VR_RESERVE_NONE:
		default:                  return "NONE";
	}
}
