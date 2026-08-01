#!/usr/bin/env python3
"""Regression checks for native Linux GLX shutdown."""

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


def validate_glx_shutdown_order() -> None:
    source = read("src/sys/linux/glimp.cpp")
    shutdown = function_body(source, "void GLimp_Shutdown() {")
    close_display = function_body(source, "static void GLimp_CloseDisplay( void ) {")
    init = function_body(source, "bool GLimp_Init( glimpParms_t a ) {")

    reject(source, "//XCloseDisplay( dpy );", "native GLX shutdown")
    reject(source, "FIXME: that's going to crash", "native GLX shutdown")

    require(close_display, "XSync( dpy, False );", "native GLX display close")
    require(close_display, "XCloseDisplay( dpy );", "native GLX display close")
    require(close_display, "dpy = NULL;", "native GLX display close")
    require(close_display, "scrnum = 0;", "native GLX display close")

    require(shutdown, "glXMakeCurrent( dpy, None, NULL );", "native GLX shutdown")
    require(shutdown, "glXDestroyContext( dpy, ctx );", "native GLX shutdown")
    require(shutdown, "GLimp_RestoreDisplayMode();", "native GLX shutdown")
    require(shutdown, "XDestroyWindow( dpy, win );", "native GLX shutdown")
    require(shutdown, "GLimp_FreeVidModes();", "native GLX shutdown")
    require(shutdown, "GLimp_CloseDisplay();", "native GLX shutdown")
    require(shutdown, "GLimp_dlclose();", "native GLX shutdown")

    require_order(shutdown, "glXDestroyContext( dpy, ctx );", "GLimp_CloseDisplay();", "native GLX shutdown")
    require_order(shutdown, "XDestroyWindow( dpy, win );", "GLimp_CloseDisplay();", "native GLX shutdown")
    require_order(shutdown, "GLimp_CloseDisplay();", "GLimp_dlclose();", "native GLX shutdown")

    require(init, "GLimp_Shutdown();\n\t\treturn false;", "native GLX failed init unwind")


def validate_release_note() -> None:
    source = read("docs/dev/platform-support.md")

    require(source, "native/legacy backends are diagnostic", "platform backend boundary")
    require(source, "fallbacks where they remain available", "platform backend boundary")


def main() -> None:
    validate_glx_shutdown_order()
    validate_release_note()
    print("native_glx_shutdown: ok")


if __name__ == "__main__":
    main()
