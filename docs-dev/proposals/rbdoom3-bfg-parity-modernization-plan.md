# RBDOOM-3-BFG Parity Modernization Plan for OpenQ4

Date: 2026-02-09  
Author: Codex (analysis + implementation plan)

## 1. Goal

Bring OpenQ4 to the same technical standard as modern RBDOOM-3-BFG, with special focus on lighting and shadowing, while preserving OpenQ4 rules:

- stock Quake 4 asset compatibility first
- no reliance on shipping custom `q4base/` content
- unified `openbase/` runtime layout
- Meson + SDL3 cross platform direction

## 2. Baseline and Evidence

### OpenQ4 / Quake4Doom baseline state

- OpenQ4 renderer is still the legacy OpenGL/stencil-shadow path (`src/renderer/OpenGL`, `src/renderer/tr_stencilshadow.cpp`, `src/renderer/draw_common.cpp`).
- OpenQ4 has no modern RBDOOM renderer modules:
  - missing `src/renderer/NVRHI`
  - missing `src/renderer/Passes`
  - missing `src/renderer/RenderWorld_lightgrid.cpp`
  - missing `src/renderer/RenderWorld_envprobes.cpp`
- OpenQ4 has basic post AA hook (`r_postAA`, SMAA material path) but no TAA/SSAO/SSR pipeline.
- Quake4Doom snapshot timestamps cluster around 2021-09, and source fingerprints align with pre-modern renderer architecture.

### Upstream RBDOOM evolution landmarks

- `v1.3.0` (2021-10-30): PBR GGX, baked GI (light grids + env probes), HDR/filmic integration, stronger shadow mapping path.
- `v1.4.0` (2022-03-06): stability and tooling passes.
- `v1.5.0` (2023-04-29): OpenGL replaced by DX12/Vulkan through NVRHI, major renderer rewrite, Donut SSAO, TAA default, shadow atlas pipeline.
- `v1.5.1` (2023-05-23): hotfix.
- `v1.6.0` (2025-05-10): masked occlusion culling, SSR refinements, improved light editor, GI/lightdata workflow upgrades, filesystem/tooling improvements.
- `v1.6.0..master` (2025-05 to 2026-02): ongoing Vulkan/DX12 stability, push constants, TAA/SSAO/tonemap fixes, macOS/driver compatibility.

### Churn indicator

Upstream commit counts touching renderer/shader stack:

- `v1.3.0..master`: `neo/renderer` 655 commits, `neo/shaders` 149 commits
- `v1.4.0..v1.5.0`: `neo/renderer` 344 commits, `neo/shaders` 61 commits
- `v1.5.1..v1.6.0`: `neo/renderer` 215 commits, `neo/shaders` 60 commits
- `v1.6.0..master`: `neo/renderer` 63 commits, `neo/shaders` 27 commits

This confirms the parity gap is mostly renderer architecture and lighting/shadow pipeline depth.

## 3. Major Enhancements Since the Fork Window

## 3.1 Renderer platform architecture

- Migration from legacy OpenGL renderer to NVRHI-backed DX12/Vulkan architecture.
- HLSL-first shader workflow with precompiled shader outputs.
- Push-constant and descriptor-layout evolution for modern GPU APIs.

## 3.2 Lighting and shadowing (highest impact)

- Shadow mapping pipeline became primary path:
  - atlas-based shadow allocation
  - point, spot, and parallel/sun lights
  - cascaded shadow maps for sun/parallel lights
  - quality tuning and acne/peter-panning mitigation controls
  - LOD logic for distant/small lights (skip/blend behavior)
- PBR-forward direct lighting:
  - GGX Cook-Torrance shading path
  - roughness/metalness/AO support (`rmaomap`)
  - compatibility fallback from legacy materials
- Indirect lighting stack:
  - environment probes (auto placement + manual entities)
  - per-area irradiance light grids with SH data
  - bake commands and cached file formats
- Screen-space and post-lighting:
  - improved SSAO (Donut-based path)
  - TAA integration and later SMAA/TAA coexistence fixes
  - HiZ support and SSR integration
  - modern tonemapping and filmic path improvements

## 3.3 Performance + visibility systems

- Masked software occlusion culling (MOC) integrated with renderer front-end culling.
- More robust GPU profiling/stat instrumentation.

## 3.4 Tooling and workflow upgrades

