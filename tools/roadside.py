"""Road furniture for the circuits: delineator posts, bend boards and the start gantry.

Each piece is modeled once with the signed distance pipeline of tools/cars.py
and placed as linked duplicates, its colours and baked occlusion carried in the
'Riverbed' point colour layer like the flora (see tools/flora.py).
"""
import math

import numpy as np

import sdf
from sdf import smin, smax, union, subtract, box, rect2, cylinder_y, capsule
from cars import Mesh, part, bake_ao, clip_decal, convex, strip, GLOSS, TRIM, METAL, RUBBER
from flora import blender_mesh

LIBRARY = {}
WHITE = (.93, .92, .88)
BLACK = (.05, .055, .06)


def assemble(parts, decals=(), reach=.5):
    meshes = [p.mesh() for p in parts]
    meshes += [clip_decal(meshes[0], *d) for d in decals]
    mesh = bake_ao(Mesh.join(meshes), [(p.points, p.faces) for p in parts], rays=40, reach=reach)
    # Per-vertex colour: every vertex belongs to faces of a single colour here.
    colour = np.ones((len(mesh.points), 3))
    for k in range(3):
        colour[mesh.faces[:, k]] = mesh.colors
    return mesh, colour * (.45 + .55 * mesh.ao)[:, None]


def library(key, build):
    """One shared Blender mesh per key."""
    if key not in LIBRARY:
        import bpy
        mesh, colour = build()
        material = bpy.data.materials.get('Road furniture') or bpy.data.materials.new('Road furniture')
        material['paint'] = 0
        LIBRARY[key] = blender_mesh(' '.join(str(k) for k in key), mesh.points, mesh.faces, colour, material)
    return LIBRARY[key]


def place(name, key, build, x, y, z, yaw=0.):
    """A linked duplicate standing at (x, y, z), turned by yaw about the vertical."""
    import bpy
    o = bpy.data.objects.new(name, library(key, build))
    bpy.context.collection.objects.link(o)
    o.location = (x, -z, y)
    o.rotation_euler = (0, 0, yaw)
    return o


# --- Pieces (game axes, standing on y = 0) ---------------------------------------------------
def delineator(reflector):
    """A round roadside post with a black band and a reflector on either face."""
    def post(x, y, z):
        return smin(cylinder_y(x, y, z, (0, .58, 0), .085, .58, .06),
                    cylinder_y(x, y, z, (0, .02, 0), .11, .03, .015), .03)

    def lens(x, y, z):
        return smax(box(x, y, z, (0, .93, 0), (.045, .075, .1), .015), -(post(x, y, z) + .004), .004)
    parts = [part(post, (-.15, -.02, -.15), (.15, 1.22, .15), .008, GLOSS, WHITE, 110),
             part(lens, (-.15, .8, -.15), (.15, 1.06, .15), .006, GLOSS, reflector, 24)]
    band = ([lambda x, y, z: 1.0 - y, lambda x, y, z: y - 1.1], GLOSS, BLACK)
    return assemble(parts, [band], .4)


def bend_board(turn, face):
    """A bend warning board on a post, facing approaching cars (local -z): white border,
    chevrons pointing into the bend (turn +1 right, -1 left)."""
    def post(x, y, z):
        return smin(cylinder_y(x, y, z, (0, 1.2, 0), .075, 1.2, .03),
                    cylinder_y(x, y, z, (0, .03, 0), .14, .04, .02), .03)

    def board(x, y, z):
        return box(x, y, z, (0, 2.05, -.1), (1.42, .6, .06), .05)
    front = lambda x, y, z: z + .14
    parts = [part(board, (-1.55, 1.3, -.3), (1.55, 2.8, .1), .008, GLOSS, WHITE, 500),
             part(post, (-.25, -.02, -.25), (.25, 2.45, .25), .008, METAL, (.30, .31, .33), 120)]
    decals = [(convex((0, 1), [(-1.30, 1.57), (1.30, 1.57), (1.30, 2.53), (-1.30, 2.53)]) + [front], GLOSS, face)]
    for k in range(3):
        cx = (k - 1) * .78
        u = lambda d: cx + turn * d
        lower = [(u(-.22), 1.72), (u(-.06), 1.72), (u(.38), 2.05), (u(.22), 2.05)]
        upper = [(u(.22), 2.05), (u(.38), 2.05), (u(-.06), 2.38), (u(-.22), 2.38)]
        for arm in (lower, upper):
            decals.append((convex((0, 1), arm) + [front], GLOSS, BLACK, .003))
    return assemble(parts, decals, .6)


def gantry(board):
    """The start and finish gantry: tapered pillars on plinths, a rounded board with checkers."""
    def pillars(x, y, z):
        ax = np.abs(x)
        t = np.clip(y / 6.4, 0, 1)
        column = smax(rect2(ax, z, (6.4, 0), (.30 - .06 * t, .30 - .06 * t), .06), np.abs(y - 3.3) - 3.3, .05)
        foot = box(ax, y, z, (6.4, .25, 0), (.48, .25, .48), .06)
        return smin(column, foot, .08)

    def beam(x, y, z):
        return box(x, y, z, (0, 7, 0), (6.85, .78, .36), .12)
    lo, hi = (-7.2, -.05, -.6), (7.2, 7.95, .6)
    parts = [part(beam, lo, hi, .012, GLOSS, board, 4000),
             part(pillars, lo, hi, .012, METAL, (.12, .13, .15), 3000)]
    decals = []
    for s in (1, -1):
        face = lambda x, y, z, s=s: .25 - s * z
        decals.append((convex((0, 1), [(-6.3, 6.4), (6.3, 6.4), (6.3, 7.64), (-6.3, 7.64)]) + [face], GLOSS, WHITE))
        for k in range(14):
            for j in range(2):
                if (k + j) % 2 == 0:
                    x0, y0 = -6.3 + k * .9, 6.4 + j * .62
                    square = [(x0, y0), (x0 + .9, y0), (x0 + .9, y0 + .62), (x0, y0 + .62)]
                    decals.append((convex((0, 1), square) + [face], GLOSS, BLACK, .003))
    return assemble(parts, decals, 1.5)
