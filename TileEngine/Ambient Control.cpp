#include "builddefines.h"

#ifdef PRECOMPILEDHEADERS
	#include "TileEngine All.h"
#else
	#include "stdio.h"
	#include "sgp.h"
	#include "Ambient types.h"
	#include "fileman.h"
	#include "environment.h"
	#include "Sound Control.h"
	#include "Game Events.h"
	#include "Ambient Control.h"
	#include "lighting.h"
	#include "Random.h"
#endif

#include "Campaign Types.h"
#include "strategicmap.h"
#include "worlddef.h"
#include "INIReader.h"
#include "Game Clock.h"
#include "Overhead.h"

AMBIENTDATA_STRUCT		gAmbData[ MAX_AMBIENT_SOUNDS ];
INT16									gsNumAmbData = 0;

/*
 * Vengeance adaptive sector ambience
 * -----------------------------------
 * This is intentionally a small state-driven soundscape layer, not another
 * pile of looping files.  A sector profile owns:
 *   - a subtle loop for each time phase (dawn/day/dusk/night, with fallbacks)
 *   - one random-container style pool of contextual one-shots per phase
 *   - weighted selection, no immediate repeats, random stereo placement
 *   - rain attenuation / lower event density
 *   - combat ducking and (by default) complete one-shot suppression
 *
 * Legacy JA2 .bad ambience is untouched and remains the fallback when
 * SectorAmbience.ini is absent, disabled, or has no usable profile.
 */

#define VR_AMBIENCE_PHASES             4
#define VR_AMBIENCE_MAX_PHASE_SOUNDS   8
#define VR_AMBIENCE_PATH_SIZE          260

enum VR_AMBIENCE_PHASE
{
	VR_AMBIENCE_DAWN = 0,
	VR_AMBIENCE_DAY,
	VR_AMBIENCE_DUSK,
	VR_AMBIENCE_NIGHT
};

typedef struct
{
	CHAR8  szSound[ VR_AMBIENCE_MAX_PHASE_SOUNDS ][ VR_AMBIENCE_PATH_SIZE ];
	UINT16 usWeight[ VR_AMBIENCE_MAX_PHASE_SOUNDS ];
	UINT8  ubCount;
	UINT32 uiMinTime;
	UINT32 uiMaxTime;
	UINT32 uiVolumeMin;
	UINT32 uiVolumeMax;
	UINT32 uiPanMin;
	UINT32 uiPanMax;
} VR_AMBIENCE_PHASE_DATA;

typedef struct
{
	BOOLEAN fActive;
	CHAR8   szProfile[ 64 ];
	CHAR8   szLoop[ VR_AMBIENCE_PHASES ][ VR_AMBIENCE_PATH_SIZE ];
	UINT32  uiLoopVolume[ VR_AMBIENCE_PHASES ];
	VR_AMBIENCE_PHASE_DATA Phase[ VR_AMBIENCE_PHASES ];

	UINT32 uiCombatLoopPercent;
	UINT32 uiRainLoopPercent;
	UINT32 uiRainOneShotVolumePercent;
	UINT32 uiRainOneShotIntervalPercent;
	UINT32 uiFadeStepMs;
	BOOLEAN fOneShotsInCombat;

	UINT8  ubCurrentPhase;
	INT8   bLastOneShot;
	UINT32 uiNextOneShotTime;
	UINT32 uiLoopHandle;
	UINT32 uiCurrentLoopVolume;
	UINT32 uiTargetLoopVolume;
	UINT32 uiLastFadeTick;
	BOOLEAN fLastCombat;
	BOOLEAN fLastRain;
	CHAR8   szCurrentLoop[ VR_AMBIENCE_PATH_SIZE ];
} VR_AMBIENCE_RUNTIME;

static VR_AMBIENCE_RUNTIME gVRAmbience;

static UINT8 GetVRAmbiencePhase( )
{
	// Keep these boundaries aligned with environment.cpp:
	// dawn 06:47, day 07:05, dusk 20:57, night 21:15.
	const UINT32 uiMinutes = GetWorldMinutesInDay();
	const UINT32 uiDawnStart = 6 * 60 + 47;
	const UINT32 uiDayStart = 7 * 60 + 5;
	const UINT32 uiDuskStart = 20 * 60 + 57;
	const UINT32 uiNightStart = 21 * 60 + 15;

	if ( uiMinutes >= uiDawnStart && uiMinutes < uiDayStart )
		return VR_AMBIENCE_DAWN;
	if ( uiMinutes >= uiDayStart && uiMinutes < uiDuskStart )
		return VR_AMBIENCE_DAY;
	if ( uiMinutes >= uiDuskStart && uiMinutes < uiNightStart )
		return VR_AMBIENCE_DUSK;
	return VR_AMBIENCE_NIGHT;
}

static BOOLEAN IsVRAmbienceRaining( )
{
	return ( guiEnvWeather & ( WEATHER_FORECAST_DRIZZLE | WEATHER_FORECAST_SHOWERS | WEATHER_FORECAST_THUNDERSHOWERS ) ) != 0;
}

