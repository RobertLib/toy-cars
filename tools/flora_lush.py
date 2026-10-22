"""Rainforest and Japanese garden flora for Jungle Ruins and Sakura Hills.

Species for tools/flora.py's plant(): each builder takes a variant number and
returns pieces at unit scale, standing on the origin (game axes: x right, y up,
z forward). Leaves too thin for the voxel grid are Sheet pieces; the ones that
should flutter in the wind are fronds.
"""
import math

import numpy as np

from sdf import smin, smax, union, ellipsoid
from flora import Piece, Sheet, limb, crown_field, bounds, foliage, bark, mix, noise, fbm, frond_sheet
import flora


# --- Shared shapes ---------------------------------------------------------------------------
def ellipsoid_crown(blobs, k=.4, bumps=.2, freq=1.6, seed=0):
    """Foliage pads: smooth-united ellipsoids (cx, cy, cz, rx, ry, rz) roughened by noise."""
    def field(x, y, z):
        d = None
        for (cx, cy, cz, rx, ry, rz) in blobs:
            e = ellipsoid(x, y, z, (cx, cy, cz), (rx, ry, rz))
            d = e if d is None else smin(d, e, k)
        return d + bumps * fbm(x * freq, y * freq, z * freq, seed)
    return field


def pad_bounds(blobs, pad=.5):
    lo = [min(b[i] - b[i + 3] for b in blobs) - pad for i in range(3)]
    hi = [max(b[i] + b[i + 3] for b in blobs) + pad for i in range(3)]
    return lo, hi


def merge(sheets):
    """Concatenates (points, faces) lists into one."""
    points, faces = [], []
    for p, f in sheets:
        base = len(points)
        points += list(p)
        faces += [(a + base, b + base, c + base) for a, b, c in f]
    return points, faces


def blade(base, direction, length, width, rise, droop, segments=6, fold=.25, taper=1., tears=(), lean=None):
    """A leaf blade along a horizontal heading: a raised midrib between two edges.

    direction is the heading angle; the blade climbs at rise (metres per metre)
    and droops quadratically towards the tip. tears lists segment numbers whose
    outer edge pulls in, like a banana leaf split by the wind."""
    ca, sa = math.cos(direction), math.sin(direction)
    points, faces = [], []
    for i in range(segments + 1):
        t = i / segments
        along = length * t
        h = rise * along - droop * length * t * t
        cx, cy, cz = base[0] + ca * along, base[1] + h, base[2] + sa * along
        w = width * (math.sin(math.pi * min(1, (t * .92 + .08))) ** taper)
        if i in tears:
            w *= .45
        sag = fold * w
        points += [(cx - sa * w, cy - sag, cz + ca * w), (cx, cy + .02, cz), (cx + sa * w, cy - sag, cz - ca * w)]
        if i:
            a, b = 3 * (i - 1), 3 * i
            faces += [(a, a + 1, b + 1), (a, b + 1, b), (a + 1, a + 2, b + 2), (a + 1, b + 2, b + 1)]
    return points, faces


def lance(base, direction, length, width, pitch, droop=.3):
    """A narrow pointed leaf (bamboo, iris): four points, two triangles."""
    ca, sa = math.cos(direction), math.sin(direction)
    cp, sp = math.cos(pitch), math.sin(pitch)
    mid = (base[0] + ca * cp * length * .45, base[1] + sp * length * .45, base[2] + sa * cp * length * .45)
    tip = (base[0] + ca * cp * length, base[1] + sp * length - droop * length, base[2] + sa * cp * length)
    left = (mid[0] - sa * width, mid[1], mid[2] + ca * width)
    right = (mid[0] + sa * width, mid[1], mid[2] - ca * width)
    return [base, left, tip, right], [(0, 1, 2), (0, 2, 3)]


def blossom(dark, light, spots, seed=0):
    """Crown colouring speckled with flowers: spots is a list of (colour, share)."""
    leaves = foliage(dark, light, seed=seed)

    def colour(p, n):
        c = leaves(p, n)
        for k, (tone, share) in enumerate(spots):
            v = noise(p[:, 0] * 5.5, p[:, 1] * 5.5, p[:, 2] * 5.5, seed + 11 * k + 5)
            # The share of vertices that flower, whatever the noise's spread.
            hit = (v > np.quantile(v, 1 - share)) & (n[:, 1] > -.3)
            c = np.where(hit[:, None], np.array(tone, float), c)
        return c
    return colour


