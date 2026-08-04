"""Sub-pixel autotracer for the retail Quake 4 glyph coverage masks.

The retail fonts are geometric: almost every letterform is built from axis
aligned stems and a handful of diagonals or arcs.  A naive "threshold and walk
the pixels" trace produces staircased stems that look wrong the moment the
glyph is scaled up, so this module reconstructs the original geometry instead:

1. Marching squares over the antialiased coverage recovers the 50% iso-contour
   with sub-pixel precision.
2. Corners are detected from the turning angle, and the contour is split into
   runs between them.
3. Runs that are straight are replaced by exact lines; runs that are near
   horizontal or vertical are snapped to the axis, and their offsets are
   clustered across the glyph so both edges of a stem share one coordinate.
4. Adjacent lines are intersected so corners come back sharp instead of
   rounded off by the antialiasing.
5. Whatever is genuinely curved is fitted with quadratic Beziers.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, replace

import numpy as np

from .path import Contour, Path2D, Point


@dataclass
class TraceOptions:
	level: float = 0.5
	corner_angle_deg: float = 32.0
	corner_window: float = 1.6
	line_tolerance: float = 0.11
	curve_tolerance: float = 0.05
	axis_angle_deg: float = 5.0
	axis_cluster: float = 0.42
	min_line_length: float = 0.9
	max_corner_extension: float = 2.6
	corner_trim: float = 1.1
	straightness_ratio: float = 0.018
	smoothing_passes: int = 1
	# Each subdivision level halves a run and re-fits by least squares, so the
	# cap doubles as a noise filter: shallow splits average the sampling noise
	# away, deep ones start reproducing it as scalloping.
	max_curve_depth: int = 5

	def for_point_size(self, point_size: float, reference: float = 48.0) -> "TraceOptions":
		"""Rescale the distance thresholds for a source of a different size.

		Every distance here is in source pixels but means a fraction of an em,
		and the defaults are tuned against the 48 point atlases.  A 16 pixel
		console cell needs them scaled to match, or a corner trim of 1.1px
		would swallow most of a glyph.
		"""
		if point_size <= 0.0 or abs(point_size - reference) < 1e-6:
			return self
		factor = point_size / reference
		return replace(
			self,
			corner_window=self.corner_window * factor,
			line_tolerance=self.line_tolerance * factor,
			curve_tolerance=self.curve_tolerance * factor,
			axis_cluster=self.axis_cluster * factor,
			min_line_length=self.min_line_length * factor,
			max_corner_extension=self.max_corner_extension * factor,
			corner_trim=self.corner_trim * factor,
		)


# ---------------------------------------------------------------------------
# marching squares
# ---------------------------------------------------------------------------

# Segment table keyed by the corner mask (bit0 TL, bit1 TR, bit2 BR, bit3 BL).
# Each entry lists (from_edge, to_edge) pairs using edge names t/r/b/l.
_SEGMENTS: dict[int, tuple[tuple[str, str], ...]] = {
	0: (),
	1: (("l", "t"),),
	2: (("t", "r"),),
	3: (("l", "r"),),
	4: (("r", "b"),),
	6: (("t", "b"),),
	7: (("l", "b"),),
	8: (("b", "l"),),
	9: (("b", "t"),),
	11: (("b", "r"),),
	12: (("r", "l"),),
	13: (("r", "t"),),
	14: (("t", "l"),),
	15: (),
}


def _interpolate(value_a: float, value_b: float, level: float) -> float:
	span = value_b - value_a
	if abs(span) < 1e-12:
		return 0.5
	t = (level - value_a) / span
	return min(1.0, max(0.0, t))


# A coverage sample is the exact area of a unit pixel that falls inside the
# glyph, so it localises the edge far better than interpolating between two
# samples does.  For an edge with unit normal n, coverage ramps from 0 to 1
# across a perpendicular band of width |nx| + |ny|, which means a sample of
# value c sits at signed distance (c - 0.5) * band from the edge.  Interpolating
# instead - especially against a saturated 0.0 or 1.0 neighbour - biases every
# edge outward by up to ~0.08px, which shows up as a halo around every stem.
def _crossing(
	value_a: float,
	value_b: float,
	normal_a: tuple[float, float],
	normal_b: tuple[float, float],
	edge: tuple[float, float],
	level: float,
) -> float:
	"""Parameter in ``[0, 1]`` along a grid edge where the outline crosses."""
	estimates: list[float] = []
	weights: list[float] = []

	for value, normal, base in ((value_a, normal_a, 0.0), (value_b, normal_b, 1.0)):
		if not (1e-4 < value < 1.0 - 1e-4):
			continue
		nx, ny = normal
		if nx == 0.0 and ny == 0.0:
			continue
		along = nx * edge[0] + ny * edge[1]
		if abs(along) < 0.35:
			# The edge runs nearly parallel to the outline; the projection is
			# ill-conditioned, so let the other endpoint decide.
			continue
		band = abs(nx) + abs(ny)
		distance = (value - level) * band
		estimates.append(base - distance / along)
		weights.append(1.0)

	if not estimates:
		return _interpolate(value_a, value_b, level)

	total = sum(weights)
	t = sum(e * w for e, w in zip(estimates, weights)) / total
	return min(0.999, max(0.001, t))


def marching_squares(field: np.ndarray, level: float = 0.5) -> list[list[Point]]:
	"""Extract closed iso-contours from a coverage field.

	The field is padded with zeros so shapes touching the border still close.
	Returned coordinates are in pixel space, where the centre of source pixel
	``(row, col)`` sits at ``(col + 0.5, row + 0.5)``.
	"""
	if field.size == 0:
		return []

	padded = np.pad(field.astype(np.float64), 2, mode="constant", constant_values=0.0)
	rows, cols = padded.shape
	inside = padded > level

	# Central-difference gradient, pointing from outside to inside.  It gives
	# each sample an outline normal, which is what makes the coverage-based
	# edge localisation in _crossing() work.
	grad_y, grad_x = np.gradient(padded)
	magnitude = np.hypot(grad_x, grad_y)
	safe = np.where(magnitude > 1e-9, magnitude, 1.0)
	normal_x = np.where(magnitude > 1e-9, grad_x / safe, 0.0)
	normal_y = np.where(magnitude > 1e-9, grad_y / safe, 0.0)

	# Each grid edge carries at most one crossing, so edges make stable node ids.
	links: dict[tuple, tuple] = {}
	positions: dict[tuple, Point] = {}

	def edge_id(name: str, i: int, j: int) -> tuple:
		if name == "t":
			return ("H", i, j)
		if name == "b":
			return ("H", i + 1, j)
		if name == "l":
			return ("V", i, j)
		return ("V", i, j + 1)

	def normal_at(i: int, j: int) -> tuple[float, float]:
		return (float(normal_x[i, j]), float(normal_y[i, j]))

	def edge_point(name: str, i: int, j: int) -> Point:
		if name == "t":
			a, b = (i, j), (i, j + 1)
			direction = (1.0, 0.0)
		elif name == "b":
			a, b = (i + 1, j), (i + 1, j + 1)
			direction = (1.0, 0.0)
		elif name == "l":
			a, b = (i, j), (i + 1, j)
			direction = (0.0, 1.0)
		else:
			a, b = (i, j + 1), (i + 1, j + 1)
			direction = (0.0, 1.0)

		t = _crossing(
			float(padded[a]),
			float(padded[b]),
			normal_at(*a),
			normal_at(*b),
			direction,
			level,
		)
		x = a[1] + direction[0] * t
		y = a[0] + direction[1] * t
		# Undo the pad and move from sample space to pixel space.
		return (x - 1.5, y - 1.5)

	for i in range(rows - 1):
		for j in range(cols - 1):
			mask = 0
			if inside[i, j]:
				mask |= 1
			if inside[i, j + 1]:
				mask |= 2
			if inside[i + 1, j + 1]:
				mask |= 4
			if inside[i + 1, j]:
				mask |= 8
			if mask == 0 or mask == 15:
				continue

			if mask == 5 or mask == 10:
				centre = 0.25 * (padded[i, j] + padded[i, j + 1] + padded[i + 1, j + 1] + padded[i + 1, j])
				connected = centre > level
				if mask == 5:
					pairs = (("r", "t"), ("l", "b")) if connected else (("l", "t"), ("r", "b"))
				else:
					pairs = (("t", "l"), ("b", "r")) if connected else (("t", "r"), ("b", "l"))
			else:
				pairs = _SEGMENTS[mask]

			for start_name, end_name in pairs:
				start_id = edge_id(start_name, i, j)
				end_id = edge_id(end_name, i, j)
				positions.setdefault(start_id, edge_point(start_name, i, j))
				positions.setdefault(end_id, edge_point(end_name, i, j))
				links[start_id] = end_id

	contours: list[list[Point]] = []
	unvisited = set(links)
	while unvisited:
		# The start point becomes the first on-curve point in the emitted glyph.
		# Set iteration is hash-randomised between Python processes, so choosing
		# it directly made equivalent traces produce different glyf byte streams.
		start = min(unvisited)
		chain: list[Point] = []
		node = start
		while node in unvisited:
			unvisited.discard(node)
			chain.append(positions[node])
			node = links[node]
		if len(chain) >= 3:
			contours.append(chain)
	return contours


# ---------------------------------------------------------------------------
# polygon analysis
# ---------------------------------------------------------------------------


def _polygon_signed_area(points: list[Point]) -> float:
	total = 0.0
	count = len(points)
	for index in range(count):
		x0, y0 = points[index]
		x1, y1 = points[(index + 1) % count]
		total += x0 * y1 - x1 * y0
	return 0.5 * total


def _detect_corners(points: list[Point], options: TraceOptions) -> list[int]:
	"""Flag vertices where the outline genuinely turns rather than curves."""
	count = len(points)
	if count < 6:
		return list(range(count))

	array = np.asarray(points, dtype=np.float64)
	deltas = np.roll(array, -1, axis=0) - array
	lengths = np.hypot(deltas[:, 0], deltas[:, 1])
	cumulative = np.concatenate([[0.0], np.cumsum(lengths)])
	perimeter = float(cumulative[-1])
	if perimeter <= 0.0:
		return [0]

	window = min(options.corner_window, perimeter / 6.0)
	threshold = math.radians(options.corner_angle_deg)

	def walk(index: int, direction: int) -> Point:
		"""Point roughly ``window`` units away along the contour."""
		travelled = 0.0
		cursor = index
		for _ in range(count):
			step = cursor if direction > 0 else (cursor - 1) % count
			travelled += lengths[step]
			cursor = (cursor + direction) % count
			if travelled >= window:
				break
		return points[cursor]

	corners: list[int] = []
	angles = np.zeros(count)
	for index in range(count):
		before = walk(index, -1)
		after = walk(index, 1)
		here = points[index]
		v0 = (here[0] - before[0], here[1] - before[1])
		v1 = (after[0] - here[0], after[1] - here[1])
		n0 = math.hypot(*v0)
		n1 = math.hypot(*v1)
		if n0 < 1e-9 or n1 < 1e-9:
			continue
		cross = v0[0] * v1[1] - v0[1] * v1[0]
		dot = v0[0] * v1[0] + v0[1] * v1[1]
		angles[index] = abs(math.atan2(cross, dot))

	# Keep only local maxima so a single physical corner yields one vertex.
	for index in range(count):
		angle = angles[index]
		if angle < threshold:
			continue
		is_peak = True
		for offset in range(1, 4):
			if angles[(index - offset) % count] > angle or angles[(index + offset) % count] > angle + 1e-12:
				is_peak = False
				break
		if is_peak:
			corners.append(index)

	if not corners:
		corners = [0]
	return corners


def _fit_line(points: np.ndarray) -> tuple[float, float, float]:
	"""Total-least-squares line as ``(nx, ny, c)`` with ``nx*x + ny*y = c``."""
	centre = points.mean(axis=0)
	centred = points - centre
	_, _, vt = np.linalg.svd(centred, full_matrices=False)
	direction = vt[0]
	normal = np.array([-direction[1], direction[0]])
	return float(normal[0]), float(normal[1]), float(normal @ centre)


def _line_deviation(points: np.ndarray, line: tuple[float, float, float]) -> float:
	nx, ny, c = line
	return float(np.abs(points[:, 0] * nx + points[:, 1] * ny - c).max())


@dataclass
class _Run:
	kind: str  # "line" or "curve"
	points: np.ndarray
	line: tuple[float, float, float] | None = None


def _trim_ends(block: np.ndarray, trim: float) -> np.ndarray:
	"""Drop the corner-rounded ends of a run before fitting a line to it.

	Antialiasing rounds every corner, so the first and last points of a run sit
	on the fillet rather than on the straight edge.  Fitting through them tilts
	the line and inflates its deviation - enough that a perfectly straight stem
	fails the straightness test and gets treated as a curve.
	"""
	if len(block) < 7 or trim <= 0.0:
		return block
	deltas = np.diff(block, axis=0)
	lengths = np.hypot(deltas[:, 0], deltas[:, 1])
	cumulative = np.concatenate([[0.0], np.cumsum(lengths)])
	total = float(cumulative[-1])
	if total <= 2.5 * trim:
		return block
	first = int(np.searchsorted(cumulative, trim))
	last = int(np.searchsorted(cumulative, total - trim))
	first = max(0, min(first, len(block) - 4))
	last = max(first + 3, min(last, len(block) - 1))
	return block[first : last + 1]


def _split_runs(points: list[Point], corners: list[int], options: TraceOptions) -> list[_Run]:
	"""Break the contour at corners, then classify each stretch as a whole.

	Corners are where these letterforms genuinely change direction, so each
	stretch between two corners is either one straight edge or one simple arc.
	Classifying whole runs - rather than greedily peeling short straight bits
	off a curve - is what keeps diagonals and bowls free of kinks.
	"""
	count = len(points)
	array = np.asarray(points, dtype=np.float64)
	corners = sorted(set(corners))

	# A bowl with no sharp corner at all (O, o, C) arrives as a single closed
	# run whose start and end coincide, which no curve fitter can parameterise.
	# Cut it into arcs so each piece is a well-formed span.
	if len(corners) < 2:
		pieces = max(4, count // 12)
		base = corners[0] if corners else 0
		corners = sorted({(base + round(i * count / pieces)) % count for i in range(pieces)})

	runs: list[_Run] = []

	for position, corner in enumerate(corners):
		nxt = corners[(position + 1) % len(corners)]
		if nxt > corner:
			indices = list(range(corner, nxt + 1))
		else:
			indices = list(range(corner, count)) + list(range(0, nxt + 1))
		if len(indices) < 2:
			continue
		block = array[indices]

		if len(block) == 2:
			runs.append(_Run("line", block, _fit_line(block)))
			continue

		# Corners are already located, so the interior of a run can be filtered
		# without blunting them.  Coverage is quantised to 1/255 and a diagonal
		# edge does not ramp perfectly linearly, which leaves the raw contour
		# with roughly a quarter-pixel of noise; left in, the curve fitter
		# reproduces it faithfully and diagonals come out scalloped.
		smoothed = _smooth_run(block, options.smoothing_passes)

		core = _trim_ends(smoothed, options.corner_trim)
		line = _fit_line(core)
		# Straightness is judged as a ratio, not an absolute distance: contour
		# noise grows with run length, while an arc's bulge grows in proportion
		# to its chord.  A 30px stem edge drifts ~0.008 of its length, a 36
		# degree arc bulges ~0.08 of its chord, so the two never overlap.
		span = float(np.hypot(*(core[-1] - core[0])))
		allowance = max(options.line_tolerance, options.straightness_ratio * span)
		if _line_deviation(core, line) <= allowance:
			runs.append(_Run("line", smoothed, line))
			continue

		runs.append(_Run("curve", smoothed))
	return runs


def _smooth_run(block: np.ndarray, passes: int) -> np.ndarray:
	"""Binomial smoothing of a run's interior, with both endpoints pinned."""
	if passes <= 0 or len(block) < 5:
		return block
	result = block.astype(np.float64).copy()
	for _ in range(passes):
		interior = 0.25 * result[:-2] + 0.5 * result[1:-1] + 0.25 * result[2:]
		result[1:-1] = interior
	return result


