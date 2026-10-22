"""Dry-country and city species for the flora library: the canyon, the volcano and the docks.

Saguaros, Joshua trees and ironwoods for Canyon Dash; charred snags and
blossoming ohia for Volcano Peak; lindens in concrete planters for Harbor
Lights; and the small shrubs, tumbleweeds and ferns of their patches. Built
like tools/flora.py: unit-scale signed distance pieces (and leaf sheets where
the voxel grid cannot hold the blades), instanced with flora.plant().
"""
import math

import numpy as np

import flora
from flora import Piece, Sheet, foliage, bark, mix, noise, fbm, limb, crown_field, bounds
from sdf import smin, smax, union, box, ellipsoid, capsule


# --- Canyon Dash ------------------------------------------------------------------------------
def ribbed(x, y, z, cx, cz, r, ribs=12, depth=.07):
    """Distance to a vertical column of radius r about (cx, cz), fluted with ribs."""
    a = np.arctan2(z - cz, x - cx)
    return np.sqrt((x - cx) ** 2 + (z - cz) ** 2) - r * (1 + depth * np.cos(ribs * a))


def saguaro(variant, arms=2, height=4.4):
    """A fluted column with upturned arms, a domed crown and, on some, cream buds."""
    rng = np.random.default_rng(500 + variant + 10 * arms)
    specs = []
    for k in range(arms):
        a = rng.uniform(0, math.tau) if k == 0 else specs[0][0] + math.pi + rng.uniform(-.7, .7)
        specs.append((a, height * rng.uniform(.30, .48) + .55 * k, rng.uniform(.75, 1.0), rng.uniform(1.2, 1.9)))

    def field(x, y, z):
        r = .38 - .06 * np.clip(y / height, 0, 1)
        trunk = smax(ribbed(x, y, z, 0, 0, r), y - height, .32)
        trunk = smax(trunk, -y - .3, .02)
        d = trunk
        for a, base, out, rise in specs:
            ca, sa = math.cos(a), math.sin(a)
            ex, ez = ca * out, sa * out
            # Out from the trunk, a rounded elbow, then up as its own fluted column.
            reach = capsule(x, y, z, (ca * .2, base, sa * .2), (ex * .8, base + .12, ez * .8), .21)
            column = smax(ribbed(x, y, z, ex, ez, .21 - .03 * np.clip((y - base) / rise, 0, 1), 10, .08),
                          np.maximum(base + .05 - y, y - (base + rise)), .2)
            d = smin(d, smin(reach, column, .22), .12)
        return d

    def colour(p, n):
        a = np.arctan2(p[:, 2], p[:, 0])
        ridge = .5 + .5 * np.cos(12 * a)
        c = mix((.12, .27, .13), (.36, .52, .27), .35 + .45 * ridge + .2 * noise(p[:, 0] * 3, p[:, 1] * 1.4, p[:, 2] * 3, variant))
        # Older skin at the foot turns woody and grey.
        return mix((.40, .36, .27), c, np.clip(p[:, 1] / .6, 0, 1))

    reach = max(s[2] for s in specs) + .5
    pieces = [Piece(field, (-reach, -.3, -reach), (reach, height + .5, reach), .022, 540, colour)]
    if variant % 2 == 0:
        buds = [(math.cos(k * 2.1) * .17, height - .02 + .03 * (k % 2), math.sin(k * 2.1) * .17) for k in range(5)]
        pieces.append(Piece(lambda x, y, z: union(*[ellipsoid(x, y, z, b, (.07, .09, .07)) for b in buds], k=.02),
                            (-.4, height - .3, -.4), (.4, height + .3, .4), .012, 120,
                            lambda p, n: mix((.93, .88, .70), (.98, .95, .82), n[:, 1] * .5 + .5)))
    return pieces


def spikes(centre, count, length, rng, up=.55, down=False):
    """A tuft of stiff, pointed blades radiating from centre, as triangles."""
    points, faces = [], []
    for k in range(count):
        # Even directions on a cap, upward unless this is a hanging skirt.
        t = (k + .5) / count
        elev = math.asin(up + (1 - up) * t) if not down else -math.asin(.15 + .6 * t)
        az = k * 2.39996 + rng.uniform(-.2, .2)
        d = np.array([math.cos(elev) * math.cos(az), math.sin(elev), math.cos(elev) * math.sin(az)])
        side = np.cross(d, [0, 1, 0])
        side = side / max(np.linalg.norm(side), 1e-6) * .035
        L = length * rng.uniform(.75, 1.1)
        base = len(points)
        c = np.array(centre)
        points += [tuple(c - side), tuple(c + side), tuple(c + d * L)]
        faces.append((base, base + 1, base + 2))
    return points, faces


