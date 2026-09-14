#ifdef PRECOMPILEDHEADERS
#include "Tactical All.h"
#else
#include "types.h"
#include "Overhead.h"
#include "Overhead Types.h"
#include "Soldier Control.h"
#include "Soldier Profile.h"
#include "Game Clock.h"
#include "Items.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#endif

#include "Campaign Tactical Telemetry.h"

static BOOLEAN gfVRTacticalTelemetryEnabled = TRUE;
static BOOLEAN gfVRTacticalBattleActive = FALSE;

static UINT32 guiVRTacticalSessionID = 0;
static UINT32 guiVRTacticalBattleSerial = 0;
static UINT32 guiVRTacticalEventSerial = 0;
static UINT32 guiVRTacticalTurnSerial = 0;

static BOOLEAN gfVRTacticalParticipant[TOTAL_SOLDIERS];

static UINT32 guiVRShots[MAXTEAMS];
static UINT32 guiVRProjectileHits[MAXTEAMS];
static UINT32 guiVRProjectileMisses[MAXTEAMS];
static UINT32 guiVRDamageDealt[MAXTEAMS];
static UINT32 guiVRDamageTaken[MAXTEAMS];
static UINT32 guiVRKills[MAXTEAMS];
static UINT32 guiVRDeaths[MAXTEAMS];
static UINT32 guiVRMoves[MAXTEAMS];
static UINT32 guiVRSuppressionAPLost[MAXTEAMS];

static INT32 VR_TacticalSafeTeam( INT32 iTeam )
{
	if( iTeam < 0 || iTeam >= MAXTEAMS )
		return -1;
	return iTeam;
}

static SOLDIERTYPE *VR_TacticalSoldierByID( INT32 iID )
{
	if( iID < 0 || iID >= TOTAL_SOLDIERS )
		return NULL;
	return MercPtrs[ iID ];
}

static void VR_TacticalEnsureSession()
{
	if( !guiVRTacticalSessionID )
	{
		guiVRTacticalSessionID = (UINT32)time( NULL );
		if( !guiVRTacticalSessionID )
			guiVRTacticalSessionID = 1;
	}
}

static void VR_TacticalEnsureHeaders()
{
	FILE *fp;
	long iLength;

	if( !gfVRTacticalTelemetryEnabled )
		return;

	VR_TacticalEnsureSession();

	fp = fopen( "Campaign Tactical Black Box.tsv", "a+" );
	if( fp )
	{
		fseek( fp, 0, SEEK_END );
		iLength = ftell( fp );
		if( iLength == 0 )
		{
			fprintf( fp,
				"session_id\tbattle_id\tevent_seq\tworld_min\tday\thour\tminute\tsector_x\tsector_y\tsector_z\tturn\tevent\tactor_team\tactor_id\tactor_profile\tactor_grid\ttarget_team\ttarget_id\ttarget_profile\ttarget_grid\titem\tvalue1\tvalue2\tvalue3\tactor_life\tactor_ap\tactor_breath\tactor_shock\treason\n" );
		}
		fclose( fp );
	}

	fp = fopen( "Campaign AI Companion.txt", "a+" );
	if( fp )
	{
		fseek( fp, 0, SEEK_END );
		iLength = ftell( fp );
		if( iLength == 0 )
		{
			fprintf( fp, "VENGEANCE CAMPAIGN AI COMPANION\n" );
			fprintf( fp, "Strategic decisions and tactical battle telemetry.\n\n" );
		}
		fclose( fp );
	}
}

