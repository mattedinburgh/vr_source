#ifdef PRECOMPILEDHEADERS
	#include "Utils All.h"
	#include "Game Clock.h"
#else
	#include "sgp.h"
	#include "font.h"
	#include "types.h"
	#include "Font Control.h"
	#include "message.h"
	#include "memory.h"
	#include "mbstring.h"
	#include "Timer Control.h"
	#include "render dirty.h"
	#include "renderworld.h"
	#include "Mutex Manager.h"
	#include "local.h"
	#include "interface.h"
	#include "Map Screen Interface Bottom.h"
	#include "WordWrap.h"
	#include "Sound Control.h"
	#include "Soundman.h"
	#include "BuildDefines.h"
	#include "Dialogue Control.h"
	#include <stdio.h>
	#include "Game Clock.h"
	#include "GameSettings.h"
	#include "sgp_logger.h"
#endif

#include "strategicmap.h"
#include "weapons.h"
#include "overhead.h"
#include "mousesystem.h"
#include "Cursors.h"
#include "Isometric Utils.h"
#include "Animation Control.h"
#include "Text.h"

typedef struct
{
	UINT32	uiFont;
	UINT32	uiTimeOfLastUpdate;
	UINT32	uiFlags;
	UINT32	uiPadding[ 3 ];
	UINT16	usColor;
	BOOLEAN fBeginningOfNewString;

} StringSaveStruct;


// WANNE: These lines defines the message position in tactical screen.
#define MAX_LINE_COUNT 6
#define X_START 2
#define MAX_AGE 10000
// HEADROCK HAM 3.6: Turned into a variable.
UINT16 LINE_WIDTH;
UINT16 MAP_LINE_WIDTH;
#define WIDTH_BETWEEN_NEW_STRINGS 5

#define BETAVERSION_COLOR FONT_ORANGE
#define TESTVERSION_COLOR FONT_GREEN
#define DEBUG_COLOR FONT_RED
#define DIALOGUE_COLOR FONT_WHITE
#define INTERFACE_COLOR FONT_YELLOW

#define MAP_SCREEN_MESSAGE_FONT TINYFONT1

UINT8 gubStartOfMapScreenMessageList = 0;
UINT8 gubEndOfMapScreenMessageList = 0;

// index of the current string we are looking at
UINT8 gubCurrentMapMessageString = 0;

// temp position for display of marker
//UINT8 ubTempPosition = 0;

// are allowed to beep on message scroll?
BOOLEAN fOkToBeepNewMessage = TRUE;


// HEADROCK HAM 3.6: Increased size of absolute MAXIMUM possible displayed messages to 36 (the most that can be seen
// in a 1024x768 display. There's now an INI setting that allows displaying up to 36 in the tactical screen. 
// Strategic screen not yet changed. Please note that there are also fail-safes that make sure that the actual
// maximum isn't more that can be displayed given the resolution being used.
//static ScrollStringStPtr	gpDisplayList[ MAX_LINE_COUNT ];
static ScrollStringStPtr	gpDisplayList[ 36 ];
static ScrollStringStPtr gMapScreenMessageList[ 256 ];
extern ScrollStringStPtr pStringS=NULL;

// first time adding any message to the message dialogue system
BOOLEAN fFirstTimeInMessageSystem = TRUE;
BOOLEAN fDisableJustForIan = FALSE;

BOOLEAN fScrollMessagesHidden = FALSE;
UINT32 uiStartOfPauseTime = 0;

// test extern functions
BOOLEAN RestoreExternBackgroundRectGivenID( INT32 iBack );
extern VIDEO_OVERLAY	gVideoOverlays[];

extern UINT16 gusSubtitleBoxWidth;
extern BOOLEAN gfFacePanelActive;

// region created and due to last quote box
extern BOOLEAN fTextBoxMouseRegionCreated;
extern BOOLEAN fDialogueBoxDueToLastMessage;

// OJW - 20090426
INT16 gTacticalMsgFilterPriority = -1; // filter for writing messages to the tactical log



// prototypes

BOOLEAN CreateStringVideoOverlay( ScrollStringStPtr pStringSt, UINT16 usX, UINT16 usY );
void SetStringVideoOverlayPosition(	ScrollStringStPtr pStringSt, UINT16 usX, UINT16 usY );

void BlitString( VIDEO_OVERLAY *pBlitter );
void RemoveStringVideoOverlay( ScrollStringStPtr pStringSt );
void EnableStringVideoOverlay( ScrollStringStPtr pStringSt, BOOLEAN fEnable );

ScrollStringStPtr GetNextString(ScrollStringStPtr pStringSt);
ScrollStringStPtr GetPrevString(ScrollStringStPtr pStringSt);
void AlignString(ScrollStringStPtr pPermStringSt);

INT32 GetMessageQueueSize( void );

ScrollStringStPtr AddString(STR16 string, UINT16 usColor, UINT32 uiFont, BOOLEAN fStartOfNewString, UINT8 ubPriority );
void SetString(ScrollStringStPtr pStringSt, STR16 String);

void SetStringPosition(ScrollStringStPtr pStringSt, UINT16 x, UINT16 y);
void SetStringColor(ScrollStringStPtr pStringSt, UINT16 color);
ScrollStringStPtr SetStringNext(ScrollStringStPtr pStringSt, ScrollStringStPtr pNext);
ScrollStringStPtr SetStringPrev(ScrollStringStPtr pStringSt, ScrollStringStPtr pPrev);
void AddStringToMapScreenMessageList( STR16 pString, UINT16 usColor, UINT32 uiFont, BOOLEAN fStartOfNewString, UINT8 ubPriority );


// clear up a linked list of wrapped strings
void ClearWrappedStrings( WRAPPED_STRING *pStringWrapperHead );
void WriteMessageToFile( const STR16 pString );

// tactical screen message
void TacticalScreenMsg( UINT16 usColor, UINT8 ubPriority, STR16 pStringA, ... );

// play bee when new message is added
void PlayNewMessageSound( void );

void HandleLastQuotePopUpTimer( void );

// HEADROCK HAM 3.6: External boolean tells game to re-render map screen message log panel.
extern BOOLEAN fMapScreenBottomDirty;

// functions


void SetStringFont(ScrollStringStPtr pStringSt, UINT32 uiFont)
{
	pStringSt->uiFont=uiFont;
}

UINT32 GetStringFont(ScrollStringStPtr pStringSt)
{
	return pStringSt->uiFont;
}


ScrollStringStPtr AddString(STR16 pString, UINT16 usColor, UINT32 uiFont, BOOLEAN fStartOfNewString, UINT8 ubPriority )
{
	// add a new string to the list of strings
	ScrollStringStPtr pStringSt=NULL;
	pStringSt= (ScrollStringStPtr)MemAlloc(sizeof(ScrollStringSt));

	SetString(pStringSt, pString);
	SetStringColor(pStringSt, usColor);
	pStringSt->uiFont = uiFont;
	pStringSt->fBeginningOfNewString = fStartOfNewString;
	pStringSt->uiFlags = ubPriority;

	SetStringNext(pStringSt, NULL);
	SetStringPrev(pStringSt, NULL);
	pStringSt->iVideoOverlay=-1;

	// now add string to map screen strings
	//AddStringToMapScreenMessageList(pString, usColor, uiFont, fStartOfNewString, ubPriority );

	return (pStringSt);
}


void SetString(ScrollStringStPtr pStringSt, STR16 pString)
{
	// ARM: Why x2 + 4 ???
	pStringSt->pString16=(STR16)MemAlloc((wcslen(pString)+2)*sizeof(CHAR16));
	wcsncpy(pStringSt->pString16, pString, wcslen(pString));
	pStringSt->pString16[wcslen(pString)]=0;
}


void SetStringPosition(ScrollStringStPtr pStringSt, UINT16 usX, UINT16 usY)
{
	SetStringVideoOverlayPosition( pStringSt, usX, usY );
}



void SetStringColor(ScrollStringStPtr pStringSt, UINT16 usColor)
{
	pStringSt->usColor=usColor;
}

ScrollStringStPtr GetNextString(ScrollStringStPtr pStringSt)
{
	// returns pointer to next string line
	if (pStringSt==NULL)
		return NULL;
	else
		return pStringSt->pNext;
}


ScrollStringStPtr GetPrevString(ScrollStringStPtr pStringSt)
{
	// returns pointer to previous string line
	if (pStringSt==NULL)
		return NULL;
	else
		return pStringSt->pPrev;
}


ScrollStringStPtr SetStringNext(ScrollStringStPtr pStringSt, ScrollStringStPtr pNext)
{
	pStringSt->pNext=pNext;
	return pStringSt;
}


ScrollStringStPtr SetStringPrev(ScrollStringStPtr pStringSt, ScrollStringStPtr pPrev)
{
	pStringSt->pPrev=pPrev;
	return pStringSt;
}


BOOLEAN CreateStringVideoOverlay( ScrollStringStPtr pStringSt, UINT16 usX, UINT16 usY )
{
	VIDEO_OVERLAY_DESC		VideoOverlayDesc;

	// WDS - bug fix: VideoOverlayDesc must be initialized! - 07/16/2007
	memset( &VideoOverlayDesc, 0, sizeof( VIDEO_OVERLAY_DESC ) );

	// SET VIDEO OVERLAY
	VideoOverlayDesc.sLeft			= usX;
	VideoOverlayDesc.sTop				= usY;
	VideoOverlayDesc.uiFontID	= pStringSt->uiFont;
	VideoOverlayDesc.ubFontBack	= FONT_MCOLOR_BLACK ;
	VideoOverlayDesc.ubFontFore	= (unsigned char)pStringSt->usColor;
	VideoOverlayDesc.sX					= VideoOverlayDesc.sLeft;
	VideoOverlayDesc.sY					= VideoOverlayDesc.sTop;
	swprintf( VideoOverlayDesc.pzText, pStringSt->pString16 );
	VideoOverlayDesc.BltCallback = BlitString;
	pStringSt->iVideoOverlay =	RegisterVideoOverlay( ( VOVERLAY_DIRTYBYTEXT ), &VideoOverlayDesc );

	if ( pStringSt->iVideoOverlay == -1 )
	{
		return( FALSE );
	}

	return( TRUE );
}


void RemoveStringVideoOverlay( ScrollStringStPtr pStringSt )
{

	// error check, remove one not there
	if( pStringSt->iVideoOverlay == -1 )
	{
		return;
	}


	RemoveVideoOverlay( pStringSt->iVideoOverlay );
	pStringSt->iVideoOverlay=-1;
}


void SetStringVideoOverlayPosition(	ScrollStringStPtr pStringSt, UINT16 usX, UINT16 usY )
{
	VIDEO_OVERLAY_DESC		VideoOverlayDesc;

	memset( &VideoOverlayDesc, 0, sizeof( VideoOverlayDesc ) );

	// Donot update if not allocated!
	if ( pStringSt->iVideoOverlay != -1 )
	{
		VideoOverlayDesc.uiFlags	= VOVERLAY_DESC_POSITION;
		VideoOverlayDesc.sLeft			= usX;
		VideoOverlayDesc.sTop				= usY;
		VideoOverlayDesc.sX					= VideoOverlayDesc.sLeft;
		VideoOverlayDesc.sY					= VideoOverlayDesc.sTop;
		UpdateVideoOverlay( &VideoOverlayDesc, pStringSt->iVideoOverlay, FALSE );
	}
}


void BlitString( VIDEO_OVERLAY *pBlitter )
{
	UINT8	*pDestBuf;
	UINT32 uiDestPitchBYTES;

	//gprintfdirty(pBlitter->sX,pBlitter->sY, pBlitter->zText);
	//RestoreExternBackgroundRect(pBlitter->sX,pBlitter->sY, pBlitter->sX+StringPixLength(pBlitter->zText,pBlitter->uiFontID ), pBlitter->sY+GetFontHeight(pBlitter->uiFontID ));

	if( fScrollMessagesHidden == TRUE )
	{
		return;
	}


	pDestBuf = LockVideoSurface( pBlitter->uiDestBuff, &uiDestPitchBYTES);
	SetFont(pBlitter->uiFontID);

	SetFontBackground( pBlitter->ubFontBack );
	SetFontForeground( pBlitter->ubFontFore );
	SetFontShadow( DEFAULT_SHADOW );
	mprintf_buffer_coded( pDestBuf, uiDestPitchBYTES, pBlitter->uiFontID, pBlitter->sX, pBlitter->sY, pBlitter->zText );
	UnLockVideoSurface( pBlitter->uiDestBuff );

}


void EnableStringVideoOverlay( ScrollStringStPtr pStringSt, BOOLEAN fEnable )
{
	VIDEO_OVERLAY_DESC		VideoOverlayDesc;

	memset( &VideoOverlayDesc, 0, sizeof( VideoOverlayDesc ) );

	if ( pStringSt->iVideoOverlay != -1 )
	{
		VideoOverlayDesc.fDisabled	= !fEnable;
		VideoOverlayDesc.uiFlags	= VOVERLAY_DESC_DISABLED;
		UpdateVideoOverlay( &VideoOverlayDesc, pStringSt->iVideoOverlay, FALSE );
	}
}


void ClearDisplayedListOfTacticalStrings( void )
{
	// this function will go through list of display strings and clear them all out
	UINT32 cnt;

	// HEADROCK HAM 3.6: Number of messages to display now depends on external options.
	//for ( cnt = 0; cnt < MAX_LINE_COUNT; cnt++ )
	for ( cnt = 0; cnt < gGameExternalOptions.ubMaxMessagesTactical; cnt++ )
	{
		if ( gpDisplayList[ cnt ] != NULL )
		{
			// CHECK IF WE HAVE AGED

			// Remove our sorry ass
			RemoveStringVideoOverlay( gpDisplayList[ cnt ] );
			MemFree( gpDisplayList[ cnt ]->pString16);
			MemFree( gpDisplayList[ cnt ] );

			// Free slot
			gpDisplayList[ cnt ] = NULL;
		}
	}

	return;
}




////////////////////////////////////////////////////////////////////////////////
// Vengeance persistent tactical battle log + clickable NCTH shot inspector.
////////////////////////////////////////////////////////////////////////////////

#define BATTLE_LOG_MAX_ENTRIES 128
#define BATTLE_LOG_HEADER_H 18
#define BATTLE_LOG_RESIZE_GRIP 12
#define BATTLE_LOG_INSPECTOR_H 280

#define BATTLELOG_OUTCOME_NONE    0
#define BATTLELOG_OUTCOME_MISS    1
#define BATTLELOG_OUTCOME_BLOCKED   2
#define BATTLELOG_OUTCOME_HIT       3
#define BATTLELOG_OUTCOME_INTERCEPT 4
#define BATTLELOG_INSPECTOR_NONE 0
#define BATTLELOG_INSPECTOR_SHOT 1
#define BATTLELOG_INSPECTOR_DAMAGE 2

typedef struct
{
	UINT32 uiSequence;
	CHAR16 zText[640];
	UINT16 usColor;
	BOOLEAN fClickable;
	BOOLEAN fDamageClickable;
	INT16 sShotClickStart;
	INT16 sShotClickEnd;
	INT16 sDamageClickStart;
	INT16 sDamageClickEnd;
	CHAR16 zShotToken[32];
	CHAR16 zDamageToken[64];
	UINT8 ubOutcome;
	UINT8 ubActualTargetID;
	UINT8 ubBlockReason;
	INT16 sDamage;
	INT32 iBullet;
	NCTH_SHOT_DIAGNOSTIC ncth;
	DAMAGE_DIAGNOSTIC damage;
} BATTLE_LOG_ENTRY;

