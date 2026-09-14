#include <stdio.h>

#include "Campaign Strategic Telemetry.h"
#include "Strategic Movement.h"
#include "Strategic Operational AI.h"
#include "Strategic Status.h"
#include "Game Clock.h"
#include "strategic.h"
#include "strategicmap.h"

extern INT32 giReinforcementPool;
extern INT32 giRequestPoints;
extern INT32 giReinforcementPoints;
extern UINT8 gubQueenPriorityPhase;

static BOOLEAN gfVRCampaignBlackBoxEnabled = TRUE;
static UINT32 guiVRCampaignDecisionSerial = 0;
static UINT32 guiVRCampaignCurrentDecision = 0;
static UINT32 guiVRCampaignPlanSerial = 0;
static UINT32 guiVRGroupPlanID[256] = { 0 };
static UINT32 guiVRGroupPlanDecisionID[256] = { 0 };
static UINT32 guiVRGroupPlanStartedAt[256] = { 0 };
static UINT8 gubVRGroupPlanTarget[256] = { 0 };
static UINT8 gubVRGroupPlanIntention[256] = { 0 };

static void VR_CampaignSectorName( INT32 iSectorID, CHAR8 *pOut )
{
	if( iSectorID < 0 || iSectorID > 255 )
	{
		sprintf( pOut, "-" );
		return;
	}
	sprintf( pOut, "%c%d", SECTORY( (UINT8)iSectorID ) + 'A' - 1, SECTORX( (UINT8)iSectorID ) );
}

static void VR_CampaignEnsureHeaders()
{
	if( !gfVRCampaignBlackBoxEnabled )
		return;

	FILE *fp = fopen( "Campaign AI Black Box.tsv", "a+" );
	if( fp )
	{
		fseek( fp, 0, SEEK_END );
		if( ftell( fp ) == 0 )
		{
			fprintf( fp,
				"schema\tworld_min\tday\thour\tminute\tdecision_id\tplan_id\tformation_id\tmission\treserve_role\tevent\tsubject\tsubject_id\tgroup_id\tsource\ttarget\tscore\taux\tgroup_size\tsupply\tmorale\tintel_confidence\tpool\trequest_points\treinforcement_points\tprogress\tqueen_phase\treason\n" );
		}
		fclose( fp );
	}

	fp = fopen( "Campaign AI Companion.txt", "a+" );
	if( fp )
	{
		fseek( fp, 0, SEEK_END );
		if( ftell( fp ) == 0 )
		{
			fprintf( fp, "VENGEANCE CAMPAIGN AI COMPANION v%d\n", VR_CAMPAIGN_COMPANION_SCHEMA_VERSION );
			fprintf( fp, "Strategic observations, alternatives, decisions, plans and outcomes.\n\n" );
		}
		fclose( fp );
	}
}

UINT32 VR_CampaignCurrentDecisionID()
{
	return guiVRCampaignCurrentDecision;
}

BOOLEAN VR_CampaignDecisionActive()
{
	return guiVRCampaignCurrentDecision != 0;
}

UINT32 VR_GetSAICampaignPlanID( UINT8 ubGroupID )
{
	return guiVRGroupPlanID[ ubGroupID ];
}

UINT32 VR_GetSAICampaignPlanDecisionID( UINT8 ubGroupID )
{
	return guiVRGroupPlanDecisionID[ ubGroupID ];
}

