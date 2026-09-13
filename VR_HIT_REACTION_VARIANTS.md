# VR Hit Reaction Variants

This pack adds 30 non-fatal gunshot reaction sequences on top of the 30 fatal cinematic variants.

These are in-game reaction sequences built from existing JA2 living-safe animation states, direction changes, momentum, checked fallback/flyback movement, and fall states. They are not 30 newly hand-drawn sprite sheets.

## Selection logic

- Light damage (1-7): mostly upright flinches and short staggers.
- Medium damage (8-17): upright reactions dominate; soft collapses are possible.
- Heavy damage (18+): normal falls and hard checked flybacks become common.
- Running: reaction selection preserves movement momentum.
- Leg hits: modest bias toward balance loss / soft collapse.
- Head hits: modest bias toward rotational flinches.
- Torso hits: full mixed pool.
- All checked displacement continues through JA2's existing fallback/flyback collision logic.

Every selected variant is logged as:

`VR_HIT variant=<id> soldier=<id> damage=<n> hitloc=<n> running=<0/1> incomingDir=<n> momentumDir=<n>`

## 30 reaction IDs

| ID | Reaction |
|---:|---|
| 0 | Compact generic torso flinch |
| 1 | Weapon-bearing shoulder recoil |
| 2 | Sharper full-body burst-style flinch |
| 3 | Soft left shoulder twist |
| 4 | Soft right shoulder twist |
| 5 | Stronger left rotational flinch |
| 6 | Stronger right rotational flinch |
| 7 | Brief turn-away recoil |
| 8 | Short straight backward stagger |
| 9 | Oblique backward stagger left |
| 10 | Oblique backward stagger right |
| 11 | Straight weapon-hit recoil |
| 12 | Gentle forward fold in current facing |
| 13 | Gentle forward fall aligned to impact |
| 14 | Soft diagonal-left collapse |
| 15 | Soft diagonal-right collapse |
| 16 | Pronounced left-side loss of balance |
| 17 | Pronounced right-side loss of balance |
| 18 | Restrained backward collapse |
| 19 | Angled backward collapse |
| 20 | Running trip preserving momentum |
| 21 | Running fall veering left |
| 22 | Running fall veering right |
| 23 | Running sideways spill left |
| 24 | Running sideways spill right |
| 25 | Checked hard flyback |
| 26 | Checked oblique flyback left |
| 27 | Checked oblique flyback right |
| 28 | Heavy hit that folds instead of launching |
| 29 | Limp-looking straight collapse |

## Current total

- 30 fatal cinematic variants
- 30 non-fatal hit / soft-fall variants

That gives 60 explicit reaction IDs before considering facing direction, hit location, momentum, gore-layer timing, stance, and weapon/body-type visual differences.
