# Vengeance Human Tactical Planner

## Goal

Make enemy and militia soldiers behave like competent human players without hidden AP, CTH, aim-cone or information advantages.

The planner is intentionally hybrid. The existing Vengeance / JA2 1.13 tactical AI remains the execution layer. A new shared planning layer decides which legacy behaviours are appropriate, ranks positions with a common utility model, and keeps a short-lived plan so soldiers do not oscillate between contradictory decisions.

## Design influences

- Classic X-COM: patrol / guard / combat / escape states, imperfect knowledge, memory and reaction-fire driven uncertainty.
- Xenonauts: directional cover, smoke, suppression and reaction fire make exposed routes dangerous even when the destination is good.
- XCOM: Enemy Unknown: utility-scored actions and movement maps, with different unit behaviour profiles.
- Long War / LWOTC: behaviour-tree gating plus configurable movement weights, spread penalties, fallback profiles and role-specific behaviour.
- F.E.A.R.: short-horizon replanning and squad cooperation such as suppress-and-move.
- Halo 2: squad orders, firing-position evaluation and high-level battle-state transitions.
- Battle Brothers: relative utility and simulationist differences between competent and deliberately weak enemy archetypes.

## Decision hierarchy

1. Hard legality / survival constraints
   - AP, path legality, gas, deep water, bombs, red smoke, weapon feasibility.
   - No action may use information outside the normal JA2 personal/public knowledge model.

2. Persistent tactical intent
   - HOLD
   - PRESS
   - FLANK
   - FALLBACK
   - DISENGAGE
   - RESCUE

   Intent persists briefly to prevent thrashing. Direct danger, a major target-location change, escape/disengagement state or casualty emergency can override immediately.

3. Distributed squad blackboard
   - Nearby soldiers fighting the same contact publish their intent.
   - Two ordinary agreeing soldiers form a squad preference.
   - A commander/officer carries enough weight to seed the plan.
   - Personal danger can veto an aggressive squad consensus.

4. Dynamic fireteam role
   - SUPPORT
   - MANEUVER
   - FLANKER
   - SCREEN
   - RESERVE

   Roles are capability-based, not permanent classes. Weapon range, optics, autofire, health, breath, stress, position and special roles affect suitability.
   Role reservations prevent the whole squad from independently selecting the same maneuver.

5. Action utility
   - Existing RED seek/help/hide/watch weights are biased by current intent and role.
   - Emergency smoke, suppression response, disengagement and fallback still have hard safety precedence where appropriate.

6. Position utility
   Candidate tiles are compared using:
   - known-threat exposure
   - ordinary, sight and prone cover
   - nearby friendly support
   - grenade-risk spacing / overcrowding
   - crossfire angle
   - weapon-appropriate range
   - intent-specific progress or separation
   - smoke
   - personal stress/risk
   - role

7. Route utility
   - Endpoint scoring alone is insufficient.
   - The best twelve reachable endpoints are shortlisted.
   - Their actual paths are then sampled for known-threat exposure.
   - Consecutive exposed tiles compound route cost.
   - Smoke reduces route exposure cost.

## Smoke decision tree

### Movement smoke

Use smoke when:
- the soldier has a PRESS or FLANK maneuver plan;
- a route segment is a real known kill zone, or the soldier is already under fire crossing a completely exposed gap;
- the throw is feasible.

Do not spend smoke merely because the route is outdoors. Select the highest-utility dangerous segment rather than a random path tile.

### Protection smoke

A soldier can smoke a teammate who is:
- critically wounded;
- severely wounded and bleeding;
- meaningfully suppressed under fire;
- personally over the risk threshold.

The thrower must believe that the teammate is exposed to known enemy fire.

## Suppression response

A suppressed soldier explicitly compares:

1. Hold the current fire-base / screen position.
2. Sprint to nearby better cover.
3. Give ground to a defensible fallback position.
4. If earlier smoke logic succeeds, use the smoke window first.

A support/screen soldier in good cover with nearby friends does not abandon the firing line merely because bullets are incoming.

## Flanking

A flank is not simply "move sideways".

A candidate flank receives utility for:
- materially different crossfire angle;
- lower known exposure;
- useful cover;
- friendly support;
- weapon-appropriate range;
- reasonable progress toward the contact.

The squad reserves a limited number of flankers/movers. Support-capable soldiers stay in the fire base when better maneuver candidates already exist.

## Withdrawal

Withdrawal means trading ground for a stronger tactical position, not running directly away.

Candidates must increase separation and are scored for:
- exposure reduction;
- cover / sight cover;
- friendly support and regrouping;
- restored standoff for long-range weapons;
- route safety.

Support-capable soldiers may act as a screen while movers displace.

## Fairness constraints

The planner must not:
- read unseen enemy coordinates;
- inspect hidden enemy AP or equipment for decisions that normal knowledge cannot justify;
- react to the player's aim cone;
- add CTH or AP bonuses to simulate intelligence;
- know that an unseen route is safe because the engine can inspect all opponents.

Training and soldier class should primarily affect consistency, role selection and willingness to execute difficult plans.

## Logging

AI logs now expose:
- tactical intent
- fireteam role
- target grid
- local stress
- personal risk / tolerance
- known-threat exposure

This is the start of a tactical black box: bad decisions should be diagnosable from the reason chain.

## Next layers

Priority extensions:

1. Cooperative search after lost contact
   - support soldier covers;
   - one or two investigators clear likely positions;
   - search sectors deconflict;
   - stale knowledge decays.

2. Breach / room-clearing plan
   - stack without grenade-bait clumping;
   - smoke or flash / grenade if appropriate;
   - one soldier crosses while another covers;
   - avoid everyone entering one doorway in sequence.

3. Reaction-fire / interrupt-aware crossing
   - known recent shooters and reserved AP create a crossing hazard;
   - suppression/smoke can deliberately reduce that hazard before movement.

4. Destructible-cover reasoning
   - suppress or destroy a strong firing position when flanking is poor;
   - avoid cover likely to be destroyed immediately.

5. Local fire superiority
   - focus enough rifles on one contact to suppress or kill;
   - avoid pathological overkill;
   - reserve one or two guns for alternate arcs.

6. Coordinated break contact
   - smoke;
   - screen;
   - alternating bounds;
   - rally at defensible terrain;
   - re-evaluate whether to hold, re-flank or continue disengaging.

7. Better black-box telemetry
   - top candidate actions and their utility components;
   - selected destination and route exposure;
   - reason an action was rejected.
