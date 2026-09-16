#include "builddefines.h"

#ifdef PRECOMPILEDHEADERS
	#include "TileEngine All.h"
#else
	#include <stdio.h>
	#include <string.h>
	#include "stdlib.h"
	#include "debug.h"
	#include "tiledef.h"
	#include "Animation Cache.h"
	#include "Animation Data.h"
	#include "Animation Control.h"
	#include "sys globals.h"
	#include "Debug Control.h"
	#include "tile surface.h"
	#include "tile cache.h"
	#include "fileman.h"
#endif

#include "ExceptionHandling.h"


UINT32	guiNumTileCacheStructs = 0;
UINT32 guiMaxTileCacheSize		= 50;
UINT32 guiCurTileCacheSize		= 0;
INT32	giDefaultStructIndex	= -1;

TILE_CACHE_ELEMENT		*gpTileCache = NULL;
TILE_CACHE_STRUCT			*gpTileCacheStructInfo = NULL;

// VHD-aware cache accounting.  The original cache treated every tile as equal and
// could evict an actively referenced tile when all 50 slots were occupied.  HD art
// breaks that assumption: resident cost varies dramatically with scale and format.
static UINT32 guiTileCacheAccessTick = 0;
static UINT32 guiTileCacheResidentBytes = 0;
static UINT32 guiTileCachePeakBytes = 0;
static UINT32 guiTileCacheHitCount = 0;
static UINT32 guiTileCacheMissCount = 0;
static UINT32 guiTileCacheEvictionCount = 0;
static UINT32 guiTileCacheBudgetBytes = 128u * 1024u * 1024u;

static UINT32 TileCacheSaturatingAdd( UINT32 a, UINT32 b )
{
	return ( 0xFFFFFFFFu - a < b ) ? 0xFFFFFFFFu : a + b;
}

static UINT32 TileCacheSaturatingMul( UINT32 a, UINT32 b )
{
	if ( a == 0 || b == 0 )
		return 0;
	return ( a > 0xFFFFFFFFu / b ) ? 0xFFFFFFFFu : a * b;
}

static BOOLEAN TileCacheDiagnosticsEnabled( )
{
	const CHAR8 *pEnabled = getenv( "VR_VHD_TILE_CACHE_DIAGNOSTICS" );
	return pEnabled != NULL && strcmp( pEnabled, "1" ) == 0;
}

static void InitTileCacheBudget( )
{
	const UINT32 uiDefaultBudgetMB = 128u;
	const UINT32 uiMinBudgetMB = 16u;
	const UINT32 uiMaxBudgetMB = 512u; // Win32/non-LAA: preserve process address-space headroom.
	UINT32 uiBudgetMB = uiDefaultBudgetMB;

	const CHAR8 *pBudgetMB = getenv( "VR_VHD_TILE_CACHE_MB" );
	if ( pBudgetMB != NULL && pBudgetMB[0] != 0 )
	{
		CHAR8 *pEnd = NULL;
		const unsigned long ulParsed = strtoul( pBudgetMB, &pEnd, 10 );
		if ( pEnd != pBudgetMB && pEnd != NULL && *pEnd == 0 )
		{
			if ( ulParsed < uiMinBudgetMB )
				uiBudgetMB = uiMinBudgetMB;
			else if ( ulParsed > uiMaxBudgetMB )
				uiBudgetMB = uiMaxBudgetMB;
			else
				uiBudgetMB = (UINT32)ulParsed;
		}
	}
	guiTileCacheBudgetBytes = uiBudgetMB * 1024u * 1024u;
}

