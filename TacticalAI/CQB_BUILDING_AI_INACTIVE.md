# Inactive CQB / Building Doctrine Research and Implementation Plan

> STATUS: INACTIVE DESIGN BRANCH
>
> Runtime wiring is intentionally absent. This branch does not modify DecideAction.cpp,
> project files, game options, or the canonical install/all-2026-09-12 branch.
> The purpose is to prepare a coherent implementation that can be reviewed and tested
> before any gameplay activation.

## 1. Objective

Add a building-aware tactical layer for Vengeance: Reloaded that makes soldiers behave
more intelligently in close quarters without turning Deidranna's army into a perfect
special-operations unit.

The system should improve two distinct problems:

1. assaulting and clearing occupied structures;
2. holding, delaying in, abandoning, or counterattacking from structures.

CQB assault and building defense are intentionally separate modes. They share geometry,
knowledge and fireteam information but use different objectives and scoring.

## 2. Research synthesis

### Real-world urban doctrine translated for JA2

Useful concepts from US Army urban-combat doctrine:

- avoid remaining exposed in doors and windows;
- move through entry points to positions that dominate assigned sectors;
- use small clearing elements rather than sending an entire formation through one doorway;
- assign overlapping sectors so movement does not mask friendly fire;
- secure already-cleared space so the enemy cannot simply recycle behind the assault;
- use support, suppression, security and obscuration to reduce the danger of ground-floor entry;
- defend strongpoints as several independent but mutually supporting positions;
- retain covered/concealed internal routes so defenders can shift, counterattack or fall back.

These principles map well to JA2 if treated as scoring and coordination rules rather than
scripted room-clearing choreography.

### Lessons from other tactical games

#### Door Kickers / Door Kickers 2

Useful ideas:
- explicit sectors and facing matter as much as destination;
- simultaneous or coordinated movement beats single-file doorway congestion;
- suppression is most useful as a movement-enabling tool, especially across a dangerous opening;
- alternate entry points and breaching geometry reduce predictable fatal funnels;
- hold points and synchronized actions produce readable squad behaviour.

What not to copy:
- perfect player-authored synchronization;
- omniscient room knowledge;
- special-forces speed/precision for ordinary Deidranna line troops.

#### Long War of the Chosen / XCOM AI

Useful ideas:
- behaviour is layered by role and situation rather than one universal action list;
- flanked units first solve the immediate positional problem;
- fallback, overwatch, cower and attack are competing outcomes in an ordered selector;
- movement profiles score cover, distance, flanking, visibility, allies and height;
- role-specific selectors create distinct behaviour without changing the underlying combat rules.

What to copy conceptually:
- context-sensitive score profiles;
- first-action vs later-action priorities;
- explicit fallback profiles.

What not to copy:
- pod logic;
- XCOM-specific activation/scamper rules;
- artificial role behaviour that ignores JA2 map orders.

#### Xenonauts 2

Useful ideas from developer updates:
- pure dynamic scoring can collapse into repetitive "best tile" behaviour;
- authored waypoints/anchors can keep defenders in believable positions until combat changes the problem;
- once contact occurs, dynamic combat reasoning can override those passive anchors;
- AI test scenes with a clearly optimal tactical answer are valuable for regression testing;
- recent AI work considers attack and return-to-cover as one combined plan rather than allocating movement and attack separately;
- replanning when new hostile information appears prevents stale plans from leaving units exposed.

Implication for Vengeance:
- preserve mapper orders / posts as defensive anchors;
- use dynamic local building logic only after relevant contact;
- evaluate a short action sequence when feasible (shoot -> shift, suppress -> move, breach -> enter),
  but keep pathfinding bounded.

#### Ready or Not / CQB simulations

Useful gameplay principles:
- thresholds are danger zones, not fighting positions;
- a cleared room becomes a foothold and rear-security problem;
- one element can hold a dangerous opening while another searches or moves;
- over-clearing with the whole squad creates congestion and uncovered sectors.

For Vengeance these are behavioural principles, not an attempt to recreate SWAT procedure.

## 3. Vengeance-specific design constraints

The new system must preserve:

- the existing JA2 personal/public opponent-knowledge model;
- AI_Knowledge_Audit.md anti-cheat rules;
- Deidranna doctrine profiles;
- current local fireteam logic;
- current morale, rout, fallback, casualty and disengagement systems;
- staged local QRF / reinforcement budgets;
- mapper-assigned STATIONARY / ONGUARD / patrol / SNIPER intent;
- current smoke scarcity and equipment reality;
- bounded pathfinding and acceptable turn time.

