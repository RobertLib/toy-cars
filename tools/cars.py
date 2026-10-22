"""The garage cars and their wheels, modeled as signed distance fields.

Every car is a set of parts: the painted shell, glazing, lamps, trim and
decals each mesh separately, so material edges stay crisp where the parts meet.
Wheels are exported on their own and spun and steered by the renderer.
Ambient occlusion is baked into the vertex colors by ray casting.

Game axes throughout: x right, y up, z forward; the origin sits on the ground
between the axles, .04 above the modeled tire bottoms.
"""
import math
import os
import struct

import numpy as np

import sdf
from sdf import smin, smax, union, subtract, box, rect2, circle2, ellipsoid, capsule
from sdf import cylinder_x, cylinder_y, cylinder_z, polygon2, rounded_polygon

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Material codes travel in the vertex paint channel; renderer.c decodes them.
PAINT, GLASS, CHROME, RUBBER, TRIM, GLOSS, HEADLAMP, TAILLAMP, METAL = range(2, 11)
# Added to wheel parts that steer with the wheel but do not spin (calipers).
HUB = 16

INK = (.035, .038, .042)
RUBBER_BLACK = (.045, .047, .05)
TINT = (.05, .085, .10)
SILVER = (.62, .64, .65)
IVORY = (.93, .90, .80)


class Mesh:
    """Triangles with per-vertex normals, per-face material codes and colors."""

    def __init__(self, points, normals, faces, codes, colors):
        self.points, self.normals, self.faces = points, normals, faces
        self.codes, self.colors = codes, colors
        self.ao = np.ones(len(points))

    @staticmethod
    def join(meshes):
        offset, faces = 0, []
        for m in meshes:
            faces.append(m.faces + offset)
            offset += len(m.points)
        out = Mesh(np.concatenate([m.points for m in meshes]), np.concatenate([m.normals for m in meshes]),
                   np.concatenate(faces), np.concatenate([m.codes for m in meshes]),
                   np.concatenate([m.colors for m in meshes]))
        out.ao = np.concatenate([m.ao for m in meshes])
        return out

    def corner_normals(self, crease=50):
        """Smooth field normals, except where a face turns away from them: there, flat."""
        corners = self.faces.reshape(-1)
        p = self.points[self.faces]
        fn = np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0])
        fn /= np.maximum(np.linalg.norm(fn, axis=1)[:, None], 1e-12)
        fn = np.repeat(fn, 3, axis=0)
        n = self.normals[corners]
        sharp = np.einsum('ij,ij->i', n, fn) < math.cos(math.radians(crease))
        return np.where(sharp[:, None], fn, n)

    def transformed(self, matrix):
        m = np.asarray(matrix, np.float64)
        points = self.points @ m[:3, :3].T + m[:3, 3]
        normals = self.normals @ np.linalg.inv(m[:3, :3])
        normals /= np.linalg.norm(normals, axis=1)[:, None]
        faces = self.faces if np.linalg.det(m[:3, :3]) > 0 else self.faces[:, ::-1]
        out = Mesh(points, normals, faces.copy(), self.codes, self.colors)
        out.ao = self.ao
        return out


def decimate(points, faces, budget):
    """Blender's quadric collapse, mirror-symmetric about x, down to about budget faces."""
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
    m = o.modifiers.new('collapse', 'DECIMATE')
    m.ratio = min(1, budget / len(faces))
    m.use_symmetry = True
    m.symmetry_axis = 'X'
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


class Part:
    """One meshed part, kept at full resolution until a level of detail asks for it.

    hidden(x, y, z) is the finished body: faces deep inside it can never be
    seen, so they are dropped before decimation spends its budget on them.
    assign(centroids, normals, codes, colors) may restyle faces.
    """

    def __init__(self, field, lo, hi, voxel, code, color, budget=None, assign=None, hidden=None, adaptivity=.01):
        self.field, self.voxel, self.code, self.color = field, voxel, code, color
        self.budget, self.assign = budget, assign
        grid = sdf.Grid(lo, hi, voxel)
        values = grid.evaluate(field)
        if values.min() >= 0:
            raise ValueError('empty part')
        points, triangles, quads = sdf.mesh_field(grid, values, adaptivity)
        del values
        # Split quads along the shorter diagonal.
        a, b, c, d = (quads[:, i] for i in range(4))
        ac = np.linalg.norm(points[a] - points[c], axis=1)
        bd = np.linalg.norm(points[b] - points[d], axis=1)
        short = (ac <= bd)[:, None]
        first = np.where(short, np.stack([a, b, c], 1), np.stack([a, b, d], 1))
        second = np.where(short, np.stack([a, c, d], 1), np.stack([b, c, d], 1))
        faces = np.concatenate([triangles.reshape(-1, 3), first, second])
        if hidden is not None:
            c = points[faces].mean(axis=1)
            faces = faces[hidden(c[:, 0], c[:, 1], c[:, 2]) > -.0025]
        used = np.unique(faces)
        remap = np.full(len(points), -1)
        remap[used] = np.arange(len(used))
        self.points, self.faces = points[used], remap[faces]

    def mesh(self, detail=1.):
        points, faces = self.points, self.faces
        if self.budget and len(faces) > self.budget * detail:
            points, faces = decimate(points, faces, self.budget * detail)
        normals = sdf.normals(self.field, points, self.voxel * .5)
        # Wind every face counter-clockwise seen from outside, along the gradient.
        cross = np.cross(points[faces[:, 1]] - points[faces[:, 0]], points[faces[:, 2]] - points[faces[:, 0]])
        if np.sum(np.einsum('ij,ij->i', cross, normals[faces].sum(axis=1)) < 0) > len(faces) / 2:
            faces = faces[:, ::-1]
            cross = -cross
        faces = faces[np.linalg.norm(cross, axis=1) > 1e-12]
        codes = np.full(len(faces), self.code, np.int32)
        colors = np.tile(np.array(self.color, np.float64), (len(faces), 1))
        if self.assign:
            centroids = points[faces].mean(axis=1)
            n = normals[faces].mean(axis=1)
            n /= np.maximum(np.linalg.norm(n, axis=1)[:, None], 1e-9)
            codes, colors = self.assign(centroids, n, codes, colors)
        return Mesh(points, normals, faces, codes, colors)


