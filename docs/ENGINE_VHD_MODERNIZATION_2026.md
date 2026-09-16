# Engine / VHD modernization — 2026 workstream

## Purpose

Modernize the Vengeance engine and VHD rendering stack without destabilizing the current integrated gameplay build.

Canonical integration line: `2026 09 12` / `install/all-2026-09-12`
Current validation branch: `engine/vhd-validation-iter2-2026-09-16`

## Hard boundaries

- Tactical AI doctrine/behaviour is out of scope.
- Strategic/campaign AI is out of scope.
- Preserve save compatibility unless a migration is explicitly designed and tested.
- Preserve map/grid/JSD identity and existing STI fallback semantics.
- No gameplay LOS/visibility changes in the engine stream.
- No full framebuffer rewrite until the current true-colour path is runtime-stable.
- Keep VS2013/v120 as the production build path until a newer toolchain is proven separately.
- Every engine change must remain independently revertible and pass compile/runtime smoke gates before integration.

## Current integrated state

The previous plan is now partly historical. The following work is already in the canonical 2026-09-12 lineage through Engine VHD Iteration 1:

- VHD renderer diagnostics and bounded frame sampling.
- High-resolution render timing telemetry.
- HD/occlusion workload counters.
- Byte-budgeted resident tile cache with LRU eviction.
- Tile-cache resident-memory accounting including Z-strip memory.
- Configurable Win32-aware cache budget: 128 MB default, clamped to 16-512 MB.
- Protection against evicting actively referenced tile imagery.
- Deterministic tile-cache residency self-test.
- Tile-cache shutdown ownership ordering: `DeleteTileCache()` occurs before `FreeAllStructureFiles()`.
- Native asset-contract startup self-test.
- Occlusion-mask parity startup self-test.
- Parity-proven one-pass VHD occlusion compositor.
- Self-hosted VS2013/v120 Release Win32 build + isolated 2x runtime smoke.
- Hosted modern-MSVC Release Win32 compile coverage.

Important historical commits include:

- `19b8a2a0` — parity-proven one-pass occlusion compositor integrated into the VHD modernization line.
- `caf52b40` — Engine playtest Iteration 1: core VHD modernization.
- `8cf216e6` — Iteration 1 promoted into the integration lineage.

Old VHD branches are therefore reference/candidate patch sets only. They must not be merged wholesale.

## Methodology correction after Iteration 1

At this point the stream is validation-heavy, not feature-heavy.

Do **not** add another compositor rewrite, framebuffer migration, material system, or toolchain migration until the existing Iteration 1 renderer is objectively validated.

The next phase is intentionally split:

### Iteration 2 — validation only

Branch: `engine/vhd-validation-iter2-2026-09-16`

Iteration 2 is not allowed to change runtime C/C++ behavior. Its purpose is to prove that the current engine state is internally coherent and buildable.

Required gates:

1. Candidate must descend from the current canonical `2026 09 12` integration branch.
2. Validation branch may change only:
   - `.github/workflows/vhd-iter2-validation.yml`
   - `Tools/QA/VERIFY_ENGINE_VHD_ITER2.ps1`
   - this workstream document.
3. `git diff --check` must pass.
4. Static invariants must confirm:
   - tile-cache startup self-test is wired;
   - occlusion parity self-test is wired;
   - 128 MB default / 16 MB min / 512 MB max cache budget remains present;
   - resident-byte accounting and LRU eviction remain present;
   - renderer diagnostics remain present;
   - runtime marker checks remain present;
   - `DeleteTileCache()` remains before `FreeAllStructureFiles()`.
5. Full hosted Release/Win32 compilation must pass.
6. Before promotion, run the production VS2013/v120 build and isolated runtime smoke.
7. Before any new rendering semantics are added, run visual regression on the C5/VHD path and compare against the Iteration 1 baseline.

### Iteration 3 — only after Iteration 2 is green

Candidate work, in this order:

1. Visual-regression baselines for wall/roof/tree/equal-Z cases.
2. Performance profiling using the existing VHD render/cache telemetry.
3. Only then evaluate `engine/vhd-material-profiles-2026` as a selective source of ideas.
4. Keep any material-profile work opt-in and data-driven first; do not combine it with framebuffer/toolchain work.
5. Revisit native 32-bit framebuffer work only after the current path is stable in real play.

## Existing experimental branches

The following branches are **not merge targets**:

- `feature/vhd-renderer`
- `engine/vhd-render-instrumentation-2026-09-15`
- `engine/vhd-render-diagnostics-2026-09-15`
- `engine/vhd-occlusion-compositor-2026-09-15`
- `engine/vhd-cache-modernization-2026-09-15`
- `engine/vhd-hardening-2026-09-15`
- `engine/vhd-material-profiles-2026`
- `exp/vhd`

Use them only for content audits or selective reimplementation against the current canonical base.

## Integration policy

Every future engine patch must:

1. start from the current `2026 09 12` canonical integration line;
2. compile independently;
3. preserve legacy STI fallback;
4. preserve map/grid/JSD identity;
5. avoid tactical-AI and strategic/campaign changes;
6. avoid save-format changes unless explicitly designed;
7. pass hosted compile before local production testing;
8. pass VS2013/v120 build and isolated runtime smoke before promotion;
9. pass visual/Z regression when renderer semantics change;
10. stay small enough to revert without reverting unrelated engine work.

## Current decision

The correct next move is **validation, not more rendering code**.

Iteration 2 freezes engine semantics, formalizes invariants, and gives the next feature iteration a trustworthy baseline.
