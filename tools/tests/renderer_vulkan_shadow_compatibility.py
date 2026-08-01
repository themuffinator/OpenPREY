#!/usr/bin/env python3
"""Regression contracts for Vulkan shadow ownership and fail-closed safety."""

from __future__ import annotations

import importlib.util
import sys
import tempfile
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[2]
TEST_PATH = "tools/tests/renderer_vulkan_shadow_compatibility.py"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def compact(value: str) -> str:
    """Ignore indentation and line wrapping while retaining source semantics."""
    return " ".join(value.split())


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_compact(haystack: str, needle: str, context: str) -> None:
    if compact(needle) not in compact(haystack):
        raise AssertionError(f"Missing compact source contract {compact(needle)!r} in {context}")


def require_order(haystack: str, needles: tuple[str, ...], context: str) -> None:
    compact_haystack = compact(haystack)
    previous = -1
    for needle in needles:
        compact_needle = compact(needle)
        position = compact_haystack.find(compact_needle, previous + 1)
        if position == -1:
            raise AssertionError(f"Missing {compact_needle!r} in {context}")
        if position <= previous:
            raise AssertionError(f"Expected ordered source contracts in {context}: {needles!r}")
        previous = position


def braced_body(source: str, marker: str, context: str) -> str:
    start = source.find(marker)
    if start == -1:
        raise AssertionError(f"Missing {marker!r} in {context}")

    opening_brace = source.find("{", start + len(marker))
    if opening_brace == -1:
        raise AssertionError(f"Missing opening brace after {marker!r} in {context}")

    depth = 0
    for index in range(opening_brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find closing brace for {marker!r} in {context}")


def load_test_module(relative_path: str, module_name: str) -> ModuleType:
    path = ROOT / relative_path
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Could not load Python test module {relative_path}")
    module = importlib.util.module_from_spec(spec)
    # dataclasses and other runtime type helpers resolve the defining module
    # through sys.modules while its top-level declarations execute.
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


def validate_runtime_failure_gates() -> None:
    gameplay = load_test_module(
        "tools/tests/renderer_gameplay_benchmark.py",
        "openq4_renderer_gameplay_benchmark_contract",
    )
    matrix = load_test_module(
        "tools/tests/renderer_validation_matrix.py",
        "openq4_renderer_validation_matrix_contract",
    )

    signature_lines = {
        "vulkanValidation": "Vulkan validation: descriptor binding mismatch",
        "vulkanVuid": "Validation ID VUID-vkCmdDrawIndexed-commandBuffer-recording",
        "vulkanCallFailed": "Vulkan: vkCreateGraphicsPipelines failed (-3)",
        "fatal": "Fatal Error: renderer bootstrap stopped",
        "errorLine": "ERROR: render target creation stopped",
    }
    failure_text = "\n".join(signature_lines.values())
    native_fatal = "FATAL: renderer bootstrap stopped"
    benign_vk_result = "Vulkan: swapchain returned VK_ERROR_OUT_OF_DATE_KHR; retry scheduled"

    for module, counter_name, context in (
        (gameplay, "warning_counts", "gameplay benchmark"),
        (matrix, "count_warning_signatures", "renderer validation matrix"),
    ):
        counter = getattr(module, counter_name)
        counts = counter(failure_text)
        for signature in signature_lines:
            if counts.get(signature) != 1:
                raise AssertionError(
                    f"{context} must count synthetic {signature} exactly once; got {counts.get(signature)!r}"
                )

        diagnostics, omitted = module.collect_failure_diagnostics(
            (("synthetic.log", failure_text),)
        )
        if omitted != 0:
            raise AssertionError(f"{context} unexpectedly omitted synthetic failure diagnostics")
        for signature, source_line in signature_lines.items():
            matches = [
                item
                for item in diagnostics
                if signature in item["signatures"] and item["text"] == source_line
            ]
            if len(matches) != 1:
                raise AssertionError(
                    f"{context} must preserve the exact source line for {signature}; got {matches!r}"
                )

        benign_counts = counter(benign_vk_result)
        if benign_counts.get("errorLine") != 0:
            raise AssertionError(
                f"{context} must not treat an embedded VK_ERROR_* result name as an engine ERROR line"
            )
        native_fatal_counts = counter(native_fatal)
        if native_fatal_counts.get("fatal") != 1:
            raise AssertionError(
                f"{context} must recognize the engine-native FATAL: prefix"
            )
        native_diagnostics, native_omitted = module.collect_failure_diagnostics(
            (("native-fatal.log", native_fatal),)
        )
        if native_omitted != 0 or not any(
            "fatal" in item["signatures"] and item["text"] == native_fatal
            for item in native_diagnostics
        ):
            raise AssertionError(
                f"{context} must preserve the engine-native FATAL: diagnostic line"
            )

    checks_ok, missing = matrix.evaluate_checks(
        failure_text,
        [],
        matrix.count_warning_signatures(failure_text),
    )
    if checks_ok:
        raise AssertionError("Renderer validation matrix must fail when a fatal runtime signature is present")
    for signature in signature_lines:
        expected = f"warning signature: {signature}=1"
        if expected not in missing:
            raise AssertionError(f"Renderer validation matrix did not gate {expected!r}")
    benign_ok, benign_missing = matrix.evaluate_checks(
        benign_vk_result,
        [],
        matrix.count_warning_signatures(benign_vk_result),
    )
    if not benign_ok:
        raise AssertionError(
            f"Benign Vulkan result enum should not fail the validation matrix: {benign_missing!r}"
        )

    # Exercise the full gameplay-role evaluator, not only its regex table:
    # with all ordinary role evidence present, the signatures alone fail the
    # role, while a benign Vulkan result enum remains a pass.
    tmp_root = ROOT / ".tmp"
    tmp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="vulkan-shadow-contract-", dir=tmp_root) as temp_name:
        temp = Path(temp_name)
        savepath = temp / "save"
        log_path = savepath / "basepr" / "logs" / "contract.log"
        screenshot_rel = "screenshots/contract.tga"
        screenshot_path = savepath / "basepr" / screenshot_rel
        stdout_path = temp / "stdout.log"
        stderr_path = temp / "stderr.log"
        log_path.parent.mkdir(parents=True)
        screenshot_path.parent.mkdir(parents=True)
        stdout_path.write_text("", encoding="utf-8")
        stderr_path.write_text("", encoding="utf-8")
        screenshot_path.write_bytes(b"source-contract")

        spec = gameplay.RunSpec(
            case_id="vulkan-shadow-contract",
            mode="SP",
            map_name="game/contract",
            purpose="synthetic failure-gate coverage",
            path_name="spawn-static",
            tier="auto",
            maxfps="0",
            swap_interval="0",
            display_mode="windowed",
            shadow_preset="mapped",
            renderer="vk",
        )

        def evaluate(log_body: str) -> dict[str, object]:
            log_path.write_text(
                f"Selected renderer tier: Vulkan\n{log_body}\n",
                encoding="utf-8",
            )
            return gameplay.evaluate_role_result(
                spec=spec,
                role="client",
                exit_code=0,
                timed_out=False,
                elapsed_seconds=1.0,
                savepath=savepath,
                log_name="contract.log",
                stdout_path=stdout_path,
                stderr_path=stderr_path,
                screenshot_rel=screenshot_rel,
                reference_dir=None,
                rms_threshold=0.0,
                max_threshold=0,
                require_reference=False,
                require_benchmark=False,
            )

        failure_result = evaluate(failure_text)
        if failure_result["status"] != "fail":
            raise AssertionError("Gameplay role must fail when fatal Vulkan diagnostics are present")
        for signature in signature_lines:
            expected = f"{signature}=1"
            if expected not in failure_result["missing"]:
                raise AssertionError(f"Gameplay role did not gate {expected!r}")
        if len(failure_result["failureDiagnostics"]) < len(signature_lines):
            raise AssertionError("Gameplay role did not retain all synthetic failure diagnostic lines")

        benign_result = evaluate(benign_vk_result)
        if benign_result["warnings"]["errorLine"] != 0:
            raise AssertionError("Gameplay role classified VK_ERROR_* as an engine ERROR line")
        if benign_result["status"] != "pass":
            raise AssertionError(
                f"Benign Vulkan result enum should not fail the gameplay role: {benign_result['missing']!r}"
            )


def validate_receiver_ownership_split() -> None:
    header = read("src/renderer/Vulkan/vk_ShadowMap.h")
    require_order(
        header,
        (
            "typedef enum vkShadowReceiverPass_e {",
            "VK_SHADOW_RECEIVER_LOCAL = 0,",
            "VK_SHADOW_RECEIVER_GLOBAL,",
            "VK_SHADOW_RECEIVER_PASS_COUNT",
            "} vkShadowReceiverPass_t;",
        ),
        "Vulkan shadow receiver-pass enum",
    )
    require_compact(
        header,
        "vkShadowPassState_t passes[ VK_SHADOW_RECEIVER_PASS_COUNT ];",
        "per-light ownership resources",
    )
    require_compact(
        header,
        """int VK_ShadowMap_PrepareViewLights( const viewDef_t *viewDef,
                bool stencilFallbackAvailable );""",
        "active-target stencil availability contract",
    )

    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    caster_gate = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_PassHasCasters(",
        "ownership-specific caster gate",
    )
    require_order(
        caster_gate,
        (
            "vLight->globalShadowMapCasters != NULL",
            "vLight->globalShadowMapDynamicCasters != NULL",
            "return true;",
            "receiverPass == VK_SHADOW_RECEIVER_GLOBAL",
            "vLight->localShadowMapCasters != NULL",
            "vLight->localShadowMapDynamicCasters != NULL",
        ),
        "ownership-specific caster gate",
    )

    prepare = braced_body(
        shadow_map,
        "int VK_ShadowMap_PrepareViewLights(",
        "shadow-map light preparation",
    )
    require_compact(
        prepare,
        """const bool passNeeded[ VK_SHADOW_RECEIVER_PASS_COUNT ] = {
            vLight->localInteractions != NULL,
            vLight->globalInteractions != NULL || hasTranslucentReceivers
        };""",
        "ownership-specific receiver admission",
    )
    require_compact(
        prepare,
        """const bool passHasCasters[ VK_SHADOW_RECEIVER_PASS_COUNT ] = {
            VK_ShadowMap_PassHasCasters( vLight, VK_SHADOW_RECEIVER_LOCAL ),
            VK_ShadowMap_PassHasCasters( vLight, VK_SHADOW_RECEIVER_GLOBAL )
        };""",
        "ownership-specific caster admission",
    )
    safe_alias = compact(
        """receiverPass == VK_SHADOW_RECEIVER_GLOBAL
           && !VK_ShadowMap_HasLocalCasters( vLight )
           && entry.passes[ VK_SHADOW_RECEIVER_LOCAL ].valid"""
    )
    if compact(prepare).count(safe_alias) < 2:
        raise AssertionError(
            "Projected and point shadow resources must alias LOCAL to GLOBAL only when no local casters exist"
        )

    render = braced_body(
        shadow_map,
        "bool VK_ShadowMap_RenderAtlas(",
        "shadow-map caster rendering",
    )
    for token in (
        "vLight->globalShadowMapCasters",
        "vLight->globalShadowMapDynamicCasters",
        "vLight->localShadowMapCasters",
        "vLight->localShadowMapDynamicCasters",
    ):
        if render.count(token) < 2:
            raise AssertionError(
                f"Projected and point ownership passes must both retain caster chain {token!r}"
            )
    if compact(render).count(
        compact("receiverPass == VK_SHADOW_RECEIVER_GLOBAL")
    ) < 2:
        raise AssertionError(
            "Projected and point GLOBAL resources must both add local caster chains"
        )

    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    require_compact(
        interactions,
        """const bool stencilFallbackAvailable =
                activeTargetHasStencil &&
                interPass.pipelineStencilShadow != VK_NULL_HANDLE;
            if ( VK_ShadowMap_PrepareViewLights(
                    viewDef, stencilFallbackAvailable ) > 0 )""",
        "active-target stencil availability handoff",
    )
    stencil_pass = braced_body(
        interactions,
        "static bool VK_StencilShadowPass(",
        "runtime stencil-volume submission",
    )
    require_order(
        stencil_pass,
        (
            "bool complete = true;",
            "interPass.volumeSkipCount++;",
            "complete = false;",
            "return complete;",
        ),
        "runtime stencil-volume completion result",
    )
    if stencil_pass.count("complete = false;") < 3:
        raise AssertionError(
            "Every classic and packed stencil geometry failure must invalidate the runtime fallback"
        )

    draw_lights = braced_body(
        interactions,
        "void VK_Interactions_DrawLights(",
        "Vulkan light interactions",
    )
    require_order(
        draw_lights,
        (
            """localShadowState = VK_ShadowMap_PassState(
                shadowState, VK_SHADOW_RECEIVER_LOCAL );""",
            """globalShadowState = VK_ShadowMap_PassState(
                shadowState, VK_SHADOW_RECEIVER_GLOBAL );""",
            "localEmptyFallback ? NULL : localShadowState",
            "VK_DrawInteractionChain( vLight->localInteractions );",
            "globalEmptyFallback ? NULL : globalShadowState",
            "VK_DrawInteractionChain( vLight->globalInteractions );",
        ),
        "ownership-specific receiver selection",
    )


