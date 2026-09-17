# JA2+AI / sevenfm Tactical AI PhD — Vengeance Research Dossier

Date: 2026-09-16  
Stream: Tactical AI  
Status: research baseline complete; implementation decisions must still be validated in playtests  
Primary comparison target: current Vengeance unified tactical AI + sevenfm/JA2+AI + modern 1.13  
Strategic layer: out of scope / read-only

## 1. Why this dossier exists

JA2+AI is not an ordinary campaign mod. It is the most important historical tactical-AI laboratory in the JA2 1.13 ecosystem and therefore a mandatory reference for Vengeance tactical-AI work.

The correct relationship is not "Vengeance is old; port modern 1.13." Vengeance itself already contains a large amount of sevenfm-derived logic, modern 1.13 contains another subset/evolution of sevenfm work, and the standalone JA2+AI executable continued experimenting beyond both at various points. The three lines cross-pollinated.

The research question is therefore:

> Which JA2+AI ideas are already inherited, which were later improved or reverted, which remain stronger than our implementation, and which historical failure modes should constrain the new Vengeance fireteam planner?

This dossier is a mandatory pre-implementation reference for tactical AI.

---

## 2. Evidence hierarchy used

1. Explicit Vengeance design decisions / current project methodology.
2. Current Vengeance project source and current AI workstream source.
3. Modern 1.13 source/documentation.
4. Primary sevenfm Bear's Pit posts and change logs.
5. Long-form player reports from Bear's Pit.
6. Later community summaries/package documentation.
7. Inference only where the above do not fully resolve a question.

Forum evidence is especially valuable for practical gameplay: a feature can be elegant in source and still be bad in a campaign.

---

## 3. What JA2+AI actually was

JA2+AI grew from sevenfm's Experimental Project 7 work beginning around 2014 and became a replacement executable for the 2014 stable 1.13 r7609 family.

The useful mental model is:

- 7609+fix: old stable baseline plus bug fixes.
- 7609+AI: bug fixes plus extensive tactical-AI and combat experimentation.
- modern 1.13: gradually received many, but not all, sevenfm tactical-AI ideas.
- Vengeance Reloaded: independently incorporated a large amount of sevenfm work and had direct sevenfm involvement.
- our unified AI: a newer planning/coordination layer built above the Vengeance/sevenfm execution library.

JA2+AI therefore matters less as a downloadable "mod" and more as a decade of tactical-AI experiments plus real-player feedback.

---

## 4. Historical chronology and lessons

### 4.1 2014–2015: repairing the inherited AI foundation

Sevenfm's early work found that old 1.13 flanking had not scaled correctly to the 100-AP system, larger maps and expanded sight ranges.

Important failures identified in the old implementation included:

- flanking ranges inherited from small maps and older sight assumptions;
- AP calculations inherited from the 25-AP era;
- weak direction selection;
- too-small search ranges;
- soldiers entering buildings and terminating useful flanks;
- water, edges and other geometry breaking routes;
- limited roaming orders interacting badly with flank logic.

The 2015 flanking patch that entered trunk expanded direction search, corrected AP assumptions, avoided map edges/deep water/buildings, tied flank distances to visibility/time of day, used faster movement, constrained who should flank, and added friend-position checks.

**Vengeance lesson:** tactical behavior must be tested under the actual map scale, AP model, sight model and terrain. A "correct" doctrine attached to obsolete geometric assumptions is still broken.

### 4.2 2016–2017: perception, night combat and uncertainty

JA2+AI increasingly treated darkness, light, hearing and last-known positions as tactical information rather than simple triggers.

Important developments included:

- more effective night flanking;
- light avoidance;
- stealthier movement;
- watched/focused locations;
- randomization of heard/noise locations;
- stronger randomization when there is no LOS to the noise source;
- non-linear noise-memory strength;
- smoke for dangerous movement.

The RANDOMIZE_NOISE_LOCATION work is especially important. Sevenfm explicitly recognized that hearing a shot should not grant a precise firing solution. Error grows with range and poor LOS.

