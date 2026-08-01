# Steam Deck QA and Shipping Checklist

Use this checklist before publishing or regression-testing a Steam Deck package. Record the tested device model, SteamOS build, SDL version, package build, and whether the run used Game Mode or Desktop Mode.

## Partner and Steam Input Setup

- [ ] Steam shortcut or partner launch option starts `openPREY-steamdeck`, not the raw `openPREY-client_x64` binary.
- [ ] Steam Input profile exposes normal gamepad controls, rear paddles, gyro, and touchpad without translating every Deck-specific input into keyboard/mouse events.
- [ ] Gyro is exposed to the game when testing native `in_gyro 1`; disable Steam-level gyro-to-mouse emulation for this pass.
- [ ] Touchpad is exposed as gamepad touchpad input when testing `in_touchpadMode 1` or `2`.
- [ ] Touch API pass-through is enabled in Steamworks/partner configuration for builds that expect native touchscreen events.
- [ ] Store or release notes mention `OPENPREY_FORCE_X11=1` as the fallback for users who hit native Wayland issues.

## Launch and Profile

- [ ] Launch through `openPREY-steamdeck`; confirm the log shows `com_platformProfile steamdeck`.
- [ ] Launch the raw client on Deck/SteamOS; confirm auto-detection selects the Steam Deck profile when `com_platformProfile` is still `default`.
- [ ] Launch with `OPENPREY_NO_STEAMDECK_AUTODETECT=1`; confirm the raw client stays on the default profile.
- [ ] Launch with `OPENPREY_FORCE_X11=1`; confirm SDL uses the X11/XWayland driver.
- [ ] Launch without SDL video overrides in a normal Deck session; confirm SDL selects native Wayland when available.

## Asset Discovery

- [ ] Stock internal-storage Steam install is found automatically.
- [ ] microSD or secondary Steam library is found through `libraryfolders.vdf`.
- [ ] `OPENPREY_PREY_PATH` or `OPENPREY_PREY_ROOT` points directly to a Prey install and wins deterministically.
- [ ] `OPENPREY_STEAM_ROOT` / `OPENPREY_STEAM_ROOTS` points to a relocated Steam client root and expands its libraries.
- [ ] `OPENPREY_STEAM_LIBRARY` / `OPENPREY_STEAM_LIBRARIES` points directly to library roots containing `steamapps/common/Prey`.
- [ ] Startup logs list Steam roots, library roots, and Prey install candidates clearly enough to diagnose failures.

## Input Diagnostics

- [ ] Run `listControllers` and save the log output.
- [ ] Confirm the active gamepad reports the expected name, type, path, GUID, power state, touchpad count, and gyro capability.
- [ ] Confirm `listControllers` reports `gyro: yes enabled=yes` when `in_gyro 1` and the device exposes a gyro.
- [ ] Confirm `listControllers` reports touchpad finger capacity on Steam Deck hardware.
- [ ] Confirm `listControllers` reports low-battery rumble cvars and whether the effective cap is active.
- [ ] Confirm ordinary Xbox/PlayStation-style controllers still report sane capabilities and keep the expected `JOY` binds.

## Input Behavior

- [ ] In menus, gamepad navigation, D-pad repeat, select, and back work.
- [ ] `in_touchpadMode 1` moves the menu/console cursor without also steering gameplay aim.
- [ ] `in_touchpadMode 2` sends touchpad movement to mouse-look during gameplay.
- [ ] Touchscreen taps activate menus and loading-continue input when native touch events are exposed.
- [ ] Gyro aim works during gameplay, stops while menus/console route mouse input, and has no large wake/resume spike.
- [ ] Rear paddles remain bindable through `JOY19` through `JOY22` when Steam Input exposes them.

## Suspend and Resume

- [ ] Suspend from an idle in-level single-player scene, wait at least 30 seconds, resume, and confirm input, audio, and camera control recover.
- [ ] Suspend during active combat, resume, and confirm no stuck attack, movement, or rumble state remains.
- [ ] Suspend from a menu or console, resume, and confirm menu cursor routing still works.
- [ ] Suspend during or immediately after a loading transition, resume, and confirm loading-continue input still works.
- [ ] Confirm the log shows background/foreground lifecycle messages and config write on background.

## Display and Performance

- [ ] `listDisplays` shows the expected Deck panel and selected display.
- [ ] `listDisplayModes` reports expected fullscreen modes.
- [ ] Native Wayland fullscreen, desktop fullscreen, and windowed modes behave correctly.
- [ ] XWayland fallback behaves correctly with `OPENPREY_FORCE_X11=1`.
- [ ] On a fresh config, the Steam Deck profile applies a `com_maxfps` cap from the detected refresh rate while preserving any non-default user cap.
- [ ] On battery below `in_joystickLowBatteryRumbleThreshold`, rumble output is capped without rewriting `in_joystickRumbleScale`.

## Completion

- [ ] Procedure 1 debug loop is clean for the Steam Deck SP launch path.
- [ ] Procedure 1 debug loop is clean for the Steam Deck MP launch path.
- [ ] At least one SP map was entered and played after startup.
- [ ] At least one MP flow was entered far enough to validate controller/menu behavior.
- [ ] Any remaining warnings are triaged into release notes, a follow-up issue, or this checklist.
