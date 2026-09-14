# CQB / Building AI — canonical integration

Canonical branch: `install/all-2026-09-12`

Status: **ACTIVE / UNIFIED TACTICAL SUBSYSTEM**

The former `inactive/cqb-building-doctrine-2026-09-14` work has been forward-ported and integrated into the
canonical tactical AI. The old branch is frozen archaeology; it is not an alternative runtime AI.

## Ownership and priority

`DecideAction.cpp` remains the top-level decision owner.

`CQBBuildingDoctrine.cpp` is a bounded building-specific planner invoked only from the canonical RED/BLACK
decision functions.

- **RED:** CQB is considered only after emergency protection, dispersion, fireteam cohesion, disengagement,
  suppression and casualty response.
- **BLACK:** immediate viable combat attacks remain senior. CQB is considered only after no executable/desirable
  direct attack is selected, and before generic cover/approach movement.
- CQB is suppressed during hidden/active interrupts.
- The adapter only runs for eligible ENEMY_TEAM soldiers under the new tactical-AI behavior.

This prevents CQB from becoming a second top-level AI brain.

## Canonical interface

- `VRCQB_IsRuntimeEnabled`
- `VRCQB_BuildContext`
- `VRCQB_Assess`
- `VRCQB_DecideAction`
- `VRCQB_ScorePosition`

There must be exactly one public declaration and one implementation of the runtime adapter.

## Building states

The planner handles:

- ASSAULT;
- HOLD;
- DELAY_FALLBACK;
- COUNTERATTACK;
- SECURE.

It uses the normal canonical systems rather than duplicating them:

- doctrine and command;
- fireteam identity;
- competence/reliability;
- risk and disengagement;
- known-threat exposure;
- route-exposure validation;
- personal/public opponent knowledge.

It does not create a second fireteam model, perception model, morale model, or pathfinder.

## Competence model

Behavioral profiles are:

- SECURITY_BASIC;
- LINE_BASIC;
- LINE_COMMANDED;
- VETERAN;
- ELITE_MOBILE;
- ELITE_GUARD.

These change planning sophistication and error rates only. They do not grant hidden CTH, AP, perception or
weapon-performance bonuses.

## Movement discipline

The planner is deliberately bounded:

- local position scan radius: 6 tiles;
- entry search radius: 8 tiles around the known indoor threat;
- no full-map room graph;
- no second global pathfinder;
- transient plan memory;
- fireteam mover budget to avoid doorway queues;
- final routes pass `AIKnownRouteExposureAcceptable`;
- veteran/mobile elements with `VRCQB_CAP_ALTERNATE_ENTRY` may use an adjacent jumpable window when it is a better alternate entry than the doorway approach;
- commanded/experienced elements with `VRCQB_CAP_PROACTIVE_SUPPORT` may place smoke on an exposed entry before committing movers, while preserving a local smoke reserve when not already under fire.

## Analytics

CQB uses the same `VRAnalytics` decision stream as the rest of the unified AI. It records state, role,
training profile, confidence, target/entry/fallback geometry, risk, candidates and committed decisions.

Do not add a separate CQB log or Black Box.

## Extension rules

Future CQB work must stay inside the unified architecture.

- destructive breaching and additional room-clearing actions must enter through the existing priority chain;
- window entry and proactive entry smoke are now active subordinate CQB actions and must remain competence-gated;
- do not bypass survival, disengagement, casualty or immediate-attack priorities;
- do not create separate squad state;
- use legal known information only;
- use the shared analytics stream;
- validate with Black Box/Companion battle outcomes before increasing aggressiveness.

## Historical branch

`inactive/cqb-building-doctrine-2026-09-14` is frozen at its archived tip. It may be used for archaeology only.
All new CQB development starts from and returns to `install/all-2026-09-12`.

## Advanced entry actions

The canonical planner now has two active advanced entry actions in addition to normal movement:

- **Proactive entry smoke:** only training profiles carrying `VRCQB_CAP_PROACTIVE_SUPPORT` may deliberately smoke an exposed entry. The normal grenade trajectory/AP calculation remains authoritative, water/already-smoked entry tiles are rejected, and scarce smoke is retained unless the element is already under fire.
- **Alternate window entry:** only profiles carrying `VRCQB_CAP_ALTERNATE_ENTRY` may deliberately select a jumpable adjacent window. Candidate windows are scored against the desired foothold/building and known-threat exposure; the actor faces the selected opening before the native `AI_ACTION_JUMP_WINDOW` executor is used.

Basic security and ordinary uncommanded line troops therefore continue to use simpler doorway/cover behaviour rather than receiving expert room-entry tactics for free.