The system must not:

- read unseen enemies' live positions, health, AP, equipment or stance;
- turn every soldier into a coordinated commando;
- cause all defenders in a sector to collapse into one building;
- make every room entry a grenade/flash/smoke event;
- make defenders camp one doorway forever;
- create perfect 360-degree awareness;
- add hidden CTH, AP, reaction or perception bonuses;
- replace the existing fireteam / doctrine architecture.

## 4. Tactical state model

A soldier/fireteam can be in one building state:

### NONE
Normal existing tactical AI.

### ASSAULT
The element is trying to enter, clear, or seize a room/building occupied or probably
occupied by known opposition.

Priorities:
1. do not idle in a doorway/fatal funnel;
2. avoid stacking multiple movers into the same entry tile;
3. establish one or more safe interior foothold positions;
4. split local sectors / facing;
5. use a support soldier to cover the entry or known firing lane when available;
6. clear the nearest high-risk corner/sector before deep movement;
7. maintain rear/side security when the building has multiple exits;
8. stop the assault and fall back if local losses/suppression make continuation irrational.

### HOLD
The element is defending a room/building or mission anchor.

Priorities:
1. hold useful firing positions rather than the geometrical centre of the room;
2. avoid excessive clustering at one window/door;
3. create mutually supporting positions;
4. cover likely approaches and internal choke points;
5. keep at least one viable internal shift/fallback route;
6. reserve a small local counterattack element only when doctrine/manpower allows;
7. use roof positions selectively, especially for SNIPER / ELITE_GUARD.

### DELAY_FALLBACK
The element is conceding part of a building while preserving combat power.

Priorities:
1. move from exposed outer positions toward a deeper covered position;
2. do not reverse back through the same fatal funnel unless necessary;
3. keep one supporting/covering soldier when local strength allows;
4. avoid retreat paths through occupied friendly tiles and obvious kill zones;
5. integrate with existing tactical fallback/disengagement rather than replacing it.

### COUNTERATTACK
A limited local attempt to retake a lost room/entry/foothold.

Entry conditions should be strict:
- recent local loss of a useful position;
- defenders still have reasonable local strength;
- known enemy disposition suggests a limited opportunity;
- command/doctrine permits initiative;
- no catastrophic morale/disengagement state.

This is not a sector-wide rush. It should normally involve one local fireteam or fewer.

### SECURE
The element has cleared or retained a room and is stabilizing the position.

Priorities:
- orient toward uncleared approaches;
- avoid all soldiers pointing at the same threat sector;
- hold briefly before immediately chaining another deep advance;
- hand off rear security when another element is available;
- resume normal AI if no building-specific need remains.

## 5. Doctrine quality / imperfection

### SECURITY
- strong HOLD bias;
- almost no independent CQB assault;
- tends to cover obvious entrances;
- may over-focus on the primary doorway;
- counterattack only with command/order support;
- slower to reassign sectors.

### LINE
Uncommanded:
- basic mutual support;
- simple two-man entry/support behaviour;
- limited alternate-entry reasoning;
- may hesitate or choose a merely adequate position.

Commanded:
- can coordinate a support-and-move pair;
- can use smoke/suppression proactively when equipment and situation justify it;
- can attempt a limited local counterattack.

### VETERAN
- better doorway discipline;
- better sector distribution;
- more willing to use alternate entry/shift routes;
- better at combined shoot-move or suppress-move plans;
- faster replanning on new contact.

### ELITE_MOBILE
- strongest assault / local counterattack behaviour;
- least anchored to original post;
- still knowledge-fair and resource-limited.

### ELITE_GUARD
- strongest HOLD / depth / crossfire logic;
- limited roaming;
- counterattacks only to protect the defended objective.

Imperfection must remain explicit. Good AI should come from better choices, not perfect
reaction time or impossible information.

## 6. Building geometry context

The first implementation should use existing map data only.

Potential signals:
- InARoom(grid, &roomNo);
- gusWorldRoomInfo[] room IDs;
- FindBuilding / SameBuilding where valid;
- doors and door status from existing structure helpers;
- windows / wall structures;
- roof level and climb points;
- reachable/path-cost checks already used by TacticalAI;
- existing cover, sight-cover and known-threat exposure helpers;
- mapper orders and original patrol/anchor grid;
- friendly occupancy / crowding;
- known enemy locations and levels only.