static void VR_TacticalWrite(
	const CHAR8 *pEvent,
	SOLDIERTYPE *pActor,
	SOLDIERTYPE *pTarget,
	INT32 iFallbackTeam,
	UINT16 usItem,
	INT32 iValue1,
	INT32 iValue2,
	INT32 iValue3,
	const CHAR8 *pReason )
{
	FILE *fp;
	INT32 iActorTeam = pActor ? pActor->bTeam : VR_TacticalSafeTeam( iFallbackTeam );
	INT32 iTargetTeam = pTarget ? pTarget->bTeam : -1;
	INT32 iActorID = pActor ? pActor->ubID : -1;
	INT32 iTargetID = pTarget ? pTarget->ubID : -1;
	INT32 iActorProfile = pActor ? pActor->ubProfile : -1;
	INT32 iTargetProfile = pTarget ? pTarget->ubProfile : -1;
	INT32 iActorGrid = pActor ? pActor->sGridNo : -1;
	INT32 iTargetGrid = pTarget ? pTarget->sGridNo : -1;
	INT32 iLife = pActor ? pActor->stats.bLife : -1;
	INT32 iAP = pActor ? pActor->bActionPoints : -1;
	INT32 iBreath = pActor ? pActor->bBreath : -1;
	INT32 iShock = pActor ? pActor->aiData.bShock : -1;

	if( !gfVRTacticalTelemetryEnabled || !gfVRTacticalBattleActive )
		return;

	VR_TacticalEnsureHeaders();
	guiVRTacticalEventSerial++;

	fp = fopen( "Campaign Tactical Black Box.tsv", "a" );
	if( fp )
	{
		fprintf( fp,
			"%u\t%u\t%u\t%u\t%u\t%u\t%u\t%d\t%d\t%d\t%u\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n",
			guiVRTacticalSessionID,
			guiVRTacticalBattleSerial,
			guiVRTacticalEventSerial,
			GetWorldTotalMin(),
			GetWorldDay(),
			GetWorldHour(),
			GetWorldMinutesInDay() % 60,
			gWorldSectorX,
			gWorldSectorY,
			gbWorldSectorZ,
			guiVRTacticalTurnSerial,
			pEvent ? pEvent : "-",
			iActorTeam,
			iActorID,
			iActorProfile,
			iActorGrid,
			iTargetTeam,
			iTargetID,
			iTargetProfile,
			iTargetGrid,
			usItem,
			iValue1,
			iValue2,
			iValue3,
			iLife,
			iAP,
			iBreath,
			iShock,
			pReason ? pReason : "-" );
		fclose( fp );
	}
}

static void VR_TacticalCompanionLine( const CHAR8 *pText )
{
	FILE *fp;

	if( !gfVRTacticalTelemetryEnabled || !gfVRTacticalBattleActive )
		return;

	VR_TacticalEnsureHeaders();
	fp = fopen( "Campaign AI Companion.txt", "a" );
	if( fp )
	{
		fprintf( fp,
			"[TACTICAL][S%u][B%u][Day %02u %02u:%02u][%c%d z%d] %s\n",
			guiVRTacticalSessionID,
			guiVRTacticalBattleSerial,
			GetWorldDay(),
			GetWorldHour(),
			GetWorldMinutesInDay() % 60,
			'A' + gWorldSectorY - 1,
			gWorldSectorX,
			gbWorldSectorZ,
			pText ? pText : "-" );
		fclose( fp );
	}
}

static void VR_TacticalLogParticipant( SOLDIERTYPE *pSoldier, const CHAR8 *pEvent )
{
	UINT16 usHandItem = 0;
	if( !pSoldier )
		return;

	if( pSoldier->inv[ HANDPOS ].exists() )
		usHandItem = pSoldier->inv[ HANDPOS ].usItem;

	VR_TacticalWrite( pEvent, pSoldier, NULL, pSoldier->bTeam, usHandItem,
		pSoldier->stats.bLife,
		pSoldier->bActionPoints,
		pSoldier->pathing.bLevel,
		"participant state snapshot" );
}

BOOLEAN VR_TacticalTelemetryBattleActive()
{
	return gfVRTacticalBattleActive;
}

