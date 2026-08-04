# SDL3 Migration Plan (Linux/Experimental macOS)

This document defines the implementation plan for migrating non-Windows platform backends toward SDL3 while preserving current runtime stability.

## Goals

- Reduce backend divergence between Windows and non-Windows hosts.
- Keep Linux and experimental macOS builds and release packaging stable during migration.
- Avoid regressions in stock-asset compatibility while platform code is modernized.

## Current State

- Windows can build/run with `platform_backend=sdl3`.
- Linux defaults to `platform_backend=sdl3` and uses the SDL3 window, input, display, and OpenGL context path. The native X11/GLX backend remains available with `-Dplatform_backend=native` as a fallback.
- Windows, Linux, and experimental macOS now share the main SDL3 implementation under `src/sys/sdl3/sdl3_backend.cpp`; `src/sys/win32/win_sdl3.cpp`, `src/sys/linux/linux_sdl3.cpp`, and `src/sys/osx/macosx_sdl3.cpp` are platform entry wrappers for build selection and host-specific glue.
- Experimental macOS `platform_backend=sdl3` now selects the SDL3 source path, including shared keyboard, mouse, and controller handling. The SDL3 path links the modern Cocoa/OpenGL/ApplicationServices framework set and leaves Carbon isolated to the native Cocoa fallback. The native Cocoa path remains available with `-Dplatform_backend=native` as comparison-only diagnostic infrastructure, not a supported release backend, and keeps the validated URL opening, shell-free process handoff, millisecond sleep timing, platform-backed memory reporting, cleaner GL startup failure handling, and improved Cocoa keyboard/mouse compatibility.
- Linux SDL3 still carries small platform-specific glue for executable discovery, POSIX lifecycle, DRM sysfs/XNVCtrl video-memory probing, and Steam Deck/XWayland launch behavior.
- Native Wayland now runs through the same SDL3 backend with driver-specific handling: startup logs the selected SDL video driver, relevant environment, active Wayland hints, display content scale, orientation, current/desktop display modes, exact refresh details when SDL reports them, and compositor-accepted window state after screen changes; Wayland mode/decorator/high-DPI hints are set before video init; `OPENQ4_FORCE_X11=1` remains available as an explicit XWayland fallback; `OPENQ4_WAYLAND_PREFER_LIBDECOR=1` can opt into SDL's libdecor preference for compositor-specific decoration issues; `OPENQ4_WAYLAND_DISABLE_LIBDECOR=1` can disable libdecor for compositor stacks where it causes startup or decoration failures; `OPENQ4_WAYLAND_SYNC_WINDOW_OPS=1` can opt into SDL's blocking window-operation synchronization for diagnosis; compositor-owned window positions are not persisted; relative mouse-look can continue when pointer confinement is unavailable; compositor-negotiated size/fullscreen changes are synchronized before renderer placement is refreshed; multi-display spanning falls back to the selected display; and `r_glTier auto` tries SDL's unversioned compatibility context before explicit profile/version contexts.
- Windows, Linux, and experimental macOS SDL3 builds now share the same multi-display and window-management implementation for `r_screen`, `r_multiScreen`, display diagnostics, desktop/exclusive fullscreen, borderless windows, high-DPI drawable refresh, display-change recovery, selected-display spanned-UI viewports, and windowed placement. Normal-window positions use the outer frame while configured sizes use the client area; SDL-reported title-bar/border extents (with a native Cocoa frame/content fallback because SDL does not expose them there) are included when constraining, centering, restoring, or reconciling a window after decoration, DPI/scale, monitor, or work-area changes. Maximized, minimized, fullscreen, and borderless geometry cannot overwrite the remembered normal placement. The macOS SDL3 wrapper also maps compatibility `Sys_DisplayToUse()` calls through the selected display index instead of assuming the main display.
- Windows, Linux, and experimental macOS SDL3 builds now share keyboard, mouse, controller, rumble, hotplug, gyro, touchpad, and touchscreen handling through `src/sys/sdl3/sdl3_backend.cpp`. Linux and experimental macOS set SDL controller HIDAPI, enhanced-report, Linux event-device, Steam Deck, IOKit, and MFi defaults before gamepad/joystick initialization, while `listControllers` reports the active hint state for device QA.

## Migration Phases

1. Backend Vocabulary + CI Alignment (completed)
- Use one backend selector (`platform_backend`) across all hosts.
- Allow non-Windows `platform_backend=sdl3` as a staging mode in CI and local builds.
- Keep native Linux and experimental macOS backends available for comparison while SDL3 paths are brought up.

2. Shared SDL3 Shell Introduction (completed for Windows/Linux/macOS source selection)
- Add shared SDL3 window/input lifecycle code under `src/sys/sdl3/`, with thin platform wrappers for Windows, Linux, and experimental macOS.
- Keep renderer context setup delegated to existing native code initially.
- Gate behind opt-in build/runtime cvars to allow side-by-side validation.

3. Linux SDL3 Runtime Bring-Up (default path)
- Replace Linux native window/event pump path with SDL3 equivalents.
- Validate fullscreen/windowed transitions, input capture, and multi-monitor behavior.
- Validate XWayland and native Wayland behavior explicitly in logs and docs.

4. Experimental macOS SDL3 Runtime Bring-Up
- Replace macOS native window/event pump path with SDL3 equivalents. Source selection now reaches the SDL3 wrapper; experimental macOS CI covers OpenGL and Metal bridge configure/build/install/package validation, the Metal package remains a bridge around the OpenGL renderer and is not a native Metal renderer, and real-device runtime validation remains required for hardware-specific input/audio/display signoff.
- Validate Cocoa integration assumptions, cursor modes, focus transitions, keyboard/text input, high-resolution scrolling, controller hotplug, gamepad/joystick mapping, and rumble behavior.
- Ensure app-bundle execution path behaves correctly.

5. Promotion To First-Class
- Keep SDL3 as the default Linux/experimental macOS path and continue broadening hardware runtime evidence after CI compile/link/package checks pass consistently.
- Keep the macOS native backend available for diagnostic comparison only unless a future support decision adds dedicated CI, packaging, and Apple-hardware runtime signoff.

## Validation Requirements Per Phase

- Configure/build/install succeeds in CI for Windows/Linux/experimental macOS.
- SP and MP startup smoke tests run without platform-specific content hacks.
- Log diagnostics for display/input failures remain actionable and explicit.
