# openPREY Release Completion List

Use this file as the source list for release changelog entries.

Process:
1. Add completed work under "Ready For Changelog".
2. When cutting a release, move shipped items into release notes.
3. Keep remaining work in "Carry Forward".

## Ready For Changelog

- [x] Repository rebranded from OpenQ4 to openPREY at Meson/project tooling level.
- [x] Canonical companion game sources now stage directly from `OpenPrey-game`; companion copies of engine/SDK headers are excluded.
- [x] Legacy external companion-repo build references removed from active build/docs/tooling paths.
- [x] VS Code tasks/launch settings refreshed for openPREY naming.
- [x] Documentation set refreshed for openPREY migration scope.
- [x] `fs_basepath` auto-discovery switched from Quake 4 Steam/GOG assumptions to Prey CD-era registry + legacy-path discovery.
- [x] Game-module loader/build updated to use a unified Prey module (`game_<arch>`) for both SP and MP paths.
- [x] Root Meson builds and stages one unified `game_<arch>` module from `src/game`, `src/Prey`, and `src/preyengine` in the companion repository.
- [x] Engine Session/Async `idGame` call sites aligned to current Prey game API signatures to unblock engine target compilation.
- [x] Meson build graph updated so `-Dbuild_games=false` no longer compiles game-idlib targets, allowing clean engine-only validation builds.
- [x] Runtime packaging keeps `basepr/game_<arch>` loose and assembles openPREY-owned content as `basepr/pak0.pk4` and `basepr/pak1.pk4`.
- [x] Archive/AppImage tooling is rebranded for openPREY and retains explicit Wayland/X11 runtime boundaries.
- [x] Active validation builds the unified game-module layout on Windows x64 and Linux x64; inherited split-module workflow fixtures are manual-only and unconditionally disabled pending a clean openPREY redesign.

## Carry Forward

- [ ] Validate default launch/map flow for Prey SP and MP in staged `.install/` runs.
- [ ] Finalize classic `pak000..pak004` checksum baseline (consolidated `pak_data/pak_sound/pak_en_*` baseline is now published).
- [ ] Continue reducing inherited Quake 4-specific assumptions in runtime/gameplay paths.
- [ ] Complete gameplay/runtime verification of the staged companion `src/Prey` and `src/preyengine` trees.
- [ ] Complete retail parity checks for the restored Doom 3/Prey particle, FX, and beam paths.
- [ ] Extend CI/runtime checks for openPREY-specific smoke tests.
- [ ] Rebuild and enable manual/nightly/AppImage publication lanes only after unified-module runtime signoff.