def validate_shadow_depth_format_selection() -> None:
    device_header = read("src/renderer/Vulkan/VulkanDevice.h")
    for field in (
        "VkFormat shadowDepthFormat;",
        "bool shadowDepthHasStencil;",
        "bool shadowDepthFilterLinear;",
    ):
        require_compact(device_header, field, "Vulkan shadow depth capabilities")

    device = read("src/renderer/Vulkan/VulkanDevice.cpp")
    stencil_classifier = braced_body(
        device,
        "static bool VK_Device_DepthFormatHasStencil(",
        "depth/stencil format classification",
    )
    require_order(
        stencil_classifier,
        (
            "case VK_FORMAT_D16_UNORM_S8_UINT:",
            "case VK_FORMAT_D24_UNORM_S8_UINT:",
            "case VK_FORMAT_D32_SFLOAT_S8_UINT:",
            "return true;",
            "default:",
            "return false;",
        ),
        "depth/stencil format classification",
    )

    selector = braced_body(
        device,
        "static void VK_Device_SelectShadowDepthFormat(",
        "shadow depth-format selection",
    )
    require_order(
        selector,
        (
            "vkCtx.shadowDepthFormat = VK_FORMAT_UNDEFINED;",
            "VK_FORMAT_D24_UNORM_S8_UINT,",
            "VK_FORMAT_D32_SFLOAT_S8_UINT,",
            "VK_FORMAT_D32_SFLOAT,",
            "VK_FORMAT_X8_D24_UNORM_PACK32,",
            "VK_FORMAT_D16_UNORM,",
            "VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT",
            "VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT",
            "VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT",
            "VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT",
            "VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT",
            "vkGetPhysicalDeviceFormatProperties2(",
            # VkFormatProperties3 is core 1.3 and every implementation at the
            # renderer's API floor populates it, but an implementation that
            # left it zeroed would silently reject every candidate and disable
            # shadow maps, so the selector falls back to the 1.0 flags.
            "VkFormatFeatureFlags2 optimalFeatures = props3.optimalTilingFeatures;",
            "if ( optimalFeatures == 0 ) {",
            "props2.formatProperties.optimalTilingFeatures",
            "VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT",
            "optimalFeatures |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;",
            "( optimalFeatures & requiredFeatures ) != requiredFeatures",
            "VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT",
            "nearestFallback = candidates[ i ];",
            "vkCtx.shadowDepthFormat = candidates[ i ];",
            "vkCtx.shadowDepthHasStencil = VK_Device_DepthFormatHasStencil( candidates[ i ] );",
            "vkCtx.shadowDepthFilterLinear = true;",
        ),
        "shadow depth-format selection",
    )
    require_compact(
        selector,
        """if ( vkCtx.shadowDepthFormat == VK_FORMAT_UNDEFINED
            && nearestFallback != VK_FORMAT_UNDEFINED )""",
        "nearest-filter shadow depth fallback",
    )
    require_compact(
        selector,
        "vkCtx.shadowDepthHasStencil = nearestFallbackHasStencil;",
        "nearest-filter shadow stencil metadata",
    )

    device_init = braced_body(device, "bool VK_Device_Init(", "Vulkan device initialization")
    require_order(
        device_init,
        (
            "vkCtx.physicalDevice = devices[ chosenDevice ];",
            "VK_Device_SelectShadowDepthFormat();",
        ),
        "shadow depth probing after physical-device selection",
    )

    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    for marker, context in (
        ("VkPipeline VK_Exec_CasterPipeline(", "projected shadow caster pipeline"),
        ("VkPipeline VK_Exec_PointCasterPipeline(", "point shadow caster pipeline"),
    ):
        caster_pipeline = braced_body(executor, marker, context)
        require_compact(
            caster_pipeline,
            "target.depthFormat = vkCtx.shadowDepthFormat;",
            context,
        )
        require_compact(
            caster_pipeline,
            """target.stencilFormat = vkCtx.shadowDepthHasStencil
                ? vkCtx.shadowDepthFormat : VK_FORMAT_UNDEFINED;""",
            context,
        )

    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    aspect_mask = braced_body(
        shadow_map,
        "static VkImageAspectFlags VK_ShadowMap_DepthAspectMask(",
        "shadow attachment aspect selection",
    )
    require_compact(
        aspect_mask,
        """VK_IMAGE_ASPECT_DEPTH_BIT |
            ( vkCtx.shadowDepthHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0 )""",
        "shadow attachment aspect selection",
    )

    resources = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_EnsureResources(",
        "shadow-map resource creation",
    )
    require_order(
        resources,
        (
            "vkCtx.shadowDepthFormat == VK_FORMAT_UNDEFINED",
            "ici.format = vkCtx.shadowDepthFormat;",
            """ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                | VK_IMAGE_USAGE_SAMPLED_BIT
                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                | VK_IMAGE_USAGE_TRANSFER_DST_BIT;""",
            "ivci.subresourceRange.aspectMask = VK_ShadowMap_DepthAspectMask();",
            "ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;",
            "vkCtx.shadowDepthFilterLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST",
            "sci.compareEnable = VK_TRUE;",
        ),
        "shadow-map resource creation",
    )

    render = braced_body(
        shadow_map,
        "bool VK_ShadowMap_RenderAtlas(",
        "shadow-map caster rendering",
    )
    if compact(render).count(
        compact("ri.pStencilAttachment = vkCtx.shadowDepthHasStencil ? &depth : NULL;")
    ) < 2:
        raise AssertionError(
            "Projected and point shadow rendering must both omit stencil attachment metadata for depth-only formats"
        )