void VR_TacticalTelemetryBattleStart( UINT8 ubStartingTeam )
{
	UINT32 uiCount;
	SOLDIERTYPE *pSoldier;
	CHAR8 zReason[256];
	INT32 iParticipants = 0;

	if( !gfVRTacticalTelemetryEnabled )
		return;

	VR_TacticalEnsureSession();

	if( gfVRTacticalBattleActive )
	{
		VR_TacticalWrite( "COMBAT_REENTER", NULL, NULL, ubStartingTeam, 0,
			ubStartingTeam, 0, 0,
			"turn-based combat mode re-entered while the same tactical battle is still open" );
		return;
	}

	guiVRTacticalBattleSerial++;
	if( !guiVRTacticalBattleSerial )
		guiVRTacticalBattleSerial = 1;
	guiVRTacticalEventSerial = 0;
	guiVRTacticalTurnSerial = 0;
	gfVRTacticalBattleActive = TRUE;

	memset( gfVRTacticalParticipant, 0, sizeof( gfVRTacticalParticipant ) );
	memset( guiVRShots, 0, sizeof( guiVRShots ) );
	memset( guiVRProjectileHits, 0, sizeof( guiVRProjectileHits ) );
	memset( guiVRProjectileMisses, 0, sizeof( guiVRProjectileMisses ) );
	memset( guiVRDamageDealt, 0, sizeof( guiVRDamageDealt ) );
	memset( guiVRDamageTaken, 0, sizeof( guiVRDamageTaken ) );
	memset( guiVRKills, 0, sizeof( guiVRKills ) );
	memset( guiVRDeaths, 0, sizeof( guiVRDeaths ) );
	memset( guiVRMoves, 0, sizeof( guiVRMoves ) );
	memset( guiVRSuppressionAPLost, 0, sizeof( guiVRSuppressionAPLost ) );

	for( uiCount = 0; uiCount < TOTAL_SOLDIERS; ++uiCount )
	{
		pSoldier = MercPtrs[ uiCount ];
		if( pSoldier && pSoldier->bActive && pSoldier->bInSector )
		{
			gfVRTacticalParticipant[ uiCount ] = TRUE;
			iParticipants++;
		}
	}

	sprintf( zReason, "battle opened; starting_team=%u participants=%d", ubStartingTeam, iParticipants );
	VR_TacticalWrite( "BATTLE_START", NULL, NULL, ubStartingTeam, 0,
		ubStartingTeam, iParticipants, 0, zReason );
	VR_TacticalCompanionLine( zReason );

	for( uiCount = 0; uiCount < TOTAL_SOLDIERS; ++uiCount )
	{
		if( gfVRTacticalParticipant[ uiCount ] )
			VR_TacticalLogParticipant( MercPtrs[ uiCount ], "PARTICIPANT_START" );
	}
}

void VR_TacticalTelemetryBattleEnd( const CHAR8 *pResult, BOOLEAN fEnemyRetreated )
{
	UINT32 uiCount;
	INT32 iTeam;
	CHAR8 zReason[320];
	SOLDIERTYPE *pSoldier;

	if( !gfVRTacticalBattleActive )
		return;

	for( uiCount = 0; uiCount < TOTAL_SOLDIERS; ++uiCount )
	{
		if( gfVRTacticalParticipant[ uiCount ] )
		{
			pSoldier = MercPtrs[ uiCount ];
			if( pSoldier )
				VR_TacticalLogParticipant( pSoldier, "PARTICIPANT_END" );
		}
	}

	for( iTeam = 0; iTeam < MAXTEAMS; ++iTeam )
	{
		if( guiVRShots[iTeam] || guiVRProjectileHits[iTeam] || guiVRProjectileMisses[iTeam] ||
			guiVRDamageDealt[iTeam] || guiVRDamageTaken[iTeam] || guiVRKills[iTeam] ||
			guiVRDeaths[iTeam] || guiVRMoves[iTeam] || guiVRSuppressionAPLost[iTeam] )
		{
			sprintf( zReason,
				"team summary: shots=%u hits=%u misses=%u damage_dealt=%u damage_taken=%u kills=%u deaths=%u moves=%u suppression_ap_lost=%u",
				guiVRShots[iTeam], guiVRProjectileHits[iTeam], guiVRProjectileMisses[iTeam],
				guiVRDamageDealt[iTeam], guiVRDamageTaken[iTeam], guiVRKills[iTeam],
				guiVRDeaths[iTeam], guiVRMoves[iTeam], guiVRSuppressionAPLost[iTeam] );
			VR_TacticalWrite( "TEAM_SUMMARY", NULL, NULL, iTeam, 0,
				guiVRShots[iTeam], guiVRProjectileHits[iTeam], guiVRDamageDealt[iTeam], zReason );
		}
	}

	sprintf( zReason, "battle closed: result=%s enemy_retreated=%d turns=%u events=%u",
		pResult ? pResult : "UNKNOWN", fEnemyRetreated, guiVRTacticalTurnSerial, guiVRTacticalEventSerial );
	VR_TacticalWrite( "BATTLE_END", NULL, NULL, -1, 0,
		fEnemyRetreated, guiVRTacticalTurnSerial, guiVRTacticalEventSerial, zReason );
	VR_TacticalCompanionLine( zReason );

	gfVRTacticalBattleActive = FALSE;
}