static UINT32 EstimateTileImageryResidentBytes( PTILE_IMAGERY pImagery )
{
	if ( pImagery == NULL )
		return 0;

	UINT32 uiBytes = (UINT32)sizeof( TILE_IMAGERY );
	HVOBJECT hVObject = pImagery->vo;
	if ( hVObject == NULL )
		return uiBytes;

	uiBytes = TileCacheSaturatingAdd( uiBytes, (UINT32)sizeof( *hVObject ) );
	uiBytes = TileCacheSaturatingAdd( uiBytes, hVObject->uiSizePixData );
	uiBytes = TileCacheSaturatingAdd( uiBytes,
		TileCacheSaturatingMul( (UINT32)hVObject->usNumberOfObjects, (UINT32)sizeof( ETRLEObject ) ) );

	if ( hVObject->pPaletteEntry != NULL )
		uiBytes = TileCacheSaturatingAdd( uiBytes, 256u * (UINT32)sizeof( SGPPaletteEntry ) );
	if ( hVObject->p16BPPPalette != NULL )
		uiBytes = TileCacheSaturatingAdd( uiBytes, 256u * (UINT32)sizeof( UINT16 ) );

	if ( hVObject->p16BPPObject != NULL && hVObject->usNumberOf16BPPObjects > 0 )
	{
		uiBytes = TileCacheSaturatingAdd( uiBytes,
			TileCacheSaturatingMul( (UINT32)hVObject->usNumberOf16BPPObjects,
				(UINT32)sizeof( SixteenBPPObjectInfo ) ) );
		for ( UINT16 usObject = 0; usObject < hVObject->usNumberOf16BPPObjects; ++usObject )
		{
			const SixteenBPPObjectInfo *pObject = &hVObject->p16BPPObject[ usObject ];
			if ( pObject->p16BPPData != NULL )
			{
				UINT32 uiPixels = TileCacheSaturatingMul( (UINT32)pObject->usWidth, (UINT32)pObject->usHeight );
				uiBytes = TileCacheSaturatingAdd( uiBytes, TileCacheSaturatingMul( uiPixels, 2u ) );
			}
		}
	}

	if ( hVObject->ppZStripInfo != NULL && hVObject->usNumberOfObjects > 0 )
	{
		uiBytes = TileCacheSaturatingAdd( uiBytes,
			TileCacheSaturatingMul( (UINT32)hVObject->usNumberOfObjects, (UINT32)sizeof( ZStripInfo * ) ) );
		for ( UINT16 usObject = 0; usObject < hVObject->usNumberOfObjects; ++usObject )
		{
			const ZStripInfo *pZStrip = hVObject->ppZStripInfo[ usObject ];
			if ( pZStrip == NULL )
				continue;
			uiBytes = TileCacheSaturatingAdd( uiBytes, (UINT32)sizeof( ZStripInfo ) );
			if ( pZStrip->pbZChange != NULL )
				uiBytes = TileCacheSaturatingAdd( uiBytes, (UINT32)pZStrip->ubNumberOfZChanges * (UINT32)sizeof( INT8 ) );
		}
	}

	return uiBytes;
}

static void TouchTileCacheEntry( UINT32 uiIndex )
{
	if ( uiIndex >= guiMaxTileCacheSize )
		return;
	++guiTileCacheAccessTick;
	if ( guiTileCacheAccessTick == 0 )
		guiTileCacheAccessTick = 1;
	gpTileCache[ uiIndex ].uiLastAccessTick = guiTileCacheAccessTick;
}

static INT32 FindLRUUnreferencedTile( INT32 iProtectedIndex )
{
	INT32 iCandidate = -1;
	UINT32 uiOldestTick = 0xFFFFFFFFu;
	for ( UINT32 uiIndex = 0; uiIndex < guiCurTileCacheSize; ++uiIndex )
	{
		if ( (INT32)uiIndex == iProtectedIndex || gpTileCache[ uiIndex ].pImagery == NULL ||
			 gpTileCache[ uiIndex ].sHits > 0 )
			continue;
		if ( iCandidate == -1 || gpTileCache[ uiIndex ].uiLastAccessTick < uiOldestTick )
		{
			iCandidate = (INT32)uiIndex;
			uiOldestTick = gpTileCache[ uiIndex ].uiLastAccessTick;
		}
	}
	return iCandidate;
}

