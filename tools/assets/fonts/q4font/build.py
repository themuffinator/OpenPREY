"""Assemble one openQ4 TrueType face from a retail Quake 4 bitmap font."""
from __future__ import annotations

import math
import unicodedata
from dataclasses import dataclass, field, replace
from pathlib import Path as FilePath

import numpy as np
from PIL import Image
from fontTools.agl import UV2AGL
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

from .charset import (
	CYRILLIC_HOMOGLYPHS,
	DERIVED_FROM_LATIN,
	GREEK_HOMOGLYPHS,
	NOTO_ARABIC_BLOCKS,
	NOTO_HEBREW_BLOCKS,
	NOTO_SANS_BLOCKS,
	byte_to_unicode,
	unicode_ranges,
)
from .donor import get_face, solve_instance
from .fontdat import SourceFont, load_prey_source_font, load_source_font
from .grid import load_grid_font
from .path import Contour, Path2D
from .trace import TraceOptions, trace_coverage

UNITS_PER_EM = 2048
# FontBuilder otherwise stamps the current time into the ``head`` table.  These
# runtime assets are generated from fixed retail inputs, so a stable timestamp
# keeps a rebuild byte-for-byte reproducible.
DETERMINISTIC_TTF_TIMESTAMP = 3818534400  # 2025-01-01T00:00:00Z in the Mac epoch
# Effective pixel size a glyph is traced at. Sources at or above this are traced
# as they are; smaller ones are resampled up to roughly this before tracing.
DENSIFY_TARGET_SIZE = 64.0
VENDOR_ID = "DMPR"

# Combining marks, and where they sit relative to the letter they attach to.
ABOVE_MARKS = {0x0300, 0x0301, 0x0302, 0x0303, 0x0304, 0x0306, 0x0307, 0x0308,
               0x0309, 0x030A, 0x030B, 0x030C, 0x030F, 0x0311}
BELOW_MARKS = {0x0323, 0x0326, 0x0327, 0x0328, 0x032E, 0x0331}

# Marks the retail Latin-1 range already draws, and the precomposed glyph each
# one can be lifted out of.
MARK_DONORS_UPPER = {0x0300: "À", 0x0301: "Á", 0x0302: "Â", 0x0303: "Ã", 0x0308: "Ä", 0x030A: "Å"}
MARK_DONORS_LOWER = {0x0300: "à", 0x0301: "á", 0x0302: "â", 0x0303: "ã", 0x0308: "ä", 0x030A: "å"}


@dataclass
class FaceSpec:
	source: str
	family: str
	style: str = "Regular"
	extended: bool = True
	all_caps: bool = False
	description: str = ""
	# Set for the fixed-cell console sheet, which has no .fontdat beside it.
	grid_atlas: str | None = None
	# Prey uses a Doom 3-style .dat record and can distribute one face across
	# several atlas images; Quake 4's .fontdat record always has one image.
	prey_format: bool = False
	project_name: str = "openQ4"
	bitmap_attribution: str = "the Quake 4 bitmap fonts by Raven Software / id Software"


@dataclass
class GlyphEntry:
	name: str
	path: Path2D | None = None
	advance: int = 0
	components: list[tuple[str, float, float]] = field(default_factory=list)


@dataclass
class FaceMetrics:
	ascender: int
	descender: int
	cap_height: int
	x_height: int
	stem: float
	average_advance: float


def glyph_name(codepoint: int) -> str:
	name = UV2AGL.get(codepoint)
	if name:
		return name
	return f"uni{codepoint:04X}" if codepoint <= 0xFFFF else f"u{codepoint:06X}"


# ---------------------------------------------------------------------------
# geometry helpers
# ---------------------------------------------------------------------------


def scanline(path: Path2D, y: float) -> list[float]:
	hits: list[float] = []
	for polygon in path.flatten(3.0):
		count = len(polygon)
		for index in range(count):
			x0, y0 = polygon[index]
			x1, y1 = polygon[(index + 1) % count]
			if (y0 > y) == (y1 > y):
				continue
			t = (y - y0) / (y1 - y0)
			hits.append(x0 + t * (x1 - x0))
	return sorted(hits)


