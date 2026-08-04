# Display Settings and Multi-Screen Guide

This guide covers openPREY display/window settings for end users, including multi-monitor behavior and modern fullscreen/window handling.

## Quick Start

- Press `Alt+Enter` to toggle fullscreen/windowed mode (fast path uses `vid_restart partial`).
- Run `listDisplays` in the console to list monitor indices for `r_screen`.
- On SDL3 builds, run `listDisplayModes [displayIndex]` to list available exclusive fullscreen modes, including SDL-reported content scale, pixel density, and exact refresh details when available.
- The in-game `Settings -> System` menu exposes display resolution, fullscreen policy, borderless/window sizing, custom exclusive fullscreen sizing, refresh rate, UI aspect behavior, display target, multi-screen, and resolution scale controls.
- `Settings -> System` also exposes a **Performance Preset** dropdown plus an **Auto-Detect** button above the display-resolution controls.
- After changing video cvars, run `vid_restart` (or `vid_restart partial` for quick window/fullscreen transitions).

## Performance Presets

The `com_performancePreset` cvar stores the selected preset. Use the Settings menu dropdown, or run `applyPerformancePreset [name]` from the console.
Running `applyPerformancePreset` without a name applies the stored `com_performancePreset`; if that stored value is invalid, openPREY falls back to `balanced`. Explicit unknown names are rejected without changing the current preset.

| Preset | Display and AA | Texture and audio profile | Intended use |
|---|---|---|---|
| `minimum` | 50% scale, no AA, 30 FPS cap | Aggressive texture downsizing, 1x anisotropy, one sound sample per shader, stereo, no EAX, lower emitter budget | Most constrained systems. |
| `lowpower` | 75% scale, no AA, 30 FPS cap | Texture downsizing, 1x anisotropy, one sound sample per shader, stereo, no EAX, lower emitter budget | Raspberry Pi-class and other low-power systems. |
| `performance` | 85% scale, SMAA medium, 60 FPS cap | Full-size textures, 2x anisotropy, stereo, no EAX, moderate emitter budget | Modest desktops and handhelds aiming for smoother frame pacing. |
| `balanced` | 100% scale, 2x MSAA, SMAA medium, 120 FPS cap | Full-size textures, 4x anisotropy, surround/EAX restored, full emitter budget | General desktop default. |
| `quality` | 100% scale, 4x MSAA, SMAA medium, 144 FPS cap | 8x anisotropy, DDS replacements enabled, larger upload budget, surround/EAX restored | Strong desktop GPUs. |
| `ultra` | 100% scale, 8x MSAA, SMAA medium, 240 FPS cap | 16x anisotropy, source textures preferred over DDS replacements, high-end benchmark tag, surround/EAX restored | Explicit high-end choice; Auto-Detect does not select this automatically. |

All presets keep optional shadow maps and subjective/modern post effects disabled, so the authored Prey (2006) look remains the baseline. Enable shadow maps, bloom, SSAO, tone mapping, motion blur, or CRT filtering separately after choosing a preset if you want those effects.

Performance presets write video, texture-allocation, texture-sampling, and audio backend cvars. Anisotropy is capped to the maximum supported by the active GPU. Run `vid_restart` after applying one so renderer, texture allocation, and sampler changes take effect; run `s_restart` as well if you want speaker/EAX/emitter-budget changes to rebuild the active sound backend immediately.

`autoDetectPerformancePreset` takes no arguments. It selects a conservative preset from platform signals, CPU architecture, system RAM, video RAM, and renderer capability flags, then applies it. Missing or implausible memory telemetry is treated as a conservative fallback instead of promoting the system to a higher preset. On Raspberry Pi hosts or explicit `OPENPREY_LOWPOWER=1` / `OPENPREY_RASPBERRYPI=1` signals, it chooses `lowpower`.

For package or platform validation, `performancePresetSelfTest` checks that the preset commands are registered, every preset is known to the menu-facing cvar and command completion lists, auto-detect returns and applies a supported non-`ultra` preset, command arguments behave correctly, all preset targets are registered and covered by backup/restore, rejected target normalization rolls back atomically, the preset progression stays coherent, the preset cvar mappings apply correctly, and the test restores touched cvar values, flags, and modified-state bookkeeping before finishing.

