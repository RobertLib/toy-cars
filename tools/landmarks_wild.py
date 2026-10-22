"""Landmarks of the canyon, autumn, highland and volcano circuits.

Built like tools/landmarks.py with the signed distance pipeline and placed as
linked duplicates through roadside.place(). Every model stands on y = 0 with
its front towards +z. Lit windows and lamps keep the exact lantern colour
GLOW, unshaded, so the renderer lights them after dark (see biomes.GLOW).
"""
import math

import numpy as np

import sdf
from sdf import smin, smax, union, subtract, box, rect2, ellipsoid, capsule
from sdf import cylinder_x, cylinder_y, cylinder_z, polyline2, rounded_polygon, Profile
from cars import part, convex, GLOSS, METAL, GLASS
from roadside import assemble, WHITE, BLACK
from flora import fbm, noise

GLOW = (.98, .80, .40)
DARK_GLASS = (.10, .16, .20)
TIMBER = (.50, .36, .24)
TIN = (.62, .64, .63)
STONE = (.50, .48, .44)
GRANITE = (.52, .52, .50)


def lit(parts, decals=(), reach=.8):
    """assemble(), keeping lantern-coloured parts at the exact unshaded GLOW."""
    mesh, colour = assemble(parts, decals, reach)
    glow = np.all(np.abs(mesh.colors - np.array(GLOW)) < 1e-6, axis=1)
    for k in range(3):
        colour[mesh.faces[glow, k]] = GLOW
    return mesh, colour


def groups(*fields):
    """Union of optional fields (None ones skipped)."""
    out = None
    for f in fields:
        if f is not None:
            out = f if out is None else np.minimum(out, f)
    return out


def masonry(u, y, course=.5, block=1.0, joint=.035):
    """Positive near the joints of a bond of blocks along u."""
    row = np.floor(y / course)
    v = np.abs(np.mod(y, course) - course / 2) - (course / 2 - joint)
    h = np.abs(np.mod(u + np.mod(row, 2) * block / 2, block) - block / 2) - (block / 2 - joint)
    return np.maximum(v, h)


