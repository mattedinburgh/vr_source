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
	VR_OPREASON_INTEL_DECAY
};

enum VR_OPERATIONAL_FLAGS
{
	VR_OPFLAG_SUPPLY_LOW      = 0x0001,
	VR_OPFLAG_SUPPLY_CRITICAL = 0x0002,
	VR_OPFLAG_RECENT_CONTACT  = 0x0004,
	VR_OPFLAG_RETREATED_ONCE  = 0x0008,
	VR_OPFLAG_REGROUPING      = 0x0010
};

BOOLEAN VR_FormationStateIsInitialized( const GROUP *pGroup );
void VR_EnsureEnemyFormationState( GROUP *pGroup );
void VR_EnsureAllEnemyFormationStates();
void VR_SyncFormationMissionFromLegacy( GROUP *pGroup );
void VR_RecordLegacyAssignment( GROUP *pGroup, UINT8 ubTargetSectorID, UINT8 ubLegacyIntention );
void VR_RecordOperationalContact( GROUP *pObserver, UINT8 ubSectorID,
	UINT8 ubObservedPlayerStrength, UINT8 ubObservedMilitiaStrength, UINT8 ubConfidence );
void VR_DecayOperationalIntelHourly();

UINT16 VR_GetFormationID( GROUP *pGroup );
UINT8 VR_GetFormationMission( GROUP *pGroup );
UINT8 VR_GetFormationReserveRole( GROUP *pGroup );

const CHAR8 *VR_OperationalMissionName( UINT8 ubMission );
const CHAR8 *VR_OperationalReserveRoleName( UINT8 ubRole );

#endif
