#ifndef _VR_ASD_H_
#define _VR_ASD_H_

#include "types.h"

// Staging API for 1.13-style strategic asset purchasing and enemy helicopter operations.
BOOLEAN VR_ASDEnabled();
BOOLEAN VR_EnemyHelicoptersEnabled();
void VR_InitASD();
void VR_InitEnemyHelicopters();

#endif
