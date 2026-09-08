"""
tcmesh - shared helpers for generating the ToyCars models in Blender.

Conventions:
  Blender: Z = up, -Y = forward (a car faces -Y).
  The USD export converts that to Y-up / -Z forward (RealityKit).
  Units: metres. Car length ~3.6 m, track width ~14 m.
"""
import bpy
import bmesh
import math
import os
from mathutils import Vector, Matrix, Euler

TAU = math.pi * 2.0


# ---------------------------------------------------------------- scene setup

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    sc.unit_settings.scale_length = 1.0
    # delete every orphaned datablock
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.objects,
                 bpy.data.images, bpy.data.curves):
        for d in list(coll):
            try:
                coll.remove(d)
            except Exception:
                pass


def srgb_to_linear(c):
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4


def hexcol(h, alpha=1.0):
    """'#ff8800' -> a linear RGBA tuple for Blender."""
    h = h.lstrip('#')
    r = int(h[0:2], 16) / 255.0
    g = int(h[2:4], 16) / 255.0
    b = int(h[4:6], 16) / 255.0
    return (srgb_to_linear(r), srgb_to_linear(g), srgb_to_linear(b), alpha)


_MAT_CACHE = {}


def material(name, color, roughness=0.45, metallic=0.0, emission=None,
             emission_strength=1.0, alpha=1.0, clearcoat=0.0):
    """Creates (or returns) a Principled BSDF material - exported as UsdPreviewSurface."""
    key = name
    if key in _MAT_CACHE and _MAT_CACHE[key].name in bpy.data.materials:
        return _MAT_CACHE[key]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes.get("Principled BSDF")
    if isinstance(color, str):
        color = hexcol(color, alpha)
    bsdf.inputs["Base Color"].default_value = color
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    if "Alpha" in bsdf.inputs:
        bsdf.inputs["Alpha"].default_value = alpha
    if alpha < 1.0:
        m.blend_method = 'BLEND'
    if emission is not None:
        if isinstance(emission, str):
            emission = hexcol(emission)
        for key_name in ("Emission Color", "Emission"):
            if key_name in bsdf.inputs:
                bsdf.inputs[key_name].default_value = emission
                break
        if "Emission Strength" in bsdf.inputs:
            bsdf.inputs["Emission Strength"].default_value = emission_strength
    if clearcoat > 0.0:
        for key_name in ("Coat Weight", "Clearcoat"):
            if key_name in bsdf.inputs:
                bsdf.inputs[key_name].default_value = clearcoat
                break
    m.diffuse_color = color
    _MAT_CACHE[key] = m
    return m


def clear_material_cache():
    _MAT_CACHE.clear()


# ---------------------------------------------------------------- mesh building

def assign_material_by_face(ob, mats, picker):
    """mats = list of materials, picker(center, normal, index) -> index into mats."""
    ob.data.materials.clear()
    for m in mats:
        ob.data.materials.append(m)
    for i, p in enumerate(ob.data.polygons):
        p.material_index = max(0, min(len(mats) - 1,
                                      picker(p.center, p.normal, i)))
    return ob


def mesh_from_pydata(name, verts, faces, mat=None, smooth=False):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in faces])
    me.validate(verbose=False)
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    if mat is not None:
        me.materials.append(mat)
    if smooth:
        shade_smooth(ob, angle=math.radians(40))
    return ob


def shade_smooth(ob, angle=math.radians(35)):
    """Auto-smooth: smooth shading + EDGE_SPLIT for edges sharper than the limit."""
    for p in ob.data.polygons:
        p.use_smooth = True
    for m in ob.modifiers:
        if m.type == 'EDGE_SPLIT':
            m.split_angle = angle
            return m
    mod = ob.modifiers.new("AutoSmooth", 'EDGE_SPLIT')
    mod.use_edge_angle = True
    mod.use_edge_sharp = True
    mod.split_angle = angle
    return mod


def shade_flat(ob):
    for p in ob.data.polygons:
        p.use_smooth = False


def bevel(ob, width=0.02, segments=2, angle_limit=math.radians(45), clamp=True):
    m = ob.modifiers.new("Bevel", 'BEVEL')
    m.width = width
    m.segments = segments
    m.limit_method = 'ANGLE'
    m.angle_limit = angle_limit
    m.use_clamp_overlap = clamp
    m.harden_normals = False
    return m


def subsurf(ob, levels=1, render=None):
    m = ob.modifiers.new("Subsurf", 'SUBSURF')
    m.levels = levels
    m.render_levels = levels if render is None else render
    m.use_limit_surface = True
    return m


