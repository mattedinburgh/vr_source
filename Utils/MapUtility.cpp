#ifdef PRECOMPILEDHEADERS
#include "Utils All.h"
#else
#include "sgp.h"
#endif
#include <stdio.h>

#ifdef JA2EDITOR
#include "Screens.h"
#include "Maputility.h"
#include "worlddef.h"
#include "worldman.h"
#include "overhead.h"
#include "fileman.h"
#include "loadscreen.h"
#include "overhead map.h"
#include "radar screen.h"
#include "vobject_blitters.h"
#include "sticonvert.h"
#include "font control.h"
#include "worlddat.h"
#include "english.h"
#include "map information.h"
#include "line.h"
#include "quantize wrap.h"
//dnl ch77 131113
#include "Editor Taskbar Utils.h"
#include "Text Input.h"
#include "Cursor Control.h"//dnl ch78 271113
#include "lighting.h"//dnl ch79 301113
#include "renderworld.h"
#include "Isometric Utils.h"

#define MINIMAP_X_SIZE	88//RADAR_WINDOW_WIDTH
#define MINIMAP_Y_SIZE	44//RADAR_WINDOW_HEIGHT
#define WINDOW_SIZE		2

extern BOOLEAN fEditModeFirstTime;
extern BOOLEAN gfOverheadMapDirty;
extern UINT16 iOffsetHorizontal;
extern UINT16 iOffsetVertical;
extern FDLG_LIST* FileList;

typedef struct
{
	INT8 r;
	INT8 g;
	INT8 b;
}RGBValues;

FLOAT gdXStep, gdYStep;
UINT32 guiMiniMap=0, gui8BitMiniMap=0, guiBigMap=0;//dnl ch77 121113
HVSURFACE ghVSurface = NULL;
RGBValues* p24BitValues = NULL;
FDLG_LIST* FListNode = NULL;
BOOLEAN gfMapUtilityWindowActive = FALSE;

static void MapPreviewPutLE16( UINT8 *p, UINT16 v )
{
	p[0] = (UINT8)(v & 0xff);
	p[1] = (UINT8)((v >> 8) & 0xff);
}

static void MapPreviewPutLE32( UINT8 *p, UINT32 v )
{
	p[0] = (UINT8)(v & 0xff);
	p[1] = (UINT8)((v >> 8) & 0xff);
	p[2] = (UINT8)((v >> 16) & 0xff);
	p[3] = (UINT8)((v >> 24) & 0xff);
}

static BOOLEAN GetMapPreviewDirectory( CHAR8 *pOutDir, UINT32 uiOutSize )
{
	if ( pOutDir == NULL || uiOutSize < 32 )
		return FALSE;

	CHAR8 zExePath[MAX_PATH];
	DWORD dwLen = GetModuleFileNameA( NULL, zExePath, MAX_PATH );
	if ( dwLen == 0 || dwLen >= MAX_PATH )
		return FALSE;

	CHAR8 *pSlash = strrchr( zExePath, '\\' );
	if ( pSlash == NULL )
		return FALSE;
	*pSlash = 0;

	_snprintf( pOutDir, uiOutSize - 1, "%s\\MAP_PREVIEWS", zExePath );
	pOutDir[uiOutSize - 1] = 0;
	CreateDirectoryA( pOutDir, NULL );
	return TRUE;
}

static void MapPreviewWriteStatus( const STR8 pText )
{
	CHAR8 zPreviewDir[MAX_PATH + 32];
	if ( !GetMapPreviewDirectory( zPreviewDir, sizeof(zPreviewDir) ) )
		return;

	CHAR8 zStatus[MAX_PATH + 64];
	_snprintf( zStatus, sizeof(zStatus) - 1, "%s\\mapshot_status.txt", zPreviewDir );
	zStatus[sizeof(zStatus) - 1] = 0;

	FILE *fp = fopen( zStatus, "a" );
	if ( fp )
	{
		fprintf( fp, "%s\n", pText ? pText : "" );
		fclose( fp );
	}
}

// Export the engine's full overhead render before it is crushed down to the
// 88x44 radar image. BMP is deliberately used here: no additional image codec
// or dependency is needed in the legacy VS2013 editor build.
static BOOLEAN SaveEngineMapPreviewBMP( const STR8 pMapFilename, UINT32 uiSurface,
	UINT16 usWidth, UINT16 usHeight )
{
	if ( pMapFilename == NULL || usWidth == 0 || usHeight == 0 )
	{
		MapPreviewWriteStatus( "SAVE_FAIL invalid arguments" );
		return FALSE;
	}

	// Keep only the file name. GetFileFirst() implementations are allowed to
	// return either a leaf name or a relative path.
	const CHAR8 *pLeaf = pMapFilename;
	const CHAR8 *pBackslash = strrchr( pMapFilename, '\\' );
	const CHAR8 *pSlash = strrchr( pMapFilename, '/' );
	if ( pBackslash && pBackslash + 1 > pLeaf )
		pLeaf = pBackslash + 1;
	if ( pSlash && pSlash + 1 > pLeaf )
		pLeaf = pSlash + 1;

	CHAR8 zBase[260];
	strncpy( zBase, pLeaf, sizeof(zBase) - 1 );
	zBase[ sizeof(zBase) - 1 ] = 0;
	CHAR8 *pDot = strrchr( zBase, '.' );
	if ( pDot )
		*pDot = 0;

	CHAR8 zPreviewDir[MAX_PATH + 32];
	if ( !GetMapPreviewDirectory( zPreviewDir, sizeof(zPreviewDir) ) )
	{
		MapPreviewWriteStatus( "SAVE_FAIL cannot resolve executable preview directory" );
		return FALSE;
	}

	CHAR8 zOutput[MAX_PATH + 320];
	_snprintf( zOutput, sizeof(zOutput) - 1, "%s\\%s_overview.bmp", zPreviewDir, zBase );
	zOutput[sizeof(zOutput) - 1] = 0;

	UINT32 uiPitchBytes = 0;
	UINT16 *pSrc = (UINT16*)LockVideoSurface( uiSurface, &uiPitchBytes );
	if ( pSrc == NULL )
	{
		MapPreviewWriteStatus( "SAVE_FAIL LockVideoSurface returned NULL" );
		return FALSE;
	}

	const UINT32 uiRowBytes = ( (UINT32)usWidth * 3u + 3u ) & ~3u;
	const UINT32 uiImageBytes = uiRowBytes * (UINT32)usHeight;
	const UINT32 uiFileBytes = 54u + uiImageBytes;

	FILE *fp = fopen( zOutput, "wb" );
	if ( fp == NULL )
	{
		UnLockVideoSurface( uiSurface );
		MapPreviewWriteStatus( "SAVE_FAIL fopen output BMP" );
		return FALSE;
	}

	UINT8 header[54];
	memset( header, 0, sizeof(header) );
	header[0] = 'B';
	header[1] = 'M';
	MapPreviewPutLE32( &header[2], uiFileBytes );
	MapPreviewPutLE32( &header[10], 54 );
	MapPreviewPutLE32( &header[14], 40 );
	MapPreviewPutLE32( &header[18], (UINT32)usWidth );
	MapPreviewPutLE32( &header[22], (UINT32)usHeight );
	MapPreviewPutLE16( &header[26], 1 );
	MapPreviewPutLE16( &header[28], 24 );
	MapPreviewPutLE32( &header[34], uiImageBytes );
	MapPreviewPutLE32( &header[38], 2835 );
	MapPreviewPutLE32( &header[42], 2835 );
	if ( fwrite( header, 1, sizeof(header), fp ) != sizeof(header) )
	{
		fclose( fp );
		UnLockVideoSurface( uiSurface );
		MapPreviewWriteStatus( "SAVE_FAIL BMP header write" );
		return FALSE;
	}

	UINT8 *pRow = (UINT8*)MemAlloc( uiRowBytes );
	if ( pRow == NULL )
	{
		fclose( fp );
		UnLockVideoSurface( uiSurface );
		MapPreviewWriteStatus( "SAVE_FAIL row allocation" );
		return FALSE;
	}

	BOOLEAN fWriteOK = TRUE;
	for ( INT32 y = (INT32)usHeight - 1; y >= 0; --y )
	{
		memset( pRow, 0, uiRowBytes );
		for ( UINT16 x = 0; x < usWidth; ++x )
		{
			const UINT16 usPixel = pSrc[ y * (uiPitchBytes / 2) + x ];
			const UINT32 uiRGB = GetRGBColor( usPixel );
			pRow[x * 3 + 0] = (UINT8)SGPGetBValue( uiRGB );
			pRow[x * 3 + 1] = (UINT8)SGPGetGValue( uiRGB );
			pRow[x * 3 + 2] = (UINT8)SGPGetRValue( uiRGB );
		}
		if ( fwrite( pRow, 1, uiRowBytes, fp ) != uiRowBytes )
		{
			fWriteOK = FALSE;
			break;
		}
	}

	MemFree( pRow );
	fclose( fp );
	UnLockVideoSurface( uiSurface );

	MapPreviewWriteStatus( fWriteOK ? "SAVE_OK engine overview BMP written" : "SAVE_FAIL BMP pixel write" );
	return fWriteOK;
}



