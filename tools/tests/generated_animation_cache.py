#!/usr/bin/env python3
"""Regression checks for the generated MD5 animation cache contract."""

from __future__ import annotations

import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(
    os.environ.get("OPENPREY_GAMELIBS_REPO")
    or os.environ.get("OPENQ4_GAMELIBS_REPO")
    or ROOT.parent / "OpenPrey-game"
).resolve()


def read(root: Path, relative_path: str) -> str:
    path = root / relative_path
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1 or first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def validate_engine_file_identity_contract() -> None:
    engine_header = read(ROOT, "src/framework/File.h")
    file_source = read(ROOT, "src/framework/File.cpp")
    filesystem_source = read(ROOT, "src/framework/FileSystem.cpp")

    # Companion SDK/header mirrors are deliberately excluded during staging;
    # the engine interface is the single canonical contract used by GameLibs.
    require(
        engine_header,
        "virtual int\t\t\t\tGetContainerChecksum( void ) const { return 0; }",
        "engine file interface",
    )
    require(
        engine_header,
        "virtual int\t\t\t\tGetContainerChecksum( void ) const { return containerChecksum; }",
        "engine file interface",
    )
    require(
        engine_header,
        "int\t\t\t\t\t\tcontainerChecksum;",
        "engine file interface",
    )

    require(file_source, "containerChecksum = 0;", "PK4 file construction")
    require(
        filesystem_source,
        "file->containerChecksum = pak->checksum;",
        "PK4 file source identity",
    )


def validate_game_cache_contract() -> None:
    game = read(GAME_LIBS_ROOT, "src/game/anim/Anim.cpp")


    for token in (
        'g_useGeneratedAnimCache( "g_useGeneratedAnimCache", "1"',
        'g_writeGeneratedAnimCache( "g_writeGeneratedAnimCache", "1"',
        'path = "generated/animations/";',
        "GENERATED_ANIM_MAGIC",
        "GENERATED_ANIM_END_MAGIC",
        "GENERATED_ANIM_VERSION",
        "MAX_GENERATED_ANIM_FRAMES",
        "MAX_GENERATED_ANIM_JOINTS",
        "MAX_GENERATED_ANIM_DATA_BYTES",
        "info.length = source->Length();",
        "info.timestamp = source->Timestamp();",
        "info.containerChecksum = source->GetContainerChecksum();",
        "info.fullPath = source->GetFullPath();",
        "sourceLength == sourceInfo.length",
        "sourceContainerChecksum == sourceInfo.containerChecksum",
        "sourceTimestamp == sourceInfo.timestamp",
        "sourceFullPath.Cmp( sourceInfo.fullPath ) == 0",
        "position < 0 || fileLength < position",
        "valueLength > MAX_STRING_CHARS",
        "LittleRevBytes( values, sizeof( float ), count );",
        "if ( !Swap_IsBigEndian() )",
        "componentCount <= 0x7fffffff",
        "dataBytes <= MAX_GENERATED_ANIM_DATA_BYTES",
        "endMagic == GENERATED_ANIM_END_MAGIC",
        "cache->Tell() == cache->Length()",
        "fileSystem->RemoveFile( cachePath );",
        "baseFrame.Clear();",
        "baseFrame.Allocated()",
    ):
        require(game, token, "unified generated animation cache implementation")

    require_order(
        game,
        "g_useGeneratedAnimCache.GetBool() && LoadGeneratedAnim( filename )",
        "parser.LoadFile( filename )",
        "cache-before-source load order",
    )
    require_order(
        game,
        "animLength = ( ( numFrames - 1 ) * 1000 + frameRate - 1 ) / frameRate;",
        "WriteGeneratedAnim( filename );",
        "source-parse cache write order",
    )


def validate_model_decl_parse_contract() -> None:
    header = read(GAME_LIBS_ROOT, "src/game/anim/Anim.h")
    parser = read(GAME_LIBS_ROOT, "src/game/anim/Anim_Blend.cpp")

    require(
        header,
        "Parse( const char *text, const int textLength, bool noCaching )",
        "model decl interface",
    )
    require(
        parser,
        "return Parse( text, textLength, false );",
        "model decl two-argument compatibility overload",
    )
    require(
        parser,
        "idDeclModelDef::Parse( const char *text, const int textLength, bool noCaching )",
        "model decl upstream parse entry point",
    )


def validate_documentation_contract() -> None:
    guide = read(ROOT, "docs/user/level-load-cache.md")

    for token in (
        "<fs_savepath>/basepr/generated/animations/",
        "g_useGeneratedAnimCache 1",
        "g_writeGeneratedAnimCache 1",
        "stale, truncated, corrupt, or mismatched cache is ignored",
        "x64 and ARM64 builds use the same format",
    ):
        require(guide, token, "level-load cache guide")


def main() -> None:
    validate_engine_file_identity_contract()
    validate_game_cache_contract()
    validate_model_decl_parse_contract()
    validate_documentation_contract()
    print("generated_animation_cache: ok")


if __name__ == "__main__":
    main()
