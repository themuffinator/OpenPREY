"""Rebuild openPREY's TrueType faces from retail Prey bitmap fonts.

Usage:
    python build_openprey_fonts.py --source .tmp/prey-fontsrc
        --donors .tmp/fontdonors --output content/basepr/pak0/fonts
        [--faces english,menu,alien,bigchars]

The source directory is populated by ``extract_source_fonts.py``.  The output
file names deliberately match the paths used by the renderer after it resolves
``fonts``, ``fonts/menu`` and ``fonts/alien`` through ``fonts/english``.
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from q4font.build import FaceBuilder, FaceSpec
from q4font.charset import unicode_ranges
from q4font.trace import TraceOptions

VERSION = "1.000"
PREY_BITMAP_ATTRIBUTION = "the Prey (2006) bitmap fonts by Human Head Studios and 3D Realms"

FACES: dict[str, FaceSpec] = {
    "english": FaceSpec(
        source="fonts/english",
        family="openPREY Default",
        description="Prey's default UI and HUD face.",
        prey_format=True,
        project_name="openPREY",
        bitmap_attribution=PREY_BITMAP_ATTRIBUTION,
    ),
    "menu": FaceSpec(
        source="fonts/english/menu",
        family="openPREY Menu",
        description="Prey's menu face.",
        prey_format=True,
        project_name="openPREY",
        bitmap_attribution=PREY_BITMAP_ATTRIBUTION,
    ),
    "alien": FaceSpec(
        source="fonts/english/alien",
        family="openPREY Alien",
        all_caps=True,
        extended=False,
        description="Prey's alien glyph face; Latin characters map to its retail symbols.",
        prey_format=True,
        project_name="openPREY",
        bitmap_attribution=PREY_BITMAP_ATTRIBUTION,
    ),
    "bigchars": FaceSpec(
        source="textures/gfx/bigchars",
        family="openPREY BigChars",
        grid_atlas="textures/gfx/bigchars.tga",
        description="Prey's fixed-cell console and loading-screen face.",
        project_name="openPREY",
        bitmap_attribution=PREY_BITMAP_ATTRIBUTION,
    ),
}


def composable_codepoints(second_pass: bool = False) -> list[int]:
    """Everything worth attempting as base plus mark, in a stable order."""
    ranges = unicode_ranges()
    blocks = ("greek", "cyrillic", "cyrillic_supp") if second_pass else ("latin_ext_a", "latin_ext_b", "latin_ext_add")
    wanted: list[int] = []
    for block in blocks:
        low, high = ranges[block]
        wanted.extend(range(low, high + 1))
    return wanted


def build_face(key: str, spec: FaceSpec, source: Path, donors: Path, output: Path) -> dict:
    started = time.perf_counter()
    builder = FaceBuilder(spec, source, donors, TraceOptions())

    builder.trace_source()
    metrics = builder.measure()
    builder.extract_marks(metrics)
    builder.compose(composable_codepoints())
    builder.alias_homoglyphs()
    builder.compose(composable_codepoints(second_pass=True))
    builder.synthesize_shapes(metrics)
    builder.import_donors(metrics)
    builder.force_monospace()
    builder.fold_to_base()

    destination = output / f"{key}.ttf"
    builder.emit(destination, metrics, VERSION)

    elapsed = time.perf_counter() - started
    report = dict(builder.report)
    report.update(
        {
            "file": destination.name,
            "glyphs": len(builder.glyphs),
            "mapped": len(builder.cmap),
            "cap": metrics.cap_height,
            "xheight": metrics.x_height,
            "ascender": metrics.ascender,
            "descender": metrics.descender,
            "seconds": round(elapsed, 1),
            "bytes": destination.stat().st_size,
        }
    )
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--donors", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--faces", default="")
    arguments = parser.parse_args()

    keys = [key.strip() for key in arguments.faces.split(",") if key.strip()] or list(FACES)
    unknown = [key for key in keys if key not in FACES]
    if unknown:
        parser.error(f"unknown face(s): {', '.join(unknown)}")

    arguments.output.mkdir(parents=True, exist_ok=True)
    for key in keys:
        report = build_face(key, FACES[key], arguments.source, arguments.donors, arguments.output)
        summary = " ".join(f"{name}={value}" for name, value in report.items())
        print(f"{key:9s} {summary}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
