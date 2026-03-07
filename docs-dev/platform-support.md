# OpenPrey Platform And Architecture Roadmap

This document defines platform direction for OpenPrey and how SDL3 + Meson are used during the Prey (2006) adaptation.

## Target End State

- First-class support on modern desktop operating systems:
  - Windows
  - Linux
  - macOS
- First-class support for modern 64-bit desktop architectures:
  - x64 (`x86_64`)
- Preserve stock Prey asset compatibility while modernizing build/platform layers.

## Current Baseline (0.0.1 Era)

- Meson host support: Windows x64, Linux x64, and macOS arm64.
- Primary actively validated gameplay/runtime host: Windows x64.
- Build system: Meson + Ninja.
- Dependency model: Meson subprojects/wraps.
- Backend direction: SDL3 first on Windows; non-Windows hosts currently map `-Dplatform_backend=sdl3` to native platform sources for command-line/CI parity.
- Toolchain direction:
  - Windows: MSVC 19.46+ recommended (enforceable with `-Denforce_msvc_2026=true`)
  - Linux: GCC or Clang with system GLEW/OpenAL/X11 development packages
  - macOS: Apple Clang with Homebrew GLEW and platform OpenAL frameworks

## SDL3 Direction

- SDL3 is the default portability layer for:
  - window lifecycle
  - input event handling
  - display/mode management
- New platform-facing work should prefer SDL3 abstractions.
- Platform-specific fallbacks should remain isolated under `src/sys/<platform>/`.

## Meson Direction

- Meson is the canonical build system.
- External dependencies should be resolved through Meson dependency/subproject flow.
- `tools/build/meson_setup.ps1` is the standard Windows entry point.
- `tools/build/meson_setup.sh` is the standard non-Windows entry point.
- `builddir/` is the standard build output directory.
- `.install/` is the standard staged runtime package root.
- `.home/` is the standard repo-local save/config/log root for validation runs launched from `.install/`.
- Linux staging should install `share/applications/openprey.desktop` plus `share/icons/hicolor/.../openprey.png`.
- macOS staging should install `OpenPrey.icns`, with nightly packaging generating an `OpenPrey.app` launcher bundle.

## Bring-Up Staging

1. Keep Windows x64 stable for OpenPrey engine/game workflows.
2. Keep Linux x64 and macOS arm64 Meson/nightly packaging green.
3. Expand non-Windows runtime and map-validation coverage.
4. Promote non-Windows platforms from build parity to runtime parity once validation is repeatable.

## Definition Of Done For First-Class Platform Support

- Clean Meson configure + build.
- Engine reaches playable map/session startup with stock Prey assets.
- Core input, rendering, audio, and networking paths work without content-side hacks.
- Regressions are fixed in engine/platform code (not by shipping replacement assets).
