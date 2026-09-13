# Consolidated Vengeance install — 2026-09-12

Use this branch as the single source install candidate:

- Branch: `install/all-2026-09-12`
- Base: corrected `launch/2026-09-12`
- Purpose: one branch containing all substantive work implemented during the Vengeance sessions, with parallel feature branches reconciled manually rather than blindly merged.

## Integrated systems

### Shooting / NCTH / weapons
- Modernized 1.13-aligned NCTH core and fallback tuning.
- Scope-mode selection, close-range optic handling, iron sights, lasers, movement tracking, recoil/counter-force and deviation safeguards.
- AI/militia NCTH parity.
- Weapon reliability probability clamping.
- Throwing-knife off-hand promotion.
- Nonstandard-map throw/launcher range correction.

### Grenades / launchers / explosives
- Current-style throwing trait effects on grenade range, AP and CTH.
- Launcher and mortar accuracy fixes.
- Water/smoke grenade behavior fixes and underwater detonation filtering.
- AI toss feasibility and removal of obsolete visible-tile restriction.
- Full deliberate hand-grenade aiming: up to four aim levels, RMB cycling, AP preview, real AP charge, diminishing accuracy returns, selection preservation and realtime snap throws.
- AI deliberately selects grenade aim based on accuracy/AP efficiency.
- Shaped/door charge fix.

### Tactical AI
- Human-like self-preservation, wounds/morale, fallback, disengagement, escape/rout and surrender/cower behavior.
- Cover/support, suppression, emergency smoke, combat medics, target allocation, range-aware positioning and search variation.
- Knowledge-fair targeting: no hidden position/health/stance/capability leaks from public or stale contacts.
- 1.13-aligned smoke concealment targeting: unseen contacts are not valid direct-fire targets by default (`AI_SHOOT_UNSEEN = FALSE`), and every claimed current contact must also pass a fresh LOS check before aimed fire.
- Fire-and-manoeuvre, local support roles and coordinated withdrawals.
- 6–9-man fireteams with local response, staged reinforcement release and remnant merging.
- Proportional/local reaction instead of whole-sector rush-to-noise behavior.
- Militia tactical parity and strategic retreat behavior.
- Corrected A* safer-cover path scoring.
- Emergency self-aid and adjacent same-fireteam buddy stabilization.
- AI avoids deliberate finishing of visibly downed human opponents and hesitates against visibly active caregivers.
- Extra explosive safety around critical/downed allies.

### Deidranna doctrine
- Security, line, veteran, elite-mobile and elite-guard initiative profiles.
- Training/command-gated complex manoeuvres.
- Doctrine-aware flanking, QRF response, anchoring and combat-medic initiative.
- Elite guards remain mission-anchored; veterans/elites retain greater autonomy.

### Radio / artillery
- Radio operator trait prioritization when a radio set is actually carried.
- Dynamic/mod-aware artillery shell and launcher lookup.
- Knowledge-safe AI artillery target selection.
- Existing Vengeance radio/jamming/error handling retained.
- Artillery remains physically sector-sourced: support is only ordered from eligible adjacent strategic sectors, with direction, troops/mortars/ammunition and cooldowns derived from that firing sector. No magical CAS/airstrike spawn is introduced.

### Downed casualties
- Symmetric player/enemy/militia downed state for eligible human combatants.
- 2–5 full tactical rounds to rescue depending on wound severity.
- Massive overkill remains immediately lethal.
- First aid stabilizes the casualty.
- Stabilized casualties remain incapacitated for the rest of the fight and can recover normally outside combat.
- Battle-end cleanup no longer automatically executes these casualties.
- Player rescuers can face an adjacent downed friendly and press **Backslash** to stabilize and drag them to safety.
- Dragging requires a free hand, forces walking, costs pickup AP to start in combat, and adds a 50% movement-AP burden; press Backslash again to release.
- Drag state is transient and does not consume more SOLDIERTYPE/savegame bytes.
- Save-structure size preserved by consuming existing filler bytes rather than expanding SOLDIERTYPE.

