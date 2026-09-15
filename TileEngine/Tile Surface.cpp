#include "builddefines.h"

#ifdef PRECOMPILEDHEADERS
	#include "TileEngine All.h"
#else
	#include "worlddef.h"
	#include "worlddat.h"
	#include <stdio.h>
	#include <string.h>
	#include "stdlib.h"
	#include "time.h"
	#include "video.h"
	#include "debug.h"
	#include "sys globals.h"
	#include "tiledat.h"
	#include "Fileman.h"
#endif

#include "ExceptionHandling.h"
#include "Isometric Utils.h"
#include "GameSettings.h"


TILE_IMAGERY				*gTileSurfaceArray[ NUMBEROFTILETYPES ];
UINT8								gbDefaultSurfaceUsed[ NUMBEROFTILETYPES ];
UINT8								gbSameAsDefaultSurfaceUsed[ NUMBEROFTILETYPES ];

static BOOLEAN IsSanMonaC5TilePath( const STR8 pFilename )
{
	return pFilename != NULL &&
		( strstr( pFilename, "TILESETS\\18\\" ) != NULL || strstr( pFilename, "tilesets\\18\\" ) != NULL );
}

static void TraceSanMonaC5VisualAsset( const STR8 pStage, const STR8 pFilename, UINT16 usObjects, UINT8 ubBitDepth, const STR8 pJsd, UINT16 usStructures )
{
	FILE *pTrace = fopen( "C5_visual_load.log", "a" );
	if ( pTrace == NULL ) return;
	fprintf( pTrace, "%s file=%s objects=%u bitDepth=%u jsd=%s structures=%u\n",
		pStage != NULL ? pStage : "", pFilename != NULL ? pFilename : "", usObjects,
		ubBitDepth, pJsd != NULL ? pJsd : "", usStructures );
	fclose( pTrace );
}

