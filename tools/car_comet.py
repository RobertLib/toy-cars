"""Comet: a low long-nosed GT racer with haunched rear fenders and a swan-neck wing.

Built with the signed distance tools in cars.py; game axes, x right, y up,
z forward, the origin on the ground between the axles.
"""
import math
import os

import numpy as np

import sdf
from cars import (Car, part, smoothstep, relu, pocket, inlay, face_rule, convex, strip, polar,
                  tire_field, PAINT, GLASS, CHROME, RUBBER, TRIM, GLOSS, HEADLAMP, TAILLAMP, METAL, HUB,
                  INK, RUBBER_BLACK, TINT, SILVER, IVORY)
from sdf import (smin, smax, union, subtract, box, rect2, circle2, ellipsoid, capsule, cylinder_x,
                 cylinder_z, rounded_polygon)

# COMET_DRAFT=1.6 meshes every part coarser for quick shape reviews.
DRAFT = float(os.environ.get('COMET_DRAFT', '1'))
GRAPHITE = (.13, .135, .14)
CALIPER = (.96, .70, .07)
NUT = (.78, .09, .05)


def ring(center, radius, count=28):
    """A circle as a convex outline for half-space decals."""
    return [(center[0] + radius * math.cos(2 * math.pi * i / count),
             center[1] + radius * math.sin(2 * math.pi * i / count)) for i in range(count)]


def decimate_free(points, faces, budget):
    """cars.decimate without the left-right symmetry a wheel does not have."""
    import bpy
    import cars
    me = bpy.data.meshes.new('decimate')
    me.vertices.add(len(points))
    me.vertices.foreach_set('co', np.stack([points[:, 0], -points[:, 2], points[:, 1]], 1).ravel())
    me.loops.add(faces.size)
    me.loops.foreach_set('vertex_index', faces.ravel())
    me.polygons.add(len(faces))
    me.polygons.foreach_set('loop_start', np.arange(0, faces.size, 3))
    me.update(calc_edges=True)
    o = bpy.data.objects.new('decimate', me)
    bpy.context.collection.objects.link(o)
    weld = o.modifiers.new('weld', 'WELD')
    weld.merge_threshold = 1e-5
    # Collapse in two passes: a single very low ratio stalls on thin rims.
    for ratio in (min(1, (budget / len(faces)) ** .5), min(1, (budget / len(faces)) ** .5)):
        m = o.modifiers.new('collapse', 'DECIMATE')
        m.ratio = ratio
        m.use_collapse_triangulate = True
    deps = bpy.context.evaluated_depsgraph_get()
    ev = o.evaluated_get(deps).to_mesh()
    ev.calc_loop_triangles()
    co = np.empty(len(ev.vertices) * 3)
    ev.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    tri = np.empty(len(ev.loop_triangles) * 3, np.int64)
    ev.loop_triangles.foreach_get('vertices', tri)
    o.evaluated_get(deps).to_mesh_clear()
    bpy.data.objects.remove(o)
    bpy.data.meshes.remove(me)
    return np.stack([co[:, 0], co[:, 2], -co[:, 1]], 1), tri.reshape(-1, 3)


def wheel_part(*args, **kwargs):
    """A part whose decimation ignores mirror symmetry (wheel parts turn, they do not mirror)."""
    import cars
    p = part(*args, **kwargs)
    original = p.mesh

    def mesh(detail=1.):
        saved = cars.decimate
        cars.decimate = decimate_free
        try:
            return original(detail)
        finally:
            cars.decimate = saved
    p.mesh = mesh
    return p


def facing(center, normal):
    """Coordinates (u across, v up, w out) in a plane through center facing normal."""
    n = np.array(normal, float)
    n /= np.linalg.norm(n)
    e1 = np.array([n[2], 0, -n[0]])
    e1 /= np.linalg.norm(e1)
    e2 = np.cross(n, e1)

    def project(x, y, z):
        d = (x - center[0], y - center[1], z - center[2])
        return tuple(d[0] * e[0] + d[1] * e[1] + d[2] * e[2] for e in (e1, e2, n))
    return project


