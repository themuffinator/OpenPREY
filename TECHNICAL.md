# OpenPrey Technical Reference

This document covers technical details for advanced users and developers: compatibility status, file layout, configuration cvars, asset validation, build dependencies, versioning, and the SDK/game library structure.

For installation and a feature overview, see the [README](README.md). For building from source, see [BUILDING.md](BUILDING.md).

---

## Table of Contents

- [Prey Compatibility Status](#prey-compatibility-status)
- [Game Directory Structure](#game-directory-structure)
- [Asset Validation](#asset-validation)
- [Advanced Configuration](#advanced-configuration)
- [SDK and Game Library](#sdk-and-game-library)
- [Dependencies](#dependencies)
- [Versioning](#versioning)

---

## Prey Compatibility Status

This status reflects compatibility with official Prey (2006) assets, not binary interchangeability with proprietary retail DLLs.

### Landed

- ✅ **Project Rebrand Completed** — Meson project metadata, staged binaries, VS Code launch settings, and documentation use OpenPrey naming throughout
- ✅ **Prey Install Discovery** — `fs_basepath` auto-detection targets Prey registry/App Paths/uninstall metadata and known legacy install roots; Steam/GOG assumptions removed
- ✅ **Unified Game Module Loader** — Engine builds and stages a unified `game_<arch>` module under `openprey/` for both SP and MP paths
- ✅ **Companion Repo Tooling** — Sync/build tooling targets `OpenPrey-GameLibs` and supports both Meson-wrapper and legacy VC-solution layouts
- ✅ **Official PK4 Layout Validation** — Engine startup rejects missing or modified required base-pack layouts when `fs_validateOfficialPaks 1` is enabled
- ✅ **Cross-host Meson Support** — Source selection, dependency wiring, and nightly packaging cover Windows, Linux, and macOS hosts

### In Progress

- ❌ **Stock-asset SP/MP Smoke Validation** — Default staged launch flow is still being verified across single-player and multiplayer startup cases
- ❌ **Prey Gameplay Bring-up** — Active `src/Prey` game-library integration and runtime verification continue during the migration
- ❌ **Particle and FX Compatibility** — Doom 3 / Prey-era particle behavior and dependent runtime paths still need restoration and parity checks
- ❌ **Cross-host Runtime Validation** — Windows remains the deepest runtime-validation path while Linux/macOS map validation is extended

### Not Yet Claimed

- **Full Campaign Completion** — The project does not yet claim end-to-end single-player completion against stock assets
- **Multiplayer Parity** — Multiplayer compatibility remains under active validation

Current follow-up work is tracked in [TODO.md](TODO.md) and [docs-dev/release-completion.md](docs-dev/release-completion.md).

---

## Game Directory Structure

```
.install/
├── OpenPrey-client_x64      # Main executable (.exe on Windows)
├── OpenPrey-ded_x64         # Dedicated server (.exe on Windows)
├── OpenAL32.dll             # (Windows) optional bundled runtime
└── openprey/
    ├── game_x64             # Unified game module (.dll / .so / .dylib)
    ├── glprogs/
    ├── guis/
    ├── maps/
    ├── materials/
    ├── script/
    └── strings/
```

- **Single-player**: loads `game_<arch>` from `openprey/`
- **Multiplayer**: loads the same unified `game_<arch>` module
- **Legacy compatibility**: `gamex86` / `gamex64` aliases are still accepted during migration

---

## Asset Validation

OpenPrey automatically validates your Prey installation at startup to confirm the required official base packs are present and unmodified.

**How it works:**

1. Engine scans the detected `base/` pack set
2. Chooses the required layout to validate against:
   - Classic retail naming (`pak000.pk4` ... `pak004.pk4`)
   - Consolidated retail naming (`pak_data.pk4`, `pak_sound.pk4`, `pak_en_v.pk4`, `pak_en_t.pk4`)
3. Refuses to continue if required packs are missing or do not match the expected checksums

**Configuration:**

- `fs_validateOfficialPaks 1` (default) — enable asset validation
- See [docs-dev/official-pk4-checksums.md](docs-dev/official-pk4-checksums.md) for the full checksum reference

---

## Advanced Configuration

### Display and Graphics

#### Multi-Monitor Support

- `r_screen -1` — auto-detect current display (default)
- `r_screen 0..N` — select a specific monitor
- `listDisplays` — list available monitor indices in the console
- `listDisplayModes [displayIndex]` — list available exclusive fullscreen modes for a display

#### Display Modes

- `r_fullscreen 0|1` — windowed vs fullscreen
- `r_fullscreenDesktop 1` — desktop-native fullscreen (default, recommended)
- `r_fullscreenDesktop 0` — exclusive fullscreen (uses `r_mode`/`r_custom*`)
- `r_mode -2` — request native desktop resolution for fullscreen mode selection
- `r_borderless 1` — borderless window when `r_fullscreen 0`

#### Windowed Sizing

- `r_windowWidth` / `r_windowHeight` — window size when running windowed
- `win_xpos` / `win_ypos` — window position (updated automatically when you move the window)
- Agent and CI validation runs should always force `+set r_fullscreen 0`

See [docs-user/display-settings.md](docs-user/display-settings.md) for the full display reference.

### File System and Validation

#### Path Variables

- `fs_basepath` — detected Prey install root (auto-discovered)
- `fs_homepath` — writable user path
- `fs_savepath` — save/config/log path (defaults to `fs_homepath`)
- `fs_game` — active game directory (`openprey`)
- `fs_cdpath` — locked runtime overlay path; use `.install/` as launch dir for testing

#### Path Discovery Order

1. Valid `fs_basepath` override if set via cvar or command line
2. Current working directory
3. Windows registry install entries (vendor keys, App Paths, uninstall metadata)
4. Known legacy CD-era install roots (`Human Head Studios/Prey`, `2K Games/Prey`, `Games/Prey`)

#### Manual Path Configuration

If your Prey installation is not auto-detected, launch with:

```
OpenPrey-client_x64 +set fs_basepath "C:\path\to\Prey"
```

### Debugging and Local Validation

**Recommended local validation loop:**

1. Launch from `.install/` in windowed mode:
   ```powershell
   .\OpenPrey-client_x64.exe +set fs_game openprey +set fs_savepath ..\.home +set logFile 2 +set logFileName logs/openprey.log +set r_fullscreen 0
   ```
2. Inspect `.home\logs\openprey.log` after each run
3. Fix warnings and errors in engine/game/parser/loader code before resorting to content-side workarounds

**Build automation helpers:**

- `tools/build/meson_setup.ps1` auto-detects and initialises the Visual Studio developer environment
- `compile -C builddir` auto-runs `setup --wipe` if the build directory is missing or invalid
- `tools/build/openprey_devcmd.cmd` is available as a reusable MSVC developer shell

---

## SDK and Game Library

OpenPrey's game code is derived from the Prey Software Development Kit and maintained in the companion [OpenPrey-GameLibs](https://github.com/themuffinator/OpenPrey-GameLibs) repository. Canonical edits for SDK/game-library work belong there first; `src/game` in this repository is a synchronized mirror used by the Meson build, with additional `src/Prey` and `src/preyengine` sync paths brought in as needed for compatibility work.

The SDK is subject to the original Human Head Studios EULA, which permits non-commercial modification for use with a legitimate copy of Prey, but prohibits commercial exploitation and standalone redistribution of the SDK-derived code. For complete terms, see `EULA.Development Kit.rtf` in the OpenPrey-GameLibs repository.

### Companion Workflow

- Default companion repo location: `../OpenPrey-GameLibs`
- `tools/build/meson_setup.ps1` syncs the companion repo before `setup`, `compile`, and `install`
- `OPENPREY_BUILD_GAMELIBS=1` triggers an additional companion build-and-stage pass during `compile`
- `tools/build/build_gamelibs.ps1` supports both Meson wrappers and the legacy `src/PREY.sln` layout
- Stage targets: `builddir/openprey/` and `.install/openprey/`

---

## Dependencies

| Library | Version | Purpose |
|---|---|---|
| [SDL3](https://www.libsdl.org/) | 3.4.0 | Cross-platform window, input, and display management |
| [GLEW](http://glew.sourceforge.net/) | 2.3.4 | OpenGL extension loading |
| [OpenAL Soft](https://openal-soft.org/) | bundled Windows package | 3D audio rendering |
| [stb_vorbis](https://github.com/nothings/stb) | 1.22 | Ogg Vorbis audio decoding |

All dependencies are resolved through Meson subprojects and wraps. No manual dependency installation is required on Windows; Linux requires system development packages (see [BUILDING.md](BUILDING.md)).

---

## Versioning

OpenPrey uses semantic base versions from `meson.build` and appends an explicit build track:

- `stable` — release builds, e.g. `X.Y.Z`
- `dev` — default local builds, e.g. `X.Y.Z-dev+gabcdef12`
- `nightly` / `beta` / `rc` — pre-release labels, e.g. `X.Y.Z-nightly.20260330.1+gabcdef12`

The base version is bumped manually in `meson.build` when advancing to the next release line; track labels, iterations, git metadata, and resource build numbers are generated automatically by the build system (`tools/build/meson_setup.ps1` / `tools/build/meson_setup.sh`).

---

[← Back to README](README.md)
