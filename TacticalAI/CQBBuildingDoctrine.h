#ifndef __VR_CQB_BUILDING_DOCTRINE_H
#define __VR_CQB_BUILDING_DOCTRINE_H

#include "Types.h"
#include "Soldier Control.h"

// Canonical CQB/building doctrine API.
//
// This module is part of the unified tactical AI. DecideAction owns the top-level
// priority order; this module only evaluates and executes building-specific movement
// after senior survival/casualty priorities (and, in BLACK, immediate attacks) have
// had first refusal. It must not become a second top-level combat decision engine.

enum VRCQB_STATE
{
	VRCQB_STATE_NONE = 0,
	VRCQB_STATE_ASSAULT,
	VRCQB_STATE_HOLD,
	VRCQB_STATE_DELAY_FALLBACK,
	VRCQB_STATE_COUNTERATTACK,
	VRCQB_STATE_SECURE,
	VRCQB_STATE_MAX
};

enum VRCQB_ROLE
{
	VRCQB_ROLE_NONE = 0,
	VRCQB_ROLE_POINT,
	VRCQB_ROLE_COVER,
	VRCQB_ROLE_SUPPORT,
	VRCQB_ROLE_SECURITY,
	VRCQB_ROLE_HOLD,
	VRCQB_ROLE_RESERVE,
	VRCQB_ROLE_MAX
};

enum VRCQB_REASON
{
	VRCQB_REASON_NONE = 0,
	VRCQB_REASON_ANCHORED_HOLD,
	VRCQB_REASON_THREAT_IN_ROOM,
	VRCQB_REASON_THREAT_IN_BUILDING,
	VRCQB_REASON_APPROACH_INDOOR_THREAT,
	VRCQB_REASON_HIGH_RISK_FALLBACK,
	VRCQB_REASON_DISENGAGEMENT,
	VRCQB_REASON_SECURE_FOOTHOLD,
	VRCQB_REASON_LOCAL_COUNTERATTACK,
	VRCQB_REASON_SECURITY_REFUSES_COMPLEX_ASSAULT,
	VRCQB_REASON_INSUFFICIENT_ENTRY_SUPPORT,
	VRCQB_REASON_ASSAULT_HESITATION,
	VRCQB_REASON_MAX
};

// CQB training is deliberately more granular than the shared BASIC/REGULAR/ELITE
// competence tier. It describes what kind of building problem an element can
// plausibly solve, not accuracy, AP, perception or reaction-time bonuses.
enum VRCQB_TRAINING_PROFILE
{
	VRCQB_TRAINING_SECURITY_BASIC = 0,
	VRCQB_TRAINING_LINE_BASIC,
	VRCQB_TRAINING_LINE_COMMANDED,
	VRCQB_TRAINING_VETERAN,
	VRCQB_TRAINING_ELITE_MOBILE,
	VRCQB_TRAINING_ELITE_GUARD,
	VRCQB_TRAINING_MAX
};

enum VRCQB_CAPABILITY
{
	VRCQB_CAP_NONE					= 0,
	VRCQB_CAP_FATAL_FUNNEL_AWARE	= 1u << 0,
	VRCQB_CAP_TWO_MAN_ENTRY			= 1u << 1,
	VRCQB_CAP_SECTOR_DECONFLICTION	= 1u << 2,
	VRCQB_CAP_REAR_SECURITY			= 1u << 3,
	VRCQB_CAP_ALTERNATE_ENTRY		= 1u << 4,
	VRCQB_CAP_DYNAMIC_REPLAN		= 1u << 5,
	VRCQB_CAP_PROACTIVE_SUPPORT		= 1u << 6,
	VRCQB_CAP_DEFENSE_IN_DEPTH		= 1u << 7,
	VRCQB_CAP_LOCAL_COUNTERATTACK	= 1u << 8,
	VRCQB_CAP_COMPLEX_ROOM_FLOW		= 1u << 9
};

struct VRCQB_TRAINING_MODEL
{
	VRCQB_TRAINING_PROFILE eProfile;
	UINT32 uiCapabilities;

