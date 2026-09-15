# 2026 Engine Stream — Cover / LOS / Ballistics: Surface Penetration

Status: **Research + staged design**
Branch: `engine/cover-penetration-2026`
Base: `launch/2026-09-12`

## Scope and guardrails

This stream owns **surface penetration, cover geometry, LOS-facing cover data, material resistance, ammo-vs-structure interaction, and future player-facing protection information**.

**Do not modify TacticalAI/** in this stream.

AI integration is limited to a later read-only handoff suggestion: expose a stable penetration/protection query that the AI stream may choose to consume. No AI behaviour, weights, pathing, target selection, memory, CQB doctrine, suppression doctrine, or tactical decision logic belongs here.

## Current engine foundation

Vengeance already has the important JA2/1.13 physical ingredients:

- actual projectile trajectory
- structure intersection
- structure material type
- structure density
- material armour/resistance table
- projectile impact and accumulated impact reduction
- ammo-specific `structureImpactReductionMultiplier` / `structureImpactReductionDivisor`
- wall orientation and structure geometry
- multiple structures encountered sequentially
- range-sensitive structure resistance
- light-vegetation special handling
- 5 x 5 horizontal x 4 vertical structure profile occupancy

The current basic model is sound:

```
trajectory
 -> structure hit
 -> material resistance
 -> density / occupied geometry
 -> ammo-vs-structure modifier
 -> accumulated impact reduction
 -> remaining projectile impact
```

The problem is not that penetration is missing. The problem is that **material classes are coarse and physical thickness is not explicit**.

## External-source conclusions used for calibration

### Thickness is real and important

Ballistic resistance rises with target thickness. This is directly observed in controlled concrete penetration experiments. We should therefore not treat a thin slab and a thick wall as equivalent merely because both are "concrete".

### Field fortifications depend on depth and construction

Military marksmanship guidance treats sandbags, packed earth, and logs as effective cover only when sufficient depth/packing is present. A single material label such as `SAND = 40` cannot represent both a loose/thin barrier and a properly built fighting position.

### Projectile construction matters

Real ballistic standards define test threats by specific projectile/ammunition types and velocities, not calibre name alone. This supports retaining and expanding the existing per-ammo structure penetration modifiers.

## Design decision

### Keep

- JA2 projectile tracing
- accumulated impact reduction / residual projectile energy
- material resistance concept
- density/occupancy concept
- ammo-specific structure penetration modifiers
- current map structure geometry

### Improve

1. recalibrate obviously over-protective light barriers
2. split a few overly broad material families where needed
3. introduce a **provisional effective-thickness concept**
4. resolve an **effective thickness for every projectile/structure encounter**, even when no explicit thickness metadata exists
5. use actual projectile path through occupied structure geometry wherever possible
6. treat explicit thickness metadata as an override/refinement, not a prerequisite
7. preserve residual energy after penetration

## Provisional thickness model — MAYBE / experimental

Do **not** pretend the old maps contain exact centimetres.

Add an optional semantic property:

```
THICKNESS_UNKNOWN
THICKNESS_VERY_THIN
THICKNESS_THIN
THICKNESS_STANDARD
THICKNESS_THICK
THICKNESS_MASSIVE
```

### Revised meaning of THICKNESS_UNKNOWN

`THICKNESS_UNKNOWN` does **not** mean "ignore thickness" and does **not** immediately collapse to a fixed 1.0 multiplier.

It means:

> **No explicit construction-thickness metadata is available, so resolve thickness from the other information the engine already has.**

Every projectile/structure encounter should therefore produce a **resolved effective thickness**, whether or not the structure was manually tagged.

Resolution hierarchy:

```
1. explicit thickness metadata, if trustworthy
2. measured projectile path through occupied structure geometry
3. structure archetype / flags / orientation
4. material-family prior
5. conservative legacy-equivalent fallback only if all inference fails
```

The fallback exists solely as a safety net. It is not the normal behaviour for old maps.

Important distinction:

```
declared_thickness_class   = optional metadata
resolved_effective_thickness = calculated for every actual shot
```

Thus old Vengeance maps can benefit from the new system without requiring every structure to be manually retagged first.

Candidate conceptual use:

```
effective_resistance =
    material_resistance
  * resolved_effective_thickness
  * ammo_material_factor
  * range_factor
```

The exact multipliers are **not frozen yet**. They must be calibrated in a controlled test sector.

### How thickness is resolved

The existing 5 x 5 x 4 structure profile can tell us how much occupied geometry the projectile crosses and therefore capture meaningful path-depth and angle effects.

That geometric result is the primary evidence when explicit metadata is absent.

However, geometry alone cannot always tell us whether a visually thin steel object represents sheet metal or a heavy plate. Therefore the resolver can refine the geometric estimate using structure semantics.

Use:

- **measured path depth** = occupied distance actually crossed by the projectile
- **structure semantics** = wall, door, fence, furniture, vehicle-like object, tree, etc. where available
- **material family** = a prior/range, not a substitute for geometry
- **declared thickness class** = optional semantic correction when the map/object definition genuinely knows more than geometry
- **resolved effective thickness** = final per-shot value used by penetration

Do **not** infer thickness from sprite artwork alone.

Do **not** use density as a direct synonym for thickness. Density/porosity can affect whether solid material is encountered; thickness describes how much material is crossed once it is encountered.

### Proposed resolver

Conceptually:

```
ResolveEffectiveThickness(projectile, structure):
    path = MeasureOccupiedPath(projectile, structure)

    if structure has trustworthy explicit thickness:
        semantic = ExplicitThickness(structure)
    else:
        semantic = InferThicknessFromArchetypeAndMaterial(structure)

    return Combine(path, semantic, confidence)
```

The combination should be conservative: geometry remains the anchor, while semantic thickness prevents a thin sheet-metal object and a heavy steel door from becoming identical merely because both occupy a similar coarse structure profile.

For `THICKNESS_UNKNOWN`, the resolver should still return a usable value plus a confidence level, for example:

```
resolvedThickness = 0.72
confidence = MEDIUM
source = GEOMETRY + ARCHETYPE + MATERIAL_PRIOR
```

Exact scale and multipliers remain experimental and must be calibrated against the penetration test matrix.

## Material-family audit

### Likely too protective today

These should be tested for downward recalibration before any global weapon buff:

- upholstered furniture
- furniture wood
- plywood / thin interior partitions
- ordinary wooden doors
- thin sheet metal
- vehicle body sheet metal
- generic porcelain / ceramic fixtures

### Generally credible ordering, but thickness-sensitive

- live wood / tree trunks
- stone masonry
- brick
- ordinary concrete
- reinforced concrete
- rock
- heavy steel

### Likely under-modelled as defensive construction

- packed sand
- sandbags
- packed earth
- layered timber/log cover
- reinforced defensive positions

The issue here is not necessarily the base material resistance; it is that defensive **depth/layers** are not represented well.

## Proposed material families

Keep the list compact. Avoid turning the engine into a materials laboratory.

### Wood
- thin plywood / panel
- furniture wood
- structural timber / wooden wall
- live wood / tree

### Metal
- thin sheet metal
- vehicle body / medium sheet
- structural steel / heavy door
- heavy / armour-like steel

### Mineral
- brick
- stone masonry
- concrete
- reinforced concrete
- rock

### Soft / granular
- cloth
- upholstery
- vegetation
- sand / earth
- sandbag construction

## Distance and projectile energy

Distance **must** matter to penetration.

Current Vengeance already approximates this, but in an indirect way:

- `BulletImpactReducedByRange()` is currently disabled/commented out.
- `StructureResistanceIncreasedByRange()` increases effective barrier resistance with distance relative to the weapon's nominal range.
- current constant: `PERCENT_BULLET_SLOWED_BY_RANGE = 25`.

This produces distance-sensitive penetration, but conceptually it makes the **wall stronger** instead of making the **projectile weaker**.

### 2026 design target

Use one coherent concept:

```
remaining_projectile_energy(distance, previous_barriers)
```

Then barrier interaction becomes:

```
energy_before_barrier
 - resistance(material, thickness, geometry, ammo/material interaction)
 = energy_after_barrier
```

Distance loss should be calculated **before** the barrier interaction, and the projectile should keep the reduced residual energy afterward.

This matters especially for:

- a barrier close to the shooter vs the same barrier near maximum range
- multiple barriers in sequence
- a target standing immediately behind a penetrated wall
- long-range AP vs FMJ comparison
- short-barrel vs full-length weapon variants where projectile performance/range differs

### Migration safety

Do not simply enable the old `BulletImpactReducedByRange()` formula. It was disabled for historical gameplay/AI reasons.

Instead:

1. capture current damage/penetration baselines
2. identify how damage falloff and weapon impact are currently calculated elsewhere
3. define a single energy-decay function
4. tune it so normal in-range shots do not lose excessive lethality
5. remove/neutralise duplicate distance penalties once the new model is authoritative
6. keep CTGT prediction and actual bullet resolution mathematically aligned

The key rule is: **distance should reduce projectile capability once, not be double-counted through both energy loss and inflated structure resistance.**

## Ammo interaction

Retain the existing externalised ammo structure modifiers.

Do not assume "AP" is a universal scalar against every barrier.

Longer-term target:

```
ammo class x material family
```

Examples:
- AP: strong advantage vs metal, moderate advantage vs wood, smaller/variable advantage vs deep masonry
- FMJ/ball: balanced baseline
- HP/JHP: poor hard-barrier performance
- subsonic: reduced barrier performance due to lower projectile energy
- heavy rifle ball may still outperform a smaller AP pistol round because base projectile impact remains part of the calculation

This should be implemented through data where possible, not hard-coded special cases.

## Precomputed structure penetration table

Do not recalculate static structure geometry from scratch for every bullet.

At map/content load time, build or load a **penetration profile table for every unique structure definition/state** and let placed map tiles reference it.

Preferred key is the **structure definition/state**, not raw map grid number, because hundreds of placed tiles can share the same DB structure. This avoids duplicating identical data.

Candidate record:

```
StructurePenetrationProfile
{
    structure_id
    state_id / partner state
    material
    density
    flags / archetype
    orientation
    occupied_shape[5][5][4]
    geometric_depth_by_direction
    inferred_thickness_class
    inference_confidence
    static_resistance_components
}
```

### What should be precomputed

For every unique static structure/state:

- material family
- current material resistance
- density / porosity
- 5 x 5 x 4 occupied geometry
- wall/object orientation
- archetype/flags
- multi-tile footprint
- thickness estimate from geometry
- representative path depths by direction/angle bins
- inferred construction class where explicit metadata is absent
- confidence/source of the inference

### What remains runtime

Runtime calculation should only combine the cached structure profile with shot-specific variables:

- exact projectile direction / sub-cell path
- impact location and height
- projectile remaining energy
- distance already travelled
- weapon/ammo
- ammo-vs-material modifier
- current structure state if changed/damaged/opened
- previous barriers already crossed

Doors/openables/destruction partners and any structure whose geometry/state changes must resolve to the appropriate cached state or be recalculated when state changes.

### Optional generated audit table

Generate a developer-facing table (CSV/debug dump) containing one row per unique structure state so we can inspect and tune the whole game systematically rather than guessing object-by-object.

Suggested columns:

```
structure_id
source tileset/file
state
archetype
material
density
flags
orientation
occupied_volume
min_path_depth
median_path_depth
max_path_depth
inferred_thickness
inference_source
confidence
legacy_resistance
```

This table can be regenerated whenever structure data changes. The runtime should use compact cached data, not repeatedly parse a CSV.

## Geometry plan

For each projectile/structure encounter:

1. retain current structure intersection test
2. determine material
3. retrieve the precomputed structure penetration profile/state
4. measure/refine the exact occupied path length for this shot using the cached geometry
5. use wall/object orientation already available
6. resolve effective thickness:
   - use explicit class if available
   - otherwise infer from measured geometry + archetype + material prior
   - use legacy-equivalent fallback only if inference is impossible
7. apply distance-dependent projectile energy state
8. apply ammo-specific structure modifier
9. subtract barrier energy loss from the projectile
10. allow projectile to continue with residual impact if > 0
11. accumulate multiple barriers naturally

This should make oblique paths and multiple layers matter without inventing a separate arbitrary "angle bonus".

## Conservative implementation phases

### Phase 0 — baseline capture
- no gameplay changes
- record current penetration outcomes for representative barriers/ammo/distances
- generate the first structure-profile audit table
- create reproducible test sector / harness

### Phase 1 — material audit
- identify which map structures use each material class
- find implausible assignments
- fix clearly wrong assignments before touching formulas

### Phase 2 — conservative material rebalance
- reduce protection of thin/light everyday objects
- do not globally buff all guns
- keep concrete/rock/heavy cover stable until measured

### Phase 2.5 — cached structure-profile table
- precompute one profile per unique structure definition/state
- map placed tiles/objects to that profile
- dump an auditable CSV/debug table for tuning
- recalculate only dynamic states as required

### Phase 3 — effective-thickness resolver
- add `THICKNESS_UNKNOWN` as "not explicitly tagged", **not** as "no thickness"
- calculate a resolved thickness for every encounter
- use geometry first, then archetype/material priors
- return inference source/confidence for diagnostics
- preserve a legacy-equivalent fallback only for unresolved edge cases
- no inference from sprite artwork alone

### Phase 4 — geometric path-depth prototype + explicit metadata pilot
- derive path depth from existing 5 x 5 x 4 occupied structure profile
- compare perpendicular vs oblique shots
- verify no duplicate resistance is charged for the same physical wall crossing
- explicitly tag only a small pilot set where semantics clearly add information
- compare explicit vs inferred thickness outcomes

### Phase 4.5 — projectile energy / distance audit
- compare current structure-resistance-with-range approximation against actual projectile-energy decay
- ensure damage and penetration use compatible energy assumptions
- avoid double-counting distance
- align actual bullet resolution and CTGT prediction

### Phase 5 — ammo/material calibration
- use externalised ammo values
- tune representative FMJ, HP, AP, SAP/enhanced penetrator, subsonic and shotgun classes
- keep calibre/base impact relevant

### Phase 6 — player-facing protection data
Expose a stable engine query that can later support:
- ballistic protection
- penetration risk
- cover UI
- shot preview

No AI changes.

## Test matrix

Representative barriers:
- bush / vegetation
- upholstered furniture
- cabinet
- plywood partition
- wooden door
- timber wall
- tree trunk
- sheet metal
- car body
- heavy metal door
- brick
- stone masonry
- concrete
- reinforced concrete
- sandbags

Representative shots:
- pistol FMJ
- pistol HP
- rifle FMJ
- rifle AP
- full-power rifle FMJ
- full-power rifle AP
- shotgun buckshot
- shotgun slug
- subsonic where available

Angles:
- perpendicular
- moderate oblique
- strong oblique

Distances:
- short
- medium
- long

Record:
- stopped / penetrated
- residual impact
- downstream damage proxy
- number of structures crossed
- measured path depth
- declared thickness class (if any)
- resolved effective thickness
- thickness inference source/confidence
- material
- ammo type

## AI stream handoff — suggestions only

**Do not implement in this branch.**

Once the engine query is stable, the AI stream may optionally consume read-only outputs such as:

```
BallisticProtection(attacker, targetGrid, stance, weapon, ammo)
PenetrationRisk(attacker, targetGrid, weapon, ammo)
```

Suggested future AI interpretation:
- distinguish concealment from ballistic protection
- understand that foliage can hide without stopping rounds
- understand that thin wood may not protect from rifle fire
- prefer genuinely protective cover when under rifle/AP threat
- reason about stance and threat direction

Again: **this branch does not modify AI behaviour**.

## Acceptance criteria

Do not merge penetration changes merely because they "feel realistic".

A change is mergeable only when:
- `THICKNESS_UNKNOWN` still resolves from other variables rather than disabling thickness
- legacy fallback remains safe for genuinely unresolved edge cases
- representative barriers behave intuitively
- rifles do not turn the map into paper
- ordinary furniture/doors are not unrealistically tanky
- serious masonry/concrete/sandbag positions remain meaningful
- ammo choice changes barrier performance in a believable way
- residual projectile energy remains consistent
- no TacticalAI source is modified