def validate_csm_atlas_and_receiver_contract() -> None:
    header = read("src/renderer/Vulkan/vk_ShadowMap.h")
    require(header, '#include "../ShadowMapProjected.h"', "Vulkan CSM shared state")
    require_compact(
        header,
        "float atlasRects[ SHADOWMAP_PROJECTED_MAX_CASCADES ][ 4 ];",
        "per-cascade atlas rectangles",
    )
    require_compact(
        header,
        "shadowMapProjectedLightState_t projectedState;",
        "full projected-light CSM state",
    )

    classification_source = read("src/renderer/ShadowMapClassification.cpp")
    classification = braced_body(
        classification_source,
        "shadowMapLightClassification_t R_ClassifyShadowMapLight(",
        "shared shadow-map light classification",
    )
    require_order(
        classification,
        (
            "if ( classification.csmEnabled )",
            "classification.cascadeCount = requestedCascadeCount;",
            "classification.atlasDiv = 2;",
            "classification.tileCount = requestedCascadeCount;",
        ),
        "CSM 2x2 atlas classification",
    )

    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    allocator = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_AllocTileBlock(",
        "contiguous shadow-atlas block allocator",
    )
    require_order(
        allocator,
        (
            "blockSize <= 0 || blockSize > vkShadow.atlasSize",
            "vkShadow.nextTileX + blockSize > vkShadow.atlasSize",
            "vkShadow.nextTileY += vkShadow.nextTileRowHeight;",
            "vkShadow.nextTileY + blockSize > vkShadow.atlasSize",
            "tileX = vkShadow.nextTileX;",
            "tileY = vkShadow.nextTileY;",
            "vkShadow.nextTileX += blockSize;",
            "vkShadow.nextTileRowHeight = Max( vkShadow.nextTileRowHeight, blockSize );",
        ),
        "contiguous shadow-atlas block allocator",
    )

    projected_alloc = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_AllocateProjectedPass(",
        "projected cascade-block allocation",
    )
    require_order(
        projected_alloc,
        (
            "const int atlasDiv = idMath::ClampInt( 1, 2, light.projectedState.atlasDiv );",
            "const int blockSize = light.tileSize * atlasDiv;",
            "VK_ShadowMap_AllocTileBlock( blockSize, tileX, tileY )",
            "const int cascadeCount = idMath::ClampInt( 1, SHADOWMAP_PROJECTED_MAX_CASCADES, light.projectedState.cascadeCount );",
            "for ( int cascadeIndex = 0 ; cascadeIndex < cascadeCount ; cascadeIndex++ )",
            "const int cascadeX = cascadeIndex % atlasDiv;",
            "const int cascadeY = cascadeIndex / atlasDiv;",
            "const int cascadeTileX = tileX + cascadeX * light.tileSize;",
            "const int cascadeTileY = tileY + cascadeY * light.tileSize;",
            "pass.atlasRects[ cascadeIndex ][ 0 ] = (float)cascadeTileX * invAtlas;",
            "pass.atlasRects[ cascadeIndex ][ 1 ] = (float)( cascadeTileY + light.tileSize ) * invAtlas;",
            "pass.atlasRects[ cascadeIndex ][ 2 ] = (float)( cascadeTileX + light.tileSize ) * invAtlas;",
            "pass.atlasRects[ cascadeIndex ][ 3 ] = (float)cascadeTileY * invAtlas;",
        ),
        "projected 1x1/2x2 cascade tile placement",
    )

    prepare = braced_body(
        shadow_map,
        "int VK_ShadowMap_PrepareViewLights(",
        "projected CSM light preparation",
    )
    require_order(
        prepare,
        (
            "const int requestedAtlasDiv = idMath::ClampInt( 1, 2, classification.atlasDiv );",
            "const int maxTileSize = vkShadow.atlasSize / requestedAtlasDiv;",
            "R_BuildShadowMapProjectedLightState( vLight, viewDef, tileSize, projectedState );",
            "projectedState.cascadeCount > SHADOWMAP_PROJECTED_MAX_CASCADES",
            "projectedState.atlasDiv < 1 || projectedState.atlasDiv > 2",
            "projectedState.tileSize * projectedState.atlasDiv > vkShadow.atlasSize",
            "entry.tileSize = projectedState.tileSize;",
            "entry.projectedState = projectedState;",
        ),
        "projected CSM admission and state retention",
    )

    render = braced_body(
        shadow_map,
        "bool VK_ShadowMap_RenderAtlas(",
        "projected cascade caster rendering",
    )
    require_order(
        render,
        (
            "const int cascadeCount = idMath::ClampInt( 1, SHADOWMAP_PROJECTED_MAX_CASCADES, light.projectedState.cascadeCount );",
            "const int atlasDiv = idMath::ClampInt( 1, 2, light.projectedState.atlasDiv );",
            "for ( int cascadeIndex = 0 ; cascadeIndex < cascadeCount ; cascadeIndex++ )",
            "const int cascadeTileX = pass.tileX + ( cascadeIndex % atlasDiv ) * light.tileSize;",
            "const int cascadeTileY = pass.tileY + ( cascadeIndex / atlasDiv ) * light.tileSize;",
            "viewport.x = (float)cascadeTileX;",
            "viewport.y = (float)( cascadeTileY + light.tileSize );",
            "scissor.offset.x = cascadeTileX;",
            "scissor.offset.y = cascadeTileY;",
            "VK_ShadowMap_DrawCasterChain( ctx, light, cascadeIndex, vLight->globalShadowMapCasters )",
            "VK_ShadowMap_DrawCasterChain( ctx, light, cascadeIndex, vLight->globalShadowMapDynamicCasters )",
            "if ( receiverPass == VK_SHADOW_RECEIVER_GLOBAL )",
            "VK_ShadowMap_DrawCasterChain( ctx, light, cascadeIndex, vLight->localShadowMapCasters )",
            "VK_ShadowMap_DrawCasterChain( ctx, light, cascadeIndex, vLight->localShadowMapDynamicCasters )",
            "VK_ShadowMap_InvalidatePassResource( light, receiverPass );",
        ),
        "complete per-cascade ownership rendering",
    )

    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    shadow_slice = braced_body(
        interactions,
        "static int VK_Inter_WriteShadowSlice(",
        "projected CSM receiver block writer",
    )
    require_order(
        shadow_slice,
        (
            "const shadowMapProjectedLightState_t &projected = state->projectedState;",
            "for ( int cascadeIndex = 0 ; cascadeIndex < cascadeCount ; cascadeIndex++ )",
            "projected.clipPlanes[ cascadeIndex ][ 0 ]",
            "block.shadowRow0[ cascadeIndex ]",
            "projected.clipPlanes[ cascadeIndex ][ 1 ]",
            "block.shadowRow1[ cascadeIndex ]",
            "projected.clipPlanes[ cascadeIndex ][ 2 ]",
            "block.shadowRow2[ cascadeIndex ]",
            "projected.clipPlanes[ cascadeIndex ][ 3 ]",
            "block.shadowRow3[ cascadeIndex ]",
            "memcpy( block.atlasRects, passState->atlasRects, sizeof( block.atlasRects ) );",
            "memcpy( block.splitDepths, projected.splitDepths, sizeof( block.splitDepths ) );",
            "block.viewDepthRow[ 0 ] = -modelView[ 2 ];",
            "block.viewDepthRow[ 1 ] = -modelView[ 6 ];",
            "block.viewDepthRow[ 2 ] = -modelView[ 10 ];",
            "block.viewDepthRow[ 3 ] = -modelView[ 14 ];",
            "block.biasParams[ 2 ] = idMath::ClampFloat( 0.0f, 0.5f, r_shadowMapCascadeBlend.GetFloat() );",
            "block.biasParams[ 3 ] = (float)cascadeCount;",
        ),
        "four-cascade receiver coordinate ABI",
    )

    vertex_shader = read("src/renderer/Vulkan/shaders/interaction_shadow.vert")
    vertex_main = braced_body(
        vertex_shader,
        "void main()",
        "projected shadow receiver vertex shader",
    )
    for cascade_index in range(4):
        require(
            vertex_shader,
            f"layout(location = {8 + cascade_index}) out vec4 vShadowCoord{cascade_index};",
            "four projected receiver coordinates",
        )
        require_compact(
            vertex_main,
            f"vShadowCoord{cascade_index} = BuildShadowCoord(position, shadowNormal, shadowSinTheta, {cascade_index});",
            "four projected receiver coordinates",
        )
    require(
        vertex_shader,
        "layout(location = 14) out float vViewDepth;",
        "projected receiver view depth",
    )
    require_compact(
        vertex_main,
        "vViewDepth = max(dot(position, shadow.viewDepthRow), 0.0);",
        "projected receiver view depth",
    )

    fragment_shader = read("src/renderer/Vulkan/shaders/interaction_shadow.frag")
    coord_select = braced_body(
        fragment_shader,
        "vec4 ShadowCoordByIndex(",
        "projected receiver coordinate selection",
    )
    atlas_select = braced_body(
        fragment_shader,
        "vec4 AtlasRectByIndex(",
        "projected receiver atlas selection",
    )
    for cascade_index in range(4):
        require(
            coord_select,
            f"vShadowCoord{cascade_index}",
            "projected receiver coordinate selection",
        )
        require(
            atlas_select,
            f"shadow.atlasRects[{cascade_index}]",
            "projected receiver atlas selection",
        )

    cascade_select = braced_body(
        fragment_shader,
        "int SelectShadowCascade(",
        "view-depth cascade selection",
    )
    require_order(
        cascade_select,
        (
            "int interiorSplitCount = ShadowCascadeCount() - 1;",
            "viewDepth < shadow.splitDepths.x",
            "return 0;",
            "viewDepth < shadow.splitDepths.y",
            "return 1;",
            "viewDepth < shadow.splitDepths.z",
            "return 2;",
            "return 3;",
        ),
        "view-depth cascade selection",
    )

    shadow_factor = braced_body(
        fragment_shader,
        "float SampleShadowFactor()",
        "cascade split-band blending",
    )
    require_order(
        shadow_factor,
        (
            "int cascadeIndex = SelectShadowCascade(vViewDepth);",
            "float shadowFactor = SampleCascadeByIndex(cascadeIndex);",
            "int lastInteriorIndex = ShadowCascadeCount() - 2;",
            "float cascadeBlend = shadow.biasParams.z;",
            "float previousSplit = cascadeIndex == 0 ? 0.0",
            "float currentSplit = CascadeComponent(shadow.splitDepths, cascadeIndex);",
            "(currentSplit - previousSplit) * cascadeBlend",
            "float blendStart = currentSplit - blendWidth;",
            "float blend = clamp((vViewDepth - blendStart) / blendWidth, 0.0, 1.0);",
            "return mix(shadowFactor, SampleCascadeByIndex(cascadeIndex + 1), blend);",
        ),
        "cascade split-band blending",
    )


def validate_shadow_descriptor_abi() -> None:
    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    require(
        executor,
        "static const int VK_SHADOW_UNIFORM_SLICE_BYTES = 512;",
        "set-7 512-byte shadow UBO ABI",
    )

    init = braced_body(executor, "static bool VK_GuiExecutor_Init(", "Vulkan executor initialization")
    require_order(
        init,
        (
            "vkCtx.deviceProperties.limits.maxUniformBufferRange < VK_SHADOW_UNIFORM_SLICE_BYTES",
            "VK_Exec_UniformSliceAlignment( VK_SHADOW_UNIFORM_SLICE_BYTES ) == 0",
            "shadowBindings[ 0 ].binding = 0;",
            "shadowBindings[ 0 ].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;",
            "shadowBindings[ 1 ].binding = 1;",
            "shadowBindings[ 1 ].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;",
            "shadowBindings[ 2 ].binding = 2;",
            "shadowBindings[ 2 ].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;",
            "dslci.bindingCount = 3;",
            "interactionSetLayouts[ 7 ] = vkExec.shadowSetLayout;",
            "bufferInfo.range = VK_SHADOW_UNIFORM_SLICE_BYTES;",
            "write.dstBinding = 1;",
        ),
        "set-7 compare/UBO/raw descriptor layout",
    )

    shadow_alloc = braced_body(
        executor,
        "int VK_Exec_ShadowUniformAlloc(",
        "set-7 shadow UBO allocation",
    )
    require_order(
        shadow_alloc,
        (
            "bytes > VK_SHADOW_UNIFORM_SLICE_BYTES",
            "VK_Exec_UniformSliceAlignment( VK_SHADOW_UNIFORM_SLICE_BYTES )",
            "VK_Ring_Alloc(",
        ),
        "set-7 512-byte shadow UBO allocation",
    )

    descriptor_getter = braced_body(
        executor,
        "VkDescriptorSet VK_Exec_ShadowDescriptorSet(",
        "atlas shadow descriptor publication",
    )
    require_compact(
        descriptor_getter,
        "return vkExec.shadowSetsHaveAtlas ? vkExec.shadowSets[ vkExec.frameSlot ] : VK_NULL_HANDLE;",
        "atlas shadow descriptor publication",
    )

    atlas_descriptors = braced_body(
        executor,
        "bool VK_Exec_UpdateShadowAtlasDescriptors(",
        "atlas compare/raw descriptors",
    )
    require_order(
        atlas_descriptors,
        (
            "vkExec.shadowSetsHaveAtlas = false;",
            "view == VK_NULL_HANDLE",
            "compareSampler == VK_NULL_HANDLE",
            "rawSampler == VK_NULL_HANDLE",
            "imageInfos[ 0 ].sampler = compareSampler;",
            "imageInfos[ 1 ].sampler = rawSampler;",
            "writes[ writeIndex ].dstBinding = writeIndex == 0 ? 0 : 2;",
            "vkUpdateDescriptorSets( vkCtx.device, 2, writes, 0, NULL );",
            "vkExec.shadowSetsHaveAtlas = true;",
        ),
        "fail-closed atlas compare/raw descriptor publication",
    )

    cube_descriptors = braced_body(
        executor,
        "bool VK_Exec_CreateShadowCubeSets(",
        "point cube compare/raw descriptors",
    )
    require_order(
        cube_descriptors,
        (
            "compareSampler == VK_NULL_HANDLE",
            "rawSampler == VK_NULL_HANDLE",
            "bufferInfo.range = VK_SHADOW_UNIFORM_SLICE_BYTES;",
            "ringWrite.dstBinding = 1;",
            "imageInfos[ 0 ].sampler = compareSampler;",
            "imageInfos[ 1 ].sampler = rawSampler;",
            "writes[ writeIndex ].dstBinding = writeIndex == 0 ? 0 : 2;",
            "vkUpdateDescriptorSets( vkCtx.device, 2, writes, 0, NULL );",
        ),
        "fail-closed point cube compare/raw descriptors",
    )

    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    resources = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_EnsureResources(",
        "atlas compare/raw sampler resources",
    )
    require_compact(
        resources,
        """vkShadow.atlasImage != VK_NULL_HANDLE
            && vkShadow.atlasSize == wantedSize
            && vkShadow.compareSampler != VK_NULL_HANDLE
            && vkShadow.rawSampler != VK_NULL_HANDLE""",
        "live atlas requires both sampler families",
    )
    compare_sampler = braced_body(
        resources,
        "if ( vkShadow.compareSampler == VK_NULL_HANDLE )",
        "shadow comparison sampler",
    )
    require_order(
        compare_sampler,
        (
            "sci.compareEnable = VK_TRUE;",
            "sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;",
            "vkCreateSampler( vkCtx.device, &sci, NULL, &vkShadow.compareSampler )",
        ),
        "shadow comparison sampler",
    )
    raw_sampler = braced_body(
        resources,
        "if ( vkShadow.rawSampler == VK_NULL_HANDLE )",
        "shadow raw-depth sampler",
    )
    require_order(
        raw_sampler,
        (
            "sci.magFilter = VK_FILTER_NEAREST;",
            "sci.minFilter = VK_FILTER_NEAREST;",
            "sci.compareEnable = VK_FALSE;",
            "vkCreateSampler( vkCtx.device, &sci, NULL, &vkShadow.rawSampler )",
        ),
        "shadow raw-depth sampler",
    )
    require_compact(
        resources,
        """VK_Exec_UpdateShadowAtlasDescriptors( vkShadow.atlasSampleView,
            vkShadow.compareSampler, vkShadow.rawSampler )""",
        "atlas descriptor update requires compare and raw samplers",
    )

    point_cube = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_CreatePointCube(",
        "point shadow cube resources",
    )
    require_order(
        point_cube,
        (
            "vkShadow.compareSampler == VK_NULL_HANDLE",
            "vkShadow.rawSampler == VK_NULL_HANDLE",
            "VK_Exec_CreateShadowCubeSets( cube.cubeSampleView, vkShadow.compareSampler, vkShadow.rawSampler, cube.sets )",
        ),
        "point cube resources require compare and raw samplers",
    )

    projected_shader = read("src/renderer/Vulkan/shaders/interaction_shadow.frag")
    point_shader = read("src/renderer/Vulkan/shaders/interaction_shadow_point.frag")
    for shader, sampler_type, raw_type, context in (
        (projected_shader, "sampler2DShadow", "sampler2D", "projected shadow receiver"),
        (point_shader, "samplerCubeShadow", "samplerCube", "point shadow receiver"),
    ):
        require(
            shader,
            f"layout(set = 7, binding = 0) uniform {sampler_type} shadowCompareMap;",
            f"{context} comparison binding",
        )
        require(
            shader,
            f"layout(set = 7, binding = 2) uniform {raw_type} shadowRawMap;",
            f"{context} raw-depth binding",
        )


