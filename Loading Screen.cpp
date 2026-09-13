	#include "vsurface.h"
	#include "mapscreen.h"
	#include "Loading Screen.h"
	#include "Campaign Types.h"
	#include "Game Clock.h"
	#include "GameSettings.h"
	#include "Random.h"
	#include "Debug.h"
	#include "local.h"
	#include "Font Control.h"
	#include "font.h"
	#include "render dirty.h"
#include "Strategic Movement.h"
#include "UndergroundInit.h"
#include <string>
#include <vector>
#include <cctype>
#include "strategicmap.h"

extern HVSURFACE ghFrameBuffer;
extern BOOLEAN gfSchedulesHosed;

#ifdef JA2UB
	#include "Ja25_Tactical.h"
	#include "Ja25 Strategic Ai.h"
#endif
UINT8 gubLastLoadingScreenID = LOADINGSCREEN_NOTHING;
FLOAT fLoadingScreenAspectRatio;
//BOOLEAN bShowSmallImage = FALSE;
SECTOR_LOADSCREENS gSectorLoadscreens[MAX_SECTOR_LOADSCREENS];

static INT16 requestedX, requestedY, requestedZ;
static STR8 szSector;
static STR8 szSectorMap[MAP_WORLD_Y][MAP_WORLD_X] =	{
	{"N",	"N" ,"N" ,"N" ,"N" ,"N" ,"N" ,"N" ,"N" ,"N" , "N" , "N" , "N" , "N" , "N" , "N" , "N"	,"N"},

	{"N",	"A1","A2","A3","A4","A5","A6","A7","A8","A9","A10","A11","A12","A13","A14","A15","A16"	,"N"},
	{"N",	"B1","B2","B3","B4","B5","B6","B7","B8","B9","B10","B11","B12","B13","B14","B15","B16"	,"N"},
	{"N",	"C1","C2","C3","C4","C5","C6","C7","C8","C9","C10","C11","C12","C13","C14","C15","C16"	,"N"},
	{"N",	"D1","D2","D3","D4","D5","D6","D7","D8","D9","D10","D11","D12","D13","D14","D15","D16"	,"N"},
	{"N",	"E1","E2","E3","E4","E5","E6","E7","E8","E9","E10","E11","E12","E13","E14","E15","E16"	,"N"},
	{"N",	"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12","F13","F14","F15","F16"	,"N"},
	{"N",	"G1","G2","G3","G4","G5","G6","G7","G8","G9","G10","G11","G12","G13","G14","G15","G16"	,"N"},
	{"N",	"H1","H2","H3","H4","H5","H6","H7","H8","H9","H10","H11","H12","H13","H14","H15","H16"	,"N"},
	{"N",	"I1","I2","I3","I4","I5","I6","I7","I8","I9","I10","I11","I12","I13","I14","I15","I16"	,"N"},
	{"N",	"J1","J2","J3","J4","J5","J6","J7","J8","J9","J10","J11","J12","J13","J14","J15","J16"	,"N"},
	{"N",	"K1","K2","K3","K4","K5","K6","K7","K8","K9","K10","K11","K12","K13","K14","K15","K16"	,"N"},
	{"N",	"L1","L2","L3","L4","L5","L6","L7","L8","L9","L10","L11","L12","L13","L14","L15","L16"	,"N"},
	{"N",	"M1","M2","M3","M4","M5","M6","M7","M8","M9","M10","M11","M12","M13","M14","M15","M16"	,"N"},
	{"N",	"N1","N2","N3","N4","N5","N6","N7","N8","N9","N10","N11","N12","N13","N14","N15","N16"	,"N"},
	{"N",	"O1","O2","O3","O4","O5","O6","O7","O8","O9","O10","O11","O12","O13","O14","O15","O16"	,"N"},
	{"N",	"P1","P2","P3","P4","P5","P6","P7","P8","P9","P10","P11","P12","P13","P14","P15","P16"	,"N"},

	{"N",	"N" ,"N" ,"N" ,"N" ,"N" ,"N" ,"N" ,"N" ,"N" , "N" , "N" , "N" , "N" , "N" , "N" , "N"	,"N"},
};

