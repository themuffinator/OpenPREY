# RenderDoc Workflow

This document covers the openPREY RenderDoc status and the March 2026 black-viewport investigation.

## Root Cause

- The black viewport reports were not caused by missing SMAA assets in the release package.
- The older two-pass SMAA placeholder path had an undefined OpenGL feedback loop in the neighborhood-blend pass:
  - `OpenPrey-game/src/game/Game_render.cpp` already copied the resolved scene into `_currentRender` before the SMAA passes.
  - `content/basepr/pak0/materials/postprocess_openq4.mtr` previously drew `postprocess/smaa_blend` into `_postProcessAlbedo0` while also sampling `_postProcessAlbedo0`.
- Some drivers preserved the previous texture contents and appeared to work. Others returned black or undefined data. That is why the issue only reproduced for some users and clustered around `r_postAA 1`.
- Current builds no longer use that path. `r_postAA 1` now stages the resolved scene in `_postProcessAlbedo2`, runs a three-pass GLSL SMAA 1x implementation from that stable source, then blits the result back to `_postProcessAlbedo0` for the remaining post stack.

## Current RenderDoc Limitation

- Upstream RenderDoc currently supports `OpenGL 3.2 - 4.6 Core` but not `OpenGL 1.0 - 2.0 Compat`; see the RenderDoc project README: <https://github.com/baldurk/renderdoc>.
- openPREY's current renderer still depends on the ARB2 compatibility path (`GL_ARB_fragment_program`, `GL_ARB_shader_objects`, and related fixed-function-era state).
- openPREY has an explicit GL tier/context ladder (`r_glTier`) and can request core-profile contexts for forced modern tiers, but the shipping executor is still routed through the ARB2 bridge. Treat forced core contexts as renderer-modernization bring-up only until the GL 3.3/4.x executor lands.
- Launching openPREY through RenderDoc injection can cause required compatibility features to disappear during startup, which aborts renderer initialization before any frame can be captured.
- openPREY reports the missing required OpenGL features explicitly during that failure instead of only showing the generic "video card / driver" error.
- The commands and wrapper below are retained as plumbing for future renderer modernization, but today they should be treated as unsupported on the shipping OpenGL renderer.

## Current Workflow Status

openPREY exposes two console commands:

- `renderDocStatus`
- `renderDocCapture [frames]`

`renderDocCapture` arms a next-frame capture when openPREY is already running under RenderDoc. If `frames` is greater than `1`, it requests a multi-frame capture.

For the current renderer, the scripted wrapper intentionally fails fast unless you pass `-AllowUnsupported`. This avoids booting the game into a guaranteed unsupported capture attempt from VS Code tasks or ad-hoc command lines.

### Experimental Failure Reproduction

If you are explicitly testing future renderer modernization work and still want to reproduce the current limitation, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\debug\renderdoc_capture.ps1 -Mode SP -Map game/roadhouse -AllowUnsupported
```

or:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\debug\renderdoc_capture.ps1 -Mode MP -Map game/dmroadhouse -AllowUnsupported
```

The wrapper still launches `renderdoccmd capture`, starts openPREY from `.install/` in windowed mode, waits for the map to settle, then calls `renderDocCapture`. Captures are written under `fs_savepath/basepr/renderdoc/`.

On the current renderer, the launch is still expected to fail before first frame with the explicit RenderDoc compatibility error above. If no `.rdc` file is produced, the wrapper reports that limitation instead of silently succeeding.

## What To Inspect

When reviewing an older or future capture in RenderDoc, inspect the three SMAA passes in order:

- `postprocess/smaa_edge`
- `postprocess/smaa_weights`
- `postprocess/smaa_blend`

Expected bindings on a fixed build:

- `postprocess/smaa_edge`
  - Render target: `_postProcessAlbedo1`
  - Texture slot 0: `_postProcessAlbedo2`
- `postprocess/smaa_weights`
  - Render target: `_postProcessAlbedo0`
  - Texture slot 0: `_postProcessAlbedo1`
  - Texture slot 1: `_smaaArea`
  - Texture slot 2: `_smaaSearch`
- `postprocess/smaa_blend`
  - Render target: `_postProcessAlbedo1`
  - Texture slot 0: `_postProcessAlbedo2`
  - Texture slot 1: `_postProcessAlbedo0`
- `postprocess/copy_postprocess1`
  - Render target: `_postProcessAlbedo0`
  - Texture slot 0: `_postProcessAlbedo1`

If the blend pass ever samples the same texture it is rendering to, the build still contains a broken feedback loop.

## Release Package Audit

The compatibility entry point `tools/build/package_release.py` delegates to the canonical nightly packager and fails packaging if the generated openPREY PK4s are missing pack-specific required runtime files. `pak0.pk4` must include:

- `materials/postprocess_openq4.mtr`
- `glprogs/smaa_edge.vs`
- `glprogs/smaa_edge.fs`
- `glprogs/smaa_weights.vs`
- `glprogs/smaa_weights.fs`
- `glprogs/smaa_blend.vs`
- `glprogs/smaa_blend.fs`

`pak1.pk4` must include representative level-loadscreen assets such as:

- `gfx/guis/loadscreens/generic.dds`
- `gfx/guis/loadscreens/generic.tga`

This check is meant to catch packaging regressions in the postprocess stack before release artifacts ship.

## Temporary User Workaround

If you need to unblock an affected user on an older build:

```cfg
seta r_postAA 0
vid_restart
```