static void MapFactoryBaseName( const STR8 pMapFilename, CHAR8 *pOut, UINT32 uiOutSize )
{
	if ( pOut == NULL || uiOutSize == 0 )
		return;
	pOut[0] = 0;
	if ( pMapFilename == NULL )
		return;

	const CHAR8 *pLeaf = pMapFilename;
	const CHAR8 *pBackslash = strrchr( pMapFilename, '\\' );
	const CHAR8 *pSlash = strrchr( pMapFilename, '/' );
	if ( pBackslash != NULL && pBackslash + 1 > pLeaf ) pLeaf = pBackslash + 1;
	if ( pSlash != NULL && pSlash + 1 > pLeaf ) pLeaf = pSlash + 1;

	strncpy( pOut, pLeaf, uiOutSize - 1 );
	pOut[uiOutSize - 1] = 0;
	CHAR8 *pDot = strrchr( pOut, '.' );
	if ( pDot != NULL ) *pDot = 0;
}

static BOOLEAN MapFactoryHasSuffixNoCase( const STR8 pText, const STR8 pSuffix )
{
	if ( pText == NULL || pSuffix == NULL )
		return FALSE;
	const size_t nText = strlen( pText );
	const size_t nSuffix = strlen( pSuffix );
	if ( nSuffix > nText )
		return FALSE;
	return _stricmp( pText + nText - nSuffix, pSuffix ) == 0;
}

static BOOLEAN MapFactoryTileTypeFromName( const STR8 pName, UINT32 *pType )
{
	if ( pName == NULL || pType == NULL )
		return FALSE;

	struct TYPE_NAME { const CHAR8 *pName; UINT32 uiType; };
	static const TYPE_NAME gTypes[] =
	{
		{ "FIRSTDECORATIONS", FIRSTDECORATIONS },
		{ "SECONDDECORATIONS", SECONDDECORATIONS },
		{ "THIRDDECORATIONS", THIRDDECORATIONS },
		{ "FOURTHDECORATIONS", FOURTHDECORATIONS },
		{ "DEBRISWEEDS", DEBRISWEEDS },
		{ "DEBRISROCKS", DEBRISROCKS },
		{ "DEBRISWOOD", DEBRISWOOD },
		{ "DEBRISMISC", DEBRISMISC },
		{ "DEBRISGRASS", DEBRISGRASS },
		{ "DEBRISSAND", DEBRISSAND }
	};
	for ( UINT32 i = 0; i < sizeof(gTypes) / sizeof(gTypes[0]); ++i )
	{
		if ( _stricmp( pName, gTypes[i].pName ) == 0 )
		{
			*pType = gTypes[i].uiType;
			return TRUE;
		}
	}
	return FALSE;
}

static BOOLEAN MapFactoryVisualGridSafe( INT32 sGridNo )
{
	if ( sGridNo < 0 || sGridNo >= WORLD_MAX || gpWorldLevelData == NULL )
		return FALSE;

	MAP_ELEMENT *pMap = &gpWorldLevelData[sGridNo];
	if ( pMap->pLandHead == NULL || pMap->pObjectHead != NULL ||
		 pMap->pStructHead != NULL || pMap->pRoofHead != NULL || pMap->pOnRoofHead != NULL )
		return FALSE;

	UINT32 uiLandType = 0;
	if ( !GetTileType( pMap->pLandHead->usIndex, &uiLandType ) )
		return FALSE;
	if ( uiLandType == REGWATERTEXTURE || uiLandType == DEEPWATERTEXTURE )
		return FALSE;
	return TRUE;
}

static BOOLEAN MapFactoryAddVisual( INT32 sGridNo, UINT32 uiType, UINT16 usSubIndex, BOOLEAN fOnRoof )
{
	if ( sGridNo < 0 || sGridNo >= WORLD_MAX || uiType >= NUMBEROFTILETYPES || usSubIndex == 0 )
		return FALSE;

	if ( fOnRoof )
	{
		if ( gpWorldLevelData == NULL || gpWorldLevelData[sGridNo].pRoofHead == NULL ||
			 gpWorldLevelData[sGridNo].pOnRoofHead != NULL )
			return FALSE;
	}
	else if ( !MapFactoryVisualGridSafe( sGridNo ) )
		return FALSE;

	UINT16 usTileIndex = NO_TILE;
	if ( !GetTileIndexFromTypeSubIndex( uiType, usSubIndex, &usTileIndex ) ||
		 usTileIndex == NO_TILE || usTileIndex >= giNumberOfTiles )
		return FALSE;

	LEVELNODE *pNode = fOnRoof
		? AddOnRoofToTail( sGridNo, usTileIndex )
		: AddObjectToTail( sGridNo, usTileIndex );
	if ( pNode == NULL )
		return FALSE;

	pNode->ubShadeLevel = LightGetAmbient();
	pNode->ubNaturalShadeLevel = pNode->ubShadeLevel;
	return TRUE;
}

static BOOLEAN MapFactoryRecipePath( const STR8 pBase, CHAR8 *pOut, UINT32 uiOutSize )
{
	if ( pBase == NULL || pOut == NULL || uiOutSize < 32 )
		return FALSE;

	CHAR8 zExePath[MAX_PATH];
	DWORD dwLen = GetModuleFileNameA( NULL, zExePath, MAX_PATH );
	if ( dwLen == 0 || dwLen >= MAX_PATH )
		return FALSE;
	CHAR8 *pSlash = strrchr( zExePath, '\\' );
	if ( pSlash == NULL )
		return FALSE;
	*pSlash = 0;

	_snprintf( pOut, uiOutSize - 1, "%s\\MAP_FACTORY\\%s.mfr", zExePath, pBase );
	pOut[uiOutSize - 1] = 0;
	return TRUE;
}

