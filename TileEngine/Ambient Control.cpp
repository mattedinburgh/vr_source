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

AMBIENTDATA_STRUCT		gAmbData[ MAX_AMBIENT_SOUNDS ];
INT16									gsNumAmbData = 0;

/*
 * Vengeance sector-aware ambience
 * --------------------------------
 * Legacy JA2 attaches random ambience to a tileset AmbientID.  Vengeance
 * currently uses the same AmbientID for its custom tilesets, so a tileset-only
 * system cannot distinguish San Mona from a farm, airport, oil rig, mine, etc.
 *
 * SectorAmbience.ini supplies a data-driven profile selected by exact sector
 * override first, then by tileset, with an underground default.  Each profile
 * can provide a low continuous day/night bed plus sparse one-shots.  If the
 * file is absent or disabled, the original .bad ambience path remains intact.
 */
static UINT32 guiVRSectorAmbienceLoopHandle = NO_SAMPLE;
static BOOLEAN gfVRSectorAmbienceProfileActive = FALSE;
static BOOLEAN gfVRSectorAmbienceLoopNight = FALSE;
static CHAR8 gzVRSectorAmbienceProfile[ 64 ] = "";

static void StopVRSectorAmbienceLoop( )
{
	if ( guiVRSectorAmbienceLoopHandle != NO_SAMPLE )
	{
		SoundStop( guiVRSectorAmbienceLoopHandle );
		guiVRSectorAmbienceLoopHandle = NO_SAMPLE;
	}
}

static BOOLEAN StartVRSectorAmbienceLoop( CIniReader &ini, STR8 szSection, BOOLEAN fNight )
{
	CHAR8 szLoop[ 260 ];
	CHAR8 szLoopKey[ 16 ];
	SOUNDPARMS spParms;
	UINT32 uiVolume;

	strcpy( szLoopKey, fNight ? "NIGHT_LOOP" : "DAY_LOOP" );
	ini.ReadString( szSection, szLoopKey, "", szLoop, sizeof( szLoop ) );
	if ( szLoop[ 0 ] == 0 )
		return FALSE;

	uiVolume = (UINT32)ini.ReadInteger( szSection, "LOOP_VOLUME", 20, 0, 127 );

	memset( &spParms, 0xff, sizeof( SOUNDPARMS ) );
	spParms.uiVolume = CalculateSoundEffectsVolume( uiVolume );
	spParms.uiLoop = 0;
	spParms.uiPriority = GROUP_AMBIENT;

	StopVRSectorAmbienceLoop( );
	guiVRSectorAmbienceLoopHandle = SoundPlay( szLoop, &spParms );
	gfVRSectorAmbienceLoopNight = fNight;

	return ( guiVRSectorAmbienceLoopHandle != NO_SAMPLE );
}

static void RefreshVRSectorAmbienceLoopForTimeOfDay( )
{
	CIniReader ini( "SectorAmbience.ini" );
	CHAR8 szSection[ 96 ];
	BOOLEAN fNight;

	if ( !gfVRSectorAmbienceProfileActive || !ini.Is_CIniReader_File_Found() || gzVRSectorAmbienceProfile[ 0 ] == 0 )
		return;

	fNight = ( gubEnvLightValue >= LIGHT_DUSK_CUTOFF );
	if ( guiVRSectorAmbienceLoopHandle != NO_SAMPLE && fNight == gfVRSectorAmbienceLoopNight )
		return;

	sprintf( szSection, "PROFILE_%s", gzVRSectorAmbienceProfile );
	StartVRSectorAmbienceLoop( ini, szSection, fNight );
}