## Core Display Settings

| Setting | Default | What it does |
|---|---:|---|
| `r_fullscreen` | `1` | `1` = fullscreen, `0` = windowed. |
| `r_fullscreenDesktop` | `1` | `1` = native desktop fullscreen (recommended). `0` = exclusive fullscreen using `r_mode`/`r_custom*`. |
| `r_borderless` | `1` | Borderless window mode when `r_fullscreen 0`. |
| `r_windowWidth` | `1280` | Windowed width. |
| `r_windowHeight` | `720` | Windowed height. |
| `win_xpos` | (auto) | Outer window-frame X position (updated automatically when you move the window). |
| `win_ypos` | (auto) | Outer window-frame Y position (updated automatically when you move the window). |
| `r_mode` | `-2` | Fullscreen sizing selector (`-2` = desktop native/current display, `-1` = custom, `0+` = legacy preset index). |
| `r_customWidth` | `1920` | Custom exclusive-fullscreen width used when `r_mode -1`. |
| `r_customHeight` | `1080` | Custom exclusive-fullscreen height used when `r_mode -1`. |
| `r_displayRefresh` | `0` | Requested fullscreen refresh rate (0 = default/driver choice). |
| `r_screen` | `-1` | SDL3 monitor target (`-1` auto/current, `0..N` explicit index). |

## Texture Quality (Picmip and Downsizing)

openPREY has two independent ways to spend less memory and bandwidth on textures.

**`image_downSize*`** is retail Prey (2006)'s ceiling: no texture of that kind is
allowed to exceed a given size. Performance presets drive these.

**`image_picmip`** is the Quake 3 style relative reduction familiar from
`r_picmip`: it drops whole mip levels, so every step halves a texture no matter
how large it started. Unlike the Quake 3 version, openPREY applies it **only to the
diffuse layer of materials** — the color texture of a `diffusemap` stage. Normal
maps, specular maps, lighting, skies, decals, fonts, and every 2D/HUD surface
keep their authored resolution, so surfaces stay correctly lit and the interface
stays sharp while the bulk of texture memory still comes down.

| Setting | Default | What it does |
|---|---:|---|
| `image_picmip` | `0` | Mip levels to drop from diffuse textures. `0` = full resolution. Each step halves the texture. |
| `image_picmipFilter` | `1` | Which image paths `image_picmip` may reduce. `0` = every diffuse texture, `1` = `textures/*` (world surfaces), `2` = `models/*` (characters, weapons, props), `4` = any other namespace. Add values to combine. |
| `image_picmipMinSize` | `32` | Reduction stops once a texture's longest axis reaches this size, so small textures never turn to mush. |
| `image_downSize` | `0` | Enables the general texture size ceiling. |
| `image_downSizeLimit` | `0` | That ceiling, in pixels. `0` = no limit. |
| `image_downSizeBump` / `image_downSizeBumpLimit` | `0` / `256` | Separate ceiling for normal maps. |
| `image_downSizeSpecular` / `image_downSizeSpecularLimit` | `0` / `64` | Separate ceiling for specular maps. |

The ceiling is applied first and `image_picmip` reduces from there, so raising
`image_picmip` always halves the result even when a preset has already clamped a
texture. A material that declares `nopicmip` opts out of both.

These settings change the pixels a texture is built from, so openPREY reloads
images automatically when you change one — no `vid_restart` needed. The generated
texture cache is keyed by the active reduction, so switching back and forth does
not leave stale sizes behind.

One limitation applies to DDS replacement packs. Compressed data can only be
reduced by discarding mip levels the file already contains, so a `.dds` exported
without a full mip chain stays larger than the same texture would on the normal
path. Set `image_showPrecompressedTextures 1` to have openPREY name any replacement
that could not reach the requested size.

```
image_picmip 2
image_picmipFilter 3
```

That halves world and model diffuse textures twice while leaving lighting detail,
the HUD, and menus untouched.

## Renderer Backend (OpenGL default; Vulkan is experimental)

