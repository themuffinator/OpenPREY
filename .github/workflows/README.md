# openPREY workflows

`openprey-validation.yml` is the active build and validation workflow. It checks the
canonical `OpenPrey-game` source staging contract, the unified `game_<arch>` module,
the `basepr/` runtime layout, and openPREY desktop/icon payloads.

The inherited OpenQ4 commit, push, ARM64, macOS, Universal2, and manual-release
workflows are retained only as static-test/reference fixtures. They are manual-only,
clearly named `DISABLED`, and every job has an unconditional false gate because they
clone `openQ4-game` and require split `game-sp`/`game-mp` modules. Remove those gates
only after rebuilding source pinning, package assertions, signing, and runtime smoke
tests around `OpenPrey-game`, `game_<arch>`, and `basepr/`.

`discord-release.yml` only reacts to an already-published release. It does not build or
publish artifacts.
