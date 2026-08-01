# openPREY Porting Baseline

This document defines the baseline assumptions for the OpenQ4 -> openPREY migration phase.

## Current Intent

- Keep engine adaptation minimal and incremental.
- Prioritize stock Prey asset compatibility over feature expansion.
- Avoid introducing new external binary dependencies during initial bring-up.
- Favor Doom 3 / Prey behavior when inherited Quake 4 behavior conflicts.

## Canonical GameLib Workflow

- Canonical game-library source lives in `E:\Repositories\OpenPrey-game` by default.
- `OPENPREY_GAMELIBS_REPO=<path>` overrides that location.
- Meson stages the companion sources directly at configure time.
- openPREY does not maintain an in-repository `src/game` mirror.
- Legacy `OPENQ4_*` variables are accepted only as temporary migration aliases.

## Runtime Layout

- Repo-authored runtime content: `content/basepr/pak0/` and `content/basepr/pak1/`
- Development-only fixtures: `content/basepr/dev/`
- Engine binaries: `.install/`
- Game modules and staged overrides: `.install/basepr/`
- Working build artifacts: `builddir/`
- Local save/config/log state for repo-based validation runs: `.home/`
- Temporary task artifacts: `.tmp/`

## Runtime Module Model

- Prey compatibility expects a unified game module model.
- openPREY loader now prefers unified module names (`game_<arch>`, legacy `gamex86`/`gamex64`).
- Split `game_sp`/`game_mp` outputs are not built or packaged; legacy names may be loader aliases only.

## Install Detection Baseline

- `fs_basepath` auto-discovery prioritizes:
  1. Current working directory.
  2. Windows registry install entries (vendor keys, App Paths, uninstall keys).
  3. Known legacy CD-era install directories.
- Steam/GOG-only assumptions are intentionally avoided for Prey (2006).

## Validation Loop

1. Build with `tools/build/meson_setup.ps1`.
2. Stage `.install/` with `meson install -C builddir --no-rebuild --skip-subprojects`.
3. Launch SP or MP from `.install/` in windowed mode with a repo-local save path (for example `+set r_fullscreen 0 +set fs_savepath ..\.home`).
4. Review `logs/openprey.log` under `fs_savepath`.
5. Resolve warnings/errors in code paths before adding content-side workarounds.

## Migration Guardrails

- Keep documentation synchronized with tooling/naming changes.
- Prefer openPREY naming in new files, scripts, and output artifacts.
- Keep temporary OpenQ4 compatibility aliases only where needed to avoid breaking active developer environments.