def joshua(variant, height=2.7):
    """A shaggy forked trunk whose branches end in spiky green tufts over skirts of dead leaves."""
    rng = np.random.default_rng(600 + variant)
    fork = (.25, height, .05)
    ends = []
    for k in range(3 + variant % 2):
        a = k * math.tau / (3 + variant % 2) + rng.uniform(-.4, .4)
        mid = (fork[0] + math.cos(a) * .6, fork[1] + .55, fork[2] + math.sin(a) * .6)
        end = (mid[0] + math.cos(a) * .55, mid[1] + .6 + rng.uniform(-.1, .2), mid[2] + math.sin(a) * .55)
        ends.append((mid, end))

    def field(x, y, z):
        d = limb(x, y, z, (0, -.3, 0), fork, .27, .19)
        for mid, end in ends:
            d = smin(d, limb(x, y, z, fork, mid, .16, .12), .12)
            d = smin(d, limb(x, y, z, mid, end, .12, .10), .08)
        d = smin(d, smax(np.sqrt(x * x + z * z) - .45 * np.clip(1 - y / .45, 0, 1), -y - .2, .05), .2)
        return d + .05 * fbm(x * 6, y * 3, z * 6, variant)

    green_p, green_f, dead_p, dead_f = [], [], [], []
    for mid, end in ends:
        p, f = spikes((end[0], end[1] + .08, end[2]), 44, .82, rng, -.25)
        green_f += [(a + len(green_p), b + len(green_p), c + len(green_p)) for a, b, c in f]
        green_p += p
        p, f = spikes((end[0], end[1] - .05, end[2]), 20, .7, rng, down=True)
        dead_f += [(a + len(dead_p), b + len(dead_p), c + len(dead_p)) for a, b, c in f]
        dead_p += p
    leaves = lambda p, n: mix((.28, .38, .16), (.58, .64, .30), .3 + .5 * np.clip((p[:, 1] - height) / 1.8, 0, 1)
                              + .2 * noise(p[:, 0] * 4, p[:, 1] * 4, p[:, 2] * 4, variant))
    skirt = lambda p, n: mix((.34, .27, .18), (.55, .46, .32), .5 + .5 * noise(p[:, 0] * 6, p[:, 1] * 6, p[:, 2] * 6, variant))
    reach = 1.6
    return [Piece(field, (-reach, -.3, -reach), (reach, height + 1.6, reach), .025, 300,
                  bark((.30, .24, .18), (.22, .18, .14), .45, variant)),
            Sheet(green_p, green_f, leaves), Sheet(dead_p, dead_f, skirt)]


def anisotropic_crown(blobs, squash, k=.4, bumps=.15, freq=1.8, seed=0):
    """Clustered foliage flattened by squash in y: an umbrella canopy."""
    def field(x, y, z):
        d = None
        for cx, cy, cz, r in blobs:
            s = (np.sqrt((x - cx) ** 2 + ((y - cy) / squash) ** 2 + (z - cz) ** 2) - r) * squash
            d = s if d is None else smin(d, s, k)
        return d + bumps * fbm(x * freq, y * freq * 1.5, z * freq, seed)
    return field


def ironwood(variant):
    """A gnarled desert ironwood: a low leaning trunk under a flat, dusty-green umbrella."""
    rng = np.random.default_rng(700 + variant)
    lean = (rng.uniform(-.3, .3), rng.uniform(-.3, .3))
    fork = (lean[0], 1.5, lean[1])
    tips = [(fork[0] + math.cos(a) * 1.2, 2.3 + rng.uniform(-.1, .2), fork[2] + math.sin(a) * 1.2)
            for a in rng.uniform(0, math.tau, 3)]
    blobs = [(fork[0], 2.55, fork[2], 1.0)] + [(t[0] * 1.15, 2.5 + rng.uniform(-.1, .15), t[2] * 1.15, rng.uniform(.75, .95))
                                                for t in tips]
    blobs += [(math.cos(a) * 1.5, 2.45, math.sin(a) * 1.4, .7) for a in rng.uniform(0, math.tau, 3)]

    def trunk(x, y, z):
        d = limb(x, y, z, (0, -.3, 0), fork, .2, .14)
        for t in tips:
            d = smin(d, limb(x, y, z, fork, t, .11, .05), .1)
        d = smin(d, smax(np.sqrt(x * x + z * z) - .34 * np.clip(1 - y / .4, 0, 1), -y - .2, .05), .18)
        return d + .02 * fbm(x * 6, y * 6, z * 6, variant)
    lo, hi = bounds(blobs, .5)
    return [Piece(trunk, (-1.8, -.3, -1.8), (1.8, 2.8, 1.8), .025, 200, bark((.33, .27, .22), (.26, .22, .18), .25, variant)),
            Piece(anisotropic_crown(blobs, .5, seed=variant), lo, hi, .05, 380,
                  foliage((.20, .26, .13), (.55, .58, .33), seed=variant, sun=.6))]


