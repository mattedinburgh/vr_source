#ifndef _VR_STRATEGIC_MODERNIZATION_H_
#define _VR_STRATEGIC_MODERNIZATION_H_

#include "types.h"

// Inactive staging branch only.
// The team-group compatibility bridge is present, but gameplay consumers remain OFF.
#define VR_STRATEGIC_TEAM_GROUP_FRAMEWORK_STAGED 1
#define VR_STRATEGIC_TRANSPORT_GROUPS_ENABLED    0
#define VR_ENEMY_HELICOPTERS_ENABLED             0
#define VR_ASD_ENABLED                           0

enum VR_STRATEGIC_MODERNIZATION_FEATURE
{
	VR_STRATEGIC_FEATURE_TEAM_GROUPS = 0,
	VR_STRATEGIC_FEATURE_TRANSPORT_GROUPS,
	VR_STRATEGIC_FEATURE_ENEMY_HELICOPTERS,
	VR_STRATEGIC_FEATURE_ASD
};

typedef struct VR_STRATEGIC_MODERNIZATION_STATE
{
	BOOLEAN fTeamGroupBridgeReady;
	BOOLEAN fTransportGroupsEnabled;
	BOOLEAN fEnemyHelicoptersEnabled;
	BOOLEAN fASDEnabled;
} VR_STRATEGIC_MODERNIZATION_STATE;

extern VR_STRATEGIC_MODERNIZATION_STATE gVRStrategicModernization;

void VR_InitStrategicModernization();
BOOLEAN VR_StrategicModernizationFeatureEnabled( UINT8 ubFeature );

#endif
