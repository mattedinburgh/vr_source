# Weather Modernization Design — Inactive Branch

Branch: `design/weather-modernization-inactive-2026-09-13`  
Base: `install/all-2026-09-12`  
Status: **INACTIVE / DESIGN ONLY**  
Runtime impact on playable build: **none**

## 1. Objective

Modernize Vengeance Reloaded weather using the current JA2 1.13 weather architecture as the reference, while adapting the actual weather catalogue and balance to Vengeance's tropical / Latin-American setting.

The target is not a blind 1.13 code dump. The target is a coherent Vengeance-native system that:
- preserves existing scripted sector behaviour,
- does not destabilize maps, saves, AI, or tactical timing,
- gives weather meaningful tactical consequences,
- supports sector-local weather instead of one global rain state,
- remains fully externalizable and easy to disable.

## 2. Current Vengeance baseline

The current branch still uses the older rain-centric architecture:
- global/current rain intensity,
- rain chance per day,
- min/max rain duration,
- rain-drop count,
- visibility reduction per rain intensity,
- weapon reliability reduction per rain intensity,
- breath recovery reduction per rain intensity,
- lightning/thunder timing.

Existing code to preserve and refactor rather than discard:
- `GameSettings.cpp/.h`
- `TileEngine/environment.cpp/.h`
- `Tactical/Rain.cpp`
- tactical visibility / hearing code
- `TileEngine/SmokeEffects.cpp`
- strategic event scheduling
- save/load sector state

## 3. Architecture

### 3.1 Sector weather state

Each surface sector receives its own persistent weather state.

Proposed logical model:

```cpp
enum WEATHER_TYPE
{
    WEATHER_CLEAR = 0,
    WEATHER_CLOUDY,
    WEATHER_DRIZZLE,
    WEATHER_RAIN,
    WEATHER_HEAVY_RAIN,
    WEATHER_THUNDERSTORM,
    WEATHER_FOG,
    WEATHER_DUST_STORM,
    WEATHER_TYPE_MAX
};
```

Each sector stores:
- weather type,
- intensity,
- start time,
- expected end time,
- front / system ID,
- transition state,
- optional wind strength,
- seeded variation value.

Underground sectors remain weather-free unless explicitly scripted.

### 3.2 Weather fronts

Weather should form contiguous regions rather than independent random rolls per sector.

Generation:
1. choose a weather system origin,
2. assign a radius / shape,
3. weight neighbouring sectors,
4. bias by terrain/region,
5. propagate over time,
6. dissipate gradually.

Neighbour continuity is important: if B1 is in heavy rain, adjacent A1/B2/C1 should have a materially higher chance of related weather.

### 3.3 Backward compatibility

Legacy Vengeance calls that expect rain intensity remain valid through a compatibility adapter.

Examples:
- old rain intensity 0 -> clear/cloudy
- low rain intensity -> drizzle/rain
- high intensity -> heavy rain/thunderstorm

No existing callsite should need to know the entire new system during the first port stage.

## 4. Vengeance weather catalogue

### Clear
Baseline. No penalties.

### Cloudy
Primarily visual/lighting.
- slightly flatter outdoor contrast,
- no meaningful combat penalty.

### Drizzle
Light atmospheric rain.
- visibility: -2%
- hearing: -10%
- breath recovery: -3%
- smoke decay: +5%

### Rain
Normal tropical rain.
- visibility: -6%
- hearing: -25%
- breath recovery: -8%
- weapon reliability stress: low
- smoke decay: +15%

### Heavy Rain
Strong tropical downpour.
- visibility: -12%
- hearing: -45%
- breath recovery: -12%
- weapon reliability stress: moderate
- smoke decay: +30%
- louder ambient masking.

### Thunderstorm
Severe rain plus lightning/thunder.
- visibility: -16%
- hearing: -60% average
- brief thunder masking windows can temporarily suppress sound localization further
- breath recovery: -15%
- weapon reliability stress: moderate
- smoke decay: +40%
- lightning flashes temporarily alter illumination.

