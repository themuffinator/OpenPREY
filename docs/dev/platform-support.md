# openPREY platform and architecture roadmap

This document records the current platform policy for the Prey (2006)-focused rebase.
It is a roadmap, not a release-support claim.

## Current baseline

- Version line: `0.0.1`.
- Canonical build system: Meson + Ninja.
- Standard build output: `builddir/`.
- Standard staged runtime: `.install/`.
- Canonical game source: adjacent `OpenPrey-game` checkout, overridable with
  `OPENPREY_GAMELIBS_REPO`.
- Runtime game directory: `basepr/`.
- Module model: one `game_<arch>` module for SP and MP.
- Platform backend direction: SDL3 first; native/legacy backends are diagnostic
  fallbacks where they remain available.

## Support matrix

| Platform | Build status | Runtime status | Current claim |
|---|---|---|---|
| Windows x64 | Active | Deepest local validation target | Primary bring-up target; retail matrix still pending |
| Linux x64 | Active CI GameLib lane and packaging tooling | Retail gameplay validation pending | Build target, not yet a fully signed-off release |
| Windows/Linux ARM64 | Inherited build support exists | Prey runtime evidence pending | Gated |
| macOS arm64/x64/universal2 | Inherited backend/tooling support exists | Prey runtime and package evidence pending | Experimental and gated |

The active workflow is `.github/workflows/openprey-validation.yml`. It validates the
canonical companion staging contract, deterministic `pak0.pk4`/`pak1.pk4` inputs,
one staged `game_<arch>` module, `basepr/`, and openPREY desktop/icon payloads on the
enabled x64 lanes.

Inherited OpenQ4 ARM64, macOS, Universal2, signing, and manual-release workflows are
retained only as manual static-test/reference fixtures. Every job is unconditionally
false-gated because it clones `openQ4-game` and asserts split `game-sp`/`game-mp`
modules. Those gates must not be removed until the lanes are rebuilt around the
openPREY contract and backed by runtime evidence.

## Linux and Steam Deck

SDL3 may use Wayland or X11 according to the available host. Preferred project
environment variables use the `OPENPREY_*` prefix:

- `OPENPREY_FORCE_X11=1`
- `OPENPREY_WAYLAND_DISABLE_LIBDECOR=1`
- `OPENPREY_WAYLAND_PREFER_LIBDECOR=1`
- `OPENPREY_WAYLAND_SYNC_WINDOW_OPS=1`
- `OPENPREY_STEAMDECK=1`

Legacy `OPENQ4_*` names remain temporary migration aliases only. The staged
`openPREY-steamdeck` launcher always forces windowed mode.

## macOS boundary

macOS code and packaging helpers are retained from upstream, but openPREY currently
makes no signed-package or first-class runtime claim. One unified game module must be
used by any future app bundle. OpenGL is the first compatibility target; Vulkan through
MoltenVK and other backend parity remain explicit validation TODOs.

## Validation requirements

First-class support for a platform requires all of the following on real target
hardware:

1. clean configure, compile, and staged install;
2. architecture and runtime-dependency checks for client, dedicated server, renderer
   modules, and unified game module;
3. launch from `.install/` with a legitimate Prey installation;
4. SP map entry, MP listen/dedicated startup, save/load, input, audio, and clean shutdown;
5. logs free of new engine/game/parser/loader errors;
6. renderer validation using the engine `screenshot` command, never an OS capture;
7. package extraction/install and launcher/icon checks;
8. evidence tied to exact engine and `OpenPrey-game` commits.

All agent-driven runtime validation must force `+set r_fullscreen 0`.

Current outstanding evidence is tracked in
[the rebase status ledger](prey-rebase/status-ledger.md#explicit-deferral-register).
