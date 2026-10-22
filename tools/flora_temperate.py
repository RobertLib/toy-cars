"""Temperate species for Maple Woods and Highland Loch, built on tools/flora.py.

Trees stand about 7 m tall at scale 1 (the height the wind shader bends);
undergrowth is modelled at its real size. Every builder takes a variant
number and returns flora pieces, so plant() can cache three of each.
"""
import math

import numpy as np

import flora
import sdf
from sdf import smin, smax, ellipsoid
from flora import Piece, Sheet, crown_field, trunk_field, limb, fbm, noise, mix, foliage, bark, hash01

AUTUMN_LEAVES = [(.80, .22, .07), (.92, .45, .09), (.95, .70, .16), (.68, .16, .10), (.55, .52, .17)]


# --- Trees -----------------------------------------------------------------------------------
def maple(variant, colour=AUTUMN_LEAVES[0]):
    """A sugar maple in autumn: a forked trunk under a dense, rounded crown."""
    rng = np.random.default_rng(500 + variant)
    blobs = [(0, 4.4, 0, 1.55)]
    for k in range(10):
        a = k * 2.4 + rng.uniform(-.3, .3)
        h = rng.uniform(0, 1)
        rad = 1.6 - .7 * h
        blobs.append((math.cos(a) * rad, 3.7 + 2.4 * h, math.sin(a) * rad, rng.uniform(.8, 1.15) - .2 * h))
    forks = rng.uniform(0, math.tau, 3)
    branches = [((0, 1.9, 0), (math.cos(a) * 1.2, 3.8 + .25 * i, math.sin(a) * 1.2), .15, .06)
                for i, a in enumerate(forks)]
    base = np.array(colour)
    dark = base * np.array([.42, .36, .36])
    light = mix(base, (1., .86, .42), np.array([.32]))[0]
    lo, hi = flora.bounds(blobs)
    return [Piece(trunk_field(3.4, .28, .12, branches, flare=1.6), (-1.6, -.3, -1.6), (1.6, 4.3, 1.6), .035, 120,
                  bark((.27, .20, .15), (.18, .15, .12))),
            Piece(crown_field(blobs, k=.33, bumps=.2, freq=1.7, seed=variant), lo, hi, .065, 380,
                  foliage(dark, light, seed=variant + 11, sun=.6))]


def birch_bark(p, n):
    """Chalk-white bark with black lenticel bands and a dark, fissured foot."""
    c = np.array((.90, .88, .82)) * (.9 + .1 * noise(p[:, 0] * 9, p[:, 1] * 3, p[:, 2] * 9, 4))[:, None]
    marks = noise(p[:, 0] * 5, p[:, 1] * 11, p[:, 2] * 5, 7) > .45
    c = np.where(marks[:, None], np.array((.13, .12, .12)), c)
    return mix((.20, .18, .16), c, np.clip((p[:, 1] - .15) / .6, 0, 1))