def part(*args, **kwargs):
    return Part(*args, **kwargs)


def bake_ao(mesh, occluders, rays=64, reach=.45, seed=7):
    """Hemisphere ray casting from each vertex against full-resolution occluders.

    occluders are (points, faces) pairs; casting against the finest geometry
    keeps a decimated surface from shadowing itself. One pass of neighbour
    averaging then removes the remaining sampling noise.
    """
    from mathutils import Vector
    from mathutils.bvhtree import BVHTree
    offset, points, faces = 0, [], []
    for p, f in occluders:
        points.append(p)
        faces.append(f + offset)
        offset += len(p)
    tree = BVHTree.FromPolygons(np.concatenate(points).tolist(), np.concatenate(faces).tolist(),
                                all_triangles=True, epsilon=0.0)
    rng = np.random.default_rng(seed)
    # Cosine-weighted directions about +z, rotated onto each normal.
    u = rng.random((rays, 2))
    r, phi = np.sqrt(u[:, 0]), 2 * np.pi * u[:, 1]
    local = np.stack([r * np.cos(phi), r * np.sin(phi), np.sqrt(1 - u[:, 0])], 1)
    ao = np.empty(len(mesh.points))
    cast = tree.ray_cast
    for i, (p, n) in enumerate(zip(mesh.points, mesh.normals)):
        t = np.array([1., 0, 0]) if abs(n[0]) < .9 else np.array([0, 1., 0])
        b1 = np.cross(n, t)
        b1 /= np.linalg.norm(b1)
        b2 = np.cross(n, b1)
        dirs = (local[:, :1] * b1 + local[:, 1:2] * b2 + local[:, 2:] * n).tolist()
        # Start clear of decals and of the few millimetres decimation may sink.
        origin = Vector(p + n * .006)
        hidden = 0.
        for d in dirs:
            hit = cast(origin, Vector(d), reach)
            if hit[0] is not None:
                hidden += 1 - hit[3] / reach
        ao[i] = 1 - hidden / rays
    # Average each vertex with its neighbours across shared faces, twice.
    for _ in range(2):
        total, weight = ao.copy(), np.ones(len(ao))
        for a, b in ((0, 1), (1, 2), (2, 0)):
            np.add.at(total, mesh.faces[:, a], ao[mesh.faces[:, b]])
            np.add.at(weight, mesh.faces[:, a], 1)
            np.add.at(total, mesh.faces[:, b], ao[mesh.faces[:, a]])
            np.add.at(weight, mesh.faces[:, b], 1)
        ao = total / weight
    mesh.ao = np.clip(ao, 0, 1)
    return mesh


def export_tcm(path, mesh, ao_strength=.85):
    """TCM2: shared vertices (position, normal, color, material code) and triangle indices."""
    corners = mesh.faces.reshape(-1)
    face_of = np.repeat(np.arange(len(mesh.faces)), 3)
    shade = 1 - ao_strength * (1 - mesh.ao[corners])
    color = mesh.colors[face_of] * shade[:, None]
    data = np.concatenate([mesh.points[corners], mesh.corner_normals(), color,
                           mesh.codes[face_of][:, None].astype(np.float64)], axis=1).astype('<f4')
    unique, index = np.unique(data, axis=0, return_inverse=True)
    with open(path, 'wb') as f:
        f.write(b'TCM2')
        f.write(struct.pack('<II', len(unique), len(index)))
        f.write(unique.tobytes())
        f.write(index.reshape(-1).astype('<u4').tobytes())
    print('EXPORTED', os.path.basename(path), len(mesh.faces), 'triangles', len(unique), 'vertices', flush=True)


# ---------------------------------------------------------------------------
# Wheels: local axes with the outer face towards +x and the axle on x.

def polar(y, z):
    return np.sqrt(y * y + z * z), np.arctan2(y, z)


def tire_field(radius, width, rim, tread, pitches, rows=2, shoulder=.06, depth=.016):
    """A rounded tire with staggered tread blocks; tread 'block', 'slick' or 'lug'."""
    half = width / 2
    base = radius - depth

    def field(x, y, z):
        r, a = polar(y, z)
        carcass = rect2(x, r, (0, (base + rim) / 2), (half, (base - rim) / 2), shoulder)
        if tread == 'slick':
            d = rect2(x, r, (0, (radius + rim) / 2), (half, (radius - rim) / 2), shoulder)
            # Two shallow rain grooves.
            for g in (-half * .35, half * .35):
                d = subtract(d, rect2(x, r, (g, radius), (.008, .006), .003), .002)
            return d
        step = 2 * math.pi / pitches
        d = carcass
        for row in range(rows):
            lane = -half + width * (row + .5) / rows
            phase = step * .5 * (row % 2)
            u = (np.mod(a + phase, step) - step / 2) * radius
            if tread == 'lug':
                # Chevron lugs that wrap onto the shoulder.
                slant = (x - lane) * (.55 if row % 2 else -.55)
                block = rect2(u + slant, x - lane, (0, 0), (step * radius * .27, width / rows * .46), .008)
                reach = np.maximum(r - (radius + rim) / 2, 0)
                lug = smax(block, rect2(x, r, (0, (radius + rim) / 2 + .02), (half + .012, (radius - rim) / 2 - .02), shoulder * .8), .004)
                d = smin(d, lug, .004)
            else:
                block = rect2(u, x - lane, (0, 0), (step * radius * .30, width / rows * .40), .01)
                shell = rect2(x, r, (0, (radius + rim) / 2), (half, (radius - rim) / 2), shoulder)
                d = smin(d, smax(block, shell, .003), .003)
        return d
    return field


