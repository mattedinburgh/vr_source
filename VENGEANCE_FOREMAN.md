# Vengeance Foreman

This file is the durable coordination layer for short-lived ChatGPT work sessions on Vengeance Reloaded.

## Mandatory startup rule

For every substantive Vengeance task, treat the current chat as a disposable worker and consult the foreman before acting.

Read/establish state in this order:

1. `VENGEANCE_FOREMAN.md` — project-wide intent, rules, ownership and continuity protocol.
2. The relevant stream memory/checkpoint — stream purpose, durable decisions, completed work, tests, blockers and precise next action.
3. Current repository reality — branch, HEAD, working tree/diff, relevant files and build/test state.
4. Only then use chat history as supplementary context when durable state is insufficient.

Repository/code state wins over stale prose. A verified newer checkpoint wins over an older checkpoint. Do not redo completed work unless verification shows it is wrong, incomplete or incompatible.

## Operating model

- **Foreman:** durable project + stream state.
- **Chat:** short-lived worker executing the next useful unit.
- **Git/GitHub:** source of truth for code, branches, commits and durable coordination files.
- **Remote machine access:** execution/build/test/deploy bridge when required; conserve calls when GitHub can supply the needed state.

The objective is to make chat history disposable. A fresh chat should be able to resume safely after reading the foreman, the relevant stream checkpoint, and the repository state.

## Project-wide intent

Preserve the identity and content of Vengeance: Reloaded while selectively modernising tactical systems using newer JA2 1.13 ideas where they improve gameplay. Prefer systemic, testable fixes over one-off patches. Preserve legal fog-of-war and avoid artificial AI accuracy/AP/knowledge cheats. Keep changes compatible across streams and avoid silently undoing previously validated work.

## Stream memory standard

Each active stream should maintain a compact durable record containing:

- **Purpose / desired end-state**
- **Design principles and constraints**
- **Ownership / boundaries** — what this stream owns and what belongs elsewhere
- **Dependencies / cross-stream interfaces**
- **Major decisions and rationale**
- **Known pitfalls / failed approaches**
- **Current branch and verified HEAD**
- **Completed work**
- **Files changed**
- **Tests performed and results**
- **Remaining work**
- **Blockers / unresolved issues**
- **Precise next implementation action**
- **Do-not-redo list**

Keep slow-changing design memory separate from fast-changing session/checkpoint data when a stream becomes large enough to justify two files.

## Current stream families

Use the closest existing stream instead of creating overlapping ownership casually:

- Tactical AI / AI Engine / Performance
- Weapons / Attachments / NCTH
- Maps / World / Tactical Geometry
- Cover / Visibility / Camouflage / Penetration
- World Interactions / Construction / Tactical Environment
- UI / Tactical Information / Audio / Localization
- Items / Inventory / Loot / Ammo / Grenades / Explosives
- Integration / Consolidation / Release
- Battle Arena / regression validation where cross-stream testing is required

## Worker execution rule

After startup verification, continue directly from the latest unfinished implementation unit. Spend only a small fraction of the session re-validating methodology unless a material inconsistency appears. Prefer completed, testable implementation over additional planning.

Do not stop merely because one small subtask is complete; continue to the next unfinished unit while useful work can still be completed safely in the current session.

## End-of-session handoff

Before ending a substantive implementation session, update the relevant durable stream checkpoint with:

1. completed work,
2. files changed,
3. tests performed and results,
4. remaining work,
5. blockers/unresolved issues,
6. exact next action,
7. branch + verified HEAD/commit when available.

If a change affects another stream, record that dependency explicitly rather than relying on chat memory.

## Conflict rule

If the foreman/checkpoint disagrees with the repository, verify the repository and repair the durable memory. Never force code to match stale notes simply for consistency.