void VR_TacticalTelemetryTurnStart( UINT8 ubTeam )
{
	INT32 iAlive = 0;
	INT32 iWounded = 0;
	UINT32 uiCount;
	SOLDIERTYPE *pSoldier;
	CHAR8 zReason[192];

	if( !gfVRTacticalBattleActive )
		return;

	guiVRTacticalTurnSerial++;

	for( uiCount = 0; uiCount < TOTAL_SOLDIERS; ++uiCount )
	{
		pSoldier = MercPtrs[ uiCount ];
		if( pSoldier && pSoldier->bActive && pSoldier->bInSector && pSoldier->bTeam == ubTeam && pSoldier->stats.bLife > 0 )
		{
			iAlive++;
			if( pSoldier->stats.bLife < pSoldier->stats.bLifeMax )
				iWounded++;
		}
	}

	sprintf( zReason, "team turn begins: team=%u alive=%d wounded=%d", ubTeam, iAlive, iWounded );
	VR_TacticalWrite( "TURN_START", NULL, NULL, ubTeam, 0, iAlive, iWounded, 0, zReason );
}

void VR_TacticalTelemetryMoveOrder( SOLDIERTYPE *pSoldier, INT32 sDestinationGridNo, UINT16 usMovementAnim )
{
	INT32 iTeam;
	if( !gfVRTacticalBattleActive || !pSoldier || sDestinationGridNo == pSoldier->sGridNo )
		return;

	iTeam = VR_TacticalSafeTeam( pSoldier->bTeam );
	if( iTeam >= 0 )
		guiVRMoves[iTeam]++;

	VR_TacticalWrite( "MOVE_ORDER", pSoldier, NULL, pSoldier->bTeam, 0,
		sDestinationGridNo, usMovementAnim, pSoldier->pathing.bLevel,
		"movement/path order accepted by soldier event layer" );
}

void VR_TacticalTelemetryShot( SOLDIERTYPE *pSoldier, INT32 sTargetGridNo )
{
	INT32 iTeam;
	UINT16 usItem = 0;

	if( !gfVRTacticalBattleActive || !pSoldier )
		return;

	iTeam = VR_TacticalSafeTeam( pSoldier->bTeam );
	if( iTeam >= 0 )
		guiVRShots[iTeam]++;

	usItem = pSoldier->usAttackingWeapon;

	VR_TacticalWrite( "SHOT", pSoldier, NULL, pSoldier->bTeam, usItem,
		sTargetGridNo, pSoldier->bDoBurst, pSoldier->bDoAutofire,
		"weapon discharge accepted; projectile outcome follows as hit/miss/contact events" );
}

void VR_TacticalTelemetryShotMiss( UINT8 ubAttackerID, INT32 iBullet )
{
	SOLDIERTYPE *pAttacker = VR_TacticalSoldierByID( ubAttackerID );
	INT32 iTeam = pAttacker ? VR_TacticalSafeTeam( pAttacker->bTeam ) : -1;
	UINT16 usItem = pAttacker ? pAttacker->usAttackingWeapon : 0;

	if( !gfVRTacticalBattleActive )
		return;

	if( iTeam >= 0 )
		guiVRProjectileMisses[iTeam]++;

	VR_TacticalWrite( "PROJECTILE_MISS", pAttacker, NULL, iTeam, usItem,
		iBullet, 0, 0, "projectile resolved without hitting a soldier" );
}