def rim_field(radius, width, spokes, style, dish=.03, hub=.07):
    """Rim barrel and spoked face. Styles: 'rally', 'star', 'mesh', 'steel'."""
    half = width / 2
    face = half - dish

    def field(x, y, z):
        r, a = polar(y, z)
        barrel = rect2(x, r, (0, radius - .03), (half - .01, .03), .008)
        lip = rect2(x, r, (half - .018, radius - .012), (.018, .022), .008)
        well = subtract(barrel, rect2(x, r, (0, radius - .03), (half - .03, .016), .004), .003)
        d = smin(well, lip, .004)
        step = 2 * math.pi / spokes
        u = (np.mod(a, step) - step / 2) * r
        disc = rect2(x, r, (face - .012, (radius - .02) / 2 + .01), (.018, (radius - .02) / 2), .01)
        if style == 'steel':
            holes = circle2(u, r, (0, radius * .62), radius * .11)
            plate = subtract(disc, smax(holes, np.abs(x - face) - .05, .002), .006)
            cap = cylinder_x(x, y, z, (face + .01, 0, 0), radius * .55, .02, .015)
            return union(d, plate, cap, k=.006)
        if style == 'mesh':
            # Cross-laced BBS-style mesh between the hub and a ring.
            v = (np.mod(a, step / 2) - step / 4) * r
            lace = np.minimum(np.abs(u * .8 + (r - radius * .5) * .5), np.abs(u * .8 - (r - radius * .5) * .5)) - .009
            web = smax(smax(lace, np.abs(x - face + .004) - .012, .003), rect2(x, r, (face, radius * .55), (.05, radius * .40), .01), .004)
            spoke = smax(np.abs(v) - .008, rect2(x, r, (face - .004, radius * .55), (.014, radius * .42), .006), .003)
            d = union(d, web, spoke, k=.004)
        else:
            width_at = (.026 if style == 'rally' else .018) + .012 * (1 - r / radius)
            spoke = smax(np.abs(u) - width_at, rect2(x, r, (face - .006 + (r - hub) * (dish * .6 / radius), (radius + hub) / 2), (.016, (radius - hub) / 2 + .01), .008), .008)
            d = smin(d, spoke, .012)
        centre = cylinder_x(x, y, z, (face - .004, 0, 0), hub + .012, .022, .012)
        d = smin(d, centre, .01)
        cap = cylinder_x(x, y, z, (face + .016, 0, 0), hub * .55, .012, .008)
        d = smin(d, cap, .004)
        # Lug nuts on a pitch circle.
        nut_step = 2 * math.pi / 5
        nu = (np.mod(a + .3, nut_step) - nut_step / 2) * r
        nuts = smax(circle2(nu, r, (0, hub * .78), .011), np.abs(x - face - .012) - .012, .003)
        return smin(d, nuts, .002)
    return field


def wheel(radius, width, rim_radius, tread, rim_style, spokes, rim_color, caliper=(.78, .08, .05),
          pitches=24, rows=2, shoulder=.06, depth=.016, rim_code=GLOSS):
    half = width / 2
    lo, hi = (-half - .01, -radius - .01, -radius - .01), (half + .01, radius + .01, radius + .01)
    tire = part(tire_field(radius, width, rim_radius - .006, tread, pitches, rows, shoulder, depth),
                lo, hi, .004, RUBBER, RUBBER_BLACK, 4000)
    rim = part(rim_field(rim_radius, width - .04, spokes, rim_style), lo, hi, .003, rim_code, rim_color, 3000)
    disc_r = rim_radius - .07
    disc = part(lambda x, y, z: subtract(cylinder_x(x, y, z, (-.02, 0, 0), disc_r, .012, .003),
                                         cylinder_x(x, y, z, (-.02, 0, 0), .05, .05), .003),
                lo, hi, .004, METAL, (.36, .36, .37), 300)

    def caliper_field(x, y, z):
        r, a = polar(y, z)
        # Mounted behind the axle, above it.
        arc = np.abs(np.arctan2(np.sin(a - 2.2), np.cos(a - 2.2))) * r - .07
        shape = smax(smax(arc, np.abs(r - disc_r + .02) - .04, .015), np.abs(x + .02) - .035, .01)
        return shape
    calipers = part(caliper_field, lo, hi, .004, GLOSS + HUB, caliper, 240)
    return [tire, rim, disc, calipers]


def wheel_mesh(parts, detail=1.):
    return bake_ao(Mesh.join([p.mesh(detail) for p in parts]), [(p.points, p.faces) for p in parts], reach=.25)


# ---------------------------------------------------------------------------
# Shared body features.

def relu(v, r=.03):
    """A ramp from zero, rounded over r so the crease it makes stays smooth."""
    return np.where(v > r, v, np.where(v < -r, 0, (v + r) ** 2 / (4 * r)))


