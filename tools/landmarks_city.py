"""Landmarks of the jungle, sakura and harbor circuits, modeled like the cars.

Every model is built once from signed distance pieces (tools/flora.py Piece:
a field, its bounds, voxel, triangle budget and a colouring rule), baked with
ambient occlusion and placed as linked duplicates through tools/roadside.py.
Models stand on y = 0 with their front towards +z unless noted. Lamps, torches
and lit windows keep the exact lantern colour biomes.GLOW, unshaded, so the
renderer lights them after dark.
"""
import math
from types import SimpleNamespace

import numpy as np

import sdf
from sdf import smin, smax, union, subtract, box, rect2, circle2, ellipsoid, capsule
from sdf import cylinder_x, cylinder_y, cylinder_z, polyline2, rounded_polygon, Profile
import flora
from flora import Piece, noise, fbm, mix
import roadside

GLOW = (.98, .80, .40)


# --- Assembly --------------------------------------------------------------------------------
class Lit(Piece):
    """A piece that glows at night: coloured exactly GLOW, without occlusion."""

    def __init__(self, field, lo, hi, voxel, budget):
        Piece.__init__(self, field, lo, hi, voxel, budget, lambda p, n: np.tile(GLOW, (len(p), 1)))
        self.glow = True


def split_sharp(points, faces, colours, angle=35.):
    """Gives each smooth region its own copy of a vertex on a hard edge.

    The exporter averages the faces round a vertex into one normal; walls and
    roofs meeting at an edge would otherwise shade as if bent across it."""
    p = points[faces]
    fn = np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0])
    fn /= np.maximum(np.linalg.norm(fn, axis=1)[:, None], 1e-12)
    limit = math.cos(math.radians(angle))
    corners = faces.reshape(-1)
    order = np.argsort(corners, kind='stable')
    new_index = np.empty(len(corners), np.int64)
    out_points, out_colours = [], []
    start = 0
    while start < len(order):
        v = corners[order[start]]
        end = start
        while end < len(order) and corners[order[end]] == v:
            end += 1
        reps = []
        for c in order[start:end]:
            n = fn[c // 3]
            for i, (r, idx) in enumerate(reps):
                if r.dot(n) > limit:
                    new_index[c] = idx
                    break
            else:
                idx = len(out_points)
                reps.append((n, idx))
                out_points.append(points[v])
                out_colours.append(colours[v])
                new_index[c] = idx
        start = end
    return np.array(out_points), new_index.reshape(-1, 3), np.array(out_colours)


def model(pieces, reach=1.5, rays=32, sharp=35.):
    """Bakes pieces into one (mesh, colours) pair for roadside.place()."""
    baked = flora.bake(pieces, reach=reach, rays=rays)
    points, faces, colours, offset = [], [], [], 0
    for piece, (p, n, f, c) in zip(pieces, baked):
        if getattr(piece, 'glow', False):
            c = np.tile(np.array(GLOW, float), (len(p), 1))
        points.append(p)
        faces.append(f + offset)
        colours.append(c)
        offset += len(p)
    points, faces, colours = np.concatenate(points), np.concatenate(faces), np.concatenate(colours)
    if sharp:
        points, faces, colours = split_sharp(points, faces, colours, sharp)
    return SimpleNamespace(points=points, faces=faces), colours


def place(name, key, build, x, y, z, yaw=0., scale=None):
    o = roadside.place(name, ('city',) + tuple(key), build, x, y, z, yaw)
    if scale is not None:
        o.scale = (scale[0], scale[2], scale[1]) if not np.isscalar(scale) else (scale, scale, scale)
    return o


# --- Colourings ------------------------------------------------------------------------------
def solid(c):
    c = np.array(c, float)
    return lambda p, n: np.tile(c, (len(p), 1))


def stone(base, moss=None, vary=.16, seed=0, cover=.55):
    """Weathered stone: blotchy variation, moss on upward faces where noise allows."""
    def colour(p, n):
        t = .5 + .5 * fbm(p[:, 0] * .8, p[:, 1] * .8, p[:, 2] * .8, seed)
        c = np.array(base, float) * (1 - vary + 2 * vary * t)[:, None]
        if moss is not None:
            m = (n[:, 1] > cover) & (noise(p[:, 0] * .7, p[:, 1] * .7, p[:, 2] * .7, seed + 5) > -.15)
            c = np.where(m[:, None], mix(c, moss, np.full(len(p), .85)), c)
            low = np.clip(1 - p[:, 1] / 1.2, 0, 1) * (noise(p[:, 0] * 1.3, 0, p[:, 2] * 1.3, seed + 9) > 0)
            c = mix(c, moss, low * .6)
        return c
    return colour


def wood(base, seed=0):
    def colour(p, n):
        t = .5 + .5 * noise(p[:, 0] * 3, p[:, 1] * 9, p[:, 2] * 3, seed)
        return np.array(base, float) * (.85 + .3 * t)[:, None]
    return colour


# --- Shared shapes ---------------------------------------------------------------------------
def staircase(u, v, w, u_top, u_bot, top, rise, half, inner=0.):
    """A straight flight of steps climbing from (u_bot, 0) to (u_top, top), u outwards."""
    n = max(1, int(round(top / rise)))
    run = (u_bot - u_top) / n
    k0 = np.floor((u - u_top) / run)
    d = None
    for dk in (-1, 0, 1):
        k = np.clip(k0 + dk, 0, n - 1)
        edge = u_top + (k + 1) * run
        h = top - k * rise
        b = rect2(u, v, ((inner + edge) / 2, h / 2), ((edge - inner) / 2, h / 2), 0)
        d = b if d is None else np.minimum(d, b)
    return smax(d, np.abs(w) - half, .01)


def curved_roof(x, y, z, base, half_eave, half_top, rise, thick=.14, upturn=.45, tiles=.022):
    """A square hipped roof, concave like temple roofs, with upturned corners and tile ribs."""
    ax, az = np.abs(x), np.abs(z)
    r = np.maximum(ax, az)
    s = np.clip((r - half_top) / (half_eave - half_top), 0, 1)
    height = base + rise * (1 - s) ** 1.6 + upturn * (np.minimum(ax, az) / half_eave) ** 3 * s
    along = np.where(ax > az, z, x)
    height = height + tiles * np.cos(along * 2 * math.pi / .3) * s
    slab = (np.abs(y - height + thick / 2) - thick / 2) * .7
    return smax(slab, r - half_eave, .04)


# =============================================================================================
# Jungle Ruins
LIMESTONE = (.62, .60, .50)
JUNGLE_MOSS = (.26, .40, .16)
BASALT = (.47, .45, .39)
JADE = (.24, .54, .42)
OCHRE_RED = (.62, .24, .14)


def temple_pyramid():
    """Six battered tiers with ledges, a stairway up every face between ramp walls,
    a shrine with a dark doorway and an openwork roof comb, braziers either side."""
    top = 15.6

    def tiers(x, y, z):
        r = np.maximum(np.abs(x), np.abs(z))
        d = None
        for k in range(6):
            w = 30 - k * 4.2
            y0 = k * 2.6
            side = (r + .45 * np.clip((y - y0) / 2.6, 0, 1) - w / 2) * .99
            t = smax(side, np.abs(y - y0 - 1.3) - 1.3, .06)
            ledge = smax(r - w / 2 + .5 - .12, np.abs(y - y0 - 2.47) - .13, .03)
            t = smin(t, ledge, .02)
            d = t if d is None else np.minimum(d, t)
        return d

    def stairs(x, y, z):
        d = None
        for axis, sign in ((0, 1), (0, -1), (2, 1), (2, -1)):
            u = (x if axis == 0 else z) * sign
            w = z if axis == 0 else x
            flight = staircase(u, y, w, 4.5, 16.4, top, .52, 2.5, inner=2.0)
            slope = (y - top * (16.4 - u) / (16.4 - 4.5)) / 1.6 - .35
            ramp = smax(smax(slope, np.abs(np.abs(w) - 2.85) - .35, .03), np.maximum(4.0 - u, u - 16.6), .03)
            f = np.minimum(flight, smax(ramp, -y, 0))
            d = f if d is None else np.minimum(d, f)
        return d

    def shrine(x, y, z):
        body = box(x, y, z, (0, top + 2.0, 0), (3.5, 2.0, 3.5), .06)
        frieze = box(x, y, z, (0, top + 3.6, 0), (3.7, .35, 3.7), .04)
        cap = box(x, y, z, (0, top + 4.15, 0), (3.2, .2, 3.2), .04)
        comb = box(x, y, z, (0, top + 5.4, -.4), (2.5, 1.1, .45), .05)
        holes = None
        for hx in (-1.4, 0, 1.4):
            h = box(x, y, z, (hx, top + 5.3, -.4), (.38, .6, .7), .04)
            holes = h if holes is None else np.minimum(holes, h)
        comb = subtract(comb, holes, .03)
        d = union(body, frieze, cap, comb)
        door = box(x, y, z, (0, top + 1.6, 3.4), (1.0, 1.55, .5), .04)
        return subtract(d, door, .03)

    def doorway(x, y, z):
        return box(x, y, z, (0, top + 1.6, 3.05), (1.05, 1.6, .06), .02)

    def braziers(x, y, z):
        d = None
        for side in (-1, 1):
            stem = cylinder_y(x, y, z, (side * 2.6, top + .45, 4.2), .18, .45, .05)
            bowl = smax(ellipsoid(x, y, z, (side * 2.6, top + 1.05, 4.2), (.5, .32, .5)), top + .95 - y, .05)
            b = smin(stem, bowl, .08)
            d = b if d is None else np.minimum(d, b)
        return d

    def flames(x, y, z):
        d = None
        for side in (-1, 1):
            f = smin(ellipsoid(x, y, z, (side * 2.6, top + 1.2, 4.2), (.32, .25, .32)),
                     ellipsoid(x, y, z, (side * 2.6, top + 1.55, 4.2), (.14, .4, .14)), .12)
            d = f if d is None else np.minimum(d, f)
        return d
    lo, hi = (-17, -.2, -17), (17, top + 7, 17)
    return model([Piece(tiers, lo, hi, .08, 7000, stone(LIMESTONE, JUNGLE_MOSS, seed=1)),
                  Piece(stairs, lo, hi, .07, 5200, stone((.66, .64, .55), JUNGLE_MOSS, .1, seed=2, cover=.85)),
                  Piece(shrine, (-4, top - .1, -4), (4, top + 7, 4.2), .05, 2400, stone(LIMESTONE, JUNGLE_MOSS, seed=3, cover=.7)),
                  Piece(doorway, (-1.3, top - .1, 2.8), (1.3, top + 3.4, 3.3), .03, 60, solid((.05, .05, .045))),
                  Piece(braziers, (-3.3, top - .1, 3.4), (3.3, top + 1.5, 5.0), .025, 500, solid((.30, .29, .26))),
                  Lit(flames, (-3.1, top + .8, 3.7), (3.1, top + 2.1, 4.7), .02, 160)], reach=4)


def stone_head():
    """A colossal basalt head under a banded helmet, its face towards +z."""
    def head(x, y, z):
        d = ellipsoid(x, y, z, (0, 2.15, -.05), (1.75, 2.15, 1.6))
        d = smax(d, -(z + 1.2), .3)
        helmet = smax(ellipsoid(x, y, z, (0, 2.55, -.05), (1.88, 1.9, 1.72)), 2.95 - y, .12)
        band = smax(ellipsoid(x, y, z, (0, 2.55, -.05), (1.95, 1.95, 1.8)), np.abs(y - 3.0) - .14, .06)
        flaps = None
        for side in (-1, 1):
            f = ellipsoid(x, y, z, (side * 1.62, 2.25, -.2), (.32, .9, .62))
            flaps = f if flaps is None else np.minimum(flaps, f)
        d = union(d, helmet, band, flaps, k=.08)
        brow = capsule(x, y, z, (-.8, 2.55, 1.32), (.8, 2.55, 1.32), .16)
        nose = ellipsoid(x, y, z, (0, 1.95, 1.5), (.45, .55, .32))
        lips = np.minimum(capsule(x, y, z, (-.55, 1.32, 1.42), (.55, 1.32, 1.42), .2),
                          capsule(x, y, z, (-.5, 1.0, 1.36), (.5, 1.0, 1.36), .18))
        cheeks = np.minimum(ellipsoid(x, y, z, (-.85, 1.75, 1.05), (.5, .55, .45)),
                            ellipsoid(x, y, z, (.85, 1.75, 1.05), (.5, .55, .45)))
        d = union(d, brow, nose, lips, cheeks, k=.12)
        for side in (-1, 1):
            d = subtract(d, ellipsoid(x, y, z, (side * .55, 2.3, 1.55), (.32, .12, .2)), .06)
        d = subtract(d, capsule(x, y, z, (-.45, 1.16, 1.65), (.45, 1.16, 1.65), .05), .03)
        return smax(d, -y - .2, .1)
    return model([Piece(head, (-2.2, -.3, -1.9), (2.2, 4.7, 2.2), .03, 3000,
                        stone(BASALT, JUNGLE_MOSS, .12, seed=4, cover=.65))], reach=2)


def column(height, broken):
    """A fluted limestone column on a square base; broken ones end in a jagged stump."""
    def shaft(x, y, z):
        r = np.sqrt(x * x + z * z)
        a = np.arctan2(z, x)
        d = smax(r - .5 + .025 * np.maximum(0, np.cos(12 * a)), np.abs(y - height / 2) - height / 2, .03)
        if broken:
            cut = y - height + .25 * fbm(x * 3, 0, z * 3, int(height * 10)) + .15 * np.sin(a * 2 + height)
            d = smax(d, cut, .03)
        else:
            capital = box(x, y, z, (0, height - .18, 0), (.66, .18, .66), .04)
            echinus = smax(ellipsoid(x, y, z, (0, height - .38, 0), (.62, .22, .62)), y - height + .36, .03)
            d = union(d, capital, echinus, k=.03)
        base = smin(box(x, y, z, (0, .2, 0), (.68, .2, .68), .04), cylinder_y(x, y, z, (0, .48, 0), .58, .1, .05), .03)
        return smin(d, base, .03)
    return model([Piece(shaft, (-.8, -.2, -.8), (.8, height + .3, .8), .025, 520,
                        stone(LIMESTONE, JUNGLE_MOSS, seed=int(height * 7), cover=.6))], reach=1.2)


def lintel():
    def block(x, y, z):
        d = box(x, y, z, (0, 0, 0), (2.1, .35, .6), .05)
        crack = smax(np.abs(x - .4 - .2 * np.sin(y * 6)) - .02, -(d + .08), .01)
        return subtract(d, crack, .015) + .03 * fbm(x * 2, y * 2, z * 2, 7)
    return model([Piece(block, (-2.3, -.5, -.8), (2.3, .5, .8), .025, 300, stone(LIMESTONE, JUNGLE_MOSS, seed=8))])


def drum():
    """A fallen column drum lying on its side, axis along x."""
    def d(x, y, z):
        r = np.sqrt((y - .5) ** 2 + z * z)
        a = np.arctan2(z, y - .5)
        return smax(r - .5 + .02 * np.maximum(0, np.cos(12 * a)), np.abs(x) - .38, .04) + .02 * fbm(x * 3, y * 3, z * 3, 3)
    return model([Piece(d, (-.5, -.1, -.6), (.5, 1.1, .6), .02, 220, stone(LIMESTONE, JUNGLE_MOSS, seed=11, cover=.4))])


def torch():
    """A bamboo torch pole with a bound fire basket."""
    def pole(x, y, z):
        p = cylinder_y(x, y, z, (0, 1.1, 0), .065, 1.1, .02)
        nodes = cylinder_y(x, y, z, (0, 1.1, 0), .075, 1.1, .01)
        nodes = smax(nodes, np.abs(np.mod(y, .45) - .225) - .02, .005)
        basket = smax(cylinder_y(x, y, z, (0, 2.3, 0), .16, .14, .03), -cylinder_y(x, y, z, (0, 2.38, 0), .12, .1), .01)
        return union(p, nodes, basket, k=.02)

    def flame(x, y, z):
        return smin(ellipsoid(x, y, z, (0, 2.45, 0), (.15, .14, .15)), ellipsoid(x, y, z, (0, 2.65, 0), (.07, .22, .07)), .08)
    return model([Piece(pole, (-.25, -.05, -.25), (.25, 2.55, .25), .012, 200, wood((.55, .45, .22))),
                  Lit(flame, (-.2, 2.25, -.2), (.2, 2.95, .2), .012, 80)], reach=.6)


def jade_idol():
    """A seated jade figure in a fanned headdress on a carved stepped plinth."""
    def plinth(x, y, z):
        d = union(box(x, y, z, (0, .3, 0), (1.2, .3, 1.2), .04), box(x, y, z, (0, .8, 0), (.95, .22, .95), .04))
        glyphs = smax(np.abs(np.mod(x + .15, .5) - .25) - .16, np.abs(y - .3) - .16, .01)
        return subtract(d, smax(glyphs, -(d + .03), .005), .01)

    def figure(x, y, z):
        body = smax(ellipsoid(x, y, z, (0, 1.75, 0), (.42, .55, .3)), 1.15 - y, .05)
        legs = ellipsoid(x, y, z, (0, 1.22, .18), (.55, .2, .32))
        arms = np.minimum(capsule(x, y, z, (-.38, 1.95, .05), (-.25, 1.45, .3), .1),
                          capsule(x, y, z, (.38, 1.95, .05), (.25, 1.45, .3), .1))
        head = ellipsoid(x, y, z, (0, 2.5, .05), (.3, .33, .28))
        fan = smax(ellipsoid(x, y, z, (0, 2.85, -.12), (.7, .55, .12)), 2.55 - y, .05)
        d = union(body, legs, arms, head, fan, k=.06)
        for side in (-1, 1):
            d = subtract(d, ellipsoid(x, y, z, (side * .11, 2.55, .32), (.07, .03, .05)), .02)
        return d
    return model([Piece(plinth, (-1.4, -.1, -1.4), (1.4, 1.1, 1.4), .02, 800, stone(LIMESTONE, JUNGLE_MOSS, seed=12, cover=.7)),
                  Piece(figure, (-.9, .95, -.5), (.9, 3.5, .6), .015, 1400, stone(JADE, None, .2, seed=13))], reach=1.2)


def reed_clump():
    """A tuft of waterside reed blades as thin sheets."""
    rng = np.random.default_rng(31)
    points, faces = [], []
    for k in range(7):
        a = k * 2.4 + rng.uniform(-.3, .3)
        lean = rng.uniform(.1, .35)
        h = rng.uniform(.7, 1.25)
        base = (math.cos(a) * .08, 0, math.sin(a) * .08)
        tip = (base[0] + math.cos(a) * lean, h, base[2] + math.sin(a) * lean)
        side = (-math.sin(a) * .035, 0, math.cos(a) * .035)
        i = len(points)
        mid = tuple(base[j] + (tip[j] - base[j]) * .5 for j in range(3))
        points += [(base[0] - side[0], 0, base[2] - side[2]), (base[0] + side[0], 0, base[2] + side[2]),
                   (mid[0] - side[0] * .7, mid[1], mid[2] - side[2] * .7), (mid[0] + side[0] * .7, mid[1], mid[2] + side[2] * .7), tip]
        faces += [(i, i + 1, i + 3), (i, i + 3, i + 2), (i + 2, i + 3, i + 4)]
    return model([flora.Sheet(points, faces, lambda p, n: mix((.20, .36, .12), (.46, .58, .22), p[:, 1] / 1.2))], reach=.4)


def pebble(seed):
    """A small, cheap wet river stone about a metre across at unit scale."""
    rng = np.random.default_rng(500 + seed)
    planes = [(v / np.linalg.norm(v), rng.uniform(.5, .8)) for v in rng.normal(size=(6, 3))]

    def f(x, y, z):
        d = ellipsoid(x, y, z, (0, 0, 0), (1, .55, .8))
        for n, off in planes:
            d = smax(d, x * n[0] + y * n[1] + z * n[2] - off, .1)
        return d
    return f


def pebble_model(colour, seed):
    return model([Piece(pebble(seed), (-1.1, -.7, -.9), (1.1, .7, .9), .06, 40, stone(colour, None, .2, seed=seed))], reach=.5)


# =============================================================================================
# Sakura Hills
VERMILION = (.82, .16, .08)
LACQUER = (.08, .08, .09)
TILE = (.22, .25, .28)
PLASTER = (.92, .89, .80)
TIMBER = (.40, .22, .14)
GRANITE = (.62, .62, .58)
GOLD = (.78, .60, .22)


def pagoda():
    """A five-storey pagoda: plaster walls between vermilion posts, bracketed eaves,
    concave tiled roofs with upturned corners and a ringed bronze spire."""
    storeys = []
    h = 0.
    for k in range(5):
        w = 7 - k * .9
        storeys.append((h, w, 1.5 - k * .1))
        h += 3.5
    spire_base = h

    def walls(x, y, z):
        d = None
        for (h0, w, flare) in storeys:
            body = box(x, y, z, (0, h0 + 1.3, 0), (w / 2 - .25, 1.3, w / 2 - .25), .03)
            d = body if d is None else np.minimum(d, body)
        return d

    def frame(x, y, z):
        d = None
        for (h0, w, flare) in storeys:
            hw = w / 2
            for px in (-1, 0, 1):
                for pz in (-1, 1):
                    for swap in (False, True):
                        cx, cz = (px * (hw - .2), pz * (hw - .2)) if not swap else (pz * (hw - .2), px * (hw - .2))
                        p = box(x, y, z, (cx, h0 + 1.3, cz), (.16, 1.3, .16), .02)
                        d = p if d is None else np.minimum(d, p)
            r = np.maximum(np.abs(x), np.abs(z))
            beam = smax(np.abs(r - hw + .05) - .2, np.abs(y - (h0 + 2.45)) - .16, .02)
            brackets = smax(np.abs(r - hw - .3) - .3, np.abs(y - (h0 + 2.72)) - .12, .02)
            d = union(d, beam, brackets)
            if h0 > 0:
                # A balcony rail round every upper storey.
                d = np.minimum(d, smax(np.abs(r - hw - .12) - .08, np.abs(y - (h0 + .55)) - .07, .01))
        return d

    def roofs(x, y, z):
        d = None
        for (h0, w, flare) in storeys:
            r = curved_roof(x, y, z, h0 + 2.75, w / 2 + flare, w * .2, .95 if h0 < 14 else 1.3, tiles=.008)
            d = r if d is None else np.minimum(d, r)
        return d

    def spire(x, y, z):
        mast = cylinder_y(x, y, z, (0, spire_base + 1.6, 0), .1, 2.3, .03)
        rings = smax(cylinder_y(x, y, z, (0, spire_base + 1.6, 0), .3, 1.5, .02),
                     np.abs(np.mod(y - spire_base, .34) - .17) - .06, .02)
        jewel = ellipsoid(x, y, z, (0, spire_base + 4.05, 0), (.22, .3, .22))
        base = cylinder_y(x, y, z, (0, spire_base - .1, 0), .55, .3, .1)
        return union(mast, rings, jewel, base, k=.03)
    lo, hi = (-6, -.1, -6), (6, spire_base + 4.6, 6)
    return model([Piece(walls, lo, hi, .05, 900, solid(PLASTER)),
                  Piece(frame, lo, hi, .035, 5000, wood(VERMILION, 3)),
                  Piece(roofs, lo, hi, .05, 4400, stone(TILE, None, .1, seed=5)),
                  Piece(spire, (-.8, spire_base - .5, -.8), (.8, spire_base + 4.6, .8), .02, 500, solid(GOLD))], reach=2.5)


def torii():
    """A torii gate: vermilion posts on black sleeves, a tie beam and a black lintel
    sweeping up at its ends over a red under-lintel; centred, posts at x = +-2.2."""
    def red(x, y, z):
        posts = np.minimum(cylinder_y(x, y, z, (-2.2, 2.0, 0), .22, 2.6, .03), cylinder_y(x, y, z, (2.2, 2.0, 0), .22, 2.6, .03))
        nuki = box(x, y, z, (0, 3.9, 0), (2.85, .14, .16), .03)
        shimaki = box(x, y, z, (0, 4.5, 0), (3.1, .12, .22), .03)
        strut = box(x, y, z, (0, 4.2, 0), (.18, .2, .14), .02)
        return union(posts, nuki, shimaki, strut, k=.02)

    def black(x, y, z):
        lift = .18 * (np.abs(x) / 3.3) ** 2.5
        kasagi = smax(np.abs(y - 4.78 - lift) - .17, rect2(x, z, (0, 0), (3.35 - .3 * np.clip(y - 4.9, 0, 1), .27), .05), .03)
        sleeves = np.minimum(cylinder_y(x, y, z, (-2.2, .1, 0), .27, .55, .03), cylinder_y(x, y, z, (2.2, .1, 0), .27, .55, .03))
        return np.minimum(kasagi, sleeves)
    lo, hi = (-3.6, -.6, -.5), (3.6, 5.4, .5)
    return model([Piece(red, lo, hi, .02, 800, wood(VERMILION, 4)),
                  Piece(black, lo, hi, .02, 450, solid(LACQUER))], reach=1.2)


def stone_lantern():
    """A granite toro: a stepped base, a post, the fire box with lit openings, a curled roof and jewel."""
    def stonework(x, y, z):
        base = union(box(x, y, z, (0, .12, 0), (.42, .12, .42), .03), cylinder_y(x, y, z, (0, .32, 0), .3, .1, .04), k=.02)
        post = cylinder_y(x, y, z, (0, .95, 0), .14, .55, .02)
        middle = box(x, y, z, (0, 1.55, 0), (.36, .08, .36), .03)
        box_ = box(x, y, z, (0, 1.85, 0), (.3, .22, .3), .02)
        windows = np.minimum(box(x, y, z, (0, 1.85, 0), (.17, .14, .4), .01), box(x, y, z, (0, 1.85, 0), (.4, .14, .17), .01))
        box_ = subtract(box_, windows, .01)
        roof = curved_roof(x, y, z, 2.1, .5, .08, .28, thick=.1, upturn=.12, tiles=0)
        jewel = smin(ellipsoid(x, y, z, (0, 2.48, 0), (.08, .1, .08)), cylinder_y(x, y, z, (0, 2.38, 0), .06, .04, .01), .02)
        return union(base, post, middle, box_, roof, jewel, k=.015)

    def light(x, y, z):
        return box(x, y, z, (0, 1.85, 0), (.22, .14, .22), .01)
    lo, hi = (-.6, -.05, -.6), (.6, 2.65, .6)
    return model([Piece(stonework, lo, hi, .012, 650, stone(GRANITE, (.36, .44, .22), .14, seed=6, cover=.7)),
                  Lit(light, (-.3, 1.6, -.3), (.3, 2.1, .3), .012, 60)], reach=.8)


def tea_house():
    """A tea house: a timber deck on posts, shoji walls framed in dark timber,
    a tiled gable roof with deep eaves and four paper lanterns along the front."""
    def deck(x, y, z):
        floor = box(x, y, z, (0, .52, 0), (4.5, .08, 3.5), .02)
        planks = smax(np.abs(np.mod(x, .25) - .125) - .11, np.abs(y - .6) - .01, .005)
        floor = subtract(floor, planks, .005)
        posts = None
        for px in (-4.2, 0, 4.2):
            for pz in (-3.2, 3.2):
                p = box(x, y, z, (px, .25, pz), (.12, .3, .12), .02)
                posts = p if posts is None else np.minimum(posts, p)
        frame = None
        for px in (-3.5, -1.15, 1.15, 3.5):
            for pz in (-2.5, 2.5):
                p = box(x, y, z, (px, 1.9, pz), (.1, 1.3, .1), .015)
                frame = p if frame is None else np.minimum(frame, p)
        sill = smax(np.abs(np.maximum(np.abs(x) / 3.5, np.abs(z) / 2.5) - 1) * 2.5 - .06, np.abs(y - .68) - .06, .01)
        head = smax(np.abs(np.maximum(np.abs(x) / 3.5, np.abs(z) / 2.5) - 1) * 2.5 - .06, np.abs(y - 3.1) - .1, .01)
        return union(floor, posts, frame, sill, head)

    def lattice(x, y, z):
        # Shoji: a coarse grid of dark muntins over the paper of the front wall.
        bars = np.minimum(np.abs(np.mod(x + .29, .58) - .29) - .025, np.abs(np.mod(y - .75, .78) - .39) - .025)
        return smax(smax(bars, np.abs(z - 2.49) - .04, .005), np.maximum(np.abs(x) - 3.4, np.abs(y - 1.9) - 1.2), .005)

    def walls(x, y, z):
        return box(x, y, z, (0, 1.9, 0), (3.45, 1.25, 2.45), .02)

    def roof(x, y, z):
        slope = [(-4.4, 2.75), (0, 5.15), (4.4, 2.75)]
        slab = smax(polyline2(x, y, slope) - .12, np.abs(z) - 3.4, .03)
        slab = slab - .02 * np.cos(z * 2 * math.pi / .3) * (y < 5.0)
        ridge = smax(np.sqrt(x * x + (y - 5.18) ** 2) - .16, np.abs(z) - 3.5, .03)
        return smin(slab, ridge, .04)

    def strings(x, y, z):
        d = None
        for k in range(4):
            px = k * 2.2 - 3.3
            s = capsule(x, y, z, (px, 2.95, 3.35), (px, 2.55, 3.35), .012)
            cap = cylinder_y(x, y, z, (px, 2.53, 3.35), .1, .025, .01)
            foot = cylinder_y(x, y, z, (px, 1.98, 3.35), .1, .025, .01)
            d = union(*(t for t in (d, s, cap, foot) if t is not None))
        return d

    def lanterns(x, y, z):
        d = None
        for k in range(4):
            l = ellipsoid(x, y, z, (k * 2.2 - 3.3, 2.25, 3.35), (.22, .28, .22))
            d = l if d is None else np.minimum(d, l)
        return d
    lo, hi = (-4.8, -.1, -3.9), (4.8, 5.5, 3.9)
    return model([Piece(deck, lo, hi, .02, 2000, wood(TIMBER, 2)),
                  Piece(lattice, (-3.6, .6, 2.3), (3.6, 3.2, 2.7), .015, 1000, solid((.30, .17, .10))),
                  Piece(walls, lo, hi, .04, 200, solid(PLASTER)),
                  Piece(roof, lo, hi, .035, 1400, stone(TILE, None, .08, seed=7)),
                  Piece(strings, (-3.7, 1.8, 3.1), (3.7, 3.1, 3.6), .01, 150, solid(LACQUER)),
                  Lit(lanterns, (-3.7, 1.9, 3.0), (3.7, 2.6, 3.7), .015, 160)], reach=2)


def zen_garden():
    """Raked gravel in parallel furrows ringed round three mossy rocks, low timber fences."""
    rocks = [(-3, -1, 1.1), (2.5, 1.5, .8), (3.5, -2, .6)]

    def gravel(x, y, z):
        ring = None
        for rx, rz, r in rocks:
            q = np.sqrt((x - rx) ** 2 + (z - rz) ** 2) - r * 1.25
            ring = q if ring is None else np.minimum(ring, q)
        rake = np.where(ring < 1.2, np.cos(ring * 2 * math.pi / .3), np.cos(z * 2 * math.pi / .3))
        return box(x, y, z, (0, .04, 0), (5.5, .08 + .012 * rake, 4.0), .03)

    def stones(x, y, z):
        d = None
        for k, (rx, rz, r) in enumerate(rocks):
            e = ellipsoid(x, y, z, (rx, r * .35, rz), (r * 1.2, r * .8, r))
            e = smax(e, (x - rx) * .5 + (y - r * .7) - .1 * k, .1) + .05 * fbm(x * 1.5, y * 1.5, z * 1.5, k)
            d = e if d is None else np.minimum(d, e)
        return d

    def fences(x, y, z):
        d = None
        for side in (-1, 1):
            rails = smax(np.abs(np.abs(y - .55) - .25) - .04, np.abs(z - side * 4.4) - .05, .01)
            rails = smax(rails, np.abs(x) - 5.7, .01)
            posts = smax(smax(np.abs(np.mod(x + .25, 1.0) - .5) - .06, np.abs(z - side * 4.4) - .06, .01), np.abs(y - .5) - .5, .01)
            posts = smax(posts, np.abs(x) - 5.7, .01)
            f = np.minimum(rails, posts)
            d = f if d is None else np.minimum(d, f)
        return d
    lo, hi = (-6, -.1, -4.7), (6, 1.3, 4.7)
    return model([Piece(gravel, lo, hi, .03, 1400, stone((.80, .78, .72), None, .05, seed=8)),
                  Piece(stones, lo, hi, .03, 600, stone((.40, .41, .40), (.30, .42, .18), .15, seed=9)),
                  Piece(fences, lo, hi, .015, 1800, wood((.42, .30, .18)))], reach=1.5)


def festival_stall(awning):
    """A timber festival booth: a counter, corner posts and a striped awning, a lantern hanging."""
    def booth(x, y, z):
        counter = box(x, y, z, (0, .5, .9), (1.55, .5, .3), .03)
        top = box(x, y, z, (0, 1.04, .95), (1.65, .04, .4), .02)
        back = box(x, y, z, (0, 1.2, -1.1), (1.55, 1.2, .06), .02)
        posts = None
        for px in (-1.5, 1.5):
            for pz in (-1.1, 1.15):
                p = box(x, y, z, (px, 1.25, pz), (.07, 1.25, .07), .015)
                posts = p if posts is None else np.minimum(posts, p)
        return union(counter, top, back, posts)

    def canopy(on):
        def f(x, y, z):
            slab = smax(np.abs(y - 2.55 + .18 * (z - 1.5)) - .04, rect2(x, z, (0, .25), (1.8, 1.55), .03), .02)
            stripe = np.sin(x * math.pi / .45) * (1 if on else -1)
            return smax(slab, -stripe * 3, .002)
        return f

    def lantern(x, y, z):
        return ellipsoid(x, y, z, (1.25, 2.05, 1.55), (.17, .22, .17))
    lo, hi = (-1.9, -.1, -1.5), (1.9, 2.9, 1.9)
    return model([Piece(booth, lo, hi, .02, 800, wood(TIMBER, 5)),
                  Piece(canopy(True), lo, hi, .02, 300, solid(awning)),
                  Piece(canopy(False), lo, hi, .02, 300, solid((.94, .92, .88))),
                  Lit(lantern, (.9, 1.7, 1.2), (1.6, 2.4, 1.9), .012, 80)], reach=1)


def arched_bridge():
    """A drum bridge spanning 7 m along z: planked arch, vermilion rails, posts and bronze caps."""
    def arch(t):
        return .2 + 1.4 * (1 - (2 * t) ** 2)

    def deck(x, y, z):
        t = np.clip(z / 7, -.5, .5)
        surf = arch(t)
        slab = smax(np.abs(y - surf + .12) - .12, np.abs(x) - .7, .02)
        slab = smax(slab, np.abs(z) - 3.6, .02)
        planks = smax(np.abs(np.mod(z, .22) - .11) - .1, np.abs(y - surf) - .015, .004)
        return subtract(slab, planks, .004)

    def rails(x, y, z):
        t = np.clip(z / 7, -.5, .5)
        surf = arch(t)
        d = None
        for side in (-1, 1):
            top = smax(np.sqrt((x - side * .62) ** 2 + (y - surf - .75) ** 2) - .045, np.abs(z) - 3.6, .02)
            mid = smax(np.sqrt((x - side * .62) ** 2 + (y - surf - .4) ** 2) - .03, np.abs(z) - 3.6, .02)
            posts = smax(smax(np.abs(np.mod(z + .5, 1.0) - .5) - .055, np.abs(x - side * .62) - .055, .01),
                         np.maximum(surf - y - .05, y - surf - .8), .01)
            posts = smax(posts, np.abs(z) - 3.6, .01)
            f = union(top, mid, posts)
            d = f if d is None else np.minimum(d, f)
        return d

    def caps(x, y, z):
        d = None
        for side in (-1, 1):
            for end in (-1, 1):
                zz = end * 3.5
                c = ellipsoid(x, y, z, (side * .62, arch(end * .5) + .9, zz), (.09, .14, .09))
                d = c if d is None else np.minimum(d, c)
        return d
    lo, hi = (-.95, -.1, -3.9), (.95, 2.75, 3.9)
    return model([Piece(deck, lo, hi, .015, 900, wood((.48, .32, .20), 6)),
                  Piece(rails, lo, hi, .015, 1100, wood(VERMILION, 7)),
                  Piece(caps, lo, hi, .012, 120, solid(GOLD))], reach=1)


# =============================================================================================
# Harbor Lights
FACADES = [(.58, .26, .18), (.86, .80, .68), (.56, .57, .58), (.20, .46, .48), (.80, .58, .26)]
DARK_GLASS = (.10, .16, .22)
TAR = (.18, .19, .20)
CRANE_YELLOW = (.92, .66, .10)
CONTAINERS = [(.70, .16, .10), (.12, .36, .62), (.15, .48, .30), (.86, .52, .10), (.42, .44, .46), (.60, .20, .48)]


BUILDINGS = {}


def building(floors, wall, variant):
    """A 10 x 10 m city building scaled to each footprint: a rusticated ground floor,
    window rows with sills and lintels, a cornice and parapet, sometimes a water tank.
    The shape is baked once per storey count and variant, then tinted per facade colour."""
    if (floors, variant) not in BUILDINGS:
        BUILDINGS[floors, variant] = building_shape(floors, variant)
    points, faces, colours, walls = BUILDINGS[floors, variant]
    colours = np.where(walls[:, None], colours * np.array(wall, float), colours)
    points, faces, colours = split_sharp(points, faces, colours)
    return SimpleNamespace(points=points, faces=faces), colours


def building_shape(floors, variant):
    h = floors * 3.2
    rng = np.random.default_rng(700 + variant * 11 + floors)
    cells = []
    for f in range(floors):
        for face in range(4):
            for k in range(3):
                cells.append((f, face, k, rng.random() < .5))

    def window_field(lit):
        def f(x, y, z):
            d = None
            for (fl, face, k, on) in cells:
                if on != lit:
                    continue
                a = (k - 1) * 3.2
                cy = 1.8 + fl * 3.2
                if face < 2:
                    w = box(x, y, z, (a, cy, (1 if face == 0 else -1) * 4.98), (.65, .75, .05), .02)
                else:
                    w = box(x, y, z, ((1 if face == 2 else -1) * 4.98, cy, a), (.05, .75, .65), .02)
                d = w if d is None else np.minimum(d, w)
            return d if d is not None else box(x, y, z, (0, -50, 0), (.1, .1, .1))
        return f

    def walls(x, y, z):
        body = box(x, y, z, (0, h / 2, 0), (4.95, h / 2, 4.95), .04)
        cornice = smax(rect2(x, z, (0, 0), (5.15, 5.15), .05), np.abs(y - h + .25) - .18, .03)
        parapet = smax(smax(rect2(x, z, (0, 0), (5.05, 5.05), .04), -rect2(x, z, (0, 0), (4.8, 4.8), .02), .01),
                       np.abs(y - h - .3) - .3, .02)
        base = smax(rect2(x, z, (0, 0), (5.05, 5.05), .04), np.abs(y - .6) - .6, .03)
        courses = smax(np.abs(np.mod(y, .4) - .2) - .18, -(base + .03) * 1, .01)
        base = subtract(base, smax(courses, y - 1.2, .01), .01)
        d = union(body, cornice, parapet, base)
        sills = None
        for fl in range(floors):
            s = smax(np.abs(np.maximum(np.abs(x), np.abs(z)) - 5.0) - .06, np.abs(y - (1.8 + fl * 3.2) + .85) - .06, .01)
            sills = s if sills is None else np.minimum(sills, s)
        d = smin(d, sills, .01)
        if variant % 2 == 0:
            legs = None
            for px in (-.6, .6):
                for pz in (-.6, .6):
                    p = box(x, y, z, (px + 2, h + .9, pz - 1.5), (.06, .4, .06), .01)
                    legs = p if legs is None else np.minimum(legs, p)
            tank = cylinder_y(x, y, z, (2, h + 2.1, -1.5), .95, .85, .1)
            roof = smax(ellipsoid(x, y, z, (2, h + 2.9, -1.5), (1.0, .55, 1.0)), h + 2.9 - y, .02)
            d = union(d, legs, tank, roof)
        return d
    lo, hi = (-5.4, -.1, -5.4), (5.4, h + 3.6, 5.4)
    lit = sum(1 for c in cells if c[3])
    pieces = [Piece(walls, lo, hi, .05, 600, stone((1, 1, 1), None, .1, seed=variant)),
              Lit(window_field(True), lo, hi, .05, 10 * lit),
              Piece(window_field(False), lo, hi, .05, 10 * (len(cells) - lit), solid(DARK_GLASS))]
    baked = flora.bake(pieces, reach=2, rays=32)
    points, faces, colours, walls_mask, offset = [], [], [], [], 0
    for k, (piece, (p, n, f, c)) in enumerate(zip(pieces, baked)):
        if getattr(piece, 'glow', False):
            c = np.tile(np.array(GLOW, float), (len(p), 1))
        points.append(p)
        faces.append(f + offset)
        colours.append(c)
        walls_mask.append(np.full(len(p), k == 0))
        offset += len(p)
    return np.concatenate(points), np.concatenate(faces), np.concatenate(colours), np.concatenate(walls_mask)


def warehouse(wall):
    """A 22 x 12 m dock warehouse: ribbed walls, a corrugated gable roof, three roller doors with lamps."""
    def walls(x, y, z):
        body = box(x, y, z, (0, 3.5, 0), (11, 3.5, 6), .05)
        gable = smax(smax(y - 7 - 2 * (1 - np.abs(x) / 11), 7 - y - .1, .02), np.maximum(np.abs(x) - 11, np.abs(z) - 6), .02)
        d = np.minimum(body, gable)
        # Pilasters every 2.2 m along the long walls.
        pilasters = smax(smax(np.abs(np.mod(x + 1.1, 2.2) - 1.1) - .15, np.abs(np.abs(z) - 6) - .12, .01), np.abs(y - 3.5) - 3.5, .01)
        d = smin(d, smax(pilasters, np.abs(x) - 10.9, .01), .02)
        doors = None
        for k in (-6, 0, 6):
            dd = box(x, y, z, (k, 2.1, 6.0), (1.8, 2.1, .2), .03)
            doors = dd if doors is None else np.minimum(doors, dd)
        return subtract(d, doors, .02)

    def roof(x, y, z):
        return smax(polyline2(x, y, [(-11.6, 6.85), (0, 9.05), (11.6, 6.85)]) - .15, np.abs(z) - 6.5, .03)

    def doors(x, y, z):
        d = None
        for k in (-6, 0, 6):
            door = box(x, y, z, (k, 2.1, 5.9), (1.8, 2.1, .05), .02)
            frame = smax(np.abs(rect2(x, y, (k, 2.15), (1.9, 2.15), .03)) - .1, np.abs(z - 6.05) - .08, .01)
            d = union(*(t for t in (d, door, frame) if t is not None))
        return d

    def lamps(x, y, z):
        d = None
        for k in (-6, 0, 6):
            l = box(x, y, z, (k, 4.8, 6.15), (.25, .12, .1), .03)
            d = l if d is None else np.minimum(d, l)
        return d
    lo, hi = (-12, -.1, -7), (12, 9.6, 7)
    return model([Piece(walls, lo, hi, .07, 1000, stone(wall, None, .12, seed=21)),
                  Piece(roof, lo, hi, .07, 600, stone(TAR, None, .1, seed=22)),
                  Piece(doors, lo, hi, .03, 500, solid((.46, .48, .50))),
                  Lit(lamps, (-7, 4.5, 5.9), (7, 5.1, 6.4), .02, 60)], reach=3)


def dock_crane():
    """A rail-mounted portal crane: four braced legs, a portal girder, a cab and a long lattice boom
    towards +z with a lamp at its tip. Legs reach 3 m below the origin for uneven quays."""
    def legs(x, y, z):
        d = None
        for sx in (-1, 1):
            for sz in (-1, 1):
                l = box(x, y, z, (sx * 5, 6.5, sz * 4), (.3, 9.5, .3), .03)
                d = l if d is None else np.minimum(d, l)
        braces = None
        for sx in (-1, 1):
            b = smax(polyline2(z, y, [(-4, 1), (4, 9), (-4, 15)]) - .12, np.abs(x - sx * 5) - .15, .01)
            braces = b if braces is None else np.minimum(braces, b)
        for sz in (-1, 1):
            b = smax(polyline2(x, y, [(-5, 2), (5, 12)]) - .12, np.abs(z - sz * 4) - .15, .01)
            braces = np.minimum(braces, b)
        portal = box(x, y, z, (0, 16.5, 0), (5.6, .6, 4.6), .05)
        return union(d, braces, portal)

    def boom(x, y, z):
        chords = None
        for sx in (-1, 1):
            for sy in (0, 1):
                c = box(x, y, z, (sx * .9, 17.6 + sy * 1.0, 6), (.1, .1, 15), .02)
                chords = c if chords is None else np.minimum(chords, c)
        lattice = smax(smax(np.abs(np.mod(z + y * .9, 2.0) - 1.0) - .06, np.abs(np.abs(x) - .9) - .08, .01),
                       np.maximum(np.abs(y - 18.1) - .5, np.abs(z - 6) - 15), .01)
        tower = box(x, y, z, (0, 20.5, -1.5), (.4, 3.5, .4), .03)
        ties = None
        for end in (16, -8):
            t = smax(polyline2(z, y, [(-1.5, 24), (end, 18.6)]) - .06, np.abs(x) - .06, .01)
            ties = t if ties is None else np.minimum(ties, t)
        counter = box(x, y, z, (0, 18, -7.5), (1.3, .9, 1.2), .05)
        return union(chords, lattice, tower, ties, counter)

    def cab(x, y, z):
        return box(x, y, z, (0, 15.2, 2.2), (1.2, 1.0, 1.2), .08)

    def cab_glass(x, y, z):
        return box(x, y, z, (0, 15.35, 2.25), (1.05, .5, 1.25), .03)

    def lamp(x, y, z):
        return box(x, y, z, (0, 17.35, 20.5), (.4, .18, .4), .05)
    lo, hi = (-6, -3.2, -9), (6, 24.6, 21.5)
    return model([Piece(legs, lo, hi, .07, 1500, stone(CRANE_YELLOW, None, .08, seed=31)),
                  Piece(boom, lo, hi, .06, 2200, stone(CRANE_YELLOW, None, .08, seed=32)),
                  Piece(cab, (-1.5, 13.8, .8), (1.5, 16.5, 3.7), .04, 300, solid((.92, .91, .86))),
                  Piece(cab_glass, (-1.5, 14.5, .8), (1.5, 16.2, 3.7), .03, 120, solid(DARK_GLASS)),
                  Lit(lamp, (-.7, 16.9, 19.8), (.7, 17.8, 21.2), .03, 40)], reach=4)


def container(colour):
    """A corrugated 20-foot container, 6.1 x 2.5 x 2.45 m along x, base at y = 0."""
    def shell(x, y, z):
        d = box(x, y, z, (0, 1.25, 0), (3.05, 1.25, 1.22), .04)
        ribs = smax(np.abs(np.mod(x, .28) - .14) - .1, np.abs(np.abs(z) - 1.22) - .03, .01)
        ribs = smax(ribs, np.maximum(np.abs(x) - 2.8, np.abs(y - 1.25) - 1.1), .01)
        d = subtract(d, ribs, .01)
        frame = smax(np.abs(rect2(z, y, (0, 1.25), (1.22, 1.25), .03)) - .07, np.abs(np.abs(x) - 3.0) - .06, .01)
        bars = smax(smax(np.abs(np.abs(z) - .45) - .03, np.abs(x - 3.07) - .03, .01), np.abs(y - 1.25) - 1.1, .01)
        return union(d, frame, bars, k=.01)
    return model([Piece(shell, (-3.2, -.1, -1.4), (3.2, 2.6, 1.4), .025, 220, stone(colour, None, .1, seed=41))], reach=1.5)


def lighthouse():
    """A tapering white tower with red bands, a railed gallery, a glazed lit lantern room and a red cap."""
    def tower(x, y, z):
        r = np.sqrt(x * x + z * z)
        body = smax(r - (1.6 - .5 * np.clip(y / 14, 0, 1)), np.abs(y - 7) - 7, .05)
        door = box(x, y, z, (0, 1.1, 1.6), (.45, 1.0, .5), .04)
        return subtract(body, door, .03)

    def bands(x, y, z):
        r = np.sqrt(x * x + z * z)
        d = None
        for k in range(3):
            y0 = 3.6 + k * 4
            b = smax(r - (1.6 - .5 * y0 / 14) - .02, np.abs(y - y0) - .65, .02)
            d = b if d is None else np.minimum(d, b)
        return d

    def gallery(x, y, z):
        r = np.sqrt(x * x + z * z)
        deck = smax(r - 1.75, np.abs(y - 14.2) - .18, .04)
        rail = smax(np.abs(r - 1.65) - .03, np.abs(y - 14.95) - .03, .01)
        posts = smax(smax(np.abs(np.mod(np.arctan2(z, x) * 1.65, .5) - .25) - .025, np.abs(r - 1.65) - .025, .005),
                     np.abs(y - 14.65) - .3, .01)
        frame = smax(np.abs(r - .95) - .05, np.abs(y - 15.25) - .9, .02)
        mullions = smax(smax(np.abs(np.mod(np.arctan2(z, x) * .95, .45) - .225) - .03, np.abs(r - .95) - .05, .005),
                        np.abs(y - 15.25) - .9, .01)
        frame = smax(frame, -smax(np.abs(r - .95) - .1, np.abs(y - 15.25) - .75, .01), .01)
        return union(deck, rail, posts, frame, mullions)

    def cap(x, y, z):
        r = np.sqrt(x * x + z * z)
        dome = smax(r - 1.15 * (1 - np.clip((y - 16.2) / 1.1, 0, 1)) ** .8, np.abs(y - 16.75) - .55, .03)
        vent = smin(cylinder_y(x, y, z, (0, 17.4, 0), .12, .15, .03), ellipsoid(x, y, z, (0, 17.62, 0), (.14, .14, .14)), .03)
        return np.minimum(dome, vent)

    def light(x, y, z):
        return cylinder_y(x, y, z, (0, 15.25, 0), .9, .75, .05)
    lo, hi = (-2, -.1, -2), (2, 17.9, 2.2)
    return model([Piece(tower, lo, hi, .04, 800, stone((.93, .92, .88), None, .06, seed=51)),
                  Piece(bands, lo, hi, .04, 500, solid((.80, .10, .08))),
                  Piece(gallery, (-2, 13.9, -2), (2, 16.3, 2), .02, 900, solid((.10, .11, .12))),
                  Piece(cap, (-1.4, 16.0, -1.4), (1.4, 17.9, 1.4), .03, 500, solid((.80, .10, .08))),
                  Lit(light, (-1, 14.4, -1), (1, 16.1, 1), .03, 200)], reach=2.5)


def ship():
    """A small container feeder, 34 m along z: a flared hull with a red boot top, a deck of
    containers, the bridge aft with lit windows and a funnel. Waterline at y = 0."""
    def hull(x, y, z):
        # Fine at the bow (+z), full aft; the sheer rises towards the bow.
        t = np.clip((z - 8) / 9, 0, 1)
        half = 3.5 * (1 - t ** 2 * .92) * (1 - .25 * np.clip(-y / 3, 0, 1))
        top = 3.5 + .8 * t ** 2
        d = smax(np.abs(x) - half, np.maximum(y - top, -3 - y), .25)
        return smax(d, np.maximum(z - 17, -17 - z), .5)

    def boot(x, y, z):
        return smax(hull(x, y, z) - .02, np.abs(y - .1) - .45, .02)

    def bridge(x, y, z):
        house = box(x, y, z, (0, 6.5, -13), (3.0, 3.0, 2.5), .06)
        wing = box(x, y, z, (0, 9.1, -12.2), (3.6, .12, .6), .03)
        funnel = smax(box(x, y, z, (0, 11, -15.5), (.9, 2.2, .9), .2), (y - 12.9) + .3 * (z + 15.5), .03)
        return union(house, wing, funnel)

    def stack(lit=False):
        def f(x, y, z):
            d = None
            for k in range(4):
                for level in range(2):
                    c = box(x, y, z, (0, 4.9 + level * 2.6, k * 6.3 - 6), (3.0, 1.25, 3.05), .04)
                    if lit:
                        continue
                    d = c if d is None else np.minimum(d, c)
            if lit:
                d = None
                for k in range(3):
                    w = box(x, y, z, ((k - 1) * 1.8, 8.4, -10.47), (.6, .4, .06), .02)
                    d = w if d is None else np.minimum(d, w)
            return d
        return f

    def containers(x, y, z):
        d = None
        for k in range(4):
            for level in range(2):
                c = box(x, y, z, (0, 4.9 + level * 2.6, k * 6.3 - 6), (3.0, 1.25, 3.05), .04)
                ribs = smax(np.abs(np.mod(z, .28) - .14) - .1, np.abs(np.abs(x) - 3.0) - .03, .01)
                c = subtract(c, smax(ribs, np.abs(y - 4.9 - level * 2.6) - 1.1, .01), .01)
                d = c if d is None else np.minimum(d, c)
        return d

    def container_colour(p, n):
        k = np.clip(np.floor((p[:, 2] + 9.15) / 6.3), 0, 3).astype(int)
        level = (p[:, 1] > 6.2).astype(int)
        palette = np.array(CONTAINERS)
        return palette[(k + level) % 6] * (.9 + .1 * noise(p[:, 0], p[:, 1], p[:, 2], 3))[:, None]
    lo, hi = (-4.2, -3, -18), (4.2, 13.5, 18)
    return model([Piece(hull, lo, hi, .07, 1200, stone((.14, .18, .24), None, .06, seed=61)),
                  Piece(boot, lo, hi, .07, 600, solid((.62, .16, .10))),
                  Piece(bridge, lo, hi, .05, 700, solid((.92, .91, .86))),
                  Lit(stack(True), (-3, 7.8, -11), (3, 9, -10), .03, 80),
                  Piece(containers, lo, hi, .06, 1100, container_colour)], reach=3)


def street_lamp():
    """A harbour street lamp: a fluted post, a curved arm reaching over +z, a lit lantern head."""
    def post(x, y, z):
        p = cylinder_y(x, y, z, (0, 2.5, 0), .08, 2.5, .02)
        foot = cylinder_y(x, y, z, (0, .3, 0), .16, .3, .04)
        arm = smax(np.abs(np.sqrt(np.maximum(0, (z - 0) ** 2 + (y - 4.6) ** 2)) - .6) - .04,
                   np.maximum(np.maximum(-z, 4.6 - y), np.abs(x) - .04), .01)
        hood = smax(ellipsoid(x, y, z, (0, 5.2, .75), (.3, .16, .26)), 5.12 - y, .02)
        return union(p, foot, arm, hood, k=.02)

    def light(x, y, z):
        return smax(ellipsoid(x, y, z, (0, 5.12, .75), (.22, .14, .19)), y - 5.13, .01)
    return model([Piece(post, (-.4, -.1, -.4), (.4, 5.5, 1.2), .015, 120, solid((.10, .11, .12))),
                  Lit(light, (-.3, 4.9, .5), (.3, 5.2, 1.0), .015, 24)], reach=.8)
