#!/usr/bin/env python3
"""Guards the multiplayer bot personality contract.

A bot's difficulty, play style and voice are content, not code: three layers of
text file resolve into one flat botTraits_t that the combat code reads.  That
only works while a set of agreements holds between the engine repo, the game
repo and the shipped content, and none of them is anything a compiler can see:

  * The character manager is heap-owning game state and is initialised and shut
    down from idGameLocal.  Prey's existing hhArtificialPlayer remains the
    network-compatible player entity; personalities do not change game API 7.
  * Content is read with DECL_LEXER_FLAGS, whose LEXFL_NOFATALERRORS is what
    makes a mod's malformed .bot file a warning instead of a dead server, and
    every ListFiles is paired with a FreeFileList, because the list crosses the
    game module's allocator boundary.
  * The trait field table, the baseline curve and botTraits_t agree.  They are
    three parallel lists in two files; nothing but this notices when one of them
    grows a row and the others do not.
  * The baseline curve actually gets better as the skill number rises.  A sign
    flipped in one row of 48 is invisible in review and produces a skill 5
    bot that reacts more slowly than a skill 1 bot.
  * The 19 shipped characters map exactly to retail player.def model slots,
    name one of six reachable styles, and own one separate .chat file.  Every
    voice has eight alternatives for 14 events plus the same nine bounded reply
    intents.  Dialogue may not leak back into mechanics-only .bot files, and no
    line may violate the broadcast token or length contract.
  * Bot chat passes a throttle first.  The engine has no chat flood protection
    anywhere, and a client whose reliable queue overflows is dropped, so
    unthrottled bot chatter can kick real players off a server.

The .style, .bot and .chat files are parsed here by an independent reader
rather than by importing anything from the game, so a parser bug and a content
bug cannot cancel each other out.
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(
    os.environ.get("OPENPREY_GAMELIBS_REPO", ROOT.parent / "OpenPrey-game")
).resolve()

BOTFILES = ROOT / "content" / "basepr" / "pak0" / "botfiles"
DOC = ROOT / "docs" / "dev" / "mp-bots.md"
GAME_SOURCE = GAME_LIBS_ROOT / "src" / "game"
PREY_SOURCE = GAME_LIBS_ROOT / "src" / "Prey"

EXPECTED_ROSTER = {
    "Tommy": (0, (3, 5)),
    "Mutilated Human": (1, (1, 3)),
    "Chuck": (2, (1, 3)),
    "Dalton": (3, (2, 4)),
    "Hider": (4, (2, 4)),
    "Grandfather": (5, (2, 4)),
    "Abducted": (6, (1, 3)),
    "Teacher": (7, (2, 4)),
    "Edward": (8, (1, 3)),
    "Trent": (9, (2, 4)),
    "Roy": (10, (2, 4)),
    "Mohawk Hider": (11, (3, 5)),
    "Victim": (12, (2, 4)),
    "Post-op": (13, (3, 5)),
    "Becky": (14, (3, 5)),
    "Elite Hunter": (15, (4, 5)),
    "Elhuit": (16, (3, 5)),
    "Hunter": (17, (2, 4)),
    "Jen": (18, (2, 4)),
}

# Traits whose curve has a direction: +1 means a higher number is a better
# player, -1 means a lower one is.  Only the vision, aim, trigger and mistake
# traits are listed - the rest are taste, and a test that pinned those would be
# pinning the tuning rather than the contract.
TRAIT_DIRECTION = {
    "sightRange": +1,
    "fov": +1,
    "reactionMsec": -1,
    "reactionVarianceMsec": -1,
    "reacquireMsec": +1,
    "reacquireFraction": -1,
    "peripheralAngle": +1,
    "peripheralPenaltyMsec": -1,
    "turnSpeed": +1,
    "turnAccel": +1,
    "turnDamping": +1,
    "aimTrackTimeConst": -1,
    "aimTremorDeg": -1,
    "aimTrackError": -1,
    "aimSettleMsec": -1,
    "aimLead": +1,
    "aimLeadError": -1,
    "fireConeDeg": -1,
    "holdFireTurnRate": -1,
    "mistakeChance": -1,
    "mistakeMsec": -1,
    "dodgeReactMsec": -1,
    "weaponSkill": +1,
    "targetSelection": +1,
}

# Deliberately flat across the whole curve: these are the axes a style or a
# character owns.  A skill curve that moved them would make every high-skill bot
# play identically, which is the thing the character system exists to stop.
TRAIT_FLAT = (
    "combatRange",
    "aggression",
    "patience",
    "targetStickiness",
    "opportunism",
    "vengefulness",
    "strafeRhythmMsec",
    "strafeRhythmVarianceMsec",
    "weaponSwitchMsec",
    "aimHeight",
    "chatiness",
)

CHAT_EVENTS = (
    "entergame",
    "levelstart",
    "kill",
    "killWrench",
    "killStreak",
    "revenge",
    "death",
    "deathAccident",
    "itemDenied",
    "leadTaken",
    "leadLost",
    "matchWin",
    "matchLose",
    "farewell",
)

CHAT_EVENT_ENUM = (
    "BOTCHAT_ENTERGAME",
    "BOTCHAT_LEVELSTART",
    "BOTCHAT_KILL",
    "BOTCHAT_KILL_WRENCH",
    "BOTCHAT_KILL_STREAK",
    "BOTCHAT_KILL_REVENGE",
    "BOTCHAT_DEATH",
    "BOTCHAT_DEATH_ACCIDENT",
    "BOTCHAT_ITEM_DENIED",
    "BOTCHAT_LEAD_TAKEN",
    "BOTCHAT_LEAD_LOST",
    "BOTCHAT_MATCH_WIN",
    "BOTCHAT_MATCH_LOSE",
    "BOTCHAT_FAREWELL",
)

# Triggered replies deliberately use a small, common vocabulary.  The names
# are part of the content contract rather than runtime event enum values: each
# character supplies its own words and responses for these conversational
# intents.
REPLY_CATEGORIES = (
    "help",
    "greeting",
    "thanks",
    "praise",
    "apology",
    "goodGame",
    "challenge",
    "farewell",
    "direct",
)
REPLY_SOURCES = frozenset(("any", "player", "bot"))
REPLY_ADDRESS_MODES = frozenset(("either", "required", "forbidden"))
REPLY_ALLOWED_TOKENS = frozenset(("$self", "$other", "$map"))

# Weapon object class names exposed by Prey's retail multiplayer player def.  A
# misspelled class is silently neutral at runtime.
KNOWN_WEAPONS = frozenset(
    {
        "weaponobj_wrench",
        "weaponobj_rifle",
        "weaponobj_crawlergrenade",
        "weaponobj_soulstripper",
        "weaponobj_autocannon",
        "weaponobj_hiderweapon",
        "weaponobj_rocketlauncher",
        "weaponobj_bow",
    }
)


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"{rel(path)} does not exist")
    return path.read_text(encoding="utf-8", errors="replace")


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_regex(haystack: str, pattern: str, context: str) -> re.Match[str]:
    match = re.search(pattern, haystack, re.DOTALL)
    if match is None:
        raise AssertionError(f"Missing /{pattern}/ in {context}")
    return match


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    a = haystack.find(first)
    b = haystack.find(second)
    if a == -1:
        raise AssertionError(f"Missing {first!r} in {context}")
    if b == -1:
        raise AssertionError(f"Missing {second!r} in {context}")
    if a > b:
        raise AssertionError(f"{first!r} must appear before {second!r} in {context}")


def braced_body(source: str, start: int, context: str) -> str:
    """Text between the first '{' at or after start and its matching '}'."""

    open_at = source.find("{", start)
    if open_at == -1:
        raise AssertionError(f"No opening brace for {context}")
    depth = 0
    for i in range(open_at, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[open_at + 1 : i]
    raise AssertionError(f"Unterminated brace section for {context}")


# ----------------------------------------------------------------------------
# An independent reader for the .style / .bot / .chat formats, so a bug in the
# game's parser and a bug in the content cannot agree with each other.
# ----------------------------------------------------------------------------

TOKEN_RE = re.compile(
    r"""
      (?P<comment>//[^\n]*|/\*.*?\*/)
    | (?P<string>"(?:[^"\\\n]|\\.)*")
    | (?P<number>-?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?)
    | (?P<word>[A-Za-z_][A-Za-z0-9_]*)
    | (?P<punct>[{}])
    | (?P<space>\s+)
    """,
    re.VERBOSE | re.DOTALL,
)


class Token:
    __slots__ = ("kind", "text", "line")

    def __init__(self, kind: str, text: str, line: int) -> None:
        self.kind = kind
        self.text = text
        self.line = line

    def __repr__(self) -> str:
        return f"{self.kind}:{self.text}"


def tokenize(source: str, name: str) -> list[Token]:
    tokens: list[Token] = []
    pos = 0
    line = 1
    while pos < len(source):
        match = TOKEN_RE.match(source, pos)
        if match is None:
            raise AssertionError(f"{name}:{line}: cannot read {source[pos:pos + 20]!r}")
        text = match.group(0)
        kind = match.lastgroup
        if kind in ("string", "number", "word", "punct"):
            if kind == "string":
                text = text[1:-1]
            tokens.append(Token(kind, text, line))
        line += text.count("\n")
        pos = match.end()
    return tokens


class Reader:
    def __init__(self, tokens: list[Token], name: str) -> None:
        self.tokens = tokens
        self.name = name
        self.pos = 0

    def at_end(self) -> bool:
        return self.pos >= len(self.tokens)

    def peek(self) -> Token | None:
        return self.tokens[self.pos] if self.pos < len(self.tokens) else None

    def next(self, what: str) -> Token:
        token = self.peek()
        if token is None:
            raise AssertionError(f"{self.name}: file ends where {what} was expected")
        self.pos += 1
        return token

    def expect(self, kind: str, what: str) -> Token:
        token = self.next(what)
        if token.kind != kind:
            raise AssertionError(
                f"{self.name}:{token.line}: expected {what}, found {token.kind} {token.text!r}"
            )
        return token

    def expect_punct(self, text: str) -> Token:
        token = self.next(repr(text))
        if token.kind != "punct" or token.text != text:
            raise AssertionError(
                f"{self.name}:{token.line}: expected {text!r}, found {token.text!r}"
            )
        return token


class Block:
    """One parsed style or character."""

    def __init__(self, kind: str, name: str, source: str) -> None:
        self.kind = kind
        self.name = name
        self.source = source
        self.description = ""
        self.inherit = ""
        self.skill_band: tuple[int, int] | None = None
        self.model_num: int | None = None
        self.traits: list[str] = []
        self.weapons: list[str] = []
        self.skill_blocks: list[int] = []
        self.chat: dict[str, list[tuple[str, int]]] = {}
        self.replies: list[ReplyRule] = []


class ReplyRule:
    """One named trigger/reply rule inside a characterChat block."""

    def __init__(self, name: str, line: int) -> None:
        self.name = name
        self.line = line
        self.priority: int | None = None
        self.source: str | None = None
        self.addressed: str | None = None
        self.triggers: list[tuple[str, int]] = []
        self.lines: list[tuple[str, int]] = []


MOD_WORDS = ("set", "add", "scale")


def parse_chat_block(reader: Reader, block: Block) -> None:
    event = reader.expect("word", "a chat event name").text
    reader.expect_punct("{")
    lines: list[tuple[str, int]] = []
    while True:
        line_token = reader.next("a chat line or '}'")
        if line_token.kind == "punct" and line_token.text == "}":
            break
        if line_token.kind != "string":
            raise AssertionError(
                f"{reader.name}:{line_token.line}: chat lines must be quoted, "
                f"found {line_token.text!r}"
            )
        lines.append((line_token.text, line_token.line))
    block.chat.setdefault(event, []).extend(lines)


def parse_reply_block(reader: Reader, block: Block) -> None:
    """Read `reply <name> { ... }` without sharing the game's parser."""

    name = reader.expect("word", "a reply rule name")
    rule = ReplyRule(name.text, name.line)
    reader.expect_punct("{")

    while True:
        token = reader.next("a reply setting, quoted line or '}'")
        if token.kind == "punct" and token.text == "}":
            break
        if token.kind == "string":
            rule.lines.append((token.text, token.line))
            continue
        if token.kind != "word":
            raise AssertionError(
                f"{reader.name}:{token.line}: expected a reply setting or quoted line, "
                f"found {token.text!r}"
            )

        if token.text == "priority":
            value = reader.expect("number", "an integer reply priority")
            if not re.fullmatch(r"-?\d+", value.text):
                raise AssertionError(
                    f"{reader.name}:{value.line}: reply priority must be an integer, "
                    f"found {value.text!r}"
                )
            if rule.priority is not None:
                raise AssertionError(
                    f"{reader.name}:{token.line}: reply {rule.name!r} repeats priority"
                )
            rule.priority = int(value.text)
        elif token.text == "source":
            value = reader.expect("word", "any, player or bot")
            if rule.source is not None:
                raise AssertionError(
                    f"{reader.name}:{token.line}: reply {rule.name!r} repeats source"
                )
            rule.source = value.text
        elif token.text == "addressed":
            value = reader.expect("word", "either, required or forbidden")
            if rule.addressed is not None:
                raise AssertionError(
                    f"{reader.name}:{token.line}: reply {rule.name!r} repeats addressed"
                )
            rule.addressed = value.text
        elif token.text == "trigger":
            value = reader.expect("string", "a quoted trigger phrase")
            rule.triggers.append((value.text, value.line))
        else:
            raise AssertionError(
                f"{reader.name}:{token.line}: unknown reply key {token.text!r}"
            )

    block.replies.append(rule)


