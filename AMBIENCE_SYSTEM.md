# Vengeance Adaptive Ambience

## Purpose

Vengeance uses a data-driven sector soundscape layer on top of JA2's existing audio engine. The goal is environmental identity without masking tactical information.

The system is implemented in `TileEngine/Ambient Control.cpp` and configured by `Data-Vengeance/SectorAmbience.ini` in `vr_gamedir`.

## Runtime model

A sector resolves a profile in this order:

1. exact `[SECTOR_OVERRIDES]` entry (including underground suffixes such as `D4_B1`);
2. current tileset entry from `[TILESET_PROFILES]`;
3. `DEFAULT_PROFILE` or `DEFAULT_UNDERGROUND_PROFILE`.

If no usable VR profile is available, the original JA2 `.bad` ambience system is used unchanged.

A VR profile contains:

- one loop per time phase (dawn/day/dusk/night); dawn falls back to day and dusk falls back to night;
- up to eight one-shot sounds per phase;
- weighted random selection;
- no immediate repeat of the same one-shot;
- randomized stereo pan and volume;
- configurable event spacing;
- rain attenuation and reduced event density;
- combat loop ducking;
- one-shots disabled during combat by default.

The one-shot pool is intentionally a single random-container-style scheduler. Do not register each cue as an independent random timer: that multiplies event density and produces artificial overlaps.

## Profile keys

Core keys:

- `DAY_LOOP`, `NIGHT_LOOP`, optional `DAWN_LOOP`, `DUSK_LOOP`
- `LOOP_VOLUME`, or phase-specific `*_LOOP_VOLUME`
- `DAY_SOUND_1..8`, `NIGHT_SOUND_1..8`, optional dawn/dusk equivalents
- `*_WEIGHT_1..8` (default 1)
- `*_MIN_MS`, `*_MAX_MS`
- `*_VOLUME` or `*_VOLUME_MIN` / `*_VOLUME_MAX`
- `*_PAN_MIN`, `*_PAN_MAX`
- `COMBAT_LOOP_PERCENT`
- `RAIN_LOOP_PERCENT`
- `RAIN_ONESHOT_VOLUME_PERCENT`
- `RAIN_ONESHOT_INTERVAL_PERCENT`
- `ONESHOTS_IN_COMBAT` (default FALSE)
- `FADE_STEP_MS`

## Mixing principles

- Continuous beds are quiet. They establish air, not foreground content.
- Contextual one-shots are sparse and weighted: common sounds get higher weights, distinctive cues lower weights.
- Wide pan is appropriate for outdoor birds, vehicles, surf and distant activity. Underground profiles use narrower stereo ranges.
- Civilian, animal and novelty cues normally stop spawning during combat.
- Loops remain in combat at a reduced level so a location keeps its acoustic identity without masking gunfire, footsteps, speech or tactical cues.
- Rain reduces the volume and frequency of exposed outdoor events.
- Avoid recognizable copyrighted music. San Mona's radio/guitar cue is an original project asset.

## Current authored identities

Profiles include rural, forest, jungle, farm, city, San Mona, coast, desert, swamp, industrial, oil rig, military, airport, underground, mine, sewer, dam, prison, hospital, palace, lab, wasteland, hacienda, scrapyard, racetrack, mall and resort.

## Data validation

Every profile audio path and every profile reference should resolve before deployment. At the 2026-09-14 integration pass:

- 185 audio references resolved;
- 0 audio references were missing;
- 182 sector/tileset profile references resolved;
- 0 profile references were missing.

## Deployment

Use `DEPLOY_AMBIENCE.cmd` or `DEPLOY_AMBIENCE.ps1` from `vr_gamedir`.

Deployment is incremental by default: only new or changed ambience assets and `SectorAmbience.ini` are copied. Use `-Force` only for clean recovery or deliberate full ambience redeployment.

## Playtest priorities

1. San Mona C5/C6/D5 at day, dusk and night.
2. A farm sector during day and night.
3. Oil rig / industrial sector in rain.
4. Airport profile.
5. Mine and sewer underground.
6. Enter and leave combat while listening for smooth duck/recovery.
7. Stay in one sector through a day/night boundary to verify loop transition and one-shot pool change.
