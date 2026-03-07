<div align="center">

<img src="assets/img/banner.png" alt="OpenPrey banner">

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Version](https://img.shields.io/badge/version-0.0.1-green.svg)](https://github.com/themuffinator/OpenPrey)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey.svg)](https://github.com/themuffinator/OpenPrey)
[![Architecture](https://img.shields.io/badge/arch-x64-orange.svg)](https://github.com/themuffinator/OpenPrey)
[![Build System](https://img.shields.io/badge/build-Meson%20%2B%20Ninja-yellow.svg)](https://mesonbuild.com/)

**A modern, open-source engine and game-code replacement for Prey (2006)**

[Features](#features) | [Compatibility](#compatibility-status) | [Quick Start](#quick-start) | [Building](#building-from-source) | [Documentation](#documentation) | [TODO](TODO.md) | [Credits](#credits)

</div>

---

> [!WARNING]
> **Development Status:** OpenPrey is in active migration from OpenQ4. Windows x64 is the current validated host, and full gameplay/runtime parity with stock Prey assets is still being established.

---

## About

The **OpenPrey Project** is a minimal, Prey-focused adaptation of the current OpenQ4 codebase. The project keeps the modernization work that makes current builds practical to develop and debug, but retargets the engine, tooling, and game-library workflow around **Prey (2006)** and its unified game-module model.

### What You Need

To use OpenPrey, you need:
- A legitimate copy of Prey (2006)
- OpenPrey engine binaries from this repository
- A modern 64-bit Windows system (Windows x64 is the current actively validated platform)

---

> [!NOTE]
> OpenPrey does **not** include commercial Prey assets. On Windows, `fs_basepath` auto-discovery checks a valid override first, then the current working directory, registry install entries (including App Paths and uninstall metadata), and finally known legacy install roots such as `Human Head Studios/Prey`, `2K Games/Prey`, and `Games/Prey`.

---

## Features

### Core Features
- **Prey-first Runtime Layout**: Unified `openprey/` staging for engine overlays, game modules, GUI scripts, shaders, maps, and strings
- **Unified Game Module Model**: Engine prefers `game_<arch>` for both SP and MP, with legacy `gamex86`/`gamex64` aliases still accepted during migration
- **Legacy Install Discovery**: Windows install detection supports CD-era registry keys, App Paths, uninstall entries, and known install roots instead of assuming Steam/GOG-only layouts
- **Official Asset Validation**: Startup validation checks the required official Prey base PK4 layout before the game continues

### Modernization And Tooling
- **SDL3-first Windows Backend**: SDL3 is the default window/input/display backend; the legacy Win32 backend remains transitional
- **Meson + Ninja Build System**: Canonical configure/build/install path with repo-local staging under `.install/`
- **Debug-Oriented Local Workflow**: `.install/` for staged runtime files, `.home/` for configs/logs/saves, `.tmp/` for task artifacts
- **Crash Diagnostics**: Windows debug builds write crash logs and minidumps into `crashes/` beside the executable

### Runtime And Display Improvements
- **Modern Window Modes**: Windowed, borderless, desktop-native fullscreen, and exclusive fullscreen behavior are all supported
- **Monitor Selection**: SDL3 builds expose `r_screen`, `listDisplays`, and `listDisplayModes` for multi-monitor setup
- **Staged Validation Launches**: Repo-local launch workflows keep logs in `.home/logs/openprey.log` and avoid writing back into the staged runtime tree

---

## Compatibility Status

This status focuses on compatibility with official Prey assets and the OpenPrey game-library pipeline, not binary interchangeability with proprietary retail DLLs.

### Landed
- **Project Rebrand Completed**: Meson project metadata, staged binaries, VS Code launch settings, and documentation now use OpenPrey naming
- **Prey Install Discovery Path**: `fs_basepath` auto-detection now targets Prey registry/app-path/uninstall metadata and legacy install roots
- **Unified Game Module Loader**: OpenPrey builds and stages a unified `game_<arch>` module under `openprey/`
- **Companion Repo Tooling**: Sync/build tooling now targets `OpenPrey-GameLibs` and supports both Meson-wrapper and legacy VC-solution layouts
- **Official PK4 Layout Validation**: Engine startup rejects missing or modified required base-pack layouts when `fs_validateOfficialPaks 1` is enabled

### In Progress
- **Stock-asset SP/MP Smoke Validation**: Default staged launch flow is still being verified across single-player and multiplayer startup cases
- **Prey Gameplay Bring-up**: Active `src/Prey` game-library integration and runtime verification continue during the migration
- **Particle And FX Compatibility**: Doom 3 / Prey-era particle behavior and dependent runtime paths still need restoration and parity checks

### Not Yet Claimed
- **Full Campaign Completion**: The project does not yet claim end-to-end single-player completion against stock assets
- **Multiplayer Parity**: Multiplayer compatibility remains under active validation
- **Non-Windows Host Support**: Linux and macOS remain roadmap items; current Meson host validation is Windows-first

Current follow-up work is tracked in [TODO.md](TODO.md), [docs-dev/release-completion.md](docs-dev/release-completion.md), and [docs-dev/prey-gamelibs-compatibility-plan.md](docs-dev/prey-gamelibs-compatibility-plan.md).

---

## Quick Start

### Prerequisites
- **Prey (2006)** installed from original media or another legitimate distribution
- **Windows x64**
- **Build tools**: [Meson](https://mesonbuild.com/), [Ninja](https://ninja-build.org/), and Python 3
- **MSVC toolchain**: Visual Studio 2026+ recommended (MSVC 19.46+)

> [!NOTE]
> `tools/build/meson_setup.ps1` automatically syncs `../OpenPrey-GameLibs` on `setup`, `compile`, and `install`. Clone the companion repo alongside OpenPrey, or set `OPENPREY_SKIP_GAMELIBS_SYNC=1` if you intentionally want to build only from the in-repo mirror.

### Installation

1. **Clone the repositories**
   ```bash
   git clone https://github.com/themuffinator/OpenPrey.git
   git clone https://github.com/themuffinator/OpenPrey-GameLibs.git
   cd OpenPrey
   ```

2. **Build and stage OpenPrey**

   **Windows (PowerShell)**
   ```powershell
   # Setup the build
   powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback

   # Compile
   powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir

   # Stage the local runtime package
   powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
   ```

3. **Run the staged build**

   ```powershell
   Set-Location .install
   .\OpenPrey-client_x64.exe +set fs_game openprey +set fs_savepath ..\.home +set logFile 2 +set logFileName logs/openprey.log +set r_fullscreen 0
   ```

For a short single-player smoke test, append `+set si_gameType singleplayer +map game/roadhouse_quick`.

---

## Building from Source

<details>
<summary><b>Detailed Build Instructions</b></summary>

### Requirements
- **Meson** 1.2.0 or newer
- **Ninja** build system
- **Python 3**
- **C++23-capable MSVC toolchain**
  - **Windows**: Visual Studio 2026 / MSVC 19.46+ recommended

### Build Options
```text
-Dbuild_engine=true|false         # Build OpenPrey-client_<arch> and OpenPrey-ded_<arch>
-Dbuild_games=true|false          # Build the unified game module from the synchronized mirror
-Dplatform_backend=sdl3|legacy_win32
-Duse_pch=true|false
-Denforce_msvc_2026=true          # Fail configure if MSVC is older than 19.46+
```

### Build Commands

**Windows (PowerShell)**
```powershell
# Configure
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup builddir . --backend ninja --buildtype=release

# Build
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir

# Stage the runtime tree used for local validation
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

**From a Developer Command Prompt**
```batch
tools\build\openprey_devcmd.cmd
meson setup builddir . --backend ninja --buildtype=release
meson compile -C builddir
meson install -C builddir --no-rebuild --skip-subprojects
```

### Output Files

**Build directory** (`builddir/`):
- `OpenPrey-client_x64.exe` - main engine executable
- `OpenPrey-ded_x64.exe` - dedicated server executable
- `openprey/game_x64.dll` - unified game module
- import libraries and PDBs may be present in local debug builds

**Staging directory** (`.install/`):
- `OpenPrey-client_x64.exe`
- `OpenPrey-ded_x64.exe`
- `OpenAL32.dll` when a bundled Windows runtime is available
- `.install/openprey/game_x64.dll`
- `.install/openprey/{glprogs,guis,maps,materials,script,strings}/`

`meson install` creates the local runtime tree used for testing and packaging input. Release packaging strips build-only artifacts such as `.pdb`, `.lib`, `.exp`, and `.ilk`.

</details>

---

## Game Directory Structure

OpenPrey uses a unified runtime directory layout under `openprey/`:

```text
.install/
|-- OpenAL32.dll                  # optional bundled runtime on Windows
|-- OpenPrey-client_x64.exe
|-- OpenPrey-ded_x64.exe
\-- openprey/
    |-- game_x64.dll
    |-- glprogs/
    |-- guis/
    |-- maps/
    |-- materials/
    |-- script/
    \-- strings/
```

The engine uses the unified module model for both game modes:
- **Single-player**: loads `game_<arch>` from `openprey/`
- **Multiplayer**: loads the same unified `game_<arch>` module
- **Legacy compatibility**: `gamex86` / `gamex64` aliases are still accepted during migration

---

## SDK And Game Library

OpenPrey keeps SDK-derived game code in the companion [OpenPrey-GameLibs](https://github.com/themuffinator/OpenPrey-GameLibs) repository. Canonical edits for SDK/game-library work belong there first; `src/game` in this repo is a synchronized mirror used by the Meson build, with additional `src/Prey` and `src/preyengine` sync paths brought in as needed for compatibility work.

### Companion Workflow
- Default companion repo location: `../OpenPrey-GameLibs`
- `tools/build/meson_setup.ps1` syncs the companion repo before `setup`, `compile`, and `install`
- `OPENPREY_BUILD_GAMELIBS=1` triggers an additional companion build-and-stage pass during `compile`
- `tools/build/build_gamelibs.ps1` can consume either a Meson wrapper or the current legacy `src/PREY.sln` layout
- Legacy `OPENQ4_*` environment variable names are still accepted as temporary migration aliases

### Prey SDK License
The companion repo contains code derived from the Prey Software Development Kit and is subject to the original Human Head Studios SDK license.

The bundled SDK EULA allows non-commercial Prey modifications that run only with a legitimate copy of Prey, but it prohibits commercial exploitation and standalone redistribution of the SDK-derived code. See `EULA.Development Kit.rtf` in the OpenPrey-GameLibs repository for the full terms.

---

## Project Goals

### Primary Objectives
- Deliver a minimal, clean OpenQ4-to-Prey adaptation
- Preserve behavior expected by shipped Prey assets where practical
- Keep single-player and multiplayer behavior aligned with Prey's unified game-library model
- Prefer Doom 3 / Prey lineage behavior over inherited Quake 4 behavior where Prey depends on it
- Keep runtime staging and packaging centered on a single `openprey/` directory

### Non-Goals
- Shipping proprietary Prey assets
- Reintroducing the old OpenQ4 closed-source companion-repo workflow
- Assuming Steam/GOG-only install layouts for Prey (2006)
- Fixing compatibility by shipping content hacks instead of repairing engine/game code

---

## Documentation

- [Platform Support](docs-dev/platform-support.md) - Windows-first platform roadmap and SDL3 direction
- [Porting Baseline](docs-dev/porting-baseline.md) - current OpenQ4 -> OpenPrey migration assumptions
- [GameLibs Compatibility Plan](docs-dev/prey-gamelibs-compatibility-plan.md) - companion repo integration and game-library bring-up status
- [Display Settings](docs-user/display-settings.md) - fullscreen, borderless, monitor selection, and windowed sizing
- [Input Key Matrix](docs-dev/input-key-matrix.md) - keyboard and input reference
- [Official PK4 Checksums](docs-dev/official-pk4-checksums.md) - required Prey base pack layouts and published checksums
- [Release Completion](docs-dev/release-completion.md) - shipped work and carry-forward items
- [Project TODO](TODO.md) - known issues and near-term tasks

---

## Asset Validation

OpenPrey validates the official Prey base-pack layout at startup when `fs_validateOfficialPaks 1` is enabled (default).

**How it works:**
1. Engine scans the detected `base/` pack set
2. Chooses the required layout to validate against:
   - classic retail naming (`pak000.pk4` ... `pak004.pk4`)
   - consolidated retail naming (`pak_data.pk4`, `pak_sound.pk4`, `pak_en_v.pk4`, `pak_en_t.pk4`)
3. Refuses to continue if the required packs are missing or modified

**Current checksum status:**
- Consolidated retail pack checksums are published in [official-pk4-checksums.md](docs-dev/official-pk4-checksums.md)
- Classic retail pack names are enforced by presence, while canonical checksum capture is still being finalized

---

## Advanced Configuration

<details>
<summary><b>Display And Graphics Settings</b></summary>

### Monitor Selection
- `r_screen -1` - auto-select current display (default)
- `r_screen 0..N` - force a specific monitor on SDL3 builds
- `listDisplays` - list monitor indices
- `listDisplayModes [displayIndex]` - list exclusive fullscreen modes for a display

### Display Modes
- `r_fullscreen 0|1` - windowed vs fullscreen
- `r_fullscreenDesktop 1` - desktop-native fullscreen (default)
- `r_fullscreenDesktop 0` - exclusive fullscreen using `r_mode`/`r_custom*`
- `r_borderless 1` - borderless window when `r_fullscreen 0`

### Windowed Sizing
- `r_windowWidth` / `r_windowHeight` - window size when running windowed
- Validation workflows should force `+set r_fullscreen 0`

</details>

<details>
<summary><b>File System And Validation Settings</b></summary>

### Path Variables
- `fs_basepath` - detected Prey install root
- `fs_homepath` - writable user path
- `fs_savepath` - save/config/log path (defaults to `fs_homepath`)
- `fs_game` - active game directory (`openprey`)
- `fs_devpath` - optional developer override path used by local debug workflows

### Validation
- `fs_validateOfficialPaks 1` - verify required official base packs at startup
- Launching from `.install/` keeps the staged runtime overlay on the search path while letting `.home/` hold writable state

</details>

---

## Dependencies

OpenPrey resolves engine dependencies through Meson subprojects and bundled Windows runtime assets:

| Library | Version | Purpose |
|---------|---------|---------|
| [SDL3](https://www.libsdl.org/) | 3.4.0 | Windowing, input, display management |
| [GLEW](http://glew.sourceforge.net/) | 2.3.4 | OpenGL extension loading |
| [OpenAL Soft](https://openal-soft.org/) | bundled Windows package | 3D audio runtime |
| [stb_vorbis](https://github.com/nothings/stb) | 1.22 | Ogg Vorbis decoding |

---

## Debugging And Development

### Local Validation Loop
- Launch from `.install/` in windowed mode with `+set r_fullscreen 0`
- Use `+set fs_savepath ..\.home` and `+set logFileName logs/openprey.log` to keep logs local to the repo
- Inspect `.home\logs\openprey.log` after each short run
- Fix warnings/errors in engine/game/parser/loader code before resorting to content-side workarounds

### Build Automation
- `tools/build/meson_setup.ps1` auto-detects and initializes the Visual Studio developer environment when needed
- `compile -C builddir` auto-runs `setup --wipe` if the build directory is missing or invalid
- `install` auto-adds `--skip-subprojects` when omitted
- `tools/build/openprey_devcmd.cmd` is available if you want a reusable MSVC developer shell

### Companion Repo Integration
- Use `OPENPREY_GAMELIBS_REPO=<path>` to override the companion repo location
- Use `OPENPREY_SKIP_GAMELIBS_SYNC=1` to suppress automatic mirror sync
- Use `OPENPREY_BUILD_GAMELIBS=1` to build companion game libraries during `compile`
- Use `OPENPREY_SKIP_GAMELIBS_BUILD=1` to skip that additional companion build step

---

## Contributing

OpenPrey is open to contributions, but the project has a few hard constraints:

- Keep compatibility work centered on stock Prey assets
- Edit SDK-derived sources in `OpenPrey-GameLibs` first; treat `src/game` here as a synchronized mirror
- Preserve the unified `openprey/` runtime layout
- Keep documentation updated when workflow, naming, or repository structure changes
- Prefer engine/game fixes over shipping replacement assets or content hacks

---

## License

<small>
<p>OpenPrey is licensed under the <a href="https://www.gnu.org/licenses/gpl-3.0">GNU General Public License v3.0</a> (GPLv3).</p>
<p>The GPLv3 license applies to OpenPrey's engine code in this repository.</p>
<p>SDK-derived game-library code in <a href="https://github.com/themuffinator/OpenPrey-GameLibs">OpenPrey-GameLibs</a> remains subject to the original Prey SDK license.</p>
<p>Prey game assets are proprietary and are not distributed with OpenPrey.</p>
</small>

---

## Credits

OpenPrey builds on work from multiple projects and teams:

### Core Contributors
- **themuffinator** - OpenPrey development and maintenance
- **Justin Marshall** - Quake4Doom baseline and related reverse-engineering work
- **Robert Backebans** - RBDOOM-3-BFG modernization work that informs this ecosystem

### Original Developers
- **id Software** - id Tech 4 lineage
- **Raven Software** - Quake 4 codebase lineage used by OpenQ4
- **Human Head Studios** - Prey (2006) and the Prey SDK

### Third-Party Libraries
- **Sean Barrett** - stb_vorbis
- **GLEW contributors**
- **OpenAL Soft contributors**
- **SDL contributors**

---

## Links

- [Repository](https://github.com/themuffinator/OpenPrey)
- [Game Library](https://github.com/themuffinator/OpenPrey-GameLibs)
- [Issue Tracker](https://github.com/themuffinator/OpenPrey/issues)

---

## Disclaimer

<small>
<p>OpenPrey is an independent project and is not affiliated with, endorsed by, or sponsored by Human Head Studios, 2K, Bethesda, ZeniMax, id Software, or Raven Software.</p>
<p>You must own a legitimate copy of Prey (2006) to use this software. OpenPrey does not include any copyrighted game assets.</p>
</small>

---

### Use At Your Own Risk

<small>
<p><strong>THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.</strong> OpenPrey is experimental software under active development. Use it at your own risk.</p>
</small>