// Map LOADINGSCREEN_NOTHING, ..., LOADINGSCREEN_NIGHTBALIME (see enum in header file)
// to some strings which will be used to create the actual filename
// (by appending e.g. "_800x600.sti" or something).
// This is only used when NOT loading them from SectorLoadscreens.xml.
static STR8 LoadScreenNames[LOADINGSCREEN_NIGHTBALIME+1] =
{
	"LS_Heli",
	"LS_DayGeneric",
	"LS_DayTown1",
	"LS_DayTown2",
	"LS_DayWild",
	"LS_DayTropical",
	"LS_DayForest",
	"LS_DayDesert",
	"LS_DayPalace",
	"LS_NightGeneric",
	"LS_NightWild",
	"LS_NightTown1",
	"LS_NightTown2",
	"LS_NightForest",
	"LS_NightTropical",
	"LS_NightDesert",
	"LS_NightPalace",
	"LS_Heli",
	"LS_Basement",
	"LS_Mine",
	"LS_Cave",
	"LS_DayPine",
	"LS_NightPine",
	"LS_DayMilitary",
	"LS_NightMilitary",
	"LS_DaySAM",
	"LS_NightSAM",
	"LS_DayPrison",
	"LS_NightPrison",
	"LS_DayHospital",
	"LS_NightHospital",
	"LS_DayAirport",
	"LS_NightAirport",
	"LS_DayLab",
	"LS_NightLab",
	"LS_DayOmerta",
	"LS_NightOmerta",
	"LS_DayChitzena",
	"LS_NightChitzena",
	"LS_DayMine",
	"LS_NightMine",
	"LS_DayBalime",
	"LS_NightBalime",
};