def birch(variant, leaves=((.62, .37, .05), (1.0, .80, .22))):
    """A slender birch: a white stem (two in one variant) under a narrow, airy crown."""
    rng = np.random.default_rng(600 + variant)
    lean = rng.uniform(-.25, .25, 2)
    stems = [((0, -.3, 0), (lean[0], 5.2, lean[1]), .15, .06)]
    if variant == 1:
        stems.append(((.12, .2, .05), (.75, 4.6, .3), .1, .045))
    blobs = [(lean[0] * .8, 4.7, lean[1] * .8, .85)]
    for k in range(8):
        a = k * 2.3 + rng.uniform(-.4, .4)
        h = k / 7
        rad = .55 * math.sin(math.pi * (.2 + .8 * h)) + .15
        blobs.append((lean[0] * (.6 + .4 * h) + math.cos(a) * rad, 3.2 + 3.3 * h,
                      lean[1] * (.6 + .4 * h) + math.sin(a) * rad, rng.uniform(.75, .95) - .25 * h))
    branches = [((lean[0] * .5, 3.0 + .5 * i, lean[1] * .5),
                 (math.cos(a) * .9, 3.8 + .6 * i, math.sin(a) * .9), .05, .025)
                for i, a in enumerate(rng.uniform(0, math.tau, 3))]

    def trunk(x, y, z):
        d = None
        for a, b, ra, rb in stems:
            s = limb(x, y, z, a, b, ra, rb)
            d = s if d is None else smin(d, s, .08)
        foot = np.sqrt(x * x + z * z) - .26 * np.clip(1 - y / .5, 0, 1)
        d = smin(d, smax(foot, -y - .2, .04), .15)
        for a, b, ra, rb in branches:
            d = smin(d, limb(x, y, z, a, b, ra, rb), .06)
        return d
    lo, hi = flora.bounds(blobs, .5)
    return [Piece(trunk, (-1.2, -.3, -1.2), (1.4, 5.4, 1.4), .025, 150, birch_bark),
            Piece(crown_field(blobs, k=.38, bumps=.22, freq=2.2, seed=variant + 20), lo, hi, .055, 340,
                  foliage(leaves[0], leaves[1], seed=variant + 21, sun=.6))]


def spruce(variant):
    """A dark, narrow spruce from the fir builder, trimmed to the forest's budget."""
    pieces = flora.fir(variant, needles=((.06, .18, .13), (.18, .36, .23)), tiers=6, height=7.2, spread=1.45)
    pieces[0].part.budget, pieces[1].part.budget = 80, 400
    return pieces


def pine_bark(p, n):
    """Grey-brown and furrowed low down, flaking orange towards the crown."""
    t = np.clip((p[:, 1] - 1.6) / 2.4, 0, 1)
    flake = .85 + .25 * noise(p[:, 0] * 7, p[:, 1] * 2.5, p[:, 2] * 7, 3)
    return mix((.30, .25, .22), (.82, .44, .22), t) * flake[:, None]


def scots_pine(variant, lean=0.):
    """A Scots pine: a tall bare trunk, orange above, under flat clouds of needles."""
    rng = np.random.default_rng(700 + variant)
    top = (lean * 1.4, 5.5, 0)
    pads = [(top[0], 6.2, 0, 1.55, .62, 1.45)]
    branches = []
    for k in range(3):
        a = k * 2.2 + rng.uniform(-.4, .4)
        h = 4.6 + .45 * k
        tip = (top[0] * h / 5.5 + math.cos(a) * 1.3, h + .5, math.sin(a) * 1.3)
        branches.append(((top[0] * h / 5.5, h, 0), tip, .09, .04))
        pads.append((tip[0], tip[1] + .2, tip[2], rng.uniform(.9, 1.2), .45, rng.uniform(.85, 1.1)))

    def crown(x, y, z):
        d = None
        for cx, cy, cz, rx, ry, rz in pads:
            e = ellipsoid(x, y, z, (cx, cy, cz), (rx, ry, rz))
            d = e if d is None else smin(d, e, .3)
        return d + .14 * fbm(x * 2.2, y * 2.2, z * 2.2, variant + 30)
    trunk = trunk_field(5.6, .22, .1, branches, flare=1.5, lean=(top[0], 0))
    lo = [min(p[0] - p[3] for p in pads) - .4, 4.6, min(p[2] - p[5] for p in pads) - .4]
    hi = [max(p[0] + p[3] for p in pads) + .4, 7.4, max(p[2] + p[5] for p in pads) + .4]
    return [Piece(trunk, (-1.8, -.3, -1.8), (2.2, 6.2, 1.8), .03, 160, pine_bark),
            Piece(crown, lo, hi, .06, 380, foliage((.07, .17, .13), (.22, .38, .25), seed=variant + 31, sun=.55))]


