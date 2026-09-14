# Vengeance Unified AI Framework

Canonical branch: `install/all-2026-09-12`

Status: **ACTIVE / SINGLE SOURCE OF TRUTH**

## Purpose

This document freezes the AI architecture and branch policy after the 2026-09-14 consolidation review.

The playable branch `install/all-2026-09-12` is the only canonical integration line for tactical AI. Older
`ai/*`, `integration/*ai*`, and `final-human-ai-*` branches are reference/archaeology unless a future
review proves that they contain a behaviour not present in the canonical branch.

No older AI branch is to be merged wholesale into the canonical branch.

## Consolidation finding

The consolidation review compared the canonical branch against the major AI branches.

### Fully subsumed / ancestor branches

These have no commits ahead of the canonical branch and are already integrated by ancestry:

- `ai/ap-budgeting`
- `ai/combat-dispersion`
- `ai/combat-medic-rescue`
- `ai/covering-fire-cooperation`
- `ai/emergency-casualty-smoke`
- `ai/fireteam-cohesion`
- `ai/individual-self-preservation`
- `ai/local-advance-cooperation`
- `ai/no-weapon-self-preservation`
- `ai/range-aware-positioning`
- `ai/search-confidence-decay`
- `ai/support-aware-withdrawal`
- `ai/target-allocation`
- `ai/wound-self-preservation`
- `ai/wounded-tactical-withdrawal`

### Diverged but functionally superseded

These branches are historically useful but their AI functions are already represented by newer code on the
canonical branch:

- `ai/deidranna-doctrine`
- `ai/legacy-core-modernization`
- `ai/shared-enemy-militia-brain`
- `ai/team-coordination`
- `ai/utility-squad-planner`
- `ai/radio-support-doctrine`
- `ai/human-tactical-final`
- `final-human-ai-modern-113`
- `integration/unified-ai-fireteams-doctrine-2026-09-14`
- `integration/unified-ai-framework-2026-09-14`

The old unified-framework branch must **not** replace the canonical branch: it was built from an older base and
is substantially behind the playable integration line. Its useful concepts are already present in newer form in
the canonical code.

## Canonical tactical architecture

The canonical AI is one layered decision system, not a collection of independent brains.

### 1. Legal perception

AI reasoning may use:

- personal/public JA2 knowledge;
- current sight and legitimate heard information;
- known contacts and legally disseminated reports;
- locally observable casualties, suppression and friendly state.

It must not gain hidden CTH/AP bonuses or exact unseen information as a substitute for competence.

### 2. Doctrine and competence

Doctrine/competence affects:

- whether complex plans are attempted;
- planner reliability and execution friction;
- command/support behaviour;
- local initiative.

It does **not** directly grant magical shooting, AP, sight or weapon-performance bonuses.

Canonical interfaces include:

- `AIGetDoctrineProfile`
- `AIGetCommandRank`
- `AICommandAuthority`
- `AIHasLocalCommandSupport`
- `AIAllowsPlanComplexity`
- `AIAllowsIndependentFlank`
- `AIAllowsProactiveSupport`

### 3. Persistent local organization

Enemy/militia combatants are organized into transient sector-local fireteams.

Canonical interfaces include:

- `AIFireteamId`
- `AIFireteamAliveCount`
- `AIFireteamCombatReadyCount`
- `AISameFireteam`
- `DecideFireteamCohesionAction`

The canonical branch additionally contains newer remnant absorption, regrouping, fixed-mission, reserve and
response-control logic. Older fireteam branches are therefore reference-only.

### 4. Battle state and self-preservation

The AI continuously reasons about:

- local stress;
- personal risk vs risk tolerance;
- perceived friendly/enemy strength;
- casualty pressure;
- isolation;
- suppression;
- route exposure;
- hopeless odds;
- disengagement/escape state.

Canonical interfaces include:

- `AILocalStress`
- `AIPersonalRisk`
- `AIPersonalRiskTolerance`
- `AIBattleSituation`
- `AIShouldAvoidAdvance`
- `AIShouldStartDisengagement`
- `DecideDisengagementAction`
- `DecideTacticalFallback`
- `DecideHopelessSurvivorAction`
- `AIKnownRouteExposureAcceptable`

### 5. Tactical intent and roles

The planner uses a shared tactical model:

Intents:

- HOLD
- PRESS
- FLANK
- FALLBACK
- DISENGAGE
- RESCUE

Roles:

- SUPPORT
- MANEUVER
- FLANKER
- SCREEN
- RESERVE

Canonical interfaces include:

- `AITacticalIntent`
- `AITacticalRole`
- `AIUtilityPositionScore`
- `AIPathExposureCost`
- `AIEngagementRangeModifier`
- `AIAdvanceHasMutualSupport`

### 6. Team fire plan