### UI / presentation / fixes
- Softer visibility overlay colours.
- Rain-preserving UI background behavior.
- Scope-mode icon cleanup after EDB close.
- Melee aiming-circle preservation under NCTH.
- Flare throw flash behavior.
- Interrupted throw item return and self-target attack deadlock fixes.
- **Improve gear** restored in the Ctrl+. tactical-functions menu: outside combat, it exchanges worn items for better-condition identical reachable sector items and consolidates/restocks matching magazine stacks.

## Deliberately not claimed as implemented

The following remains deliberately excluded:
- A separate CAS/airstrike subsystem. Legacy Air Raid remains unsuitable and is not re-enabled; the implemented radio/artillery system instead requires real support from an eligible adjacent strategic sector.

## Validation performed

- Consolidation branch is a descendant of the corrected launch branch.
- Parallel Deidranna/radio/tactical-final/militia/legacy-AI work was audited and reconciled content-wise.
- No Git conflict markers remain in the consolidated source changes.
- Structural brace signatures of consolidated files match the launch baseline.
- Aimed grenade UI, CTH, AP-affordability, actual AP deduction and AI planning call paths were traced.
- Bleed-out save layout, full-round countdown and no-return-to-combat stabilization were rechecked.
- Temporary direct-merge PRs were closed after manual reconciliation.

## Final consolidation audit

- Unique content from `ai/human-tactical-final` was checked against this branch at file-content level; all substantive additions are present or superseded by newer compatible logic.
- Unique content from `ai/shared-enemy-militia-brain` is fully represented.
- Unique content from `ai/legacy-core-modernization` is fully represented; apparent Medical.cpp line mismatches were formatting/newer-logic differences, not missing behavior.
- The recoil auto-weapons divisor uses a backward-compatible read of both the legacy Vengeance key and the newer 1.13 key, intentionally superseding the older single-key implementation.
- Temporary integration PRs were closed after reconciliation; use only `install/all-2026-09-12`.

## Tactical AI porting freeze

Future upstream JA2 1.13 AI work is governed by `TacticalAI/AI_PORTING_POLICY.md`.
Correctness, path/AP safety, grenade correctness, crash fixes, performance and
diagnostics may be reviewed for porting. Tactical doctrine/behaviour changes are
frozen unless explicitly designed and audited for this Vengeance architecture.
## Cross-system hardening — 2026-09-14

The integration branch was audited as a whole after the combat, casualty, retreat,
presentation and LOBOT systems were combined. The following interaction faults were
corrected:

- Battle victory can no longer leave a hostile bleed-out casualty alive when the
  prisoner system is disabled. POW-enabled games still stabilize/capture the casualty;
  POW-disabled games release the temporary bleed-out protection and use the legacy
  incapacitated-enemy cleanup. This preserves pursuit-sector retreat locks and sector
  ownership invariants.
- Eligible fatal gunshots now have one visual owner: the VR 30-way fatal dispatcher runs
  before legacy head-explode/flyback special branches. Legacy fall/JFK paths remain only
  as fallbacks for cases the VR dispatcher intentionally excludes, and no longer stack a
  second VR gore package.
- Bleed-out/drag state is explicitly save-versioned as version 152. Versions 147-151
  consume the historical 20 filler bytes without interpreting them, preventing plausible
  legacy filler values from becoming fake casualties or drag links.
- Visible-equipment deployment is pinned to a fixed 1.13 gamedir revision instead of
  moving `master`, while retaining case-resolved asset discovery and the READY marker
  runtime gate.
- NCTH movement range evaluation now searches the same available scope-mode catalogue as
  firing logic rather than judging a position only through `USE_BEST_SCOPE`.
- Allied casualty callouts share a six-second battlefield medic-call cooldown, and agony
  reactions receive short spacing so several simultaneous casualties do not produce
  overlapping voice spam.
- Civilian murder/loyalty handling was re-audited and deliberately left unchanged:
  existing Vengeance logic already attributes player/enemy/rebel/monster responsibility,
  considers witnesses/false blame and scales the loyalty effect accordingly.

A self-hosted Release/Win32 integration-build workflow now syntax-checks deployment
PowerShell and compiles the Vengeance executable on relevant source pushes. It never
launches normal gameplay.

## Build gate

There is no repository CI build configured for this branch. A local Windows Visual Studio **Rebuild Solution** is still required before treating the executable as compiler-verified.