static BATTLE_LOG_ENTRY gBattleLogEntries[BATTLE_LOG_MAX_ENTRIES];
static UINT32 guiBattleLogSequence = 0;
static UINT16 gusBattleLogScrollOffset = 0;
static INT16 gsBattleLogSectorX = -1;
static INT16 gsBattleLogSectorY = -1;
static INT8 gbBattleLogSectorZ = -1;

static BOOLEAN gfBattleLogVisible = TRUE;
static BOOLEAN gfBattleLogRegionsCreated = FALSE;
static BOOLEAN gfBattleLogDragging = FALSE;
static BOOLEAN gfBattleLogResizing = FALSE;
static BOOLEAN gfBattleLogInspectorVisible = FALSE;

static INT16 gsBattleLogX = 6;
static INT16 gsBattleLogY = -1;
static INT16 gsBattleLogW = 330;
static INT16 gsBattleLogH = 108;

static INT16 gsBattleLogStartMouseX = 0;
static INT16 gsBattleLogStartMouseY = 0;
static INT16 gsBattleLogStartX = 0;
static INT16 gsBattleLogStartY = 0;
static INT16 gsBattleLogStartW = 0;
static INT16 gsBattleLogStartH = 0;

static INT32 giBattleLogOverlay = -1;
static MOUSE_REGION gBattleLogHeaderRegion;
static MOUSE_REGION gBattleLogContentRegion;
static MOUSE_REGION gBattleLogResizeRegion;
static MOUSE_REGION gBattleLogInspectorRegion;
static NCTH_SHOT_DIAGNOSTIC gBattleLogInspectorDiagnostic;
static DAMAGE_DIAGNOSTIC gBattleLogInspectorDamageDiagnostic;
static UINT8 gubBattleLogInspectorMode = BATTLELOG_INSPECTOR_NONE;
static UINT32 guiBattleLogInspectorSequence = 0;
static UINT8 gubBattleLogInspectorOutcome = BATTLELOG_OUTCOME_NONE;
static UINT8 gubBattleLogInspectorActualTargetID = NOBODY;
static UINT8 gubBattleLogInspectorBlockReason = BATTLELOG_BLOCK_STRUCTURE;
static INT16 gsBattleLogInspectorDamage = 0;

static void BattleLogRebuildOverlay( void );
static void BattleLogUpdateRegions( void );

static void BattleLogCheckSector( void )
{
	if ( gsBattleLogSectorX == gWorldSectorX &&
		 gsBattleLogSectorY == gWorldSectorY &&
		 gbBattleLogSectorZ == gbWorldSectorZ )
	{
		return;
	}

	gsBattleLogSectorX = gWorldSectorX;
	gsBattleLogSectorY = gWorldSectorY;
	gbBattleLogSectorZ = gbWorldSectorZ;
	memset( gBattleLogEntries, 0, sizeof(gBattleLogEntries) );
	guiBattleLogSequence = 0;
	gusBattleLogScrollOffset = 0;
	gfBattleLogInspectorVisible = FALSE;
	gubBattleLogInspectorMode = BATTLELOG_INSPECTOR_NONE;
	guiBattleLogInspectorSequence = 0;
	gubBattleLogInspectorOutcome = BATTLELOG_OUTCOME_NONE;
	gubBattleLogInspectorActualTargetID = NOBODY;
	gsBattleLogInspectorDamage = 0;
}

static UINT32 BattleLogOldestSequence( void )
{
	if ( guiBattleLogSequence > BATTLE_LOG_MAX_ENTRIES )
		return guiBattleLogSequence - BATTLE_LOG_MAX_ENTRIES + 1;
	return guiBattleLogSequence ? 1 : 0;
}

static BATTLE_LOG_ENTRY* BattleLogEntryBySequence( UINT32 uiSequence )
{
	if ( uiSequence == 0 || uiSequence > guiBattleLogSequence || uiSequence < BattleLogOldestSequence() )
		return NULL;

	BATTLE_LOG_ENTRY *pEntry = &gBattleLogEntries[(uiSequence - 1) % BATTLE_LOG_MAX_ENTRIES];
	if ( pEntry->uiSequence != uiSequence )
		return NULL;
	return pEntry;
}

static INT16 BattleLogLineHeight( void )
{
	return (INT16)(GetFontHeight( TINYFONT1 ) + 1);
}

static void BattleLogSetTokenRange( BATTLE_LOG_ENTRY *pEntry, const CHAR16 *pToken, BOOLEAN fDamage )
{
	if ( pEntry == NULL || pToken == NULL || pToken[0] == 0 )
		return;

	const CHAR16 *pFound = wcsstr( pEntry->zText, pToken );
	if ( pFound == NULL )
		return;

	CHAR16 prefix[640];
	UINT32 uiChars = (UINT32)(pFound - pEntry->zText);
	if ( uiChars >= 639 )
		uiChars = 639;
	wcsncpy( prefix, pEntry->zText, uiChars );
	prefix[uiChars] = 0;

	INT16 sStart = (INT16)StringPixLength( prefix, TINYFONT1 );
	INT16 sEnd = (INT16)(sStart + StringPixLength( (STR16)pToken, TINYFONT1 ));

	if ( fDamage )
	{
		pEntry->sDamageClickStart = sStart;
		pEntry->sDamageClickEnd = sEnd;
		wcsncpy( pEntry->zDamageToken, pToken, 63 );
		pEntry->zDamageToken[63] = 0;
	}
	else
	{
		pEntry->sShotClickStart = sStart;
		pEntry->sShotClickEnd = sEnd;
		wcsncpy( pEntry->zShotToken, pToken, 31 );
		pEntry->zShotToken[31] = 0;
	}
}

static INT16 BattleLogInspectorWidth( void )
{
	INT16 available = (INT16)__max( gsBattleLogW, SCREEN_WIDTH - gsBattleLogX - 2 );
	return (INT16)__max( gsBattleLogW, __min( 560, available ) );
}

static INT16 BattleLogInspectorTop( void )
{
	return (INT16)__max( 2, gsBattleLogY - BATTLE_LOG_INSPECTOR_H - 3 );
}

static INT16 BattleLogInspectorBottom( void )
{
	return (INT16)__min( SCREEN_HEIGHT - 2, BattleLogInspectorTop() + BATTLE_LOG_INSPECTOR_H );
}

static UINT16 BattleLogVisibleRows( void )
{
	INT16 usable = gsBattleLogH - BATTLE_LOG_HEADER_H - 5;
	INT16 lineH = BattleLogLineHeight();
	return (UINT16)__max( 1, usable / __max(1, lineH) );
}

static UINT32 BattleLogFirstVisibleSequence( UINT32 *pEndExclusive )
{
	UINT32 oldest = BattleLogOldestSequence();
	UINT32 endExclusive = guiBattleLogSequence + 1;
	UINT32 available;

	if ( endExclusive <= oldest )
	{
		if ( pEndExclusive ) *pEndExclusive = endExclusive;
		return 0;
	}

	available = endExclusive - oldest;
	UINT32 maxOffset = available > 0 ? available - 1 : 0;
	if ( gusBattleLogScrollOffset > maxOffset )
		gusBattleLogScrollOffset = (UINT16)maxOffset;

	endExclusive -= gusBattleLogScrollOffset;
	UINT32 rows = BattleLogVisibleRows();
	UINT32 first = endExclusive > rows ? endExclusive - rows : oldest;
	if ( first < oldest ) first = oldest;

	if ( pEndExclusive ) *pEndExclusive = endExclusive;
	return first;
}

static void BattleLogClampGeometry( void )
{
	gsBattleLogW = __max( 220, __min( gsBattleLogW, (INT16)__min(720, SCREEN_WIDTH - 8) ) );
	gsBattleLogH = __max( 82, __min( gsBattleLogH, (INT16)__min(300, SCREEN_HEIGHT - 8) ) );

	if ( gsBattleLogY < 0 )
	{
		// With the tactical HUD right-anchored on widescreen, the entire lower-left
		// strip is otherwise unused. Prefer it for the battle log so the log does
		// not cover the tactical world. Use most of the strip, capped at 560 px.
		INT16 sFreeLeftWidth = (INT16)(INTERFACE_START_X - gsBattleLogX - 6);
		if ( sFreeLeftWidth >= 220 )
		{
			gsBattleLogW = (INT16)__max( gsBattleLogW, __min( 560, sFreeLeftWidth ) );
			gsBattleLogY = (INT16)__max( 2, SCREEN_HEIGHT - gsBattleLogH - 4 );
		}
		else
		{
			// Narrow / 4:3 fallback: keep the log above the HUD rather than covering it.
			gsBattleLogY = (INT16)__max( 2, INTERFACE_START_Y - gsBattleLogH - 4 );
		}
	}

	gsBattleLogX = __max( 2, __min( gsBattleLogX, (INT16)(SCREEN_WIDTH - gsBattleLogW - 2) ) );
	gsBattleLogY = __max( 2, __min( gsBattleLogY, (INT16)(SCREEN_HEIGHT - gsBattleLogH - 2) ) );
}

static void BattleLogMoveCallback( MOUSE_REGION *pRegion, INT32 iReason )
{
	// Move/resize continuously while the mouse is grabbed. This makes the battle
	// log behave like a small desktop window instead of jumping only on release.
	if ( !(iReason & MSYS_CALLBACK_REASON_MOVE) )
		return;

	BOOLEAN fChanged = FALSE;
	if ( gfBattleLogDragging )
	{
		gsBattleLogX = (INT16)(gsBattleLogStartX + pRegion->MouseXPos - gsBattleLogStartMouseX);
		gsBattleLogY = (INT16)(gsBattleLogStartY + pRegion->MouseYPos - gsBattleLogStartMouseY);
		fChanged = TRUE;
	}
	else if ( gfBattleLogResizing )
	{
		gsBattleLogW = (INT16)(gsBattleLogStartW + pRegion->MouseXPos - gsBattleLogStartMouseX);
		gsBattleLogH = (INT16)(gsBattleLogStartH + pRegion->MouseYPos - gsBattleLogStartMouseY);
		fChanged = TRUE;
	}

	if ( fChanged )
	{
		BattleLogClampGeometry();
		BattleLogRebuildOverlay();
	}
}

static void BattleLogHeaderCallback( MOUSE_REGION *pRegion, INT32 iReason )
{
	if ( iReason & MSYS_CALLBACK_REASON_LBUTTON_DOUBLECLICK )
	{
		// Reset to the preferred lower-left widescreen layout.
		gfBattleLogDragging = FALSE;
		gfBattleLogResizing = FALSE;
		gsBattleLogX = 6;
		gsBattleLogY = -1;
		gsBattleLogW = 330;
		gsBattleLogH = 108;
		BattleLogClampGeometry();
		BattleLogUpdateRegions();
		BattleLogRebuildOverlay();
		return;
	}

	if ( iReason & MSYS_CALLBACK_REASON_LBUTTON_DWN )
	{
		gfBattleLogDragging = TRUE;
		gsBattleLogStartMouseX = pRegion->MouseXPos;
		gsBattleLogStartMouseY = pRegion->MouseYPos;
		gsBattleLogStartX = gsBattleLogX;
		gsBattleLogStartY = gsBattleLogY;
		MSYS_GrabMouse( pRegion );
	}
	else if ( iReason & MSYS_CALLBACK_REASON_LBUTTON_UP )
	{
		if ( gfBattleLogDragging )
		{
			gsBattleLogX = (INT16)(gsBattleLogStartX + pRegion->MouseXPos - gsBattleLogStartMouseX);
			gsBattleLogY = (INT16)(gsBattleLogStartY + pRegion->MouseYPos - gsBattleLogStartMouseY);
			gfBattleLogDragging = FALSE;
			MSYS_ReleaseMouse( pRegion );
			BattleLogClampGeometry();
			BattleLogUpdateRegions();
			BattleLogRebuildOverlay();
		}
	}
	else if ( iReason & MSYS_CALLBACK_REASON_LOST_MOUSE )
	{
		if ( gfBattleLogDragging )
		{
			gfBattleLogDragging = FALSE;
			BattleLogClampGeometry();
			BattleLogUpdateRegions();
			BattleLogRebuildOverlay();
		}
	}
	else if ( iReason & MSYS_CALLBACK_REASON_RBUTTON_UP )
	{
		// Never make the panel impossible to reopen by accident. Right-clicking
		// the header only dismisses the currently open shot inspector.
		if ( gfBattleLogInspectorVisible )
		{
			gfBattleLogInspectorVisible = FALSE;
			gubBattleLogInspectorMode = BATTLELOG_INSPECTOR_NONE;
			guiBattleLogInspectorSequence = 0;
			BattleLogUpdateRegions();
			BattleLogRebuildOverlay();
		}
	}
}

static void BattleLogResizeCallback( MOUSE_REGION *pRegion, INT32 iReason )
{
	if ( iReason & MSYS_CALLBACK_REASON_LBUTTON_DWN )
	{
		gfBattleLogResizing = TRUE;
		gsBattleLogStartMouseX = pRegion->MouseXPos;
		gsBattleLogStartMouseY = pRegion->MouseYPos;
		gsBattleLogStartW = gsBattleLogW;
		gsBattleLogStartH = gsBattleLogH;
		MSYS_GrabMouse( pRegion );
	}
	else if ( iReason & MSYS_CALLBACK_REASON_LBUTTON_UP )
	{
		if ( gfBattleLogResizing )
		{
			gsBattleLogW = (INT16)(gsBattleLogStartW + pRegion->MouseXPos - gsBattleLogStartMouseX);
			gsBattleLogH = (INT16)(gsBattleLogStartH + pRegion->MouseYPos - gsBattleLogStartMouseY);
			gfBattleLogResizing = FALSE;
			MSYS_ReleaseMouse( pRegion );
			BattleLogClampGeometry();
			BattleLogUpdateRegions();
			BattleLogRebuildOverlay();
		}
	}
	else if ( iReason & MSYS_CALLBACK_REASON_LOST_MOUSE )
	{
		if ( gfBattleLogResizing )
		{
			gfBattleLogResizing = FALSE;
			BattleLogClampGeometry();
			BattleLogUpdateRegions();
			BattleLogRebuildOverlay();
		}
	}
}

static void BattleLogInspectorCallback( MOUSE_REGION *pRegion, INT32 iReason )
{
	// The inspector sits over the tactical world. Consume clicks here so a player
	// cannot accidentally issue a move/fire command through the diagnostic panel.
	if ( iReason & MSYS_CALLBACK_REASON_RBUTTON_UP )
	{
		gfBattleLogInspectorVisible = FALSE;
		guiBattleLogInspectorSequence = 0;
		BattleLogUpdateRegions();
		BattleLogRebuildOverlay();
	}
}

