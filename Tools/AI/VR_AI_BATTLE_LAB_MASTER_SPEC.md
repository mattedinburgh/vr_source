# Vengeance AI Battle Lab (VR-AIBL) — Master Research & Design Specification

**Status:** PARKED / DESIGN ONLY  
**Implementation status:** No engine code, runner, scenarios, or telemetry changes have been implemented from this specification.  
**Canonical AI branch:** install/all-2026-09-12  
**Purpose:** Preserve a research-grade design for a future unattended Vengeance tactical AI evaluation environment.

---

## 1. Executive conclusion

The correct project is not an autoresolve simulator and not a second simplified combat engine. It is a **controlled experimental harness around the real Vengeance tactical engine and the real tactical AI**.

The lab should execute genuine TacticalAI decisions, pathfinding, line-of-sight, NCTH, interrupts, suppression, cover, smoke, grenades, wounds, morale, retreat, medical behaviour and other engine mechanics. It should run battles without human input, repeat them under controlled random seeds, tag all telemetry with experiment identity, and compare AI versions scientifically.

The lab should serve five distinct purposes:

1. **Behavioural regression testing** — detect when an AI change improves one behaviour but damages another.
2. **Comparative evaluation** — compare candidate AI versions against frozen historical baselines under matched conditions.
3. **Generalisation testing** — verify that improvements hold across maps, force ratios, visibility regimes, equipment, and tactical situations rather than one hand-tuned scenario.
4. **Engine stress testing** — surface rare crashes, hangs, pathological loops and performance regressions through large numbers of autonomous battles.
5. **Evidence generation for model-assisted development** — convert BlackBox/VRAnalytics evidence into structured conclusions and hypotheses for future AI development.

The lab must remain physically and logically separated from the player's normal Vengeance installation so it can run unattended on the same PC without contaminating saves, configuration, BlackBox logs, or gameplay.

---

## 2. Research basis

### 2.1 Direct Jagged Alliance 2 precedent

Manuel Ladebeck's 2008 diploma thesis, "Applying Dynamic Scripting to Jagged Alliance 2", modified a JA2 v1.13 build specifically for automatic AI benchmarking.

Key design choices from that work:

- two AI-controlled teams fought each other;
- because JA2 required a player mercenary, a player unit was placed at the edge of the sector where it would not affect perception or combat;
- the player team's turn was automatically skipped;
- after combat ended, the sector was reset;
- experiments were executed in batches;
- the author emphasized that individual battles were not meaningful because JA2 combat is strongly randomized;
- the evaluation used 10 batches of 100 matches — 1,000 runs per scenario;
- an equal-strength, equal-equipment scenario was used so performance differences could be attributed to tactical behaviour;
- the principal runtime problem was that graphical presentation and fixed animation delays could not be disabled, causing a scenario to take roughly 48 hours.

This is unusually relevant: the fundamental idea has already been shown to work in the JA2 engine family. VR-AIBL should preserve the useful structure while replacing the weak parts with deterministic experiment identity, modern telemetry, paired comparisons, held-out scenarios, versioned baselines and faster execution.

Source:
Manuel Ladebeck, Applying Dynamic Scripting to Jagged Alliance 2, 2008.
https://doczz.net/doc/3780831/applying-dynamic-scripting-to-jagged-alliance-2

### 2.2 Multi-agent self-play lessons

OpenAI Five trained mostly against its current policy but deliberately played a fraction of games against historical versions to reduce strategy collapse. AlphaStar went further with a league containing main agents and exploiters designed to expose weaknesses. These systems demonstrate an important evaluation lesson even though VR-AIBL is not intended to train a neural network:

**Never judge a candidate only against itself or only against the immediately previous version.**

A Vengeance AI can become locally stronger while becoming brittle against strategies that an older version handled well. The evaluation population should therefore include frozen historical AI baselines and specialist adversarial configurations.

Sources:
https://openai.com/index/openai-five/
https://cdn.openai.com/dota-2.pdf
https://deepmind.google/blog/alphastar-grandmaster-level-in-starcraft-ii-using-multi-agent-reinforcement-learning/

### 2.3 Generalisation benchmarks

DeepMind's Melting Pot and XLand evaluation work separates development environments from held-out evaluation tasks. GVGAI research similarly evaluates agents on unseen levels.

The transferable principle is:

**A battle-lab score is only credible if some scenarios are never used to tune the AI.**

VR-AIBL should maintain:
- visible development scenarios;
- locked validation scenarios;
- a small final benchmark set that is changed rarely.