openPREY ships with an **OpenGL renderer as the default and only supported
backend** on every platform. A **Vulkan renderer is included but is
experimental and opt-in** — it is under active development, not feature-complete
or performance-validated, and can show visual artifacts or instability. Do not
use it for normal play; OpenGL remains the recommended renderer.

| Setting | Default | What it does |
|---|---:|---|
| `r_renderApi` | `gl` | Renderer backend: `gl` (default, supported) or `vulkan` (**experimental**). `best` resolves to `gl` until the Vulkan backend clears its promotion evidence and sign-off. Takes effect on **engine restart**, not `vid_restart`. |
| `r_actualRenderApi` | (read-only) | Reports the backend that actually initialized. If a Vulkan request fails, the engine **falls back to OpenGL** and this reports `gl`. |

### All `r_renderApi` values

| Value | Aliases | What it selects |
|---|---|---|
| `best` | — | The platform default. Currently resolves to `gl` on **every** platform, and will keep doing so until Vulkan clears its promotion evidence and sign-off. |
| `gl` | `opengl` | The OpenGL renderer. This is the default and the recommended choice. |
| `vulkan` | `vk` | The experimental Vulkan renderer module. |
| `gl-module` | — | Always loads the OpenGL renderer as a module instead of using a statically linked copy. This is a diagnostic option; it renders identically to `gl`. |

Anything else is rejected with a warning, and openPREY uses `gl`.

Notes:

- The `vulkan` selection is archived to your config and applied at the next
  engine start; restart openPREY fully (not just `vid_restart`) to switch.
- If Vulkan cannot initialize (no compatible driver/GPU, or a module error),
  openPREY logs a warning and renders with OpenGL so you are never left with a
  black screen. Check `r_actualRenderApi` or `gfxInfo` to see the active
  backend.
- Experimental status means known issues are expected; please only file
  Vulkan-specific reports with `openprey.log` and `gfxInfo`, and note that it is
  not yet a release-supported path.

### Vulkan on macOS (through MoltenVK)

Apple does not ship a Vulkan driver. On macOS, openPREY's Vulkan renderer runs on
top of **MoltenVK**, a Vulkan-on-Metal translation layer that is bundled inside
both macOS packages. It is a translation layer, not a Metal renderer, and it
does not replace or remove the OpenGL renderer.

- **OpenGL is still the default on macOS**, in both the OpenGL and Metal bridge
  packages. Nothing changes unless you opt in.
- Vulkan is a **runtime option, not a separate download**. There is no third
  macOS package to install and nothing to enable at install time.
- To try it: open the console, run `r_renderApi vulkan`, then quit and relaunch
  openPREY. Check `r_actualRenderApi` or `gfxInfo` afterwards to confirm what
  actually started.
- **Expect problems.** macOS support is experimental, the Vulkan renderer is
  experimental, and this combination has no accepted testing on real Apple
  hardware yet. Missing effects, wrong shading, poor performance, or a refusal
  to start are all plausible.
- **To go back:** run `r_renderApi gl` and restart. The setting is saved to your
  config, so it stays on OpenGL after that.
- **If it cannot start**, you do not need to do anything. openPREY logs the
  reason and renders with OpenGL instead, so a failed attempt never leaves you
  without a picture. Common reasons on a Mac are a GPU that does not meet the
  renderer's Vulkan 1.3 feature floor, or a package whose bundled translation
  layer is missing or was stripped by a copy.
- When reporting a macOS Vulkan problem, include `openprey.log` (it records which
  translation-layer library was loaded), the `gfxInfo` output, and your Mac
  model and macOS version.

## Frame Cap

| Setting | Default | What it does |
|---|---:|---|
| `com_maxfps` | `240` | Presentation frame cap (`0` = uncapped). |
| `com_steamDeckAutoFrameCap` | `1` | When the Steam Deck profile is active, applies a Deck-friendly cap only while `com_maxfps` is still `240`. |
| `com_steamDeckFrameCap` | `0` | Steam Deck default cap override (`0` = use detected display refresh clamped to the Deck-oriented range). |