def parse_statements(reader: Reader, block: Block, skill_levels: int, nested: bool) -> None:
    while True:
        token = reader.next("a key or '}'")
        if token.kind == "punct" and token.text == "}":
            return
        if token.kind != "word":
            raise AssertionError(
                f"{reader.name}:{token.line}: expected a key, found {token.text!r}"
            )

        key = token.text

        if key in MOD_WORDS:
            block.traits.append(reader.expect("word", "a trait name").text)
            reader.expect("number", f"a number for '{key}'")
            continue

        if key == "weapon":
            block.weapons.append(reader.expect("string", "a weapon class name").text)
            reader.expect("number", "a weapon bias")
            continue

        if key == "skill":
            level = reader.expect("number", "a skill level")
            if nested:
                raise AssertionError(
                    f"{reader.name}:{level.line}: skill blocks may not be nested"
                )
            if not re.fullmatch(r"\d+", level.text) or not 1 <= int(level.text) <= skill_levels:
                raise AssertionError(
                    f"{reader.name}:{level.line}: skill level {level.text!r} is outside 1..{skill_levels}"
                )
            block.skill_blocks.append(int(level.text))
            reader.expect_punct("{")
            parse_statements(reader, block, skill_levels, nested=True)
            continue

        if key == "description":
            block.description = reader.expect("string", "a description").text
            continue

        if key == "inherit":
            block.inherit = reader.expect("string", "a style name").text
            continue

        if block.kind == "character":
            if key == "skillBand":
                low = reader.expect("number", "the lowest skill")
                high = reader.expect("number", "the highest skill")
                block.skill_band = (int(float(low.text)), int(float(high.text)))
                continue
            if key == "modelNum":
                value = reader.expect("number", "a retail multiplayer model number")
                if not re.fullmatch(r"\d+", value.text):
                    raise AssertionError(
                        f"{reader.name}:{value.line}: modelNum must be an integer, "
                        f"found {value.text!r}"
                    )
                if block.model_num is not None:
                    raise AssertionError(
                        f"{reader.name}:{token.line}: character repeats modelNum"
                    )
                block.model_num = int(value.text)
                continue
            if key == "chat":
                parse_chat_block(reader, block)
                continue

        raise AssertionError(
            f"{reader.name}:{token.line}: unknown key {key!r} in a {block.kind} file"
        )


def parse_file(path: Path, kind: str, skill_levels: int) -> Block:
    name = rel(path)
    reader = Reader(tokenize(read(path), name), name)
    header = reader.expect("word", f"'{kind}'")
    if header.text != kind:
        raise AssertionError(f"{name}:{header.line}: expected '{kind}', found {header.text!r}")
    block = Block(kind, reader.expect("string", f"the {kind} name").text, name)
    reader.expect_punct("{")
    parse_statements(reader, block, skill_levels, nested=False)
    if not reader.at_end():
        token = reader.peek()
        raise AssertionError(f"{name}:{token.line}: trailing {token.text!r} after the block")
    return block


def parse_chat_file(path: Path) -> Block:
    """Read one `characterChat "<owner>" { chat <event> { ... } }` file."""

    name = rel(path)
    reader = Reader(tokenize(read(path), name), name)
    header = reader.expect("word", "'characterChat'")
    if header.text != "characterChat":
        raise AssertionError(
            f"{name}:{header.line}: expected 'characterChat', found {header.text!r}"
        )

    block = Block(
        "characterChat",
        reader.expect("string", "the owning character name").text,
        name,
    )
    reader.expect_punct("{")

    while True:
        token = reader.next("'chat', 'reply' or '}'")
        if token.kind == "punct" and token.text == "}":
            break
        if token.kind != "word":
            raise AssertionError(
                f"{name}:{token.line}: expected 'chat' or 'reply', found {token.text!r}"
            )
        if token.text == "chat":
            parse_chat_block(reader, block)
        elif token.text == "reply":
            parse_reply_block(reader, block)
        else:
            raise AssertionError(
                f"{name}:{token.line}: expected 'chat' or 'reply', found {token.text!r}"
            )

    if not reader.at_end():
        token = reader.peek()
        raise AssertionError(f"{name}:{token.line}: trailing {token.text!r} after the block")

    return block


IDTECH_COLOR_RE = re.compile(r"\^(?:[cC][0-9]{3}|[rR]|[^^])")
REPLY_WORD_RE = re.compile(r"[a-z0-9]+")


def normalize_reply_words(text: str) -> tuple[str, ...]:
    """Mirror the runtime's colour-free, ASCII, case-insensitive word view."""

    without_colors = IDTECH_COLOR_RE.sub("", text)
    return tuple(REPLY_WORD_RE.findall(without_colors.lower()))


def normalize_reply_phrase(text: str) -> str:
    return " ".join(normalize_reply_words(text))


def reply_phrase_matches(trigger: str, message: str) -> bool:
    """Contiguous whole-word phrase matching, never substring matching."""

    needle = normalize_reply_words(trigger)
    words = normalize_reply_words(message)
    if not needle or len(needle) > len(words):
        return False
    return any(words[index : index + len(needle)] == needle
               for index in range(len(words) - len(needle) + 1))


def validate_reply_matcher_vectors() -> None:
    """Pin the player-facing matching semantics independently of C++."""

    vectors = (
        ("hello", "^1HeLLo, ^7Tommy!", True, "colours, case and punctuation"),
        ("hi", "HI!", True, "case-insensitive exact word"),
        ("hi", "this should not match", False, "short word is not a substring"),
        ("hi", "a high ledge", False, "word prefix is not a match"),
        ("good game", "Well, GOOD... GAME!", True, "punctuated phrase"),
        ("good game", "that was a good gamer", False, "phrase final boundary"),
        ("good game", "good very game", False, "phrase words stay contiguous"),
        ("gg", "egg on your face", False, "abbreviation is a whole word"),
    )
    for trigger, message, expected, context in vectors:
        actual = reply_phrase_matches(trigger, message)
        if actual != expected:
            raise AssertionError(
                f"reply matcher failed {context}: trigger {trigger!r}, "
                f"message {message!r}, expected {expected}, got {actual}"
            )


# ----------------------------------------------------------------------------
# Validators
# ----------------------------------------------------------------------------


def header_constants(header: str) -> dict[str, int]:
    constants: dict[str, int] = {}
    for name in (
        "BOT_SKILL_LEVELS",
        "BOT_MAX_WEAPON_BIAS",
        "BOT_CHAT_MAX_LEN",
        "BOT_MAX_REPLY_RULES",
        "BOT_MAX_REPLY_TRIGGERS",
        "BOT_MAX_REPLY_LINES",
        "BOT_REPLY_TRIGGER_MAX_LEN",
    ):
        match = re.search(rf"\b{name}\s*=\s*(\d+)\s*;", header)
        if match is None:
            raise AssertionError(f"{name} is not defined in BotCharacter.h")
        constants[name] = int(match.group(1))
    return constants


def trait_fields(header: str) -> list[str]:
    body = header[header.index("typedef struct botTraits_s {") : header.index("} botTraits_t;")]
    return re.findall(r"^\tfloat\t+(\w+);", body, re.MULTILINE)


def chat_event_enum(header: str) -> list[str]:
    body = header[header.index("typedef enum rvBotChatEvent_e {") : header.index("} rvBotChatEvent;")]
    return [name for name in re.findall(r"\b(BOTCHAT_\w+)\b", body) if name != "BOTCHAT_NUM"]


def validate_wiring() -> None:
    if not GAME_SOURCE.is_dir():
        print(f"mp_bot_characters: skipped game checks (no GameLibs checkout at {GAME_LIBS_ROOT})")
        return

    # Character data is game-module state.  Prey's existing artificial-player
    # class remains the player/network implementation, so the public game ABI
    # must remain the retail-compatible version 7.
    game_local_h = read(GAME_SOURCE / "Game_local.h")
    require(game_local_h, '#include "bots/BotCharacter.h"', "Game_local.h")
    game_api = read(GAME_SOURCE / "Game.h")
    require_regex(game_api, r"GAME_API_VERSION\s*=\s*7\s*;", "Prey game API")

    player_header = read(PREY_SOURCE / "game_player.h")
    require(
        player_header,
        "class hhArtificialPlayer : public hhPlayer",
        "Prey artificial-player foundation",
    )
    for needle in (
        "const rvBotCharacter *botCharacter",
        "botTraits_t",
        "BindBotCharacter(",
        "RebindBotCharacter(",
        "TryQueueBotReply(",
        "OnBotItemPickedUp(",
        "botPendingChatIsReply",
    ):
        require(player_header, needle, "hhArtificialPlayer character state/API")

    # The manager owns loaded style, character and chat data.
    game_local = read(GAME_SOURCE / "Game_local.cpp")
    init = game_local[game_local.index("void idGameLocal::Init") :][:6000]
    require(init, "botCharacterManager.Init();", "idGameLocal::Init")
    shutdown = game_local[game_local.index("void idGameLocal::Shutdown") :][:6000]
    require(shutdown, "botCharacterManager.Shutdown();", "idGameLocal::Shutdown")

    # A bot speaks through the same server call as a human, so the line is
    # indistinguishable from an ordinary player chat line.
    mp_header = read(GAME_SOURCE / "MultiplayerGame.h")
    at = mp_header.index("ProcessChatMessage")
    access = None
    for match in re.finditer(r"^\s*(public|protected|private)\s*:", mp_header[:at], re.MULTILINE):
        access = match.group(1)
    if access != "public":
        raise AssertionError(
            f"idMultiplayerGame::ProcessChatMessage is {access}; hhArtificialPlayer cannot reach it"
        )


