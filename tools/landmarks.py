"""Buildings and dressing of the first three circuits, modeled like the cars.

Placed as linked duplicates through tools/roadside.py; every model stands on
y = 0 with its front (doors, faces) towards +z unless noted.
"""
import math

import numpy as np

import sdf
from sdf import smin, smax, union, subtract, box, rect2, circle2, ellipsoid, capsule
from sdf import cylinder_x, cylinder_y, cylinder_z, polyline2, rounded_polygon, Profile
from cars import part, convex, strip, GLOSS, TRIM, METAL, GLASS, RUBBER
from roadside import assemble, WHITE, BLACK

BARN_RED = (.66, .17, .11)
CHARCOAL = (.17, .20, .22)
STONE = (.46, .45, .42)
CEDAR = (.45, .29, .18)
STRAW = (.80, .62, .29)
SNOW = (.90, .93, .95)


def boards(u, pitch=.3, width=.012):
    """Grooves between vertical boards along coordinate u."""
    return np.abs(np.mod(u, pitch) - pitch / 2) - (pitch / 2 - width)


def gable_house(width, length, eaves, ridge, walls, roof_colour, trim=WHITE, logs=False, snow=None,
                door=(3.2, 4.0), loft=True, cupola=True):
    """A gabled building: board or log walls on a stone footing, an overhanging roof
    with standing seams, corner boards, a braced front door, windows and a cupola."""
    hw, hl = width / 2, length / 2
    gable = Profile(rounded_polygon([(-hw, .5), (hw, .5), (hw, eaves), (0, ridge), (-hw, eaves)], .03))
    over = .85
    slope = [(-hw - over, eaves - over * (ridge - eaves) / hw), (0, ridge + .02),
             (hw + over, eaves - over * (ridge - eaves) / hw)]

    def shell(x, y, z):
        body = smax(gable(x, y), np.abs(z) - hl, .03)
        if logs:
            # Round logs: horizontal bulges on every face.
            ripple = .05 * (np.abs(np.mod(y, .32) - .16) / .16) - .025
            body = body + ripple * (y < eaves + .1)
        else:
            u = np.where(np.abs(np.abs(z) - hl) < .05, x, z)
            body = subtract(body, smax(boards(u), -(body + .03), .004), .006)
        return body

    def footing(x, y, z):
        return box(x, y, z, (0, .28, 0), (hw + .12, .3, hl + .12), .06)

    def roof(x, y, z):
        slab = polyline2(x, y, slope) - .11
        slab = smax(slab, np.abs(z) - hl - .5, .04)
        # Corrugated sheet: a gentle continuous ripple across the slope.
        slab = slab - .018 * np.cos(z * 2 * math.pi / .45)
        if cupola:
            c = box(x, y, z, (0, ridge + .55, 0), (.65, .55, .65), .04)
            cap = smax(polyline2(x, y, [(-1.0, ridge + .9), (0, ridge + 1.55), (1.0, ridge + .9)]) - .08,
                       np.abs(z) - 1.0, .02)
            slab = union(slab, c, cap, k=.02)
        return slab

    def trims(x, y, z):
        ax, az = np.abs(x), np.abs(z)
        corners = smax(rect2(ax, az, (hw, hl), (.14, .14), .02), np.abs(y - (eaves + .5) / 2 - .25) - (eaves - .5) / 2, .02)
        fascia = smax(polyline2(x, y, slope) - .16, np.abs(az - hl - .5) - .05, .01)
        dw, dh = door
        frame = smax(np.abs(rect2(x, y, (0, .5 + dh / 2), (dw / 2, dh / 2), .02)) - .09, np.abs(z - hl - .03) - .06, .01)
        # Z-braces across both leaves of the door.
        braces = None
        for side in (-1, 1):
            leaf = [(side * .05, .65), (side * dw / 2 - side * .1, .65 + dh - .3)]
            b = smax(polyline2(np.abs(x) * side, y, leaf) - .07, np.abs(z - hl - .03) - .05, .01)
            braces = b if braces is None else np.minimum(braces, b)
        windows = None
        for wx in (-hw * .62, hw * .62):
            w = smax(np.abs(rect2(x, y, (wx, 2.6), (.55, .6), .02)) - .07, np.abs(z - hl - .03) - .06, .01)
            windows = w if windows is None else np.minimum(windows, w)
        d = union(corners, fascia, frame, braces, windows)
        if loft:
            d = np.minimum(d, smax(np.abs(rect2(x, y, (0, eaves + .9), (.75, .65), .02)) - .08,
                                   np.abs(z - hl - .03) - .06, .01))
        return d

    def glass(x, y, z):
        panes = None
        for wx in (-hw * .62, hw * .62):
            p = smax(rect2(x, y, (wx, 2.6), (.5, .55), .02), np.abs(z - hl) - .04, .01)
            panes = p if panes is None else np.minimum(panes, p)
        return panes

    lo = (-hw - 1.2, -.05, -hl - 1)
    hi = (hw + 1.2, ridge + 1.8, hl + 1)
    parts = [part(shell, lo, hi, .03, GLOSS, walls, 2400),
             part(roof, lo, hi, .03, GLOSS, roof_colour, 1800),
             part(trims, lo, hi, .025, GLOSS, trim, 2600),
             part(footing, lo, hi, .03, GLOSS, STONE, 400),
             part(glass, lo, hi, .025, GLASS, (.10, .16, .20), 200)]
    decals = []
    if snow is not None:
        # Snow lies on the roof: a lumpy cap following the slope.
        def cap(x, y, z):
            top = polyline2(x, y, [(p[0], p[1] + .18) for p in slope]) - .11
            lumps = .05 * np.sin(z * 2.1) * np.sin(x * 1.7)
            return smax(smax(top + lumps, np.abs(z) - hl - .45, .05), -(polyline2(x, y, slope) - .05), .02)
        parts.append(part(cap, lo, hi, .03, GLOSS, snow, 1500))
    return parts, decals


