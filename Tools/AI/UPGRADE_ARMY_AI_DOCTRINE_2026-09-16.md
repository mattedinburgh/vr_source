# Upgrade Army AI — Authoritative Tactical Doctrine Reconciliation

Date: 2026-09-16  
Stream: Tactical AI  
Status: authoritative design baseline for ENEMY_TEAM tactical intelligence  
Primary target branch: active AI workstream; canonical integration remains `2026 09 12` / Main / Super Master  
Strategic layer: read-only unless explicitly requested

## 1. Authority and supersession

This document consolidates the conclusions from the dedicated **Upgrade Army AI** design conversation and reconciles them with:
- current Vengeance tactical AI;
- JA2+AI / sevenfm research;
- modern 1.13;
- the unified Vengeance planner;
- Black Box / Companion battle evidence.

For ENEMY_TEAM tactical intelligence, this document **supersedes older concepts that intentionally made administrators/line troops/regulars less intelligent or less able to use advanced tactics**.

The current target is not a heterogeneous intelligence ladder. It is:

> Every enemy combatant reasons like an impossibly experienced top-tier special-operations veteran operating under JA2 fog of war.

Differences between enemy soldiers may still come from:
- equipment;
- ammunition/resources;
- weapon role;
- wounds/fatigue/suppression;
- physical position;
- mission assignment/order;
- local leadership presence;
- information actually available to that soldier/fireteam.

They must **not** come from deliberately making some ENEMY_TEAM soldiers tactically stupid.

Legacy doctrine labels such as SECURITY / LINE / VETERAN / ELITE_MOBILE / ELITE_GUARD may remain as **mission/posture labels**, but they may not reduce enemy reasoning quality or inject competence failure.

---

## 2. Governing metaphor

The target is:

> **A chess game against a grandmaster with no cheats.**

The AI should:
- evaluate multiple legal alternatives;
- anticipate likely player responses;
- think several tactical steps ahead;
- preserve future options;
- exploit positional advantages;
- avoid wasteful actions;
- coordinate timing and roles;
- reconsider immediately when new information invalidates the plan.

Unlike chess, JA2 has fog of war.

Therefore the AI must never receive:
- hidden current enemy positions;
- hidden AP;
- hidden stance;
- hidden health;
- hidden equipment;
- hidden movement;
- hidden sector strength;
- artificial vision;
- artificial perception;
- enemy-only CTH/AP/damage bonuses used as a substitute for intelligence.

The AI may make an exceptional inference from incomplete evidence.  
It may not receive exceptional evidence.

---

## 3. Enemy competence standard

All live ENEMY_TEAM combatants use top-end tactical reasoning.

Required implementation behavior:
- no competence roll can reject an otherwise legal enemy plan;
- no random utility noise may make an enemy deliberately choose an inferior position merely to simulate lower training;
- no doctrine profile may turn advanced reasoning off for ordinary enemies;
- stress/morale may change **what the correct plan is**, but not make an enemy forget how tactics work;
- mission roles may restrict actions only when the role itself makes the action irrational, e.g. a protected commander staying with the command group rather than personally breaching.

Current implementation status:
- `AICompetenceTier(ENEMY_TEAM) -> AI_COMPETENCE_ELITE`;
- enemy planner reliability is 100%;
- enemy competence utility noise is zero;
- coordinated planning is not randomly denied;
- advanced enemy manoeuvre can only be vetoed by explicit mission-role restrictions.

This is correct and must be preserved.

---

## 4. Brave, psychologically steady, never suicidal

Every enemy should be courageous and resilient under fire.

Bravery means willingness to accept **useful risk**, including:
- holding tactically important ground;
- crossing a dangerous lane when suppression/support makes the move worthwhile;
- completing a valuable flank;
- covering a withdrawing teammate;
- rescuing a casualty when survival odds justify it;
- delaying the player;
- maintaining pressure during a genuine advantage;
- staying engaged despite casualties when the local plan still has value.

Bravery must not mean:
- unsupported frontal charges;
- repeated exposure to a known kill zone;
- crossing catastrophic danger for no tactical payoff;
- rescuing a casualty through obviously lethal exposure;
- continuing an unwinnable isolated attack when regroup/fallback is available;
- ignoring suppression, wounds, exhaustion or route failure.

