#!/usr/bin/env python3
"""
Post-port refinement for the Vengeance Map Factory pilot.

This is intentionally applied after replaying the proven Map Factory commits onto
install/all-2026-09-12.  It fixes two problems visible in successful run #99:
  * semi-random debris-kit placement looked like dressing, not authored redesign;
  * wilderness/military grading pushed large sectors too far into dark green.

The patch is strict: if the expected proven code is not present it aborts instead
of silently producing a partial integration.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAPUTIL = ROOT / "Utils" / "MapUtility.cpp"
WORLDDEF = ROOT / "TileEngine" / "worlddef.cpp"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def replace_if_present(text: str, old: str, new: str, label: str) -> tuple[str, bool]:
    if old not in text:
        return text, False
    return text.replace(old, new), True


def patch_maputility() -> None:
    text = MAPUTIL.read_text(encoding="utf-8", errors="strict")

    old = r'''static UINT32 MapFactoryApplyPilotDesign( UINT8 ubArchetype )
{
	if ( gpWorldLevelData == NULL )
		return 0;

	const INT32 sRows = WORLD_MAX / WORLD_COLS;
	INT32 sLastRow = -1000, sLastCol = -1000;
	UINT32 uiKits = 0, uiPieces = 0;

	for ( INT32 sRow = 5; sRow < sRows - 10 && uiKits < 6; sRow += 3 )
	{
		for ( INT32 sCol = 5; sCol < WORLD_COLS - 10 && uiKits < 6; sCol += 3 )
		{
			const INT32 sGridNo = sRow * WORLD_COLS + sCol;
			if ( !MapFactoryAnchorMatches( sGridNo, ubArchetype ) )
				continue;

			if ( abs( sRow - sLastRow ) + abs( sCol - sLastCol ) < 32 )
				continue;

			const UINT32 uiHash = (UINT32)sGridNo * 2654435761u + (UINT32)ubArchetype * 2246822519u;
			if ( (uiHash & 1u) != 0u )
				continue;

			const UINT16 usPlaced = MapFactoryPlaceCompositionKit( sGridNo, ubArchetype, uiHash );
			if ( usPlaced >= 7 )
			{
				++uiKits;
				uiPieces += usPlaced;
				sLastRow = sRow;
				sLastCol = sCol;
			}
		}
	}

	CHAR8 zStatus[176];
	_snprintf( zStatus, sizeof(zStatus) - 1, "PILOT_KIT_DESIGN archetype=%u kits=%lu pieces=%lu",
		(UINT16)ubArchetype, uiKits, uiPieces );
	zStatus[sizeof(zStatus) - 1] = 0;
	MapPreviewWriteStatus( zStatus );
	return uiPieces;
}'''

    new = r'''typedef struct
{
	UINT8 ubRowPercent;
	UINT8 ubColPercent;
} MAP_FACTORY_PILOT_POINT;

static INT32 MapFactoryFindPilotAnchor( UINT8 ubRowPercent, UINT8 ubColPercent,
	UINT8 ubArchetype )
{
	if ( gpWorldLevelData == NULL )
		return NOWHERE;

	const INT32 sRows = WORLD_MAX / WORLD_COLS;
	const INT32 sTargetRow = (INT32)((UINT32)(sRows - 1) * ubRowPercent / 100u);
	const INT32 sTargetCol = (INT32)((UINT32)(WORLD_COLS - 1) * ubColPercent / 100u);

	// Deterministic expanding diamond search.  A sector always converges on the
	// same authored neighbourhood, but we still respect the actual map geometry.
	for ( INT32 sRadius = 0; sRadius <= 28; ++sRadius )
	{
		for ( INT32 dRow = -sRadius; dRow <= sRadius; ++dRow )
		{
			const INT32 dColAbs = sRadius - abs( dRow );
			const INT32 dCol[2] = { -dColAbs, dColAbs };
			const UINT8 ubChoices = dColAbs == 0 ? 1 : 2;
			for ( UINT8 i = 0; i < ubChoices; ++i )
			{
				const INT32 sRow = sTargetRow + dRow;
				const INT32 sCol = sTargetCol + dCol[i];
				if ( sRow < 5 || sRow >= sRows - 10 || sCol < 5 || sCol >= WORLD_COLS - 10 )
					continue;
				const INT32 sGridNo = sRow * WORLD_COLS + sCol;
				if ( MapFactoryAnchorMatches( sGridNo, ubArchetype ) )
					return sGridNo;
			}
		}
	}
	return NOWHERE;
}

static UINT32 MapFactoryPilotSeed( const STR8 pMapName, UINT8 ubIndex, UINT8 ubArchetype )
{
	UINT32 uiHash = 2166136261u;
	if ( pMapName != NULL )
	{
		for ( const CHAR8 *p = pMapName; *p != 0; ++p )
		{
			CHAR8 c = *p;
			if ( c >= 'A' && c <= 'Z' ) c = (CHAR8)(c - 'A' + 'a');
			uiHash ^= (UINT8)c;
			uiHash *= 16777619u;
		}
	}
	uiHash ^= (UINT32)ubIndex * 2246822519u;
	uiHash ^= (UINT32)ubArchetype * 3266489917u;
	return uiHash;
}

static UINT32 MapFactoryApplyPilotDesign( const STR8 pMapName, UINT8 ubArchetype )
{
	if ( gpWorldLevelData == NULL || pMapName == NULL )
		return 0;

	CHAR8 zBase[64];
	MapFactoryBaseName( pMapName, zBase, sizeof(zBase) );

	// These are art-director targets, not random samples.  The engine searches
	// locally for a geometry-safe anchor, keeping authored placement stable even
	// when a source map receives small unrelated edits.
	static const MAP_FACTORY_PILOT_POINT gA8[] =
	{
		{18,24}, {20,70}, {43,50}, {63,22}, {66,72}, {82,47}
	};
	static const MAP_FACTORY_PILOT_POINT gA12[] =
	{
		{16,18}, {18,68}, {39,42}, {58,77}, {74,25}, {82,60}
	};
	static const MAP_FACTORY_PILOT_POINT gB13[] =
	{
		{18,26}, {20,65}, {40,43}, {55,76}, {69,25}, {80,58}
	};
	static const MAP_FACTORY_PILOT_POINT gF15[] =
	{
		{18,20}, {22,70}, {42,45}, {58,78}, {72,25}, {82,58}
	};
	static const MAP_FACTORY_PILOT_POINT gDefault[] =
	{
		{20,20}, {20,70}, {45,45}, {62,76}, {75,24}, {82,58}
	};

	const MAP_FACTORY_PILOT_POINT *pPoints = gDefault;
	UINT8 ubCount = (UINT8)(sizeof(gDefault) / sizeof(gDefault[0]));
	if ( _stricmp( zBase, "A8" ) == 0 )
	{
		pPoints = gA8; ubCount = (UINT8)(sizeof(gA8) / sizeof(gA8[0]));
	}
	else if ( _stricmp( zBase, "A12" ) == 0 )
	{
		pPoints = gA12; ubCount = (UINT8)(sizeof(gA12) / sizeof(gA12[0]));
	}
	else if ( _stricmp( zBase, "b13" ) == 0 )
	{
		pPoints = gB13; ubCount = (UINT8)(sizeof(gB13) / sizeof(gB13[0]));
	}
	else if ( _stricmp( zBase, "f15" ) == 0 )
	{
		pPoints = gF15; ubCount = (UINT8)(sizeof(gF15) / sizeof(gF15[0]));
	}

	UINT32 uiKits = 0, uiPieces = 0;
	for ( UINT8 i = 0; i < ubCount; ++i )
	{
		const INT32 sGridNo = MapFactoryFindPilotAnchor(
			pPoints[i].ubRowPercent, pPoints[i].ubColPercent, ubArchetype );
		if ( sGridNo == NOWHERE )
			continue;

		const UINT32 uiSeed = MapFactoryPilotSeed( zBase, i, ubArchetype );
		const UINT16 usPlaced = MapFactoryPlaceCompositionKit( sGridNo, ubArchetype, uiSeed );
		if ( usPlaced >= 5 )
		{
			++uiKits;
			uiPieces += usPlaced;

			CHAR8 zAnchor[160];
			_snprintf( zAnchor, sizeof(zAnchor) - 1,
				"PILOT_ANCHOR sector=%s index=%u grid=%ld pieces=%u",
				zBase, (UINT16)i, sGridNo, usPlaced );
			zAnchor[sizeof(zAnchor) - 1] = 0;
			MapPreviewWriteStatus( zAnchor );
		}
	}

	CHAR8 zStatus[208];
	_snprintf( zStatus, sizeof(zStatus) - 1,
		"PILOT_AUTHORED_DESIGN sector=%s archetype=%u kits=%lu pieces=%lu",
		zBase, (UINT16)ubArchetype, uiKits, uiPieces );
	zStatus[sizeof(zStatus) - 1] = 0;
	MapPreviewWriteStatus( zStatus );
	return uiPieces;
}'''

    text = replace_once(text, old, new, "replace procedural pilot design")
    text = replace_once(
        text,
        "const UINT32 uiPieces = MapFactoryApplyPilotDesign( ubArchetype );",
        "const UINT32 uiPieces = MapFactoryApplyPilotDesign( pMapName, ubArchetype );",
        "update pilot design call",
    )

    # Make composition kits read as authored site detail, not pure ground debris.
    replacements = [
        (
            """case MAP_FACTORY_MILITARY:
				uiTypes[i] = (i % 4 == 0) ? DEBRISMISC :
					(i % 4 == 1) ? DEBRISWOOD :
					(i % 4 == 2) ? DEBRISROCKS : DEBRISWEEDS;
				break;""",
            """case MAP_FACTORY_MILITARY:
				uiTypes[i] = (i % 4 == 0) ? FIRSTDECORATIONS :
					(i % 4 == 1) ? SECONDDECORATIONS :
					(i % 4 == 2) ? DEBRISROCKS : DEBRISWOOD;
				break;""",
            "military material mix",
        ),
        (
            """case MAP_FACTORY_INDUSTRIAL:
				uiTypes[i] = (i % 4 == 0) ? DEBRISMISC :
					(i % 4 == 1) ? DEBRISWOOD :
					(i % 4 == 2) ? DEBRISROCKS : DEBRISGRASS;
				break;""",
            """case MAP_FACTORY_INDUSTRIAL:
				uiTypes[i] = (i % 4 == 0) ? FIRSTDECORATIONS :
					(i % 4 == 1) ? SECONDDECORATIONS :
					(i % 4 == 2) ? THIRDDECORATIONS : DEBRISMISC;
				break;""",
            "industrial material mix",
        ),
        (
            """case MAP_FACTORY_SETTLEMENT:
				uiTypes[i] = (i % 4 == 0) ? DEBRISMISC :
					(i % 4 == 1) ? DEBRISWOOD :
					(i % 4 == 2) ? DEBRISWEEDS : DEBRISROCKS;
				break;""",
            """case MAP_FACTORY_SETTLEMENT:
				uiTypes[i] = (i % 4 == 0) ? FIRSTDECORATIONS :
					(i % 4 == 1) ? SECONDDECORATIONS :
					(i % 4 == 2) ? DEBRISWEEDS : DEBRISWOOD;
				break;""",
            "settlement material mix",
        ),
        (
            """default:
				uiTypes[i] = (i % 4 == 0) ? DEBRISWEEDS :
					(i % 4 == 1) ? DEBRISROCKS :
					(i % 4 == 2) ? DEBRISGRASS : DEBRISWOOD;
				break;""",
            """default:
				uiTypes[i] = (i % 4 == 0) ? DEBRISWEEDS :
					(i % 4 == 1) ? DEBRISROCKS :
					(i % 4 == 2) ? DEBRISGRASS : FOURTHDECORATIONS;
				break;""",
            "wilderness/open-country material mix",
        ),
    ]
    for old_mix, new_mix, label in replacements:
        text = replace_once(text, old_mix, new_mix, label)

    MAPUTIL.write_text(text, encoding="utf-8", newline="
")


def patch_worlddef() -> None:
    text = WORLDDEF.read_text(encoding="utf-8", errors="strict")
    changed = []

    # 32-bit surfaces: pull back the dark emerald cast while preserving useful
    # material separation introduced by the successful cache-safe renderer.
    pairs = [
        (
            "sat = 132; contrast = 115; rBias = -4; gBias = 12; bBias = -5;",
            "sat = 118; contrast = 115; rBias = -2; gBias = 6; bBias = -3;",
            "truecolor wilderness base",
        ),
        (
            "targetR = fGreen ? 54 : 151; targetG = fGreen ? 137 : 111; targetB = fGreen ? 58 : 62;\n\t\t\tmix = fGreen ? 28 : 20;",
            "targetR = fGreen ? 68 : 151; targetG = fGreen ? 118 : 111; targetB = fGreen ? 62 : 62;\n\t\t\tmix = fGreen ? 18 : 16;",
            "truecolor wilderness green",
        ),
        (
            "sat = 116; contrast = 120; rBias = 4; gBias = 5; bBias = -4;",
            "sat = 108; contrast = 120; rBias = 4; gBias = 3; bBias = -3;",
            "truecolor military base",
        ),
        (
            "if ( fGreen ) { targetR = 57; targetG = 126; targetB = 58; mix = 24; }",
            "if ( fGreen ) { targetR = 68; targetG = 112; targetB = 62; mix = 17; }",
            "truecolor military green",
        ),
        (
            "sat = 132; contrast = 116; rBias = 8; gBias = 9; bBias = -8;",
            "sat = 120; contrast = 116; rBias = 7; gBias = 6; bBias = -6;",
            "truecolor farmland base",
        ),
        (
            "targetR = fGreen ? 64 : 165; targetG = fGreen ? 148 : 102; targetB = fGreen ? 55 : 55;\n\t\t\tmix = fGreen ? 28 : 23;",
            "targetR = fGreen ? 75 : 165; targetG = fGreen ? 130 : 102; targetB = fGreen ? 60 : 55;\n\t\t\tmix = fGreen ? 20 : 18;",
            "truecolor farmland green",
        ),
    ]

    for old, new, label in pairs:
        text, did = replace_if_present(text, old, new, label)
        if did:
            changed.append(label)

    # 8-bit/palette surfaces.  These exact lines come from the proven colourful
    # pass; keep colour identity but stop globally flooding wilderness/military
    # sectors with green.
    palette_pairs = [
        ("saturationPercent = 126; contrastPercent = 115;\n\t\t\tredBias = -4; greenBias = 11; blueBias = -4;",
         "saturationPercent = 116; contrastPercent = 115;\n\t\t\tredBias = -2; greenBias = 6; blueBias = -3;",
         "palette wilderness base"),
        ("if ( fMFGreen ) { saturationPercent = 142; contrastPercent = 118; redBias = -8; greenBias = 20; blueBias = -9; }",
         "if ( fMFGreen ) { saturationPercent = 124; contrastPercent = 118; redBias = -5; greenBias = 12; blueBias = -6; }",
         "palette wilderness green"),
        ("saturationPercent = 112; contrastPercent = 120;\n\t\t\tredBias = 4; greenBias = 4; blueBias = -3;",
         "saturationPercent = 108; contrastPercent = 120;\n\t\t\tredBias = 4; greenBias = 3; blueBias = -3;",
         "palette military base"),
        ("if ( fMFGreen ) { saturationPercent = 126; greenBias = 14; redBias = -5; blueBias = -7; }",
         "if ( fMFGreen ) { saturationPercent = 116; greenBias = 9; redBias = -3; blueBias = -5; }",
         "palette military green"),
        ("if ( fMFGreen ) { saturationPercent = 128; contrastPercent = 118; redBias = -5; greenBias = 14; blueBias = -7; }",
         "if ( fMFGreen ) { saturationPercent = 118; contrastPercent = 118; redBias = -3; greenBias = 9; blueBias = -5; }",
         "palette industrial green"),
        ("if ( fMFGreen ) { saturationPercent = 132; greenBias = 15; redBias = -5; blueBias = -7; }",
         "if ( fMFGreen ) { saturationPercent = 122; greenBias = 10; redBias = -3; blueBias = -5; }",
         "palette settlement green"),
    ]
    for old, new, label in palette_pairs:
        text, did = replace_if_present(text, old, new, label)
        if did:
            changed.append(label)

    if len(changed) < 6:
        raise SystemExit(
            "worlddef colour restraint patch found too few expected patterns: "
            + ", ".join(changed)
        )

    WORLDDEF.write_text(text, encoding="utf-8", newline="
")
    print("Applied colour restraint:", ", ".join(changed))


def main() -> None:
    patch_maputility()
    patch_worlddef()
    print("Map Factory v2 pilot refinement applied.")


if __name__ == "__main__":
    main()
