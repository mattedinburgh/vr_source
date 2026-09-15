# Branch Reconciliation — 2026-09-15

This file records cleanup decisions. It is intentionally conservative: **unique content is preserved until proven obsolete, ported, or explicitly rejected.**

## Canonical direction

- De-facto old canonical: `install/all-2026-09-12`
- Temporary cleanup branch: `reconcile/streamline-2026-09-15` — reconciled and retired
- Current integration spine: `install/all-2026-09-12`
- Final target: `master`
- Safety snapshot: `archive/pre-streamline-2026-09-15`
- Old-master snapshot: `archive/master-pre-streamline-2026-09-15`
- Pre-reconciliation integration snapshot: `archive/install-all-pre-reconcile-2026-09-15`
- VHD temporary experiment: `exp/vhd`

## Current-base active workstreams

- `install/all-2026-09-12` — integration spine pending final master cutover.
- `exp/vhd` — HD/VHD renderer experiment.
- `maps/visual-overhaul-2026` — map visual overhaul and authoring tools.
- `strategic/campaign-modernization-2026` — strategic/campaign modernization staging.
- `ai/cqb-doctrine-2026` — CQB doctrine staging.
- `world/weather-modernization-2026` — weather modernization design/staging.

Matching gamedir lanes:
- `maps/visual-overhaul-2026`
- `graphics/item-icons-2026`
- `ui/cold-ui-2026`
- `audio/ambience-2026`
- `exp/vhd`

The branch inventory captured 87 source branches and 29 gamedir branches before retirement work; most are historical/backup/validation lines and should receive no new development.

## Selectively reconciled into cleanup branch

### Source
- master-only sector inventory/ammo/grenade logic
- anti-materiel bullet flag and structure-damage path
- ammo target-specific damage fields and XML parsing
- ammo breath/stun modifier
- telemetry link implementation (`VRAnalytics.cpp`; header declarations were already present)
- compact Black Box hang dump + current heartbeat-thread tracking
- item-description Black Box breadcrumbs

### Game data
- current enemy item-choice/radio-equipment data
- modern ammo-type fields/data
- magazine mappings
- rubber-slug item definitions 3043–3049

### Explicitly *not* imported from master
- experimental item-icon binary sheets and pilot documentation; in-game art was not accepted.

## Safe-to-retire code branches after master cutover

These have no required unique gameplay content relative to the consolidated line, or their unique commit history was explicitly superseded by later manual/content-level integration:

- `ai/ap-budgeting`
- `ai/combat-dispersion`
- `ai/combat-medic-rescue`
- `ai/covering-fire-cooperation`
- `ai/emergency-casualty-smoke`
- `ai/fireteam-cohesion`
- `ai/individual-self-preservation`
- `ai/local-advance-cooperation`
- `ai/no-weapon-self-preservation`
- `ai/radio-support-doctrine`
- `ai/range-aware-positioning`
- `ai/search-confidence-decay`
- `ai/support-aware-withdrawal`
- `ai/target-allocation`
- `ai/utility-squad-planner`
- `ai/wound-self-preservation`
- `ai/wounded-tactical-withdrawal`
- `feature/downed-casualties`
- `feature/enemy-role-identification-113`
- `feature/final-missing-2026-09-12`
- `feature/hd-tactical-2x`
- `feature/lobot-equipment-graphics-20260913`
- `feature/lobot-integration-20260913`
- `feature/sector-inventory-loadout-buttons`
- `feature/sparse-png-item-overrides-2026-09-14`
- `feature/visible-equipment-port-complete-20260914`
- `modern-113-shooting`
- `telemetry/companion-sessions`
- `work/enemy-loadout-planner-2026-09-13`
- old backup/pre-* branches
- old `integration/final-*`, `final-human-ai-modern-113`, and unified AI integration branches once the final master cutover is complete

Important: visible-equipment branch retirement does **not** close the visible-armour defect.

## Diverged branches whose unique history is superseded — do not merge wholesale

