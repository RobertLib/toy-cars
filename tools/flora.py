"""Trees, shrubs and rocks for the circuits, modeled as signed distance fields.

Each species is built once per variant at unit scale: smooth parts meshed and
decimated by tools/cars.py, coloured per vertex with a gradient and baked
ambient occlusion, then stored as one Blender mesh. Placements are linked
duplicates of that mesh, so a forest costs one mesh per variant to author. The
colours travel in the 'Riverbed' point colour layer that build_assets.export()
reads in place of the material colour.

Game axes: x right, y up, z forward. Trees stand on their origin, about 7 m
tall at scale 1, which is the height the wind shader bends them over.
"""
import math

import numpy as np

import sdf
from sdf import smin, smax, union, subtract, box, ellipsoid, capsule, cylinder_y
from cars import Part, Mesh, bake_ao

LIBRARY = {}


# --- Noise -----------------------------------------------------------------------------------
def _hash(ix, iy, iz, seed):
    h = (ix * 374761393 + iy * 668265263 + iz * 2147483647 + seed * 1274126177) & 0xffffffff
    h = ((h ^ (h >> 13)) * 1274126177) & 0xffffffff
    return ((h ^ (h >> 16)) & 0xffff) / 65535.


def noise(x, y, z, seed=0):
    """Smooth value noise in [-1, 1] with unit feature size."""
    x, y, z = np.broadcast_arrays(x, y, z)
    fx, fy, fz = np.floor(x), np.floor(y), np.floor(z)
    ix, iy, iz = fx.astype(np.int64), fy.astype(np.int64), fz.astype(np.int64)
    tx, ty, tz = x - fx, y - fy, z - fz
    tx, ty, tz = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty), tz * tz * (3 - 2 * tz)
    out = 0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (tx if dx else 1 - tx) * (ty if dy else 1 - ty) * (tz if dz else 1 - tz)
                out = out + w * _hash(ix + dx, iy + dy, iz + dz, seed)
    return out * 2 - 1


def fbm(x, y, z, seed=0, octaves=3):
    total, amp, freq = 0, .5, 1.
    for o in range(octaves):
        total = total + amp * noise(x * freq, y * freq, z * freq, seed + o * 31)
        amp *= .5
        freq *= 2.03
    return total


def hash01(*values):
    """A stable pseudo-random number from placement coordinates."""
    h = 2166136261
    for v in values:
        h = ((h ^ (int(round(v * 1000)) & 0xffffffff)) * 16777619) & 0xffffffff
    return (h & 0xffffff) / float(0xffffff)


# --- Specimens -------------------------------------------------------------------------------
class Piece:
    """A part of a specimen with a colouring rule colour(points, normals) -> (n, 3)."""

    def __init__(self, field, lo, hi, voxel, budget, colour, frond=False):
        self.part = Part(field, lo, hi, voxel, 0, (1, 1, 1), budget, adaptivity=.02)
        self.colour, self.frond = colour, frond


def bake(pieces, reach=1.2, rays=32):
    """Meshes, colours and shades pieces; returns one (points, normals, faces, colours) per piece."""
    meshes = [p.part.mesh() for p in pieces]
    occluders = [(p.part.points, p.part.faces) for p in pieces]
    out = []
    for piece, mesh in zip(pieces, meshes):
        bake_ao(mesh, occluders, rays=rays, reach=reach)
        colour = piece.colour(mesh.points, mesh.normals)
        # Thin fronds let light through, so occlusion darkens them less.
        shade = ((.7 + .3 * mesh.ao) if piece.frond else (.5 + .5 * mesh.ao))[:, None]
        out.append((mesh.points, mesh.normals, mesh.faces, np.clip(colour * shade, 0, 1)))
    return out


