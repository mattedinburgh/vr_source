# Unseen Fire Bearing — Tactical Combat Feedback

Status: **QUEUED / DESIGN-READY**  
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

- Aggregate rounds by firer + similar bearing + short time window.
- A burst reinforces one marker instead of spawning one marker per bullet.
- Maintain a small fixed cue pool and cluster nearby bearings.
- Preserve distinct cues for materially different shooter directions.

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

## Queue exit criteria

Ready to implement when the UI/UX stream is selected for active work. Implementation is complete only after:
1. hidden shooters never leak exact positions;
2. burst spam is aggregated;
3. multiple directions remain readable;
4. suppressors/hearing/level differences affect cue confidence;
5. no unwanted camera movement occurs;
6. in-game playtest confirms the cue is informative but not arcade-like.
