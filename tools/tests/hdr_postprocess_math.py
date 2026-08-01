#!/usr/bin/env python3
import math
from pathlib import Path
import sys


LUMA = (0.2126, 0.7152, 0.0722)
BLOOM_SATURATION_WEIGHT = 0.25


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def clamp(value, low, high):
    return max(low, min(high, value))


def smoothstep(edge0, edge1, value):
    if edge1 <= edge0:
        return 1.0 if value >= edge1 else 0.0
    t = clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def weapon_wheel_circle_of_confusion(view_distance, focus_distance, blur_range, blur_strength):
    blur_factor = clamp(abs(view_distance - focus_distance) / blur_range, 0.0, 1.0)
    return smoothstep(0.0, 1.0, blur_factor) * blur_strength


def weapon_wheel_depth_edge_weight(center_distance, sample_distance):
    depth_delta = abs(sample_distance - center_distance)
    depth_tolerance = max(3.0, min(center_distance, sample_distance) * 0.04)
    same_surface = 1.0 - smoothstep(depth_tolerance, depth_tolerance * 4.0, depth_delta)
    return 0.02 + 0.98 * same_surface


def mix(a, b, t):
    return tuple(x * (1.0 - t) + y * t for x, y in zip(a, b))


def max_channel(color):
    return max(color[0], color[1], color[2])


def scene_referred_bloom_color(color):
    return tuple(max(channel, 0.0) for channel in color)


def scene_referred_hdr_color(color):
    return tuple(max(channel, 0.0) for channel in color)


def bloom_brightness(color):
    color = scene_referred_bloom_color(color)
    luminance = dot(color, LUMA)
    peak = max_channel(color)
    return luminance * (1.0 - BLOOM_SATURATION_WEIGHT) + peak * BLOOM_SATURATION_WEIGHT


def bloom_contribution(color, threshold, soft_knee):
    brightness = bloom_brightness(color)
    if brightness <= 1.0e-4:
        return 0.0

    if threshold <= 1.0e-4:
        return 1.0

    knee = max(threshold * soft_knee, 0.0)
    soft = 0.0
    if knee > 1.0e-4:
        soft = brightness - threshold + knee
        soft = clamp(soft, 0.0, 2.0 * knee)
        soft = (soft * soft) / max(4.0 * knee, 1.0e-4)

    hard = max(brightness - threshold, 0.0)
    contribution = max(hard, soft)
    return contribution / brightness


def extract_bloom(color, threshold, soft_knee):
    color = scene_referred_bloom_color(color)
    contribution = bloom_contribution(color, threshold, soft_knee)
    return tuple(channel * contribution for channel in color)


def aces_film_scalar(value):
    a = 2.51
    b = 0.03
    c = 2.43
    d = 0.59
    e = 0.14
    return (value * (a * value + b)) / (value * (c * value + d) + e)


def highlight_compress(color, highlight_desaturation, gamut_compression):
    luma = dot(color, LUMA)
    peak = max_channel(color)
    highlight = smoothstep(0.98, 1.0, peak)
    color = mix(color, (luma, luma, luma), clamp(highlight * highlight_desaturation, 0.0, 1.0))

    peak = max_channel(color)
    if peak > 1.0 and gamut_compression > 0.0:
        compressed_peak = 1.0 + (peak - 1.0) / (1.0 + gamut_compression * (peak - 1.0))
        scale = compressed_peak / peak
        color = tuple(channel * scale for channel in color)

    return color


def tone_map_hdr(color, exposure, white_point, highlight_desaturation, gamut_compression):
    color = scene_referred_hdr_color(color)
    safe_exposure = max(exposure, 1.0e-3)
    exposed = tuple(channel * safe_exposure for channel in color)
    safe_white = max(white_point, 1.0)
    shoulder_start = 0.98
    exposed_white = max(safe_white * safe_exposure, shoulder_start + 1.0e-3)
    shoulder_range = max(exposed_white - shoulder_start, 1.0e-3)
    shoulder_norm = max(1.0 - math.exp(-4.0), 1.0e-4)
    mapped = []
    for channel in exposed:
        if channel < shoulder_start:
            mapped.append(channel)
            continue
        shoulder_t = max(channel - shoulder_start, 0.0) / shoulder_range
        shoulder_value = shoulder_start + (1.0 - shoulder_start) * (1.0 - math.exp(-shoulder_t * 4.0)) / shoulder_norm
        mapped.append(shoulder_value)
    compressed = highlight_compress(tuple(mapped), highlight_desaturation, gamut_compression)
    return tuple(clamp(channel, 0.0, 1.0) for channel in compressed)


