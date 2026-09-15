#ifndef _VR_STRATEGIC_AI_DIAGNOSTICS_H_
#define _VR_STRATEGIC_AI_DIAGNOSTICS_H_

#include "types.h"

struct GROUP;

void VR_StrategicDiagnosticsInit();
void VR_StrategicDiagnosticsHourly();
void VR_StrategicDiagnosticsRecord( const CHAR8 *pEvent, const CHAR8 *pSubject,
	INT32 iSubjectID, GROUP *pGroup, INT32 iSourceSector, INT32 iTargetSector,
	INT32 iScore, INT32 iAux, const CHAR8 *pReason );
void VR_StrategicDiagnosticsGroupOrder( GROUP *pGroup, UINT8 ubTargetSector,
	UINT8 ubLegacyIntention, UINT32 uiMoveCode );
void VR_StrategicDiagnosticsIntel( UINT8 ubSectorID, UINT8 ubConfidence,
	const CHAR8 *pReason );

#endif