def barn():
    parts, decals = gable_house(8, 11, 6.0, 8.6, BARN_RED, CHARCOAL)

    def silo(x, y, z):
        r = np.sqrt((x - 6) ** 2 + (z + 2) ** 2)
        body = smax(r - 2, np.abs(y - 4) - 4, .05)
        dome = ellipsoid(x, y, z, (6, 8, -2), (2.05, 1.3, 2.05))
        return smin(body, dome, .05)
    lo, hi = (3.6, -.05, -4.4), (8.4, 9.6, .4)
    parts.append(part(silo, lo, hi, .03, METAL, (.58, .62, .60), 1600))
    return assemble(parts, decals, reach=2.5)


def chalet():
    parts, decals = gable_house(8, 11, 5.6, 8.4, CEDAR, (.28, .22, .18), trim=(.92, .90, .84), logs=True,
                                snow=SNOW, cupola=False)
    return assemble(parts, decals, reach=2.5)


def hay_bale():
    """A round bale on its side, axis along x: wound straw under a loose net."""
    def bale(x, y, z):
        r = np.sqrt((y - .95) ** 2 + z * z)
        d = rect2(x, r, (0, 0), (.7, .95), .14)
        a = np.arctan2(y - .95, z)
        d = d + .012 * np.sin(r * 40 + a * 2) * (r > .2)
        return d
    p = part(bale, (-.85, -.05, -1.1), (.85, 2.0, 1.1), .02, GLOSS, STRAW, 700)
    bands = [([lambda x, y, z, c=c: np.abs(x - c) - .03], GLOSS, (.86, .78, .55), .004) for c in (-.45, 0, .45)]
    return assemble([p], [], reach=.8)


def stook():
    """Wheat sheaves leaning together, tied at the waist, ears spreading on top."""
    def sheaves(x, y, z):
        d = None
        for k in range(5):
            a = k * math.tau / 5
            foot = (math.cos(a) * .32, 0, math.sin(a) * .32)
            s = smin(capsule(x, y, z, foot, (math.cos(a) * .05, .75, math.sin(a) * .05), .1),
                     ellipsoid(x, y, z, (math.cos(a) * .12, 1.0, math.sin(a) * .12), (.14, .2, .14)), .1)
            d = s if d is None else smin(d, s, .05)
        return d + .015 * np.sin(np.arctan2(z, x) * 23) * (y < .8)
    p = part(sheaves, (-.6, -.05, -.6), (.6, 1.3, .6), .015, GLOSS, (.82, .66, .26), 360)
    return assemble([p], [], reach=.6)