static void AddVRSectorAmbientEntries( CIniReader &ini, STR8 szSection, const CHAR8 *szPrefix, UINT8 ubTimeCategory )
{
	CHAR8 szKey[ 32 ];
	CHAR8 szSound[ 260 ];
	CHAR8 szMinKey[ 32 ];
	CHAR8 szMaxKey[ 32 ];
	CHAR8 szVolumeKey[ 32 ];
	UINT32 uiMinTime;
	UINT32 uiMaxTime;
	UINT32 uiVolume;
	INT32 i;

	sprintf( szMinKey, "%s_MIN_MS", szPrefix );
	sprintf( szMaxKey, "%s_MAX_MS", szPrefix );
	sprintf( szVolumeKey, "%s_VOLUME", szPrefix );

	uiMinTime = (UINT32)ini.ReadInteger( szSection, szMinKey, 15000, 250, 600000 );
	uiMaxTime = (UINT32)ini.ReadInteger( szSection, szMaxKey, 45000, 250, 600000 );
	uiVolume = (UINT32)ini.ReadInteger( szSection, szVolumeKey, 25, 0, 127 );

	if ( uiMaxTime < uiMinTime )
	{
		UINT32 uiSwap = uiMinTime;
		uiMinTime = uiMaxTime;
		uiMaxTime = uiSwap;
	}

	for ( i = 1; i <= 8 && gsNumAmbData < MAX_AMBIENT_SOUNDS; ++i )
	{
		sprintf( szKey, "%s_SOUND_%d", szPrefix, i );
		ini.ReadString( szSection, szKey, "", szSound, sizeof( szSound ) );
		if ( szSound[ 0 ] == 0 )
			continue;

		AMBIENTDATA_STRUCT *pData = &gAmbData[ gsNumAmbData++ ];
		memset( pData, 0, sizeof( AMBIENTDATA_STRUCT ) );
		pData->uiMinTime = uiMinTime;
		pData->uiMaxTime = uiMaxTime;
		pData->ubTimeCatagory = ubTimeCategory;
		pData->uiVol = uiVolume;
		strncpy( pData->zFilename, szSound, sizeof( pData->zFilename ) - 1 );
		pData->zFilename[ sizeof( pData->zFilename ) - 1 ] = 0;
	}
}

static BOOLEAN LoadVRSectorAmbienceProfile( )
{
	CIniReader ini( "SectorAmbience.ini" );
	CHAR8 szSectorKey[ 32 ];
	CHAR8 szTilesetKey[ 16 ];
	CHAR8 szProfile[ 64 ];
	CHAR8 szSection[ 96 ];
	BOOLEAN fNight;
	BOOLEAN fLoopStarted;

	gfVRSectorAmbienceProfileActive = FALSE;
	gzVRSectorAmbienceProfile[ 0 ] = 0;
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

	AddVRSectorAmbientEntries( ini, szSection, "DAY", AMB_TOD_DAY );
	AddVRSectorAmbientEntries( ini, szSection, "NIGHT", AMB_TOD_NIGHT );

	fNight = ( gubEnvLightValue >= LIGHT_DUSK_CUTOFF );
	fLoopStarted = StartVRSectorAmbienceLoop( ini, szSection, fNight );

	if ( gsNumAmbData > 0 )
		BuildDayAmbientSounds( );

	if ( !fLoopStarted && gsNumAmbData == 0 )
		return FALSE;

	strncpy( gzVRSectorAmbienceProfile, szProfile, sizeof( gzVRSectorAmbienceProfile ) - 1 );
	gzVRSectorAmbienceProfile[ sizeof( gzVRSectorAmbienceProfile ) - 1 ] = 0;
	gfVRSectorAmbienceProfileActive = TRUE;
	return TRUE;
}


UINT8 ubSector )
{
	switch( ubSector )
	{
		case SEC_A11:
		case SEC_B5: case SEC_B6: case SEC_B8: case SEC_B12:
		case SEC_D6:
		case SEC_E7: case SEC_E8: case SEC_E9:
		case SEC_G14:
		case SEC_I7:
			return TRUE;
	}
	return FALSE;
}

static BOOLEAN VRAmbienceNightlifeSector( UINT8 ubSector )
{
	switch( ubSector )
	{
		// San Mona, resort, race track, mall district
		case SEC_C5: case SEC_C6: case SEC_D5:
		case SEC_I9:
		case SEC_M12:
		case SEC_P12:
			return TRUE;
	}
	return FALSE;
}

static BOOLEAN VRAmbienceUrbanSector( UINT8 ubSector )
{
	switch( ubSector )
	{
		// Oronegro / Omerta / Drassen
		case SEC_A2: case SEC_A3: case SEC_A9: case SEC_A10:
		case SEC_B2: case SEC_B10: case SEC_B14:
		case SEC_C1: case SEC_C13:

		// Salinas
		case SEC_F7: case SEC_F8: case SEC_F9:
		case SEC_G7: case SEC_G8: case SEC_G9:
		case SEC_H8: case SEC_H9:

		// Doran / Alma / Estoni
		case SEC_G1: case SEC_G2: case SEC_G3:
		case SEC_H1: case SEC_H2: case SEC_H3: case SEC_H4:
		case SEC_H13: case SEC_H14:
		case SEC_I6: case SEC_I13: case SEC_I15:
		case SEC_J3:

		// Malino / Burton / Palaccio / Kingpin
		case SEC_K11: case SEC_K12:
		case SEC_L10: case SEC_L11: case SEC_L12:
		case SEC_M3: case SEC_M4: case SEC_M5: case SEC_M11:
		case SEC_N5: case SEC_N6:
		case SEC_O3: case SEC_O4: case SEC_O5: case SEC_O9: case SEC_O12:
		case SEC_P3:
			return TRUE;
	}
	return FALSE;
}

