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

### Downed casualties
- Symmetric player/enemy/militia downed state for eligible human combatants.
- 2–5 full tactical rounds to rescue depending on wound severity.
- Massive overkill remains immediately lethal.
- First aid stabilizes the casualty.
- Stabilized casualties remain incapacitated for the rest of the fight and can recover normally outside combat.
- Battle-end cleanup no longer automatically executes these casualties.
- Save-structure size preserved by consuming existing filler bytes rather than expanding SOLDIERTYPE.

### UI / presentation / fixes
- Softer visibility overlay colours.
- Rain-preserving UI background behavior.
- Scope-mode icon cleanup after EDB close.
- Melee aiming-circle preservation under NCTH.
- Flare throw flash behavior.
- Interrupted throw item return and self-target attack deadlock fixes.

## Deliberately not claimed as implemented

These were discussed but are not in this install candidate because they require separate safe implementation:
- Carry/drag a downed soldier to safety.
- New separate CAS/airstrike subsystem. Legacy Air Raid remains unsuitable and is not re-enabled.
- The unverified equipment-upgrade shortcut/button.

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

## Build gate

There is no repository CI build configured for this branch. A local Windows Visual Studio **Rebuild Solution** is still required before treating the executable as compiler-verified.