def contour_bounds(contour: Contour) -> tuple[float, float, float, float]:
	polygon = contour.flatten(1.0)
	xs = [p[0] for p in polygon]
	ys = [p[1] for p in polygon]
	return min(xs), min(ys), max(xs), max(ys)


def split_mark(accented: Path2D, base: Path2D, above: bool) -> Path2D | None:
	"""Lift the diacritic out of a precomposed glyph by discarding the base."""
	base_bounds = base.bounds()
	if base_bounds is None:
		return None
	mark = Path2D()
	for contour in accented.contours:
		x0, y0, x1, y1 = contour_bounds(contour)
		if above and y0 >= base_bounds[3] - 0.02 * UNITS_PER_EM:
			mark.contours.append(contour)
		elif not above and y1 <= base_bounds[1] + 0.02 * UNITS_PER_EM:
			mark.contours.append(contour)
	return mark if mark.contours else None


def flip_vertical(path: Path2D) -> Path2D:
	bounds = path.bounds()
	if bounds is None:
		return path
	centre = 0.5 * (bounds[1] + bounds[3])
	flipped = path.transformed(1.0, -1.0, 0.0, 2.0 * centre)
	# Mirroring reverses every contour's winding; put it back.
	return Path2D([contour.reverse() for contour in flipped.contours])


def flip_horizontal(path: Path2D, advance: float) -> Path2D:
	flipped = path.transformed(-1.0, 1.0, advance, 0.0)
	return Path2D([contour.reverse() for contour in flipped.contours])


# ---------------------------------------------------------------------------
# builder
# ---------------------------------------------------------------------------


