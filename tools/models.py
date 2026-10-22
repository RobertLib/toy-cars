"""The fuel can, the camera helicopter and the garage turntable, modeled like the cars.

They share the signed distance pipeline of tools/cars.py: smooth parts meshed
separately per material, decimated, decals clipped onto the finished surface
and ambient occlusion baked into the vertex colors. These are drawn with a
white paint colour, so every surface names its own colour.
"""
import math
import os

import numpy as np

import sdf
from sdf import smin, smax, union, subtract, box, rect2, circle2, ellipsoid, capsule
from sdf import cylinder_x, cylinder_y, cylinder_z, rounded_polygon
from cars import (Mesh, part, bake_ao, clip_decal, convex, export_tcm, blender_object, empty_scene, ROOT, PAINT, GLASS,
                  CHROME, RUBBER, TRIM, GLOSS, HEADLAMP, METAL, IVORY, INK, TINT, SILVER)


def assemble(parts, decals=(), rays=48, reach=.4):
    """Meshes parts, prints decals onto the first one and bakes occlusion."""
    meshes = [p.mesh() for p in parts]
    meshes += [clip_decal(meshes[0], *d) for d in decals]
    return bake_ao(Mesh.join(meshes), [(p.points, p.faces) for p in parts], rays=rays, reach=reach)


# ---------------------------------------------------------------------------
# Fuel can: a pressed-steel jerrycan with the stamped X, three handles and a
# lever-locked spout. About .78 wide, 1.2 tall and .36 deep; the base at y = 0.

FUEL = (.30, .84, .52)


def fuel_can():
    W, H, D = .37, .52, .17   # half extents of the can body
    CY = .54

    def shell(x, y, z):
        return box(x, y, z, (0, CY, 0), (W, H, D), .055)

    def can(x, y, z):
        d = shell(x, y, z)
        az = np.abs(z)
        # A shallow panel pressed into each face, with the raised X inside it.
        panel = rect2(x, y, (0, CY - .02), (W - .07, H - .10), .05)
        d = subtract(d, smax(panel, D - .012 - az, .004), .006)
        u, v = x / (W - .07), (y - CY + .02) / (H - .10)
        cross = np.minimum(np.abs(u - v), np.abs(u + v)) * .3 - .022
        d = smin(d, smax(smax(cross, panel, .004), az - D + .002, .003), .004)
        # The weld bead around the rim where the two halves meet.
        d = smin(d, smax(np.abs(z) - .008, shell(x, y, z) - .009, .003), .004)
        # Three carrying handles over the top.
        for hx in (-.2, 0, .2):
            d = smin(d, capsule(x, y, z, (hx, 1.17, -.11), (hx, 1.17, .11), .028), .006)
            for hz in (-.11, .11):
                d = smin(d, capsule(x, y, z, (hx, 1.03, hz * .9), (hx, 1.17, hz), .022), .012)
        return d

    def spout(x, y, z):
        neck = cylinder_y(x, y, z, (.25, 1.09, 0), .075, .045, .012)
        cap = cylinder_y(x, y, z, (.25, 1.15, 0), .085, .03, .015)
        lever = box(x, y, z, (.17, 1.17, 0), (.08, .014, .03), .012)
        return union(neck, cap, lever, k=.008)

    lo, hi = (-.45, -.02, -.25), (.45, 1.25, .25)
    parts = [part(can, lo, hi, .004, GLOSS, FUEL, 9000),
             part(spout, lo, hi, .003, METAL, (.30, .31, .33), 1500)]
    decals = []
    for side in (1, -1):
        # A fuel drop in the upper triangle of the X on each face.
        drop = [(0, .90)] + [(.065 * math.cos(a), .785 + .065 * math.sin(a))
                             for a in np.linspace(math.radians(20), math.radians(-200), 11)]
        decals.append((convex((0, 1), drop) + [lambda x, y, z, s=side: .1 - s * z], GLOSS, IVORY))
    return assemble(parts, decals, reach=.3)


# ---------------------------------------------------------------------------
# Camera helicopter: a light utility airframe in broadcast livery. Body pivot as
# before: the main rotor turns about (0, 1.55, 0) and the tail rotor about
# (.25, .74, -4.02) on the x axis; skids rest at y = -1.12.

CREAM = (.93, .91, .84)
NAVY = (.10, .19, .27)
ORANGE = (.92, .30, .08)