static void BattleLogContentCallback( MOUSE_REGION *pRegion, INT32 iReason )
{
	UINT32 endExclusive = 0;
	UINT32 first = BattleLogFirstVisibleSequence( &endExclusive );
	INT16 lineH = BattleLogLineHeight();

	if ( iReason & MSYS_CALLBACK_REASON_WHEEL_UP )
	{
		UINT32 oldest = BattleLogOldestSequence();
		UINT32 available = guiBattleLogSequence >= oldest ? guiBattleLogSequence - oldest + 1 : 0;
		UINT32 maxOffset = available > 0 ? available - 1 : 0;
		gusBattleLogScrollOffset = (UINT16)__min( maxOffset, (UINT32)gusBattleLogScrollOffset + 3 );
		InvalidateRegion( gsBattleLogX, gsBattleLogY, gsBattleLogX + gsBattleLogW, gsBattleLogY + gsBattleLogH );
		return;
	}
	if ( iReason & MSYS_CALLBACK_REASON_WHEEL_DOWN )
	{
		gusBattleLogScrollOffset = gusBattleLogScrollOffset > 3 ? gusBattleLogScrollOffset - 3 : 0;
		InvalidateRegion( gsBattleLogX, gsBattleLogY, gsBattleLogX + gsBattleLogW, gsBattleLogY + gsBattleLogH );
		return;
	}
	if ( iReason & MSYS_CALLBACK_REASON_RBUTTON_UP )
	{
		if ( gfBattleLogInspectorVisible )
		{
			gfBattleLogInspectorVisible = FALSE;
			guiBattleLogInspectorSequence = 0;
			BattleLogUpdateRegions();
			BattleLogRebuildOverlay();
		}
		return;
	}
	if ( !(iReason & MSYS_CALLBACK_REASON_LBUTTON_UP) || first == 0 )
		return;

	INT16 row = (INT16)((pRegion->MouseYPos - (gsBattleLogY + BATTLE_LOG_HEADER_H + 2)) / __max(1, lineH));
	if ( row < 0 )
		return;

	UINT32 seq = first + row;
	if ( seq >= endExclusive )
		return;

	BATTLE_LOG_ENTRY *pEntry = BattleLogEntryBySequence( seq );
	if ( pEntry )
	{
		INT16 sRelativeX = (INT16)(pRegion->MouseXPos - (gsBattleLogX + 6));
		UINT8 ubRequestedMode = BATTLELOG_INSPECTOR_NONE;
		if ( pEntry->fDamageClickable && sRelativeX >= pEntry->sDamageClickStart && sRelativeX <= pEntry->sDamageClickEnd )
			ubRequestedMode = BATTLELOG_INSPECTOR_DAMAGE;
		else if ( pEntry->fClickable && sRelativeX >= pEntry->sShotClickStart && sRelativeX <= pEntry->sShotClickEnd )
			ubRequestedMode = BATTLELOG_INSPECTOR_SHOT;
		if ( ubRequestedMode == BATTLELOG_INSPECTOR_NONE )
			return;

		if ( gfBattleLogInspectorVisible && guiBattleLogInspectorSequence == seq && gubBattleLogInspectorMode == ubRequestedMode )
		{
			gfBattleLogInspectorVisible = FALSE;
			guiBattleLogInspectorSequence = 0;
		}
		else
		{
			if ( ubRequestedMode == BATTLELOG_INSPECTOR_DAMAGE )
				gBattleLogInspectorDamageDiagnostic = pEntry->damage;
			else
				gBattleLogInspectorDiagnostic = pEntry->ncth;
			gubBattleLogInspectorMode = ubRequestedMode;
			gubBattleLogInspectorOutcome = pEntry->ubOutcome;
			gubBattleLogInspectorActualTargetID = pEntry->ubActualTargetID;
			gubBattleLogInspectorBlockReason = pEntry->ubBlockReason;
			gsBattleLogInspectorDamage = pEntry->sDamage;
			guiBattleLogInspectorSequence = seq;
			gfBattleLogInspectorVisible = TRUE;
		}
		BattleLogUpdateRegions();
		BattleLogRebuildOverlay();
	}
}

static void BattleLogUpdateRegions( void )
{
	if ( !gfBattleLogRegionsCreated )
		return;

	gBattleLogHeaderRegion.RegionTopLeftX = gsBattleLogX;
	gBattleLogHeaderRegion.RegionTopLeftY = gsBattleLogY;
	gBattleLogHeaderRegion.RegionBottomRightX = gsBattleLogX + gsBattleLogW;
	gBattleLogHeaderRegion.RegionBottomRightY = gsBattleLogY + BATTLE_LOG_HEADER_H;

	gBattleLogContentRegion.RegionTopLeftX = gsBattleLogX;
	gBattleLogContentRegion.RegionTopLeftY = gsBattleLogY + BATTLE_LOG_HEADER_H;
	gBattleLogContentRegion.RegionBottomRightX = gsBattleLogX + gsBattleLogW;
	gBattleLogContentRegion.RegionBottomRightY = gsBattleLogY + gsBattleLogH - BATTLE_LOG_RESIZE_GRIP;

	gBattleLogResizeRegion.RegionTopLeftX = gsBattleLogX + gsBattleLogW - BATTLE_LOG_RESIZE_GRIP;
	gBattleLogResizeRegion.RegionTopLeftY = gsBattleLogY + gsBattleLogH - BATTLE_LOG_RESIZE_GRIP;
	gBattleLogResizeRegion.RegionBottomRightX = gsBattleLogX + gsBattleLogW;
	gBattleLogResizeRegion.RegionBottomRightY = gsBattleLogY + gsBattleLogH;

	INT16 sInspectorY = BattleLogInspectorTop();
	gBattleLogInspectorRegion.RegionTopLeftX = gsBattleLogX;
	gBattleLogInspectorRegion.RegionTopLeftY = sInspectorY;
	gBattleLogInspectorRegion.RegionBottomRightX = gsBattleLogX + BattleLogInspectorWidth();
	gBattleLogInspectorRegion.RegionBottomRightY = BattleLogInspectorBottom();
	if ( gfBattleLogInspectorVisible )
		MSYS_EnableRegion( &gBattleLogInspectorRegion );
	else
		MSYS_DisableRegion( &gBattleLogInspectorRegion );

	RefreshMouseRegions();
}

static void BattleLogCreateRegions( void )
{
	if ( gfBattleLogRegionsCreated )
		return;

	MSYS_DefineRegion( &gBattleLogContentRegion, gsBattleLogX, gsBattleLogY + BATTLE_LOG_HEADER_H,
		gsBattleLogX + gsBattleLogW, gsBattleLogY + gsBattleLogH - BATTLE_LOG_RESIZE_GRIP,
		MSYS_PRIORITY_HIGH, CURSOR_NORMAL, MSYS_NO_CALLBACK, BattleLogContentCallback );
	MSYS_AddRegion( &gBattleLogContentRegion );

	MSYS_DefineRegion( &gBattleLogHeaderRegion, gsBattleLogX, gsBattleLogY,
		gsBattleLogX + gsBattleLogW, gsBattleLogY + BATTLE_LOG_HEADER_H,
		MSYS_PRIORITY_HIGHEST - 2, CURSOR_NORMAL, BattleLogMoveCallback, BattleLogHeaderCallback );
	MSYS_AddRegion( &gBattleLogHeaderRegion );

	MSYS_DefineRegion( &gBattleLogResizeRegion, gsBattleLogX + gsBattleLogW - BATTLE_LOG_RESIZE_GRIP,
		gsBattleLogY + gsBattleLogH - BATTLE_LOG_RESIZE_GRIP, gsBattleLogX + gsBattleLogW, gsBattleLogY + gsBattleLogH,
		MSYS_PRIORITY_HIGHEST - 1, CURSOR_NORMAL, BattleLogMoveCallback, BattleLogResizeCallback );
	MSYS_AddRegion( &gBattleLogResizeRegion );

	INT16 sInspectorY = BattleLogInspectorTop();
	MSYS_DefineRegion( &gBattleLogInspectorRegion, gsBattleLogX, sInspectorY,
		gsBattleLogX + BattleLogInspectorWidth(), BattleLogInspectorBottom(),
		MSYS_PRIORITY_HIGHEST - 3, CURSOR_NORMAL, MSYS_NO_CALLBACK, BattleLogInspectorCallback );
	MSYS_AddRegion( &gBattleLogInspectorRegion );
	if ( !gfBattleLogInspectorVisible )
		MSYS_DisableRegion( &gBattleLogInspectorRegion );

	gfBattleLogRegionsCreated = TRUE;
}

static void BattleLogRemoveRegions( void )
{
	if ( !gfBattleLogRegionsCreated )
		return;
	MSYS_RemoveRegion( &gBattleLogInspectorRegion );
	MSYS_RemoveRegion( &gBattleLogResizeRegion );
	MSYS_RemoveRegion( &gBattleLogHeaderRegion );
	MSYS_RemoveRegion( &gBattleLogContentRegion );
	gfBattleLogRegionsCreated = FALSE;
}

static UINT8 *gpBattleLogDestBuf = NULL;
static UINT32 guiBattleLogDestPitchBYTES = 0;

static void BattleLogPrintInspectorLine( INT16 x, INT16 y, UINT16 color, STR16 text )
{
	if ( gpBattleLogDestBuf == NULL || text == NULL )
		return;

	SetFontForeground( color );
	mprintf_buffer( gpBattleLogDestBuf, guiBattleLogDestPitchBYTES, TINYFONT1, x, y, L"%s", text );
}

static void BattleLogPrintClippedLine( INT16 x, INT16 y, INT16 maxWidth, UINT16 color, STR16 text )
{
	if ( text == NULL )
		return;

	CHAR16 clipped[256];
	wcsncpy( clipped, text, 255 );
	clipped[255] = 0;

	UINT32 len = (UINT32)wcslen( clipped );
	BOOLEAN fClipped = FALSE;
	while ( len > 4 && StringPixLength( clipped, TINYFONT1 ) > maxWidth )
	{
		clipped[--len] = 0;
		fClipped = TRUE;
	}
	if ( fClipped && len > 3 )
	{
		clipped[len - 3] = L'.';
		clipped[len - 2] = L'.';
		clipped[len - 1] = L'.';
	}

	BattleLogPrintInspectorLine( x, y, color, clipped );
}

