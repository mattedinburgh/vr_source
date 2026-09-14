#ifdef PRECOMPILEDHEADERS
#include "Strategic All.h"
#else
#include "types.h"
#include "Strategic AI Diagnostics.h"
#include "Strategic AI.h"
#include "Strategic Movement.h"
#include "Strategic Operational AI.h"
#include "Game Clock.h"
#include "strategic.h"
#include "strategicmap.h"
#include <stdio.h>
#include <string.h>
#endif

#include "Strategic AI Diagnostics.h"
#include <stdio.h>
#include <string.h>

#define VR_STRATEGIC_DIAG_SCHEMA 2
#define VR_STRATEGIC_FRAMEWORK "unified-ai-2026-09-14"

extern INT32 giReinforcementPool;
extern INT32 giRequestPoints;
extern INT32 giReinforcementPoints;
extern UINT8 gubQueenPriorityPhase;

static UINT32 guiVRStrategicDecisionSerial = 0;

static void VR_StrategicSectorName( INT32 iSector, CHAR8 *pOut )
{
	if( !pOut ) return;
	if( iSector < 0 || iSector > 255 )
	{
		sprintf( pOut, "-" );
		return;
	}
	sprintf( pOut, "%c%d", SECTORY( (UINT8)iSector ) + 'A' - 1, SECTORX( (UINT8)iSector ) );
}

static void VR_StrategicSanitize( const CHAR8 *pIn, CHAR8 *pOut, UINT32 uiSize )
{
	if( !pOut || !uiSize ) return;
	if( !pIn )
	{
		pOut[0] = '-';
		if( uiSize > 1 ) pOut[1] = 0;
		return;
	}

	UINT32 j = 0;
	for( UINT32 i = 0; pIn[i] && j + 1 < uiSize; ++i )
	{
		CHAR8 ch = pIn[i];
		if( ch == '\t' || ch == '\r' || ch == '\n' ) ch = ' ';
		pOut[j++] = ch;
	}
	pOut[j] = 0;
}

void VR_StrategicDiagnosticsInit()
{
	FILE *fp = fopen( "Campaign AI Black Box.tsv", "a+" );
	if( fp )
	{
		fseek( fp, 0, SEEK_END );
		if( ftell( fp ) == 0 )
		{
			fprintf( fp,
				"schema_version\tframework_version\tworld_min\tday\thour\tminute\tdecision_id\tevent\tsubject\tsubject_id\tgroup_id\tformation_id\tsource\ttarget\tmission\treserve_role\tsupply\tmorale\tintel_confidence\tgroup_size\tscore\taux\tpool\trequest_points\treinforcement_points\tqueen_phase\treason\n" );
		}
		fclose( fp );
	}

	fp = fopen( "Campaign AI Companion.txt", "a+" );
	if( fp )
	{
		fseek( fp, 0, SEEK_END );
		if( ftell( fp ) == 0 )
		{
			fprintf( fp, "VENGEANCE CAMPAIGN AI COMPANION\n" );
			fprintf( fp, "Framework: %s | Schema: %u\n\n",
				VR_STRATEGIC_FRAMEWORK, VR_STRATEGIC_DIAG_SCHEMA );
		}
		fclose( fp );
	}
}

void VR_StrategicDiagnosticsRecord( const CHAR8 *pEvent, const CHAR8 *pSubject,
	INT32 iSubjectID, GROUP *pGroup, INT32 iSourceSector, INT32 iTargetSector,
	INT32 iScore, INT32 iAux, const CHAR8 *pReason )
{
	VR_StrategicDiagnosticsInit();

	CHAR8 zSource[16], zTarget[16], zReason[384];
	VR_StrategicSectorName( iSourceSector, zSource );
	VR_StrategicSectorName( iTargetSector, zTarget );
	VR_StrategicSanitize( pReason, zReason, sizeof(zReason) );

	UINT8 ubGroupID = pGroup ? pGroup->ubGroupID : 0;
	UINT16 usFormation = (pGroup && pGroup->pEnemyGroup) ? pGroup->pEnemyGroup->usFormationID : 0;
	UINT8 ubMission = (pGroup && pGroup->pEnemyGroup) ? pGroup->pEnemyGroup->ubOperationalMission : 0;
	UINT8 ubReserve = (pGroup && pGroup->pEnemyGroup) ? pGroup->pEnemyGroup->ubOperationalReserveRole : 0;
	UINT8 ubSupply = (pGroup && pGroup->pEnemyGroup) ? pGroup->pEnemyGroup->ubOperationalSupply : 0;
	UINT8 ubMorale = (pGroup && pGroup->pEnemyGroup) ? pGroup->pEnemyGroup->ubOperationalMorale : 0;
	UINT8 ubIntel = (pGroup && pGroup->pEnemyGroup) ? pGroup->pEnemyGroup->ubOperationalIntelConfidence : 0;
	UINT8 ubSize = pGroup ? pGroup->ubGroupSize : 0;

	FILE *fp = fopen( "Campaign AI Black Box.tsv", "a" );
	if( fp )
	{
		fprintf( fp,
			"%u\t%s\t%u\t%u\t%u\t%u\t%u\t%s\t%s\t%d\t%u\t%u\t%s\t%s\t%u\t%u\t%u\t%u\t%u\t%u\t%d\t%d\t%d\t%d\t%d\t%u\t%s\n",
			VR_STRATEGIC_DIAG_SCHEMA, VR_STRATEGIC_FRAMEWORK,
			GetWorldTotalMin(), GetWorldDay(), GetWorldHour(), GetWorldMinutesInDay() % 60,
			guiVRStrategicDecisionSerial,
			pEvent ? pEvent : "-", pSubject ? pSubject : "-", iSubjectID,
			ubGroupID, usFormation, zSource, zTarget,
			ubMission, ubReserve, ubSupply, ubMorale, ubIntel, ubSize,
			iScore, iAux, giReinforcementPool, giRequestPoints,
			giReinforcementPoints, gubQueenPriorityPhase, zReason );
		fclose( fp );
	}

	fp = fopen( "Campaign AI Companion.txt", "a" );
	if( fp )
	{
		fprintf( fp,
			"[STRATEGIC][D%06u][Day %02u %02u:%02u][%s] %s #%d group=%u formation=%u from=%s to=%s mission=%s supply=%u morale=%u intel=%u | %s\n",
			guiVRStrategicDecisionSerial, GetWorldDay(), GetWorldHour(),
			GetWorldMinutesInDay() % 60, pEvent ? pEvent : "-",
			pSubject ? pSubject : "-", iSubjectID, ubGroupID, usFormation,
			zSource, zTarget, pGroup && pGroup->pEnemyGroup ?
				VR_OperationalMissionName( ubMission ) : "-",
			ubSupply, ubMorale, ubIntel, zReason );
		fclose( fp );
	}
}

