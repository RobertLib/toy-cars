"""Sprite: a chunky retro city hatch with round eyes, chrome brightwork and an ivory roof."""
import math

import numpy as np

import sdf
from cars import (Car, part, wheel, face_rule, pocket, inlay, relu, smoothstep, strip, convex,
                  PAINT, GLASS, CHROME, TRIM, GLOSS, HEADLAMP, TAILLAMP, INK, TINT, SILVER, IVORY)
from sdf import smin, smax, union, subtract, box, rect2, ellipsoid, capsule, cylinder_x, cylinder_z, rounded_polygon

AMBER = (1., .52, .07)
RED_LENS = (.85, .06, .04)
GAP = (.02, .02, .022)


def ring(ref, r, radius, tube, sink):
    """A rolled chrome bezel: a tube following the surface around a round opening."""
    return np.hypot(r - radius, ref + sink) - tube


def sprite():
    FRONT, REAR, WX, TR = .90, -.90, .83, .37
    WY, ARCH, W = TR - .04, .415, .80
    P = sdf.Profile
    lower_side = P(rounded_polygon([(-1.38, .25, .05), (1.28, .25, .10), (1.415, .33, .07), (1.44, .60, .14),
                                    (1.36, .775, .16), (.62, .885, .08), (-1.22, .915, .08), (-1.40, .86, .10),
                                    (-1.425, .30, .05)], .05))
    lower_plan = P(rounded_polygon([(-.66, 1.44, .30), (.66, 1.44, .30), (W, 1.05, .4), (W, -1.10, .4),
                                    (.74, -1.43, .24), (-.74, -1.43, .24), (-W, -1.10, .4), (-W, 1.05, .4)], .2))
    cabin_side = P(rounded_polygon([(.70, .83, 0), (.62, .885, .03), (.31, 1.405, .07), (-1.10, 1.455, .09),
                                    (-1.30, 1.00, .05), (-1.34, .85, 0)], .03))
    door_glass = P(rounded_polygon([(.546, .925), (.281, 1.37), (-.28, 1.375), (-.28, .925)], .025))
    quarter_glass = P(rounded_polygon([(-.33, .925), (-.33, 1.375), (-1.067, 1.37), (-1.263, .925)], .025))
    front_glass = P(rounded_polygon([(-.64, .925), (.64, .925), (.57, 1.37), (-.57, 1.37)], .05))
    rear_glass = P(rounded_polygon([(-.55, 1.03), (.55, 1.03), (.52, 1.40), (-.52, 1.40)], .05))

    def cabin_width(y):
        return .765 - .17 * (y - .885)

    def cabin(x, y, z):
        return smax(cabin_side(z, y + .05 * x * x), (np.abs(x) - cabin_width(y)) / 1.0144, .055)

    def shell(x, y, z):
        ax = np.abs(x)
        tumble = .35 * relu(.42 - y) + .22 * relu(y - .74)
        # The front wings rise a little around the lamps, like pressed steel.
        lift = .035 * smoothstep(.45, .70, ax) * smoothstep(.40, .80, z)
        d = smax(lower_side(z, y + .06 * x * x - lift), lower_plan(ax + tumble, z), .07)
        d = smin(d, cabin(x, y, z), .018)
        # A small lip over the rear window, part of the roof pressing.
        d = smin(d, box(x, y, z, (0, 1.44, -1.13), (.54, .012, .05), .01), .02)
        for zc in (FRONT, REAR):
            d = subtract(d, cylinder_x(ax, y, z, (1.0, WY, zc), ARCH, .42), .015)
        return d

    def windows(x, y, z):
        ax = np.abs(x)
        s = smax(np.minimum(door_glass(z, y), quarter_glass(z, y)), cabin_width(y) - .10 - ax, 0)
        f = smax(front_glass(x, y), -z, 0)
        r = smax(rear_glass(x, y), z + 1.0, 0)
        return np.minimum(np.minimum(s, f), r)

    def front(cx, cy):
        return lambda x, y, z: np.hypot(np.abs(x) - cx, y - cy) + np.where(z > 1.1, 0, 1)

    def rear(cx, cy):
        return lambda x, y, z: np.hypot(np.abs(x) - cx, y - cy) + np.where(z < -1.1, 0, 1)

    head_r = front(.555, .655)
    tail_r = rear(.57, .66)

    def head(x, y, z):
        return head_r(x, y, z) - .095

    def tail(x, y, z):
        return tail_r(x, y, z) - .062

    def grille(x, y, z):
        return smax(rect2(x, y, (0, .54), (.24, .062), .05), 1.1 - z, 0)

    def plate(x, y, z):
        return smax(rect2(x, y, (0, .55), (.17, .045), .01), z + 1.2, 0)

    POCKETS = ((head, .032), (grille, .035), (tail, .02), (plate, .006))

    def body(x, y, z):
        ref = shell(x, y, z)
        d = pocket(ref, ref, windows(x, y, z), .012, .007)
        for region, depth in POCKETS:
            d = pocket(d, ref, region(x, y, z), depth, .004)
        return d

    def glass(x, y, z):
        c = cabin(x, y, z)
        return smax(smax(c + .007, windows(x, y, z) - .015, .002), -(c + .025), 0)

    def underside(c, n):
        ax = np.abs(c[:, 0])
        well = np.zeros(len(c), bool)
        for zc in (FRONT, REAR):
            dy, dz = c[:, 1] - WY, c[:, 2] - zc
            r = np.hypot(dy, dz)
            inward = -(n[:, 1] * dy + n[:, 2] * dz) / np.maximum(r, 1e-6)
            well |= (r < ARCH + .01) & (ax < .99) & ((inward > .4) | ((ax < .62) & (np.abs(n[:, 0]) > .6)))
        return well | ((n[:, 1] < -.55) & (c[:, 1] < .32))

    V = .004
    wheel_parts = wheel(TR, .25, .235, 'block', 'star', 8, (.52, .53, .55), caliper=(.30, .31, .33),
                        pitches=26, depth=.012, shoulder=.055)
    # Small eight-spoke rims need more of the wheel's budget than the tire.
    wheel_parts[0].budget, wheel_parts[1].budget = 3200, 3800
    car = Car('sprite', wheel_parts, WX, WY, (FRONT, REAR))
    add = car.parts.append
    add(part(body, (-1.0, -.02, -1.50), (1.0, 1.52, 1.52), V, PAINT, (1, 1, 1), 39500,
             lambda c, n, k, col: face_rule(c, n, k, col, [(underside, TRIM, INK)])))
    add(part(glass, (-.80, .86, -1.38), (.80, 1.48, .72), V, GLASS, TINT, 6500))

    # Round headlamps: chrome reflector, domed lens, chrome bezel; amber indicators below.
    front_lo, front_hi = (-.75, .38, 1.10), (.75, .80, 1.52)
    add(part(lambda x, y, z: inlay(shell(x, y, z), head(x, y, z), .027, .02),
             front_lo, front_hi, .003, CHROME, SILVER, 900))

    def lens(region_r, radius, lift=.0):
        def field(x, y, z):
            ref = shell(x, y, z)
            r = region_r(x, y, z)
            dome = .011 - .009 * np.clip(1 - (r / radius) ** 2, 0, 1) - lift
            return smax(smax(ref + dome, r - radius, .002), -(ref + .025), 0)
        return field
    add(part(lens(head_r, .078), front_lo, front_hi, .003, HEADLAMP, (1, .97, .9), 1400))
    add(part(lambda x, y, z: ring(shell(x, y, z), head_r(x, y, z), .098, .010, .004),
             front_lo, front_hi, .003, CHROME, SILVER, 2200))
    indicator_r = front(.62, .49)
    add(part(lens(indicator_r, .032, .012), front_lo, front_hi, .003, GLOSS, AMBER, 400))
    add(part(lambda x, y, z: ring(shell(x, y, z), indicator_r(x, y, z), .035, .0055, .002),
             front_lo, front_hi, .003, CHROME, SILVER, 900))

    # Grille: dark floor, chrome bars and a chrome surround.
    add(part(lambda x, y, z: inlay(shell(x, y, z), grille(x, y, z), .03, .02),
             front_lo, front_hi, .003, TRIM, INK, 400))

    def grille_bars(x, y, z):
        ref = shell(x, y, z)
        bars = np.abs(np.mod(y - .54 + .0155, .031) - .0155) - .0055
        return smax(smax(smax(bars, grille(x, y, z) + .003, .002), ref + .009, .003), -(ref + .035), 0)
    add(part(grille_bars, front_lo, front_hi, .003, CHROME, SILVER, 1000))

    def grille_surround(x, y, z):
        ref = shell(x, y, z)
        g = grille(x, y, z)
        return smax(smax(np.abs(g + .002) - .008, ref - .003, .003), -(ref + .015), 0)
    add(part(grille_surround, front_lo, front_hi, .003, CHROME, SILVER, 1400))

    # A small chrome badge on the nose.
    def badge(x, y, z):
        ref = shell(x, y, z)
        oval = rect2(x, z, (0, 1.335), (.045, .018), .018)
        return smax(smax(oval, ref - .005, .003), -(ref + .01), 0)
    add(part(badge, (-.08, .70, 1.28), (.08, .84, 1.40), .002, CHROME, SILVER, 300))

    # Wrap-round chrome bumpers with over-riders.
    def bumpers(x, y, z):
        ax = np.abs(x)
        outline = lower_plan(ax, z)
        blade = smax(outline - .035, np.abs(y - .345) - .026, .014)
        front_blade = smax(blade, 1.22 - z, .03)
        rear_blade = smax(blade, z + 1.24, .03)
        riders = np.minimum(capsule(ax, y, z, (.30, .29, 1.465), (.30, .45, 1.465), .022),
                            capsule(ax, y, z, (.30, .29, -1.448), (.30, .45, -1.448), .022))
        return smin(np.minimum(front_blade, rear_blade), riders, .008)
    add(part(bumpers, (-.9, .22, -1.55), (.9, .52, 1.56), .003, CHROME, SILVER, 2800))

    # Tail lamps: red domes in chrome rings, white reversing lamps, plate.
    rear_lo, rear_hi = (-.75, .42, -1.50), (.75, .80, -1.10)
    add(part(lambda x, y, z: inlay(shell(x, y, z), tail(x, y, z), .018, .02),
             rear_lo, rear_hi, .003, CHROME, SILVER, 500))
    add(part(lens(tail_r, .052), rear_lo, rear_hi, .003, TAILLAMP, RED_LENS, 1000))
    add(part(lambda x, y, z: ring(shell(x, y, z), tail_r(x, y, z), .064, .0075, .003),
             rear_lo, rear_hi, .003, CHROME, SILVER, 1500))
    add(part(lambda x, y, z: inlay(shell(x, y, z), plate(x, y, z), .002, .02),
             rear_lo, rear_hi, .003, GLOSS, IVORY, 240))

    # Black arch extensions, sills, roof spoiler.
    def trims(x, y, z):
        ax = np.abs(x)
        d = 1.
        for zc in (FRONT, REAR):
            r = np.hypot(y - WY, z - zc)
            band = smax(np.abs(r - ARCH - .02) - .022, smax(ax - .945, .70 - ax, .01), .012)
            d = np.minimum(d, smax(band, WY - .11 - y, .02))
        sill = box(ax, y, z, (.79, .30, 0), (.03, .035, .47), .02)
        return np.minimum(d, sill)
    add(part(trims, (-.96, .14, -1.36), (.96, .80, 1.36), V, TRIM, INK, 2600))

    # Bullet mirrors, door handles, fuel filler cap, exhaust and wipers.
    def mirrors(x, y, z):
        ax = np.abs(x)
        stalk = capsule(ax, y, z, (.76, .895, .44), (.855, .975, .435), .011)
        head_ = ellipsoid(ax, y, z, (.875, .99, .425), (.04, .034, .058))
        return smin(stalk, head_, .008)
    add(part(mirrors, (-.95, .85, .33), (.95, 1.06, .52), .003, CHROME, SILVER, 1000, hidden=body))

    def handles(x, y, z):
        ref = shell(x, y, z)
        bar = smax(rect2(z, y, (-.17, .80), (.045, .009), .008), .65 - np.abs(x), 0)
        return smax(smax(ref - .006, -(ref + .004), 0), bar, .003)
    add(part(handles, (-.86, .76, -.25), (.86, .84, -.09), .002, CHROME, SILVER, 300))
    add(part(lambda x, y, z: smax(cylinder_x(x, y, z, (.79, .79, -1.06), .042, .012, .006), -(body(x, y, z) + .0015), .002),
             (.6, .72, -1.13), (.86, .86, -.99), .002, CHROME, SILVER, 300))
    add(part(lambda x, y, z: subtract(cylinder_z(x, y, z, (.40, .235, -1.45), .03, .06, .008),
                                      cylinder_z(x, y, z, (.40, .235, -1.50), .021, .07), .003),
             (.33, .18, -1.53), (.47, .30, -1.36), .002, CHROME, SILVER, 500, hidden=body))

    def wipers(x, y, z):
        arms = np.minimum(capsule(x, y, z, (-.50, .95, .586), (-.02, .958, .582), .008),
                          capsule(x, y, z, (.06, .95, .586), (.54, .958, .582), .008))
        return smax(arms, -(cabin(x, y, z) + .002), .002)
    add(part(wipers, (-.62, .90, .50), (.66, 1.02, .66), .002, TRIM, INK, 600, hidden=body))

    # Livery: ivory roof, twin bonnet stripes, chrome waistline.
    car.decals.append(([lambda x, y, z: 1.395 - y], GLOSS, IVORY))
    for side in (1, -1):
        car.decals.append(([lambda x, y, z, s=side: s * x - .16, lambda x, y, z, s=side: .09 - s * x,
                            lambda x, y, z: .72 - y, lambda x, y, z: .68 - z], GLOSS, IVORY))
        car.decals.append((strip((2, 1), (.56, .905), (-1.24, .905), .0035, .6 * side), CHROME, SILVER))
        # Twin coachlines along the doors.
        for y in (.47, .497):
            car.decals.append((strip((2, 1), (.40, y), (-.40, y), .0045, .6 * side), GLOSS, IVORY))
    # Panel gaps: doors, bonnet and boot lid.
    door_lines = [[(.53, .31), (.53, .885)], [(-.30, .31), (-.30, .905)], [(.53, .31), (-.30, .31)]]
    for line in door_lines:
        for side in (.6, -.6):
            car.decals.append((strip((2, 1), *line, .0018, side), TRIM, GAP))
    bonnet = [(-.58, .66), (-.58, 1.31), (.58, 1.31), (.58, .66)]
    for a, b in zip(bonnet[:-1], bonnet[1:]):
        car.decals.append((strip((0, 2), a, b, .0018) + [lambda x, y, z: .74 - y], TRIM, GAP))
    boot = [(-.50, .845), (-.50, .44), (.50, .44), (.50, .845)]
    for a, b in zip(boot[:-1], boot[1:]):
        car.decals.append((strip((0, 1), a, b, .0018) + [lambda x, y, z: z + 1.2], TRIM, GAP))
    car.lamps = {'head': (.555, .655, 1.363), 'tail': (.57, .66, -1.38)}
    return car
