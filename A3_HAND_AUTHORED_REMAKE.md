# A3 Remake — Hand-authored iterative art plan

## Intent

A3 is treated as a real place first and a tile grid second.

Visual identity: a poor but functioning tropical agricultural compound on the outskirts of Oronegro. It should feel inhabited, worked and improvised rather than procedurally decorated.

The existing authored map provides the tactical skeleton. New art is composed deliberately around that skeleton and validated in the actual Vengeance MapEditor after every pass.

## Sector composition

### 1. Main farmhouse / owner house
- white/cream sun-faded stucco
- clay/red tile roof
- shaded veranda / porch
- water barrel/tank, washing, tools, sacks and small domestic clutter
- one practical vehicle parking area
- worn footpaths connecting the house to yard and fields

### 2. Working barn / machinery shed
- larger, cheaper utility structure
- corrugated metal / patched roof
- tractor or truck working apron
- pallets, drums, spare timber, tyres, fuel and repair clutter
- visibly dirtier ground than the domestic house

### 3. Cattle paddock
- open readable ground, not crop wallpaper
- trough, fence repairs, hay/straw, hoof-worn mud
- cows concentrated here
- one clear gate and a practical route to the yard

### 4. Cultivated fields
- 3–5 coherent fields with different crop states, not random individual sprites
- planted rows follow a consistent direction per field
- mature crop, young crop, harvested/stubble and trampled edge variants
- headlands/turning strips at row ends
- occasional missing/broken rows
- access lanes large enough to read at 1920x1080

### 5. Irrigation / drainage
- channels follow terrain and field boundaries
- wet/dark soil around them
- improvised plank crossings
- small pump/tank or junction near the working yard

### 6. Perimeter / tropical vegetation
- dense vegetation concentrated at unused margins
- palms and scrub frame the sector instead of being sprinkled uniformly
- field boundaries remain visually clear
- roadside/entry area has sign, dust, wheel ruts and damaged fence

## Composition rule

No broad hash/random placement.

Every major visual block has:
1. a named purpose;
2. an authored footprint;
3. a deliberate anchor;
4. a coherent sprite family;
5. a screenshot review before the next block is added.

Small stochastic variation is allowed only *inside* an authored block (for example choosing one of three dirt marks), never for deciding the sector layout.

## Iteration loop

1. Render current A3 in Vengeance MapEditor.
2. Inspect the actual screenshot.
3. Record the 5–10 largest visual defects.
4. Change only enough art/layout to address those defects.
5. Render again.
6. Compare against previous screenshot.
7. Keep improvements; revert regressions.
8. Repeat until the sector reads correctly at both overview scale and normal tactical zoom.

## Tile production method

For each hero composition (house frontage, barn apron, crop field strip, cattle station, irrigation junction):

1. Draw the complete composition first on an isometric canvas.
2. Match JA2 perspective and lighting.
3. Slice the composition into reusable frame-sized pieces.
4. Preserve transparent margins and correct offsets.
5. Encode frames into B1TC.
6. Place only the intended frames at known map anchors.
7. Verify the assembled composition in-engine.

Buildings are treated the same way: design the whole facade/roof as one coherent object, then split it into compatible tile frames rather than painting unrelated wall pieces independently.

## V1 rejection criteria

The current V1 render is rejected because:
- custom overlays resolve as tall pale slab/column sprites;
- red/blue debug-looking fragments dominate the ground;
- field treatment reads as noise rather than agriculture;
- no hierarchy between domestic house, work yard, paddock and fields;
- procedural density destroys tactical readability.

V2 starts from a clean authored-map baseline, with custom art reintroduced only after visual inspection of each family.


## 2026-09-15 production reset: graphics-only

The old runtime dressing/baking route is retired from the active map stream.
A3 must not gain objects, cows, crops, roof nodes or other map content during
LoadWorld, and MAPSHOT must not save a transformed A3_REMASTERED.dat.

The authoritative geometry is the authored A3.dat.  The visual profile may:
- route sector-scoped replacement art;
- apply renderer-side visual treatment;
- preserve the original tile/frame identity contract.

It may not mutate world nodes or tactical placement.

### Structural pilot gate

`Tools/A3/generate_a3_structural_pilot.py` now covers the major A3 surface
families (walls, floors and roofs) using the old STI only for frame count,
dimensions, offsets and alpha footprint. Legacy RGB is not sampled.

The structural pilot is intentionally **quarantined / not routed**. Promotion
requires, in order:

1. B1TC header/frame-contract validation.
2. Contact-sheet review for seams, perspective, alpha holes and family coherence.
3. Real MapEditor / MAPSHOT review at native 1920x1080.
4. Side-by-side tactical-readability review against original A3.
5. Only then, sector-scoped routing. No global tileset replacement.

If a candidate looks like a recolour, texture filter or generic HD treatment,
reject it rather than trying to polish it in place.