Do not create a full navmesh or expensive room graph in the hot decision loop.

Preferred approach:
1. build/cache a lightweight room adjacency summary when the tactical map is initialized or on first use;
2. invalidate only when relevant doors/structures change;
3. run bounded local candidate scoring per acting soldier.

## 7. Tactical roles inside a local fireteam

Roles are temporary intents, not permanent classes:

- POINT: next mover / first interior position;
- COVER: watches the entry or known danger lane;
- SUPPORT: suppression or longer-range overwatch;
- SECURITY: protects rear/side approach;
- HOLD: remains on defensive anchor;
- RESERVE: available for local counterattack / casualty replacement.

Assignment should use existing fireteam information plus capability:
weapon range, suppression capability, AP, wounds, shock, morale, doctrine and current position.

Do not assign roles from hidden enemy data.

## 8. Candidate-position scoring

A CQB position score should be composed from understandable terms:

Positive:
- real cover against known threats;
- sight cover where appropriate;
- line of fire to likely approach;
- support from nearby fireteam members;
- ability to cover an uncovered sector;
- distance from doorway/threshold after entry;
- route continuity toward the next objective;
- protected route to fallback position;
- roof/elevation when appropriate to role.

Negative:
- doorway/threshold exposure;
- window silhouette without compensating cover;
- friendly crowding;
- crossing another friendly's fire lane;
- isolation;
- exposure to multiple known threat directions;
- dead-end position with no fallback route;
- excessive distance from fireteam;
- repeated use of the same obvious firing tile;
- unsupported deep penetration.

Weights should vary by tactical state and doctrine profile.

## 9. Assault sequence concept

The system should prefer short coordinated sequences, not choreography.

Example:
1. identify a target entry based on known threat and accessible geometry;
2. choose POINT and COVER;
3. COVER establishes a viable lane or suppression opportunity;
4. POINT moves through/around the threshold to a domination/foothold tile;
5. second mover occupies a non-conflicting supporting tile;
6. element transitions to SECURE or continues ASSAULT;
7. abort to DELAY_FALLBACK if suppression/casualty/morale conditions deteriorate.

Important:
- soldiers may still attack immediately when a good shot is available;
- this layer should not suppress normal opportunistic combat;
- entry plans expire quickly when new information invalidates them.

## 10. Building-holding strategy

A defended building should behave as a network of local positions.

Preferred pattern:
- outer observation/fire positions;
- internal choke / hallway / stair positions;
- one deeper fallback position;
- optional roof position;
- optional local reserve.

Defenders should:
- shift laterally if one window becomes compromised;
- abandon a position that is repeatedly suppressed/exposed;
- avoid filling every window;
- avoid all leaving the objective to chase one contact;
- allow one local element to counterattack a penetrated room if conditions are favourable;
- otherwise trade space for survivability.

This directly complements the current map-order anchoring and QRF caps.

## 11. Resource doctrine

Smoke / grenades:
- emergency smoke remains available under the existing self-preservation rules;
- proactive entry smoke should require command/veteran/elite initiative;
- frag/HE is not a default room-clearing button;
- grenade choice must respect civilians/friendlies/known information and inventory scarcity.

Suppression:
- useful for crossing/opening control and fixing a known threat;
- should not become perpetual blind fire;
- only soldiers with appropriate weapons/ammo/AP should be selected as SUPPORT.

Breaching:
- initial version uses existing doors/windows/wall openings only;
- destructive breaching can be a later extension after door-charge correctness is stable.

## 12. Interaction with existing systems

### Fireteams
CQB roles are assigned within current local fireteams. No second squad system.

### Doctrine
AIGetDoctrineProfile() remains authoritative for initiative and complexity.

### Fallback / disengagement
CQB DELAY_FALLBACK proposes building-aware positions but must defer to the existing
disengagement state when that state is active.

### QRF
Remote reserve logic remains intact. CQB must not pull distant anchored defenders into
a building fight merely because a building state exists elsewhere.

### Medical
No new casualty omniscience. CQB roles yield to the current rescue safety rules.

### Knowledge
All opponent-based scoring uses current personal/public knowledge and known locations.

## 13. Black Box / Companion telemetry

When eventually enabled, log compact events rather than every candidate tile.

Suggested events:
- cqb_state_enter / cqb_state_exit;
- cqb_role_assign;
- cqb_entry_selected;
- cqb_entry_aborted;
- cqb_hold_reposition;
- cqb_fallback_room;
- cqb_counterattack_start / abort;
- cqb_threshold_avoid;
- cqb_sector_assignment;
- cqb_support_task;
- cqb_plan_invalidated.

