# Catalog: framework

> Historical pre-rebase audit snapshot. Paths and recommendations below describe the
> compared source trees; use [status-ledger.md](status-ledger.md) for current disposition,
> paths, implementation state, and validation TODOs.

## Upstream summary
Upstream OpenQ4 framework moved massively since the fork (66 files, ~27k insertions over ~575 commits). Structural changes that matter for reapplying openPREY work: Session.cpp nearly doubled and split off Session_demo.cpp plus async/MultiViewDemo.* (bots, MVD recording, advanced demo viewer, demo menu — d41186e4), and the savegame system was rewritten (saveType_t, hardened Session_Read/WriteSaveGameString header IO, multi-gamename acceptance via SAVEGAME_GAME_NAME_RETAIL/LEGACY_OPENQ4, entity-filter field, generated compat header — fbeff719/ab725f55). Game-module loading kept the split game_sp/game_mp scheme but gained phase-tracked diagnostics (GameModuleDiagnostics.h) and macOS universal2 fallback; the Q4 gameImport/gameExport contract is unchanged and now underpins the bot/MVD features, with gameImport.bse backed by in-tree BSE after the external libbse-q4 runtime was dropped (9c1989fd). The renderer boundary was hardened during the Vulkan phases: framework code no longer includes tr_local.h or reads glConfig (engineWindowState, public renderer interface, shared imagetools library), and both the loading-canvas expansion and the bakeLightGrids driver — which openPREY carries as local copies — now exist upstream in evolved, interface-clean form (.lightgridpack, imagetools). FileSystem grew pak/mod manifests, relaxed retail PK4 validation with game-binary-pk4 ignoring, case-resolving base-path probing (FS_TryResolveBasePathCandidate) and content-search diagnostics, while retaining Steam/GOG discovery and an unchanged FindMapScreenshot. DeclManager was reorganized (RegisterDeclFolderWrapper, allocator trampolines, exposed decl-file methods) and idDecl gained a Parse(text,len,noCaching) overload; DECL_FX/DECL_PARTICLE remain commented out. Console gained TTF fonts and themes; UsercmdGen gained controller/high-DPI/impulse work plus BUTTON_WEAPONWHEEL BIT(9); DemoFile independently implemented multi-magic acceptance; default net rates were raised to 25600; the English language-dict fallback and machine-spec modernization that openPREY made were independently landed upstream (58cd4571, 2709cf57/d495e61a).

## Changes

### [FW-01] openPREY branding and Prey install/identity constants
- category: rebrand | disposition: **reapply-clean** | effort: M | risk: low
- files: src/framework/licensee.h, src/framework/Common.cpp, src/framework/FileSystem.cpp, src/framework/Session.cpp, src/framework/async/AsyncServer.cpp, src/framework/Console.cpp
- desc: PROJECT_NAME/GAME_NAME/repo/icon -> openPREY; CD_BASEDIR 'openPREY', BASE_GAMEDIR 'base', BASE_MPGAMEDIR 'base', OPENPREY_GAMEDIR 'basepr'; GAME_PLAYERDEFNAME player_tommy defs; CDKEY_FILE 'preykey' + Human Head/2K CDKEY_TEXT; window-class names, default paths, log filename 'logs/openPREY_*', fs_game default, mod-list label, command rename openq4_startSingleplayer->openprey_startSingleplayer, SAVEGAME_VERSION 114, and OpenQ4->openPREY strings scattered through FileSystem/AsyncServer messages.
- reason: Mechanical rename against upstream licensee.h (checked openq4-upstream/main:src/framework/licensee.h). One real fixup: upstream now generates PROJECT_VERSION from openq4_version_generated.h (OPENQ4_VERSION_SHORT) and changed ENGINE_VERSION format, so openPREY's hardcoded "0.0.1" must move onto the generated-version scheme. String sites moved around in the much larger upstream files but are grep-portable.

### [FW-02] Prey compatibility feature-toggle macro block
- category: prey-feature | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/licensee.h
- desc: Adds Prey/HumanHead build toggles consumed by game/renderer code: SINGLE_MAP_BUILD, PARTICLE_BOUNDS, GUIS_IN_DEMOS, MUSICAL_LEVELLOADS, GAMEPORTAL_PVS/SOUND, DEATHWALK_AUTOLOAD, _HH_RENDERDEMO_HACKS, _HH_CLIP_FASTSECTORS, _HH_MYGAMES_SAVES, GOLD, GERMAN_VERSION, etc. Plus INTERIM/LEGACY_CONFIG_FILE casing constants.
- reason: Pure additions at the end of licensee.h; upstream did not touch that region (verified upstream licensee.h has no equivalent block). Ports verbatim.