static UINT32 MapFactoryApplyRecipe( const STR8 pMapFilename )
{
	if ( !gfMapPreviewCaptureMode || pMapFilename == NULL )
		return 0;

	CHAR8 zBase[260];
	MapFactoryBaseName( pMapFilename, zBase, sizeof(zBase) );
	if ( zBase[0] == 0 || MapFactoryHasSuffixNoCase( zBase, "_PRISTINE" ) ||
		 MapFactoryHasSuffixNoCase( zBase, "_REMASTERED" ) )
		return 0;

	CHAR8 zRecipe[MAX_PATH + 320];
	if ( !MapFactoryRecipePath( zBase, zRecipe, sizeof(zRecipe) ) )
		return 0;

	FILE *fp = fopen( zRecipe, "r" );
	if ( fp == NULL )
		return 0;

	UINT32 uiApplied = 0;
	CHAR8 zLine[512];
	while ( fgets( zLine, sizeof(zLine), fp ) != NULL )
	{
		CHAR8 *pNL = strpbrk( zLine, "\r\n" );
		if ( pNL != NULL ) *pNL = 0;
		if ( zLine[0] == 0 || zLine[0] == '#' )
			continue;

		CHAR8 zCommand[64] = {0};
		CHAR8 zType[64] = {0};
		INT32 sGridNo = NOWHERE;
		UINT16 usSubIndex = 0;
		if ( sscanf( zLine, "%63[^,],%d,%63[^,],%hu", zCommand, &sGridNo, zType, &usSubIndex ) == 4 )
		{
			UINT32 uiType = 0;
			if ( MapFactoryTileTypeFromName( zType, &uiType ) )
			{
				const BOOLEAN fOnRoof = ( _stricmp( zCommand, "ADD_ONROOF" ) == 0 );
				if ( ( _stricmp( zCommand, "ADD_OBJECT" ) == 0 || fOnRoof ) &&
					 MapFactoryAddVisual( sGridNo, uiType, usSubIndex, fOnRoof ) )
					++uiApplied;
			}
		}
	}
	fclose( fp );

	CHAR8 zStatus[128];
	_snprintf( zStatus, sizeof(zStatus) - 1, "FACTORY recipe=%s applied=%lu", zBase, uiApplied );
	zStatus[sizeof(zStatus) - 1] = 0;
	MapPreviewWriteStatus( zStatus );
	return uiApplied;
}

static BOOLEAN MapFactoryNeighbourFlags( INT32 sGridNo, BOOLEAN *pfNearStruct,
	BOOLEAN *pfNearWater, BOOLEAN *pfNearTrail )
{
	if ( sGridNo < 0 || sGridNo >= WORLD_MAX || gpWorldLevelData == NULL )
		return FALSE;

	*pfNearStruct = FALSE;
	*pfNearWater = FALSE;
	*pfNearTrail = FALSE;
	const INT32 sCol = sGridNo % WORLD_COLS;
	const INT32 sNeighbours[4] =
	{
		sGridNo - WORLD_COLS, sGridNo + WORLD_COLS, sGridNo - 1, sGridNo + 1
	};
	for ( UINT8 i = 0; i < 4; ++i )
	{
		const INT32 sNext = sNeighbours[i];
		if ( sNext < 0 || sNext >= WORLD_MAX ) continue;
		if ( i == 2 && sCol == 0 ) continue;
		if ( i == 3 && sCol + 1 >= WORLD_COLS ) continue;

		MAP_ELEMENT *pNext = &gpWorldLevelData[sNext];
		if ( pNext->pStructHead != NULL || pNext->pRoofHead != NULL )
			*pfNearStruct = TRUE;
		if ( pNext->pLandHead != NULL )
		{
			UINT32 uiType = 0;
			if ( GetTileType( pNext->pLandHead->usIndex, &uiType ) )
			{
				if ( uiType == REGWATERTEXTURE || uiType == DEEPWATERTEXTURE )
					*pfNearWater = TRUE;
				if ( uiType == SEVENTHTEXTURE )
					*pfNearTrail = TRUE;
			}
		}
	}
	return TRUE;
}

static void MapFactoryWriteProfile( const STR8 pMapFilename )
{
	if ( !gfMapPreviewCaptureMode || pMapFilename == NULL || gpWorldLevelData == NULL )
		return;

	CHAR8 zBase[260];
	MapFactoryBaseName( pMapFilename, zBase, sizeof(zBase) );
	CHAR8 zPreviewDir[MAX_PATH + 32];
	if ( !GetMapPreviewDirectory( zPreviewDir, sizeof(zPreviewDir) ) )
		return;

	CHAR8 zOutput[MAX_PATH + 320];
	_snprintf( zOutput, sizeof(zOutput) - 1, "%s\\%s_factory_profile.csv", zPreviewDir, zBase );
	zOutput[sizeof(zOutput) - 1] = 0;

	FILE *fp = fopen( zOutput, "w" );
	if ( fp == NULL )
		return;
	fprintf( fp, "kind,grid,row,col,land_type,near_structure,near_water,near_trail\n" );

	const INT32 sRows = WORLD_MAX / WORLD_COLS;
	for ( INT32 sGridNo = 0; sGridNo < WORLD_MAX; ++sGridNo )
	{
		const INT32 sRow = sGridNo / WORLD_COLS;
		const INT32 sCol = sGridNo % WORLD_COLS;
		MAP_ELEMENT *pMap = &gpWorldLevelData[sGridNo];

		if ( pMap->pRoofHead != NULL && ((sRow + sCol) % 7) == 0 )
		{
			fprintf( fp, "roof,%d,%d,%d,0,1,0,0\n", sGridNo, sRow, sCol );
			continue;
		}

		if ( (sRow % 4) != 0 || (sCol % 4) != 0 || !MapFactoryVisualGridSafe( sGridNo ) )
			continue;

		UINT32 uiLandType = 0;
		if ( pMap->pLandHead == NULL || !GetTileType( pMap->pLandHead->usIndex, &uiLandType ) )
			continue;
		BOOLEAN fNearStruct = FALSE, fNearWater = FALSE, fNearTrail = FALSE;
		MapFactoryNeighbourFlags( sGridNo, &fNearStruct, &fNearWater, &fNearTrail );
		fprintf( fp, "open,%d,%d,%d,%lu,%u,%u,%u\n", sGridNo, sRow, sCol, uiLandType,
			fNearStruct ? 1 : 0, fNearWater ? 1 : 0, fNearTrail ? 1 : 0 );
	}
	fclose( fp );

	CHAR8 zStatus[128];
	_snprintf( zStatus, sizeof(zStatus) - 1, "PROFILE_OK %s", zBase );
	zStatus[sizeof(zStatus) - 1] = 0;
	MapPreviewWriteStatus( zStatus );
}

static UINT8 MapFactoryLoadRecipeCameras( const STR8 pMapFilename, INT32 *pCamera, UINT8 ubMax )
{
	if ( pMapFilename == NULL || pCamera == NULL || ubMax == 0 )
		return 0;

	CHAR8 zBase[260];
	MapFactoryBaseName( pMapFilename, zBase, sizeof(zBase) );
	if ( MapFactoryHasSuffixNoCase( zBase, "_PRISTINE" ) )
		zBase[strlen(zBase) - strlen("_PRISTINE")] = 0;
	else if ( MapFactoryHasSuffixNoCase( zBase, "_REMASTERED" ) )
		zBase[strlen(zBase) - strlen("_REMASTERED")] = 0;

	CHAR8 zRecipe[MAX_PATH + 320];
	if ( !MapFactoryRecipePath( zBase, zRecipe, sizeof(zRecipe) ) )
		return 0;
	FILE *fp = fopen( zRecipe, "r" );
	if ( fp == NULL )
		return 0;

	UINT8 ubCount = 0;
	CHAR8 zLine[512];
	while ( ubCount < ubMax && fgets( zLine, sizeof(zLine), fp ) != NULL )
	{
		INT32 sGridNo = NOWHERE;
		if ( sscanf( zLine, "CAMERA,%d", &sGridNo ) == 1 &&
			 sGridNo >= 0 && sGridNo < WORLD_MAX )
			pCamera[ubCount++] = sGridNo;
	}
	fclose( fp );
	return ubCount;
}