def pocket(d, ref, region, depth, k=.003):
    """Recesses d by depth below the reference surface wherever region < 0."""
    return subtract(d, smax(region, -(ref + depth), .0015), k)


def inlay(ref, region, at, thickness=.012):
    """A skin `at` below the reference surface inside region: a pocket's floor part."""
    return smax(smax(ref + at, region - .004, .002), -(ref + at + thickness), 0)


def decal(ref, region, lift=.0025, thickness=.007):
    """A printed panel standing `lift` proud of the reference surface inside region."""
    return smax(smax(ref - lift, -(ref + thickness), 0), region, .001)


def groove(d, ref, distance, width=.0035, depth=.006):
    return subtract(d, smax(distance - width, -(ref + depth), .001), .0025)


def face_rule(centroids, normals, codes, colors, rules):
    """Applies (predicate, code, color) rules to faces, later rules winning."""
    for test, code, color in rules:
        hit = test(centroids, normals)
        codes = np.where(hit, code, codes)
        colors = np.where(hit[:, None], np.array(color), colors)
    return codes, colors


def clip_decal(mesh, region, code, color, lift=.0015, refine=True):
    """The part of a finished surface where region < 0, lifted along its normals.

    Triangles crossing the outline are clipped at the exact crossing, so print
    edges stay crisp at any level of detail and the decal never parts from
    the surface it was cut from.
    """
    p, n, faces = mesh.points, mesh.normals, mesh.faces
    if isinstance(region, (list, tuple)):
        # An intersection of half-spaces: each is linear, so it clips exactly.
        out = Mesh(p, n, faces, mesh.codes, mesh.colors)
        for plane in region:
            out = clip_decal(out, plane, code, color, 0, False)
        out.points = out.points + out.normals * lift
        return out
    v = region(p[:, 0], p[:, 1], p[:, 2])
    # Halve the longest edge of triangles the outline crosses until it is
    # finely sampled; the midpoints lie on the surface's own triangles.
    for _ in range(14 if refine else 0):
        edges = np.stack([np.linalg.norm(p[faces[:, a]] - p[faces[:, b]], axis=1)
                          for a, b in ((0, 1), (1, 2), (2, 0))], 1)
        e = edges.max(axis=1)
        split = (np.min(v[faces], axis=1) < e * .5) & (np.max(v[faces], axis=1) > -e * .5) & (e > .015)
        if not split.any():
            break
        work = faces[split]
        k = edges[split].argmax(axis=1)
        a = work[np.arange(len(work)), k]
        b = work[np.arange(len(work)), (k + 1) % 3]
        c = work[np.arange(len(work)), (k + 2) % 3]
        unique, inverse = np.unique(np.sort(np.stack([a, b], 1), axis=1), axis=0, return_inverse=True)
        mid_p = (p[unique[:, 0]] + p[unique[:, 1]]) / 2
        mid_n = n[unique[:, 0]] + n[unique[:, 1]]
        mid_n /= np.linalg.norm(mid_n, axis=1)[:, None]
        m = len(p) + inverse.reshape(-1)
        p, n = np.concatenate([p, mid_p]), np.concatenate([n, mid_n])
        v = np.concatenate([v, region(mid_p[:, 0], mid_p[:, 1], mid_p[:, 2])])
        faces = np.concatenate([faces[~split], np.stack([a, m, c], 1), np.stack([m, b, c], 1)])
    inside = v < 0
    hit = inside[faces]
    points, normals = list(p), list(n)
    faces_out = [tuple(f) for f in faces[hit.all(axis=1)]]
    for f in faces[hit.any(axis=1) & ~hit.all(axis=1)]:
        polygon = []
        for a, b in ((f[0], f[1]), (f[1], f[2]), (f[2], f[0])):
            if inside[a]:
                polygon.append(a)
            if inside[a] != inside[b]:
                t = v[a] / (v[a] - v[b])
                points.append(p[a] + (p[b] - p[a]) * t)
                normal = n[a] + (n[b] - n[a]) * t
                normals.append(normal / np.linalg.norm(normal))
                polygon.append(len(points) - 1)
        for k in range(1, len(polygon) - 1):
            faces_out.append((polygon[0], polygon[k], polygon[k + 1]))
    if not faces_out:
        raise ValueError('decal misses the surface')
    faces = np.array(faces_out)
    used = np.unique(faces)
    remap = np.full(len(points), -1)
    remap[used] = np.arange(len(used))
    points, normals = np.array(points)[used], np.array(normals)[used]
    return Mesh(points + normals * lift, normals, remap[faces], np.full(len(faces), code, np.int32),
                np.tile(np.array(color, np.float64), (len(faces), 1)))


def strip(axes, a, b, width, side=None):
    """Half-spaces of a straight band from a to b in the plane of two axes (0 x,
    1 y, 2 z), projected along the third; side limits it to x > side or x < side."""
    (i, j), (au, av), (bu, bv) = axes, a, b
    length = math.hypot(bu - au, bv - av)
    du, dv = (bu - au) / length, (bv - av) / length
    planes = [lambda x, y, z: ((x, y, z)[i] - au) * dv - ((x, y, z)[j] - av) * du - width,
              lambda x, y, z: -((x, y, z)[i] - au) * dv + ((x, y, z)[j] - av) * du - width,
              lambda x, y, z: -((x, y, z)[i] - au) * du - ((x, y, z)[j] - av) * dv - width,
              lambda x, y, z: ((x, y, z)[i] - au) * du + ((x, y, z)[j] - av) * dv - length - width]
    if side is not None:
        planes.append(lambda x, y, z: (side - x) if side > 0 else (x - side))
    return planes