### [FW-03] Prey retail PK4 checksum/validation policy (dual pack layouts)
- category: prey-feature | disposition: **reapply-rework** | effort: M | risk: medium
- files: src/framework/FileSystem.cpp
- desc: Replaces the Q4 officialPk4s table with Prey packs: classic CD/DVD pak000-004 (checksums not yet captured) and consolidated digital layout pak_data/pak_sound/pak_en_v/pak_en_t (with checksums). ValidateRequiredOfficialPaks now detects which of the two required sets is present and validates only that set, skipping zero (unknown) checksums; FatalError text reworded for Prey.
- reason: Upstream kept ValidateRequiredOfficialPaks but reworked policy around it: relaxed the required set (commit 67542718; pak023-025 now optional), added FS_IsIgnoredOfficialGameBinaryPk4 (d51aac02) and PrintContentSearchDiagnostics (case-mismatch Linux diagnostics), and added a pak/mod manifest system (3a1b576f, OPENQ4_MOD_MANIFEST_FILENAME, ReadModManifestFile). The Prey dual-set logic must be re-grafted onto that richer validation/diagnostic flow (FileSystem.cpp grew +3111 lines).

### [FW-04] fs_basepath auto-discovery via Windows registry CD-era Prey installs
- category: prey-feature | disposition: **reapply-rework** | effort: M | risk: medium
- files: src/framework/FileSystem.cpp
- desc: Removes Steam/GOG Quake4 discovery and adds FS_BuildRegistryInstallCandidates: scans HKCU/HKLM Human Head/2K Games/3D Realms Prey keys (+WOW6432Node), App Paths\prey.exe, and enumerates Uninstall entries whose DisplayName contains 'prey' (InstallLocation/DisplayIcon/UninstallString exe-dir extraction), plus FS_BuildKnownInstallCandidates for well-known Program Files/Games dirs. FS_HasGameFilesAtBasePath probes Prey pak names instead of Q4 pak001+game000.
- reason: Upstream still ships the Steam/GOG discovery this change deleted (FS_BuildSteamInstallCandidates line 942, FS_BuildGogInstallCandidates line 1062, FS_AutoDiscoverBasePath line 1127 of upstream FileSystem.cpp), and FS_HasGameFilesAtBasePath was rewritten to delegate to FS_TryResolveBasePathCandidate (case-insensitive path resolution for Linux). The registry scanner ports as a new candidate source, but it should be inserted alongside (not replacing) the resolver, and the Steam/GOG candidates should be retargeted to Prey's Steam/GOG folder names rather than deleted (Prey exists on both stores).

### [FW-05] Prey map loadscreen lookup in FindMapScreenshot
- category: prey-feature | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/FileSystem.cpp
- desc: FindMapScreenshot searches Prey locations (guis/assets/loading/%s.tga, guis/assets/loading/thumbs/%s.tga, then legacy gfx/guis/loadscreens/%s.tga) using both full map path and leaf-name candidates, special-cases game/roadhouse to keep openPREY-authored art, keeps the Q4 addon-extraction path, and falls back to guis/assets/loading/loading.tga.
- reason: Upstream FindMapScreenshot is byte-identical to the fork-era Q4 version (verified in upstream FileSystem.cpp around line 1319 impl); the replacement drops in unchanged.

### [FW-06] OpenExplicitFileAppend filesystem API
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/FileSystem.h, src/framework/FileSystem.cpp
- desc: New idFileSystem virtual OpenExplicitFileAppend(OSPath) (opens 'ab' with CreateOSPath), used by Prey-side logging/append consumers.
- reason: Upstream FileSystem.h still has only OpenExplicitFileRead/Write (lines 272-274); additive virtual + impl ports directly. Note it changes the idFileSystem vtable, so engine and game modules must be rebuilt together.

### [FW-07] Demote OSPathToRelativePath failure to DPrintf
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/FileSystem.cpp
- desc: Prey assets carry legacy absolute authoring paths; the per-file Warning is downgraded to a quiet DPrintf.
- reason: Upstream still emits common->Warning at that site; one-line change, and related upstream commit f2b72fdc (suppress override warnings) did not touch this path.

### [FW-08] Config load strictly from fs_savepath with legacy casings; write config on Quit
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/CmdSystem.cpp, src/framework/Common.cpp, src/framework/licensee.h
- desc: New exec_savepath console command (reads a cfg explicitly from fs_savepath and inserts it); InitGame probes CONFIG_FILE then INTERIM_CONFIG_FILE ('OpenPREYConfig.cfg') then LEGACY_CONFIG_FILE ('OpenPreyConfig.cfg') via openPREY_ExecConfigFromSavePath; idCommonLocal::Quit calls WriteConfiguration so late menu-driven cvar changes are persisted.
- reason: Checked upstream: Quit() still lacks the WriteConfiguration call, no exec_savepath exists, and the InitGame config-exec block survives in recognizable form. CmdSystem.cpp upstream changes are completion/AAPCS64 fixes (d8a2051e) that don't collide with adding a command.

### [FW-09] Engine-registered Prey GUI compatibility cvars
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/Common.cpp
- desc: Registers cvars retail Prey GUIs bind to: gui_filter_pb, g_subtitles, com_profanity, r_shaderlevel, r_correctspecular, r_normalizebumpmap, r_skipGlowOverlay, r_lowParticleDetail, r_useFastSkinning, image_anisotropy, s_musicvolume_dB, g_levelloadmusic, s_reverse.
- reason: Grep of upstream framework/renderer shows none of these names registered (image_anisotropy would coexist harmlessly if the renderer later defines it, since idCVar instances by name are unified). Pure additive block near the other Common.cpp cvars.

