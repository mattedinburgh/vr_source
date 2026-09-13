#ifndef __ENEMY_LOADOUT_PLANNER_H
#define __ENEMY_LOADOUT_PLANNER_H

#include "types.h"

#define ENEMY_LOADOUT_TEAM_MIN 5
#define ENEMY_LOADOUT_TEAM_TARGET 8
#define ENEMY_LOADOUT_TEAM_MAX 10
#define ENEMY_LOADOUT_MAX_CELLS 32

// Experimental enemy equipment planning layer.
//
// This module is compiled by the isolated work-branch Tactical project but is
// intentionally not called by GenerateRandomEquipment() or enemy creation yet.
// It can therefore be reviewed and tuned without changing live Vengeance behaviour.
//
// Design rule:
//   squad doctrine -> soldier role -> weapon family -> attachments -> LBE/load
// rather than:
//   equipment coolness -> random weapon/LBE/attachments.

enum ENEMY_LOADOUT_ROLE
{
	ENEMY_ROLE_RIFLEMAN = 0,
	ENEMY_ROLE_ASSAULT,
	ENEMY_ROLE_AUTOMATIC_RIFLEMAN,
	ENEMY_ROLE_MARKSMAN,
	ENEMY_ROLE_SNIPER,
	ENEMY_ROLE_GRENADIER,
	ENEMY_ROLE_AT_SPECIALIST,
	ENEMY_ROLE_MEDIC,
	ENEMY_ROLE_RADIO_OPERATOR,
	ENEMY_ROLE_SQUAD_LEADER,
	ENEMY_ROLE_SCOUT,
	ENEMY_ROLE_MORTAR,
	ENEMY_ROLE_MAX
};

enum ENEMY_LBE_PROFILE
{
	ENEMY_LBE_LIGHT = 0,
	ENEMY_LBE_STANDARD_RIFLE,
	ENEMY_LBE_ASSAULT,
	ENEMY_LBE_AUTOMATIC,
	ENEMY_LBE_GRENADIER,
	ENEMY_LBE_MEDIC,
	ENEMY_LBE_RADIO,
	ENEMY_LBE_HEAVY_SUPPORT
};

enum ENEMY_OPTIC_PROFILE
{
	ENEMY_OPTIC_IRONS = 0,
	ENEMY_OPTIC_CLOSE_COMBAT,
	ENEMY_OPTIC_LOW_POWER,
	ENEMY_OPTIC_MARKSMAN,
	ENEMY_OPTIC_SNIPER
};

struct ENEMY_ROLE_TARGETS
{
	UINT8 ubDesired[ENEMY_ROLE_MAX];
	UINT8 ubMaximum[ENEMY_ROLE_MAX];
};

struct ENEMY_SQUAD_LOADOUT_STATE
{
	UINT8 ubPlannedSoldiers;
	UINT8 ubAssignedSoldiers;
	UINT8 ubAssigned[ENEMY_ROLE_MAX];
	ENEMY_ROLE_TARGETS Targets;
};

struct ENEMY_LOADOUT_CELL
{
	UINT8 ubSize;
	UINT8 ubAdmins;
	UINT8 ubRegulars;
	UINT8 ubElites;
	INT8 bDoctrineClass;
	ENEMY_SQUAD_LOADOUT_STATE State;

	// Planned role tickets for this cell.  Class-specific counts let the future
	// creation-context layer hand a coherent role to whichever class is created
	// next without depending on soldier insertion order.
	UINT8 ubRoleCount[ENEMY_ROLE_MAX];
	UINT8 ubAdminRoleCount[ENEMY_ROLE_MAX];
	UINT8 ubRegularRoleCount[ENEMY_ROLE_MAX];
	UINT8 ubEliteRoleCount[ENEMY_ROLE_MAX];
};

struct ENEMY_LOADOUT_BATCH
{
	UINT8 ubTotalSoldiers;
	UINT8 ubAdmins;
	UINT8 ubRegulars;
	UINT8 ubElites;
	UINT8 ubCellCount;
	ENEMY_LOADOUT_CELL Cells[ENEMY_LOADOUT_MAX_CELLS];
};

struct ENEMY_LBE_PACKAGE
{
	UINT16 usVest;
	UINT16 usThigh;
	UINT16 usCombatPack;
	UINT16 usBackpack;
};

#define ENEMY_LOADOUT_MAX_ATTACHMENTS 4