def hdr_log_luminance_sample(color):
    color = scene_referred_hdr_color(color)
    luminance = dot(color, LUMA)
    return math.log(max(luminance, 1.0e-4))


def apply_lift_gamma_gain(color, lift, post_gamma, gain):
    safe_gamma = max(post_gamma, 1.0e-3)
    lifted = tuple(max(channel + lift, 0.0) for channel in color)
    corrected = tuple(math.pow(channel, 1.0 / safe_gamma) for channel in lifted)
    return tuple(channel * gain for channel in corrected)


def apply_display_color_mapping(color, brightness, gamma):
    safe_gamma = max(gamma, 1.0e-3)
    brightened = tuple(clamp(channel * brightness, 0.0, 1.0) for channel in color)
    return tuple(math.pow(channel, 1.0 / safe_gamma) for channel in brightened)


def legacy_gamma_table_value(channel, brightness, gamma):
    j = min(int(channel * 255.0) * brightness, 255.0)
    if gamma == 1.0:
        return ((int(j) << 8) | int(j)) / 65535.0
    return clamp((65535.0 * math.pow(j / 255.0, 1.0 / gamma) + 0.5) / 65535.0, 0.0, 1.0)


def apply_vibrance(color, vibrance):
    luma = dot(color, LUMA)
    saturation = max_channel(color) - min(color[0], color[1], color[2])
    vibrance_mix = clamp(1.0 + vibrance * (1.0 - saturation), 0.0, 2.0)
    return mix((luma, luma, luma), color, vibrance_mix)


def apply_color_adjustments(color, lift, post_gamma, gain, vibrance, saturation, contrast):
    color = apply_lift_gamma_gain(color, lift, post_gamma, gain)
    color = apply_vibrance(color, vibrance)
    luma = dot(color, LUMA)
    color = mix((luma, luma, luma), color, saturation)
    color = tuple((channel - 0.5) * contrast + 0.5 for channel in color)
    return tuple(clamp(channel, 0.0, 1.0) for channel in color)


def assert_true(condition, message):
    if not condition:
        raise AssertionError(message)


def cxx_function_body(source, signature):
    start = source.find(signature)
    assert_true(start >= 0, f"{signature} should exist")

    open_brace = source.find("{", start)
    assert_true(open_brace >= 0, f"{signature} should have a function body")

    depth = 0
    for index in range(open_brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1:index]

    raise AssertionError(f"{signature} should have a closed function body")


def test_bloom_contribution_monotonic():
    previous = -1.0
    for step in range(0, 200):
        value = step / 16.0
        contribution = bloom_contribution((value, value, value), 1.0, 0.25)
        assert_true(contribution >= previous - 1.0e-6, "bloom contribution must be monotonic")
        previous = contribution


def test_bloom_threshold_is_soft():
    contribution = bloom_contribution((1.0, 1.0, 1.0), 1.0, 0.25)
    assert_true(contribution > 0.0, "soft knee should start contributing at threshold")
    assert_true(contribution < 0.1, "threshold hit should not keep the full source color")


def test_bloom_extract_keeps_only_excess_energy():
    color = (1.25, 1.25, 1.25)
    extracted = extract_bloom(color, 1.0, 0.25)
    assert_true(extracted[0] > 0.0, "above-threshold color should still contribute to bloom")
    assert_true(extracted[0] < color[0], "bloom should keep only energy above threshold")


def test_bloom_extract_preserves_saturated_hdr_highlights():
    red_hdr = extract_bloom((2.0, 0.0, 0.0), 0.7, 0.15)
    blue_hdr = extract_bloom((0.0, 0.0, 4.0), 0.7, 0.15)
    red_ldr = extract_bloom((1.0, 0.0, 0.0), 0.7, 0.15)

    assert_true(red_hdr[0] > 0.0, "saturated red HDR highlights should survive the bloom bright-pass")
    assert_true(blue_hdr[2] > 0.0, "saturated blue HDR highlights should survive the bloom bright-pass")
    assert_true(max_channel(red_ldr) == 0.0, "ordinary saturated LDR color should remain below the default bloom threshold")