- `ai/human-tactical-final`: old divergent AI line; later compatible behavior was reconciled into the all-in-one line.
- `ai/legacy-core-modernization`: reviewed feature-by-feature; additive/fairness pieces were ported, not the branch.
- `ai/shared-enemy-militia-brain`: independent planner intentionally not restored.
- `ai/team-coordination`: superseded by newer unified planner/coordination implementation.
- `ai/deidranna-doctrine`: later doctrine work was manually/content-level integrated.
- `launch/2026-09-12`: historical launch line; later canonical branch contains compatible/superseding rescue/tactical work.
- `final/all-work-2026-09-12`: old regional enemy-supply implementation; current canonical has a newer logistics model.
- validation/* branches whose only unique content is old branch-specific workflow YAML.

## DO NOT DELETE YET — unique unfinished work

### VHD
Old branches are being collapsed into `exp/vhd`.
- `feature/vhd-renderer` — superseded base.
- `engine/vhd-render-instrumentation-2026-09-15` — used as the clean current base for `exp/vhd`.
- `engine/vhd-cache-modernization-2026-09-15` — cache/memory work has been ported to `exp/vhd`.
- `engine/vhd-render-diagnostics-2026-09-15` — older CSV diagnostics; do not reintroduce over current Black Box instrumentation.
- `engine/vhd-occlusion-compositor-2026-09-15` — one-pass indexed compositor has been ported to `exp/vhd` and adapted to current Black Box frame telemetry; historical branch can retire after final VHD verification.
- `feature/vhdl-hd-layer` — audited; its scale/projection APIs are already present on `exp/vhd`, so no missing functionality remains.
- `engine/vhd-hardening-2026-09-15` — content already integrated in the VHD lineage; retire after final exp/vhd verification.

### Maps
- Current lane: `maps/visual-overhaul-2026`.
- Map Factory docs, art direction and authoring tools have been preserved on the current-base lane.
- Audit of the legacy A3/Map Factory engine patches found automatic map dressing/baking and QA machinery that mutates map content. That approach is rejected for the present graphics-only programme.
- The safe lighting change that prevents forced shade-table builds from being cached was ported to the current map lane.
- Old `map-factory-v1` and `a3/hand-authored-v2` remain historical references, not active development lines.

### Strategic / analytics experiments
Do not merge old branches wholesale.

Current-base preservation lanes now exist:
- `strategic/campaign-modernization-2026` preserves standalone ASD, strategic-modernization, operational-AI, transport and campaign-telemetry modules.
- `ai/cqb-doctrine-2026` preserves the standalone CQB doctrine implementation and design documents.
- `world/weather-modernization-2026` preserves the weather modernization design.

Historical branches remain references only for shared-file patches that still require deliberate adaptation:
- `feature/vr-analytics-foundation`
- `diagnostics/campaign-companion-v2`
- `diagnostics/strategic-campaign-blackbox`
- `diagnostics/strategic-campaign-companion`
- `design/weather-modernization-inactive-2026-09-13`
- `inactive/cqb-building-doctrine-2026-09-14`

### UI/art
- `art/cold-ui-pilot`: preserve as design reference until the cold UI is redesigned/accepted.
- item-icon pilot/generation branches in `vr_gamedir`: preserve until the icon redesign chooses a source set.

## Game-data icon lineage

- `art/item-icons-pilot-20` — earliest actual in-game 20-icon pilot.
- `pilot/inventory-icons-20-2026-09-14` — later pilot source set.
- `save23/inventory-icons-ai-2026-09-14` — direct successor to the later pilot (+3 commits).
- `art/all-inventory-icons-ai-2026-09-14` — separate bulk-generation experiment; diverged from save23.
- `art/save20-hd-png-overrides-2026-09-14` — separate HD override experiment; not the accepted icon line.

No icon experiment is canonical until it passes in-game visual review.

## Accidental cleanup branches

A `develop` branch was created during the initial cleanup plan. It contains no independent intended product work and must not become a second development line. After master cutover it should be retired/deleted (or left permanently unused if branch deletion is unavailable).

## Promotion checklist

Before moving `master` to reconciliation:
- [x] audit VHD `feature/vhdl-hd-layer` unique changes
- [x] port one-pass VHD occlusion compositor to current Black Box instrumentation
- [x] preserve current map factory tooling and establish a current-base map lane
- [x] establish current-base icon/UI/audio/map gamedir lanes without promoting rejected art
- [ ] source full Release build passes
- [x] source hosted compile passes
- [x] game-data/XML consistency passes
- [ ] basic launch/runtime smoke passes
- [ ] current AI/Black Box play session does not expose a blocker
- [x] archive old master tips before force/fast-forward cutover
