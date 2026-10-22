"""Rolling stock for the railways and the seaside road train, modeled like the cars.

Every vehicle is built from signed distance parts in tools/cars.py: smooth
pieces meshed per material, decimated, lining and numbers clipped onto the
finished paint, ambient occlusion baked into the vertex colours. Body panels
marked PAINT take the circuit's livery at runtime; everything else names its
own colour.

Game axes: x across, y up from the rail tops, z forward. The exported extents
match the RailCar table in src/railway.c, so buffers and drawbars still touch,
and the lamp lenses sit where RAIL_HEADLAMPS in src/renderer.c puts the halos.
Standard-gauge rails run at x = +-.72, the alpine line's at +-.5.
"""
import math
import os

import numpy as np

import sdf
from sdf import smin, smax, union, subtract, box, rect2, circle2, ellipsoid, capsule
from sdf import cylinder_x, cylinder_y, cylinder_z, rounded_polygon
import cars
from cars import (Mesh, bake_ao, clip_decal, convex, export_tcm, blender_object, empty_scene, ROOT,
                  PAINT, GLASS, CHROME, RUBBER, TRIM, GLOSS, HEADLAMP, TAILLAMP, METAL, INK, TINT)
from flora import fbm

def decimate_free(points, faces, budget):
    """Quadric collapse in two gentler passes without forced mirror symmetry: the long
    vehicle bodies tear when the symmetric collapse cannot pair their vertices."""
    import bpy
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
    ratio = min(1, budget / len(faces)) ** .5
    for k in range(2):
        m = o.modifiers.new('collapse %d' % k, 'DECIMATE')
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


class Part(cars.Part):
    def mesh(self, detail=1.):
        saved, cars.decimate = cars.decimate, decimate_free
        try:
            return super().mesh(detail)
        finally:
            cars.decimate = saved


def part(*args, **kwargs):
    return Part(*args, **kwargs)


BLACK = (.055, .06, .065)
BEAM_RED = (.72, .12, .08)
BRASS = (.86, .64, .28)
STEEL = (.46, .47, .49)
GREY = (.20, .21, .23)
CREAM = (.92, .89, .78)
WHITE = (.93, .93, .90)
YELLOW = (.98, .74, .14)
TURQUOISE = (.08, .58, .62)
COAL = (.04, .04, .045)
ROOF = (.38, .40, .42)
WOOD = (.55, .31, .15)
LAMP = (1, .96, .86)
GAUGE = .72


def assemble(parts, decals=(), reach=.8):
    """Meshes parts, prints decals (part index, region, code, colour[, lift]) and bakes occlusion."""
    meshes = [p.mesh() for p in parts]
    printed = [clip_decal(meshes[i], region, code, colour, *rest) for i, region, code, colour, *rest in decals]
    return bake_ao(Mesh.join(meshes + printed), [(p.points, p.faces) for p in parts], rays=40, reach=reach)


def glazing(shell, region, lo, hi, voxel, budget, inset=.022):
    """Panes set into a body: the shell's skin `inset` deep inside region, shaded with the
    shell's own normals so decimation cannot streak the reflections."""
    def field(x, y, z):
        s = shell(x, y, z)
        # The pane reaches under the surrounding paint, so its trimmed edge stays hidden.
        return smax(smax(s + inset, -(s + inset + .018), 0), region(x, y, z) - .025, .002)
    pane = part(field, lo, hi, voxel, GLASS, TINT, budget, hidden=lambda x, y, z: shell(x, y, z) + inset + .008)
    pane.field = shell
    return pane


def x_band(lo, hi):
    return [lambda x, y, z: lo - y, lambda x, y, z: y - hi]


def side_face(sign, depth):
    """Selects surfaces on one flank (sign * x beyond depth)."""
    return lambda x, y, z: depth - sign * x


def end_face(sign, depth):
    return lambda x, y, z: depth - sign * z


# --- Running gear ------------------------------------------------------------------------------
def rail_wheel(x, y, z, cx, cy, cz, r, width=.13, spokes=0, counterweight=False):
    """A flanged rail wheel on both sides, its tread centred at |x| = cx on the axle (cy, cz)."""
    ax = np.abs(x)
    tread = cylinder_x(ax, y, z, (cx, cy, cz), r, width / 2, .012)
    flange = cylinder_x(ax, y, z, (cx - width / 2 - .01, cy, cz), r + .045, .014, .008)
    d = smin(tread, flange, .01)
    rr = np.sqrt((y - cy) ** 2 + (z - cz) ** 2)
    # A raised hub boss; large wheels also get a dished face.
    if r > .6:
        d = subtract(d, smax(rr - r * .86, -(ax - cx - width / 2 + .025), .01), .01)
    d = smin(d, cylinder_x(ax, y, z, (cx + width / 2 - .01, cy, cz), r * .22, .03, .015), .01)
    if spokes:
        step = 2 * math.pi / spokes
        a = np.arctan2(y - cy, z - cz)
        u = (np.mod(a, step) - step / 2) * rr
        hole = smax(smax(r * .3 - rr, rr - r * .82, .02), .03 + .02 * rr / r - np.abs(u), .015)
        if counterweight:
            # The balance weight fills a crescent opposite the crank.
            hole = smax(hole, -(np.abs(np.arctan2(np.sin(a + math.pi / 2), np.cos(a + math.pi / 2))) - .9) * rr, .02)
        d = subtract(d, smax(hole, np.abs(ax - cx) - width, .005), .008)
    return d


def tyre(x, y, z, cx, cy, cz, r, width=.13):
    """The bright steel running band of a wheel."""
    ax = np.abs(x)
    rr = np.sqrt((y - cy) ** 2 + (z - cz) ** 2)
    return smax(smax(np.abs(rr - r + .03) - .035, np.abs(ax - cx) - width / 2 - .004, .008), -(r + .05 - rr), 0)


def buffers(x, y, z, zend, sign, cy=1.0, spread=.85, head=.2):
    """A pair of sprung buffers whose heads end at z = zend (sign: +1 at the front)."""
    ax = np.abs(x)
    stock = cylinder_z(ax, y, z, (spread, cy, zend - sign * .22), .09, .16, .02)
    collar = cylinder_z(ax, y, z, (spread, cy, zend - sign * .37), .13, .03, .015)
    plate = cylinder_z(ax, y, z, (spread, cy, zend - sign * .03), head, .03, .025)
    return union(stock, collar, plate, k=.015)


def coupling(x, y, z, zend, sign, cy=1.0):
    """The hook between the buffers, reaching zend."""
    hook = box(x, y, z, (0, cy, zend - sign * .12), (.05, .07, .12), .03)
    link = sdf.torus_x(z, y, x, (zend - sign * .02, cy - .06, 0), .07, .022)
    return smin(hook, link, .02)


def bogie(x, y, z, cz, half_gauge=GAUGE, r=.42, base=1.6):
    """A two-axle bogie: side frames with axle boxes and leaf springs, outside the wheels."""
    ax = np.abs(x)
    fx = half_gauge + .16
    frame = box(ax, y, z, (fx, r + .08, cz), (.05, .13, base / 2 + .35), .04)
    for dz in (-base / 2, base / 2):
        frame = smin(frame, box(ax, y, z, (fx + .03, r, cz + dz), (.08, .12, .13), .03), .03)
    spring = box(ax, y, z, (fx + .02, r + .22, cz), (.07, .05, .42), .04)
    bolster = box(x, y, z, (0, r + .22, cz), (fx, .07, .2), .04)
    return union(frame, spring, bolster, k=.03)


