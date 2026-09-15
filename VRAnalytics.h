#ifndef VR_ANALYTICS_H
#define VR_ANALYTICS_H

// Vengeance Reloaded analytics foundation.
//
// One shared event stream feeds two consumers:
//   * Black Box: forensic troubleshooting.
//   * Campaign Companion: post-session refinement analysis.
//
// Every event is tagged as tactical or strategic so both consumers can keep
// the battle and strategy layers separate while still reconstructing causal
// chains that cross between them.

enum VRAnalyticsLayer
{
	VR_ANALYTICS_TACTICAL = 1,
	VR_ANALYTICS_STRATEGIC = 2
};

// Runtime control. Enabled by default.
void VRAnalyticsSetEnabled( bool enabled );
bool VRAnalyticsIsEnabled();

// Clean lifecycle boundary for the structured JSONL journal. Safe to call
// repeatedly; after shutdown, later telemetry calls become no-ops.
void VRAnalyticsShutdown();

// Generic causal decision API.
unsigned long VRAnalyticsBeginDecision(
	VRAnalyticsLayer layer,
	const char* actorType,
	unsigned int actorId,
	const char* decisionType );

void VRAnalyticsStateInt(
	unsigned long decisionId,
	const char* key,
	long value );

void VRAnalyticsCandidate(
	unsigned long decisionId,
	const char* candidate,
	long subjectId,
	long rawScore,
	long adjustedScore,
	bool eligible,
	const char* reason );

void VRAnalyticsCommitDecision(
	unsigned long decisionId,
	const char* selection,
	long subjectId,
	long score,
	const char* reason );

void VRAnalyticsOutcome(
	unsigned long decisionId,
	const char* status,
	const char* metricA,
	long valueA,
	const char* metricB,
	long valueB,
	const char* detail );

void VRAnalyticsDiagnostic(
	VRAnalyticsLayer layer,
	const char* actorType,
	unsigned int actorId,
	const char* code,
	const char* detail );

// Battle lifecycle. A battle remains open across temporary exits/re-entries
// into turn-based combat in the same sector.
void VRAnalyticsBattleStarted(
	unsigned long worldMinutes,
	int sectorX,
	int sectorY,
	int sectorZ,
	int playerCount,
	int enemyCount,
	int militiaCount );

void VRAnalyticsBattleEnded(
	const char* result,
	unsigned long worldMinutes,
	int sectorX,
	int sectorY,
	int sectorZ,
	int playerCount,
	int enemyCount,
	int militiaCount,
	bool enemyRetreated );

// Strategic execution telemetry. Group IDs and target sectors provide the
// bridge from Queen-level intent to later tactical battles.
void VRAnalyticsStrategicMoveOrdered(
	unsigned long worldMinutes,
	unsigned int groupId,
	int sourceSector,
	int targetSector,
	int groupSize,
	int moveCode,
	int intention );

void VRAnalyticsStrategicGroupArrived(
	unsigned long worldMinutes,
	unsigned int groupId,
	int sector,
	int groupSize,
	const char* assignment );

// Tactical convenience wrappers. They maintain one active decision chain per
// soldier and automatically correlate action completion with the selection.
void VRAnalyticsTacticalCandidate(
	unsigned int soldierId,
	const char* candidate,
	long target,
	long rawScore,
	long adjustedScore,
	bool eligible,
	const char* reason );

// Attach perception/assessment values to the currently open tactical decision.
// This is the Black Box v2 decision-forensics layer: raw inputs remain linked
// to the same decision ID as candidates, the final action and its outcome.
void VRAnalyticsTacticalStateInt(
	unsigned int soldierId,
	const char* key,
	long value );

// Compact Black Box v2 assessment. All fields are actor-perceived/current
// decision inputs and are emitted as one JSONL event to avoid dozens of disk
// flushes per soldier evaluation.
void VRAnalyticsTacticalRetreatAssessment(
	unsigned int soldierId,
	unsigned long turn,
	int battleSituation,
	long perceivedFriendlyStrength,
	long perceivedEnemyStrength,
	int knownOpponents,
	int friendlyCasualtyPct,
	int localCasualtyPct,
	int holdConfidence,
	int localStress,
	int personalRisk,
	int riskTolerance,
	int routPressure,
	int collapseStreak,
	bool lastSurvivorPressure,
	int nearbyOperationalFriends,
	bool stableLeaderNearby,
	bool hasCover,
	bool hasSightCover,
	bool underFire,
	int lifePct,
	int marksmanship,
	int experienceLevel,
	int gunDeadliness,
	int gunAmmo,
	bool lastAttackHit,
	bool lastTargetSuppressed,
	bool escapeIntentActive );

