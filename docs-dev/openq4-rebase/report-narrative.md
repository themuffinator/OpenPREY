# openPREY Port Narrative (fork `e634332b` → working tree, repo `E:\Repositories\openPREY`, branch `new-prey`)

## 1. Shape of the history

58 commits between the fork point (`e634332b`, 2026-02-19, byte-identical to OpenQ4 `abb9a688`) and HEAD (`968ac51b`, 2026-03-30), plus ~96 uncommitted modified files (+3,893/−757 lines vs HEAD) and a set of untracked new files that together constitute a **fourth, in-flight phase**. The history falls into four distinct phases:

| Phase | When | Character |
|---|---|---|
| 1. Conversion mega-import | 2026-02-28 (3 commits) | One giant commit turns OpenQ4 into OpenPrey; game code + assets replaced wholesale |
| 2. Parity & feature blitz | 2026-03-03 → 03-09 (~40 commits) | Rapid hand-authored commits: retail-Prey parity fixes, new post-FX, subtitles, audio, platform ports |
| 3. Agent-driven infra PRs | 2026-03-30 (PRs #17–#20) | Copilot/Codex sessions: macOS linker fix, static CRT, docs restructure, base-folder rename |
| 4. Uncommitted working tree | now | Third rebrand pass (openPREY casing), light-grid GI bake system, HDR pipeline rewrite, trigger sweep fix |

### Phase 1 — the conversion commit (misleadingly titled)

- **`486a87e5` "misc" (2026-02-28)** is the fork-defining commit despite its one-word message:
  - `src/framework/licensee.h`: `PROJECT_NAME` OpenQ4→OpenPrey; game dirs `q4base`/`q4mp`/`openbase` → `base`/`base`/`openprey` (retail Prey ships everything in `base/`, no separate MP dir); `CDKEY_FILE` `quake4key`→`preykey`.
  - Imports **~95k lines of HumanHead Prey SDK gameplay code** as `E:\Repositories\openPREY\src\Prey\` (hh* classes) plus `src\preyengine\` (profiler.h), and **replaces Quake 4's Raven game code** in `src\game\` (−150k/+44k lines — now id Software 2004 / Doom 3-derived with HUMANHEAD markers).
  - `openbase/` asset overlay renamed to `openprey/` with ~350k lines of new assets; framework +3.8k lines (idGame API alignment to Prey's `Game.h` contract); docs rewritten.
- `2e58323b` bulk-imports the OpenPrey GUI system (bloom shaders, .gui layouts, hundreds of textures); `6fccedfc` starts color correction/HDR/menu work.

### Phase 2 — March blitz (well-scoped commits, a few "misc" landmines)

Highlights in chronological order: `42fe2818` (Prey icons/refs; accidentally commits repro cfgs and stdout/stderr logs), `fd9a7ce0` (FOV/scripting/audio refactor), `f826e0ec` (x64 determinism + DDS loader), `7fbb1d1d` (ARB fragment programs for Prey-style programmable stages), `a876dee7` (subtitles + profanity censor), `8e55c7d1` (Prey beam renderer), `e5cd21d1`/`dc4e3d9c`/`2f4e1069` (menu sound routing, save/quickload GUI, shaderlevel tokens), `58f5562e` (mirror clip parity work + `references/`), **`870ae405` "misc commit"** (another landmine: moves ~519k lines out of `.install/` and ~572k into `openprey/` — an asset-tree restructure hidden behind a two-word message), `fce5d2bc` (retail portal/skybox subview rendering + UI text-spline effects), `c3d93369`/`b4823b66`/`b6c2cde2` (load music, dB volumes, portal refinements), `4263338e`→`0b2e6f18` (Linux/macOS Meson build, macOS API modernization, include-case fixes), `332e5166`→`98e3a303` (aspect-correct GUI expansion, HDR tonemapping, focus muting, CRT effect, SSAO), `d3bab3d6` (native-fullscreen `r_mode -2`, assorted engine fixes).

### Phase 3 — agent PRs (2026-03-30)

PR #17 `983079ed` (macOS `Sys_IsGameWindowFocused` stub), PR #18 `2a3fd053` (static MSVC CRT), PR #19 `81d9c575` (BUILDING.md/TECHNICAL.md/README restructure to OpenQ4 style), PR #20 `c0aa046a`+`968ac51b` (base folder rename). **Note a quiet inconsistency**: PR #20's title says openprey→`basepy`, but the final commit `968ac51b` flips `OPENPREY_GAMEDIR` in licensee.h from `basepy` to **`basepr`** while the tracked asset folder remains `basepy/`. `968ac51b` also committed ~6.8M lines under `.install/` (staged install tree including built binaries — repo-hygiene problem for any rebase).

### Phase 4 — the uncommitted working tree (~96 files, +3.9k lines, plus untracked files)

Four independent efforts in flight simultaneously (see themes below): (a) third rebrand pass OpenPrey→**openPREY** casing across ~40 files; (b) a brand-new **light-grid GI bake system**; (c) an **HDR/bloom pipeline rewrite**; (d) a **trigger swept-hull detection fix**. Also: deletion of previously committed `.install/` binaries/icons, `src/sys/win32/rc/OpenPreyVersion.rc` → untracked `openprey_version.rc`, and `basepy/meson.build` simplified to `install_subdir`.

---

## 2. Cross-cutting themes

### A. Branding/rename passes (port preference) — three waves
1. OpenQ4→OpenPrey: `486a87e5` (licensee.h) + `a7ad6593` (Linux savepath `~/.local/share/openprey`, macOS paths/panel).
2. Doc/tooling normalization: `81d9c575` (PR #19).
3. **In flight, uncommitted**: OpenPrey→**openPREY** casing in `E:\Repositories\openPREY\src\framework\licensee.h` (PROJECT_NAME, window/console classes, CD_BASEDIR, macOS `/Applications/openPREY`), CI workflow artifact names, `.vscode\tasks.json`, `meson.build`, docs, TODO. Includes a deliberate **config migration shim**: `CONFIG_FILE openPREYConfig.cfg` with `INTERIM_CONFIG_FILE`/`LEGACY_CONFIG_FILE` read-only fallbacks for configs written under earlier casings. TODO.md explicitly flags "Confirm save/config path behavior after openPREY rebrand changes" — i.e. the author knows this is risky and unfinished.

### B. Base-folder rename chain (mixed: `base` is a Prey requirement; overlay name is preference)
`openbase` → `openprey` (`486a87e5`) → `basepy` (`c0aa046a`, PR #20) → **`basepr`** (`968ac51b`, HEAD). Retail Prey's data dir is `base/` (`BASE_GAMEDIR`), and the port's overlay dir is `OPENPREY_GAMEDIR`. **Half-finished at HEAD**: `meson.build` line 74 installs to `basepr` but line 465 still does `subdir('basepy')`; the tracked asset tree is still `basepy/`; an untracked `basepr\maps\game\` exists (bake outputs); TODO says "keep `basepr` as the sole namespace". Any rebase must treat basepy/basepr/openprey as one moving rename.

### C. Savepath / CD-key handling (Prey requirement + modern-path hygiene)
- `CDKEY_FILE`=`preykey` since `486a87e5`; **`523b0bae`** moved CD-key read/write from basepath to **fs_savepath** via `fileSystem->BuildOSPath` with explicit file IO and error prints, plus MSG_CDKEY GUI visibility fixes.
- Uncommitted: new `exec_savepath` console command (`src\framework\CmdSystem.cpp`), `openPREY_ExecConfigFromSavePath` helper, log files written to `logs/openPREY_%Y%m%d_%H%M%S.log` in savepath, and an extra `WriteConfiguration()` call so late-frame menu cvar changes persist. Untracked `.install\base\` contains `preykey`, `logs/`, and repro cfgs (test staging).

### D. Unified game module model (Prey requirement: retail shipped one `gamex86.dll`)
- Fork-point OpenQ4 selected split `game_sp`/`game_mp` DLLs (`com_activeGameModule`). openPREY's `meson.build` now builds **one `game_<arch>` module** when `build_games=true`; uncommitted `src\framework\Common.cpp` adds `openPREY_IsValidGameModuleName`/`SelectGameModuleBaseName`/`BuildGameModuleCandidateList` mapping legacy names (`game`, `game_sp`, `game_mp`, `gamex86`, `gamex64`) onto the unified module with a candidate search order.
- `E:\Repositories\openPREY\docs-dev\prey-gamelibs-compatibility-plan.md` documents a **companion repo** (`../OpenPrey-game`, legacy `PREY.sln`/`2005game.vcproj`) sync model and enumerates the open compile blockers as of 2026-02-20: `gameImport_t` drift, idlib template/`idEntityPtr` drift, missing `idBitMsg` helpers (`WriteBool`/`ReadVec3` etc.), `cmHandle_t`/clip-model drift, missing `renderEntity_s::weaponDepthHack`, include-ordering issues, and effects-system parity. Engine-only builds pass with `-Dbuild_games=false`. TODO items ("Integrate Prey gameplay trees from companion repo", "Resolve remaining game-library compile blockers") are still open — **this whole workstream is explicitly unfinished**, though the in-tree `src\Prey` code clearly runs (maps are launched for validation).

### E. Prey decl types (Prey requirement)
`src\framework\DeclManager.cpp` diff vs fork: registers **`DECL_BEAM`** (`hhDeclBeam`, `.beam` files under `beams/`, `listBeams` command) and **re-enables `DECL_FX` and `DECL_PARTICLE`** which OpenQ4 had commented out (Q4 used Raven's BSE effects; Prey uses Doom 3-era particles/FX plus its own beams). Companion renderer work: `8e55c7d1` adds `hhRenderModelBeam`. TODO longer-term: "Reinstate Doom 3/Prey particle-system behavior" — particle parity is acknowledged incomplete (current tree still carries BSE-era paths, see `src\bse_api\`).

### F. Subtitle system + profanity censor (Prey requirement — retail feature)
`a876dee7`: Session `guiSubtitles` + `ShowSubtitle/HideSubtitle`; sound-shader parsing registers subtitle tables with backend/frontend queues and `listSubtitles`; `com_profanity` drives both **LangDict text replacement** and **delayed audio fade-out censorship** over authored profanity segments. `332e5166` aligns subtitle GUIs to the screen bottom under aspect expansion; `8e55c7d1` nulls `guiSubtitles` on shutdown.

### G. FOV / scripting / audio refactors (mixed)
- **FOV** (`fd9a7ce0`, port modernization): removed `r_aspectRatio`, `CalcFov` rewritten from actual render resolution, legacy zoom helpers dropped; `327da28a` removed the aspect-ratio menu UI.
- **Scripting/x64** (`fd9a7ce0`, `f826e0ec`, requirement): `hhThread::PushParm` to `intptr_t`, script-compiler 64-bit temp fix, doubled script stack (`8e55c7d1`).
- **Audio** (Prey behavior + QoL): `b4823b66` dB volume cvars (`s_volume_dB`) with linear/dB alias syncing for menu sliders; `e5cd21d1`/`c3d93369` menu-music routing and retail load-music behavior (incl. suppressing default beeps for missing `snd_*`); `b4ad72b6` focus-based muting (port QoL, distinguishes explicit vs focus mute).

### H. Color correction / HDR / bloom / post-FX (port preference — not retail)
Committed: `6fccedfc` (initial), `823c6c8e` (ACES filmic tonemap, r_hdrWhitePoint/Lift/PostGamma/Gain/Vibrance), `884e8a05` (skip bloom on fullscreen menu views), `07857b21`+`5f73fbe8` (CRT effect + menu option), `98e3a303` (SSAO + Post FX menu page, framebuffer-blit depth copy).
**Uncommitted rewrite** (the largest in-flight chunk): `src\renderer\draw_common.cpp` (+1,493) builds an **FMT_RGBA16F HDR scene target** (`ImageOpts.h`, `Image_intrinsic.cpp` switch `_currentRender` etc. to RGBA16F), a **multi-level bloom pyramid** with dedicated untracked shaders (`basepy\glprogs\openprey_bloom_extract/downsample/blur.*`, `openprey_hdr_luminance.*`), **auto-exposure** via a log-luminance downsample chain with adaptation speeds, sRGB framebuffer control, and ~20 new cvars (`r_bloomThreshold/SoftKnee/MipCount`, `r_hdrSceneTarget/ToneMap/AutoExposure/KeyValue/Min-MaxExposure/AdaptUp-DownSpeed/HighlightDesaturation`, …) in `RenderSystem_init.cpp`. The old single-pass `openprey_bloom.fs` is modified in place.

### I. Light-grid GI bake system (entirely new, entirely uncommitted — port enhancement)
- Untracked `E:\Repositories\openPREY\src\renderer\RenderWorld_lightgrid.cpp` (Doom 3 GPL header; Quake-3-style light grid with irradiance-probe atlases, versioned `LGRID` files, worker threads, async readback — closely resembling RBDOOM-3-BFG's approach), new `LightGrid` class in `RenderWorld_local.h`, load path in `RenderWorld_load.cpp`, `RB_STD_LightGridIndirect` render stage in `draw_common.cpp` with untracked `lightgrid_indirect.vs/fs`, debug draw in `tr_rendertools.cpp`.
- `src\framework\Session.cpp` (+975 uncommitted) gains a full **`bakeLightGrids` command suite** (~25 `Session_*LightGrid*` helpers: map lists incl. MP maps, output/atlas path management, artifact pruning, bake orchestration) plus `Session_openPREYStartSingleplayer_f`. Untracked `basepr\maps\game\` holds bake outputs. This is mid-development: new source files aren't even `git add`ed yet.

### J. Retail mirror/portal clip parity (Prey requirement — disassembly-driven)
Documented in `E:\Repositories\openPREY\references\retail_mirror_clip_parity_plan.md` (added `58f5562e`, updated 2026-03-04): retail `PREY.exe` disassembly findings on mirror subview construction, depth-fill clip caching, and `skipClip` being parse-only metadata. Status: non-retail `MF_SKIPCLIP` runtime wiring removed; retail `surf->space` clip-update gate restored; **remaining gap explicitly flagged** — openPREY currently disables the mirror clip plane (`numClipPlanes = 0`) as a stability workaround where retail sets 1; follow-up must reconstruct retail clip-side math. Related retail-subview work: `fce5d2bc` (DI_PORTAL_RENDER/DI_SKYBOX_RENDER, `portalRenderMap`/`skyboxRenderMap`, portal camera transforms, interaction alpha-test plumbing), `b6c2cde2` (portal remote-eye offset, distance culling), and an uncommitted `tr_subview.cpp` guard making portal-sky renders single-pass per frame "as in retail PREY".

### K. Retail UI/GUI parity + widescreen expansion (mixed)
`332e5166` (aspect-correct GUI background edge tiles, `DrawMaterialUV`, loading-splash edge slices, 3×3 TGA tile extractor tool), `dc4e3d9c` (save/quickload GUI, `MaterialKeyForBinding` key-bind tip textures), `d6de452b` (GuiTrace dynamic-model handling per retail), `2f4e1069` (shaderlevelN/shaderfallbackN tokens, `lightWholeMesh` no-op), `fce5d2bc` (retail text-spline/credits effects). Uncommitted: Session now **generates expanded loading-background images at runtime** (resample/replicate-edge helpers, `Session_PrepareExpandedLoadingBackground`), with matching `basepy\guis\map\loading.gui` var renames (`gui::image` → `gui::loading_bkgnd*`) and `mainmenu.gui` tweaks.

### L. Platform/build/CI (port infrastructure)
`4263338e` (Linux/macOS Meson + nightly matrix), `dbb4c911` (3-job nightly workflow, changelog generator, pak0.pk4 overlay bundling, macOS .app), `30a100d9`–`0b2e6f18` (macOS modernization: Cocoa UTF-8 APIs, CGL renderer queries, GL core calls, clipboard guards), `12cfd95f`/`21196280` (include-case, `uintptr_t`, SIMD alignment — case-sensitive-FS and x64 correctness), `2a3fd053` (static MSVC CRT). Uncommitted: CI artifact names re-cased to openPREY, `.vscode\launch.json` +578 lines of per-map launch configs.

### M. Gameplay correctness / trigger reliability (in flight)
`f826e0ec` added an x64 bounds-overlap fallback for player trigger detection; the **uncommitted** work replaces that heuristic: `src\game\Entity.cpp` now sweeps the actual player clip model via `gameLocal.clip.TranslationModel` against the trigger ("thin hallway triggers remain reliable"), and `src\Prey\game_trigger.cpp/.h` add `acceptSweptTouch` so confirmed sweep hits fire immediately. Untracked repro cfgs (`repro_roadhouse_trigger_timing.cfg` in `.install\base\`) show this is still under validation.

---

## 3. Half-finished / hazard list (most relevant to the rebase)

1. **Uncommitted tree is four unrelated efforts at once** — openPREY casing rebrand, light-grid GI (with untracked source files `RenderWorld_lightgrid.cpp`, 9 shader files, `openprey_version.rc`), HDR/bloom rewrite, trigger sweep fix. None can be dropped without losing work; `Session.cpp` (+975) and `draw_common.cpp` (+1,493) will conflict maximally with upstream OpenQ4 renderer/session evolution.
2. **basepy vs basepr split-brain at HEAD**: code and meson install dir say `basepr`; tracked asset folder and `subdir()` say `basepy`; untracked `basepr\` exists. Third rename in the chain, incomplete.
3. **Casing rebrand risks user data**: window-class names, config filename, macOS/Linux paths all change; licensee.h ships INTERIM/LEGACY config fallbacks but TODO still lists "Confirm save/config path behavior" as open.
4. **Mirror clip plane parity gap**: mirror clip plane deliberately disabled (`numClipPlanes=0` workaround) pending reconstruction of retail math — documented in `references\retail_mirror_clip_parity_plan.md`.
5. **Game-module compilation**: per `docs-dev\prey-gamelibs-compatibility-plan.md` + TODO, companion-repo gameplay-tree integration and the listed idlib/idBitMsg/cmHandle_t/renderEntity blockers remain open; only engine-only builds are declared green there (doc may be partially stale — in-tree game demonstrably runs).
6. **Particle/FX parity** acknowledged incomplete (TODO: "Reinstate Doom 3/Prey particle-system behavior"); BSE-era code (`src\bse_api\`) still present.
7. **Repo hygiene**: `968ac51b` committed a ~6.8M-line `.install\` staging tree including built binaries and icons; the working tree deletes them (`.install\OpenPrey-client_x64`, `-ded_x64`, `.icns`, `.ico`). Earlier commits (`42fe2818`) committed and later removed repro cfgs/stdout logs. Two mega-commits titled "misc"/"misc commit" (`486a87e5`, `870ae405`) hide the fork conversion and a ~1M-line asset restructure — history is not a reliable guide to intent without diffing.
8. **Open TODO.md items** (E:\Repositories\openPREY\TODO.md): startup-warning audit under stock Prey assets, SP/MP launch defaults under unified module loading, `fs_basepath` CD-era install auto-detection, PK4 checksum baseline, naming cleanup, runtime validation matrix, and keeping `basepr` as the sole namespace.