Each event should include:
- soldier ID / unique soldier ID;
- fireteam ID if available;
- doctrine profile;
- room/building IDs;
- current/target grid;
- known-threat count and certainty bucket;
- local friendly count;
- morale/stress/suppression bucket;
- selected score and top rejection reason;
- decision ID from the canonical VRAnalytics stream.

## 14. Testing strategy

Create small deterministic AI test scenes, inspired by Xenonauts 2's AI test scenes.

Minimum regression scenarios:

1. Doorway fatal funnel
   - two defenders cover a door;
   - attacker should not stop on threshold.

2. Two-man entry
   - first mover enters;
   - second should avoid occupying same lane / blocking first.

3. Defensive window crowding
   - four defenders in one room;
   - they should not all choose the same window.

4. Depth defense
   - outer room becomes heavily suppressed;
   - some defenders give ground to deeper positions rather than charge.

5. Local counterattack
   - player enters one room with weak local force;
   - veteran/elite defenders may counterattack if odds are reasonable.

6. Counterattack refusal
   - same geometry but defenders are losing/catastrophic;
   - no suicidal retake.

7. Rear security
   - building with two entrances;
   - not every defender rotates toward the first contact.

8. Roof sniper
   - SNIPER / ELITE_GUARD should value roof firing position but abandon it when exposed.

9. Knowledge fairness
   - hidden player in adjacent room;
   - AI may infer from heard/last-known data but must not target exact live grid.

10. Performance
   - 20+ enemies across several buildings;
   - path searches and candidate evaluations remain bounded.

## 15. Implementation phases

### Phase A - geometry and context only
- room/building context;
- doorway/threshold detection;
- local known-threat directions;
- local friendly occupancy;
- no action changes.

### Phase B - HOLD scoring
- defensive anchor distribution;
- anti-crowding;
- internal fallback candidate;
- no assault logic.

### Phase C - ASSAULT entry discipline
- threshold avoidance;
- POINT/COVER role pair;
- short-lived entry plan;
- no destructive breaching.

### Phase D - SECURE / rear security
- foothold state;
- sector distribution;
- room hand-off.

### Phase E - DELAY_FALLBACK integration
- building-aware fallback candidate;
- integrate with existing fallback/disengagement.

### Phase F - limited COUNTERATTACK
- veteran/elite/command gates;
- local only;
- strict morale/odds checks.

### Phase G - utility support
- selective suppression / smoke support;
- later, breaching tools if stable.

## 16. Activation gate

Do not merge/enable until all are true:

- compile succeeds on the canonical VS2013 path;
- no savegame structure changes are required or they are version-safe;
- knowledge audit passes;
- no new sector-wide coordination is introduced;
- Black Box telemetry is sufficient to explain CQB decisions;
- regression scenes demonstrate improvement without doorway camping;
- turn-time impact is acceptable;
- line/security troops remain visibly imperfect;
- user explicitly approves activation.

## 17. Current branch status

Prepared only:
- research/design contract;
- API scaffold;
- no runtime call sites;
- no project-file inclusion;
- no feature option;
- no canonical-branch changes.

The next coding step, if approved later, should be Phase A only.


## 18. Professional training-quality model

The coarse doctrine distinctions in section 5 are now formalized in
`CQB_PROFESSIONAL_TRAINING_MODEL.md`.

Key change: CQB quality is **not one scalar**.

The dormant API now separates:

- assault/clearing skill;
- holding/defensive skill;
- sector discipline;
- replanning skill;
- hesitation;
- threshold mistakes;
- coordination breakdown;
- maximum coordinated movers;
- explicit capabilities such as alternate-entry reasoning, rear security,
  defense-in-depth and local counterattack.

This intentionally makes SECURITY/admin troops different from experienced troops.

SECURITY is allowed to be reasonably competent at an assigned defensive post while
remaining a poor dynamic clearing force. LINE infantry gets recognizable basic room
clearing. Local command unlocks a simple rehearsed plan. VETERAN and ELITE profiles
gain increasingly sophisticated sector management, replanning and building flow.

The implementation reuses `AIGetDoctrineProfile()`, `AICompetenceTier()`,
`AIPlannerReliability()` and `AIHasLocalCommandSupport()`; it does not create a
parallel experience/rank system.

Runtime status remains unchanged: **inactive**.
