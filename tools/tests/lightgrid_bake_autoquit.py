#!/usr/bin/env python3
"""Pin bakeLightGrids auto-quit behavior when a renderer bake fails."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index < 0 or second_index < 0 or first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


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


def conditional_blocks(source: str, marker: str) -> list[str]:
    blocks: list[str] = []
    cursor = 0
    while True:
        start = source.find(marker, cursor)
        if start < 0:
            return blocks
        brace = source.find("{", start)
        depth = 0
        for index in range(brace, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    blocks.append(source[start : index + 1])
                    cursor = index + 1
                    break
        else:
            raise AssertionError(f"Unterminated conditional {marker!r}")


def main() -> None:
    session = read("src/framework/Session.cpp")
    run_bake = function_body(session, "static void Session_RunLightGridBake( const idCmdArgs &args )")
    failure_blocks = conditional_blocks(
        run_bake,
        "if ( !Session_BakeLightGridCurrentMap( options, forceBake ) ) {",
    )
    if len(failure_blocks) != 2:
        raise AssertionError(
            "Expected current-map and batch bake failure exits, "
            f"found {len(failure_blocks)}"
        )

    queued_quit = (
        'if ( autoQuit ) {\n'
        '\t\t\t\tcmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\\n" );\n'
        "\t\t\t}"
    )
    for label, block in zip(("current-map failure", "batch failure"), failure_blocks):
        require(block, queued_quit, label)
        require_order(block, queued_quit, "\t\t\treturn;", label)

    require(
        run_bake,
        'common->Printf( "bakeLightGrids: batch completed for %i map(s)\\n", mapTargets.Num() );\n'
        '\tif ( autoQuit ) {\n'
        '\t\tcmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\\n" );\n'
        "\t}",
        "successful batch auto-quit path",
    )
    require(
        read("tools/validation/openq4_validate.py"),
        "lightgrid_bake_autoquit.py",
        "validation runner registration",
    )

    print("lightgrid_bake_autoquit: ok")


if __name__ == "__main__":
    main()