def snowman():
    """Three snowballs, coal eyes and buttons, a carrot nose, a scarf, stick arms and a top hat."""
    def snow(x, y, z):
        return union(ellipsoid(x, y, z, (0, .75, 0), (.85, .78, .85)),
                     ellipsoid(x, y, z, (0, 1.85, 0), (.62, .58, .62)),
                     ellipsoid(x, y, z, (0, 2.7, 0), (.43, .42, .43)), k=.12)

    def coal(x, y, z):
        bits = [(-.14, 2.82, .36), (.14, 2.82, .36)] + [(0, 1.85 + d, .6 - abs(d) * .3) for d in (-.25, 0, .25)]
        return union(*[ellipsoid(x, y, z, b, (.05, .05, .04)) for b in bits])

    def carrot(x, y, z):
        return capsule(x, y, z, (0, 2.68, .38), (0, 2.66, .72), .055) + .04 * np.clip((z - .38) / .34, 0, 1)

    def scarf(x, y, z):
        ring = sdf.torus_x(y, x, z, (2.3, 0, 0), .5, .09) if False else None
        band = np.sqrt(np.sqrt(x * x + z * z) ** 2) - .5
        d = smax(np.abs(band) - .08, np.abs(y - 2.32) - .09, .04)
        tail = box(x, y, z, (.28, 2.0, .38), (.08, .3, .025), .02)
        return smin(d, tail, .03)

    def hat(x, y, z):
        return smin(cylinder_y(x, y, z, (0, 3.18, 0), .42, .025, .015), cylinder_y(x, y, z, (0, 3.42, 0), .27, .25, .02), .01)

    def arms(x, y, z):
        return np.minimum(capsule(x, y, z, (-.5, 1.95, 0), (-1.2, 2.35, .05), .03),
                          capsule(x, y, z, (.5, 1.95, 0), (1.15, 2.4, -.05), .03))
    lo, hi = (-1.3, -.05, -.95), (1.3, 3.75, .95)
    parts = [part(snow, lo, hi, .02, GLOSS, SNOW, 600),
             part(coal, lo, hi, .01, GLOSS, BLACK, 200),
             part(carrot, lo, hi, .01, GLOSS, (.95, .45, .08), 120),
             part(scarf, lo, hi, .015, GLOSS, (.80, .14, .12), 400),
             part(hat, lo, hi, .015, GLOSS, BLACK, 300),
             part(arms, lo, hi, .012, GLOSS, (.30, .20, .12), 200)]
    return assemble(parts, [], reach=1.0)


def parasol(colour):
    """A beach parasol: eight panels alternating colour and white, a scalloped valance."""
    def canopy(x, y, z):
        r = np.sqrt(x * x + z * z)
        a = np.arctan2(z, x)
        sag = .07 * np.cos(8 * a) * (r / 3.5)
        shell = np.abs(y - (4.85 - .95 * r / 3.5 + sag)) - .04
        return smax(shell, r - 3.5 - .06 * np.cos(16 * a), .03)

    def sector(on):
        def f(x, y, z):
            a = np.arctan2(z, x)
            s = np.sin(4 * a) * (1 if on else -1)
            return smax(canopy(x, y, z), -s * 2.0, .002)
        return f

    def pole(x, y, z):
        return union(cylinder_y(x, y, z, (0, 2.45, 0), .05, 2.45, .02),
                     ellipsoid(x, y, z, (0, 4.9, 0), (.1, .1, .1)), k=.02)
    lo, hi = (-3.7, -.05, -3.7), (3.7, 5.1, 3.7)
    parts = [part(sector(True), lo, hi, .03, GLOSS, colour, 900),
             part(sector(False), lo, hi, .03, GLOSS, WHITE, 900),
             part(pole, lo, hi, .02, GLOSS, (.86, .84, .78), 150)]
    return assemble(parts, [], reach=1.5)


def lounger():
    """A slatted wooden sun lounger with a raised back, about 2.8 m long along z."""
    def frame(x, y, z):
        seat = smax(box(x, y, z, (0, .42, .3), (.65, .04, .95), .02), np.abs(np.mod(z, .16) - .08) - .065, .005)
        back = smax(box(x, y, z, (0, .75, -.95), (.65, .04, .5), .02), np.abs(np.mod(z, .16) - .08) - .065, .005)
        c, s = math.cos(.7), math.sin(.7)
        back = smax(rect2(x, (z + .95) * c + (y - .75) * s, (0, 0), (.65, .5), .02),
                    np.abs(-(z + .95) * s + (y - .75) * c) - .04, .01)
        legs = None
        for lx in (-.58, .58):
            for lz in (-1.2, 1.15):
                l = box(x, y, z, (lx, .2, lz), (.045, .22, .045), .015)
                legs = l if legs is None else np.minimum(legs, l)
        rails = np.minimum(box(x, y, z, (.6, .36, 0), (.04, .05, 1.3), .015), box(x, y, z, (-.6, .36, 0), (.04, .05, 1.3), .015))
        return union(seat, back, legs, rails)
    p = part(frame, (-.8, -.05, -1.6), (.8, 1.3, 1.5), .012, GLOSS, (.60, .40, .22), 900)
    return assemble([p], [], reach=.6)