static UINT8 MapFactoryBuildCameraSet( const STR8 pMapFilename, INT32 *pCamera, UINT8 ubMax )
{
	UINT8 ubCount = MapFactoryLoadRecipeCameras( pMapFilename, pCamera, ubMax );
	const INT32 sRows = WORLD_MAX / WORLD_COLS;

	// Prefer roofs/structures so tactical QA samples authored points of interest.
	for ( INT32 sGridNo = 0; ubCount < ubMax && sGridNo < WORLD_MAX; ++sGridNo )
	{
		if ( gpWorldLevelData[sGridNo].pRoofHead == NULL )
			continue;
		const INT32 sRow = sGridNo / WORLD_COLS;
		const INT32 sCol = sGridNo % WORLD_COLS;
		BOOLEAN fFarEnough = TRUE;
		for ( UINT8 i = 0; i < ubCount; ++i )
		{
			const INT32 r = pCamera[i] / WORLD_COLS;
			const INT32 c = pCamera[i] % WORLD_COLS;
			if ( abs( r - sRow ) + abs( c - sCol ) < 28 )
			{
				fFarEnough = FALSE;
				break;
			}
		}
		if ( fFarEnough )
			pCamera[ubCount++] = sGridNo;
	}

	const INT32 sFallback[][2] =
	{
		{ sRows / 4, WORLD_COLS / 4 },
		{ sRows / 4, (WORLD_COLS * 3) / 4 },
		{ sRows / 2, WORLD_COLS / 2 },
		{ (sRows * 3) / 4, WORLD_COLS / 4 },
		{ (sRows * 3) / 4, (WORLD_COLS * 3) / 4 },
		{ sRows / 2, (WORLD_COLS * 3) / 4 }
	};
	for ( UINT8 i = 0; ubCount < ubMax && i < (UINT8)(sizeof(sFallback)/sizeof(sFallback[0])); ++i )
	{
		const INT32 sGridNo = sFallback[i][0] * WORLD_COLS + sFallback[i][1];
		if ( sGridNo >= 0 && sGridNo < WORLD_MAX )
			pCamera[ubCount++] = sGridNo;
	}
	return ubCount;
}

static void SaveMapFactoryTacticalPreviewSet( const STR8 pMapFilename )
{
	if ( !gfMapPreviewCaptureMode || pMapFilename == NULL || gpWorldLevelData == NULL )
		return;

	INT32 sCameraGrid[6];
	const UINT8 ubCameraCount = MapFactoryBuildCameraSet( pMapFilename, sCameraGrid, 6 );
	for ( UINT8 i = 0; i < ubCameraCount; ++i )
	{
		INT16 sCellX = 0, sCellY = 0;
		ConvertGridNoToCenterCellXY( sCameraGrid[i], &sCellX, &sCellY );
		SetRenderCenter( sCellX, sCellY );
		InvalidateWorldRedundency();
		SetRenderFlags( RENDER_FLAG_FULL | RENDER_FLAG_SHADOWS );
		RenderWorld();

		CHAR8 zShotName[320];
		_snprintf( zShotName, sizeof(zShotName) - 1, "%s_tactical_%02u.dat", pMapFilename, (UINT16)(i + 1) );
		zShotName[sizeof(zShotName) - 1] = 0;

		const UINT16 usCaptureHeight = ( gsVIEWPORT_END_Y > 0 && gsVIEWPORT_END_Y <= SCREEN_HEIGHT )
			? (UINT16)gsVIEWPORT_END_Y : (UINT16)SCREEN_HEIGHT;
		SaveEngineMapPreviewBMP( zShotName, FRAME_BUFFER, (UINT16)SCREEN_WIDTH, usCaptureHeight );
	}

	CHAR8 zStatus[96];
	_snprintf( zStatus, sizeof(zStatus) - 1, "TACTICAL_OK captures=%u", ubCameraCount );
	zStatus[sizeof(zStatus) - 1] = 0;
	MapPreviewWriteStatus( zStatus );
}


enum MAP_FACTORY_ARCHETYPE
{
	MAP_FACTORY_FARMLAND = 0,
	MAP_FACTORY_WILDERNESS,
	MAP_FACTORY_MILITARY,
	MAP_FACTORY_INDUSTRIAL,
	MAP_FACTORY_SETTLEMENT,
	MAP_FACTORY_ROADSIDE,
	MAP_FACTORY_OPEN_COUNTRY
};

static UINT16 MapFactoryPickSubIndex( UINT32 uiType, UINT32 uiSeed )
{
	if ( uiType >= NUMBEROFTILETYPES || gTileSurfaceArray[uiType] == NULL ||
		 gTileSurfaceArray[uiType]->vo == NULL )
		return 0;

	UINT16 usCapacity = (UINT16)gTileSurfaceArray[uiType]->vo->usNumberOfObjects;
	if ( gNumTilesPerType[uiType] < usCapacity )
		usCapacity = gNumTilesPerType[uiType];
	if ( usCapacity == 0 )
		return 0;
	if ( usCapacity > 8 )
		usCapacity = 8;
	return (UINT16)(1 + (uiSeed % usCapacity));
}

static BOOLEAN MapFactoryAnchorMatches( INT32 sGridNo, UINT8 ubArchetype )
{
	if ( !MapFactoryVisualGridSafe( sGridNo ) )
		return FALSE;

	BOOLEAN fNearStruct = FALSE, fNearWater = FALSE, fNearTrail = FALSE;
	MapFactoryNeighbourFlags( sGridNo, &fNearStruct, &fNearWater, &fNearTrail );

	switch ( ubArchetype )
	{
		case MAP_FACTORY_MILITARY:
		case MAP_FACTORY_INDUSTRIAL:
		case MAP_FACTORY_SETTLEMENT:
			return fNearStruct && !fNearWater;
		case MAP_FACTORY_ROADSIDE:
			return fNearTrail && !fNearWater;
		case MAP_FACTORY_FARMLAND:
			return !fNearStruct && !fNearWater;
		case MAP_FACTORY_WILDERNESS:
			return !fNearStruct && !fNearWater;
		default:
			return !fNearWater;
	}
}

static UINT16 MapFactoryPlacePilotModule( INT32 sAnchor, UINT8 ubArchetype, UINT32 uiSeed )
{
	const INT32 sRow = sAnchor / WORLD_COLS;
	const INT32 sCol = sAnchor % WORLD_COLS;
	const INT8 bDx[4] = { 0, 2, 0, 2 };
	const INT8 bDy[4] = { 0, 0, 2, 2 };

	UINT32 uiTypes[4];
	switch ( ubArchetype )
	{
		case MAP_FACTORY_MILITARY:
			uiTypes[0] = DEBRISMISC; uiTypes[1] = DEBRISWOOD;
			uiTypes[2] = DEBRISROCKS; uiTypes[3] = DEBRISMISC; break;
		case MAP_FACTORY_INDUSTRIAL:
			uiTypes[0] = DEBRISMISC; uiTypes[1] = DEBRISWOOD;
			uiTypes[2] = DEBRISROCKS; uiTypes[3] = DEBRISGRASS; break;
		case MAP_FACTORY_SETTLEMENT:
			uiTypes[0] = DEBRISMISC; uiTypes[1] = DEBRISWOOD;
			uiTypes[2] = DEBRISWEEDS; uiTypes[3] = DEBRISROCKS; break;
		case MAP_FACTORY_ROADSIDE:
			uiTypes[0] = DEBRISSAND; uiTypes[1] = DEBRISROCKS;
			uiTypes[2] = DEBRISWEEDS; uiTypes[3] = DEBRISWOOD; break;
		case MAP_FACTORY_FARMLAND:
			uiTypes[0] = DEBRISGRASS; uiTypes[1] = DEBRISWEEDS;
			uiTypes[2] = DEBRISSAND; uiTypes[3] = DEBRISWOOD; break;
		case MAP_FACTORY_WILDERNESS:
			uiTypes[0] = DEBRISWEEDS; uiTypes[1] = DEBRISROCKS;
			uiTypes[2] = DEBRISGRASS; uiTypes[3] = DEBRISWOOD; break;
		default:
			uiTypes[0] = DEBRISWEEDS; uiTypes[1] = DEBRISROCKS;
			uiTypes[2] = DEBRISGRASS; uiTypes[3] = DEBRISWOOD; break;
	}

	INT32 sGrid[4];
	UINT16 usSub[4];
	for ( UINT8 i = 0; i < 4; ++i )
	{
		const INT32 r = sRow + bDy[i];
		const INT32 col = sCol + bDx[i];
		if ( r < 0 || col < 0 || col >= WORLD_COLS )
			return 0;
		sGrid[i] = r * WORLD_COLS + col;
		if ( sGrid[i] < 0 || sGrid[i] >= WORLD_MAX || !MapFactoryVisualGridSafe( sGrid[i] ) )
			return 0;
		usSub[i] = MapFactoryPickSubIndex( uiTypes[i], uiSeed + i * 17u );
		if ( usSub[i] == 0 )
			return 0;
	}

	UINT16 usPlaced = 0;
	for ( UINT8 i = 0; i < 4; ++i )
		if ( MapFactoryAddVisual( sGrid[i], uiTypes[i], usSub[i], FALSE ) )
			++usPlaced;
	return usPlaced;
}

