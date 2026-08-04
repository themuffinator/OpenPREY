"""Readers for the retail bitmap-font metrics and atlas pages.

A ``.fontdat`` is a flat little-endian record: 256 glyphs of nine floats
(width, height, horiAdvance, horiBearingX, horiBearingY, s, t, s2, t2) followed
by four font-wide floats (pointSize, fontHeight, ascender, descender) and a
four byte serialised material pointer.  Every measurement is in pixels at the
file's point size, and the s/t pairs are normalised atlas coordinates.

The atlas itself is an RGBA TGA whose colour channels are a constant white; the
glyph coverage lives entirely in the alpha channel.  The module also supports
Prey's original Doom 3-style ``.dat`` records and their per-glyph atlas pages.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

GLYPHS_PER_FONT = 256
FONTDAT_SIZE = GLYPHS_PER_FONT * 9 * 4 + 4 * 4 + 4


@dataclass
class SourceGlyph:
	"""One glyph slot as recorded in a ``.fontdat``."""

	code: int
	width: float
	height: float
	advance: float
	bearing_x: float
	bearing_y: float
	s: float
	t: float
	s2: float
	t2: float
	# Most Quake 4 faces use one atlas.  Prey serialises the source material
	# with each glyph, so the same font can span several atlas pages.
	atlas: str | None = None

	@property
	def has_outline(self) -> bool:
		return self.width > 0.0 and self.height > 0.0 and self.s2 > self.s and self.t2 > self.t


@dataclass
class SourceFont:
	"""A single point size of one retail bitmap font."""

	name: str
	point_size: float
	font_height: float
	ascender: float
	descender: float
	glyphs: list[SourceGlyph]
	alpha: np.ndarray  # (h, w) uint8 coverage
	atlas_pages: dict[str, np.ndarray] | None = None

	@property
	def atlas_width(self) -> int:
		return int(self.alpha.shape[1])

	@property
	def atlas_height(self) -> int:
		return int(self.alpha.shape[0])

	def coverage(self, glyph: SourceGlyph) -> np.ndarray:
		"""Return the glyph's coverage mask as float32 in ``[0, 1]``.

		The atlas rectangle is snapped to whole texels the same way the engine
		does when it samples the atlas, so the returned block matches what the
		bitmap renderer actually puts on screen.
		"""
		atlas = self.alpha
		if glyph.atlas is not None and self.atlas_pages is not None:
			atlas = self.atlas_pages.get(glyph.atlas, atlas)
		atlas_height, atlas_width = atlas.shape
		x0 = int(round(glyph.s * atlas_width))
		x1 = int(round(glyph.s2 * atlas_width))
		y0 = int(round(glyph.t * atlas_height))
		y1 = int(round(glyph.t2 * atlas_height))
		x0 = max(0, min(atlas_width, x0))
		x1 = max(0, min(atlas_width, x1))
		y0 = max(0, min(atlas_height, y0))
		y1 = max(0, min(atlas_height, y1))
		if x1 <= x0 or y1 <= y0:
			return np.zeros((0, 0), dtype=np.float32)
		return atlas[y0:y1, x0:x1].astype(np.float32) / 255.0


def read_fontdat(path: Path) -> tuple[list[SourceGlyph], dict[str, float]]:
	raw = path.read_bytes()
	if len(raw) != FONTDAT_SIZE:
		raise ValueError(f"{path}: expected {FONTDAT_SIZE} bytes, found {len(raw)}")

	metrics = struct.unpack("<%df" % (GLYPHS_PER_FONT * 9), raw[: GLYPHS_PER_FONT * 9 * 4])
	glyphs = []
	for index in range(GLYPHS_PER_FONT):
		block = metrics[index * 9 : index * 9 + 9]
		glyphs.append(SourceGlyph(index, *block))

	tail = GLYPHS_PER_FONT * 9 * 4
	point_size, font_height, ascender, descender = struct.unpack("<4f", raw[tail : tail + 16])
	return glyphs, {
		"point_size": point_size,
		"font_height": font_height,
		"ascender": ascender,
		"descender": descender,
	}


def load_source_font(directory: Path, name: str, size: int) -> SourceFont:
	glyphs, header = read_fontdat(directory / f"{name}_{size}.fontdat")
	image = Image.open(directory / f"{name}_{size}.tga")
	if image.mode != "RGBA":
		image = image.convert("RGBA")
	alpha = np.asarray(image)[:, :, 3].copy()
	return SourceFont(
		name=name,
		point_size=header["point_size"],
		font_height=header["font_height"],
		ascender=header["ascender"],
		descender=header["descender"],
		glyphs=glyphs,
		alpha=alpha,
	)


# Prey (2006) stores the original Doom 3 glyph layout.  Each record contains
# seven integer metrics, four UV floats, a serialized pointer, and the name of
# the atlas page material.  The pointer is process-local data from the retail
# renderer and is intentionally ignored.
PREY_GLYPH_RECORD = struct.Struct("<7i4fi32s")
PREY_FONTDAT_SIZE = GLYPHS_PER_FONT * PREY_GLYPH_RECORD.size + 4 + 64


def _prey_page_name(shader_name: bytes) -> str:
	"""Return the case-insensitive basename of a serialized Prey material."""
	decoded = shader_name.split(b"\0", 1)[0].decode("latin-1").replace("\\", "/")
	return decoded.rsplit("/", 1)[-1].casefold()


def _load_prey_atlas_pages(directory: Path) -> dict[str, np.ndarray]:
	pages: dict[str, np.ndarray] = {}
	for path in directory.glob("*.tga"):
		image = Image.open(path)
		if image.mode != "RGBA":
			image = image.convert("RGBA")
		pages[path.name.casefold()] = np.asarray(image)[:, :, 3].copy()
	if not pages:
		raise ValueError(f"{directory}: no Prey font atlas pages found")
	return pages


def _find_casefold_file(directory: Path, name: str) -> Path:
	"""Find a retail asset even when an extractor normalised its filename."""
	wanted = name.casefold()
	for path in directory.iterdir():
		if path.is_file() and path.name.casefold() == wanted:
			return path
	raise FileNotFoundError(directory / name)


def load_prey_source_font(directory: Path, size: int, name: str) -> SourceFont:
	"""Load one Prey font size and every atlas page referenced by its glyphs."""
	path = _find_casefold_file(directory, f"fontImage_{size}.dat")
	raw = path.read_bytes()
	if len(raw) != PREY_FONTDAT_SIZE:
		raise ValueError(f"{path}: expected {PREY_FONTDAT_SIZE} bytes, found {len(raw)}")

	pages = _load_prey_atlas_pages(directory)
	glyphs: list[SourceGlyph] = []
	ascender = 0.0
	descender = 0.0
	for code in range(GLYPHS_PER_FONT):
		values = PREY_GLYPH_RECORD.unpack_from(raw, code * PREY_GLYPH_RECORD.size)
		height, top, _bottom, _pitch, x_skip, image_width, image_height = values[:7]
		s, t, s2, t2 = values[7:11]
		page = _prey_page_name(values[12])
		if page and page not in pages:
			raise ValueError(f"{path}: glyph {code} references missing atlas page {page}")
		glyphs.append(
			SourceGlyph(
				code=code,
				width=float(image_width),
				height=float(image_height),
				advance=float(x_skip),
				bearing_x=0.0,
				bearing_y=float(top),
				s=s,
				t=t,
				s2=s2,
				t2=t2,
				atlas=page or None,
			)
		)
		ascender = max(ascender, float(top))
		descender = max(descender, float(image_height - top))

	# The last 68 bytes are retail glyphScale and fontName fields.  They do not
	# affect the screen-space metrics reconstructed above.
	first_page = next(iter(pages.values()))
	return SourceFont(
		name=name,
		point_size=float(size),
		font_height=ascender + descender,
		ascender=ascender,
		descender=descender,
		glyphs=glyphs,
		alpha=first_page,
		atlas_pages=pages,
	)