Sources:
https://deepmind.google/blog/melting-pot-an-evaluation-suite-for-multi-agent-reinforcement-learning
https://deepmind.google/blog/generally-capable-agents-emerge-from-open-ended-play
https://arxiv.org/abs/2005.11247

### 2.4 Reproducible multi-agent environments

PettingZoo explicitly tests that seeding produces deterministic environment behaviour, including reset-and-repeat determinism. That is a useful standard for VR-AIBL.

A future lab should have a dedicated determinism compliance test:
- same build + same data + same scenario + same seed -> same initial state;
- repeated reset with the same seed -> same initial state;
- where full tactical determinism is technically possible, the event sequence should also reproduce;
- if full determinism is impossible because of legacy timing or OS state, every nondeterministic source should be identified and classified.

Source:
https://pettingzoo.farama.org/content/environment_tests/

### 2.5 Simulation experiment design

The Winter Simulation Conference literature strongly recommends independent replications for stochastic simulation output and warns that a single simulation run does not provide an answer. Common Random Numbers (CRN) are a standard variance-reduction method for comparing alternative configurations: use the same random-number stream for corresponding runs so differences are more attributable to the configuration rather than luck.

For Vengeance this translates naturally into **paired seeds and mirrored sides**.

Sources:
https://www.informs-sim.org/wsc23papers/124.pdf
https://informs-sim.org/wsc09papers/232.pdf
https://informs-sim.org/wsc20papers/134.pdf

### 2.6 Ranking populations rather than chasing one win rate

TrueSkill represents skill with both an estimated level and uncertainty rather than a raw win percentage. Population-based evaluation literature also highlights cyclic matchups where A beats B, B beats C, and C beats A.

VR-AIBL should therefore retain the full pairwise matchup matrix. A single scalar rating can be useful for navigation, but it must never replace scenario-level and opponent-level evidence.

Sources:
https://www.microsoft.com/en-us/research/publication/trueskilltm-a-bayesian-skill-rating-system/
https://deepmind.google/research/publications/22497/

### 2.7 Reproducer-first failure handling

Modern fuzzing systems treat a reproducible testcase as the core artifact for debugging. VR-AIBL should apply the same concept to tactical failures: preserve the exact scenario, seed, build identities and telemetry required to rerun an anomaly.

Sources:
https://google.github.io/oss-fuzz/advanced-topics/reproducing/
https://google.github.io/oss-fuzz/advanced-topics/ideal-integration/

---

## 3. Non-goals

VR-AIBL is **not**:

- a replacement for real campaign playtesting;
- the strategic autoresolve system;
- a simplified mathematical combat model;
- a new tactical AI implementation;
- a reinforcement-learning training environment by default;
- permission for an automated model to rewrite and merge AI code;
- a single overall "AI score";
- a performance benchmark that ignores behaviour;
- a reason to create permanent divergent AI branches.

The lab evaluates the canonical tactical AI. It must not become a second source of AI behaviour.

---

## 4. Scientific principles

### 4.1 Measure differences, not anecdotes

One dramatic battle is useful for discovering a behaviour but weak evidence for estimating its prevalence.

Every claim should answer:
- how often?
- under what scenarios?
- relative to which baseline?
- with what uncertainty?
- with what side/seed controls?
- what changed in secondary and guardrail metrics?

### 4.2 Separate strength from quality

Winning is important but insufficient.

A candidate may improve win rate by:
- camping indefinitely;
- exploiting a bug;
- gaining hidden information;
- grenade spamming;
- refusing reasonable rescue behaviour;
- taking extremely slow turns;
- concentrating all soldiers into one exploitative tactic.

The lab must therefore distinguish:
- **Outcome strength**
- **Tactical competence**
- **Fairness / information legality**
- **Human-believable behaviour**
- **Tactical diversity**
- **Robustness**
- **Performance / runtime cost**
- **Engine stability**

### 4.3 Treat the scenario as the statistical unit of generalisation

Thousands of seeds on one map estimate performance on that map very well but say little about general tactical quality.

Coverage across scenario families matters more than merely increasing repetitions forever.

### 4.4 Freeze experiment definitions

Once a benchmark suite is used for comparison, scenario definitions must be versioned. Changing spawn locations, equipment or time-of-day creates a new benchmark version.

### 4.5 No hidden-information leakage

The existing Vengeance Tactical AI knowledge contract remains binding. Battle Lab instrumentation may inspect omniscient state for **evaluation**, but AI decision functions must not receive that state.

This separation is crucial:
- evaluator may know the true enemy position;
- AI may only know information allowed by the game's personal/public knowledge systems.

---

## 5. Proposed architecture

### Layer A — Isolated runtime