static UINT32 MapFactoryApplyPilotDesign( UINT8 ubArchetype )
{
	if ( gpWorldLevelData == NULL )
		return 0;

	const INT32 sRows = WORLD_MAX / WORLD_COLS;
	INT32 sLastRow = -1000, sLastCol = -1000;
	UINT32 uiModules = 0, uiPieces = 0;

	for ( INT32 sRow = 5; sRow < sRows - 5 && uiModules < 8; sRow += 3 )
	{
		for ( INT32 sCol = 5; sCol < WORLD_COLS - 5 && uiModules < 8; sCol += 3 )
		{
			const INT32 sGridNo = sRow * WORLD_COLS + sCol;
			if ( !MapFactoryAnchorMatches( sGridNo, ubArchetype ) )
				continue;

			if ( abs( sRow - sLastRow ) + abs( sCol - sLastCol ) < 24 )
				continue;

			const UINT32 uiHash = (UINT32)sGridNo * 2654435761u + (UINT32)ubArchetype * 2246822519u;
			if ( (uiHash & 3u) != 0u )
				continue;

			const UINT16 usPlaced = MapFactoryPlacePilotModule( sGridNo, ubArchetype, uiHash );
			if ( usPlaced == 4 )
			{
				++uiModules;
				uiPieces += usPlaced;
				sLastRow = sRow;
				sLastCol = sCol;
			}
		}
	}

	CHAR8 zStatus[160];
	_snprintf( zStatus, sizeof(zStatus) - 1, "PILOT_DESIGN archetype=%u modules=%lu pieces=%lu",
		(UINT16)ubArchetype, uiModules, uiPieces );
	zStatus[sizeof(zStatus) - 1] = 0;
	MapPreviewWriteStatus( zStatus );
	return uiPieces;
}

static BOOLEAN MapFactoryLoadPilotMap( const STR8 pMapName )
{
	FLOAT dMajorMapVersion = 0.0f;
	UINT8 ubMinorMapVersion = 0;
	if ( !LoadWorld( (STR8)pMapName, &dMajorMapVersion, &ubMinorMapVersion ) )
		return FALSE;
	LightReset();
	LightSpriteRenderAll();
	return TRUE;
}


static BOOLEAN MapFactoryStageRemasteredMap( const STR8 pMapName )
{
	if ( pMapName == NULL || pMapName[0] == 0 )
		return FALSE;

	CHAR8 zExePath[MAX_PATH];
	DWORD dwLen = GetModuleFileNameA( NULL, zExePath, MAX_PATH );
	if ( dwLen == 0 || dwLen >= MAX_PATH )
		return FALSE;
	CHAR8 *pSlash = strrchr( zExePath, '\\' );
	if ( pSlash == NULL )
		return FALSE;
	*pSlash = 0;

	CHAR8 zSource[MAX_PATH + 320];
	_snprintf( zSource, sizeof(zSource) - 1,
		"%s\\Profiles\\UserProfile_Vengeance\\MAPS\\%s", zExePath, pMapName );
	zSource[sizeof(zSource) - 1] = 0;

	const CHAR8 *pStageRoot = getenv( "JA2_MAP_FACTORY_OUTPUT" );
	CHAR8 zFallback[MAX_PATH];
	if ( pStageRoot == NULL || pStageRoot[0] == 0 )
	{
		strncpy( zFallback, "C:\\VENGENCE\\Jagged Alliance 2\\MapFactoryOutput", sizeof(zFallback) - 1 );
		zFallback[sizeof(zFallback) - 1] = 0;
		pStageRoot = zFallback;
	}

	CreateDirectoryA( pStageRoot, NULL );
	CHAR8 zDestination[MAX_PATH + 320];
	_snprintf( zDestination, sizeof(zDestination) - 1, "%s\\%s", pStageRoot, pMapName );
	zDestination[sizeof(zDestination) - 1] = 0;

	const BOOLEAN fCopied = CopyFileA( zSource, zDestination, FALSE ) ? TRUE : FALSE;
	CHAR8 zStatus[480];
	_snprintf( zStatus, sizeof(zStatus) - 1, "STAGE_%s map=%s destination=%s",
		fCopied ? "OK" : "FAIL", pMapName, zDestination );
	zStatus[sizeof(zStatus) - 1] = 0;
	MapPreviewWriteStatus( zStatus );
	return fCopied;
}

static void MapFactoryRunPilotSector( const STR8 pMapName, UINT8 ubArchetype )
{
	if ( pMapName == NULL )
		return;

	CHAR8 zBase[260];
	MapFactoryBaseName( pMapName, zBase, sizeof(zBase) );

	if ( !MapFactoryLoadPilotMap( pMapName ) )
	{
		CHAR8 zFail[320];
		_snprintf( zFail, sizeof(zFail) - 1, "PILOT_FAIL load %s", pMapName );
		zFail[sizeof(zFail) - 1] = 0;
		MapPreviewWriteStatus( zFail );
		return;
	}

	CHAR8 zPristine[320];
	_snprintf( zPristine, sizeof(zPristine) - 1, "%s_PRISTINE.dat", zBase );
	zPristine[sizeof(zPristine) - 1] = 0;
	MapFactoryWriteProfile( pMapName );
	SaveMapFactoryTacticalPreviewSet( zPristine );

	const UINT32 uiPieces = MapFactoryApplyPilotDesign( ubArchetype );
	SaveMapFactoryTacticalPreviewSet( pMapName );

	CHAR8 zRemastered[320];
	_snprintf( zRemastered, sizeof(zRemastered) - 1, "%s_REMASTERED.dat", zBase );
	zRemastered[sizeof(zRemastered) - 1] = 0;
	const BOOLEAN fSaved = ( uiPieces > 0 ) ? SaveWorld( zRemastered ) : FALSE;
	if ( fSaved )
		MapFactoryStageRemasteredMap( zRemastered );

	if ( fSaved && MapFactoryLoadPilotMap( zRemastered ) )
		SaveMapFactoryTacticalPreviewSet( zRemastered );

	CHAR8 zDone[384];
	_snprintf( zDone, sizeof(zDone) - 1, "PILOT_%s sector=%s pieces=%lu output=%s",
		fSaved ? "OK" : "FAIL", zBase, uiPieces, zRemastered );
	zDone[sizeof(zDone) - 1] = 0;
	MapPreviewWriteStatus( zDone );
}