struct ENEMY_ATTACHMENT_PACKAGE
{
	UINT8 ubCount;
	UINT16 usItem[ENEMY_LOADOUT_MAX_ATTACHMENTS];
};

struct ENEMY_LOADOUT_ASSIGNMENT_STATE
{
	ENEMY_LOADOUT_BATCH Remaining;
	UINT8 ubNextAdminCell;
	UINT8 ubNextRegularCell;
	UINT8 ubNextEliteCell;
};

struct ENEMY_LOADOUT_PLAN
{
	ENEMY_LOADOUT_ROLE Role;
	ENEMY_LBE_PROFILE LBEProfile;
	ENEMY_OPTIC_PROFILE OpticProfile;

	// Carry/load targets.  The actual item chooser will translate these to
	// concrete magazines, belts, rockets, grenades and LBE pocket layouts.
	UINT8 ubAmmoMinimum;
	UINT8 ubAmmoMaximum;
	UINT8 ubGrenadeMinimum;
	UINT8 ubGrenadeMaximum;
	UINT8 ubSmokeMinimum;
	UINT8 ubSmokeMaximum;

	// Attachment budget is a useful upper bound, not a command to fill every
	// available attachment slot.
	UINT8 ubAttachmentMinimum;
	UINT8 ubAttachmentMaximum;

	BOOLEAN fPreferBipod;
	BOOLEAN fPreferLaser;
	BOOLEAN fAllowSuppressor;
	BOOLEAN fPreferNightEquipment;
	BOOLEAN fUseBackpack;
	BOOLEAN fHeavyWeapon;
};

// Splits a creation batch into balanced 5-10 man equipment cells aligned
// with the tactical AI fireteam scale (target 8, normal max 9/10).
// Returns number of cells written to pubSizes.
UINT8 PlanEnemyLoadoutCellSizes(
	UINT8 ubTotalSoldiers,
	UINT8 *pubSizes,
	UINT8 ubCapacity);

// Builds class-balanced equipment cells for one creation batch.  This does not
// create soldiers or modify inventory; it is safe to use for dry-run audits.
void BuildEnemyLoadoutBatch(
	ENEMY_LOADOUT_BATCH *pBatch,
	UINT8 ubAdmins,
	UINT8 ubRegulars,
	UINT8 ubElites,
	UINT8 ubProgress,
	INT8 bEquipmentRating);

// Fills the role-ticket counts for an already class-balanced cell and maps
// those tickets back to admin/regular/elite soldiers by suitability.
void PlanEnemyLoadoutCellRoles(
	ENEMY_LOADOUT_CELL *pCell,
	UINT8 ubProgress,
	INT8 bEquipmentRating);

// Diagnostic score used when assigning a planned role ticket to a soldier
// class.  Higher is a better doctrinal fit.
INT16 EnemyRoleClassSuitability(
	ENEMY_LOADOUT_ROLE Role,
	INT8 bSoldierClass);

// Returns the doctrine class for a mixed cell.  A token elite should not turn
// an admin-heavy security element into an elite assault team.
INT8 EnemyLoadoutDoctrineClass(
	UINT8 ubAdmins,
	UINT8 ubRegulars,
	UINT8 ubElites);

// Builds squad-level role targets.  Progress is 0..100.
// Equipment rating uses the existing GenerateRandomEquipment 0..4 scale.
void BuildEnemyRoleTargets(
	ENEMY_ROLE_TARGETS *pTargets,
	INT8 bSoldierClass,
	UINT8 ubSquadSize,
	UINT8 ubProgress,
	INT8 bEquipmentRating);

// Initializes per-squad state for role allocation.
void InitEnemySquadLoadoutState(
	ENEMY_SQUAD_LOADOUT_STATE *pState,
	INT8 bSoldierClass,
	UINT8 ubSquadSize,
	UINT8 ubProgress,
	INT8 bEquipmentRating);

// Chooses the next role while respecting desired composition and hard caps.
// The caller records the result via RecordEnemyLoadoutRole().
ENEMY_LOADOUT_ROLE ChooseEnemyLoadoutRole(
	const ENEMY_SQUAD_LOADOUT_STATE *pState,
	INT8 bSoldierClass,
	UINT8 ubProgress,
	INT8 bEquipmentRating,
	INT8 bExpLevel);

