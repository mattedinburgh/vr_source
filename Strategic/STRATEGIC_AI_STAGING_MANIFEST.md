# Strategic AI staging manifest

Canonical integration branch: `install/all-2026-09-12`

Status: **TRACKED, INACTIVE, NOT PART OF THE CURRENT PLAYABLE AI**

## Why this exists

The tactical AI consolidation review found several strategic-AI branches whose tactical code is superseded by the
canonical branch, but whose strategic modernization work is genuinely not yet present in the playable source.

This is deliberate staging, not a second active AI brain.

The relevant historical branches are:

- `inactive/strategic-modernization`
- `integration/unified-strategic-companion-2026-09-14`
- `consolidation/install-all-2026-09-14`
- `diagnostics/strategic-campaign-blackbox`
- `diagnostics/strategic-campaign-companion`

## Unique staged work

The historical strategic branches contain preparation for:

- team-owned strategic groups / compatibility bridge;
- persistent enemy formation identity;
- operational missions and reserve roles;
- formation supply and morale;
- delayed/degraded operational intelligence;
- target scoring for towns, mines, SAM sites, ownership and distance;
- retreat -> regroup -> reserve lifecycle;
- staged transport groups / convoys;
- staged enemy helicopters;
- staged ASD purchasing/asset logic;
- strategic decision telemetry.

The core operational-AI prototypes include interfaces such as:

- `VR_EnsureEnemyFormationState`
- `VR_SetFormationMission`
- `VR_SetFormationReserveRole`
- `VR_ReportOperationalIntel`
- `VR_ScoreOperationalTarget`
- `VR_FindBestOperationalTarget`
- `VR_OnEnemyGroupAssigned`
- `VR_OnEnemyGroupArrived`
- `VR_OnEnemyGroupRetreated`
- `VR_HourlyOperationalUpdate`

## Why it is not compiled into the canonical branch yet

The strategic prototype is **not safe to forward-copy as active code**.

It depends on structural changes that are absent from the current canonical branch, including operational fields
inside strategic enemy-group state and the staged team-group compatibility bridge. The old strategic branch was
also created from a much older repository base.

Blindly replacing current `Strategic Movement.*`, `Strategic AI.*`, or project files with those historical
versions would undo newer canonical changes and create savegame/serialization risk.

Therefore:

1. tactical AI from those branches is treated as superseded;
2. strategic modernization is explicitly classified as **inactive staged work**;
3. old branches remain archaeology/reference until the prerequisites are ported forward;
4. no strategic prototype may silently become a second active AI integration line.

## Required forward-integration sequence

When strategic modernization is resumed, port it into `install/all-2026-09-12` in this order:

1. **Team-group compatibility bridge**
   - audit raw `fPlayer` assumptions;
   - preserve save size/layout where possible;
   - prove old-save compatibility.

2. **Persistent formation state**
   - formation ID;
   - mission;
   - reserve role;
   - supply/morale;
   - operational intel snapshot.

3. **Operational intelligence**
   - local reports only;
   - confidence degradation with time/distance;
   - no global omniscience.

4. **Operational mission loop**
   - garrison/patrol/recon/attack/reinforce/relieve/intercept;
   - retreat/regroup/reserve;
   - bounded reserve release.

5. **Strategic telemetry**
   - use the canonical shared analytics/Black Box path;
   - do not restore a separate strategic logging architecture if the shared system can represent the decision.

6. **Optional modernization consumers**
   - transport groups first;
   - enemy helicopters second;
   - ASD purchasing last.

7. **Enable one consumer at a time**
   - each remains OFF by default until save/load, autoresolve, reinforcement, retreat and simultaneous-arrival
     regression tests pass.

## Branch policy

The historical strategic branches are classified as archaeology/staging in the AI-fragmentation audit. This
classification means they may remain divergent **only because they are inactive**.

Any new active strategic-AI development must start from `install/all-2026-09-12` and return there. Creating a
new permanent strategic integration branch is not allowed.

## Definition of integrated

Strategic AI is integrated only when:

- its code is forward-ported onto the canonical branch;
- it compiles against the current canonical source;
- save/load compatibility is tested;
- the feature gate is explicit;
- tactical and strategic AI share one analytics model;
- retreat/reinforcement/autoresolve interactions are tested;
- the old staging branch is no longer required to understand or reproduce the behaviour.
