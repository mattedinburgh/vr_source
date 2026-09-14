# CQB / Building AI — canonical dormant staging

Canonical branch: `install/all-2026-09-12`

Status: **COMPILED / RUNTIME DISABLED**

The former `inactive/cqb-building-doctrine-2026-09-14` implementation has been forward-ported onto the canonical
branch so the code is no longer stranded on a second AI line.

## Safety gate

`VRCQB_IsRuntimeEnabled()` must return `FALSE` until an explicit activation review.

There are currently:

- no `DecideAction` call sites;
- no returned CQB-specific `AI_ACTION_*`;
- no automatic CQB smoke/suppression task;
- no destructive breaching;
- no window-entry action layer;
- no CQB savegame state.

Compiling this module must therefore have no gameplay effect.

## What is staged

The module contains knowledge-safe building/CQB assessment for:

- ASSAULT;
- HOLD;
- DELAY_FALLBACK;
- COUNTERATTACK;
- SECURE.

It reuses the canonical AI architecture:

- `AIGetDoctrineProfile`;
- `AIHasLocalCommandSupport`;
- `AICompetenceTier`;
- `AIPlannerReliability`;
- `AIKnownThreatExposure`;
- existing morale/risk/disengagement systems;
- current personal/public opponent knowledge.

It does **not** create a second fireteam system or a second combat AI.

## Training model

Profiles are behavioural capability bands, not combat-stat bonuses:

- SECURITY_BASIC;
- LINE_BASIC;
- LINE_COMMANDED;
- VETERAN;
- ELITE_MOBILE;
- ELITE_GUARD.

They control planning sophistication such as threshold awareness, alternate-entry reasoning, sector
deconfliction, defense in depth and local counterattack permission.

## Performance bounds

The dormant planner is intentionally local:

- local position scan radius: 6 tiles;
- entry search radius: 8 tiles around the known indoor threat;
- no full-map room graph;
- no second global pathfinder;
- transient plan memory only.

## Activation sequence

Do not activate the complete system at once.

1. Keep compiled and disabled while canonical builds are validated.
2. Add `VRAnalytics` shadow telemetry.
3. Run diagnostic-only shadow evaluation while existing AI still acts.
4. Compare shadow decisions with Black Box/Companion battle outcomes.
5. Activate HOLD / SECURE first.
6. Activate building-aware fallback.
7. Activate ASSAULT only after stable results.
8. Activate local counterattack last.

Any activation must enter the normal canonical priority hierarchy in `DecideAction.cpp`. CQB must never
become a parallel top-level decision engine.

## Branch status

The old CQB branch is now archaeology/reference only. New CQB work starts from
`install/all-2026-09-12` and modifies the canonical module directly or through a short-lived branch that
returns to canonical.
