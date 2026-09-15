#include "Strategic Transport Groups.h"
#include "Strategic Modernization.h"

BOOLEAN VR_StrategicTransportGroupsEnabled()
{
	return VR_STRATEGIC_TRANSPORT_GROUPS_ENABLED ? TRUE : FALSE;
}

void VR_InitStrategicTransportGroups()
{
	if( !VR_StrategicTransportGroupsEnabled() )
		return;

	// Port target: current 1.13 Strategic Transport Groups implementation.
	// Intentionally empty on the inactive staging branch until dependencies are migrated.
}

void VR_UpdateStrategicTransportGroups()
{
	if( !VR_StrategicTransportGroupsEnabled() )
		return;
}
