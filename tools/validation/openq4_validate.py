#!/usr/bin/env python3
"""Validation profiles for openPREY local pushes and pull requests.

The filename remains ``openq4_validate.py`` as a temporary compatibility entry
point for existing automation. New validation behavior and user-facing output
use openPREY names and package layout.
"""

from __future__ import annotations

import argparse
import fnmatch
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import time
import unicodedata
from pathlib import Path

BUILD_TOOLS_DIR = Path(__file__).resolve().parents[1] / "build"
if str(BUILD_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(BUILD_TOOLS_DIR))

from linux_metadata import (  # noqa: E402
    LinuxMetadataError,
    desktop_entry_exec as parse_linux_desktop_entry_exec,
    desktop_exec_command as parse_linux_desktop_exec_command,
)


PROFILE_DEFAULTS = {
    "macos-static": {
        "build_dir": "builddir",
        "buildtype": "debug",
        "clean": False,
        "install": False,
        "skip_build": True,
    },
    "push": {
        "build_dir": "builddir",
        "buildtype": "debug",
        "clean": False,
        "install": False,
    },
    "pr": {
        "build_dir": ".tmp/validation/pr-builddir",
        "buildtype": "debug",
        "clean": True,
        "install": True,
    },
}

MACOS_STATIC_PROFILE = "macos-static"
MACOS_STATIC_SHELL_SYNTAX_FILES = (
    "tools/build/meson_setup.sh",
    "tools/macos/collect_macos_support_info.sh",
    "tools/macos/guest/openq4-macos-bootstrap.sh",
    "tools/macos/guest/openq4-macos-install-quake4-assets.sh",
    "tools/macos/guest/openq4-macos-sync-build-test.sh",
    "tools/validation/validate_push.sh",
    "tools/validation/validate_pr.sh",
    "tools/validation/validate_macos_static.sh",
)
MACOS_STATIC_DRY_RUN_PROFILES = (
    "push",
    "pr",
)

STAGED_REQUIRED_GAME_FILES = (
    "mod.json",
    "pak0.pk4",
    "pak1.pk4",
)

STAGED_FORBIDDEN_LOOSE_GAME_PATHS = (
    "default.cfg",
    "openprey_defaults.cfg",
    "openprey_profile_steamdeck.cfg",
    "openq4_defaults.cfg",
    "openq4_profile_steamdeck.cfg",
    "botfiles",
    "def",
    "env",
    "gfx",
    "glprogs",
    "guis",
    "maps",
    "materials",
    "scripts",
    "strings",
)

NON_RUNTIME_PATTERNS = (
    "*.a",
    "*.exp",
    "*.ilk",
    "*.lib",
    "*.map",
    "*.zip",
)

GAME_DIR_NAME = "basepr"
CLIENT_BINARY_STEM = "openPREY-client"
DEDICATED_BINARY_STEM = "openPREY-ded"
GAME_MODULE_STEM = "game"
LEGACY_SPLIT_GAME_MODULE_PATTERNS = (
    "game-sp_*.dll",
    "game-sp_*.so",
    "game-sp_*.dylib",
    "game-mp_*.dll",
    "game-mp_*.so",
    "game-mp_*.dylib",
    "game_sp_*.dll",
    "game_sp_*.so",
    "game_sp_*.dylib",
    "game_mp_*.dll",
    "game_mp_*.so",
    "game_mp_*.dylib",
)
FORBIDDEN_STAGED_RUNTIME_DIR_NAMES = frozenset(("base", "baseoq4", "baseq4", "basepy", "openprey"))
LEGACY_ENGINE_BINARY_PREFIXES = ("openq4-client_", "openq4-ded_")
LEGACY_RETAIL_GAME_MODULE_NAMES = frozenset(
    f"{stem}{suffix}"
    for stem in ("game", "gamex86", "gamex64", "gamearm64", "gameaarch64")
    for suffix in (".dll", ".so", ".dylib")
)

# TODO-RELEASE-LANES in docs/dev/prey-rebase/status-ledger.md is the
# authoritative gate for these inherited fixtures. They still validate
# OpenQ4 release/signing/evidence lanes (including split game modules), so
# they remain available for manual migration work but are not active
# openPREY push/PR checks until rebuilt around basepr and game_<arch>.
DEFERRED_RELEASE_LANE_TESTS = frozenset(
    (
        "linux_arm64_cross_compile.py",
        "linux_arm64_ci_coverage.py",
        "linux_arm64_release_evidence.py",
        # These tests validate the inherited linux-sanitizer/linux-wayland
        # workflow jobs themselves. Both jobs are unconditionally
        # OPENPREY-GATED because they still encode OpenQ4 lane identities and
        # split-module release assumptions. Platform-independent sanitizer
        # source invariants remain active in linux_sanitizer_source_contract.py.
        "linux_sanitizer_ci.py",
        "linux_wayland_build_contract.py",
        # This test is the static contract for the inherited manual/nightly
        # release workflow and intentionally still asserts split game modules.
        "release_tooling_safety.py",
        "macos_dedicated_server_smoke_contract.py",
        "macos_evidence_plumbing.py",
        "macos_evidence_recording.py",
        "macos_gamelibs_alignment.py",
        "macos_intel_ci.py",
        "macos_local_validation_track.py",
        "macos_matrix_policy.py",
        "macos_native_backend_containment.py",
        "macos_openal_provider_policy.py",
        "macos_package_policy.py",
        "macos_renderer_backend_policy.py",
        "macos_sanitizer_ci.py",
        "macos_signoff_archive.py",
        "macos_support_claim_policy.py",
        "macos_support_intake.py",
        "macos_symbolication_policy.py",
        "macos_universal2_runtime.py",
        "macos_universal2_release_candidate.py",
        "macos_metal_bridge.py",
    )
)

# Upstream's split-map campaign assertion is tied to Quake 4's
# mcc_2/storage/tram transition chain and companion idTarget_EndLevel shape.
# The flat-item and player-visibility tests similarly require OpenQ4's MP
# flat-diffuse/outline renderer-game ABI and companion `src/mpgame` tree;
# Prey's unified companion uses `src/game`, `src/Prey`, and `src/preyengine`
# instead and exposes no separate player-visibility contract of that shape.
# The settings-menu registry likewise describes OpenQ4's monolithic
# `guis/menu/settings` tree, while Prey keeps its modular mainmenu fragments.
# The embedded-icon fixture requires OpenQ4's `icon <code>` entity-def caching
# and `idMultiplayerGame::ReceiveDeathMessage` obituary path in `src/mpgame`;
# Prey's unified game/Prey/preyengine companion has neither contract.
# The allocator test requires OpenQ4's dual game/mpgame allocation rewrite,
# which is absent from the selected Prey companion baseline. The generated
# animation cache now has a canonical Prey implementation and remains active.
# These remaining exclusions are non-applicable rather than release-lane gates
# awaiting evidence.
INHERITED_OPENQ4_ONLY_TESTS = frozenset(
    (
        "campaign_split_state_transition.py",
        "game_class_allocator_alignment.py",
        "renderer_mp_flat_items.py",
        "renderer_player_visibility.py",
        "settings_menu_coverage.py",
        "ui_embedded_icons.py",
    )
)

# TODO-D9 keeps MVD/bot/repeater integration disabled until it is adapted
# without extending Prey's v7 game API. These tests assert the enabled OpenQ4
# UI, schemas, and companion-game implementation, so they are explicit API
# deferrals rather than active openPREY checks.
DEFERRED_PREY_API_V7_TESTS = frozenset(
    (
        "demo_playback.py",
        "mp_bot_characters.py",
        "mp_bot_navigation.py",
        "multiview_demo.py",
    )
)

MACOS_FORBIDDEN_XATTRS = (
    "com.apple.quarantine",
)