// Records a role after the caller accepts it.
void RecordEnemyLoadoutRole(
	ENEMY_SQUAD_LOADOUT_STATE *pState,
	ENEMY_LOADOUT_ROLE Role);

// Creates a consumable copy of a planned batch.  Role-ticket consumption is
// deterministic and does not consume game RNG.
void InitEnemyLoadoutAssignmentState(
	ENEMY_LOADOUT_ASSIGNMENT_STATE *pAssignment,
	const ENEMY_LOADOUT_BATCH *pBatch);

// Dispenses the next preplanned role ticket for a soldier class and identifies
// the equipment cell it belongs to.  The original batch remains unchanged.
BOOLEAN ConsumeEnemyLoadoutRoleTicket(
	ENEMY_LOADOUT_ASSIGNMENT_STATE *pAssignment,
	INT8 bSoldierClass,
	UINT8 *pubCell,
	ENEMY_LOADOUT_ROLE *pRole);

// Convenience wrapper: consumes a ticket and builds its loadout intent.
BOOLEAN ConsumeEnemyLoadoutPlan(
	ENEMY_LOADOUT_ASSIGNMENT_STATE *pAssignment,
	INT8 bSoldierClass,
	INT8 bExpLevel,
	UINT8 ubProgress,
	INT8 bEquipmentRating,
	BOOLEAN fNight,
	UINT8 *pubCell,
	ENEMY_LOADOUT_PLAN *pPlan);

// Converts a role + progression into LBE, ammunition and attachment intent.
void BuildEnemyLoadoutPlan(
	ENEMY_LOADOUT_PLAN *pPlan,
	ENEMY_LOADOUT_ROLE Role,
	INT8 bSoldierClass,
	UINT8 ubProgress,
	INT8 bEquipmentRating,
	INT8 bExpLevel,
	BOOLEAN fNight);

// Deterministic scoring helpers for the future role-aware item selectors.
// Negative large scores mean "do not use"; no RNG is consumed.
INT32 ScoreEnemyAttachmentForPlan(
	const ENEMY_LOADOUT_PLAN *pPlan,
	UINT16 usBaseItem,
	UINT16 usAttachment,
	UINT8 ubMaxCoolness);

INT32 ScoreEnemyLBEForPlan(
	const ENEMY_LOADOUT_PLAN *pPlan,
	UINT16 usLBEItem,
	UINT8 ubMaxCoolness);

// Deterministic audit selectors.  They search the same class-specific pools as
// the live equipment system but do not consume game RNG and are not wired into
// soldier generation yet.
UINT16 SelectBestEnemyAttachmentForPlan(
	const ENEMY_LOADOUT_PLAN *pPlan,
	INT8 bSoldierClass,
	UINT16 usBaseItem,
	UINT8 ubItemChoiceType,
	UINT8 ubMaxCoolness);

UINT16 SelectBestEnemyLBEForPlan(
	const ENEMY_LOADOUT_PLAN *pPlan,
	INT8 bSoldierClass,
	UINT8 ubMaxCoolness,
	INT8 bRequiredLBEClass);

// Builds a coherent LBE recommendation.  At most one back-carried pack is
// recommended; ordinary rifle/assault roles are not given backpacks merely
// because late-game gear is available.
void BuildBestEnemyLBEPackageForPlan(
	ENEMY_LBE_PACKAGE *pPackage,
	const ENEMY_LOADOUT_PLAN *pPlan,
	INT8 bSoldierClass,
	UINT8 ubMaxCoolness);

// Builds a compatible attachment package on a temporary weapon object.  The
// package respects the role budget and does not fill slots merely because they
// exist.  This remains an audit/planning helper until explicitly integrated.
void BuildBestEnemyAttachmentPackageForPlan(
	ENEMY_ATTACHMENT_PACKAGE *pPackage,
	const ENEMY_LOADOUT_PLAN *pPlan,
	INT8 bSoldierClass,
	UINT16 usBaseItem,
	UINT8 ubMaxCoolness);

// Structural audit helpers for isolated testing/instrumentation.
BOOLEAN ValidateEnemyLoadoutBatch(const ENEMY_LOADOUT_BATCH *pBatch);
BOOLEAN ValidateEnemyLoadoutPlan(const ENEMY_LOADOUT_PLAN *pPlan);

const char *EnemyLoadoutRoleName(ENEMY_LOADOUT_ROLE Role);

#endif
