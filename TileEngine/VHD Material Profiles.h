#ifndef __VHD_MATERIAL_PROFILES_H
#define __VHD_MATERIAL_PROFILES_H

#include "types.h"
#include "TileDat.h"

// Visual-only sector identities. These values never participate in map,
// structure, collision, LOS, AI, pathfinding, destruction or save logic.
enum SectorVisualProfile
{
	SECTOR_VISUAL_DEFAULT = 0,
	SECTOR_VISUAL_ORONEGRO_TOWN,
	SECTOR_VISUAL_A3_FARM,
	SECTOR_VISUAL_ORONEGRO_OIL_RIG,
	SECTOR_VISUAL_SAN_MONA_C5_STRIP,
	SECTOR_VISUAL_SAN_MONA_C6_EAST,
	SECTOR_VISUAL_SAN_MONA_D4_MINE,
	SECTOR_VISUAL_SAN_MONA_D5_KINGPIN,
	SECTOR_VISUAL_SAN_MONA_UNDERGROUND
};

enum VHDVisualMaterialClass
{
	VHD_MATERIAL_GENERIC = 0,
	VHD_MATERIAL_WATER,
	VHD_MATERIAL_DEEP_OR_AUX_WATER,
	VHD_MATERIAL_TERRAIN,
	VHD_MATERIAL_GREEN_TERRAIN,
	VHD_MATERIAL_VEGETATION,
	VHD_MATERIAL_WALL,
	VHD_MATERIAL_ROOF,
	VHD_MATERIAL_FLOOR,
	VHD_MATERIAL_ROAD,
	VHD_MATERIAL_ONROOF,
	VHD_MATERIAL_MACHINERY,
	VHD_MATERIAL_INTERIOR,
	VHD_MATERIAL_DECAL,
	VHD_MATERIAL_DEBRIS_PALE,
	VHD_MATERIAL_DEBRIS_RUST,
	VHD_MATERIAL_DEBRIS_GREEN,
	VHD_MATERIAL_DEBRIS_GENERIC
};

struct VHDVisualGrade
{
	INT32 saturationPercent;
	INT32 contrastPercent;
	INT32 redBias;
	INT32 greenBias;
	INT32 blueBias;
};

static VHDVisualGrade VHDMakeVisualGrade(
	INT32 saturationPercent, INT32 contrastPercent,
	INT32 redBias, INT32 greenBias, INT32 blueBias )
{
	VHDVisualGrade grade;
	grade.saturationPercent = saturationPercent;
	grade.contrastPercent = contrastPercent;
	grade.redBias = redBias;
	grade.greenBias = greenBias;
	grade.blueBias = blueBias;
	return grade;
}

static BOOLEAN VHDIsSanMonaVisualProfile( UINT8 ubProfile )
{
	return ubProfile == SECTOR_VISUAL_SAN_MONA_C5_STRIP ||
		ubProfile == SECTOR_VISUAL_SAN_MONA_C6_EAST ||
		ubProfile == SECTOR_VISUAL_SAN_MONA_D4_MINE ||
		ubProfile == SECTOR_VISUAL_SAN_MONA_D5_KINGPIN ||
		ubProfile == SECTOR_VISUAL_SAN_MONA_UNDERGROUND;
}

static BOOLEAN VHDMaterialIsDebris( VHDVisualMaterialClass material )
{
	return material == VHD_MATERIAL_DEBRIS_PALE ||
		material == VHD_MATERIAL_DEBRIS_RUST ||
		material == VHD_MATERIAL_DEBRIS_GREEN ||
		material == VHD_MATERIAL_DEBRIS_GENERIC;
}

static BOOLEAN VHDMaterialIsWater( VHDVisualMaterialClass material )
{
	return material == VHD_MATERIAL_WATER ||
		material == VHD_MATERIAL_DEEP_OR_AUX_WATER;
}