- Improved in-game light editor workflow.
- Better map conversion and standalone compile workflows.
- GI and light data cache workflows (including HDR-to-bimage caching path).

## 4. OpenQ4 Gap Matrix (Prioritized)

Priority P0 (critical to "same standard"):

- Modern shadow mapping system (atlas + CSM + point/spot support)
- PBR interaction path with legacy compatibility
- GI stack (env probes + light grids + bake/runtime load)
- SSAO + TAA + tonemap integration into the frame graph

Priority P1 (needed for production quality/performance):

- Renderer API modernization strategy (RHI layer and backend split)
- HiZ + SSR
- Masked occlusion culling
- GPU timing/diagnostic instrumentation parity

Priority P2 (important, but after lighting core):

- Full light editor modernization
- Expanded mapping/baking tooling
- Additional visual modes and non-core rendering extras

## 5. Recommended Delivery Strategy

Use a dual-track strategy:

- Track A: deliver lighting/shadow parity on current OpenGL renderer first (shortest path to visible Quake 4 gains).
- Track B: in parallel, build a backend abstraction seam so Vulkan/DX12 (or Vulkan-first) can land without redoing gameplay integration twice.

Reason:

- A direct big-bang backend swap is high risk for Quake 4 behavior parity.
- Lighting/shadow quality problems can be solved earlier with targeted renderer evolution.

## 6. Implementation Plan

## Phase 0: Fork-Delta Capture and Render Test Harness (2-3 weeks)

Deliverables:

- Frozen parity target list from RBDOOM features and cvars.
- Golden-scene capture suite for Quake 4 maps (same camera paths, fixed cvars, deterministic screenshots).
- Performance telemetry baseline for OpenQ4.

Tasks:

- Add automated capture command sets for representative SP/MP maps.
- Add frame timing buckets for shadow pass, ambient pass, post stack.
- Define pass/fail thresholds per scene.

Exit criteria:

- Repeatable visual/perf baselines committed in docs and test scripts.

## Phase 1: Shadow Mapping Core (6-10 weeks)

Deliverables:

- Shadow map rendering path in OpenQ4, initially toggleable beside stencil shadows.
- Atlas allocator with debug visualization and stats.
- Point + spot light shadow maps.

Tasks:

- Introduce shadow map textures and per-light shadow metadata.
- Implement shadow caster gather rules preserving existing Quake 4 material semantics.
- Add cvars for atlas size, map resolution, bias, sample count.
- Implement PCF/Vogel-like sampling path.

Exit criteria:

- Stencil and shadow-map paths switchable.
- Shadow acne and peter-panning within acceptable tuning range across golden scenes.

## Phase 2: Parallel/Sun Lights and Cascades (4-6 weeks)

Deliverables:

- Parallel light shadow support with cascaded splits.
- Stable split selection and transition blending.

Tasks:

- Define parallel-light representation compatible with Quake 4 light entities.
- Add split logic and camera-dependent matrix generation.
- Add debug overlays for cascade partitions and aliasing hotspots.

Exit criteria:

- Outdoor maps and long-view scenes show stable sun shadows without severe pumping.

## Phase 3: PBR Interaction Path with Compatibility Fallback (6-8 weeks)

Deliverables:

- PBR shading path (GGX-based) integrated into light interactions.
- Legacy material fallback path with conservative defaults.

Tasks:

- Extend material parsing for `basecolormap`/`rmaomap` style keywords while preserving legacy Doom3/Q4 keywords.
- Implement roughness/metalness/AO usage in direct + indirect terms.
- Add feature gates so stock assets maintain expected look by default.

Exit criteria:

- No regressions on stock Quake 4 assets.
- Optional PBR overrides function through `openbase` staged content without requiring repo `q4base`.

## Phase 4: Indirect Lighting Stack (Env Probes + Light Grids) (8-12 weeks)

Deliverables:

- Environment probe system with fallback data when no baked content exists.
- Per-area light grid support with bake + load paths.
- Commands comparable to `bakeEnvironmentProbes` and `bakeLightGrids`.

Tasks:

- Implement probe placement policy using portal-area topology.
- Implement light grid data structures and interpolation for dynamic models and surfaces.
- Add cache format under `fs_savepath/openbase` (not packaged custom assets by default).
- Provide background bake jobs and resumable cache generation.

