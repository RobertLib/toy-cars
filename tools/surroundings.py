"""The world beyond each island, out to the horizon.

Driving still ends at the island's outline: crossing it recovers the car. The
world no longer does. A ring mesh starts on the island mesh's own outline
vertices, so the two share their seam without a slit, continues the island's
terrain outward and rises into each environment's backdrop: alpine peaks,
mesas, wooded ridges, farmland, a city across the harbour, islets in the sea.
Its rings widen with distance; only stream valleys and islets are refined.

Everything here is scenery for its own runtime file, `<track>-land.tcm`, and
none of it joins the driving surface. A fence or a dry-stone wall marks the
outline on dry land. A stream that leaves a dry island runs on in a carved
valley, and the sea reaches the horizon; both return as extra water triangles.
"""
import bisect
import math
import random
from mathutils import Vector
from mathutils.bvhtree import BVHTree

REACH = 780.            # Land and sea reach at least this far from the centre
SEA_LEVEL = -6.85       # hydrology.Watershed.SEA_LEVEL
SEABED = -8.2           # A shallow shelf round the shore; the sea deepens offshore
SHORE = 24.             # The rim slopes down to the seabed over this distance
SMOOTH = 45.            # Smooth-shaded beside the island, faceted farther out
GLOW = (.98, .80, .40)  # Lit windows and molten craters glow at night (biomes.GLOW)
TONES = (.97, .985, 1., 1.015, 1.03)