static void EvictTileCacheEntry( UINT32 uiIndex, const STR8 pReason )
{
	if ( uiIndex >= guiMaxTileCacheSize || gpTileCache[ uiIndex ].pImagery == NULL )
		return;

	if ( TileCacheDiagnosticsEnabled( ) )
	{
		BlackBoxEvent( "VHD_CACHE",
			"evict index=%u file=%s refs=%d bytes=%u reason=%s resident=%u budget=%u",
			uiIndex, gpTileCache[ uiIndex ].zName, gpTileCache[ uiIndex ].sHits,
			gpTileCache[ uiIndex ].uiResidentBytes, pReason != NULL ? pReason : "unknown",
			guiTileCacheResidentBytes, guiTileCacheBudgetBytes );
	}

	if ( gpTileCache[ uiIndex ].uiResidentBytes <= guiTileCacheResidentBytes )
		guiTileCacheResidentBytes -= gpTileCache[ uiIndex ].uiResidentBytes;
	else
		guiTileCacheResidentBytes = 0;

	DeleteTileSurface( gpTileCache[ uiIndex ].pImagery );
	gpTileCache[ uiIndex ].pImagery = NULL;
	gpTileCache[ uiIndex ].sHits = 0;
	gpTileCache[ uiIndex ].ubNumFrames = 0;
	gpTileCache[ uiIndex ].sStructRefID = -1;
	gpTileCache[ uiIndex ].uiLastAccessTick = 0;
	gpTileCache[ uiIndex ].uiResidentBytes = 0;
	gpTileCache[ uiIndex ].zName[0] = 0;
	gpTileCache[ uiIndex ].zRootName[0] = 0;
	++guiTileCacheEvictionCount;

	while ( guiCurTileCacheSize > 0 && gpTileCache[ guiCurTileCacheSize - 1 ].pImagery == NULL )
		--guiCurTileCacheSize;
}

static void TrimTileCacheToBudget( INT32 iProtectedIndex )
{
	while ( guiTileCacheResidentBytes > guiTileCacheBudgetBytes )
	{
		const INT32 iVictim = FindLRUUnreferencedTile( iProtectedIndex );
		if ( iVictim < 0 )
		{
			if ( TileCacheDiagnosticsEnabled( ) )
				BlackBoxEvent( "VHD_CACHE",
					"budget pressure resident=%u budget=%u but all resident tiles are referenced",
					guiTileCacheResidentBytes, guiTileCacheBudgetBytes );
			break;
		}
		EvictTileCacheEntry( (UINT32)iVictim, "byte-budget" );
	}
}

static BOOLEAN GrowTileCache( )
{
	const UINT32 uiOldMax = guiMaxTileCacheSize;
	const UINT32 uiGrowBy = 25;
	const UINT32 uiHardMax = 200;
	if ( uiOldMax >= uiHardMax )
		return FALSE;

	const UINT32 uiNewMax = __min( uiOldMax + uiGrowBy, uiHardMax );
	TILE_CACHE_ELEMENT *pNewCache = (TILE_CACHE_ELEMENT *)MemRealloc(
		gpTileCache, sizeof( TILE_CACHE_ELEMENT ) * uiNewMax );
	if ( pNewCache == NULL )
		return FALSE;

	gpTileCache = pNewCache;
	memset( &gpTileCache[ uiOldMax ], 0, sizeof( TILE_CACHE_ELEMENT ) * ( uiNewMax - uiOldMax ) );
	for ( UINT32 uiIndex = uiOldMax; uiIndex < uiNewMax; ++uiIndex )
		gpTileCache[ uiIndex ].sStructRefID = -1;
	guiMaxTileCacheSize = uiNewMax;

	BlackBoxEvent( "VHD_CACHE",
		"expanded slot table old=%u new=%u because all previous slots were actively referenced",
		uiOldMax, uiNewMax );
	return TRUE;
}



