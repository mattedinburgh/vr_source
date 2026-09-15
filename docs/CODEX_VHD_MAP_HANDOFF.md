# Codex handoff — Vengeance HD map production

You are taking over the **map/graphics production phase for Jagged Alliance 2: Vengeance Reloaded**.

The Vengeance HD renderer work has now passed its automated build/startup gate. Your job is to use the new renderer capabilities to begin producing genuinely higher-resolution tactical maps and assets. Do **not** redesign the simulation or reopen renderer architecture unless a reproducible production bug requires it.

## Project / branches

Source repository: `mattedinburgh/vr_source`

VHD source branch: `feature/vhd-renderer`

Canonical gameplay/source integration branch: `install/all-2026-09-12`

Game-data repository: `mattedinburgh/vr_gamedir`

VHD game-data test branch: `feature/vhd-renderer-test`

**Do not merge VHD into `install/all-2026-09-12` unless Matt explicitly asks.**

## What VHD can do now

VHD supports tactical render scales of **1x / 2x / 4x** while preserving the original JA2 simulation/map coordinate model.

The first production target is **2x at 1920x1080, 16:9**.

Do not globally change the legacy world/simulation constants such as `WORLD_TILE_X`, `WORLD_TILE_Y`, `CELL_X_SIZE`, or `CELL_Y_SIZE`. VHD scales the tactical projection/rendering layer instead.

### Native HD asset resolution

VHD can resolve parallel native assets such as:

- `VHD2/<original asset path>`
- `VHD4/<original asset path>`

If a native HD asset does not exist, the engine can fall back to the legacy asset.

Use this to convert the game incrementally and safely.

### Formats

Supported production paths include:

- legacy STI
- indexed / ETRLE-compressed sprites
- PNG
- JPC / true-colour paths

Do not blindly convert everything to RGBA. Soldier animations depend on indexed palettes for shirt/trouser/skin/hair recolouring and should remain indexed unless there is an explicit replacement plan.

### Scale-aware systems already implemented

The VHD renderer now has scale-aware handling for:

- central isometric projection
- inverse screen-to-world projection
- tactical render-loop stepping
- scrolling and render origins
- native/fallback asset resolution
- JSD depth/structure mapping
- indexed multi-Z rendering
- strict Z
- same-Z burn-through
- obscured rendering
- trans-shadow rendering
- soldier screen positions and hitboxes
- world-height offsets
- soldier hit-test sampling
- item locators
- merc/multipurpose locators
- burst targeting markers
- doors/windows interactive rectangles
- tactical interactive-tile anchors
- editor merc anchors
- editor patrol/schedule markers
- cached tactical animations loaded through tile surfaces
- fallback-scaler allocation guards for 32-bit memory safety

The old 16-bit framebuffer/Z-buffer remains in use.

## Hard rule: preserve gameplay semantics

The HD conversion is primarily a **graphics project**.

Unless Matt explicitly requests a gameplay/map-layout change, preserve:

- map dimensions
- sector architecture
- building footprints
- room layouts
- road/alley layout
- wall positions
- door positions
- window positions
- roofs
- object positions
- tile identity
- anchors
- JSD/structure semantics
- collision
- pathfinding
- LOS
- cover logic
- destruction
- penetration
- elevation
- save compatibility

A prettier wall must still be the **same wall** from the game's point of view.

## Art objective

Do not merely upscale old 40x20-style art and call it HD.

Native VHD2 artwork should add genuine new information:

- sharper construction detail
- material texture
- cracks
- dirt
- stains
- weathering
- masonry variation
- roof detail
- believable vegetation
- richer props
- realistic object construction
- environmental storytelling

The player should see an immediate, major improvement in before/after comparisons.

## Vengeance / Latin visual direction

The world should increasingly read as a believable Latin / Central-American-type environment appropriate to Vengeance's lore, without turning every area into the same stereotype.

Use sector function and lore to guide art direction.

Potential visual vocabulary includes:

- sun-faded paint
- worn plaster
- exposed concrete
- corrugated roofing
- improvised repairs
- aged shop fronts
- utility wiring
- local signage
- drainage marks
- puddles
- litter
- tyre marks
- boxes
- barrels
- oil stains
- tropical/subtropical weathering
- damaged pavement
- industrial/agricultural clutter appropriate to the location

