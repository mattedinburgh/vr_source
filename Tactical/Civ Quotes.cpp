#ifdef PRECOMPILEDHEADERS
	#include "Tactical All.h"
#else
#include "builddefines.h"
#include <stdio.h>
#include "Types.h"
#include "civ quotes.h"
#include "mousesystem.h"
#include "strategicmap.h"
#include "WCheck.h"
#include "FileMan.h"
#include "encrypted file.h"
#include "MessageBoxScreen.h"
#include "Queen Command.h"
#include "Overhead.h"
#include "render dirty.h"
#include "merctextbox.h"
#include "ai.h"
#include "Text.h"
#include "screenids.h"
#include "Animation Data.h"
#include "Video.h"
#include "Font Control.h"
#include "message.h"
#include "local.h"
#include "renderworld.h"
#include "Interface.h"
#include "cursors.h"
#include "Dialogue Control.h"
#include "Quests.h"
#include "Strategic Town Loyalty.h"
#include "NPC.h"
#include "Strategic Mines.h"
#include "Random.h"
#endif
#include "connect.h"
#include "VRAnalytics.h"

// for enemy taunts
#include "Soldier Profile.h"
#include "Campaign.h"
#include "opplist.h"
#include "Items.h"
#include "Weapons.h"
#include "LOS.h"

// sevenfm: for voice taunts
#include "Sound Control.h"

#define			DIALOGUE_DEFAULT_WIDTH			200
#define			EXTREAMLY_LOW_TOWN_LOYALTY	20
#define			HIGH_TOWN_LOYALTY						80
#define			CIV_QUOTE_HINT							99

#define			MAX_APPLICABLE_TAUNTS			512

extern void CaptureTimerCallback( void );

BOOLEAN gfSurrendered = FALSE;

//--------------------------------------------------------------
//Not used 
typedef struct
{
	UINT16	ubNumEntries;
	UINT16	ubUnusedCurrentEntry; //Unused

} CIV_QUOTE;

CIV_QUOTE	gCivQuotes[ NUM_CIV_QUOTES]; //Not used 

UINT16	gubNumEntries[ NUM_CIV_QUOTES ] = // Not used 
{
	15,
	15,
	15,
	15,
	15,
	15,
	15,
	15,
	15,
	15,

	15,
	15,
	15,
	15,
	15,
	15,
	15,
	15,
	15,
	15,

	5,
	5,
	15,
	15,
	15,
	15,
	15,
	15,
	15,
	15,

	15,
	15,
	2,
	15,
	15,
	10,
	10,
	5,
	3,
	10,

	3,
	3,
	3,
	3,
	3,
	3,
	3,
	3,
	3,
	3
};
//--------------------------------------------------------------

typedef struct
{
	BOOLEAN				bActive;
	MOUSE_REGION	MouseRegion;
	INT32					iVideoOverlay;
	INT32					iDialogueBox;
	UINT32				uiTimeOfCreation;
	UINT32				uiDelayTime;
	SOLDIERTYPE *	pCiv;
} QUOTE_SYSTEM_STRUCT;


QUOTE_SYSTEM_STRUCT	gCivQuoteData;

CHAR16	gzCivQuote[ 320 ];
UINT16	gusCivQuoteBoxWidth;
UINT16	gusCivQuoteBoxHeight;

// anv: store times, when enemy taunt will be finished (so they won't taunt 50 times / second)
UINT32	uiTauntFinishTimes[ TOTAL_SOLDIERS ];

// VR battlefield communication state. One display gate plus one pending
// high-priority semantic callout keeps large sectors readable without silently
// losing important grenade/medic/withdrawal warnings.
static UINT32 guiLastAIActionPopupTime = 0;
static UINT8 gubActiveAICombatCalloutPriority = 0;
static UINT8 gubLastAICombatCalloutEvent[ TOTAL_SOLDIERS ];
static UINT32 guiLastAICombatCalloutEventTime[ TOTAL_SOLDIERS ];

typedef struct
{
	BOOLEAN fActive;
	UINT8 ubSoldierID;
	AI_BATTLE_CALLOUT ubCallout;
	UINT8 ubPriority;
	UINT32 uiQueuedAt;
} AI_PENDING_CALLOUT;

static AI_PENDING_CALLOUT gPendingAICombatCallout;
static void FlushPendingAICombatCallout();

TAUNT_VALUES zApplicableTaunts[NUM_TAUNT];

//--------------------------------------------------------------
void CopyNumEntriesIntoQuoteStruct( ) //  Not used 
{
	INT32	cnt;

	for ( cnt = 0; cnt < NUM_CIV_QUOTES; cnt++ )
	{	
		
		if (cnt <= 50) 
			gCivQuotes[ cnt ].ubNumEntries = gubNumEntries[ cnt ];
		else 
			gCivQuotes[ cnt ].ubNumEntries = 15;
	}

}
//--------------------------------------------------------------

BOOLEAN GetCivQuoteText(UINT16 ubCivQuoteID, UINT16 ubEntryID, STR16 zQuote )
{
	CHAR8 zFileName[164];

	// Build filename....
	if ( ubCivQuoteID == CIV_QUOTE_HINT )
	{
	if ( gbWorldSectorZ > 0 )
	{
		//sprintf( zFileName, "NPCData\\miners.edt" );
			sprintf( zFileName,"NPCDATA\\CIV%02d.edt", CIV_QUOTE_MINERS_NOT_FOR_PLAYER );
	}
	else
	{
		sprintf( zFileName, "NPCData\\%c%d.edt", 'A' + (gWorldSectorY - 1) , gWorldSectorX );
	}
	}
	else
	{
		if (ubCivQuoteID <= 9)
			sprintf( zFileName,"NPCDATA\\CIV%02d.edt",ubCivQuoteID );
		else
			sprintf( zFileName,"NPCDATA\\CIV%d.edt",ubCivQuoteID );
	}

	CHECKF( FileExists( zFileName ) );

	// Get data...
	LoadEncryptedDataFromFile( zFileName, zQuote, ubEntryID * 320, 320 );

	if( zQuote[0] == 0 )
	{
		return( FALSE );
	}

	return( TRUE );
}

void SurrenderMessageBoxCallBack( UINT8 ubExitValue )
{
	SOLDIERTYPE *pTeamSoldier;
	INT32				cnt = 0;

	if ( ubExitValue == MSG_BOX_RETURN_YES )
	{
		// CJC Dec 1 2002: fix multiple captures
		BeginCaptureSquence();

	// Do capture....
		cnt = gTacticalStatus.Team[ gbPlayerNum ].bFirstID;

		for ( pTeamSoldier = MercPtrs[ cnt ]; cnt <= gTacticalStatus.Team[ gbPlayerNum ].bLastID; cnt++,pTeamSoldier++)
		{
			// Are we active and in sector.....
			if ( pTeamSoldier->bActive && pTeamSoldier->bInSector )
			{
		if ( pTeamSoldier->stats.bLife != 0 )
				{
					EnemyCapturesPlayerSoldier( pTeamSoldier );

					RemoveSoldierFromTacticalSector( pTeamSoldier, TRUE );
				}
			}
	}

		EndCaptureSequence( );

		gfSurrendered = TRUE;
		SetCustomizableTimerCallbackAndDelay( 3000, CaptureTimerCallback, FALSE );

		ActionDone( gCivQuoteData.pCiv );
	}
	else
	{
		ActionDone( gCivQuoteData.pCiv );
	}
}

void ShutDownQuoteBox( BOOLEAN fForce )
{
	// Combat callouts reuse the legacy quote overlay for rendering only; they must
	// not inherit narrative side effects such as the surrender confirmation path.
	BOOLEAN fClosingAICombatCallout = (gubActiveAICombatCalloutPriority > 0);

	if ( !gCivQuoteData.bActive )
	{
		return;
	}

	// Check for min time....
	if ( ( GetJA2Clock( ) - gCivQuoteData.uiTimeOfCreation ) > 300 || fForce )
	{
		RemoveVideoOverlay( gCivQuoteData.iVideoOverlay );

		// Remove mouse region...
		MSYS_RemoveRegion( &(gCivQuoteData.MouseRegion) );

	RemoveMercPopupBoxFromIndex( gCivQuoteData.iDialogueBox );
	gCivQuoteData.iDialogueBox = -1;

		gCivQuoteData.bActive = FALSE;
		gubActiveAICombatCalloutPriority = 0;
#ifdef JA2UB
// no UB
#else
		// do we need to do anything at the end of the civ quote?
		if ( !fClosingAICombatCallout && gCivQuoteData.pCiv &&
			gCivQuoteData.pCiv->aiData.bAction == AI_ACTION_OFFER_SURRENDER )
		{
// Haydent
			if(!is_networked)
			{
				DoMessageBox( MSG_BOX_BASIC_STYLE, Message[ STR_SURRENDER ], GAME_SCREEN, ( UINT8 )MSG_BOX_FLAG_YESNO, SurrenderMessageBoxCallBack, NULL );
			}
			else 
			{
				ScreenMsg( FONT_LTGREEN, MSG_MPSYSTEM, MPClientMessage[39] );
				ActionDone( gCivQuoteData.pCiv );
			}
		}
#endif
	}
}

BOOLEAN ShutDownQuoteBoxIfActive( )
{
	if ( gCivQuoteData.bActive )
	{
		ShutDownQuoteBox( TRUE );

		return( TRUE );
	}

	return( FALSE );
}


INT8 GetCivType( SOLDIERTYPE *pCiv )
{
	if ( pCiv->ubProfile != NO_PROFILE )
	{
		return( CIV_TYPE_NA );
	}

	// ATE: Check if this person is married.....
	// 1 ) check sector....
	if ( gWorldSectorX == 10 && gWorldSectorY == 6 && gbWorldSectorZ == 0 )
	{
	// 2 ) the only female....
	if ( pCiv->ubCivilianGroup == 0 && pCiv->bTeam != gbPlayerNum && pCiv->ubBodyType == REGFEMALE )
		{
			// She's a ho!
			return( CIV_TYPE_MARRIED_PC );
		}
	}

	// OK, look for enemy type - MUST be on enemy team, merc bodytype
	if ( pCiv->bTeam == ENEMY_TEAM && IS_MERC_BODY_TYPE( pCiv ) )
	{
		return( CIV_TYPE_ENEMY );
	}

	if ( pCiv->bTeam != CIV_TEAM && pCiv->bTeam != MILITIA_TEAM )
	{
		return( CIV_TYPE_NA );
	}

	switch( pCiv->ubBodyType )
	{
		case REGMALE:
		case BIGMALE:
		case STOCKYMALE:
		case REGFEMALE:
		case FATCIV:
		case MANCIV:
		case MINICIV:
		case DRESSCIV:
		case CRIPPLECIV:

			return( CIV_TYPE_ADULT );
			break;

		case ADULTFEMALEMONSTER:
		case AM_MONSTER:
		case YAF_MONSTER:
		case YAM_MONSTER:
		case LARVAE_MONSTER:
		case INFANT_MONSTER:
		case QUEENMONSTER:

			return( CIV_TYPE_NA );

		case HATKIDCIV:
		case KIDCIV:

			return( CIV_TYPE_KID );

		default:

			return( CIV_TYPE_NA );
	}

	//return( CIV_TYPE_NA ); // not needed when there is a default! (jonathanl)
}


void RenderCivQuoteBoxOverlay( VIDEO_OVERLAY *pBlitter )
{
	if ( gCivQuoteData.iVideoOverlay != -1 )
	{
		RenderMercPopUpBoxFromIndex( gCivQuoteData.iDialogueBox, pBlitter->sX, pBlitter->sY,	pBlitter->uiDestBuff );

		InvalidateRegion( pBlitter->sX, pBlitter->sY, pBlitter->sX + gusCivQuoteBoxWidth, pBlitter->sY + gusCivQuoteBoxHeight );
	}
}


void QuoteOverlayClickCallback( MOUSE_REGION * pRegion, INT32 iReason )
{
	static BOOLEAN fLButtonDown = FALSE;

	if (iReason & MSYS_CALLBACK_REASON_LBUTTON_DWN )
	{
		fLButtonDown = TRUE;
	}

	if (iReason & MSYS_CALLBACK_REASON_LBUTTON_UP && fLButtonDown )
	{
		// Shutdown quote box....
		ShutDownQuoteBox( FALSE );
	}
	else if (iReason & MSYS_CALLBACK_REASON_LOST_MOUSE )
	{
		fLButtonDown = FALSE;
	}
}


void BeginCivQuote( SOLDIERTYPE *pCiv, UINT16 ubCivQuoteID, UINT16 ubEntryID, INT16 sX, INT16 sY )
{
	VIDEO_OVERLAY_DESC		VideoOverlayDesc;
	CHAR16					zQuote[ 320 ];	
	
	// OK, do we have another on?
	if ( gCivQuoteData.bActive )
	{
		// Delete?
		ShutDownQuoteBox( TRUE );
	}
	
	// get text
	if ( !GetCivQuoteText( ubCivQuoteID, ubEntryID, zQuote ) )
	{
		return;
	}

	swprintf( gzCivQuote, L"\"%s\"", zQuote );

	if ( ubCivQuoteID == CIV_QUOTE_HINT )
	{
		MapScreenMessage( FONT_MCOLOR_WHITE, MSG_DIALOG, L"%s",	gzCivQuote );
	}

	// Create video oeverlay....
	memset( &VideoOverlayDesc, 0, sizeof( VIDEO_OVERLAY_DESC ) );

	//never use it anymore
	//SET_USE_WINFONTS( TRUE );
	//SET_WINFONT( giSubTitleWinFont );
	// Prepare text box
	gCivQuoteData.iDialogueBox = PrepareMercPopupBox( gCivQuoteData.iDialogueBox , BASIC_MERC_POPUP_BACKGROUND, BASIC_MERC_POPUP_BORDER, gzCivQuote, DIALOGUE_DEFAULT_WIDTH, 0, 0, 0, &gusCivQuoteBoxWidth, &gusCivQuoteBoxHeight );
	//SET_USE_WINFONTS( FALSE );


	// OK, find center for box......
	sX = sX - ( gusCivQuoteBoxWidth / 2 );
	sY = sY - ( gusCivQuoteBoxHeight / 2 );

	// OK, limit to screen......
	{
		if ( sX < 0 )
		{
			sX = 0;
		}

		// CHECK FOR LEFT/RIGHT
		if ( ( sX + gusCivQuoteBoxWidth ) > SCREEN_WIDTH )
		{
			sX = SCREEN_WIDTH - gusCivQuoteBoxWidth;
		}

		// Now check for top
		if ( sY < gsVIEWPORT_WINDOW_START_Y )
		{
			sY = gsVIEWPORT_WINDOW_START_Y;
		}

		// Check for bottom
		if ( ( sY + gusCivQuoteBoxHeight ) > (SCREEN_HEIGHT - INV_INTERFACE_HEIGHT))
		{
			sY = (SCREEN_HEIGHT - INV_INTERFACE_HEIGHT) - gusCivQuoteBoxHeight;
		}
	}

	VideoOverlayDesc.sLeft			= sX;
	VideoOverlayDesc.sTop				= sY;
	VideoOverlayDesc.sRight			= VideoOverlayDesc.sLeft + gusCivQuoteBoxWidth;
	VideoOverlayDesc.sBottom		= VideoOverlayDesc.sTop + gusCivQuoteBoxHeight;
	VideoOverlayDesc.sX					= VideoOverlayDesc.sLeft;
	VideoOverlayDesc.sY					= VideoOverlayDesc.sTop;
	VideoOverlayDesc.BltCallback = RenderCivQuoteBoxOverlay;

	gCivQuoteData.iVideoOverlay =	RegisterVideoOverlay( 0, &VideoOverlayDesc );


	//Define main region
	MSYS_DefineRegion( &(gCivQuoteData.MouseRegion), VideoOverlayDesc.sLeft, VideoOverlayDesc.sTop,	VideoOverlayDesc.sRight, VideoOverlayDesc.sBottom, MSYS_PRIORITY_HIGHEST,
						CURSOR_NORMAL, MSYS_NO_CALLBACK, QuoteOverlayClickCallback );
	// Add region
	MSYS_AddRegion( &(gCivQuoteData.MouseRegion) );


	gCivQuoteData.bActive = TRUE;

	gCivQuoteData.uiTimeOfCreation = GetJA2Clock( );

	gCivQuoteData.uiDelayTime = FindDelayForString( gzCivQuote ) + 500;

	gCivQuoteData.pCiv = pCiv;

}

