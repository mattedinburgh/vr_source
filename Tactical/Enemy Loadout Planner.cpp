#ifdef PRECOMPILEDHEADERS
	#include "Tactical All.h"
#else
	#include <memory.h>
	#include "Enemy Loadout Planner.h"
	#include "Soldier Control.h"
	#include "Random.h"
	#include "Items.h"
	#include "Item Types.h"
#endif

#include "Enemy Loadout Planner.h"

// Compiled only on the isolated work branch.  Nothing in the live enemy
// creation/equipment path calls this module yet.

static UINT8 ClampU8(INT32 value, UINT8 minValue, UINT8 maxValue)
{
	if ( value < minValue )
		return minValue;
	if ( value > maxValue )
		return maxValue;
	return (UINT8)value;
}

static BOOLEAN IsRegularOrEliteEnemy(INT8 bSoldierClass)
{
	return (bSoldierClass == SOLDIER_CLASS_ARMY || bSoldierClass == SOLDIER_CLASS_ELITE);
}

static BOOLEAN IsEliteEnemy(INT8 bSoldierClass)
{
	return (bSoldierClass == SOLDIER_CLASS_ELITE);
}

UINT8 PlanEnemyLoadoutCellSizes(
	UINT8 ubTotalSoldiers,
	UINT8 *pubSizes,
	UINT8 ubCapacity)
{
	UINT8 ubMinCells;
	UINT8 ubMaxCells;
	UINT8 ubIdealCells;
	UINT8 ubCells;
	UINT8 ubBase;
	UINT8 ubRemainder;
	UINT8 i;

	if ( ubTotalSoldiers == 0 || !pubSizes || ubCapacity == 0 )
		return 0;

	// Very small encounters remain one undersized cell.  Once there are enough
	// troops for proper elements, enforce the 5-10 range and bias toward 8.
	if ( ubTotalSoldiers < ENEMY_LOADOUT_TEAM_MIN )
	{
		pubSizes[0] = ubTotalSoldiers;
		return 1;
	}

	ubMinCells = (UINT8)((ubTotalSoldiers + ENEMY_LOADOUT_TEAM_MAX - 1) / ENEMY_LOADOUT_TEAM_MAX);
	ubMaxCells = (UINT8)(ubTotalSoldiers / ENEMY_LOADOUT_TEAM_MIN);
	if ( ubMaxCells == 0 )
		ubMaxCells = 1;

	ubIdealCells = (UINT8)((ubTotalSoldiers + (ENEMY_LOADOUT_TEAM_TARGET / 2)) / ENEMY_LOADOUT_TEAM_TARGET);
	if ( ubIdealCells < ubMinCells )
		ubIdealCells = ubMinCells;
	if ( ubIdealCells > ubMaxCells )
		ubIdealCells = ubMaxCells;

	ubCells = ubIdealCells;
	if ( ubCells > ubCapacity )
		return 0;

	ubBase = (UINT8)(ubTotalSoldiers / ubCells);
	ubRemainder = (UINT8)(ubTotalSoldiers % ubCells);

	for ( i = 0; i < ubCells; ++i )
		pubSizes[i] = (UINT8)(ubBase + (i < ubRemainder ? 1 : 0));

	return ubCells;
}

INT8 EnemyLoadoutDoctrineClass(
	UINT8 ubAdmins,
	UINT8 ubRegulars,
	UINT8 ubElites)
{
	UINT16 ubTotal = (UINT16)ubAdmins + ubRegulars + ubElites;

	if ( ubTotal == 0 )
		return SOLDIER_CLASS_ADMINISTRATOR;

	// A genuinely elite-majority cell gets elite doctrine.  Otherwise regular
	// doctrine is used when line troops + elites form the majority.  This keeps
	// one attached elite/NCO from upgrading an admin-heavy security cell.
	if ( (UINT16)ubElites * 2 >= ubTotal )
		return SOLDIER_CLASS_ELITE;

	if ( (UINT16)(ubRegulars + ubElites) * 2 >= ubTotal )
		return SOLDIER_CLASS_ARMY;

	return SOLDIER_CLASS_ADMINISTRATOR;
}

static UINT8 EnemyLoadoutCellFilled(const ENEMY_LOADOUT_CELL *pCell)
{
	if ( !pCell )
		return 0;

	return (UINT8)(pCell->ubAdmins + pCell->ubRegulars + pCell->ubElites);
}

static void AddClassMembersToCells(
	ENEMY_LOADOUT_BATCH *pBatch,
	UINT8 ubCount,
	INT8 bSoldierClass,
	UINT8 ubStartCell)
{
	UINT8 ubCell;
	UINT8 ubSearches;

	if ( !pBatch || pBatch->ubCellCount == 0 )
		return;

	ubCell = (UINT8)(ubStartCell % pBatch->ubCellCount);

	while ( ubCount > 0 )
	{
		ubSearches = 0;
		while ( ubSearches < pBatch->ubCellCount &&
				EnemyLoadoutCellFilled(&pBatch->Cells[ubCell]) >= pBatch->Cells[ubCell].ubSize )
		{
			ubCell = (UINT8)((ubCell + 1) % pBatch->ubCellCount);
			++ubSearches;
		}

		if ( ubSearches >= pBatch->ubCellCount )
			return;

		switch ( bSoldierClass )
		{
			case SOLDIER_CLASS_ELITE:
				++pBatch->Cells[ubCell].ubElites;
				break;
			case SOLDIER_CLASS_ARMY:
				++pBatch->Cells[ubCell].ubRegulars;
				break;
			case SOLDIER_CLASS_ADMINISTRATOR:
			default:
				++pBatch->Cells[ubCell].ubAdmins;
				break;
		}

		--ubCount;
		ubCell = (UINT8)((ubCell + 1) % pBatch->ubCellCount);
	}
}

