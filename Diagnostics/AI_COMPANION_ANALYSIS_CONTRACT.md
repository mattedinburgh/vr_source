# AI Companion Daily Analysis Contract

Schema version: **1**
Framework: **Vengeance Unified AI Framework**

## Working relationship

The Companion is the developer-facing and analyst-facing explanation layer for the AI.
The Black Box is the raw forensic event stream. They are designed together.

At the end of each play day the player can provide the generated logs. The analysis must be
possible without guessing from screenshots or relying on remembered intentions.

## Required daily bundle

Keep these files stable and append-only within a play session:

1. `Campaign Tactical Black Box.tsv`
   - physical outcomes: moves, shots, hits, misses, damage, suppression, smoke, explosions,
     casualties, battle start/end and team summaries.
2. `Campaign Tactical Decisions.tsv`
   - AI reasoning: decision id, knowledge snapshot, competence, intent, role, candidate
     actions, utility components, rejections, selection and execution.
3. `Campaign AI Black Box.tsv`
   - strategic decisions, candidates, weights, plans and outcomes.
4. `Campaign AI Companion.txt`
   - human-readable narrative linking strategic and tactical events.

The daily review should also identify the exact framework/schema version in every header.

## Tactical decision contract

Every material decision should be reconstructable by a stable `decision_id`.

### DECISION_BEGIN
Record:
- battle/session/time/sector/turn
- actor id/team/class
- competence tier + execution reliability
- alert status / morale
- life/AP/breath/shock
- perceived battle situation
- personal risk / tolerance
- local stress / rout pressure
- tactical intent
- fireteam role
- target / last-known target
- known-threat exposure
- nearby friendly support

### CANDIDATE
For each shortlisted material action record:
- action type
- destination or target
- total utility
- cover contribution
- exposure contribution
- route-exposure cost
- crossfire contribution
- range contribution
- friendly-support contribution
- crowding/grenade-risk contribution
- smoke contribution
- inferred interrupt/reaction-fire risk
- target saturation / fire-allocation state
- competence/friction modifier
- legality state

Do not log thousands of raw path tiles. Log the shortlisted candidates and the selected
route's aggregate cost.

### REJECT
Record explicit rejection reason, e.g.:
- illegal/no AP
- hidden-information boundary
- route too exposed
- no mutual support
- role reserved by teammate
- target already saturated
- smoke too scarce
- risk exceeds tolerance
- competence too low for plan complexity
- retreat/disengagement has precedence
- friendly-fire risk
- no useful firing solution

### SELECT
Record:
- selected action + target/destination
- final score
- runner-up action + score
- score margin
- whether competence friction changed the mathematical best choice
- whether the decision came from hard emergency logic, planner utility, or legacy fallback

### EXECUTE
Record whether the planned action actually began and, if not, why it failed.

### OUTCOME
Link back to `decision_id` where practical:
- movement completed / interrupted / aborted
- shot hit/miss/suppression generated
- smoke created useful concealment
- casualty rescued / rescue abandoned
- fallback reduced exposure
- flank created crossfire
- unit was hit/killed during or soon after action
- contact lost/gained
- plan expired or changed

## Strategic decision contract

Every strategic `plan_id` must link to:
- formation identity/composition/command quality
- mission
- source/target
- supply/morale
- intelligence source, age and confidence
- candidate objectives and score breakdown
- selected objective and runner-up
- communication path/delay for new intelligence
- reserve/reinforcement state
- outcome: arrived, fought, reinforced, retreated, regrouped, destroyed, mission changed.

## Daily derived metrics

The Companion developer should make these easy to derive from logs.

### Tactical
- deaths / serious wounds per battle and per 100 enemy turns
- shots, hits, damage and suppression by role
- smoke used vs smoke available; last-smoke usage
- flank attempts / successful crossfires / flank casualties
- exposed-route moves and casualties during those moves
- fallback success rate: exposure before vs after
- disengagement/escape success
- medic rescue attempts / saves / medic casualties
- target overkill and under-allocation
- percentage of actions changed by competence friction
- administrator / regular / elite decision quality separately
- search-after-lost-contact efficiency
- door/CQB casualties and congestion
- reinforcement response delay and number responding

### Strategic
- objective changes and reasons
- stale/incorrect intelligence impact
- reinforcement travel time
- reserve depletion
- formation losses, retreat frequency and recovery
- supply-related mission changes
- command-quality error rate
- number of plans changed after tactical outcomes

## What the daily analyst will look for

The daily review should classify findings into:

- **BUG** — implementation malfunction or impossible state.
- **CHEAT** — AI used information/stat advantage it should not possess.
- **THRASH** — contradictory replanning or repeated reversal.
- **OVERCOORDINATION** — behaviour too synchronized for formation quality.
- **UNDERCOORDINATION** — soldiers fail to exploit obvious local cooperation.
- **SUICIDE** — action violated reasonable risk/survival constraints.
- **PASSIVITY** — safe useful action existed but AI stalled.
- **RESOURCE_WASTE** — smoke/grenade/ammo/support asset used with poor utility.
- **TACTICAL_SUCCESS** — good decision worth preserving.
- **BALANCE_ONLY** — logic is sound but threshold/weight needs tuning.
- **STRATEGIC_FRICTION** — command/intel/logistics timing issue.
- **UNKNOWN** — telemetry insufficient; Companion developer must add fields.

## Change discipline

Never tune from a single spectacular event unless it is a deterministic bug or cheat.
Prefer repeated evidence across battles and compare by soldier quality.

For every tuning change record:
- old value / rule
- new value / rule
- hypothesis
- metrics expected to move
- possible adverse interactions
- first build/session containing the change

This creates a causal history between AI revisions and campaign outcomes.
