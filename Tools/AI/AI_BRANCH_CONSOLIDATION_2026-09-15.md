# AI Branch Consolidation — 2026-09-15

Canonical branch: `install/all-2026-09-12`

## Purpose

This records the branch-consolidation pass performed after the game-AI methodology review. The policy is one canonical playable/integration branch, with only short-lived active workstreams and deliberately retained archaeology/staging references.

## Canonical changes made in this pass

- expanded the shared `AITACTICALDECISIONCONTEXT` so force strength, casualty state, disengagement and escape state are computed once and exposed consistently;
- consolidated RED/BLACK personal-risk withdrawal through one context-driven helper;
- added unique-soldier identity binding for transient intent/role planner state;
- added an explicit quickload reset for transient tactical planner state;
- preserved the existing legal-information contract, persistent intent, competence model, fireteams, CQB and utility systems;
- did not introduce a second planner, second knowledge model, or second tactical telemetry stream.

## Verified redundant branches deleted

The following refs were verified as subsumed, superseded, identical, or obsolete validation/history before deletion:

- `ai/ap-budgeting`
- `ai/combat-dispersion`
- `ai/combat-medic-rescue`
- `ai/covering-fire-cooperation`
- `ai/deidranna-doctrine`
- `ai/emergency-casualty-smoke`
- `ai/fireteam-cohesion`
- `ai/human-tactical-final`
- `ai/individual-self-preservation`
- `ai/legacy-core-modernization`
- `ai/local-advance-cooperation`
- `ai/no-weapon-self-preservation`
- `ai/radio-support-doctrine`
- `ai/range-aware-positioning`
- `ai/search-confidence-decay`
- `ai/shared-enemy-militia-brain`
- `ai/support-aware-withdrawal`
- `ai/target-allocation`
- `ai/team-coordination`
- `ai/utility-squad-planner`
- `ai/wound-self-preservation`
- `ai/wounded-tactical-withdrawal`
- `final-human-ai-modern-113`
- `feature/enemy-role-identification-113`
- `integration/unified-ai-fireteams-doctrine-2026-09-14`
- `integration/unified-ai-framework-2026-09-14`
- `inactive/cqb-building-doctrine-2026-09-14`
- `validation/cqb-live-2026-09-14`
- `telemetry/companion-sessions`
- `blackbox/v5-reliability-2026-09-15`
- `reconcile/streamline-2026-09-15`

The one-shot deletion workflow used for this cleanup was removed immediately after successful execution.

## Protected live workstreams

Do not retire or overwrite these while they are active:

- `ai/cqb-doctrine-2026`
- `strategic/campaign-modernization-2026`

Both were one unique commit ahead of canonical during this audit and are intentionally treated as concurrent workstreams.

## Deliberately retained strategic references

These remain because the strategic staging manifest still identifies un-forward-ported historical concepts such as transport groups, helicopters, ASD purchasing and broader operational mission issuance:

- `inactive/strategic-modernization`
- `integration/unified-strategic-companion-2026-09-14`
- `consolidation/install-all-2026-09-14`
- `diagnostics/campaign-companion-v2`
- `diagnostics/strategic-campaign-blackbox`
- `diagnostics/strategic-campaign-companion`

These are reference/archaeology branches only. New strategic AI development must start from canonical and return to canonical.

## Validation state

- AI fragmentation/integrity audit passed after the tactical changes.
- Hosted Windows integration translation-unit compile passed.
- Self-hosted Release Win32 build was still running when this record was first written; check the corresponding integration-build run before treating the pass as fully release-validated.

## Concurrency rule

Another chat may develop AI simultaneously. Integration work must therefore re-read the latest canonical head before every write, forward-port unique work rather than overwrite it, and avoid creating parallel implementations of the same tactical responsibility.
