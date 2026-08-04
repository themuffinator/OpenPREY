# openPREY Technical Reference

This document covers technical details for advanced users and developers: compatibility status, file layout, configuration cvars, asset validation, build dependencies, versioning, and the SDK/game library structure.

For installation and a feature overview, see the [README](README.md). For building from source, see [BUILDING.md](BUILDING.md).

---

## Table of Contents

- [Prey Compatibility Status](#prey-compatibility-status)
- [Game Directory Structure](#game-directory-structure)
- [Asset Validation](#asset-validation)
- [Advanced Configuration](#advanced-configuration)
- [Light Grids](#light-grids)
- [SDK and Game Library](#sdk-and-game-library)
- [Dependencies](#dependencies)
- [Versioning](#versioning)

---

## Prey Compatibility Status

This status reflects compatibility with official Prey (2006) assets, not binary interchangeability with proprietary retail DLLs.

### Landed

- ✅ **Project Rebrand Completed** — Meson project metadata, staged binaries, VS Code launch settings, and documentation use openPREY naming throughout
- ✅ **Prey Install Discovery** — `fs_basepath` auto-detection targets Prey registry/App Paths/uninstall metadata and known legacy install roots; Steam/GOG assumptions removed
- ✅ **Unified Game Module Loader** — Engine builds and stages a unified `game_<arch>` module under `basepr/` for both SP and MP paths
- ✅ **Companion Repo Tooling** — Meson stages canonical `OpenPrey-game` sources directly and builds one unified module
- ✅ **Official PK4 Layout Validation** — Engine startup rejects missing or modified required base-pack layouts when `fs_validateOfficialPaks 1` is enabled
- ✅ **Cross-host Meson Support** — Source selection, dependency wiring, and packaging tooling cover Windows, Linux, and macOS hosts; active CI currently covers the core x64 lanes
- ✅ **Precomputed Light-Grid Irradiance** — Portal-area light grids can be loaded, baked, visualized, and applied as an indirect-diffuse pass in the renderer

### In Progress

- ❌ **Stock-asset SP/MP Smoke Validation** — Default staged launch flow is still being verified across single-player and multiplayer startup cases
- ❌ **Prey Gameplay Bring-up** — Canonical `OpenPrey-game` integration is implemented; retail runtime verification continues during the migration
- ❌ **Particle and FX Compatibility** — Doom 3 / Prey-era declarations and runtime paths are restored, with retail parity checks still pending
- ❌ **Cross-host Runtime Validation** — Windows remains the deepest runtime-validation path while Linux/macOS map validation is extended

### Not Yet Claimed

- **Full Campaign Completion** — The project does not yet claim end-to-end single-player completion against stock assets
- **Multiplayer Parity** — Multiplayer compatibility remains under active validation

Current follow-up work is tracked in [TODO.md](TODO.md) and [docs/dev/release-completion.md](docs/dev/release-completion.md).

---

## Game Directory Structure

```
.install/
├── openPREY-client_x64      # Main executable (.exe on Windows)
├── openPREY-ded_x64         # Dedicated server (.exe on Windows)
├── OpenAL32.dll             # (Windows) optional bundled runtime
└── basepr/
    ├── game_x64             # Unified game module (.dll / .so / .dylib)
    ├── pak0.pk4             # openPREY code/config/text content
    ├── pak1.pk4             # openPREY binary assets
    └── mod.json
```

- **Single-player**: loads `game_<arch>` from `basepr/`
- **Multiplayer**: loads the same unified `game_<arch>` module
- **Legacy compatibility**: `gamex86` / `gamex64` aliases are still accepted during migration

---

## Asset Validation

openPREY automatically validates your Prey installation at startup to confirm the required official base packs are present and unmodified.

**How it works:**

1. Engine scans the detected `base/` pack set
2. Chooses the required layout to validate against:
   - Classic retail naming (`pak000.pk4` ... `pak004.pk4`)
   - Consolidated retail naming (`pak_data.pk4`, `pak_sound.pk4`, `pak_en_v.pk4`, `pak_en_t.pk4`)
3. Strictly checks known consolidated-layout checksums; classic-layout packs remain
   presence-only until independently verified CD-era checksums are recorded

**Configuration:**

- `fs_validateOfficialPaks 1` (default) — enable asset validation
- See [docs/dev/prey-rebase/official-pk4-checksums.md](docs/dev/prey-rebase/official-pk4-checksums.md) for the full checksum reference

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

See [docs/user/display-settings.md](docs/user/display-settings.md) for the full display reference.

### File System and Validation

#### Path Variables

- `fs_basepath` — detected Prey install root (auto-discovered)
- `fs_homepath` — writable user path
- `fs_savepath` — save/config/log path (defaults to `fs_homepath`)
- `fs_game` — active game directory (`basepr`)
- `fs_cdpath` — locked runtime overlay path; use `.install/` as launch dir for testing

#### Path Discovery Order

1. Valid `fs_basepath` override if set via cvar or command line
2. Current working directory
3. Windows registry install entries (vendor keys, App Paths, uninstall metadata)
4. Known legacy CD-era install roots (`Human Head Studios/Prey`, `2K Games/Prey`, `Games/Prey`)

#### Manual Path Configuration

If your Prey installation is not auto-detected, launch with:

```
openPREY-client_x64 +set fs_basepath "C:\path\to\Prey"
```

### Debugging and Local Validation

**Recommended local validation loop:**

1. Launch from `.install/` in windowed mode:
   ```powershell
   .\openPREY-client_x64.exe +set fs_game basepr +set fs_savepath ..\.home +set logFile 2 +set logFileName logs/openprey.log +set r_fullscreen 0
   ```
2. Inspect `.home\logs\openprey.log` after each run
3. Fix warnings and errors in engine/game/parser/loader code before resorting to content-side workarounds

**Build automation helpers:**

- `tools/build/meson_setup.ps1` auto-detects and initialises the Visual Studio developer environment
- `compile -C builddir` auto-runs `setup --wipe` if the build directory is missing or invalid
- `tools/build/openprey_devcmd.cmd` is available as a reusable MSVC developer shell

---

## Light Grids

openPREY supports idTech 4-style precomputed irradiance volumes for indirect diffuse lighting. Runtime data is stored in two parts:

- `maps/<map>.lightgrid` — probe layout, atlas metadata, and per-area light-grid parameters
- `env/maps/<map>/area*_lightgrid_amb.tga` — baked irradiance atlas images, typically one per populated portal area

### Baking

Run baking from a staged `.install/` launch against a valid Prey asset install and always force windowed mode:

```powershell
.\openPREY-client_x64.exe +set fs_game basepr +set fs_savepath ..\.home +set r_fullscreen 0 +bakeLightGrids game/roadhouse force
```

Command form:

```text
bakeLightGrids [all | all-mp | <map> ...] [force] [-quit] [limit<num>] [bounce<num>] [size<num>] [blends<num>] [samples<num>] [separateAreas] [grid ( x y z )]
```

Important options:

- `all` — bake every discovered map, single-player first and then multiplayer
- `all-mp` — bake only multiplayer maps
- `force` — remove existing `.lightgrid` and atlas outputs before rebaking
- `-quit` — exit automatically after the bake batch completes
- `limit<num>` — cap the number of generated probes for a bake pass
- `bounce<num>` — number of diffuse bounces to integrate
- `size<num>` — per-probe capture resolution
- `blends<num>` — atlas blend samples per probe
- `samples<num>` — supersample count per capture
- `separateAreas` — regenerate per-area layouts during baking to reduce peak memory use
- `grid ( x y z )` — override probe spacing in world units

### Runtime and Debugging

- `r_useLightGrid 1` — enable the indirect-diffuse light-grid pass
- `r_showLightGrid 0..3` — visualize probe placement by area
- `r_forceAmbient <value>` — lift the final scene toward a minimum brightness floor
- `r_lightGridBakeWorkers` — control CPU worker count during baking
- `r_lightGridBakeAsyncReadback 0|1` — enable async GPU readback when supported
- `r_lightGridBakeMemoryMB` — cap transient bake memory usage
- `r_lightGridBakeReadbackSlots` — control async readback buffer count

Use `r_showLightGrid 1` to inspect only the current portal area, `2` to draw valid probes in all areas, and `3` to include invalid probe locations as well.

---

## SDK and Game Library

openPREY's game code is derived from the Prey Software Development Kit and maintained in the companion [OpenPrey-game](https://github.com/themuffinator/OpenPrey-GameLibs) repository. Canonical edits for SDK/game-library work belong there first. Meson stages its `src/game`, `src/Prey`, and `src/preyengine` trees under `<builddir>/.tmp/openprey_gamelibs_stage/` and builds them against engine headers from this repository; companion copies of those engine headers are deliberately excluded. The per-build snapshot keeps concurrent platform configurations isolated.

The SDK is subject to the original Human Head Studios EULA, which permits non-commercial modification for use with a legitimate copy of Prey, but prohibits commercial exploitation and standalone redistribution of the SDK-derived code. For complete terms, see `EULA.Development Kit.rtf` in the OpenPrey-game repository.

### Companion Workflow

- Default companion repo location: `../OpenPrey-game`
- `OPENPREY_GAMELIBS_REPO` overrides the companion repository location
- `tools/build/meson_setup.ps1` and `tools/build/meson_setup.sh` refresh the staged source snapshot when needed
- `tools/build/build_gamelibs.ps1` is a compatibility entry point that directs developers back to the canonical root Meson build
- Stage targets: `builddir/basepr/` and `.install/basepr/`

---

## Dependencies

| Library | Version | Purpose |
|---|---|---|
| [SDL3](https://www.libsdl.org/) | 3.4.0 | Cross-platform window, input, and display management |
| [GLEW](http://glew.sourceforge.net/) | 2.3.4 | OpenGL extension loading |
| [OpenAL Soft](https://openal-soft.org/) | bundled Windows package | 3D audio rendering |
| [libogg](https://xiph.org/ogg/) | 1.3.6 | Ogg bitstream support |
| [libvorbis](https://xiph.org/vorbis/) | 1.3.7 | Ogg Vorbis audio decoding and file access |

All dependencies are resolved through Meson subprojects and wraps. No manual dependency installation is required on Windows; Linux requires system development packages (see [BUILDING.md](BUILDING.md)).

---

## Versioning

openPREY uses semantic base versions from `meson.build` and appends an explicit build track:

- `stable` — release builds, e.g. `X.Y.Z`
- `dev` — default local builds, e.g. `X.Y.Z-dev+gabcdef12`
- `nightly` / `beta` / `rc` — pre-release labels, e.g. `X.Y.Z-nightly.20260330.1+gabcdef12`

The base version is bumped manually in `meson.build` when advancing to the next release line; track labels, iterations, git metadata, and resource build numbers are generated automatically by the build system (`tools/build/meson_setup.ps1` / `tools/build/meson_setup.sh`).

---

[← Back to README](README.md)