Implementation principle:

> **Danger is usually a cost, not a hard prohibition.**

Mission value, support, local superiority, time pressure and lack of alternatives may justify accepting danger. Catastrophic danger still overrides ordinary aggression.

Current implementation status:
- enemy risk tolerance is explicitly raised and bounded;
- personal withdrawal/fallback exists;
- tactical fallback and disengagement are separate;
- rescue routines reject suicidal rescues;
- route/exposure systems can reject catastrophic movement.

Validation requirement:
- ensure brave enemies do not become overly cautious because several independent safety layers all penalize the same risk.

---

## 5. Local hive mind, never sector omniscience

Enemy intelligence is shared **locally**, not globally.

The target network is:
- fireteam/local-element scoped;
- bounded by physical proximity and plausible communication;
- confidence-weighted;
- age-decaying;
- capable of limited relay;
- unable to turn reports into exact hidden target state.

A report should carry concepts such as:
- source;
- confidence;
- age;
- approximate location/bearing;
- threat type;
- intended tactical response.

Shared reports may affect:
- facing;
- route choice;
- flank side;
- fallback direction;
- suppression priority;
- search priority;
- smoke planning;
- casualty protection;
- team intent;
- reserve/QRF response.

Shared reports **must not authorize direct fire by themselves**.  
The firing soldier must still satisfy normal JA2 legal target knowledge / attack legality.

Enemy initial alarm may tell the sector that combat exists, but must not copy exact opponent locations into a sector-wide public opponent list.

Current implementation status:
- sector-wide enemy radio sighting propagation is suppressed;
- planning uses local fireteam contact reports;
- shared contacts are explicitly marked planning-only;
- confidence decay/bounded relay is present in the contact network;
- perceived enemy strength and movement-risk helpers consume planning contacts rather than hidden live state.

Mandatory audit:
- preserve the intended bounded relay/two-hop ceiling;
- verify no legacy `gbPublicOpplist` path silently reintroduces enemy-sector omniscience.

---

## 6. Fireteam as the primary tactical intelligence unit

The enemy should not behave as:
- 20 independent geniuses; or
- one omniscient sector brain.

The target is several **coherent local fireteams/elements**, typically around 5–10 soldiers where map population allows.

A fireteam should share:
- a coarse tactical picture;
- a primary problem/contact axis;
- a current intent;
- complementary tasks;
- short-lived plan state;
- local success/failure memory.

The individual soldier still owns:
- exact movement execution;
- AP legality;
- stance legality;
- weapon/item legality;
- immediate self-preservation;
- personal direct-fire legality.

Current implementation status:
- fireteam identities exist;
- local fireteam counts and support checks exist;
- remnant reattachment exists;
- reserve/QRF release is element-based;
- same-fireteam checks constrain cooperation.

---

## 7. Shared intent

The fireteam should have a persistent but interruptible tactical intent:

- HOLD;
- PRESS;
- FLANK;
- FALLBACK;
- DISENGAGE;
- RESCUE.

The intent should not be a rigid script.

It persists long enough to coordinate multiple soldiers, but must be invalidated by:
- surprise contact;
- major casualty;
- collapse of support;
- route becoming unsafe/blocked;
- objective change;
- severe isolation;
- new multi-angle threat;
- catastrophic personal danger.

This prevents the classic JA2 failure where every soldier recalculates independently and the group oscillates between advance/hide/flank from turn to turn.

Current implementation status:
- persistent tactical intents and interruptible short plans are present;
- plan state is transient and load/rewind hardened.

---

## 8. Complementary fireteam roles

Required dynamic roles:

- **SUPPORT / BASE OF FIRE** — suppress/fix, preserve firing lane, cover movers;
- **MANEUVER** — advance under support toward useful geometry;
- **FLANKER** — exploit a side/weak sector;
- **SCREEN / REAR GUARD** — protect withdrawal, flank/rear security, prevent collapse;
- **RESERVE** — remain uncommitted until needed.

