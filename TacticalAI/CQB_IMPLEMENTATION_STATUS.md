# CQB dormant implementation status

## Safety status

The CQB/building subsystem is **compiled but inactive** on
`inactive/cqb-building-doctrine-2026-09-14`.

It is deliberately absent from all `DecideAction*` call paths and
`VRCQB_IsRuntimeEnabled()` returns `FALSE`.

No gameplay behaviour can change merely because the module is compiled.

## Implemented

### Competence model
- SECURITY / administrator
- LINE uncommanded
- LINE commanded
- VETERAN
- ELITE_MOBILE
- ELITE_GUARD

Separate assault and holding proficiency plus explicit capability flags.

### Knowledge-safe building context
- current room / building
- primary known threat room / building
- known indoor threats
- same-room and same-building contact
- doorway proximity
- threshold exposure
- local friendly support
- local fallback availability

Opponent geometry comes only from `Knowledge()`, `KnownLocation()` and
`KnownLevel()`.

### Bounded entry search
- searches around the known indoor threat, not the whole map;
- identifies actual door tiles;
- evaluates adjacent interior foothold tiles;
- advanced troops may compare alternate entries;
- basic troops are biased toward the nearest obvious entry.

### Bounded local position planner
Used for:
- HOLD
- SECURE
- DELAY_FALLBACK
- local COUNTERATTACK

The planner considers:
- normal cover;
- knowledge-safe threat exposure;
- friendly crowding;
- nearby support;
- doorway / fatal-funnel exposure;
- distance to known threat;
- defense-in-depth capability;
- crossfire only when the troop profile supports it;
- existing competence noise so weak troops do not become deterministic robots.

No new global pathfinder or whole-sector room graph was added.

### State assessment
Implemented dormant state selection for:
- ASSAULT
- HOLD
- DELAY_FALLBACK
- COUNTERATTACK
- SECURE

Existing disengagement and personal-risk systems override aggressive CQB planning.

SECURITY explicitly refuses complex independent assault.

ELITE_GUARD prefers to hold the penetrated strongpoint rather than roam after contact.

### Short-lived plan memory
Transient per-soldier plan slots:
- key against `uiUniqueSoldierIdValue`;
- invalidate on soldier-slot reuse;
- expire after two tactical turns;
- wipe on tactical turn rollback;
- require no SOLDIERTYPE/savegame changes.

This memory allows the future action hook to distinguish, for example, a fresh
counterattack opportunity from generic enemy presence in a room.

### Telemetry-ready output
Assessment exposes:
- CQB state;
- CQB role;
- reason code;
- training profile;
- capabilities;
- chosen entry and target;
- fallback position;
- risk;
- known-threat exposure;
- effective skill;
- planner reliability;
- confidence.

The name adapters are ready for Black Box logging once runtime activation is approved.

## Explicitly not implemented yet

These are intentionally activation-layer concerns:

- no `DecideAction` hook;
- no returned `AI_ACTION_*`;
- no automatic suppression/smoke task is issued;
- no destructive breaching;
- no window-entry logic;
- no live battlefield shouts;
- no savegame state;
- no INI/game option.

## Performance design

Hot-path cost is bounded:
- local position scan radius: 6 tiles;
- entry search radius: 8 tiles around the known indoor threat;
- no full-map room graph;
- no second global pathfinder;
- expensive threat geometry uses the existing knowledge-safe exposure helper.

When runtime integration begins, the action layer should path-check only the final
selected destination rather than every scored candidate.

## Activation sequence

1. Build validation with the dormant module compiled.
2. Black Box adapter.
3. Diagnostic-only shadow evaluation: existing AI still acts, CQB planner only logs what
   it would have done.
4. Compare shadow decisions to actual battles.
5. Activate HOLD/SECURE first.
6. Activate fallback.
7. Activate ASSAULT last.
8. Enable local counterattack only after telemetry shows stable behavior.

This staged activation is preferred over switching the entire CQB subsystem on at once.