void VR_CampaignRecord(
	const CHAR8 *pEvent,
	const CHAR8 *pSubject,
	INT32 iSubjectID,
	INT32 iGroupID,
	INT32 iSourceSector,
	INT32 iTargetSector,
	INT32 iScore,
	INT32 iAux,
	const CHAR8 *pReason )
{
	if( !gfVRCampaignBlackBoxEnabled )
		return;

	VR_CampaignEnsureHeaders();

	CHAR8 zSource[16];
	CHAR8 zTarget[16];
	VR_CampaignSectorName( iSourceSector, zSource );
	VR_CampaignSectorName( iTargetSector, zTarget );

	UINT32 uiPlan = 0;
	GROUP *pGroup = NULL;
	if( iGroupID >= 0 && iGroupID < 256 )
	{
		uiPlan = guiVRGroupPlanID[ iGroupID ];
		pGroup = GetGroup( (UINT8)iGroupID );
	}

	UINT16 usFormation = 0;
	UINT8 ubMission = 0;
	UINT8 ubReserve = 0;
	UINT8 ubSupply = 0;
	UINT8 ubMorale = 0;
	UINT8 ubIntel = 0;
	UINT8 ubGroupSize = 0;
	if( pGroup )
	{
		ubGroupSize = pGroup->ubGroupSize;
		if( !pGroup->fPlayer && pGroup->pEnemyGroup )
		{
			// Recorder never initializes gameplay state: it only observes state that the
			// operational layer has already established. This prevents logging recursion.
			usFormation = pGroup->pEnemyGroup->usFormationID;
			ubMission = pGroup->pEnemyGroup->ubOperationalMission;
			ubReserve = pGroup->pEnemyGroup->ubOperationalReserveRole;
			ubSupply = pGroup->pEnemyGroup->ubOperationalSupply;
			ubMorale = pGroup->pEnemyGroup->ubOperationalMorale;
			ubIntel = pGroup->pEnemyGroup->ubOperationalIntelConfidence;
		}
	}

	FILE *fp = fopen( "Campaign AI Black Box.tsv", "a" );
	if( fp )
	{
		fprintf( fp,
			"%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%s\t%s\t%d\t%d\t%s\t%s\t%d\t%d\t%u\t%u\t%u\t%u\t%d\t%d\t%d\t%d\t%u\t%s\n",
			VR_CAMPAIGN_COMPANION_SCHEMA_VERSION,
			GetWorldTotalMin(),
			GetWorldDay(),
			GetWorldHour(),
			GetWorldMinutesInDay() % 60,
			guiVRCampaignCurrentDecision,
			uiPlan,
			usFormation,
			ubMission,
			ubReserve,
			pEvent ? pEvent : "-",
			pSubject ? pSubject : "-",
			iSubjectID,
			iGroupID,
			zSource,
			zTarget,
			iScore,
			iAux,
			ubGroupSize,
			ubSupply,
			ubMorale,
			ubIntel,
			giReinforcementPool,
			giRequestPoints,
			giReinforcementPoints,
			CurrentPlayerProgressPercentage(),
			gubQueenPriorityPhase,
			pReason ? pReason : "-" );
		fclose( fp );
	}

	fp = fopen( "Campaign AI Companion.txt", "a" );
	if( fp )
	{
		fprintf( fp,
			"[Day %02u %02u:%02u][D%06u][P%06u][F%04u][%s] %s #%d",
			GetWorldDay(), GetWorldHour(), GetWorldMinutesInDay() % 60,
			guiVRCampaignCurrentDecision, uiPlan, usFormation,
			pEvent ? pEvent : "-", pSubject ? pSubject : "-", iSubjectID );
		if( iGroupID >= 0 )
			fprintf( fp, " group=%d", iGroupID );
		if( iSourceSector >= 0 )
			fprintf( fp, " from=%s", zSource );
		if( iTargetSector >= 0 )
			fprintf( fp, " to=%s", zTarget );
		if( pGroup && pGroup->pEnemyGroup )
			fprintf( fp, " mission=%s reserve=%s supply=%u morale=%u intel=%u",
				VR_OperationalMissionName( ubMission ),
				VR_OperationalReserveRoleName( ubReserve ),
				ubSupply, ubMorale, ubIntel );
		fprintf( fp, " score=%d aux=%d | %s\n", iScore, iAux, pReason ? pReason : "-" );
		fclose( fp );
	}
}

UINT32 VR_CampaignBeginDecision( const CHAR8 *pTrigger )
{
	++guiVRCampaignDecisionSerial;
	if( guiVRCampaignDecisionSerial == 0 )
		guiVRCampaignDecisionSerial = 1;
	guiVRCampaignCurrentDecision = guiVRCampaignDecisionSerial;
	VR_CampaignRecord( "DECISION_BEGIN", "queen", -1, -1, -1, -1,
		giRequestPoints, giReinforcementPoints, pTrigger ? pTrigger : "strategic evaluation" );
	return guiVRCampaignCurrentDecision;
}

