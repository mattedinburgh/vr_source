# VR Cinematic Death and Dismemberment Pack

Active branch: `install/all-2026-09-12`

The fatal gunshot selector in `Tactical/Soldier Control.cpp` now exposes 35 in-game variants, numbered 0-34. Motion remains the default; dismemberment is damage-gated and deliberately rare, with a modest cinematic bias.

## Variant map

| ID | Body reaction | Gore package |
|---:|---|---|
| 0 | Forward fold | forward fall overlay + medium spray + trail |
| 1 | One-tile backward collapse | fall-back overlay + entry backspatter |
| 2 | Two-tile hard flyback | fall-back overlay + chunks + long trail |
| 3 | Left-side collapse | left fall overlay |
| 4 | Right-side collapse | right fall overlay |
| 5 | JFK head death | head gib + heavy exit spray + trail |
| 6 | JFK head burst | head gib + chunks + entry-side spray |
| 7 | Head hit, left spin/fall | head gib + left-fall overlay |
| 8 | Head hit, right spin/fall | head gib + right-fall overlay |
| 9 | Head hit, backward launch | head gib + chunks + flyback |
| 10 | Left arm loss | arm gib + left collapse |
| 11 | Right arm loss | arm gib + right collapse |
| 12 | Arm loss + hard flyback | arm gib + chunks + trail |
| 13 | Arm loss + forward fold | arm gib + forward-fall overlay |
| 14 | Shoulder/arm backward stumble | arm gib + heavy spray + fallback |
| 15 | Leg sever buckle | leg gib + crumple + trail |
| 16 | Leg sever face-plant | leg gib + forward fall |
| 17 | Leg sever left collapse | leg gib + crumple |
| 18 | Leg sever right collapse | leg gib + crumple |
| 19 | Leg destruction backward | leg gib + chunks + fallback |
| 20 | Torso chunk backward | torso gib + heavy spray + trail |
| 21 | Torso destruction flyback | torso gib + chunks + heavy spray |
| 22 | Torso destruction forward | torso gib + heavy spray + forward fall |
| 23 | Torso destruction left | torso gib + chunks + left fall |
| 24 | Torso destruction right | torso gib + chunks + right fall |
| 25 | Full-body dismemberment | chunks + torso gib + heavy spray + BODYEXPLODING |
| 26 | Mixed-limb crumple | crumple + arm gib + leg gib |
| 27 | Triple-layer backward blast | torso + chunks + heavy spray + entry spray |
| 28 | Huge mist-cone collapse | heavy + medium + small spray + long trail |
| 29 | Maximum-gore lateral death | chunks + head + arm + leg + randomized side fall |

## Selection rules

- Base selection: `Random(30)`.
- Head hits: 80% bias toward variants 5-9.
- Leg hits: 75% bias toward variants 15-19.
- Torso hits: 70% bias toward variants 20-24.
- Crouched/prone deaths keep the 30-way gore selection but finish through stance-safe JA2 death states.
- Water deaths are intentionally excluded from this system.
- Human merc body types only for this pass.

## Diagnostics

Every fatal selection writes a debug line in the form:

```
VR_FATAL variant=<0-29> soldier=<id> hitloc=<id> damage=<hp> height=<stance> incomingDir=<dir>
```

Use this ID when reporting a bad-looking death or crash.

## Assets

The system uses the bundled assets already deployed by `DEPLOY_GORE.ps1`:

- `VR_FATAL_FALL_FORWARD.STI`
- `VR_FATAL_FALL_BACK.STI`
- `VR_FATAL_FALL_LEFT.STI`
- `VR_FATAL_FALL_RIGHT.STI`
- `VR_FATAL_CRUMPLE.STI`
- `VR_FATAL_HEAD_GIB.STI`
- `VR_FATAL_ARM_GIB.STI`
- `VR_FATAL_LEG_GIB.STI`
- `VR_FATAL_TORSO_GIB.STI`
- `VR_FATAL_CHUNKS.STI`
- `VR_GORE_SPRAY_SMALL.STI`
- `VR_GORE_SPRAY_MEDIUM.STI`
- `VR_GORE_SPRAY_HEAVY.STI`

## Important limitation

These are 30 distinct in-game death sequences assembled from the real JA2 merc motion states plus direction, displacement and gore layers. They are not 30 newly hand-drawn full-body sprite-sheet animation sets. The current Vengeance game data does not contain 30 unused human merc death sprite sheets to expose directly.

## Pseudo-ragdoll and sprinkler pass

Current extreme-test behaviour also includes:

- moving victims preserve their travel direction as momentum for a large share of fatal reactions;
- momentum-biased deaths prefer face-plants, side spins, hard flybacks and mist-cone collapses;
- all displacement continues through JA2's existing blocked-tile checks;
- every fatal gunshot receives an extreme gore envelope before its individual variant: dense local blood, a long thinning trail, tissue chunks and a hit-location-specific head/leg/torso layer;
- blood projection now uses many small delayed droplets with slight directional jitter ("sprinkler") rather than one broad heavy fan;
- heavy spray layers in the fatal pack were replaced by medium/small layered sprays;
- gore animation tiles render two shade levels darker than default and ground blood decals three shade levels darker, producing a deeper/darker red without changing terrain or character palettes.

This remains an intentionally exaggerated test configuration. Frequency, displacement, gore volume and damage/weapon scaling are deferred until the visual system is proven stable.

## Added limb-loss variants

| ID | Reaction |
|---:|---|
| 30 | Left forearm/hand-sized limb fragment thrown clear, left collapse |
| 31 | Right forearm/hand-sized limb fragment thrown clear, right collapse |
| 32 | Arm/forearm detaches forward while the body recoils backward |
| 33 | Detached leg thrown laterally with immediate buckle |
| 34 | Exceptionally rare mixed-limb catastrophic breakup |

Dismemberment probability now scales strongly with fatal hit damage. Head and leg hits receive a small location bonus. A selected dismemberment event is no longer overwritten by the moving-victim momentum selector.

The normal non-fatal hit system does not use these limb-loss variants.