MACOS_NON_RUNTIME_PATTERNS = NON_RUNTIME_PATTERNS + (
    ".DS_Store",
    "._*",
    "__MACOSX",
    ".fseventsd",
    ".Spotlight-V100",
    ".Trashes",
    "Icon\r",
    "*.dSYM",
    "*.pdb",
)
MACOS_SUPPORT_INFO_SCRIPT_NAME = "collect_macos_support_info.sh"
MAX_MACOS_SUPPORT_INFO_SCRIPT_BYTES = 256 * 1024
MACOS_SUPPORT_INFO_REQUIRED_TOKENS = (
    "#!/bin/sh",
    "OPENPREY_PACKAGE_ROOT",
    "HOME_DIR=${HOME:-}",
    "ARCHIVE_TMP",
    "MAX_SUPPORT_TEXT_BYTES",
    "MAX_CRASH_REPORT_BYTES",
    "MAX_SUPPORT_ARCHIVE_BYTES",
    "write_bounded_report()",
    "write_openq4_log_candidate_paths()",
    "write_openq4_renderer_config_candidate_paths()",
    "sanitize_text()",
    "redact_text()",
    "limit_stream_tail()",
    "Support report output is limited to the final",
    "contains_control_chars()",
    "Support package root must not contain control characters",
    "Support output directory must not contain control characters",
    "HOME was not set; home-scoped openprey.log files were skipped.",
    "HOME was not set; saved openPREYConfig.cfg paths were skipped.",
    "logs/renderer-config.txt",
    "Game module search failed:",
    "Game module load failed:",
    "dlopen .* failed:",
    "Only renderer and performance settings are copied",
    "HOME was not set; the macOS DiagnosticReports directory could not be located.",
    ".XXXXXX.tar.gz.tmp",
    "does not dump the environment",
    "does not launch openPREY",
    "does not copy retail Prey assets",
    "truncated copy failed; source was not copied",
    "COPYFILE_DISABLE=1 tar -czf",
    "COPYFILE_DISABLE=1 tar -tzf",
    "Support archive is empty or unreadable before publish",
    "Support archive validation failed before publish",
    "Support archive is too large before publish",
    'ln "${ARCHIVE_TMP}" "${ARCHIVE_PATH}"',
)
MACOS_SUPPORT_INFO_FORBIDDEN_TOKENS = (
    "printenv",
    "env >",
    "set >",
    "openPREY-client_arm64 >",
    "openPREY-client_arm64 2>",
    "openPREY-client_x64 >",
    "openPREY-client_x64 2>",
    "openPREY-client_x86 >",
    "openPREY-client_x86 2>",
    "openPREY-ded_arm64 >",
    "openPREY-ded_arm64 2>",
    "openPREY-ded_x64 >",
    "openPREY-ded_x64 2>",
    "openPREY-ded_x86 >",
    "openPREY-ded_x86 2>",
    "xattr -l",
    "xattr -p",
    "xattr -w",
    "|| cat",
    'tail -c "${max_bytes}" < "${source_path}" 2>/dev/null || cat',
)

MACOS_LIPO_ARCHES = {
    "arm64": frozenset(("arm64",)),
    "x64": frozenset(("x86_64",)),
    "x86": frozenset(("i386",)),
    "universal2": frozenset(("arm64", "x86_64")),
}


class ValidationError(RuntimeError):
    pass


def positive_int(value: str) -> int:
    try:
        parsed = int(value, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"{value!r} is not an integer") from exc

    if parsed <= 0:
        raise argparse.ArgumentTypeError(f"{value!r} must be a positive integer")
    return parsed


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def host_is_windows() -> bool:
    return platform.system().lower() == "windows"


def host_is_linux() -> bool:
    return platform.system().lower() == "linux"


def host_is_macos() -> bool:
    return platform.system().lower() == "darwin"


def format_command(command: list[str]) -> str:
    if host_is_windows():
        return subprocess.list2cmdline(command)
    return " ".join(shlex.quote(part) for part in command)


