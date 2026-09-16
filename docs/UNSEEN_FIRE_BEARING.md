# Unseen Fire Bearing — Tactical Combat Feedback

Status: **ACTIVE / PLAYTEST V1**  
Owner: **UI / UX — Tactical Combat Feedback**  
Scope: player-facing combat information only; no AI/NCTH behavior changes.

## Objective

Give the player a readable visual indication of the **direction of unseen incoming fire** without revealing the hidden shooter's exact position.

## Core design

- Trigger only when the shooter is not currently visible to the relevant player-side observer.
- Prefer a thin, translucent directional arc/chevron around the threatened merc.
- Add a small viewport-edge bearing cue when the source direction is substantially off-screen.
- Never center/snap the camera to the shooter.
- Never place a locator on the true hidden-enemy grid.
- Do not display exact distance or weapon identity.
- Use roof/up/down indication when source level is inferable.

## Information sources

Reuse existing engine data:
- gunfire/noise: `MakeNoise()`, `ProcessNoise()`, `HearNoise()`
- acoustic direction: existing `atan8()` bearing and effective-volume calculations
- visibility: opponent lists / LOS state
- ballistic direction: current bullet trajectory / firer data in `bullets.cpp`
- rendering foundation: existing multi-purpose/radio locator and topmost UI rendering

## Perception and fairness

A cue is allowed only when a player merc could plausibly infer direction:
- audible muzzle blast
- bullet passes close enough to establish incoming direction
- bullet hits a merc
- visible/audible nearby impact
- perceptible muzzle flash

No cue when the shot is effectively silent/unheard and produces no observable near-miss, hit, impact, or flash.

## Confidence model

- near ballistic pass / hit: narrow, high-confidence bearing
- loud nearby unsuppressed shot: moderately narrow
- distant acoustic-only shot: wider/softer
- suppressed/subsonic shot: reduced confidence or no acoustic cue
- hearing aid / strong hearing: improve acoustic confidence
- deafened / impaired listener: reduce or suppress acoustic cue

The visual cue represents a **bearing/confidence sector**, never an exact tile.

## Burst and multi-shooter handling

Current V1:
- repeated audible gunfire reports from the same coarse direction are clustered for 450 ms;
- this prevents automatic-fire bursts and multiple listeners from continuously restarting the global locator;
- materially different coarse directions are not suppressed by that clustering gate.

Still required for the final renderer:
- aggregate rounds by firer + similar bearing + short time window;
- maintain a small fixed cue pool so multiple directions can remain visible at once;
- reinforce an existing bearing cue rather than replacing it;
- preserve distinct cues for materially different shooter directions.

## Persistence

- strong during the hostile action
- fades after the burst/shot
- faint remembered bearing may remain briefly into the player's turn
- remove when stale or when normal visible-enemy presentation supersedes it

## Implementation constraints

- no Tactical AI decision changes
- no Strategic AI changes
- no NCTH accuracy/recoil changes
- no VHD dependency
- low-cost event-driven state; no continuous LOS/pathfinding scans
- visual styling can later be harmonised with Cold UI

## Primary integration points

- `Tactical/Weapons.cpp`
- `Tactical/bullets.cpp`
- `Tactical/opplist.cpp`
- `Tactical/LOS.cpp`
- `Tactical/Interface.cpp` / `Interface Panels.cpp`

## Current implementation

Implemented on the current-base tactical-information stream:
- unseen audible gunfire is converted to a coarse eight-direction bearing;
- the displayed locator is offset from the listener rather than placed on the hidden shooter's true grid;
- the locator is invoked with camera sliding disabled;
- existing hearing-volume thresholds and hearing-aid logic remain the permission gate;
- same-direction reports are clustered for 450 ms to reduce burst/listener spam.

V1 deliberately uses the existing single multi-purpose locator. That makes it low-risk and easy to validate, but it also means simultaneous bearings cannot yet remain on screen together.

## Exit criteria

Implementation is complete only after:
1. hidden shooters never leak exact positions;
2. burst spam is aggregated without suppressing materially different bearings;
3. multiple directions can remain readable simultaneously;
4. suppressors/hearing/level differences affect cue confidence appropriately;
5. no unwanted camera movement occurs;
6. near-miss/hit ballistic information can strengthen a bearing without exposing the shooter;
7. roof/up/down information is conveyed only when legitimately inferable;
8. in-game playtest confirms the cue is informative but not arcade-like.
