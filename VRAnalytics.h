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

#endif // VR_ANALYTICS_H
