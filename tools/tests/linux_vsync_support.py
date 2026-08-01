#!/usr/bin/env python3
"""Regression checks for Linux swap-interval / VSync support."""

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


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1:
        raise AssertionError(f"Missing ordered symbols {first!r} and/or {second!r} in {context}")
    if first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def validate_sdl3_swap_interval() -> None:
    # The SDL3 GL context half moved into the renderer-gl module; it now reaches
    # SDL through the window-services seam, so both halves have to be pinned.
    source = read("src/renderer/OpenGL/gl_ContextSDL3.cpp")
    backend = read("src/sys/sdl3/sdl3_backend.cpp")
    apply_swap = function_body(source, "static bool SDL3_ApplySwapInterval(void) {")
    init = function_body(source, "bool GLimp_Init(glimpParms_t parms) {")
    set_screen = function_body(source, "bool GLimp_SetScreenParms(glimpParms_t parms) {")
    swap = function_body(source, "void GLimp_SwapBuffers(void) {")
    backend_set = function_body(backend, "static bool SDL3_WindowServices_SetGLSwapInterval(int interval) {")
    backend_get = function_body(backend, "static bool SDL3_WindowServices_GetGLSwapInterval(int *outInterval) {")

    require(apply_swap, "SetGLSwapInterval(requestedInterval)", "SDL3 swap interval helper")
    require(apply_swap, "GetGLSwapInterval(&actualInterval)", "SDL3 swap interval helper")
    require(apply_swap, "requested swap interval", "SDL3 swap interval diagnostics")

    require(backend_set, "SDL_GL_SetSwapInterval(interval)", "SDL3 swap interval seam implementation")
    require(backend_get, "SDL_GL_GetSwapInterval(outInterval)", "SDL3 swap interval seam implementation")

    require(init, "SDL3_LoadWGLExtensions();", "SDL3 GL init")
    require(init, "SDL3_ApplySwapInterval();", "SDL3 GL init")
    require_order(init, "SDL3_LoadWGLExtensions();", "SDL3_ApplySwapInterval();", "SDL3 GL init")

    require(set_screen, "r_swapInterval.SetModified();", "SDL3 screen parm changes")
    require(set_screen, "SDL3_ApplySwapInterval();", "SDL3 screen parm changes")

    require(swap, "r_swapInterval.IsModified()", "SDL3 buffer swap")
    require(swap, "SDL3_ApplySwapInterval();", "SDL3 buffer swap")
    reject(swap, "SDL_GL_SetSwapInterval(r_swapInterval.GetInteger())", "SDL3 buffer swap")


def validate_native_glx_swap_interval() -> None:
    source = read("src/sys/linux/glimp.cpp")
    init_swap = function_body(source, "static void GLX_InitSwapControl( void ) {")
    apply_swap = function_body(source, "static bool GLX_ApplySwapInterval( void ) {")
    init = function_body(source, "int GLX_Init(glimpParms_t a) {")
    shutdown = function_body(source, "void GLimp_Shutdown() {")
    swap = function_body(source, "void GLimp_SwapBuffers() {")

    require(source, "openq4GLXSwapIntervalEXTProc_t", "native GLX swap-control declarations")
    require(source, "openq4GLXSwapIntervalMESAProc_t", "native GLX swap-control declarations")
    require(source, "openq4GLXSwapIntervalSGIProc_t", "native GLX swap-control declarations")
    require(source, "GLX_EXT_swap_control_tear", "native GLX adaptive swap-control detection")

    require(init_swap, "GLX_EXT_swap_control", "native GLX swap-control init")
    require(init_swap, "GLX_MESA_swap_control", "native GLX swap-control init")
    require(init_swap, "GLX_SGI_swap_control", "native GLX swap-control init")
    require(init_swap, "glXSwapIntervalEXT", "native GLX swap-control init")
    require(init_swap, "glXSwapIntervalMESA", "native GLX swap-control init")
    require(init_swap, "glXSwapIntervalSGI", "native GLX swap-control init")

    require(apply_swap, "GLX_NormalizeSwapIntervalForBackend", "native GLX swap interval application")
    require(apply_swap, "glx_swap_interval_ext", "native GLX swap interval application")
    require(apply_swap, "glx_swap_interval_mesa", "native GLX swap interval application")
    require(apply_swap, "glx_swap_interval_sgi", "native GLX swap interval application")
    require(apply_swap, "no usable GLX swap-control extension", "native GLX swap interval diagnostics")

    require(init, "GLX_InitSwapControl();", "native GLX init")
    require(init, "r_swapInterval.SetModified();", "native GLX init")
    require(init, "GLX_ApplySwapInterval();", "native GLX init")
    require_order(init, "GLX_InitSwapControl();", "GLX_ApplySwapInterval();", "native GLX init")

    require(swap, "r_swapInterval.IsModified()", "native GLX buffer swap")
    require(swap, "GLX_ApplySwapInterval();", "native GLX buffer swap")
    require_order(swap, "GLX_ApplySwapInterval();", "glXSwapBuffers( dpy, win );", "native GLX buffer swap")

    require(shutdown, "GLX_ResetSwapControl();", "native GLX shutdown")


def validate_shared_metadata() -> None:
    renderer = read("src/renderer/RenderSystem_init.cpp")
    header = read("src/renderer/tr_local.h")

    require(renderer, "controls the platform swap interval / VSync state", "r_swapInterval cvar description")
    require(header, "controls platform swap interval / VSync", "r_swapInterval declaration comment")


def validate_ci_smoke() -> None:
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")
    runner = read("tools/validation/openq4_validate.py")

    require(push, "tools/tests/linux_vsync_support.py", "push verification workflow")
    require(push, "python tools/tests/linux_vsync_support.py", "push script-smoke regression check")
    require(commit, "tools/tests/linux_vsync_support.py", "commit validation workflow")
    require(commit, "python tools/tests/linux_vsync_support.py", "commit script-smoke regression check")
    require(runner, "linux_vsync_support.py", "validation Python tests")


def main() -> None:
    validate_sdl3_swap_interval()
    validate_native_glx_swap_interval()
    validate_shared_metadata()
    validate_ci_smoke()
    print("linux_vsync_support: ok")


if __name__ == "__main__":
    main()
