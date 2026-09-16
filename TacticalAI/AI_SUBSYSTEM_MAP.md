# Vengeance AI Subsystem Map

Canonical branch: `install/all-2026-09-12`

This file is the maintenance map for the active AI. It exists to prevent the same behaviour from being
implemented in several places or on several permanent branches.

## Source ownership

| Area | Canonical source | Responsibility |
| --- | --- | --- |
| Public AI interface | `TacticalAI/ai.h` | Declarations only; no second public AI header |
| Shared evaluation/state | `TacticalAI/AIUtils.cpp` | Doctrine, command, fireteams, risk, battle state, route exposure, compatibility utility wrapper |
| Shared tactical reasoning | `TacticalAI/TacticalReasoning.cpp` | Legal contact beliefs, decaying visual memory, anonymous sound corroboration, 8-sector local battlefield geometry, battle-local setback memory, spatial feature evaluation/scoring, task reservations, short plans, contact-change/surprise tracking |
| Decision orchestration | `TacticalAI/DecideAction.cpp` | Alert-state entry points, planner priority, suppression/contact-surprise responses, planner analytics adapters |
| Casualty/medical AI | `TacticalAI/Medical.cpp` | Evacuation, medic rescue, buddy aid, self-aid |
| Attack evaluation/execution helpers | `TacticalAI/Attacks.cpp` | Attack candidates and weapon-use execution support |
| Movement candidate generation | `TacticalAI/FindLocations.cpp` | Cover/advance/retreat/flank search plus weakest-sector geometry breakout search |
| CQB/building planner | `TacticalAI/CQBBuildingDoctrine.cpp/.h` | Building context/utility/action adapter; consumes shared geometry, entry reservations and CQB short-plan state; top-level priority remains owned by `DecideAction.cpp` |
| Shared analytics | `VRAnalytics.cpp`, `VRAnalytics.h` | Tactical + strategic Black Box event stream |
| Architecture policy | `UNIFIED_AI_FRAMEWORK.md` | Single-source architecture and branch rules |
| Strategic staged work | `Strategic/STRATEGIC_AI_STAGING_MANIFEST.md` | Inactive strategic-AI forward-port plan |
| Integrity checks | `Tools/AI/VERIFY_AI_INTEGRITY.ps1` | Duplicate definitions, stale alternate paths, conflict markers |
| Branch checks | `Tools/AI/VERIFY_AI_BRANCH_CONSOLIDATION.ps1` | Detect new permanent/divergent AI integration lines |

## One owner per behaviour

A behaviour may call helpers from several files, but it has exactly one orchestration owner.

| Behaviour | Orchestration owner |
| --- | --- |
| Doctrine / competence | `AIUtils.cpp` |
| Command / rank | `AIUtils.cpp` |
| Fireteam identity/cohesion | `AIUtils.cpp` |
| Tactical intent / role | `AIUtils.cpp` (persistent posture/role) + `TacticalReasoning.cpp` (short-plan/task state) |
| Contact belief / uncertainty | `TacticalReasoning.cpp` |
| Contact memory / sound evidence fusion | `TacticalReasoning.cpp`; `Knowledge.cpp` supplies only legitimate personal/public noise observations |
| Local battlefield geometry | `TacticalReasoning.cpp` (threat/friendly sectors, remembered/corroborated danger, open flanks, safest direction, encirclement) |
| Geometry-aware breakout search | `FindLocations.cpp::FindGeometryBreakoutSpot` |
| Position utility / spatial features | `TacticalReasoning.cpp`; `AIUtils.cpp::AIUtilityPositionScore` is the compatibility wrapper |
| Battle-local setback memory | `TacticalReasoning.cpp`; producers register only high-confidence local setbacks, shared position/route scoring consumes them |
| Task reservations | `TacticalReasoning.cpp` |
| Short tactical plans | `TacticalReasoning.cpp` |
| Contact surprise / encirclement reassessment | `DecideAction.cpp` (decision) + `TacticalReasoning.cpp` (legal observation state) |
| Disengagement / escape state | `DecideAction.cpp` (decision) + `AIUtils.cpp` (state/helpers) |
| Suppression response | `DecideAction.cpp` |
| Alert-state priority | `DecideAction.cpp` |
| Flank decision gate | `DecideAction.cpp`; `TacticalReasoning.cpp::AIFireteamPreferredFlankAction` owns shared left/right attack-axis preference |
| Building/CQB behavior | `DecideAction.cpp` (priority) + `CQBBuildingDoctrine.cpp` (building planner); shared task/short-plan primitives remain owned by `TacticalReasoning.cpp` |
| Casualty response | `Medical.cpp` through `DecideCombatCasualtyResponse` |
| Medic rescue | `Medical.cpp` |
| Buddy aid | `Medical.cpp` |
| Emergency self-aid | `Medical.cpp` |
| Decision telemetry adapters | `DecideAction.cpp` -> `VRAnalytics` |

Do not add a second implementation in another file merely to make a feature easier to call. Add a helper if
needed, but keep one authoritative decision path.

## Required priority structure

High-priority state can pre-empt ordinary combat:

1. forced/manual retreat;
2. immediate environmental/emergency protection;
3. dispersion where required;
4. newly revealed-contact / encirclement reassessment;
5. fireteam cohesion / remnant handling;
6. persistent disengagement / escape;
7. suppression response;
8. viable casualty response;
9. tactical fallback / self-preservation;
10. immediate viable attack in direct BLACK contact;
11. building-aware CQB movement when context applies;
12. generic coordinated attack/support/movement and legacy movement execution.

The exact details may evolve, but a new behaviour must be deliberately placed in this hierarchy.

## Legacy code rule

Vengeance/1.13 AI remains valuable as an execution library.

- Planner/state code decides **whether and why**.
- Shared reasoning owns legal beliefs, reusable spatial features, reservations and short-lived plans.
- Legacy helpers decide **how to execute a legal action**.
- A legacy path must not independently trigger the same high-level behaviour after the planner rejected it.
- Generic civilian panic code must not become a second retreat path for combat teams.

## Telemetry rule

There is one Black Box decision stream: `VRAnalytics`.

The tactical planner adapters are:

- `VRPlannerTraceBeginDecision`
- `VRPlannerTraceCandidate`
- `VRPlannerTraceReject`
- `VRPlannerTraceSelect`

Do not restore the old standalone `AI Diagnostics.cpp/.h` stream or create another tactical decision log.

## Branch rule

Active development starts from and returns to `install/all-2026-09-12`.

Short-lived feature branches are allowed. Permanent AI integration branches are not.

Historical AI branches are archaeology/reference only unless a function-level review proves a unique behaviour is
missing from the canonical source.

## Change checklist

Before merging an AI change:

1. Identify its single orchestration owner.
2. Confirm it does not duplicate an existing decision path.
3. Confirm it uses only legal AI information.
4. Place it deliberately in the decision priority.
5. Reuse `VRAnalytics`; material position choices must expose candidate/selection evidence.
6. Reuse `TacticalReasoning.cpp` for beliefs, spatial scoring, task claims, short plans and setback memory instead of creating parallel state.
7. Run `Tools/AI/VERIFY_AI_INTEGRITY.ps1`.
8. Run `Tools/AI/VERIFY_AI_BRANCH_CONSOLIDATION.ps1` when branch topology changed.
9. Complete the canonical integration build.
10. Use Black Box/Companion results for behavioural tuning instead of adding parallel heuristics.
