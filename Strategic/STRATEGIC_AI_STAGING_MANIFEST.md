# Strategic AI staging manifest

Canonical integration branch: `install/all-2026-09-12`

Status: **FOUNDATION FORWARD-PORTED TO CANONICAL; OPERATIONAL ORDER ISSUANCE REMAINS GATED OFF**

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

## Current canonical status

The safe foundation has now been forward-ported directly onto `install/all-2026-09-12`:

- save-compatible team ownership bridge for strategic groups;
- persistent enemy formation identity/state stored inside former raw-save padding;
- explicit 29-byte `ENEMYGROUP` compile guard;
- byte-pair storage for 16-bit formation IDs/flags so alignment cannot enlarge legacy saves;
- persistent mission, reserve role, supply, morale, contact confidence and retreat state;
- local/degrading operational intelligence with no target-scoring access to live player positions;
- auditable target scoring for ownership, garrisons, towns, mines, SAM sites, distance and known force risk;
- tactical map-edge escape -> persistent strategic retreat formation handoff;
- existing pursuit/PBI/autoresolve path retained, with legacy static-counter fallback on allocation failure;
- RETREAT -> REGROUP -> RESERVE recovery lifecycle;
- movement supply consumption, friendly-hub resupply and morale recovery/erosion;
- pure reserve-readiness and reserve-selection queries;
- a compiled but runtime-OFF reserve-release hook ahead of palace reinforcement spawning;
- shared `VRAnalytics` / Black Box telemetry only;
- integrity checks enforcing save layout, knowledge fairness, retreat handoff and the OFF movement gate.

The operational decision gate remains:

```cpp
#define VR_OPERATIONAL_DECISION_LOOP_ENABLED 0
```

Thus the new state/intelligence/scoring systems are active foundations, but they do **not** yet replace the
legacy Queen as the authority that issues strategic movement orders.

## Historical staging still not integrated

The historical branches remain useful only for optional/advanced consumers not yet forward-ported:

- transport groups / convoys;
- enemy helicopters;
- ASD purchasing/asset logic;
- broader operational mission issuance beyond the prepared reserve-reinforcement hook.

Several historical prototype choices were deliberately **not** copied:

- no random/time-derived formation IDs;
- no global `PlayerMercsInSector()` intelligence broadcast;
- no separate `Strategic Operational BlackBox.txt`;
- no wholesale replacement of current Strategic Movement / Strategic AI files;
- no second strategic AI integration branch.

## Why the remaining prototype is not copied wholesale

The remaining historical code was created from a much older repository base. Blindly replacing current
`Strategic Movement.*`, `Strategic AI.*`, or project files would undo newer canonical retreat, pursuit,
autoresolve, analytics and build work. Only audited concepts may be forward-ported into current files.

## Forward-integration sequence

1. **Team-group compatibility bridge — DONE**
   - old-save normalization and unchanged GROUP byte footprint.

2. **Persistent formation state — DONE**
   - deterministic identity, mission, reserve role, supply/morale and intel snapshot;
   - raw-save layout guarded at compile time.

3. **Operational intelligence — FOUNDATION DONE**
   - observer-local contact reports;
   - hourly confidence decay;
   - target scorer prohibited from querying live player/militia presence.

4. **Retreat/regroup/reserve lifecycle — FOUNDATION DONE**
   - tactical edge escape becomes a persistent remnant formation;
   - pursuit/autoresolve behavior preserved;
   - uncontested escape completes into REGROUP;
   - fresh-contact and morale recovery gate later RESERVE readiness.

5. **Operational mission issuance — PREPARED, OFF**
   - target recommendations are advisory;
   - reserve selection is pure;
   - high-priority garrison reserve release is compiled behind
     `VR_OPERATIONAL_DECISION_LOOP_ENABLED == 0`;
   - palace spawning remains the fallback.

6. **Strategic telemetry — DONE**
   - canonical shared `VRAnalytics` only.

7. **Optional modernization consumers — NOT STARTED**
   - transport groups first;
   - enemy helicopters second;
   - ASD purchasing last.

8. **Activation requirement**
   - obtain a clean Release Win32 integration build;
   - validate save/load, pursuit, autoresolve and simultaneous-arrival behavior;
   - then enable only the narrow reserve-reinforcement consumer before considering broader mission control.

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