### [FW-10] Bind-tip key material API and input-compat virtuals on idCommon
- category: prey-feature | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/Common.h, src/framework/Common.cpp
- desc: MaterialKeyForBinding(binding, keyMaterial, key, isWide): maps the first key bound to a command onto Prey tip materials (textures/interface/tips/*) with wide-key heuristics and #str_07133 unbound text; plus default-implemented virtuals SetGameSensitivityFactor and FixupKeyTranslations for game-side Prey code. Used by the quickload prompt (FW-24) and game HUD tips.
- reason: Upstream Common.h still only has KeysFromBinding/BindingFromKey (line 338); the additions are self-contained and build on idKeyInput APIs that upstream retains. Watch only for upstream's TTF/localized key-name work (8632851a) when choosing localized names.

### [FW-11] Unified single 'game' module loading with legacy-name candidates
- category: prey-feature | disposition: **reapply-rework** | effort: M | risk: medium
- files: src/framework/Common.cpp, src/framework/async/AsyncNetwork.cpp, src/framework/Session.cpp
- desc: Prey ships one game module: module selection always resolves to 'game'; LoadGameDLL tries a candidate list (game_x64/gamex86/gamex64/game); AsyncNetwork spawnServer/connect/reconnect and Session StartNewGame/LoadGame/DevMap use Session_ModuleSupportsSingleplayer/Multiplayer helpers accepting 'game' plus legacy game_sp/game_mp, setting com_nextGameModule to 'game'.
- reason: Upstream still selects split game_sp/game_mp (openQ4_SelectGameModuleBaseName, Common.cpp:5273) but wrapped loading in a phase-tracked diagnostic framework (GameModuleDiagnostics.h, Com_SetGameModuleLoadPhase GAME_MODULE_PHASE_*, f6e0b868) with a macOS universal2 binary fallback (Common.cpp:5449-5461). The unified-module approach survives but the selection/candidate code must be rewritten inside those phases and the universal2 branch, and the reload-engine handoff points in AsyncNetwork/Session moved in the larger files.

### [FW-12] Prey (Prey-SDK) game API contract at engine call sites
- category: prey-feature | disposition: **conflict-major** | effort: L | risk: high
- files: src/framework/Common.cpp, src/framework/Session.cpp, src/framework/async/AsyncServer.cpp, src/framework/async/AsyncClient.cpp, src/framework/async/AsyncNetwork.cpp
- desc: Framework call sites switched from the Q4 gameExport contract to Prey's: gameImport.bse removed; game->SetUserInfo gains a 4th bool; InitFromSaveGame/InitFromNewMap take the idSoundWorld; SpawnPlayer(clientNum); RunFrame(cmds) (no serverGameFrame args); ClientPrediction without lastPredictFrame; ServerClientConnect(clientNum); ServerClientBegin(clientNum); ServerAllowClient(numClients, addr, guid, password, reason); ServerWriteSnapshot without lastSnapshotFrame and with byte[] clientInPVS; HandleGuiCommands replaces HandleMainMenuCommands.
- reason: Upstream keeps and extends the Q4 contract: AsyncServer.cpp:405/2985 call ServerClientBegin(clientNum, isBot, botName) for the new bot system, ServerAllowClient still takes clientId (line 1825), ServerWriteSnapshot still passes the extra arg (line 1273) and is now documented as destructive for MVD recording, gameImport.bse is still wired (Common.cpp:5511, backed by in-tree BSE). The new architecture is upstream's bots + MVD + advanced demo viewer (d41186e4) built on the extended signatures. Reapplying Prey's narrower contract requires either re-plumbing bot/MVD hooks through the Prey game API or shedding those upstream features for openPREY; it must be co-designed with the game-side Game.h/GameEdit changes.

### [FW-13] Make external BSE runtime optional and quiet
- category: compat-shim | disposition: **obsolete-upstream** | effort: S | risk: low
- files: src/framework/Common.cpp
- desc: libbse-q4 not found becomes a DPrintf (Prey uses Doom 3 FX/particle decls); warning texts note FX/particle decls remain available; LoadBSEDLL kept but soft-failing.
- reason: Upstream commit 9c1989fd 'Integrate BSE sources; drop external runtime' removed LoadBSEDLL/libbse entirely (grep of upstream Common.cpp finds no LoadBSEDLL/libbse); DECL_EFFECT now allocates via in-tree openQ4_AllocEffectDecl. There is no external DLL load left to soften. The only remaining decision is build-level: whether openPREY compiles the in-tree BSE at all.

### [FW-14] Doom 3 FX and particle decl support restored
- category: prey-feature | disposition: **reapply-rework** | effort: M | risk: medium
- files: src/framework/DeclFX.cpp, src/framework/DeclFX.h, src/framework/DeclParticle.cpp, src/framework/DeclParticle.h, src/framework/DeclManager.cpp, src/framework/declManager.h, src/framework/Common.h
- desc: Ports Doom 3 GPL idDeclFX and idDeclParticle (new files, ~2400 lines), re-enables DECL_FX/DECL_PARTICLE in the declType_t enum, registers types and listFX/listParticles/printFX/printParticle commands, adds EDITOR_PARTICLE flag alias and MemInfo animAssetsTotal.
- reason: Upstream still has DECL_FX/DECL_PARTICLE commented out (declManager.h:56-57, DeclManager.cpp:1373-1374) so re-enabling doesn't collide semantically, but three upstream changes force adaptation: idDecl::Parse now has a (text, textLength, noCaching) overload that every decl class overrides (declManager.h:116/243-244, commit 043833c8); registration moved to RegisterDeclFolderWrapper with allocator trampolines (DeclManager.cpp:1355-1400); and EDITOR_FX moved to BIT(10) with an EDITOR_PARTICLE alias already present in upstream Common.h (lines 53/72), making the Common.h edit obsolete.

### [FW-15] hhDeclBeam Prey beam declaration type
- category: prey-feature | disposition: **reapply-rework** | effort: M | risk: medium
- files: src/framework/DeclPreyBeam.cpp, src/framework/declPreyBeam.h, src/framework/DeclManager.cpp, src/framework/declManager.h
- desc: New hhDeclBeam decl (beam node/spline commands, per-beam shaders/quads, MAX_BEAMS 8) with DECL_BEAM enum value, 'beam' type + 'beams' folder registration, listBeams command, and inline FindBeam/BeamByIndex helpers on idDeclManager. Consumed by the Prey beam renderer (renderer subsystem) and game.
- reason: Same adaptation as FW-14: upstream's Parse(noCaching) overload and RegisterDeclFolderWrapper pattern (DeclManager.cpp:1391-1399). DECL_BEAM enum insertion also shifts declType_t values, which must stay coordinated with the game module's copy of the enum. No upstream equivalent exists.

### [FW-16] Prey energynode entityDef compatibility defaults
- category: prey-feature | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/DeclEntityDef.cpp
- desc: ApplyPreyEntityDefCompatibility in idDeclEntityDef::Parse: object_energynode* classnames get default spawnclass hhEnergyNode, model/bounds/matter/leechPoint keys, and per-variant (plasma/railgun/sunbeam/freeze) def_energy + sounds when inherit chains are incomplete, so leech-beam targeting doesn't fall back to idStaticEntity.
- reason: Upstream DeclEntityDef.cpp changed only modestly (+37 lines, mostly the Parse noCaching overload per commit 043833c8); the hook point (end of Parse before precache) still exists. Insert into the noCaching overload variant.

### [FW-17] DeclManager insideLevelLoad accessors
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/DeclManager.cpp, src/framework/declManager.h
- desc: New virtuals SetInsideLevelLoad(bool)/GetInsideLevelLoad() exposing the decl manager's insideLevelLoad flag to game code (Prey save/restore and media purge control).
- reason: Grep shows no equivalent upstream; insideLevelLoad member still exists in the (heavily grown, +2200 line) upstream DeclManagerLocal. Additive virtuals; changes idDeclManager vtable so modules must rebuild together.

### [FW-18] DeclSkin parse-result fix and GAME_DLL inline RemapShaderBySkin
- category: bugfix | disposition: **reapply-rework** | effort: S | risk: low
- files: src/framework/DeclSkin.cpp, src/framework/declSkin.h
- desc: idDeclSkin::Parse changed to return true (id's original code returns false, marking skins defaulted in some flows); RemapShaderBySkin given an inline GAME_DLL implementation so game-module code can remap without linking DeclSkin.cpp.
- reason: Half is superseded: upstream declSkin.h:49 made RemapShaderBySkin virtual, which solves cross-module dispatch without the inline hack — drop that part. The Parse return-value fix is NOT upstream (upstream DeclSkin.cpp still ends 'return false;') but upstream added DeclManager_ValidateParsedDecl (declManager.h:280) into the parse flow, so re-verify whether returning true is still the right fix there before porting.

### [FW-19] Render-demo magic backward compatibility for openPREY casings
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/DemoFile.cpp
- desc: OpenForReading accepts current 'openPREY RDEMO' plus interim 'OpenPREY RDEMO' and legacy 'OpenPrey RDEMO' magics, with static_asserts pinning equal lengths.
- reason: Upstream independently built the identical mechanism (DemoFile.cpp:36-38 DEMO_MAGIC_HISTORICAL 'OpenQ4 RDEMO' and DEMO_MAGIC_QUAKE4, multi-compare at lines 125-129 plus a legacyQuake4 flag). Porting = swapping the accepted spellings to the openPREY set; DemoFile.cpp otherwise changed for MVD support (d41186e4) without touching this area's shape.

### [FW-20] Cyan console accent and Prey bigchars charset
- category: cosmetic | disposition: **reapply-rework** | effort: S | risk: medium
- files: src/framework/Console.cpp
- desc: kConsoleBorderColor changed to #00ffff and console charset material changed from fonts/english/bigchars to Prey's textures/bigchars (also used by Common/Session loading text, see FW-22).
- reason: Upstream Console.cpp grew +3792 lines with TTF fonts and console themes (8632851a) and scaled diagnostics (18438c36); kConsoleBorderColor now feeds many accent paths (scrollbars, popups, cursors — upstream lines 4494-4616) and charset selection interacts with the TTF font system (charSetShader still at line 1338). Recolor should go through the new theme mechanism rather than the constant, and textures/bigchars must be validated against the TTF fallback path.

### [FW-21] Prey usercmd button layout and _attackalt binding
- category: prey-feature | disposition: **reapply-rework** | effort: S | risk: medium
- files: src/framework/UsercmdGen.h, src/framework/UsercmdGen.cpp
- desc: Adds BUTTON_ATTACK_ALT at BIT(3) (Prey alt-fire), shifting BUTTON_SCORES/BUTTON_MLOOK to BIT(4)/BIT(5) and collapsing Raven's INGAMESTATS/VOICECHAT/TOURNEY/STRAFE onto BUTTON_6/BUTTON_7 aliases; registers '_attackalt' -> UB_BUTTON3 in userCmdStrings; adds USERCMD_ONE_OVER_HZ.
- reason: Upstream UsercmdGen.h kept the Q4 layout and added BUTTON_WEAPONWHEEL = BIT(9), and UsercmdGen.cpp was reworked +590 lines for controllers, rumble, high-DPI mouse and impulse bindings (5445a872, 5f51f467, ba4a0e0a). The bit re-layout is a network/game wire-format decision that must be reconciled with BUTTON_WEAPONWHEEL and re-applied over the rewritten input code, in lockstep with the game module.

### [FW-22] Prey loading/splash screen pipeline
- category: prey-feature | disposition: **reapply-rework** | effort: M | risk: medium
- files: src/framework/Session.cpp, src/framework/Common.cpp
- desc: Loading GUI fallback chain led by guis/map/loading.gui (Session_FindFallbackLoadingGui); loading background defaults to Prey art via FindMapScreenshot, with mapDef lookups tolerating path/leaf variants (Session_FindMapDeclForLoading); Prey GUI state vars (image, friendlyname, loading_bkgnd_canvasfill, image_left/right/top/bottom edge states, showddainfo=false); PrintLoadingMessage splash uses resolved-material candidate chain (guis/assets/loading/loading etc.), Prey text color and textures/bigchars, aspect-aware textY; optional guiGameOver falls back to main menu; DeadZone gametype string and MP limit strings.
- reason: Upstream Session.cpp/Common.cpp evolved heavily: LoadLoadingGui gained localization fixes (58cd4571) and already consumes the canvas-expansion path (Session_PrepareExpandedLoadingBackground called at upstream Session.cpp:4627), and PrintLoadingMessage was rewritten onto engineWindowState instead of glConfig (Vulkan Phase B5a, 7f076176). The Prey material paths, GUI state names, and fallback GUI chain re-graft onto those reshaped functions; keep upstream's expansion machinery and only redirect its inputs.

### [FW-23] Aspect-correct loading-background canvas expansion (image compositing)
- category: prey-feature | disposition: **obsolete-upstream** | effort: S | risk: low
- files: src/framework/Session.cpp, src/framework/Common.cpp
- desc: Composites widescreen loading backgrounds by cropping/resampling optional _left/_right/_top/_bottom side images (or edge-replicating) around the 4:3 art and writing a generated TGA (guis/assets/generated/loadscreens/), using R_LoadImage/R_ResampleTexture/R_WriteTGA/R_StaticAlloc directly from Session.cpp (includes renderer/tr_local.h); Common.cpp draws matching edge slices behind the startup splash.
- reason: Upstream now contains this exact mechanism, refactored to be architecture-clean: Session_GetLoadingCanvasExpansion/Session_PrepareExpandedLoadingBackground live in upstream Session.cpp (lines 1643/1688) using the shared imagetools static library (../imagetools/ImageTools.h, carved in 04128897) instead of tr_local.h, with R_WriteTGA routed to fs_savepath. Framework access to renderer internals was deliberately removed upstream (73bccf7a/79df21e3), so the openPREY copy must be dropped in favor of upstream's; only the Prey-specific material naming rides along via FW-22.

### [FW-24] Prey save/quickload UX and savegame compatibility
- category: prey-feature | disposition: **conflict-major** | effort: L | risk: high
- files: src/framework/Session.cpp, src/framework/Session_local.h, src/framework/Session_menu.cpp
- desc: guiSave (guis/save.gui) toast messages with DrawSaveGui/Show/HideSaveGuiMessage; HandleQuickLoad double-press confirm prompt showing the bound key via MaterialKeyForBinding; savegames written with gamename 'Prey' and accepted for 'Prey' or GAME_NAME; version 114 with legacy 1 accepted; multi-gamedir savegame search (fs_game -> basepr -> base) via Session_OpenSaveGameReadHandle/Session_BuildSaveGameSearchDirs, GetSaveGameList aggregating across dirs with loadGameListGameDirs, per-dir details/delete/timestamp; LoadGame(saveName, preferredGameDir); restart-menu loadlastsave/mainmenu commands.
- reason: Upstream rewrote the savegame framework: SaveGame(saveName, saveType_t) with ST_* types, hardened header IO via Session_ReadSaveGameString/Session_WriteSaveGameString, a multi-name acceptance mechanism (SAVEGAME_GAME_NAME_RETAIL / SAVEGAME_GAME_NAME_LEGACY_OPENQ4, Session_IsSupportedSaveGameName at upstream Session.cpp:255-261/5719), an entity-filter header field, and a generated compat header (openq4_savegame_compat_generated.h) — commits fbeff719, ab725f55, ffa75b5f. GetSaveGameList in upstream Session_menu.cpp (line 1731) was also reworked. Prey's quickload prompt, 'Prey' gamename, version policy, and multi-gamedir search must be redesigned as extensions of that hardened framework (e.g., add 'Prey' to the supported-name set) rather than re-applied as written.

### [FW-25] Level-load and menu music with Prey sound-world routing
- category: prey-feature | disposition: **reapply-rework** | effort: M | risk: medium
- files: src/framework/Session.cpp, src/framework/Session_menu.cpp, src/framework/Common.cpp
- desc: MUSICAL_LEVELLOADS behavior: mapDef snd_loadmusic (with defaultMap fallback) played on menuSoundWorld through ExecuteMapChange, mixed via soundSystem->Render() in PacifierUpdate and Session_ServiceLoadingSound for startup +map; SetPlayingSoundWorld() rewritten to keep menu world during loads and game world once mapSpawned; menu music guisounds_menu_music started in StartMenu; stale-mute recovery in Frame(); menuSoundWorld emitters cleared at handoff; g_levelloadmusic cvar.
- reason: No upstream equivalent (no Session_GetMapLoadMusic/load music), but the surrounding code changed: upstream SetPlayingSoundWorld() is still the simple Q4 version with unfocus-silencing folded in, sound emitter lifecycle was fixed upstream (fc65d3f5), OpenAL restart-on-change landed (874e7d3b), and ExecuteMapChange grew substantially (loading asset queue, split-map campaign state 6b889e6f). The routing rewrite must be redone against upstream's current ExecuteMapChange/Frame/PacifierUpdate, coordinated with FW-26.

### [FW-26] Focus-based audio muting via sound-system mute state
- category: bugfix | disposition: **reapply-rework** | effort: S | risk: medium
- files: src/framework/Session.cpp, src/framework/Session_local.h
- desc: Replaces upstream's silence-by-NULL-sound-world approach with soundSystem->SetMuteForFocus(ShouldMuteForFocus()) so the playing world is preserved while unfocused (avoids losing/mis-restoring the active world); adds IsMutedExplicitly-based stale-mute recovery.
- reason: Upstream kept and extended the old mechanism (Session_ShouldSilenceAudioWhenUnfocused at upstream Session.cpp:2091 with SDL-aware Sys_SDL_IsGameWindowFocused). The openPREY approach depends on new idSoundSystem APIs (SetMuteForFocus/IsMutedExplicitly — sound subsystem) and must replace upstream's mechanism wholesale; port together with the sound-subsystem side.

### [FW-27] Subtitle GUI hooks in session
- category: prey-feature | disposition: **reapply-rework** | effort: S | risk: medium
- files: src/framework/Session.cpp, src/framework/Session_local.h
- desc: guiSubtitles (guis/subtitles.gui) with ShowSubtitle(idStrList)/HideSubtitle (3-line localized rolling display, timegroup-aware StateChanged), redrawn after game->Draw in all three Draw() branches; cleared on Shutdown before uiManager teardown; full-screen menu GUIs get an explicit black backdrop for Prey's alpha-heavy menu art.
- reason: No upstream subtitle support, but idSessionLocal::Draw was reshaped upstream (advanced demo viewer overlays d41186e4, 16:9 cinematic bars 2eb9f17a, FPS overlay modes 29b03314), so the redraw insertion points and the black-backdrop branch must be re-placed in the new Draw. The API additions to idSession (ShowSubtitle called from the sound system) are additive.

### [FW-28] CD key persistence to fs_savepath
- category: prey-feature | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/Session.cpp
- desc: Implements ReadCDKey/WriteCDKey (previously stubs): reads/writes fs_savepath/base/preykey via OpenExplicitFileRead/Write with CDKEY_TEXT, resetting auth state fields.
- reason: Upstream ReadCDKey/WriteCDKey are still empty stubs (upstream Session.cpp:7102/7117, cdkey_state = CDKEY_NA); the implementations drop in, dependent only on CDKEY_FILE from FW-01.

### [FW-29] Doom3-era wipe material remap and null-safe wipes
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/Session.cpp
- desc: StartWipe resolves gfx/wipes/fade -> wipeMaterial and gfx/wipes/fade_blend -> wipe2Material (Prey/D3 decl names) before capturing, aborting cleanly with ClearWipe when neither exists; DrawWipeModel guards NULL wipeMaterial.
- reason: Upstream StartWipe (Session.cpp:3302) still has the fork-era shape (FindMaterial after capture, no null guard); the remap helper and guards apply with trivial context fixup.

### [FW-30] Remove single-player loading-continue gate
- category: prey-feature | disposition: **reapply-rework** | effort: S | risk: low
- files: src/framework/Session.cpp
- desc: Deletes the 'press key to continue' gate after ExecuteMapChange (and its com_skipLoadingContinue cvar / Session_IsLoadingContinueKey), keeping only idKeyInput::ClearStates so stale input doesn't leak into gameplay; loading GUI gets showddainfo=false. Retail Prey enters the map immediately.
- reason: Upstream kept the gate and invested in it (input-event clearing 1372fe65, logLoadingContinueGate logging at upstream Session.cpp:5072-5084). Removal must be redone against the evolved block; a lower-conflict alternative is defaulting com_skipLoadingContinue to 1 for openPREY and keeping upstream's code.

### [FW-31] Prey main-menu features: campaign flags, map rescan, MP player models
- category: prey-feature | disposition: **reapply-rework** | effort: M | risk: medium
- files: src/framework/Session_menu.cpp, src/framework/Session.cpp, src/framework/Session_local.h
- desc: RescanMaps() rebuilds the MP map list through the guiMainMenu_MapList idListGUI (replacing upstream's state-var MAPScan/CommitStartServerMapSelection flow), Deathmatch default gametype; Prey player-model selection (modelscan/click_modelList reading model_mp*/mtr_modelPortrait* from player_tommy_mp def); main-menu state g_roadhouseCompleted/wicked/casino + startGame applying casino/wicked; browser_levelshot Prey thumb; SetGUI calls CallStartup; guiIntro-latch escape in Frame(); LISTEN_SERVER_MAX_PLAYERS from MAX_ASYNC_CLIENTS.
- reason: Upstream refactored the same menu region differently: CommitStartServerMapSelection retained (upstream Session_menu.cpp:1018) plus large settings/crosshair menu refactors (9bfd581b), localization handling (58cd4571) and controller navigation (178fd6fc) — Session_menu.cpp is +2588 lines. The Prey list-GUI rescan and model-scan port conceptually but every hook site must be relocated; game->HandleGuiCommands hookup belongs to FW-12.

