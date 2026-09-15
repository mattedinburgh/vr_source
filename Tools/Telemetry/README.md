# Automatic Vengeance telemetry upload

The telemetry agent removes the manual "send me the Black Box / Companion files" step.

## What it does

- starts automatically with the current Windows user;
- watches for a JA2/Vengeance executable running under the game root;
- on game exit, slices only the log bytes generated since the previous packaged session;
- creates a session folder with:
  - `Campaign Tactical Black Box.tsv`
  - `Campaign Tactical Decisions.tsv`
  - `Campaign AI Black Box.tsv`
  - `Campaign AI Companion.txt`
  - `session.json`
- pushes the session to `telemetry/companion-sessions`;
- if offline/auth fails, keeps the session locally and retries automatically later.

No credentials are stored in these scripts. Git uses the credentials already configured on the PC.

## Install

Run:

`Tools\Telemetry\INSTALL_VR_TELEMETRY_AGENT.cmd`

once.

The installer uses the current-user Windows Run key, so administrator rights should not normally be required.

## Local queue

Pending and uploaded packages live under:

`%LOCALAPPDATA%\VengeanceTelemetry\pending`

Agent diagnostics:

`%LOCALAPPDATA%\VengeanceTelemetry\agent.log`

## Important

The agent deliberately excludes Map Editor, updater, setup and installer processes. It accepts game executables whose names contain `ja2` or `vengeance` and whose executable path is inside the Vengeance game folder.

If the actual game executable uses a completely different name, extend the match rule in `Get-GameProcesses`.

## Privacy / scope

Only the four named AI telemetry files plus a small session manifest are uploaded. Savegames, screenshots, personal files and unrelated game files are not uploaded.