def validate_exact_filter_tiers(sample_body: str, sample_call: str, context: str) -> None:
    tier_start = sample_body.find("float result = " + sample_call)
    if tier_start == -1:
        raise AssertionError(f"{context} is missing the center sample that starts its tiered kernel")
    if sample_body[tier_start:].count(sample_call) != 13:
        raise AssertionError(f"{context} must issue exactly 13 samples at its maximum tier")
    if sample_body.count("RotateShadowOffset(") != 12:
        raise AssertionError(f"{context} must rotate exactly the twelve off-center Poisson taps")
    require_order(
        sample_body,
        (
            "float result = " + sample_call,
            "if (shadow.filterParams.y <= 1.0)",
            "return result;",
            "if (shadow.filterParams.y <= 5.0)",
            "return result * (1.0 / 5.0);",
            "if (shadow.filterParams.y <= 9.0)",
            "return result * (1.0 / 9.0);",
            "return result * (1.0 / 13.0);",
        ),
        f"{context} exact 1/5/9/13 tiers",
    )


def validate_shadow_filtering_contract() -> None:
    classification = read("src/renderer/ShadowMapClassification.cpp")
    shared_settings = braced_body(
        classification,
        "shadowMapProjectedFilterSettings_t R_ShadowMapProjectedFilterSettings(",
        "shared projected shadow filter policy",
    )
    require_order(
        shared_settings,
        (
            "const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );",
            "settings.filterScale = settings.distantSource",
            "r_shadowMapDistantFilterScale.GetFloat()",
            "settings.filterRadius = Max( 0.0f, r_shadowMapFilterRadius.GetFloat() ) * settings.filterScale;",
            "settings.filterTaps = idMath::ClampInt( 1, 13, r_shadowMapFilterTaps.GetInteger() );",
            "settings.filterMode = idMath::ClampInt( 0, 2, r_shadowMapFilterMode.GetInteger() );",
            "settings.pcssLightRadius = Max( 0.0f, r_shadowMapPCSSLightRadius.GetFloat() ) * settings.filterScale;",
            "settings.pcssMaxRadius = Max( 0.0f, r_shadowMapPCSSMaxRadius.GetFloat() ) * settings.filterScale;",
            "if ( settings.filterMode == 2 )",
            "Max( settings.pcssLightRadius, settings.pcssMaxRadius )",
        ),
        "shared projected shadow filter policy",
    )

    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    shadow_slice = braced_body(
        interactions,
        "static int VK_Inter_WriteShadowSlice(",
        "Vulkan shadow filter parameter upload",
    )
    require_order(
        shadow_slice,
        (
            "r_shadowMapPointFilterRadius.GetFloat()",
            "idMath::ClampInt( 1, 13, r_shadowMapPointFilterTaps.GetInteger() )",
            "idMath::ClampInt( 0, 1, r_shadowMapPointFilterMode.GetInteger() )",
            "r_shadowMapPointDepthCompare.GetBool() ? 1.0f : 0.0f",
            "R_ShadowMapProjectedFilterSettings( state->vLight )",
            "block.filterParams[ 0 ] = filterSettings.filterRadius;",
            "block.filterParams[ 1 ] = (float)filterSettings.filterTaps;",
            "block.filterParams[ 2 ] = (float)filterSettings.filterMode;",
            "r_shadowMapDepthCompare.GetBool() && filterSettings.filterMode != 2",
            "block.pcssParams[ 0 ] = filterSettings.pcssLightRadius;",
            "block.pcssParams[ 1 ] = filterSettings.pcssMaxRadius;",
            "block.pcssParams[ 2 ] = filterSettings.effectiveFilterRadius;",
            "r_shadowMapReceiverPlaneBias.GetBool() ? 1.0f : 0.0f",
        ),
        "shared projected and point runtime filter upload",
    )

    projected = read("src/renderer/Vulkan/shaders/interaction_shadow.frag")
    projected_compare = braced_body(
        projected,
        "float SampleShadowCompare(",
        "projected runtime depth comparison",
    )
    require_order(
        projected_compare,
        (
            "float compareDepth = depth - ShadowReceiverBias(cascadeIndex);",
            "if (shadow.filterParams.w > 0.5)",
            "texture(shadowCompareMap, vec3(uv, compareDepth))",
            "texture(shadowRawMap, uv).r",
            "compareDepth <= storedDepth ? 1.0 : 0.0",
        ),
        "projected runtime compare/raw selection",
    )

    projected_rotation = braced_body(
        projected,
        "vec2 RotateShadowOffset(",
        "projected stable Poisson rotation",
    )
    require_order(
        projected_rotation,
        (
            "if (shadow.filterParams.z < 0.5)",
            "StableShadowHash(vec3(",
            "floor(uv / max(shadow.texelSize.x, 1.0e-6))",
            "floor(depth * 1024.0)",
            "* 6.2831853",
            "return vec2(c * offset.x - s * offset.y, s * offset.x + c * offset.y);",
        ),
        "projected stable Poisson rotation",
    )

    receiver_bias = braced_body(
        projected,
        "float ShadowReceiverBias(",
        "projected derivative receiver bias",
    )
    require_order(
        receiver_bias,
        (
            "float receiverPlaneBias = 0.0;",
            "if (shadow.pcssParams.w > 0.5)",
            "ShadowDepthGradient(cascadeIndex)",
            "max(shadow.pcssParams.z, 1.0)",
            "max(max(texelBias, receiverPlaneBias), 0.0)",
        ),
        "projected derivative receiver bias",
    )

    blocker_search = braced_body(
        projected,
        "float ProjectedPCSSRadius(",
        "projected PCSS blocker search",
    )
    require_order(
        blocker_search,
        (
            "shadow.filterParams.z < 1.5 || shadow.filterParams.w > 0.5",
            "float compareDepth = depth - ShadowReceiverBias(cascadeIndex);",
            "float blockerDepth = 0.0;",
            "float blockerCount = 0.0;",
            "float d0 = RawShadowDepth(uv);",
            "float d1 = RawShadowDepth(",
            "float d2 = RawShadowDepth(",
            "float d3 = RawShadowDepth(",
            "float d4 = RawShadowDepth(",
            "if (blockerCount <= 0.0)",
            "float averageBlocker = blockerDepth / blockerCount;",
            "float penumbra = (depth - averageBlocker) / max(averageBlocker, 1.0e-4);",
            "return clamp(max(baseRadius, penumbra * shadow.pcssParams.x), baseRadius, maxRadius);",
        ),
        "projected PCSS blocker search",
    )
    if blocker_search.count("RawShadowDepth(") != 5:
        raise AssertionError("Projected PCSS blocker search must use its fixed five raw-depth probes")

    projected_samples = braced_body(
        projected,
        "float SampleShadowCascade(",
        "projected Poisson shadow filter",
    )
    validate_exact_filter_tiers(
        projected_samples,
        "SampleShadowCompare(",
        "projected Poisson shadow filter",
    )

    projected_main = braced_body(projected, "void main()", "projected shadow fragment main")
    if projected_main.count("dFdx(") != 4 or projected_main.count("dFdy(") != 4:
        raise AssertionError(
            "Projected receiver-plane bias must derive all four cascade depths before selection"
        )
    require_order(
        projected_main,
        (
            "if (shadow.pcssParams.w > 0.5)",
            "gShadowDepthGradients = vec4(",
            "abs(dFdx(vShadowCoord0.z)) + abs(dFdy(vShadowCoord0.z))",
            "abs(dFdx(vShadowCoord3.z)) + abs(dFdy(vShadowCoord3.z))",
            "light *= SampleShadowFactor();",
        ),
        "projected derivative calculation before divergent cascade sampling",
    )

    point = read("src/renderer/Vulkan/shaders/interaction_shadow_point.frag")
    point_compare = braced_body(
        point,
        "float SamplePointShadowCompare(",
        "point runtime depth comparison",
    )
    require_order(
        point_compare,
        (
            "float compareDepth = depth - ShadowReceiverBias();",
            "if (shadow.samplingParams.x > 0.5)",
            "texture(shadowCompareMap, vec4(direction, compareDepth))",
            "texture(shadowRawMap, direction).r",
            "compareDepth <= storedDepth ? 1.0 : 0.0",
        ),
        "point runtime compare/raw selection",
    )

    point_rotation = braced_body(
        point,
        "vec2 RotateShadowOffset(",
        "point stable tangent-disc rotation",
    )
    require_order(
        point_rotation,
        (
            "if (shadow.filterParams.z < 0.5)",
            "StableShadowHash(floor(direction * 37.0))",
            "* 6.2831853",
            "return vec2(c * offset.x - s * offset.y, s * offset.x + c * offset.y);",
        ),
        "point stable tangent-disc rotation",
    )

    point_samples = braced_body(
        point,
        "float SampleShadowFactor()",
        "point tangent-disc shadow filter",
    )
    require_order(
        point_samples,
        (
            "vec3 direction = SafeNormalize(vPointShadowVector);",
            "vec3 tangent = SafeNormalize(cross(up, direction));",
            "vec3 bitangent = cross(direction, tangent);",
            "float tap = shadow.filterParams.w * filterRadius;",
            "tangent * o1.x + bitangent * o1.y",
        ),
        "point tangent-disc sampling basis",
    )
    validate_exact_filter_tiers(
        point_samples,
        "SamplePointShadowCompare(",
        "point tangent-disc shadow filter",
    )


