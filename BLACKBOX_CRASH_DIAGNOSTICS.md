# Vengeance Crash Black Box

The Vengeance fork now keeps a lightweight crash flight recorder alongside the existing
exception report system.

## Files produced

- `BlackBox_LastRun.log` — durable milestones from the current run. Important events are
  flushed immediately.
- `BlackBox_PreviousRun.log` — automatically preserved copy of the previous run.
- `Crash Report_DD_MM_YYYY___HH_MM_SS.txt` — existing text crash report, now with the
  black-box checkpoint and recent event journal embedded near the top.
- `Vengeance-Crash-<PID>-<TID>-YYYYMMDD-HHMMSS.dmp` — Windows minidump captured before
  stack walking.

## What is recorded

Always-on durable events currently include:

- engine startup and clean shutdown;
- screen transitions;
- save/load begin, header context, sector restoration and completion/failure;
- map loading phase changes and map metadata.

High-frequency memory-only checkpoints currently include:

- exact map-loader layer, grid, entry, tile type/subindex and map-buffer byte offset;
- current tactical AI soldier, team, grid, AP, life, alert state, action/action-data,
  sector and current tactical team.

This lets a crash report answer questions such as:

```
MAPS\B1.dat
phase=STRUCT
grid=18742
entry=1
type=37
sub=14
offset=182331/483912
```

or:

```
AI
sector=9,1,0
currentTeam=1
soldier=76
grid=12281
AP=54
action=30
data=11982
```

## Crash-ticket procedure

For a reproducible CTD, attach all files that exist from this list:

1. the newest `Crash Report_*.txt`;
2. the matching `Vengeance-Crash-*.dmp`;
3. `BlackBox_LastRun.log`;
4. `BlackBox_PreviousRun.log` if the game has already been restarted.

Do not restart repeatedly before collecting the files; only one previous run is retained.

## Implementation notes

The recorder is intentionally independent of VFS/FileMan and uses Win32 file I/O, so it
can remain available during startup/shutdown and when game file infrastructure is part of
the failure. Durable events use disk flushes. Very frequent checkpoints stay memory-only
and are copied into the crash report by the exception handler.

The crash handler never acquires the black-box critical section while dumping evidence,
to avoid deadlocking if the original fault happened during logging.
