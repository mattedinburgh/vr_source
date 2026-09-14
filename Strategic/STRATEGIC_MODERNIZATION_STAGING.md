# Strategic modernization staging branch

Branch: `inactive/strategic-modernization`

This branch is intentionally isolated from `master`. It prepares modern 1.13 strategic systems without enabling them in normal Vengeance gameplay.

## Current state

| Layer | State | Runtime effect |
| --- | --- | --- |
| Team-aware strategic groups | Compatibility bridge staged | None for existing player/enemy groups |
| Strategic enemy transports / convoys | Build/API scaffold present, gate OFF | None |
| Enemy strategic helicopters | Build/API scaffold present, gate OFF | None |
| ASD strategic asset purchasing | Build/API scaffold present, gate OFF | None |

All consumer gates are defined in `Strategic/Strategic Modernization.h` and default to 0.

## Phase 0: team-group framework

Modern 1.13 changed strategic `GROUP` ownership from the binary `fPlayer` model to a team-owned model. Vengeance still has extensive legacy code that reads `fPlayer` directly, so this branch uses a compatibility migration instead of changing every caller at once.

The bridge:

- keeps legacy `fPlayer` for current VR code;
- adds `usGroupTeam` in bytes that were previously `GROUP` padding;
- consumes the same number of bytes, keeping the serialized `GROUP` size unchanged;
- writes an `STG` marker beside the team field;
- derives OUR_TEAM / ENEMY_TEAM from `fPlayer` when loading an old save without that marker;
- provides `VR_GetStrategicGroupTeam`, `VR_SetStrategicGroupTeam`, and normalization helpers;
- initializes player, vehicle and enemy groups through the new bridge.

No militia/other-team strategic groups should be created until legacy strategic code that assumes `!fPlayer == enemy` has been audited and converted to the helper API.

## Upstream 1.13 reference points

Reference repository: `1dot13/source`

- `Strategic/Strategic Movement.h` — blob `6f29c9697930b573066e416daf8a987e3dc5ff43`
- `Strategic/Strategic Movement.cpp` — blob `cec0f1b126fdcfe98d38f20145d9a25ba42b67a8`
- `Strategic/Strategic Transport Groups.h` — blob `2da967d913090522b173f6487581f969d384bb45`
- `Strategic/Strategic Transport Groups.cpp` — blob `b3f45b1d4c0265c2db3f2e49e51a70c31f4a220a`
- `Strategic/ASD.h` — blob `eb49150dca85345300fabc7014052214f72f7547`
- `Strategic/ASD.cpp` — blob `642a883c3b9b39ad6628710beb7e00ef91f36221`
- `Strategic/Game Event Hook.h` — blob `03ebfb996c47348065864247c7c65c5ea13e3b26`
- `Strategic/Game Event Hook.cpp` — blob `91a0eb62571fd490b48409ec3772c4f6f751582c`

## Activation order

1. **Finish team-group audit.** Convert strategic movement decisions that semantically mean player/enemy ownership away from raw `fPlayer` checks.
2. **Port transport groups first.** They can remain ENEMY_TEAM groups and therefore exercise the new plumbing with minimal behavioural risk.
3. **Decouple convoy rewards from ASD.** Current 1.13 transport groups include `ASD.h`; Vengeance should route rewards through a small adapter so convoys can be tested before ASD is active.
4. **Port enemy helicopter event/state logic.** Add event IDs, save/load state and strategic-map handling while keeping the feature gate OFF.
5. **Port ASD budget/purchasing.** Only after helicopter state is stable. ASD should purchase/refuel/repair assets rather than spawn them magically.
6. **Enable one feature at a time on this branch.** Never flip more than one consumer gate for the first regression pass.

## Do-not-merge conditions

Do not merge this branch to `master` while any of the following are true:

- transport, helicopter or ASD gates are enabled by default;
- old save compatibility has not been load-tested;
- a non-player/non-enemy strategic group can reach code that treats every `!fPlayer` group as enemy;
- strategic event save/load has not been audited for the new event types;
- autoresolve, reinforcement arrival, retreat and simultaneous-arrival logic have not been tested against staged groups.

## Intended end state

The long-term target is modern team-owned strategic groups plus Vengeance-specific operational AI. The 1.13 systems are inputs, not a blind wholesale port.


## Operational AI milestone — approved design

The operational layer is now being built around these campaign rules:

1. **Persistent enemy formations** — approved. Mobile enemy groups keep formation identity, mission, morale, supply, intelligence and retreat history across strategic movement and saves.
2. **Real operational objectives** — approved. Mission vocabulary includes garrison, patrol, recon, attack, raid, reinforce, relieve, intercept, block-road, escort, supply, retreat, regroup and reserve.
3. **Dynamic strategic target evaluation** — approved. Current scoring combines Queen priority, ownership, town/mine/SAM value, remembered hostile strength, distance and supply.
4. **Strategic reserves** — approved. Returning mobile formations can remain intact as local/regional/central reserves instead of being dissolved into anonymous pool points.
5. **Logistics / supply** — approved. Formation supply is persistent; movement consumes it, friendly infrastructure restores it, and critical supply forces regrouping.
6. **Recon / imperfect information** — approved. Operational scoring uses remembered strength snapshots rather than hidden live player counts. Sector loss, investigation and direct contact create reports with different confidence.
7. **Militia operational layer** — approved with a Vengeance constraint: keep garrison militia and mobile militia as separate concepts. Mobile militia must remain materially more expensive. Current Vengeance data already uses a 3x mobile training-cost multiplier; treat this as a floor, not a reason to merge the systems.
8. **Enemy helicopters / SAM interaction** — approved, but remains staged and disabled until the team-group/operational core is stable.
9. **Enemy strategic economy** — explicitly deferred. Do not make economy a required dependency for the operational AI, convoys or ASD.
10. **One connected campaign loop** — approved. Tactical outcomes, retreat/regrouping, formation morale, strategic movement, reserves, logistics, intelligence and subsequent objectives should feed one another.

### Current active experimental behavior on this branch

- team-aware strategic-group compatibility bridge;
- persistent operational state stored in legacy ENEMYGROUP padding with unchanged struct size;
- operational black box logging to `Strategic Operational BlackBox.txt`;
- imperfect-information snapshots and confidence decay;
- supply and morale recovery/degradation;
- persistent retreat history;
- operational target scoring;
- bounded operational garrison reassignment with deliberate legacy fallback/randomness;
- returning formations preserved as central reserves and eligible for later redispatch;
- critical-supply / critical-morale formations forced to regroup.

### Still staged OFF

- strategic transport/convoy runtime;
- enemy helicopter runtime;
- ASD asset purchasing/runtime.

Those systems stay compile-staged behind default-OFF gates until the operational core is validated.
