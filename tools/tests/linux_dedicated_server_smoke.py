#!/usr/bin/env python3
"""Run a deterministic, asset-free Linux dedicated-server startup smoke."""

from __future__ import annotations

import argparse
import json
import os
import platform
import subprocess
import sys
import tempfile
import time
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
READY_MARKER = "OPENPREY_DEDICATED_SMOKE_READY"
REQUIRED_MARKERS = (
    "Selected game module: logical='game'",
    "------------- Initializing Game -------------",
    "game initialized.",
    READY_MARKER,
    "--- Common Initialization Complete ---",
    "Type 'help' for dedicated server info.",
    "--------------- Game Shutdown ---------------",
)
FATAL_MARKERS = (
    "ERROR:",
    "Error during initialization",
    "couldn't load game dynamic library",
    "couldn't find game DLL API",
    "wrong game DLL API version",
)


def host_arch_tag() -> str:
    machine = platform.machine().lower()
    if machine in {"x86_64", "amd64"}:
        return "x64"
    if machine in {"aarch64", "arm64"}:
        return "arm64"
    raise RuntimeError(f"unsupported Linux smoke host architecture: {machine or '<empty>'}")


def create_minimal_base(base_path: Path) -> Path:
    prey_base = base_path / "base"
    prey_base.mkdir(parents=True, exist_ok=False)
    pak_path = prey_base / "pak000.pk4"
    with zipfile.ZipFile(pak_path, "w", compression=zipfile.ZIP_STORED) as archive:
        archive.writestr(
            "openprey-dedicated-smoke.txt",
            "Generated test-only media marker; contains no proprietary Prey data.\n",
        )
    return pak_path


def read_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def tail(text: str, line_count: int = 80) -> str:
    return "\n".join(text.splitlines()[-line_count:])


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--install-root",
        type=Path,
        default=ROOT / ".install",
        help="Staged package root containing the Linux dedicated server and basepr module.",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=ROOT / ".tmp" / "linux-dedicated-smoke",
        help="Parent directory for isolated runtime homes and diagnostic reports.",
    )
    parser.add_argument("--executable", type=Path, default=None, help="Override the staged dedicated-server executable.")
    parser.add_argument("--arch", choices=("x64", "arm64"), default=None, help="Override host architecture detection.")
    parser.add_argument("--timeout", type=float, default=45.0, help="Maximum runtime in seconds.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if sys.platform != "linux":
        raise RuntimeError("linux_dedicated_server_smoke must run natively on Linux")
    if args.timeout <= 0:
        raise RuntimeError("--timeout must be greater than zero")

    arch = args.arch or host_arch_tag()
    install_root = args.install_root.resolve()
    executable = (args.executable or install_root / f"openPREY-ded_{arch}").resolve()
    game_module = install_root / "basepr" / f"game_{arch}.so"

    if not executable.is_file():
        raise RuntimeError(f"dedicated-server executable not found: {executable}")
    if not os.access(executable, os.X_OK):
        raise RuntimeError(f"dedicated-server executable is not executable: {executable}")
    if not game_module.is_file():
        raise RuntimeError(f"unified game module not found: {game_module}")

    packaged_retail_base = install_root / "base"
    if packaged_retail_base.is_dir() and any(path.is_file() for path in packaged_retail_base.rglob("*")):
        raise RuntimeError(
            f"refusing assetless smoke because the staged package contains retail base files: {packaged_retail_base}"
        )

    args.output_root.mkdir(parents=True, exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix=f"{arch}-", dir=args.output_root.resolve()))
    base_path = run_dir / "minimal-base"
    home_path = run_dir / "home"
    home_path.mkdir()
    minimal_pak = create_minimal_base(base_path)
    stdout_path = run_dir / "stdout.txt"
    stderr_path = run_dir / "stderr.txt"
    report_path = run_dir / "report.json"

    command = [
        str(executable),
        "+set", "fs_basepath", str(base_path),
        "+set", "fs_homepath", str(home_path),
        "+set", "fs_savepath", str(home_path),
        "+set", "fs_devpath", str(install_root),
        "+set", "fs_game", "basepr",
        "+set", "fs_validateOfficialPaks", "0",
        "+set", "g_allowAssetlessStartup", "1",
        "+set", "si_gameType", "deathmatch",
        "+set", "s_noSound", "1",
        "+set", "net_serverDedicated", "1",
        "+set", "logFile", "2",
        "+set", "logFileName", "logs/dedicated-smoke.log",
        "+echo", READY_MARKER,
        "+com_activeGameModule",
        "+wait", "1",
        "+quit",
    ]

    environment = os.environ.copy()
    environment["HOME"] = str(home_path)
    environment["XDG_CONFIG_HOME"] = str(home_path / ".config")
    environment["XDG_DATA_HOME"] = str(home_path / ".local" / "share")

    started = time.monotonic()
    timed_out = False
    exit_code: int | None = None
    stdout = ""
    stderr = ""
    try:
        completed = subprocess.run(
            command,
            cwd=install_root,
            env=environment,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=args.timeout,
            check=False,
        )
        exit_code = completed.returncode
        stdout = completed.stdout
        stderr = completed.stderr
    except subprocess.TimeoutExpired as exc:
        timed_out = True
        stdout = exc.stdout.decode("utf-8", errors="replace") if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        stderr = exc.stderr.decode("utf-8", errors="replace") if isinstance(exc.stderr, bytes) else (exc.stderr or "")

    elapsed = time.monotonic() - started
    stdout_path.write_text(stdout, encoding="utf-8")
    stderr_path.write_text(stderr, encoding="utf-8")

    log_candidates = sorted(home_path.rglob("dedicated-smoke.log"))
    log_path = log_candidates[0] if log_candidates else None
    log_text = read_text(log_path) if log_path else ""
    diagnostic_text = "\n".join(part for part in (stdout, stderr, log_text) if part)
    missing = [marker for marker in REQUIRED_MARKERS if marker not in diagnostic_text]
    fatal = [marker for marker in FATAL_MARKERS if marker in diagnostic_text]
    passed = exit_code == 0 and not timed_out and log_path is not None and not missing and not fatal

    report = {
        "status": "pass" if passed else "fail",
        "architecture": arch,
        "executable": str(executable),
        "gameModule": str(game_module),
        "minimalPak": str(minimal_pak),
        "log": str(log_path) if log_path else "",
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "exitCode": exit_code,
        "timedOut": timed_out,
        "elapsedSeconds": round(elapsed, 3),
        "missingMarkers": missing,
        "fatalMarkers": fatal,
        "command": command,
    }
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(f"linux_dedicated_server_smoke: {report['status']} ({arch}, {elapsed:.2f}s)")
    print(f"  report: {report_path}")
    if not passed:
        print(f"  exitCode={exit_code} timedOut={int(timed_out)} logFound={int(log_path is not None)}")
        if missing:
            print("  missing markers:")
            for marker in missing:
                print(f"    - {marker}")
        if fatal:
            print("  fatal markers:")
            for marker in fatal:
                print(f"    - {marker}")
        if diagnostic_text:
            print("  diagnostic tail:")
            for line in tail(diagnostic_text).splitlines():
                print(f"    {line}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