# --- Jungle Ruins ----------------------------------------------------------------------------
def kapok(variant):
    """An emergent kapok: a pale straight bole on plank buttresses under a flat umbrella crown."""
    rng = np.random.default_rng(500 + variant)
    height = 8.4
    roots = [k * math.tau / 5 + rng.uniform(-.25, .25) for k in range(5)]
    arms = [k * math.tau / 7 + rng.uniform(-.25, .25) + .6 for k in range(7)]

    def trunk(x, y, z):
        d = limb(x, y, z, (0, -.3, 0), (.1, height, 0), .40, .24)
        for a in roots:
            u = x * math.cos(a) + z * math.sin(a)
            v = -x * math.sin(a) + z * math.cos(a)
            top = 2.5 * (1 - np.clip((u - .25) / 1.75, 0, 1)) ** 1.4
            plate = np.maximum(np.abs(v) - (.10 - .04 * np.clip(u / 2, 0, 1)), (y - top) * .6)
            plate = smax(smax(plate, -u, .1), -(y + .25), .05)
            d = smin(d, plate, .18)
        for i, a in enumerate(arms[:5]):
            reach = 1.9 + .3 * (i % 2)
            end = (math.cos(a) * reach, height + .55 + .15 * (i % 3), math.sin(a) * reach)
            d = smin(d, limb(x, y, z, (0, height - .9, 0), end, .15, .06), .15)
        return d + .015 * noise(x * 9, y * 2, z * 9, variant)

    blobs = [(0, height + 1.3, 0, 2.1, .95, 2.0)]
    for i, a in enumerate(arms):
        r = rng.uniform(2.3, 3.0)
        blobs.append((math.cos(a) * r, height + .8 + rng.uniform(0, .5), math.sin(a) * r,
                      rng.uniform(1.4, 1.8), rng.uniform(.62, .8), rng.uniform(1.3, 1.7)))
    lo, hi = pad_bounds(blobs)

    def pale_bark(p, n):
        c = bark((.41, .38, .32), streak=.12, seed=variant)(p, n)
        moss = np.clip(1.2 - p[:, 1] / 1.6, 0, 1) * (.5 + .5 * noise(p[:, 0] * 4, p[:, 1] * 4, p[:, 2] * 4, variant))
        return mix(c, (.20, .30, .14), moss * .8)
    return [Piece(trunk, (-2.4, -.3, -2.4), (2.4, height + 1.2, 2.4), .04, 160, pale_bark),
            Piece(ellipsoid_crown(blobs, k=.5, bumps=.28, freq=1.3, seed=variant), lo, hi, .07, 360,
                  foliage((.04, .16, .06), (.20, .42, .12), seed=variant, sun=.6))]


def jungle_palm(variant):
    """The coast's coconut palm in deeper rainforest greens, leaner for a dense forest."""
    pieces = flora.palm(variant, leaves=((.05, .22, .09), (.25, .50, .15)), fronds=8)
    pieces[0].part.budget, pieces[1].part.budget = 160, 50
    return pieces


def banana(variant):
    """A banana plant: a sheathed pseudostem under broad, torn paddle leaves."""
    rng = np.random.default_rng(600 + variant)
    top = (.3, 2.3, 0.)

    def stem(x, y, z):
        t = np.clip(y / top[1], 0, 1)
        d = np.sqrt((x - top[0] * t) ** 2 + z * z) - (.19 - .07 * t) - .012 * np.cos(np.arctan2(z, x) * 7)
        d = smax(d, np.maximum(-y - .15, y - top[1] - .1), .04)
        if variant != 1:
            # A hanging bunch with its purple heart.
            bunch = ellipsoid(x, y, z, (top[0] + .32, top[1] - .35, .1), (.17, .28, .17))
            heart = ellipsoid(x, y, z, (top[0] + .40, top[1] - .78, .12), (.08, .14, .08))
            d = union(d, bunch, heart, k=.05)
        return d

    def stem_colour(p, n):
        c = mix((.32, .27, .16), (.36, .48, .20), p[:, 1] / 1.0)
        bunch = (p[:, 0] > top[0] + .12) & (p[:, 1] > top[1] - .65) & (p[:, 1] < top[1] - .05)
        heart = (p[:, 0] > top[0] + .25) & (p[:, 1] < top[1] - .62) & (p[:, 1] > top[1] - .95)
        c = np.where(bunch[:, None], np.array((.46, .55, .16)), c)
        return np.where(heart[:, None], np.array((.42, .12, .24)), c)

    sheets = []
    for k in range(7):
        a = k * math.tau / 7 + rng.uniform(-.25, .25)
        # Young leaves stand up from the centre; old ones arch out and hang.
        young = k % 4 == 1
        tears = tuple(int(v) for v in rng.integers(2, 8, 2)) if rng.random() < .7 else ()
        sheets.append(blade((top[0], top[1] + rng.uniform(-.1, .3), top[2]), a,
                            rng.uniform(1.6, 1.9) if young else rng.uniform(2.3, 2.9), rng.uniform(.40, .52),
                            1.6 if young else rng.uniform(.6, .95), .35 if young else rng.uniform(.6, .85),
                            8, .22, .55, tears))
    points, faces = merge(sheets)
    return [Piece(stem, (-.4, -.2, -.4), (1.0, 2.5, .5), .025, 140, stem_colour),
            Sheet(points, faces, foliage((.10, .32, .10), (.38, .62, .20), seed=variant, sun=.65), frond=True)]


