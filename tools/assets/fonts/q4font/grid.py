"""Reader for a fixed-cell console atlas (``bigchars``).

Unlike the ``.fontdat`` fonts there is no metrics file: ``bigchars`` is a plain
16x16 grid of 16x16 pixel cells indexed by Windows-1252 byte, and the engine
draws each character by slicing the cell straight out of the sheet.  All the
metrics therefore have to be recovered from the pixels.

The result is presented through the same interface as a ``.fontdat`` source, so
the tracer and font builder do not care which kind of atlas a face came from.
"""
from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image

from .fontdat import SourceFont, SourceGlyph

GRID_COLUMNS = 16
GRID_ROWS = 16

# Coverage below this is atlas noise rather than ink.
INK_THRESHOLD = 0.06


def _cell(alpha: np.ndarray, code: int, cell_size: int) -> np.ndarray:
	row, column = code >> 4, code & 15
	return alpha[row * cell_size : (row + 1) * cell_size, column * cell_size : (column + 1) * cell_size]


def _ink_bounds(patch: np.ndarray) -> tuple[int, int, int, int] | None:
	rows, columns = np.where(patch > INK_THRESHOLD * 255.0)
	if len(rows) == 0:
		return None
	return int(columns.min()), int(rows.min()), int(columns.max()), int(rows.max())


def _find_baseline(alpha: np.ndarray, cell_size: int) -> int:
	"""Locate the baseline row inside a cell from where the capitals sit.

	Capitals rest on the baseline by definition, so the row just past the
	lowest capital ink is it.  Falling back to three quarters of the cell keeps
	a damaged sheet from producing nonsense.
	"""
	bottoms = []
	for character in "ABCDEFGHIJKLMNOPRSTUVWXYZ":
		bounds = _ink_bounds(_cell(alpha, ord(character), cell_size))
		if bounds is not None:
			bottoms.append(bounds[3])
	if not bottoms:
		return int(round(cell_size * 0.75))
	# The most common capital bottom, so one odd glyph cannot shift the baseline.
	values, counts = np.unique(np.array(bottoms), return_counts=True)
	return int(values[int(np.argmax(counts))]) + 1


def load_grid_font(path: Path, name: str) -> SourceFont:
	image = Image.open(path)
	if image.mode != "RGBA":
		image = image.convert("RGBA")
	alpha = np.asarray(image)[:, :, 3].copy()

	if alpha.shape[0] % GRID_ROWS or alpha.shape[1] % GRID_COLUMNS:
		raise ValueError(f"{path}: {alpha.shape[1]}x{alpha.shape[0]} is not a {GRID_COLUMNS}x{GRID_ROWS} grid")
	cell_size = alpha.shape[1] // GRID_COLUMNS
	if cell_size != alpha.shape[0] // GRID_ROWS:
		raise ValueError(f"{path}: cells are not square")

	baseline = _find_baseline(alpha, cell_size)

	glyphs: list[SourceGlyph] = []
	for code in range(GRID_COLUMNS * GRID_ROWS):
		patch = _cell(alpha, code, cell_size)
		bounds = _ink_bounds(patch)
		if bounds is None:
			# Blank cell. The console still steps a full cell for it, so the
			# advance is the cell width whether or not anything is drawn.
			glyphs.append(SourceGlyph(code, 0.0, 0.0, float(cell_size), 0.0, 0.0, 0.0, 0.0, 0.0, 0.0))
			continue

		x0, y0, x1, y1 = bounds
		row, column = code >> 4, code & 15
		atlas_x0 = column * cell_size + x0
		atlas_y0 = row * cell_size + y0
		width = x1 - x0 + 1
		height = y1 - y0 + 1

		glyphs.append(
			SourceGlyph(
				code=code,
				width=float(width),
				height=float(height),
				advance=float(cell_size),
				bearing_x=float(x0),
				bearing_y=float(baseline - y0),
				s=atlas_x0 / alpha.shape[1],
				t=atlas_y0 / alpha.shape[0],
				s2=(atlas_x0 + width) / alpha.shape[1],
				t2=(atlas_y0 + height) / alpha.shape[0],
			)
		)

	return SourceFont(
		name=name,
		point_size=float(cell_size),
		font_height=float(cell_size),
		ascender=float(baseline),
		descender=float(cell_size - baseline),
		glyphs=glyphs,
		alpha=alpha,
	)
