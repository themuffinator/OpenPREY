# openPREY rebase evidence bundle

UPSTREAM_BASE: d41186e4 (2026-07-31)

This directory is the durable audit trail for the 2026 openPREY-on-OpenQ4 rebase.

- [plan.md](plan.md) records the strategy and decisions.
- [implementation.md](implementation.md) records the ordered work packages.
- [status-ledger.md](status-ledger.md) is the current disposition and validation ledger
  for all 137 catalog IDs.
- `catalog-*.md` and `report-*.md` are preserved pre-rebase evidence snapshots. Their
  old `basepy`, `docs/dev`, `src/game`, and OpenQ4 paths describe the source trees that
  were audited; they are not current workflow guidance.
- [porting-baseline.md](porting-baseline.md), [input-key-matrix.md](input-key-matrix.md),
  and [official-pk4-checksums.md](official-pk4-checksums.md) carry the Prey-specific
  validation baseline forward.

Current structural decisions are authoritative: `content/basepr/{pak0,pak1}` is the
repo-authored runtime content source, `.install/basepr/` is the staged runtime game
directory, `E:\Repositories\OpenPrey-game` is the canonical game source staged directly
at configure time, and one `game_<arch>` module serves both SP and MP. Legacy OpenQ4
environment-variable names are migration aliases only.
