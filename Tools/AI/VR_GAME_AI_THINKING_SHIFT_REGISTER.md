# Vengeance Game AI — Thinking Shift & Decision Register

**Status:** DESIGN ONLY  
**Implementation:** None.  
**Companion document:** \`Tools/AI/VR_GAME_AI_METHODOLOGY_MASTER_SPEC.md\`

This is the short future-reference document describing how our **way of thinking about AI development should change**.

---

## 1. From "add behaviours" to "design a decision system"

Old question:

> How do we make the enemy flank?

Better questions:

- What battlefield state makes flanking desirable?
- Who should own that intent?
- Who is eligible to flank?
- Which alternatives compete with it?
- How long should the intent persist?
- What spatial candidates constitute a viable flank?
- What makes the flank unsafe?
- What knowledge is the decision based on?
- How do we know afterward whether the flank helped?

A behaviour is not finished because code exists for it. It is finished when it has a place in the decision architecture.

---

## 2. From "if X, do Y" to "generate, constrain, score, commit"

Prefer:

1. generate plausible options;
2. eliminate illegal/absurd ones;
3. score the survivors;
4. select;
5. remain committed unless the situation materially changes.

This produces more human-looking continuity than forests of hard thresholds.

---

## 3. From omniscient state to belief state

Never ask merely:

> Where is the enemy?

Ask:

> What does this soldier/fireteam currently believe about the enemy, based on legal information?

Represent:
- recency;
- certainty;
- source;
- communication;
- confidence.

This is both fairer and more interesting tactically.

---

## 4. From individual genius to bounded team intelligence

A real-looking squad should not consist of:
- six isolated idiots;
- or six bodies controlled by one omniscient brain.

Use local coordination:
- shared intent;
- shared decision frame (for example the fireteam's current attack axis);
- roles;
- claims;
- reservations;
- confidence;
- legal public knowledge.

Shared intent alone is not enough. If several soldiers agree to flank but independently choose opposing sides, the team still behaves like isolated individuals. Coordinate the coarse frame first (hold/press/flank and, when relevant, centre/left/right), then let each actor choose its own legal route, position and execution details.

Individuals retain local autonomy and emergency override. Surprise, collapse, personal danger and invalidated knowledge must be able to break the shared frame immediately.

---

## 5. From "best action" to "best action for this actor"

Utility depends on:
- competence;
- role;
- weapon;
- personality/attitude;
- risk tolerance;
- wounds;
- local support;
- current intent;
- mission.

The same battlefield should legitimately produce different decisions from different soldiers.

---

## 6. From random incompetence to bounded rationality

Low-quality troops should not simply roll a die and do something stupid.

They should:
- consider fewer good options;
- use simpler plans;
- communicate worse;
- react more slowly;
- make noisier utility estimates;
- abandon complex tactics more often;
- choose a reasonable but suboptimal legal action.

This feels human rather than artificial.

---

## 7. From per-behaviour pathfinding to shared spatial intelligence

Flank, retreat, rescue, CQB and advance should not each reinvent:
- cover;
- exposure;
- route safety;
- support;
- crowding;
- useful range.

Build common spatial considerations and combine them differently.

This is a major architectural priority.

---

## 8. From one-turn thinking to rolling short-horizon intent

Do not make the AI permanently myopic.

Also do not lock it into long scripts.

Preferred:
- choose intent;
- plan 1–3 meaningful steps;
- execute one;
- re-evaluate.

This is closer to human tactical action.

---

## 9. From thresholds to response curves

Many concepts are continuous:
- fear;
- confidence;
- risk;
- urgency;
- support;
- exposure.

Use normalized response curves where practical.

Keep hard thresholds for actual constraints:
- legality;
- AP;
- geometry;
- mission orders;
- unacceptable friendly fire;
- hidden-information rules.

---

## 10. From "smartest possible" to "best game opponent"

The goal is not optimal warfare.

The goal is:
- tactically credible;
- challenging;
- fair;
- readable;
- varied;
- human-like;
- compatible with Vengeance's tone;
- performant.

An unbeatable or perfectly coordinated AI can be a worse game AI.

---

## 11. From winning to multi-objective quality

Never define AI quality as win rate alone.

Also measure:
- survival behaviour;
- cover discipline;
- tactical diversity;
- knowledge fairness;
- coordination;
- medical behaviour;
- friendly fire;
- resource use;
- performance cost;
- player readability/believability.

---

## 12. From code-first to hypothesis-first

Before changing behaviour, write:

- problem;
- suspected layer;
- hypothesis;
- expected metric movement;
- possible negative side effects;
- smallest useful test.

Then code.

Afterward:
- compare evidence;
- keep/revise/revert.

---

## 13. From symptom patches to layer diagnosis

When something stupid happens, do not immediately add another exception.

First classify:

- perception?
- belief?
- situation assessment?
- group intent?
- candidate generation?
- legality?
- utility?
- commitment?
- plan?
- execution?
- balance?

Fix the earliest incorrect layer.

---

## 14. From copying 1.13 to extracting concepts

For upstream AI:

Wrong:
> Newer 1.13 has this function; port it.

Right:
> What problem is it solving? Does Vengeance already solve it? Which layer owns that problem here? Is its information legal? Can its concept improve the canonical system without creating a second decision path?

Port concepts and correctness fixes, not architectures by accident.

---

## 15. From "AI feature" to "AI contract"

Every feature should declare:

- owner;
- inputs;
- legal knowledge requirements;
- outputs;
- preemption priority;
- state lifetime;
- performance budget;
- telemetry;
- tests;
- interaction with other subsystems.

This stops branch/logic sprawl.

---

## 16. From perfect coordination to communication-limited coordination

If coordination uses knowledge, ask:

- who observed it?
- who was close enough?
- was it radioed?
- how old is the report?
- does this troop quality use it reliably?

Coordination quality becomes part of doctrine and competence rather than hidden omniscience.

---

## 17. From action randomness to tactical variation

Variation should occur among **near-equivalent reasonable choices**.

Bad:
- 15% chance of a stupid action.

Better:
- shortlist actions within an acceptable utility band;
- vary among them based on competence/personality;
- preserve hard safety/legality constraints.

---

## 18. From global scoring to hierarchical choice

Do not compare everything against everything.

Use:
- emergency bucket;
- plan/mission bucket;
- combat bucket;
- casualty bucket;
- tactical movement bucket;
- maintenance bucket.

Then use utility within the active bucket.

---

## 19. From hand-tuned constants to inspectable parameters

Every important tuning constant should eventually have:

- semantic name;
- units/normalization;
- response curve;
- doctrine/competence modifier;
- default;
- telemetry visibility;
- experiment history.

"37 because it seemed okay" is technical debt.

---

## 20. From large battles as debugging to layered tests

Use the smallest test that can answer the question.

- utility function -> unit test;
- LOS/knowledge -> micro scenario;
- rescue sequence -> specialist tactical scenario;
- squad coordination -> Battle Lab suite;
- global quality -> broad benchmark;
- feel -> campaign play.

---

## 21. From hidden architecture to visualizable reasoning

The eventual ideal debug view should be able to show:

- actor belief contacts;
- squad intent;
- role;
- active short plan;
- generated candidate positions;
- hard rejects;
- utility contributions;
- selected action;
- why runner-up lost.

If we cannot explain a decision, tuning it safely is difficult.

---

## 22. From "more intelligence" to "better information + better choice"

A recurring industry lesson is that poor AI often stems from bad knowledge representation, not a weak decision algorithm.

Before introducing a more sophisticated planner, ask:

> Does the agent actually have a clean representation of the tactical facts required to make this decision?

Often the correct upgrade is:
- better contact confidence;
- better route exposure;
- better cover representation;
- better team role state;

not a more advanced search algorithm.

---

## 23. From one giant AI to multiple conceptual models

Different problems deserve different reasoners.

- emergency reaction -> priority/rule;
- battle posture -> persistent intent;
- action comparison -> utility;
- position choice -> spatial query;
- multi-step maneuver -> HTN-like plan;
- execution -> legacy engine action.

Trying to make one formalism solve all of these is unnecessary complexity.

---

## 24. From permanent plans to interruptible plans

Every plan must know:
- why it exists;
- when it succeeds;
- when it should abort;
- when it should be recomputed.

A plan is evidence of continuity, not a commitment to ignore the battlefield.

---

## 25. From retrospective stories to causal evidence

BlackBox/Companion should answer:

- what did it know?
- what did it consider?
- what was rejected?
- why did it select this?
- what happened?
- did the effect repeat across battles?

Do not invent a narrative after seeing the outcome.

---

## 26. From "feature complete" to "validated"

A tactical feature is complete only when:

1. architecture owner is clear;
2. legal knowledge is clear;
3. decision telemetry exists;
4. deterministic/specialist tests exist where feasible;
5. Battle Lab evidence exists for stochastic effects;
6. no major interaction regression appears;
7. human gameplay still looks good.

---

## 27. Recommended mental model

Think of each AI soldier as:

> **an imperfect tactical decision-maker with limited information, local social coordination, persistent but interruptible intent, limited cognitive sophistication determined by troop quality, and access to a shared library of physical actions.**

Think of each fireteam as:

> **a lightweight coordinator that allocates intent, roles and scarce tactical opportunities without directly piloting every soldier.**

Think of the engine as:

> **the physics/execution world, not the AI brain.**

Think of Battle Lab as:

> **the scientific instrument that tells us whether our beliefs about the AI are actually true.**

---

## 28. Future shorthand

When reviewing a proposed AI change, ask these nine questions:

1. **KNOW:** What does the actor legally know?
2. **ASSESS:** What does it think the situation is?
3. **INTENT:** What is it trying to accomplish?
4. **TEAM:** What has the group allocated/reserved?
5. **PLAN:** Is sequencing required?
6. **OPTIONS:** What reasonable actions/positions exist?
7. **VALUE:** Why is one better?
8. **COMMIT:** Why should it continue or change?
9. **VERIFY:** What evidence will prove improvement?

If those nine answers are clear, implementation is usually straightforward.

---

## 29. Parking instruction

This methodology is guidance only until explicitly approved for implementation.

When resuming:
1. read this register;
2. read the Master Methodology;
3. read the current Unified AI Framework and subsystem map;
4. verify Battle Lab status;
5. choose one bounded subsystem for architectural migration;
6. preserve behavior first, then improve it.