// Omniscient team snapshot for troubleshooting. This is intentionally separate
// from soldier perception telemetry so analysis never confuses what really
// existed in the sector with what an AI actor was allowed to know.
void VRAnalyticsTacticalFormationSnapshot(
	unsigned long turn,
	int team,
	int sectorX,
	int sectorY,
	int sectorZ,
	int living,
	int combatReady,
	int cowering,
	int disengaging,
	int escaping,
	int leaders,
	int casualtyPercent,
	int averageMorale,
	int averageStress );

void VRAnalyticsTacticalDecisionSelected(
	unsigned int soldierId,
	int team,
	int side,
	bool neutral,
	int profile,
	int soldierClass,
	int action,
	long actionData,
	long gridNo,
	int actionPoints,
	int life,
	int breath,
	int alertStatus,
	int aiMorale,
	int orders,
	int attitude );

void VRAnalyticsTacticalDecisionSelected(
	unsigned int soldierId,
	int team,
	int action,
	long actionData,
	long gridNo,
	int actionPoints,
	int life,
	int breath,
	int alertStatus,
	int aiMorale,
	int orders,
	int attitude );

void VRAnalyticsTacticalActionDone(
	unsigned int soldierId,
	int action,
	long gridNo,
	int actionPoints,
	int life,
	int breath,
	bool lastAttackHit );

void VRAnalyticsTacticalActionRejected(
	unsigned int soldierId,
	int action,
	long actionData,
	const char* reason );

void VRAnalyticsTacticalCombatHit(
	unsigned int attackerId,
	unsigned int targetId,
	int attackerTeam,
	int targetTeam,
	int weaponIndex,
	int requestedDamage,
	int requestedBreathLoss,
	int range,
	int hitLocation,
	int targetLifeBefore,
	int targetLifeAfter,
	int targetBreathBefore,
	int targetBreathAfter );

// Hand-thrown grenade flight telemetry. These events are intentionally
// physical/outcome focused rather than planner focused: they allow Black Box
// and Companion to verify that AI-controlled soldiers obey the same throw
// range rules as player mercs, including stance, breath and Throwing traits.
void VRAnalyticsTacticalGrenadeThrowLaunched(
	unsigned int soldierId,
	int team,
	int projectileId,
	int itemIndex,
	long startGrid,
	long targetGrid,
	int targetDistance,
	int nearestPlayerId,
	int distanceToNearestPlayer,
	int targetOffsetToNearestPlayer,
	int maxRange,
	int effectiveStrength,
	int breath,
	int breathMax,
	int stance,
	int throwingTraits,
	int itemWeight );

void VRAnalyticsTacticalGrenadeThrowLanded(
	unsigned int soldierId,
	int team,
	int projectileId,
	int itemIndex,
	long startGrid,
	long endGrid,
	int actualDistance,
	int tilesMoved,
	int nearestPlayerId,
	int landingOffsetToNearestPlayer,
	bool inWater );

// Canonical applied-damage event emitted by SoldierTakeDamage(). Unlike
// combat_hit this includes non-bullet causes such as explosions, bleeding,
// gas, falls and melee, and records bleedout/downed transitions.
void VRAnalyticsTacticalDamageApplied(
	unsigned int targetId,
	int targetTeam,
	int targetSide,
	bool targetNeutral,
	int targetProfile,
	int targetSoldierClass,
	unsigned int attackerId,
	int attackerTeam,
	int damageReason,
	long sourceGrid,
	int incomingLifeDamage,
	int incomingBreathLoss,
	int lifeBefore,
	int lifeAfter,
	int breathBefore,
	int breathAfter,
	int bleedoutStateBefore,
	int bleedoutStateAfter,
	int bleedoutTurns );


// Compatibility emitters used by low-level tactical/static-library hooks.
// Keep these primitive parameter types exact: the VS2013 linker resolves
// their C++-mangled names across Tactical.lib / TileEngine.lib boundaries.
void VR_TacticalTelemetryTurnStart( unsigned char team );
void VR_TacticalTelemetrySmoke(
	unsigned char owner,
	int gridNo,
	unsigned short item,
	signed char level );

#endif // VR_ANALYTICS_H
