# Catalog: corelibs

## Upstream summary
Upstream OpenQ4 (abb9a688..d41186e4, 575 commits) evolved these paths as follows. idlib (~64 files, +3017/-711): a 64-bit/portability pass (f145a4af: uintptr_t heap fixes, register removal — absorbing most of openPREY's equivalent fixes), size_t hardening of the Heap API (ID_HEAP_MAX_SIZE, size_t Mem_Alloc), restoration of the corrupted 8-bit idStr case tables plus signedness-independent hashing (2038e639, aa477f29), a NEW color-escape system in idStr (variable-length escapes via ColorEscapeLength, icon escape codes, rainbow color indices, console themes — 8632851a) which breaks any code assuming 2-char color codes, LangDict CP1252/glyph-fold normalization (~200 lines), SSE2 SIMD additions (Simd_SSE2.*), BitMsg/idMsgQueue input validation, and the same Linux/macOS platform blocks openPREY added to precompiled.h (70cb09f5, 33bd277e). Extrapolate.h/Interpolate.h are untouched. cm (~1.7k lines changed): major load-pipeline rework — LexerFactory with binary lexer support (b37bf37f), binary .cm writing and PROC/AAS out-of-date tracking (6a08ab1a), per-map model lifetime/caching (40d49258, 89c58292, 9adb69a3), stable polygon feature IDs (aa77c793), devpath/whitelist parity (e667496d); interface additions ExtractCollisionModel/PreCacheModel/PurgeModels/CompoundTrmFromModel/ModelInfo(int) (41d02793 supersedes openPREY's ModelInfo), model-level DrawModel/ModelInfo; Contacts rewritten to delegate to Translation; Translation gained a NULL-model warn-once safe return but no world fallback; LoadProcBSP rewritten (takes CRC, still Q4-format-only — openPREY's D3 proc support must be re-worked in). aas: upstream now natively supports version 1.07 files (tactical-features gated on version), ai_allowOldAAS for CRC mismatches, and dummy-node tolerance — superseding openPREY's AAS compat edits. bse_api: DELETED by 9c1989fd 'Integrate BSE sources; drop external runtime' — replaced by src/bse/ containing full BSE effects sources (BSE_Effect/Manager/Particle/Segment etc.); BSEInterface.h/BSE_API.h now live in src/bse/, so all ../bse_api/ includes must be repointed. external: gained ~51k lines of Vulkan support headers (vulkan/, vma/, volk/) for the new Vulkan renderer backend — additive, no conflict with openPREY (which never touched external). tools: ~96 files, +681/-801 of radiant editor maintenance — openPREY never touched src/tools, so it rebases for free. New upstream sibling dirs src/imagetools and src/render_geo appeared (outside assigned paths).

## Changes

### [corelibs-01-prey-support-headers] Prey SDK idlib support headers (hhMath, hhHermiteInterpolate, hhStack, hhProfiler)
- category: prey-feature | disposition: **reapply-clean** | effort: S | risk: low
- files: src/idlib/math/prey_math.h, src/idlib/math/prey_math.cpp, src/idlib/math/prey_interpolate.h, src/idlib/containers/PreyStack.h, src/preyengine/profiler.h, src/idlib/Lib.h, src/idlib/precompiled.h
- desc: New files ported from the Prey SDK: hhMath (dB scaling, lerp helpers, BuildRotationMatrix, GetClosestPtOnBoundary), hhHermiteInterpolate and related Prey interpolators, hhStack list-based stack, and the hhProfiler ingame-profiler header in a new src/preyengine/ directory. Wired in via #includes in Lib.h and precompiled.h.
- reason: All are brand-new files with no upstream counterpart (verified none exist in openq4-upstream/main). Upstream Lib.h and precompiled.h changed moderately (new hashing comments, platform blocks) but the include lists where these hook in are intact; re-adding the includes is mechanical.

### [corelibs-02-bitmsg-prey-compat] idBitMsg/idBitMsgDelta/idMsgQueue Prey API compatibility helpers
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/idlib/BitMsg.h
- desc: Adds methods the Prey game code expects: idBitMsg::WriteBool/ReadBool, WriteVec3/ReadVec3, a WriteString(s,maxLength,make7Bit) overload, SetDebugEntType stub; idBitMsgDelta::Init overloads aliasing InitWriting/InitReading; idMsgQueue legacy wrappers Add(data,size), WriteToMsg/ReadFromMsg, GetDirect. All header-only inlines.
- reason: Checked openq4-upstream/main:src/idlib/BitMsg.h — idBitMsg still lacks all of these (WriteVec3/ReadVec3 exist only on idBitMsgDelta, the Raven abahr block, as at fork). Upstream BitMsg changes are confined to BitMsg.cpp hardening (idMsgQueue::ReadFrom size validation, WriteNetadr type guard) that does not collide with these header additions.

### [corelibs-03-alloc-template-defaults] Default memoryTag template argument on idBlockAlloc/idDynamicAlloc/idDynamicBlockAlloc
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/idlib/Heap.h
- desc: Adds '= MA_DEFAULT' defaults to the byte memoryTag template parameter of idBlockAlloc, idDynamicAlloc and idDynamicBlockAlloc so Prey SDK code that instantiates them with Doom 3-era two/three-argument forms compiles.
- reason: Upstream Heap.h was reworked (size_t migration, ID_HEAP_MAX_SIZE overflow hardening, offsetof-based element recovery — verified in diff abb9a688..openq4-upstream/main) but the template declarations still have no default memoryTag (line 390 of upstream Heap.h: 'template<class type, int blockSize, byte memoryTag>'). The 3-line default-arg addition ports directly; just verify against the new size_t member signatures.

### [corelibs-04-64bit-portability-fixes] 64-bit/portability fixes in Heap, MD5, Simd, containers, ScrubFileName
- category: bugfix | disposition: **obsolete-upstream** | effort: S | risk: low
- files: src/idlib/Heap.cpp, src/idlib/hashing/MD5.cpp, src/idlib/math/Simd.cpp, src/idlib/math/Simd_generic.cpp, src/idlib/Lib.cpp, src/idlib/containers/List.h, src/idlib/containers/StaticList.h, src/idlib/Str.cpp
- desc: uintptr_t pointer-stashing fixes in idHeap Allocate16/Free16/SmallAllocate/SmallFree/LargeAllocate, removal of the 'register' keyword, MD5_Final memset(ctx, sizeof(*ctx)), Simd.cpp TestDot ALIGN16 initializer fix, ~idList()/~idStaticList() destructor syntax, ScrubFileName signed-char index cast.
- reason: Upstream commit f145a4af ('Use uintptr_t, remove register, add CI arch') plus later hardening delivered all of these; verified in openq4-upstream/main: Heap.cpp uses uintptr_t at the same sites, MD5.cpp:252 has sizeof(*ctx), Simd.cpp:734-755 uses the deferred v3constant/v4constant init, List.h:67 has ~idList(void), StaticList.h has ~idStaticList(void), Str.cpp ScrubFileName casts to unsigned char. Drop the openPREY versions and take upstream's.

### [corelibs-05-str-8bit-tables] idStr 8-bit case tables converted to hex byte arrays
- category: bugfix | disposition: **obsolete-upstream** | effort: S | risk: low
- files: src/idlib/Str.cpp, src/idlib/Str.h
- desc: Replaces the encoding-corrupted upperCaseCharacter/lowerCaseCharacter char tables (raw high-byte literals were mangled by a UTF-8 re-encode) with 'const byte' tables written as 0x.. hex values, changing the declared type in Str.h.
- reason: Upstream fixed the same corruption differently: commit 2038e639 'Restore the 8-bit idlib tables lost to a UTF-8 re-encode' restored the raw single-byte literals as 'const char', and aa477f29 'Make idStr hashing and comparison independent of char signedness' added static_cast<byte> at every indexing/hashing site (verified in openq4-upstream/main:src/idlib/Str.cpp/Str.h). Upstream's ToLower/ToUpper/hash code now indexes these tables extensively with the char type, so keeping the openPREY byte-typed tables would force a rework of upstream code; adopt upstream's fix. (Optional follow-up: upstream's raw-literal file is still vulnerable to a future re-encode; the hex form could be offered upstream separately.)

### [corelibs-06-platform-define-blocks] Linux/macOS platform blocks in precompiled.h (AlignmentChecker guard, RESTRICT/TIME_THIS_SCOPE fallbacks)
- category: build | disposition: **obsolete-upstream** | effort: S | risk: low
- files: src/idlib/precompiled.h
- desc: Adds __linux__ and MACOS_X/__APPLE__ sections defining _OPENGL, _LITTLE_ENDIAN, _CASE_SENSITIVE_FILESYSTEM, NEWLINE etc., an ID_ALIGNMENTCHECKER_DEFINED include guard around the AlignmentChecker stub, and global RESTRICT/TIME_THIS_SCOPE fallback defines.
- reason: openq4-upstream/main:src/idlib/precompiled.h contains essentially identical blocks (verified: __linux__ at line 136, MACOS_X at 182, three ID_ALIGNMENTCHECKER_DEFINED guards, RESTRICT/TIME_THIS_SCOPE fallbacks at 233-238, plus a MACOS_X auto-define at line 52), from upstream commits 70cb09f5/33bd277e/ad8bd7d8. Vector.h also gained the AlignmentChecker guard upstream. Take upstream's.

### [corelibs-07-include-case-fixes] Case-sensitive filesystem include-path fixes (licensee.h, declManager.h, threads/)
- category: build | disposition: **obsolete-upstream** | effort: S | risk: low
- files: src/idlib/precompiled.h, src/idlib/Lib.h
- desc: Lowercases include paths for case-sensitive filesystems: framework/licensee.h, declManager.h, declTable.h, declSkin.h, declEntityDef.h, declAF.h, and threads/ under the _XENON block in Lib.h.
- reason: Upstream precompiled.h already uses the lowercase forms (verified: '../framework/licensee.h', '../framework/declManager.h' etc. in openq4-upstream/main). The only remnant is 'Threads/' inside Lib.h's #ifdef _XENON block, which is dead code on every supported platform — not worth carrying.

### [corelibs-08-precompiled-prey-decl-includes] Prey decl-type and profiler includes in precompiled.h
- category: prey-feature | disposition: **reapply-clean** | effort: S | risk: low
- files: src/idlib/precompiled.h
- desc: Adds #includes for DeclFX.h, DeclParticle.h (Doom 3-era decl types Prey uses) and declPreyBeam.h (Prey beam decl), plus ../preyengine/profiler.h exposed to both engine and game.
- reason: Upstream's decl include block is intact but evolved: it now also includes DeclPDA.h and the effects include moved from ../bse_api/BSEInterface.h to ../bse/BSEInterface.h (commit 9c1989fd). Re-adding the three Prey decl includes and the profiler include is mechanical; when reapplying, the openPREY tree's remaining bse_api include path must follow upstream to ../bse/ (framework files Common.cpp and DeclManager.cpp include ../bse_api/BSE_API.h too — coordinate with the framework subsystem).

### [corelibs-09-littlebitfield] LittleBitField endian helper (RevBitFieldSwap/NoSwap)
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/idlib/Lib.h, src/idlib/Lib.cpp
- desc: Adds LittleBitField(bp, elsize) with RevBitFieldSwap/RevBitFieldNoSwap implementations and Swap_Init wiring — the Doom 3/Prey-era bitfield endian conversion used by Prey save/serialization code (Quake 4 had dropped it).
- reason: git grep LittleBitField openq4-upstream/main -- src/idlib returns nothing; upstream Lib.cpp changed only ~17 lines (unrelated). The addition slots back into Swap_Init and the endian function tables unchanged.

### [corelibs-10-interpolate-typeinfo-accessors] idExtrapolate/idInterpolate typeinfo pointer accessors and friend idTypeInfoTools
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/idlib/math/Extrapolate.h, src/idlib/math/Interpolate.h
- desc: Adds Get*Ptr() accessors (GetStartTimePtr, GetDurationPtr, GetStartValuePtr, etc.), GetLinearTime(), and 'friend class idTypeInfoTools' to idExtrapolate, idInterpolate, and both AccelDecel interpolators, so Prey's GameTypeInfo/savegame introspection can reach private members.
- reason: Neither Extrapolate.h nor Interpolate.h appears in the upstream diff stat abb9a688..openq4-upstream/main — both files are byte-identical to the fork point, so the additions apply verbatim.

### [corelibs-11-vec3-math-prey-helpers] idVec3 Prey helpers (hhToMat3, DirectionMask, MASK_* defines) and FLOAT_IS_INVALID
- category: prey-feature | disposition: **reapply-clean** | effort: S | risk: low
- files: src/idlib/math/Vector.h, src/idlib/math/Vector.cpp, src/idlib/math/Math.h
- desc: Adds idVec3::hhToMat3 (Prey Z-up basis construction via NormalVectors), DirectionMask()/idVec3(int directionMask) constructor and the MASK_NEGX..MASK_POSZ direction-bitmask defines used by Prey vehicle/thruster code, plus FLOAT_IS_INVALID(x) in Math.h.
- reason: Upstream Vector.h changed only slightly (IsZero rewritten via idMath_FloatBits, uintptr_t alignment asserts, AlignmentChecker guard — verified in diff) and Math.h's rework (fixed 32-bit float-bit ops, dd7b1b35) does not define FLOAT_IS_INVALID (grep confirms absent). Additions are non-overlapping; FLOAT_IS_INVALID should be re-expressed with upstream's new idMath_FloatBits-style macros if they replaced FLOAT_IS_NAN/FLOAT_IS_INF internals, which is a one-line check.

### [corelibs-12-langdict-profanity-filter] idLangDict <PROFANITY> tag censor for Prey subtitles
- category: prey-feature | disposition: **reapply-clean** | effort: M | risk: low
- files: src/idlib/LangDict.cpp, src/idlib/LangDict.h
- desc: GetString() now scans values for <PROFANITY>...</PROFANITY> tags and, when the com_profanity cvar is 0, replaces the tagged spans with the #str_41028 replacement string, using four mutable rotating scratch idStrs. Part of the subtitle-system commit a876dee7 'Add subtitle system and profanity censor'.
- reason: Upstream LangDict.cpp gained ~200 lines of CP1252/glyph-fold handling (LANGDICT_CP1252_HIGH, LANGDICT_GLYPH_FOLD, applied at load/normalization time) but idLangDict::GetString still returns args[i].value directly at the same two hit sites (direct hash hit and the D3->Q4 legacy-id remap hit, verified in openq4-upstream/main), so the filter hooks reinsert mechanically. Minor caution: the duplicated filter block should ideally be factored into one helper during reapply, and it must run after upstream's CP1252 normalization (which is load-time, so no conflict).

### [corelibs-13-str-icmpnocolor] idStr::IcmpNoColor color-code-skipping compare
- category: prey-feature | disposition: **reapply-rework** | effort: S | risk: medium
- files: src/idlib/Str.cpp, src/idlib/Str.h
- desc: Adds static and member idStr::IcmpNoColor which compares strings case-insensitively while skipping 2-character ^-color escape sequences (via idStr::IsColor, advancing s += 2).
- reason: Upstream commit 8632851a introduced a new color-escape system: Str.h now has ColorEscapeLength(s, outColor, resetToDefault), RegisterIconEscapeCode, rainbow color indices (COLOR_INDEX_RAINBOW_A..Z, COLOR_INDEX_COUNT), meaning color escapes are no longer uniformly 2 characters. IcmpNoColor's hardcoded 's += 2' must be rewritten to advance by ColorEscapeLength(s); otherwise it will mis-compare strings containing the new extended escapes. Small function, but the skip logic needs adapting to the new API.

### [corelibs-14-console-color-cyan] S_COLOR_CONSOLE changed to cyan
- category: cosmetic | disposition: **reapply-clean** | effort: S | risk: low
- files: src/idlib/Str.cpp
- desc: Changes the S_COLOR_CONSOLE entry in g_color_table from Raven orange (0.94, 0.62, 0.05) to cyan (0, 1, 1). Commit 4407cab0 'Change console color to cyan'.
- reason: Upstream g_color_table was restructured to COLOR_INDEX_COUNT entries (rainbow gradient appended) but the S_COLOR_CONSOLE slot still exists with the orange value (openq4-upstream/main:src/idlib/Str.cpp:55), so the one-value tweak reapplies trivially. Note upstream commit 8632851a also added 'console themes'; if that mechanism makes console colors user-configurable, this hardcoded tweak may be better expressed as an OpenPrey default theme instead.

### [corelibs-15-vsnprintf-seh-guard] SEH guard around _vsnprintf in idStr::vsnPrintf
- category: bugfix | disposition: **reapply-clean** | effort: S | risk: low
- files: src/idlib/Str.cpp
- desc: Wraps the Win32 _vsnprintf call in __try/__except so a crashing format/argument mismatch produces an empty string and -1 instead of taking down the process.
- reason: Upstream idStr::vsnPrintf (verified in openq4-upstream/main:src/idlib/Str.cpp) is unchanged apart from context — no SEH guard present. Reapplies directly; MSVC-only construct already properly #ifdef _WIN32 scoped.

### [corelibs-16-cm-prey-clip-api-compat] cmHandle_t typedef and Doom 3/Prey-era idCollisionModelManager wrapper API
- category: compat-shim | disposition: **reapply-rework** | effort: M | risk: medium
- files: src/cm/CollisionModel.h
- desc: Adds 'typedef idCollisionModel* cmHandle_t' plus non-virtual legacy wrappers Prey game code calls: FreeMap(void), LoadModel(modelName, precache), SetupTrmModel(trm, material), TrmFromModel(modelName, trm), GetModelName/GetModelBounds/GetModelContents/GetModelVertex/GetModelEdge/GetModelPolygon inspection helpers, DrawModel without explicit viewAxis, and a new pure-virtual ContentsName(contents).
- reason: Upstream restructured the interface (commits 40d49258, 41d02793, 9adb69a3): idCollisionModel itself gained ModelInfo(void) and a different DrawModel(modelOrigin, modelAxis, viewOrigin, radius) member; the manager gained ExtractCollisionModel, PreCacheModel, PurgeModels, CompoundTrmFromModel, ModelInfo(int), and LoadModel now defaults precache=false; DebugOutput signature changed to (viewOrigin, viewAxis). The manager-level six-argument DrawModel virtual still exists, and no cmHandle_t exists upstream (git grep confirms). The wrapper set must be re-derived against the current virtuals — most wrappers port with small signature fixups, the model-inspection helpers can now delegate to the richer idCollisionModel interface, and ContentsName can wrap the still-present internal StringFromContents (CollisionModel_local.h:591).

### [corelibs-17-cm-null-model-world-fallback] NULL model -> world model fallback in cm trace entry points
- category: compat-shim | disposition: **reapply-rework** | effort: M | risk: medium
- files: src/cm/CollisionModel_translate.cpp, src/cm/CollisionModel_rotate.cpp, src/cm/CollisionModel_contents.cpp, src/cm/CollisionModel_contacts.cpp
- desc: Prey/Doom 3 game code passes model==NULL to mean 'the world'. Replaces the fork's FatalError with 'model = models[0]' world fallback plus a warn-once safe-return (fraction=1, CONTACT_NONE) when no world model exists, across Translation, Rotation, Contents, PointContents, ContentsTrm, and Contacts.
- reason: Upstream diverged per-function: Translation already has the warn-once safe-return but no models[0] fallback (openq4-upstream/main:src/cm/CollisionModel_translate.cpp:790); Contacts no longer checks NULL at all (rewritten by 41d02793 to delegate to Translation, and now uses only the translational contact direction); Contents/PointContents/ContentsTrm still FatalError (CollisionModel_contents.cpp:431/496/649). The models[0] fallback must be reinserted into each current guard individually, and it should be confirmed that models[0] is still guaranteed to be the world model under upstream's reworked per-map model lifetime (commits 40d49258, 9adb69a3).

### [corelibs-18-cm-modelinfo] idCollisionModelManager::ModelInfo(int) debug command support
- category: compat-shim | disposition: **obsolete-upstream** | effort: S | risk: low
- files: src/cm/CollisionModel.h, src/cm/CollisionModel_local.h, src/cm/CollisionModel_load.cpp
- desc: Adds a ModelInfo(int modelIndex) virtual printing per-model or accumulated collision statistics (negative index = all models), for Prey's collision debug commands.
- reason: Upstream commit 41d02793 'Add CM ModelInfo output and fix Contacts' added the equivalent: idCollisionModelManager::ModelInfo(int num) with -1 for accumulated info (openq4-upstream/main:src/cm/CollisionModel.h:186, CollisionModel_local.h:402) plus a per-model idCollisionModel::ModelInfo(void). Use upstream's.

### [corelibs-19-cm-humanhead-contents] HUMANHEAD contents-flag names (forcefield, spiritbridge, hunterclip, ...)
- category: prey-feature | disposition: **reapply-clean** | effort: S | risk: low
- files: src/cm/CollisionModel_debug.cpp
- desc: Under #ifdef HUMANHEAD, replaces indices 17-28 of cm_contentsNameByIndex/cm_contentsFlagByIndex with Prey's contents set (forcefield, spiritbridge, areaportal, nocsg, block_radiusdamage, shootable, deathvolume, vehicleclip, owner_to_owner, game_portal, shootablebyarrow, hunterclip) and adds underscore-less alias parsing in ContentsFromString for retail .cm data.
- reason: Upstream CollisionModel_debug.cpp grew ~181 lines (debug failure visuals, f06faf33) but the stock Q4 contents tables are unchanged (sightclip still at index 17, verified). The #ifdef HUMANHEAD table swap and ContentsFromString aliases reapply directly; only surrounding-context fixup needed. Depends on the CONTENTS_* flag definitions living in game headers (other subsystem).

### [corelibs-20-cm-d3-proc-format] LoadProcBSP accepts Doom 3 'mapProcFile003' proc files
- category: prey-feature | disposition: **reapply-rework** | effort: M | risk: medium
- files: src/cm/CollisionModel_load.cpp
- desc: Prey ships Doom 3-format .proc files (ID token 'mapProcFile003', no separate version/CRC tokens). Reworks LoadProcBSP to detect either the Q4 PROC_FILE_ID (then read version+CRC) or the D3 ID (skip them), instead of rejecting non-Q4 procs.
- reason: Upstream rewrote LoadProcBSP substantially (verified openq4-upstream/main:src/cm/CollisionModel_load.cpp:280): it now takes (name, mapFileCRC), returns bool, uses LexerFactory::MakeLexer (binary-lexer support, b37bf37f), validates the CRC against the map and marks proc-out-of-date state (6a08ab1a), and feeds CheckProcModelSurfClip/StoreProcClipModel with an isLegacyWorldFile flag. Still hard-requires PROC_FILE_ID and unconditionally reads version+CRC tokens, so D3 procs are rejected. The dual-format branch must be rebuilt inside the new function: accept 'mapProcFile003', skip the version/CRC reads for it, decide what out-of-date semantics mean for CRC-less D3 procs, and verify the binary-lexer path tolerates D3 files.

### [corelibs-21-cm-trm-bounds-fallback] TrmFromModel bounds-box fallback for over-complex collision models
- category: prey-feature | disposition: **reapply-clean** | effort: S | risk: low
- files: src/cm/CollisionModel_load.cpp
- desc: When a collision model exceeds MAX_TRACEMODEL_VERTS/EDGES/POLYS or has dangling edges, TrmFromModel now warns and returns an idTraceModel built from the model bounds (expanded 1 unit if degenerate) instead of failing — retail Prey content relies on this leniency.
- reason: Upstream's idCollisionModelManagerLocal::TrmFromModel(const idCollisionModelLocal*, idTraceModel&) still returns false at the same four failure sites (verified; messages now include counts). The fallbackToBounds lambda reinserts directly. Note upstream added CompoundTrmFromModel (one trm per source primitive, CollisionModel.h:161) which is a higher-fidelity alternative for complex models — worth considering as the preferred path for some Prey callers, but the bounds fallback remains the simple safety net.

### [corelibs-22-cm-appendmap-stubs] AppendMap / WillUseAlreadyLoadedCollisionMap interface stubs for Prey deathwalk maps
- category: prey-feature | disposition: **reapply-clean** | effort: S | risk: low
- files: src/cm/CollisionModel.h
- desc: Adds two default-no-op virtuals to idCollisionModelManager: AppendMap(mapFile) and WillUseAlreadyLoadedCollisionMap(mapFile) (returns false), called from src/game/Game_local.cpp for Prey's deathwalk additional-map loading. Currently stubs — collision for the appended map is not actually merged.
- reason: No upstream equivalent exists (grep confirms). As inline default-bodied virtuals they re-add to the current interface trivially. However, a real implementation must eventually target upstream's reworked load pipeline (per-map model tables, binary CM cache, LexerFactory — commits 40d49258/6a08ab1a/9adb69a3), which upstream's mapName-keyed manager actually makes easier than the fork-era code.

### [corelibs-23-aas-107-robustness] AAS 1.07 acceptance, CRC warning demotion, node-count guards
- category: compat-shim | disposition: **obsolete-upstream** | effort: S | risk: low
- files: src/aas/AASFile.cpp, src/aas/AASFile_sample.cpp
- desc: Accepts Prey/D3-era AAS version 1.07 under #ifdef HUMANHEAD, demotes the out-of-date CRC warning to DPrintf, rejects files whose node section holds only the dummy sentinel (numNodes <= 1), and guards MaxTreeDepth/MaxTreeDepth_r against out-of-range node indices.
- reason: Upstream now handles all of this, mostly better (verified in openq4-upstream/main:src/aas/AASFile.cpp): Load() accepts any version with a warning and stores it, ParseAreas keys tactical-feature parsing off version 1.07 (line 1006: hasTacticalFeatures = version.Icmp("1.07") != 0), CRC mismatch sets console out-of-date state and honors an ai_allowOldAAS cvar, ParseNodes marks numNodes==0 as dummy (isDummy[AAS_DUMMY_NODES]) instead of hard-failing, and AASFile_sample.cpp contains the byte-identical MaxTreeDepth guards. Verify Prey 1.07 files load through upstream's path (they should, since 1.07 is explicitly modeled) and drop the openPREY variants.

### [corelibs-24-aas-idaaslocal-friend] friend class idAASLocal on idAASFile
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/aas/AASFile.h
- desc: One-line 'friend class idAASLocal' on the idAASFile interface so the Prey game-side AAS implementation can access file internals.
- reason: Upstream AASFile.h grew ~32 lines (version member, dummy-index plumbing) but idAASFile has no idAASLocal friend (its only friend is idAASFileLocal on a different class). The one-liner reapplies verbatim.

### [corelibs-25-maya-exporter-signature] Revert exporterInterface_t to two-argument D3/Prey signature
- category: compat-shim | disposition: **reapply-clean** | effort: S | risk: low
- files: src/MayaImport/maya_main.h
- desc: Replaces Raven's three-argument exporterInterface_t (src_ospath, dst_ospath, commandline) with the Doom 3/Prey SDK two-argument form (ospath, commandline) that Prey's model-export tooling expects.
- reason: Upstream maya_main.h is unchanged since fork (still the Raven 3-arg typedef); upstream's only MayaImport change is long->int printf-type fixes in maya_main.cpp (verified in diff abb9a688..openq4-upstream/main). The typedef swap reapplies verbatim; the caller side lives in framework/tools code owned by other subsystems.

## Notes
Working-tree check: git status --porcelain shows no uncommitted changes inside the assigned paths, so HEAD == working tree here; everything cataloged is committed. openPREY made ZERO changes in src/tools, src/external, and src/bse_api — nothing to reapply there. Cross-pollination alert: several openPREY fixes (Heap uintptr_t, MaxTreeDepth guards, Simd TestDot init, platform blocks in precompiled.h) exist byte-identically upstream, suggesting the same author upstreams fixes; the rebase should prefer the upstream copy wherever the two are equivalent to minimize diff. Two coordination points with other subsystems: (1) the bse_api->bse move requires repointing includes in src/framework/Common.cpp, src/framework/DeclManager.cpp and src/idlib/precompiled.h (line 376), and the framework's BSE DLL-loading code (bseImport/bseExport in Common.cpp:3246) is likely obsolete against upstream's in-tree BSE — Prey does not use BSE effects, so the Prey build may simply want upstream's in-tree BSE compiled as-is or stubbed; (2) the Prey CONTENTS_* flags referenced by the cm HUMANHEAD tables are defined in game headers owned by the game subsystem. New upstream directories src/imagetools and src/render_geo exist outside my assigned paths but will need meson wiring during the rebase.