The AI coordinates rather than letting every soldier independently chase the same local optimum.

Canonical mechanisms include:

- target saturation control;
- covering fire;
- support-aware advance;
- withdrawal cover;
- alternate arcs/crossfire;
- response limits and reserve retention;
- bounded reinforcement of local contacts.

Canonical interfaces include:

- `AITargetSaturation`
- `AIFriendNeedsCoveringFire`
- `AIFriendAdvancingNeedsCover`
- `AIFriendWithdrawingNeedsCover`
- `AIShouldHoldForWithdrawingFriend`
- `AICrossfirePositionScore`

### 7. Casualty handling

Casualty behaviour is part of the same priority system, not a separate medic AI.

Canonical order is intentionally coordinated across alert states:

1. emergency protection / dispersion where required;
2. fireteam cohesion;
3. persistent disengagement;
4. suppression response;
5. safe medic casualty response;
6. emergency self-aid;
7. non-medic casualty response / buddy aid;
8. ordinary offensive behaviour.

Canonical entry point:

- `DecideCombatCasualtyResponse`

Older branch-specific routines such as a separate `DecideCombatMedicRescue` path are superseded by the
canonical casualty-response path.

### 8. Legacy Vengeance / 1.13 behaviour

Legacy AI is retained as the execution library.

The rule is:

> Unified planner decides **whether/when/why** an action is appropriate; legacy code executes the legal action.

A legacy heuristic must not independently re-trigger a behaviour already controlled by the canonical planner.
Emergency/survival rules may pre-empt ordinary planning.

## Canonical decision priority

The current integration intentionally gives high-priority survival/team-state decisions the opportunity to
pre-empt lower-priority opportunistic actions.

Important ordering relationships:

- forced/manual retreat remains authoritative across alert-state changes;
- emergency smoke/dispersion can pre-empt ordinary movement;
- shattered fireteams may regroup/reattach before ordinary attack movement;
- persistent disengagement outranks attack setup;
- suppression response is blocked while active disengagement is in control;
- casualty response outranks opportunistic sniper/mortar/support actions when viable;
- combat-team panic movement does not fall through to the old generic civilian RUN_AWAY path.

When adding a new behaviour, it must be inserted into this priority model instead of being called independently
from multiple unrelated locations.

## Analytics / Black Box integration

The canonical branch already uses the shared `VRAnalytics` stream for planner decisions.

Planner adapters in `TacticalAI/DecideAction.cpp` include:

- `VRPlannerTraceBeginDecision`
- `VRPlannerTraceCandidate`
- `VRPlannerTraceReject`
- `VRPlannerTraceSelect`

Do not restore the older separate `AI Diagnostics.cpp` / standalone tactical TSV implementation from the old
unified branch. That would recreate duplicate diagnostics and another source of truth.

New AI behaviour should record enough state to answer:

- What did the actor legally know?
- What intent/role was active?
- What candidates were considered?
- Why were candidates rejected?
- What action was selected?
- Did competence/doctrine friction alter the choice?
- What was the outcome?
- Which subsystem drove the decision?

## Strategic AI staging

Strategic modernization is tracked separately from the active tactical AI because it depends on savegame/group-state
changes that are not yet safe to activate on the playable branch.

The historical branches `inactive/strategic-modernization`,
`integration/unified-strategic-companion-2026-09-14`, and
`consolidation/install-all-2026-09-14` are classified as **inactive staging/archaeology**, not alternative active
integration lines. Their tactical AI is superseded by the canonical branch; their genuinely unique strategic work
is catalogued in `Strategic/STRATEGIC_AI_STAGING_MANIFEST.md` and must be forward-ported from the current canonical
base when resumed.

## Branch policy

1. All completed AI work ends on `install/all-2026-09-12`.
2. Short-lived experimental branches are allowed, but they are not permanent integration lines.
3. A new AI branch must start from the current canonical branch.
4. Never merge an old AI branch wholesale merely because its name sounds newer.
5. Port only behaviour proven to be unique and still desirable.
6. If the canonical branch already has a newer implementation of the same behaviour, keep the canonical one.
7. New AI systems must use the shared planner state and `VRAnalytics`; do not create parallel decision engines,
   parallel fireteam state, or parallel Black Box formats.
8. Before declaring an AI feature complete, verify:
   - code is on the canonical branch;
   - it compiles;
   - its priority relative to survival/retreat/casualty behaviour is deliberate;
   - its information use is legal;
   - its decisions are observable in analytics;
   - it has no duplicate legacy trigger that can independently fire the same behaviour.

## Future cleanup

Old AI branches should remain available for archaeology until their useful ideas have been catalogued, but they
are **inactive**. Their existence must not be interpreted as unfinished integration.

Any future audit should compare candidate branches against `install/all-2026-09-12` at the function/behaviour
level, not by branch age or branch name.