def validate_exact_static_cache_and_admission_contract() -> None:
    header = read("src/renderer/Vulkan/vk_ShadowMap.h")
    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")

    resources_known_good = braced_body(
        shadow_map,
        "bool VK_ShadowMap_ResourcesKnownGood(",
        "conservative Vulkan shadow resource truth",
    )
    require_order(
        resources_known_good,
        (
            "vkShadowProjectedResourcesOkGeneration != tr.videoRestartCount",
            "return false;",
            """pointLight
                && vkShadowPointResourcesOkGeneration
                    != tr.videoRestartCount""",
            "return false;",
        ),
        "generation-gated Vulkan shadow resource truth",
    )
    if "return true;" in resources_known_good:
        raise AssertionError(
            "Vulkan resource truth must remain conservative until pre-front-end admission is safe"
        )
    if resources_known_good.count("return false;") < 3 or not compact(
        resources_known_good
    ).endswith("return false; }"):
        raise AssertionError(
            "Vulkan resource truth must end in an unconditional false after its generation checks"
        )

    require_compact(
        header,
        """static const int VK_SHADOW_MAX_POINT_CUBES =
            VK_SHADOW_MAX_LIGHTS * VK_SHADOW_RECEIVER_PASS_COUNT;""",
        "point scratch capacity for every admitted receiver ownership",
    )
    require_compact(
        header,
        "static const int VK_SHADOW_MAX_CACHE_SLOTS = 16;",
        "bounded class-specific static caches",
    )
    for obsolete_gate in (
        "VK_SHADOW_MAX_POINT_LIGHTS",
        "pointLightsUsed",
    ):
        if obsolete_gate in header or obsolete_gate in shadow_map:
            raise AssertionError(
                f"Vulkan point admission must not retain the old arbitrary light gate {obsolete_gate!r}"
            )

    projected_entry = braced_body(
        shadow_map,
        "typedef struct vkProjectedShadowCacheEntry_s",
        "projected static-cache entry",
    )
    require_order(
        projected_entry,
        (
            "bool valid;",
            "bool reserved;",
            "int generation;",
            "const idRenderWorldLocal *renderWorld;",
            "int lightIndex;",
            "vkShadowReceiverPass_t passKind;",
            "int signature;",
            "int tileSize;",
            "int lastUsedFrame;",
            "shadowMapProjectedLightState_t projectedState;",
            "VkImage image;",
            "VkImageLayout layout;",
        ),
        "projected exact resident metadata",
    )
    point_entry = braced_body(
        shadow_map,
        "typedef struct vkPointShadowCacheEntry_s",
        "point static-cache entry",
    )
    require_order(
        point_entry,
        (
            "bool valid;",
            "bool reserved;",
            "int generation;",
            "const idRenderWorldLocal *renderWorld;",
            "int lightIndex;",
            "vkShadowReceiverPass_t passKind;",
            "int signature;",
            "int size;",
            "int lastUsedFrame;",
            "float pointFar;",
            "float lightOrigin[ 3 ];",
            "vkPointShadowCube_t cube;",
        ),
        "point exact resident metadata",
    )

    state = braced_body(
        shadow_map,
        "typedef struct vkShadowMapState_s",
        "Vulkan shadow-map state",
    )
    require_order(
        state,
        (
            "vkPointShadowCube_t pointCubes[ VK_SHADOW_MAX_POINT_CUBES ];",
            "vkProjectedShadowCacheEntry_t projectedCache[ VK_SHADOW_MAX_CACHE_SLOTS ];",
            "vkPointShadowCacheEntry_t pointCache[ VK_SHADOW_MAX_CACHE_SLOTS ];",
            "const idRenderWorldLocal *cacheRenderWorld;",
            "unsigned int cacheMapFileCRC;",
            "int cacheMapNameHash;",
            "int pointCubesUsed;",
            "int freshUpdates;",
        ),
        "separate scratch, projected-resident, and point-resident storage",
    )

    signature = braced_body(
        shadow_map,
        "static int VK_ShadowMap_BuildPassSignatureForView(",
        "exact shadow-cache signature",
    )
    for token, context in (
        ("viewDef != NULL ? viewDef->renderWorld : NULL", "render-world identity"),
        ("viewDef->renderWorld->mapFileCRC", "map-file identity"),
        ("VK_ShadowMap_MapNameHash( viewDef )", "map-name identity"),
        ("VK_ShadowMap_LightIndex( vLight )", "light identity"),
        ("static_cast<int>( passKind )", "ownership identity"),
        ("static_cast<int>( classification.lightClass )", "light-class identity"),
        ("vLight->shadowMapCasterSignature", "caster-content identity"),
        ("resourceSize", "resource-size identity"),
        ("VK_ShadowMap_PointLightFar( vLight )", "point far-envelope identity"),
        ("vLight->lightDef->parms.lightCenter[ i ]", "point center identity"),
        ("vLight->globalLightOrigin[ i ]", "receiver light-origin identity"),
        ("vLight->lightRadius[ i ]", "light-radius identity"),
        ("vLight->lightProject[ planeIndex ][ component ]", "projected-light identity"),
    ):
        require(signature, token, f"exact shadow-cache {context}")

    projected_find = braced_body(
        shadow_map,
        "static int VK_ShadowMap_FindProjectedCacheEntry(",
        "exact projected cache lookup",
    )
    require_order(
        projected_find,
        (
            "entry.valid && !entry.reserved",
            "entry.generation == tr.videoRestartCount",
            "entry.renderWorld == renderWorld",
            "entry.lightIndex == lightIndex",
            "entry.passKind == passKind",
            "entry.signature == signature",
            "entry.tileSize == tileSize",
            "entry.image != VK_NULL_HANDLE",
            "entry.layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL",
            "entry.lastUsedFrame = tr.frameCount;",
            "entry.reserved = true;",
            "return i;",
        ),
        "exact projected cache lookup and hit reservation",
    )
    point_find = braced_body(
        shadow_map,
        "static int VK_ShadowMap_FindPointCacheEntry(",
        "exact point cache lookup",
    )
    require_order(
        point_find,
        (
            "entry.valid && !entry.reserved",
            "entry.generation == tr.videoRestartCount",
            "entry.renderWorld == renderWorld",
            "entry.lightIndex == lightIndex",
            "entry.passKind == passKind",
            "entry.signature == signature",
            "entry.size == size",
            "entry.cube.image != VK_NULL_HANDLE",
            """entry.cube.layout
                == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL""",
            "entry.lastUsedFrame = tr.frameCount;",
            "entry.reserved = true;",
            "return i;",
        ),
        "exact point cache lookup and hit reservation",
    )

    lookup_definitions = [
        line.strip().split("(", 1)[0].split()[-1]
        for line in shadow_map.splitlines()
        if line.startswith("static int VK_ShadowMap_Find") and "Cache" in line
    ]
    if lookup_definitions != [
        "VK_ShadowMap_FindProjectedCacheEntry",
        "VK_ShadowMap_FindPointCacheEntry",
    ]:
        raise AssertionError(
            f"Vulkan cache scheduling must expose only exact class lookups; got {lookup_definitions!r}"
        )
    for stale_identifier in (
        "FindAny",
        "FindStale",
        "AllowStale",
        "allowStale",
        "anySignature",
        "staleSignature",
    ):
        if stale_identifier in shadow_map:
            raise AssertionError(
                f"Vulkan cache scheduling must not contain stale/any-signature path {stale_identifier!r}"
            )

    static_gate = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_StaticCacheable(",
        "opaque static-only cache gate",
    )
    require_compact(
        static_gate,
        """const bool haveDynamicCasters = vLight != NULL
            && ( vLight->shadowMapDynamicCasterCount > 0
                || vLight->globalShadowMapDynamicCasters != NULL
                || vLight->localShadowMapDynamicCasters != NULL );""",
        "dynamic caster cache exclusion",
    )
    for exclusion in (
        "haveDynamicCasters",
        "vLight->shadowMapCasterCount <= 0",
        "vLight->shadowMapStaticCasterCount <= 0",
        "vLight->shadowMapAlphaCasterCount > 0",
        "vLight->shadowMapTranslucentCasterCount > 0",
        "vLight->globalTranslucentShadowMapCasters != NULL",
        "vLight->localTranslucentShadowMapCasters != NULL",
    ):
        require(static_gate, exclusion, f"opaque static-only exclusion {exclusion}")
    require_compact(
        static_gate,
        """if ( !pointLight
            && ( cascadeCount != 1 || atlasDiv != 1 ) ) {
            return false;
        }""",
        "projected CSM cache exclusion",
    )
    require(
        static_gate,
        "r_shadowMapStaticHysteresisFrames.GetInteger()",
        "static-cache dynamic hysteresis",
    )

    projected_limit = braced_body(
        shadow_map,
        "static int VK_ShadowMap_ProjectedCacheSlotLimit(",
        "projected cache-size policy",
    )
    point_limit = braced_body(
        shadow_map,
        "static int VK_ShadowMap_PointCacheSlotLimit(",
        "point cache-size policy",
    )
    require(
        projected_limit,
        "r_shadowMapProjectedCacheSize.GetInteger()",
        "projected cache-size cvar",
    )
    require(
        point_limit,
        "r_shadowMapPointCacheSize.GetInteger()",
        "point cache-size cvar",
    )

    begin_cache_view = braced_body(
        shadow_map,
        "static void VK_ShadowMap_BeginCacheView(",
        "per-view cache invalidation",
    )
    require_order(
        begin_cache_view,
        (
            "renderWorld->mapFileCRC",
            "VK_ShadowMap_MapNameHash( viewDef )",
            "r_shadowMapResidentFrames.GetInteger()",
            "vkShadow.cacheRenderWorld != renderWorld",
            "vkShadow.cacheMapFileCRC != mapFileCRC",
            "vkShadow.cacheMapNameHash != mapNameHash",
            "VK_ShadowMap_ClearProjectedEntryMetadata(",
            "VK_ShadowMap_ClearPointEntryMetadata(",
            "projected.reserved = false;",
            "projected.generation != tr.videoRestartCount",
            "tr.frameCount - projected.lastUsedFrame",
            "point.reserved = false;",
            "point.generation != tr.videoRestartCount",
            "tr.frameCount - point.lastUsedFrame",
        ),
        "map/generation/residency cache invalidation",
    )

    cache_pass_kind = braced_body(
        shadow_map,
        "static vkShadowReceiverPass_t VK_ShadowMap_CachePassKind(",
        "LOCAL/GLOBAL cache canonicalization",
    )
    require_compact(
        cache_pass_kind,
        """vLight->localShadowMapCasters == NULL
            && vLight->localShadowMapDynamicCasters == NULL
            && vLight->localTranslucentShadowMapCasters == NULL""",
        "canonicalization across every local caster class",
    )
    require_order(
        cache_pass_kind,
        (
            "vLight->localTranslucentShadowMapCasters == NULL",
            "return VK_SHADOW_RECEIVER_GLOBAL;",
            "return requestedPass;",
        ),
        "safe LOCAL/GLOBAL canonical identity",
    )
    has_local_casters = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_HasLocalCasters(",
        "LOCAL/GLOBAL runtime resource alias",
    )
    require_compact(
        has_local_casters,
        """return vLight->localShadowMapCasters != NULL
            || vLight->localShadowMapDynamicCasters != NULL
            || vLight->localTranslucentShadowMapCasters != NULL;""",
        "runtime alias check across every local caster class",
    )

    for marker, context, ensure_call, cache_name in (
        (
            "static int VK_ShadowMap_AllocProjectedCacheEntry(",
            "projected cache LRU allocation",
            "VK_ShadowMap_EnsureProjectedCacheImage( selected )",
            "projectedCache",
        ),
        (
            "static int VK_ShadowMap_AllocPointCacheEntry(",
            "point cache LRU allocation",
            "VK_ShadowMap_EnsurePointCacheCube( selected )",
            "pointCache",
        ),
    ):
        allocation = braced_body(shadow_map, marker, context)
        require_order(
            allocation,
            (
                "if ( entry.reserved )",
                "continue;",
                "if ( !entry.valid )",
                "selected = i;",
                "entry.lastUsedFrame",
                f"vkShadow.{cache_name}[ selected ].lastUsedFrame",
                ensure_call,
                f"VK_ShadowMap_Clear{'Projected' if cache_name == 'projectedCache' else 'Point'}EntryMetadata(",
                f"vkShadow.{cache_name}[ selected ].reserved = true;",
                "return selected;",
            ),
            context,
        )

    schedule = braced_body(
        shadow_map,
        "static vkShadowSchedule_t VK_ShadowMap_SchedulePass(",
        "exact cache and fresh-update admission",
    )
    require_compact(
        schedule,
        """const int incompleteStencilMask =
            vLight->shadowMapIncompleteStencilMask |
            ( vLight->shadowMapPrelightStencilRequiredMask
                & ~vLight->shadowMapPrelightStencilReadyMask );
        const bool mapRequiredForCorrectness =
            !stencilFallbackAvailable ||
            ( incompleteStencilMask & receiverMask ) != 0;""",
        "map-only and stencil-less-target correctness override",
    )
    policy2_position = schedule.find("subviewPolicy >= 2")
    cache_gate_position = schedule.find("schedule.cacheable =")
    lookup_position = schedule.find("if ( schedule.cacheable )")
    policy1_position = schedule.find("subviewPolicy >= 1")
    budget_position = schedule.find("const int updateBudget")
    fresh_position = schedule.find("vkShadow.freshUpdates++;")
    cache_alloc_position = schedule.rfind("if ( schedule.cacheable )")
    if not (
        0 <= policy2_position < cache_gate_position <= lookup_position
        < policy1_position < budget_position < fresh_position < cache_alloc_position
    ):
        raise AssertionError(
            "Subview policy 2, exact lookup, policy 1 fallback, budget, and fresh admission are out of order"
        )
    policy2_block = schedule[policy2_position:cache_gate_position]
    exact_lookup_block = schedule[lookup_position:policy1_position]
    policy1_block = schedule[policy1_position:budget_position]
    budget_block = schedule[budget_position:fresh_position]
    for block, context in (
        (policy2_block, "subview policy 2"),
        (policy1_block, "subview policy 1 miss"),
        (budget_block, "fresh-update budget miss"),
    ):
        require(
            block,
            "schedule.action = VK_SHADOW_SCHEDULE_FALLBACK;",
            context,
        )
        require(block, "return schedule;", context)
        require(
            block,
            "!mapRequiredForCorrectness",
            f"{context} correctness override",
        )
    require(
        exact_lookup_block,
        "VK_ShadowMap_FindPointCacheEntry(",
        "point exact lookup before subview policy 1 fallback",
    )
    require(
        exact_lookup_block,
        "VK_ShadowMap_FindProjectedCacheEntry(",
        "projected exact lookup before subview policy 1 fallback",
    )
    require_order(
        exact_lookup_block,
        (
            "if ( schedule.cacheEntry >= 0 )",
            "schedule.action = VK_SHADOW_SCHEDULE_REUSE;",
            "return schedule;",
        ),
        "exact hit admission before subview/budget fallback",
    )
    if schedule.count("VK_ShadowMap_Find") != 2:
        raise AssertionError("Vulkan scheduling must perform exactly the two exact class lookups")
    if schedule.count("vkShadow.freshUpdates++;") != 1:
        raise AssertionError("A fresh ownership map must consume the update budget exactly once")
    if "VK_ShadowMap_MarkStencilFallbackSticky" in schedule:
        raise AssertionError("Subview/budget cache misses must not become sticky")
    if "VK_SHADOW_SCHEDULE_FALLBACK" in schedule[cache_alloc_position:]:
        raise AssertionError("Optional cache-allocation failure must remain a fresh uncached update")

    prepare = braced_body(
        shadow_map,
        "int VK_ShadowMap_PrepareViewLights(",
        "per-view cache admission",
    )
    require_compact(
        prepare,
        """for ( int correctnessPhase = 0 ; correctnessPhase < 2 ;
                correctnessPhase++ ) {
            const bool requiredPhase = correctnessPhase == 0;""",
        "correctness-required light priority",
    )
    require_compact(
        prepare,
        """const bool passRequiresMap[ VK_SHADOW_RECEIVER_PASS_COUNT ] = {
            !stencilFallbackAvailable ||
                ( incompleteStencilMask &
                    SHADOWMAP_RECEIVER_MASK_LOCAL ) != 0,
            !stencilFallbackAvailable ||
                ( incompleteStencilMask &
                    SHADOWMAP_RECEIVER_MASK_GLOBAL ) != 0
        };""",
        "per-ownership stencil availability override",
    )
    require_compact(
        prepare,
        """if ( lightHasRequiredMap &&
                    !passRequiresMap[ passIndex ] ) {
                continue;
            }""",
        "mixed required/optional ownership admission",
    )
    require_order(
        prepare,
        (
            "const bool requiredPhase = correctnessPhase == 0;",
            "const bool lightHasRequiredMap =",
            "if ( lightHasRequiredMap != requiredPhase )",
            "if ( vkShadow.numLights >= VK_SHADOW_MAX_LIGHTS )",
        ),
        "correctness-required maps before bounded capacity admission",
    )
    require_order(
        prepare,
        (
            "VK_ShadowMap_AliasPass( entry, receiverPass, VK_SHADOW_RECEIVER_LOCAL );",
            "VK_ShadowMap_SchedulePass(",
            "VK_ShadowMap_AliasPass( entry, receiverPass, VK_SHADOW_RECEIVER_LOCAL );",
            "VK_ShadowMap_SchedulePass(",
        ),
        "ownership aliases before point/projected budget admission",
    )
    point_section_start = prepare.find("if ( classification.pointLight )")
    projected_section_start = prepare.find("// Match RB_ShadowMapTileSizeForLight")
    if not (0 <= point_section_start < projected_section_start):
        raise AssertionError("Could not isolate point/projected admission branches")
    for section, allocate_call, context in (
        (
            prepare[point_section_start:projected_section_start],
            "VK_ShadowMap_AllocatePointPass(",
            "point admission fallback",
        ),
        (
            prepare[projected_section_start:],
            "VK_ShadowMap_AllocateProjectedPass(",
            "projected admission fallback",
        ),
    ):
        fallback_position = section.find("VK_SHADOW_SCHEDULE_FALLBACK")
        allocation_position = section.find(allocate_call, fallback_position)
        if not (0 <= fallback_position < allocation_position):
            raise AssertionError(f"Could not isolate {context}")
        fallback_block = section[fallback_position:allocation_position]
        require(fallback_block, "continue;", context)
        if "VK_ShadowMap_MarkStencilFallbackSticky" in fallback_block:
            raise AssertionError(f"{context} must retain same-frame stencil without becoming sticky")

    resources = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_EnsureResources(",
        "transfer-capable shadow atlas",
    )
    require_compact(
        resources,
        """ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT;""",
        "projected atlas transfer source/destination usage",
    )
    projected_image = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_EnsureProjectedCacheImage(",
        "transfer-only projected resident image",
    )
    require_compact(
        projected_image,
        """ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT;""",
        "projected resident transfer usage",
    )
    for forbidden_usage in (
        "VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT",
        "VK_IMAGE_USAGE_SAMPLED_BIT",
    ):
        if forbidden_usage in projected_image:
            raise AssertionError(
                f"Projected resident tiles must stay transfer-only, not {forbidden_usage}"
            )
    require_order(
        projected_image,
        (
            "vmaCreateImage(",
            "entry.layout = VK_IMAGE_LAYOUT_UNDEFINED;",
        ),
        "projected resident initial layout tracking",
    )

    image_barrier = braced_body(
        shadow_map,
        "static void VK_ShadowMap_ImageBarrier(",
        "shadow cache image barriers",
    )
    require_order(
        image_barrier,
        (
            "barrier.oldLayout = oldLayout;",
            "barrier.newLayout = newLayout;",
            "barrier.image = image;",
            "barrier.subresourceRange.aspectMask = VK_ShadowMap_DepthAspectMask();",
            "barrier.subresourceRange.layerCount = layerCount;",
            "vkCmdPipelineBarrier2( cmd, &dep );",
        ),
        "tracked depth-image layout transitions",
    )
    copy_depth = braced_body(
        shadow_map,
        "static void VK_ShadowMap_CopyDepthTile(",
        "projected cache depth copy",
    )
    require_order(
        copy_depth,
        (
            "region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;",
            "region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;",
            "copy.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;",
            "copy.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;",
            "vkCmdCopyImage2( cmd, &copy );",
        ),
        "depth-only projected cache copy",
    )

    render = braced_body(
        shadow_map,
        "bool VK_ShadowMap_RenderAtlas(",
        "cached shadow rendering",
    )
    if render.count("VK_ShadowMap_CopyDepthTile(") != 2:
        raise AssertionError(
            "Projected caching must contain exactly one atlas-to-cache and one cache-to-atlas copy path"
        )
    require_order(
        render,
        (
            "if ( haveCacheUpdates )",
            "VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL",
            """VK_ShadowMap_CopyDepthTile( cmd,
                vkShadow.atlasImage,
                pass.tileX, pass.tileY,
                cache.image, 0, 0,
                light.tileSize );""",
            "cache.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;",
            "if ( haveCacheHits )",
            "VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL",
            """VK_ShadowMap_CopyDepthTile( cmd,
                cache.image, 0, 0,
                vkShadow.atlasImage,
                pass.tileX, pass.tileY,
                light.tileSize );""",
            "VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL",
            "vkShadow.atlasLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;",
        ),
        "projected resident publish/reuse transfers and final sampled layout",
    )
    if compact(render).count(compact("|| !cache->reserved")) != 2:
        raise AssertionError(
            "Projected and point exact hits must both revalidate their per-view reservation"
        )

    ensure_point_scratch = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_EnsurePointCube(",
        "point scratch cube allocation",
    )
    ensure_point_resident = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_EnsurePointCacheCube(",
        "point resident cube allocation",
    )
    require_compact(
        ensure_point_scratch,
        "VK_ShadowMap_CreatePointCube( vkShadow.pointCubes[ index ] )",
        "scratch point cube ownership",
    )
    require_compact(
        ensure_point_resident,
        "VK_ShadowMap_CreatePointCube( vkShadow.pointCache[ index ].cube )",
        "identity-resident point cube ownership",
    )

    allocate_point = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_AllocatePointPass(",
        "point resident-hit and scratch allocation",
    )
    require_order(
        allocate_point,
        (
            "if ( schedule.action == VK_SHADOW_SCHEDULE_REUSE )",
            "vkShadow.pointCache[ schedule.cacheEntry ]",
            "cache.cube.sets[ frameSlot ]",
            "pass.cacheHit = true;",
            "light.tileSize = cache.size;",
            "light.pointFar = cache.pointFar;",
            "light.pointLightOrigin[ i ] = cache.lightOrigin[ i ];",
            "if ( schedule.cacheEntry >= 0 )",
            "pass.cacheUpdate = true;",
            "cache.reserved = false;",
            "vkShadow.pointCubesUsed >= VK_SHADOW_MAX_POINT_CUBES",
            "VK_ShadowMap_EnsurePointCube(",
            "vkShadow.pointCubesUsed++;",
        ),
        "resident point hit state restore before fresh cache/scratch fallback",
    )
    require_compact(
        render,
        """if ( projectedCount == 0 && pointFreshCount == 0
            && pointHitCount > 0 ) {
            VK_ShadowMap_FinalizeCachePasses( viewDef );
            return true;
        }""",
        "identity-resident point-hit view without scratch rendering",
    )

    finalize = braced_body(
        shadow_map,
        "static void VK_ShadowMap_FinalizeCachePasses(",
        "post-render cache metadata publication",
    )
    require_order(
        finalize,
        (
            "if ( pass.cacheHit )",
            "vkShadow.pointCache[",
            "pass.cacheEntry ].reserved = false;",
            "vkShadow.projectedCache[",
            "pass.cacheEntry ].reserved = false;",
            "if ( !pass.cacheUpdate )",
            "cache.cube.layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL",
            "cache.valid = true;",
            "cache.reserved = false;",
            "cache.generation = tr.videoRestartCount;",
            "cache.renderWorld = viewDef->renderWorld;",
            "cache.signature = pass.cacheSignature;",
            "cache.size = light.tileSize;",
            "cache.pointFar = light.pointFar;",
            "cache.lightOrigin[ originIndex ]",
            "cache.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL",
            "cache.valid = true;",
            "cache.reserved = false;",
            "cache.generation = tr.videoRestartCount;",
            "cache.renderWorld = viewDef->renderWorld;",
            "cache.signature = pass.cacheSignature;",
            "cache.tileSize = light.tileSize;",
            "cache.projectedState = light.projectedState;",
        ),
        "successful point/projected cache metadata publication",
    )
    if shadow_map.count("cache.valid = true;") != finalize.count(
        "cache.valid = true;"
    ):
        raise AssertionError(
            "Resident cache metadata must only become valid inside the final publication step"
        )

    resume_position = render.rfind("const bool resumedMainRendering")
    failure_position = render.find("if ( !resumedMainRendering )", resume_position)
    abandon_position = render.find("VK_ShadowMap_AbandonPreparedLights();", failure_position)
    success_position = render.find("} else {", abandon_position)
    finalize_position = render.find(
        "VK_ShadowMap_FinalizeCachePasses( viewDef );",
        success_position,
    )
    if not (
        0 <= resume_position < failure_position < abandon_position
        < success_position < finalize_position
    ):
        raise AssertionError(
            "Fresh cache metadata must publish only after main rendering resumes successfully"
        )