def gorse(variant):
    """A gorse bush: a prickly dark mound dotted with yellow flowers. Built around (0, .7, 0)."""
    rng = np.random.default_rng(800 + variant)
    blobs = [(0, .75, 0, .8)]
    for k in range(6):
        a = k * 1.05 + rng.uniform(-.2, .2)
        blobs.append((math.cos(a) * .75, rng.uniform(.45, .95), math.sin(a) * .7, rng.uniform(.45, .65)))

    def field(x, y, z):
        d = None
        for cx, cy, cz, r in blobs:
            s = np.sqrt((x - cx) ** 2 + (y - cy) ** 2 + (z - cz) ** 2) - r
            d = s if d is None else smin(d, s, .3)
        d = d + .1 * fbm(x * 2.5, y * 2.5, z * 2.5, variant + 40) + .035 * noise(x * 9, y * 9, z * 9, variant)
        return smax(d, -y, .05)

    def colour(p, n):
        c = foliage((.08, .17, .06), (.24, .36, .12), seed=variant + 41, sun=.5)(p, n)
        bloom = (fbm(p[:, 0] * 3.2, p[:, 1] * 3.2, p[:, 2] * 3.2, variant + 42) > .05) & (n[:, 1] > -.3)
        return np.where(bloom[:, None], np.array((.98, .78, .10)) * (.85 + .15 * n[:, 1:2]), c)
    return [Piece(field, (-1.6, -.1, -1.6), (1.6, 1.8, 1.6), .035, 250, colour)]


# --- Undergrowth -----------------------------------------------------------------------------
def heather(variant):
    """A low cushion of heather in flower, on the ground at its origin; about 1 m across."""
    rng = np.random.default_rng(900 + variant)
    blobs = [(rng.uniform(-.25, .25), rng.uniform(.05, .18), rng.uniform(-.25, .25), rng.uniform(.18, .28))
             for _ in range(6)]

    def field(x, y, z):
        d = None
        for cx, cy, cz, r in blobs:
            s = ellipsoid(x, y, z, (cx, cy, cz), (r * 1.3, r * .9, r * 1.3))
            d = s if d is None else smin(d, s, .12)
        return smax(d + .03 * fbm(x * 8, y * 8, z * 8, variant + 50), -y - .02, .02)

    def colour(p, n):
        base = mix((.20, .22, .12), (.36, .34, .18), np.clip(p[:, 1] * 3, 0, 1))
        bloom = mix((.48, .24, .46), (.74, .48, .70), .5 + .5 * noise(p[:, 0] * 6, p[:, 1] * 6, p[:, 2] * 6, variant))
        top = (n[:, 1] > .2) & (noise(p[:, 0] * 9, p[:, 1] * 9, p[:, 2] * 9, variant + 51) > -.35)
        return np.where(top[:, None], bloom, base)
    return [Piece(field, (-.75, -.05, -.75), (.75, .55, .75), .02, 70, colour)]


def fern(variant, colour=((.48, .32, .12), (.80, .60, .25))):
    """A clump of arching bracken fronds from one crown at the origin."""
    rng = np.random.default_rng(1000 + variant)
    points, faces = [], []
    count = 6
    for k in range(count):
        a = k * math.tau / count + rng.uniform(-.3, .3)
        p, f = flora.frond_sheet((0, .05, 0), a, rng.uniform(.8, 1.05), rng.uniform(.45, .6), rng, segments=6,
                                 width=.17)
        base = len(points)
        points += p
        faces += [(i + base, j + base, l + base) for i, j, l in f]
    return [Sheet(points, faces, foliage(colour[0], colour[1], seed=variant + 60, sun=.6))]