INT16 EnemyRoleClassSuitability(
	ENEMY_LOADOUT_ROLE Role,
	INT8 bSoldierClass)
{
	switch ( Role )
	{
		case ENEMY_ROLE_SQUAD_LEADER:
			if ( bSoldierClass == SOLDIER_CLASS_ELITE ) return 100;
			if ( bSoldierClass == SOLDIER_CLASS_ARMY ) return 80;
			return 35;

		case ENEMY_ROLE_SNIPER:
			if ( bSoldierClass == SOLDIER_CLASS_ELITE ) return 100;
			if ( bSoldierClass == SOLDIER_CLASS_ARMY ) return 45;
			return -100;

		case ENEMY_ROLE_SCOUT:
			if ( bSoldierClass == SOLDIER_CLASS_ELITE ) return 95;
			if ( bSoldierClass == SOLDIER_CLASS_ARMY ) return 70;
			return 25;

		case ENEMY_ROLE_RADIO_OPERATOR:
			if ( bSoldierClass == SOLDIER_CLASS_ELITE ) return 90;
			if ( bSoldierClass == SOLDIER_CLASS_ARMY ) return 75;
			return 10;

		case ENEMY_ROLE_MEDIC:
			if ( bSoldierClass == SOLDIER_CLASS_ELITE ) return 85;
			if ( bSoldierClass == SOLDIER_CLASS_ARMY ) return 80;
			return 15;

		case ENEMY_ROLE_MARKSMAN:
			if ( bSoldierClass == SOLDIER_CLASS_ELITE ) return 90;
			if ( bSoldierClass == SOLDIER_CLASS_ARMY ) return 75;
			return 25;

		case ENEMY_ROLE_AUTOMATIC_RIFLEMAN:
			if ( bSoldierClass == SOLDIER_CLASS_ARMY ) return 90;
			if ( bSoldierClass == SOLDIER_CLASS_ELITE ) return 80;
			return 20;

		case ENEMY_ROLE_GRENADIER:
			if ( bSoldierClass == SOLDIER_CLASS_ARMY ) return 90;
			if ( bSoldierClass == SOLDIER_CLASS_ELITE ) return 80;
			return 25;

		case ENEMY_ROLE_AT_SPECIALIST:
			if ( bSoldierClass == SOLDIER_CLASS_ARMY ) return 90;
			if ( bSoldierClass == SOLDIER_CLASS_ELITE ) return 85;
			return 10;

		case ENEMY_ROLE_MORTAR:
			if ( bSoldierClass == SOLDIER_CLASS_ARMY ) return 90;
			if ( bSoldierClass == SOLDIER_CLASS_ELITE ) return 80;
			return -100;

		case ENEMY_ROLE_ASSAULT:
			if ( bSoldierClass == SOLDIER_CLASS_ELITE ) return 85;
			if ( bSoldierClass == SOLDIER_CLASS_ARMY ) return 80;
			return 55;

		case ENEMY_ROLE_RIFLEMAN:
		default:
			// Preserve elite personnel for roles where their training matters.
			if ( bSoldierClass == SOLDIER_CLASS_ADMINISTRATOR ) return 85;
			if ( bSoldierClass == SOLDIER_CLASS_ARMY ) return 80;
			return 60;
	}
}

static INT8 EnemyLoadoutNominalExperience(INT8 bDoctrineClass, UINT8 ubProgress)
{
	INT8 bExpLevel;

	switch ( bDoctrineClass )
	{
		case SOLDIER_CLASS_ELITE:
			bExpLevel = (INT8)(5 + ubProgress / 25);
			return (INT8)__min(9, bExpLevel);

		case SOLDIER_CLASS_ARMY:
			bExpLevel = (INT8)(3 + ubProgress / 25);
			return (INT8)__min(7, bExpLevel);

		case SOLDIER_CLASS_ADMINISTRATOR:
		default:
			bExpLevel = (INT8)(2 + ubProgress / 33);
			return (INT8)__min(5, bExpLevel);
	}
}

static void AssignRoleTicketToBestClass(
	ENEMY_LOADOUT_CELL *pCell,
	ENEMY_LOADOUT_ROLE Role,
	UINT8 *pubAdminsRemaining,
	UINT8 *pubRegularsRemaining,
	UINT8 *pubElitesRemaining)
{
	INT16 sAdmin = -32767;
	INT16 sRegular = -32767;
	INT16 sElite = -32767;

	if ( !pCell )
		return;

	if ( pubAdminsRemaining && *pubAdminsRemaining > 0 )
		sAdmin = (INT16)(EnemyRoleClassSuitability(Role, SOLDIER_CLASS_ADMINISTRATOR) + *pubAdminsRemaining);
	if ( pubRegularsRemaining && *pubRegularsRemaining > 0 )
		sRegular = (INT16)(EnemyRoleClassSuitability(Role, SOLDIER_CLASS_ARMY) + *pubRegularsRemaining);
	if ( pubElitesRemaining && *pubElitesRemaining > 0 )
		sElite = (INT16)(EnemyRoleClassSuitability(Role, SOLDIER_CLASS_ELITE) + *pubElitesRemaining);

	if ( sElite >= sRegular && sElite >= sAdmin && pubElitesRemaining && *pubElitesRemaining > 0 )
	{
		++pCell->ubEliteRoleCount[Role];
		--(*pubElitesRemaining);
	}
	else if ( sRegular >= sAdmin && pubRegularsRemaining && *pubRegularsRemaining > 0 )
	{
		++pCell->ubRegularRoleCount[Role];
		--(*pubRegularsRemaining);
	}
	else if ( pubAdminsRemaining && *pubAdminsRemaining > 0 )
	{
		++pCell->ubAdminRoleCount[Role];
		--(*pubAdminsRemaining);
	}
}

void PlanEnemyLoadoutCellRoles(
	ENEMY_LOADOUT_CELL *pCell,
	UINT8 ubProgress,
	INT8 bEquipmentRating)
{
	static const ENEMY_LOADOUT_ROLE roleAssignmentPriority[] =
	{
		ENEMY_ROLE_SQUAD_LEADER,
		ENEMY_ROLE_SNIPER,
		ENEMY_ROLE_RADIO_OPERATOR,
		ENEMY_ROLE_MEDIC,
		ENEMY_ROLE_MARKSMAN,
		ENEMY_ROLE_AT_SPECIALIST,
		ENEMY_ROLE_MORTAR,
		ENEMY_ROLE_AUTOMATIC_RIFLEMAN,
		ENEMY_ROLE_GRENADIER,
		ENEMY_ROLE_SCOUT,
		ENEMY_ROLE_ASSAULT,
		ENEMY_ROLE_RIFLEMAN
	};
	UINT8 ubAdminsRemaining;
	UINT8 ubRegularsRemaining;
	UINT8 ubElitesRemaining;
	INT8 bNominalExp;
	UINT8 i;
	UINT8 j;

	if ( !pCell || pCell->ubSize == 0 )
		return;

	memset(pCell->ubRoleCount, 0, sizeof(pCell->ubRoleCount));
	memset(pCell->ubAdminRoleCount, 0, sizeof(pCell->ubAdminRoleCount));
	memset(pCell->ubRegularRoleCount, 0, sizeof(pCell->ubRegularRoleCount));
	memset(pCell->ubEliteRoleCount, 0, sizeof(pCell->ubEliteRoleCount));
	memset(pCell->State.ubAssigned, 0, sizeof(pCell->State.ubAssigned));
	pCell->State.ubAssignedSoldiers = 0;

	bNominalExp = EnemyLoadoutNominalExperience(pCell->bDoctrineClass, ubProgress);

	// First decide the cell's role mix independent of creation order.
	for ( i = 0; i < pCell->ubSize; ++i )
	{
		ENEMY_LOADOUT_ROLE Role = ChooseEnemyLoadoutRole(
			&pCell->State,
			pCell->bDoctrineClass,
			ubProgress,
			bEquipmentRating,
			bNominalExp);

		++pCell->ubRoleCount[Role];
		RecordEnemyLoadoutRole(&pCell->State, Role);
	}

	// Then map specialist tickets to the most suitable available classes.
	// Specialized roles are assigned first so elite/regular personnel are not
	// accidentally consumed as ordinary riflemen merely due to insertion order.
	ubAdminsRemaining = pCell->ubAdmins;
	ubRegularsRemaining = pCell->ubRegulars;
	ubElitesRemaining = pCell->ubElites;

	for ( i = 0; i < sizeof(roleAssignmentPriority) / sizeof(roleAssignmentPriority[0]); ++i )
	{
		ENEMY_LOADOUT_ROLE Role = roleAssignmentPriority[i];
		for ( j = 0; j < pCell->ubRoleCount[Role]; ++j )
		{
			AssignRoleTicketToBestClass(
				pCell,
				Role,
				&ubAdminsRemaining,
				&ubRegularsRemaining,
				&ubElitesRemaining);
		}
	}
}