static void BlitBattleLog( VIDEO_OVERLAY *pBlitter )
{
	if ( !gfBattleLogVisible )
		return;

	BattleLogClampGeometry();

	UINT16 border = Get16BPPColor( FROMRGB( 68, 82, 92 ) );
	UINT16 borderHi = Get16BPPColor( FROMRGB( 120, 138, 146 ) );
	UINT16 bg = Get16BPPColor( FROMRGB( 11, 16, 20 ) );
	UINT16 header = Get16BPPColor( FROMRGB( 31, 43, 50 ) );

	ColorFillVideoSurfaceArea( pBlitter->uiDestBuff, gsBattleLogX, gsBattleLogY, gsBattleLogX + gsBattleLogW, gsBattleLogY + gsBattleLogH, bg );
	ColorFillVideoSurfaceArea( pBlitter->uiDestBuff, gsBattleLogX, gsBattleLogY, gsBattleLogX + gsBattleLogW, gsBattleLogY + BATTLE_LOG_HEADER_H, header );
	ColorFillVideoSurfaceArea( pBlitter->uiDestBuff, gsBattleLogX, gsBattleLogY, gsBattleLogX + gsBattleLogW, gsBattleLogY + 1, borderHi );
	ColorFillVideoSurfaceArea( pBlitter->uiDestBuff, gsBattleLogX, gsBattleLogY + gsBattleLogH - 1, gsBattleLogX + gsBattleLogW, gsBattleLogY + gsBattleLogH, border );
	ColorFillVideoSurfaceArea( pBlitter->uiDestBuff, gsBattleLogX, gsBattleLogY, gsBattleLogX + 1, gsBattleLogY + gsBattleLogH, borderHi );
	ColorFillVideoSurfaceArea( pBlitter->uiDestBuff, gsBattleLogX + gsBattleLogW - 1, gsBattleLogY, gsBattleLogX + gsBattleLogW, gsBattleLogY + gsBattleLogH, border );

	// Draw all solid panel surfaces before locking the framebuffer for font blits.
	// ColorFillVideoSurfaceArea performs its own surface access and must not be
	// invoked while we already hold the video-surface lock.
	INT16 inspectorX = gsBattleLogX;
	INT16 inspectorY = BattleLogInspectorTop();
	INT16 inspectorW = BattleLogInspectorWidth();
	if ( gfBattleLogInspectorVisible )
	{
		ColorFillVideoSurfaceArea( pBlitter->uiDestBuff, inspectorX, inspectorY, inspectorX + inspectorW, inspectorY + BATTLE_LOG_INSPECTOR_H, bg );
		ColorFillVideoSurfaceArea( pBlitter->uiDestBuff, inspectorX, inspectorY, inspectorX + inspectorW, inspectorY + BATTLE_LOG_HEADER_H, header );
		ColorFillVideoSurfaceArea( pBlitter->uiDestBuff, inspectorX, inspectorY, inspectorX + inspectorW, inspectorY + 1, borderHi );
		ColorFillVideoSurfaceArea( pBlitter->uiDestBuff, inspectorX, inspectorY, inspectorX + 1, inspectorY + BATTLE_LOG_INSPECTOR_H, borderHi );
		ColorFillVideoSurfaceArea( pBlitter->uiDestBuff, inspectorX + inspectorW - 1, inspectorY, inspectorX + inspectorW, inspectorY + BATTLE_LOG_INSPECTOR_H, border );
		ColorFillVideoSurfaceArea( pBlitter->uiDestBuff, inspectorX, inspectorY + BATTLE_LOG_INSPECTOR_H - 1, inspectorX + inspectorW, inspectorY + BATTLE_LOG_INSPECTOR_H, border );
	}

	gpBattleLogDestBuf = LockVideoSurface( pBlitter->uiDestBuff, &guiBattleLogDestPitchBYTES );
	if ( gpBattleLogDestBuf == NULL )
		return;

	SetFont( TINYFONT1 );
	SetFontBackground( FONT_MCOLOR_BLACK );
	SetFontShadow( DEFAULT_SHADOW );
	BattleLogPrintInspectorLine( gsBattleLogX + 6, gsBattleLogY + 4, FONT_MCOLOR_WHITE, L"BATTLE LOG" );
	BattleLogPrintClippedLine( gsBattleLogX + 76, gsBattleLogY + 4, gsBattleLogW - 108,
		FONT_MCOLOR_LTGRAY, L"drag | resize // | wheel | click shot | dblclick reset" );
	BattleLogPrintInspectorLine( gsBattleLogX + gsBattleLogW - 26, gsBattleLogY + 4, FONT_MCOLOR_LTGRAY, L"::" );

	UINT32 endExclusive = 0;
	UINT32 seq = BattleLogFirstVisibleSequence( &endExclusive );
	INT16 lineH = BattleLogLineHeight();
	INT16 y = gsBattleLogY + BATTLE_LOG_HEADER_H + 2;
	while ( seq && seq < endExclusive && y < gsBattleLogY + gsBattleLogH - BATTLE_LOG_RESIZE_GRIP )
	{
		BATTLE_LOG_ENTRY *pEntry = BattleLogEntryBySequence( seq );
		if ( pEntry )
		{
			BattleLogPrintClippedLine( gsBattleLogX + 6, y, gsBattleLogW - 18, pEntry->usColor, pEntry->zText );
		}
		seq++;
		y += lineH;
	}

	// Resize grip.
	BattleLogPrintInspectorLine( gsBattleLogX + gsBattleLogW - 12, gsBattleLogY + gsBattleLogH - 10, FONT_MCOLOR_LTGRAY, L"//" );

	if ( gfBattleLogInspectorVisible )
	{
		NCTH_SHOT_DIAGNOSTIC &d = gBattleLogInspectorDiagnostic;
		INT16 ix = inspectorX;
		INT16 iy = inspectorY;
		INT16 iw = inspectorW;
		CHAR16 z[256];
		UINT8 ubRound = d.ubVolleyShot > 0 ? d.ubVolleyShot : 1;
		const CHAR16 *pStance = L"standing";
		if ( d.ubStance == ANIM_CROUCH ) pStance = L"crouched";
		else if ( d.ubStance == ANIM_PRONE ) pStance = L"prone";

		const CHAR16 *pOutcomeTitle = L"SHOT INSPECTOR";
		UINT16 usOutcomeColor = FONT_MCOLOR_LTYELLOW;
		BOOLEAN fInspectorShooterPlayer = ( d.ubShooterID != NOBODY && MercPtrs[d.ubShooterID] &&
			MercPtrs[d.ubShooterID]->bTeam == gbPlayerNum );
		BOOLEAN fInspectorActualTargetPlayer = ( gubBattleLogInspectorActualTargetID != NOBODY &&
			MercPtrs[gubBattleLogInspectorActualTargetID] &&
			MercPtrs[gubBattleLogInspectorActualTargetID]->bTeam == gbPlayerNum );

		if ( gubBattleLogInspectorOutcome == BATTLELOG_OUTCOME_HIT )
		{
			pOutcomeTitle = L"SHOT INSPECTOR - HIT";
			usOutcomeColor = fInspectorShooterPlayer ? FONT_MCOLOR_LTGREEN : FONT_MCOLOR_LTRED;
		}
		else if ( gubBattleLogInspectorOutcome == BATTLELOG_OUTCOME_BLOCKED )
		{
			pOutcomeTitle = L"SHOT INSPECTOR - BLOCKED";
			usOutcomeColor = FONT_MCOLOR_LTYELLOW;
		}
		else if ( gubBattleLogInspectorOutcome == BATTLELOG_OUTCOME_INTERCEPT )
		{
			pOutcomeTitle = L"SHOT INSPECTOR - HIT OTHER";
			usOutcomeColor = fInspectorActualTargetPlayer ? FONT_MCOLOR_LTRED : FONT_MCOLOR_LTYELLOW;
		}
		else if ( gubBattleLogInspectorOutcome == BATTLELOG_OUTCOME_MISS )
		{
			pOutcomeTitle = L"SHOT INSPECTOR - MISS";
			usOutcomeColor = fInspectorShooterPlayer ? FONT_MCOLOR_LTRED : FONT_MCOLOR_LTYELLOW;
		}
		BattleLogPrintInspectorLine( ix + 6, iy + 4, usOutcomeColor, (STR16)pOutcomeTitle );

		INT16 sy = iy + BATTLE_LOG_HEADER_H + 3;
		const CHAR16 *pShooterName = L"unknown";
		const CHAR16 *pTargetName = L"target";
		const CHAR16 *pActualTargetName = L"someone";
		if ( d.ubShooterID != NOBODY && MercPtrs[d.ubShooterID] )
			pShooterName = MercPtrs[d.ubShooterID]->GetName();
		if ( d.ubTargetID != NOBODY && MercPtrs[d.ubTargetID] )
			pTargetName = MercPtrs[d.ubTargetID]->GetName();
		if ( gubBattleLogInspectorActualTargetID != NOBODY && MercPtrs[gubBattleLogInspectorActualTargetID] )
			pActualTargetName = MercPtrs[gubBattleLogInspectorActualTargetID]->GetName();

		if ( gubBattleLogInspectorOutcome == BATTLELOG_OUTCOME_INTERCEPT )
			swprintf( z, L"%s aimed %s -> hit %s | damage %d", pShooterName, pTargetName, pActualTargetName, gsBattleLogInspectorDamage );
		else if ( gubBattleLogInspectorOutcome == BATTLELOG_OUTCOME_HIT )
			swprintf( z, L"%s -> %s | damage %d", pShooterName, pActualTargetName, gsBattleLogInspectorDamage );
		else if ( gubBattleLogInspectorOutcome == BATTLELOG_OUTCOME_BLOCKED )
		{
			const CHAR16 *pBlock = L"cover/structure";
			if ( gubBattleLogInspectorBlockReason == BATTLELOG_BLOCK_GROUND ) pBlock = L"ground";
			else if ( gubBattleLogInspectorBlockReason == BATTLELOG_BLOCK_ROOF ) pBlock = L"roof";
			swprintf( z, L"%s -> %s | stopped by %s", pShooterName, pTargetName, pBlock );
		}
		else
			swprintf( z, L"%s -> %s", pShooterName, pTargetName );

		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_WHITE, z ); sy += lineH;

		const CHAR16 *pWeaponName = L"unknown weapon";
		if ( d.usWeapon < MAXITEMS && ShortItemNames[d.usWeapon][0] != 0 )
			pWeaponName = ShortItemNames[d.usWeapon];

		swprintf( z, L"Weapon: %s [item %d]", pWeaponName, d.usWeapon );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_WHITE, z ); sy += lineH;

		swprintf( z, L"Aim %d | round %d | range %.1f tiles | NCTH %.1f | muzzle sway %.1f",
			d.ubAimTime, ubRound, d.fRange / (FLOAT)CELL_X_SIZE, d.fFinalChance, d.fMuzzleSway );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_WHITE, z ); sy += lineH;

		swprintf( z, L"Skill: MRK %d  DEX %d  WIS %d  EXP %d | breath %d  shock %d",
			d.bMarksmanship, d.bDexterity, d.bWisdom, d.bExperience, d.bBreath, d.bShock );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_WHITE, z ); sy += lineH;

		swprintf( z, L"Handling: %d | base difficulty %.2f  aim difficulty %.2f | %s",
			d.ubModifiedHandling, d.fGunBaseDifficulty, d.fGunAimDifficulty, pStance );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTGRAY, z ); sy += lineH;

		swprintf( z, L"Base: attributes %.1f  flat %+0.1f  -> %.1f", d.fBaseAttribute, d.fFlatBase, d.fBaseChance );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTGREEN, z ); sy += lineH;

		swprintf( z, L"Base mod: condition %+0.1f%%  weapon %+0.1f%%  target %+0.1f%%",
			d.fBaseEffect, d.fBaseWeapon, d.fBaseTarget );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTGRAY, z ); sy += lineH;

		swprintf( z, L"Base extra: gear %+0.1f%%  team/difficulty %+0.1f%%  total %+0.1f%%",
			d.fGearAim, d.fBaseSpecial, d.fBaseModifier );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTGRAY, z ); sy += lineH;

		swprintf( z, L"Aim cap: attributes %.1f  trait %+0.1f  item-cap %+0.1f  -> %.1f",
			d.fAimAttribute, d.fAimTraitCap, d.fPercentCap, d.fAimCap );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTGREEN, z ); sy += lineH;

		swprintf( z, L"Aim mod: condition %+0.1f%%  weapon %+0.1f%%  team/difficulty %+0.1f%%",
			d.fAimEffect, d.fAimWeapon, d.fAimSpecial );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTGRAY, z ); sy += lineH;

		swprintf( z, L"Aim extra: trait %+0.1f%%  background %+0.1f%%  spotter %+0.1f%%",
			d.fTraitModifier, d.fBackground, d.fSpotter );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTGRAY, z ); sy += lineH;

		swprintf( z, L"Target: %+0.1f%%  visibility %+0.1f%%  close-scope %+0.1f%% | aim points +%.1f",
			d.fAimTarget, d.fVisibility, d.fScopePenalty, d.fAimPoints );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTGRAY, z ); sy += lineH;

		swprintf( z, L"Aperture: raw %.2f -> iron %.2f -> laser %.2f -> scope %.2f -> sway %.2f",
			d.fRawBasicAperture, d.fIronAperture, d.fLaserAperture, d.fMaxAperture, d.fFinalAperture );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_WHITE, z ); sy += lineH;

		swprintf( z, L"Optics: %.2fx raw / %.2fx effective | laser range %d | light %d | laser effect %.1f%%",
			d.fMagFactor, d.fEffectiveMagFactor, d.sLaserRange, d.bLaserLightLevel, d.fLaserEffectPercent );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_WHITE, z ); sy += lineH;

		swprintf( z, L"Sway/track: random %+0.2f,%+0.2f | tracking %+0.2f,%+0.2f",
			d.fRandomSwayX, d.fRandomSwayY, d.fTargetTrackingX, d.fTargetTrackingY );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTYELLOW, z ); sy += lineH;

		swprintf( z, L"Volley: inherited %+0.2f,%+0.2f | recoil %+0.2f,%+0.2f",
			d.fInheritedMuzzleX, d.fInheritedMuzzleY, d.fRecoilX, d.fRecoilY );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTGRAY, z ); sy += lineH;

		swprintf( z, L"Compensation: pre-recoil %+0.2f,%+0.2f | beyond-range Y %+0.2f",
			d.fPreRecoilX, d.fPreRecoilY, d.fRangeCompensationY );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTGRAY, z ); sy += lineH;

		swprintf( z, L"Weapon dispersion: %+0.2f,%+0.2f (radius %.2f) | final %+0.2f,%+0.2f",
			d.fDeviationX, d.fDeviationY, d.fBulletDeviation, d.fShotOffsetX, d.fShotOffsetY );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTRED, z ); sy += lineH;

		swprintf( z, L"Trajectory limiter correction: %+0.2f,%+0.2f",
			d.fLimitCorrectionX, d.fLimitCorrectionY );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTGRAY, z ); sy += lineH;

		swprintf( z, L"Accuracy extras: class %+0.1f%% | special NPC %+0.1f%%",
			d.fClassAccuracyBonus, d.fSpecialNPCAccuracyBonus );
		BattleLogPrintInspectorLine( ix + 7, sy,
			(d.fClassAccuracyBonus != 0.0f || d.fSpecialNPCAccuracyBonus != 0.0f) ? FONT_MCOLOR_LTYELLOW : FONT_MCOLOR_LTGRAY, z );
		sy += lineH;

		swprintf( z, L"Difficulty bonuses: base %+0.1f%% | aim %+0.1f%% | combined special %.1f / %.1f",
			d.fBaseDifficultyBonus, d.fAimDifficultyBonus, d.fBaseSpecial, d.fAimSpecial );
		BattleLogPrintInspectorLine( ix + 7, sy,
			(d.fBaseDifficultyBonus != 0.0f || d.fAimDifficultyBonus != 0.0f) ? FONT_MCOLOR_LTYELLOW : FONT_MCOLOR_LTGRAY, z );
		sy += lineH;

		// Player-readable dominant formula factor. The raw values above remain
		// visible so the explanation never hides the actual NCTH calculation.
		const CHAR16 *why = L"no major formula penalty";
		FLOAT worst = 0.0f;
		if ( d.fBaseWeapon < worst ) { worst = d.fBaseWeapon; why = L"weapon handling / base weapon penalty"; }
		if ( d.fAimWeapon < worst ) { worst = d.fAimWeapon; why = L"weapon handling while aiming"; }
		if ( d.fAimTarget < worst ) { worst = d.fAimTarget; why = L"target movement / stance / target difficulty"; }
		if ( d.fVisibility < worst ) { worst = d.fVisibility; why = L"visibility / intervening obstruction penalty"; }
		if ( d.fScopePenalty < worst ) { worst = d.fScopePenalty; why = L"scope used inside its efficient range"; }
		if ( d.fBaseEffect < worst ) { worst = d.fBaseEffect; why = L"shooter condition: shock, injury, fatigue or morale"; }
		swprintf( z, L"Biggest formula penalty: %s (%+.1f)", why, worst );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTYELLOW, z ); sy += lineH;

		// Also identify which physical component moved this particular round the
		// most. This is what makes two shots with the same NCTH end differently.
		const CHAR16 *physicalWhy = L"random muzzle sway";
		FLOAT physicalMagnitude = d.fRandomSwayX*d.fRandomSwayX + d.fRandomSwayY*d.fRandomSwayY;
		FLOAT candidate = d.fTargetTrackingX*d.fTargetTrackingX + d.fTargetTrackingY*d.fTargetTrackingY;
		if ( candidate > physicalMagnitude ) { physicalMagnitude = candidate; physicalWhy = L"target tracking/lead error"; }
		candidate = d.fPreRecoilX*d.fPreRecoilX + d.fPreRecoilY*d.fPreRecoilY;
		if ( candidate > physicalMagnitude ) { physicalMagnitude = candidate; physicalWhy = L"pre-recoil compensation"; }
		candidate = d.fInheritedMuzzleX*d.fInheritedMuzzleX + d.fInheritedMuzzleY*d.fInheritedMuzzleY;
		if ( candidate > physicalMagnitude ) { physicalMagnitude = candidate; physicalWhy = L"inherited burst direction"; }
		candidate = d.fRecoilX*d.fRecoilX + d.fRecoilY*d.fRecoilY;
		if ( candidate > physicalMagnitude ) { physicalMagnitude = candidate; physicalWhy = L"burst/autofire recoil"; }
		candidate = d.fRangeCompensationY*d.fRangeCompensationY;
		if ( candidate > physicalMagnitude ) { physicalMagnitude = candidate; physicalWhy = L"beyond-range compensation"; }
		candidate = d.fDeviationX*d.fDeviationX + d.fDeviationY*d.fDeviationY;
		if ( candidate > physicalMagnitude ) { physicalMagnitude = candidate; physicalWhy = L"intrinsic weapon dispersion"; }
		swprintf( z, L"Largest trajectory component: %s | aperture quality %d%%",
			physicalWhy, d.sApertureRatio );
		BattleLogPrintInspectorLine( ix + 7, sy, FONT_MCOLOR_LTRED, z );
	}

	UnLockVideoSurface( pBlitter->uiDestBuff );
	gpBattleLogDestBuf = NULL;
	guiBattleLogDestPitchBYTES = 0;

	InvalidateRegion( gsBattleLogX,
		gfBattleLogInspectorVisible ? BattleLogInspectorTop() : gsBattleLogY,
		gsBattleLogX + ( gfBattleLogInspectorVisible ? BattleLogInspectorWidth() : gsBattleLogW ) + 1,
		gfBattleLogInspectorVisible
			? (INT16)__max( gsBattleLogY + gsBattleLogH + 1, BattleLogInspectorBottom() + 1 )
			: gsBattleLogY + gsBattleLogH + 1 );
}

static void BattleLogRebuildOverlay( void )
{
	if ( giBattleLogOverlay != -1 )
	{
		RemoveVideoOverlay( giBattleLogOverlay );
		giBattleLogOverlay = -1;
	}

	if ( !gfBattleLogVisible || guiCurrentScreen != GAME_SCREEN )
		return;

	BattleLogClampGeometry();

	VIDEO_OVERLAY_DESC d;
	memset( &d, 0, sizeof(d) );
	d.sLeft = gsBattleLogX;
	d.sTop = gfBattleLogInspectorVisible ? BattleLogInspectorTop() : gsBattleLogY;
	d.sRight = gsBattleLogX + ( gfBattleLogInspectorVisible ? BattleLogInspectorWidth() : gsBattleLogW ) + 1;
	d.sBottom = gfBattleLogInspectorVisible
		? (INT16)__max( gsBattleLogY + gsBattleLogH + 1, BattleLogInspectorBottom() + 1 )
		: gsBattleLogY + gsBattleLogH + 1;
	d.sX = d.sLeft;
	d.sY = d.sTop;
	d.BltCallback = BlitBattleLog;
	giBattleLogOverlay = RegisterVideoOverlay( 0, &d );
	BattleLogUpdateRegions();
}

