# Vengeance Reloaded — Project Control

Updated: 2026-09-16

## Canonical-version rule

Vengeance is a **single playable product**. There is no supported alternative gameplay branch.

Current canonical rule:
- `install/all-2026-09-12` is the one canonical integration/playtest branch. In project shorthand, **`2026 09 12` = main = Super Master = normal integration target**.
- Do not create or revive a separate branch named "Super Master", "main", "final", or "integration" as an alternative integration spine.
- Workstream and experimental branches remain isolated only while unfinished; selected changes are reconciled back into `install/all-2026-09-12`.
- `master` is a repository/default-history branch, not the normal integration target for current project work.
- The pre-reconciliation state remains preserved at `archive/install-all-pre-reconcile-2026-09-15`.

## Branch policy

1. New normal work starts from the current `install/all-2026-09-12` head.
2. Each active subsystem uses at most one current workstream branch when isolation is useful.
3. Reconcile selected, validated changes back into `install/all-2026-09-12`; then retire the temporary workstream branch when appropriate.
4. Never create parallel `final`, `integration`, `launch`, `main`, `Super Master`, or duplicate subsystem branches as competing integration spines.
5. Before creating a branch, check whether an active branch already owns that subsystem.
6. "Branch merged" does **not** mean "feature finished." Broken/incomplete behavior stays OPEN below until verified in-game.
7. Do not delete any branch containing unique content until that content is either ported, explicitly rejected, or recorded with a recovery reference.
8. Parallel development is encouraged across independent workstreams. One active workstream owns each subsystem/problem.
9. Cross-cutting changes that touch another workstream's core files must be reconciled against the current canonical base before integration.

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
| Modern 1.13 ammo types / target damage modifiers | RECONCILED, NEEDS BUILD+PLAYTEST | Source fields/parsing plus required XML/magazine/item data preserved. The gamedir integration XML gate is green on the reconciled critical files and required ammo item definitions. |
| Black Box | RECONCILED, NEEDS PLAY DATA | Telemetry-link fix, compact hang dumps, correct heartbeat thread, and item-description breadcrumbs preserved. |
| Campaign Companion | PARTLY CANONICAL / STRATEGIC EXPERIMENT NOT MERGED | Do not merge old strategic-modernization branches wholesale. Re-evaluate only specific missing telemetry after current AI sessions. |
| Strategic / Campaign modernization | ACTIVE CURRENT-BASE WORKSTREAM | `strategic/campaign-modernization-2026` preserves the unique standalone strategic modernization, operational-AI, transport, ASD and campaign-telemetry modules on the reconciled base. Old modifications to shared Strategic/Tactical files are not merged wholesale; adapt them selectively against current code. |
| CQB building doctrine | ACTIVE CURRENT-BASE WORKSTREAM | `ai/cqb-doctrine-2026` preserves the standalone CQB doctrine implementation and design documents on the reconciled base. It is not yet wired into the current project/build; integration must be adapted and validated. |
| Weather modernization | ACTIVE DESIGN WORKSTREAM | `world/weather-modernization-2026` preserves the weather modernization design on the reconciled base. Old shared-file implementation patches remain quarantined until selectively reimplemented against current source. |
| Visible equipment / armour | CODE PRESENT, DEFECT OPEN | Old LOBOT/visible-equipment branches are contained by canonical history, but armour is still not visible in-game. Fix on canonical code; do not revive old branches wholesale. |
| VHD / HD renderer | ACTIVE EXPERIMENT + CI | Single temporary line: `exp/vhd`. Black Box renderer instrumentation, cache/memory modernization and one-pass indexed occlusion are consolidated there. VHD build/smoke workflows now run directly on `exp/vhd`. |
| VHD occlusion compositor | CONSOLIDATED ON EXPERIMENT | One-pass indexed compositor has been ported to `exp/vhd` and adapted to current Black Box frame telemetry (`onepass`, masked-pixel counters). Old CSV-diagnostics implementation is now historical. |
| Maps / Latin visual overhaul | ACTIVE CURRENT-BASE WORKSTREAM | `maps/visual-overhaul-2026` is the current-base map lane. Map Factory docs/art direction/authoring tools are preserved there. Audit of the old `worlddef`/`MapUtility` implementation found automatic sector dressing/baking that conflicts with the graphics-only rule, so that methodology is rejected rather than merged. The safe forced-shade-cache guard was ported to the current map lane. Preserve map geometry/gameplay properties unless deliberately approved. |
| Item icons | REJECTED PILOT / REDESIGN OPEN | Earlier 20-icon pilot is superseded by `save23`; bulk `art/all-inventory-icons-ai` is separate. None is accepted canonical art. Keep source material until redesign is complete. |
| Cold UI | DORMANT / REDESIGN OPEN | Source-side harmonisation exists; pilot art remains non-final. Do not force old pilot visuals into canonical build. |
| Tactical UI / information | ACTIVE CURRENT-BASE WORKSTREAM | `ui/tactical-information-2026-09-16` is based on the current `2026 09 12` head. Hover labels and battle-log/shot-inspector entries now share one tactical soldier identity path; authored profile names are preserved, while regular enemies receive deterministic per-soldier identities instead of the generic sector labels from `EnemyNames.xml`. Unseen-fire V2 uses a dedicated four-slot coarse-bearing pool with same-direction reinforcement, no true-source grid, and no camera-slide path. Purpose-built cue art, confidence/level semantics and in-game validation remain open. See `docs/UNSEEN_FIRE_BEARING.md`. |
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

## Parallel workstream map

Independent categories may progress simultaneously, with one owning branch per subsystem:

- AI / tactical behaviour
- combat / NCTH
- items / inventory
- UI / UX
- graphics / visible equipment / icons
- maps / world art
- audio / ambience
- engine / VHD
- strategic / campaign
- diagnostics / testing
- world systems such as weather
- isolated bug fixes

The limiting factor is shared-file collision and validation, not an arbitrary branch count. Workstreams converge frequently into the integration/canonical line rather than becoming alternative game versions.

## Current playtest rule

While repository cleanup is happening, the local game installation does not need to change. Play normally and generate Black Box/AI data. Repository changes only affect the local game when explicitly pulled/built/deployed.

## Definition of done for cleanup

Cleanup is complete when:
1. required unique source/game-data work is present on the integration line;
2. unfinished work is explicitly listed here;
3. experimental VHD work exists on only `exp/vhd`;
4. map/icon work has a single surviving source-of-truth workstream or an explicit preservation task;
5. both repositories validate;
6. selected workstream changes are reconciled into `install/all-2026-09-12` without creating a competing integration spine;
7. old branches are retired/archived and no future work is committed to them.