# --- A general cottage -------------------------------------------------------------------------
def cottage(width, depth, eaves, ridge, walls, roof_colour, *, ridge_x=False, wall='boards', roof='tin',
            trim=WHITE, windows=(), doors=(), chimneys=(), porch=None, footing=STONE, lit_windows=True,
            overhang=.45, door_colour=(.55, .18, .12), budget=1.):
    """Parts of a gabled house, width along x and depth along z, its front face at +z.

    The ridge runs along z (gables at the front and back) unless ridge_x.
    windows: (face, along, height, half width, half height); doors: (face, along, width, height).
    Faces are '+z', '-z', '+x', '-x'; 'along' is x on z faces and z on x faces.
    chimneys: (x, z, top, half size); porch: (depth, post xs) along the +z face."""
    hx, hz = width / 2, depth / 2
    hu, hv = (hz, hx) if ridge_x else (hx, hz)
    gable = Profile(rounded_polygon([(-hu, .42), (hu, .42), (hu, eaves), (0, ridge), (-hu, eaves)], .03))
    drop = overhang * (ridge - eaves) / hu
    slope = [(-hu - overhang, eaves - drop), (0, ridge + .02), (hu + overhang, eaves - drop)]

    def uv(x, z):
        return (z, x) if ridge_x else (x, z)

    def face_frame(face, x, z):
        """(normal coordinate minus the wall plane, along-face coordinate) for a face."""
        if face[1] == 'z':
            return (z - hz) if face[0] == '+' else (-z - hz), x
        return (x - hx) if face[0] == '+' else (-x - hx), z

    def openings(x, y, z, grow=0., inset=.13, front=.12):
        d = None
        for face, a, h, w, hh in windows:
            n, t = face_frame(face, x, z)
            o = smax(rect2(t, y, (a, h), (w + grow, hh + grow), .03), np.maximum(-n - inset, n - front), .01)
            d = o if d is None else np.minimum(d, o)
        for face, a, w, hh in doors:
            n, t = face_frame(face, x, z)
            o = smax(rect2(t, y, (a, .45 + hh / 2), (w / 2 + grow, hh / 2 + grow), .03),
                     np.maximum(-n - inset, n - front), .01)
            d = o if d is None else np.minimum(d, o)
        return d

    def shell(x, y, z):
        u, v = uv(x, z)
        body = smax(gable(u, y), np.abs(v) - hv, .03)
        on_end = np.abs(np.abs(v) - hv) < .06
        along = np.where(on_end, u, v)
        if wall == 'logs':
            body = body + (.05 * (np.abs(np.mod(y, .32) - .16) / .16) - .025) * (y < eaves + .1)
        elif wall == 'boards':
            body = subtract(body, smax(np.abs(np.mod(along, .36) - .18) - .16, -(body + .035), .005), .007)
        elif wall == 'ribbed':
            body = body - .022 * np.cos(along * 2 * math.pi / .25)
        elif wall == 'stone':
            body = body + .03 * fbm(x * 1.5, y * 1.5, z * 1.5, 5)
        else:  # plaster
            body = body + .012 * noise(x * 2.3, y * 2.3, z * 2.3, 5)
        hole = openings(x, y, z)
        return body if hole is None else subtract(body, hole, .01)

    def roofing(x, y, z):
        u, v = uv(x, z)
        line = polyline2(u, y, slope)
        thick = {'thatch': .26, 'tin': .09, 'shingle': .12, 'slate': .08}[roof]
        slab = smax(line - thick, np.abs(v) - hv - overhang, .03 if roof != 'thatch' else .12)
        if roof == 'tin':
            slab = slab - .016 * np.cos(v * 2 * math.pi / .42)
        elif roof in ('shingle', 'slate'):
            # Courses of shingles or slates: a soft ripple down the slope.
            slab = slab - (.018 if roof == 'shingle' else .01) * np.cos(y * 2 * math.pi / (.24 if roof == 'shingle' else .2))
        else:
            slab = slab + .035 * fbm(x * 2.5, y * 2.5, z * 2.5, 9)
        ends = ((0, ridge + .05, -hv - overhang), (0, ridge + .05, hv + overhang))
        if ridge_x:
            ends = ((-hv - overhang, ridge + .05, 0), (hv + overhang, ridge + .05, 0))
        ridge_cap = capsule(x, y, z, *ends, .09 if roof != 'thatch' else .2)
        return smin(slab, ridge_cap, .03)

    def trims(x, y, z):
        u, v = uv(x, z)
        frames = None
        if trim is not None:
            for face, a, h, w, hh in windows:
                n, t = face_frame(face, x, z)
                f = smax(np.abs(rect2(t, y, (a, h), (w, hh), .03)) - .06, np.abs(n - .02) - .05, .01)
                bars = smax(np.minimum(np.abs(t - a) - .025, np.abs(y - h) - .025),
                            smax(rect2(t, y, (a, h), (w, hh), .02), np.abs(n + .06) - .025, .005), .005)
                frames = groups(frames, f, bars)
            for face, a, w, hh in doors:
                n, t = face_frame(face, x, z)
                f = smax(np.abs(rect2(t, y, (a, .45 + hh / 2), (w / 2, hh / 2), .03)) - .07, np.abs(n - .02) - .05, .01)
                frames = groups(frames, f)
            if wall != 'logs' and wall != 'stone':
                ax, az = np.abs(x), np.abs(z)
                corners = smax(rect2(ax, az, (hx, hz), (.12, .12), .02), np.abs(y - (eaves + .42) / 2) - (eaves - .42) / 2, .02)
                frames = groups(frames, corners)
            fascia = smax(polyline2(u, y, slope) - .15, np.abs(np.abs(v) - hv - overhang) - .05, .01)
            frames = groups(frames, fascia)
        if porch is not None:
            pd, posts = porch
            deck = box(x, y, z, (0, .32, hz + pd / 2), (hx + .2, .1, pd / 2), .03)
            columns = groups(*[box(x, y, z, (px, (eaves - .2) / 2 + .2, hz + pd - .25), (.09, (eaves - .4) / 2, .09), .02)
                               for px in posts])
            rail = smax(box(x, y, z, (0, 1.2, hz + pd - .25), (hx, .05, .05), .02), -np.abs(x) + .8, .02)
            frames = groups(frames, deck, columns, rail)
        return frames

    def porch_roof(x, y, z):
        pd, _ = porch
        top, low = eaves - .1, eaves - .1 - .35 * pd
        line = polyline2(z, y, [(hz - .1, top), (hz + pd + .35, low)])
        return smax(line - .07, np.abs(x) - hx - .3, .02) - .012 * np.cos(x * 2 * math.pi / .42)

    def panes(x, y, z):
        d = None
        for face, a, h, w, hh in windows:
            n, t = face_frame(face, x, z)
            p = smax(rect2(t, y, (a, h), (w + .02, hh + .02), .02), np.abs(n + .07) - .02, .005)
            d = p if d is None else np.minimum(d, p)
        return d

    def door_leaves(x, y, z):
        d = None
        for face, a, w, hh in doors:
            n, t = face_frame(face, x, z)
            leaf = smax(rect2(t, y, (a, .45 + hh / 2), (w / 2 + .02, hh / 2 + .02), .02), np.abs(n + .06) - .03, .005)
            leaf = subtract(leaf, smax(np.abs(np.mod(t - a, .2) - .1) - .088, n + .025, .003), .003)
            d = leaf if d is None else np.minimum(d, leaf)
        return d

    def stacks(x, y, z):
        d = None
        for cx, cz, top, s in chimneys:
            c = box(x, y, z, (cx, top / 2, cz), (s, top / 2, s), .03)
            c = c + .02 * fbm(x * 3, y * 3, z * 3, 8)
            cap = box(x, y, z, (cx, top + .06, cz), (s + .08, .06, s + .08), .02)
            pot = cylinder_y(x, y, z, (cx, top + .25, cz), s * .45, .16, .03)
            c = union(c, cap, pot, k=.02)
            d = c if d is None else np.minimum(d, c)
        return d

    def base(x, y, z):
        return box(x, y, z, (0, .22, 0), (hx + .1, .24, hz + .1), .05)

    reach = max(width, depth)
    lo = (-hx - overhang - .4, -.05, -hz - overhang - .4)
    hi = (hx + overhang + .4, max([ridge + .5] + [c[2] + .5 for c in chimneys]), hz + overhang + .4 + (porch[0] if porch else 0))
    vox = max(.025, reach / 360)
    b = lambda n: int(n * budget)
    area = 2 * (width + depth) * eaves + width * (ridge - eaves)
    textured = wall in ('boards', 'stone', 'logs', 'ribbed')
    parts = [part(shell, lo, hi, vox, GLOSS, walls, b(int(area * (24 if textured else 12)) + 600)),
             part(roofing, lo, hi, vox, GLOSS, roof_colour, b(1800))]
    if trims(np.zeros(1), np.zeros(1) + 1, np.zeros(1)) is not None:
        parts.append(part(trims, lo, hi, vox * .85, GLOSS, trim if trim is not None else TIMBER, b(1600)))
    if windows:
        parts.append(part(panes, lo, hi, vox * .85, GLASS, GLOW if lit_windows else DARK_GLASS, b(160)))
    if doors:
        parts.append(part(door_leaves, lo, hi, vox * .85, GLOSS, door_colour, b(240)))
    if chimneys:
        parts.append(part(stacks, lo, hi, vox, GLOSS, STONE, b(500 * len(chimneys))))
    if porch is not None:
        parts.append(part(porch_roof, lo, hi, vox, GLOSS, roof_colour, b(500)))
    if footing is not None:
        parts.append(part(base, lo, hi, vox, GLOSS, footing, b(150)))
    return parts


# --- Canyon Dash -----------------------------------------------------------------------------------
def water_tower():
    """A wooden stave tank on splayed timber legs, a tin cone roof, a spout and a ladder."""
    legs = [((dx, 0, dz), (dx * .7, 6.05, dz * .7)) for dx in (-1.6, 1.6) for dz in (-1.6, 1.6)]

    def frame(x, y, z):
        d = groups(*[capsule(x, y, z, a, b, .15) for a, b in legs])
        # Cross bracing between neighbouring legs, two tiers.
        ring = [legs[0], legs[1], legs[3], legs[2]]
        for (a0, a1), (b0, b1) in zip(ring, ring[1:] + ring[:1]):
            for t0, t1 in ((.1, .45), (.5, .85)):
                pa = [a0[i] + (a1[i] - a0[i]) * t0 for i in range(3)]
                pb = [b0[i] + (b1[i] - b0[i]) * t1 for i in range(3)]
                qa = [a0[i] + (a1[i] - a0[i]) * t1 for i in range(3)]
                qb = [b0[i] + (b1[i] - b0[i]) * t0 for i in range(3)]
                d = np.minimum(d, np.minimum(capsule(x, y, z, pa, pb, .06), capsule(x, y, z, qa, qb, .06)))
        deck = box(x, y, z, (0, 6.05, 0), (1.95, .08, 1.95), .03)
        ladder = groups(capsule(x, y, z, (-.25, 0, 2.35), (-.25, 6.0, 1.95), .04),
                        capsule(x, y, z, (.25, 0, 2.35), (.25, 6.0, 1.95), .04))
        yr = np.round(y / .4) * .4
        rungs = smax(np.sqrt((y - yr) ** 2 + (z - (2.35 - .4 * yr / 6)) ** 2) - .025, np.abs(x) - .25, .01)
        rungs = smax(rungs, np.maximum(-y + .3, y - 5.9), .01)
        return union(d, deck, ladder, rungs, k=.02)

    def tank(x, y, z):
        r = np.sqrt(x * x + z * z)
        a = np.arctan2(z, x)
        body = smax(r - 2.1 + .012 * np.abs(np.cos(a * 36)), np.abs(y - 7.55) - 1.45, .03)
        return body

    def hoops(x, y, z):
        r = np.sqrt(x * x + z * z)
        return groups(*[rect2(r, y, (2.12, h), (.025, .05), .015) for h in (6.5, 7.6, 8.7)])

    def roof(x, y, z):
        r = np.sqrt(x * x + z * z)
        cone = smax((r * 1.3 / 2.4 + (y - 10.3)) / 1.2, -(y - 8.95), .03)
        cone = cone - .012 * np.cos(np.arctan2(z, x) * 24) * (r > .3)
        finial = ellipsoid(x, y, z, (0, 10.35, 0), (.12, .14, .12))
        spout = capsule(x, y, z, (2.0, 6.6, 0), (3.2, 5.2, 0), .13)
        return union(cone, finial, spout, k=.03)
    lo, hi = (-3.4, -.05, -3.0), (3.5, 10.6, 3.0)
    parts = [part(tank, lo, hi, .03, GLOSS, TIMBER, 1400),
             part(frame, lo, hi, .025, GLOSS, (.44, .31, .20), 2600),
             part(hoops, lo, hi, .02, METAL, (.25, .25, .27), 500),
             part(roof, lo, hi, .03, METAL, TIN, 900)]
    return lit(parts, reach=2.0)


