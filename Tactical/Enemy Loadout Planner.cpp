#ifdef PRECOMPILEDHEADERS
	#include "Tactical All.h"
#else
	#include <memory.h>
	#include "Enemy Loadout Planner.h"
	#include "Soldier Control.h"
	#include "Random.h"
#endif

#include "Enemy Loadout Planner.h"

// This file is deliberately not part of the build yet.  It is an isolated
// implementation draft on the work/enemy-loadout-planner branch.

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

static void SetRoleTarget(ENEMY_ROLE_TARGETS *pTargets, ENEMY_LOADOUT_ROLE Role, UINT8 desired, UINT8 maximum)
{
	pTargets->ubDesired[Role] = desired;
	pTargets->ubMaximum[Role] = maximum;
}

void BuildEnemyRoleTargets(
	ENEMY_ROLE_TARGETS *pTargets,
	INT8 bSoldierClass,
	UINT8 ubSquadSize,
	UINT8 ubProgress,
	INT8 bEquipmentRating)
{
	UINT8 supportScale;

	if ( !pTargets )
		return;

	memset(pTargets, 0, sizeof(ENEMY_ROLE_TARGETS));

	if ( ubSquadSize == 0 )
		return;

	// Administrators remain security/irregular troops.  Their late-game
	// progression should improve competence and limited support equipment,
	// not turn the whole class into line infantry.
	if ( bSoldierClass == SOLDIER_CLASS_ADMINISTRATOR )
	{
		SetRoleTarget(pTargets, ENEMY_ROLE_RIFLEMAN, ubSquadSize, ubSquadSize);
		SetRoleTarget(pTargets, ENEMY_ROLE_ASSAULT, (ubSquadSize >= 4) ? 1 : 0, (ubSquadSize >= 8) ? 2 : 1);
		SetRoleTarget(pTargets, ENEMY_ROLE_SQUAD_LEADER, (ubProgress >= 25 && ubSquadSize >= 5) ? 1 : 0, 1);
		SetRoleTarget(pTargets, ENEMY_ROLE_MARKSMAN, (ubProgress >= 65 && ubSquadSize >= 8) ? 1 : 0, 1);
		SetRoleTarget(pTargets, ENEMY_ROLE_GRENADIER, (ubProgress >= 55 && ubSquadSize >= 8) ? 1 : 0, 1);
		return;
	}

	if ( !IsRegularOrEliteEnemy(bSoldierClass) )
	{
		SetRoleTarget(pTargets, ENEMY_ROLE_RIFLEMAN, ubSquadSize, ubSquadSize);
		return;
	}

	// Support density rises mainly through professionalism/progression,
	// not by replacing every rifle with a more expensive rifle.
	supportScale = 0;
	if ( ubProgress >= 20 ) supportScale++;
	if ( ubProgress >= 40 ) supportScale++;
	if ( ubProgress >= 60 ) supportScale++;
	if ( ubProgress >= 80 ) supportScale++;
	if ( bEquipmentRating >= 3 ) supportScale++;
	if ( IsEliteEnemy(bSoldierClass) ) supportScale++;

	// Rifleman is the fallback role and has no hard quota pressure.
	SetRoleTarget(pTargets, ENEMY_ROLE_RIFLEMAN, ubSquadSize, ubSquadSize);

	// Leadership: one leader for a meaningful fireteam/squad.
	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_SQUAD_LEADER,
		(ubSquadSize >= 5) ? 1 : 0,
		(ubSquadSize >= 14 && IsEliteEnemy(bSoldierClass)) ? 2 : 1);

	// Automatic rifleman / LMG: roughly one per 5-7 soldiers.
	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_AUTOMATIC_RIFLEMAN,
		(ubProgress >= 20 && ubSquadSize >= 5) ? 1 : 0,
		ClampU8((ubSquadSize + 5) / 6, 1, 3));

	// Grenadier: one per 4-6 once organized troops are established.
	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_GRENADIER,
		(ubProgress >= 25 && ubSquadSize >= 5) ? 1 : 0,
		ClampU8((ubSquadSize + 4) / 5, 1, 3));

	// Marksman appears before dedicated snipers and remains much more common.
	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_MARKSMAN,
		(ubProgress >= 35 && ubSquadSize >= 6) ? 1 : 0,
		ClampU8((ubSquadSize + 7) / 8, 1, 2));

	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_SNIPER,
		(IsEliteEnemy(bSoldierClass) && ubProgress >= 65 && ubSquadSize >= 8) ? 1 : 0,
		(ubSquadSize >= 16 && IsEliteEnemy(bSoldierClass)) ? 2 : 1);

	// AT is deliberately capped.  Heavy support should be a squad resource,
	// not an independent random roll on every soldier.
	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_AT_SPECIALIST,
		(ubProgress >= 40 && ubSquadSize >= 7 && supportScale >= 3) ? 1 : 0,
		(ubSquadSize >= 14 && ubProgress >= 70) ? 2 : 1);

	// Medics/radio operators represent professionalization.
	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_MEDIC,
		((ubProgress >= (IsEliteEnemy(bSoldierClass) ? 35 : 45)) && ubSquadSize >= 8) ? 1 : 0,
		(ubSquadSize >= 16) ? 2 : 1);

	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_RADIO_OPERATOR,
		((ubProgress >= (IsEliteEnemy(bSoldierClass) ? 40 : 50)) && ubSquadSize >= 7) ? 1 : 0,
		(ubSquadSize >= 16) ? 2 : 1);

	// Scouts/assault troops add controlled variety without consuming the
	// support-weapon caps.
	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_ASSAULT,
		(ubProgress >= 15 && ubSquadSize >= 5) ? 1 : 0,
		ClampU8((ubSquadSize + 6) / 7, 1, 3));

	SetRoleTarget(
		pTargets,
		ENEMY_ROLE_SCOUT,
		(IsEliteEnemy(bSoldierClass) && ubProgress >= 45 && ubSquadSize >= 7) ? 1 : 0,
		(ubSquadSize >= 14 && IsEliteEnemy(bSoldierClass)) ? 2 : 1);

	// Mortars are intentionally not a normal desired role.  They are optional
	// fire-support assets and should later be allocated at group/sector level.
	SetRoleTarget(pTargets, ENEMY_ROLE_MORTAR, 0, (ubProgress >= 55 && ubSquadSize >= 10) ? 1 : 0);
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