def sagebrush(variant):
    """A low, two-lobed desert scrub of dusty sage leaves."""
    rng = np.random.default_rng(800 + variant)
    blobs = [(0, .32, 0, .62), (.7, .22, .3, .45)] + [(rng.uniform(-.4, .9), .2 + rng.uniform(0, .25), rng.uniform(-.4, .6),
                                                      rng.uniform(.28, .4)) for _ in range(4)]
    field = anisotropic_crown(blobs, .65, k=.25, bumps=.08, freq=4, seed=variant)
    lo, hi = bounds(blobs, .3)
    lo[1] = -.1
    return [Piece(lambda x, y, z: smax(field(x, y, z), -y - .05, .05), lo, hi, .03, 220,
                  foliage((.20, .25, .15), (.56, .60, .42), seed=variant, sun=.6))]


def tumbleweed(variant):
    """A dry, open tangle: a ball of the curves where two lattices cross."""
    o = variant * 1.7

    def field(x, y, z):
        f = 15.
        g1 = np.sin(x * f + o) * np.cos(y * f) + np.sin(y * f) * np.cos(z * f + o) + np.sin(z * f) * np.cos(x * f)
        g2 = np.sin(x * f * .8) * np.cos(z * f * .8 + 1) + np.sin(z * f * .8) * np.cos(y * f * .8 + o) + np.sin(y * f * .8 + 2) * np.cos(x * f * .8)
        twigs = (np.sqrt(g1 * g1 + g2 * g2) - .32) / f * .9
        ball = np.sqrt(x * x + (y - .02) ** 2 / .8 + z * z) - .5
        return smax(twigs, ball, .02)
    return [Piece(field, (-.6, -.55, -.6), (.6, .55, .6), .012, 250,
                  lambda p, n: mix((.48, .38, .22), (.78, .66, .44), .5 + .5 * n[:, 1] + .2 * noise(p[:, 0] * 8, p[:, 1] * 8, p[:, 2] * 8)))]


# --- Volcano Peak ------------------------------------------------------------------------------
def charred(variant):
    """A dead snag left by an old flow: split black wood, broken stubs and grey ash on the tops."""
    rng = np.random.default_rng(900 + variant)
    top = (.3, 3.4, 0)
    branches = [((0, 2.2, 0), (math.cos(a) * 1.2, 3.0, math.sin(a) * 1.2), .08, .03)
                for a in (rng.uniform(0, math.tau), rng.uniform(0, math.tau))]
    stubs = [((0, h, 0), (math.cos(a) * .35, h + .12, math.sin(a) * .35), .06, .04)
             for a, h in zip(rng.uniform(0, math.tau, 2), (1.3, 2.7))]

    def field(x, y, z):
        d = limb(x, y, z, (0, -.3, 0), top, .21, .07)
        for a, b, ra, rb in branches + stubs:
            d = smin(d, limb(x, y, z, a, b, ra, rb), .07)
        a = np.arctan2(z, x)
        d = smin(d, smax(np.sqrt(x * x + z * z) - .36 * (1 + .3 * np.cos(4 * a)) * np.clip(1 - y / .5, 0, 1), -y - .2, .05), .15)
        # Deep vertical splits in the burnt wood.
        return d + .025 * np.abs(np.sin(a * 7 + y * .8)) + .015 * fbm(x * 8, y * 3, z * 8, variant)

    def colour(p, n):
        c = mix((.05, .045, .04), (.16, .14, .12), .5 + .5 * noise(p[:, 0] * 6, p[:, 1] * 2, p[:, 2] * 6, variant))
        return np.where((n[:, 1] > .35)[:, None], mix(c, (.42, .40, .38), np.full(len(p), .7)), c)
    return [Piece(field, (-1.5, -.3, -1.5), (1.6, 3.7, 1.5), .022, 260, colour)]