static BOOLEAN IsVRAmbienceCombat( )
{
	return ( gTacticalStatus.uiFlags & INCOMBAT ) != 0;
}

static void StopVRSectorAmbienceLoop( )
{
	if ( gVRAmbience.uiLoopHandle != NO_SAMPLE )
	{
		SoundStop( gVRAmbience.uiLoopHandle );
		gVRAmbience.uiLoopHandle = NO_SAMPLE;
	}
	gVRAmbience.szCurrentLoop[ 0 ] = 0;
}

static void ResetVRSectorAmbience( )
{
	StopVRSectorAmbienceLoop( );
	memset( &gVRAmbience, 0, sizeof( gVRAmbience ) );
	gVRAmbience.uiLoopHandle = NO_SAMPLE;
	gVRAmbience.bLastOneShot = -1;
}

static void ReadVRAmbiencePhaseData( CIniReader &ini, STR8 szSection, const CHAR8 *szPrefix, const VR_AMBIENCE_PHASE_DATA *pFallback, VR_AMBIENCE_PHASE_DATA *pOut )
{
	CHAR8 szKey[ 64 ];
	CHAR8 szValue[ VR_AMBIENCE_PATH_SIZE ];
	UINT32 uiBaseVolume;
	UINT32 uiDefaultMin;
	UINT32 uiDefaultMax;
	UINT32 uiDefaultPanMin;
	UINT32 uiDefaultPanMax;
	INT32 i;

	memset( pOut, 0, sizeof( VR_AMBIENCE_PHASE_DATA ) );

	uiDefaultMin = pFallback ? pFallback->uiMinTime : 18000;
	uiDefaultMax = pFallback ? pFallback->uiMaxTime : 52000;
	uiDefaultPanMin = pFallback ? pFallback->uiPanMin : 28;
	uiDefaultPanMax = pFallback ? pFallback->uiPanMax : 228;
	uiBaseVolume = pFallback ? ( pFallback->uiVolumeMin + pFallback->uiVolumeMax ) / 2 : 25;

	sprintf( szKey, "%s_MIN_MS", szPrefix );
	pOut->uiMinTime = (UINT32)ini.ReadInteger( szSection, szKey, uiDefaultMin, 1000, 900000 );
	sprintf( szKey, "%s_MAX_MS", szPrefix );
	pOut->uiMaxTime = (UINT32)ini.ReadInteger( szSection, szKey, uiDefaultMax, 1000, 900000 );
	if ( pOut->uiMaxTime < pOut->uiMinTime )
	{
		UINT32 uiSwap = pOut->uiMinTime;
		pOut->uiMinTime = pOut->uiMaxTime;
		pOut->uiMaxTime = uiSwap;
	}

	sprintf( szKey, "%s_VOLUME", szPrefix );
	uiBaseVolume = (UINT32)ini.ReadInteger( szSection, szKey, uiBaseVolume, 0, 127 );
	sprintf( szKey, "%s_VOLUME_MIN", szPrefix );
	pOut->uiVolumeMin = (UINT32)ini.ReadInteger( szSection, szKey, (INT32)( uiBaseVolume > 3 ? uiBaseVolume - 3 : 0 ), 0, 127 );
	sprintf( szKey, "%s_VOLUME_MAX", szPrefix );
	pOut->uiVolumeMax = (UINT32)ini.ReadInteger( szSection, szKey, (INT32)( uiBaseVolume < 124 ? uiBaseVolume + 3 : 127 ), 0, 127 );
	if ( pOut->uiVolumeMax < pOut->uiVolumeMin )
	{
		UINT32 uiSwap = pOut->uiVolumeMin;
		pOut->uiVolumeMin = pOut->uiVolumeMax;
		pOut->uiVolumeMax = uiSwap;
	}

	sprintf( szKey, "%s_PAN_MIN", szPrefix );
	pOut->uiPanMin = (UINT32)ini.ReadInteger( szSection, szKey, uiDefaultPanMin, 0, 255 );
	sprintf( szKey, "%s_PAN_MAX", szPrefix );
	pOut->uiPanMax = (UINT32)ini.ReadInteger( szSection, szKey, uiDefaultPanMax, 0, 255 );
	if ( pOut->uiPanMax < pOut->uiPanMin )
	{
		UINT32 uiSwap = pOut->uiPanMin;
		pOut->uiPanMin = pOut->uiPanMax;
		pOut->uiPanMax = uiSwap;
	}

	for ( i = 1; i <= VR_AMBIENCE_MAX_PHASE_SOUNDS; ++i )
	{
		sprintf( szKey, "%s_SOUND_%d", szPrefix, i );
		ini.ReadString( szSection, szKey, "", szValue, sizeof( szValue ) );
		if ( szValue[ 0 ] == 0 )
			continue;

		strncpy( pOut->szSound[ pOut->ubCount ], szValue, VR_AMBIENCE_PATH_SIZE - 1 );
		pOut->szSound[ pOut->ubCount ][ VR_AMBIENCE_PATH_SIZE - 1 ] = 0;

		sprintf( szKey, "%s_WEIGHT_%d", szPrefix, i );
		pOut->usWeight[ pOut->ubCount ] = (UINT16)ini.ReadInteger( szSection, szKey, 1, 1, 1000 );
		++pOut->ubCount;
	}

	// Dawn/dusk profiles are optional.  If they contain no explicit sounds,
	// inherit the nearest full phase instead of going unnaturally silent.
	if ( pOut->ubCount == 0 && pFallback )
	{
		pOut->ubCount = pFallback->ubCount;
		for ( i = 0; i < pFallback->ubCount; ++i )
		{
			strcpy( pOut->szSound[ i ], pFallback->szSound[ i ] );
			pOut->usWeight[ i ] = pFallback->usWeight[ i ];
		}
	}
}