def test_bloom_extract_discards_negative_energy():
    extracted = extract_bloom((-4.0, 2.0, -1.0), 0.7, 0.15)
    assert_true(extracted[0] == 0.0 and extracted[2] == 0.0, "bloom should not carry negative scene energy")
    assert_true(extracted[1] >= 0.0, "positive scene energy should remain non-negative")


def test_zero_threshold_extracts_full_color():
    color = (0.8, 0.4, 0.2)
    extracted = extract_bloom(color, 0.0, 0.15)
    for extracted_channel, source_channel in zip(extracted, color):
        assert_true(abs(extracted_channel - source_channel) < 1.0e-6, "zero threshold should keep the full color")


def test_tone_map_monotonic():
    previous = -1.0
    for step in range(0, 1024):
        value = step / 32.0
        mapped = tone_map_hdr((value, value, value), 1.0, 6.0, 0.35, 1.0)[0]
        assert_true(mapped >= previous - 1.0e-6, "tone map must be monotonic")
        previous = mapped


def test_white_point_near_one():
    mapped = tone_map_hdr((6.0, 6.0, 6.0), 1.0, 6.0, 0.35, 1.0)[0]
    assert_true(abs(mapped - 1.0) < 0.05, "white point should map close to display white")


def test_tone_map_preserves_midrange_sdr():
    color = (0.5, 0.5, 0.5)
    mapped = tone_map_hdr(color, 1.0, 6.0, 0.35, 1.0)
    for mapped_channel, source_channel in zip(mapped, color):
        assert_true(abs(mapped_channel - source_channel) < 0.02, "HDR tonemap should preserve midrange SDR contrast before the shoulder")


def test_tone_map_preserves_ldr_authored_highlights():
    colors = [
        (1.0, 1.0, 1.0),
        (0.85, 1.0, 1.0),
        (1.0, 0.7, 0.35),
    ]
    for color in colors:
        mapped = tone_map_hdr(color, 1.0, 6.0, 0.35, 1.0)
        for mapped_channel, source_channel in zip(mapped, color):
            assert_true(abs(mapped_channel - source_channel) < 0.03, "HDR tonemap should not dull stock LDR-authored additive highlights")


def test_hdr_rejects_negative_scene_energy():
    mapped = tone_map_hdr((-8.0, 0.5, -0.25), 1.0, 6.0, 0.35, 1.0)
    assert_true(mapped[0] == 0.0 and mapped[2] == 0.0, "negative HDR color must not create tone-mapped output")
    assert_true(mapped[1] > 0.0, "positive HDR color should survive negative-channel cleanup")

    dark_log_luma = hdr_log_luminance_sample((-4.0, -2.0, -1.0))
    assert_true(abs(dark_log_luma - math.log(1.0e-4)) < 1.0e-6, "auto exposure luminance should ignore negative scene energy")


def test_no_nan_edge_cases():
    cases = [
        ((0.0, 0.0, 0.0), 0.0, 1.0, 0.0, 0.0),
        ((32.0, 0.1, 0.1), 0.01, 16.0, 1.0, 4.0),
        ((0.001, 0.002, 0.003), 8.0, 1.0, 0.0, 0.0),
    ]
    for color, exposure, white_point, desat, compression in cases:
        mapped = tone_map_hdr(color, exposure, white_point, desat, compression)
        adjusted = apply_color_adjustments(mapped, -0.25, 2.5, 2.0, 1.0, 2.0, 3.0)
        for channel in adjusted:
            assert_true(math.isfinite(channel), "color adjustment produced a non-finite value")


def test_modern_lighting_keeps_scene_referred_energy():
    shader_library = Path(__file__).resolve().parents[2] / "src" / "renderer" / "ModernGLShaderLibrary.cpp"
    source = shader_library.read_text(encoding="utf-8")

    blocked_lighting_clamps = [
        "out_Color = vec4(clamp(lit, vec3(0.0), vec3(1.0))",
        "out_Color = vec4(clamp(mix(baseColor, blendColor, blendAmount) + lightAccum + emissive, vec3(0.0), vec3(1.0))",
    ]
    for snippet in blocked_lighting_clamps:
        assert_true(snippet not in source, "modern lighting must preserve HDR energy for bloom/tonemap")

    assert_true("ModernSceneReferredColor" in source, "modern shaders should route final scene color through the HDR-safe helper")


