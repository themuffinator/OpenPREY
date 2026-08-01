#!/usr/bin/env python3
"""Regression checks for Linux packaging and audit-status guidance."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def validate_building_packager_notes() -> None:
    source = read("BUILDING.md")

    require(source, "## Building on Linux / macOS", "BUILDING Linux build section")
    require(source, "1.2.0 or newer", "BUILDING Meson floor")
    require(source, "libglew-dev", "BUILDING OpenGL development dependency")
    require(source, "libopenal-dev", "BUILDING OpenAL development dependency")
    require(source, "OPENPREY_FORCE_X11=1", "BUILDING project XWayland fallback")
    require(source, "OPENPREY_GAMELIBS_REPO", "BUILDING GameLibs source-input path")
    require(source, "`src/game`, `src/Prey`, and `src/preyengine`", "BUILDING GameLibs staging inputs")
    require(source, "tools/build/meson_setup.sh", "BUILDING canonical Linux wrapper")
    require(source, "install -C builddir --no-rebuild --skip-subprojects", "BUILDING package staging command")
    require(source, "openprey-<version-tag>-linux.tar.xz", "BUILDING Linux archive identity")
    require(source, "openprey-<version-tag>-x86_64.AppImage", "BUILDING Linux AppImage identity")
    require(source, "basepr/game_<arch>.(dll|so|dylib)", "BUILDING unified game module layout")


def validate_platform_support() -> None:
    source = read("docs/dev/platform-support.md")

    require(source, "Linux x64", "platform support Linux lane")
    require(source, "Retail gameplay validation pending", "platform support runtime boundary")
    require(source, "SDL3 may use Wayland or X11", "platform support display stack")
    require(source, "OPENPREY_FORCE_X11=1", "platform support XWayland fallback")
    require(source, "OPENPREY_WAYLAND_DISABLE_LIBDECOR=1", "platform support libdecor fallback")
    require(source, "Legacy `OPENQ4_*` names remain temporary migration aliases only", "platform support migration boundary")


def validate_plan_status() -> None:
    source = read("docs/dev/prey-rebase/status-ledger.md")

    require(source, "## Explicit deferral register", "rebase status ledger")
    require(source, "TODO-RELEASE-LANES", "rebase release-lane deferral")
    require(source, "OpenPrey-game", "rebase release-lane companion contract")
    require(source, "basepr", "rebase release-lane runtime layout")
    require(source, "one `game_<arch>` module", "rebase release-lane module contract")


def validate_ci_wiring() -> None:
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    runner = read("tools/validation/openq4_validate.py")

    for source, context in ((commit, "commit script smoke"), (push, "push script smoke")):
        require(source, "tools/tests/linux_packaging_guidance.py", context)
        require(source, "python tools/tests/linux_packaging_guidance.py", context)

    require(runner, "linux_packaging_guidance.py", "validation runner")


def validate_release_note() -> None:
    source = read("docs/dev/release-completion.md")

    require(source, "Archive/AppImage tooling is rebranded for openPREY", "release completion notes")
    require(source, "Wayland/X11 runtime boundaries", "release completion notes")
    require(source, "publication lanes", "release completion notes")


def main() -> None:
    validate_building_packager_notes()
    validate_platform_support()
    validate_plan_status()
    validate_ci_wiring()
    validate_release_note()
    print("linux_packaging_guidance: ok")


if __name__ == "__main__":
    main()
