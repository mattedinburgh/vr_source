# Vengeance Reloaded — AI Self-Play Battle Lab

## Purpose

Run authentic tactical AI-vs-AI battles unattended, capture black-box evidence, compare identical scenarios and seeds across AI builds, and use the evidence to improve tactical AI without changing the strategic campaign.

This is an experimentation harness, not a second combat simulator. The measured fights use the shipped tactical engine: map/pathing, NCTH, weapons, suppression, smoke, wounds, roofs, doors, grenades and the normal tactical AI execution path.

## Research conclusions encoded in v1

1. **Use the real engine.** A simplified simulator would be faster but could teach the wrong lessons.
2. **Fixture snapshots.** A normal tactical save at the start of a battle is the scenario definition.
3. **Deterministic seed identity.** Each run resets both RNG paths after the fixture finishes loading.
4. **Paired A/B testing.** Re-run the same fixture+seed range on baseline and candidate builds.
5. **State hashes.** Duplicate build+fixture+seed runs should converge to the same final hash; mismatches flag nondeterminism.
6. **No campaign consequences.** Lab battle completion is intercepted before normal sector ownership, loyalty, morale, quests and records are applied. The fixture is reloaded for every trial.
7. **Hard stall budget.** A configurable maximum team-turn count converts infinite/stalled fights into an explicit `max_team_turns` result.
8. **Presentation is not evidence.** The window is hidden and the real event loop is accelerated. V1 deliberately does not virtualize the JA2 clock because bullets, animations, event queues, doors and deadlock handling depend on it.
9. **Black-box first.** Record chosen executable AI decisions and combat outcomes, then change AI between controlled batches. Do not let the running game rewrite its own AI.

## Command line

```
ja2.exe -SELFPLAY=<saveSlot>,<runs>,<baseSeed>,<maxTeamTurns>,<buildLabel>
```

Example:

```
ja2.exe -SELFPLAY=3,100,50000,1200,baseline
```

The process hides its game window, loads save slot 3, runs 100 battles using seeds 50000..50099, writes the corpus, then exits.

For the candidate AI build, use the same fixture and seed range:

```
ja2.exe -SELFPLAY=3,100,50000,1200,candidate
```

The optional build label is deliberately stored in every row for paired comparisons.

## Fixture requirements

The save should be:
- already on `GAME_SCREEN`;
- in turn-based tactical combat;
- saved at a clean repeatable point, preferably the beginning of a team turn;
- contain at least one living OUR_TEAM soldier and one living ENEMY_TEAM soldier.

Use a bank of fixtures rather than one battle to avoid overfitting. Recommended corpus:
- dense urban/CQB;
- military compound;
- open terrain/long range;
- night battle;
- mixed roof/interior;
- suppression-heavy;
- smoke/fallback;
- casualty/medical-rescue;
- outnumbered defence;
- attack against prepared defence.

## Output

### AI SelfPlay Runs.tsv

One row per completed trial:
- build label, fixture, run and seed;
- sector/result/team-turn count/wall time;
- starting and surviving strength;
- shots/hits/misses/damage/kills/deaths;
- movement/suppression/explosive/smoke counts;
- deterministic final-state hash.

### AI SelfPlay Decisions.tsv

One row per final selected action that reaches affordability/execution:
- run/seed/team turn;
- soldier/team/profile/grid/level;
- life/AP/breath/shock/alert/morale;
- action and destination/action data;
- next action;
- under-fire state.

### Campaign Tactical Black Box.tsv

Existing low-level tactical telemetry remains active and records movement orders, projectiles, damage, casualties, suppression, smoke and explosions.

### AI SelfPlay Batch.txt

Human-readable lifecycle/errors/batch summary.

## Analysis

Run:

```
python tools/analyze_selfplay.py
```

For paired comparison:

```
python tools/analyze_selfplay.py --baseline baseline --candidate candidate
```

The analyzer reports aggregate outcomes and efficiency, action mix, stall signals, repeated identical action/destination loops, duplicate-seed hash conflicts and paired metric deltas.

## Interpretation rule

Do not optimize only for win rate. A stronger build may win more for a bad reason (for example excessive aggression exploiting one fixture). Review:
- stalls/deadlocks;
- exposure and casualties;
- movement versus useful fire;
- flank/withdrawal/cover/smoke behavior;
- repeated actions;
- suppression value;
- scenario-specific regressions;
- reproducibility.

Any AI change should be evaluated on the same seed bank before and after the change.

## Current v1 limitation

“Headless” in v1 means **invisible, presentation-suppressed, accelerated real-engine execution**. The Windows/game event loop still exists. Full clock/render virtualization is intentionally deferred until deterministic replay is proven because it risks changing tactical behavior itself.

## Branch

Implementation branch: `ai/selfplay-battle-lab-2026`

Base: `install/all-2026-09-12` (canonical 2026-09-12 / Main / Super Master line).
