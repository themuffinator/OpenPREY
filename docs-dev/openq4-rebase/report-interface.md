# Game/Engine Interface Analysis: openPREY (Prey 2006) vs current OpenQ4

Scope: what engine API surface the in-tree Prey game code requires, how current upstream OpenQ4 (d41186e4) hosts game code, and a requirements map for rebasing openPREY's engine onto it. All findings verified against the working tree at `E:\Repositories\openPREY` (fork point `e634332b` ≙ upstream `abb9a688`) and upstream ref `openq4-upstream/main` / checkout `E:\Repositories\OpenQ4`.

---

## 1. The engine API surface the Prey game code depends on

The Prey game code (`src/Prey`, 329 files; `src/game`, 138 Q4-SDK-derived files) consumes the engine through **two channels**:

### 1a. The `GetGameAPI` ABI — Prey SDK contract, `GAME_API_VERSION = 7`

Defined in `E:\Repositories\openPREY\src\game\Game.h` (retail Prey SDK shape):

- `gameImport_t` (v7): exactly 13 system pointers — `idSys, idCommon, idCmdSystem, idCVarSystem, idFileSystem, idNetworkSystem, idRenderSystem, idSoundSystem, idRenderModelManager, idUserInterfaceManager, idDeclManager, idAASFileManager, idCollisionModelManager` — plus an optional `hhProfiler *` behind `INGAME_PROFILER_ENABLED` (`src/preyengine/profiler.h`). **No `bse` member, no Raven heap members.**
- `gameExport_t`: `version, idGame*, idGameEdit*`. **No `rvGameLog`.**
- `idGame` is the *Prey* interface, materially different from Q4's:
  - `InitFromNewMap(..., idRenderWorld*, idSoundWorld*, ...)` and `InitFromSaveGame(..., idSoundWorld*, ...)` — **the engine must hand the game a sound world** (Q4's versions have no `idSoundWorld` parameter).
  - `RunFrame(const usercmd_t*)`, `ClientPrediction(clientNum, cmds)` — no catch-up/serverGameFrame/lastPredict parameters.
  - `SetUserInfo(clientNum, info, isClient, canModify)` — 4 args (Q4: 3).
  - `ServerClientConnect(clientNum)`, `ServerClientBegin(clientNum)`, `ServerWriteSnapshot(..., byte *clientInPVS, numPVSClients)`, `ClientReadSnapshot(...)` returning `void`.
  - `gameReturn_t` carries Prey HUD/health fields (`health, heartRate, stamina, combat`).
  - HumanHead-specific: `PrintMemInfo(MemInfo_t*)` (`MemInfo_t` in `src/framework/Common.h:134`), `PlayerIsDeathwalking()`, `GetTimePlayed()/ClearTimePlayed()`, `SelectTimeGroup/GetTimeGroupTime`.
- Engine call sites already re-aligned to these signatures in `src/framework/Session.cpp` (~3204–3246, 4487–4596), `src/framework/async/AsyncClient.cpp` (~806–1881), `src/framework/async/AsyncServer.cpp` (~742–2928).
- Loader: `idCommonLocal::LoadGameDLL` (`src/framework/Common.cpp:3310`) loads a **unified module** via a candidate list — `game_<arch>` preferred, legacy `gamex86/gamex64` aliases accepted — populates the 13-member v7 import struct, then `RefreshBSEBindings()` (engine-side only). Runtime layout: repo content dir `basepy/` (recently renamed), staged runtime game dir `.install/basepr/` with `game_x64.dll` (`meson.build:74`, `basepy/meson.build`, `TECHNICAL.md`).

### 1b. Shared in-tree headers compiled into the game module

195 of the game files include `../idlib/precompiled.h` (41 more via `../../idlib/precompiled.h`) — the classic monolithic surface. openPREY has modified/added **~4,650 lines across 48 game-visible engine headers/sources** since the fork. Verified Prey-specific requirements, by subsystem:

**idlib** (built twice: engine + `openprey_game_idlib` static lib linked into the game module):
- `src/idlib/BitMsg.h` (+51): `WriteBool/ReadBool`, `WriteVec3/ReadVec3`, `WriteString(s, maxLength, make7Bit)`, `SetDebugEntType`, `idBitMsgDelta::Init` read/write overloads, msg-queue helpers (`Add/WriteToMsg/ReadFromMsg/GetDirect`). Used by 134 game files.
- `src/idlib/Heap.h`: `idBlockAlloc` template gained a defaulted `byte memoryTag = MA_DEFAULT` so Prey's 2-arg instantiations compile (upstream requires 3 args; same issue class for `idDynamicBlockAlloc`).
- New files: `containers/PreyStack.h`, `math/prey_math.{h,cpp}`, `math/prey_interpolate.h`; additions to `Vector.h/.cpp`, `Extrapolate.h`, `Interpolate.h`, `Str`, `LangDict`, `Lib`.

**Collision (`src/cm/CollisionModel.h`, +65)**: `typedef idCollisionModel *cmHandle_t;` plus legacy wrapper methods Prey's `idClip`/game code calls: `LoadModel(name, precache)`, `SetupTrmModel(trm, material)`, `FreeMap()`, `GetModelName/Bounds/Contents/Vertex/Edge/Polygon`, `TrmFromModel(name, trm)`, `ContentsName`, `ModelInfo`, `AppendMap`, `WillUseAlreadyLoadedCollisionMap`, `DrawModel` (no-view-axis overload). Consumers: `src/game/physics/Clip.{h,cpp}`, `src/game/gamesys/TypeInfo.cpp`.

**Renderer public surface (`src/renderer/RenderWorld.h`, +102)**:
- `renderEntity_s` Prey fields: `const hhDeclBeam *declBeam`, `hhBeamNodes_t *beamNodes` (`MAX_BEAM_NODES=32`), `weaponDepthHack`, `onlyVisibleInSpirit`, `onlyInvisibleInSpirit`, `lowSkippable`, `eyeDistance`, `timeGroup`, `notInRenderDemos` (`_HH_RENDERDEMO_HACKS`).
- **`referenceSound` restored to `idSoundEmitter *`** in `renderEntity_s`/`renderLight_s` (upstream keeps `int` handle) — struct layout + sound-model divergence.
- `renderView_t::viewSpiritEntities`; `exitPortal_t::frac`; portal attributes reworked: `PS_BLOCK_SOUND` (replaces Raven `PS_BLOCK_GRAVITY` semantics).
- `idRenderWorld` virtuals added (default-stub style): game portals (`FindGamePortal, RegisterGamePortals, DrawGamePortals, GetGamePortalSrc/Dst, IsGamePortal`), sound areas (`NumSoundPortalsInArea, LevelInit/ShutdownSoundAreas, PrecalculateValidSoundAreas, ValidSoundArea, DistanceToSoundArea, MaxSoundAreaExtents, DrawValidSoundAreas, GetSoundPortal`), `GuiTrace(..., interactiveMask)`, `DebugClearLines/DebugClearPolygons`, `DemoSmokeEvent`.
- `SHADERPARM_MISC/ANY_DEFORM/ANY_DEFORM_PARM1/2/DISTANCE` constants and 20-value `DEFORMTYPE_*` enum (spirit/portal/deform effects).
- Engine-side implementations backing these: new `src/renderer/Model_hhBeam.cpp` (322 lines), `Model_prt.cpp` (Doom3 particle model, +178), plus large diffs in `draw_common.cpp` (+2168), `tr_subview.cpp` (+312), `draw_arb2.cpp` (+274), `tr_light.cpp` (+112), `tr_deform.cpp` (+105), `Material.cpp` (+402), `RenderSystem_init.cpp` (+196).

**Decl system (`src/framework/declManager.h`, +14)**: `declType_t` re-enables `DECL_FX`, `DECL_PARTICLE` and adds `DECL_BEAM`; `idDeclManager::FindBeam/BeamByIndex`, `SetInsideLevelLoad/GetInsideLevelLoad`. Restored/added engine files: `DeclFX.{h,cpp}` (+649), `DeclParticle.{h,cpp}` (+1748, Doom 3 particle decls), `DeclPreyBeam.{cpp}`/`declPreyBeam.h` (+323, `hhDeclBeam`). These implement the compat-plan mandate: **Doom3/Prey particle-FX paths instead of Q4 BSE** — grep confirms **zero** references to `rvBSEManager`/BSE in `src/Prey` + `src/game`.

**Sound (`src/sound/sound.h`, +130)**: `hhSoundShaderParmsModifier`, `SOUNDCLASS_NORMAL/VOICEDUCKER/SPIRITWALK/VOICE/MUSIC`, `SSF_VOICEAMPLITUDE` alias, `idSoundWorld` extensions (`ModifySound`, `GetSoundParms`, `CurrentAmplitude/CurrentVoiceAmplitude/CurrentShakeAmplitudeForPosition`, `PlaceListener(..., areaName)`, `RegisterLocation`, `ClearAreaLocations`, `SetSpiritWalkEffect`, `SetVoiceDucker`), subtitle API (`GetSubtitleIndex/SetSubtitleData/GetSubtitle/GetSubtitleList`), focus-mute API.

**Input (`src/framework/UsercmdGen.h`, +24)**: Prey button layout — `BUTTON_ATTACK_ALT = BIT(3)`, shifted `BUTTON_SCORES/MLOOK`, `buttons` stays **`byte`**; `USERCMD_ONE_OVER_HZ`.

**Other**: `framework/FileSystem.h` (+8), `framework/Common.h` (+28, `MemInfo_t` etc.), `sys/sys_public.h` (+48), `ui/UserInterface.h` (+6), `ui/ListGUI.h`, `aas/AASFile.h` (+1), `bse_api/BSEInterface.h` (engine-internal stub retained; included by precompiled.h), `preyengine/profiler.h` (new, exposed to both sides).

Hygiene check: no leakage of engine-only globals (`session->`, `sessLocal`, `tr.`) from game code — the surface above is complete.

---

## 2. How current upstream OpenQ4 hosts game code

Sources: `E:\Repositories\OpenQ4\TECHNICAL.md`, `E:\Repositories\OpenQ4\BUILDING.md` ("GameLibs Companion Repository"), upstream `meson.build`, `src/framework/Common.cpp:5388–5560`, staged header `E:\Repositories\OpenQ4\.tmp\gamelibs_stage\src\game\Game.h`.

**Source model — no game code in-tree.** Commit `4ff3d848` ("Stage game sources from OpenQ4-GameLibs") deleted `src/game`. Canonical game source lives in the companion repo `../openQ4-game` (`src/game` = SP, `src/mpgame` = MP; override via `OPENQ4_GAMELIBS_REPO`). At **configure time** `tools/build/stage_gamelibs.py` copies both trees into `.tmp/gamelibs_stage/` and writes a manifest with file hashes + git state; Meson refuses to build without it (`meson.build:317–373`).

**Header coupling trick.** `src/idlib/precompiled.h:403` still says `#include "../game/Game.h"`, resolved through `include_directories('.tmp/gamelibs_stage/src/game')` (`meson.build:1088–1096`) — the engine compiles against the *staged* game headers. A `game_idlib` is built per flavour (`game_idlib_library`, `game_idlib_library_mp`; the MP flavour resolves `../mpgame/Game_local.h`).

**Build/packaging.** Two modules — `game-sp_<arch>` and `game-mp_<arch>` (`shared_module`, `shared_library` on macOS; `Game.def`/`mpgame.def` on Windows) — installed into `.install/baseoq4/` (`content/baseoq4/meson.build`). SP loads `game-sp`, MP loads `game-mp`. Mods use `mod.json` manifests, may omit modules and fall back to `baseoq4/` modules.

**ABI.** `GAME_API_VERSION = 40`. `gameImport_t` = Q4 SDK shape **plus `rvBSEManager *bse`** (upstream reconstructed BSE lives in-engine at `src/bse`, statically linked into the client; `gameImport.bse = ::bse`) and optional `rvHeapArena/rvHeap` members. `gameExport_t` adds `rvGameLog *`. `idGame` is the big Q4/Raven interface, further extended upstream: bots (`SpawnPlayer(clientNum, isBot, botName)`, `ServerClientBegin(clientNum, isBot, botName)`, `GetRandomBotName`), repeater support (`Repeater*` family), `MenuFrame`, `HandleMainMenuCommands`, `RunFrame(cmds, activeEditors, lastCatchupFrame, serverGameFrame)`, `ServerWriteSnapshot(..., dword *clientInPVS, ..., lastSnapshotFrame)`, `ClientReadSnapshot(...) -> bool` with `readEntityInstances`, `ClientPrediction(..., lastPredictFrame, ClientStats_t*)`, BSE effect entry points (`PlayEffect`, `StartViewEffect`), no `idSoundWorld` in map init.

**Loader.** `idCommonLocal::LoadGameDLL` (`Common.cpp:5429`) with phase instrumentation (`GameModuleDiagnostics.h`: locate → binary load → resolve `GetGameAPI` → call → verify version → `game->Init()`), SP/MP module-base-name selection, `com_activeGameModule`/`com_nextGameModule` for runtime mod switching, macOS universal2 fallback names.

**New engine consumers of the game ABI.** `src/framework/async/MultiViewDemo.{h,cpp}` records/plays multi-client demos and embeds `GAME_API_VERSION` as its game schema version; a generated `savegame_compat_header` couples savegame compat to game builds.

**Renderer restructure (rebase-relevant).** On SDL3 Windows/Linux the client **sheds the static renderer and loads a `renderer-gl` module** (plus an in-progress Vulkan `renderer-vk` module) via `src/renderer/RenderModuleAPI.h` / `RendererModule.cpp`; `bse`, `imagetools`, `render_geo` are separate static libs. Renderer-visible structs (`renderEntity_t` etc.) now cross **three** binaries: client, renderer module, game module.

---

## 3. Requirements map: Prey game interface needs → status in current upstream → action for a rebased openPREY

Legend: **E** = engine-side patch (re-apply openPREY delta onto upstream), **G** = game-side adaptation, **S** = compat shim, **B** = build-system work.

| # | Prey game-code requirement | Status in current upstream OpenQ4 | Action for rebased openPREY |
|---|---|---|---|
| 1 | `gameImport_t` v7 (13 systems, no `bse`, optional `hhProfiler`) | **Restructured** — v40 struct with `bse` + Raven heap members | **E**: keep engine compiling against Prey `Game.h` via the staged-include mechanism; `LoadGameDLL` populates only the v7 fields (openPREY already did this — port that patch onto upstream's phase-instrumented loader, keeping upstream's diagnostics) |
| 2 | `GAME_API_VERSION = 7` | **Removed/replaced** (40) | **E**: version constant comes from the Prey `Game.h`; also feeds MultiViewDemo schema (see #17) |
| 3 | Prey `idGame` signatures incl. `idSoundWorld*` in `InitFromNewMap/InitFromSaveGame`, 4-arg `SetUserInfo`, plain `RunFrame/ClientPrediction/ServerWriteSnapshot/ClientReadSnapshot`, `gameReturn_t` HUD fields, `PrintMemInfo`, `PlayerIsDeathwalking`, `GetTimePlayed` | **Restructured** — upstream call sites use extended Q4 signatures (bots, repeater, catch-up frames) in `Session.cpp`, `Session_menu.cpp`, `AsyncClient.cpp`, `AsyncServer.cpp`, `MultiViewDemo.cpp` | **E**: re-apply openPREY's call-site conversion onto upstream's heavily evolved Session/Async code. This is a *manual merge hotspot* — upstream Session.cpp grew ~1,700 lines (savegame reliability, multiview, menu work). Repeater/bot-name call sites must be reduced to Prey arity or shimmed (**S**) with engine-local wrappers |
| 4 | Unified game module `game_<arch>` in `basepr/` (legacy `gamex86` aliases) | **Restructured** — split `game-sp_<arch>`/`game-mp_<arch>` in `baseoq4/`, SP/MP base-name selection, mod-switch cvars | **E+B**: keep upstream loader skeleton but restore openPREY's single-module candidate list; neutralize SP/MP selection (both map to `game_<arch>`). Keep `mod.json` gating if desired |
| 5 | Game sources currently vendored in-tree (`src/game`, `src/Prey`, `src/preyengine`) synced from `../OpenPrey-game` via `tools/build/sync_gamelibs.ps1` | **Restructured** — configure-time staging from companion repo with hash manifest; engine never carries game sources | **B (recommended)**: adopt upstream's `stage_gamelibs.py` model — stage `src/game` + `src/Prey` + `src/preyengine` + shared headers from `OpenPrey-game` into `.tmp/gamelibs_stage/`, one staged include dir, one `game_idlib` flavour (Prey is unified — no mpgame flavour). This removes the in-tree/companion dual-maintenance the compat plan flags |
| 6 | `idBitMsg` helpers (`WriteBool/ReadBool/WriteVec3/ReadVec3`, `WriteString(make7Bit)`, `SetDebugEntType`, `idBitMsgDelta::Init`, queue helpers) | **Absent** upstream (never existed there) | **E/S**: re-apply `src/idlib/BitMsg.h` +51-line inline shim block — additive, low conflict risk |
| 7 | `idBlockAlloc`/`idDynamicBlockAlloc` 2-/3-arg instantiations | **Present-but-incompatible** — upstream templates require explicit `memoryTag` (and upstream reworked `Heap/rvMemSys` further) | **E/S**: re-apply defaulted `memoryTag = MA_DEFAULT` template args in upstream `Heap.h` |
| 8 | Prey idlib additions (`PreyStack.h`, `prey_math`, `prey_interpolate`, `Vector/Extrapolate/Interpolate/Str/LangDict` deltas) | **Absent**; upstream idlib itself evolved substantially (new `Simd_SSE2`, `Math.h` rework, hashing/CRC changes) | **E**: re-apply additive files verbatim; hand-merge the shared-file deltas (`Vector.h`, `Math.h`, `Str.cpp`, `LangDict.cpp`) against upstream's rewrites |
| 9 | `cmHandle_t = idCollisionModel*` + legacy `idCollisionModelManager` wrappers (`LoadModel(name,precache)`, `SetupTrmModel`, `GetModel*`, `TrmFromModel`, `FreeMap()`, `ContentsName`, `ModelInfo`, `AppendMap`, ...) | **Absent** — upstream kept the named-map class-based API only (moderate churn since fork) | **E/S**: re-apply the +65-line wrapper block in `cm/CollisionModel.h`; mostly inline forwarding, expect a clean port |
| 10 | `renderEntity_s` Prey fields (`declBeam`, `beamNodes`, `weaponDepthHack`, `onlyVisibleInSpirit/onlyInvisibleInSpirit`, `lowSkippable`, `eyeDistance`, `timeGroup`, `notInRenderDemos`) + `viewSpiritEntities`, `PS_BLOCK_SOUND`, `SHADERPARM_*`, `DEFORMTYPE_*` | **Absent**; and the struct now crosses client + renderer-gl/vk modules + game module | **E**: re-apply header fields *and* re-port the consuming renderer code (spirit filtering in `tr_light.cpp`, depth hack, beam/particle models, deforms, subview/portal work) onto upstream's modularized renderer. **This is the largest engineering item** — upstream renderer diff is ~150k lines with a new module ABI (`RenderModuleAPI.h`); all three binaries must be rebuilt from the same patched headers |
| 11 | `referenceSound` as `idSoundEmitter*` in `renderEntity_s`/`renderLight_s`/`refSound_t` | **Restructured** — upstream uses `int` handle model throughout sound/renderer | **E** (keep openPREY's pointer restore, re-applied across upstream's evolved sound/renderer code) or **G** (convert Prey game + hhDecl paths to handles). openPREY already chose the pointer restore; re-applying it is consistent but touches upstream's demo/multiview sound recording paths — flag for careful review |
| 12 | `idRenderWorld` game-portal + sound-area virtuals, `GuiTrace(interactiveMask)`, `DebugClearLines/Polygons`, `DemoSmokeEvent` | **Absent** | **E/S**: re-apply — openPREY implemented these as default-stub virtuals on the interface (real portal work partially in `RenderWorld_portals.cpp`/`tr_subview.cpp`), so the interface part is additive; implementations ride along with #10 |
| 13 | `DECL_FX`/`DECL_PARTICLE` enabled + `DECL_BEAM`; `DeclFX/DeclParticle/DeclPreyBeam` engine classes; `FindBeam/BeamByIndex`, `SetInsideLevelLoad` | **Absent/disabled** — upstream keeps them commented out in `declType_t` and has no such decl classes; upstream also flipped `Q4SDK_MD5R` and `RV_SINGLE_DECL_FILE` **on** in `precompiled.h` (behavioral divergence from Prey expectations — verify both) | **E**: re-apply enum entries (positional enum — keep ordering identical to openPREY's to protect any serialized indices), add the four decl source files back, merge `DeclManager.cpp` registration; audit the `Q4SDK_MD5R`/`RV_SINGLE_DECL_FILE` defines for Prey correctness |
| 14 | Prey sound surface (`hhSoundShaderParmsModifier`, `SOUNDCLASS_*`, spirit-walk/voice-ducker, `PlaceListener(areaName)`, subtitles, `CurrentVoiceAmplitude`, sound-area propagation hooks) | **Absent**; upstream `sound.h` evolved independently (+81: `SSF_VO/SSF_MUSIC` runtime flags, `rvSoundShaderEdit`, `UpdateEmitter` velocity overload, `OPENQ4_SOUND_HAS_SOUNDWORLD_SKIP`) | **E**: manual merge of both deltas into one `idSoundWorld`/`idSoundShader` interface — same-file, same-class edits on both sides; vtable ordering must stay consistent across engine and game module (in-tree compile makes this safe, but merge carefully) |
| 15 | Prey `usercmd_t`: `byte buttons`, `BUTTON_ATTACK_ALT=BIT(3)` layout | **Restructured** — upstream widened to `short buttons`, added `BUTTON_WEAPONWHEEL`, impulses to 127, new `idUsercmdGen` virtuals (`ResolveImpulseCommand`, `TriggerImpulse`, `GetPresentationViewDelta`) | **E**: keep Prey layout (network/demo compatibility); adopt upstream's new `idUsercmdGen` virtuals (engine-internal, game only reads the struct). Decide explicitly whether to keep upstream's 16-bit buttons with Prey bit assignments or revert to `byte` — openPREY currently uses `byte` |
| 16 | No BSE dependency; Doom3/Prey particle & FX runtime instead | **Restructured** — upstream ships a reconstructed BSE runtime (`src/bse`) statically linked into the client, passed via `gameImport.bse`, integrated with soft particles/screen effects | **E (policy)**: keep `src/bse` compiled-but-dormant (Prey import v7 has no `bse` field; nothing binds it) rather than excising it from upstream's renderer/sound integration; openPREY's current `src/bse_api` stub + `RefreshBSEBindings()` pattern maps onto this. Long-term optional removal |
| 17 | `MemInfo_t`, `hhProfiler` (`preyengine/profiler.h`), misc `Common.h`/`FileSystem.h`/`sys_public.h` deltas | Partially present (`MemInfo_t` exists at `Common.h:158`); profiler **absent** | **E**: re-apply small deltas; keep `preyengine/profiler.h` in the shared staged-header set |
| 18 | New upstream game-ABI consumers: MultiViewDemo, `savegame_compat_header`, ASan staged-game lanes, mod-manifest gating | **New upstream features**, written against Q4 v40 semantics | **E/G decision per feature**: either adapt (MultiViewDemo schema = Prey v7; snapshot calls need Prey arity) or gate off initially (`build option`), re-enabling after core parity. Do not silently keep them calling non-existent Q4 methods |

### Merge-conflict hotspots (both sides modified the same files since the fork)

`src/idlib/precompiled.h`, `src/sound/sound.h`, `src/renderer/RenderWorld.h`, `src/renderer/RenderSystem.h` (+165 upstream / +11 openPREY), `src/renderer/Material.h` (+156 / +94), `src/renderer/Model.h`, `src/framework/UsercmdGen.h`, `src/framework/Common.h`, `src/sys/sys_public.h`, `src/framework/Common.cpp` (loader), `src/framework/Session.cpp` + `src/framework/async/*` (call sites), and the entire renderer implementation (openPREY +~4,000 lines Prey features vs upstream's module split + Vulkan work).

### Recommended rebase sequence (interface-driven)

1. **B**: adopt upstream's staging/gamelibs build model, pointed at `OpenPrey-game`, unified single-module output (`game_<arch>` → `basepr/`), Prey `Game.h` as the staged contract header.
2. **E**: port the loader patch (v7 import population, unified candidate names) onto upstream's phase-instrumented `LoadGameDLL`; keep diagnostics.
3. **E**: re-apply additive compat shims first (BitMsg, Heap template defaults, cm wrappers, prey_math/PreyStack, decl enum + decl classes, profiler header) — low-risk, unblocks game-module compilation.
4. **E**: hand-merge the contested interface headers (sound.h, RenderWorld.h, UsercmdGen.h, Common.h) combining both sides' deltas.
5. **E**: re-apply engine→game call-site signatures across upstream Session/Async/MultiViewDemo, gating or adapting upstream-only features (bots pass-through already exists in openPREY's Prey-arity form).
6. **E (largest)**: re-port Prey renderer features (hhBeam, Doom3 prt, spirit visibility, deforms, portal/subview, weapon depth hack, `idSoundEmitter*` referenceSound) onto the modularized renderer — treat as its own workstream against `RenderModuleAPI.h`.

Key reference files:
- openPREY contract: `E:\Repositories\openPREY\src\game\Game.h`, loader `E:\Repositories\openPREY\src\framework\Common.cpp` (line ~3310), build `E:\Repositories\openPREY\meson.build` + `E:\Repositories\openPREY\basepy\meson.build`, sync `E:\Repositories\openPREY\tools\build\sync_gamelibs.ps1`
- Upstream contract: `E:\Repositories\OpenQ4\.tmp\gamelibs_stage\src\game\Game.h` (v40), loader `E:\Repositories\OpenQ4\src\framework\Common.cpp` (lines ~5388–5560), staging `E:\Repositories\OpenQ4\tools\build\stage_gamelibs.py`, module targets `E:\Repositories\OpenQ4\content\baseoq4\meson.build`, docs `E:\Repositories\OpenQ4\BUILDING.md` ("GameLibs Companion Repository") and `E:\Repositories\OpenQ4\TECHNICAL.md` ("SDK and Game Library")
- Known-drift docs (validated and extended above): `E:\Repositories\openPREY\docs-dev\prey-gamelibs-compatibility-plan.md`, `E:\Repositories\openPREY\docs-dev\porting-baseline.md`
