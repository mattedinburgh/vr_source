#include "ASD.h"
#include "Strategic Modernization.h"

BOOLEAN VR_ASDEnabled()
{
	return VR_ASD_ENABLED ? TRUE : FALSE;
}

BOOLEAN VR_EnemyHelicoptersEnabled()
{
	return VR_ENEMY_HELICOPTERS_ENABLED ? TRUE : FALSE;
}

void VR_InitASD()
{
	if( !VR_ASDEnabled() )
		return;
}

void VR_InitEnemyHelicopters()
{
	if( !VR_EnemyHelicoptersEnabled() )
		return;
}
