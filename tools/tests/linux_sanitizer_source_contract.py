#!/usr/bin/env python3
"""Active platform-independent sanitizer source regression checks."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(source: str, token: str, context: str) -> None:
    if token not in source:
        raise AssertionError(f"Missing {token!r} in {context}")


def reject(source: str, token: str, context: str) -> None:
    if token in source:
        raise AssertionError(f"Unexpected {token!r} in {context}")


def validate_bse_without_pch() -> None:
    meson = read("meson.build")
    require(meson, "common_header_cpp_args = ['-include', common_header_path]", "non-PCH common-header fallback")
    require(meson, "engine_cpp_args = shared_cpp_args + common_header_cpp_args", "engine non-PCH common header")
    require(meson, "game_common_cpp_args = shared_cpp_args + common_header_cpp_args", "game non-PCH common header")

    bse_sources = sorted((ROOT / "src" / "bse").glob("*.cpp"))
    if not bse_sources:
        raise AssertionError("No in-tree BSE sources found")
    for path in bse_sources:
        source = path.read_text(encoding="utf-8")
        require(source, '#include "../idlib/precompiled.h"', f"{path.name} explicit engine declarations")
        require(source, "#pragma hdrstop", f"{path.name} precompiled-header compatibility")


def validate_cross_dso_sanitizer_boundary() -> None:
    meson = read("meson.build")
    require(
        meson,
        "linux_cross_dso_sanitizer_cpp_args += ['-fno-sanitize=vptr']",
        "Linux engine/game cross-DSO sanitizer boundary",
    )
    require(
        meson,
        "engine_cpp_args = shared_cpp_args + common_header_cpp_args + ['-D__DOOM_DLL__'] + linux_cross_dso_sanitizer_cpp_args",
        "Linux engine sanitizer scope",
    )
    require(
        meson,
        "game_common_cpp_args = shared_cpp_args + common_header_cpp_args + ['-DGAME_DLL'] + linux_cross_dso_sanitizer_cpp_args",
        "unified game-module sanitizer scope",
    )
    require(meson, "bse_cpp_args = shared_cpp_args + common_header_cpp_args", "BSE vptr sanitizer coverage")
    reject(meson, "shared_cpp_args += ['-fno-sanitize=vptr']", "over-broad UBSan vptr exclusion")
    require(meson, "dedicated_engine_cpp_args = engine_cpp_args", "dedicated sanitizer inheritance")
    require(meson, "Keep AddressSanitizer and every other requested UBSan check enabled.", "sanitizer scope")


def main() -> None:
    validate_bse_without_pch()
    validate_cross_dso_sanitizer_boundary()
    print("linux_sanitizer_source_contract: ok")


if __name__ == "__main__":
    main()