**Vengeance lesson:** uncertainty must be represented explicitly. Our contact-belief model is the correct next step, but any legacy action that bypasses it can reintroduce pseudo-omniscience.

### 4.3 2017–2018: smoke, grenades and suppression become tactical tools

JA2+AI moved smoke away from "grenade type the AI may randomly throw" toward a tactical enabler:

- smoke to cross dangerous ground;
- smoke when advancing;
- smoke associated with fallback/retreat;
- smoke for another soldier;
- avoid duplicate/unnecessary smoke;
- avoid smoke when a good real attack exists;
- reduce smoke use when too close for it to be useful.

Area weapons and grenades increasingly used known/public information, with special handling for buildings, launchers and obstacles.

Sevenfm also experimented with obstacle breaching using explosives when path geometry strongly implied a barrier.

**Vengeance lesson:** utility depends on the follow-up. "Throw smoke" is not the plan. "Smoke -> move/rescue/withdraw/reposition" is the plan.

### 4.4 2018–2019: practical difficulty exposes unfairness and deadlocks

Player feedback is especially valuable here.

Blind suppression became one of JA2+AI's signature features, but players reported that earlier revisions could feel too accurate and lethal, especially with NCTH. Sevenfm subsequently toned it down.

This is the canonical example of a behavior that is:
- doctrinally realistic;
- tactically interesting;
- capable of feeling like cheating if uncertainty is too narrow.

Suppression was repeatedly retuned for:
- frequency;
- whether shooter had cover;
- friendly-fire risk;
- turn timing/performance;
- target state.

At the same time, artillery/red-smoke avoidance exposed another class of failure. A player who greatly expanded the artillery radius observed AI/militia forming a crescent around the danger area and entering deadlocks. The logic was sensible locally, but the hard exclusion geometry left no useful path.

**Vengeance lesson:** danger should usually be a scored cost, not an absolute prohibition. Hard exclusions need escape clauses. The planner must know when mission value, time pressure or lack of alternatives justifies crossing danger.

### 4.5 2019: emergent "group attack" without true group AI

Sevenfm explicitly explained the architecture:

- most 1.13 decisions were randomized;
- spacing was not centrally controlled;
- morale mainly changed seek/help/hide/watch and aggression/passivity;
- there was no group AI;
- there were no commanders/officers making group-level decisions.

Group-looking behavior was emergent. Examples:
- if a nearby friend had a successful attack, another soldier could become more willing to attack;
- soldiers could ignore danger/stop conditions when nearby allies were succeeding;
- after a flanker scored useful hits, other soldiers might rush.

A demo around r1068 showed survivors hiding after initial losses, waiting until friendly flankers scored successful hits, then rushing together.

This is both a strength and a ceiling.

**Strength:** decentralized behavior is robust, cheap and unpredictable.  
**Ceiling:** no one decides "support fixes the front; maneuver pair takes left; reserve waits; assault follows on success."

**Vengeance lesson:** our fireteam planner should preserve decentralized execution while adding a bounded shared intent layer. Do not replace local autonomy with one sector-wide mastermind.

### 4.6 2019–2020: retreat, path safety and the cost of ambitious behavior

Tactical retreat was introduced/experimented with but exposed tactical/strategic-state problems. At one point sevenfm disabled retreat after reports that soldiers leaving a sector could create broken/invisible enemy state. Later trunk retained tactical retreat as optional and it required later fixes.

Other failures included:
- endless loops when a soldier could not find an acceptable darker tile;
- movement/danger calculations becoming expensive;
- artillery danger creating pathing deadlocks.

By 2020 JA2+AI automatically disabled some "complex AI" calculations in realtime or when more than 42 active combatants were present, and the Slow PC option also disabled them.

**Vengeance lesson:** large-battle scalability is a first-class AI requirement. We should not solve this by turning off team intelligence at a hard threshold. Better approach:
- preserve shared intent and information honesty;
- reduce candidate count/search depth/route detail progressively;
- cache only safe transient features;
- use tiered budgets by combatant count;
- keep emergency/self-preservation rules cheap and always active.