def windpump():
    """A steel lattice windpump: three legs, a twelve-blade wheel, a red tail vane and a stock tank."""
    feet = [(-1.1, 0, -1.1), (1.1, 0, -1.1), (0, 0, 1.3)]
    top = (0, 8, 0)

    def tower(x, y, z):
        d = groups(*[capsule(x, y, z, f, top, .075) for f in feet])
        for h in (2, 4, 6):
            pts = [[f[i] + (top[i] - f[i]) * h / 8 for i in range(3)] for f in feet]
            for a, b in zip(pts, pts[1:] + pts[:1]):
                d = np.minimum(d, capsule(x, y, z, a, b, .04))
        rod = capsule(x, y, z, (0, .4, 0), (0, 8, 0), .03)
        head = box(x, y, z, (0, 8.2, .05), (.28, .24, .55), .08)
        arm = capsule(x, y, z, (0, 8.35, 0), (0, 8.35, -1.5), .05)
        return union(d, rod, head, arm, k=.02)

    def wheel(x, y, z):
        r = np.sqrt(x * x + (y - 8.4) ** 2)
        a = np.arctan2(y - 8.4, x)
        step = math.tau / 12
        u = (np.mod(a + .1 * (r - 1), step) - step / 2) * r
        blades = smax(smax(np.abs(u) - .17 * np.clip(r / 1.7, .4, 1), np.abs(z - .75 - .05 * u) - .02, .005),
                      np.maximum(.38 - r, r - 1.7), .02)
        rings = smax(np.minimum(np.abs(r - 1.68) - .035, np.abs(r - .95) - .03), np.abs(z - .75) - .035, .01)
        hub = cylinder_z(x, y, z, (0, 8.4, .78), .22, .12, .05)
        return union(blades, rings, hub, k=.01)

    def vane(x, y, z):
        return box(x, y, z, (0, 8.4, -1.65), (.03, .5, .85), .03)

    def tank(x, y, z):
        r = np.sqrt((x - 2.4) ** 2 + z * z)
        shell = smax(np.abs(r - .95) - .025 + .01 * np.cos(np.arctan2(z, x - 2.4) * 30), np.abs(y - .37) - .37, .01)
        water = smax(r - .93, np.abs(y - .62) - .02, .005)
        return np.minimum(shell, water)
    lo, hi = (-2.0, -.05, -2.7), (3.6, 10.3, 1.6)
    parts = [part(tower, lo, hi, .02, METAL, (.55, .57, .57), 2600),
             part(wheel, lo, hi, .015, METAL, (.70, .72, .71), 2400),
             part(vane, lo, hi, .02, GLOSS, (.68, .17, .10), 120),
             part(tank, lo, hi, .02, METAL, TIN, 700)]
    decals = []
    return lit(parts, decals, reach=1.5)


def ranch_house():
    """The ranch: board walls under a tin roof, a deep front porch, lit windows and a red door."""
    parts = cottage(9, 6, 4, 6, (.56, .41, .27), TIN, wall='boards', roof='tin', trim=(.88, .84, .74),
                    windows=[('+z', -2.6, 2.3, .6, .55), ('+z', 2.6, 2.3, .6, .55), ('-z', 0, 2.3, .6, .55),
                             ('+x', 0, 2.3, .55, .55), ('-x', 0, 2.3, .55, .55)],
                    doors=[('+z', 0, 1.2, 2.3)], chimneys=[(-2.8, -1.2, 6.6, .45)],
                    porch=(2.4, (-4.2, -1.4, 1.4, 4.2)), door_colour=(.62, .19, .12))
    return lit(parts, reach=2.5)


def wagon():
    """A covered wagon: plank bed, spoked wheels, canvas over hoops and a tongue."""
    def bed(x, y, z):
        b = box(x, y, z, (0, 1.1, 0), (.8, .26, 1.7), .04)
        b = subtract(b, box(x, y, z, (0, 1.32, 0), (.7, .2, 1.6), .03), .02)
        b = subtract(b, smax(np.abs(np.mod(y - 1.1, .17) - .085) - .07, -(b + .02), .005), .005)
        tongue = capsule(x, y, z, (0, .9, 1.7), (0, .55, 3.0), .06)
        axles = groups(*[capsule(x, y, z, (-1.0, .7, dz), (1.0, .7, dz), .06) for dz in (-1.2, 1.2)])
        return union(b, tongue, axles, k=.03)

    def wheels(x, y, z):
        d = None
        for dz, rad in ((-1.2, .72), (1.2, .58)):
            for side in (-1, 1):
                cx = side * .95
                r = np.sqrt((y - .72) ** 2 + (z - dz) ** 2)
                a = np.arctan2(y - .72, z - dz)
                rim = smax(np.abs(r - rad + .04) - .04, np.abs(x - cx) - .05, .01)
                step = math.tau / 12
                spokes = smax(smax(np.abs((np.mod(a, step) - step / 2) * r) - .025, r - rad + .05, .005),
                              np.abs(x - cx) - .025, .005)
                hub = cylinder_x(x, y, z, (cx, .72, dz), .1, .09, .03)
                w = union(rim, spokes, hub, k=.01)
                d = w if d is None else np.minimum(d, w)
        return d

    def canvas(x, y, z):
        c = smax(ellipsoid(x, y, z, (0, 1.38, 0), (.92, 1.05, 2.4)), np.abs(z) - 1.45, .08)
        c = smax(c, -(ellipsoid(x, y, z, (0, 1.38, 0), (.86, .99, 2.3))), .02)
        c = smax(c, 1.4 - y, .03)
        return c - .012 * np.cos(z * 2 * math.pi / .7)
    lo, hi = (-1.2, -.05, -2.2), (1.2, 2.6, 3.1)
    parts = [part(bed, lo, hi, .02, GLOSS, TIMBER, 1400),
             part(wheels, lo, hi, .012, GLOSS, (.36, .25, .16), 2400),
             part(canvas, lo, hi, .02, GLOSS, (.90, .86, .74), 1200)]
    return lit(parts, reach=1.2)