A dedicated game copy or runtime directory:
- its own executable;
- its own Data / profiles / configuration overlay;
- its own saves;
- its own BlackBox / VRAnalytics output;
- its own temporary files;
- no mutation of the player's active installation.

Conceptual layout:

~~~
AI_BattleLab/
  runtime/
  scenarios/
  baselines/
  experiments/
  telemetry/
  reports/
  reproducers/
  cache/
~~~

### Layer B — Scenario loader

Loads a versioned scenario manifest and constructs the battle.

Responsibilities:
- map / sector;
- time and environment;
- team composition;
- classes, stats and traits;
- weapons, ammo, armour and consumables;
- starting locations and facing;
- AI orders / attitudes where relevant;
- initial knowledge state where relevant;
- optional scripted damage/suppression/wounded state;
- victory/termination criteria;
- maximum turns / actions / runtime.

### Layer C — Tactical battle runner

Runs the genuine tactical engine without human decisions.

State machine:

~~~
prepare experiment
-> initialize deterministic streams
-> load scenario
-> validate initial-state fingerprint
-> start tactical engagement
-> execute AI teams
-> monitor health/timeouts
-> determine terminal state
-> close telemetry
-> validate output
-> reset process or world
-> next replication
~~~

### Layer D — VRAnalytics / BlackBox integration

Preserve the current architecture:
- **VRAnalytics** = structured behavioural and experimental telemetry;
- **BlackBox recorder** = crash/hang forensic safety net.

Do not create a third tactical decision stream.

Every lab event should inherit:
- experiment_id;
- suite_id;
- scenario_id;
- scenario_version;
- replication_id;
- pair_id;
- seed;
- side_assignment;
- engine SHA;
- AI source SHA;
- game-data SHA;
- configuration fingerprint;
- baseline/candidate identities.

### Layer E — Experiment store

Raw JSONL should not be the analytical database.

Derived tables should conceptually include:

**experiment**
- identity and build metadata

**battle**
- scenario, seed, sides, outcome, duration

**actor**
- starting/final state and role

**decision**
- state -> candidate set -> selected action -> result

**combat_event**
- shots, hits, damage, explosions, suppression

**movement_event**
- movement, cover transitions, exposure

**medical_event**
- downed state, rescue, aid, stabilization

**formation_snapshot**
- cohesion, living/combat-ready counts, morale/stress

**anomaly**
- invariant violation, crash, hang, pathological loop, suspicious knowledge use

### Layer F — Analyzer

Produces:
- paired candidate-vs-baseline deltas;
- confidence intervals;
- scenario-family breakdowns;
- historical matchup matrices;
- regression alarms;
- anomaly clusters;
- performance-cost analysis;
- reproducible testcase manifests.

### Layer G — model-assisted interpretation

The model receives **aggregated evidence plus selected traces**, not an uncontrolled raw firehose.

Ideal model input:
1. experiment hypothesis;
2. code/build identities;
3. primary metric result;
4. scenario-family breakdown;
5. guardrail regressions;
6. anomalous trace exemplars;
7. selected BlackBox decision chains;
8. previous comparable experiments.

Output:
- what changed;
- confidence level;
- likely causal mechanisms;
- files/functions to inspect;
- proposed next experiment;
- optional candidate patch design.

The model should not automatically integrate source changes.

---

## 6. Runtime modes

### 6.1 VISUAL / FORENSIC

Full or near-full rendering.
Purpose:
- watch a reproducer;
- inspect strange behaviour;
- compare telemetry to visible action.

This is the behavioural truth reference.

### 6.2 FAST

Same tactical semantics but:
- shortened/disabled cosmetic delays;
- sound disabled;
- UI updates minimized;
- expensive non-semantic presentation skipped where safe.

This should be the first acceleration target.

### 6.3 HEADLESS / ULTRA

Long-term mode.
Rendering and presentation removed as far as safely possible.

This mode should not be trusted until differential tests demonstrate that VISUAL and HEADLESS produce equivalent tactical outcomes for controlled seeds or equivalent distributions where exact equivalence is impossible.

**Do not start implementation with HEADLESS.**

The 2008 JA2 thesis demonstrates why acceleration is desirable, but semantic drift would be worse than slow tests.

---

## 7. Randomness and reproducibility contract

Randomness is not one thing. The design should inventory every random stream, for example:

- tactical decision randomness;
- weapon dispersion;
- hit resolution;
- damage;
- interrupts;
- suppression effects;
- grenade scatter;
- AI variation;
- environmental randomness;
- initialization/spawn variation.

Preferred architecture:
- master experiment seed;
- deterministic derived substreams by purpose.

Example:

~~~
master seed
  -> scenario RNG
  -> combat RNG
  -> AI variation RNG
  -> cosmetic RNG
