# Tactical AI knowledge contract

This note defines the information boundary for the human-tactical AI work.

## Existing JA2 knowledge model

Use the engine's existing opponent-knowledge system instead of reading hidden opponent state directly.

For each opponent, a soldier can have:
- personal knowledge: `pSoldier->aiData.bOppList[id]`
- personal last-known location/level: `gsLastKnownOppLoc` / `gbLastKnownOppLevel`
- team/public knowledge: `gbPublicOpplist[team][id]`
- team/public last-known location/level: `gsPublicLastKnownOppLoc` / `gbPublicLastKnownOppLevel`

Knowledge ages through:
- HEARD_THIS_TURN, HEARD_LAST_TURN, HEARD_2_TURNS_AGO, HEARD_3_TURNS_AGO
- SEEN_CURRENTLY, SEEN_THIS_TURN, SEEN_LAST_TURN, SEEN_2_TURNS_AGO, SEEN_3_TURNS_AGO
- NOT_HEARD_OR_SEEN

The public and personal lists decay automatically. Public knowledge is updated through existing radio/noise mechanisms.

## Required API for new tactical logic

Prefer:
- `Knowledge(pSoldier, opponentID)`
- `KnownLocation(pSoldier, opponentID)`
- `KnownLevel(pSoldier, opponentID)`
- `PersonalKnowledge(...)` / `PublicKnowledge(...)` only when the distinction itself matters

`Knowledge` already selects the more useful/recent personal or public information using the engine's knowledge-value table. This matches current 1.13 behaviour.

## Hard anti-cheat rules

New AI must not:
- use an opponent's real current grid when that opponent is not currently known there
- count hidden AIM mercs from raw team/sector totals as perceived enemy strength
- treat a stale last-known location as exact current position
- infer exact hidden enemy health, stance, equipment or movement from raw soldier state unless existing perception already exposes it
- create a second omniscient perception system

New AI may:
- use currently seen opponents
- use heard contacts and last-known positions with lower certainty
- use legitimate team/public knowledge shared by the engine
- reason from incomplete knowledge
- use friendly-team state where existing JA2 AI already assumes friendly locations/status are known, while keeping immediate casualty shock local/witnessed rather than sector-omniscient

## Scale rule

Do not invent real-world metre conversions or arbitrary fixed radii unless unavoidable.

Prefer existing JA2 concepts:
- sight/visibility ranges
- weapon/tactical ranges
- existing nearby-friend helpers
- existing cover/pathfinding search bounds
- known-opponent distances

Movement goals should select a better tactical position, not a hard-coded number of tiles backward.

## Implication for later chunks

A perceived force ratio is not actual sector force ratio.

Example: if one surviving soldier knows of four AIM contacts, the tactical AI reasons from those four known contacts plus its loss of friendly support. It must not know that five additional AIM mercs are hidden elsewhere in the sector.

This contract applies to enemy and militia tactical AI changes.


## Chunk 1: battlefield situation awareness

Chunk 1 adds information-only helpers. No action-selection call site uses them yet.

- `AIObservedRecentCasualties`: visible fresh friendly corpses plus nearby incapacitated teammates, using `TACTICAL_RANGE`.
- `AILocalCasualtyPercent`: local observed/down casualty share relative to nearby combat-capable teammates.
- `AIFriendlyCasualtyPercent`: local casualty share, plus the engine's existing army battle-loss percentage for enemy troops.
- `AIPerceivedFriendlyStrength`: combat-capable teammates within `TACTICAL_RANGE`; 100 points per soldier.
- `AIPerceivedEnemyStrength`: only opponents present in personal/public JA2 knowledge, at their known location, weighted by the existing `ThreatPercent` certainty table.
- `AIBattleSituation`: classifies UNKNOWN / WINNING / EVEN / LOSING / CATASTROPHIC from perceived local strength and friendly losses.

The opponent-strength helper never reads hidden AIM positions, hidden sector enemy counts, or hidden current opponent health. A stale/heard contact contributes less than a currently seen contact through JA2's existing knowledge certainty table.

## Chunk 2: hopeless fights and survivor behaviour

Chunk 2 turns the Chunk 1 assessment into a limited behaviour change without adding a new pathfinder.

