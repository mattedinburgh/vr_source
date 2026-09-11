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
- `AILastSurvivorPressure`: one/two local soldiers plus at least 50% known friendly losses.
- `AIHopelessOddsModifier`: soft RED-state seek penalty for losing/catastrophic situations.
- `AIShouldAvoidAdvance`: hard gate for catastrophic fights and badly depleted isolated elements.
- `DecideHopelessSurvivorAction`: at the start of a fresh turn, reuse `AI_ACTION_WITHDRAW`; if no acceptable withdrawal exists, seek existing nearby cover.
- New flanks stop and new flanks/GET_CLOSER/distant melee charges are suppressed while `AIShouldAvoidAdvance` is true.

This does not yet implement map-edge escape or a persistent disengagement state. Those remain later chunks. Soldiers may still shoot, throw, suppress or fight from their current position; the change is that hopeless survivors stop initiating another charge.