~~~

Why separate streams:
- cosmetic changes should not alter combat;
- adding one new AI random call should not shift every later bullet roll;
- CRN pairing becomes more meaningful;
- failures become easier to reproduce.

Every experiment should record seed-stream version, not only seed value.

---

## 8. Determinism compliance suite

Before trusting results, VR-AIBL should test itself.

### Test D1 — Initial-state determinism
Two fresh processes, same manifest and seed -> identical initial-state hash.

### Test D2 — Reset determinism
Same process: run, reset, recreate same seed -> identical initial-state hash.

### Test D3 — Tactical replay determinism
Where technically achievable: identical decision/action/event hashes.

### Test D4 — Telemetry non-interference
Analytics enabled vs analytics disabled should produce the same tactical event outcome for a deterministic seed.

### Test D5 — Presentation non-interference
VISUAL vs FAST should produce the same tactical semantics for deterministic seeds.

### Test D6 — Isolation
Running the lab may not modify files in the player's normal game instance.

A failed determinism test invalidates fine-grained paired conclusions until understood.

---

## 9. Scenario taxonomy

Scenarios should be tagged across several independent axes rather than treated as a flat list.

### Terrain / geometry
- open
- forest/vegetation
- mixed rural
- dense urban
- building interior
- industrial
- elevation
- chokepoint
- multiple structures
- water/shoreline

### Visibility
- daylight
- dusk
- night
- artificial light
- smoke
- mixed LOS

### Force ratio
- 1:1
- 3:2
- 2:1
- 1:2
- last survivor / remnant

### Equipment
- pistols
- SMGs
- rifles
- marksman/sniper
- shotguns
- mixed arms
- grenades
- smoke
- low ammunition
- wounded/low supplies

### Tactical problem
- meeting engagement
- assault
- defence
- building entry
- break contact
- casualty rescue
- suppression recovery
- flank
- local reinforcement
- leader loss
- militia coordination
- last-survivor behaviour
- retreat/escape

### Skill class
- low
- regular
- elite
- mixed

### Information state
- no contact
- heard contact
- stale contact
- current sight
- lost sight
- conflicting public/personal knowledge

A good suite samples combinations across these dimensions.

---

## 10. Scenario sets

### 10.1 Development set

Visible and repeatedly inspected.
Used to:
- debug new behaviours;
- tune thresholds;
- understand failures.

### 10.2 Validation set

Not normally used for tuning.
Run after a candidate appears successful.

### 10.3 Locked benchmark set

Rarely inspected and changed only deliberately.
Purpose:
- protect against Battle-Lab overfitting;
- provide a stable historical trend.

### 10.4 Campaign realism set

Actual Vengeance sectors and plausible campaign compositions.

Purpose:
- ensure synthetic competence translates back to the real game.

### 10.5 Reproducer corpus

Every important bug or pathological behaviour becomes a permanent small scenario/replay when feasible.

This follows the same philosophy as a fuzzer seed/regression corpus: fixed inputs that once exposed a problem remain in the suite forever.

---

## 11. Experimental designs

### 11.1 Mirror self-play

Same AI on both sides.
Useful for:
- detecting map/side bias;
- system balance;
- behavioural distribution;
- symmetry checks.

It is **not** sufficient for proving AI improvement.

### 11.2 Candidate vs baseline

Primary comparative design.

Candidate and baseline run under the same scenario and random-seed pairing.

### 11.3 Mirrored pair

For each seed:

~~~
Match A:
candidate -> side A
baseline  -> side B

Match B:
baseline  -> side A
candidate -> side B
~~~

This controls for map side, spawn layout and some turn-order effects.

### 11.4 Common Random Numbers

Use corresponding random streams for candidate and baseline where valid.

The goal is not to make matches identical. The goal is to make environmental luck correlated enough that the **difference between versions** has lower variance.

### 11.5 Ablation tests

Disable one new AI component while leaving the rest unchanged.

Examples:
- candidate with new retreat logic;
- same candidate but retreat logic disabled.

Ablation is often more causally informative than candidate vs an old monolithic build.

### 11.6 Factorial experiments

For interactions such as:
- AI version × night/day;
- AI version × weapon class;
- AI version × force ratio.

Do not use factorial designs everywhere, but they are useful when a suspected interaction matters.

### 11.7 Metamorphic tests

Check invariants under transformations.

Examples:
- swapping east/west sides should not systematically benefit one AI version;
- renaming actor IDs should not change behaviour;
- cosmetic asset changes should not change outcomes;
- telemetry on/off should not change tactical results;
- equivalent symmetric spawn rotations should produce comparable distributions.