UINT16 DetermineCivQuoteEntry( SOLDIERTYPE *pCiv, UINT16 *pubCivHintToUse, BOOLEAN fCanUseHints )
{
	UINT8	ubCivType;
#ifdef JA2UB

#else
	INT8	bTownId;
	INT8		bCivHint;
	INT8	bMineId;
#endif

	BOOLEAN	bCivLowLoyalty = FALSE;
	BOOLEAN	bCivHighLoyalty = FALSE;

	BOOLEAN bMiners = FALSE;
	UINT16 iCounter2;
	UINT16 FileEDTQUoteID;
	
	(*pubCivHintToUse) = 0;

	ubCivType = GetCivType( pCiv );
	
	
	for( iCounter2 = NON_CIV_GROUP; iCounter2 < NUM_CIV_GROUPS; iCounter2++ )
		{	
#ifdef JA2UB
			if (pCiv->ubCivilianGroup > UNNAMED_CIV_GROUP_19 && pCiv->ubCivilianGroup == iCounter2)
#else
			if (pCiv->ubCivilianGroup > QUEENS_CIV_GROUP && pCiv->ubCivilianGroup == iCounter2)
#endif
			{
				if ( pCiv->aiData.bNeutral )
					{
						return( FileEDTQUoteID = iCounter2*2 +10);
					}
					else
					{
						return( FileEDTQUoteID = iCounter2*2 + 11);
					}
			}	
		}
		
#ifdef JA2UB		
	if( ubCivType != CIV_TYPE_ENEMY )
	{
		//if the civ is not an enemy
		if ( pCiv->aiData.bNeutral )
		{
			return( CIV_QUOTE__CIV_NOT_ENEMY ); //43
		}
		else
		{
			//
			//the civ is an enemy
			//

			//if the civ can fight
			if( pCiv->ubBodyType == REGMALE || pCiv->ubBodyType == REGFEMALE || pCiv->ubBodyType == BIGMALE )
			{
				return( CIV_QUOTE__CIV_ENEMY_CAN_FIGHT); //40 
			}
			else if( pCiv->stats.bLife < pCiv->stats.bLifeMax )
			{
				return( CIV_QUOTE__CIV_HURT ); //42
			}
			else
			{
				return( CIV_QUOTE__CIV_ENEMY_GENERIC ); //41
			}
		}
	}


	if( ubCivType == CIV_TYPE_ENEMY )
	{
		// Determine what type of quote to say...
		// Are are we going to attack?

		if (	pCiv->aiData.bAction == AI_ACTION_TOSS_PROJECTILE 
			||	pCiv->aiData.bAction == AI_ACTION_FIRE_GUN 
			||	pCiv->aiData.bAction == AI_ACTION_THROW_KNIFE 
			||	pCiv->aiData.bAction == AI_ACTION_KNIFE_MOVE )
		{
			return( CIV_QUOTE_ENEMY_THREAT );
		}

		// Hurt?
		else if ( pCiv->stats.bLife < 30 )
		{
			return( CIV_QUOTE_ENEMY_HURT );
		}
		// elite?
		else if ( pCiv->ubSoldierClass == SOLDIER_CLASS_ELITE )
		{
			return( CIV_QUOTE_ENEMY_ELITE );
		}
		else
		{
			return( CIV_QUOTE_ENEMY_ADMIN );
		}
	}

	return( 255 );
#else	
			
	if ( ubCivType == CIV_TYPE_ENEMY )
	{
		// Determine what type of quote to say...
		// Are are we going to attack?

		if ( pCiv->aiData.bAction == AI_ACTION_TOSS_PROJECTILE || pCiv->aiData.bAction == AI_ACTION_FIRE_GUN ||
							pCiv->aiData.bAction == AI_ACTION_FIRE_GUN || pCiv->aiData.bAction == AI_ACTION_KNIFE_MOVE )
		{
			return( CIV_QUOTE_ENEMY_THREAT );
		}
		else if ( pCiv->aiData.bAction == AI_ACTION_OFFER_SURRENDER )
		{
			return( CIV_QUOTE_ENEMY_OFFER_SURRENDER );
		}
		// Hurt?
		else if ( pCiv->stats.bLife < 30 )
		{
			return( CIV_QUOTE_ENEMY_HURT );
		}
		// elite?
		else if ( pCiv->ubSoldierClass == SOLDIER_CLASS_ELITE )
		{
			return( CIV_QUOTE_ENEMY_ELITE );
		}
		else
		{
			return( CIV_QUOTE_ENEMY_ADMIN );
		}
	}

	// Are we in a town sector?
	// get town id
	bTownId = GetTownIdForSector( gWorldSectorX, gWorldSectorY );


	// If a married PC...
	if ( ubCivType == CIV_TYPE_MARRIED_PC )
	{
		return( CIV_QUOTE_PC_MARRIED );
	}

	// CIV GROUPS FIRST!
	// Hicks.....
	if ( pCiv->ubCivilianGroup == HICKS_CIV_GROUP )
	{
		// Are they friendly?
		//if ( gTacticalStatus.fCivGroupHostile[ HICKS_CIV_GROUP ] < CIV_GROUP_WILL_BECOME_HOSTILE )
		if ( pCiv->aiData.bNeutral )
		{
			return( CIV_QUOTE_HICKS_FRIENDLY );
		}
		else
		{
			return( CIV_QUOTE_HICKS_ENEMIES );
		}
	}

	// Goons.....
	if ( pCiv->ubCivilianGroup == KINGPIN_CIV_GROUP )
	{
		// Are they friendly?
		//if ( gTacticalStatus.fCivGroupHostile[ KINGPIN_CIV_GROUP ] < CIV_GROUP_WILL_BECOME_HOSTILE )
		if ( pCiv->aiData.bNeutral )
		{
			return( CIV_QUOTE_GOONS_FRIENDLY );
		}
		else
		{
			return( CIV_QUOTE_GOONS_ENEMIES );
		}
	}

	// anv: VR
	if (pCiv->ubCivilianGroup == WARDEN_CIV_GROUP)
	{
		// Are they friendly?
		if (pCiv->aiData.bNeutral)
		{
			return(CIV_QUOTE_WARDEN_FRIENDLY);
		}
		else
		{
			return(CIV_QUOTE_WARDEN_ENEMIES);
		}
	}

	// anv: VR
	if (pCiv->ubCivilianGroup == KINGPIN_FORT_CIV_GROUP)
	{
		// Are they friendly?
		if (pCiv->aiData.bNeutral)
		{
			return(CIV_QUOTE_GOONS_FORT_FRIENDLY);
		}
		else
		{
			return(CIV_QUOTE_GOONS_FORT_ENEMIES);
		}
	}

	// ATE: Cowering people take precedence....
	if ( ( pCiv->flags.uiStatusFlags & SOLDIER_COWERING ) || ( pCiv->bTeam == CIV_TEAM && ( gTacticalStatus.uiFlags & INCOMBAT ) ) )
	{
		if ( ubCivType == CIV_TYPE_ADULT )
		{
			return( CIV_QUOTE_ADULTS_COWER );
		}
		else
		{
			return( CIV_QUOTE_KIDS_COWER );
		}
	}

	// Kid slaves...
	if ( pCiv->ubCivilianGroup == FACTORY_KIDS_GROUP )
	{
		// Check fact.....
		if ( CheckFact( FACT_DOREEN_HAD_CHANGE_OF_HEART, 0 ) || !CheckFact( FACT_DOREEN_ALIVE, 0 ) )
		{
			return( CIV_QUOTE_KID_SLAVES_FREE );
		}
		else
		{
			return( CIV_QUOTE_KID_SLAVES );
		}
	}

	// BEGGERS
	if ( pCiv->ubCivilianGroup == BEGGARS_CIV_GROUP )
	{
		// Check if we are in a town...
		if( bTownId != BLANK_SECTOR && gbWorldSectorZ == 0 )
		{
			if ( bTownId == SAN_MONA && ubCivType == CIV_TYPE_ADULT )
			{
				return( CIV_QUOTE_SAN_MONA_BEGGERS );
			}
		}

		// DO normal beggers...
		if ( ubCivType == CIV_TYPE_ADULT )
		{
			return( CIV_QUOTE_ADULTS_BEGGING );
		}
		else
		{
			return( CIV_QUOTE_KIDS_BEGGING );
		}
	}

	// REBELS
	if ( pCiv->ubCivilianGroup == REBEL_CIV_GROUP )
	{
		// DO normal beggers...
		if ( ubCivType == CIV_TYPE_ADULT )
		{
			return( CIV_QUOTE_ADULTS_REBELS );
		}
		else
		{
			return( CIV_QUOTE_KIDS_REBELS );
		}
	}

	// Do miltitia...
	if ( pCiv->bTeam == MILITIA_TEAM )
	{
		// Different types....
		if ( pCiv->ubSoldierClass == SOLDIER_CLASS_GREEN_MILITIA )
		{
			return( CIV_QUOTE_GREEN_MILITIA );
		}
		if ( pCiv->ubSoldierClass == SOLDIER_CLASS_REG_MILITIA )
		{
			return( CIV_QUOTE_MEDIUM_MILITIA );
		}
		if ( pCiv->ubSoldierClass == SOLDIER_CLASS_ELITE_MILITIA )
		{
			return( CIV_QUOTE_ELITE_MILITIA );
		}
	}

	// If we are in medunna, and queen is dead, use these...
	if ( bTownId == MEDUNA && CheckFact( FACT_QUEEN_DEAD, 0 ) )
	{
	return( CIV_QUOTE_DEIDRANNA_DEAD );
	}

	// if in a town
	if( ( bTownId != BLANK_SECTOR ) && ( gbWorldSectorZ == 0 ) && gfTownUsesLoyalty[ bTownId ] )
	{
		// Check loyalty special quotes.....
		// EXTREMELY LOW TOWN LOYALTY...
		if ( gTownLoyalty[ bTownId ].ubRating < EXTREAMLY_LOW_TOWN_LOYALTY )
		{
			bCivLowLoyalty = TRUE;
		}

		// HIGH TOWN LOYALTY...
		if ( gTownLoyalty[ bTownId ].ubRating >= HIGH_TOWN_LOYALTY )
		{
			bCivHighLoyalty = TRUE;
		}
	}


	// ATE: OK, check if we should look for a civ hint....
	if ( fCanUseHints )
	{
	bCivHint = ConsiderCivilianQuotes( gWorldSectorX, gWorldSectorY, gbWorldSectorZ,	FALSE );
	}
	else
	{
	bCivHint = -1;
	}

	// ATE: check miners......
	if ( pCiv->ubSoldierClass == SOLDIER_CLASS_MINER )
	{
	bMiners = TRUE;

	// If not a civ hint available...
	if ( bCivHint == -1 )
	{
		// Check if they are under our control...

		// Should I go talk to miner?
		// Not done yet.

		// Are they working for us?
		bMineId = GetIdOfMineForSector( gWorldSectorX, gWorldSectorY, gbWorldSectorZ );

		// anv: VR - separate sets for oil rig workers and miners
		UINT8 ubSector = SECTOR(gWorldSectorX, gWorldSectorY);
		BOOLEAN bIsOilRig = FALSE;
		for (UINT16 cnt = 0; cnt < NUM_FACILITY_TYPES; cnt++)
		{
			if (gFacilityLocations[ubSector][cnt].fFacilityHere && gFacilityTypes[cnt].AssignmentData[FAC_MANAGE_OIL_RIG].usMineIncomeModifier > 0)
			{
				bIsOilRig = TRUE;
				break;
			}
		}

		if (bIsOilRig)
		{
			if (PlayerControlsMine(bMineId))
			{
				return(CIV_QUOTE_MINERS_FOR_PLAYER_OIL_RIG);
			}
			else
			{
				return(CIV_QUOTE_MINERS_NOT_FOR_PLAYER_OIL_RIG);
			}
		}

		if ( PlayerControlsMine( bMineId ) )
		{
		return( CIV_QUOTE_MINERS_FOR_PLAYER );
		}
		else
		{
		return( CIV_QUOTE_MINERS_NOT_FOR_PLAYER );
		}
	}
	}


	// Is one availible?
	// If we are to say low loyalty, do chance
	if ( bCivHint != -1 && bCivLowLoyalty && !bMiners )
	{
		if ( Random( 100 ) < 25 )
		{
			// Get rid of hint...
			bCivHint = -1;
		}
	}

	// Say hint if availible...
	if ( bCivHint != -1 )
	{
		if ( ubCivType == CIV_TYPE_ADULT )
		{
			(*pubCivHintToUse) = bCivHint;

			// Set quote as used...
			ConsiderCivilianQuotes( gWorldSectorX, gWorldSectorY, gbWorldSectorZ, TRUE );

			// retrun value....
			return( CIV_QUOTE_HINT );
		}
	}

	if ( bCivLowLoyalty )
	{
		if ( ubCivType == CIV_TYPE_ADULT )
		{
			return( CIV_QUOTE_ADULTS_EXTREMLY_LOW_LOYALTY );
		}
		else
		{
			return( CIV_QUOTE_KIDS_EXTREMLY_LOW_LOYALTY );
		}
	}

	if ( bCivHighLoyalty )
	{
		if ( ubCivType == CIV_TYPE_ADULT )
		{
			return( CIV_QUOTE_ADULTS_HIGH_LOYALTY );
		}
		else
		{
			return( CIV_QUOTE_KIDS_HIGH_LOYALTY );
		}
	}


	// All purpose quote here....
	if ( ubCivType == CIV_TYPE_ADULT )
	{
		return( CIV_QUOTE_ADULTS_ALL_PURPOSE );
	}
	else
	{
		return( CIV_QUOTE_KIDS_ALL_PURPOSE );
	}
#endif
}


void HandleCivQuote( )
{
	if ( gCivQuoteData.bActive )
	{
		// Check for min time....
		if ( ( GetJA2Clock( ) - gCivQuoteData.uiTimeOfCreation ) > gCivQuoteData.uiDelayTime )
		{
			// Stop!
			ShutDownQuoteBox( TRUE );
		}
	}

	if ( !gCivQuoteData.bActive )
		FlushPendingAICombatCallout();
}

void StartCivQuote( SOLDIERTYPE *pCiv )
{
	UINT16 ubCivQuoteID;
	INT16	sX, sY;
	UINT16	ubEntryID = 0;
	INT16	sScreenX, sScreenY;
	UINT16	ubCivHintToUse;
	UINT16 CivQuoteDelta = 0;
	
	UINT16 ubCivQuoteID2;
	UINT16 RandomVal;
	
	// ATE: Check for old quote.....
	// This could have been stored on last attempt...
	if ( pCiv->bCurrentCivQuote == CIV_QUOTE_HINT )
	{
		// Determine which quote to say.....
		// CAN'T USE HINTS, since we just did one...
		pCiv->bCurrentCivQuote = -1;
		pCiv->bCurrentCivQuoteDelta = 0;
		ubCivQuoteID = DetermineCivQuoteEntry( pCiv, &ubCivHintToUse, FALSE );
	}
	else
	{
		// Determine which quote to say.....
		ubCivQuoteID = DetermineCivQuoteEntry( pCiv, &ubCivHintToUse, TRUE );
	}
	
	if (ubCivQuoteID == CIV_QUOTE_ADULTS_REBELS || ubCivQuoteID == CIV_QUOTE_KIDS_REBELS || ubCivQuoteID == CIV_QUOTE_ENEMY_OFFER_SURRENDER ) 
	{
		RandomVal = 5;
	}
	else if (ubCivQuoteID == CIV_QUOTE_PC_MARRIED) 
	{
		RandomVal = 2;
	}
	else if (ubCivQuoteID == CIV_QUOTE_HICKS_SEE_US_AT_NIGHT) 
	{
		RandomVal = 3;
	}
	else 
		RandomVal = 15;

#ifdef JA2UB		
	if( ubCivQuoteID == 255 )
	{
		return;
	}
#endif	
	
	// Determine entry id
	// ATE: Try and get entry from soldier pointer....
	if ( ubCivQuoteID != CIV_QUOTE_HINT )
	{
		if ( pCiv->bCurrentCivQuote == -1 )
		{
			// Pick random one
			//pCiv->bCurrentCivQuote = (INT8)Random( gCivQuotes[ ubCivQuoteID ].ubNumEntries - 2 );
			ubCivQuoteID2  = Random(RandomVal-2);
			pCiv->bCurrentCivQuoteDelta = 0;
		}
		else
		{
			ubCivQuoteID2 = pCiv->bCurrentCivQuote;
		}

		//ubEntryID	= pCiv->bCurrentCivQuote + pCiv->bCurrentCivQuoteDelta;
		ubEntryID	= ubCivQuoteID2 + pCiv->bCurrentCivQuoteDelta;
	}
	else
	{
		ubEntryID =ubCivHintToUse;

		// ATE: set value for quote ID.....
		//pCiv->bCurrentCivQuote			= ubCivQuoteID;
		ubCivQuoteID2 = ubCivQuoteID;
		CivQuoteDelta = ubEntryID;
		//pCiv->bCurrentCivQuoteDelta = ubEntryID;

	}

	// Flugente: if we are an assassin, we speak like the militia we emulate
	if ( pCiv->usSoldierFlagMask & SOLDIER_ASSASSIN )
	{
		switch ( pCiv->GetUniformType() )
		{
		case UNIFORM_MILITIA_REGULAR:
			ubCivQuoteID = CIV_QUOTE_MEDIUM_MILITIA;
			break;
		case UNIFORM_MILITIA_ELITE:
			ubCivQuoteID = CIV_QUOTE_ELITE_MILITIA;
			break;
		default:
			ubCivQuoteID = CIV_QUOTE_GREEN_MILITIA;
			break;
		}
	}

	// Determine location...
	// Get location of civ on screen.....
	GetSoldierScreenPos( pCiv, &sScreenX, &sScreenY );
	sX = sScreenX;
	sY = sScreenY;

	// begin quote
	BeginCivQuote( pCiv, ubCivQuoteID, ubEntryID, sX, sY );

	// Increment use
	if ( ubCivQuoteID != CIV_QUOTE_HINT )
	{
		//pCiv->bCurrentCivQuoteDelta++;
		CivQuoteDelta++;
		/*
		if ( pCiv->bCurrentCivQuoteDelta == 2 )
		{
			pCiv->bCurrentCivQuoteDelta = 0;
		}
		*/
		if ( CivQuoteDelta == 2 )
		{
			CivQuoteDelta = 0;
		}	
		
		
	}
}