void BuildEnemyLoadoutBatch(
	ENEMY_LOADOUT_BATCH *pBatch,
	UINT8 ubAdmins,
	UINT8 ubRegulars,
	UINT8 ubElites,
	UINT8 ubProgress,
	INT8 bEquipmentRating)
{
	UINT16 usTotal;
	UINT8 ubSizes[ENEMY_LOADOUT_MAX_CELLS];
	UINT8 i;

	if ( !pBatch )
		return;

	memset(pBatch, 0, sizeof(ENEMY_LOADOUT_BATCH));
	memset(ubSizes, 0, sizeof(ubSizes));

	usTotal = (UINT16)ubAdmins + ubRegulars + ubElites;
	if ( usTotal == 0 )
		return;

	// Tactical enemy counts are far below 255 in normal play.  Keep the audit
	// structure bounded and fail closed rather than silently wrapping.
	if ( usTotal > 255 )
		return;

	pBatch->ubTotalSoldiers = (UINT8)usTotal;
	pBatch->ubAdmins = ubAdmins;
	pBatch->ubRegulars = ubRegulars;
	pBatch->ubElites = ubElites;
	pBatch->ubCellCount = PlanEnemyLoadoutCellSizes(
		pBatch->ubTotalSoldiers,
		ubSizes,
		ENEMY_LOADOUT_MAX_CELLS);

	if ( pBatch->ubCellCount == 0 )
		return;

	for ( i = 0; i < pBatch->ubCellCount; ++i )
		pBatch->Cells[i].ubSize = ubSizes[i];

	// Spread higher-value personnel first so command/specialist capability is
	// not accidentally concentrated in one cell merely because of creation
	// order.  Regulars and admins then fill the same balanced capacities.
	AddClassMembersToCells(pBatch, ubElites, SOLDIER_CLASS_ELITE, 0);
	AddClassMembersToCells(pBatch, ubRegulars, SOLDIER_CLASS_ARMY, 0);
	AddClassMembersToCells(pBatch, ubAdmins, SOLDIER_CLASS_ADMINISTRATOR, 0);

	for ( i = 0; i < pBatch->ubCellCount; ++i )
	{
		ENEMY_LOADOUT_CELL *pCell = &pBatch->Cells[i];

		pCell->bDoctrineClass = EnemyLoadoutDoctrineClass(
			pCell->ubAdmins,
			pCell->ubRegulars,
			pCell->ubElites);

		InitEnemySquadLoadoutState(
			&pCell->State,
			pCell->bDoctrineClass,
			pCell->ubSize,
			ubProgress,
			bEquipmentRating);

		PlanEnemyLoadoutCellRoles(
			pCell,
			ubProgress,
			bEquipmentRating);
	}
}

static void SetRoleTarget(ENEMY_ROLE_TARGETS *pTargets, ENEMY_LOADOUT_ROLE Role, UINT8 desired, UINT8 maximum)
{
	pTargets->ubDesired[Role] = desired;
	pTargets->ubMaximum[Role] = maximum;
}

static UINT8 CountDesiredSpecialists(const ENEMY_ROLE_TARGETS *pTargets)
{
	UINT8 count = 0;
	UINT8 i;

	for ( i = 0; i < ENEMY_ROLE_MAX; ++i )
	{
		if ( i != ENEMY_ROLE_RIFLEMAN )
			count = (UINT8)(count + pTargets->ubDesired[i]);
	}

	return count;
}

static void ProtectRifleCore(
	ENEMY_ROLE_TARGETS *pTargets,
	UINT8 ubSquadSize,
	UINT8 minimumRiflemen,
	UINT8 optionalSlots)
{
	static const ENEMY_LOADOUT_ROLE demotionOrder[] =
	{
		ENEMY_ROLE_MEDIC,
		ENEMY_ROLE_MARKSMAN,
		ENEMY_ROLE_GRENADIER,
		ENEMY_ROLE_ASSAULT,
		ENEMY_ROLE_AUTOMATIC_RIFLEMAN
	};
	UINT8 specialistCount;
	UINT8 i;

	if ( !pTargets )
		return;

	specialistCount = CountDesiredSpecialists(pTargets);

	for ( i = 0;
		  i < sizeof(demotionOrder) / sizeof(demotionOrder[0]) &&
		  specialistCount + minimumRiflemen + optionalSlots > ubSquadSize;
		  ++i )
	{
		ENEMY_LOADOUT_ROLE role = demotionOrder[i];

		if ( pTargets->ubDesired[role] > 0 )
		{
			pTargets->ubDesired[role]--;
			specialistCount--;
			// Maximum is left intact, so the role can still occupy a controlled
			// optional slot if the final composition has room.
		}
	}
}