static void MapFactoryRunFiveSectorPilot( const STR8 pTriggerMap )
{
	if ( pTriggerMap == NULL )
		return;

	CHAR8 zBase[260];
	MapFactoryBaseName( pTriggerMap, zBase, sizeof(zBase) );
	if ( _stricmp( zBase, "A3" ) != 0 )
		return;

	const CHAR8 *pActions = getenv( "GITHUB_ACTIONS" );
	if ( pActions == NULL || _stricmp( pActions, "true" ) != 0 )
		return;

	static BOOLEAN fPilotAlreadyRun = FALSE;
	if ( fPilotAlreadyRun )
		return;
	fPilotAlreadyRun = TRUE;

	MapPreviewWriteStatus( "PILOT_BEGIN A8,A12,B13,F15" );
	MapFactoryRunPilotSector( "A8.dat", MAP_FACTORY_MILITARY );
	MapFactoryRunPilotSector( "A12.DAT", MAP_FACTORY_WILDERNESS );
	MapFactoryRunPilotSector( "b13.dat", MAP_FACTORY_INDUSTRIAL );
	MapFactoryRunPilotSector( "f15.dat", MAP_FACTORY_MILITARY );
	MapPreviewWriteStatus( "PILOT_DONE A8,A12,B13,F15" );
}

void GenerateAllMapsInit(void)
{
	GETFILESTRUCT FileInfo;
	TrashFDlgList(FileList);
	gfMapPreviewCaptureMode = FALSE;
	if(GetFileFirst("MAPS\\*.dat", &FileInfo))
	{
		FileList = AddToFDlgList(FileList, &FileInfo);
		while(GetFileNext(&FileInfo))
			FileList = AddToFDlgList(FileList, &FileInfo);
		GetFileClose(&FileInfo);
	}
	FListNode = FileList;
}

BOOLEAN GenerateSingleMapPreviewInit( STR8 pMapFile )
{
	GETFILESTRUCT FileInfo;
	CHAR8 zMapPath[320];
	CHAR8 zMapName[260];

	TrashFDlgList( FileList );
	FileList = FListNode = NULL;
	gfMapPreviewCaptureMode = TRUE;
	MapPreviewWriteStatus( "INIT mapshot requested" );

	if ( pMapFile == NULL || pMapFile[0] == 0 )
	{
		gfMapPreviewCaptureMode = FALSE;
		MapPreviewWriteStatus( "INIT_FAIL empty map name" );
		return FALSE;
	}

	// Accept extra command-line switches after the file name, e.g.
	// -MAPSHOT=A3.dat /WINDOW /NOSOUND.
	UINT32 i = 0;
	while ( pMapFile[i] && pMapFile[i] != ' ' && pMapFile[i] != '\t' && i < sizeof(zMapName) - 1 )
	{
		zMapName[i] = pMapFile[i];
		++i;
	}
	zMapName[i] = 0;

	_snprintf( zMapPath, sizeof(zMapPath) - 1, "MAPS\\%s", zMapName );
	zMapPath[sizeof(zMapPath) - 1] = 0;
	if ( !GetFileFirst( zMapPath, &FileInfo ) )
	{
		gfMapPreviewCaptureMode = FALSE;
		MapPreviewWriteStatus( "INIT_FAIL map not found through VFS" );
		return FALSE;
	}

	FileList = AddToFDlgList( FileList, &FileInfo );
	GetFileClose( &FileInfo );
	FListNode = FileList;
	MapPreviewWriteStatus( FListNode ? "INIT_OK map queued" : "INIT_FAIL map queue empty" );
	return ( FListNode != NULL );
}

// Utililty file for sub-sampling/creating our radar screen maps.
// Loops through our maps directory and reads all *.map files, subsamples an area, color quantizes into an 8-bit image and writes to sti file in radarmaps.
// From editor will create radar map for you current map.
//dnl ch77 121113
UINT32 MapUtilScreenInit(void)
{
	UINT8 ubBitDepth;
	UINT16 usWidth, usHeight;
	VSURFACE_DESC vs_desc;

	vs_desc.fCreateFlags = VSURFACE_CREATE_DEFAULT | VSURFACE_SYSTEM_MEM_USAGE;
	// Create render buffer
	GetCurrentVideoSettings(&usWidth, &usHeight, &ubBitDepth);
	if(!GetVideoSurface(&ghVSurface, guiMiniMap))
	{
		vs_desc.usWidth = MINIMAP_X_SIZE;
		vs_desc.usHeight = MINIMAP_Y_SIZE;
		vs_desc.ubBitDepth = ubBitDepth;
		if(AddVideoSurface(&vs_desc, (UINT32*)&guiMiniMap) == FALSE)
			return(ERROR_SCREEN);
	}
	// Allocate 24 bit Surface
	if(!p24BitValues)
		p24BitValues = (RGBValues*)MemAlloc(MINIMAP_X_SIZE * MINIMAP_Y_SIZE * sizeof(RGBValues));
	//Allocate 8-bit surface
	if(!GetVideoSurface(&ghVSurface, gui8BitMiniMap))
	{
		vs_desc.usWidth = MINIMAP_X_SIZE;
		vs_desc.usHeight = MINIMAP_Y_SIZE;
		vs_desc.ubBitDepth = 8;
		if(AddVideoSurface(&vs_desc, (UINT32*)&gui8BitMiniMap) == FALSE)
			return(ERROR_SCREEN);
	}
	// Create big map buffer which contains whole map for radar map generation
	vs_desc.usWidth = (640 * WORLD_COLS / OLD_WORLD_COLS);
	vs_desc.usHeight = (320 * WORLD_ROWS / OLD_WORLD_ROWS) + 50;//!!! without this additional lines editor will crash as renderer go beyond them
	vs_desc.ubBitDepth = ubBitDepth;
	if(!GetVideoSurface(&ghVSurface, guiBigMap))
	{
		if(AddVideoSurface(&vs_desc, (UINT32*)&guiBigMap) == FALSE)
			return(ERROR_SCREEN);
	}
	else if(!(ghVSurface->usWidth == vs_desc.usWidth && ghVSurface->usHeight == vs_desc.usHeight))
	{
		DeleteVideoSurfaceFromIndex(guiBigMap);
		if(AddVideoSurface(&vs_desc, (UINT32*)&guiBigMap) == FALSE)
			return(ERROR_SCREEN);
	}
	GetVideoSurface(&ghVSurface, gui8BitMiniMap);
	return(TRUE);
}

