#ifndef _VR_STRATEGIC_OPERATIONAL_AI_H_
#define _VR_STRATEGIC_OPERATIONAL_AI_H_

#include "types.h"

// Experimental branch-only decision gate. Master is unaffected because this file does not exist there.
#define VR_OPERATIONAL_GARRISON_REASSIGNMENT_ENABLED 1

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
	VR_OPMISSION_RESERVE
};

enum VR_OPERATIONAL_RESERVE_ROLE
{
	VR_RESERVE_NONE = 0,
	VR_RESERVE_LOCAL,
	VR_RESERVE_REGIONAL,
	VR_RESERVE_CENTRAL
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
	VR_OPFLAG_SUPPLY_LOW        = 0x0001,
	VR_OPFLAG_SUPPLY_CRITICAL   = 0x0002,
	VR_OPFLAG_RECENT_CONTACT    = 0x0004,
	VR_OPFLAG_RETREATED_ONCE    = 0x0008,
	VR_OPFLAG_REGROUPING        = 0x0010
};

typedef struct VR_OPERATIONAL_SCORE
{
	INT32 iBasePriority;
	INT32 iOwnershipValue;
	INT32 iTownValue;
	INT32 iMineValue;
	INT32 iSAMValue;
	INT32 iPlayerForceRisk;
	INT32 iMilitiaRisk;
	INT32 iDistanceCost;
	INT32 iSupplyRisk;
	INT32 iTotal;
} VR_OPERATIONAL_SCORE;

void VR_EnsureEnemyFormationState( GROUP *pGroup );
void VR_EnsureAllEnemyFormationStates();

void VR_OnEnemyGroupAssigned( GROUP *pGroup, UINT8 ubTargetSectorID, UINT8 ubLegacyIntention );
void VR_OnEnemyGroupArrived( GROUP *pGroup );
void VR_OnEnemyGroupRetreated( GROUP *pGroup );
void VR_HourlyOperationalUpdate();
void VR_ReportOperationalIntel( UINT8 ubSectorID, UINT8 ubConfidence );

INT32 VR_ScoreOperationalTarget( GROUP *pGroup, UINT8 ubSectorID, VR_OPERATIONAL_SCORE *pBreakdown );
UINT8 VR_FindBestOperationalTarget( GROUP *pGroup, INT32 *piBestScore );

void VR_SetFormationMission( GROUP *pGroup, UINT8 ubMission, UINT8 ubReason );
void VR_SetFormationReserveRole( GROUP *pGroup, UINT8 ubReserveRole, UINT8 ubReason );

const CHAR8 *VR_OperationalMissionName( UINT8 ubMission );
const CHAR8 *VR_OperationalReserveRoleName( UINT8 ubRole );
const CHAR8 *VR_OperationalReasonName( UINT8 ubReason );

void VR_LogOperationalDecision( GROUP *pGroup, const CHAR8 *szEvent, const VR_OPERATIONAL_SCORE *pScore );

#endif