void BuildEnemyRoleTargets(
	ENEMY_ROLE_TARGETS *pTargets,
	INT8 bSoldierClass,
	UINT8 ubSquadSize,
	UINT8 ubProgress,
	INT8 bEquipmentRating)
{
	UINT8 mandatoryRoles = 0;
	UINT8 optionalSlots = 0;
	UINT8 minimumRiflemen = 0;

	if ( !pTargets )
		return;

	memset(pTargets, 0, sizeof(ENEMY_ROLE_TARGETS));

	if ( ubSquadSize == 0 )
		return;

	// Administrators remain security/irregular troops.  Even late in the
	// campaign they should not converge on line-infantry specialist density.
	if ( bSoldierClass == SOLDIER_CLASS_ADMINISTRATOR )
	{
		SetRoleTarget(
			pTargets,
			ENEMY_ROLE_SQUAD_LEADER,
			(ubProgress >= 25 && ubSquadSize >= 5) ? 1 : 0,
			1);

		SetRoleTarget(
			pTargets,
			ENEMY_ROLE_ASSAULT,
			(ubProgress >= 20 && ubSquadSize >= 5) ? 1 : 0,
			(ubSquadSize >= 8) ? 2 : 1);

		// Marksman/grenadier are optional late security-force specialists.
		SetRoleTarget(
			pTargets,
			ENEMY_ROLE_MARKSMAN,
			0,
			(ubProgress >= 65 && ubSquadSize >= 8) ? 1 : 0);
		SetRoleTarget(
			pTargets,
			ENEMY_ROLE_GRENADIER,
			0,
			(ubProgress >= 55 && ubSquadSize >= 8) ? 1 : 0);

		mandatoryRoles =
			pTargets->ubDesired[ENEMY_ROLE_SQUAD_LEADER] +
			pTargets->ubDesired[ENEMY_ROLE_ASSAULT];

		optionalSlots = (ubProgress >= 55 && ubSquadSize >= 8) ? 1 : 0;
		minimumRiflemen = 1;

		SetRoleTarget(
			pTargets,
			ENEMY_ROLE_RIFLEMAN,
			(UINT8)max((INT32)minimumRiflemen, (INT32)ubSquadSize - mandatoryRoles - optionalSlots),
			ubSquadSize);
		return;
	}

	if ( !IsRegularOrEliteEnemy(bSoldierClass) )
	{
		SetRoleTarget(pTargets, ENEMY_ROLE_RIFLEMAN, ubSquadSize, ubSquadSize);
		return;
	}

	// Core professional roles.  These become normal parts of a squad as the
	// campaign develops, but we deliberately leave free slots for controlled
	// variation (AT/radio/sniper/scout) rather than making every support role
	// mandatory at once.
	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_SQUAD_LEADER,
		(ubSquadSize >= 5) ? 1 : 0,
		(ubSquadSize >= 5)
			? ((ubSquadSize >= 14 && IsEliteEnemy(bSoldierClass)) ? 2 : 1)
			: 0);

	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_AUTOMATIC_RIFLEMAN,
		(ubProgress >= 20 && ubSquadSize >= 5) ? 1 : 0,
		(ubProgress >= 20 && ubSquadSize >= 5)
			? ClampU8((ubSquadSize + 5) / 6, 1, 3)
			: 0);

	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_GRENADIER,
		(ubProgress >= 30 && ubSquadSize >= 6) ? 1 : 0,
		(ubProgress >= 30 && ubSquadSize >= 6)
			? ClampU8((ubSquadSize + 4) / 5, 1, 3)
			: 0);

	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_MARKSMAN,
		(ubProgress >= 40 && ubSquadSize >= 7) ? 1 : 0,
		(ubProgress >= 40 && ubSquadSize >= 7)
			? ClampU8((ubSquadSize + 7) / 8, 1, 2)
			: 0);

	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_ASSAULT,
		(ubProgress >= 20 && ubSquadSize >= 5) ? 1 : 0,
		(ubProgress >= 20 && ubSquadSize >= 5)
			? ClampU8((ubSquadSize + 6) / 7, 1, 3)
			: 0);

	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_MEDIC,
		((ubProgress >= (IsEliteEnemy(bSoldierClass) ? 40 : 50)) && ubSquadSize >= 9) ? 1 : 0,
		((ubProgress >= (IsEliteEnemy(bSoldierClass) ? 40 : 50)) && ubSquadSize >= 9)
			? ((ubSquadSize >= 16) ? 2 : 1)
			: 0);

	// Optional roles.  Desired stays zero; the planner can spend reserved
	// variation slots on these according to availability and caps.
	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_SNIPER,
		0,
		(IsEliteEnemy(bSoldierClass) && ubProgress >= 65 && ubSquadSize >= 8)
			? ((ubSquadSize >= 16) ? 2 : 1)
			: 0);

	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_AT_SPECIALIST,
		0,
		(ubProgress >= 40 && ubSquadSize >= 7)
			? ((ubSquadSize >= 14 && ubProgress >= 70) ? 2 : 1)
			: 0);

	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_RADIO_OPERATOR,
		0,
		((ubProgress >= (IsEliteEnemy(bSoldierClass) ? 45 : 55)) && ubSquadSize >= 7)
			? ((ubSquadSize >= 16) ? 2 : 1)
			: 0);

	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_SCOUT,
		0,
		(IsEliteEnemy(bSoldierClass) && ubProgress >= 45 && ubSquadSize >= 7)
			? ((ubSquadSize >= 14) ? 2 : 1)
			: 0);

	// Mortar is a sector/group fire-support role, never a normal per-soldier
	// desired slot.  It stays unavailable to the chooser until that group-level
	// allocation step is explicitly implemented.
	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_MORTAR,
		0,
		(ubProgress >= 55 && ubSquadSize >= 10) ? 1 : 0);

	// Reserve one variable specialist slot once the army is established.
	// Late elite groups may reserve two.  This variation budget is protected:
	// lower-priority "mandatory" roles are demoted before we sacrifice it.
	if ( ubProgress >= 40 && ubSquadSize >= 8 )
		optionalSlots = 1;
	if ( IsEliteEnemy(bSoldierClass) && ubProgress >= 70 && ubSquadSize >= 10 )
		optionalSlots = 2;

	// Keep a meaningful rifle core.  30% is a floor.  If the core roles plus
	// the variation budget do not fit, demote lower-priority roles back to
	// optional status rather than eliminating either riflemen or variation.
	minimumRiflemen = ClampU8((ubSquadSize * 3 + 9) / 10, 1, ubSquadSize);
	if ( minimumRiflemen + optionalSlots > ubSquadSize )
		optionalSlots = (UINT8)(ubSquadSize - minimumRiflemen);

	ProtectRifleCore(pTargets, ubSquadSize, minimumRiflemen, optionalSlots);
	mandatoryRoles = CountDesiredSpecialists(pTargets);

	// A final defensive clamp for pathological future target changes.
	if ( mandatoryRoles + minimumRiflemen + optionalSlots > ubSquadSize )
		optionalSlots = (UINT8)__max(0, (INT32)ubSquadSize - mandatoryRoles - minimumRiflemen);

	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_RIFLEMAN,
		(UINT8)(ubSquadSize - mandatoryRoles - optionalSlots),
		ubSquadSize);
}

void InitEnemySquadLoadoutState(
	ENEMY_SQUAD_LOADOUT_STATE *pState,
	INT8 bSoldierClass,
	UINT8 ubSquadSize,
	UINT8 ubProgress,
	INT8 bEquipmentRating)
{
	if ( !pState )
		return;

	memset(pState, 0, sizeof(ENEMY_SQUAD_LOADOUT_STATE));
	pState->ubPlannedSoldiers = ubSquadSize;
	BuildEnemyRoleTargets(&pState->Targets, bSoldierClass, ubSquadSize, ubProgress, bEquipmentRating);
}

static INT16 RoleNeedScore(const ENEMY_SQUAD_LOADOUT_STATE *pState, ENEMY_LOADOUT_ROLE Role)
{
	INT16 desired;
	INT16 assigned;
	INT16 maximum;

	desired = pState->Targets.ubDesired[Role];
	assigned = pState->ubAssigned[Role];
	maximum = pState->Targets.ubMaximum[Role];

	if ( maximum == 0 || assigned >= maximum )
		return -32767;

	// Strongly prioritize roles still below their desired count.
	if ( assigned < desired )
		return (INT16)(100 + (desired - assigned) * 20);

	// Optional roles remain possible but low priority.
	return (INT16)(10 - assigned * 2);
}