//returns the UINT8 ID for the specified sector.
// Which for sectors above ground is one of { HELI, DAY, NIGHT }
// if gGameExternalOptions.gfUseExternalLoadscreens.
// Otherwise and for underground sectors this is the actual loadsceen ID.
UINT8 GetLoadScreenID(INT16 sSectorX, INT16 sSectorY, INT8 bSectorZ)
{
	const UINT8 ubSectorID = SECTOR( sSectorX, sSectorY );
	const BOOLEAN fNight = NightTime(); //before 5AM or after 9PM

	requestedX = sSectorX; requestedY = sSectorY; requestedZ = bSectorZ;

	/* User made system - BEGIN */
	if (gGameExternalOptions.gfUseExternalLoadscreens)
	{
		szSector = szSectorMap [sSectorY][sSectorX];
		if (DidGameJustStart())
			return DidGameJustStart() ? HELI : (fNight ? NIGHT : DAY);

		else if (bSectorZ != 0)
			return UNDERGROUND;

		else
			return fNight ? NIGHT : DAY;
		
	} /* WANNE: User made System - END */

	/* WANNE: Sir-Tech System - BEGIN */
	else
	{
		// Which map layer? (ground, basement 1-3)?
		switch( bSectorZ )
		{
			// Ground
			case 0:
			{
				switch( ubSectorID )
				{
					case SEC_A2:
					case SEC_B2:
						return fNight ? LOADINGSCREEN_NIGHTCHITZENA : LOADINGSCREEN_DAYCHITZENA;
					case SEC_A9:
							return DidGameJustStart() ? LOADINGSCREEN_HELI :
								(fNight ? LOADINGSCREEN_NIGHTOMERTA : LOADINGSCREEN_DAYOMERTA);
					case SEC_A10:
						return fNight ? LOADINGSCREEN_NIGHTOMERTA : LOADINGSCREEN_DAYOMERTA;
					case SEC_P3:
						return fNight ? LOADINGSCREEN_NIGHTPALACE : LOADINGSCREEN_DAYPALACE;
					case SEC_H13:
					case SEC_H14: //military installations
					case SEC_I13:
					case SEC_N7:
						return fNight ? LOADINGSCREEN_NIGHTMILITARY : LOADINGSCREEN_DAYMILITARY;
					case SEC_K4:
						return fNight ? LOADINGSCREEN_NIGHTLAB : LOADINGSCREEN_DAYLAB;
					case SEC_J9:
						return fNight ? LOADINGSCREEN_NIGHTPRISON : LOADINGSCREEN_DAYPRISON;
					case SEC_D2:
					case SEC_D15:
					case SEC_I8:
					case SEC_N4:
						return fNight ? LOADINGSCREEN_NIGHTSAM : LOADINGSCREEN_DAYSAM;
					case SEC_F8:
						return fNight ? LOADINGSCREEN_NIGHTHOSPITAL : LOADINGSCREEN_DAYHOSPITAL;
					case SEC_B13:
					case SEC_N3:
						return fNight ? LOADINGSCREEN_NIGHTAIRPORT : LOADINGSCREEN_DAYAIRPORT;
					case SEC_L11:
					case SEC_L12:
						return fNight ? LOADINGSCREEN_NIGHTBALIME : LOADINGSCREEN_DAYBALIME;
					case SEC_H3:
					case SEC_H8:
					case SEC_D4:
						return fNight ? LOADINGSCREEN_NIGHTMINE : LOADINGSCREEN_DAYMINE;
				}

				SECTORINFO *pSector = &SectorInfo[ ubSectorID ];
				switch( pSector->ubTraversability[ THROUGH_STRATEGIC_MOVE ] )
				{
					case TOWN:
						return Random(2) > 0 ?
							(fNight ? LOADINGSCREEN_NIGHTTOWN2 : LOADINGSCREEN_DAYTOWN2) :
							(fNight ? LOADINGSCREEN_NIGHTTOWN1 : LOADINGSCREEN_DAYTOWN1);
					case SAND:
					case SAND_ROAD:
						return fNight ? LOADINGSCREEN_NIGHTDESERT : LOADINGSCREEN_DAYDESERT;
					case FARMLAND:
					case FARMLAND_ROAD:
					case ROAD:
						return fNight ? LOADINGSCREEN_NIGHTGENERIC : LOADINGSCREEN_DAYGENERIC;
					case PLAINS:
					case SPARSE:
					case HILLS:
					case PLAINS_ROAD:
					case SPARSE_ROAD:
					case HILLS_ROAD:
						return fNight ? LOADINGSCREEN_NIGHTWILD : LOADINGSCREEN_DAYWILD;
					case DENSE:
					case SWAMP:
					case SWAMP_ROAD:
					case DENSE_ROAD:
						return fNight ? LOADINGSCREEN_NIGHTFOREST : LOADINGSCREEN_DAYFOREST;
					case TROPICS:
					case TROPICS_ROAD:
					case WATER:
					case NS_RIVER:
					case EW_RIVER:
					case COASTAL:
					case COASTAL_ROAD:
						return fNight ? LOADINGSCREEN_NIGHTTROPICAL : LOADINGSCREEN_DAYTROPICAL;
					default:
						Assert( 0 );
						return fNight ? LOADINGSCREEN_NIGHTGENERIC : LOADINGSCREEN_DAYGENERIC;
				}
			/* WANNE: Sir-Tech System */
		}
#ifdef JA2UB

		case 1:
		{
			switch( ubSectorID )
			{
				case SEC_I13:
				case SEC_J13:
					return fNight ? LOADINGSCREEN_MINE : LOADINGSCREEN_MINE;
				//tunnels
				case SEC_J14:
				case SEC_K14:
					return LOADINGSCREEN_TUNNELS;
				case SEC_K15:
				{
					if( gJa25SaveStruct.ubLoadScreenStairTraversal == LS__GOING_UP_STAIRS )
						return  LOADINGSCREEN_UP_STAIRS;
					else if( gJa25SaveStruct.ubLoadScreenStairTraversal == LS__GOING_DOWN_STAIRS )
						return LOADINGSCREEN_DOWN_STAIRS;
					else
						return LOADINGSCREEN_COMPLEX_BASEMENT_GENERIC;
				}
				default:
					return LOADINGSCREEN_BASEMENT;
			}
		}
		case 2:
		{
			switch( ubSectorID )
			{
				case SEC_K15:
				{	
					//if we are going up stairs, else traversing at same level
					if( gJa25SaveStruct.ubLoadScreenStairTraversal == LS__GOING_UP_STAIRS )
						return LOADINGSCREEN_UP_STAIRS;
					else if( gJa25SaveStruct.ubLoadScreenStairTraversal == LS__GOING_DOWN_STAIRS )
						return LOADINGSCREEN_DOWN_STAIRS;
					else
						return LOADINGSCREEN_COMPLEX_BASEMENT;
				}
				case SEC_L15:
				{
					//if we are going up stairs, else traversing at same level
					if( gJa25SaveStruct.ubLoadScreenStairTraversal == LS__GOING_UP_STAIRS )
						return LOADINGSCREEN_UP_STAIRS;
					else
						return LOADINGSCREEN_COMPLEX_BASEMENT;
				}
				default:
					return LOADINGSCREEN_BASEMENT;
			}
		}
		case 3:
		{
			switch( ubSectorID )
			{
				case SEC_L15:
				{
					//if we are going up stairs, else traversing at same level
					if( gJa25SaveStruct.ubLoadScreenStairTraversal == LS__GOING_DOWN_STAIRS )
						return LOADINGSCREEN_DOWN_STAIRS;
					else
						return LOADINGSCREEN_COMPLEX_BASEMENT_GENERIC;
				}
				default:
					return LOADINGSCREEN_CAVE;
			}
		}
		break;
			return LOADINGSCREEN_CAVE;
		default:

    /*
    case 1:
    case 2:
    case 3:
         return LOADINGSCREEN_CAVE;
    break;
	return LOADINGSCREEN_CAVE;
   default:
   */
#else
			// Basement Level 1
			case 1:
				switch( ubSectorID )
				{
					case SEC_A10:	//Miguel's basement
					case SEC_I13:	//Alma prison dungeon
					case SEC_J9:	//Tixa prison dungeon
					case SEC_K4:	//Orta weapons plant
					case SEC_O3:	//Meduna
					case SEC_P3:	//Meduna
						return LOADINGSCREEN_BASEMENT;
					default:		//rest are mines
						return LOADINGSCREEN_MINE;
				}
			// Basement Level 2 and 3
			case 2:
			case 3:
				//all level 2 and 3 maps are caves!
				return LOADINGSCREEN_CAVE;
			default:
#endif
				// shouldn't ever happen
				Assert( FALSE );
				return fNight ? LOADINGSCREEN_NIGHTGENERIC : LOADINGSCREEN_DAYGENERIC;
		}
	
	} /* WANNE: Sir-Tech System - END */
}