void InitCivQuoteSystem( )
{
	memset( &gCivQuotes, 0, sizeof( gCivQuotes ) );  //Not used 

	memset( &gCivQuoteData, 0, sizeof( gCivQuoteData ) );
	gCivQuoteData.bActive				= FALSE;
	gCivQuoteData.iVideoOverlay	= -1;
	gCivQuoteData.iDialogueBox	= -1;
	guiLastAIActionPopupTime = 0;
	gubActiveAICombatCalloutPriority = 0;
	memset( &gubLastAICombatCalloutEvent, 0, sizeof(gubLastAICombatCalloutEvent) );
	memset( &guiLastAICombatCalloutEventTime, 0, sizeof(guiLastAICombatCalloutEventTime) );
	memset( &gPendingAICombatCallout, 0, sizeof(gPendingAICombatCallout) );
}

//--------------------------------------------------------------
//is allowed remove. Not used  and remove from SaveLoadGame.cpp.
BOOLEAN SaveCivQuotesToSaveGameFile( HWFILE hFile )
{
	UINT32	uiNumBytesWritten;

	FileWrite( hFile, &gCivQuotes, sizeof( gCivQuotes), &uiNumBytesWritten );
	if( uiNumBytesWritten != sizeof( gCivQuotes ) )
	{
		return( FALSE );
	}

	return( TRUE );
}

// anv: used now
//is allowed remove. Not used and remove from SaveLoadGame.cpp.
BOOLEAN LoadCivQuotesFromLoadGameFile( HWFILE hFile )
{
	UINT32	uiNumBytesRead;

	// anv: reset taunt timers after game is loaded (guiBaseJA2Clock can decrease)
	memset( &uiTauntFinishTimes, 0, sizeof( uiTauntFinishTimes ) );
	guiLastAIActionPopupTime = 0;
	gubActiveAICombatCalloutPriority = 0;
	memset( &gubLastAICombatCalloutEvent, 0, sizeof(gubLastAICombatCalloutEvent) );
	memset( &guiLastAICombatCalloutEventTime, 0, sizeof(guiLastAICombatCalloutEventTime) );
	memset( &gPendingAICombatCallout, 0, sizeof(gPendingAICombatCallout) );

	FileRead( hFile, &gCivQuotes, sizeof( gCivQuotes ), &uiNumBytesRead );
	if( uiNumBytesRead != sizeof( gCivQuotes ) )
	{
		return( FALSE );
	}

	CopyNumEntriesIntoQuoteStruct( ); //Not used 

	return( TRUE );
}
//--------------------------------------------------------------

// anv: start enemy taunt with probabilty depending on taunt settings
void PossiblyStartEnemyTaunt( SOLDIERTYPE *pCiv, TAUNTTYPE iTauntType, UINT32 uiTargetID )
{
	SOLDIERTYPE *pTarget = NULL;
	if( uiTargetID != NOBODY )
	{
		pTarget = MercPtrs[uiTargetID];
	}
	if (is_networked)	// No taunts in multiplayer
		return;

	// taunts disabled?
	if( gGameSettings.fOptions[TOPTION_ALLOW_TAUNTS] == FALSE )
	{
		return;
	}
	// uh, just in case
	if( pCiv == NULL )
	{
		return;
	}
	// is enemy blocked from taunting at the moment?
	if( uiTauntFinishTimes[pCiv->ubID] > GetJA2Clock() )
	{		
		return;
	}
	// check if generated person
	if ( !(IS_MERC_BODY_TYPE( pCiv )) || !(pCiv->ubProfile == NO_PROFILE) )
	{
		return;
	}
	// only enemies and militia taunt
	// sevenfm: allow voice taunts for Kingpin faction
	if( pCiv->bTeam != ENEMY_TEAM &&
		pCiv->bTeam != MILITIA_TEAM &&
		!( pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == KINGPIN_CIV_GROUP && gGameExternalOptions.fVoiceTaunts ) &&
		!(pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == KINGPIN_FORT_CIV_GROUP && gGameExternalOptions.fVoiceTaunts) &&
		!( pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == HICKS_CIV_GROUP && gGameExternalOptions.fVoiceTaunts ) &&
		!( pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == WARDEN_CIV_GROUP && gGameExternalOptions.fVoiceTaunts ) &&
		!( pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == UNNAMED_CIV_GROUP_16 && gGameExternalOptions.fVoiceTaunts ) &&
		!( pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == TRACONA_DRAGON_GROUP && gGameExternalOptions.fVoiceTaunts ) &&
		!( pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == CIA_OPERATIVES_GROUP && gGameExternalOptions.fVoiceTaunts ) &&
		!( pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == CIA_STANLEY_GROUP && gGameExternalOptions.fVoiceTaunts ) &&
		!( pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == TRACONA_OPERATIVES_GROUP && gGameExternalOptions.fVoiceTaunts ) &&
		!( pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == COCKEYE_THUGS && gGameExternalOptions.fVoiceTaunts ))		
	{
		return;
	}
	// only visible enemies taunt (unless set otherwise)
	if ( ( pCiv->bVisible == -1 ) && ( gTauntsSettings.fTauntOnlyVisibleEnemies == TRUE ) )
	{
		return;
	}
	// only enemies that are able to speak at the moment can taunt
	if ( pCiv->stats.bLife < OKLIFE || pCiv->bCollapsed || pCiv->bBreathCollapsed )
	{
		return;
	}
	// check probability
 	switch(iTauntType)
	{
		case TAUNT_FIRE_GUN:
			if( Random(100)+1 > gTauntsSettings.ubTauntFireGunChance )
				return;
			break;
		case TAUNT_FIRE_LAUNCHER:
			if( Random(100)+1 > gTauntsSettings.ubTauntFireLauncherChance )
				return;
			break;
		case TAUNT_ATTACK_BLADE:
			if( Random(100)+1 > gTauntsSettings.ubTauntAttackBladeChance)
				return;
			break;
		case TAUNT_ATTACK_HTH:
			if( Random(100)+1 > gTauntsSettings.ubTauntAttackHTHChance )
				return;
			break;

		case TAUNT_THROW_KNIFE:
			if( Random(100)+1 > gTauntsSettings.ubTauntThrowKnifeChance )
				return;
			break;
		case TAUNT_THROW_GRENADE:
			if( Random(100)+1 > gTauntsSettings.ubTauntThrowGrenadeChance )
				return;
			break;
		case TAUNT_CHARGE_BLADE:
			if( Random(100)+1 > gTauntsSettings.ubTauntChargeKnifeChance )
				return;
			break;
		case TAUNT_CHARGE_HTH:
			if( Random(100)+1 > gTauntsSettings.ubTauntChargeFistsChance )
				return;
			break;

		case TAUNT_STEAL:
			if( Random(100)+1 > gTauntsSettings.ubTauntStealChance )
				return;
			break;

		case TAUNT_RUN_AWAY:
			if( Random(100)+1 > gTauntsSettings.ubTauntRunAwayChance)
				return;
			break;
		case TAUNT_SEEK_NOISE:
			if( Random(100)+1 > gTauntsSettings.ubTauntSeekNoiseChance )
				return;
			break;
		case TAUNT_ALERT:
			if( Random(100)+1 > gTauntsSettings.ubTauntAlertChance )
				return;
			break;
		case TAUNT_SUSPICIOUS:
			if( Random(100)+1 > gTauntsSettings.ubTauntSuspiciousChance)
				return;
			break;

		case TAUNT_GOT_HIT:
		case TAUNT_GOT_HIT_BLOODLOSS:
		case TAUNT_GOT_HIT_EXPLOSION:
		case TAUNT_GOT_HIT_FALLROOF:
		case TAUNT_GOT_HIT_GAS:
		case TAUNT_GOT_HIT_GUNFIRE:
		case TAUNT_GOT_HIT_HTH:
		case TAUNT_GOT_HIT_BLADE:
		case TAUNT_GOT_HIT_OBJECT:
		case TAUNT_GOT_HIT_STRUCTURE_EXPLOSION:
		case TAUNT_GOT_HIT_TENTACLES:
		case TAUNT_GOT_HIT_THROWING_KNIFE:
			if( Random(100)+1 > gTauntsSettings.ubTauntGotHitChance )
				return;
			break;

		case TAUNT_GOT_BLINDED:
		case TAUNT_GOT_DEAFENED:
			if( Random(100)+1 > gTauntsSettings.ubTauntGotDeafenedBlindedChance)
				return;
			break;

		case TAUNT_GOT_ROBBED:
			if( Random(100)+1 > gTauntsSettings.ubTauntGotRobbedChance)
				return;
			break;

		case TAUNT_GOT_MISSED:
		case TAUNT_GOT_MISSED_GUNFIRE:
		case TAUNT_GOT_MISSED_BLADE:
		case TAUNT_GOT_MISSED_HTH:
		case TAUNT_GOT_MISSED_THROWING_KNIFE:
			if( Random(100)+1 > gTauntsSettings.ubTauntGotMissedChance )
				return;
			break;

		case TAUNT_HIT:
		case TAUNT_HIT_GUNFIRE:
		case TAUNT_HIT_BLADE:
		case TAUNT_HIT_HTH:
		case TAUNT_HIT_THROWING_KNIFE:
			if( Random(100)+1 > gTauntsSettings.ubTauntHitChance )
				return;
			break;

		case TAUNT_KILL:
		case TAUNT_KILL_GUNFIRE:
		case TAUNT_KILL_BLADE:
		case TAUNT_KILL_HTH:
		case TAUNT_KILL_THROWING_KNIFE:
			if( Random(100)+1 > gTauntsSettings.ubTauntKillChance )
				return;
			break;

		case TAUNT_HEAD_POP:
			if( Random(100)+1 > gTauntsSettings.ubTauntHeadPopChance )
				return;
			break;

		case TAUNT_MISS:
		case TAUNT_MISS_GUNFIRE:
		case TAUNT_MISS_BLADE:
		case TAUNT_MISS_HTH:
		case TAUNT_MISS_THROWING_KNIFE:
			if( Random(100)+1 > gTauntsSettings.ubTauntMissChance )
				return;
			break;

		case TAUNT_OUT_OF_AMMO:
			if( Random(100)+1 > gTauntsSettings.ubTauntOutOfAmmoChance )
				return;
			break;
		case TAUNT_RELOAD:
			if( Random(100)+1 > gTauntsSettings.ubTauntReloadChance )
				return;
			break;

		case TAUNT_NOTICED_UNSEEN:
			if( Random(100)+1 > gTauntsSettings.ubTauntNoticedUnseenChance )
				return;
			break;
		case TAUNT_SAY_HI:
			if( Random(100)+1 > gTauntsSettings.ubTauntSayHiChance )
				return;
			break;
		case TAUNT_INFORM_ABOUT:
			if( Random(100)+1 > gTauntsSettings.ubTauntInformAboutChance )
				return;
			break;

		case TAUNT_RIPOSTE:
			if( Random(100)+1 > gTauntsSettings.ubRiposteChance)
				return;
			break;

		default:
			break;
	}
	
	StartEnemyTaunt( pCiv, iTauntType, pTarget );

}

// VR battlefield communication -------------------------------------------------
// 22 semantic classes x 10 variants = 220 concise contextual lines.  The text
// lives here rather than in decision code so AI behaviour and presentation stay
// independent and later localisation/XML migration is straightforward.
static const CHAR16 * const gAICombatLines_CONTACT[] =
{
	L"\"Contact!\"",
	L"\"Enemy spotted!\"",
	L"\"There! Contact!\"",
	L"\"Eyes on them!\"",
	L"\"I see them!\"",
	L"\"Contact ahead!\"",
	L"\"They're here!\"",
	L"\"Movement! Contact!\"",
	L"\"Enemy in sight!\"",
	L"\"We've got contact!\""
};

static const CHAR16 * const gAICombatLines_ADVANCE[] =
{
	L"\"Move up!\"",
	L"\"Advance!\"",
	L"\"Push forward!\"",
	L"\"Keep moving!\"",
	L"\"Go! Go!\"",
	L"\"Forward!\"",
	L"\"Close the distance!\"",
	L"\"Move on them!\"",
	L"\"Keep the pressure on!\"",
	L"\"Up! Move!\""
};

static const CHAR16 * const gAICombatLines_TAKE_COVER[] =
{
	L"\"Take cover!\"",
	L"\"Get down!\"",
	L"\"Find cover!\"",
	L"\"Behind something!\"",
	L"\"Heads down!\"",
	L"\"Get under cover!\"",
	L"\"Move to cover!\"",
	L"\"Stay low!\"",
	L"\"Get out of the open!\"",
	L"\"Cover! Now!\""
};

static const CHAR16 * const gAICombatLines_FLANK_LEFT[] =
{
	L"\"Flank left!\"",
	L"\"Go left!\"",
	L"\"Around the left!\"",
	L"\"Left side! Move!\"",
	L"\"Work around left!\"",
	L"\"Take their left!\"",
	L"\"Swing left!\"",
	L"\"Push the left side!\"",
	L"\"Get around them left!\"",
	L"\"Left flank, go!\""
};

static const CHAR16 * const gAICombatLines_FLANK_RIGHT[] =
{
	L"\"Flank right!\"",
	L"\"Go right!\"",
	L"\"Around the right!\"",
	L"\"Right side! Move!\"",
	L"\"Work around right!\"",
	L"\"Take their right!\"",
	L"\"Swing right!\"",
	L"\"Push the right side!\"",
	L"\"Get around them right!\"",
	L"\"Right flank, go!\""
};

static const CHAR16 * const gAICombatLines_WITHDRAW[] =
{
	L"\"Fall back!\"",
	L"\"Break contact!\"",
	L"\"Pull back!\"",
	L"\"Back! Back!\"",
	L"\"Give ground!\"",
	L"\"Get out of there!\"",
	L"\"Withdraw!\"",
	L"\"Move back!\"",
	L"\"Disengage!\"",
	L"\"Back to cover!\""
};

static const CHAR16 * const gAICombatLines_REGROUP[] =
{
	L"\"Regroup!\"",
	L"\"Link up!\"",
	L"\"Stay together!\"",
	L"\"Back to the group!\"",
	L"\"Close up!\"",
	L"\"Get with the others!\"",
	L"\"Form up!\"",
	L"\"Don't get isolated!\"",
	L"\"Join the others!\"",
	L"\"Pull together!\""
};

static const CHAR16 * const gAICombatLines_RALLY[] =
{
	L"\"Move! You're fine!\"",
	L"\"Back in the fight!\"",
	L"\"Get up and move!\"",
	L"\"Come on! Move!\"",
	L"\"Stay with us!\"",
	L"\"Keep it together!\"",
	L"\"Get moving!\"",
	L"\"Up! Up!\"",
	L"\"Don't freeze!\"",
	L"\"Move with the group!\""
};

static const CHAR16 * const gAICombatLines_SUPPRESS[] =
{
	L"\"Keep them down!\"",
	L"\"Suppress them!\"",
	L"\"Fire! Keep them pinned!\"",
	L"\"Hold them there!\"",
	L"\"Put fire on them!\"",
	L"\"Keep firing!\"",
	L"\"Pin them down!\"",
	L"\"Covering fire!\"",
	L"\"Don't let them move!\"",
	L"\"Fire on that position!\""
};