def validate_packed_shadow_geometry() -> None:
    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    header_gate = braced_body(
        interactions,
        "static bool VK_Inter_PackedShadowHeaderValid(",
        "packed shadow header validation",
    )
    require_order(
        header_gate,
        (
            "const int64_t headerWords = static_cast<int64_t>( numPrimBatches ) * 2;",
            "const int64_t requiredWords = static_cast<int64_t>( tri->numIndexes ) + headerWords;",
            "if ( requiredWords > tri->numAllocedIndices )",
            "if ( noCaps < 0 || withCaps < 0",
            "( noCaps % 3 ) != 0",
            "( withCaps % 3 ) != 0",
            "noCaps > withCaps",
            "withCaps > tri->numIndexes - total",
            "return total == tri->numIndexes;",
        ),
        "packed shadow header validation",
    )

    packed_draw = braced_body(
        interactions,
        "static bool VK_Inter_DrawPackedShadowSurface(",
        "packed shadow surface drawing",
    )
    require_compact(
        packed_draw,
        "indexCount = drawCaps ? totalIndexCount : noCaps;",
        "packed shadow cap selection",
    )
    require_compact(
        packed_draw,
        """skinPackedVertices
            && !VK_Inter_MD5RSkinShadowPosition( *vertexBuffer, sourceVertex, batch,
                tri, range.transformBase, position )""",
        "packed shadow CPU skinning",
    )
    require_order(
        packed_draw,
        (
            "for ( int batchIndex = 0 ; batchIndex < numBatches ; batchIndex++ )",
            "idTempArray<shadowCache_t> verts(",
            "idTempArray<glIndex_t> indexes(",
            "VK_Exec_BindRawShadowGeometry(",
            "bool drewAnything = false;",
            "vkCmdDrawIndexed(",
        ),
        "prevalidated packed shadow upload and draw",
    )

    stencil_pass = braced_body(
        interactions,
        "static bool VK_StencilShadowPass(",
        "stencil shadow volume rendering",
    )
    require_order(
        stencil_pass,
        (
            "const bool packedPrimBatches = R_TriHasPrimBatchMesh( tri );",
            "if ( !packedPrimBatches",
            "VK_Exec_BindShadowGeometry( cmd, interPass.slot, tri )",
            "if ( packedPrimBatches )",
            """VK_Inter_DrawPackedShadowSurface( surf, packedCapInclusive, external,
                frontSidedFace, backSidedFace )""",
            "interPass.volumeSkipCount++;",
            "continue;",
        ),
        "packed/classic stencil geometry dispatch",
    )

    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    raw_bind = braced_body(
        executor,
        "bool VK_Exec_BindRawShadowGeometry(",
        "transient packed shadow geometry binding",
    )
    require_order(
        raw_bind,
        (
            "verts == NULL || indexes == NULL || numVerts <= 0 || numIndexes <= 0",
            "VK_Ring_Alloc( vkExec.vertexRings[ slot ], verts,",
            "if ( vertexOffset < 0 )",
            "VK_Ring_Alloc( vkExec.indexRings[ slot ], indexes,",
            "if ( indexOffset < 0 )",
            "vkCmdBindVertexBuffers(",
            "vkCmdBindIndexBuffer(",
        ),
        "transient packed shadow geometry binding",
    )


