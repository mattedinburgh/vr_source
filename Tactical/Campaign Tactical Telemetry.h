#ifndef __CAMPAIGN_TACTICAL_TELEMETRY_H
#define __CAMPAIGN_TACTICAL_TELEMETRY_H

#include "types.h"

#define VR_AI_COMPANION_SCHEMA_VERSION 3
#define VR_AI_FRAMEWORK_VERSION "unified-ai-2026-09-14"

class SOLDIERTYPE;

void VR_TacticalTelemetryBattleStart( UINT8 ubStartingTeam );
void VR_TacticalTelemetryBattleEnd( const CHAR8 *pResult, BOOLEAN fEnemyRetreated );
void VR_TacticalTelemetryTurnStart( UINT8 ubTeam );
void VR_TacticalTelemetryMoveOrder( SOLDIERTYPE *pSoldier, INT32 sDestinationGridNo, UINT16 usMovementAnim );
void VR_TacticalTelemetryShot( SOLDIERTYPE *pSoldier, INT32 sTargetGridNo );
void VR_TacticalTelemetryShotMiss( UINT8 ubAttackerID, INT32 iBullet );
void VR_TacticalTelemetryProjectileHit( UINT8 ubAttackerID, UINT16 usTargetID, UINT16 usWeaponIndex,
	INT16 sDamage, INT16 sBreathLoss, UINT8 ubHitLocation, INT16 sRange, BOOLEAN fHit );
void VR_TacticalTelemetryDamage( SOLDIERTYPE *pTarget, UINT8 ubAttackerID, UINT8 ubReason,
	INT8 bOldLife, INT16 sBreathLoss, INT32 sSourceGrid );
void VR_TacticalTelemetrySuppression( SOLDIERTYPE *pTarget, UINT8 ubAttackerID,
	UINT8 ubSuppressionPoints, UINT8 ubAPLost, UINT8 ubNewStance );
void VR_TacticalTelemetryExplosion( UINT8 ubOwner, INT32 sGridNo, UINT16 usItem, INT8 bLevel );
void VR_TacticalTelemetrySmoke( UINT8 ubOwner, INT32 sGridNo, UINT16 usItem, INT8 bLevel );

BOOLEAN VR_TacticalTelemetryBattleActive();
UINT32 VR_TacticalTelemetrySessionID();
UINT32 VR_TacticalTelemetryBattleID();
UINT32 VR_TacticalTelemetryTurnID();

#endif
