"""Extract every retail Prey bitmap font needed by the TTF generator.

Prey's ``fonts/english`` data uses the original Doom 3 record format: each
``fontImage_<size>.dat`` points at one or more atlas pages.  ``bigchars`` is a
separate 16x16 console sheet under ``textures/gfx``.  The source files live in
the game's pk4 files, so no game code needs to run to rebuild the derived TTFs.

Usage:
    python extract_source_fonts.py --install "C:/.../Prey" --output .tmp/prey-fontsrc
"""
from __future__ import annotations

import argparse
from zipfile import BadZipFile, ZipFile, ZipInfo
from pathlib import Path

FONT_PREFIX = "fonts/english/"
BIGCHARS_PATH = "textures/gfx/bigchars.tga"


def _read_entry(bundle: ZipFile, entry: ZipInfo) -> bytes:
    """Read an entry from retail archives that disagree on slash direction.

    Some Prey pk4 central directories use ``/`` while their local headers use
    ``\\``.  Python correctly rejects that malformed pairing by default; the
    compression stream remains valid, so temporarily matching the local header
    lets us extract it without loosening validation for any other error.
    """
    try:
        with bundle.open(entry) as source:
            return source.read()
    except BadZipFile as error:
        if "differ" not in str(error):
            raise
        original_name = entry.orig_filename
        entry.orig_filename = original_name.replace("/", "\\")
        try:
            with bundle.open(entry) as source:
                return source.read()
        finally:
            entry.orig_filename = original_name


def _is_wanted(path: str) -> bool:
    return (
        path.startswith(FONT_PREFIX) and path.endswith((".dat", ".tga"))
    ) or path == BIGCHARS_PATH


def _base_directory(install: Path) -> Path:
    """Accept either the Prey install root or its ``base`` directory."""
    nested = install / "base"
    return nested if nested.is_dir() else install


def extract(install: Path, output: Path) -> int:
	output.mkdir(parents=True, exist_ok=True)
	base = _base_directory(install)
	found = 0

	# Later archives override earlier ones, matching the engine's virtual file
	# system.  Keep the original relative paths: default, menu and alien share
	# the same fontImage_*.dat names, so flattening would corrupt the source.
	for archive in sorted(base.glob("*.pk4")):
		try:
			bundle = ZipFile(archive)
		except BadZipFile:
			print(f"  skipping unreadable archive: {archive.name}")
			continue

		with bundle:
			for entry in bundle.infolist():
				normalized = entry.filename.replace("\\", "/").lower()
				if not _is_wanted(normalized):
					continue
				destination = output / normalized
				destination.parent.mkdir(parents=True, exist_ok=True)
				with open(destination, "wb") as target:
					target.write(_read_entry(bundle, entry))
				found += 1

	if found == 0:
		return 0

	# The TTF builder consumes the four authoritative 48px sources below.  Do
	# not silently produce a partial set: a missing page would otherwise appear
	# much later as a blank glyph range.
	required = (
		"fonts/english/fontImage_48.dat",
		"fonts/english/menu/fontImage_48.dat",
		"fonts/english/alien/fontImage_48.dat",
		BIGCHARS_PATH,
	)
	missing = [path for path in required if not (output / path).is_file()]
	if missing:
		raise RuntimeError("retail Prey font extraction is incomplete: " + ", ".join(missing))
	return found


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--install", required=True, type=Path, help="path to the Prey install root or base directory")
	parser.add_argument("--output", required=True, type=Path)
	arguments = parser.parse_args()

	if not arguments.install.is_dir():
		parser.error(f"not a directory: {arguments.install}")

	found = extract(arguments.install, arguments.output)
	if found == 0:
		print(f"No retail Prey font entries found under {_base_directory(arguments.install)}")
		return 1

	print(f"extracted {found} files to {arguments.output}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
