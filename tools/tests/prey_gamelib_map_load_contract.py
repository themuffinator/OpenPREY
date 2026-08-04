#!/usr/bin/env python3
"""Focused source contracts for GameLib map-load audit fixes."""

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
    return path.read_text(encoding="utf-8", errors="replace")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1 or first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


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


def validate_optional_aas_contract() -> None:
    source = read(GAME_LIBS_ROOT, "src/game/ai/AAS.cpp")
    body = cxx_function_body(source, "bool idAASLocal::Init(")
    require(
        body,
        "fileSystem->ReadFile( mapName, NULL, NULL ) < 0",
        "optional retail AAS probe",
    )
    require(body, "AASFileManager->LoadAAS", "existing AAS loader")
    require(body, "Couldn't load AAS file", "malformed existing AAS diagnostic")
    require_order(
        body,
        "fileSystem->ReadFile( mapName, NULL, NULL )",
        "AASFileManager->LoadAAS",
        "AAS existence probe order",
    )


def validate_recovered_rigidbody_contract() -> None:
    source = read(GAME_LIBS_ROOT, "src/game/physics/Physics_RigidBody.cpp")
    drop = cxx_function_body(source, "void idPhysics_RigidBody::DropToFloorAndRest(")
    clip = cxx_function_body(source, "void idPhysics_RigidBody::SetClipModel(")

    for diagnostic in (
        "rigid body in solid for entity",
        "rigid body not at rest for entity",
    ):
        require(drop, f'gameLocal.DPrintf( "{diagnostic}', "recovered rigid-body diagnostics")
        reject(drop, f'gameLocal.DWarning( "{diagnostic}', "recovered rigid-body diagnostics")
    require(drop, 'gameLocal.Warning( "rigid body outside world bounds', "hard bounds diagnostic")
    require(drop, "Rest();", "rigid-body recovery")

    diagnostic = "idPhysics_RigidBody::SetClipModel: unbalanced inertia tensor"
    require(clip, f'gameLocal.DPrintf( "{diagnostic}', "balanced-inertia recovery diagnostic")
    reject(clip, f'gameLocal.DWarning( "{diagnostic}', "balanced-inertia recovery diagnostic")
    require(clip, "inertiaTensor *= inertiaScale;", "balanced-inertia recovery")


def validate_player_animation_contract() -> None:
    source = read(GAME_LIBS_ROOT, "src/game/Actor.cpp")
    get_anim = cxx_function_body(source, "int idActor::GetAnim(")
    play_anim = cxx_function_body(source, "void idActor::Event_PlayAnim(")

    require(get_anim, "channel == ANIMCHANNEL_LEGS", "Tommy run fallback channel scope")
    require(get_anim, 'GetName(), "model_tommy"', "Tommy run fallback model scope")
    for requested, fallback in (
        ("run_forward", "walk"),
        ("run_backwards", "walk_backwards"),
        ("run_strafe_left", "walk_strafe_left"),
        ("run_strafe_right", "walk_strafe_right"),
    ):
        require(get_anim, f'animname, "{requested}"', "Tommy run fallback map")
        require(get_anim, f'fallbackAnim = "{fallback}"', "Tommy run fallback map")
    require_order(
        get_anim,
        "animatorPtr->GetAnim( animname )",
        "channel == ANIMCHANNEL_LEGS",
        "explicit-animation-before-fallback order",
    )

    for token in (
        "optionalPlayerLower",
        "channel == ANIMCHANNEL_TORSO",
        "!animPrefix.Length()",
        'GetName(), "model_tommy"',
        'animname, "lower"',
        'animname, "altlower"',
        "if ( !optionalPlayerLower )",
        "missing '%s' animation",
        "idThread::ReturnInt( 0 );",
    ):
        require(play_anim, token, "optional unprefixed Tommy lower animation")


def validate_decl_precache_contract() -> None:
    game_source = read(GAME_LIBS_ROOT, "src/game/Game_local.cpp")
    cache = cxx_function_body(game_source, "void idGameLocal::CacheDictionaryMedia(")
    for token in (
        'dict->FindKey( "ragdoll" )',
        "declManager->FindType( DECL_AF",
        'dict->FindKey( "hud" )',
        'dict->FindKey( "cursor" )',
        "uiManager->FindGui",
        'dict->FindKey( "item" )',
        "hhUtils::SplitString",
        'dict->MatchPrefix( "doorobject", NULL )',
        "FindEntityDef",
    ):
        require(cache, token, "entityDef dependency precache")

    multiplayer_source = read(GAME_LIBS_ROOT, "src/game/MultiplayerGame.cpp")
    multiplayer = cxx_function_body(multiplayer_source, "void idMultiplayerGame::Precache(")
    for token in (
        '"textures/decals/painview"',
        'FindMaterial( "_scratch", true )',
        '"textures/decals/hurtview"',
        "LAGO_MATERIAL",
        '"textures/interface/directionalDamageLeft"',
        '"textures/interface/directionalDamageFront"',
        'FindEntityDef( "ammo_types", false )',
        'FindEntityDef( "func_fx", false )',
    ):
        require(multiplayer, token, "multiplayer runtime dependency precache")

    thread_source = read(GAME_LIBS_ROOT, "src/game/script/Script_Thread.cpp")
    set_spawn_arg = cxx_function_body(thread_source, "void idThread::Event_SetSpawnArg(")
    for token in (
        "declManager->GetInsideLoad()",
        "declManager->SetInsideLoad( true )",
        "declManager->SetInsideLoad( false )",
        "idDict mediaArg;",
        "mediaArg.Set( key, value );",
        "gameLocal.CacheDictionaryMedia( &mediaArg );",
    ):
        require(set_spawn_arg, token, "script-built spawn dictionary precache")

    decl_manager = read(ROOT, "src/framework/DeclManager.cpp")
    require(
        decl_manager,
        'common->Warning( "Loading non pre-cached %s decl %s"',
        "late declaration load diagnostic",
    )


def main() -> int:
    validate_optional_aas_contract()
    validate_recovered_rigidbody_contract()
    validate_player_animation_contract()
    validate_decl_precache_contract()
    print("prey_gamelib_map_load_contract: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