def towel(colour):
    def t(x, y, z):
        return box(x, y, z, (0, .02, 0), (.85, .018, 1.75), .015)
    p = part(t, (-1, -.05, -1.9), (1, .1, 1.9), .015, GLOSS, WHITE, 120)
    stripes = [([lambda x, y, z, c=c: np.abs(x - c) - .14], GLOSS, colour) for c in (-.5, 0, .5)]
    return assemble([p], stripes, reach=.3)


def beach_hut(colour=(.22, .58, .57)):
    """A lifeguard hut on stilts: plank walls, white trim, a pitched roof, steps and a flag."""
    hw, hl = 3.0, 2.5

    def walls(x, y, z):
        body = box(x, y, z, (0, 2.85, 0), (hw, 1.85, hl), .04)
        planks = np.abs(np.mod(y, .28) - .14) - .125
        body = subtract(body, smax(planks, -(body + .025), .004), .005)
        door = smax(rect2(x, y, (-1.3, 2.55), (.55, 1.0), .03), np.abs(z - hl) - .06, .01)
        win = smax(rect2(x, y, (1.0, 3.2), (.75, .55), .03), np.abs(z - hl) - .06, .01)
        return subtract(body, np.minimum(door, win), .01)

    def deck(x, y, z):
        floor = box(x, y, z, (0, .95, .5), (hw + .4, .07, hl + .9), .03)
        posts = None
        for px in (-hw, hw):
            for pz in (-hl, hl + .8):
                p = box(x, y, z, (px, .5, pz), (.1, .5, .1), .02)
                posts = p if posts is None else np.minimum(posts, p)
        steps = None
        for k in range(4):
            s = box(x, y, z, (-1.3, .2 + k * .24, hl + 1.5 + .35 * (3 - k)), (.6, .035, .17), .015)
            steps = s if steps is None else np.minimum(steps, s)
        rail = smax(polyline2(np.abs(x), y, [(hw + .35, 1.0), (hw + .35, 1.95)]) - .04, np.abs(z - .5) - hl - .9, .01)
        return union(floor, posts, steps, rail)

    def roof(x, y, z):
        slope = [(-hw - .6, 4.6), (0, 5.8), (hw + .6, 4.6)]
        return smax(polyline2(x, y, slope) - .1, np.abs(z) - hl - .5, .03)

    def trim(x, y, z):
        frame = smax(np.abs(rect2(x, y, (1.0, 3.2), (.75, .55), .03)) - .07, np.abs(z - hl - .03) - .05, .01)
        door = smax(np.abs(rect2(x, y, (-1.3, 2.55), (.55, 1.0), .03)) - .07, np.abs(z - hl - .03) - .05, .01)
        pole = capsule(x, y, z, (hw - .2, 4.6, -hl + .3), (hw - .2, 7.2, -hl + .3), .035)
        return union(frame, door, pole)

    def flag(x, y, z):
        return box(x, y, z, (hw + .35, 6.85, -hl + .3), (.55, .32, .015), .01)

    def inside(x, y, z):
        return np.minimum(smax(rect2(x, y, (-1.3, 2.55), (.55, 1.0), .03), np.abs(z - hl + .08) - .02, .01),
                          smax(rect2(x, y, (1.0, 3.2), (.75, .55), .03), np.abs(z - hl + .08) - .02, .01))
    lo, hi = (-hw - .8, -.05, -hl - .8), (hw + 1.0, 7.4, hl + 3.0)
    parts = [part(walls, lo, hi, .025, GLOSS, colour, 2200),
             part(deck, lo, hi, .02, GLOSS, (.62, .44, .26), 1600),
             part(roof, lo, hi, .03, GLOSS, (.93, .91, .84), 500),
             part(trim, lo, hi, .02, GLOSS, WHITE, 900),
             part(flag, lo, hi, .02, GLOSS, (.88, .14, .10), 60),
             part(inside, lo, hi, .02, GLOSS, (.08, .10, .12), 120)]
    return assemble(parts, [], reach=2.0)


