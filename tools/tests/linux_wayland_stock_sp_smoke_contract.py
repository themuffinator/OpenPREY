#!/usr/bin/env python3
"""Static contract for the opt-in Linux Wayland stock-SP evidence runner."""

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


def main() -> None:
    runner = read("tools/tests/linux_wayland_stock_sp_smoke.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    validator = read("tools/validation/openq4_validate.py")
    status_ledger = read("docs/dev/prey-rebase/status-ledger.md")

    for token in (
        'MAP_NAME = "game/roadhouse"',
        'REPORT_SCHEMA_VERSION = 1',
        'REPORT_TYPE = "linux-wayland-stock-sp"',
        'DEFAULT_SAVE_SLOT = "linux_wayland_roundtrip"',
        '"--install-root"',
        '"--client-executable"',
        '"--basepath"',
        '"--output-root"',
        '"--arch"',
        '"--save-slot"',
        're.fullmatch(r"[A-Za-z0-9_]{1,64}", args.save_slot)',
        "id command lexer treats '-' as punctuation",
        '"--wayland-display"',
        '"--xdg-runtime-dir"',
        '"--physical-hardware"',
        '"--human-audio-playback-verified"',
        "if arch != native_arch:",
        '"stock SP evidence must run natively:',
        "host_evidence = collect_linux_host_evidence()",
        "reject_virtualized_physical_attestation(host_evidence)",
        "validate_native_elf(client, arch)",
        "validate_native_elf(game_module, arch)",
        '("packaged openPREY mod manifest", mod_manifest)',
        '("packaged openPREY pak0.pk4", openprey_pak0)',
        '("packaged openPREY pak1.pk4", openprey_pak1)',
        'install_root / "base"',
        '"refusing stock SP evidence because the staged package contains retail base overrides:',
        "stat.S_ISSOCK(socket_mode)",
        '"Wayland display path is not a live Unix socket:',
        '"SDL_VIDEO_DRIVER": "wayland"',
        '"SDL_VIDEODRIVER": "wayland"',
        'environment.pop("DISPLAY", None)',
        'environment.pop("OPENPREY_FORCE_X11", None)',
        '("g_autoSkipCinematics", "1")',
        '("s_noSound", "0")',
        '("g_autoExecAfterMapLoad", FIRST_CFG_REL)',
        'f"saveGame {save_slot}"',
        'f"loadGame {save_slot}"',
        '"OPENPREY_WAYLAND_SP_RESTORE_ACTIVE"',
        "validate_save_files(home, args.save_slot)",
        "validate_nontrivial_tga(find_screenshot(home))",
        'log_text.count(f"Map: {MAP_NAME}")',
        'log_text.count("AutoExecAfterMapLoad: first active draw observed")',
        '"---------- Game Map Init SaveGame -----------"',
        '"secondActiveGameplayLifecycle": second_lifecycle',
        '"reportSchemaVersion": REPORT_SCHEMA_VERSION',
        '"reportType": REPORT_TYPE',
        '"lifecycleOrderFailures": lifecycle_order_failures',
        '"OpenAL ALC version reported"',
        '"OpenAL active device reported"',
        '"engine sound system initialized"',
        '"softwareInitializationPassed": software_audio_passed',
        '"humanAudiblePlaybackVerified": bool(args.human_audio_playback_verified)',
        '"automationHeardAudio": False',
        '"physicalHardwareAttested": bool(args.physical_hardware)',
        '"virtualMachineDetected": host_evidence["virtualMachineDetected"]',
        '"containerDetected": host_evidence["containerDetected"]',
        '"virtualizationInspection": host_evidence',
        '"nativeProcessArchitecture": True',
        '"fatalMarkers": fatal_markers',
        '"glErrorMarkers": gl_error_markers',
        '"audioErrorMarkers": audio_error_markers',
        '"cleanExit": clean_exit',
        '"sha256": {',
        'run_dir / "report.json"',
        'run_dir / "report.md"',
        "The automated harness verifies software audio initialization only.",
    ):
        require(runner, token, "Linux Wayland stock-SP smoke runner")

    for token in (
        "RDP Sink",
        "/run/user/0",
        "Program Files",
        "steamapps",
        '"s_noSound", "1"',
        '"SDL_VIDEO_DRIVER": "x11"',
        'r"[A-Za-z0-9_-]{1,64}"',
        "baseoq4",
        "q4base",
        "game-sp_",
        "openQ4-client",
        "Quake 4",
    ):
        reject(runner, token, "portable native-Wayland stock-SP smoke runner")

    for workflow, name in ((commit, "commit validation"), (push, "push validation")):
        require(workflow, "tools/tests/linux_wayland_stock_sp_smoke.py", f"{name} syntax coverage")
        require(workflow, "tools/tests/linux_physical_host_evidence.py", f"{name} host-evidence syntax coverage")
        require(
            workflow,
            "python tools/tests/linux_wayland_stock_sp_smoke_contract.py",
            f"{name} static contract coverage",
        )
        require(
            workflow,
            "python tools/tests/linux_physical_host_evidence_contract.py",
            f"{name} host-evidence behavior coverage",
        )
        reject(
            workflow,
            "python tools/tests/linux_wayland_stock_sp_smoke.py",
            f"{name} must not run retail stock media automatically",
        )

    require(
        validator,
        'root / "tools" / "tests" / "linux_wayland_stock_sp_smoke_contract.py"',
        "shared validation static contract coverage",
    )
    require(
        validator,
        'root / "tools" / "tests" / "linux_physical_host_evidence_contract.py"',
        "shared validation host-evidence behavior coverage",
    )
    reject(
        validator,
        'root / "tools" / "tests" / "linux_wayland_stock_sp_smoke.py"',
        "shared validation must not run retail stock media automatically",
    )
    require(status_ledger, "TODO-RELEASE-LANES", "release-lane deferral policy")

    print("linux_wayland_stock_sp_smoke_contract: ok")


if __name__ == "__main__":
    main()