The Steam Deck auto cap preserves custom `com_maxfps` values. Set `com_steamDeckAutoFrameCap 0` to opt out, or set `com_maxfps` directly for a fixed cap.

## Anti-Aliasing Settings (New)

| Setting | Default | What it does |
|---|---:|---|
| `r_multiSamples` | `0` | MSAA sample count for the main scene render target (`0`, `2`, `4`, `8`, `16`; `0` = off). |
| `r_postAA` | `0` | Post AA mode (`0` = off, `1` = SMAA medium, `2` = SMAA high, `3` = SMAA ultra, `4` = color-edge prototype). |
| `r_msaaAlphaToCoverage` | `1` | Enables alpha-to-coverage for perforated/alpha-tested materials when MSAA is active. Helps foliage/fences look cleaner. |
| `r_msaaResolveDepth` | `0` | Also resolves depth during MSAA resolve. Usually leave this off unless debugging a depth-dependent edge case. |

`r_multiSamples` value guide:
- `0`: disabled (fastest, most aliasing).
- `2`: low-cost MSAA uplift for modest GPUs.
- `4`: recommended default quality/performance balance.
- `8`: high quality, noticeably higher GPU cost.
- `16`: enthusiast/high-end setting where supported.
- `1` usually provides no meaningful benefit and is not recommended.

`r_postAA` value guide:
- `0`: disabled.
- `1`: SMAA medium; luma-edge detection with a `0.10` threshold and 8-step search, recommended as the post-AA default.
- `2`: SMAA high; luma-edge detection with the same `0.10` threshold and a 16-step search.
- `3`: SMAA ultra; luma-edge detection with a lower `0.05` threshold and a 32-step search.
- `4`: color-edge prototype; color-edge detection with a `0.10` threshold and a 16-step search for comparison captures.

Notes:
- `r_multiSamples` is hardware-limited and may be clamped by the driver/GPU.
- Unsupported `r_multiSamples` values are normalized to the supported ladder before video startup (`1` becomes off; odd/intermediate values step up to `2`, `4`, `8`, or `16`).
- On SDL3 builds, video startup retries lower MSAA requests if the window or GL context rejects the requested sample count (`16 -> 8 -> 4 -> 2 -> off`) and logs the requested, selected, and driver-reported multisample attributes.
- The game scene target is validated separately from the window framebuffer. If a driver rejects the RGBA8 + depth/stencil offscreen target at the selected sample count, openPREY retries lower samples down to `0`; if even the single-sample target is unavailable, it keeps running through the direct-render fallback and logs the exact framebuffer status and attachment details.
- `gfxInfo` reports the active AA summary, including requested/effective MSAA, `GL_MAX_SAMPLES`, alpha-to-coverage, post AA mode, screen fraction, and supersampling state.
- The Post AA startup/runtime log records the active SMAA edge mode, threshold, search steps, and local contrast scale so quality captures can be compared without guessing which shader contract was active.
- Changing `r_multiSamples` should be followed by `vid_restart`.
- `r_postAA`, `r_msaaAlphaToCoverage`, and `r_msaaResolveDepth` can be changed at runtime, but a `vid_restart` is still safe if behavior looks stale.

## Multiplayer Visibility Effects

These optional client-side cvars add player outlines, rim lighting, and bright skins in multiplayer. Defaults keep the effects off; set one or more strength values above `0` to enable them. They do not change hit detection, snapshots, or server authority.

All of them are exposed in `Multiplayer -> Settings -> Appearance`, under the **Enemy** and **Teammate** tabs, with a live preview of the selected model. Colors and outline width are preset dropdowns there; the cvars accept any value in range if you prefer to set them from a config.