static BOOLEAN VRAmbienceIndustrialSector( UINT8 ubSector )
{
	switch( ubSector )
	{
		// Oil rigs, mines, utilities and industrial/logistics sites
		case SEC_B1:
		case SEC_D4: case SEC_D13: case SEC_D14:
		case SEC_F10:
		case SEC_G15:
		case SEC_I3: case SEC_I14:
		case SEC_J7:
		case SEC_L2:
		case SEC_M10:
		case SEC_O14:

		// Airports/heliport also get a restrained mechanical layer
		case SEC_B13:
		case SEC_N3: case SEC_N16:
		case SEC_O2:
			return TRUE;
	}
	return FALSE;
}

static BOOLEAN VRAmbienceAviationSector( UINT8 ubSector )
{
	switch( ubSector )
	{
		case SEC_A8:
		case SEC_B13:
		case SEC_D2: case SEC_D15:
		case SEC_I8:
		case SEC_K4:
		case SEC_M13:
		case SEC_N3: case SEC_N4: case SEC_N16:
		case SEC_O2:
			return TRUE;
	}
	return FALSE;
}

static BOOLEAN VRAmbienceCoastSector( UINT8 ubSector )
{
	switch( ubSector )
	{
		case SEC_F2:
		case SEC_J2:
		case SEC_L1: case SEC_L13:
		case SEC_M2:
		case SEC_N7: case SEC_N8:
		case SEC_O11: case SEC_O14:
			return TRUE;
	}
	return FALSE;
}

static BOOLEAN VRAmbienceNatureSector( UINT8 ubSector )
{
	switch( ubSector )
	{
		// Tropical
		case SEC_A1:
		case SEC_C2: case SEC_C3:
		case SEC_E2:
		case SEC_N9: case SEC_N10:
		case SEC_O8: case SEC_O13:
		case SEC_P11:

		// Hills / plains / forests / woods / swamps / rural roads
		case SEC_A6: case SEC_A7: case SEC_A12: case SEC_A13: case SEC_A14: case SEC_A15:
		case SEC_B3: case SEC_B4: case SEC_B7: case SEC_B11: case SEC_B15: case SEC_B16:
		case SEC_C4: case SEC_C7: case SEC_C8: case SEC_C9: case SEC_C10: case SEC_C12: case SEC_C15: case SEC_C16:
		case SEC_D3: case SEC_D7: case SEC_D8: case SEC_D10: case SEC_D11: case SEC_D12: case SEC_D16:
		case SEC_E3: case SEC_E4: case SEC_E5: case SEC_E6: case SEC_E10: case SEC_E11: case SEC_E12: case SEC_E14: case SEC_E15:
		case SEC_F3: case SEC_F4: case SEC_F5: case SEC_F6: case SEC_F11: case SEC_F12: case SEC_F13: case SEC_F14:
		case SEC_G4: case SEC_G5: case SEC_G6: case SEC_G12: case SEC_G13: case SEC_G16:
		case SEC_H5: case SEC_H6: case SEC_H7: case SEC_H12: case SEC_H15: case SEC_H16:
		case SEC_I4: case SEC_I5: case SEC_I11: case SEC_I12: case SEC_I16:
		case SEC_J4: case SEC_J5: case SEC_J6: case SEC_J12: case SEC_J13: case SEC_J15:
		case SEC_K3: case SEC_K5: case SEC_K6: case SEC_K13: case SEC_K14: case SEC_K15:
		case SEC_L3: case SEC_L4: case SEC_L5: case SEC_L6: case SEC_L7: case SEC_L9: case SEC_L14: case SEC_L15:
		case SEC_M6: case SEC_M7: case SEC_M8: case SEC_M9: case SEC_M14:
		case SEC_N15:
		case SEC_O15:
			return TRUE;
	}
	return FALSE;
}

static void AddVengeanceAmbient( const CHAR8 *szFilename, UINT32 uiMinTime, UINT32 uiMaxTime, UINT8 ubTimeCategory, UINT32 uiVolume )
{
	if( gsNumAmbData >= MAX_AMBIENT_SOUNDS || !szFilename || !szFilename[0] )
		return;

	AMBIENTDATA_STRUCT *pData = &gAmbData[ gsNumAmbData++ ];
	memset( pData, 0, sizeof( AMBIENTDATA_STRUCT ) );

	pData->uiMinTime = uiMinTime;
	pData->uiMaxTime = ( uiMaxTime > uiMinTime ) ? uiMaxTime : uiMinTime + 1;
	pData->ubTimeCatagory = ubTimeCategory;
	pData->uiVol = uiVolume;

	strncpy( pData->zFilename, szFilename, sizeof( pData->zFilename ) - 1 );
	pData->zFilename[ sizeof( pData->zFilename ) - 1 ] = 0;
}

