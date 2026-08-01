#!/usr/bin/env python3
"""Regression checks for language-table encoding and the engine-side transcode.

openPREY authors its string tables in UTF-8, but the stock Prey fonts are a
fixed 256-glyph atlas indexed by a raw byte, so the engine transcodes UTF-8
tables to Windows-1252 at load time (idLangDict::Load).  Two things therefore
have to stay true:

  * every repo-authored ``.lang`` file must be valid UTF-8 whose code points are
    all representable in CP1252, otherwise the transcode silently declines and
    the accents render as two wrong glyphs (GitHub issue #89);
  * the transcode itself must stay in place in both idlib copies.
"""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
STRINGS_DIR = ROOT / "content" / "basepr" / "pak0" / "strings"

# Windows-1252 0x80-0x9F block; None marks the five unassigned slots.
CP1252_HIGH = [
    0x20AC, None, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, None, 0x017D, None,
    None, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, None, 0x017E, 0x0178,
]

# CP1252 codes that land on a .notdef cell in every stock Prey font.  The
# engine folds these to ASCII rather than drawing nothing; 0xA0 in particular
# has a zero advance, so leaving it alone would delete the word gap entirely.
FOLDED_CP1252 = {0x82, 0x84, 0x85, 0x91, 0x92, 0x93, 0x94, 0x96, 0x97, 0xA0}


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def cp1252_representable(code_point: int) -> bool:
    if code_point < 0x80 or 0xA0 <= code_point <= 0xFF:
        return True
    return code_point in [value for value in CP1252_HIGH if value is not None]


def validate_lang_files() -> None:
    lang_files = sorted(STRINGS_DIR.glob("*.lang"))
    if not lang_files:
        raise AssertionError(f"No .lang files found under {STRINGS_DIR}")

    for path in lang_files:
        data = path.read_bytes()
        rel = path.relative_to(ROOT).as_posix()

        if data.startswith(b"\xef\xbb\xbf"):
            raise AssertionError(
                f"{rel} starts with a UTF-8 BOM; idLangDict::Load tolerates it but "
                "the retail parser does not - save without a BOM"
            )

        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise AssertionError(
                f"{rel} is not valid UTF-8 at byte {exc.start} ({exc.reason}). "
                "Repo-authored string tables are UTF-8; the engine transcodes them "
                "to the 8-bit font codepage at load time."
            ) from exc

        for index, char in enumerate(text):
            code_point = ord(char)
            if not cp1252_representable(code_point):
                raise AssertionError(
                    f"{rel}: U+{code_point:04X} ({char!r}) at character {index} cannot be "
                    "represented in Windows-1252, so the stock 256-glyph fonts cannot "
                    "draw it. Use a CP1252-representable character."
                )


def validate_transcode_contract() -> None:
    for relative_path in (
        "src/idlib/LangDict.cpp",
        # the game repo builds its own idlib; keep the two copies in lockstep
    ):
        source = read(relative_path)
        context = f"{relative_path} language-table transcode"
        require(source, "LangDict_ConvertUtf8ToCp1252", context)
        require(source, "LANGDICT_CP1252_HIGH", context)
        require(source, "LANGDICT_GLYPH_FOLD", context)
        require(source, "LangDict_DecodeUtf8", context)
        require(source, "LangDict_CodePointToCp1252", context)
        require(
            source,
            "idStr transcoded;",
            f"{context} (buffer must outlive the lexer)",
        )
        require(
            source,
            "src.LoadMemory( parseText, parseLength, fileName );",
            f"{context} (lexer must parse the transcoded text)",
        )

        declaration = source.index("idStr transcoded;")
        lexer = source.index("idLexer src(")
        if declaration > lexer:
            raise AssertionError(
                f"{relative_path}: 'idStr transcoded' must be declared before 'idLexer src' - "
                "idLexer::LoadMemory stores the pointer without copying, so the buffer has to "
                "be destroyed after the lexer"
            )

        # every folded code must be one the engine can actually produce
        for match in re.finditer(r"\{\s*0x([0-9A-Fa-f]{2}),\s*\"", source):
            code = int(match.group(1), 16)
            if code not in FOLDED_CP1252:
                raise AssertionError(
                    f"{relative_path}: unexpected glyph fold entry 0x{code:02X}; update "
                    "FOLDED_CP1252 in this test if the fold table intentionally changed"
                )


def validate_ci_smoke() -> None:
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")
    runner = read("tools/validation/openq4_validate.py")

    for source, context in (
        (push, "push verification workflow"),
        (commit, "commit validation workflow"),
        (runner, "validation runner"),
    ):
        require(source, "lang_table_encoding.py", context)


def main() -> None:
    validate_lang_files()
    validate_transcode_contract()
    validate_ci_smoke()
    print("lang_table_encoding: ok")


if __name__ == "__main__":
    main()