BOOLEAN InitTileCache(	)
{
	UINT32				cnt;
	GETFILESTRUCT FileInfo;
	INT16					sFiles = 0;

	InitTileCacheBudget( );
	gpTileCache = (TILE_CACHE_ELEMENT *)MemAlloc( sizeof( TILE_CACHE_ELEMENT ) * guiMaxTileCacheSize );
	if ( gpTileCache == NULL )
		return FALSE;

	memset( gpTileCache, 0, sizeof( TILE_CACHE_ELEMENT ) * guiMaxTileCacheSize );
	for ( cnt = 0; cnt < guiMaxTileCacheSize; cnt++ )
		gpTileCache[ cnt ].sStructRefID = -1;

	guiCurTileCacheSize = 0;
	guiTileCacheAccessTick = 0;
	guiTileCacheResidentBytes = 0;
	guiTileCachePeakBytes = 0;
	guiTileCacheHitCount = 0;
	guiTileCacheMissCount = 0;
	guiTileCacheEvictionCount = 0;

	if ( TileCacheDiagnosticsEnabled( ) )
		BlackBoxEvent( "VHD_CACHE", "init slots=%u budget=%u", guiMaxTileCacheSize, guiTileCacheBudgetBytes );


	// OK, look for JSD files in the tile cache directory and
	// load any we find....
	if( GetFileFirst("TILECACHE\\*.jsd", &FileInfo) )
	{
		while( GetFileNext(&FileInfo) )
		{
			sFiles++;
		}
		GetFileClose(&FileInfo);
	}

	// Allocate memory...
	if ( sFiles > 0 )
	{
		cnt = 0;

		guiNumTileCacheStructs = sFiles;

		gpTileCacheStructInfo = (TILE_CACHE_STRUCT *)MemAlloc( sizeof( TILE_CACHE_STRUCT ) * sFiles );

		// Loop through and set filenames
		if( GetFileFirst("TILECACHE\\*.jsd", &FileInfo) )
		{
			while( GetFileNext(&FileInfo) )
			{
				sprintf( gpTileCacheStructInfo[ cnt ].Filename, "TILECACHE\\%s", FileInfo.zFileName );

				// Get root name
				GetRootName( gpTileCacheStructInfo[ cnt ].zRootName, gpTileCacheStructInfo[ cnt ].Filename );

				// Load struc data....
				gpTileCacheStructInfo[ cnt ].pStructureFileRef = LoadStructureFile( gpTileCacheStructInfo[ cnt ].Filename );

#ifdef JA2TESTVERSION
				if ( gpTileCacheStructInfo[ cnt ].pStructureFileRef == NULL )
				{
					SET_ERROR(	"Cannot load tilecache JSD: %s", gpTileCacheStructInfo[ cnt ].Filename );
				}
#endif
		if ( _stricmp( gpTileCacheStructInfo[ cnt ].zRootName, "l_dead1" ) == 0 )
		{
			giDefaultStructIndex = cnt;
		}

				cnt++;
			}
			GetFileClose(&FileInfo);
		}
	}

	return( TRUE );
}

void DeleteTileCache( )
{
	UINT32 cnt;

	if ( TileCacheDiagnosticsEnabled( ) )
	{
		BlackBoxEvent( "VHD_CACHE",
			"shutdown resident=%u peak=%u hits=%u misses=%u evictions=%u slots=%u",
			guiTileCacheResidentBytes, guiTileCachePeakBytes, guiTileCacheHitCount,
			guiTileCacheMissCount, guiTileCacheEvictionCount, guiMaxTileCacheSize );
	}

	// Allocate entries
	if ( gpTileCache != NULL )
	{
		// Loop through and delete any entries
		for ( cnt = 0; cnt < guiMaxTileCacheSize; cnt++ )
		{
			if ( gpTileCache[ cnt ].pImagery != NULL )
			{
				DeleteTileSurface( gpTileCache[ cnt ].pImagery );
			}
		}
		MemFree( gpTileCache );
		gpTileCache = NULL;
	}

	if ( gpTileCacheStructInfo != NULL )
	{
		MemFree( gpTileCacheStructInfo );
		gpTileCacheStructInfo = NULL;
	}

	guiCurTileCacheSize = 0;
	guiNumTileCacheStructs = 0;
	giDefaultStructIndex = -1;
	guiTileCacheResidentBytes = 0;
	guiTileCachePeakBytes = 0;
}

INT16 FindCacheStructDataIndex( STR8 cFilename )
{
	UINT32 cnt;

	for ( cnt = 0; cnt < guiNumTileCacheStructs; cnt++ )
	{
		if ( _stricmp( gpTileCacheStructInfo[ cnt ].zRootName, cFilename ) == 0 )
		{
			return(	(INT16)cnt );
		}
	}

	return( -1 );
}

