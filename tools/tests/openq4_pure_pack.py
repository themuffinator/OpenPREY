#!/usr/bin/env python3
"""Regression checks for openPREY pure-pack handling (legacy filename)."""

from __future__ import annotations

import importlib.util
import hashlib
import shutil
import struct
import subprocess
import sys
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1:
        raise AssertionError(f"Missing ordered symbols {first!r} and/or {second!r} in {context}")
    if first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def load_package_module() -> ModuleType:
    package_path = ROOT / "tools" / "build" / "package_nightly.py"
    spec = importlib.util.spec_from_file_location("package_nightly_for_openq4_pure_pack_test", package_path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Could not load package module from {package_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_test_file(path: Path, contents: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(contents)


def expect_runtime_error(message: str, callback, context: str) -> None:
    try:
        callback()
    except RuntimeError as exc:
        if message not in str(exc):
            raise AssertionError(f"Expected {message!r} in {context}, got {exc!r}") from exc
        return
    raise AssertionError(f"Expected RuntimeError for {context}")


def validate_filesystem_pure_pack_contract() -> None:
    source = read("src/framework/FileSystem.cpp")
    md5_header = read("src/idlib/hashing/MD5.h")
    md5_source = read("src/idlib/hashing/MD5.cpp")
    async_client = read("src/framework/async/AsyncClient.cpp")
    async_server = read("src/framework/async/AsyncServer.cpp")
    helper = function_body(source, "bool idFileSystemLocal::IsOpenQ4PurePack(")
    checksum_validator = function_body(source, "bool idFileSystemLocal::ValidateOpenQ4Paks(")
    misplaced_validator = function_body(source, "bool idFileSystemLocal::FindMisplacedOfficialPaks(")
    startup = function_body(source, "void idFileSystemLocal::Startup(")
    status = function_body(source, "pureStatus_t idFileSystemLocal::GetPackStatus(")

    require(source, '#include "openq4_paks_generated.h"', "filesystem generated pack checksum header")
    require(source, "IsOpenQ4PurePack", "filesystem pure-pack declaration")
    require(source, "ValidateOpenQ4Paks", "filesystem pack checksum declaration")
    require(helper, "OPENQ4_GAMEDIR", "openQ4 pure-pack directory check")
    require(helper, '"pak0.pk4"', "openQ4 pure-pack filename check")
    require(helper, '"pak1.pk4"', "openQ4 pure-pack filename check")
    require(helper, "IsGameDirPack( pak, OPENQ4_GAMEDIR )", "openQ4 pure-pack directory check")
    require(checksum_validator, "OPENQ4_PAK0_MD5", "openQ4 pak0 expected checksum")
    require(checksum_validator, "OPENQ4_PAK1_MD5", "openQ4 pak1 expected checksum")
    require(checksum_validator, "MD5_FileChecksum", "openQ4 pak0 actual checksum")
    require(checksum_validator, "FindGamePackByName( expected.name, OPENQ4_GAMEDIR )", "openQ4 pack lookup")
    require(checksum_validator, "checksum mismatch for %s/%s", "openQ4 pack checksum diagnostic")
    require(startup, "ValidateOpenQ4Paks( openQ4PakErrors )", "startup pack checksum validation")
    require(startup, "openPREY runtime content packs in '%s' are missing or modified", "startup pack checksum fatal")
    require(startup, "openPREY runtime directory '%s' is missing a compatible mod.json", "startup mod manifest fatal")
    require(startup, "Retail Prey media pk4 files must be installed in '%s', not '%s'.", "startup misplaced retail pk4 fatal")
    require(startup, "pak000.pk4 through pak004.pk4", "startup classic retail pk4 guidance")
    require(startup, "pak_data.pk4, pak_sound.pk4, pak_en_v.pk4, and pak_en_t.pk4", "startup digital retail pk4 guidance")
    require(startup, "Do not put retail pk4 files in '%s'", "startup baseoq4 retail pk4 warning")
    require(misplaced_validator, "FindGamePackByName( info->name, OPENQ4_GAMEDIR )", "misplaced retail pk4 baseoq4 lookup")
    require(misplaced_validator, "with checksum 0x%08x but belongs in %s", "misplaced retail pk4 checksum diagnostic")
    require(misplaced_validator, "was found in both %s and %s; remove the copy from %s", "misplaced duplicate retail pk4 diagnostic")
    require(md5_header, "MD5_FileChecksum", "MD5 file checksum declaration")
    require(md5_source, "MD5_FileChecksum", "MD5 file checksum implementation")
    require(md5_source, "fopen( path, \"rb\" )", "MD5 file checksum binary read")
    require(
        md5_source,
        'static_assert( sizeof( unsigned int ) == 4, "MD5 requires 32-bit digest words" );',
        "fixed-width MD5 digest contract",
    )
    require(md5_source, "unsigned int\tdigest[4];", "fixed-width MD5 digest storage")
    require(md5_source, "unsigned int\tval;", "fixed-width MD5 checksum accumulator")
    require(md5_source, "val = digest[0] ^ digest[1] ^ digest[2] ^ digest[3];", "MD5 word folding")
    require(md5_source, "memset( ctx, 0, sizeof( *ctx ) );", "complete MD5 context clearing")
    reject(md5_source, "unsigned long\tdigest[4];", "LP64-unsafe MD5 digest storage")
    reject(md5_source, "memset( ctx, 0, sizeof( ctx ) );", "pointer-sized MD5 context clearing")

    checksum_vectors = {
        b"": 0x3B75655E,
        b"abc": 0x275FA452,
        b"openQ4": 0x8A8BAE9B,
    }
    for payload, expected in checksum_vectors.items():
        digest_words = struct.unpack("<4I", hashlib.md5(payload).digest())
        actual = digest_words[0] ^ digest_words[1] ^ digest_words[2] ^ digest_words[3]
        if actual != expected:
            raise AssertionError(
                f"MD5 block-checksum semantics changed for {payload!r}: "
                f"expected 0x{expected:08x}, got 0x{actual:08x}"
            )

    require(async_client, "Client decl checksum: 0x%08x", "client decl checksum diagnostic")
    require(async_server, "Server decl checksum: 0x%08x", "server decl checksum diagnostic")
    require(async_server, "client=0x%08x server=0x%08x (non-pure)", "non-pure mismatch diagnostic")
    require(async_server, "client=0x%08x server=0x%08x (pure)", "pure mismatch diagnostic")

    require(status, "IsOpenQ4PurePack( pak )", "GetPackStatus openQ4 pure-pack check")
    require_order(
        status,
        "IsOpenQ4PurePack( pak )",
        "FindOfficialPk4Info",
        "GetPackStatus openQ4 pure-pack precedence",
    )
    require_order(
        status,
        "IsOpenQ4PurePack( pak )",
        "// check content for PURE_NEVER",
        "GetPackStatus openQ4 pure-pack precedence",
    )

    openq4_check = status[
        status.find("if ( IsOpenQ4PurePack( pak ) )") :
        status.find("// Keep the stock Quake 4 base media")
    ]
    require(openq4_check, "pak->pureStatus = PURE_ALWAYS;", "openQ4 pure-pack status")
    require(openq4_check, "return PURE_ALWAYS;", "openQ4 pure-pack return")


def validate_install_error_console_contract() -> None:
    posix_main = read("src/sys/posix/posix_main.cpp")
    posix_signal = read("src/sys/posix/posix_signal.cpp")
    posix_console = read("src/sys/posix/posix_syscon.cpp")
    posix_header = read("src/sys/posix/posix_public.h")
    win_console = read("src/sys/win32/win_syscon.cpp")
    console_theme = read("src/sys/sys_console_theme.h")

    require(posix_header, "Posix_ConsoleSetFatalError", "POSIX fatal console declaration")
    require(posix_header, "Posix_ConsoleFatalErrorWait", "POSIX fatal console declaration")
    require(posix_main, "Sys_SetFatalError( text );", "POSIX Sys_Error fatal banner update")
    require(posix_main, "Sys_Printf( \"Sys_Error: %s\\n\", text );", "POSIX Sys_Error console log text")
    require(posix_main, "Posix_ConsoleFatalErrorWait();", "POSIX Sys_Error visible console wait")
    require(posix_signal, "Posix_ConsoleSetFatalError( fatalError );", "POSIX fatal signal bridge")
    require(posix_console, "POSIX_CONSOLE_STATUS_HEIGHT", "POSIX console status strip")
    require(posix_console, "Posix_ConsoleDrawStatus", "POSIX console status rendering")
    reject(posix_console, "statusText = \"ERROR: \";", "POSIX console fatal status text")
    require(posix_console, "statusText.Append( scan, 1 );", "POSIX console fatal status text")
    # Presentation moved to the shared cross-platform theme; see
    # tools/tests/system_console_presentation.py for the full contract.
    require(console_theme, '#define SYSCON_STATUS_READY_TEXT\t"System console ready"', "shared console ready status text")
    require(posix_console, "statusText = SYSCON_STATUS_READY_TEXT;", "POSIX console ready status")
    require(posix_console, "s_consoleWindow.forceFatalWindow = true;", "POSIX fatal console forced visibility")
    require(
        posix_console,
        "if ( !s_consoleWindow.forceFatalWindow &&\n\t\t ( cvarSystem == NULL || !cvarSystem->IsInitialized() || !sys_consoleWindow.GetBool() ) )",
        "POSIX fatal console avoids released cvar storage",
    )
    require(posix_console, "s_consoleWindow.exitRequested = true;", "POSIX fatal console quit/close exit")
    require(win_console, "SYSCON_STATUS_READY_TEXT", "Windows console ready status")
    require(win_console, "SYSCON_WIN_RGB(PANEL)", "Windows console status background color")
    require(win_console, "SYSCON_WIN_RGB(TEXT)", "Windows console status text color")
    require(win_console, "SYSCON_WIN_RGB(ALERT_PANEL)", "Windows console fatal status background color")
    require(win_console, "SetWindowText(s_wcd.hwndErrorBox, s_wcd.errorString);", "Windows console status text update")


def validate_packager_pure_pack_contract() -> None:
    package = load_package_module()
    pak_helper = read("tools/build/openq4_pak.py")
    packager = read("tools/build/package_nightly.py")
    work = ROOT / ".tmp" / "openq4-pure-pack-contract"

    require(pak_helper, "DETERMINISTIC_ZIP_TIMESTAMP", "deterministic zip metadata")
    require(pak_helper, "PAK1_NAME", "pak1 helper name")
    require(pak_helper, "OPENQ4_REQUIRED_PK4_FILES_BY_PACK", "per-pack required files")
    require(pak_helper, "OPENQ4_PK4_FORBIDDEN_FILES", "package pure-pack marker guard")
    require(pak_helper, '"binary.conf"', "package pure-pack binary marker guard")
    require(pak_helper, '"addon.conf"', "package pure-pack addon marker guard")
    require(pak_helper, "must remain a pure runtime pack", "package pure-pack marker diagnostic")
    require(pak_helper, "hashlib.md5", "pack full-file checksum")
    require(pak_helper, "copy_game_pk4", "package staged pack copy helper")
    require(packager, "from openq4_pak import", "release packager shared pak helper")
    require(packager, "OPENQ4_PACK_NAMES", "release packager pack list")
    require(packager, "for pak_name in OPENQ4_PACK_NAMES", "release packager iterates openQ4 packs")
    require(packager, "staged_game_pk4_path", "release packager staged pack preference")
    require(packager, "copy_game_pk4(staged_game_pk4_path, game_pk4_path, pak_name=pak_name)", "release packager staged pack copy")
    require(packager, "md5 {pk4_result.md5_hex}", "release packager checksum summary")

    shutil.rmtree(work, ignore_errors=True)
    try:
        for pak_name in ("pak0.pk4", "pak1.pk4"):
            for marker in ("binary.conf", "addon.conf"):
                shutil.rmtree(work, ignore_errors=True)
                install_game_dir = work / "basepr"
                destination_pk4 = work / pak_name
                write_test_file(install_game_dir / marker, b"marker\n")
                expect_runtime_error(
                    "must remain a pure runtime pack",
                    lambda: package.create_game_pk4(install_game_dir, destination_pk4, pak_name),
                    f"{pak_name} marker rejection for {marker}",
                )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_build_pak0_contract() -> None:
    meson = read("meson.build")
    basepr_meson = read("content/basepr/meson.build")
    build_pak0 = read("tools/build/build_pak0.py")
    build_openq4_pack = read("tools/build/build_openq4_pack.py")
    generate_pak_header = read("tools/build/generate_pak_header.py")
    write_pak_manifest = read("tools/build/write_pak_manifest.py")
    pak_helper = read("tools/build/openq4_pak.py")
    install_guard = read("tools/build/check_staged_content_edits.py")
    windows_runtime = read("tools/build/windows_runtime.py")
    validator = read("tools/validation/openq4_validate.py")
    work = ROOT / ".tmp" / "openq4-pak0-build-contract"

    require(meson, "custom_target(", "Meson pak0 build target")
    require(meson, "'openq4_pak0'", "Meson openQ4 pak0 build target name")
    require(meson, "'openq4_pak1'", "Meson openQ4 pak1 build target name")
    require(meson, "'openq4_paks_generated_header'", "Meson generated pack checksum target name")
    require(meson, "'openq4_pak0_source_manifest'", "Meson pak0 source manifest target")
    require(meson, "'openq4_pak1_source_manifest'", "Meson pak1 source manifest target")
    require(meson, "write_pak_manifest.py", "Meson pack source manifest refresh")
    require(meson, "build_openq4_pack.py", "Meson single-pack build script")
    require(meson, "generate_pak_header.py", "Meson pack header generation script")
    require(meson, "'openq4_paks_generated.h'", "Meson generated pack checksum header")
    require(meson, "input: openq4_pak0_source_manifest", "Meson pak0 depends on source manifest")
    require(meson, "input: openq4_pak1_source_manifest", "Meson pak1 depends on source manifest")
    require(meson, "--manifest", "Meson pack target consumes source manifest")
    require(meson, "build_by_default: true", "Meson pack default build")
    require(meson, "build_always_stale: true", "Meson source manifests rescan pack directories")
    reject(meson, "openq4_pak0_source_paths", "Meson pak0 must not bake a configured source-file list")
    reject(meson, "openq4_pak1_source_paths", "Meson pak1 must not bake a configured source-file list")
    require(meson, "install: true", "Meson pack install")
    require(meson, "install: false", "Meson does not install generated header")
    require(meson, "install_dir: install_game_dir", "Meson installs generated packs")
    require(meson, "openq4_engine_sources += openq4_paks_generated_header", "engine depends on pack checksum header")
    require_order(meson, "'openq4_pak0'", "'openq4_paks_generated_header'", "packs before checksum header")
    require_order(meson, "'openq4_paks_generated_header'", "openq4_engine_sources += openq4_paks_generated_header", "checksum header before engine sources")

    require(basepr_meson, "basepr_manifest", "loose mod.json install")
    reject(basepr_meson, "install_subdir(", "basepr content should be inside openPREY PK4s")
    reject(basepr_meson, "'openq4_defaults.cfg'", "basepr loose config install")
    reject(basepr_meson, "'default.cfg'", "basepr loose default config install")

    require(pak_helper, "OPENQ4_PAK0_MD5", "generated pak0 checksum macro")
    require(pak_helper, "OPENQ4_PAK1_MD5", "generated pak1 checksum macro")
    require(build_pak0, "format_openq4_paks_header", "standalone pack builder uses shared header formatter")
    require(build_pak0, "create_game_pk4", "build pak0 helper")
    require(build_pak0, "--pak0-stage-out", "builddir direct-run pak0 staging")
    require(build_pak0, "--pak1-stage-out", "builddir direct-run pak1 staging")
    require(build_openq4_pack, "--pak-name", "single-pack build script pack selection")
    require(build_openq4_pack, "create_game_pk4", "single-pack build script helper")
    require(build_openq4_pack, "--manifest", "single-pack build script manifest dependency")
    require(build_openq4_pack, "--stage-out", "single-pack builddir direct-run staging")
    require(generate_pak_header, "format_openq4_paks_header", "generated pack header shared formatter")
    require(generate_pak_header, "inspect_game_pk4", "generated pack header validates built packs")
    require(pak_helper, "format_pk4_source_manifest", "pack source manifest matches packer filtering")
    require(write_pak_manifest, "format_pk4_source_manifest", "Meson pack manifest source list matches packer filtering")
    require(write_pak_manifest, "write_text_if_changed", "Meson pack manifest avoids unnecessary downstream rebuilds")
    require(install_guard, "STALE_LOOSE_ROOT_FILES", "install guard stale loose root files")
    require(install_guard, "STALE_LOOSE_SUBDIRS", "install guard stale loose content dirs")
    require(install_guard, '"default.cfg"', "install guard stale loose default config")
    require(install_guard, '"env"', "install guard stale loose env content")
    require(install_guard, '"scripts"', "install guard stale loose map scripts")
    require(install_guard, "assert_inside", "install guard deletion containment")
    require(install_guard, "shutil.rmtree", "install guard stale loose directory pruning")
    require(windows_runtime, "build_game_pk4s", "Windows direct-run pack staging")
    require(windows_runtime, '"pak1.pk4"', "Windows direct-run pak1 destination")
    require(validator, '"pak0.pk4"', "staged payload requires pak0.pk4")
    require(validator, '"pak1.pk4"', "staged payload requires pak1.pk4")
    require(validator, "STAGED_FORBIDDEN_LOOSE_GAME_PATHS", "staged payload forbids stale loose content")
    require(validator, '"openq4_defaults.cfg"', "staged payload forbids loose defaults")
    require(validator, "must live inside openPREY PK4s", "staged payload stale loose diagnostic")

    shutil.rmtree(work, ignore_errors=True)
    try:
        pak0_source_dir = work / "source" / "basepr" / "pak0"
        pak1_source_dir = work / "source" / "basepr" / "pak1"
        pak0_required_files = [
            "glprogs/smaa_blend.fs",
            "glprogs/smaa_blend.vs",
            "glprogs/smaa_edge.fs",
            "glprogs/smaa_edge.vs",
            "glprogs/smaa_weights.fs",
            "glprogs/smaa_weights.vs",
            "materials/postprocess_openq4.mtr",
        ]
        pak1_required_files = [
            "guis/assets/guicursor_arrow.tga",
            "guis/assets/guicursor_menu.tga",
        ]
        for relative_path in pak0_required_files:
            write_test_file(pak0_source_dir / relative_path, f"{relative_path}\n".encode("utf-8"))
        for relative_path in pak1_required_files:
            write_test_file(pak1_source_dir / relative_path, f"{relative_path}\n".encode("utf-8"))

        pak0_out = work / "pak0.pk4"
        pak1_out = work / "pak1.pk4"
        header_out = work / "openq4_paks_generated.h"
        pak0_stage_out = work / "stage" / "basepr" / "pak0.pk4"
        pak1_stage_out = work / "stage" / "basepr" / "pak1.pk4"
        subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "build" / "build_pak0.py"),
                "--pak0-source-dir",
                str(pak0_source_dir),
                "--pak1-source-dir",
                str(pak1_source_dir),
                "--pak0-out",
                str(pak0_out),
                "--pak1-out",
                str(pak1_out),
                "--header-out",
                str(header_out),
                "--pak0-stage-out",
                str(pak0_stage_out),
                "--pak1-stage-out",
                str(pak1_stage_out),
            ],
            cwd=ROOT,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        pak0_digest = hashlib.md5(pak0_out.read_bytes()).hexdigest()
        pak1_digest = hashlib.md5(pak1_out.read_bytes()).hexdigest()
        header = header_out.read_text(encoding="utf-8")
        require(header, f'#define OPENQ4_PAK0_MD5 "{pak0_digest}"', "generated pak0 checksum header")
        require(header, f'#define OPENQ4_PAK1_MD5 "{pak1_digest}"', "generated pak1 checksum header")
        require(header, "#define OPENQ4_PAK0_FILE_COUNT 7", "generated pak0 file count")
        require(header, "#define OPENQ4_PAK1_FILE_COUNT 2", "generated pak1 file count")
        if pak0_out.read_bytes() != pak0_stage_out.read_bytes():
            raise AssertionError("Staged pak0.pk4 does not match generated pak0.pk4")
        if pak1_out.read_bytes() != pak1_stage_out.read_bytes():
            raise AssertionError("Staged pak1.pk4 does not match generated pak1.pk4")
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_validation_coverage() -> None:
    validator = read("tools/validation/openq4_validate.py")
    workflow = read(".github/workflows/openprey-validation.yml")

    require(validator, "openq4_pure_pack.py", "validation runner")

    require(workflow, "content/basepr/${pak}", "openPREY pak source roots")
    require(workflow, "build_openq4_pack.py", "openPREY pak build workflow")
    require(workflow, "write_pak_manifest.py", "openPREY pak manifest workflow")
    require(workflow, ".install/basepr/pak0.pk4", "staged pak0 verification")
    require(workflow, ".install/basepr/pak1.pk4", "staged pak1 verification")


def main() -> None:
    validate_filesystem_pure_pack_contract()
    validate_install_error_console_contract()
    validate_packager_pure_pack_contract()
    validate_build_pak0_contract()
    validate_validation_coverage()
    print("openq4_pure_pack: ok")


if __name__ == "__main__":
    main()
