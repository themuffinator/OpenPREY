<div align="center">

<img src="assets/img/banner.png" alt="OpenPrey banner">

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Version](https://img.shields.io/badge/version-0.0.1-green.svg)](https://github.com/themuffinator/OpenPrey)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](https://github.com/themuffinator/OpenPrey)
[![Architecture](https://img.shields.io/badge/arch-x64-orange.svg)](https://github.com/themuffinator/OpenPrey)
[![Build System](https://img.shields.io/badge/build-Meson%20%2B%20Ninja-yellow.svg)](https://mesonbuild.com/)

**A modern, open-source engine and game-code replacement for Prey (2006)**

[Quick Start](#quick-start) • [Build](#building-from-source) • [Documentation](#documentation) • [TODO](TODO.md) • [Credits](#credits)

</div>

---

## About

**OpenPrey** is a minimal adaptation of the OpenQ4 codebase for **Prey (2006)**.

Project focus:
- Keep behavior compatible with stock Prey assets.
- Provide open engine and game binaries (`OpenPrey-client`, `OpenPrey-ded`) with Prey-compatible game-module loading.
- Maintain a clean companion-repo workflow for SDK-derived game code.
- Modernize build/platform layers with Meson + SDL3 while preserving idTech 4-era game behavior.

## What You Need

To run OpenPrey, you need:
- A legitimate copy of Prey (2006).
- OpenPrey binaries built from this repository.
- A modern 64-bit OS (Windows is the current primary target).

OpenPrey does not include commercial game assets.

### Install Auto-Detection

OpenPrey auto-detects `fs_basepath` in this order:
- explicit `fs_basepath` override (if valid)
- current working directory
- Windows registry install entries (including CD-era install roots, App Paths, and uninstall metadata)
- known legacy install directories (`Human Head Studios/Prey`, `2K Games/Prey`, `Games/Prey`, etc.)

This intentionally avoids Steam/GOG-only assumptions for Prey (2006).

---

## Quick Start

### Clone

```bash
git clone https://github.com/themuffinator/OpenPrey.git
cd OpenPrey
```

### Build (Windows)

```powershell
# Configure
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup --wipe builddir . --backend ninja --buildtype debug --wrap-mode=forcefallback

# Compile
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir

# Stage .install package tree
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

### Run

```powershell
.install/OpenPrey-client_x64.exe
```

---

## Building from Source

### Requirements

- Meson (>= 1.2.0)
- Ninja
- MSVC toolchain (Windows; Visual Studio Developer tools)
- C++23-capable compiler mode (`vc++latest`)

### Key Meson Options

```text
-Dbuild_engine=true|false     # Build OpenPrey-client_<arch> and OpenPrey-ded_<arch>
-Dbuild_games=true|false      # Build unified Prey game module
-Denforce_msvc_2026=true      # Optional strict MSVC baseline enforcement
```

### Output Layout

`builddir/`:
- `OpenPrey-client_x64.exe`
- `OpenPrey-ded_x64.exe`
- `openprey/game_x64.dll`
- `openprey/` is the Meson game-module subdir.

Runtime loader behavior:
- Uses unified Prey-style module names (`game_<arch>`, then legacy `gamex86`/`gamex64` aliases).

`.install/`:
- staged runtime package root
- engine executables in `.install/`
- game modules and staged overrides in `.install/openprey/`

---

## Companion GameLibs Repository

OpenPrey uses a companion repo for SDK-derived game-library sources:
- Local default: `../OpenPrey-GameLibs`
- Canonical edit location for game libs: `OpenPrey-GameLibs`
- Local mirror in this repo: `src/game` (synchronized by build tooling)
- Additional Prey gameplay trees are synchronized as needed (`src/Prey`, `src/preyengine`) for compatibility bring-up.

Companion build behavior:
- If `OpenPrey-GameLibs` provides a Meson wrapper, `tools/build/build_gamelibs.ps1` uses it.
- If only legacy project files are present, OpenPrey tooling can run legacy solution builds and stage discovered outputs.

### Environment Variables

Primary variables:
- `OPENPREY_GAMELIBS_REPO=<path>`
- `OPENPREY_SKIP_GAMELIBS_SYNC=1`
- `OPENPREY_BUILD_GAMELIBS=1`
- `OPENPREY_SKIP_GAMELIBS_BUILD=1`

Legacy `OPENQ4_*` variants are still accepted temporarily for migration compatibility.

---

## Documentation

- [Platform Support](docs-dev/platform-support.md)
- [Porting Baseline](docs-dev/porting-baseline.md)
- [GameLibs Compatibility Plan](docs-dev/prey-gamelibs-compatibility-plan.md)
- [Display Settings](docs-user/display-settings.md)
- [Input Key Matrix](docs-dev/input-key-matrix.md)
- [Official PK4 Checksums](docs-dev/official-pk4-checksums.md)
- [Release Completion](docs-dev/release-completion.md)

---

## Current Scope

In scope:
- OpenPrey rebrand and build/runtime workflow migration from OpenQ4.
- Prey-focused compatibility work on engine/game integration.
- Documentation and tooling cleanup for the new project identity.

Out of scope (for this phase):
- Preserving deprecated OpenQ4-only companion-repo workflows.
- Shipping proprietary assets.

---

## Credits

- themuffinator
- Justin Marshall
- Robert Backebans
- id Software
- Raven Software
- Human Head Studios

OpenPrey is an independent project and is not affiliated with or endorsed by Bethesda, ZeniMax, id Software, or Human Head Studios.
