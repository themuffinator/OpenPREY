#!/usr/bin/env python3
"""Regression checks for Linux SDL3 high-DPI mouse handling."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def validate_sdl3_highdpi_contract() -> None:
    source = read("src/sys/sdl3/sdl3_backend.cpp")
    gl_module = read("src/renderer/OpenGL/gl_ContextSDL3.cpp")
    syscon = read("src/sys/posix/posix_syscon.cpp")
    hints = function_body(source, "static void SDL3_SetMouseHintDefaults(void) {")
    video_hints = function_body(source, "static void SDL3_SetVideoHintDefaults(void) {")
    metadata = function_body(source, "static void SDL3_SetAppMetadataDefaults(void) {")
    apply_video_defaults = function_body(source, "void Sys_SDL_ApplyVideoHintDefaults(void) {")
    display_list = function_body(source, "static void SDL3_PrintDisplayList(void) {")
    display_orientation = function_body(source, "static const char *SDL3_DisplayOrientationName(SDL_DisplayOrientation orientation) {")
    display_mode = function_body(source, "static void SDL3_FormatDisplayMode(const SDL_DisplayMode *mode, char *buffer, int bufferSize) {")
    display_modes_command = function_body(source, "static void SDL3_ListDisplayModes_f(const idCmdArgs &args) {")
    summary = function_body(source, "static void SDL3_PrintVideoDriverSummary(void) {")
    transform = function_body(source, "static bool SDL3_BuildGuiMouseTransform(sdl3GuiMouseTransform_t &transform) {")
    refresh = function_body(source, "static void SDL3_RefreshWindowPlacement(void) {")
    consume = function_body(source, "static int SDL3_ConsumeMouseDelta(float delta, float &remainder) {")
    routed_delta = function_body(source, "static bool SDL3_UpdateRoutedMouseDelta(float menuMouseX, float menuMouseY, int &dx, int &dy) {")
    window_event = function_body(source, "static void SDL3_HandleWindowEvent(const SDL_WindowEvent &event, int eventTime) {")
    event_window_id = function_body(source, "static bool SDL3_EventWindowID(const SDL_Event &event, SDL_WindowID &windowID) {")
    event_targets_game = function_body(source, "static bool SDL3_EventTargetsGameWindow(const SDL_Event &event) {")
    sync_window = function_body(source, "static void SDL3_SyncWindowAfterScreenChange(const char *description) {")
    window_state = function_body(source, "static void SDL3_PrintWaylandWindowState(const char *description) {")
    screen_parms = function_body(source, "static bool SDL3_ApplyScreenParms(glimpParms_t parms) {")
    activate_mouse = function_body(source, "void IN_ActivateMouse(void) {")
    mouse_capture_diagnostics = function_body(source, "static void SDL3_MouseCaptureDiagnostics_f(const idCmdArgs &args) {")
    pump = function_body(source, "bool Sys_SDL_PumpEvents(void) {")
    # GLimp_Init moved into the renderer-gl module; the backend now owns window
    # and input startup (PrepareWindowSystem) and render-window creation
    # (CreateWindowForFramebuffer), which the module drives across the seam.
    startup = function_body(source, "static bool SDL3_WindowServices_PrepareWindowSystem(void) {")
    create_window = function_body(
        source,
        "static bool SDL3_WindowServices_CreateWindowForFramebuffer(const renderFramebufferDesc_t *desc, const renderWindowParms_t *parms,",
    )
    console_layout = function_body(syscon, "static void Posix_ConsoleUpdateLayout( void ) {")
    console_presentation = function_body(syscon, "static void Posix_ConsoleApplyLogicalPresentation( int width, int height ) {")
    console_coordinates = function_body(syscon, "static void Posix_ConsoleWindowToRenderCoordinates( float &x, float &y ) {")
    console_click = function_body(syscon, "static void Posix_ConsoleClickButton( float x, float y ) {")

    require(hints, 'SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0"', "SDL3 unscaled relative mouse hint")
    require(hints, 'SDL_HINT_MOUSE_RELATIVE_WARP_MOTION, "0"', "SDL3 relative warp filtering hint")
    require(source, 'static bool SDL3_EnvFlagEnabled(const char *name) {', "SDL3 Linux environment flag parsing")
    require(source, 'static bool SDL3_EnvHasValue(const char *name) {', "SDL3 Linux environment override parsing")
    require(source, "SDL3_EnvFlagEnabledWithLegacyAlias", "SDL3 openPREY environment migration aliases")
    require(
        video_hints,
        'SDL3_EnvFlagEnabledWithLegacyAlias("OPENPREY_FORCE_X11", "OPENQ4_FORCE_X11")',
        "SDL3 direct XWayland fallback flag",
    )
    require(video_hints, 'SDL_HINT_VIDEO_DRIVER, "x11"', "SDL3 direct XWayland fallback hint")
    require(video_hints, '!SDL3_EnvHasValue("SDL_VIDEO_DRIVER")', "SDL3 direct XWayland fallback respects SDL_VIDEO_DRIVER")
    require(video_hints, '!SDL3_EnvHasValue("SDL_VIDEODRIVER")', "SDL3 direct XWayland fallback respects SDL_VIDEODRIVER")
    require(video_hints, '"OPENPREY_WAYLAND_DISABLE_LIBDECOR", "OPENQ4_WAYLAND_DISABLE_LIBDECOR"', "SDL3 Wayland libdecor disable flag")
    require(video_hints, '"OPENPREY_WAYLAND_PREFER_LIBDECOR", "OPENQ4_WAYLAND_PREFER_LIBDECOR"', "SDL3 Wayland libdecor preference flag")
    require(video_hints, "!disableWaylandLibdecor && SDL3_EnvFlagEnabledWithLegacyAlias(", "SDL3 Wayland libdecor disable precedence")
    require(video_hints, 'SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR, "1"', "SDL3 Wayland libdecor preference hint")
    require(video_hints, '"OPENPREY_WAYLAND_SYNC_WINDOW_OPS", "OPENQ4_WAYLAND_SYNC_WINDOW_OPS"', "SDL3 Wayland sync window ops flag")
    require(video_hints, 'SDL_HINT_VIDEO_SYNC_WINDOW_OPERATIONS, "1"', "SDL3 Wayland sync window ops hint")
    require(video_hints, '!SDL3_EnvHasValue("SDL_VIDEO_DRIVER")', "SDL3 explicit video driver override preservation")
    require(video_hints, '!SDL3_EnvHasValue("SDL_VIDEODRIVER")', "SDL3 legacy video driver override preservation")
    require(video_hints, 'disableWaylandLibdecor ? "0" : "1"', "SDL3 Wayland libdecor allow opt-out")
    require(video_hints, 'SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY, "0"', "SDL3 Wayland logical-size high-DPI hint")
    require(metadata, 'SDL_SetAppMetadata(GAME_NAME, PROJECT_VERSION, "openprey")', "SDL3 stable application metadata")
    require(metadata, "s_sdlAppMetadataAttempted", "SDL3 application metadata one-time guard")
    require(apply_video_defaults, "SDL3_SetAppMetadataDefaults();", "SDL3 metadata before first video initialization")
    require(apply_video_defaults, "SDL3_SetVideoHintDefaults();", "SDL3 platform video defaults")
    if apply_video_defaults.index("SDL3_SetAppMetadataDefaults();") >= apply_video_defaults.index("SDL3_SetVideoHintDefaults();"):
        raise AssertionError("SDL3 application metadata must be set before video hints/initialization")
    require(read("assets/linux/openprey.desktop.in"), "Name=openPREY", "Linux desktop identity paired with SDL metadata")
    require(startup, "Sys_SDL_ApplyVideoHintDefaults();", "SDL3 game metadata and hints before video initialization")
    if startup.index("Sys_SDL_ApplyVideoHintDefaults();") >= startup.index("SDL_InitSubSystem(SDL_INIT_VIDEO)"):
        raise AssertionError("SDL3 game metadata and hints must be applied before video initialization")
    require(summary, "OPENPREY_FORCE_X11=%s OPENPREY_WAYLAND_DISABLE_LIBDECOR=%s OPENPREY_WAYLAND_PREFER_LIBDECOR=%s OPENPREY_WAYLAND_SYNC_WINDOW_OPS=%s", "SDL3 Linux video environment summary")
    require(summary, "SDL3: Wayland hints:", "SDL3 Wayland hint diagnostics")
    require(summary, "SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR", "SDL3 Wayland libdecor hint diagnostics")
    require(summary, "SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR", "SDL3 Wayland libdecor preference diagnostics")
    require(summary, "SDL_HINT_VIDEO_WAYLAND_MODE_EMULATION", "SDL3 Wayland mode emulation diagnostics")
    require(summary, "SDL_HINT_VIDEO_WAYLAND_MODE_SCALING", "SDL3 Wayland mode scaling diagnostics")
    require(summary, "SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY", "SDL3 Wayland scale diagnostics")
    require(summary, "SDL_HINT_VIDEO_SYNC_WINDOW_OPERATIONS", "SDL3 window sync hint diagnostics")
    require(display_orientation, "SDL_ORIENTATION_PORTRAIT", "SDL3 display orientation diagnostics")
    require(display_mode, "mode->pixel_density", "SDL3 display mode pixel density diagnostics")
    require(display_mode, "mode->refresh_rate_numerator", "SDL3 exact refresh numerator diagnostics")
    require(display_mode, "mode->refresh_rate_denominator", "SDL3 exact refresh denominator diagnostics")
    require(display_list, "SDL_GetDisplayContentScale(display)", "SDL3 display content scale diagnostics")
    require(display_list, "SDL_GetNaturalDisplayOrientation(display)", "SDL3 natural display orientation diagnostics")
    require(display_list, "SDL_GetCurrentDisplayOrientation(display)", "SDL3 current display orientation diagnostics")
    require(display_list, "SDL_GetDesktopDisplayMode(display)", "SDL3 desktop mode diagnostics")
    require(display_list, "SDL_GetCurrentDisplayMode(display)", "SDL3 current mode diagnostics")
    require(display_list, "contentScale %.2f", "SDL3 display scale log")
    require(display_list, "orientation %s/%s", "SDL3 display orientation log")
    require(display_modes_command, "SDL_GetDisplayContentScale(display)", "SDL3 listDisplayModes display scale diagnostics")
    require(display_modes_command, "SDL3_FormatDisplayMode(mode, modeText, sizeof(modeText))", "SDL3 listDisplayModes pixel density diagnostics")
    require(create_window, "SDL_WINDOW_HIGH_PIXEL_DENSITY", "SDL3 high pixel density window creation")
    require(syscon, "SDL_WINDOW_HIGH_PIXEL_DENSITY", "SDL3 system console high pixel density window creation")
    require(console_layout, "SDL_GetWindowSize( s_consoleWindow.window, &width, &height )", "SDL3 system console logical window size")
    require(console_layout, "Posix_ConsoleApplyLogicalPresentation( width, height );", "SDL3 system console logical presentation refresh")
    require(console_presentation, "SDL_SetRenderLogicalPresentation(", "SDL3 system console HiDPI presentation")
    require(console_presentation, "SDL_LOGICAL_PRESENTATION_STRETCH", "SDL3 system console full-window presentation")
    require(console_coordinates, "SDL_RenderCoordinatesFromWindow(", "SDL3 system console pointer coordinate conversion")
    require(console_click, "Posix_ConsoleWindowToRenderCoordinates( x, y );", "SDL3 system console HiDPI button input")
    require(sync_window, "SDL_SyncWindow(s_sdlWindow)", "SDL3 compositor window synchronization")
    require(window_state, "SDL_GetWindowPixelDensity(s_sdlWindow)", "SDL3 Wayland window pixel density diagnostics")
    require(window_state, "SDL_GetWindowDisplayScale(s_sdlWindow)", "SDL3 Wayland window display scale diagnostics")
    require(window_state, "native Wayland window state after %s", "SDL3 Wayland window state log")
    require(screen_parms, "SDL3_SyncWindowAfterScreenChange(parms.fullScreen ? \"fullscreen change\" : \"windowed change\")", "SDL3 screen parm compositor synchronization")
    require(screen_parms, "SDL3_RefreshWindowPlacement();", "SDL3 post-sync placement refresh")
    require(screen_parms, "SDL3_PrintWaylandWindowState(parms.fullScreen ? \"fullscreen change\" : \"windowed change\")", "SDL3 post-sync Wayland window state diagnostics")
    require(read("tools/tests/renderer_validation_matrix.py"), '"id": "sdl3-force-x11-display-diagnostics"', "OPENPREY_FORCE_X11 runtime diagnostics case")
    require(read(".github/workflows/commit-validation.yml"), "OPENPREY_FORCE_X11=1 xvfb-run", "OPENPREY_FORCE_X11 CI runtime diagnostics")
    require(activate_mouse, "if (SDL3_IsNativeWaylandVideoDriver()) {", "SDL3 Wayland mouse capture path")
    require(activate_mouse, "SDL_SetWindowRelativeMouseMode(s_sdlWindow, true)", "SDL3 Wayland relative mouse priority")
    require(activate_mouse, "native Wayland could not confine mouse pointer; continuing with relative mouse mode", "SDL3 Wayland best-effort pointer confinement")
    require(mouse_capture_diagnostics, "idMath::ClampInt(1, 8", "SDL3 mouse capture diagnostics repeat clamp")
    require(mouse_capture_diagnostics, "SDL3 mouse capture diagnostics: begin repeat=%d", "SDL3 mouse capture diagnostics repeat log")
    require(mouse_capture_diagnostics, "SDL3 mouse capture diagnostics: iteration %d/%d", "SDL3 mouse capture diagnostics iteration log")
    require(mouse_capture_diagnostics, "IN_ActivateMouse();", "SDL3 mouse capture diagnostics repeated activate")
    require(mouse_capture_diagnostics, "IN_DeactivateMouse();", "SDL3 mouse capture diagnostics repeated deactivate")
    require(startup, "optional repeat count is clamped to 1..8", "SDL3 mouse capture diagnostics command help")

    require(transform, "SDL_GetWindowSize(s_sdlWindow, &windowWidth, &windowHeight)", "SDL3 GUI mouse transform")
    require(transform, "SDL_GetWindowSizeInPixels(s_sdlWindow, &pixelWidth, &pixelHeight)", "SDL3 GUI mouse transform")
    require(transform, "transform.windowToPixelX", "SDL3 window-to-pixel mouse scale")
    require(refresh, "SDL_GetWindowSizeInPixels(s_sdlWindow, &pixelWidth, &pixelHeight)", "SDL3 placement refresh")
    # The framebuffer size is published through one setter that writes the
    # shared engine window state; the static renderer keeps its glConfig mirror
    # and module renderers poll the same state through the window services.
    set_vid_size = function_body(source, "static void SDL3_SetVidSize( int width, int height ) {")
    require(refresh, "SDL3_SetVidSize( pixelWidth, pixelHeight );", "SDL3 framebuffer size refresh")
    require(set_vid_size, "engineWindowState.vidWidth = width;", "SDL3 framebuffer width refresh")
    require(set_vid_size, "engineWindowState.vidHeight = height;", "SDL3 framebuffer height refresh")
    require(set_vid_size, "glConfig.vidWidth = width;", "SDL3 framebuffer width mirror")
    require(set_vid_size, "glConfig.vidHeight = height;", "SDL3 framebuffer height mirror")
    require(gl_module, "glConfig.vidWidth = windowInfo.pixelWidth;", "renderer-gl module framebuffer width poll")
    require(gl_module, "glConfig.vidHeight = windowInfo.pixelHeight;", "renderer-gl module framebuffer height poll")

    require(consume, "const float accumulated = delta + remainder;", "SDL3 fractional mouse accumulator")
    require(consume, "if (!std::isfinite(delta) || !std::isfinite(remainder))", "SDL3 fractional mouse accumulator finite guard")
    require(consume, "if (!std::isfinite(accumulated))", "SDL3 fractional mouse accumulator finite guard")
    require(consume, "const float clampedAccumulated = idMath::ClampFloat(", "SDL3 fractional mouse accumulator clamp")
    require(consume, "const int whole = static_cast<int>(clampedAccumulated);", "SDL3 fractional mouse accumulator")
    require(consume, "remainder = clampedAccumulated - static_cast<float>(whole);", "SDL3 fractional mouse accumulator")
    reject(source, "SDL3_RoundToInt", "SDL3 high-DPI mouse path")

    require(routed_delta, "SDL3_ConsumeMouseDelta(menuMouseX - previousX, s_menuMouseRemainderX)", "SDL3 routed mouse delta")
    require(routed_delta, "SDL3_ConsumeMouseDelta(menuMouseY - previousY, s_menuMouseRemainderY)", "SDL3 routed mouse delta")
    require(pump, "SDL3_ConsumeMouseDelta(event.motion.xrel, s_sdlRelativeMouseRemainderX)", "SDL3 captured relative mouse delta")
    require(pump, "SDL3_ConsumeMouseDelta(event.motion.yrel, s_sdlRelativeMouseRemainderY)", "SDL3 captured relative mouse delta")

    for token in (
        "event.window.windowID",
        "event.key.windowID",
        "event.text.windowID",
        "event.motion.windowID",
        "event.button.windowID",
        "event.wheel.windowID",
        "event.tfinger.windowID",
    ):
        require(event_window_id, token, "SDL3 window-scoped event identification")
    require(event_targets_game, "SDL_GetWindowID(s_sdlWindow)", "SDL3 game window identity filter")
    require(event_targets_game, "eventWindowID == gameWindowID", "SDL3 game window identity filter")
    require(pump, "if (!SDL3_EventTargetsGameWindow(event))", "SDL3 support-window event isolation")
    if pump.index("if (!SDL3_EventTargetsGameWindow(event))") >= pump.index("SDL3_HandleWindowEvent(event.window, eventTime)"):
        raise AssertionError("SDL3 support-window events must be rejected before game window handling")

    require(window_event, "case SDL_EVENT_WINDOW_DISPLAY_CHANGED:", "SDL3 display migration handling")
    require(window_event, "case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:", "SDL3 display scale handling")
    require(window_event, "SDL3_RefreshWindowPlacement();", "SDL3 display scale placement refresh")
    require(window_event, "SDL3_InvalidateMenuMouseRouting();", "SDL3 display scale mouse routing reset")


def validate_ci_smoke() -> None:
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")
    runner = read("tools/validation/openq4_validate.py")

    require(push, "tools/tests/linux_highdpi_mouse.py", "push verification workflow")
    require(push, "python tools/tests/linux_highdpi_mouse.py", "push script-smoke regression check")
    require(commit, "tools/tests/linux_highdpi_mouse.py", "commit validation workflow")
    require(commit, "python tools/tests/linux_highdpi_mouse.py", "commit script-smoke regression check")
    require(runner, "linux_highdpi_mouse.py", "validation Python tests")


def main() -> None:
    validate_sdl3_highdpi_contract()
    validate_ci_smoke()
    print("linux_highdpi_mouse: ok")


if __name__ == "__main__":
    main()
