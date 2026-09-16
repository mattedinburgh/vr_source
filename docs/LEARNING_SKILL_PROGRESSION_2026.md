# Learning / Skill Progression 2026

## Scope

This stream tunes mercenary learning and stat progression only. It does not change strategic AI,
enemy force generation, patrols, garrisons, reinforcement logic, or campaign-force sizing.

Canonical integration target: `install/all-2026-09-12`.
Development branch: `systems/learning-skill-progression-2026`.

## Design baseline

Vengeance keeps the classic JA2/1.13 stat-award economy as the starting point:
- attributes require 50 subpoints per point;
- skills require 25 subpoints per point;
- experience level requires 350 subpoints multiplied by current level;
- normal stat learning starts from the JA2 probability `100 - current rating`;
- wisdom modifies learning for wisdom-affected skills;
- training keeps the existing training cap and does not award experience-level progress.

The stream does not globally retune individual action awards unless an award is demonstrated to
be an outlier, exploit, duplicate, or regression relative to the current 1.13 behavior.

## Corrected competence curve

The older VR mastery pass multiplied JA2's existing `100-rating` slowdown by another very steep
high-rating penalty. At 98-99 this acted as a practical hard cap rather than diminishing returns.
The revised multiplier is deliberately bounded:

| Effective rating | Extra multiplier |
| --- | ---: |
| <=30 | 125% |
| 30-40 | 125% -> 115% |
| 40-50 | 115% -> 105% |
| 50-60 | 105% -> 100% |
| 60-75 | 100% |
| 75-80 | 100% -> 95% |
| 80-85 | 95% -> 85% |
| 85-90 | 85% -> 75% |
| 90-95 | 75% -> 60% |
| 95-99 | 60% -> 50% |

This preserves novice catch-up, leaves normal competent progression close to JA2, and adds only a
modest additional penalty to expertise. The base JA2 probability already makes 95-100 difficult.

With default subpoint costs, representative expected awarded chances per +1 are:

| Rating | Attribute | Skill, WIS 80 |
| ---: | ---: | ---: |
| 60 | 125 | 48 |
| 75 | 200 | 78 |
| 85 | 392 | 155 |
| 90 | 667 | 256 |
| 95 | 1,667 | 694 |
| 98 | 4,762 | 2,381 |
| 99 | 10,000 | 5,000 |

These are awarded learning chances, not literal combat actions. Many successful actions award
multiple chances, while some failure-type awards cannot themselves cross the final subpoint boundary.
## Award-source audit

`Tools/Progression/audit_stat_awards.py` inventories all StatChange/ProfileStatChange call sites.
The first full pass found 239 call sites.

Representative high-volume sources were checked against current 1.13 structure:
- firearm use and NCTH hit bonuses;
- doctoring;
- item repair;
- teammate instruction;
- lockpicking;
- autoresolve shooting/hit awards.

No blanket rescaling was applied because these sources remain structurally aligned with 1.13.

NCTH and OCTH are mutually exclusive: `UseGun()` routes directly to `UseGunNCTH()` when NCTH
is active. NCTH gives basic failure-type learning at attack time and success-type bonuses after an
actual intended hit, so the old-CTH gun award path does not double-count the same shot.

## Validation tools

Run from the worktree:

`py Tools/Progression/model_progression.py`

`py Tools/Progression/test_progression.py`

`py Tools/Progression/audit_stat_awards.py .`

The model reports expected learning probabilities and awarded chances per +1. The regression test
guards the intended curve shape and prevents future changes from recreating an effective mastery cap.

## Playtest protocol

For the next campaign sample, record starting and ending stats for several distinct merc profiles
over a useful combat window. Prioritize Marksmanship, Dexterity, Agility, Medical, Mechanical,
Explosives, Leadership and experience level.

Flag for review if:
- a 60-80 skill rises so quickly that specialist starting stats lose value;
- a 90+ specialist gains points routinely over only a few ordinary battles;
- a 95+ stat shows no plausible movement across a long, active campaign sample;
- passive/repeatable assignments outperform equivalent field use by an obviously exploitable margin;
- OCTH/NCTH, autoresolve/tactical, or success/failure paths produce materially inconsistent growth.

Do not retune from a single merc or a single battle. Prefer accumulated awarded-event evidence and
multi-battle stat deltas before changing action-source rewards.