def rel(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return str(path)


def is_relative_to(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError:
        return False
    return True


def section(title: str) -> None:
    print(f"\n==> {title}", flush=True)


def run_command(
    command: list[str],
    *,
    cwd: Path,
    env: dict[str, str],
    title: str,
    dry_run: bool,
) -> None:
    section(title)
    print(format_command(command), flush=True)
    if dry_run:
        return

    result = subprocess.run(command, cwd=str(cwd), env=env)
    if result.returncode != 0:
        raise ValidationError(f"{title} failed with exit code {result.returncode}.")


def capture_command(command: list[str], *, cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=str(cwd),
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def resolve_meson_wrapper(root: Path) -> list[str]:
    if host_is_windows():
        wrapper = root / "tools" / "build" / "meson_setup.ps1"
        if not wrapper.is_file():
            raise ValidationError(f"openPREY Meson wrapper not found: {wrapper}")
        return ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(wrapper)]

    wrapper = root / "tools" / "build" / "meson_setup.sh"
    if not wrapper.is_file():
        raise ValidationError(f"openPREY Meson wrapper not found: {wrapper}")
    return ["bash", str(wrapper)]


def validate_source_root(root: Path) -> None:
    if root.is_symlink():
        raise ValidationError(f"Source root must not be a symlink: {root}")

    if not root.is_dir():
        raise ValidationError(f"Source root does not exist or is not a directory: {root}")

    required_files = (
        root / "meson.build",
        root / "tools" / "validation" / "openq4_validate.py",
    )
    missing = [path for path in required_files if not path.is_file()]
    if missing:
        formatted = "\n".join(f"  - {path}" for path in missing)
        raise ValidationError(f"Source root is missing required openPREY files:\n{formatted}")

    if host_is_windows():
        wrapper = root / "tools" / "build" / "meson_setup.ps1"
    else:
        wrapper = root / "tools" / "build" / "meson_setup.sh"
    if not wrapper.is_file():
        raise ValidationError(f"Source root is missing the platform Meson wrapper: {wrapper}")


def validate_build_dir(root: Path, build_dir: Path) -> None:
    if build_dir.is_symlink():
        raise ValidationError(f"Build directory must not be a symlink: {build_dir}")

    if build_dir.exists() and not build_dir.is_dir():
        raise ValidationError(f"Build directory path exists but is not a directory: {build_dir}")

    if build_dir.resolve() == root.resolve():
        raise ValidationError("Build directory must not be the source root.")

    if is_relative_to(root, build_dir):
        raise ValidationError("Build directory must not contain the source root.")

    if is_relative_to(build_dir, root):
        relative_parts = build_dir.resolve().relative_to(root.resolve()).parts
        first_part = relative_parts[0] if relative_parts else ""
        if first_part != ".tmp" and not first_part.startswith("builddir"):
            raise ValidationError(
                "Build directory inside the source tree must live under .tmp/ or use a builddir* name."
            )


def is_meson_build_dir(path: Path) -> bool:
    return (path / "meson-private" / "coredata.dat").is_file() and (path / "build.ninja").is_file()


def setup_args(args: argparse.Namespace, root: Path, build_dir: Path) -> list[str]:
    base = ["setup"]
    if args.clean or not is_meson_build_dir(build_dir):
        base += ["--wipe", str(build_dir), str(root)]
    else:
        base += ["--reconfigure", str(build_dir), str(root)]

    base += [
        "--backend",
        "ninja",
        f"--buildtype={args.buildtype}",
        "--wrap-mode=forcefallback",
    ]

    if args.platform_backend:
        base.append(f"-Dplatform_backend={args.platform_backend}")

    base.extend(args.extra_setup_arg or [])
    return base


def compile_args(args: argparse.Namespace, build_dir: Path) -> list[str]:
    command = ["compile", "-C", str(build_dir)]
    if args.jobs is not None:
        command += ["-j", str(args.jobs)]
    command.extend(args.extra_compile_arg or [])
    return command


def install_args(build_dir: Path) -> list[str]:
    return ["install", "-C", str(build_dir), "--no-rebuild", "--skip-subprojects"]


def validation_env(args: argparse.Namespace, root: Path) -> dict[str, str]:
    env = os.environ.copy()
    if args.game_libs_repo:
        game_libs_repo = validate_game_libs_repo_path(Path(args.game_libs_repo))
    else:
        configured_repo = env.get("OPENPREY_GAMELIBS_REPO", "").strip()
        if not configured_repo:
            configured_repo = env.get("OPENQ4_GAMELIBS_REPO", "").strip()
        game_libs_repo = validate_game_libs_repo_path(
            Path(configured_repo) if configured_repo else root / ".." / "OpenPrey-game"
        )

    # OPENPREY_GAMELIBS_REPO is canonical. Mirror it into the migration alias so
    # older helper scripts invoked by this compatibility entry point agree.
    canonical_game_libs_repo = str(game_libs_repo)
    env["OPENPREY_GAMELIBS_REPO"] = canonical_game_libs_repo
    env["OPENQ4_GAMELIBS_REPO"] = canonical_game_libs_repo

    if args.build_gamelibs:
        env["OPENPREY_BUILD_GAMELIBS"] = "1"

    if args.skip_icon_sync:
        env["OPENPREY_SKIP_ICON_SYNC"] = "1"

    return env


def validate_game_libs_repo_path(game_libs_repo: Path) -> Path:
    if game_libs_repo.is_symlink():
        raise ValidationError(f"GameLibs repository must not be a symlink: {game_libs_repo}")
    return game_libs_repo.resolve()


def run_python_tests(args: argparse.Namespace, root: Path, env: dict[str, str]) -> None:
    inherited_tests = [
        root / "tools" / "tests" / "campaign_split_state_transition.py",
        root / "tools" / "tests" / "demo_playback.py",
        root / "tools" / "tests" / "module_only_render_geo_lifecycle.py",
        root / "tools" / "tests" / "lightgrid_bake_autoquit.py",
        root / "tools" / "tests" / "docs_link_integrity.py",
        root / "tools" / "tests" / "filesystem_case_segments.py",
        root / "tools" / "tests" / "filesystem_mod_manifest.py",
        root / "tools" / "tests" / "openprey_filesystem_policy.py",
        root / "tools" / "tests" / "game_class_allocator_alignment.py",
        root / "tools" / "tests" / "game_type_module_selection.py",
        root / "tools" / "tests" / "gamelibs_staging.py",
        root / "tools" / "tests" / "generated_animation_cache.py",
        root / "tools" / "tests" / "hdr_postprocess_math.py",
        root / "tools" / "tests" / "linux_arm64_cross_compile.py",
        root / "tools" / "tests" / "linux_arm64_ci_coverage.py",
        root / "tools" / "tests" / "linux_arm64_release_evidence.py",
        root / "tools" / "tests" / "linux_arm64_source_portability.py",
        root / "tools" / "tests" / "linux_dedicated_server_smoke_contract.py",
        root / "tools" / "tests" / "linux_dedicated_stock_map_smoke_contract.py",
        root / "tools" / "tests" / "linux_physical_host_evidence.py",
        root / "tools" / "tests" / "linux_physical_host_evidence_contract.py",
        root / "tools" / "tests" / "linux_wayland_stock_sp_smoke_contract.py",
        root / "tools" / "tests" / "linux_gui_presentation_defaults.py",
        root / "tools" / "tests" / "linux_highdpi_mouse.py",
        root / "tools" / "tests" / "linux_appimage_packaging.py",
        root / "tools" / "tests" / "linux_metadata_fuzz.py",
        root / "tools" / "tests" / "linux_packaging_guidance.py",
        root / "tools" / "tests" / "linux_pk4_legacy_tools.py",
        root / "tools" / "tests" / "linux_sanitizer_ci.py",
        root / "tools" / "tests" / "linux_sanitizer_source_contract.py",
        root / "tools" / "tests" / "linux_sdl3_glew_loader.py",
        root / "tools" / "tests" / "linux_vsync_support.py",
        root / "tools" / "tests" / "linux_wayland_build_contract.py",
        root / "tools" / "tests" / "loading_continue_input.py",
        root / "tools" / "tests" / "macos_apple_gl21_arb2_corridor.py",
        root / "tools" / "tests" / "macos_dedicated_server_smoke_contract.py",
        root / "tools" / "tests" / "macos_evidence_plumbing.py",
        root / "tools" / "tests" / "macos_evidence_recording.py",
        root / "tools" / "tests" / "macos_gamelibs_alignment.py",
        root / "tools" / "tests" / "macos_intel_ci.py",
        root / "tools" / "tests" / "macos_local_validation_track.py",
        root / "tools" / "tests" / "macos_matrix_policy.py",
        root / "tools" / "tests" / "macos_native_backend_guard.py",
        root / "tools" / "tests" / "macos_native_backend_containment.py",
        root / "tools" / "tests" / "macos_openal_provider_policy.py",
        root / "tools" / "tests" / "macos_package_policy.py",
        root / "tools" / "tests" / "macos_package_robustness.py",
        root / "tools" / "tests" / "macos_renderer_backend_policy.py",
        root / "tools" / "tests" / "macos_renderer_phase_breadcrumbs.py",
        root / "tools" / "tests" / "macos_renderer_startup_guard.py",
        root / "tools" / "tests" / "macos_sanitizer_ci.py",
        root / "tools" / "tests" / "macos_sdl3_backend_guard.py",
        root / "tools" / "tests" / "macos_shadow_policy.py",
        root / "tools" / "tests" / "macos_signoff_archive.py",
        root / "tools" / "tests" / "macos_static_policy.py",
        root / "tools" / "tests" / "macos_support_claim_policy.py",
        root / "tools" / "tests" / "macos_support_intake.py",
        root / "tools" / "tests" / "macos_symbolication_policy.py",
        root / "tools" / "tests" / "macos_universal2_assembly.py",
        root / "tools" / "tests" / "macos_universal2_runtime.py",
        root / "tools" / "tests" / "macos_universal2_release_candidate.py",
        root / "tools" / "tests" / "macos_metal_bridge.py",
        root / "tools" / "tests" / "macos_moltenvk_policy.py",
        root / "tools" / "tests" / "mp_bot_characters.py",
        root / "tools" / "tests" / "mp_bot_navigation.py",
        root / "tools" / "tests" / "mp_client_combat_effects.py",
        root / "tools" / "tests" / "multiview_demo.py",
        root / "tools" / "tests" / "native_glx_shutdown.py",
        root / "tools" / "tests" / "openq4_pure_pack.py",
        root / "tools" / "tests" / "openprey_runtime_branding.py",
        root / "tools" / "tests" / "openprey_tool_branding.py",
        root / "tools" / "tests" / "packaging_safety.py",
        root / "tools" / "tests" / "preprocessor_macro_safety.py",
        root / "tools" / "tests" / "posix_memory_management.py",
        root / "tools" / "tests" / "posix_monotonic_time.py",
        root / "tools" / "tests" / "posix_network_resolution.py",
        root / "tools" / "tests" / "posix_thread_shutdown.py",
        root / "tools" / "tests" / "release_tooling_safety.py",
        root / "tools" / "tests" / "renderer_cel_shading.py",
        root / "tools" / "tests" / "renderer_mp_flat_items.py",
        root / "tools" / "tests" / "renderer_msaa_cvar_safety.py",
        root / "tools" / "tests" / "renderer_picmip_policy.py",
        root / "tools" / "tests" / "renderer_player_visibility.py",
        root / "tools" / "tests" / "renderer_screenshot_readback.py",
        root / "tools" / "tests" / "renderer_supersampling_safety.py",
        root / "tools" / "tests" / "renderer_vulkan_decal_compatibility.py",
        root / "tools" / "tests" / "renderer_vulkan_gui_residency.py",
        root / "tools" / "tests" / "renderer_vulkan_md5r_compatibility.py",
        root / "tools" / "tests" / "renderer_vulkan_shadow_compatibility.py",
        root / "tools" / "tests" / "renderer_vulkan_world_interaction_compatibility.py",
        root / "tools" / "tests" / "savegame_corruption_contract.py",
        root / "tools" / "tests" / "savegame_pointer_width_safety.py",
        root / "tools" / "tests" / "sdl3_input_parity.py",
        root / "tools" / "tests" / "sdl3_multidisplay_windowing.py",
        root / "tools" / "tests" / "settings_menu_coverage.py",
        root / "tools" / "tests" / "source_charset_integrity.py",
        root / "tools" / "tests" / "steam_deck_support.py",
        root / "tools" / "tests" / "system_console_presentation.py",
        root / "tools" / "tests" / "lang_table_encoding.py",
        root / "tools" / "tests" / "startup_language_override.py",
        root / "tools" / "tests" / "ui_embedded_icons.py",
        root / "tools" / "tests" / "validation_hardening.py",
        root / "tools" / "tests" / "vk_shader_header_pin.py",
        root / "tools" / "tests" / "vscode_fast_build.py",
    ]
    excluded_tests = (
        DEFERRED_RELEASE_LANE_TESTS
        | INHERITED_OPENQ4_ONLY_TESTS
        | DEFERRED_PREY_API_V7_TESTS
    )
    tests = [test for test in inherited_tests if test.name not in excluded_tests]
    for test_script in tests:
        if not test_script.is_file():
            raise ValidationError(f"Python validation test not found: {test_script}")
        run_command(
            [sys.executable, str(test_script)],
            cwd=root,
            env=env,
            title=f"Python check: {rel(test_script, root)}",
            dry_run=args.dry_run,
        )


def ensure_game_libs_repo(env: dict[str, str]) -> None:
    game_libs_repo = validate_game_libs_repo_path(Path(env["OPENPREY_GAMELIBS_REPO"]))
    for source_tree, label in (
        ("game", "shared game"),
        ("Prey", "Prey gameplay"),
        ("preyengine", "Prey engine-support"),
    ):
        expected = game_libs_repo / "src" / source_tree
        if not expected.is_dir():
            raise ValidationError(
                f"OpenPrey-game {label} source directory was not found. "
                f"Expected: {expected}"
            )


def find_any(root: Path, patterns: tuple[str, ...]) -> list[Path]:
    matches: list[Path] = []
    for pattern in patterns:
        matches.extend(sorted(root.glob(pattern)))
    return matches


def find_engine_executables(install_root: Path, stem: str) -> list[Path]:
    suffix = ".exe" if host_is_windows() else ""
    return [
        path
        for path in sorted(install_root.glob(f"{stem}_*{suffix}"))
        if path.is_file()
    ]


def staged_binary_arch(path: Path, stem: str) -> str | None:
    match = re.fullmatch(rf"{re.escape(stem)}_([A-Za-z0-9_]+)", path.stem)
    if match is None:
        return None
    return match.group(1)


def staged_arches(paths: list[Path], stem: str, root: Path, label: str) -> set[str]:
    arches: set[str] = set()
    for path in paths:
        arch = staged_binary_arch(path, stem)
        if arch is None:
            raise ValidationError(f"Unexpected {label} binary name: {rel(path, root)}")
        arches.add(arch)
    return arches


def expected_game_module_suffix() -> str:
    if host_is_windows():
        return ".dll"
    if host_is_macos():
        return ".dylib"
    return ".so"


def find_staged_game_modules(game_dir: Path) -> list[Path]:
    candidates = find_any(
        game_dir,
        (f"{GAME_MODULE_STEM}_*.dll", f"{GAME_MODULE_STEM}_*.so", f"{GAME_MODULE_STEM}_*.dylib"),
    )
    return [
        path
        for path in candidates
        if not path.name.startswith(("game_sp_", "game_mp_"))
    ]


def find_legacy_split_game_modules(game_dir: Path) -> list[Path]:
    return find_any(game_dir, LEGACY_SPLIT_GAME_MODULE_PATTERNS)


def find_forbidden_staged_legacy_paths(install_root: Path, game_dir: Path) -> list[Path]:
    matches: list[Path] = []
    for path in install_root.iterdir():
        lower_name = path.name.lower()
        if lower_name in FORBIDDEN_STAGED_RUNTIME_DIR_NAMES or lower_name.startswith(LEGACY_ENGINE_BINARY_PREFIXES):
            matches.append(path)
    for path in game_dir.iterdir():
        if path.name.lower() in LEGACY_RETAIL_GAME_MODULE_NAMES:
            matches.append(path)
    return sorted(set(matches))


def validate_staged_architecture_set(
    root: Path,
    game_dir: Path,
    client_candidates: list[Path],
    dedicated_candidates: list[Path],
) -> set[str]:
    client_arches = staged_arches(client_candidates, CLIENT_BINARY_STEM, root, "client")
    dedicated_arches = staged_arches(dedicated_candidates, DEDICATED_BINARY_STEM, root, "dedicated-server")
    if client_arches != dedicated_arches or len(client_arches) != 1:
        raise ValidationError(
            "Staged engine binary architecture mismatch: "
            f"clients={sorted(client_arches)} dedicated={sorted(dedicated_arches)}"
        )

    expected_suffix = expected_game_module_suffix()
    game_modules = find_staged_game_modules(game_dir)
    wrong_suffix_modules = sorted(path for path in game_modules if path.suffix.lower() != expected_suffix)
    if wrong_suffix_modules:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in wrong_suffix_modules)
        raise ValidationError(
            f"Staged payload contains game modules for the wrong platform suffix; "
            f"expected {expected_suffix}:\n{formatted}"
        )

    platform_modules = [path for path in game_modules if path.suffix.lower() == expected_suffix]
    game_arches = staged_arches(platform_modules, GAME_MODULE_STEM, root, "unified game module")
    if len(platform_modules) != 1 or game_arches != client_arches:
        raise ValidationError(
            "Staged game module architecture mismatch: "
            f"engine={sorted(client_arches)} game={sorted(game_arches)} "
            f"module_count={len(platform_modules)}"
        )

    return client_arches


def is_posix_executable(path: Path) -> bool:
    return path.is_file() and os.access(path, os.X_OK)


def require_posix_executable(path: Path, root: Path, label: str) -> None:
    if not is_posix_executable(path):
        raise ValidationError(f"{label} is not executable: {rel(path, root)}")


def validate_macos_support_collector_script(root: Path, install_root: Path) -> None:
    script_path = install_root / MACOS_SUPPORT_INFO_SCRIPT_NAME
    if not script_path.is_file():
        raise ValidationError(f"macOS staged payload is missing support collector: {rel(script_path, root)}")
    require_posix_executable(script_path, root, "macOS support collector")

    try:
        script_size = script_path.stat().st_size
    except OSError as exc:
        raise ValidationError(f"macOS support collector is unreadable: {rel(script_path, root)}") from exc
    if script_size > MAX_MACOS_SUPPORT_INFO_SCRIPT_BYTES:
        raise ValidationError(
            f"macOS support collector is too large: {rel(script_path, root)} "
            f"({script_size} bytes)"
        )

    try:
        script_data = script_path.read_bytes()
    except OSError as exc:
        raise ValidationError(f"macOS support collector is unreadable: {rel(script_path, root)}") from exc
    try:
        script_text = script_data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ValidationError(f"macOS support collector is not UTF-8: {rel(script_path, root)}") from exc

    if "\x00" in script_text:
        raise ValidationError(f"macOS support collector contains NUL bytes: {rel(script_path, root)}")
    if "\r" in script_text:
        raise ValidationError(f"macOS support collector contains CRLF or carriage returns: {rel(script_path, root)}")

    for token in MACOS_SUPPORT_INFO_REQUIRED_TOKENS:
        if token not in script_text:
            raise ValidationError(
                f"macOS support collector is missing required marker {token!r}: {rel(script_path, root)}"
            )
    for token in MACOS_SUPPORT_INFO_FORBIDDEN_TOKENS:
        if token in script_text:
            raise ValidationError(
                f"macOS support collector contains forbidden privacy/no-launch pattern {token!r}: "
                f"{rel(script_path, root)}"
            )


def macos_forbidden_xattrs(path: Path) -> list[str]:
    try:
        names = os.listxattr(path)
    except (AttributeError, OSError):
        return []
    return sorted(name for name in names if name in MACOS_FORBIDDEN_XATTRS)


def validate_no_macos_forbidden_xattrs(root: Path, install_root: Path) -> None:
    offenders: list[tuple[Path, list[str]]] = []
    for path in [install_root, *install_root.rglob("*")]:
        bad_attrs = macos_forbidden_xattrs(path)
        if bad_attrs:
            offenders.append((path, bad_attrs))

    if offenders:
        formatted = "\n".join(
            f"  - {rel(path, root)}: {', '.join(attrs)}" for path, attrs in offenders[:20]
        )
        raise ValidationError(f"macOS staged payload contains forbidden extended attributes:\n{formatted}")


def validate_no_macos_non_runtime_artifacts(root: Path, install_root: Path, game_dir: Path) -> None:
    validate_no_non_runtime_artifacts(
        root,
        (install_root, game_dir),
        MACOS_NON_RUNTIME_PATTERNS,
        "macOS staged payload contains non-runtime artifacts",
    )


def macos_casefold_path_key(path: str) -> str:
    return unicodedata.normalize("NFC", path).casefold()


def validate_no_macos_casefold_path_collisions(root: Path, install_root: Path) -> None:
    seen_paths: dict[str, str] = {}
    for path in sorted(install_root.rglob("*")):
        relative = path.relative_to(install_root).as_posix()
        key = macos_casefold_path_key(relative)
        previous = seen_paths.get(key)
        if previous is not None and previous != relative:
            raise ValidationError(
                "macOS staged payload contains case-insensitive duplicate paths:\n"
                f"  - {rel(install_root / previous, root)}\n"
                f"  - {rel(path, root)}"
            )
        seen_paths[key] = relative


def validate_no_staged_symlinks(root: Path, install_root: Path) -> None:
    symlinks = [path for path in install_root.rglob("*") if path.is_symlink()]
    if symlinks:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in sorted(symlinks)[:20])
        raise ValidationError(f"Staged payload contains symlink entries:\n{formatted}")


