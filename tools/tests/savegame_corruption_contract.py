#!/usr/bin/env python3
"""Savegame corruption and fuzz guardrails for engine-side load code."""

from __future__ import annotations

import importlib.util
import os
import re
import struct
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(
    os.environ.get("OPENPREY_GAMELIBS_REPO")
    or os.environ.get("OPENQ4_GAMELIBS_REPO")
    or ROOT.parent / "OpenPrey-game"
).resolve()


def read(relative_path: str) -> str:
    path = ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


def read_game_libs(relative_path: str) -> str:
    path = GAME_LIBS_ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"Required OpenPrey-game source file not found: {path}")
    return path.read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_regex(haystack: str, pattern: str, context: str) -> None:
    if re.search(pattern, haystack, re.MULTILINE | re.DOTALL) is None:
        raise AssertionError(f"Missing pattern {pattern!r} in {context}")


def parse_int_constant(relative_path: str, pattern: str, context: str) -> int:
    source = read(relative_path)
    match = re.search(pattern, source, re.MULTILINE)
    if match is None:
        raise AssertionError(f"Missing integer constant for {context}")
    return int(match.group(1))


def parse_ssd_entity_types() -> dict[str, int]:
    source = read("src/ui/GameSSDWindow.h")
    for match in re.finditer(r"enum\s*\{(?P<body>.*?)\};", source, re.MULTILINE | re.DOTALL):
        body = match.group("body")
        if "SSD_ENTITY_BASE" not in body:
            continue

        values: dict[str, int] = {}
        current = -1
        for raw_entry in body.split(","):
            entry = raw_entry.strip()
            if not entry:
                continue
            if "=" in entry:
                name, value = entry.split("=", 1)
                current = int(value.strip())
            else:
                name = entry
                current += 1
            values[name.strip()] = current
        return values

    raise AssertionError("Missing SSD entity type enum")


MAX_STRING_CHARS = parse_int_constant(
    "src/idlib/Lib.h",
    r"#define\s+MAX_STRING_CHARS\s+(\d+)",
    "MAX_STRING_CHARS",
)
MAX_ASYNC_CLIENTS = parse_int_constant(
    "src/framework/async/AsyncNetwork.h",
    r"MAX_ASYNC_CLIENTS\s*=\s*(\d+)",
    "MAX_ASYNC_CLIENTS",
)
SESSION_MAX_SAVEGAME_DICT_KV = 16384
SSD_MAX_ASTEROIDS = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_ASTEROIDS\s+(\d+)", "MAX_ASTEROIDS")
SSD_MAX_ASTRONAUT = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_ASTRONAUT\s+(\d+)", "MAX_ASTRONAUT")
SSD_MAX_EXPLOSIONS = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_EXPLOSIONS\s+(\d+)", "MAX_EXPLOSIONS")
SSD_MAX_POINTS = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_POINTS\s+(\d+)", "MAX_POINTS")
SSD_MAX_PROJECTILES = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_PROJECTILES\s+(\d+)", "MAX_PROJECTILES")
SSD_MAX_POWERUPS = parse_int_constant("src/ui/GameSSDWindow.h", r"#define\s+MAX_POWERUPS\s+(\d+)", "MAX_POWERUPS")
SSD_MAX_SAVE_LEVELS = parse_int_constant(
    "src/ui/GameSSDWindow.cpp",
    r"GAME_SSD_MAX_SAVE_LEVELS\s*=\s*(\d+)",
    "GAME_SSD_MAX_SAVE_LEVELS",
)
SSD_MAX_SAVE_WEAPONS = parse_int_constant(
    "src/ui/GameSSDWindow.cpp",
    r"GAME_SSD_MAX_SAVE_WEAPONS\s*=\s*(\d+)",
    "GAME_SSD_MAX_SAVE_WEAPONS",
)
SSD_ENTITY_TYPES = parse_ssd_entity_types()
SSD_POOL_ORDER = (
    ("asteroid", SSD_ENTITY_TYPES["SSD_ENTITY_ASTEROID"], SSD_MAX_ASTEROIDS),
    ("astronaut", SSD_ENTITY_TYPES["SSD_ENTITY_ASTRONAUT"], SSD_MAX_ASTRONAUT),
    ("explosion", SSD_ENTITY_TYPES["SSD_ENTITY_EXPLOSION"], SSD_MAX_EXPLOSIONS),
    ("points", SSD_ENTITY_TYPES["SSD_ENTITY_POINTS"], SSD_MAX_POINTS),
    ("projectile", SSD_ENTITY_TYPES["SSD_ENTITY_PROJECTILE"], SSD_MAX_PROJECTILES),
    ("powerup", SSD_ENTITY_TYPES["SSD_ENTITY_POWERUP"], SSD_MAX_POWERUPS),
)
SSD_ENTITY_POOL_LIMITS = {entity_type: max_count for _, entity_type, max_count in SSD_POOL_ORDER}
SSD_MAX_SAVE_ENTITY_REFS = sum(SSD_ENTITY_POOL_LIMITS.values())


class CorruptSave(ValueError):
    pass


class SaveHeaderReader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0

    def read_int(self) -> int:
        if self.offset + 4 > len(self.data):
            raise CorruptSave("truncated int")
        value = struct.unpack_from("<i", self.data, self.offset)[0]
        self.offset += 4
        return value

    def read_save_string(self, max_length: int) -> bytes:
        length = self.read_int()
        remaining = len(self.data) - self.offset
        if length < 0 or length > max_length or length > remaining:
            raise CorruptSave(f"invalid save string length {length}")
        value = self.data[self.offset : self.offset + length]
        self.offset += length
        return value

    def read_cstring(self, max_length: int) -> bytes:
        start = self.offset
        for _ in range(max_length):
            if self.offset >= len(self.data):
                raise CorruptSave("truncated cstring")
            value = self.data[self.offset]
            self.offset += 1
            if value == 0:
                return self.data[start : self.offset - 1]
        raise CorruptSave("unterminated cstring")

    def read_dict(self) -> dict[bytes, bytes]:
        count = self.read_int()
        if count < 0 or count > SESSION_MAX_SAVEGAME_DICT_KV:
            raise CorruptSave(f"invalid dict count {count}")
        values: dict[bytes, bytes] = {}
        for _ in range(count):
            key = self.read_cstring(MAX_STRING_CHARS)
            value = self.read_cstring(MAX_STRING_CHARS)
            values[key] = value
        return values


class SSDRestoreGuardReader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0
        self.restored_ids: dict[int, set[int]] = {entity_type: set() for entity_type in SSD_ENTITY_POOL_LIMITS}

    def read_int(self) -> int:
        if self.offset + 4 > len(self.data):
            raise CorruptSave("truncated SSD int")
        value = struct.unpack_from("<i", self.data, self.offset)[0]
        self.offset += 4
        return value

    def read_count(self, label: str, max_count: int) -> int:
        count = self.read_int()
        if count < 0 or count > max_count:
            raise CorruptSave(f"invalid SSD {label} {count}")
        return count

    @staticmethod
    def validate_index(label: str, value: int, count: int) -> None:
        if value < 0 or value >= count:
            raise CorruptSave(f"invalid SSD {label} {value} for count {count}")

    @staticmethod
    def validate_index_or_end(label: str, value: int, count: int) -> None:
        if value < 0 or value > count:
            raise CorruptSave(f"invalid SSD {label} {value} for count {count}")

    @staticmethod
    def validate_entity_reference(label: str, entity_type: int, entity_id: int) -> None:
        max_count = SSD_ENTITY_POOL_LIMITS.get(entity_type)
        if max_count is None:
            raise CorruptSave(f"invalid SSD {label} type {entity_type}")
        if entity_id < 0 or entity_id >= max_count:
            raise CorruptSave(f"invalid SSD {label} id {entity_id}")

    def parse(self) -> None:
        level_count = self.read_count("level count", SSD_MAX_SAVE_LEVELS)
        weapon_count = self.read_count("weapon count", SSD_MAX_SAVE_WEAPONS)

        current_weapon = self.read_int()
        current_level = self.read_int()
        next_level = self.read_int()
        self.validate_index("current weapon", current_weapon, weapon_count)
        self.validate_index("current level", current_level, level_count)
        self.validate_index_or_end("next level", next_level, level_count)

        for label, entity_type, max_count in SSD_POOL_ORDER:
            count = self.read_count(f"{label} count", max_count)
            for _ in range(count):
                entity_id = self.read_int()
                self.validate_entity_reference(f"{label} id", entity_type, entity_id)
                self.restored_ids[entity_type].add(entity_id)

        entity_ref_count = self.read_count("entity reference count", SSD_MAX_SAVE_ENTITY_REFS)
        for _ in range(entity_ref_count):
            entity_type = self.read_int()
            entity_id = self.read_int()
            self.validate_entity_reference("entity reference", entity_type, entity_id)
            if entity_id not in self.restored_ids[entity_type]:
                raise CorruptSave(f"SSD entity reference points at inactive pool id {entity_id}")

        if self.offset != len(self.data):
            raise CorruptSave("SSD guard payload has trailing bytes")