def _snap_axes(runs: list[_Run], options: TraceOptions) -> None:
	"""Force near-axis lines onto the axis and share offsets between stems."""
	axis_threshold = math.sin(math.radians(options.axis_angle_deg))
	horizontals: list[_Run] = []
	verticals: list[_Run] = []

	for run in runs:
		if run.kind != "line" or run.line is None:
			continue
		nx, ny, _ = run.line
		if abs(nx) <= axis_threshold:  # normal points along y -> horizontal line
			offset = float(run.points[:, 1].mean())
			run.line = (0.0, 1.0, offset)
			horizontals.append(run)
		elif abs(ny) <= axis_threshold:
			offset = float(run.points[:, 0].mean())
			run.line = (1.0, 0.0, offset)
			verticals.append(run)

	for group in (horizontals, verticals):
		if not group:
			continue
		weights = [float(np.hypot(*(run.points[-1] - run.points[0]))) for run in group]
		offsets = [run.line[2] for run in group]
		order = sorted(range(len(group)), key=lambda i: offsets[i])

		cluster: list[int] = []
		clusters: list[list[int]] = []
		for index in order:
			if cluster and offsets[index] - offsets[cluster[-1]] > options.axis_cluster:
				clusters.append(cluster)
				cluster = []
			cluster.append(index)
		if cluster:
			clusters.append(cluster)

		for members in clusters:
			total_weight = sum(weights[i] for i in members)
			if total_weight <= 0.0:
				continue
			merged = sum(offsets[i] * weights[i] for i in members) / total_weight
			for i in members:
				nx, ny, _ = group[i].line
				group[i].line = (nx, ny, merged)