TILE_IMAGERY *LoadTileSurface(	STR8	cFilename )
{
	// Add tile surface
	PTILE_IMAGERY	pTileSurf = NULL;
	SGPFILENAME cVisualFilename;
	strncpy( cVisualFilename, cFilename, sizeof(cVisualFilename) - 1 );
	cVisualFilename[ sizeof(cVisualFilename) - 1 ] = 0;
	BOOLEAN fC5VisualOverride = FALSE;
	if ( IsSanMonaC5GraphicsOnlyProfile() && cFilename != NULL )
	{
		const CHAR8 *pLeaf = strrchr( cFilename, '\\' );
		pLeaf = ( pLeaf != NULL ) ? pLeaf + 1 : cFilename;

		// C5 may visually override inherited GENERIC-1 art without changing the
		// logical STI/JSD identity.  Missing C5 assets fall back automatically to
		// cFilename below, so the list can be populated incrementally and safely.
		struct C5_VISUAL_ALIAS
		{
			const CHAR8 *pOriginal;
			const CHAR8 *pVisual;
		};
		static const C5_VISUAL_ALIAS gC5VisualAliases[] =
		{
			{ "SGRASS1.STI",  "TILESETS\\18\\C5_SGRASS1.STI" },
			{ "BUILD_01.STI", "TILESETS\\18\\C5_BUILD_01.STI" },
			{ "BUILD_21.STI", "TILESETS\\18\\C5_BUILD_21.STI" },
			{ "BUILD_06.STI", "TILESETS\\18\\C5_BUILD_06.STI" },
			{ "FLAT_R1.STI",  "TILESETS\\18\\C5_FLAT_R1.STI" },
			{ "FLAT_R2.STI",  "TILESETS\\18\\C5_FLAT_R2.STI" },
			{ "FLAT_R3.STI",  "TILESETS\\18\\C5_FLAT_R3.STI" },
		};

		for ( UINT32 uiAlias = 0; uiAlias < sizeof(gC5VisualAliases) / sizeof(gC5VisualAliases[0]); ++uiAlias )
		{
			if ( _stricmp( pLeaf, gC5VisualAliases[uiAlias].pOriginal ) == 0 )
			{
				strcpy( cVisualFilename, gC5VisualAliases[uiAlias].pVisual );
				fC5VisualOverride = TRUE;
				break;
			}
		}
	}
	const BOOLEAN fTraceB1Asset = ( cFilename != NULL && strstr( cFilename, "B1_" ) != NULL );
	const BOOLEAN fTraceC5Asset = IsSanMonaC5TilePath( cFilename ) || fC5VisualOverride;
	if ( fTraceB1Asset )
		TraceB1RemasterLoad( "TILE LOAD BEGIN", cFilename );
	VOBJECT_DESC	VObjectDesc;
	HVOBJECT		hVObject;
	HIMAGE				hImage;
	SGPFILENAME						cStructureFilename;
	STR										cEndOfName;
	STRUCTURE_FILE_REF *	pStructureFileRef;
	BOOLEAN								fOk;
	UINT8 ubLoadedVHDScale = 1;
	BOOLEAN fLoadedNativeVHD = FALSE;

	// Vengeance HD overlay.  Keep map/JSD filenames untouched and only replace
	// the visual package.  A 2x game looks under VHD2\..., a 4x game under
	// VHD4\..., first for a multi-frame JPC package and then for a single PNG.
	// Missing HD art falls through to the normal VFS/legacy path.
	hImage = NULL;
	const UINT8 ubRequestedVHDScale = GetVHDRenderScale();
	if ( gGameExternalOptions.fVHDPreferNativeAssets &&
		 ( ubRequestedVHDScale == 2 || ubRequestedVHDScale == 4 ) )
	{
		CHAR8 cVHDVisualFilename[512];
		const int iVHDNameLen = sprintf( cVHDVisualFilename, "VHD%u\\%s",
			(unsigned int)ubRequestedVHDScale, cVisualFilename );
		if ( iVHDNameLen > 0 && iVHDNameLen < (int)sizeof(cVHDVisualFilename) )
		{
			hImage = CreateImage( cVHDVisualFilename, IMAGE_ALLDATA, ImageFileType::JPC );
			if ( hImage == NULL )
				hImage = CreateImage( cVHDVisualFilename, IMAGE_ALLDATA, ImageFileType::PNG );
			if ( hImage != NULL )
			{
				ubLoadedVHDScale = ubRequestedVHDScale;
				fLoadedNativeVHD = TRUE;
			}
		}
	}

	if ( hImage == NULL )
		hImage = CreateImage( cVisualFilename, IMAGE_ALLDATA );
	if ( hImage == NULL && fC5VisualOverride )
	{
		TraceSanMonaC5VisualAsset( "PIXEL_OVERRIDE_FALLBACK", cVisualFilename, 0, 0, cFilename, 0 );
		hImage = CreateImage( cFilename, IMAGE_ALLDATA );
		fC5VisualOverride = FALSE;
	}

	// If no native VHD package exists, enlarge the loaded legacy imagery once.
	// Legacy indexed ETRLE stays indexed/compressed. The VHD renderer's C++ multi-Z
	// fallback maps scaled source pixels back to authored/JSD strip coordinates.
	if ( hImage != NULL && ubLoadedVHDScale == 1 &&
		 ( ubRequestedVHDScale == 2 || ubRequestedVHDScale == 4 ) )
	{
		if ( ScaleImageNearestForVHD( hImage, ubRequestedVHDScale ) )
		{
			ubLoadedVHDScale = ubRequestedVHDScale;
		}
		else
		{
			BlackBoxEvent( "VHD", "fallback scale failed file=%s scale=%u",
				cFilename != NULL ? cFilename : "(null)", ubRequestedVHDScale );
		}
	}

	if (hImage == NULL)
	{
		if ( fTraceB1Asset )
			TraceB1RemasterLoad( "CREATE IMAGE FAILED", cFilename );
		BlackBoxEvent( "ASSET", "tile image load failed file=%s", cFilename != NULL ? cFilename : "(null)" );
		// Report error
		SET_ERROR( "Could not load tile file: %s", cFilename );
		return( NULL );
	}
	if ( fTraceC5Asset )
		TraceSanMonaC5VisualAsset( fLoadedNativeVHD ? "IMAGE_VHD_NATIVE" :
			( ubLoadedVHDScale > 1 ? "IMAGE_VHD_FALLBACK" :
			  ( hImage->ubBitDepth == 32 ? "IMAGE_TRUECOLOR" : "IMAGE_LEGACY" ) ),
			hImage->ImageFile, hImage->usNumberOfObjects, hImage->ubBitDepth, "", 0 );
	if ( fTraceB1Asset )
	{
		TraceB1RemasterLoad( "CREATE IMAGE OK", cFilename );
		CHAR8 zB1ImageSource[256];
		sprintf( zB1ImageSource, "requested=%s resolved=%s bitDepth=%u objects=%u",
			cFilename,
			hImage->ImageFile,
			hImage->ubBitDepth,
			hImage->usNumberOfObjects );
		TraceB1RemasterLoad( hImage->ubBitDepth == 32 ? "TRUECOLOR ACTIVE" : "LEGACY IMAGE ACTIVE", zB1ImageSource );
	}

	VObjectDesc.fCreateFlags = VOBJECT_CREATE_FROMHIMAGE;
	VObjectDesc.hImage = hImage;

	hVObject = CreateVideoObject( &VObjectDesc );

	if ( hVObject != NULL )
		hVObject->ubVHDAssetScale = ubLoadedVHDScale;

	if ( hVObject == NULL )
	{
		if ( fTraceB1Asset )
			TraceB1RemasterLoad( "CREATE VIDEO OBJECT FAILED", cFilename );
		// Report error
		SET_ERROR( "Could not load tile file: %s", cFilename );
		// Video Object will set error conition.]
		DestroyImage( hImage );
		return( NULL );
	}
	if ( fTraceB1Asset )
	{
		CHAR8 zB1VideoInfo[192];
		sprintf( zB1VideoInfo, "%s objects=%u bitDepth=%u", cFilename,
			hVObject->usNumberOfObjects, hVObject->ubBitDepth );
		TraceB1RemasterLoad( "CREATE VIDEO OBJECT OK", zB1VideoInfo );
	}

	// Load structure data, if any.
	// Start by hacking the image filename into that for the structure data
	strcpy( cStructureFilename, cFilename );
	cEndOfName = strchr( cStructureFilename, '.' );
	if (cEndOfName != NULL)
	{
		cEndOfName++;
		*cEndOfName = '\0';
	}
	else
	{
		strcat( cStructureFilename, "." );
	}
	strcat( cStructureFilename, STRUCTURE_FILE_EXTENSION );
	const BOOLEAN fStructureExists = FileExists( cStructureFilename );
	if ( fTraceC5Asset )
		TraceSanMonaC5VisualAsset( fStructureExists ? "JSD_FOUND" : "JSD_NOT_PRESENT", cFilename, hVObject->usNumberOfObjects, hVObject->ubBitDepth, cStructureFilename, 0 );
	if ( fTraceB1Asset )
		TraceB1RemasterLoad( fStructureExists ? "JSD FOUND" : "JSD NOT PRESENT", cStructureFilename );
	if ( fStructureExists )
	{
		pStructureFileRef = LoadStructureFile( cStructureFilename );
		if (pStructureFileRef == NULL || hVObject->usNumberOfObjects != pStructureFileRef->usNumberOfStructures)
		{
			if ( fTraceC5Asset )
				TraceSanMonaC5VisualAsset( "JSD_COUNT_FAILED", cFilename, hVObject->usNumberOfObjects, hVObject->ubBitDepth, cStructureFilename, pStructureFileRef != NULL ? pStructureFileRef->usNumberOfStructures : 0 );
			if ( fTraceB1Asset )
			{
				CHAR8 zB1StructError[224];
				sprintf( zB1StructError, "%s imageObjects=%u structures=%u ref=%s",
					cStructureFilename,
					hVObject->usNumberOfObjects,
					pStructureFileRef != NULL ? pStructureFileRef->usNumberOfStructures : 0,
					pStructureFileRef != NULL ? "ok" : "null" );
				TraceB1RemasterLoad( "JSD LOAD/COUNT FAILED", zB1StructError );
			}
			DestroyImage( hImage );
			DeleteVideoObject( hVObject );
			SET_ERROR(	"Structure file error: %s", cStructureFilename );
			return( NULL );
		}

		if ( fTraceC5Asset )
			TraceSanMonaC5VisualAsset( "JSD_OK", cFilename, hVObject->usNumberOfObjects, hVObject->ubBitDepth, cStructureFilename, pStructureFileRef->usNumberOfStructures );
		if ( fTraceB1Asset )
			TraceB1RemasterLoad( "JSD LOAD OK", cStructureFilename );
		DebugMsg( TOPIC_JA2, DBG_LEVEL_3, cStructureFilename );

		fOk = AddZStripInfoToVObject( hVObject, pStructureFileRef, FALSE, 0 );
		if (fOk == FALSE)
		{
			if ( fTraceB1Asset )
				TraceB1RemasterLoad( "ZSTRIP FAILED", cStructureFilename );
			DestroyImage( hImage );
			DeleteVideoObject( hVObject );
			SET_ERROR(	"ZStrip creation error: %s", cStructureFilename );
			return( NULL );
		}
		if ( fTraceB1Asset )
			TraceB1RemasterLoad( "ZSTRIP OK", cStructureFilename );

	}
	else
	{
		pStructureFileRef = NULL;
	}

	pTileSurf = (PTILE_IMAGERY) MemAlloc( sizeof( TILE_IMAGERY ) );

	// Set all values to zero
	memset( pTileSurf, 0, sizeof( TILE_IMAGERY ) );

	pTileSurf->vo									= hVObject;
	pTileSurf->pStructureFileRef	= pStructureFileRef;

	if (pStructureFileRef && pStructureFileRef->pAuxData != NULL)
	{
		pTileSurf->pAuxData = pStructureFileRef->pAuxData;
		pTileSurf->pTileLocData = pStructureFileRef->pTileLocData;
	}
	else if (hImage->uiAppDataSize == hVObject->usNumberOfObjects * sizeof( AuxObjectData ))
	{
		// Valid auxiliary data, so make a copy of it for TileSurf
		pTileSurf->pAuxData = (AuxObjectData *) MemAlloc( hImage->uiAppDataSize );
		if ( pTileSurf->pAuxData == NULL)
		{
			DestroyImage( hImage );
			DeleteVideoObject( hVObject );
			return( NULL );
		}
		memcpy( pTileSurf->pAuxData, hImage->pAppData, hImage->uiAppDataSize );
	}
	else
	{
		pTileSurf->pAuxData = NULL;
	}
	// the hImage is no longer needed
	DestroyImage( hImage );

	if ( fTraceB1Asset )
		TraceB1RemasterLoad( "TILE LOAD COMPLETE", cFilename );

	return( pTileSurf );
}


