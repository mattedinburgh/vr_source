#ifndef _VR_STRATEGIC_OPERATIONAL_AI_H_
#define _VR_STRATEGIC_OPERATIONAL_AI_H_

#include "types.h"

// Persistent formation state is active, but operational decision consumers remain
// disabled until their information, movement and save/load interactions are integrated.
#define VR_OPERATIONAL_STATE_FOUNDATION_ENABLED 1
#define VR_OPERATIONAL_DECISION_LOOP_ENABLED    0
#define VR_OPERATIONAL_STRENGTH_UNKNOWN          0xff

struct GROUP;

enum VR_OPERATIONAL_MISSION
{
	VR_OPMISSION_NONE = 0,
	VR_OPMISSION_GARRISON,
	VR_OPMISSION_PATROL,
	VR_OPMISSION_RECON,
	VR_OPMISSION_ATTACK,
	VR_OPMISSION_RAID,
	VR_OPMISSION_REINFORCE,
	VR_OPMISSION_RELIEVE,
	VR_OPMISSION_INTERCEPT,
	VR_OPMISSION_BLOCK_ROAD,
	VR_OPMISSION_ESCORT,
	VR_OPMISSION_SUPPLY,
	VR_OPMISSION_RETREAT,
	VR_OPMISSION_REGROUP,
	VR_OPMISSION_RESERVE,
	VR_OPMISSION_COUNT
};

enum VR_OPERATIONAL_RESERVE_ROLE
{
	VR_RESERVE_NONE = 0,
	VR_RESERVE_LOCAL,
	VR_RESERVE_REGIONAL,
	VR_RESERVE_CENTRAL,
	VR_RESERVE_COUNT
};

enum VR_OPERATIONAL_DECISION_REASON
{
	VR_OPREASON_NONE = 0,
	VR_OPREASON_FORMATION_CREATED,
	VR_OPREASON_LEGACY_ASSIGNMENT,
	VR_OPREASON_TARGET_SCORE,
	VR_OPREASON_ARRIVAL,
	VR_OPREASON_RETREAT,
	VR_OPREASON_REGROUP,
	VR_OPREASON_LOW_SUPPLY,
	VR_OPREASON_RESUPPLIED,
	VR_OPREASON_CONTACT,
	VR_OPREASON_INTEL_DECAY,
	VR_OPREASON_RESERVE_POSTURE,
	VR_OPREASON_TARGET_RECOMMENDATION
};

enum VR_OPERATIONAL_FLAGS
{
	VR_OPFLAG_SUPPLY_LOW      = 0x0001,
	VR_OPFLAG_SUPPLY_CRITICAL = 0x0002,
	VR_OPFLAG_RECENT_CONTACT  = 0x0004,
	VR_OPFLAG_RETREATED_ONCE  = 0x0008,
	VR_OPFLAG_REGROUPING      = 0x0010
};

typedef struct VR_OPERATIONAL_SCORE
{
	INT32 iTotal;
	INT32 iBasePriority;
	INT32 iOwnershipValue;
	INT32 iTownValue;
	INT32 iMineValue;
	INT32 iSAMValue;
	INT32 iPlayerForceRisk;
	INT32 iMilitiaRisk;
	INT32 iDistanceCost;
	INT32 iSupplyRisk;
} VR_OPERATIONAL_SCORE;

BOOLEAN VR_FormationStateIsInitialized( const GROUP *pGroup );
void VR_EnsureEnemyFormationState( GROUP *pGroup );
void VR_EnsureAllEnemyFormationStates();
void VR_SyncFormationMissionFromLegacy( GROUP *pGroup );
void VR_RecordLegacyAssignment( GROUP *pGroup, UINT8 ubTargetSectorID, UINT8 ubLegacyIntention );
void VR_RecordOperationalContact( GROUP *pObserver, UINT8 ubSectorID,
	UINT8 ubObservedPlayerStrength, UINT8 ubObservedMilitiaStrength, UINT8 ubConfidence );
void VR_DecayOperationalIntelHourly();
void VR_UpdateOperationalReadinessHourly();
void VR_TraceOperationalRecommendationsHourly();
void VR_RecordFormationRetreat( GROUP *pGroup, UINT8 ubDestinationSectorID );
void VR_RecordFormationArrival( GROUP *pGroup );
BOOLEAN VR_RegisterTacticalRetreatSoldier( UINT8 ubSourceX, UINT8 ubSourceY,
	UINT8 ubDestinationX, UINT8 ubDestinationY,
	UINT8 ubAdmins, UINT8 ubTroops, UINT8 ubElites );
void VR_CompleteRetreatInSector( UINT8 ubSectorX, UINT8 ubSectorY );
INT32 VR_ScoreOperationalTarget( GROUP *pGroup, UINT8 ubSectorID, VR_OPERATIONAL_SCORE *pBreakdown );
UINT8 VR_FindBestOperationalTarget( GROUP *pGroup, INT32 *piBestScore );

UINT16 VR_GetFormationID( GROUP *pGroup );
UINT8 VR_GetFormationMission( GROUP *pGroup );
UINT8 VR_GetFormationReserveRole( GROUP *pGroup );

const CHAR8 *VR_OperationalMissionName( UINT8 ubMission );
const CHAR8 *VR_OperationalReserveRoleName( UINT8 ubRole );

#endif