Roles must be dynamic. They depend on:
- weapon;
- ammo;
- position;
- wounds;
- fatigue;
- current support;
- threat axis;
- route quality;
- mission state.

A machine gunner is often support but not permanently locked to it. A rifleman can become support when the usual support element is suppressed.

Current implementation status:
- dynamic role scoring exists;
- task reservations deconflict jobs;
- withdrawal cover selection exists;
- reserve handling exists.

---

## 9. Suppression, base of fire and movement

Suppression is not an isolated attack choice. It is a movement-enabling team action.

Target behavior:
1. identify a threat/fire lane;
2. establish effective fire where tactically useful;
3. reserve one or more movers;
4. move only when support is credible;
5. preserve support until movers reach a useful position or abort;
6. exploit success, or replan if suppression fails.

Required interactions:
- suppress -> maneuver;
- suppress -> flank;
- suppress -> casualty rescue;
- suppress -> covered withdrawal;
- suppress -> smoke -> movement.

Avoid:
- every soldier suppressing the same target;
- suppression when no teammate can exploit it;
- suppression of stale contacts with unrealistically precise aim;
- firing through dangerous friendly lanes.

JA2+AI lesson:
- preserve sevenfm's mature suppression execution/tuning;
- planner owns **why/when/who**, legacy execution often owns **how**.

---

## 10. Bounding and movement coordination

Bounding should be condition-based rather than a rigid animation script.

A move is justified when:
- another element can cover;
- route exposure is acceptable relative to mission value;
- movement improves geometry;
- the destination provides cover/LOS/range/flank value;
- enough AP remains for a sensible end state.

The team should avoid:
- all members advancing simultaneously through the same exposed lane;
- one-man trickle attacks;
- bunching;
- repeatedly feeding the same centre axis after casualties/suppression.

Mover limits should be decided at element level so several soldiers evaluating the same situation do not all independently decide to move.

Current implementation status:
- mover/task deconfliction exists;
- fireteam support checks exist;
- shared attack-axis pressure/setback memory exists;
- route exposure scoring exists.

---

## 11. Flanking and crossfire

Flanking should be a team decision, not a random left/right preference.

Planner responsibilities:
- decide whether flanking is preferable to hold/press/fallback;
- choose a preferred flank axis;
- allocate one or more flankers;
- keep a fixing/support element;
- preserve reserve/security;
- stop feeding a failed approach;
- exploit flank success.

Execution responsibilities:
- use mature Vengeance/sevenfm flank/pathing behavior;
- respect actual AP/path/terrain legality.

Crossfire is desirable only when it improves threat containment without creating friendly-fire risk or isolated soldiers.

Current implementation status:
- shared flank-axis preference exists;
- flank reservations exist;
- mature sevenfm flank execution is retained;
- local approach-setback memory exists.

Important rule:
- do not let planner gates veto a strong mature flank merely because of an obsolete competence/doctrine restriction.

---

## 12. Smoke doctrine

Smoke must always have a purpose.

Good tactical sequences include:
- SMOKE -> CROSS;
- SMOKE -> RESCUE;
- SMOKE -> FALLBACK;
- SUPPRESS -> SMOKE -> MANEUVER;
- SMOKE -> reposition wounded/support element.

Avoid:
- smoke for its own sake;
- duplicate smoke;
- smoke when a strong direct attack is clearly better;
- smoke that blocks the team's own decisive fire lane without compensating value.

JA2+AI's mature smoke execution is a donor/benchmark.  
The unified planner should own the tactical purpose.

---

## 13. Casualty rescue and medic behavior

Casualties create tactical tasks, not automatic suicide missions.

Desired sequence:
1. determine whether casualty is recoverable;
2. assess exposure and route;
3. assign cover/smoke if required;
4. assign rescuer/medic;
5. preserve security;
6. abort when conditions become catastrophic;
7. stabilize/recover;
8. reassess formation.

Medics prioritize life-saving actions, but:
- do not run through obviously lethal lanes;
- do not abandon the entire team plan without tactical cover;
- may require smoke or suppression first.

Covering soldiers should recognize:
- teammate withdrawing;
- teammate advancing;
- casualty rescue movement.

