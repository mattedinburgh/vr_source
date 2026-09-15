# VHD material / visual-profile architecture

## Purpose

E8 separates sector art direction from tactical-world loading/rendering code.
The material layer is visual-only: it must not change map geometry, tile identity,
JSD data, collision, LOS, cover, movement, destruction, AI, scripts or saves.

The first E8 milestone is deliberately appearance-preserving. Existing A3,
B1/Oronegro and San Mona palette grades and luma tone rules are moved into a
shared material/profile resolver before any new visual tuning is attempted.

## Current architecture

Tile loading classifies each tactical tile into a reusable visual material class,
then resolves the sector profile plus that material into a visual grade.

Current material classes include:

- water, terrain and green terrain;
- vegetation;
- walls, roofs, floors and roads;
- on-roof objects and machinery;
- interiors and decals;
- pale, rust/wood and vegetation debris families;
- generic fallback.
The renderer-facing call sequence is:

1. VHDClassifyVisualMaterial(tileType)
2. VHDResolveVisualGrade(profile, material, replacementLoaded)
3. existing palette grade math
4. VHDApplyMaterialTone(profile, material, luma, rgb)

This removes profile-specific grading policy from worlddef.cpp while keeping
the existing palette renderer and its output contract intact.

## Safety contract

- SECTOR_VISUAL_DEFAULT remains untouched.
- Dedicated shadow sprites and non-world/UI tile types are not graded.
- Palette index 0 remains untouched.
- Only 8-bit tactical tile palettes use this first-stage resolver.
- True-colour B1TC/VHD imagery remains on its existing per-pixel renderer.
- STI fallback behaviour is unchanged.
- No tactical or strategic AI files are in scope.

## Validation gate

Before integration into engine/vhd-modernization-2026:

- VS2013/v120 Release compile must pass;
- hosted renderer/SGP/TileEngine/Tactical/Editor builds must pass;
- startup smoke must pass;
- C5 visual QA must pass;
- representative A3/B1/C5 palette outputs must remain visually equivalent.
## Next E8/E9 work

After parity is proven:

1. replace hard-coded grade constants with declarative profile records;
2. allow reusable material presets shared across sectors;
3. move authored weathering/variant generation into the VHD asset compiler;
4. add true-colour material-response hooks for Lighting 2.0;
5. add deterministic terrain variation without changing logical tile identity.

Lighting, anti-repetition and authored-material changes are intentionally separate
from this refactor so visual regressions can be attributed to one change at a time.

[executed on device: MSI (e4de0d4a-2679-4f35-acaf-b6552e7ec980)]