### [FW-32] Prey audio-options menu OpenAL wiring
- category: prey-feature | disposition: **reapply-rework** | effort: M | risk: medium
- files: src/framework/Session_menu.cpp
- desc: Session_RefreshMainMenuAudioState publishes OpenAL device list (ALC_ENUMERATE_ALL_EXT), active device, EAX/EFX capability into Prey GUI states (device_name/openAL/eax/...); 'sound init/system/device/speakers/eax/driver' menu commands force s_useOpenAL, s_restart on device change, and refresh state; includes snd_local.h for ALC APIs.
- reason: Upstream built its own OpenAL menu/runtime handling since the fork: opt-in with restart-on-change (874e7d3b), device monitoring and HRTF (b648aa1f), reliability/diagnostics (f255b903), and its 'eax'/'speakers' menu cases were rewritten (upstream Session_menu.cpp:2709-2758). The Prey-GUI state publication is still needed but should sit on upstream's device-monitor/restart machinery instead of raw ALC calls in Session_menu.

### [FW-33] bakeLightGrids batch driver in Session
- category: prey-feature | disposition: **obsolete-upstream** | effort: S | risk: low
- files: src/framework/Session.cpp
- desc: Full bakeLightGrids console command: arg parsing (all/all-mp/force/quit/limit/bounce/size/blends/samples/grid/separateAreas), batch map loading (SP StartNewGame / MP spawnServer with cheat gating), completeness checks against existing .lightgrid + area*_lightgrid_amb.tga outputs, stale-output cleanup, R_BakeCurrentLightGrids invocation. Reaches into tr.primaryWorld/portalAreas via tr_local.h.
- reason: Upstream has the same command in a more evolved form: Session_BakeLightGrids_f at upstream Session.cpp:2771 registered at :6952, with world internals moved behind the renderer interface (79df21e3 — no tr_local.h in framework), .lightgridpack format (15ab3183), lightgrid texture handling (44908638) and visibility-aware sampling (c325780c). Take upstream's version wholesale; the openPREY copy (which predates the renderer-interface cleanup) should not be reapplied.