def validate_manager() -> None:
    if not GAME_SOURCE.is_dir():
        return

    source = read(GAME_SOURCE / "bots" / "BotCharacter.cpp")
    header = read(GAME_SOURCE / "bots" / "BotCharacter.h")

    # The implementation is a Prey schema, not a compatibility parser for
    # upstream Quake 4 identities or weapon lore.
    for forbidden in ("modelMarine", "modelStrogg", "rail gun", '"Voss"'):
        if forbidden.lower() in (source + header).lower():
            raise AssertionError(
                f"Prey bot-character source retains upstream-only token {forbidden!r}"
            )

    # A mod's malformed character file must warn, not kill the server.
    # LEXFL_NOFATALERRORS is what buys that, and it comes with these flags.
    # Every reader needs it, so check every reader rather than just finding the
    # token once - styles, characters and voices are loaded down separate paths.
    flags = [arg.strip() for arg in re.findall(r"SetFlags\(\s*([^)]*)\)", source)]
    if not flags:
        raise AssertionError(
            "BotCharacter.cpp never sets the lexer flags, so a malformed character file in a "
            "mod is a fatal engine error instead of a warning"
        )
    wrong = [arg for arg in flags if arg != "DECL_LEXER_FLAGS"]
    if wrong:
        raise AssertionError(
            f"BotCharacter.cpp reads content with {wrong} rather than DECL_LEXER_FLAGS, whose "
            "LEXFL_NOFATALERRORS is what keeps bad content from killing a server"
        )

    # The file list crosses the game module's allocator boundary.
    listed = source.count("->ListFiles(")
    freed = source.count("->FreeFileList(")
    if listed == 0:
        raise AssertionError(
            "BotCharacter.cpp never calls ListFiles; characters in a pk4 or a mod would be invisible"
        )
    if listed != freed:
        raise AssertionError(
            f"BotCharacter.cpp has {listed} ListFiles calls and {freed} FreeFileList calls"
        )

    for needle in (
        '"botfiles/styles"',
        '"botfiles/characters"',
        '"botfiles/chats"',
        '".style"',
        '".bot"',
        '".chat"',
    ):
        require(source, needle, "rvBotCharacterManager::Init enumeration")

    require_order(
        source,
        "LoadCharacterFile( path.c_str() );",
        "LoadChatFile( path.c_str() );",
        "rvBotCharacterManager::Init character/chat load order",
    )

    # Separate voice banks are merged by owner after every character exists.
    # Keep this check on code rather than comments: case-sensitive ownership
    # would silently strand a perfectly valid `characterChat "anderson"` block,
    # and parsing directly into the live character would leave its early lines
    # behind when a later line or closing brace is malformed.
    code = strip_comments(source)
    load_chat_at = code.index("bool rvBotCharacterManager::LoadChatFile")
    resolve_at = code.index("void rvBotCharacterManager::ResolveInheritance", load_chat_at)
    load_chat = code[load_chat_at:resolve_at]
    require(load_chat, "idStr::Icmp(", "rvBotCharacterManager::LoadChatFile owner lookup")
    require(
        load_chat,
        "SkipBracedSection( false )",
        "rvBotCharacterManager::LoadChatFile unknown-owner recovery",
    )
    require(
        load_chat,
        "stagedChat.ParseChatBlock(",
        "rvBotCharacterManager::LoadChatFile atomic block parse",
    )
    require(
        load_chat,
        "stagedChat.ParseReplyBlock(",
        "rvBotCharacterManager::LoadChatFile atomic reply parse",
    )
    require_order(
        load_chat,
        "if ( !closed )",
        "character->chat[event].Append(",
        "rvBotCharacterManager::LoadChatFile atomic block commit",
    )
    require_order(
        load_chat,
        "if ( !closed )",
        "character->replies.Append(",
        "rvBotCharacterManager::LoadChatFile atomic reply commit",
    )

    # Reply rules are mod content too.  Their parser must enforce the same
    # bounded-memory and non-fatal contracts as the event-line parser.
    reply_parse_at = code.index("bool rvBotCharacter::ParseReplyBlock")
    character_parse_at = code.index("bool rvBotCharacter::Parse(", reply_parse_at)
    reply_parse = code[reply_parse_at:character_parse_at]
    for constant in (
        "BOT_MAX_REPLY_RULES",
        "BOT_MAX_REPLY_TRIGGERS",
        "BOT_MAX_REPLY_LINES",
        "BOT_REPLY_TRIGGER_MAX_LEN",
    ):
        require(reply_parse, constant, "rvBotCharacter::ParseReplyBlock content cap")
    require(
        reply_parse,
        "BotReplyLineTokensValid(",
        "rvBotCharacter::ParseReplyBlock reply token validation",
    )
    require(
        reply_parse,
        "NormalizeReplyText(",
        "rvBotCharacter::ParseReplyBlock trigger normalization",
    )

    # Shipped dialogue is separate, but existing add-ons may still carry
    # inline `chat` blocks in a character.  Both paths must share the same
    # parser so validation and line-length behaviour cannot drift.
    character_parse_at = code.index("bool rvBotCharacter::Parse(")
    character_apply_at = code.index("void rvBotCharacter::Apply", character_parse_at)
    character_parse = code[character_parse_at:character_apply_at]
    require(
        character_parse,
        'token.Icmp( "chat" )',
        "rvBotCharacter legacy inline chat compatibility",
    )
    require(
        character_parse,
        "ParseChatBlock( lexer, sourceName )",
        "rvBotCharacter legacy inline chat compatibility",
    )
    require(
        character_parse,
        'token.Icmp( "modelNum" )',
        "rvBotCharacter retail modelNum parser",
    )
    require(character_parse, "parsedModelNum", "rvBotCharacter integer modelNum parser")
    require(character_parse, "parsedModelNum > 18", "rvBotCharacter modelNum upper bound")

    # The dead Quake 3 prototypes share the botfiles tree; nothing may read them.
    for dead in ('"botfiles/bots"', '"botfiles/items.c"', '"botfiles/weapons.c"'):
        if dead in source:
            raise AssertionError(f"BotCharacter.cpp reads {dead}, which is a dead Quake 3 prototype")

    # Unbounded bot chat can kick real players: the engine drops a client whose
    # reliable queue overflows, and nothing in the chat path rate limits.
    for constant in ("BOT_CHAT_CLIENT_THROTTLE_MSEC", "BOT_CHAT_GLOBAL_THROTTLE_MSEC"):
        require(source, constant, "BotCharacter.cpp chat throttle")

    header = read(GAME_SOURCE / "bots" / "BotCharacter.h")
    constants = header_constants(header)
    fields = trait_fields(header)
    if constants["BOT_SKILL_LEVELS"] != 5:
        raise AssertionError(
            f"BOT_SKILL_LEVELS is {constants['BOT_SKILL_LEVELS']}; the shipped curve is 1..5"
        )
    if len(fields) != 48:
        raise AssertionError(
            f"botTraits_t has {len(fields)} float traits; the authored contract has 48"
        )

    # The trait table is how a content file names a trait.  Every row has to
    # point at a real field, and every field wants a row or it can never be
    # tuned from content.
    validate_baseline(trait_table(source, fields, constants["BOT_SKILL_LEVELS"]),
                      constants["BOT_SKILL_LEVELS"])


NUMBER_RE = re.compile(r"(?<![A-Za-z0-9_.])[-+]?\d+(?:\.\d*)?(?:[eE][-+]?\d+)?[fF]?(?![A-Za-z0-9_.])")


def strip_comments(text: str) -> str:
    return re.sub(r"//[^\n]*", " ", re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL))


def numbers_in(text: str) -> list[float]:
    return [float(token.rstrip("fF")) for token in NUMBER_RE.findall(text)]


def split_rows(body: str) -> list[str]:
    """Split a C initialiser body at its top-level commas."""

    rows: list[str] = []
    current: list[str] = []
    depth = 0
    for ch in body:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            rows.append("".join(current))
            current = []
        else:
            current.append(ch)
    rows.append("".join(current))
    return [row for row in rows if row.strip()]


def row_name(row: str) -> str:
    """The trait a table row is about, whether it is written as a macro or a struct."""

    macro = re.match(r"\s*[A-Z][A-Z0-9_]*\(\s*(\w+)\s*[,)]", row)
    if macro is not None:
        return macro.group(1)
    quoted = re.search(r'"(\w+)"', row)
    return quoted.group(1) if quoted is not None else ""


def trait_table(source: str, fields: list[str], levels: int) -> dict[str, list[float]]:
    """{trait: [value at skill 1..N]} out of whatever table BotCharacter.cpp writes it in.

    The layout is the implementation's business - one row per trait carrying its
    own curve, or a field table beside a parallel curve array - so this finds
    whichever array names the most traits and reads the numbers out of it rather
    than insisting on a shape.
    """

    known = set(fields)
    best: list[str] = []
    for match in re.finditer(r"\w+\s+\w+\s*\[\s*\]\s*=", source):
        rows = split_rows(strip_comments(braced_body(source, match.end(), "a trait table")))
        if sum(1 for row in rows if row_name(row) in known) > sum(
            1 for row in best if row_name(row) in known
        ):
            best = rows
    named = [row_name(row) for row in best]
    if not [name for name in named if name in known]:
        raise AssertionError(
            "BotCharacter.cpp has no table naming the botTraits_t fields, so no style or "
            "character file can reach any trait by name"
        )

    unknown = [name for name in named if name not in known]
    if unknown:
        raise AssertionError(
            f"The trait table names {unknown}, which are not fields of botTraits_t"
        )
    missing = [name for name in fields if name not in named]
    if missing:
        raise AssertionError(
            f"botTraits_t fields {missing} have no row in the trait table, so no style or "
            "character file can ever reach them"
        )

    curve: dict[str, list[float]] = {}
    for name, row in zip(named, best):
        values = numbers_in(row)
        if len(values) == levels:
            curve[name] = values
        elif len(values) == levels + 2:
            # The row carries its clamp as well.  A baseline value outside its
            # own clamp is clamped away on the first resolve and the tuning
            # never takes effect, so check it while the numbers are in hand.
            low, high, values = values[0], values[1], values[2:]
            outside = [value for value in values if not low <= value <= high]
            if outside:
                raise AssertionError(
                    f"The baseline for {name!r} has {outside} outside its own clamp "
                    f"of {low}..{high}, which is silently clamped away on the first resolve"
                )
            curve[name] = values
        elif len(values) < levels:
            return separate_curve(source, fields, levels)
        else:
            raise AssertionError(
                f"The trait table row for {name!r} carries {len(values)} numbers; "
                f"expected {levels} or {levels + 2}"
            )
    return curve


def separate_curve(source: str, fields: list[str], levels: int) -> dict[str, list[float]]:
    """The layout where the field table and the skill curve are parallel arrays."""

    match = re.search(r"\bfloat\s+\w+\s*\[[^\]]*\]\s*\[[^\]]*\]\s*=", source)
    if match is None:
        raise AssertionError(
            "The trait table carries no skill curve and there is no separate curve array, "
            "so nothing can check that skill 5 is better than skill 1"
        )
    rows = [
        numbers_in(row)
        for row in split_rows(strip_comments(braced_body(source, match.end(), "the skill curve")))
    ]
    if len(rows) == len(fields) and all(len(values) == levels for values in rows):
        return {fields[i]: rows[i] for i in range(len(fields))}
    if len(rows) == levels and all(len(values) == len(fields) for values in rows):
        return {fields[i]: [rows[level][i] for level in range(levels)] for i in range(len(fields))}
    raise AssertionError(
        f"The skill curve is {len(rows)} rows of {sorted({len(v) for v in rows})} numbers; "
        f"expected {len(fields)} rows of {levels} or {levels} rows of {len(fields)}"
    )


def validate_baseline(curve: dict[str, list[float]], levels: int) -> None:
    for name, direction in TRAIT_DIRECTION.items():
        if name not in curve:
            raise AssertionError(f"The baseline curve has no row for {name!r}")
        values = curve[name]
        ordered = values if direction > 0 else list(reversed(values))
        for level in range(1, levels):
            if ordered[level] <= ordered[level - 1]:
                raise AssertionError(
                    f"The baseline curve for {name!r} is {values}, which does not improve "
                    f"monotonically from skill 1 to skill {levels}"
                )
        # Stated separately because it is the claim the documentation makes.
        if direction > 0 and not values[0] < values[-1]:
            raise AssertionError(f"{name!r} is not better at skill {levels} than at skill 1")
        if direction < 0 and not values[0] > values[-1]:
            raise AssertionError(f"{name!r} is not better at skill {levels} than at skill 1")

    for name in TRAIT_FLAT:
        if name not in curve:
            raise AssertionError(f"The baseline curve has no row for {name!r}")
        if len(set(curve[name])) != 1:
            raise AssertionError(
                f"{name!r} is {curve[name]} across the skill curve, but it is a style and "
                "character axis and must stay flat, or every high-skill bot plays the same"
            )