| Setting | Default | What it does |
|---|---:|---|
| `cl_player_outline_enemy` | `0` | Enemy player outline strength (`0..1`), used as the outline opacity. |
| `cl_player_outline_team` | `0` | Teammate player outline strength (`0..1`). Teammate outlines ignore depth so allies stay readable through geometry. |
| `cl_player_outline_width` | `2.0` | Outline width in screen pixels (`0.5..6.0`). |
| `cl_player_rimlight_enemy` | `0` | Enemy player rimlight strength (`0..1`). |
| `cl_player_rimlight_team` | `0` | Teammate player rimlight strength (`0..1`). |
| `cl_player_visibility_enemy_color` | `1 0.12 0.05` | Enemy outline/rimlight RGB color, using float components. |
| `cl_player_visibility_team_color` | `0.1 0.85 0.25` | Teammate outline/rimlight RGB color, using float components. |
| `cl_player_brightskin_enemy` | `0` | Enemy player bright skin strength (`0..1`). |
| `cl_player_brightskin_team` | `0` | Teammate player bright skin strength (`0..1`). |
| `cl_player_brightskin_enemy_color` | `1 0.05 0.02` | Enemy bright skin RGB color, using float components. |
| `cl_player_brightskin_team_color` | `0.05 1 0.22` | Teammate bright skin RGB color, using float components. |

Two renderer cvars shape the rimlight itself. They are not in the menu, and their defaults reproduce the fixed falloff the pass used before they existed, so an untouched config looks the same:

| Setting | Default | What it does |
|---|---:|---|
| `r_playerRimlightPower` | `2.0` | Rimlight falloff exponent (`0.25..8.0`). Higher tightens the band to the silhouette; lower spreads it across the body. |
| `r_playerRimlightFloor` | `0` | Rimlight floor (`0..1`). Lifts the whole body by this fraction of the rim strength, so a player facing you head on is still tinted. At `1` the rimlight becomes a flat additive wash. |

Example:

```cfg
seta cl_player_outline_enemy 0.85
seta cl_player_rimlight_enemy 0.5
seta cl_player_outline_team 0.45
seta cl_player_rimlight_team 0.25
seta cl_player_outline_width 2.0
```

Notes:
- The outline is a shell drawn just outside the player silhouette and masked against the silhouette itself, so it stays a constant-width ring at any distance instead of tinting the whole body. Overlapping body, head, and weapon surfaces are each painted once, so the outline does not darken where they meet.
- The ring holds its requested pixel width in every direction, including on ultrawide displays.
- Only the **outline** is ever drawn through geometry. Teammate outlines ignore depth and are drawn across the whole level, so an ally anywhere on the map reads as a ring at their position. The rimlight and the bright skin stay depth tested in every case — a ring marks a position, while a shaded body seen through a wall is a different thing entirely.
- See-through and depth-tested outlines are masked separately, so an ally behind geometry never erases the ring off an enemy standing in front of them.
- The rimlight needs GLSL support (`glprogs/player_rimlight.*`). On a driver without it the rimlight is skipped and the console says so once; outline and bright skin still work.
- Without GLSL the outline falls back to a scaled shell, which approximates the requested pixel width instead of matching it exactly.
- The overlays are drawn after the ambient floor and light-grid passes, so `r_forceAmbient` and indirect lighting no longer wash them out.
- `r_skipPlayerVisibilityEffects 1` disables all three overlays engine-side, for clean captures or A/B comparisons.
- Bots are ordinary players for this feature: they occupy real client slots, so they receive the same overlays as human opponents and teammates. Nothing in the decision keys on being a bot.
- `g_showPlayerVisibilityEffects 1` prints one line per player whenever the answer changes — whether the overlays were applied and which render entities were reached (body, head, world weapon), or the reason they were skipped, and whether the client is a bot. Use it when a strength cvar looks like it is doing nothing. Steady states print once, not every frame.

## Multiplayer Pickup and Opponent-Weapon Style

`Settings -> Game Options -> General` includes two client-side presentation
controls for multiplayer. Stock appearance remains the default, and neither
control changes game state or what anybody else sees.

| Setting | Default | What it does |
|---|---:|---|
| `g_simpleItems` | `0` | Pickup style: `0` original models, `1` legacy simple icons, `2` icon-coloured flat diffuse, or `3` flat diffuse with a soft light sweep moving up each world item. |
| `g_mpFlatOpponentWeapons` | `0` | `1` gives weapons held by opponents the matching icon-coloured flat diffuse. Held weapons never receive the moving sweep. |