ENEMY_LOADOUT_ROLE ChooseEnemyLoadoutRole(
	const ENEMY_SQUAD_LOADOUT_STATE *pState,
	INT8 bSoldierClass,
	UINT8 ubProgress,
	INT8 bEquipmentRating,
	INT8 bExpLevel)
{
	ENEMY_LOADOUT_ROLE candidates[ENEMY_ROLE_MAX];
	UINT8 candidateCount = 0;
	INT16 bestScore = -32767;
	UINT8 i;

	if ( !pState )
		return ENEMY_ROLE_RIFLEMAN;

	// Administrators deliberately use a narrower role set.
	if ( bSoldierClass == SOLDIER_CLASS_ADMINISTRATOR )
	{
		const ENEMY_LOADOUT_ROLE adminRoles[] =
		{
			ENEMY_ROLE_SQUAD_LEADER,
			ENEMY_ROLE_ASSAULT,
			ENEMY_ROLE_MARKSMAN,
			ENEMY_ROLE_GRENADIER,
			ENEMY_ROLE_RIFLEMAN
		};
		for ( i = 0; i < sizeof(adminRoles) / sizeof(adminRoles[0]); ++i )
		{
			INT16 score = RoleNeedScore(pState, adminRoles[i]);
			if ( score > bestScore )
			{
				bestScore = score;
				candidateCount = 0;
				candidates[candidateCount++] = adminRoles[i];
			}
			else if ( score == bestScore && candidateCount < ENEMY_ROLE_MAX )
			{
				candidates[candidateCount++] = adminRoles[i];
			}
		}
	}
	else
	{
		for ( i = 0; i < ENEMY_ROLE_MAX; ++i )
		{
			ENEMY_LOADOUT_ROLE role = (ENEMY_LOADOUT_ROLE)i;
			INT16 score = RoleNeedScore(pState, role);

			// Dedicated sniper/heavy roles require a minimum experience floor.
			if ( role == ENEMY_ROLE_SNIPER && bExpLevel < 5 )
				continue;
			if ( role == ENEMY_ROLE_AT_SPECIALIST && bExpLevel < 4 )
				continue;
			if ( role == ENEMY_ROLE_MORTAR && bExpLevel < 5 )
				continue;

			// Mortar remains opt-in until group-level fire support is wired.
			if ( role == ENEMY_ROLE_MORTAR )
				continue;

			if ( score > bestScore )
			{
				bestScore = score;
				candidateCount = 0;
				candidates[candidateCount++] = role;
			}
			else if ( score == bestScore && candidateCount < ENEMY_ROLE_MAX )
			{
				candidates[candidateCount++] = role;
			}
		}
	}

	if ( candidateCount == 0 )
		return ENEMY_ROLE_RIFLEMAN;

	// Controlled randomness only among equally useful roles.
	return candidates[Random(candidateCount)];
}

void RecordEnemyLoadoutRole(ENEMY_SQUAD_LOADOUT_STATE *pState, ENEMY_LOADOUT_ROLE Role)
{
	if ( !pState || Role < 0 || Role >= ENEMY_ROLE_MAX )
		return;

	if ( pState->ubAssigned[Role] < 255 )
		pState->ubAssigned[Role]++;

	if ( pState->ubAssignedSoldiers < 255 )
		pState->ubAssignedSoldiers++;
}

static UINT8 AttachmentBudgetMaximum(ENEMY_LOADOUT_ROLE Role, BOOLEAN fElite, UINT8 ubProgress)
{
	UINT8 maximum = 0;

	if ( ubProgress < 20 )
		maximum = 1;
	else if ( ubProgress < 40 )
		maximum = 2;
	else if ( ubProgress < 60 )
		maximum = 2;
	else if ( ubProgress < 80 )
		maximum = 3;
	else
		maximum = fElite ? 4 : 3;

	if ( Role == ENEMY_ROLE_MARKSMAN || Role == ENEMY_ROLE_SNIPER )
		maximum = ClampU8(maximum + 1, 1, 4);

	return maximum;
}

