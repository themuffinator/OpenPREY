#!/usr/bin/env python3
"""Static contract for opt-in Linux stock-map dedicated gameplay evidence."""

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
    runner = read("tools/tests/linux_dedicated_stock_map_smoke.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    validator = read("tools/validation/openq4_validate.py")
    status_ledger = read("docs/dev/prey-rebase/status-ledger.md")
    session = read("src/framework/Session.cpp")
    linux_main = read("src/sys/linux/main.cpp")
    macos_main = read("src/sys/osx/macosx_sdl3_main.cpp")
    posix_console = read("src/sys/posix/posix_syscon.cpp")

    for token in (
        'MAP_NAME = "game/dmroadhouse"',
        'REPORT_SCHEMA_VERSION = 1',
        'REPORT_TYPE = "linux-wayland-stock-dedicated"',
        '"--dedicated-executable"',
        '"--client-executable"',
        '"--basepath"',
        '"--output-root"',
        '"--physical-hardware"',
        '"--port"',
        '"--server-ready-timeout"',
        '"--client-timeout"',
        '"--shutdown-timeout"',
        "validate_native_elf",
        'if arch != native_arch:',
        "host_evidence = collect_linux_host_evidence()",
        "reject_virtualized_physical_attestation(host_evidence)",
        '"packaged runtime architecture mismatch:',
        '("packaged openPREY mod manifest", mod_manifest)',
        '("packaged openPREY pak0.pk4", openprey_pak0)',
        '("packaged openPREY pak1.pk4", openprey_pak1)',
        'install_root / "base"',
        '"refusing stock-map evidence because the staged package contains retail base overrides:',
        'home / ".local" / "share" / "openprey"',
        "server_command = [str(dedicated)]",
        "client_command = [str(client)]",
        '("sys_consoleWindow", "0")',
        "Selected game module: logical='game'",
        "expected_module_marker(game_module, arch)",
        '"Server decl checksum:"',
        'f"Map: {MAP_NAME}"',
        'f"Dedicated map ready: {MAP_NAME}"',
        '"SDL_VIDEO_DRIVER": "wayland"',
        '"SDL_VIDEODRIVER": "wayland"',
        "stat.S_ISSOCK(socket_mode)",
        '"Wayland display path is not a live Unix socket:',
        'client_environment.pop("DISPLAY", None)',
        'client_environment.pop("OPENPREY_FORCE_X11", None)',
        'server_environment.pop(name, None)',
        'tempfile.TemporaryDirectory(prefix=f"openprey-ded-xdg-{arch}-")',
        '"XDG_RUNTIME_DIR": str(server_runtime)',
        'SERVER_VIDEO_DRIVER_CANARY = "openprey-dedicated-must-not-init-video"',
        'server_environment["SDL_VIDEO_DRIVER"] = SERVER_VIDEO_DRIVER_CANARY',
        'server_environment["SDL_VIDEODRIVER"] = SERVER_VIDEO_DRIVER_CANARY',
        'private dedicated XDG runtime socket path would exceed the Linux limit',
        'find_server_video_markers',
        '"nonempty dedicated engine log"',
        '"nonempty client engine log"',
        'ordered_pattern_failures',
        '"serverLifecycleOrderFailures": server_sequence_failures',
        '"clientLifecycleOrderFailures": client_sequence_failures',
        '"received connect response from"',
        'r"SpawnPlayer:\\s*\\d+"',
        '"AutoExecAfterMapLoad: first active draw observed"',
        '"Renderer tier contract:"',
        '"r_rendererMetrics 1"',
        '"rendererBenchmarkCapture"',
        '"rendererBenchmark capture("',
        'int(samples.group(1)) > 0',
        "validate_nontrivial_tga",
        'process.stdin.write("quit\\n")',
        "stop_process(server_process, args.shutdown_timeout, server_ready)",
        'client_abort_reason = "client timeout"',
        '"--------------- Game Shutdown ---------------"',
        '"missingMarkers": missing',
        '"reportSchemaVersion": REPORT_SCHEMA_VERSION',
        '"reportType": REPORT_TYPE',
        '"fatalMarkers": fatal',
        '"physicalHardwareAttested": bool(args.physical_hardware)',
        '"virtualMachineDetected": host_evidence["virtualMachineDetected"]',
        '"containerDetected": host_evidence["containerDetected"]',
        '"virtualizationInspection": host_evidence',
        '"nativeProcessArchitecture": True',
        'run_dir / "report.json"',
    ):
        require(runner, token, "Linux stock-map dedicated smoke runner")

    for token in (
        "/run/user/0",
        "Program Files",
        "steamapps",
        "r_rendererPerfThreshold",
        '"dummy"',
        "baseoq4",
        "q4base",
        "game-mp_",
        "openQ4-client",
        "openQ4-ded",
        "Quake 4",
    ):
        reject(runner, token, "portable opt-in Linux stock-map runner")

    for workflow, name in ((commit, "commit validation"), (push, "push validation")):
        require(workflow, "tools/tests/linux_dedicated_stock_map_smoke.py", f"{name} syntax coverage")
        require(workflow, "tools/tests/linux_physical_host_evidence.py", f"{name} host-evidence syntax coverage")
        require(
            workflow,
            "python tools/tests/linux_dedicated_stock_map_smoke_contract.py",
            f"{name} static contract coverage",
        )
        require(
            workflow,
            "python tools/tests/linux_physical_host_evidence_contract.py",
            f"{name} host-evidence behavior coverage",
        )
    require(
        validator,
        'root / "tools" / "tests" / "linux_dedicated_stock_map_smoke_contract.py"',
        "shared validation static contract coverage",
    )
    require(
        validator,
        'root / "tools" / "tests" / "linux_physical_host_evidence_contract.py"',
        "shared validation host-evidence behavior coverage",
    )
    require(status_ledger, "TODO-RELEASE-LANES", "release-lane deferral policy")
    require(
        session,
        "void idSessionLocal::ShowLoadingGui() {\n#ifdef ID_DEDICATED\n\t// Dedicated servers have no loading GUI",
        "dedicated map-load presentation exclusion",
    )
    require(
        session,
        "#ifdef ID_DEDICATED\n\t// Dedicated map loads have no client presentation path",
        "dedicated map-load pacifier exclusion",
    )
    require(
        session,
        "lastPacifierTime = dedicatedTime;\n\tidAsyncNetwork::server.PacifierUpdate();\n\treturn;\n#endif",
        "dedicated map-load server pacifier preservation",
    )
    require(
        session,
        "void idSessionLocal::LoadLoadingGui( const char *mapName ) {\n#ifdef ID_DEDICATED\n\tguiLoading = NULL;\n\treturn;",
        "dedicated loading GUI exclusion",
    )
    require(
        session,
        "void idSessionLocal::UpdateScreen( bool outOfSequence ) {\n\n#ifdef ID_DEDICATED\n\treturn;",
        "dedicated screen-update exclusion",
    )
    require(
        session,
        "void idSessionLocal::StartWipe( const char *_wipeMaterial, bool hold ) {\n#ifdef ID_DEDICATED\n\t// Dedicated servers never own a presentation surface",
        "dedicated wipe-capture exclusion",
    )
    require(
        session,
        "void idSessionLocal::CompleteWipe() {\n#ifdef ID_DEDICATED\n\treturn;",
        "dedicated wipe-completion exclusion",
    )
    require(
        session,
        'mapSpawned = true;\n#ifdef ID_DEDICATED\n\tcommon->Printf( "Dedicated map ready: %s\\n", mapString.c_str() );',
        "dedicated post-load readiness marker",
    )
    require(
        linux_main,
        "Posix_EarlyInit( );\n#ifndef ID_DEDICATED\n\tSys_ReportWaylandRuntime();\n\tSys_ShowSplash();",
        "Linux dedicated splash exclusion",
    )
    require(
        macos_main,
        "Posix_EarlyInit();\n#ifndef ID_DEDICATED\n\tSys_ShowSplash();",
        "macOS dedicated splash exclusion",
    )
    require(
        posix_console,
        '"sys_consoleWindow",\n#ifdef ID_DEDICATED\n\t"0",\n#else\n\t"1",',
        "dedicated system-console window default",
    )
    require(
        posix_console,
        "void Posix_ConsoleFatalErrorWait( void ) {\n#ifdef ID_DEDICATED\n\t// Headless servers already preserve fatal diagnostics",
        "dedicated fatal-error window exclusion",
    )
    require(
        posix_console,
        "void Sys_ShowSplash( void ) {\n#if defined( USE_SDL3 ) && !defined( ID_DEDICATED )",
        "dedicated splash implementation exclusion",
    )

    print("linux_dedicated_stock_map_smoke_contract: ok")


if __name__ == "__main__":
    main()
