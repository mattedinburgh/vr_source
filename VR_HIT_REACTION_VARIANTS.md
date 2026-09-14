# VR Hit Reaction Variants

This pack now provides **80 non-fatal gunshot reaction IDs** plus the separate fatal-reaction system.

These are in-game reaction sequences assembled from existing JA2 living-safe animation states, facing changes, checked fallback/flyback movement, momentum and fall states. They are not 80 newly hand-drawn sprite sheets.

## Severity model

Reaction selection is no longer in demo mode. It now uses:

- HP damage from the hit;
- breath/stamina lost on that hit (`sBreathLoss / 100`);
- remaining stamina after the hit;
- hit location;
- whether the victim was running;
- a small random cinematic term.

Low remaining stamina matters independently of HP damage. An exhausted soldier therefore has a substantially higher chance to stagger, buckle or fall from the same bullet impact.

Critical reactions receive a deliberate small cinematic probability boost above the strict severity threshold. This affects body motion only; it does **not** increase ordinary hit blood volume.

## Probability behaviour

- Fresh + light wound: overwhelmingly upright flinches, shoulder reactions and short staggers.
- Moderate wound or moderate stamina loss: wider rotational reactions and balance checks become common.
- Low stamina: strong reactions rise sharply even when HP damage is not extreme.
- Heavy wound + low stamina: falls, collapses and checked flybacks become plausible.
- Running: dedicated momentum reactions preserve travel direction where possible.
- Head hits: bias toward upper-body rotational reactions and a modest critical boost.
- Leg hits: bias toward balance-loss reactions and a modest critical boost.
- Crouched/prone targets use stance-safe severity mapping instead of being forced to fall on every hit.

## Reaction families

| IDs | Family |
|---:|---|
| 0-11 | Original light/upright flinches and short staggers |
| 12-19 | Soft collapses / balance loss |
| 20-24 | Original running spills |
| 25-29 | Original heavy checked reactions |
| 30-39 | New nuanced torso/shoulder rotations and directional flinches |
| 40-49 | New balance checks and short upright staggers |
| 50-59 | New stamina-sensitive buckles and recoverable falls |
| 60-64 | New running momentum flinches/stumbles |
| 65-69 | New running momentum spills/falls |
| 70-79 | New cinematic critical reactions: hard collapse/flyback/fall |

The extra 50 variants are intentionally weighted toward **normal hit behaviour**, not spectacular deaths.

## Diagnostics

Every selected hit reaction logs:

`VR_HIT variant=<id> soldier=<id> damage=<hp> breathloss=<breath points> stamina=<remaining> severity=<score> criticalchance=<pct> ...`

This makes it possible to compare the animation outcome against actual damage and stamina state during black-box review.

## Current totals

- 80 non-fatal gunshot reaction IDs
- 35 fatal cinematic variants
- stance, direction, momentum, weapon-holding and hit-location permutations on top of those IDs