def fence_post():
    def post(x, y, z):
        d = box(x, y, z, (0, .95, 0), (.11, .95, .11), .025)
        return smax(d, (y - 1.9) + .6 * np.maximum(np.abs(x), np.abs(z)), .02)
    p = part(post, (-.2, -.05, -.2), (.2, 2.0, .2), .012, GLOSS, (.42, .30, .20), 120)
    return assemble([p], [], reach=.4)


def fence_rail():
    """A split rail one metre long along +x from the origin; scaled to each span."""
    def rail(x, y, z):
        d = smax(ellipsoid(x, y, z, (.5, 0, 0), (.6, .075, .06)), np.abs(x - .5) - .5, .01)
        return d + .006 * np.sin(x * 37) * np.sin(z * 50)
    p = part(rail, (-.05, -.1, -.1), (1.05, .1, .1), .008, GLOSS, (.86, .82, .70), 100)
    return assemble([p], [], reach=.2)


# --- Television compound -----------------------------------------------------------------------
COBALT = (.055, .24, .62)


def ob_van():
    """An outside-broadcast truck, 5.1 m long along z (cab towards +z), standing on y = 0."""
    def body(x, y, z):
        box_ = box(x, y, z, (0, 1.75, -.1), (1.3, 1.15, 2.45), .14)
        cab = smax(box(x, y, z, (0, 1.45, 2.2), (1.25, .85, .45), .2), (z - 2.3) + .55 * (y - 1.5), .1)
        return smin(box_, cab, .08)

    def glass(x, y, z):
        screen = smax(box(x, y, z, (0, 2.0, 2.15), (1.12, .38, .4), .06), -(z - 2.25) - .55 * (y - 1.5) - .05, .02)
        return screen

    def wheels(x, y, z):
        d = None
        for wz in (-1.5, 1.7):
            w = cylinder_x(np.abs(x), y, z, (1.15, .5, wz), .5, .17, .06)
            d = w if d is None else np.minimum(d, w)
        return d

    def kit(x, y, z):
        rack = smax(box(x, y, z, (0, 2.98, -.8), (1.0, .04, 1.3), .02), -np.abs(np.mod(z, .4) - .2) + .17, .005)
        mast = capsule(x, y, z, (-.9, 2.9, -1.9), (-.9, 5.0, -1.9), .035)
        bumper = box(x, y, z, (0, .48, 2.62), (1.28, .14, .1), .05)
        return union(rack, mast, bumper)
    lo, hi = (-1.6, -.05, -2.8), (1.6, 5.1, 2.9)
    parts = [part(body, lo, hi, .02, GLOSS, (.93, .91, .84), 2200),
             part(glass, lo, hi, .02, GLASS, (.08, .13, .17), 200),
             part(wheels, lo, hi, .015, RUBBER, (.05, .05, .055), 700),
             part(kit, lo, hi, .015, METAL, (.45, .47, .50), 600)]
    side = lambda s: (lambda x, y, z: 1.0 - s * x)
    decals = []
    for s in (1, -1):
        decals.append(([lambda x, y, z: 1.1 - y, lambda x, y, z: y - 1.5, side(s)], GLOSS, COBALT))
        # Block letters T and V, in the local (z, y) plane of each flank.
        t_top = [(-1.75, 2.62), (-1.0, 2.62), (-1.0, 2.48), (-1.75, 2.48)]
        t_stem = [(-1.45, 2.48), (-1.30, 2.48), (-1.30, 1.85), (-1.45, 1.85)]
        v_left = [(-.85, 2.62), (-.70, 2.62), (-.48, 1.85), (-.58, 1.85)]
        v_right = [(-.25, 2.62), (-.10, 2.62), (-.38, 1.85), (-.48, 1.85)]
        for glyph in (t_top, t_stem, v_left, v_right):
            # Seen from the +x flank the cab is on the viewer's left, so the
            # letters run the other way there to read "TV" on both flanks.
            g = [(-zz, yy) for zz, yy in glyph] if s > 0 else glyph
            decals.append((convex((2, 1), g) + [side(s)], GLOSS, COBALT))
        win = [(.9, 1.85), (1.75, 1.85), (1.75, 2.45), (.9, 2.45)]
        decals.append((convex((2, 1), win) + [side(s)], GLOSS, (.08, .13, .17)))
    for s in (1, -1):
        lamp = [(s * .7, .75), (s * 1.05, .75), (s * 1.05, .95), (s * .7, .95)]
        decals.append((convex((0, 1), lamp) + [lambda x, y, z: 2.55 - z], GLOSS, (1, .97, .88)))
    return assemble(parts, decals, reach=1.5)


