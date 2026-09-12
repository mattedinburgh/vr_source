# Deidranna Army tactical doctrine

This branch adds a doctrine layer above the existing human-like tactical AI.

The doctrine layer does **not** grant hidden CTH, AP, perception, health or equipment
bonuses. All classes keep using JA2 personal/public opponent knowledge and the same
anti-cheat rules documented in `AI_Knowledge_Audit.md`.

## Profiles

| Profile | Main mapping | Initiative | Mission anchoring | Complex manoeuvre |
| --- | --- | --- | --- | --- |
| SECURITY | enemy administrators | very low | very high | no |
| LINE | ordinary army | low/moderate | moderate | only with nearby command |
| VETERAN | officers/commanders and cunning regulars | moderate/high | low | yes |
| ELITE_MOBILE | mobile enemy elites | high | low | yes |
| ELITE_GUARD | stationary/guard/sniper elites | high tactical skill, low roaming | high | yes |

Militia is deliberately left on the existing shared human-like AI behavior; these
restrictions are specifically for Deidranna's heterogeneous army.

## Command effect

A nearby active officer/commander provides local command support to line infantry.
Command support dynamically enables:

- independent flanking;
- full capability-aware fire-and-manoeuvre selection;
- proactive suppression preparation;
- proactive movement smoke;
- the normal combat-medic rescue envelope.

If the leader becomes incapacitated, cowers, disengages or escapes, ordinary line
troops immediately revert to simpler covered movement/support behavior. Veterans and
elites remain independently capable.

## Contact / QRF doctrine

The existing staged noise-response system remains the base.

Initial distant response size is doctrine-dependent:

- SECURITY: 2 (maximum 3 even on mobile orders);
- LINE: 4;
- VETERAN: 5;
- ELITE_MOBILE: 6;
- ELITE_GUARD: 4.

`ONCALL` expands the response element by two; `SEEKENEMY` by one.

After confirmed radio contact, the existing perceived-enemy-strength reinforcement
waves can expand line/QRF commitment. Fixed SECURITY troops remain capped at three
unless explicitly assigned ONCALL/SEEKENEMY, and ELITE_GUARD troops are capped at five,
so a distant firefight does not automatically empty a defended facility.

## Fire and manoeuvre

Everyone retains basic self-preservation, cover, fallback, dispersion and mutual-support
logic.

Lower-quality enemy formations are intentionally constrained:

- SECURITY will not independently solve exposed advance geometry;
- uncommanded LINE troops accept simple covered movement but avoid complex exposed
  manoeuvre;
- advanced crossfire scoring is disabled for SECURITY/uncommanded LINE;
- uncommanded basic elements use a one-mover-at-a-time cap;
- capability-based selection of the best mover is reserved for commanded/veteran/elite
  elements;
- unsupported improvisational dashes are reserved for experienced troops.

## Suppression and smoke

Specific covering-fire tasks remain available to ordinary troops. Direct-contact and
return-fire suppression also remain available.

Proactive support is restricted:

- SECURITY: reactive only;
- uncommanded LINE: reactive only;
- commanded LINE / VETERAN / ELITE: proactive suppression preparation and movement smoke.

Emergency protection smoke for casualties, pinned troops and survival remains
available regardless of doctrine.

## Medical behavior

Combat medics still refuse suicidal rescue paths.

- SECURITY medics are local first-aid assets: short rescue radius, low exposure tolerance.
- Uncommanded LINE medics use a moderately reduced rescue envelope.
- Commanded LINE, VETERAN and ELITE medics use the full human-like rescue logic.

## Objective anchoring

Existing map orders remain authoritative and are used instead of hard-coding sectors.

SECURITY strongly respects STATIONARY / ONGUARD / patrol assignments.
LINE infantry has moderate anchoring.
ELITE_GUARD stays mission-focused.
ELITE_MOBILE retains freedom to manoeuvre.

This allows Alma, SAM sites, prisons, Meduna and other designed strongpoints to inherit
doctrinal behavior from mapper-assigned orders without sector-specific AI code.

## Systems intentionally unchanged

- knowledge / fog-of-war contract;
- perceived force ratios;
- casualty and stress model;
- morale/rout cascade;
- individual self-preservation;
- tactical fallback and disengagement;
- escape and surrender;
- grenade/NCTH work;
- strategic militia retreat;
- militia human-like tactical parity.

The doctrine layer is therefore an identity/command system on top of the existing AI,
not a replacement tactical engine.