### 4.7 2020–2021: mature battles show what "smart" feels like

One of the strongest practical reports described a night battle where the AI:
- remembered player positions from gunfire;
- changed approach after a mine detonation;
- used windows and alternate routes;
- flanked;
- used flares and smoke;
- selected dark approaches;
- went quiet for several turns;
- then attacked more cohesively.

The player described the experience as feeling closer to fighting another player.

This matters because it defines the practical benchmark. We do not need novelty for its own sake; we need to preserve and exceed this observable behavior.

**Vengeance lesson:** the benchmark suite must include long-horizon situations, not only single-turn tactical choices.

### 4.8 2021–2025: continual tuning proves AI must understand mechanics

Later JA2+AI/changelog work repeatedly changed AI valuation in response to mechanics:

- suppression willingness depending on actual suppression/shock effectiveness;
- grenade range/utility corrections;
- target priority adjustments;
- retreat/cover behavior;
- behavior while blinded;
- avoiding pointless shots on dying/invalid targets;
- red-smoke/artillery checks only when corresponding mechanics exist;
- door/visibility logic causing endless-clock regressions and partial reverts.

The pattern is more important than any one revision:

> AI utility must be derived from current game mechanics, not from a fixed doctrine written for another ruleset.

That is especially relevant to Vengeance because we are simultaneously changing NCTH, cover/penetration, grenades, items and maps.

---

## 5. Practical gameplay findings from Bear's Pit

### Behaviors players consistently perceived as "smart"

- flanking instead of direct zerg rush;
- avoiding exposed/lit routes at night;
- reacting to gunfire without exact omniscience;
- using smoke before crossing danger;
- using windows/alternate access;
- changing route after mines/casualties;
- taking cover intelligently;
- suppressing a recently occupied position;
- waiting rather than forcing a bad move;
- attacking when another element has created an advantage.

### Behaviors players perceived as cheating or frustrating

- blind suppression with too little positional uncertainty;
- high lethality from unseen fire under NCTH;
- perfect-seeming response to hidden player movement;
- path/danger deadlocks;
- excessive passivity after defensive logic becomes too strong;
- endless clocks/loops;
- very slow large battles.

### Important interpretation

"Smarter" did not mean "more aggressive."

The best reports came from AI that could:
- pause;
- flank;
- wait for an advantage;
- attack after conditions changed.

The worst reports often came from a rule that was too hard:
- never enter danger;
- always avoid artillery radius;
- repeatedly search for a darker/better tile;
- use unseen fire with too much certainty.

---

## 6. Direct code-lineage audit

Public Vengeance source is already heavily sevenfm-derived.

The core files contain large numbers of explicit sevenfm-marked changes, including:
- DecideAction;
- AIUtils;
- Attacks;
- FindLocations;
- Knowledge;
- AIMain;
- PATHAI.

Examples present in the Vengeance lineage include:
- dedicated start/continue flanking logic;
- smoke-covered movement;
- grenade special-purpose logic;
- wirecutter use;
- path/final-spot danger abortion;
- sight-cover advance;
- local/public outnumbering checks;
- successful-attack checks;
- team-under-attack counts;
- friend-black/flanking counts;
- cover, light, bomb, corpse and obstacle handling;
- role-aware and weapon-aware decisions.

Therefore do **not** characterize Vengeance as stock r7609 tactical AI.

Modern 1.13 has later/parallel sevenfm-derived features that Vengeance does not mirror identically, including current retreat helpers, friend-smoke support and optionized unseen/safe suppression.

---

## 7. Current Vengeance unified AI vs JA2+AI

### 7.1 Areas where our architecture is clearly more explicit/advanced

#### Legal belief layer
Our AICONTACTBELIEF normalizes legal last-known location, source, age and confidence. JA2+AI improved uncertainty, but remained primarily built around legacy opponent/noise structures plus randomization.

**Decision:** KEEP OURS. Use JA2+AI uncertainty lessons as calibration/tests.

#### Explicit fireteams
Our AI has local fireteam identity, local membership and local support calculations.