def validate_no_macos_symlinks(root: Path, install_root: Path) -> None:
    validate_no_staged_symlinks(root, install_root)


def validate_no_non_runtime_artifacts(
    root: Path,
    directories: tuple[Path, ...],
    patterns: tuple[str, ...],
    message: str,
) -> None:
    bad_artifacts: list[Path] = []
    for directory in directories:
        for path in directory.rglob("*"):
            if any(fnmatch.fnmatch(path.name.lower(), pattern.lower()) for pattern in patterns):
                bad_artifacts.append(path)

    if bad_artifacts:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in sorted(bad_artifacts)[:20])
        raise ValidationError(f"{message}:\n{formatted}")


def validate_no_macos_unsafe_file_modes(root: Path, install_root: Path) -> None:
    if not host_is_macos():
        return

    offenders: list[str] = []
    for path in install_root.rglob("*"):
        if not path.is_file():
            continue
        mode = path.stat().st_mode & 0o7777
        if mode & 0o7000:
            offenders.append(f"{rel(path, root)} has special mode bits {mode:o}")
        elif mode & 0o022:
            offenders.append(f"{rel(path, root)} is group/other writable ({mode:o})")

    if offenders:
        formatted = "\n".join(f"  - {offender}" for offender in offenders[:20])
        raise ValidationError(f"macOS staged payload contains unsafe file modes:\n{formatted}")