# --- Maple Woods -------------------------------------------------------------------------------------
def log_cabin():
    """A log cabin with a shingle roof, a stone chimney, glowing windows and a red door."""
    parts = cottage(7, 5.5, 3.2, 5.4, (.48, .31, .19), (.24, .20, .19), wall='logs', roof='shingle',
                    trim=(.36, .24, .15), windows=[('+z', -1.6, 1.8, .5, .45), ('+z', 1.6, 1.8, .5, .45),
                                                   ('-x', 0, 1.8, .45, .45)],
                    doors=[('+z', 0, 1.0, 2.05)], chimneys=[(2.6, -.6, 6.3, .45)], door_colour=(.66, .20, .12))
    return lit(parts, reach=2.0)


def woodpile():
    """Split logs stacked between two stakes, about 1.6 m long along z."""
    def logs(x, y, z):
        d = None
        for k in range(5):
            cx, cy = -.6 + k * .3, .2 + (k % 2) * .38
            log = smax(np.sqrt((x - cx) ** 2 + (y - cy) ** 2) - .19, np.abs(z - .8) - .8, .02)
            d = log if d is None else np.minimum(d, log)
        return d + .008 * noise(x * 9, y * 9, z * 3, 2)

    lo, hi = (-.9, -.05, -.1), (.9, .9, 1.7)
    p = part(logs, lo, hi, .02, GLOSS, (.48, .32, .19), 700)
    mesh, colour = lit([p], reach=.6)
    end = np.abs(mesh.normals[:, 2]) > .75
    colour[end] = np.array((.82, .66, .44)) * (.6 + .4 * mesh.ao[end])[:, None]
    return mesh, colour


def sawmill():
    """A timber sawmill: board shed under a lean-to tin roof, open bay, a circular saw bench."""
    def shed(x, y, z):
        body = box(x, y, z, (0, 2.5, 0), (6, 2.5, 3.5), .03)
        body = smax(body, (y - 5.6) - .15 * z, .03)
        body = subtract(body, smax(np.abs(np.mod(x, .3) - .15) - .135, -(body + .03), .004), .005)
        bay = box(x, y, z, (-1.0, 1.9, 3.5), (2.6, 1.9, .5), .04)
        return subtract(body, bay, .02)

    def roof(x, y, z):
        line = polyline2(z, y, [(-4.1, 4.95), (4.4, 6.25)])
        return smax(line - .08, np.abs(x) - 6.5, .03) - .014 * np.cos(x * 2 * math.pi / .42)

    def frame(x, y, z):
        posts = groups(*[box(x, y, z, (px, 2.5, 3.55), (.12, 2.5, .12), .02) for px in (-6, -3.6, 1.6, 6)])
        lintel = box(x, y, z, (-1.0, 3.9, 3.55), (2.8, .12, .12), .02)
        inside = box(x, y, z, (-1.0, 1.9, 3.1), (2.5, 1.85, .05), .02)
        bench = box(x, y, z, (7.0, .85, 0), (.8, .08, 1.6), .03)
        legs = groups(*[box(x, y, z, (7.0 + sx * .65, .42, sz * 1.4), (.07, .42, .07), .02) for sx in (-1, 1) for sz in (-1, 1)])
        return union(posts, lintel, bench, legs, inside, k=.01)

    def blade(x, y, z):
        return cylinder_x(x, y, z, (7.0, 1.4, 0), 1.0, .015, .006)

    def dust(x, y, z):
        return ellipsoid(x, y, z, (7.3, 0, 1.9), (.9, .35, .8)) + .02 * noise(x * 6, y * 6, z * 6, 4)
    lo, hi = (-6.8, -.05, -4.6), (8.2, 6.8, 4.8)
    parts = [part(shed, lo, hi, .03, GLOSS, (.50, .36, .24), 2000),
             part(roof, lo, hi, .03, METAL, TIN, 900),
             part(frame, lo, hi, .025, GLOSS, (.40, .28, .18), 1200),
             part(blade, lo, hi, .012, METAL, (.78, .80, .82), 300),
             part(dust, lo, hi, .03, GLOSS, (.84, .70, .46), 200)]
    return lit(parts, reach=2.5)


def log_pile():
    """Three tiers of felled logs, 6 m long along z from z = 0."""
    def logs(x, y, z):
        d = None
        for row in range(3):
            for k in range(4 - row):
                cx, cy = -4 + k * .9 + row * .45, .45 + row * .8
                log = smax(np.sqrt((x - cx) ** 2 + (y - cy) ** 2) - .42, np.abs(z - 3) - 3, .03)
                d = log if d is None else np.minimum(d, log)
        return d + .012 * noise(x * 4, y * 4, z * 1.2, 3)

    lo, hi = (-4.6, -.05, -.1), (-.1, 2.2, 6.1)
    p = part(logs, lo, hi, .03, GLOSS, (.40, .27, .17), 1300)
    mesh, colour = lit([p], reach=1.0)
    end = np.abs(mesh.normals[:, 2]) > .75
    colour[end] = np.array((.86, .70, .48)) * (.6 + .4 * mesh.ao[end])[:, None]
    return mesh, colour