def validate_fail_closed_target_and_stencil_behavior() -> None:
    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    target_has_stencil = braced_body(
        executor,
        "bool VK_Exec_ActiveTargetHasStencil(",
        "active render-target stencil query",
    )
    require_compact(
        target_has_stencil,
        """return vkExec.frameOpen
            && vkExec.activeDepthAttachmentView != VK_NULL_HANDLE
            && vkExec.activePipelineTarget.stencilFormat != VK_FORMAT_UNDEFINED;""",
        "active render-target stencil query",
    )

    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    draw_lights = braced_body(
        interactions,
        "void VK_Interactions_DrawLights(",
        "Vulkan light interactions",
    )
    require_order(
        draw_lights,
        (
            "const bool activeTargetHasStencil = VK_Exec_ActiveTargetHasStencil();",
            """interPass.pipelineStencilShadow = activeTargetHasStencil
                ? VK_Exec_StencilShadowPipeline() : VK_NULL_HANDLE;""",
            """const int incompleteMapMask =
                vLight->shadowMapIncompleteMapMask
                | vLight->shadowMapPrelightMapMissingMask;""",
            """const int incompleteStencilMask =
                vLight->shadowMapIncompleteStencilMask
                | ( vLight->shadowMapPrelightStencilRequiredMask
                    & ~vLight->shadowMapPrelightStencilReadyMask );""",
            "const bool localReceiverNeedsFallback =",
            "const bool globalOpaqueReceiverNeedsFallback =",
            "const bool translucentReceiverNeedsFallback =",
            "const bool globalReceiverNeedsFallback =",
            "const bool missingRequiredShadow =",
            "const bool localStencilOwnershipComplete =",
            "const bool globalStencilOwnershipComplete =",
            "const bool localEmptyFallback =",
            "const bool globalEmptyFallback =",
            "const bool localStencilFallback =",
            "const bool globalStencilFallback =",
            "const bool anyStencilFallback =",
            "if ( missingRequiredShadow && unresolvedBeforeSubmit )",
            "if ( anyStencilFallback )",
            """const drawSurf_t *localGlobalVolumes =
                    localMapNeedsSupplement
                        ? vLight->globalShadowMapStencilSupplements
                        : vLight->globalShadows;""",
            "VK_StencilShadowPass( localGlobalVolumes );",
            "VK_DrawInteractionChain( vLight->localInteractions );",
            """const drawSurf_t *globalGlobalVolumes =
                    ( globalOpaqueMapNeedsSupplement ||
                        translucentMapNeedsSupplement )
                        ? vLight->globalShadowMapStencilSupplements
                        : vLight->globalShadows;""",
            """const drawSurf_t *globalLocalVolumes =
                    ( globalOpaqueMapNeedsSupplement ||
                        translucentMapNeedsSupplement )
                        ? vLight->localShadowMapStencilSupplements
                        : vLight->localShadows;""",
            "VK_StencilShadowPass( globalGlobalVolumes );",
            "VK_StencilShadowPass( globalLocalVolumes );",
            "VK_DrawInteractionChain( vLight->globalInteractions );",
            "const bool translucentUsesStencilFallback =",
            "const bool translucentUsesEmptyFallback =",
            "const bool drawTranslucentReceiver =",
            "if ( drawTranslucentReceiver )",
            "VK_DrawInteractionChain( vLight->translucentInteractions );",
        ),
        "fail-closed receiver and stencil fallback gates",
    )
    for forbidden in ("requiredStencilMask", "stencilShadowLight"):
        if forbidden in draw_lights:
            raise AssertionError(
                f"Vulkan fallback must remain per receiver ownership, not whole-light state: {forbidden}"
            )
    for snippet, label in (
        (
            """const bool stencilResourcesReady =
                activeTargetHasStencil &&
                interPass.pipelineStencilShadow != VK_NULL_HANDLE;""",
            "stencil target and pipeline readiness",
        ),
        (
            """const bool localEmptyFallback =
                localReceiverNeedsFallback &&
                localStencilOwnershipComplete &&
                vLight->globalShadows == NULL;""",
            "LOCAL complete-empty fallback",
        ),
        (
            """const bool globalEmptyFallback =
                globalReceiverNeedsFallback &&
                globalStencilOwnershipComplete &&
                vLight->globalShadows == NULL &&
                vLight->localShadows == NULL;""",
            "GLOBAL complete-empty fallback",
        ),
        (
            """const bool localStencilFallback =
                localReceiverNeedsStencil &&
                !localEmptyFallback &&
                localStencilOwnershipComplete &&
                stencilResourcesReady;""",
            "LOCAL nonempty stencil fallback",
        ),
        (
            """const bool globalStencilFallback =
                globalReceiverNeedsStencil &&
                !globalEmptyFallback &&
                globalStencilOwnershipComplete &&
                stencilResourcesReady;""",
            "GLOBAL nonempty stencil fallback",
        ),
        (
            """globalStencilPassComplete =
                globalVolumePassComplete &&
                localVolumePassComplete;""",
            "GLOBAL two-pass runtime completion",
        ),
        (
            """const bool runtimeMissingRequiredShadow =
                ( localStencilFallback &&
                    !localReceiverDrewWithStencil )
                || ( globalStencilFallback &&
                    !globalStencilPassComplete );""",
            "late stencil submission fail-closed gate",
        ),
    ):
        require_compact(draw_lights, snippet, label)
    require(
        draw_lights,
        "stencil shadow volume submission incomplete; affected light receivers are skipped fail-closed",
        "late stencil submission diagnostic",
    )
    expected_draw_counts = {
        "VK_DrawInteractionChain( vLight->localInteractions );": 3,
        "VK_DrawInteractionChain( vLight->globalInteractions );": 3,
        "VK_DrawInteractionChain( vLight->translucentInteractions );": 1,
    }
    for call, expected_count in expected_draw_counts.items():
        actual_count = draw_lights.count(call)
        if actual_count != expected_count:
            raise AssertionError(
                f"Per-ownership fallback draw cardinality changed for {call!r}: "
                f"{actual_count} != {expected_count}"
            )
    require_compact(
        draw_lights,
        """const bool translucentReceiverNeedsShadow = shadowingEnabled
            && vLight->translucentInteractions != NULL
            && ( hasGlobalCasters || hasLocalCasters
                || ( incompleteMapMask
                    & SHADOWMAP_RECEIVER_MASK_GLOBAL ) != 0 )
            && ( globalShadowState != NULL
                ? r_shadowMapTranslucentReceivers.GetBool()
                : r_stencilTranslucentShadows.GetBool() );""",
        "GLOBAL-owned translucent fallback selection",
    )
    require(
        draw_lights,
        "required shadow resource unavailable; affected light receivers are skipped fail-closed",
        "fail-closed shadow diagnostic",
    )

    shared = read("src/renderer/tr_local.h")
    for token in (
        "SHADOWMAP_RECEIVER_MASK_LOCAL = 1 << 0",
        "SHADOWMAP_RECEIVER_MASK_GLOBAL = 1 << 1",
        "shadowMapIncompleteMapMask",
        "shadowMapIncompleteStencilMask",
        "shadowMapHybridIncompleteMask",
        "globalShadowMapStencilSupplements",
        "localShadowMapStencilSupplements",
        "shadowMapPrelightMapMissingMask",
        "shadowMapPrelightStencilRequiredMask",
        "shadowMapPrelightStencilReadyMask",
    ):
        require(shared, token, "shared ownership-completeness state")

    interaction_header = read("src/renderer/Interaction.h")
    require_order(
        interaction_header,
        (
            "bool shadowStencilEligible;",
            "bool shadowStencilUsesPrelight;",
        ),
        "cached stencil-fallback provenance",
    )

    frontend = read("src/renderer/Interaction.cpp")
    moments_support = braced_body(
        frontend,
        "static bool R_TranslucentShadowMapMomentsSupportedForLight(",
        "translucent shadow moment backend gate",
    )
    require_order(
        moments_support,
        (
            'cvarSystem->GetCVarString( "r_actualRenderApi" )',
            'idStr::Icmp( activeRenderApi, "vulkan" ) == 0',
            "return false;",
            "r_shadowMapTranslucentMoments.GetBool()",
        ),
        "explicit Vulkan translucent-moment rejection",
    )
    require_order(
        frontend,
        (
            "sint->shadowStencilEligible =",
            "sint->shadowStencilUsesPrelight =",
            "bool admittedShadowMapCaster = false;",
            "bool linkedShadowMapCaster = false;",
            """admittedShadowMapCaster =
                allowShadowMapCaster ||
                allowTranslucentShadowMapCaster;""",
            "linkedShadowMapCaster = true;",
            "vLight->shadowMapPrelightStencilRequiredMask |=",
            "vLight->shadowMapPrelightMapMissingMask |=",
            "} else if ( mapMissingCasterNeedsStencil )",
            "vLight->shadowMapIncompleteMapMask |=",
            "vLight->shadowMapIncompleteStencilMask |=",
            "mapMissingNeedsVisibleVolume = true;",
            "R_CullLocalBox(",
            "if ( mapMissingNeedsVisibleVolume )",
            "R_EnsureInteractionShadowCache(",
            "localShadowMapStencilSupplements",
            "globalShadowMapStencilSupplements",
        ),
        "front-end map/stencil ownership completeness",
    )
    require_compact(
        frontend,
        """const bool mapMissingCasterNeedsStencil =
            shadowMapCasterPolicyActive &&
            !sint->shadowStencilUsesPrelight &&
            !linkedShadowMapCaster &&
            ( admittedShadowMapCaster ||
                shadowTris != NULL ) &&
            ( !shadowMapCasterOnly ||
                admittedShadowMapCaster );""",
        "actual-caster map completeness provenance",
    )
    require_compact(
        frontend,
        """const bool forcePointEmitterStencilGeneration =
            pointMapPolicyActive &&
            sint->shadowStencilEligible &&
            R_ShouldSkipPointLightEmitterCaster( shader, tri,
                shadowMapLocalLightOrigin, lightDef->parms.lightRadius );""",
        "point-emitter stencil generation probe",
    )
    require_compact(
        frontend,
        """const bool suppressDynamicShadowVolume =
            surfaceCanCastStencilShadowVolume && shadowLODAdmitted &&
            model->IsDynamicModel() != DM_STATIC &&
            !forcePointEmitterStencilGeneration &&
            R_ShadowMapLightWillUseShadowMaps( lightDef );""",
        "point-emitter volume probe before dynamic elision",
    )
    per_surface_volume_gate = braced_body(
        frontend,
        "bool R_VulkanShadowMapsNeedPerSurfaceStencilVolumes(",
        "Vulkan mapped per-surface stencil-volume policy",
    )
    require_order(
        per_surface_volume_gate,
        (
            "!r_shadows.GetBool()",
            "!r_useShadowMap.GetBool()",
            "lightDef->parms.pointLight",
            "!r_shadowMapPointLights.GetBool()",
            'cvarSystem->GetCVarString( "r_actualRenderApi" )',
            'idStr::Icmp( activeRenderApi, "vulkan" ) == 0',
        ),
        "Vulkan mapped per-surface stencil-volume policy",
    )
    require_compact(
        frontend,
        """sint->shadowStencilUsesPrelight =
            sint->shadowStencilEligible &&
            R_LightHasRealPrelightModel( lightDef->parms ) &&
            model->IsStaticWorldModel() &&
            r_useOptimizedShadows.GetBool() &&
            !R_VulkanShadowMapsNeedPerSurfaceStencilVolumes( lightDef );""",
        "mapped Vulkan optimized-prelight split",
    )
    render_system = read("src/renderer/RenderSystem.cpp")
    require_order(
        render_system,
        (
            "if ( r_shadows.IsModified()",
            "|| r_useShadowMap.IsModified()",
            "|| r_useOptimizedShadows.IsModified()",
            "|| r_lod_shadows_percent.IsModified()",
            "|| r_shadowMapPointLights.IsModified()",
            "|| r_shadowMapConservativeCasters.IsModified()",
            "r_shadows.ClearModified();",
            "r_useShadowMap.ClearModified();",
            "r_useOptimizedShadows.ClearModified();",
            "r_lod_shadows_percent.ClearModified();",
            "r_shadowMapPointLights.ClearModified();",
            "r_shadowMapConservativeCasters.ClearModified();",
            "primaryWorld->FreeInteractions();",
        ),
        "live shadow representation rebuild",
    )
    require_compact(
        frontend,
        """const bool volumeElidedForShadowMaps =
            shadowTris != NULL &&
            !shadowMapCasterOnly &&
            R_ShadowMapLightWillUseShadowMaps( lightDef );""",
        "caster-only fallback-volume retention",
    )
    require_compact(
        frontend,
        """if ( !shadowMapCasterOnly &&
                r_useShadowCulling.GetBool() &&
                !R_ShouldDisableEntityCullingForLevelshot() &&
                !shadowTris->bounds.IsCleared() )""",
        "caster-only conservative full-light volume culling",
    )
    require_order(
        frontend,
        (
            "const bool linkedFullShadowVolume = R_LinkLightSurf(",
            "!linkedFullShadowVolume )",
            "const bool linkedStencilSupplement = R_LinkLightSurf(",
            "if ( !linkedStencilSupplement )",
            "vLight->shadowMapHybridIncompleteMask |=",
        ),
        "successful full and supplement volume linking",
    )

    prelight_source = read("src/renderer/tr_light.cpp")
    prelight_start = prelight_source.find(
        "static void R_AddOptimizedPrelightShadows("
    )
    prelight_end = prelight_source.find(
        "\n/*\n=================\nR_AddLightSurfaces", prelight_start
    )
    if prelight_start < 0 or prelight_end < 0:
        raise AssertionError(
            "Could not isolate optimized-prelight completeness resolution"
        )
    prelight = prelight_source[prelight_start:prelight_end]
    if "ShadowMapStencilSupplements" in prelight:
        raise AssertionError(
            "Combined optimized prelight volumes must remain full-fallback-only"
        )
    require_order(
        prelight,
        (
            "R_VulkanShadowMapsNeedPerSurfaceStencilVolumes(",
            "return;",
            "idRenderModel *prelightModel = R_ViewLightPrelightModel( vLight );",
        ),
        "mapped Vulkan combined-prelight bypass",
    )
    require_order(
        prelight,
        (
            "vLight->shadowMapPrelightStencilRequiredMask = 0;",
            "vLight->shadowMapPrelightMapMissingMask = 0;",
            "const bool linkedPrelightVolume = R_LinkLightSurf(",
            "vLight->shadowMapIncompleteMapMask |=",
            "if ( linkedPrelightVolume )",
            "vLight->shadowMapPrelightStencilReadyMask |=",
        ),
        "optimized-prelight completeness resolution",
    )
    require_compact(
        prelight,
        """const int prelightStencilOwnershipMask =
            protectStaticWorldNoSelfReceivers
                ? SHADOWMAP_RECEIVER_MASK_GLOBAL
                : ( SHADOWMAP_RECEIVER_MASK_LOCAL |
                    SHADOWMAP_RECEIVER_MASK_GLOBAL );""",
        "optimized-prelight receiver ownership routing",
    )
    require_compact(
        prelight,
        """vLight->shadowMapPrelightStencilReadyMask |=
            vLight->shadowMapPrelightStencilRequiredMask &
            prelightStencilOwnershipMask;""",
        "optimized-prelight ownership-specific readiness",
    )

    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    map_complete = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_MapOrHybridOwnershipComplete(",
        "mapped or hybrid ownership completeness gate",
    )
    require_order(
        map_complete,
        (
            "VK_ShadowMap_ReceiverMask( receiverPass )",
            "vLight->shadowMapIncompleteMapMask",
            "vLight->shadowMapPrelightMapMissingMask",
            "if ( ( incompleteMapMask & receiverMask ) == 0 )",
            "vLight->shadowMapHybridIncompleteMask",
            "vLight->shadowMapPrelightMapMissingMask",
            "vLight->globalShadowMapStencilSupplements != NULL",
            "vLight->localShadowMapStencilSupplements != NULL",
        ),
        "mapped or hybrid ownership completeness gate",
    )
    if shadow_map.count("!VK_ShadowMap_MapOrHybridOwnershipComplete(") != 2:
        raise AssertionError(
            "Point and projected receiver scheduling must both gate incomplete map/hybrid ownership"
        )

    abandon = braced_body(
        shadow_map,
        "void VK_ShadowMap_AbandonPreparedLights(",
        "prepared-shadow abandonment",
    )
    require_order(
        abandon,
        (
            "VK_ShadowMap_MarkStencilFallbackSticky( vkShadow.lights[ i ].vLight );",
            "vkShadow.lights[ i ].passes[ passIndex ].valid = false;",
            "vkShadow.lights[ i ].valid = false;",
        ),
        "prepared-shadow abandonment",
    )

    render = braced_body(
        shadow_map,
        "bool VK_ShadowMap_RenderAtlas(",
        "shadow-map caster rendering",
    )
    require_order(
        render,
        (
            "const bool resumedMainRendering = VK_Exec_BeginMainRendering( false );",
            "if ( !resumedMainRendering )",
            "VK_ShadowMap_AbandonPreparedLights();",
            "return resumedMainRendering;",
        ),
        "main-rendering resume failure",
    )


def validate_ci_registration() -> None:
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")

    if validator.count("renderer_vulkan_shadow_compatibility.py") != 1:
        raise AssertionError(
            "Local validation runner must register the Vulkan shadow compatibility test exactly once"
        )

    for workflow, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        if workflow.count(TEST_PATH) != 2:
            raise AssertionError(f"{context} must compile and directly run {TEST_PATH}")
        require(workflow, f"python {TEST_PATH}", context)


def main() -> None:
    validate_runtime_failure_gates()
    validate_receiver_ownership_split()
    validate_shadow_depth_format_selection()
    validate_csm_atlas_and_receiver_contract()
    validate_shadow_descriptor_abi()
    validate_shadow_filtering_contract()
    validate_exact_static_cache_and_admission_contract()
    validate_packed_shadow_geometry()
    validate_fail_closed_target_and_stencil_behavior()
    validate_ci_registration()
    print("renderer_vulkan_shadow_compatibility: ok")


if __name__ == "__main__":
    main()