def validate_content() -> None:
    header_path = GAME_SOURCE / "bots" / "BotCharacter.h"
    if not header_path.is_file():
        print(f"mp_bot_characters: skipped content checks (no BotCharacter.h at {header_path})")
        return

    header = read(header_path)
    constants = header_constants(header)
    levels = constants["BOT_SKILL_LEVELS"]
    fields = set(trait_fields(header))
    events = chat_event_enum(header)
    if tuple(events) != CHAT_EVENT_ENUM:
        raise AssertionError(
            "rvBotChatEvent order differs from the content word table; "
            f"expected {list(CHAT_EVENT_ENUM)}, found {events}"
        )

    manager = GAME_SOURCE / "bots" / "BotCharacter.cpp"
    words = chat_event_words(manager, len(events))

    style_dir = BOTFILES / "styles"
    character_dir = BOTFILES / "characters"
    chat_dir = BOTFILES / "chats"
    if not style_dir.is_dir():
        raise AssertionError(f"{rel(style_dir)} does not exist")
    if not character_dir.is_dir():
        raise AssertionError(f"{rel(character_dir)} does not exist")
    if not chat_dir.is_dir():
        raise AssertionError(f"{rel(chat_dir)} does not exist")

    styles = {}
    for path in sorted(style_dir.glob("*.style")):
        block = parse_file(path, "style", levels)
        if path.stem.lower() != block.name.lower():
            raise AssertionError(
                f"{rel(path)}: file name must match style {block.name!r}"
            )
        style_key = block.name.lower()
        if style_key in styles:
            raise AssertionError(
                f"{rel(path)}: style {block.name!r} is declared by more than one file"
            )
        styles[style_key] = block
    expected_styles = {"rusher", "sniper", "roamer", "hunter", "ambusher", "skirmisher"}
    if set(styles) != expected_styles:
        raise AssertionError(
            f"{rel(style_dir)} styles differ from the six-style contract; "
            f"missing {sorted(expected_styles - set(styles))}, "
            f"unknown {sorted(set(styles) - expected_styles)}"
        )

    characters = [parse_file(path, "character", levels) for path in sorted(character_dir.glob("*.bot"))]
    if len(characters) != len(EXPECTED_ROSTER):
        raise AssertionError(
            f"{rel(character_dir)} holds {len(characters)} characters; "
            f"the retail-selectable roster has exactly {len(EXPECTED_ROSTER)}"
        )
    chat_blocks = [parse_chat_file(path) for path in sorted(chat_dir.glob("*.chat"))]

    seen: dict[str, str] = {}
    for block in list(styles.values()) + characters:
        check_block(block, fields, constants)

        if block.kind == "character":
            key = block.name.lower()
            if key in seen:
                raise AssertionError(
                    f"{block.source}: character {block.name!r} is also declared in {seen[key]}"
                )
            seen[key] = block.source

            if not block.inherit:
                raise AssertionError(f"{block.source}: character {block.name!r} names no style")
            if block.inherit.lower() not in styles:
                raise AssertionError(
                    f"{block.source}: character {block.name!r} inherits style "
                    f"{block.inherit!r}, which no .style file declares"
                )
            if block.skill_band is None:
                raise AssertionError(f"{block.source}: character {block.name!r} has no skillBand")
            low, high = block.skill_band
            if not 1 <= low <= high <= levels:
                raise AssertionError(
                    f"{block.source}: skillBand {low} {high} is outside 1..{levels}"
                )

            if block.name not in EXPECTED_ROSTER:
                raise AssertionError(
                    f"{block.source}: {block.name!r} is not one of Prey's 19 "
                    "retail-selectable multiplayer models"
                )
            expected_model, expected_band = EXPECTED_ROSTER[block.name]
            if block.model_num != expected_model:
                raise AssertionError(
                    f"{block.source}: {block.name!r} uses modelNum {block.model_num!r}; "
                    f"retail player.def assigns modelNum {expected_model}"
                )
            if block.skill_band != expected_band:
                raise AssertionError(
                    f"{block.source}: {block.name!r} uses skillBand "
                    f"{block.skill_band!r}; the authored roster expects {expected_band}"
                )
            expected_stem = re.sub(r"[^a-z0-9]+", "_", block.name.lower()).strip("_")
            if Path(block.source).stem.lower() != expected_stem:
                raise AssertionError(
                    f"{block.source}: file name must match character {block.name!r}"
                )

            if block.chat:
                raise AssertionError(
                    f"{block.source}: character mechanics files may not contain chat blocks; "
                    f"move {block.name!r}'s dialogue to botfiles/chats"
                )
        elif block.inherit and block.inherit.lower() not in styles:
            raise AssertionError(
                f"{block.source}: style {block.name!r} inherits {block.inherit!r}, "
                "which no .style file declares"
            )

    expected_events = set(words)
    allowed_tokens = {
        word: {"$self", "$map"}
        for word in words
    }
    actual_roster = set(seen)
    expected_roster = {name.lower() for name in EXPECTED_ROSTER}
    if actual_roster != expected_roster:
        raise AssertionError(
            "character roster differs from retail player.def; "
            f"missing {sorted(expected_roster - actual_roster)}, "
            f"unknown {sorted(actual_roster - expected_roster)}"
        )

    for word in ("kill", "killWrench", "killStreak", "revenge", "death"):
        allowed_tokens[word].update(("$other", "$weapon"))
    allowed_tokens["itemDenied"].update(("$other", "$item"))
    allowed_tokens["leadLost"].add("$other")

    chats_by_owner: dict[str, Block] = {}
    normalized_lines: dict[str, tuple[str, str, int]] = {}
    for block in chat_blocks:
        check_block(block, fields, constants)
        owner = block.name.lower()

        if owner in chats_by_owner:
            raise AssertionError(
                f"{block.source}: character {block.name!r} also owns "
                f"{chats_by_owner[owner].source}"
            )
        chats_by_owner[owner] = block

        if owner not in seen:
            raise AssertionError(
                f"{block.source}: chat owner {block.name!r} has no matching character"
            )

        file_stem = Path(block.source).stem.lower()
        owner_stem = re.sub(r"[^a-z0-9]+", "_", owner).strip("_")
        if file_stem != owner_stem:
            raise AssertionError(
                f"{block.source}: file name must match its chat owner {block.name!r}"
            )

        declared_events = set(block.chat)
        unknown = sorted(declared_events - expected_events)
        missing = sorted(expected_events - declared_events)
        if unknown:
            raise AssertionError(f"{block.source}: unknown chat events {unknown}")
        if missing:
            raise AssertionError(
                f"{block.source}: character {block.name!r} has nothing to say for {missing}"
            )

        for event in words:
            lines = block.chat[event]
            if len(lines) != 8:
                raise AssertionError(
                    f"{block.source}: chat {event} has {len(lines)} lines; "
                    "the shipped voice-bank target is exactly 8"
                )

            token_free = 0
            for text, line in lines:
                tokens = set(re.findall(r"\$[A-Za-z_][A-Za-z0-9_]*", text))
                invalid = sorted(tokens - allowed_tokens[event])
                if invalid:
                    raise AssertionError(
                        f"{block.source}:{line}: chat {event} uses unavailable tokens {invalid}"
                    )
                if "$other" not in tokens:
                    token_free += 1

                normalized = re.sub(r"[^a-z0-9$]+", " ", text.lower()).strip()
                previous = normalized_lines.get(normalized)
                if previous is not None:
                    previous_owner, previous_event, previous_line = previous
                    raise AssertionError(
                        f"{block.source}:{line}: {block.name}/{event} repeats the line at "
                        f"{previous_owner}/{previous_event}:{previous_line}"
                    )
                normalized_lines[normalized] = (block.name, event, line)

            if event == "leadLost" and token_free < 4:
                raise AssertionError(
                    f"{block.source}: chat {event} needs at least four lines that do not "
                    "require $other, because the lead event may have no named rival"
                )

        if len(block.replies) > constants["BOT_MAX_REPLY_RULES"]:
            raise AssertionError(
                f"{block.source}: {len(block.replies)} reply rules exceed "
                f"BOT_MAX_REPLY_RULES ({constants['BOT_MAX_REPLY_RULES']})"
            )

        replies_by_name: dict[str, ReplyRule] = {}
        normalized_triggers: dict[str, tuple[str, int]] = {}
        for rule in block.replies:
            if rule.name in replies_by_name:
                previous = replies_by_name[rule.name]
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply category {rule.name!r} is also "
                    f"declared on line {previous.line}"
                )
            replies_by_name[rule.name] = rule

            if rule.priority is None:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} declares no priority"
                )
            if not 0 <= rule.priority <= 100:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} priority "
                    f"{rule.priority} is outside the runtime's 0..100 range"
                )
            if rule.source not in REPLY_SOURCES:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} has source "
                    f"{rule.source!r}, expected one of {sorted(REPLY_SOURCES)}"
                )
            if rule.addressed not in REPLY_ADDRESS_MODES:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} has addressed "
                    f"{rule.addressed!r}, expected one of {sorted(REPLY_ADDRESS_MODES)}"
                )
            if len(rule.triggers) != 8:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} has "
                    f"{len(rule.triggers)} triggers; the shipped contract requires exactly 8"
                )
            if len(rule.triggers) > constants["BOT_MAX_REPLY_TRIGGERS"]:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} has "
                    f"{len(rule.triggers)} triggers, over BOT_MAX_REPLY_TRIGGERS "
                    f"({constants['BOT_MAX_REPLY_TRIGGERS']})"
                )
            if len(rule.lines) > constants["BOT_MAX_REPLY_LINES"]:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} has "
                    f"{len(rule.lines)} lines, over BOT_MAX_REPLY_LINES "
                    f"({constants['BOT_MAX_REPLY_LINES']})"
                )

            for trigger, line in rule.triggers:
                normalized = normalize_reply_phrase(trigger)
                if not normalized:
                    raise AssertionError(
                        f"{block.source}:{line}: reply {rule.name!r} trigger {trigger!r} "
                        "contains no matchable words"
                    )
                if len(normalized) > constants["BOT_REPLY_TRIGGER_MAX_LEN"]:
                    raise AssertionError(
                        f"{block.source}:{line}: reply {rule.name!r} trigger is "
                        f"{len(normalized)} characters after normalization, over "
                        f"BOT_REPLY_TRIGGER_MAX_LEN "
                        f"({constants['BOT_REPLY_TRIGGER_MAX_LEN']})"
                    )
                previous = normalized_triggers.get(normalized)
                if previous is not None:
                    previous_rule, previous_line = previous
                    raise AssertionError(
                        f"{block.source}:{line}: reply {rule.name!r} trigger {trigger!r} "
                        f"normalizes to the trigger already used by {previous_rule!r} on "
                        f"line {previous_line}"
                    )
                normalized_triggers[normalized] = (rule.name, line)

            if len(rule.lines) != 4:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} has "
                    f"{len(rule.lines)} lines; shipped characters require exactly 4"
                )

            for text, line in rule.lines:
                tokens = set(re.findall(r"\$[A-Za-z_][A-Za-z0-9_]*", text))
                invalid = sorted(tokens - REPLY_ALLOWED_TOKENS)
                if invalid:
                    raise AssertionError(
                        f"{block.source}:{line}: reply {rule.name!r} uses unavailable "
                        f"tokens {invalid}; replies only know $self, $other and $map"
                    )

                normalized = re.sub(r"[^a-z0-9$]+", " ", text.lower()).strip()
                previous = normalized_lines.get(normalized)
                if previous is not None:
                    previous_owner, previous_context, previous_line = previous
                    raise AssertionError(
                        f"{block.source}:{line}: {block.name}/{rule.name} repeats the "
                        f"authored line at {previous_owner}/{previous_context}:{previous_line}"
                    )
                normalized_lines[normalized] = (
                    block.name, f"reply {rule.name}", line
                )

        missing_replies = sorted(set(REPLY_CATEGORIES) - set(replies_by_name))
        unknown_replies = sorted(set(replies_by_name) - set(REPLY_CATEGORIES))
        if missing_replies or unknown_replies:
            raise AssertionError(
                f"{block.source}: reply categories differ from the shipped contract; "
                f"missing {missing_replies}, unknown {unknown_replies}"
            )

        direct = replies_by_name["direct"]
        if direct.addressed != "required":
            raise AssertionError(
                f"{block.source}:{direct.line}: direct reply must require addressing"
            )
        owner_words = normalize_reply_phrase(block.name)
        if owner_words not in {
            normalize_reply_phrase(trigger) for trigger, _ in direct.triggers
        }:
            raise AssertionError(
                f"{block.source}:{direct.line}: direct reply does not trigger on its "
                f"owner name {block.name!r}"
            )

    missing_chats = sorted(name for name in seen if name not in chats_by_owner)
    if missing_chats:
        raise AssertionError(f"Characters without a dedicated .chat file: {missing_chats}")

    # Every style has to be worn by someone, or the archetype is unreachable.
    worn = {block.inherit.lower() for block in characters}
    unworn = sorted(name for name in styles if name not in worn)
    if unworn:
        raise AssertionError(f"No character uses the {unworn} style; the archetype is unreachable")