def int32(value: int) -> bytes:
    return struct.pack("<i", value)


def save_string(value: bytes) -> bytes:
    return int32(len(value)) + value


def save_dict(pairs: tuple[tuple[bytes, bytes], ...] = ()) -> bytes:
    data = int32(len(pairs))
    for key, value in pairs:
        data += key + b"\0" + value + b"\0"
    return data


def valid_prey_header() -> bytes:
    return (
        save_string(b"Prey")
        + int32(12)
        + save_string(b"maps/game/roadhouse.map")
        + save_string(b"")
        + (save_dict() * MAX_ASYNC_CLIENTS)
    )


def parse_prey_header(data: bytes) -> int:
    reader = SaveHeaderReader(data)
    reader.read_save_string(MAX_STRING_CHARS)
    reader.read_int()
    reader.read_save_string(MAX_STRING_CHARS)
    reader.read_save_string(MAX_STRING_CHARS)
    for _ in range(MAX_ASYNC_CLIENTS):
        reader.read_dict()
    return reader.offset


def expect_corrupt(data: bytes, label: str) -> None:
    try:
        parse_prey_header(data)
    except CorruptSave:
        return
    raise AssertionError(f"{label} unexpectedly parsed as a valid save header")


def ssd_guard_payload(
    *,
    level_count: int = 1,
    weapon_count: int = 1,
    current_weapon: int = 0,
    current_level: int = 0,
    next_level: int = 0,
    pool_counts: dict[str, int] | None = None,
    pool_ids: dict[str, tuple[int, ...]] | None = None,
    entity_ref_count: int | None = None,
    entity_refs: tuple[tuple[int, int], ...] = (),
) -> bytes:
    pool_counts = pool_counts or {}
    pool_ids = pool_ids or {}

    data = (
        int32(level_count)
        + int32(weapon_count)
        + int32(current_weapon)
        + int32(current_level)
        + int32(next_level)
    )

    for label, _, _ in SSD_POOL_ORDER:
        ids = pool_ids.get(label, ())
        data += int32(pool_counts.get(label, len(ids)))
        for entity_id in ids:
            data += int32(entity_id)

    data += int32(len(entity_refs) if entity_ref_count is None else entity_ref_count)
    for entity_type, entity_id in entity_refs:
        data += int32(entity_type) + int32(entity_id)
    return data


def parse_ssd_guard_payload(data: bytes) -> None:
    SSDRestoreGuardReader(data).parse()


def expect_ssd_corrupt(data: bytes, label: str) -> None:
    try:
        parse_ssd_guard_payload(data)
    except CorruptSave:
        return
    raise AssertionError(f"{label} unexpectedly parsed as a valid SSD restore guard payload")


def validate_header_fuzz_model() -> None:
    valid = valid_prey_header()
    if parse_prey_header(valid) != len(valid):
        raise AssertionError("valid save header fixture was not fully consumed")

    expect_corrupt(b"\x00\x01", "truncated game-name length")
    expect_corrupt(int32(-1), "negative game-name length")
    expect_corrupt(int32(MAX_STRING_CHARS + 1) + (b"x" * (MAX_STRING_CHARS + 1)), "oversized game-name length")
    expect_corrupt(save_string(b"Prey") + int32(12) + int32(100), "map-name length beyond remaining bytes")

    header_before_dicts = (
        save_string(b"Prey")
        + int32(12)
        + save_string(b"maps/game/roadhouse.map")
        + save_string(b"")
    )
    expect_corrupt(header_before_dicts + int32(-1), "negative persistent-player dictionary count")
    expect_corrupt(
        header_before_dicts + int32(SESSION_MAX_SAVEGAME_DICT_KV + 1),
        "oversized persistent-player dictionary count",
    )
    expect_corrupt(header_before_dicts + int32(1) + b"missing-nul", "unterminated persistent-player key")
    expect_corrupt(
        header_before_dicts + int32(1) + b"key\0" + (b"value" * MAX_STRING_CHARS),
        "unterminated persistent-player value",
    )


def validate_ssd_restore_fuzz_model() -> None:
    parse_ssd_guard_payload(ssd_guard_payload())

    asteroid_type = SSD_ENTITY_TYPES["SSD_ENTITY_ASTEROID"]
    parse_ssd_guard_payload(
        ssd_guard_payload(
            pool_ids={"asteroid": (0,)},
            entity_refs=((asteroid_type, 0),),
        )
    )

    for bad_count in (-1, SSD_MAX_SAVE_LEVELS + 1):
        expect_ssd_corrupt(ssd_guard_payload(level_count=bad_count), f"bad SSD level count {bad_count}")

    for bad_count in (-1, SSD_MAX_SAVE_WEAPONS + 1):
        expect_ssd_corrupt(ssd_guard_payload(weapon_count=bad_count), f"bad SSD weapon count {bad_count}")

    expect_ssd_corrupt(ssd_guard_payload(current_level=-1), "negative SSD current level")
    expect_ssd_corrupt(ssd_guard_payload(current_level=1), "SSD current level beyond restored level count")
    expect_ssd_corrupt(ssd_guard_payload(next_level=-1), "negative SSD next level")
    expect_ssd_corrupt(ssd_guard_payload(next_level=2), "SSD next level beyond restored level count")
    expect_ssd_corrupt(ssd_guard_payload(current_weapon=-1), "negative SSD current weapon")
    expect_ssd_corrupt(ssd_guard_payload(current_weapon=1), "SSD current weapon beyond restored weapon count")

    for label, _, max_count in SSD_POOL_ORDER:
        expect_ssd_corrupt(
            ssd_guard_payload(pool_counts={label: -1}),
            f"negative SSD {label} pool count",
        )
        expect_ssd_corrupt(
            ssd_guard_payload(pool_counts={label: max_count + 1}),
            f"oversized SSD {label} pool count",
        )
        expect_ssd_corrupt(
            ssd_guard_payload(pool_ids={label: (-1,)}),
            f"negative SSD {label} pool id",
        )
        expect_ssd_corrupt(
            ssd_guard_payload(pool_ids={label: (max_count,)}),
            f"oversized SSD {label} pool id",
        )

    unknown_type = max(SSD_ENTITY_POOL_LIMITS) + 1
    expect_ssd_corrupt(
        ssd_guard_payload(entity_refs=((unknown_type, 0),)),
        "unknown SSD entity reference type",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(pool_ids={"asteroid": (0,)}, entity_refs=((asteroid_type, -1),)),
        "negative SSD entity reference id",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(pool_ids={"asteroid": (0,)}, entity_refs=((asteroid_type, SSD_MAX_ASTEROIDS),)),
        "oversized SSD entity reference id",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(entity_refs=((asteroid_type, 0),)),
        "SSD entity reference to inactive pool id",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(entity_ref_count=-1),
        "negative SSD entity reference count",
    )
    expect_ssd_corrupt(
        ssd_guard_payload(entity_ref_count=SSD_MAX_SAVE_ENTITY_REFS + 1),
        "oversized SSD entity reference count",
    )