JA2+AI explicitly had no group-level planner.

**Decision:** KEEP OURS; this is the main architectural opportunity.

#### Shared intent
Our HOLD/PRESS/FLANK/FALLBACK/DISENGAGE/RESCUE intents establish a team context beyond individual randomized decisions.

**Decision:** KEEP, but validate that intent does not suppress useful legacy actions.

#### Dynamic complementary roles
SUPPORT/MANEUVER/FLANKER/SCREEN/RESERVE is beyond JA2+AI's mostly implicit role emergence.

**Decision:** KEEP, but roles must be cheap to abandon when reality changes.

#### Task reservations
Reservations prevent five soldiers independently choosing the same tactical job.

**Decision:** KEEP. This directly addresses duplicate behavior without omniscience.

#### Short persistent plans
Our 1–3-step commitments solve a problem JA2+AI largely handled through counters/local heuristics.

**Decision:** KEEP, but every plan needs emergency invalidation.

#### Shared tactical geometry
Our 8-sector threat/support geometry and weakest-breakout reasoning are more unified than the historical collection of directional heuristics.

**Decision:** KEEP, but geometry must remain belief-bound.

#### Setback/approach memory
Our local, decaying memory of a bad approach is a cleaner generalization of many JA2+AI special-case danger reactions.

**Decision:** KEEP; ensure "bad ground" never becomes hidden-enemy knowledge.

### 7.2 Areas where JA2+AI remains an important behavior benchmark

#### Night combat
JA2+AI accumulated years of tuning around darkness, flares, lights, stealth movement and flank timing.

**Decision:** ADAPT / BENCHMARK. Our planner should consume the mature execution logic rather than override it with generic utility.

#### Suppression
JA2+AI's repeated practical tuning is more valuable than a one-shot theoretical model.

**Decision:** ADAPT. Use current actual suppression mechanics, uncertainty and friendly-fire checks; benchmark frequency and lethality.

#### Smoke
JA2+AI learned when smoke is useful, redundant or wasteful.

**Decision:** ADAPT. Planner owns purpose; legacy execution provides tactical smoke mechanics.

#### Obstacle/fence breaching
JA2+AI's wirecutter/explosive path ideas are useful low-level capabilities.

**Decision:** ADAPT under path/mission utility, not random use.

#### Local momentum
The "friend succeeded -> press" heuristic produced convincing emergent group attacks.

**Decision:** KEEP AS A LOCAL SIGNAL. Feed successful allied action into shared plan confidence/momentum rather than deleting it because we have a planner.

#### Behavior randomness
Historical randomness prevented perfect predictability.

**Decision:** KEEP CONTROLLED VARIATION. Candidate scores should not make identical states produce robotic identical behavior, but randomness must not override obviously dominant safety/mission choices.

---

## 8. Areas where JA2+AI should NOT be copied

### Sector-wide pseudo-hive mind
Never replace local fireteams with perfect sector knowledge.

### Exact or near-exact unseen targeting
Do not authorize attacks using hidden current positions. Last-known/noise evidence should produce uncertainty, not a live tracking solution.

### Hard danger exclusions
Avoid rules that create artillery crescents, darkness-search loops or permanent refusal to cross necessary danger.

### Hard performance cliff
Do not simply disable "smart AI" above a fixed combatant count. Degrade search depth gracefully.

### Mechanics-blind doctrine
Do not use suppression/smoke/artillery/grenades because "the AI should use them." Their utility must depend on the current mechanics.

### Historical tactical retreat semantics without state audit
Leaving the tactical sector touches campaign state and is strategically sensitive. Our current project rule freezes the strategic layer anyway.

---

## 9. Core methodology change produced by this research

The unified planner should not replace JA2+AI/Vengeance tactical behaviors. It should be a **coordination and arbitration layer over a mature execution library**.

Correct split:

### Planner owns
- what the local fireteam is trying to achieve;
- confidence in the plan;
- who is support/maneuver/flank/reserve;
- which axis is currently preferred;
- whether a route/set of actions is worth its risk;
- whether a prior approach failed;
- when to abandon/replan;
- how many soldiers should perform each job.