def smoothstep(a, b, x):
    t = max(0., min(1., (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def rise(d, start, scale):
    """How much backdrop relief shows: none beside the island, all of it far away."""
    return 1 - math.exp(-max(0., d - start) / scale)


def lattice(i, j, seed):
    h = (i * 374761393 + j * 668265263 + seed * 982451653) & 0xffffffff
    h = ((h ^ (h >> 13)) * 1274126177) & 0xffffffff
    return ((h ^ (h >> 16)) & 0xffffff) / 16777216.


def noise(x, z, seed):
    i, j = math.floor(x), math.floor(z)
    u, v = x - i, z - j
    u, v = u * u * (3 - 2 * u), v * v * (3 - 2 * v)
    a, b = lattice(i, j, seed), lattice(i + 1, j, seed)
    c, d = lattice(i, j + 1, seed), lattice(i + 1, j + 1, seed)
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v


def fbm(x, z, seed, octaves=4):
    total = norm = 0.
    amplitude = 1.
    for k in range(octaves):
        total += noise(x, z, seed + k) * amplitude
        norm += amplitude
        x, z, amplitude = x * 2.03 + 17.1, z * 2.03 - 9.4, amplitude * .5
    return total / norm


def ridges(x, z, seed, octaves=4):
    """Sharp crests where the noise crosses its middle, like an eroded range."""
    total = norm = 0.
    amplitude = 1.
    for k in range(octaves):
        n = 1 - abs(2 * noise(x, z, seed + k) - 1)
        total += n * n * amplitude
        norm += amplitude
        x, z, amplitude = x * 2.07 + 5.3, z * 2.07 + 11.9, amplitude * .5
    return total / norm


def polar(T, a, d):
    """The point d metres beyond the outline in the direction a."""
    r = T.edge_radius(a) + d
    return math.cos(a) * r, math.sin(a) * r


# --- Geometry -----------------------------------------------------------------------------------
_T = (1 + 5 ** .5) / 2
_ICO = [Vector(v).normalized() for v in
        ((-1, _T, 0), (1, _T, 0), (-1, -_T, 0), (1, -_T, 0), (0, -1, _T), (0, 1, _T),
         (0, -1, -_T), (0, 1, -_T), (_T, 0, -1), (_T, 0, 1), (-_T, 0, -1), (-_T, 0, 1))]
_ICO_FACES = ((0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4),
              (11, 10, 2), (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8),
              (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1))


class Batch:
    """Low-poly dressing, merged into one mesh per colour: flat-shaded walls and
    trunks, smooth-shaded crowns and cones so distant woods read as soft masses."""

    def __init__(self, api, name, land=True):
        self.api, self.name, self.land = api, name, land
        self.parts = {}

    def faces(self, color, vs, fs, paint=0, smooth=False):
        key = (tuple(round(c, 4) for c in color), paint, smooth)
        part_vs, part_fs = self.parts.setdefault(key, ([], []))
        base = len(part_vs)
        part_vs.extend(tuple(v) for v in vs)
        part_fs.extend(tuple(base + i for i in f) for f in fs)

    def prism(self, a, b, r, color, sides=5, r2=None, cap=True):
        """A cylinder, or a cone when r2 is 0, from a to b."""
        a, b = Vector(a), Vector(b)
        axis = (b - a).normalized()
        u = axis.cross(Vector((1, 0, 0)) if abs(axis.y) > .9 else Vector((0, 1, 0))).normalized()
        v = axis.cross(u)
        r2 = r if r2 is None else r2
        vs = [a + (u * math.cos(k * 2 * math.pi / sides) + v * math.sin(k * 2 * math.pi / sides)) * r
              for k in range(sides)]
        if r2 > 0:
            vs += [b + p - a for p in ((q - a) * (r2 / r) + a for q in vs)]
            fs = [(k, (k + 1) % sides, sides + (k + 1) % sides, sides + k) for k in range(sides)]
            if cap:
                fs.append(tuple(range(2 * sides - 1, sides - 1, -1)))
        else:
            vs.append(b)
            fs = [(k, (k + 1) % sides, sides) for k in range(sides)]
            # A cone has no caps, so it shades smoothly round its sides.
            self.faces(color, vs, fs, smooth=True)
            return
        self.faces(color, vs, fs)

    def box(self, center, size, color, yaw=0.):
        x, y, z = center
        c, s = math.cos(yaw), math.sin(yaw)
        vs = []
        for dy in (-.5, .5):
            for dx, dz in ((-.5, -.5), (.5, -.5), (.5, .5), (-.5, .5)):
                a, b = dx * size[0], dz * size[2]
                vs.append((x + c * a + s * b, y + dy * size[1], z - s * a + c * b))
        self.faces(color, vs, [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)])

    def ico(self, center, scale, color, yaw=0.):
        c, s = math.cos(yaw), math.sin(yaw)
        # Each vertex bulges or sinks a little, so no crown is a regular solid.
        lump = [1 + .08 * math.sin(yaw * 3.1 + k * 2.39) for k in range(len(_ICO))]
        vs = [(center[0] + (c * p.x + s * p.z) * scale[0] * l, center[1] + p.y * scale[1] * l,
               center[2] + (c * p.z - s * p.x) * scale[2] * l) for p, l in zip(_ICO, lump)]
        self.faces(color, vs, _ICO_FACES, smooth=True)

    def quad(self, corners, color):
        self.faces(color, corners, [(0, 1, 2, 3)])

    def gable(self, x, y, z, w, d, h, rise, wall, roof, yaw=0., overhang=.4):
        """Box walls under a pitched roof, its ridge along the local x axis."""
        self.box((x, y + h / 2 - .3, z), (w, h + .6, d), wall, yaw)
        c, s = math.cos(yaw), math.sin(yaw)

        def P(a, hh, b):
            return (x + c * a + s * b, y + hh, z - s * a + c * b)
        hw, hd = w / 2 + overhang, d / 2 + overhang
        vs = [P(-hw, h, -hd), P(hw, h, -hd), P(hw, h + rise, 0), P(-hw, h + rise, 0), P(-hw, h, hd), P(hw, h, hd)]
        self.faces(roof, vs, [(0, 1, 2, 3), (3, 2, 5, 4), (0, 3, 4), (1, 5, 2)])

    def wall(self, a, b, width, height, color):
        """A wall segment from a to b, following the ground at both ends."""
        dx, dz = b[0] - a[0], b[2] - a[2]
        length = math.hypot(dx, dz) or 1.
        nx, nz = -dz / length * width / 2, dx / length * width / 2
        vs = []
        for top in (False, True):
            for p, side in ((a, -1), (b, -1), (b, 1), (a, 1)):
                vs.append((p[0] + nx * side * (.8 if top else 1), p[1] + (height if top else -.3),
                           p[2] + nz * side * (.8 if top else 1)))
        self.faces(color, vs, [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)])

    def build(self):
        objs = []
        for (color, paint, smooth), (vs, fs) in self.parts.items():
            name = '%s %.3f %.3f %.3f' % (self.name, *color)
            obj = self.api['mesh'](self.name, vs, fs, self.api['mat'](name, color, paint))
            for poly in obj.data.polygons:
                poly.use_smooth = smooth
            if self.land:
                obj['land'] = True
            objs.append(obj)
        self.parts = {}
        return objs


def zip_rings(inner, ai, outer, ao, faces):
    """Triangulate between two closed rings by polar angle (ascending from -pi)."""
    ni, no = len(inner), len(outer)

    def A(k):
        return ai[k % ni] + 2 * math.pi * (k // ni)

    def B(k):
        return ao[k % no] + 2 * math.pi * (k // no)
    i = j = 0
    while i < ni or j < no:
        if j < no and (i >= ni or B(j + 1) <= A(i + 1)):
            faces.append((inner[i % ni], outer[(j + 1) % no], outer[j % no]))
            j += 1
        else:
            faces.append((inner[i % ni], inner[(i + 1) % ni], outer[j % no]))
            i += 1


def refine(vs, fs, wanted, locked, passes=8):
    """Split edges longer than wanted(x, z) where it applies. Edges between two
    of the first `locked` vertices (the island's outline) stay whole."""
    for _ in range(passes):
        split = {}
        for face in fs:
            for a, b in zip(face, face[1:] + face[:1]):
                if a < locked and b < locked:
                    continue
                key = (a, b) if a < b else (b, a)
                if key in split:
                    continue
                p, q = vs[a], vs[b]
                length = math.hypot(p[0] - q[0], p[2] - q[2])
                limits = [w for w in (wanted(p[0], p[2]), wanted(q[0], q[2]),
                                      wanted((p[0] + q[0]) / 2, (p[2] + q[2]) / 2)) if w]
                if limits and length > min(limits):
                    split[key] = None
        if not split:
            break
        for key in sorted(split):
            a, b = key
            split[key] = len(vs)
            vs.append(tuple((u + v) / 2 for u, v in zip(vs[a], vs[b])))
        refined = []
        for face in fs:
            mids = [split.get((a, b) if a < b else (b, a)) for a, b in zip(face, face[1:] + face[:1])]
            count = sum(m is not None for m in mids)
            if not count:
                refined.append(face)
                continue
            # Rotate so that the first edge is split and, with two splits, the
            # whole edge is the last one.
            k = next(k for k in range(3) if mids[k] is not None and (count != 2 or mids[(k + 2) % 3] is None))
            v0, v1, v2 = face[k], face[(k + 1) % 3], face[(k + 2) % 3]
            m01, m12, m20 = mids[k], mids[(k + 1) % 3], mids[(k + 2) % 3]
            if count == 3:
                refined += [(v0, m01, m20), (m01, v1, m12), (m20, m12, v2), (m01, m12, m20)]
            elif count == 2:
                refined += [(m01, v1, m12), (v0, m01, m12), (v0, m12, v2)]
            else:
                refined += [(v0, m01, v2), (m01, v1, v2)]
        fs = refined
    return vs, fs


# --- The land model -----------------------------------------------------------------------------
class Outlet:
    """A stream leaving a dry island runs on in its own valley, falling gently."""
    STEP = 4.
    LENGTH = 480.

    def __init__(self, land, watershed):
        route = watershed.route
        x, level, z = route[-1]
        px, _, pz = route[-6]
        heading = math.atan2(z - pz, x - px)
        self.speed = land.T.water['speed']
        width0 = watershed.segments[-1][5]
        self.points = []
        s = 0.
        while s <= self.LENGTH:
            self.points.append((x, level - .03 * s, z, width0 * (1 + min(2., s / 160)), s))
            # Meander gently while bearing away from the island.
            radial = math.atan2(z, x)
            heading += ((radial - heading + math.pi) % (2 * math.pi) - math.pi) * .08
            heading += .045 * math.sin(s * .019 + land.seed)
            x, z, s = x + math.cos(heading) * self.STEP, z + math.sin(heading) * self.STEP, s + self.STEP
        # The valley's walls open out to 125 m from the water; detail stays near it.
        self.cells = self.index(125.)
        self.near_cells = self.index(24.)

    def index(self, pad):
        cells = {}
        for i, (a, b) in enumerate(zip(self.points, self.points[1:])):
            for cx in range(math.floor((min(a[0], b[0]) - pad) / 16), math.floor((max(a[0], b[0]) + pad) / 16) + 1):
                for cz in range(math.floor((min(a[2], b[2]) - pad) / 16), math.floor((max(a[2], b[2]) + pad) / 16) + 1):
                    cells.setdefault((cx, cz), []).append(i)
        return cells

    def nearest(self, x, z, cells=None):
        """Distance from the channel's centre, water level, width and distance along it."""
        best = None
        for i in (cells or self.cells).get((math.floor(x / 16), math.floor(z / 16)), ()):
            a, b = self.points[i], self.points[i + 1]
            dx, dz = b[0] - a[0], b[2] - a[2]
            f = max(0., min(1., ((x - a[0]) * dx + (z - a[2]) * dz) / (dx * dx + dz * dz)))
            q = math.hypot(x - a[0] - dx * f, z - a[2] - dz * f)
            if best is None or q < best[0]:
                best = (q, a[1] + (b[1] - a[1]) * f, a[3] + (b[3] - a[3]) * f, a[4] + (b[4] - a[4]) * f)
        return best

    def lateral(self, x, z):
        """How far a point lies beyond the water's edge."""
        near = self.nearest(x, z, self.near_cells)
        return 1e9 if near is None else near[0] - near[2]

    def carve(self, x, z, y):
        near = self.nearest(x, z)
        if near is None:
            return y
        q, level, width, _ = near
        if q < width:
            return min(y, level - (.35 + .12 * width) * (1 - (q / width) ** 2))
        # The banks rise from the waterline; the valley opens out farther away.
        return min(y, level + (q - width) * .35 + .025 * max(0., q - width - 12) ** 2)

    def wanted(self, x, z):
        near = self.nearest(x, z, self.near_cells)
        if near is None:
            return None
        q, _, width, s = near
        fine = max(1., .55 * width) * (1 + s / 200)
        if q < width + 3:
            return fine
        return fine * 2 + (q - width) * .35 if q < width + 22 else None

    def water(self):
        """The channel's surface; the water ends where the banks rise above it."""
        rows = []
        n = len(self.points)
        for i, (x, level, z, width, _) in enumerate(self.points):
            a, b = self.points[max(0, i - 1)], self.points[min(n - 1, i + 1)]
            dx, dz = b[0] - a[0], b[2] - a[2]
            length = math.hypot(dx, dz)
            dx, dz = dx / length, dz / length
            rows.append([(x - dz * o, level, z + dx * o, width - abs(o), dx * self.speed, dz * self.speed)
                         for o in (-width - 1, 0., width + 1)])
        tris = []
        for r0, r1 in zip(rows, rows[1:]):
            for k in range(2):
                tris += [[r0[k], r1[k], r1[k + 1]], [r0[k], r1[k + 1], r0[k + 1]]]
        return tris


def far_sea(T):
    """Open water from the outer edge of build_water's ocean ring to the horizon."""
    segments = 192 if T.legacy else 288
    fractions = (0., .05, .12, .22, .36, .55, .78, 1.)
    tris = []
    for i in range(segments):
        a, b = i * 2 * math.pi / segments, (i + 1) * 2 * math.pi / segments

        def point(t, f):
            inner = 145. if T.legacy else T.edge_radius(t) + 26
            r = inner + (REACH - inner) * f
            return (math.cos(t) * r, SEA_LEVEL, math.sin(t) * r, 3., .18, .08)
        for f0, f1 in zip(fractions, fractions[1:]):
            q = [point(a, f0), point(a, f1), point(b, f1), point(b, f0)]
            tris += [[q[0], q[2], q[1]], [q[0], q[3], q[2]]]
    return tris


class Land:
    """Height and colour of the ground beyond the island outline."""

    def __init__(self, T, outline, watershed):
        self.T = T
        self.seed = 7919 * (T.index + 1)
        # Outline vertices by polar angle, as the island mesh has them.
        self.outline = sorted((math.atan2(z, x), x, y, z) for x, y, z in outline)
        self.angles = [p[0] for p in self.outline]
        self.top = max(p[2] for p in self.outline)
        self.outlet = Outlet(self, watershed) if not T.sea and not T.water.get('lake') else None
        self.style = STYLES[T.land['style']](self)

    def rim(self, angle):
        """The island's graded outline height in a direction."""
        n = len(self.angles)
        k = bisect.bisect_right(self.angles, angle) % n
        j = (k - 1) % n
        span = (self.angles[k] - self.angles[j]) % (2 * math.pi) or 2 * math.pi
        f = ((angle - self.angles[j]) % (2 * math.pi)) / span
        return self.outline[j][2] + (self.outline[k][2] - self.outline[j][2]) * f

    def beyond(self, x, z):
        return max(0., math.hypot(x, z) - self.T.edge_radius(math.atan2(z, x)))

    def height(self, x, z):
        T = self.T
        angle = math.atan2(z, x)
        edge = T.edge_radius(angle)
        d = max(0., math.hypot(x, z) - edge)
        if T.sea:
            # The island's rim runs on down to the seabed, which deepens offshore.
            bed = SEABED - 15 * smoothstep(60, 360, d)
            y = bed + (self.rim(angle) - bed) * (1 - min(1., d / SHORE)) ** 2
        else:
            # The island's rim falloff, mirrored: its outline lies in a shallow
            # valley, and the exact graded rim fades into the open terrain.
            w = .13 * edge
            ex, ez = math.cos(angle) * edge, math.sin(angle) * edge
            base = self.style.base
            y = base(x, z) - .14 - 5 * max(0., 1 - d / (1.6 * w)) ** 2
            y += (self.rim(angle) - (base(ex, ez) - 5.14)) * (1 - smoothstep(0, w, d))
        y = self.style.relief(x, z, d, y)
        return self.outlet.carve(x, z, y) if self.outlet else y

    def wanted(self, x, z):
        limits = [w for w in (self.style.wanted(x, z), self.outlet and self.outlet.wanted(x, z)) if w]
        return min(limits) if limits else None

    def grid(self):
        """Rings from the outline outward, wider apart and coarser with distance."""
        T = self.T
        vs = [(x, y, z) for _, x, y, z in self.outline]
        rings = [(list(range(len(vs))), self.angles)]
        mean = sum(T.radius) / 2
        d, step = 0., 2.5
        while True:
            d += step
            n = max(64, int(round(2 * math.pi * (mean + d) / min(30., max(4.6, step)))))
            angles = [-math.pi + j * 2 * math.pi / n for j in range(n)]
            ring = []
            for a in angles:
                ring.append(len(vs))
                x, z = polar(T, a, d)
                vs.append((x, 0., z))
            rings.append((ring, angles))
            if min(T.radius) + d >= REACH:
                break
            step *= 1.2
        fs = []
        for (inner, ai), (outer, ao) in zip(rings, rings[1:]):
            zip_rings(inner, ai, outer, ao, fs)
        return vs, fs

    def sites(self, kit, count, near, far, clear=4., dry=True, flat=None, below=None, sector=None):
        """Random dressing sites, denser near the island: (x, ground y, z, d)."""
        rng = kit.rng
        for _ in range(count):
            for _ in range(8):
                a = rng.uniform(*(sector or (-math.pi, math.pi)))
                d = near + (far - near) * rng.random() ** 1.5
                x, z = polar(self.T, a, d)
                hit = kit.ground(x, z)
                if hit is None or (dry and hit[0] < SEA_LEVEL + .5) or (flat and hit[1] < flat):
                    continue
                if (below is not None and hit[0] > below) or (self.outlet and self.outlet.lateral(x, z) < clear):
                    continue
                yield x, hit[0], z, d
                break

    def water(self):
        return (self.outlet.water() if self.outlet else []) + (far_sea(self.T) if self.T.sea else [])


# --- Dressing helpers ---------------------------------------------------------------------------
def broadleaf(b, x, y, z, s, crown, bark=(.36, .25, .16)):
    b.prism((x, y - .2, z), (x, y + 1.8 * s, z), .2 * s, bark, 4, cap=False)
    b.ico((x, y + 2.9 * s, z), (1.6 * s, 1.35 * s, 1.6 * s), crown, s * 7)


def conifer(b, x, y, z, s, green, snow=None, bark=(.33, .24, .17)):
    b.prism((x, y - .2, z), (x, y + .9 * s, z), .18 * s, bark, 4, cap=False)
    b.prism((x, y + .6 * s, z), (x, y + 4.4 * s, z), 1.55 * s, green, 6, 0)
    b.prism((x, y + 2.6 * s, z), (x, y + 6.2 * s, z), 1.1 * s, green, 6, 0)
    if snow:
        b.prism((x, y + 4.9 * s, z), (x, y + 6.5 * s, z), .55 * s, snow, 5, 0)


def clump(b, x, y, z, s, color, rng):
    """A crowd of crowns seen from afar: one broad, half-sunk canopy."""
    b.ico((x, y + .3 * s, z), (s, .72 * s, s * rng.uniform(.75, 1.15)), color, rng.uniform(0, 6.3))


def palm(b, x, y, z, s, rng, crown=(.24, .48, .20)):
    lean = rng.uniform(0, 6.3)
    top = (x + math.cos(lean) * .9 * s, y + 5 * s, z + math.sin(lean) * .9 * s)
    b.prism((x, y - .2, z), top, .17 * s, (.46, .33, .21), 5, .12 * s, cap=False)
    b.ico(top, (2.5 * s, .45 * s, 2.5 * s), crown, lean)


def lighthouse(b, x, y, z):
    b.prism((x, y - .5, z), (x, y + 15, z), 2.6, (.93, .92, .88), 8, 1.9)
    b.prism((x, y + 6, z), (x, y + 9, z), 2.32, (.80, .14, .10), 8, 2.15)
    b.prism((x, y + 15, z), (x, y + 17.2, z), 1.5, (.15, .16, .18), 8)
    b.ico((x, y + 16.1, z), (1.15, 1.0, 1.15), GLOW)
    b.prism((x, y + 17.2, z), (x, y + 19, z), 1.8, (.80, .14, .10), 8, 0)


def facing(a):
    """Yaw that turns a building's local +z towards the island."""
    return math.atan2(-math.cos(a), -math.sin(a))


def house(b, rng, x, y, z, a, wall, roof, lit=1.):
    w, d = rng.uniform(6, 10), rng.uniform(5, 7)
    yaw = facing(a) + rng.uniform(-.5, .5)
    b.gable(x, y, z, w, d, 3.2, 2.2, wall, roof, yaw)
    c, s = math.cos(yaw), math.sin(yaw)
    for k in (-1, 1):
        if rng.random() < lit:
            a_, b_ = k * w / 4, d / 2 + .06
            b.quad([(x + c * (a_ + u) + s * b_, y + h, z - s * (a_ + u) + c * b_)
                    for u, h in ((-.5, 1.), (.5, 1.), (.5, 2.1), (-.5, 2.1))], GLOW)


class Kit:
    def __init__(self, api, land, ground):
        self.api, self.land, self.T, self.ground = api, land, land.T, ground
        self.batch = Batch(api, 'Distant scenery ' + land.T.key)
        self.rng = random.Random(land.seed + 2)


# --- Environments -------------------------------------------------------------------------------
class Style:
    """Dry land continuing the island's own terrain; subclasses add the backdrop."""
    seabed = (.62, .56, .40)

    def __init__(self, land):
        self.land, self.T, self.C = land, land.T, land.T.colors
        self.seed = land.seed

    def base(self, x, z):
        """Terrain without the island's rim falloff; the island's own by default."""
        return self.T.terrain(x, z)

    def relief(self, x, z, d, y):
        return y

    def wanted(self, x, z):
        return None

    def ground(self):
        return self.C['grass'], -self.T.ground

    def paint(self, x, y, z, ny, d):
        return self.ground()

    def dress(self, kit):
        pass


class Farmland(Style):
    """Harvest Hills: a patchwork of fields and hedgerows over rolling hills."""
    CROPS = ((.42, .58, .26), (.80, .66, .26), (.56, .43, .27), (.71, .63, .35), (.36, .52, .24), None)
    ANGLE = .42

    def relief(self, x, z, d, y):
        return (y + rise(d, 30, 320) * (10 + 50 * fbm(x / 240, z / 240, self.seed))
                + rise(d, 15, 60) * 4 * (fbm(x / 47, z / 47, self.seed + 7) - .5))

    def field(self, x, z):
        c, s = math.cos(self.ANGLE), math.sin(self.ANGLE)
        u, v = x * c + z * s, z * c - x * s
        i = math.floor(u / 58)
        return i, math.floor(v / 38 + .37 * i)

    def paint(self, x, y, z, ny, d):
        if d < 22:
            return self.ground()
        crop = self.CROPS[int(lattice(*self.field(x, z), self.seed) * len(self.CROPS))]
        return crop or self.C['grass'], -self.T.ground

    def dress(self, kit):
        b, rng, land = kit.batch, kit.rng, kit.land
        hedge = tuple(v * .8 for v in self.C['foliage'])
        c, s = math.cos(self.ANGLE), math.sin(self.ANGLE)
        lines = []
        for i in range(-16, 17):
            lines.append([(58 * i, v) for v in range(-820, 821, 9)])
            for j in range(-24, 25):
                v = 38 * (j - .37 * i)
                lines.append([(u, v) for u in range(58 * i, 58 * i + 58, 9)])
        for k, line in enumerate(lines):
            for n, (u, v) in enumerate(line):
                # Hedges grow along some field boundaries, with gaps for gates.
                if lattice(k, n // 6, self.seed + 3) > .35 or rng.random() < .25:
                    continue
                x, z = u * c - v * s, u * s + v * c
                d = land.beyond(x, z)
                if not 10 < d < 230 or (land.outlet and land.outlet.lateral(x, z) < 4):
                    continue
                hit = kit.ground(x, z)
                if hit is None:
                    continue
                if rng.random() < .12:
                    broadleaf(b, x, hit[0], z, rng.uniform(1.1, 1.6), self.C['foliage'])
                else:
                    b.ico((x, hit[0] + .7, z), (2.1, 1.5, 2.1), hedge, rng.uniform(0, 6.3))
        for x, y, z, d in land.sites(kit, 90, 8, 260):
            broadleaf(b, x, y, z, rng.uniform(1., 1.7), self.C['foliage'])
        for x, y, z, d in land.sites(kit, 7, 50, 330, flat=.93):
            yaw = facing(math.atan2(z, x)) + rng.uniform(-.6, .6)
            cs, sn = math.cos(yaw), math.sin(yaw)
            b.gable(x, y, z, 11, 8, 5.5, 3.6, (.62, .18, .12), (.28, .24, .22), yaw)
            hx, hz = x + cs * 12 + sn * 6, z - sn * 12 + cs * 6
            house(b, rng, hx, kit.ground(hx, hz)[0] if kit.ground(hx, hz) else y, hz, math.atan2(z, x),
                  (.90, .88, .82), (.36, .30, .28))
            sx, sz = x - cs * 8, z + sn * 8
            b.prism((sx, y - .5, sz), (sx, y + 10, sz), 2.1, (.70, .72, .70), 8)
            b.prism((sx, y + 10, sz), (sx, y + 12, sz), 2.2, (.48, .50, .50), 8, 0)


class Coast(Style):
    """Sunshine Coast: open sea with sandy islets, palms and a lighthouse."""
    seabed = (.66, .58, .40)

    def __init__(self, land):
        super().__init__(land)
        rng = random.Random(self.seed)
        self.islets = []
        for k in range(6):
            a = rng.uniform(-math.pi, math.pi)
            x, z = polar(self.T, a, rng.uniform(150, 380))
            self.islets.append((x, z, rng.uniform(28, 70), rng.uniform(4, 13)))

    def relief(self, x, z, d, y):
        for cx, cz, radius, height in self.islets:
            t = 1 - math.hypot(x - cx, z - cz) / radius
            if t > 0:
                y = max(y, SEA_LEVEL - 4 + (height + 4) * t ** .6 + 2 * (fbm(x / 20, z / 20, self.seed) - .5) * t)
        return y

    def wanted(self, x, z):
        return 8. if any(math.hypot(x - cx, z - cz) < r + 15 for cx, cz, r, _ in self.islets) else None

    def paint(self, x, y, z, ny, d):
        if y < SEA_LEVEL - .3:
            return self.seabed, 0
        if y < SEA_LEVEL + 2.2 or d < SHORE:
            return self.ground()
        return self.C['foliage'], 0

    def dress(self, kit):
        b, rng = kit.batch, kit.rng
        for k, (cx, cz, radius, height) in enumerate(self.islets):
            if k == 0:
                hit = kit.ground(cx, cz)
                lighthouse(b, cx, hit[0], cz)
            for _ in range(int(radius / 4)):
                a, r = rng.uniform(0, 6.3), radius * .65 * rng.random() ** .5
                x, z = cx + math.cos(a) * r, cz + math.sin(a) * r
                hit = kit.ground(x, z)
                if hit and hit[0] > SEA_LEVEL + 1.5 and math.hypot(x - cx, z - cz) > 6:
                    palm(b, x, hit[0], z, rng.uniform(.9, 1.4), rng)


class Alpine(Style):
    """Alpine Rush: the circuit lies in a valley under snowy peaks."""

    def relief(self, x, z, d, y):
        return (y + rise(d, 35, 240) * (80 + 240 * ridges(x / 260, z / 260, self.seed))
                + rise(d, 25, 80) * 6 * fbm(x / 40, z / 40, self.seed + 3))

    def paint(self, x, y, z, ny, d):
        if ny < .7 and d > 30:
            return ((.46, .52, .56) if ny < .55 else (.60, .67, .70)), 0
        return self.ground()

    def dress(self, kit):
        b, rng, land = kit.batch, kit.rng, kit.land
        green, snow = self.C['foliage'], (.90, .95, .96)
        for x, y, z, d in land.sites(kit, 560, 7, 260, flat=.74, below=land.top + 95):
            conifer(b, x, y, z, rng.uniform(.8, 1.5) * (1 + d / 400), green, snow)
        for x, y, z, d in land.sites(kit, 9, 30, 260, flat=.9):
            house(b, rng, x, y, z, math.atan2(z, x), (.50, .34, .20), snow)


class Desert(Style):
    """Canyon Dash: buttes on the canyon floor, then stepped sandstone walls."""
    STRATA = ((.66, .36, .22), (.74, .45, .28), (.58, .30, .18), (.78, .52, .33))

    def base(self, x, z):
        # The island's canyon floor runs on. Far from its roads the island's own
        # terrain is mesa everywhere; out here buttes stand apart instead.
        floor = 2.6 + 1.5 * math.sin(x * .029 + .4) * math.cos(z * .025) + .8 * math.sin(z * .052 + x * .017)
        # Broad, flat-topped buttes: a narrow band of the noise forms their cliffs.
        # They keep 25 m clear of the island, leaving the view across the floor open.
        cliff = (9 + 9 * fbm(x / 50, z / 50, self.seed + 12)) * smoothstep(.57, .60, fbm(x / 110, z / 110, self.seed + 11))
        cliff *= smoothstep(25, 70, self.land.beyond(x, z))
        terraced = math.floor(cliff / 2.6) * 2.6 + 2.6 * smoothstep(.55, 1, (cliff / 2.6) % 1)
        return floor + terraced * .85 + cliff * .15

    def relief(self, x, z, d, y):
        wall = rise(d, 70, 260) * (25 + 95 * ridges(x / 210, z / 210, self.seed)) / 7
        return y + (math.floor(wall) + smoothstep(.55, 1, wall % 1)) * 7

    def paint(self, x, y, z, ny, d):
        if ny > .86 or d < 12:
            return self.ground()
        return self.STRATA[int(math.floor(y / 6)) % len(self.STRATA)], 0

    def dress(self, kit):
        b, rng, land = kit.batch, kit.rng, kit.land
        cactus, shrub = (.30, .46, .26), (.45, .48, .27)
        for x, y, z, d in land.sites(kit, 140, 6, 220, flat=.8):
            s = rng.uniform(.8, 1.4)
            b.prism((x, y - .3, z), (x, y + 4.5 * s, z), .32 * s, cactus, 6)
            for side in (-1, 1):
                if rng.random() < .75:
                    yaw = rng.uniform(0, 6.3)
                    h = rng.uniform(1.6, 2.6) * s
                    ox, oz = math.cos(yaw) * side * 1.1 * s, math.sin(yaw) * side * 1.1 * s
                    b.prism((x, y + h, z), (x + ox, y + h, z + oz), .22 * s, cactus, 5)
                    b.prism((x + ox, y + h - .2 * s, z + oz), (x + ox, y + h + 1.6 * s, z + oz), .22 * s, cactus, 5)
        for x, y, z, d in land.sites(kit, 220, 5, 240):
            s = rng.uniform(.8, 1.6)
            b.ico((x, y + .35 * s, z), (s, .6 * s, s), shrub, rng.uniform(0, 6.3))


class Forest(Style):
    """Maple Woods: wooded ridges under an autumn canopy."""
    CANOPY = ((.80, .22, .07), (.92, .45, .09), (.95, .70, .16), (.68, .16, .10), (.55, .52, .17), (.24, .34, .16))

    def relief(self, x, z, d, y):
        return (y + rise(d, 30, 300) * (25 + 85 * fbm(x / 210, z / 210, self.seed))
                + rise(d, 20, 70) * 5 * (fbm(x / 50, z / 50, self.seed + 5) - .5))

    def paint(self, x, y, z, ny, d):
        if d < 60:
            return self.ground()
        return self.CANOPY[int(lattice(math.floor(x / 23), math.floor(z / 23), self.seed) * len(self.CANOPY))], 0

    def dress(self, kit):
        # Trees beside the island; farther out their crowns merge into canopy.
        b, rng, land = kit.batch, kit.rng, kit.land
        for x, y, z, d in land.sites(kit, 260, 5, 70):
            broadleaf(b, x, y, z, rng.uniform(1.2, 2.), rng.choice(self.CANOPY))
        for x, y, z, d in land.sites(kit, 700, 70, 240):
            clump(b, x, y, z, rng.uniform(4.5, 8.), rng.choice(self.CANOPY), rng)


class Rainforest(Style):
    """Jungle Ruins: tower hills under the rainforest canopy."""
    CANOPY = ((.16, .34, .13), (.22, .42, .16), (.12, .28, .11), (.28, .46, .18), (.20, .38, .20))

    def relief(self, x, z, d, y):
        return y + rise(d, 35, 200) * (30 + 150 * ridges(x / 150, z / 150, self.seed) ** 1.6)

    def paint(self, x, y, z, ny, d):
        if ny < .55 and d > 30:
            return (.36, .40, .32), 0
        if d < 60:
            return self.ground()
        return self.CANOPY[int(lattice(math.floor(x / 19), math.floor(z / 19), self.seed) * len(self.CANOPY))], 0

    def dress(self, kit):
        # Tall trees with umbrella crowns and palms beside the island, canopy beyond.
        b, rng, land = kit.batch, kit.rng, kit.land
        for x, y, z, d in land.sites(kit, 60, 5, 50):
            palm(b, x, y, z, rng.uniform(1., 1.5), rng, rng.choice(self.CANOPY))
        for x, y, z, d in land.sites(kit, 200, 5, 70):
            s = rng.uniform(1., 1.7)
            b.prism((x, y - .2, z), (x, y + 5.5 * s, z), .3 * s, (.36, .28, .20), 5, .2 * s, cap=False)
            b.ico((x, y + 6.2 * s, z), (3. * s, 1.3 * s, 3. * s), rng.choice(self.CANOPY), rng.uniform(0, 6.3))
        for x, y, z, d in land.sites(kit, 650, 70, 230):
            clump(b, x, y, z, rng.uniform(5., 9.), rng.choice(self.CANOPY), rng)


class Garden(Style):
    """Sakura Hills: tea fields, blossom and a snow-capped cone on the horizon."""
    FIELDS = ((.42, .60, .28), (.55, .68, .32), (.30, .46, .22), None)
    BLOSSOM = ((.95, .72, .80), (.97, .82, .87))
    GREENS = ((.30, .48, .22), (.36, .54, .24), (.24, .40, .20))

    def __init__(self, land):
        super().__init__(land)
        self.peak = polar(self.T, -2.2, 300)

    def cone(self, x, z):
        t = 1 - math.hypot(x - self.peak[0], z - self.peak[1]) / 260
        return min(230 * t ** 1.5, 214 + 16 * t) if t > 0 else -1e9

    def relief(self, x, z, d, y):
        y += rise(d, 30, 280) * (8 + 40 * fbm(x / 200, z / 200, self.seed))
        return max(y, self.cone(x, z))

    def paint(self, x, y, z, ny, d):
        if self.cone(x, z) >= y - .01:
            if y > 150 + 12 * (fbm(x / 30, z / 30, self.seed) - .5):
                return (.93, .95, .97), 0
            return ((.40, .45, .52) if y > 45 else (.28, .42, .24)), 0
        if d < 22:
            return self.ground()
        field = self.FIELDS[int(lattice(math.floor(x / 31), math.floor(z / 27), self.seed) * len(self.FIELDS))]
        return field or self.C['grass'], -self.T.ground

    def dress(self, kit):
        b, rng, land = kit.batch, kit.rng, kit.land
        for x, y, z, d in land.sites(kit, 500, 6, 240, below=60):
            if self.cone(x, z) > y - 1:
                continue
            color = rng.choice(self.BLOSSOM if rng.random() < .4 else self.GREENS)
            if d < 60:
                broadleaf(b, x, y, z, rng.uniform(1., 1.6), color)
            else:
                clump(b, x, y, z, rng.uniform(3.5, 6.5), color, rng)
        for x, y, z, d in land.sites(kit, 10, 30, 280, flat=.92, below=60):
            house(b, rng, x, y, z, math.atan2(z, x), (.90, .88, .82), (.22, .24, .28), .6)


class Harbor(Style):
    """Harbor Lights: across the basin the city goes on, with its quays and cranes."""
    seabed = (.30, .32, .32)
    INLAND = math.pi / 2
    QUAY = 1.6
    WALLS = ((.62, .60, .56), (.52, .50, .48), (.70, .66, .58), (.46, .40, .36), (.58, .54, .50))
    CONTAINERS = ((.70, .16, .10), (.12, .36, .62), (.15, .48, .30), (.86, .52, .10), (.42, .44, .46), (.60, .20, .48))

    def off(self, a):
        return abs((a - self.INLAND + math.pi) % (2 * math.pi) - math.pi)

    def shore(self, a):
        """Where the mainland begins beyond the outline; open sea outside its arc."""
        return 150 + 30 * math.sin(3 * a + 1) + 900 * smoothstep(1.0, 1.5, self.off(a))

    def relief(self, x, z, d, y):
        D = self.shore(math.atan2(z, x))
        return y + (self.QUAY - y) * smoothstep(D, D + 5, d)

    def wanted(self, x, z):
        a = math.atan2(z, x)
        D = self.shore(a)
        return 6. if D < 600 and abs(self.land.beyond(x, z) - D - 2.5) < 14 else None

    def paint(self, x, y, z, ny, d):
        if y > self.QUAY - .2:
            return self.ground()
        if d < SHORE:
            return self.C['edge'], 0
        return ((.46, .46, .44) if y > SEA_LEVEL - 1 else self.seabed), 0

    def dress(self, kit):
        b, rng, land, T = kit.batch, kit.rng, kit.land, kit.T
        placed = []
        for _ in range(900):
            a = self.INLAND + rng.uniform(-1.1, 1.1)
            D = self.shore(a)
            d = D + 14 + 320 * rng.random() ** 1.3
            x, z = polar(T, a, d)
            w, depth = rng.uniform(14, 28), rng.uniform(12, 22)
            radius = math.hypot(w, depth) / 2
            if any(math.hypot(x - px, z - pz) < radius + pr + 3 for px, pz, pr in placed):
                continue
            hit = kit.ground(x, z)
            if hit is None or hit[0] < self.QUAY - .1:
                continue
            placed.append((x, z, radius))
            floors = int(3 + rng.random() * (4 + 11 * smoothstep(D, D + 260, d)))
            height = floors * 3.2
            yaw = facing(a) + rng.uniform(-.15, .15)
            y = hit[0]
            b.box((x, y + height / 2 - .5, z), (w, height + 1, depth), rng.choice(self.WALLS), yaw)
            b.box((x, y + height + .3, z), (w - 1, .6, depth - 1), (.18, .19, .20), yaw)
            # Strips of office windows on the side facing the island, some lit.
            c, s = math.cos(yaw), math.sin(yaw)
            for f in range(floors):
                h = 1.2 + f * 3.2
                segments = rng.randint(1, 3)
                span = (w - 2) / segments
                for k in range(segments):
                    a0 = -w / 2 + 1 + k * span + .3
                    color = GLOW if rng.random() < .45 else (.10, .16, .22)
                    b.quad([(x + c * u + s * (depth / 2 + .05), y + v, z - s * u + c * (depth / 2 + .05))
                            for u, v in ((a0, h), (a0 + span - .6, h), (a0 + span - .6, h + 1.4), (a0, h + 1.4))],
                           color)
            if len(placed) >= 110:
                break
        # Container cranes and stacks along the quay facing the island.
        crane = (.92, .66, .10)
        for k in range(5):
            a = self.INLAND + (k - 2) * .32
            D = self.shore(a)
            x, z = polar(T, a, D + 8)
            yaw = facing(a)
            c, s = math.cos(yaw), math.sin(yaw)

            def P(u, h, v):
                return (x + c * u + s * v, self.QUAY + h, z - s * u + c * v)
            for u in (-5, 5):
                for v in (-4, 4):
                    b.prism(P(u, -.5, v), P(u, 28, v), .45, crane, 4)
            b.box(P(0, 28.5, 4), (12, 1.6, 36), crane, yaw)
            b.box(P(0, 30.5, -2), (6, 3, 6), (.86, .84, .80), yaw)
            for row in range(3):
                for stack in range(rng.randint(1, 3)):
                    b.box(P(rng.uniform(-14, 14), 1.3 + stack * 2.6, -10 - row * 3.2), (6, 2.6, 2.5),
                          rng.choice(self.CONTAINERS), yaw)


class Moor(Style):
    """Highland Loch: open moorland rising to bare mountains."""

    def relief(self, x, z, d, y):
        return (y + rise(d, 40, 260) * (35 + 170 * ridges(x / 240, z / 240, self.seed) ** 1.3)
                + rise(d, 30, 90) * 4 * (fbm(x / 45, z / 45, self.seed + 4) - .5))

    def paint(self, x, y, z, ny, d):
        if ny < .7 and d > 30:
            return ((.42, .42, .40) if ny < .55 else (.50, .50, .46)), 0
        if y > self.land.top + 130:
            return (.52, .50, .44), 0
        return self.ground()

    def dress(self, kit):
        b, rng, land = kit.batch, kit.rng, kit.land
        pine = (.16, .26, .17)
        for cx, cy, cz, d in land.sites(kit, 7, 60, 300, flat=.85):
            yaw = rng.uniform(0, 3.2)
            c, s = math.cos(yaw), math.sin(yaw)
            for i in range(-3, 4):
                for j in range(-2, 3):
                    u, v = i * 6 + rng.uniform(-1, 1), j * 6 + rng.uniform(-1, 1)
                    x, z = cx + c * u + s * v, cz - s * u + c * v
                    hit = kit.ground(x, z)
                    if hit and land.beyond(x, z) > 6:
                        b.prism((x, hit[0] - .2, z), (x, hit[0] + 7.5, z), 1.7, pine, 6, 0)
        for x, y, z, d in land.sites(kit, 5, 40, 280, flat=.9):
            house(b, rng, x, y, z, math.atan2(z, x), (.55, .54, .50), (.22, .24, .27), .5)


class Volcanic(Style):
    """Volcano Peak: dark volcanic islands in the sea, their craters aglow."""
    seabed = (.18, .17, .17)

    def __init__(self, land):
        super().__init__(land)
        rng = random.Random(self.seed)
        self.cones = []
        for k in range(4):
            a = (k + rng.uniform(.1, .7)) * math.pi / 2
            x, z = polar(self.T, a, rng.uniform(170, 330))
            self.cones.append((x, z, rng.uniform(70, 150), rng.uniform(45, 160)))

    def cone(self, x, z):
        best = -1e9
        for cx, cz, radius, height in self.cones:
            t = 1 - math.hypot(x - cx, z - cz) / radius
            if t > 0:
                h = SEA_LEVEL - 3 + (height + 3) * t ** 1.3
                if t > .82:
                    h = min(h, SEA_LEVEL - 3 + (height + 3) * .82 ** 1.3 - 14 * smoothstep(.82, 1, t))
                best = max(best, h + 3 * (fbm(x / 25, z / 25, self.seed) - .5) * t)
        return best

    def relief(self, x, z, d, y):
        return max(y, self.cone(x, z))

    def wanted(self, x, z):
        return 14. if any(math.hypot(x - cx, z - cz) < r + 10 for cx, cz, r, _ in self.cones) else None

    def paint(self, x, y, z, ny, d):
        for cx, cz, radius, height in self.cones:
            if math.hypot(x - cx, z - cz) < radius * .17 and y > height * .5:
                return GLOW, 0
        if d < SHORE and y > SEA_LEVEL - .3:
            return self.C['edge'], 0
        if y < SEA_LEVEL + 1.5:
            return self.seabed, 0
        return ((.24, .22, .21) if y < 40 else (.30, .28, .27)), 0


STYLES = {'farmland': Farmland, 'coast': Coast, 'alpine': Alpine, 'desert': Desert, 'forest': Forest,
          'rainforest': Rainforest, 'garden': Garden, 'harbor': Harbor, 'moor': Moor, 'volcanic': Volcanic}


# --- Blender ------------------------------------------------------------------------------------
def island_outline(T, vs, fs):
    """The island mesh's own boundary vertices on its outline, refinements included."""
    uses = {}
    for face in fs:
        for a, b in zip(face, face[1:] + face[:1]):
            key = (a, b) if a < b else (b, a)
            uses[key] = uses.get(key, 0) + 1
    rim = set()
    for (a, b), n in uses.items():
        if n == 1 and T.island(vs[a][0], vs[a][2]) > .995 and T.island(vs[b][0], vs[b][2]) > .995:
            rim.update((a, b))
    return [vs[i] for i in rim]


def terrain_mesh(api, land, vs, fs):
    """Colours per vertex, so that fields and snowlines blend instead of
    following the triangles; each face only chooses ground shading or none."""
    rng = random.Random(land.seed + 1)
    normals = [[0., 0., 0.] for _ in vs]
    paints, smooth = [], []
    for face in fs:
        a, b, c = (vs[i] for i in face)
        u = [b[k] - a[k] for k in range(3)]
        v = [c[k] - a[k] for k in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        if n[1] < 0:
            n = tuple(-q for q in n)
        for i in face:
            for k in range(3):
                normals[i][k] += n[k]
        x, y, z = ((a[k] + b[k] + c[k]) / 3 for k in range(3))
        d = land.beyond(x, z)
        ny = n[1] / (math.sqrt(sum(q * q for q in n)) or 1)
        paints.append(land.style.paint(x, y, z, ny, d)[1] != 0)
        smooth.append(d < SMOOTH)
    colors = []
    for (x, y, z), n in zip(vs, normals):
        ny = n[1] / (math.sqrt(sum(q * q for q in n)) or 1)
        color, _ = land.style.paint(x, y, z, ny, land.beyond(x, z))
        if color != GLOW:
            tone = rng.choice(TONES)
            color = tuple(min(1., q * tone) for q in color)
        colors.append(color)
    C = land.T.colors
    plain = api['mat']('Land ' + land.T.key, C['grass'])
    ground = api['mat']('Land ground ' + land.T.key, C['grass'], -land.T.ground)
    obj = api['mesh']('Surrounding land', vs, fs, plain)
    obj.data.materials.append(ground)
    for poly, paint, s in zip(obj.data.polygons, paints, smooth):
        poly.material_index = int(paint)
        poly.use_smooth = s
    # The exporter takes this point attribute as each vertex's colour.
    attribute = obj.data.color_attributes.new(name='Riverbed', type='FLOAT_COLOR', domain='POINT')
    for item, color in zip(attribute.data, colors):
        item.color = (*color, 1.)
    obj['land'] = True
    return obj


def fence(api, T, land, ground, water_sample):
    """Posts and rails, wire or a dry-stone wall just outside the outline."""
    kind = T.land.get('fence')
    if not kind:
        return []
    color = T.land['fence_color']
    batch = Batch(api, 'Boundary fence ' + T.key, land=False)
    spacing = {'rail': 3.6, 'wire': 5., 'wall': 3.}[kind]
    ring = [(x * (1 + .8 / math.hypot(x, z)), z * (1 + .8 / math.hypot(x, z))) for _, x, _, z in land.outline]
    posts, carry = [], 0.
    for (ax, az), (bx, bz) in zip(ring, ring[1:] + ring[:1]):
        length = math.hypot(bx - ax, bz - az)
        s = carry
        while s < length:
            posts.append((ax + (bx - ax) * s / length, az + (bz - az) * s / length))
            s += spacing
        carry = s - length
    points = []
    for x, z in posts:
        hit = ground(x, z)
        # Gaps where a stream or the sea crosses the line.
        wet = (hit is None or hit[0] < SEA_LEVEL + .3 or water_sample(x, z)[0] > -1.8
               or (land.outlet and land.outlet.lateral(x, z) < 1.5))
        points.append(None if wet else (x, hit[0], z))
    for i, p in enumerate(points):
        q = points[(i + 1) % len(points)]
        if p is None:
            continue
        if kind == 'wall':
            if q:
                batch.wall(p, q, .62, .85 + .1 * math.sin(i * 1.7), color)
            continue
        batch.prism((p[0], p[1] - .3, p[2]), (p[0], p[1] + (1.15 if kind == 'rail' else 1.25), p[2]), .09, color, 4)
        if q:
            for h in ((.45, .95) if kind == 'rail' else (.35, .7, 1.05)):
                batch.prism((p[0], p[1] + h, p[2]), (q[0], q[1] + h, q[2]), .07 if kind == 'rail' else .025,
                            color if kind == 'rail' else (.25, .25, .27), 3, cap=False)
    return batch.build()


def build(api, T, island_vs, island_fs, watershed, water_sample):
    """Author the land beyond the island; returns its extra water triangles."""
    land = Land(T, island_outline(T, island_vs, island_fs), watershed)
    vs, fs = land.grid()
    fixed = len(land.outline)
    vs, fs = refine(vs, fs, land.wanted, fixed)
    vs = [v if i < fixed else (v[0], land.height(v[0], v[2]), v[2]) for i, v in enumerate(vs)]
    terrain_mesh(api, land, vs, fs)
    xyz = api['xyz']
    bvh = BVHTree.FromPolygons([Vector(xyz(v)) for v in vs], fs, all_triangles=True)

    def ground(x, z):
        hit, normal, _, _ = bvh.ray_cast(Vector((x, -z, 2000)), Vector((0, 0, -1)))
        return None if hit is None else (hit.z, abs(normal.z))
    kit = Kit(api, land, ground)
    land.style.dress(kit)
    kit.batch.build()
    fence(api, T, land, ground, water_sample)
    print('LAND', T.key, len(fs), 'terrain triangles', flush=True)
    return land.water()
