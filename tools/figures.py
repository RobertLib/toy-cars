"""Smooth primitives for people and small rounded things: closed rings of points
with two poles, so Blender's smooth vertex normals shade them without seams.

All functions return (vertices, faces) in game axes (x right, y up, z forward),
faces wound counter-clockwise seen from outside.
"""
import math
from math import cos, sin, pi
from mathutils import Vector


def rings(loops, bottom, top):
    n = len(loops[0])
    vs = [bottom] + [p for loop in loops for p in loop] + [top]
    fs = [(0, 1 + (k + 1) % n, 1 + k) for k in range(n)]
    for j in range(len(loops) - 1):
        a = 1 + j * n
        fs += [(a + k, a + (k + 1) % n, a + n + (k + 1) % n, a + n + k) for k in range(n)]
    t = len(vs) - 1
    a = 1 + (len(loops) - 1) * n
    fs += [(a + k, a + (k + 1) % n, t) for k in range(n)]
    return vs, [tuple(reversed(f)) for f in fs]


def blob(c, r, lon=8, lat=5, e=1.):
    """An ellipsoid of radii r about c; with e < 1 a rounded box (superellipsoid)."""
    f = lambda v: math.copysign(abs(v) ** e, v)
    loops = [[(c[0] + r[0] * f(cos(ph)) * f(cos(th)), c[1] + r[1] * f(sin(ph)), c[2] + r[2] * f(cos(ph)) * f(sin(th)))
              for th in (2 * pi * k / lon for k in range(lon))]
             for ph in (-pi / 2 + pi * j / lat for j in range(1, lat))]
    return rings(loops, (c[0], c[1] - r[1], c[2]), (c[0], c[1] + r[1], c[2]))


def limb(a, b, ra, rb=None, sides=5):
    """A tapered capsule from a to b with rounded ends."""
    rb = ra if rb is None else rb
    a, b = Vector(a), Vector(b)
    axis = (b - a).normalized()
    u = axis.cross(Vector((0, 0, 1)))
    if u.length < .01:
        u = axis.cross(Vector((1, 0, 0)))
    u.normalize()
    v = axis.cross(u)
    loops = []
    for c, r, h in ((a, ra, -.7), (a, ra, 0), (b, rb, 0), (b, rb, .7)):
        k = (1 - h * h) ** .5
        loops.append([tuple(c + axis * h * r + r * k * (cos(i * 2 * pi / sides) * u + sin(i * 2 * pi / sides) * v))
                      for i in range(sides)])
    return rings(loops, tuple(a - axis * ra), tuple(b + axis * rb))


def shape(name, centre, yaw, pieces, material, mesh_new):
    """One smooth object located at centre (game axes), turned by yaw, from (vertices, faces)
    pieces given relative to centre."""
    import bpy
    vs, fs = [], []
    for v, f in pieces:
        base = len(vs)
        vs += v
        fs += [tuple(base + i for i in face) for face in f]
    c, s = cos(yaw), sin(yaw)
    # Game (x, y, z) -> Blender (x, -z, y), turned about the vertical by yaw.
    local = [(c * x + s * z, -(-s * x + c * z), y) for x, y, z in vs]
    me = bpy.data.meshes.new(name)
    me.from_pydata(local, [], fs)
    me.update()
    for p in me.polygons:
        p.use_smooth = True
    me.materials.append(material)
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    o.location = (centre[0], -centre[2], centre[1])
    return o
