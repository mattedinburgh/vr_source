# B2 Tactical AI Battle Lessons ΓÇö 2026-09-15

Status: PHASE 2 IMPLEMENTED / STATIC QA PASSED ΓÇö REQUIRES BUILD + PLAYTEST

Implementation now covers two B2 team-reasoning passes. Phase 1 added local directional contact callouts, effective-fire movement windows, shared attack-axis setback pressure, base-of-fire/mover role pairing, and a basic fireteam flank path that does not require the advanced-doctrine gate. Phase 2 adds a shared fireteam flank-axis preference so soldiers that agree on manoeuvre also tend to agree on left versus right, while exact tile choice and emergency self-preservation remain individual. Strategic/campaign movement remains untouched. Black Box remains the evidence source for the next playtest.

## Upgrade Army AI reconciliation — 2026-09-16

The old B2 finding that viable coordinated-flank candidates were rejected by a competence/doctrine gate is now **historical baseline evidence, not current doctrine**.

Current ENEMY_TEAM rules:
- every enemy uses elite tactical reasoning;
- planner reliability is not randomly reduced for enemy competence;
- enemy utility selection has no artificial competence noise;
- coordinated plans are not rejected because an ordinary enemy is considered too poorly trained;
- remaining enemy vetoes must be attributable to real tactical/mission causes such as route safety, insufficient support, duplicate reservation, emergency self-preservation, stale legal information, or explicit mission-role restriction.

The next Black Box comparison should therefore track whether good mature Vengeance/sevenfm actions are being vetoed by the higher planner and whether the replacement action actually improves the outcome.


## Scope
Black Box battle_id=3, B2, including reload/retry branches.
Battle opened with 18 enemies versus 12 player mercs + 20 militia.
Strategic context: enemy operation morale was high and additional enemy formations were nearby.
Retry branches mean raw action totals are behavioral samples, not one continuous timeline.

## Primary lesson
Individual tactical reasoning is substantially ahead of team reasoning.
Soldiers can perceive danger, react to surprise, seek cover, withdraw, and reassess geometry.
The missing layer is converting several locally sensible observations into one shared fireteam plan.

## Team-level behavior requirements
1. Local fireteams should develop a shared intent: hold, fix/suppress, flank, disengage, rescue, or reserve.
2. Heavy resistance on the centre axis should reduce willingness to continue attacking that axis.
3. The team should compare centre / left / right / hold-reposition approaches rather than treating every move independently.
4. Failed approaches should temporarily accumulate local risk memory so successive soldiers do not rediscover the same kill zone.
5. Successful manoeuvre should create momentum: safe flank progress, new LOS, or defender reorientation should increase support for that plan.
6. Roles should be complementary: fixing element, manoeuvre element, support, reserve/rear security.
7. Coordinated plans need persistence across several soldiers and several decisions, but must break immediately on genuine surprise or collapse.

## Very-local knowledge sharing
Fireteam cooperation must not become global omniscience.
Nearby soldiers should share limited, plausible contact information such as:
- "enemy/contact on the right"
- "contact front/left"
- "shots from that direction"
- "heavy resistance on centre approach"
- "friend hit / route dangerous"
- "flanker made progress"

Shared information should be local, recent, confidence-weighted, and degradable.
Exact hidden grids should not be granted merely because another soldier knows something.
The purpose is to make nearby soldiers cooperate, not create a hive mind.
A soldier who sees a threat can improve nearby teammates' directional understanding and plan choice.
A soldier outside the local element should continue to rely on normal personal/team knowledge rules.

## Evidence from this battle
Spatial awareness is functioning: threat direction, flank quality, rear safety, crossfire, support, surprise and encirclement are being evaluated.
Contact reassessment is functioning: surprised soldiers can stop, reconsider and choose safer/breakout positions.
Suppression response is functioning and can choose hold, local cover or bounded fallback.
However, 11 observed coordinated-flank candidates were rejected by the competence/doctrine gate.
This is the clearest current symptom of good individual reasoning failing to become team manoeuvre.

## Attack-axis lesson
"Centre is heavy -> go around" should be a team-level inference, not a hard scripted flank.
Evidence must come from legitimate knowledge: visible contacts, reported contacts, incoming fire, suppression, casualties, failed advances and remembered firing lanes.
The team should become progressively less willing to reuse an approach that repeatedly produces suppression/casualties without progress.