def lookout():
    """A fire lookout: splayed timber legs with cross braces, a glazed cab, a balcony, a pyramid roof."""
    def frame(x, y, z):
        d = None
        corners = [(sx, sz) for sx in (-1, 1) for sz in (-1, 1)]
        for sx, sz in corners:
            leg = capsule(x, y, z, (sx * 2.24, 0, sz * 2.24), (sx * 1.6, 11, sz * 1.6), .15)
            d = leg if d is None else np.minimum(d, leg)
        for h0, h1 in ((0, 3.5), (3.5, 7), (7, 11)):
            k0, k1 = 1.6 + .64 * (1 - h0 / 11), 1.6 + .64 * (1 - h1 / 11)
            for a, b in (((-1, -1), (1, -1)), ((1, -1), (1, 1)), ((1, 1), (-1, 1)), ((-1, 1), (-1, -1))):
                d = np.minimum(d, capsule(x, y, z, (a[0] * k0, h0, a[1] * k0), (b[0] * k1, h1, b[1] * k1), .06))
                d = np.minimum(d, capsule(x, y, z, (a[0] * k1, h1, a[1] * k1), (b[0] * k1, h1, b[1] * k1), .07))
        deck = box(x, y, z, (0, 11.0, 0), (2.4, .1, 2.4), .03)
        rail = smax(np.abs(np.maximum(np.abs(x), np.abs(z)) - 2.35) - .04, np.abs(y - 12.0) - .04, .01)
        balusters = smax(np.abs(np.maximum(np.abs(x), np.abs(z)) - 2.35) - .03,
                         np.minimum(np.abs(np.mod(x, .5) - .25), np.abs(np.mod(z, .5) - .25)) - .025, .005)
        balusters = smax(balusters, np.abs(y - 11.55) - .45, .01)
        return union(d, deck, rail, balusters, k=.01)

    def cab(x, y, z):
        body = box(x, y, z, (0, 12.2, 0), (2.0, 1.2, 2.0), .04)
        hole = smax(np.abs(np.maximum(np.abs(x), np.abs(z)) - 2.0) - .2, np.abs(y - 12.55) - .55, .02)
        along = np.where(np.abs(x) > np.abs(z), z, x)
        posts = .07 - np.abs(np.mod(along + .4, .8) - .4)
        return subtract(body, smax(hole, posts, .01), .01)

    def glass(x, y, z):
        return smax(np.abs(np.maximum(np.abs(x), np.abs(z)) - 1.92) - .02, np.abs(y - 12.55) - .55, .01)

    def roof(x, y, z):
        pyramid = (np.maximum(np.abs(x), np.abs(z)) * (1.8 / 2.6) + (y - 15.2)) / 1.23
        return smax(smax(pyramid, -(y - 13.35), .03), np.maximum(np.abs(x), np.abs(z)) - 2.6, .03)
    lo, hi = (-2.6, -.05, -2.6), (2.6, 15.4, 2.6)
    parts = [part(frame, lo, hi, .025, GLOSS, (.44, .31, .20), 5200),
             part(cab, lo, hi, .03, GLOSS, (.62, .44, .28), 1200),
             part(glass, lo, hi, .03, GLASS, GLOW, 300),
             part(roof, lo, hi, .03, GLOSS, (.24, .20, .19), 900)]
    return lit(parts, reach=2.0)


def pumpkin(variant=0):
    """A ribbed pumpkin about a metre across (scaled per placement), with a curled stem."""
    def body(x, y, z):
        r = np.sqrt(x * x + z * z)
        a = np.arctan2(z, x)
        lobes = 1 - .07 * np.abs(np.sin(a * 4 + variant))
        return ellipsoid(x, y, z, (0, .72, 0), (1.0 * lobes, .72, 1.0 * lobes))

    def stem(x, y, z):
        return capsule(x, y, z, (0, 1.35, 0), (.08, 1.62, .05), .07)
    lo, hi = (-1.1, -.05, -1.1), (1.1, 1.75, 1.1)
    parts = [part(body, lo, hi, .02, GLOSS, (.93, .45, .08), 150),
             part(stem, lo, hi, .015, GLOSS, (.30, .36, .14), 30)]
    return lit(parts, reach=.8)


def scarecrow():
    """A scarecrow: post and crossbar, a plaid shirt, sack head under a straw hat, straw tufts."""
    def wood(x, y, z):
        return np.minimum(capsule(x, y, z, (0, 0, 0), (0, 2.5, 0), .06), capsule(x, y, z, (-1.0, 2.0, 0), (1.0, 2.0, 0), .05))

    def shirt(x, y, z):
        body = box(x, y, z, (0, 1.75, 0), (.34, .42, .16), .12)
        sleeves = capsule(x, y, z, (-.85, 2.0, 0), (.85, 2.0, 0), .12)
        return smin(body, sleeves, .06) + .015 * noise(x * 5, y * 5, z * 5, 3)

    def straw(x, y, z):
        head = ellipsoid(x, y, z, (0, 2.48, 0), (.24, .27, .22))
        tufts = groups(*[ellipsoid(x, y, z, (s * 1.0, 2.0, 0), (.12, .1, .08)) for s in (-1, 1)])
        hem = ellipsoid(x, y, z, (0, 1.32, 0), (.32, .08, .16))
        return union(head, tufts, hem, k=.03) + .02 * noise(x * 14, y * 14, z * 14, 6)

    def hat(x, y, z):
        brim = cylinder_y(x, y, z, (0, 2.7, 0), .46, .025, .02)
        crown = cylinder_y(x, y, z, (0, 2.86, 0), .23, .16, .07)
        return smin(brim, crown, .03)
    lo, hi = (-1.2, -.05, -.4), (1.2, 3.1, .4)
    parts = [part(wood, lo, hi, .015, GLOSS, (.42, .30, .20), 200),
             part(shirt, lo, hi, .015, GLOSS, (.72, .20, .14), 500),
             part(straw, lo, hi, .015, GLOSS, (.84, .70, .36), 500),
             part(hat, lo, hi, .015, GLOSS, (.68, .55, .30), 250)]
    return lit(parts, reach=.8)


# --- Highland Loch -----------------------------------------------------------------------------------
def weathered(mesh, colour, base, seed, moss=None):
    """Mottles a stone colour with low-frequency noise, greening the lowest courses."""
    p = mesh.points
    tone = .84 + .28 * fbm(p[:, 0] * .7, p[:, 1] * .7, p[:, 2] * .7, seed)
    c = colour * tone[:, None]
    if moss is not None:
        damp = np.clip(1 - p[:, 1] / 1.2, 0, 1) * (.5 + .5 * noise(p[:, 0] * 1.5, p[:, 1], p[:, 2] * 1.5, seed + 3))
        c = c * (1 - damp[:, None] * .6) + np.array(moss) * (damp[:, None] * .6) * (c.mean(axis=1, keepdims=True) / np.mean(base))
    return mesh, c