static void BattleLogEnsureUI( void )
{
	if ( !gfBattleLogVisible )
		return;
	BattleLogCheckSector();
	BattleLogClampGeometry();
	BattleLogCreateRegions();
	if ( giBattleLogOverlay == -1 )
		BattleLogRebuildOverlay();
}

static void BattleLogDestroyUI( void )
{
	BattleLogRemoveRegions();
	if ( giBattleLogOverlay != -1 )
	{
		RemoveVideoOverlay( giBattleLogOverlay );
		giBattleLogOverlay = -1;
	}
}

void BattleLogSetVisible( BOOLEAN fVisible )
{
	gfBattleLogVisible = fVisible;
	if ( fVisible )
	{
		BattleLogEnsureUI();
		BattleLogRebuildOverlay();
	}
	else
		BattleLogDestroyUI();
}

void BattleLogAddText( UINT16 usColor, STR16 pString )
{
	BattleLogCheckSector();
	if ( pString == NULL || pString[0] == 0 )
		return;

	guiBattleLogSequence++;
	BATTLE_LOG_ENTRY *pEntry = &gBattleLogEntries[(guiBattleLogSequence - 1) % BATTLE_LOG_MAX_ENTRIES];
	memset( pEntry, 0, sizeof(*pEntry) );
	pEntry->uiSequence = guiBattleLogSequence;
	pEntry->usColor = usColor;
	pEntry->ubOutcome = BATTLELOG_OUTCOME_NONE;
	pEntry->ubActualTargetID = NOBODY;
	pEntry->iBullet = -1;

	// ScreenMsg can carry a 512-character formatted string. Keep the battle-log
	// copy bounded so a long diagnostic or mod message cannot overrun the entry.
	swprintf( pEntry->zText, L"[%02d:%02d] ", guiHour, guiMin );
	UINT32 uiPrefixLen = (UINT32)wcslen( pEntry->zText );
	UINT32 uiCapacity = (UINT32)(sizeof(pEntry->zText) / sizeof(pEntry->zText[0]));
	if ( uiPrefixLen + 1 < uiCapacity )
	{
		wcsncat( pEntry->zText, pString, uiCapacity - uiPrefixLen - 1 );
		pEntry->zText[uiCapacity - 1] = 0;
	}
	gusBattleLogScrollOffset = 0;

	if ( guiCurrentScreen == GAME_SCREEN && gfBattleLogVisible )
	{
		BattleLogEnsureUI();
		InvalidateRegion( gsBattleLogX, gsBattleLogY, gsBattleLogX + gsBattleLogW, gsBattleLogY + gsBattleLogH );
	}
}

void BattleLogAddNCTHMiss( INT32 iBullet )
{
	BattleLogCheckSector();
	NCTH_SHOT_DIAGNOSTIC d;
	if ( !NCTHGetBulletDiagnostic( iBullet, &d ) )
		return;

	BOOLEAN fShooterPlayer = ( d.ubShooterID != NOBODY && MercPtrs[d.ubShooterID] &&
		MercPtrs[d.ubShooterID]->bTeam == gbPlayerNum );
	BOOLEAN fTargetPlayer = ( d.ubTargetID != NOBODY && MercPtrs[d.ubTargetID] &&
		MercPtrs[d.ubTargetID]->bTeam == gbPlayerNum );
	if ( !fShooterPlayer && !fTargetPlayer )
		return;

	guiBattleLogSequence++;
	BATTLE_LOG_ENTRY *pEntry = &gBattleLogEntries[(guiBattleLogSequence - 1) % BATTLE_LOG_MAX_ENTRIES];
	memset( pEntry, 0, sizeof(*pEntry) );
	pEntry->uiSequence = guiBattleLogSequence;
	pEntry->usColor = fShooterPlayer ? FONT_MCOLOR_LTRED : FONT_MCOLOR_LTYELLOW;
	pEntry->fClickable = TRUE;
	pEntry->ubOutcome = BATTLELOG_OUTCOME_MISS;
	pEntry->ubActualTargetID = NOBODY;
	pEntry->iBullet = iBullet;
	pEntry->ncth = d;

	const CHAR16 *pName = L"Merc";
	const CHAR16 *pTargetName = L"target";
	if ( d.ubShooterID != NOBODY && MercPtrs[d.ubShooterID] )
		pName = MercPtrs[d.ubShooterID]->GetName();
	if ( d.ubTargetID != NOBODY && MercPtrs[d.ubTargetID] )
		pTargetName = MercPtrs[d.ubTargetID]->GetName();

	const CHAR16 *pOutcome = fShooterPlayer ? L"MISS" : L"ENEMY MISS";
	swprintf( pEntry->zText, L"[%02d:%02d] %s - %s -> %s - NCTH %.0f  [click]",
		guiHour, guiMin, pOutcome, pName, pTargetName, d.fFinalChance );
	gusBattleLogScrollOffset = 0;

	if ( guiCurrentScreen == GAME_SCREEN && gfBattleLogVisible )
	{
		BattleLogEnsureUI();
		InvalidateRegion( gsBattleLogX, gsBattleLogY, gsBattleLogX + gsBattleLogW, gsBattleLogY + gsBattleLogH );
	}
}


void BattleLogAddNCTHBlocked( INT32 iBullet, UINT8 ubReason )
{
	BattleLogCheckSector();
	NCTH_SHOT_DIAGNOSTIC d;
	if ( !NCTHGetBulletDiagnostic( iBullet, &d ) )
		return;

	// Only describe a blocked round as a missed merc shot when there actually was
	// an intended soldier target. Deliberate fire at doors/windows/terrain should
	// remain ordinary structure interaction, not appear as a shooting failure.
	if ( d.ubTargetID == NOBODY )
		return;

	BOOLEAN fShooterPlayer = ( d.ubShooterID != NOBODY && MercPtrs[d.ubShooterID] &&
		MercPtrs[d.ubShooterID]->bTeam == gbPlayerNum );
	BOOLEAN fTargetPlayer = ( d.ubTargetID != NOBODY && MercPtrs[d.ubTargetID] &&
		MercPtrs[d.ubTargetID]->bTeam == gbPlayerNum );
	if ( !fShooterPlayer && !fTargetPlayer )
		return;

	guiBattleLogSequence++;
	BATTLE_LOG_ENTRY *pEntry = &gBattleLogEntries[(guiBattleLogSequence - 1) % BATTLE_LOG_MAX_ENTRIES];
	memset( pEntry, 0, sizeof(*pEntry) );
	pEntry->uiSequence = guiBattleLogSequence;
	pEntry->usColor = FONT_MCOLOR_LTYELLOW;
	pEntry->fClickable = TRUE;
	pEntry->ubOutcome = BATTLELOG_OUTCOME_BLOCKED;
	pEntry->ubActualTargetID = NOBODY;
	pEntry->ubBlockReason = ubReason;
	pEntry->iBullet = iBullet;
	pEntry->ncth = d;

	const CHAR16 *pName = L"Merc";
	const CHAR16 *pTargetName = L"target";
	const CHAR16 *pBlockReason = L"cover/structure";

	if ( d.ubShooterID != NOBODY && MercPtrs[d.ubShooterID] )
		pName = MercPtrs[d.ubShooterID]->GetName();
	if ( d.ubTargetID != NOBODY && MercPtrs[d.ubTargetID] )
		pTargetName = MercPtrs[d.ubTargetID]->GetName();

	if ( ubReason == BATTLELOG_BLOCK_GROUND )
		pBlockReason = L"ground";
	else if ( ubReason == BATTLELOG_BLOCK_ROOF )
		pBlockReason = L"roof";

	const CHAR16 *pOutcome = fShooterPlayer ? L"BLOCKED" : L"ENEMY BLOCKED";
	swprintf( pEntry->zText, L"[%02d:%02d] %s - %s -> %s - %s - NCTH %.0f  [click]",
		guiHour, guiMin, pOutcome, pName, pTargetName, pBlockReason, d.fFinalChance );
	gusBattleLogScrollOffset = 0;

	if ( guiCurrentScreen == GAME_SCREEN && gfBattleLogVisible )
	{
		BattleLogEnsureUI();
		InvalidateRegion( gsBattleLogX, gsBattleLogY,
			gsBattleLogX + gsBattleLogW, gsBattleLogY + gsBattleLogH );
	}
}


void BattleLogAddNCTHHit( INT32 iBullet, UINT8 ubTargetID, INT16 sDamage )
{
	BattleLogCheckSector();
	NCTH_SHOT_DIAGNOSTIC d;
	if ( !NCTHGetBulletDiagnostic( iBullet, &d ) )
		return;

	BOOLEAN fIntendedHit = ( d.ubTargetID != NOBODY && d.ubTargetID == ubTargetID );
	BOOLEAN fShooterPlayer = ( d.ubShooterID != NOBODY && MercPtrs[d.ubShooterID] &&
		MercPtrs[d.ubShooterID]->bTeam == gbPlayerNum );
	BOOLEAN fIntendedTargetPlayer = ( d.ubTargetID != NOBODY && MercPtrs[d.ubTargetID] &&
		MercPtrs[d.ubTargetID]->bTeam == gbPlayerNum );
	BOOLEAN fActualTargetPlayer = ( ubTargetID != NOBODY && MercPtrs[ubTargetID] &&
		MercPtrs[ubTargetID]->bTeam == gbPlayerNum );

	// Keep AI-vs-AI traffic out of the player's log. If an intended player shot
	// struck somebody else, keep it: that is exactly the kind of trajectory
	// outcome the inspector is meant to explain.
	if ( !fShooterPlayer && !fIntendedTargetPlayer && !fActualTargetPlayer )
		return;

	guiBattleLogSequence++;
	BATTLE_LOG_ENTRY *pEntry = &gBattleLogEntries[(guiBattleLogSequence - 1) % BATTLE_LOG_MAX_ENTRIES];
	memset( pEntry, 0, sizeof(*pEntry) );
	pEntry->uiSequence = guiBattleLogSequence;
	pEntry->fClickable = TRUE;
	pEntry->ubOutcome = fIntendedHit ? BATTLELOG_OUTCOME_HIT : BATTLELOG_OUTCOME_INTERCEPT;
	pEntry->usColor = fIntendedHit
		? ( fShooterPlayer ? FONT_MCOLOR_LTGREEN : FONT_MCOLOR_LTRED )
		: ( fActualTargetPlayer ? FONT_MCOLOR_LTRED : FONT_MCOLOR_LTYELLOW );
	pEntry->ubActualTargetID = ubTargetID;
	pEntry->sDamage = sDamage;
	pEntry->iBullet = iBullet;
	pEntry->ncth = d;

	const CHAR16 *pName = L"Merc";
	const CHAR16 *pIntendedTargetName = L"target";
	const CHAR16 *pActualTargetName = L"someone";
	if ( d.ubShooterID != NOBODY && MercPtrs[d.ubShooterID] )
		pName = MercPtrs[d.ubShooterID]->GetName();
	if ( d.ubTargetID != NOBODY && MercPtrs[d.ubTargetID] )
		pIntendedTargetName = MercPtrs[d.ubTargetID]->GetName();
	if ( ubTargetID != NOBODY && MercPtrs[ubTargetID] )
		pActualTargetName = MercPtrs[ubTargetID]->GetName();

	if ( fIntendedHit )
	{
		const CHAR16 *pOutcome = fShooterPlayer ? L"HIT" : L"ENEMY HIT";
		swprintf( pEntry->zText, L"[%02d:%02d] %s - %s -> %s - dmg %d - NCTH %.0f  [click]",
			guiHour, guiMin, pOutcome, pName, pActualTargetName, sDamage, d.fFinalChance );
	}
	else
	{
		const CHAR16 *pOutcome = fShooterPlayer ? L"HIT OTHER" : L"ENEMY HIT OTHER";
		swprintf( pEntry->zText, L"[%02d:%02d] %s - %s aimed %s, hit %s - dmg %d  [click]",
			guiHour, guiMin, pOutcome, pName, pIntendedTargetName, pActualTargetName, sDamage );
	}
	gusBattleLogScrollOffset = 0;

	if ( guiCurrentScreen == GAME_SCREEN && gfBattleLogVisible )
	{
		BattleLogEnsureUI();
		InvalidateRegion( gsBattleLogX, gsBattleLogY,
			gsBattleLogX + gsBattleLogW, gsBattleLogY + gsBattleLogH );
	}
}

void ScrollString( )
{
	UINT32 suiTimer=0;
	UINT32 cnt;
	INT32 iNumberOfNewStrings = 0; // the count of new strings, so we can update position by WIDTH_BETWEEN_NEW_STRINGS pixels in the y
	INT32 iNumberOfMessagesOnQueue = 0;
	INT32 iMaxAge = 0;
	BOOLEAN fDitchLastMessage = FALSE;

	INT16 iMsgYStart = ((UsingNewInventorySystem() == false)) ? SCREEN_HEIGHT - 150 : SCREEN_HEIGHT - 210;


	// UPDATE TIMER
	suiTimer=GetJA2Clock();

	// might have pop up text timer
	HandleLastQuotePopUpTimer( );

	if ( guiCurrentScreen == GAME_SCREEN )
		BattleLogEnsureUI();
	else
		BattleLogDestroyUI();

	if( guiCurrentScreen == MAP_SCREEN )
	{
		return;
	}

	// HEADROCK HAM 3.6: This replaces the static variable MAX_LINE_COUNT for all tactical displays.
	UINT8 ubLines = gGameExternalOptions.ubMaxMessagesTactical;

	// DONOT UPDATE IF WE ARE SCROLLING!
	if ( gfScrollPending || gfScrollInertia )
	{
		return;
	}

	// messages hidden
	if( fScrollMessagesHidden )
	{
		return;
	}

	iNumberOfMessagesOnQueue = GetMessageQueueSize( );
	iMaxAge =	MAX_AGE;

	if( ( iNumberOfMessagesOnQueue > 0 )&&( gpDisplayList[ ubLines - 1 ] != NULL) )
	{
		fDitchLastMessage = TRUE;
	}
	else
	{
		fDitchLastMessage = FALSE;

	}


	if( ( iNumberOfMessagesOnQueue * 1000 ) >= iMaxAge )
	{
		iNumberOfMessagesOnQueue = ( iMaxAge / 1000 );
	}
	else if( iNumberOfMessagesOnQueue < 0 )
	{
		iNumberOfMessagesOnQueue = 0;
	}

	//AGE
	for ( cnt = 0; cnt < ubLines; cnt++ )
	{
		if ( gpDisplayList[ cnt ] != NULL )
		{
			if( ( fDitchLastMessage ) && ( cnt == ubLines - 1 ) )
			{
				gpDisplayList[ cnt ]->uiTimeOfLastUpdate = iMaxAge;
			}
				// CHECK IF WE HAVE AGED
			if ( ( suiTimer - gpDisplayList[ cnt ]->uiTimeOfLastUpdate ) > ( UINT32 )( iMaxAge - ( 1000 * iNumberOfMessagesOnQueue ) ) )
			{
				// Remove our sorry ass
				RemoveStringVideoOverlay( gpDisplayList[ cnt ] );
				MemFree( gpDisplayList[ cnt ]->pString16);
				MemFree( gpDisplayList[ cnt ] );

				// Free slot
				gpDisplayList[ cnt ] = NULL;
			}
		}
	}


	// CHECK FOR FREE SPOTS AND ADD ANY STRINGS IF WE HAVE SOME TO ADD!

	// FIRST CHECK IF WE HAVE ANY IN OUR QUEUE
	if ( pStringS != NULL )
	{
		// CHECK IF WE HAVE A SLOT!
		// CHECK OUR LAST SLOT!
		if ( gpDisplayList[ ubLines - 1 ] == NULL )
		{
			// MOVE ALL UP!

		// cpy, then move
		for( cnt = ubLines - 1; cnt > 0; cnt-- )
		{
				gpDisplayList[ cnt ] =	gpDisplayList[ cnt - 1 ];
		}

			// now add in the new string
		cnt = 0;
		gpDisplayList[ cnt ] = pStringS;
		CreateStringVideoOverlay( pStringS, X_START, iMsgYStart );
		if( pStringS->fBeginningOfNewString == TRUE )
		{
			iNumberOfNewStrings++;
		}

		// set up age
		pStringS->uiTimeOfLastUpdate = GetJA2Clock();

		// now move
		for ( cnt = 0; cnt <= (UINT8)(ubLines - 1); cnt++ )
		{

				// Adjust position!
				if ( gpDisplayList[ cnt	] != NULL )
				{

					SetStringVideoOverlayPosition( gpDisplayList[ cnt ], X_START, (INT16)( ( iMsgYStart - ( ( cnt ) * GetFontHeight( SMALLFONT1 ) ) ) - ( INT16)( WIDTH_BETWEEN_NEW_STRINGS * ( iNumberOfNewStrings ) ) ) );

					// start of new string, increment count of new strings, for spacing purposes
					if( gpDisplayList[ cnt ]->fBeginningOfNewString == TRUE )
					{
						iNumberOfNewStrings++;
					}


				}

		}


		// WE NOW HAVE A FREE SPACE, INSERT!

		// Adjust head!
		pStringS = pStringS->pNext;
		if( pStringS )
		{
			pStringS->pPrev = NULL;
		}

		//check if new meesage we have not seen since mapscreen..if so, beep
			if( ( fOkToBeepNewMessage == TRUE ) && ( gpDisplayList[ ubLines - 2 ] == NULL ) && ( ( guiCurrentScreen == GAME_SCREEN ) || ( guiCurrentScreen == MAP_SCREEN ) ) && ( gfFacePanelActive == FALSE ) )
			{
				PlayNewMessageSound( );
			}
		}
	}
}