static VHDVisualMaterialClass VHDClassifyVisualMaterial( UINT32 ubType )
{
	if ( ubType == REGWATERTEXTURE )
		return VHD_MATERIAL_WATER;

	if ( ubType == DEEPWATERTEXTURE || ubType == ANOTHERDEBRIS )
		return VHD_MATERIAL_DEEP_OR_AUX_WATER;

	if ( ubType >= THIRDTEXTURE && ubType <= SIXTHTEXTURE )
		return VHD_MATERIAL_GREEN_TERRAIN;

	if ( ubType >= FIRSTTEXTURE && ubType <= SEVENTHTEXTURE )
		return VHD_MATERIAL_TERRAIN;

	if ( (ubType >= FIRSTOSTRUCT && ubType <= SEVENTHOSTRUCT) ||
		 ubType == FIRSTFULLSTRUCT || ubType == SECONDFULLSTRUCT )
		return VHD_MATERIAL_VEGETATION;

	if ( ubType >= FIRSTWALL && ubType <= LASTDOOR )
		return VHD_MATERIAL_WALL;

	if ( ubType >= FIRSTROOF && ubType <= LASTSLANTROOF )
		return VHD_MATERIAL_ROOF;

	if ( ubType >= FIRSTFLOOR && ubType <= LASTFLOOR )
		return VHD_MATERIAL_FLOOR;

	if ( (ubType >= FIRSTROAD && ubType <= LASTROAD) || ubType == ROADPIECES )
		return VHD_MATERIAL_ROAD;

	if ( ubType >= FIRSTONROOF && ubType <= LASTONROOF )
		return VHD_MATERIAL_ONROOF;

	if ( ubType == EIGHTOSTRUCT || ubType == THRIDISTRUCT ||
		 ubType == FIRSTVEHICLE || ubType == SECONDVEHICLE ||
		 ubType == TENTHOSTRUCT || ubType == FENCESTRUCT )
		return VHD_MATERIAL_MACHINERY;

	if ( ubType >= FIRSTISTRUCT && ubType <= FIRSTCISTRUCT )
		return VHD_MATERIAL_INTERIOR;

	if ( ubType >= FIRSTWALLDECAL && ubType <= EIGTHWALLDECAL )
		return VHD_MATERIAL_DECAL;

	if ( ubType == DEBRISROCKS || ubType == DEBRISMISC )
		return VHD_MATERIAL_DEBRIS_PALE;

	if ( ubType == DEBRISWOOD || ubType == DEBRISSAND || ubType == DEBRIS2MISC )
		return VHD_MATERIAL_DEBRIS_RUST;

	if ( ubType == DEBRISWEEDS || ubType == DEBRISGRASS )
		return VHD_MATERIAL_DEBRIS_GREEN;

	return VHD_MATERIAL_GENERIC;
}