void DeleteTileSurface( PTILE_IMAGERY	pTileSurf )
{
	if ( pTileSurf->pStructureFileRef != NULL )
	{
		FreeStructureFile( pTileSurf->pStructureFileRef );
	}
	else
	{
		// If a structure file exists, it will free the auxdata.
		// Since there is no structure file in this instance, we
		// free it ourselves.
		if (pTileSurf->pAuxData != NULL)
		{
			MemFree( pTileSurf->pAuxData );
		}
	}

	DeleteVideoObject( pTileSurf->vo );
	MemFree( pTileSurf );
}


extern void GetRootName( STR8 pDestStr, const STR8 pSrcStr );


void SetRaisedObjectFlag( STR8 cFilename, TILE_IMAGERY *pTileSurf )
{
	INT32 cnt = 0;
	CHAR8	cRootFile[ 128 ];
	CHAR8 ubRaisedObjectFiles[][80] =
	{
		"bones",
		"bones2",
		"grass2",
		"grass3",
		"l_weed3",
		"litter",
		"miniweed",
		"sblast",
		"sweeds",
		"twigs",
		"wing",
		"1"
	};

	// Loop through array of RAISED objecttype imagery and
	// set global value...
	if ( ( pTileSurf->fType >= DEBRISWOOD && pTileSurf->fType <= DEBRISWEEDS ) || pTileSurf->fType == DEBRIS2MISC || pTileSurf->fType == ANOTHERDEBRIS )
	{
		GetRootName( cRootFile, cFilename );
		while( ubRaisedObjectFiles[ cnt ][ 0 ] != '1' )
		{
			if ( _stricmp( ubRaisedObjectFiles[ cnt ], cRootFile ) == 0 )
			{
				pTileSurf->bRaisedObjectType = TRUE;
			}

			cnt++;
		}
	}
}