static const CHAR16 * const gAICombatLines_GRENADE[] =
{
	L"\"Grenade!\"",
	L"\"Grenade out!\"",
	L"\"Frag out!\"",
	L"\"Fire in the hole!\"",
	L"\"Grenade, take cover!\"",
	L"\"Throwing grenade!\"",
	L"\"Explosive out!\"",
	L"\"Get down! Grenade!\"",
	L"\"Grenade on them!\"",
	L"\"Watch the blast!\""
};

static const CHAR16 * const gAICombatLines_SMOKE[] =
{
	L"\"Smoke out!\"",
	L"\"Smoke that position!\"",
	L"\"Put smoke down!\"",
	L"\"Smoke! Move!\"",
	L"\"Cover them with smoke!\"",
	L"\"Screen that area!\"",
	L"\"Smoke the approach!\"",
	L"\"Get smoke in there!\"",
	L"\"Smoke between us!\"",
	L"\"Use the smoke!\""
};

static const CHAR16 * const gAICombatLines_HEAVY_WEAPON[] =
{
	L"\"Launcher!\"",
	L"\"Heavy weapon!\"",
	L"\"RPG!\"",
	L"\"Launcher firing!\"",
	L"\"Rocket! Get down!\"",
	L"\"Heavy shot!\"",
	L"\"Watch the launcher!\"",
	L"\"Clear the backblast!\"",
	L"\"Rocket going out!\"",
	L"\"Heavy weapon firing!\""
};

static const CHAR16 * const gAICombatLines_MEDIC[] =
{
	L"\"Medic!\"",
	L"\"Medic, here!\"",
	L"\"Get the medic!\"",
	L"\"He's hit! Medic!\"",
	L"\"Help him!\"",
	L"\"Medic, move!\"",
	L"\"Get over here, medic!\"",
	L"\"We need aid!\"",
	L"\"Treat him!\"",
	L"\"Medic! He's bleeding!\""
};

static const CHAR16 * const gAICombatLines_RELOAD[] =
{
	L"\"Reloading!\"",
	L"\"Cover me, reloading!\"",
	L"\"Magazine! Cover me!\"",
	L"\"Reload!\"",
	L"\"I'm reloading!\"",
	L"\"Cover while I reload!\"",
	L"\"Changing magazine!\"",
	L"\"Need a second! Reloading!\"",
	L"\"Reloading, watch me!\"",
	L"\"Weapon empty, reloading!\""
};

static const CHAR16 * const gAICombatLines_OUT_OF_AMMO[] =
{
	L"\"Out of ammo!\"",
	L"\"I'm empty!\"",
	L"\"No rounds!\"",
	L"\"Weapon's empty!\"",
	L"\"Cover me, I'm dry!\"",
	L"\"I'm out!\"",
	L"\"No ammo!\"",
	L"\"Empty! Cover me!\"",
	L"\"I need ammunition!\"",
	L"\"Dry!\""
};

static const CHAR16 * const gAICombatLines_CASUALTY[] =
{
	L"\"I'm hit!\"",
	L"\"Man hit!\"",
	L"\"I've been hit!\"",
	L"\"Hit!\"",
	L"\"I'm wounded!\"",
	L"\"Taking fire! I'm hit!\"",
	L"\"They got me!\"",
	L"\"Wounded here!\"",
	L"\"I'm hurt!\"",
	L"\"Hit over here!\""
};

static const CHAR16 * const gAICombatLines_INCOMING[] =
{
	L"\"Incoming!\"",
	L"\"Down, now!\"",
	L"\"Shots incoming!\"",
	L"\"Take cover! Incoming!\"",
	L"\"We're taking fire!\"",
	L"\"Down! Down!\"",
	L"\"Fire coming in!\"",
	L"\"Watch it!\"",
	L"\"They're firing on us!\"",
	L"\"Incoming fire!\""
};

static const CHAR16 * const gAICombatLines_SEARCH[] =
{
	L"\"Check that noise!\"",
	L"\"Search there!\"",
	L"\"Look around!\"",
	L"\"Check it out!\"",
	L"\"Something's there!\"",
	L"\"Watch that area!\"",
	L"\"Search that position!\"",
	L"\"Go check it!\"",
	L"\"Eyes open!\"",
	L"\"Find out what that was!\""
};

static const CHAR16 * const gAICombatLines_REINFORCE[] =
{
	L"\"Tell the others!\"",
	L"\"Call it in!\"",
	L"\"Alert everyone!\"",
	L"\"Get the others here!\"",
	L"\"Contact! Call for support!\"",
	L"\"Radio it in!\"",
	L"\"Tell the squad!\"",
	L"\"Get help over here!\"",
	L"\"Call the others!\"",
	L"\"Report contact!\""
};

static const CHAR16 * const gAICombatLines_VEHICLE[] =
{
	L"\"Vehicle!\"",
	L"\"Armored vehicle!\"",
	L"\"Tank!\"",
	L"\"Tank! Take cover!\"",
	L"\"Armor ahead!\"",
	L"\"Watch the vehicle!\"",
	L"\"Heavy vehicle!\"",
	L"\"Tank in sight!\"",
	L"\"Armor! Get down!\"",
	L"\"Vehicle contact!\""
};

static const CHAR16 * const gAICombatLines_CIVILIAN[] =
{
	L"\"Civilian! Watch your fire!\"",
	L"\"Civilians near the target!\"",
	L"\"Watch the civilians!\"",
	L"\"Noncombatant! Check your fire!\"",
	L"\"Civilian in the line!\"",
	L"\"Careful! Civilian!\"",
	L"\"Watch your shots! Civilian!\"",
	L"\"Civilian close!\"",
	L"\"Check fire! Civilian nearby!\"",
	L"\"Mind the civilians!\""
};

static const CHAR16 * const gAICombatLines_HOLD[] =
{
	L"\"Hold!\"",
	L"\"Hold here!\"",
	L"\"Stay put!\"",
	L"\"Keep this position!\"",
	L"\"Hold the line!\"",
	L"\"Don't move yet!\"",
	L"\"Stay ready!\"",
	L"\"Wait for it!\"",
	L"\"Hold position!\"",
	L"\"Keep your ground!\""
};

static const CHAR16 * const gAICombatLines_TARGET_DOWN[] =
{
	L"\"Target down!\"",
	L"\"He's down!\"",
	L"\"Got one!\"",
	L"\"Enemy down!\"",
	L"\"One down!\"",
	L"\"Dropped him!\"",
	L"\"Target's down!\"",
	L"\"He's out!\"",
	L"\"That one's down!\"",
	L"\"Enemy hit and down!\""
};

