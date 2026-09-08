"""
tcland - the lay of the land around a track.

A track is not laid out on a plane any more. Every track first gets a
landscape: a handful of analytic primitives (hills, ridges, peaks, basins)
plus fractal detail, all summed into one height field.

Everything that needs to know how high the ground is asks this one function:

  * the road's elevation profile (sampled at the control points, so the
    road climbs and drops with the landscape it is carved into),
  * the terrain mesh (which blends from road level out to the field),
  * prop placement (trees stand on the ground, not in the air).

Because the road is anchored to the same field, the terrain beside it rises
into a cut on the uphill side and falls away on the downhill one all by
itself - which is what makes a track read as three-dimensional.

No Blender dependency, so the field can be exercised outside Blender.
"""
import math
import random


# ------------------------------------------------------------------ noise

class Noise:
    """Simple value noise with fractal octaves - no dependencies."""

    def __init__(self, seed=1):
        self.p = list(range(256))
        rnd = random.Random(seed)
        rnd.shuffle(self.p)
        self.p = self.p * 2

    def _grad(self, ix, iy):
        h = self.p[(self.p[ix & 255] + iy) & 255]
        return (h / 255.0) * 2.0 - 1.0

    def value(self, x, y):
        ix, iy = math.floor(x), math.floor(y)
        fx, fy = x - ix, y - iy
        sx = fx * fx * (3 - 2 * fx)
        sy = fy * fy * (3 - 2 * fy)
        v00 = self._grad(ix, iy)
        v10 = self._grad(ix + 1, iy)
        v01 = self._grad(ix, iy + 1)
        v11 = self._grad(ix + 1, iy + 1)
        a = v00 + (v10 - v00) * sx
        b = v01 + (v11 - v01) * sx
        return a + (b - a) * sy

    def fbm(self, x, y, octaves=4, lac=2.0, gain=0.5):
        amp, freq, tot, norm = 1.0, 1.0, 0.0, 0.0
        for _ in range(octaves):
            tot += self.value(x * freq, y * freq) * amp
            norm += amp
            amp *= gain
            freq *= lac
        return tot / norm


# ------------------------------------------------------------------ kernels

def _bump(u):
    """Dome: 1 in the middle, 0 with a flat tangent at the rim."""
    if u >= 1.0:
        return 0.0
    v = 1.0 - u * u
    return v * v


def _crest(u):
    """Summit: a real peak in the middle, not a dome."""
    if u >= 1.0:
        return 0.0
    return (1.0 - u) ** 1.45


def _seg_dist(x, y, x0, y0, x1, y1):
    dx, dy = x1 - x0, y1 - y0
    ln = dx * dx + dy * dy
    if ln < 1e-9:
        return math.hypot(x - x0, y - y0)
    t = ((x - x0) * dx + (y - y0) * dy) / ln
    t = max(0.0, min(1.0, t))
    return math.hypot(x - (x0 + dx * t), y - (y0 + dy * t))


# ------------------------------------------------------------------ field

class LandField:
    """
    The height field of one track's surroundings.

    hills   [(x, y, radius, height)]            - domes; a negative height
                                                  digs a basin or a valley
    ridges  [(x0, y0, x1, y1, radius, height)]  - a dome swept along a line
    peaks   [(x, y, radius, height)]            - mountains, with a summit

    detail / detail_scale  fractal roughness added everywhere
    rough                  how much that roughness grows with altitude, so
                           high ground turns craggy and the lowlands stay calm
    """

    def __init__(self, seed=1, base=0.0, hills=(), ridges=(), peaks=(),
                 detail=2.4, detail_scale=0.011, rough=0.0, noise=None):
        self.noise = noise or Noise(seed)
        self.base = base
        self.hills = [tuple(h) for h in hills]
        self.ridges = [tuple(r) for r in ridges]
        self.peaks = [tuple(p) for p in peaks]
        self.detail = detail
        self.ds = detail_scale
        self.rough = rough

    # -------------------------------------------------------------- height

    def landforms(self, x, y):
        """The big shape only - no fractal detail. This is what the road follows."""
        h = self.base
        for hx, hy, r, ht in self.hills:
            d = math.hypot(x - hx, y - hy)
            if d < r:
                h += ht * _bump(d / r)
        for x0, y0, x1, y1, r, ht in self.ridges:
            d = _seg_dist(x, y, x0, y0, x1, y1)
            if d < r:
                h += ht * _bump(d / r)
        for px, py, r, ht in self.peaks:
            d = math.hypot(x - px, y - py)
            if d < r:
                h += ht * _crest(d / r)
        return h

    def height(self, x, y):
        """Ground level, detail included - this is what the terrain mesh uses."""
        h = self.landforms(x, y)
        n = self.noise
        d = n.fbm(x * self.ds, y * self.ds, 4) * self.detail
        d += n.fbm(x * self.ds * 3.6, y * self.ds * 3.6, 2) * self.detail * 0.34
        if self.rough:
            d *= 1.0 + self.rough * max(0.0, h - self.base) / 20.0
        return h + d

    # -------------------------------------------------------------- extent

    def reach(self, x0, y0, x1, y1):
        """
        How far beyond a box the landforms still have something to say. The
        terrain mesh uses it so a mountain is never sliced off at the edge.
        """
        out = 0.0
        for items in (self.hills, self.peaks):
            for it in items:
                cx, cy, r = it[0], it[1], it[2]
                out = max(out, _box_reach(cx, cy, r, x0, y0, x1, y1))
        for x0r, y0r, x1r, y1r, r, _h in self.ridges:
            out = max(out, _box_reach(x0r, y0r, r, x0, y0, x1, y1))
            out = max(out, _box_reach(x1r, y1r, r, x0, y0, x1, y1))
        return out


def _box_reach(cx, cy, r, x0, y0, x1, y1):
    """How far outside the box [x0,y0,x1,y1] a circle (cx,cy,r) sticks out."""
    dx = max(x0 - (cx - r), (cx + r) - x1, 0.0)
    dy = max(y0 - (cy - r), (cy + r) - y1, 0.0)
    return max(dx, dy)