def apply_modifiers(ob):
    dg = bpy.context.evaluated_depsgraph_get()
    ob_eval = ob.evaluated_get(dg)
    me = bpy.data.meshes.new_from_object(ob_eval)
    old = ob.data
    ob.modifiers.clear()
    ob.data = me
    try:
        bpy.data.meshes.remove(old)
    except Exception:
        pass
    return ob


def join(objects, name):
    """Joins objects into one, preserving the material slots."""
    objects = [o for o in objects if o is not None]
    if not objects:
        return None
    if len(objects) == 1:
        objects[0].name = name
        return objects[0]
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    res = bpy.context.view_layer.objects.active
    res.name = name
    return res


# ---------------------------------------------------------------- primitives

def box(name, size, center=(0, 0, 0), mat=None):
    sx, sy, sz = (s * 0.5 for s in size)
    cx, cy, cz = center
    v = [(cx - sx, cy - sy, cz - sz), (cx + sx, cy - sy, cz - sz),
         (cx + sx, cy + sy, cz - sz), (cx - sx, cy + sy, cz - sz),
         (cx - sx, cy - sy, cz + sz), (cx + sx, cy - sy, cz + sz),
         (cx + sx, cy + sy, cz + sz), (cx - sx, cy + sy, cz + sz)]
    f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
         (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    return mesh_from_pydata(name, v, f, mat)


def cylinder(name, radius, depth, segments=16, center=(0, 0, 0), axis='Z',
             mat=None, smooth=True, radius_top=None):
    rt = radius if radius_top is None else radius_top
    verts, faces = [], []
    h = depth * 0.5
    for i in range(segments):
        a = TAU * i / segments
        c, s = math.cos(a), math.sin(a)
        verts.append((c * radius, s * radius, -h))
    for i in range(segments):
        a = TAU * i / segments
        c, s = math.cos(a), math.sin(a)
        verts.append((c * rt, s * rt, h))
    for i in range(segments):
        j = (i + 1) % segments
        faces.append((i, j, segments + j, segments + i))
    faces.append(tuple(reversed(range(segments))))
    faces.append(tuple(range(segments, segments * 2)))
    ob = mesh_from_pydata(name, verts, faces, mat, smooth=smooth)
    if axis == 'X':
        rot = Euler((0, math.radians(90), 0)).to_matrix().to_4x4()
    elif axis == 'Y':
        rot = Euler((math.radians(90), 0, 0)).to_matrix().to_4x4()
    else:
        rot = Matrix.Identity(4)
    ob.data.transform(rot)
    ob.data.transform(Matrix.Translation(Vector(center)))
    return ob


def uv_sphere(name, radius, segments=20, rings=12, center=(0, 0, 0), mat=None,
              scale=(1, 1, 1)):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=rings,
                              radius=radius)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    if mat:
        me.materials.append(mat)
    ob.data.transform(Matrix.Diagonal(Vector(scale + (1.0,))))
    ob.data.transform(Matrix.Translation(Vector(center)))
    shade_smooth(ob)
    return ob


def cone(name, radius, depth, segments=16, center=(0, 0, 0), mat=None,
         radius_top=0.0):
    return cylinder(name, radius, depth, segments, center, 'Z', mat,
                    smooth=True, radius_top=radius_top)


def torus(name, major_r, minor_r, major_seg=24, minor_seg=10, center=(0, 0, 0),
          mat=None, axis='Z'):
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=1)
    bm.free()
    bm = bmesh.new()
    verts = []
    for i in range(major_seg):
        a = TAU * i / major_seg
        ca, sa = math.cos(a), math.sin(a)
        ring = []
        for j in range(minor_seg):
            b = TAU * j / minor_seg
            r = major_r + minor_r * math.cos(b)
            ring.append(bm.verts.new((ca * r, sa * r, minor_r * math.sin(b))))
        verts.append(ring)
    for i in range(major_seg):
        i2 = (i + 1) % major_seg
        for j in range(minor_seg):
            j2 = (j + 1) % minor_seg
            bm.faces.new((verts[i][j], verts[i2][j], verts[i2][j2], verts[i][j2]))
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    if mat:
        me.materials.append(mat)
    if axis == 'X':
        ob.data.transform(Euler((0, math.radians(90), 0)).to_matrix().to_4x4())
    elif axis == 'Y':
        ob.data.transform(Euler((math.radians(90), 0, 0)).to_matrix().to_4x4())
    ob.data.transform(Matrix.Translation(Vector(center)))
    shade_smooth(ob)
    return ob


# ---------------------------------------------------------------- loft / hull

