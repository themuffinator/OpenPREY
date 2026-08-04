# openPREY rebase disposition and validation ledger

UPSTREAM_BASE: d41186e4 (2026-07-31)

- Branch baseline: `prey-on-oq4`
- Companion game baseline: `OpenPrey-game` at `61ef246`
- Preserved pre-rebase tag: `pre-oq4-rebase`
- Ledger generated: 2026-08-01

This is the current execution ledger for the 137 IDs in the six catalog files. The
historical catalogs explain what changed between the old fork and upstream; this file
records what the rebase actually did.

- **PORTED** means the implementation is present in the rebased tree.
- **DROPPED** means the old delta was intentionally not replayed, normally because
  upstream already has an equivalent or safer implementation.
- **DEFERRED** means an explicit TODO remains.
- **IMPLEMENTED** never implies runtime proof. Validation is recorded only where a
  focused test, build matrix, tool run, or retail runtime exercised the behavior; rows
  without item-specific evidence remain **VALIDATION-PENDING** even though the aggregate
  matrices pass.
- Current totals: **103 PORTED, 34 DROPPED, 0 wholly DEFERRED; 137 total**. Partial
  backend/mode deferrals are listed separately below.

## Recorded validation rollup

| Area | Evidence recorded on 2026-08-01 |
|---|---|
| Windows full | **1003/1003 build targets completed**; staged install completed successfully |
| Windows engine-only | **767/767 build targets completed** |
| Linux | **1001/1001 build targets completed** |
| Retail single-player | Stock menu plus Roadhouse, Feeding Tower, deathwalk/spirit, Shuttle, and `girlfriendx`/portal gameplay; save/load and config-casing migration; neutral-directory install auto-discovery |
| Modules/render selection | Unified module and legacy loader aliases reached gameplay; stale renderer ABI failed closed; explicit Vulkan requests now reach the Vulkan module on focused Roadhouse/Biolabsa smoke coverage |
| Multiplayer/input | Listen server plus two clients; network `_attackalt` reached the game through the merged `usercmd_t` layout |
| Filesystem layouts | Digital retail layout passed. A **synthetic** numbered-pak layout reached the settled menu; this is not evidence of genuine CD-era checksums |
| Quick fixture/tool runtime | Two fresh dmap runs produced byte-identical `.cm`/`.proc` output which loaded successfully; bounded light-grid bake completed; zero-probe `-quit` exited cleanly |
| Renderer foundation | Foundation self-tests pass. Focused Vulkan Roadhouse/Biolabsa gameplay smoke passes with zero Vulkan warning counters; this still does not establish exhaustive ModernGL/Vulkan Prey feature parity or complete manual visual parity |
| Package/validator closure | Windows x64 archive production, manifest/payload inspection, isolated Roadhouse runtime smoke, and engine screenshot passed; the root push validator passed every active check including staged-payload validation |
| Post-closure audit | All 137 catalog IDs reconcile exactly with this ledger. `openprey_rebase_contract.py` guards the corrected FW-19/FW-38 behavior, WP5.7 GUI stream/flag contracts, RD-22 structural replacement, removal of the old external-BSE workflow vocabulary, the autosave string overlay, loading-music decl-load scope, and benchmark post-map command path. The corrected Windows tree built **951/951** targets; its fresh GUI-marker save/load passed. A final staging/content audit added plain-install cleanup and guards preventing development-only `roadhouse_quick` fixture content from shipping in PK4s. A final Roadhouse clean-log launch wrote an engine screenshot and shut down with zero warnings/errors; the repaired renderer gameplay smoke profile passed. Follow-up audio/script validation fixed default OpenAL device selection, restored `waitForSilence` for `$player1`/all speaking entities, verified Roadhouse intro script timing, and passed clean Roadhouse/Biolabsa GL plus Vulkan smoke runs. Active CI is configured to build engine, dedicated, GL/Vulkan renderers, and the unified game module on Windows/Linux; hosted execution remains CI-pending. |

The retail runs above are representative, not an exhaustive per-map, localization,
subtitle/censor/audio, platform, or renderer-backend matrix. Item rows therefore retain
conservative validation states unless that evidence directly exercises them.