def tree_fern(variant):
    """A tree fern: a shaggy dark column crowned with arching, finely cut fronds."""
    rng = np.random.default_rng(700 + variant)
    top = (.35, 3.3, 0.)

    def trunk(x, y, z):
        t = np.clip(y / top[1], 0, 1)
        cx = top[0] * t * t
        shag = .02 * noise(x * 14, y * 5, z * 14, variant)
        d = np.sqrt((x - cx) ** 2 + z * z) - (.22 - .06 * t) - shag
        d = smax(d, np.maximum(-y - .15, y - top[1]), .04)
        # Coiled fiddleheads unrolling at the crown.
        for a in (.3, 2.4, 4.4):
            d = smin(d, np.sqrt((x - top[0] - .16 * math.cos(a)) ** 2 + (y - top[1] - .12) ** 2
                                + (z - .16 * math.sin(a)) ** 2) - .07, .04)
        return d

    sheets = []
    for k in range(9):
        a = k * math.tau / 9 + rng.uniform(-.15, .15)
        # Young fronds still climb; the outer ones spread level and dip at the tips.
        droop = rng.uniform(.25, .45) if k % 3 else rng.uniform(.05, .15)
        sheets.append(frond_sheet(top, a, rng.uniform(2.2, 2.8), droop, rng, 10, .32))
    points, faces = merge(sheets)
    return [Piece(trunk, (-.5, -.2, -.5), (.9, 3.6, .5), .025, 130, bark((.20, .15, .11), streak=.3, seed=variant)),
            Sheet(points, faces, foliage((.08, .28, .08), (.34, .58, .18), seed=variant, sun=.6), frond=True)]


def elephant_ears(variant):
    """Big arrow-shaped leaves held out on long stalks, close to the forest floor."""
    rng = np.random.default_rng(800 + variant)
    sheets = []
    for k in range(5):
        a = k * math.tau / 5 + rng.uniform(-.3, .3)
        ca, sa = math.cos(a), math.sin(a)
        reach, high = rng.uniform(.35, .55), rng.uniform(.65, 1.0)
        tip = (ca * reach, high, sa * reach)
        # Stalk: a narrow strip from the ground up to the blade.
        sheets.append(([(-sa * .025, -.05, ca * .025), (sa * .025, -.05, -ca * .025), tip], [(0, 1, 2)]))
        sheets.append(blade(tip, a, rng.uniform(.75, .95), rng.uniform(.34, .42), -.15, .35, 6, .16, .45))
    points, faces = merge(sheets)
    return [Sheet(points, faces, foliage((.06, .26, .09), (.30, .55, .18), seed=variant, sun=.7))]