### Fog / Morning Mist
Used primarily around dawn, jungle, water, valley and humid sectors.
- visibility: -20% to -40% depending on intensity,
- hearing: little/no penalty,
- no weapon reliability penalty,
- burns off gradually after sunrise.

### Dust Storm
Rare; only appropriate dry-region sectors.
- visibility: -25% to -45%
- hearing: -20%
- breath recovery: -10%
- weapon reliability stress: high
- smoke dispersal moderately accelerated.

Snow is deliberately excluded from the normal Vengeance campaign weather pool.

## 5. Tactical effects

### 5.1 Vision
Use weather-type multipliers rather than a single rain-intensity penalty.

Rules:
- affect both player and AI through the same core visibility calculation,
- no hidden AI immunity,
- optics reduce but do not nullify atmospheric visibility loss,
- thermal/night systems should not magically bypass severe rain/fog unless their real in-game logic supports it.

### 5.2 Hearing and noise localization
Weather must modify hearing centrally.

Heavy rain and storms should:
- reduce effective hearing distance,
- lower certainty of exact source localization,
- make distant gunfire less informative,
- reduce the probability that one shot alerts an entire sector.

Thunder events can create short masking windows. Gunfire during a thunderclap may still be heard locally but should be much harder to localize at range.

This integrates directly with the separate AI work on:
- smaller local response groups,
- proportional reinforcement,
- less whole-sector aggro,
- uncertainty-driven searches.

### 5.3 Suppression and morale
Do **not** arbitrarily alter the existing suppression baseline just because weather exists.

Weather may indirectly alter suppression through:
- reduced visibility,
- poorer source localization,
- shorter engagement ranges,
- lower confidence.

### 5.4 Smoke
Port the modern 1.13 concept that weather shortens outdoor smoke duration.

Initial proposal:
- drizzle: 0 to -1 extra lifetime unit per update,
- rain: -1,
- heavy rain: -2,
- thunderstorm: -3,
- dust storm: -1.

Indoor smoke is largely insulated from rain effects.

### 5.5 Weapons
Weather should affect reliability conservatively.

Avoid turning rain into a constant jam generator.

Suggested logic:
- modern/service weapons: small effect,
- worn/poor-condition weapons: larger effect,
- exposed weapon condition + weather combine,
- dust is worse for reliability than ordinary rain,
- existing condition/reliability mechanics remain primary.

### 5.6 Breath / fatigue
Weather modifies recovery, not raw AP economy.

Rain/storm effects should be modest enough that combat remains playable.

## 6. AI behaviour

AI receives the same sensory penalties as the player.

Additional weather-aware behaviour:
- reduced confidence when investigating sound during heavy weather,
- more local searches before calling distant groups,
- shorter expected visual-contact ranges,
- greater use of cover when visibility is poor,
- less long-range speculative fire,
- no magical target retention after LOS is lost,
- storm noise can delay reinforcement reaction.

Weather does **not** grant statistical cheating bonuses to compensate for reduced senses.

## 7. Lighting and presentation

### Rain
Retain existing Vengeance rain renderer initially.

### Heavy rain
Increase drop density/variation rather than simply multiplying identical drops.

### Lightning
Preserve current lightning system, but tie it to sector thunderstorm state.

Lightning should:
- briefly increase outdoor illumination,
- create a visible flash,
- produce thunder after distance-dependent/randomized delay,
- not permanently alter tactical light state.

### Fog
Prefer a lightweight tile/overlay solution compatible with the existing renderer.
Do not redesign the whole renderer for fog.

### Audio
Weather loops should cross-fade rather than hard switch.
Possible layers:
- light rain,
- heavy rain,
- storm bed,
- wind,
- distant thunder,
- close thunder.

## 8. Regional weighting

Weather probabilities should be data-driven by region/terrain.

Examples:
- jungle/humid: high rain, heavy rain, fog,
- urban: same regional weather, but no artificial city immunity,
- coast/water: higher mist/fog probability,
- dry interior: lower rain, occasional dust,
- underground: none.