def helicopter():
    P = sdf.Profile
    side = P(rounded_polygon([(1.92, -.18, .35), (1.55, .52, .45), (.70, .98, .30), (-.55, 1.02, .25),
                              (-1.05, .70, .25), (-1.0, -.30, .2), (-.40, -.62, .25), (1.10, -.62, .35)], .2))
    plan = P(rounded_polygon([(-.86, 1.15, .4), (-.74, 1.95, .5), (.74, 1.95, .5), (.86, 1.15, .4),
                              (.82, -.85, .3), (-.82, -.85, .3)], .3))
    windscreen = P(rounded_polygon([(1.88, .02), (1.52, .62), (.95, .90), (.60, .92), (.60, -.05), (1.10, -.40)], .06))
    door = P(rounded_polygon([(.45, .02), (.45, .80), (-.35, .80), (-.35, .02)], .07))
    fin = P(rounded_polygon([(-3.70, .55), (-4.05, 1.85), (-4.42, 1.92), (-4.30, .30)], .07))

    def body(x, y, z):
        ax = np.abs(x)
        # Rounded section: the pod narrows towards its keel and roof.
        d = smax(side(z, y), plan(ax + .18 * (y - .2) ** 2, z), .25)
        # Engine cowling and the tail boom tapering into the fin.
        d = smin(d, box(x, y, z, (0, 1.02, -.55), (.42, .22, .75), .18), .12)
        t = np.clip((-z - .8) / 3.3, 0, 1)
        boom = np.sqrt(x * x + (y - (.55 + .02 * t)) ** 2) - (.30 - .19 * t)
        d = smin(d, smax(boom, np.maximum(z - (-.6), -4.15 - z), .05), .25)
        d = smin(d, smax(fin(z, y), ax - .045 + .02 * np.clip(y - .5, 0, 1.5), .02), .05)
        d = smin(d, smax(sdf.rect2(z, y, (-3.25, .50), (.25, .035), .03), ax - 1.0, .03), .04)
        d = smin(d, box(ax, y, z, (1.0, .55, -3.25), (.018, .16, .20), .015), .015)
        # Rotor mast fairing.
        d = smin(d, cylinder_y(x, y, z, (0, 1.30, 0), .17, .12, .05), .06)
        return d

    def glazing(x, y, z):
        ax = np.abs(x)
        front = windscreen(z, y)
        doors = smax(door(z, y), .45 - ax, 0)
        return np.minimum(front, doors)

    def skin(x, y, z):
        return subtract(body(x, y, z), smax(glazing(x, y, z), -(body(x, y, z) + .02), .002), .006)

    def glass(x, y, z):
        b = body(x, y, z)
        return smax(smax(b + .012, glazing(x, y, z) - .02, .002), -(b + .035), 0)

    def skids(x, y, z):
        ax = np.abs(x)
        rail = capsule(ax, y, z, (.97, -1.12, -1.25), (.97, -1.12, 1.30), .055)
        nose = capsule(ax, y, z, (.97, -1.12, 1.30), (.97, -.94, 1.68), .055)
        d = smin(rail, nose, .03)
        for zc in (-.73, .85):
            d = smin(d, capsule(ax, y, z, (.58, -.48, zc), (.97, -1.12, zc), .05), .04)
        return d

    def gimbal(x, y, z):
        ball = ellipsoid(x, y, z, (0, -.80, 1.15), (.27, .27, .27))
        yoke = cylinder_y(x, y, z, (0, -.58, 1.15), .10, .06, .03)
        return smin(ball, yoke, .03)

    def lens(x, y, z):
        return smax(cylinder_z(x, y, z, (0, -.82, 1.38), .12, .06, .02), -(gimbal(x, y, z) - .005), .004)

    def mast(x, y, z):
        return cylinder_y(x, y, z, (0, 1.45, 0), .07, .12, .02)

    lo, hi = (-1.15, -1.25, -4.55), (1.15, 2.0, 2.05)
    parts = [part(skin, lo, hi, .007, GLOSS, CREAM, 14000),
             part(glass, lo, hi, .007, GLASS, TINT, 2400),
             part(skids, lo, hi, .005, METAL, (.25, .26, .28), 1400),
             part(gimbal, (-.35, -1.1, .8), (.35, -.45, 1.5), .004, TRIM, INK, 900),
             part(lens, (-.35, -1.1, .8), (.35, -.45, 1.5), .003, GLASS, (.04, .06, .09), 300),
             part(mast, (-.3, 1.2, -.3), (.3, 1.7, .3), .004, METAL, (.35, .36, .38), 300)]
    decals = [
        # Navy keel, an orange cheat line along each side and down the boom.
        ([lambda x, y, z: y + .22], GLOSS, NAVY),
        (convex((2, 1), [(1.75, .14), (-4.2, .50), (-4.2, .62), (1.75, .30)]), GLOSS, ORANGE),
    ]
    return assemble(parts, decals, reach=.6)


