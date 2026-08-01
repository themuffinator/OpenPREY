# openPREY Engine Rebase Plan: onto OpenQ4 `d41186e4`

Status: the rebase implementation is complete (2026-08-01). It was developed on
`prey-on-oq4`; the local `new-prey` branch is cut over to the closing head while the
`pre-oq4-rebase` tag preserves the former branch tip. Windows full, Windows engine-only,
and Linux build matrices pass, and representative retail, fixture, package, and validator
evidence is recorded below. Supporting change-by-change
analysis lives in this directory — six subsystem catalogs (137 dispositioned changes), a
game/engine interface map, an upstream architecture survey, a port-history narrative, and a
coverage audit. The execution-level companion —
[`implementation.md`](implementation.md) — breaks this plan
into ordered work packages with concrete steps, commands, acceptance gates, and a full
item→work-package index. Actual dispositions and verification state live in
[`status-ledger.md`](status-ledger.md).

## 1. Baseline facts (initial analysis snapshot)

- **Fork point is exact.** openPREY's `e634332b` ("Initial commit", 2026-02-19) is
  byte-identical in source to openQ4 `abb9a688` (2026-02-18); only 42 Quake 4 asset files
  were stripped. Both refs exist in this repo (`openq4-upstream` remote). Every diff and
  3-way merge in this plan is anchored on that pair.
- **Upstream since fork:** 575 commits, ~282k lines changed in `src/`. Renderer overhauled
  three times and split into loadable modules (`renderer-gl`/`renderer-vk`, versioned C ABI
  in `RenderModuleAPI.h`, v7 at the selected upstream base; rebased openPREY is now v9);
  `src/game` deleted in favor of companion-repo
  staging (`openQ4-game` + `tools/build/stage_gamelibs.py`, split `game-sp`/`game-mp`
  modules); BSE integrated in-tree (`src/bse`); new `src/imagetools` and `src/render_geo`
  static libs; SDL3 default on all platforms; savegame format rewritten (version 1834,
  stamped + preflight-validated); new `.mvd` multi-view demo system + bots; content moved to
  `content/baseoq4/pak0|pak1` with build-time pak assembly and `mod.json` manifests; CI
  rebuilt around `manual-release`/`push-verification`/`commit-validation` (nightly workflow
  deleted).
- **Pre-rebase openPREY input snapshot:** 58 commits plus ~96 uncommitted files. Its
  engine-side delta was ~15k lines across renderer/framework/sound/ui/idlib/cm/sys, plus
  additive Prey game trees (`src/Prey` 329 files, `src/game` 138 files,
  `src/preyengine`). Those game sources are now canonical in `OpenPrey-game` and are
  staged at configure time; they are not an in-repository `src/game` mirror.
- **Cross-pollination is decisive.** Both repos share a maintainer, and a large share of
  openPREY's engine work (post-FX stack, light-grid GI, bakeLightGrids, canvas expansion,
  cursor bounds, macOS/Linux bring-up, 64-bit portability, DXGI vram, focus-window
  plumbing, DDS loading, English lang fallback…) already exists upstream in more evolved
  form. Reapplying those would create duplicate, worse implementations.

## 2. Initial planning disposition summary (137 cataloged changes)

| Disposition | Count | Meaning |
|---|---|---|
| reapply-clean | 47 | Target code still similar upstream; ports with minor fixup |
| reapply-rework | 52 | Approach survives; must be re-seated into rewritten upstream code |
| conflict-major | 7 | Upstream restructured the area; needs redesign, not a merge |
| obsolete-upstream | 29 | Upstream now has an equivalent or better mechanism — **drop** |
| drop | 2 | No longer needed at all |

These counts preserve the initial catalog analysis. Execution reclassified several items —
most notably `corelibs-23-aas-107-robustness`, which was reworked and ported instead of
dropped. The authoritative final outcome is **103 PORTED / 34 DROPPED / 0 wholly
DEFERRED**; partial backend and feature deferrals are listed separately in the status
ledger.

