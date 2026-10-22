"""Bruiser: an off-road trophy-truck pickup on beadlock wheels and mud-terrain tires.

Tall and square-shouldered: black box flares over the arches, rock sliders,
a bull bar with a winch, a snorkel up the right pillar, a roof light bar and a
roll bar carrying four spot lamps over an upright spare wheel in the bed.
"""
import math

import numpy as np

import sdf
from sdf import smin, smax, union, subtract, box, rect2, circle2, ellipsoid, capsule
from sdf import cylinder_x, cylinder_y, cylinder_z, rounded_polygon
from cars import (Car, part, polar, pocket, inlay, face_rule, convex, strip, relu, smoothstep,
                  PAINT, GLASS, CHROME, RUBBER, TRIM, GLOSS, HEADLAMP, TAILLAMP, METAL, HUB,
                  INK, RUBBER_BLACK, TINT, SILVER, IVORY)

BRONZE = (.46, .31, .15)
GUNMETAL = (.16, .165, .17)
AMBER = (.95, .52, .08)


def mud_tire(radius, width, rim, pitches=16):
    """Mud-terrain tread: two staggered rows of big blocks, and shoulder lugs
    between them that run off the tread onto the sidewall."""
    half, depth = width / 2, .03
    base = radius - depth
    step = 2 * math.pi / pitches

    def field(x, y, z):
        r, a = polar(y, z)
        carcass = rect2(x, r, (0, (base + rim) / 2), (half - .004, (base - rim) / 2), .07)
        envelope = rect2(x, r, (0, (radius + rim) / 2), (half + .006, (radius - rim) / 2), .04)
        blocks = 1.
        for row, side in enumerate((-1, 1)):
            u = (np.mod(a + step * .5 * row, step) - step / 2) * radius
            centre = rect2(u, x - side * half * .42, (0, 0), (step * radius * .27, half * .36), .014)
            v = (np.mod(a + step * .5 * (1 - row), step) - step / 2) * radius
            shoulder = smax(rect2(v, x - side * half * .80, (0, 0), (step * radius * .17, half * .32), .012),
                            (radius - .07) - r, .012)
            blocks = np.minimum(blocks, np.minimum(centre, shoulder))
        tread = smax(smax(blocks, envelope, .006), (base - .008) - r, .004)
        d = smin(carcass, tread, .005)
        # A raised ring on the sidewall where the lettering would sit.
        ring = smax(np.abs(r - (rim + base) / 2 - .01) - .016, np.abs(np.abs(x) - half + .004) - .006, .004)
        return smin(d, ring, .004)
    return field


def beadlock_face(radius, width):
    """Deep-dish wheel centre with six round windows and a lug-nut hub."""
    half = width / 2
    face = half - .055

    def field(x, y, z):
        r, a = polar(y, z)
        barrel = rect2(x, r, (0, radius - .03), (half - .01, .03), .008)
        well = subtract(barrel, rect2(x, r, (0, radius - .03), (half - .03, .016), .004), .003)
        # Dish wall from the face out to the lip.
        dish = smax(np.abs(r - (radius - .045)) - .012, np.abs(x - (face + half) / 2) - (half - face) / 2, .006)
        disc = rect2(x, r, (face - .008, (radius - .03) / 2), (.014, (radius - .03) / 2), .008)
        step = 2 * math.pi / 6
        u = (np.mod(a, step) - step / 2) * r
        holes = circle2(u, r, (0, radius * .58), radius * .17)
        plate = subtract(disc, smax(holes, np.abs(x - face) - .05, .002), .008)
        hub = cylinder_x(x, y, z, (face + .006, 0, 0), .085, .02, .012)
        cap = cylinder_x(x, y, z, (face + .03, 0, 0), .045, .012, .008)
        nut_step = 2 * math.pi / 6
        nu = (np.mod(a, nut_step) - nut_step / 2) * r
        nuts = smax(circle2(nu, r, (0, .066), .012), np.abs(x - face - .03) - .012, .003)
        return union(well, dish, plate, hub, cap, nuts, k=.006)
    return field