static void ReadVRAmbienceLoopData( CIniReader &ini, STR8 szSection, const CHAR8 *szPrefix, const CHAR8 *szFallbackLoop, UINT32 uiFallbackVolume, CHAR8 *szOutLoop, UINT32 *puiOutVolume )
{
	CHAR8 szKey[ 64 ];
	UINT32 uiGeneralVolume;

	szOutLoop[ 0 ] = 0;
	if ( szFallbackLoop && szFallbackLoop[ 0 ] )
		strcpy( szOutLoop, szFallbackLoop );

	sprintf( szKey, "%s_LOOP", szPrefix );
	ini.ReadString( szSection, szKey, szOutLoop, szOutLoop, VR_AMBIENCE_PATH_SIZE );

	uiGeneralVolume = (UINT32)ini.ReadInteger( szSection, "LOOP_VOLUME", uiFallbackVolume, 0, 127 );
	sprintf( szKey, "%s_LOOP_VOLUME", szPrefix );
	*puiOutVolume = (UINT32)ini.ReadInteger( szSection, szKey, uiGeneralVolume, 0, 127 );
}

static UINT32 GetVRAmbienceTargetLoopVolume( UINT8 ubPhase )
{
	UINT32 uiVolume;

	if ( ubPhase >= VR_AMBIENCE_PHASES )
		return 0;

	uiVolume = gVRAmbience.uiLoopVolume[ ubPhase ];

	if ( IsVRAmbienceRaining() )
		uiVolume = uiVolume * gVRAmbience.uiRainLoopPercent / 100;

	if ( IsVRAmbienceCombat() )
		uiVolume = uiVolume * gVRAmbience.uiCombatLoopPercent / 100;

	return CalculateSoundEffectsVolume( uiVolume );
}

static BOOLEAN StartVRSectorAmbienceLoopForPhase( UINT8 ubPhase )
{
	SOUNDPARMS spParms;
	const CHAR8 *szLoop;

	if ( ubPhase >= VR_AMBIENCE_PHASES )
		return FALSE;

	szLoop = gVRAmbience.szLoop[ ubPhase ];
	if ( szLoop[ 0 ] == 0 )
	{
		StopVRSectorAmbienceLoop( );
		return FALSE;
	}

	// If dawn/day or dusk/night share the same bed, keep it running and only
	// alter the target volume.  This avoids an audible restart at phase edges.
	if ( gVRAmbience.uiLoopHandle != NO_SAMPLE && strcmp( gVRAmbience.szCurrentLoop, szLoop ) == 0 )
	{
		gVRAmbience.uiTargetLoopVolume = GetVRAmbienceTargetLoopVolume( ubPhase );
		return TRUE;
	}

	StopVRSectorAmbienceLoop( );

	memset( &spParms, 0xff, sizeof( SOUNDPARMS ) );
	gVRAmbience.uiTargetLoopVolume = GetVRAmbienceTargetLoopVolume( ubPhase );
	gVRAmbience.uiCurrentLoopVolume = gVRAmbience.uiTargetLoopVolume;
	spParms.uiVolume = gVRAmbience.uiCurrentLoopVolume;
	spParms.uiPan = MIDDLEPAN;
	spParms.uiLoop = LOOPING;
	spParms.uiPriority = GROUP_AMBIENT;

	gVRAmbience.uiLoopHandle = SoundPlay( (STR)szLoop, &spParms );
	if ( gVRAmbience.uiLoopHandle == NO_SAMPLE )
		return FALSE;

	strncpy( gVRAmbience.szCurrentLoop, szLoop, VR_AMBIENCE_PATH_SIZE - 1 );
	gVRAmbience.szCurrentLoop[ VR_AMBIENCE_PATH_SIZE - 1 ] = 0;
	gVRAmbience.uiLastFadeTick = GetTickCount();
	return TRUE;
}

static void ScheduleNextVRAmbienceOneShot( )
{
	const VR_AMBIENCE_PHASE_DATA *pPhase;
	UINT32 uiMinTime;
	UINT32 uiMaxTime;
	UINT32 uiRange;

	if ( !gVRAmbience.fActive || gVRAmbience.ubCurrentPhase >= VR_AMBIENCE_PHASES )
		return;

	pPhase = &gVRAmbience.Phase[ gVRAmbience.ubCurrentPhase ];
	if ( pPhase->ubCount == 0 )
	{
		gVRAmbience.uiNextOneShotTime = 0;
		return;
	}

	uiMinTime = pPhase->uiMinTime;
	uiMaxTime = pPhase->uiMaxTime;

	if ( IsVRAmbienceRaining() )
	{
		uiMinTime = uiMinTime * gVRAmbience.uiRainOneShotIntervalPercent / 100;
		uiMaxTime = uiMaxTime * gVRAmbience.uiRainOneShotIntervalPercent / 100;
	}

	if ( uiMaxTime < uiMinTime )
		uiMaxTime = uiMinTime;

	uiRange = uiMaxTime - uiMinTime;
	gVRAmbience.uiNextOneShotTime = GetTickCount() + uiMinTime + ( uiRange ? Random( uiRange + 1 ) : 0 );
}