void DisableScrollMessages( void )
{
	// will stop the scroll of messages in tactical and hide them during an NPC's dialogue
	// disble video overlay for tatcitcal scroll messages
	EnableDisableScrollStringVideoOverlay( FALSE );
	return;
}


void EnableScrollMessages( void )
{
	EnableDisableScrollStringVideoOverlay( TRUE );
	return;
}

void HideMessagesDuringNPCDialogue( void )
{
	// will stop the scroll of messages in tactical and hide them during an NPC's dialogue
	INT32 cnt;

	VIDEO_OVERLAY_DESC		VideoOverlayDesc;

	memset( &VideoOverlayDesc, 0, sizeof( VideoOverlayDesc ) );


	VideoOverlayDesc.fDisabled	= TRUE;
	VideoOverlayDesc.uiFlags	= VOVERLAY_DESC_DISABLED;


	fScrollMessagesHidden = TRUE;
	uiStartOfPauseTime = GetJA2Clock();

	UINT8 ubLines = 0;

	// HEADROCK HAM 3.6: Different number of lines shown in Tactical and Strategic modes.
	if( guiCurrentScreen == MAP_SCREEN )
	{
		ubLines = MAX_LINE_COUNT;
	}
	else
	{
		ubLines = gGameExternalOptions.ubMaxMessagesTactical;
	}

	for ( cnt = 0; cnt < ubLines; cnt++ )
	{
			if ( gpDisplayList[ cnt ] != NULL )
			{
				RestoreExternBackgroundRectGivenID( gVideoOverlays[ gpDisplayList[ cnt ]->iVideoOverlay ].uiBackground );
				UpdateVideoOverlay( &VideoOverlayDesc, gpDisplayList[ cnt ]->iVideoOverlay, FALSE );
			}
	}

	return;
}


void UnHideMessagesDuringNPCDialogue( void )
{

	VIDEO_OVERLAY_DESC		VideoOverlayDesc;
	INT32 cnt = 0;

	memset( &VideoOverlayDesc, 0, sizeof( VideoOverlayDesc ) );


	VideoOverlayDesc.fDisabled	= FALSE;
	VideoOverlayDesc.uiFlags	= VOVERLAY_DESC_DISABLED;
	fScrollMessagesHidden				= FALSE;

	UINT8 ubLines = 0;

	// HEADROCK HAM 3.6: Different number of lines shown in Tactical and Strategic modes.
	if( guiCurrentScreen == MAP_SCREEN )
	{
		ubLines = MAX_LINE_COUNT;
	}
	else
	{
		ubLines = gGameExternalOptions.ubMaxMessagesTactical;
	}

	for ( cnt = 0; cnt < ubLines; cnt++ )
	{
		if ( gpDisplayList[ cnt ] != NULL )
		{
			gpDisplayList[ cnt ]->uiTimeOfLastUpdate+= GetJA2Clock() - uiStartOfPauseTime;
			UpdateVideoOverlay( &VideoOverlayDesc, gpDisplayList[ cnt ]->iVideoOverlay, FALSE );
		}
	}


	return;
}

void ScreenMsg( UINT16 usColor, UINT8 ubPriority, STR16 pStringA, ...)
{
	//__try
	//{
		CHAR16	DestString[512];
		va_list argptr;

		if( fDisableJustForIan == TRUE )
		{
			if( ubPriority == MSG_BETAVERSION )
			{
				return;
			}
			else if( ubPriority == MSG_TESTVERSION )
			{
				return;
			}
			else if ( ubPriority == MSG_DEBUG )
			{
				return;
			}
		}

		if( ubPriority == MSG_DEBUG )
		{
			usColor = DEBUG_COLOR;
			#ifndef _DEBUG
				return;
			#endif
		}

		if( ubPriority == MSG_BETAVERSION )
		{
			usColor = BETAVERSION_COLOR;
			#ifndef JA2BETAVERSION
				#ifndef JA2TESTVERSION
					return;
				#endif
			#endif

		}

		if( ubPriority == MSG_TESTVERSION )
		{
			usColor = TESTVERSION_COLOR;

			#ifndef JA2TESTVERSION
				return;
			#endif

		}

		va_start(argptr, pStringA);
		vswprintf(DestString, pStringA, argptr);
		va_end(argptr);

		if ( guiCurrentScreen == GAME_SCREEN && ubPriority != MSG_DEBUG && ubPriority != MSG_TESTVERSION && ubPriority != MSG_BETAVERSION )
			BattleLogAddText( usColor, DestString );

		// pass onto tactical message and mapscreen message
		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,String("ScreenMsg start: %s", DestString));
		TacticalScreenMsg( usColor, ubPriority, DestString );
		DebugMsg (TOPIC_JA2,DBG_LEVEL_3,String("ScreenMsg end: %s", DestString));

		//	if( ( ubPriority != MSG_DEBUG ) && ( ubPriority != MSG_TESTVERSION ) )
		{
			MapScreenMessage( usColor, ubPriority, DestString );
		}


		if( guiCurrentScreen == MAP_SCREEN )
		{
			PlayNewMessageSound( );
		}
		else
		{
			fOkToBeepNewMessage = TRUE;
		}

		return;
	//}
	//__except(filter(GetExceptionCode(), GetExceptionInformation()))
	//{
	//	return;
	//}
}

void ClearWrappedStrings( WRAPPED_STRING *pStringWrapperHead )
{
	WRAPPED_STRING *pNode = pStringWrapperHead;
	WRAPPED_STRING *pDeleteNode = NULL;
	// clear out a link list of wrapped string structures

	// error check, is there a node to delete?
	if( pNode == NULL )
	{
		// leave,
		return;
	}

	do
	{

		// set delete node as current node
		pDeleteNode = pNode;

		// set current node as next node
		pNode = pNode->pNextWrappedString;

		//delete the string
		MemFree( pDeleteNode->sString );
		pDeleteNode->sString = NULL;

		// clear out delete node
		MemFree( pDeleteNode );
		pDeleteNode = NULL;

	}	while( pNode );


//	MemFree( pNode );

	pStringWrapperHead = NULL;

}


// new tactical and mapscreen message system
void TacticalScreenMsg( UINT16 usColor, UINT8 ubPriority, STR16 pStringA, ... )
{
	// this function sets up the string into several single line structures

	ScrollStringStPtr pStringSt;
	UINT32 uiFont = TINYFONT1;
	//STR16pString;
	va_list argptr;

	CHAR16	DestString[512];//, DestStringA[ 512 ];
	//STR16pStringBuffer;
	ScrollStringStPtr pTempStringSt=NULL;
	WRAPPED_STRING *pStringWrapper=NULL;
	WRAPPED_STRING *pStringWrapperHead=NULL;
	BOOLEAN fNewString = FALSE;
	UINT16	usLineWidthIfWordIsWiderThenWidth=0;


	if( giTimeCompressMode > TIME_COMPRESS_X1 )
	{
		return;
	}

	if( fDisableJustForIan == TRUE && ubPriority != MSG_ERROR && ubPriority != MSG_INTERFACE )
	{
		return;
	}

	// OJW - 20090426 - Filter tactical messages
	if ( gTacticalMsgFilterPriority != -1 && gTacticalMsgFilterPriority != ubPriority )
	{
		return;
	}

	if( ubPriority == MSG_BETAVERSION )
	{
		usColor = BETAVERSION_COLOR;
		#ifndef JA2BETAVERSION
			#ifndef JA2TESTVERSION
				return;
			#endif
		#endif
		va_start(argptr, pStringA);					// Set up variable argument pointer
		vswprintf(DestString, pStringA, argptr);	// process gprintf string (get output str)
		va_end(argptr);
		WriteMessageToFile( DestString );
	}

	if( ubPriority == MSG_TESTVERSION )
	{
		usColor = TESTVERSION_COLOR;

		#ifndef JA2TESTVERSION
			return;
		#endif
		va_start(argptr, pStringA);					// Set up variable argument pointer
		vswprintf(DestString, pStringA, argptr);	// process gprintf string (get output str)
		va_end(argptr);
		WriteMessageToFile( DestString );
	}


	if ( fFirstTimeInMessageSystem )
	{
		// Init display array!
		memset( gpDisplayList, 0, sizeof( gpDisplayList ) );
		fFirstTimeInMessageSystem = FALSE;
		//if(!(InitializeMutex(SCROLL_MESSAGE_MUTEX,"ScrollMessageMutex" )))
		//	return;
	}


	pStringSt=pStringS;
	while(GetNextString(pStringSt))
		 pStringSt=GetNextString(pStringSt);

	va_start(argptr, pStringA);			// Set up variable argument pointer
	vswprintf(DestString, pStringA, argptr);	// process gprintf string (get output str)
	va_end(argptr);

	if ( ubPriority == MSG_DEBUG )
	{
		#ifndef _DEBUG
			return;
		#else
		usColor = DEBUG_COLOR;
		//wcscpy( DestStringA, DestString );
		//swprintf( DestString, L"Debug: %s", DestStringA );
		//WriteMessageToFile( DestStringA );
		#endif
	}

	if ( ubPriority == MSG_DIALOG )
	{
		usColor = DIALOGUE_COLOR;
	}

	if ( ubPriority == MSG_INTERFACE )
	{
		// HEADROCK HAM 3.6: Why force yellow? Let's not.
		//usColor = INTERFACE_COLOR;
	}

	// HEADROCK HAM 3.6: Allow for longer lines.
	LINE_WIDTH = (SCREEN_WIDTH - 320);

	pStringWrapperHead=LineWrap(uiFont, LINE_WIDTH, &usLineWidthIfWordIsWiderThenWidth, DestString);
	pStringWrapper=pStringWrapperHead;
	if(!pStringWrapper)
		return;

	fNewString = TRUE;
	while(pStringWrapper->pNextWrappedString!=NULL)
	{
		if(!pStringSt)
		{
			pStringSt=AddString(pStringWrapper->sString, usColor, uiFont, fNewString, ubPriority );
			fNewString = FALSE;
			pStringSt->pNext=NULL;
			pStringSt->pPrev=NULL;
			pStringS=pStringSt;
		}
		else
		{
			pTempStringSt=AddString(pStringWrapper->sString, usColor, uiFont, fNewString, ubPriority);
			fNewString = FALSE;
			pTempStringSt->pPrev=pStringSt;
			pStringSt->pNext=pTempStringSt;
			pStringSt=pTempStringSt;
			pTempStringSt->pNext=NULL;
		}
		pStringWrapper=pStringWrapper->pNextWrappedString;
	}
	pTempStringSt=AddString(pStringWrapper->sString, usColor, uiFont, fNewString, ubPriority );
	if(pStringSt)
	{
		pStringSt->pNext=pTempStringSt;
		pTempStringSt->pPrev=pStringSt;
		pStringSt=pTempStringSt;
		pStringSt->pNext=NULL;
	}
	else
	{
		pStringSt=pTempStringSt;
		pStringSt->pNext=NULL;
		pStringSt->pPrev=NULL;
		pStringS=pStringSt;
	}

	// clear up list of wrapped strings
	ClearWrappedStrings( pStringWrapperHead );

	//LeaveMutex(SCROLL_MESSAGE_MUTEX, __LINE__, __FILE__);
	return;
}