Flat pickup styles change only the diffuse colour layer of a model. Authored
normal maps, specular highlights, effects, emissive/ambient layers, and alpha
coverage remain intact; transparent shells such as the bubbles around health
pickups therefore keep their original appearance. Weapon and ammo pickups in
the world are items, so style `3` gives them the upward sweep too.

The first-person view weapon is always excluded, even when opponent-weapon
colour is enabled. In team modes the optional held-weapon colour applies only
to the opposing team; in free-for-all it applies to every other player.
Changes take effect immediately without reconnecting.

## Resolution Scale

| Setting | Default | What it does |
|---|---:|---|
| `r_screenFraction` | `100` | Main-scene resolution scale percentage (`10..200`). Values below `100` reduce scene resolution for performance; values above `100` supersample the scene before resolving it back to the native back buffer. |

The Display menu exposes curated presets: `10%`, `25%`, `50%`, `75%`, `85%`, `100%`, `125%`, `150%`, and `200%`.

## Fullscreen Policy (Desktop vs Exclusive)

- Default behavior is **desktop-native fullscreen** (`r_fullscreenDesktop 1`): fullscreen matches your current desktop resolution and does not change Windows display mode.
- For **exclusive fullscreen** (explicit mode switch), set `r_fullscreenDesktop 0`. In this mode, `r_mode`/`r_customWidth`/`r_customHeight` control the requested fullscreen resolution.
- In `Settings -> System`, **Display Resolution** lists Desktop Native first, then only the fullscreen preset resolutions SDL3 reports for the selected display, followed by Custom. Preset entries include compact aspect labels such as `16:9` or `21:9`. Choosing an unusual reported mode writes exact custom width/height when no legacy `r_mode` index exists.
- The console `listModes` command shows the expanded legacy `r_mode` preset catalog for configs and command-line use, covering common desktop, laptop, ultrawide, HiDPI, 4K, 5K, 6K, and 8K resolutions. The Settings dropdown still hides static presets that the selected display does not report.
- **Refresh Rate** lists Auto plus SDL3-reported refresh rates for the currently selected fullscreen resolution. Leave it on Auto unless you specifically need an exclusive-mode refresh request.
- On Windows, fullscreen windows minimize on focus loss so system UI such as Alt+Tab and the Snipping Tool overlay can take foreground cleanly.
- On Windows, `PrintScreen` yields to the system snipping UI by default (`win_printScreenToSystemTool 1`). Use `F12` for the built-in openPREY screenshot command, or set that cvar to `0` if you explicitly want `PrintScreen` available for in-engine binds again.

Notes:
- When `r_fullscreenDesktop 1`, `r_mode` and `r_custom*` are ignored for fullscreen sizing (they still exist for legacy configs and exclusive mode). Use `r_screenFraction` for below-native scaling or supersampling while staying in desktop-native fullscreen.
- Use `listDisplayModes` to see what your monitor actually supports in exclusive mode. On SDL3/Wayland, display diagnostics also report scale, orientation, pixel density, and exact refresh details that help diagnose compositor scaling behavior.

## Windowed Sizing and Placement

- `Settings -> System -> Display Sizing` exposes `r_windowWidth`, `r_windowHeight`, `r_customWidth`, `r_customHeight`, and `r_displayRefresh`. Leaving Refresh Rate on `Auto` writes `r_displayRefresh 0`.
- New Windows installs, and legacy Windows configs migrated from the old default, use borderless windowed presentation when `r_fullscreen 0` to avoid OpenGL bordered-window frame pacing stalls. Set `r_borderless 0` and run `vid_restart` if you specifically want a resizable bordered window.
- When bordered windowed mode is active (`r_fullscreen 0`, `r_borderless 0`), resizing updates `r_windowWidth`/`r_windowHeight` from the client area automatically. Maximizing or minimizing does not overwrite the remembered normal size.
- Moving the window updates `win_xpos`/`win_ypos` from the outer frame, including the title bar and borders. Borderless/fullscreen placement does not overwrite the remembered normal position.
- When switching fullscreen -> windowed, openPREY restores the last remembered windowed size/position (it should not come back as a fullscreen-sized window).
- If you unplug/rearrange monitors and the saved window position becomes off-screen, openPREY will recover by clamping/recentering the complete outer frame back onto a valid display.
- If you set `r_screen` to an explicit display index (`0..N`), the complete window frame is constrained to that display's usable area. Client sizing accounts for the platform's current title bar/border extents, including after display-scale or work-area changes. With `r_screen -1`, placement is respected unless it becomes invalid/off-screen.
- SDL3 tip: hold `Shift` while resizing to snap the window aspect ratio to common targets (4:3, 16:9, 16:10, 21:9, etc.).

