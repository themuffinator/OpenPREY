# OpenPrey GameLibs Compatibility Plan

This document captures the current compatibility research baseline and concrete workstreams required for full Prey (2006) game-library parity.

## Current Findings

- Companion repo (`../OpenPrey-GameLibs`) currently uses legacy project layouts (`src/PREY.sln`, `src/2005game.vcproj`) and does not currently expose a Meson wrapper.
- Legacy game-library outputs are unified-module style (`gamex86.dll`) rather than hard split SP/MP outputs.
- Companion sources include Prey-specific gameplay trees not yet fully integrated in OpenPrey runtime build flow:
  - `src/Prey`
  - `src/preyengine`
  - shared header dependencies such as `src/framework/declPreyBeam.h`
- OpenPrey runtime loader and filesystem still carried inherited Quake 4 assumptions before this pass (module naming and install discovery patterns).

## Observed Compile Blockers (2026-02-20)

The latest compile runs (`tools/build/meson_setup.ps1 compile -C builddir`) confirm:
- Build/script wiring is working.
- Engine-side `idGame` call signatures have been aligned enough to compile engine targets.
- Active blockers are now concentrated in game-library and shared idlib compatibility.

### Baseline Updated In This Pass

- Removed obsolete engine assignment to non-existent `gameImport_t` fields from inherited OpenQ4 integration.
- Updated engine Session/Async call sites to current Prey `idGame` signatures (`SetUserInfo`, `InitFromNewMap`, `InitFromSaveGame`, `RunFrame`, `ClientPrediction`, server-client connect/begin/snapshot paths).
- Updated main-menu command hook usage to rely on supported `idGame` GUI command entry points.
- Updated Meson gating so `-Dbuild_games=false` skips game-idlib compilation and supports engine-only compile validation.

Validation result:
- `tools/build/meson_setup.ps1 compile -C builddir-engine` now succeeds with `-Dbuild_games=false`.
- Full default build (`builddir`) still fails in game-library compatibility layers (blockers below).

Primary blocker groups:

1. Engine-to-game import contract drift
- Example: `gameImport_t` in synced Prey `Game.h` no longer exposes inherited OpenQ4 fields.
- Action: keep engine `gameImport_t` population strictly aligned to companion `Game.h`.

2. Core idlib/container API drift
- Template signature mismatches (`idDynamicBlockAlloc`, `idBlockAlloc`) and pointer-wrapper behavior (`idEntityPtr` APIs).
- Action: restore Prey-compatible template typedefs/helpers or port callers to current signatures.

3. Networking/bit-message API drift
- `idBitMsg` is missing Prey-expected helpers (`WriteBool`, `ReadBool`, `WriteVec3`, `ReadVec3`).
- Action: reintroduce missing helpers or provide equivalent wrappers with matching semantics.

4. Collision/physics type drift
- Missing/renamed collision model types (`cmHandle_t`) and clip-model interfaces expected by Prey gameplay headers.
- Action: restore compatibility typedefs and method signatures in collision/physics interfaces.

5. Renderer/entity-struct field drift
- Prey gameplay expects fields such as `renderEntity_s::weaponDepthHack` not currently present.
- Action: audit and restore required render-entity fields/flags used by gameplay code paths.

6. Prey gameplay tree integration issues
- Multiple syntax/contract errors from synced Prey headers indicate missing prerequisites or incompatible include ordering.
- Action: define and document a minimal include/config baseline for `src/Prey` and `src/preyengine` before full module compile.

7. Effects/particle-system parity gaps
- Current baseline still reflects inherited OpenQ4/BSE-era behavior while Prey expects Doom 3/Prey-era effect paths.
- Action: prioritize Doom 3 particle/Fx path reinstatement and related decl/runtime integration.

## Compatibility Workstreams

1. Game Module Architecture
- Build and load a unified module (`game_<arch>`) for both SP and MP runtime paths.
- Keep lightweight runtime alias compatibility for legacy module names where practical.
- Validate both SP and MP flows against unified module loading in live gameplay.

2. Companion Source Sync Completeness
- Ensure sync covers all game-library trees required by Prey gameplay code, not just `src/game`.
- Track synced companion files explicitly and avoid accidental engine-side overwrites.
- Add guardrails for missing optional trees in partial companion checkouts.

3. Companion Build + Stage Integration
- Support both Meson-based and legacy VC-solution companion build paths.
- Stage discovered companion module outputs into:
  - `builddir/basepr/`
  - `.install/basepr/`
- Normalize legacy module naming aliases (`gamex86.dll` -> `game_x86.dll`) for runtime loader compatibility.

4. Filesystem + Install Discovery
- Keep `fs_basepath` discovery robust for CD-era installs:
  - current working directory
  - registry vendor keys / App Paths / uninstall metadata
  - known legacy install roots
- Continue validating registry key coverage across localized and patched installs.

5. PK4 Validation Baseline
- Capture canonical checksums from clean retail Prey installs.
- Replace temporary presence-only required-pack checks with finalized checksum policy.
- Verify optional patch/localization pack handling does not block valid installs.

6. Engine Compatibility Gaps (High Impact)
- Reinstate Doom 3/Prey particle-system behavior required by shipped assets/effects.
- Port Prey-specific decl/runtime paths (beam/effects-related declarations and parser behavior).
- Audit renderer/material/FX behavior where inherited Quake 4 assumptions conflict with Prey content.

7. Validation + CI
- Add automated smoke scenarios for:
  - module load (unified + split fallback)
  - filesystem install auto-detection
  - baseline map start in SP and MP
- Gate release readiness on in-game validation, not main-menu startup only.

## Immediate Next Milestones

1. Integrate synced `src/Prey` sources into active OpenPrey game-module compilation path.
2. Verify unified-module runtime in SP and MP map start flows.
3. Capture and publish official Prey PK4 checksums from known-good retail installs.
4. Begin particle-system reinstatement and validate stock effects behavior in representative maps.