void BuildEnemyLoadoutPlan(
	ENEMY_LOADOUT_PLAN *pPlan,
	ENEMY_LOADOUT_ROLE Role,
	INT8 bSoldierClass,
	UINT8 ubProgress,
	INT8 bEquipmentRating,
	INT8 bExpLevel,
	BOOLEAN fNight)
{
	BOOLEAN fElite;

	if ( !pPlan )
		return;

	memset(pPlan, 0, sizeof(ENEMY_LOADOUT_PLAN));
	pPlan->Role = Role;
	fElite = IsEliteEnemy(bSoldierClass);

	pPlan->LBEProfile = ENEMY_LBE_STANDARD_RIFLE;
	pPlan->OpticProfile = ENEMY_OPTIC_IRONS;
	pPlan->ubAmmoMinimum = 4;
	pPlan->ubAmmoMaximum = 6;
	pPlan->ubGrenadeMinimum = (ubProgress >= 30) ? 1 : 0;
	pPlan->ubGrenadeMaximum = (ubProgress >= 50) ? 2 : 1;
	pPlan->ubSmokeMinimum = 0;
	pPlan->ubSmokeMaximum = (ubProgress >= 35) ? 1 : 0;
	pPlan->ubAttachmentMinimum = (ubProgress >= 45) ? 1 : 0;
	pPlan->ubAttachmentMaximum = AttachmentBudgetMaximum(Role, fElite, ubProgress);

	switch ( Role )
	{
		case ENEMY_ROLE_ASSAULT:
			pPlan->LBEProfile = ENEMY_LBE_ASSAULT;
			pPlan->OpticProfile = (ubProgress >= 30) ? ENEMY_OPTIC_CLOSE_COMBAT : ENEMY_OPTIC_IRONS;
			pPlan->ubAmmoMinimum = 5;
			pPlan->ubAmmoMaximum = 7;
			pPlan->ubGrenadeMinimum = (ubProgress >= 25) ? 2 : 1;
			pPlan->ubGrenadeMaximum = 3;
			pPlan->ubSmokeMaximum = 2;
			pPlan->fPreferLaser = (ubProgress >= 40);
			break;

		case ENEMY_ROLE_AUTOMATIC_RIFLEMAN:
			pPlan->LBEProfile = ENEMY_LBE_AUTOMATIC;
			pPlan->OpticProfile = (ubProgress >= 55) ? ENEMY_OPTIC_LOW_POWER : ENEMY_OPTIC_IRONS;
			pPlan->ubAmmoMinimum = 5;
			pPlan->ubAmmoMaximum = 8;
			pPlan->ubGrenadeMinimum = 0;
			pPlan->ubGrenadeMaximum = 1;
			pPlan->ubSmokeMaximum = 1;
			pPlan->fPreferBipod = TRUE;
			pPlan->fHeavyWeapon = TRUE;
			break;

		case ENEMY_ROLE_MARKSMAN:
			pPlan->LBEProfile = ENEMY_LBE_LIGHT;
			pPlan->OpticProfile = (ubProgress >= 55) ? ENEMY_OPTIC_MARKSMAN : ENEMY_OPTIC_LOW_POWER;
			pPlan->ubAmmoMinimum = 4;
			pPlan->ubAmmoMaximum = 6;
			pPlan->ubGrenadeMaximum = 1;
			pPlan->fPreferBipod = (ubProgress >= 45);
			break;

		case ENEMY_ROLE_SNIPER:
			pPlan->LBEProfile = ENEMY_LBE_LIGHT;
			pPlan->OpticProfile = ENEMY_OPTIC_SNIPER;
			pPlan->ubAmmoMinimum = 3;
			pPlan->ubAmmoMaximum = 5;
			pPlan->ubGrenadeMinimum = 0;
			pPlan->ubGrenadeMaximum = 1;
			pPlan->ubSmokeMinimum = 1;
			pPlan->ubSmokeMaximum = 2;
			pPlan->fPreferBipod = TRUE;
			pPlan->fAllowSuppressor = fElite && ubProgress >= 70;
			pPlan->fUseBackpack = TRUE;
			break;

		case ENEMY_ROLE_GRENADIER:
			pPlan->LBEProfile = ENEMY_LBE_GRENADIER;
			pPlan->OpticProfile = (ubProgress >= 40) ? ENEMY_OPTIC_LOW_POWER : ENEMY_OPTIC_IRONS;
			pPlan->ubAmmoMinimum = 4;
			pPlan->ubAmmoMaximum = 5;
			pPlan->ubGrenadeMinimum = 4;
			pPlan->ubGrenadeMaximum = 7;
			pPlan->ubSmokeMinimum = 1;
			pPlan->ubSmokeMaximum = 2;
			pPlan->fHeavyWeapon = TRUE;
			break;

		case ENEMY_ROLE_AT_SPECIALIST:
			pPlan->LBEProfile = ENEMY_LBE_HEAVY_SUPPORT;
			pPlan->OpticProfile = (ubProgress >= 45) ? ENEMY_OPTIC_CLOSE_COMBAT : ENEMY_OPTIC_IRONS;
			pPlan->ubAmmoMinimum = 3;
			pPlan->ubAmmoMaximum = 5;
			pPlan->ubGrenadeMaximum = 1;
			pPlan->ubSmokeMaximum = 1;
			pPlan->fHeavyWeapon = TRUE;
			break;

		case ENEMY_ROLE_MEDIC:
			pPlan->LBEProfile = ENEMY_LBE_MEDIC;
			pPlan->OpticProfile = (ubProgress >= 55) ? ENEMY_OPTIC_CLOSE_COMBAT : ENEMY_OPTIC_IRONS;
			pPlan->ubAmmoMinimum = 3;
			pPlan->ubAmmoMaximum = 5;
			pPlan->ubGrenadeMaximum = 1;
			pPlan->ubSmokeMinimum = 1;
			pPlan->ubSmokeMaximum = 2;
			pPlan->fUseBackpack = TRUE;
			break;

		case ENEMY_ROLE_RADIO_OPERATOR:
			pPlan->LBEProfile = ENEMY_LBE_RADIO;
			pPlan->OpticProfile = (ubProgress >= 50) ? ENEMY_OPTIC_CLOSE_COMBAT : ENEMY_OPTIC_IRONS;
			pPlan->ubAmmoMinimum = 4;
			pPlan->ubAmmoMaximum = 5;
			pPlan->ubGrenadeMaximum = 1;
			pPlan->ubSmokeMinimum = 1;
			pPlan->ubSmokeMaximum = 2;
			pPlan->fUseBackpack = TRUE;
			break;

		case ENEMY_ROLE_SQUAD_LEADER:
			pPlan->LBEProfile = ENEMY_LBE_LIGHT;
			pPlan->OpticProfile = (ubProgress >= 35) ? ENEMY_OPTIC_CLOSE_COMBAT : ENEMY_OPTIC_IRONS;
			pPlan->ubAmmoMinimum = 4;
			pPlan->ubAmmoMaximum = 5;
			pPlan->ubGrenadeMinimum = 1;
			pPlan->ubGrenadeMaximum = 2;
			pPlan->ubSmokeMinimum = (ubProgress >= 35) ? 1 : 0;
			pPlan->ubSmokeMaximum = 2;
			break;

		case ENEMY_ROLE_SCOUT:
			pPlan->LBEProfile = ENEMY_LBE_LIGHT;
			pPlan->OpticProfile = (ubProgress >= 45) ? ENEMY_OPTIC_CLOSE_COMBAT : ENEMY_OPTIC_IRONS;
			pPlan->ubAmmoMinimum = 4;
			pPlan->ubAmmoMaximum = 6;
			pPlan->ubGrenadeMaximum = 2;
			pPlan->ubSmokeMinimum = 1;
			pPlan->ubSmokeMaximum = 2;
			pPlan->fPreferLaser = (ubProgress >= 50);
			pPlan->fAllowSuppressor = fElite && ubProgress >= 60;
			break;

		case ENEMY_ROLE_MORTAR:
			pPlan->LBEProfile = ENEMY_LBE_HEAVY_SUPPORT;
			pPlan->OpticProfile = ENEMY_OPTIC_IRONS;
			pPlan->ubAmmoMinimum = 2;
			pPlan->ubAmmoMaximum = 4;
			pPlan->ubGrenadeMinimum = 2;
			pPlan->ubGrenadeMaximum = 4;
			pPlan->fUseBackpack = TRUE;
			pPlan->fHeavyWeapon = TRUE;
			break;

		case ENEMY_ROLE_RIFLEMAN:
		default:
			if ( ubProgress >= 65 )
				pPlan->OpticProfile = ENEMY_OPTIC_LOW_POWER;
			else if ( ubProgress >= 40 )
				pPlan->OpticProfile = ENEMY_OPTIC_CLOSE_COMBAT;
			break;
	}

	// Night equipment is a professionalism multiplier, not a universal late-game
	// entitlement.  Scouts/snipers/elites receive first priority.
	if ( fNight && ubProgress >= 45 )
	{
		if ( fElite || Role == ENEMY_ROLE_SCOUT || Role == ENEMY_ROLE_SNIPER || Role == ENEMY_ROLE_SQUAD_LEADER )
			pPlan->fPreferNightEquipment = TRUE;
	}

	// Suppressors remain specialist equipment, never a generic high-coolness
	// attachment granted solely because the campaign is late.
	if ( Role != ENEMY_ROLE_SCOUT && Role != ENEMY_ROLE_SNIPER )
		pPlan->fAllowSuppressor = FALSE;

	// Low equipment rating should reduce package density while preserving role.
	if ( bEquipmentRating <= 1 )
	{
		pPlan->ubAttachmentMinimum = 0;
		pPlan->ubAttachmentMaximum = ClampU8(pPlan->ubAttachmentMaximum, 0, 1);
		pPlan->fPreferLaser = FALSE;
		pPlan->fAllowSuppressor = FALSE;
	}
	else if ( bEquipmentRating == 2 )
	{
		pPlan->ubAttachmentMaximum = ClampU8(pPlan->ubAttachmentMaximum, 0, 2);
	}

	// Very inexperienced soldiers do not receive specialist-grade optics solely
	// because campaign progress is high.
	if ( bExpLevel < 4 && pPlan->OpticProfile > ENEMY_OPTIC_LOW_POWER )
		pPlan->OpticProfile = ENEMY_OPTIC_LOW_POWER;
}