def comet_wheel(radius=.37, width=.30, rim=.284):
    """Low-profile slick with sidewall lettering on a centre-lock split five-spoke rim."""
    half = width / 2
    lo, hi = (-half - .012, -radius - .01, -radius - .01), (half + .012, radius + .01, radius + .01)
    tire = wheel_part(tire_field(radius, width, rim - .006, 'slick', 24, shoulder=.035),
                lo, hi, .003 * DRAFT, RUBBER, RUBBER_BLACK, 2400)

    def lettering(x, y, z):
        r, a = polar(y, z)
        band = .3235
        length = a * band
        cell = np.floor(length / .021)
        local = length - (cell + .5) * .021
        size = .0055 + .0075 * np.mod(np.sin(cell * 12.9898) * 43758.5453, 1)
        blank = np.mod(cell, 7) == 6
        # Two printed arcs, top and bottom of the sidewall.
        arc = np.minimum(np.abs(a - math.pi / 2), np.abs(a + math.pi / 2)) - .85
        glyph = np.where(blank, .01, np.abs(local) - size / 2)
        region = smax(smax(np.abs(r - band) - .0065, glyph, 0), arc * band, 0)
        return smax(region, np.abs(x - half - .0004) - .0016, 0)
    letters = wheel_part(lettering, (half - .01, -radius, -radius), (half + .01, radius, radius), .0015 * DRAFT, GLOSS,
                   (.88, .86, .80), 500)

    face = half - .045

    def spokes(x, y, z):
        r, a = polar(y, z)
        barrel = rect2(x, r, (-.01, rim - .022), (half - .02, .022), .006)
        well = subtract(barrel, rect2(x, r, (-.01, rim - .022), (half - .04, .012), .004), .003)
        step = 2 * math.pi / 5
        u = (np.mod(a, step) - step / 2) * r
        t = np.clip((r - .055) / (rim - .065), 0, 1)
        # Split spokes: two arms that part towards the rim, dished towards the hub.
        arms = np.abs(np.abs(u) - (.006 + .040 * t)) - (.0125 - .004 * t)
        centre = face - .045 * (1 - t) ** 1.5
        slab = np.abs(x - centre + .008) - .011
        spoke = smax(smax(arms, slab, .006), smax(.05 - r, r - rim + .008, 0), .006)
        hub = cylinder_x(x, y, z, (face - .055, 0, 0), .072, .022, .012)
        return union(well, spoke, hub, k=.01)
    rim_part = wheel_part(spokes, lo, hi, .0025 * DRAFT, GLOSS, GRAPHITE, 2200)
    lip = wheel_part(lambda x, y, z: rect2(x, polar(y, z)[0], (half - .03, rim - .008), (.012, .015), .006),
               lo, hi, .0025 * DRAFT, CHROME, SILVER, 600)

    def nut(x, y, z):
        r, a = polar(y, z)
        lobes = .040 + .005 * np.cos(6 * a)
        body = smax(r - lobes, np.abs(x - face + .045) - .03, .006)
        return smax(body, -cylinder_x(x, y, z, (face, 0, 0), .014, .02), .003)
    nut_part = wheel_part(nut, lo, hi, .0025 * DRAFT, GLOSS, NUT, 300)

    disc_r = rim - .058

    def disc(x, y, z):
        r, a = polar(y, z)
        d = subtract(cylinder_x(x, y, z, (-.035, 0, 0), disc_r, .012, .003),
                     cylinder_x(x, y, z, (-.035, 0, 0), .06, .05), .003)
        for ring_r, off in ((disc_r - .03, 0), (disc_r - .062, .5)):
            step = 2 * math.pi / 22
            hu = (np.mod(a + step * off, step) - step / 2) * r
            d = subtract(d, smax(circle2(hu, r, (0, ring_r), .0065), np.abs(x + .035) - .03, 0), .002)
        return d
    disc_part = wheel_part(disc, lo, hi, .003 * DRAFT, METAL, (.40, .40, .41), 600)

    def caliper(x, y, z):
        r, a = polar(y, z)
        # On top of the disc, so the turned-around left wheels match the right.
        arc = np.abs(np.arctan2(np.sin(a - math.pi / 2), np.cos(a - math.pi / 2))) * r - .085
        return smax(smax(arc, np.abs(r - disc_r + .022) - .042, .02), np.abs(x + .03) - .036, .012)
    caliper_part = wheel_part(caliper, lo, hi, .003 * DRAFT, GLOSS + HUB, CALIPER, 300)
    return [tire, letters, rim_part, lip, nut_part, disc_part, caliper_part]


