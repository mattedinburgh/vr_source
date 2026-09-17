# Integration / 2026-09-12 Super Master policy

## Canonical branch

- Technical Git ref: `install/all-2026-09-12`.
- User-facing names `2026 09 12`, `Super Master`, `main branch`, and `canonical integration branch` all mean that same ref.
- A separate branch named `super-master` is not an integration target. Treat any such ref as historical/stale unless this policy is explicitly changed.

## Freshness and pinning

Local QA is non-destructive by default: it must not silently fetch, pull, reset, checkout, build, or push.
Matt performs the final GitHub Desktop fetch/pull and Visual Studio run/playtest unless he explicitly asks otherwise.

After the final user-controlled fetch, record the exact canonical SHA and pass it to the gate with
`-ExpectedCanonicalSha`. If the SHA differs, the gate stops. The optional `-Fetch` switch is only for an
explicitly requested automated refresh.

Never integrate from a dirty feature checkout or from `00000` when it contains active stream edits,
IDE retargeting, build outputs, or generated files. Use clean stream worktrees and disposable QA worktrees.
## Integration method

1. Run `AUDIT_INTEGRATION_BRANCHES.ps1` to classify streams as AHEAD, DIVERGED, ALIGNED, or CONTAINED.
2. DIVERGED streams must be forward-ported onto the pinned canonical baseline before integration.
3. Run `TEST_INTEGRATION_CANDIDATE.ps1` on every AHEAD stream individually.
4. Run `CHECK_INTEGRATION_CONFLICTS.ps1` (also invoked automatically by the batch gate) to detect exact shared-file ownership and shared subsystem areas across the intended merge set. Exact shared-file overlap blocks by default even when Git can merge it textually; override only after explicit compatibility review.
5. Run `TEST_INTEGRATION_BATCH.ps1` on the exact intended merge set and merge order. The gate resolves every candidate ref to an immutable SHA before testing, removes any candidate that is wholly contained by another supplied candidate (while reporting it as redundant), runs the cross-stream conflict scan on the independent tips, and merges those SHAs, so parallel workstreams cannot move underneath the batch.
6. Only after the static gates pass may the set be considered merge-eligible.
7. A successful compile/build is still required before anything is called playtest-ready.
8. Preserve a rollback anchor before broad or high-risk integration.

## Project-wide release inventory

Before any project-wide `release`, run `BUILD_RELEASE_INVENTORY.ps1` against the pinned canonical SHA. `RELEASE_STREAM_REGISTRY.json` is the explicit project-wide classification of current workstream tips as `ready`, `active`, `blocked`, `parked`, or `superseded`. A release gate must include every registry `ready` branch that is AHEAD of canonical. A `ready` branch that is DIVERGED must be forward-ported first; it cannot be silently skipped. Any unregistered AHEAD branch blocks release until it is classified, preventing newly completed streams from disappearing from the release set.

Owning streams must update their registry entry when their checkpoint changes release state. Historical DIVERGED refs may remain visible in the inventory. Unregistered AHEAD refs that are strict ancestors of another AHEAD tip are reported as subsumed rather than treated as independent release blockers; if an ancestor is independently release-ready while its descendant is not, register that ancestor explicitly as `ready`. Only registered current stream tips plus independent unregistered AHEAD refs participate in the hard release gate. This avoids both partial releases and false blockers from nested/superseded branch history.

A clean textual merge is necessary but not sufficient. Behavioural overlap in tactical AI, LOS, weapons,
physics, rendering, inventory, audio, progression, and UI requires subsystem-specific regression checks.

## Performance regression gate

`COMPARE_AI_PERFORMANCE.ps1` consumes the AI stream's existing `[AI-PERF]` records and compares baseline vs candidate decision-time, pathfinding, search-count and cache metrics. Integration/QA owns the regression comparison; AI owns the instrumentation and tactical implementation. Use `-FailOnRegression` for release gating.

## Donor-map integration gate

Stream 4 owns sector dependency discovery and manifest generation. Integration/QA owns `VALIDATE_DONOR_SECTOR_MANIFEST.ps1`, which refuses to treat a donor sector as integration-ready unless required map/RPG/script/item/asset/provenance checks are explicitly evidenced and quest/NPC, item, entry/exit and save/load regressions pass. High/Protected sectors additionally require explicit manual approval.

## Hard blockers

- Generated/build/IDE noise in a candidate delta.
- Exact cross-stream shared-file overlap unless explicitly reviewed and allowed. Prefer `-AllowedCrossStreamPaths` with exact reviewed paths; use the broad `-AllowCrossStreamFileOverlap` override only when every overlap in the batch has been deliberately reviewed.
- Unresolved or committed merge markers.
- PowerShell syntax errors in changed QA/deployment scripts.
- Tactical-AI changes that fail the unified AI integrity audit.
- Strategic/campaign-layer changes unless Matt explicitly requested them. Files physically under `Strategic/` that implement a reviewed non-campaign subsystem may use `-AllowedStrategicPaths` for exact paths only; this is not permission to alter campaign AI, force movement, reinforcements, garrisons, patrols, logistics, or force sizing. Prefer the exact-path allowlist over the broad `-AllowStrategicChanges` switch whenever the exception is file-scoped.
- Candidate history that is not based on the pinned canonical tip.

## Ownership boundary

Engine/rendering, combat, audio, map, AI, UI, item, progression and world-interaction streams stay isolated
until they satisfy these gates. Integration/QA owns consolidation discipline, regression gating and branch
hygiene; it does not redesign features inside those streams.