def signed_volume(verts, faces):
    """
    Signed volume of a closed shell (divergence theorem over triangles).
    Positive = normals point outwards, negative = the model is inside out.
    For a closed surface the value does not depend on the position relative
    to the origin, so it works as a test.
    """
    v6 = 0.0
    for f in faces:
        x0, y0, z0 = verts[f[0]]
        for k in range(1, len(f) - 1):
            x1, y1, z1 = verts[f[k]]
            x2, y2, z2 = verts[f[k + 1]]
            v6 += (x0 * (y1 * z2 - z1 * y2)
                   - y0 * (x1 * z2 - z1 * x2)
                   + z0 * (x1 * y2 - y1 * x2))
    return v6 / 6.0


def _bbox_scale(verts):
    if not verts:
        return 1.0
    xs = [v[0] for v in verts]
    ys = [v[1] for v in verts]
    zs = [v[2] for v in verts]
    d = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
    return max(d, 1e-6)


def loft(name, sections, mat=None, cap_start=True, cap_end=True, smooth=True,
         closed_rings=True, mats=None, face_mat=None):
    """
    sections: a list of rings, each a list of (x,y,z) points of equal length.
    Builds the shell between consecutive rings.

    Face winding is determined automatically. The shell is temporarily capped
    at both ends, the signed volume is computed, and if it comes out negative
    the whole loft is flipped. Callers therefore need not care which way a
    ring runs - and that is exactly the mistake you cannot see in Blender
    (the renderer draws both sides), while RealityKit culls back faces and
    the car turns see-through from the inside.
    """
    verts = []
    n = len(sections[0])
    for ring in sections:
        assert len(ring) == n, "every ring must have the same number of points"
        verts.extend(ring)
    rng = n if closed_rings else n - 1
    nsec = len(sections)
    base_end = (nsec - 1) * n

    def build(flip):
        out = []
        for s in range(nsec - 1):
            a0 = s * n
            a1 = (s + 1) * n
            for i in range(rng):
                j = (i + 1) % n
                out.append((a0 + i, a1 + i, a1 + j, a0 + j) if flip
                           else (a0 + i, a0 + j, a1 + j, a1 + i))
        return out

    # orientation test on a temporarily capped shell (caps are always added)
    if n >= 3 and nsec >= 2:
        probe = build(False)
        probe.append(tuple(reversed(range(n))))
        probe.append(tuple(range(base_end, base_end + n)))
        vol = signed_volume(verts, probe)
        eps = _bbox_scale(verts) ** 3 * 1e-4
        if vol < -eps:
            flip = True
        elif vol > eps:
            flip = False
        else:                       # flat or self-intersecting profile
            flip = False
            print("  ! loft '%s': orientation cannot be determined (volume %.3g), "
                  "the cross-section is probably degenerate" % (name, vol))
    else:
        flip = False

    faces = build(flip)
    fidx = []
    if face_mat is not None:
        for s in range(nsec - 1):
            for i in range(rng):
                fidx.append(face_mat('side', s, i, n))
    if cap_start and n >= 3:
        r0 = range(n)
        faces.append(tuple(r0) if flip else tuple(reversed(r0)))
        if face_mat is not None:
            fidx.append(face_mat('cap0', 0, 0, n))
    if cap_end and n >= 3:
        r1 = range(base_end, base_end + n)
        faces.append(tuple(reversed(r1)) if flip else tuple(r1))
        if face_mat is not None:
            fidx.append(face_mat('cap1', nsec - 1, 0, n))
    ob = mesh_from_pydata(name, verts, faces, mat, smooth=smooth)
    if mats:
        ob.data.materials.clear()
        for m in mats:
            ob.data.materials.append(m)
        if fidx:
            for k, poly in enumerate(ob.data.polygons):
                poly.material_index = fidx[k] if k < len(fidx) else 0
    return ob


def superellipse_ring(cx, cy, cz, half_w, half_h, n=16, power=2.6,
                      flat_bottom=0.0, axis='YZ'):
    """
    A superellipse ring (rounded rectangle) in a plane perpendicular to X or Y.
    axis='YZ' -> the ring lies in the Y-Z plane (its normal runs along X)
    axis='XZ' -> the ring lies in the X-Z plane (its normal runs along Y)
    """
    pts = []
    for i in range(n):
        t = TAU * i / n
        c, s = math.cos(t), math.sin(t)
        u = math.copysign(abs(c) ** (2.0 / power), c) * half_w
        v = math.copysign(abs(s) ** (2.0 / power), s) * half_h
        if flat_bottom > 0 and v < 0:
            v = max(v, -half_h * (1.0 - flat_bottom) - half_h * flat_bottom * 0.0)
        if axis == 'YZ':
            pts.append((cx, cy + u, cz + v))
        else:
            pts.append((cx + u, cy, cz + v))
    return pts


# ---------------------------------------------------------------- transforms

