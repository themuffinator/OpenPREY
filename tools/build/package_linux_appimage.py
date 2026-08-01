#!/usr/bin/env python3
"""Build and verify an openPREY AppImage from a packaged Linux release tree."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.build import verify_linux_release_artifacts as linux_release  # noqa: E402
from tools.build.linux_metadata import (  # noqa: E402
    LinuxMetadataError,
    desktop_entry_exec,
    desktop_exec_command,
)
from tools.validation import openq4_validate as staged_validator  # noqa: E402


APPIMAGE_ARCHES = {
    "x64": "x86_64",
    "arm64": "aarch64",
}
APPIMAGE_PACKAGE_PATH = Path("usr") / "share" / "openprey"
APPIMAGE_TYPE2_MAGIC = b"AI\x02"
MAX_APPIMAGE_BYTES = 4 * 1024 * 1024 * 1024
CORE_RUNTIME_RELATIVE_PATHS = {
    "x64": (
        Path("openPREY-client_x64"),
        Path("openPREY-ded_x64"),
        Path("basepr/game_x64.so"),
    ),
    "arm64": (
        Path("openPREY-client_arm64"),
        Path("openPREY-ded_arm64"),
        Path("basepr/game_arm64.so"),
    ),
}
RUNPATH_RE = re.compile(r"\((?:RPATH|RUNPATH)\).*?\[([^\]]*)\]")


class AppImageError(RuntimeError):
    """The AppImage input, toolchain, or output violated its release contract."""


def is_relative_to(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def require_directory(path: Path, label: str) -> Path:
    if path.is_symlink():
        raise AppImageError(f"{label} must not be a symlink: {path}")
    try:
        resolved = path.resolve(strict=True)
    except OSError as exc:
        raise AppImageError(f"{label} is unavailable: {path}") from exc
    if not resolved.is_dir():
        raise AppImageError(f"{label} is not a directory: {resolved}")
    return resolved


def require_regular_file(path: Path, label: str, *, executable: bool = False) -> Path:
    if path.is_symlink():
        raise AppImageError(f"{label} must not be a symlink: {path}")
    try:
        resolved = path.resolve(strict=True)
    except OSError as exc:
        raise AppImageError(f"{label} is unavailable: {path}") from exc
    if not resolved.is_file():
        raise AppImageError(f"{label} is not a regular file: {resolved}")
    if executable and not os.access(resolved, os.X_OK):
        raise AppImageError(f"{label} is not executable: {resolved}")
    return resolved


def validate_package_suffix(raw_suffix: str) -> str:
    suffix = raw_suffix.strip()
    if suffix and re.fullmatch(r"(?:-[A-Za-z0-9]+)+", suffix) is None:
        raise AppImageError(f"unsafe AppImage package suffix: {raw_suffix!r}")
    return suffix


def appimage_arch(arch: str) -> str:
    try:
        return APPIMAGE_ARCHES[arch]
    except KeyError as exc:
        raise AppImageError(f"unsupported AppImage architecture: {arch!r}") from exc


def appimage_filename(version_tag: str, arch: str, package_suffix: str = "") -> str:
    if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]*", version_tag) is None or ".." in version_tag:
        raise AppImageError(f"unsafe AppImage version tag: {version_tag!r}")
    suffix = validate_package_suffix(package_suffix)
    return f"openprey-{version_tag}{suffix}-{appimage_arch(arch)}.AppImage"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_tree_entries(root: Path, label: str, *, allow_safe_symlinks: bool) -> None:
    root = require_directory(root, label)
    for path in [root, *sorted(root.rglob("*"))]:
        try:
            mode = path.lstat().st_mode
        except OSError as exc:
            raise AppImageError(f"{label} entry is unreadable: {path}") from exc
        if stat.S_ISLNK(mode):
            if not allow_safe_symlinks:
                raise AppImageError(f"{label} contains a symlink: {path.relative_to(root)}")
            try:
                target = path.resolve(strict=True)
            except OSError as exc:
                raise AppImageError(f"{label} contains a broken symlink: {path.relative_to(root)}") from exc
            if not is_relative_to(target, root):
                raise AppImageError(
                    f"{label} symlink escapes its root: {path.relative_to(root)} -> {target}"
                )
        elif not stat.S_ISREG(mode) and not stat.S_ISDIR(mode):
            raise AppImageError(f"{label} contains a special file: {path.relative_to(root)}")


def render_appimage_desktop(source_text: str) -> str:
    if "\x00" in source_text:
        raise AppImageError("Linux desktop entry contains NUL data")
    lines = source_text.splitlines()
    in_desktop_entry = False
    exec_count = 0
    icon_count = 0
    for index, raw_line in enumerate(lines):
        line = raw_line.strip()
        if line.startswith("[") and line.endswith("]"):
            in_desktop_entry = line == "[Desktop Entry]"
            continue
        if not in_desktop_entry or not line or line.startswith(("#", ";")):
            continue
        if line.startswith("Exec="):
            lines[index] = "Exec=openprey"
            exec_count += 1
        elif line.startswith("Icon="):
            if line != "Icon=openprey":
                raise AppImageError("Linux desktop entry must use Icon=openprey")
            icon_count += 1
    if exec_count != 1:
        raise AppImageError(f"Linux desktop entry must contain exactly one Exec key; found {exec_count}")
    if icon_count != 1:
        raise AppImageError(f"Linux desktop entry must contain exactly one Icon key; found {icon_count}")
    return "\n".join(lines) + "\n"


def render_apprun(arch: str) -> str:
    client = f"openPREY-client_{arch}"
    return f"""#!/bin/sh
