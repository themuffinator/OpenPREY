#!/usr/bin/env python3
"""Cross-platform contract tests for fail-closed macOS universal2 assembly."""

from __future__ import annotations

import importlib.util
import json
import shutil
import stat
from types import SimpleNamespace
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ASSEMBLER_PATH = ROOT / "tools" / "build" / "assemble_macos_universal2.py"
PACKAGER_PATH = ROOT / "tools" / "build" / "package_nightly.py"
VALIDATOR_PATH = ROOT / "tools" / "validation" / "openq4_validate.py"


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not import {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ASSEMBLER = load_module(ASSEMBLER_PATH, "openq4_macos_universal2_assembler")
PACKAGER = load_module(PACKAGER_PATH, "openq4_macos_universal2_packager")
VALIDATOR = load_module(VALIDATOR_PATH, "openq4_macos_universal2_validator")


def write_file(path: Path, data: bytes, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    path.chmod(mode)


def populate_staging(root: Path, arch: str, *, shared_value: bytes = b"same\n") -> None:
    shared = {
        "openPREY.icns": shared_value,
        "collect_macos_support_info.sh": b"#!/bin/sh\n",
        "assets/splash/prey_rt_bitmap_4001.bmp": b"bmp\n",
        "basepr/mod.json": b"{}\n",
        "basepr/pak0.pk4": b"pak0\n",
        "basepr/pak1.pk4": b"pak1\n",
        # libMoltenVK.dylib is third-party and already universal upstream, so both
        # thin trees stage the SAME bytes and it flows through as ordinary shared
        # payload rather than as a lipo-merged code key. The literal is deliberately
        # arch-independent: staging different MoltenVK builds per arch is exactly
        # what validate_matching_inputs() must reject.
        ASSEMBLER.MOLTENVK_DYLIB_NAME: b"moltenvk-universal\n",
    }
    for relative, data in shared.items():
        write_file(root / relative, data, 0o755 if relative.endswith((".sh", ".dylib")) else 0o644)
    for key, relative in ASSEMBLER.thin_code_paths(arch).items():
        write_file(root / relative, f"{key}-{arch}\n".encode(), 0o755)


def thin_manifest(arch: str, shared_records: dict[str, dict[str, object]]) -> dict[str, object]:
    binaries = {}
    for key, relative in ASSEMBLER.thin_code_paths(arch).items():
        binaries[key] = {
            "path": relative.as_posix(),
            "sha256": "0" * 64,
            "size": 1,
            "mode": 0o755,
            "machoArchitectures": sorted(ASSEMBLER.THIN_MACHO_ARCHES[arch]),
            "minimumOS": "11.0",
            "installName": ASSEMBLER.expected_install_name(key, arch),
            "dependencies": ["/usr/lib/libSystem.B.dylib"],
        }
    return {
        "format": 1,
        "architecture": arch,
        "machoArchitectures": sorted(ASSEMBLER.THIN_MACHO_ARCHES[arch]),
        "projectGitCommit": "a" * 40,
        "projectGitDirty": False,
        "gameLibsGitCommit": "b" * 40,
        "gameLibsGitDirty": False,
        "stagedSourceSha256": "c" * 64,
        "graphicsBridge": "metal",
        "openALProvider": "apple_framework",
        "deploymentTarget": "11.0",
        "buildType": "release",
        "sharedPayloadSha256": ASSEMBLER.canonical_json_sha256(list(shared_records.values())),
        "sharedFileCount": len(shared_records),
        "binaries": binaries,
    }


def expect_error(fragment: str, callback, label: str) -> None:
    try:
        callback()
    except ASSEMBLER.Universal2Error as exc:
        if fragment not in str(exc):
            raise AssertionError(f"{label}: unexpected error: {exc}") from exc
    else:
        raise AssertionError(f"{label}: expected Universal2Error")


def test_tree_classification_and_shared_matching() -> None:
    work = ROOT / ".tmp" / "macos-universal2-assembly-contract"
    arm_root = work / "arm64"
    x64_root = work / "x64"
    shutil.rmtree(work, ignore_errors=True)
    try:
        populate_staging(arm_root, "arm64")
        populate_staging(x64_root, "x64")
        arm_code, arm_shared = ASSEMBLER.classify_staged_tree(arm_root, "arm64")
        x64_code, x64_shared = ASSEMBLER.classify_staged_tree(x64_root, "x64")
        # The unified Prey module plus client, dedicated, and Vulkan renderer
        # make up the four code artifacts fused into universal2.
        if set(arm_code) != set(ASSEMBLER.CODE_KEYS) or set(x64_code) != set(ASSEMBLER.CODE_KEYS):
            raise AssertionError("thin staging classifier did not find the four required binaries")
        if arm_shared != x64_shared:
            raise AssertionError("byte-identical shared payloads did not normalize equally")

        arm_manifest = thin_manifest("arm64", arm_shared)
        x64_manifest = thin_manifest("x64", x64_shared)
        ASSEMBLER.validate_matching_inputs(arm_manifest, x64_manifest, arm_shared, x64_shared)

        x64_manifest["graphicsBridge"] = "opengl"
        expect_error(
            "graphicsBridge",
            lambda: ASSEMBLER.validate_matching_inputs(arm_manifest, x64_manifest, arm_shared, x64_shared),
            "mismatched graphics bridges",
        )
        x64_manifest["graphicsBridge"] = "metal"

        write_file(x64_root / "openPREY.icns", b"different\n")
        _, changed_shared = ASSEMBLER.classify_staged_tree(x64_root, "x64")
        expect_error(
            "shared payloads are not identical",
            lambda: ASSEMBLER.validate_matching_inputs(arm_manifest, x64_manifest, arm_shared, changed_shared),
            "mismatched shared payloads",
        )

        write_file(arm_root / "openPREY-client_x64", b"stale\n", 0o755)
        expect_error(
            "stale or mismatched code file",
            lambda: ASSEMBLER.classify_staged_tree(arm_root, "arm64"),
            "stale thin binary",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def test_thin_manifest_metadata_reinspection_contract() -> None:
    work = ROOT / ".tmp" / "macos-universal2-metadata-contract"
    arm_root = work / "arm64"
    shutil.rmtree(work, ignore_errors=True)
    try:
        populate_staging(arm_root, "arm64")
        code_records, shared_records = ASSEMBLER.classify_staged_tree(arm_root, "arm64")
        manifest = thin_manifest("arm64", shared_records)
        for key, record in code_records.items():
            manifest["binaries"][key].update(record)

        ASSEMBLER.validate_thin_manifest_metadata(manifest, "arm64")
        manifest["binaries"]["client"]["dependencies"] = ["/usr/lib/libSystem.B.dylib", "/System/Library/Frameworks/Cocoa.framework/Cocoa"]
        expect_error(
            "invalid client dependencies",
            lambda: ASSEMBLER.validate_thin_manifest_metadata(manifest, "arm64"),
            "unsorted thin dependency manifest",
        )
        manifest["binaries"]["client"]["dependencies"] = ["/usr/lib/libSystem.B.dylib"]

        original_collect = ASSEMBLER.collect_binary_metadata
        original_validate_arches = ASSEMBLER.validate_exact_arches
        try:
            inspected = {key: dict(record) for key, record in manifest["binaries"].items()}
            ASSEMBLER.collect_binary_metadata = lambda root, arch, deployment_target: inspected
            ASSEMBLER.validate_exact_arches = lambda path, expected: None
            ASSEMBLER.validate_recorded_tree(arm_root, "arm64", manifest)

            inspected["game"]["minimumOS"] = "12.0"
            expect_error(
                "Mach-O metadata changed after recording",
                lambda: ASSEMBLER.validate_recorded_tree(arm_root, "arm64", manifest),
                "redetected thin Mach-O metadata mismatch",
            )
        finally:
            ASSEMBLER.collect_binary_metadata = original_collect
            ASSEMBLER.validate_exact_arches = original_validate_arches
    finally:
        shutil.rmtree(work, ignore_errors=True)


def test_source_provenance_validation() -> None:
    work = ROOT / ".tmp" / "macos-universal2-source-manifest-contract"
    manifest_path = work / "manifest.json"
    shutil.rmtree(work, ignore_errors=True)
    try:
        work.mkdir(parents=True)
        manifest = {
            "format": 1,
            "projectGitCommit": "a" * 40,
            "projectGitDirty": False,
            "gameLibsGitCommit": "b" * 40,
            "gameLibsGitDirty": False,
            "fileCount": 2,
            "files": [
                {"path": "src/game/A.cpp", "sha256": "1" * 64},
                {"path": "src/mpgame/B.cpp", "sha256": "2" * 64},
            ],
        }
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        loaded = ASSEMBLER.load_source_manifest(manifest_path)
        if loaded["projectGitCommit"] != "a" * 40 or loaded["stagedSourceSha256"] == "":
            raise AssertionError("valid staged-source provenance was not loaded")

        manifest["projectGitDirty"] = True
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        expect_error(
            "clean source trees",
            lambda: ASSEMBLER.load_source_manifest(manifest_path),
            "dirty source provenance",
        )

        manifest["projectGitDirty"] = False
        manifest["files"][1]["path"] = "../escape"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        expect_error(
            "unsafe entry",
            lambda: ASSEMBLER.load_source_manifest(manifest_path),
            "unsafe staged-source path",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def test_exact_slice_contracts() -> None:
    expected = frozenset(("arm64", "x86_64"))
    if PACKAGER.macos_expected_lipo_arches("universal2") != expected:
        raise AssertionError("packager universal2 mapping is not the exact dual-slice set")
    if VALIDATOR.macos_expected_lipo_arches("universal2") != expected:
        raise AssertionError("staged validator universal2 mapping is not the exact dual-slice set")
    if PACKAGER.macos_expected_lipo_arch("arm64") != "arm64":
        raise AssertionError("thin compatibility mapping changed")
    try:
        PACKAGER.macos_expected_lipo_arch("universal2")
    except RuntimeError:
        pass
    else:
        raise AssertionError("singleton compatibility mapping accepted universal2")


def make_universal_symbol_manifest() -> bytes:
    version = "0.1.10"
    version_tag = "universal2-ci"
    arch = "universal2"
    suffix = "-opengl"
    runtime_archive = "openprey-universal2-ci-macos-universal2-opengl.tar.xz"
    symbol_archive = "openprey-universal2-ci-macos-universal2-opengl-symbols.tar.xz"
    records = (
        ("openPREY.app/Contents/MacOS/openPREY", "openPREY.app.dSYM"),
        ("openPREY-client_universal2", "openPREY-client_universal2.dSYM"),
        ("openPREY-ded_universal2", "openPREY-ded_universal2.dSYM"),
        ("openPREY.app/Contents/Frameworks/game_universal2.dylib", "game_universal2.dylib.dSYM"),
        # The fused renderer module gets a dSYM like any other openPREY-built code.
        # libMoltenVK.dylib rides in the same Frameworks directory but has no
        # record here: it is third-party, ships no DWARF, and the packager's
        # expected-binary set excludes it.
        ("openPREY.app/Contents/Frameworks/renderer-vk_universal2.dylib", "renderer-vk_universal2.dylib.dSYM"),
    )
    lines = [
        "openPREY macOS symbols",
        "format=1",
        f"version={version}",
        f"version_tag={version_tag}",
        "platform=macos",
        f"arch={arch}",
        f"package_suffix={suffix}",
        f"runtime_archive={runtime_archive}",
        f"symbol_archive={symbol_archive}",
        "",
        "binaries:",
    ]
    for index, (path, dsym) in enumerate(records, start=1):
        lines.extend(
            (
                f"- path={path}",
                f"  sha256={index:064x}",
                "  size=1",
                "  macho_uuid="
                f"UUID: 00000000-0000-0000-0000-{index:012x} (arm64) {path}; "
                f"UUID: 10000000-0000-0000-0000-{index:012x} (x86_64) {path}",
                f"  dsym=dSYMs/{dsym}",
            )
        )
    return ("\n".join(lines) + "\n").encode("utf-8")


def test_universal_symbol_manifest_contract() -> None:
    data = make_universal_symbol_manifest()
    kwargs = {
        "version": "0.1.10",
        "version_tag": "universal2-ci",
        "arch": "universal2",
        "package_suffix": "-opengl",
        "runtime_archive_name": "openprey-universal2-ci-macos-universal2-opengl.tar.xz",
        "symbol_archive_name": "openprey-universal2-ci-macos-universal2-opengl-symbols.tar.xz",
    }
    PACKAGER.validate_macos_symbol_manifest_bytes(data, "universal2 symbol manifest", **kwargs)
    bad_data = data.replace(b"(x86_64)", b"(arm64)")
    try:
        PACKAGER.validate_macos_symbol_manifest_bytes(
            bad_data,
            "bad universal2 symbol manifest",
            **kwargs,
        )
    except RuntimeError as exc:
        if "invalid macho_uuid" not in str(exc):
            raise AssertionError(f"unexpected universal2 UUID rejection: {exc}") from exc
    else:
        raise AssertionError("universal2 symbol manifest accepted duplicate arm64 UUID slices")


def test_universal_dsym_uuid_pair_contract() -> None:
    original_platform = PACKAGER.sys.platform
    original_which = PACKAGER.shutil.which
    original_command = PACKAGER.run_macos_command
    binary = Path("/tmp/openPREY-client_universal2")
    dsym = Path("/tmp/openPREY-client_universal2.dSYM")
    binary_output = (
        "UUID: 00000000-0000-0000-0000-000000000001 (arm64) /tmp/openPREY-client_universal2\n"
        "UUID: 00000000-0000-0000-0000-000000000002 (x86_64) /tmp/openPREY-client_universal2\n"
    )
    try:
        PACKAGER.sys.platform = "darwin"
        PACKAGER.shutil.which = lambda name: name
        PACKAGER.run_macos_command = lambda command, *, label: SimpleNamespace(stdout=binary_output)
        PACKAGER.validate_macos_dsym_matches_binary(binary, dsym)

        mismatched_output = binary_output.replace("000000000002", "000000000003")
        PACKAGER.run_macos_command = (
            lambda command, *, label: SimpleNamespace(
                stdout=mismatched_output if str(command[-1]).endswith(".dSYM") else binary_output
            )
        )
        try:
            PACKAGER.validate_macos_dsym_matches_binary(binary, dsym)
        except RuntimeError as exc:
            if "UUID slices do not match" not in str(exc):
                raise AssertionError(f"unexpected dSYM UUID mismatch error: {exc}") from exc
        else:
            raise AssertionError("universal2 dSYM UUID validation accepted a mismatched x86_64 slice")
    finally:
        PACKAGER.sys.platform = original_platform
        PACKAGER.shutil.which = original_which
        PACKAGER.run_macos_command = original_command


def test_static_fail_closed_contract() -> None:
    source = ASSEMBLER_PATH.read_text(encoding="utf-8")
    required = (
        "THIN_MANIFEST_NAME",
        "projectGitCommit",
        "gameLibsGitCommit",
        "stagedSourceSha256",
        "graphicsBridge",
        "openALProvider",
        "deploymentTarget",
        "buildType",
        "sharedPayloadSha256",
        "validate_exact_arches",
        "minimum_os_version(path, macho_arch=macho_arch)",
        "dependencies(path, macho_arch=macho_arch",
        'require_tool("lipo")',
        'require_tool("otool")',
        'require_tool("install_name_tool")',
        'require_tool("codesign")',
        'require_tool("nm")',
        '"-create"',
        '"--remove-signature"',
        "GetGameAPI",
        "thin shared payloads are not identical",
        "thin macOS provenance/build settings differ",
        "universal2 output root must not overlap",
        "universal2 output root must not be a symlink",
        "assembly manifest must not be a symlink",
        "assembly manifest must be outside",
        "os.replace(temporary_root, output_root)",
    )
    for token in required:
        if token not in source:
            raise AssertionError(f"universal2 assembler is missing fail-closed token: {token}")
    if "shutil.copytree" in source:
        raise AssertionError("universal2 assembler must not blindly copy an input staging tree")

    packager_source = PACKAGER_PATH.read_text(encoding="utf-8")
    for token in (
        "macos_macho_uuid_pairs",
        "validate_macos_dsym_matches_binary",
        "macOS dSYM UUID slices do not match the distributed binary",
        "validate_macos_dsym_matches_binary(binary_path, dsym_path)",
        "macos_otool_minimum_os_version(binary_path, macho_arch=macho_arch)",
        "macos_otool_dependencies(binary_path, macho_arch=macho_arch)",
    ):
        if token not in packager_source:
            raise AssertionError(f"universal2 package-symbol contract is missing: {token}")


def main() -> int:
    test_tree_classification_and_shared_matching()
    test_thin_manifest_metadata_reinspection_contract()
    test_source_provenance_validation()
    test_exact_slice_contracts()
    test_universal_symbol_manifest_contract()
    test_universal_dsym_uuid_pair_contract()
    test_static_fail_closed_contract()
    print("macOS universal2 assembly contract checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
