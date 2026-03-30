# OpenPrey Release Completion List

Use this file as the source list for release changelog entries.

Process:
1. Add completed work under "Ready For Changelog".
2. When cutting a release, move shipped items into release notes.
3. Keep remaining work in "Carry Forward".

## Ready For Changelog

- [x] Repository rebranded from OpenQ4 to OpenPrey at Meson/project tooling level.
- [x] Companion game-library repo wiring switched to `OpenPrey-GameLibs` defaults.
- [x] Legacy external companion-repo build references removed from active build/docs/tooling paths.
- [x] VS Code tasks/launch settings refreshed for OpenPrey naming.
- [x] Documentation set refreshed for OpenPrey migration scope.
- [x] `fs_basepath` auto-discovery switched from Quake 4 Steam/GOG assumptions to Prey CD-era registry + legacy-path discovery.
- [x] Game-module loader/build updated to use a unified Prey module (`game_<arch>`) for both SP and MP paths.
- [x] Companion game-library tooling updated to support both Meson and legacy project layouts, with staged module output copying.
- [x] Engine Session/Async `idGame` call sites aligned to current Prey game API signatures to unblock engine target compilation.
- [x] Meson build graph updated so `-Dbuild_games=false` no longer compiles game-idlib targets, allowing clean engine-only validation builds.
- [x] Nightly GitHub Actions packaging now keeps `basepr/game_<arch>` loose, writes staged overlay content to `basepr/pak0.pk4`, and publishes a versioned nightly release with generated notes.
- [x] Meson source selection, dependency wiring, and nightly CI now build/package Windows, Linux, and macOS hosts from the same OpenPrey staging layout.

## Carry Forward

- [ ] Validate default launch/map flow for Prey SP and MP in staged `.install/` runs.
- [ ] Finalize classic `pak000..pak004` checksum baseline (consolidated `pak_data/pak_sound/pak_en_*` baseline is now published).
- [ ] Continue reducing inherited Quake 4-specific assumptions in runtime/gameplay paths.
- [ ] Port and verify Prey-specific gameplay trees (`src/Prey`) in active OpenPrey game-module builds.
- [ ] Reinstate Doom 3/Prey particle-system behavior and dependent decl/runtime paths.
- [ ] Extend CI/runtime checks for OpenPrey-specific smoke tests.
