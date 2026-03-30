<div align="center">

<img src="assets/img/banner.png" alt="OpenPrey banner">

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Version](https://img.shields.io/badge/version-0.0.1-green.svg)](https://github.com/themuffinator/OpenPrey)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](https://github.com/themuffinator/OpenPrey)
[![Architecture](https://img.shields.io/badge/arch-x64%20%7C%20arm64-orange.svg)](https://github.com/themuffinator/OpenPrey)
[![Build System](https://img.shields.io/badge/build-Meson%20%2B%20Ninja-yellow.svg)](https://mesonbuild.com/)

**Prey (2006) reborn — open-source, modern platform, classic feel.**

[Features](#features) • [Installation](#installation) • [Building](BUILDING.md) • [Documentation](#documentation) • [Credits](#credits)

</div>

---

> [!WARNING]
> **Development Notice:** This project leans heavily on exploratory, agentic AI "vibe coding" for speed. If you want a traditionally engineered codebase, this isn't for you.

---

## About

**OpenPrey** is a free, open-source engine and game-code replacement for Prey (2006). Built as a minimal, Prey-focused adaptation of the [OpenQ4](https://github.com/themuffinator/OpenQ4) codebase, it keeps the modernization work that makes current builds practical to develop and debug, while retargeting the engine, tooling, and game-library workflow around Prey's unified game-module model.

Run your existing copy of Prey on modern hardware across Windows, Linux, and macOS — without changing the game you remember.

> [!NOTE]
> **OpenPrey does not include game assets.** You must own a legitimate copy of Prey (2006) to play. On Windows, `fs_basepath` auto-discovery checks the current working directory, registry install entries (including App Paths and uninstall metadata), and known legacy install roots such as `Human Head Studios/Prey`, `2K Games/Prey`, and `Games/Prey`.

---

## Features

### Prey Compatibility
- **Prey-first Runtime Layout** — Unified `openprey/` directory for engine overlays, game modules, GUI scripts, shaders, maps, and strings
- **Unified Game Module Model** — Engine loads `game_<arch>` for both SP and MP paths, with legacy `gamex86`/`gamex64` aliases accepted during migration
- **Legacy Install Discovery** — Windows install detection covers CD-era registry keys, App Paths, uninstall entries, and known install roots without assuming Steam/GOG-only layouts
- **Official Asset Validation** — Startup validation checks the required official Prey base PK4 layout before the game runs

### Modern Platform and Tooling
- **SDL3-first Windows Backend** — SDL3 is the default window, input, and display backend; the legacy Win32 backend remains as a transitional fallback
- **Meson + Ninja Build System** — Canonical configure/build/install path with repo-local staging under `.install/`
- **Cross-platform Nightly Builds** — GitHub Actions builds and packages Windows, Linux, and macOS artifacts from the same Meson staging flow
- **Crash Diagnostics** — Windows debug builds write crash logs and minidumps into `crashes/` beside the executable

### Display and Runtime
- **Modern Window Modes** — Windowed, borderless, desktop-native fullscreen, and exclusive fullscreen all supported
- **Monitor Selection** — SDL3 builds expose `r_screen`, `listDisplays`, and `listDisplayModes` for multi-monitor setup
- **Aspect Ratio and FOV** — Automatically derived from the current render size; no manual aspect-ratio setting needed

---

## Installation

### Step 1 — Get Prey (2006)

You need a copy of **Prey (2006)** installed from original media or another legitimate distribution. OpenPrey supports CD-era install layouts and does not require Steam or GOG.

### Step 2 — Download the latest OpenPrey release

Head to the **[Releases page](https://github.com/themuffinator/OpenPrey/releases)** and download the latest archive for your platform (Windows, Linux, or macOS).

### Step 3 — Extract

Unzip or unpack the archive to any folder you like.

### Step 4 — Play

Launch `OpenPrey-client_x64` (that's `OpenPrey-client_x64.exe` on Windows). OpenPrey will find your Prey (2006) installation automatically in most cases.

> [!NOTE]
> **Windows players:** The package is self-contained — no extra software needs to be installed.

> [!NOTE]
> **Linux players:** OpenPrey currently runs through XWayland on Wayland desktops. Make sure `DISPLAY` is set in your environment.

> [!TIP]
> If OpenPrey can't find your Prey installation automatically, launch with `+set fs_basepath "C:\path\to\Prey"`. See the [manual path configuration](TECHNICAL.md#manual-path-configuration) section in the technical reference.

---

## Documentation

- [Display Settings](docs-user/display-settings.md) — fullscreen, borderless, monitor selection, and anti-aliasing
- [Technical Reference](TECHNICAL.md) — compatibility status, advanced configuration, file layout, dependencies, and versioning
- [Building from Source](BUILDING.md) — compiler requirements, build options, and the GameLibs companion repository

---

## Building from Source

Want to compile OpenPrey yourself? Full instructions, compiler requirements, and notes on the [OpenPrey-GameLibs](https://github.com/themuffinator/OpenPrey-GameLibs) companion repository live in **[BUILDING.md](BUILDING.md)**.

---

## Contributing

OpenPrey is an open project and welcomes contributions of all kinds — bug reports, code fixes, documentation, and platform testing.

1. Fork the repository
2. Create a feature branch
3. Make your changes and test thoroughly
4. Submit a pull request

Keep compatibility with official Prey assets in mind, follow the existing code style, and see [BUILDING.md](BUILDING.md) for build setup instructions.

---

## Credits

### Project Lead

- **themuffinator** — OpenPrey development and maintenance

### Upstream Credit

- **Justin Marshall** — [Quake4Doom](https://github.com/jmarshall23/Quake4Doom) baseline and related reverse-engineering work
- **Robert Backebans** — RBDOOM-3-BFG modernization work that informs this ecosystem

### Original Developers

- **id Software** — idTech 4 engine lineage
- **Raven Software** — Quake 4 codebase lineage used by OpenQ4
- **Human Head Studios** — Prey (2006) and the Prey SDK

### Third-Party Libraries

- **Sean Barrett** — [stb_vorbis](https://github.com/nothings/stb) audio codec
- **GLEW Team** — OpenGL extension wrangler
- **OpenAL Soft Contributors** — 3D audio implementation
- **SDL Team** — Cross-platform framework

---

## License

OpenPrey is licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0) (GPLv3). You are free to use, modify, and distribute the software under its terms.

See the [LICENSE](LICENSE) file for full details.

**Note:** The GPLv3 license applies to OpenPrey's engine code only. Game library code in [OpenPrey-GameLibs](https://github.com/themuffinator/OpenPrey-GameLibs) is derived from the Prey SDK and subject to the original Human Head Studios SDK EULA. Prey game assets remain the property of Human Head Studios and 2K.

---

## Disclaimer

OpenPrey is an independent project and is not affiliated with, endorsed by, or sponsored by Human Head Studios, 2K, Bethesda, ZeniMax, id Software, or Raven Software. Prey is a trademark of ZeniMax Media Inc.

You must own a legitimate copy of Prey (2006) to use this software. OpenPrey does not include any copyrighted game assets.

**THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.** OpenPrey is experimental software under active development. Use at your own risk.

---

## Links

[Repository](https://github.com/themuffinator/OpenPrey) • [Game Library](https://github.com/themuffinator/OpenPrey-GameLibs) • [Issue Tracker](https://github.com/themuffinator/OpenPrey/issues) • [Releases](https://github.com/themuffinator/OpenPrey/releases) • [OpenQ4](https://github.com/themuffinator/OpenQ4)