def test_bloom_shader_uses_saturation_aware_brightness():
    root = Path(__file__).resolve().parents[2]
    shader = (root / "content" / "basepr" / "pak0" / "glprogs" / "bloom_extract.fs").read_text(encoding="utf-8")
    draw_common = (root / "src" / "renderer" / "draw_common.cpp").read_text(encoding="utf-8")

    assert_true("BloomBrightness" in shader, "bloom extraction should use an explicit brightness helper")
    assert_true("mix( luminance, peak, 0.25 )" in shader, "bloom extraction should preserve saturated HDR highlights")
    assert_true("SceneReferredBloomColor" in shader, "bloom extraction should reject negative scene energy")
    assert_true("r_bloomIntensity.GetFloat() > 0.0001f" in draw_common, "zero-intensity bloom should not force the bloom pass")


def test_hdr_shader_uses_scene_referred_inputs():
    root = Path(__file__).resolve().parents[2]
    composite = (root / "content" / "basepr" / "pak0" / "glprogs" / "bloom.fs").read_text(encoding="utf-8")
    luminance = (root / "content" / "basepr" / "pak0" / "glprogs" / "hdr_luminance.fs").read_text(encoding="utf-8")
    draw_common = (root / "src" / "renderer" / "draw_common.cpp").read_text(encoding="utf-8")

    assert_true("SceneReferredHDRColor" in composite, "HDR composite should sanitize scene-referred color before tonemap/debug")
    assert_true("sampleColor = max( sampleColor, vec3( 0.0 ) );" in luminance, "auto exposure should ignore negative scene energy")
    assert_true("r_hdrAutoExposure.GetBool() && r_hdrToneMap.GetBool()" in draw_common, "auto exposure should not request HDR work when tonemap is off")


def test_weapon_wheel_dof_quality_contract():
    root = Path(__file__).resolve().parents[2]
    shader = (root / "content" / "basepr" / "pak0" / "glprogs" / "blur.fs").read_text(encoding="utf-8")

    previous = -1.0
    for step in range(65):
        coc = weapon_wheel_circle_of_confusion(16.0 + step, 16.0, 64.0, 1.0)
        assert_true(coc >= previous - 1.0e-6, "weapon-wheel circle of confusion must grow monotonically away from focus")
        assert_true(0.0 <= coc <= 1.0, "weapon-wheel circle of confusion must remain normalized")
        previous = coc

    half_strength = weapon_wheel_circle_of_confusion(80.0, 16.0, 64.0, 0.5)
    assert_true(abs(half_strength - 0.5) < 1.0e-6, "weapon-wheel blend must scale the maximum circle of confusion")
    assert_true(abs(weapon_wheel_depth_edge_weight(128.0, 128.0) - 1.0) < 1.0e-6, "same-surface DoF taps should keep full weight")
    assert_true(weapon_wheel_depth_edge_weight(16.0, 128.0) <= 0.021, "large depth discontinuities should reject nearly all cross-surface color")

    assert_true("GatherDepthAwareSample" in shader, "weapon-wheel DoF should use depth-aware color gathering")
    assert_true(shader.count("GatherDepthAwareSample( uv") == 18, "weapon-wheel DoF should retain the balanced 18-tap circular kernel")
    assert_true("24.0 * blurAmount" in shader, "weapon-wheel DoF radius should scale with the local circle of confusion")
    assert_true("mix( 0.02, 1.0, sameSurface )" in shader, "weapon-wheel DoF should preserve the depth-edge rejection floor")
    assert_true("vec2 tap1" not in shader, "weapon-wheel DoF should not regress to the sparse axial kernel")


def test_scene_post_process_excludes_sidecar_views():
    draw_common = (Path(__file__).resolve().parents[2] / "src" / "renderer" / "draw_common.cpp").read_text(encoding="utf-8")
    main_scene_helper = cxx_function_body(
        draw_common,
        "static bool RB_IsMainScenePostProcessView( const viewDef_t *viewDef )",
    )
    scene_target_request = cxx_function_body(
        draw_common,
        "static bool RB_ViewRequestsSceneRenderTarget( const viewDef_t *viewDef )",
    )

    assert_true("viewDef->isSubview" in main_scene_helper, "scene post should not run inside nested subviews")
    assert_true("viewDef->superView != NULL" in main_scene_helper, "scene post should not run inside portal/mirror child views")
    assert_true("viewDef->subviewSurface != NULL" in main_scene_helper, "scene post should not run inside render-to-texture sidecar views")
    assert_true("viewDef->renderView.viewID < 0" in main_scene_helper, "scene post should not run inside render demo/cinematic sidecar views")
    assert_true("RB_IsMainScenePostProcessView( viewDef )" in scene_target_request, "scene render-target requests should use the shared main-scene guard")