// Resolve the exact grading constants previously embedded in worlddef.cpp.
// This first E8 step is deliberately appearance-preserving: architecture
// changes now, visual tuning changes later under separate QA.
static VHDVisualGrade VHDResolveVisualGrade(
	UINT8 ubProfile, VHDVisualMaterialClass material, BOOLEAN fReplacementLoaded )
{
	VHDVisualGrade grade = VHDMakeVisualGrade( 106, 106, 2, 1, 0 );

	if ( VHDIsSanMonaVisualProfile( ubProfile ) )
	{
		switch ( ubProfile )
		{
			case SECTOR_VISUAL_SAN_MONA_C5_STRIP:
				grade = VHDMakeVisualGrade( 112, 118, 7, 2, -5 );
				break;
			case SECTOR_VISUAL_SAN_MONA_D4_MINE:
				grade = VHDMakeVisualGrade( 96, 120, 8, 2, -8 );
				break;
			case SECTOR_VISUAL_SAN_MONA_D5_KINGPIN:
				grade = VHDMakeVisualGrade( 105, 122, 7, 2, -6 );
				break;
			case SECTOR_VISUAL_SAN_MONA_UNDERGROUND:
				grade = VHDMakeVisualGrade( 88, 118, -1, 2, 3 );
				break;
			default:
				break; // C6 intentionally retains the historical base grade.
		}

		switch ( material )
		{
			case VHD_MATERIAL_ROAD:
				grade.saturationPercent -= 8; grade.contrastPercent += 5;
				grade.redBias -= 2; grade.greenBias -= 2; grade.blueBias -= 1;
				break;
			case VHD_MATERIAL_WALL:
				grade.contrastPercent += 3; grade.redBias += 3; grade.blueBias -= 2;
				break;
			case VHD_MATERIAL_ROOF:
				grade.contrastPercent += 5; grade.redBias += 4;
				grade.greenBias -= 1; grade.blueBias -= 3;
				break;
			case VHD_MATERIAL_FLOOR:
				grade.saturationPercent -= 7; grade.contrastPercent += 3; grade.blueBias -= 2;
				break;
			case VHD_MATERIAL_VEGETATION:
			case VHD_MATERIAL_GREEN_TERRAIN:
				grade.saturationPercent += 7; grade.greenBias += 7;
				grade.redBias -= 3; grade.blueBias -= 2;
				break;
			case VHD_MATERIAL_TERRAIN:
				grade.redBias += 4; grade.greenBias += 2; grade.blueBias -= 4;
				break;
			case VHD_MATERIAL_INTERIOR:
				grade.saturationPercent -= 3; grade.contrastPercent += 2;
				break;
			case VHD_MATERIAL_DEBRIS_PALE:
			case VHD_MATERIAL_DEBRIS_RUST:
			case VHD_MATERIAL_DEBRIS_GREEN:
			case VHD_MATERIAL_DEBRIS_GENERIC:
				grade.contrastPercent += 4; grade.redBias += 3; grade.blueBias -= 3;
				break;
			default:
				break;
		}
		return grade;
	}

	if ( ubProfile == SECTOR_VISUAL_A3_FARM )
	{
		grade = VHDMakeVisualGrade( 111, 114, 3, 4, -3 );
		switch ( material )
		{
			case VHD_MATERIAL_WATER:
			case VHD_MATERIAL_DEEP_OR_AUX_WATER:
				return VHDMakeVisualGrade( 116, 115, -8, 7, 10 );
			case VHD_MATERIAL_GREEN_TERRAIN:
				return VHDMakeVisualGrade( 120, 116, -4, 11, -6 );
			case VHD_MATERIAL_TERRAIN:
				return VHDMakeVisualGrade( 112, 116, 9, 5, -9 );
			case VHD_MATERIAL_VEGETATION:
				return VHDMakeVisualGrade( 122, 118, -4, 12, -6 );
			case VHD_MATERIAL_WALL:
				return VHDMakeVisualGrade( 96, 118, 6, 3, -5 );
			case VHD_MATERIAL_ROOF:
				return VHDMakeVisualGrade( 101, 121, 8, 2, -7 );
			case VHD_MATERIAL_FLOOR:
				return VHDMakeVisualGrade( 94, 116, 5, 3, -5 );
			case VHD_MATERIAL_DEBRIS_PALE:
			case VHD_MATERIAL_DEBRIS_RUST:
			case VHD_MATERIAL_DEBRIS_GREEN:
			case VHD_MATERIAL_DEBRIS_GENERIC:
				return VHDMakeVisualGrade( 108, 118, 6, 4, -6 );
			default:
				return grade;
		}
	}

	if ( ubProfile == SECTOR_VISUAL_ORONEGRO_OIL_RIG )
	{
		grade = VHDMakeVisualGrade( 104, fReplacementLoaded ? 116 : 114, 3, 2, -2 );
		switch ( material )
		{
			case VHD_MATERIAL_WATER:
			case VHD_MATERIAL_DEEP_OR_AUX_WATER:
				return VHDMakeVisualGrade( 118, 116, -10, 5, 13 );
			case VHD_MATERIAL_GREEN_TERRAIN:
				return VHDMakeVisualGrade( 114, 116, -2, 8, -5 );
			case VHD_MATERIAL_TERRAIN:
				return VHDMakeVisualGrade( 110, 116, 8, 5, -8 );
			case VHD_MATERIAL_FLOOR:
				return VHDMakeVisualGrade( 92, 120, 2, 1, -4 );
			case VHD_MATERIAL_ROOF:
				return VHDMakeVisualGrade( 105, 122, 8, 2, -7 );
			case VHD_MATERIAL_ONROOF:
				return VHDMakeVisualGrade( 108, 124, 8, 1, -8 );
			case VHD_MATERIAL_WALL:
				return VHDMakeVisualGrade( 98, 118, 6, 3, -5 );
			case VHD_MATERIAL_ROAD:
				return VHDMakeVisualGrade( 88, 118, 2, 1, -4 );
			case VHD_MATERIAL_VEGETATION:
				return VHDMakeVisualGrade( 116, 118, -3, 9, -4 );
			case VHD_MATERIAL_MACHINERY:
				return VHDMakeVisualGrade( 104, 124, 7, 1, -7 );
			case VHD_MATERIAL_INTERIOR:
				return VHDMakeVisualGrade( 96, 116, 4, 2, -4 );
			case VHD_MATERIAL_DECAL:
				return VHDMakeVisualGrade( 112, 120, 6, 3, -4 );
			case VHD_MATERIAL_DEBRIS_PALE:
				return VHDMakeVisualGrade( 90, 118, 4, 3, -3 );
			case VHD_MATERIAL_DEBRIS_RUST:
				return VHDMakeVisualGrade( 108, 122, 8, 1, -8 );
			case VHD_MATERIAL_DEBRIS_GREEN:
				return VHDMakeVisualGrade( 108, 116, -1, 6, -4 );
			default:
				return grade;
		}
	}

	if ( ubProfile == SECTOR_VISUAL_ORONEGRO_TOWN )
	{
		if ( material == VHD_MATERIAL_TERRAIN || material == VHD_MATERIAL_GREEN_TERRAIN ||
			 material == VHD_MATERIAL_WATER )
		{
			grade.saturationPercent += 4;
			grade.contrastPercent += 2;
		}
		else if ( material == VHD_MATERIAL_WALL || material == VHD_MATERIAL_ROOF )
		{
			grade.contrastPercent += 2;
			grade.saturationPercent -= 2;
		}
	}

	return grade;
}

