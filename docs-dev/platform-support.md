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

- Primary actively validated host: Windows x64.
- Build system: Meson + Ninja.
- Dependency model: Meson subprojects/wraps.
- Backend direction: SDL3 first (legacy Win32 path is transitional).
- Toolchain direction: MSVC 19.46+ recommended (enforceable with `-Denforce_msvc_2026=true`).

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
- `builddir/` is the standard build output directory.
- `.install/` is the standard staged runtime package root.

## Bring-Up Staging

1. Keep Windows x64 stable for OpenPrey engine/game workflows.
2. Incrementally enable Linux build/source selection and validation.
3. Incrementally enable macOS build/source selection and validation.
4. Promote non-Windows platforms once compile/link/runtime validation becomes repeatable.

## Definition Of Done For First-Class Platform Support

- Clean Meson configure + build.
- Engine reaches playable map/session startup with stock Prey assets.
- Core input, rendering, audio, and networking paths work without content-side hacks.
- Regressions are fixed in engine/platform code (not by shipping replacement assets).
