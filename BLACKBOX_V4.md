# Vengeance Black Box v5

The black box is a crash/freeze flight recorder designed for this modified 32-bit Vengeance build. It complements the legacy JA2 debug logs instead of replacing them.

## What it records

- **Durable event timeline**: 4,096 important events kept in memory and written to `BlackBox_LastRun.log`.
- **High-frequency breadcrumbs**: 4,096 memory-only checkpoints for hot paths such as AI, input, save/load and screen handling.
- **Per-subsystem latest state**: the newest AI/SAVE/SCREEN/etc. breadcrumb is retained independently.
- **Structured context**: persistent key/value state such as the current save operation, sector request and screen transition.
- **Named counters**: attempts/successes/failures and other cumulative diagnostic counters.
- **Timed operations**: nested operations (for example LOAD -> SECTOR) remain visible as active until they complete. Slow operations (>=2 s) and failures become durable events.
- **First-chance exception feed**: critical SEH exceptions are captured before normal crash handling can obscure the first fault.
- **Main-loop watchdog**: after an 8-second stall, the watchdog captures frame phase, registers, stack words and a process minidump.
- **Resource health**: heartbeat, process handles, GDI/USER objects and memory pressure are sampled without logging every frame.
- **Fatal crash report**: structured context, counters, operations, checkpoints, events, registers and stack information are embedded in the normal crash report and binary dump.

## Output files

Normal run:
- `BlackBox_LastRun.log`
- `BlackBox_PreviousRun*.log`

Freeze/hang:
- `BlackBox_Hang_LastRun.log`
- `BlackBox_Hang_LastRun.dmp`
- rotated `BlackBox_Hang_PreviousRun*` files

Fatal crash:
- timestamped `Crash Report_*.txt`
- timestamped crash `.dmp` file

## One-click collection

Double-click:

`COLLECT_BLACKBOX.bat`

It runs `COLLECT_BLACKBOX.ps1`, detects the Vengeance game root from the repository location, and creates:

`BlackBoxBundles\Vengeance_BlackBox_YYYYMMDD_HHMMSS.zip`

The ZIP contains current/rotated black-box evidence, recent crash reports/dumps, a small set of relevant configuration files, executable SHA-256/build identity, Git branch/commit/status when available, and a SHA-256 manifest.

It deliberately does **not** collect save games, screenshots, arbitrary user files, or an environment-variable dump.

## Instrumentation API

Durable milestones:

```cpp
BlackBoxEvent("SAVE", "opened slot=%d", slot);
```

Hot-path breadcrumb:

```cpp
BlackBoxCheckpoint("AI", "soldier=%u action=%d grid=%d", id, action, grid);
```

Persistent structured state:

```cpp
BlackBoxContext("sector.current", "x=%d y=%d z=%d", x, y, z);
```

Cumulative diagnostic counter:

```cpp
BlackBoxCounterAdd("load.failures", 1);
```

Multi-stage operation:

```cpp
DWORD token = BlackBoxOperationBegin("SECTOR", "SetCurrentWorldSector A3");
BOOLEAN ok = LoadSector();
BlackBoxOperationEnd(token, ok ? "OK" : "FAILED");
```

If the game dies before `BlackBoxOperationEnd`, the crash report shows that operation as **ACTIVE**.

## Reliability hardening in v5

The recorder format remains compatible with existing `vr-blackbox-1` telemetry while the
analysis path is being hardened separately.

Current reliability changes:

- a crash-truncated **final** JSONL record no longer makes the whole Companion report unusable;
- malformed JSONL in the middle of a log still fails fast;
- `--strict-jsonl` restores fail-fast validation for every malformed record;
- the Companion reports duplicate, out-of-order and missing sequence IDs;
- Markdown and JSON summaries are written to a temporary file, flushed to disk, and only
  then atomically replace the prior report;
- regression tests cover truncated-tail recovery, strict parsing, sequence integrity and
  atomic output replacement.
- watchdog shutdown now keeps its synchronization handles alive when the watchdog is still finishing a hang dump, preventing an invalid-handle spin during process teardown.

This deliberately does not weaken the in-engine crash recorder. The next engine-side work
should focus on bounded I/O and shutdown/watchdog race hardening only after the Companion
changes pass their test workflow.

## Engine-side v5 changes

- ordinary durable events are still written immediately, but physical disk flushes are
  throttled to a one-second cadence;
- crash/assert/stall/recovery/fatal/watchdog events and failed operations force immediate
  durability;
- repeated flush failures use attempt-based backoff so a disk problem cannot make the
  recorder hammer `FlushFileBuffers()` on every event;
- first-chance exception batches force one durability flush per drained batch rather than
  one physical flush per exception;
- event/checkpoint rings, subsystem state, structured context, counters, timed operations
  and first-chance exception records use commit-marker snapshot validation so the crash
  handler does not consume half-published data;
- timed-operation slot reuse is serialized, preventing an old operation from corrupting a
  newer operation that reused the same fixed-size slot;
- watchdog shutdown keeps synchronization handles alive if a hang dump is still finishing,
  avoiding an invalid-handle loop during teardown.

## Collection and analysis v5 changes

- the evidence collector treats individual copy failures as recoverable and records them in
  `collection_errors.txt` instead of aborting the entire bundle;
- bundle compression writes to a partial ZIP and only moves it into place after successful
  completion;
- temporary collector state is always cleaned up;
- the collector uses the platform temp-directory API rather than assuming `TEMP`;
- CI syntax-checks and executes the collector against a synthetic game directory, then
  expands the ZIP and verifies its manifest and core evidence;
- the Companion streams JSONL input instead of holding a second full raw-text copy in
  memory;
- a crash-truncated final JSONL record is recoverable while malformed records in the middle
  still fail;
- report files use process-unique temporary files, fsync, and atomic replacement.

## Design rules

1. Do not write every frame or AI decision to disk.
2. Prefer checkpoints in hot code and events at boundaries.
3. Put long-lived diagnostic facts in structured context.
4. Wrap complicated operations so crashes reveal what was still active.
5. The crash handler must not wait on the normal recorder lock.
6. The watchdog uses an independent output path so a hung main thread cannot block hang evidence.
