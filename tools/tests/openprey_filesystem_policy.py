#!/usr/bin/env python3
"""Regression checks for Prey retail discovery and PK4 policy."""

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


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    brace_start = source.find("{", start)
    depth = 0
    in_string = False
    escaped = False
    for index in range(brace_start, len(source)):
        char = source[index]
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"Could not find end of {signature!r}")


def validate_pk4_policy(source: str) -> None:
    validator = function_body(
        source,
        "bool idFileSystemLocal::ValidateRequiredOfficialPaks(",
    )
    resolver_probe = function_body(source, "static bool FS_HasGameFilesAtGameDirPath(")
    misplaced = function_body(source, "bool idFileSystemLocal::FindMisplacedOfficialPaks(")

    for name in ("pak000.pk4", "pak001.pk4", "pak002.pk4", "pak003.pk4", "pak004.pk4"):
        require(source, f'"{name}"', "classic Prey PK4 catalog")
    for name, checksum in (
        ("pak_data.pk4", "0xbe295ead"),
        ("pak_sound.pk4", "0xe0c27ee2"),
        ("pak_en_v.pk4", "0x952b910e"),
        ("pak_en_t.pk4", "0x6625f12d"),
    ):
        require(source, f'{{ "{name}",', "digital Prey PK4 catalog")
        require(source, checksum, f"{name} checksum")
        require(resolver_probe, f'"{name}"', f"{name} discovery probe")

    require(validator, "requiredClassicPk4s", "classic layout detection")
    require(validator, "requiredDigitalPk4s", "digital layout detection")
    require(validator, "classicFoundCount", "classic layout member count")
    require(validator, "digitalFoundCount", "digital layout member count")
    require(validator, "expectedChecksum != 0", "presence-only zero-checksum policy")
    require(misplaced, "info->checksum != 0", "presence-only misplaced-pack diagnostics")
    require(validator, "no known official Prey retail pack layout detected", "dual-layout diagnostic")


def validate_install_discovery(source: str) -> None:
    resolver = function_body(source, "static bool FS_TryResolveBasePathCandidate(")
    registry = function_body(source, "static void FS_BuildRegistryInstallCandidates(")
    uninstall = function_body(
        source,
        "static void FS_AppendPreyPathsFromRegistryUninstallBranch(",
    )
    known = function_body(source, "static void FS_BuildKnownInstallCandidates(")
    steam = function_body(source, "static void FS_BuildSteamInstallCandidates(")
    gog = function_body(source, "static void FS_BuildGogInstallCandidates(")
    auto = function_body(source, "static bool FS_AutoDiscoverBasePath(")

    require(resolver, "FS_HasGameFilesAtGameDirPath", "shared install resolver")
    for owner in ("Human Head Studios", "2K Games", "3D Realms"):
        require(registry, owner, f"{owner} registry install key")
        require(known, owner, f"{owner} known install root")
    require(registry, "App Paths\\\\prey.exe", "Prey App Paths lookup")
    for value in ("DisplayName", "InstallLocation", "DisplayIcon", "UninstallString"):
        require(uninstall, f'"{value}"', f"uninstall {value} lookup")
    require(uninstall, 'displayName.Find( "prey", false )', "Prey uninstall-name filter")

    require(steam, '"OPENPREY_STEAM_ROOT"', "primary Steam override")
    require(steam, 'path.AppendPath( "Prey" )', "Steam Prey folder")
    reject(steam, 'path.AppendPath( "Quake 4" )', "Steam Quake 4 folder")
    require(gog, '"C:/GOG Games/Prey"', "GOG Prey folder")
    reject(gog, '"C:/GOG Games/Quake 4"', "GOG Quake 4 folder")
    require(auto, "FS_BuildRegistryInstallCandidates", "registry discovery source")
    require(auto, "FS_BuildKnownInstallCandidates", "known-path discovery source")
    require(auto, "FS_BuildSteamInstallCandidates", "Steam discovery source")
    require(auto, "FS_BuildGogInstallCandidates", "GOG discovery source")


def validate_platform_residuals() -> None:
    sys_header = read("src/sys/sys_public.h")
    win_main = read("src/sys/win32/win_main.cpp")
    common = function_body(read("src/framework/Common.cpp"), "void Com_ExecMachineSpec_f(")

    require(sys_header, "#if defined( _WIN64 )", "Win64 event ABI branch")
    require(sys_header, "#define CPU_EASYARGS", "event callback ABI setting")
    require(win_main, "!win32.activeApp || win32.hWnd == NULL", "native focus guard")
    for mode in (6, 3, 2, 0):
        require(common, f'SetCVarInteger( "r_mode", {mode}', f"Prey r_mode preset {mode}")


def validate_prey_filesystem_api(source: str) -> None:
    header = read("src/framework/FileSystem.h")
    require(header, "OpenExplicitFileAppend", "Prey explicit append interface")
    append = function_body(source, "idFile *idFileSystemLocal::OpenExplicitFileAppend(")
    require(append, 'OpenOSFile( OSPath, "ab" )', "Prey explicit append mode")
    require(append, "f->handleSync = sync", "Prey explicit append sync policy")

    require(source, 'fs_devpath( "fs_devpath"', "registered development path cvar")
    init = function_body(source, "void idFileSystemLocal::Init(")
    require(init, 'StartupVariable( "fs_devpath", false )', "early development path override")
    setup = function_body(source, "void idFileSystemLocal::SetupGameDirectories(")
    require(setup, "AddGameDirectory( fs_devpath.GetString(), gameName )", "development overlay search root")
    if setup.find("AddGameDirectory( fs_devpath.GetString(), gameName )") < setup.find("AddGameDirectory( fs_cdpath.GetString(), gameName )"):
        raise AssertionError("fs_devpath must be inserted last so it has highest loose-file priority")
    require(header, "optional development overlay root, read & write", "documented development overlay")


def validate_user_diagnostics(source: str) -> None:
    for stale in (
        '"openQ4 runtime',
        '"Retail Quake 4',
        '"Required official Quake 4',
        '"openQ4 startup config',
    ):
        reject(source, stale, "active FileSystem diagnostics")
    require(source, "Required official Prey media pk4 files", "Prey retail validation diagnostic")
    require(source, "openPREY runtime content packs", "openPREY runtime-pack diagnostic")


def main() -> None:
    source = read("src/framework/FileSystem.cpp")
    validate_pk4_policy(source)
    validate_install_discovery(source)
    validate_platform_residuals()
    validate_prey_filesystem_api(source)
    validate_user_diagnostics(source)
    print("openprey_filesystem_policy: ok")


if __name__ == "__main__":
    main()