def _intersect(a: tuple[float, float, float], b: tuple[float, float, float]) -> Point | None:
	a0, a1, ac = a
	b0, b1, bc = b
	det = a0 * b1 - a1 * b0
	if abs(det) < 1e-9:
		return None
	x = (ac * b1 - a1 * bc) / det
	y = (a0 * bc - ac * b0) / det
	return (x, y)


def _project(line: tuple[float, float, float], point: Point) -> Point:
	nx, ny, c = line
	distance = nx * point[0] + ny * point[1] - c
	return (point[0] - nx * distance, point[1] - ny * distance)


def _fit_quadratics(block: np.ndarray, start: Point, end: Point, options: TraceOptions, depth: int = 0) -> list[tuple]:
	"""Least-squares quadratic through ``block``, subdividing until it fits."""
	if len(block) < 3:
		return [("L", (float(end[0]), float(end[1])))]

	deltas = np.diff(block, axis=0)
	lengths = np.hypot(deltas[:, 0], deltas[:, 1])
	cumulative = np.concatenate([[0.0], np.cumsum(lengths)])
	if cumulative[-1] <= 1e-9:
		return [("L", (float(end[0]), float(end[1])))]
	t = cumulative / cumulative[-1]

	p0 = np.asarray(start, dtype=np.float64)
	p1 = np.asarray(end, dtype=np.float64)
	weight = 2.0 * (1.0 - t) * t
	residual = block - ((1.0 - t) ** 2)[:, None] * p0 - (t**2)[:, None] * p1
	denominator = float((weight**2).sum())
	if denominator <= 1e-12:
		return [("L", (float(end[0]), float(end[1])))]
	control = (weight[:, None] * residual).sum(axis=0) / denominator

	predicted = ((1.0 - t) ** 2)[:, None] * p0 + weight[:, None] * control + (t**2)[:, None] * p1
	error = float(np.hypot(*(predicted - block).T).max())
	fitted = [("Q", (float(control[0]), float(control[1])), (float(end[0]), float(end[1])))]
	if error <= options.curve_tolerance or depth >= options.max_curve_depth:
		return fitted

	middle = len(block) // 2
	if middle < 1 or middle > len(block) - 2:
		return fitted
	joint = (float(block[middle][0]), float(block[middle][1]))
	left = _fit_quadratics(block[: middle + 1], start, joint, options, depth + 1)
	right = _fit_quadratics(block[middle:], joint, end, options, depth + 1)
	return left + right