static BOOLEAN BuildAICombatCalloutText( AI_BATTLE_CALLOUT ubCallout, STR16 zText )
{
	if ( zText == NULL )
		return FALSE;

	switch ( ubCallout )
	{
		case AI_BATTLE_CALL_CONTACT:
			wcscpy( zText, gAICombatLines_CONTACT[ Random( sizeof(gAICombatLines_CONTACT) / sizeof(gAICombatLines_CONTACT[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_ADVANCE:
			wcscpy( zText, gAICombatLines_ADVANCE[ Random( sizeof(gAICombatLines_ADVANCE) / sizeof(gAICombatLines_ADVANCE[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_TAKE_COVER:
			wcscpy( zText, gAICombatLines_TAKE_COVER[ Random( sizeof(gAICombatLines_TAKE_COVER) / sizeof(gAICombatLines_TAKE_COVER[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_FLANK_LEFT:
			wcscpy( zText, gAICombatLines_FLANK_LEFT[ Random( sizeof(gAICombatLines_FLANK_LEFT) / sizeof(gAICombatLines_FLANK_LEFT[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_FLANK_RIGHT:
			wcscpy( zText, gAICombatLines_FLANK_RIGHT[ Random( sizeof(gAICombatLines_FLANK_RIGHT) / sizeof(gAICombatLines_FLANK_RIGHT[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_WITHDRAW:
			wcscpy( zText, gAICombatLines_WITHDRAW[ Random( sizeof(gAICombatLines_WITHDRAW) / sizeof(gAICombatLines_WITHDRAW[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_REGROUP:
			wcscpy( zText, gAICombatLines_REGROUP[ Random( sizeof(gAICombatLines_REGROUP) / sizeof(gAICombatLines_REGROUP[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_RALLY:
			wcscpy( zText, gAICombatLines_RALLY[ Random( sizeof(gAICombatLines_RALLY) / sizeof(gAICombatLines_RALLY[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_SUPPRESS:
			wcscpy( zText, gAICombatLines_SUPPRESS[ Random( sizeof(gAICombatLines_SUPPRESS) / sizeof(gAICombatLines_SUPPRESS[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_GRENADE:
			wcscpy( zText, gAICombatLines_GRENADE[ Random( sizeof(gAICombatLines_GRENADE) / sizeof(gAICombatLines_GRENADE[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_SMOKE:
			wcscpy( zText, gAICombatLines_SMOKE[ Random( sizeof(gAICombatLines_SMOKE) / sizeof(gAICombatLines_SMOKE[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_HEAVY_WEAPON:
			wcscpy( zText, gAICombatLines_HEAVY_WEAPON[ Random( sizeof(gAICombatLines_HEAVY_WEAPON) / sizeof(gAICombatLines_HEAVY_WEAPON[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_MEDIC:
			wcscpy( zText, gAICombatLines_MEDIC[ Random( sizeof(gAICombatLines_MEDIC) / sizeof(gAICombatLines_MEDIC[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_RELOAD:
			wcscpy( zText, gAICombatLines_RELOAD[ Random( sizeof(gAICombatLines_RELOAD) / sizeof(gAICombatLines_RELOAD[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_OUT_OF_AMMO:
			wcscpy( zText, gAICombatLines_OUT_OF_AMMO[ Random( sizeof(gAICombatLines_OUT_OF_AMMO) / sizeof(gAICombatLines_OUT_OF_AMMO[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_CASUALTY:
			wcscpy( zText, gAICombatLines_CASUALTY[ Random( sizeof(gAICombatLines_CASUALTY) / sizeof(gAICombatLines_CASUALTY[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_INCOMING:
			wcscpy( zText, gAICombatLines_INCOMING[ Random( sizeof(gAICombatLines_INCOMING) / sizeof(gAICombatLines_INCOMING[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_SEARCH:
			wcscpy( zText, gAICombatLines_SEARCH[ Random( sizeof(gAICombatLines_SEARCH) / sizeof(gAICombatLines_SEARCH[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_REINFORCE:
			wcscpy( zText, gAICombatLines_REINFORCE[ Random( sizeof(gAICombatLines_REINFORCE) / sizeof(gAICombatLines_REINFORCE[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_VEHICLE:
			wcscpy( zText, gAICombatLines_VEHICLE[ Random( sizeof(gAICombatLines_VEHICLE) / sizeof(gAICombatLines_VEHICLE[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_CIVILIAN:
			wcscpy( zText, gAICombatLines_CIVILIAN[ Random( sizeof(gAICombatLines_CIVILIAN) / sizeof(gAICombatLines_CIVILIAN[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_HOLD:
			wcscpy( zText, gAICombatLines_HOLD[ Random( sizeof(gAICombatLines_HOLD) / sizeof(gAICombatLines_HOLD[0]) ) ] );
			return TRUE;
		case AI_BATTLE_CALL_TARGET_DOWN:
			wcscpy( zText, gAICombatLines_TARGET_DOWN[ Random( sizeof(gAICombatLines_TARGET_DOWN) / sizeof(gAICombatLines_TARGET_DOWN[0]) ) ] );
			return TRUE;
		default:
			return FALSE;
	}
}

static UINT8 AICombatCalloutPriority( AI_BATTLE_CALLOUT ubCallout )
{
	switch ( ubCallout )
	{
		case AI_BATTLE_CALL_GRENADE:
		case AI_BATTLE_CALL_VEHICLE:
			return 100;
		case AI_BATTLE_CALL_SMOKE:
		case AI_BATTLE_CALL_MEDIC:
			return 92;
		case AI_BATTLE_CALL_WITHDRAW:
		case AI_BATTLE_CALL_INCOMING:
			return 88;
		case AI_BATTLE_CALL_HEAVY_WEAPON:
		case AI_BATTLE_CALL_CONTACT:
		case AI_BATTLE_CALL_REINFORCE:
		case AI_BATTLE_CALL_CIVILIAN:
			return 82;
		case AI_BATTLE_CALL_FLANK_LEFT:
		case AI_BATTLE_CALL_FLANK_RIGHT:
		case AI_BATTLE_CALL_SUPPRESS:
			return 75;
		case AI_BATTLE_CALL_REGROUP:
		case AI_BATTLE_CALL_RALLY:
			return 65;
		case AI_BATTLE_CALL_TAKE_COVER:
		case AI_BATTLE_CALL_OUT_OF_AMMO:
		case AI_BATTLE_CALL_CASUALTY:
			return 58;
		case AI_BATTLE_CALL_TARGET_DOWN:
			return 48;
		case AI_BATTLE_CALL_RELOAD:
			return 38;
		case AI_BATTLE_CALL_ADVANCE:
		case AI_BATTLE_CALL_SEARCH:
		case AI_BATTLE_CALL_HOLD:
			return 30;
		default:
			return 0;
	}
}

static UINT8 AICombatCalloutChance( AI_BATTLE_CALLOUT ubCallout )
{
	switch ( ubCallout )
	{
		case AI_BATTLE_CALL_GRENADE:
		case AI_BATTLE_CALL_VEHICLE:
			return 100;
		case AI_BATTLE_CALL_SMOKE:
		case AI_BATTLE_CALL_MEDIC:
		case AI_BATTLE_CALL_HEAVY_WEAPON:
			return 90;
		case AI_BATTLE_CALL_WITHDRAW:
		case AI_BATTLE_CALL_REINFORCE:
			return 80;
		case AI_BATTLE_CALL_FLANK_LEFT:
		case AI_BATTLE_CALL_FLANK_RIGHT:
			return 72;
		case AI_BATTLE_CALL_CONTACT:
		case AI_BATTLE_CALL_INCOMING:
		case AI_BATTLE_CALL_CIVILIAN:
			return 65;
		case AI_BATTLE_CALL_REGROUP:
		case AI_BATTLE_CALL_RALLY:
		case AI_BATTLE_CALL_OUT_OF_AMMO:
			return 55;
		case AI_BATTLE_CALL_SUPPRESS:
		case AI_BATTLE_CALL_CASUALTY:
			return 42;
		case AI_BATTLE_CALL_TAKE_COVER:
		case AI_BATTLE_CALL_TARGET_DOWN:
			return 35;
		case AI_BATTLE_CALL_RELOAD:
		case AI_BATTLE_CALL_SEARCH:
			return 25;
		case AI_BATTLE_CALL_ADVANCE:
		case AI_BATTLE_CALL_HOLD:
			return 22;
		default:
			return 0;
	}
}

static UINT32 AICombatCalloutMaxQueueAge( AI_BATTLE_CALLOUT ubCallout )
{
	UINT8 ubPriority = AICombatCalloutPriority( ubCallout );
	if ( ubPriority >= 90 )
		return 1800;
	if ( ubPriority >= 75 )
		return 2600;
	return 3500;
}

static BOOLEAN AIHandItemIsSmoke( SOLDIERTYPE *pCiv )
{
	if ( !pCiv )
		return FALSE;

	UINT16 usItem = pCiv->inv[HANDPOS].usItem;
	if ( usItem >= MAXITEMS || !(Item[usItem].usItemClass & IC_GRENADE) )
		return FALSE;

	UINT16 usExplosiveIndex = Item[usItem].ubClassIndex;
	if ( usExplosiveIndex > MAXITEMS )
		return FALSE;

	return Explosive[usExplosiveIndex].ubType == EXPLOSV_SMOKE ||
		Explosive[usExplosiveIndex].ubType == EXPLOSV_SIGNAL_SMOKE;
}

static BOOLEAN AICombatCalloutSpeakerValid( SOLDIERTYPE *pCiv )
{
	return pCiv &&
		pCiv->bActive && pCiv->bInSector &&
		(pCiv->bTeam == ENEMY_TEAM || pCiv->bTeam == MILITIA_TEAM) &&
		pCiv->bVisible != -1 &&
		pCiv->stats.bLife >= OKLIFE &&
		!pCiv->bCollapsed && !pCiv->bBreathCollapsed &&
		!pCiv->IsZombie();
}

static const CHAR8 * AICombatCalloutName( AI_BATTLE_CALLOUT ubCallout )
{
	switch ( ubCallout )
	{
		case AI_BATTLE_CALL_CONTACT: return "contact";
		case AI_BATTLE_CALL_ADVANCE: return "advance";
		case AI_BATTLE_CALL_TAKE_COVER: return "take_cover";
		case AI_BATTLE_CALL_FLANK_LEFT: return "flank_left";
		case AI_BATTLE_CALL_FLANK_RIGHT: return "flank_right";
		case AI_BATTLE_CALL_WITHDRAW: return "withdraw";
		case AI_BATTLE_CALL_REGROUP: return "regroup";
		case AI_BATTLE_CALL_RALLY: return "rally";
		case AI_BATTLE_CALL_SUPPRESS: return "suppress";
		case AI_BATTLE_CALL_GRENADE: return "grenade";
		case AI_BATTLE_CALL_SMOKE: return "smoke";
		case AI_BATTLE_CALL_HEAVY_WEAPON: return "heavy_weapon";
		case AI_BATTLE_CALL_MEDIC: return "medic";
		case AI_BATTLE_CALL_RELOAD: return "reload";
		case AI_BATTLE_CALL_OUT_OF_AMMO: return "out_of_ammo";
		case AI_BATTLE_CALL_CASUALTY: return "casualty";
		case AI_BATTLE_CALL_INCOMING: return "incoming";
		case AI_BATTLE_CALL_SEARCH: return "search";
		case AI_BATTLE_CALL_REINFORCE: return "reinforce";
		case AI_BATTLE_CALL_VEHICLE: return "vehicle";
		case AI_BATTLE_CALL_CIVILIAN: return "civilian";
		case AI_BATTLE_CALL_HOLD: return "hold";
		case AI_BATTLE_CALL_TARGET_DOWN: return "target_down";
		default: return "unknown";
	}
}

// Semantic callout timing is intentionally independent from uiTauntFinishTimes:
// the latter throttles legacy/voice taunts and must not discard a queued grenade,
// medic or withdrawal warning before the visual slot becomes available.
static void ShowAICombatCalloutNow( SOLDIERTYPE *pCiv, AI_BATTLE_CALLOUT ubCallout )
{
	CHAR16 zText[320];
	if ( !AICombatCalloutSpeakerValid( pCiv ) || !BuildAICombatCalloutText( ubCallout, zText ) )
		return;

	ShowTauntPopupBox( pCiv, zText );
	gubActiveAICombatCalloutPriority = AICombatCalloutPriority( ubCallout );
	VRAnalyticsDiagnostic( VR_ANALYTICS_TACTICAL, "soldier", pCiv->ubID,
		"battle_callout", AICombatCalloutName( ubCallout ) );
	guiLastAIActionPopupTime = GetJA2Clock();
	gubLastAICombatCalloutEvent[pCiv->ubID] = (UINT8)ubCallout;
	guiLastAICombatCalloutEventTime[pCiv->ubID] = guiLastAIActionPopupTime;
}

static BOOLEAN AICombatCalloutIsCommand( AI_BATTLE_CALLOUT ubCallout )
{
	switch ( ubCallout )
	{
		case AI_BATTLE_CALL_ADVANCE:
		case AI_BATTLE_CALL_TAKE_COVER:
		case AI_BATTLE_CALL_FLANK_LEFT:
		case AI_BATTLE_CALL_FLANK_RIGHT:
		case AI_BATTLE_CALL_WITHDRAW:
		case AI_BATTLE_CALL_REGROUP:
		case AI_BATTLE_CALL_RALLY:
		case AI_BATTLE_CALL_SUPPRESS:
		case AI_BATTLE_CALL_SMOKE:
		case AI_BATTLE_CALL_REINFORCE:
		case AI_BATTLE_CALL_HOLD:
			return TRUE;
		default:
			return FALSE;
	}
}

void QueueAICombatCallout( SOLDIERTYPE *pCiv, AI_BATTLE_CALLOUT ubCallout )
{
	if ( is_networked || !AICombatCalloutSpeakerValid( pCiv ) )
		return;
	if ( !(gTacticalStatus.uiFlags & INCOMBAT) )
		return;
	if ( gGameSettings.fOptions[TOPTION_ALLOW_TAUNTS] == FALSE ||
		gTauntsSettings.fTauntShowPopupBox == FALSE )
		return;
	if ( ubCallout <= AI_BATTLE_CALL_NONE || ubCallout >= AI_BATTLE_CALL_MAX )
		return;

	UINT8 ubChance = AICombatCalloutChance( ubCallout );
	if ( ubChance == 0 )
		return;

	// Orders are more often voiced by soldiers with command presence, while
	// inexperienced troops still call urgent hazards at the normal rate.
	if ( AICombatCalloutIsCommand( ubCallout ) )
	{
		if ( pCiv->stats.bLeadership >= 75 ||
			pCiv->ubSoldierClass == SOLDIER_CLASS_ELITE ||
			pCiv->ubSoldierClass == SOLDIER_CLASS_ELITE_MILITIA )
		{
			ubChance = (UINT8)__min( 100, (INT32)ubChance + 18 );
		}
		else if ( pCiv->stats.bLeadership < 40 && ubChance > 15 )
		{
			ubChance = (UINT8)__max( 10, (INT32)ubChance - 10 );
		}
	}

	if ( Random(100) >= ubChance )
		return;

	UINT32 uiNow = GetJA2Clock();
	// Per-speaker/event debounce prevents the same intent from being repeated
	// every AI evaluation while allowing a different urgent event immediately.
	if ( gubLastAICombatCalloutEvent[pCiv->ubID] == (UINT8)ubCallout &&
		guiLastAICombatCalloutEventTime[pCiv->ubID] != 0 &&
		(uiNow - guiLastAICombatCalloutEventTime[pCiv->ubID]) < 3500 )
	{
		return;
	}

	UINT8 ubPriority = AICombatCalloutPriority( ubCallout );

	// Urgent battlefield hazards may interrupt a lower-value semantic bubble,
	// but never a normal civilian/dialogue quote (active priority == 0).
	if ( gCivQuoteData.bActive && gubActiveAICombatCalloutPriority > 0 &&
		ubPriority >= 88 && ubPriority >= gubActiveAICombatCalloutPriority + 15 )
	{
		ShutDownQuoteBox( TRUE );
		if ( gPendingAICombatCallout.fActive &&
			gPendingAICombatCallout.ubPriority <= ubPriority )
		{
			gPendingAICombatCallout.fActive = FALSE;
		}
		ShowAICombatCalloutNow( pCiv, ubCallout );
		return;
	}

	if ( gCivQuoteData.bActive == FALSE &&
		(guiLastAIActionPopupTime == 0 || (uiNow - guiLastAIActionPopupTime) >= 900) )
	{
		ShowAICombatCalloutNow( pCiv, ubCallout );
		return;
	}

	// Keep the most important waiting event. Equal-priority events keep the first
	// call so rapid grenade/contact bursts do not churn the queue.
	if ( !gPendingAICombatCallout.fActive || ubPriority > gPendingAICombatCallout.ubPriority )
	{
		gPendingAICombatCallout.fActive = TRUE;
		gPendingAICombatCallout.ubSoldierID = pCiv->ubID;
		gPendingAICombatCallout.ubCallout = ubCallout;
		gPendingAICombatCallout.ubPriority = ubPriority;
		gPendingAICombatCallout.uiQueuedAt = uiNow;
	}
}

static void FlushPendingAICombatCallout()
{
	if ( !gPendingAICombatCallout.fActive || gCivQuoteData.bActive )
		return;

	UINT32 uiNow = GetJA2Clock();
	if ( uiNow - gPendingAICombatCallout.uiQueuedAt >
		AICombatCalloutMaxQueueAge( gPendingAICombatCallout.ubCallout ) )
	{
		gPendingAICombatCallout.fActive = FALSE;
		return;
	}
	if ( guiLastAIActionPopupTime != 0 && (uiNow - guiLastAIActionPopupTime) < 900 )
		return;

	UINT8 ubSoldierID = gPendingAICombatCallout.ubSoldierID;
	AI_BATTLE_CALLOUT ubCallout = gPendingAICombatCallout.ubCallout;
	gPendingAICombatCallout.fActive = FALSE;

	if ( ubSoldierID >= TOTAL_SOLDIERS )
		return;
	SOLDIERTYPE *pCiv = MercPtrs[ubSoldierID];
	if ( !AICombatCalloutSpeakerValid( pCiv ) )
		return;

	ShowAICombatCalloutNow( pCiv, ubCallout );
}

static BOOLEAN AICombatSoldierIsTank( SOLDIERTYPE *pSoldier )
{
	return pSoldier && (pSoldier->ubBodyType == TANK_NE || pSoldier->ubBodyType == TANK_NW);
}

static AI_BATTLE_CALLOUT AICombatCalloutFromTaunt( SOLDIERTYPE *pCiv, TAUNTTYPE iTauntType, SOLDIERTYPE *pTarget )
{
	switch ( iTauntType )
	{
		case TAUNT_FIRE_GUN:
			return (pCiv && (pCiv->bDoBurst || pCiv->bDoAutofire > 1)) ? AI_BATTLE_CALL_SUPPRESS : AI_BATTLE_CALL_NONE;
		case TAUNT_FIRE_LAUNCHER:
			return AI_BATTLE_CALL_HEAVY_WEAPON;
		case TAUNT_THROW_GRENADE:
			return AIHandItemIsSmoke( pCiv ) ? AI_BATTLE_CALL_SMOKE : AI_BATTLE_CALL_GRENADE;
		case TAUNT_OUT_OF_AMMO:
			return AI_BATTLE_CALL_OUT_OF_AMMO;
		case TAUNT_RELOAD:
			return AI_BATTLE_CALL_RELOAD;
		case TAUNT_CHARGE_BLADE:
		case TAUNT_CHARGE_HTH:
			return AI_BATTLE_CALL_ADVANCE;
		case TAUNT_RUN_AWAY:
			return AI_BATTLE_CALL_WITHDRAW;
		case TAUNT_SEEK_NOISE:
			return AI_BATTLE_CALL_SEARCH;
		case TAUNT_ALERT:
			return AI_BATTLE_CALL_REINFORCE;
		case TAUNT_SUSPICIOUS:
			return AI_BATTLE_CALL_HOLD;
		case TAUNT_NOTICED_UNSEEN:
			return AI_BATTLE_CALL_INCOMING;
		case TAUNT_INFORM_ABOUT:
			return AICombatSoldierIsTank( pTarget ) ? AI_BATTLE_CALL_VEHICLE : AI_BATTLE_CALL_CONTACT;
		case TAUNT_GOT_HIT_BLOODLOSS:
			return AI_BATTLE_CALL_MEDIC;
		case TAUNT_GOT_HIT:
		case TAUNT_GOT_HIT_GUNFIRE:
		case TAUNT_GOT_HIT_BLADE:
		case TAUNT_GOT_HIT_HTH:
		case TAUNT_GOT_HIT_EXPLOSION:
		case TAUNT_GOT_HIT_STRUCTURE_EXPLOSION:
		case TAUNT_GOT_HIT_OBJECT:
		case TAUNT_GOT_HIT_THROWING_KNIFE:
			return AI_BATTLE_CALL_CASUALTY;
		case TAUNT_GOT_MISSED:
		case TAUNT_GOT_MISSED_GUNFIRE:
		case TAUNT_GOT_MISSED_BLADE:
		case TAUNT_GOT_MISSED_HTH:
		case TAUNT_GOT_MISSED_THROWING_KNIFE:
			return AI_BATTLE_CALL_INCOMING;
		case TAUNT_KILL:
		case TAUNT_KILL_GUNFIRE:
		case TAUNT_KILL_BLADE:
		case TAUNT_KILL_HTH:
		case TAUNT_KILL_THROWING_KNIFE:
			return AI_BATTLE_CALL_TARGET_DOWN;
		default:
			return AI_BATTLE_CALL_NONE;
	}
}

static SOLDIERTYPE * AICombatActionTarget( SOLDIERTYPE *pCiv )
{
	if ( !pCiv || pCiv->ubOppNum == NOBODY || pCiv->ubOppNum >= TOTAL_SOLDIERS )
		return NULL;
	return MercPtrs[pCiv->ubOppNum];
}

static BOOLEAN AICombatTargetIsVehicle( SOLDIERTYPE *pCiv )
{
	SOLDIERTYPE *pTarget = AICombatActionTarget( pCiv );
	return pTarget && pTarget->bActive && pTarget->bInSector && AICombatSoldierIsTank( pTarget );
}

static BOOLEAN AICivilianNearActionTarget( SOLDIERTYPE *pCiv )
{
	if ( !pCiv || TileIsOutOfBounds( pCiv->aiData.usActionData ) )
		return FALSE;

	for ( UINT8 ubID = gTacticalStatus.Team[CIV_TEAM].bFirstID;
		ubID <= gTacticalStatus.Team[CIV_TEAM].bLastID; ++ubID )
	{
		SOLDIERTYPE *pOther = MercPtrs[ubID];
		if ( !pOther || !pOther->bActive || !pOther->bInSector || pOther->stats.bLife <= 0 )
			continue;
		if ( !pOther->aiData.bNeutral || !IS_MERC_BODY_TYPE( pOther ) )
			continue;
		if ( pOther->pathing.bLevel != pCiv->bTargetLevel )
			continue;
		if ( LOS_Raised( pCiv, pOther, CALC_FROM_ALL_DIRS ) <= 0 )
			continue;
		if ( PythSpacesAway( pCiv->aiData.usActionData, pOther->sGridNo ) <= 2 )
			return TRUE;
	}
	return FALSE;
}

static BOOLEAN AIActionLooksLikeMedicRescue( SOLDIERTYPE *pCiv )
{
	if ( !pCiv || FindObjClass( pCiv, IC_MEDKIT ) == NO_SLOT ||
		TileIsOutOfBounds( pCiv->aiData.usActionData ) )
	{
		return FALSE;
	}

	for ( UINT8 ubID = gTacticalStatus.Team[pCiv->bTeam].bFirstID;
		ubID <= gTacticalStatus.Team[pCiv->bTeam].bLastID; ++ubID )
	{
		SOLDIERTYPE *pPatient = MercPtrs[ubID];
		if ( !pPatient || pPatient == pCiv || !pPatient->bActive || !pPatient->bInSector )
			continue;
		if ( pPatient->stats.bLife <= 0 || pPatient->bBleeding <= 0 )
			continue;
		if ( pPatient->pathing.bLevel != pCiv->pathing.bLevel )
			continue;
		if ( PythSpacesAway( pCiv->aiData.usActionData, pPatient->sGridNo ) <= 1 )
			return TRUE;
	}

	return FALSE;
}

void ShowAIActionPopup( SOLDIERTYPE *pCiv, INT8 bAction )
{
	if ( !pCiv )
		return;

	AI_BATTLE_CALLOUT ubCallout = AI_BATTLE_CALL_NONE;
	switch ( bAction )
	{
		case AI_ACTION_TAKE_COVER: ubCallout = AI_BATTLE_CALL_TAKE_COVER; break;
		case AI_ACTION_GET_CLOSER:
			ubCallout = AIActionLooksLikeMedicRescue( pCiv ) ? AI_BATTLE_CALL_MEDIC : AI_BATTLE_CALL_ADVANCE;
			break;
		case AI_ACTION_SEEK_OPPONENT: ubCallout = AI_BATTLE_CALL_ADVANCE; break;
		case AI_ACTION_SEEK_FRIEND: ubCallout = AI_BATTLE_CALL_REGROUP; break;
		case AI_ACTION_WITHDRAW:
		case AI_ACTION_RUN_AWAY: ubCallout = AI_BATTLE_CALL_WITHDRAW; break;
		case AI_ACTION_FLANK_LEFT: ubCallout = AI_BATTLE_CALL_FLANK_LEFT; break;
		case AI_ACTION_FLANK_RIGHT: ubCallout = AI_BATTLE_CALL_FLANK_RIGHT; break;
		case AI_ACTION_RED_ALERT: ubCallout = AI_BATTLE_CALL_REINFORCE; break;
		case AI_ACTION_YELLOW_ALERT:
		case AI_ACTION_SEEK_NOISE: ubCallout = AI_BATTLE_CALL_SEARCH; break;
		case AI_ACTION_GIVE_AID: ubCallout = AI_BATTLE_CALL_MEDIC; break;
		case AI_ACTION_RELOAD_GUN: ubCallout = AI_BATTLE_CALL_RELOAD; break;
		case AI_ACTION_TOSS_PROJECTILE:
			ubCallout = AIHandItemIsSmoke( pCiv ) ? AI_BATTLE_CALL_SMOKE : AI_BATTLE_CALL_GRENADE;
			break;
		case AI_ACTION_FIRE_GUN:
			if ( AICombatTargetIsVehicle( pCiv ) )
				ubCallout = AI_BATTLE_CALL_VEHICLE;
			else if ( AICivilianNearActionTarget( pCiv ) )
				ubCallout = AI_BATTLE_CALL_CIVILIAN;
			else if ( pCiv->bDoBurst || pCiv->bDoAutofire > 1 )
				ubCallout = AI_BATTLE_CALL_SUPPRESS;
			break;
		case AI_ACTION_STOP_COWERING: ubCallout = AI_BATTLE_CALL_RALLY; break;
		default: break;
	}

	if ( ubCallout != AI_BATTLE_CALL_NONE )
		QueueAICombatCallout( pCiv, ubCallout );
}

// SANDRO - soldier taunts 
void StartEnemyTaunt( SOLDIERTYPE *pCiv, TAUNTTYPE iTauntType, SOLDIERTYPE *pTarget )
{
	CHAR16	sTauntText[ 320 ];	
	CHAR16	gzTauntQuote[ 320 ];
	UINT16	iApplicableTaunts = 0;

	// Flugente: zombies don't talk
	if ( pCiv->IsZombie() )
		return;

	// Semantic visual reaction is independent of VOICE_TAUNTS.  This means hit,
	// kill, contact, reload and other battlefield feedback still works in a
	// text-only setup.
	AI_BATTLE_CALLOUT ubSemanticCallout = AICombatCalloutFromTaunt( pCiv, iTauntType, pTarget );
	if ( ubSemanticCallout != AI_BATTLE_CALL_NONE )
		QueueAICombatCallout( pCiv, ubSemanticCallout );

	// sevenfm: audio and visual combat communication are independent. Audio keeps
	// the original Vengeance voice bank; semantic text goes through the priority
	// queue so important calls are not lost behind an existing bubble.
	if( gGameExternalOptions.fVoiceTaunts )		
	{		
		PlayVoiceTaunt( pCiv, iTauntType, pTarget );

		AI_BATTLE_CALLOUT ubVoiceCallout = AICombatCalloutFromTaunt( pCiv, iTauntType, pTarget );
		if ( ubVoiceCallout != AI_BATTLE_CALL_NONE &&
			gTauntsSettings.fTauntShowInLog == TRUE &&
			( gbPublicOpplist[gbPlayerNum][pCiv->ubID] == SEEN_CURRENTLY || gTauntsSettings.fTauntAlwaysShowInLog == TRUE ) )
		{
			CHAR16 zVoiceLog[320];
			if ( BuildAICombatCalloutText( ubVoiceCallout, zVoiceLog ) )
				ScreenMsg( FONT_GRAY2, MSG_INTERFACE, L"%s: %s", pCiv->GetName(), zVoiceLog );
		}

		uiTauntFinishTimes[pCiv->ubID] = GetJA2Clock() + min( gTauntsSettings.sMaxDelay,
			max( gTauntsSettings.sMinDelay, FindDelayForString( L"You're the disease and I'm the cure!" ) + gTauntsSettings.sModDelay ) );
		return;
	}

	// anv: check all taunts, and remember those applicable
	for(UINT16 i=0; i<num_found_taunt; i++)
	{
		// check if attitudes are ok
		switch( pCiv->aiData.bAttitude )
		{
			case CUNNINGAID:
				if( !(zTaunt[ i ].uiFlags & TAUNT_A_CUNNING_AID) )
					continue;
				break;
			case CUNNINGSOLO:
				if( !(zTaunt[ i ].uiFlags & TAUNT_A_CUNNING_SOLO) )
					continue;
				break;
			case BRAVEAID:
				if( !(zTaunt[ i ].uiFlags & TAUNT_A_BRAVE_AID) )
					continue;
			case BRAVESOLO:
				if( !(zTaunt[ i ].uiFlags & TAUNT_A_BRAVE_SOLO) )
					continue;
				break;
			case DEFENSIVE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_A_DEFENSIVE) )
					continue;
				break;
			case AGGRESSIVE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_A_AGGRESSIVE) )
					continue;
				break;
			default:
				break;
		}
		// check if situation is ok
		switch(iTauntType)
		{
			// actions
			case TAUNT_FIRE_GUN:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_FIRE_GUN) )
					continue;
				break;
			case TAUNT_FIRE_LAUNCHER:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_FIRE_LAUNCHER) )
					continue;
				break;
			case TAUNT_ATTACK_BLADE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_ATTACK_BLADE) )
					continue;
				break;
			case TAUNT_ATTACK_HTH:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_ATTACK_HTH) )
					continue;
				break;

			case TAUNT_THROW_KNIFE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_THROW_KNIFE) )
					continue;
				break;
			case TAUNT_THROW_GRENADE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_THROW_GRENADE) )
					continue;
				break;

			case TAUNT_OUT_OF_AMMO:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_OUT_OF_AMMO) )
					continue;
				break;
			case TAUNT_RELOAD:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_RELOAD) )
					continue;
				break;

			// AI routines
			case TAUNT_CHARGE_BLADE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_CHARGE_BLADE) )
					continue;
				break;
			case TAUNT_CHARGE_HTH:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_CHARGE_HTH) )
					continue;
				break;
			case TAUNT_STEAL:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_STEAL) )
					continue;
				break;
			case TAUNT_RUN_AWAY:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_RUN_AWAY) )
					continue;
				break;
			case TAUNT_SEEK_NOISE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_SEEK_NOISE) )
					continue;
				break;
			case TAUNT_ALERT:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_ALERT) )
					continue;
				break;
			case TAUNT_SUSPICIOUS:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_SUSPICIOUS) )
					continue;
				break;
			case TAUNT_NOTICED_UNSEEN:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_NOTICED_UNSEEN) )
					continue;
				break;
			case TAUNT_SAY_HI:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_SAY_HI) )
					continue;
				break;
			case TAUNT_INFORM_ABOUT:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_INFORM_ABOUT) )
					continue;
				break;

			// got_hit_xxx
			case TAUNT_GOT_HIT:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_HIT) )
					continue;
				break;
			case TAUNT_GOT_HIT_GUNFIRE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_HIT_GUNFIRE) )
					continue;
				break;
			case TAUNT_GOT_HIT_BLADE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_HIT_BLADE) )
					continue;
				break;
			case TAUNT_GOT_HIT_HTH:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_HIT_HTH) )
					continue;
				break;
			case TAUNT_GOT_HIT_FALLROOF:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_HIT_FALLROOF) )
					continue;
				break;
			case TAUNT_GOT_HIT_BLOODLOSS:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_HIT_BLOODLOSS) )
					continue;
				break;
			case TAUNT_GOT_HIT_EXPLOSION:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_HIT_EXPLOSION) )
					continue;
				break;
			case TAUNT_GOT_HIT_GAS:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_HIT_GAS) )
					continue;
				break;
			case TAUNT_GOT_HIT_TENTACLES:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_HIT_TENTACLES) )
					continue;
				break;
			case TAUNT_GOT_HIT_STRUCTURE_EXPLOSION:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_HIT_STRUCTURE_EXPLOSION) )
					continue;
				break;
			case TAUNT_GOT_HIT_OBJECT:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_HIT_OBJECT) )
					continue;
				break;
			case TAUNT_GOT_HIT_THROWING_KNIFE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_HIT) )
					continue;
				break;

			case TAUNT_GOT_DEAFENED:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_DEAFENED) )
					continue;
				break;
			case TAUNT_GOT_BLINDED:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_BLINDED) )
					continue;
				break;

			// got_missed_xxx
			case TAUNT_GOT_MISSED:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_MISSED) )
					continue;
				break;
			case TAUNT_GOT_MISSED_GUNFIRE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_MISSED_GUNFIRE) )
					continue;
				break;
			case TAUNT_GOT_MISSED_BLADE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_MISSED_BLADE) )
					continue;
				break;
			case TAUNT_GOT_MISSED_HTH:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_MISSED_HTH) )
					continue;
				break;
			case TAUNT_GOT_MISSED_THROWING_KNIFE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_GOT_MISSED_THROWING_KNIFE) )
					continue;
				break;

			// hit_xxx
			case TAUNT_HIT:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_HIT) )
					continue;
				break;
			case TAUNT_HIT_GUNFIRE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_HIT_GUNFIRE) )
					continue;
				break;
			case TAUNT_HIT_BLADE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_HIT_BLADE) )
					continue;
				break;
			case TAUNT_HIT_HTH:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_HIT_HTH) )
					continue;
				break;
			case TAUNT_HIT_THROWING_KNIFE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_HIT_THROWING_KNIFE) )
					continue;
				break;

			// kill_xxx
			case TAUNT_KILL:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_KILL) )
					continue;
				break;
			case TAUNT_KILL_GUNFIRE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_KILL_GUNFIRE) )
					continue;
				break;
			case TAUNT_KILL_BLADE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_KILL_BLADE) )
					continue;
				break;
			case TAUNT_KILL_HTH:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_KILL_HTH) )
					continue;
				break;
			case TAUNT_KILL_THROWING_KNIFE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_KILL_THROWING_KNIFE) )
					continue;
				break;
			case TAUNT_HEAD_POP:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_HEAD_POP) )
					continue;
				break;

			// miss_xxx
			case TAUNT_MISS:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_MISS) )
					continue;
				break;
			case TAUNT_MISS_GUNFIRE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_MISS_GUNFIRE) )
					continue;
				break;
			case TAUNT_MISS_BLADE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_MISS_BLADE) )
					continue;
				break;
			case TAUNT_MISS_HTH:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_MISS_HTH) )
					continue;
				break;
			case TAUNT_MISS_THROWING_KNIFE:
				if( !(zTaunt[ i ].uiFlags & TAUNT_S_MISS_THROWING_KNIFE) )
					continue;
				break;

			// ripostes to merc quotes
			case TAUNT_RIPOSTE:
				if( !(gTacticalStatus.ubLastQuoteSaid == zTaunt[ i ].value[TAUNT_RIPOSTE_QUOTE]) )
					continue;
				break;

			default:
				continue;
				break;
		}

		// class and predefined profiles
		switch( pCiv->ubSoldierClass )
		{
			case SOLDIER_CLASS_ADMINISTRATOR:
				if( !(zTaunt[ i ].uiFlags2 & TAUNT_C_ADMIN) )
					continue;
				if( (zTaunt[ i ].value[TAUNT_PROFILE_ADMIN] != -1 ) && !(zTaunt[ i ].value[TAUNT_PROFILE_ADMIN] == pCiv->usSoldierProfile ) )
					continue;
				break;
			case SOLDIER_CLASS_ARMY:
				if( !(zTaunt[ i ].uiFlags2 & TAUNT_C_ARMY) )
					continue;
				if( (zTaunt[ i ].value[TAUNT_PROFILE_ARMY] != -1 ) && !(zTaunt[ i ].value[TAUNT_PROFILE_ARMY] == pCiv->usSoldierProfile ) )
					continue;
				break;
			case SOLDIER_CLASS_ELITE:
				if( !(zTaunt[ i ].uiFlags2 & TAUNT_C_ELITE) )
					continue;
				if( (zTaunt[ i ].value[TAUNT_PROFILE_ELITE] != -1 ) && !(zTaunt[ i ].value[TAUNT_PROFILE_ELITE] == pCiv->usSoldierProfile ) )
					continue;
				break;
			case SOLDIER_CLASS_GREEN_MILITIA:
				if( !(zTaunt[ i ].uiFlags2 & TAUNT_C_GREEN) )
					continue;
				if( (zTaunt[ i ].value[TAUNT_PROFILE_GREEN] != -1 ) && !( zTaunt[ i ].value[TAUNT_PROFILE_GREEN] == pCiv->usSoldierProfile ) )
					continue;
				break;
			case SOLDIER_CLASS_REG_MILITIA:
				if( !(zTaunt[ i ].uiFlags2 & TAUNT_C_REGULAR) )
					continue;
				if( (zTaunt[ i ].value[TAUNT_PROFILE_ARMY] != -1 ) && !(zTaunt[ i ].value[TAUNT_PROFILE_ARMY] == pCiv->usSoldierProfile ) )
					continue;
				break;
			case SOLDIER_CLASS_ELITE_MILITIA:
				if( !(zTaunt[ i ].uiFlags2 & TAUNT_C_VETERAN) )
					continue;
				if( (zTaunt[ i ].value[TAUNT_PROFILE_ARMY] != -1 ) && !(zTaunt[ i ].value[TAUNT_PROFILE_ARMY] == pCiv->usSoldierProfile ) )
					continue;
				break;
			default:
				return;
				break;
		}

		// gender
		switch( pCiv->ubBodyType )
		{
			case REGMALE:
			case BIGMALE:
			case STOCKYMALE:
				if( !(zTaunt[ i ].uiFlags2 & TAUNT_G_MALE) )
					continue;
				break;
			case REGFEMALE:
				if( !(zTaunt[ i ].uiFlags2 & TAUNT_G_FEMALE) )
					continue;
				break;
			default:
				return;
				break;
		}

		// energy, health
		if( zTaunt[ i ].value[TAUNT_ENERGY_GT] != -1 )
		{
			if( pCiv->bBreath <= zTaunt[ i ].value[TAUNT_ENERGY_GT] )
				continue;
		}
		if( zTaunt[ i ].value[TAUNT_ENERGY_LT] != -1 )
		{
			if( pCiv->bBreath >= zTaunt[ i ].value[TAUNT_ENERGY_LT] )
				continue;
		}
		if( zTaunt[ i ].value[TAUNT_ENERGY_MAX_GT] != -1 )
		{
			if( pCiv->bBreathMax <= zTaunt[ i ].value[TAUNT_ENERGY_MAX_GT] )
				continue;
		}
		if( zTaunt[ i ].value[TAUNT_ENERGY_MAX_LT] != -1 )
		{
			if( pCiv->bBreathMax >= zTaunt[ i ].value[TAUNT_ENERGY_MAX_LT] )
				continue;
		}
		if( zTaunt[ i ].value[TAUNT_HEALTH_GT] != -1 )
		{
			if( pCiv->stats.bLife <= zTaunt[ i ].value[TAUNT_HEALTH_GT] )
				continue;
		}
		if( zTaunt[ i ].value[TAUNT_HEALTH_GT] != -1 )
		{
			if( pCiv->stats.bLife <= zTaunt[ i ].value[TAUNT_TARGET_HEALTH_GT] )
				continue;
		}
		if( zTaunt[ i ].value[TAUNT_HEALTH_LT] != -1 )
		{
			if( pCiv->stats.bLife >= zTaunt[ i ].value[TAUNT_HEALTH_LT] )
				continue;
		}
		if( zTaunt[ i ].value[TAUNT_HEALTH_MAX_GT] != -1 )
		{
			if( pCiv->stats.bLifeMax <= zTaunt[ i ].value[TAUNT_HEALTH_MAX_GT] )
				continue;
		}
		if( zTaunt[ i ].value[TAUNT_HEALTH_MAX_LT] != -1 )
		{
			if( pCiv->stats.bLifeMax >= zTaunt[ i ].value[TAUNT_HEALTH_MAX_LT] )
				continue;
		}
		// morale
		if( zTaunt[ i ].value[TAUNT_MORALE_GT] != -1 )
		{
			if( pCiv->aiData.bMorale <= zTaunt[ i ].value[TAUNT_MORALE_GT] )
				continue;
		}
		if( zTaunt[ i ].value[TAUNT_MORALE_LT] != -1 )
		{
			if( pCiv->aiData.bMorale >= zTaunt[ i ].value[TAUNT_MORALE_LT] )
				continue;
		}
		// experience
		if( zTaunt[ i ].value[TAUNT_EXP_LEVEL_GT] != -1 )
		{
			if( pCiv->stats.bExpLevel <= zTaunt[ i ].value[TAUNT_EXP_LEVEL_GT] )
				continue;
		}
		if( zTaunt[ i ].value[TAUNT_EXP_LEVEL_LT] != -1 )
		{
			if( pCiv->stats.bExpLevel >= zTaunt[ i ].value[TAUNT_EXP_LEVEL_LT] )
				continue;
		}

		// game progress
		if( zTaunt[ i ].value[TAUNT_PROGRESS_GT] != -1 )
		{
			if( zTaunt[ i ].value[TAUNT_PROGRESS_GT] >= CurrentPlayerProgressPercentage() )
				continue;
		}
		if( zTaunt[ i ].value[TAUNT_PROGRESS_LT] != -1 )
		{
			if( zTaunt[ i ].value[TAUNT_PROGRESS_GT] <= CurrentPlayerProgressPercentage() )
				continue;
		}

		// facts
		if( zTaunt[ i ].value[TAUNT_FACT_TRUE] != -1 )
		{
			if( gubFact[ zTaunt[ i ].value[TAUNT_FACT_TRUE] ] != TRUE )
				continue;
		}
		if( zTaunt[ i ].value[TAUNT_FACT_FALSE] != -1 )
		{
			if( gubFact[ zTaunt[ i ].value[TAUNT_FACT_FALSE] ] != FALSE )
				continue;
		}

		// target limitations
		if( pTarget != NULL )
		{

			// target should be zombie
			if( zTaunt[ i ].uiFlags2 & TAUNT_T_ZOMBIE )
			{
			// anv: moved ifdef - if zombies are off, we want to skip any taunts with TAUNT_T_ZOMBIE flag
				if( pTarget->IsZombie() == FALSE )
					continue;
			}

			// target merc profile
			if( zTaunt[ i ].value[TAUNT_TARGET_MERC_PROFILE] != -1 )
			{
				if( pTarget->ubProfile != zTaunt[ i ].value[TAUNT_TARGET_MERC_PROFILE] )
					continue;
			}
			// target type
			if( zTaunt[ i ].value[TAUNT_TARGET_TYPE] != -1 )
			{
				if( pTarget->ubProfile != zTaunt[ i ].value[TAUNT_TARGET_TYPE] )
					continue;
			}
			// target gender
			switch( pTarget->ubBodyType )
			{
				case REGMALE:
				case BIGMALE:
				case STOCKYMALE:
					if( !(zTaunt[ i ].uiFlags2 & TAUNT_T_MALE) )
						continue;
					break;
				case REGFEMALE:
					if( !(zTaunt[ i ].uiFlags2 & TAUNT_T_FEMALE) )
						continue;
					break;
				default:
					return;
					break;
			}
			// target appearance
			if( zTaunt[ i ].value[TAUNT_TARGET_APPEARANCE] != -1 )
			{
				// check if pTarget has his own predefined profile (ubProfile = 200 for generated characters)
				if( pTarget->ubProfile != 200 )
				{
					if( gMercProfiles[pTarget->ubProfile].bAppearance != zTaunt[ i ].value[TAUNT_TARGET_APPEARANCE] )
						continue;
				}
				else
					continue;
			}

			// target type
			switch( zTaunt[ i ].value[TAUNT_TARGET_TYPE] )
			{
				case 0:
					if( pTarget->ubWhatKindOfMercAmI != MERC_TYPE__PLAYER_CHARACTER )
						continue;
					break;
				case 1:
					if( pTarget->ubWhatKindOfMercAmI != MERC_TYPE__AIM_MERC )
						continue;
					break;
				case 2:
					if( pTarget->ubWhatKindOfMercAmI != MERC_TYPE__MERC )
						continue;
					break;
				case 3:
					if( pTarget->ubWhatKindOfMercAmI != MERC_TYPE__NPC )
						continue;
					break;
				case 4:
					if( pTarget->ubWhatKindOfMercAmI != MERC_TYPE__EPC )
						continue;
					break;
				case 5:
					if( pTarget->ubWhatKindOfMercAmI != MERC_TYPE__NPC_WITH_UNEXTENDABLE_CONTRACT )
						continue;
					break;
				case 6:
					if( pTarget->ubWhatKindOfMercAmI != MERC_TYPE__VEHICLE )
						continue;
					break;
				default:
					break;
			}

			// target energy, health
			if( zTaunt[ i ].value[TAUNT_TARGET_ENERGY_GT] != -1 )
			{
				if( pTarget->bBreath <= zTaunt[ i ].value[TAUNT_TARGET_ENERGY_GT] )
					continue;
			}
			if( zTaunt[ i ].value[TAUNT_TARGET_ENERGY_LT] != -1 )
			{
				if( pTarget->bBreath >= zTaunt[ i ].value[TAUNT_TARGET_ENERGY_LT] )
					continue;
			}
			if( zTaunt[ i ].value[TAUNT_TARGET_ENERGY_MAX_GT] != -1 )
			{
				if( pTarget->bBreathMax <= zTaunt[ i ].value[TAUNT_TARGET_ENERGY_MAX_GT] )
					continue;
			}
			if( zTaunt[ i ].value[TAUNT_TARGET_ENERGY_MAX_LT] != -1 )
			{
				if( pTarget->bBreathMax >= zTaunt[ i ].value[TAUNT_TARGET_ENERGY_MAX_LT] )
					continue;
			}
			if( zTaunt[ i ].value[TAUNT_TARGET_HEALTH_GT] != -1 )
			{
				if( pTarget->stats.bLife <= zTaunt[ i ].value[TAUNT_TARGET_HEALTH_GT] )
					continue;
			}
			if( zTaunt[ i ].value[TAUNT_TARGET_HEALTH_GT] != -1 )
			{
				if( pTarget->stats.bLife <= zTaunt[ i ].value[TAUNT_TARGET_HEALTH_GT] )
					continue;
			}
			if( zTaunt[ i ].value[TAUNT_TARGET_HEALTH_LT] != -1 )
			{
				if( pTarget->stats.bLife >= zTaunt[ i ].value[TAUNT_TARGET_HEALTH_LT] )
					continue;
			}
			if( zTaunt[ i ].value[TAUNT_TARGET_HEALTH_MAX_GT] != -1 )
			{
				if( pTarget->stats.bLifeMax <= zTaunt[ i ].value[TAUNT_TARGET_HEALTH_MAX_GT] )
					continue;
			}
			if( zTaunt[ i ].value[TAUNT_TARGET_HEALTH_MAX_LT] != -1 )
			{
				if( pTarget->stats.bLifeMax >= zTaunt[ i ].value[TAUNT_TARGET_HEALTH_MAX_LT] )
					continue;
			}
			// morale
			if( zTaunt[ i ].value[TAUNT_TARGET_MORALE_GT] != -1 )
			{
				if( pTarget->aiData.bMorale <= zTaunt[ i ].value[TAUNT_TARGET_MORALE_GT] )
					continue;
			}
			if( zTaunt[ i ].value[TAUNT_TARGET_MORALE_LT] != -1 )
			{
				if( pTarget->aiData.bMorale >= zTaunt[ i ].value[TAUNT_TARGET_MORALE_LT] )
					continue;
			}
			// experience
			if( zTaunt[ i ].value[TAUNT_TARGET_EXP_LEVEL_GT] != -1 )
			{
				if( pTarget->stats.bExpLevel <= zTaunt[ i ].value[TAUNT_TARGET_EXP_LEVEL_GT] )
					continue;
			}
			if( zTaunt[ i ].value[TAUNT_TARGET_EXP_LEVEL_LT] != -1 )
			{
				if( pTarget->stats.bExpLevel >= zTaunt[ i ].value[TAUNT_TARGET_EXP_LEVEL_LT] )
					continue;
			}
		}
		else // pTarget==NULL
		{
			if( ( zTaunt[ i ].value[TAUNT_TARGET_MERC_PROFILE] != -1 ) ||
				( zTaunt[ i ].value[TAUNT_TARGET_APPEARANCE] != -1 ) ||
				( zTaunt[ i ].value[TAUNT_TARGET_ENERGY_GT] != -1 ) ||
				( zTaunt[ i ].value[TAUNT_TARGET_ENERGY_LT] != -1 ) ||
				( zTaunt[ i ].value[TAUNT_TARGET_ENERGY_MAX_GT] != -1 ) ||
				( zTaunt[ i ].value[TAUNT_TARGET_ENERGY_MAX_LT] != -1 ) ||
				( zTaunt[ i ].value[TAUNT_TARGET_HEALTH_GT] != -1 ) ||
				( zTaunt[ i ].value[TAUNT_TARGET_HEALTH_LT] != -1 ) ||
				( zTaunt[ i ].value[TAUNT_TARGET_HEALTH_MAX_GT] != -1 ) ||
				( zTaunt[ i ].value[TAUNT_TARGET_HEALTH_MAX_LT] != -1 ) ||
				( zTaunt[ i ].value[TAUNT_TARGET_EXP_LEVEL_GT] != -1 ) ||
				( zTaunt[ i ].value[TAUNT_TARGET_EXP_LEVEL_LT] != -1 ) ||
				( zTaunt[ i ].value[TAUNT_TARGET_MORALE_GT] != -1 ) ||
				( zTaunt[ i ].value[TAUNT_TARGET_MORALE_LT] != -1 ) ||
				( zTaunt[ i ].value[TAUNT_TARGET_TYPE] != -1 ) || 
				( zTaunt[ i ].uiFlags2 & TAUNT_T_ZOMBIE ) )
					continue;

		}
		// everything ok, current taunt is applicable, remember it
		zApplicableTaunts[iApplicableTaunts] = zTaunt[i];
		iApplicableTaunts++;
		if(iApplicableTaunts >= MAX_APPLICABLE_TAUNTS)
			continue;
	}
	// are there any applicable taunts?
	if( iApplicableTaunts > 0 )
	{
		// use random one
		// use censored version if setting is set
		UINT16 iChosenTaunt = Random(iApplicableTaunts); 
		if( gTauntsSettings.fTauntCensoredMode == TRUE && zApplicableTaunts[ iChosenTaunt ].szCensoredText[0] != 0 )
		{
			swprintf( sTauntText, zApplicableTaunts[ iChosenTaunt ].szCensoredText );
		}
		else
		{
			swprintf( sTauntText, zApplicableTaunts[ iChosenTaunt ].szText );
		}

		swprintf( gzTauntQuote, L"\"%s\"", sTauntText );

		// block this enemy from taunting for a time being
		uiTauntFinishTimes[pCiv->ubID] = GetJA2Clock() + min( gTauntsSettings.sMaxDelay , max( gTauntsSettings.sMinDelay, FindDelayForString( gzTauntQuote ) + gTauntsSettings.sModDelay ) ); 

		if( gTauntsSettings.fTauntMakeNoise == TRUE )
			MakeNoise( pCiv->ubID, pCiv->sGridNo, pCiv->pathing.bLevel, pCiv->bOverTerrainType, (UINT8)gTauntsSettings.sVolume, NOISE_VOICE, gzTauntQuote );
		else
		{
			if(gTauntsSettings.fTauntShowPopupBox == TRUE)
			{	
				if( gbPublicOpplist[gbPlayerNum][pCiv->ubID] == SEEN_CURRENTLY || gTauntsSettings.fTauntAlwaysShowPopupBox == TRUE )
				{
					ShowTauntPopupBox( pCiv, gzTauntQuote );
				}
			}
			if(gTauntsSettings.fTauntShowInLog == TRUE)
			{
				if( gbPublicOpplist[gbPlayerNum][pCiv->ubID] == SEEN_CURRENTLY || gTauntsSettings.fTauntAlwaysShowInLog == TRUE )
				{
					ScreenMsg( FONT_GRAY2, MSG_INTERFACE, L"%s: %s", pCiv->GetName(), gzTauntQuote );
				}
			}
		}
	}
	else
	{
		return;
	}

}