def bogie_wheels(x, y, z, cz, half_gauge=GAUGE, r=.42, base=1.6):
    return np.minimum(rail_wheel(x, y, z, half_gauge, r, cz - base / 2, r),
                      rail_wheel(x, y, z, half_gauge, r, cz + base / 2, r))


def window_row(x, y, z, sign, centres, half, cy, r=.08):
    """Rounded window openings on one flank, as a region in (z, y)."""
    d = None
    for c in centres:
        w = rect2(z, y, (c, cy), half, r)
        d = w if d is None else np.minimum(d, w)
    return d


# --- Steam: locomotive, tender, coach --------------------------------------------------------
def steam_locomotive():
    DRIVERS = (-1.1, .25, 1.6)
    R = .74

    def boiler(x, y, z):
        barrel = cylinder_z(x, y, z, (0, 2.12, .75), .82, 1.85, .04)
        sand = smin(cylinder_y(x, y, z, (0, 2.85, .1), .3, .16, .06), ellipsoid(x, y, z, (0, 3.0, .1), (.3, .14, .3)), .04)
        d = smin(barrel, sand, .12)
        # Splashers over the driving wheels and the cab sides and front.
        for zc in DRIVERS:
            d = smin(d, smax(cylinder_x(np.abs(x), y, z, (GAUGE, R, zc), R + .1, .13, .02), 1.42 - y, .02), .02)
        cab = box(x, y, z, (0, 2.3, -2.35), (1.3, 1.08, 1.05), .07)
        cab = subtract(cab, box(x, y, z, (0, 2.45, -2.6), (1.18, 1.2, 1.12), .03), .02)
        spectacles = circle2(np.abs(x), y, (.62, 2.95), .26)
        cab = subtract(cab, smax(spectacles, np.abs(z + 1.4) - .12, .01), .02)
        opening = rect2(z, y, (-2.45, 2.78), (.55, .52), .14)
        cab = subtract(cab, smax(opening, 1.0 - np.abs(x), .01), .02)
        return smin(d, cab, .05)

    def black_work(x, y, z):
        ax = np.abs(x)
        frame = box(ax, y, z, (.56, .98, .1), (.06, .32, 3.45), .03)
        board = box(x, y, z, (0, 1.42, 1.0), (1.45, .04, 2.5), .02)
        smokebox = cylinder_z(x, y, z, (0, 2.12, 2.97), .86, .38, .05)
        door = smax(ellipsoid(x, y, z, (0, 2.12, 3.33), (.66, .66, .12)), 3.33 - z, .02)
        saddle = box(x, y, z, (0, 1.6, 2.97), (.62, .35, .35), .08)
        chimney = smin(cylinder_y(x, y, z, (0, 3.3, 2.85), .25, .5, .03),
                       cylinder_y(x, y, z, (0, 3.86, 2.85), .36, .08, .05), .06)
        roof = smax(sdf.rect2(x, y - .06 * x * x, (0, 3.52), (1.45, .06), .05), np.abs(z + 2.4) - 1.3, .04)
        rear = box(x, y, z, (0, 1.05, -3.45), (1.25, .2, .08), .04)
        firebox = box(x, y, z, (0, 1.78, -1.3), (.72, .42, .25), .1)
        return union(frame, board, smokebox, door, saddle, chimney, roof, rear, firebox, k=.04)

    def beams(x, y, z):
        front = box(x, y, z, (0, 1.0, 3.53), (1.4, .28, .12), .04)
        valance = smax(box(np.abs(x), y, z, (1.43, 1.36, 1.0), (.03, .08, 2.5), .02), 1.0 - np.abs(x), .01)
        return union(front, valance, k=.02)

    def cylinders(x, y, z):
        ax = np.abs(x)
        barrel = cylinder_z(ax, y, z, (1.08, 1.0, 2.55), .33, .52, .06)
        valve = cylinder_z(ax, y, z, (1.02, 1.42, 2.55), .14, .45, .04)
        return smin(barrel, valve, .05)

    def wheels(x, y, z):
        d = None
        for zc in DRIVERS:
            w = rail_wheel(x, y, z, GAUGE, R, zc, R, .14, 16, True)
            d = w if d is None else np.minimum(d, w)
        return np.minimum(d, rail_wheel(x, y, z, GAUGE, .42, 3.0, .42, .12, 10))

    def tyres(x, y, z):
        d = None
        for zc in DRIVERS:
            t = tyre(x, y, z, GAUGE, R, zc, R, .14)
            d = t if d is None else np.minimum(d, t)
        return d

    def motion(x, y, z):
        ax = np.abs(x)
        rod = box(ax, y, z, (.88, .62, .25), (.025, .055, 1.45), .02)
        main = capsule(ax, y, z, (.92, .64, .25), (.92, 1.0, 2.1), .045)
        pins = None
        for zc in DRIVERS:
            pin = cylinder_x(ax, y, z, (.88, .62, zc + .02), .07, .05, .02)
            pins = pin if pins is None else np.minimum(pins, pin)
        crosshead = box(ax, y, z, (.94, 1.0, 2.1), (.05, .08, .18), .02)
        slide = box(ax, y, z, (.94, 1.0, 2.0), (.025, .03, .55), .01)
        return union(rod, main, pins, crosshead, slide, k=.015)

    def brass(x, y, z):
        d = smin(cylinder_y(x, y, z, (0, 2.85, 1.2), .36, .16, .06), ellipsoid(x, y, z, (0, 3.02, 1.2), (.36, .2, .36)), .05)
        whistle = smin(cylinder_y(x, y, z, (0, 3.05, -1.05), .045, .2, .01),
                       cylinder_y(x, y, z, (0, 3.32, -1.05), .08, .1, .03), .02)
        valves = cylinder_y(x, y, z, (0, 3.0, -.55), .1, .18, .03)
        rails = capsule(np.abs(x), y, z, (.72, 2.62, -1.0), (.72, 2.62, 2.5), .022)
        knobs = np.minimum.reduce([capsule(np.abs(x), y, z, (.66, 2.58, zc), (.72, 2.62, zc), .02) for zc in (-.6, .9, 2.3)])
        return union(d, whistle, valves, rails, knobs, k=.01)

    def bands(x, y, z):
        d = None
        for zc in (-.3, .55, 1.6):
            band = smax(np.abs(np.sqrt(x * x + (y - 2.12) ** 2) - .83) - .014, np.abs(z - zc) - .045, .006)
            d = band if d is None else np.minimum(d, band)
        return d

    def lamp_bodies(x, y, z):
        top = box(x, y, z, (0, 3.0, 3.39), (.17, .15, .1), .04)
        low = box(np.abs(x), y, z, (.7, 1.55, 3.58), (.12, .12, .09), .035)
        return np.minimum(top, low)

    def lenses(x, y, z):
        top = smax(cylinder_z(x, y, z, (0, 3.0, 3.49), .1, .02, .01), -(lamp_bodies(x, y, z) + .002), .002)
        low = smax(cylinder_z(np.abs(x), y, z, (.7, 1.55, 3.68), .075, .02, .01), -(lamp_bodies(x, y, z) + .002), .002)
        return np.minimum(top, low)

    def glazing(x, y, z):
        spectacles = smax(circle2(np.abs(x), y, (.62, 2.95), .27), np.abs(z + 1.4) - .03, .005)
        return spectacles

    def chrome(x, y, z):
        d = np.minimum(buffers(x, y, z, 4.05, 1, 1.0, .85), coupling(x, y, z, 3.75, -1, 1.0) + 0)
        handle = cylinder_z(x, y, z, (0, 2.12, 3.47), .05, .03, .01)
        return np.minimum(d, handle)

    lo, hi = (-1.6, -.05, -3.9), (1.6, 4.1, 4.15)
    parts = [part(boiler, lo, hi, .014, PAINT, (1, 1, 1), 3600),
             part(black_work, lo, hi, .014, TRIM, BLACK, 2600),
             part(beams, (-1.55, .6, -.1), (1.55, 1.5, 3.8), .012, GLOSS, BEAM_RED, 500),
             part(cylinders, (-1.5, .55, 1.9), (1.5, 1.7, 3.2), .012, PAINT, (1, 1, 1), 600),
             part(wheels, (-1.0, -.05, -2.0), (1.0, 1.55, 3.6), .007, GLOSS, BEAM_RED, 4000),
             part(tyres, (-1.0, -.05, -2.0), (1.0, 1.55, 2.5), .01, METAL, STEEL, 700),
             part(motion, (-1.05, .45, -1.4), (1.05, 1.2, 2.7), .01, CHROME, STEEL, 900),
             part(brass, (-.85, 2.4, -1.2), (.85, 3.45, 2.6), .01, CHROME, BRASS, 900),
             part(bands, (-.9, 1.25, -.4), (.9, 3.0, 1.7), .008, CHROME, BRASS, 600),
             part(lamp_bodies, (-.9, 1.3, 3.2), (.9, 3.25, 3.75), .008, TRIM, BLACK, 300),
             part(lenses, (-.9, 1.3, 3.3), (.9, 3.25, 3.75), .006, HEADLAMP, LAMP, 200),
             part(glazing, (-1.0, 2.5, -1.5), (1.0, 3.3, -1.3), .008, GLASS, TINT, 160),
             part(chrome, (-1.15, .7, -3.9), (1.15, 1.3, 4.1), .01, CHROME, STEEL, 900)]
    decals = []
    for s in (1, -1):
        # Lining below the cab opening and a brass number plate.
        decals.append((0, convex((2, 1), [(-3.3, 1.4), (-1.4, 1.4), (-1.4, 2.08), (-3.3, 2.08)]) +
                       [side_face(s, 1.25)], GLOSS, CREAM))
        decals.append((0, convex((2, 1), [(-3.25, 1.45), (-1.45, 1.45), (-1.45, 2.03), (-3.25, 2.03)]) +
                       [side_face(s, 1.25)], PAINT, (1, 1, 1), .003))
        decals.append((0, convex((2, 1), [(-2.65, 1.6), (-2.05, 1.6), (-2.05, 1.88), (-2.65, 1.88)]) +
                       [side_face(s, 1.25)], CHROME, BRASS, .005))
    return assemble(parts, decals, 1.0)


