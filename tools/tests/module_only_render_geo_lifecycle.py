#!/usr/bin/env python3
"""Pin the module-only executable render-geometry allocator lifecycle."""

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
    if start < 0:
        raise AssertionError(f"Missing function signature {signature!r}")
    brace = source.find("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"Unterminated function {signature!r}")


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index < 0 or second_index < 0 or first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def main() -> None:
    common = read("src/framework/Common.cpp")
    init_game = function_body(common, "void idCommonLocal::InitGame( void )")
    shutdown_game = function_body(common, "void idCommonLocal::ShutdownGame( bool reloading )")
    tri_surf = read("src/render_geo/RenderGeometryTriSurf.cpp")
    dmap = function_body(
        read("src/tools/compilers/dmap/dmap.cpp"),
        "void Dmap_f( const idCmdArgs &args )",
    )

    require(common, '#include "../render_geo/RenderGeometry.h"', "common render-geometry dependency")
    require(
        common,
        "#ifdef OPENQ4_RENDERER_MODULE_ONLY\n"
        "// The executable and renderer module each link their own render-geometry\n"
        "// library. Track ownership of the executable copy used by offline tools.\n"
        "static bool commonOwnsRenderGeoTriSurfData;\n"
        "#endif",
        "module-only executable lifecycle ownership",
    )

    require(
        init_game,
        "#ifdef OPENQ4_RENDERER_MODULE_ONLY\n"
        "\tif ( !commonOwnsRenderGeoTriSurfData ) {\n"
        "\t\tR_InitTriSurfData();\n"
        "\t\tcommonOwnsRenderGeoTriSurfData = true;\n"
        "\t}\n"
        "#endif",
        "module-only executable triangle allocator initialization",
    )
    require_order(init_game, "R_InitTriSurfData();", "R_RendererModule_BootEarly();", "game initialization")

    require(
        shutdown_game,
        "#ifdef OPENQ4_RENDERER_MODULE_ONLY\n"
        "\tif ( commonOwnsRenderGeoTriSurfData ) {\n"
        "\t\tR_ShutdownTriSurfData();\n"
        "\t\tcommonOwnsRenderGeoTriSurfData = false;\n"
        "\t}\n"
        "#endif",
        "module-only executable triangle allocator shutdown",
    )
    require_order(shutdown_game, "R_ShutdownTriSurfData();", "fileSystem->Shutdown( reloading );", "game shutdown")

    require(tri_surf, "static bool\t\t\ttriSurfDataInitialized;", "triangle allocator lifecycle state")
    require(tri_surf, "if ( triSurfDataInitialized )", "idempotent triangle allocator initialization")
    require(tri_surf, "if ( !triSurfDataInitialized )", "idempotent triangle allocator shutdown")
    require(tri_surf, "silEdges = NULL;", "released silhouette-edge storage")

    require(dmap, "RenderGeo_SetShadowOptimizer", "offline shadow optimizer binding")
    reject(dmap, "R_InitTriSurfData", "dmap command-local lifecycle")
    reject(dmap, "R_ShutdownTriSurfData", "dmap command-local lifecycle")
    reject(dmap, "ownsTriSurfData", "dmap command-local ownership")

    print("module_only_render_geo_lifecycle: ok")


if __name__ == "__main__":
    main()
