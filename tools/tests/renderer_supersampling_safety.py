#!/usr/bin/env python3
from pathlib import Path


def assert_true(condition, message):
    if not condition:
        raise AssertionError(message)


def read_repo_file(relative_path):
    return (Path(__file__).resolve().parents[2] / relative_path).read_text(encoding="utf-8")


def test_cvar_exposes_safe_supersampling_range():
    init_cpp = read_repo_file(Path("src") / "renderer" / "RenderSystem_init.cpp")
    display_settings = read_repo_file(Path("docs") / "user" / "display-settings.md")

    assert_true('"r_screenFraction", "100"' in init_cpp, "r_screenFraction should keep native resolution as the default")
    assert_true("10, 200" in init_cpp, "r_screenFraction should expose the guarded 10..200 range")
    assert_true("Main-scene resolution scale percentage (`10..200`)" in display_settings, "display guide should document the guarded supersampling range")


def test_legacy_crop_does_not_run_above_native():
    render_system = read_repo_file(Path("src") / "renderer" / "RenderSystem.cpp")

    assert_true("screenFraction < 100 && r_resolutionScaleMode.GetInteger() == 0" in render_system, "legacy crop mode should only run below native resolution")
    assert_true("Supersampling above native" in render_system, "BeginFrame should document that supersampling is handled offscreen")


def test_scene_target_supersampling_is_guarded_and_scales_clipping():
    draw_common = read_repo_file(Path("src") / "renderer" / "draw_common.cpp")

    assert_true("RB_SCREEN_FRACTION_MAX = 200" in draw_common, "renderer should keep a conservative supersampling ceiling")
    assert_true("RB_MaxSceneScaleDimension" in draw_common and "glConfig.maxTextureSize" in draw_common, "supersampling should clamp against GL texture limits")
    assert_true("RB_SupersampledSceneTargetRequested" in draw_common, "supersampling should request the scene render target")
    assert_true("!supersampledScene && requestedSamples > 1" in draw_common, "supersampled scene targets should remain single-sample instead of layering MSAA onto the oversized FBO")
    assert_true("RB_BeginSceneSupersampling" in draw_common, "supersampling should scale the backend scene command")
    assert_true("RB_ScaleLocalScreenRect( vLight->scissorRect" in draw_common, "light scissors should scale with the supersampled viewport")
    assert_true("RB_ScaleDrawSurfChainScissors" in draw_common, "light draw-surface chains should have scaled scissors")
    assert_true("RB_ScaleLocalScreenRect( vEntity->scissorRect" in draw_common, "entity scissors should scale with the supersampled viewport")
    assert_true("RB_DrawFullscreenPostProcessQuad( sourceViewportWidth, sourceViewportHeight" in draw_common, "present should sample the supersampled source region when resolving")
    assert_true("targetViewport = scaleState.active ? scaleState.nativeViewport" in draw_common, "present should resolve back to the native viewport")


def main():
    test_cvar_exposes_safe_supersampling_range()
    test_legacy_crop_does_not_run_above_native()
    test_scene_target_supersampling_is_guarded_and_scales_clipping()
    print("renderer_supersampling_safety: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