class FaceBuilder:
	def __init__(self, spec: FaceSpec, source_dir: FilePath, donor_dir: FilePath, options: TraceOptions | None = None):
		self.spec = spec
		if spec.grid_atlas is not None:
			self.source: SourceFont = load_grid_font(source_dir / spec.grid_atlas, spec.source)
		elif spec.prey_format:
			self.source = load_prey_source_font(source_dir / spec.source, 48, spec.source)
		else:
			self.source = load_source_font(source_dir, spec.source, 48)
		# Small sources are densified before tracing.  The tracer places an edge
		# from the coverage value itself, treating a sample as the area of a
		# pixel cut by one edge.  That holds at 48 point, where a stem spans
		# several pixels, but not on the 16 pixel console cells where a single
		# pixel is usually cut by both sides of a stem at once - there the
		# assumption breaks down and contours come out lumpy.  Resampling first
		# separates the two edges into different pixels and restores it.
		self.densify = max(1, int(round(DENSIFY_TARGET_SIZE / max(self.source.point_size, 1.0))))
		# Distance thresholds in the tracer are source pixels standing in for
		# fractions of an em, so they follow the size actually being traced.
		self.options = (options or TraceOptions()).for_point_size(self.source.point_size * self.densify)
		if self.densify > 1:
			# The resample is itself a low-pass, so smoothing the contour again
			# only rounds off detail the source actually had.
			self.options = replace(self.options, smoothing_passes=0)
		self.donor_dir = donor_dir
		self.scale = UNITS_PER_EM / self.source.point_size
		self.glyphs: dict[str, GlyphEntry] = {}
		self.cmap: dict[int, str] = {}
		self.paths: dict[str, Path2D] = {}
		self.advances: dict[str, int] = {}
		self.marks_upper: dict[int, Path2D] = {}
		self.marks_lower: dict[int, Path2D] = {}
		self.report: dict[str, int] = {}

	# -- stage 1: the retail bitmaps ---------------------------------------

	def trace_source(self) -> None:
		notdef = GlyphEntry(".notdef", advance=int(round(0.5 * UNITS_PER_EM)))
		self.glyphs[".notdef"] = notdef

		for slot in self.source.glyphs:
			if slot.code < 32:
				continue
			codepoint = byte_to_unicode(slot.code)
			name = glyph_name(codepoint)
			# Round advances up rather than to nearest.  The engine measures
			# text with Ceil() on the advance, so an advance that lands a
			# fraction below the retail value loses a whole unit per glyph and
			# strings come out a pixel short of the layout the GUIs expect.
			# Overshoot is at most one font unit, or 0.02 of a point.
			advance = int(math.ceil(slot.advance * self.scale - 1e-6))

			if slot.code == 32:
				# Space occupies a small blank patch in the atlas, so it passes
				# the has-outline test; it is always emitted blank.  The retail
				# chain font also ships a zero advance for it at all three point
				# sizes, which collapses every gap in a line of text.  Every
				# other face uses exactly half an em, so fall back to that.
				if advance <= 0:
					advance = UNITS_PER_EM // 2
					self.report["space_repaired"] = 1
				self._add(name, codepoint, None, advance)
				continue

			if not slot.has_outline:
				if advance > 0:
					self._add(name, codepoint, None, advance)
				continue

			coverage = self.source.coverage(slot)
			traced = self._trace(coverage)
			if not traced.contours:
				# Some Prey slots (notably non-breaking space) serialize a useful
				# advance and UV rectangle even though their source page has no ink.
				# Preserve the blank advancing glyph: omitting it makes legacy
				# Windows-1252 text fall through to .notdef and changes layout.
				if advance > 0:
					self._add(name, codepoint, None, advance)
					self.report["blank_advancing_slots"] = self.report.get("blank_advancing_slots", 0) + 1
					continue
				# Other empty reserved slots have no layout meaning. Leaving those
				# absent lets a donor supply a real glyph where one is available.
				self.report["blank_slots"] = self.report.get("blank_slots", 0) + 1
				continue
			# Pixel space (y down, glyph-local) -> font units (y up, on the pen).
			path = traced.transformed(
				self.scale,
				-self.scale,
				slot.bearing_x * self.scale,
				slot.bearing_y * self.scale,
			)
			self._add(name, codepoint, path, advance)

		self.report["traced"] = len(self.paths)

	def _trace(self, coverage) -> Path2D:
		"""Trace one glyph, resampling first when the source is small."""
		if self.densify <= 1:
			return trace_coverage(coverage, self.options)
		height, width = coverage.shape
		if height == 0 or width == 0:
			return Path2D()
		# Bilinear is the right reconstruction here: the sheet is antialiased,
		# so the coverage ramp already encodes where the edge sits and the
		# resample just spreads it over enough samples for the tracer to use.
		big = np.asarray(
			Image.fromarray((np.clip(coverage, 0.0, 1.0) * 255.0).astype(np.uint8)).resize(
				(width * self.densify, height * self.densify), Image.BILINEAR
			),
			dtype=np.float32,
		) / 255.0
		traced = trace_coverage(big, self.options)
		return traced.transformed(1.0 / self.densify, 1.0 / self.densify)

	def _add(self, name: str, codepoint: int | None, path: Path2D | None, advance: int) -> None:
		entry = self.glyphs.get(name)
		if entry is None:
			entry = GlyphEntry(name, path, advance)
			self.glyphs[name] = entry
			if path is not None:
				self.paths[name] = path
			self.advances[name] = advance
		if codepoint is not None:
			self.cmap.setdefault(codepoint, name)

	# -- stage 2: measurements ---------------------------------------------

	def measure(self) -> FaceMetrics:
		cap = self._top_of("H") or self._top_of("A") or 0.62 * UNITS_PER_EM
		x_height = self._top_of("x") or self._top_of("o") or 0.48 * UNITS_PER_EM
		if self.spec.all_caps:
			x_height = cap

		stem = self._stem_of("I") or self._stem_of("H") or self._stem_of("l") or 0.1 * UNITS_PER_EM

		widths = [self.advances[glyph_name(ord(c))] for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ" if glyph_name(ord(c)) in self.advances]
		average = sum(widths) / len(widths) if widths else 0.6 * UNITS_PER_EM

		ascender = int(round(self.source.ascender * self.scale))
		descender = int(round(self.source.descender * self.scale))
		# Some retail faces report a zero descender because they are all caps;
		# fall back to what the outlines actually reach so the line box is sane.
		lowest = min((p.bounds()[1] for p in self.paths.values() if p.bounds()), default=0.0)
		highest = max((p.bounds()[3] for p in self.paths.values() if p.bounds()), default=cap)
		ascender = max(ascender, int(round(highest)))
		descender = max(descender, int(round(-lowest)))

		return FaceMetrics(ascender, descender, int(round(cap)), int(round(x_height)), stem, average)

	def _top_of(self, character: str) -> float | None:
		path = self.paths.get(glyph_name(ord(character)))
		if path is None:
			return None
		bounds = path.bounds()
		return bounds[3] if bounds else None

	def _stem_of(self, character: str) -> float | None:
		path = self.paths.get(glyph_name(ord(character)))
		if path is None:
			return None
		bounds = path.bounds()
		if bounds is None:
			return None
		hits = scanline(path, 0.5 * (bounds[1] + bounds[3]))
		if len(hits) < 2:
			return None
		return hits[1] - hits[0]

	# -- stage 3: diacritics ------------------------------------------------

	def extract_marks(self, metrics: FaceMetrics) -> None:
		for table, target, base_char in (
			(MARK_DONORS_UPPER, self.marks_upper, "A"),
			(MARK_DONORS_LOWER, self.marks_lower, "a"),
		):
			base = self.paths.get(glyph_name(ord(base_char)))
			if base is None:
				continue
			for mark_cp, accented_char in table.items():
				accented = self.paths.get(glyph_name(ord(accented_char)))
				if accented is None:
					continue
				mark = split_mark(accented, base, above=True)
				if mark is not None:
					target[mark_cp] = mark

		cedilla_base = self.paths.get(glyph_name(ord("C")))
		cedilla = self.paths.get(glyph_name(ord("Ç")))
		if cedilla_base is not None and cedilla is not None:
			extracted = split_mark(cedilla, cedilla_base, above=False)
			if extracted is not None:
				self.marks_upper[0x0327] = extracted
				self.marks_lower[0x0327] = extracted

		# Caron is a circumflex turned over; macron is a plain bar at the same
		# width.  Both are better derived from the face than imported.
		for target in (self.marks_upper, self.marks_lower):
			circumflex = target.get(0x0302)
			if circumflex is not None:
				target.setdefault(0x030C, flip_vertical(circumflex))
				bounds = circumflex.bounds()
				if bounds:
					thickness = max(metrics.stem * 0.8, 0.02 * UNITS_PER_EM)
					bar = Contour((bounds[0], bounds[3] - thickness))
					bar.line_to((bounds[2], bounds[3] - thickness))
					bar.line_to((bounds[2], bounds[3]))
					bar.line_to((bounds[0], bounds[3]))
					target.setdefault(0x0304, Path2D([bar]))
			acute = target.get(0x0301)
			if acute is not None:
				bounds = acute.bounds()
				if bounds:
					offset = (bounds[2] - bounds[0]) * 0.75
					doubled = Path2D(list(acute.contours) + list(acute.transformed(1.0, 1.0, offset, 0.0).contours))
					target.setdefault(0x030B, doubled)
			diaeresis = target.get(0x0308)
			if diaeresis is not None and len(diaeresis.contours) >= 2:
				single = Path2D([diaeresis.contours[0]])
				bounds_all = diaeresis.bounds()
				bounds_one = single.bounds()
				if bounds_all and bounds_one:
					shift = 0.5 * (bounds_all[0] + bounds_all[2]) - 0.5 * (bounds_one[0] + bounds_one[2])
					target.setdefault(0x0307, single.transformed(1.0, 1.0, shift, 0.0))

		self.report["marks"] = len(self.marks_upper) + len(self.marks_lower)

	def _place_mark(self, base_name: str, mark_cp: int, uppercase: bool) -> tuple[Path2D, float, float] | None:
		marks = self.marks_upper if uppercase else self.marks_lower
		mark = marks.get(mark_cp)
		reference = self.paths.get(glyph_name(ord("A" if uppercase else "a")))
		base = self.paths.get(base_name)
		if mark is None or reference is None or base is None:
			return None
		mark_bounds = mark.bounds()
		base_bounds = base.bounds()
		reference_bounds = reference.bounds()
		if not (mark_bounds and base_bounds and reference_bounds):
			return None

		dx = 0.5 * (base_bounds[0] + base_bounds[2]) - 0.5 * (mark_bounds[0] + mark_bounds[2])
		if mark_cp in BELOW_MARKS:
			dy = base_bounds[1] - reference_bounds[1]
		else:
			# Riding the base's own top means marks clear ascenders automatically.
			dy = base_bounds[3] - reference_bounds[3]
		return mark, dx, dy

	# -- stage 4: composites ------------------------------------------------

	def compose(self, codepoints: list[int]) -> set[int]:
		"""Build precomposed letters out of traced bases and extracted marks."""
		built: set[int] = set()
		for codepoint in codepoints:
			if codepoint in self.cmap:
				continue
			parts = _canonical_parts(codepoint)
			if not parts:
				continue
			base_cp, marks = parts[0], parts[1:]
			base_name = self.cmap.get(base_cp)
			if base_name is None or not marks:
				continue
			if not all(m in ABOVE_MARKS or m in BELOW_MARKS for m in marks):
				continue

			uppercase = unicodedata.category(chr(base_cp)) == "Lu"
			placements = []
			for mark_cp in marks:
				placed = self._place_mark(base_name, mark_cp, uppercase)
				if placed is None:
					break
				placements.append((mark_cp, placed))
			if len(placements) != len(marks):
				continue

			name = glyph_name(codepoint)
			combined = Path2D(list(self.paths[base_name].contours))
			for _, (mark, dx, dy) in placements:
				combined.contours.extend(mark.transformed(1.0, 1.0, dx, dy).contours)
			self._add(name, codepoint, combined, self.advances.get(base_name, 0))
			built.add(codepoint)
		self.report["composed"] = self.report.get("composed", 0) + len(built)
		return built

	# -- stage 5: shared shapes and donors ----------------------------------

	def alias_homoglyphs(self) -> None:
		count = 0
		for table in (CYRILLIC_HOMOGLYPHS, GREEK_HOMOGLYPHS):
			for codepoint, latin in table.items():
				if codepoint in self.cmap:
					continue
				name = latin if latin in self.glyphs else glyph_name(ord(latin)) if len(latin) == 1 else None
				if name and name in self.glyphs:
					self.cmap[codepoint] = name
					count += 1

		for codepoint, (source, operation) in DERIVED_FROM_LATIN.items():
			if codepoint in self.cmap:
				continue
			name = source if source in self.glyphs else (glyph_name(ord(source)) if len(source) == 1 else None)
			if not name or name not in self.paths:
				continue
			advance = self.advances.get(name, 0)
			path = flip_horizontal(self.paths[name], advance) if operation == "flip_x" else self.paths[name]
			self._add(glyph_name(codepoint), codepoint, path, advance)
			count += 1
		self.report["homoglyphs"] = count

	def synthesize_shapes(self, metrics: FaceMetrics) -> None:
		"""Draw the geometric and arrow symbols the Noto donor does not carry.

		Noto Sans ships almost nothing from the arrows, geometric-shape and
		misc-symbol blocks, yet those are exactly the characters a HUD reaches
		for.  They are pure geometry, so they are constructed here at the
		face's own weight rather than pulled from a fourth donor.
		"""
		if not self.spec.extended:
			return

		cap = float(metrics.cap_height)
		mid = 0.5 * (cap - metrics.stem)  # vertical centre of a symbol band
		size = 0.62 * cap
		low = mid - 0.5 * size
		high = mid + 0.5 * size
		left = 0.16 * cap
		right = left + size
		centre_x = 0.5 * (left + right)
		advance = int(round(size + 2.0 * left))
		count = 0

		def emit(codepoint: int, path: Path2D) -> None:
			nonlocal count
			if codepoint in self.cmap or not path.contours:
				return
			self._add(glyph_name(codepoint), codepoint, path, advance)
			count += 1

		def polygon(points: list[tuple[float, float]]) -> Path2D:
			contour = Contour(points[0])
			for point in points[1:]:
				contour.line_to(point)
			return Path2D([contour])

		def ellipse(cx: float, cy: float, rx: float, ry: float, reverse: bool = False) -> Contour:
			# Four quadratic arcs; the control offset is the standard circular
			# approximation for a quadratic quarter-arc.
			k = 0.5523
			contour = Contour((cx + rx, cy))
			contour.quad_to((cx + rx, cy + ry * k * 1.2), (cx, cy + ry))
			contour.quad_to((cx - rx * k * 1.2, cy + ry), (cx - rx, cy))
			contour.quad_to((cx - rx, cy - ry * k * 1.2), (cx, cy - ry))
			contour.quad_to((cx + rx * k * 1.2, cy - ry), (cx + rx, cy))
			return contour.reverse() if reverse else contour

		def ring(cx: float, cy: float, radius: float) -> Path2D:
			inner = radius - max(metrics.stem * 0.7, 0.03 * cap)
			shape = Path2D([ellipse(cx, cy, radius, radius)])
			if inner > 0.1 * radius:
				shape.contours.append(ellipse(cx, cy, inner, inner, reverse=True))
			return shape

		def frame(points: list[tuple[float, float]], inset: float) -> Path2D:
			outer = polygon(points)
			cx = sum(p[0] for p in points) / len(points)
			cy = sum(p[1] for p in points) / len(points)
			scale = 1.0 - inset
			shrunk = [(cx + (x - cx) * scale, cy + (y - cy) * scale) for x, y in points]
			inner = Contour(shrunk[0])
			for point in shrunk[1:]:
				inner.line_to(point)
			outer.contours.append(inner.reverse())
			return outer

		square = [(left, low), (right, low), (right, high), (left, high)]
		up = [(centre_x, high), (right, low), (left, low)]
		down = [(centre_x, low), (left, high), (right, high)]
		lefty = [(left, mid), (right, high), (right, low)]
		righty = [(right, mid), (left, low), (left, high)]

		emit(0x25A0, polygon(square))                       # black square
		emit(0x25A1, frame(square, 0.32))                   # white square
		emit(0x25AA, polygon(_scaled(square, 0.65)))        # small black square
		emit(0x25B2, polygon(up))
		emit(0x25B3, frame(up, 0.42))
		emit(0x25BC, polygon(down))
		emit(0x25BD, frame(down, 0.42))
		emit(0x25C0, polygon(lefty))
		emit(0x25C1, frame(lefty, 0.42))
		emit(0x25B6, polygon(righty))
		emit(0x25B7, frame(righty, 0.42))
		emit(0x25CF, Path2D([ellipse(centre_x, mid, 0.5 * size, 0.5 * size)]))
		emit(0x25CB, ring(centre_x, mid, 0.5 * size))
		emit(0x2022, Path2D([ellipse(centre_x, mid, 0.22 * size, 0.22 * size)]))
		emit(0x2605, polygon(_star(centre_x, mid, 0.55 * size, 0.24 * size)))
		emit(0x2606, frame(_star(centre_x, mid, 0.55 * size, 0.24 * size), 0.3))

		bar = max(metrics.stem * 0.85, 0.035 * cap)
		head = 0.34 * size
		for codepoint, direction in ((0x2190, "left"), (0x2192, "right"), (0x2191, "up"), (0x2193, "down")):
			emit(codepoint, _arrow(left, right, low, high, mid, centre_x, bar, head, direction))

		self.report["synthesized"] = count

	def import_donors(self, metrics: FaceMetrics) -> None:
		if not self.spec.extended:
			return

		ranges = unicode_ranges()
		targets: list[tuple[str, tuple[str, ...]]] = [
			("NotoSans-var.ttf", NOTO_SANS_BLOCKS),
			("NotoSansArabic-var.ttf", NOTO_ARABIC_BLOCKS),
			("NotoSansHebrew-var.ttf", NOTO_HEBREW_BLOCKS),
		]

		stem_ratio = metrics.stem / max(metrics.cap_height, 1)
		width_ratio = metrics.average_advance / max(metrics.cap_height, 1)
		imported = 0

		for filename, blocks in targets:
			path = self.donor_dir / filename
			if not path.exists():
				continue
			weight, width = solve_instance(stem_ratio, width_ratio, path)
			face = get_face(path, weight, width)
			self.report[f"instance_{filename.split('-')[0]}"] = f"{weight:.0f}/{width:.0f}"
			donor_cap = face.metrics.cap_height or 0.7 * face.upm
			scale = metrics.cap_height / donor_cap

			for block in blocks:
				low, high = ranges[block]
				for codepoint in range(low, high + 1):
					if codepoint in self.cmap or not face.has(codepoint):
						continue
					result = face.outline(codepoint)
					if result is None:
						continue
					outline, advance = result
					name = glyph_name(codepoint)
					if name in self.glyphs:
						self.cmap[codepoint] = name
						continue
					scaled = outline.transformed(scale, scale) if outline.contours else Path2D()
					self._add(name, codepoint, scaled if scaled.contours else None, int(round(advance * scale)))
					imported += 1

		self.report["donor"] = imported

	def force_monospace(self) -> None:
		"""Give every glyph the cell advance, for a fixed-cell source.

		The console and the loading screen step a fixed cell per character and
		ignore font advances entirely, so a proportional advance would be
		fiction.  It matters for the imported scripts, which arrive carrying the
		donor's proportional metrics.
		"""
		if self.spec.grid_atlas is None:
			return
		cell = int(round(self.source.point_size * self.scale))
		for entry in self.glyphs.values():
			if entry.name != ".notdef":
				entry.advance = cell
		for name in self.advances:
			self.advances[name] = cell
		self.report["monospaced"] = cell

	def fold_to_base(self) -> None:
		"""For faces with no extended coverage, point accents at their base rune."""
		if self.spec.extended:
			return
		count = 0
		for codepoint in range(0x00C0, 0x2000):
			if codepoint in self.cmap:
				continue
			parts = _canonical_parts(codepoint)
			if not parts:
				continue
			base = self.cmap.get(parts[0])
			if base:
				self.cmap[codepoint] = base
				count += 1
		self.report["folded"] = count

	# -- stage 6: emit ------------------------------------------------------

	def emit(self, destination: FilePath, metrics: FaceMetrics, version: str) -> None:
		order = [".notdef"] + sorted(name for name in self.glyphs if name != ".notdef")
		builder = FontBuilder(UNITS_PER_EM, isTTF=True)
		builder.setupGlyphOrder(order)
		builder.setupCharacterMap(self.cmap)

		pen_glyphs = {}
		hmtx = {}
		for name in order:
			entry = self.glyphs[name]
			pen = TTGlyphPen(None)
			if entry.path is not None:
				entry.path.draw(pen)
			glyph = pen.glyph()
			pen_glyphs[name] = glyph
			left = 0
			bounds = entry.path.bounds() if entry.path else None
			if bounds:
				left = int(round(bounds[0]))
			hmtx[name] = (max(0, entry.advance), left)

		builder.setupGlyf(pen_glyphs)
		builder.setupHorizontalMetrics(hmtx)
		builder.setupHorizontalHeader(ascent=metrics.ascender, descent=-metrics.descender, lineGap=0)

		full = f"{self.spec.family} {self.spec.style}"
		builder.setupNameTable({
			"familyName": self.spec.family,
			"styleName": self.spec.style,
			"uniqueFontIdentifier": f"DarkMatter Productions: {full}: {self.spec.project_name} {version}",
			"fullName": full,
			"version": f"Version {version}",
			"psName": f"{self.spec.family.replace(' ', '')}-{self.spec.style}",
			"designer": "DarkMatter Productions",
			"description": self.spec.description,
			"manufacturer": "DarkMatter Productions",
			"licenseDescription": (
				f"Latin outlines are traced from {self.spec.bitmap_attribution}. "
				"Glyphs for scripts outside that source are "
				"derived from the Noto fonts, (c) Google, licensed under the SIL Open Font License 1.1."
			),
		})
		builder.setupOS2(
			sTypoAscender=metrics.ascender,
			sTypoDescender=-metrics.descender,
			sTypoLineGap=0,
			usWinAscent=metrics.ascender,
			usWinDescent=metrics.descender,
			sCapHeight=metrics.cap_height,
			sxHeight=metrics.x_height,
			achVendID=VENDOR_ID,
			fsType=0,
		)
		builder.setupPost()
		builder.font["head"].created = DETERMINISTIC_TTF_TIMESTAMP
		builder.font["head"].modified = DETERMINISTIC_TTF_TIMESTAMP
		builder.font.recalcTimestamp = False
		destination.parent.mkdir(parents=True, exist_ok=True)
		builder.save(str(destination))


def _scaled(points: list[tuple[float, float]], factor: float) -> list[tuple[float, float]]:
	cx = sum(p[0] for p in points) / len(points)
	cy = sum(p[1] for p in points) / len(points)
	return [(cx + (x - cx) * factor, cy + (y - cy) * factor) for x, y in points]


def _star(cx: float, cy: float, outer: float, inner: float) -> list[tuple[float, float]]:
	import math

	points: list[tuple[float, float]] = []
	for index in range(10):
		radius = outer if index % 2 == 0 else inner
		angle = math.pi / 2.0 + index * math.pi / 5.0
		points.append((cx + radius * math.cos(angle), cy + radius * math.sin(angle)))
	return points


def _arrow(
	left: float,
	right: float,
	low: float,
	high: float,
	mid: float,
	centre_x: float,
	bar: float,
	head: float,
	direction: str,
) -> Path2D:
	"""A shafted arrow built as one closed outline."""
	half = 0.5 * bar
	wing = 0.55 * (high - low)
	if direction in ("left", "right"):
		sign = -1.0 if direction == "left" else 1.0
		tip = right if direction == "right" else left
		tail = left if direction == "right" else right
		base = tip - sign * head
		points = [
			(tip, mid),
			(base, mid + wing * 0.5),
			(base, mid + half),
			(tail, mid + half),
			(tail, mid - half),
			(base, mid - half),
			(base, mid - wing * 0.5),
		]
	else:
		sign = 1.0 if direction == "up" else -1.0
		tip = high if direction == "up" else low
		tail = low if direction == "up" else high
		base = tip - sign * head
		points = [
			(centre_x, tip),
			(centre_x + wing * 0.5, base),
			(centre_x + half, base),
			(centre_x + half, tail),
			(centre_x - half, tail),
			(centre_x - half, base),
			(centre_x - wing * 0.5, base),
		]

	contour = Contour(points[0])
	for point in points[1:]:
		contour.line_to(point)
	path = Path2D([contour])
	# Winding depends on the direction the points were emitted in; normalise.
	if contour.signed_area() < 0:
		path = Path2D([contour.reverse()])
	return path


def _canonical_parts(codepoint: int) -> list[int]:
	"""Fully decompose a codepoint, keeping only canonical decompositions."""
	decomposition = unicodedata.decomposition(chr(codepoint))
	if not decomposition or decomposition.startswith("<"):
		return []
	parts = [int(token, 16) for token in decomposition.split()]
	result: list[int] = []
	for index, part in enumerate(parts):
		if index == 0:
			nested = _canonical_parts(part)
			result.extend(nested if nested else [part])
		else:
			result.append(part)
	return result
