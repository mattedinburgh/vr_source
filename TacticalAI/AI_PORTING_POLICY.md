# Tactical AI upstream-porting policy

This file is the repository-level freeze rule for the Vengeance tactical AI.

## Default rule

Do **not** import upstream JA2 1.13 tactical doctrine or behaviour wholesale.

The current Vengeance AI has its own integrated architecture: local fireteams, Deidranna
army doctrine, human-like self-preservation, casualty response, morale/rout, staged
reinforcement, knowledge-fair targeting and Vengeance-specific mission orders. An
upstream change is not automatically compatible merely because it is newer.

## Safe-by-default upstream work

Upstream changes may be considered when they are primarily correctness or engineering
fixes and do not materially redefine tactical doctrine:

- AP-cost and AP-affordability correctness;
- pathfinding correctness and traversal safety;
- impossible-destination / deadlock fixes;
- grenade, launcher and mortar correctness;
- friendly-fire correctness;
- LOS/visibility correctness that preserves the knowledge contract;
- crash, bounds, memory and state-lifetime fixes;
- deterministic save/load state invalidation;
- performance improvements that preserve decisions;
- diagnostics, logging and developer tooling.

Even these changes must be reviewed for interaction with the integrated Vengeance AI.

## Frozen without explicit design approval

Do not silently port changes that alter:

- aggression or willingness to advance;
- retreat, rout, surrender or escape doctrine;
- noise response, QRF sizing or reinforcement release;
- fireteam composition or local-cohesion rules;
- command/officer effects;
- suppression, smoke or fire-and-manoeuvre doctrine;
- medic/casualty priorities;
- target-selection priorities;
- perceived force ratios;
- information sharing or opponent knowledge;
- militia tactical behaviour;
- Deidranna SECURITY / LINE / VETERAN / ELITE doctrine;
- any behaviour that creates sector-wide coordination or hidden-information access.

Such changes require an explicit Vengeance design decision and a focused integration
audit before they enter the consolidated branch.

## Knowledge boundary is non-negotiable

All new tactical logic must follow `AI_Knowledge_Audit.md`.

In particular, code must not use an unseen opponent's live grid, health, stance,
equipment, AP, movement, sector presence or arbitrary callback/predicate result unless
that state is legitimately available through current personal perception.

Generic callbacks are treated as potentially reading hidden mutable state. They must
not be evaluated on unseen opponents unless a dedicated knowledge-safe contract exists.

## Deidranna doctrine boundary

`AIGetDoctrineProfile()` and all doctrine restrictions are an **ENEMY_TEAM-only**
identity layer. Militia shares the human-like tactical core and fireteam machinery but
does not inherit Deidranna command/initiative restrictions.

## Review checklist for future ports

Before importing an upstream tactical-AI commit, answer all of the following:

1. Is this a correctness/engineering fix rather than a doctrine change?
2. Does it preserve the opponent-knowledge contract?
3. Does it preserve Vengeance fireteam and QRF behaviour?
4. Does it preserve Deidranna-vs-militia separation?
5. Does it respect map orders and scripted strongpoints?
6. Does it avoid adding duplicate path searches or sector-wide scans to hot AI paths?
7. Has the change been reconciled with the consolidated branch rather than blindly merged?

If any answer is no or uncertain, the change is frozen until explicitly designed and
audited.
