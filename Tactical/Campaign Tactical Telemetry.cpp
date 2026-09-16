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
#include "Strategic Movement.h"
#include "Strategic AI.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#endif

#include "Campaign Tactical Telemetry.h"
#include "SaveLoadGame.h"
#include "SaveLoadScreen.h"
#include "screens.h"
#include "sgp.h"
#include "Map Information.h"
#include "random.h"
#include "Timer Control.h"
#include "video.h"
#include "TeamTurns.h"
#include "ai.h"
#include "AIList.h"
#include "Soldier macros.h"
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

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
static UINT32 guiVRExplosions[MAXTEAMS];
static UINT32 guiVRSmokeEffects[MAXTEAMS];

static BOOLEAN VR_SelfPlayOnTeamTurnStarted( UINT32 uiTeamTurn );

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
				"schema_version\tframework_version\tsession_id\tbattle_id\tevent_seq\tworld_min\tday\thour\tminute\tsector_x\tsector_y\tsector_z\tturn\tevent\tactor_team\tactor_id\tactor_profile\tactor_grid\ttarget_team\ttarget_id\ttarget_profile\ttarget_grid\titem\tvalue1\tvalue2\tvalue3\tactor_life\tactor_ap\tactor_breath\tactor_shock\treason\n" );
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
			fprintf( fp, "Framework: %s | Schema: %u\n", VR_AI_FRAMEWORK_VERSION, VR_AI_COMPANION_SCHEMA_VERSION );
			fprintf( fp, "Strategic decisions, AI reasoning and tactical battle telemetry.\n\n" );
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
			"%u\t%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%d\t%d\t%d\t%u\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n",
			VR_AI_COMPANION_SCHEMA_VERSION,
			VR_AI_FRAMEWORK_VERSION,
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

UINT32 VR_TacticalTelemetrySessionID()
{
	VR_TacticalEnsureSession();
	return guiVRTacticalSessionID;
}

UINT32 VR_TacticalTelemetryBattleID()
{
	return guiVRTacticalBattleSerial;
}

