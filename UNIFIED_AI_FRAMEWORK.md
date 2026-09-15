# Vengeance Unified AI Framework

Branch: `install/all-2026-09-12`

## Purpose

`install/all-2026-09-12` is the single canonical integration line for Vengeance AI work. It is based on
`ai/utility-squad-planner` because that branch contains the newest coherent tactical planner.
Older AI branches are reference material only: useful behaviours are ported deliberately into
this framework, never merged wholesale.

## Architecture

1. **Legal perception**
   - JA2 personal/public knowledge, sight, noise and legitimate radio reports only.
   - No hidden AP, exact unseen position, hidden equipment, or raw sector-strength cheats.

2. **Explicit contact beliefs**
   - `AICONTACTBELIEF` normalizes legal last-known position, source, age and confidence.
   - Beliefs represent uncertainty; they never reveal hidden live opponent state.
   - Surprise is created by genuinely new personal sight, not by omniscient prediction.

3. **Competence / friction**
   - Administrators and green militia use simpler/noisier reasoning.
   - Regular army and militia use coordinated tactics inconsistently.
   - Elites can reliably exploit deeper candidate evaluation.
   - Competence changes reasoning and execution, never CTH/AP.

4. **Battle state**
   - casualties, perceived force balance, stress, risk, morale, isolation and rout pressure.

5. **Persistent local intent**
   - HOLD, PRESS, FLANK, FALLBACK, DISENGAGE, RESCUE.

6. **Dynamic fireteam role**
   - SUPPORT, MANEUVER, FLANKER, SCREEN, RESERVE.

7. **Local task reservations**
   - fireteam-scoped claims prevent duplicate flank, maneuver and screen responsibilities.
   - reservations are transient, identity-bound and expire quickly.
   - coordination never grants extra opponent knowledge.

8. **Interruptible short plans**
   - 1–3 step tactical commitments preserve useful intent without becoming rigid scripts.
   - FLANK, FALLBACK, DISENGAGE, RESCUE and CQB-style sequences can persist briefly.
   - plans are revalidated against fresh risk/battle state and are cancelled by emergencies/new information.

9. **Shared spatial intelligence**
   - candidate evaluation uses reusable features: cover, sight/prone cover, known-threat exposure,
     support, crowding, crossfire, range fit, smoke, mission progress, route exposure and inferred reaction risk.
   - full route analysis is allowed when it materially improves the decision.
   - analysis uses non-destructive path queries so evaluation does not corrupt execution state.

10. **Contact-surprise / encirclement reassessment**
    - movement that unexpectedly reveals new personal contacts invalidates the old movement commitment.
    - the soldier can hold, return to the last decision tile, seek cover, withdraw or make a lateral/backward move.
    - multi-angle visible pressure can also trigger repositioning when the geometry worsens.
    - the response is scored rather than hard-scripted; if no candidate is materially better, the soldier fights where he is.

11. **Local fire plan**
    - target saturation, covering fire, fire superiority, alternate-arc preservation.

12. **Action utility**
    - legacy Vengeance / 1.13 behaviours remain the execution library.
    - the planner decides when those behaviours are appropriate.

13. **Execution friction**
    - lower-quality troops may fall back to a simpler legal action instead of executing
      the mathematically best complex plan.

14. **Outcome feedback**
    - Black Box records raw facts, candidate scores and selections; Companion reconstructs plans,
      reasons and outcomes.

### Performance policy

The target machine has enough CPU headroom for deeper tactical search. Prefer better decisions over
micro-optimizing away useful route/candidate analysis. Avoid large persistent caches when recalculation
is cheap enough; tactical state should remain small, transient and identity-bound.

## Branch policy

- `install/all-2026-09-12` is canonical for all new tactical AI development.
- Strategic modernization and diagnostics are ported onto this line.
- No AI feature branch may become a second permanent integration branch.
- Experimental features branch from this line and return through reviewed commits.
- Historical branches are deleted once uniqueness is disproved; reference branches are retained only while they contain documented, genuinely unported work.
- A behaviour is considered integrated only when:
  1. its code is present here;
  2. it compiles;
  3. it is visible in Black Box / Companion telemetry;
  4. its interactions with morale, smoke, suppression, movement and retreat are tested.


## Consolidation status — 2026-09-15

The unified line now includes the professor-architecture foundation:

- explicit legal contact beliefs with confidence/source/age;
- shared spatial feature evaluation and utility scoring;
- safe full-route exposure/reaction-risk analysis via `NO_COPYROUTE`;
- fireteam-local task reservations;
- identity-bound interruptible short plans;
- contact-surprise and encirclement reassessment that can stop/reverse a bad advance;
- Black Box candidate/selection telemetry for surprise repositioning;
- quickload/reset hardening for all new transient reasoning state.

The older tactical feature branches whose unique behavior was already represented on canonical were
deleted after function-level review. Strategic reference branches remain only where the staging manifest
still records genuinely unported strategic concepts.

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
