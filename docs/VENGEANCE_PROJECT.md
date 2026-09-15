# Vengeance Reloaded — Project Control

Updated: 2026-09-15

## Canonical-version rule

Vengeance is a **single playable product**. There is no supported alternative gameplay branch.

Current cleanup state:
- `install/all-2026-09-12` was the de-facto integration branch.
- `reconcile/streamline-2026-09-15` is the temporary cleanup branch used to preserve unique work and remove historical divergence.
- **Target after validation: `master` becomes the only canonical playable branch** in both `vr_source` and `vr_gamedir`.
- Temporary experimental branches are allowed only when unfinished work is unsafe to put in the playable build. They must have a named port/exit task and must not become alternative game versions.

## Branch policy

1. New normal work starts from `master`.
2. Small features/fixes use a short-lived `feat/*` or `fix/*` branch only when necessary.
3. Merge/port into `master` promptly; then retire the temporary branch.
4. Never create parallel `final`, `integration`, `launch`, or second AI branches for the same subsystem.
5. Before creating a branch, check whether an active branch already owns that subsystem.
6. "Branch merged" does **not** mean "feature finished." Broken/incomplete behavior stays OPEN below until verified in-game.
7. Do not delete any branch containing unique content until that content is either ported, explicitly rejected, or recorded with a recovery reference.

## Safety references

- Source safety snapshot: `archive/pre-streamline-2026-09-15`
- Game-data safety snapshot: `archive/pre-streamline-2026-09-15`

These snapshots preserve the pre-cleanup all-in-one baseline.

## Workstream status

| Workstream | State | Canonical decision / next action |
|---|---|---|
| Tactical AI / morale / retreat / fireteams | IN CANONICAL, NEEDS PLAYTEST | Old parallel AI branches are historical/superseded. Use current Black Box play data to tune the one implementation. |
| NCTH / recoil / optics / bursts / grenade aiming | IN CANONICAL, NEEDS PLAYTEST | Keep one current implementation; no parallel shooting branch. |
| Enemy loadouts / progression | IN CANONICAL | Newer canonical logistics implementation supersedes old `final/all-work` regional-supply commits. |
| Ammo pooling / 3 mags / grenade distribution | RECONCILED, NEEDS BUILD+PLAYTEST | Current master-only logic was selectively ported into cleanup branch. Weapons load first; penetration priority is XML/data-driven; grenade button preserved. |
| Modern 1.13 ammo types / target damage modifiers | RECONCILED, NEEDS BUILD+PLAYTEST | Source fields/parsing plus required XML/magazine/item data preserved. |
| Black Box | RECONCILED, NEEDS PLAY DATA | Telemetry-link fix, compact hang dumps, correct heartbeat thread, and item-description breadcrumbs preserved. |
| Campaign Companion | PARTLY CANONICAL / STRATEGIC EXPERIMENT NOT MERGED | Do not merge old strategic-modernization branches wholesale. Re-evaluate only specific missing telemetry after current AI sessions. |
| Visible equipment / armour | CODE PRESENT, DEFECT OPEN | Old LOBOT/visible-equipment branches are contained by canonical history, but armour is still not visible in-game. Fix on canonical code; do not revive old branches wholesale. |
| VHD / HD renderer | ACTIVE EXPERIMENT | Single temporary line: `exp/vhd`. Newer Black Box renderer instrumentation + cache/memory modernization are consolidated there. |
| VHD occlusion compositor | PORT REQUIRED | One-pass indexed compositor is useful but lives on stale CSV-diagnostics code. Port algorithm to `exp/vhd` using current Black Box VHD counters; then retire old compositor branch. |
| Maps / Latin visual overhaul | ACTIVE, NOT YET RECONCILED | `map-factory-v1` and `a3/hand-authored-v2` each contain unique work. DO NOT DELETE until selectively reconciled into one map workstream. Graphics only: preserve map geometry/gameplay properties unless deliberately approved. |
| Item icons | REJECTED PILOT / REDESIGN OPEN | Earlier 20-icon pilot is superseded by `save23`; bulk `art/all-inventory-icons-ai` is separate. None is accepted canonical art. Keep source material until redesign is complete. |
| Cold UI | DORMANT / REDESIGN OPEN | Source-side harmonisation exists; pilot art remains non-final. Do not force old pilot visuals into canonical build. |
| Spanish battle popups + screams | OPEN | Popups should be Spanish and allowed concurrently with screams. Verify implementation in live battle. |
| Radio operator | VERIFY/FINISH | Confirm full trait/equipment/Bobby Ray's availability path in canonical game data and source. |
| Rag bandages | OPEN DEFECT | Must actually work when used from hand/on body; acts as a much less efficient first-aid treatment that secures bleeding/yellow damage. |
| Enemy loot minimum | OPEN/VERIFY | Ensure a defeated enemy leaves at least one eligible item while respecting intended droppable rules. |
| Ground item pickup | OPEN DEFECT | Reproduce and fix current pickup problem on canonical build. |
| Shotgun range | REVIEW/VERIFY | Cross-check normal shotgun ranges with current 1.13 intent and correct outliers only. |
| Window breaking AP | OPEN | Apply reasonable AP cost/behavior; verify against 1.13-style interaction. |
| Isometric wall occlusion | IN PROGRESS | Fallout-style semi-transparent/cutaway wall behavior; keep doors/windows readable. VHD optimization handled separately. |
| Vision-cone colours | OPEN/VERIFY | Keep red but make all vision colours extremely transparent/gentle. |
| Stealing AP scaling | OPEN/VERIFY | Each stolen item must consume appropriate AP; align with 1.13 mechanics. |
| Sounds / map ambience | ACTIVE/UNFINISHED | Preserve ambience work; continue sector-appropriate city/farm/jingle/etc. treatment. |
| Gore / hit reactions | IN CANONICAL, VERIFY | Preserve newer canonical gore/diagnostic code when resolving old branches. |

## Current playtest rule

While repository cleanup is happening, the local game installation does not need to change. Play normally and generate Black Box/AI data. Repository changes only affect the local game when explicitly pulled/built/deployed.

## Definition of done for cleanup

Cleanup is complete when:
1. required unique source/game-data work is present on the reconciliation line;
2. unfinished work is explicitly listed here;
3. experimental VHD work exists on only `exp/vhd`;
4. map/icon work has a single surviving source-of-truth workstream or an explicit preservation task;
5. both repositories validate;
6. `master` is moved to the validated reconciliation state;
7. old branches are retired/archived and no future work is committed to them.