### Legacy/sevenfm execution owns or strongly informs
- how to flank through actual JA2 pathing;
- how to take cover;
- how to choose stance/fire mode;
- how to throw/use smoke;
- how to perform suppression;
- how to use grenades/launchers;
- how to cut/breach obstacles;
- how to navigate light/water/gas/corpses/doors;
- low-level AP/animation/item legality.

This avoids a classic AI rewrite mistake: replacing years of tuned tactical micro-behavior with a cleaner but less battle-tested planner.

---

## 10. Information-honesty rules after JA2+AI review

The "grandmaster with no cheats" rule survives the PhD and becomes more specific.

### Allowed
- personal current sight;
- legitimate recent personal knowledge;
- legitimate team/public radio knowledge;
- approximate heard bearing/location;
- local friendly state;
- observed casualties and failed routes;
- inference from known geometry;
- probabilistic anticipation of likely enemy behavior.

### Not allowed
- hidden current opponent grid;
- hidden AP/stance/equipment/health;
- exact sector strength of unseen opponents;
- moving old knowledge to the enemy's real position;
- exact attack solution based only on old noise;
- global instant knowledge propagation.

### Key principle

> The AI may make an exceptional inference from incomplete evidence; it may not receive exceptional evidence.

---

## 11. Fireteam doctrine after comparison

JA2+AI confirms that a fully centralized commander is unnecessary and potentially harmful.

Recommended model:

1. Local fireteam receives only plausible legal information.
2. Fireteam derives a shared coarse intent.
3. Soldiers receive complementary roles/reservations.
4. Individual soldiers still select/execute legal micro-actions.
5. Local success/failure changes plan confidence.
6. Surprise or material geometry change invalidates the plan.
7. The team may temporarily fragment under suppression/casualties/isolation.
8. Recovered soldiers can rejoin/reform around the local intent.

This creates coordination without a hive mind.

---

## 12. Bravery / self-preservation conclusions

JA2+AI history supports the current Vengeance doctrine: bravery must not be implemented as "ignore danger."

The best model is utility-based willingness to accept risk.

A soldier should cross danger when:
- the flank is decisive;
- friendly suppression meaningfully protects the move;
- a wounded teammate can realistically be saved;
- holding terrain matters;
- withdrawal requires covering exposure;
- the alternative is worse.

A soldier should reject danger when:
- there is no tactical payoff;
- the route is repeatedly failing;
- support is absent;
- the soldier is isolated and the objective does not justify sacrifice.

Danger penalties should be reduced by mission value, support, local superiority and time pressure—not switched off globally by "brave" status.

---

## 13. Performance conclusions

JA2+AI's >42-agent complex-AI cutoff is a critical warning.

For Vengeance, use adaptive reasoning budgets:

### Small battle
- full route exposure;
- more candidate positions;
- deeper plan comparison;
- richer geometry updates.

### Medium battle
- fewer candidate positions;
- reuse local geometry per fireteam/turn where legal;
- prioritize decisions for contact/frontline soldiers.

### Large battle
- preserve fireteam intent/roles;
- reduce per-soldier candidate search;
- cheap emergency rules first;
- stagger expensive reasoning;
- do not degrade information honesty;
- do not turn all soldiers into simple zerg AI.

Performance telemetry should include decision time by subsystem and combatant count.

---

## 14. Mandatory regression suite derived from JA2+AI history

### Information / unseen fire
1. Recently seen target moves behind wall.
2. Heard-only target at short/medium/long range.
3. Suppressed vs unsuppressed shooter.
4. Noise with/without LOS.
5. Empty last-known location becomes visibly checked.
6. Player moves 1–2 tiles after firing.
7. Multiple possible noise sources.

Pass condition: dangerous suppression remains possible but does not track hidden exact movement.

### Flanking
8. Open terrain.
9. Dense urban block.
10. Building adjacent to flank route.
11. Deep water.
12. map edge.
13. fence/concertina.
14. failed flank route.
15. flank succeeds and creates local momentum.