### [FW-34] Forced windowed runtime policy in Session::Init
- category: compat-shim | disposition: **drop** | effort: S | risk: low
- files: src/framework/Session.cpp
- desc: Unconditionally sets r_fullscreen 0, r_fullscreenDesktop 0, r_borderless 0 at session init 'for local development and validation runs'.
- reason: Development-time hack that stomps user settings every launch. Upstream moved Windows to borderless-windowed by default (341c2c39) and added display-aware resolution selection (11116db1), which addresses the underlying need properly.

### [FW-35] English fallback for non-English language dictionaries
- category: bugfix | disposition: **obsolete-upstream** | effort: S | risk: low
- files: src/framework/Common.cpp
- desc: InitLanguageDict loads the English .lang set first as a base when langName != english, so missing localized strings fall back to English instead of raw #str ids.
- reason: Upstream commit 58cd4571 'Fix non-English menu localization handling' contains the identical englishLangList fallback block (verified in upstream InitLanguageDict, which now also takes applyStartupSysLang/allowAutoLanguageSelect params). Nothing to port.

### [FW-36] Prey string-table ID remap and MSG_CDKEY layout fix
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/Common.cpp, src/framework/Session_menu.cpp, src/framework/async/AsyncClient.cpp
- desc: Q4 #str_104xxx / #str_107212 ids replaced with Prey/D3 #str_04xxx / #str_07212 equivalents in loading messages, MessageBox button labels, and update prompts; MSG_CDKEY case gains explicit visible_left/mid/right states.
- reason: Upstream still uses the Q4 ids (e.g. #str_104343 at upstream Common.cpp:4801, #str_104339 family in Session_menu MessageBox); the remap is mechanical grep-and-replace at relocated sites. Depends on Prey .lang content being present.

### [FW-37] Machine-spec threshold modernization
- category: compat-shim | disposition: **obsolete-upstream** | effort: S | risk: low
- files: src/framework/Common.cpp
- desc: SetMachineSpec raised to modern thresholds (Ultra: 2.5GHz/6GB VRAM/16GB RAM etc., dropped CPUID_AMD special cases); Com_ExecMachineSpec r_mode presets retargeted and low-memory fallbacks re-thresholded (videoRam<2048, sysRam<4096).
- reason: Upstream reworked machine classification itself: machine-quality/downsizing tiers with modern values (upstream Common.cpp:2878 uses sysRam>=16384 && vidRam>=6144), arm64 tier fixes (2709cf57, d495e61a), and generated-image caching for load perf (089331a6). Adopt upstream's classifier; only re-check that Prey's r_mode preset indices (which differ per r_mode table) are set appropriately in upstream's Com_ExecMachineSpec.

### [FW-38] Modernized network rate defaults and listen-server presets
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/async/AsyncNetwork.cpp, src/framework/Session_menu.cpp
- desc: net_serverMaxClientRate/net_clientMaxRate defaults raised to 32000; gui_configServerRate presets remapped (12000/16000/24000/32000) with maxclients capped at MAX_ASYNC_CLIENTS instead of literal 16; listen-warning limits rescaled.
- reason: Upstream already modernized part of this (defaults now 25600 at upstream AsyncNetwork.cpp:48-49; preset 4/5 treat modern connections at 25600) but kept the legacy 8000/9500/10500 low presets (upstream Session_menu.cpp:2559-2596). Remaining delta is a small numeric preference patch; trivially portable, or arguably droppable in favor of upstream values.

### [FW-39] Misc portability and header cleanups
- category: cosmetic | disposition: **reapply-clean** | effort: S | risk: low
- files: src/framework/Unzip.cpp, src/framework/Common.cpp, src/framework/Common.h, src/framework/BuildVersion.h, src/framework/UsercmdGen.h, src/framework/FileSystem.cpp
- desc: Removes C++17-illegal 'register' keywords in Unzip huft_build; INT_PTR -> intptr_t for gameDLL/bseDLL; <ctype.h>/<windows.h> includes in FileSystem; BuildVersion.h include guard + ID_VERSIONTAG default; MemInfo_t animAssetsTotal field; USERCMD_ONE_OVER_HZ constant.
- reason: Upstream already fixed intptr_t for gameDLL (upstream Common.cpp:746) and has no bseDLL member anymore (BSE in-tree), making that part obsolete; upstream Unzip.cpp still contains 5 'register' uses so that cleanup still applies; the remaining additions (guard, MemInfo field, constants) are additive and conflict-free.

## Notes
Working tree (not just commits) was diffed as instructed; the uncommitted framework work (~1076 lines, mostly Session.cpp bakeLightGrids backport, licensee.h feature toggles, exec_savepath, DemoFile magics) is included in the catalog. Cross-cutting facts for the rebase planner: (1) The single highest-risk item is FW-12 (Prey game API contract) — it must be planned jointly with the game subsystem, and it collides head-on with upstream's bots/MVD/demo-viewer work that extends the Q4 contract. (2) Upstream now forbids framework->renderer-internal coupling (tr_local.h/glConfig usage was removed from Session/Common in the Vulkan phases; engineWindowState and the imagetools library replace them) — any reapplied Session/Common code touching glConfig, tr.*, or R_* internals must be rewritten against the public renderer interface. (3) Several openPREY inventions were cross-pollinated into upstream by the same author and are now better there: loading-canvas expansion (FW-23), bakeLightGrids (FW-33), English lang fallback (FW-35), demo-magic multi-accept mechanism (FW-19), intptr_t fix — prefer upstream's versions. (4) All idDecl subclasses gained a Parse(text,len,noCaching) overload upstream; every new Prey decl class must implement it. (5) idFileSystem/idDeclManager/idCommon vtable additions (FW-06, FW-10, FW-17) require engine+game modules to be rebuilt together and coordinated with the game-side headers. (6) DECL_BEAM insertion shifts declType_t enum values shared with the game module.