def macos_expected_lipo_arches(arch: str) -> frozenset[str]:
    expected = MACOS_LIPO_ARCHES.get(arch)
    if expected is None:
        raise ValidationError(f"macOS staged architecture {arch!r} has no lipo mapping")
    return expected


def macos_expected_lipo_arch(arch: str) -> str:
    """Return the singleton Mach-O slice for compatibility with thin-only callers."""
    expected = macos_expected_lipo_arches(arch)
    if len(expected) != 1:
        raise ValidationError(f"macOS staged architecture {arch!r} is not a thin architecture")
    return next(iter(expected))


def macos_lipo_arches(binary_path: Path) -> set[str]:
    if not host_is_macos():
        return set()

    lipo_path = shutil.which("lipo")
    if lipo_path is None:
        raise ValidationError("macOS architecture validation requires lipo.")

    completed = subprocess.run(
        [lipo_path, "-archs", str(binary_path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        raise ValidationError(f"lipo failed for macOS binary {binary_path}: {message}")

    return set(completed.stdout.strip().split())


def validate_macos_binary_architectures(root: Path, arch: str, binary_paths: list[Path]) -> None:
    if not host_is_macos():
        return

    expected_arches = macos_expected_lipo_arches(arch)
    for binary_path in binary_paths:
        actual_arches = macos_lipo_arches(binary_path)
        if actual_arches != expected_arches:
            actual = ", ".join(sorted(actual_arches)) or "<none>"
            raise ValidationError(
                f"macOS staged binary architecture mismatch: {rel(binary_path, root)} "
                f"expected {', '.join(sorted(expected_arches))}, found {actual}"
            )


def validate_linux_steamdeck_launcher(path: Path, root: Path, client_candidates: list[Path]) -> None:
    try:
        launcher = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ValidationError(f"Linux Steam Deck launcher is unreadable: {rel(path, root)}") from exc

    required_tokens = (
        "OPENPREY_STEAMDECK",
        "OPENPREY_FORCE_X11",
        "SDL_VIDEO_DRIVER=x11",
        "SDL_VIDEODRIVER=x11",
        "+set com_platformProfile steamdeck",
    )
    for token in required_tokens:
        if token not in launcher:
            raise ValidationError(f"Linux Steam Deck launcher is missing {token!r}: {rel(path, root)}")

    if "WAYLAND_DISPLAY" in launcher:
        raise ValidationError(f"Linux Steam Deck launcher still forces X11 from Wayland session state: {rel(path, root)}")

    client_names = {candidate.name for candidate in client_candidates}
    if not any(client_name in launcher for client_name in client_names):
        expected = ", ".join(sorted(client_names))
        raise ValidationError(
            f"Linux Steam Deck launcher does not exec a staged client binary. "
            f"Expected one of: {expected}"
        )


def desktop_entry_exec(path: Path, root: Path) -> str:
    try:
        return parse_linux_desktop_entry_exec(path)
    except LinuxMetadataError as exc:
        raise ValidationError(str(exc)) from exc


def desktop_exec_command(exec_line: str) -> str:
    try:
        return parse_linux_desktop_exec_command(exec_line)
    except LinuxMetadataError as exc:
        raise ValueError(str(exc)) from exc


def validate_linux_launch_metadata(root: Path, install_root: Path, client_candidates: list[Path]) -> None:
    steamdeck_launcher = install_root / "openPREY-steamdeck"
    if not steamdeck_launcher.is_file():
        raise ValidationError("Linux staged payload is missing openPREY-steamdeck.")
    require_posix_executable(steamdeck_launcher, root, "Linux Steam Deck launcher")
    validate_linux_steamdeck_launcher(steamdeck_launcher, root, client_candidates)

    desktop_dir = install_root / "share" / "applications"
    desktop_entries = {
        "openprey.desktop": {path.name for path in client_candidates},
        "openprey-steamdeck.desktop": {steamdeck_launcher.name},
    }
    for filename, allowed_commands in desktop_entries.items():
        desktop_path = desktop_dir / filename
        if not desktop_path.is_file():
            raise ValidationError(f"Linux staged payload is missing desktop entry: {rel(desktop_path, root)}")

        exec_line = desktop_entry_exec(desktop_path, root)
        try:
            exec_command = desktop_exec_command(exec_line)
        except ValueError as exc:
            raise ValidationError(
                f"Linux desktop entry {rel(desktop_path, root)} has an invalid Exec line: {exc}"
            ) from exc
        if exec_command not in allowed_commands:
            allowed = ", ".join(sorted(allowed_commands))
            raise ValidationError(
                f"Linux desktop entry {rel(desktop_path, root)} points at {exec_command or '<empty>'!r}; "
                f"expected one of: {allowed}"
            )


def readelf_output(path: Path, args: list[str], root: Path) -> str:
    readelf_path = shutil.which("readelf")
    if readelf_path is None:
        raise ValidationError("Linux hardening validation requires readelf from binutils.")

    readelf_env = os.environ.copy()
    readelf_env["LC_ALL"] = "C"
    readelf_env["LANG"] = "C"
    completed = subprocess.run(
        [readelf_path, *args, str(path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=readelf_env,
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or completed.stdout.strip()
        raise ValidationError(f"readelf failed for {rel(path, root)}: {message}")
    return completed.stdout


LINUX_DEDICATED_COMMON_ALLOWED_NEEDED = frozenset(
    {
        # C/C++ and compiler runtimes used by supported GCC/Clang builds.
        "libc.so",
        "libc.so.6",
        "libc++.so.1",
        "libc++abi.so.1",
        "libgcc_s.so.1",
        "libstdc++.so.6",
        "libunwind.so.1",
        "libatomic.so.1",
        "libssp.so.0",
        # POSIX runtime libraries that remain separate on older glibc systems.
        "libdl.so",
        "libdl.so.2",
        "libm.so",
        "libm.so.6",
        "libpthread.so",
        "libpthread.so.0",
        "librt.so",
        "librt.so.1",
    }
)

# Dynamic loaders normally appear in PT_INTERP rather than DT_NEEDED, but some
# toolchain/runtime combinations record them as dependencies. Keep those names
# architecture-specific so an x64 payload cannot silently accept an AArch64
# loader (or vice versa). The musl libc/loader SONAMEs support local non-release
# builds without weakening the client-library boundary.
LINUX_DEDICATED_ARCH_ALLOWED_NEEDED = {
    "x64": frozenset(
        {
            "ld-linux-x86-64.so.2",
            "ld-musl-x86_64.so.1",
            "libc.musl-x86_64.so.1",
        }
    ),
    "arm64": frozenset(
        {
            "ld-linux-aarch64.so.1",
            "ld-musl-aarch64.so.1",
            "libc.musl-aarch64.so.1",
        }
    ),
    "x86": frozenset(
        {
            "ld-linux.so.2",
            "ld-musl-i386.so.1",
            "libc.musl-x86.so.1",
        }
    ),
}


def linux_needed_libraries(root: Path, binary_path: Path) -> list[str]:
    dynamic_section = readelf_output(binary_path, ["-W", "-d"], root)
    return re.findall(
        r"\(NEEDED\).*Shared library: \[([^\]]+)\]",
        dynamic_section,
    )


def validate_linux_client_runtime_dependencies(
    root: Path,
    client_candidates: list[Path],
) -> None:
    for client_path in client_candidates:
        forbidden = sorted(
            library
            for library in linux_needed_libraries(root, client_path)
            if library.lower().startswith("libpipewire-")
        )
        if forbidden:
            raise ValidationError(
                "Linux client has a forbidden direct PipeWire runtime dependency: "
                f"{rel(client_path, root)}: {', '.join(forbidden)}"
            )


def validate_linux_dedicated_runtime_dependencies(
    root: Path,
    runtime_binary_specs: list[tuple[Path, str]],
) -> None:
    for binary_path, arch in runtime_binary_specs:
        architecture_allowed = LINUX_DEDICATED_ARCH_ALLOWED_NEEDED.get(arch)
        if architecture_allowed is None:
            raise ValidationError(
                "Linux dedicated runtime dependency validation does not support architecture "
                f"{arch or '<empty>'!r}: {rel(binary_path, root)}"
            )
        allowed = LINUX_DEDICATED_COMMON_ALLOWED_NEEDED | architecture_allowed
        unexpected = sorted(
            set(linux_needed_libraries(root, binary_path)).difference(allowed)
        )
        if unexpected:
            raise ValidationError(
                "Linux dedicated runtime component has non-core DT_NEEDED dependencies: "
                f"{rel(binary_path, root)} ({arch}): {', '.join(unexpected)}"
            )


def validate_linux_binary_hardening(
    root: Path,
    binary_specs: list[tuple[Path, str, bool]],
) -> None:
    architecture_contract = {
        "x64": ("ELF64", "Advanced Micro Devices X86-64"),
        "arm64": ("ELF64", "AArch64"),
        "x86": ("ELF32", "Intel 80386"),
    }

    for binary_path, staged_arch, is_game_module in binary_specs:
        if staged_arch not in architecture_contract:
            raise ValidationError(
                f"Linux ELF validation has no architecture mapping for {staged_arch!r}: "
                f"{rel(binary_path, root)}"
            )

        expected_class, expected_machine = architecture_contract[staged_arch]
        elf_header = readelf_output(binary_path, ["-h"], root)
        class_line = next((line for line in elf_header.splitlines() if "Class:" in line), "")
        machine_line = next((line for line in elf_header.splitlines() if "Machine:" in line), "")
        if expected_class not in class_line or expected_machine not in machine_line:
            raise ValidationError(
                f"Linux ELF architecture does not match its staged name: {rel(binary_path, root)} "
                f"(expected {expected_class}/{expected_machine})"
            )
        if "Type:" not in elf_header or "DYN" not in elf_header:
            raise ValidationError(f"Linux binary is not PIE/ET_DYN: {rel(binary_path, root)}")

        program_headers = readelf_output(binary_path, ["-W", "-l"], root)
        if "GNU_RELRO" not in program_headers:
            raise ValidationError(f"Linux binary is missing GNU_RELRO: {rel(binary_path, root)}")

        stack_line = next((line for line in program_headers.splitlines() if "GNU_STACK" in line), "")
        if not stack_line:
            raise ValidationError(f"Linux binary is missing GNU_STACK metadata: {rel(binary_path, root)}")
        stack_fields = stack_line.split()
        stack_flags = stack_fields[-2] if len(stack_fields) >= 2 else ""
        if "E" in stack_flags:
            raise ValidationError(f"Linux binary has an executable stack: {rel(binary_path, root)}")

        dynamic_section = readelf_output(binary_path, ["-W", "-d"], root)
        if "BIND_NOW" not in dynamic_section and "(NOW)" not in dynamic_section:
            raise ValidationError(f"Linux binary is missing immediate binding/BIND_NOW: {rel(binary_path, root)}")

        if is_game_module:
            symbols = readelf_output(binary_path, ["--wide", "--dyn-syms"], root)
            public_symbols: list[tuple[str, str, str, str, str]] = []
            for line in symbols.splitlines():
                fields = line.split()
                # readelf columns: Num, Value, Size, Type, Bind, Vis, Ndx, Name.
                # Reject every externally visible definition except the module
                # entry point. This includes weak, GNU-unique, and protected
                # definitions that a GLOBAL/DEFAULT-only check would overlook.
                if (
                    len(fields) >= 8
                    and fields[6] != "UND"
                    and fields[4] in {"GLOBAL", "WEAK", "UNIQUE"}
                    and fields[5] in {"DEFAULT", "PROTECTED"}
                ):
                    public_symbols.append(
                        (fields[3], fields[4], fields[5], fields[6], fields[-1])
                    )

            expected_game_api = (
                len(public_symbols) == 1
                and public_symbols[0][0] == "FUNC"
                and public_symbols[0][1] == "GLOBAL"
                and public_symbols[0][2] == "DEFAULT"
                and public_symbols[0][4] == "GetGameAPI"
            )
            if not expected_game_api:
                public_summary = ", ".join(
                    f"{symbol_type}/{binding}/{visibility}/{index} {name}"
                    for symbol_type, binding, visibility, index, name in public_symbols
                ) or "<none>"
                raise ValidationError(
                    "Linux game module must expose exactly one GLOBAL/DEFAULT/FUNC "
                    f"GetGameAPI definition: {rel(binary_path, root)}; found {public_summary}"
                )


def macos_binary_arch(path: Path, stem: str) -> str | None:
    match = re.fullmatch(rf"{re.escape(stem)}_([A-Za-z0-9_]+)", path.name)
    if match is None:
        return None
    return match.group(1)


def is_macos_root_engine_binary(path: Path) -> bool:
    return path.is_file() and (
        path.name.startswith(f"{CLIENT_BINARY_STEM}_")
        or path.name.startswith(f"{DEDICATED_BINARY_STEM}_")
    )


def validate_macos_staged_root_engine_binaries(
    root: Path,
    install_root: Path,
    client_arches: set[str],
    dedicated_arches: set[str],
) -> str:
    all_arches = client_arches | dedicated_arches
    if client_arches != dedicated_arches or len(all_arches) != 1:
        raise ValidationError(
            "macOS staged payload contains stale or mismatched root engine binaries: "
            f"clients={sorted(client_arches)} dedicated={sorted(dedicated_arches)}"
        )

    arch = next(iter(all_arches))
    expected_names = {
        f"{CLIENT_BINARY_STEM}_{arch}",
        f"{DEDICATED_BINARY_STEM}_{arch}",
    }
    stale_binaries = sorted(
        path
        for path in install_root.iterdir()
        if is_macos_root_engine_binary(path) and path.name not in expected_names
    )
    if stale_binaries:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in stale_binaries)
        raise ValidationError(f"macOS staged payload contains stale or mismatched root engine binaries:\n{formatted}")

    return arch


def validate_macos_staged_metadata(
    root: Path,
    install_root: Path,
    game_dir: Path,
    client_candidates: list[Path],
    dedicated_candidates: list[Path],
) -> None:
    icon_path = install_root / "prey.icns"
    splash_path = install_root / "assets" / "splash" / "prey_rt_bitmap_4001.bmp"

    if not icon_path.is_file():
        raise ValidationError(f"macOS staged payload is missing app icon: {rel(icon_path, root)}")
    if not splash_path.is_file():
        raise ValidationError(f"macOS staged payload is missing startup splash asset: {rel(splash_path, root)}")
    validate_no_macos_symlinks(root, install_root)
    validate_macos_support_collector_script(root, install_root)

    client_arches: set[str] = set()
    for client in client_candidates:
        require_posix_executable(client, root, "macOS staged client executable")
        arch = macos_binary_arch(client, CLIENT_BINARY_STEM)
        if arch is None:
            raise ValidationError(f"Unexpected macOS client binary name: {rel(client, root)}")
        client_arches.add(arch)

    dedicated_arches: set[str] = set()
    for dedicated in dedicated_candidates:
        require_posix_executable(dedicated, root, "macOS staged dedicated-server executable")
        arch = macos_binary_arch(dedicated, DEDICATED_BINARY_STEM)
        if arch is None:
            raise ValidationError(f"Unexpected macOS dedicated-server binary name: {rel(dedicated, root)}")
        dedicated_arches.add(arch)

    staged_arch = validate_macos_staged_root_engine_binaries(root, install_root, client_arches, dedicated_arches)

    bad_game_modules = sorted(
        find_any(game_dir, ("game_*.dll", "game_*.so"))
        + find_legacy_split_game_modules(game_dir)
    )
    if bad_game_modules:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in bad_game_modules)
        raise ValidationError(f"macOS staged payload contains non-unified or non-dylib game modules:\n{formatted}")

    expected_game_module = game_dir / f"{GAME_MODULE_STEM}_{staged_arch}.dylib"
    if not expected_game_module.is_file():
        raise ValidationError(
            "macOS staged payload is missing its architecture-matched unified game module: "
            f"{rel(expected_game_module, root)}"
        )
    if not is_posix_executable(expected_game_module):
        raise ValidationError(f"macOS staged game module is not executable: {rel(expected_game_module, root)}")

    stale_game_modules = sorted(
        path
        for path in game_dir.glob("game_*.dylib")
        if path != expected_game_module
    )
    if stale_game_modules:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in stale_game_modules)
        raise ValidationError(f"macOS staged payload contains stale or mismatched game modules:\n{formatted}")

    validate_macos_binary_architectures(
        root,
        staged_arch,
        [
            install_root / f"{CLIENT_BINARY_STEM}_{staged_arch}",
            install_root / f"{DEDICATED_BINARY_STEM}_{staged_arch}",
            expected_game_module,
        ],
    )

    validate_no_macos_non_runtime_artifacts(root, install_root, game_dir)
    validate_no_macos_casefold_path_collisions(root, install_root)
    validate_no_macos_forbidden_xattrs(root, install_root)
    validate_no_macos_unsafe_file_modes(root, install_root)


def validate_windows_symbols(root: Path, install_root: Path, game_dir: Path, arches: set[str]) -> None:
    if not host_is_windows():
        return

    if not (install_root / "OpenAL32.dll").is_file():
        raise ValidationError("Windows staged payload is missing OpenAL32.dll.")

    required_symbols = {
        install_root / f"{CLIENT_BINARY_STEM}_{arch}.pdb"
        for arch in arches
    } | {
        install_root / f"{DEDICATED_BINARY_STEM}_{arch}.pdb"
        for arch in arches
    } | {
        game_dir / f"{GAME_MODULE_STEM}_{arch}.pdb"
        for arch in arches
    }

    missing_symbols = sorted(path for path in required_symbols if not path.is_file())
    if missing_symbols:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in missing_symbols)
        raise ValidationError(f"Windows staged payload is missing architecture-matched diagnostic PDB files:\n{formatted}")

    staged_symbols = (
        sorted(install_root.glob(f"{CLIENT_BINARY_STEM}_*.pdb"))
        + sorted(install_root.glob(f"{DEDICATED_BINARY_STEM}_*.pdb"))
        + sorted(game_dir.glob(f"{GAME_MODULE_STEM}_*.pdb"))
        + sorted(game_dir.glob("game-sp_*.pdb"))
        + sorted(game_dir.glob("game-mp_*.pdb"))
        + sorted(game_dir.glob("game_sp_*.pdb"))
        + sorted(game_dir.glob("game_mp_*.pdb"))
    )
    stale_symbols = [path for path in staged_symbols if path not in required_symbols]
    if stale_symbols:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in stale_symbols)
        raise ValidationError(f"Windows staged payload contains stale or architecture-mismatched PDB files:\n{formatted}")


def validate_staged_payload(root: Path, *, dry_run: bool) -> None:
    section("Validate staged .install payload")
    if dry_run:
        print("Staged payload checks skipped during dry run.", flush=True)
        return

    install_root = root / ".install"
    game_dir = install_root / GAME_DIR_NAME
    if install_root.is_symlink():
        raise ValidationError(f"Install root must not be a symlink: {install_root}")
    if not install_root.is_dir():
        raise ValidationError(f"Install root is missing: {install_root}")
    if game_dir.is_symlink():
        raise ValidationError(f"Staged game directory must not be a symlink: {game_dir}")
    if not game_dir.is_dir():
        raise ValidationError(f"Staged game directory is missing: {game_dir}")

    legacy_runtime_paths = find_forbidden_staged_legacy_paths(install_root, game_dir)
    if legacy_runtime_paths:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in legacy_runtime_paths)
        raise ValidationError(
            "Staged payload contains an obsolete OpenQ4/retail runtime layout; "
            "openPREY packages only openPREY-client/openPREY-ded with basepr/game_<arch>:\n"
            f"{formatted}"
        )

    client_candidates = find_engine_executables(install_root, CLIENT_BINARY_STEM)
    dedicated_candidates = find_engine_executables(install_root, DEDICATED_BINARY_STEM)
    if not client_candidates:
        raise ValidationError("Staged client executable was not found under .install/.")
    if not dedicated_candidates:
        raise ValidationError("Staged dedicated-server executable was not found under .install/.")
    if not host_is_windows():
        for client in client_candidates:
            require_posix_executable(client, root, "Staged client executable")
        for dedicated in dedicated_candidates:
            require_posix_executable(dedicated, root, "Staged dedicated-server executable")

    split_modules = find_legacy_split_game_modules(game_dir)
    if split_modules:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in split_modules)
        raise ValidationError(
            "Staged payload contains obsolete split game modules; openPREY packages one unified game_<arch> module:\n"
            f"{formatted}"
        )

    game_modules = find_staged_game_modules(game_dir)
    if not game_modules:
        raise ValidationError("Staged unified game module was not found under .install/basepr/.")

    staged_arches_for_payload = validate_staged_architecture_set(
        root,
        game_dir,
        client_candidates,
        dedicated_candidates,
    )
    validate_no_staged_symlinks(root, install_root)

    if host_is_linux():
        validate_linux_launch_metadata(root, install_root, client_candidates)
        linux_binary_specs: list[tuple[Path, str, bool]] = []
        for binary_path in client_candidates:
            linux_binary_specs.append(
                (binary_path, staged_binary_arch(binary_path, CLIENT_BINARY_STEM) or "", False)
            )
        for binary_path in dedicated_candidates:
            linux_binary_specs.append(
                (binary_path, staged_binary_arch(binary_path, DEDICATED_BINARY_STEM) or "", False)
            )
        for binary_path in game_modules:
            linux_binary_specs.append(
                (binary_path, staged_binary_arch(binary_path, GAME_MODULE_STEM) or "", True)
            )
        validate_linux_binary_hardening(root, linux_binary_specs)
        validate_linux_client_runtime_dependencies(root, client_candidates)
        dedicated_runtime_specs = [
            (binary_path, staged_binary_arch(binary_path, DEDICATED_BINARY_STEM) or "")
            for binary_path in dedicated_candidates
        ] + [
            (binary_path, staged_binary_arch(binary_path, GAME_MODULE_STEM) or "")
            for binary_path in game_modules
        ]
        validate_linux_dedicated_runtime_dependencies(root, dedicated_runtime_specs)
    if host_is_macos():
        validate_macos_staged_metadata(
            root,
            install_root,
            game_dir,
            client_candidates,
            dedicated_candidates,
        )

    for relative_name in STAGED_REQUIRED_GAME_FILES:
        required_file = game_dir / relative_name
        if not required_file.is_file():
            raise ValidationError(f"Required staged game file is missing: {rel(required_file, root)}")

    stale_loose_content = [
        game_dir / relative_name
        for relative_name in STAGED_FORBIDDEN_LOOSE_GAME_PATHS
        if (game_dir / relative_name).exists()
    ]
    if stale_loose_content:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in stale_loose_content)
        raise ValidationError(
            "Staged basepr contains loose content that must live inside openPREY PK4s:\n"
            f"{formatted}"
        )

    validate_windows_symbols(root, install_root, game_dir, staged_arches_for_payload)
    validate_no_non_runtime_artifacts(
        root,
        (install_root, game_dir),
        NON_RUNTIME_PATTERNS,
        "Non-runtime artifacts remain staged",
    )

    print(f"Client: {rel(client_candidates[0], root)}", flush=True)
    print(f"Dedicated server: {rel(dedicated_candidates[0], root)}", flush=True)
    print(f"Game module: {rel(game_modules[0], root)}", flush=True)