## Item ledger

| Catalog ID | Disposition | Implementation | Validation | Evidence / next action |
|---|---|---|---|---|
| `corelibs-01-prey-support-headers` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-02-bitmsg-prey-compat` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-03-alloc-template-defaults` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-04-64bit-portability-fixes` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Equivalent or safer behavior is already present in upstream `d41186e4`; the older fork delta was not replayed. |
| `corelibs-05-str-8bit-tables` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Equivalent or safer behavior is already present in upstream `d41186e4`; the older fork delta was not replayed. |
| `corelibs-06-platform-define-blocks` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Equivalent or safer behavior is already present in upstream `d41186e4`; the older fork delta was not replayed. |
| `corelibs-07-include-case-fixes` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Equivalent or safer behavior is already present in upstream `d41186e4`; the older fork delta was not replayed. |
| `corelibs-08-precompiled-prey-decl-includes` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-09-littlebitfield` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-10-interpolate-typeinfo-accessors` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-11-vec3-math-prey-helpers` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-12-langdict-profanity-filter` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-13-str-icmpnocolor` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-14-console-color-cyan` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Superseded by upstream theme support. |
| `corelibs-15-vsnprintf-seh-guard` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream string formatting implementation retained without the old MSVC-only guard. |
| `corelibs-16-cm-prey-clip-api-compat` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-17-cm-null-model-world-fallback` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-18-cm-modelinfo` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream collision diagnostics retained. |
| `corelibs-19-cm-humanhead-contents` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-20-cm-d3-proc-format` | PORTED | IMPLEMENTED | TOOL/RUNTIME-CHECKED | Two isolated `roadhouse_quick` dmap runs emitted byte-identical `.cm`/`.proc` output; the generated files loaded successfully. |
| `corelibs-21-cm-trm-bounds-fallback` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-22-cm-appendmap-stubs` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-23-aas-107-robustness` | PORTED | IMPLEMENTED | RUNTIME-CHECKED | Reworked `AASFile.cpp` for the Prey 1.07 six-field area writer layout and explicit CRC diagnostics; retail `.aas48` loading passed. |
| `corelibs-24-aas-idaaslocal-friend` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `corelibs-25-maya-exporter-signature` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-01` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-02` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-03` | PORTED | IMPLEMENTED | PARTIAL-RUNTIME-CHECKED | Digital retail assets passed. A synthetic numbered-pak layout reached the menu; genuine CD-era checksum capture remains TODO-PK4-CLASSIC. |
| `FW-04` | PORTED | IMPLEMENTED | RUNTIME-CHECKED | Neutral-directory launch auto-discovered the installed retail Prey asset root and reached the stock menu. |
| `FW-05` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-06` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-07` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-08` | PORTED | IMPLEMENTED | RUNTIME-CHECKED | Save-path config loading and legacy casing migration passed; validation logs use lowercase `logs/openprey_*.log` names. |
| `FW-09` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-10` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-11` | PORTED | IMPLEMENTED | RUNTIME-CHECKED | The unified module loaded through canonical and legacy aliases and reached representative SP and MP gameplay. |
| `FW-12` | PORTED | IMPLEMENTED | RUNTIME-CHECKED | Representative retail SP plus a listen server/two-client session exercised the Prey v7 call-site contract. |
| `FW-13` | DROPPED | NOT-APPLICABLE | STATIC/RUNTIME-CHECKED | The external closed-source companion workflow and its residual naming are absent. In-tree BSE is actively built, linked, attached, and initialized; nested `SEG_EFFECT` spawning remains TODO-BSE-NESTED-EFFECTS. |
| `FW-14` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-15` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-16` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-17` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-18` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream skin parsing/remapping already supplies the required safe behavior. |
| `FW-19` | PORTED | IMPLEMENTED | STATIC-CHECKED; RUNTIME-PENDING | Current `openPREY RDEMO` plus interim `OpenPREY RDEMO` and legacy `OpenPrey RDEMO` wrappers are accepted with compile-time equal-length guards; the active rebase-contract test rejects the stale OpenQ4/Quake4 set. |
| `FW-20` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-21` | PORTED | IMPLEMENTED | MP-RUNTIME-CHECKED | Two-client multiplayer delivered network `_attackalt` through the merged 16-bit `usercmd_t` layout. |
| `FW-22` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-23` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream loading-background composition supersedes the old engine-side expansion. |
| `FW-24` | PORTED | IMPLEMENTED | RUNTIME-CHECKED | SP save/load round-trip and config-casing migration passed under the live version-1834 stamped payload policy. |
| `FW-25` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-26` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-27` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-28` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-29` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-30` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-31` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-32` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-33` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | The superseded light-grid bake WIP remains preserved at `pre-oq4-rebase`. |
| `FW-34` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Agent launches enforce windowed mode; the engine does not globally override user fullscreen settings. |
| `FW-35` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream already provides English dictionary fallback. |
| `FW-36` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `FW-37` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream machine-spec logic was retained. |
| `FW-38` | PORTED | IMPLEMENTED | STATIC-CHECKED; RUNTIME-PENDING | Client/server defaults are 32000; internet presets and manual warning thresholds are 12000/16000/24000/32000 and use `LISTEN_SERVER_MAX_PLAYERS`; the active rebase-contract test guards the values. |
| `FW-39` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-01` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-02` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-03` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-04` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-05` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-06` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-07` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-08` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-09` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-10` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream post-processing/HDR implementation retained. |
| `RD-11` | DROPPED | NOT-APPLICABLE | TOOL/RUNTIME-CHECKED | Upstream light-grid implementation retained; a bounded quick-fixture bake completed and the zero-probe `-quit` path shut down cleanly. Old WIP remains preserved by tag. |
| `RD-12` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-13` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream image/DDS loader retained. |
| `RD-14` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-15` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-16` | PORTED | IMPLEMENTED | BUILD/RUNTIME-CHECKED | Renderer API is v9; all current modules build, and a deliberately stale module was rejected fail-closed at runtime. |
| `RD-17` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-18` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-19` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-20` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-21` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `RD-22` | DROPPED | NOT-APPLICABLE | STATIC-CHECKED | Upstream replaced the old pointer-as-surface-index trick with the integer `surfIndex` field, so the historical `uintptr_t` casts are obsolete; the active rebase-contract test guards that structure. |
| `RD-23` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `sound-01-subtitle-system` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `sound-02-profanity-censor` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `sound-03-db-volume-cvars` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `sound-04-focus-mute` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `sound-05-prey-shader-dialect` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `sound-06-prey-sound-api-shims` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `sound-07-iseax-available` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream OpenAL capability path retained. |
| `sound-08-openal-device-selection` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream OpenAL device-selection and initialization path retained. |
| `ui-01-retail-gui-def-dialect` | PORTED | IMPLEMENTED | STATIC-CHECKED; RUNTIME-PENDING | The retail dialect is present and the complete GUI window-bit set now has a compile-time no-overlap assertion; full retail GUI navigation remains pending. |
| `ui-02-retail-spline-text-effect` | PORTED | IMPLEMENTED | COMPILE-CHECKED; RUNTIME-PENDING | Bitmap-font spline/credit path is ported and its objects compile. `r_useTrueTypeFonts=0` is the default; TODO-D10 covers the TTF variant, which currently no-ops/warns. |
| `ui-03-gui-events-startup` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `ui-04-guiscript-inc-resetcapture` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `ui-05-slider-volume-thumb` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `ui-06-prey-font-cursor-assets` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `ui-07-background-expansion` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `ui-08-shear-winvar` | PORTED | IMPLEMENTED | STATIC/RUNTIME-CHECKED | The animatable shear winvar is serialized behind an explicit openPREY GUI magic/version marker, preventing the changed layout from being mistaken for another GUI stream. A fresh post-change Roadhouse save contained 54 `PGUI` v1 markers and restored through `Game Map Init SaveGame` without GUI stream errors. |
| `ui-09-text-shadow-choice-fixes` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `ui-10-ui-header-shims` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `ui-11-worldgui-aspect-bypass` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Superseded by current UI viewport/aspect call-site handling. |
| `ui-12-cursor-bounds` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream transform-aware cursor behavior retained. |
| `ui-13-renderwindow-viewport` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream viewport correction retained. |
| `multi-platform-meson` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream multi-platform Meson architecture retained. |
| `toolchain-warn-suppression` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `unified-game-module` | PORTED | IMPLEMENTED | RUNTIME-CHECKED | One staged `game_<arch>` module served representative SP and a listen server/two-client MP session; canonical and legacy aliases loaded it successfully. |
| `gamelib-sync-scripts` | PORTED | IMPLEMENTED | BUILD/PACKAGE-CHECKED | Windows and Linux builds consumed the isolated build-local GameLib stage; package provenance recorded the clean canonical companion commit and emitted one unified module. |
| `nightly-ci-overhaul` | PORTED | IMPLEMENTED | STATIC-CHECKED; CI-PENDING | Active YAML parses and builds engine, dedicated server, GL/Vulkan renderer modules, and one staged `game_<arch>` module on Windows/Linux; it also asserts `OpenPrey-game`, `basepr`, rebase source contracts, and openPREY icon/desktop payloads. Hosted CI has not run on this tree yet. |
| `package-nightly-prey` | PORTED | IMPLEMENTED | TOOL/PACKAGE/RUNTIME-CHECKED | Pak/release tooling passed; a Windows x64 archive was produced and inspected, then launched in isolation to Roadhouse and wrote an engine screenshot. |
| `openal-dll-staging` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream OpenAL staging retained. |
| `linux-desktop-icons` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `vscode-prey-launch` | PORTED | IMPLEMENTED | STATIC-CHECKED; RUNTIME-PENDING | Generator produced 35 per-map plus base/dedicated configurations; all client configs select `basepr`, `.home`, and `r_fullscreen 0`. Launches have not all been executed. |
| `rebrand-openprey` | PORTED | IMPLEMENTED | STATIC/RUNTIME/PACKAGE-CHECKED | Active runtime/tool branding validators pass; lowercase openPREY logs and the isolated packaged runtime were verified. Compatibility-only OpenQ4 identifiers remain documented. |
| `portability-64bit-fixes` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream portability work retained. |
| `cpu-easyargs-win64` | PORTED | IMPLEMENTED | RUNTIME-CHECKED | Scripted float+entity event dispatch passed on Win64 with `CPU_EASYARGS=0`. |
| `macos-native-bringup` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream macOS backend retained; Prey runtime signoff is deferred. |
| `focus-audio-muting` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream platform focus query retained; Prey sound wiring is tracked by `sound-04`/`FW-26`. |
| `dxgi-videoram` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream DXGI implementation retained. |
| `sdl3-gui-cursor-bounds` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream SDL3 cursor correction retained. |
| `console-color-tweak` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Superseded by theme-based branding. |
| `linux-native-fixes` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Upstream Linux fixes retained. |
| `lglcd-idsys-hooks` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `assets-01-prey-gui-suite` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `assets-02-postprocess-shader-mirror` | DROPPED | NOT-APPLICABLE | NOT-APPLICABLE | Local shader mirrors were superseded; current upstream shader assets were retained under `content/basepr/`. |
| `assets-03-interaction-vfp` | PORTED | IMPLEMENTED | VALIDATION-PENDING | Rebased implementation is present; final automated and retail-runtime matrix evidence has not yet been recorded. |
| `assets-04-menu-strings-999` | PORTED | IMPLEMENTED | STATIC/RUNTIME-CHECKED | Decision executed: retain `#str_122xxx`; four packs renamed to `<lang>_openprey.lang`, present in pak0 sources, with no `*999.lang` residual. The clean-log Roadhouse launch also proved the overlay carries the inherited `#str_107240` autosave label. Full GUI localization navigation remains broader than this rebase gate. |
| `assets-05-aspect-expand-backgrounds` | PORTED | IMPLEMENTED | TOOL/RUNTIME-CHECKED; PARTIAL | Tile extractor passed a synthetic 3×3 TGA smoke and the stock menu was captured through the engine screenshot command. Broader loading-screen presentation remains unrun. |
| `assets-06-prey-map-cm-fixtures` | PORTED | IMPLEMENTED | PARTIAL-TOOL/RUNTIME-CHECKED | Fresh quick-fixture `.cm`/`.proc` generation was deterministic and loaded. The complete `roadhouse_quick` fixture, including its script, is dev-only loose content guarded against runtime PK4 leakage. Committed retail mirror regeneration/comparison remains a separate pending check. |
| `assets-07-install-tree-artifacts` | DROPPED | NOT-APPLICABLE | TOOL-CHECKED | Generated `.install/` content was removed from version control, and plain Meson installs now scrub non-runtime import/build artifacts before staged-payload validation. |
| `assets-08-prey-branding-packaging-assets` | PORTED | IMPLEMENTED | STATIC/PACKAGE-CHECKED | Branding validators pass and the produced archive payload carries openPREY identities and documentation. |
| `assets-09-prey-docs-adaptation` | PORTED | IMPLEMENTED | STATIC-CHECKED | Rebase evidence is rehomed, active public docs are Prey-focused, and `docs_link_integrity.py` passes. |
| `assets-10-basepy-meson-unified-module` | PORTED | IMPLEMENTED | BUILD/PACKAGE/RUNTIME-CHECKED | All build matrices, staged install, archive inspection, and isolated retail runtime used `basepr` with one unified game module. |