static void BuildLoadscreenFilename(std::string& dst, const char* path, int resolution, const char* ext)
{
	if (path)
		dst.append(path);

	if (ext)
	{
		dst.append(".");
		dst.append(ext);
	}
	else
		// This might suck later on. If it does, remove it.
		dst.append(".sti");
}

std::string GetResolutionSuffix(SCREEN_RESOLUTION resolution)
{
	switch (resolution)
	{
		case _960x540: return "_960x540";
		case _800x600: return "_800x600";
		case _1024x600: return "_1024x600";
		case _1280x720: return "_1280x720";
		case _1024x768: return "_1024x768";
		case _1280x768: return "_1280x768";
		case _1360x768: return "_1360x768";
		case _1366x768: return "_1366x768";
		case _1280x800: return "_1280x800";
		case _1440x900: return "_1440x900";
		case _1600x900: return "_1600x900";
		case _1280x960: return "_1280x960";
		case _1440x960: return "_1440x960";
		case _1770x1000: return "_1770x1000";
		case _1280x1024: return "_1280x1024";
		case _1360x1024: return "_1360x1024";
		case _1600x1024: return "_1600x1024";
		case _1440x1050: return "_1440x1050";
		case _1680x1050: return "_1680x1050";
		case _1920x1080: return "_1920x1080";
		case _1600x1200: return "_1600x1200";
		case _1920x1200: return "_1920x1200";
		case _2560x1440: return "_2560x1440";
		case _2560x1600: return "_2560x1600";
		default: return "";
	}
}

std::string FindBestFittingLoadscreenFilename(const std::string& baseName, SCREEN_RESOLUTION resolution)
{
	for (SCREEN_RESOLUTION res = resolution; res <= _2560x1600; res = (SCREEN_RESOLUTION)(res + 1))
	{
		std::string fileName = baseName + GetResolutionSuffix(res);
		if (FileExists((CHAR8*)((fileName + ".png").c_str())))
		{
			return fileName + ".png";
		}

		if (FileExists((CHAR8*)((fileName + ".sti").c_str())))
		{
			return fileName + ".sti";
		}
	}

	if (FileExists((CHAR8*)((baseName + ".png").c_str())))
	{
		return baseName + ".png";
	}

	return baseName + ".sti";
}

// Documentary loadscreen pool support.  The generated pack uses predictable
// pool/file names, so the game can discover it through FileExists() without
// depending on OS directory enumeration or on SectorLoadscreens.xml limits.
static const INT32 REAL_CONFLICT_POOL_COUNT = 9;
static const UINT32 REAL_CONFLICT_MAX_FILES_PER_POOL = 500;
static const UINT32 REAL_CONFLICT_RECENT_HISTORY = 32;

static const CHAR8* gRealConflictPoolCodes[REAL_CONFLICT_POOL_COUNT] =
{
	"TM", "JM", "RM", "AF", "AR", "CT", "IN", "RU", "JG"
};

static BOOLEAN gRealConflictPoolScanned[REAL_CONFLICT_POOL_COUNT] =
{
	FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE
};

static std::vector<std::string> gRealConflictPoolFiles[REAL_CONFLICT_POOL_COUNT];
static std::vector<std::string> gRecentRealConflictLoadscreens;

static INT32 GetRealConflictPoolIndex(const CHAR8* pCode)
{
	if (pCode == NULL)
		return -1;

	for (INT32 i = 0; i < REAL_CONFLICT_POOL_COUNT; ++i)
	{
		if (strcmp(gRealConflictPoolCodes[i], pCode) == 0)
			return i;
	}

	return -1;
}