Current implementation status:
- casualty response exists;
- medic priority exists;
- suicidal-rescue checks exist;
- covering withdrawal/advance helpers exist.

---

## 14. Morale, cohesion, rout and rally

Morale does **not** reduce enemy intelligence.

Morale changes:
- willingness to hold;
- willingness to press;
- fallback/disengagement choice;
- risk tolerance;
- local cohesion;
- likelihood of organized withdrawal/rout.

Local psychological state should consider:
- casualties;
- clustered recent losses;
- leader loss/breaking;
- nearby cowering/routing;
- suppression/shock;
- isolation;
- successful friendly attack;
- stable nearby leadership;
- recovery/regrouping.

Rout contagion should be local, not sector-magical.

A nearby leader visibly breaking is destabilizing.  
A stable leader can slow collapse/rally the element.

Because the target enemy force is psychologically steady:
- casualties should not immediately turn them into cowards;
- high casualties plus isolation, repeated tactical failure and catastrophic local odds can still produce disengagement/rout.

Current implementation status:
- local rout pressure exists;
- leader effects exist;
- collapse requires sustained evidence;
- recovery/rally streaks exist.

---

## 15. Leadership and command succession

Leaders improve:
- coordination;
- plan stability;
- rally/recovery;
- neighbouring-element synchronization;
- reserve release.

They do **not** grant intelligence that ordinary enemy soldiers lack.

If a leader is killed:
- the fireteam does not become tactically stupid;
- available lower-level leadership can stabilize locally;
- otherwise the team still uses elite reasoning but with less command support/cohesion.

A General/VIP may remain with the command element because of role value.  
That is a mission restriction, not a competence restriction.

Current implementation status:
- local command authority/radii exist;
- NCO command is fireteam-local;
- senior command reaches neighbouring elements only within bounded local radius;
- no rank grants sector-wide exact information.

---

## 16. Reinforcement / contact-response doctrine

The entire sector should not magnetize toward one gunshot.

Response should be:
- local;
- staged;
- proportional;
- element-based;
- sensitive to whether another fireteam is already engaged elsewhere.

Fixed guards/snipers may keep their mission.  
A command group may remain reserve while another coherent element can respond.  
Later waves release only when escalation justifies them.

Current implementation status:
- fireteam-level response budget exists;
- nearest deployable element receives first mission;
- complete elements are released rather than peeling arbitrary soldiers;
- separate fights can remain separate;
- command group reserve behavior exists.

This is tactical response only.  
Strategic force movement/reinforcement remains read-only.

---

## 17. Remnants and last survivors

A shattered 1–2 man element should first try to join another viable nearby fireteam.

Priority:
1. safe reattachment/regroup;
2. local fallback/disengagement;
3. sector escape only under genuine collapse where existing tactical/strategic rules already permit it.

The last survivor should not continue an irrational private attack if a nearby element can absorb him.

Current implementation status:
- remnant destination selection exists;
- reserve logic keeps remnants from being assigned as independent QRF;
- cohesion action can physically reattach them.

---

## 18. Surprise and encirclement

A soldier moving into unexpected personal contact must immediately reassess.

Allowed responses:
- stop;
- hold;
- seek cover;
- return toward last decision tile;
- move laterally;
- withdraw through the safest known sector;
- continue fighting if no alternative materially improves the situation.

Encirclement cannot be inferred from hidden enemies.

Strong encirclement conclusions require personal/current multi-angle evidence.  
Stale/heard/shared reports may increase caution but may not manufacture a fake "surrounded" state.

Current implementation status:
- surprise tracker exists;
- visible multi-angle geometry exists;
- weakest-sector breakout exists.

---

## 19. Short-horizon planning

The AI should think several moves ahead without attempting an expensive global perfect search.

Useful tactical plans:
- FIX -> FLANK -> ASSAULT;
- SUPPRESS -> MOVE -> COVER;
- SMOKE -> CROSS -> REASSESS;
- COVER -> RESCUE -> WITHDRAW;
- FALLBACK -> REGROUP -> COUNTERATTACK;
- SEARCH -> SUPPORT -> CLEAR.

Plans should be:
- 1–3 tactical steps;
- interruptible;
- revalidated after each material change;
- abandoned when assumptions fail.

