# True-colour tactical tile rendering

Cross-chat handoff for the Vengeance Reloaded graphics remaster.

Tracking issue: #16

## Architecture decision

Do **not** change JA2 logical map/grid tile size.

Keep:
- existing map files and region indices;
- JSD structure data;
- 16-bit tactical framebuffer and Z-buffer;
- legacy 8-bit STI/ETRLE renderer as the default/fallback.

Add an opt-in true-colour imagery path:
- multi-frame JPC archive containing RGB/RGBA PNG frames;
- each PNG frame retains the same logical region index as the legacy STI;
- RGB frames are normalized to RGBA internally;
- HIMAGE/VOBJECT preserve every region and the normal object count;
- tactical rendering converts true-colour pixels into the existing 16-bit framebuffer;
- standard JA2 shade levels are applied;
- simple Z test/write is supported;
- alpha transparency is supported for RGBA.

## Current implementation on install/all-2026-09-12

Commits:
- `d6877f1f` preserve multi-region metadata for 16/32-bit VOBJECTs
- `367510e0` multi-frame true-colour JPC loading
- `6f23cc07` loader state-scope correction
- `02874050` declare tactical true-colour blitter
- `18e27cb4` shaded/Z-aware true-colour tactical blitter
- `ffca6183` route true-colour map imagery through renderworld
- `71b167a7` stable 4x4 ordered RGB565 dithering
- `93dbc53b` move dithering before alpha blend and preserve full-black shade
- `1a15b062` harden JPC frame sequencing and keep single-frame RGB at 32bpp
- `a175cded` prevent low-alpha edge pixels from writing solid Z
- `8db387c0` remove redundant explicit JPC fallback change (fallback was already the CreateImage default)
- `9b848a96` keep legacy-only item/physics/erase-Z blitters away from true-colour objects
- `0ac01ba2` preserve exact black/white channel endpoints while dithering

## Replacement archive format

For a legacy tileset such as:

`foo.sti`

an optional replacement may be supplied as:

`foo.jpc.7z`

containing sequential frames:

```
0.png
1.png
2.png
...
```

An existing XML appdata file inside the JPC remains supported.

The PNG frame count must remain compatible with the associated `foo.jsd` structure count when a JSD exists.

RGB (3-channel, 8-bit) and RGBA (4-channel, 8-bit) frames are accepted. A single archive must not mix indexed/paletted and true-colour frames.

Frame filenames are validated as a contiguous numeric sequence beginning with `0.png`. Missing indices are rejected instead of silently compacting later frames into the wrong tile index.

PNG X/Y offsets are part of tile compatibility. Remastered frames must preserve the original region offsets (PNG offset metadata) as well as frame order and dimensions where structure alignment depends on them.

## Rollout / safety

### Safe first targets
Use true-colour replacements first for:
- ground;
- grass terrain;
- trails;
- roads;
- floors;
- other imagery that does not rely on Z-strip increment logic.

### Do not convert yet
Keep legacy indexed rendering for:
- walls that rely on Z strips / burn-through rules;
- complex roofs/structures until Z-strip parity is added;
- shadows/intensity masks;
- merc/soldier sprites and palette-swapped body art;
- item-outline/glow imagery;
- special pixelation/obscured effects.

The generic true-colour path has simple Z test/write, but does not yet reproduce every specialized 8-bit blitter.

RGBA depth writes use an alpha cutout threshold: pixels below 128 alpha may blend visually but do not become solid Z blockers. Legacy-only item-outline, physics-object and erase-Z branches are guarded so true-colour objects fall through safely instead of calling 8-bit ETRLE blitters.

## Lighting

True-colour rendering maps normal JA2 shade levels onto per-channel RGB scaling.

Level 4 = neutral.

Levels 1-3 = brightening.

Levels 5-15 = progressive darkening.

Indexed shade level 0 is a special glow palette; true-colour map art treats level 0 as neutral to avoid injecting an unintended glow tint.

## Colour-depth policy

The source artwork may use full 24-bit RGB / 32-bit RGBA colour.

The tactical framebuffer remains RGB565 for now. This gives 65,536 physical output values, but true-colour source art is now converted with stable ordered dithering so gradients, vegetation, concrete, water and other textured surfaces retain much more apparent colour detail than direct RGB565 rounding.

The dither pattern is anchored to source-image coordinates so it does not crawl or shimmer when the camera scrolls.

A full 32-bit tactical framebuffer remains a possible later engine project, but it is deliberately **not** part of the current map-remaster rollout. Converting the global framebuffer would touch a large amount of legacy rendering, UI, font, cursor, assembly blitter and pitch-calculation code for a smaller visual improvement than the current 256-colour -> true-colour-source + dithered-RGB565 jump.

Decision: finish and validate the true-colour asset path first. Revisit a native 32-bit framebuffer only after terrain, Z-strip structures, shadows and lighting are stable.

## Compatibility rule

Deleting/renaming the optional `.jpc.7z` replacement must immediately return that tileset to the original STI renderer without a map conversion.

Note: this fallback behavior already existed in `CreateImage()` before the true-colour work. No extra Tile Surface override is required.

## Next engineering work

1. Build/test VS2013 Release Win32.
2. Test one isolated B1 ground/road tileset with a true-colour JPC replacement.
3. Verify day/night shade progression and sector save/reload.
4. Verify Z interaction for selected ground/floor cases.
5. Add true-colour Z-strip increment blitters before converting walls/roofs.
6. Add shadow/intensity/pixelation parity only after the terrain path is stable.