def flowering_shrub(variant):
    """A dense rainforest shrub studded with heliconia, orchid and hibiscus flowers."""
    rng = np.random.default_rng(900 + variant)
    blobs = [(0, .45, 0, .62)] + [(math.cos(a) * .48, rng.uniform(.3, .65), math.sin(a) * .42, rng.uniform(.38, .5))
                                  for a in rng.uniform(0, math.tau, 5)]
    lo, hi = bounds(blobs, .3)
    lo[1] = -.15
    crown = crown_field(blobs, k=.25, bumps=.08, freq=3.2, seed=variant)
    # Flowers sit on the outside of the crown, mostly on its upper half.
    blooms = []
    for k in range(9):
        a, h = rng.uniform(0, math.tau), rng.uniform(.25, .95)
        d = np.array((math.cos(a) * math.sqrt(1 - h * h), h, math.sin(a) * math.sqrt(1 - h * h)))
        p = np.array((0, .45, 0)) + d * .9
        for _ in range(12):
            p = p - d * float(crown(*[np.array([v]) for v in p])[0]) * .8
        blooms.append((p, k % 3))
    tones = [(.92, .16, .07), (.88, .38, .72), (.98, .78, .10)]

    def flowers(x, y, z):
        d = None
        for p, _ in blooms:
            e = ellipsoid(x, y, z, tuple(p + np.array((0, .02, 0))), (.085, .06, .085))
            d = e if d is None else np.minimum(d, e)
        return d

    def flower_colour(pts, n):
        c = np.zeros((len(pts), 3))
        near = np.argmin([np.linalg.norm(pts - p, axis=1) for p, _ in blooms], axis=0)
        for i, (_, tone) in enumerate(blooms):
            c[near == i] = tones[tone]
        return c * (.85 + .15 * n[:, 1:2])
    return [Piece(lambda x, y, z: smax(crown(x, y, z), -y - .1, .05), lo, hi, .03, 200,
                  foliage((.05, .21, .07), (.24, .46, .14), seed=variant)),
            Piece(flowers, lo, hi, .012, 110, flower_colour)]


def fern_clump(variant):
    """A ground fern: a rosette of small cut fronds."""
    rng = np.random.default_rng(1000 + variant)
    sheets = []
    for k in range(7):
        a = k * math.tau / 7 + rng.uniform(-.3, .3)
        sheets.append(frond_sheet((0, .05, 0), a, rng.uniform(.9, 1.25), rng.uniform(.35, .6), rng, 10, .13))
    points, faces = merge(sheets)
    return [Sheet(points, faces, foliage((.08, .28, .09), (.36, .60, .20), seed=variant, sun=.7))]


# --- Sakura Hills ----------------------------------------------------------------------------
def cherry(variant):
    """A cherry in full blossom: a dark, twisting trunk spreading into soft pink clouds."""
    rng = np.random.default_rng(1100 + variant)
    fork = (.25, 2.3, .05)
    ends = []
    for k in range(4):
        a = k * math.tau / 4 + rng.uniform(-.4, .4)
        ends.append((math.cos(a) * rng.uniform(1.5, 2.0), rng.uniform(3.3, 4.0), math.sin(a) * rng.uniform(1.4, 1.9)))

    def trunk(x, y, z):
        twist = .05 * noise(x * 3, y * 1.4, z * 3, variant)
        d = limb(x, y, z, (0, -.3, 0), fork, .26, .16) + twist
        a = np.arctan2(z, x)
        foot = np.sqrt(x * x + z * z) - .42 * (1 + .25 * np.cos(4 * a)) * np.clip(1 - y / .6, 0, 1)
        d = smin(d, smax(foot, -y - .2, .05), .2)
        for e in ends:
            elbow = ((fork[0] + e[0]) * .5, (fork[1] + e[1]) * .5 + .2, (fork[2] + e[2]) * .5)
            d = smin(d, limb(x, y, z, fork, elbow, .14, .09), .1)
            d = smin(d, limb(x, y, z, elbow, e, .09, .04), .06)
        return d

    blobs = [(fork[0], 4.6, fork[2], 1.25)]
    for e in ends:
        blobs.append((e[0], e[1] + .25, e[2], rng.uniform(.9, 1.15)))
        blobs.append((e[0] * .55, e[1] + .85, e[2] * .55, rng.uniform(.8, 1.0)))
    for a in rng.uniform(0, math.tau, 3):
        blobs.append((math.cos(a) * 1.0, 5.2, math.sin(a) * 1.0, .75))
    lo, hi = bounds(blobs)
    petals = blossom((.90, .56, .68), (1., .88, .92), [((1., .97, .97), .14), ((.95, .52, .66), .08)], variant)
    return [Piece(trunk, (-2.4, -.3, -2.4), (2.6, 4.3, 2.4), .03, 150, bark((.22, .15, .14), streak=.25, seed=variant)),
            Piece(crown_field(blobs, k=.3, bumps=.3, freq=1.9, seed=variant), lo, hi, .055, 380, petals)]


