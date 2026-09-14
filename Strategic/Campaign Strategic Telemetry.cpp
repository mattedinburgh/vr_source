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