def steam_tender():
    def tank(x, y, z):
        body = box(x, y, z, (0, 1.95, 0), (1.35, .78, 2.45), .1)
        flare = smax(box(x, y, z, (0, 2.78, .2), (1.38, .1, 2.2), .04),
                     -box(x, y, z, (0, 2.8, .2), (1.28, .2, 2.1), .03), .02)
        tools = box(x, y, z, (0, 2.95, -1.95), (1.15, .25, .3), .05)
        return union(body, flare, tools, k=.04)

    def coal(x, y, z):
        heap = smax(box(x, y, z, (0, 2.62, .45), (1.22, .45, 1.9), .1),
                    y - (3.05 - .25 * (z - .6) ** 2 / 4 - .06 * x * x) - .07 * fbm(x * 4, y * 4, z * 4, 3), .1)
        return smax(heap, box(x, y, z, (0, 2.62, .45), (1.25, .6, 1.95)), 0)

    def frame(x, y, z):
        ax = np.abs(x)
        sole = box(x, y, z, (0, 1.02, 0), (1.15, .16, 2.45), .04)
        hornplates = box(ax, y, z, (GAUGE + .2, .62, 0), (.05, .28, 2.1), .03)
        boxes = None
        for zc in (-1.7, 0, 1.7):
            b = box(ax, y, z, (GAUGE + .24, .48, zc), (.08, .13, .14), .03)
            boxes = b if boxes is None else np.minimum(boxes, b)
        return union(sole, hornplates, boxes, k=.03)

    def beams(x, y, z):
        return np.minimum(box(x, y, z, (0, 1.0, 2.42), (1.3, .25, .08), .03),
                          box(x, y, z, (0, 1.0, -2.42), (1.3, .25, .08), .03))

    def wheels(x, y, z):
        return np.minimum.reduce([rail_wheel(x, y, z, GAUGE, .48, zc, .48, .12, 10) for zc in (-1.7, 0, 1.7)])

    def chrome(x, y, z):
        return np.minimum.reduce([buffers(x, y, z, 2.88, 1), buffers(x, y, z, -2.88, -1),
                                  capsule(np.abs(x), y, z, (1.38, 1.4, -2.3), (1.38, 2.6, -2.3), .025)])
    lo, hi = (-1.5, -.05, -2.6), (1.5, 3.3, 2.6)
    parts = [part(tank, lo, hi, .012, PAINT, (1, 1, 1), 3500),
             part(coal, (-1.35, 2.1, -1.6), (1.35, 3.2, 2.5), .02, TRIM, COAL, 1600),
             part(frame, lo, (1.5, 1.25, 2.6), .012, TRIM, BLACK, 1800),
             part(beams, (-1.4, .7, -2.6), (1.4, 1.3, 2.6), .012, GLOSS, BEAM_RED, 300),
             part(wheels, (-1.0, -.05, -2.3), (1.0, 1.0, 2.3), .01, TRIM, BLACK, 2400),
             part(chrome, (-1.5, .75, -2.95), (1.5, 2.7, 2.95), .01, CHROME, STEEL, 900)]
    decals = []
    for s in (1, -1):
        decals.append((0, convex((2, 1), [(-2.2, 1.35), (2.2, 1.35), (2.2, 2.58), (-2.2, 2.58)]) +
                       [side_face(s, 1.25)], GLOSS, CREAM))
        decals.append((0, convex((2, 1), [(-2.14, 1.41), (2.14, 1.41), (2.14, 2.52), (-2.14, 2.52)]) +
                       [side_face(s, 1.25)], PAINT, (1, 1, 1), .003))
    return assemble(parts, decals, 1.0)


