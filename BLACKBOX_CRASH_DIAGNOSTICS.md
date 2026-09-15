# Vengeance Crash Black Box

Vengeance now has an always-on crash **flight recorder** layered on top of the existing
exception report and Windows minidump system. Recorder **v5** is intentionally independent
of VFS/FileMan for its durable journal and has an additional watchdog path that does not
take the normal recorder lock.

The design goal is aggressive diagnostics without making ordinary gameplay dependent on
constant disk I/O.

## Files produced

- `BlackBox_LastRun.log` — durable milestones from the current run.
- `BlackBox_PreviousRun.log` through `BlackBox_PreviousRun_4.log` — retained earlier runs.
- `BlackBox_Hang_LastRun.log` — watchdog evidence when the main game loop stops responding.
- `BlackBox_Hang_PreviousRun.log` through `BlackBox_Hang_PreviousRun_4.log` — retained hang evidence.
- `Crash Report_DD_MM_YYYY___HH_MM_SS.txt` — text crash report containing the recorder
  snapshot, first-chance exception feed, subsystem states, checkpoint history, durable
  history, registers and stack data.
- `Vengeance-Crash-<PID>-<TID>-YYYYMMDD-HHMMSS.dmp` — Windows minidump captured before
  the more complex text/stack reporting work. Recorder v5 first tries a richer dump with
  referenced memory, unloaded modules and additional thread/process metadata, then
  automatically falls back to the legacy-compatible dump flags if the installed
  `dbghelp.dll` rejects the enhanced request.

## Recorder v5 event format

Durable events carry enough information to correlate work across threads and time:

```
[15:48:39.524] [+22466ms] [#000123] [T45932] [B1] ASSET REQUEST: TILESETS\50\B1_T_SAND1.STI
```

Each event contains wall-clock time, process-relative uptime, sequence, Windows thread ID,
subsystem/category and message.

The in-memory recorder now keeps the latest **4,096 durable events**.

## High-frequency checkpoint history

`BlackBoxCheckpoint()` is memory-only and is safe for hot diagnostic paths.

Recorder v5 keeps:

- the latest global checkpoint;
- the latest **4,096 high-frequency checkpoints** as a circular history;
- the latest checkpoint independently for up to **32 subsystems**.

Existing map, AI, save/load, VFS, B1/remaster and UI callers automatically benefit from the
larger history.

## Main-loop heartbeat and hang watchdog

`GameLoop()` now sends one cheap lock-free heartbeat every loop iteration.

The watchdog runs on a separate thread. Once gameplay has started, if the main thread stops
producing heartbeats for **8 seconds**, the watchdog writes independent evidence to
`BlackBox_Hang_LastRun.log`. A continued hang produces another breadcrumb every 15 seconds.

This is deliberately separate from the normal `BlackBoxEvent()` lock. If the main thread
deadlocks while holding the recorder lock, the watchdog can still preserve:

- how long the main thread has been stalled;
- main-thread ID;
- last heartbeat sequence;
- last known screen;
- exact frame phase (`INPUT`, `SCREEN_HANDLER`, `RENDER`, `CLOCK`, `NETWORK`, etc.);
- latest semantic checkpoint.

For an 8+ second stall, the watchdog briefly suspends the main thread, captures its x86
register context plus a small raw stack window, immediately resumes it, and only then writes
the evidence to disk. It does not perform symbol handling while the game thread is suspended.

When a stalled main loop eventually resumes, the normal durable timeline also receives a
`STALL` event with the measured gap.

## First-chance structured-exception feed

Recorder v5 installs a Windows vectored exception handler for fatal-class structured
exceptions. It records the fault **before** normal exception dispatch has a chance to mask
or transform it.

Tracked classes include:

- access violations;
- illegal/privileged instructions;
- stack overflow;
- heap corruption;
- stack-buffer-overrun / fast-fail status;
- in-page errors;
- array bounds and datatype alignment faults;
- integer/floating divide-by-zero;
- invalid handles and integer overflow.

