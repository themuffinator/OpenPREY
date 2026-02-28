#!/usr/bin/env python3
"""Create curated nightly distributable archives for OpenPrey."""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


PRODUCT_NAME = "OpenPrey"
SUPPORTED_ARCHES = ("x64", "x86", "arm64")

PLATFORM_EXECUTABLE_EXT = {
    "windows": ".exe",
    "linux": "",
    "macos": "",
}

OPENPREY_EXCLUDED_DIRS = {"logs", "screenshots"}
OPENPREY_EXCLUDED_SUFFIXES = {".pdb", ".lib", ".exp", ".ilk"}


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Package OpenPrey nightly artifacts into a release zip."
    )
    parser.add_argument(
        "--platform",
        required=True,
        choices=sorted(PLATFORM_EXECUTABLE_EXT.keys()),
        help="Target runner platform (windows/linux/macos).",
    )
    parser.add_argument(
        "--arch",
        default="x64",
        choices=SUPPORTED_ARCHES,
        help="Target binary architecture tag (default: x64).",
    )
    parser.add_argument(
        "--version",
        required=True,
        help="Human-readable nightly version string.",
    )
    parser.add_argument(
        "--version-tag",
        required=True,
        help="File-safe nightly version tag.",
    )
    parser.add_argument(
        "--source-root",
        default=".",
        help="OpenPrey repository root.",
    )
    parser.add_argument(
        "--install-dir",
        default=None,
        help="Install directory to package (defaults to <source-root>/.install).",
    )
    parser.add_argument(
        "--output-dir",
        required=True,
        help="Output directory for the generated package artifacts.",
    )
    return parser.parse_args(argv[1:])


def get_root_binaries(platform: str, arch: str) -> tuple[str, ...]:
    exe_ext = PLATFORM_EXECUTABLE_EXT[platform]
    binaries = [
        f"{PRODUCT_NAME}-client_{arch}{exe_ext}",
        f"{PRODUCT_NAME}-ded_{arch}{exe_ext}",
    ]
    if platform == "windows":
        binaries.append("OpenAL32.dll")
    return tuple(binaries)


def copy_required_binaries(platform: str, arch: str, install_dir: Path, package_root: Path) -> None:
    for filename in get_root_binaries(platform, arch):
        source = install_dir / filename
        if not source.is_file():
            raise FileNotFoundError(f"required distributable not found: {source}")
        shutil.copy2(source, package_root / filename)


def create_openprey_pk4(
    install_openprey_dir: Path, destination_pk4: Path
) -> tuple[int, list[str]]:
    added_files = 0
    skipped_samples: list[str] = []

    with ZipFile(destination_pk4, "w", compression=ZIP_DEFLATED, compresslevel=9) as pk4:
        for path in sorted(install_openprey_dir.rglob("*")):
            if not path.is_file():
                continue

            rel = path.relative_to(install_openprey_dir)
            rel_parts_lower = {part.lower() for part in rel.parts}

            if rel_parts_lower & OPENPREY_EXCLUDED_DIRS:
                if len(skipped_samples) < 5:
                    skipped_samples.append(rel.as_posix())
                continue

            if path.suffix.lower() in OPENPREY_EXCLUDED_SUFFIXES:
                if len(skipped_samples) < 5:
                    skipped_samples.append(rel.as_posix())
                continue

            pk4.write(path, arcname=rel.as_posix())
            added_files += 1

    return added_files, skipped_samples


def create_release_zip(package_root: Path, zip_path: Path) -> None:
    if zip_path.exists():
        zip_path.unlink()

    with ZipFile(zip_path, "w", compression=ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(package_root.rglob("*")):
            if not path.is_file():
                continue
            archive.write(path, arcname=path.relative_to(package_root).as_posix())


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    source_root = Path(args.source_root).resolve()
    install_dir = (
        Path(args.install_dir).resolve()
        if args.install_dir is not None
        else (source_root / ".install").resolve()
    )
    output_dir = Path(args.output_dir).resolve()

    if not install_dir.is_dir():
        print(f"error: install directory not found: {install_dir}", file=sys.stderr)
        return 1

    readme_path = source_root / "README.md"
    if not readme_path.is_file():
        print(f"error: README.md not found at {readme_path}", file=sys.stderr)
        return 1

    install_game_dir = install_dir / "openprey"
    game_dir_name = "openprey"
    if not install_game_dir.is_dir():
        print(
            f"error: game directory not found: {install_game_dir}",
            file=sys.stderr,
        )
        return 1

    output_dir.mkdir(parents=True, exist_ok=True)

    package_stem = f"openprey-{args.version_tag}-{args.platform}"
    package_root = output_dir / package_stem
    if package_root.exists():
        shutil.rmtree(package_root)
    package_root.mkdir(parents=True, exist_ok=True)

    shutil.copy2(readme_path, package_root / "README.md")
    copy_required_binaries(args.platform, args.arch, install_dir, package_root)

    game_package_dir = package_root / game_dir_name
    game_package_dir.mkdir(parents=True, exist_ok=True)
    game_pk4_name = f"openprey-{game_dir_name}-{args.version_tag}.pk4"
    game_pk4_path = game_package_dir / game_pk4_name

    added_files, skipped_samples = create_openprey_pk4(
        install_game_dir, game_pk4_path
    )
    if added_files == 0:
        print(
            f"error: {game_dir_name} pk4 packaging found no eligible files after filtering",
            file=sys.stderr,
        )
        return 1

    zip_path = output_dir / f"{package_stem}.zip"
    create_release_zip(package_root, zip_path)

    print(f"Packaged OpenPrey nightly {args.version} for {args.platform}")
    print(f"Package directory: {package_root}")
    print(f"Release archive: {zip_path}")
    print(f"Game pk4 ({game_dir_name}): {game_pk4_path} ({added_files} files)")
    if skipped_samples:
        print("Filtered sample paths:")
        for rel in skipped_samples:
            print(f"  - {rel}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