UINT32 VR_TacticalTelemetryTurnID()
{
	return guiVRTacticalTurnSerial;
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
	memset( guiVRExplosions, 0, sizeof( guiVRExplosions ) );
	memset( guiVRSmokeEffects, 0, sizeof( guiVRSmokeEffects ) );

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

	// Link this battle to any live strategic movement groups occupying the sector.
	// This is the bridge from "why did the strategic AI send them?" to "what happened
	// when that plan reached tactical combat?"
	{
		GROUP *pGroup = gpGroupList;
		while( pGroup )
		{
			if( pGroup->ubSectorX == gWorldSectorX &&
				pGroup->ubSectorY == gWorldSectorY &&
				pGroup->ubSectorZ == gbWorldSectorZ )
			{
				UINT32 uiPlanID = VR_GetSAICampaignPlanID( pGroup->ubGroupID );
				UINT32 uiParentDecision = VR_GetSAICampaignPlanDecisionID( pGroup->ubGroupID );
				UINT8 ubStrategicTeam = VR_GetStrategicGroupTeam( pGroup );
				UINT8 ubIntention = 0;
				CHAR8 zGroupReason[256];

				if( VR_IsEnemyStrategicGroup( pGroup ) && pGroup->pEnemyGroup )
					ubIntention = pGroup->pEnemyGroup->ubIntention;

				sprintf( zGroupReason,
					"strategic group present at battle start: group=%u team=%u size=%u intention=%u plan=%u parent_decision=%u between_sectors=%d created_sector=%u original_sector=%u",
					pGroup->ubGroupID, ubStrategicTeam, pGroup->ubGroupSize, ubIntention,
					uiPlanID, uiParentDecision, pGroup->fBetweenSectors,
					pGroup->ubCreatedSectorID, pGroup->ubOriginalSector );

				VR_TacticalWrite( "STRATEGIC_GROUP_CONTEXT", NULL, NULL, ubStrategicTeam, 0,
					pGroup->ubGroupID, uiPlanID, uiParentDecision, zGroupReason );
			}
			pGroup = pGroup->next;
		}
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
			guiVRDeaths[iTeam] || guiVRMoves[iTeam] || guiVRSuppressionAPLost[iTeam] ||
			guiVRExplosions[iTeam] || guiVRSmokeEffects[iTeam] )
		{
			sprintf( zReason,
				"team summary: shots=%u hits=%u misses=%u damage_dealt=%u damage_taken=%u kills=%u deaths=%u moves=%u suppression_ap_lost=%u explosions=%u smoke=%u",
				guiVRShots[iTeam], guiVRProjectileHits[iTeam], guiVRProjectileMisses[iTeam],
				guiVRDamageDealt[iTeam], guiVRDamageTaken[iTeam], guiVRKills[iTeam],
				guiVRDeaths[iTeam], guiVRMoves[iTeam], guiVRSuppressionAPLost[iTeam],
				guiVRExplosions[iTeam], guiVRSmokeEffects[iTeam] );
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

	if( VR_SelfPlayOnTeamTurnStarted( guiVRTacticalTurnSerial ) )
		return;


	for( uiCount = 0; uiCount < TOTAL_SOLDIERS; ++uiCount )
	{
		pSoldier = MercPtrs[ uiCount ];
		if( pSoldier && pSoldier->bActive && pSoldier->bInSector )
		{
			if( !gfVRTacticalParticipant[ uiCount ] )
			{
				gfVRTacticalParticipant[ uiCount ] = TRUE;
				VR_TacticalLogParticipant( pSoldier, "PARTICIPANT_JOIN" );
			}
		}
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

	CHAR8 zReason[160];
	sprintf( zReason, "projectile collision before final mitigation; hit_location=%u", ubHitLocation );
	VR_TacticalWrite( fHit ? "PROJECTILE_HIT" : "PROJECTILE_CONTACT",
		pAttacker, pTarget, iTeam, usWeaponIndex,
		sDamage, sBreathLoss, sRange, zReason );
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

void VR_TacticalTelemetryExplosion( UINT8 ubOwner, INT32 sGridNo, UINT16 usItem, INT8 bLevel )
{
	SOLDIERTYPE *pOwner = VR_TacticalSoldierByID( ubOwner );
	INT32 iTeam = pOwner ? VR_TacticalSafeTeam( pOwner->bTeam ) : -1;

	if( !gfVRTacticalBattleActive )
		return;

	if( iTeam >= 0 )
		guiVRExplosions[iTeam]++;

	VR_TacticalWrite( "EXPLOSION", pOwner, NULL, iTeam, usItem,
		sGridNo, bLevel, 0,
		"explosive effect ignited; subsequent DAMAGE events contain actual casualties and post-mitigation injury" );
}

void VR_TacticalTelemetrySmoke( UINT8 ubOwner, INT32 sGridNo, UINT16 usItem, INT8 bLevel )
{
	SOLDIERTYPE *pOwner = VR_TacticalSoldierByID( ubOwner );
	INT32 iTeam = pOwner ? VR_TacticalSafeTeam( pOwner->bTeam ) : -1;

	if( !gfVRTacticalBattleActive )
		return;

	if( iTeam >= 0 )
		guiVRSmokeEffects[iTeam]++;

	VR_TacticalWrite( "SMOKE_DEPLOYED", pOwner, NULL, iTeam, usItem,
		sGridNo, bLevel, 0,
		"new smoke/gas effect created; useful for later cover, withdrawal and suppression-response analysis" );
}


// ============================================================================
// AI SELF-PLAY BATTLE LAB
// ============================================================================
// This harness deliberately reuses the shipped tactical engine.  A normal
// tactical save is the scenario fixture; each run reloads it, applies a known
// seed, hands both sides to the normal tactical AI, suppresses presentation by
// hiding the game window, and writes reproducible black-box output.
//
// It does NOT mutate campaign state as part of the experiment.  CheckForEndOfBattle
// intercepts a lab result before normal strategic consequences are applied.

enum
{
	VR_SELFPLAY_STATE_DISABLED = 0,
	VR_SELFPLAY_STATE_NEED_LOAD,
	VR_SELFPLAY_STATE_WAIT_WORLD,
	VR_SELFPLAY_STATE_RUNNING,
	VR_SELFPLAY_STATE_RELOAD_PENDING,
	VR_SELFPLAY_STATE_FINISHED
};

static BOOLEAN gfVRSelfPlayConfigured = FALSE;
static INT32 giVRSelfPlayState = VR_SELFPLAY_STATE_DISABLED;
static INT32 giVRSelfPlaySaveSlot = 0;
static UINT32 guiVRSelfPlayRuns = 0;
static UINT32 guiVRSelfPlayRunIndex = 0;
static UINT32 guiVRSelfPlayBaseSeed = 1000;
static UINT32 guiVRSelfPlayMaxTeamTurns = 1200;
static UINT32 guiVRSelfPlaySideAWins = 0;
static UINT32 guiVRSelfPlaySideBWins = 0;
static UINT32 guiVRSelfPlayStalemates = 0;
static UINT32 guiVRSelfPlayErrors = 0;
static INT32 giVRSelfPlaySideAStart = 0;
static INT32 giVRSelfPlaySideBStart = 0;
static clock_t gVRSelfPlayRunClockStart = 0;
static BOOLEAN gfVRSelfPlayWindowHidden = FALSE;
static CHAR8 gzVRSelfPlayBuildLabel[64] = "current";
static UINT32 guiVRSelfPlayWallStart = 0;
static BOOLEAN gfVRSelfPlayMapSelected = FALSE;
static BOOLEAN gfVRSelfPlayMapFixtureResolved = FALSE;
static INT16 gsVRSelfPlayMapX = 0;
static INT16 gsVRSelfPlayMapY = 0;
static INT8 gbVRSelfPlayMapZ = 0;
static CHAR8 gzVRSelfPlayMapSpec[16] = "slot";
static INT32 giVRSelfPlayMapCandidateSlots[NUM_SAVE_GAMES];
static UINT32 guiVRSelfPlayMapCandidateTime[NUM_SAVE_GAMES];
static UINT16 gusVRSelfPlayMapCandidateCount = 0;
static UINT16 gusVRSelfPlayMapCandidateIndex = 0;


static UINT32 VR_SelfPlayCurrentSeed()
{
	return guiVRSelfPlayBaseSeed + guiVRSelfPlayRunIndex;
}

static INT32 VR_SelfPlayAliveOnTeam( INT32 iTeam )
{
	INT32 iAlive = 0;
	for( UINT32 i = 0; i < TOTAL_SOLDIERS; ++i )
	{
		SOLDIERTYPE *pSoldier = MercPtrs[i];
		if( pSoldier && pSoldier->bActive && pSoldier->bInSector &&
			pSoldier->bTeam == iTeam && pSoldier->stats.bLife > 0 )
		{
			++iAlive;
		}
	}
	return iAlive;
}

static UINT32 VR_SelfPlayHashValue( UINT32 uiHash, UINT32 uiValue )
{
	// FNV-1a, byte-wise, intentionally allowing 32-bit overflow.
	for( UINT8 i = 0; i < 4; ++i )
	{
		uiHash ^= (uiValue >> (8 * i)) & 0xff;
		uiHash *= 16777619u;
	}
	return uiHash;
}

static UINT32 VR_SelfPlayStateHash()
{
	UINT32 uiHash = 2166136261u;

	uiHash = VR_SelfPlayHashValue( uiHash, (UINT32)gWorldSectorX );
	uiHash = VR_SelfPlayHashValue( uiHash, (UINT32)gWorldSectorY );
	uiHash = VR_SelfPlayHashValue( uiHash, (UINT32)gbWorldSectorZ );
	uiHash = VR_SelfPlayHashValue( uiHash, guiVRTacticalTurnSerial );

	for( UINT32 i = 0; i < TOTAL_SOLDIERS; ++i )
	{
		if( !gfVRTacticalParticipant[i] )
			continue;

		SOLDIERTYPE *pSoldier = MercPtrs[i];
		uiHash = VR_SelfPlayHashValue( uiHash, i );
		if( !pSoldier )
		{
			uiHash = VR_SelfPlayHashValue( uiHash, 0xffffffffu );
			continue;
		}

		uiHash = VR_SelfPlayHashValue( uiHash, (UINT32)pSoldier->bActive );
		uiHash = VR_SelfPlayHashValue( uiHash, (UINT32)pSoldier->bInSector );
		uiHash = VR_SelfPlayHashValue( uiHash, (UINT32)(UINT8)pSoldier->bTeam );
		uiHash = VR_SelfPlayHashValue( uiHash, (UINT32)pSoldier->sGridNo );
		uiHash = VR_SelfPlayHashValue( uiHash, (UINT32)(UINT8)pSoldier->pathing.bLevel );
		uiHash = VR_SelfPlayHashValue( uiHash, (UINT32)(UINT8)pSoldier->stats.bLife );
		uiHash = VR_SelfPlayHashValue( uiHash, (UINT32)(UINT8)pSoldier->bBreath );
		uiHash = VR_SelfPlayHashValue( uiHash, (UINT32)(UINT8)pSoldier->bActionPoints );
		uiHash = VR_SelfPlayHashValue( uiHash, (UINT32)(UINT8)pSoldier->aiData.bAlertStatus );
		uiHash = VR_SelfPlayHashValue( uiHash, (UINT32)(UINT8)pSoldier->aiData.bAction );
		if( pSoldier->inv[HANDPOS].exists() )
			uiHash = VR_SelfPlayHashValue( uiHash, pSoldier->inv[HANDPOS].usItem );
	}

	for( INT32 iTeam = 0; iTeam < MAXTEAMS; ++iTeam )
	{
		uiHash = VR_SelfPlayHashValue( uiHash, guiVRShots[iTeam] );
		uiHash = VR_SelfPlayHashValue( uiHash, guiVRProjectileHits[iTeam] );
		uiHash = VR_SelfPlayHashValue( uiHash, guiVRDamageDealt[iTeam] );
		uiHash = VR_SelfPlayHashValue( uiHash, guiVRDeaths[iTeam] );
		uiHash = VR_SelfPlayHashValue( uiHash, guiVRMoves[iTeam] );
	}

	return uiHash;
}

static void VR_SelfPlayEnsureHeaders()
{
	FILE *fp = fopen( "AI SelfPlay Runs.tsv", "a+" );
	if( fp )
	{
		fseek( fp, 0, SEEK_END );
		if( ftell( fp ) == 0 )
		{
			fprintf( fp,
				"schema_version\tframework_version\tbuild_label\tselected_map\tfixture_slot\trun\tseed\tsector_x\tsector_y\tsector_z\tresult\tteam_turns\twall_ms\t"
				"side_a_start\tside_b_start\tside_a_alive\tside_b_alive\t"
				"a_shots\ta_hits\ta_misses\ta_damage\ta_kills\ta_deaths\ta_moves\ta_suppression_ap\ta_explosions\ta_smoke\t"
				"b_shots\tb_hits\tb_misses\tb_damage\tb_kills\tb_deaths\tb_moves\tb_suppression_ap\tb_explosions\tb_smoke\tstate_hash\n" );
		}
		fclose( fp );
	}

	fp = fopen( "AI SelfPlay Decisions.tsv", "a+" );
	if( fp )
	{
		fseek( fp, 0, SEEK_END );
		if( ftell( fp ) == 0 )
		{
			fprintf( fp,
				"schema_version\tframework_version\tbuild_label\tfixture_slot\trun\tseed\tteam_turn\tsector_x\tsector_y\tsector_z\t"
				"team\tsoldier_id\tprofile\tgrid\tlevel\tlife\tap\tbreath\tshock\talert\tmorale\t"
				"action\taction_data\tnext_action\tnext_data\tunder_fire\n" );
		}
		fclose( fp );
	}
}

static void VR_SelfPlayWriteBatchLine( const CHAR8 *pFormat, ... )
{
	FILE *fp = fopen( "AI SelfPlay Batch.txt", "a" );
	if( !fp )
		return;

	va_list args;
	va_start( args, pFormat );
	vfprintf( fp, pFormat, args );
	va_end( args );
	fclose( fp );
}

static void VR_SelfPlayWriteRun( const CHAR8 *pResult )
{
	const INT32 iA = VR_TacticalSafeTeam( gbPlayerNum );
	const INT32 iB = VR_TacticalSafeTeam( ENEMY_TEAM );
	const INT32 iAAlive = VR_SelfPlayAliveOnTeam( gbPlayerNum );
	const INT32 iBAlive = VR_SelfPlayAliveOnTeam( ENEMY_TEAM );
	const UINT32 uiStateHash = VR_SelfPlayStateHash();
	UINT32 uiWallMs = guiVRSelfPlayWallStart ? (GetTickCount() - guiVRSelfPlayWallStart) : 0;

	VR_SelfPlayEnsureHeaders();
	FILE *fp = fopen( "AI SelfPlay Runs.tsv", "a" );
	if( fp )
	{
		fprintf( fp,
			"%u\t%s\t%s\t%s\t%d\t%u\t%u\t%d\t%d\t%d\t%s\t%u\t%u\t"
			"%d\t%d\t%d\t%d\t"
			"%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t"
			"%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%08X\n",
			VR_AI_SELFPLAY_SCHEMA_VERSION,
			VR_AI_FRAMEWORK_VERSION,
			gzVRSelfPlayBuildLabel,
			gzVRSelfPlayMapSpec,
			giVRSelfPlaySaveSlot,
			guiVRSelfPlayRunIndex + 1,
			VR_SelfPlayCurrentSeed(),
			gWorldSectorX, gWorldSectorY, gbWorldSectorZ,
			pResult ? pResult : "unknown",
			guiVRTacticalTurnSerial,
			uiWallMs,
			giVRSelfPlaySideAStart,
			giVRSelfPlaySideBStart,
			iAAlive,
			iBAlive,
			iA >= 0 ? guiVRShots[iA] : 0,
			iA >= 0 ? guiVRProjectileHits[iA] : 0,
			iA >= 0 ? guiVRProjectileMisses[iA] : 0,
			iA >= 0 ? guiVRDamageDealt[iA] : 0,
			iA >= 0 ? guiVRKills[iA] : 0,
			iA >= 0 ? guiVRDeaths[iA] : 0,
			iA >= 0 ? guiVRMoves[iA] : 0,
			iA >= 0 ? guiVRSuppressionAPLost[iA] : 0,
			iA >= 0 ? guiVRExplosions[iA] : 0,
			iA >= 0 ? guiVRSmokeEffects[iA] : 0,
			iB >= 0 ? guiVRShots[iB] : 0,
			iB >= 0 ? guiVRProjectileHits[iB] : 0,
			iB >= 0 ? guiVRProjectileMisses[iB] : 0,
			iB >= 0 ? guiVRDamageDealt[iB] : 0,
			iB >= 0 ? guiVRKills[iB] : 0,
			iB >= 0 ? guiVRDeaths[iB] : 0,
			iB >= 0 ? guiVRMoves[iB] : 0,
			iB >= 0 ? guiVRSuppressionAPLost[iB] : 0,
			iB >= 0 ? guiVRExplosions[iB] : 0,
			iB >= 0 ? guiVRSmokeEffects[iB] : 0,
			uiStateHash );
		fclose( fp );
	}
}

static void VR_SelfPlayClearPlayerAIFlags()
{
	for( UINT8 ubID = gTacticalStatus.Team[gbPlayerNum].bFirstID;
		ubID <= gTacticalStatus.Team[gbPlayerNum].bLastID; ++ubID )
	{
		SOLDIERTYPE *pSoldier = MercPtrs[ubID];
		if( pSoldier )
		{
			pSoldier->flags.uiStatusFlags &= ~SOLDIER_PCUNDERAICONTROL;
			pSoldier->flags.uiStatusFlags &= ~SOLDIER_UNDERAICONTROL;
		}
	}
}

static void VR_SelfPlayFinishRun( const CHAR8 *pResult, BOOLEAN fEnemyRetreated )
{
	if( giVRSelfPlayState != VR_SELFPLAY_STATE_RUNNING )
		return;

	if( VR_TacticalTelemetryBattleActive() )
		VR_TacticalTelemetryBattleEnd( pResult, fEnemyRetreated );

	VR_SelfPlayWriteRun( pResult );
	giVRSelfPlayState = VR_SELFPLAY_STATE_RELOAD_PENDING;
}

static void VR_SelfPlayFinishBatch( const CHAR8 *pReason )
{
	VR_SelfPlayWriteBatchLine(
		"END label=%s selected_map=%s fixture=%d requested_runs=%u completed=%u base_seed=%u side_a_wins=%u side_b_wins=%u stalemates=%u errors=%u reason=%s\n",
		gzVRSelfPlayBuildLabel, gzVRSelfPlayMapSpec, giVRSelfPlaySaveSlot,
		guiVRSelfPlayRuns,
		guiVRSelfPlayRunIndex + (giVRSelfPlayState == VR_SELFPLAY_STATE_RELOAD_PENDING ? 1 : 0),
		guiVRSelfPlayBaseSeed,
		guiVRSelfPlaySideAWins,
		guiVRSelfPlaySideBWins,
		guiVRSelfPlayStalemates,
		guiVRSelfPlayErrors,
		pReason ? pReason : "complete" );

	VR_SelfPlayClearPlayerAIFlags();
	SetClockSpeedPercent( 100.0f );
	giVRSelfPlayState = VR_SELFPLAY_STATE_FINISHED;
	gfProgramIsRunning = FALSE;
}

static void VR_SelfPlayStartCurrentTeamAI()
{
	if( gTacticalStatus.ubCurrentTeam > LAST_TEAM )
		return;

	for( UINT8 ubID = gTacticalStatus.Team[gTacticalStatus.ubCurrentTeam].bFirstID;
		ubID <= gTacticalStatus.Team[gTacticalStatus.ubCurrentTeam].bLastID; ++ubID )
	{
		SOLDIERTYPE *pSoldier = MercPtrs[ubID];
		if( pSoldier && pSoldier->bActive && pSoldier->bInSector &&
			(pSoldier->flags.uiStatusFlags & SOLDIER_UNDERAICONTROL) )
		{
			return;
		}
	}

	if( BuildAIListForTeam( gTacticalStatus.ubCurrentTeam ) )
	{
		UINT8 ubID = RemoveFirstAIListEntry();
		if( ubID != NOBODY )
		{
			StartNPCAI( MercPtrs[ubID] );
			return;
		}
	}

	// No actor can act for this team; use the standard team transition.
	EndAITurn();
}

static BOOLEAN VR_SelfPlayStartLoadedFixture()
{
	if( guiCurrentScreen != GAME_SCREEN || !gfWorldLoaded )
		return FALSE;

	if( !(gTacticalStatus.uiFlags & INCOMBAT) )
	{
		VR_SelfPlayWriteBatchLine(
			"REJECT fixture=%d map=%s reason=fixture_not_in_tactical_combat screen=%u sector=%d,%d,%d\n",
			giVRSelfPlaySaveSlot, gzVRSelfPlayMapSpec,
			guiCurrentScreen, gWorldSectorX, gWorldSectorY, gbWorldSectorZ );

		if( VR_SelfPlayTryNextMapFixture( "not_in_tactical_combat" ) )
			return FALSE;

		++guiVRSelfPlayErrors;
		VR_SelfPlayFinishBatch( "fixture_not_in_tactical_combat" );
		return FALSE;
	}

	// Seed only after the fixture is fully loaded. Loading itself is allowed to
	// consume randomness; every measured battle starts from exactly this reset.
	SetRandomSeed( VR_SelfPlayCurrentSeed() );

	for( UINT8 ubID = gTacticalStatus.Team[gbPlayerNum].bFirstID;
		ubID <= gTacticalStatus.Team[gbPlayerNum].bLastID; ++ubID )
	{
		SOLDIERTYPE *pSoldier = MercPtrs[ubID];
		if( pSoldier && pSoldier->bActive && pSoldier->bInSector )
			pSoldier->flags.uiStatusFlags |= SOLDIER_PCUNDERAICONTROL;
	}

	if( gfVRSelfPlayMapSelected )
		gfVRSelfPlayMapFixtureResolved = TRUE;

	giVRSelfPlaySideAStart = VR_SelfPlayAliveOnTeam( gbPlayerNum );
	giVRSelfPlaySideBStart = VR_SelfPlayAliveOnTeam( ENEMY_TEAM );
	if( giVRSelfPlaySideAStart <= 0 || giVRSelfPlaySideBStart <= 0 )
	{
		++guiVRSelfPlayErrors;
		VR_SelfPlayWriteBatchLine(
			"ERROR fixture=%d run=%u seed=%u reason=missing_combat_side side_a=%d side_b=%d\n",
			giVRSelfPlaySaveSlot, guiVRSelfPlayRunIndex + 1, VR_SelfPlayCurrentSeed(),
			giVRSelfPlaySideAStart, giVRSelfPlaySideBStart );
		VR_SelfPlayFinishBatch( "missing_combat_side" );
		return FALSE;
	}

	SetClockSpeedPercent( 5000.0f );
	gVRSelfPlayRunClockStart = clock();
	guiVRSelfPlayWallStart = GetTickCount();
	giVRSelfPlayState = VR_SELFPLAY_STATE_RUNNING;

	if( !VR_TacticalTelemetryBattleActive() )
		VR_TacticalTelemetryBattleStart( gTacticalStatus.ubCurrentTeam );

	VR_SelfPlayWriteBatchLine(
		"RUN label=%s selected_map=%s fixture=%d run=%u/%u seed=%u sector=%d,%d,%d side_a=%d side_b=%d\n",
		gzVRSelfPlayBuildLabel, gzVRSelfPlayMapSpec, giVRSelfPlaySaveSlot, guiVRSelfPlayRunIndex + 1, guiVRSelfPlayRuns,
		VR_SelfPlayCurrentSeed(), gWorldSectorX, gWorldSectorY, gbWorldSectorZ,
		giVRSelfPlaySideAStart, giVRSelfPlaySideBStart );

	VR_SelfPlayStartCurrentTeamAI();
	return TRUE;
}


static BOOLEAN VR_SelfPlayParseMapSpec( const CHAR8 *pSpec, INT16 *psX, INT16 *psY, INT8 *pbZ )
{
	if( !pSpec || !pSpec[0] || !isalpha((unsigned char)pSpec[0]) )
		return FALSE;

	const INT16 sY = (INT16)(toupper((unsigned char)pSpec[0]) - 'A' + 1);
	if( sY < 1 || sY > 16 )
		return FALSE;

	CHAR8 *pEnd = NULL;
	long lX = strtol( pSpec + 1, &pEnd, 10 );
	if( pEnd == pSpec + 1 || lX < 1 || lX > 16 )
		return FALSE;

	long lZ = 0;
	if( *pEnd == '-' || *pEnd == ':' )
	{
		++pEnd;
		CHAR8 *pZEnd = NULL;
		lZ = strtol( pEnd, &pZEnd, 10 );
		if( pZEnd == pEnd || *pZEnd != 0 || lZ < 0 || lZ > 9 )
			return FALSE;
	}
	else if( *pEnd != 0 )
	{
		return FALSE;
	}

	*psX = (INT16)lX;
	*psY = sY;
	*pbZ = (INT8)lZ;
	return TRUE;
}

static UINT32 VR_SelfPlaySaveHeaderMinute( const SAVED_GAME_HEADER *pHeader )
{
	if( !pHeader )
		return 0;
	return pHeader->uiDay * 24u * 60u +
		(UINT32)pHeader->ubHour * 60u +
		(UINT32)pHeader->ubMin;
}

static BOOLEAN VR_SelfPlayBuildMapCandidates()
{
	gusVRSelfPlayMapCandidateCount = 0;
	gusVRSelfPlayMapCandidateIndex = 0;

	for( INT32 iSlot = 0; iSlot < NUM_SAVE_GAMES; ++iSlot )
	{
		if( !gbSaveGameArray[iSlot] )
			continue;

		SAVED_GAME_HEADER Header;
		memset( &Header, 0, sizeof(Header) );
		if( !LoadSavedGameHeader( iSlot, &Header ) )
			continue;

		if( !Header.fWorldLoaded ||
			Header.uiCurrentScreen != GAME_SCREEN ||
			Header.sSectorX != gsVRSelfPlayMapX ||
			Header.sSectorY != gsVRSelfPlayMapY ||
			Header.bSectorZ != gbVRSelfPlayMapZ )
		{
			continue;
		}

		const UINT32 uiMinute = VR_SelfPlaySaveHeaderMinute( &Header );
		UINT16 insertAt = gusVRSelfPlayMapCandidateCount;
		while( insertAt > 0 && guiVRSelfPlayMapCandidateTime[insertAt - 1] < uiMinute )
		{
			giVRSelfPlayMapCandidateSlots[insertAt] = giVRSelfPlayMapCandidateSlots[insertAt - 1];
			guiVRSelfPlayMapCandidateTime[insertAt] = guiVRSelfPlayMapCandidateTime[insertAt - 1];
			--insertAt;
		}

		giVRSelfPlayMapCandidateSlots[insertAt] = iSlot;
		guiVRSelfPlayMapCandidateTime[insertAt] = uiMinute;
		++gusVRSelfPlayMapCandidateCount;
	}

	if( !gusVRSelfPlayMapCandidateCount )
		return FALSE;

	giVRSelfPlaySaveSlot = giVRSelfPlayMapCandidateSlots[0];
	VR_SelfPlayWriteBatchLine(
		"MAP_SELECT map=%s candidates=%u initial_fixture=%d\n",
		gzVRSelfPlayMapSpec, gusVRSelfPlayMapCandidateCount, giVRSelfPlaySaveSlot );
	return TRUE;
}

static BOOLEAN VR_SelfPlayTryNextMapFixture( const CHAR8 *pReason )
{
	if( !gfVRSelfPlayMapSelected || gfVRSelfPlayMapFixtureResolved )
		return FALSE;

	if( gusVRSelfPlayMapCandidateIndex + 1 >= gusVRSelfPlayMapCandidateCount )
		return FALSE;

	++gusVRSelfPlayMapCandidateIndex;
	giVRSelfPlaySaveSlot = giVRSelfPlayMapCandidateSlots[gusVRSelfPlayMapCandidateIndex];
	giVRSelfPlayState = VR_SELFPLAY_STATE_NEED_LOAD;

	VR_SelfPlayWriteBatchLine(
		"MAP_RETRY map=%s fixture=%d candidate=%u/%u reason=%s\n",
		gzVRSelfPlayMapSpec,
		giVRSelfPlaySaveSlot,
		(UINT32)gusVRSelfPlayMapCandidateIndex + 1,
		(UINT32)gusVRSelfPlayMapCandidateCount,
		pReason ? pReason : "invalid_fixture" );
	return TRUE;
}

BOOLEAN VR_SelfPlayConfigureFromCommandLine( const CHAR8 *pCommandLine )
{
	if( !pCommandLine || !pCommandLine[0] )
		return FALSE;

	const CHAR8 *pSelfPlay = strstr( pCommandLine, "-SELFPLAY=" );
	if( !pSelfPlay )
		pSelfPlay = strstr( pCommandLine, "-selfplay=" );
	if( !pSelfPlay )
		return FALSE;

	INT32 iSlot = -1;
	UINT32 uiRuns = 40;
	UINT32 uiSeed = 1000;
	UINT32 uiMaxTurns = 1200;
	CHAR8 zBuildLabel[64] = "current";
	CHAR8 zFixtureSpec[32] = "";

	pSelfPlay += 10;
	const INT32 iRead = sscanf( pSelfPlay, "%31[^,],%u,%u,%u,%63[^,\t\r\n ]",
		zFixtureSpec, &uiRuns, &uiSeed, &uiMaxTurns, zBuildLabel );
	if( iRead < 1 )
		return FALSE;

	gfVRSelfPlayMapSelected = VR_SelfPlayParseMapSpec(
		zFixtureSpec, &gsVRSelfPlayMapX, &gsVRSelfPlayMapY, &gbVRSelfPlayMapZ );

	if( gfVRSelfPlayMapSelected )
	{
		strncpy( gzVRSelfPlayMapSpec, zFixtureSpec, sizeof(gzVRSelfPlayMapSpec) - 1 );
		gzVRSelfPlayMapSpec[sizeof(gzVRSelfPlayMapSpec) - 1] = 0;
		for( CHAR8 *p = gzVRSelfPlayMapSpec; *p; ++p )
			*p = (CHAR8)toupper((unsigned char)*p);
	}
	else
	{
		CHAR8 *pEnd = NULL;
		long lSlot = strtol( zFixtureSpec, &pEnd, 10 );
		if( pEnd == zFixtureSpec || *pEnd != 0 || lSlot < 0 || lSlot >= NUM_SAVE_GAMES )
			return FALSE;
		iSlot = (INT32)lSlot;
		strcpy( gzVRSelfPlayMapSpec, "slot" );
	}
	if( iRead < 2 || uiRuns == 0 )
		uiRuns = 40;
	if( iRead < 3 || uiSeed == 0 )
		uiSeed = 1000;
	if( iRead < 4 || uiMaxTurns < 10 )
		uiMaxTurns = 1200;

	giVRSelfPlaySaveSlot = iSlot;
	gfVRSelfPlayMapFixtureResolved = FALSE;
	gusVRSelfPlayMapCandidateCount = 0;
	gusVRSelfPlayMapCandidateIndex = 0;
	guiVRSelfPlayRuns = uiRuns;
	guiVRSelfPlayBaseSeed = uiSeed;
	guiVRSelfPlayMaxTeamTurns = uiMaxTurns;
	strncpy( gzVRSelfPlayBuildLabel, iRead >= 5 ? zBuildLabel : "current", sizeof(gzVRSelfPlayBuildLabel) - 1 );
	gzVRSelfPlayBuildLabel[sizeof(gzVRSelfPlayBuildLabel) - 1] = 0;
	guiVRSelfPlayRunIndex = 0;
	guiVRSelfPlaySideAWins = 0;
	guiVRSelfPlaySideBWins = 0;
	guiVRSelfPlayStalemates = 0;
	guiVRSelfPlayErrors = 0;
	gfVRSelfPlayConfigured = TRUE;
	giVRSelfPlayState = VR_SELFPLAY_STATE_NEED_LOAD;

	VR_SelfPlayEnsureHeaders();
	VR_SelfPlayWriteBatchLine(
		"BEGIN schema=%u framework=%s label=%s selected_map=%s fixture=%d runs=%u base_seed=%u max_team_turns=%u\n",
		VR_AI_SELFPLAY_SCHEMA_VERSION, VR_AI_FRAMEWORK_VERSION, gzVRSelfPlayBuildLabel,
		gzVRSelfPlayMapSpec, giVRSelfPlaySaveSlot, guiVRSelfPlayRuns,
		guiVRSelfPlayBaseSeed, guiVRSelfPlayMaxTeamTurns );

	return TRUE;
}


static BOOLEAN VR_SelfPlayOnTeamTurnStarted( UINT32 uiTeamTurn )
{
	if( gfVRSelfPlayConfigured &&
		giVRSelfPlayState == VR_SELFPLAY_STATE_RUNNING &&
		uiTeamTurn >= guiVRSelfPlayMaxTeamTurns )
	{
		++guiVRSelfPlayStalemates;
		VR_SelfPlayFinishRun( "max_team_turns", FALSE );
		return TRUE;
	}
	return FALSE;
}

BOOLEAN VR_SelfPlayConfigured()
{
	return gfVRSelfPlayConfigured;
}

BOOLEAN VR_SelfPlayActive()
{
	return gfVRSelfPlayConfigured && giVRSelfPlayState == VR_SELFPLAY_STATE_RUNNING;
}

BOOLEAN VR_SelfPlayShouldAbortCurrentBattle()
{
	return gfVRSelfPlayConfigured && giVRSelfPlayState == VR_SELFPLAY_STATE_RELOAD_PENDING;
}

void VR_SelfPlayDecision( SOLDIERTYPE *pSoldier )
{
	if( !VR_SelfPlayActive() || !pSoldier )
		return;

	VR_SelfPlayEnsureHeaders();
	FILE *fp = fopen( "AI SelfPlay Decisions.tsv", "a" );
	if( !fp )
		return;

	fprintf( fp,
		"%u\t%s\t%s\t%s\t%d\t%u\t%u\t%u\t%d\t%d\t%d\t"
		"%d\t%u\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t"
		"%d\t%d\t%d\t%d\t%d\n",
		VR_AI_SELFPLAY_SCHEMA_VERSION,
		VR_AI_FRAMEWORK_VERSION,
		gzVRSelfPlayBuildLabel,
			gzVRSelfPlayMapSpec,
		giVRSelfPlaySaveSlot,
		guiVRSelfPlayRunIndex + 1,
		VR_SelfPlayCurrentSeed(),
		guiVRTacticalTurnSerial,
		gWorldSectorX, gWorldSectorY, gbWorldSectorZ,
		pSoldier->bTeam,
		pSoldier->ubID,
		pSoldier->ubProfile,
		pSoldier->sGridNo,
		pSoldier->pathing.bLevel,
		pSoldier->stats.bLife,
		pSoldier->bActionPoints,
		pSoldier->bBreath,
		pSoldier->aiData.bShock,
		pSoldier->aiData.bAlertStatus,
		pSoldier->aiData.bAIMorale,
		pSoldier->aiData.bAction,
		pSoldier->aiData.usActionData,
		pSoldier->aiData.bNextAction,
		pSoldier->aiData.usNextActionData,
		pSoldier->aiData.bUnderFire );
	fclose( fp );
}

BOOLEAN VR_SelfPlayInterceptBattleEnd( BOOLEAN fPlayerWon, BOOLEAN fPlayerLost, BOOLEAN fEnemyRetreated )
{
	if( !VR_SelfPlayActive() )
		return FALSE;

	if( fPlayerWon )
	{
		++guiVRSelfPlaySideAWins;
		VR_SelfPlayFinishRun( fEnemyRetreated ? "side_a_win_enemy_retreat" : "side_a_win", fEnemyRetreated );
	}
	else if( fPlayerLost )
	{
		++guiVRSelfPlaySideBWins;
		VR_SelfPlayFinishRun( "side_b_win", FALSE );
	}
	else
	{
		++guiVRSelfPlayStalemates;
		VR_SelfPlayFinishRun( "unresolved_end", fEnemyRetreated );
	}
	return TRUE;
}

void VR_SelfPlayGameLoop()
{
	if( !gfVRSelfPlayConfigured || giVRSelfPlayState == VR_SELFPLAY_STATE_FINISHED )
		return;

	if( !gfVRSelfPlayWindowHidden && ghWindow )
	{
		ShowWindow( ghWindow, SW_HIDE );
		gfVRSelfPlayWindowHidden = TRUE;
	}

	if( giVRSelfPlayState == VR_SELFPLAY_STATE_RUNNING )
		return;

	if( giVRSelfPlayState == VR_SELFPLAY_STATE_RELOAD_PENDING )
	{
		// A max-turn abort can leave combat mode active. Normal wins/losses are
		// intercepted in CheckForEndOfBattle and already exit combat cleanly.
		if( gTacticalStatus.uiFlags & INCOMBAT )
		{
			EndAllAITurns();
			ExitCombatMode();
		}
		VR_SelfPlayClearPlayerAIFlags();

		if( guiVRSelfPlayRunIndex + 1 >= guiVRSelfPlayRuns )
		{
			VR_SelfPlayFinishBatch( "complete" );
			return;
		}

		++guiVRSelfPlayRunIndex;
		giVRSelfPlayState = VR_SELFPLAY_STATE_NEED_LOAD;
	}

	if( giVRSelfPlayState == VR_SELFPLAY_STATE_NEED_LOAD )
	{
		// Initialization must be complete before direct save loading. On subsequent
		// runs we may already be on GAME_SCREEN, which is also safe for a reload.
		if( guiCurrentScreen != MAINMENU_SCREEN && guiCurrentScreen != GAME_SCREEN )
			return;

		if( !InitSaveGameArray() )
		{
			++guiVRSelfPlayErrors;
			VR_SelfPlayFinishBatch( "save_array_init_failed" );
			return;
		}

		if( gfVRSelfPlayMapSelected && !gfVRSelfPlayMapFixtureResolved &&
			gusVRSelfPlayMapCandidateCount == 0 )
		{
			if( !VR_SelfPlayBuildMapCandidates() )
			{
				++guiVRSelfPlayErrors;
				VR_SelfPlayWriteBatchLine(
					"ERROR map=%s reason=no_saved_fixture_for_selected_map\n",
					gzVRSelfPlayMapSpec );
				VR_SelfPlayFinishBatch( "no_saved_fixture_for_selected_map" );
				return;
			}
		}

		if( giVRSelfPlaySaveSlot < 0 ||
			giVRSelfPlaySaveSlot >= NUM_SAVE_GAMES ||
			!gbSaveGameArray[giVRSelfPlaySaveSlot] )
		{
			if( VR_SelfPlayTryNextMapFixture( "save_slot_not_available" ) )
				return;

			++guiVRSelfPlayErrors;
			VR_SelfPlayWriteBatchLine(
				"ERROR fixture=%d map=%s reason=save_slot_not_available screen=%u\n",
				giVRSelfPlaySaveSlot, gzVRSelfPlayMapSpec, guiCurrentScreen );
			VR_SelfPlayFinishBatch( "save_slot_not_available" );
			return;
		}

		if( !LoadSavedGame( giVRSelfPlaySaveSlot ) )
		{
			if( VR_SelfPlayTryNextMapFixture( "load_failed" ) )
				return;

			++guiVRSelfPlayErrors;
			VR_SelfPlayWriteBatchLine(
				"ERROR fixture=%d map=%s run=%u reason=load_failed\n",
				giVRSelfPlaySaveSlot, gzVRSelfPlayMapSpec, guiVRSelfPlayRunIndex + 1 );
			VR_SelfPlayFinishBatch( "load_failed" );
			return;
		}

		giVRSelfPlayState = VR_SELFPLAY_STATE_WAIT_WORLD;
		return;
	}

	if( giVRSelfPlayState == VR_SELFPLAY_STATE_WAIT_WORLD )
	{
		if( guiCurrentScreen == ERROR_SCREEN )
		{
			++guiVRSelfPlayErrors;
			VR_SelfPlayFinishBatch( "game_error_after_load" );
			return;
		}

		if( guiCurrentScreen == GAME_SCREEN && gfWorldLoaded )
			VR_SelfPlayStartLoadedFixture();
	}
}