def _runs_to_contour(runs: list[_Run], options: TraceOptions) -> Contour | None:
	if not runs:
		return None

	count = len(runs)
	starts: list[Point] = [None] * count  # type: ignore[list-item]
	ends: list[Point] = [None] * count  # type: ignore[list-item]

	for index, run in enumerate(runs):
		starts[index] = (float(run.points[0][0]), float(run.points[0][1]))
		ends[index] = (float(run.points[-1][0]), float(run.points[-1][1]))

	# Where two straight runs meet, replace the antialiasing-rounded junction
	# with the exact intersection of the two lines.
	for index, run in enumerate(runs):
		nxt = runs[(index + 1) % count]
		if run.kind == "line" and nxt.kind == "line" and run.line and nxt.line:
			# Shallow crossings shoot off to infinity and would spike the
			# outline, so only rebuild a corner when the two edges really meet.
			cosine = abs(run.line[0] * nxt.line[0] + run.line[1] * nxt.line[1])
			crossing = _intersect(run.line, nxt.line) if cosine < 0.985 else None
			if crossing is not None:
				drift = math.hypot(crossing[0] - ends[index][0], crossing[1] - ends[index][1])
				if drift <= options.max_corner_extension:
					ends[index] = crossing
					starts[(index + 1) % count] = crossing
					continue
		if run.kind == "line" and run.line:
			ends[index] = _project(run.line, ends[index])
			starts[(index + 1) % count] = ends[index]
		elif nxt.kind == "line" and nxt.line:
			starts[(index + 1) % count] = _project(nxt.line, starts[(index + 1) % count])
			ends[index] = starts[(index + 1) % count]
		else:
			midpoint = (
				0.5 * (ends[index][0] + starts[(index + 1) % count][0]),
				0.5 * (ends[index][1] + starts[(index + 1) % count][1]),
			)
			ends[index] = midpoint
			starts[(index + 1) % count] = midpoint

	for index, run in enumerate(runs):
		if run.kind == "line" and run.line:
			starts[index] = _project(run.line, starts[index])

	contour = Contour(starts[0])
	for index, run in enumerate(runs):
		if run.kind == "line":
			contour.line_to(ends[index])
			continue
		for segment in _fit_quadratics(run.points, starts[index], ends[index], options):
			if segment[0] == "L":
				contour.line_to(segment[1])
			else:
				contour.quad_to(segment[1], segment[2])

	# The walk above already returns to the start; drop a redundant closing line.
	if contour.segments and contour.segments[-1][0] == "L":
		last = contour.segments[-1][1]
		if math.hypot(last[0] - contour.start[0], last[1] - contour.start[1]) < 1e-6:
			contour.segments.pop()
	return contour if contour.segments else None