INT32 GetCachedTile( const STR8 cFilename )
{
	UINT32 cnt;

	// Reuse resident imagery even if its active reference count dropped to zero.
	for ( cnt = 0; cnt < guiCurTileCacheSize; cnt++ )
	{
		if ( gpTileCache[ cnt ].pImagery != NULL &&
			 _stricmp( gpTileCache[ cnt ].zName, cFilename ) == 0 )
		{
			if ( gpTileCache[ cnt ].sHits < 32767 )
				gpTileCache[ cnt ].sHits++;
			TouchTileCacheEntry( cnt );
			++guiTileCacheHitCount;
			return (INT32)cnt;
		}
	}

	++guiTileCacheMissCount;

	INT32 iSlot = -1;
	for ( cnt = 0; cnt < guiMaxTileCacheSize; cnt++ )
	{
		if ( gpTileCache[ cnt ].pImagery == NULL )
		{
			iSlot = (INT32)cnt;
			break;
		}
	}

	// Prefer a genuinely unused LRU entry. Never discard an actively referenced
	// tile merely because the legacy 50-slot table is full.
	if ( iSlot < 0 )
	{
		iSlot = FindLRUUnreferencedTile( -1 );
		if ( iSlot >= 0 )
			EvictTileCacheEntry( (UINT32)iSlot, "slot-pressure" );
		else if ( GrowTileCache( ) )
		{
			iSlot = (INT32)cnt; // cnt is the old size; first newly allocated slot.
		}
		else
		{
			BlackBoxEvent( "VHD_CACHE",
				"load refused: all %u slots are actively referenced file=%s resident=%u",
				guiMaxTileCacheSize, cFilename, guiTileCacheResidentBytes );
			return -1;
		}
	}

	const UINT32 uiSlot = (UINT32)iSlot;
	gpTileCache[ uiSlot ].pImagery = LoadTileSurface( cFilename );
	if ( gpTileCache[ uiSlot ].pImagery == NULL )
		return -1;

	strcpy( gpTileCache[ uiSlot ].zName, cFilename );
	gpTileCache[ uiSlot ].sHits = 1;
	GetRootName( gpTileCache[ uiSlot ].zRootName, cFilename );
	gpTileCache[ uiSlot ].sStructRefID = FindCacheStructDataIndex( gpTileCache[ uiSlot ].zRootName );

	if ( gpTileCache[ uiSlot ].sStructRefID != -1 )
	{
		AddZStripInfoToVObject( gpTileCache[ uiSlot ].pImagery->vo,
			gpTileCacheStructInfo[ gpTileCache[ uiSlot ].sStructRefID ].pStructureFileRef, TRUE, 0 );
	}

	if ( gpTileCache[ uiSlot ].pImagery->pAuxData != NULL )
		gpTileCache[ uiSlot ].ubNumFrames = gpTileCache[ uiSlot ].pImagery->pAuxData->ubNumberOfFrames;
	else
		gpTileCache[ uiSlot ].ubNumFrames = 1;

	gpTileCache[ uiSlot ].uiResidentBytes = EstimateTileImageryResidentBytes( gpTileCache[ uiSlot ].pImagery );
	guiTileCacheResidentBytes = TileCacheSaturatingAdd(
		guiTileCacheResidentBytes, gpTileCache[ uiSlot ].uiResidentBytes );
	guiTileCachePeakBytes = __max( guiTileCachePeakBytes, guiTileCacheResidentBytes );
	TouchTileCacheEntry( uiSlot );

	if ( uiSlot >= guiCurTileCacheSize )
		guiCurTileCacheSize = uiSlot + 1;

	if ( TileCacheDiagnosticsEnabled( ) )
	{
		BlackBoxEvent( "VHD_CACHE",
			"load index=%u file=%s bytes=%u resident=%u peak=%u hits=%u misses=%u scale=%u",
			uiSlot, cFilename, gpTileCache[ uiSlot ].uiResidentBytes,
			guiTileCacheResidentBytes, guiTileCachePeakBytes, guiTileCacheHitCount,
			guiTileCacheMissCount, gpTileCache[ uiSlot ].pImagery->vo != NULL ?
			gpTileCache[ uiSlot ].pImagery->vo->ubVHDAssetScale : 1 );
	}

	TrimTileCacheToBudget( (INT32)uiSlot );
	return (INT32)uiSlot;
}

