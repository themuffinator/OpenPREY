#!/usr/bin/env python3
"""Focused policy and helper tests for Linux AppImage release packaging."""

from __future__ import annotations

import importlib.util
import os
import shutil
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HELPER_PATH = ROOT / "tools" / "build" / "package_linux_appimage.py"


def load_helper():
    if str(HELPER_PATH.parent) not in sys.path:
        sys.path.insert(0, str(HELPER_PATH.parent))
    spec = importlib.util.spec_from_file_location("package_linux_appimage_under_test", HELPER_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load AppImage helper: {HELPER_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


APPIMAGE = load_helper()


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        raise AssertionError(f"{label} is missing {token!r}")


def expect_appimage_error(callback, token: str, label: str) -> None:
    try:
        callback()
    except APPIMAGE.AppImageError as exc:
        if token not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected {token!r}") from exc
        return
    raise AssertionError(f"{label} did not reject malformed input")


def validate_filename_policy() -> None:
    if APPIMAGE.appimage_filename("0.1.011", "x64") != "openprey-0.1.011-x86_64.AppImage":
        raise AssertionError("x64 AppImage filename does not use the standard architecture spelling")
    if (
        APPIMAGE.appimage_filename("0.1.011", "arm64", "-preview")
        != "openprey-0.1.011-preview-aarch64.AppImage"
    ):
        raise AssertionError("preview ARM64 AppImage filename is not explicit or architecture-standard")
    expect_appimage_error(
        lambda: APPIMAGE.appimage_filename("../escape", "x64"),
        "unsafe AppImage version tag",
        "AppImage version traversal",
    )
    expect_appimage_error(
        lambda: APPIMAGE.appimage_filename("0.1.011", "x64", "../preview"),
        "unsafe AppImage package suffix",
        "AppImage suffix traversal",
    )
    expect_appimage_error(
        lambda: APPIMAGE.appimage_filename("0.1.011", "x86"),
        "unsupported AppImage architecture",
        "unsupported AppImage architecture",
    )


def validate_desktop_and_apprun_generation() -> None:
    desktop = """[Desktop Entry]
Type=Application
Name=openPREY
Exec=openPREY-client_x64
Icon=openprey
Categories=Game;
"""
    rendered = APPIMAGE.render_appimage_desktop(desktop)
    require(rendered, "Exec=openprey\n", "AppImage desktop entry")
    if "Exec=openPREY-client_x64" in rendered:
        raise AssertionError("AppImage desktop entry retained the package-specific client filename")
    expect_appimage_error(
        lambda: APPIMAGE.render_appimage_desktop(desktop + "Exec=duplicate\n"),
        "exactly one Exec key",
        "duplicate AppImage desktop Exec",
    )
    expect_appimage_error(
        lambda: APPIMAGE.render_appimage_desktop(desktop.replace("Icon=openprey", "Icon=other")),
        "Icon=openprey",
        "mismatched AppImage desktop icon",
    )

    for arch in ("x64", "arm64"):
        apprun = APPIMAGE.render_apprun(arch)
        for token in (
            "#!/bin/sh",
            "set -eu",
            "usr/share/openprey",
            f"openPREY-client_{arch}",
            "LD_LIBRARY_PATH",
            'exec "./openPREY-client_',
            '"$@"',
        ):
            require(apprun, token, f"{arch} AppRun")
        if "+set fs_basepath" in apprun or "+set fs_cdpath" in apprun:
            raise AssertionError("AppRun must preserve user asset discovery and the engine-owned cdpath lock")


def validate_tree_safety() -> None:
    with tempfile.TemporaryDirectory(prefix="openq4-appimage-policy-") as temp_name:
        root = Path(temp_name) / "AppDir"
        root.mkdir()
        (root / "payload").write_text("ok\n", encoding="utf-8")
        APPIMAGE.validate_tree_entries(root, "synthetic AppDir", allow_safe_symlinks=False)

        internal_link = root / "internal-link"
        try:
            os.symlink("payload", internal_link)
        except (OSError, NotImplementedError):
            return
        APPIMAGE.validate_tree_entries(root, "synthetic AppDir", allow_safe_symlinks=True)
        expect_appimage_error(
            lambda: APPIMAGE.validate_tree_entries(
                root, "synthetic AppDir", allow_safe_symlinks=False
            ),
            "contains a symlink",
            "source package symlink",
        )

        external = Path(temp_name) / "outside"
        external.write_text("outside\n", encoding="utf-8")
        internal_link.unlink()
        os.symlink(external, internal_link)
        expect_appimage_error(
            lambda: APPIMAGE.validate_tree_entries(
                root, "synthetic AppDir", allow_safe_symlinks=True
            ),
            "escapes its root",
            "escaping AppDir symlink",
        )


def validate_environment_safety_policy() -> None:
    helper = read("tools/build/package_linux_appimage.py")
    for token in (
        'env.pop("APPIMAGE_EXTRACT_AND_RUN", None)',
        "linux_release.sanitized_loader_environment()",
        "AppImage output must be outside the detached-symbol tree",
        "AppImage work root must be outside the source package tree",
    ):
        require(helper, token, "AppImage environment/path safety policy")


def validate_release_workflow_policy() -> None:
    workflow = read(".github/workflows/manual-release.yml")
    for token in (
        "Build and validate Linux AppImage",
        "Validate Linux AppImage under Wayland and X11",
        "Upload Linux AppImage",
        "tools/build/package_linux_appimage.py",
        "APPIMAGE_EXTRACT_AND_RUN=1",
        "SOURCE_DATE_EPOCH",
        "desktop-file-utils",
        "https://github.com/AppImage/appimagetool/releases/download/1.9.1/",
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/",
        "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0",
        "f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158",
        "c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d",
        "620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff",
        "openq4-${{ needs.metadata.outputs.version_tag }}-x86_64.AppImage",
        "openq4-${{ needs.metadata.outputs.version_tag }}-preview-aarch64.AppImage",
        "openq4-${{ needs.metadata.outputs.version_tag }}-aarch64.AppImage",
        "--executable \"${{ steps.appimage.outputs.appimage_path }}\"",
    ):
        require(workflow, token, "manual release AppImage policy")

    build_offset = workflow.index("- name: Build and validate Linux AppImage")
    smoke_offset = workflow.index("- name: Validate Linux AppImage under Wayland and X11")
    upload_offset = workflow.index("- name: Upload Linux AppImage")
    release_offset = workflow.index("  release:")
    if not build_offset < smoke_offset < upload_offset < release_offset:
        raise AssertionError("AppImage must be built, runtime-tested, and uploaded before publication")

    build_step = workflow[build_offset:smoke_offset]
    for token in (
        "sha256sum --check",
        'runtime_offset="$("${appimagetool}" --appimage-offset)"',
        "--runtime-file",
        "--symbols-dir",
        "--source-date-epoch",
        "inputs.generate_linux_arm64_evidence_candidate == false",
    ):
        require(build_step, token, "AppImage build step")
    if 'APPIMAGE_EXTRACT_AND_RUN=1 "${appimagetool}" --appimage-offset' in build_step:
        raise AssertionError("runtime extraction must query the outer AppImage runtime")


def validate_user_documentation() -> None:
    for relative, tokens in {
        "README.md": ("AppImage", "x86_64", "aarch64"),
        "BUILDING.md": ("AppImage", "package_linux_appimage.py", "APPIMAGE_EXTRACT_AND_RUN=1"),
        "docs/user/getting-started.md": (".AppImage", "chmod +x", "Prey (2006) assets"),
        "assets/release/README.html": ("AppImage", "chmod +x", "Prey (2006) assets"),
        "docs/dev/release-completion.md": ("AppImage", "Wayland", "X11"),
    }.items():
        text = read(relative)
        for token in tokens:
            require(text, token, f"AppImage documentation in {relative}")


def main() -> None:
    try:
        validate_filename_policy()
        validate_desktop_and_apprun_generation()
        validate_tree_safety()
        validate_environment_safety_policy()
        validate_release_workflow_policy()
        validate_user_documentation()
    finally:
        shutil.rmtree(ROOT / ".tmp" / "openq4-appimage-policy", ignore_errors=True)
    print("linux_appimage_packaging: ok")


if __name__ == "__main__":
    main()