def coach():
    WINDOWS = [-3.55 + k * 1.42 for k in range(6)]

    def shell(x, y, z):
        # A gently tumbled-home body under an arched clerestory roof.
        body = box(x + 0 * y, y, z, (0, 2.25, 0), (1.42 - .04 * np.clip((1.6 - y), 0, 1), 1.18, 4.98), .08)
        return body

    def body(x, y, z):
        s = shell(x, y, z)
        win = window_row(x, y, z, 1, WINDOWS, (.48, .34), 2.92, .1)
        doors = np.minimum(rect2(z, y, (4.55, 2.95), (.2, .24), .06), rect2(z, y, (-4.55, 2.95), (.2, .24), .06))
        openings = smax(np.minimum(win, doors), 1.2 - np.abs(x), .01)
        return subtract(s, smax(openings, -(s + .03), .005), .01)

    def panes(x, y, z):
        win = np.minimum(window_row(x, y, z, 1, WINDOWS, (.5, .36), 2.92, .1),
                         np.minimum(rect2(z, y, (4.55, 2.95), (.22, .26), .06), rect2(z, y, (-4.55, 2.95), (.22, .26), .06)))
        return smax(win, 1.2 - np.abs(x), .01)

    def roof(x, y, z):
        arch = rect2(x, y + .22 * x * x, (0, 3.52), (1.48, .12), .1)
        clerestory = rect2(x, y, (0, 3.72), (.55, .12), .06)
        d = smax(smin(arch, clerestory, .06), np.abs(z) - 5.08, .08)
        return d

    def under(x, y, z):
        frame = box(x, y, z, (0, .98, 0), (1.12, .14, 4.95), .04)
        trussing = smax(box(np.abs(x), y, z, (.9, .78, 0), (.03, .16, 1.8), .02), -(.98 - y), 0)
        gangways = np.minimum(box(x, y, z, (0, 2.3, 5.08), (.6, 1.05, .14), .08), box(x, y, z, (0, 2.3, -5.08), (.6, 1.05, .14), .08))
        bogies = np.minimum(bogie(x, y, z, 3.4), bogie(x, y, z, -3.4))
        beams = np.minimum(box(x, y, z, (0, 1.0, 4.98), (1.3, .2, .08), .03), box(x, y, z, (0, 1.0, -4.98), (1.3, .2, .08), .03))
        return union(frame, trussing, gangways, bogies, beams, k=.03)

    def wheels(x, y, z):
        return np.minimum(bogie_wheels(x, y, z, 3.4), bogie_wheels(x, y, z, -3.4))

    def chrome(x, y, z):
        return np.minimum(buffers(x, y, z, 5.38, 1), buffers(x, y, z, -5.38, -1))

    def handrails(x, y, z):
        return np.minimum.reduce([capsule(np.abs(x), y, z, (1.47, 1.7, zc), (1.47, 2.6, zc), .022)
                                  for zc in (4.25, 4.85, -4.25, -4.85)])
    lo, hi = (-1.6, .6, -5.3), (1.6, 3.95, 5.3)
    parts = [part(body, lo, hi, .014, PAINT, (1, 1, 1), 4600),
             glazing(shell, panes, (-1.5, 2.4, -4.9), (1.5, 3.4, 4.9), .012, 1200),
             part(roof, (-1.6, 3.2, -5.2), (1.6, 3.95, 5.2), .016, GLOSS, ROOF, 1100),
             part(under, (-1.6, -.05, -5.3), (1.6, 3.45, 5.3), .014, TRIM, BLACK, 2500),
             part(wheels, (-1.0, -.05, -4.5), (1.0, .95, 4.5), .01, METAL, STEEL, 1800),
             part(chrome, (-1.15, .75, -5.45), (1.15, 1.25, 5.45), .008, CHROME, STEEL, 900),
             part(handrails, (-1.55, 1.6, -4.95), (1.55, 2.7, 4.95), .008, CHROME, STEEL, 300)]
    decals = []
    for s in (1, -1):
        # Cream upper panels with a gold waist line and panel joints.
        decals.append((0, convex((2, 1), [(-4.95, 2.35), (4.95, 2.35), (4.95, 3.42), (-4.95, 3.42)]) +
                       [side_face(s, 1.25)], GLOSS, CREAM))
        decals.append((0, convex((2, 1), [(-4.95, 2.22), (4.95, 2.22), (4.95, 2.29), (-4.95, 2.29)]) +
                       [side_face(s, 1.25)], CHROME, BRASS))
        for zc in (-4.15, -2.85, -1.42, 0, 1.42, 2.85, 4.15):
            decals.append((0, convex((2, 1), [(zc - .012, 1.25), (zc + .012, 1.25), (zc + .012, 2.18), (zc - .012, 2.18)]) +
                           [side_face(s, 1.25)], TRIM, BLACK, .002))
    return assemble(parts, decals, 1.0)


# --- Alpine railway: box-cab locomotive and panorama coach (gauge +-.5) ----------------------
ALPINE_GAUGE = .5