### Smoke
16. dangerous advance.
17. covered withdrawal.
18. wounded rescue.
19. smoke already present.
20. close range where smoke adds little.
21. strong attack exists instead.
22. smoke blocks friendly useful LOS.

### Danger/pathing
23. artillery/red-smoke zone with alternate path.
24. artillery zone with no safe path.
25. expanding danger radius.
26. fresh corpse lane.
27. bright night route.
28. gas/deep water.
29. no acceptable darker tile.

Pass condition: no loops or permanent paralysis; the AI can choose the least-bad route.

### Team coordination
30. support + one maneuver pair.
31. two possible flank sides.
32. heavy centre resistance.
33. first flanker succeeds.
34. first flanker is killed.
35. support element suppressed.
36. reserve needed.
37. wounded soldier rescue.
38. multiple unrelated contacts.

Pass condition: shared intent without perfect global knowledge.

### CQB
39. doorway.
40. window alternative.
41. room with unseen corner.
42. blocked doorway after new contact.
43. smoke/flashbang before entry.
44. abort entry after surprise.

### Scale
45. 8 combatants.
46. 20 combatants.
47. 40 combatants.
48. 60+ combatants.

Pass condition: graceful decision-quality degradation, no hard collapse.

### Deadlock regressions
49. door opens and reveals enemy.
50. no path to preferred flank.
51. no darker tile.
52. danger ring blocks objective.
53. action becomes illegal between planning and execution.

---

## 15. Feature classification matrix

| JA2+AI / sevenfm concept | Current disposition |
|---|---|
| Corrected flank geometry/AP scaling | ALREADY INHERITED / preserve |
| Night light avoidance | ALREADY INHERITED / benchmark |
| Sight-cover valuation | ALREADY INHERITED / preserve |
| Anti-crowding | ALREADY INHERITED / preserve |
| Randomized heard/noise location | ADAPT into belief uncertainty |
| Non-linear noise-memory strength | ADAPT from modern trunk |
| Blind suppression | ADAPT heavily; belief-bound |
| Safe suppression / friendly-fire checks | ADOPT/VERIFY |
| Smoke for dangerous movement | ALREADY INHERITED; planner should own purpose |
| Smoke for friend withdrawal | ADOPT/UNIFY |
| Retreat counter / temporary fallback | ADAPT into short-plan system |
| Successful-friend momentum | ADAPT into fireteam plan confidence |
| Local group-attack heuristics | PRESERVE as execution signal |
| Full sector group mastermind | REJECT |
| Hard artillery danger exclusion | REJECT; utility cost instead |
| Complex-AI hard cutoff >42 | REJECT; adaptive budget instead |
| Wirecutter/fence path breaching | ADAPT |
| Grenade/explosive obstacle breaching | ADAPT with safety/mission checks |
| Public-knowledge area attacks | ADAPT under legal confidence |
| Tactical sector exit retreat | HOLD / strategic freeze |
| Randomized decision variety | ADAPT as bounded variation |
| Behavior that reads exact hidden state | REJECT |
| Explicit fireteam planner | OURS; retain and validate |
| Contact-belief confidence/age/source | OURS; retain |
| Shared task reservations | OURS; retain |
| Short persistent plans | OURS; retain |
| Shared tactical geometry | OURS; retain |
| Fireteam-local setback memory | OURS; retain |

---

## 16. Main risk to our current AI

The greatest risk is no longer "not enough intelligence."

It is that our planner can accidentally **suppress the mature sevenfm behavior underneath it**.

A planner gate can turn:
- a good flank into HOLD;
- an opportunistic attack into waiting;
- useful suppression into over-analysis;
- local momentum into role rigidity.

The pre-Phase-2 B2 telemetry already gave a warning: many flank-planner events were rejected by competence/doctrine friction.

Therefore every higher-level planner change must measure:
- how often it vetoes a legal legacy action;
- why;
- whether the veto improved the outcome;
- whether the same soldier/team later achieved the intended plan.

A "smarter" architecture that produces fewer convincing tactical actions is a regression.

