# Savegame Reliability

This document describes openPREY's save/load compatibility checks, the upstream
failure recorded in [OpenQ4 issue #84](https://github.com/themuffinator/openQ4/issues/84),
and the defensive checks expected when the save format changes.

## Upstream issue #84

The reported `idMoveState::Restore: invalid path length` values were not valid path
lengths and did not, by themselves, prove that the restore cursor had drifted. In
the affected GameLib version, `idMoveState` did not initialize `pathLen`,
`pathArea`, `pathTime`, or its path entries before the first save. A fresh save
could therefore serialize an indeterminate `pathLen`, and the restore-side range
check correctly rejected it.

The Q4-specific move-state implementation from that report is not part of Prey's
unified game module. openPREY retains the broadly applicable outcome: serialized
counts, indices, strings, object references, script state, animation state, and
articulated-physics state are range-checked before they can allocate or index
runtime storage. Saves produced by incompatible builds are not repaired in place.

## Save Pipeline

The engine owns the session header, screenshot and description sidecars, staged
file commit, menu discovery, and the transition into a loaded map. The single
unified Prey GameLib owns the positional gameplay payload for both SP and MP.

New gameplay payloads contain:

- a format marker and version;
- the engine build number;
- a generated GameLib source-snapshot stamp and source-file count;
- class-boundary sync markers;
- a footer containing the final payload offset and object/script bookkeeping
  counts.

Save files are written to temporary paths and validated before the old save is
backed up and the new group is committed. Loading performs the same payload
preflight before `ExecuteMapChange`, so a truncated, stale, or source-incompatible
payload cannot destroy the currently running map merely to report a compatibility
error later.

Legacy, unstamped payloads are accepted only when their build number exactly
matches the running engine. Stamped payloads must match the format version, build,
source stamp, source-file count, and footer structure. The source generator
normalizes line endings before hashing, so equivalent Windows and Unix checkouts
produce the same compatibility stamp.

## Additional Hardening

- Sound-world serialization now checks every write and validates all serialized
  counts, strings, emitters, channels, shaders, and sound classes. Restore creates
  emitters at their serialized indices instead of passing through the allocator's
  reusable-emitter path, which could alias two saved indices. Trailing reusable
  empty emitters are omitted from new saves.
- Game object references use a pointer hash index during save construction and
  lookup. This removes repeated linear scans from object discovery and reference
  serialization without changing the on-disk indices.
- Gameplay restore code retains explicit bounds checks for variable-sized data and
  class sync markers for diagnosing the first mismatched object boundary.

## Compatibility and Residual Risk

The gameplay payload remains a positional binary format. Sync markers and the
footer make structural failures easier to reject and diagnose, but they are not a
cryptographic whole-file integrity check. Source- or build-incompatible saves are
intentionally rejected rather than guessed through; compatibility across arbitrary
revisions is not promised.

Menu discovery validates the session header and aggregates saves from the active
game directory, `basepr`, and the retail `base` directory. A save can therefore
remain visible even when its gameplay payload is too old to load; selecting it
produces a precise warning while the current map continues running.

## Validation

The regression contract is exercised with:

```text
python tools/tests/savegame_corruption_contract.py
```

Runtime validation should load the unified `game_<arch>` module, enter a real Prey
map such as `game/roadhouse`, save, and load the newly written save. Confirm
`Game Map Init SaveGame` without restore errors or sound assertions. A deliberately
stale save should be rejected during engine preflight before game-map initialization.