def run_runtime_matrix(args: argparse.Namespace, root: Path, env: dict[str, str]) -> None:
    matrix_script = root / "tools" / "tests" / "renderer_validation_matrix.py"
    if not matrix_script.is_file():
        raise ValidationError(f"Renderer validation matrix not found: {matrix_script}")

    command = [
        sys.executable,
        str(matrix_script),
        "--tiers",
        args.runtime_tiers,
        "--timeout",
        str(args.runtime_timeout),
    ]
    if args.runtime_cases:
        command += ["--cases", args.runtime_cases]
    if args.runtime_basepath is not None:
        command += ["--basepath", args.runtime_basepath]
    if args.runtime_skip_official_pak_validation:
        command += ["--skip-official-pak-validation"]

    run_command(
        command,
        cwd=root,
        env=env,
        title="Optional renderer startup validation matrix",
        dry_run=args.dry_run,
    )


def run_macos_static_shell_syntax_checks(args: argparse.Namespace, root: Path, env: dict[str, str]) -> None:
    bash = shutil.which("bash")
    if bash is None:
        print("warning: bash was not found; skipping POSIX shell syntax checks for the macos-static profile.", file=sys.stderr)
        return

    for relative_path in MACOS_STATIC_SHELL_SYNTAX_FILES:
        script = root / relative_path
        if not script.is_file():
            raise ValidationError(f"macos-static shell syntax target was not found: {script}")
        run_command(
            [bash, "-n", relative_path],
            cwd=root,
            env=env,
            title=f"Shell syntax: {relative_path}",
            dry_run=args.dry_run,
        )