def ohia(variant):
    """A tough ohia lehua: a grey-green crown of small leaves topped with red pompom blossoms."""
    rng = np.random.default_rng(1000 + variant)
    blobs = [(0, 3.1, 0, 1.45), (.7, 2.75, .3, 1.0)]
    blobs += [(math.cos(a) * 1.1, rng.uniform(2.6, 3.5), math.sin(a) * 1.1, rng.uniform(.7, .95))
              for a in np.linspace(0, math.tau, 5, endpoint=False) + rng.uniform(0, 1)]
    branches = [((0, 1.7, 0), (math.cos(a) * .7, 2.7, math.sin(a) * .7), .11, .05) for a in rng.uniform(0, math.tau, 3)]
    crown = crown_field(blobs, k=.4, bumps=.12, freq=1.8, seed=variant)
    # Blossoms sit on the upper surface of the crown.
    flowers = []
    for k in range(13):
        cx, cy, cz, r = blobs[rng.integers(len(blobs))]
        d = rng.normal(size=3)
        d[1] = abs(d[1]) + .5
        d /= np.linalg.norm(d)
        size = rng.uniform(.2, .28)
        flowers.append((cx + d[0] * (r + .12), cy + d[1] * (r + .12), cz + d[2] * (r + .12), size))

    def blossoms(x, y, z):
        d = union(*[np.sqrt((x - fx) ** 2 + (y - fy) ** 2 + (z - fz) ** 2) - r for fx, fy, fz, r in flowers], k=.02)
        return d + .04 * noise(x * 14, y * 14, z * 14, variant)
    lo, hi = bounds(blobs)
    flo, fhi = bounds([(f[0], f[1], f[2], f[3]) for f in flowers], .1)
    return [Piece(flora.trunk_field(2.0, .2, .1, branches, flare=1.6), (-1.2, -.3, -1.2), (1.2, 2.9, 1.2), .03, 160,
                  bark((.32, .26, .21), (.24, .20, .17))),
            Piece(crown, lo, hi, .055, 380, foliage((.12, .20, .12), (.40, .50, .32), seed=variant, sun=.6)),
            Piece(blossoms, flo, fhi, .02, 200,
                  lambda p, n: mix((.62, .05, .04), (.98, .22, .12), .5 + .5 * n[:, 1] + .2 * noise(p[:, 0] * 9, p[:, 1] * 9, p[:, 2] * 9)))]


def fern_clump(variant):
    """A ground fern: a rosette of small pinnate fronds."""
    rng = np.random.default_rng(1100 + variant)
    points, faces = [], []
    for k in range(7):
        a = k * math.tau / 7 + rng.uniform(-.25, .25)
        p, f = flora.frond_sheet((0, .04, 0), a, rng.uniform(.7, .95), rng.uniform(.35, .55), rng, segments=6, width=.13)
        faces += [(i + len(points), j + len(points), l + len(points)) for i, j, l in f]
        points += p
    return [Sheet(points, faces, lambda p, n: mix((.10, .26, .10), (.34, .56, .22), .3 + .7 * np.clip(p[:, 1] * 2.5, 0, 1)
                                                  + .15 * noise(p[:, 0] * 5, p[:, 1] * 5, p[:, 2] * 5, variant)))]


# --- Harbor Lights -----------------------------------------------------------------------------
def planter_field(half, height=.4):
    """A concrete planter box with a rim, its soil sunk a little below it."""
    def field(x, y, z):
        outer = box(x, y, z, (0, height / 2, 0), (half, height / 2, half), .03)
        return smax(outer, -box(x, y, z, (0, height, 0), (half - .08, .05, half - .08), .02), .01)
    return field


def planter_colour(half, height=.4):
    def colour(p, n):
        concrete = mix((.50, .49, .47), (.66, .65, .62), .5 + .5 * noise(p[:, 0] * 5, p[:, 1] * 5, p[:, 2] * 5))
        soil = (np.abs(p[:, 0]) < half - .07) & (np.abs(p[:, 2]) < half - .07) & (p[:, 1] < height - .02)
        return np.where(soil[:, None], np.array((.20, .15, .11)), concrete)
    return colour