void VR_TacticalTelemetryProjectileHit( UINT8 ubAttackerID, UINT16 usTargetID, UINT16 usWeaponIndex,
	INT16 sDamage, INT16 sBreathLoss, UINT8 ubHitLocation, INT16 sRange, BOOLEAN fHit )
{
	SOLDIERTYPE *pAttacker = VR_TacticalSoldierByID( ubAttackerID );
	SOLDIERTYPE *pTarget = VR_TacticalSoldierByID( usTargetID );
	INT32 iTeam = pAttacker ? VR_TacticalSafeTeam( pAttacker->bTeam ) : -1;

	if( !gfVRTacticalBattleActive )
		return;

	if( fHit && iTeam >= 0 )
		guiVRProjectileHits[iTeam]++;

	VR_TacticalWrite( fHit ? "PROJECTILE_HIT" : "PROJECTILE_CONTACT",
		pAttacker, pTarget, iTeam, usWeaponIndex,
		sDamage, sBreathLoss, ((INT32)ubHitLocation << 16) | (UINT16)sRange,
		"projectile collision before final armour/resistance/life deductions" );
}

void VR_TacticalTelemetryDamage( SOLDIERTYPE *pTarget, UINT8 ubAttackerID, UINT8 ubReason,
	INT8 bOldLife, INT16 sBreathLoss, INT32 sSourceGrid )
{
	SOLDIERTYPE *pAttacker = VR_TacticalSoldierByID( ubAttackerID );
	INT32 iAttackerTeam = pAttacker ? VR_TacticalSafeTeam( pAttacker->bTeam ) : -1;
	INT32 iTargetTeam = pTarget ? VR_TacticalSafeTeam( pTarget->bTeam ) : -1;
	INT32 iActualDamage;

	if( !gfVRTacticalBattleActive || !pTarget )
		return;

	iActualDamage = (INT32)bOldLife - (INT32)pTarget->stats.bLife;

	if( iActualDamage > 0 )
	{
		if( iAttackerTeam >= 0 )
			guiVRDamageDealt[iAttackerTeam] += iActualDamage;
		if( iTargetTeam >= 0 )
			guiVRDamageTaken[iTargetTeam] += iActualDamage;
	}

	VR_TacticalWrite( "DAMAGE", pAttacker, pTarget, iAttackerTeam, 0,
		iActualDamage, ubReason, sBreathLoss,
		"post-mitigation damage after armour/resistance and life adjustment" );

	if( bOldLife > 0 && pTarget->stats.bLife == 0 )
	{
		if( iAttackerTeam >= 0 )
			guiVRKills[iAttackerTeam]++;
		if( iTargetTeam >= 0 )
			guiVRDeaths[iTargetTeam]++;

		VR_TacticalWrite( "CASUALTY", pAttacker, pTarget, iAttackerTeam, 0,
			ubReason, sSourceGrid, iActualDamage,
			"target transitioned from alive to dead during damage resolution" );
	}
}

void VR_TacticalTelemetrySuppression( SOLDIERTYPE *pTarget, UINT8 ubAttackerID,
	UINT8 ubSuppressionPoints, UINT8 ubAPLost, UINT8 ubNewStance )
{
	SOLDIERTYPE *pAttacker = VR_TacticalSoldierByID( ubAttackerID );
	INT32 iTargetTeam = pTarget ? VR_TacticalSafeTeam( pTarget->bTeam ) : -1;

	if( !gfVRTacticalBattleActive || !pTarget )
		return;

	if( iTargetTeam >= 0 )
		guiVRSuppressionAPLost[iTargetTeam] += ubAPLost;

	VR_TacticalWrite( "SUPPRESSION", pAttacker, pTarget,
		pAttacker ? pAttacker->bTeam : -1, 0,
		ubSuppressionPoints, ubAPLost, ubNewStance,
		"suppression resolved: accumulated near-fire pressure, AP loss and resulting stance/cower decision" );
}