This should be externalized rather than hard-coded by exact sector wherever possible.

## 9. Persistence and saves

Required:
- sector weather persists when leaving/re-entering,
- active fronts advance with strategic time,
- save/load restores weather cleanly,
- old saves load with a safe default mapping,
- weather state must not invalidate existing sector temp files.

Save versioning must be explicit if new serialized state is added.

## 10. Configuration

Create a dedicated section/file rather than expanding unrelated settings indefinitely.

Suggested:
`Weather_Settings.ini`

Core controls:
- ENABLE_ADVANCED_WEATHER
- ENABLE_LOCAL_WEATHER
- ENABLE_WEATHER_AI_EFFECTS
- ENABLE_WEATHER_SMOKE_EFFECTS
- ENABLE_WEATHER_WEAPON_EFFECTS
- ENABLE_FOG
- ENABLE_DUST_STORMS
- weather probabilities
- duration ranges
- transition rates
- vision/hearing/breath/reliability multipliers
- visual density
- audio levels

Default on this branch during development:
```
ENABLE_ADVANCED_WEATHER = FALSE
```

This gives a second protection layer even if the branch is accidentally built.

## 11. Implementation sequence

### Phase 0 — Design only
Current state of this branch.

### Phase 1 — Data model
- weather enum/type,
- sector state,
- compatibility adapter,
- no tactical effect.

### Phase 2 — Strategic scheduler
- localized weather generation,
- fronts,
- persistence,
- save/load.

### Phase 3 — Existing rain integration
- map old rain renderer onto new weather state,
- storms/lightning,
- no new renderer yet.

### Phase 4 — Tactical sensory effects
- visibility,
- hearing,
- sound localization,
- AI parity.

### Phase 5 — Smoke, breath, reliability
- smoke decay,
- stamina,
- conservative weapon effects.

### Phase 6 — Fog/dust presentation
- visuals/audio,
- terrain-region weighting.

### Phase 7 — Balance
- long campaign test,
- night combat,
- urban combat,
- jungle/open terrain,
- save compatibility,
- performance profiling.

## 12. Files expected to change when implementation starts

Primary:
- `TileEngine/environment.h`
- `TileEngine/environment.cpp`
- `Tactical/Rain.cpp`
- `GameSettings.h`
- `GameSettings.cpp`

Likely:
- `Tactical/opplist.cpp` for hearing/vision integration,
- `TileEngine/SmokeEffects.cpp`,
- strategic event code,
- save/load structures,
- tactical AI sound investigation logic,
- external INI data in `vr_gamedir`.

## 13. Safety / integration rules

1. Never merge this work directly into the playable branch without a separate audit.
2. No weather code may bypass existing story/scripted sector events.
3. Player and AI use the same weather sensory rules.
4. No global whole-sector alert shortcuts introduced by weather.
5. Existing rain renderer remains the fallback until new state logic is proven.
6. Every phase should compile independently.
7. Advanced weather defaults OFF until its full test matrix passes.
8. No deployment script should point at this branch while it remains inactive.

## 14. Acceptance criteria before activation

Must pass:
- 100+ strategic weather transitions without crash,
- save/load during every weather type,
- sector enter/leave/re-enter persistence,
- underground transitions,
- battle during lightning,
- night battle during storm,
- smoke grenades outdoors/indoors during rain,
- AI hearing comparison clear vs storm,
- no whole-sector instant aggro regression,
- no player/AI asymmetric sight rule,
- no scripted-sector regression,
- no measurable pathological performance loss.

## 15. Activation path

When implementation is eventually approved:
1. finish development on this isolated branch,
2. audit against latest `install/all-2026-09-12` descendant,
3. rebase/merge current playable changes,
4. build Release Win32,
5. run weather-specific smoke/save/AI tests,
6. only then merge into the active integration branch,
7. enable `ENABLE_ADVANCED_WEATHER` in data after code validation.