def blender_mesh(name, points, faces, colours, material):
    """A smooth-shaded Blender mesh with per-vertex colour in the 'Riverbed' layer."""
    import bpy
    me = bpy.data.meshes.new(name)
    me.vertices.add(len(points))
    me.vertices.foreach_set('co', np.stack([points[:, 0], -points[:, 2], points[:, 1]], 1).ravel())
    me.loops.add(faces.size)
    me.loops.foreach_set('vertex_index', faces.ravel())
    me.polygons.add(len(faces))
    me.polygons.foreach_set('loop_start', np.arange(0, faces.size, 3))
    me.update(calc_edges=True)
    me.polygons.foreach_set('use_smooth', np.ones(len(faces), bool))
    layer = me.color_attributes.new('Riverbed', 'FLOAT_COLOR', 'POINT')
    layer.data.foreach_set('color', np.concatenate([colours, np.ones((len(colours), 1))], 1).ravel())
    me.materials.append(material)
    me.use_fake_user = True
    return me


def specimen(species, variant, build):
    """The cached Blender meshes of one species variant: [(mesh, frond flag)]."""
    species = '-'.join(species) if isinstance(species, tuple) else species
    key = (species, variant)
    if key not in LIBRARY:
        import bpy
        pieces = build(variant)
        material = bpy.data.materials.get('Flora') or bpy.data.materials.new('Flora')
        material['paint'] = 0
        baked = bake(pieces)
        LIBRARY[key] = [(blender_mesh('%s %d.%d' % (species, variant, i), p, f, c, material), piece.frond)
                        for i, ((p, n, f, c), piece) in enumerate(zip(baked, pieces))]
    return LIBRARY[key]


def plant(name, species, build, x, y, z, s, variants=3, tilt=0.):
    """Linked duplicates of a species variant at (x, y, z), turned by a stable hash.

    s is a uniform scale or game-axis (x, y, z) scales."""
    import bpy
    variant = int(hash01(x, z, 1) * variants) % variants
    yaw = hash01(x, z, 2) * math.tau
    objects = []
    parts = specimen(species, variant, build)
    # Plants with fronds (palms, bananas, tree ferns) bend further in the wind.
    pliant = any(frond for _, frond in parts)
    if pliant:
        # Fronds flutter about the middle of the crown, an offset from the root in game axes.
        co = [v.co for mesh, frond in parts if frond for v in mesh.vertices]
        sx, sy = (s, s) if np.isscalar(s) else (s[0], s[2])
        bx, by = sum(c.x for c in co) / len(co) * sx, sum(c.y for c in co) / len(co) * sy
        crown = (bx * math.cos(yaw) - by * math.sin(yaw), -(bx * math.sin(yaw) + by * math.cos(yaw)))
    for mesh, frond in parts:
        o = bpy.data.objects.new(name, mesh)
        bpy.context.collection.objects.link(o)
        o.location = (x, -z, y)
        o.rotation_euler = (tilt * math.cos(yaw), tilt * math.sin(yaw), yaw)
        o.scale = (s, s, s) if np.isscalar(s) else (s[0], s[2], s[1])
        if frond:
            o['frond'] = True
        if pliant:
            o['sway'] = .95
            o['crown'] = crown
        objects.append(o)
    return objects


# --- Colour helpers --------------------------------------------------------------------------
def mix(a, b, t):
    a, b = np.array(a, float), np.array(b, float)
    t = np.clip(t, 0, 1)[:, None]
    return a + (b - a) * t


def foliage(dark, light, top=None, seed=0, sun=.55, cover=.55):
    """Crown colouring: lighter towards the sky and the outside, mottled by noise.
    top (snow) settles on faces turned up more steeply than cover."""
    def colour(p, n):
        t = sun * (n[:, 1] * .5 + .5) + .25 + .35 * fbm(p[:, 0] * .9, p[:, 1] * .9, p[:, 2] * .9, seed)
        c = mix(dark, light, t)
        if top is not None:
            settled = n[:, 1] - .25 * fbm(p[:, 0] * 1.6, p[:, 1], p[:, 2] * 1.6, seed + 7) > cover
            c = np.where(settled[:, None], mix(c, top, np.ones(len(p))), c)
        return c
    return colour


def bark(base, root=None, streak=.18, seed=0):
    def colour(p, n):
        t = .5 + .5 * noise(p[:, 0] * 6, p[:, 1] * 1.5, p[:, 2] * 6, seed)
        c = np.array(base, float) * (1 - streak + streak * 2 * t)[:, None]
        if root is not None:
            c = mix(root, c, p[:, 1] / 1.2)
        return c
    return colour


