# Vengeance Crash Black Box

Vengeance now has an always-on crash **flight recorder** layered on top of the existing
exception report and Windows minidump system. It is intentionally independent of VFS/FileMan
for its own durable journal, so it remains useful when the file layer itself is failing.

## Files produced

- `BlackBox_LastRun.log` — durable milestones from the current run. Important events are
  written and flushed immediately.
- `BlackBox_PreviousRun.log` — the immediately previous run.
- `BlackBox_PreviousRun_2.log` through `BlackBox_PreviousRun_4.log` — older retained runs,
  so restarting after a CTD does not immediately destroy the only useful timeline.
- `Crash Report_DD_MM_YYYY___HH_MM_SS.txt` — text crash report containing the recorder
  snapshot, subsystem states, checkpoint history, durable history, registers and stack data.
- `Vengeance-Crash-<PID>-<TID>-YYYYMMDD-HHMMSS.dmp` — Windows minidump captured before
  the more complex text/stack reporting work.

## Recorder v2 event format

Durable events now carry enough information to correlate work across threads and time:

```
[15:48:39.524] [+22466ms] [#000123] [T45932] [B1] ASSET REQUEST: TILESETS\50\B1_T_SAND1.STI
```

Each event contains:

- wall-clock time;
- process-relative uptime;
- monotonically increasing event sequence;
- Windows thread ID;
- subsystem/category;
- message.

The in-memory recorder keeps the latest **1,024 durable events**.

## High-frequency checkpoint history

`BlackBoxCheckpoint()` is still memory-only, so it can be used in hot paths without a disk
flush. It no longer stores only one overwritten string.

Recorder v2 keeps:

- the latest global checkpoint;
- the latest **1,024 high-frequency checkpoints** as a circular history;
- the latest checkpoint independently for up to **32 subsystems**.

This means a MAP checkpoint no longer destroys the most recent AI, SAVE, VFS, B1 or UI state.

Current high-frequency instrumentation includes detailed map-loader state and tactical AI
state. Existing callers automatically benefit from the new checkpoint history without needing
to be rewritten.

## Failure feeds

The black box now receives dedicated evidence for:

- engine startup and clean shutdown;
- screen transitions;
- save/load lifecycle;
- map load phases and map metadata;
- B1/remaster asset visibility and STI/JSD loading stages;
- VFS errors;
- assertion failures;
- top-level fatal C++/VFS/standard exceptions.

## Crash snapshot

Before stack walking, the text crash report records a semantic snapshot including:

- exception code, flags and address;
- access-violation operation/address when applicable;
- PID and crashing thread ID;
- process uptime;
- Windows `GetLastError()` value;
- physical/pagefile memory pressure;
- executable path and current working directory;
- recorder health counters;
- latest global checkpoint;
- latest checkpoint for every active subsystem;
- high-frequency checkpoint timeline;
- durable event timeline.

The recorder deliberately does **not** acquire its critical section while dumping crash
evidence. If the original fault occurred while a logger held that lock, attempting to acquire
it in the crash handler could deadlock and destroy the evidence.

## B1/remaster diagnostics

B1 asset loading now reports distinct stages such as:

```
ASSET EXISTS
LOAD TILE SURFACE BEGIN
TILE LOAD BEGIN
CREATE IMAGE OK
CREATE VIDEO OBJECT OK
JSD FOUND
JSD LOAD OK
ZSTRIP OK
TILE LOAD COMPLETE
ASSET LOADED
```

Failures are explicit, for example:

```
ASSET MISSING
CREATE IMAGE FAILED
JSD LOAD/COUNT FAILED
ZSTRIP FAILED
FALLBACK DECODE FAILURE
```

Visual remaster failures should fall back to authored assets instead of making B1 unplayable,
while the recorder preserves the exact reason for the fallback.

## Crash-ticket procedure

For a reproducible CTD, attach all files that exist from this list:

1. newest `Crash Report_*.txt`;
2. matching `Vengeance-Crash-*.dmp`;
3. `BlackBox_LastRun.log`;
4. `BlackBox_PreviousRun.log` if the game has already been restarted;
5. older `BlackBox_PreviousRun_*.log` files when the failure happened several launches ago.

For a non-crashing failure, `BlackBox_LastRun.log` is usually enough to reconstruct the
durable timeline; a later crash report additionally contains the in-memory checkpoint history.
