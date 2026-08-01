# Experimental macOS Support Data

macOS support is experimental and does not currently have an active package/signing
workflow. Reports should therefore identify the exact source commit, companion
`OpenPrey-game` commit, Meson options, architecture, and graphics backend used for the
local build.

## Minimum useful report

Include:

- `git rev-parse HEAD` for openPREY and `OpenPrey-game`;
- macOS version and hardware model;
- `uname -m` and whether the process ran natively or through translation;
- the full terminal transcript;
- the saved `basepr/logs/openprey.log`;
- the selected `r_renderApi` and the renderer/driver lines from `gfxInfo`;
- any macOS `.ips` crash report;
- the exact Meson setup command and build type.

Do not use an operating-system screen capture to validate rendering. Use the engine's
registered `screenshot` command and attach the image it writes from the render target.

## Reproduction launch

Always force windowed mode for development runs:

```sh
./openPREY-client_arm64 \
  +set r_fullscreen 0 \
  +set fs_game basepr \
  +set fs_savepath ../.home \
  +set logFile 2 \
  +set logFileName logs/openprey.log
```

Point `fs_basepath` at a legitimate Prey (2006) installation when automatic discovery
does not find one. Do not copy retail `base/` PK4s into `basepr/`.

## Support collector

If the staged tree contains `collect_macos_support_info.sh`, run it from the package
root and inspect the archive before attaching it. The collector is diagnostic tooling;
its presence does not imply a signed or supported macOS release.

The archive's `package/path-resolution.txt` records the package root, app path,
expected loose openPREY runtime paths, embedded `basepr` data and unified game-module
paths, plus any `fs_basepath`, `fs_cdpath`, and `fs_savepath` lines it can find. The
collector gathers this information without launching openPREY.

## Current boundaries

- OpenGL is the first compatibility target.
- Vulkan through MoltenVK and other backend parity remain validation TODOs.
- One unified `game_<arch>` module serves SP and MP; split `game-sp`/`game-mp`
  packages are not part of openPREY.
- See [the rebase status ledger](../dev/prey-rebase/status-ledger.md) for current
  validation and release-lane TODOs.