def beadlock_ring(radius, width):
    """The bolted clamp ring proud of the rim lip."""
    half = width / 2

    def field(x, y, z):
        r, a = polar(y, z)
        ring = rect2(x, r, (half - .004, radius - .028), (.014, .032), .01)
        step = 2 * math.pi / 12
        u = (np.mod(a, step) - step / 2) * r
        bolts = ellipsoid(u, r, x, (0, radius - .028, half + .01), (.014, .014, .01))
        return smin(ring, bolts, .006)
    return field


def bruiser_wheel(radius=.50, width=.30, rim_radius=.30):
    half = width / 2
    lo, hi = (-half - .015, -radius - .01, -radius - .01), (half + .025, radius + .01, radius + .01)
    tire = part(mud_tire(radius, width, rim_radius - .006), lo, hi, .004, RUBBER, RUBBER_BLACK, 4800)
    face = part(beadlock_face(rim_radius, width - .03), lo, hi, .003, GLOSS, BRONZE, 1300)
    ring = part(beadlock_ring(rim_radius, width - .03), lo, hi, .0025, METAL, SILVER, 1400)
    disc_r = rim_radius - .085
    disc = part(lambda x, y, z: subtract(cylinder_x(x, y, z, (-.03, 0, 0), disc_r, .012, .003),
                                         cylinder_x(x, y, z, (-.03, 0, 0), .06, .05), .003),
                lo, hi, .004, METAL, (.36, .36, .37), 300)

    def caliper(x, y, z):
        r, a = polar(y, z)
        arc = np.abs(np.arctan2(np.sin(a - 2.2), np.cos(a - 2.2))) * r - .08
        return smax(smax(arc, np.abs(r - disc_r + .02) - .042, .015), np.abs(x + .03) - .038, .01)
    calipers = part(caliper, lo, hi, .004, GLOSS + HUB, (.10, .10, .11), 240)
    return [tire, face, ring, disc, calipers]