	// 0-100 behavioural skill bands. These are planner weights only.
	UINT8 ubAssaultSkill;
	UINT8 ubHoldSkill;
	UINT8 ubSectorDiscipline;
	UINT8 ubReplanSkill;

	// Deliberate human imperfection. Runtime implementation combines these with
	// AIPlannerReliability(), morale, stress and suppression.
	UINT8 ubHesitationChance;
	UINT8 ubThresholdMistakeChance;
	UINT8 ubCoordinationBreakChance;

	UINT8 ubPreferredClearTeamSize;
	UINT8 ubMaxCoordinatedMovers;
};

struct VRCQB_CONTEXT
{
	BOOLEAN fValid;
	BOOLEAN fInsideRoom;
	BOOLEAN fOnRoof;
	BOOLEAN fUnderFire;
	BOOLEAN fKnownThreatIndoor;
	BOOLEAN fKnownThreatInSameRoom;
	BOOLEAN fKnownThreatInSameBuilding;
	BOOLEAN fNearEntry;
	BOOLEAN fEntryExposed;
	BOOLEAN fHasLocalSupport;
	BOOLEAN fFallbackAvailable;

	UINT16 usRoomNo;
	UINT16 usThreatRoomNo;
	UINT8 ubBuildingID;
	UINT8 ubThreatBuildingID;
	UINT8 ubKnownThreats;
	UINT8 ubLocalFriends;

	INT32 sPrimaryKnownThreat;
	INT8 bPrimaryKnownThreatLevel;
	INT32 sPreferredEntry;
	INT32 sPreferredFoothold;
	INT32 sPreferredFallback;
};

struct VRCQB_ASSESSMENT
{
	VRCQB_STATE eState;
	VRCQB_ROLE eRole;
	VRCQB_REASON eReason;
	VRCQB_TRAINING_PROFILE eTrainingProfile;
	UINT32 uiCapabilities;

	INT32 sAnchorGridNo;
	INT32 sEntryGridNo;
	INT32 sTargetGridNo;
	INT32 sFallbackGridNo;

	INT32 iPositionScore;
	INT32 iRiskScore;
	UINT16 usKnownThreatExposure;

	UINT8 ubConfidence;
	UINT8 ubPlannerReliability;
	UINT8 ubEffectiveSkill;
	UINT8 ubMaxCoordinatedMovers;
};

BOOLEAN VRCQB_IsRuntimeEnabled(void);

VRCQB_TRAINING_PROFILE VRCQB_GetTrainingProfile(SOLDIERTYPE *pSoldier);
BOOLEAN VRCQB_GetTrainingModel(SOLDIERTYPE *pSoldier, VRCQB_TRAINING_MODEL *pModel);
BOOLEAN VRCQB_HasCapability(const VRCQB_TRAINING_MODEL *pModel, UINT32 uiCapability);

BOOLEAN VRCQB_BuildContext(SOLDIERTYPE *pSoldier, VRCQB_CONTEXT *pContext);
BOOLEAN VRCQB_Assess(SOLDIERTYPE *pSoldier, const VRCQB_CONTEXT *pContext, VRCQB_ASSESSMENT *pAssessment);

INT8 VRCQB_DecideAction(SOLDIERTYPE *pSoldier, BOOLEAN fCanMove, BOOLEAN fAllowAssault);


INT32 VRCQB_ScorePosition(SOLDIERTYPE *pSoldier, const VRCQB_CONTEXT *pContext,
	const VRCQB_TRAINING_MODEL *pModel, VRCQB_STATE eState, VRCQB_ROLE eRole,
	INT32 sCandidateGridNo);

const CHAR8 *VRCQB_StateName(VRCQB_STATE eState);
const CHAR8 *VRCQB_RoleName(VRCQB_ROLE eRole);
const CHAR8 *VRCQB_ProfileName(VRCQB_TRAINING_PROFILE eProfile);
const CHAR8 *VRCQB_ReasonName(VRCQB_REASON eReason);

void VRCQB_InvalidateSoldierPlan(SOLDIERTYPE *pSoldier);
void VRCQB_ResetTransientState(void);

#endif