BOOLEAN RemoveCachedTile( INT32 iCachedTile )
{
	if ( iCachedTile < 0 || (UINT32)iCachedTile >= guiCurTileCacheSize )
		return FALSE;

	TILE_CACHE_ELEMENT *pEntry = &gpTileCache[ iCachedTile ];
	if ( pEntry->pImagery == NULL )
		return FALSE;

	if ( pEntry->sHits > 0 )
		pEntry->sHits--;
	TouchTileCacheEntry( (UINT32)iCachedTile );

	// A zero reference count now means reusable, not immediately destroyed. This
	// turns the old reference table into a real cache and lets short-lived animation
	// tiles avoid repeated HD decode/scale work. Byte pressure still trims promptly.
	if ( pEntry->sHits == 0 )
		TrimTileCacheToBudget( -1 );

	return TRUE;
}

BOOLEAN RunVHDTileCacheSelfTest( const STR8 cFilename )
{
	const CHAR8 *pEnabled = getenv( "VR_VHD_TILE_CACHE_TEST" );
	if ( pEnabled == NULL || strcmp( pEnabled, "1" ) != 0 )
		return TRUE;

	if ( cFilename == NULL )
	{
		BlackBoxEvent( "VHD_CACHE", "self-test missing fixture path" );
		return FALSE;
	}

	BlackBoxEvent( "VHD_CACHE", "self-test begin file=%s", cFilename );

	const INT32 iFirst = GetCachedTile( cFilename );
	if ( iFirst < 0 )
	{
		BlackBoxEvent( "VHD_CACHE", "self-test failed initial load file=%s", cFilename );
		return FALSE;
	}

	const INT32 iSecond = GetCachedTile( cFilename );
	if ( iSecond != iFirst || gpTileCache[ iFirst ].sHits < 2 )
	{
		BlackBoxEvent( "VHD_CACHE",
			"self-test failed resident hit first=%d second=%d refs=%d",
			iFirst, iSecond, gpTileCache[ iFirst ].sHits );
		return FALSE;
	}

	if ( !RemoveCachedTile( iFirst ) || !RemoveCachedTile( iSecond ) ||
		 gpTileCache[ iFirst ].pImagery == NULL || gpTileCache[ iFirst ].sHits != 0 )
	{
		BlackBoxEvent( "VHD_CACHE",
			"self-test failed zero-ref residency index=%d refs=%d resident=%d",
			iFirst, gpTileCache[ iFirst ].sHits, gpTileCache[ iFirst ].pImagery != NULL );
		return FALSE;
	}

	const INT32 iReused = GetCachedTile( cFilename );
	if ( iReused != iFirst || gpTileCache[ iFirst ].sHits != 1 )
	{
		BlackBoxEvent( "VHD_CACHE",
			"self-test failed LRU reuse first=%d reused=%d refs=%d",
			iFirst, iReused, gpTileCache[ iFirst ].sHits );
		return FALSE;
	}

	if ( !RemoveCachedTile( iReused ) || gpTileCache[ iFirst ].sHits != 0 )
	{
		BlackBoxEvent( "VHD_CACHE", "self-test failed final release index=%d refs=%d",
			iFirst, gpTileCache[ iFirst ].sHits );
		return FALSE;
	}

	const UINT32 uiSavedBudget = guiTileCacheBudgetBytes;
	guiTileCacheBudgetBytes = 0;
	TrimTileCacheToBudget( -1 );
	guiTileCacheBudgetBytes = uiSavedBudget;

	if ( (UINT32)iFirst < guiMaxTileCacheSize && gpTileCache[ iFirst ].pImagery != NULL )
	{
		BlackBoxEvent( "VHD_CACHE", "self-test failed byte-budget eviction index=%d", iFirst );
		return FALSE;
	}

	FILE *pMarker = fopen( "vhd-tile-cache-selftest.ok", "w" );
	if ( pMarker == NULL )
	{
		BlackBoxEvent( "VHD_CACHE", "self-test could not write marker" );
		return FALSE;
	}
	fprintf( pMarker, "pass file=%s hits=%u misses=%u evictions=%u peak=%u\n",
		cFilename, guiTileCacheHitCount, guiTileCacheMissCount,
		guiTileCacheEvictionCount, guiTileCachePeakBytes );
	fclose( pMarker );

	BlackBoxEvent( "VHD_CACHE",
		"self-test passed file=%s hits=%u misses=%u evictions=%u peak=%u",
		cFilename, guiTileCacheHitCount, guiTileCacheMissCount,
		guiTileCacheEvictionCount, guiTileCachePeakBytes );
	return TRUE;
}

