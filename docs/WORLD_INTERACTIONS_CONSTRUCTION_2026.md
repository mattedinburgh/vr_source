# World interactions / construction — 2026 workstream

## Integration rule
- Canonical integration target: `2026 09 12` / Super Master (currently represented by `install/all-2026-09-12` in vr_source).
- Development branch: `world/interactions-construction-2026`.
- Do not modify strategic/campaign AI, force movement, garrisons, patrols, reinforcement logic, or campaign force sizing.

## Methodology
The construction system already tracks the modern 1.13 fortification implementation closely, so this stream uses invariant-driven hardening instead of wholesale porting.

Priority order:
1. Correct actor and target ownership.
2. Correct material accounting (build consumes the matching item; dismantle returns the matching item).
3. Valid placement/removal only.
4. Persistence through map temp changes and sector reload.
5. Movement-cost/render/shadow refresh after world changes.
6. UI cursor and execution logic must use the same validity predicate.
7. Extend mechanics only after the baseline invariants are stable.

## Baseline already present
- Empty sandbag + shovel can fill sandbags on supported terrain.
- Full sandbag and concertina can be constructed.
- Sandbags are allowed inside rooms in VR.
- Construction/removal uses map-temp persistence.
- City-sector shovel seeding is already present in the canonical branch (commit 39c6b698).
- Construction is blocked underground / on roof interface level.

## Hardening pass implemented
- Construction orientation now comes from the merc performing the multi-turn action, not `gusSelectedSoldier`.
- Shovel cursor/interaction accepts only actual removable fortifications, not arbitrary `STRUCTURE_GENERIC` objects.
- Multi-turn dismantling revalidates that the target is still a removable fortification before completion.
- Removable fortification detection iterates generic structures on the tile and recognizes:
  - `sandbag.sti` => full sandbag
  - `spot_1.sti` concertina variants => concertina
- Concertina can now be dismantled with the construction shovel path.
- Dismantling returns the correct material item type instead of always spawning a full sandbag.
- New helper declarations are explicitly included for non-PCH compilation paths.
- UI target validation, cursor validation, item handling, action start, and multi-turn completion now share the same removable-fortification predicate.
- Fortification actions are rejected early on roofs and in underground sectors instead of beginning an action that the completion routine cannot finish.
- Build/remove action start now validates the target before starting the multi-turn action.

## Validation completed (static)
- Checked current VR construction implementation against available `upstream/master`; core fortification logic is materially the same.
- Verified only one runtime call site for `BuildFortification` and `RemoveFortification`.
- Verified no stale two-argument `BuildFortification` or one-argument runtime `RemoveFortification` call remains.
- `git diff --check` passes when CRLF is treated correctly with `core.whitespace=cr-at-eol`.
- No build/run performed in this stream pass.

## Remaining work
1. Tactical test matrix: sandbag/concertina build-remove-rebuild; indoor/outdoor; hostile/non-hostile; turn-based/realtime; save/reload.
2. Confirm structure removal on every concertina orientation and tileset fallback.
3. Verify movement-cost updates immediately after build/remove for pathfinding and AI.
4. Verify map-temp persistence after sector exit/re-entry for both sandbags and concertina.
5. Audit failure handling when a build becomes invalid on the completion tick; avoid material/AP inconsistencies.
6. Audit destructible-world interactions that belong here (doors, windows, terrain/objects) and keep explosive damage logic in the grenade/explosives stream.
7. Add targeted regression diagnostics if playtesting reveals persistence or structure-database edge cases.
