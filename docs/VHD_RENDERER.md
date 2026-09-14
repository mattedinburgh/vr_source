# Vengeance HD (VHD) renderer

Experimental renderer work. This branch is intentionally isolated from `install/all-2026-09-12`.

## Goal

Increase tactical-world graphical resolution without changing map topology, pathfinding, LOS, collision, destruction, AI, or saved-map coordinates.

Logical world geometry remains unchanged:

- `WORLD_TILE_X = 40`
- `WORLD_TILE_Y = 20`
- `CELL_X_SIZE = 10`
- `CELL_Y_SIZE = 10`

VHD adds an independent tactical render scale:

- 1x = legacy
- 2x = HD target / first production milestone
- 4x = later optional target

## Current implementation

The branch currently provides:

1. A render-scale API in `TileEngine/Isometric Utils.*`.
2. Scale-aware integer and floating-point world-to-screen projection.
3. Matching inverse screen-to-world projection, including mouse/world mapping.
4. Absolute world/screen conversions routed through the same projection path.
5. Experimental INI options under `[Graphics Settings]`:
   - `VHD_RENDER_SCALE=1|2|4`
   - `VHD_PREFER_NATIVE_ASSETS=TRUE|FALSE`
6. Safe default is 1x.

## Architecture decision

Do **not** change `WORLD_TILE_X`, `WORLD_TILE_Y`, `CELL_X_SIZE`, or `CELL_Y_SIZE`.
Those constants are used by rendering, interaction, physics, lighting, scrolling, cursor logic and structure handling.

Do **not** simply enlarge legacy STI sprites and feed them to the existing renderer.

The main blocker is JA2's structure depth system. JSD-derived Z-strip information is expressed in legacy sprite-pixel space (including the historical 20-pixel strip cadence). Blindly scaling a wall/structure image while leaving its Z-strip semantics unchanged causes incorrect occlusion/depth ordering.

## Target design

### Coordinate layer

World simulation remains 1x. Only world-to-screen projection is scaled.

### Asset layer

Prefer native VHD assets when present, with per-sprite metadata describing their authored scale.

Proposed lookup order at 2x:

1. native 2x VHD asset
2. generated 2x cache/fallback derived from legacy asset
3. legacy asset only when VHD is disabled

The final asset resolver must support multi-subimage animation/tile packages, not only single PNG replacements.

### Depth layer

Z-strip evaluation must become scale-aware.

For a rendered source X coordinate, depth lookup must resolve against the equivalent legacy/JSD coordinate:

`legacySourceX = renderedSourceX / renderScale`

This should be implemented in the C++ depth-aware renderer rather than trying to mutate JSD data or duplicate strip records.

### Legacy 8-bit assets

There are many hand-written/assembly 8-bit blitters. Rewriting every blitter is not the preferred first path.

Preferred migration direction:

- retain legacy 8-bit path at scale 1
- use the existing true-colour C++ renderer as the VHD path
- extend true-colour image/package loading to support multi-subimage assets
- allow legacy imagery to be converted/cached into the VHD representation where no native HD art exists

This gives one maintainable scale-aware C++ path for:
- clipping
- alpha
- shading
- Z-buffering
- JSD Z strips
- obscured rendering
- occlusion bubble behaviour

### Soldiers and tactical effects

VHD cannot stop at map tiles. Merc animations, corpses, projectiles, effects, shadows and tactical markers must eventually obey the same world render scale or proportions will be wrong.

The soldier animation loader is separate from `LoadTileSurface`, so it will be integrated after the tile/depth prototype proves correct.

## Milestones

### VHD-0 — projection foundation
Status: implemented on this branch.

- 1x/2x/4x projection
- inverse mapping
- INI switch
- 1x default

### VHD-1 — native 2x true-colour tile prototype

Use a small controlled test set:
- ground tile
- wall
- door/window
- roof
- prop

Requirements:
- correct anchor offsets
- clipping
- lighting/shading
- JSD Z strip
- occlusion bubble
- mouse interaction
- destruction/door state remains unchanged

### VHD-2 — generated legacy fallback

Create/cache 2x VHD representations for missing art so mixed converted/unconverted sectors remain visually coherent.

### VHD-3 — tactical actors/effects

Scale or replace:
- merc/enemy animations
- corpses
- projectiles
- explosions/smoke
- shadows
- tactical world markers

### VHD-4 — 4x optional mode

Only after 2x is stable.

## Non-goals

- no map conversion
- no save-format change
- no AI/pathfinding change
- no collision/LOS change
- no merge into the canonical integration branch until the prototype is visually and mechanically validated