static void ScanRealConflictPool(INT32 iPool)
{
	if (iPool < 0 || iPool >= REAL_CONFLICT_POOL_COUNT || gRealConflictPoolScanned[iPool])
		return;

	gRealConflictPoolScanned[iPool] = TRUE;

	for (UINT32 uiSeq = 1; uiSeq <= REAL_CONFLICT_MAX_FILES_PER_POOL; ++uiSeq)
	{
		CHAR8 szPath[260];
		sprintf(
			szPath,
			"LOADSCREENS\\\\RealConflict\\\\%s\\\\%s_%03u_1920x1080.png",
			gRealConflictPoolCodes[iPool],
			gRealConflictPoolCodes[iPool],
			uiSeq);

		if (FileExists(szPath))
			gRealConflictPoolFiles[iPool].push_back(szPath);
	}
}

static BOOLEAN WasRealConflictLoadscreenShownRecently(const std::string& imagePath)
{
	for (std::vector<std::string>::const_iterator it = gRecentRealConflictLoadscreens.begin();
		 it != gRecentRealConflictLoadscreens.end(); ++it)
	{
		if (*it == imagePath)
			return TRUE;
	}

	return FALSE;
}

static BOOLEAN PickRealConflictPoolImage(const CHAR8* pPoolCode, std::string& imagePath)
{
	const INT32 iPool = GetRealConflictPoolIndex(pPoolCode);
	if (iPool < 0)
		return FALSE;

	ScanRealConflictPool(iPool);
	const std::vector<std::string>& files = gRealConflictPoolFiles[iPool];
	if (files.empty())
		return FALSE;

	std::vector<UINT32> eligible;
	for (UINT32 i = 0; i < files.size(); ++i)
	{
		if (!WasRealConflictLoadscreenShownRecently(files[i]))
			eligible.push_back(i);
	}

	// Small pools eventually exhaust the global history.  When that happens,
	// permit older images again but still avoid an immediate repeat whenever
	// there is more than one choice.
	if (eligible.empty())
	{
		for (UINT32 i = 0; i < files.size(); ++i)
		{
			if (gRecentRealConflictLoadscreens.empty() ||
				files.size() == 1 ||
				files[i] != gRecentRealConflictLoadscreens.back())
			{
				eligible.push_back(i);
			}
		}
	}

	if (eligible.empty())
		eligible.push_back(0);

	const UINT32 uiChoice = eligible[Random((UINT32)eligible.size())];
	imagePath = files[uiChoice];

	gRecentRealConflictLoadscreens.push_back(imagePath);
	if (gRecentRealConflictLoadscreens.size() > REAL_CONFLICT_RECENT_HISTORY)
		gRecentRealConflictLoadscreens.erase(gRecentRealConflictLoadscreens.begin());

	return TRUE;
}

static std::string LowercaseLoadscreenPath(const std::string& path)
{
	std::string lower(path);
	for (size_t i = 0; i < lower.size(); ++i)
		lower[i] = (CHAR8)tolower((unsigned char)lower[i]);
	return lower;
}

static BOOLEAN LoadscreenPathContains(const std::string& lowerPath, const CHAR8* pText)
{
	return lowerPath.find(pText) != std::string::npos;
}