static INT32 ScoreEnemyOpticForPlan(
	const ENEMY_LOADOUT_PLAN *pPlan,
	UINT16 usAttachment)
{
	UINT64 uiAttachmentClass;
	FLOAT dMagnification;
	INT32 iScore = 0;

	if ( !pPlan || usAttachment == 0 )
		return -10000;

	uiAttachmentClass = Item[usAttachment].nasAttachmentClass;
	if ( !(uiAttachmentClass & (AC_SCOPE | AC_SIGHT | AC_IRONSIGHT)) )
		return 0;

	dMagnification = Item[usAttachment].scopemagfactor;

	switch ( pPlan->OpticProfile )
	{
		case ENEMY_OPTIC_IRONS:
			if ( uiAttachmentClass & AC_IRONSIGHT )
				iScore += 30;
			if ( uiAttachmentClass & AC_SIGHT )
				iScore += 5;
			if ( dMagnification > 1.5f )
				iScore -= 80;
			break;

		case ENEMY_OPTIC_CLOSE_COMBAT:
			if ( Item[usAttachment].speeddot )
				iScore += 45;
			if ( uiAttachmentClass & AC_SIGHT )
				iScore += 25;
			if ( dMagnification <= 1.5f )
				iScore += 35;
			else if ( dMagnification <= 2.25f )
				iScore += 15;
			else if ( dMagnification > 3.0f )
				iScore -= 70;
			break;

		case ENEMY_OPTIC_LOW_POWER:
			if ( dMagnification >= 1.5f && dMagnification <= 3.0f )
				iScore += 55;
			else if ( dMagnification > 3.0f && dMagnification <= 4.5f )
				iScore += 20;
			else if ( dMagnification > 5.0f )
				iScore -= 55;
			else if ( Item[usAttachment].speeddot )
				iScore += 15;
			break;

		case ENEMY_OPTIC_MARKSMAN:
			if ( dMagnification >= 3.0f && dMagnification <= 6.5f )
				iScore += 65;
			else if ( dMagnification > 6.5f && dMagnification <= 8.0f )
				iScore += 30;
			else if ( dMagnification < 2.0f )
				iScore -= 45;
			break;

		case ENEMY_OPTIC_SNIPER:
			if ( dMagnification >= 6.0f && dMagnification <= 10.5f )
				iScore += 75;
			else if ( dMagnification >= 4.0f )
				iScore += 35;
			else
				iScore -= 65;
			break;

		default:
			break;
	}

	return iScore;
}

INT32 ScoreEnemyAttachmentForPlan(
	const ENEMY_LOADOUT_PLAN *pPlan,
	UINT16 usBaseItem,
	UINT16 usAttachment,
	UINT8 ubMaxCoolness)
{
	UINT64 uiAttachmentClass;
	INT32 iScore = 0;
	BOOLEAN fSuppressor;

	if ( !pPlan || usBaseItem == 0 || usAttachment == 0 )
		return -10000;

	if ( !ItemIsLegal(usAttachment) || !ValidAttachment(usAttachment, usBaseItem) )
		return -10000;

	if ( ubMaxCoolness > 0 && Item[usAttachment].ubCoolness > ubMaxCoolness )
		return -10000;

	uiAttachmentClass = Item[usAttachment].nasAttachmentClass;
	fSuppressor = (Item[usAttachment].percentnoisereduction > 0);

	// Suppression devices are a doctrinal choice, not a generic high-coolness
	// bonus.  Roles that are not allowed one reject it outright.
	if ( fSuppressor )
	{
		if ( !pPlan->fAllowSuppressor )
			return -10000;

		iScore += 50;
		if ( pPlan->Role == ENEMY_ROLE_SCOUT || pPlan->Role == ENEMY_ROLE_SNIPER )
			iScore += 20;
	}

	iScore += ScoreEnemyOpticForPlan(pPlan, usAttachment);

	if ( uiAttachmentClass & AC_BIPOD )
	{
		iScore += pPlan->fPreferBipod ? 50 : 5;
		if ( pPlan->Role == ENEMY_ROLE_AUTOMATIC_RIFLEMAN ||
			 pPlan->Role == ENEMY_ROLE_MARKSMAN ||
			 pPlan->Role == ENEMY_ROLE_SNIPER )
		{
			iScore += 15;
		}
	}

	if ( uiAttachmentClass & AC_LASER )
	{
		iScore += pPlan->fPreferLaser ? 40 : 5;
		if ( pPlan->OpticProfile == ENEMY_OPTIC_CLOSE_COMBAT )
			iScore += 10;
	}

	if ( uiAttachmentClass & (AC_FOREGRIP | AC_STOCK) )
	{
		if ( pPlan->Role == ENEMY_ROLE_ASSAULT ||
			 pPlan->Role == ENEMY_ROLE_AUTOMATIC_RIFLEMAN )
			iScore += 25;
		else if ( pPlan->Role == ENEMY_ROLE_RIFLEMAN )
			iScore += 10;
	}

	if ( uiAttachmentClass & AC_SLING )
	{
		if ( pPlan->LBEProfile == ENEMY_LBE_LIGHT ||
			 pPlan->Role == ENEMY_ROLE_MEDIC ||
			 pPlan->Role == ENEMY_ROLE_RADIO_OPERATOR )
		{
			iScore += 15;
		}
		else
			iScore += 5;
	}

	if ( uiAttachmentClass & AC_UNDERBARREL )
	{
		if ( pPlan->Role == ENEMY_ROLE_GRENADIER )
			iScore += 50;
		else
			iScore -= 10;
	}

	// A flash hider has legitimate night value without being treated as a
	// suppressor.  Full suppressors were handled above.
	if ( Item[usAttachment].hidemuzzleflash && !fSuppressor )
	{
		if ( pPlan->fPreferNightEquipment ||
			 pPlan->Role == ENEMY_ROLE_SCOUT ||
			 pPlan->Role == ENEMY_ROLE_SNIPER )
			iScore += 15;
		else
			iScore += 5;
	}

	// Coolness is a secondary quality signal.  It must never overpower a role
	// mismatch, which is why it contributes only a few points.
	iScore += __min((INT32)10, (INT32)Item[usAttachment].ubCoolness);

	return iScore;
}

