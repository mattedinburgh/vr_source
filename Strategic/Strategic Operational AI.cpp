#include "Strategic Operational AI.h"
#include "Strategic Movement.h"
#include "Campaign Types.h"

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
	pEnemy->ubOperationalLastKnownPlayerStrength = 0;
	pEnemy->ubOperationalLastKnownMilitiaStrength = 0;
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
