#!/usr/bin/env python3
"""Load every canonical Prey map, write per-map conDumps, and audit issues.

The runner intentionally uses the engine's own console dump command.  Each map
gets an isolated fs_savepath beneath .tmp so logs, generated config files, and
conDumps do not contaminate the normal development runtime.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import os
import platform
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = ROOT / ".vscode" / "prey-maps.json"
DEFAULT_INSTALL_ROOT = ROOT / ".install"
DEFAULT_AUDIT_PATH = ROOT / "docs" / "dev" / "prey-rebase" / "map-load-condump-audit.md"


ISSUE_PATTERNS: tuple[re.Pattern[str], ...] = (
    re.compile(r"\b(?:WARNING|Warning|warning)\b"),
    re.compile(r"\b(?:ERROR|Error|error)\b"),
    re.compile(r"\b(?:FATAL|Fatal|fatal)\b"),
    re.compile(r"\b(?:ASSERT|Assert|assert)\b"),
    re.compile(r"\b(?:couldn'?t|Couldn'?t|COULDN'?T)\b"),
    re.compile(r"\b(?:could not|Could not|COULD NOT)\b"),
    re.compile(r"\b(?:can'?t|Can'?t|CAN'?T)\b"),
    re.compile(r"\b(?:failed|Failed|FAILED)\b"),
    re.compile(r"\b(?:missing|Missing|MISSING)\b"),
    re.compile(r"\b(?:not found|Not found|NOT FOUND)\b"),
    re.compile(r"\b(?:unknown|Unknown|UNKNOWN)\b"),
    re.compile(r"\b(?:script error|Script error|SCRIPT ERROR)\b"),
    re.compile(r"\b(?:bad token|Bad token|BAD TOKEN)\b"),
)

NOISE_PATTERNS: tuple[re.Pattern[str], ...] = (
    re.compile(r"^WARNING: using hardcoded default base path$", re.IGNORECASE),
)

WARNING_REPLAY_HEADER = re.compile(r"^-+ Warnings -+$", re.IGNORECASE)
WARNING_REPLAY_FOOTER = re.compile(r"^(?:\d+|more than \d+) warnings!?\.?$", re.IGNORECASE)

# These strings come from invalid 3DS Max authoring metadata embedded in the
# shipped Prey ASE models.  They are not failed VFS opens: Model_ase passes the
# metadata to OSPathToRelativePath while deriving a material name, then keeps
# the retail-compatible empty material when conversion is impossible.
KNOWN_RETAIL_ASE_PATH_DIAGNOSTICS = frozenset(
    {
        "warning: idfilesystem::ospathtorelativepath failed on",
        "warning: idfilesystem::ospathtorelativepath failed on c:/documents",
        "warning: idfilesystem::ospathtorelativepath failed on cage_arm.tga",
        "warning: idfilesystem::ospathtorelativepath failed on cage_bas.tga",
        "warning: idfilesystem::ospathtorelativepath failed on stone_70.jpg",
    }
)

# Exact mixed-case model roots authored into the shipped pak_data.pk4 and its
# retail map/model references.  The archive is immutable.  Keep this allowlist
# exact so new mixed-case paths in repo-authored or third-party content remain
# actionable portability warnings.
KNOWN_RETAIL_UPPERCASE_ASSET_PATHS = frozenset(
    {
        "<game-root>/models/mapobjects/alien_roadhouse",
        "<game-root>/models/mapobjects/deathwalk/ash",
        "<game-root>/models/mapobjects/dm",
        "<game-root>/models/mapobjects/keeper_couch",
        "<game-root>/models/mapobjects/portal_generator",
        "<game-root>/models/mapobjects/portalframe",
        "<game-root>/models/mapobjects/roadhouse/barrel",
        "<game-root>/models/mapobjects/roadhouse/barstool",
        "<game-root>/models/mapobjects/roadhouse/beercase",
        "<game-root>/models/mapobjects/roadhouse/beertap",
        "<game-root>/models/mapobjects/roadhouse/beertap_handle",
        "<game-root>/models/mapobjects/roadhouse/beertap_handle3",
        "<game-root>/models/mapobjects/roadhouse/boxblue",
        "<game-root>/models/mapobjects/roadhouse/boxred",
        "<game-root>/models/mapobjects/roadhouse/extinguisher",
        "<game-root>/models/mapobjects/roadhouse/fan",
        "<game-root>/models/mapobjects/roadhouse/neon_sign",
        "<game-root>/models/mapobjects/roadhouse/truck",
        "<game-root>/models/mapobjects/roadhouse/wastecan",
        "<game-root>/models/mapobjects/salvageboss",
        "<game-root>/models/mapobjects/spindle",
        "<game-root>/models/mapobjects/spiritbridges/biolabs",
        "<game-root>/models/mapobjects/spiritbridges/feedingtowerc",
        "<game-root>/models/mapobjects/spiritbridges/feedingtowerd",
        "<game-root>/models/mapobjects/spiritbridges/girlfriendx",
        "<game-root>/models/mapobjects/spiritbridges/keeperfortress",
        "<game-root>/models/mapobjects/spiritbridges/salvage",
        "<game-root>/models/mapobjects/spiritbridges/salvageboss",
        "<game-root>/models/mapobjects/spiritbridges/schoolbus",
        "<game-root>/models/mapobjects/spiritbridges/spindlea",
        "<game-root>/models/mapobjects/spiritbridges/spindleb",
        "<game-root>/models/mapobjects/tubes",
        "<game-root>/models/monsters/hunter/gibs",
    }
)


@dataclass(frozen=True)
class MapEntry:
    name: str
    kind: str
    map_name: str


@dataclass
class IssueOccurrence:
    map_name: str
    source: str
    line_number: int


@dataclass
class Issue:
    signature: str
    severity: str
    representative: str
    occurrences: list[IssueOccurrence] = field(default_factory=list)

    @property
    def maps(self) -> list[str]:
        return sorted({occ.map_name for occ in self.occurrences})


@dataclass
class IssueFamily:
    name: str
    severity: str
    issue_signatures: set[str] = field(default_factory=set)
    maps_seen: set[str] = field(default_factory=set)
    occurrences: int = 0
    examples: list[str] = field(default_factory=list)


@dataclass
class MapResult:
    entry: MapEntry
    safe_id: str
    savepath: Path
    stdout_path: Path
    stderr_path: Path
    log_path: Path | None
    condump_path: Path | None
    exit_code: int | None
    timed_out: bool
    elapsed_seconds: float
    issue_ids: list[str] = field(default_factory=list)
    map_load_completed: bool = False
    map_load_completion: str = ""
    map_load_failed: bool = False
    map_load_failure: str = ""

    @property
    def status(self) -> str:
        if self.timed_out:
            return "timeout"
        if self.exit_code is None:
            return "unknown exit"
        if self.exit_code != 0:
            return f"exit {self.exit_code}"
        if self.condump_path is None:
            return "missing conDump"
        if self.map_load_failed:
            return "map load failed"
        if not self.map_load_completed:
            return "map load incomplete"
        return "ok"


def rel(path: Path | None) -> str:
    if path is None:
        return ""
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def md_escape(value: str) -> str:
    return value.replace("\\", "\\\\").replace("|", "\\|").replace("\n", " ").strip()


def code(value: str) -> str:
    escaped = value.replace("`", "\\`")
    return f"`{escaped}`"


def append_set(args: list[str], name: str, value: Any) -> None:
    args.extend(("+set", name, str(value)))


def append_command(args: list[str], command: str, *values: Any) -> None:
    args.append("+" + command)
    args.extend(str(value) for value in values)


def safe_map_id(map_name: str) -> str:
    safe = re.sub(r"[^A-Za-z0-9_.-]+", "_", map_name.strip().replace("\\", "/"))
    safe = safe.strip("._")
    return safe or "map"


def load_manifest(path: Path) -> list[MapEntry]:
    data = json.loads(path.read_text(encoding="utf-8"))
    maps = data.get("maps")
    if not isinstance(maps, list):
        raise RuntimeError(f"{path} does not contain a maps array")

    entries: list[MapEntry] = []
    seen: set[str] = set()
    for item in maps:
        if not isinstance(item, dict):
            raise RuntimeError(f"{path} contains a non-object map entry: {item!r}")
        name = item.get("name")
        kind = item.get("kind")
        map_name = item.get("map")
        if not isinstance(name, str) or not name.strip():
            raise RuntimeError(f"{path} contains a map entry without a name")
        if kind not in ("sp", "mp"):
            raise RuntimeError(f"{path} contains unsupported map kind for {name!r}: {kind!r}")
        if not isinstance(map_name, str) or not map_name.strip():
            raise RuntimeError(f"{path} contains a map entry without a map path: {name!r}")
        if map_name in seen:
            raise RuntimeError(f"{path} repeats map {map_name!r}")
        seen.add(map_name)
        entries.append(MapEntry(name=name, kind=kind, map_name=map_name))
    return entries


def default_executable(install_root: Path) -> Path:
    system = platform.system().lower()
    if system == "windows":
        return install_root / "openPREY-client_x64.exe"
    return install_root / "openPREY-client_x64"


def detect_basepath() -> str:
    env = os.environ.get("OPENPREY_PREY_BASEPATH") or os.environ.get("PREY_BASEPATH")
    if env and Path(env).exists():
        return str(Path(env).resolve())

    candidates: list[Path] = []
    if platform.system().lower() == "windows":
        candidates.extend(
            Path(p)
            for p in (
                r"C:\Program Files (x86)\Human Head Studios\Prey",
                r"C:\Program Files\Human Head Studios\Prey",
                r"C:\Program Files (x86)\R.G. Mechanics\Prey",
                r"C:\Program Files\R.G. Mechanics\Prey",
                r"D:\Games\Prey",
                r"E:\Games\Prey",
            )
        )
    else:
        candidates.extend(
            Path(p)
            for p in (
                "/usr/local/games/prey",
                "/usr/local/share/prey",
                "/opt/prey",
            )
        )

    for candidate in candidates:
        base_dir = candidate / "base"
        if not base_dir.is_dir():
            continue
        for pak_name in ("pak000.pk4", "pak001.pk4", "pak_data.pk4", "pak_sound.pk4", "pak_en_v.pk4", "pak_en_t.pk4"):
            if (base_dir / pak_name).is_file():
                return str(candidate.resolve())
    return ""


def write_post_load_cfg(savepath: Path, safe_id: str, settle_ms: int) -> str:
    cfg_rel = f"map-load-audit/{safe_id}.cfg"
    condump_rel = f"logs/condumps/{safe_id}.txt"
    cfg_path = savepath / "basepr" / Path(cfg_rel.replace("/", os.sep))
    cfg_path.parent.mkdir(parents=True, exist_ok=True)
    (savepath / "basepr" / "logs" / "condumps").mkdir(parents=True, exist_ok=True)
    cfg_path.write_text(
        "\n".join(
            (
                f"waitMsec {max(0, settle_ms)}",
                f"conDump {condump_rel}",
                "quit",
                "",
            )
        ),
        encoding="utf-8",
    )
    return cfg_rel


def build_args(
    entry: MapEntry,
    *,
    savepath: Path,
    install_root: Path,
    basepath: str,
    renderer_api: str,
    log_name: str,
    post_load_cfg: str,
) -> list[str]:
    args: list[str] = []
    multiple_instance_cvar = "win_allowMultipleInstances" if platform.system().lower() == "windows" else "sys_allowMultipleInstances"
    append_set(args, multiple_instance_cvar, "1")
    append_set(args, "logFile", "2")
    append_set(args, "logFileName", f"logs/{log_name}")
    append_set(args, "developer", "1")
    append_set(args, "r_ignoreGLErrors", "0")
    append_set(args, "r_fullscreen", "0")
    append_set(args, "s_deviceName", "default")
    append_set(args, "com_skipLoadingContinue", "1")
    append_set(args, "com_loadingContinueAutoAdvance", "1")
    append_set(args, "g_autoScreenshot", "0")
    append_set(args, "fs_savepath", str(savepath))
    append_set(args, "fs_devpath", str(install_root))
    append_set(args, "fs_game", "basepr")
    append_set(args, "r_renderApi", renderer_api)
    if basepath:
        append_set(args, "fs_basepath", basepath)

    if entry.kind == "mp":
        append_set(args, "net_serverDedicated", "0")
        append_set(args, "si_gameType", "DM")
        append_set(args, "si_map", entry.map_name)
        append_set(args, "sv_cheats", "1")
        append_command(args, "spawnServer", entry.map_name)
    else:
        append_set(args, "si_gameType", "singleplayer")
        append_command(args, "map", entry.map_name)

    append_command(args, "exec_savepath", post_load_cfg)
    return args


def find_first_existing(candidates: Iterable[Path]) -> Path | None:
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def read_text(path: Path | None) -> str:
    if path is None or not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def strip_console_codes(line: str) -> str:
    line = re.sub(r"\^[0-9A-Za-z]", "", line)
    line = line.replace("\x00", "")
    return line.strip()


def severity_for(line: str) -> str:
    lower = line.lower()
    if "fatal" in lower:
        return "fatal"
    if "assert" in lower:
        return "assert"
    if "error" in lower or "script error" in lower:
        return "error"
    if "warning" in lower:
        return "warning"
    if "failed" in lower or "could" in lower or "can't" in lower:
        return "failure"
    if "missing" in lower or "not found" in lower or "unknown" in lower:
        return "missing"
    return "notice"


def issue_line(raw_line: str) -> bool:
    line = strip_console_codes(raw_line)
    if not line:
        return False
    if any(pattern.search(line) for pattern in NOISE_PATTERNS):
        return False
    return any(pattern.search(line) for pattern in ISSUE_PATTERNS)


def normalize_issue(line: str, *, run_root: Path, basepath: str) -> str:
    normalized = strip_console_codes(line)
    replacements = {
        str(run_root.resolve()): "<audit-root>",
        str(ROOT.resolve()): "<repo-root>",
        str((ROOT / ".install").resolve()): "<install-root>",
    }
    if basepath:
        replacements[str(Path(basepath).resolve())] = "<prey-root>"

    for source, target in replacements.items():
        normalized = normalized.replace(source, target)
        normalized = normalized.replace(source.replace("\\", "/"), target)
    normalized = normalized.replace("\\", "/")
    normalized = re.sub(
        r"(?i)(non-portable: path contains uppercase characters: )(?:basepr|base)/",
        r"\1<game-root>/",
        normalized,
    )
    normalized = re.sub(r"\b\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:\.\d+)?\b", "<timestamp>", normalized)
    normalized = re.sub(r"\b0x[0-9a-fA-F]{6,16}\b", "<hex>", normalized)
    normalized = re.sub(r"\s+", " ", normalized).strip()
    return normalized


def source_issue_lines(
    text: str,
    *,
    source_name: str,
    map_name: str,
    run_root: Path,
    basepath: str,
) -> Iterable[tuple[str, str, str, IssueOccurrence]]:
    candidates: list[tuple[str, str, str, IssueOccurrence, bool]] = []
    in_warning_replay = False
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        representative = strip_console_codes(raw_line)
        if WARNING_REPLAY_HEADER.fullmatch(representative):
            in_warning_replay = True
            continue
        if in_warning_replay and WARNING_REPLAY_FOOTER.fullmatch(representative):
            in_warning_replay = False
            continue
        if not issue_line(representative):
            continue
        signature = normalize_issue(representative, run_root=run_root, basepath=basepath)
        if not signature:
            continue
        candidates.append(
            (
                signature,
                severity_for(representative),
                representative,
                IssueOccurrence(map_name=map_name, source=source_name, line_number=line_number),
                in_warning_replay,
            )
        )

    # Full logs contain the original warning and Common::PrintWarnings replay.
    # A fallback conDump may contain only the replay, so retain replay signatures
    # there only when no direct occurrence supplied the same evidence.
    direct_signatures = {candidate[0] for candidate in candidates if not candidate[4]}
    for signature, severity, representative, occurrence, is_replay in candidates:
        if is_replay and (source_name != "conDump" or signature in direct_signatures):
            continue
        yield signature, severity, representative, occurrence


def select_audit_sources(result: MapResult) -> list[tuple[str, str]]:
    """Return sources for issue extraction.

    The engine log is preferred because conDump wraps long console lines to the
    visible console width.  The conDump is still generated and linked as the raw
    evidence for each map; it is only used for extraction when a log was not
    produced.  Process streams are supplemental: collect_issues ignores a
    signature there when the canonical log/conDump already supplied it.
    """

    sources: list[tuple[str, str]] = []
    log_text = read_text(result.log_path)
    if log_text.strip():
        sources.append(("log", log_text))
    else:
        condump_text = read_text(result.condump_path)
        if condump_text.strip():
            sources.append(("conDump", condump_text))

    stderr_text = read_text(result.stderr_path)
    if stderr_text.strip():
        sources.append(("stderr", stderr_text))
    stdout_text = read_text(result.stdout_path)
    if stdout_text.strip():
        sources.append(("stdout", stdout_text))
    return sources


def select_map_load_sources(result: MapResult) -> list[tuple[str, str]]:
    """Return every available source that can prove the target map load."""
    sources: list[tuple[str, str]] = []
    for source_name, path in (
        ("log", result.log_path),
        ("conDump", result.condump_path),
        ("stderr", result.stderr_path),
        ("stdout", result.stdout_path),
    ):
        source_text = read_text(path)
        if source_text.strip():
            sources.append((source_name, source_text))
    return sources


def detect_map_load_state(
    result: MapResult,
    sources: list[tuple[str, str]],
) -> tuple[bool, str, bool, str]:
    expected = f"maps/{result.entry.map_name}.map".replace("\\", "/").lower()
    completion_pattern = re.compile(
        rf"^\s*\d+\s+msec\s+to\s+load\s+{re.escape(result.entry.map_name)}\s*$",
        re.IGNORECASE,
    )
    completion_evidence = ""
    for source_name, source_text in sources:
        for line_number, raw_line in enumerate(source_text.splitlines(), start=1):
            line = strip_console_codes(raw_line)
            normalized = line.replace("\\", "/").lower()
            if "can't find map" in normalized and expected in normalized:
                return False, completion_evidence, True, f"{source_name}:{line_number} {line}"
            if "couldn't load map" in normalized and result.entry.map_name.lower() in normalized:
                return False, completion_evidence, True, f"{source_name}:{line_number} {line}"
            if "failed to load map" in normalized and result.entry.map_name.lower() in normalized:
                return False, completion_evidence, True, f"{source_name}:{line_number} {line}"
            if not completion_evidence and completion_pattern.fullmatch(line.replace("\\", "/")):
                completion_evidence = f"{source_name}:{line_number} {line}"
    return bool(completion_evidence), completion_evidence, False, ""


def collect_issues(results: list[MapResult], *, run_root: Path, basepath: str) -> dict[str, Issue]:
    issues: dict[str, Issue] = {}
    for result in results:
        sources = select_audit_sources(result)
        (
            result.map_load_completed,
            result.map_load_completion,
            result.map_load_failed,
            result.map_load_failure,
        ) = detect_map_load_state(result, select_map_load_sources(result))

        seen_for_map: set[str] = set()
        seen_in_prior_sources: set[str] = set()
        for source_name, source_text in sources:
            source_signatures: set[str] = set()
            for signature, _detected_severity, representative, occurrence in source_issue_lines(
                source_text,
                source_name=source_name,
                map_name=result.entry.map_name,
                run_root=run_root,
                basepath=basepath,
            ):
                if signature in seen_in_prior_sources:
                    continue
                issue = issues.get(signature)
                if issue is None:
                    _family, canonical_severity = classify_issue_family(signature)
                    issue = Issue(
                        signature=signature,
                        severity=canonical_severity,
                        representative=representative,
                    )
                    issues[signature] = issue
                issue.occurrences.append(occurrence)
                seen_for_map.add(signature)
                source_signatures.add(signature)
            seen_in_prior_sources.update(source_signatures)
        result.issue_ids = sorted(seen_for_map)
    return issues


def classify_issue_family(signature: str) -> tuple[str, str]:
    lower = signature.lower()
    if "can't find map maps/" in lower or "couldn't load map" in lower or "failed to load map" in lower:
        return "Map file missing / map command failed", "failure"
    if lower.startswith("glprogs/") and "file not found" in lower:
        return "Missing renderer program file", "missing"
    if "gl_" in lower and "not found" in lower:
        return "Unavailable OpenGL extension probe", "notice"
    if "couldn't load aas file" in lower:
        return "Missing AAS navigation file", "warning"
    if "loading non pre-cached" in lower:
        return "Non-precached decl load", "warning"
    uppercase_prefix = "warning: non-portable: path contains uppercase characters: "
    if lower.startswith(uppercase_prefix):
        asset_path = lower[len(uppercase_prefix) :]
        if asset_path in KNOWN_RETAIL_UPPERCASE_ASSET_PATHS:
            return "Uppercase retail asset path diagnostic", "notice"
        return "Uppercase asset path warning", "warning"
    if "couldn't load image" in lower or "couldn't load cube image" in lower:
        return "Image/material dependency load warning", "warning"
    if "couldn't load sound" in lower or "loadogg(" in lower:
        return "Sound asset/decode warning", "warning"
    if "bad deform type" in lower or "material '" in lower:
        return "Material declaration warning", "warning"
    if lower in KNOWN_RETAIL_ASE_PATH_DIAGNOSTICS:
        return "Stock ASE authoring-path diagnostic", "notice"
    if "ospathtorelativepath failed" in lower:
        return "Filesystem relative-path warning", "warning"
    if (lower.startswith("missing '") or " missing '" in lower) and " animation on " in lower:
        return "Missing animation on entity", "missing"
    if "rigid body" in lower or "idphysics_rigidbody::setclipmodel" in lower:
        return "Physics spawn warning", "warning"
    if "convertlwotomodelsurfaces" in lower:
        return "Model conversion warning", "warning"
    return "Other issue-like console line", severity_for(signature)


def build_issue_families(issues: dict[str, Issue]) -> list[IssueFamily]:
    families: dict[str, IssueFamily] = {}
    for signature, issue in issues.items():
        family_name, family_severity = classify_issue_family(signature)
        family = families.get(family_name)
        if family is None:
            family = IssueFamily(name=family_name, severity=family_severity)
            families[family_name] = family
        family.issue_signatures.add(signature)
        family.maps_seen.update(issue.maps)
        family.occurrences += len(issue.occurrences)
        if len(family.examples) < 3:
            family.examples.append(signature)

    severity_order = {
        "fatal": 0,
        "assert": 1,
        "error": 2,
        "failure": 3,
        "missing": 4,
        "warning": 5,
        "notice": 6,
    }
    return sorted(
        families.values(),
        key=lambda family: (
            severity_order.get(family.severity, 99),
            -len(family.maps_seen),
            -len(family.issue_signatures),
            family.name.lower(),
        ),
    )


def issue_sort_key(item: tuple[str, Issue]) -> tuple[int, int, str]:
    severity_order = {
        "fatal": 0,
        "assert": 1,
        "error": 2,
        "failure": 3,
        "missing": 4,
        "warning": 5,
        "notice": 6,
    }
    signature, issue = item
    return (severity_order.get(issue.severity, 99), -len(issue.maps), signature.lower())


def issue_is_actionable(issue: Issue) -> bool:
    return issue.severity != "notice"


def format_maps(maps: list[str], total_maps: int) -> str:
    if len(maps) == total_maps:
        return f"all {total_maps} maps"
    if len(maps) <= 8:
        return ", ".join(maps)
    head = ", ".join(maps[:8])
    return f"{head}, … (+{len(maps) - 8})"


def write_json_summary(
    path: Path,
    *,
    results: list[MapResult],
    issues: dict[str, Issue],
    args: argparse.Namespace,
    run_root: Path,
    started_at: str,
    finished_at: str,
) -> None:
    sorted_issue_items = sorted(issues.items(), key=issue_sort_key)
    issue_ids = {signature: f"U{index:03d}" for index, (signature, _issue) in enumerate(sorted_issue_items, start=1)}
    families = build_issue_families(issues)
    actionable_count = sum(1 for issue in issues.values() if issue_is_actionable(issue))
    notice_count = len(issues) - actionable_count
    payload = {
        "startedAt": started_at,
        "finishedAt": finished_at,
        "manifest": rel(args.manifest),
        "rendererApi": args.renderer_api,
        "basepath": args.basepath,
        "installRoot": rel(args.install_root),
        "runRoot": rel(run_root),
        "auditPath": rel(args.audit),
        "summary": {
            "uniqueIssueSignatures": len(issues),
            "actionableIssueSignatures": actionable_count,
            "noticeSignatures": notice_count,
        },
        "maps": [
            {
                "name": result.entry.name,
                "kind": result.entry.kind,
                "map": result.entry.map_name,
                "status": result.status,
                "mapLoadCompleted": result.map_load_completed,
                "mapLoadCompletion": result.map_load_completion,
                "mapLoadFailed": result.map_load_failed,
                "mapLoadFailure": result.map_load_failure,
                "exitCode": result.exit_code,
                "timedOut": result.timed_out,
                "elapsedSeconds": round(result.elapsed_seconds, 3),
                "stdout": rel(result.stdout_path),
                "stderr": rel(result.stderr_path),
                "log": rel(result.log_path),
                "conDump": rel(result.condump_path),
                "issues": [issue_ids[signature] for signature in result.issue_ids if signature in issue_ids],
            }
            for result in results
        ],
        "issueFamilies": [
            {
                "name": family.name,
                "severity": family.severity,
                "uniqueIssues": len(family.issue_signatures),
                "maps": sorted(family.maps_seen),
                "occurrences": family.occurrences,
                "examples": family.examples,
            }
            for family in families
        ],
        "issues": [
            {
                "id": issue_ids[signature],
                "severity": issue.severity,
                "maps": issue.maps,
                "occurrences": len(issue.occurrences),
                "signature": signature,
                "representative": issue.representative,
            }
            for signature, issue in sorted_issue_items
        ],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def write_audit_markdown(
    path: Path,
    *,
    results: list[MapResult],
    issues: dict[str, Issue],
    args: argparse.Namespace,
    run_root: Path,
    started_at: str,
    finished_at: str,
) -> None:
    sorted_issue_items = sorted(issues.items(), key=issue_sort_key)
    issue_ids = {signature: f"U{index:03d}" for index, (signature, _issue) in enumerate(sorted_issue_items, start=1)}
    families = build_issue_families(issues)
    total_maps = len(results)
    ok_count = sum(1 for result in results if result.status == "ok")
    timeout_count = sum(1 for result in results if result.timed_out)
    missing_condumps = sum(1 for result in results if result.condump_path is None)
    nonzero = sum(1 for result in results if result.exit_code not in (0, None))
    unknown_exits = sum(1 for result in results if result.exit_code is None and not result.timed_out)
    completed_loads = sum(1 for result in results if result.map_load_completed)
    map_load_failures = sum(1 for result in results if result.map_load_failed)
    unverified_loads = sum(
        1 for result in results if not result.map_load_completed and not result.map_load_failed
    )
    actionable_count = sum(1 for issue in issues.values() if issue_is_actionable(issue))
    notice_count = len(issues) - actionable_count

    lines: list[str] = []
    lines.append("# Prey map load conDump audit")
    lines.append("")
    lines.append(f"- Generated: {finished_at}")
    lines.append(f"- Started: {started_at}")
    lines.append(f"- Map manifest: {code(rel(args.manifest))}")
    lines.append(f"- Renderer API: {code(args.renderer_api)}")
    lines.append(f"- Retail asset root: {code(args.basepath or '(engine auto-discovery)')}")
    lines.append(f"- openPREY install root: {code(rel(args.install_root))}")
    lines.append(f"- Raw audit output: {code(rel(run_root))}")
    lines.append(f"- Per-map timeout: {args.timeout:.0f}s")
    lines.append(f"- Post-load settle time before conDump: {args.settle_ms}ms")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append(f"- Maps attempted: {total_maps}")
    lines.append(f"- Maps with conDumps: {total_maps - missing_condumps}")
    lines.append(f"- Map-command loads that completed: {ok_count}")
    lines.append(f"- Positive target-map completion markers: {completed_loads}")
    lines.append(f"- Process exits with conDumps and verified map loads: {ok_count}")
    lines.append(f"- Detected map-load failures: {map_load_failures}")
    lines.append(f"- Missing positive target-map completion markers: {unverified_loads}")
    lines.append(f"- Timeouts: {timeout_count}")
    lines.append(f"- Non-zero process exits: {nonzero}")
    lines.append(f"- Unknown process exit statuses: {unknown_exits}")
    lines.append(f"- Unique issue signatures: {len(issues)}")
    lines.append(f"- Actionable issue signatures: {actionable_count}")
    lines.append(f"- Known notice signatures: {notice_count}")
    lines.append("")

    if timeout_count or missing_condumps or nonzero or unknown_exits or map_load_failures or unverified_loads:
        lines.append("## Load failures / incomplete dumps")
        lines.append("")
        lines.append("| Map | Kind | Status | Exit | Time | Completion/failure evidence | conDump | Log |")
        lines.append("| --- | --- | --- | ---: | ---: | --- | --- | --- |")
        for result in results:
            if result.status == "ok":
                continue
            lines.append(
                "| "
                + " | ".join(
                    (
                        md_escape(result.entry.map_name),
                        result.entry.kind,
                        md_escape(result.status),
                        "" if result.exit_code is None else str(result.exit_code),
                        f"{result.elapsed_seconds:.1f}s",
                        code(md_escape(result.map_load_failure or result.map_load_completion))
                        if result.map_load_failure or result.map_load_completion
                        else code(f"missing: msec to load {result.entry.map_name}"),
                        code(rel(result.condump_path)) if result.condump_path else "",
                        code(rel(result.log_path)) if result.log_path else "",
                    )
                )
                + " |"
            )
        lines.append("")

    lines.append("## Issue families")
    lines.append("")
    if families:
        lines.append("| Family | Severity | Unique issues | Maps | Occurrences | Examples |")
        lines.append("| --- | --- | ---: | --- | ---: | --- |")
        for family in families:
            examples = "; ".join(code(md_escape(example)) for example in family.examples)
            lines.append(
                "| "
                + " | ".join(
                    (
                        md_escape(family.name),
                        family.severity,
                        str(len(family.issue_signatures)),
                        md_escape(format_maps(sorted(family.maps_seen), total_maps)),
                        str(family.occurrences),
                        examples,
                    )
                )
                + " |"
            )
    else:
        lines.append("No issue-like lines were found in the collected logs/conDumps.")
    lines.append("")

    lines.append("## Unique issues")
    lines.append("")
    if issues:
        lines.append("| ID | Severity | Maps | Occurrences | Representative issue |")
        lines.append("| --- | --- | --- | ---: | --- |")
        for signature, issue in sorted_issue_items:
            lines.append(
                "| "
                + " | ".join(
                    (
                        issue_ids[signature],
                        issue.severity,
                        md_escape(format_maps(issue.maps, total_maps)),
                        str(len(issue.occurrences)),
                        code(md_escape(signature)),
                    )
                )
                + " |"
            )
    else:
        lines.append("No issue-like lines were found in the collected conDumps/logs.")
    lines.append("")

    lines.append("## Per-map results")
    lines.append("")
    lines.append("| Map | Kind | Status | Time | Issues | conDump | Log |")
    lines.append("| --- | --- | --- | ---: | --- | --- | --- |")
    for result in results:
        result_issue_ids = [issue_ids[signature] for signature in result.issue_ids if signature in issue_ids]
        issue_text = ", ".join(result_issue_ids) if result_issue_ids else ""
        lines.append(
            "| "
            + " | ".join(
                (
                    md_escape(result.entry.map_name),
                    result.entry.kind,
                    md_escape(result.status),
                    f"{result.elapsed_seconds:.1f}s",
                    md_escape(issue_text),
                    code(rel(result.condump_path)) if result.condump_path else "",
                    code(rel(result.log_path)) if result.log_path else "",
                )
            )
            + " |"
        )
    lines.append("")

    lines.append("## Reproduction")
    lines.append("")
    lines.append("From the repository root:")
    lines.append("")
    lines.append("```powershell")
    basepath_part = f' --basepath "{args.basepath}"' if args.basepath else ' --basepath ""'
    lines.append(
        f'py -3 tools\\validation\\prey_map_load_condump_audit.py --renderer-api {args.renderer_api}{basepath_part}'
    )
    lines.append("```")
    lines.append("")
    lines.append("Every launch is forced windowed with `+set r_fullscreen 0` and uses the engine `conDump` command.")
    lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def launch_map(
    entry: MapEntry,
    *,
    index: int,
    total: int,
    args: argparse.Namespace,
    run_root: Path,
) -> MapResult:
    safe_id = f"{index:02d}_{safe_map_id(entry.map_name)}"
    savepath = run_root / "savepaths" / safe_id
    savepath.mkdir(parents=True, exist_ok=True)
    log_name = f"{safe_id}.log"
    post_load_cfg = write_post_load_cfg(savepath, safe_id, args.settle_ms)
    launch_args = build_args(
        entry,
        savepath=savepath,
        install_root=args.install_root,
        basepath=args.basepath,
        renderer_api=args.renderer_api,
        log_name=log_name,
        post_load_cfg=post_load_cfg,
    )

    stdout_path = run_root / "stdout" / f"{safe_id}.out.txt"
    stderr_path = run_root / "stderr" / f"{safe_id}.err.txt"
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    stderr_path.parent.mkdir(parents=True, exist_ok=True)
    command_path = run_root / "commands" / f"{safe_id}.json"
    command_path.parent.mkdir(parents=True, exist_ok=True)
    command_path.write_text(
        json.dumps(
            {
                "executable": str(args.executable),
                "cwd": str(args.install_root),
                "args": launch_args,
            },
            indent=2,
        ),
        encoding="utf-8",
    )

    print(f"[{index:02d}/{total:02d}] loading {entry.map_name} ({entry.kind})", flush=True)
    started = time.monotonic()
    exit_code: int | None
    timed_out = False
    creationflags = 0
    if platform.system().lower() == "windows":
        creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout_file, stderr_path.open(
        "w", encoding="utf-8", errors="replace"
    ) as stderr_file:
        proc = subprocess.Popen(
            [str(args.executable), *launch_args],
            cwd=str(args.install_root),
            stdout=stdout_file,
            stderr=stderr_file,
            creationflags=creationflags,
        )
        try:
            exit_code = proc.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            proc.kill()
            try:
                exit_code = proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                exit_code = None

    elapsed = time.monotonic() - started
    log_path = find_first_existing(
        (
            savepath / "basepr" / "logs" / log_name,
            savepath / "base" / "logs" / log_name,
            savepath / "logs" / log_name,
        )
    )
    condump_path = find_first_existing(
        (
            savepath / "basepr" / "logs" / "condumps" / f"{safe_id}.txt",
            savepath / "base" / "logs" / "condumps" / f"{safe_id}.txt",
            savepath / "logs" / "condumps" / f"{safe_id}.txt",
        )
    )
    print(
        f"[{index:02d}/{total:02d}] {entry.map_name}: "
        f"{'timeout' if timed_out else 'exit ' + str(exit_code)}; "
        f"conDump={'yes' if condump_path else 'no'}; {elapsed:.1f}s",
        flush=True,
    )
    return MapResult(
        entry=entry,
        safe_id=safe_id,
        savepath=savepath,
        stdout_path=stdout_path,
        stderr_path=stderr_path,
        log_path=log_path,
        condump_path=condump_path,
        exit_code=exit_code,
        timed_out=timed_out,
        elapsed_seconds=elapsed,
    )


def resolve_report_path(raw: str | None) -> Path | None:
    if not raw:
        return None
    path = Path(raw)
    if not path.is_absolute():
        path = ROOT / path
    return path.resolve()


def find_reused_safe_id(run_root: Path, entry: MapEntry) -> str | None:
    safe_suffix = safe_map_id(entry.map_name)
    savepaths_root = run_root / "savepaths"
    if savepaths_root.is_dir():
        matches = sorted(path.name for path in savepaths_root.glob(f"*_{safe_suffix}") if path.is_dir())
        if matches:
            return matches[0]
    return None


def load_previous_summary(run_root: Path) -> dict[str, dict[str, Any]]:
    summary_path = run_root / "map-load-condump-audit.json"
    if not summary_path.is_file():
        return {}
    data = json.loads(summary_path.read_text(encoding="utf-8"))
    maps = data.get("maps", [])
    if not isinstance(maps, list):
        return {}
    return {item.get("map"): item for item in maps if isinstance(item, dict) and isinstance(item.get("map"), str)}


def reuse_map_result(
    entry: MapEntry,
    *,
    index: int,
    run_root: Path,
    previous_summary: dict[str, dict[str, Any]],
) -> MapResult:
    previous = previous_summary.get(entry.map_name, {})
    safe_id = find_reused_safe_id(run_root, entry)
    if safe_id is None and previous:
        stdout = resolve_report_path(previous.get("stdout"))
        if stdout is not None:
            name = stdout.name
            safe_id = name.removesuffix(".out.txt")
    if safe_id is None:
        safe_id = f"{index:02d}_{safe_map_id(entry.map_name)}"

    savepath = run_root / "savepaths" / safe_id
    stdout_path = resolve_report_path(previous.get("stdout")) or (run_root / "stdout" / f"{safe_id}.out.txt")
    stderr_path = resolve_report_path(previous.get("stderr")) or (run_root / "stderr" / f"{safe_id}.err.txt")
    log_path = resolve_report_path(previous.get("log")) or find_first_existing(
        (
            savepath / "basepr" / "logs" / f"{safe_id}.log",
            savepath / "base" / "logs" / f"{safe_id}.log",
            savepath / "logs" / f"{safe_id}.log",
        )
    )
    condump_path = resolve_report_path(previous.get("conDump")) or find_first_existing(
        (
            savepath / "basepr" / "logs" / "condumps" / f"{safe_id}.txt",
            savepath / "base" / "logs" / "condumps" / f"{safe_id}.txt",
            savepath / "logs" / "condumps" / f"{safe_id}.txt",
        )
    )
    return MapResult(
        entry=entry,
        safe_id=safe_id,
        savepath=savepath,
        stdout_path=stdout_path,
        stderr_path=stderr_path,
        log_path=log_path,
        condump_path=condump_path,
        exit_code=previous.get("exitCode") if previous else None,
        timed_out=bool(previous.get("timedOut", False)) if previous else False,
        elapsed_seconds=float(previous.get("elapsedSeconds", 0.0)) if previous else 0.0,
    )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--install-root", type=Path, default=DEFAULT_INSTALL_ROOT)
    parser.add_argument("--executable", type=Path, default=None)
    parser.add_argument("--basepath", default=None, help="Prey install root containing base/*.pk4. Empty string uses engine discovery.")
    parser.add_argument("--renderer-api", default="gl", choices=("gl", "vulkan", "best", "gl-module"))
    parser.add_argument("--output-root", type=Path, default=None)
    parser.add_argument("--audit", type=Path, default=DEFAULT_AUDIT_PATH)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--settle-ms", type=int, default=2500)
    parser.add_argument("--map", dest="map_filters", action="append", default=[], help="Only run a map path from the manifest. Repeatable.")
    parser.add_argument("--analyze-existing", action="store_true", help="Reuse an existing --output-root and regenerate the audit without launching the game.")
    parser.add_argument("--dry-run", action="store_true")
    ns = parser.parse_args(argv)
    output_root_arg = ns.output_root
    if ns.analyze_existing and output_root_arg is None:
        parser.error("--analyze-existing requires --output-root")

    ns.manifest = ns.manifest.resolve()
    ns.install_root = ns.install_root.resolve()
    ns.executable = (ns.executable or default_executable(ns.install_root)).resolve()
    ns.audit = ns.audit.resolve()
    if output_root_arg is None:
        stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        ns.output_root = (ROOT / ".tmp" / "map-load-condumps" / stamp).resolve()
    else:
        ns.output_root = ns.output_root.resolve()
    if ns.basepath is None:
        ns.basepath = detect_basepath()
    elif ns.basepath:
        ns.basepath = str(Path(ns.basepath).resolve())
    return ns


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if not args.manifest.is_file():
        raise RuntimeError(f"map manifest not found: {args.manifest}")
    if not args.install_root.is_dir():
        raise RuntimeError(f"install root not found: {args.install_root}")
    if not args.executable.is_file():
        raise RuntimeError(f"client executable not found: {args.executable}")
    if args.basepath and not Path(args.basepath).is_dir():
        raise RuntimeError(f"basepath not found: {args.basepath}")

    entries = load_manifest(args.manifest)
    if args.map_filters:
        requested = set(args.map_filters)
        entries = [entry for entry in entries if entry.map_name in requested]
        missing = sorted(requested - {entry.map_name for entry in entries})
        if missing:
            raise RuntimeError(f"map filter(s) not found in manifest: {', '.join(missing)}")
    if not entries:
        raise RuntimeError("no maps selected")

    if args.analyze_existing and not args.output_root.is_dir():
        raise RuntimeError(f"existing output root not found: {args.output_root}")

    started_at = _dt.datetime.now().astimezone().isoformat(timespec="seconds")
    run_root = args.output_root
    run_root.mkdir(parents=True, exist_ok=True)
    print(f"manifest: {args.manifest}", flush=True)
    print(f"maps: {len(entries)}", flush=True)
    print(f"renderer API: {args.renderer_api}", flush=True)
    print(f"retail asset root: {args.basepath or '(engine auto-discovery)'}", flush=True)
    print(f"raw output: {run_root}", flush=True)
    print(f"audit document: {args.audit}", flush=True)

    if args.dry_run:
        return 0

    results: list[MapResult] = []
    if args.analyze_existing:
        previous_summary_path = run_root / "map-load-condump-audit.json"
        if previous_summary_path.is_file():
            previous_payload = json.loads(previous_summary_path.read_text(encoding="utf-8"))
            if isinstance(previous_payload.get("startedAt"), str):
                started_at = previous_payload["startedAt"]
        previous_summary = load_previous_summary(run_root)
        for index, entry in enumerate(entries, start=1):
            results.append(reuse_map_result(entry, index=index, run_root=run_root, previous_summary=previous_summary))
    else:
        for index, entry in enumerate(entries, start=1):
            results.append(launch_map(entry, index=index, total=len(entries), args=args, run_root=run_root))

    issues = collect_issues(results, run_root=run_root, basepath=args.basepath)
    finished_at = _dt.datetime.now().astimezone().isoformat(timespec="seconds")
    write_json_summary(
        run_root / "map-load-condump-audit.json",
        results=results,
        issues=issues,
        args=args,
        run_root=run_root,
        started_at=started_at,
        finished_at=finished_at,
    )
    write_audit_markdown(
        args.audit,
        results=results,
        issues=issues,
        args=args,
        run_root=run_root,
        started_at=started_at,
        finished_at=finished_at,
    )
    actionable_count = sum(1 for issue in issues.values() if issue_is_actionable(issue))
    print(f"unique issue signatures: {len(issues)} ({actionable_count} actionable)", flush=True)
    print(f"wrote {args.audit}", flush=True)
    print(f"wrote {run_root / 'map-load-condump-audit.json'}", flush=True)
    return 1 if any(result.status != "ok" for result in results) or actionable_count else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        raise SystemExit(130)
