# Getting Started with openPREY

This guide is for players who want to install openPREY and start playing as quickly as possible.

## What You Need

- A legitimate installed copy of **Prey (2006)** from original media or another lawful distribution
- The latest openPREY release from the [Releases page](https://github.com/themuffinator/openPREY/releases)
- A release package that matches your operating system and CPU architecture (`x64` or `arm64`); Linux ARM64 downloads are currently preview packages
- Enough free disk space for both the openPREY package and the retail Prey (2006) `base/` assets

> [!NOTE]
> openPREY does not include Prey (2006) assets. It uses the original retail game files from your existing installation.

## System Requirements

These requirements are practical guidance for the current beta packages. openPREY can scale down through performance presets, but old or incomplete OpenGL drivers may still fail even when the raw hardware looks fast enough.

| Component | Minimum | Recommended |
|---|---|---|
| CPU | 64-bit CPU matching the package architecture; dual-core class or better | Modern quad-core or better x64/arm64 CPU |
| Operating system | Windows 7 or later; a modern 64-bit Linux desktop userspace comparable to Ubuntu 24.04; preview Linux ARM64 on 64-bit little-endian AArch64 hardware; experimental macOS 11 or later on Apple Silicon/arm64; SteamOS 3.x on Steam Deck | Windows 10/11, a current Linux distribution or SteamOS release, or an up-to-date Apple Silicon macOS install with maintained graphics drivers for experimental Mac testing |
| Memory | 4 GB RAM | 8 GB RAM for balanced play; 16 GB if you want higher presets, high resolutions, or room for background apps |
| Graphics | OpenGL compatibility driver with the ARB2-era features openPREY requires, including vertex/fragment program support; 1 GB VRAM is the practical low-end target | OpenGL 4.1+ compatibility-class GPU with 2 GB+ VRAM for balanced 1080p play; 6 GB+ VRAM for `quality`, `ultra`, high-resolution displays, or heavier post-processing |
| Storage | About 12 GB free for openPREY plus the retail Prey (2006) assets | 15 GB+ free for the package, assets, saves, logs, crash dumps, and future updates |
| Assets | Official retail Prey (2006) `base/` PK4 files | The same assets left unmodified so validation can identify a known layout |
| Input and audio | Keyboard/mouse and a working audio output | Controller optional; Steam Deck controls use the Deck profile; broadband network recommended for online multiplayer |

Use the `Settings -> System` Performance Preset dropdown or its adjacent Auto-Detect button after first launch if you are unsure where to start. The console command is `autoDetectPerformancePreset`. Systems near the minimum should use `minimum`, `lowpower`, or `performance` before enabling expensive options such as high MSAA, high resolution scale, shadow maps, or heavier post-processing.

## Recommended Install Flow

1. Install **Prey (2006)** from your original media or lawful distribution.
2. Download the openPREY release that matches your platform and CPU architecture.
3. Install or extract openPREY to its **own folder**, or use the matching Linux AppImage directly.
4. Launch `openPREY-client_<arch>` from an extracted package, or launch the Linux AppImage.
5. On Steam Deck, launch `openPREY-steamdeck` when it is included in the package.

openPREY will try to find your Prey (2006) install automatically.

## Folder Layout Options

### Recommended

- Leave retail Prey (2006) in its original installation folder.
- Keep openPREY in a separate folder.
- Let openPREY detect the retail `base/` assets automatically.

### Portable / Manual

If you prefer a self-contained setup, keep these side by side in the same root folder:

- `basepr/` from openPREY
- `base/` copied from your Prey (2006) install

> [!IMPORTANT]
> Do **not** copy retail PK4 files into `basepr/`. That folder is for openPREY runtime files.

## Platform Notes

### Windows

- You can use the matching installer or the `.zip` release.
- Windows release packages are meant to be self-contained.
- Current validation focuses on Windows 11 and Windows 10. Windows 7/8/8.1 remain legacy compatibility targets rather than the main test matrix.
- If openPREY crashes, check the `crashes/` folder beside the executable for log and dump files.

### Linux

- For the simplest desktop launch, download `openprey-<version>-x86_64.AppImage` on x64 or the clearly marked `openprey-<version>-preview-aarch64.AppImage` on ARM64, then run `chmod +x openprey-*.AppImage` and launch the file. It does not need system-wide installation.
- The AppImage contains openPREY and its packaged runtime dependencies, but it does **not** contain Prey (2006) assets. Point it at your existing install with `+set fs_basepath "/path/to/Prey"` when automatic discovery does not find it.
- The `.tar.xz` archive remains available when you want the loose client, dedicated server, Steam Deck launcher, desktop metadata, or offline documentation as ordinary files. Extract it to a folder of your choice.
- Linux packages default to the SDL3 runtime path and should be treated as targeting an Ubuntu 24.04-class 64-bit desktop userspace with working OpenGL plus Wayland/EGL or X11/GLX support.
- Linux ARM64 packages are preview builds until real ARM64 hardware completes native-Wayland SP/MP gameplay, dedicated-server, audio, input, and package signoff.
- Linux ARM64 requires 64-bit little-endian AArch64 hardware and a desktop OpenGL compatibility driver. GLES-only boards are not covered by the current package.

### Experimental macOS

- macOS support is experimental. Current packages are for Apple Silicon/arm64 Macs on macOS 11 or later. Intel Mac and universal2 packages are not published yet, and Rosetta is not a supported release target.
- Packaged macOS release/signing lanes are not currently enabled. Build from source only if you are prepared to diagnose an experimental target.
- The Metal bridge package still uses openPREY's OpenGL renderer path; it is not a native Metal renderer.
- Both macOS packages also carry openPREY's experimental Vulkan renderer. Apple ships no Vulkan driver, so on macOS it runs on top of MoltenVK, a Vulkan-on-Metal translation layer bundled inside the package. It is a translation layer, not a Metal renderer.
- OpenGL is still the default macOS renderer. Vulkan is opt-in: run `r_renderApi vulkan` in the console, then quit and relaunch openPREY. It is a runtime option rather than a separate download, so there is no third macOS package to install.
- Expect problems if you try it. macOS support is experimental, the Vulkan renderer is experimental, and this combination has no accepted testing on real Apple hardware yet. If it cannot start, openPREY logs the reason and renders with OpenGL instead. To go back, run `r_renderApi gl` and restart. See [Display Settings](display-settings.md#vulkan-on-macos-through-moltenvk).
- Unsigned macOS archives are ad-hoc signed only for bundle validity, are not notarized, and may require normal Gatekeeper approval on first launch.
- A future macOS app bundle will contain openPREY's `basepr` data and one unified game module. Do not copy retail `base` assets into an app bundle.
- For crashes like GitHub issue #73, attach full terminal output as text plus `openprey.log` and any `.ips` report. See [Experimental macOS Support Data](macos-support-data.md), and run `collect_macos_support_info.sh` from the package root when it is included.

### Steam Deck

- Use `openPREY-steamdeck` when it is included in the package. It enables the `steamdeck` platform profile and sets `OPENPREY_STEAMDECK=1`.
- Direct `openPREY-client_<arch>` launches on Steam Deck or SteamOS also auto-select the Deck profile while `com_platformProfile` is still `default`.
- Native Wayland is the default SDL path when available. Set `OPENPREY_FORCE_X11=1` to force the XWayland fallback from either the Steam Deck launcher or a direct client launch.
- If a native Wayland compositor has libdecor-related startup or window-decoration trouble, set `OPENPREY_WAYLAND_DISABLE_LIBDECOR=1` for that launch.
- Tune Deck controller behavior under `Settings -> Game Options -> Controller`, including gyro, touchpad mode, touchscreen routing, and low-battery rumble caps.
- Run `listControllers` from the console when reporting Deck input, battery, gyro, touchpad, or touchscreen issues.
- For more detail, see the [Steam Deck guide](steam-deck.md).

## If Auto-Detection Fails

Point openPREY at the **Prey (2006) root folder** that contains `base/`.

Example:

```text
openPREY-client_x64 +set fs_basepath "C:\path\to\Prey (2006)"
```

Do not point it directly at `base/` or `basepr/`.

For advanced path behavior and file layout details, see [TECHNICAL.md](../../TECHNICAL.md).

## Next Steps

- Want better video options? See [Display Settings](display-settings.md).
- Want to tune controls? See [Input Settings](input-settings.md).
- Want a quick overview of player-facing options? See [Client Settings Guide](client-settings.md).
- Want to host games? See [Server Setup Guide](server-setup.md).