# --- Trunks and crowns -----------------------------------------------------------------------
def limb(x, y, z, a, b, ra, rb):
    """A tapered branch from a to b."""
    pa = np.stack(np.broadcast_arrays(x - a[0], y - a[1], z - a[2]))
    ba = np.array(b, float) - np.array(a, float)
    h = np.clip((pa[0] * ba[0] + pa[1] * ba[1] + pa[2] * ba[2]) / ba.dot(ba), 0, 1)
    d = np.sqrt((pa[0] - ba[0] * h) ** 2 + (pa[1] - ba[1] * h) ** 2 + (pa[2] - ba[2] * h) ** 2)
    return d - (ra + (rb - ra) * h)


def trunk_field(height, base, top, branches=(), flare=1.8, lean=(0, 0)):
    def field(x, y, z):
        d = limb(x, y, z, (0, -.3, 0), (lean[0], height, lean[1]), base, top)
        # Root flare: a squat, lobed foot.
        a = np.arctan2(z, x)
        foot = np.sqrt(x * x + z * z) - base * flare * (1 + .25 * np.cos(5 * a)) * np.clip(1 - y / .7, 0, 1)
        d = smin(d, smax(foot, -y - .2, .05), .25)
        for (a0, a1, r0, r1) in branches:
            d = smin(d, limb(x, y, z, a0, a1, r0, r1), .12)
        return d
    return field


def crown_field(blobs, k=.55, bumps=.32, freq=1.25, seed=0):
    """Clustered foliage: smooth-united spheres roughened by noise."""
    def field(x, y, z):
        d = None
        for (cx, cy, cz, r) in blobs:
            s = np.sqrt((x - cx) ** 2 + (y - cy) ** 2 + (z - cz) ** 2) - r
            d = s if d is None else smin(d, s, k)
        return d + bumps * fbm(x * freq, y * freq, z * freq, seed)
    return field


def bounds(blobs, pad=.6):
    lo = [min(b[i] - b[3] for b in blobs) - pad for i in range(3)]
    hi = [max(b[i] + b[3] for b in blobs) + pad for i in range(3)]
    return lo, hi


# --- Species ---------------------------------------------------------------------------------
def broadleaf(variant, dark=(.07, .17, .06), light=(.32, .50, .13)):
    """A rounded meadow oak: a stout trunk forking into clustered, cauliflower lobes."""
    rng = np.random.default_rng(100 + variant)
    blobs = [(0, 4.7, 0, 1.5)]
    # Lobes on a domed shell, smaller towards the top, the underside left open.
    for k in range(11):
        a = k * 2.4 + rng.uniform(-.3, .3)
        h = rng.uniform(0, 1)
        rad = 1.9 - .9 * h
        blobs.append((math.cos(a) * rad, 3.9 + 2.3 * h, math.sin(a) * rad, rng.uniform(.85, 1.25) - .2 * h))
    forks = rng.uniform(0, math.tau, 4)
    branches = [((0, 2.1, 0), (math.cos(a) * 1.3, 4.0 + .3 * i, math.sin(a) * 1.3), .17, .06)
                for i, a in enumerate(forks)]
    lo, hi = bounds(blobs)
    return [Piece(trunk_field(3.6, .33, .14, branches, flare=1.7), (-1.8, -.3, -1.8), (1.8, 4.6, 1.8), .035, 160,
                  bark((.25, .19, .14), (.17, .14, .11))),
            Piece(crown_field(blobs, k=.35, bumps=.22, freq=1.6, seed=variant), lo, hi, .06, 520,
                  foliage(dark, light, seed=variant))]