## Explicit deferral register

| TODO | Scope | Required closure evidence |
|---|---|---|
| TODO-D9 | MVD, bots, and repeater integration against Prey API v7 remain compile-time gated off by `OPENPREY_ENABLE_MVD=0`, `OPENPREY_ENABLE_BOTS=0`, and `OPENPREY_ENABLE_REPEATER=0`. | Adapt their schema/call sites without extending the v7 ABI; add focused automated and MP runtime coverage. |
| TODO-D10 | TTF variant of `ui-02-retail-spline-text-effect`; bitmap-font implementation is ported. | Enable TTF only for a focused test, compare retail credits, adapt the glyph path if needed, then decide the default. |
| TODO-RENDER-BACKENDS | Prey feature parity for ModernGL and Vulkan; legacy GL/ARB2 is the first target. | Backend-specific material, interaction, portal, spirit, glow, and beam captures from the engine `screenshot` command. |
| TODO-BSE-NESTED-EFFECTS | Nested BSE `SEG_EFFECT` spawning is gated; the core BSE subsystem itself is active. | Add a Prey-v7-compatible callback/spawn path and validate stock nested FX with engine logs and screenshots. |
| TODO-SOUND-DSP | `SetSpiritWalkEffect` and `SetVoiceDucker` are compatibility no-ops. | Implement DSP behavior or document a permanent compatibility no-op with audio tests. |
| TODO-PK4-CLASSIC | Classic-layout retail PK4 checksum values are not yet independently captured. | Record hashes from a verified CD-era install and add checksum tests without weakening unknown-layout startup. |
| TODO-MIRROR-CLIP | Retail mirror clip-plane math parity. | Add the planned comparison fixture and renderer capture evidence. |
| TODO-RELEASE-LANES | Inherited ARM64/macOS/signing/manual-publish fixtures, plus the legacy Linux sanitizer and native-Wayland workflow jobs, are manual-only and false-gated because they still encode OpenQ4 lane identities or split-module assumptions. | Rebuild lanes around `OpenPrey-game`, `basepr`, one `game_<arch>` module, and openPREY payload/signing identities before removing the gates. Platform-independent sanitizer source invariants remain active separately. |
| TODO-UPSTREAM-PRS | Generic Doom 3/idTech 4 compatibility changes suitable for upstreaming. | Split title-neutral commits and submit them upstream after openPREY runtime validation. |

The previous “companion-repo migration” deferral is closed as an architectural decision:
`E:\Repositories\OpenPrey-game` is now canonical and openPREY stages it directly at
configure time. There is no in-repository `src/game` mirror.