def toadstool(variant):
    """A fly agaric 20 cm tall: a pale stalk under a red cap with white warts."""
    h = .2

    def stalk(x, y, z):
        return smax(np.sqrt(x * x + z * z) - .032 - .012 * np.clip(1 - y / .06, 0, 1), np.maximum(-y, y - h), .01)

    def cap(x, y, z):
        dome = ellipsoid(x, y, z, (0, h - .01, 0), (.12, .07, .12))
        return smax(dome, h - .025 - y, .01)

    def spots(p, n):
        warts = noise(p[:, 0] * 45, p[:, 1] * 45, p[:, 2] * 45, variant + 70) > .45
        red = np.array((.80, .10, .06)) * (.85 + .15 * n[:, 1:2])
        return np.where((warts & (n[:, 1] > .1))[:, None], np.array((.94, .92, .86)), red)
    return [Piece(stalk, (-.06, -.02, -.06), (.06, h + .01, .06), .006, 30, lambda p, n: np.tile((.90, .88, .80), (len(p), 1))),
            Piece(cap, (-.15, h - .06, -.15), (.15, h + .08, .15), .006, 60, spots)]


def leaf_pile(variant, colour=AUTUMN_LEAVES[0]):
    """A drift of fallen leaves: a lumpy, flat mound 1 m in radius and .25 m high."""
    def field(x, y, z):
        r = np.sqrt(x * x + z * z)
        top = .25 * np.clip(1 - (r / 1.0) ** 2, 0, 1) + .05 * fbm(x * 3, 0 * y, z * 3, variant + 80)
        rim = r - .95 - .08 * noise(x * 2.5, 0 * y, z * 2.5, variant + 82)
        return smax(smax(y - top, -y - .05, .02), rim, .1)

    def paint(p, n):
        t = noise(p[:, 0] * 7, p[:, 1] * 7, p[:, 2] * 7, variant + 81)
        other = np.array(AUTUMN_LEAVES[(AUTUMN_LEAVES.index(tuple(colour)) + 2) % 5])
        return mix(np.array(colour) * .9, other, np.clip(t + .2, 0, 1) * .6)
    return [Piece(field, (-1.15, -.1, -1.15), (1.15, .4, 1.15), .03, 40, paint)]


def fallen_log(variant):
    """A mossy fallen trunk 2.7 m long along +x with pale broken ends and a shelf fungus."""
    rng = np.random.default_rng(1100 + variant)

    def log(x, y, z):
        d = np.sqrt(y * y + z * z) - .26 + .015 * noise(x * 3, y * 8, z * 8, variant + 90)
        ends = np.maximum(-x - .04 * noise(y * 12, z * 12, 0 * x, variant), x - 2.7 + .05 * noise(y * 10, z * 10, 0 * x, 2))
        d = smax(d, ends, .02)
        # A stub of a broken branch.
        d = smin(d, limb(x, y, z, (1.7, .1, .15), (2.0, .45, .3), .07, .045), .05)
        return d

    def fungus(x, y, z):
        shelf = ellipsoid(x, y, z, (1.2, .12, .25), (.2, .05, .14))
        return smax(shelf, .26 - np.sqrt(y * y + z * z) - .01, .01)

    def paint(p, n):
        c = bark((.32, .24, .17), streak=.25, seed=variant)(p, n)
        moss = (n[:, 1] > .5) & (fbm(p[:, 0] * 2, p[:, 1] * 2, p[:, 2] * 2, variant + 91) > -.1)
        c = np.where(moss[:, None], np.array((.26, .36, .12)), c)
        end = (p[:, 0] < .02) | (p[:, 0] > 2.66)
        return np.where(end[:, None], np.array((.76, .62, .44)), c)
    return [Piece(log, (-.15, -.35, -.35), (2.85, .55, .45), .025, 200, paint),
            Piece(fungus, (.9, 0, .05), (1.5, .25, .45), .01, 40, lambda p, n: np.tile((.86, .78, .60), (len(p), 1)))]


# --- Placement -------------------------------------------------------------------------------
def place(name, species, build, x, y, z, scale, yaw=None, pitch=0.):
    """Like flora.plant(), with an explicit heading (radians about y, from +x towards +z) when given."""
    objects = flora.plant(name, species, build, x, y, z, scale)
    if yaw is not None:
        for o in objects:
            o.rotation_euler = (0, pitch, -yaw)
    return objects