def fir(variant, needles=((.10, .27, .18), (.27, .46, .29)), snow=None, tiers=5, height=7.2, spread=2.2,
        budget=480):
    """A layered spruce: drooping, scalloped tiers that narrow to a spire."""
    rng = np.random.default_rng(200 + variant)
    phase = rng.uniform(0, math.tau, tiers)

    def field(x, y, z):
        r = np.sqrt(x * x + z * z)
        a = np.arctan2(z, x)
        d = None
        for k in range(tiers):
            t = k / (tiers - 1)
            base = 1.1 + t * (height - 2.6)
            span = spread * (1 - .78 * t) + .25
            tall = 2.2 - .6 * t
            lobes = 1 + .13 * np.cos(9 * a + phase[k]) + .05 * np.cos(17 * a + 2 * phase[k])
            # A cone whose rim droops below its base.
            u = (r / (span * lobes))
            cone = (y - base) - tall * (1 - u) + .35 * u * u
            tier = smax(cone * .6, -(y - base + .45 * u * u + .1) * .8, .12)
            tier = smax(tier, r - span * lobes, .15)
            d = tier if d is None else smin(d, tier, .25)
        spire = limb(x, y, z, (0, height - 1.2, 0), (0, height + .25, 0), .22, .02)
        return smin(d, spire, .2) + .06 * fbm(x * 3, y * 3, z * 3, variant)
    trunk = trunk_field(2.0, .22, .14, flare=1.6)
    colour = foliage(needles[0], needles[1], snow, seed=variant, sun=.45, cover=.25)
    return [Piece(trunk, (-.6, -.3, -.6), (.6, 2.2, .6), .03, max(16, budget // 5), bark((.33, .22, .15))),
            Piece(field, (-spread - .6, .5, -spread - .6), (spread + .6, height + .5, spread + .6), .06, budget, colour)]


class Sheet:
    """A ready-made mesh piece (thin leaves the voxel grid cannot hold)."""

    class _Part:
        def __init__(self, points, faces, normals):
            self.points, self.faces, self.normals = points, faces, normals

        def mesh(self, detail=1.):
            return Mesh(self.points, self.normals, self.faces, np.zeros(len(self.faces), np.int32),
                        np.ones((len(self.faces), 3)))

    def __init__(self, points, faces, colour, frond=False):
        points, faces = np.asarray(points, float), np.asarray(faces, np.int64)
        fn = np.cross(points[faces[:, 1]] - points[faces[:, 0]], points[faces[:, 2]] - points[faces[:, 0]])
        normals = np.zeros_like(points)
        for k in range(3):
            np.add.at(normals, faces[:, k], fn)
        normals /= np.maximum(np.linalg.norm(normals, axis=1)[:, None], 1e-12)
        self.part = Sheet._Part(points, faces, normals)
        self.colour, self.frond = colour, frond


def frond_sheet(top, angle, length, droop, rng, segments=10, width=.5):
    """A pinnate palm leaf: an arching midrib with saw-toothed leaflets either side."""
    points, faces = [], []
    ca, sa = math.cos(angle), math.sin(angle)
    rise = rng.uniform(.5, .9)
    for i in range(segments + 1):
        t = i / segments
        # Arch up from the crown, then droop towards the tip.
        along = length * t
        h = rise * math.sin(t * math.pi * .6) * length * .35 - droop * t * t * length
        cx, cy, cz = top[0] + ca * along, top[1] + h, top[2] + sa * along
        w = width * math.sin(math.pi * min(1, t * 1.15)) * (1.15 if i % 2 else .8)
        sag = .18 * w
        points += [(cx - sa * w, cy - sag, cz + ca * w), (cx, cy + .03, cz), (cx + sa * w, cy - sag, cz - ca * w)]
        if i:
            a, b = 3 * (i - 1), 3 * i
            faces += [(a, a + 1, b + 1), (a, b + 1, b), (a + 1, a + 2, b + 2), (a + 1, b + 2, b + 1)]
    return points, faces


def palm(variant, leaves=((.10, .30, .12), (.36, .58, .20)), fronds=9):
    """A coconut palm leaning over: a ringed trunk, an arching crown and coconuts."""
    rng = np.random.default_rng(300 + variant)
    lean = .7

    def axis(t):
        return lean * t * t, 7.0 * t

    def trunk(x, y, z):
        t = np.clip(y / 7.0, 0, 1)
        cx = lean * t * t
        r = .26 - .09 * t
        rings = .018 * np.cos(y * 2 * math.pi / .32)
        d = np.sqrt((x - cx) ** 2 + z * z) - r - rings
        d = smax(d, np.maximum(-y - .2, y - 7.05), .05)
        return smin(d, np.sqrt(x * x + z * z) - .42 * np.clip(1 - y / .5, 0, 1) - .02 + 0 * y, .2)

    top = (lean, 6.95, 0.)
    points, faces = [], []
    for k in range(fronds):
        a = k * math.tau / fronds + rng.uniform(-.2, .2)
        p, f = frond_sheet(top, a, rng.uniform(3.6, 4.4), rng.uniform(.75, 1.05), rng)
        base = len(points)
        points += p
        faces += [(i + base, j + base, l + base) for i, j, l in f]
    nuts = [(lean + .28 * math.cos(a), 6.65, .28 * math.sin(a), .19) for a in (0.4, 2.5, 4.4)]

    def coconuts(x, y, z):
        return union(*[np.sqrt((x - cx) ** 2 + (y - cy) ** 2 + (z - cz) ** 2) - r for cx, cy, cz, r in nuts], k=.03)

    def ringed(p, n):
        c = bark((.47, .36, .24), seed=variant)(p, n)
        return c * (.82 + .18 * np.cos(p[:, 1] * 2 * math.pi / .32))[:, None]
    return [Piece(trunk, (-.6, -.3, -.6), (1.2, 7.2, .6), .03, 260, ringed),
            Piece(coconuts, (.2, 6.3, -.6), (1.2, 7.0, .6), .02, 96, bark((.36, .25, .12), streak=.1)),
            Sheet(points, faces, foliage(leaves[0], leaves[1], seed=variant, sun=.6), frond=True)]


def boulder(variant, colour=(.52, .52, .50), moss=None, budget=180):
    """A weathered rock about a metre in radius: a chunk cut by rounded planes, origin at its centre."""
    rng = np.random.default_rng(400 + variant)
    planes = []
    for k in range(9):
        n = rng.normal(size=3)
        n[1] = abs(n[1]) * .6 if k < 3 else n[1]
        n /= np.linalg.norm(n)
        planes.append((n, rng.uniform(.55, .82)))

    def field(x, y, z):
        d = ellipsoid(x, y, z, (0, 0, 0), (1.05, 1.0, 1.05))
        for n, off in planes:
            d = smax(d, (x * n[0] + y * n[1] + z * n[2]) - off, .07)
        return d + .045 * fbm(x * 2.2, y * 2.2, z * 2.2, variant) + .02 * noise(x * 7, y * 7, z * 7, variant)

    def paint(p, n):
        t = .5 + .5 * fbm(p[:, 0] * 1.7, p[:, 1] * 1.7, p[:, 2] * 1.7, variant + 3)
        c = np.array(colour, float) * (.78 + .3 * t + .12 * p[:, 1])[:, None]
        if moss is not None:
            cover = (n[:, 1] > .45) & (noise(p[:, 0] * 2.5, p[:, 1] * 2.5, p[:, 2] * 2.5, variant + 9) > -.2)
            c = np.where(cover[:, None], mix(c, moss, np.full(len(p), .8)), c)
        return c
    return [Piece(field, (-1.2, -1.2, -1.2), (1.2, 1.2, 1.2), .03, budget, paint)]


def rock(name, colour, moss=None, budget=180):
    """A plant() builder and species key for rocks of one colour."""
    key = ('rock', '%.3f %.3f %.3f' % tuple(colour), 'moss' if moss else '', str(budget))
    return key, (lambda v: boulder(v, colour, moss, budget))


def shrub(variant, dark=(.12, .24, .08), light=(.36, .52, .18)):
    """A low garden shrub about a metre across, standing on its origin."""
    rng = np.random.default_rng(500 + variant)
    blobs = [(rng.uniform(-.3, .3), rng.uniform(.3, .6), rng.uniform(-.3, .3), rng.uniform(.32, .45)) for _ in range(6)]
    lo, hi = bounds(blobs, .3)
    lo[1] = -.1
    return [Piece(crown_field(blobs, k=.18, bumps=.08, freq=4.5, seed=variant), lo, hi, .03, 160,
                  foliage(dark, light, seed=variant))]
