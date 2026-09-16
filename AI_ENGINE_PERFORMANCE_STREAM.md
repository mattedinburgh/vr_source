# AI Engine / Performance Stream

Branch: work/ai-shared-brain-2026-09-16
Canonical integration target: 2026 09 12
Strategic/campaign layer: read-only.

## Evidence from 2026-09-17 00:00 battle
- Enemy-turn anti-freeze path cap prevented the previous multi-minute ENEMY_TEAM path search, but the battle remained unplayably slow.
- Worst stalls were mainly MILITIA_TEAM soldiers 151, 155 and 159.
- Observed main-thread stalls included roughly 45.6 s, 43.0 s and 35.5 s.
- The emergency path cap originally covered ENEMY_TEAM only, leaving MILITIA_TEAM uncapped.
- Even bounded ENEMY_TEAM turns could still take 8-12 s because many individually bounded path searches can accumulate inside one tactical decision.
- Black-box session contained 431 decision starts, 349 commits and 308 outcomes.
- Repeated AI_ACTION_RAISE_GUN could spend 0 AP and immediately trigger another expensive tactical evaluation.

## Emergency playable-build safeguards
These are safety rails, not the target architecture:
1. Per-FindBestPath hard cap.
2. Apply the cap to ENEMY_TEAM and MILITIA_TEAM.
3. Add a cumulative per-soldier pathfinding allowance per tactical team turn so many legal 25 ms searches cannot accumulate into seconds.
4. Repeated no-progress RAISE_GUN with no meaningful queued follow-up ends the soldier turn rather than forcing a full replan.
5. Preserve player pathfinding unchanged.

## Long-term engine fix
Goal: fast, bounded grandmaster/no-cheat tactical AI without reducing tactical quality.

### 1. Shared fireteam tactical brain
Build AIFireteamTacticalContext on top of the existing fireteam/contact infrastructure:
- AIFireteamId
- AISameFireteam
- AILocalFireteamCommHops
- AISharedFireteamContact
- AISharedFireteamOpponentContact
- AIPlanningContactForOpponent

Shared context contains only legally available fireteam information:
- believed opponent contacts, confidence and uncertainty
- threat directions
- shared tile exposure/danger
- cover and sight-cover fields where safe to cache
- smoke/gas/hazard fields
- support density
- crossfire geometry
- flank opportunities
- fallback/escape geometry
- local tactical axes/objective geometry

Soldier-specific data remains private:
- AP
- weapon/range/ammo
- stance
- wounds
- role
- morale/suppression
- personal movement limitations
- individual orders/intent

### 2. Cache expensive tactical fields
Stop rebuilding the same opponent-contact picture in AIKnownThreatExposure and AIInferredReactionRisk.
Cache by fireteam / battlefield revision / grid / level:
- exposure
- inferred reaction risk
- LOS threat geometry
- cover bits
- support/crossfire base values
- hazards

Invalidate on meaningful events only:
- new/lost contact
- meaningful contact relocation
- smoke/gas/door geometry change
- teammate death/major relocation
- fireteam split/merge
- objective/intent change

### 3. Reentrant reachability/path engine
Replace repeated destination-by-destination FindBestPath calls with one bounded reachability expansion per soldier decision:
- AP cost to reachable tiles
- predecessor map
- route exposure/risk/hazard accumulation
- reconstruct final route from predecessors

Eliminate duplicate patterns where one candidate currently triggers 2-3 separate path searches through:
- AIKnownRouteExposureAcceptable
- AIPathExposureCost
- AIUtilityPositionScore / AIEvaluateTacticalPosition

Longer term move shared globals such as guiPathingData, gubNPCAPBudget and gubNPCDistLimit into AIPathContext so path queries become reentrant and thread-safe.

### 4. Candidate funnel
- cheap broad scan from cached fields
- retain top K
- detailed scoring only for finalists
- bounded second-ply only for top 2-3 candidates
- never recursively invoke the full expensive evaluator for lookahead

### 5. Whole-decision budget
Implement explicit per-soldier planning context:
- soft time budget: stop optional/deeper work
- hard time budget: choose best candidate found so far
- path query count budget
- candidate count budget
- lookahead node budget
No AI decision is allowed to block the main thread for seconds.

### 6. No-progress action protection
Generalize beyond RAISE_GUN:
detect action completion with no meaningful change in:
- AP
- grid
- stance/facing/weapon-ready state
- tactical state
Prevent repeated expensive replans for the same ineffective setup action.

### 7. Instrumentation
Per decision log:
- total ms
- candidate generation ms
- reachability/path ms
- exposure ms
- reaction-risk ms
- cover/LOS ms
- number of FindBestPath calls
- nodes processed
- cache hits/misses
- cheap/detail/lookahead candidate counts
- budget early-outs
- no-progress actions

### 8. Multicore after reentrancy
Do not parallelize whole soldiers against shared mutable JA2 globals.
After immutable contexts exist, parallelize safe independent work:
- exposure field
- cover field
- support/crossfire field
- tactical geometry
- independent finalist scoring
Final action selection/world mutation remains sequential.
Battle Arena can immediately scale across separate processes/cores.

## Implementation order
1. Fireteam contact/threat snapshot and cache.
2. Cache AIKnownThreatExposure / AIInferredReactionRisk inputs.
3. Remove duplicate path/evaluation calls.
4. Add explicit whole-decision budget/context.
5. Single reusable reachability expansion.
6. Cheap top-K funnel.
7. Reintroduce bounded depth-2 lookahead.
8. Refactor PathAI into reentrant AIPathContext.
9. Parallelize safe immutable field computation.
10. Benchmark continuously in AI Battle Arena and live battle logs.

## Release convention
Every playable game release must be copied into the game root with a timestamp in the filename using JA2_EN_Release_YYYY-MM-DD_HH-MM-SS.exe. The timestamped executable is the canonical test build for log correlation.