def trace_coverage(field: np.ndarray, options: TraceOptions | None = None) -> Path2D:
	"""Convert one glyph coverage mask into an outline in pixel space."""
	options = options or TraceOptions()
	path = Path2D()
	polygons = marching_squares(field, options.level)

	for polygon in polygons:
		if len(polygon) < 4:
			continue
		corners = _detect_corners(polygon, options)
		runs = _split_runs(polygon, corners, options)
		if not runs:
			continue
		_snap_axes(runs, options)
		contour = _runs_to_contour(runs, options)
		if contour is None:
			continue
		if abs(contour.signed_area()) < 0.05:
			continue
		path.contours.append(contour)

	_orient(path)
	return path


def _orient(path: Path2D) -> None:
	"""Set winding by nesting depth: outer contours one way, holes the other.

	Coordinates here are y-down; the TTF writer flips y, which flips the sense,
	so outer contours are made counter-clockwise in this space to land on
	TrueType's clockwise-outer convention.
	"""
	polygons = [contour.flatten(0.05) for contour in path.contours]
	for index, contour in enumerate(path.contours):
		probe = polygons[index][0]
		depth = 0
		for other, polygon in enumerate(polygons):
			if other == index:
				continue
			if _point_in_polygon(probe, polygon):
				depth += 1
		area = _polygon_signed_area(polygons[index])
		want_positive = depth % 2 == 0
		if (area > 0) != want_positive:
			path.contours[index] = contour.reverse()


def _point_in_polygon(point: Point, polygon: list[Point]) -> bool:
	x, y = point
	inside = False
	count = len(polygon)
	for index in range(count):
		x0, y0 = polygon[index]
		x1, y1 = polygon[(index + 1) % count]
		if (y0 > y) != (y1 > y):
			t = (y - y0) / (y1 - y0) if y1 != y0 else 0.0
			if x < x0 + t * (x1 - x0):
				inside = not inside
	return inside