def comet():
    FRONT, REAR, WX, TR, ARCH = 1.04, -1.04, .85, .37, .405
    WY = TR - .04
    P = sdf.Profile

    def mirrored(points):
        return points + [(-x, z) for x, z in reversed(points)][1:-1]

    hull_side = P(sdf.bezier([(1.30, .15), (1.56, .17), (1.665, .29), (1.66, .42), (1.55, .545), (1.25, .645),
                              (.85, .70), (.35, .73), (-.20, .76), (-.80, .79), (-1.25, .81), (-1.55, .845),
                              (-1.645, .79), (-1.655, .58), (-1.63, .32), (-1.55, .20), (-1.20, .16), (0, .15)], 8), .003)
    hull_plan = P(sdf.bezier(mirrored([(0, 1.675), (.42, 1.655), (.68, 1.56), (.82, 1.36), (.875, 1.05),
                                       (.86, .65), (.835, .20), (.835, -.25), (.86, -.65), (.885, -1.05),
                                       (.86, -1.40), (.74, -1.60), (.40, -1.655), (0, -1.66)]), 6), .003)
    cabin_side = P(sdf.bezier([(.48, .66), (.34, .72), (.06, .92), (-.22, 1.11), (-.52, 1.155), (-.88, 1.10),
                               (-1.22, .95), (-1.50, .84), (-1.52, .70), (-.50, .62), (.40, .62)], 8), .003)
    side_glass = P(rounded_polygon([(.24, .775), (-.17, 1.075), (-.58, 1.10), (-.97, 1.00), (-1.10, .86),
                                    (-1.0, .805)], .04))
    front_glass = P(rounded_polygon([(-.56, .755), (.56, .755), (.44, 1.085), (-.44, 1.085)], .05))
    rear_glass = P(rounded_polygon([(-.40, -.80), (.40, -.80), (.30, -1.32), (-.30, -1.32)], .05))

    def cabin_width(y, z):
        return .66 - .38 * (y - .74) - .10 * smoothstep(-.6, -1.4, z)

    def cabin(x, y, z):
        return smax(cabin_side(z, y + .12 * x * x), (np.abs(x) - cabin_width(y, z)) / 1.07, .09)

    def shell(x, y, z):
        ax = np.abs(x)
        tumble = .5 * relu(.30 - y, .04) + .30 * relu(y - .52, .04)
        d = smax(hull_side(z, y + .05 * x * x), hull_plan(ax + tumble, z), .09)
        # Front fenders peak above the bonnet; the rear haunches swell over the wheels.
        # Both are trimmed square to the nose and tail so they never bulge past them.
        d = smin(d, smax(ellipsoid(ax, y, z, (.71, .46, FRONT - .01), (.28, .38, .66)), z - 1.60, .08), .10)
        d = smin(d, smax(ellipsoid(ax, y, z, (.74, .48, REAR + .04), (.26, .42, .82)), -1.58 - z, .08), .12)
        d = smin(d, cabin(x, y, z), .05)
        # Aero mirrors on short stalks.
        d = smin(d, box(ax, y, z, (.80, .835, .235), (.07, .035, .045), .03), .006)
        d = smin(d, capsule(ax, y, z, (.64, .78, .27), (.76, .82, .245), .016), .01)
        for zc in (FRONT, REAR):
            d = subtract(d, cylinder_x(ax, y, z, (1.06, WY, zc), ARCH, .42), .02)
        return d

    def windows(x, y, z):
        ax = np.abs(x)
        s = smax(side_glass(z, y), cabin_width(y, z) - .10 - ax, 0)
        f = smax(front_glass(x, y), -(z + .35), 0)
        r = smax(rear_glass(x, z), .84 - y, 0)
        # Only the canopy's own surface: never the fenders the outlines sweep over.
        return smax(np.minimum(np.minimum(s, f), r), cabin(x, y, z) - .015, 0)

    def front_face(region):
        return lambda x, y, z: smax(region(np.abs(x), y), 1.2 - z, 0)

    def rear_face(region):
        return lambda x, y, z: smax(region(np.abs(x), y), z + 1.4, 0)

    # Teardrop lamps let into the fronts of the fenders, centred where the
    # aiming line from inside the fender meets the paint.
    lamp_normal = np.array((.32, .42, 1.0)) / np.linalg.norm((.32, .42, 1.0))
    start = np.array((.62, .53, 1.44))
    t = np.linspace(0, .4, 801)
    line = start + t[:, None] * lamp_normal
    lamp_center = line[np.argmax(shell(line[:, 0], line[:, 1], line[:, 2]) > 0)]
    lamp = facing(lamp_center, lamp_normal)

    def head(x, y, z):
        u, v, w = lamp(np.abs(x), y, z)
        oval = (np.sqrt((u / .115) ** 2 + (v / .064) ** 2) - 1) * .064
        return smax(oval, -.10 - w, 0)
    mouth = front_face(lambda u, v: rect2(u, v, (0, .28), (.50, .075), .05))
    tails = [(.62, .62, .065), (.43, .62, .055)]
    tail = rear_face(lambda u, v: np.minimum(circle2(u, v, tails[0][:2], tails[0][2]),
                                             circle2(u, v, tails[1][:2], tails[1][2])))
    plate = rear_face(lambda u, v: rect2(u, v, (0, .48), (.19, .05), .012))

    side_intake = P(rounded_polygon([(-.40, .34), (-.40, .60), (-.56, .62), (-.61, .36)], .04))

    def scoop(x, y, z):
        return smax(side_intake(z, y), .6 - np.abs(x), 0)

    def handle(x, y, z):
        return smax(rect2(z, y, (-.30, .66), (.045, .012), .01), .6 - np.abs(x), 0)

    POCKETS = ((head, .035), (mouth, .06), (tail, .02), (plate, .006), (scoop, .05), (handle, .008))

    def body(x, y, z):
        ref = shell(x, y, z)
        d = pocket(ref, ref, windows(x, y, z), .012, .005)
        for region, depth in POCKETS:
            d = pocket(d, ref, region(x, y, z), depth, .004)
        return d

    def glass(x, y, z):
        c = cabin(x, y, z)
        return smax(smax(c + .007, windows(x, y, z) - .015, .002), -(c + .025), 0)

    FLOORS = {'reflector': (head, .03), 'mouth': (mouth, .055), 'plate': (plate, .002), 'scoop': (scoop, .045)}

    def floor(name):
        region, at = FLOORS[name]
        return lambda x, y, z: inlay(shell(x, y, z), region(x, y, z), at, .022)

    def underside(c, n):
        ax = np.abs(c[:, 0])
        well = np.zeros(len(c), bool)
        for zc in (FRONT, REAR):
            dy, dz = c[:, 1] - WY, c[:, 2] - zc
            r = np.hypot(dy, dz)
            inward = -(n[:, 1] * dy + n[:, 2] * dz) / np.maximum(r, 1e-6)
            # Only inside the cut: its curved roof, and its inner wall at x = .64.
            well |= (r < ARCH + .01) & (ax > .632) & (ax < 1.0) & ((inward > .4) | ((ax < .65) & (np.abs(n[:, 0]) > .6)))
        return well | ((n[:, 1] < -.55) & (c[:, 1] < .25))

    V = .004 * DRAFT
    car = Car('comet', comet_wheel(TR), WX, WY, (FRONT, REAR))
    add = car.parts.append
    add(part(body, (-1.06, -.02, -1.72), (1.06, 1.24, 1.74), V, PAINT, (1, 1, 1), 44000,
             lambda c, n, k, col: face_rule(c, n, k, col, [(underside, TRIM, INK)])))
    add(part(glass, (-.74, .66, -1.45), (.74, 1.20, .50), V, GLASS, TINT, 6000))

    # Headlamps: a chrome reflector bowl around one big domed lens each.
    front_lo, front_hi = (-.82, .14, 1.20), (.82, .70, 1.74)
    add(part(floor('reflector'), front_lo, front_hi, .003 * DRAFT, CHROME, SILVER, 800))
    lens_center = lamp_center - lamp_normal * .028

    def lenses_front(x, y, z):
        dome = sdf.ellipsoid(np.abs(x), y, z, lens_center, (.047, .047, .047))
        return smax(dome, shell(x, y, z) + .010, .003)
    add(part(lenses_front, front_lo, front_hi, .0025 * DRAFT, HEADLAMP, (1, .97, .92), 900))

    def lens_rings(x, y, z):
        u, v, w = lamp(np.abs(x), y, z)
        return smax(np.abs(np.hypot(u, v) - .051) - .0055, np.abs(w + .02) - .012, .003)
    add(part(lens_rings, front_lo, front_hi, .002 * DRAFT, CHROME, SILVER, 900))
    add(part(floor('mouth'), front_lo, front_hi, .003 * DRAFT, TRIM, INK, 300))

    def mouth_slats(x, y, z):
        ref = shell(x, y, z)
        slats = np.minimum(np.abs(y - .28) - .006, np.minimum(np.abs(np.abs(x) - .17), np.abs(np.abs(x) - .34)) - .006)
        return smax(smax(smax(slats, mouth(x, y, z) + .002, .002), ref + .014, .003), -(ref + .06), 0)
    add(part(mouth_slats, front_lo, front_hi, .003 * DRAFT, TRIM, (.10, .105, .11), 900))

    # Splitter, canards, side skirts and the diffuser with its fins.
    def aero(x, y, z):
        ax = np.abs(x)
        outline = hull_plan(ax, z)
        splitter = smax(smax(outline - .045, np.abs(y - .155) - .014, .006), 1.18 - z, .02)
        u, v = ax - .79, y - .36
        cu, cv = math.cos(.32), math.sin(.32)
        canard = smax(box(u * cu + v * cv, -u * cv + v * cu, z, (0, 0, 1.40), (.055, .007, .065), .005),
                      .70 - ax, .004)
        skirt = smax(smax(outline - .014, np.abs(y - .19) - .04, .01), np.abs(z) - .60, .02)
        diffuser = smax(smax(outline + .004, np.abs(y - .25) - .09, .01), -(z + 1.30), .02)
        for xc in (0, .26, .52):
            diffuser = subtract(diffuser, box(ax, y, z, (xc, .19, -1.75), (.10, .07, .32), .015), .006)
        return union(splitter, canard, skirt, diffuser)
    add(part(aero, (-1.0, .10, -1.74), (1.0, .46, 1.76), .004 * DRAFT, TRIM, INK, 4200))

    # Side scoops ahead of the rear wheels.
    add(part(floor('scoop'), (-1.0, .28, -.70), (1.0, .70, -.32), .003 * DRAFT, TRIM, INK, 300))

    def scoop_slat(x, y, z):
        ref = shell(x, y, z)
        return smax(smax(smax(np.abs(y - .47) - .006, scoop(x, y, z) + .002, .002), ref + .01, .003), -(ref + .05), 0)
    add(part(scoop_slat, (-1.0, .28, -.70), (1.0, .70, -.32), .003 * DRAFT, TRIM, (.10, .105, .11), 300))

    # Rear: four round tail lamps in chrome bezels, the plate, quad exhausts.
    rear_lo, rear_hi = (-.80, .36, -1.72), (.80, .80, -1.40)

    def lenses(x, y, z):
        ax = np.abs(x)
        domes = np.minimum(ellipsoid(ax, y, z, (.62, .62, -1.58), (.058, .058, .08)),
                           ellipsoid(ax, y, z, (.43, .62, -1.58), (.048, .048, .08)))
        return smax(domes, shell(x, y, z) + .003, .003)
    add(part(lenses, rear_lo, rear_hi, .0025 * DRAFT, TAILLAMP, (.88, .06, .04), 900))

    def bezels(x, y, z):
        ax, ref = np.abs(x), shell(x, y, z)
        rings = np.minimum(np.abs(circle2(ax, y, tails[0][:2], tails[0][2])),
                           np.abs(circle2(ax, y, tails[1][:2], tails[1][2]))) - .0065
        return smax(rings, np.abs(ref + .0005) - .0045, .0025)
    add(part(bezels, rear_lo, rear_hi, .002 * DRAFT, CHROME, SILVER, 1800))
    add(part(floor('plate'), rear_lo, rear_hi, .003 * DRAFT, GLOSS, (.93, .93, .90), 200))

    def exhausts(x, y, z):
        ax = np.abs(x)
        tubes = cylinder_z(ax, y, z, (.17, .30, -1.64), .05, .07, .01)
        bores = cylinder_z(ax, y, z, (.17, .30, -1.70), .038, .07)
        return subtract(tubes, bores, .005)
    add(part(exhausts, (-.32, .24, -1.74), (.32, .38, -1.50), .0025 * DRAFT, CHROME, SILVER, 1000))

    # Swan-neck rear wing: carbon plane and endplates hanging from two uprights.
    def wing(x, y, z):
        ax = np.abs(x)
        u, v = z + 1.50, y - 1.12 - (z + 1.50) * math.tan(math.radians(8))
        plane = smax(rect2(u, v, (0, 0), (.15, .018), .017), ax - .80, .01)
        plates = box(ax, y, z, (.812, 1.10, -1.51), (.008, .075, .19), .006)
        neck = smax(sdf.polyline2(z, y, [(-1.36, .78), (-1.38, 1.00), (-1.42, 1.15), (-1.48, 1.175),
                                         (-1.55, 1.155)]) - .02, np.abs(ax - .30) - .008, .004)
        return union(plane, plates, neck, k=.006)
    add(part(wing, (-.86, .72, -1.74), (.86, 1.24, -1.26), .003 * DRAFT, TRIM, (.06, .062, .066), 3200))

    # Mirror glass, door handles and the racing fuel filler on the right haunch.
    add(part(lambda x, y, z: smax(box(np.abs(x), y, z, (.80, .835, .19), (.058, .026, .02), .014), z - .197, .002),
             (-.92, .76, .12), (.92, .92, .32), .0025 * DRAFT, CHROME, SILVER, 400))

    def handle_bar(x, y, z):
        ref = shell(x, y, z)
        bar = smax(rect2(z, y, (-.30, .66), (.033, .005), .005), .6 - np.abs(x), 0)
        return smax(smax(ref + .002, -(ref + .008), 0), bar, .002)
    add(part(handle_bar, (-.92, .60, -.38), (.92, .72, -.22), .002 * DRAFT, CHROME, SILVER, 300))
    def cap(cx, cz, radius, mirror=False):
        def field(x, y, z):
            b = body(x, y, z)
            disc = np.hypot((np.abs(x) if mirror else x) - cx, z - cz) - radius
            return smax(disc, smax(-(b + .0015), b - .0035, 0), .0015)
        return field
    add(part(cap(.62, -.72, .05), (.52, .70, -.82), (.72, .98, -.62), .002 * DRAFT, CHROME, SILVER, 400))
    add(part(cap(.42, 1.47, .02, True), (-.48, .40, 1.40), (.48, .70, 1.54), .002 * DRAFT, CHROME, SILVER, 300))

    # A single racing wiper parked at the base of the screen, and a red tow strap.
    def wiper(x, y, z):
        arm = capsule(x, y, z, (-.40, .747, .285), (.18, .762, .285), .008)
        return smax(arm, -(cabin(x, y, z) + .002), .002)
    add(part(wiper, (-.48, .68, .22), (.26, .84, .35), .002 * DRAFT, TRIM, INK, 400))
    add(part(lambda x, y, z: sdf.torus_x(x, y, z, (-.60, .25, 1.585), .032, .0075),
             (-.66, .18, 1.52), (-.54, .32, 1.65), .002 * DRAFT, GLOSS, (.80, .07, .05), 300))

    # Livery: twin Le Mans stripes from nose to tail, door roundels with an 8.
    for s in (1, -1):
        stripe = [lambda x, y, z, s=s: s * x - .155, lambda x, y, z, s=s: .035 - s * x]
        car.decals.append((stripe + [lambda x, y, z: .40 - y, lambda x, y, z: -z, lambda x, y, z: z - 1.70],
                           GLOSS, IVORY))
        car.decals.append((stripe + [lambda x, y, z: .74 - y, lambda x, y, z: z, lambda x, y, z: -1.70 - z],
                           GLOSS, IVORY))
        car.decals.append((convex((2, 1), ring((-.06, .47), .155), .6 * s), GLOSS, IVORY))

    def number(x, y, z):
        top = np.abs(circle2(z, y, (-.06, .516), .040)) - .0105
        bottom = np.abs(circle2(z, y, (-.06, .423), .050)) - .0115
        return smax(np.minimum(top, bottom), .6 - np.abs(x), 0)
    car.decals.append((number, GLOSS, INK, .0028))

    # Panel gaps: doors, the clamshell bonnet and the engine lid.
    gap = (.02, .02, .022)
    for side in (.6, -.6):
        for line in ([(.30, .245), (.30, .765)], [(-.42, .245), (-.42, .80)], [(.30, .245), (-.42, .245)]):
            car.decals.append((strip((2, 1), *line, .0018, side), TRIM, gap, .0025))
    # Shut lines run only where the paint faces the projection, so they keep their width.
    for a, b, floor_y in (((-.40, .42), (.40, .42), .45), ((-.38, 1.52), (.38, 1.52), .45),
                          ((-.38, -1.25), (.38, -1.25), .58), ((-.38, -1.25), (-.38, -1.56), .58),
                          ((.38, -1.25), (.38, -1.56), .58)):
        car.decals.append((strip((0, 2), a, b, .0018) + [lambda x, y, z, f=floor_y: f - y], TRIM, gap, .0025))
    # Lens centres for renderer.c: the headlamp where its aiming line meets the paint,
    # the tail lamps midway between each side's pair on the tail face.
    zs = np.linspace(-1.40, -1.80, 801)
    tail_z = zs[np.argmax(shell(np.full_like(zs, .525), np.full_like(zs, .62), zs) > 0)]
    car.lamps = {'head': tuple(float(round(v, 3)) for v in lamp_center), 'tail': (.525, .62, float(round(tail_z, 3)))}
    return car