void VR_CampaignEndDecision( const CHAR8 *pReason )
{
	VR_CampaignRecord( "DECISION_END", "queen", -1, -1, -1, -1, 0, 0,
		pReason ? pReason : "complete" );
	guiVRCampaignCurrentDecision = 0;
}

UINT32 VR_CampaignStartOrRefreshPlan( GROUP *pGroup, UINT8 ubTargetSector, UINT8 ubIntention, UINT32 uiMoveCode )
{
	if( !pGroup || !pGroup->pEnemyGroup )
		return 0;

	UINT8 ubID = pGroup->ubGroupID;
	CHAR8 zReason[192];
	if( !guiVRGroupPlanID[ ubID ] ||
		gubVRGroupPlanTarget[ ubID ] != ubTargetSector ||
		gubVRGroupPlanIntention[ ubID ] != ubIntention )
	{
		++guiVRCampaignPlanSerial;
		if( guiVRCampaignPlanSerial == 0 )
			guiVRCampaignPlanSerial = 1;
		guiVRGroupPlanID[ ubID ] = guiVRCampaignPlanSerial;
		guiVRGroupPlanDecisionID[ ubID ] = guiVRCampaignCurrentDecision;
		guiVRGroupPlanStartedAt[ ubID ] = GetWorldTotalMin();
		gubVRGroupPlanTarget[ ubID ] = ubTargetSector;
		gubVRGroupPlanIntention[ ubID ] = ubIntention;

		sprintf( zReason, "new mobile plan: intention=%u move_policy=%u parent_decision=%u",
			ubIntention, uiMoveCode, guiVRGroupPlanDecisionID[ ubID ] );
		VR_CampaignRecord( "PLAN_BEGIN", "mobile_group", ubIntention, ubID,
			SECTOR( pGroup->ubSectorX, pGroup->ubSectorY ), ubTargetSector,
			pGroup->ubGroupSize, uiMoveCode, zReason );
	}
	else
	{
		sprintf( zReason, "existing plan refreshed: intention=%u move_policy=%u age_minutes=%u",
			ubIntention, uiMoveCode, GetWorldTotalMin() - guiVRGroupPlanStartedAt[ ubID ] );
		VR_CampaignRecord( "PLAN_REFRESH", "mobile_group", ubIntention, ubID,
			SECTOR( pGroup->ubSectorX, pGroup->ubSectorY ), ubTargetSector,
			pGroup->ubGroupSize, uiMoveCode, zReason );
	}
	return guiVRGroupPlanID[ ubID ];
}

void VR_CampaignClosePlan( GROUP *pGroup, const CHAR8 *pOutcome )
{
	if( !pGroup )
		return;
	UINT8 ubID = pGroup->ubGroupID;
	if( !guiVRGroupPlanID[ ubID ] )
		return;

	CHAR8 zReason[192];
	sprintf( zReason, "%s; plan_age_minutes=%u parent_decision=%u",
		pOutcome ? pOutcome : "plan closed",
		GetWorldTotalMin() - guiVRGroupPlanStartedAt[ ubID ],
		guiVRGroupPlanDecisionID[ ubID ] );
	VR_CampaignRecord( "PLAN_END", "mobile_group",
		pGroup->pEnemyGroup ? pGroup->pEnemyGroup->ubIntention : 0,
		ubID, SECTOR( pGroup->ubSectorX, pGroup->ubSectorY ),
		gubVRGroupPlanTarget[ ubID ], pGroup->ubGroupSize,
		GetWorldTotalMin() - guiVRGroupPlanStartedAt[ ubID ], zReason );

	guiVRGroupPlanID[ ubID ] = 0;
	guiVRGroupPlanDecisionID[ ubID ] = 0;
	guiVRGroupPlanStartedAt[ ubID ] = 0;
	gubVRGroupPlanTarget[ ubID ] = 0;
	gubVRGroupPlanIntention[ ubID ] = 0;
}
