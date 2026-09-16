# Integration / 2026-09-12 Super Master policy

## Canonical branch

- Technical Git ref: `install/all-2026-09-12`.
- User-facing names `2026 09 12`, `Super Master`, `main branch`, and `canonical integration branch` all mean that same ref.
- A separate branch named `super-master` is not an integration target. Treat any such ref as historical/stale unless this policy is explicitly changed.

## Concurrency rule

Every validation run must begin with `git fetch origin --prune` and pin the exact canonical SHA being tested. If the canonical SHA changes before integration, repeat the gate against the new SHA.

Never integrate from a dirty feature checkout or from `00000` when it contains active stream edits, IDE retargeting, build outputs, or generated files. Use a clean disposable worktree.

## Integration rule

Diverged feature branches are forward-ported onto the current canonical baseline before integration. Do not merge a stale branch head wholesale merely because Git can merge it automatically.

A clean textual merge is necessary but not sufficient. Behavioural overlap in tactical AI, LOS, weapons, physics, rendering, inventory, and strategic code requires subsystem-specific regression checks.
## Required gates

1. Fetch/prune and record the canonical SHA.
2. Confirm candidate ancestry and unique commit count.
3. Dry-run merge in a disposable worktree and enumerate conflicts.
4. Run `git diff --check` and reject merge markers or whitespace errors.
5. Review changed files for cross-stream hot-zone overlap and generated/IDE noise.
6. Run the relevant subsystem audits and static checks.
7. Require a successful compile/build before declaring a code change playtest-ready.
8. Preserve a rollback anchor before broad or high-risk integration.
9. Do not integrate strategic/campaign-layer changes unless they were explicitly requested.
10. Matt performs the final GitHub Desktop fetch/pull and Visual Studio run/playtest unless he explicitly asks otherwise.

## Current interpretation

Engine/rendering, combat, audio, map, AI, UI and item streams stay isolated until they satisfy these gates. Integration/QA owns consolidation discipline, not feature design.