- `AISeverelyIsolated`: one/two combat-capable local soldiers facing at least equal known opposition.
- `AILastSurvivorPressure`: true last one/two combat-capable team survivors after heavy losses, or a one/two-man local remnant with direct local casualty evidence.
- `AIHopelessOddsModifier`: soft RED-state seek penalty for losing/catastrophic situations.
- `AIShouldAvoidAdvance`: hard gate for catastrophic fights and badly depleted isolated elements.
- `DecideHopelessSurvivorAction`: at the start of a fresh turn, reuse `AI_ACTION_WITHDRAW`; if no acceptable withdrawal exists, seek existing nearby cover.
- New flanks stop and new flanks/GET_CLOSER/distant melee charges are suppressed while `AIShouldAvoidAdvance` is true.

This does not yet implement map-edge escape or a persistent disengagement state. Those remain later chunks. Soldiers may still shoot, throw, suppress or fight from their current position; the change is that hopeless survivors stop initiating another charge.

## Chunk 3: tactical fallback / giving ground

Chunk 3 makes backward movement a normal positional decision before morale collapse.

- `AIShouldConsiderTacticalFallback` combines under-fire state, cover, personal risk, local stress, perceived battle situation, isolation and weapon-range preference.
- `AIKnownThreatExposure` evaluates a position only from JA2 personal/public knowledge and last-known locations; it does not inspect hidden opponent life, current position, weapon or AP.
- `DecideTacticalFallback` searches once at the start of a fresh turn and moves only when the fallback position is meaningfully better.
- Existing `AI_ACTION_WITHDRAW` scoring now compares current vs candidate cover, sight cover, nearby support, crowding and long-range standoff while retaining its bounded `TACTICAL_RANGE / 4` search.
- RED and BLACK combat AI can now concede ground before reaching hopeless morale.
- Chunk 2 survivor-position exposure checks were switched to the same knowledge-bound exposure helper.

Performance rule: no second pathfinder was added. `FindFlankingSpot(..., AI_ACTION_WITHDRAW)` remains the single bounded path search, and the more expensive known-contact exposure check is run only on the current position and the selected final fallback candidate.

## Chunk 4: disengagement

Chunk 4 adds a short-lived break-contact state distinct from ordinary tactical fallback.

- `AIShouldStartDisengagement` triggers on catastrophic perceived odds, last-survivor pressure, or a losing fight combined with meaningful casualties / severe stress and personal risk.
- `AIUpdateDisengagementState` keeps that intent for about 2 tactical turns (3 for catastrophic/last-survivor cases), decays it once per tactical turn, and clears it early when the situation becomes clearly winning and calm.
- State lives in TacticalAI static arrays keyed by soldier ID, avoiding `SOLDIERTYPE` and savegame changes.
- `DecideDisengagementAction` repeatedly uses the existing bounded withdrawal search to break contact; if no acceptable route exists it seeks safer nearby cover, then normal attack logic may return fire while renewed advance remains blocked.
- While disengagement is active, ordinary survivor/fallback/self-preservation movement searches are skipped to avoid duplicate pathfinding.
- `AIShouldAvoidAdvance` treats active disengagement as a hard no-advance condition, preventing SEEK/GET_CLOSER/flank reversal until the intent expires or the battlefield clearly improves.

This chunk does not yet choose a map edge or leave the sector. That remains Chunk 5.

Chunk 4 review hardening: disengagement state is additionally bound to `uiUniqueSoldierIdValue` so reused soldier slots cannot inherit stale state, and named/profile NPCs are excluded from entering the new persistent disengagement mode to avoid interfering with story scripts.

Chunk 4 final audit: GREEN/YELLOW decisions now clear transient disengagement state through the common legacy dispatcher, and disengagement entry/update shares precomputed battle-state inputs to avoid a duplicated full assessment.

## Consolidation audit: integrated AI interaction fixes

The integrated branch was re-audited against the original Vengeance/SevenFM decision flow after the fireteam, doctrine, morale, casualty and reinforcement layers were combined.

- Response episodes are contact-local and expire after two quiet tactical turns.
- Response budgets count deployable troops; `STATIONARY` and `SNIPER` soldiers do not consume mobile response slots.
- `SEEK_FRIEND` uses the same response budget as `SEEK_NOISE`.
- YELLOW rear-area radio operators may provide legitimate remote artillery support without being forced into RED movement behavior.
- Exact downed/incapacitated target state requires personal current sight.
- Fireteam, response, escape/disengagement, tactical variation, escape-plan, militia-consolidation and drag caches reject stale state after a tactical turn rollback.
- Fireteam lookups use a cached fast path but invalidate when the in-sector enemy force changes.
- Casualty dragging is bound to unique soldier identities and is cancelled in high water.
- With the prisoner system enabled, surviving hostile downed casualties are stabilized and routed into the POW pipeline after victory.

These changes preserve the original Vengeance tactical core while preventing the added coordination layers from recreating sector-wide hive-mind behavior.
