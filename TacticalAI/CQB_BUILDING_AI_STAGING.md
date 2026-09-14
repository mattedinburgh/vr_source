# CQB / Building AI — canonical live integration

Canonical branch: `install/all-2026-09-12`

Status: **LIVE / RUNTIME ENABLED**

The former `inactive/cqb-building-doctrine-2026-09-14` branch is now archaeology/reference only.
Its implementation has been forward-ported into the single canonical AI line and activated after
explicit user approval on 2026-09-14.

## Runtime contract

`VRCQB_IsRuntimeEnabled()` returns `TRUE`.

CQB is not a parallel top-level AI. It is called from the normal canonical tactical hierarchy:

- RED: only after emergency smoke, dispersion, cohesion, disengagement, suppression response,
  tactical fallback, self-aid and casualty response have had first refusal;
- BLACK: only after normal attack selection/arbitration has failed to produce an executable,
  desirable immediate attack.

This preserves the rule that survival and a good shot outrank building choreography.

## Active states

The live module can produce building-aware movement for:

- ASSAULT;
- HOLD;
- DELAY_FALLBACK;
- COUNTERATTACK;
- SECURE.

Movement uses existing JA2 actions (`SEEK_OPPONENT`, `TAKE_COVER`, `WITHDRAW`) rather than
inventing a second action engine.

## Training model

CQB behaviour remains competence-dependent rather than stat-cheated:

- SECURITY_BASIC — noticeably better at guarding than clearing;
- LINE_BASIC — basic buddy/entry discipline;
- LINE_COMMANDED — simple coordinated assault/support/security;
- VETERAN — stronger sector discipline, replanning and alternate-entry reasoning;
- ELITE_MOBILE — strongest assault/room-flow profile;
- ELITE_GUARD — strongest depth/strongpoint profile.

No CQB profile grants hidden CTH, AP, perception, damage or reaction bonuses.

## Coordination

The live adapter uses short-lived fireteam-local plan reservations so excess movers do not all queue
through the same entry. When the mover budget is full, another soldier is redirected toward a local
support/hold position instead of joining the doorway stack.

## Knowledge and performance

The planner remains knowledge-safe:

- opponent geometry comes from personal/public known locations;
- no unseen live position, stance, AP, health or equipment is read.

Performance remains bounded:

- local position scan radius: 6 tiles;
- entry search radius: 8 tiles around the known indoor threat;
- cheap first-pass scoring;
- detailed exposure/crossfire scoring only for shortlisted candidates;
- no full-map room graph;
- no second global pathfinder.

## Black Box / Companion

Every live CQB assessment can write to the canonical `VRAnalytics` stream, including:

- state and role;
- doctrine/training profile;
- reason;
- room/building and entry;
- known-threat count;
- local support;
- confidence/reliability/effective skill;
- selected movement;
- current/desired score;
- route rejection reasons.

This is intended for after-battle tuning with the Black Box/Companion workflow.

## Not yet part of CQB

Activation does **not** add:

- destructive breaching logic;
- window-entry choreography;
- automatic grenade-as-default room clearing;
- extra smoke inventory;
- new savegame state;
- a second fireteam or perception system.

Those remain separate future refinements.

## Branch status

All new active CQB work belongs on `install/all-2026-09-12`.
The old inactive CQB branch is frozen historical reference and must not become a competing AI line.
