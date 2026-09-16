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

3. **Universal enemy competence / non-enemy friction**
   - Every ENEMY_TEAM combatant uses elite/top-end tactical reasoning.
   - Enemy mission/doctrine labels may change posture, role or objective; they must not deliberately reduce reasoning quality.
   - Enemy planning has no competence-failure roll and no artificial utility noise.
   - Militia/non-enemy AI may still use BASIC / REGULAR / ELITE execution-friction tiers where appropriate.
   - Competence never grants CTH/AP/damage/vision/perception bonuses.

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

9. **Shared spatial intelligence / local battlefield geometry**
   - candidate evaluation uses reusable features: cover, sight/prone cover, known-threat exposure,
     support, crowding, crossfire, range fit, smoke, mission progress, route exposure and inferred reaction risk.
   - an 8-sector geometry model summarizes the primary/secondary known threat axes, local friendly
     pressure, open left/right flanks, rear safety, and the weakest breakout direction.
   - flank side selection, flank/fallback tile search, CQB position scoring and break-contact movement
     consume the same geometry instead of maintaining separate directional heuristics.
   - full route analysis is allowed when it materially improves the decision.
   - analysis uses non-destructive path queries so evaluation does not corrupt execution state.

10. **Contact-surprise / encirclement reassessment**
    - movement that unexpectedly reveals new personal contacts invalidates the old movement commitment.
    - the soldier can hold, return to the last decision tile, seek cover, withdraw or make a lateral/backward move.
    - multi-angle visible pressure can also trigger repositioning when the geometry worsens.
    - under multi-angle pressure, fallback/disengagement can search the weakest sector instead of assuming
      the correct retreat direction is directly away from the closest opponent.
    - remembered/heard contacts influence caution, but an encirclement conclusion requires personally visible
      separated threat sectors.
    - the response is scored rather than hard-scripted; if no candidate is materially better, the soldier fights where he is.

11. **Local fire plan**
    - target saturation, covering fire, fire superiority, alternate-arc preservation.

12. **Action utility**
    - legacy Vengeance / 1.13 behaviours remain the execution library.
    - the planner decides when those behaviours are appropriate.

13. **Battle-local setback memory**
    - genuinely bad tactical outcomes create short-lived local memory of the affected ground.
    - surprise/encirclement tiles and exposure-rejected CQB approaches are remembered briefly.
    - destination and route scoring penalize those areas while the memory decays.
    - nearby members of the same fireteam may use the lesson at reduced strength; there is no sector-wide danger map.
    - setback state is transient, identity/sector-bound, reset on load/rewind, and never creates opponent knowledge.

14. **Execution friction**
    - ENEMY_TEAM does not receive artificial tactical mistakes or complexity failures to simulate lower training.
    - stress, suppression, wounds, legal uncertainty and mission role can change the correct enemy decision, but do not make the enemy forget advanced tactics.
    - militia/non-enemy combatants may still fall back to simpler legal actions according to their competence tier.

15. **Outcome feedback**
    - Black Box records raw facts, candidate scores, setback penalties and selections; Companion reconstructs plans,
      reasons and outcomes.

16. **Player tactical command mode**
    - The player may explicitly hand the squad to the same tactical AI used by the unified planner for the duration of the tactical engagement.
    - `ATTACK AS TEAM` fixes the team objective at PRESS while leaving emergency self-preservation/casualty logic authoritative.
    - `WITHDRAW AS TEAM` fixes the team objective at FALLBACK and starts with the geometry-aware weakest-sector breakout search: normally away from the principal legally known threat, but lateral/diagonal when crossfire makes the nominal rear unsafe.
    - Command mode persists across player/enemy/militia rounds until tactical combat resolves or the player explicitly reclaims control.
    - `ESC` queues manual takeover at the next clean AI-soldier action boundary, preserving already-spent AP and the remainder of the current player turn.
    - Player-team interrupts remain under AI command while takeover is active; reclaiming control during an interrupt returns that interrupt to the player.
    - The player squad is treated as one coordinated element for roles/task deconfliction, but mercs receive no hidden opponent knowledge.
    - Player command mode never invokes enemy strategic escape or campaign movement. Withdrawal remains inside the current tactical sector.
    - A fresh player command resets only the commanded mercs' tactical-fallback allowance; enemy/militia anti-kiting state is unchanged.
    - First implementation is turn-based single-player only and is disabled during boxing/scripted control states.

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
- an 8-sector local battlefield geometry model with threat axes, open flanks, rear safety and weakest-sector breakout;
- safe full-route exposure/reaction-risk analysis via `NO_COPYROUTE`;
- geometry-driven flank selection and flank/fallback candidate scoring instead of random left/right tie-breaking;
- fireteam-local task reservations, including CQB point/support entry claims;
- identity-bound interruptible short plans, including a CQB wrapper owned/invalidation-controlled by the CQB planner;
- contact-surprise and visible-encirclement reassessment that can stop/reverse a bad advance or break through the safer sector;
- Black Box candidate/selection telemetry for surprise repositioning;
- short-lived fireteam-local setback memory so ambush tiles and rejected CQB approaches are not mechanically retried;
- quickload/turn-rewind/reset hardening for all new transient reasoning state.

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


## Mandatory JA2+AI / sevenfm research gate

Before a material Tactical AI behavior is implemented or materially retuned, consult
`Tools/AI/JA2_PLUS_AI_PHD_2026-09-16.md`.

Required questions:

1. Is there a JA2+AI/sevenfm precedent and what practical problem was it solving?
2. Was that behavior later changed, disabled or reverted after player testing?
3. Is the behavior already inherited in Vengeance or modern 1.13?
4. Does the unified planner supersede it, or should legacy sevenfm logic remain the execution layer?
5. Could planner gating suppress a mature behavior that already works well?
6. Does the change preserve the legal-information contract and local-fireteam boundary?
7. Which historical JA2+AI failure mode becomes a regression test?

JA2+AI is the primary historical external Tactical AI benchmark. Bear's Pit gameplay evidence is
first-class evidence for behavior quality, especially for suppression, flanking, smoke, night
combat, retreat, danger avoidance, pathing deadlocks and large-battle performance.

Research completion does not increase implementation completion percentage.


## Mandatory Upgrade Army AI doctrine gate

Before a material Tactical AI behavior is implemented or retuned, consult
`Tools/AI/UPGRADE_ARMY_AI_DOCTRINE_2026-09-16.md`.

For ENEMY_TEAM this doctrine supersedes older heterogeneous-intelligence concepts:
- every enemy reasons at elite/top-tier special-operations level;
- equipment/resources/mission role may differ, intelligence quality does not;
- local fireteams share bounded, confidence-decayed planning information without creating direct-fire authorization;
- bravery means accepting useful risk, not ignoring self-preservation;
- fireteam intent, complementary roles, reservations, bounding, rescue, covering withdrawal, remnant reattachment and staged response are first-class requirements;
- strategic/campaign AI remains read-only.

The JA2+AI dossier remains the primary historical external benchmark. The governing integration rule is:
**unified planner for team cognition + mature Vengeance/sevenfm code for low-level legal execution**.

Planner changes must be checked for accidental veto of strong mature behavior and exposed through Black Box / Companion telemetry.