def run_macos_static_dry_run_profiles(args: argparse.Namespace, root: Path, env: dict[str, str]) -> None:
    runner = Path(__file__).resolve()
    for profile in MACOS_STATIC_DRY_RUN_PROFILES:
        run_command(
            [
                sys.executable,
                str(runner),
                profile,
                "--skip-python-tests",
                "--skip-build",
                "--no-install",
                "--dry-run",
            ],
            cwd=root,
            env=env,
            title=f"Dry-run validation profile: {profile}",
            dry_run=args.dry_run,
        )


def run_macos_static_validation_track(args: argparse.Namespace, root: Path, env: dict[str, str]) -> None:
    if args.runtime and host_is_macos():
        raise ValidationError(
            "The macos-static profile must not run openPREY on macOS. "
            "Run renderer self-test binaries only on available non-macOS hosts, "
            "or use the documented Apple-hardware signoff workflow for macOS runtime evidence."
        )

    run_macos_static_shell_syntax_checks(args, root, env)
    run_macos_static_dry_run_profiles(args, root, env)


def check_dirty_worktree(args: argparse.Namespace, root: Path, env: dict[str, str]) -> None:
    if not args.fail_on_dirty:
        return

    result = capture_command(["git", "status", "--short"], cwd=root, env=env)
    if result.returncode != 0:
        raise ValidationError(result.stderr.strip() or "git status failed.")
    if result.stdout.strip():
        raise ValidationError("Working tree is dirty; commit, stash, or rerun without --fail-on-dirty.")


