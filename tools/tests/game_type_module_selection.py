#!/usr/bin/env python3
"""Guards openPREY's unified game-module selection contract.

Prey uses one game module for both single-player and multiplayer.  The gameplay
mode selected by ``si_gameType`` must therefore never choose a different binary.
This test also pins the canonical OpenPrey-game default to single-player and
keeps the guarded reload path covered.
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(
    os.environ.get("OPENPREY_GAMELIBS_REPO")
    or os.environ.get("OPENQ4_GAMELIBS_REPO")
    or ROOT.parent / "OpenPrey-game"
).resolve()


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def cxx_function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function {signature!r}")
    open_brace = source.index("{", start)
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1 : index]
    raise AssertionError(f"Unclosed function {signature!r}")


def validate_unified_module_selection() -> None:
    common = read(ROOT / "src" / "framework" / "Common.cpp")
    selector = cxx_function_body(
        common, "static const char *openPREY_SelectGameModuleBaseName( void )"
    )
    require(selector, 'return "game";', "unified game-module selector")
    reject(selector, "si_gameType", "unified game-module selector")

    candidates = cxx_function_body(
        common, "static void openPREY_BuildGameModuleCandidateList( idStrList &candidates )"
    )
    require(
        candidates,
        'va( "game_%s", OPENPREY_MODULE_ARCH_TAG )',
        "unified game-module candidate list",
    )
    require(candidates, '"game_universal2"', "unified macOS module compatibility")
    require(candidates, '"game"', "unified untagged module compatibility")
    reject(candidates, '"game_sp', "unified game-module candidate list")
    reject(candidates, '"game_mp', "unified game-module candidate list")

    loader = cxx_function_body(common, "void idCommonLocal::LoadGameDLL( void )")
    require(
        loader,
        "openPREY_SelectGameModuleBaseName()",
        "unified game-module loader",
    )
    require(
        loader,
        "openPREY_BuildGameModuleCandidateList( gameModuleCandidates )",
        "unified game-module loader",
    )
    require(
        loader,
        "couldn't find unified game dynamic library",
        "unified game-module loader diagnostic",
    )


def validate_game_type_default() -> None:
    sys_cvar = GAME_LIBS_ROOT / "src" / "game" / "gamesys" / "SysCvar.cpp"
    if not sys_cvar.is_file():
        print(
            "game_type_module_selection: skipped GameLibs default cross-check "
            f"(no canonical source at {sys_cvar})"
        )
        return

    source = read(sys_cvar)
    match = re.search(
        r"^const char \*si_gameTypeArgs\[\]\s*=\s*\{([^}]*)\};",
        source,
        re.MULTILINE,
    )
    if match is None:
        raise AssertionError("active OpenPrey-game si_gameTypeArgs declaration is missing")
    game_types = re.findall(r'"([^"]*)"', match.group(1))
    if not game_types or game_types[0] != "singleplayer":
        raise AssertionError(
            f"OpenPrey-game si_gameTypeArgs starts with {game_types[:1]!r}, "
            "expected 'singleplayer'"
        )
    require(
        source,
        'idCVar si_gameType(\t\t\t\t\t"si_gameType",\t\t\t\tsi_gameTypeArgs[ 0 ]',
        "OpenPrey-game default gametype",
    )


def validate_unified_build_contract() -> None:
    meson = read(ROOT / "meson.build")
    basepr_meson = read(ROOT / "content" / "basepr" / "meson.build")
    require(meson, "game_binary_name = 'game_' + binary_arch", "unified module name")
    require(
        meson,
        "Prey uses one unified game module for both single-player and multiplayer.",
        "unified module source contract",
    )
    require(basepr_meson, "game_module_target =", "unified basepr module target")


def validate_swap_guard() -> None:
    common = read(ROOT / "src" / "framework" / "Common.cpp")
    start = common.index("void Com_ReloadGameModule_f( const idCmdArgs &args ) {")
    end = common.index("idCommonLocal::GetLanguageDict", start)
    body = common[start:end]
    for token in (
        "try {",
        "commonLocal.ShutdownGame( true );",
        "commonLocal.InitGame();",
        "catch( idException &ex ) {",
        "swapFailed = true;",
        "Com_GameModuleLoadPhaseName( Com_GetGameModuleLoadPhase() )",
        "============= ReloadGameModule failed ============",
        "session->StartMenu();",
    ):
        require(body, token, "game module swap exception guard")


def validate_decl_source_scanner_contract() -> None:
    source = read(ROOT / "src" / "framework" / "DeclManager.cpp")
    require(
        source,
        "declDefinition = finalPreprocessedBuffer.Mid( startMarker, sourceSize );",
        "loose decl exact-source scanner",
    )
    require(
        source,
        "declDefinition = packedText.Mid( startMarker, sourceSize );",
        "packed decl exact-source scanner",
    )
    reject(
        source,
        "src.ParseBracedSectionExact( declDefinition, -1 );",
        "comment-safe decl source scanners",
    )


def validate_renderer_module_fail_closed_contract() -> None:
    api = read(ROOT / "src" / "renderer" / "RenderModuleAPI.h")
    require(api, "#define RENDER_API_VERSION\t\t\t9", "current renderer ABI")

    loader = read(ROOT / "src" / "renderer" / "RendererModule.cpp")
    require(
        loader,
        "moduleExport->version != RENDER_API_VERSION",
        "stale renderer-module rejection",
    )
    require(
        loader,
        '"module render API version mismatch"',
        "stale renderer-module diagnostic",
    )

    session = read(ROOT / "src" / "framework" / "Session.cpp")
    unload = cxx_function_body(session, "void idSessionLocal::UnloadMap()")
    require(
        unload,
        "if ( soundSystem && sw )",
        "renderer-ABI early-fatal sound-world guard",
    )


def main() -> int:
    validate_unified_module_selection()
    validate_game_type_default()
    validate_unified_build_contract()
    validate_swap_guard()
    validate_decl_source_scanner_contract()
    validate_renderer_module_fail_closed_contract()
    print("game_type_module_selection: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
