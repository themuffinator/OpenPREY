#!/usr/bin/env python3
"""Static contracts for high-risk openPREY rebase decisions."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CATALOGS = (
    "catalog-renderer.md",
    "catalog-framework.md",
    "catalog-sys-build.md",
    "catalog-sound-ui.md",
    "catalog-corelibs.md",
    "catalog-assets.md",
)


def source(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(relative_path: str, text: str, *tokens: str) -> None:
    for token in tokens:
        if token not in text:
            raise SystemExit(f"{relative_path}: missing rebase contract token {token!r}")


def forbid(relative_path: str, text: str, *tokens: str) -> None:
    for token in tokens:
        if token in text:
            raise SystemExit(f"{relative_path}: stale pre-rebase/upstream token {token!r}")


def validate_catalog_coverage() -> None:
    catalog_ids: list[str] = []
    for catalog in CATALOGS:
        catalog_ids.extend(
            re.findall(r"^### \[([^\]]+)\]", source(f"docs/dev/prey-rebase/{catalog}"), re.MULTILINE)
        )

    ledger = source("docs/dev/prey-rebase/status-ledger.md")
    ledger_rows = re.findall(
        r"^\| `([^`]+)` \| (PORTED|DROPPED|DEFERRED) \|",
        ledger,
        re.MULTILINE,
    )
    ledger_ids = [item_id for item_id, _ in ledger_rows]

    if len(catalog_ids) != len(set(catalog_ids)):
        raise SystemExit("rebase catalogs contain duplicate IDs")
    if len(ledger_ids) != len(set(ledger_ids)):
        raise SystemExit("rebase status ledger contains duplicate IDs")
    if set(catalog_ids) != set(ledger_ids):
        missing = sorted(set(catalog_ids) - set(ledger_ids))
        extra = sorted(set(ledger_ids) - set(catalog_ids))
        raise SystemExit(f"rebase ledger/catalog mismatch: missing={missing}, extra={extra}")

    dispositions = [disposition for _, disposition in ledger_rows]
    expected = {"PORTED": 103, "DROPPED": 34, "DEFERRED": 0}
    actual = {name: dispositions.count(name) for name in expected}
    if len(catalog_ids) != 137 or actual != expected:
        raise SystemExit(
            f"rebase disposition totals changed: IDs={len(catalog_ids)}, dispositions={actual}"
        )


def main() -> None:
    demo_path = "src/framework/DemoFile.cpp"
    demo = source(demo_path)
    require(
        demo_path,
        demo,
        'static const char DEMO_MAGIC[] = GAME_NAME " RDEMO";',
        'static const char INTERIM_DEMO_MAGIC[] = "OpenPREY RDEMO";',
        'static const char LEGACY_DEMO_MAGIC[] = "OpenPrey RDEMO";',
        "sizeof( DEMO_MAGIC ) == sizeof( INTERIM_DEMO_MAGIC )",
        "sizeof( DEMO_MAGIC ) == sizeof( LEGACY_DEMO_MAGIC )",
        "memcmp( magicBuffer, INTERIM_DEMO_MAGIC, magicLen ) == 0",
        "memcmp( magicBuffer, LEGACY_DEMO_MAGIC, magicLen ) == 0",
    )
    forbid(
        demo_path,
        demo,
        "DEMO_MAGIC_HISTORICAL",
        "DEMO_MAGIC_QUAKE4",
        "ambiguousQuake4Wrapper",
    )

    async_path = "src/framework/async/AsyncNetwork.cpp"
    async_network = source(async_path)
    require(
        async_path,
        async_network,
        'serverMaxClientRate( "net_serverMaxClientRate", "32000"',
        'clientMaxRate( "net_clientMaxRate", "32000"',
    )

    menu_path = "src/framework/Session_menu.cpp"
    menu = source(menu_path)
    require(
        menu_path,
        menu,
        "if ( maxClientRate <= 12000 )",
        "if ( maxClientRate <= 16000 )",
        "if ( maxClientRate <= 24000 )",
        'SetCVarInteger( "net_serverMaxClientRate", 12000 )',
        'SetCVarInteger( "net_serverMaxClientRate", 16000 )',
        'SetCVarInteger( "net_serverMaxClientRate", 24000 )',
        'SetCVarInteger( "net_serverMaxClientRate", 32000 )',
        "maxclients = LISTEN_SERVER_MAX_PLAYERS",
    )
    forbid(
        menu_path,
        menu,
        "if ( maxClientRate <= 8000 )",
        "if ( maxClientRate <= 9500 )",
        "if ( maxClientRate <= 10500 )",
        'SetCVarInteger( "net_serverMaxClientRate", 8000 )',
        'SetCVarInteger( "net_serverMaxClientRate", 9500 )',
        'SetCVarInteger( "net_serverMaxClientRate", 10500 )',
    )

    window_path = "src/ui/Window.h"
    window = source(window_path)
    require(
        window_path,
        window,
        "WIN_ALL_FLAGS_MASK",
        "WIN_ALL_FLAGS_SUM",
        'static_assert( WIN_ALL_FLAGS_SUM == WIN_ALL_FLAGS_MASK, "GUI window flag bits overlap" );',
    )

    gui_path = "src/ui/UserInterface.cpp"
    gui = source(gui_path)
    require(
        gui_path,
        gui,
        "OPENPREY_GUI_SAVE_MAGIC",
        "OPENPREY_GUI_SAVE_VERSION",
        '"GUI save magic"',
        '"GUI save version"',
        "unsupported GUI stream marker",
    )
    if gui.index("&OPENPREY_GUI_SAVE_MAGIC") > gui.index("int num = state.GetNumKeyVals()"):
        raise SystemExit(f"{gui_path}: GUI marker must precede the serialized GUI payload")

    lwo_path = "src/renderer/Model_lwo.cpp"
    lwo = source(lwo_path)
    require(lwo_path, lwo, "pp->surfIndex = j;", "index = polygon->pol[ i ].surfIndex;")
    forbid(lwo_path, lwo, "( lwSurface * )(uintptr_t)j", "( int )(uintptr_t)polygon->pol[ i ].surf")

    for bse_path in (
        "meson.build",
        "src/bse/BSE_API.h",
        "src/bse/BSE_Module.cpp",
        "src/framework/Common.cpp",
        "src/framework/DeclManager.cpp",
        "tools/build/meson_setup.ps1",
        "tools/build/meson_setup.sh",
    ):
        bse_source = source(bse_path)
        forbid(
            bse_path,
            bse_source,
            "openq4_bse",
            "openQ4-BSE",
            "build_libbse",
            "openQ4_GetIntegratedBSE",
            "openQ4_AllocIntegratedBSE",
            "openQ4_IsIntegratedBSE",
            "openQ4_DisableBSE",
            "OpenPrey_",
        )

    require("meson.build", source("meson.build"), "'openprey_bse'")
    require(
        "src/bse/BSE_API.h",
        source("src/bse/BSE_API.h"),
        "openPREY_GetIntegratedBSEManager",
        "openPREY_GetIntegratedBSEDeclEffectEdit",
        "openPREY_AllocIntegratedBSEDeclEffect",
        "openPREY_IsIntegratedBSEDeclEffect",
    )

    session_path = "src/framework/Session.cpp"
    session = source(session_path)
    require(
        session_path,
        session,
        "const bool oldDeclInsideLoad = declManager->GetInsideLoad();",
        "declManager->SetInsideLoad( true );",
        "declManager->SetInsideLoad( oldDeclInsideLoad );",
    )

    benchmark_path = "tools/tests/renderer_gameplay_benchmark.py"
    benchmark = source(benchmark_path)
    require(
        benchmark_path,
        benchmark,
        '"g_skipCinematics"',
        '"exec_savepath"',
        "append_post_map_autoexec",
        '"profileLaunchCvars"',
        'log_name = "openprey_gameplay_sp.log"',
        '"caseId": spec.case_id',
        "spec.case_id",
        "--require-references",
        'lines.extend(exec_commands)\n    lines += [\n        "getviewpos",',
    )
    forbid(
        benchmark_path,
        benchmark,
        "g_autoSkipCinematics",
        "g_autoExecAfterMapLoad",
        "g_autoExecAfterMapLoadDelayMs",
    )

    for language in ("english", "french", "italian", "spanish"):
        strings_path = f"content/basepr/pak0/strings/{language}_openprey.lang"
        require(
            strings_path,
            source(strings_path),
            '"#str_107240"',
        )

    validate_catalog_coverage()
    print("openPREY rebase contract checks passed")


if __name__ == "__main__":
    main()
