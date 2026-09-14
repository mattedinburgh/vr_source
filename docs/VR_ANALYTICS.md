# Vengeance Reloaded Analytics Architecture

## Purpose

The analytics system exists to answer two different questions from the same
structured telemetry:

1. **Black Box:** why did the engine do this, and where did a failure occur?
2. **Campaign Companion:** what gameplay consequences did decisions and code
   changes produce, including effects that cross tactical and strategic layers?

Both systems are split into **tactical/battle** and **strategic/campaign**
subsystems. They share an event stream so conclusions can cross those
boundaries without maintaining two incompatible logging systems.

## Architecture

```text
Vengeance game state
        |
        v
VR analytics event stream (VR_BlackBox.jsonl)
        |
        +-----------------------+
        |                       |
        v                       v
Black Box                 Campaign Companion
forensics                 refinement analysis
        |                       |
   +----+----+              +---+---+
   |         |              |       |
tactical  strategic      tactical strategic
```

The shared event stream is append-only JSONL using schema
`vr-blackbox-1`. Every record has a session identifier and sequence number.

## Current causal chains

### Tactical

The current implementation records:

- AI decision start
- selected action
- current tactical state at selection
- action rejection when it is unaffordable
- action completion
- AP consumed
- movement delta
- life/breath change
- last-attack-hit flag
- selected attack-vs-cover candidate scores
- battle start
- battle result
- start/end team counts
- sector
- campaign time

The core chain is:

```text
candidate evaluation
    -> selected tactical action
    -> action execution
    -> completion/rejection/supersession
    -> battle outcome
```

### Strategic

The current implementation records:

- Queen reinforcement-evaluation state
- campaign time
- reinforcement pool/request/available points
- player progress and difficulty
- eligible garrison and patrol targets
- target sector and score
- selected reinforcement target
- explicit no-action reasons
- strategic group move orders
- source and target sector
- group size
- movement mode and intention
- actual reinforcement arrival
- group ID
- arrival sector and campaign time

The core chain is:

```text
Queen evaluation
    -> candidate target sectors
    -> selected target
    -> group movement order
    -> group arrival
    -> later battle in that sector
```

## Black Box

The Black Box is the raw forensic layer. It must preserve evidence rather than
decide whether a game mechanic is good or bad.

Primary file:

```text
VR_BlackBox.jsonl
```

Important identifiers:

- `session`: executable run
- `seq`: total event order within that run
- `decision_id`: local causal decision chain
- `battle_id`: tactical battle lifecycle
- `group_id`: strategic formation/movement identity
- sector and `world_minutes`: cross-layer correlation
- `experiment_tag`: tuning/code experiment attached to the executable session

The logger flushes after each record so a crash should still leave the recent
decision history available.

### Experiment labels

Before launching the game, put the current change label in:

```text
VR_Analytics_Experiment.txt
```

in the game working directory, for example:

```text
AI_SMOKE_COORDINATION_R3
```

Alternatively set the `VR_ANALYTICS_EXPERIMENT` environment variable. The
environment variable takes precedence. If neither exists, the session is
recorded as `unlabeled`.

## Campaign Companion

Run:

```text
python tools/vr_companion_analyze.py VR_BlackBox.jsonl
```

Compare against an earlier experiment:

```text
python tools/vr_companion_analyze.py VR_BlackBox.jsonl \
    --baseline VR_BlackBox_previous.jsonl
```

Outputs:

```text
VR_Companion_Report.md
VR_Companion_Summary.json
```

Current analysis includes:

- tactical completion/rejection/supersession rates
- attack-vs-cover score balance
- battle results and team-count changes
- Queen reinforcement-selection behavior
- strategic move-order/arrival matching
- strategic travel time
- unmatched/outstanding strategic movements
- before/after metric deltas
- reinforcement-to-battle cross-layer analysis

The initial cross-layer test classifies a battle as recently reinforced when an
enemy strategic group arrived in the same sector during the preceding 48
campaign hours. It then compares battle results for reinforced and
unreinforced sectors.

This is deliberately treated as a **causal lead**, not proof. A statistical
difference tells us which Black Box chains to inspect.

## Design rules

### 1. Record reasons, not just actions

A useful event chain contains the alternatives and their scores whenever the
decision logic exposes them. Logging only the final action is insufficient for
AI refinement.

### 2. Separate intended decision from execution

For example:

```text
Queen selects sector B2 for reinforcement
    -> Group 17 receives B2 movement order
    -> Group 17 is redirected
    -> Group 17 eventually reaches C2
```

Selection, movement, redirection and arrival are separate facts.

### 3. Outcomes matter at multiple time scales

A tactical decision can be locally successful while making the campaign
easier or harder. Analysis should eventually support:

```text
mechanic/code change
    -> tactical decision distribution
    -> tactical battle outcomes
    -> survivor/replacement requirements
    -> strategic force availability
    -> later campaign outcomes
```

### 4. Session-safe IDs

Decision and battle counters restart when the executable restarts. The
Companion therefore keys local IDs with their session ID. Never correlate a
bare `decision_id` or `battle_id` across sessions.

### 5. Do not infer causality from one correlation

The Companion should nominate suspected interactions. The Black Box should
then reconstruct the concrete decision chains that support or contradict the
hypothesis.

## Regression tests

Run from the `tools` directory:

```text
python test_vr_companion.py
```

Current tests cover:

- session-safe decision IDs
- strategic order-to-arrival matching
- redirected strategic groups
- strategic reinforcement-to-battle linkage
- exclusion of stale reinforcement events

## Next instrumentation priorities

The current branch is a foundation, not complete coverage. The next high-value
hooks are:

1. tactical suppression, smoke, flank and retreat candidate scoring
2. target selection and NCTH/optic calculation traces
3. squad-level objectives and cooperation requests
4. tactical casualties, ammunition and grenade expenditure
5. strategic formation creation/split/merge/destruction
6. strategic objectives beyond reinforcement
7. battle reinforcement entry timing
8. save/load continuity markers
9. parameter snapshots so a result can be reproduced against the exact AI,
    NCTH, weapon, optics, morale and suppression configuration

These should extend the same event schema rather than introduce separate log
files for each subsystem.
