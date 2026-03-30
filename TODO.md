# OpenPrey TODO

This file tracks current known issues and upcoming tasks for the OpenPrey migration.

## Known Issues

- [ ] Validate startup/module boot sequence against stock Prey assets and remove remaining startup warnings.
- [ ] Audit SP/MP launch defaults and map bootstrap commands for Prey behavior under unified game-module loading.
- [ ] Confirm save/config path behavior after OpenPrey rebrand changes.
- [ ] Confirm `fs_basepath` auto-detection across real CD-era install layouts and registry variants.

## Near-Term Tasks

- [ ] Continue OpenQ4 -> OpenPrey naming cleanup in user-facing strings and packaging metadata.
- [ ] Document Prey PK4 checksum baseline once validation data is captured.
- [ ] Expand runtime validation matrix (SP, MP, dedicated server).
- [ ] Verify companion `OpenPrey-GameLibs` sync/build workflow (Meson + legacy VC layout) in CI and local dev loops.
- [ ] Integrate Prey gameplay trees from companion repo into active OpenPrey game-module compilation.
- [ ] Resolve remaining game-library compile blockers after engine `idGame` API alignment (idlib allocators, `idBitMsg` helpers, collision handles, render entity fields, entity-pointer APIs).

## Longer-Term

- [ ] Reduce inherited Quake 4 assumptions in gameplay/asset compatibility paths where they conflict with Prey behavior.
- [ ] Reinstate Doom 3/Prey particle-system behavior and validate effects parity.
- [ ] Remove any remaining legacy game-directory naming from docs/scripts and keep `basepr` as the sole namespace.