static const CHAR8* GetRealConflictPoolForContext(const std::string& legacyImagePath, UINT8 ubLoadScreenID)
{
	// Documentary photographs are intended for above-ground travel/combat
	// transitions.  Purpose-built cave/mine/basement art remains preferable
	// underground.
	if (requestedZ != 0 ||
		ubLoadScreenID == UNDERGROUND ||
		ubLoadScreenID == LOADINGSCREEN_BASEMENT ||
		ubLoadScreenID == LOADINGSCREEN_MINE ||
		ubLoadScreenID == LOADINGSCREEN_CAVE)
	{
		return NULL;
	}

	if (ubLoadScreenID == HELI || ubLoadScreenID == LOADINGSCREEN_HELI)
		return "AF";

	const std::string path = LowercaseLoadscreenPath(legacyImagePath);

	if (LoadscreenPathContains(path, "airport") ||
		LoadscreenPathContains(path, "airfield") ||
		LoadscreenPathContains(path, "airdrop") ||
		LoadscreenPathContains(path, "airborne") ||
		LoadscreenPathContains(path, "parachut") ||
		LoadscreenPathContains(path, "heli") ||
		LoadscreenPathContains(path, "plane"))
		return "AF";

	if (LoadscreenPathContains(path, "tank") ||
		LoadscreenPathContains(path, "armor") ||
		LoadscreenPathContains(path, "armour") ||
		LoadscreenPathContains(path, "apc"))
		return "AR";

	if (LoadscreenPathContains(path, "bridge") ||
		LoadscreenPathContains(path, "dam") ||
		LoadscreenPathContains(path, "power") ||
		LoadscreenPathContains(path, "oil") ||
		LoadscreenPathContains(path, "refinery") ||
		LoadscreenPathContains(path, "pipeline"))
		return "IN";

	if (LoadscreenPathContains(path, "ruin") ||
		LoadscreenPathContains(path, "destroy") ||
		LoadscreenPathContains(path, "wreck") ||
		LoadscreenPathContains(path, "burn"))
		return "RU";

	if (LoadscreenPathContains(path, "coast") ||
		LoadscreenPathContains(path, "beach") ||
		LoadscreenPathContains(path, "harbor") ||
		LoadscreenPathContains(path, "harbour") ||
		LoadscreenPathContains(path, "port") ||
		LoadscreenPathContains(path, "dock") ||
		LoadscreenPathContains(path, "island"))
		return "CT";

	if (LoadscreenPathContains(path, "town") ||
		LoadscreenPathContains(path, "city") ||
		LoadscreenPathContains(path, "street") ||
		LoadscreenPathContains(path, "military") ||
		LoadscreenPathContains(path, "army") ||
		LoadscreenPathContains(path, "prison") ||
		LoadscreenPathContains(path, "palace") ||
		LoadscreenPathContains(path, "hospital") ||
		LoadscreenPathContains(path, "warehouse") ||
		LoadscreenPathContains(path, "barrack") ||
		LoadscreenPathContains(path, "police") ||
		LoadscreenPathContains(path, "mall") ||
		LoadscreenPathContains(path, "sam"))
		return "TM";

	if (LoadscreenPathContains(path, "jungle") ||
		LoadscreenPathContains(path, "tropical") ||
		LoadscreenPathContains(path, "forest") ||
		LoadscreenPathContains(path, "swamp") ||
		LoadscreenPathContains(path, "river") ||
		LoadscreenPathContains(path, "bamboo"))
		return "JM";

	if (LoadscreenPathContains(path, "farm") ||
		LoadscreenPathContains(path, "village") ||
		LoadscreenPathContains(path, "valley") ||
		LoadscreenPathContains(path, "trail") ||
		LoadscreenPathContains(path, "wild") ||
		LoadscreenPathContains(path, "desert") ||
		LoadscreenPathContains(path, "road"))
		return "RM";

	// If the authored loadscreen name is generic, use strategic terrain as a
	// second source of context rather than choosing a totally unrelated photo.
	if (requestedX > 0 && requestedX < MAP_WORLD_X - 1 &&
		requestedY > 0 && requestedY < MAP_WORLD_Y - 1)
	{
		SECTORINFO* pSector = &SectorInfo[SECTOR(requestedX, requestedY)];
		switch (pSector->ubTraversability[THROUGH_STRATEGIC_MOVE])
		{
			case TOWN:
				return "TM";
			case TROPICS:
			case TROPICS_ROAD:
			case DENSE:
			case DENSE_ROAD:
			case SWAMP:
			case SWAMP_ROAD:
			case WATER:
			case NS_RIVER:
			case EW_RIVER:
				return "JM";
			case COASTAL:
			case COASTAL_ROAD:
				return "CT";
			default:
				return "RM";
		}
	}

	return "RM";
}

static BOOLEAN PickRealConflictLoadscreen(const std::string& legacyImagePath, UINT8 ubLoadScreenID, std::string& imagePath)
{
	const CHAR8* pPrimaryPool = GetRealConflictPoolForContext(legacyImagePath, ubLoadScreenID);
	if (pPrimaryPool == NULL)
		return FALSE;

	if (PickRealConflictPoolImage(pPrimaryPool, imagePath))
		return TRUE;

	// Context-aware fallbacks keep the feature useful while a photo category is
	// sparse, but never replace underground screens.
	const CHAR8* pFallback1 = "JM";
	const CHAR8* pFallback2 = "TM";

	if (strcmp(pPrimaryPool, "AF") == 0)
	{
		pFallback1 = "JM";
		pFallback2 = "TM";
	}
	else if (strcmp(pPrimaryPool, "CT") == 0 || strcmp(pPrimaryPool, "RM") == 0 || strcmp(pPrimaryPool, "JG") == 0)
	{
		pFallback1 = "JM";
		pFallback2 = "TM";
	}
	else if (strcmp(pPrimaryPool, "IN") == 0 || strcmp(pPrimaryPool, "RU") == 0 || strcmp(pPrimaryPool, "AR") == 0)
	{
		pFallback1 = "TM";
		pFallback2 = "JM";
	}
	else if (strcmp(pPrimaryPool, "JM") == 0)
	{
		pFallback1 = "TM";
		pFallback2 = "AF";
	}
	else if (strcmp(pPrimaryPool, "TM") == 0)
	{
		pFallback1 = "JM";
		pFallback2 = "AF";
	}

	if (strcmp(pPrimaryPool, pFallback1) != 0 && PickRealConflictPoolImage(pFallback1, imagePath))
		return TRUE;
	if (strcmp(pPrimaryPool, pFallback2) != 0 && strcmp(pFallback1, pFallback2) != 0 &&
		PickRealConflictPoolImage(pFallback2, imagePath))
		return TRUE;

	return FALSE;
}