def move(ob, v):
    ob.data.transform(Matrix.Translation(Vector(v)))
    return ob


def scale_mesh(ob, s):
    if isinstance(s, (int, float)):
        s = (s, s, s)
    ob.data.transform(Matrix.Diagonal(Vector(tuple(s) + (1.0,))))
    return ob


def rotate_mesh(ob, angle, axis='Z'):
    idx = {'X': 0, 'Y': 1, 'Z': 2}[axis]
    e = [0, 0, 0]
    e[idx] = angle
    ob.data.transform(Euler(e).to_matrix().to_4x4())
    return ob


def mirror_x(ob, name=None):
    """Creates a copy mirrored across X (a left/right pair)."""
    new_me = ob.data.copy()
    new_me.transform(Matrix.Diagonal(Vector((-1, 1, 1, 1))))
    new_me.flip_normals()
    nob = bpy.data.objects.new(name or (ob.name + "_M"), new_me)
    bpy.context.collection.objects.link(nob)
    return nob


def face_up(ob):
    """
    Flips faces that point downwards. For horizontal strips (road, kerbs,
    trims, starting grids) the correct orientation is unambiguous, and
    watching the winding by hand is a source of bugs: Blender shows both
    sides, but RealityKit culls the back ones and the strip disappears when
    seen from above. Returns the number of flipped faces.
    """
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.normal_update()
    bad = [f for f in bm.faces if f.normal.z < 0.0]
    if bad:
        bmesh.ops.reverse_faces(bm, faces=bad)
        bm.to_mesh(ob.data)
        ob.data.update()
    n = len(bad)
    bm.free()
    return n


# ---------------------------------------------------------------- checks

def check_normals(ob, label=None):
    """
    Walks the closed (manifold) parts of a mesh and checks that the normals
    point outwards. Open surfaces (windows, stripes, the road) are skipped -
    the signed volume says nothing about them. Returns the number of inverted
    parts and prints them.

    Blender draws both sides of a face, so an inside-out model is invisible in
    the preview; RealityKit culls the back faces and you can see through it.
    That is why this check runs on every generation.
    """
    label = label or ob.name
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.faces.ensure_lookup_table()
    seen = set()
    bad = 0
    closed = 0
    for f0 in bm.faces:
        if f0.index in seen:
            continue
        # collect the connected part (flood fill across edges)
        part, stack = [], [f0]
        seen.add(f0.index)
        while stack:
            f = stack.pop()
            part.append(f)
            for e in f.edges:
                for nf in e.link_faces:
                    if nf.index not in seen:
                        seen.add(nf.index)
                        stack.append(nf)
        if any(not e.is_manifold for f in part for e in f.edges):
            continue                      # open surface - cannot decide
        closed += 1
        v6 = 0.0
        for f in part:
            vs = f.verts
            x0, y0, z0 = vs[0].co
            for k in range(1, len(vs) - 1):
                x1, y1, z1 = vs[k].co
                x2, y2, z2 = vs[k + 1].co
                v6 += (x0 * (y1 * z2 - z1 * y2)
                       - y0 * (x1 * z2 - z1 * x2)
                       + z0 * (x1 * y2 - y1 * x2))
        if v6 < 0.0:
            bad += 1
    bm.free()
    if bad:
        print("  ! %s: %d of %d closed parts have inward normals"
              % (label, bad, closed))
    return bad


# ---------------------------------------------------------------- export

def set_origin_to(ob, loc):
    """Shifts the mesh so the given point sits at the object's origin."""
    ob.data.transform(Matrix.Translation(-Vector(loc)))
    ob.location = Vector(loc)
    return ob


def export_usdz(filepath, root='/Root'):
    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    bpy.ops.wm.usd_export(
        filepath=filepath,
        export_materials=True,
        generate_preview_surface=True,
        generate_materialx_network=False,
        convert_orientation=True,
        export_global_forward_selection='NEGATIVE_Z',
        export_global_up_selection='Y',
        root_prim_path=root,
        triangulate_meshes=True,
        export_lights=False,
        export_cameras=False,
        export_animation=False,
        export_uvmaps=True,
        export_normals=True,
        export_mesh_colors=False,
        export_custom_properties=False,
        use_instancing=False,
        evaluation_mode='RENDER',
        check_existing=False,
        convert_world_material=False,
    )
    return filepath


def tri_count():
    n = 0
    dg = bpy.context.evaluated_depsgraph_get()
    for ob in bpy.context.scene.objects:
        if ob.type != 'MESH':
            continue
        me = ob.evaluated_get(dg).to_mesh()
        for p in me.polygons:
            n += len(p.vertices) - 2
        ob.evaluated_get(dg).to_mesh_clear()
    return n