def helicopter_rotor():
    """Four tapered blades about a hub at the origin."""
    def blades(x, y, z):
        d = None
        for a in (0, math.pi / 2):
            c, s = math.cos(a), math.sin(a)
            u, v = c * x + s * z, -s * x + c * z
            r = np.abs(u)
            chord = .095 - .02 * np.clip((r - .4) / 3, 0, 1)
            blade = smax(smax(np.abs(v) - chord, np.abs(y) - .018 * (1 - .4 * np.clip(r / 3.4, 0, 1)), .01),
                         r - 3.4, .06)
            d = blade if d is None else np.minimum(d, blade)
        return smin(d, cylinder_y(x, y, z, (0, 0, 0), .22, .07, .04), .05)
    lo, hi = (-3.5, -.12, -3.5), (3.5, .12, 3.5)
    p = part(blades, lo, hi, .008, TRIM, (.07, .075, .08), 2400)
    # Painted tips make the disc visible as it turns.
    return assemble([p], [([lambda x, y, z: 3.1 - np.maximum(np.abs(x), np.abs(z))], GLOSS, (.96, .74, .12))],
                    reach=.2)


def helicopter_tail_rotor():
    """Two blades spinning about the x axis, with a hub."""
    def rotor(x, y, z):
        blade = smax(smax(np.abs(z) - .055, np.abs(x) - .012, .006), np.abs(y) - .62, .03)
        return smin(blade, cylinder_x(x, y, z, (0, 0, 0), .09, .05, .02), .02)
    lo, hi = (-.12, -.7, -.15), (.12, .7, .15)
    p = part(rotor, lo, hi, .003, TRIM, (.07, .075, .08), 800)
    return assemble([p], reach=.15)


# ---------------------------------------------------------------------------
# Track props: a barrier tire lying flat and a traffic cone, about their centres
# of mass as src/props.c simulates them.

def prop_tire():
    """A worn car tire on its side: ring radius .43, tube .20, tread and sidewall rings."""
    def tire(x, y, z):
        r = np.sqrt(x * x + z * z)
        a = np.arctan2(z, x)
        tube = rect2(r, y, (.43, 0), (.20, .19), .085)
        # Tread blocks across the outer face and a bead lip inside.
        step = 2 * math.pi / 30
        u = (np.mod(a, step) - step / 2) * .63
        groove = smax(np.abs(u) - .012, -(r - .60), .004)
        tube = subtract(tube, smax(groove, np.abs(y) - .16, .004), .004)
        tube = subtract(tube, rect2(r, y, (.63, 0), (.02, .012), .004), .003)
        for side in (-1, 1):
            tube = subtract(tube, rect2(r, y, (.47, side * .19), (.05, .006), .004), .003)
        return tube
    lo, hi = (-.66, -.22, -.66), (.66, .22, .66)
    p = part(tire, lo, hi, .006, RUBBER, (.045, .047, .05), 2200)
    return assemble([p], reach=.3)


def prop_cone():
    """A traffic cone on a square rubber foot, two reflective collars; base at y = -.20."""
    def cone(x, y, z):
        r = np.sqrt(x * x + z * z)
        t = np.clip((y + .16) / .72, 0, 1)
        body = smax(r - (.25 - .21 * t), np.maximum(-(y + .16), y - .56), .02)
        return body

    def foot(x, y, z):
        return box(x, y, z, (0, -.18, 0), (.36, .02, .36), .03)
    lo, hi = (-.4, -.22, -.4), (.4, .6, .4)
    parts = [part(cone, lo, hi, .004, GLOSS, (1, .24, .025), 1200),
             part(foot, lo, hi, .005, RUBBER, (.10, .105, .11), 300)]
    bands = [([lambda x, y, z, a=a: a - y, lambda x, y, z, b=b: y - b], GLOSS, (.95, .95, .90))
             for a, b in ((.06, .16), (.29, .37))]
    return assemble(parts, bands, reach=.3)


# ---------------------------------------------------------------------------
# Garage turntable.

