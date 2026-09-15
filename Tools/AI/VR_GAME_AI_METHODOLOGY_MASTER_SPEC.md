# Vengeance Game AI Methodology — Master Architecture & Selection Framework

**Status:** DESIGN / METHODOLOGY ONLY  
**Implementation status:** No gameplay or TacticalAI implementation changes are made by this document.  
**Canonical branch:** \`install/all-2026-09-12\`  
**Related documents:** \`UNIFIED_AI_FRAMEWORK.md\`, \`TacticalAI/AI_SUBSYSTEM_MAP.md\`, \`TacticalAI/AI_Knowledge_Audit.md\`, \`Diagnostics/AI_COMPANION_ANALYSIS_CONTRACT.md\`, \`Tools/AI/VR_AI_BATTLE_LAB_MASTER_SPEC.md\`

---

## 1. Executive decision

Vengeance should **not** be rewritten around one fashionable game-AI methodology.

The strongest fit is a **hybrid, hierarchical, modular, knowledge-limited architecture** in which different decision techniques own different kinds of problems:

- **hard reactive rules / priority selectors** for immediate emergencies and non-negotiable safety;
- **persistent state / intent** for tactical commitment and hysteresis;
- **utility reasoning** for choosing among comparable tactical alternatives;
- **spatial query / tactical-point evaluation** for deciding *where* to act;
- **local squad coordination** for assigning roles and avoiding duplicated work;
- **bounded HTN-like / authored short-horizon plans** for multi-step tactics where sequencing matters;
- **legacy Vengeance / 1.13 execution code** for actually carrying out legal actions;
- **VRAnalytics + Battle Lab** for evaluation, tuning, and regression control.

This is not a recommendation to replace the current Unified AI Framework. It is a recommendation to **formalize what is already good in that framework and make the boundaries much stricter**.

The current Vengeance design already contains several features that align strongly with modern game-AI practice:

- legal personal/public knowledge;
- persistent tactical intent;
- fireteam roles;
- battle-state reasoning;
- explicit competence/friction;
- utility components;
- separate CQB planner;
- shared analytics;
- one orchestration owner per behaviour;
- legacy code used as an execution library.

The next step should be architectural consolidation, not another broad AI rewrite.

---

## 2. Research basis

### Game AI Pro — behavior selection

The Game AI Pro behavior-selection survey compares FSM/HFSM, Behavior Trees, Utility AI, GOAP and HTN and explicitly argues that no one algorithm is universally best; technique selection should depend on the game, agent knowledge, platform and desired experience.

Source:
https://www.gameaipro.com/GameAIPro/GameAIPro_Chapter04_Behavior_Selection_Algorithms.pdf

### Game AI Pro — utility theory

Utility AI is particularly suitable for choosing among competing actions under incomplete information. Important production lessons include:

- normalize scores consistently;
- separate value from contextual desirability;
- use priority buckets for action families that should dominate lower-priority families;
- use top-scoring subsets rather than unconstrained weighted randomness;
- add inertia/cooldowns to prevent oscillation.

Source:
https://www.gameaipro.com/GameAIPro/GameAIPro_Chapter09_An_Introduction_to_Utility_Theory.pdf

### Game AI Pro — reactivity vs deliberation

Carle Côté's chapter argues against forcing all decision-making into one model. Reactivity and deliberation have different dynamics, and hybrid architectures separating deliberation, sequencing/action selection, and execution reduce complexity.

Source:
https://www.gameaipro.com/GameAIPro/GameAIPro_Chapter11_Reactivity_and_Deliberation_in_Decision-Making_Systems.pdf

### Game AI Pro — modular AI

Kevin Dill and Christopher Dragert's modular-AI architecture emphasizes:

- designer-level behavioral abstractions;
- strict encapsulation;
- explicit module interfaces;
- reusable considerations;
- multiple reasoner types within one hierarchy;
- data-driven configuration rather than duplicating code.

Source:
https://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter08_Modular_AI.pdf

### Tactical position selection

Crytek's Tactical Position Selection architecture separates:

1. candidate generation;
2. hard filtering/conditions;
3. weighted scoring;
4. result selection;
5. feedback to the high-level behaviour system.

This is directly relevant to Vengeance's movement, fallback, flanking, cover, CQB, smoke positioning and rescue logic.

Source:
https://www.gameaipro.com/GameAIPro/GameAIPro_Chapter26_Tactical_Position_Selection.pdf

### Knowledge representation

Daniel Brewer's overview argues that agent behaviour is often more dependent on **what information is represented and supplied** than on which decision algorithm is used. Static environment knowledge, dynamic spatial knowledge and entity knowledge should be treated explicitly.

Source:
https://www.gameaipro.com/GameAIProOnlineEdition2021/GameAIProOnlineEdition2021_Chapter04_Knowledge_is_Power_an_Overview_of_AI_Knowledge_Representation_in_Games.pdf

### Squad coordination

Days Gone divides responsibility between the squad and individuals:

- squad decides collective objective, roles and general positions;
- individual executes the role;
- individual can override it for higher-priority local needs;
- squad confidence influences press/hold/retreat;
- formation/frontline reasoning structures spatial coordination.

This maps unusually well to Vengeance fireteams, command, battle state, local reinforcement, fallback and disengagement.

Source:
https://www.gameaipro.com/GameAIProOnlineEdition2021/GameAIProOnlineEdition2021_Chapter12_Squad_Coordination_in_Days_Gone.pdf

### GOAP and HTN examples

F.E.A.R. demonstrates the value of planning when characters need to dynamically assemble action sequences, while Killzone 2 used hierarchical planning for bot behaviour. However, planning also reduces direct authorial control and can create valid but undesirable plans.

Sources:
https://www.gdcvault.com/play/1013282/Three-States-and-a-Plan
https://www.guerrilla-games.com/read/killzone-2-multiplayer-bots

### Authored vs systemic

Naughty Dog's Uncharted 4 work explicitly describes moving toward systemic combat AI and then rebalancing toward authored control. This supports a central Vengeance principle: systemic reasoning is useful, but the desired player experience must retain architectural constraints.

Source:
https://www.gdcvault.com/play/1024467/Authored-vs-Systemic-Finding-a

---

## 3. Why a pure architecture is the wrong answer

### Pure FSM / HFSM

**Strengths**
- cheap;
- explicit;
- easy to inspect;
- good lifecycle/state handling.

**Weaknesses**
- transition explosion;
- brittle when many tactical factors interact;
- encourages binary thresholds;
- easy to create duplicated transition logic.

**Vengeance role**
- retain states where they describe persistent lifecycle/status;
- do not make FSMs the universal tactical reasoner.

---

### Pure Behavior Tree

**Strengths**
- clear priorities;
- good interruption/reactivity;
- modular subtrees;
- excellent debugging when visualized.

**Weaknesses**
- priority order can encode hidden design assumptions;
- complex tactical comparison becomes awkward;
- repeated decorators/conditions can recreate spaghetti in tree form;
- a full BT rewrite would discard valuable legacy Vengeance logic.

**Vengeance role**
- use BT-like **priority semantics**, not necessarily a new BT runtime.
- The current RED/BLACK orchestration already resembles a priority selector.
- Formalize it instead of rewriting it.

---

### Pure Utility AI

**Strengths**
- excellent for contextual choice;
- naturally combines many factors;
- handles incomplete information well;
- supports personality/competence variation;
- good for action and position scoring.

**Weaknesses**
- difficult to tune globally;
- unrestricted competition can produce absurd choices;
- weak at multi-step sequencing;
- can thrash without commitment/inertia.

**Vengeance role**
- **primary local choice mechanism** inside bounded action families.
- Do not flatten every behavior in the game into one giant score table.

---

### Pure GOAP

**Strengths**
- produces novel sequences;
- adapts when preconditions change;
- separates action mechanics from goal achievement.

**Weaknesses**
- search/world-state modelling cost;
- partial observability complicates planning;
- plans can be technically valid but tactically/experientially wrong;
- hard to constrain competence tiers naturally;
- difficult to graft cleanly onto the large legacy JA2 action ecosystem.

**Vengeance role**
- not recommended as primary architecture.
- Concepts may be useful for isolated action sequences or tooling.

---

### Pure HTN

**Strengths**
- strong authorial control;
- naturally hierarchical;
- good for military/tactical sequences;
- expresses multiple methods for accomplishing a task.

**Weaknesses**
- author must encode methods;
- can become a second large behavior hierarchy;
- long plans are fragile in stochastic combat.

**Vengeance role**
- **best planning technique for short tactical plans**, not the whole AI.

Examples:
- establish support -> move flanker -> exploit;
- smoke entry -> move point -> support follows;
- choose casualty -> suppress/smoke -> move medic -> stabilize;
- alternating withdrawal;
- regroup remnant -> establish cover -> disengage.

Plans should normally be 1–3 meaningful tactical steps and be revalidated after each execution.

---

### MCTS / deep search

**Strengths**
- can reason several actions ahead with a simulator;
- potentially strong tactical play.

**Weaknesses**
- expensive;
- requires an accurate fast forward model;
- difficult under hidden information;
- optimization may produce alien/unfun tactics;
- hard to explain/debug in a legacy engine.

**Vengeance role**
- possible future Battle-Lab oracle or offline comparison tool.
- not recommended for normal runtime decisions.

---

### Reinforcement learning / neural policies

**Strengths**
- can learn complex policies;
- useful where explicit modelling is extremely difficult.

**Weaknesses**
- opaque;
- expensive to train;
- hard to constrain for legal information;
- harder to maintain in an old C++ mod;
- difficult to guarantee authored behaviour and competence tiers;
- reward design can optimize undesirable tactics.

**Vengeance role**
- not recommended as the live tactical brain.
- potentially useful **offline** for parameter search, anomaly detection, or generating adversarial test policies.

---

## 4. Recommended Vengeance architecture

The recommended architecture has ten conceptual layers.

### Layer 0 — legal world interface

One authoritative interface for what the AI is allowed to know.

Inputs include:
- personal knowledge;
- public/team knowledge;
- known locations/levels;
- heard/seen age;
- legal friendly state;
- map orders;
- legitimate radio information.

No higher layer reads hidden opponent state directly.

This remains the non-negotiable \`AI_Knowledge_Audit.md\` boundary.

---

### Layer 1 — perception and belief state

Do not expose raw sensory events directly everywhere.

Convert perception into stable tactical beliefs:

- contact confidence;
- likely threat area;
- last-known location age;
- certainty of enemy strength;
- contact direction;
- known/suspected occupied building;
- known casualty;
- suspected firing lane.

Important principle:

**The AI reasons about a belief-state battlefield, not the actual battlefield.**

Where possible, uncertainty should be represented explicitly rather than collapsing every known contact into a Boolean.

---

### Layer 2 — tactical spatial model

This should become a first-class substrate instead of each behaviour inventing its own spatial reasoning.

Candidate spatial features:

- cover;
- sight cover;
- known-threat exposure;
- route exposure;
- known enemy firing arcs;
- friendly support;
- crossfire potential;
- useful weapon range;
- crowding;
- grenade density risk;
- smoke coverage;
- retreat gradient;
- local numerical support;
- choke/doorway/entry;
- high-value firing position;
- mission-anchor distance;
- probable interrupt/reaction risk.

All enemy-derived features must use legal knowledge.

The spatial model need not be a giant full-map influence map. For JA2, **bounded local queries and cached local fields** are likely safer and cheaper.

---

### Layer 3 — battle/squad situation assessment

Current concepts such as:
- perceived force balance;
- casualties;
- stress;
- morale;
- command;
- cohesion;
- local isolation;
- rout pressure;

belong here.

This layer answers:

**What kind of fight do we think we are in?**

Not:
**What exact action do I take?**

---

### Layer 4 — group intent and resource allocation

The fireteam/squad layer should decide:

- PRESS;
- HOLD;
- FLANK;
- SUPPORT;
- FALLBACK;
- DISENGAGE;
- RESCUE;
- REGROUP;
- DEFEND ANCHOR;
- SEARCH/CONTAIN.

It may also allocate scarce roles/resources:

- one flanker;
- one support shooter;
- one medic responder;
- one smoke user;
- limited movers through a doorway;
- one grenade target claim;
- reserve release.

It should not micromanage every AP.

The squad gives **intent, roles, reservations and boundaries**. Individuals solve local execution.

---

### Layer 5 — reactive override

Immediate threats must bypass normal deliberation.

Examples:
- grenade/explosion danger;
- environmental hazard;
- catastrophic exposure requiring immediate reaction;
- hidden interrupt constraints;
- emergency self-preservation.

This should be small and explicit.

Do not hide ordinary tactical preferences inside the emergency layer.

---

### Layer 6 — short-horizon tactical plan

Only when a behaviour genuinely requires sequence.

Recommended planner form:
- authored HTN-like methods or compact plan templates;
- bounded search;
- few steps;
- preconditions based on legal knowledge;
- revalidate after every action;
- abort cleanly when state changes.

Plan state should represent **intent continuity**, not lock an agent into stupidity.

---

### Layer 7 — action-family selection

Use **priority buckets** first, utility inside each bucket.

Illustrative structure:

1. emergency/survival;
2. mandatory mission/script constraints;
3. active plan continuation;
4. immediate combat opportunity;
5. casualty response;
6. positional/tactical action;
7. information/search;
8. maintenance/resource action;
9. legacy fallback.

This prevents nonsensical cross-category comparisons while retaining utility flexibility where it helps.

---

### Layer 8 — action utility

For shortlisted actions, score comparable alternatives.

Examples:
- aimed shot;
- burst;
- suppress;
- grenade;
- smoke;
- reload;
- change stance;
- move;
- hold;
- fallback;
- assist;
- flank.

Utility inputs should be modular **considerations**.

Example attack considerations:
- expected damage;
- chance of useful suppression;
- AP efficiency;
- target value;
- ammo scarcity;
- friendly-fire risk;
- exposure while firing;
- role compatibility;
- current intent;
- target saturation.

Example movement considerations:
- destination cover;
- exposure reduction;
- route exposure;
- useful range;
- support;
- crossfire;
- crowding;
- mission progress;
- escape progress.

Use normalized response curves rather than arbitrary unrelated score scales.

---

### Layer 9 — spatial candidate query

Movement decisions should follow a consistent pipeline:

**Generate -> Reject -> Score -> Select**

Generate only plausible local candidates.

Reject impossible/illegal candidates early:
- unreachable;
- AP impossible;
- hidden-information dependence;
- unacceptable route risk;
- reserved position/role;
- fatal hazard.

Score survivors using context-specific considerations.

This should gradually become the shared infrastructure underneath:
- fallback;
- advance;
- flank;
- CQB;
- medic movement;
- regroup;
- escape;
- support positions.

---

### Layer 10 — execution

Reuse existing mature engine actions whenever possible.

Execution answers:
- how to path;
- how to fire;
- how to throw;
- how to open/use;
- how to bandage;
- how to jump window;
- how AP is charged.

Higher-level systems decide **why and when**.

This is consistent with the current Unified AI Framework and should be preserved.

---

## 5. The Vengeance decision rule

A future developer should be able to classify every AI addition with this table.

| Question | Method |
|---|---|
| Is this an immediate non-negotiable danger? | Reactive hard rule |
| Is this a persistent tactical posture? | Intent/state + hysteresis |
| Is this allocation of squad members/resources? | Squad coordinator / reservation |
| Is this choosing among comparable one-step actions? | Utility |
| Is this choosing a location? | Spatial candidate query |
| Does success require a sequence of actions? | Short HTN-like plan |
| Is this merely performing an already chosen action? | Legacy execution |
| Does it require opponent information? | Knowledge/belief interface |
| Are we unsure about weights/thresholds? | Battle Lab experiment, not another heuristic |

If a proposed feature cannot be classified, architecture work is probably needed before implementation.

---

## 6. Utility methodology

### 6.1 Use hierarchical utility, not one flat score

First choose relevant action family/bucket.

Then score options inside that family.

This is much safer than letting:
- reload,
- flank,
- bandage,
- flee,
- suppress,
- inspect noise

all compete on one arbitrary numeric scale.

### 6.2 Normalize considerations

Prefer a common semantic range such as 0..1.

A score should have an interpretable meaning.

### 6.3 Response curves, not threshold forests

Replace brittle:
- if risk > 37;
- if morale < 42;
- if allies >= 3;

with smooth or piecewise response curves when the underlying concept is continuous.

Hard gates remain appropriate for:
- illegality;
- impossible AP;
- hidden-information violation;
- catastrophic friendly fire;
- mission restriction.

### 6.4 Inertia / commitment

Every persistent choice needs hysteresis.

The currently selected intent/action gets a commitment bonus that decays or is invalidated by significant state change.

This prevents:
- advance/fallback oscillation;
- repeated flank cancellation;
- medic rescue thrashing;
- stance churn.

### 6.5 Controlled variability

Do not randomly pick from all actions.

If variation is desired:
- shortlist top actions within an acceptable utility band;
- competence/personality controls how wide that band is;
- never allow clearly dominated unsafe actions merely for randomness.

---

## 7. Competence model

Difficulty and troop quality should primarily affect **reasoning quality**, not physical cheating.

Possible dimensions:

- number of candidates considered;
- quality of spatial query;
- ability to execute multi-step plans;
- communication quality;
- stale-information handling;
- role discipline;
- amount of tactical lookahead;
- utility noise;
- reaction delay;
- probability of abandoning a sophisticated plan for a simpler legal action.

Do not improve enemy quality by secretly giving:
- AP;
- CTH;
- perfect sight;
- hidden target location;
- instant sector-wide information.

This remains a strong existing Vengeance principle.

---

## 8. Squad AI methodology

Vengeance should avoid both extremes:

### Extreme A — every soldier independent
Produces:
- duplicated flanks;
- crowding;
- doorway queues;
- five grenades at one target;
- no cover fire;
- no coherent withdrawal.

### Extreme B — one omniscient commander
Produces:
- hive-mind behaviour;
- unrealistic perfect coordination;
- hidden-information leakage.

Recommended middle ground:

**local squad coordination with bounded authority.**

The group can own:
- tactical intent;
- role assignments;
- task claims/reservations;
- shared legal knowledge;
- confidence/battle state;
- local geometric boundaries.

The individual owns:
- exact action;
- exact destination;
- emergency override;
- personal survival;
- execution details.

---

## 9. Short-plan methodology

Plans should be **rolling and disposable**.

Bad:
> "For the next six turns, execute this fixed flank sequence."

Good:
> "Current objective: create a flank. Step 1: support holds. Step 2: mover reaches a viable lateral position. Reassess."

A plan should contain:

- objective;
- owner/team;
- assigned role(s);
- current step;
- legal preconditions;
- abort conditions;
- success conditions;
- expiry;
- confidence;
- fallback.

A plan must never create a second perception or pathfinding system.

---

## 10. Spatial reasoning methodology

Spatial reasoning is probably the highest-value architectural improvement area.

Current tactical behaviours frequently differ less in **what they value** than in **which spatial candidates they search**.

Create reusable criteria such as:

- \`CoverFromKnownThreats\`
- \`SightCoverFromKnownThreats\`
- \`RouteExposure\`
- \`FriendlySupport\`
- \`CrossfirePotential\`
- \`WeaponRangeFit\`
- \`CrowdingPenalty\`
- \`GrenadeRisk\`
- \`SmokeBenefit\`
- \`MissionAnchorFit\`
- \`EscapeProgress\`
- \`DoorwayCongestion\`
- \`ReactionRiskEstimate\`

Then combine them differently by context instead of writing new path logic for every behaviour.

---

## 11. Knowledge methodology

The biggest conceptual shift should be from **truth variables** to **belief variables**.

For opponent-derived facts, ask:

- How do we know this?
- Who knows it?
- How old is it?
- How certain is it?
- Has it been communicated?
- Is fresh LOS required?
- What uncertainty should the reasoner attach?

Examples:

Bad concept:
- \`enemyStrength = 7\`

Better:
- \`perceivedEnemyStrength = 4.2 equivalent units\`
- \`knownContacts = 4\`
- \`confidence = medium\`
- \`oldestRelevantContactAge = 2 turns\`

The evaluator/BlackBox may know ground truth; the AI must not.

---

## 12. Refactor methodology

Do not replace working AI wholesale.

Recommended migration rule:

### Step A — expose
Wrap existing behaviour in a clear interface and telemetry without changing outcome.

### Step B — separate
Split:
- information gathering;
- candidate generation;
- legality;
- scoring;
- selection;
- execution.

### Step C — compare
Run old and new evaluators in shadow mode if feasible.

### Step D — switch
Only after the new pathway reproduces acceptable old behaviour and improves targeted cases.

### Step E — delete duplication
Once authoritative, remove the old competing trigger.

This avoids a permanent "old AI + new AI both deciding" architecture.

---

## 13. Testing methodology

Every change should have at least one test at the **smallest useful level**.

Examples:

- perception helper -> deterministic unit/micro test;
- utility curve -> numerical test;
- candidate rejection -> micro scenario;
- flank planner -> specialist scenario;
- squad behaviour -> Battle Lab suite;
- global tuning -> broad paired benchmark;
- player experience -> campaign playtest.

Do not use 1,000-battle stochastic tests to diagnose a simple wrong LOS predicate.

Do not use a two-character unit test to claim general tactical superiority.

---

## 14. Telemetry requirements

For each material decision:

1. **knowledge** — what was legally believed;
2. **context** — intent, role, battle state;
3. **candidate family**;
4. **candidates generated**;
5. **hard rejects and reason**;
6. **utility considerations**;
7. **selection and runner-up**;
8. **commitment/inertia influence**;
9. **execution outcome**;
10. **near-term tactical outcome**.

This makes the AI scientifically inspectable.

---

## 15. Implementation roadmap when approved

### Phase 0 — architecture contracts
No behavioural change.

Define:
- DecisionContext;
- ActionCandidate;
- SpatialCandidate;
- TacticalIntent;
- ShortPlan;
- legal knowledge interface;
- consideration interface;
- reservation/claim interface.

### Phase 1 — unified decision snapshot
Create one read-only legal tactical context per decision.

Goal:
stop every subsystem independently recomputing slightly different interpretations of the battlefield.

### Phase 2 — consideration library
Extract reusable normalized factors:
- risk;
- cover;
- support;
- range;
- exposure;
- route risk;
- confidence;
- crowding;
- scarcity.

No major behavior rewrite yet.

### Phase 3 — spatial query framework
Unify candidate generation/filter/score for movement-related behaviours.

Start with fallback because it is comparatively bounded and already instrumented.

Then:
- generic cover;
- flank;
- support;
- rescue;
- CQB.

### Phase 4 — formal utility buckets
Turn the current priority hierarchy into explicit action families and documented precedence.

### Phase 5 — intent commitment
Formalize persistence, hysteresis, expiry and re-evaluation.

### Phase 6 — coordination board
Add bounded team-level claims/reservations using only legal shared knowledge.

### Phase 7 — short-plan framework
Only for behaviours shown to need sequence.

Port existing CQB and rescue continuity into this common model before inventing many new plans.

### Phase 8 — competence/friction normalization
Make troop quality alter reasoning complexity and reliability in a consistent way.

### Phase 9 — Battle Lab calibration
Use controlled evidence for curves/weights.

### Phase 10 — selective offline optimization
Only after metrics and constraints are stable, consider automated parameter search.

---

## 16. What should NOT be done next

Do not:

- rewrite TacticalAI into a generic Behavior Tree engine;
- port a full GOAP implementation;
- introduce RL into runtime combat;
- create a global omniscient squad blackboard;
- add a separate pathfinder per behaviour;
- keep adding one-off thresholds into \`DecideAction.cpp\`;
- create a second tactical decision log;
- merge newer 1.13 AI wholesale;
- optimize Battle Lab win rate as the definition of intelligence.

---

## 17. Architectural diagnosis rule

When behaviour is bad, classify the failure before editing code:

### PERCEPTION failure
The AI did not receive legal information it should have.

### BELIEF failure
It had information but interpreted certainty/age incorrectly.

### SITUATION-ASSESSMENT failure
It misunderstood whether the fight was winning/losing/risky.

### COORDINATION failure
The right action existed but roles/resources conflicted.

### CANDIDATE failure
The good action/position was never considered.

### LEGALITY failure
A valid option was rejected or an invalid one admitted.

### UTILITY failure
The options existed but were scored incorrectly.

### COMMITMENT failure
The AI changed plan too easily or refused to change when necessary.

### PLAN failure
The sequence was bad or stale.

### EXECUTION failure
The decision was correct but engine execution failed.

### BALANCE failure
Logic is correct; parameter values need tuning.

### KNOWLEDGE-CHEAT failure
The decision relied on information the actor should not possess.

This taxonomy should replace vague diagnoses such as "AI is stupid."

---

## 18. Final methodology statement

The Vengeance tactical AI should be understood as a **bounded-rational military agent system**, not as a pile of scripted reactions and not as a search algorithm trying to maximize victory.

Its job is to:

1. perceive legally;
2. maintain imperfect beliefs;
3. assess the local battle;
4. coordinate locally;
5. form a bounded intent;
6. react instantly to emergencies;
7. plan briefly when sequence matters;
8. generate realistic options;
9. select among them by contextual utility;
10. execute using mature engine mechanics;
11. remain committed long enough to look purposeful;
12. expose every material decision to telemetry;
13. improve through controlled evidence rather than anecdotes.

That is the recommended long-term methodology for Vengeance.