def satellite_dish():
    """A dish on a turntable, facing up and forward; the base at y = 0."""
    def dish(x, y, z):
        c, s = math.cos(-.7), math.sin(-.7)
        u, v = z * c - (y - .75) * s, z * s + (y - .75) * c
        r = np.sqrt(x * x + u * u)
        bowl = np.abs(v - .22 * r * r) - .025
        return smax(bowl, r - 1.05, .02)

    def mount(x, y, z):
        base = cylinder_y(x, y, z, (0, .12, 0), .28, .12, .04)
        post = capsule(x, y, z, (0, .1, 0), (0, .7, 0), .07)
        feed = capsule(x, y, z, (0, .75, 0), (0, 1.3, .55), .025)
        horn = ellipsoid(x, y, z, (0, 1.3, .55), (.07, .07, .09))
        return union(base, post, feed, horn, k=.02)
    lo, hi = (-1.2, -.05, -1.0), (1.2, 1.9, 1.1)
    parts = [part(dish, lo, hi, .01, GLOSS, (.93, .92, .88), 1200),
             part(mount, lo, hi, .01, METAL, (.40, .42, .45), 600)]
    return assemble(parts, [], reach=.8)


def tv_camera():
    """A broadcast camera body with a long lens towards +z; its centre at the origin."""
    def body(x, y, z):
        head = box(x, y, z, (0, 0, 0), (.27, .2, .42), .05)
        hood = box(x, y, z, (0, 0, .62), (.2, .17, .22), .04)
        finder = box(x, y, z, (-.37, .18, -.19), (.11, .09, .11), .03)
        handle = capsule(x, y, z, (.2, -.23, 0), (.35, -.3, -.65), .035)
        top = capsule(x, y, z, (-.1, .25, -.2), (-.1, .25, .2), .03)
        return union(head, hood, finder, handle, top, k=.02)

    def tally(x, y, z):
        return box(x, y, z, (0, .235, .3), (.06, .035, .045), .015)
    lo, hi = (-.55, -.4, -.8), (.45, .35, .9)
    parts = [part(body, lo, hi, .008, GLOSS, (.07, .075, .085), 600),
             part(tally, lo, hi, .006, GLOSS, (.98, .055, .035), 80)]
    return assemble(parts, [], reach=.4)


def bench():
    """A park bench: cast-iron scroll ends, four seat slats and two back slats; 2.7 m long
    along x, the seat front towards +z, the feet reaching .35 below y = 0 for slopes."""
    def iron(x, y, z):
        d = None
        for ex in (-1.05, 1.05):
            ax = np.abs(x - ex) - .03
            side = np.minimum.reduce([polyline2(z, y, [(.3, -.35), (.28, .72), (-.3, .72)]) - .04,
                                      polyline2(z, y, [(-.32, -.35), (-.3, .72), (-.36, 1.48)]) - .04,
                                      polyline2(z, y, [(-.3, .5), (.3, .5)]) - .025])
            part_ = smax(side, ax, .01)
            d = part_ if d is None else np.minimum(d, part_)
        return d

    def slats(x, y, z):
        seat = None
        for dz in (-.24, -.08, .08, .24):
            s = box(x, y, z, (0, .74, dz), (1.35, .025, .065), .015)
            seat = s if seat is None else np.minimum(seat, s)
        c, s_ = math.cos(.18), math.sin(.18)
        for h in (1.05, 1.33):
            u = (z + .36) * c + (y - h) * s_
            v = -(z + .36) * s_ + (y - h) * c
            b = smax(np.abs(x) - 1.35, rect2(u, v, (0, 0), (.035, .1), .015), .01)
            seat = np.minimum(seat, b)
        return seat
    lo, hi = (-1.5, -.4, -.6), (1.5, 1.6, .5)
    parts = [part(slats, lo, hi, .01, GLOSS, (.62, .40, .22), 380),
             part(iron, lo, hi, .01, METAL, (.10, .11, .12), 260)]
    return assemble(parts, [], reach=.5)