def chat_event_words(manager: Path, count: int) -> list[str]:
    """The event key words a content file may use.

    The documentation is the readable list, the enum fixes how many there are,
    and the game has to answer to every word in it - a word a character file may
    write that ChatEventForName does not know is a chat block that never fires.
    """

    doc = read(DOC)
    section = doc[doc.index("\n## Chat") :]
    section = section[: section.index("\n## ", 1)]
    listed = re.search(r"The events are:(.*?)\.\s", section, re.DOTALL)
    if listed is None:
        raise AssertionError("docs/dev/mp-bots.md no longer lists the chat events")
    words = re.findall(r"`(\w+)`", listed.group(1))
    if len(words) != count:
        raise AssertionError(
            f"docs/dev/mp-bots.md lists {len(words)} chat events but rvBotChatEvent declares {count}"
        )
    if tuple(words) != CHAT_EVENTS:
        raise AssertionError(
            "docs/dev/mp-bots.md chat event list differs from the shipped Prey contract; "
            f"expected {list(CHAT_EVENTS)}, found {words}"
        )

    if not manager.is_file():
        raise AssertionError(f"{rel(manager)} does not exist")
    source = read(manager)
    table = re.search(r"\bbotChatEventNames\s*\[[^\]]*\]\s*=", source)
    if table is None:
        raise AssertionError(
            "BotCharacter.cpp has no botChatEventNames table mapping content words to events"
        )
    runtime_words = re.findall(
        r'"(\w+)"', braced_body(source, table.end(), "botChatEventNames")
    )
    if runtime_words != words:
        raise AssertionError(
            "BotCharacter.cpp chat event word order disagrees with the enum/docs; "
            f"runtime {runtime_words}, docs {words}"
        )
    return words


def check_block(block: Block, fields: set[str], constants: dict[str, int]) -> None:
    unknown = sorted({name for name in block.traits if name not in fields})
    if unknown:
        raise AssertionError(
            f"{block.source}: {block.kind} {block.name!r} sets {unknown}, which are not "
            "fields of botTraits_t and would be warned away at load"
        )

    for weapon in block.weapons:
        if weapon not in KNOWN_WEAPONS:
            raise AssertionError(
                f"{block.source}: {block.kind} {block.name!r} has an opinion about "
                f"{weapon!r}, which is not a multiplayer weapon class"
            )
    if len(set(block.weapons)) > constants["BOT_MAX_WEAPON_BIAS"]:
        raise AssertionError(
            f"{block.source}: {len(set(block.weapons))} weapon biases exceeds "
            f"BOT_MAX_WEAPON_BIAS ({constants['BOT_MAX_WEAPON_BIAS']})"
        )

    if len(block.skill_blocks) != len(set(block.skill_blocks)):
        raise AssertionError(
            f"{block.source}: {block.kind} {block.name!r} has two skill blocks for one level"
        )

    limit = constants["BOT_CHAT_MAX_LEN"]
    for event, lines in block.chat.items():
        if not lines:
            raise AssertionError(f"{block.source}: chat {event} declares no lines")
        for text, line in lines:
            if not text.strip():
                raise AssertionError(f"{block.source}:{line}: chat {event} has an empty line")
            if text.startswith("#"):
                raise AssertionError(
                    f"{block.source}:{line}: chat lines may not start with '#'; the broadcast "
                    "runs them through GetLocalizedString and would substitute it away"
                )
            if len(text) > limit:
                raise AssertionError(
                    f"{block.source}:{line}: chat line is {len(text)} characters, over the "
                    f"BOT_CHAT_MAX_LEN budget of {limit}"
                )
        if len({text for text, _ in lines}) != len(lines):
            raise AssertionError(
                f"{block.source}: chat {event} repeats a line, which wastes a slot in the "
                "no-immediate-repeat rotation"
            )

    for rule in block.replies:
        if not rule.lines:
            raise AssertionError(
                f"{block.source}:{rule.line}: reply {rule.name!r} declares no lines"
            )
        for text, line in rule.lines:
            if not text.strip():
                raise AssertionError(
                    f"{block.source}:{line}: reply {rule.name!r} has an empty line"
                )
            if text.startswith("#"):
                raise AssertionError(
                    f"{block.source}:{line}: reply lines may not start with '#'; the "
                    "broadcast path would interpret one as a localisation key"
                )
            if len(text) > limit:
                raise AssertionError(
                    f"{block.source}:{line}: reply line is {len(text)} characters, over "
                    f"the BOT_CHAT_MAX_LEN budget of {limit}"
                )
        if len({text for text, _ in rule.lines}) != len(rule.lines):
            raise AssertionError(
                f"{block.source}:{rule.line}: reply {rule.name!r} repeats an exact line"
            )


def validate_cvars_and_commands() -> None:
    doc = read(DOC)

    cvar_table = doc[doc.index("\n## Cvars") :]
    cvar_table = cvar_table[: cvar_table.index("\n## ", 1)]
    documented = re.findall(r"^\|\s*`(bot_\w+)`\s*\|\s*`([^`]*)`\s*\|", cvar_table, re.MULTILINE)
    expected_defaults = {
        "bot_skill": "3",
        "bot_characters": "1",
        "bot_forceCharacter": "",
        "bot_skillVariance": "0",
        "bot_chat": "1",
        "bot_chatDelay": "600",
        "bot_chatCPM": "900",
    }
    documented_defaults = {name: default.strip('"') for name, default in documented}
    if len(documented) != len(expected_defaults) or documented_defaults != expected_defaults:
        raise AssertionError(
            "docs/dev/mp-bots.md bot cvars/defaults differ from the seven-cvar contract; "
            f"expected {expected_defaults}, found {documented_defaults}"
        )

    command_table = doc[doc.index("\n## Commands") :]
    command_table = command_table[: command_table.index("\n## ", 1)]
    commands = {row.split()[0] for row in re.findall(r"^\|\s*`([^`]+)`", command_table, re.MULTILINE)}
    for expected in (
        "addbot",
        "removebot",
        "removebots",
        "botcharacters",
        "botreload",
        "botlist",
    ):
        if expected not in commands:
            raise AssertionError(f"docs/dev/mp-bots.md does not document the {expected!r} command")

    if not GAME_SOURCE.is_dir():
        return

    declared = read(GAME_SOURCE / "gamesys" / "SysCvar.cpp")
    exported = read(GAME_SOURCE / "gamesys" / "SysCvar.h")

    for name, default in documented:
        match = re.search(rf'^idCVar {name}\(\s*"{name}",\s*"([^"]*)",\s*([^,]+),', declared, re.MULTILINE)
        if match is None:
            raise AssertionError(f"{name} is documented but not defined in game SysCvar.cpp")
        require_regex(
            exported,
            rf"\bextern\s+idCVar\s+{re.escape(name)}\s*;",
            "game SysCvar.h",
        )
        documented_default = default.strip('"')
        if match.group(1) != documented_default:
            raise AssertionError(
                f"{name} defaults to {match.group(1)!r} but the documentation says "
                f"{documented_default!r}"
            )

    # A command line +set never reaches a CVAR_GAME cvar, because the game module
    # registers it after the engine has parsed the command line.  Anything an
    # operator or a test harness has to set therefore has to be archived.
    for name in expected_defaults.keys() - {"bot_forceCharacter"}:
        line = re.search(rf"^idCVar {name}\(.*$", declared, re.MULTILINE)
        if line is None or "CVAR_ARCHIVE" not in line.group(0):
            raise AssertionError(
                f"{name} is not CVAR_ARCHIVE, so a server config cannot set it and a "
                "command line +set will not reach it either"
            )

    # A forced personality is a one-session tuning aid; archiving it would
    # unexpectedly fill later matches with clones.
    forced = re.search(r"^idCVar bot_forceCharacter\(.*$", declared, re.MULTILINE)
    if forced is None:
        raise AssertionError("bot_forceCharacter is not defined in game SysCvar.cpp")
    if "CVAR_ARCHIVE" in forced.group(0):
        raise AssertionError("bot_forceCharacter is archived; it is a session tuning knob")

    # addbot/removebot keep this legacy reconciler cvar in step with the live
    # roster.  A plain CVAR_GAME becomes CVAR_CHEAT automatically, which makes
    # those writes fail on a normal multiplayer server and causes the next
    # frame to retire every bot because the desired count remains zero.
    desired_count = re.search(
        r"^idCVar g_artificialPlayerCount\(.*$", declared, re.MULTILINE
    )
    if desired_count is None or "CVAR_NOCHEAT" not in desired_count.group(0):
        raise AssertionError(
            "g_artificialPlayerCount is not CVAR_NOCHEAT, so normal multiplayer "
            "addbot/removebot commands cannot maintain the desired roster"
        )

    source = read(GAME_SOURCE / "gamesys" / "SysCmds.cpp")
    for command in sorted(commands):
        require_regex(
            source,
            rf'AddCommand\(\s*"{re.escape(command)}"\s*,',
            "idGameLocal::InitConsoleCommands",
        )

    # addbot grew an optional per-bot skill override; the handler has to read it.
    handler = source[source.index("Cmd_AddBot_f") :][:1800]
    require(handler, "args.Argv( 2 )", "Cmd_AddBot_f skill argument")
    require(handler, "BOT_SKILL_LEVELS", "Cmd_AddBot_f skill argument")
    require(handler, "SpawnArtificialPlayer( character, skill )", "Cmd_AddBot_f spawn")

    for function in (
        "Cmd_RemoveBot_f",
        "Cmd_RemoveBots_f",
        "Cmd_BotList_f",
        "Cmd_BotCharacters_f",
        "Cmd_BotReload_f",
    ):
        require(source, function, "Prey bot command handlers")