static void VHDApplyMaterialTone(
	UINT8 ubProfile, VHDVisualMaterialClass material, INT32 luma,
	INT32 *pOutR, INT32 *pOutG, INT32 *pOutB )
{
	if ( pOutR == NULL || pOutG == NULL || pOutB == NULL )
		return;

	if ( VHDIsSanMonaVisualProfile( ubProfile ) )
	{
		if ( luma < 86 )
		{
			const INT32 depth = 86 - luma;
			*pOutR -= 2 + depth / 24;
			*pOutG -= 1 + depth / 32;
			*pOutB += ( ubProfile == SECTOR_VISUAL_SAN_MONA_UNDERGROUND ) ?
				(4 + depth / 18) : (2 + depth / 28);
		}
		else if ( luma > 172 )
		{
			const INT32 light = luma - 172;
			*pOutR += 3 + light / 22;
			*pOutG += 2 + light / 30;
			if ( ubProfile == SECTOR_VISUAL_SAN_MONA_C6_EAST )
				*pOutG += 2;
		}

		if ( material == VHD_MATERIAL_VEGETATION || material == VHD_MATERIAL_GREEN_TERRAIN )
		{
			*pOutR -= 1; *pOutG += 3; *pOutB -= 1;
		}
		else if ( material == VHD_MATERIAL_ROAD || material == VHD_MATERIAL_FLOOR )
		{
			*pOutR -= 1; *pOutG -= 1;
		}
		else if ( material == VHD_MATERIAL_WALL || material == VHD_MATERIAL_ROOF ||
				  VHDMaterialIsDebris( material ) )
		{
			*pOutR += 2; *pOutB -= 2;
		}
		return;
	}

	if ( ubProfile == SECTOR_VISUAL_A3_FARM )
	{
		if ( luma < 82 )
		{
			const INT32 depth = 82 - luma;
			*pOutR -= 2 + depth / 24;
			*pOutB += VHDMaterialIsWater( material ) ?
				(5 + depth / 16) : (2 + depth / 30);
		}
		else if ( luma > 174 )
		{
			const INT32 light = luma - 174;
			if ( !VHDMaterialIsWater( material ) )
				*pOutR += 3 + light / 22;
			*pOutG += 2 + light / 28;
			if ( VHDMaterialIsWater( material ) )
				*pOutB += 4 + light / 18;
		}

		if ( material == VHD_MATERIAL_VEGETATION || material == VHD_MATERIAL_GREEN_TERRAIN )
		{
			*pOutR -= 1; *pOutG += 3; *pOutB -= 1;
		}
		else if ( material == VHD_MATERIAL_ROOF || material == VHD_MATERIAL_WALL ||
				  VHDMaterialIsDebris( material ) )
		{
			*pOutR += 2; *pOutB -= 2;
		}
		return;
	}

	if ( ubProfile == SECTOR_VISUAL_ORONEGRO_OIL_RIG )
	{
		if ( luma < 84 )
		{
			const INT32 depth = 84 - luma;
			*pOutR -= 3 + depth / 18;
			*pOutG -= 1 + depth / 30;
			*pOutB += VHDMaterialIsWater( material ) ?
				(6 + depth / 14) : (2 + depth / 24);
		}
		else if ( luma > 176 )
		{
			const INT32 light = luma - 176;
			if ( VHDMaterialIsWater( material ) )
			{
				*pOutG += 3 + light / 20;
				*pOutB += 5 + light / 15;
			}
			else
			{
				*pOutR += 4 + light / 18;
				*pOutG += 2 + light / 24;
				*pOutB += light / 40;
			}
		}

		if ( material == VHD_MATERIAL_FLOOR || material == VHD_MATERIAL_ROAD )
		{
			*pOutR -= 2;
			*pOutG -= 2;
		}
		else if ( material == VHD_MATERIAL_ROOF || material == VHD_MATERIAL_ONROOF ||
				  material == VHD_MATERIAL_MACHINERY )
		{
			*pOutR += 3;
			*pOutB -= 3;
		}
		else if ( material == VHD_MATERIAL_VEGETATION )
		{
			*pOutR -= 2;
			*pOutG += 4;
			*pOutB -= 1;
		}
	}
}

#endif