static UINT8 ChooseVRAmbienceOneShot( const VR_AMBIENCE_PHASE_DATA *pPhase )
{
	UINT32 uiTotal = 0;
	UINT32 uiPick;
	UINT32 uiRunning = 0;
	UINT8 i;
	BOOLEAN fAvoidLast = ( pPhase->ubCount > 1 && gVRAmbience.bLastOneShot >= 0 && gVRAmbience.bLastOneShot < pPhase->ubCount );

	for ( i = 0; i < pPhase->ubCount; ++i )
	{
		if ( fAvoidLast && i == (UINT8)gVRAmbience.bLastOneShot )
			continue;
		uiTotal += pPhase->usWeight[ i ] ? pPhase->usWeight[ i ] : 1;
	}

	if ( uiTotal == 0 )
		return 0;

	uiPick = Random( uiTotal );
	for ( i = 0; i < pPhase->ubCount; ++i )
	{
		UINT32 uiWeight;
		if ( fAvoidLast && i == (UINT8)gVRAmbience.bLastOneShot )
			continue;

		uiWeight = pPhase->usWeight[ i ] ? pPhase->usWeight[ i ] : 1;
		uiRunning += uiWeight;
		if ( uiPick < uiRunning )
			return i;
	}

	return 0;
}

static void PlayVRAmbienceOneShot( )
{
	const VR_AMBIENCE_PHASE_DATA *pPhase;
	SOUNDPARMS spParms;
	UINT8 ubIndex;
	UINT32 uiVolume;
	UINT32 uiPan;
	UINT32 uiRange;

	if ( !gVRAmbience.fActive || gVRAmbience.ubCurrentPhase >= VR_AMBIENCE_PHASES )
		return;

	if ( IsVRAmbienceCombat() && !gVRAmbience.fOneShotsInCombat )
		return;

	pPhase = &gVRAmbience.Phase[ gVRAmbience.ubCurrentPhase ];
	if ( pPhase->ubCount == 0 )
		return;

	ubIndex = ChooseVRAmbienceOneShot( pPhase );

	uiRange = pPhase->uiVolumeMax - pPhase->uiVolumeMin;
	uiVolume = pPhase->uiVolumeMin + ( uiRange ? Random( uiRange + 1 ) : 0 );
	if ( IsVRAmbienceRaining() )
		uiVolume = uiVolume * gVRAmbience.uiRainOneShotVolumePercent / 100;

	uiRange = pPhase->uiPanMax - pPhase->uiPanMin;
	uiPan = pPhase->uiPanMin + ( uiRange ? Random( uiRange + 1 ) : 0 );

	memset( &spParms, 0xff, sizeof( SOUNDPARMS ) );
	spParms.uiVolume = CalculateSoundEffectsVolume( uiVolume );
	spParms.uiPan = uiPan;
	spParms.uiLoop = 1;
	spParms.uiPriority = GROUP_AMBIENT;

	SoundPlay( pPhase->szSound[ ubIndex ], &spParms );
	gVRAmbience.bLastOneShot = (INT8)ubIndex;
}