def validate_chat_path() -> None:
    """Guard Prey's artificial-player personality and delayed-chat path."""

    if not GAME_SOURCE.is_dir():
        return

    player = strip_comments(read(PREY_SOURCE / "game_player.cpp"))
    manager = strip_comments(read(GAME_SOURCE / "bots" / "BotCharacter.cpp"))
    manager_header = read(GAME_SOURCE / "bots" / "BotCharacter.h")
    game_local = strip_comments(read(GAME_SOURCE / "Game_local.cpp"))
    commands = strip_comments(read(GAME_SOURCE / "gamesys" / "SysCmds.cpp"))

    # A removed player must return its character reservation.  Assignment and
    # live reload must also rebuild resolved traits rather than carrying a stale
    # pointer into the next frame.
    destructor_at = player.index("hhArtificialPlayer::~hhArtificialPlayer")
    destructor = braced_body(player, destructor_at, "hhArtificialPlayer destructor")
    require(destructor, "ReleaseCharacter(", "hhArtificialPlayer destructor")

    bind_at = player.index("void hhArtificialPlayer::BindBotCharacter")
    bind = braced_body(player, bind_at, "hhArtificialPlayer::BindBotCharacter")
    for needle in ("ReleaseCharacter(", "MarkCharacterUsed(", "ResolveBotTraits("):
        require(bind, needle, "hhArtificialPlayer::BindBotCharacter")
    require(
        bind,
        "botCharacter != character",
        "skill-only bot rebind leaves the character reservation count unchanged",
    )

    # Forced-character sessions intentionally allow clones. Reservations must
    # therefore be counted: releasing one clone cannot make its still-live
    # siblings look available to ordinary random selection.
    require(manager_header, "reservationCount", "reference-counted character ownership")
    if re.search(r"\bbool\s+inUse\b", manager_header):
        raise AssertionError("bot character ownership is still a clone-unsafe boolean")
    mark_at = manager.index("void rvBotCharacterManager::MarkCharacterUsed")
    mark = braced_body(manager, mark_at, "rvBotCharacterManager::MarkCharacterUsed")
    release_at = manager.index("void rvBotCharacterManager::ReleaseCharacter")
    release = braced_body(manager, release_at, "rvBotCharacterManager::ReleaseCharacter")
    release_all_at = manager.index("void rvBotCharacterManager::ReleaseAllCharacters")
    release_all = braced_body(
        manager, release_all_at, "rvBotCharacterManager::ReleaseAllCharacters"
    )
    require(mark, "++", "character reservation acquisition")
    require(release, "reservationCount > 0", "bounded character reservation release")
    require(release, "--", "character reservation release")
    require(release_all, "reservationCount = 0", "character reservation reset")

    rebind_at = player.index("void hhArtificialPlayer::RebindBotCharacter")
    rebind = braced_body(player, rebind_at, "hhArtificialPlayer::RebindBotCharacter")
    for needle in ("FindCharacter(", "PickCharacter(", "MarkCharacterUsed(", "ResolveBotTraits("):
        require(rebind, needle, "hhArtificialPlayer::RebindBotCharacter")

    spawn_at = game_local.index("bool idGameLocal::SpawnArtificialPlayer")
    spawn = braced_body(game_local, spawn_at, "idGameLocal::SpawnArtificialPlayer")
    for needle in (
        "botCharacterManager.FindCharacter(",
        "botCharacterManager.PickCharacter(",
        '"player_artificial_mp"',
        '"bot_character"',
        '"bot_skill"',
        "mpGame.SpawnPlayer(",
    ):
        require(spawn, needle, "idGameLocal::SpawnArtificialPlayer")

    userinfo_at = game_local.index("void idGameLocal::GetAPUserInfo")
    userinfo = braced_body(game_local, userinfo_at, "idGameLocal::GetAPUserInfo")
    for needle in (
        '"ui_name"',
        '"bot_character"',
        '"bot_skill"',
        "GetModelNum()",
        '"ui_modelNum"',
        "ClampInt( 0, 18",
    ):
        require(userinfo, needle, "idGameLocal::GetAPUserInfo")

    display_name_at = player.index("const char *hhArtificialPlayer::BotDisplayName")
    display_name = braced_body(
        player, display_name_at, "hhArtificialPlayer::BotDisplayName"
    )
    require(display_name, "gameLocal.GetUserInfo(", "trusted bot chat display name")
    require(display_name, 'GetString( "ui_name"', "trusted bot chat display name")

    remove_at = game_local.index("bool idGameLocal::RemoveArtificialPlayer")
    remove = braced_body(game_local, remove_at, "idGameLocal::RemoveArtificialPlayer")
    require(remove, "SayBotFarewell(", "idGameLocal::RemoveArtificialPlayer")
    require(remove, "ServerClientDisconnect(", "idGameLocal::RemoveArtificialPlayer")

    live_rebind_at = game_local.index("void idGameLocal::RebindArtificialPlayers")
    live_rebind = braced_body(
        game_local, live_rebind_at, "idGameLocal::RebindArtificialPlayers"
    )
    require(live_rebind, "ReleaseAllCharacters(", "idGameLocal::RebindArtificialPlayers")
    require(live_rebind, "RebindBotCharacter(", "idGameLocal::RebindArtificialPlayers")
    require_order(
        live_rebind,
        "ReleaseAllCharacters(",
        "RebindBotCharacter(",
        "idGameLocal::RebindArtificialPlayers reservation reset",
    )

    reload_at = commands.index("static void Cmd_BotReload_f")
    reload_command = braced_body(commands, reload_at, "Cmd_BotReload_f")
    require_order(
        reload_command,
        "botCharacterManager.Reload()",
        "RebindArtificialPlayers()",
        "Cmd_BotReload_f reload/rebind order",
    )
    require_order(
        reload_command,
        "RebindArtificialPlayers()",
        "if ( loaded )",
        "Cmd_BotReload_f unconditional live-bot rebind",
    )
    if reload_command.count("RebindArtificialPlayers()") != 1:
        raise AssertionError(
            "Cmd_BotReload_f must rebind live bots exactly once, outside the reload-result arm"
        )

    # Both routes that reset or replace map state clear absolute chat throttle
    # stamps.  Otherwise a timestamp from a long prior map can mute the new one.
    restart_at = game_local.index("void idGameLocal::LocalMapRestart")
    restart_end = game_local.index("void idGameLocal::MapRestart", restart_at)
    local_restart = game_local[restart_at:restart_end]
    require(
        local_restart,
        "botCharacterManager.ResetChatThrottle();",
        "idGameLocal::LocalMapRestart chat clock reset",
    )

    new_map_at = game_local.index("void idGameLocal::InitFromNewMap")
    new_map_end = game_local.index("bool idGameLocal::InitFromSaveGame", new_map_at)
    new_map = game_local[new_map_at:new_map_end]
    require(
        new_map,
        "botCharacterManager.ResetChatThrottle();",
        "idGameLocal::InitFromNewMap chat clock reset",
    )

    # Every advertised axis must reach behavior code.  Parser support alone
    # would otherwise let content validate while a tuning choice does nothing.
    runtime_traits = trait_fields(read(GAME_SOURCE / "bots" / "BotCharacter.h"))
    for trait in runtime_traits:
        require(player, f"botTraits.{trait}", f"hhArtificialPlayer runtime trait {trait}")

    think_at = player.index("void hhArtificialPlayer::Think")
    think_end = player.index("void hhArtificialPlayer::ClientPredictionThink", think_at)
    think = player[think_at:think_end]
    for needle in (
        "FindBotEnemy(",
        "UpdateBotWeapon(",
        "UpdateBotMovement(",
        "UpdateBotAimAndFire(",
        "UpdateBotChat(",
        "BOTCHAT_LEAD_TAKEN",
        "BOTCHAT_LEAD_LOST",
    ):
        require(think, needle, "hhArtificialPlayer::Think")

    # Retail MP maps do not ship bot AAS.  The fallback still has to respect
    # wall-walk gravity and avoid blindly walking into collision.
    if "GetEyeAxis()" not in player and "GetGravityAxis()" not in player:
        raise AssertionError("hhArtificialPlayer has no gravity-aware steering axis")
    movement_at = player.index("void hhArtificialPlayer::UpdateBotMovement")
    movement = braced_body(player, movement_at, "hhArtificialPlayer::UpdateBotMovement")
    require(movement, "TraceBounds(", "hhArtificialPlayer wall avoidance")

    weapon_at = player.index("void hhArtificialPlayer::UpdateBotWeapon")
    weapon = braced_body(player, weapon_at, "hhArtificialPlayer::UpdateBotWeapon")
    for needle in ("GetWeaponName(", "WeaponBias(", "SelectWeapon("):
        require(weapon, needle, "hhArtificialPlayer weapon preference")

    # Event and reply lines share the manager's flood throttle, but are delayed
    # by a character-specific typing time before using the normal server chat
    # route.  The pending-reply bit is provenance for the recursion brake.
    event_at = player.index("bool hhArtificialPlayer::QueueBotChat")
    event_queue = braced_body(player, event_at, "hhArtificialPlayer::QueueBotChat")
    for needle in (
        "!botPendingChat.IsEmpty()",
        "ChatLine(",
        "AllowChat(",
        "bot_chatDelay",
        "bot_chatCPM",
        "botTraits.chatDelayScale",
        "botPendingChatIsReply = false",
    ):
        require(event_queue, needle, "hhArtificialPlayer::QueueBotChat")

    reply_at = player.index("bool hhArtificialPlayer::TryQueueBotReply")
    reply_queue = braced_body(player, reply_at, "hhArtificialPlayer::TryQueueBotReply")
    for needle in (
        "!botPendingChat.IsEmpty()",
        "ReplyLine(",
        "AllowChat(",
        "bot_chatDelay",
        "bot_chatCPM",
        "botTraits.chatDelayScale",
        "botPendingChatIsReply = true",
    ):
        require(reply_queue, needle, "hhArtificialPlayer::TryQueueBotReply")

    update_at = player.index("void hhArtificialPlayer::UpdateBotChat")
    update = braced_body(player, update_at, "hhArtificialPlayer::UpdateBotChat")
    for needle in (
        "gameLocal.time < botPendingChatTime",
        "wasReply",
        "ReplyDispatchSuppressed",
        "ProcessChatMessage(",
        "botPendingChat.Clear()",
    ):
        require(update, needle, "hhArtificialPlayer::UpdateBotChat")
    require_order(
        update,
        "ReplyDispatchSuppressed",
        "ProcessChatMessage(",
        "hhArtificialPlayer::UpdateBotChat recursion guard",
    )

    require(manager, "ReleaseCharacter", "rvBotCharacterManager")


def validate_reply_runtime() -> None:
    """Guard the server-only conversational-reply path and its loop brakes."""

    if not GAME_SOURCE.is_dir():
        return

    character = strip_comments(read(GAME_SOURCE / "bots" / "BotCharacter.cpp"))
    multiplayer = strip_comments(read(GAME_SOURCE / "MultiplayerGame.cpp"))
    player = strip_comments(read(PREY_SOURCE / "game_player.cpp"))
    prey_items = strip_comments(read(PREY_SOURCE / "prey_items.cpp"))

    normalize_at = character.index("void rvBotCharacterManager::NormalizeReplyText")
    phrase_at = character.index(
        "bool rvBotCharacterManager::ReplyPhraseMatches", normalize_at
    )
    normalize = character[normalize_at:phrase_at]
    for needle in (
        "RemoveEscapes( S_ESCAPE_ALL )",
        "CharIsAlpha(",
        "CharIsNumeric(",
        "ToLower(",
    ):
        require(normalize, needle, "rvBotCharacterManager::NormalizeReplyText")

    phrase_end = character.index(
        "bool rvBotCharacterManager::ReplyNameMatches", phrase_at
    )
    phrase = character[phrase_at:phrase_end]
    require(phrase, "leftBoundary", "ReplyPhraseMatches whole-word left boundary")
    require(phrase, "rightBoundary", "ReplyPhraseMatches whole-word right boundary")

    process_at = multiplayer.index("void idMultiplayerGame::ProcessChatMessage")
    process = braced_body(multiplayer, process_at, "idMultiplayerGame::ProcessChatMessage")
    for needle, context in (
        ("clientNum >= 0", "real-player source gate"),
        ("hhArtificialPlayer::ReplyDispatchSuppressed()", "generated-reply recursion gate"),
        ("rvBotCharacterManager::ReplyNameMatches(", "addressed-name matching"),
        ("HasReply(", "reply eligibility before responder selection"),
        ("TryQueueBotReply(", "reply queue dispatch"),
        ("hhArtificialPlayer::Type", "bot source/candidate classification"),
        ("team", "team-chat visibility"),
        ("addressedCandidates", "addressed responder preference"),
        ("generalCandidates", "general responder pool"),
    ):
        require(process, needle, f"idMultiplayerGame::ProcessChatMessage {context}")

    if not any(
        call in process
        for call in (
            "botCharacterManager.NormalizeReplyText(",
            "rvBotCharacterManager::NormalizeReplyText(",
        )
    ):
        raise AssertionError(
            "ProcessChatMessage does not normalize accepted text before reply matching"
        )
    if "spectating" not in process and "IsHidden()" not in process:
        raise AssertionError(
            "ProcessChatMessage does not exclude non-playing bot reply candidates"
        )

    if process.count("TryQueueBotReply(") != 1:
        raise AssertionError(
            "ProcessChatMessage must have one TryQueueBotReply call site, so one "
            "incoming line cannot make several bots answer"
        )
    require(
        process,
        "foundAddressedName ?",
        "ProcessChatMessage addressed-name preference without shorter-name fallback",
    )
    require(
        process,
        "RandomInt( candidates->Num() )",
        "ProcessChatMessage selects one reply responder",
    )
    require_regex(
        process,
        r"TryQueueBotReply\s*\([^;]*\bp\s*,\s*team\s*\)",
        "ProcessChatMessage passes the authoritative source player to the responder",
    )

    # Combat and match transitions are the authoritative hooks for voice-bank
    # events; polling scores or health would duplicate lines around snapshots.
    death_at = multiplayer.index("void idMultiplayerGame::PlayerDeath")
    death = braced_body(multiplayer, death_at, "idMultiplayerGame::PlayerDeath")
    require(death, "OnBotKill(", "idMultiplayerGame::PlayerDeath kill hook")
    require(death, "OnBotDeath(", "idMultiplayerGame::PlayerDeath death hook")
    require(death, "GetWeaponName(", "idMultiplayerGame::PlayerDeath weapon context")

    state_at = multiplayer.index("void idMultiplayerGame::NewState")
    state = braced_body(multiplayer, state_at, "idMultiplayerGame::NewState")
    require(state, "OnBotMatchStart(", "idMultiplayerGame::NewState GAMEON hook")
    require(state, "OnBotMatchEnd(", "idMultiplayerGame::NewState GAMEREVIEW hook")

    # The item-denied bank is reachable only when an authoritative successful
    # multiplayer pickup tells bots which visible goal and player won the race.
    denied_at = player.index("void hhArtificialPlayer::OnBotItemPickedUp")
    denied = braced_body(player, denied_at, "hhArtificialPlayer::OnBotItemPickedUp")
    for needle in (
        "botItemGoal.GetEntity() != item",
        "picker == this",
        'GetString( "inv_name"',
        'GetString( "classname"',
        "QueueBotChat( BOTCHAT_ITEM_DENIED, picker, NULL, itemName.c_str() )",
    ):
        require(denied, needle, "hhArtificialPlayer::OnBotItemPickedUp")

    pickup_at = prey_items.index("bool hhItem::MultiplayerPickup")
    pickup = braced_body(prey_items, pickup_at, "hhItem::MultiplayerPickup")
    require(pickup, "hhArtificialPlayer::Type", "multiplayer item-denied bot fanout")
    require(pickup, "OnBotItemPickedUp( this, player )", "multiplayer item-denied hook")
    require_order(
        pickup,
        "GiveToPlayer(player)",
        "OnBotItemPickedUp( this, player )",
        "hhItem::MultiplayerPickup successful-pickup ordering",
    )
    require_order(
        pickup,
        "OnBotItemPickedUp( this, player )",
        "DetermineRemoveOrRespawn(",
        "hhItem::MultiplayerPickup item lifetime ordering",
    )