The seven conflict-major items are the backbone of the schedule: the Prey game-API
contract at engine call sites (FW-12), the savegame/quickload framework (FW-24), the
unified game module + build pipeline (sys-build ×2, assets-10), the CI/release rebuild,
and the spline/credits text effect vs the new TTF font pipeline (ui-02).

## 3. Strategy

### 3.1 Direction: fresh branch from upstream, curated replay — not a mechanical rebase

Replaying the 58 openPREY commits onto upstream (`git rebase --onto`) is technically
possible (the identical fork trees give git a real merge base) but would be
counterproductive: two of the commits are ~1M-line "misc" mega-commits, four unrelated
efforts sit uncommitted in the working tree, and ~31 of 137 changes must be consciously
dropped rather than merged. Instead:

1. Branch from `openq4-upstream/main` (call it `prey-on-oq4`).
2. Reapply work **by workstream, in dependency order**, using the disposition ledger
   (§6) as the worklist — one catalog ID per commit wherever practical, with the ID in
   the commit message.
3. Use per-file 3-way merges (`git diff abb9a688..<ref> -- <file>`, `git merge-file`)
   as mechanical assists for reapply-clean items; hand-port the rest.

### 3.2 Upstream-first: shrink the fork before growing it

Upstream's own `TODO.md` ends with: *"Project may benefit from catering towards multiple
idTech4 titles, to include: Doom 3, Doom 3: BFG, Prey, ETQW."* Since the same person
maintains both repos, the single highest-leverage move is to land **title-agnostic
engine hooks in openQ4 first**, so openPREY's permanent delta contains only genuinely
Prey-specific code. Strong candidates (all currently dispositioned reapply-*):