def keep():
    """A granite keep: weathered walls, quoins, string courses, a corbelled crenellated
    parapet, arrow slits, an arched door and a lit window."""
    def walls(x, y, z):
        ax, az = np.abs(x), np.abs(z)
        body = box(x, y, z, (0, 7, 0), (5, 7, 5), .04) + .04 * fbm(x * .8, y * .8, z * .8, 4)
        courses = smax(np.abs(np.maximum(ax, az) - 5.0) - .07, np.minimum(np.abs(y - 4.6), np.abs(y - 9.6)) - .1, .02)
        corners = rect2(ax, az, (5, 5), (.75, .75), .03)
        stones = np.abs(np.mod(y, 1.1) - .55) - .52
        corners = smax(smax(corners, np.maximum(ax, az) - 5.06, .02), np.maximum(stones, y - 13.7), .01)
        parapet = smax(np.abs(np.maximum(ax, az) - 5.05) - .3, np.abs(y - 14.6) - .6, .02)
        crenel = smax(np.abs(np.mod(np.where(ax > az, z, x) + .45, 1.8) - .9) - .45, y - 14.85, .02)
        parapet = subtract(parapet, crenel, .02)
        corbels = smax(np.abs(np.maximum(ax, az) - 5.1) - .2, np.abs(y - 13.85) - .15, .02)
        corbels = smax(corbels, np.abs(np.mod(np.where(ax > az, z, x) + .3, .6) - .3) - .2, .02)
        body = union(body, courses, corners, parapet, corbels, k=.02)
        slits = None
        for k in range(4):
            a = k * math.pi / 2
            n = x * math.sin(a) + z * math.cos(a)
            t = x * math.cos(a) - z * math.sin(a)
            s_ = smax(rect2(t, y, (0, 7.2), (.18, .9), .05), np.abs(n - 5) - .4, .02)
            slits = s_ if slits is None else np.minimum(slits, s_)
        door = smax(np.minimum(rect2(x, y, (0, 1.2), (.9, 1.2), .02), np.sqrt(x * x + (y - 2.4) ** 2) - .9),
                    np.abs(z - 5) - .4, .02)
        window = smax(np.minimum(rect2(x, y, (0, 10.8), (.7, .8), .02), np.sqrt(x * x + (y - 11.6) ** 2) - .7),
                      np.abs(z - 5) - .4, .02)
        return subtract(body, groups(slits, door, window), .02)

    def shadow(x, y, z):
        door = smax(np.minimum(rect2(x, y, (0, 1.2), (.9, 1.2), .02), np.sqrt(x * x + (y - 2.4) ** 2) - .9),
                    np.abs(z - 4.7) - .04, .01)
        slits = groups(*[smax(rect2(x * math.cos(k * math.pi / 2) - z * math.sin(k * math.pi / 2), y, (0, 7.2), (.2, .92), .05),
                              np.abs(x * math.sin(k * math.pi / 2) + z * math.cos(k * math.pi / 2) - 4.75) - .04, .01)
                         for k in range(4)])
        return np.minimum(door, slits)

    def window(x, y, z):
        return smax(np.minimum(rect2(x, y, (0, 10.8), (.72, .82), .02), np.sqrt(x * x + (y - 11.6) ** 2) - .72),
                    np.abs(z - 4.72) - .04, .01)
    lo, hi = (-5.6, -.05, -5.6), (5.6, 15.4, 5.6)
    parts = [part(walls, lo, hi, .04, GLOSS, GRANITE, 6500),
             part(shadow, lo, hi, .03, GLOSS, (.12, .12, .13), 300),
             part(window, lo, hi, .03, GLOSS, GLOW, 120)]
    mesh, colour = lit(parts, reach=3.0)
    glow = np.all(np.abs(colour - np.array(GLOW)) < 1e-6, axis=1)
    mesh, mottled = weathered(mesh, colour, GRANITE, 4, (.30, .36, .20))
    mottled[glow] = GLOW
    return mesh, mottled


def tower(height):
    """A round corner tower from y = -.6 (into sloping ground) to height, crenellated."""
    def stone(x, y, z):
        r = np.sqrt(x * x + z * z)
        a = np.arctan2(z, x)
        body = smax(r - 2.6, np.maximum(-y - .6, y - height), .04) + .04 * fbm(x * .8, y * .8, z * .8, 6)
        course = smax(np.abs(r - 2.62) - .08, np.abs(y - height * .55) - .1, .02)
        top = smax(np.abs(r - 2.75) - .3, np.abs(y - height - .35) - .45, .02)
        crenel = smax(np.abs(np.mod(a * 2.75 + .45, 1.8) - .9) - .45, y - height - .55, .02)
        top = subtract(top, crenel, .02)
        corbels = smax(smax(np.abs(r - 2.7) - .15, np.abs(y - height + .25) - .15, .02),
                       np.abs(np.mod(a * 2.7 + .3, .6) - .3) - .2, .02)
        slit = smax(rect2((a - .8) * 2.6, y, (0, height * .6), (.15, .7), .04), np.abs(r - 2.6) - .3, .02)
        return subtract(union(body, course, top, corbels, k=.03), slit, .02)
    lo, hi = (-3.2, -.7, -3.2), (3.2, height + 1.0, 3.2)
    mesh, colour = lit([part(stone, lo, hi, .04, GLOSS, GRANITE, 2600)], reach=2.5)
    return weathered(mesh, colour, GRANITE, 6, (.30, .36, .20))


def curtain(height, length=3.97):
    """A section of curtain wall along x, from y = -.4; the full height carries merlons,
    the gate's low section is broken off."""
    def stone(x, y, z):
        top = height - .4
        body = box(x, y, z, (0, top / 2 - .2, 0), (length / 2, top / 2 + .2, .7), .03)
        body = body + .04 * fbm(x * .8, y * .8, z * .8, 7)
        if height > 4:
            merlons = box(x, y, z, (0, top + .3, 0), (length / 2, .3, .62), .03)
            crenel = smax(np.abs(np.mod(x + .45, 1.8) - .9) - .45, y - top - .1, .02)
            body = union(body, subtract(merlons, crenel, .02), k=.02)
        else:
            body = body + .12 * fbm(x * .9, y * .9, z * .9, 3) * (y > top - .6)
        return body
    lo, hi = (-length / 2 - .1, -.5, -.9), (length / 2 + .1, height + .5, .9)
    mesh, colour = lit([part(stone, lo, hi, .04, GLOSS, GRANITE, 360)], reach=2.0)
    return weathered(mesh, colour, GRANITE, 7, (.30, .36, .20))


def croft(thatched):
    """A whitewashed croft: long and low, chimneys at the gable ends, thatch or slate,
    a blue door between two lit windows."""
    parts = cottage(8, 5, 2.8, 5.0, (.92, .91, .86), (.62, .50, .30) if thatched else (.24, .26, .29), ridge_x=True,
                    wall='plaster', roof='thatch' if thatched else 'slate', trim=(.92, .91, .86),
                    windows=[('+z', -1.7, 1.6, .5, .45), ('+z', 1.7, 1.6, .5, .45)],
                    doors=[('+z', 0, 1.0, 1.9)], chimneys=[(-3.8, 0, 5.4 if thatched else 5.2, .42),
                                                         (3.8, 0, 5.4 if thatched else 5.2, .42)],
                    door_colour=(.18, .30, .52), overhang=.3 if not thatched else .45, budget=.6)
    return lit(parts, reach=2.0)


def standing_stone(variant):
    """A weathered menhir 3 m tall from y = -.2, lichen on its weather side."""
    rng = np.random.default_rng(600 + variant)
    lean = rng.uniform(-.08, .08)

    def stone(x, y, z):
        w = .55 - .1 * np.clip((y - 1.0) / 2, 0, 1)
        d = rect2(x - lean * y, z, (0, 0), (w, .3), .1)
        d = smax(d, np.maximum(-y - .2, y - 2.8 - .25 * np.cos(x * 2.5 + variant)), .12)
        return d + .04 * fbm(x * 2.2, y * 2.2, z * 2.2, variant)

    lo, hi = (-.9, -.3, -.6), (.9, 3.2, .6)
    p = part(stone, lo, hi, .02, GLOSS, STONE, 260)
    mesh, col = lit([p], reach=1.0)
    lichen = (mesh.normals[:, 2] > .3) & (noise(mesh.points[:, 0] * 3, mesh.points[:, 1] * 3, mesh.points[:, 2] * 3, variant) > .05)
    col[lichen] = np.array((.62, .62, .38)) * (.5 + .5 * mesh.ao[lichen])[:, None]
    return mesh, col