---

## 17. Main research conclusion

JA2+AI remains the strongest historical external tactical-AI benchmark because it combines:
- deep low-level JA2 knowledge;
- many years of iteration;
- practical player feedback;
- aggressive experimentation;
- numerous regressions and subsequent corrections.

However, its author explicitly described the core architecture as individual rather than group-planned.

Our Vengeance AI therefore has a plausible route beyond JA2+AI:

> Preserve sevenfm's battle-tested individual/execution intelligence, place it behind a strict legal-information contract, and add a bounded fireteam planner that coordinates complementary actions without creating a sector-wide hive mind.

That is the current preferred methodology.

It is not yet proven superior in gameplay. Superiority must be demonstrated through the regression/benchmark suite and real campaign battles.

---

## 18. Required benchmark standard

Before claiming the Vengeance AI is "better than JA2+AI", test at least:

- day assault;
- night assault;
- open terrain;
- dense urban;
- roof defense;
- CQB;
- mine/obstacle route;
- smoke advance;
- smoke withdrawal/rescue;
- blind suppression;
- lost contact/search;
- failed frontal attack -> flank;
- multi-angle contact;
- wounded/casualty cascade;
- 30–50 soldier battle;
- high-density >50 combatant battle.

Evaluation axes:
- information honesty;
- tactical effectiveness;
- coordination;
- self-preservation;
- aggression/initiative;
- unpredictability;
- deadlocks;
- CPU/turn time;
- player perception of fairness;
- repeatability across seeds.

No "best AI" claim until this evidence exists.

---

## 19. Source map

Primary / high-value sources consulted:

- Modern 1.13 tactical AI documentation:
  https://1dot13.github.io/documentation/playing/features/tactical-ai/
- Experimental Project 7 / JA2+AI, Bear's Pit:
  https://thepit.ja-galaxy-forum.com/index.php?t=msg&th=21864
- Early flanking diagnosis and r8001 discussion:
  https://www.thepit.ja-galaxy-forum.com/index.php?goto=340562&t=msg
- 2017 smoke / RANDOMIZE_NOISE_LOCATION work:
  https://thepit.ja-galaxy-forum.com/index.php?goto=351756&t=msg&th=21864
- Blind suppression player feedback and tuning:
  https://www.thepit.ja-galaxy-forum.com/index.php?goto=355930&t=msg
- 2019 individual-vs-group AI explanation / r1068 emergent group attack:
  https://thepit.ja-galaxy-forum.com/index.php?prevloaded=1&start=1000&t=msg&th=21864
- 2019 artillery avoidance/deadlock feedback:
  https://thepit.ja-galaxy-forum.com/index.php?prevloaded=1&start=1000&t=msg&th=21864
- Trunk AI and improvements:
  https://thepit.ja-galaxy-forum.com/index.php?t=msg&th=24289
- 2020 complex-AI performance cutoff:
  https://thepit.ja-galaxy-forum.com/index.php?goto=361458&t=msg&th=21864
- 7609/+AI ecosystem compatibility summary:
  https://thepit.ja-galaxy-forum.com/index.php?goto=361643&t=msg
- Vengeance thread documenting sevenfm as VR coder:
  https://www.thepit.ja-galaxy-forum.com/index.php?goto=348249&t=msg&th=23279

Code audited:
- VengeanceReloaded/vr_source public source
- 1dot13/source current source
- mattedinburgh/vr_source active tactical-AI workstream

---

## 20. Stream rule created by this dossier

From now on, any meaningful Tactical AI change must answer:

1. Is there a JA2+AI/sevenfm precedent?
2. Was it later changed, disabled or reverted?
3. What did players report in real battles?
4. Is the behavior already inherited in Vengeance?
5. Does modern 1.13 implement a later variation?
6. Does our planner already supersede it?
7. Could the new planner suppress a mature legacy behavior?
8. Does the change respect the legal-information contract?
9. What historical regression case must be rerun?
10. What telemetry proves the change improved behavior?

Research completion alone does not increase implementation completion percentage.