### 11.8 Soak / stress tests

Very long unattended runs for:
- crashes;
- hangs;
- leaks;
- pathological AI loops;
- state contamination between battles.

These are engineering tests, not AI-strength tests.

---

## 12. Historical opponent league

Freeze milestone AI versions.

Example identity:
- BASELINE_2026_09_12_CONSOLIDATED
- BASELINE_POST_MORALE
- BASELINE_POST_FIRETEAM
- BASELINE_POST_CQB
- candidate current

Maintain pairwise matchup results by scenario family.

Why:
- detect strategic cycles;
- detect forgotten capabilities;
- detect brittle specialization.

A rating such as TrueSkill can be calculated for convenience because it tracks uncertainty, but the authoritative evidence remains:
- pairwise matrix;
- scenario-family matrix;
- behavioural guardrails.

---

## 13. Metrics framework

### 13.1 Primary outcome metrics
- win / loss / retreat / timeout;
- surviving combat-ready soldiers;
- casualty differential;
- incapacitation differential;
- battle duration / tactical turns.

### 13.2 Fire effectiveness
- shots;
- bursts;
- hit rate;
- damage per shot;
- damage per AP;
- kill/incapacitation conversion;
- range distribution;
- aimed vs low-aim shots;
- ammunition consumed.

### 13.3 Suppression
- suppression inflicted;
- suppression received;
- actions lost to suppression;
- useful suppression followed by movement;
- suppression without tactical follow-up.

### 13.4 Position and cover
- turns exposed;
- cover transitions;
- movement into worse exposure;
- time spent in useful weapon range;
- overextension;
- retreat quality;
- route safety.

### 13.5 Coordination
- local numerical support;
- fireteam cohesion;
- unsupported advances;
- concentration/crowding;
- leader proximity where relevant;
- response to local contact;
- reinforcement timing.

### 13.6 CQB
- doorway congestion;
- room-entry success;
- piecemeal entry;
- close-range weapon suitability;
- building assault casualties;
- building defence efficiency.

### 13.7 Grenades / smoke
- throws attempted;
- legal/illegal;
- friendly-fire risk;
- target offset;
- actual landing error;
- effective damage/suppression;
- wasted throws;
- smoke value around casualty/withdrawal/assault.

### 13.8 Medical
- time to aid;
- downed survival;
- rescue attempts;
- rescue success;
- suicidal rescue;
- medic casualties;
- smoke/cover protection during rescue.

### 13.9 Morale / survival behaviour
- tactical fallback frequency;
- disengagement frequency;
- successful break contact;
- rout;
- rally;
- surrender/escape if applicable;
- last-survivor charges;
- re-engagement after stabilization.

### 13.10 Information legality / anti-cheat
- target knowledge state at decision;
- target knowledge age;
- fresh LOS at fire;
- use of known vs true position;
- actions suspiciously correlated with unseen real positions;
- invalid dependence on hidden health/equipment.

Zero tolerance for proven information-contract violations.

### 13.11 Action quality / planner health
- candidate count;
- eligible/rejected candidates;
- selected utility/score;
- selected action completion;
- superseded decisions;
- rejected execution;
- no-op turns;
- repeated oscillation;
- AP left unused without reason.

### 13.12 Tactical diversity
Track distributions rather than reward a fixed quota:
- attack;
- hold;
- flank;
- fallback;
- suppress;
- smoke;
- grenade;
- rescue;
- regroup.

Goal: detect collapse into one dominant behaviour.

### 13.13 Performance
- AI decision wall time;
- p50/p95/p99 decision latency;
- pathfinding calls;
- pathfinding time;
- allocations/memory;
- handles/GDI/USER objects;
- battles/hour;
- telemetry overhead.

---

## 14. Guardrail metrics

A candidate should not be promoted because of one primary gain if it violates a hard guardrail.

Suggested hard guardrails:
- no increase in knowledge-contract violations;
- no new reproducible crash/hang;
- no material increase in friendly grenade incidents;
- no severe increase in no-op/pathological-loop rate;
- no unacceptable p99 AI latency regression;
- no large regression on locked holdout scenarios.

Suggested soft guardrails:
- medic survival;
- tactical diversity;
- ammo efficiency;
- unnecessary exposure;
- battle duration.

---

## 15. Statistical methodology

### 15.1 Independent replication

Each seed/pair is a replication. Do not treat individual shots or actions inside one battle as independent samples for battle-level outcome inference.

### 15.2 Paired analysis

For candidate-vs-baseline, analyze within-pair differences whenever the pairing remains valid.

Examples:
- survivor difference;
- damage difference;
- AP efficiency difference.