HVOBJECT GetCachedTileVideoObject( INT32 iIndex )
{
	if ( iIndex == -1 )
	{
		return( NULL );
	}

	if ( gpTileCache[ iIndex ].pImagery == NULL )
	{
		return( NULL );
	}

	return( gpTileCache[ iIndex ].pImagery->vo );
}


STRUCTURE_FILE_REF *GetCachedTileStructureRef( INT32 iIndex )
{
	if ( iIndex == -1 )
	{
		return( NULL );
	}

	if ( gpTileCache[ iIndex ].sStructRefID == -1 )
	{
		return( NULL );
	}

	return( gpTileCacheStructInfo[ gpTileCache[ iIndex ].sStructRefID ].pStructureFileRef );
}


STRUCTURE_FILE_REF *GetCachedTileStructureRefFromFilename( const STR8 cFilename )
{
	INT16 sStructDataIndex;

	// Given filename, look for index
	sStructDataIndex = FindCacheStructDataIndex( cFilename );

	if ( sStructDataIndex == -1 )
	{
		return( NULL );
	}

	return( gpTileCacheStructInfo[ sStructDataIndex ].pStructureFileRef );
}


void CheckForAndAddTileCacheStructInfo( LEVELNODE *pNode, INT32 sGridNo, UINT16 usIndex, UINT16 usSubIndex )
{
	STRUCTURE_FILE_REF *pStructureFileRef;

	pStructureFileRef = GetCachedTileStructureRef( usIndex );

	if ( pStructureFileRef != NULL)
	{
		if ( !AddStructureToWorld( sGridNo, 0, &( pStructureFileRef->pDBStructureRef[ usSubIndex ] ), pNode ) )
	{
		if ( giDefaultStructIndex != -1 )
		{
		pStructureFileRef = gpTileCacheStructInfo[ giDefaultStructIndex ].pStructureFileRef;

		if ( pStructureFileRef != NULL)
		{
			AddStructureToWorld( sGridNo, 0, &( pStructureFileRef->pDBStructureRef[ usSubIndex ] ), pNode );
		}
		}
	}
	}
}

void CheckForAndDeleteTileCacheStructInfo( LEVELNODE *pNode, UINT16 usIndex )
{
	STRUCTURE_FILE_REF *pStructureFileRef;

	if ( usIndex >= TILE_CACHE_START_INDEX )
	{
		pStructureFileRef = GetCachedTileStructureRef( ( usIndex - TILE_CACHE_START_INDEX ) );

		if ( pStructureFileRef != NULL)
		{
			DeleteStructureFromWorld( pNode->pStructureData );
		}
	}
}

void GetRootName( STR8 pDestStr, const STR8 pSrcStr )
{
	// Remove path and extension
	CHAR8		cTempFilename[ 120 ];
	STR			cEndOfName;

	// Remove path
	strcpy( cTempFilename, pSrcStr );
	cEndOfName = strrchr( cTempFilename, '\\' );
	if (cEndOfName != NULL)
	{
		cEndOfName++;
		strcpy( pDestStr, cEndOfName );
	}
	else
	{
		strcpy( pDestStr, cTempFilename );
	}

	// Now remove extension...
	cEndOfName = strchr( pDestStr, '.' );
	if (cEndOfName != NULL)
	{
		*cEndOfName = '\0';
	}

}