set -eu

if [ -n "${{APPDIR:-}}" ]; then
    case "${{APPDIR}}" in
        /*) appdir="${{APPDIR}}" ;;
        *) echo "openPREY AppImage received a non-absolute APPDIR" >&2; exit 127 ;;
    esac
else
    appdir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)"
fi

package_root="${{appdir}}/usr/share/openprey"
if [ ! -x "${{package_root}}/{client}" ] || [ ! -d "${{package_root}}/basepr" ]; then
    echo "openPREY AppImage payload is incomplete" >&2
    exit 127
fi

if [ -n "${{LD_LIBRARY_PATH:-}}" ]; then
    export LD_LIBRARY_PATH="${{appdir}}/usr/lib:${{LD_LIBRARY_PATH}}"
else
    export LD_LIBRARY_PATH="${{appdir}}/usr/lib"
fi

cd -- "${{package_root}}"
exec "./{client}" "$@"
"""


def write_utf8_file(path: Path, text: str, *, executable: bool = False) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
    os.chmod(path, 0o755 if executable else 0o644)


def run_checked(command: list[str], *, cwd: Path, env: dict[str, str], label: str) -> None:
    completed = subprocess.run(command, cwd=cwd, env=env, check=False)
    if completed.returncode != 0:
        raise AppImageError(f"{label} failed with exit code {completed.returncode}")


def validate_desktop_file(path: Path) -> None:
    validator = shutil.which("desktop-file-validate")
    if validator is None:
        raise AppImageError("AppImage packaging requires desktop-file-validate")
    completed = subprocess.run(
        [validator, str(path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        diagnostics = "\n".join(
            part for part in (completed.stdout.strip(), completed.stderr.strip()) if part
        )
        raise AppImageError(f"AppImage desktop entry is invalid: {diagnostics or '<no diagnostics>'}")


def compare_packaged_payload(source_root: Path, appimage_package_root: Path, arch: str) -> None:
    source_files = {
        path.relative_to(source_root)
        for path in source_root.rglob("*")
        if path.is_file() and not path.is_symlink()
    }
    packaged_files = {
        path.relative_to(appimage_package_root)
        for path in appimage_package_root.rglob("*")
        if path.is_file() and not path.is_symlink()
    }
    if source_files != packaged_files:
        missing = sorted(path.as_posix() for path in source_files - packaged_files)
        unexpected = sorted(path.as_posix() for path in packaged_files - source_files)
        raise AppImageError(
            "AppImage openPREY payload file set differs from the release package. "
            f"Missing: {missing or '<none>'}; unexpected: {unexpected or '<none>'}"
        )

    mutable_elfs = set(CORE_RUNTIME_RELATIVE_PATHS[arch])
    for relative in sorted(source_files - mutable_elfs):
        source_hash = sha256_file(source_root / relative)
        packaged_hash = sha256_file(appimage_package_root / relative)
        if source_hash != packaged_hash:
            raise AppImageError(
                f"AppImage packaging changed non-ELF release payload bytes: {relative.as_posix()}"
            )


def validate_appimage_rpaths(appdir: Path, package_root: Path, arch: str) -> None:
    library_root = (appdir / "usr/lib").resolve(strict=True)
    for relative in CORE_RUNTIME_RELATIVE_PATHS[arch]:
        binary = package_root / relative
        dynamic = staged_validator.readelf_output(binary, ["-W", "-d"], appdir)
        if "(TEXTREL)" in dynamic:
            raise AppImageError(f"AppImage ELF contains TEXTREL: {relative.as_posix()}")
        runpaths = RUNPATH_RE.findall(dynamic)
        if not runpaths:
            raise AppImageError(f"AppImage ELF has no package-relative runtime search path: {relative}")
        reaches_library_root = False
        for runpath in runpaths:
            for entry in runpath.split(":"):
                if not entry.startswith("$ORIGIN"):
                    raise AppImageError(
                        f"AppImage ELF has a non-relative runtime search path: {relative}: {entry!r}"
                    )
                suffix = entry[len("$ORIGIN") :].lstrip("/")
                resolved = (binary.parent / suffix).resolve(strict=False)
                if not is_relative_to(resolved, appdir):
                    raise AppImageError(
                        f"AppImage ELF runtime search path escapes AppDir: {relative}: {entry!r}"
                    )
                if resolved == library_root:
                    reaches_library_root = True
        if not reaches_library_root:
            raise AppImageError(
                f"AppImage ELF runtime search path does not reach usr/lib: {relative.as_posix()}"
            )


def validate_appimage_dependency_resolution(appdir: Path, package_root: Path, arch: str) -> None:
    ldd = shutil.which("ldd")
    if ldd is None:
        raise AppImageError("AppImage dependency validation requires ldd")
    env = linux_release.sanitized_loader_environment()
    for relative in CORE_RUNTIME_RELATIVE_PATHS[arch]:
        completed = subprocess.run(
            [ldd, "-r", str(package_root / relative)],
            cwd=appdir,
            env=env,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        diagnostics = "\n".join(
            part for part in (completed.stdout.strip(), completed.stderr.strip()) if part
        )
        if completed.returncode != 0 or re.search(
            r"(?:\bnot found\b|\bundefined symbol\b|\bsymbol lookup error\b)",
            diagnostics,
            flags=re.IGNORECASE,
        ):
            raise AppImageError(
                f"AppImage dependency resolution failed for {relative.as_posix()} "
                f"(ldd -r exit {completed.returncode}):\n{diagnostics or '<no diagnostics>'}"
            )


def validate_appdir(
    appdir: Path,
    *,
    source_package_root: Path,
    symbols_root: Path,
    arch: str,
) -> None:
    appdir = require_directory(appdir, "AppDir")
    validate_tree_entries(appdir, "AppDir", allow_safe_symlinks=True)
    package_root = require_directory(appdir / APPIMAGE_PACKAGE_PATH, "AppImage openPREY package root")
    validate_tree_entries(package_root, "AppImage openPREY package payload", allow_safe_symlinks=False)

    apprun = require_regular_file(appdir / "AppRun", "AppImage AppRun", executable=True)
    apprun_text = apprun.read_text(encoding="utf-8")
    for token in (
        f"openPREY-client_{arch}",
        "usr/share/openprey",
        "LD_LIBRARY_PATH",
        'exec "./openPREY-client_',
        '"$@"',
    ):
        if token not in apprun_text:
            raise AppImageError(f"AppImage AppRun is missing required token {token!r}")

    desktop_files = sorted(appdir.glob("*.desktop"))
    if len(desktop_files) != 1:
        raise AppImageError(
            f"AppDir must contain exactly one root desktop entry; found {len(desktop_files)}"
        )
    validate_desktop_file(desktop_files[0])
    try:
        command = desktop_exec_command(desktop_entry_exec(desktop_files[0]))
    except LinuxMetadataError as exc:
        raise AppImageError(f"AppImage desktop entry is malformed: {exc}") from exc
    if command != "openprey":
        raise AppImageError(f"AppImage desktop entry uses Exec={command!r}, expected 'openprey'")

    root_icons = [path for path in appdir.glob("openprey.*") if path.suffix in {".png", ".svg", ".xpm"}]
    if len(root_icons) != 1:
        raise AppImageError(f"AppDir must contain exactly one root openprey icon; found {len(root_icons)}")
    dir_icon = appdir / ".DirIcon"
    if not dir_icon.is_symlink() or dir_icon.resolve(strict=True) != root_icons[0].resolve(strict=True):
        raise AppImageError("AppDir .DirIcon must be a safe link to the root openprey icon")

    compare_packaged_payload(source_package_root, package_root, arch)
    expected = linux_release.expected_runtime_binaries(package_root, arch)
    linux_release.reject_unexpected_binary_variants(package_root, expected)
    staged_validator.validate_staged_architecture_set(
        package_root,
        package_root / "basepr",
        [expected[0][0]],
        [expected[1][0]],
    )
    staged_validator.validate_distinct_game_modules(
        package_root,
        [expected[2][0]],
        [expected[3][0]],
    )
    staged_validator.validate_linux_binary_hardening(
        package_root,
        [(path, arch, is_module) for path, _, is_module in expected],
    )
    linux_release.validate_debuglink_pairs(package_root, symbols_root, expected)
    validate_appimage_rpaths(appdir, package_root, arch)
    validate_appimage_dependency_resolution(appdir, package_root, arch)


def validate_type2_appimage(path: Path, arch: str) -> Path:
    path = require_regular_file(path, "Linux AppImage", executable=True)
    size = path.stat().st_size
    if size <= 0 or size > MAX_APPIMAGE_BYTES:
        raise AppImageError(f"Linux AppImage has an invalid size ({size} bytes): {path}")
    with path.open("rb") as handle:
        header = handle.read(16)
    if len(header) < 11 or header[:4] != b"\x7fELF" or header[8:11] != APPIMAGE_TYPE2_MAGIC:
        raise AppImageError(f"Linux AppImage is not a type-2 AppImage: {path}")

    elf_header = staged_validator.readelf_output(path, ["-h"], path.parent)
    class_line = next((line for line in elf_header.splitlines() if "Class:" in line), "")
    machine_line = next((line for line in elf_header.splitlines() if "Machine:" in line), "")
    expected_machine = {
        "x64": "Advanced Micro Devices X86-64",
        "arm64": "AArch64",
    }[arch]
    if "ELF64" not in class_line or expected_machine not in machine_line:
        raise AppImageError(
            f"Linux AppImage runtime architecture does not match {arch}: "
            f"{class_line.strip() or '<missing class>'}; "
            f"{machine_line.strip() or '<missing machine>'}"
        )
    return path


def extract_appimage(appimage: Path, destination: Path) -> Path:
    destination.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    env.pop("APPDIR", None)
    env.pop("APPIMAGE", None)
    env.pop("APPIMAGE_EXTRACT_AND_RUN", None)
    env.pop("OWD", None)
    completed = subprocess.run(
        [str(appimage), "--appimage-extract"],
        cwd=destination,
        env=env,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        diagnostics = "\n".join(
            part for part in (completed.stdout.strip(), completed.stderr.strip()) if part
        )
        raise AppImageError(
            f"type-2 AppImage extraction failed with exit code {completed.returncode}:\n"
            f"{diagnostics or '<no diagnostics>'}"
        )
    return require_directory(destination / "squashfs-root", "extracted AppDir")


def validate_source_package(package_root: Path, symbols_root: Path, arch: str) -> None:
    validate_tree_entries(package_root, "Linux release package", allow_safe_symlinks=False)
    require_regular_file(package_root / "VERSION.txt", "Linux release version manifest")
    require_regular_file(package_root / "LICENSE", "Linux release license")
    require_regular_file(package_root / "README.html", "Linux release readme")
    require_regular_file(
        package_root / "share/applications/openprey.desktop",
        "Linux release desktop entry",
    )
    icon_candidates = (
        package_root / "share/icons/hicolor/scalable/apps/openprey.svg",
        package_root / "share/icons/hicolor/256x256/apps/openprey.png",
    )
    if not any(path.is_file() and not path.is_symlink() for path in icon_candidates):
        raise AppImageError("Linux release package has no scalable or 256x256 openprey icon")

    expected = linux_release.validate_linux_runtime_payload(package_root, arch)
    linux_release.validate_debuglink_pairs(package_root, symbols_root, expected)


def build_appimage(args: argparse.Namespace) -> Path:
    arch = args.arch
    if arch not in APPIMAGE_ARCHES:
        raise AppImageError(f"unsupported AppImage architecture: {arch!r}")
    source_date_epoch = args.source_date_epoch.strip()
    if re.fullmatch(r"[0-9]{1,12}", source_date_epoch) is None:
        raise AppImageError(f"invalid SOURCE_DATE_EPOCH: {args.source_date_epoch!r}")

    package_root = require_directory(Path(args.package_dir), "Linux release package root")
    symbols_root = require_directory(Path(args.symbols_dir), "Linux detached-symbol root")
    work_root = require_directory(Path(args.work_root), "AppImage work root")
    linuxdeploy = require_regular_file(Path(args.linuxdeploy), "linuxdeploy", executable=True)
    appimagetool = require_regular_file(Path(args.appimagetool), "appimagetool", executable=True)
    runtime_file = require_regular_file(Path(args.runtime_file), "AppImage type-2 runtime")

    expected_name = appimage_filename(args.version_tag, arch, args.package_suffix)
    output = Path(args.output).resolve(strict=False)
    if output.name != expected_name:
        raise AppImageError(f"AppImage output must be named {expected_name!r}, got {output.name!r}")
    if output.exists() or output.is_symlink():
        raise AppImageError(f"AppImage output already exists: {output}")
    if is_relative_to(output, package_root):
        raise AppImageError("AppImage output must be outside the source package tree")
    if is_relative_to(output, symbols_root):
        raise AppImageError("AppImage output must be outside the detached-symbol tree")
    if is_relative_to(work_root, package_root):
        raise AppImageError("AppImage work root must be outside the source package tree")
    output.parent.mkdir(parents=True, exist_ok=True)

    validate_source_package(package_root, symbols_root, arch)

    with tempfile.TemporaryDirectory(prefix=f"openprey-appimage-{arch}-", dir=work_root) as temp_name:
        temp_root = Path(temp_name).resolve(strict=True)
        appdir = temp_root / "openPREY.AppDir"
        appimage_package_root = appdir / APPIMAGE_PACKAGE_PATH
        appimage_package_root.parent.mkdir(parents=True)
        shutil.copytree(package_root, appimage_package_root)

        inputs = temp_root / "inputs"
        desktop_source = package_root / "share/applications/openprey.desktop"
        desktop_input = inputs / "openprey.desktop"
        write_utf8_file(
            desktop_input,
            render_appimage_desktop(desktop_source.read_text(encoding="utf-8")),
        )
        validate_desktop_file(desktop_input)

        icon_candidates = (
            package_root / "share/icons/hicolor/scalable/apps/openprey.svg",
            package_root / "share/icons/hicolor/256x256/apps/openprey.png",
        )
        icon_input = next(path for path in icon_candidates if path.is_file() and not path.is_symlink())
        apprun_input = inputs / "AppRun"
        write_utf8_file(apprun_input, render_apprun(arch), executable=True)

        linuxdeploy_command = [
            str(linuxdeploy),
            "--appdir",
            str(appdir),
            "--desktop-file",
            str(desktop_input),
            "--icon-file",
            str(icon_input),
            "--custom-apprun",
            str(apprun_input),
        ]
        for relative in CORE_RUNTIME_RELATIVE_PATHS[arch]:
            linuxdeploy_command.extend(
                ["--deploy-deps-only", str(appimage_package_root / relative)]
            )

        tool_env = linux_release.sanitized_loader_environment()
        tool_env["APPIMAGE_EXTRACT_AND_RUN"] = "1"
        tool_env["NO_STRIP"] = "1"
        tool_env["SOURCE_DATE_EPOCH"] = source_date_epoch
        tool_env["VERSION"] = args.version
        run_checked(
            linuxdeploy_command,
            cwd=temp_root,
            env=tool_env,
            label="linuxdeploy AppDir construction",
        )

        root_icons = [path for path in appdir.glob("openprey.*") if path.suffix in {".png", ".svg", ".xpm"}]
        if len(root_icons) != 1:
            raise AppImageError(
                f"linuxdeploy produced {len(root_icons)} root openprey icons; expected exactly one"
            )
        dir_icon = appdir / ".DirIcon"
        if dir_icon.exists() or dir_icon.is_symlink():
            raise AppImageError("linuxdeploy unexpectedly produced a pre-existing .DirIcon")
        dir_icon.symlink_to(root_icons[0].name)

        validate_appdir(
            appdir,
            source_package_root=package_root,
            symbols_root=symbols_root,
            arch=arch,
        )

        appimagetool_env = tool_env.copy()
        appimagetool_env["ARCH"] = appimage_arch(arch)
        run_checked(
            [
                str(appimagetool),
                "--comp",
                "zstd",
                "--runtime-file",
                str(runtime_file),
                str(appdir),
                str(output),
            ],
            cwd=temp_root,
            env=appimagetool_env,
            label="appimagetool image creation",
        )

        os.chmod(output, 0o755)
        validate_type2_appimage(output, arch)
        extracted = extract_appimage(output, temp_root / "extract")
        validate_appdir(
            extracted,
            source_package_root=package_root,
            symbols_root=symbols_root,
            arch=arch,
        )

    return output


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-dir", required=True, help="Packaged Linux release directory")
    parser.add_argument("--symbols-dir", required=True, help="Matching detached-symbol directory")
    parser.add_argument("--arch", required=True, choices=tuple(APPIMAGE_ARCHES))
    parser.add_argument("--version", required=True, help="Display/release version")
    parser.add_argument("--version-tag", required=True, help="Filename-safe release version tag")
    parser.add_argument("--package-suffix", default="", help="Support-tier suffix such as -preview")
    parser.add_argument("--source-date-epoch", required=True, help="Reproducible image timestamp")
    parser.add_argument("--linuxdeploy", required=True, help="Pinned native linuxdeploy AppImage")
    parser.add_argument("--appimagetool", required=True, help="Pinned native appimagetool AppImage")
    parser.add_argument("--runtime-file", required=True, help="Pinned type-2 AppImage runtime prefix")
    parser.add_argument("--work-root", required=True, help="Existing directory for isolated temporary work")
    parser.add_argument("--output", required=True, help="Final .AppImage output path")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        output = build_appimage(args)
    except (
        AppImageError,
        FileNotFoundError,
        LinuxMetadataError,
        linux_release.ReleaseArtifactError,
        staged_validator.ValidationError,
        OSError,
    ) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"Linux AppImage: {output}")
    print(f"SHA-256: {sha256_file(output)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