def linden(variant):
    """A clipped street linden in a square concrete planter."""
    rng = np.random.default_rng(1200 + variant)
    blobs = [(0, 4.1, 0, 1.55)]
    for k in range(8):
        a = k * math.tau / 8 + rng.uniform(-.25, .25)
        h = rng.uniform(-.8, .9)
        r = 1.35 * math.sqrt(max(0, 1 - (h / 1.7) ** 2)) + .1
        blobs.append((math.cos(a) * r, 4.1 + h, math.sin(a) * r, rng.uniform(.75, 1.0)))
    blobs.append((0, 5.6, 0, .85))
    branches = [((0, 2.6, 0), (math.cos(a) * .8, 3.7, math.sin(a) * .8), .09, .05) for a in rng.uniform(0, math.tau, 3)]
    lo, hi = bounds(blobs)
    return [Piece(planter_field(.7), (-.8, -.1, -.8), (.8, .5, .8), .02, 120, planter_colour(.7)),
            Piece(flora.trunk_field(3.0, .15, .10, branches, flare=1.3), (-1.1, .2, -1.1), (1.1, 3.9, 1.1), .025, 140,
                  bark((.30, .25, .21))),
            Piece(crown_field(blobs, k=.4, bumps=.16, freq=1.7, seed=variant), lo, hi, .06, 420,
                  foliage((.10, .22, .08), (.40, .58, .22), seed=variant))]


def planter_shrub(variant):
    """A clipped box ball in a low concrete planter."""
    rng = np.random.default_rng(1300 + variant)
    blobs = [(0, .78, 0, .62)] + [(math.cos(a) * .35, .75 + rng.uniform(-.1, .25), math.sin(a) * .35, .4)
                                  for a in rng.uniform(0, math.tau, 5)]
    lo, hi = bounds(blobs, .2)
    return [Piece(planter_field(.8), (-.9, -.1, -.9), (.9, .5, .9), .02, 110, planter_colour(.8)),
            Piece(crown_field(blobs, k=.25, bumps=.06, freq=5, seed=variant), lo, hi, .03, 220,
                  foliage((.07, .19, .07), (.30, .48, .17), seed=variant))]


# --- Clean meshes --------------------------------------------------------------------------------
def _without_folds(mesh_of):
    """Drops faces around vertices whose face normals cancel out.

    Decimation can fold a sliver back over its neighbour; the shared vertex then
    has no direction at all, and the renderer's normalize() turns it into NaN.
    """
    def mesh(detail=1.):
        m = mesh_of(detail)
        for _ in range(4):
            p, f = m.points, m.faces
            fn = np.cross(p[f[:, 1]] - p[f[:, 0]], p[f[:, 2]] - p[f[:, 0]])
            area = np.linalg.norm(fn, axis=1)
            unit = fn / np.maximum(area[:, None], 1e-30)
            total, size, units, count = (np.zeros_like(p), np.zeros(len(p)), np.zeros_like(p), np.zeros(len(p)))
            for k in range(3):
                np.add.at(total, f[:, k], fn)
                np.add.at(size, f[:, k], area)
                np.add.at(units, f[:, k], unit)
                np.add.at(count, f[:, k], 1)
            weak = ((np.linalg.norm(total, axis=1) < .05 * size) | (np.linalg.norm(units, axis=1) < .2 * count))
            bad = weak[f].any(axis=1) | (area < 1e-10)
            if not bad.any():
                break
            m = flora.Mesh(p, m.normals, f[~bad], m.codes[~bad], m.colors[~bad])
        return m
    return mesh


def clean(build):
    """A species builder whose pieces never carry folded, directionless vertices."""
    def wrapped(variant, *args, **kwargs):
        pieces = build(variant, *args, **kwargs)
        for piece in pieces:
            piece.part.mesh = _without_folds(piece.part.mesh)
        return pieces
    return wrapped


palm = clean(flora.palm)


def rock(name, colour, moss=None):
    key, build = flora.rock(name, colour, moss)
    return key + ('clean',), clean(build)


saguaro, joshua, ironwood, sagebrush, tumbleweed = map(clean, (saguaro, joshua, ironwood, sagebrush, tumbleweed))
charred, ohia, fern_clump, linden, planter_shrub = map(clean, (charred, ohia, fern_clump, linden, planter_shrub))