static BOOLEAN LoadVRSectorAmbienceProfile( )
{
	CIniReader ini( "SectorAmbience.ini" );
	CHAR8 szSectorKey[ 32 ];
	CHAR8 szTilesetKey[ 16 ];
	CHAR8 szProfile[ 64 ];
	CHAR8 szSection[ 96 ];
	UINT32 uiGeneralLoopVolume;
	BOOLEAN fHasAnySound = FALSE;
	UINT8 i;

	ResetVRSectorAmbience( );
	gsNumAmbData = 0;

	if ( !ini.Is_CIniReader_File_Found() )
		return FALSE;
	if ( !ini.ReadBoolean( "SETTINGS", "ENABLED", TRUE, FALSE ) )
		return FALSE;
	if ( gWorldSectorX <= 0 || gWorldSectorY <= 0 )
		return FALSE;

	if ( gbWorldSectorZ > 0 )
		sprintf( szSectorKey, "%c%d_B%d", 'A' + gWorldSectorY - 1, gWorldSectorX, gbWorldSectorZ );
	else
		sprintf( szSectorKey, "%c%d", 'A' + gWorldSectorY - 1, gWorldSectorX );

	ini.ReadString( "SECTOR_OVERRIDES", szSectorKey, "", szProfile, sizeof( szProfile ) );

	if ( szProfile[ 0 ] == 0 )
	{
		if ( gbWorldSectorZ > 0 )
		{
			ini.ReadString( "SETTINGS", "DEFAULT_UNDERGROUND_PROFILE", "UNDERGROUND", szProfile, sizeof( szProfile ) );
		}
		else
		{
			sprintf( szTilesetKey, "%d", giCurrentTilesetID );
			ini.ReadString( "TILESET_PROFILES", szTilesetKey, "", szProfile, sizeof( szProfile ) );
			if ( szProfile[ 0 ] == 0 )
				ini.ReadString( "SETTINGS", "DEFAULT_PROFILE", "RURAL", szProfile, sizeof( szProfile ) );
		}
	}

	if ( szProfile[ 0 ] == 0 )
		return FALSE;

	sprintf( szSection, "PROFILE_%s", szProfile );

	// Day and night are the authored anchors. Dawn inherits day and dusk inherits
	// night unless a profile explicitly defines those transition pools.
	ReadVRAmbiencePhaseData( ini, szSection, "DAY", NULL, &gVRAmbience.Phase[ VR_AMBIENCE_DAY ] );
	ReadVRAmbiencePhaseData( ini, szSection, "NIGHT", NULL, &gVRAmbience.Phase[ VR_AMBIENCE_NIGHT ] );
	ReadVRAmbiencePhaseData( ini, szSection, "DAWN", &gVRAmbience.Phase[ VR_AMBIENCE_DAY ], &gVRAmbience.Phase[ VR_AMBIENCE_DAWN ] );
	ReadVRAmbiencePhaseData( ini, szSection, "DUSK", &gVRAmbience.Phase[ VR_AMBIENCE_NIGHT ], &gVRAmbience.Phase[ VR_AMBIENCE_DUSK ] );

	uiGeneralLoopVolume = (UINT32)ini.ReadInteger( szSection, "LOOP_VOLUME", 20, 0, 127 );
	ReadVRAmbienceLoopData( ini, szSection, "DAY", "", uiGeneralLoopVolume, gVRAmbience.szLoop[ VR_AMBIENCE_DAY ], &gVRAmbience.uiLoopVolume[ VR_AMBIENCE_DAY ] );
	ReadVRAmbienceLoopData( ini, szSection, "NIGHT", "", uiGeneralLoopVolume, gVRAmbience.szLoop[ VR_AMBIENCE_NIGHT ], &gVRAmbience.uiLoopVolume[ VR_AMBIENCE_NIGHT ] );
	ReadVRAmbienceLoopData( ini, szSection, "DAWN", gVRAmbience.szLoop[ VR_AMBIENCE_DAY ], gVRAmbience.uiLoopVolume[ VR_AMBIENCE_DAY ], gVRAmbience.szLoop[ VR_AMBIENCE_DAWN ], &gVRAmbience.uiLoopVolume[ VR_AMBIENCE_DAWN ] );
	ReadVRAmbienceLoopData( ini, szSection, "DUSK", gVRAmbience.szLoop[ VR_AMBIENCE_NIGHT ], gVRAmbience.uiLoopVolume[ VR_AMBIENCE_NIGHT ], gVRAmbience.szLoop[ VR_AMBIENCE_DUSK ], &gVRAmbience.uiLoopVolume[ VR_AMBIENCE_DUSK ] );

	gVRAmbience.uiCombatLoopPercent = (UINT32)ini.ReadInteger( szSection, "COMBAT_LOOP_PERCENT", 35, 0, 100 );
	gVRAmbience.uiRainLoopPercent = (UINT32)ini.ReadInteger( szSection, "RAIN_LOOP_PERCENT", 72, 0, 100 );
	gVRAmbience.uiRainOneShotVolumePercent = (UINT32)ini.ReadInteger( szSection, "RAIN_ONESHOT_VOLUME_PERCENT", 58, 0, 100 );
	gVRAmbience.uiRainOneShotIntervalPercent = (UINT32)ini.ReadInteger( szSection, "RAIN_ONESHOT_INTERVAL_PERCENT", 175, 100, 500 );
	gVRAmbience.uiFadeStepMs = (UINT32)ini.ReadInteger( szSection, "FADE_STEP_MS", 32, 10, 250 );
	gVRAmbience.fOneShotsInCombat = ini.ReadBoolean( szSection, "ONESHOTS_IN_COMBAT", FALSE, FALSE );

	for ( i = 0; i < VR_AMBIENCE_PHASES; ++i )
	{
		if ( gVRAmbience.szLoop[ i ][ 0 ] != 0 || gVRAmbience.Phase[ i ].ubCount > 0 )
		{
			fHasAnySound = TRUE;
			break;
		}
	}

	if ( !fHasAnySound )
	{
		ResetVRSectorAmbience( );
		return FALSE;
	}

	strncpy( gVRAmbience.szProfile, szProfile, sizeof( gVRAmbience.szProfile ) - 1 );
	gVRAmbience.szProfile[ sizeof( gVRAmbience.szProfile ) - 1 ] = 0;
	gVRAmbience.fActive = TRUE;
	gVRAmbience.ubCurrentPhase = GetVRAmbiencePhase();
	gVRAmbience.bLastOneShot = -1;
	gVRAmbience.fLastCombat = IsVRAmbienceCombat();
	gVRAmbience.fLastRain = IsVRAmbienceRaining();

	StartVRSectorAmbienceLoopForPhase( gVRAmbience.ubCurrentPhase );
	ScheduleNextVRAmbienceOneShot( );
	return TRUE;
}