void MapScreenMessage( UINT16 usColor, UINT8 ubPriority, STR16 pStringA, ... )
{
	// this function sets up the string into several single line structures

	ScrollStringStPtr pStringSt;
	UINT32 uiFont = MAP_SCREEN_MESSAGE_FONT;
	//STR16pString;
	va_list argptr;
	CHAR16	DestString[512], DestStringA[ 512 ];
	//STR16pStringBuffer;
	WRAPPED_STRING *pStringWrapper=NULL;
	WRAPPED_STRING *pStringWrapperHead=NULL;
	BOOLEAN fNewString = FALSE;
	UINT16	usLineWidthIfWordIsWiderThenWidth;

	if( fDisableJustForIan == TRUE )
	{
		if( ubPriority == MSG_BETAVERSION )
		{
			return;
		}
		else if( ubPriority == MSG_TESTVERSION )
		{
			return;
		}
		else if ( ubPriority == MSG_DEBUG )
		{
			return;
		}
	}

	if( ubPriority == MSG_BETAVERSION )
	{
		usColor = BETAVERSION_COLOR;
		#ifndef JA2BETAVERSION
			#ifndef JA2TESTVERSION
				return;
			#endif
		#endif

		va_start(argptr, pStringA);			// Set up variable argument pointer
		vswprintf(DestString, pStringA, argptr);	// process gprintf string (get output str)
		va_end(argptr);
		WriteMessageToFile( DestString );
	}

	if( ubPriority == MSG_TESTVERSION )
	{
		usColor = TESTVERSION_COLOR;
		va_start(argptr, pStringA);			// Set up variable argument pointer
		vswprintf(DestString, pStringA, argptr);	// process gprintf string (get output str)
		va_end(argptr);

		#ifndef JA2TESTVERSION
			return;
		#endif
		WriteMessageToFile( DestString );
	}
	// OK, check if we are ani imeediate feedback message, if so, do something else!
	if ( ubPriority == MSG_UI_FEEDBACK )
	{
		va_start(argptr, pStringA);			// Set up variable argument pointer
		vswprintf(DestString, pStringA, argptr);	// process gprintf string (get output str)
		va_end(argptr);

		BeginUIMessage( DestString );
		return;
	}

	if ( ubPriority == MSG_SKULL_UI_FEEDBACK )
	{
		va_start(argptr, pStringA);			// Set up variable argument pointer
		vswprintf(DestString, pStringA, argptr);	// process gprintf string (get output str)
		va_end(argptr);

		InternalBeginUIMessage( TRUE, DestString );
		return;
	}

	// check if error
	if ( ubPriority == MSG_ERROR )
	{
		va_start(argptr, pStringA);			// Set up variable argument pointer
		vswprintf(DestString, pStringA, argptr);	// process gprintf string (get output str)
		va_end(argptr);

		swprintf( DestStringA, L"DEBUG: %s", DestString );

		BeginUIMessage( DestStringA );
		WriteMessageToFile( DestStringA );

		return;
	}


		// OK, check if we are an immediate MAP feedback message, if so, do something else!
	if ( ( ubPriority == MSG_MAP_UI_POSITION_UPPER	) ||
			( ubPriority == MSG_MAP_UI_POSITION_MIDDLE ) ||
			( ubPriority == MSG_MAP_UI_POSITION_LOWER	) )
	{
		va_start(argptr, pStringA);			// Set up variable argument pointer
		vswprintf(DestString, pStringA, argptr);	// process gprintf string (get output str)
		va_end(argptr);

		BeginMapUIMessage( ubPriority, DestString );
		return;
	}


	if ( fFirstTimeInMessageSystem )
	{
		// Init display array!
		memset( gpDisplayList, 0, sizeof( gpDisplayList ) );
		fFirstTimeInMessageSystem = FALSE;
		//if(!(InitializeMutex(SCROLL_MESSAGE_MUTEX,"ScrollMessageMutex" )))
		//	return;
	}


	pStringSt=pStringS;
	while(GetNextString(pStringSt))
		 pStringSt=GetNextString(pStringSt);

	va_start(argptr, pStringA);			// Set up variable argument pointer
	vswprintf(DestString, pStringA, argptr);	// process gprintf string (get output str)
	va_end(argptr);

	if ( ubPriority == MSG_DEBUG )
	{
		#ifndef _DEBUG
			return;
		#endif
		usColor = DEBUG_COLOR;
		wcscpy( DestStringA, DestString );
		swprintf( DestString, L"Debug: %s", DestStringA );
	}

	if ( ubPriority == MSG_DIALOG )
	{
		usColor = DIALOGUE_COLOR;
	}

	if ( ubPriority == MSG_INTERFACE )
	{
		// HEADROCK HAM 3.6: Why force yellow? Let's not.
		//usColor = INTERFACE_COLOR;
	}

	// HEADROCK HAM 3.6: Allow for longer lines.
	// Lejardo ARSProject
	MAP_LINE_WIDTH = (INTERFACE_WIDTH - 330);

	pStringWrapperHead=LineWrap(uiFont, MAP_LINE_WIDTH, &usLineWidthIfWordIsWiderThenWidth, DestString);
	pStringWrapper=pStringWrapperHead;
	if(!pStringWrapper)
	return;

	fNewString = TRUE;

	while(pStringWrapper->pNextWrappedString!=NULL)
	{
		AddStringToMapScreenMessageList(pStringWrapper->sString, usColor, uiFont, fNewString, ubPriority );
		fNewString = FALSE;

		pStringWrapper=pStringWrapper->pNextWrappedString;
	}

	AddStringToMapScreenMessageList(pStringWrapper->sString, usColor, uiFont, fNewString, ubPriority );
	// HEADROCK HAM 3.6: Refresh screen bottom.
	fMapScreenBottomDirty = TRUE;
	//RenderMapScreenInterfaceBottom ( TRUE );

	// clear up list of wrapped strings
	ClearWrappedStrings( pStringWrapperHead );

	// play new message beep
	//PlayNewMessageSound( );

	MoveToEndOfMapScreenMessageList( );

	//LeaveMutex(SCROLL_MESSAGE_MUTEX, __LINE__, __FILE__);
}



// add string to the map screen message list
void AddStringToMapScreenMessageList( STR16 pString, UINT16 usColor, UINT32 uiFont, BOOLEAN fStartOfNewString, UINT8 ubPriority )
{
	ScrollStringStPtr pStringSt = NULL;


	pStringSt = (ScrollStringStPtr) MemAlloc(sizeof(ScrollStringSt));

	SetString(pStringSt, pString);
	SetStringColor(pStringSt, usColor);
	pStringSt->uiFont = uiFont;
	pStringSt->fBeginningOfNewString = fStartOfNewString;
	pStringSt->uiFlags = ubPriority;
	pStringSt->iVideoOverlay = -1;

	// next/previous are not used, it's strictly a wraparound queue
	SetStringNext(pStringSt, NULL);
	SetStringPrev(pStringSt, NULL);


	// Figure out which queue slot index we're going to use to store this
	// If queue isn't full, this is easy, if is is full, we'll re-use the oldest slot
	// Must always keep the wraparound in mind, although this is easy enough with a static, fixed-size queue.


	// always store the new message at the END index

	// check if slot is being used, if so, clear it up
	if( gMapScreenMessageList[ gubEndOfMapScreenMessageList ] != NULL )
	{
		MemFree( gMapScreenMessageList[ gubEndOfMapScreenMessageList ]->pString16 );
		MemFree( gMapScreenMessageList[ gubEndOfMapScreenMessageList ] );
	}

	// store the new message there
	gMapScreenMessageList[ gubEndOfMapScreenMessageList ] = pStringSt;

	// increment the end
	gubEndOfMapScreenMessageList = ( gubEndOfMapScreenMessageList + 1 ) % 256;

	// if queue is full, end will now match the start
	if ( gubEndOfMapScreenMessageList == gubStartOfMapScreenMessageList )
	{
		// if that's so, increment the start
		gubStartOfMapScreenMessageList = ( gubStartOfMapScreenMessageList + 1 ) % 256;
	}
}


void DisplayStringsInMapScreenMessageList( void )
{
	UINT8 ubCurrentStringIndex;
	UINT8	ubLinesPrinted;
	INT16 sY;
	UINT16 usSpacing;

	//SetFontDestBuffer( FRAME_BUFFER, 17, 360 + 6, 407, 360 + 101, FALSE );

	// CHRISL: Change both X paramters so they dynamically generate from right edge of screen
	//SetFontDestBuffer( FRAME_BUFFER, (SCREEN_WIDTH - 509), (SCREEN_HEIGHT - 114), (SCREEN_WIDTH - 233), (SCREEN_HEIGHT - 114) + 101, FALSE );
	// CHRISL: Use this setup if we want message box on the left side
	// HEADROCK HAM 3.6: Message window now as wide as possible. The money screen has been moved to the right side.
	SetFontDestBuffer( FRAME_BUFFER, (SCREEN_WIDTH - INTERFACE_WIDTH)/2 + 17, (SCREEN_HEIGHT - 114), (SCREEN_WIDTH - INTERFACE_WIDTH)/2 + (INTERFACE_WIDTH - 330), (SCREEN_HEIGHT - 114) + 101, FALSE );

	SetFont( MAP_SCREEN_MESSAGE_FONT );		// no longer supports variable fonts
	SetFontBackground( FONT_BLACK );
	SetFontShadow( DEFAULT_SHADOW );

	ubCurrentStringIndex = gubCurrentMapMessageString;

	//sY = 377;
	sY = (SCREEN_HEIGHT - 103);

	usSpacing = GetFontHeight( MAP_SCREEN_MESSAGE_FONT );

	for ( ubLinesPrinted = 0; ubLinesPrinted < MAX_MESSAGES_ON_MAP_BOTTOM; ubLinesPrinted++ )
	{
		// reached the end of the list?
		if ( ubCurrentStringIndex == gubEndOfMapScreenMessageList )
		{
			break;
		}

		// nothing stored there?
		if ( gMapScreenMessageList[ ubCurrentStringIndex ] == NULL )
		{
			break;
		}

		// set font color
		SetFontForeground( ( UINT8 )( gMapScreenMessageList[ ubCurrentStringIndex ]->usColor ) );

		// print this line
		// CHRISL: Change X parameter to dynamically generate from right edge of screen
		//mprintf_coded( (SCREEN_WIDTH - 506), sY, gMapScreenMessageList[ ubCurrentStringIndex ]->pString16 );
		// CHRISL: Use this line if we want to display from the left edge
		mprintf_coded( (SCREEN_WIDTH - INTERFACE_WIDTH)/2 + 20, sY, gMapScreenMessageList[ ubCurrentStringIndex ]->pString16 );

		sY = sY + usSpacing;

		// next message index to print (may wrap around)
		ubCurrentStringIndex = ( ubCurrentStringIndex + 1 ) % 256;
	}

	SetFontDestBuffer( FRAME_BUFFER, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, FALSE );
}


void EnableDisableScrollStringVideoOverlay( BOOLEAN fEnable )
{
	// will go through the list of video overlays for the tactical scroll message system, and enable/disable
	// video overlays depending on fEnable
	INT8 bCounter = 0;
	UINT8 ubLines = 0;

	if( guiCurrentScreen == MAP_SCREEN )
	{
		ubLines = MAX_LINE_COUNT;
	}
	else
	{
		ubLines = gGameExternalOptions.ubMaxMessagesTactical;
	}
	
	for( bCounter = 0; bCounter < ubLines; bCounter++ )
	{

		// if valid, enable/disable
		if( gpDisplayList[ bCounter ] != NULL )
		{
			EnableVideoOverlay( fEnable ,gpDisplayList[ bCounter ]->iVideoOverlay );
		}
	}

	return;

}


void PlayNewMessageSound( void )
{
	// play a new message sound, if there is one playing, do nothing
	static UINT32 uiSoundId = NO_SAMPLE;

	if( uiSoundId != NO_SAMPLE )
	{
		// is sound playing?..don't play new one
		if( SoundIsPlaying( uiSoundId ) == TRUE )
		{
			return;
		}
	}

	// otherwise no sound playing, play one
	uiSoundId = PlayJA2SampleFromFile( "Sounds\\newbeep.wav", RATE_11025, MIDVOLUME, 1 , MIDDLEPAN );

	return;
}


BOOLEAN SaveMapScreenMessagesToSaveGameFile( HWFILE hFile )
{
	UINT32	uiNumBytesWritten;
	UINT32	uiCount;
	UINT32	uiSizeOfString;
	StringSaveStruct StringSave;


	//	write to the begining of the message list
	FileWrite( hFile, &gubEndOfMapScreenMessageList, sizeof( UINT8 ), &uiNumBytesWritten );
	if( uiNumBytesWritten != sizeof( UINT8 ) )
	{
		return(FALSE);
	}

	FileWrite( hFile, &gubStartOfMapScreenMessageList, sizeof( UINT8 ), &uiNumBytesWritten );
	if( uiNumBytesWritten != sizeof( UINT8 ) )
	{
		return(FALSE);
	}

	//	write the current message string
	FileWrite( hFile, &gubCurrentMapMessageString, sizeof( UINT8 ), &uiNumBytesWritten );
	if( uiNumBytesWritten != sizeof( UINT8 ) )
	{
		return(FALSE);
	}


	//Loopthrough all the messages
	for( uiCount=0; uiCount<256; uiCount++)
	{
		if( gMapScreenMessageList[ uiCount ] )
		{
			uiSizeOfString = ( wcslen( gMapScreenMessageList[ uiCount ]->pString16 ) + 1 ) * 2;
		}
		else
			uiSizeOfString = 0;

		//	write to the file the size of the message
		FileWrite( hFile, &uiSizeOfString, sizeof( UINT32 ), &uiNumBytesWritten );
		if( uiNumBytesWritten != sizeof( UINT32 ) )
		{
			return(FALSE);
		}

		//if there is a message
		if( uiSizeOfString )
		{
			//	write the message to the file
			FileWrite( hFile, gMapScreenMessageList[ uiCount ]->pString16, uiSizeOfString, &uiNumBytesWritten );
			if( uiNumBytesWritten != uiSizeOfString )
			{
				return(FALSE);
			}

			// Create	the saved string struct
			StringSave.uiFont = gMapScreenMessageList[ uiCount ]->uiFont;
			StringSave.usColor = gMapScreenMessageList[ uiCount ]->usColor;
			StringSave.fBeginningOfNewString = gMapScreenMessageList[ uiCount ]->fBeginningOfNewString;
			StringSave.uiTimeOfLastUpdate = gMapScreenMessageList[ uiCount ]->uiTimeOfLastUpdate;
			StringSave.uiFlags= gMapScreenMessageList[ uiCount ]->uiFlags;


			//Write the rest of the message information to the saved game file
			FileWrite( hFile, &StringSave, sizeof( StringSaveStruct ), &uiNumBytesWritten );
			if( uiNumBytesWritten != sizeof( StringSaveStruct ) )
			{
				return(FALSE);
			}
		}

	}

	return( TRUE );
}


BOOLEAN LoadMapScreenMessagesFromSaveGameFile( HWFILE hFile )
{
	UINT32	uiNumBytesRead;
	UINT32	uiCount;
	UINT32	uiSizeOfString;
	StringSaveStruct StringSave;
	CHAR16	SavedString[ 512 ];

	// clear tactical message queue
	ClearTacticalMessageQueue( );

	gubEndOfMapScreenMessageList = 0;
	gubStartOfMapScreenMessageList = 0;
	gubCurrentMapMessageString = 0;

	//	Read to the begining of the message list
	FileRead( hFile, &gubEndOfMapScreenMessageList, sizeof( UINT8 ), &uiNumBytesRead );
	if( uiNumBytesRead != sizeof( UINT8 ) )
	{
		return(FALSE);
	}

	//	Read the current message string
	FileRead( hFile, &gubStartOfMapScreenMessageList, sizeof( UINT8 ), &uiNumBytesRead );
	if( uiNumBytesRead != sizeof( UINT8 ) )
	{
		return(FALSE);
	}

	//	Read the current message string
	FileRead( hFile, &gubCurrentMapMessageString, sizeof( UINT8 ), &uiNumBytesRead );
	if( uiNumBytesRead != sizeof( UINT8 ) )
	{
		return(FALSE);
	}

	//Loopthrough all the messages
	for( uiCount=0; uiCount<256; uiCount++)
	{
		//	Read to the file the size of the message
		FileRead( hFile, &uiSizeOfString, sizeof( UINT32 ), &uiNumBytesRead );
		if( uiNumBytesRead != sizeof( UINT32 ) )
		{
			return(FALSE);
		}

		//if there is a message
		if( uiSizeOfString )
		{
			//	Read the message from the file
			FileRead( hFile, SavedString, uiSizeOfString, &uiNumBytesRead );
			if( uiNumBytesRead != uiSizeOfString )
			{
				return(FALSE);
			}

			//if there is an existing string,delete it
			if( gMapScreenMessageList[ uiCount ] )
			{
				if( gMapScreenMessageList[ uiCount ]->pString16 )
				{
					MemFree( gMapScreenMessageList[ uiCount ]->pString16 );
					gMapScreenMessageList[ uiCount ]->pString16 = NULL;
				}
			}
			else
			{
				// There is now message here, add one
				ScrollStringSt	*sScroll;


				sScroll = (ScrollStringSt *) MemAlloc( sizeof( ScrollStringSt ) );
				if( sScroll == NULL )
					return( FALSE );

				memset( sScroll, 0, sizeof( ScrollStringSt ) );

				gMapScreenMessageList[ uiCount ] = sScroll;
			}

			//allocate space for the new string
			gMapScreenMessageList[ uiCount ]->pString16 = (STR16) MemAlloc( uiSizeOfString );
			if( gMapScreenMessageList[ uiCount ]->pString16 == NULL )
				return( FALSE );

			memset( gMapScreenMessageList[ uiCount ]->pString16, 0, uiSizeOfString);

			//copy the string over
			wcscpy( gMapScreenMessageList[ uiCount ]->pString16, SavedString );


			//Read the rest of the message information to the saved game file
			FileRead( hFile, &StringSave, sizeof( StringSaveStruct ), &uiNumBytesRead );
			if( uiNumBytesRead != sizeof( StringSaveStruct ) )
			{
				return(FALSE);
			}

			// Create	the saved string struct
			gMapScreenMessageList[ uiCount ]->uiFont = StringSave.uiFont;
			gMapScreenMessageList[ uiCount ]->usColor = StringSave.usColor;
			gMapScreenMessageList[ uiCount ]->uiFlags = StringSave.uiFlags;
			gMapScreenMessageList[ uiCount ]->fBeginningOfNewString = StringSave.fBeginningOfNewString;
			gMapScreenMessageList[ uiCount ]->uiTimeOfLastUpdate = StringSave.uiTimeOfLastUpdate;
		}
		else
			gMapScreenMessageList[ uiCount ] = NULL;

	}


	// this will set a valid value for gubFirstMapscreenMessageIndex, which isn't being saved/restored
	MoveToEndOfMapScreenMessageList();

	return( TRUE );
}


