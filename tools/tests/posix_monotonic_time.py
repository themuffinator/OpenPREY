#!/usr/bin/env python3
"""Regression checks for POSIX elapsed-time accounting."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def sys_milliseconds_body(source: str) -> str:
    signature = "int Sys_Milliseconds( void ) {"
    start = source.find(signature)
    if start == -1:
        raise AssertionError("Missing Sys_Milliseconds definition")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError("Could not find end of Sys_Milliseconds definition")


def validate_posix_elapsed_clock() -> None:
    source = read("src/sys/posix/posix_main.cpp")
    body = sys_milliseconds_body(source)

    require(body, "clock_gettime( CLOCK_MONOTONIC, &ts )", "POSIX elapsed timer")
    require(body, "sys_timeBaseNs", "POSIX elapsed timer origin")
    require(body, "1000000000ull", "nanosecond conversion")
    require(body, "1000000ull", "millisecond conversion")
    require(body, "sys_lastMilliseconds", "clock query fallback")
    reject(body, "gettimeofday", "POSIX elapsed timer")
    reject(body, "struct timeval", "POSIX elapsed timer")
    reject(source, "sys_timeBase = 0", "legacy wall-clock timer origin")


def main() -> None:
    validate_posix_elapsed_clock()
    print("posix_monotonic_time: ok")


if __name__ == "__main__":
    main()