def validate_session_source_contract() -> None:
    source = read("src/framework/Session.cpp")
    menu_source = read("src/framework/Session_menu.cpp")
    dict_source = read("src/idlib/Dict.cpp")
    sound_world_source = read("src/sound/snd_world.cpp")

    for token in (
        '#include "BuildVersion.h"',
        "static bool Session_WriteSaveGameBytes( idFile *file, const void *buffer, int len, const char *fieldName, const char *savePath )",
        "static bool Session_WriteSaveGameInt( idFile *file, int value, const char *fieldName, const char *savePath )",
        "static bool Session_WriteSaveGameString( idFile *file, const char *string, int maxLength, const char *fieldName, const char *savePath )",
        "static bool Session_WriteSaveGameCString( idFile *file, const char *string, int maxLength, const char *fieldName, const char *savePath )",
        "static bool Session_WriteSaveGameDict( idFile *file, const idDict &dict, const char *fieldName, const char *savePath )",
        "static bool Session_WriteSaveTextLine( idFile *file, const char *line, bool quoted, const char *fieldName, const char *savePath )",
        "SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC",
        "SESSION_OPENQ4_SAVEGAME_FOOTER_MAGIC",
        "SESSION_OPENQ4_SAVEGAME_FOOTER_BYTES",
        "SESSION_MAX_SAVE_DESCRIPTION_BYTES = 8192",
        "SESSION_MAX_SAVE_PREVIEW_BYTES = 64 * 1024 * 1024",
        "static bool Session_IsSafeSaveMaterialPath( const idStr &path )",
        "static bool Session_SaveDescriptionMatchesSlot( const idStr &expectedSlotName, const idStr &descriptionSaveName )",
        "static bool Session_ValidateStagedSaveDescription( const idStr &descriptionPath, const idStr &expectedSlotName, saveType_t saveType )",
        "static bool Session_ValidateStagedSavePreview( const idStr &previewPath )",
        "static bool Session_ValidateSaveGamePayload( idFile *file, const idStr &savePath, bool allowLegacyPayload )",
        "OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH",
        "OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT",
        "if ( marker != SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC )",
        "if ( valid && payloadBuild != BUILD_NUMBER )",
        "const int footerOffset = fileLength - SESSION_OPENQ4_SAVEGAME_FOOTER_BYTES;",
        "if ( valid && footerMarker != SESSION_OPENQ4_SAVEGAME_FOOTER_MAGIC )",
        "if ( valid && savedFooterOffset != footerOffset )",
        "if ( valid && !Session_SaveDescriptionMatchesSlot( expectedSlotName, sidecarSaveName ) )",
        "if ( valid && !Session_IsSafeSaveMaterialPath( sidecarScreenshot ) )",
        "if ( imageType != 2 && imageType != 10 )",
        "if ( bitsPerPixel != 24 && bitsPerPixel != 32 )",
        "Session_ValidateStagedSaveDescription( tempDescriptionFile, saveSlotFileName, saveType )",
        "Session_ValidateStagedSavePreview( tempPreviewFile )",
        "Session_RemoveRelativeSaveFile( tempPreviewFile );",
        "if ( bytesWritten != len )",
        "if ( bytesWritten != sizeof( value ) )",
        "if ( count < 0 || count > SESSION_MAX_SAVEGAME_DICT_KV )",
        "static bool Session_ReadSaveGameInt( idFile *file, int &value, const char *fieldName, const char *savePath )",
        "if ( bytesRead != sizeof( value ) )",
        "static bool Session_ReadSaveGameString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath )",
        "if ( len < 0 || len > maxLength || len > remainingBytes )",
        "if ( bytesRead != len )",
        "static bool Session_ReadSaveGameCString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath )",
        "for ( int len = 0; len < maxLength; len++ )",
        "has unterminated %s",
        "static bool Session_ReadSaveGameDict( idFile *file, idDict &dict, const char *fieldName, const char *savePath )",
        "SESSION_MAX_SAVEGAME_DICT_KV = 16384",
        "if ( count < 0 || count > SESSION_MAX_SAVEGAME_DICT_KV )",
        "Session_ReadSaveGameCString( file, key, MAX_STRING_CHARS",
        "Session_ReadSaveGameCString( file, value, MAX_STRING_CHARS",
        "idDict loadedPersistentPlayerInfo[MAX_ASYNC_CLIENTS];",
        "idFile *loadGameFile = Session_OpenSaveGameReadHandle",
        "loadingSaveGame = false;",
        "savegameFile = NULL;",
        "if ( headerValid && saveMap.IsEmpty() )",
        "mapSpawnData.persistentPlayerInfo[i] = loadedPersistentPlayerInfo[i];",
        "savegameFile = loadGameFile;",
        "inFileName[i] <= ' '",
    ):
        require(source, token, "Session savegame corruption reader contract")

    for field_name in ("game name", "map name", "entity filter"):
        require_regex(
            source,
            rf"Session_ReadSaveGameString\s*\(\s*loadGameFile,\s*[^,]+,\s*MAX_STRING_CHARS,\s*\"{field_name}\"",
            f"load savegame bounded {field_name} header read",
        )

    require(source, "if ( headerValid && !Session_IsSupportedSaveGameName( gamename ) )", "savegame header name allowlist")
    require(source, "for ( i = 0; i < MAX_ASYNC_CLIENTS; i++ )", "fixed persistent-player dictionary slots")
    require_regex(
        source,
        r"Session_ReadSaveGameDict\s*\(\s*loadGameFile,\s*loadedPersistentPlayerInfo\s*\[\s*i\s*\],"
        r"\s*va\s*\(\s*\"persistent player info %d\"",
        "bounded persistent-player dictionary reads into temporary load state",
    )
    require_regex(
        source,
        r"for\s*\(\s*i\s*=\s*0;\s*i\s*<\s*MAX_ASYNC_CLIENTS;\s*i\+\+\s*\).*?"
        r"mapSpawnData\.persistentPlayerInfo\s*\[\s*i\s*\]\s*=\s*loadedPersistentPlayerInfo\s*\[\s*i\s*\];",
        "persistent-player dictionaries are committed after full header validation",
    )
    require_regex(
        source,
        r"Session_ReadSaveGameDict\s*\(\s*loadGameFile,\s*loadedPersistentPlayerInfo.*?"
        r"Session_ValidateSaveGamePayload\s*\(\s*loadGameFile,\s*in,\s*true\s*\).*?"
        r"mapSpawnData\.persistentPlayerInfo.*?ExecuteMapChange",
        "gameplay payload is preflighted before load state is committed or the map changes",
    )
    require_regex(
        source,
        r"Session_ReadSaveGameString\s*\(\s*file,\s*gamename,\s*MAX_STRING_CHARS,\s*\"game name\".*?"
        r"Session_ReadSaveGameString\s*\(\s*file,\s*saveMap,\s*MAX_STRING_CHARS,\s*\"map name\".*?"
        r"Session_ReadSaveGameString\s*\(\s*file,\s*entityFilter,\s*MAX_STRING_CHARS,\s*\"entity filter\".*?"
        r"Session_ReadSaveGameDict\s*\(\s*file,\s*persistentPlayerInfo.*?"
        r"Session_ValidateSaveGamePayload\s*\(\s*file,\s*savePath,\s*false\s*\)",
        "staged save validation uses bounded header/dict reads and payload footer check",
    )
    require_regex(
        source,
        r"Session_WriteSaveTextLine\s*\(\s*fileDesc,\s*escapedSlotName.*?"
        r"if\s*\(\s*!descriptionWritten\s*\).*?"
        r"Session_ValidateStagedSaveDescription\s*\(\s*tempDescriptionFile,\s*saveSlotFileName,\s*saveType\s*\)",
        "staged save description write and validation cleanup",
    )
    require_regex(
        source,
        r"Session_WriteSaveGameString\s*\(\s*fileOut,\s*SAVEGAME_GAME_NAME_PREY.*?"
        r"Session_WriteSaveGameDict\s*\(\s*fileOut,\s*mapSpawnData\.persistentPlayerInfo\s*\[\s*i\s*\].*?"
        r"if\s*\(\s*!headerWritten\s*\)",
        "staged save header write failure cleanup",
    )

    for token in (
        "SESSION_MENU_MAX_SAVE_DESCRIPTION_BYTES = 8192",
        "SESSION_MENU_MAX_SAVEGAME_DICT_KV = 16384",
        "SESSION_MENU_MAX_SAVEGAME_BASENAME = 96",
        "static bool Session_MenuIsSafeSaveSlotName( const idStr &slotName )",
        "static bool Session_MenuSaveDescriptionMatchesSlot( const idStr &slotName, const idStr &descriptionSaveName )",
        "static bool Session_MenuIsSafeSaveMaterialPath( const idStr &path )",
        "sessLocal.ScrubSaveGameFileName( scrubbedName );",
        "idStr::FindText( path.c_str(), \"..\" )",
        "static bool Session_MenuReadSaveGameString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath )",
        "static bool Session_MenuReadSaveGameCString( idFile *file, idStr &string, int maxLength, const char *fieldName, const char *savePath )",
        "static bool Session_MenuSkipSaveGameDict( idFile *file, const char *fieldName, const char *savePath )",
        "static bool Session_MenuIsLoadableSaveGameSlot( const idStr &slotName, const char *gameDir )",
        "static bool Session_MenuReadSaveDescription( const idStr &slotName, sessionMenuSaveDescription_t &description, const char *gameDir = NULL )",
        "len > SESSION_MENU_MAX_SAVE_DESCRIPTION_BYTES",
        "idLexer src( buffer, len, descriptionPath.c_str(), LEXFL_NOERRORS | LEXFL_NOSTRINGCONCAT );",
        "!Session_MenuSaveDescriptionMatchesSlot( slotName, description.saveName )",
        "!Session_MenuIsSafeSaveMaterialPath( description.screenshot )",
        "!Session_MenuIsLoadableSaveGameSlot( slotName, gameDir )",
        "Session_MenuReadSaveDescription( loadGameList[i], description, loadGameListGameDirs[i].c_str() )",
        "description.screenshot.Length() > 0 || description.noOverwrite",
    ):
        require(menu_source, token, "Session savegame menu/list contract")

    for token in (
        "static void DictWriteChecked( idFile *f, int bytesWritten, int expected, const char *detail, int offset )",
        "idDict::WriteToFileHandle: failed to write %s at offset %d",
        "if ( args.Num() < 0 || args.Num() > MAX_DICT_FILE_KV )",
        "DictWriteChecked( f, f->Write( s, len + 1 ), len + 1, \"string\", offset );",
        "DictWriteChecked( f, f->Write( &c, sizeof( c ) ), static_cast<int>( sizeof( c ) ), \"key/value count\", offset );",
    ):
        require(dict_source, token, "idDict savegame write contract")

    for token in (
        "SOUND_SAVEGAME_MAX_EMITTERS = 8192",
        "SOUND_SAVEGAME_MAX_TOTAL_CHANNELS = 8192",
        "static void WriteChecked( idFile* savefile, int bytesWritten, int expected, const char* fieldName, int offset )",
        "idSoundWorldLocal::WriteToSaveGame: failed to write %s at offset %d",
        "while( num > 1 )",
        "const int restoredIndex = emitters.Append( emitter );",
        "Do not use AllocSoundEmitter here",
        "class idSoundStateReader",
        "bool ReadInt( int& value, const char* fieldName )",
        "common->Error( \"idSoundWorldLocal::ReadFromSaveGame: %s\", detail );",
        "has a truncated %s",
        "bool ReadString( idStr& value, const char* fieldName )",
        "len < 0 || len > MAX_STRING_CHARS",
        "parms.soundClass < 0 || parms.soundClass >= SOUND_MAX_CLASSES",
        "numEmitters < 1 || numEmitters > SOUND_SAVEGAME_MAX_EMITTERS",
        "numChannels < 0 || numChannels > MAX_CHANNELS_PER_EMITTER",
        "totalChannels > SOUND_SAVEGAME_MAX_TOTAL_CHANNELS - numChannels",
        "idSoundStateReader reader( savefile, demoFile );",
        "reader.ReadString( shaderName, \"sound shader name\" )",
    ):
        require(sound_world_source, token, "sound-world savegame restore corruption contract")


