# openPREY → openQ4 Rebase: Implementation Guide

Status: executed rebase record (2026-08-01). Implementation is complete; validated
outcomes, including package/runtime and root-validator closure, are summarized in Phase 8
and tracked per item in [`status-ledger.md`](status-ledger.md). This is the
execution-level companion to
[`plan.md`](plan.md) (strategy, standing decisions D1–D10, risk register).
Per-change evidence lives in the catalogs in this directory, and current status is in
[`status-ledger.md`](status-ledger.md) — every work package below cites ledger IDs
(RD-*, FW-*, sound-*, ui-*, corelibs-*, assets-*, and the sys-build kebab-case IDs).
Read the cited ledger entry before starting an item: it names the exact upstream
functions/lines/commits the change must be re-seated against.

## 0. Conventions

- **Work packages (WPs)** are numbered `WP<phase>.<n>` and ordered by dependency within
  a phase. Each WP states: goal, ledger items, steps, dependencies, and an acceptance
  check. A WP is the unit of review; a ledger item is the unit of commit.
- **Commit format:** one ledger item per commit where practical:
  `rebase: <ID> <short title>` with body text naming what was adapted vs. the ledger
  entry (e.g. "re-seated into upstream ParseStage; dropped fragmentParm half — already
  upstream"). Multi-item commits allowed only inside one WP.
- **Ledger tracking:** update [status-ledger.md](status-ledger.md) with disposition,
  implementation state, validation state, and final evidence. Do not infer runtime
  validation from a successful port or compile.
- **Refs used throughout:**
  - `FORK_PREY = e634332b` (openPREY initial commit)
  - `FORK_OQ4 = abb9a688` (identical upstream tree)
  - `UPSTREAM = openq4-upstream/main` (`d41186e42d...`, 2026-07-31)
  - `OLD = new-prey` (pre-rebase branch), `NEW = prey-on-oq4` (rebase branch)
- **Golden rule (cross-pollination):** wherever openPREY and upstream both implement
  something, take upstream's version and delete openPREY's. The initial plan identified
  31 obsolete/drop items; execution finished at 34 intentionally dropped deltas. If an
  undocumented collision is found during a future sync, prefer upstream and note it in
  the ledger.
- **ABI rules (apply to every phase):**
  1. Any change to `idRenderSystem`/`idRenderWorld`/`idMaterial` vtables or
     `renderEntity_t`/`renderView_t`/`renderLight_t` layout ⇒ bump `RENDER_API_VERSION`
     in `src/renderer/RenderModuleAPI.h` and rebuild engine + renderer-gl + renderer-vk
     together. Execution used two coordinated increments, v7→v8→v9; v8 carried the
     Prey entity/time-group bridge and v9 added the per-glyph material field.
  2. Any change to `idFileSystem`/`idDeclManager`/`idCommon`/`idSoundSystem`/
     `idSoundWorld`/`idSession` vtables or to `gameImport_t`/`gameExport_t`/`idGame`/
     `usercmd_t`/`soundShaderParms_t` ⇒ engine and game module must rebuild together;
     never ship one without the other.
  3. `declType_t` ordering (with `DECL_BEAM` inserted and FX/PARTICLE re-enabled) must
     be byte-identical between engine and staged game headers. Enforce with a
     `static_assert` on the enum count in `precompiled.h` after WP2.4.
  4. Sound-class numbering: engine and game must agree on `SOUNDCLASS_*` values and
     `SOUND_MAX_CLASSES` (decision D5; see WP5.4).

## 1. Preconditions and tooling

### WP1.0 — verify the workspace

```powershell
git -C E:\Repositories\openPREY fetch openq4-upstream
git -C E:\Repositories\openPREY merge-base HEAD openq4-upstream/main   # expect: no output (unrelated histories)
git -C E:\Repositories\openPREY diff --shortstat e634332b abb9a688     # expect: 42 files, insertions only (assets)
```

- Confirm `E:\Repositories\OpenQ4` is on the same commit as `openq4-upstream/main`
  (used for reading upstream docs/files directly).
- Companion game repo: `E:\Repositories\OpenPrey-game` is the canonical source of truth.
  Configure-time staging consumes it directly; do not add an in-repo `src/game` mirror.

### Optional 3-way assist

Because the fork trees are identical, you can synthesize a merge base for tooling:

```bash
# per-file 3-way: base = fork, ours = upstream, theirs = openPREY
git show abb9a688:src/renderer/tr_subview.cpp > /tmp/base.cpp
git show openq4-upstream/main:src/renderer/tr_subview.cpp > /tmp/ours.cpp
git show new-prey:src/renderer/tr_subview.cpp > /tmp/theirs.cpp
git merge-file -p /tmp/ours.cpp /tmp/base.cpp /tmp/theirs.cpp > merged.cpp
```

Use this only for reapply-clean items; conflict markers in a rework item mean "read the
ledger and hand-port" — do not resolve blind.

Alternatively `git replace --graft e634332b abb9a688` makes `git log`/`git merge`
believe the histories join at the fork; remove with `git replace -d e634332b` when done.
(Cosmetic convenience only; nothing below depends on it.)

## 2. Phase 0 — pre-rebase hygiene (on `new-prey`)

Goal: a clean, fully committed, tagged pre-rebase state. The working tree currently
mixes four unrelated efforts (ledger: report-narrative §Phase 4).

### WP0.1 — commit the trigger swept-hull fix (game-side, survives rebase)

Files: `src/game/Entity.cpp`, `src/Prey/game_trigger.cpp/.h`, plus the repro cfg if
worth keeping (move `repro_roadhouse_trigger_timing.cfg` out of `.install/` into
`tools/fixtures/` first). Commit alone: `fix: swept-hull player trigger detection`.

### WP0.2 — commit the openPREY casing rebrand pass

Files (uncommitted): `src/framework/licensee.h` (PROJECT_NAME/window classes/
CONFIG_FILE + INTERIM/LEGACY fallbacks/CD_BASEDIR), `.github/workflows/*`,
`.vscode/tasks.json`, `meson.build`, docs, TODO. Add untracked
`src/sys/win32/rc/openprey_version.rc` (replacing deleted `OpenPreyVersion.rc`).
This commit is superseded by WP1.2's systematic sweep but preserves the intent and the
config-migration shim constants for reuse.

### WP0.3 — commit the light-grid GI and HDR/bloom work for the record

Both are dispositioned obsolete-upstream (RD-11, RD-10) — they will NOT be ported, but
must not be lost from history:

```bash
git add src/renderer/RenderWorld_lightgrid.cpp basepy/glprogs/openprey_bloom_*.{fs,vs} \
        basepy/glprogs/openprey_hdr_luminance.{fs,vs} basepy/glprogs/lightgrid_indirect.{fs,vs}
git add -u   # remaining uncommitted renderer/session files
```

Two commits: `wip: light-grid GI bake system (superseded by upstream openQ4 lightgrid)`
and `wip: HDR scene target + bloom pyramid rewrite (superseded by upstream post stack)`.
Untangle overlapping hunks in `Session.cpp`/`draw_common.cpp` between WP0.2/0.3 with
`git add -p`; perfection is not required — the tag preserves the aggregate.

### WP0.4 — resolve the basepy/basepr split-brain (decision D7)

Assumed choice: `basepr` (per TODO "keep basepr as the sole namespace").

1. `git mv basepy basepr`; update `meson.build:465` `subdir('basepy')` → `basepr` and
   confirm `meson.build:74` install dir already says `basepr`.
2. Grep-audit: `git grep -n "basepy"` — fix all remaining active references (docs,
   scripts, `.vscode`, workflows). Historical audit snapshots may retain the old path;
   record the current layout in `docs/dev/prey-rebase/porting-baseline.md`.
3. Fold the untracked `basepr/maps/game/` bake outputs in or delete them (they are
   obsolete with RD-11 dropped — recommend delete).

### WP0.5 — purge tracked `.install/` state (assets-07, disposition: drop)

1. Salvage: move unique repro cfgs (`.install/openprey/repro_*.cfg`, `.install/base/`
   cfgs, `preykey` test fixture) to `tools/fixtures/`.
2. `git rm -r --cached .install/basepy .install/openprey` (and the already-deleted
   binaries/icons); commit the working-tree deletions of `OpenPrey-client_x64`,
   `-ded_x64`, `.icns`, `.ico`.
3. Add `.install/` to `.gitignore` (keep only what upstream keeps: nothing but icons —
   and openPREY's icons live in `assets/icons/`, so ignore the whole tree).

### WP0.6 — tag and snapshot

```bash
git tag pre-oq4-rebase
git push origin new-prey --tags   # if pushing is desired at this point
```

Acceptance for Phase 0: `git status` is clean on `new-prey`; `pre-oq4-rebase` tag
exists; `git grep -c basepy` returns 0 (or documented exceptions).

## 3. Phase 1 — scaffold (upstream tree under openPREY identity)

### WP1.1 — create the branch and prove the baseline builds

```bash
git checkout -b prey-on-oq4 openq4-upstream/main
```

Build unmodified upstream once to establish the baseline (Windows):

```powershell
tools/build/meson_setup.ps1            # upstream's setup script (now in-tree on this branch)
meson compile -C builddir
```

Record deviations (toolchain, subproject fetch issues) before any openPREY change lands.
Note: upstream requires the gamelibs stage (`meson.build` refuses to build without
`.tmp/gamelibs_stage`) — for this baseline either clone `openQ4-game` beside the repo or
configure engine-only if upstream's options allow; otherwise defer the full build gate
to WP1.4 and only validate `meson setup` here.

### WP1.2 — systematic rebrand sweep (FW-01, rebrand-openprey, FW-02, assets-08, linux-desktop-icons)

Do this as a scripted, reviewable pass — the branding surface tripled upstream.

1. **Identity constants** (`src/framework/licensee.h`): port openPREY's block onto
   upstream's file — GAME_NAME/PROJECT_NAME `openPREY`, `BASE_GAMEDIR "base"`,
   `BASE_MPGAMEDIR "base"`, `OPENPREY_GAMEDIR "basepr"` (replacing upstream's openq4
   gamedir constant, keep the macro name upstream code expects or rename all uses),
   `CDKEY_FILE "preykey"` + CDKEY_TEXT, `GAME_PLAYERDEFNAME player_tommy`,
   CONFIG_FILE `openPREYConfig.cfg` + INTERIM/LEGACY fallbacks (from WP0.2), window/
   console class names, `CD_BASEDIR`, savegame gamename constants (coordinate with
   WP5.9: add `"Prey"` to upstream's `SAVEGAME_GAME_NAME_*` set rather than replacing).
   Also port FW-02's Prey feature-toggle macro block verbatim (SINGLE_MAP_BUILD,
   GAMEPORTAL_PVS/SOUND, DEATHWALK_AUTOLOAD, `_HH_RENDERDEMO_HACKS`, MUSICAL_LEVELLOADS,
   GUIS_IN_DEMOS, …) — additive at end of file.
2. **Version pipeline**: do NOT reintroduce a hardcoded version rc. Rebrand
   `tools/build/openq4_version.py` / `openq4_release_version.py` outputs (generated
   header consumed by the version rc and AutoVersion.h); rename the rc to
   `openprey_version.rc` and point the meson rc target at it.
3. **Paths**: Windows `LocalAppData\openPREY`; Linux `~/.local/share/openprey` +
   `/usr/local/games/basepr` defaults; macOS `Application Support/openPREY`, bundle id
   `com.darkmatter.openprey`; single-instance mutex/lock names; crash artifacts
   `openprey_crash_*`. Keep legacy-dir read fallbacks (pattern already exists in the
   OLD branch — copy the fallback lists).
4. **Env vars**: `OPENPREY_*` with `OPENQ4_*` accepted as legacy fallback (pattern from
   OLD `meson_setup.ps1` / `sync_gamelibs.ps1`).
5. **Desktop/icons** (assets-08 + linux-desktop-icons): copy `assets/icons/prey.*` from
   OLD; rename into upstream's expected set (upstream now also wants 1024/512/40/20 px
   — generate from `prey_256.svg`); add `assets/linux/openprey.desktop.in` (+ a
   Steam-Deck variant cloned from upstream's); wire into upstream's meson icon/desktop
   loops (`meson.build:1638-1715` region); replace splash bmp.
6. **Console theme** (FW-20 partial, console-color-tweak): express Prey cyan accent and
   charset as a theme in `src/sys/sys_console_theme.h` + `kConsoleBorderColor` default;
   charset `textures/bigchars` lands with WP5.7 (needs assets present).
7. **Sweep audit**: `git grep -inE "openq4|quake ?4" -- ':!docs' ':!*.md'` — every
   remaining hit is either intentional (upstream compat markers like
   `OPENQ4_SOUND_HAS_SOUNDWORLD_SKIP`, legacy fallbacks, third-party) or a bug. Record
   the intentional list in this document's appendix when done.

Suggested: 3–6 commits (identity, version pipeline, paths/env, desktop/icons, sweep).

### WP1.3 — content tree relocation (D6; assets-01, assets-06, part of assets-10)

1. Create `content/basepr/pak0/` mirroring upstream's `content/baseoq4/pak0` layout;
   move the OLD `basepr/` (ex-`basepy/`) trees in: `guis/`, `materials/`, `strings/`,
   `script/`, `glprogs/` (contents decided in WP4.8 — for now copy Prey-only shaders:
   `interaction.vfp`; do NOT copy the openprey_* post-FX mirrors, they are dropped by
   assets-02).
2. `mod.json.in`: clone upstream's, set Prey identity; wire `configure_file`.
3. Pak assembly: reuse upstream's `tools/build/build_pak0.py` + manifest custom_target,
   pointed at `content/basepr/pak0`; large dev fixtures (assets-06: retail `.cm`
   mirrors, `roadhouse_quick.*`, repro cfgs) go to `content/basepr/dev/` (loose,
   NOT in the pak manifest) or stay under `tools/fixtures/` — decide by whether the
   validation loop needs them in the search path (recommend: `dev/` staged only for
   local runs via `fs_devpath`).
4. Strings: retain the private `#str_122xxx` IDs and move each pack to the maintainable
   `<lang>_openprey.lang` convention in `content/basepr/pak0/strings/` (WP7.4).

### WP1.4 — Phase 1 gate

- `meson setup` + engine-only compile succeeds (game staging may still point at
  upstream's expectations — acceptable to stub `build_games=false` until WP3.1).
- Client launches to console/menu shell with openPREY branding (retail assets not yet
  required — expect missing-asset warnings, not crashes).
- Grep audit from WP1.2.7 recorded.

## 4. Phase 2 — additive compat layer

All items here are additive headers/impls with no upstream structural conflict.
Engine-only build must stay green after every WP. Order minimizes include-graph churn.

### WP2.1 — idlib foundations

Items: corelibs-01, corelibs-08, corelibs-09, corelibs-10, corelibs-11, corelibs-15,
corelibs-14, corelibs-13 (rework), corelibs-12.

1. Copy from OLD verbatim: `src/idlib/math/prey_math.{h,cpp}`,
   `math/prey_interpolate.h`, `containers/PreyStack.h`, `src/preyengine/profiler.h`.
   Wire includes into upstream's `Lib.h`/`precompiled.h` at the fork-era positions
   (corelibs-01). Add the new files to `tools/build/meson_sources.py` (upstream
   generates source lists — never hand-edit meson file lists).
2. `precompiled.h` (corelibs-08): add `DeclFX.h`/`DeclParticle.h`/`declPreyBeam.h`
   includes (classes arrive in WP2.4 — add includes in that WP if you want per-WP
   compilability; noted here for ordering) and the profiler include. The
   `bse_api`→`bse` repoint is already correct on the upstream base — nothing to do,
   but verify no OLD include of `../bse_api/` sneaks in via later ports.
3. `Lib.h/.cpp` (corelibs-09): LittleBitField + Swap_Init wiring, verbatim.
4. `Extrapolate.h`/`Interpolate.h` (corelibs-10): Get*Ptr accessors + friend
   idTypeInfoTools — files are untouched upstream; apply verbatim.
5. `Vector.h/.cpp`, `Math.h` (corelibs-11): hhToMat3, DirectionMask + MASK_* defines,
   FLOAT_IS_INVALID — check whether upstream's reworked `Math.h` float-bit macros
   should back FLOAT_IS_INVALID (one-line check per ledger).
6. `Str.cpp` (corelibs-15 SEH guard; corelibs-14 cyan `S_COLOR_CONSOLE` — note upstream
   table is now COLOR_INDEX_COUNT-sized; edit the same slot only).
7. `Str.{h,cpp}` (corelibs-13): port `IcmpNoColor` but advance with upstream's
   `ColorEscapeLength(s, ...)` instead of `s += 2` — REQUIRED, upstream escapes are
   variable-length.
8. `LangDict.{h,cpp}` (corelibs-12): GetString `<PROFANITY>` filter at both hit sites
   (direct + legacy-remap); factor into one helper; runs after upstream's load-time
   CP1252 normalization (no interaction).
9. Do NOT port corelibs-04/05/06/07 (obsolete: 64-bit fixes, str tables, platform
   blocks, include-case — all upstream). Mark `DROPPED` in ledger.

### WP2.2 — messaging and allocators

Items: corelibs-02, corelibs-03.

- `BitMsg.h`: the +51-line inline block (WriteBool/ReadBool, WriteVec3/ReadVec3,
  WriteString(make7Bit), SetDebugEntType, idBitMsgDelta::Init aliases, idMsgQueue
  wrappers). Header-only; no collision with upstream's BitMsg.cpp hardening.
- `Heap.h`: add `= MA_DEFAULT` defaults to the `byte memoryTag` template parameter of
  `idBlockAlloc`/`idDynamicAlloc`/`idDynamicBlockAlloc` (3 lines); re-verify against
  upstream's size_t-migrated member signatures.

### WP2.3 — collision model compat

Items: corelibs-16 (rework), corelibs-17 (rework), corelibs-19, corelibs-21,
corelibs-22, corelibs-23 (rework), corelibs-24, corelibs-25. Drop: corelibs-18.

1. `cm/CollisionModel.h` (corelibs-16): re-derive the Prey wrapper set against
   upstream's current virtuals: `typedef idCollisionModel *cmHandle_t;` + inline
   wrappers `FreeMap()`, `LoadModel(name, precache)` (upstream defaults
   precache=false — keep explicit arg), `SetupTrmModel`, `TrmFromModel(name, trm)`,
   `GetModelName/Bounds/Contents/Vertex/Edge/Polygon` (delegate to the richer
   `idCollisionModel` members upstream added), `DrawModel` no-view-axis overload,
   `AppendMap`/`WillUseAlreadyLoadedCollisionMap` stubs (corelibs-22), and
   `ContentsName(contents)` wrapping internal `StringFromContents`
   (`CollisionModel_local.h:591`).
2. NULL-model→world fallback (corelibs-17): insert `model = models[0]` fallback into
   each entry point individually — upstream diverged per-function: Translation has
   warn-once/no fallback; Contacts delegates to Translation (add nothing there unless
   game paths hit it — re-check); Contents/PointContents/ContentsTrm still FatalError.
   First VERIFY `models[0]` is still guaranteed to be the world model under upstream's
   per-map model lifetime (ledger cites commits 40d49258/9adb69a3) — if not, resolve
   the world model by name instead.
3. `CollisionModel_debug.cpp` (corelibs-19): `#ifdef HUMANHEAD` Prey contents tables
   (indices 17–28) + underscore-less alias parsing. Requires HUMANHEAD define
   (WP1.2.1 toggles) and game-side CONTENTS_* values (game headers — staged in Phase 3;
   the engine-side table uses its own literals, so this compiles standalone).
4. `TrmFromModel` bounds fallback (corelibs-21): reinsert the fallbackToBounds lambda
   at the four failure sites; note upstream's `CompoundTrmFromModel` as the better path
   for specific Prey callers later (game-side follow-up, not engine).
5. `AASFile.h` friend (corelibs-24), `MayaImport/maya_main.h` 2-arg exporter typedef
   (corelibs-25): verbatim.
6. `AASFile.cpp` (corelibs-23): emit the Doom 3-era six-field area record when building
   for Human Head/Prey instead of labelling the Quake 4 1.08 feature fields as 1.07;
   retain explicit file/map CRC diagnostics and old-AAS policy. This reworked port was
   runtime-checked by loading retail Prey `.aas48` data.
7. Drop corelibs-18 (upstream ModelInfo).

### WP2.4 — Prey decl types

Items: FW-14 (rework), FW-15 (rework), FW-16, FW-17. Prereq for RD-02 (prt models)
and RD-01 (beams).

1. Copy `DeclFX.{h,cpp}`, `DeclParticle.{h,cpp}`, `DeclPreyBeam.cpp`/`declPreyBeam.h`
   from OLD. Adapt each class to upstream's decl contract:
   - implement the `Parse(const char *text, int textLength, bool noCaching)` overload
     (upstream commit 043833c8 — every decl class overrides it);
   - register via `RegisterDeclFolderWrapper` + allocator trampolines
     (`DeclManager.cpp:1355-1400` region), NOT the fork-era direct registration.
2. `declManager.h`: re-enable `DECL_FX`, `DECL_PARTICLE` (upstream keeps them
   commented at lines ~56-57), insert `DECL_BEAM` at the SAME position OLD used;
   add `FindBeam/BeamByIndex` inline helpers and the `SetInsideLevelLoad/
   GetInsideLevelLoad` virtuals (FW-17; member exists upstream).
   Then add the enum-count static_assert (ABI rule 3).
3. Register commands: `listFX`, `listParticles`, `printFX`, `printParticle`,
   `listBeams`; folder registrations (`fx`, `particles`, `beams` with `.beam`).
4. Skip the `Common.h` EDITOR_PARTICLE part of FW-14 (already upstream).
5. `DeclEntityDef.cpp` (FW-16): `ApplyPreyEntityDefCompatibility` inserted at the end
   of the `Parse` — into the noCaching overload upstream uses.
6. FW-18 (DeclSkin): port only after verifying against upstream's
   `DeclManager_ValidateParsedDecl` flow whether `Parse` returning true is still
   correct (ledger flags this); the RemapShaderBySkin inline-GAME_DLL half is DROPPED
   (upstream made it virtual).

### WP2.5 — framework additive surface

Items: FW-05, FW-06, FW-07, FW-08, FW-09, FW-10, FW-19, FW-28, FW-29, FW-36, FW-38,
FW-39 (surviving parts).

- FW-05: replace `FindMapScreenshot` body (upstream's is byte-identical to fork —
  drops in).
- FW-06: `OpenExplicitFileAppend` virtual + impl (vtable — ABI rule 2; fine now, game
  module doesn't exist yet).
- FW-07: Warning→DPrintf at `OSPathToRelativePath` failure.
- FW-08: `exec_savepath` command; `openPREY_ExecConfigFromSavePath` probing
  CONFIG→INTERIM→LEGACY; `WriteConfiguration()` in `idCommonLocal::Quit`.
- FW-09: Prey GUI compat cvar block (gui_filter_pb, g_subtitles, com_profanity,
  r_shaderlevel, r_correctspecular, r_normalizebumpmap, r_skipGlowOverlay — NOTE
  upstream already registers r_skipGlowOverlay; skip that one —
  r_lowParticleDetail, r_useFastSkinning, image_anisotropy, s_musicvolume_dB,
  g_levelloadmusic, s_reverse).
- FW-10: `MaterialKeyForBinding` + `SetGameSensitivityFactor`/`FixupKeyTranslations`
  virtuals; check localized key names against upstream's TTF key-name work when
  choosing display strings.
- FW-19: swap accepted demo magics to the openPREY casing set inside upstream's
  multi-magic mechanism (`DemoFile.cpp:36-38` pattern) — do not port OLD's parallel
  implementation.
- FW-28: ReadCDKey/WriteCDKey implementations into upstream's empty stubs
  (`Session.cpp:~7102`); depends on CDKEY_FILE (WP1.2).
- FW-29: StartWipe material remap + null guards.
- FW-36: `#str_104xxx`→`#str_04xxx` remaps at relocated sites (grep-driven);
  MSG_CDKEY visible_left/mid/right states.
- FW-38: raise rate defaults 25600→32000 and remap presets (or accept upstream's
  25600 — decide and note in ledger; the delta is preference-only).
- FW-39: port only the surviving bits — Unzip 'register' removal (upstream still has
  5), BuildVersion.h guard, MemInfo animAssetsTotal, USERCMD_ONE_OVER_HZ (lands with
  WP3.4 UsercmdGen merge if preferred).

### WP2.6 — sys/UI/sound additive shims

Items: lglcd-idsys-hooks, sound-04, ui-10, ui-03, ui-04, toolchain-warn-suppression.

- `sys_public.h`: ten LGLCD_* default virtuals appended to idSys (verbatim).
- sound-04: `SetMuteForFocus`/`IsMutedForFocus`/`IsMutedExplicitly` virtuals +
  focusMuted flag; `IsMuted()` returns `muted || focusMuted`; emitter gate switches to
  `IsMuted()` (`snd_emitter.cpp:~1012`). (Session-side caller lands WP5.6.)
- ui-10: `idListGUI::Add(int, const idStr&)` pure virtual (+ impl in ListGUI local
  class), `idUserInterface::Translate` default virtual.
- ui-03: ON_TABACTIVATE/ON_SLIDERCHANGE/ON_STARTUP/ON_MAXCHARS enum + ScriptNames +
  `CallStartup()`; EditWindow ON_MAXCHARS firing (sites survive upstream).
- ui-04: GuiScript `inc`/`resetCapture` commands + `SetInternalVarValue` routing.
- toolchain-warn-suppression: add openPREY's `warning_level=0` / `/W0` /
  `-fpermissive` for Prey-era sources into upstream's `shared_cpp_args` (scope to game
  targets if practical; engine can stay at upstream's warning level).

**Phase 2 gate:** engine + renderer-gl compile clean; client still reaches menu shell;
`reloadDecls` with a Prey pak present parses `.prt`/`.fx`/`.beam` without errors
(spot-check with retail assets if available, else defer to Phase 3 gate).

## 5. Phase 3 — game module: compiles, loads, runs

Highest-coordination phase. Keep engine and staged game headers in one commit series;
never let them drift. Sub-steps ordered so the build breaks early and locally.

### WP3.1 — staging + build integration (unified-game-module, gamelib-sync-scripts, assets-10; decision D1)

1. **Stage the canonical Prey game trees** from `E:\Repositories\OpenPrey-game` (or
   `OPENPREY_GAMELIBS_REPO=<path>`): `src/game`, `src/Prey`, `src/preyengine`, and
   required shared headers. `tools/build/stage_gamelibs.py` copies them into
   `<builddir>/.tmp/openprey_gamelibs_stage/` with manifest/hashing. The per-build
   location prevents concurrent platform configurations from rewriting one another's
   source inputs. Do not create an in-repo mirror. This
   keeps upstream's configure-time gate and include-dir mechanism intact
   (`include_directories('.tmp/openprey_gamelibs_stage/src/game')` resolves
   `precompiled.h`'s `#include "../game/Game.h"` to PREY's Game.h — v7 contract).
2. **Meson targets**: replace upstream's split `game-sp_/game-mp_` targets
   (root `meson.build` ~180-370/1303-1335 + `content/baseoq4/meson.build`) with ONE
   `game_<arch>` `shared_module` built from the staged tree (source list from OLD
   the preserved game-source manifest incl. per-file exclusions), linked against a single
   `openprey_game_idlib` flavour, `vs_module_defs: src/game/Game.def`,
   `/BASE:0x180000000` already upstream, Linux/darwin export maps cloned from
   upstream's `linux_game_module.map`/`darwin_game_module.exp` (regenerate symbol
   list for `GetGameAPI`), installed to `basepr/`. Keep `build_games` option;
   keep `build_game_sp/mp` as accepted no-ops.
3. Keep old sync/build helpers only as migration shims; canonical builds consume the
   staged companion sources and never write an in-repo mirror.
4. `stage_direct_run_game_module.py`: adapt for the unified module name.

### WP3.2 — loader (FW-11)

Inside upstream's phase-instrumented `LoadGameDLL` (`Common.cpp:~5388-5560`,
`GameModuleDiagnostics.h`):

- Module selection always resolves to the unified base name; candidate list
  `game_x64`/`game_x86`/`gamex64`/`gamex86`/`game` (OLD helper trio
  `openPREY_IsValidGameModuleName`/`SelectGameModuleBaseName`/
  `BuildGameModuleCandidateList`); keep phase tracking, diagnostics, and the macOS
  universal2 fallback branch.
- `gameImport_t` population: exactly the 13 Prey v7 systems (+ optional `hhProfiler*`
  under INGAME_PROFILER_ENABLED). No `.bse` member exists in Prey's struct, so BSE is
  not passed through the game ABI. It is nevertheless an active in-tree engine/runtime
  subsystem: Meson builds and links it, Common attaches and initializes it, and the
  renderer module imports the shared instance (D3).
- `com_activeGameModule`/`com_nextGameModule` mod-switch flow: map every value onto
  the unified module (Session/AsyncNetwork helpers `Session_ModuleSupportsSingleplayer/
  Multiplayer` accepting legacy names — port from OLD, re-site per ledger).

### WP3.3 — engine↔game call-site contract (FW-12; the conflict-major core)

Re-apply Prey signatures across upstream's evolved call sites. The authoritative
signature list (verified in `report-interface.md` §1a):

| Call | Prey v7 form |
|---|---|
| `InitFromNewMap` | `(mapName, renderWorld, soundWorld, isServer, isClient, randseed)` — passes menuless **soundWorld** |
| `InitFromSaveGame` | `(mapName, renderWorld, soundWorld, saveGameFile)` |
| `RunFrame` | `(const usercmd_t *clientCmds)` — no catchup/serverGameFrame args |
| `ClientPrediction` | `(clientNum, clientCmds)` — no lastPredictFrame/ClientStats |
| `SetUserInfo` | `(clientNum, userInfo, isClient, canModify)` — 4 args |
| `SpawnPlayer` | `(clientNum)` — no bot args |
| `ServerClientConnect` | `(clientNum)` |
| `ServerClientBegin` | `(clientNum)` |
| `ServerAllowClient` | `(numClients, IP, guid, password, reason)` |
| `ServerWriteSnapshot` | `(clientNum, sequence, msg, byte *clientInPVS, numPVSClients)` |
| `ClientReadSnapshot` | `void` return, Prey arity |
| `HandleGuiCommands` | replaces `HandleMainMenuCommands` usage |
| `GetTimeGroupTime`, `SelectTimeGroup`, `PrintMemInfo`, `PlayerIsDeathwalking`, `GetTimePlayed/ClearTimePlayed` | HumanHead additions (called by renderer/session) |

Sites to convert (upstream locations per ledger): `Session.cpp` (~map-change +
menu-command paths), `Session_menu.cpp`, `AsyncClient.cpp`, `AsyncServer.cpp`
(`:405/:1273/:1825/:2985` bot/snapshot sites), `AsyncNetwork.cpp`, `Common.cpp`,
`MultiViewDemo.cpp` (see WP3.5). Technique: engine-local shims where upstream features
need extra data (e.g. an engine-side `ServerClientBeginShim(clientNum, isBot, botName)`
that drops bot args and logs when bots are enabled), so upstream call-flow survives
with Prey arity underneath.

### WP3.4 — contested header merges

One commit per header; engine+game rebuild after each.

1. `src/framework/UsercmdGen.h/.cpp` (FW-21, decision D4): keep upstream 16-bit
   `buttons` + impulse-127 + new idUsercmdGen virtuals; re-base Prey bits on top:
   `BUTTON_ATTACK_ALT = BIT(3)`, shifted `BUTTON_SCORES/MLOOK`, keep
   `BUTTON_WEAPONWHEEL` at a free bit; register `_attackalt` → UB_BUTTON3; add
   `USERCMD_ONE_OVER_HZ`. **Game-side action**: update Prey game code's button
   constants to the merged layout (single commit pairing both sides).
2. `src/framework/Common.h` (FW-10 done; MemInfo_t verify — upstream already has it at
   a different offset; keep upstream's, add `animAssetsTotal` if missing).
3. `src/sound/sound.h` interface part of sound-05/06 (constants + virtuals only;
   implementations land Phase 5): `SOUNDCLASS_*` per D5 (see WP5.4 numbering
   resolution), `SSF_VOICEAMPLITUDE` alias of upstream's renumbered `SSF_VO=BIT(22)`,
   `hhSoundShaderParmsModifier`, idSoundWorld/idSoundEmitter virtual additions
   (ModifySound, GetSoundParms, CurrentAmplitude family, PlaceListener(areaName),
   RegisterLocation/ClearAreaLocations, SetSpiritWalkEffect/SetVoiceDucker, subtitle
   API, focus-mute API from WP2.6). Respect upstream's `UpdateEmitter` velocity-pure-
   virtual polarity.
4. `src/renderer/RenderWorld.h` — declarations only if needed for game compile before
   WP4.1; otherwise defer entirely to WP4.1 to keep the single ABI bump. If game
   compile requires the fields sooner, do WP4.1 first (see Phase 4 note on ordering).
5. `soundShaderParms_t` (D5): add `subIndex` + profanity triplet AFTER upstream's
   `attenuatedVolume`; comment the deliberate divergence at upstream's ABI-freeze note.

### WP3.5 — MVD / bots / repeater gating (decision D9)

- The executed gates are compile-time constants, not a Meson or runtime option:
  `OPENPREY_ENABLE_MVD=0`, `OPENPREY_ENABLE_BOTS=0`, and
  `OPENPREY_ENABLE_REPEATER=0`. Recording/playback, bot, and repeater entry points are
  inert; every gated site has an `OPENPREY-GATED(D9)` marker so re-enablement is
  greppable.
- `MultiViewDemo` schema constant: when later enabled, its game schema version must be
  Prey's `GAME_API_VERSION=7` (ledger interface map row 18) — leave a TODO at the
  constant now.
- Savegame compat header generation (`openq4_savegame_compat_generated.h`): keep the
  mechanism; it stamps the staged game snapshot — verify it works against the unified
  staging (WP3.1) manifest.

### Phase 3 gate

```powershell
tools/build/meson_setup.ps1 compile -C builddir     # full build incl. game module
meson install -C builddir --no-rebuild --skip-subprojects
# from .install/: SP start on the self-contained test map
.\openPREY-client_x64 +set r_fullscreen 0 +set fs_savepath ..\.home +map game/roadhouse_quick
```

Acceptance: unified `game_x64.dll` builds, stages to `basepr/`, loads through all
diagnostics phases, `roadhouse_quick` reaches gameplay; MP listen server starts
(`spawnServer`) with the same module; lowercase `logs/openprey_*.log` files show no
contract-drift fatals.

## 6. Phase 4 — renderer workstream

Ordering note: the plan originally grouped renderer layout work into one bump. Execution
used two coherent increments because the per-glyph material bridge followed the initial
Prey entity/time-group bridge; keeping each engine/module handshake internally consistent
matters more than phase numbering.

### WP4.1 — the ABI extension commit (RD-16 + RD-01/08 fields + RD-14 + RD-20 accessors)

Executed across two coordinated increments in all three binaries, ending at
`RENDER_API_VERSION 9`. Full inventory:

`renderEntity_s` additions (positions per OLD `RenderWorld.h`):
`const hhDeclBeam *declBeam`; `hhBeamNodes_t *beamNodes` (`MAX_BEAM_NODES 32`);
`bool weaponDepthHack`; `bool onlyVisibleInSpirit`; `bool onlyInvisibleInSpirit`;
`bool lowSkippable`; `float eyeDistance`; `int timeGroup`;
`bool notInRenderDemos` (under `_HH_RENDERDEMO_HACKS`).
`renderView_t`: `bool viewSpiritEntities`. `exitPortal_t`: `float frac`.
Portal attr: `PS_BLOCK_SOUND` (replaces Raven PS_BLOCK_GRAVITY semantics).
Constants: `SHADERPARM_MISC/ANY_DEFORM/ANY_DEFORM_PARM1/2/DISTANCE`,
21-enumerator `DEFORMTYPE_*` enum (`NONE=0` plus 20 effects through `PORTAL=20`),
`MAX_ENTITY_SHADER_PARMS 12→13` (+`EXP_REG_PARM12`
lands WP4.3 with its evaluator — keep the array-size bump here since it is layout).

`idRenderSystem` virtuals: `SetEntireSceneMaterial`, `IsScopeView/SetScopeView`,
`IsSpiritWalkView/SetSpiritWalkView`, `IsShuttleView/SetShuttleView`,
`SupportsFragmentPrograms`, `VideoCardNumber`, `LogViewRender`.
`idRenderWorld` virtuals (default-stub style): game portals (`FindGamePortal`,
`RegisterGamePortals`, `DrawGamePortals`, `GetGamePortalSrc/Dst`, `IsGamePortal`),
sound areas (`NumSoundPortalsInArea`, `LevelInit/ShutdownSoundAreas`,
`PrecalculateValidSoundAreas`, `ValidSoundArea`, `DistanceToSoundArea`,
`MaxSoundAreaExtents`, `DrawValidSoundAreas`, `GetSoundPortal`),
`GuiTrace(..., interactiveMask)` overload (map OLD `pt.frac` → upstream
`pt.fraction`; animated-model half of RD-14 is already upstream — port the overload
only), `DebugClearLines/DebugClearPolygons`, `DemoSmokeEvent`.
`idRenderModel`: `IntersectBounds`, `SetGameUpdatedModel`.

Executed `referenceSound` decision (flagged in plan D2/RD row 11): retain both the
integer `referenceSoundHandle` and `idSoundEmitter *referenceSound` on render entities
and lights. Shader evaluation prefers the pointer and resolves the handle when the
pointer is null; render-demo serialization preserves both representations.

Vulkan side: add stubs returning defaults for every new virtual; renderer-vk must
still build (D2).

Acceptance recorded: engine + renderer modules build together, and a deliberately stale
renderer module was rejected fail-closed by the version handshake before rebuilt modules
loaded normally.

### WP4.2 — clean feature ports

Items: RD-01, RD-02, RD-15, RD-17, RD-19, RD-21 (+RD-23 naming folded in).

- RD-01 hhBeam: copy `Model_hhBeam.cpp`; `Model_local.h` class decl; `.beam`
  extension dispatch in `idRenderModelManagerLocal::GetModel` (upstream still has the
  fork-shape dispatch; `.liquid` branch anchor at `ModelManager.cpp:485`). Add to
  meson_sources for BOTH the engine's shared render_geo context and renderer modules
  as upstream's layout requires (beam model is front-end model code — engine side).
- RD-02 prt: replace upstream's stub `InstantiateDynamicModel` with OLD's Doom3
  implementation; depends on WP2.4 DECL_PARTICLE.
- RD-15: `ShouldSkipPlayerTraceEntity` transplant into upstream `Trace`
  (`RenderWorld.cpp:1580-1645` still verbatim fork code).
- RD-17 corona deform: DFRM_CORONA parse + `R_CoronaDeform`; keep OLD's enum insertion
  position (before DFRM_EXPAND); Phase 8 visual check that upstream's lens-flare
  system doesn't double-draw coronas.
- RD-19 shaderLevel: `r_shaderLevel` cvar + shaderLevelN/shaderFallbackN token
  handlers in ParseStage; verify materials re-parse on change under upstream's
  MaterialResourceTable caching (`reloadDecls`/vid_restart is acceptable parity with
  OLD — document in `docs/user`).
- RD-21 robustness: BoundsInAreas clamp, corner-cull guard, lockSurfaces viewDefs;
  SKIP the drawSurf->area init half (upstream has its own area member — verify only).

### WP4.3 — material system rework

Items: RD-03, RD-04, RD-20.

- RD-03: merge into upstream's evolved `Material.cpp` parser: HUMANHEAD
  CONTENTS_/SURFTYPE_ tables + matter_ aliases; no-op tokens (lightWholeMesh,
  specularExp, highres, skipClip, glass/skybox/overlay macros, seeThru family);
  decal_alphatest/scorch macros; `blend shader`; `deform beam`→DFRM_TUBE, jitter
  consumed; profilemap→idSndWindow; EXP_REG_PARM12 + `distance` alias with
  SHADERPARM_DISTANCE evaluation in `tr_light.cpp` (EvaluateRegisters call sites
  moved: ~1650/~1997, already take a resolved `idSoundEmitter*`); timeGroup time
  override in `R_AddDrawSurf` via `game->GetTimeGroupTime` (upstream still has it
  commented at `tr_light.cpp:2110`). DROP the halves already upstream: fragmentParm/
  MAX_VERTEX_PARMS=8, texgen screen/screen2/glassWarp.
- RD-04 decalInfo fade: extend `decalInfo_t` {fadeTime, start/end RGBA}; full D3
  ParseDecalInfo; re-implement stay+fade lifetime and vertex-color interpolation
  against upstream's rebatched `ModelDecal.cpp` (+402 upstream — re-derive
  `AddDecalDrawSurf` integration, don't cherry-pick).
- RD-20 view-mode expressions: extend `expRegister_t` with
  EXP_REG_SCOPE_VIEW/SPIRIT_WALK/SHUTTLE_VIEW (+ dynamic FRAGMENT_PROGRAMS/
  GLSL_PROGRAMS registers from RD-05), evaluated from the WP4.1 accessors; follow
  upstream's `EvaluateRegisters`/`EvaluateStageRegisters` split and check
  MaterialResourceTable serialization of the register table (EXP_REG_NUM_PREDEFINED
  changes layout — verify cache invalidation/version).

### WP4.4 — backend features (highest renderer risk)

Items: RD-05, RD-09.

- RD-05 SL_INTERACTION: port `R_ARBProgramSourceUsesInteractionInputs` detection into
  upstream's restructured `R_LoadARBProgram`; SL_INTERACTION classification;
  `RB_ARB2_DrawShaderInteraction` re-implemented against upstream's ~12k-line
  `draw_arb2.cpp` (anchors: progs[] table `:~11910`, `R_FindARBProgram`,
  `glConfig.allowARB2Path`); `drawInteraction_t.alphaTestThreshold` → fragment env
  param 7; polygon-offset move into `RB_CreateSingleDrawInteractions` (verify against
  upstream's current shape); `r_useFragmentProgramMaterials` cvar. MUST-CHECK list
  from ledger: shadow-map receiver paths honor or skip SL_INTERACTION stages
  coherently; Apple GL2.1 simple-interaction path ignores them gracefully; ModernGL
  executor either skips (with one-time warning) or is gated off for materials using
  them (legacy-path-first per D2).
- RD-09 glow overlay: re-sequence `RB_STD_DrawGlowView`/`RB_STD_GlowOverlay` against
  upstream's post stack ordering inside `RB_STD_DrawView`
  (anchors: `draw_common.cpp:10641` RB_STD_DrawView, `:6873`
  RB_STD_T_RenderShaderPasses): glow capture happens BEFORE scene-target present;
  reuse upstream's post-process helpers (scratch FBO/blit utilities) instead of OLD's
  copies; new intrinsics `_glowScreen`/`_glowComposite`; `r_glow*` cvars; upstream's
  existing `r_skipGlowOverlay` cvar becomes the real skip flag (it exists for the
  lightgrid baker — wire it).

### WP4.5 — subviews and visibility

Items: RD-06, RD-07, RD-08.

- RD-06 Prey portals: `subviewClass_t` on idMaterial (`directportal <distExpr>`,
  `skyboxportal`), DI_PORTAL_RENDER/DI_SKYBOX_RENDER + portalRenderMap/
  skyboxRenderMap stages (merge into upstream's `dynamicidImage_t` which gained
  DI_REFLECTION/REFRACTION_RENDER — append, keep upstream order);
  `R_PortalSubviewBySurface` retail transform + gates; `R_PortalSkyboxSubviewBySurface`
  once-per-frame guard; `R_GenerateSurfaceSubview` dispatch (upstream anchor `:586`,
  structure survives). Cooperate with upstream's shadow subview policy
  (commit f3198bb7): portal subviews must respect the importance budget or opt out
  explicitly. Mirror clip plane: keep OLD's `numClipPlanes=0` workaround initially
  (known parity gap, `references/retail_mirror_clip_parity_plan.md`) — do NOT adopt
  upstream's clip-plane-on state for Prey mirrors without retail validation.
- RD-07 viewID normalization: introduce `R_EffectiveViewIDForSubview` and RE-ENUMERATE
  all comparison sites on the upstream tree (known: `Interaction.cpp:2053`,
  `tr_light.cpp:1022`, `RenderWorld_portals.cpp:815`; NEW candidates: shadow-map
  caster selection, ModernShadowPlanner — grep `suppressSurfaceInViewID|
  allowSurfaceInViewID|suppressShadowInViewID|suppressLightInViewID|
  allowLightInViewID|weaponDepthHackInViewID`); restore `weaponDepthHack`
  propagation.
- RD-08 spirit visibility: filters in `AddAreaEntityRefs` (`RenderWorld_portals.cpp
  ~:800`) and `idInteraction::AddActiveInteraction` (`Interaction.cpp:1816`); ALSO
  apply to shadow-map caster gathering (ledger warning: spirit-only entities must not
  cast in normal views) — add a Phase 8 visual check.

### WP4.6 — images, formats, world load

Items: RD-12, RD-18 (+corelibs-20).

- RD-12: `openPREY_RemapLegacyImageName` inserted at upstream's
  `R_NormalizeInternalImageName` call sites (`ImageManager.cpp:273`, sites
  560/609/662/733); retail texture fallbacks re-sited in `ActuallyLoadImage`
  (`Image_load.cpp:385`); `_threshold` intrinsic + `_replay` placeholder; opaque
  `R_BlackImage`; TD_FONT DeriveOpts path; `g_lowresFullscreenFX` clamp re-implemented
  against upstream's scratch-FBO Copy helpers (`Image_load.cpp:997/1142`). DROP the
  already-upstream halves listed in the ledger (ImageHandleDeferred, ScratchImage
  realloc, warning suppression, isPersistant skip).
- RD-18 + corelibs-20 (ideally upstream-first per plan §3.2): accept
  `mapProcFile003` in renderer `InitFromMap` (upstream `RenderWorld_load.cpp` +888 —
  re-merge header logic; 12-float vertex half already upstream via
  `Parse1DMatrixOpenEnded:703`) and in `cm::LoadProcBSP` (rebuild dual-format branch
  inside upstream's rewritten function: LexerFactory path, CRC/out-of-date semantics
  for CRC-less D3 procs — define: D3 procs are never "out of date", log once);
  portalArea globalBounds capture; verify binary-lexer path tolerates D3 files.

### WP4.7 — executed drops + verifications

Items: RD-10, RD-11, RD-13, RD-22, FW-33, FW-23 (framework twins).

1. Nothing to port. Actions: mark `DROPPED` in ledger; then:
2. Re-express openPREY defaults as cvar defaults only (from OLD: r_hdrToneMap etc. —
   diff OLD `RenderSystem_init.cpp` cvar defaults vs upstream and carry the wanted
   ones in one commit `rebase: openPREY renderer cvar defaults`).
3. RD-22 residual: verify `Model_lwo` uintptr_t casts exist upstream (ledger could not
   confirm — grep upstream's +43-line Model_lwo diff; port the two casts if absent).
4. Lightgrid: validate upstream's system against Prey content in Phase 8 (bake one map
   with upstream `bakeLightGrids`, D3-proc interaction from WP4.6).

### WP4.8 — shaders and shader-adjacent assets

Items: assets-02 (drop+adopt), assets-03 (rework).

- Delete OLD's `openprey_*` post-FX shader mirrors from content (never copied in
  WP1.3); ensure `content/basepr/pak0/glprogs/` carries upstream's current GLSL set
  (either by referencing upstream's content dir in the pak manifest or copying —
  prefer a build-time copy from a single source to avoid drift) and upstream's
  `postprocess_openq4.mtr` (rename decision: keep upstream material names
  `postprocess/black` etc.; renderer references follow upstream).
- assets-03 interaction.vfp: validate OLD's Prey-extended `interaction.vfp` against
  upstream's env-parameter contract (`draw_arb2.cpp:~11357` env[16].xy vertex-color
  packing; `r_interactionColorMode` sniffing at `:1721`): update the program source to
  satisfy the contract, keep the alpha-test env param wired to WP4.4's plumbing; ship
  in `content/basepr/pak0/glprogs/`.

### Phase 4 gate

- Retail Prey map (`roadhouse` + one spiritwalk-heavy map) on renderer-gl/ARB2:
  beams render, `.prt` effects render, portals + portal skybox correct, spiritwalk
  visibility correct (both directions), glow overlay present, no material parse
  warnings for stock assets, weapon depth hack correct in mirrors.
- Upstream renderer self-tests (commit-validation lanes, `r_shaderReport`) pass.
- renderer-vk still builds and fails closed to GL at runtime for Prey materials.

## 7. Phase 5 — sound, UI, session features

### WP5.1 — subtitles end-to-end (sound-01, FW-27, assets: hud_subtitles/subtitles.gui)

Engine order: shader parsing (`subtitleN`/`subtitlecombatN` → tables; re-seat into
upstream's restructured ParseShader) → `soundShaderParms_t.subIndex` (layout landed
WP3.4) → backend queue `CollectActiveSubtitles` in `Render()` with the audibility gate
REWRITTEN for linear volumeScale (OLD gated on `volumeDB <= DB_SILENCE`; use
upstream's sanitized linear scale ≤ epsilon) → frontend sync →
`sessLocal.ShowSubtitle/HideSubtitle` (FW-27: guiSubtitles load, 3-line rolling
display, redraw after `game->Draw` re-placed in upstream's reshaped
`idSessionLocal::Draw` — anchors: demo-viewer overlays, cinematic bars, FPS overlay) →
`listSubtitles` command → demo/save defaults `subIndex=-1` (`ProcessDemoCommand` now
returns bool — follow upstream's error path).

### WP5.2 — profanity censor (sound-02; corelibs-12 landed WP2.1)

`bleep <index> <delay> <duration>` parse; re-express the hold window in the linear
pipeline: `idSoundFade.fadeHold` forcing a 0.0 linear scale inside [start,end] then
restoring (OLD's dB-offset math is meaningless now — ledger sound-02); propagate via
OverrideParms (upstream extended it — merge); clear fadeHold on savegame restore;
`com_profanity` gates both this and the LangDict filter.

### WP5.3 — dB volume layer (sound-03, ui-05)

Keep upstream's linear pipeline untouched. Add: `SyncVolumeAliasCVars`
(s_volume↔s_volume_dB, s_musicVolume↔s_musicvolume_dB; dB wins on conflict) called
from Render()/Init(); `DBtoLinearClamped` helper; upstream's unused `s_volume_dB`
cvar becomes the alias target. Do NOT drive listener AL_GAIN (upstream fixed it at
1.0/0.0 — the alias sync makes OLD's listener change unnecessary).
ui-05: `volumeslider` winvar mapping slider 0–1 ↔ dB (−60..0) re-seated into
upstream's reworked `SliderWindow` (HandleEvent switch, ClampAndSnapValue); dynamic
scrollbar thumb sizing (`UpdateThumbMetrics`) and ON_SLIDERCHANGE firing re-inserted
into the new bodies.

### WP5.4 — Prey sound-shader dialect (sound-05; decision D5 executes here)

- `DOOM_TO_METERS = 0.0254`: change and then re-validate the WHOLE attenuation chain
  (upstream consumes it in more places: directDistance, spatializedDistance,
  SetInnerRadius, SoundWorldApplyDistanceFalloff) — A/B against retail by ear on a
  known map; document result.
- `volume` token = dB (Prey semantics) while upstream's writer emits `volumeDb`:
  thread Prey interpretation through parse AND the decl text writer so round-trips
  preserve semantics.
- Sound classes: adopt Prey's `SOUNDCLASS_*` 0–4 (`SC_MUSIC=4`,
  `SOUND_MAX_CLASSES=5`); map upstream's `SOUND_CLASS_MUSICAL=3` usage
  (`SoundUsesMusicVolume`) onto the Prey numbering — one definition site, engine+game
  agree (ABI rule 4); symbolic token parsing (SC_NORMAL…SC_MUSIC).
- Token aliases: noPortalFlow→SSF_NO_OCCLUSION, omniwhenclose→SSF_OMNIDIRECTIONAL,
  noreverb metadata, legacy `if` skipped, jawflap→SSF_VOICEAMPLITUDE (=upstream
  SSF_VO BIT(22) — set alias accordingly).

### WP5.5 — sound API shim implementations (sound-06)

Concrete impls against the rewritten emitter internals: `ModifySound` (via
hhSoundShaderParmsModifier), `GetSoundParms` (convert linear→dB on the way out),
`CurrentAmplitude/CurrentVoiceAmplitude` (SSF_VOICEAMPLITUDE-filtered; re-derive from
upstream's channel structure — `CurrentAmplitude` anchor `snd_local.h:438`),
`CurrentShakeAmplitudeForPosition`, `PlaceListener(..., areaName)` +
`RegisterLocation/ClearAreaLocations` (sound-area names feed WP4.1's sound-area
virtuals — engine-side storage), SetSpiritWalkEffect/SetVoiceDucker no-ops for now
(retail DSP effects are a later parity project — note in TODO).

### WP5.6 — focus mute wiring (FW-26 + sound-04)

Replace upstream's `Session_ShouldSilenceAudioWhenUnfocused` NULL-world mechanism
wholesale with `soundSystem->SetMuteForFocus(ShouldMuteForFocus())` in Session Frame;
IsMutedExplicitly-based stale-mute recovery; keep upstream's
`Sys_SDL_IsGameWindowFocused` as the focus source; retire `s_muteUnfocused` or map it
onto the new path (keep the cvar name working).

### WP5.7 — GUI dialect and Prey UI (ui-01, ui-06, ui-07, ui-08, ui-09; drops ui-11/12/13)

- ui-01 (L): re-seat hunk-by-hunk into upstream's rewritten `Window.cpp`:
  def-keyword extensions in Parse/InitFromFile (upstream Parse anchor `:3289` still
  matches only windowDef/animationDef); tab-container runtime; superWindowDef frames;
  button segment backgrounds INTO upstream's rewritten DrawBackground (now has
  MATCOVER/MATFIT/MATCANVASFILL + underlays — Prey segments become another branch);
  retail special vars; AddChildWindow; invisible-rect Redraw behavior. Window flag
  space is tighter upstream (claimed bits listed in ledger) — re-assign OLD's flag
  values to free bits and static_assert no overlap.
- ui-06: fonts path fallback `fonts/` (+ registration-order fix), `gui_smallFontLimit`
  0.10, Prey cursor materials + CURSOR_MENU (+CURSOR_COUNT), size 15 — validated
  under the TTF/bitmap dual pipeline with `r_useTrueTypeFonts=0` by default (D10).
- ui-07: side-slice half only (backgroundLeft/Right/Top/Bottom winvars +
  DrawBackgroundExpansionSlice) integrated with upstream's underlay system;
  matcanvasfill half is already upstream — drop.
- ui-08: shear→idWinVec2 merged with upstream's existing
  `regList.FindReg("shear")` transform check; savegame layout change coordinated with
  upstream's hardened GUI savegame IO (bump/flag the GUI save marker — verify
  restore of an OLD-format save is NOT required across the rebase; savegame compat
  policy is "Prey saves from this port only", note in docs).
- ui-09: shadow-alpha + ChoiceWindow textAlign honored — re-applied into the new
  DrawText call shapes (extra args); check currentChoice guard redundancy.
- Execute drops: ui-11 (use upstream `Redraw(time, false)` — ensure WP4 world-GUI
  call sites pass false), ui-12, ui-13; sound-07, sound-08 (verify Prey menu writes
  `s_deviceName` — sound settings GUI publishes via WP5.9 FW-32).

### WP5.8 — retail spline/credits text effect (ui-02, conflict-major — schedule last in phase)

Redesign against the TTF-aware glyph pipeline: per-glyph injection re-anchored at
`PaintChar` (`DeviceContext.cpp:1226`); handle scaledFont.renderScale, icon glyphs,
variable-length escapes; Window-side credit arming re-seats with ui-01. With D10
(TTF off for openPREY), first implement for the bitmap path and no-op under TTF with
a logged notice; TTF support is follow-up.

### WP5.9 — session features (FW-22, FW-25, FW-30, FW-31, FW-32, FW-24)

- FW-22 loading pipeline: keep upstream's expansion machinery
  (`Session_PrepareExpandedLoadingBackground` — imagetools-based) and REDIRECT inputs:
  Prey fallback GUI chain (`guis/map/loading.gui` first), Prey GUI state vars,
  `PrintLoadingMessage` on engineWindowState with Prey splash-material candidates +
  bigchars text; FindMapScreenshot already Prey (WP2.5). Drop FW-23 (obsolete).
- FW-25 music routing: rebuild against current `ExecuteMapChange` (loading asset
  queue, split-map campaign state): `snd_loadmusic` on menuSoundWorld,
  `Session_ServiceLoadingSound` mixing, `SetPlayingSoundWorld` rewrite (menu world
  during loads), `guisounds_menu_music` in StartMenu, emitter cleanup at handoff,
  `g_levelloadmusic`. Coordinate with upstream's emitter-lifecycle and
  OpenAL-restart behavior.
- FW-30: default `com_skipLoadingContinue 1` (keep upstream's gate code — lower
  conflict than deletion; ledger recommendation).
- FW-31 menus: Prey RescanMaps via idListGUI, Deathmatch default, player-model scan
  from `player_tommy_mp`, roadhouse/wicked/casino state vars, guiIntro escape latch,
  LISTEN_SERVER_MAX_PLAYERS — every hook re-located in upstream's +2588-line
  Session_menu (settings/controller/localization refactors).
- FW-32 audio menu: publish device list/EAX state into Prey GUI states ON TOP of
  upstream's device-monitor/restart machinery (no raw ALC calls in Session_menu).
- FW-24 savegames (conflict-major): implement as EXTENSIONS of upstream's hardened
  framework: add `"Prey"` to the supported-gamename set
  (`Session_IsSupportedSaveGameName`), write gamename "Prey"; version policy: adopt
  upstream's live `SAVEGAME_VERSION=1834`. The generated staged-game payload stamp is
  SHA-256 `6b7bd1d91fd551e5f972dc1c3900c8483b58fb586f60dbdf7eab89bb48e7a7d7`
  across 376 source files. The companion repository's framework-header value `114` is
  archival/non-runtime: that framework tree is neither staged nor compiled. Multi-gamedir
  search (fs_game → basepr → base) via
  `Session_BuildSaveGameSearchDirs` grafted into upstream's read path;
  quickload double-press confirm using `MaterialKeyForBinding` (WP2.5); guiSave toast
  wiring; GetSaveGameList aggregation re-built on upstream's reworked
  `Session_menu.cpp:1731`.

### Phase 5 gate

Subtitled cutscene plays with `g_subtitles 1` (+ censored variant with
`com_profanity 0` — audio bleep and text replacement); volume sliders move audible
levels and persist across restart in dB cvars; main menu fully navigable with Prey
art, tabs, save/load lists across gamedirs; quickload prompt shows the bound key;
level-load music plays; loading screens aspect-correct.

## 8. Phase 6 — filesystem and platform polish

### WP6.1 — Prey PK4 validation (FW-03)

Graft the dual-layout policy (classic pak000–004 vs digital pak_data/pak_sound/
pak_en_*) onto upstream's richer flow: set-detection before
`ValidateRequiredOfficialPaks`; zero-checksum = presence-only (until classic
checksums are captured — open TODO); keep upstream's game-binary-pk4 ignore
(`FS_IsIgnoredOfficialGameBinaryPk4` — Prey equivalents: none known, verify),
content-search diagnostics, and mod-manifest system. Update
`docs/dev/prey-rebase/official-pk4-checksums.md` (WP7.3).

### WP6.2 — install discovery (FW-04)

Add `FS_BuildRegistryInstallCandidates` (Human Head/2K/3D Realms keys, App Paths,
Uninstall scan) and `FS_BuildKnownInstallCandidates` as ADDITIONAL candidate sources
feeding upstream's `FS_TryResolveBasePathCandidate` resolver; RETARGET (don't delete)
Steam/GOG discovery to Prey's store folder names (`steamapps/common/Prey`, GOG
Prey ids); `FS_HasGameFilesAtBasePath` probes Prey pak names through the resolver.

### WP6.3 — residual sys decisions

- cpu-easyargs-win64: inspect the staged game's `Class.cpp` event dispatch on x64
  (upstream game code ships x64 with CPU_EASYARGS=1 — determine its fix; Prey's
  event set may still need the `_WIN64 → CPU_EASYARGS 0` override). Coordinate the
  one-line header change with a game-side check; add an event-dispatch smoke test to
  Phase 8 (fire a float+entity-arg script event on x64).
- focus-audio-muting residual: two-line hardening (activeApp early-return, null-hWnd
  guard) for native `Sys_IsGameWindowFocused` only if `legacy_win32` backend is kept.
- FW-37: verify Prey-appropriate r_mode preset indices inside upstream's
  `Com_ExecMachineSpec`; FW-34 confirmed dropped (no forced-windowed stomp).

## 9. Phase 7 — build, CI, packaging, docs, strings

### WP7.1 — CI (nightly-ci-overhaul redesign; decision D8)

Rebrand+adopt upstream workflows: `commit-validation.yml`, `push-verification.yml`,
`manual-release.yml`, (optional `discord-release.yml`); strip lanes that don't apply
yet (macOS signoff, ARM64 evidence gates) into allow-fail or removal, documented.
If nightly is still wanted: a thin scheduled workflow calling upstream's release
tooling with `--prerelease nightly-` tags (rebuild, no merge of OLD's yml).
Payload validation: assert unified `game_<arch>` module + `basepr/` staging +
desktop/icons in the artifact.

### WP7.2 — packaging (package-nightly-prey rework)

Re-express Prey identity as configuration on upstream's grown `package_nightly.py`
(+ `build_windows_installer.py`, `package_linux_appimage.py`,
`assemble_macos_universal2.py`): PRODUCT_NAME openPREY, stem openprey, GAME_DIR
basepr, unified module name, Prey pk4 exclusion rules, OpenAL payload; drop OLD's
ad-hoc .app bundling in favor of upstream's.

### WP7.3 — docs (assets-09) and dev-tooling (vscode-prey-launch)

- Adopt upstream's `docs/dev` + `docs/user` structure; port Prey docs (pk4 checksums,
  porting baseline, input-key matrix, THIS plan + implementation guide + ledger →
  `docs/dev/prey-rebase/`); redo edits to upstream-rewritten pages
  (platform-support, display-settings — r_mode −2 text already upstream, keep
  branding only); refresh TECHNICAL/BUILDING/README from upstream's with Prey deltas
  (companion staging docs point at canonical direct staging per D1).
- `.vscode`: adopt upstream `meson-task.ps1` layout; regenerate the Prey per-map SP
  launch configs (map list from OLD launch.json; args `+set fs_game basepr`,
  `fs_savepath ${workspaceFolder}\.home`, windowed) — script the generation so the
  map list stays maintainable.
- Port `tools/assets/extract_menu_background_tiles.ps1` (audit gap) alongside
  assets-05's art.

### WP7.4 — strings (assets-04)

**Executed decision:** retain openPREY's private `#str_122xxx` menu IDs so existing GUI
and engine bindings stay stable. Rename `*999.lang` to `<lang>_openprey.lang` in
`content/basepr/pak0/strings/`; no ID remap is required. The status ledger records this
choice under assets-04.

## 10. Phase 8 — validation and cut-over

### WP8.1 — automated matrix

| Check | Recorded result |
|---|---|
| Windows full build/install | **1003/1003 build targets completed**; `.install/` staging succeeded |
| Windows engine-only build | **767/767 build targets completed** |
| Linux build | **1001/1001 build targets completed** |
| Renderer foundation self-tests | Passed; proves shared renderer infrastructure, not complete ModernGL/Vulkan Prey parity |
| ABI handshake | A deliberately stale renderer module failed closed; rebuilt modules loaded normally |
| Unified module diagnostics | Canonical module and legacy loader aliases reached retail gameplay with all load phases green |
| Event dispatch x64 (WP6.3) | Float+entity event-dispatch smoke passed with `CPU_EASYARGS=0` |
| Pak validation | Digital retail layout passed; a synthetic numbered-pak layout reached the settled menu, but genuine CD-era checksums remain deferred |
| Package archive/runtime | **Passed**; the Windows x64 archive manifest and payload were inspected, then an isolated windowed launch reached Roadhouse and wrote an engine screenshot |
| Root validator | **Passed**; the push profile completed every active check, including staged-payload validation |

### WP8.2 — runtime matrix (retail assets)

Recorded representative runtime evidence:

1. Neutral-directory install auto-detection resolved the retail asset root and reached
   the stock menu.
2. Retail gameplay ran on Roadhouse, Feeding Tower, deathwalk/spirit, Shuttle, and
   `girlfriendx`/portal scenes. These are representative captures, not an exhaustive
   per-map or renderer-backend parity matrix.
3. Save/load round-trip and legacy config-casing migration passed.
4. The unified module loaded through canonical and legacy aliases; a requested Vulkan
   renderer fell back to GL as designed when Prey Vulkan parity was unavailable.
5. A listen server and two clients ran together; network `_attackalt` reached the game
   through the merged `usercmd_t` layout.
6. Two isolated `roadhouse_quick` dmap runs emitted byte-identical `.cm`/`.proc` files,
   and the generated geometry loaded successfully. A bounded light-grid bake completed,
   while a deliberately zero-probe bake honored `-quit` and shut down cleanly.
7. Runtime logs use lowercase `logs/openprey_*.log` names.

Still unrun or incomplete: the exhaustive SP/MP/manual visual matrix, localization,
subtitle/censor/audio checks, full ModernGL/Vulkan parity, genuine CD-era checksum capture,
and macOS/ARM64 release-lane signoff. These are explicit parity/release deferrals rather
than unclosed rebase evidence gates.
The mirror clip-plane workaround remains active and tracked in
`references/retail_mirror_clip_parity_plan.md`.

### WP8.3 — cut-over

1. Ledger sweep: complete at 103 PORTED / 34 DROPPED across all 137 IDs. Partial
   deferrals are explicit below; none hides inside an undispositioned catalog item.
2. Local cut-over completed at closure: `new-prey` points at the closing rebase head and
   `pre-oq4-rebase` remains preserved permanently. Remote default-branch retargeting is an
   operational follow-up, not a local implementation gate.
3. Record the sync point: add `UPSTREAM_BASE: d41186e4 (2026-07-31)` to the ledger
   README; future upstream syncs are ordinary merges from `openq4-upstream/main`
   (histories now shared), with the ledger's rework items as the conflict map.
4. Open upstream candidates (§3.2 of the plan) as issues/PRs on openQ4 with links to
   the ported commits.

## 11. Item → work-package index (all 137)

| Ledger ID | Disposition | WP |
|---|---|---|
| RD-01 | reapply-clean | 4.1 (fields) + 4.2 |
| RD-02 | reapply-clean | 4.2 |
| RD-03 | reapply-rework | 4.3 |
| RD-04 | reapply-rework | 4.3 |
| RD-05 | reapply-rework | 4.4 |
| RD-06 | reapply-rework | 4.5 |
| RD-07 | reapply-rework | 4.5 |
| RD-08 | reapply-rework | 4.1 (fields) + 4.5 |
| RD-09 | reapply-rework | 4.4 |
| RD-10 | obsolete-upstream | 4.7 |
| RD-11 | obsolete-upstream | 4.7 (record: WP0.3) |
| RD-12 | reapply-rework | 4.6 |
| RD-13 | obsolete-upstream | 4.7 |
| RD-14 | reapply-rework | 4.1 |
| RD-15 | reapply-clean | 4.2 |
| RD-16 | reapply-rework | 4.1 |
| RD-17 | reapply-clean | 4.2 |
| RD-18 | reapply-rework | 4.6 |
| RD-19 | reapply-clean | 4.2 |
| RD-20 | reapply-rework | 4.1 (accessors) + 4.3 |
| RD-21 | reapply-clean | 4.2 |
| RD-22 | obsolete-upstream | 4.7 (verify Model_lwo) |
| RD-23 | reapply-clean | folded into 4.2–4.8 |
| FW-01 | reapply-clean | 1.2 |
| FW-02 | reapply-clean | 1.2 |
| FW-03 | reapply-rework | 6.1 |
| FW-04 | reapply-rework | 6.2 |
| FW-05 | reapply-clean | 2.5 |
| FW-06 | reapply-clean | 2.5 |
| FW-07 | reapply-clean | 2.5 |
| FW-08 | reapply-clean | 2.5 |
| FW-09 | reapply-clean | 2.5 |
| FW-10 | reapply-clean | 2.5 |
| FW-11 | reapply-rework | 3.2 |
| FW-12 | conflict-major | 3.3 |
| FW-13 | obsolete-upstream | — (drop) |
| FW-14 | reapply-rework | 2.4 |
| FW-15 | reapply-rework | 2.4 |
| FW-16 | reapply-clean | 2.4 |
| FW-17 | reapply-clean | 2.4 |
| FW-18 | reapply-rework | 2.4 |
| FW-19 | reapply-clean | 2.5 |
| FW-20 | reapply-rework | 1.2 (theme) + 5.7 (charset) |
| FW-21 | reapply-rework | 3.4 |
| FW-22 | reapply-rework | 5.9 |
| FW-23 | obsolete-upstream | — (drop; art in 7.3/assets-05) |
| FW-24 | conflict-major | 5.9 |
| FW-25 | reapply-rework | 5.9 |
| FW-26 | reapply-rework | 5.6 |
| FW-27 | reapply-rework | 5.1 |
| FW-28 | reapply-clean | 2.5 |
| FW-29 | reapply-clean | 2.5 |
| FW-30 | reapply-rework | 5.9 (cvar default) |
| FW-31 | reapply-rework | 5.9 |
| FW-32 | reapply-rework | 5.9 |
| FW-33 | obsolete-upstream | — (drop; record WP0.3) |
| FW-34 | drop | — |
| FW-35 | obsolete-upstream | — |
| FW-36 | reapply-clean | 2.5 |
| FW-37 | obsolete-upstream | 6.3 (preset verify) |
| FW-38 | reapply-clean | 2.5 |
| FW-39 | reapply-clean | 2.5 (parts) |
| sound-01 | reapply-rework | 5.1 |
| sound-02 | reapply-rework | 5.2 |
| sound-03 | reapply-rework | 5.3 |
| sound-04 | reapply-clean | 2.6 (+5.6 wiring) |
| sound-05 | reapply-rework | 3.4 (interface) + 5.4 |
| sound-06 | reapply-rework | 3.4 (interface) + 5.5 |
| sound-07 | obsolete-upstream | — (verify in 5.7 drops) |
| sound-08 | obsolete-upstream | — (verify in 5.7 drops) |
| ui-01 | reapply-rework | 5.7 |
| ui-02 | conflict-major | 5.8 |
| ui-03 | reapply-clean | 2.6 |
| ui-04 | reapply-clean | 2.6 |
| ui-05 | reapply-rework | 5.3 |
| ui-06 | reapply-rework | 5.7 |
| ui-07 | reapply-rework | 5.7 |
| ui-08 | reapply-rework | 5.7 |
| ui-09 | reapply-rework | 5.7 |
| ui-10 | reapply-clean | 2.6 |
| ui-11 | obsolete-upstream | — (WP4 call sites pass false) |
| ui-12 | obsolete-upstream | — |
| ui-13 | obsolete-upstream | — |
| corelibs-01 | reapply-clean | 2.1 |
| corelibs-02 | reapply-clean | 2.2 |
| corelibs-03 | reapply-clean | 2.2 |
| corelibs-04..07 | obsolete-upstream | — |
| corelibs-08 | reapply-clean | 2.1/2.4 |
| corelibs-09 | reapply-clean | 2.1 |
| corelibs-10 | reapply-clean | 2.1 |
| corelibs-11 | reapply-clean | 2.1 |
| corelibs-12 | reapply-clean | 2.1 |
| corelibs-13 | reapply-rework | 2.1 |
| corelibs-14 | reapply-clean | 2.1 |
| corelibs-15 | reapply-clean | 2.1 |
| corelibs-16 | reapply-rework | 2.3 |
| corelibs-17 | reapply-rework | 2.3 |
| corelibs-18 | obsolete-upstream | — |
| corelibs-19 | reapply-clean | 2.3 |
| corelibs-20 | reapply-rework | 4.6 |
| corelibs-21 | reapply-clean | 2.3 |
| corelibs-22 | reapply-clean | 2.3 |
| corelibs-23 | reapply-rework | 2.3 |
| corelibs-24 | reapply-clean | 2.3 |
| corelibs-25 | reapply-clean | 2.3 |
| multi-platform-meson | obsolete-upstream | — |
| toolchain-warn-suppression | reapply-clean | 2.6 |
| unified-game-module | conflict-major | 3.1 |
| gamelib-sync-scripts | conflict-major | 3.1 |
| nightly-ci-overhaul | conflict-major | 7.1 |
| package-nightly-prey | reapply-rework | 7.2 |
| openal-dll-staging | obsolete-upstream | — |
| linux-desktop-icons | reapply-clean | 1.2 |
| vscode-prey-launch | reapply-rework | 7.3 |
| rebrand-openprey | reapply-rework | 1.2 |
| portability-64bit-fixes | obsolete-upstream | — |
| cpu-easyargs-win64 | reapply-rework | 6.3 |
| macos-native-bringup | obsolete-upstream | — |
| focus-audio-muting | obsolete-upstream | 6.3 (optional hardening) |
| dxgi-videoram | obsolete-upstream | — |
| sdl3-gui-cursor-bounds | obsolete-upstream | — (QA in 8.2) |
| console-color-tweak | obsolete-upstream | 1.2 (as theme) |
| linux-native-fixes | obsolete-upstream | — (path string in 1.2) |
| lglcd-idsys-hooks | reapply-clean | 2.6 |
| assets-01 | reapply-clean | 1.3 |
| assets-02 | obsolete-upstream | 4.8 |
| assets-03 | reapply-rework | 4.8 |
| assets-04 | reapply-rework | 7.4 |
| assets-05 | reapply-rework | 5.9/7.3 (art + tool; engine side dropped) |
| assets-06 | reapply-clean | 1.3 (+8.2 verify) |
| assets-07 | drop | 0.5 |
| assets-08 | reapply-rework | 1.2 |
| assets-09 | reapply-rework | 7.3 |
| assets-10 | conflict-major | 3.1 |

## 12. Deferral register (expected open items at cut-over)

| Deferred item | Tracking |
|---|---|
| MVD / bots / repeater on Prey API | D9 gate markers; MultiViewDemo schema TODO |
| ui-02 spline text under TTF | WP5.8 note; `r_useTrueTypeFonts=0` (D10) |
| ModernGL / Vulkan Prey feature parity | D2; renderer-vk stubs from WP4.1 |
| Nested BSE `SEG_EFFECT` spawning | D3; add a Prey-v7-compatible callback/spawn path and stock-FX runtime capture before enabling |
| SetSpiritWalkEffect / SetVoiceDucker DSP | WP5.5 no-ops; TODO |
| Classic-layout PK4 checksums | WP6.1; docs TODO |
| Mirror clip plane retail math | `references/retail_mirror_clip_parity_plan.md` |
| ARM64/macOS/signing/manual-publish release lanes | Rebuild false-gated inherited lanes around `OpenPrey-game`, `basepr`, the unified module, and openPREY release identities |
| Remote default-branch cut-over | WP8.3.2 local branch cut-over is complete; remote retargeting is an operational follow-up |
| Upstream-first PR candidates | plan §3.2 list; WP8.3.4 |

Package production/inspection/runtime-smoke evidence and the root-validator run are closed.
The table above contains only explicit feature, parity, release-lane, and upstream follow-up
work; none is an undisclosed rebase implementation gate.