void UpdateVRSectorAmbience( )
{
	UINT8 ubPhase;
	BOOLEAN fCombat;
	BOOLEAN fRain;
	UINT32 uiNow;

	if ( !gVRAmbience.fActive )
		return;

	uiNow = GetTickCount();
	ubPhase = GetVRAmbiencePhase();
	fCombat = IsVRAmbienceCombat();
	fRain = IsVRAmbienceRaining();

	if ( ubPhase != gVRAmbience.ubCurrentPhase )
	{
		gVRAmbience.ubCurrentPhase = ubPhase;
		gVRAmbience.bLastOneShot = -1;
		StartVRSectorAmbienceLoopForPhase( ubPhase );
		ScheduleNextVRAmbienceOneShot( );
	}

	if ( fCombat != gVRAmbience.fLastCombat )
	{
		gVRAmbience.fLastCombat = fCombat;
		gVRAmbience.uiTargetLoopVolume = GetVRAmbienceTargetLoopVolume( gVRAmbience.ubCurrentPhase );

		// Do not let an overdue civilian/animal cue fire the instant combat ends.
		if ( !fCombat )
			ScheduleNextVRAmbienceOneShot( );
	}

	if ( fRain != gVRAmbience.fLastRain )
	{
		gVRAmbience.fLastRain = fRain;
		gVRAmbience.uiTargetLoopVolume = GetVRAmbienceTargetLoopVolume( gVRAmbience.ubCurrentPhase );
		ScheduleNextVRAmbienceOneShot( );
	}

	if ( gVRAmbience.uiLoopHandle != NO_SAMPLE )
	{
		gVRAmbience.uiTargetLoopVolume = GetVRAmbienceTargetLoopVolume( gVRAmbience.ubCurrentPhase );

		if ( !SoundIsPlaying( gVRAmbience.uiLoopHandle ) )
		{
			StartVRSectorAmbienceLoopForPhase( gVRAmbience.ubCurrentPhase );
		}
		else if ( gVRAmbience.uiCurrentLoopVolume != gVRAmbience.uiTargetLoopVolume &&
				  uiNow - gVRAmbience.uiLastFadeTick >= gVRAmbience.uiFadeStepMs )
		{
			UINT32 uiSteps = ( uiNow - gVRAmbience.uiLastFadeTick ) / gVRAmbience.uiFadeStepMs;
			if ( uiSteps < 1 )
				uiSteps = 1;

			if ( gVRAmbience.uiCurrentLoopVolume < gVRAmbience.uiTargetLoopVolume )
			{
				UINT32 uiDelta = gVRAmbience.uiTargetLoopVolume - gVRAmbience.uiCurrentLoopVolume;
				gVRAmbience.uiCurrentLoopVolume += ( uiSteps < uiDelta ? uiSteps : uiDelta );
			}
			else
			{
				UINT32 uiDelta = gVRAmbience.uiCurrentLoopVolume - gVRAmbience.uiTargetLoopVolume;
				gVRAmbience.uiCurrentLoopVolume -= ( uiSteps < uiDelta ? uiSteps : uiDelta );
			}

			SoundSetVolume( gVRAmbience.uiLoopHandle, gVRAmbience.uiCurrentLoopVolume );
			gVRAmbience.uiLastFadeTick = uiNow;
		}
	}

	if ( gVRAmbience.uiNextOneShotTime != 0 && uiNow >= gVRAmbience.uiNextOneShotTime )
	{
		if ( !fCombat || gVRAmbience.fOneShotsInCombat )
			PlayVRAmbienceOneShot( );

		ScheduleNextVRAmbienceOneShot( );
	}
}