void ShowTauntPopupBox( SOLDIERTYPE *pCiv, STR16 gzTauntQuote )
{
	INT16	sX, sY;
	INT16	sScreenX, sScreenY;
	VIDEO_OVERLAY_DESC		VideoOverlayDesc;

	// stop if other civ quote is already being shown 
	if( gCivQuoteData.bActive == TRUE )
	{
		return;
	}

	// Determine location...
	// Get location of civ on screen.....
	GetSoldierScreenPos( pCiv, &sScreenX, &sScreenY );
	sX = sScreenX;
	// Flugente: have the box appear a bit above the soldier. Otherwise it will obstruct us from aiming at him, which is annoying if it happens very often
	sY = sScreenY - 40;

	// Create video oeverlay....
	memset( &VideoOverlayDesc, 0, sizeof( VIDEO_OVERLAY_DESC ) );

	// Prepare text box
	gCivQuoteData.iDialogueBox = PrepareMercPopupBox( gCivQuoteData.iDialogueBox , BASIC_MERC_POPUP_BACKGROUND, BASIC_MERC_POPUP_BORDER, gzTauntQuote, DIALOGUE_DEFAULT_WIDTH, 0, 0, 0, &gusCivQuoteBoxWidth, &gusCivQuoteBoxHeight );

	// OK, find center for box......
	sX = sX - ( gusCivQuoteBoxWidth / 2 );
	sY = sY - ( gusCivQuoteBoxHeight / 2 );

	// OK, limit to screen......
	{
		if ( sX < 0 )
		{
			sX = 0;
		}

		// CHECK FOR LEFT/RIGHT
		if ( ( sX + gusCivQuoteBoxWidth ) > SCREEN_WIDTH )
		{
			sX = SCREEN_WIDTH - gusCivQuoteBoxWidth;
		}

		// Now check for top
		if ( sY < gsVIEWPORT_WINDOW_START_Y )
		{
			sY = gsVIEWPORT_WINDOW_START_Y;
		}

		// Check for bottom
		if ( ( sY + gusCivQuoteBoxHeight ) > (SCREEN_HEIGHT - INV_INTERFACE_HEIGHT))
		{
			sY = (SCREEN_HEIGHT - INV_INTERFACE_HEIGHT) - gusCivQuoteBoxHeight;
		}
	}

	VideoOverlayDesc.sLeft			= sX;
	VideoOverlayDesc.sTop				= sY;
	VideoOverlayDesc.sRight			= VideoOverlayDesc.sLeft + gusCivQuoteBoxWidth;
	VideoOverlayDesc.sBottom		= VideoOverlayDesc.sTop + gusCivQuoteBoxHeight;
	VideoOverlayDesc.sX					= VideoOverlayDesc.sLeft;
	VideoOverlayDesc.sY					= VideoOverlayDesc.sTop;
	VideoOverlayDesc.BltCallback = RenderCivQuoteBoxOverlay;

	gCivQuoteData.iVideoOverlay =	RegisterVideoOverlay( 0, &VideoOverlayDesc );

	//Define main region
	MSYS_DefineRegion( &(gCivQuoteData.MouseRegion), VideoOverlayDesc.sLeft, VideoOverlayDesc.sTop,	VideoOverlayDesc.sRight, VideoOverlayDesc.sBottom, MSYS_PRIORITY_HIGHEST,
						CURSOR_NORMAL, MSYS_NO_CALLBACK, QuoteOverlayClickCallback );
	// Add region
	MSYS_AddRegion( &(gCivQuoteData.MouseRegion) );


	gCivQuoteData.bActive = TRUE;

	gCivQuoteData.uiTimeOfCreation = GetJA2Clock( );
	
	gCivQuoteData.uiDelayTime = min( gTauntsSettings.sMaxDelay , max( gTauntsSettings.sMinDelay, FindDelayForString( gzTauntQuote ) + gTauntsSettings.sModDelay ) );

	gCivQuoteData.pCiv = pCiv;
}