def validate_gamelibs_save_payload_contract() -> None:
    # Prey uses one unified game module for both single-player and multiplayer.
    for module in ("game",):
        save_header = read_game_libs(f"src/{module}/gamesys/SaveGame.h")
        save_source = read_game_libs(f"src/{module}/gamesys/SaveGame.cpp")
        game_header = read_game_libs(f"src/{module}/Game_local.h")
        game_local = read_game_libs(f"src/{module}/Game_local.cpp")
        actor_source = read_game_libs(f"src/{module}/Actor.cpp")
        misc_source = read_game_libs(f"src/{module}/Misc.cpp")
        mover_source = read_game_libs(f"src/{module}/Mover.cpp")
        physics_af = read_game_libs(f"src/{module}/physics/Physics_AF.cpp")
        script_interpreter = read_game_libs(f"src/{module}/script/Script_Interpreter.cpp")
        script_program = read_game_libs(f"src/{module}/script/Script_Program.h")
        script_program_source = read_game_libs(f"src/{module}/script/Script_Program.cpp")

        for token in (
            "OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'S' << 24 )",
            "OPENQ4_SAVEGAME_COMPATIBILITY_VERSION = 2",
            "OPENQ4_SAVEGAME_SYNC_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'Y' << 24 )",
            "OPENQ4_SAVEGAME_FOOTER_MAGIC = 'O' | ( 'Q' << 8 ) | ( '4' << 16 ) | ( 'F' << 24 )",
            '__has_include( "openq4_savegame_compat_generated.h" )',
            '#define OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH "standalone-openprey-game"',
            "void\t\t\t\t\tWriteChecked( int bytesWritten, int expected, const char *detail, int offset );",
            "void\t\t\t\t\tWriteSaveGameFooter( int numObjects );",
            "void\t\t\t\t\tReadSyncId( const char *detail = \"unspecified\", const char *classname = NULL );",
            "void\t\t\t\t\tReadSaveGameFooter( void );",
            "bool\t\t\t\t\tIsOpenQ4SaveGameCompatible( void ) const;",
            "const char *\t\t\tGetOpenQ4SaveGameCompatibilityError( void ) const;",
            "int\t\t\t\t\t\topenQ4SaveGameCompatibilitySourceFileCount;",
            "int\t\t\t\t\t\topenQ4SaveGameNextSyncId;",
            "bool\t\t\t\t\topenQ4SaveGameSyncMarkersEnabled;",
            "idStr\t\t\t\t\topenQ4SaveGameCompatibilityStamp;",
            "idStr\t\t\t\t\topenQ4SaveGameCompatibilityError;",
            "idHashIndex\t\t\t\tobjectHash;",
        ):
            require(save_header, token, f"{module} savegame compatibility header")

        for token in (
            '#include "../../framework/BuildVersion.h"',
            "static bool SaveGame_IsValidRenderBounds( const idBounds &bounds )",
            "static int SaveGame_ObjectHashKey( const idClass *obj )",
            "static int SaveGame_FindObjectIndex( const idList<const idClass *> &objects, const idHashIndex &objectHash, const idClass *obj )",
            "objectHash.Add( SaveGame_ObjectHashKey( obj ), index );",
            "const float boundsMin = bounds[0][i];",
            "const float boundsMax = bounds[1][i];",
            "FLOAT_IS_NAN( boundsMin ) || FLOAT_IS_NAN( boundsMax )",
            "boundsMax - boundsMin >= MAX_BOUND_SIZE",
            "void idSaveGame::WriteChecked( int bytesWritten, int expected, const char *detail, int offset )",
            "idSaveGame: failed to write %s at offset %d",
            "WriteChecked( file->WriteInt( value ), static_cast<int>( sizeof( value ) ), \"int\", offset );",
            "WriteChecked( file->Write( string, len ), len, \"string\", offset );",
            "idSaveGame::WriteWinding: invalid point count %d",
            "idSaveGame::WriteDict: invalid key/value count %d (max %d)",
            "WriteInt( OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC );",
            "WriteInt( OPENQ4_SAVEGAME_COMPATIBILITY_VERSION );",
            "WriteString( OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH );",
            "WriteInt( OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT );",
            "WriteInt( OPENQ4_SAVEGAME_SYNC_MAGIC );",
            "void idSaveGame::WriteSaveGameFooter( int numObjects )",
            "WriteInt( OPENQ4_SAVEGAME_FOOTER_MAGIC );",
            "ReadInt( marker );",
            "if ( marker != OPENQ4_SAVEGAME_COMPATIBILITY_MAGIC )",
            "if ( buildNumber == BUILD_NUMBER )",
            "legacy save payload build %d does not match current build %d",
            "ReadInt( openQ4SaveGameCompatibilityVersion );",
            "ReadString( openQ4SaveGameCompatibilityStamp );",
            "ReadInt( openQ4SaveGameCompatibilitySourceFileCount );",
            "openQ4SaveGameCompatibilityVersion != OPENQ4_SAVEGAME_COMPATIBILITY_VERSION",
            "payload build %d does not match current build %d",
            "openQ4SaveGameCompatibilityStamp.Icmp( OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH ) != 0",
            "source snapshot %s does not match current snapshot %s",
            "openQ4SaveGameCompatibilitySourceFileCount != OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT",
            "void idRestoreGame::ReadSyncId( const char *detail, const char *classname )",
            "marker mismatch while reading %s%s%s",
            "sequence mismatch while reading %s%s%s",
            "void idRestoreGame::ReadSaveGameFooter( void )",
            "OPENQ4_SAVEGAME_FOOTER_MAGIC",
            "unexpected trailing bytes after savegame payload",
            "bool idRestoreGame::IsOpenQ4SaveGameCompatible( void ) const",
            "const char *idRestoreGame::GetOpenQ4SaveGameCompatibilityError( void ) const",
            "!SaveGame_IsValidRenderBounds( renderEntity.bounds )",
            "idRestoreGame::ReadRenderEntity: invalid render bounds",
        ):
            require(save_source, token, f"{module} savegame compatibility source")

        require_regex(
            game_local,
            r"savegame\.ReadBuildNumber\(\);.*?"
            r"if\s*\(\s*!savegame\.IsOpenQ4SaveGameCompatible\(\)\s*\).*?"
            r"GetOpenQ4SaveGameCompatibilityError\(\).*?"
            r"return false;.*?"
            r"savegame\.CreateObjects\(\);",
            f"{module} savegame compatibility gate before object restore",
        )

        require(
            script_program,
            "NumFunctions( void ) { return functions.Num(); }",
            f"{module} script program function-count accessor",
        )

        for token in (
            "if ( callStackDepth < 0 || callStackDepth > MAX_STACK_DEPTH )",
            "callStack[i].s < -1 || callStack[i].s >= gameLocal.program.NumStatements()",
            "func_index >= gameLocal.program.NumFunctions()",
            "func_index == -1",
            "callStack[i].stackbase < 0 || callStack[i].stackbase > LOCALSTACK_SIZE",
            "maxStackDepth < callStackDepth || maxStackDepth > MAX_STACK_DEPTH",
            "localstackUsed < 0 || localstackUsed > LOCALSTACK_SIZE",
            "memset( localstack, 0, sizeof( localstack ) );",
            "savefile->Read( localstack, localstackUsed );",
            "localstackBase < 0 || localstackBase > localstackUsed",
            "maxLocalstackUsed < localstackUsed || maxLocalstackUsed > LOCALSTACK_SIZE",
            "instructionPointer < -1 || instructionPointer >= gameLocal.program.NumStatements()",
            "popParms < 0 || popParms > localstackUsed",
            "unknown multi-frame event",
            "multiFrameEvent = NULL;",
        ):
            require(script_interpreter, token, f"{module} script interpreter restore bounds contract")

        for token in (
            "MAX_SAVEGAME_ANIMATED_ANIMS",
            "MAX_SAVEGAME_ACTOR_ATTACHMENTS",
            "MAX_SAVEGAME_ELEVATOR_FLOORS",
        ):
            require(game_header, token, f"{module} savegame scalar/list limits")

        for token in (
            "channel < ANIMCHANNEL_ALL || channel >= ANIM_NumAnimChannels",
            "idAnimState::Restore: invalid animation channel %d",
            "idActor::Restore: invalid attachment animation channel %d",
        ):
            require(actor_source, token, f"{module} actor animation channel restore bounds")

        for token in (
            "num_anims < 0 || num_anims > MAX_SAVEGAME_ANIMATED_ANIMS",
            "current_anim_index < 0 || current_anim_index > num_anims",
            "anim < 0 || anim > animator.NumAnims()",
        ):
            require(misc_source, token, f"{module} idAnimated restore bounds")

        for token in (
            "savedState < INIT || savedState > WAITING_ON_DOORS",
            "idElevator::Restore: invalid elevator state %d",
            "num < 0 || num > MAX_SAVEGAME_ELEVATOR_FLOORS",
            "idElevator::Restore: invalid floor count %d",
        ):
            require(mover_source, token, f"{module} elevator restore bounds")

        for token in (
            "idPhysics_AF::Restore: articulated body count mismatch %d (expected %d)",
            "idPhysics_AF::Restore: articulated constraint count mismatch %d (expected %d)",
        ):
            require(physics_af, token, f"{module} articulated physics restore count contract")

        for token in (
            "num < 0 || num > 4096",
            "index >= variableDefaults.Num() || index >= MAX_GLOBALS",
            "if ( index != -1 )",
            "num < variableDefaults.Num() || num > MAX_GLOBALS",
            "numVariables = num;",
        ):
            require(script_program_source, token, f"{module} Prey script program restore bounds")

    meson = read("meson.build")
    for token in (
        "generate_savegame_compat_header.py",
        "openq4_savegame_compat_generated.h",
        "savegame_compat_header = custom_target",
        "game_sources += [savegame_compat_header]",
        "openq4_engine_sources += [savegame_compat_header]",
        "depend_files: files(engine_source_paths) + game_sources",
    ):
        require(meson, token, "Meson savegame compatibility header wiring")

    generator = read("tools/build/generate_savegame_compat_header.py")
    for token in (
        "RELEVANCE_TOKENS",
        "normalize_source_bytes",
        'data.replace(b"\\r\\n", b"\\n").replace(b"\\r", b"\\n")',
        "OPENQ4_SAVEGAME_COMPAT_SOURCE_HASH",
        "OPENQ4_SAVEGAME_COMPAT_SOURCE_FILE_COUNT",
        "write_if_changed",
        "PROJECT_SCAN_DIRS",
        "GAME_SCAN_DIRS",
        '"src/game"',
        '"src/Prey"',
        '"src/preyengine"',
    ):
        require(generator, token, "savegame compatibility header generator")

    savegame_docs = read("docs/dev/savegame-reliability.md")
    for token in (
        "generated GameLib source-snapshot stamp and source-file count",
        "class-boundary sync markers",
        "footer containing the final payload offset",
    ):
        require(savegame_docs, token, "savegame reliability notes")


