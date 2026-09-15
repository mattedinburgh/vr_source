# Engine / VHD modernization — 2026 workstream

## Purpose

Modernize the Vengeance engine and VHD rendering stack without destabilizing the current integrated gameplay build.

Canonical base: `install/all-2026-09-12`
Umbrella branch: `engine/vhd-modernization-2026`

## Hard boundaries

- Tactical AI doctrine/behaviour is out of scope.
- Strategic AI is out of scope.
- Preserve save compatibility unless a migration is explicitly designed and tested.
- Preserve map/grid/JSD identity and existing STI fallback semantics.
- No full framebuffer rewrite until the current true-colour path is runtime-stable.
- Every engine change must remain independently revertible and pass compile/runtime smoke gates before integration.

## Existing baseline already available

- B1TC / JPC true-colour asset path with legacy STI fallback.
- Multi-region VOBJECT support and RGB/RGBA source preservation.
- JA2 shade-level lighting, ordered RGB565 dithering, alpha compositing.
- Z test/write, JSD Z-strip support, wall equal-Z semantics and obscured rendering parity.
- True-colour shadow/intensity masks.
- Black Box v5 crash/hang diagnostics and analytics lifecycle hardening.
- Self-hosted Release/Win32 VHD build and startup smoke workflows.
- VHD2 asset-generation and MapEditor cache infrastructure.

## Existing VHD branches to audit before porting

- `feature/vhd-renderer`
- `engine/vhd-render-instrumentation-2026-09-15`
- `engine/vhd-render-diagnostics-2026-09-15`
- `engine/vhd-occlusion-compositor-2026-09-15`
- `engine/vhd-cache-modernization-2026-09-15`
- `engine/vhd-hardening-2026-09-15`

These branches are treated as candidate patch sets, not merge targets. They diverged from the current integrated branch and must be content-audited/cherry-picked or reimplemented against the current base.

## Work packages

### E1 — Baseline and regression gate
- Build current integrated Release/Win32 baseline.
- Run 2x startup smoke.
- Record executable hash and black-box baseline.
- Verify VHD workflow coverage for this branch.

### E2 — Renderer correctness and diagnostics
- Consolidate VHD render instrumentation.
- Keep diagnostics low-overhead and bounded.
- Add counters for true-colour/fallback selection, Z/occlusion paths, cache hits/misses and failed asset contracts.

### E3 — Tile/cache modernization
- Audit Tile Cache lifetime, lookup complexity and invalidation.
- Port only verified cache improvements from the existing cache-modernization branch.
- Guard against stale VOBJECT/JSD references and repeated decode/load work.

### E4 — Occlusion/compositor hardening
- Audit roof/wall/tree obscuration and equal-Z ordering.
- Consolidate the experimental compositor only where it preserves legacy visibility semantics.
- No gameplay visibility/LOS changes in this package.

### E5 — VHD2 asset/runtime contract
- Preserve frame count, geometry, offsets and JSD contracts.
- Make invalid VHD2 assets fail safely to STI rather than corrupting tile identity.
- Keep map files untouched.

### E6 — Platform/toolchain modernization
- Keep VS2013/v120-compatible production build until a newer path is proven.
- Maintain compile-only validation on modern MSVC.
- Investigate a parallel modern build description only after engine behavior is stable.
- Do not combine renderer migration and toolchain migration in one patch.

### E7 — Performance and stability
- Profile render hot paths, cache churn, I/O and frame stalls.
- Prefer bounded allocations and O(1)/amortized lookup improvements.
- Use Black Box instrumentation for hangs/crashes instead of speculative rewrites.

## Integration policy

Each patch must:
1. compile independently;
2. pass runtime startup smoke;
3. preserve legacy STI fallback;
4. avoid AI behavior changes;
5. document any save/map/asset-format implications;
6. be small enough to revert without reverting unrelated engine work.

## Initial priority

1. Establish clean baseline on this branch.
2. Audit the five active VHD substreams against the current integrated base.
3. Port diagnostics/instrumentation first.
4. Port cache improvements second.
5. Port compositor changes only after visual/Z regression checks.
6. Revisit native 32-bit framebuffer only after the true-colour pipeline is proven stable.
