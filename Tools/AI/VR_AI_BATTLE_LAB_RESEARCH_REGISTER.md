# VR AI Battle Lab — Research & Decision Register

**Status:** PARKED  
**Companion document:** Tools/AI/VR_AI_BATTLE_LAB_MASTER_SPEC.md  
**Canonical branch:** install/all-2026-09-12  
**Implementation:** None. This file records research conclusions and decisions only.

---

## A. Why this document exists

This is the short re-entry document for future work. Read this first, then the Master Specification.

The project idea is an unattended AI-vs-AI tactical laboratory using the actual Vengeance engine and AI. It exists to replace anecdotal judgments such as "the AI seems smarter" with controlled experiments.

---

## B. Highest-value research findings

### 1. JA2 already has a direct research precedent

Manuel Ladebeck's 2008 diploma thesis "Applying Dynamic Scripting to Jagged Alliance 2" built automatic AI-vs-AI benchmarking on JA2 v1.13 revision 2088.

The test environment:
- placed a harmless player merc at a sector edge because the engine required one;
- automatically skipped the player team's turn;
- allowed two computer-controlled sides to fight unattended;
- reset the sector after combat;
- ran matches in batches;
- used equal-strength/equal-equipment scenarios;
- explicitly warned that single JA2 battles are too random to carry much evidential weight;
- ran 1,000 matches per scenario;
- suffered badly from graphical animation/fixed-delay overhead.

Primary lesson:
**VR-AIBL is technically plausible in the JA2 engine family, but it should add modern reproducibility and statistical discipline.**

Source:
https://doczz.net/doc/3780831/applying-dynamic-scripting-to-jagged-alliance-2

### 2. Historical opponents prevent false progress

OpenAI Five deliberately played against past versions; AlphaStar used a league including exploiters designed to expose weaknesses.

Primary lesson:
**Do not evaluate a new Vengeance AI only against itself or only against the immediately previous build. Keep milestone baselines.**

Sources:
https://openai.com/index/openai-five/
https://cdn.openai.com/dota-2.pdf
https://deepmind.google/blog/alphastar-grandmaster-level-in-starcraft-ii-using-multi-agent-reinforcement-learning/

### 3. Holdout scenarios are mandatory

Melting Pot evaluates on held-out scenarios. XLand separates evaluation tasks from training tasks. GVGAI work tests generalisation to unseen levels.

Primary lesson:
**Some VR-AIBL scenarios must remain outside normal AI tuning.**

Sources:
https://deepmind.google/blog/melting-pot-an-evaluation-suite-for-multi-agent-reinforcement-learning
https://deepmind.google/blog/generally-capable-agents-emerge-from-open-ended-play
https://arxiv.org/abs/2005.11247

### 4. Deterministic seeding should be tested, not assumed

PettingZoo includes explicit seed reproducibility tests across separate environment instances and reset cycles.

Primary lesson:
**VR-AIBL itself needs a determinism compliance suite.**

Source:
https://pettingzoo.farama.org/content/environment_tests/

### 5. Paired random streams improve comparisons

Simulation methodology uses Common Random Numbers to compare alternative configurations with reduced variance.

Primary lesson:
**Candidate vs baseline should use matched seeds/random streams where technically valid and mirror side assignments.**

Sources:
https://informs-sim.org/wsc09papers/232.pdf
https://www.informs-sim.org/wsc23papers/124.pdf

### 6. Independent repetitions matter

Simulation-output methodology warns that one simulation run is not an answer; independent replications and confidence intervals are basic requirements.

Primary lesson:
**Battle counts should be driven by precision/effect size, not one spectacular result or an arbitrary fixed number.**

Sources:
https://informs-sim.org/wsc20papers/134.pdf
https://www.informs-sim.org/wsc23papers/124.pdf

### 7. Ratings are navigation, not truth

TrueSkill models both estimated skill and uncertainty. Population-based game evaluation demonstrates that cyclic matchups can exist.

Primary lesson:
**A scalar rating may summarize the historical league but must not replace the pairwise/scenario matrix.**

Sources:
https://www.microsoft.com/en-us/research/publication/trueskilltm-a-bayesian-skill-rating-system/
https://deepmind.google/research/publications/22497/

### 8. Preserve reproducers

OSS-Fuzz centers debugging around exact reproducible testcases and retains useful seed corpora/regression inputs.

Primary lesson:
**Every important tactical pathology should become a permanent reproducer: scenario + seed + version hashes + expected anomaly.**

Sources:
https://google.github.io/oss-fuzz/advanced-topics/reproducing/
https://google.github.io/oss-fuzz/advanced-topics/ideal-integration/

---

## C. Frozen design decisions

Unless later research provides a better answer:

| # | Decision |
|---|---|
| 1 | Test the actual tactical engine; do not use autoresolve as a proxy. |
| 2 | Execute canonical TacticalAI code; do not maintain a second test AI implementation. |
| 3 | Lab runtime must be isolated from the playable Vengeance installation. |
| 4 | VRAnalytics remains the structured decision stream; BlackBox remains crash/hang forensics. |
| 5 | Candidate comparisons use paired seeds and mirrored sides. |
| 6 | Keep historical milestone AI opponents. |
| 7 | Keep development, validation and locked benchmark scenarios separate. |
| 8 | Record complete experiment identity and hashes. |
| 9 | Report confidence/uncertainty and practical effect size. |
| 10 | Win rate is only one metric; fairness, behaviour, robustness and speed are separate axes. |
| 11 | Omniscient evaluator data must never enter the AI's information path. |
| 12 | FAST simulation precedes true headless simulation. |
| 13 | Validate FAST/headless equivalence against visual mode. |
| 14 | Important anomalies become reproducible regression cases. |
| 15 | Human campaign play remains the final behavioural sanity check. |
| 16 | Model analysis may recommend changes; it must not automatically merge them. |
| 17 | Battle Lab infrastructure returns to the consolidated branch rather than spawning a permanent AI fork. |

---

## D. Proposed evidence hierarchy

From weakest to strongest:

1. **Anecdotal human battle**
   - good for discovering a possible problem.

2. **Single deterministic reproducer**
   - good for proving a specific bug/pathology.

3. **Specialist micro-suite**
   - measures prevalence around one behaviour.

4. **Broad paired tactical suite**
   - measures general effect across scenarios.

5. **Historical-opponent league**
   - tests forgotten strategies/cyclic weaknesses.

6. **Locked validation suite**
   - tests generalisation without direct tuning.

7. **Human campaign validation**
   - confirms that benchmark improvement actually improves Vengeance.

No lower level should substitute for a higher level when the question requires it.

---

## E. Proposed experiment verdict vocabulary

Use consistent labels:

- **CONFIRMED** — effect is repeatable, practically meaningful and survives relevant controls.
- **PROBABLE** — evidence is strong but sample/coverage is incomplete.
- **INCONCLUSIVE** — uncertainty too large or evidence conflicts.
- **DISPROVED** — expected improvement absent under adequate test.
- **REGRESSION** — candidate causes material harm.
- **HARNESS ISSUE** — experiment invalidated by determinism, telemetry or scenario problem.

---

## F. Minimum experiment record

A future result is not valid without at least:

- experiment ID;
- hypothesis;
- candidate SHA;
- baseline SHA;
- game-data SHA;
- config fingerprint;
- scenario ID/version;
- seed/pair ID;
- side assignment;
- runtime mode;
- terminal outcome;
- primary metrics;
- guardrails;
- anomaly references.

---

## G. First implementation checkpoint when resumed

Do **not** immediately optimize rendering or build thousands of scenarios.

First prove:

1. one unattended battle can use the real tactical AI;
2. initial state can be reproduced from scenario + seed;
3. battle end is detected correctly;
4. state resets cleanly;
5. 100 repetitions can run without user input;
6. the same experiment can be rerun and identified from telemetry;
7. analytics do not alter tactical semantics.

Only then expand.

---

## H. Statistics shortlist

For the future analyzer:

- Wilson interval for simple proportions such as win rate;
- paired differences for candidate-vs-baseline CRN runs;
- bootstrap/hierarchical bootstrap across scenarios for broad aggregate effects;
- scenario-family stratification;
- predeclared primary metrics;
- minimum practically important effect;
- predeclared stopping rules;
- explicit multiple-metric guardrails.

Avoid:
- treating shots/actions inside a single battle as independent battle outcomes;
- p-value hunting across dozens of metrics;
- reporting only an aggregate average;
- allowing one scenario with huge replication count to dominate the evaluation.

NIST proportion CI reference:
https://www.itl.nist.gov/div898/handbook/prc/section2/prc241.htm

---

## I. Most important conceptual distinction

**VR-AIBL is not intended to train a black-box game-playing agent.**

It is an empirical engineering system for improving a human-readable heuristic tactical AI.

Desired loop:

~~~
hypothesis
-> code candidate
-> controlled battles
-> statistics + behavioural traces
-> diagnosis
-> human/model design judgment
-> next candidate
~~~

Not:

~~~
maximize one score
-> automatically mutate AI
-> merge whichever version wins
~~~

---

## J. Re-entry instruction for future ChatGPT work

When the user says to resume the AI Battle Lab:

1. Read this file.
2. Read Tools/AI/VR_AI_BATTLE_LAB_MASTER_SPEC.md.
3. Read TacticalAI/AI_SUBSYSTEM_MAP.md.
4. Read TacticalAI/AI_KNOWLEDGE_AUDIT.md (actual filename currently AI_Knowledge_Audit.md).
5. Read BLACKBOX_V4.md.
6. Inspect current VRAnalytics.h / VRAnalytics.cpp.
7. Inspect tools/vr_companion_analyze.py.
8. Check whether the canonical branch or architecture has moved since this design was parked.
9. Revalidate assumptions before implementation.
10. Resume at Phase 0 (contracts), not at headless optimization.

---

## K. Parking state

**No further work should be inferred from the existence of these documents.**
The idea is intentionally parked until the user explicitly asks to resume it.