def altar():
    def slab(x, y, z):
        return box(x, y, z, (0, .3, 0), (1.3, .3, .6), .1) + .03 * fbm(x * 2, y * 2, z * 2, 8)
    return lit([part(slab, (-1.6, -.05, -.9), (1.6, .8, .9), .02, GLOSS, STONE, 200)], reach=.8)


def phone_box():
    """A red telephone kiosk: domed roof, glazed sides with glazing bars, a lit sign, from y = 0."""
    def around(x, z):
        ax, az = np.abs(x), np.abs(z)
        return np.maximum(ax, az), np.where(ax > az, z, x)

    def kiosk(x, y, z):
        n, t = around(x, z)
        body = box(x, y, z, (0, 1.25, 0), (.5, 1.25, .5), .04)
        rim = smax(np.abs(rect2(t, y, (0, 1.35), (.37, .86), .04)) - .035, np.abs(n - .5) - .025, .01)
        crown = box(x, y, z, (0, 2.62, 0), (.55, .12, .55), .05)
        dome = smax(ellipsoid(x, y, z, (0, 2.62, 0), (.58, .3, .58)), n - .5, .05)
        plinth = box(x, y, z, (0, .06, 0), (.56, .06, .56), .02)
        return union(body, rim, crown, dome, plinth, k=.02)

    def sign(x, y, z):
        n, t = around(x, z)
        return smax(rect2(t, y, (0, 2.33), (.3, .07), .02), np.abs(n - .505) - .015, .005)
    lo, hi = (-.7, -.05, -.7), (.7, 3.0, .7)
    parts = [part(kiosk, lo, hi, .012, GLOSS, (.80, .10, .08), 1400),
             part(sign, lo, hi, .01, GLOSS, GLOW, 80)]
    # Panes printed onto the flat sides between the glazing bars: three columns, six rows.
    decals = []
    for axis, sign_ in ((0, 1), (0, -1), (2, 1), (2, -1)):
        face = lambda x, y, z, a=axis, s_=sign_: .44 - s_ * (x if a == 0 else z)
        along = lambda x, y, z, a=axis: z if a == 0 else x
        for c in range(3):
            t0, t1 = -.33 + c * .22 + .018, -.33 + (c + 1) * .22 - .018
            for r in range(6):
                y0, y1 = .54 + r * .27 + .018, .54 + (r + 1) * .27 - .018
                decals.append(([face, lambda x, y, z, v=t0, f=along: v - f(x, y, z),
                                lambda x, y, z, v=t1, f=along: f(x, y, z) - v,
                                lambda x, y, z, v=y0: v - y, lambda x, y, z, v=y1: y - v], GLASS, DARK_GLASS))
    return lit(parts, decals, reach=.8)


def drystone(length=3.2, height=1.0, thickness=.7):
    """A dry-stone wall section along x, from y = 0, with a row of upright coping stones."""
    def stones(x, y, z):
        body = box(x, y, z, (0, height / 2 - .05, 0), (length / 2, height / 2 - .05, thickness / 2 - .06 * y), .06)
        body = body + .05 * fbm(x * 2.2, y * 2.2, z * 2.2, 11)
        cope = smax(np.abs(np.mod(x + .2, .4) - .2) - .16, np.abs(y - height) - .12, .03)
        cope = smax(cope, np.abs(z) - thickness / 2 + .08, .02)
        cope = smax(cope, np.abs(x) - length / 2, .02)
        return smin(body, cope, .03)
    lo, hi = (-length / 2 - .1, -.05, -thickness / 2 - .1), (length / 2 + .1, height + .2, thickness / 2 + .1)
    p = part(stones, lo, hi, .03, GLOSS, STONE, 90)
    mesh, col = lit([p], reach=.6)
    tone = .85 + .3 * fbm(mesh.points[:, 0] * 1.8, mesh.points[:, 1] * 1.8, mesh.points[:, 2] * 1.8, 21)
    return mesh, col * tone[:, None]


# --- Volcano Peak ------------------------------------------------------------------------------------
BASALT = (.17, .16, .16)


def basalt_column(variant):
    """A hexagonal basalt prism 4 m tall from y = 0 (scaled per placement), its top tilted."""
    def prism(x, y, z):
        a = np.arctan2(z, x) + variant * .3
        r = np.sqrt(x * x + z * z)
        hexa = r * np.cos(np.mod(a + math.pi / 6, math.pi / 3) - math.pi / 6) - .74
        top = y - 4 + .12 * x * math.cos(variant) + .1 * z * math.sin(variant)
        return smax(hexa, np.maximum(-y, top), .035)
    lo, hi = (-.9, -.05, -.9), (.9, 4.3, .9)
    p = part(prism, lo, hi, .02, GLOSS, BASALT, 90)
    mesh, col = lit([p], reach=.6)
    pts = mesh.points
    return mesh, col * (.85 + .3 * noise(pts[:, 0] * 2, pts[:, 1] * .7, pts[:, 2] * 2, variant))[:, None]


def observatory():
    """A white observatory: panelled drum, ribbed dome with an open slit, door and a lit lamp."""
    def drum(x, y, z):
        r = np.sqrt(x * x + z * z)
        a = np.arctan2(z, x)
        d = smax(r - 4.0, np.abs(y - 2.25) - 2.25, .04)
        door = box(x, y, z, (0, 1.2, 4.0), (.62, 1.2, .25), .03)
        return subtract(d, door, .02)

    def dome(x, y, z):
        d = smax(ellipsoid(x, y, z, (0, 4.5, 0), (4.1, 3.6, 4.1)), 4.4 - y, .05)
        slit = smax(np.abs(x) - .6, -z, .05)
        return subtract(d, smax(slit, 6.0 - y, .05), .05)

    def metal(x, y, z):
        inside = smax(smax(np.abs(x) - .55, np.abs(ellipsoid(x, y, z, (0, 4.5, 0), (3.95, 3.45, 3.95))) - .04, .02),
                      6.0 - y, .03)
        scope = capsule(x, y, z, (0, 5.0, 0), (0, 7.2, 2.3), .35)
        door = box(x, y, z, (0, 1.2, 3.93), (.6, 1.2, .05), .02)
        return union(inside, scope, door, k=.02)

    def lamp(x, y, z):
        return box(x, y, z, (0, 3.2, 4.05), (.25, .15, .06), .03)
    lo, hi = (-4.4, -.05, -4.4), (4.4, 8.4, 4.4)
    parts = [part(drum, lo, hi, .035, GLOSS, (.90, .91, .92), 1600),
             part(dome, lo, hi, .035, GLOSS, (.88, .89, .90), 2200),
             part(metal, lo, hi, .03, METAL, (.45, .48, .50), 1200),
             part(lamp, lo, hi, .02, GLOSS, GLOW, 60)]
    return lit(parts, reach=3.0)


