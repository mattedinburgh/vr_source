# Map Visual QA Doctrine — 2026

## Problem

The A3 remaster produced visually plausible but false freestanding structural
objects in open ground. A generic visual review initially judged the render
positively and missed the defect.

The failure proved that binary/geometry compatibility is necessary but not
sufficient. A replacement can preserve dimensions, alpha, offsets and JSD
contracts while still assigning the wrong visual meaning to a frame.

## Root cause established on A3

- A3.dat was not the source of the false objects.
- BUILD_39, BUILD_31, BUILD_40 and BUILD_35 each contain 65 image frames and
  59 stored JSD structures.
- None of those four JSDs contains STRUCTURE_DOOR, STRUCTURE_SLIDINGDOOR,
  STRUCTURE_DDOOR_LEFT or STRUCTURE_DDOOR_RIGHT.
- Frames 29..34 in all four families have no stored JSD structure record.
- The old generator treated those unknown frames as generic auxiliary/shadow
  graphics and invented new RGB content from their alpha silhouette.
- That preserved the technical sprite contract while corrupting visual meaning.

Correct rule:

> Unknown structural semantics are not permission to invent artwork.

Unknown JSD-backed frames must remain canonical RGBA until their role is
explicitly understood.

## Truth hierarchy

### Tier 0 — immutable scope

For a graphics-only remaster:

- map DAT geometry/placement is read-only;
- strategic/campaign data is out of scope;
- object additions/removals/relocations are forbidden unless separately
  approved as map-design work;
- a missing source contract never licenses substitution.

### Tier 1 — deterministic structural truth

Hard-fail production when any of these are violated:

1. Canonical tactical source contract is missing or ambiguous.
2. Frame count changes.
3. Frame dimensions change.
4. Sprite offsets change.
5. Alpha footprint changes.
6. JSD frame references are out of range.
7. Unsupported door semantics are encountered.
8. Unknown JSD-backed frame RGB differs from canonical RGBA.
9. B1TC serialization/read-back changes the contract.
10. Generator and runtime alias tables drift.
11. STI-to-B1TC sibling loader behavior is no longer present.
12. An explicit original-art fallback has a stale override file.

A fallback must be named explicitly. Silent fallback and silent substitution
are both forbidden.

### Tier 2 — independent verifier

The generator does not grade its own work.

Tools/A3/audit_a3_structural_output.py independently parses STI, JSD and B1TC
output and re-checks the production invariants.

Current A3 proof:

- BUILD_39: PASS, unknown passthrough frames 29..34
- BUILD_31: PASS, unknown passthrough frames 29..34
- BUILD_40: PASS, unknown passthrough frames 29..34
- BUILD_35: PASS, unknown passthrough frames 29..34
- WELFLOR3: PASS
- P_FLOOR3: PASS
- WELFLOR1: PASS
- WELFLOR2: PASS
- SLANT_11: PASS
- SLANT_13: PASS
- W_ROOF1: explicit ORIGINAL-FALLBACK because no canonical tactical source
  contract is available in the installed tilesets.

### Tier 3 — paired engine-render QA

Use fixed-camera pristine/remastered engine renders of the same geometry.

Global colour statistics remain useful for:
- excessive darkness;
- excessive saturation;
- blanket green casts;
- large dark-area growth.

They are not sufficient for local semantic defects.

The local artifact detector therefore scans overlapping 64-pixel windows at
32-pixel stride. A hard review hotspot requires all of:

- the pristine patch was visually quiet;
- dark-area share rises materially;
- structural edge energy rises materially;
- local luminance collapses materially.

This is intentionally aimed at false structural masses appearing in previously
quiet ground, where global averages can hide them.

## Regression benchmark

Tools/Test-MapFactoryVisualQARegression.ps1 creates six views for five
controlled cases and runs the real QA script.

| Case | Expected | Measured |
| --- | --- | --- |
| Palette-only warm shift | PASS | PASS |
| Mild ground texture enrichment | PASS | PASS |
| Aggressive recolour of an existing building | PASS | PASS |
| New freestanding structural object | FAIL | FAIL |
| Smaller freestanding structural object | FAIL | FAIL |

The previous non-overlapping/high-threshold detector failed both inserted-object
tests. The overlapping quiet-region detector passes this regression suite.

## Vision / recognition systems

Vision models are critics and proposal generators, not authorities.

### Open-vocabulary object localisation

Grounding DINO or YOLO-World can propose boxes for prompts such as:
door, wall panel, window, roof fragment, fence, post, duplicated prop.

Use them to find candidate anomalies. Do not let detector confidence decide
map correctness.

### Segmentation

SAM 2 can refine candidate boxes into masks so a suspected object can be
compared with engine/map provenance and neighbouring geometry.

### Spatial semantic critic

Qwen3-VL is suitable for a defect-first prompt because it supports visual
reasoning and spatial grounding. It should be asked to exhaust structural and
physical inconsistencies before commenting on aesthetics.

Never ask only: "Does this map look good?"

Required critic prompt pattern:

1. Locate every physically/architecturally implausible object.
2. Give coordinates/boxes.
3. Check wall-door-window-roof adjacency.
4. Check object scale and orientation.
5. Check repeated/stamped assets.
6. Check seams and lighting/material inconsistency.
7. Only then discuss overall art direction.

### Dense feature outliers

DINOv3 is best used for patch-level feature similarity/outlier analysis:
- repeated vegetation stamps;
- stylistically alien tiles;
- inconsistent lighting/material families;
- duplicate patches;
- local texture seams.

### Unified lightweight secondary critic

Florence-2 can provide captioning, detection, grounding and segmentation in one
prompt-driven model. It is useful as an additional independent critic, not as
the map truth source.

## Final decision rule

The engine/map data decides factual correctness.

Image models may say:

> "This looks like a freestanding door at region X."

The QA system then asks:

- What map node produced the pixels?
- Which tile family and frame produced it?
- What does the JSD say?
- Was the same visual/semantic object present in pristine output?
- Is the frame semantically understood?
- Is this change permitted by the current scope?

If engine/map provenance contradicts the apparent object, the render fails.

If the image critic reports a plausible aesthetic problem but structural truth
is intact, the result is a visual-review item rather than a semantic hard fail.

## Acceptance gate for a reference sector

A sector is not a production visual reference until all are true:

1. Graphics-only scope is proven.
2. Structural generator strict gate passes, with any fallback explicitly named.
3. Independent structural verifier passes.
4. Visual-QA regression benchmark passes.
5. Six paired engine-render views exist.
6. Paired render QA has zero unexplained local structural hotspots.
7. No unresolved object-localisation warning survives provenance review.
8. Actual tactical-scale screenshot has been reviewed; asset sheets do not count.
9. Colour/material direction is coherent at sector scale.
10. The original remains available as a deterministic fallback.

## A3 status after this investigation

The QA methodology is now materially stronger and regression-tested.

A3 itself is not yet visually accepted. The old screenshot containing the false
freestanding structures must not be used as evidence of the corrected output.
The corrected assets need to be regenerated/deployed and A3 must be rendered
again through the engine before the sector can pass visual acceptance.