def git_value(root: Path, env: dict[str, str], *git_args: str) -> str:
    result = capture_command(["git", *git_args], cwd=root, env=env)
    if result.returncode != 0:
        return "unavailable"
    return result.stdout.strip() or "unavailable"


def describe_git_revision(root: Path, env: dict[str, str]) -> str:
    short_sha = git_value(root, env, "rev-parse", "--short", "HEAD")
    branch = git_value(root, env, "rev-parse", "--abbrev-ref", "HEAD")
    status = capture_command(["git", "status", "--short"], cwd=root, env=env)
    dirty_state = "dirty" if status.returncode == 0 and status.stdout.strip() else "clean"

    github_sha = env.get("GITHUB_SHA", "").strip()
    if github_sha:
        return f"{short_sha} ({branch}, {dirty_state}, GitHub SHA {github_sha[:12]})"

    return f"{short_sha} ({branch}, {dirty_state})"


def apply_profile_defaults(args: argparse.Namespace, root: Path) -> None:
    defaults = PROFILE_DEFAULTS[args.profile]
    if args.build_dir is None:
        args.build_dir = str(root / defaults["build_dir"])
    if args.buildtype is None:
        args.buildtype = defaults["buildtype"]
    if args.clean is None:
        args.clean = defaults["clean"]
    if args.install is None:
        args.install = defaults["install"]
    if defaults.get("skip_build", False):
        args.skip_build = True


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", choices=sorted(PROFILE_DEFAULTS), help="Validation profile to run.")
    parser.add_argument("--source-root", default="", help="openPREY source root. Defaults to this script's repository.")
    parser.add_argument("--build-dir", default=None, help="Meson build directory for this validation run.")
    parser.add_argument("--buildtype", default=None, help="Meson buildtype. Profile default: push=debug, pr=debug.")
    parser.add_argument("--platform-backend", default="", help="Optional Meson platform_backend override.")
    parser.add_argument("--clean", dest="clean", action="store_true", default=None, help="Use Meson --wipe for setup.")
    parser.add_argument("--no-clean", dest="clean", action="store_false", help="Reconfigure/reuse an existing build directory.")
    parser.add_argument(
        "--install",
        dest="install",
        action="store_true",
        default=None,
        help="Run Meson install when building and validate the staged payload.",
    )
    parser.add_argument("--no-install", dest="install", action="store_false", help="Skip Meson install and staged payload checks.")
    parser.add_argument("--skip-python-tests", action="store_true", help="Skip lightweight Python validation tests.")
    parser.add_argument("--skip-build", action="store_true", help="Skip Meson setup/compile/install steps.")
    parser.add_argument("--build-gamelibs", action="store_true", help="Ask the Windows Meson wrapper to build OpenPrey-game during compile.")
    parser.add_argument("--game-libs-repo", default="", help="Override the OpenPrey-game companion repository path.")
    parser.add_argument("--skip-icon-sync", action="store_true", help="Set OPENPREY_SKIP_ICON_SYNC=1 for this run.")
    parser.add_argument("--jobs", "-j", type=positive_int, default=None, help="Parallel compile job count passed to Meson.")
    parser.add_argument("--extra-setup-arg", action="append", default=[], help="Additional argument appended to Meson setup.")
    parser.add_argument("--extra-compile-arg", action="append", default=[], help="Additional argument appended to Meson compile.")
    parser.add_argument("--runtime", action="store_true", help="Also run the safe renderer startup validation matrix after install.")
    parser.add_argument("--runtime-cases", default="", help="Comma-separated renderer validation case ids.")
    parser.add_argument("--runtime-tiers", default="auto,legacy", help="Renderer tiers for --runtime. Defaults to auto,legacy.")
    parser.add_argument("--runtime-timeout", type=positive_int, default=60, help="Per-case renderer validation timeout.")
    parser.add_argument("--runtime-basepath", default=None, help="Prey (2006) install/base path override for renderer validation.")
    parser.add_argument(
        "--runtime-skip-official-pak-validation",
        action="store_true",
        help="Disable official Prey retail PK4 validation for assetless renderer startup smoke checks.",
    )
    parser.add_argument("--fail-on-dirty", action="store_true", help="Fail when the openPREY working tree has uncommitted changes.")
    parser.add_argument("--dry-run", action="store_true", help="Print the selected commands without executing them.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    raw_root = Path(args.source_root) if args.source_root else repo_root_from_script()
    validate_source_root(raw_root)
    root = raw_root.resolve()
    apply_profile_defaults(args, root)
    raw_build_dir = Path(args.build_dir)
    validate_build_dir(root, raw_build_dir)
    build_dir = raw_build_dir.resolve()
    build_dir.parent.mkdir(parents=True, exist_ok=True)
    env = validation_env(args, root)
    wrapper = resolve_meson_wrapper(root)

    started = time.monotonic()
    print(f"openPREY {args.profile} validation", flush=True)
    print(f"Source root: {root}", flush=True)
    print(f"Git revision: {describe_git_revision(root, env)}", flush=True)
    print(f"Build dir: {build_dir}", flush=True)
    print(f"Build type: {args.buildtype}", flush=True)
    print(f"GameLibs repo: {env['OPENPREY_GAMELIBS_REPO']}", flush=True)

    try:
        check_dirty_worktree(args, root, env)
        ensure_game_libs_repo(env)

        if not args.skip_python_tests:
            run_python_tests(args, root, env)

        if args.profile == MACOS_STATIC_PROFILE:
            run_macos_static_validation_track(args, root, env)

        if not args.skip_build:
            run_command(
                wrapper + setup_args(args, root, build_dir),
                cwd=root,
                env=env,
                title="Meson setup",
                dry_run=args.dry_run,
            )
            run_command(
                wrapper + compile_args(args, build_dir),
                cwd=root,
                env=env,
                title="Meson compile",
                dry_run=args.dry_run,
            )
            if args.install:
                run_command(
                    wrapper + install_args(build_dir),
                    cwd=root,
                    env=env,
                    title="Meson install",
                    dry_run=args.dry_run,
                )

        # --skip-build suppresses setup, compile, and install, but an explicit
        # --install still requests validation of the existing staged payload.
        # Release workflows use this after staging and before mutating binaries
        # for debug-symbol packaging.
        if args.install:
            validate_staged_payload(root, dry_run=args.dry_run)

        if args.runtime:
            if not args.install and not args.dry_run:
                print("warning: --runtime uses the current .install tree because --install is disabled.", file=sys.stderr)
            run_runtime_matrix(args, root, env)

    except ValidationError as exc:
        print(f"\nvalidation failed: {exc}", file=sys.stderr, flush=True)
        return 1

    elapsed = time.monotonic() - started
    section("Validation complete")
    print(f"{args.profile} validation passed in {elapsed:.1f}s.", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