def alpine_locomotive():
    def shell(x, y, z):
        body = box(x, y, z, (0, 2.25, 0), (1.3 - .05 * np.clip(y - 2.6, 0, 1), 1.28, 4.68), .22)
        # Raked cab ends.
        return smax(body, np.abs(z) - 4.7 + .35 * np.clip(y - 2.5, 0, 1.2), .12)

    def body(x, y, z):
        s = shell(x, y, z)
        screens = smax(rect2(x, y, (0, 2.98), (.92, .42), .12), 4.2 - np.abs(z), .01)
        cabs = smax(rect2(np.abs(z), y, (3.95, 2.95), (.42, .38), .1), 1.15 - np.abs(x), .01)
        grilles = smax(rect2(np.abs(z), y, (1.4, 2.7), (.8, .4), .06), 1.15 - np.abs(x), .01)
        d = subtract(s, smax(np.minimum(screens, cabs), -(s + .03), .005), .01)
        return subtract(d, smax(grilles, -(s + .02), .004), .006)

    def panes(x, y, z):
        screens = smax(rect2(x, y, (0, 2.98), (.95, .45), .12), 4.2 - np.abs(z), .01)
        cabs = smax(rect2(np.abs(z), y, (3.95, 2.95), (.45, .41), .1), 1.15 - np.abs(x), .01)
        return np.minimum(screens, cabs)

    def grille_slats(x, y, z):
        s = shell(x, y, z)
        slats = np.abs(np.mod(y - 2.7, .1) - .05) - .018
        region = smax(rect2(np.abs(z), y, (1.4, 2.7), (.8, .4), .06), 1.15 - np.abs(x), .01)
        return smax(smax(smax(slats, region + .002, .002), s + .006, .002), -(s + .02), 0)

    def roof(x, y, z):
        deck = box(x, y, z, (0, 3.6, 0), (1.0, .08, 3.6), .04)
        boxes = np.minimum(box(x, y, z, (0, 3.75, 1.7), (.6, .14, .9), .05), box(x, y, z, (0, 3.75, -1.9), (.6, .12, .7), .05))
        under = box(x, y, z, (0, 1.05, 0), (1.1, .16, 4.6), .04)
        nose = np.minimum(box(x, y, z, (0, 1.4, 4.7), (1.15, .3, .1), .06), box(x, y, z, (0, 1.4, -4.7), (1.15, .3, .1), .06))
        bogies = np.minimum(bogie(x, y, z, 2.9, ALPINE_GAUGE, .45, 1.5), bogie(x, y, z, -2.9, ALPINE_GAUGE, .45, 1.5))
        return union(deck, boxes, under, nose, bogies, k=.03)

    def pantograph(x, y, z):
        ax = np.abs(x)
        insulators = np.minimum.reduce([cylinder_y(ax, y, z, (.5, 3.78, zc), .06, .1, .02) for zc in (-.9, -.1)])
        arms = np.minimum.reduce([capsule(ax, y, z, (.45, 3.86, -.9), (.18, 4.4, .05), .03),
                                  capsule(ax, y, z, (.18, 4.4, .05), (.45, 4.82, -.35), .03),
                                  capsule(ax, y, z, (.45, 3.86, -.1), (.18, 4.4, .05), .025)])
        head = box(x, y, z, (0, 4.86, -.35), (.8, .025, .08), .02)
        horns = capsule(ax, y, z, (.8, 4.86, -.35), (.95, 4.78, -.35), .02)
        return union(insulators, arms, head, horns, k=.015)

    def wheels(x, y, z):
        return np.minimum(bogie_wheels(x, y, z, 2.9, ALPINE_GAUGE, .45, 1.5), bogie_wheels(x, y, z, -2.9, ALPINE_GAUGE, .45, 1.5))

    def lamp_bodies(x, y, z):
        ax, az = np.abs(x), np.abs(z)
        low = box(ax, y, z, (.72, 1.68, 4.78), (.18, .13, .06), .04)
        top = box(x, y, az, (0, 3.5, 4.66), (.16, .12, .08), .04)
        return np.minimum(low, top)

    def lenses(x, y, z):
        ax, az = np.abs(x), np.abs(z)
        low = cylinder_z(ax, y, az, (.72, 1.68, 4.85), .1, .02, .01)
        top = cylinder_z(x, y, az, (0, 3.5, 4.76), .085, .02, .01)
        return np.minimum(low, top)

    def chrome(x, y, z):
        d = np.minimum(buffers(x, y, z, 5.23, 1, 1.05, .7, .16), buffers(x, y, z, -5.23, -1, 1.05, .7, .16))
        for zc in (4.2, -4.2):
            d = np.minimum(d, capsule(np.abs(x), y, z, (1.36, 1.6, zc), (1.36, 2.5, zc), .022))
        return d
    lo, hi = (-1.45, .85, -4.95), (1.45, 3.6, 4.95)
    parts = [part(body, lo, hi, .014, PAINT, (1, 1, 1), 4200),
             glazing(shell, panes, lo, hi, .012, 1100),
             part(grille_slats, lo, hi, .012, TRIM, GREY, 900),
             part(roof, (-1.45, -.05, -4.95), (1.45, 4.0, 4.95), .014, TRIM, GREY, 2800),
             part(pantograph, (-1.1, 3.6, -1.2), (1.1, 5.0, .4), .008, METAL, (.25, .26, .28), 1200),
             part(wheels, (-.8, -.05, -4.0), (.8, 1.0, 4.0), .008, METAL, STEEL, 2600),
             part(lamp_bodies, (-1.1, 1.4, -4.95), (1.1, 3.75, 4.95), .008, GLOSS, WHITE, 400),
             part(lenses, (-1.1, 1.4, -4.95), (1.1, 3.75, 4.95), .006, HEADLAMP, LAMP, 300),
             part(chrome, (-1.45, .8, -5.3), (1.45, 2.6, 5.3), .01, CHROME, STEEL, 800)]
    decals = []
    for s in (1, -1):
        # A white band low on the flanks and round each nose, and a cream number.
        decals.append((0, x_band(1.42, 1.62), GLOSS, WHITE))
        decals.append((0, convex((2, 1), [(-.35, 1.85), (.35, 1.85), (.35, 2.15), (-.35, 2.15)]) +
                       [side_face(s, 1.1)], GLOSS, WHITE))
    return assemble(parts, decals, 1.0)


def alpine_coach():
    PILLARS = [-4.5 + k * 1.5 for k in range(7)]

    def shell(x, y, z):
        return box(x, y, z, (0, 2.45, 0), (1.3 - .1 * np.clip(y - 2.4, 0, 1.5), 1.45, 5.3), .32)

    def windows(x, y, z):
        ax = np.abs(x)
        side = np.minimum.reduce([rect2(z, y, ((a + b) / 2, 2.86), (.68, .42), .1)
                                  for a, b in zip(PILLARS[:-1], PILLARS[1:])])
        side = smax(side, .9 - ax, .01)
        roof = smax(smax(rect2(x, z, (0, 0), (1.0, 4.45), .12), .24 - ax, .01), 3.42 - y, .01)
        return np.minimum(side, roof)

    def body(x, y, z):
        ref = shell(x, y, z)
        return subtract(ref, smax(windows(x, y, z), -(ref + .03), .005), .01)

    def under(x, y, z):
        frame = box(x, y, z, (0, 1.0, 0), (1.05, .16, 5.2), .04)
        bogies = np.minimum(bogie(x, y, z, 3.7, ALPINE_GAUGE, .45, 1.5), bogie(x, y, z, -3.7, ALPINE_GAUGE, .45, 1.5))
        ends = np.minimum(box(x, y, z, (0, 2.3, 5.32), (.55, .95, .1), .06), box(x, y, z, (0, 2.3, -5.32), (.55, .95, .1), .06))
        return union(frame, bogies, ends, k=.03)

    def wheels(x, y, z):
        return np.minimum(bogie_wheels(x, y, z, 3.7, ALPINE_GAUGE, .45, 1.5), bogie_wheels(x, y, z, -3.7, ALPINE_GAUGE, .45, 1.5))

    def chrome(x, y, z):
        return np.minimum(buffers(x, y, z, 5.68, 1, 1.05, .7, .16), buffers(x, y, z, -5.68, -1, 1.05, .7, .16))
    lo, hi = (-1.45, .85, -5.55), (1.45, 4.0, 5.55)
    parts = [part(body, lo, hi, .014, PAINT, (1, 1, 1), 5000),
             glazing(shell, windows, lo, hi, .014, 2200, .02),
             part(under, (-1.3, -.05, -5.5), (1.3, 3.3, 5.5), .014, TRIM, GREY, 2600),
             part(wheels, (-.8, -.05, -4.8), (.8, 1.0, 4.8), .008, METAL, STEEL, 3200),
             part(chrome, (-1.2, .8, -5.75), (1.2, 1.3, 5.75), .008, CHROME, STEEL, 800)]
    decals = [(0, x_band(2.18, 2.3), GLOSS, WHITE)]
    for s in (1, -1):
        for zc in (4.75, -4.75):
            decals.append((0, convex((2, 1), [(zc - .3, 1.2), (zc + .3, 1.2), (zc + .3, 2.08), (zc - .3, 2.08)]) +
                           [side_face(s, 1.1)], GLOSS, WHITE))
    return assemble(parts, decals, 1.0)


# --- Seaside road train: a toy locomotive on tyres and open carriages ------------------------
RT_RED = (.90, .20, .14)


