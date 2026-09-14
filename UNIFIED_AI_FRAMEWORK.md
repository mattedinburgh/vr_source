# Vengeance Unified AI Framework

Branch: `integration/unified-ai-framework-2026-09-14`

## Purpose

This branch is the single canonical integration line for Vengeance AI work. It is based on
`ai/utility-squad-planner` because that branch contains the newest coherent tactical planner.
Older AI branches are reference material only: useful behaviours are ported deliberately into
this framework, never merged wholesale.

## Architecture

1. **Legal perception**
   - JA2 personal/public knowledge, sight, noise and legitimate radio reports only.
   - No hidden AP, exact unseen position, hidden equipment, or raw sector-strength cheats.

2. **Competence / friction**
   - Administrators and green militia use simple, noisy plans.
   - Regular army and militia use coordinated tactics inconsistently.
   - Elites can reliably use the full planner.
   - Competence changes reasoning and execution, never CTH/AP.

3. **Battle state**
   - casualties, perceived force balance, stress, risk, morale, isolation and rout pressure.

4. **Persistent local intent**
   - HOLD, PRESS, FLANK, FALLBACK, DISENGAGE, RESCUE.

5. **Dynamic fireteam role**
   - SUPPORT, MANEUVER, FLANKER, SCREEN, RESERVE.

6. **Local fire plan**
   - target saturation, covering fire, fire superiority, alternate-arc preservation.

7. **Action utility**
   - legacy Vengeance / 1.13 behaviours remain the execution library.
   - the planner decides when those behaviours are appropriate.

8. **Position / route utility**
   - cover, known-threat exposure, crossfire, useful weapon range, support, crowding,
     smoke, route exposure and inferred reaction-fire risk.

9. **Execution friction**
   - lower-quality troops may fall back to a simpler legal action instead of executing
     the mathematically best complex plan.

10. **Outcome feedback**
    - Black Box records raw facts; Companion records plans, reasons and outcomes.

## Branch policy

- This branch is canonical for all new tactical AI development.
- Strategic modernization and diagnostics are ported onto this line.
- No AI feature branch may become a second permanent integration branch.
- Experimental features branch from this line and return through reviewed commits.
- Old branches stay available for archaeology until all unique behaviours are catalogued.
- A behaviour is considered integrated only when:
  1. its code is present here;
  2. it compiles;
  3. it is visible in Black Box / Companion telemetry;
  4. its interactions with morale, smoke, suppression, movement and retreat are tested.


## Consolidation status — 2026-09-14

Integrated onto the unified line:

- persistent sector-local enemy fireteams with coherent reserve release and remnant absorption;
- Deidranna doctrine profiles that limit initiative/complexity without granting combat-stat bonuses;
- doctrine-aware crossfire, bounding, independent flanking, proactive support/smoke and mission anchoring;
- doctrine/fireteam fields in the tactical decision stream for Companion analysis.

Divergent branches are not merged wholesale:

- `ai/team-coordination` is superseded by the newer utility planner and is reference-only;
- `ai/shared-enemy-militia-brain` has no unique planner interface worth restoring independently;
- `ai/legacy-core-modernization` remains archaeology until each unique helper is proven to add behaviour not already represented by the unified planner;
- strategic Companion/Black Box work remains a separate port because it touches a wider strategic/tactical surface.

## Legacy + modern coexistence

The old Vengeance / 1.13 AI is retained as an execution library, not discarded.

- Legacy code answers: *How do I perform this legal action?*
- Unified planner answers: *Should I perform it now, with which role, against which known
  contact, and at what risk?*
- Hard emergency/survival rules may pre-empt the planner.
- A legacy heuristic must not independently re-trigger a behaviour already reserved by the
  planner; adapters/gates prevent double decisions.

## Strategic integration policy

Strategic modernization remains gated until tactical integration is stable.

The eventual shared loop is:

strategic mission -> tactical posture -> tactical outcome -> formation losses/morale/supply
-> retreat/regroup/reserve -> new strategic mission.

Strategic intelligence must use delayed/degraded dissemination. A contact report must not
instantly update every enemy formation.

## Diagnostic policy

Every material AI change must expose enough information for the daily Companion review to
answer:

- What did the AI know?
- What alternatives did it consider?
- Why did it reject alternatives?
- Why was the selected action preferred?
- Did execution match the plan?
- What happened afterward?
- Which subsystem materially drove the result?
- Did the change improve behaviour across battles or only one anecdote?

See `Diagnostics/AI_COMPANION_ANALYSIS_CONTRACT.md`.