UINT8					gubCurrentSteadyStateAmbience = SSA_NONE;
UINT8					gubCurrentSteadyStateSound	= 0;
UINT32					guiCurrentSteadyStateSoundHandle = NO_SAMPLE;
STEADY_STATE_AMBIENCE	gSteadyStateAmbientTable[ NUM_STEADY_STATE_AMBIENCES ] =
{
	// NONE
	"",
	"",
	"",
	"",
	// NIGHT
	"",
	"",
	"",
	"",
	// COUNTRYSIZE
	// DAY
	"SOUNDS\\SSA\\insects Day 01.wav",
	"",
	"",
	"",
	// NIGHT
	"SOUNDS\\SSA\\night_crickets_01D.wav",
	"SOUNDS\\SSA\\night_crickets_01B.wav",
	"SOUNDS\\SSA\\night_crickets_01C.wav",
	"SOUNDS\\SSA\\night_crickets_01A.wav",
	// NEAR WATER
	// DAY
	"SOUNDS\\SSA\\swamp_day_01a.wav",
	"SOUNDS\\SSA\\swamp_day_01b.wav",
	"SOUNDS\\SSA\\swamp_day_01c.wav",
	"SOUNDS\\SSA\\swamp_day_01d.wav",
	//NIGHT
	"SOUNDS\\SSA\\marsh_at_night_01a.wav",
	"SOUNDS\\SSA\\marsh_at_night_01b.wav",
	"SOUNDS\\SSA\\marsh_at_night_01c.wav",
	"SOUNDS\\SSA\\marsh_at_night_01d.wav",
	//INWATER
	//DAY
	"SOUNDS\\SSA\\middle_of_water_01d.wav",
	"SOUNDS\\SSA\\middle_of_water_01c.wav",
	"SOUNDS\\SSA\\middle_of_water_01b.wav",
	"SOUNDS\\SSA\\middle_of_water_01a.wav",
	// night
	"SOUNDS\\SSA\\middle_of_water_01d.wav",
	"SOUNDS\\SSA\\middle_of_water_01c.wav",
	"SOUNDS\\SSA\\middle_of_water_01b.wav",
	"SOUNDS\\SSA\\middle_of_water_01a.wav",
	// HEAVY FOREST
	// day
	"SOUNDS\\SSA\\JUNGLE_DAY_01a.wav",
	"SOUNDS\\SSA\\JUNGLE_DAY_01b.wav",
	"SOUNDS\\SSA\\JUNGLE_DAY_01c.wav",
	"SOUNDS\\SSA\\JUNGLE_DAY_01d.wav",
	// night
	"SOUNDS\\SSA\\night_crickets_03a.wav",
	"SOUNDS\\SSA\\night_crickets_03b.wav",
	"SOUNDS\\SSA\\night_crickets_03c.wav",
	"SOUNDS\\SSA\\night_crickets_03d.wav",
	// PINE FOREST
	// DAY
	"SOUNDS\\SSA\\pine_forest_01a.wav",
	"SOUNDS\\SSA\\pine_forest_01b.wav",
	"SOUNDS\\SSA\\pine_forest_01c.wav",
	"SOUNDS\\SSA\\pine_forest_01d.wav",
	// NIGHT
	"SOUNDS\\SSA\\night_crickets_02a.wav",
	"SOUNDS\\SSA\\night_crickets_02b.wav",
	"SOUNDS\\SSA\\night_crickets_02c.wav",
	"SOUNDS\\SSA\\night_crickets_02d.wav",
	// ABANDANDED
	// DAY
	"SOUNDS\\SSA\\metal_wind_01a.wav",
	"SOUNDS\\SSA\\metal_wind_01b.wav",
	"SOUNDS\\SSA\\metal_wind_01c.wav",
	"SOUNDS\\SSA\\metal_wind_01d.wav",
	// NIGHT
	"SOUNDS\\SSA\\night_insects_01a.wav",
	"SOUNDS\\SSA\\night_insects_01b.wav",
	"SOUNDS\\SSA\\night_insects_01c.wav",
	"SOUNDS\\SSA\\night_insects_01d.wav",
	// AIRPORT
	// DAY
	"SOUNDS\\SSA\\rotating radar dish.wav",
	"",
	"",
	"",
	// NIGHT
	"SOUNDS\\SSA\\rotating radar dish.wav",
	"",
	"",
	"",
	// WASTE LAND
	// DAY
	"SOUNDS\\SSA\\gentle_wind.wav",
	"",
	"",
	"",
	// NIGHT
	"SOUNDS\\SSA\\insects_at_night_04.wav",
	"",
	"",
	"",
	// UNDERGROUND
	// DAY
	"SOUNDS\\SSA\\low ominous ambience.wav",
	"",
	"",
	"",
	// NIGHT
	"SOUNDS\\SSA\\low ominous ambience.wav",
	"",
	"",
	"",
	// OCEAN
	// DAY
	"SOUNDS\\SSA\\sea_01a.wav",
	"SOUNDS\\SSA\\sea_01b.wav",
	"SOUNDS\\SSA\\sea_01c.wav",
	"SOUNDS\\SSA\\sea_01d.wav",
	// NIGHT
	"SOUNDS\\SSA\\ocean_waves_01a.wav",
	"SOUNDS\\SSA\\ocean_waves_01b.wav",
	"SOUNDS\\SSA\\ocean_waves_01c.wav",
	"SOUNDS\\SSA\\ocean_waves_01d.wav",
};


BOOLEAN LoadAmbientControlFile( UINT8 ubAmbientID )
{
	SGPFILENAME						zFilename;
	HWFILE								hFile;
	INT32								cnt;



	// BUILD FILENAME
	sprintf( zFilename, "AMBIENT\\%d.bad", ubAmbientID );

	// OPEN, LOAD
	hFile = FileOpen( zFilename, FILE_ACCESS_READ, FALSE);
	if ( !hFile )
	{
		return( FALSE );
	}

	// READ #
	if( !FileRead( hFile, &gsNumAmbData, sizeof( INT16 ), NULL ) )
	{
		return( FALSE );
	}

	// LOOP FOR OTHERS
	for ( cnt = 0; cnt < gsNumAmbData; cnt++ )
	{
		if( !FileRead( hFile, &(gAmbData[ cnt ]), sizeof( AMBIENTDATA_STRUCT ), NULL ) )
		{
			return( FALSE );
		}

		sprintf( zFilename, "AMBIENT\\%s", gAmbData[ cnt ].zFilename );
		strcpy( gAmbData[ cnt ].zFilename, zFilename );
	}

	FileClose( hFile );

	return( TRUE );
}