This is the natural benefit of Common Random Numbers.

### 15.3 Confidence intervals

Every reported primary result should carry uncertainty.

For proportions such as win rate, prefer a Wilson-type interval over a naive normal approximation.

Reference:
NIST confidence intervals for proportions:
https://www.itl.nist.gov/div898/handbook/prc/section2/prc241.htm

### 15.4 Scenario-stratified reporting

Never report only "candidate +4% overall".

Also report:
- each scenario family;
- worst decile/percentile;
- number of families improved/regressed;
- locked holdout separately.

### 15.5 Hierarchical thinking

Seeds are nested inside scenarios; scenarios are nested inside families.

A mature analyzer can use hierarchical bootstrap or a hierarchical statistical model so thousands of repetitions from one scenario do not dominate ten repetitions across many scenarios.

### 15.6 Practical significance

Define a **minimum practically important difference** before large experiments.

Examples:
- +0.5 percentage point win rate may be irrelevant;
- -20% suicidal last-survivor advances may be highly valuable even with no win-rate gain.

Statistical significance alone is not promotion criteria.

### 15.7 Sequential stopping

Do not blindly run 10,000 battles if 300 already establish that a candidate is disastrously worse.

Allow stopping rules for:
- clear harm;
- clear equivalence within a defined tolerance;
- desired confidence-interval precision.

But the rule must be specified before inspecting results to avoid informal "stop when it looks good" bias.

### 15.8 Multiple metrics

Because many metrics are inspected, treat primary hypotheses separately from exploratory findings.

Recommended structure:
- 1–3 predeclared primary metrics;
- explicit hard guardrails;
- all other metrics exploratory/diagnostic.

This prevents accidental metric shopping.

---

## 16. Promotion policy

A future candidate AI should pass gates, not one score.

### Gate 0 — Build/integrity
- builds;
- AI integrity checks;
- no duplicated orchestration path.

### Gate 1 — Micro behaviour
- specialist scenarios for changed subsystem.

### Gate 2 — Tactical regression suite
- broad development suite.

### Gate 3 — Historical league
- no serious forgotten-strategy regression.

### Gate 4 — Locked validation
- passes unseen scenarios.

### Gate 5 — Performance/stability
- no unacceptable slowdown/crash/hang.

### Gate 6 — Human campaign play
- behaviour still feels coherent, fair and enjoyable.

Only then integrate/promote.

---

## 17. Telemetry architecture

Current repository architecture already states:
- VRAnalytics owns the structured tactical/strategic decision stream;
- BlackBox is the forensic recorder;
- tactical planner adapters feed VRAnalytics.

VR-AIBL should extend those systems, not fork them.

### Experiment metadata event

At process/session start:
- lab schema version;
- experiment ID;
- suite/scenario IDs;
- seed;
- build SHA;
- game-data SHA;
- config hashes;
- candidate/baseline labels;
- runtime mode.

### Battle start/end

Existing battle lifecycle telemetry should gain correlation with experiment metadata.

### Decision telemetry

Keep current decision chain:
- assessment/state;
- candidates;
- rejection;
- selection;
- completion/outcome.

### Omniscient evaluator telemetry

If added, it must be clearly marked as **evaluation-only** and never callable from tactical decision code.

Example uses:
- actual unseen opponent location for post-hoc anti-cheat detection;
- true cover/exposure benchmark;
- ground-truth projectile result.

---

## 18. Data volume strategy

Do not store everything forever.

### Tier 1 — Summary forever
Keep compact battle and experiment summaries.

### Tier 2 — Structured detailed sample
Keep representative decision telemetry for a rolling window.

### Tier 3 — Full forensic retention
Keep complete raw telemetry for:
- crashes;
- hangs;
- invariant violations;
- fairness violations;
- extreme outliers;
- newly discovered behavioural regressions;
- manually pinned experiments.

### Reproducer package

A reproducer should contain enough to reconstruct:
- manifest;
- seed/substream version;
- build and data hashes;
- configuration;
- expected anomaly;
- relevant telemetry slice.

---

## 19. Model-feedback protocol

The model should not learn conclusions from an unstructured dump.

Each experiment report should be summarized into a stable machine-readable "evidence packet":

- hypothesis;
- candidate change identifier;
- baseline;
- suite;
- number of scenario-seed pairs;
- primary effect + uncertainty;
- guardrail results;
- top regressions;
- top improvements;
- per-family matrix;
- performance cost;
- anomaly IDs;
- selected reproducer IDs;
- previous related experiments.

The model should classify conclusions as:
- **confirmed**
- **probable**
- **inconclusive**
- **disproved**
- **regression**
- **instrumentation problem**

