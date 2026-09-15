#ifndef _VR_CAMPAIGN_STRATEGIC_TELEMETRY_H_
#define _VR_CAMPAIGN_STRATEGIC_TELEMETRY_H_

#include "types.h"

struct GROUP;

#define VR_CAMPAIGN_COMPANION_SCHEMA_VERSION 1

UINT32 VR_CampaignBeginDecision( const CHAR8 *pTrigger );
void VR_CampaignEndDecision( const CHAR8 *pReason );
UINT32 VR_CampaignCurrentDecisionID();
BOOLEAN VR_CampaignDecisionActive();

void VR_CampaignRecord(
	const CHAR8 *pEvent,
	const CHAR8 *pSubject,
	INT32 iSubjectID,
	INT32 iGroupID,
	INT32 iSourceSector,
	INT32 iTargetSector,
	INT32 iScore,
	INT32 iAux,
	const CHAR8 *pReason );

UINT32 VR_CampaignStartOrRefreshPlan( GROUP *pGroup, UINT8 ubTargetSector, UINT8 ubIntention, UINT32 uiMoveCode );
void VR_CampaignClosePlan( GROUP *pGroup, const CHAR8 *pOutcome );
UINT32 VR_GetSAICampaignPlanID( UINT8 ubGroupID );
UINT32 VR_GetSAICampaignPlanDecisionID( UINT8 ubGroupID );

#endif
