# Steam Deck

> [!NOTE]
> This page documents inherited engine/tooling capability. Retail Prey behavior remains
> runtime-validation pending unless the rebase status ledger records specific evidence.

openPREY includes a dedicated `openPREY-steamdeck` launcher in staged Linux packages.

## Launching

- Use `openPREY-steamdeck` instead of `openPREY-client_x64`.
- The launcher adds `+set com_platformProfile steamdeck`, exports `OPENPREY_STEAMDECK=1`, and preserves any extra command-line arguments you pass.
- Native Wayland is the default SDL choice when available. To force the old XWayland fallback path, launch with `OPENPREY_FORCE_X11=1`; the dedicated launcher and direct client launches both honor it unless an explicit SDL video-driver override is already set.
- If native Wayland hits a libdecor startup or window-decoration issue on a specific compositor stack, launch with `OPENPREY_WAYLAND_DISABLE_LIBDECOR=1`.
- If you launch `openPREY-client_x64` directly on a Steam Deck or SteamOS host, openPREY auto-selects the `steamdeck` platform profile when `com_platformProfile` is still `default`. Set `OPENPREY_NO_STEAMDECK_AUTODETECT=1` or `OPENPREY_DISABLE_STEAMDECK_AUTODETECT=1` to disable that fallback.

## Controls

Default gameplay bindings shipped by the stock openPREY config:

- `JOY15` = attack
- `JOY16` = zoom
- `JOY3` = jump
- `JOY4` = crouch
- `JOY6` = reload
- `JOY5` = flashlight
- `JOY1` = weapon wheel
- `JOY2` = last weapon
- `JOY13` = run toggle / walk modifier
- `JOY18` = show objectives / scores
- `JOY19` = jump if exposed as right rear paddle 1
- `JOY20` = crouch if exposed as left rear paddle 1
- `JOY21` = reload if exposed as right rear paddle 2
- `JOY22` = weapon wheel if exposed as left rear paddle 2

Menu behavior:

- `JOY7` and `JOY8` both open the in-game menu; `JOY17` also opens the menu when the OS delivers the Guide button to the game.
- `JOY3` selects the focused menu item, while `JOY4`, `JOY7`, and `JOY8` back out of menus.
- The D-pad is reserved for menu focus movement whenever a menu is open, even though those same buttons also have gameplay bindings.
- The movement stick can also move menu focus, and holding the D-pad, movement stick, or shoulder buttons repeats navigation/scrolling for longer lists.
- Extra `JOY23` through `JOY32` and `AUX1` through `AUX16` buttons are bindable but left unbound by default for user customization.

Tuning and haptics:

- Controller rumble is driven from Prey (2006) sound shake/rumble metadata during gameplay.
- Use `in_joystickRumble 0` to disable motor output, or tune strength with `in_joystickRumbleScale`.
- The Steam Deck profile sets `in_joystickLowBatteryRumbleThreshold 20` and `in_joystickLowBatteryRumbleScale 0.75`, which caps effective rumble output while SDL reports a controller battery at or below 20 percent. This does not rewrite your configured rumble strength.
- The Steam Deck profile enables native SDL gyro aiming with a conservative `in_gyroSensitivity 0.20`; raise or lower that cvar if gyro aim feels slow or fast.
- `in_touchpadMode 1` routes gamepad touchpad motion to menu/console cursor movement. Use `in_touchpadMode 2` for touchpad mouse-look, `0` to disable touchpad motion, or `3` to leave touchpad motion off while keeping touchpad button binds.
- `in_touchscreen 1` routes direct touchscreen taps and drags to menus, console, and loading-continue input when SDL delivers native touch events.
- The in-game menu exposes controller controls under `Settings -> Game Options -> Controller`, including stick layout, radial dead-zone, look sensitivity, look curve, invert-look, trigger threshold, rumble tuning, gyro, touchpad mode, touchscreen input, and low-battery rumble caps.
- For the full input settings guide, see [input-settings.md](input-settings.md).

## Steam Input and Diagnostics

For native Steam Deck testing, use a Steam Input profile that exposes the Deck as a gamepad with its gyro and touchpad capabilities available to SDL. Steam-level gyro-to-mouse or touchpad-to-mouse mappings can still be useful for users, but they hide those inputs from openPREY's native `in_gyro` and `in_touchpadMode` paths.

If a build expects native touchscreen or multi-touch events, enable Touch API pass-through in the Steamworks/partner configuration. Without that partner-side setting, Steam may deliver touch as mouse emulation instead of SDL touchscreen events.

Run `listControllers` from the console when diagnosing Deck input. It prints the active SDL gamepad/joystick, GUID, type, path, power state, touchpad count, sensor capability, gyro/touchpad cvars, touchscreen routing, and low-battery rumble state. Save the log output with bug reports when gyro, touchpad, touchscreen, or rumble behavior is unclear.

## Performance

- When the Steam Deck profile is active, `com_steamDeckAutoFrameCap 1` applies a Deck-friendly `com_maxfps` only if `com_maxfps` is still the global default of `240`.
- The automatic cap uses SDL's current display refresh and clamps it to the Deck-oriented `40..90` range. LCD Deck sessions normally land on `60`; OLED or higher-refresh sessions can land higher.
- Set `com_maxfps` yourself to preserve a specific cap. Set `com_steamDeckAutoFrameCap 0` to disable the automatic default, or set `com_steamDeckFrameCap` to a nonzero value to choose the Deck default cap explicitly.

### Shadow Maps on Deck

Shadow maps run well on Deck-class hardware with a bounded update budget. Recommended settings:

```cfg
seta r_shadows 1
seta r_useShadowMap 1
seta r_shadowMapSize 1024
seta r_shadowMapPointSize 512
seta r_shadowMapCSM 1
seta r_shadowMapCascadeCount 3
seta r_shadowMapMaxUpdatesPerView 2
vid_restart
```

With the update budget set, the renderer spends each frame's shadow renders on the most important stale lights and serves the rest from the resident cache, so shadow cost stays bounded in dense scenes. See `docs/user/shadow-mapping.md` for the full settings reference.

## Asset Discovery

Linux Steam auto-discovery checks these roots and then expands any additional library folders from `libraryfolders.vdf`:

- `OPENPREY_STEAM_ROOT` / `OPENPREY_STEAM_ROOTS`
- `STEAM_COMPAT_CLIENT_INSTALL_PATH`
- `$XDG_DATA_HOME/Steam`
- `~/.steam/steam`
- `~/.steam/root`
- `~/.local/share/Steam`
- `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`

openPREY then looks for `steamapps/common/Prey` under each Steam library root.

For deterministic testing, set `OPENPREY_PREY_PATH` or `OPENPREY_PREY_ROOT` to the Prey (2006) install root directly, or set `OPENPREY_STEAM_LIBRARY` / `OPENPREY_STEAM_LIBRARIES` to Steam library roots that contain `steamapps/common/Prey`. On Linux, multiple roots may be separated with `:` or `;`.

## Notes

- Steam Deck support is still profile-driven, but the engine now has a Deck/SteamOS fallback detector for direct client launches.
- Suspend/resume and foreground/background SDL events release captured input, stop rumble, write the current config, and reacquire controllers when the app returns.
- Native Wayland is supported through the SDL3 backend; XWayland remains available through `OPENPREY_FORCE_X11=1` or explicit SDL video-driver environment variables.
- `OPENPREY_WAYLAND_DISABLE_LIBDECOR=1` is available as a native Wayland troubleshooting switch for compositor/libdecor problems.
- The developer-facing Deck QA checklist lives in [../dev/steam-deck-qa.md](../dev/steam-deck-qa.md).