//sets up the loadscreen with specified ID, and draws it to the FRAME_BUFFER,
//and refreshing the screen with it.
void DisplayLoadScreenWithID( UINT8 ubLoadScreenID )
{
	VSURFACE_DESC		vs_desc = {};
	HVSURFACE			hVSurface;
	UINT32				uiLoadScreen;

	vs_desc.fCreateFlags = VSURFACE_CREATE_FROMFILE | VSURFACE_SYSTEM_MEM_USAGE | VSURFACE_CREATE_FROMPNG_FALLBACK;

	szSector = szSectorMap [gWorldSectorY][gWorldSectorX];
	const BOOLEAN fExternalLS = (szSector != NULL && strcmp(szSector, "N") != 0) && ((DAY <= ubLoadScreenID && ubLoadScreenID <= NIGHT_ALT) || (ubLoadScreenID == UNDERGROUND));
	
	if (fExternalLS)
	{
		// Get the image path
		std::string imagePath;
		std::string imageFormat;

		// Start screen
		if (ubLoadScreenID == HELI)
		{
			imagePath = gSectorLoadscreens[0].szDay;
			imageFormat = gSectorLoadscreens[0].szImageFormat;
		}
		else if (ubLoadScreenID == UNDERGROUND)
		{
			g_luaUnderground.GetLoadscreen(requestedX, requestedY, requestedZ, imagePath, imageFormat);
		}
		else
		{
			for (int i = 1; i < MAX_SECTOR_LOADSCREENS; i++)
			{
				// We found the sector
				if (strcmp(gSectorLoadscreens[i].szLocation, szSector) == 0)
				{
					imageFormat = gSectorLoadscreens[i].szImageFormat;

					BOOLEAN fAlternate = gSectorLoadscreens[i].RandomAltSector;

					if (fAlternate && (Random(2) > 0))
					{
						if (strlen(gSectorLoadscreens[i].szImageFormatAlt) > 0)
							imageFormat = gSectorLoadscreens[i].szImageFormatAlt;
						switch (ubLoadScreenID)
						{
							case DAY:
								ubLoadScreenID = DAY_ALT;
								break;
							case NIGHT:
								ubLoadScreenID = NIGHT_ALT;
								break;
							default:
								break;
						}
					}

					switch (ubLoadScreenID)
					{
						case DAY:
							imagePath = gSectorLoadscreens[i].szDay;
							break;
						case DAY_ALT:
							imagePath = gSectorLoadscreens[i].szDayAlt;
							break;
						case NIGHT:
							imagePath = gSectorLoadscreens[i].szNight;
							break;
						case NIGHT_ALT:
							imagePath = gSectorLoadscreens[i].szNight;
							break;
					}
					break;
				}
			}
		}

		std::string strImage = FindBestFittingLoadscreenFilename(imagePath, (SCREEN_RESOLUTION)iResolution);
		std::string documentaryImage;
		if (PickRealConflictLoadscreen(strImage, ubLoadScreenID, documentaryImage))
			strImage = documentaryImage;
		strImage.copy(vs_desc.ImageFile, sizeof(vs_desc.ImageFile) - 1);
	}
	else
	{
		std::string strImage("LOADSCREENS\\");

		if (LOADINGSCREEN_NOTHING <= ubLoadScreenID && ubLoadScreenID <= LOADINGSCREEN_NIGHTBALIME)
		{
			strImage.append(LoadScreenNames[ubLoadScreenID]);
		}
		else
		{
			// for some reason the heli screen is the default
			strImage.append(LoadScreenNames[0]);
		}
		strImage = FindBestFittingLoadscreenFilename(strImage, (SCREEN_RESOLUTION)iResolution);

		std::string documentaryImage;
		if (PickRealConflictLoadscreen(strImage, ubLoadScreenID, documentaryImage))
			strImage = documentaryImage;

		strImage.copy(vs_desc.ImageFile, sizeof(vs_desc.ImageFile)-1);
	}


	if( gfSchedulesHosed )
	{
		SetFont( FONT10ARIAL );
		SetFontForeground( FONT_YELLOW );
		SetFontShadow( FONT_NEARBLACK );
		ColorFillVideoSurfaceArea( FRAME_BUFFER, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0 );
		mprintf( 5, 5, L"Error loading save, attempting to patch save to version 1.02...", vs_desc.ImageFile );
	}
	else
	{
		if (FileExists(vs_desc.ImageFile) && AddVideoSurface(&vs_desc, &uiLoadScreen))
		{
			SGPRect SrcRect, DstRect;
									
			//Blit the background image
			GetVideoSurface(&hVSurface, uiLoadScreen);

			fLoadingScreenAspectRatio = (FLOAT)hVSurface->usWidth / (FLOAT)hVSurface->usHeight;
			FLOAT fScreenAspectRatio = (FLOAT)SCREEN_WIDTH / (FLOAT)SCREEN_HEIGHT;

			if (gGameExternalOptions.ubLoadscreenStretchMode == 1) 
			{
				if (fLoadingScreenAspectRatio > fScreenAspectRatio) 
				{
					FLOAT fCropWidth = (FLOAT)hVSurface->usHeight * fScreenAspectRatio;
					SrcRect.iLeft = (hVSurface->usWidth - (INT32)fCropWidth) / 2;
					SrcRect.iRight = SrcRect.iLeft + (INT32)fCropWidth;
					SrcRect.iTop = 0;
					SrcRect.iBottom = hVSurface->usHeight;

					DstRect.iLeft = 0;
					DstRect.iTop = 0;
					DstRect.iRight = SCREEN_WIDTH;
					DstRect.iBottom = SCREEN_HEIGHT;
				}
				else 
				{
					SrcRect.iLeft = 0;
					SrcRect.iRight = hVSurface->usWidth;
					SrcRect.iTop = 0;
					SrcRect.iBottom = hVSurface->usHeight;

					INT32 newWidth = (INT32)(SCREEN_HEIGHT * fLoadingScreenAspectRatio);
					DstRect.iLeft = (SCREEN_WIDTH - newWidth) / 2;
					DstRect.iRight = DstRect.iLeft + newWidth;
					DstRect.iTop = 0;
					DstRect.iBottom = SCREEN_HEIGHT;

				}
			}
			else if (gGameExternalOptions.ubLoadscreenStretchMode == 2) 
			{
				if (fLoadingScreenAspectRatio < fScreenAspectRatio) 
				{
					FLOAT cropHeight = (FLOAT)hVSurface->usWidth / fScreenAspectRatio;
					SrcRect.iTop = (hVSurface->usHeight - (INT32)cropHeight) / 2;
					SrcRect.iBottom = SrcRect.iTop + (INT32)cropHeight;
					SrcRect.iLeft = 0;
					SrcRect.iRight = hVSurface->usWidth;

					DstRect.iLeft = 0;
					DstRect.iTop = 0;
					DstRect.iRight = SCREEN_WIDTH;
					DstRect.iBottom = SCREEN_HEIGHT;
				}
				else 
				{
					SrcRect.iTop = 0;
					SrcRect.iBottom = hVSurface->usHeight;
					SrcRect.iLeft = 0;
					SrcRect.iRight = hVSurface->usWidth;

					INT32 newHeight = (INT32)(SCREEN_WIDTH / fLoadingScreenAspectRatio);
					DstRect.iLeft = 0;
					DstRect.iRight = 0;
					DstRect.iTop = (SCREEN_HEIGHT - newHeight) / 2;
					DstRect.iBottom = DstRect.iTop + newHeight;
				}

			}
			else
			{
				// vanilla (stretch to fit)
				// Stretch the background image
				SrcRect.iLeft = 0;
				SrcRect.iTop = 0;
				SrcRect.iRight = hVSurface->usWidth;
				SrcRect.iBottom = hVSurface->usHeight;

				DstRect.iLeft = 0;
				DstRect.iTop = 0;
				DstRect.iRight = SCREEN_WIDTH;
				DstRect.iBottom = SCREEN_HEIGHT;
			}

			BltStretchVideoSurface( FRAME_BUFFER, uiLoadScreen, 0, 0, 0, &SrcRect, &DstRect );
						
			DeleteVideoSurfaceFromIndex( uiLoadScreen );			
		}
		else
		{
			 //Failed to load the file, so use a black screen and print out message.
			SetFont( FONT10ARIAL );
			SetFontForeground( FONT_YELLOW );
			SetFontShadow( FONT_NEARBLACK );
			ColorFillVideoSurfaceArea( FRAME_BUFFER, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0 );
			mprintf( 5, 5, L"%S loadscreen data file not found...", vs_desc.ImageFile );
		}
	}

	gubLastLoadingScreenID = ubLoadScreenID;
	InvalidateScreen( );
	ExecuteBaseDirtyRectQueue();
	EndFrameBufferRender();
	RefreshScreen( NULL );
}