def validate_savegame_compat_stamp_model() -> None:
    generator_path = ROOT / "tools" / "build" / "generate_savegame_compat_header.py"
    spec = importlib.util.spec_from_file_location("openq4_savegame_compat_generator", generator_path)
    if spec is None or spec.loader is None:
        raise AssertionError("Could not load savegame compatibility generator")
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)

    temp_root = ROOT / ".tmp"
    temp_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="savegame-compat-stamp-", dir=temp_root) as raw_temp:
        work = Path(raw_temp)
        project_root = work / "openPREY"
        game_root = work / "stage"
        project_file = project_root / "src" / "framework" / "Session.cpp"
        game_file = game_root / "src" / "game" / "Game_local.cpp"
        project_file.parent.mkdir(parents=True)
        game_file.parent.mkdir(parents=True)

        project_file.write_bytes(b"void SaveGame();\n")
        game_file.write_bytes(b"void Restore();\n")
        lf_hash, lf_count = generator.build_digest(project_root, game_root)

        project_file.write_bytes(b"void SaveGame();\r\n")
        game_file.write_bytes(b"void Restore();\r\n")
        crlf_hash, crlf_count = generator.build_digest(project_root, game_root)
        if (crlf_hash, crlf_count) != (lf_hash, lf_count):
            raise AssertionError("Savegame compatibility stamp varies with checkout line endings")

        game_file.write_bytes(b"void Restore();\nvoid Save();\n")
        changed_hash, changed_count = generator.build_digest(project_root, game_root)
        if changed_hash == lf_hash or changed_count != lf_count:
            raise AssertionError("Savegame compatibility stamp did not track a relevant semantic source change")


