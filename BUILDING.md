# Building openPREY from Source

This guide covers everything required to compile openPREY from source on Windows, Linux, and macOS.

---

## Table of Contents

- [Prerequisites](#prerequisites)
- [GameLibs Companion Repository](#gamelibs-companion-repository)
- [Build Setup](#build-setup)
- [Build Options](#build-options)
- [Building on Windows](#building-on-windows)
- [Building on Linux / macOS](#building-on-linux--macos)
- [Output Files](#output-files)
- [Packaging a Distributable](#packaging-a-distributable)

---

## Prerequisites

### Compiler

| Platform | Minimum | Notes |
|---|---|---|
| **Windows** | MSVC 19.46+ (Visual Studio 2026+) | Use the Developer PowerShell or run `tools/build/openprey_devcmd.cmd` to initialise the environment |
| **Linux** | GCC 13+ or Clang 17+ | Distro packages are fine |
| **macOS** | Xcode 16+ / Clang 17+ | Install Command Line Tools via `xcode-select --install` |

### Build Tools

- **[Meson](https://mesonbuild.com/)** 1.2.0 or newer
- **[Ninja](https://ninja-build.org/)** (recommended backend)
- **Python 3** (used by build wrapper scripts)

### Linux System Packages

Linux builds require the following development packages:

```
glew  openal  x11  xext  xxf86vm
```

Install them through your distro's package manager before configuring (e.g. `libglew-dev`, `libopenal-dev`, `libx11-dev`, `libxext-dev`, `libxxf86vm-dev` on Debian/Ubuntu).

### Windows Note

On Windows, always invoke Meson through `tools/build/meson_setup.ps1` rather than calling `meson` directly from an arbitrary shell. The wrapper ensures MSVC tools (`cl.exe`, `link.exe`, etc.) are on `PATH` before setup, compile, and install steps.

```powershell
# Open a regular PowerShell window and use the wrapper:
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 <meson-command> [args...]
```

Alternatively, open `tools/build/openprey_devcmd.cmd` first to initialise the Visual Studio environment, then call `meson` directly.

> [!NOTE]
> Windows builds use Meson's `b_vscrt=static_from_buildtype` policy so openPREY itself does not require a separate Visual C++ redistributable install.

---

## GameLibs Companion Repository

openPREY's game code lives in a separate companion repository — [OpenPrey-game](https://github.com/themuffinator/OpenPrey-GameLibs). This separation clearly identifies the SDK-licensed components derived from the Prey Software Development Kit.

> [!IMPORTANT]
> **The openPREY build expects OpenPrey-game to be checked out alongside openPREY**, at `../OpenPrey-game` relative to this repository. If the companion repository is missing or at a different path, game-module builds will fail.

### Setting Up

```bash
# Clone both repositories side-by-side:
git clone https://github.com/themuffinator/openPREY.git
git clone https://github.com/themuffinator/OpenPrey-GameLibs.git OpenPrey-game

# Result:
#   ./openPREY/            ← this repository
#   ./OpenPrey-game/       ← game library source
```

To use a custom location, set the environment variable before configuring:

```bash
export OPENPREY_GAMELIBS_REPO=/path/to/OpenPrey-game   # Linux / macOS
$env:OPENPREY_GAMELIBS_REPO = "C:\path\to\OpenPrey-game"  # PowerShell
```

During configuration, Meson stages the canonical companion `src/game`, `src/Prey`, and `src/preyengine` trees under `<builddir>/.tmp/openprey_gamelibs_stage/`. Each Meson build directory owns its snapshot, so Windows, Linux, and other configurations can run independently without rewriting one another's inputs. Engine-facing headers such as `idlib` always come from this repository; similarly named companion SDK copies are not compiled. There is no in-repo game-source mirror and no separate companion-library build step.

`OPENPREY_GAMELIBS_REPO` overrides the companion location. The legacy `OPENQ4_GAMELIBS_REPO` name remains a temporary migration alias.

---

## Build Setup

All engine dependencies (SDL3, GLEW, OpenAL Soft, stb\_vorbis) are managed as Meson subprojects — no manual dependency installation is required on Windows. Linux requires system development packages (see [Linux System Packages](#linux-system-packages) above). The first configure step downloads and builds subproject dependencies automatically.

---

## Build Options

Pass any of these with `-D<option>=<value>` on the `meson setup` command line:

| Option | Default | Description |
|---|---|---|
| `build_engine` | `true` | Build `openPREY-client_<arch>` and `openPREY-ded_<arch>` |
| `build_games` | `true` | Build the unified game module from the canonical companion sources |
| `platform_backend` | `sdl3` | `sdl3`, `legacy_win32`, or `native` |
| `use_pch` | `true` | Use precompiled headers |
| `enforce_msvc_2026` | `false` | Fail configure if MSVC is older than 19.46+ (Windows only) |

---

## Building on Windows

> [!NOTE]
> Release packaging targets the static MSVC CRT so end users do not need a separate Visual C++ Redistributable install.

### Debug Build

```powershell
# 1. Configure
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback

# 2. Compile
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir

# 3. Run directly from builddir
builddir\openPREY-client_x64.exe +set r_fullscreen 0
```

### Release Build

```powershell
# 1. Configure
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 setup builddir . --backend ninja --buildtype=release --wrap-mode=forcefallback

# 2. Compile
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 compile -C builddir

# 3. Stage distributable package into .install/
powershell -ExecutionPolicy Bypass -File tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

### From a Visual Studio Developer Command Prompt

Once the MSVC environment is initialised via `tools/build/openprey_devcmd.cmd` you can call `meson` directly:

```batch
meson setup builddir . --backend ninja --buildtype=release
meson compile -C builddir
meson install -C builddir --no-rebuild --skip-subprojects
```

---

## Building on Linux / macOS

> [!NOTE]
> SDL3 selects the available Wayland or X11 display path on Linux. Use `OPENPREY_FORCE_X11=1` only when an XWayland fallback is required.

### Debug Build

```bash
# 1. Configure
bash tools/build/meson_setup.sh setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback -Dplatform_backend=sdl3

# 2. Compile
bash tools/build/meson_setup.sh compile -C builddir

# 3. Run directly from builddir
./builddir/openPREY-client_x64 +set r_fullscreen 0
```

### Release Build

```bash
# 1. Configure
bash tools/build/meson_setup.sh setup builddir . --backend ninja --buildtype=release --wrap-mode=forcefallback -Dplatform_backend=sdl3

# 2. Compile
bash tools/build/meson_setup.sh compile -C builddir

# 3. Stage distributable package into .install/
bash tools/build/meson_setup.sh install -C builddir --no-rebuild --skip-subprojects
```

---

## Output Files

### Build directory (`builddir/`)

| File | Description |
|---|---|
| `openPREY-client_x64[.exe]` | Main engine executable |
| `openPREY-ded_x64[.exe]` | Dedicated server |
| `basepr/game_x64[.dll/.so/.dylib]` | Unified game module |

- On Windows, import libraries (`.lib`), program databases (`.pdb`), and export files (`.exp`) may be present in debug builds; these are development-only artifacts.
- The wrapper stages `OpenAL32.dll` next to the executables on Windows where a bundled runtime is available.

### Install directory (`.install/`)

After running the install step, `.install/` is a self-contained distributable package:

```
.install/
├── openPREY-client_x64.exe     # Main executable
├── openPREY-ded_x64.exe        # Dedicated server
├── OpenAL32.dll                # (Windows) runtime dependency
└── basepr/
    ├── game_x64.dll            # Unified game module
    ├── pak0.pk4                # openPREY text/code/config content
    ├── pak1.pk4                # openPREY binary assets
    └── mod.json
```

> [!NOTE]
> Do not distribute raw `buildtype=debug` artifacts in public packages. MSVC import libraries (`*.lib`) are development-only artifacts and are not required in the package.

---

## Packaging a Distributable

The `meson install` step stages all required binaries into `.install/`. This directory is the input for release packaging.

The retained packaging tools can produce archives such as:

- `openprey-<version-tag>-windows.zip`
- `openprey-<version-tag>-linux.tar.xz`
- `openprey-<version-tag>-macos.tar.gz`
- `openprey-<version-tag>-x86_64.AppImage`
- `openprey-<version-tag>-aarch64.AppImage`

Package contents:
- `basepr/game_<arch>.(dll|so|dylib)` stays as a loose runtime module
- Staged overlay content is bundled into `basepr/pak0.pk4`
- Linux packages include `share/applications` and `share/icons` payloads
- macOS packages include an `openPREY.app` launcher bundle
- `tools/build/package_linux_appimage.py` assembles AppImages from a verified staged
  package. Set `APPIMAGE_EXTRACT_AND_RUN=1` only when the host cannot execute the
  AppImage runtime directly.

The inherited nightly/manual publishing workflows are disabled until they are rebuilt
around `OpenPrey-game`, `basepr`, and the unified module. Local packaging tools remain
available for candidate validation; they do not imply a published release.

---

[← Back to README](README.md)