def convex(axes, polygon, side=None):
    """Half-spaces of a convex outline in the plane of two axes, projected along the third."""
    i, j = axes
    area = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(polygon, polygon[1:] + polygon[:1]))
    planes = []
    for (au, av), (bu, bv) in zip(polygon, polygon[1:] + polygon[:1]):
        length = math.hypot(bu - au, bv - av)
        if length < 1e-6:
            continue
        nu, nv = (bv - av) / length, -(bu - au) / length
        if area < 0:
            nu, nv = -nu, -nv
        planes.append(lambda x, y, z, au=au, av=av, nu=nu, nv=nv:
                      ((x, y, z)[i] - au) * nu + ((x, y, z)[j] - av) * nv)
    if side is not None:
        planes.append(lambda x, y, z: (side - x) if side > 0 else (x - side))
    return planes


class Car:
    """Parts in car space, wheel placement and lamp centres for renderer.c.

    The first part is the painted shell; decals are printed onto it.
    """

    def __init__(self, name, wheel_parts, wheel_x, wheel_y, axles):
        self.name, self.wheel, self.parts = name, wheel_parts, []
        self.wheel_x, self.wheel_y, self.axles = wheel_x, wheel_y, axles
        self.lamps, self.decals = {}, []

    def wheel_matrices(self):
        out = []
        for z in self.axles:
            for side in (-1, 1):
                m = np.eye(4)
                if side < 0:
                    m[:3, :3] = np.diag([-1., 1, -1])  # turned half way round, not mirrored
                m[:3, 3] = (side * self.wheel_x, self.wheel_y, z)
                out.append(m)
        return out

    def build(self, detail=1., rays=64):
        """Body and wheel meshes for one level of detail, occlusion baked."""
        wheel = wheel_mesh(self.wheel, detail)
        meshes = [p.mesh(detail) for p in self.parts]
        decals = [clip_decal(meshes[0], *d) for d in self.decals]
        print('decal triangles', [len(d.faces) for d in decals], flush=True)
        body = Mesh.join(meshes + decals)
        occluders = [(p.points, p.faces) for p in self.parts]
        for m in self.wheel_matrices():
            for p in self.wheel:
                occluders.append((p.points @ m[:3, :3].T + m[:3, 3], p.faces))
        return bake_ao(body, occluders, rays=rays, reach=.45), wheel


# ---------------------------------------------------------------------------
# Trailblazer: a three-door rally hatch with box flares, spot lamps and mud flaps.