static UINT32 GetVengeanceSectorAmbienceFlags( )
{
	if( gWorldSectorX < 1 || gWorldSectorX > 16 || gWorldSectorY < 1 || gWorldSectorY > 16 )
		return VR_AMBIENCE_NONE;

	const UINT8 ubSector = SECTOR( gWorldSectorX, gWorldSectorY );
	UINT32 uiFlags = VR_AMBIENCE_NONE;

	if( VRAmbienceFarmSector( ubSector ) )
		uiFlags |= VR_AMBIENCE_FARM | VR_AMBIENCE_NATURE;

	if( VRAmbienceNightlifeSector( ubSector ) )
		uiFlags |= VR_AMBIENCE_URBAN | VR_AMBIENCE_NIGHTLIFE;
	else if( VRAmbienceUrbanSector( ubSector ) )
		uiFlags |= VR_AMBIENCE_URBAN;

	if( VRAmbienceIndustrialSector( ubSector ) )
		uiFlags |= VR_AMBIENCE_INDUSTRIAL;

	if( VRAmbienceAviationSector( ubSector ) )
		uiFlags |= VR_AMBIENCE_AVIATION;

	if( VRAmbienceCoastSector( ubSector ) )
		uiFlags |= VR_AMBIENCE_COAST;

	if( uiFlags == VR_AMBIENCE_NONE && VRAmbienceNatureSector( ubSector ) )
		uiFlags |= VR_AMBIENCE_NATURE;

	return uiFlags;
}

static void AppendVengeanceSectorAmbience( )
{
	const UINT32 uiFlags = GetVengeanceSectorAmbienceFlags();

	// Existing Vengeance wildlife.  Farms use a busier bird/cow pattern;
	// wilderness stays much sparser so it does not become a soundboard.
	if( uiFlags & VR_AMBIENCE_FARM )
	{
		AddVengeanceAmbient( "AMBIENT\\BIRD4.wav",             35000,  85000, AMB_TOD_DAY,  16 );
		AddVengeanceAmbient( "SOUNDS\\COWMOO3.wav",           120000, 300000, AMB_TOD_DAY,  15 );
		AddVengeanceAmbient( "AMBIENT\\VR_FARM_BELL.wav",     150000, 360000, AMB_TOD_DAY,  13 );
	}
	else if( uiFlags & VR_AMBIENCE_NATURE )
	{
		AddVengeanceAmbient( "AMBIENT\\BIRD6.wav",             55000, 140000, AMB_TOD_DAY,  15 );
	}

	if( uiFlags & VR_AMBIENCE_COAST )
	{
		AddVengeanceAmbient( "AMBIENT\\BIRD9.wav",             80000, 190000, AMB_TOD_DAY,  13 );
	}

	if( uiFlags & VR_AMBIENCE_URBAN )
	{
		AddVengeanceAmbient( "AMBIENT\\VR_CITY_DAY.wav",       45000, 105000, AMB_TOD_DAY,   11 );
		AddVengeanceAmbient( "AMBIENT\\VR_CITY_HORN.wav",     120000, 330000, AMB_TOD_DAY,   10 );
		AddVengeanceAmbient( "AMBIENT\\VR_CITY_NIGHT.wav",     90000, 210000, AMB_TOD_NIGHT,  8 );

		if( !( uiFlags & VR_AMBIENCE_NIGHTLIFE ) )
			AddVengeanceAmbient( "AMBIENT\\VR_CITY_JINGLE.wav", 210000, 480000, AMB_TOD_DAY, 9 );
	}

	if( uiFlags & VR_AMBIENCE_NIGHTLIFE )
	{
		// A little more identity around San Mona / resort / mall / race track,
		// but still infrequent enough to leave dialogue and combat legible.
		AddVengeanceAmbient( "AMBIENT\\VR_CITY_JINGLE.wav",    100000, 240000, AMB_TOD_DUSK, 10 );
	}

	if( uiFlags & VR_AMBIENCE_INDUSTRIAL )
	{
		AddVengeanceAmbient( "AMBIENT\\VR_INDUSTRIAL_CLANK.wav", 65000, 160000, AMB_TOD_DAY, 11 );
	}

	if( uiFlags & VR_AMBIENCE_AVIATION )
	{
		AddVengeanceAmbient( "SOUNDS\\HELI1.WAV",              300000, 720000, AMB_TOD_DAY,   8 );
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

	// Existing TOD ambient events also keep the continuous sector bed in sync.
	RefreshVRSectorAmbienceLoopForTimeOfDay( );

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