def turntable():
    """The garage's revolving dais: a satin top with machined rings over a dark plinth,
    banded in the brand orange. Top surface at y = 0, radius 3.3."""
    def top(x, y, z):
        r = np.sqrt(x * x + z * z)
        disc = sdf.rect2(r, y, (0, -.11), (3.3, .11), .045)
        for ring in (2.35, 2.95):
            disc = subtract(disc, sdf.rect2(r, y, (ring, 0), (.012, .006), .003), .003)
        return disc

    def band(x, y, z):
        r = np.sqrt(x * x + z * z)
        return sdf.rect2(r, y, (3.25, -.115), (.065, .045), .02)

    def plinth(x, y, z):
        r = np.sqrt(x * x + z * z)
        return sdf.rect2(r, y, (0, -.31), (3.55, .09), .05)
    lo, hi = (-3.7, -.45, -3.7), (3.7, .05, 3.7)
    parts = [part(top, lo, hi, .006, TRIM, (.16, .17, .19), 9000),
             part(band, lo, hi, .006, GLOSS, (.89, .29, .12), 5000),
             part(plinth, lo, hi, .008, METAL, (.10, .105, .115), 5000)]
    return assemble(parts, reach=.6)


def puff():
    """A soft cluster of balls for dust and smoke; about unit radius, tinted by the paint colour."""
    lobes = [(0, 0, 0, .78), (.42, .18, .1, .55), (-.38, .12, .25, .52), (.05, .35, -.38, .5), (-.1, -.3, .3, .45)]

    def cloud(x, y, z):
        return union(*[np.sqrt((x - a) ** 2 + (y - b) ** 2 + (z - c) ** 2) - r for a, b, c, r in lobes], k=.18)
    p = part(cloud, (-1.1, -1.1, -1.1), (1.1, 1.1, 1.1), .03, PAINT, (1, 1, 1), 140)
    return assemble([p], reach=.8)


def barrier_segment():
    """One stripe of a crossing barrier arm: a round bar from z -1 to 1 of unit radius,
    scaled and tinted per stripe by the renderer."""
    def bar(x, y, z):
        return cylinder_z(x, y, z, (0, 0, 0), 1, 1, .15)
    p = part(bar, (-1.1, -1.1, -1.1), (1.1, 1.1, 1.1), .03, PAINT, (1, 1, 1), 120)
    return assemble([p], reach=.3)


def barrier_hinge():
    """The barrier's pivot housing with its counterweight, about the hinge at the origin."""
    def housing(x, y, z):
        drum = cylinder_x(x, y, z, (0, 0, 0), .16, .12, .04)
        weight = box(x, y, z, (0, 0, -.42), (.1, .12, .2), .04)
        arm = box(x, y, z, (0, 0, -.2), (.05, .05, .2), .02)
        return union(drum, weight, arm, k=.03)
    p = part(housing, (-.3, -.3, -.7), (.3, .3, .3), .008, GLOSS, (.16, .17, .19), 500)
    return assemble([p], reach=.3)


MODELS = {'fuel': fuel_can, 'helicopter': helicopter, 'helicopter-rotor': helicopter_rotor,
          'helicopter-tail-rotor': helicopter_tail_rotor, 'turntable': turntable,
          'prop-tire': prop_tire, 'prop-cone': prop_cone, 'puff': puff,
          'barrier-segment': barrier_segment, 'barrier-hinge': barrier_hinge}


# Editable scenes in art/, with the rotors mounted where the renderer turns them.
SCENES = {'fuel_can': {'fuel': (0, 0, 0)},
          'helicopter': {'helicopter': (0, 0, 0), 'helicopter-rotor': (0, 1.55, 0),
                         'helicopter-tail-rotor': (.25, .74, -4.02)},
          'turntable': {'turntable': (0, 0, 0)},
          'track_props': {'prop-tire': (0, .2, 0), 'prop-cone': (1.5, .2, 0), 'puff': (3, 1, 0),
                          'barrier-segment': (5, 1, 0), 'barrier-hinge': (7, 1, 0)}}


def build(only=None):
    import bpy
    for blend, members in SCENES.items():
        if only not in (None, *members):
            continue
        empty_scene()
        for name, at in members.items():
            mesh = MODELS[name]()
            export_tcm(os.path.join(ROOT, 'assets/models', name + '.tcm'), mesh)
            matrix = np.eye(4)
            matrix[:3, 3] = at
            blender_object(name, mesh, (1, 1, 1), matrix)
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT, 'art', blend + '.blend'))
