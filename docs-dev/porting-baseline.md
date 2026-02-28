# OpenPrey Porting Baseline

This document defines the baseline assumptions for the OpenQ4 -> OpenPrey migration phase.

## Current Intent

- Keep engine adaptation minimal and incremental.
- Prioritize stock Prey asset compatibility over feature expansion.
- Avoid introducing new external binary dependencies during initial bring-up.
- Favor Doom 3 / Prey behavior when inherited Quake 4 behavior conflicts.

## Companion Repo Workflow

- Canonical game-library source lives in `../OpenPrey-GameLibs`.
- Sync into `src/game` through `tools/build/sync_gamelibs.ps1`.
- Sync Prey-specific companion trees as needed (`src/Prey`, `src/preyengine`, and required shared headers).
- Optional companion build step can be enabled during compile via `OPENPREY_BUILD_GAMELIBS=1`.
- Companion build tooling supports both Meson wrappers and legacy VC-solution layouts.

## Runtime Layout

- Engine binaries: `.install/`
- Game modules and staged overrides: `.install/openprey/`
- Working build artifacts: `builddir/`

## Runtime Module Model

- Prey compatibility expects a unified game module model.
- OpenPrey loader now prefers unified module names (`game_<arch>`, legacy `gamex86`/`gamex64`).
- Split migration modules (`game_sp`, `game_mp`) remain temporary fallback compatibility targets.

## Install Detection Baseline

- `fs_basepath` auto-discovery prioritizes:
  1. Current working directory.
  2. Windows registry install entries (vendor keys, App Paths, uninstall keys).
  3. Known legacy CD-era install directories.
- Steam/GOG-only assumptions are intentionally avoided for Prey (2006).

## Validation Loop

1. Build with `tools/build/meson_setup.ps1`.
2. Stage `.install/` with `meson install -C builddir --no-rebuild --skip-subprojects`.
3. Launch SP or MP using mode-specific configs.
4. Review `logs/openprey.log` under `fs_savepath`.
5. Resolve warnings/errors in code paths before adding content-side workarounds.

## Migration Guardrails

- Keep documentation synchronized with tooling/naming changes.
- Prefer OpenPrey naming in new files, scripts, and output artifacts.
- Keep temporary OpenQ4 compatibility aliases only where needed to avoid breaking active developer environments.
