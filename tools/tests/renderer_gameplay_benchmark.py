#!/usr/bin/env python3
"""Run opt-in openPREY renderer gameplay benchmark and capture cases.

Unlike renderer_validation_matrix.py, this runner enters maps. It is intended
for local, target-hardware validation where stock Prey (2006) assets are available.
It launches from .install, writes isolated save/log roots under .tmp, captures
screenshots, dumps renderer benchmark metrics, and records a Markdown/JSON
report for performance triage. Every role fails closed on renderer, Vulkan
validation/call, fatal, and engine ERROR diagnostics found in its log streams.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import re
import struct
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SAFE_TIERS = ("auto", "legacy", "gl33", "gl41", "gl43", "gl45", "gl46")
PRESENTATION_MAXFPS = ("0", "120", "240")
PRESENTATION_SWAP_INTERVALS = ("0", "1")
DISPLAY_MODES = ("windowed",)

PREY_MAP_MANIFEST = Path(__file__).resolve().parents[2] / ".vscode" / "prey-maps.json"


def load_prey_map_manifest() -> dict[str, dict[str, str]]:
    if not PREY_MAP_MANIFEST.is_file():
        raise RuntimeError(f"canonical Prey map manifest is missing: {PREY_MAP_MANIFEST}")
    payload = json.loads(PREY_MAP_MANIFEST.read_text(encoding="utf-8"))
    entries = payload.get("maps")
    if not isinstance(entries, list):
        raise RuntimeError(f"canonical Prey map manifest has no maps list: {PREY_MAP_MANIFEST}")

    maps: dict[str, dict[str, str]] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise RuntimeError(f"canonical Prey map manifest contains a non-object entry: {entry!r}")
        map_name = entry.get("map")
        kind = entry.get("kind")
        title = entry.get("name")
        if not isinstance(map_name, str) or not map_name:
            raise RuntimeError(f"canonical Prey map manifest contains an invalid map path: {entry!r}")
        if kind not in ("sp", "mp") or not isinstance(title, str) or not title:
            raise RuntimeError(f"canonical Prey map manifest contains invalid metadata: {entry!r}")
        if map_name in maps:
            raise RuntimeError(f"canonical Prey map manifest repeats {map_name!r}")
        maps[map_name] = {"kind": kind, "name": title}
    return maps


CANONICAL_PREY_MAPS = load_prey_map_manifest()


def prey_scene(map_name: str, kind: str, purpose: str) -> dict[str, str]:
    entry = CANONICAL_PREY_MAPS.get(map_name)
    if entry is None:
        raise RuntimeError(f"renderer scene uses a map absent from {PREY_MAP_MANIFEST}: {map_name}")
    if entry["kind"] != kind:
        raise RuntimeError(
            f"renderer scene classifies {map_name} as {kind}, "
            f"but {PREY_MAP_MANIFEST} classifies it as {entry['kind']}"
        )
    return {
        "mode": kind.upper(),
        "map": map_name,
        "title": entry["name"],
        "purpose": purpose,
        "path": "spawn-static",
    }

REQUIRED_SCENES: dict[str, dict[str, Any]] = {
    "sp-roadhouse": prey_scene(
        "game/roadhouse",
        "sp",
        "opening-campaign baseline for characters, indoor materials, scripted sequences, mirror/glass, and GUI presentation",
    ),
    "sp-feedingtowera": prey_scene(
        "game/feedingtowera",
        "sp",
        "gravity, wall-walk, portal traversal, sky, energy, glass, and industrial-lighting coverage",
    ),
    "sp-biolabsa": prey_scene(
        "game/biolabsa",
        "sp",
        "dense interior materials, scan/energy effects, local lights, animated characters, and combat effects",
    ),
    "sp-superportal": prey_scene(
        "game/superportal",
        "sp",
        "large-scale portal, translucent energy, glass, post-process, and long-view coverage",
    ),
    "sp-shuttlea": prey_scene(
        "game/shuttlea",
        "sp",
        "shuttle view, cockpit GUI, beam/particle effects, portal views, and HUD composition",
    ),
    "sp-lotaa": prey_scene(
        "game/lotaa",
        "sp",
        "outdoor/dreamworld sky, spirit-era gameplay, portal geometry, and Prey effect coverage",
    ),
    "mp-dmroadhouse-listen": prey_scene(
        "game/dmroadhouse",
        "mp",
        "unified-module listen server plus local loopback client renderer parity on the stock MP smoke map",
    ),
}

SHADOW_SCENES: dict[str, dict[str, Any]] = {
    "shadow-projected-feedingtowera": prey_scene(
        "game/feedingtowera",
        "sp",
        "projected-light caster/receiver validation against energy, sky, and industrial geometry",
    ),
    "shadow-point-biolabsa": prey_scene(
        "game/biolabsa",
        "sp",
        "dense point-light face coverage and local-light receiver validation",
    ),
    "shadow-csm-lotaa": prey_scene(
        "game/lotaa",
        "sp",
        "CSM camera-sweep readiness across outdoor/dreamworld sky geometry",
    ),
    "shadow-cutout-roadhouse": prey_scene(
        "game/roadhouse",
        "sp",
        "hashed-alpha chain-link/cutout caster validation at distance",
    ),
    "shadow-character-feedingtowera": prey_scene(
        "game/feedingtowera",
        "sp",
        "dynamic character/skinned shadow caster and receiver validation",
    ),
    "shadow-translucent-superportal": prey_scene(
        "game/superportal",
        "sp",
        "optional translucent portal/energy moment-caster coverage where supported",
    ),
}

ALL_SCENES = {**REQUIRED_SCENES, **SHADOW_SCENES}

SHADOW_PRESETS: dict[str, dict[str, str]] = {
    "default": {},
    "stencil": {
        "r_shadows": "1",
        "r_useShadowMap": "0",
    },
    "mapped": {
        "r_shadows": "1",
        "r_useShadowMap": "1",
        "r_shadowMapCSM": "0",
        "r_shadowMapHashedAlpha": "1",
        "r_shadowMapTranslucentMoments": "0",
    },
    "csm": {
        "r_shadows": "1",
        "r_useShadowMap": "1",
        "r_shadowMapCSM": "1",
        "r_shadowMapHashedAlpha": "1",
        "r_shadowMapTranslucentMoments": "0",
    },
    "translucent": {
        "r_shadows": "1",
        "r_useShadowMap": "1",
        "r_shadowMapCSM": "1",
        "r_shadowMapHashedAlpha": "1",
        "r_shadowMapTranslucentMoments": "1",
    },
}

SHADOW_DEBUG_PRESET_MODES = (1, 2, 3, 4, 5, 6, 7, 12, 13, 14)

for debug_mode in SHADOW_DEBUG_PRESET_MODES:
    SHADOW_PRESETS[f"debug{debug_mode}"] = {
        "r_shadows": "1",
        "r_useShadowMap": "1",
        "r_shadowMapCSM": "1",
        "r_shadowMapHashedAlpha": "1",
        "r_shadowMapDebugOverlay": "1",
        "r_shadowMapDebugMode": str(debug_mode),
        "r_shadowMapTranslucentMoments": "0",
    }

PROFILE_DEFAULTS = {
    "smoke": {
        "cases": ("sp-roadhouse",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
    },
    "required": {
        "cases": tuple(REQUIRED_SCENES.keys()),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
    },
    "tiers": {
        "cases": ("sp-roadhouse",),
        "tiers": SAFE_TIERS,
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
    },
    "presentation": {
        "cases": ("sp-roadhouse",),
        "tiers": ("auto",),
        "maxfps": PRESENTATION_MAXFPS,
        "swap": PRESENTATION_SWAP_INTERVALS,
        "display": DISPLAY_MODES,
        "shadows": ("default",),
    },
    "shadows": {
        "cases": tuple(SHADOW_SCENES.keys()),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("stencil", "mapped", "csm", "translucent", "debug1", "debug2", "debug3", "debug4", "debug5", "debug6", "debug7", "debug12", "debug13", "debug14"),
    },
    "shadow-regression": {
        "cases": (
            "shadow-projected-feedingtowera",
            "shadow-point-biolabsa",
            "shadow-csm-lotaa",
            "shadow-character-feedingtowera",
            "shadow-cutout-roadhouse",
        ),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("csm",),
        "cvars": (
            ("r_shadowMapPointLights", "1"),
            ("r_shadowMapReport", "1"),
        ),
        # Freeze game time from tic 0 so animation/effect/weapon-raise state
        # is identical at capture. As a post-load cvar the freeze raced map
        # load timing and captures froze on different tics run to run (camera
        # micro-drift, weapon state deltas); as a launch cvar the capture
        # state is the spawn state, every run.
        "launchCvars": (
            ("g_stopTime", "1"),
        ),
    },
    "postaa-state-poison": {
        "cases": ("sp-roadhouse",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "cvars": (
            ("r_postAA", "1"),
            ("r_postAAStatePoisonTest", "1"),
        ),
    },
    "postaa-high": {
        "cases": ("sp-roadhouse",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "cvars": (
            ("r_postAA", "2"),
        ),
    },
    "postaa-ultra": {
        "cases": ("sp-roadhouse",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "cvars": (
            ("r_postAA", "3"),
        ),
    },
    "postaa-color-prototype": {
        "cases": ("sp-roadhouse",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "cvars": (
            ("r_postAA", "4"),
        ),
    },
    "full": {
        "cases": tuple(ALL_SCENES.keys()),
        "tiers": SAFE_TIERS,
        "maxfps": PRESENTATION_MAXFPS,
        "swap": PRESENTATION_SWAP_INTERVALS,
        "display": DISPLAY_MODES,
        "shadows": ("default", "stencil", "mapped", "csm", "translucent"),
    },
}

WARNING_PATTERNS = {
    "snPrintfOverflow": re.compile(r"idStr::snPrintf:\s*overflow", re.IGNORECASE),
    "idStrWarning": re.compile(r"WARNING:\s+idStr", re.IGNORECASE),
    "shaderCompile": re.compile(r"(shader compile|program link).*(failed|error)|failed to compile", re.IGNORECASE),
    "glError": re.compile(
        r"\bGL_(?:INVALID_[A-Z_]+|OUT_OF_MEMORY|STACK_(?:OVERFLOW|UNDERFLOW)|CONTEXT_LOST)\b"
        r"|OpenGL\s+error"
        r"|\bGL\s+debug\s+callback\b[^\r\n]{0,160}\btype\s*=\s*(?:error|undefined)\b"
        r"|\b(?:glGetError\s*(?:\(\s*\))?|GL_CheckErrors)\b[^\r\n]{0,48}"
        r"(?:0x(?!0+\b)[0-9A-F]+|[1-9][0-9]{2,})\b",
        re.IGNORECASE,
    ),
    "framebufferIncomplete": re.compile(
        r"\bGL_FRAMEBUFFER_(?:INCOMPLETE[A-Z0-9_]*|UNSUPPORTED|UNDEFINED)\b"
        r"|\b(?:framebuffer|FBO)\b[^\r\n]{0,64}\b(?:incomplete|unsupported)\b"
        r"|\b(?:incomplete|unsupported)\b[^\r\n]{0,32}\bframebuffer\b",
        re.IGNORECASE,
    ),
    "glDebugHighSeverity": re.compile(
        r"\bGL_DEBUG_SEVERITY_HIGH\b"
        r"|^(?=[^\r\n]*\b(?:GL|OpenGL)\b)"
        r"(?=[^\r\n]*\b(?:debug|callback)\b)"
        r"(?=[^\r\n]*(?:\bseverity\s*[:=]?\s*(?:high|0x9146|37190)\b|\bhigh[- ]severity\b|\[\s*high\s*\]))"
        r"[^\r\n]*$",
        re.IGNORECASE | re.MULTILINE,
    ),
    "vulkanValidation": re.compile(r"\bVulkan validation:", re.IGNORECASE),
    "vulkanVuid": re.compile(r"\bVUID-[A-Za-z0-9][A-Za-z0-9_.-]*\b"),
    "vulkanCallFailed": re.compile(
        r"\bVulkan\b[^\r\n]{0,160}\bvk[A-Z][A-Za-z0-9_]*\b[^\r\n]{0,96}\bfailed\b",
        re.IGNORECASE,
    ),
    "fatal": re.compile(
        r"\bFatal Error\b|^[ \t]*(?:\*+[ \t]*)?FATAL[ \t]*:|(?:could not|unable to) initialize OpenGL",
        re.IGNORECASE | re.MULTILINE,
    ),
    "errorLine": re.compile(r"^[ \t]*(?:\*+[ \t]*)?ERROR(?:[ \t]*:|[ \t]*$)", re.MULTILINE),
}

MAX_FAILURE_DIAGNOSTICS = 32
MAX_FAILURE_DIAGNOSTIC_CHARS = 600


@dataclass(frozen=True)
class RunSpec:
    case_id: str
    mode: str
    map_name: str
    purpose: str
    path_name: str
    tier: str
    maxfps: str
    swap_interval: str
    display_mode: str
    shadow_preset: str
    renderer: str

    @property
    def id(self) -> str:
        parts = [
            self.case_id,
            self.tier,
            f"fps{self.maxfps}",
            f"vsync{self.swap_interval}",
            self.display_mode,
            self.shadow_preset,
        ]
        parts.append(self.renderer)
        return sanitize_case_id("_".join(parts))


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def host_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64"):
        return "x64"
    if machine in ("arm64", "aarch64"):
        return "arm64"
    if machine in ("x86", "i386", "i686"):
        return "x86"
    return machine


def find_client_executable(root: Path) -> Path:
    install_dir = root / ".install"
    suffix = ".exe" if os.name == "nt" else ""
    candidate_prefixes = ("openPREY-client",)
    for prefix in candidate_prefixes:
        preferred = install_dir / f"{prefix}_{host_arch()}{suffix}"
        if preferred.exists():
            return preferred

    candidates: list[Path] = []
    seen: set[Path] = set()
    for prefix in candidate_prefixes:
        for candidate in sorted(install_dir.glob(f"{prefix}_*{suffix}")):
            if candidate not in seen:
                candidates.append(candidate)
                seen.add(candidate)

    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"openPREY client executable not found under {install_dir}")


def default_basepath() -> str:
    for name in ("OPENPREY_PREY_PATH", "OPENPREY_PREY_ROOT"):
        configured = os.environ.get(name, "").strip()
        if configured:
            return configured
    return ""


def sanitize_case_id(case_id: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", case_id)


def split_csv(value: str, defaults: tuple[str, ...]) -> tuple[str, ...]:
    if not value:
        return defaults
    return tuple(item.strip() for item in value.split(",") if item.strip())


def parse_extra_cvars(values: list[str]) -> tuple[tuple[str, str], ...]:
    parsed: list[tuple[str, str]] = []
    for raw in values:
        item = raw.strip()
        if not item:
            continue
        if "=" in item:
            name, value = item.split("=", 1)
        else:
            parts = item.split(None, 1)
            if len(parts) != 2:
                raise ValueError(f"extra cvar '{raw}' must use name=value or 'name value'")
            name, value = parts
        name = name.strip()
        value = value.strip()
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            raise ValueError(f"extra cvar name '{name}' is not a valid cvar identifier")
        if not value:
            raise ValueError(f"extra cvar '{name}' needs a value")
        parsed.append((name, value))
    return tuple(parsed)


def parse_exec_commands(values: list[str]) -> tuple[str, ...]:
    commands: list[str] = []
    for raw in values:
        command = raw.strip()
        if not command:
            raise ValueError("empty --exec-command value")
        if any(ord(ch) < 32 for ch in command):
            raise ValueError(f"--exec-command contains a control character: {raw!r}")
        commands.append(command)
    return tuple(commands)


def append_set(args: list[str], name: str, value: Any) -> None:
    args += ["+set", name, str(value)]


def append_command(args: list[str], name: str, *values: Any) -> None:
    args.append("+" + name)
    args.extend(str(value) for value in values)


def common_args(
    root: Path,
    savepath: Path,
    log_name: str,
    basepath: str,
    spec: RunSpec,
    benchmark_preset: str,
    modern_executor: bool,
    show_fps_overlay: bool,
    launch_cvars: tuple[tuple[str, str], ...] = (),
    autoexec_cfg: str | None = None,
    autoexec_delay_ms: int = 1000,
) -> list[str]:
    args: list[str] = []
    multiple_instance_cvar = "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances"
    append_set(args, multiple_instance_cvar, "1")
    append_set(args, "logFile", "2")
    append_set(args, "logFileName", f"logs/{log_name}")
    append_set(args, "developer", "1")
    append_set(args, "r_ignoreGLErrors", "0")
    append_set(args, "r_fullscreen", "0")
    append_set(args, "r_swapInterval", spec.swap_interval)
    append_set(args, "com_maxfps", spec.maxfps)
    append_set(args, "com_showFPS", "1" if show_fps_overlay else "0")
    append_set(args, "com_skipLoadingContinue", "1")
    append_set(args, "com_loadingContinueAutoAdvance", "1")
    append_set(args, "g_autoSkipCinematics", "1")
    append_set(args, "g_autoScreenshot", "0")
    if autoexec_cfg:
        append_set(args, "g_autoExecAfterMapLoad", autoexec_cfg)
        append_set(args, "g_autoExecAfterMapLoadDelayMs", max(0, autoexec_delay_ms))
    append_set(args, "r_glTier", spec.tier)
    append_set(args, "r_renderer", spec.renderer)
    append_set(args, "r_rendererMetrics", "0")
    append_set(args, "r_rendererGpuTimers", "0")
    append_set(args, "r_rendererModernExecutor", "1" if modern_executor and spec.tier != "legacy" else "0")
    append_set(args, "r_rendererModernAutoPromote", "0")
    append_set(args, "r_rendererBenchmarkPreset", benchmark_preset)
    append_set(args, "fs_savepath", str(savepath))
    append_set(args, "fs_devpath", str(root / ".install"))
    append_set(args, "fs_game", "basepr")
    if basepath:
        append_set(args, "fs_basepath", basepath)

    for name, value in launch_cvars:
        append_set(args, name, value)

    for name, value in SHADOW_PRESETS[spec.shadow_preset].items():
        append_set(args, name, value)

    return args


def build_scripted_capture_lines(
    spec: RunSpec,
    role: str,
    run_id: str,
    settle_frames: int,
    sample_frames: int,
    sample_msec: int,
    extra_cvars: tuple[tuple[str, str], ...] = (),
    exec_commands: tuple[str, ...] = (),
    gpu_timers: bool = False,
    renderer_metrics: bool = True,
    capture_index: int = 0,
) -> tuple[list[str], str]:
    shot_name = f"screenshots/renderer-bench/{role}_{capture_index}.tga"
    lines: list[str] = [
        "r_rendererModernVisible 0",
        "r_rendererModernVisibleDepth 0",
        "r_rendererModernOpaque 0",
        "r_rendererModernDeferred 0",
        "r_rendererForwardPlus 0",
        "r_rendererModernSubmit 0",
        "r_rendererGpuValidation 0",
        "r_rendererBindless 0",
        "r_rendererShaderReload 0",
    ]
    for name, value in extra_cvars:
        lines.append(f"{name} {value}")
    lines += [
        f"wait {max(1, settle_frames)}",
        "god",
        "notarget",
        "getviewpos",
    ]
    lines.extend(exec_commands)
    lines.append("framePacingReset")
    sample_wait = f"waitMsec {max(1, sample_msec)}" if sample_msec > 0 else f"wait {max(1, sample_frames)}"
    if renderer_metrics:
        lines += [
            "r_rendererMetrics 1",
            f"r_rendererGpuTimers {1 if gpu_timers else 0}",
            sample_wait,
            "rendererBenchmarkCapture",
            "r_rendererMetrics 0",
        ]
    else:
        lines += [
            "r_rendererMetrics 0",
            "r_rendererGpuTimers 0",
            sample_wait,
        ]
    lines += [
        "framePacingSnapshot",
        "gfxInfo",
        f'screenshot "{shot_name}"',
        "wait 5",
        "quit",
    ]
    return lines, shot_name


def write_autoexec_cfg(
    savepath: Path,
    spec: RunSpec,
    role: str,
    run_id: str,
    settle_frames: int,
    sample_frames: int,
    sample_msec: int,
    extra_cvars: tuple[tuple[str, str], ...] = (),
    exec_commands: tuple[str, ...] = (),
    gpu_timers: bool = False,
    renderer_metrics: bool = True,
    capture_index: int = 0,
) -> tuple[str, str]:
    lines, shot_name = build_scripted_capture_lines(
        spec,
        role,
        run_id,
        settle_frames,
        sample_frames,
        sample_msec,
        extra_cvars,
        exec_commands,
        gpu_timers,
        renderer_metrics,
        capture_index,
    )
    cfg_rel = f"renderer-bench/{role}_{capture_index}.cfg"
    payload = "\n".join(lines) + "\n"
    screenshot_rel = Path(shot_name.replace("/", os.sep))
    for game_dir in ("basepr",):
        cfg_path = savepath / game_dir / Path(cfg_rel)
        cfg_path.parent.mkdir(parents=True, exist_ok=True)
        cfg_path.write_text(payload, encoding="utf-8")
        screenshot_path = savepath / game_dir / screenshot_rel
        screenshot_path.parent.mkdir(parents=True, exist_ok=True)
    return cfg_rel, shot_name


def find_log(savepath: Path, log_name: str) -> Path | None:
    candidates = [
        savepath / "basepr" / "logs" / log_name,
        savepath / "logs" / log_name,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def find_screenshot(savepath: Path, relative_name: str) -> Path | None:
    rel = Path(relative_name.replace("/", os.sep))
    for game_dir in ("basepr",):
        candidate = savepath / game_dir / rel
        if candidate.exists():
            return candidate
    candidate = savepath / rel
    if candidate.exists():
        return candidate
    return None


def read_text(path: Path | None) -> str:
    if path is None or not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def warning_counts(text: str) -> dict[str, int]:
    return {name: len(pattern.findall(text)) for name, pattern in WARNING_PATTERNS.items()}


def collect_failure_diagnostics(
    sources: tuple[tuple[str, str], ...],
) -> tuple[list[dict[str, Any]], int]:
    diagnostics: list[dict[str, Any]] = []
    omitted = 0
    for source_name, source_text in sources:
        for line_number, raw_line in enumerate(source_text.splitlines(), start=1):
            signatures = [name for name, pattern in WARNING_PATTERNS.items() if pattern.search(raw_line)]
            if not signatures:
                continue
            line = raw_line.strip()
            if len(line) > MAX_FAILURE_DIAGNOSTIC_CHARS:
                line = line[: MAX_FAILURE_DIAGNOSTIC_CHARS - 3] + "..."
            if len(diagnostics) < MAX_FAILURE_DIAGNOSTICS:
                diagnostics.append(
                    {
                        "source": source_name,
                        "lineNumber": line_number,
                        "signatures": signatures,
                        "text": line,
                    }
                )
            else:
                omitted += 1
    return diagnostics, omitted


def format_failure_diagnostic(diagnostic: dict[str, Any]) -> str:
    signatures = ",".join(diagnostic["signatures"])
    return (
        f"{diagnostic['source']}:{diagnostic['lineNumber']} "
        f"[{signatures}] {diagnostic['text']}"
    )


def extract_last_line(text: str, token: str) -> str:
    lines = [line.strip() for line in text.splitlines() if token in line]
    return lines[-1] if lines else ""


def parse_frame_pacing(line: str) -> dict[str, str]:
    if not line:
        return {}
    match = re.search(
        r"samples=(\d+).*?present=([0-9.]+) ms \(([0-9.]+) Hz\)"
        r"(?:, p50=([0-9.]+) ms, p95=([0-9.]+) ms, p99=([0-9.]+) ms, max=([0-9.]+) ms)?",
        line,
    )
    if not match:
        return {}
    samples, present_ms, present_hz, p50_ms, p95_ms, p99_ms, max_ms = match.groups()
    result = {
        "pacingSamples": samples,
        "pacingPresentMs": present_ms,
        "pacingHz": present_hz,
    }
    if p50_ms is not None:
        result.update(
            {
                "pacingP50Ms": p50_ms,
                "pacingP95Ms": p95_ms,
                "pacingP99Ms": p99_ms,
                "pacingMaxMs": max_ms,
            }
        )
    return result


def extract_summary(text: str) -> dict[str, str]:
    summary: dict[str, str] = {
        "benchmarkCapture": extract_last_line(text, "rendererBenchmark capture("),
        "benchmarkInfo": extract_last_line(text, "Renderer benchmark:"),
        "framePacing": extract_last_line(text, "Frame pacing"),
        "selectedTier": extract_last_line(text, "Selected renderer tier:"),
        "tierContract": extract_last_line(text, "Renderer tier contract:"),
    }
    matches = re.findall(
        r"rendererBenchmark capture\(.*?samples=(\d+).*?p50=(\d+).*?p95=(\d+).*?p99=(\d+)"
        r".*?\b(?:thresholdPass|pass)\s*=\s*(\d+)\b",
        text,
        re.IGNORECASE,
    )
    if matches:
        samples, p50, p95, p99, threshold_pass = matches[-1]
        summary.update(
            {
                "samples": samples,
                "p50": p50,
                "p95": p95,
                "p99": p99,
                "thresholdPass": threshold_pass,
            }
        )
    if "thresholdPass" not in summary:
        for benchmark_line in (summary["benchmarkCapture"], summary["benchmarkInfo"]):
            match = re.search(r"\b(?:thresholdPass|pass)\s*=\s*(\d+)\b", benchmark_line, re.IGNORECASE)
            if match:
                summary["thresholdPass"] = match.group(1)
                break
    summary.update(parse_frame_pacing(summary["framePacing"]))
    return summary


def summary_float(summary: dict[str, str], key: str) -> float | None:
    value = summary.get(key)
    if value is None:
        return None
    try:
        return float(value)
    except ValueError:
        return None


def load_tga_rgb(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if len(data) < 18:
        raise ValueError("file is too small to be a TGA")
    id_length, color_map_type, image_type = data[0], data[1], data[2]
    if color_map_type != 0 or image_type not in (2, 3):
        raise ValueError(f"unsupported TGA type {image_type} with color map {color_map_type}")
    width = struct.unpack_from("<H", data, 12)[0]
    height = struct.unpack_from("<H", data, 14)[0]
    bits = data[16]
    if width <= 0 or height <= 0 or bits not in (24, 32):
        raise ValueError(f"unsupported TGA dimensions/depth {width}x{height}x{bits}")
    pixel_size = bits // 8
    pixel_count = width * height
    start = 18 + id_length
    end = start + pixel_count * pixel_size
    if len(data) < end:
        raise ValueError("truncated TGA pixel payload")
    pixels = data[start:end]
    rgb = bytearray(pixel_count * 3)
    for i in range(pixel_count):
        src = i * pixel_size
        dst = i * 3
        if image_type == 3:
            value = pixels[src]
            rgb[dst : dst + 3] = bytes((value, value, value))
        else:
            b, g, r = pixels[src], pixels[src + 1], pixels[src + 2]
            rgb[dst : dst + 3] = bytes((r, g, b))
    return width, height, bytes(rgb)


def compare_tga(actual: Path, reference: Path) -> dict[str, Any]:
    aw, ah, ap = load_tga_rgb(actual)
    rw, rh, rp = load_tga_rgb(reference)
    if (aw, ah) != (rw, rh):
        return {
            "status": "dimension-mismatch",
            "actualSize": f"{aw}x{ah}",
            "referenceSize": f"{rw}x{rh}",
        }
    total_sq = 0
    max_delta = 0
    differing = 0
    for a, r in zip(ap, rp):
        delta = abs(a - r)
        if delta:
            differing += 1
            total_sq += delta * delta
            max_delta = max(max_delta, delta)
    rms = math.sqrt(total_sq / max(1, len(ap)))
    return {
        "status": "compared",
        "actualSize": f"{aw}x{ah}",
        "referenceSize": f"{rw}x{rh}",
        "rms": round(rms, 4),
        "maxDelta": max_delta,
        "differingChannels": differing,
    }


def screenshot_reference_candidates(
    reference_dir: Path, screenshot: Path, savepath: Path, case_id: str | None = None
) -> list[Path]:
    # Case-scoped candidates take precedence: every case captures the same
    # relative screenshot name (screenshots/renderer-bench/sp_0.tga), so a
    # flat reference directory can only ever serve a single case per profile.
    candidates = [reference_dir / screenshot.name]
    if case_id:
        candidates.insert(0, reference_dir / case_id / screenshot.name)
    for game_dir in ("basepr",):
        root = savepath / game_dir
        try:
            rel = screenshot.relative_to(root)
            candidates.insert(0, reference_dir / rel)
            if case_id:
                candidates.insert(0, reference_dir / case_id / rel)
        except ValueError:
            pass
    return candidates


def compare_screenshot_if_requested(
    screenshot: Path | None,
    savepath: Path,
    reference_dir: Path | None,
    rms_threshold: float,
    max_threshold: int,
    require_reference: bool,
    case_id: str | None = None,
) -> dict[str, Any]:
    if screenshot is None:
        return {"status": "missing-screenshot"}
    result: dict[str, Any] = {
        "status": "not-requested",
        "actual": str(screenshot),
        "sha256": hashlib.sha256(screenshot.read_bytes()).hexdigest(),
    }
    if reference_dir is None:
        return result
    for candidate in screenshot_reference_candidates(reference_dir, screenshot, savepath, case_id):
        if candidate.exists():
            comparison = compare_tga(screenshot, candidate)
            comparison["actual"] = str(screenshot)
            comparison["reference"] = str(candidate)
            if comparison["status"] == "compared":
                comparison["pass"] = comparison["rms"] <= rms_threshold and comparison["maxDelta"] <= max_threshold
            return comparison
    result["status"] = "missing-reference" if require_reference else "reference-not-found"
    result["referenceDir"] = str(reference_dir)
    result["pass"] = not require_reference
    return result


def evaluate_role_result(
    spec: RunSpec,
    role: str,
    exit_code: int,
    timed_out: bool,
    elapsed_seconds: float,
    savepath: Path,
    log_name: str,
    stdout_path: Path,
    stderr_path: Path,
    screenshot_rel: str,
    reference_dir: Path | None,
    rms_threshold: float,
    max_threshold: int,
    require_reference: bool,
    require_benchmark: bool = True,
    min_pacing_hz: float = 0.0,
    max_p95_ms: float = 0.0,
    max_p99_ms: float = 0.0,
) -> dict[str, Any]:
    log_path = find_log(savepath, log_name)
    diagnostic_sources = (
        ("log", read_text(log_path)),
        ("stdout", read_text(stdout_path)),
        ("stderr", read_text(stderr_path)),
    )
    text = "\n".join(part for _, part in diagnostic_sources if part)
    screenshot = find_screenshot(savepath, screenshot_rel)
    warnings = warning_counts(text)
    failure_diagnostics, failure_diagnostics_omitted = collect_failure_diagnostics(diagnostic_sources)
    summary = extract_summary(text)
    image = compare_screenshot_if_requested(
        screenshot,
        savepath,
        reference_dir,
        rms_threshold,
        max_threshold,
        require_reference,
        spec.id,
    )
    missing: list[str] = []
    if timed_out:
        missing.append("timeout")
    if log_path is None:
        missing.append("log file")
    if require_benchmark and "rendererBenchmark capture(" not in text:
        missing.append("renderer benchmark capture line")
    if require_benchmark and "Renderer benchmark:" not in text:
        missing.append("gfxInfo benchmark line")
    threshold_pass = summary.get("thresholdPass")
    if require_benchmark and threshold_pass != "1":
        missing.append(f"renderer benchmark thresholdPass={threshold_pass or 'missing'}")
    elif threshold_pass is not None and threshold_pass != "1":
        missing.append(f"renderer benchmark thresholdPass={threshold_pass}")
    if min_pacing_hz > 0.0:
        pacing_hz = summary_float(summary, "pacingHz")
        if pacing_hz is None:
            missing.append("frame pacing Hz")
        elif pacing_hz < min_pacing_hz:
            missing.append(f"pacingHz={pacing_hz:.1f}<{min_pacing_hz:.1f}")
    if max_p95_ms > 0.0:
        p95_ms = summary_float(summary, "pacingP95Ms")
        if p95_ms is None:
            missing.append("frame pacing p95")
        elif p95_ms > max_p95_ms:
            missing.append(f"pacingP95={p95_ms:.1f}>{max_p95_ms:.1f}")
    if max_p99_ms > 0.0:
        p99_ms = summary_float(summary, "pacingP99Ms")
        if p99_ms is None:
            missing.append("frame pacing p99")
        elif p99_ms > max_p99_ms:
            missing.append(f"pacingP99={p99_ms:.1f}>{max_p99_ms:.1f}")
    if "Selected renderer tier:" not in text:
        missing.append("selected tier line")
    if screenshot is None:
        missing.append("screenshot")
    if any(count > 0 for count in warnings.values()):
        missing += [f"{name}={count}" for name, count in warnings.items() if count > 0]
    if image.get("pass") is False:
        missing.append(f"image comparison {image.get('status')}")

    ok = exit_code == 0 and not timed_out and not missing
    return {
        "id": spec.id,
        "role": role,
        "status": "pass" if ok else "fail",
        "exitCode": exit_code,
        "timedOut": timed_out,
        "elapsedSeconds": round(elapsed_seconds, 2),
        "log": str(log_path) if log_path is not None else "",
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "screenshot": str(screenshot) if screenshot is not None else "",
        "screenshotRequest": screenshot_rel,
        "warnings": warnings,
        "failureDiagnostics": failure_diagnostics,
        "failureDiagnosticsOmitted": failure_diagnostics_omitted,
        "missing": missing,
        "summary": summary,
        "image": image,
    }


def launch_and_wait(
    executable: Path,
    args: list[str],
    cwd: Path,
    stdout_path: Path,
    stderr_path: Path,
    timeout_seconds: int,
) -> tuple[int, bool, float]:
    started = time.time()
    timed_out = False
    with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout_file, stderr_path.open("w", encoding="utf-8", errors="replace") as stderr_file:
        process = subprocess.Popen(
            [str(executable)] + args,
            cwd=str(cwd),
            stdout=stdout_file,
            stderr=stderr_file,
        )
        try:
            exit_code = process.wait(timeout=timeout_seconds)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
            exit_code = process.wait(timeout=10)
    elapsed = time.time() - started
    return exit_code, timed_out, elapsed


def run_sp_spec(
    root: Path,
    executable: Path,
    output_dir: Path,
    basepath: str,
    run_id: str,
    spec: RunSpec,
    args: argparse.Namespace,
) -> dict[str, Any]:
    savepath = output_dir / "savepaths" / spec.id
    savepath.mkdir(parents=True, exist_ok=True)
    log_name = f"openprey_gameplay_{spec.id}.log"
    log_path = find_log(savepath, log_name)
    if log_path is not None:
        log_path.unlink()
    stdout_path = output_dir / f"{spec.id}.out.txt"
    stderr_path = output_dir / f"{spec.id}.err.txt"
    autoexec_cfg, screenshot_rel = write_autoexec_cfg(
        savepath,
        spec,
        "sp",
        run_id,
        args.settle_frames,
        args.sample_frames,
        args.sample_msec,
        args.extra_cvars,
        args.exec_commands,
        args.gpu_timers,
        not args.pacing_only,
    )
    game_args = common_args(
        root,
        savepath,
        log_name,
        basepath,
        spec,
        args.benchmark_preset,
        args.modern_executor,
        args.show_fps_overlay,
        args.launch_cvars,
        autoexec_cfg,
        args.autoexec_delay_ms,
    )
    append_set(game_args, "si_gameType", "singleplayer")
    append_command(game_args, "map", spec.map_name)

    if args.dry_run:
        return {
            "id": spec.id,
            "mode": spec.mode,
            "map": spec.map_name,
            "status": "planned",
            "args": game_args,
            "autoexecCfg": autoexec_cfg,
            "screenshotRequest": screenshot_rel,
            "roles": [],
        }

    exit_code, timed_out, elapsed = launch_and_wait(
        executable,
        game_args,
        root / ".install",
        stdout_path,
        stderr_path,
        args.timeout,
    )
    role_result = evaluate_role_result(
        spec,
        "sp",
        exit_code,
        timed_out,
        elapsed,
        savepath,
        log_name,
        stdout_path,
        stderr_path,
        screenshot_rel,
        args.reference_dir_path,
        args.image_rms_threshold,
        args.image_max_threshold,
        args.require_references,
        not args.pacing_only,
        args.min_pacing_hz,
        args.max_p95_ms,
        args.max_p99_ms,
    )
    return {
        "id": spec.id,
        "mode": spec.mode,
        "map": spec.map_name,
        "purpose": spec.purpose,
        "tier": spec.tier,
        "maxfps": spec.maxfps,
        "swapInterval": spec.swap_interval,
        "display": spec.display_mode,
        "shadowPreset": spec.shadow_preset,
        "renderer": spec.renderer,
        "status": role_result["status"],
        "roles": [role_result],
    }


def run_mp_spec(
    root: Path,
    executable: Path,
    output_dir: Path,
    basepath: str,
    run_id: str,
    spec: RunSpec,
    index: int,
    args: argparse.Namespace,
) -> dict[str, Any]:
    port = args.mp_port + index
    server_savepath = output_dir / "savepaths" / f"{spec.id}_server"
    client_savepath = output_dir / "savepaths" / f"{spec.id}_client"
    server_savepath.mkdir(parents=True, exist_ok=True)
    client_savepath.mkdir(parents=True, exist_ok=True)

    server_log = f"openprey_gameplay_{spec.id}_server.log"
    client_log = f"openprey_gameplay_{spec.id}_client.log"
    for savepath, log_name in ((server_savepath, server_log), (client_savepath, client_log)):
        log_path = find_log(savepath, log_name)
        if log_path is not None:
            log_path.unlink()

    server_stdout = output_dir / f"{spec.id}_server.out.txt"
    server_stderr = output_dir / f"{spec.id}_server.err.txt"
    client_stdout = output_dir / f"{spec.id}_client.out.txt"
    client_stderr = output_dir / f"{spec.id}_client.err.txt"

    server_autoexec_cfg, server_screenshot = write_autoexec_cfg(
        server_savepath,
        spec,
        "server",
        run_id,
        args.settle_frames + args.mp_client_delay_frames,
        args.sample_frames,
        args.sample_msec,
        args.extra_cvars,
        args.exec_commands,
        args.gpu_timers,
        not args.pacing_only,
    )
    server_args = common_args(
        root,
        server_savepath,
        server_log,
        basepath,
        spec,
        args.benchmark_preset,
        args.modern_executor,
        args.show_fps_overlay,
        args.launch_cvars,
        server_autoexec_cfg,
        args.autoexec_delay_ms,
    )
    append_set(server_args, "net_serverDedicated", "0")
    append_set(server_args, "net_port", str(port))
    server_args += ["+seta", "si_pure", "0"]
    append_set(server_args, "net_serverAllowServerMod", "1")
    append_set(server_args, "sv_cheats", "1")
    append_set(server_args, "si_gameType", "DM")
    append_command(server_args, "spawnServer", spec.map_name)

    client_autoexec_cfg, client_screenshot = write_autoexec_cfg(
        client_savepath,
        spec,
        "client",
        run_id,
        args.settle_frames,
        args.sample_frames,
        args.sample_msec,
        args.extra_cvars,
        args.exec_commands,
        args.gpu_timers,
        not args.pacing_only,
    )
    client_args = common_args(
        root,
        client_savepath,
        client_log,
        basepath,
        spec,
        args.benchmark_preset,
        args.modern_executor,
        args.show_fps_overlay,
        args.launch_cvars,
        client_autoexec_cfg,
        args.autoexec_delay_ms,
    )
    append_set(client_args, "ui_name", "RendererBenchClient")
    append_command(client_args, "connect", f"127.0.0.1:{port}")

    if args.dry_run:
        return {
            "id": spec.id,
            "mode": spec.mode,
            "map": spec.map_name,
            "status": "planned",
            "serverArgs": server_args,
            "clientArgs": client_args,
            "serverAutoexecCfg": server_autoexec_cfg,
            "clientAutoexecCfg": client_autoexec_cfg,
            "serverScreenshotRequest": server_screenshot,
            "clientScreenshotRequest": client_screenshot,
            "roles": [],
        }

    started = time.time()
    server_timed_out = False
    client_timed_out = False
    with server_stdout.open("w", encoding="utf-8", errors="replace") as server_out, server_stderr.open("w", encoding="utf-8", errors="replace") as server_err:
        server_process = subprocess.Popen(
            [str(executable)] + server_args,
            cwd=str(root / ".install"),
            stdout=server_out,
            stderr=server_err,
        )
    time.sleep(max(1, args.mp_client_delay))
    with client_stdout.open("w", encoding="utf-8", errors="replace") as client_out, client_stderr.open("w", encoding="utf-8", errors="replace") as client_err:
        client_process = subprocess.Popen(
            [str(executable)] + client_args,
            cwd=str(root / ".install"),
            stdout=client_out,
            stderr=client_err,
        )

    try:
        client_exit = client_process.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        client_timed_out = True
        client_process.kill()
        client_exit = client_process.wait(timeout=10)

    remaining = max(10, args.timeout - int(time.time() - started))
    try:
        server_exit = server_process.wait(timeout=remaining)
    except subprocess.TimeoutExpired:
        server_timed_out = True
        server_process.kill()
        server_exit = server_process.wait(timeout=10)

    elapsed = time.time() - started
    server_result = evaluate_role_result(
        spec,
        "server",
        server_exit,
        server_timed_out,
        elapsed,
        server_savepath,
        server_log,
        server_stdout,
        server_stderr,
        server_screenshot,
        args.reference_dir_path,
        args.image_rms_threshold,
        args.image_max_threshold,
        args.require_references,
        not args.pacing_only,
        args.min_pacing_hz,
        args.max_p95_ms,
        args.max_p99_ms,
    )
    client_result = evaluate_role_result(
        spec,
        "client",
        client_exit,
        client_timed_out,
        elapsed,
        client_savepath,
        client_log,
        client_stdout,
        client_stderr,
        client_screenshot,
        args.reference_dir_path,
        args.image_rms_threshold,
        args.image_max_threshold,
        args.require_references,
        not args.pacing_only,
        args.min_pacing_hz,
        args.max_p95_ms,
        args.max_p99_ms,
    )
    ok = server_result["status"] == "pass" and client_result["status"] == "pass"
    return {
        "id": spec.id,
        "mode": spec.mode,
        "map": spec.map_name,
        "purpose": spec.purpose,
        "tier": spec.tier,
        "maxfps": spec.maxfps,
        "swapInterval": spec.swap_interval,
        "display": spec.display_mode,
        "shadowPreset": spec.shadow_preset,
        "renderer": spec.renderer,
        "status": "pass" if ok else "fail",
        "port": port,
        "roles": [server_result, client_result],
    }


def harness_failure_result(spec: RunSpec, exc: Exception) -> dict[str, Any]:
    message = f"harness exception: {type(exc).__name__}: {exc}"
    role = "client" if spec.mode == "MP" else "sp"
    role_result = {
        "id": spec.id,
        "role": role,
        "status": "fail",
        "exitCode": "",
        "timedOut": False,
        "elapsedSeconds": 0.0,
        "log": "",
        "stdout": "",
        "stderr": "",
        "screenshot": "",
        "screenshotRequest": "",
        "warnings": {},
        "failureDiagnostics": [],
        "failureDiagnosticsOmitted": 0,
        "missing": [message],
        "summary": {},
        "image": {"status": "harness-error"},
    }
    return {
        "id": spec.id,
        "mode": spec.mode,
        "map": spec.map_name,
        "purpose": spec.purpose,
        "tier": spec.tier,
        "maxfps": spec.maxfps,
        "swapInterval": spec.swap_interval,
        "display": spec.display_mode,
        "shadowPreset": spec.shadow_preset,
        "renderer": spec.renderer,
        "status": "fail",
        "roles": [role_result],
        "harnessError": message,
    }


def build_specs(args: argparse.Namespace) -> list[RunSpec]:
    defaults = PROFILE_DEFAULTS[args.profile]
    case_ids = split_csv(args.cases, defaults["cases"])
    tiers = split_csv(args.tiers, defaults["tiers"])
    maxfps_values = split_csv(args.maxfps, defaults["maxfps"])
    swap_values = split_csv(args.swap_intervals, defaults["swap"])
    display_values = split_csv(args.display_modes, defaults["display"])
    shadow_values = split_csv(args.shadow_presets, defaults["shadows"])

    specs: list[RunSpec] = []
    for case_id in case_ids:
        if case_id not in ALL_SCENES:
            raise ValueError(f"unknown case '{case_id}'. Use --list to inspect valid cases.")
        scene = ALL_SCENES[case_id]
        for tier in tiers:
            if tier not in SAFE_TIERS:
                raise ValueError(f"unknown r_glTier '{tier}'")
            for maxfps in maxfps_values:
                for swap in swap_values:
                    for display in display_values:
                        if display not in DISPLAY_MODES:
                            raise ValueError(f"unknown display mode '{display}'")
                        for shadow in shadow_values:
                            if shadow not in SHADOW_PRESETS:
                                raise ValueError(f"unknown shadow preset '{shadow}'")
                            specs.append(
                                RunSpec(
                                    case_id=case_id,
                                    mode=scene["mode"],
                                    map_name=scene["map"],
                                    purpose=scene["purpose"],
                                    path_name=scene["path"],
                                    tier=tier,
                                    maxfps=maxfps,
                                    swap_interval=swap,
                                    display_mode=display,
                                    shadow_preset=shadow,
                                    renderer=args.renderer,
                                )
                            )
    if args.limit > 0:
        specs = specs[: args.limit]
    return specs


def write_reports(output_dir: Path, results: list[dict[str, Any]], metadata: dict[str, Any]) -> tuple[Path, Path]:
    report_json = output_dir / "renderer_gameplay_benchmark_report.json"
    report_md = output_dir / "renderer_gameplay_benchmark_report.md"
    payload = {
        "metadata": metadata,
        "preyMapManifest": str(PREY_MAP_MANIFEST),
        "requiredScenes": REQUIRED_SCENES,
        "shadowScenes": SHADOW_SCENES,
        "shadowPresets": SHADOW_PRESETS,
        "results": results,
    }
    report_json.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    passed = sum(1 for result in results if result["status"] == "pass")
    failed = sum(1 for result in results if result["status"] == "fail")
    planned = sum(1 for result in results if result["status"] == "planned")
    lines = [
        "# Renderer Gameplay Benchmark Report",
        "",
        f"- Generated: {metadata['generated']}",
        f"- Host: {metadata['host']}",
        f"- Executable: `{metadata['executable']}`",
        f"- Base path: `{metadata['basepath'] or 'not set'}`",
        f"- Canonical Prey map manifest: `{PREY_MAP_MANIFEST}`",
        f"- Profile: `{metadata['profile']}`",
        f"- Sample: `{metadata['sampleMsec']} ms`" if metadata.get("sampleMsec", 0) > 0 else f"- Sample: `{metadata['sampleFrames']} frames`",
        f"- Cases: {passed} passed, {failed} failed, {planned} planned",
        "",
        "## Results",
        "",
        "| Status | Case | Mode | Map | Tier | FPS | VSync | Display | Shadow | Pacing | Benchmark | Image | Screenshot | Log |",
        "|---|---|---|---|---|---:|---:|---|---|---|---|---|---|---|",
    ]
    for result in results:
        if result["status"] == "planned":
            lines.append(
                f"| planned | `{result['id']}` | {result['mode']} | `{result['map']}` |  |  |  |  |  |  | dry run |  |  |  |"
            )
            continue
        role = next((item for item in result.get("roles", []) if item["role"] in ("client", "sp")), result.get("roles", [{}])[0])
        summary = role.get("summary", {})
        benchmark = summary.get("benchmarkCapture", "")
        if len(benchmark) > 80:
            benchmark = benchmark[:77] + "..."
        pacing = ""
        if summary.get("pacingHz"):
            pacing = f"{summary['pacingHz']} Hz"
            if summary.get("pacingP95Ms"):
                pacing += f" / p95 {summary['pacingP95Ms']} ms"
        image = role.get("image", {}) or {}
        image_status = image.get("status", "missing")
        if image_status == "compared":
            image_status = f"compared rms={image.get('rms', '?')} max={image.get('maxDelta', '?')} pass={int(bool(image.get('pass', False)))}"
        elif image_status in ("not-requested", "reference-not-found"):
            image_status = f"{image_status} {image.get('sha256', '')[:12]}".strip()
        screenshot = role.get("screenshot", "")
        log = role.get("log", "")
        lines.append(
            f"| {result['status']} | `{result['id']}` | {result['mode']} | `{result['map']}` | `{result['tier']}` | {result['maxfps']} | {result['swapInterval']} | {result['display']} | `{result['shadowPreset']}` | {pacing or 'missing'} | {benchmark or 'missing'} | {image_status} | `{screenshot}` | `{log}` |"
        )
        for role_result in result.get("roles", []):
            if role_result.get("missing"):
                lines.append(
                    f"|  | `{role_result['role']}` missing |  |  |  |  |  |  |  | {'; '.join(role_result['missing'])} |  |  |  |  |"
                )

    diagnostic_roles = [
        (result, role_result)
        for result in results
        for role_result in result.get("roles", [])
        if role_result.get("failureDiagnostics")
    ]
    if diagnostic_roles:
        lines += [
            "",
            "## Matched Failure Diagnostics",
            "",
            "The exact matched lines are retained here even when they fall outside the normal log tail.",
        ]
        for result, role_result in diagnostic_roles:
            lines += [
                "",
                f"### `{result['id']}` / `{role_result['role']}`",
                "",
                "```text",
            ]
            lines.extend(format_failure_diagnostic(item) for item in role_result["failureDiagnostics"])
            omitted = role_result.get("failureDiagnosticsOmitted", 0)
            if omitted:
                lines.append(f"... {omitted} additional matching line(s) omitted")
            lines.append("```")

    lines += [
        "",
        "## Required Scene Coverage",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for case_id, scene in REQUIRED_SCENES.items():
        lines.append(f"| `{case_id}` | {scene['mode']} | `{scene['map']}` | {scene['purpose']} |")

    lines += [
        "",
        "## Shadow Correctness Coverage",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for case_id, scene in SHADOW_SCENES.items():
        lines.append(f"| `{case_id}` | {scene['mode']} | `{scene['map']}` | {scene['purpose']} |")

    lines += [
        "",
        "## Shadow Presets",
        "",
        "| Preset | Cvars |",
        "|---|---|",
    ]
    for preset, cvars in SHADOW_PRESETS.items():
        cvar_text = ", ".join(f"`{key} {value}`" for key, value in cvars.items()) or "stock defaults"
        lines.append(f"| `{preset}` | {cvar_text} |")

    report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report_json, report_md


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=tuple(PROFILE_DEFAULTS.keys()), default="smoke", help="Preset case/dimension profile.")
    parser.add_argument("--cases", default="", help="Comma-separated case ids. Overrides profile cases.")
    parser.add_argument("--tiers", default="", help="Comma-separated r_glTier values. Overrides profile tiers.")
    parser.add_argument("--maxfps", default="", help="Comma-separated com_maxfps values. Overrides profile values.")
    parser.add_argument("--swap-intervals", default="", help="Comma-separated r_swapInterval values. Overrides profile values.")
    parser.add_argument("--display-modes", default="", help="Display mode. openPREY agent validation is windowed-only.")
    parser.add_argument("--shadow-presets", default="", help="Comma-separated shadow presets. Use --list to inspect values.")
    parser.add_argument("--renderer", default="best", help="Value for r_renderer, usually best or arb2.")
    parser.add_argument("--benchmark-preset", default="baseline", help="Value for r_rendererBenchmarkPreset.")
    parser.add_argument("--modern-executor", action="store_true", help="Opt into r_rendererModernExecutor for gameplay benchmarking. Defaults off so ARB2/high-FPS baselines are not polluted by side-path work.")
    parser.add_argument("--gpu-timers", action="store_true", help="Enable GL timer queries during the sampled benchmark window. Defaults off for acceptance FPS runs because timer queries can perturb frame pacing.")
    parser.add_argument("--show-fps-overlay", action="store_true", help="Draw the in-game FPS overlay during the run. Defaults off so acceptance timings measure renderer/gameplay cost, not diagnostic text drawing.")
    parser.add_argument("--pacing-only", action="store_true", help="Measure frame pacing without enabling r_rendererMetrics or rendererBenchmarkCapture. Use this for high-FPS acceptance runs after diagnostic captures are clean.")
    parser.add_argument("--min-pacing-hz", type=float, default=0.0, help="Fail when the parsed frame-pacing snapshot falls below this average presentation rate.")
    parser.add_argument("--max-p95-ms", type=float, default=0.0, help="Fail when the parsed frame-pacing P95 exceeds this millisecond budget. Use 0 to disable.")
    parser.add_argument("--max-p99-ms", type=float, default=0.0, help="Fail when the parsed frame-pacing P99 exceeds this millisecond budget. Use 0 to disable.")
    parser.add_argument("--set-cvar", action="append", default=[], metavar="NAME=VALUE", help="Extra post-map cvar written into the generated benchmark cfg. Repeat for A/B diagnostics without extending the launch command line.")
    parser.add_argument("--set-launch-cvar", action="append", default=[], metavar="NAME=VALUE", help="Extra cvar applied on the openPREY launch command line before the map loads. Use for load-time renderer knobs such as vertex/index buffer caching.")
    parser.add_argument("--exec-command", action="append", default=[], metavar="COMMAND", help="Extra post-map console command written into the generated benchmark cfg. Repeat for targeted diagnostics such as flashlight impulses.")
    parser.add_argument("--autoexec-delay-ms", type=int, default=1000, help="Delay after active map draw before executing the generated benchmark cfg.")
    parser.add_argument("--settle-frames", type=int, default=360, help="Frames to wait after map/connect before sampling.")
    parser.add_argument("--sample-frames", type=int, default=600, help="Frames to sample before dumping metrics and screenshots.")
    parser.add_argument("--sample-msec", type=int, default=0, help="Real milliseconds to sample before dumping metrics and screenshots. Overrides --sample-frames when positive.")
    parser.add_argument("--timeout", type=int, default=180, help="Per-case process timeout in seconds.")
    parser.add_argument("--basepath", default=default_basepath(), help="Prey (2006) install/base path. Omit or set empty to use engine discovery.")
    parser.add_argument("--output-dir", default="", help="Report/output directory. Defaults to <repo>/.tmp/renderer-gameplay/<timestamp>.")
    parser.add_argument("--reference-dir", default="", help="Optional TGA reference screenshot root for deterministic image comparison.")
    parser.add_argument("--require-references", action="store_true", help="Fail captures when --reference-dir has no matching reference image.")
    parser.add_argument("--image-rms-threshold", type=float, default=2.0, help="Allowed RMS channel delta for TGA comparisons.")
    parser.add_argument("--image-max-threshold", type=int, default=24, help="Allowed maximum channel delta for TGA comparisons.")
    parser.add_argument("--mp-port", type=int, default=28110, help="Base listen-server port for MP runs.")
    parser.add_argument("--mp-client-delay", type=int, default=12, help="Seconds to wait before launching the MP loopback client.")
    parser.add_argument("--mp-client-delay-frames", type=int, default=480, help="Extra server frames before server-side capture in MP runs.")
    parser.add_argument("--limit", type=int, default=0, help="Limit generated specs, useful for bounded local smoke runs.")
    parser.add_argument("--dry-run", action="store_true", help="Write the planned command lines without launching openPREY.")
    parser.add_argument("--list", action="store_true", help="List profiles, cases, and shadow presets without running.")
    parsed = parser.parse_args(argv)
    try:
        profile_cvars = tuple(PROFILE_DEFAULTS[parsed.profile].get("cvars", ()))
        profile_launch_cvars = tuple(PROFILE_DEFAULTS[parsed.profile].get("launchCvars", ()))
        profile_exec_commands = tuple(PROFILE_DEFAULTS[parsed.profile].get("execCommands", ()))
        parsed.extra_cvars = profile_cvars + parse_extra_cvars(parsed.set_cvar)
        parsed.launch_cvars = profile_launch_cvars + parse_extra_cvars(parsed.set_launch_cvar)
        parsed.exec_commands = profile_exec_commands + parse_exec_commands(parsed.exec_command)
    except ValueError as exc:
        parser.error(str(exc))
    parsed.reference_dir_path = Path(parsed.reference_dir).resolve() if parsed.reference_dir else None
    return parsed


def print_list() -> None:
    print("Profiles:")
    for profile, defaults in PROFILE_DEFAULTS.items():
        count = (
            len(defaults["cases"])
            * len(defaults["tiers"])
            * len(defaults["maxfps"])
            * len(defaults["swap"])
            * len(defaults["display"])
            * len(defaults["shadows"])
        )
        profile_cvars = defaults.get("cvars", ())
        profile_exec_commands = defaults.get("execCommands", ())
        annotations: list[str] = []
        if profile_cvars:
            annotations.append("cvars " + ", ".join(f"{key}={value}" for key, value in profile_cvars))
        if profile_exec_commands:
            annotations.append(f"{len(profile_exec_commands)} scripted command(s)")
        annotation_text = " - " + "; ".join(annotations) if annotations else ""
        print(f"  {profile}: {count} generated case(s){annotation_text}")
    print("\nRequired gameplay cases:")
    for case_id, scene in REQUIRED_SCENES.items():
        print(f"  {case_id}: {scene['mode']} {scene['map']} - {scene['purpose']}")
    print("\nShadow correctness cases:")
    for case_id, scene in SHADOW_SCENES.items():
        print(f"  {case_id}: {scene['mode']} {scene['map']} - {scene['purpose']}")
    print("\nShadow presets:")
    for preset, cvars in SHADOW_PRESETS.items():
        cvar_text = ", ".join(f"{key}={value}" for key, value in cvars.items()) or "stock defaults"
        print(f"  {preset}: {cvar_text}")

def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.list:
        print_list()
        return 0

    root = repo_root()
    executable = find_client_executable(root)
    basepath = args.basepath
    if basepath and not Path(basepath).exists():
        print(f"warning: basepath does not exist, omitting fs_basepath: {basepath}", file=sys.stderr)
        basepath = ""
    if args.reference_dir_path is not None and not args.reference_dir_path.exists():
        raise FileNotFoundError(f"reference directory does not exist: {args.reference_dir_path}")

    specs = build_specs(args)
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    output_dir = Path(args.output_dir).resolve() if args.output_dir else root / ".tmp" / "renderer-gameplay" / timestamp
    output_dir.mkdir(parents=True, exist_ok=True)
    run_id = output_dir.name

    results: list[dict[str, Any]] = []
    for index, spec in enumerate(specs):
        print(f"running {spec.id} ({spec.mode} {spec.map_name})...", flush=True)
        try:
            if spec.mode == "MP":
                result = run_mp_spec(root, executable, output_dir, basepath, run_id, spec, index, args)
            else:
                result = run_sp_spec(root, executable, output_dir, basepath, run_id, spec, args)
        except Exception as exc:
            result = harness_failure_result(spec, exc)
            print(f"  fail ({type(exc).__name__}: {exc})", file=sys.stderr, flush=True)
        else:
            print(f"  {result['status']}", flush=True)
        results.append(result)

    metadata = {
        "generated": time.strftime("%Y-%m-%d %H:%M:%S %z"),
        "host": f"{platform.system()} {platform.release()} {platform.machine()}",
        "executable": str(executable),
        "basepath": basepath,
        "profile": args.profile,
        "dryRun": args.dry_run,
        "autoexecDelayMs": args.autoexec_delay_ms,
        "settleFrames": args.settle_frames,
        "sampleFrames": args.sample_frames,
        "sampleMsec": args.sample_msec,
        "minPacingHz": args.min_pacing_hz,
        "maxP95Ms": args.max_p95_ms,
        "maxP99Ms": args.max_p99_ms,
        "profileCvars": dict(PROFILE_DEFAULTS[args.profile].get("cvars", ())),
        "profileExecCommands": list(PROFILE_DEFAULTS[args.profile].get("execCommands", ())),
        "launchCvars": dict(args.launch_cvars),
        "execCommands": list(args.exec_commands),
    }
    report_json, report_md = write_reports(output_dir, results, metadata)
    print(f"wrote {report_md}")
    print(f"wrote {report_json}")

    if args.dry_run:
        return 0
    return 0 if all(result["status"] == "pass" for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