INT32 ScoreEnemyLBEForPlan(
	const ENEMY_LOADOUT_PLAN *pPlan,
	UINT16 usLBEItem,
	UINT8 ubMaxCoolness)
{
	UINT16 usLBEIndex;
	const LBETYPE *pLBE;
	UINT8 ubActivePockets = 0;
	UINT16 usPocketCapacity = 0;
	INT32 iScore = 0;

	if ( !pPlan || usLBEItem == 0 )
		return -10000;

	if ( Item[usLBEItem].usItemClass != IC_LBEGEAR || !ItemIsLegal(usLBEItem) )
		return -10000;

	if ( ubMaxCoolness > 0 && Item[usLBEItem].ubCoolness > ubMaxCoolness )
		return -10000;

	usLBEIndex = Item[usLBEItem].ubClassIndex;
	if ( usLBEIndex >= LoadBearingEquipment.size() )
		return -10000;

	pLBE = &LoadBearingEquipment[usLBEIndex];

	for (UINT16 i = 0; i < pLBE->lbePocketIndex.size(); ++i)
	{
		UINT8 ubPocket = pLBE->lbePocketIndex[i];
		if ( ubPocket == 0 || ubPocket >= LBEPocketType.size() )
			continue;

		++ubActivePockets;

		UINT8 ubBestCapacity = 0;
		for (UINT16 s = 0; s < LBEPocketType[ubPocket].ItemCapacityPerSize.size(); ++s)
			ubBestCapacity = __max(ubBestCapacity, LBEPocketType[ubPocket].ItemCapacityPerSize[s]);

		usPocketCapacity = (UINT16)__min(
			(UINT32)255,
			(UINT32)usPocketCapacity + ubBestCapacity);
	}

	switch ( pPlan->LBEProfile )
	{
		case ENEMY_LBE_LIGHT:
			if ( pLBE->lbeClass == VEST_PACK ) iScore += 45;
			else if ( pLBE->lbeClass == THIGH_PACK ) iScore += 30;
			else if ( pLBE->lbeClass == COMBAT_PACK ) iScore += 5;
			else if ( pLBE->lbeClass == BACKPACK ) iScore -= 45;
			break;

		case ENEMY_LBE_STANDARD_RIFLE:
			if ( pLBE->lbeClass == VEST_PACK ) iScore += 55;
			else if ( pLBE->lbeClass == THIGH_PACK ) iScore += 25;
			else if ( pLBE->lbeClass == COMBAT_PACK ) iScore += 10;
			else if ( pLBE->lbeClass == BACKPACK ) iScore -= 35;
			break;

		case ENEMY_LBE_ASSAULT:
			if ( pLBE->lbeClass == VEST_PACK ) iScore += 60;
			else if ( pLBE->lbeClass == THIGH_PACK ) iScore += 35;
			else if ( pLBE->lbeClass == COMBAT_PACK ) iScore += 0;
			else if ( pLBE->lbeClass == BACKPACK ) iScore -= 60;
			break;

		case ENEMY_LBE_AUTOMATIC:
			if ( pLBE->lbeClass == VEST_PACK ) iScore += 50;
			else if ( pLBE->lbeClass == THIGH_PACK ) iScore += 45;
			else if ( pLBE->lbeClass == COMBAT_PACK ) iScore += 20;
			else if ( pLBE->lbeClass == BACKPACK ) iScore -= 30;
			break;

		case ENEMY_LBE_GRENADIER:
			if ( pLBE->lbeClass == THIGH_PACK ) iScore += 55;
			else if ( pLBE->lbeClass == VEST_PACK ) iScore += 45;
			else if ( pLBE->lbeClass == COMBAT_PACK ) iScore += 15;
			else if ( pLBE->lbeClass == BACKPACK ) iScore -= 35;
			break;

		case ENEMY_LBE_MEDIC:
			if ( pLBE->lbeClass == COMBAT_PACK ) iScore += 55;
			else if ( pLBE->lbeClass == VEST_PACK ) iScore += 40;
			else if ( pLBE->lbeClass == BACKPACK ) iScore += 25;
			else if ( pLBE->lbeClass == THIGH_PACK ) iScore += 10;
			break;

		case ENEMY_LBE_RADIO:
			if ( pLBE->lbeClass == BACKPACK ) iScore += 55;
			else if ( pLBE->lbeClass == COMBAT_PACK ) iScore += 50;
			else if ( pLBE->lbeClass == VEST_PACK ) iScore += 20;
			break;

		case ENEMY_LBE_HEAVY_SUPPORT:
			if ( pLBE->lbeClass == BACKPACK ) iScore += 55;
			else if ( pLBE->lbeClass == COMBAT_PACK ) iScore += 50;
			else if ( pLBE->lbeClass == THIGH_PACK ) iScore += 25;
			else if ( pLBE->lbeClass == VEST_PACK ) iScore += 20;
			break;

		default:
			break;
	}

	if ( pLBE->lbeClass == BACKPACK )
	{
		if ( pPlan->fUseBackpack )
			iScore += 25;
		else
			iScore -= 80;
	}
	else if ( pLBE->lbeClass == COMBAT_PACK && pPlan->fUseBackpack )
	{
		iScore += 15;
	}

	// Capacity matters most for support roles, but no role should select a
	// gigantic pack solely because it exposes many slots.
	iScore += __min((INT32)32, (INT32)ubActivePockets * 4);
	iScore += __min((INT32)24, (INT32)usPocketCapacity / 2);
	iScore += __min((INT32)20, (INT32)pLBE->lbeAvailableVolume);

	if ( pPlan->LBEProfile == ENEMY_LBE_LIGHT ||
		 pPlan->LBEProfile == ENEMY_LBE_ASSAULT )
	{
		iScore -= (INT32)Item[usLBEItem].ubWeight * 2;
	}
	else if ( pPlan->LBEProfile == ENEMY_LBE_STANDARD_RIFLE )
	{
		iScore -= (INT32)Item[usLBEItem].ubWeight;
	}

	iScore += __min((INT32)8, (INT32)Item[usLBEItem].ubCoolness);

	return iScore;
}

const char *EnemyLoadoutRoleName(ENEMY_LOADOUT_ROLE Role)
{
	switch ( Role )
	{
		case ENEMY_ROLE_RIFLEMAN: return "Rifleman";
		case ENEMY_ROLE_ASSAULT: return "Assault";
		case ENEMY_ROLE_AUTOMATIC_RIFLEMAN: return "Automatic Rifleman";
		case ENEMY_ROLE_MARKSMAN: return "Marksman";
		case ENEMY_ROLE_SNIPER: return "Sniper";
		case ENEMY_ROLE_GRENADIER: return "Grenadier";
		case ENEMY_ROLE_AT_SPECIALIST: return "AT Specialist";
		case ENEMY_ROLE_MEDIC: return "Medic";
		case ENEMY_ROLE_RADIO_OPERATOR: return "Radio Operator";
		case ENEMY_ROLE_SQUAD_LEADER: return "Squad Leader";
		case ENEMY_ROLE_SCOUT: return "Scout";
		case ENEMY_ROLE_MORTAR: return "Mortar";
		default: return "Unknown";
	}
}