void HandleLastQuotePopUpTimer( void )
{
	if( ( fTextBoxMouseRegionCreated == FALSE ) || ( fDialogueBoxDueToLastMessage == FALSE ) )
	{
		return;
	}

	// check if timed out
	if( GetJA2Clock() - guiDialogueLastQuoteTime >	guiDialogueLastQuoteDelay )
	{
		// done clear up
		ShutDownLastQuoteTacticalTextBox( );
		guiDialogueLastQuoteTime = 0;
		guiDialogueLastQuoteDelay = 0;

	}
}


ScrollStringStPtr MoveToBeginningOfMessageQueue( void )
{
	ScrollStringStPtr pStringSt = pStringS;

	if( pStringSt == NULL )
	{
		return( NULL );
	}

	while( pStringSt->pPrev )
	{
		pStringSt = pStringSt->pPrev;
	}

	return( pStringSt );
}



INT32 GetMessageQueueSize( void )
{
	ScrollStringStPtr pStringSt = pStringS;
	INT32 iCounter = 0;

	pStringSt = MoveToBeginningOfMessageQueue( );

	while( pStringSt )
	{
		iCounter++;
		pStringSt = pStringSt->pNext;
	}

	return( iCounter );
}



void ClearTacticalMessageQueue( void )
{

	ScrollStringStPtr pStringSt = pStringS, pOtherStringSt = pStringS;

	ClearDisplayedListOfTacticalStrings( );

	// now run through all the tactical messages
	while( pStringSt )
	{
		pOtherStringSt = pStringSt;
		pStringSt = pStringSt->pNext;
		MemFree( pOtherStringSt->pString16 );
		MemFree( pOtherStringSt );
	}

	pStringS = NULL;

	return;
}

static struct DebugMessageLog {
	sgp::Logger_ID id;
	DebugMessageLog() {
		id = sgp::Logger::instance().createLogger();
		sgp::Logger::instance().connectFile(id, L"DebugMessage.txt", true, sgp::Logger::FLUSH_ON_ENDL);
	}
} s_DebugMessageLog;
void WriteMessageToFile( const STR16 pString )
{
#ifdef JA2BETAVERSION
#ifndef USE_VFS
	FILE *fp;

	fp = fopen( "DebugMessage.txt", "a" );

	if( fp == NULL )
	{
		return;
	}

	fprintf( fp, "%S\n", pString );
	fclose( fp );
#else
	SGP_LOG(s_DebugMessageLog.id, pString);
#endif
#endif
}



void InitGlobalMessageList( void )
{
	INT32 iCounter = 0;

	for( iCounter = 0; iCounter < 256; iCounter++ )
	{
		gMapScreenMessageList[ iCounter ] = NULL;
	}

	gubEndOfMapScreenMessageList = 0;
	gubStartOfMapScreenMessageList = 0;
	gubCurrentMapMessageString = 0;
//	ubTempPosition = 0;

	return;
}


void FreeGlobalMessageList( void )
{
	INT32 iCounter = 0;

	for( iCounter = 0; iCounter < 256; iCounter++ )
	{
		// check if next unit is empty, if not...clear it up
		if( gMapScreenMessageList[ iCounter ] != NULL )
		{
			MemFree( gMapScreenMessageList[ iCounter ]->pString16 );
			MemFree( gMapScreenMessageList[ iCounter ] );
		}
	}

	InitGlobalMessageList( );

	return;
}


UINT8 GetRangeOfMapScreenMessages( void )
{
	UINT8 ubRange = 0;

	// NOTE: End is non-inclusive, so start/end 0/0 means no messages, 0/1 means 1 message, etc.
	if( gubStartOfMapScreenMessageList <= gubEndOfMapScreenMessageList )
	{
		ubRange = gubEndOfMapScreenMessageList - gubStartOfMapScreenMessageList;
	}
	else
	{
		// this should always be 255 now, since this only happens when queue fills up, and we never remove any messages
		ubRange = ( gubEndOfMapScreenMessageList + 256 ) - gubStartOfMapScreenMessageList;
	}

	return ( ubRange );
}

// OJW - 20090426 - only show certain messages in the tactical / message overlay
void SetTacticalMessageFilter( UINT ubPriority )
{
	gTacticalMsgFilterPriority = ubPriority;
}

void RemoveTacticalMessageFilter ( void )
{
	gTacticalMsgFilterPriority = -1;
}



/*
BOOLEAN IsThereAnEmptySlotInTheMapScreenMessageList( void )
{
	// find if there is an empty slot

	if( gMapScreenMessageList[ ( UINT8 )( gubEndOfMapScreenMessageList + 1 ) ] != NULL )
	{
		return ( FALSE );
	}
	else
	{
		return( TRUE );
	}
}


UINT8 GetFirstEmptySlotInTheMapScreenMessageList( void )
{
	UINT8 ubSlotId = 0;

	// find first empty slot in list

	if( IsThereAnEmptySlotInTheMapScreenMessageList(	) == FALSE)
	{
		ubSlotId = gubEndOfMapScreenMessageList;
		return( ubSlotId );
	}
	else
	{
		// start at head of list
		ubSlotId = gubEndOfMapScreenMessageList;

		// run through list
		while( gMapScreenMessageList[ ubSlotId ] != NULL )
		{
			ubSlotId++;
		}

	}
	return( ubSlotId );
}



void SetCurrentMapScreenMessageString( UINT8 ubCurrentStringPosition )
{
	// will attempt to set current string to this value, or the closest one
	UINT8 ubCounter = 0;

	if( gMapScreenMessageList[ ubCurrentStringPosition ] == NULL )
	{
		// no message here, run down to nearest
		ubCounter = ubCurrentStringPosition;
		ubCounter--;

		while(	( gMapScreenMessageList[ ubCounter ] == NULL )&&( ubCounter != ubCurrentStringPosition ) )
		{
			if( ubCounter == 0 )
			{
				ubCounter = 255;
			}
			else
			{
				ubCounter--;
			}
		}

		ubCurrentStringPosition = ubCounter;

	}
	return;
}


UINT8 GetTheRelativePositionOfCurrentMessage( void )
{
	UINT8 ubPosition = 0;

	if( gubEndOfMapScreenMessageList > gubStartOfMapScreenMessageList)
	{
		ubPosition = gubCurrentMapMessageString - gubStartOfMapScreenMessageList;
	}
	else
	{
		ubPosition = ( 255 - gubStartOfMapScreenMessageList ) + gubCurrentMapMessageString;
	}


	return( ubPosition );
}




void MoveCurrentMessagePointerDownList( void )
{
	// check to see if we can move 'down' to newer messages?
	if( gMapScreenMessageList[ ( UINT8 )( gubCurrentMapMessageString	+ 1 )	] != NULL )
	{
		if(	( UINT8 ) ( gubCurrentMapMessageString + 1 ) != gubEndOfMapScreenMessageList )
		{
			if( ( AreThereASetOfStringsAfterThisIndex( gubCurrentMapMessageString, MAX_MESSAGES_ON_MAP_BOTTOM ) == TRUE ) )
			{
				gubCurrentMapMessageString++;
			}
		}
	}
}


void MoveCurrentMessagePointerUpList(void )
{
		// check to see if we can move 'down' to newer messages?
	if( gMapScreenMessageList[ ( UINT8 )( gubCurrentMapMessageString	- 1 )	] != NULL )
	{
		if( ( UINT8 ) ( gubCurrentMapMessageString - 1 ) != gubEndOfMapScreenMessageList )
		{
			gubCurrentMapMessageString--;
		}
	}

}



void ScrollToHereInMapScreenMessageList( UINT8 ubPosition )
{
	// a position ranging from 0 to 255 where 0 is top and 255 is bottom
	// get the range of messages, * ubPosition /255 and set current to this position
	UINT8 ubTestPosition = gubCurrentMapMessageString;
	UINT8 ubRange = 0;

	ubRange = GetRangeOfMapScreenMessages( );

	if( ubRange > 1 )
	{
		ubRange += 9;
	}

	ubTestPosition = ( UINT8 )( gubEndOfMapScreenMessageList - ( UINT8 )(	ubRange	) + (	( ( UINT8 )( ubRange )	* ubPosition ) / 256 ) );

	if( AreThereASetOfStringsAfterThisIndex( ubTestPosition, MAX_MESSAGES_ON_MAP_BOTTOM ) == TRUE )
	{
		gubCurrentMapMessageString = ubTestPosition;
	}

	ubTempPosition = ubTestPosition;

	return;
}


BOOLEAN AreThereASetOfStringsAfterThisIndex( UINT8 ubMsgIndex, INT32 iNumberOfStrings )
{
	INT32 iCounter;

	// go through this number of strings, if they pass, then we have at least iNumberOfStrings after index ubMsgIndex
	for( iCounter = 0; iCounter < iNumberOfStrings; iCounter++ )
	{
		// start checking AFTER this index, so skip ahead to the next index BEFORE checking
		if( ubMsgIndex < 255 )
		{
			ubMsgIndex++;
		}
		else
		{
			ubMsgIndex = 0;
		}

		if( gMapScreenMessageList[ ubMsgIndex ] == NULL )
		{
			return ( FALSE );
		}

		if( ubMsgIndex == gubEndOfMapScreenMessageList )
		{
			return( FALSE );
		}
	}

	return( TRUE );
}



UINT8 GetCurrentMessageValue( void )
{
	// return the value of the current message in the list, relative to the start of the list

	if( GetRangeOfMapScreenMessages( ) >= 255	)
	{
	return( gubCurrentMapMessageString - gubStartOfMapScreenMessageList );
	}
	else
	{
		return( gubCurrentMapMessageString );
	}
}



UINT8 GetCurrentTempMessageValue( void )
{
	if( GetRangeOfMapScreenMessages( ) >= 255	)
	{
		return( ubTempPosition - gubEndOfMapScreenMessageList );
	}
	else
	{
		return( ubTempPosition );
	}
}


UINT8 GetNewMessageValueGivenPosition( UINT8 ubPosition )
{
	// if we were to scroll to this position, what would current message index value be?

	return( ( UINT8 )( ( gubEndOfMapScreenMessageList - ( UINT8 )( GetRangeOfMapScreenMessages( ) ) ) + ( UINT8 )( ( GetRangeOfMapScreenMessages( ) * ubPosition ) / 255 ) ) );

}


BOOLEAN IsThisTheLastMessageInTheList( void )
{
	// is the current message the last message in the list?

	if( ( ( UINT8 )( gubCurrentMapMessageString + 1 ) ) == ( gubEndOfMapScreenMessageList ) && ( GetRangeOfMapScreenMessages( ) < 255 ) )
	{
		return( TRUE );
	}
	else if( gMapScreenMessageList[ ( UINT8 )( gubCurrentMapMessageString + 1 ) ] == NULL )
	{
		return( TRUE );
	}
	else
	{
		if( AreThereASetOfStringsAfterThisIndex( gubCurrentMapMessageString, MAX_MESSAGES_ON_MAP_BOTTOM ) == FALSE )
		{
			return( TRUE );
		}
		else
		{
			return ( FALSE );
		}
	}
}


BOOLEAN IsThisTheFirstMessageInTheList( void )
{
	// is the current message the first message in the list?

	if( ( gubCurrentMapMessageString ) == ( gubEndOfMapScreenMessageList ) )
	{
		return( TRUE );
	}
	else
	{
		return ( FALSE );
	}
}


void DisplayLastMessage( void )
{
	// start at end of list go back until message flag says dialogue
	UINT8 ubCounter = 0;
	BOOLEAN fNotDone = TRUE;
	BOOLEAN fFound = FALSE;
	BOOLEAN fSecondNewString = FALSE;
	CHAR16 sString[ 256 ];

	sString[ 0 ] = 0;


	// set counter to end of list
	while( ( gMapScreenMessageList[ ( UINT8 )( ubCounter	+ 1 )	] != NULL ) && ( ( UINT8 ) ( ubCounter + 1 ) != gubEndOfMapScreenMessageList ) )
	{
		ubCounter++;
	}

	// now start moving back until dialogue is found
	while( fNotDone )
	{
		if( ubCounter == gubEndOfMapScreenMessageList )
		{
			fNotDone = FALSE;
			fFound = FALSE;
			continue;
		}

		if( gMapScreenMessageList[ ubCounter ] == NULL )
		{
			fNotDone = FALSE;
			fFound = FALSE;
			continue;
		}
		// check if message if dialogue
		if( gMapScreenMessageList[ ubCounter ]->uiFlags == MSG_DIALOG )
		{
			if( gMapScreenMessageList[ ubCounter ]->fBeginningOfNewString == TRUE )
			{
				// yup
				fNotDone = FALSE;
				fFound = TRUE;

				// now display message
				continue;
			}
		}

		ubCounter--;
	}

	if( fFound == TRUE )
	{
		fNotDone = TRUE;

		while( fNotDone )
		{
			if( gMapScreenMessageList[ ubCounter ] )
			{
				if( ( fSecondNewString ) && ( gMapScreenMessageList[ ubCounter ]-> fBeginningOfNewString ) )
				{
					fNotDone = FALSE;
				}
				else if( gMapScreenMessageList[ ubCounter ]->uiFlags == MSG_DIALOG )
				{
					wcscat( sString, gMapScreenMessageList[ ubCounter ]->pString16 );
					wcscat( sString, L" " );
				}

				if( ( gMapScreenMessageList[ ubCounter ]-> fBeginningOfNewString ) )
				{
					fSecondNewString = TRUE;
				}

			}
			else
			{
				fNotDone = FALSE;
			}

			// the next string
			ubCounter++;
		}
		// execute text box
		ExecuteTacticalTextBoxForLastQuote( ( INT16 )( ( 640 - gusSubtitleBoxWidth ) / 2 ),	sString );
	}

	return;
}

*/



