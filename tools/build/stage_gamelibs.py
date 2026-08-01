#!/usr/bin/env python3
"""Stage OpenPrey-game sources with openPREY's current public engine headers."""

from __future__ import annotations

import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path


OPENPREY_SUPPORT_DIRS = (
    "idlib",
    "framework",
    "renderer",
    "ui",
    "sys",
    "bse",
    "MayaImport",
    "cm",
    "sound",
    "tools/compilers",
)

OPENPREY_GAME_SOURCE_DIRS = (
    "game",
    "Prey",
    "preyengine",
)

MANIFEST_NAME = "openprey_gamelibs_stage_manifest.json"


def repo_git_value(root: Path, *args: str) -> str:
    if not (root / ".git").exists():
        return ""

    completed = subprocess.run(
        ["git", "-C", str(root), *args],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    if completed.returncode != 0:
        return ""
    return completed.stdout.strip()


def repo_git_dirty(root: Path) -> bool:
    return bool(repo_git_value(root, "status", "--porcelain"))


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def iter_regular_source_files(source_dir: Path) -> list[Path]:
    files: list[Path] = []
    for path in sorted(source_dir.rglob("*")):
        if path.is_symlink():
            raise RuntimeError(f"refusing to stage symlink: {path}")
        if path.is_dir():
            continue
        if not path.is_file():
            raise RuntimeError(f"refusing to stage non-regular file: {path}")
        files.append(path)
    return files


def copy_regular_tree(source_dir: Path, dest_dir: Path) -> list[Path]:
    if dest_dir.exists():
        shutil.rmtree(dest_dir)

    copied: list[Path] = []
    for source_path in iter_regular_source_files(source_dir):
        rel = source_path.relative_to(source_dir)
        if any(part in ("", ".", "..") for part in rel.parts):
            raise RuntimeError(f"refusing to stage unsafe path: {source_path}")

        dest_path = dest_dir / rel
        dest_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_path, dest_path)
        copied.append(dest_path)
    return copied


