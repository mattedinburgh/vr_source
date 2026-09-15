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

## Geometry plan

For each projectile/structure encounter:

1. retain current structure intersection test
2. determine material
3. measure/estimate occupied path length through the structure profile
4. use wall/object orientation already available
5. resolve effective thickness:
   - use explicit class if available
   - otherwise infer from measured geometry + archetype + material prior
   - use legacy-equivalent fallback only if inference is impossible
6. apply ammo-specific structure modifier
7. subtract energy from projectile
8. allow projectile to continue with residual impact if > 0
9. accumulate multiple barriers naturally

This should make oblique paths and multiple layers matter without inventing a separate arbitrary "angle bonus".

## Conservative implementation phases

### Phase 0 — baseline capture
- no gameplay changes
- record current penetration outcomes for representative barriers/ammo
- create reproducible test sector / harness

### Phase 1 — material audit
- identify which map structures use each material class
- find implausible assignments
- fix clearly wrong assignments before touching formulas

### Phase 2 — conservative material rebalance
- reduce protection of thin/light everyday objects
- do not globally buff all guns
- keep concrete/rock/heavy cover stable until measured

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