## Phase 2 validation targets
The next B2-style playtest should test the change as a hypothesis, not as a presumed improvement. Black Box should show:
- lower disagreement between same-fireteam flankers on left versus right for the same contact;
- successful flank selections committed with `fireteam_flank_axis` plus the actually selected action/grid;
- support soldiers remaining in the base of fire while one or two movers exploit the chosen side;
- fewer repeated advances through an axis carrying recent shared setback pressure;
- immediate abandonment of the shared axis when surprise, collapse, unacceptable personal danger, or an invalid route triggers a higher-priority response;
- no increase in cross-fire bunching, unsupported rushes, or hidden-information use.

A failed shared route must still allow the individual execution layer to use the opposite legal flank when necessary. The shared axis is tactical inertia, not a command that overrides route safety.

## Morale / operational context
Do not globally raise enemy morale from this battle.
The enemy fought almost to annihilation and did not strategically retreat.
Nearby friendly strategic formations should at most provide a modest operational-confidence effect.
They must not be counted as local combat power because they cannot fire, suppress, rescue or absorb casualties in this sector.

## Grenade/throw observation
One notable throw by enemy actor 43:
- effective strength 90, no Throwing trait
- target distance 18 tiles
- computed maximum range 23 tiles
- landing one tile from the nearest player
The distance is within the calculated legal range, so this is not evidence of a range cheat.
The precision is worth auditing against current 1.13 throw dispersion/CTH.
Current Black Box does not expose the throw CTH/dispersion roll, so no balance conclusion yet.

## Hustler accuracy incident
Target profile 219 (Hustler) was repeatedly hit/killed by enemy actor 43 across retry branches.
The shooter had MRK 86, experience level 5 and used weapon 633 (Colt M4A1).
Ranges were approximately 13-17 tiles, well inside the M4A1's 335 range value.
One retry shows a 10-round ammunition drop and six registered hits, which is high enough to audit burst/autofire dispersion but not by itself proof of cheating.
Other deaths were accelerated by high-damage head hits, including a 66-damage head hit.
Repeated kills are not fully independent evidence because the trace contains reload/replay branches from very similar tactical states.
Current NCTH source inspection found no positive enemy-only difficulty CtH modifier in the NCTH calculation; legacy CtH difficulty code is clamped so human combat AI does not receive a positive hidden bonus.
Before changing accuracy, add/inspect per-shot diagnostics for aim clicks, aperture/CTH, recoil state, shot number, target stance/cover, attachments and RNG/dispersion result.

## Priority conclusions for AI stream
1. Highest priority: local fireteam cooperation and shared intent.
2. High priority: attack-axis pressure memory and flank exploitation.
3. Historical issue resolved in doctrine: ENEMY_TEAM competence friction must not suppress viable coordinated flanks; validate that remaining mission/safety gates are justified.
4. Preserve strong surprise, suppression and individual self-preservation behavior.
5. Keep local information sharing bounded and plausible.
6. Separate tactical AI conclusions from NCTH/throw conclusions; accuracy and grenade precision remain investigation items.
7. No implementation from this note until more battles confirm the pattern.

## Attacking as a unit
An attacking formation must enter combat with a shared mission and local group identity.
Soldiers should not independently optimize only their own survival while the rest of the assault happens around them.

Required doctrine:
- the group has a common objective and attack direction before contact;
- nearby soldiers recognize who is part of their local assault element;
- one element can fix/suppress while another advances or flanks;
- movement by one soldier should be influenced by what nearby teammates are doing;
- if the centre becomes costly, the group can collectively shift the main effort left/right rather than repeatedly feeding individuals into the same axis;
- soldiers should preserve spacing and mutual support rather than bunching or fragmenting;
- leaders/NCOs can stabilize the local plan, coordinate role changes and keep the group committed;
- casualties, suppression, loss of leaders or isolation can break cohesion and cause the unit to fragment;
- local fireteam knowledge-sharing should support this unit behavior without granting global omniscience.

The intended result is coordinated assault behavior: suppress-and-advance, fire-and-move, bounded movement, fixing fire, flank exploitation, covering withdrawal and local reserve behavior.
Individual self-preservation remains important, but should operate inside the shared team plan rather than replacing it.