def black_pine(variant):
    """A cloud-pruned black pine: a leaning, kinked trunk holding flat needle pads."""
    rng = np.random.default_rng(1200 + variant)
    spine = [(0, -.3, 0), (.35, 1.4, .1), (.95, 2.6, -.05), (1.15, 3.7, .2), (.9, 4.4, .25)]
    pads = [(spine[-1][0], 4.75, spine[-1][2], 1.15, .40, 1.0)]
    branches = []
    for k, (i, a) in enumerate(zip((1, 2, 2, 3), rng.uniform(0, math.tau, 4))):
        start = spine[i]
        reach = rng.uniform(1.1, 1.6)
        end = (start[0] + math.cos(a) * reach, start[1] + rng.uniform(.2, .6), start[2] + math.sin(a) * reach)
        branches.append((start, end))
        pads.append((end[0], end[1] + .25, end[2], rng.uniform(1.0, 1.35), rng.uniform(.34, .42), rng.uniform(.85, 1.15)))

    def trunk(x, y, z):
        d = None
        for (a, b), r in zip(zip(spine[:-1], spine[1:]), (.24, .19, .15, .12, .09)):
            s = limb(x, y, z, a, b, r, r * .82)
            d = s if d is None else smin(d, s, .12)
        for a, b in branches:
            d = smin(d, limb(x, y, z, a, b, .10, .05), .08)
        return d + .02 * noise(x * 6, y * 3, z * 6, variant)

    def pad_field(x, y, z):
        d = ellipsoid_crown(pads, k=.15, bumps=.10, freq=2.4, seed=variant)(x, y, z)
        # Flatten the undersides into trimmed shelves.
        for (cx, cy, cz, rx, ry, rz) in pads:
            d = np.where(np.abs(y - cy) < ry * 2, smax(d, -(y - cy + ry * .55), .08), d)
        return d
    lo, hi = pad_bounds(pads, .4)
    return [Piece(trunk, (-1.6, -.3, -1.6), (2.8, 4.8, 1.8), .03, 150, bark((.24, .19, .17), streak=.3, seed=variant)),
            Piece(pad_field, lo, hi, .05, 330, foliage((.04, .15, .10), (.18, .34, .20), seed=variant, sun=.7))]


def japanese_maple(variant):
    """A Japanese maple: slender stems fanning into layered, fine-leaved red tiers."""
    rng = np.random.default_rng(1300 + variant)
    ends = [(math.cos(a) * rng.uniform(.9, 1.4), rng.uniform(2.6, 3.4), math.sin(a) * rng.uniform(.9, 1.4))
            for a in np.arange(5) * math.tau / 5 + rng.uniform(0, 1)]

    def trunk(x, y, z):
        d = limb(x, y, z, (0, -.3, 0), (.05, 1.3, 0), .17, .12)
        for e in ends:
            d = smin(d, limb(x, y, z, (.05, 1.2, 0), e, .08, .03), .08)
        return d

    blobs = []
    for e in ends:
        blobs.append((e[0] * 1.1, e[1] + .1, e[2] * 1.1, 1.0, .5, .95))
    blobs.append((0, 3.9, 0, 1.25, .55, 1.15))
    blobs.append((0, 4.35, 0, .8, .4, .75))
    lo, hi = pad_bounds(blobs)
    leaves = blossom((.40, .04, .03), (.90, .26, .08), [((.97, .52, .12), .06)], variant)
    return [Piece(trunk, (-1.6, -.3, -1.6), (1.6, 3.6, 1.6), .025, 120, bark((.30, .20, .17), seed=variant)),
            Piece(ellipsoid_crown(blobs, k=.35, bumps=.16, freq=2.6, seed=variant), lo, hi, .05, 340, leaves)]