STR VoiceTauntFileName[] = 
{
	"FIRE_GUN",
	"FIRE_LAUNCHER",
	"ATTACK_BLADE",
	"ATTACK_HTH",

	"THROW_KNIFE",
	"THROW_GRENADE",

	"OUT_OF_AMMO",
	"RELOAD",

	"STEAL",

	// AI routines
	"CHARGE_BLADE",
	"CHARGE_HTH",
	"RUN_AWAY",
	"SEEK_NOISE",
	"ALERT",
	"SUSPICIOUS",
	"NOTICED_UNSEEN",
	"SAY_HI",
	"INFORM_ABOUT",

	// got_hit_xxx
	"GOT_HIT",
	"GOT_HIT_GUNFIRE",
	"GOT_HIT_BLADE",
	"GOT_HIT_HTH",
	"GOT_HIT_FALLROOF",
	"GOT_HIT_BLOODLOSS",
	"GOT_HIT_EXPLOSION",
	"GOT_HIT_GAS",
	"GOT_HIT_TENTACLES",
	"GOT_HIT_STRUCTURE_EXPLOSION",
	"GOT_HIT_OBJECT",
	"GOT_HIT_THROWING_KNIFE",

	"GOT_DEAFENED",
	"GOT_BLINDED",

	"GOT_ROBBED",

	// got_missed_xxx
	"GOT_MISSED",
	"GOT_MISSED_GUNFIRE",
	"GOT_MISSED_BLADE",
	"GOT_MISSED_HTH",
	"GOT_MISSED_THROWING_KNIFE",

	// hit_xxx
	"HIT",
	"HIT_GUNFIRE",
	"HIT_BLADE",
	"HIT_HTH",
	"HIT_EXPLOSION",
	"HIT_THROWING_KNIFE",

	// kill_xxx
	"KILL",
	"KILL_GUNFIRE",
	"KILL_BLADE",
	"KILL_HTH",
	"KILL_THROWING_KNIFE",
	"HEAD_POP",

	// miss_xxx
	"MISS",
	"MISS_GUNFIRE",
	"TAUNT_MISS_BLADE",
	"MISS_HTH",
	"MISS_THROWING_KNIFE",

	// ripostes to merc quotes
	"RIPOSTE"
};

// Shared battlefield reactions: a small CC0 military callout bank is used as an
// occasional alternative to the existing per-class/per-voice taunt library.
// This keeps the original VR personalities while making obvious combat events
// sound more like battlefield communication.  One global cooldown prevents a
// large sector from turning into overlapping radio chatter.
static UINT32 guiLastSharedBattlefieldReaction = 0;