def road_wheel(x, y, z, cx, cy, cz, r, width):
    ax = np.abs(x)
    rr = np.sqrt((y - cy) ** 2 + (z - cz) ** 2)
    return sdf.rect2(ax, rr, (cx, r - .08), (width / 2, .08), .06) if False else smax(
        sdf.rect2(ax - cx, rr, (0, r * .72), (width / 2, r * .28), .07), -rr, 0)


def road_train_tractor():
    def body(x, y, z):
        boiler = cylinder_z(x, y, z, (0, 1.32, 1.07), .62, .88, .06)
        cab = box(x, y, z, (0, 1.0, -1.05), (.95, .14, .95), .05)
        back = box(x, y, z, (0, 1.62, -1.92), (.95, .66, .07), .05)
        hood = box(x, y, z, (0, .96, .9), (.82, .2, 1.1), .08)
        return union(boiler, cab, back, hood, k=.06)

    def dark(x, y, z):
        chassis = box(x, y, z, (0, .62, 0), (.85, .16, 2.0), .06)
        smokebox = cylinder_z(x, y, z, (0, 1.32, 2.02), .66, .09, .04)
        chimney = cylinder_y(x, y, z, (0, 2.18, 1.6), .17, .36, .03)
        wells = np.minimum.reduce([smax(cylinder_x(np.abs(x), y, z, (.95, .42, zc), .5, .2, .03), .3 - y, .02)
                                   for zc in (1.45, -1.3)])
        return union(chassis, smokebox, chimney, k=.03)

    def yellow(x, y, z):
        crown = smax(cylinder_y(x, y, z, (0, 2.58, 1.6), .3, .07, .03), -cylinder_y(x, y, z, (0, 2.6, 1.6), .2, .2), .02)
        band = smax(np.abs(np.sqrt(x * x + (y - 1.32) ** 2) - .63) - .015, np.abs(z - 1.1) - .05, .006)
        bumper = box(x, y, z, (0, .62, 2.1), (1.0, .12, .07), .05)
        canopy = smax(box(x, y, z, (0, 2.72, -1.0), (1.08, .05, 1.12), .04), -(y - 2.67) + 0 * x - 10, 0)
        hubs = np.minimum.reduce([cylinder_x(np.abs(x), y, z, (1.06, .42, zc), .2, .025, .02) for zc in (1.45, -1.3)])
        return union(crown, band, bumper, canopy, hubs, k=.01)

    def tyres(x, y, z):
        return np.minimum.reduce([smax(cylinder_x(np.abs(x), y, z, (.94, .42, zc), .42, .13, .07),
                                       -cylinder_x(np.abs(x), y, z, (1.1, .42, zc), .2, .1), .02) for zc in (1.45, -1.3)])

    def cream(x, y, z):
        posts = np.minimum.reduce([capsule(np.abs(x), y, z, (.9, 1.1, zc), (.9, 2.68, zc), .04) for zc in (-1.92, -.12)])
        rail = capsule(np.abs(x), y, z, (.9, 1.55, -1.85), (.9, 1.55, -.2), .03)
        return np.minimum(posts, rail)

    def turquoise(x, y, z):
        seat = box(x, y, z, (0, 1.25, -1.45), (.45, .15, .28), .06)
        driver = smin(box(x, y, z, (0, 1.72, -1.3), (.27, .32, .17), .1), ellipsoid(x, y, z, (0, 1.4, -1.15), (.25, .14, .3)), .05)
        return np.minimum(seat, driver)

    def skin(x, y, z):
        return ellipsoid(x, y, z, (0, 2.2, -1.25), (.19, .22, .19))

    def brass(x, y, z):
        bell = smin(ellipsoid(x, y, z, (0, 2.02, .75), (.16, .2, .16)), cylinder_y(x, y, z, (0, 1.92, .75), .19, .03, .02), .02)
        lamp = box(x, y, z, (0, 1.95, 2.08), (.16, .13, .1), .04)
        return np.minimum(bell, lamp)

    def lens(x, y, z):
        return cylinder_z(x, y, z, (0, 1.95, 2.19), .1, .02, .01)
    lo, hi = (-1.2, -.05, -2.15), (1.2, 2.85, 2.25)
    parts = [part(body, lo, hi, .012, GLOSS, RT_RED, 4000),
             part(dark, lo, hi, .012, TRIM, INK, 1800),
             part(yellow, lo, hi, .01, GLOSS, YELLOW, 2200),
             part(tyres, (-1.2, -.05, -1.8), (1.2, .9, 1.95), .01, RUBBER, (.05, .05, .055), 1800),
             part(cream, (-1.1, 1.0, -2.05), (1.1, 2.75, 0), .01, GLOSS, CREAM, 600),
             part(turquoise, (-.6, 1.0, -1.8), (.6, 2.1, -.8), .01, GLOSS, TURQUOISE, 500),
             part(skin, (-.3, 1.95, -1.5), (.3, 2.45, -1.0), .008, GLOSS, (.86, .62, .45), 200),
             part(brass, (-.3, 1.75, .5), (.3, 2.25, 2.25), .008, CHROME, BRASS, 500),
             part(lens, (-.2, 1.8, 2.1), (.2, 2.1, 2.25), .005, HEADLAMP, LAMP, 120)]
    decals = []
    for s in (1, -1):
        decals.append((0, convex((2, 1), [(-1.88, 1.25), (-.25, 1.25), (-.25, 2.2), (-1.88, 2.2)]) + [side_face(s, .8)],
                       GLOSS, YELLOW))
    return assemble(parts, decals, .8)


