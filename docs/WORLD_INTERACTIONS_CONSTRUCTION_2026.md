# World interactions / construction — 2026 workstream

## Integration rule
- Canonical integration target: `2026 09 12` / Super Master, represented by `install/all-2026-09-12`.
- Development branch: `world/interactions-construction-2026-09-17`.
- Baseline verified/rebased on `origin/install/all-2026-09-12` at `fb2e4cbc` on 2026-09-17.
- Do not modify strategic/campaign AI, force movement, garrisons, patrols, reinforcement logic, or campaign force sizing.
- Explosive damage/breaching balance remains owned by the items/explosives stream; this stream may only harden door/lock interaction boundaries.

## Methodology
The Vengeance construction system already shares much of the mature 1.13 interaction model, so this stream uses research-first, invariant-driven hardening rather than wholesale feature copying.

Priority order:
1. Correct actor and target ownership.
2. Correct material accounting and transactional completion.
3. Valid placement/removal only.
4. Persistence through map-temp changes and sector reload.
5. Immediate movement-cost/render/shadow refresh after world changes.
6. UI cursor, action start, completion validation, and execution must agree.
7. Modern 1.13 tactical-only improvements are adapted only when compatible with Vengeance data/save assumptions.
8. Extend mechanics only after the baseline invariants are stable.

## Baseline already present
- Empty sandbag + shovel can fill sandbags on supported terrain.
- Full sandbag and concertina can be constructed.
- Sandbags are allowed inside rooms in VR.
- Construction/removal is written through map-temp changes.
- City-sector shovel seeding is present in the canonical branch.
- Construction is blocked underground / on roof interface level.
## Verified completed work
- Construction orientation comes from the merc performing the multi-turn action, not `gusSelectedSoldier`.
- Shovel cursor/interaction accepts only actual removable fortifications, not arbitrary `STRUCTURE_GENERIC` objects.
- Multi-turn dismantling revalidates that the target is still removable before completion.
- Removable fortification detection iterates generic structures and recognizes `sandbag.sti` as full sandbag and applicable `spot_1.sti` variants as concertina.
- Concertina can be dismantled through the construction shovel path.
- Dismantling returns the material matching the removed fortification.
- UI target validation, cursor validation, action start, completion validation, and execution share the removable-fortification predicate.
- Fortification actions are rejected on roofs and in underground sectors before work begins.
- Non-PCH helper declarations avoid the legacy `Handle Items.h` include cycle.
- City shovel seeding checks tactical accessibility before deciding the sector already has a usable shovel.
- Door opening noise is emitted from the interacted door grid and current 1.13 unseen/noisy-door animation handling is retained.
- Lockpick/unlock/breach success is separated from opening a keyed door during turn-based combat, matching current 1.13 intent.

## 2026-09-17 implementation
### Fortification completion and recovery
- Final multi-turn completion is transactional: a failed build/remove/fill mutation no longer consumes the final AP/BP slice or reports a successful completion.
- Build material is consumed only after `BuildFortification()` succeeds.
- Dismantling resolves the exact recoverable material before mutating the world. Missing/malformed item data therefore cannot silently destroy a fortification.
- `IsRemovableFortificationAtGridNo()` now agrees with execution: dismantling is advertised only if the corresponding material item exists.
- `RemoveFortification()` returns the exact recovered item ID rather than a generic material flag.
- Successful dismantling spawns that prevalidated material and only then completes the multi-turn action.

### Locks / keys
- Vengeance currently has `NUM_LOCKS == 64`, while modern 1.13 has a 255-slot runtime table with a 64-entry legacy `Locks.bin` payload.
- Unsupported/sentinel lock IDs are rejected before `LockTable` access in lockpicking, crowbar, smash, and breach paths.
- Key lock/unlock paths reject unsupported Vengeance lock IDs rather than treating an undefined byte value as a usable key.
- Door breaching charges are not consumed when the lock ID is invalid/unsupported.
- The existing 64-lock data/save model is intentionally retained for now; expanding it is a separate compatibility task, not a blind modern-1.13 port.

### QA / CI
- World-interactions source-invariant QA covers door-noise behavior, combat lock/open separation, non-PCH declarations, fortification orientation/removal validity, transactional completion, recovery preflight, and lock-ID bounds.
- Hosted compile workflow trigger targets the actual `world/interactions-construction-2026-09-17` branch.

## Validation completed
- Rebased workstream on the current canonical remote baseline before implementation.
- Compared relevant door/lock behavior against current `1dot13/source` and retained the existing methodology.
- `Tools/QA/TEST_WORLD_INTERACTIONS.ps1`: PASS after implementation.
- `git diff --check` with `core.whitespace=cr-at-eol`: PASS.
- Local Visual Studio 2013 / v120 `ClCompile`: PASS after implementation; only pre-existing warnings observed.
- Movement-cost code path checked: build and removal both call `RecompileLocalMovementCosts`, whose implementation clears/recompiles a local radius plus spillover, so no missing one-tile refresh was found statically.
- No in-game runtime test was performed in this implementation turn; runtime persistence/orientation/pathfinding checks remain open.

## Remaining work
1. Runtime tactical matrix: sandbag/concertina build-remove-rebuild; indoor/outdoor; hostile/non-hostile; turn-based/realtime.
2. Confirm every concertina orientation and tileset fallback removes the intended structure and returns concertina.
3. Verify runtime pathfinding/AI reacts immediately to build/remove despite the statically correct movement-cost refresh.
4. Verify map-temp persistence after sector exit/re-entry and save/reload for both sandbags and concertina.
5. Audit door trap/key edge cases and tactical-only interaction behavior for compatibility with Vengeance's 64-lock model.
6. Audit destructible tactical environment interactions that belong here (doors, windows, fences/objects), while leaving explosive damage/balance in the items/explosives stream.
7. Decide separately whether a compatibility-safe `NUM_LOCKS/NUM_KEYS` expansion is worthwhile; it requires map/save/keyring migration analysis and is not assumed safe.
8. Add targeted runtime diagnostics only if playtesting exposes persistence or structure-database edge cases.

## Precise next implementation action
Audit the tactical door/key/trap interaction paths against current 1.13 for any additional **tactical-only** fixes that do not require changing Vengeance's save/data capacities, add deterministic regression invariants for any adopted fixes, then move directly into the fence/window/destructible-object interaction audit.