void VR_StrategicDiagnosticsGroupOrder( GROUP *pGroup, UINT8 ubTargetSector,
	UINT8 ubLegacyIntention, UINT32 uiMoveCode )
{
	if( !pGroup || !pGroup->pEnemyGroup ) return;
	++guiVRStrategicDecisionSerial;
	if( !guiVRStrategicDecisionSerial ) ++guiVRStrategicDecisionSerial;

	CHAR8 zReason[256];
	sprintf( zReason,
		"mobile order: legacy_intention=%u move_policy=%u operational_reason=%s",
		ubLegacyIntention, uiMoveCode,
		VR_OperationalReasonName( pGroup->pEnemyGroup->ubOperationalLastDecisionReason ) );
	VR_StrategicDiagnosticsRecord( "GROUP_ORDER", "mobile_group",
		ubLegacyIntention, pGroup,
		SECTOR( pGroup->ubSectorX, pGroup->ubSectorY ), ubTargetSector,
		pGroup->ubGroupSize, uiMoveCode, zReason );
}

void VR_StrategicDiagnosticsIntel( UINT8 ubSectorID, UINT8 ubConfidence,
	const CHAR8 *pReason )
{
	++guiVRStrategicDecisionSerial;
	if( !guiVRStrategicDecisionSerial ) ++guiVRStrategicDecisionSerial;
	VR_StrategicDiagnosticsRecord( "INTEL_REPORT", "sector", ubSectorID,
		NULL, ubSectorID, ubSectorID, ubConfidence, 0, pReason );
}

void VR_StrategicDiagnosticsHourly()
{
	++guiVRStrategicDecisionSerial;
	if( !guiVRStrategicDecisionSerial ) ++guiVRStrategicDecisionSerial;

	INT32 iGroups = 0;
	INT32 iTroops = 0;
	for( GROUP *pGroup = gpGroupList; pGroup; pGroup = pGroup->next )
	{
		if( pGroup->fPlayer || !pGroup->pEnemyGroup ) continue;
		VR_EnsureEnemyFormationState( pGroup );
		++iGroups;
		iTroops += pGroup->ubGroupSize;

		INT32 iTarget = pGroup->pEnemyGroup->ubOperationalTargetSectorID;
		CHAR8 zReason[256];
		sprintf( zReason,
			"hourly formation state: reserve=%s retreat_count=%u flags=%u last_known_player=%c%d",
			VR_OperationalReserveRoleName( pGroup->pEnemyGroup->ubOperationalReserveRole ),
			pGroup->pEnemyGroup->ubOperationalRetreatCount,
			pGroup->pEnemyGroup->usOperationalFlags,
			SECTORY( pGroup->pEnemyGroup->ubOperationalLastKnownPlayerSectorID ) + 'A' - 1,
			SECTORX( pGroup->pEnemyGroup->ubOperationalLastKnownPlayerSectorID ) );

		VR_StrategicDiagnosticsRecord( "GROUP_STATUS", "mobile_group",
			pGroup->pEnemyGroup->ubOperationalMission, pGroup,
			SECTOR( pGroup->ubSectorX, pGroup->ubSectorY ), iTarget,
			pGroup->ubGroupSize, pGroup->pEnemyGroup->ubOperationalSupply, zReason );
	}

	CHAR8 zSummary[256];
	sprintf( zSummary, "hourly campaign snapshot: groups=%d mobile_troops=%d", iGroups, iTroops );
	VR_StrategicDiagnosticsRecord( "CAMPAIGN_SNAPSHOT", "enemy_army",
		iGroups, NULL, -1, -1, iTroops, giReinforcementPool, zSummary );
}
