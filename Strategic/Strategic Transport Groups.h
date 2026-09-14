#ifndef _VR_STRATEGIC_TRANSPORT_GROUPS_H_
#define _VR_STRATEGIC_TRANSPORT_GROUPS_H_

#include "types.h"

// Staging API. Implementation is deliberately dormant until the feature gate is enabled.
BOOLEAN VR_StrategicTransportGroupsEnabled();
void VR_InitStrategicTransportGroups();
void VR_UpdateStrategicTransportGroups();

#endif