def smoothstep(e0, e1, v):
    t = np.clip((v - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def trailblazer():
    W, FRONT, REAR, WX, WY, TR, ARCH = .835, .945, -.95, .845, .38, .42, .475
    P = sdf.Profile
    lower_side = P(rounded_polygon([(-1.505, .25, .05), (1.30, .25, .12), (1.548, .37, .08), (1.565, .665, .10),
                                    (1.48, .845, .10), (.42, .965, .06), (-1.37, .99, .06), (-1.54, .93, .08),
                                    (-1.552, .32, .05)], .05))
    lower_plan = P(rounded_polygon([(-.77, 1.57, .16), (.77, 1.57, .16), (W, 1.20, .3), (W, -1.25, .3),
                                    (.81, -1.56, .14), (-.81, -1.56, .14), (-W, -1.25, .3), (-W, 1.20, .3)], .1))
    cabin_side = P(rounded_polygon([(.52, .90, 0), (.42, .965, .03), (-.10, 1.42, .09), (-1.10, 1.455, .10),
                                    (-1.43, 1.03, .04), (-1.47, .90, 0)], .03))
    side_glass = P(rounded_polygon([(.29, 1.012), (-.135, 1.36), (-.50, 1.363), (-.50, 1.012)], .03))
    quarter_glass = P(rounded_polygon([(-.58, 1.012), (-.58, 1.363), (-1.03, 1.355), (-1.23, 1.03)], .03))
    front_glass = P(rounded_polygon([(-.675, .998), (.675, .998), (.545, 1.37), (-.545, 1.37)], .05))
    rear_glass = P(rounded_polygon([(-.61, 1.06), (.61, 1.06), (.54, 1.41), (-.54, 1.41)], .05))
    door_plate = P(rounded_polygon([(.18, .54), (.18, .80), (-.40, .80), (-.40, .54)], .05))

    def cabin_width(y):
        return .80 - .30 * (y - .965)

    def cabin(x, y, z):
        return smax(cabin_side(z, y + .05 * x * x), (np.abs(x) - cabin_width(y)) / 1.044, .07)

    def reach(z):
        return np.maximum(smoothstep(.64, .50, np.abs(z - FRONT)), smoothstep(.64, .50, np.abs(z - REAR)))

    def shell(x, y, z):
        ax = np.abs(x)
        tumble = .45 * relu(.44 - y) + .28 * relu(y - .76)
        # Box flares push the sides out over the wheels and lift the fender tops.
        flare = .14 * reach(z) * smoothstep(.95, .86, y)
        lift = .07 * reach(z) * smoothstep(.50, .72, ax)
        d = smax(lower_side(z, y + .045 * x * x - lift), lower_plan(ax + tumble - flare, z), .075)
        d = smin(d, cabin(x, y, z), .02)
        # Roof scoop and spoiler.
        d = smin(d, box(x, y, z, (0, 1.45, -.30), (.13, .05, .14), .035), .02)
        d = smin(d, box(x, y, z, (0, 1.453, -1.16), (.64, .018, .12), .016), .015)
        d = smin(d, box(ax, y, z, (.64, 1.425, -1.16), (.014, .045, .12), .012), .008)
        # Aero mirrors on short stalks.
        d = smin(d, box(ax, y, z, (.915, 1.07, .27), (.075, .045, .045), .035), .006)
        d = smin(d, capsule(ax, y, z, (.79, 1.01, .31), (.87, 1.045, .285), .018), .012)
        for zc in (FRONT, REAR):
            d = subtract(d, cylinder_x(ax, y, z, (1.0, WY, zc), ARCH, .39), .02)
        return d

    def windows(x, y, z):
        ax = np.abs(x)
        s = smax(np.minimum(side_glass(z, y), quarter_glass(z, y)), cabin_width(y) - .12 - ax, 0)
        f = smax(front_glass(x, y), -(z + .25), 0)
        r = smax(rear_glass(x, y), z + 1.0, 0)
        return np.minimum(np.minimum(s, f), r)

    def front_face(region):
        return lambda x, y, z: smax(region(np.abs(x), y), 1.2 - z, 0)

    def rear_face(region):
        return lambda x, y, z: smax(region(np.abs(x), y), z + 1.3, 0)

    head = front_face(lambda u, v: rect2(u, v, (.53, .735), (.185, .068), .03))
    grille = front_face(lambda u, v: rect2(u, v, (0, .73), (.31, .062), .025))
    intake = front_face(lambda u, v: rect2(u, v, (0, .43), (.40, .062), .03))
    tail = rear_face(lambda u, v: rect2(u, v, (.56, .835), (.20, .06), .022))
    plate = rear_face(lambda u, v: rect2(u, v, (0, .61), (.21, .056), .012))

    def handle(x, y, z):
        return smax(rect2(z, y, (-.44, .905), (.065, .017), .014), .7 - np.abs(x), 0)

    def scoop_mouth(x, y, z):
        return smax(rect2(x, y, (0, 1.463), (.092, .024), .012), -.2 - z, 0)

    door_lines = [[(.31, .29), (.31, .965)], [(-.535, .29), (-.535, 1.0)], [(.31, .30), (-.535, .30)]]
    hood_line = [(-.70, .44), (-.70, 1.43), (.70, 1.43), (.70, .44)]
    POCKETS = ((head, .035), (grille, .045), (intake, .05), (tail, .022), (plate, .007), (handle, .009),
               (scoop_mouth, .07))

    def body(x, y, z):
        ref = shell(x, y, z)
        d = pocket(ref, ref, windows(x, y, z), .012, .005)
        for region, depth in POCKETS:
            d = pocket(d, ref, region(x, y, z), depth, .004)
        return d

    def glass(x, y, z):
        c = cabin(x, y, z)
        return smax(smax(c + .007, windows(x, y, z) - .015, .002), -(c + .025), 0)

    # Floors for the lamp, grille and plate pockets, a little above each pocket's bottom.
    FLOORS = {'reflector': (head, .028), 'grille': (grille, .04), 'intake': (intake, .045),
              'tail': (tail, .007), 'plate': (plate, .002)}

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
            well |= (r < ARCH + .01) & (ax < .99) & ((inward > .4) | ((ax < .66) & (np.abs(n[:, 0]) > .6)))
        return well | ((n[:, 1] < -.55) & (c[:, 1] < .34))

    def mouth(c, n):
        return ((np.abs(c[:, 0]) < .1) & (c[:, 1] > 1.425) & (c[:, 1] < 1.495) & (c[:, 2] > -.40)
                & (c[:, 2] < -.14) & (n[:, 1] < .9))

    V = .004
    car = Car('rally_car', wheel(TR, .29, .285, 'block', 'rally', 6, (.92, .91, .88), pitches=22),
              WX, WY, (FRONT, REAR))
    add = car.parts.append
    add(part(body, (-1.08, -.02, -1.66), (1.08, 1.56, 1.70), V, PAINT, (1, 1, 1), 50000,
             lambda c, n, k, col: face_rule(c, n, k, col, [(underside, TRIM, INK), (mouth, TRIM, INK)])))
    # Only the outer skin of the glass is kept, shaded with the cabin's own normals:
    # decimated glass would otherwise mirror its triangles in streaks.
    pane = part(glass, (-.84, .94, -1.5), (.84, 1.49, .5), V, GLASS, TINT, 5000,
                hidden=lambda x, y, z: cabin(x, y, z) + .016)
    pane.field = cabin
    add(pane)

    front_lo, front_hi = (-.8, .30, 1.30), (.8, .84, 1.68)
    add(part(floor('reflector'), front_lo, front_hi, .003, CHROME, SILVER, 1000))

    def eyes(x, y, z):
        ax = np.abs(x)
        e = np.minimum(ellipsoid(ax, y, z, (.44, .735, 1.47), (.05, .05, .12)),
                       ellipsoid(ax, y, z, (.62, .735, 1.47), (.05, .05, .12)))
        return smax(e, shell(x, y, z) + .009, .004)
    add(part(eyes, front_lo, front_hi, .003, HEADLAMP, (1, .97, .9), 1400, hidden=body))
    add(part(floor('grille'), front_lo, front_hi, .003, TRIM, INK, 400))

    def grille_bars(x, y, z):
        ref = shell(x, y, z)
        bars = np.abs(np.mod(y - .73 + .015, .03) - .015) - .0065
        return smax(smax(smax(bars, grille(x, y, z) + .002, .002), ref + .014, .003), -(ref + .045), 0)
    add(part(grille_bars, front_lo, front_hi, .003, TRIM, (.10, .105, .11), 1400, hidden=body))
    add(part(floor('intake'), front_lo, front_hi, .003, TRIM, INK, 400))

    def intake_slats(x, y, z):
        ref = shell(x, y, z)
        slats = np.minimum(np.abs(y - .43) - .007, np.abs(np.abs(x) - .20) - .007)
        return smax(smax(smax(slats, intake(x, y, z) + .002, .002), ref + .012, .003), -(ref + .05), 0)
    add(part(intake_slats, front_lo, front_hi, .003, TRIM, (.10, .105, .11), 800, hidden=body))

    # Rally spot lamps on the bumper, and a red tow hook.
    spot_lo, spot_hi = (-.42, .38, 1.50), (.42, .60, 1.68)
    add(part(lambda x, y, z: smin(cylinder_z(np.abs(x), y, z, (.30, .49, 1.585), .085, .045, .025),
                                  box(np.abs(x), y, z, (.30, .42, 1.555), (.02, .04, .02), .008), .01),
             spot_lo, spot_hi, .003, TRIM, INK, 1200, hidden=body))
    add(part(lambda x, y, z: smin(cylinder_z(np.abs(x), y, z, (.30, .49, 1.632), .068, .006, .005),
                                  ellipsoid(np.abs(x), y, z, (.30, .49, 1.625), (.06, .06, .018)), .004),
             spot_lo, spot_hi, .003, HEADLAMP, (1, .93, .72), 800, hidden=body))
    add(part(lambda x, y, z: sdf.torus_x(z, y, np.abs(x), (1.631, .49, .30), .072, .008),
             spot_lo, spot_hi, .003, CHROME, SILVER, 800, hidden=body))
    add(part(lambda x, y, z: sdf.torus_x(x, y, z, (.56, .33, 1.585), .035, .009),
             (.48, .26, 1.50), (.64, .40, 1.66), .002, GLOSS, (.80, .07, .05), 300, hidden=body))

    # Tail lamps with white reversing lenses inboard, and the plate.
    rear_lo, rear_hi = (-.8, .40, -1.62), (.8, 1.02, -1.30)
    tail_floor = floor('tail')
    add(part(lambda x, y, z: smax(tail_floor(x, y, z), .45 - np.abs(x), .002), rear_lo, rear_hi, .003,
             TAILLAMP, (.85, .06, .04), 600))
    add(part(lambda x, y, z: smax(tail_floor(x, y, z), np.abs(x) - .45, .002), rear_lo, rear_hi, .003,
             GLOSS, (.92, .92, .9), 300))
    add(part(floor('plate'), rear_lo, rear_hi, .003, GLOSS, (.95, .78, .16), 240))

    # Bumpers, skid plate, sills and mud flaps.
    def trim(x, y, z):
        ax = np.abs(x)
        outline = lower_plan(ax, z)
        skid = smax(smax(outline - .012, np.abs(y - .262) - .022, .01), 1.18 - z, .02)
        rear = smax(smax(outline - .02, np.abs(y - .31) - .07, .02), z + 1.42, .02)
        return union(skid, rear, box(ax, y, z, (.80, .29, 0), (.055, .04, .33), .03),
                     box(ax, y, z, (.84, .20, FRONT - .49), (.13, .10, .007), .005),
                     box(ax, y, z, (.84, .20, REAR - .49), (.13, .10, .007), .005))
    add(part(trim, (-1.0, .03, -1.62), (1.0, .4, 1.62), V, TRIM, INK, 3200, hidden=body))
    add(part(lambda x, y, z: subtract(cylinder_z(x, y, z, (.50, .25, -1.565), .045, .06, .012),
                                      cylinder_z(x, y, z, (.50, .25, -1.60), .033, .07), .004),
             (.4, .15, -1.65), (.6, .35, -1.45), .003, CHROME, SILVER, 600, hidden=body))
    # Mirror glass, door handles and wiper arms.
    add(part(lambda x, y, z: smax(box(np.abs(x), y, z, (.92, 1.07, .225), (.062, .035, .02), .015), z - .232, .002),
             (-1.0, .98, .14), (1.0, 1.15, .38), .003, CHROME, SILVER, 400, hidden=body))

    def handle_bar(x, y, z):
        ref = shell(x, y, z)
        bar = smax(rect2(z, y, (-.44, .905), (.05, .0065), .006), .7 - np.abs(x), 0)
        return smax(smax(ref + .0025, -(ref + .009), 0), bar, .002)
    add(part(handle_bar, (-.9, .85, -.55), (.9, .96, -.33), .002, CHROME, SILVER, 320, hidden=body))

    def wipers(x, y, z):
        arms = np.minimum(capsule(x, y, z, (-.52, 1.02, .37), (.02, 1.035, .385), .009),
                          capsule(x, y, z, (.06, 1.02, .37), (.58, 1.035, .385), .009))
        return smax(arms, -(cabin(x, y, z) + .002), .002)
    add(part(wipers, (-.65, .95, .30), (.65, 1.10, .46), .002, TRIM, INK, 600, hidden=body))

    # Livery: twin stripes over hood, roof and tailgate, and door plates.
    for side in (1, -1):
        car.decals.append(([lambda x, y, z, s=side: s * x - .26, lambda x, y, z, s=side: .16 - s * x,
                            lambda x, y, z: .82 - y, lambda x, y, z: z - 1.47, lambda x, y, z: -1.39 - z],
                           GLOSS, IVORY))
        car.decals.append((convex((2, 1), rounded_polygon([(.18, .54), (.18, .80), (-.40, .80), (-.40, .54)], .05, 3),
                                  .6 * side), GLOSS, IVORY))
    # Panel gaps around the doors and the bonnet.
    for line in door_lines:
        for side in (.6, -.6):
            car.decals.append((strip((2, 1), *line, .0018, side), TRIM, (.02, .02, .022)))
    for a, b in zip(hood_line[:-1], hood_line[1:]):
        car.decals.append((strip((0, 2), a, b, .0018) + [lambda x, y, z: .80 - y], TRIM, (.02, .02, .022)))
    # Fuel filler cap on the right rear quarter.
    add(part(lambda x, y, z: smax(cylinder_x(x, y, z, (.80, .83, -1.17), .05, .03, .006), -(body(x, y, z) + .0015), .002),
             (.6, .74, -1.26), (.95, .92, -1.08), .002, GLOSS, (.08, .08, .09), 300))
    car.lamps = {'head': (.53, .735, 1.53), 'tail': (.56, .835, -1.545)}
    return car


# ---------------------------------------------------------------------------
# Export: two levels of detail per car, wheels separate, plus the Blender scene.

BLEND_MATERIALS = {PAINT: (.25, 0), GLASS: (.05, 0), CHROME: (.12, 1), RUBBER: (.75, 0), TRIM: (.45, 0),
                   GLOSS: (.3, 0), HEADLAMP: (.08, 0), TAILLAMP: (.15, 0), METAL: (.35, 1)}


def blender_object(name, mesh, paint=(.96, .25, .075), matrix=None):
    """An editable Blender object with one material per code and occlusion in a color layer."""
    import bpy
    if matrix is not None:
        mesh = mesh.transformed(matrix)
    me = bpy.data.meshes.new(name)
    me.from_pydata([(p[0], -p[2], p[1]) for p in mesh.points], [], [tuple(f) for f in mesh.faces])
    codes = sorted(set(int(c) for c in mesh.codes))
    for c in codes:
        rough, metal = BLEND_MATERIALS[c % HUB]
        m = bpy.data.materials.get('ToyCars %d' % c) or bpy.data.materials.new('ToyCars %d' % c)
        m.use_nodes = True
        m['code'] = c
        nt = m.node_tree
        bsdf = nt.nodes['Principled BSDF']
        bsdf.inputs['Roughness'].default_value = rough
        bsdf.inputs['Metallic'].default_value = metal
        if not any(n.type == 'VERTEX_COLOR' for n in nt.nodes):
            col = nt.nodes.new('ShaderNodeVertexColor')
            col.layer_name = 'Color'
            if c % HUB == PAINT:
                mix = nt.nodes.new('ShaderNodeMix')
                mix.data_type, mix.blend_type = 'RGBA', 'MULTIPLY'
                mix.inputs['Factor'].default_value = 1
                mix.inputs[6].default_value = (*paint, 1)
                nt.links.new(col.outputs['Color'], mix.inputs[7])
                nt.links.new(mix.outputs[2], bsdf.inputs['Base Color'])
                bsdf.inputs['Coat Weight'].default_value = 1
            else:
                nt.links.new(col.outputs['Color'], bsdf.inputs['Base Color'])
        me.materials.append(m)
    index = {c: i for i, c in enumerate(codes)}
    me.polygons.foreach_set('material_index', [index[int(c)] for c in mesh.codes])
    layer = me.color_attributes.new('Color', 'FLOAT_COLOR', 'CORNER')
    shade = 1 - .85 * (1 - mesh.ao[mesh.faces.reshape(-1)])
    colors = np.repeat(mesh.colors, 3, axis=0) * shade[:, None]
    layer.data.foreach_set('color', np.concatenate([colors, np.ones((len(colors), 1))], 1).ravel())
    me.polygons.foreach_set('use_smooth', [True] * len(me.polygons))
    me.normals_split_custom_set([(n[0], -n[2], n[1]) for n in mesh.corner_normals()])
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    return o


def empty_scene():
    """Removes every object and mesh, keeping materials others may hold on to."""
    import bpy
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    for m in list(bpy.data.meshes):
        bpy.data.meshes.remove(m)


def export_car(design, stem, blend):
    """Writes <stem>.tcm, <stem>-wheel.tcm and their '-far' levels of detail."""
    import bpy
    car = design()
    empty_scene()
    for suffix, detail in (('', 1.), ('-far', .25)):
        body, wheel = car.build(detail)
        export_tcm(os.path.join(ROOT, 'assets/models', stem + suffix + '.tcm'), body)
        export_tcm(os.path.join(ROOT, 'assets/models', stem + '-wheel' + suffix + '.tcm'), wheel)
        if not suffix:
            blender_object(car.name + ' body', body)
            for i, m in enumerate(car.wheel_matrices()):
                blender_object(car.name + ' wheel %d' % i, wheel, matrix=m)
    radius = float(np.max(np.linalg.norm(wheel.points[:, 1:], axis=1)))
    print('WHEELS', stem, car.wheel_x, car.wheel_y, car.axles, round(radius, 3), flush=True)
    print('LAMPS', stem, car.lamps, flush=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT, 'art', blend + '.blend'))


# Asset stem, Blender scene and the design function (module:function for the others).
CARS = [('car', 'rally_car', 'trailblazer'), ('sprite', 'sprite', 'car_sprite:sprite'),
        ('bruiser', 'bruiser', 'car_bruiser:bruiser'), ('comet', 'comet', 'car_comet:comet')]


def build(only=None):
    import importlib
    for stem, blend, design in CARS:
        if only in (None, stem):
            module, name = design.split(':') if ':' in design else (__name__, design)
            export_car(getattr(importlib.import_module(module), name), stem, blend)