The exception handler never takes the normal recorder lock and never performs normal log
I/O. It publishes compact evidence into a **256-slot lock-free emergency ring**. If the
game recovers, the next normal heartbeat drains those records into the durable log. If the
game crashes, the raw emergency ring is written directly into the crash report.

Normal C++ exceptions and debugger breakpoint/single-step exceptions are intentionally not
treated as fatal-class first-chance errors to avoid useless noise.

## Recent user input and window state

Mouse button/repeat/wheel events dequeued by the main loop are stored as memory-only
`INPUT` checkpoints with coordinates, button state and active screen. This is specifically
useful for UI crashes that happen immediately after clicking, dragging or resizing an
in-game control.

Windows lifecycle/display events are also captured:

- window activation / Alt-Tab;
- display-mode changes;
- enter/exit OS window resizing;
- close/destroy;
- `WM_SIZE` as a memory-only checkpoint.

## Health / leak telemetry

Once per second, the main loop stores a memory-only `HEALTH` checkpoint containing:

- heartbeat number and screen;
- inter-heartbeat gap;
- process handle count;
- GDI object count;
- USER object count;
- Windows memory pressure and available physical memory.

Every 30 seconds the same class of health data is written as a durable event. This makes
slow handle/GDI leaks and memory-pressure problems visible without per-frame disk writes.

## v5 durability and snapshot safety

Recorder v5 separates **write visibility** from **physical flush frequency**:

- every durable event is written to the Windows file handle immediately;
- ordinary events force a physical flush at most once per second;
- crash/assert/stall/recovery/fatal/watchdog events and failed timed operations flush
  immediately;
- recovered first-chance exception batches perform one forced flush after the batch;
- failed flushes are attempt-throttled, preventing repeated disk errors from turning the
  diagnostic system into a performance problem.

The fatal reporter still never takes the normal recorder lock. Instead, v5 uses
commit-marker validation around every lock-free snapshot class. Event/checkpoint ring
entries, subsystem state, structured context, counters, timed operations and first-chance
exception records are copied only when their commit marker is stable before and after the
copy. A partially published record is skipped or retried rather than printed as valid
evidence.

## Failure feeds

The black box receives dedicated evidence for:

- engine startup and clean shutdown;
- screen transitions;
- global engine errors and their message text;
- save/load lifecycle;
- map load phases and map metadata;
- B1/remaster asset visibility and STI/JSD loading stages;
- VFS errors;
- assertion failures;
- top-level fatal C++/VFS/standard exceptions;
- fatal-class first-chance structured exceptions;
- main-loop stalls and recoveries;
- periodic process-health state.

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
- watchdog/main-loop heartbeat age, last screen and exact frame phase;
- whether the enhanced or fallback minidump path succeeded;
- recent first-chance critical exceptions;
- latest global checkpoint;
- latest checkpoint for every active subsystem;
- high-frequency checkpoint timeline;
- durable event timeline.

The recorder deliberately does **not** acquire its main critical section while dumping crash
evidence. If the original fault occurred while a logger held that lock, attempting to acquire
it in the crash handler could deadlock and destroy the evidence.

## B1/remaster diagnostics

B1 asset loading reports distinct stages such as:

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

For a reproducible CTD or hang, attach all files that exist from this list:

1. newest `Crash Report_*.txt`;
2. matching `Vengeance-Crash-*.dmp`;
3. `BlackBox_LastRun.log`;
4. `BlackBox_Hang_LastRun.log` when present;
5. `BlackBox_PreviousRun.log` and/or `BlackBox_Hang_PreviousRun.log` if the game has
   already been restarted;
6. older retained run files when the failure happened several launches ago.

For a non-crashing failure, `BlackBox_LastRun.log` is usually enough to reconstruct the
durable timeline. For a freeze/hang, the independent hang log is particularly important.