def validate_minigame_restore_contract() -> None:
    ui_restore_contracts = {
        "src/ui/Winvar.h": (
            "static ID_INLINE void OpenQ4_ReadSaveGameBytes( idFile *savefile, void *buffer, int len, const char *context, const char *fieldName )",
            "static ID_INLINE void OpenQ4_ReadSaveGameField( idFile *savefile, type &value, const char *context, const char *fieldName )",
            "common->Error( \"%s: truncated %s at offset %d (read %d of %d)\"",
            "OpenQ4_ReadSaveGameBytes( savefile, &eval, sizeof( eval ), \"idWinBool::ReadFromSaveGame\", \"eval flag\" )",
            "OpenQ4_ReadSaveGameBytes( savefile, &data[0], len, \"idWinStr::ReadFromSaveGame\", \"string\" )",
            "OpenQ4_ReadSaveGameBytes( savefile, &data[0], len, \"idWinBackground::ReadFromSaveGame\", \"material name\" )",
        ),
        "src/ui/Window.cpp": (
            "OpenQ4_ReadSaveGameField( savefile, offset, \"idWindow::ReadSaveGameTransition\", \"offset\" )",
            "OpenQ4_ReadSaveGameField( savefile, trans.interp, \"idWindow::ReadSaveGameTransition\", \"interpolate state\" )",
            "OpenQ4_ReadSaveGameBytes( savefile, &string[0], len, \"idWindow::ReadSaveGameString\", \"string\" )",
            "OpenQ4_ReadSaveGameField( savefile, actualX, \"idWindow::ReadFromSaveGame\", \"actualX\" )",
            "focusedChild = NULL;",
            "captureChild = NULL;",
            "overChild = NULL;",
            "OpenQ4_ReadSaveGameField( savefile, timeLineEvents[i]->pending, \"idWindow::ReadFromSaveGame\", \"timeline pending flag\" )",
            "OpenQ4_ReadSaveGameField( savefile, num, \"idWindow::ReadFromSaveGame\", \"transition count\" )",
        ),
        "src/ui/SimpleWindow.cpp": (
            "OpenQ4_ReadSaveGameField( savefile, flags, \"idSimpleWindow::ReadFromSaveGame\", \"flags\" )",
            "OpenQ4_ReadSaveGameField( savefile, stringLen, \"idSimpleWindow::ReadFromSaveGame\", \"background length\" )",
            "OpenQ4_ReadSaveGameBytes( savefile, &(backName)[0], stringLen, \"idSimpleWindow::ReadFromSaveGame\", \"background name\" )",
        ),
        "src/ui/GuiScript.cpp": (
            "OpenQ4_ReadSaveGameField( savefile, conditionReg, \"idGuiScript::ReadFromSaveGame\", \"condition register\" )",
        ),
    }
    for relative_path, tokens in ui_restore_contracts.items():
        source = read(relative_path)
        for token in tokens:
            require(source, token, f"UI savegame restore short-read guard in {relative_path}")

    ui_raw_read_regressions = {
        "src/ui/Winvar.h": (
            "savefile->Read( &eval",
            "savefile->Read( &data",
        ),
        "src/ui/Window.cpp": (
            "savefile->Read( &actualX",
            "savefile->Read( &drawRect",
            "savefile->Read( &winID",
            "savefile->Read( &timeLineEvents",
            "savefile->Read( &num, sizeof( num )",
        ),
        "src/ui/SimpleWindow.cpp": (
            "savefile->Read( &flags",
            "savefile->Read( &drawRect",
            "savefile->Read( &stringLen",
        ),
        "src/ui/GuiScript.cpp": (
            "savefile->Read( &conditionReg",
        ),
    }
    for relative_path, needles in ui_raw_read_regressions.items():
        source = read(relative_path)
        for needle in needles:
            if needle in source:
                raise AssertionError(f"UI savegame restore field bypasses checked read helper: {needle!r} in {relative_path}")

    minigame_contracts = {
        "src/ui/GameBustOutWindow.cpp": (
            "GAME_BUSTOUT_MAX_SAVE_ENTITIES = 512",
            "GAME_BUSTOUT_SAVE_MAGIC = 'O' | ( 'Q' << 8 ) | ( 'B' << 16 ) | ( 'O' << 24 )",
            "GAME_BUSTOUT_SAVE_VERSION = 1",
            "GameBustOut_WriteSaveInt",
            "savefile->WriteInt( value );",
            "GameBustOut_ReadLegacySaveInt",
            "GameBustOut_ReadEndianSaveInt",
            "savefile->ReadInt( value )",
            "GameBustOut_ReadSaveInt",
            "GameBustOut_ReadSaveVersion",
            "savefile->Seek( markerOffset, FS_SEEK_SET );",
            "unsupported format version",
            "GameBustOut_WriteSaveInt( savefile, GAME_BUSTOUT_SAVE_MAGIC );",
            "GameBustOut_WriteSaveInt( savefile, GAME_BUSTOUT_SAVE_VERSION );",
            "if ( bytesRead != sizeof( value ) )",
            "GameBustOut_ValidateSaveCount",
            "count < 0 || count > GAME_BUSTOUT_MAX_SAVE_ENTITIES",
            "GameBustOut_ValidateEntityIndex",
            "index < 0 || index >= entityCount",
            "GameBustOut_ReadSaveBlock",
            "GameBustOut_ReadSaveField",
            "GameBustOut_ReadSavePowerup",
            "value < POWERUP_NONE || value > POWERUP_MULTIBALL",
            "powerup = GameBustOut_ReadSavePowerup( savefile, \"entity powerup\", saveVersion );",
            "GameBustOut_ReadSaveField( savefile, ballSpeed, \"ball speed\" );",
            "ball entity index",
            "powerup entity index",
            "brick entity index",
        ),
        "src/ui/GameBearShootWindow.cpp": (
            "GAME_BEARSHOOT_MAX_SAVE_ENTITIES = 64",
            "GAME_BEARSHOOT_SAVE_MAGIC = 'O' | ( 'Q' << 8 ) | ( 'B' << 16 ) | ( 'S' << 24 )",
            "GAME_BEARSHOOT_SAVE_VERSION = 1",
            "GameBearShoot_WriteSaveInt",
            "savefile->WriteInt( value );",
            "GameBearShoot_ReadLegacySaveInt",
            "GameBearShoot_ReadEndianSaveInt",
            "savefile->ReadInt( value )",
            "GameBearShoot_ReadSaveInt",
            "GameBearShoot_ReadSaveVersion",
            "savefile->Seek( markerOffset, FS_SEEK_SET );",
            "unsupported format version",
            "GameBearShoot_WriteSaveInt( savefile, GAME_BEARSHOOT_SAVE_MAGIC );",
            "GameBearShoot_WriteSaveInt( savefile, GAME_BEARSHOOT_SAVE_VERSION );",
            "if ( bytesRead != sizeof( value ) )",
            "GameBearShoot_ValidateSaveCount",
            "count < 0 || count > GAME_BEARSHOOT_MAX_SAVE_ENTITIES",
            "GameBearShoot_ValidateEntityIndex",
            "index < 0 || index >= entityCount",
            "GameBearShoot_ReadSaveBlock",
            "GameBearShoot_ReadSaveField",
            "GameBearShoot_ReadSaveField( savefile, timeRemaining, \"time remaining\" );",
            "GameBearShoot_ReadSaveField( savefile, windForce, \"wind force\" );",
            "turret entity index",
            "bear entity index",
            "gunblast entity index",
        ),
        "src/ui/GameSSDWindow.cpp": (
            "GAME_SSD_SAVE_MAGIC = 'O' | ( 'Q' << 8 ) | ( 'S' << 16 ) | ( 'D' << 24 )",
            "GAME_SSD_SAVE_VERSION = 1",
            "GAME_SSD_MAX_SAVE_LEVELS = 64",
            "GAME_SSD_MAX_SAVE_WEAPONS = 16",
            "GAME_SSD_MAX_SAVE_ENTITY_REFS = MAX_ASTEROIDS + MAX_ASTRONAUT + MAX_EXPLOSIONS + MAX_POINTS + MAX_PROJECTILES + MAX_POWERUPS",
            "GameSSD_WriteSaveInt",
            "savefile->WriteInt( value );",
            "GameSSD_ReadLegacySaveInt",
            "GameSSD_ReadLegacySaveBlock",
            "truncated legacy %s",
            "GameSSD_ReadEndianSaveInt",
            "savefile->ReadInt( value )",
            "GameSSD_ReadSaveInt",
            "GameSSD_ReadSaveCount",
            "GameSSD_ReadSavePoolId",
            "GameSSD_ValidateSaveCount",
            "GameSSD_ValidatePoolId",
            "GameSSD_ValidateEntityType",
            "GameSSD_ValidateExpectedEntityType",
            "GameSSD_ValidateEntityReference",
            "GameSSD_ReadSaveVersion",
            "savefile->Seek( markerOffset, FS_SEEK_SET );",
            "unsupported format version",
            "GameSSD_WriteSaveInt( savefile, GAME_SSD_SAVE_MAGIC );",
            "GameSSD_WriteSaveInt( savefile, GAME_SSD_SAVE_VERSION );",
            "GameSSD_WriteLevelData",
            "GameSSD_ReadLevelData",
            "GameSSD_WriteAsteroidData",
            "GameSSD_ReadAsteroidData",
            "GameSSD_WriteAstronautData",
            "GameSSD_ReadAstronautData",
            "GameSSD_WritePowerupData",
            "GameSSD_ReadPowerupData",
            "GameSSD_WriteWeaponData",
            "GameSSD_ReadWeaponData",
            "GameSSD_WriteGameStats",
            "GameSSD_ReadGameStats",
            "GameSSD_ReadSaveBlock",
            "GameSSD_ReadSaveField",
            "GameSSD_WriteSaveInt( savefile, entCount );",
            "explosionPool[i].id = i;",
            "pointsPool[i].id = i;",
            "projectilePool[i].id = i;",
            "powerupPool[i].id = i;",
            "GameSSD_ReadSaveCount( savefile, \"asteroid count\", MAX_ASTEROIDS );",
            "GameSSD_ReadSavePoolId( savefile, \"asteroid id\", MAX_ASTEROIDS );",
            "GameSSD_ReadSaveCount( savefile, \"astronaut count\", MAX_ASTRONAUT );",
            "GameSSD_ReadSavePoolId( savefile, \"astronaut id\", MAX_ASTRONAUT );",
            "GameSSD_ReadSaveCount( savefile, \"explosion count\", MAX_EXPLOSIONS );",
            "GameSSD_ReadSavePoolId( savefile, \"explosion id\", MAX_EXPLOSIONS );",
            "GameSSD_ReadSaveCount( savefile, \"points count\", MAX_POINTS );",
            "GameSSD_ReadSavePoolId( savefile, \"points id\", MAX_POINTS );",
            "GameSSD_ReadSaveCount( savefile, \"projectile count\", MAX_PROJECTILES );",
            "GameSSD_ReadSavePoolId( savefile, \"projectile id\", MAX_PROJECTILES );",
            "GameSSD_ReadSaveCount( savefile, \"powerup count\", MAX_POWERUPS );",
            "GameSSD_ReadSavePoolId( savefile, \"powerup id\", MAX_POWERUPS );",
            "GameSSD_ReadSaveCount( savefile, \"level count\", GAME_SSD_MAX_SAVE_LEVELS );",
            "GameSSD_ReadSaveCount( savefile, \"weapon count\", GAME_SSD_MAX_SAVE_WEAPONS );",
            "GameSSD_ReadSaveCount( savefile, \"entity reference count\", GAME_SSD_MAX_SAVE_ENTITY_REFS );",
            "GameSSD_ValidateSaveIndex( \"crosshair index\", currentCrosshair, SSDCrossHair::CROSSHAIR_COUNT );",
            "GameSSD_ValidateExpectedEntityType( \"asteroid entity\", type, SSD_ENTITY_ASTEROID );",
            "GameSSD_ValidateExpectedEntityType( \"astronaut entity\", type, SSD_ENTITY_ASTRONAUT );",
            "GameSSD_ValidateExpectedEntityType( \"explosion entity\", type, SSD_ENTITY_EXPLOSION );",
            "GameSSD_ValidateExpectedEntityType( \"points entity\", type, SSD_ENTITY_POINTS );",
            "GameSSD_ValidateExpectedEntityType( \"projectile entity\", type, SSD_ENTITY_PROJECTILE );",
            "GameSSD_ValidateExpectedEntityType( \"powerup entity\", type, SSD_ENTITY_POWERUP );",
            "GameSSD_ValidateSaveIndex( \"explosion type\", explosionType, EXPLOSION_MATERIAL_COUNT );",
            "GameSSD_ValidateSaveIndex( \"powerup state\", powerupState, POWERUP_STATE_OPEN + 1 );",
            "GameSSD_ValidateSaveIndex( \"powerup type\", powerupType, POWERUP_TYPE_MAX );",
            "GameSSD_ValidateSaveIndex( \"current level\", gameStats.currentLevel, levelCount );",
            "GameSSD_ValidateSaveIndexOrEnd( \"next level\", gameStats.nextLevel, levelCount );",
            "GameSSD_ValidateSaveIndex( \"current weapon\", gameStats.currentWeapon, weaponCount );",
            "GameSSD_ValidateEntityReference( \"explosion buddy\", type, id );",
            "type = GameSSD_ReadSaveInt( savefile, \"entity reference type\" );",
            "id = GameSSD_ReadSaveInt( savefile, \"entity reference id\" );",
            "GameSSD_ValidateEntityReference( \"entity reference\", type, id );",
            "if( ent == NULL || !ent->inUse )",
            "GameSSD_ReadSaveField( savefile, position, \"entity position\" );",
            "GameSSD_ReadSaveField( savefile, finalSize, \"explosion final size\" );",
            "GameSSD_ReadSaveField( savefile, dir, \"projectile direction\" );",
            "GameSSD_ReadSaveField( savefile, screenBounds, \"screen bounds\" );",
        ),
    }
    for relative_path, tokens in minigame_contracts.items():
        source = read(relative_path)
        for token in tokens:
            require(source, token, f"minigame save restore corruption guard in {relative_path}")

    raw_integer_regressions = {
        "src/ui/GameBustOutWindow.cpp": (
            "savefile->Write( &numLevels",
            "savefile->Write( &numBricks",
            "savefile->Write( &currentLevel",
            "savefile->Write( &gameScore",
            "savefile->Write( &nextBallScore",
            "savefile->Write( &bigPaddleTime",
            "savefile->Write( &ballsRemaining",
            "savefile->Write( &ballsInPlay",
            "savefile->Write( &numberOfEnts",
            "savefile->Write( &ballIndex",
            "savefile->Write( &powerIndex",
            "savefile->Write( &index",
            "savefile->Write( &powerup",
            "savefile->Read( &numLevels",
            "savefile->Read( &numBricks",
            "savefile->Read( &currentLevel",
            "savefile->Read( &gameScore",
            "savefile->Read( &nextBallScore",
            "savefile->Read( &bigPaddleTime",
            "savefile->Read( &ballsRemaining",
            "savefile->Read( &ballsInPlay",
            "savefile->Read( &powerup",
        ),
        "src/ui/GameBearShootWindow.cpp": (
            "savefile->Write( &currentLevel",
            "savefile->Write( &goalsHit",
            "savefile->Write( &bearShrinkStartTime",
            "savefile->Write( &windUpdateTime",
            "savefile->Write( &numberOfEnts",
            "savefile->Write( &index",
            "savefile->Read( &currentLevel",
            "savefile->Read( &goalsHit",
            "savefile->Read( &bearShrinkStartTime",
            "savefile->Read( &windUpdateTime",
        ),
        "src/ui/GameSSDWindow.cpp": (
            "savefile->Write(&currentCrosshair",
            "savefile->Read(&currentCrosshair",
            "savefile->Write(&type",
            "savefile->Read(&type",
            "savefile->Write(&currentTime",
            "savefile->Read(&currentTime",
            "savefile->Write(&lastUpdate",
            "savefile->Read(&lastUpdate",
            "savefile->Write(&elapsed",
            "savefile->Read(&elapsed",
            "savefile->Write(&health",
            "savefile->Read(&health",
            "savefile->Write(&count",
            "savefile->Read(&count",
            "savefile->Write(&id",
            "savefile->Read(&id",
            "savefile->Write(&length",
            "savefile->Read(&length",
            "savefile->Write(&beginTime",
            "savefile->Read(&beginTime",
            "savefile->Write(&endTime",
            "savefile->Read(&endTime",
            "savefile->Write(&explosionType",
            "savefile->Read(&explosionType",
            "savefile->Write(&distance",
            "savefile->Read(&distance",
            "savefile->Write(&powerupState",
            "savefile->Read(&powerupState",
            "savefile->Write(&powerupType",
            "savefile->Read(&powerupType",
            "savefile->Write(&ssdTime",
            "savefile->Read(&ssdTime",
            "savefile->Write(&levelCount",
            "savefile->Read(&levelCount",
            "savefile->Write(&weaponCount",
            "savefile->Read(&weaponCount",
            "savefile->Write(&superBlasterTimeout",
            "savefile->Read(&superBlasterTimeout",
            "savefile->Write(&gameStats",
            "savefile->Read(&gameStats",
            "savefile->Write(&entCount",
            "savefile->Read(&entCount",
            "savefile->Write(&(levelData[i])",
            "savefile->Read(&newLevel",
            "savefile->Write(&(asteroidData[i])",
            "savefile->Read(&newAsteroid",
            "savefile->Write(&(astronautData[i])",
            "savefile->Read(&newAstronaut",
            "savefile->Write(&(powerupData[i])",
            "savefile->Read(&newPowerup",
            "savefile->Write(&(weaponData[i])",
            "savefile->Read(&newWeapon",
        ),
    }
    for relative_path, needles in raw_integer_regressions.items():
        source = read(relative_path)
        for needle in needles:
            if needle in source:
                raise AssertionError(f"Minigame integer field still uses raw host-order IO: {needle!r} in {relative_path}")

    raw_block_regressions = {
        "src/ui/GameBustOutWindow.cpp": (
            "savefile->Read( &visible",
            "savefile->Read( &width",
            "savefile->Read( &color",
            "savefile->Read( &paddleVelocity",
            "savefile->Read( &ballSpeed",
        ),
        "src/ui/GameBearShootWindow.cpp": (
            "savefile->Read( &width",
            "savefile->Read( &entColor",
            "savefile->Read( &timeRemaining",
            "savefile->Read( &bearScale",
            "savefile->Read( &windForce",
        ),
        "src/ui/GameSSDWindow.cpp": (
            "savefile->Read(&position",
            "savefile->Read(&matColor",
            "savefile->Read(&finalSize",
            "savefile->Read(&beginPosition",
            "savefile->Read(&dir",
            "savefile->Read(&screenBounds",
        ),
    }
    for relative_path, needles in raw_block_regressions.items():
        source = read(relative_path)
        for needle in needles:
            if needle in source:
                raise AssertionError(f"Minigame save field bypasses checked block reader: {needle!r} in {relative_path}")

    ssd_source = read("src/ui/GameSSDWindow.cpp")
    unchecked_ssd_restore_fields = (
        'count = GameSSD_ReadSaveInt( savefile, "asteroid count" );',
        'id = GameSSD_ReadSaveInt( savefile, "asteroid id" );',
        'count = GameSSD_ReadSaveInt( savefile, "astronaut count" );',
        'id = GameSSD_ReadSaveInt( savefile, "astronaut id" );',
        'count = GameSSD_ReadSaveInt( savefile, "explosion count" );',
        'id = GameSSD_ReadSaveInt( savefile, "explosion id" );',
        'count = GameSSD_ReadSaveInt( savefile, "points count" );',
        'id = GameSSD_ReadSaveInt( savefile, "points id" );',
        'count = GameSSD_ReadSaveInt( savefile, "projectile count" );',
        'id = GameSSD_ReadSaveInt( savefile, "projectile id" );',
        'count = GameSSD_ReadSaveInt( savefile, "powerup count" );',
        'id = GameSSD_ReadSaveInt( savefile, "powerup id" );',
        'levelCount = GameSSD_ReadSaveInt( savefile, "level count" );',
        'weaponCount = GameSSD_ReadSaveInt( savefile, "weapon count" );',
        'entCount = GameSSD_ReadSaveInt( savefile, "entity reference count" );',
    )
    for needle in unchecked_ssd_restore_fields:
        if needle in ssd_source:
            raise AssertionError(f"SSD restore field bypasses bounds helper: {needle!r}")
    if ssd_source.count("ent->id = id;") < 6:
        raise AssertionError("SSD pool restores must stamp each restored pool object's saved id")


def validate_validation_wiring() -> None:
    for relative_path in (
        "tools/validation/openq4_validate.py",
        ".github/workflows/push-verification.yml",
        ".github/workflows/commit-validation.yml",
    ):
        require(read(relative_path), "savegame_corruption_contract.py", f"{relative_path} wiring")


def main() -> None:
    validate_header_fuzz_model()
    validate_ssd_restore_fuzz_model()
    validate_session_source_contract()
    validate_gamelibs_save_payload_contract()
    validate_savegame_compat_stamp_model()
    validate_minigame_restore_contract()
    validate_validation_wiring()
    print("savegame_corruption_contract: ok")


if __name__ == "__main__":
    main()