def is_relative_to(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
    except ValueError:
        return False
    return True


def validate_stage_root(project_root: Path, gamelibs_root: Path, stage_root: Path) -> None:
    if stage_root.is_symlink():
        raise RuntimeError(f"refusing to stage into symlink: {stage_root}")

    if stage_root.name != "openprey_gamelibs_stage" or stage_root.parent.name != ".tmp":
        raise RuntimeError(f"stage root must end with .tmp/openprey_gamelibs_stage: {stage_root}")

    if is_relative_to(project_root, stage_root) or is_relative_to(gamelibs_root, stage_root):
        raise RuntimeError(f"refusing to stage over source repository: {stage_root}")

    if is_relative_to(stage_root, gamelibs_root):
        raise RuntimeError(f"refusing to stage inside GameLibs source repository: {stage_root}")

    protected_project_roots = (
        project_root / ".git",
        project_root / "src",
        project_root / "content",
        project_root / "tools",
    )
    for protected_root in protected_project_roots:
        if is_relative_to(stage_root, protected_root):
            raise RuntimeError(f"refusing to stage inside openPREY source tree: {stage_root}")


def copy_game_sources(source_game_dir: Path, dest_game_dir: Path) -> list[Path]:
    return copy_regular_tree(source_game_dir, dest_game_dir)


def mirror_support_dir(source_dir: Path, dest_dir: Path) -> list[Path]:
    if dest_dir.exists():
        shutil.rmtree(dest_dir)
    if not source_dir.is_dir():
        return []
    return copy_regular_tree(source_dir, dest_dir)


def mirror_project_support_dirs(project_root: Path, stage_root: Path) -> list[Path]:
    source_root = project_root / "src"
    stage_src_root = stage_root / "src"
    copied: list[Path] = []

    for dir_name in OPENPREY_SUPPORT_DIRS:
        copied += mirror_support_dir(source_root / dir_name, stage_src_root / dir_name)
    return copied


def staged_file_manifest(stage_root: Path, staged_files: list[Path]) -> list[dict[str, str]]:
    entries: list[dict[str, str]] = []
    for path in sorted(staged_files):
        entries.append(
            {
                "path": path.relative_to(stage_root).as_posix(),
                "sha256": file_sha256(path),
            }
        )
    return entries


def write_stage_manifest(project_root: Path, gamelibs_root: Path, stage_root: Path, staged_files: list[Path]) -> None:
    manifest = {
        "format": 1,
        "projectRoot": project_root.as_posix(),
        "projectGitCommit": repo_git_value(project_root, "rev-parse", "--verify", "HEAD"),
        "projectGitDirty": repo_git_dirty(project_root),
        "gameLibsRoot": gamelibs_root.as_posix(),
        "gameLibsGitCommit": repo_git_value(gamelibs_root, "rev-parse", "--verify", "HEAD"),
        "gameLibsGitDirty": repo_git_dirty(gamelibs_root),
        "fileCount": len(staged_files),
        "files": staged_file_manifest(stage_root, staged_files),
    }

    manifest_path = stage_root / MANIFEST_NAME
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def validate_stage_manifest(stage_root: Path) -> None:
    stage_root = stage_root.resolve()
    manifest_path = stage_root / MANIFEST_NAME
    if manifest_path.is_symlink():
        raise RuntimeError(f"stage manifest must not be a symlink: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != 1:
        raise RuntimeError("stage manifest has unsupported format")
    files = manifest.get("files")
    file_count = manifest.get("fileCount")
    if not isinstance(files, list) or not isinstance(file_count, int) or isinstance(file_count, bool) or file_count != len(files):
        raise RuntimeError("stage manifest file count is inconsistent")

    for entry in files:
        rel = entry.get("path")
        expected_hash = entry.get("sha256")
        if not isinstance(rel, str) or not isinstance(expected_hash, str):
            raise RuntimeError("stage manifest contains malformed file entry")
        if re.fullmatch(r"[0-9a-f]{64}", expected_hash) is None:
            raise RuntimeError(f"stage manifest contains malformed sha256 for {rel}")

        rel_path = Path(rel)
        if not rel_path.parts or rel_path.is_absolute() or any(part in ("", ".", "..") for part in rel_path.parts):
            raise RuntimeError(f"stage manifest contains unsafe path: {rel}")

        path = (stage_root / rel_path).resolve()
        if not is_relative_to(path, stage_root):
            raise RuntimeError(f"stage manifest path escapes stage root: {rel}")
        if path.is_symlink() or not path.is_file():
            raise RuntimeError(f"stage manifest references missing file: {rel}")
        actual_hash = file_sha256(path)
        if actual_hash != expected_hash:
            raise RuntimeError(f"stage manifest hash mismatch for {rel}")


def prepare_stage_root(stage_root: Path) -> None:
    if stage_root.exists():
        shutil.rmtree(stage_root)
    stage_root.mkdir(parents=True, exist_ok=True)


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(
            "usage: stage_gamelibs.py <project-root> <gamelibs-root> <stage-root>",
            file=sys.stderr,
        )
        return 2

    raw_project_root = Path(argv[1])
    raw_gamelibs_root = Path(argv[2])
    if raw_project_root.is_symlink():
        print(f"error: openPREY root must not be a symlink: {raw_project_root}", file=sys.stderr)
        return 1
    if raw_gamelibs_root.is_symlink():
        print(f"error: GameLibs root must not be a symlink: {raw_gamelibs_root}", file=sys.stderr)
        return 1

    project_root = raw_project_root.resolve()
    gamelibs_root = raw_gamelibs_root.resolve()
    raw_stage_root = Path(argv[3])
    if raw_stage_root.is_symlink():
        print(f"error: refusing to stage into symlink: {raw_stage_root}", file=sys.stderr)
        return 1
    stage_root = raw_stage_root.resolve()

    source_game_dirs = {
        dir_name: gamelibs_root / "src" / dir_name
        for dir_name in OPENPREY_GAME_SOURCE_DIRS
    }
    for module_name, source_game_dir in source_game_dirs.items():
        if not source_game_dir.is_dir():
            print(f"error: {module_name} source directory not found: {source_game_dir}", file=sys.stderr)
            return 1

    if not project_root.is_dir():
        print(f"error: openPREY root not found: {project_root}", file=sys.stderr)
        return 1

    try:
        validate_stage_root(project_root, gamelibs_root, stage_root)
        prepare_stage_root(stage_root)
        staged_files = []
        for module_name, source_game_dir in source_game_dirs.items():
            staged_files += copy_game_sources(source_game_dir, stage_root / "src" / module_name)
        staged_files += mirror_project_support_dirs(project_root, stage_root)
        write_stage_manifest(project_root, gamelibs_root, stage_root, staged_files)
        validate_stage_manifest(stage_root)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(stage_root.as_posix())
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