Exit criteria:

- Pitch-black ambient failure cases resolved without violating stock-asset behavior.
- Clean startup and runtime when baked data is absent (graceful fallback).

## Phase 5: Modern Post Lighting Stack (SSAO, TAA, Tonemap, Optional SMAA) (6-10 weeks)

Deliverables:

- Integrated pass chain for SSAO, TAA, tonemap.
- Existing SMAA path retained as an option.

Tasks:

- Introduce pass ordering and history buffers.
- Stabilize motion vectors and GUI/transparency handling for TAA.
- Add r_show/perf counters for pass timings.

Exit criteria:

- Visual stability in motion and acceptable ghosting profile.
- Pass cost is measurable and tunable from cvars.

## Phase 6: SSR + HiZ + Occlusion Culling (6-8 weeks)

Deliverables:

- HiZ generation and SSR path (optional toggle).
- Masked occlusion culling prototype then production mode.

Tasks:

- Integrate hierarchical depth path and screen-space trace.
- Restrict SSR to materials/classes where artifacts are manageable.
- Add MOC guardrails and diagnostics to prevent over-culling gameplay-critical entities.

Exit criteria:

- Net positive performance in heavy scenes with stable correctness.

## Phase 7: Renderer Backend Abstraction and Vulkan-First Bring-Up (parallel, 12-20+ weeks)

Deliverables:

- Backend abstraction seam in OpenQ4 renderer.
- Vulkan backend boot path with feature parity subset, then staged completion.

Tasks:

- Isolate API-specific resource, command, and pipeline concepts from gameplay/scene logic.
- Start Vulkan with the new lighting stack already validated in OpenGL.
- Keep platform behavior aligned with SDL3 + Meson.

Exit criteria:

- Vulkan path can run core campaign maps with parity checks passing.

## 7. Lighting and Shadowing Special Plan (Detail)

The lighting/shadow track should be implemented as four strict gates:

Gate L1:

- Atlas shadow maps (point/spot), tunable bias/sampling, fallback to stencil path.

Gate L2:

- Parallel/sun cascades, stable splits, blending of distant/small-light behavior.

Gate L3:

- PBR direct interactions + legacy fallback calibration for stock Quake 4 assets.

Gate L4:

- Indirect stack (env probes + light grids) with runtime fallback and no required packaged override assets.

Only after L4 should SSR/MOC be considered default-on candidates.

## 8. Risks and Mitigations

Risk:

- Quake 4 authored content diverges from Doom 3 BFG assumptions in light/entity behavior.

Mitigation:

- Keep dual-path rendering toggles during migration and compare per-map screenshots.

Risk:

- Shipping prebaked GI data conflicts with OpenQ4 asset rules.

Mitigation:

- Prefer runtime/generated cache in `fs_savepath`; keep packaged data optional and minimal.

Risk:

- Backend rewrite stalls gameplay parity work.

Mitigation:

- Deliver lighting features first on existing backend, then swap backend underneath validated pass logic.

Risk:

- Performance regressions from new passes.

Mitigation:

- Add hard perf budgets and per-pass timers from Phase 0 onward.

## 9. Immediate Next Steps (Recommended Order)

1. Create `docs-dev/proposals/rbdoom-shadow-modernization-spec.md` with Phase 1 technical design:
   - atlas layout
   - pass ordering
   - cvar contract
   - material/shader impact
2. Implement Phase 0 capture+telemetry harness and commit it before renderer changes.
3. Start Phase 1 behind `r_useShadowMapping` (default off) and run Procedure 1 loop for each milestone.

## 10. Primary Source References

- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/RELEASE-NOTES.md`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/README.md`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/neo/renderer/RenderSystem_init.cpp`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/neo/renderer/RenderBackend.cpp`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/neo/renderer/RenderWorld_lightgrid.cpp`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/neo/renderer/RenderWorld_envprobes.cpp`
- `E:/_SOURCE/_CODE/_tmp/RBDOOM-3-BFG-full2/neo/renderer/Material.cpp`
- `E:/_SOURCE/_CODE/Quake4Doom-master/src/renderer/RenderSystem_init.cpp`
- `e:/Repositories/OpenQ4/src/renderer/RenderSystem_init.cpp`
- `e:/Repositories/OpenQ4/src/renderer/tr_stencilshadow.cpp`
