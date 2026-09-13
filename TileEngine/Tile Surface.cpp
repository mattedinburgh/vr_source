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


TILE_IMAGERY				*gTileSurfaceArray[ NUMBEROFTILETYPES ];
UINT8								gbDefaultSurfaceUsed[ NUMBEROFTILETYPES ];
UINT8								gbSameAsDefaultSurfaceUsed[ NUMBEROFTILETYPES ];

TILE_IMAGERY *LoadTileSurface(	STR8	cFilename )
{
	// Add tile surface
	PTILE_IMAGERY	pTileSurf = NULL;
	const BOOLEAN fTraceB1Asset = ( cFilename != NULL && strstr( cFilename, "B1_" ) != NULL );
	if ( fTraceB1Asset )
		TraceB1RemasterLoad( "TILE LOAD BEGIN", cFilename );
	VOBJECT_DESC	VObjectDesc;
	HVOBJECT		hVObject;
	HIMAGE				hImage;
	SGPFILENAME						cStructureFilename;
	STR										cEndOfName;
	STRUCTURE_FILE_REF *	pStructureFileRef;
	BOOLEAN								fOk;


	hImage = CreateImage( cFilename, IMAGE_ALLDATA );
	if (hImage == NULL)
	{
		if ( fTraceB1Asset )
			TraceB1RemasterLoad( "CREATE IMAGE FAILED", cFilename );
		BlackBoxEvent( "ASSET", "tile image load failed file=%s", cFilename != NULL ? cFilename : "(null)" );
		// Report error
		SET_ERROR( "Could not load tile file: %s", cFilename );
		return( NULL );
	}
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
	if ( fTraceB1Asset )
		TraceB1RemasterLoad( fStructureExists ? "JSD FOUND" : "JSD NOT PRESENT", cStructureFilename );
	if ( fStructureExists )
	{
		pStructureFileRef = LoadStructureFile( cStructureFilename );
		if (pStructureFileRef == NULL || hVObject->usNumberOfObjects != pStructureFileRef->usNumberOfStructures)
		{
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