A wealthy district, farm, mine, military site, slum, and San Mona should not all receive the same treatment.

## Research before redrawing

For every important asset family:

1. inspect the existing Vengeance asset;
2. inspect relevant JA2 1.13 equivalents;
3. identify what the sprite is actually intended to represent;
4. use real-world references where useful;
5. then redraw it.

Do not guess an object's real function from a tiny legacy sprite if the repository can tell you what it is.

## Decorative additions

Purely decorative map enrichments are allowed where appropriate, for example:

- rubbish
- papers
- cans
- puddles
- stains
- tyre marks
- cracks
- weeds
- debris
- oil marks
- broken plaster
- discarded boxes

These should normally avoid changing pathfinding, LOS, cover, collision, or structure logic.

## Pilot workflow

Start with a **small controlled pilot sector** before broad rollout.

Prefer **San Mona C5** unless repository state clearly shows another sector is already a better prepared pilot.

### Step 1 — inspect the sector

Inventory all referenced assets:

- terrain
- roads
- floors
- walls
- doors
- windows
- roofs
- fences
- vegetation
- furniture
- machinery
- street objects
- signage
- debris
- special structures

### Step 2 — classify assets

Classify each asset as:

- structural
- ground
- prop
- decorative
- character/animation

Different categories have different constraints.

### Step 3 — create genuine VHD2 variants

Keep original logical geometry and anchors.

Create native `VHD2` variants with genuinely higher detail.

Keep legacy 1x assets as fallback.

Do not prioritize VHD4 until the 2x workflow is stable, but keep the asset pipeline architecture-compatible with 4x.

### Step 4 — preserve anchors and structure semantics

For every converted asset verify:

- base position
- tile origin
- object bottom point
- door pivot
- roof alignment
- wall intersections
- window position
- neighbouring seams
- JSD/depth correspondence

Do not visually re-center sprites if that moves their gameplay anchor.

### Step 5 — validate in engine/editor

Check:

- sector loads
- tile seams
- road seams
- wall intersections
- roofs
- doors opening/closing
- windows
- destroyed structures
- multi-tile props
- shadows
- merc depth
- corpses
- items
- roof switching
- occlusion bubble
- mouse/grid mapping
- interactive doors/windows
- scrolling
- screen edges
- editor placement

### Step 6 — compare 1x vs 2x

Each batch should prove both:

1. same gameplay geometry;
2. major visual improvement.

### Step 7 — commit incrementally

Use small, reversible commits, for example:

- `VHD2 San Mona C5 — road/floor pilot`
- `VHD2 San Mona C5 — masonry/walls`
- `VHD2 San Mona C5 — doors/windows`
- `VHD2 San Mona C5 — roofs`
- `VHD2 San Mona C5 — props`
- `VHD2 San Mona C5 — decals/clutter`

Graphics deployment should be incremental by default. Keep a full/force redeploy path only for recovery or structural changes.

## Occlusion / wall behavior

The project also contains Fallout-style wall occlusion/cutaway behavior.

HD walls must be tested with semi-transparent/occluded states, especially:

- corners
- door frames
- windows
- adjoining wall segments
- roofs
- mercs behind walls

Preserve structural readability.

## Do not overwrite unrelated work

This repository contains parallel work on AI, NCTH, UI, sounds, tactical systems, item graphics, maps, and gameplay.

Inspect current file state/history before modifying shared files.

Do not replace recent unrelated work because an upstream file looks cleaner.

## Build/startup validation status

As of VHD head `8220b81f2f9624fd98f4296862a8d4636b4e7599`:

- hosted Win32 full compile/link: passed
- targeted SGP/TileEngine/Tactical/Editor compile: passed
- root compile-only check: passed
- true VS2013/v120 Release Win32 build: passed
- isolated VHD 2x startup smoke: passed
- runtime-log upload/cleanup: passed
- final end-to-end VHD workflow: **green**

The automated engineering gate is therefore complete.

Manual visual QA still remains part of map/asset production and should be performed continuously as sectors are converted.

## Definition of success

A successful VHD sector should feel like:

> **the exact same Jagged Alliance sector mechanically, but as though its world artwork had originally been authored for a much higher-resolution version of the game.**

Same battle. Same buildings. Same doors. Same tactical geometry. Same map logic.

**Far better world.**

Begin the VHD map-production phase now.
