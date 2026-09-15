# Map Factory Engine Constraints and Best Practices

## System we are actually designing for

Vengeance Reloaded is a legacy JA2 isometric tactical engine with a modernized renderer layered over the original map/tile model.

Hard constraints:
- fixed isometric grid and map tile indices;
- authored .dat sector files;
- JSD structure metadata controls collision/LOS/cover/destruction;
- logical frame/region indices must remain stable;
- normal tactical presentation is still a 16-bit RGB565 framebuffer;
- map art may be legacy indexed STI or Vengeance true-colour B1TC/JPC/PNG;
- true-colour source art is dithered into RGB565 at presentation;
- walls/roofs/structures may use true colour only when frame geometry and JSD compatibility are preserved.

## What the engine is good at

Use aggressively:
- tile-family palette/material grading;
- 32-bit RGBA pixel treatment on true-colour VOBJECT frames;
- B1TC siblings that preserve source STI identity;
- reusable non-structural visual modules;
- sector archetype profiles;
- native SaveWorld persistence;
- deterministic screenshot QA;
- existing authored map geometry and tactical composition.

## What the engine is bad at

Do not build the production method around:
- thousands of tiny decorative placements;
- subtle changes invisible at tactical zoom;
- modern free-form procedural geometry;
- large arbitrary sprites that visually overlap unrelated tactical structures;
- changing JSD-backed walls/roofs without preserving frame order/dimensions/offsets;
- repeatedly rebuilding an entire sector just to alter colour/material mood;
- one universal tropical look for every Arulco biome.

## Visual hierarchy

A successful remaster should improve the map in this order:

1. Macro readability
   - vegetation vs soil vs roads vs buildings vs roofs clearly separate;
   - regional colour language immediately visible.

2. Material quality
   - worn plaster, brick, concrete, corrugated metal, rust, timber, wet/dry soil;
   - full available colour range where the renderer supports it.

3. Mid-scale composition
   - believable farm yards, roadside clutter, checkpoints, work yards, courtyards;
   - several large readable clusters rather than tiny scatter.

4. Micro-detail
   - debris, weeds, crates, stains and small props only after the first three levels work.

## Safe modification rule

Default preserve:
- grid geometry;
- JSDs;
- collision;
- LOS/cover;
- doors/locks;
- exits;
- NPC placements;
- quest triggers;
- scripted structures.

Visual-only changes are preferred unless a sector is explicitly selected for tactical redesign.

## Colour path

Legacy 8-bit surfaces:
- modify palette/material grading at tile-surface load;
- rebuild normal shade tables afterward.

True-colour 32-bit surfaces:
- grade raw RGBA VOBJECT frame pixels directly;
- never alter alpha unless the specific art pass intentionally changes visual holes;
- preserve frame dimensions, offsets and object counts.

Detailed custom art:
- prefer B1TC siblings of existing STI families;
- preserve original STI filename as map/JSD identity;
- frame count, width, height and X/Y offsets must remain compatible;
- remove the optional sibling and the game must fall back cleanly to stock STI.

## Map Factory methodology

Prototype on representative sectors, not one sector indefinitely.

A method is evaluated using:
- visible tactical improvement;
- reload persistence;
- lore consistency;
- engine safety;
- throughput.

Switch method when a failure is structural.
Optimize once when a method is viable but under target.
Freeze once good enough and move to production.

## Quality floor

A sector does not pass because:
- it compiled;
- SaveWorld succeeded;
- hashes differ;
- a few pixels changed;
- extra debris exists somewhere.

It passes when a player can immediately see that it is a richer, more coherent version of the same Arulco location at normal tactical zoom without breaking gameplay.

## Production architecture

Layer 1: archetype macro material profile.
Layer 2: sector/tileset-specific true-colour weathering or replacement art where useful.
Layer 3: several mid-scale authored composition kits.
Layer 4: sector-specific hero details only for important locations.

This hierarchy is the default unless evidence shows a better method.