A recommendation should state:
- evidence;
- likely mechanism;
- alternative explanations;
- proposed next experiment;
- proposed code area only after evidence supports it.

---

## 20. Why the model must not optimize the score directly

If a model sees a scalar objective and repeatedly edits heuristics against it, Goodhart's law becomes an immediate risk.

Examples:
- reward cover usage -> soldiers never leave cover;
- reward survival -> soldiers avoid necessary attacks;
- reward hit rate -> soldiers refuse suppressive fire;
- reward win rate -> exploitable camping dominates;
- reward low friendly fire -> grenades disappear entirely.

Therefore:
- no single training reward;
- multi-dimensional evaluation;
- hard constraints;
- locked scenarios;
- human campaign validation.

---

## 21. Failure and anomaly taxonomy

Every anomaly should receive a stable type.

Suggested classes:
- CRASH
- HANG
- TIMEOUT
- MEMORY_GROWTH
- INVALID_ACTION
- ACTION_OSCILLATION
- NO_OP_LOOP
- PATHFINDING_LOOP
- KNOWLEDGE_VIOLATION
- FRIENDLY_FIRE
- GRENADE_SUICIDE
- SUICIDAL_ADVANCE
- MEDIC_SUICIDE
- CQB_DOORWAY_STALL
- RETREAT_REVERSAL
- FORMATION_COLLAPSE
- IMPOSSIBLE_STATE
- TELEMETRY_CORRELATION_FAILURE
- DETERMINISM_FAILURE

Each anomaly should be countable and, ideally, reproducible.

---

## 22. Resource isolation on the user's PC

Future runner requirements:
- below-normal process priority by default;
- configurable CPU affinity;
- configurable maximum concurrent instances;
- configurable memory ceiling;
- sound off;
- no focus stealing;
- no main-monitor UI requirement;
- optional pause when the playable Vengeance process starts;
- bounded disk use;
- graceful checkpoint/stop.

Parallel execution should only be introduced after proving that multiple instances do not share mutable files or global resources.

---

## 23. Process isolation vs in-process reset

The 2008 JA2 work reset the sector between matches. For modern VR-AIBL, two reset strategies should be supported conceptually:

### In-process reset
Pros:
- fast.

Risks:
- hidden global state;
- allocator/state contamination;
- stale AI caches;
- legacy subsystem residue.

### Fresh-process replication
Pros:
- strongest isolation and reproducibility.

Cons:
- startup overhead.

Recommended design:
- micro suites may use validated in-process reset;
- canonical benchmark should periodically use fresh-process runs;
- if any state contamination is detected, fresh-process becomes authoritative.

---

## 24. Validation of the test harness itself

The lab can produce precise nonsense if the harness is wrong.

Before trusting AI conclusions, validate:

1. Equal-AI symmetric scenarios converge near expected symmetry after side mirroring.
2. A deliberately crippled AI version loses clearly.
3. A known cheating AI is detected by fairness instrumentation.
4. Deliberately slowed AI is caught by performance guardrails.
5. Injected crash/hang is preserved by BlackBox and reproducer packaging.
6. Same-seed replay behaves as specified.
7. Held-out scenarios remain inaccessible to tuning workflow.
8. Scenario version/hash changes invalidate direct comparisons unless explicitly rebased.

---

## 25. Initial benchmark suite philosophy

Do not begin with 100 scenarios.

Start with a small "golden" set that is understood deeply:

- symmetric open firefight;
- mixed-cover firefight;
- building assault;
- building defence;
- night contact;
- suppression-heavy fight;
- casualty/medic scenario;
- disadvantaged withdrawal;
- last-survivor/remnant scenario;
- grenade/smoke scenario;
- reinforcement/local-response scenario;
- realistic Vengeance sector.

After the harness is trustworthy, expand systematically.

---

## 26. Suggested suite scale after maturity

Illustrative, not fixed:

### PR / feature smoke
- 10–20 specialist scenarios
- 10–30 paired seeds
- fast

### Daily regression
- 30–50 scenarios
- 30–100 paired seeds

### Milestone evaluation
- 75–150 scenarios
- 100+ paired seeds
- mirrored sides
- historical opponent subset
- locked validation

### Long soak
- duration-based rather than fixed sample count
- focuses on stability and rare anomalies

Sample size should ultimately be driven by required confidence precision and effect size, not round numbers.

---

## 27. Candidate evaluation report

A useful report should begin with the answer, not raw data.

Example structure:

### Decision
PASS / HOLD / FAIL

### Primary hypothesis
"New disengagement state reduces suicidal advances under catastrophic odds."

### Evidence
- suicidal advance rate: candidate vs baseline;
- confidence interval;
- affected scenario families;
- holdout result.

