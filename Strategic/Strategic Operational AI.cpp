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

static UINT8 VR_ClampOperationalPercent( INT32 iValue )
{
	if( iValue < 0 )
		return 0;
	if( iValue > 100 )
		return 100;
	return (UINT8)iValue;
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

static UINT8 VR_SelectReserveRole( const GROUP *pGroup )
{
	if( !pGroup )
		return VR_RESERVE_NONE;

	// Reserve depth is organizational, not a difficulty bonus. Small remnants
	// remain local; larger intact formations can serve at regional/central depth.
	if( pGroup->ubGroupSize >= 20 )
		return VR_RESERVE_CENTRAL;
	if( pGroup->ubGroupSize >= 10 )
		return VR_RESERVE_REGIONAL;
	return VR_RESERVE_LOCAL;
}

void VR_UpdateOperationalReadinessHourly()
{
	for( GROUP *pGroup = gpGroupList; pGroup; pGroup = pGroup->next )
	{
		VR_EnsureEnemyFormationState( pGroup );
		if( !VR_FormationStateIsInitialized( pGroup ) )
			continue;

		ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
		const UINT8 ubCurrentSector =
			(UINT8)SECTOR( pGroup->ubSectorX, pGroup->ubSectorY );

		// Some legacy callers create a GROUP first and assign ubIntention
		// immediately afterwards. Formation state may therefore have been born as
		// NONE even though the group is now a patrol/staging/reinforcement group.
		// Mirror that authoritative legacy intent before considering reserve status.
		if( pEnemy->ubOperationalMission == VR_OPMISSION_NONE &&
			pEnemy->ubIntention != NO_INTENTIONS )
		{
			pEnemy->ubOperationalMission =
				VR_MissionFromLegacyIntention( pEnemy->ubIntention );
			pEnemy->ubOperationalReserveRole = VR_RESERVE_NONE;
			pEnemy->ubOperationalLastDecisionReason =
				VR_OPREASON_LEGACY_ASSIGNMENT;
		}


		// Logistics model is intentionally information-independent: it depends only
		// on the formation's own movement and Queen-controlled infrastructure.
		if( pGroup->fBetweenSectors )
		{
			pEnemy->ubOperationalSupply =
				VR_ClampOperationalPercent( (INT32)pEnemy->ubOperationalSupply - 1 );
		}
		else
		{
			const BOOLEAN fEnemyControlled =
				StrategicMap[
					CALCULATE_STRATEGIC_INDEX(
						pGroup->ubSectorX, pGroup->ubSectorY ) ].fEnemyControlled;

			if( fEnemyControlled && ubCurrentSector == pEnemy->ubOperationalHomeSectorID )
			{
				pEnemy->ubOperationalSupply =
					VR_ClampOperationalPercent( (INT32)pEnemy->ubOperationalSupply + 5 );
			}
			else if( fEnemyControlled &&
				( SectorInfo[ ubCurrentSector ].ubGarrisonID != NO_GARRISON ||
				  SectorInfo[ ubCurrentSector ].ubTraversability[ 4 ] == TOWN ||
				  IsThereAMineInThisSector( pGroup->ubSectorX, pGroup->ubSectorY ) ||
				  IsThisSectorASAMSector( pGroup->ubSectorX, pGroup->ubSectorY, 0 ) ) )
			{
				pEnemy->ubOperationalSupply =
					VR_ClampOperationalPercent( (INT32)pEnemy->ubOperationalSupply + 2 );
			}
		}

		if( pEnemy->ubOperationalSupply < 25 )
		{
			pEnemy->usOperationalFlags |=
				VR_OPFLAG_SUPPLY_LOW | VR_OPFLAG_SUPPLY_CRITICAL;
		}
		else if( pEnemy->ubOperationalSupply < 50 )
		{
			pEnemy->usOperationalFlags |= VR_OPFLAG_SUPPLY_LOW;
			pEnemy->usOperationalFlags &= ~VR_OPFLAG_SUPPLY_CRITICAL;
		}
		else
		{
			pEnemy->usOperationalFlags &=
				~( VR_OPFLAG_SUPPLY_LOW | VR_OPFLAG_SUPPLY_CRITICAL );
		}

		// Only idle/recovering formations receive reserve classification here.
		// Active legacy Queen assignments remain untouched.
		if( (pEnemy->ubOperationalMission == VR_OPMISSION_NONE &&
			 pEnemy->ubIntention == NO_INTENTIONS &&
			 !pGroup->fBetweenSectors &&
			 pGroup->pWaypoints == NULL) ||
			pEnemy->ubOperationalMission == VR_OPMISSION_RESERVE )
		{
			const UINT8 ubRole = VR_SelectReserveRole( pGroup );
			if( pEnemy->ubOperationalReserveRole != ubRole )
			{
				pEnemy->ubOperationalReserveRole = ubRole;
				pEnemy->ubOperationalMission = VR_OPMISSION_RESERVE;
				pEnemy->ubOperationalLastDecisionReason =
					VR_OPREASON_RESERVE_POSTURE;
			}
		}

		// A formation that has completed its retreat and is physically stationary
		// may progress from REGROUP to RESERVE once basic readiness is restored.
		if( pEnemy->ubOperationalMission == VR_OPMISSION_REGROUP &&
			!pGroup->fBetweenSectors &&
			!(pEnemy->usOperationalFlags & VR_OPFLAG_RECENT_CONTACT) &&
			pEnemy->ubOperationalSupply >= 60 &&
			pEnemy->ubOperationalMorale >= 55 )
		{
			pEnemy->ubOperationalMission = VR_OPMISSION_RESERVE;
			pEnemy->ubOperationalReserveRole = VR_SelectReserveRole( pGroup );
			pEnemy->usOperationalFlags &= ~VR_OPFLAG_REGROUPING;
			pEnemy->ubOperationalLastDecisionReason =
				VR_OPREASON_RESERVE_POSTURE;
		}
	}
}

void VR_RecordFormationRetreat( GROUP *pGroup, UINT8 ubDestinationSectorID )
{
	VR_EnsureEnemyFormationState( pGroup );
	if( !VR_FormationStateIsInitialized( pGroup ) )
		return;

	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
	pEnemy->ubOperationalMission = VR_OPMISSION_RETREAT;
	pEnemy->ubOperationalTargetSectorID = ubDestinationSectorID;
	pEnemy->ubOperationalReserveRole = VR_RESERVE_NONE;
	pEnemy->usOperationalFlags |=
		VR_OPFLAG_RETREATED_ONCE | VR_OPFLAG_REGROUPING | VR_OPFLAG_RECENT_CONTACT;
	// A formation that has just broken contact is not immediately reusable as a
	// reserve even if its abstract supply/morale remain healthy. Preserve any
	// stronger existing report; otherwise keep a short-lived generic contact
	// confidence that decays through the normal hourly intel path.
	if( pEnemy->ubOperationalIntelConfidence < 80 )
		pEnemy->ubOperationalIntelConfidence = 80;
	if( pEnemy->ubOperationalRetreatCount < 255 )
		++pEnemy->ubOperationalRetreatCount;
	pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_RETREAT;

	const unsigned long uiDecision = VRAnalyticsBeginDecision(
		VR_ANALYTICS_STRATEGIC, "enemy_formation",
		(unsigned int)pEnemy->usFormationID, "formation_retreat" );
	if( uiDecision )
	{
		VRAnalyticsStateInt( uiDecision, "group_id", pGroup->ubGroupID );
		VRAnalyticsStateInt( uiDecision, "destination_sector", ubDestinationSectorID );
		VRAnalyticsStateInt( uiDecision, "retreat_count",
			pEnemy->ubOperationalRetreatCount );
		VRAnalyticsCommitDecision( uiDecision, "retreat",
			ubDestinationSectorID, pGroup->ubGroupSize,
			"persistent formation retreat state" );
	}
}

void VR_RecordFormationArrival( GROUP *pGroup )
{
	VR_EnsureEnemyFormationState( pGroup );
	if( !VR_FormationStateIsInitialized( pGroup ) )
		return;

	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
	const UINT8 ubSectorID =
		(UINT8)SECTOR( pGroup->ubSectorX, pGroup->ubSectorY );

	if( pEnemy->ubOperationalMission == VR_OPMISSION_RETREAT )
	{
		pEnemy->ubOperationalMission = VR_OPMISSION_REGROUP;
		pEnemy->ubOperationalTargetSectorID = ubSectorID;
		pEnemy->ubOperationalReserveRole = VR_RESERVE_NONE;
		pEnemy->usOperationalFlags |= VR_OPFLAG_REGROUPING;
		pEnemy->ubOperationalLastDecisionReason = VR_OPREASON_REGROUP;
	}
	else if( pEnemy->ubOperationalMission == VR_OPMISSION_REGROUP )
	{
		pEnemy->ubOperationalTargetSectorID = ubSectorID;
	}

	const unsigned long uiDecision = VRAnalyticsBeginDecision(
		VR_ANALYTICS_STRATEGIC, "enemy_formation",
		(unsigned int)pEnemy->usFormationID, "formation_arrival" );
	if( uiDecision )
	{
		VRAnalyticsStateInt( uiDecision, "group_id", pGroup->ubGroupID );
		VRAnalyticsStateInt( uiDecision, "sector", ubSectorID );
		VRAnalyticsStateInt( uiDecision, "mission",
			pEnemy->ubOperationalMission );
		VRAnalyticsCommitDecision( uiDecision, "arrive",
			ubSectorID, pGroup->ubGroupSize,
			"persistent formation arrival state" );
	}
}


static GROUP *VR_FindTacticalRetreatRemnant( UINT8 ubSourceSectorID,
	UINT8 ubDestinationX, UINT8 ubDestinationY )
{
	for( GROUP *pGroup = gpGroupList; pGroup; pGroup = pGroup->next )
	{
		if( !VR_FormationStateIsInitialized( pGroup ) ||
			pGroup->fBetweenSectors ||
			pGroup->ubSectorX != ubDestinationX ||
			pGroup->ubSectorY != ubDestinationY )
		{
			continue;
		}

		ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
		if( pEnemy->ubOperationalMission == VR_OPMISSION_RETREAT &&
			pEnemy->ubOperationalHomeSectorID == ubSourceSectorID &&
			pEnemy->ubOperationalTargetSectorID ==
				(UINT8)SECTOR( ubDestinationX, ubDestinationY ) &&
			(pEnemy->usOperationalFlags & VR_OPFLAG_REGROUPING) )
		{
			return pGroup;
		}
	}

	return NULL;
}

BOOLEAN VR_RegisterTacticalRetreatSoldier( UINT8 ubSourceX, UINT8 ubSourceY,
	UINT8 ubDestinationX, UINT8 ubDestinationY,
	UINT8 ubAdmins, UINT8 ubTroops, UINT8 ubElites )
{
	if( ubSourceX < 1 || ubSourceX > 16 ||
		ubSourceY < 1 || ubSourceY > 16 ||
		ubDestinationX < 1 || ubDestinationX > 16 ||
		ubDestinationY < 1 || ubDestinationY > 16 ||
		( UINT16 )ubAdmins + ( UINT16 )ubTroops + ( UINT16 )ubElites != 1 )
	{
		return FALSE;
	}

	const UINT8 ubSourceSectorID =
		(UINT8)SECTOR( ubSourceX, ubSourceY );
	const UINT8 ubDestinationSectorID =
		(UINT8)SECTOR( ubDestinationX, ubDestinationY );

	GROUP *pGroup = VR_FindTacticalRetreatRemnant(
		ubSourceSectorID, ubDestinationX, ubDestinationY );

	if( pGroup )
	{
		pGroup->pEnemyGroup->ubNumAdmins += ubAdmins;
		pGroup->pEnemyGroup->ubNumTroops += ubTroops;
		pGroup->pEnemyGroup->ubNumElites += ubElites;
		++pGroup->ubGroupSize;
	}
	else
	{
		pGroup = CreateNewEnemyGroupDepartingFromSector(
			ubDestinationSectorID, ubAdmins, ubTroops, ubElites );
		if( !pGroup )
			return FALSE;

		// Native tactical traversal is instantaneous at the strategic level. The
		// remnant therefore exists in the destination already, but its previous
		// sector records the edge it escaped through for any immediate pursuit.
		pGroup->ubPrevX = ubSourceX;
		pGroup->ubPrevY = ubSourceY;
		pGroup->ubMoveType = ONE_WAY;
		pGroup->ubCreatedSectorID = ubSourceSectorID;
		pGroup->pEnemyGroup->ubIntention = NO_INTENTIONS;

		VR_EnsureEnemyFormationState( pGroup );
		if( !VR_FormationStateIsInitialized( pGroup ) )
			return FALSE;

		pGroup->pEnemyGroup->ubOperationalHomeSectorID = ubSourceSectorID;
		VR_RecordFormationRetreat( pGroup, ubDestinationSectorID );
	}

	const unsigned long uiDecision = VRAnalyticsBeginDecision(
		VR_ANALYTICS_STRATEGIC, "enemy_formation",
		(unsigned int)pGroup->pEnemyGroup->usFormationID,
		"tactical_retreat_transfer" );
	if( uiDecision )
	{
		VRAnalyticsStateInt( uiDecision, "source_sector", ubSourceSectorID );
		VRAnalyticsStateInt( uiDecision, "destination_sector", ubDestinationSectorID );
		VRAnalyticsStateInt( uiDecision, "group_id", pGroup->ubGroupID );
		VRAnalyticsStateInt( uiDecision, "group_size", pGroup->ubGroupSize );
		VRAnalyticsCommitDecision( uiDecision, "preserve_retreat_remnant",
			ubDestinationSectorID, pGroup->ubGroupSize,
			"map-edge escape transferred into persistent formation" );
	}

	return TRUE;
}

void VR_CompleteRetreatInSector( UINT8 ubSectorX, UINT8 ubSectorY )
{
	for( GROUP *pGroup = gpGroupList; pGroup; pGroup = pGroup->next )
	{
		if( !VR_FormationStateIsInitialized( pGroup ) ||
			pGroup->fBetweenSectors ||
			pGroup->ubSectorX != ubSectorX ||
			pGroup->ubSectorY != ubSectorY )
		{
			continue;
		}

		if( pGroup->pEnemyGroup->ubOperationalMission == VR_OPMISSION_RETREAT )
			VR_RecordFormationArrival( pGroup );
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



BOOLEAN VR_IsReadyOperationalReserve( GROUP *pGroup )
{
	VR_EnsureEnemyFormationState( pGroup );
	if( !VR_FormationStateIsInitialized( pGroup ) ||
		pGroup->fBetweenSectors ||
		pGroup->ubGroupSize == 0 )
	{
		return FALSE;
	}

	ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
	return pEnemy->ubOperationalMission == VR_OPMISSION_RESERVE &&
		pEnemy->ubOperationalReserveRole != VR_RESERVE_NONE &&
		pEnemy->ubOperationalSupply >= 60 &&
		pEnemy->ubOperationalMorale >= 55 &&
		!(pEnemy->usOperationalFlags &
			(VR_OPFLAG_SUPPLY_CRITICAL | VR_OPFLAG_REGROUPING |
			 VR_OPFLAG_RECENT_CONTACT));
}

GROUP *VR_FindReadyOperationalReserveForSector( UINT8 ubTargetSectorID )
{
	GROUP *pBest = NULL;
	INT32 iBestScore = -32767;
	const UINT8 ubTargetX = (UINT8)SECTORX( ubTargetSectorID );
	const UINT8 ubTargetY = (UINT8)SECTORY( ubTargetSectorID );

	for( GROUP *pGroup = gpGroupList; pGroup; pGroup = pGroup->next )
	{
		if( !VR_IsReadyOperationalReserve( pGroup ) )
			continue;

		ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
		const INT32 iDistance =
			VR_OperationalAbs( (INT32)pGroup->ubSectorX - (INT32)ubTargetX ) +
			VR_OperationalAbs( (INT32)pGroup->ubSectorY - (INT32)ubTargetY );

		// Pure readiness query: closer formations are preferred, with modest
		// credit for strength/readiness. Reserve role is descriptive here, not
		// a magical movement or combat bonus.
		INT32 iScore =
			-( iDistance * 10 ) +
			(INT32)pGroup->ubGroupSize +
			(INT32)pEnemy->ubOperationalSupply / 10 +
			(INT32)pEnemy->ubOperationalMorale / 10;

		if( iScore > iBestScore )
		{
			iBestScore = iScore;
			pBest = pGroup;
		}
	}

	return pBest;
}

void VR_TraceOperationalRecommendationsHourly()
{
	for( GROUP *pGroup = gpGroupList; pGroup; pGroup = pGroup->next )
	{
		if( !VR_FormationStateIsInitialized( pGroup ) ||
			pGroup->fBetweenSectors )
		{
			continue;
		}

		INT32 iBestScore = 0;
		const UINT8 ubBestSector =
			VR_FindBestOperationalTarget( pGroup, &iBestScore );
		// Sector IDs span the full UINT8 range; 0xff is the real P16 sector.
		// Use the score sentinel instead of sacrificing a valid map sector.
		if( iBestScore <= -32767 )
			continue;

		VR_OPERATIONAL_SCORE Score;
		VR_ScoreOperationalTarget( pGroup, ubBestSector, &Score );

		ENEMYGROUP *pEnemy = pGroup->pEnemyGroup;
		const unsigned long uiDecision = VRAnalyticsBeginDecision(
			VR_ANALYTICS_STRATEGIC, "enemy_formation",
			(unsigned int)pEnemy->usFormationID, "operational_target_recommendation" );
		if( !uiDecision )
			continue;

		VRAnalyticsStateInt( uiDecision, "group_id", pGroup->ubGroupID );
		VRAnalyticsStateInt( uiDecision, "mission", pEnemy->ubOperationalMission );
		VRAnalyticsStateInt( uiDecision, "supply", pEnemy->ubOperationalSupply );
		VRAnalyticsStateInt( uiDecision, "morale", pEnemy->ubOperationalMorale );
		VRAnalyticsStateInt( uiDecision, "intel_confidence",
			pEnemy->ubOperationalIntelConfidence );
		VRAnalyticsStateInt( uiDecision, "base_priority", Score.iBasePriority );
		VRAnalyticsStateInt( uiDecision, "ownership_value", Score.iOwnershipValue );
		VRAnalyticsStateInt( uiDecision, "town_value", Score.iTownValue );
		VRAnalyticsStateInt( uiDecision, "mine_value", Score.iMineValue );
		VRAnalyticsStateInt( uiDecision, "sam_value", Score.iSAMValue );
		VRAnalyticsStateInt( uiDecision, "player_force_risk", Score.iPlayerForceRisk );
		VRAnalyticsStateInt( uiDecision, "militia_risk", Score.iMilitiaRisk );
		VRAnalyticsStateInt( uiDecision, "distance_cost", Score.iDistanceCost );
		VRAnalyticsStateInt( uiDecision, "supply_risk", Score.iSupplyRisk );
		VRAnalyticsCommitDecision( uiDecision, "recommend_target",
			ubBestSector, iBestScore,
			"advisory only; operational movement loop disabled" );
	}
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