## Aspect Ratio and FOV

- `r_aspectRatio` is **deprecated/ignored**. Aspect ratio and FOV behavior are derived automatically from the current render size, so the game follows any aspect ratio without manual selection.
- The Display menu no longer exposes a manual Aspect Ratio selector; Display Resolution and the live window size drive aspect behavior automatically.
- Weapon gameplay zoom uses the same gameplay FOV conversion path as normal view FOV, so authored weapon zoom values keep consistent framing/magnification across aspect ratios.
- In multiplayer, zoomed first-person view suppresses view bob while scoped so reticle tracking stays stable during movement.
- Scope GUI yaw tracking for zoom overlays follows the weapon/player view axis path, improving scope alignment while turning.

## View Weapon FOV and Placement (New)

These settings control first-person viewmodel rendering (the weapon on screen). They are client-side tuning controls and are not gameplay/network authority cvars.

The in-game menu exposes these under `Settings -> Game Options -> View Weapon`.

| Setting | Default | What it does |
|---|---:|---|
| `cl_gunfov` | `0` | View-weapon FOV override (`0` = follow current view FOV). |
| `cl_gunfov_adjust` | `1` | Aspect policy for `cl_gunfov`: `1` keeps classic 4:3-style weapon framing across screen ratios, `0` uses direct viewport-based FOV conversion. |
| `cl_gun_x` | `0` | Client weapon X/right offset. |
| `cl_gun_y` | `0` | Client weapon Y/forward offset. |
| `cl_gun_z` | `0` | Client weapon Z/up offset. |

Notes:
- `cl_gunfov` values above `0` are clamped to a safe range internally for weapon projection.
- Weapon projection is handled in renderer weapon-depth path, so narrow/wide aspect changes are handled consistently.
- `cl_gun_x/y/z` are additive with legacy `g_gunX/Y/Z` offsets. Prefer `cl_gun_*` for user config.
- openPREY's legacy baseline keeps `g_gunX` at `1` and `g_gunZ` at `-1` so the default widescreen viewmodel framing stays out of the viewport edge.

## UI Aspect Correction (New)

This controls 2D UI layout behavior (menu, HUD, console, loading/initializing screens):

The in-game menu exposes this as `Settings -> System -> Display Sizing -> UI Aspect`.

| Setting | Default | What it does |
|---|---:|---|
| `ui_aspectCorrection` | `1` | `1` keeps classic 4:3-style correction for all 2D UI. `0` stretches 2D UI to the full 2D draw region. |

## Text Rendering (New)

Menu and HUD text is drawn from scalable font files rather than the original
fixed-size bitmap ones. The letterforms are the same — the fonts were rebuilt by
tracing the game's own artwork — but they are now rasterised at whatever
resolution your display is actually running at, instead of a 640x480-era atlas
being magnified. Text stays sharp at 1440p and 4K rather than softening.

| Setting | Default | What it does |
|---|---:|---|
| `r_useTrueTypeFonts` | `1` | `1` draws text from the scalable fonts. `0` returns to the original bitmap fonts. |
| `r_ttfFontResolution` | `1.0` | Multiplies the resolution the glyphs are rasterised at. Raise for slightly sharper text at the cost of texture memory; lower to save memory. |

Changing either takes effect the next time fonts are loaded, so restart the game
or run `vid_restart` to see the change.

One visible difference: the original `chain` font shipped with a zero-width
space character, which ran words together in that face. The rebuilt font gives it
a normal space, so some text is spaced differently — correctly — than it used to be.

This covers the developer console and the loading screen too. Those draw from a
separate fixed-size character sheet, which is rebuilt at your display's
resolution the same way.