- Doom 3 `.proc` header acceptance (`mapProcFile003`) in renderer + cm (RD-18, corelibs-20)
- `idRenderModelPrt` implementation (RD-02) and DECL_FX/DECL_PARTICLE re-enable hooks (FW-14)
- Doom 3 `decalInfo` fade syntax (RD-04)
- ARB fragment-program interaction stages / `SL_INTERACTION` (RD-05) — benefits D3-era mods generally
- Unified single-game-module build option (vs sp/mp split) (sys-build)
- cm NULL-model→world fallback and TrmFromModel bounds fallback (corelibs-17/21) — Doom 3 semantics
- Subview suppress/allow viewID normalization (RD-07); BoundsInAreas robustness (RD-21)
- Generic subtitle/censor infrastructure (sound-01/02) if desired upstream
- idStr hex-byte case tables (hardening upstream's re-encode-vulnerable raw literals)

Everything landed upstream is one less thing to re-merge on every future sync. This is
optional per item, but the plan assumes at least the proc-format and particle/FX items go
upstream-first because they are pure Doom-3-era restorations with no Prey coupling.

### 3.3 Standing decisions (settle before Phase 2 starts)

| # | Decision | Recommendation |
|---|---|---|
| D1 | Game-code hosting | **Executed:** `E:\Repositories\OpenPrey-game` is canonical. `stage_gamelibs.py` stages it directly at configure time; openPREY has no in-repo `src/game` mirror. Build **one** unified `game_<arch>` module (Prey retail model), installed to `basepr/`; keep upstream's `GameModuleDiagnostics` phase instrumentation. |
| D2 | Renderer module ABI | **Executed at API v9:** the module split remains. API v8 carried the Prey entity/time-group bridge; API v9 added the per-glyph material field in `glyphInfo_t`. Prey rendering targets the legacy GL/ARB2 path first; ModernGL/Vulkan feature parity remains explicitly deferred. |
| D3 | BSE | **Executed as an active in-tree runtime subsystem:** Meson always builds and links `src/bse`; Common attaches and initializes it, and the renderer module imports the shared instance. Prey's `gameImport_t` v7 correctly has no `bse` member, so the game module does not receive it through that ABI. Nested `SEG_EFFECT` spawning remains explicitly gated pending a v7-compatible callback path and stock-FX capture. |
| D4 | `usercmd_t` | **Executed:** upstream's 16-bit `buttons` and impulse-127 plumbing are retained with Prey's bit assignments re-based on top (`BUTTON_ATTACK_ALT` etc., coexisting with `BUTTON_WEAPONWHEEL`). The paired engine/game layout and network `_attackalt` path were validated with a two-client MP run. |
| D5 | `soundShaderParms_t` | **Executed:** the shared engine/game definition carries Prey's `subIndex` and profanity fields. Sound classes use Prey's five-class numbering (`SOUNDCLASS_MUSIC=4`, `SOUND_MAX_CLASSES=5`), with the upstream musical alias mapped to that value. This is a deliberate divergence from upstream's retail-Q4 ABI freeze because openPREY builds its paired Prey game module. |
| D6 | Content pipeline | **Executed:** repo-authored runtime content uses `content/basepr/pak0|pak1`, build-time PK4s, and `mod.json`. Large generated fixtures (retail `.cm` mirrors and `roadhouse_quick`) live in the dev-only loose directory rather than pak0. |
| D7 | Game-dir name | **Executed:** `basepr` is the sole openPREY runtime namespace. Repo-authored runtime content is assembled from `content/basepr/`, and the unified game module installs beneath `.install/basepr/`. |
| D8 | CI/release | **Executed for active validation and portable packaging:** `openprey-validation.yml` carries the current cross-platform validation architecture. The Windows x64 archive was produced, inspected, and launched successfully in isolation. Inherited release/signing/manual-publish lanes that still encode OpenQ4 or split-module assumptions are deliberately false-gated and remain a release-lane deferral. |
| D9 | MVD / bots / repeater | **Executed as fixed compile-time gates:** `OPENPREY_ENABLE_MVD`, `OPENPREY_ENABLE_BOTS`, and `OPENPREY_ENABLE_REPEATER` are `0`; guarded sites carry `OPENPREY-GATED(D9)` markers and entry points are inert. Re-enablement remains design work against Prey's v7 API, not a hidden runtime option. |
| D10 | TTF fonts | **Executed with bitmap-first behavior:** `r_useTrueTypeFonts` defaults to `0`; the retail bitmap-font spline path is implemented. The TTF spline variant explicitly no-ops/warns and remains deferred until focused credits-parity work is complete. |

## 4. Phases

### Phase 0 — pre-rebase hygiene (on `new-prey`, before branching)

The working tree currently mixes four unrelated in-flight efforts (~96 files, plus
untracked sources). Land or shelve them so the rebase starts from a clean, tagged state:

1. **Commit the trigger swept-hull fix** (`src/game/Entity.cpp`, `src/Prey/game_trigger.*`)
   — game-side, survives the rebase untouched.
2. **Commit the openPREY casing rebrand** (licensee.h, workflows, docs) as its own commit.
3. **Commit the light-grid GI and HDR/bloom rewrites as-is for the record**, knowing both
   are dispositioned obsolete-upstream (upstream's `RenderWorld_lightgrid.cpp` is 3453
   lines vs the local untracked 1666; upstream's HDR/bloom/auto-exposure stack is newer).
   They are dropped during the rebase, not lost — the old branch preserves them. Add the
   untracked files (`RenderWorld_lightgrid.cpp`, shader files, `openprey_version.rc`) first.
4. **Resolve D7 (basepy/basepr)** and fix the `meson.build` line-74 vs line-465 mismatch.
5. **Purge tracked `.install/` artifacts** (assets-07: committed binaries, the stale
   `.install/openprey/` pre-rename copy, duplicated asset trees; keep unique repro cfgs by
   moving them to a fixtures dir). Upstream's model generates `.install/` at build time.
6. Tag: `pre-oq4-rebase`.

### Phase 1 — scaffold (target: upstream tree builds under openPREY identity)

- Branch `prey-on-oq4` from `openq4-upstream/main`.
- Systematic rebrand pass (FW-01, sys-build rebrand): scripted openQ4→openPREY sweep
  against the *current* upstream surface — licensee.h identity block, generated-version
  pipeline (`openq4_version.py` model feeding the version rc; replaces the hardcoded rc),
  save paths with legacy-dir fallbacks, env vars with `OPENQ4_*` fallbacks, desktop
  entries/icons (assets-08), console theme (as a theme definition, not RGB patches).
- Re-add the Prey feature-toggle macro block (FW-02) and INTERIM/LEGACY config constants.
- Relocate `basepy/` content into the upstream content-pipeline layout (D6); wire Prey
  icons into upstream's install loops.
- Gate: engine + renderer-gl build and launch to the console with openPREY branding
  (no game module, no Prey assets yet).

### Phase 2 — additive compat layer (target: engine-only build with Prey surfaces)

All reapply-clean shims, roughly in include-order: Prey idlib headers + `precompiled.h`
includes (corelibs-01/08, repointing `bse_api`→`bse`), BitMsg helpers (corelibs-02), Heap
template defaults (corelibs-03), LittleBitField (09), interpolator accessors (10), Vec3
helpers (11), LangDict censor (12), SEH guard (15), cm wrapper API + HUMANHEAD contents
tables (corelibs-16/19/21/22/24), Maya signature (25), FileSystem additions (FW-05/06/07),
GUI-compat cvars (FW-09), bind-tip API (FW-10), decl accessors (FW-17), DemoFile magics
(FW-19), energynode defaults (FW-16), CD key (FW-28), wipe remap (FW-29), string remaps
(FW-36), LGLCD hooks, UI header shims (ui-10), GUI script events/commands (ui-03/04),
focus-mute virtuals (sound-04). Adaptation notes per item are in the catalogs — notably:
every new decl class implements upstream's `Parse(text, len, noCaching)` overload and
registers via `RegisterDeclFolderWrapper` (FW-14/15); `IcmpNoColor` must use upstream's
`ColorEscapeLength` (corelibs-13).

### Phase 3 — game module: compiles, loads, runs (the contract phase)

The critical path; co-design engine and game-header sides together.

1. **Staging + build** (D1): extend `stage_gamelibs.py` to stage the Prey trees; single
   `game_<arch>` `shared_module` + `openprey_game_idlib`; `Game.def`; install to game dir.
2. **Loader** (FW-11): unified-module candidate list (`game_x64`/`gamex86`/legacy aliases)
   rebuilt inside upstream's phase-instrumented `LoadGameDLL`, keeping diagnostics and the
   macOS universal2 fallback; v7 `gameImport_t` population (13 systems, no `bse` field).
   The separately linked engine/renderer BSE subsystem remains active per D3.
3. **Call-site contract** (FW-12, conflict-major): re-apply Prey `idGame` signatures
   (`InitFromNewMap/InitFromSaveGame` with `idSoundWorld*`, 4-arg `SetUserInfo`, plain
   `RunFrame`/`ClientPrediction`/snapshot family, `gameReturn_t` HUD fields,
   `PrintMemInfo`, `PlayerIsDeathwalking`, time-group API) across upstream's evolved
   `Session*`/`Async*` — reducing bot/repeater arities to Prey's or shimming with
   engine-local wrappers; MVD/bots gated per D9.
4. **Contested interface headers**, hand-merged combining both sides' deltas:
   `sound.h` (sound-05/06 + upstream's evolution), `RenderWorld.h` (fields/virtuals — lands
   with Phase 4's ABI bump), `UsercmdGen.h` (D4, FW-21), `Common.h`, `FileSystem.h`,
   `cm/CollisionModel.h`.
5. Vtable-affecting additions (idFileSystem/idDeclManager/idCommon/idSoundWorld) land here
   so engine + game module rebuild together once. Keep `declType_t` ordering identical
   between engine and game copies (DECL_BEAM insertion shifts values).
- Gate: SP map start (`roadhouse_quick`) with the unified module; MP listen-server start.

### Phase 4 — renderer workstream (largest single effort)

Policy per D2: coordinated ABI changes with fail-closed handshakes; execution used two
increments, v7→v8→v9. The legacy GL/ARB2 path remains first; every feature must either
be honored or explicitly no-oped by the shadow-map, ModernGL and Vulkan paths.

Order of battle:
1. **ABI extension commit** (RD-16 + fields from RD-01/08, `GuiTrace` overload RD-14,
   view-mode accessors for RD-20): `renderEntity_s`/`renderView_s` fields, `idRenderSystem`
   and `idRenderWorld` virtuals, `PS_BLOCK_SOUND`, SHADERPARM/DEFORMTYPE constants,
   version bumps, Vulkan stubs. The executed `referenceSound` representation retains both
   the integer handle and `idSoundEmitter *`: shader evaluation prefers the pointer and
   resolves the handle as a fallback, while render-demo IO preserves both values.
2. **Clean ports**: hhBeam model (RD-01), `idRenderModelPrt` (RD-02), corona deform
   (RD-17), shaderLevel gating (RD-19), robustness fixes (RD-21), skipPlayer traces
   (RD-15).
3. **Reworks into the evolved backend**: material compat tables/tokens/parm12/timeGroup
   (RD-03 — dropping the parts already upstream), decalInfo fade (RD-04), `SL_INTERACTION`
   ARB stages + interaction alpha-test (RD-05 — highest-risk renderer item; must also be
   reconciled with shadow-map receiver paths), Prey portal/skybox subviews (RD-06 —
   cooperate with upstream's subview policy/importance budget), viewID normalization
   (RD-07 — re-enumerate comparison sites incl. shadow planner), spirit-walk filtering
   (RD-08 — shadow caster gathering must respect it), glow overlay (RD-09 — re-sequenced
   against upstream's post stack, reusing its helpers), image remaps/fallbacks (RD-12),
   D3 proc header (RD-18 + corelibs-20, ideally upstream-first per §3.2), view-mode
   expressions (RD-20 — follows upstream's register/cache model).
4. **Drops** (obsolete-upstream): post-FX stack RD-10, light-grid RD-11, DDS loader RD-13,
   portability bundle RD-22 (re-verify only the `Model_lwo` casts). Re-express desired
   openPREY defaults (e.g. tonemap) as cvar defaults, not code.
5. **Assets**: take upstream's `pak0/glprogs` + `postprocess` materials wholesale
   (assets-02), dropping the `openprey_`-prefixed snapshots and re-pointing the renderer
   at upstream shader/material names; re-validate the Prey `interaction.vfp` against
   upstream's env-parameter contract and colormode sniffing (assets-03).
- Gate: retail Prey map renders correctly on `renderer-gl` (beams, portals, spirit walk,
  glow, materials), upstream renderer self-tests still pass.

### Phase 5 — sound, UI, session features

- Subtitles end-to-end (sound-01 + FW-27 + `hud_subtitles` GUI): rewrite the audibility
  gate for the linear-volume pipeline; `ProcessDemoCommand` bool returns.
- Profanity censor (sound-02 + corelibs-12): re-express the hold window as a linear fade
  scale.
- dB volume layer (sound-03 + ui-05): keep upstream's linear pipeline; add the dB alias
  sync + volumeslider mapping on top.
- Prey shader dialect (sound-05): meters attenuation re-validated against the new
  falloff path; dual volume/volumeDb parse+write; symbolic sound classes (D5 numbering).
- Prey sound API shims (sound-06) re-derived against the rewritten emitter internals.
- Focus mute (FW-26 + sound-04) replacing upstream's silence-by-NULL-world wholesale.
- GUI dialect (ui-01: tab containers, superWindowDef, buttonDef…) re-seated hunk-by-hunk
  into upstream's rewritten `Window.cpp`; background side-slices (ui-07) re-integrated
  with upstream's underlay/canvasfill system (drop the matcanvasfill half — already
  upstream); shear winvar (ui-08) reconciled with hardened savegame IO; fonts/cursors
  (ui-06) validated under the TTF/bitmap dual pipeline (D10); spline text effect (ui-02,
  conflict-major) redesigned against the TTF-aware glyph loop — schedule last, it's
  cosmetic-critical but isolable.
- Session features: loading/splash pipeline (FW-22 — keep upstream's expansion machinery,
  redirect its inputs; drop FW-23), load/menu music routing (FW-25) rebuilt against
  current `ExecuteMapChange`, menus (FW-31/32 — Prey GUI state publication on upstream's
  device-monitor machinery), savegame UX (FW-24, conflict-major — 'Prey' gamename +
  version policy + multi-gamedir search + quickload prompt as *extensions* of upstream's
  hardened framework, e.g. adding 'Prey' to the supported-name set), loading-continue
  gate via `com_skipLoadingContinue=1` default (FW-30) instead of code removal.

### Phase 6 — filesystem and platform polish

- Prey PK4 validation (FW-03: dual CD/digital pack sets) grafted onto upstream's richer
  validation/diagnostics; capture the outstanding classic-layout checksums (TODO item).
- `fs_basepath` discovery (FW-04): registry/CD-era scanner added as a candidate source
  *alongside* upstream's resolver; retarget (don't delete) Steam/GOG discovery to Prey's
  store folder names.
- `CPU_EASYARGS` question (sys-build): verify how upstream's game code handles x64 event
  args before re-applying the `_WIN64` override; coordinate with game-code tree.
- Optional two-line hardening of the native `Sys_IsGameWindowFocused` if `legacy_win32`
  is kept.

### Phase 7 — build, CI, packaging, docs

- CI per D8; Prey identity re-expressed as configuration on upstream's packaging scripts
  (`package_nightly.py` etc.), not diff replay.
- `.vscode`: regenerate Prey per-map launch configs on upstream's `meson-task.ps1` layout.
- Docs: re-home preserved `docs/dev/` material into upstream's `docs/dev` structure
  structure; port Prey-specific docs (pk4 checksums, porting baseline, this plan);
  refresh `TECHNICAL.md`/`BUILDING.md` from upstream's versions with Prey deltas.
- Menu strings: retain private `#str_122xxx` IDs and rename `*999.lang` packs to
  `<lang>_openprey.lang` in the pak layout (assets-04).

### Phase 8 — validation and cut-over

Recorded validation evidence:

- Build matrix: Windows full **1003/1003 build targets** plus staged install, Windows
  engine-only **767/767 build targets**, and Linux **1001/1001 build targets**. Renderer
  foundation self-tests pass; these prove
  shared renderer infrastructure, not full ModernGL/Vulkan Prey feature parity.
- Retail runtime: stock menu; Roadhouse, Feeding Tower, deathwalk/spirit, Shuttle, and
  `girlfriendx`/portal gameplay; save/load and config-casing migration; neutral-directory
  install auto-discovery; unified-module loading through canonical and legacy aliases;
  stale renderer-ABI rejection; and an explicit Vulkan request falling back to GL.
- Multiplayer: listen server plus two clients, including network `_attackalt` delivery
  through the merged `usercmd_t` layout.
- Filesystem compatibility: a synthetic numbered-pak layout reaches the settled menu.
  This is deliberately **not** evidence of genuine CD-era checksums, which remain deferred.
- Tool/runtime fixtures: two fresh `roadhouse_quick` dmap runs produced byte-identical
  `.cm`/`.proc` output; the generated geometry loaded; bounded light-grid baking completed;
  and the zero-probe `-quit` path exited cleanly.
- Package/validator closure: a Windows x64 archive was produced, its manifest and payload
  were inspected, and an isolated windowed launch reached Roadhouse and wrote an engine
  screenshot. The root push validator passed every active check, including staged-payload
  validation.

Explicitly deferred or broader than rebase closure:

- Exhaustive per-map, localization, subtitle/censor/audio, renderer-backend, portal/mirror,
  and platform parity. Representative runtime evidence above must not be read as a full
  manual matrix.
- macOS/ARM64/signing/manual-publish release-lane reconstruction, remote default-branch
  retargeting, and upstream PR submission.

- Known carried-over gap (unchanged by the rebase): mirror clip plane disabled
  (`numClipPlanes=0`) pending retail clip-side math reconstruction — tracked in
  `references/retail_mirror_clip_parity_plan.md`.
- Regenerate/verify the committed **retail** `.cm` mirrors against the rebased collision
  code (assets-06). The committed quick fixture was regenerated and validated separately.
- Local cut-over is complete: `new-prey` points at the closing rebase head,
  `pre-oq4-rebase` remains preserved, and the next-sync base (`d41186e4`) is recorded in
  the porting ledger.

## 5. Risk register

| # | Risk | Mitigation |
|---|---|---|
| 1 | FW-12 game-API contract vs upstream bots/MVD/repeater (conflict-major, high) | Co-design with game headers; gate features per D9; engine-local shims for arity reduction |
| 2 | Renderer module ABI extension across three binaries | Two coordinated increments to API v9; Vulkan stubs; version checks police stale modules |
| 3 | RD-05 `SL_INTERACTION` + RD-09 glow into a backend that grew ~23k lines | Legacy-path-first policy; upstream post-stack helpers reused; explicit shadow-map interaction review |
| 4 | Sound ABI decisions (parms layout, class numbering, dB layer) | Settle D5 up front; document divergence at upstream's freeze comment |
| 5 | `usercmd_t` re-layout touches game code + demos | Settle D4 up front; single commit pairing engine+game header |
| 6 | ui-02 spline text vs TTF glyph pipeline | D10 (TTF off by default initially); schedule last; PaintChar anchor survives |
| 7 | `declType_t`/enum ordering drift between engine and game module | One shared header policy via staging include dir; static_asserts |
| 8 | FW-24 savegame redesign on the hardened framework | Extend upstream mechanisms (supported-name set) instead of replacing |
| 9 | Uncommitted in-flight work lost or half-carried | Phase 0 commits everything first; obsolete items preserved on old branch |
| 10 | Rebrand incompleteness (surface tripled upstream) | Scripted sweep + grep audit; legacy fallbacks for config/save paths |

## 6. Ledger

The authoritative per-change worklist (137 items with files, descriptions, dispositions,
verified upstream evidence, effort, risk) is in this directory:

- [`catalog-renderer.md`](catalog-renderer.md) — 23 items (RD-*)
- [`catalog-framework.md`](catalog-framework.md) — 39 items (FW-*)
- [`catalog-sys-build.md`](catalog-sys-build.md) — 19 items
- [`catalog-sound-ui.md`](catalog-sound-ui.md) — 21 items (sound-*/ui-*)
- [`catalog-corelibs.md`](catalog-corelibs.md) — 25 items (corelibs-*)
- [`catalog-assets.md`](catalog-assets.md) — 10 items (assets-*)
- [`report-interface.md`](report-interface.md) — game↔engine requirements map (18 rows)
- [`report-architecture.md`](report-architecture.md) — upstream evolution survey
- [`report-narrative.md`](report-narrative.md) — openPREY port history + hazards
- [`report-audit.md`](report-audit.md) — coverage audit (complete; one minor gap:
  `tools/assets/extract_menu_background_tiles.ps1`, carried with assets-05)

Working practice: check items off in the catalogs as they land (add a `PORTED:<commit>`
line), so the ledger doubles as the audit trail for the next upstream sync.
