#ifndef __AI_DIAGNOSTICS_H
#define __AI_DIAGNOSTICS_H

#include "types.h"

class SOLDIERTYPE;

// Decision-level forensic stream used by the daily Companion review.
// Candidate logging is intentionally limited to shortlisted material choices.
UINT32 AITraceBeginDecision(SOLDIERTYPE *pSoldier, const CHAR8 *pSource,
	INT32 sTargetSpot, INT8 bIntent, INT8 bRole);
void AITraceCandidate(SOLDIERTYPE *pSoldier, UINT32 uiDecisionID, const CHAR8 *pSource,
	INT8 bAction, INT32 sCandidateGrid, INT32 iScore, INT32 iRouteCost,
	INT32 iSupport, INT32 iCrossfire, const CHAR8 *pReason);
void AITraceReject(SOLDIERTYPE *pSoldier, UINT32 uiDecisionID, const CHAR8 *pSource,
	INT8 bAction, INT32 sCandidateGrid, const CHAR8 *pReason);
void AITraceSelect(SOLDIERTYPE *pSoldier, UINT32 uiDecisionID, const CHAR8 *pSource,
	INT8 bAction, INT32 sCandidateGrid, INT32 iScore, INT32 iRunnerUpScore,
	BOOLEAN fFrictionChangedChoice, const CHAR8 *pReason);
void AITraceOutcome(SOLDIERTYPE *pSoldier, UINT32 uiDecisionID, const CHAR8 *pOutcome,
	INT32 iValue1, INT32 iValue2, const CHAR8 *pReason);
UINT32 AITraceCurrentDecision(SOLDIERTYPE *pSoldier);

#endif