def test_display_color_mapping_matches_legacy_curve_and_present_path():
    root = Path(__file__).resolve().parents[2]
    draw_common = (root / "src" / "renderer" / "draw_common.cpp").read_text(encoding="utf-8")
    tr_backend = (root / "src" / "renderer" / "tr_backend.cpp").read_text(encoding="utf-8")
    # The SDL3 GL context half now lives in the renderer-gl module, and the
    # renderer-vk module owns its own copy; both must decline native ramps.
    gl_module = (root / "src" / "renderer" / "OpenGL" / "gl_ContextSDL3.cpp").read_text(encoding="utf-8")
    vk_module = (root / "src" / "renderer" / "Vulkan" / "vk_Backend.cpp").read_text(encoding="utf-8")
    win32_backend = (root / "src" / "sys" / "win32" / "win_glimp.cpp").read_text(encoding="utf-8")
    linux_native = (root / "src" / "sys" / "linux" / "glimp.cpp").read_text(encoding="utf-8")
    osx_native = (root / "src" / "sys" / "osx" / "macosx_glimp.mm").read_text(encoding="utf-8")

    for channel in (0.0, 0.1, 0.25, 0.5, 0.85, 1.0):
        shader_value = apply_display_color_mapping((channel, channel, channel), 1.2, 1.4)[0]
        legacy_value = legacy_gamma_table_value(channel, 1.2, 1.4)
        assert_true(abs(shader_value - legacy_value) < 0.01, "display color mapping should match the legacy gamma table curve")

    assert_true("vec3 color = clamp( sampleColor.rgb * brightness, 0.0, 1.0 );" in draw_common, "final color mapping should apply r_brightness before gamma")
    assert_true("color = pow( color, vec3( 1.0 / safeGamma ) );" in draw_common, "final color mapping should apply r_gamma as the legacy display curve")
    assert_true("GLimp_UseNativeGammaRamps()" in draw_common, "final color mapping should skip platforms that still use native gamma ramps")
    assert_true("RB_ApplyResolutionScaleToBackBuffer();\n\t\tRB_ApplyCRTToBackBuffer();\n\t\tRB_ApplyColorMappingsToBackBuffer();" in tr_backend, "display color mapping should run after other final backbuffer passes")
    assert_true("bool GLimp_UseNativeGammaRamps(void) {\n\treturn false;\n}" in gl_module, "SDL3 renderer-gl module should use renderer-owned color mapping")
    assert_true("bool GLimp_UseNativeGammaRamps( void ) {\n\treturn false;\n}" in vk_module, "renderer-vk module should use renderer-owned color mapping")
    assert_true("bool GLimp_UseNativeGammaRamps( void ) {\n\treturn false;\n}" in win32_backend, "legacy Win32 should use renderer-owned color mapping")
    assert_true("bool GLimp_UseNativeGammaRamps( void ) {\n\treturn true;\n}" in linux_native, "native Linux GL should keep using OS gamma ramps")
    assert_true("bool GLimp_UseNativeGammaRamps(void) {\n\treturn true;\n}" in osx_native, "native macOS GL should keep using OS gamma ramps")


def main():
    tests = [
        test_bloom_contribution_monotonic,
        test_bloom_threshold_is_soft,
        test_bloom_extract_keeps_only_excess_energy,
        test_bloom_extract_preserves_saturated_hdr_highlights,
        test_bloom_extract_discards_negative_energy,
        test_zero_threshold_extracts_full_color,
        test_tone_map_monotonic,
        test_white_point_near_one,
        test_tone_map_preserves_midrange_sdr,
        test_tone_map_preserves_ldr_authored_highlights,
        test_hdr_rejects_negative_scene_energy,
        test_no_nan_edge_cases,
        test_modern_lighting_keeps_scene_referred_energy,
        test_bloom_shader_uses_saturation_aware_brightness,
        test_hdr_shader_uses_scene_referred_inputs,
        test_weapon_wheel_dof_quality_contract,
        test_scene_post_process_excludes_sidecar_views,
        test_display_color_mapping_matches_legacy_curve_and_present_path,
    ]

    for test in tests:
        test()

    print("hdr_postprocess_math: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
