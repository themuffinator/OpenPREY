#!/usr/bin/env python3
"""Regression checks for split-map campaign transition validation."""

from __future__ import annotations

import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game"))


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1:
        raise AssertionError(f"Missing ordered symbols {first!r} and/or {second!r} in {context}")
    if first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


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


def validate_session_assertion_command() -> None:
    source = read(ROOT / "src/framework/Session.cpp")
    body = function_body(source, "static void Session_openQ4AssertMapState_f( const idCmdArgs &args ) {")
    move_body = function_body(source, "void idSessionLocal::MoveToNewMap( const char *mapName ) {")

    require(source, 'cmdSystem->AddCommand( "openq4_assertMapState"', "session command registration")
    require(body, 'Session_NormalizeMapPathAndEntityFilter( args.Argv( 1 ), Session_GetEntityFilterArg( args ), expectedMap, expectedEntityFilter );', "expected map/filter normalization")
    require(body, 'Session_NormalizeMapDeclPath( sessLocal.mapSpawnData.serverInfo.GetString( "si_map", "" ), actualMap );', "actual map normalization")
    require(body, 'Session_NormalizeEntityFilterToken( sessLocal.mapSpawnData.serverInfo.GetString( "si_entityFilter", "" ), actualEntityFilter );', "actual entity-filter normalization")
    require(body, "fileSystem->FilenameCompare( actualMap.c_str(), expectedMap.c_str() ) == 0", "case-insensitive map comparison")
    require(body, "idStr::Icmp( actualEntityFilter.c_str(), expectedEntityFilter.c_str() ) == 0", "entity-filter comparison")
    require(body, "common->Error( \"openPREY map state mismatch:", "hard-fail mismatch")
    reject(body, "Session_NormalizeMapPathAndEntityFilter( sessLocal.mapSpawnData.serverInfo", "actual state must not default missing filters to first")
    require(move_body, "Session_NormalizeMapPathAndEntityFilter( mapName, \"\", normalizedMapName, embeddedEntityFilter, false );", "MoveToNewMap must not synthesize first over an explicit transition filter")


def validate_benchmark_profile() -> None:
    source = read(ROOT / "tools/tests/renderer_gameplay_benchmark.py")

    require(source, '"sp-campaign-mcc2-to-tram1"', "campaign transition scene")
    require(source, '"campaign-split-state-transition"', "campaign transition profile")
    require(source, '"execCommands": CAMPAIGN_MCC2_TO_TRAM1_COMMANDS', "profile command binding")
    require(source, '"mapStateMismatch": re.compile(r"ERROR:\\s+openPREY map state mismatch|openPREY map state assertion"', "map-state mismatch warning pattern")

    expected_sequence = (
        "openq4_assertMapState game/mcc_2",
        "trigger mcc2_endlevel",
        "openq4_assertMapState game/storage1 first",
        "trigger endLevel",
        "openq4_assertMapState game/storage2",
        "trigger target_endlevel_1",
        "openq4_assertMapState game/storage1 second",
        "trigger target_endlevel_2",
        "openq4_assertMapState game/tram1",
    )
    for token in expected_sequence:
        require(source, token, "MCC2 to Tram1 scripted transition sequence")
    for first, second in zip(expected_sequence, expected_sequence[1:]):
        require_order(source, first, second, "MCC2 to Tram1 scripted transition order")


def validate_game_endlevel_command_shape() -> None:
    target_source = read(GAME_LIBS / "src/game/Target.cpp")
    body = function_body(target_source, "void idTarget_EndLevel::Event_Activate( idEntity *activator ) {")

    require(body, 'gameLocal.sessionCommand = "map ";', "target_endlevel map command")
    require(body, "gameLocal.sessionCommand += nextMap;", "target_endlevel nextMap append")
    require(body, 'spawnArgs.GetString( "entityFilter", "", &entityFilter ) && *entityFilter', "target_endlevel entityFilter lookup")
    require_order(body, "gameLocal.sessionCommand += nextMap;", 'gameLocal.sessionCommand += " ";', "target_endlevel filter separator")
    require_order(body, 'gameLocal.sessionCommand += " ";', "gameLocal.sessionCommand += entityFilter;", "target_endlevel filter append")


def validate_ci_wiring() -> None:
    validation = read(ROOT / "tools/validation/openq4_validate.py")
    push = read(ROOT / ".github/workflows/push-verification.yml")
    commit = read(ROOT / ".github/workflows/commit-validation.yml")

    for context, source in (
        ("validation runner", validation),
        ("push workflow", push),
        ("commit workflow", commit),
    ):
        require(source, "campaign_split_state_transition.py", context)


def main() -> None:
    validate_session_assertion_command()
    validate_benchmark_profile()
    validate_game_endlevel_command_shape()
    validate_ci_wiring()
    print("campaign_split_state_transition: ok")


if __name__ == "__main__":
    main()
