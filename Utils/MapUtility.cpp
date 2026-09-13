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
		if(strcmp(gzCommandLine, "-DOMAPSCNV") == 0)
			if(!(dMajorMapVersion == MAJOR_MAP_VERSION && ubMinorMapVersion == MINOR_MAP_VERSION && gMapInformation.ubMapVersion == MINOR_MAP_VERSION))
				if(!SaveWorld(zFilename))
					return(ERROR_SCREEN);
		LightReset();
		LightSpriteRenderAll();
	}
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

	// MAPSHOT is a single-purpose automation path. Do not spend another pass
	// generating/quantizing the tiny radar STI; stop immediately after the
	// full engine overview is on disk. The workflow validates the file itself.
	if ( gfMapPreviewCaptureMode )
	{
		TrashOverheadMap();
		FListNode = NULL;
		gfProgramIsRunning = FALSE;
		MapPreviewWriteStatus( fPreviewSaved ? "DONE success" : "DONE failure" );
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
