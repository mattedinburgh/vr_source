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

## Shared belief layer

New tactical reasoning should consume the explicit `AICONTACTBELIEF` representation in
`TacticalReasoning.cpp` when it needs contact confidence, age, or source rather than
reinterpreting raw opponent lists independently.

A contact belief contains only:
- opponent ID;
- legal last-known grid/level;
- selected JA2 knowledge state;
- whether personal or public knowledge supplied it;
- confidence derived from the existing `ThreatPercent` table;
- coarse age in tactical turns;
- whether the opponent is personally and freshly visible.

The belief layer never estimates hidden current health, AP, stance, weapon, movement, or exact
current position. It is a normalized view over the existing JA2 information model, not a second
perception system.

## Decaying contact memory and evidence fusion

The shared reasoning layer retains a short-lived, identity-bound memory of **visually established**
last-known contact locations after normal JA2 opponent knowledge expires.

Rules:
- only visual knowledge refreshes an exact contact-memory location;
- memory confidence decays every tactical turn and expires automatically;
- personally established memory persists more strongly than public/team-reported visual memory;
- reaching and visibly checking the remembered location sharply reduces or clears the hypothesis;
- remembered locations may influence search, facing, route choice, flank evaluation and local geometry;
- memory alone may **never** authorize aimed fire, grenades, exact unseen targeting, or an encirclement conclusion.

Fresh legitimate miscellaneous noise is compared with remembered sectors. A sound from the same
or adjacent direction, especially near the last-known area, receives a bounded investigation-priority
boost. Matching evidence also creates a **short-lived anonymous directional threat cue**: up to three
independent cues per observer, retained for at most four tactical turns. Cue strength is weighted by
the volume actually heard, reduced for public/team-reported noise, and smeared into adjacent sectors
when tactical geometry is rebuilt. This lets loud gunfire reinforce a remembered danger sector more
than weak incidental noise without pretending the listener knows the shooter.

This is **evidence fusion, not identification**. A corroborated cue:
- may affect search priority, facing, route choice, flank side, fallback direction and CQB geometry;
- may keep an unresolved remembered sector salient for a short time;
- never assigns the sound to a specific unseen opponent;
- never moves an old contact memory to the noise tile;
- never authorizes aimed fire, grenades or exact unseen targeting;
- never creates encirclement by itself, because encirclement still requires personal visible geometry.

If no normal heard/public/noise cue remains, combatants may briefly investigate a sufficiently
confident last-known visual area. A recently corroborated memory receives a small salience bonus,
but fresh legal evidence always outranks this fallback memory.

## Memory-driven search doctrine

Expired contact knowledge may sustain a **search hypothesis**, never a firing solution.

When a last-known contact remains unresolved:
- the remembered grid defines an uncertainty area rather than an exact destination;
- `FindThreatSearchObservationSpot` selects reachable covered positions that can observe/clear that area;
- route exposure, cover, friendly support, crowding, doorway funnels, lighting and battlefield geometry all influence the observation point;
- one investigator is normal; a second is permitted only for a sufficiently capable, supported fireteam and stronger/corroborated evidence;
- other fireteam members reserve `AI_TASK_SEARCH_SUPPORT`, face the likely sector and preserve overwatch instead of following the investigators;
- a compatible fresh sound can move the shared search focus and reduce uncertainty, but it remains anonymous directional evidence;
- seeing the remembered area empty decays/clears the hypothesis;
- CQB may use unresolved memory only for SECURE/HOLD/search positioning. It cannot promote stale memory into ASSAULT, COUNTERATTACK or an attack against an unseen opponent.

Fresh ordinary JA2 sight/hearing knowledge always supersedes this weaker memory layer.

## Surprise / encirclement fairness

A contact-surprise event is permitted only when a combatant gains **personal current sight** of
opponents after a prior decision snapshot. The current implementation additionally validates
fresh LOS before counting a contact as newly revealed.

The surprise tracker may remember:
- the soldier's own previous decision grid;
- how many opponents were personally visible;
- coarse directions of those personally visible contacts;
- legal known-threat exposure at the soldier's own position.

It may not use hidden enemies to decide that a soldier is surrounded. The shared 8-sector geometry
may use stale/heard/public contacts as lower-confidence pressure for ordinary caution, route choice,
and flank evaluation, but **encirclement pressure requires personally visible multi-angle geometry**.
Public/radio knowledge can therefore influence where a soldier prefers to move without manufacturing
a false "I can see that I am surrounded" conclusion.

## Hard anti-cheat rules

New AI must not:
- use an opponent's real current grid when that opponent is not currently known there
- count hidden AIM mercs from raw team/sector totals as perceived enemy strength
- treat a stale last-known location as exact current position
- infer exact hidden enemy health, stance, equipment or movement from raw soldier state unless existing perception already exposes it
- create a second omniscient perception system
- treat cached `SEEN_CURRENTLY` as sufficient for aimed fire when a fresh LOS test is blocked by smoke or another newly changed visibility condition

Generic callback/predicate rule:
- arbitrary `SOLDIER_CONDITION`/callback predicates are treated as potentially reading hidden mutable state;
- do not evaluate them on unseen opponents unless the caller has a dedicated knowledge-safe contract;
- generic AoE selection therefore classifies predicate-based enemy/taboo targets only under fresh personal sight, while dedicated grenade/launcher code uses JA2 known locations for remembered contacts.
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

## Shared local battlefield geometry

`AIBuildTacticalGeometry` converts legal contact beliefs and local friendly positions into an
eight-direction tactical picture around the deciding soldier. It records pressure by direction,
the primary/secondary known threat axes, strongest local friendly sector, left/right flank
opportunity, rear safety, and the currently safest breakout direction.

Opponent-sector pressure is confidence-weighted by the existing JA2 knowledge model. Friendly
pressure for combat teams is fireteam-local, preventing sector-wide perfect coordination.

The geometry layer may guide:
- flank-side choice;
- flank/fallback tile scoring;
- CQB position utility;
- surprise reassessment;
- weakest-sector breakout under multi-angle pressure.

A remembered contact may make a direction less attractive. It may not, by itself, create an
encirclement state. That stronger conclusion requires personally visible, separated threat sectors.

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

Performance/quality rule: tactical quality now takes precedence over minimizing path queries on the target hardware. The shared spatial evaluator may run additional full-route analyses for serious candidate positions. Those analyses use `FindBestPath(..., NO_COPYROUTE, ...)`, so they can inspect the generated route without overwriting the soldier's prepared execution path. Route scoring still uses only legal known-threat exposure, smoke, cover, hazards and inferred reaction risk.

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