def road_train_carriage():
    SHIRTS = [(.92, .22, .12), (.12, .49, .70), (.97, .68, .12), (.54, .28, .65), (.20, .65, .43)]
    SKINS = [(.94, .69, .49), (.73, .45, .28), (.59, .35, .23)]

    def floor(x, y, z):
        deck = box(x, y, z, (0, .8, 0), (1.02, .11, 2.4), .05)
        sides = smax(box(x, y, z, (0, 1.0, 0), (1.02, .22, 2.4), .05), -box(x, y, z, (0, 1.1, 0), (.95, .3, 2.33), .03), .02)
        return smin(deck, sides, .02)

    def dark(x, y, z):
        skirt = box(x, y, z, (0, .56, 0), (.95, .14, 2.25), .04)
        drawbar = capsule(x, y, z, (0, .5, 2.3), (0, .5, 2.86), .05)
        eye = sdf.torus_x(z, y, x, (2.86, .5, 0), .05, .018)
        rear = box(x, y, z, (0, .5, -2.42), (.12, .08, .08), .03)
        return union(skirt, drawbar, eye, rear, k=.02)

    def wood(x, y, z):
        d = None
        for k in range(3):
            zc = -1.55 + k * 1.55
            bench = smin(box(x, y, z, (0, 1.22, zc), (.92, .06, .25), .03),
                         box(x, y, z, (0, 1.55, zc - .24), (.92, .27, .04), .03), .03)
            d = bench if d is None else np.minimum(d, bench)
        return d

    def yellow(x, y, z):
        trim = smax(box(x, y, z, (0, 1.22, 0), (1.05, .03, 2.43), .02), -box(x, y, z, (0, 1.22, 0), (.92, .1, 2.3), .01), .01)
        hubs = np.minimum.reduce([cylinder_x(np.abs(x), y, z, (1.0, .36, zc), .17, .02, .015) for zc in (1.6, -1.6)])
        return np.minimum(trim, hubs)

    def tyres(x, y, z):
        return np.minimum.reduce([smax(cylinder_x(np.abs(x), y, z, (.88, .36, zc), .36, .11, .06),
                                       -cylinder_x(np.abs(x), y, z, (1.0, .36, zc), .17, .1), .02) for zc in (1.6, -1.6)])

    def posts(x, y, z):
        return np.minimum.reduce([capsule(np.abs(x), y, z, (.98, .95, zc), (.98, 2.55, zc), .04) for zc in (-2.3, 0, 2.3)])

    def canopy(x, y, z):
        return box(x, y, z + 0 * x, (0, 2.6 - .0, 0), (1.15, .06 + 0 * x, 2.5), .05) + .0 * x

    def people(colour_index):
        def field(x, y, z):
            d = None
            for k in range(3):
                for side in (-1, 1):
                    if (k * 2 + side) % 5 != colour_index:
                        continue
                    zc = -1.55 + k * 1.55 + .02
                    torso = smin(box(x, y, z, (side * .48, 1.58, zc), (.2, .26, .13), .1),
                                 ellipsoid(x, y, z, (side * .48, 1.3, zc + .15), (.2, .12, .22)), .05)
                    d = torso if d is None else np.minimum(d, torso)
            return d
        return field

    def heads(skin):
        return lambda x, y, z: np.minimum.reduce([ellipsoid(x, y, z, (side * .48, 2.06, -1.55 + k * 1.55 + .04),
                                                            (.17, .2, .17)) for k in range(3) for side in (-1, 1)
                                                  if (k + (side > 0)) % 3 == skin])
    lo, hi = (-1.25, -.05, -2.6), (1.25, 2.75, 2.95)
    parts = [part(floor, lo, hi, .012, GLOSS, TURQUOISE, 2200),
             part(dark, lo, hi, .012, TRIM, INK, 900),
             part(wood, lo, hi, .012, GLOSS, WOOD, 1200),
             part(yellow, lo, hi, .01, GLOSS, YELLOW, 1000),
             part(tyres, (-1.15, -.05, -2.05), (1.15, .75, 2.05), .008, RUBBER, (.05, .05, .055), 2400),
             part(posts, lo, hi, .01, GLOSS, CREAM, 500),
             part(canopy, (-1.25, 2.45, -2.6), (1.25, 2.72, 2.6), .012, GLOSS, CREAM, 900),
]
    parts += [part(heads(k), (-.8, 1.8, -1.9), (.8, 2.3, 1.7), .008, GLOSS, SKINS[k], 200) for k in range(3)]
    used = sorted({(k * 2 + side) % 5 for k in range(3) for side in (-1, 1)})
    for c in used:
        parts.append(part(people(c), (-.8, 1.1, -1.9), (.8, 1.9, 1.7), .01, GLOSS, SHIRTS[c], 400))
    # Turquoise and cream stripes along the canopy, both faces.
    decals = [(6, [lambda x, y, z, k=k: -2.5 + k * .833 - z, lambda x, y, z, k=k: z - (-2.5 + (k + 1) * .833)],
               GLOSS, TURQUOISE) for k in range(1, 6, 2)]
    return assemble(parts, decals, .8)


# --- Harbour freight: hood diesel and container flat -----------------------------------------
def diesel_locomotive():
    def body(x, y, z):
        hood = box(x, y, z, (0, 2.2, -.1), (1.08, .98, 4.05), .14)
        cabs = None
        for end in (-1, 1):
            cab = box(x, y, z, (0, 2.62, end * 5.0), (1.4, 1.38, 1.15), .16)
            nose = smax(cab, end * z - 6.18 + .3 * np.clip(y - 2.9, 0, 1), .1)
            cabs = nose if cabs is None else np.minimum(cabs, nose)
        d = smin(hood, cabs, .05)
        screens = smax(rect2(x, y, (0, 3.18), (.95, .42), .1), 5.6 - np.abs(z), .01)
        sides = smax(rect2(np.abs(z), y, (5.0, 3.18), (.55, .4), .08), 1.2 - np.abs(x), .01)
        d = subtract(d, smax(np.minimum(screens, sides), -(d + .03), .005), .01)
        louvres = smax(rect2(z, y, (-.1, 2.45), (3.2, .45), .05), .9 - np.abs(x), .01)
        return subtract(d, smax(louvres, -(d + .02), .004), .006)

    def outer(x, y, z):
        hood = box(x, y, z, (0, 2.2, -.1), (1.08, .98, 4.05), .14)
        cabs = np.minimum.reduce([smax(box(x, y, z, (0, 2.62, e * 5.0), (1.4, 1.38, 1.15), .16),
                                       e * z - 6.18 + .3 * np.clip(y - 2.9, 0, 1), .1) for e in (-1, 1)])
        return smin(hood, cabs, .05)

    def panes(x, y, z):
        screens = smax(rect2(x, y, (0, 3.18), (.95, .42), .1), 5.6 - np.abs(z), .01)
        sides = smax(rect2(np.abs(z), y, (5.0, 3.18), (.55, .4), .08), 1.2 - np.abs(x), .01)
        return np.minimum(screens, sides)

    def louvre_slats(x, y, z):
        hood = box(x, y, z, (0, 2.2, -.1), (1.08, .98, 4.05), .14)
        slats = np.abs(np.mod(z, .16) - .08) - .025
        region = smax(rect2(z, y, (-.1, 2.45), (3.2, .45), .05), .9 - np.abs(x), .01)
        return smax(smax(smax(slats, region + .002, .002), hood + .005, .002), -(hood + .02), 0)

    def grey(x, y, z):
        frame = box(x, y, z, (0, 1.08, 0), (1.42, .18, 6.1), .05)
        roofs = np.minimum(box(x, y, z, (0, 4.04, 5.0), (1.45, .08, 1.2), .06), box(x, y, z, (0, 4.04, -5.0), (1.45, .08, 1.2), .06))
        tanks = cylinder_z(np.abs(x), y, z, (.55, .65, 0), .32, 1.6, .08)
        bogies = np.minimum(bogie(x, y, z, 3.8), bogie(x, y, z, -3.8))
        fans = np.minimum.reduce([cylinder_y(x, y, z, (0, 3.2, zc), .42, .04, .03) for zc in (-2.6, -1.4)])
        return union(frame, roofs, tanks, bogies, fans, k=.03)

    def wheels(x, y, z):
        return np.minimum(bogie_wheels(x, y, z, 3.8), bogie_wheels(x, y, z, -3.8))

    def rails(x, y, z):
        ax = np.abs(x)
        hand = capsule(ax, y, z, (1.36, 2.0, -3.75), (1.36, 2.0, 3.65), .025)
        stanchions = np.minimum.reduce([capsule(ax, y, z, (1.36, 1.26, zc), (1.36, 2.0, zc), .022) for zc in (-3.7, -1.2, 1.2, 3.6)])
        horns = np.minimum.reduce([cylinder_z(x, y, z, (xc, 4.18, 5.5), .06, .14, .03) for xc in (-.3, .3)])
        return union(hand, stanchions, horns, k=.01)

    def chrome(x, y, z):
        return np.minimum(buffers(x, y, z, 6.58, 1, 1.1, .85), buffers(x, y, z, -6.58, -1, 1.1, .85))

    def lamps(x, y, z):
        return cylinder_z(np.abs(x), y, np.abs(z), (.8, 2.05, 6.12), .11, .03, .015)
    lo, hi = (-1.6, .85, -6.4), (1.6, 4.1, 6.4)
    parts = [part(body, lo, hi, .015, PAINT, (1, 1, 1), 6000),
             glazing(outer, panes, lo, hi, .012, 1200),
             part(louvre_slats, (-1.2, 1.9, -3.4), (1.2, 3.0, 3.2), .012, TRIM, GREY, 1200),
             part(grey, (-1.6, -.05, -6.3), (1.6, 4.2, 6.3), .015, TRIM, GREY, 4000),
             part(wheels, (-1.0, -.05, -4.9), (1.0, .95, 4.9), .01, METAL, STEEL, 2000),
             part(rails, (-1.5, 1.2, -6.0), (1.5, 4.4, 6.0), .008, GLOSS, YELLOW, 900),
             part(chrome, (-1.6, .8, -6.7), (1.6, 1.4, 6.7), .01, CHROME, STEEL, 600),
             part(lamps, (-1.0, 1.85, -6.3), (1.0, 2.25, 6.3), .006, HEADLAMP, LAMP, 300)]
    decals = [(0, x_band(1.42, 1.66), GLOSS, YELLOW)]
    for e in (1, -1):
        # Warning chevrons on each nose.
        for k in range(-3, 4):
            x0 = k * .42
            decals.append((0, convex((0, 1), [(x0 - .1, 1.25), (x0 + .1, 1.25), (x0 + .3, 1.66), (x0 + .1, 1.66)]) +
                           [end_face(e, 6.05)], GLOSS, INK, .003))
    return assemble(parts, decals, 1.0)


