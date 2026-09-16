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
4. Run `TEST_INTEGRATION_BATCH.ps1` on the exact intended merge set and merge order. The gate resolves every candidate ref to an immutable SHA before testing and merges those SHAs, so parallel workstreams cannot move underneath the batch.
5. Only after both static gates pass may the set be considered merge-eligible.
6. A successful compile/build is still required before anything is called playtest-ready.
7. Preserve a rollback anchor before broad or high-risk integration.

A clean textual merge is necessary but not sufficient. Behavioural overlap in tactical AI, LOS, weapons,
physics, rendering, inventory, audio, progression, and UI requires subsystem-specific regression checks.

## Hard blockers

- Generated/build/IDE noise in a candidate delta.
- Unresolved or committed merge markers.
- PowerShell syntax errors in changed QA/deployment scripts.
- Tactical-AI changes that fail the unified AI integrity audit.
- Strategic/campaign-layer changes unless Matt explicitly requested them.
- Candidate history that is not based on the pinned canonical tip.

## Ownership boundary

Engine/rendering, combat, audio, map, AI, UI, item, progression and world-interaction streams stay isolated
until they satisfy these gates. Integration/QA owns consolidation discipline, regression gating and branch
hygiene; it does not redesign features inside those streams.