Raising `r_ttfFontResolution` costs texture memory quadratically: at 1440p a font
uses roughly 21 MB across its three sizes at the default of `1.0`. Lowering it to
`0.75` roughly halves that if you are short on video memory.

If the font files are missing (for example in a mod that replaces them), the game
falls back to the original bitmap fonts automatically.

## Text Background (Accessibility)

Prey (2006) draws much of its text directly over the world and over busy panel
artwork, which can leave very little contrast. This draws a solid black backing
behind each line of menu and HUD text, so the text stays readable regardless of
what is behind it.

| Setting | Default | What it does |
|---|---:|---|
| `gui_textBackground` | `0` | How opaque the backing is. `0` is off, `1` is fully opaque black. `0.6`–`0.8` is usually enough to read comfortably while still showing the artwork. |
| `gui_textBackgroundPadding` | `2` | How far the backing extends past the text, in 640x480 virtual units. Raise it if the text feels cramped against the edge. |

Both apply immediately — no restart needed — and work with either font path.

```
seta gui_textBackground 0.75
seta gui_textBackgroundPadding 2
```

The backing covers text drawn by the menus, HUD and in-game GUIs. The developer
console draws on its own path and is not affected.

## Multi-Monitor Behavior (New)

When the render surface spans multiple monitors:

- 2D elements (console, HUD, menus, loading/initializing UI) are constrained to the selected display region. With `r_screen -1`, this defaults to the primary display.
- 2D aspect behavior inside that region is controlled by `ui_aspectCorrection`.
- Menu cursor mapping follows the same 2D region so mouse interaction stays aligned.

3D world rendering is unchanged by these UI cvars.

## Useful Console Examples

### Recommended Modern Defaults

```cfg
seta r_screen -1
seta r_fullscreenDesktop 1
seta r_fullscreen 1
seta r_multiSamples 4
seta r_postAA 1
seta r_msaaAlphaToCoverage 1
seta ui_aspectCorrection 1
vid_restart
```

### Borderless Window on a Specific Monitor

```cfg
seta r_fullscreen 0
seta r_borderless 1
seta r_screen 1
vid_restart
```

### Custom Fullscreen Resolution

```cfg
seta r_fullscreen 1
seta r_fullscreenDesktop 0
seta r_mode -1
seta r_customWidth 2560
seta r_customHeight 1440
vid_restart
```

### Stretch Menu + HUD (No 4:3 Correction)

```cfg
seta ui_aspectCorrection 0
```

### View Weapon: Classic Framing + Slight Lowering

```cfg
seta cl_gunfov 90
seta cl_gunfov_adjust 1
seta cl_gun_z -1.15
```

### View Weapon: Match World FOV, Personal Position Offset

```cfg
seta cl_gunfov 0
seta cl_gun_x 0.5
seta cl_gun_y -0.5
seta cl_gun_z -0.5
```

### Performance-Focused AA Preset

```cfg
seta r_multiSamples 2
seta r_postAA 1
seta r_msaaAlphaToCoverage 1
vid_restart
```

### Maximum Clarity (Higher GPU Cost)

```cfg
seta r_multiSamples 8
seta r_postAA 1
seta r_msaaAlphaToCoverage 1
vid_restart
```

## Troubleshooting

- If a display change does not apply, run `vid_restart`.
- If monitor targeting looks wrong, run `listDisplays`, then set `r_screen` to the correct index and restart video.
- If UI appears too centered/boxed on wide displays, set `ui_aspectCorrection 0`.
- If the window opens off-screen after a monitor change, set `r_screen` explicitly to the target monitor and restart video; openPREY will also attempt to recover automatically.
- If AA settings seem unchanged, check values with `r_multiSamples`, `r_postAA`, and `r_msaaAlphaToCoverage`, then run `vid_restart`.
- If enabling `r_postAA 1` turns the 3D viewport black on an older build, set `r_postAA 0`, run `vid_restart`, and attach `openprey.log` plus the output of `gfxInfo`. Current builds use a three-pass GLSL SMAA path and should no longer hit the old feedback-loop failure. RenderDoc capture is not yet supported on the current openPREY renderer.