def validate_bot_runtime_regressions() -> None:
    """Guard network identity and the easy-to-regress combat safety fixes."""

    if not GAME_SOURCE.is_dir():
        return

    player = strip_comments(read(PREY_SOURCE / "game_player.cpp"))
    player_header = strip_comments(read(PREY_SOURCE / "game_player.h"))
    game_local = strip_comments(read(GAME_SOURCE / "Game_local.cpp"))
    game_header = strip_comments(read(GAME_SOURCE / "Game_local.h"))
    game_network = strip_comments(read(GAME_SOURCE / "Game_network.cpp"))
    multiplayer = strip_comments(read(GAME_SOURCE / "MultiplayerGame.cpp"))

    # Engine-free AP slots may be claimed by a real async connection.  Lower
    # the desired count before deleting the AP so the per-frame reconciler
    # cannot refill the slot during the SCS_CONNECTED -> ServerClientBegin gap.
    connect_at = game_network.index("void idGameLocal::ServerClientConnect")
    connect = braced_body(game_network, connect_at, "idGameLocal::ServerClientConnect")
    for needle in (
        "clientConnectionPending[ clientNum ] = true",
        "hhArtificialPlayer::Type",
        "NumArtificialPlayers()",
        "g_artificialPlayerCount.SetInteger(",
        "GAME_RELIABLE_MESSAGE_DELETE_ENT",
        "GetSpawnId( entities[ clientNum ] )",
        "ServerSendReliableMessageExcluding( clientNum, deleteMsg )",
        "delete entities[ clientNum ]",
    ):
        require(connect, needle, "real-client artificial-player displacement")
    require_order(
        connect,
        "g_artificialPlayerCount.SetInteger(",
        "delete entities[ clientNum ]",
        "real-client bot displacement before reconciliation can refill the slot",
    )
    require_order(
        connect,
        "ServerSendReliableMessageExcluding( clientNum, deleteMsg )",
        "delete entities[ clientNum ]",
        "existing clients delete a displaced AP before the slot becomes human",
    )

    # A connection can also claim an empty slot while the desired bot count is
    # nonzero.  Reserve the SCS_CONNECTED -> ServerClientBegin gap explicitly,
    # or the per-frame reconciler can spawn a bot into the pending human slot.
    require_regex(
        game_header,
        r"\bbool\s+clientConnectionPending\s*\[\s*MAX_CLIENTS\s*\]",
        "pending async-client slot state",
    )
    require(
        game_local,
        "memset( clientConnectionPending, 0, sizeof( clientConnectionPending ) )",
        "pending async-client slot state initialization",
    )
    require(
        game_local,
        "!clientConnectionPending[clientNum]",
        "manual bot spawn excludes pending async-client slots",
    )
    require(
        game_local,
        "if ( clientConnectionPending[i] )",
        "bot reconciler counts pending async-client slots",
    )
    pending_slot = game_local.index("if ( clientConnectionPending[i] )")
    pending_body = braced_body(
        game_local, pending_slot, "pending async-client slot reconciliation"
    )
    require(
        pending_body,
        "continue",
        "pending async-client slot is not double-counted as an entity slot",
    )
    begin_at = game_network.index("void idGameLocal::ServerClientBegin")
    begin = braced_body(game_network, begin_at, "idGameLocal::ServerClientBegin")
    require(
        begin,
        "clientConnectionPending[ clientNum ] = false",
        "client begin releases slot reservation",
    )
    require_order(
        begin,
        "SpawnPlayer( clientNum )",
        "clientConnectionPending[ clientNum ] = false",
        "client slot remains reserved until its player exists",
    )
    disconnect_at = game_network.index("void idGameLocal::ServerClientDisconnect")
    disconnect = braced_body(
        game_network, disconnect_at, "idGameLocal::ServerClientDisconnect"
    )
    require(
        disconnect,
        "clientConnectionPending[ clientNum ] = false",
        "client disconnect releases slot reservation",
    )

    # Artificial-player spawn args exist only on the authoritative server.  A
    # client must therefore receive the resolved character, skill, model and
    # team, rather than independently selecting whatever local content happens
    # to be free.  Keep the dictionary first in the entity snapshot: Prey's
    # idBitMsgDelta::WriteDict/ReadDict assign the shared `changed` flag, so
    # placing it after the parent fields can erase a real parent delta.
    write_at = player.index("void hhArtificialPlayer::WriteToSnapshot")
    write_snapshot = braced_body(
        player, write_at, "hhArtificialPlayer::WriteToSnapshot"
    )
    read_at = player.index("void hhArtificialPlayer::ReadFromSnapshot")
    read_snapshot = braced_body(
        player, read_at, "hhArtificialPlayer::ReadFromSnapshot"
    )
    write_dict = require_regex(
        write_snapshot, r"\bmsg\s*\.\s*WriteDict\s*\(",
        "artificial-player identity snapshot",
    )
    require(
        write_snapshot,
        "gameLocal.GetUserInfo(",
        "full authoritative artificial-player userinfo snapshot",
    )
    read_dict = require_regex(
        read_snapshot, r"\.\s*ReadDict\s*\(",
        "artificial-player identity snapshot",
    )
    parent_write = write_snapshot.find("hhPlayer::WriteToSnapshot(")
    parent_read = read_snapshot.find("hhPlayer::ReadFromSnapshot(")
    if parent_write == -1 or write_dict.start() > parent_write:
        raise AssertionError(
            "artificial-player dictionary must precede hhPlayer::WriteToSnapshot"
        )
    if parent_read == -1 or read_dict.start() > parent_read:
        raise AssertionError(
            "artificial-player dictionary must precede hhPlayer::ReadFromSnapshot"
        )
    first_write = re.search(r"\.\s*(Write\w*)\s*\(", write_snapshot)
    if first_write is None or first_write.group(1) != "WriteDict":
        raise AssertionError(
            "hhArtificialPlayer::WriteToSnapshot writes a field before its identity "
            "dictionary; WriteDict must be the first msg write"
        )
    first_read = re.search(r"\.\s*(Read\w*)\s*\(", read_snapshot)
    if first_read is None or first_read.group(1) != "ReadDict":
        raise AssertionError(
            "hhArtificialPlayer::ReadFromSnapshot reads a field before its identity "
            "dictionary; ReadDict must be the first msg read"
        )
    require(read_snapshot, "idDict", "artificial-player snapshot dictionary staging")
    require(read_snapshot, "gameLocal.SetUserInfo(", "artificial-player snapshot identity apply")
    if read_dict.start() > read_snapshot.find("gameLocal.SetUserInfo("):
        raise AssertionError(
            "artificial-player snapshot dictionary is applied before it is read"
        )
    require_order(
        read_snapshot,
        "gameLocal.SetUserInfo(",
        "hhPlayer::ReadFromSnapshot(",
        "artificial-player identity before parent snapshot",
    )

    userinfo_at = game_local.index("void idGameLocal::GetAPUserInfo")
    userinfo = braced_body(game_local, userinfo_at, "idGameLocal::GetAPUserInfo")
    for key in ("ui_name", "ui_modelNum", "ui_team"):
        require(userinfo, f'"{key}"', f"artificial-player {key} publication")
    require(read_snapshot, "ApplyBotUserInfo(", "authoritative bot identity snapshot apply")
    require(read_snapshot, "SetPlayerModel(", "authoritative bot model snapshot apply")
    apply_at = player.index("void hhArtificialPlayer::ApplyBotUserInfo")
    apply_userinfo = braced_body(
        player, apply_at, "hhArtificialPlayer::ApplyBotUserInfo"
    )
    require(
        apply_userinfo,
        'GetString( "bot_character"',
        "authoritative bot character snapshot apply",
    )
    require(
        apply_userinfo,
        'GetInt( "bot_skill"',
        "authoritative bot skill snapshot apply",
    )
    require(apply_userinfo, "BindBotCharacter(", "authoritative bot trait rebind")

    ap_spawn_at = player.index("void hhArtificialPlayer::Spawn")
    ap_spawn = braced_body(player, ap_spawn_at, "hhArtificialPlayer::Spawn")
    for key in ("bot_character", "bot_skill"):
        require(ap_spawn, f'"{key}"', f"artificial-player Spawn {key} publication")

    # A late joiner receives reliable player-spawn messages before it can rely
    # on PVS snapshots.  Synthetic slots are absent from the engine's normal
    # client-info loop, so invoke the engine's synchronous updateUI publisher
    # immediately before asking the client to spawn that slot.
    initial_at = game_network.index("void idGameLocal::ServerWriteInitialReliableMessages")
    initial_messages = braced_body(
        game_network, initial_at, "idGameLocal::ServerWriteInitialReliableMessages"
    )
    require(initial_messages, "hhArtificialPlayer::Type", "late-join artificial-player gate")
    require(initial_messages, '"updateUI %d', "late-join bot userinfo publication")
    require(initial_messages, "CMD_EXEC_NOW", "synchronous late-join bot userinfo publication")
    require_regex(
        initial_messages,
        r'"updateUI %d\\n"\s*,\s*i\s*\)',
        "late-join bot userinfo targets the artificial-player slot",
    )
    require_order(
        initial_messages,
        "hhArtificialPlayer::Type",
        '"updateUI %d',
        "late-join artificial-player gate/publication order",
    )
    require_order(
        initial_messages,
        '"updateUI %d',
        "GAME_RELIABLE_MESSAGE_SPAWN_PLAYER",
        "late-join bot userinfo before artificial-player spawn",
    )

    # Prove that updateUI is not merely a similarly named console command: it
    # must synchronously reach the server's reliable, send-to-all clientinfo
    # path.  The subsequent first entity snapshot carries the AP type and
    # recycles the temporary player placeholder through the supported path.
    async_network_path = ROOT / "src" / "framework" / "async" / "AsyncNetwork.cpp"
    async_server_path = ROOT / "src" / "framework" / "async" / "AsyncServer.cpp"
    async_network = strip_comments(read(async_network_path))
    async_server = strip_comments(read(async_server_path))
    update_f_at = async_network.index("void idAsyncNetwork::UpdateUI_f")
    update_f = braced_body(async_network, update_f_at, "idAsyncNetwork::UpdateUI_f")
    require(update_f, "server.UpdateUI(", "updateUI server bridge")
    update_at = async_server.index("void idAsyncServer::UpdateUI")
    update_ui = braced_body(async_server, update_at, "idAsyncServer::UpdateUI")
    require(update_ui, "SendUserInfoBroadcast(", "updateUI reliable clientinfo publication")
    require_regex(
        update_ui,
        r"SendUserInfoBroadcast\s*\([^;]*\btrue\s*\)",
        "updateUI send-to-all publication",
    )
    client_snapshot_at = game_network.index("void idGameLocal::ClientReadSnapshot")
    client_snapshot = braced_body(
        game_network, client_snapshot_at, "idGameLocal::ClientReadSnapshot"
    )
    for needle in (
        "ent->GetType()->typeNum != typeNum",
        "delete ent",
        "SpawnEntityDef(",
    ):
        require(client_snapshot, needle, "snapshot artificial-player type recycling")
    for needle in (
        "expectedArtificialPlayerRecycle",
        "hhArtificialPlayer::Type.typeNum",
        'userInfo[i].GetString( "bot_character" )',
        "common->DPrintf(",
        "common->Warning(",
    ):
        require(
            client_snapshot,
            needle,
            "expected artificial-player placeholder upgrade diagnostics",
        )

    # The same reliable publication must be reachable both when a bot is added
    # and when botreload changes its resolved identity/model.  Accept a shared
    # helper or the proven synchronous updateUI path.
    spawn_at = game_local.index("bool idGameLocal::SpawnArtificialPlayer")
    spawn = braced_body(game_local, spawn_at, "idGameLocal::SpawnArtificialPlayer")
    rebind_at = game_local.index("void idGameLocal::RebindArtificialPlayers")
    rebind = braced_body(game_local, rebind_at, "idGameLocal::RebindArtificialPlayers")
    for body, context, slot in (
        (spawn, "bot spawn", "clientNum"),
        (rebind, "botreload", "i"),
    ):
        require(body, "GetAPUserInfo(", f"{context} canonical bot userinfo publication")
        require(body, '"updateUI %d', f"{context} reliable identity publication")
        require(body, "CMD_EXEC_NOW", f"{context} synchronous identity publication")
        require_regex(
            body,
            rf'"updateUI %d\\n"\s*,\s*{slot}\s*\)',
            f"{context} identity publication targets the bot slot",
        )
        require_order(
            body,
            "SetUserInfo(",
            '"updateUI %d',
            f"{context} identity apply/publication order",
        )
    require(rebind, "SetPlayerModel(", "botreload model refresh")

    # Team DM bots need an explicit least-populated-team choice.  GetAPUserInfo
    # must also preserve an existing ui_team during botreload rather than
    # silently moving a live bot.
    bot_setup = game_local[max(0, userinfo_at - 5000):spawn_at]
    for needle in ("GAME_TDM", '"ui_team"', '"Red"', '"Blue"'):
        require(bot_setup, needle, "balanced Team DM bot assignment")
    if not re.search(r"(?:FindKey|GetString)\s*\(\s*\"ui_team\"", userinfo):
        raise AssertionError(
            "idGameLocal::GetAPUserInfo overwrites ui_team instead of preserving a "
            "bot's assigned team across reload"
        )
    counted_balance = all(needle in bot_setup for needle in (
        "numClients", "entities[", "idPlayer::Type", 'GetString( "ui_team"'
    )) and re.search(r"\+\+\s*\w*team\w*\s*\[", bot_setup, re.IGNORECASE)
    if not counted_balance and "BalanceTDM(" not in bot_setup:
        raise AssertionError(
            "Team DM bot assignment does not count the live teams or use BalanceTDM"
        )

    # GetViewAngles is already in world space.  Multiplying it by the gravity
    # axis a second time breaks FOV and steering on wall-walk surfaces; vision,
    # aiming and usercmd steering instead start in the untransformed local frame
    # and project through the interpolated eye axis exactly once.
    can_see_at = player.index("bool hhArtificialPlayer::BotCanSee")
    can_see = braced_body(player, can_see_at, "hhArtificialPlayer::BotCanSee")
    require(can_see, "GetUntransformedViewAngles(", "gravity-local artificial-player FOV")
    require(can_see, "GetEyeAxis(", "artificial-player FOV projected to world")
    if "GetViewAngles(" in can_see or "GetGravityAxis(" in can_see:
        raise AssertionError(
            "hhArtificialPlayer::BotCanSee mixes the world view or raw gravity axis "
            "with its gravity-local eye frame"
        )
    movement_at = player.index("void hhArtificialPlayer::UpdateBotMovement")
    movement = braced_body(player, movement_at, "hhArtificialPlayer::UpdateBotMovement")
    if not any(name in movement for name in (
        "GetUntransformedViewAngles(", "GetUntransformedViewAxis("
    )):
        raise AssertionError(
            "hhArtificialPlayer::UpdateBotMovement does not build steering from the "
            "gravity-local view frame"
        )
    if "GetEyeAxis(" not in movement and "GetGravityAxis(" not in movement:
        raise AssertionError(
            "hhArtificialPlayer::UpdateBotMovement never projects local steering to world"
        )
    aim_at = player.index("void hhArtificialPlayer::UpdateBotAimAndFire")
    aim = braced_body(player, aim_at, "hhArtificialPlayer::UpdateBotAimAndFire")
    require(aim, "GetUntransformedViewAngles(", "gravity-local artificial-player aim")
    require(aim, "AxisProjection(", "gravity-relative enemy target height")
    if "GetViewAngles(" in aim:
        raise AssertionError(
            "hhArtificialPlayer::UpdateBotAimAndFire mixes world view angles with a "
            "gravity-local desired direction"
        )
    bad_double_transform = re.compile(
        r"GetViewAngles\s*\(\s*\)\s*\.ToMat3\s*\(\s*\)\s*\*\s*"
        r"(?:physicsObj\.)?GetGravityAxis\s*\("
    )
    if bad_double_transform.search(player):
        raise AssertionError(
            "artificial-player code still double-applies gravity to world view angles"
        )

    # Several valid roster names contain another valid name (Elite Hunter /
    # Hunter and Mohawk Hider / Hider).  A direct reply must go to the longest
    # matching addressed name, independent of entity iteration order.
    process_at = multiplayer.index("void idMultiplayerGame::ProcessChatMessage")
    process = braced_body(multiplayer, process_at, "idMultiplayerGame::ProcessChatMessage")
    require(process, "GetBotCharacterName(", "addressed bot-name matching")
    require(process, ".Length()", "longest addressed bot-name matching")
    longest_markers = (
        "addressedCandidates.Clear(",
        "longestAddress",
        "longestName",
        "maxAddress",
        "bestAddress",
    )
    if not any(marker.lower() in process.lower() for marker in longest_markers):
        raise AssertionError(
            "ProcessChatMessage does not retain only the longest matching addressed "
            "bot name"
        )

    # Weapon ownership alone is insufficient: SelectWeapon rejects locked,
    # spirit-only and empty weapons.  Preference scoring must use the same
    # eligibility facts so bots do not repeatedly select a weapon that can
    # never become active.
    weapon_at = player.index("void hhArtificialPlayer::UpdateBotWeapon")
    weapon = braced_body(player, weapon_at, "hhArtificialPlayer::UpdateBotWeapon")
    for needle in (
        "SkipWeapon(",
        "weaponFlags",
        "inventory.HasAmmo(",
        "inventory.HasAltAmmo(",
        "weapon%d_allowempty",
    ):
        require(weapon, needle, "artificial-player usable weapon filtering")

    # A new match cannot inherit revenge targeting, a stale enemy/item handle,
    # old aim acquisition or a half-complete burst from the previous round.
    match_at = player.index("void hhArtificialPlayer::OnBotMatchStart")
    match_start = braced_body(player, match_at, "hhArtificialPlayer::OnBotMatchStart")
    reset_scope = match_start
    reset_call = re.search(r"\b((?:Reset|Clear)Bot\w*)\s*\(\s*\)\s*;", match_start)
    if reset_call:
        helper_signature = f"void hhArtificialPlayer::{reset_call.group(1)}"
        helper_at = player.index(helper_signature)
        reset_scope += braced_body(player, helper_at, helper_signature)
    for needle in (
        "botLastKiller = -1",
        "botEnemy = NULL",
        "botItemGoal = NULL",
        "botIgnoredItem = NULL",
        "botLastSeenPosition = vec3_origin",
        "botBelievedTarget = vec3_origin",
        "botTargetAcquiredTime = 0",
        "botLastSeenTime = 0",
        "botAimSettledTime = 0",
        "botBurstEndTime = 0",
        "botNextBurstTime = 0",
        "botMistakeEndTime = 0",
        "botItemLastProgressTime = -1",
        "botItemBestDistance = idMath::INFINITY",
        "botPendingChat.Clear()",
        "botPendingChatTime = 0",
        "botPendingChatTeam = false",
        "botPendingChatIsReply = false",
    ):
        require(reset_scope, needle, "new-match artificial-player state reset")
    require_order(
        match_start,
        "botPendingChat.Clear()",
        "QueueBotChat( BOTCHAT_LEVELSTART )",
        "new-match stale chat cleared before level-start chat",
    )

    # An active-looking pickup can still be unusable or unreachable.  Goal
    # selection and continued steering must both consult an eligibility/progress
    # check, and abandoning a failed goal needs memory so the next 500 ms scan
    # does not immediately select it again.
    for marker in (
        "botIgnoredItem",
        "botItemCloseTime",
        "botItemIgnoreUntil",
        "botItemLastProgressTime",
        "botItemBestDistance",
    ):
        require(player_header, marker, "failed item-goal memory")
        require(movement, marker, "failed item-goal abandonment")
    for needle in (
        "BOT_ITEM_PROGRESS_DISTANCE",
        "BOT_ITEM_PROGRESS_TIMEOUT",
        "itemDistance + BOT_ITEM_PROGRESS_DISTANCE < botItemBestDistance",
        "botItemLastProgressTime >= 0",
        "gameLocal.time - botItemLastProgressTime >= BOT_ITEM_PROGRESS_TIMEOUT",
    ):
        require(movement, needle, "unreachable visible item progress timeout")
    ignore_at = movement.find("botIgnoredItem = botItemGoal")
    if ignore_at == -1 or movement.find("botItemGoal = NULL", ignore_at) == -1:
        raise AssertionError(
            "failed item goal is cleared without first being remembered for exclusion"
        )
    require_regex(
        movement,
        r"item\s*==\s*botIgnoredItem\.GetEntity\s*\(\s*\)[^)]*"
        r"gameLocal\.time\s*<\s*botItemIgnoreUntil",
        "failed item excluded from subsequent scans",
    )
    if movement.count("botItemGoal = NULL") < 2:
        raise AssertionError(
            "hhArtificialPlayer::UpdateBotMovement does not abandon an invalid/stalled "
            "item goal after selecting it"
        )


def main() -> int:
    try:
        validate_reply_matcher_vectors()
        validate_wiring()
        validate_manager()
        validate_content()
        validate_cvars_and_commands()
        validate_chat_path()
        validate_reply_runtime()
        validate_bot_runtime_regressions()
    except AssertionError as error:
        print(f"mp_bot_characters: FAILED - {error}")
        return 1

    print("mp_bot_characters: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