void GetAmbientDataPtr( AMBIENTDATA_STRUCT **ppAmbData, UINT16 *pusNumData )
{
	*ppAmbData		= gAmbData;
	*pusNumData		= gsNumAmbData;
}


void StopAmbients( )
{
	SoundStopAllRandom( );
	StopVRSectorAmbienceLoop( );
	gfVRSectorAmbienceProfileActive = FALSE;
}

void HandleNewSectorAmbience( UINT8 ubAmbientID )
{
	// A newly loaded sector owns a fresh ambience state.
	SoundStopAllRandom( );
	StopVRSectorAmbienceLoop( );
	DeleteAllStrategicEventsOfType( EVENT_AMBIENT );

	// The Vengeance profile layer works both above and below ground.  Missing or
	// disabled configuration falls through to the original JA2 tileset system.
	if ( LoadVRSectorAmbienceProfile( ) )
		return;

	if( !gfBasement && !gfCaves )
	{
		if( LoadAmbientControlFile( ubAmbientID ) )
		{
			BuildDayAmbientSounds( );
		}
		else
		{
			DebugMsg(TOPIC_JA2, DBG_LEVEL_0, String("Cannot load Ambient data for tileset" ) );
		}
	}
}

void DeleteAllAmbients()
{
	SoundStopAllRandom();
	StopVRSectorAmbienceLoop( );
	gfVRSectorAmbienceProfileActive = FALSE;
	DeleteAllStrategicEventsOfType( EVENT_AMBIENT );
}

UINT32 SetupNewAmbientSound( UINT32 uiAmbientID )
{
	RANDOMPARMS rpParms;

	//SoundLog((CHAR8 *)String("	SetupNewAmbientSound()1:	uiAmbientID: '%d'", uiAmbientID ) );

	memset(&rpParms, 0xff, sizeof(RANDOMPARMS));

	rpParms.uiTimeMin		=	gAmbData[ uiAmbientID ].uiMinTime;
	rpParms.uiTimeMax		=	gAmbData[ uiAmbientID ].uiMaxTime;
	rpParms.uiVolMin		= CalculateSoundEffectsVolume( gAmbData[ uiAmbientID ].uiVol );
	rpParms.uiVolMax		= CalculateSoundEffectsVolume( gAmbData[ uiAmbientID ].uiVol );
	rpParms.uiPriority	=	GROUP_AMBIENT;

	//SoundLog((CHAR8 *)String("	SetupNewAmbientSound()2:	gAmbData[ uiAmbientID ].zFilename: '%s',	Params: '%s'", gAmbData[ uiAmbientID ].zFilename, &rpParms ) );

	return SoundPlayRandom( gAmbData[ uiAmbientID ].zFilename, &rpParms );	//bug Nr14
}


UINT32 StartSteadyStateAmbient( UINT32 ubVolume, UINT32 ubLoops)
{
SOUNDPARMS spParms;

	memset(&spParms, 0xff, sizeof(SOUNDPARMS));

	spParms.uiVolume = CalculateSoundEffectsVolume( ubVolume );
	spParms.uiLoop = ubLoops;
	spParms.uiPriority=GROUP_AMBIENT;

	return(SoundPlay( gSteadyStateAmbientTable[ gubCurrentSteadyStateAmbience ].zSoundNames[ gubCurrentSteadyStateSound ], &spParms ) );
}



BOOLEAN SetSteadyStateAmbience( UINT8 ubAmbience )
{
	BOOLEAN fInNight = FALSE;
	INT32	 cnt;
	UINT8	 ubNumSounds = 0;
	UINT8	 ubChosenSound;

	// Stop all ambients...
	if ( guiCurrentSteadyStateSoundHandle != NO_SAMPLE )
	{
		SoundStop( guiCurrentSteadyStateSoundHandle );
		guiCurrentSteadyStateSoundHandle = NO_SAMPLE;
	}

	// Determine what time of day we are in ( day/night)
	if( gubEnvLightValue >= LIGHT_DUSK_CUTOFF)
	{
	fInNight = TRUE;
	}

	// loop through listing to get num sounds...
	for ( cnt = ( fInNight * 4 ); cnt < ( NUM_SOUNDS_PER_TIMEFRAME / 2 ); cnt++ )
	{
	if ( gSteadyStateAmbientTable[ ubAmbience ].zSoundNames[ cnt ][ 0 ] == 0 )
	{
		break;
	}

	ubNumSounds++;
	}

	if ( ubNumSounds == 0 )
	{
	return( FALSE );
	}

	// Pick one
	ubChosenSound = (UINT8) Random( ubNumSounds );

	// Set!
	gubCurrentSteadyStateAmbience = ubAmbience;
	gubCurrentSteadyStateSound	= ubChosenSound;

	guiCurrentSteadyStateSoundHandle =	StartSteadyStateAmbient( LOWVOLUME, 0 );

	return( TRUE );
}