def bruiser():
    W, FRONT, REAR, WX, WY, TR, ARCH = .82, 1.05, -1.02, .85, .46, .50, .565
    P = sdf.Profile
    lower_side = P(rounded_polygon([(-1.575, .52, .04), (1.40, .52, .06), (1.525, .64, .05), (1.535, 1.10, .06),
                                    (1.47, 1.195, .07), (.60, 1.27, .04), (-1.575, 1.27, .025)], .04))
    lower_plan = P(rounded_polygon([(-W + .04, 1.535, .12), (W - .04, 1.535, .12), (W, 1.30, .2), (W, -1.50, .06),
                                    (W - .02, -1.585, .05), (-W + .02, -1.585, .05), (-W, -1.50, .06),
                                    (-W, 1.30, .2)], .06))
    cab_side = P(rounded_polygon([(.66, 1.20, 0), (.58, 1.262, .025), (-.02, 1.845, .07), (-.80, 1.87, .07),
                                  (-.855, 1.80, .04), (-.875, 1.20, 0)], .03))
    side_glass = P(rounded_polygon([(.47, 1.335), (.01, 1.785), (-.72, 1.80), (-.745, 1.335)], .03))
    front_glass = P(rounded_polygon([(-.665, 1.315), (.665, 1.315), (.60, 1.79), (-.60, 1.79)], .045))
    back_glass = P(rounded_polygon([(-.56, 1.40), (.56, 1.40), (.53, 1.77), (-.53, 1.77)], .045))

    def cab_width(y):
        return .785 - .13 * (y - 1.27)

    def cab(x, y, z):
        return smax(cab_side(z, y + .025 * x * x), (np.abs(x) - cab_width(y)) / 1.008, .045)

    def shell(x, y, z):
        ax = np.abs(x)
        tumble = .22 * relu(y - 1.16, .025)
        d = smax(lower_side(z, y + .03 * x * x), lower_plan(ax + tumble, z), .045)
        # Power dome on the bonnet.
        d = smin(d, box(x, y, z, (0, 1.225, 1.00), (.34, .035, .44), .03), .03)
        d = smin(d, cab(x, y, z), .015)
        # The bed: an open tub behind the cab.
        d = subtract(d, box(x, y, z, (0, 1.12, -1.225), (.69, .30, .305), .02), .012)
        for zc in (FRONT, REAR):
            d = subtract(d, cylinder_x(ax, y, z, (1.0, WY, zc), ARCH, .45), .015)
        return d

    def windows(x, y, z):
        ax = np.abs(x)
        s = smax(side_glass(z, y), cab_width(y) - .10 - ax, 0)
        f = smax(front_glass(x, y), -(z + .10), 0)
        b = smax(back_glass(x, y), z + .70, 0)
        return np.minimum(np.minimum(s, f), b)

    def front_face(region):
        return lambda x, y, z: smax(region(np.abs(x), y), 1.30 - z, 0)

    def rear_face(region):
        return lambda x, y, z: smax(region(np.abs(x), y), z + 1.45, 0)

    head = front_face(lambda u, v: rect2(u, v, (.585, 1.025), (.15, .08), .028))
    indicator = front_face(lambda u, v: rect2(u, v, (.585, .905), (.13, .022), .012))
    grille = front_face(lambda u, v: rect2(u, v, (0, 1.005), (.385, .115), .022))
    tail = rear_face(lambda u, v: rect2(u, v, (.735, 1.07), (.05, .15), .016))
    plate = rear_face(lambda u, v: rect2(u, v, (0, .80), (.20, .056), .01))
    gate_handle = rear_face(lambda u, v: rect2(u, v, (0, 1.17), (.11, .022), .014))

    def handle(x, y, z):
        return smax(rect2(z, y, (-.36, 1.205), (.07, .018), .015), .7 - np.abs(x), 0)

    POCKETS = ((head, .04), (indicator, .016), (grille, .055), (tail, .022), (plate, .007),
               (gate_handle, .012), (handle, .01))

    def body(x, y, z):
        ref = shell(x, y, z)
        d = pocket(ref, ref, windows(x, y, z), .012, .005)
        for region, depth in POCKETS:
            d = pocket(d, ref, region(x, y, z), depth, .004)
        return d

    def glass(x, y, z):
        c = cab(x, y, z)
        return smax(smax(c + .007, windows(x, y, z) - .015, .002), -(c + .025), 0)

    FLOORS = {'reflector': (head, .033), 'grille': (grille, .05), 'tail': (tail, .007), 'plate': (plate, .002),
              'indicator': (indicator, .005)}

    def floor(name):
        region, at = FLOORS[name]
        return lambda x, y, z: inlay(shell(x, y, z), region(x, y, z), at, .022)

    def dark_faces(c, n):
        ax = np.abs(c[:, 0])
        well = np.zeros(len(c), bool)
        for zc in (FRONT, REAR):
            dy, dz = c[:, 1] - WY, c[:, 2] - zc
            r = np.hypot(dy, dz)
            inward = -(n[:, 1] * dy + n[:, 2] * dz) / np.maximum(r, 1e-6)
            well |= (r < ARCH + .012) & (ax < .99) & ((inward > .4) | ((ax < .62) & (np.abs(n[:, 0]) > .6)))
        under = (n[:, 1] < -.55) & (c[:, 1] < .58)
        # A black bed liner inside the tub.
        liner = ((ax < .705) & (c[:, 2] < -.905) & (c[:, 2] > -1.545) & (c[:, 1] > .80) & (c[:, 1] < 1.262)
                 & ~((ax > .69) & (n[:, 1] > .9)))
        return well | under | liner

    V = .0045
    car = Car('bruiser', bruiser_wheel(TR, .30, .30), WX, WY, (FRONT, REAR))
    add = car.parts.append
    add(part(body, (-1.0, .40, -1.66), (1.0, 1.95, 1.62), V, PAINT, (1, 1, 1), 38200,
             lambda c, n, k, col: face_rule(c, n, k, col, [(dark_faces, TRIM, INK)])))
    # Glass: keep only the outer skin (its rim and inner side hide under the
    # paint) and shade it with the cab's own normals, so decimated slivers
    # across the panes cannot bend the reflections.
    pane = part(glass, (-.84, 1.25, -.95), (.84, 1.90, .70), .004, GLASS, TINT, 5000,
                hidden=lambda x, y, z: cab(x, y, z) + .016)
    pane.field = cab
    add(pane)

    # Headlamps: a chrome reflector, one big round lamp and an amber indicator beneath.
    front_lo, front_hi = (-.8, .5, 1.36), (.8, 1.22, 1.70)
    add(part(floor('reflector'), front_lo, front_hi, .003, CHROME, SILVER, 900))

    def lamps(x, y, z):
        e = ellipsoid(np.abs(x), y, z, (.585, 1.025, 1.44), (.07, .07, .14))
        return smax(e, shell(x, y, z) + .01, .004)
    add(part(lamps, front_lo, front_hi, .003, HEADLAMP, (1, .97, .9), 1000))
    add(part(lambda x, y, z: smax(sdf.torus_x(z, y, np.abs(x), (1.50, 1.025, .585), .072, .009), shell(x, y, z) + .004, .003),
             front_lo, front_hi, .003, CHROME, SILVER, 600))
    add(part(floor('indicator'), front_lo, front_hi, .003, GLOSS, AMBER, 300))
    add(part(floor('grille'), front_lo, front_hi, .003, TRIM, INK, 400))

    def grille_bars(x, y, z):
        ref = shell(x, y, z)
        bars = np.minimum(np.abs(np.mod(y - 1.005 + .028, .056) - .028) - .011, np.abs(x) - .016)
        return smax(smax(smax(bars, grille(x, y, z) + .002, .002), ref + .012, .003), -(ref + .055), 0)
    add(part(grille_bars, front_lo, front_hi, .003, TRIM, (.13, .135, .14), 900))

    # Steel bumper, bull bar, winch and a red recovery hook.
    def bumper(x, y, z):
        ax = np.abs(x)
        beam = box(x, y, z, (0, .655, 1.565), (.80, .085, .065), .02)
        skid = smax(box(x, y, z, (0, .555, 1.50), (.62, .02, .12), .01), (y - .555) - (z - 1.38) * .25, .01)
        tubes = union(capsule(ax, y, z, (.30, .74, 1.635), (.30, 1.13, 1.625), .025),
                      capsule(x, y, z, (-.30, 1.13, 1.625), (.30, 1.13, 1.625), .025),
                      capsule(ax, y, z, (.30, .93, 1.63), (.70, .75, 1.60), .022), k=.012)
        return union(beam, skid, tubes, k=.01)
    add(part(bumper, (-.85, .50, 1.36), (.85, 1.18, 1.68), .003, TRIM, INK, 2000))
    add(part(lambda x, y, z: union(cylinder_x(x, y, z, (0, .655, 1.62), .05, .19, .008),
                                   cylinder_x(np.abs(x), y, z, (.215, .655, 1.62), .055, .02, .006), k=.004),
             (-.3, .58, 1.55), (.3, .73, 1.69), .003, METAL, (.30, .30, .31), 800))
    add(part(lambda x, y, z: sdf.torus_x(z, y, x, (1.64, .575, 0), .03, .009),
             (-.06, .53, 1.62), (.06, .62, 1.66), .002, GLOSS, (.80, .07, .05), 300))

    # Black box flares and rock sliders.
    def flares(x, y, z):
        ax = np.abs(x)
        d = 1.
        for zc in (FRONT, REAR):
            r = np.hypot(y - WY, z - zc)
            band = smax(r - (ARCH + .085), (ARCH - .008) - r, .02)
            lip = smax(band, smax(ax - .995, .74 - ax, .01), .02)
            d = np.minimum(d, smax(lip, .515 - y, .008))
        return d
    add(part(flares, (-1.01, .38, -1.62), (1.01, 1.10, 1.64), .0035, TRIM, INK, 6500))

    def sliders(x, y, z):
        ax = np.abs(x)
        rails = capsule(ax, y, z, (.87, .565, -.38), (.87, .565, .41), .042)
        tabs = np.minimum(box(ax, y, z, (.84, .55, -.20), (.04, .025, .02), .008),
                          box(ax, y, z, (.84, .55, .22), (.04, .025, .02), .008))
        return union(rails, tabs, k=.008)
    add(part(sliders, (-.93, .50, -.44), (.93, .62, .47), .003, TRIM, INK, 900))

    # Mud flaps behind every wheel.
    add(part(lambda x, y, z: np.minimum(box(np.abs(x), y, z, (.86, .36, FRONT - .60), (.12, .17, .006), .004),
                                        box(np.abs(x), y, z, (.86, .36, REAR - .60), (.12, .17, .006), .004)),
             (-1.0, .17, -1.64), (1.0, .55, .47), .003, TRIM, (.06, .06, .065), 500))

    # Snorkel up the right A-pillar.
    def snorkel(x, y, z):
        pipe = union(capsule(x, y, z, (.845, 1.12, .66), (.86, 1.30, .54), .045),
                     capsule(x, y, z, (.86, 1.30, .54), (.835, 1.86, .02), .043), k=.03)
        head = box(x, y, z, (.835, 1.905, .06), (.05, .045, .09), .025)
        return subtract(union(pipe, head, k=.02), box(x, y, z, (.835, 1.91, .16), (.032, .025, .02), .008), .004)
    add(part(snorkel, (.75, 1.05, -.07), (.93, 1.97, .74), .003, TRIM, INK, 1200))

    # Towing mirrors on arms.
    def mirrors(x, y, z):
        ax = np.abs(x)
        return union(box(ax, y, z, (.965, 1.43, .40), (.028, .095, .065), .02),
                     capsule(ax, y, z, (.78, 1.36, .47), (.95, 1.41, .42), .014), k=.01)
    add(part(mirrors, (-1.0, 1.30, .30), (1.0, 1.54, .52), .003, TRIM, INK, 700))
    add(part(lambda x, y, z: smax(box(np.abs(x), y, z, (.965, 1.43, .334), (.022, .085, .006), .005), z - .337, .002),
             (-1.0, 1.32, .31), (1.0, 1.53, .36), .002, CHROME, SILVER, 300))

    # Roof light bar.
    def light_bar(x, y, z):
        return union(box(x, y, z, (0, 1.95, -.12), (.56, .03, .048), .018),
                     box(np.abs(x), y, z, (.42, 1.895, -.12), (.022, .035, .035), .01), k=.008)
    add(part(light_bar, (-.62, 1.84, -.20), (.62, 2.0, -.04), .003, TRIM, INK, 800))
    add(part(lambda x, y, z: smax(box(x, y, z, (0, 1.95, -.069), (.53, .016, .006), .004), -(light_bar(x, y, z) + .0005), 0),
             (-.58, 1.92, -.09), (.58, 1.98, -.05), .002, HEADLAMP, (1, .97, .9), 500))

    # Roll bar over the bed with four spot lamps facing forward.
    def roll_bar(x, y, z):
        ax = np.abs(x)
        return union(capsule(ax, y, z, (.665, 1.26, -.98), (.60, 2.0, -.98), .03),
                     capsule(x, y, z, (-.60, 2.0, -.98), (.60, 2.0, -.98), .03),
                     capsule(ax, y, z, (.62, 1.80, -.99), (.665, 1.27, -1.50), .026), k=.02)
    add(part(roll_bar, (-.72, 1.20, -1.56), (.72, 2.06, -.92), .003, TRIM, INK, 1500))
    spots = (.17, .48)

    def spot_housing(x, y, z):
        ax = np.abs(x)
        return np.minimum.reduce([union(cylinder_z(ax, y, z, (s, 2.085, -.955), .06, .04, .02),
                                        box(ax, y, z, (s, 2.03, -.975), (.012, .03, .012), .004), k=.008)
                                  for s in spots])
    add(part(spot_housing, (-.6, 1.98, -1.02), (.6, 2.16, -.90), .003, TRIM, INK, 900))
    add(part(lambda x, y, z: np.minimum.reduce([ellipsoid(np.abs(x), y, z, (s, 2.085, -.918), (.048, .048, .012))
                                                 for s in spots]),
             (-.6, 2.02, -.94), (.6, 2.15, -.90), .002, HEADLAMP, (1, .95, .82), 800))
    add(part(lambda x, y, z: np.minimum.reduce([sdf.torus_x(z, y, np.abs(x), (-.916, 2.085, s), .053, .007)
                                                 for s in spots]),
             (-.6, 2.02, -.94), (.6, 2.15, -.89), .002, CHROME, SILVER, 600))

    # The spare wheel stands upright in the bed, held by a strap.
    spare_tire, spare_face = mud_tire(.46, .26, .27, 15), beadlock_face(.276, .23)

    def spare(field):
        return lambda x, y, z: field(-(z + 1.24), y - 1.28, x)
    spare_lo, spare_hi = (-.48, .80, -1.39), (.48, 1.76, -1.09)
    add(part(spare(spare_tire), spare_lo, spare_hi, .004, RUBBER, RUBBER_BLACK, 2000))
    add(part(spare(spare_face), spare_lo, spare_hi, .003, GLOSS, BRONZE, 1100))
    add(part(lambda x, y, z: union(cylinder_z(x, y, z, (0, 1.28, -1.36), .035, .03, .01),
                                   box(x, y, z, (0, 1.05, -1.33), (.03, .23, .015), .008), k=.01),
             (-.08, .80, -1.42), (.08, 1.34, -1.28), .003, TRIM, INK, 400))

    # Tail lamps with white reversing lenses below, plate, step bumper and hitch.
    rear_lo, rear_hi = (-.82, .55, -1.62), (.82, 1.30, -1.40)
    tail_floor = floor('tail')
    add(part(lambda x, y, z: smax(tail_floor(x, y, z), .985 - y, .002), rear_lo, rear_hi, .003,
             TAILLAMP, (.85, .06, .04), 400))
    add(part(lambda x, y, z: smax(tail_floor(x, y, z), y - .985, .002), rear_lo, rear_hi, .003,
             GLOSS, (.92, .92, .9), 200))
    add(part(floor('plate'), rear_lo, rear_hi, .003, GLOSS, (.95, .78, .16), 200))

    def rear_trim(x, y, z):
        step = box(x, y, z, (0, .60, -1.605), (.80, .07, .05), .02)
        hitch = union(box(x, y, z, (0, .555, -1.625), (.035, .035, .035), .006),
                      cylinder_y(x, y, z, (0, .60, -1.645), .012, .03, .004), k=.004)
        return np.minimum(step, hitch)
    add(part(rear_trim, (-.84, .48, -1.72), (.84, .70, -1.52), .003, TRIM, INK, 900))
    add(part(lambda x, y, z: sdf.ellipsoid(x, y, z, (0, .645, -1.645), (.026, .026, .026)),
             (-.04, .60, -1.69), (.04, .68, -1.61), .002, CHROME, SILVER, 200))

    # Racing fuel filler on the left bed side, for the big tank.
    def filler(x, y, z):
        ref = shell(x, y, z)
        cap = cylinder_x(x, y, z, (-.825, 1.175, -1.27), .05, .012, .006)
        return smax(cap, -(ref + .004), .002)
    add(part(filler, (-.88, 1.10, -1.34), (-.76, 1.25, -1.20), .002, CHROME, SILVER, 400))
    ring = [(-1.27 + .064 * math.cos(t), 1.175 + .064 * math.sin(t)) for t in np.linspace(0, 2 * math.pi, 20)[:-1]]
    car.decals.append((convex((2, 1), ring, -.6), TRIM, (.03, .03, .033)))

    # Whip antenna with a flag on the right rear bed corner, and a side-exit exhaust.
    whip_base, whip_top = (.70, 1.27, -1.30), (.72, 2.45, -1.34)

    def whip(x, y, z):
        rod = capsule(x, y, z, whip_base, whip_top, .006)
        foot = cylinder_y(x, y, z, (.70, 1.29, -1.30), .016, .025, .006)
        return np.minimum(rod, foot)
    add(part(whip, (.66, 1.24, -1.38), (.76, 2.47, -1.26), .002, TRIM, INK, 300))
    flag = sdf.Profile([(-1.34, 2.44), (-1.58, 2.375), (-1.34, 2.31)])
    add(part(lambda x, y, z: smax(flag(z, y), np.abs(x - .72) - .004, .001),
             (.70, 2.28, -1.62), (.74, 2.47, -1.31), .002, GLOSS, (.98, .42, .06), 200))

    def exhaust(x, y, z):
        pipe = capsule(x, y, z, (.62, .56, -.50), (.86, .56, -.50), .038)
        return subtract(pipe, cylinder_x(x, y, z, (.90, .56, -.50), .027, .06), .004)
    add(part(exhaust, (.55, .50, -.56), (.92, .62, -.44), .0025, CHROME, SILVER, 500))

    # Door handles.
    def handle_bar(x, y, z):
        ref = shell(x, y, z)
        bar = smax(rect2(z, y, (-.36, 1.205), (.055, .007), .006), .7 - np.abs(x), 0)
        return smax(smax(ref + .0025, -(ref + .01), 0), bar, .002)
    add(part(handle_bar, (-.9, 1.15, -.44), (.9, 1.26, -.28), .002, CHROME, SILVER, 300))

    # Livery: a bold ivory flash along the doors, a satin black bonnet panel.
    for side in (1, -1):
        car.decals.append((convex((2, 1), [(.43, .80), (.43, .93), (-.22, .93), (-.40, .80)], .6 * side),
                           GLOSS, IVORY))
        car.decals.append((convex((2, 1), [(.43, .965), (.43, .985), (-.19, .985), (-.17, .965)], .6 * side),
                           GLOSS, IVORY))
    car.decals.append((convex((0, 2), rounded_polygon([(-.30, .62), (.30, .62), (.30, 1.36), (-.30, 1.36)], .05, 3))
                       + [lambda x, y, z: 1.15 - y], TRIM, (.05, .052, .056)))

    # Panel gaps: doors, bonnet, tailgate.
    gap = (.02, .02, .022)
    for line in ([(.53, .58), (.53, 1.255)], [(-.78, .58), (-.78, 1.255)], [(.53, .585), (-.78, .585)]):
        for side in (.6, -.6):
            car.decals.append((strip((2, 1), *line, .0018, side), TRIM, gap))
    bonnet = [(-.74, .64), (-.74, 1.47), (.74, 1.47), (.74, .64)]
    for a, b in zip(bonnet[:-1], bonnet[1:]):
        car.decals.append((strip((0, 2), a, b, .0018) + [lambda x, y, z: 1.12 - y], TRIM, gap))
    for a, b in (((-.67, .64), (-.67, 1.255)), ((.67, .64), (.67, 1.255)), ((-.67, .64), (.67, .64))):
        car.decals.append((strip((0, 1), a, b, .0018) + [lambda x, y, z: z + 1.45], TRIM, gap))
    car.lamps = {'head': (.585, 1.025, 1.53), 'tail': (.735, 1.07, -1.58)}
    return car