### Guardrails
- win rate;
- retreat survival;
- decision latency;
- knowledge violations;
- friendly fire.

### Regressions
Ranked by severity.

### Mechanistic traces
3–10 representative decision chains.

### Recommended next action
- accept;
- tune;
- revert;
- instrument further;
- run targeted experiment.

---

## 28. Repository placement when implementation resumes

Keep it inside the canonical project, conceptually:

~~~
Tools/AI/BattleLab/
  README.md
  schemas/
  scenarios/
  analysis/
  runner/
  tests/
  baselines/
~~~

Engine integration should be minimal and clearly named.

Do not create a permanent "AI Battle Lab AI" branch. Feature work may use short-lived branches, but final infrastructure returns to install/all-2026-09-12 in accordance with the current AI consolidation policy.

---

## 29. Implementation order when unparked

### Phase 0 — contract
- schema;
- experiment identity;
- determinism inventory;
- primary metrics;
- scenario manifest.

### Phase 1 — one unattended battle
- use genuine tactical AI;
- no human input;
- terminal-state detection.

### Phase 2 — reset and repeat
- verified isolation;
- 100 replications.

### Phase 3 — seed control
- deterministic initialization;
- seed metadata;
- reproducibility tests.

### Phase 4 — experiment telemetry
- VRAnalytics metadata;
- battle result summaries;
- anomaly tagging.

### Phase 5 — paired comparator
- candidate vs baseline;
- mirrored sides;
- CRN where valid.

### Phase 6 — analyzer
- confidence intervals;
- scenario breakdown;
- regression report.

### Phase 7 — scenario library
- specialist + broad suites;
- versioning.

### Phase 8 — holdout discipline
- locked validation suite.

### Phase 9 — historical league
- opponent pool;
- pairwise matrix;
- optional TrueSkill navigation rating.

### Phase 10 — acceleration
- FAST mode;
- differential equivalence tests.

### Phase 11 — headless experiment
- only after FAST is validated.

### Phase 12 — background operation
- CPU/priority/disk controls.

### Phase 13 — model evidence packets
- automated summarization and diagnosis support.

### Phase 14 — soak/fuzz-style corpus
- crash/hang/pathology regression corpus.

---

## 30. Explicit decisions already made

These decisions should be treated as defaults when the project is resumed unless new evidence overturns them:

1. Use the real tactical engine, not autoresolve.
2. Use the real canonical TacticalAI implementation, not a test AI clone.
3. Keep normal Vengeance and Battle Lab runtime isolated.
4. Reuse VRAnalytics and BlackBox; do not create another decision telemetry system.
5. Use paired seeds and mirrored side assignments.
6. Maintain historical baselines.
7. Maintain held-out scenarios.
8. Report uncertainty, not only point estimates.
9. Judge AI quality on multiple dimensions, not only win rate.
10. Preserve exact reproducers for important anomalies.
11. Build FAST before attempting true headless execution.
12. Keep human campaign playtesting as a final promotion gate.
13. Do not let a model auto-merge AI changes based on benchmark scores.
14. Keep the project on the consolidated canonical AI line rather than spawning permanent branches.

---

## 31. Research-derived warning list

When implementation resumes, explicitly guard against:

- overfitting to a few maps;
- overfitting to historical opponent versions;
- side/spawn bias;
- RNG noise being mistaken for improvement;
- telemetry changing timing/behaviour;
- in-process state contamination;
- scenario definition drift;
- hidden-information leakage from evaluator to AI;
- optimizing proxy metrics rather than player-visible quality;
- repeated significance testing / stopping when results "look good";
- one easy scenario dominating aggregate results;
- raw log volume exhausting disk;
- background runner stealing focus/resources;
- FAST/headless mode subtly changing tactics;
- branch proliferation;
- model recommendations becoming self-confirming benchmark optimization.

---

## 32. Resume trigger

This project is deliberately parked.

When resuming, the first task is **not coding the runner**. The first task is to re-read:

- this specification;
- TacticalAI/AI_SUBSYSTEM_MAP.md;
- TacticalAI/AI_Knowledge_Audit.md;
- BLACKBOX_V4.md;
- VRAnalytics.h / VRAnalytics.cpp;
- tools/vr_companion_analyze.py.

Then complete Phase 0 contracts and verify the current engine has not materially changed in ways that invalidate the architecture.

---

## 33. One-sentence project definition

**VR-AIBL is a reproducible, paired, scenario-diverse, telemetry-rich tactical self-play laboratory for measuring whether Vengeance AI changes actually make the canonical game AI more competent, robust, fair, believable and stable.**