def research_hut():
    """A prefabricated orange hut 5 x 3 x 4 m with ribbed walls, two lit windows and a door."""
    parts = cottage(5, 4, 3.0, 3.35, (.86, .40, .12), (.55, .57, .58), wall='ribbed', roof='tin',
                    trim=(.90, .90, .88), windows=[('+z', -1.1, 1.8, .45, .4), ('+z', 1.1, 1.8, .45, .4)],
                    doors=[('-z', 0, .9, 2.0)], footing=(.35, .35, .36), overhang=.25, door_colour=(.30, .32, .34))
    return lit(parts, reach=1.5)


def mast():
    """A lattice radio mast 9 m tall with a lit beacon."""
    def lattice(x, y, z):
        legs = groups(*[capsule(x, y, z, (sx * .35, 0, sz * .35), (sx * .12, 9, sz * .12), .035)
                        for sx in (-1, 1) for sz in (-1, 1)])
        k = .35 - .23 * np.clip(y / 9, 0, 1)
        yr = np.clip(np.round(y / .9) * .9, .9, 8.1)
        along_x = smax(np.sqrt((y - yr) ** 2 + (np.abs(z) - k) ** 2) - .022, np.abs(x) - k, .005)
        along_z = smax(np.sqrt((y - yr) ** 2 + (np.abs(x) - k) ** 2) - .022, np.abs(z) - k, .005)
        legs = np.minimum(legs, np.minimum(along_x, along_z))
        dish = smax(ellipsoid(x, y, z, (.3, 7.0, 0), (.08, .45, .45)), -(x - .27), .02)
        return union(legs, dish, k=.02)

    def beacon(x, y, z):
        return ellipsoid(x, y, z, (0, 9.2, 0), (.16, .18, .16))
    lo, hi = (-.6, -.05, -.6), (.6, 9.5, .6)
    parts = [part(lattice, lo, hi, .015, METAL, (.70, .70, .72), 1400),
             part(beacon, lo, hi, .015, GLOSS, GLOW, 60)]
    return lit(parts, reach=1.0)


def warning_sign():
    """A triangular volcano warning sign, both faces, on a post; faces along z."""
    tri = [(-.72, 1.78), (.72, 1.78), (0, 3.02)]

    def board(x, y, z):
        t = Profile(rounded_polygon(tri, .06))
        return smax(t(x, y), np.abs(z) - .025, .01)

    def post(x, y, z):
        return capsule(x, y, z, (0, 0, -.06), (0, 2.6, -.06), .055)
    lo, hi = (-.9, -.05, -.2), (.9, 3.2, .2)
    parts = [part(board, lo, hi, .01, GLOSS, (.95, .78, .10), 300),
             part(post, lo, hi, .012, METAL, (.55, .57, .58), 80)]
    inner = [(-.52, 1.9), (.52, 1.9), (0, 2.8)]
    decals = []
    for s in (1, -1):
        face = lambda x, y, z, s=s: -s * z
        border_out = convex((0, 1), [(-.66, 1.81), (.66, 1.81), (0, 2.95)]) + [face]
        decals.append((border_out, GLOSS, (.82, .10, .06), .0015))
        decals.append((convex((0, 1), inner) + [face], GLOSS, (.95, .78, .10), .003))
        decals.append((convex((0, 1), [(-.05, 2.17), (.05, 2.17), (.07, 2.62), (-.07, 2.62)]) + [face], GLOSS, BLACK, .0045))
        decals.append((convex((0, 1), [(-.06, 2.0), (.06, 2.0), (.06, 2.1), (-.06, 2.1)]) + [face], GLOSS, BLACK, .0045))
    return lit(parts, decals, reach=.8)


def fumarole():
    """A steaming vent: a low mound of rock with a crater, crusted in yellow sulphur."""
    def mound(x, y, z):
        r = np.sqrt(x * x + z * z)
        d = ellipsoid(x, y, z, (0, -.25, 0), (2.4, .85, 2.4))
        crater = ellipsoid(x, y, z, (0, .55, 0), (.9, .6, .9))
        d = subtract(d, crater, .25)
        return d + .12 * fbm(x * 1.6, y * 1.6, z * 1.6, 13) + .04 * noise(x * 5, y * 5, z * 5, 7)
    lo, hi = (-2.8, -1.2, -2.8), (2.8, 1.0, 2.8)
    p = part(mound, lo, hi, .03, GLOSS, BASALT, 900)
    mesh, col = lit([p], reach=1.5)
    pts = mesh.points
    r = np.sqrt(pts[:, 0] ** 2 + pts[:, 2] ** 2)
    sulphur = (r < 1.5) & (fbm(pts[:, 0] * 1.4, pts[:, 1] * 1.4, pts[:, 2] * 1.4, 2) > -.15)
    col[sulphur] = np.array((.86, .78, .22)) * (.55 + .45 * mesh.ao[sulphur])[:, None]
    return mesh, col


# Preview helpers: argument-free builders.
def tower_tall():
    return tower(10.0)


def curtain_full():
    return curtain(6.0)


def croft_thatch():
    return croft(True)


def croft_slate():
    return croft(False)


def stone0():
    return standing_stone(0)


def column0():
    return basalt_column(0)


def corral_rail():
    """A weathered split rail one metre long along +x from the origin; scaled to each span."""
    def rail(x, y, z):
        d = smax(ellipsoid(x, y, z, (.5, 0, 0), (.6, .07, .055)), np.abs(x - .5) - .5, .01)
        return d + .006 * np.sin(x * 37) * np.sin(z * 50)
    return lit([part(rail, (-.05, -.1, -.1), (1.05, .1, .1), .008, GLOSS, (.52, .40, .28), 100)], reach=.2)


# --- Placement ---------------------------------------------------------------------------------------
def put(name, key, build, x, y, z, yaw=0., scale=None):
    """A linked duplicate of a model; scale is uniform or game-axis (x, y, z)."""
    import roadside
    o = roadside.place(name, ('wild',) + tuple(key), build, x, y, z, yaw)
    if scale is not None:
        o.scale = (scale, scale, scale) if np.isscalar(scale) else (scale[0], scale[2], scale[1])
    return o


def span(name, key, build, p, q):
    """A one-metre model along +x stretched and tilted from point p to point q."""
    import roadside
    dx, dy, dz = (q[i] - p[i] for i in range(3))
    level = math.hypot(dx, dz)
    o = roadside.place(name, ('wild',) + tuple(key), build, *p)
    o.scale = (math.hypot(level, dy), 1, 1)
    o.rotation_euler = (0, -math.atan2(dy, level), math.atan2(-dz, dx))
    return o


def hay_bale():
    import landmarks
    return landmarks.hay_bale()


def fence_post():
    import landmarks
    return landmarks.fence_post()