Hysteresis/commitment is important: the AI should not thrash between advance/flank/fallback every decision.

---

## 20. JA2+AI / sevenfm integration rule

JA2+AI is the primary historical external tactical-AI benchmark.

Key conclusion:
- sevenfm produced exceptional **individual/micro tactical behavior**;
- he explicitly described the historical system as lacking true group command;
- Vengeance already inherits substantial sevenfm behavior.

Therefore:

> **Our planner is the brain above sevenfm's hands.**

The planner owns:
- shared intent;
- role allocation;
- axis choice;
- reservations;
- plan confidence;
- local memory;
- replan conditions.

Mature Vengeance/sevenfm code should usually retain:
- actual flank movement/pathing;
- cover execution;
- stance/fire-mode execution;
- suppression mechanics;
- smoke throw execution;
- grenade/launcher execution;
- obstacle/fence interaction;
- low-level AP/item/animation legality.

Do not rewrite proven microbehavior merely because a new abstraction looks cleaner.

---

## 21. Planner-veto rule

A higher-level planner can make the AI worse by vetoing mature legal behavior.

For every material planner gate, telemetry should make it possible to determine:
- what mature legal action was available;
- whether the planner vetoed it;
- why;
- what was selected instead;
- whether the result was better.

The historical B2 finding that coordinated-flank candidates were rejected by competence/doctrine friction is now interpreted as an obsolete-model warning. For ENEMY_TEAM, competence failure must not be the reason a legal coordinated flank is rejected.

Valid rejection reasons include:
- mission-role restriction;
- no credible support;
- unacceptable route;
- personal emergency;
- stale/insufficient legal information;
- duplicate task already reserved;
- plan superseded by a better tactical objective.

---

## 22. Black Box / Companion self-improvement loop

The AI should improve from real battles, not from source-code elegance alone.

Loop:
1. play/automate battle;
2. record structured Black Box facts silently;
3. analyze with Companion;
4. identify pathing/coordination/information/risk failure;
5. patch one hypothesis;
6. rerun comparable scenarios;
7. compare outcomes;
8. keep/revert based on evidence.

Required telemetry themes:
- knowledge source/confidence;
- fireteam ID;
- intent/role;
- candidate actions;
- selected action;
- rejected candidates/reasons;
- task reservations;
- support state;
- route exposure;
- shared approach pressure;
- surprise/encirclement;
- casualty/rout pressure;
- planner vetoes of mature behavior;
- execution success/failure;
- decision time/performance.

---

## 23. Scalability

Do not copy JA2+AI's historical hard intelligence cutoff at high combatant counts.

When battle size rises:
- preserve legal information boundaries;
- preserve fireteam intent/roles;
- preserve emergency self-preservation;
- preserve coordination;
- reduce candidate count/depth first;
- reuse safe fireteam-level calculations;
- stagger expensive route evaluation;
- profile decision time by subsystem.

Large battles must not become dumb zerg battles.

---

## 24. Strategic-layer boundary

This doctrine governs **tactical AI only**.

Do not modify:
- strategic force movement;
- garrisons;
- patrols;
- campaign reinforcement generation;
- strategic logistics;
- force sizing;
- strategic AI missions;

unless explicitly requested.

Existing tactical code that interfaces with previously implemented retreat state should not be expanded into new strategic behavior as part of this reconciliation.

---

## 25. Reconciliation checklist

Every future Tactical AI change must answer:

1. Does every ENEMY_TEAM soldier retain elite reasoning?
2. Does the change use only legal information?
3. Is information local/confidence-decayed rather than sector-global?
4. Does the fireteam have a coherent intent?
5. Are support/mover/flank/rear/reserve jobs deconflicted?
6. Is risk meaningful rather than simply forbidden?
7. Can surprise/new information interrupt the plan?
8. Does the behavior preserve mature sevenfm/Vengeance execution?
9. Could the planner be suppressing a good legacy action?
10. Does Black Box provide evidence to answer that question?
11. Does the change scale to large battles?
12. Does it leave the strategic layer untouched?

This checklist and the JA2+AI research dossier are mandatory Tactical AI research gates.