def container_wagon():
    def deck(x, y, z):
        top = box(x, y, z, (0, 1.25, 0), (1.25, .15, 6.45), .04)
        sole = box(x, y, z, (0, .95, 0), (1.0, .15, 6.25), .04)
        fish = box(x, y, z, (0, .78, 0), (.15, .22, 4.0), .04)
        bogies = np.minimum(bogie(x, y, z, 4.6), bogie(x, y, z, -4.6))
        locks = np.minimum.reduce([box(np.abs(x), y, z, (1.18, 1.42, zc), (.06, .04, .06), .015) for zc in (-6.15, -.15, .15, 6.15)])
        return union(top, sole, fish, bogies, locks, k=.03)

    def container(cz):
        def field(x, y, z):
            ax = np.abs(x)
            # Corrugated flanks and roof, corner posts and the door end.
            corr = .025 * np.sin(z * 2 * math.pi / .3)
            shell = box(x, y, z, (0, 2.71, cz), (1.2 + corr * np.clip(1.15 - ax, -1, 1) * 0 + .0, 1.27, 3.02), .02)
            ripple = smax(ax - 1.2 - corr, np.abs(y - 2.71) - 1.15, .01)
            ripple = smax(ripple, np.abs(z - cz) - 2.9, .01)
            d = np.minimum(smax(shell, -(ripple + .03), 0) + 0, smin(shell, ripple, .005))
            posts = None
            for sx in (-1, 1):
                for sz in (-1, 1):
                    p = box(x, y, z, (sx * 1.15, 2.71, cz + sz * 2.97), (.06, 1.27, .06), .01)
                    posts = p if posts is None else np.minimum(posts, p)
            return smin(d, posts, .005)
        return field

    def wheels(x, y, z):
        return np.minimum(bogie_wheels(x, y, z, 4.6), bogie_wheels(x, y, z, -4.6))

    def door_bars(cz, end):
        def field(x, y, z):
            bars = np.minimum.reduce([capsule(x, y, z, (xc, 1.55, cz + end * 3.06), (xc, 3.85, cz + end * 3.06), .025)
                                      for xc in (-.75, -.35, .35, .75)])
            return bars
        return field

    def chrome(x, y, z):
        return np.minimum(buffers(x, y, z, 6.88, 1, 1.1, .85), buffers(x, y, z, -6.88, -1, 1.1, .85))
    parts = [part(container(-3.15), (-1.3, 1.35, -6.25), (1.3, 4.05, -.05), .014, GLOSS, (.66, .20, .12), 3500),
             part(container(3.15), (-1.3, 1.35, .05), (1.3, 4.05, 6.25), .014, GLOSS, (.14, .36, .60), 3500),
             part(deck, (-1.4, -.05, -6.6), (1.4, 1.5, 6.6), .015, TRIM, (.26, .25, .24), 3000),
             part(wheels, (-1.0, -.05, -5.6), (1.0, .95, 5.6), .01, METAL, STEEL, 2000),
             part(door_bars(-3.15, -1), (-1.0, 1.4, -6.3), (1.0, 4.0, -6.05), .006, METAL, STEEL, 400),
             part(door_bars(3.15, 1), (-1.0, 1.4, 6.05), (1.0, 4.0, 6.3), .006, METAL, STEEL, 400),
             part(chrome, (-1.6, .8, -7.0), (1.6, 1.4, 7.0), .01, CHROME, STEEL, 600)]
    decals = []
    for i, (cz, colour) in enumerate(((-3.15, WHITE), (3.15, WHITE))):
        for s in (1, -1):
            # A white operator's code on each flank.
            for k in range(4):
                z0 = cz - .9 + k * .5
                decals.append((i, convex((2, 1), [(z0, 3.45), (z0 + .36, 3.45), (z0 + .36, 3.62), (z0, 3.62)]) +
                               [side_face(s, 1.1)], GLOSS, colour, .004))
    return assemble(parts, decals, 1.0)


VEHICLES = {'steam-locomotive': steam_locomotive, 'steam-tender': steam_tender, 'coach': coach,
            'alpine-locomotive': alpine_locomotive, 'alpine-coach': alpine_coach,
            'road-train-tractor': road_train_tractor, 'road-train-carriage': road_train_carriage,
            'diesel-locomotive': diesel_locomotive, 'container-wagon': container_wagon}


LIVERIES = {'steam-locomotive': (.12, .39, .27), 'steam-tender': (.12, .39, .27), 'coach': (.12, .39, .27),
            'alpine-locomotive': (.76, .09, .08), 'alpine-coach': (.76, .09, .08),
            'diesel-locomotive': (.10, .30, .62)}


def build(only=None, liveries=LIVERIES):
    """Exports each vehicle and lays them out side by side in art/trains.blend."""
    import bpy
    empty_scene()
    x = 0.
    for name, design in VEHICLES.items():
        if only not in (None, name):
            continue
        mesh = design()
        export_tcm(os.path.join(ROOT, 'assets/models', name + '.tcm'), mesh)
        matrix = np.eye(4)
        matrix[0, 3] = x
        blender_object(name, mesh, (liveries or {}).get(name, (.12, .39, .27)), matrix)
        x += 4.
    if only is None:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT, 'art', 'trains.blend'))