def bamboo(variant):
    """A bamboo clump: jointed green canes with sprays of narrow leaves near the top."""
    rng = np.random.default_rng(1400 + variant)
    canes = []
    for k in range(8):
        a, r = rng.uniform(0, math.tau), rng.uniform(.1, .85)
        h = rng.uniform(4.6, 7.0)
        base = (math.cos(a) * r, math.sin(a) * r)
        lean = (math.cos(a) * rng.uniform(.25, .6) + .2, math.sin(a) * rng.uniform(.25, .6))
        canes.append((base, lean, h, rng.uniform(.055, .07)))

    def field(x, y, z):
        d = None
        for (bx, bz), (lx, lz), h, r in canes:
            t = np.clip(y / h, 0, 1)
            cx, cz = bx + lx * t * t, bz + lz * t * t
            node = .008 * np.cos(y * 2 * math.pi / .42) ** 8
            c = np.sqrt((x - cx) ** 2 + (z - cz) ** 2) - r * (1 - .3 * t) - node
            c = smax(c, np.maximum(-y - .1, y - h), .02)
            d = c if d is None else np.minimum(d, c)
        return d

    def cane_colour(p, n):
        c = mix((.30, .42, .15), (.55, .66, .26), .5 + .5 * noise(p[:, 0] * 3, p[:, 1] * .7, p[:, 2] * 3, variant))
        joint = np.cos(p[:, 1] * 2 * math.pi / .42) ** 8
        return c * (1 - .25 * joint)[:, None]

    sheets = []
    for (bx, bz), (lx, lz), h, r in canes:
        # Fans of drooping leaves on short twigs, thickest near the top.
        for f in (.7, .85, .97):
            y = h * f
            cx, cz = bx + lx * f * f, bz + lz * f * f
            spray = rng.uniform(0, math.tau)
            for j in range(6):
                a = spray + (j - 2.5) * .36
                sheets.append(lance((cx, y, cz), a, rng.uniform(.45, .65), .065, rng.uniform(-.05, .35), .55))
    points, faces = merge(sheets)
    return [Piece(field, (-1.3, -.2, -1.3), (1.9, 7.1, 1.6), .02, 220, cane_colour),
            Sheet(points, faces, foliage((.12, .30, .10), (.42, .62, .22), seed=variant, sun=.7), frond=True)]


AZALEA = {'pink': (.93, .42, .62), 'white': (.96, .94, .93), 'magenta': (.80, .18, .48)}


def azalea(colour):
    """A clipped azalea mound smothered in flowers of one colour."""
    def build(variant):
        rng = np.random.default_rng(1500 + variant)
        blobs = [(0, .32, 0, .55)] + [(math.cos(a) * .3, .3, math.sin(a) * .28, .38) for a in rng.uniform(0, math.tau, 4)]
        lo, hi = bounds(blobs, .25)
        lo[1] = -.15
        flowers = blossom((.07, .22, .08), (.22, .40, .14), [(AZALEA[colour], .42), (tuple(np.minimum(1, np.array(AZALEA[colour]) * 1.12)), .2)], variant)
        return [Piece(lambda x, y, z: smax(crown_field(blobs, k=.3, bumps=.05, freq=4, seed=variant)(x, y, z), -y - .1, .04),
                      lo, hi, .025, 160, flowers)]
    return ('lush', 'azalea', colour), build


def moss_mound(variant):
    """A low cushion of garden moss."""
    def field(x, y, z):
        d = ellipsoid(x, y, z, (0, .02, 0), (.95, .32, .85))
        return smax(d + .05 * fbm(x * 3, y * 3, z * 3, variant), -y - .1, .05)
    return [Piece(field, (-1.1, -.15, -1.0), (1.1, .4, 1.0), .03, 110,
                  foliage((.16, .30, .10), (.42, .58, .22), seed=variant, sun=.4))]


def iris(variant):
    """A clump of blue flag irises: upright sword leaves and a few open blooms."""
    rng = np.random.default_rng(1600 + variant)
    sheets = []
    for k in range(9):
        a = rng.uniform(0, math.tau)
        r = rng.uniform(0, .25)
        sheets.append(lance((math.cos(a) * r, -.05, math.sin(a) * r), a, rng.uniform(.5, .75), .045,
                            rng.uniform(1.1, 1.4), .12))
    stems = [(math.cos(a) * .15, rng.uniform(.6, .75), math.sin(a) * .15) for a in rng.uniform(0, math.tau, 3)]
    points, faces = merge(sheets)

    def blooms(x, y, z):
        d = None
        for cx, cy, cz in stems:
            petals = ellipsoid(x, y, z, (cx, cy, cz), (.09, .06, .09))
            stem = limb(x, y, z, (cx * .5, -.05, cz * .5), (cx, cy - .04, cz), .012, .01)
            s = smin(petals, stem, .02)
            d = s if d is None else np.minimum(d, s)
        return d

    def bloom_colour(p, n):
        c = np.tile(np.array((.36, .30, .78)), (len(p), 1))
        c = np.where((p[:, 1] < .5)[:, None], np.array((.22, .40, .14)), c)
        return np.where((n[:, 1] > .75)[:, None], np.array((.96, .82, .22)), c)
    return [Sheet(points, faces, foliage((.10, .30, .10), (.36, .56, .20), seed=variant, sun=.4)),
            Piece(blooms, (-.4, -.1, -.4), (.4, .9, .4), .012, 60, bloom_colour)]
