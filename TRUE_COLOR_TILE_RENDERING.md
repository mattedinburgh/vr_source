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

The renderer now has two true-colour asset inputs:

- **B1TC** — the active B1/Oronegro sibling format. A `foo.b1tc` file automatically replaces the pixels of `foo.sti` while retaining the STI name as map/JSD identity.
- **JPC/PNG** — the more generic replacement route retained for other assets.

Implemented engine behavior:

- multi-region 16/32-bit VOBJECT metadata and frame counts;
- full RGB/RGBA source pixels kept until final RGB565 presentation;
- JA2 shade-level lighting;
- stable source-coordinate RGB565 dithering;
- RGBA alpha compositing;
- ordinary Z test/write and legacy obscured checkerboard reveal;
- JSD-derived multi-tile Z-strip depth;
- wall equal-Z burn-through semantics;
- true-colour shadow and intensity masks;
- STI appdata inheritance for B1TC sibling replacements;
- 8-bit-only item/physics/erase-Z special blitters guarded from true-colour objects.

Recent true-colour commits include:

- `d6877f1f` multi-region true-colour VOBJECTs
- `18e27cb4` initial shaded/Z-aware true-colour blitter
- `71b167a7` ordered RGB565 dithering
- `1a15b062` hardened JPC frame loading
- `93dbc53b` alpha/dither/full-black fixes
- `a175cded` alpha-aware simple Z writes
- `9b848a96` legacy-special-blitter guards
- `1479a9a8` / `cedef5ece9` B1TC reader and STI-sibling selection
- `d771acee` / `0b32098d` true-colour JSD Z-strip rendering
- `57a19bf0` / `8a552fc2` true-colour shadow/intensity masks
- `ffa05d19` / `059a44f3` ordinary obscured-Z parity
- `4ec7f343` inherit original STI appdata for B1TC
- `0c1b247b` O(1) per-pixel Z-strip depth lookup via per-blit prefix sums
- `a5c13e8c` match legacy multi-Z depth-write behavior

### B1 asset validation

The current B1TC payloads were compared against their original STI frame directories. The following sets have **zero frame-geometry mismatches**: frame counts, width, height, X offset and Y offset all match exactly.

- `B1_P-FLOOR3` — 10/10
- `B1_ROADTLE2` — 312/312
- `B1_W-ROOF2` — 14/14
- `B1_BUILD_31` — 65/65
- `B1_BUILD_35` — 65/65
- `B1_BUILD_36` — 65/65
- `B1_BUILD_40` — 65/65
- `B1_TR_WATER` — 46/46
- `B1_TRWATER2` — 47/47
- `B1_WELFLOR1/2/3` — 8/8 each

Structural frame counts also match JSD counts exactly: `B1_W-ROOF2` 14/14 and each validated `B1_BUILD_*` facade set 65/65.

All audited B1TC records have valid `width * height * 4` RGBA payload sizes. Current B1TC assets use binary alpha (0/255), so there is no ambiguous partial-alpha collision edge in the current B1 remaster.


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

### Remaining legacy-only areas
Keep these indexed for now:
- merc/soldier sprites and palette-swapped body art;
- item-outline/glow imagery;
- merc trans-shadow/index-254 semantics;
- special merc invisibility/translucency paths.

Map walls, roofs and structures may now use true-colour imagery when their B1TC/JPC frame geometry and JSD counts are preserved. JSD Z strips, wall equal-Z behavior, ordinary obscured rendering, and shadow/intensity map masks now have true-colour paths.

RGBA simple-Z writes use an alpha cutout threshold: pixels below 128 alpha may blend visually but do not become solid Z blockers. Current audited B1TC assets are binary-alpha, so their behavior is unambiguous.

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
2. Enter B1 from a clean/new save and verify true-colour ground, road, water, roof and facade loading in black-box traces.
3. Visually verify day/night shade progression, roof hiding/reveal, wall occlusion and equal-Z intersections.
4. Test sector save/reload and leave/re-enter B1.
5. Keep merc/item/special translucency art indexed until those specialist blitters are deliberately ported.
6. Revisit a native 32-bit framebuffer only after the B1 true-colour path is runtime-stable.
