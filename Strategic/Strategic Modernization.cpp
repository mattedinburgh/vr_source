#include "Strategic Modernization.h"
#include "Strategic Transport Groups.h"
#include "ASD.h"

VR_STRATEGIC_MODERNIZATION_STATE gVRStrategicModernization;

void VR_InitStrategicModernization()
{
	gVRStrategicModernization.fTeamGroupBridgeReady = VR_STRATEGIC_TEAM_GROUP_FRAMEWORK_STAGED ? TRUE : FALSE;
	gVRStrategicModernization.fTransportGroupsEnabled = VR_StrategicTransportGroupsEnabled();
	gVRStrategicModernization.fEnemyHelicoptersEnabled = VR_EnemyHelicoptersEnabled();
	gVRStrategicModernization.fASDEnabled = VR_ASDEnabled();

	// These are safe no-ops while their gates are disabled.
	VR_InitStrategicTransportGroups();
	VR_InitEnemyHelicopters();
	VR_InitASD();
}

BOOLEAN VR_StrategicModernizationFeatureEnabled( UINT8 ubFeature )
{
	switch( ubFeature )
	{
		case VR_STRATEGIC_FEATURE_TEAM_GROUPS:
			return gVRStrategicModernization.fTeamGroupBridgeReady;
		case VR_STRATEGIC_FEATURE_TRANSPORT_GROUPS:
			return gVRStrategicModernization.fTransportGroupsEnabled;
		case VR_STRATEGIC_FEATURE_ENEMY_HELICOPTERS:
			return gVRStrategicModernization.fEnemyHelicoptersEnabled;
		case VR_STRATEGIC_FEATURE_ASD:
			return gVRStrategicModernization.fASDEnabled;
		default:
			return FALSE;
	}
}