//dnl ch77 131113
UINT32 MapUtilScreenHandle(void)
{
	InputAtom InputEvent;
	UINT32 uiMapFactoryChanges = 0;
	SGPPaletteEntry pPalette[256];
	CHAR8 zFilename[260], zFilename2[260];
	UINT8 *pDataPtr, ubMinorMapVersion;
	UINT16 *pDestBuf, *pSrcBuf;
	UINT32 uiDestPitchBYTES, uiSrcPitchBYTES, uiRGBColor, bR, bG, bB, bAvR, bAvG, bAvB;
	INT16 s16BPPSrc, sDest16BPPColor, sX1, sX2, sY1, sY2, sTop, sBottom, sLeft, sRight;
	INT32 cnt, iX, iY, iSubX1, iSubY1, iSubX2, iSubY2, iWindowX, iWindowY, iCount;
	FLOAT dX, dY, dStartX, dStartY, dMajorMapVersion;

	while(DequeueSpecificEvent(&InputEvent, KEY_DOWN|KEY_UP|KEY_REPEAT) == TRUE || (_RightButtonDown ? InputEvent.usParam = ESC : 0))//dnl ch78 291113
	{
		if(InputEvent.usParam == ESC)
		{
			// Exit the program
			if(fEditModeFirstTime == FALSE)
			{
				TrashFDlgList(FileList);
				FileList = FListNode = NULL;
				EnableEditorTaskbar();
				EnableAllTextFields();
				gfMapUtilityWindowActive = FALSE;
				return(EDIT_SCREEN);
			}
			else
			{
				gfProgramIsRunning = FALSE;
				return(MAPUTILITY_SCREEN);
			}
		}
	}
	if(gfMapUtilityWindowActive)
	{
		SetCurrentCursorFromDatabase(VIDEO_NO_CURSOR);//dnl ch78 271113
		return(MAPUTILITY_SCREEN);
	}
	else
	{
		DisableEditorTaskbar();
		DisableAllTextFields();
	}
	sDest16BPPColor = -1;
	bAvR = bAvG = bAvB = 0;
	// Zero out area!
	ColorFillVideoSurfaceArea(FRAME_BUFFER, 0, 0, (INT16)(SCREEN_WIDTH), (INT16)(SCREEN_HEIGHT), Get16BPPColor(FROMRGB(0, 0, 0)));
	if(fEditModeFirstTime == FALSE && gfWorldLoaded && FListNode == NULL)// Just create radarmap for current loaded world
		sprintf(zFilename, "%s", gubFilename);
	else
	{
		// OK, we are here, now loop through files
		if(FListNode == NULL)
		{
			gfProgramIsRunning = FALSE;
			return(MAPUTILITY_SCREEN);
		}
		sprintf(zFilename, "%s", FListNode->FileInfo.zFileName);
		// OK, load maps and do overhead shrinkage of them... //dnl ch79 301113
		if(!LoadWorld(zFilename, &dMajorMapVersion, &ubMinorMapVersion))
			return(ERROR_SCREEN);
		uiMapFactoryChanges = MapFactoryApplyRecipe( zFilename );
		MapFactoryWriteProfile( zFilename );
		if(strcmp(gzCommandLine, "-DOMAPSCNV") == 0)
			if(!(dMajorMapVersion == MAJOR_MAP_VERSION && ubMinorMapVersion == MINOR_MAP_VERSION && gMapInformation.ubMapVersion == MINOR_MAP_VERSION))
				if(!SaveWorld(zFilename))
					return(ERROR_SCREEN);
		LightReset();
		LightSpriteRenderAll();
	}
	// Capture normal tactical views first.  These are the primary visual-QA
	// images for A3 art iteration; the overhead render below remains useful only
	// for composition/navigation.
	SaveMapFactoryTacticalPreviewSet( zFilename );

	// Render small map
	//iOffsetHorizontal = (SCREEN_WIDTH / 2) - (640 / 2);// Horizontal start postion of the overview map
	//iOffsetVertical = (SCREEN_HEIGHT - 160) / 2 - 160;// Vertical start position of the overview map
	iOffsetHorizontal = iOffsetVertical = 0;
	MapUtilScreenInit();//dnl ch79 291113
	//ColorFillVideoSurfaceArea(guiBigMap, 0, 0, (INT16)(640 * WORLD_COLS / OLD_WORLD_COLS), (INT16)(320 * WORLD_ROWS / OLD_WORLD_ROWS), Get16BPPColor(FROMRGB(0, 0, 0)));
	InitNewOverheadDB((UINT8)giCurrentTilesetID);
	gfOverheadMapDirty = TRUE;
	//Buggler: interim code for radar map sti creation <= 360x360 based on DBrot bigger overview code
	RenderOverheadMap(0, (WORLD_COLS/2), iOffsetHorizontal, iOffsetVertical, iOffsetHorizontal + (640 * WORLD_COLS / OLD_WORLD_COLS), iOffsetVertical + (320 * WORLD_ROWS / OLD_WORLD_ROWS), guiBigMap);//dnl ch82 090114

	// Preserve the full engine-rendered sector for visual QA before radar-map
	// downsampling destroys the detail we need to inspect.
	const BOOLEAN fPreviewSaved = SaveEngineMapPreviewBMP( zFilename, guiBigMap,
		(UINT16)(640 * WORLD_COLS / OLD_WORLD_COLS),
		(UINT16)(320 * WORLD_ROWS / OLD_WORLD_ROWS) );

	// MAPSHOT is also the Map Factory compiler path. Any source sector may be
	// persisted as <SECTOR>_REMASTERED.dat after recipe/visual-profile changes.
	BOOLEAN fBakeSaved = TRUE;
	CHAR8 zBakeBase[260];
	MapFactoryBaseName( zFilename, zBakeBase, sizeof(zBakeBase) );
	if ( gfMapPreviewCaptureMode &&
		 !MapFactoryHasSuffixNoCase( zBakeBase, "_PRISTINE" ) &&
		 !MapFactoryHasSuffixNoCase( zBakeBase, "_REMASTERED" ) )
	{
		CHAR8 zBakeName[320];
		_snprintf( zBakeName, sizeof(zBakeName) - 1, "%s_REMASTERED.dat", zBakeBase );
		zBakeName[sizeof(zBakeName) - 1] = 0;

		CHAR8 zBegin[384];
		_snprintf( zBegin, sizeof(zBegin) - 1, "BAKE begin %s changes=%lu", zBakeName, uiMapFactoryChanges );
		zBegin[sizeof(zBegin) - 1] = 0;
		MapPreviewWriteStatus( zBegin );

		fBakeSaved = SaveWorld( zBakeName );

		CHAR8 zDone[384];
		_snprintf( zDone, sizeof(zDone) - 1, "%s %s", fBakeSaved ? "BAKE_OK" : "BAKE_FAIL", zBakeName );
		zDone[sizeof(zDone) - 1] = 0;
		MapPreviewWriteStatus( zDone );
	}

	// Run the representative cross-archetype Map Factory pilot inside the same
	// editor invocation. The existing CI artifact glob already captures all
	// tactical screenshots, so no workflow-specific batching is required.
	if ( gfMapPreviewCaptureMode )
		MapFactoryRunFiveSectorPilot( zFilename );

	// MAPSHOT is a single-purpose automation path. Do not spend another pass
	// generating/quantizing the tiny radar STI; stop immediately after the
	// full engine overview and optional baked map are on disk.
	if ( gfMapPreviewCaptureMode )
	{
		TrashOverheadMap();
		FListNode = NULL;
		gfProgramIsRunning = FALSE;
		MapPreviewWriteStatus( (fPreviewSaved && fBakeSaved) ? "DONE success" : "DONE failure" );
		return MAPUTILITY_SCREEN;
	}

	TrashOverheadMap();
	// OK, NOW PROCESS OVERHEAD MAP ( SHOULD BE ON THE FRAMEBUFFER )
	//Buggler: interim code for radar map sti creation <= 360x360 based on DBrot bigger overview code
	gdXStep	= (FLOAT)(640 * WORLD_COLS / OLD_WORLD_COLS) / (FLOAT)MINIMAP_X_SIZE;
	gdYStep	= (FLOAT)(320 * WORLD_ROWS / OLD_WORLD_ROWS) / (FLOAT)MINIMAP_Y_SIZE;
	dStartX = iOffsetHorizontal;
	dStartY = iOffsetVertical;
	// Adjust if we are using a restricted map...
	if(gMapInformation.ubRestrictedScrollID != 0)
	{
		CalculateRestrictedMapCoords(NORTH, &sX1, &sY1, &sX2, &sTop, iOffsetHorizontal+640, iOffsetVertical+320);
		CalculateRestrictedMapCoords(SOUTH, &sX1, &sBottom, &sX2, &sY2, iOffsetHorizontal+640, iOffsetVertical+320);
		CalculateRestrictedMapCoords(WEST,	&sX1, &sY1, &sLeft, &sY2, iOffsetHorizontal+640, iOffsetVertical+320);
		CalculateRestrictedMapCoords(EAST, &sRight, &sY1, &sX2, &sY2, iOffsetHorizontal+640, iOffsetVertical+320);
		gdXStep	= (FLOAT)(sRight - sLeft) / (FLOAT)MINIMAP_X_SIZE;
		gdYStep	= (FLOAT)(sBottom - sTop) / (FLOAT)MINIMAP_Y_SIZE;
		dStartX = sLeft;
		dStartY = sTop;
	}
	//LOCK BUFFERS
	dX = dStartX;
	dY = dStartY;
	pDestBuf = (UINT16*)LockVideoSurface(guiMiniMap, &uiDestPitchBYTES);
	pSrcBuf = (UINT16*)LockVideoSurface(guiBigMap, &uiSrcPitchBYTES);
	for ( iX = 0; iX < MINIMAP_X_SIZE; iX++ )
	{
		dY = dStartY;

		for ( iY = 0; iY < MINIMAP_Y_SIZE; iY++ )
		{
			//OK, AVERAGE PIXELS
			iSubX1 = (INT32)dX - WINDOW_SIZE;

			iSubX2 = (INT32)dX + WINDOW_SIZE;

			iSubY1 = (INT32)dY - WINDOW_SIZE;

			iSubY2 = (INT32)dY + WINDOW_SIZE;

			iCount = 0;
			bR = bG = bB = 0;

			for ( iWindowX = iSubX1; iWindowX < iSubX2; iWindowX++ )
			{
				for ( iWindowY = iSubY1; iWindowY < iSubY2; iWindowY++ )
				{
					//Buggler: interim code for radar map sti creation <= 360x360 based on DBrot bigger overview code					
					if ( iWindowX >= iOffsetHorizontal && iWindowX < (iOffsetHorizontal + (640 * WORLD_COLS / OLD_WORLD_COLS)) && iWindowY >= iOffsetVertical && iWindowY < (iOffsetVertical + (320 * WORLD_ROWS / OLD_WORLD_ROWS)) )
					{
						s16BPPSrc = pSrcBuf[ ( iWindowY * (uiSrcPitchBYTES/2) ) + iWindowX ];

						uiRGBColor = GetRGBColor( s16BPPSrc );

						bR += SGPGetRValue( uiRGBColor );
						bG += SGPGetGValue( uiRGBColor );
						bB += SGPGetBValue( uiRGBColor );

						// Average!
						iCount++;
					}
				}
			}

			if ( iCount > 0 )
			{
				bAvR = bR / (UINT8)iCount;
				bAvG = bG / (UINT8)iCount;
				bAvB = bB / (UINT8)iCount;

				sDest16BPPColor = Get16BPPColor( FROMRGB( bAvR, bAvG, bAvB ) );
			}

			//Write into dest!
			pDestBuf[ ( iY * (uiDestPitchBYTES/2) ) + iX ] = sDest16BPPColor;

			p24BitValues[ ( iY * (uiDestPitchBYTES/2) ) + iX ].r = (UINT8)bAvR;
			p24BitValues[ ( iY * (uiDestPitchBYTES/2) ) + iX ].g = (UINT8)bAvG;
			p24BitValues[ ( iY * (uiDestPitchBYTES/2) ) + iX ].b = (UINT8)bAvB;

			//Increment
			dY += gdYStep;
		}

		//Increment
		dX += gdXStep;
	}
	// RENDER!
	UnLockVideoSurface(guiMiniMap);
	iOffsetVertical = SCREEN_HEIGHT - 480;
	BltVideoSurface(FRAME_BUFFER, guiMiniMap, 0, iOffsetHorizontal+10, iOffsetVertical+360, VS_BLT_FAST|VS_BLT_USECOLORKEY, NULL);
	pDestBuf = (UINT16*)LockVideoSurface(FRAME_BUFFER, &uiDestPitchBYTES);
	Blt16BPPTo16BPP((UINT16 *)pDestBuf, uiDestPitchBYTES, (UINT16 *)pSrcBuf, uiSrcPitchBYTES, 0, 0, 0, 0, min((640 * WORLD_COLS / OLD_WORLD_COLS), SCREEN_WIDTH), min((320 * WORLD_ROWS / OLD_WORLD_ROWS), SCREEN_HEIGHT - 160));
	UnLockVideoSurface(guiBigMap);
	//QUantize!
	pDataPtr = (UINT8*)LockVideoSurface(gui8BitMiniMap, &uiSrcPitchBYTES);
	QuantizeImage(pDataPtr, (UINT8*)p24BitValues, MINIMAP_X_SIZE, MINIMAP_Y_SIZE, pPalette);
	SetVideoSurfacePalette(ghVSurface, pPalette);
	// Blit!
	Blt8BPPDataTo16BPPBuffer(pDestBuf, uiDestPitchBYTES, ghVSurface, pDataPtr, iOffsetHorizontal+10+MINIMAP_X_SIZE+20, iOffsetVertical+360);
	// Write palette!
	iX = iOffsetHorizontal + 10;
	iY = iOffsetVertical + 420;
	SetClippingRegionAndImageWidth(uiDestPitchBYTES, 0, 0, iOffsetHorizontal+640, iOffsetVertical+480);
	for(cnt=0; cnt<256; cnt++)
	{
		UINT16 usLineColor = Get16BPPColor(FROMRGB(pPalette[cnt].peRed, pPalette[cnt].peGreen, pPalette[cnt].peBlue));
		RectangleDraw(TRUE, iX, iY, iX, (INT16)(iY+10), usLineColor, (UINT8*)pDestBuf);
		iX++;
		RectangleDraw(TRUE, iX, iY, iX, (INT16)(iY+10), usLineColor, (UINT8*)pDestBuf);
		iX++;
	}
	UnLockVideoSurface(FRAME_BUFFER);
	// Remove extension
	for(cnt=strlen(zFilename)-1; cnt>=0; cnt--)
		if(zFilename[cnt] == '.')
			zFilename[cnt] = '\0';
	sprintf(zFilename2, "RADARMAPS\\%s.STI", zFilename);
	WriteSTIFile((INT8*)pDataPtr, pPalette, MINIMAP_X_SIZE, MINIMAP_Y_SIZE, (STR)zFilename2, CONVERT_ETRLE_COMPRESS, 0);
	UnLockVideoSurface(gui8BitMiniMap);
	SetFont(TINYFONT1);
	SetFontBackground(FONT_MCOLOR_BLACK);
	SetFontForeground(FONT_MCOLOR_DKGRAY);
	mprintf(iOffsetHorizontal+10, iOffsetVertical+330, L"Writing radar image %S", zFilename2);
	mprintf(iOffsetHorizontal+10, iOffsetVertical+340, L"Using tileset %s", gTilesets[giCurrentTilesetID].zName);
	InvalidateScreen();
	// Set next
	if(FListNode)
		FListNode = FListNode->pNext;
	if(fEditModeFirstTime == FALSE && FListNode == NULL)
	{
		if(gfWorldLoaded)
		{
			gfMapUtilityWindowActive = TRUE;
			return(MAPUTILITY_SCREEN);
		}
		else
			return(EDIT_SCREEN);
	}
	return(MAPUTILITY_SCREEN);
}

UINT32 MapUtilScreenShutdown(void)
{
	TrashFDlgList(FileList);
	MemFree(p24BitValues);
	p24BitValues = NULL;
	gfMapPreviewCaptureMode = FALSE;
	return(TRUE);
}

#else //non-editor version

#include "types.h"
#include "screenids.h"

UINT32	MapUtilScreenInit( )
{
	return( TRUE );
}

UINT32	MapUtilScreenHandle( )
{
	//If this screen ever gets set, then this is a bad thing -- endless loop
	return( ERROR_SCREEN );
}

UINT32 MapUtilScreenShutdown( )
{
	return( TRUE );
}

#endif