static BOOLEAN PlaySharedBattlefieldReaction( SOLDIERTYPE *pCiv, TAUNTTYPE iTauntType )
{
	if ( !pCiv || pCiv->stats.bLife < OKLIFE || pCiv->bCollapsed || pCiv->bBreathCollapsed )
		return FALSE;

	const CHAR8 *zChoices[4] = { NULL, NULL, NULL, NULL };
	UINT8 ubChoiceCount = 0;
	UINT8 ubSharedChance = 0;

	switch ( iTauntType )
	{
		case TAUNT_FIRE_GUN:
			zChoices[0] = "target_engaged";
			zChoices[1] = "suppressing_fire";
			ubChoiceCount = 2;
			ubSharedChance = 30;
			break;

		case TAUNT_FIRE_LAUNCHER:
			zChoices[0] = "rpg";
			zChoices[1] = "fire_in_the_hole";
			ubChoiceCount = 2;
			ubSharedChance = 65;
			break;

		case TAUNT_THROW_GRENADE:
			zChoices[0] = "fire_in_the_hole";
			ubChoiceCount = 1;
			ubSharedChance = 75;
			break;

		case TAUNT_OUT_OF_AMMO:
			zChoices[0] = "cover_me";
			zChoices[1] = "call_for_backup";
			ubChoiceCount = 2;
			ubSharedChance = 55;
			break;

		case TAUNT_RELOAD:
			zChoices[0] = "reloading";
			zChoices[1] = "cover_me";
			ubChoiceCount = 2;
			ubSharedChance = 60;
			break;

		case TAUNT_CHARGE_BLADE:
		case TAUNT_CHARGE_HTH:
			zChoices[0] = "go_go_go";
			ubChoiceCount = 1;
			ubSharedChance = 55;
			break;

		case TAUNT_RUN_AWAY:
			zChoices[0] = "call_for_backup";
			zChoices[1] = "watch_my_back";
			zChoices[2] = "get_down";
			ubChoiceCount = 3;
			ubSharedChance = 50;
			break;

		case TAUNT_SEEK_NOISE:
			zChoices[0] = "hold";
			zChoices[1] = "watch_my_back";
			zChoices[2] = "look_out";
			ubChoiceCount = 3;
			ubSharedChance = 35;
			break;

		case TAUNT_ALERT:
			zChoices[0] = "look_out";
			zChoices[1] = "call_for_backup";
			zChoices[2] = "target_engaged";
			ubChoiceCount = 3;
			ubSharedChance = 50;
			break;

		case TAUNT_SUSPICIOUS:
			zChoices[0] = "hold";
			zChoices[1] = "watch_my_back";
			ubChoiceCount = 2;
			ubSharedChance = 35;
			break;

		case TAUNT_NOTICED_UNSEEN:
			zChoices[0] = "sniper";
			zChoices[1] = "look_out";
			ubChoiceCount = 2;
			ubSharedChance = 55;
			break;

		case TAUNT_INFORM_ABOUT:
			zChoices[0] = "target_engaged";
			zChoices[1] = "watch_my_back";
			ubChoiceCount = 2;
			ubSharedChance = 35;
			break;

		case TAUNT_GOT_HIT_BLOODLOSS:
			zChoices[0] = "medic";
			zChoices[1] = "cover_me";
			ubChoiceCount = 2;
			ubSharedChance = 35;
			break;

		case TAUNT_GOT_HIT_EXPLOSION:
		case TAUNT_GOT_HIT_STRUCTURE_EXPLOSION:
		case TAUNT_GOT_HIT_FALLROOF:
			zChoices[0] = "get_down";
			zChoices[1] = "look_out";
			ubChoiceCount = 2;
			ubSharedChance = 45;
			break;

		case TAUNT_GOT_HIT:
		case TAUNT_GOT_HIT_GUNFIRE:
		case TAUNT_GOT_HIT_BLADE:
		case TAUNT_GOT_HIT_HTH:
		case TAUNT_GOT_HIT_OBJECT:
		case TAUNT_GOT_HIT_THROWING_KNIFE:
			zChoices[0] = "cover_me";
			zChoices[1] = "get_down";
			ubChoiceCount = 2;
			ubSharedChance = 25;
			break;

		case TAUNT_GOT_MISSED:
		case TAUNT_GOT_MISSED_GUNFIRE:
		case TAUNT_GOT_MISSED_BLADE:
		case TAUNT_GOT_MISSED_HTH:
		case TAUNT_GOT_MISSED_THROWING_KNIFE:
			zChoices[0] = "get_down";
			zChoices[1] = "look_out";
			ubChoiceCount = 2;
			ubSharedChance = 25;
			break;

		case TAUNT_KILL:
		case TAUNT_KILL_GUNFIRE:
		case TAUNT_KILL_BLADE:
		case TAUNT_KILL_HTH:
		case TAUNT_KILL_THROWING_KNIFE:
		case TAUNT_HEAD_POP:
			zChoices[0] = "target_destroyed";
			ubChoiceCount = 1;
			ubSharedChance = 65;
			break;

		case TAUNT_HIT:
		case TAUNT_HIT_GUNFIRE:
		case TAUNT_HIT_BLADE:
		case TAUNT_HIT_HTH:
		case TAUNT_HIT_THROWING_KNIFE:
			zChoices[0] = "target_engaged";
			ubChoiceCount = 1;
			ubSharedChance = 20;
			break;

		case TAUNT_MISS:
		case TAUNT_MISS_GUNFIRE:
		case TAUNT_MISS_BLADE:
		case TAUNT_MISS_HTH:
		case TAUNT_MISS_THROWING_KNIFE:
			zChoices[0] = "cover_me";
			zChoices[1] = "suppressing_fire";
			ubChoiceCount = 2;
			ubSharedChance = 15;
			break;

		default:
			return FALSE;
	}

	UINT32 uiNow = GetJA2Clock();
	if ( ubChoiceCount == 0 || Random( 100 ) >= ubSharedChance ||
		( uiNow - guiLastSharedBattlefieldReaction ) < 1800 )
	{
		return FALSE;
	}

	CHAR8 zFilename[260];
	CHAR16 zNoise[260];
	UINT8 ubFirstChoice = Random( ubChoiceCount );
	BOOLEAN fFound = FALSE;

	for ( UINT8 ubCheck = 0; ubCheck < ubChoiceCount; ++ubCheck )
	{
		UINT8 ubChoice = ( ubFirstChoice + ubCheck ) % ubChoiceCount;
		sprintf( zFilename, "Voice\\Battlefield\\%s\\%s.ogg",
			( pCiv->ubBodyType == REGFEMALE ) ? "Female" : "Male", zChoices[ubChoice] );
		if ( FileExists( zFilename ) )
		{
			fFound = TRUE;
			break;
		}
	}

	if ( !fFound )
		return FALSE;

	if ( gTauntsSettings.fTauntMakeNoise == TRUE )
	{
		mbstowcs( zNoise, zFilename, strlen( zFilename ) + 1 );
		MakeNoise( pCiv->ubID, pCiv->sGridNo, pCiv->pathing.bLevel,
			pCiv->bOverTerrainType, (UINT8)gTauntsSettings.sVolume, NOISE_VOICE, zNoise );
	}
	else if ( PlayJA2SampleFromFile( zFilename, RATE_11025,
		SoundVolume( HIGHVOLUME, pCiv->sGridNo ), 1, SoundDir( pCiv->sGridNo ) ) == SOUND_ERROR )
	{
		return FALSE;
	}

	guiLastSharedBattlefieldReaction = uiNow;
	return TRUE;
}

// sevenfm: voice taunts
BOOLEAN PlayVoiceTaunt(SOLDIERTYPE *pCiv, TAUNTTYPE iTauntType, SOLDIERTYPE *pTarget)
{
	CHAR8 filename[1024];
	CHAR8 filenameExtra[1024];
	CHAR16 noise[1024];
	CHAR8 buf[1024];
	INT32 iRandomTaunt = 0;
	UINT8 ubExtraTaunts = 0;

	CHECKF(pCiv);

	if (!gGameExternalOptions.fVoiceTaunts)
	{
		return FALSE;
	}

	// show some information about taunts
	if (gGameExternalOptions.fVoiceTauntsDebugInfo)
	{
		ScreenMsg(FONT_MCOLOR_LTGREEN, MSG_INTERFACE, L"Soldier [%d] TauntType %d", pCiv->ubID, iTauntType);
	}

	// cannot taunt when dead or collapsed
	if (pCiv->stats.bLife < OKLIFE || pCiv->bCollapsed || pCiv->bBreathCollapsed)
	{
		if (gGameExternalOptions.fVoiceTauntsDebugInfo)
		{
			ScreenMsg(FONT_MCOLOR_LTGREEN, MSG_INTERFACE, L"Bad soldier state (dying or collapsed)");
		}
		return FALSE;
	}

	strcpy(filename, "Voice");

	if (iTauntType < TAUNT_FIRE_GUN || iTauntType > TAUNT_RIPOSTE)
	{
		if (gGameExternalOptions.fVoiceTauntsDebugInfo)
		{
			ScreenMsg(FONT_MCOLOR_LTGREEN, MSG_INTERFACE, L"Bad taunt");
		}
		return FALSE;
	}

	// About one event in several uses the shared military bank; otherwise the
	// original Vengeance per-voice taunt system continues unchanged.
	if ( PlaySharedBattlefieldReaction( pCiv, iTauntType ) )
		return TRUE;

	if (pCiv->bTeam == MILITIA_TEAM)
	{
		strcat(filename, "\\Militia\\");

		if (pCiv->ubBodyType == REGFEMALE)
			strcat(filename, "Female\\");
		else
			strcat(filename, "Male\\");

		if (pCiv->ubSoldierClass == SOLDIER_CLASS_ELITE_MILITIA)
			strcat(filename, "Elite\\");
		else if (pCiv->ubSoldierClass == SOLDIER_CLASS_REG_MILITIA)
			strcat(filename, "Regular\\");
		else if (pCiv->ubSoldierClass == SOLDIER_CLASS_GREEN_MILITIA)
			strcat(filename, "Green\\");
	}
	else if (pCiv->bTeam == ENEMY_TEAM)
	{
		strcat(filename, "\\Army\\");

		if (pCiv->ubBodyType == REGFEMALE)
			strcat(filename, "Female\\");
		else
			strcat(filename, "Male\\");

		if (pCiv->ubSoldierClass == SOLDIER_CLASS_ELITE)
			strcat(filename, "Elite\\");
		else if (pCiv->ubSoldierClass == SOLDIER_CLASS_ARMY)
			strcat(filename, "Regular\\");
		else if (pCiv->ubSoldierClass == SOLDIER_CLASS_ADMINISTRATOR)
			strcat(filename, "Admin\\");
	}
	else if (pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == WARDEN_CIV_GROUP)
	{
		strcat(filename, "\\Warden\\");

		if (pCiv->ubBodyType == REGFEMALE)
			strcat(filename, "Female\\");
		else
			strcat(filename, "Male\\");
	}
	else if (pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == KINGPIN_CIV_GROUP || pCiv->ubCivilianGroup == KINGPIN_FORT_CIV_GROUP)
	{
		strcat(filename, "\\Kingpin\\");
	}
	// anv: VR - kingpin fort group
	else if (pCiv->ubCivilianGroup == KINGPIN_FORT_CIV_GROUP)
	{
		strcat(filename, "\\Kingpin\\");
	}
	else if (pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == HICKS_CIV_GROUP)
	{
		strcat(filename, "\\Hale and Burton\\");
	}
	else if (pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == UNNAMED_CIV_GROUP_16)
	{
		strcat(filename, "\\Traconian Army\\");
	}
	else if (pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == CIA_OPERATIVES_GROUP)
	{
		strcat(filename, "\\CIA Operatives\\");
	}
	else if (pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == TRACONA_OPERATIVES_GROUP)
	{
		strcat(filename, "\\Tracona Operatives\\");
	}
	else if (pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == COCKEYE_THUGS)
	{
		strcat(filename, "\\Cockeye Thugs\\");
	}
	else if (pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == CIA_STANLEY_GROUP)
	{
		strcat(filename, "\\CIA Stanley\\");
	}
	else if (pCiv->bTeam == CIV_TEAM && pCiv->ubCivilianGroup == TRACONA_DRAGON_GROUP)
	{
		strcat(filename, "\\TracOps Dragon\\");
	}
	else if (pCiv->bTeam == CIV_TEAM)
	{
		strcat(filename, "\\Civilian\\");

		if (pCiv->ubBodyType == REGFEMALE)
			strcat(filename, "Female\\");
		else
			strcat(filename, "Male\\");
	}
	else
	{
		if (gGameExternalOptions.fVoiceTauntsDebugInfo)
		{
			ScreenMsg(FONT_MCOLOR_LTRED, MSG_INTERFACE, L"Taunt: incorrect bodytype");
		}
		return FALSE;
	}

	// count possible voices
	UINT8 ubVoiceCount;
	for (ubVoiceCount = 1; ubVoiceCount < 100; ubVoiceCount++)
	{
		sprintf(buf, "%s%02d", filename, ubVoiceCount);
		strcat(buf, "\\alert.ogg");

		// check that folder exists
		if (!FileExists(buf))
		{
			if (gGameExternalOptions.fVoiceTauntsDebugInfo)
			{
				mbstowcs(noise, buf, strlen(buf) + 1);
				ScreenMsg(FONT_GREEN, MSG_INTERFACE, noise);
			}

			break;
		}
	}
	// find last good number
	ubVoiceCount--;

	if (ubVoiceCount < 1)
	{
		if (gGameExternalOptions.fVoiceTauntsDebugInfo)
			ScreenMsg(FONT_MCOLOR_LTGREEN, MSG_INTERFACE, L"Could not find any voice folder");

		return FALSE;
	}

	if (gGameExternalOptions.fVoiceTauntsDebugInfo)
		ScreenMsg(FONT_MCOLOR_LTGREEN, MSG_INTERFACE, L"found %d voices", ubVoiceCount);

	// prepare voice folder name
	sprintf(buf, "%02d", 1 + pCiv->ubID % ubVoiceCount);
	strcat(filename, buf);
	strcat(filename, "\\");

	strcat(filename, VoiceTauntFileName[iTauntType]);

	// count possible extra filenames
	ubExtraTaunts = 0;
	for (UINT8 ubCheck = 1; ubCheck <= 10; ubCheck++)
	{
		// check extra taunt file
		strcpy(filenameExtra, filename);
		sprintf(buf, " %d", ubExtraTaunts);
		strcat(filenameExtra, buf);
		strcat(filenameExtra, ".ogg");
		if (!FileExists(filenameExtra))
		{
			break;
		}
		ubExtraTaunts = ubCheck;
	}

	// possibly use extra taunt
	iRandomTaunt = Random(ubExtraTaunts + 1);
	if (iRandomTaunt > 0)
	{
		sprintf(buf, " %d", iRandomTaunt - 1);
		strcat(filename, buf);
	}

	strcat(filename, ".ogg");

	// log taunt file names
	if (gGameExternalOptions.fVoiceTauntsDebugInfo)
	{
		FILE	*OutFile;
		if ((OutFile = fopen("VoiceTauntLog.txt", "a+t")) != NULL)
		{
			fprintf(OutFile, "Soldier [%d] TauntType %d %s\n",
				pCiv->ubID,
				iTauntType,
				filename);
			fclose(OutFile);
		}

		// show some information about taunts	
		mbstowcs(noise, filename, strlen(filename) + 1);
		ScreenMsg(FONT_GREEN, MSG_INTERFACE, noise);
	}

	// check that taunt file exists
	if (!FileExists(filename))
	{
		if (gGameExternalOptions.fVoiceTauntsDebugInfo)
		{
			mbstowcs(noise, filename, strlen(filename) + 1);
			ScreenMsg(FONT_GREEN, MSG_INTERFACE, noise);
			ScreenMsg(FONT_MCOLOR_LTRED, MSG_INTERFACE, L"Taunt: no file %s", noise);
		}

		return FALSE;
	}

	if (gTauntsSettings.fTauntMakeNoise == TRUE)
	{
		// convert char to char16
		mbstowcs(noise, filename, strlen(filename) + 1);
		// use filename as taunt text, play sound later
		MakeNoise(pCiv->ubID, pCiv->sGridNo, pCiv->pathing.bLevel, pCiv->bOverTerrainType, (UINT8)gTauntsSettings.sVolume, NOISE_VOICE, noise);
	}
	else
	{
		// play voice taunt
		if (PlayJA2SampleFromFile(filename, RATE_11025, SoundVolume(HIGHVOLUME, pCiv->sGridNo), 1, SoundDir(pCiv->sGridNo)) == SOUND_ERROR)
		{
			if (gGameExternalOptions.fVoiceTauntsDebugInfo)
			{
				ScreenMsg(FONT_MCOLOR_LTRED, MSG_INTERFACE, L"Failed to play taunt");
			}
			return FALSE;
		}
	}

	return TRUE;
}