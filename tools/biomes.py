"""Scenery for the later circuits: trees, landmarks, habitats and wildlife.

build_assets.py calls these for every theme that is not one of the first three.
Primitives are built directly with bmesh, without operator calls or scene
updates, so the larger islands stay quick to author. Each style keeps its
triangle budget in mind: crowns are single icospheres, small props are plain
boxes, and repeated dressing shares materials.
"""
import math
import random
import bmesh
import bpy
from mathutils import Vector, Matrix

SPECIES = {}
STYLE_TREES = {}
STYLE_SCENERY = {}
STYLE_PATCHES = {}
STYLE_BANKS = {}
materials = {}


# --- Fast primitives (game coordinates: x right, y up, z forward) --------------------
def mat(name, color, paint=0):
    if name in materials and materials[name].name in bpy.data.materials:
        return materials[name]
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1); m['paint'] = paint
    materials[name] = m
    return m


def xyz(p):
    return (p[0], -p[2], p[1])


def obj_from_bmesh(name, bm, material, location=None):
    data = bpy.data.meshes.new(name); bm.to_mesh(data); bm.free()
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    if location is not None:
        obj.location = xyz(location)
    data.materials.append(material)
    return obj


def ico(name, p, scale, material, sub=1):
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=sub, radius=1)
    for v in bm.verts:
        v.co.x *= scale[0]; v.co.y *= scale[2]; v.co.z *= scale[1]
    return obj_from_bmesh(name, bm, material, p)


def box(name, p, size, material, yaw=0.):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1)
    for v in bm.verts:
        v.co.x *= size[0]; v.co.y *= size[2]; v.co.z *= size[1]
    if yaw:
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(yaw, 3, 'Z'))
    return obj_from_bmesh(name, bm, material, p)


def cone(name, p, r1, r2, depth, material, vertices=8):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=vertices, radius1=r1, radius2=r2, depth=depth)
    return obj_from_bmesh(name, bm, material, p)


def beam(name, a, b, r, material, vertices=6, r2=None):
    """A tapered cylinder between two points."""
    a, b = Vector(xyz(a)), Vector(xyz(b))
    d = b - a
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=vertices, radius1=r, radius2=r if r2 is None else r2,
                          depth=d.length)
    obj = obj_from_bmesh(name, bm, material)
    obj.location = (a + b) * .5
    obj.rotation_euler = d.to_track_quat('Z', 'Y').to_euler()
    return obj


def mesh(name, vs, fs, material):
    m = bpy.data.meshes.new(name); m.from_pydata([xyz(v) for v in vs], [], fs); m.update()
    o = bpy.data.objects.new(name, m); bpy.context.collection.objects.link(o); o.data.materials.append(material)
    return o


def footprint(objs):
    """XZ bounds of new objects, for keeping later dressing clear of them."""
    bpy.context.view_layer.update()
    out = []
    for o in objs:
        if o.type != 'MESH':
            continue
        corners = [o.matrix_world @ Vector(c) for c in o.bound_box]
        out.append((min(c.x for c in corners), max(c.x for c in corners),
                    min(-c.y for c in corners), max(-c.y for c in corners)))
    return out


# --- Site selection --------------------------------------------------------------------------
def sign_sites(T, path):
    """Direction boards some 40 m before the sharpest bends of the lap."""
    n = len(path); step = path[1][6] - path[0][6]
    k = max(2, int(10 / step))
    scored = []
    for i in range(n):
        a, b = path[(i - k) % n], path[(i + k) % n]
        turn = abs(math.atan2(a[3] * b[4] - a[4] * b[3], a[3] * b[3] + a[4] * b[4]))
        scored.append((turn, i))
    count = max(6, int(round(6 * path[-1][6] / 1000)))
    chosen = []
    for turn, i in sorted(scored, reverse=True):
        if turn < .3 or len(chosen) == count:
            break
        if any(min(abs(i - j), n - abs(i - j)) * step < 90 for j in chosen):
            continue
        chosen.append(i)
    return sorted((i - int(40 / step)) % n for i in chosen)


def crowd_sites(T, path, ramps):
    """Viewing spots: the start, both ends of every jump and the slowest bends."""
    n = len(path); step = path[1][6] - path[0][6]
    sites = [int(8 / step)]
    sites += [(i - int(8 / step)) % n for i, _ in ramps]
    sites += [(i + int(30 / step)) % n for i, _ in ramps]
    sites += [(i + int(40 / step)) % n for i in sign_sites(T, path)]
    sites = sorted(set(sites))
    while len(sites) > T.crowd_groups:
        gaps = [((sites[(j + 1) % len(sites)] - sites[j]) % n, j) for j in range(len(sites))]
        _, j = min(gaps)
        del sites[(j + 1) % len(sites)]
    # Fill long gaps so crowds stand along the whole lap.
    while len(sites) < T.crowd_groups:
        gaps = [((sites[(j + 1) % len(sites)] - sites[j]) % n, j) for j in range(len(sites))]
        gap, j = max(gaps)
        sites.insert(j + 1, (sites[j] + gap // 2) % n)
        sites.sort()
    return sites


def broadcast_sites(T, path):
    n = len(path)
    return [int(n * f) % n for f in (.015, .14, .34, .56, .79)]


# --- Dispatch ------------------------------------------------------------------------------------
def tree(T, x, y, z, s, rng=None):
    """A theme's typical tree, tagged for the vegetation export."""
    before = set(bpy.context.scene.objects)
    STYLE_TREES[T.tree](T, x, y, z, s, rng or random)
    for obj in set(bpy.context.scene.objects) - before:
        obj['vegetation_origin'] = (x, y, z, 7 * s)
        obj['vegetation_motion'] = (obj.get('sway', T.sway), float(bool(obj.get('frond'))), 0., 0.)


def scenery(ctx):
    T = ctx['T']
    rng = random.Random(7310 + T.index)
    ctx['rng'] = rng
    ctx['bounds'] = []
    STYLE_SCENERY[T.scenery](ctx)


def banks(ctx):
    STYLE_BANKS.get(ctx['T'].banks, lambda ctx: None)(ctx)


def patch(T, number, x, y, z, rng, ground, kit):
    STYLE_PATCHES[T.patch](T, number, x, y, z, rng, ground, kit)


def animal(T, name, number, kit):
    return SPECIES[name]['build'](T, number, kit)


def scatter(ctx, count, clearance, place, attempts=None, water_gap=4., inside=(119, 118)):
    """Random free sites off the road, the water, the railway and earlier props."""
    T, rng, nearest, water, rail = ctx['T'], ctx['rng'], ctx['nearest'], ctx['water'], ctx['rail']
    placed = 0
    rx, rz = T.sx(111), T.sz(108)
    for _ in range(attempts or count * 12):
        x, z = rng.uniform(-rx, rx), rng.uniform(-rz, rz)
        if not T.within(x, z, *inside) or nearest(x, z) < clearance:
            continue
        if water(x, z)[0] > -water_gap or not ctx['rail'].clear(x, z, 2.5):
            continue
        if any(a - 1 < x < b + 1 and c - 1 < z < d + 1 for a, b, c, d in ctx['bounds']):
            continue
        place(x, ctx['ground'](x, z), z)
        placed += 1
        if placed >= count:
            break
    return placed


def landmark(ctx, x, z, radius, build):
    """Build a structure on the nearest free site; later dressing keeps clear."""
    x, z = ctx['site'](x, z, radius)
    before = set(bpy.context.scene.objects)
    build(x, ctx['ground'](x, z), z)
    ctx['bounds'].extend(footprint(set(bpy.context.scene.objects) - before))
    return x, z


def boulders(ctx, count, color, name='Boulder', squash=1.):
    rng = ctx['rng']
    m = mat(name + ' ' + ctx['T'].key, color)

    def place(x, y, z):
        s = (rng.uniform(1.0, 2.4), rng.uniform(.8, 2.2) * squash, rng.uniform(1, 2.2))
        ico(name, (x, y + .45 * s[1], z), s, m, 1)
    scatter(ctx, count, 6.5, place)


# --- Canyon Dash: red rock desert ---------------------------------------------------------------
def desert_tree(T, x, y, z, s, rng):
    green = mat('Saguaro green', (.30, .45, .26))
    kind = rng.random()
    if kind < .55:
        # Saguaro: a ribbed column with one or two upturned arms.
        h = 4.2 * s
        beam('Saguaro trunk', (x, y, z), (x, y + h, z), .38 * s, green, 8, .3 * s)
        ico('Saguaro crown', (x, y + h, z), (.3 * s, .26 * s, .3 * s), green, 1)
        for side in ((-1, .25), (1, .55))[:1 + (rng.random() < .7)]:
            a = rng.uniform(0, math.tau)
            ax, az = math.cos(a) * side[0], math.sin(a) * side[0]
            base = (x + ax * .3 * s, y + h * (.35 + side[1] * .3), z + az * .3 * s)
            elbow = (x + ax * 1.0 * s, base[1] + .1 * s, z + az * 1.0 * s)
            beam('Saguaro arm', base, elbow, .2 * s, green, 6)
            beam('Saguaro arm', elbow, (elbow[0], elbow[1] + 1.5 * s, elbow[2]), .2 * s, green, 6, .16 * s)
    elif kind < .8:
        # Joshua tree: forked trunk with spiky tufts.
        bark = mat('Joshua bark', (.42, .33, .24))
        tuft = mat('Joshua leaves', (.43, .52, .26))
        top = (x + .3 * s, y + 2.6 * s, z)
        beam('Joshua trunk', (x, y, z), top, .26 * s, bark, 6, .2 * s)
        for k in range(3):
            a = k * 2.1 + s
            end = (top[0] + math.cos(a) * 1.2 * s, top[1] + .9 * s, top[2] + math.sin(a) * 1.2 * s)
            beam('Joshua branch', top, end, .15 * s, bark, 5)
            cone('Joshua tuft', (end[0], end[1] + .2 * s, end[2]), .55 * s, 0, .9 * s, tuft, 6)
    else:
        # A dry acacia-like shrub tree with a flat crown.
        bark = mat('Desert ironwood', (.38, .30, .23))
        crown = mat('Desert crown', (.52, .55, .30))
        beam('Ironwood trunk', (x, y, z), (x, y + 1.8 * s, z), .2 * s, bark, 5)
        ico('Ironwood crown', (x, y + 2.3 * s, z), (2.0 * s, .7 * s, 1.8 * s), crown, 1)


def canyon_scenery(ctx):
    api, T, rng, ground = ctx['api'], ctx['T'], ctx['rng'], ctx['ground']
    wood = mat('Weathered timber', (.50, .36, .24))
    tin = mat('Galvanised tin', (.62, .64, .63))
    red = mat('Barn red paint', (.62, .19, .12))
    dark = api['dark']; cream = api['cream']

    def water_tower(x, y, z):
        for dx in (-1.6, 1.6):
            for dz in (-1.6, 1.6):
                beam('Water tower leg', (x + dx, y, z + dz), (x + dx * .7, y + 6, z + dz * .7), .16, wood, 5)
        beam('Water tower tank', (x, y + 6, z), (x, y + 9, z), 2.1, wood, 12)
        cone('Water tower roof', (x, y + 9.6, z), 2.4, 0, 1.3, tin, 12)
        beam('Water tower spout', (x + 2, y + 6.6, z), (x + 3.2, y + 5.2, z), .14, tin, 5)

    def windpump(x, y, z):
        for dx, dz in ((-1.1, -1.1), (1.1, -1.1), (0, 1.3)):
            beam('Windpump leg', (x + dx, y, z + dz), (x, y + 8, z), .09, tin, 5)
        box('Windpump head', (x, y + 8.2, z), (.6, .5, 1.2), tin)
        for k in range(12):
            a = k * math.tau / 12
            beam('Windpump blade', (x, y + 8.4, z + .7),
                 (x + math.cos(a) * 1.7, y + 8.4 + math.sin(a) * 1.7, z + .7), .14, tin, 4)
        box('Windpump vane', (x, y + 8.4, z - 1.6), (.06, 1.0, 1.8), red)
        box('Stock tank', (x + 2.4, y + .35, z), (2.2, .7, 1.4), tin)

    def ranch(x, y, z):
        box('Ranch house', (x, y + 2, z), (9, 4, 6), wood)
        mesh('Ranch roof', [(x - 5, y + 4, z - 3.4), (x + 5, y + 4, z - 3.4), (x, y + 6, z - 3.4),
                            (x - 5, y + 4, z + 3.4), (x + 5, y + 4, z + 3.4), (x, y + 6, z + 3.4)],
             [(0, 3, 5, 2), (2, 5, 4, 1), (0, 2, 1), (3, 4, 5)], tin)
        box('Ranch porch', (x, y + .2, z + 4.2), (9, .4, 2.4), wood)
        for dx in (-4.2, -1.4, 1.4, 4.2):
            beam('Porch post', (x + dx, y + .4, z + 5.2), (x + dx, y + 3.9, z + 5.2), .1, wood, 5)
        box('Porch roof', (x, y + 4, z + 4.4), (9.4, .15, 2.8), tin)
        for dx in (-2.5, 2.5):
            box('Ranch window', (x + dx, y + 2.3, z + 3.02), (1.3, 1.1, .08), api['glass'])
        box('Ranch door', (x, y + 1.4, z + 3.02), (1.2, 2.3, .08), red)
        # Corral fence.
        for k in range(10):
            a, b = k * math.tau / 10, (k + 1) * math.tau / 10
            p = (x + 14 + math.cos(a) * 6, z + math.sin(a) * 6)
            q = (x + 14 + math.cos(b) * 6, z + math.sin(b) * 6)
            py, qy = ground(*p), ground(*q)
            beam('Corral post', (p[0], py, p[1]), (p[0], py + 1.4, p[1]), .1, wood, 5)
            for h in (.6, 1.2):
                beam('Corral rail', (p[0], py + h, p[1]), (q[0], qy + h, q[1]), .06, wood, 4)

    def wagon(x, y, z):
        box('Wagon bed', (x, y + 1.1, z), (1.6, .5, 3.4), wood)
        for side in (-1, 1):
            for dz in (-1.2, 1.2):
                beam('Wagon wheel', (x + side * .9, y + .7, z + dz), (x + side * 1.0, y + .7, z + dz), .7, wood, 10)
        for k in range(4):
            a = .2 + k * .9
            beam('Canvas hoop', (x - .8, y + 1.4, z - 1.4 + k * .9), (x + .8, y + 1.4, z - 1.4 + k * .9), .04, wood, 4)
        ico('Wagon canvas', (x, y + 1.7, z), (.85, .7, 1.7), cream, 1)

    rx, rz = T.sx(1), T.sz(1)
    landmark(ctx, -20 * rx, 60 * rz, 9, water_tower)
    landmark(ctx, 70 * rx, -60 * rz, 7, windpump)
    landmark(ctx, -60 * rx, -60 * rz, 16, ranch)
    landmark(ctx, 40 * rx, 85 * rz, 4, wagon)
    landmark(ctx, -95 * rx, 40 * rz, 6, windpump)
    scatter(ctx, T.trees, 5.5, lambda x, y, z: tree(T, x, y - .05, z, rng.uniform(.75, 1.3), rng))
    boulders(ctx, T.boulders, T.colors['boulder'], 'Sandstone boulder', .8)


def desert_patch(T, number, x, y, z, rng, ground, kit):
    ico, beam, mesh = kit['ico'], kit['beam'], kit['mesh']
    if number % 9 == 0:
        # Tumbleweed: an open tangle, low and light.
        ico('Tumbleweed', (x, y + .45, z), (.55, .45, .55), kit['dry'], 1)
    elif number % 3 == 0:
        ico('Desert scrub', (x, y + .35, z), (.9, .5, .8), kit['foliage'], 1)
        ico('Desert scrub', (x + .7, ground(x + .7, z + .3) + .25, z + .3), (.6, .35, .55), kit['foliage'], 1)
    else:
        for j in range(rng.randrange(3, 6)):
            a, b = x + rng.uniform(-1, 1), z + rng.uniform(-1, 1)
            yy = ground(a, b)
            h = rng.uniform(.25, .6)
            for k in range(2):
                angle = k * 2.6 + j
                mesh('Desert grass', [(a - .06, yy, b), (a + .06, yy, b),
                                      (a + math.cos(angle) * .2, yy + h, b + math.sin(angle) * .2)],
                     [(0, 1, 2)], kit['dry'])
            if j == 0 and rng.random() < .5:
                ico('Desert bloom', (a, yy + h, b), (.1, .08, .1), kit['flower'])
        if number % 4 == 1:
            ico('Red rock', (x, y + .2, z), (.6, .35, .5), kit['stone'], 1)


def canyon_banks(ctx):
    """Palms and reeds round the oasis creek, cracked mud where it dries."""
    T, rng, water, ground, nearest = ctx['T'], random.Random(613 + ctx['T'].index), ctx['water'], ctx['ground'], ctx['nearest']
    x0, x1, z0, z1 = ctx['watershed'].bounds
    reed = mat('Oasis reeds', (.42, .48, .20))
    pebble = mat('Wet river stones ' + T.key, T.colors['pebble'])
    palms = 0
    for _ in range(600):
        x = rng.uniform(x0 - 4, x1 + 4); z = rng.uniform(z0 - 4, z1 + 4)
        wet, level, _ = water(x, z)
        if not -5 < wet < .5 or nearest(x, z) < 4 or not T.within(x, z, 124, 122) or not ctx['rail'].clear(x, z, 1):
            continue
        y = ground(x, z)
        if wet < -2.5 and palms < 14 and rng.random() < .3:
            before = set(bpy.context.scene.objects)
            palm(x, y - .05, z, rng.uniform(.7, 1.0))
            for obj in set(bpy.context.scene.objects) - before:
                obj['vegetation_origin'] = (x, y - .05, z, 7 * .85)
                obj['vegetation_motion'] = (1.0, float(bool(obj.get('frond'))), 0., 0.)
            palms += 1
        elif wet > -1.8:
            size = rng.uniform(.2, .5)
            ico('Riverbank pebble', (x, y + size * .2, z), (size, size * .45, size * .7), pebble, 1)
            if rng.random() < .4:
                for blade in range(rng.randrange(3, 7)):
                    rx = x + rng.uniform(-.3, .3); rz = z + rng.uniform(-.3, .3)
                    ry = ground(rx, rz)
                    beam('Waterside reed', (rx, ry, rz), (rx + .1, ry + .5 + rng.random() * .5, rz + .08), .03, reed, 4)


def palm(x, y, z, s):
    trunk = mat('Palm trunk wood', (.53, .40, .26))
    frond = mat('Palm green', (.15, .47, .29))
    top = (x + s * .7, y + s * 7, z)
    beam('Palm trunk', (x, y, z), top, s * .25, trunk, 6, s * .18)
    for k in range(7):
        a = k * math.tau / 7
        end = (top[0] + math.cos(a) * s * 4.3, top[1] - .9 * s, top[2] + math.sin(a) * s * 4.3)
        mid = (top[0] + math.cos(a) * s * 2, top[1] + s * .6, top[2] + math.sin(a) * s * 2)
        perp = (-math.sin(a) * s * .7, 0, math.cos(a) * s * .7)
        o = mesh('Frond', [top, (mid[0] + perp[0], mid[1], mid[2] + perp[2]), end,
                           (mid[0] - perp[0], mid[1], mid[2] - perp[2]), mid],
                 [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)], frond)
        o['frond'] = True


STYLE_TREES['desert'] = desert_tree
STYLE_SCENERY['canyon'] = canyon_scenery
STYLE_PATCHES['desert'] = desert_patch
STYLE_BANKS['canyon'] = canyon_banks


# --- Wildlife -----------------------------------------------------------------------------------
def quadruped(kit, number, size, coat, tail, ears=(.13, .26, .09), snout=None, horns=None):
    """A four-legged animal scaled from the deer proportions; the head nods."""
    ell, rod, black = kit['ell'], kit['rod'], kit['black']
    k = size
    ell('Body', (0, 1.18 * k, 0), (.40 * k, .50 * k, .83 * k), coat)
    rod('Neck', (0, 1.28 * k, .5 * k), (0, 1.75 * k, .82 * k), .2 * k, coat, 3)
    ell('Head', (0, 1.8 * k, 1.0 * k), (.24 * k, .24 * k, .36 * k), coat, 3)
    ell('Muzzle', (0, 1.7 * k, 1.28 * k), (.15 * k, .12 * k, .18 * k), snout or black, 3)
    for side in (-1, 1):
        ell('Ear', (side * .2 * k, 2.02 * k, .88 * k), tuple(v * k for v in ears), coat, 3)
        ell('Eye', (side * .19 * k, 1.86 * k, 1.12 * k), (.035 * k, .04 * k, .035 * k), black, 3)
        for front in (-1, 1):
            part = 1 if side * front > 0 else 2
            rod('Leg', (side * .25 * k, .99 * k, front * .55 * k), (side * .27 * k, .08 * k, front * .57 * k), .075 * k, coat, part)
            ell('Hoof', (side * .27 * k, .09 * k, front * .60 * k), (.09 * k, .09 * k, .14 * k), black, part)
        if horns:
            horns(side, k)
    if tail:
        tail(k)
    return k - 1


def coyote(T, number, kit):
    coat = mat('Coyote tawny coat', (.62, .47, .30))
    pale = mat('Coyote cream', (.83, .76, .62))
    return quadruped(kit, number, .58, coat, lambda k: kit['ell']('Bushy tail', (0, .95 * k, -1.05 * k),
                     (.16 * k, .2 * k, .5 * k), coat), ears=(.09, .24, .06), snout=pale)


def jackrabbit(T, number, kit):
    ell = kit['ell']
    coat = mat('Jackrabbit coat', (.60, .50, .36))
    ell('Hare body', (0, .36, 0), (.27, .33, .47), coat)
    ell('Hare head', (0, .64, .38), (.2, .22, .23), coat, 3)
    for side in (-1, 1):
        ell('Long ear', (side * .12, 1.12, .32), (.08, .42, .06), coat, 3)
        ell('Hare eye', (side * .17, .71, .5), (.032, .035, .032), kit['black'], 3)
        ell('Hare hind paw', (side * .22, .1, -.2), (.14, .1, .3), coat, 1)
        ell('Hare forepaw', (side * .16, .08, .34), (.07, .08, .17), coat, 2)
    ell('Hare tail', (0, .4, -.46), (.12, .12, .12), kit['pale'])
    return 0.


def lizard(T, number, kit):
    ell = kit['ell']
    scales = mat('Lizard scales', (.55, .42, .25))
    for j in range(9):
        zz = -.5 + j * .1
        r = .07 if j > 2 else .045
        ell('Lizard body', (math.sin(j * .7) * .06, .05, zz), (r, .045, .08), scales)
    ell('Lizard head', (math.sin(9 * .7) * .06, .06, .44), (.075, .05, .1), scales, 3)
    for side in (-1, 1):
        for dz in (-.15, .2):
            ell('Lizard foot', (side * .1, .02, dz), (.05, .02, .03), scales)
    return 0.


def bird(T, number, kit, plumage, size=1., beak=None):
    ell, mesh, x, y, z = kit['ell'], kit['mesh'], kit['x'], kit['y'], kit['z']
    k = size
    ell('Bird body', (0, 0, 0), (.16 * k, .17 * k, .43 * k), plumage)
    ell('Bird head', (0, .09 * k, .35 * k), (.13 * k, .14 * k, .16 * k), plumage)
    ell('Bird beak', (0, .07 * k, .52 * k), (.055 * k, .04 * k, .11 * k), beak or kit['orange'])
    wing = kit['wing']
    for side in (-1, 1):
        obj = mesh('Bird articulated wing', [(x + side * .12 * k, y, z + .16 * k), (x + side * .68 * k, y + .08 * k, z + .11 * k),
                                             (x + side * 1.13 * k, y - .04 * k, z - .26 * k), (x + side * .52 * k, y, z - .24 * k),
                                             (x + side * .13 * k, y, z - .2 * k)], [(0, 1, 3), (1, 2, 3), (0, 3, 4)], wing)
        obj['wildlife_part'] = 1 if side < 0 else 2
    ell('Bird tail', (0, 0, -.43 * k), (.19 * k, .05 * k, .22 * k), wing)
    return 0.


def vulture(T, number, kit):
    return bird(T, number, kit, mat('Vulture plumage', (.18, .15, .14)), 1.5, mat('Vulture beak', (.75, .62, .45)))


SPECIES.update({
    'coyote': dict(kind=0, build=coyote),
    'jackrabbit': dict(kind=1, build=jackrabbit),
    'lizard': dict(kind=3, build=lizard),
    'vulture': dict(kind=4, build=vulture),
})


# --- Shared builders ------------------------------------------------------------------------------
GLOW = (.98, .80, .40)  # Lanterns, torches and lit windows; the renderer lights them at night


def gable(name, x, y, z, w, d, h, rise, wall, roof, yaw=0., overhang=.4):
    """A simple house: box walls and a pitched roof along its depth."""
    box(name + ' walls', (x, y + h / 2, z), (w, h, d), wall, yaw)
    c, s_ = math.cos(yaw), math.sin(yaw)
    def P(a, hh, b):
        return (x + c * a + s_ * b, y + hh, z - s_ * a + c * b)
    hw, hd = w / 2 + overhang, d / 2 + overhang
    vs = [P(-hw, h, -hd), P(hw, h, -hd), P(0, h + rise, -hd), P(-hw, h, hd), P(hw, h, hd), P(0, h + rise, hd)]
    mesh(name + ' roof', vs, [(0, 3, 5, 2), (2, 5, 4, 1), (0, 2, 1), (3, 4, 5)], roof)


def window_row(x, y, z, yaw, count, spacing, size, material, depth):
    c, s_ = math.cos(yaw), math.sin(yaw)
    for k in range(count):
        a = (k - (count - 1) / 2) * spacing
        box('Window', (x + c * a + s_ * depth, y, z - s_ * a + c * depth), (size[0], size[1], .08), material, yaw)


def crowned_tree(x, y, z, s, trunk, crowns, height=3.2, rng=random, crown_sub=1, lean=0.):
    """A trunk and a cluster of crowns; returns the top of the trunk."""
    top = (x + lean * s, y + height * s, z)
    beam('Tree trunk', (x, y, z), top, .22 * s, trunk, 6, .15 * s)
    for k, (dx, dy, dz, r, m) in enumerate(crowns):
        ico('Tree crown', (top[0] + dx * s, top[1] + dy * s, top[2] + dz * s), (r * s, r * .9 * s, r * s), m, crown_sub)
    return top


# --- Maple Woods: autumn forest -------------------------------------------------------------------------
AUTUMN_LEAVES = [(.80, .22, .07), (.92, .45, .09), (.95, .70, .16), (.68, .16, .10), (.55, .52, .17)]


def autumn_tree(T, x, y, z, s, rng):
    trunk = mat('Maple bark', (.36, .26, .19))
    kind = rng.random()
    if kind < .5:
        leaves = [mat('Maple leaves ' + str(i), c) for i, c in enumerate(AUTUMN_LEAVES)]
        m = leaves[rng.randrange(len(leaves))]
        m2 = leaves[rng.randrange(len(leaves))]
        crowned_tree(x, y, z, s, trunk, [(0, 1.2, 0, 2.3, m), (1.2, .6, .5, 1.6, m2), (-1.0, .9, -.6, 1.5, m)], 3.0, rng)
    elif kind < .72:
        # Birch: a white trunk with dark flecks and a light golden crown.
        bark = mat('Birch bark', (.88, .86, .80))
        crown = mat('Birch gold', (.93, .78, .26))
        top = (x, y + 4.4 * s, z)
        beam('Birch trunk', (x, y, z), top, .15 * s, bark, 6, .1 * s)
        for k in range(3):
            box('Birch fleck', (x, y + (1 + k * 1.1) * s, z + .14 * s), (.1 * s, .06 * s, .04 * s), mat('Birch fleck', (.12, .12, .12)))
        ico('Birch crown', (x, top[1] + .3 * s, z), (1.5 * s, 2.0 * s, 1.5 * s), crown, 1)
    else:
        # Spruce: tiers of dark needles.
        needles = mat('Spruce needles', (.13, .27, .19))
        beam('Spruce trunk', (x, y, z), (x, y + 1.6 * s, z), .2 * s, trunk, 5)
        for k in range(3):
            cone('Spruce tier', (x, y + (2.2 + k * 1.3) * s, z), (1.9 - k * .45) * s, 0, 2.6 * s, needles, 7)


def autumn_scenery(ctx):
    api, T, rng, ground = ctx['api'], ctx['T'], ctx['rng'], ctx['ground']
    logs = mat('Log cabin wood', (.48, .31, .19))
    roof = mat('Cabin shingle roof', (.24, .20, .19))
    stone = mat('Field stone', (.52, .50, .46))
    glow = mat('Lantern glow', GLOW)
    pumpkin = mat('Pumpkin orange', (.93, .45, .08))
    stem = mat('Pumpkin stem', (.30, .36, .14))
    straw = mat('Hay straw', (.80, .62, .29))
    red = mat('Barn vermilion', (.72, .22, .13))
    wood = mat('Weathered timber', (.50, .36, .24))
    cream = api['cream']

    def cabin(x, y, z):
        yaw = rng.uniform(-.4, .4)
        gable('Log cabin', x, y, z, 7, 5.5, 3.2, 2.2, logs, roof, yaw)
        c, s_ = math.cos(yaw), math.sin(yaw)
        box('Cabin chimney', (x + c * 2.6, y + 4.6, z - s_ * 2.6), (.9, 3.4, .9), stone, yaw)
        window_row(x, y + 1.8, z, yaw, 2, 3.2, (1.1, 1.0), glow, 2.8)
        box('Cabin door', (x + s_ * 2.78, y + 1.1, z + c * 2.78), (1.0, 2.1, .08), red, yaw)
        for k in range(5):
            beam('Woodpile log', (x - c * 4.6 + s_ * (k * .3 - .6), y + .25 + (k % 2) * .4, z + s_ * 4.6 + c * (k * .3 - .6)),
                 (x - c * 4.6 + s_ * (k * .3 - .6) + s_ * 0, y + .25 + (k % 2) * .4, z + s_ * 4.6 + c * (k * .3 - .6) + 1.6), .2, wood, 6)

    def sawmill(x, y, z):
        box('Sawmill shed', (x, y + 2.5, z), (12, 5, 7), wood)
        mesh('Sawmill roof', [(x - 6.5, y + 5, z - 4), (x + 6.5, y + 5, z - 4), (x + 6.5, y + 6.2, z + 4), (x - 6.5, y + 6.2, z + 4)],
             [(0, 3, 2, 1)], roof)
        for row in range(3):
            for k in range(4 - row):
                beam('Log pile', (x - 4 + k * .9 + row * .45, y + .45 + row * .8, z + 6),
                     (x - 4 + k * .9 + row * .45, y + .45 + row * .8, z + 12), .42, logs, 8)
        beam('Saw blade', (x + 6.2, y + 1.5, z), (x + 6.4, y + 1.5, z), 1.2, api['chrome'], 14)

    def lookout(x, y, z):
        for dx in (-1.6, 1.6):
            for dz in (-1.6, 1.6):
                beam('Lookout leg', (x + dx * 1.4, y, z + dz * 1.4), (x + dx, y + 11, z + dz), .16, wood, 5)
        for h in (3.5, 7):
            for a, b in (((-1, -1), (1, -1)), ((1, -1), (1, 1)), ((1, 1), (-1, 1)), ((-1, 1), (-1, -1))):
                k = 1.6 * (1 + .4 * (1 - h / 11))
                beam('Lookout brace', (x + a[0] * k, y + h, z + a[1] * k), (x + b[0] * k, y + h, z + b[1] * k), .08, wood, 4)
        box('Lookout cabin', (x, y + 12.2, z), (4, 2.4, 4), wood)
        window_row(x, y + 12.4, z, 0, 2, 1.6, (1.2, .9), glow, 2.02)
        mesh('Lookout roof', [(x - 2.6, y + 13.4, z - 2.6), (x + 2.6, y + 13.4, z - 2.6), (x + 2.6, y + 13.4, z + 2.6),
                              (x - 2.6, y + 13.4, z + 2.6), (x, y + 15.2, z)],
             [(0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)], roof)

    def pumpkins(x, y, z):
        for k in range(18):
            a, r = rng.uniform(0, math.tau), rng.uniform(0, 5)
            px, pz = x + math.cos(a) * r, z + math.sin(a) * r
            py = ground(px, pz); size = rng.uniform(.35, .7)
            ico('Pumpkin', (px, py + size * .7, pz), (size, size * .75, size), pumpkin, 1)
            beam('Pumpkin stem', (px, py + size * 1.35, pz), (px, py + size * 1.6, pz), .06, stem, 4)
        for k in range(3):
            a = k * 2.1
            px, pz = x + math.cos(a) * 7, z + math.sin(a) * 7
            cone('Hay bale', (px, ground(px, pz) + .9, pz), 1.1, 1.1, 1.7, straw, 10)
        # A scarecrow keeps watch.
        beam('Scarecrow post', (x, y, z), (x, y + 2.6, z), .07, wood, 4)
        beam('Scarecrow arms', (x - 1, y + 2, z), (x + 1, y + 2, z), .06, wood, 4)
        box('Scarecrow shirt', (x, y + 1.8, z), (.7, .8, .3), red)
        ico('Scarecrow head', (x, y + 2.5, z), (.25, .3, .25), straw, 1)
        cone('Scarecrow hat', (x, y + 2.85, z), .45, .2, .3, wood, 8)

    rx, rz = T.sx(1), T.sz(1)
    landmark(ctx, 85 * rx, 5 * rz, 7, cabin)
    landmark(ctx, -80 * rx, 45 * rz, 7, cabin)
    landmark(ctx, -10 * rx, 70 * rz, 12, sawmill)
    landmark(ctx, 40 * rx, -80 * rz, 5, lookout)
    landmark(ctx, -100 * rx, -40 * rz, 8, pumpkins)
    landmark(ctx, 20 * rx, 20 * rz, 6, pumpkins)
    scatter(ctx, T.trees, 4.6, lambda x, y, z: tree(T, x, y - .05, z, rng.uniform(.75, 1.35), rng))
    boulders(ctx, T.boulders, T.colors['boulder'], 'Mossy boulder', .7)


def autumn_patch(T, number, x, y, z, rng, ground, kit):
    ico, beam, mesh = kit['ico'], kit['beam'], kit['mesh']
    leaves = [mat('Leaf litter ' + str(i), c) for i, c in enumerate(AUTUMN_LEAVES)]
    if number % 7 == 0:
        end = (x + 2.6, ground(x + 2.6, z + .6) + .25, z + .6)
        beam('Fallen log', (x, y + .25, z), end, .26, kit['bark'], 7)
        ico('Shelf fungus', (x + 1.2, y + .45, z + .5), (.2, .06, .15), mat('Fungus cream', (.85, .78, .62)), 1)
    elif number % 3 == 0:
        # Fly agarics: red caps with pale stems.
        cap = mat('Toadstool red', (.82, .12, .08)); stalk = mat('Toadstool stalk', (.90, .88, .80))
        for k in range(rng.randrange(2, 5)):
            a, b = x + rng.uniform(-.6, .6), z + rng.uniform(-.6, .6)
            yy = ground(a, b); h = rng.uniform(.12, .25)
            beam('Toadstool stalk', (a, yy, b), (a, yy + h, b), .04, stalk, 5)
            ico('Toadstool cap', (a, yy + h, b), (.12, .06, .12), cap, 1)
    else:
        for j in range(rng.randrange(3, 7)):
            a, b = x + rng.uniform(-1.2, 1.2), z + rng.uniform(-1.2, 1.2)
            yy = ground(a, b)
            ico('Leaf pile', (a, yy + .05, b), (rng.uniform(.3, .6), .07, rng.uniform(.3, .6)), leaves[rng.randrange(5)], 1)
        if number % 4 == 1:
            for k in range(4):
                a = k * 1.6
                mesh('Fern frond', [(x, y, z), (x + math.cos(a) * .2, y + .5, z + math.sin(a) * .2),
                                    (x + math.cos(a) * .8, y + .2, z + math.sin(a) * .8)], [(0, 1, 2)], kit['foliage'])


def autumn_banks(ctx):
    T, water, ground, nearest = ctx['T'], ctx['water'], ctx['ground'], ctx['nearest']
    rng = random.Random(613 + T.index)
    x0, x1, z0, z1 = ctx['watershed'].bounds
    reed = mat('Reed green', (.35, .40, .16)); cattail = mat('Cattail brown', (.36, .22, .12))
    pebble = mat('Wet river stones ' + T.key, T.colors['pebble'])
    for _ in range(700):
        x = rng.uniform(x0 - 2, x1 + 2); z = rng.uniform(z0 - 2, z1 + 2)
        wet, level, _ = water(x, z)
        if not -1.8 < wet < .55 or nearest(x, z) < 2.8 or not T.within(x, z, 124, 122) or not ctx['rail'].clear(x, z, 1):
            continue
        y = ground(x, z); size = rng.uniform(.22, .55)
        ico('Riverbank pebble', (x, y + size * .2, z), (size, size * .45, size * .72), pebble, 1)
        if wet < 0 and rng.random() < .35:
            for blade in range(rng.randrange(4, 8)):
                rx = x + rng.uniform(-.35, .35); rz = z + rng.uniform(-.35, .35); ry = ground(rx, rz)
                h = .6 + rng.random() * .6
                beam('Waterside reed', (rx, ry, rz), (rx + .1, ry + h, rz + .08), .035, reed, 4)
                if blade == 0:
                    beam('Cattail', (rx + .1, ry + h, rz + .08), (rx + .11, ry + h + .25, rz + .09), .06, cattail, 5)


def deer(T, number, kit):
    coat = mat('Deer russet fur', (.48, .29, .15))
    def antlers(side, k):
        if number % 3 == 0:
            kit['rod']('Stag antler', (side * .13 * k, 2.0 * k, .85 * k), (side * .44 * k, 2.8 * k, .66 * k), .045 * k, kit['dry'], 3)
    return quadruped(kit, number, 1., coat, lambda k: kit['ell']('Deer tail', (0, 1.35, -.81), (.14, .18, .26), kit['pale']),
                     horns=antlers)


def fox(T, number, kit):
    coat = mat('Fox red coat', (.80, .36, .10))
    white = mat('Fox white', (.92, .90, .84))
    return quadruped(kit, number, .46, coat, lambda k: kit['ell']('Fox brush', (0, .9 * k, -1.15 * k), (.2 * k, .22 * k, .6 * k), coat),
                     ears=(.09, .22, .05), snout=white)


def squirrel(T, number, kit):
    ell = kit['ell']
    coat = mat('Squirrel red', (.62, .30, .14))
    ell('Squirrel body', (0, .2, 0), (.12, .16, .2), coat)
    ell('Squirrel head', (0, .34, .2), (.09, .09, .1), coat, 3)
    for side in (-1, 1):
        ell('Squirrel ear', (side * .05, .45, .18), (.025, .05, .02), coat, 3)
        ell('Squirrel paw', (side * .08, .04, -.08), (.05, .04, .1), coat, 1)
        ell('Squirrel paw', (side * .06, .04, .14), (.03, .04, .06), coat, 2)
    ell('Squirrel tail', (0, .4, -.24), (.1, .26, .1), coat)
    return 0.


def duck(T, number, kit):
    ell, rod = kit['ell'], kit['rod']
    body = mat('Duck brown', (.52, .42, .32)); head = mat('Mallard green', (.10, .36, .20))
    ell('Duck body', (0, .3, 0), (.2, .19, .32), body)
    rod('Duck neck', (0, .34, .2), (0, .55, .26), .07, head, 3)
    ell('Duck head', (0, .58, .28), (.1, .1, .12), head, 3)
    ell('Duck bill', (0, .55, .42), (.06, .03, .08), kit['orange'], 3)
    for side in (-1, 1):
        rod('Duck leg', (side * .08, .16, 0), (side * .08, .02, .02), .025, kit['orange'], 1 if side < 0 else 2)
    return 0.


def crow(T, number, kit):
    return bird(T, number, kit, mat('Crow black', (.08, .08, .09)), 1.0, mat('Crow beak', (.15, .15, .15)))


STYLE_TREES['autumn'] = autumn_tree
STYLE_SCENERY['autumn'] = autumn_scenery
STYLE_PATCHES['autumn'] = autumn_patch
STYLE_BANKS['autumn'] = autumn_banks
SPECIES.update({
    'deer': dict(kind=0, build=deer),
    'fox': dict(kind=0, build=fox),
    'squirrel': dict(kind=1, build=squirrel),
    'duck': dict(kind=2, build=duck, water=True),
    'crow': dict(kind=4, build=crow),
})


# --- Jungle Ruins: rainforest round a stepped temple ---------------------------------------------------
def jungle_tree(T, x, y, z, s, rng):
    trunk = mat('Rainforest bark', (.38, .31, .24))
    kind = rng.random()
    if kind < .35:
        # Emergent kapok: a tall straight trunk, buttress roots and a flat crown.
        top = (x, y + 9 * s, z)
        beam('Kapok trunk', (x, y, z), top, .45 * s, trunk, 7, .3 * s)
        for k in range(4):
            a = k * math.pi / 2 + .4
            mesh('Buttress root', [(x, y + 2.2 * s, z), (x + math.cos(a) * 1.6 * s, y, z + math.sin(a) * 1.6 * s),
                                   (x + math.cos(a) * .3 * s, y, z + math.sin(a) * .3 * s)], [(0, 1, 2), (0, 2, 1)], trunk)
        canopy = mat('Canopy green', (.16, .38, .15))
        ico('Kapok crown', (x, top[1] + .6 * s, z), (4.2 * s, 1.5 * s, 4.0 * s), canopy, 1)
        ico('Kapok crown', (x + 1.6 * s, top[1] + 1.4 * s, z - 1 * s), (2.6 * s, 1.1 * s, 2.4 * s), mat('Canopy light', (.24, .48, .18)), 1)
    elif kind < .6:
        palm(x, y, z, s * rng.uniform(.8, 1.1))
    elif kind < .82:
        # Banana plant: a short stem with broad paddle leaves.
        stem = mat('Banana stem', (.36, .50, .22)); leaf = mat('Banana leaf', (.28, .60, .22))
        top = (x, y + 2.4 * s, z)
        beam('Banana stem', (x, y, z), top, .2 * s, stem, 6)
        for k in range(6):
            a = k * math.tau / 6 + rng.random()
            tip = (x + math.cos(a) * 2.2 * s, top[1] + .4 * s, z + math.sin(a) * 2.2 * s)
            side = (-math.sin(a) * .45 * s, 0, math.cos(a) * .45 * s)
            mid = ((top[0] + tip[0]) / 2, top[1] + .7 * s, (top[2] + tip[2]) / 2)
            o = mesh('Frond', [top, (mid[0] + side[0], mid[1], mid[2] + side[2]), tip, (mid[0] - side[0], mid[1], mid[2] - side[2])],
                     [(0, 1, 2), (0, 2, 3)], leaf)
            o['frond'] = True
    else:
        # Tree fern.
        stem = mat('Tree fern stem', (.26, .22, .16)); frond = mat('Tree fern', (.22, .46, .17))
        top = (x, y + 3.2 * s, z)
        beam('Tree fern trunk', (x, y, z), top, .18 * s, stem, 6)
        for k in range(8):
            a = k * math.tau / 8
            o = mesh('Frond', [top, (x + math.cos(a + .25) * 1.2 * s, top[1] + .5 * s, z + math.sin(a + .25) * 1.2 * s),
                               (x + math.cos(a) * 2.4 * s, top[1] - .6 * s, z + math.sin(a) * 2.4 * s),
                               (x + math.cos(a - .25) * 1.2 * s, top[1] + .5 * s, z + math.sin(a - .25) * 1.2 * s)],
                     [(0, 1, 2), (0, 2, 3)], frond)
            o['frond'] = True


def jungle_scenery(ctx):
    api, T, rng, ground = ctx['api'], ctx['T'], ctx['rng'], ctx['ground']
    stone = mat('Temple limestone', (.62, .60, .50))
    moss = mat('Temple moss', (.30, .44, .20))
    dark = mat('Temple shadow stone', (.36, .35, .30))
    glow = mat('Lantern glow', GLOW)
    jade = mat('Jade carving', (.26, .55, .44))

    def pyramid(x, y, z):
        tiers = 6
        for k in range(tiers):
            w = 30 - k * 4.2
            box('Temple tier', (x, y + 1.3 + k * 2.6, z), (w, 2.6, w), stone if k % 2 == 0 else moss)
        top = y + tiers * 2.6
        box('Temple shrine', (x, top + 2, z), (7, 4, 7), stone)
        box('Shrine door', (x, top + 1.6, z + 3.55), (2, 3, .1), dark)
        box('Shrine roof comb', (x, top + 5, z), (5, 2, 1.2), moss)
        # A stairway up each face, with a flame at the shrine.
        for yaw in (0, math.pi / 2, math.pi, -math.pi / 2):
            c, s_ = math.cos(yaw), math.sin(yaw)
            for k in range(tiers):
                w = 30 - k * 4.2
                d = w / 2 + .9
                box('Temple stair', (x + s_ * d, y + .9 + k * 2.6, z + c * d), (5, 1.8, 1.8), stone, yaw)
        for side in (-1, 1):
            beam('Brazier', (x + side * 2.6, top, z + 4.2), (x + side * 2.6, top + 1.2, z + 4.2), .25, dark, 6)
            ico('Brazier flame', (x + side * 2.6, top + 1.5, z + 4.2), (.35, .5, .35), glow, 1)

    def head(x, y, z):
        yaw = rng.uniform(-1, 1)
        ico('Stone head', (x, y + 2.2, z), (1.8, 2.3, 1.7), stone, 2)
        box('Stone head helmet', (x, y + 4.1, z), (3.4, .7, 3.2), dark, yaw)
        c, s_ = math.cos(yaw), math.sin(yaw)
        for side in (-1, 1):
            box('Stone eye', (x + c * side * .7 + s_ * 1.6, y + 2.6, z - s_ * side * .7 + c * 1.6), (.5, .3, .2), dark, yaw)
        box('Stone mouth', (x + s_ * 1.6, y + 1.4, z + c * 1.6), (1.2, .3, .2), dark, yaw)

    def colonnade(x, y, z):
        yaw = rng.uniform(0, math.pi)
        c, s_ = math.cos(yaw), math.sin(yaw)
        for k in range(6):
            a = (k - 2.5) * 3.2
            h = rng.uniform(2.5, 5.5) if k % 3 else 5.5
            px, pz = x + c * a, z - s_ * a
            py = ground(px, pz)
            beam('Ruined column', (px, py, pz), (px, py + h, pz), .55, stone, 8)
            if k % 3 == 0 and k < 5:
                box('Column lintel', (px + c * 1.6, py + 5.8, pz - s_ * 1.6), (4.2, .7, 1.2), stone, yaw)
            if rng.random() < .5:
                ico('Fallen drum', (px + s_ * 1.5, ground(px + s_ * 1.5, pz + c * 1.5) + .4, pz + c * 1.5), (.55, .4, .55), moss, 1)
        for k in range(4):
            px, pz = x + c * (k - 1.5) * 4.5 + s_ * 4, z - s_ * (k - 1.5) * 4.5 + c * 4
            py = ground(px, pz)
            beam('Torch pole', (px, py, pz), (px, py + 2.2, pz), .08, dark, 5)
            ico('Torch flame', (px, py + 2.45, pz), (.2, .3, .2), glow, 1)

    def idol(x, y, z):
        box('Idol plinth', (x, y + .6, z), (2.4, 1.2, 2.4), stone)
        box('Jade idol', (x, y + 2.1, z), (1.1, 1.8, .8), jade)
        ico('Idol head', (x, y + 3.3, z), (.6, .6, .55), jade, 1)

    rx, rz = T.sx(1), T.sz(1)
    landmark(ctx, 0, 0, 24, pyramid)
    landmark(ctx, -40 * rx, -30 * rz, 4, head)
    landmark(ctx, 50 * rx, 35 * rz, 4, head)
    landmark(ctx, 40 * rx, -30 * rz, 10, colonnade)
    landmark(ctx, -45 * rx, 25 * rz, 10, colonnade)
    landmark(ctx, -100 * rx, 80 * rz, 10, colonnade)
    landmark(ctx, 110 * rx, -80 * rz, 3, idol)
    landmark(ctx, -100 * rx, -100 * rz, 3, idol)
    scatter(ctx, T.trees, 4.6, lambda x, y, z: tree(T, x, y - .05, z, rng.uniform(.75, 1.3), rng))
    boulders(ctx, T.boulders, T.colors['boulder'], 'Mossy boulder', .8)


def jungle_patch(T, number, x, y, z, rng, ground, kit):
    ico, beam, mesh = kit['ico'], kit['beam'], kit['mesh']
    if number % 6 == 0:
        # Elephant ears: big heart-shaped leaves near the ground.
        leaf = mat('Elephant ear', (.20, .50, .20))
        for k in range(4):
            a = k * 1.57 + rng.random()
            tip = (x + math.cos(a) * 1.3, y + .9, z + math.sin(a) * 1.3)
            side = (-math.sin(a) * .5, 0, math.cos(a) * .5)
            mesh('Elephant ear leaf', [(x, y + .2, z), (x + math.cos(a) * .6 + side[0], y + 1.1, z + math.sin(a) * .6 + side[2]),
                                       tip, (x + math.cos(a) * .6 - side[0], y + 1.1, z + math.sin(a) * .6 - side[2])],
                 [(0, 1, 2), (0, 2, 3)], leaf)
    elif number % 4 == 0:
        bloom = [mat('Heliconia red', (.88, .18, .10)), mat('Orchid pink', (.86, .40, .70)), mat('Hibiscus yellow', (.96, .78, .14))]
        ico('Jungle shrub', (x, y + .5, z), (1.0, .7, .9), kit['foliage'], 1)
        for k in range(3):
            ico('Jungle flower', (x + rng.uniform(-.7, .7), y + .9 + rng.random() * .3, z + rng.uniform(-.7, .7)),
                (.13, .1, .13), bloom[rng.randrange(3)])
    else:
        for k in range(rng.randrange(4, 8)):
            a = k * 1.3 + rng.random()
            r = rng.uniform(.6, 1.1)
            mesh('Fern frond', [(x, y, z), (x + math.cos(a + .2) * r * .5, y + .55, z + math.sin(a + .2) * r * .5),
                                (x + math.cos(a) * r, y + .2, z + math.sin(a) * r)], [(0, 1, 2)], kit['foliage'])
        if number % 5 == 1:
            ico('Mossy stone', (x + .8, y + .2, z), (.5, .3, .45), kit['stone'], 1)


def jungle_banks(ctx):
    T, water, ground, nearest = ctx['T'], ctx['water'], ctx['ground'], ctx['nearest']
    rng = random.Random(613 + T.index)
    x0, x1, z0, z1 = ctx['watershed'].bounds
    reed = mat('River grass', (.28, .48, .18)); pebble = mat('Wet river stones ' + T.key, T.colors['pebble'])
    for _ in range(800):
        x = rng.uniform(x0 - 2, x1 + 2); z = rng.uniform(z0 - 2, z1 + 2)
        wet, level, _ = water(x, z)
        if not -2.2 < wet < .5 or nearest(x, z) < 3 or not T.within(x, z, 124, 122) or not ctx['rail'].clear(x, z, 1):
            continue
        y = ground(x, z)
        if rng.random() < .5:
            size = rng.uniform(.25, .6)
            ico('Riverbank pebble', (x, y + size * .2, z), (size, size * .45, size * .7), pebble, 1)
        else:
            for blade in range(rng.randrange(4, 8)):
                rx = x + rng.uniform(-.4, .4); rz = z + rng.uniform(-.4, .4); ry = ground(rx, rz)
                beam('Waterside reed', (rx, ry, rz), (rx + .12, ry + .7 + rng.random() * .6, rz + .1), .035, reed, 4)


def parrot(T, number, kit):
    colors = [(.86, .14, .10), (.12, .40, .80), (.20, .62, .25), (.95, .78, .12)]
    plumage = mat('Parrot plumage ' + str(number % 4), colors[number % 4])
    return bird(T, number, kit, plumage, .8, mat('Parrot beak', (.95, .88, .70)))


def toucan(T, number, kit):
    return bird(T, number, kit, mat('Toucan black', (.07, .07, .08)), .9, mat('Toucan bill', (.98, .55, .08)))


def frog(T, number, kit):
    ell = kit['ell']
    skin = mat('Tree frog green', (.30, .72, .22))
    ell('Frog body', (0, .12, 0), (.16, .1, .2), skin)
    ell('Frog head', (0, .16, .16), (.12, .08, .1), skin, 3)
    for side in (-1, 1):
        ell('Frog eye', (side * .08, .24, .2), (.045, .045, .045), mat('Frog eye red', (.85, .15, .08)), 3)
        ell('Frog hind leg', (side * .14, .06, -.1), (.07, .05, .14), skin, 1)
        ell('Frog front leg', (side * .1, .04, .14), (.04, .04, .06), skin, 2)
    return 0.


def tapir(T, number, kit):
    coat = mat('Tapir grey', (.22, .21, .22))
    pale = mat('Tapir saddle', (.80, .78, .74))
    return quadruped(kit, number, .7, coat, lambda k: kit['ell']('Tapir saddle', (0, 1.25 * k, -.1 * k), (.42 * k, .4 * k, .5 * k), pale),
                     ears=(.08, .14, .06), snout=coat)


def jungle_snake(T, number, kit):
    ell = kit['ell']
    scales = mat('Emerald scales', (.12, .50, .22))
    for j in range(16):
        ell('Snake scales', (math.sin(j * .65) * .13, .075, -.85 + j * .12), (.08, .07, .13), scales)
    ell('Snake head', (math.sin(15 * .65) * .13, .09, 1.04), (.115, .085, .16), scales, 3)
    return 0.


STYLE_TREES['jungle'] = jungle_tree
STYLE_SCENERY['jungle'] = jungle_scenery
STYLE_PATCHES['jungle'] = jungle_patch
STYLE_BANKS['jungle'] = jungle_banks
SPECIES.update({
    'parrot': dict(kind=4, build=parrot),
    'toucan': dict(kind=4, build=toucan),
    'frog': dict(kind=1, build=frog),
    'tapir': dict(kind=0, build=tapir),
    'emerald snake': dict(kind=3, build=jungle_snake),
})


# --- Sakura Hills: spring in a Japanese garden landscape ---------------------------------------------
def sakura_tree(T, x, y, z, s, rng):
    bark = mat('Cherry bark', (.30, .22, .20))
    kind = rng.random()
    if kind < .45:
        pinks = [mat('Cherry blossom', (.97, .74, .82)), mat('Cherry blossom pale', (.99, .86, .90)),
                 mat('Cherry blossom deep', (.92, .56, .70))]
        top = (x + .2 * s, y + 2.6 * s, z)
        beam('Cherry trunk', (x, y, z), top, .2 * s, bark, 6, .14 * s)
        for k in range(2):
            a = k * 2.6 + rng.random()
            end = (top[0] + math.cos(a) * 1.4 * s, top[1] + .9 * s, top[2] + math.sin(a) * 1.4 * s)
            beam('Cherry branch', top, end, .1 * s, bark, 5)
            ico('Blossom cloud', end, (1.7 * s, 1.1 * s, 1.6 * s), pinks[rng.randrange(3)], 1)
        ico('Blossom cloud', (top[0], top[1] + 1.4 * s, top[2]), (2.1 * s, 1.3 * s, 2.0 * s), pinks[rng.randrange(3)], 1)
    elif kind < .65:
        # Japanese black pine: a leaning trunk with flat needle clouds.
        needles = mat('Pine cloud', (.14, .30, .20))
        lean = rng.uniform(-.8, .8)
        top = (x + lean * s, y + 3.6 * s, z + .4 * s)
        beam('Pine trunk', (x, y, z), top, .2 * s, bark, 6, .12 * s)
        for k in range(3):
            px, py, pz = x + lean * s * (.5 + k * .25) + rng.uniform(-.8, .8) * s, y + (2.2 + k * .8) * s, z + rng.uniform(-.8, .8) * s
            ico('Pine cloud', (px, py, pz), ((1.6 - k * .3) * s, .4 * s, (1.4 - k * .3) * s), needles, 1)
    elif kind < .8:
        maple = mat('Japanese maple', (.78, .16, .10))
        crowned_tree(x, y, z, s * .8, bark, [(0, 1, 0, 2.0, maple), (.9, .6, .4, 1.3, maple)], 2.4, rng)
    else:
        # A clump of bamboo with leafy tops.
        cane = mat('Bamboo cane', (.52, .66, .30)); leaf = mat('Bamboo leaves', (.36, .56, .22))
        for k in range(7):
            a, r = rng.uniform(0, math.tau), rng.uniform(0, .9) * s
            px, pz = x + math.cos(a) * r, z + math.sin(a) * r
            h = rng.uniform(4.5, 7) * s
            beam('Bamboo cane', (px, y, pz), (px + .2 * s, y + h, pz), .07 * s, cane, 5)
            ico('Bamboo leaves', (px + .2 * s, y + h, pz), (.6 * s, .9 * s, .6 * s), leaf, 1)


def sakura_scenery(ctx):
    api, T, rng, ground = ctx['api'], ctx['T'], ctx['rng'], ctx['ground']
    red = mat('Torii vermilion', (.82, .16, .08))
    black = mat('Lacquer black', (.08, .08, .09))
    tile = mat('Roof tiles', (.22, .25, .28))
    plaster = mat('Paper wall', (.92, .89, .80))
    timber = mat('Temple timber', (.40, .22, .14))
    stone = mat('Lantern granite', (.62, .62, .58))
    gravel = mat('Raked gravel', (.80, .78, .72))
    glow = mat('Lantern glow', GLOW)

    def roof(x, y, z, w, rise, flare=1.6):
        hw = w / 2 + flare
        vs = [(x - hw, y, z - hw), (x + hw, y, z - hw), (x + hw, y, z + hw), (x - hw, y, z + hw),
              (x - w * .2, y + rise, z - w * .2), (x + w * .2, y + rise, z - w * .2),
              (x + w * .2, y + rise, z + w * .2), (x - w * .2, y + rise, z + w * .2)]
        mesh('Flared roof', vs, [(0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7), (4, 5, 6, 7), (0, 3, 2, 1)], tile)

    def pagoda(x, y, z):
        h = y
        for k in range(5):
            w = 7 - k * .9
            box('Pagoda storey', (x, h + 1.3, z), (w, 2.6, w), plaster if k % 2 == 0 else timber)
            for side in range(4):
                a = side * math.pi / 2
                box('Pagoda beam', (x + math.sin(a) * w / 2, h + 2.4, z + math.cos(a) * w / 2), (w + .4, .3, .3), red, a)
            roof(x, h + 2.6, z, w, .9, 1.5 - k * .1)
            h += 3.5
        beam('Pagoda spire', (x, h - .9, z), (x, h + 3.5, z), .12, black, 6)
        for k in range(5):
            beam('Spire ring', (x, h + k * .5, z), (x, h + k * .5 + .12, z), .3, black, 8)

    def torii(x, y, z, yaw=0., s=1.):
        c, s_ = math.cos(yaw), math.sin(yaw)
        for side in (-1, 1):
            px, pz = x + c * side * 2.2 * s, z - s_ * side * 2.2 * s
            py = ground(px, pz)
            beam('Torii post', (px, py, pz), (px, py + 4.6 * s, pz), .22 * s, red, 8)
            box('Torii foot', (px, py + .25, pz), (.6 * s, .5, .6 * s), black, yaw)
        box('Torii beam', (x, y + 3.9 * s, z), (5.4 * s, .3 * s, .35 * s), red, yaw)
        box('Torii lintel', (x, y + 4.75 * s, z), (6.6 * s, .38 * s, .5 * s), black, yaw)
        box('Torii lintel red', (x, y + 4.5 * s, z), (6.2 * s, .2 * s, .44 * s), red, yaw)

    def torii_path(x, y, z):
        yaw = rng.uniform(0, math.pi)
        c, s_ = math.cos(yaw), math.sin(yaw)
        for k in range(6):
            px, pz = x + s_ * (k - 2.5) * 3.6, z + c * (k - 2.5) * 3.6
            torii(px, ground(px, pz), pz, yaw, .8)

    def lantern(x, y, z):
        box('Lantern base', (x, y + .3, z), (.8, .6, .8), stone)
        beam('Lantern post', (x, y + .6, z), (x, y + 1.5, z), .18, stone, 6)
        box('Lantern light box', (x, y + 1.85, z), (.65, .6, .65), glow)
        cone('Lantern roof', (x, y + 2.35, z), .75, .1, .45, stone, 6)

    def tea_house(x, y, z):
        yaw = rng.uniform(-.5, .5)
        box('Tea house deck', (x, y + .3, z), (9, .6, 7), timber, yaw)
        gable('Tea house', x, y + .6, z, 7, 5, 2.6, 2.0, plaster, tile, yaw, .9)
        c, s_ = math.cos(yaw), math.sin(yaw)
        for k in range(3):
            box('Shoji frame', (x + c * (k - 1) * 2.2 + s_ * 2.55, y + 1.9, z - s_ * (k - 1) * 2.2 + c * 2.55), (.1, 2.2, .1), timber, yaw)
        for k in range(4):
            px, pz = x + c * (k * 2.5 - 3.75) + s_ * 5.5, z - s_ * (k * 2.5 - 3.75) + c * 5.5
            beam('Paper lantern string', (px, y + 3.2, pz), (px, y + 2.7, pz), .02, black, 4)
            ico('Paper lantern', (px, y + 2.5, pz), (.3, .38, .3), glow, 1)

    def zen_garden(x, y, z):
        box('Raked gravel', (x, y + .06, z), (11, .14, 8), gravel)
        for k in range(5):
            box('Gravel furrow', (x, y + .14, z - 3 + k * 1.5), (10.6, .04, .12), mat('Raked gravel shade', (.70, .69, .64)))
        for k, (dx, dz, r) in enumerate(((-3, -1, 1.1), (2.5, 1.5, .8), (3.5, -2, .6))):
            ico('Garden rock', (x + dx, y + r * .5, z + dz), (r * 1.2, r * .8, r), mat('Garden rock', (.40, .41, .40)), 1)
        for side in (-1, 1):
            box('Garden fence', (x, y + .5, z + side * 4.4), (11.4, 1, .12), timber)

    def stalls(x, y, z):
        yaw = rng.uniform(0, math.pi)
        c, s_ = math.cos(yaw), math.sin(yaw)
        awnings = [mat('Stall awning red', (.86, .20, .14)), mat('Stall awning white', (.94, .92, .88))]
        for k in range(4):
            px, pz = x + c * (k - 1.5) * 4, z - s_ * (k - 1.5) * 4
            py = ground(px, pz)
            box('Festival stall', (px, py + 1, pz), (3.2, 2, 2.4), timber, yaw)
            box('Stall awning', (px, py + 2.6, pz), (3.6, .2, 3.0), awnings[k % 2], yaw)
            ico('Stall lantern', (px + s_ * 1.6, py + 2.2, pz + c * 1.6), (.28, .34, .28), glow, 1)

    rx, rz = T.sx(1), T.sz(1)
    landmark(ctx, 135 * rx, 70 * rz, 8, pagoda)
    landmark(ctx, 100 * rx, 95 * rz, 10, torii_path)
    landmark(ctx, -105 * rx, 75 * rz, 8, tea_house)
    landmark(ctx, 20 * rx, -95 * rz, 9, zen_garden)
    landmark(ctx, -20 * rx, -55 * rz, 10, stalls)
    landmark(ctx, -150 * rx, -110 * rz, 7, torii_path)
    for x, z in ((60, 10), (-30, 100), (-130, 40), (90, -60), (150, -110), (-90, -110)):
        landmark(ctx, x * rx, z * rz, 2, lantern)
    scatter(ctx, T.trees, 4.6, lambda x, y, z: tree(T, x, y - .05, z, rng.uniform(.75, 1.25), rng))
    boulders(ctx, T.boulders, T.colors['boulder'], 'Garden boulder', .7)


def sakura_patch(T, number, x, y, z, rng, ground, kit):
    ico, beam, mesh = kit['ico'], kit['beam'], kit['mesh']
    if number % 4 == 0:
        azalea = [mat('Azalea pink', (.93, .45, .65)), mat('Azalea white', (.95, .93, .92)), mat('Azalea magenta', (.80, .22, .50))]
        for k in range(rng.randrange(2, 4)):
            ico('Azalea', (x + rng.uniform(-.8, .8), y + .45, z + rng.uniform(-.8, .8)), (.75, .5, .7), azalea[rng.randrange(3)], 1)
    elif number % 4 == 1:
        ico('Moss mound', (x, y + .15, z), (.9, .3, .8), mat('Garden moss', (.34, .50, .22)), 1)
        ico('Garden stone', (x + .6, y + .2, z), (.4, .3, .35), kit['stone'], 1)
    else:
        for j in range(rng.randrange(3, 6)):
            a, b = x + rng.uniform(-1, 1), z + rng.uniform(-1, 1)
            yy = ground(a, b); h = rng.uniform(.3, .6)
            mesh('Iris leaves', [(a - .05, yy, b), (a + .05, yy, b), (a, yy + h, b)], [(0, 1, 2)], kit['foliage'])
            if j % 2 == 0:
                ico('Iris bloom', (a, yy + h, b), (.1, .08, .1), kit['flower'])


def sakura_banks(ctx):
    T, water, ground, nearest = ctx['T'], ctx['water'], ctx['ground'], ctx['nearest']
    rng = random.Random(613 + T.index)
    x0, x1, z0, z1 = ctx['watershed'].bounds
    pebble = mat('Wet river stones ' + T.key, T.colors['pebble'])
    iris = mat('Water iris', (.42, .30, .72)); leaves = mat('Iris leaves green', (.30, .50, .20))
    red = mat('Torii vermilion', (.82, .16, .08))
    bridged = False
    route = ctx['watershed'].route
    # An arched footbridge where the stream runs well away from the road.
    for k in range(len(route) // 3, len(route) - 3, 2):
        if bridged:
            break
        p, q = route[k - 1], route[k + 1]
        if nearest(p[0], p[2]) < 14 or not T.within(p[0], p[2], 115, 112) or not ctx['rail'].clear(p[0], p[2], 6):
            continue
        dx, dz = q[0] - p[0], q[2] - p[2]; l = math.hypot(dx, dz)
        nx, nz = -dz / l, dx / l
        x, z = route[k][0], route[k][2]
        y = max(ground(x + nx * 3, z + nz * 3), ground(x - nx * 3, z - nz * 3))
        pts = []
        for j in range(9):
            t = j / 8 - .5
            pts.append((x + nx * t * 7, y + .2 + 1.4 * (1 - (2 * t) ** 2), z + nz * t * 7))
        for a, b in zip(pts, pts[1:]):
            beam('Arched bridge deck', a, b, .55, red, 6)
            for side in (-1, 1):
                beam('Arched bridge rail', (a[0] + dx / l * side * .6, a[1] + .7, a[2] + dz / l * side * .6),
                     (b[0] + dx / l * side * .6, b[1] + .7, b[2] + dz / l * side * .6), .05, red, 4)
        bridged = True
    for _ in range(700):
        x = rng.uniform(x0 - 2, x1 + 2); z = rng.uniform(z0 - 2, z1 + 2)
        wet, level, _ = water(x, z)
        if not -1.8 < wet < .55 or nearest(x, z) < 2.8 or not T.within(x, z, 124, 122) or not ctx['rail'].clear(x, z, 1):
            continue
        y = ground(x, z); size = rng.uniform(.25, .6)
        ico('Riverbank pebble', (x, y + size * .2, z), (size, size * .45, size * .7), pebble, 1)
        if wet < 0 and rng.random() < .3:
            for blade in range(4):
                rx = x + rng.uniform(-.3, .3); rz = z + rng.uniform(-.3, .3); ry = ground(rx, rz)
                beam('Iris leaf', (rx, ry, rz), (rx + .05, ry + .6, rz), .03, leaves, 4)
            ico('Water iris', (x, ground(x, z) + .65, z), (.12, .1, .12), iris)


def crane(T, number, kit):
    ell, rod = kit['ell'], kit['rod']
    white = mat('Crane white', (.95, .95, .93)); black = mat('Crane black', (.09, .09, .1)); red = mat('Crane crown', (.85, .12, .10))
    ell('Crane body', (0, 1.0, 0), (.26, .28, .52), white)
    ell('Crane tail', (0, 1.05, -.45), (.2, .14, .3), black)
    rod('Crane neck', (0, 1.15, .35), (0, 1.75, .5), .07, black, 3)
    ell('Crane head', (0, 1.8, .55), (.09, .09, .13), white, 3)
    ell('Crane crown', (0, 1.88, .53), (.05, .03, .05), red, 3)
    ell('Crane beak', (0, 1.78, .72), (.03, .03, .1), kit['orange'], 3)
    for side in (-1, 1):
        rod('Crane leg', (side * .1, .8, 0), (side * .1, .03, .04), .03, black, 1 if side < 0 else 2)
    return 0.


def sika(T, number, kit):
    coat = mat('Sika deer coat', (.58, .36, .20))
    spots = mat('Sika spots', (.92, .88, .78))
    k = .85
    pivot = quadruped(kit, number, k, coat, lambda kk: kit['ell']('Deer tail', (0, 1.3 * kk, -.8 * kk), (.12 * kk, .16 * kk, .2 * kk), spots))
    for j in range(6):
        kit['ell']('Sika spot', ((j % 2 - .5) * .62 * k, 1.35 * k, (j // 2 - 1) * .4 * k), (.06, .06, .06), spots, 0)
    return pivot


def white_eye(T, number, kit):
    return bird(T, number, kit, mat('White-eye olive', (.52, .62, .22)), .6, mat('White-eye beak', (.2, .2, .2)))


STYLE_TREES['sakura'] = sakura_tree
STYLE_SCENERY['sakura'] = sakura_scenery
STYLE_PATCHES['sakura'] = sakura_patch
STYLE_BANKS['sakura'] = sakura_banks
SPECIES.update({
    'crane': dict(kind=2, build=crane, water=True),
    'sika deer': dict(kind=0, build=sika),
    'white-eye': dict(kind=4, build=white_eye),
})


# --- Harbor Lights: city docks at night ------------------------------------------------------------------
def harbor_tree(T, x, y, z, s, rng):
    trunk = mat('Street tree bark', (.32, .26, .22))
    if rng.random() < .35:
        palm(x, y, z, s * .85)
        return
    leaf = mat('Linden green', (.26, .44, .20)); leaf2 = mat('Linden light', (.34, .52, .24))
    box('Tree planter', (x, y + .2, z), (1.4 * s, .4, 1.4 * s), mat('Planter concrete', (.58, .57, .55)))
    crowned_tree(x, y, z, s, trunk, [(0, 1.0, 0, 1.9, leaf), (.8, .5, .3, 1.3, leaf2)], 3.0, rng)


def harbor_scenery(ctx):
    api, T, rng, ground, nearest, water = ctx['api'], ctx['T'], ctx['rng'], ctx['ground'], ctx['nearest'], ctx['water']
    walls = [mat('Brick red', (.58, .26, .18)), mat('Stucco cream', (.86, .80, .68)), mat('Concrete grey', (.56, .57, .58)),
             mat('Painted teal', (.20, .46, .48)), mat('Ochre facade', (.80, .58, .26))]
    roofs = [mat('Flat roof tar', (.18, .19, .20)), mat('Roof tiles', (.42, .24, .18))]
    glow = mat('Lantern glow', GLOW)
    dark_glass = mat('Office glass', (.10, .16, .22))
    steel = mat('Crane yellow', (.92, .66, .10))
    red = mat('Signal red', (.80, .10, .08)); white = api['cream']
    containers = [mat('Container ' + str(i), c) for i, c in enumerate(((.70, .16, .10), (.12, .36, .62), (.15, .48, .30),
                                                                         (.86, .52, .10), (.42, .44, .46), (.60, .20, .48)))]
    hull = mat('Ship hull', (.14, .18, .24)); deck = mat('Ship deck', (.62, .26, .16))

    def building(x, y, z, w, d, floors, wall, yaw=0., lit=.5):
        h = floors * 3.2
        box('Building', (x, y + h / 2, z), (w, h, d), wall, yaw)
        box('Roof parapet', (x, y + h + .2, z), (w + .3, .4, d + .3), roofs[0], yaw)
        c, s_ = math.cos(yaw), math.sin(yaw)
        cols = max(1, int(w / 3.2)); rows = max(1, int(d / 3.2))
        for f in range(floors):
            fy = y + 1.8 + f * 3.2
            for face, (count, span, depth, angle) in enumerate(((cols, w, d / 2 + .02, yaw), (cols, w, -d / 2 - .02, yaw),
                                                                 (rows, d, w / 2 + .02, yaw + math.pi / 2),
                                                                 (rows, d, -w / 2 - .02, yaw + math.pi / 2))):
                ca, sa = math.cos(angle), math.sin(angle)
                for k in range(count):
                    a = (k - (count - 1) / 2) * (span / count)
                    m = glow if rng.random() < lit else dark_glass
                    box('Window', (x + ca * a + sa * depth, fy, z - sa * a + ca * depth), (1.3, 1.5, .06), m, angle)
        if rng.random() < .4:
            box('Rooftop tank', (x + c * w * .2, y + h + 1.2, z - s_ * w * .2), (2, 2, 2), roofs[0], yaw)

    def block(x, y, z):
        # A city block of three to five buildings round a court.
        for k in range(rng.randrange(3, 6)):
            bx, bz = x + rng.uniform(-9, 9), z + rng.uniform(-9, 9)
            if nearest(bx, bz) < 12:
                continue
            building(bx, ground(bx, bz), bz, rng.uniform(7, 12), rng.uniform(7, 12), rng.randrange(2, 7),
                     walls[rng.randrange(len(walls))], rng.choice((0, math.pi / 2)) + rng.uniform(-.05, .05))

    def warehouse(x, y, z):
        yaw = rng.choice((0, math.pi / 2))
        box('Warehouse', (x, y + 3.5, z), (22, 7, 12), walls[0] if rng.random() < .5 else walls[2], yaw)
        c, s_ = math.cos(yaw), math.sin(yaw)
        mesh('Warehouse roof', [(x - c * 11.4 + s_ * 6.4, y + 7, z + s_ * 11.4 + c * 6.4), (x + c * 11.4 + s_ * 6.4, y + 7, z - s_ * 11.4 + c * 6.4),
                                (x + c * 11.4, y + 9, z - s_ * 11.4), (x - c * 11.4, y + 9, z + s_ * 11.4)], [(0, 1, 2, 3)], roofs[0])
        mesh('Warehouse roof', [(x - c * 11.4 - s_ * 6.4, y + 7, z + s_ * 11.4 - c * 6.4), (x - c * 11.4, y + 9, z + s_ * 11.4),
                                (x + c * 11.4, y + 9, z - s_ * 11.4), (x + c * 11.4 - s_ * 6.4, y + 7, z - s_ * 11.4 - c * 6.4)], [(0, 1, 2, 3)], roofs[0])
        for k in (-6, 0, 6):
            box('Loading door', (x + c * k + s_ * 6.05, y + 2.2, z - s_ * k + c * 6.05), (3.6, 4.2, .1), mat('Roller door', (.46, .48, .50)), yaw)
            box('Door lamp', (x + c * k + s_ * 6.15, y + 4.8, z - s_ * k + c * 6.15), (.5, .3, .2), glow, yaw)

    def crane(x, y, z):
        yaw = rng.uniform(0, math.pi)
        c, s_ = math.cos(yaw), math.sin(yaw)
        for side in (-1, 1):
            for end in (-1, 1):
                px, pz = x + c * side * 5 + s_ * end * 4, z - s_ * side * 5 + c * end * 4
                beam('Crane leg', (px, ground(px, pz), pz), (px, y + 16, pz), .35, steel, 6)
        box('Crane portal', (x, y + 16.5, z), (11, 1.2, 9), steel, yaw)
        box('Crane boom', (x + s_ * 6, y + 18, z + c * 6), (2, 1, 30), steel, yaw)
        box('Crane cab', (x + s_ * 2, y + 15.4, z + c * 2), (2.4, 2, 2.4), white, yaw)
        box('Crane lamp', (x + s_ * 20, y + 17.4, z + c * 20), (.8, .4, .8), glow, yaw)
        for k in range(8):
            beam('Crane tie', (x, y + 19, z), (x + s_ * (k * 4 - 6), y + 18.5, z + c * (k * 4 - 6)), .06, steel, 4)

    def container_stack(x, y, z):
        yaw = rng.choice((0, math.pi / 2))
        c, s_ = math.cos(yaw), math.sin(yaw)
        for row in range(3):
            for col in range(2):
                for level in range(rng.randrange(1, 4)):
                    px, pz = x + c * (row - 1) * 6.4 + s_ * (col - .5) * 2.6, z - s_ * (row - 1) * 6.4 + c * (col - .5) * 2.6
                    box('Shipping container', (px, y + 1.3 + level * 2.6, pz), (6.1, 2.5, 2.45), containers[rng.randrange(6)], yaw)

    def lighthouse(x, y, z):
        beam('Lighthouse tower', (x, y, z), (x, y + 14, z), 1.6, white, 12, 1.1)
        for k in range(3):
            beam('Lighthouse band', (x, y + 3 + k * 4, z), (x, y + 4.3 + k * 4, z), 1.55 - k * .14, red, 12)
        box('Lantern gallery', (x, y + 14.3, z), (3.2, .5, 3.2), api['dark'])
        beam('Lantern room', (x, y + 14.5, z), (x, y + 16.2, z), .9, glow, 10)
        cone('Lantern cap', (x, y + 16.8, z), 1.1, .1, 1.2, red, 10)

    def ship(x, z, yaw):
        c, s_ = math.cos(yaw), math.sin(yaw)
        y = -6.85
        box('Ship hull', (x, y + 1.5, z), (7, 4, 34), hull, yaw)
        box('Ship deck', (x, y + 3.55, z), (6.8, .1, 33), deck, yaw)
        box('Ship bridge', (x - s_ * 13, y + 7, z - c * 13), (6, 6, 5), white, yaw)
        window_row(x - s_ * 13, y + 9, z - c * 13, yaw, 3, 1.8, (1.2, .8), glow, 2.55)
        box('Ship funnel', (x - s_ * 15.5, y + 11, z - c * 15.5), (2, 3, 2), red, yaw)
        for k in range(4):
            for level in range(2):
                box('Deck container', (x + s_ * (k * 6.3 - 6), y + 4.9 + level * 2.6, z + c * (k * 6.3 - 6)),
                    (6, 2.5, 6.1), containers[(k + level) % 6], yaw)

    def street_lamps():
        path = ctx['path']
        for i in range(0, len(path), 18):
            p = path[i]
            for side in (-1, 1):
                if (i // 18) % 2 and side > 0:
                    continue
                x, y, z = ctx['anchor'](i, side, 2.6)
                if water(x, z)[0] > -2 or not ctx['rail'].clear(x, z, 1):
                    continue
                beam('Street lamp post', (x, y, z), (x, y + 5, z), .08, api['dark'], 5)
                ex, ez = x - p[4] * side * 1.2, z + p[3] * side * 1.2
                beam('Street lamp arm', (x, y + 5, z), (ex, y + 5.3, ez), .06, api['dark'], 4)
                box('Street lamp head', (ex, y + 5.1, ez), (.6, .2, .4), glow)

    rx, rz = T.sx(1), T.sz(1)
    street_lamps()
    for x, z in ((10, 40), (-65, 20), (-70, 100), (120, 0), (95, 110), (10, 105), (-115, -60), (0, -40), (-120, 60)):
        landmark(ctx, x * rx, z * rz, 10, block)
    for x, z in ((-60, -110), (60, -105), (130, -70)):
        landmark(ctx, x * rx, z * rz, 13, warehouse)
    for x, z in ((-20, -112), (95, -115)):
        landmark(ctx, x * rx, z * rz, 9, container_stack)
    landmark(ctx, -135 * rx, -110 * rz, 10, crane)
    landmark(ctx, 150 * rx, -120 * rz, 10, crane)
    landmark(ctx, 165 * rx, 120 * rz, 3, lighthouse)
    # Ships moor in the sea off the quay.
    for k, (x, z, yaw) in enumerate(((-70, -178, math.pi / 2), (50, -182, math.pi / 2 + .1), (-185, 30, .05))):
        ship(x * rx, z * rz, yaw)
    scatter(ctx, T.trees, 6, lambda x, y, z: tree(T, x, y - .05, z, rng.uniform(.8, 1.15), rng))


def harbor_patch(T, number, x, y, z, rng, ground, kit):
    ico, beam, mesh = kit['ico'], kit['beam'], kit['mesh']
    wood = mat('Crate wood', (.60, .44, .26)); steel = mat('Barrel blue', (.16, .34, .58))
    if number % 3 == 0:
        for k in range(rng.randrange(2, 5)):
            box('Crate', (x + rng.uniform(-1, 1), y + .4 + (k % 2) * .8, z + rng.uniform(-1, 1)), (.8, .8, .8), wood, rng.uniform(0, 1))
    elif number % 3 == 1:
        for k in range(rng.randrange(2, 4)):
            a, b = x + rng.uniform(-.8, .8), z + rng.uniform(-.8, .8)
            beam('Oil drum', (a, ground(a, b), b), (a, ground(a, b) + .9, b), .3, steel, 8)
    else:
        ico('Planter shrub', (x, y + .5, z), (.8, .6, .8), kit['foliage'], 1)
        box('Planter', (x, y + .2, z), (1.6, .4, 1.6), mat('Planter concrete', (.58, .57, .55)))


def gull(T, number, kit):
    return bird(T, number, kit, mat('Gull white', (.92, .93, .92)), .9, kit['orange'])


def pigeon(T, number, kit):
    ell, rod = kit['ell'], kit['rod']
    grey = mat('Pigeon grey', (.48, .50, .55)); sheen = mat('Pigeon sheen', (.32, .42, .40))
    ell('Pigeon body', (0, .2, 0), (.12, .12, .2), grey)
    ell('Pigeon breast', (0, .25, .12), (.1, .1, .1), sheen)
    ell('Pigeon head', (0, .34, .18), (.06, .06, .07), grey, 3)
    ell('Pigeon beak', (0, .33, .25), (.02, .02, .04), mat('Pigeon beak', (.3, .3, .3)), 3)
    for side in (-1, 1):
        rod('Pigeon leg', (side * .04, .1, 0), (side * .04, .01, .02), .015, mat('Pigeon feet', (.75, .35, .35)), 1 if side < 0 else 2)
    return 0.


def cat(T, number, kit):
    coats = [(.85, .52, .22), (.15, .15, .16), (.70, .70, .68)]
    coat = mat('Cat coat ' + str(number % 3), coats[number % 3])
    return quadruped(kit, number, .32, coat, lambda k: kit['rod']('Cat tail', (0, 1.0 * k, -.8 * k), (0, 1.8 * k, -1.2 * k), .06 * k, coat),
                     ears=(.1, .2, .05), snout=coat)


STYLE_TREES['harbor'] = harbor_tree
STYLE_SCENERY['harbor'] = harbor_scenery
STYLE_PATCHES['harbor'] = harbor_patch
STYLE_BANKS['harbor'] = lambda ctx: None
SPECIES.update({
    'gull': dict(kind=4, build=gull),
    'pigeon': dict(kind=2, build=pigeon),
    'cat': dict(kind=0, build=cat),
})


# --- Highland Loch: Scottish moorland ----------------------------------------------------------------
def highland_tree(T, x, y, z, s, rng):
    kind = rng.random()
    if kind < .55:
        # Scots pine: a tall bare orange trunk and a flat crown.
        bark = mat('Scots pine bark', (.58, .36, .22)); needles = mat('Scots pine needles', (.16, .30, .20))
        top = (x + rng.uniform(-.4, .4) * s, y + 5.2 * s, z)
        beam('Pine trunk', (x, y, z), top, .2 * s, bark, 6, .12 * s)
        ico('Pine crown', (top[0], top[1] + .5 * s, top[2]), (2.0 * s, .8 * s, 1.8 * s), needles, 1)
        ico('Pine crown', (top[0] + .9 * s, top[1] - .5 * s, top[2] + .3 * s), (1.2 * s, .5 * s, 1.1 * s), needles, 1)
    elif kind < .8:
        bark = mat('Birch bark', (.88, .86, .80)); crown = mat('Highland birch', (.52, .62, .26))
        beam('Birch trunk', (x, y, z), (x, y + 3.6 * s, z), .13 * s, bark, 6, .08 * s)
        ico('Birch crown', (x, y + 4.0 * s, z), (1.4 * s, 1.8 * s, 1.4 * s), crown, 1)
    else:
        # Gorse: a spiny dark bush in yellow flower.
        bush = mat('Gorse green', (.22, .32, .14)); bloom = mat('Gorse flower', (.96, .80, .14))
        ico('Gorse bush', (x, y + .7 * s, z), (1.3 * s, .9 * s, 1.2 * s), bush, 1)
        for k in range(5):
            a = k * 1.3
            ico('Gorse bloom', (x + math.cos(a) * .9 * s, y + (1.1 + (k % 2) * .3) * s, z + math.sin(a) * .9 * s),
                (.3 * s, .2 * s, .3 * s), bloom, 1)


def highland_scenery(ctx):
    api, T, rng, ground, nearest = ctx['api'], ctx['T'], ctx['rng'], ctx['ground'], ctx['nearest']
    granite = mat('Castle granite', (.52, .52, .50))
    dark = mat('Castle shadow', (.30, .30, .30))
    whitewash = mat('Croft whitewash', (.92, .91, .86))
    slate = mat('Slate roof', (.24, .26, .29))
    thatch = mat('Thatch', (.62, .50, .30))
    glow = mat('Lantern glow', GLOW)
    red = mat('Phone box red', (.80, .10, .08))
    drystone = mat('Dry stone', (.50, .48, .44))

    def castle(x, y, z):
        # A ruined keep, a curtain wall with a gate and corner towers.
        box('Castle keep', (x, y + 7, z), (10, 14, 10), granite)
        for k in range(4):
            a = k * math.pi / 2
            for j in (-1, 1):
                box('Battlement', (x + math.sin(a) * 5 + math.cos(a) * j * 3, y + 14.6, z + math.cos(a) * 5 - math.sin(a) * j * 3),
                    (1.2, 1.2, 1.2), granite, a)
            box('Arrow slit', (x + math.sin(a) * 5.05, y + 9, z + math.cos(a) * 5.05), (.4, 1.8, .1), dark, a)
        box('Keep window', (x, y + 11, z + 5.05), (1.4, 2, .1), glow)
        for k in range(4):
            a = k * math.pi / 2 + math.pi / 4
            tx, tz = x + math.cos(a) * 16, z + math.sin(a) * 16
            ty = ground(tx, tz)
            beam('Corner tower', (tx, ty - .5, tz), (tx, ty + 10 - (k % 2) * 3, tz), 2.6, granite, 10)
            cone('Tower top', (tx, ty + 10.5 - (k % 2) * 3, tz), 2.9, 2.9, 1.0, dark, 10)
        for k in range(4):
            a0, a1 = k * math.pi / 2 + math.pi / 4, (k + 1) * math.pi / 2 + math.pi / 4
            p0 = (x + math.cos(a0) * 16, z + math.sin(a0) * 16); p1 = (x + math.cos(a1) * 16, z + math.sin(a1) * 16)
            if k == 1:
                continue  # a breach in the north wall
            for j in range(6):
                f0, f1 = j / 6, (j + 1) / 6
                ax, az = p0[0] + (p1[0] - p0[0]) * f0, p0[1] + (p1[1] - p0[1]) * f0
                bx, bz = p0[0] + (p1[0] - p0[0]) * f1, p0[1] + (p1[1] - p0[1]) * f1
                h = 6 if k != 3 or j not in (2, 3) else 2.5
                mx, mz = (ax + bx) / 2, (az + bz) / 2
                box('Curtain wall', (mx, ground(mx, mz) + h / 2 - .4, mz), (math.hypot(bx - ax, bz - az) + .2, h, 1.4), granite,
                    math.atan2(-(bz - az), bx - ax))

    def croft(x, y, z):
        yaw = rng.uniform(-.6, .6)
        c, s_ = math.cos(yaw), math.sin(yaw)
        gable('Croft', x, y, z, 8, 5, 2.8, 2.4, whitewash, thatch if rng.random() < .5 else slate, yaw, .3)
        for end in (-1, 1):
            box('Croft chimney', (x + c * end * 3.8, y + 5, z - s_ * end * 3.8), (.9, 2, .9), drystone, yaw)
        window_row(x, y + 1.6, z, yaw, 2, 3.4, (1.0, .9), glow, 2.52)
        box('Croft door', (x + s_ * 2.52, y + 1.0, z + c * 2.52), (1, 1.9, .08), mat('Croft door blue', (.18, .30, .52)), yaw)

    def stones(x, y, z):
        # A circle of standing stones round a fallen altar.
        for k in range(9):
            a = k * math.tau / 9
            px, pz = x + math.cos(a) * 7, z + math.sin(a) * 7
            h = rng.uniform(2.2, 3.6)
            box('Standing stone', (px, ground(px, pz) + h / 2 - .2, pz), (1.1, h, .6), drystone, -a + rng.uniform(-.2, .2))
        box('Altar stone', (x, y + .3, z), (2.6, .6, 1.2), drystone, rng.uniform(0, math.pi))

    def phone_box(x, y, z):
        box('Phone box', (x, y + 1.3, z), (1, 2.6, 1), red)
        box('Phone box sign', (x, y + 2.4, z + .51), (.7, .2, .04), glow)
        box('Phone box top', (x, y + 2.7, z), (1.1, .2, 1.1), red)

    def sheep_pen(x, y, z):
        for k in range(12):
            a, b = k * math.tau / 12, (k + 1) * math.tau / 12
            ax, az = x + math.cos(a) * 6, z + math.sin(a) * 6
            bx, bz = x + math.cos(b) * 6, z + math.sin(b) * 6
            if k == 0:
                continue
            mx, mz = (ax + bx) / 2, (az + bz) / 2
            box('Pen wall', (mx, ground(mx, mz) + .45, mz), (math.hypot(bx - ax, bz - az) + .2, .9, .6), drystone,
                math.atan2(-(bz - az), bx - ax))

    def walls():
        # Dry stone walls follow field boundaries over the hills.
        made = 0
        for _ in range(400):
            if made >= T.walls:
                break
            x, z = rng.uniform(-T.sx(100), T.sx(100)), rng.uniform(-T.sz(100), T.sz(100))
            a = rng.uniform(0, math.pi)
            dx, dz = math.cos(a), math.sin(a)
            length = rng.uniform(30, 60)
            pts = [(x + dx * t, z + dz * t) for t in range(0, int(length), 3)]
            if not all(T.within(px, pz, 112, 110) and nearest(px, pz) > 6 and ctx['water'](px, pz)[0] < -2 and
                       ctx['rail'].clear(px, pz, 2) and
                       not any(a0 - 1 < px < b0 + 1 and c0 - 1 < pz < d0 + 1 for a0, b0, c0, d0 in ctx['bounds'])
                       for px, pz in pts):
                continue
            for (px, pz), (qx, qz) in zip(pts, pts[1:]):
                mx, mz = (px + qx) / 2, (pz + qz) / 2
                box('Dry stone wall', (mx, ground(mx, mz) + .5, mz), (3.15, 1.0, .7), drystone, -a)
            made += 1

    rx, rz = T.sx(1), T.sz(1)
    landmark(ctx, 0, 112 * rz, 19, castle)
    for x, z in ((-150, -30), (140, 110), (150, -120), (-60, -120), (120, -10)):
        landmark(ctx, x * rx, z * rz, 6, croft)
    landmark(ctx, 0, -110 * rz, 9, stones)
    landmark(ctx, -40 * rx, -100 * rz, 2, phone_box)
    landmark(ctx, 135 * rx, 40 * rz, 7, sheep_pen)
    walls()
    scatter(ctx, T.trees, 5, lambda x, y, z: tree(T, x, y - .05, z, rng.uniform(.8, 1.3), rng))
    boulders(ctx, T.boulders, T.colors['boulder'], 'Lichen boulder', .75)


def highland_patch(T, number, x, y, z, rng, ground, kit):
    ico, beam, mesh = kit['ico'], kit['beam'], kit['mesh']
    if number % 3 == 0:
        heather = [mat('Heather purple', (.56, .32, .54)), mat('Heather mauve', (.66, .44, .62))]
        for k in range(rng.randrange(3, 7)):
            a, b = x + rng.uniform(-1.2, 1.2), z + rng.uniform(-1.2, 1.2)
            ico('Heather clump', (a, ground(a, b) + .2, b), (.5, .25, .45), heather[k % 2], 1)
    elif number % 3 == 1:
        bracken = mat('Bracken', (.58, .46, .22))
        for k in range(rng.randrange(4, 7)):
            ang = k * 1.1 + rng.random()
            mesh('Bracken frond', [(x, y, z), (x + math.cos(ang + .3) * .4, y + .8, z + math.sin(ang + .3) * .4),
                                   (x + math.cos(ang) * 1.0, y + .5, z + math.sin(ang) * 1.0)], [(0, 1, 2)], bracken)
    else:
        ico('Moor stone', (x, y + .25, z), (.7, .4, .6), kit['stone'], 1)
        if number % 2:
            ico('Lichen patch', (x + .1, y + .55, z), (.4, .06, .3), mat('Lichen', (.72, .74, .52)), 1)


def highland_banks(ctx):
    T, water, ground, nearest = ctx['T'], ctx['water'], ctx['ground'], ctx['nearest']
    rng = random.Random(613 + T.index)
    x0, x1, z0, z1 = ctx['watershed'].bounds
    pebble = mat('Wet river stones ' + T.key, T.colors['pebble']); rush = mat('Bog rush', (.38, .42, .20))
    for _ in range(800):
        x = rng.uniform(x0 - 2, x1 + 2); z = rng.uniform(z0 - 2, z1 + 2)
        wet, level, _ = water(x, z)
        if not -1.8 < wet < .55 or nearest(x, z) < 2.8 or not T.within(x, z, 124, 122) or not ctx['rail'].clear(x, z, 1):
            continue
        y = ground(x, z); size = rng.uniform(.3, .8)
        ico('Riverbank pebble', (x, y + size * .2, z), (size, size * .45, size * .7), pebble, 1)
        if rng.random() < .3:
            for blade in range(5):
                rx = x + rng.uniform(-.4, .4); rz = z + rng.uniform(-.4, .4); ry = ground(rx, rz)
                beam('Bog rush', (rx, ry, rz), (rx + .08, ry + .6, rz + .05), .03, rush, 4)


def sheep(T, number, kit):
    ell, rod = kit['ell'], kit['rod']
    wool = mat('Sheep wool', (.92, .91, .86)); face = mat('Sheep face', (.10, .10, .10))
    k = .62
    ell('Fleece', (0, 1.1 * k, 0), (.55 * k, .55 * k, .85 * k), wool)
    rod('Neck', (0, 1.25 * k, .6 * k), (0, 1.55 * k, .85 * k), .18 * k, wool, 3)
    ell('Head', (0, 1.55 * k, 1.0 * k), (.2 * k, .22 * k, .32 * k), face, 3)
    for side in (-1, 1):
        ell('Ear', (side * .22 * k, 1.65 * k, .9 * k), (.14 * k, .05 * k, .07 * k), face, 3)
        for front in (-1, 1):
            part = 1 if side * front > 0 else 2
            rod('Leg', (side * .25 * k, .8 * k, front * .5 * k), (side * .25 * k, .05 * k, front * .5 * k), .07 * k, face, part)
    return k - 1


def highland_cow(T, number, kit):
    coat = mat('Highland coat', (.72, .38, .16)); horn = mat('Highland horn', (.88, .82, .66))
    def horns(side, k):
        kit['rod']('Horn', (side * .2 * k, 1.95 * k, .9 * k), (side * .75 * k, 2.2 * k, .95 * k), .07 * k, horn, 3)
        kit['rod']('Horn tip', (side * .75 * k, 2.2 * k, .95 * k), (side * .85 * k, 2.55 * k, .9 * k), .05 * k, horn, 3)
    pivot = quadruped(kit, number, .9, coat, lambda k: kit['ell']('Tail', (0, 1.2 * k, -.85 * k), (.1 * k, .4 * k, .1 * k), coat),
                      ears=(.14, .1, .08), snout=mat('Cow nose', (.25, .18, .16)), horns=horns)
    kit['ell']('Shaggy fringe', (0, 1.95 * .9, 1.05 * .9), (.26, .12, .2), coat, 3)
    return pivot


def red_deer(T, number, kit):
    coat = mat('Red deer coat', (.52, .30, .18))
    def antlers(side, k):
        kit['rod']('Stag antler', (side * .13 * k, 2.0 * k, .85 * k), (side * .5 * k, 2.9 * k, .6 * k), .05 * k, kit['dry'], 3)
        for j in range(3):
            kit['rod']('Antler tine', (side * (.22 + j * .1) * k, (2.25 + j * .22) * k, .76 * k),
                       (side * (.42 + j * .1) * k, (2.5 + j * .22) * k, .92 * k), .03 * k, kit['dry'], 3)
    return quadruped(kit, number, 1.05, coat, lambda k: kit['ell']('Deer tail', (0, 1.35 * k, -.8 * k), (.12 * k, .16 * k, .2 * k), kit['pale']),
                     horns=antlers if number % 2 == 0 else None)


def mountain_hare(T, number, kit):
    return jackrabbit(T, number, kit)


def eagle(T, number, kit):
    return bird(T, number, kit, mat('Eagle brown', (.30, .20, .12)), 1.6, mat('Eagle beak', (.92, .74, .20)))


STYLE_TREES['highland'] = highland_tree
STYLE_SCENERY['highland'] = highland_scenery
STYLE_PATCHES['highland'] = highland_patch
STYLE_BANKS['highland'] = highland_banks
SPECIES.update({
    'sheep': dict(kind=0, build=sheep),
    'highland cow': dict(kind=0, build=highland_cow),
    'red deer': dict(kind=0, build=red_deer),
    'mountain hare': dict(kind=1, build=mountain_hare),
    'eagle': dict(kind=4, build=eagle),
})


# --- Volcano Peak: an island volcano with a lava lake ------------------------------------------------
def volcano_tree(T, x, y, z, s, rng):
    kind = rng.random()
    if y < 6 and kind < .5:
        palm(x, y, z, s * .9)
    elif kind < .65:
        # A charred trunk left by an old flow.
        char = mat('Charred wood', (.12, .10, .09))
        top = (x + .3 * s, y + 3.4 * s, z)
        beam('Charred trunk', (x, y, z), top, .2 * s, char, 5, .08 * s)
        for k in range(2):
            a = k * 2.5 + rng.random()
            beam('Charred branch', (x, y + 2.2 * s, z), (x + math.cos(a) * 1.2 * s, y + 3 * s, z + math.sin(a) * 1.2 * s), .07 * s, char, 4)
    else:
        # A tough ohia shrub with red blossoms.
        bark = mat('Ohia bark', (.30, .24, .20)); leaf = mat('Ohia leaves', (.24, .38, .22)); bloom = mat('Ohia blossom', (.88, .16, .10))
        crowned_tree(x, y, z, s * .8, bark, [(0, .8, 0, 1.7, leaf), (.7, .5, .3, 1.1, leaf)], 2.2, rng)
        for k in range(4):
            a = k * 1.6
            ico('Ohia blossom', (x + math.cos(a) * 1.2 * s, y + 2.9 * s, z + math.sin(a) * 1.2 * s), (.18 * s, .14 * s, .18 * s), bloom)


def volcano_scenery(ctx):
    api, T, rng, ground = ctx['api'], ctx['T'], ctx['rng'], ctx['ground']
    basalt = mat('Basalt column', (.17, .16, .16))
    white = mat('Observatory white', (.90, .91, .92))
    metal = mat('Observatory metal', (.55, .58, .60))
    glow = mat('Lantern glow', GLOW)
    yellow = mat('Warning yellow', (.95, .78, .10))
    hut = mat('Station hut orange', (.86, .40, .12))

    def columns(x, y, z):
        for k in range(14):
            a, r = rng.uniform(0, math.tau), rng.uniform(0, 4)
            px, pz = x + math.cos(a) * r, z + math.sin(a) * r
            h = rng.uniform(1.5, 6.5)
            cone('Basalt column', (px, ground(px, pz) + h / 2 - .3, pz), .75, .75, h, basalt, 6)

    def observatory(x, y, z):
        beam('Observatory drum', (x, y, z), (x, y + 4.5, z), 4, white, 16)
        ico('Observatory dome', (x, y + 4.5, z), (4.1, 3.6, 4.1), white, 2)
        box('Dome slit', (x, y + 6.4, z + 3.2), (1.2, 3, .3), metal, 0)
        box('Observatory door', (x, y + 1.2, z + 4.02), (1.2, 2.4, .1), metal)
        box('Observatory light', (x, y + 3.2, z + 4.05), (.5, .3, .12), glow)

    def station(x, y, z):
        yaw = rng.uniform(0, math.pi)
        c, s_ = math.cos(yaw), math.sin(yaw)
        for k in range(2):
            px, pz = x + c * (k - .5) * 7, z - s_ * (k - .5) * 7
            box('Research hut', (px, ground(px, pz) + 1.5, pz), (5, 3, 4), hut, yaw)
            window_row(px, ground(px, pz) + 1.8, pz, yaw, 2, 2.2, (.9, .8), glow, 2.02)
        beam('Antenna mast', (x + s_ * 5, y, z + c * 5), (x + s_ * 5, y + 9, z + c * 5), .1, metal, 5)
        box('Mast beacon', (x + s_ * 5, y + 9.2, z + c * 5), (.3, .3, .3), glow)

    def warning(x, y, z):
        beam('Warning sign post', (x, y, z), (x, y + 2.2, z), .07, metal, 5)
        tri = [(x - .7, y + 1.8, z), (x + .7, y + 1.8, z), (x, y + 3.0, z)]
        mesh('Warning sign', tri, [(0, 1, 2), (0, 2, 1)], yellow)

    def vent(x, y, z):
        for k in range(8):
            a = k * math.tau / 8
            px, pz = x + math.cos(a) * 1.6, z + math.sin(a) * 1.6
            ico('Vent rock', (px, ground(px, pz) + .3, pz), (.7, .5, .6), mat('Sulphur crust', (.86, .78, .22)) if k % 3 == 0 else basalt, 1)

    rx, rz = T.sx(1), T.sz(1)
    for x, z in ((150, 120), (-150, 150), (160, -60), (-90, -150), (60, 150), (-170, -40)):
        landmark(ctx, x * rx, z * rz, 5, columns)
    landmark(ctx, 120 * rx, 60 * rz, 6, observatory)
    landmark(ctx, -120 * rx, 110 * rz, 8, station)
    for x, z in ((80, -60), (-80, 60), (60, 90), (-60, -80), (0, -120), (0, 130)):
        landmark(ctx, x * rx, z * rz, 2, warning)
    for x, z in ((100, 0), (-100, 20), (20, -95), (-30, 95)):
        landmark(ctx, x * rx, z * rz, 3, vent)
    scatter(ctx, T.trees, 5, lambda x, y, z: tree(T, x, y - .05, z, rng.uniform(.75, 1.2), rng))
    boulders(ctx, T.boulders, T.colors['boulder'], 'Lava boulder', .7)


def volcano_patch(T, number, x, y, z, rng, ground, kit):
    ico, beam, mesh = kit['ico'], kit['beam'], kit['mesh']
    if number % 4 == 0:
        ico('Sulphur crust', (x, y + .08, z), (.8, .12, .7), mat('Sulphur crust', (.86, .78, .22)), 1)
    elif number % 4 == 1:
        for k in range(rng.randrange(3, 6)):
            a, b = x + rng.uniform(-1, 1), z + rng.uniform(-1, 1)
            ico('Lava rock', (a, ground(a, b) + .15, b), (.35, .25, .3), kit['stone'], 1)
    else:
        for k in range(rng.randrange(3, 5)):
            ang = k * 1.6 + rng.random()
            mesh('Fern frond', [(x, y, z), (x + math.cos(ang + .3) * .3, y + .5, z + math.sin(ang + .3) * .3),
                                (x + math.cos(ang) * .8, y + .2, z + math.sin(ang) * .8)], [(0, 1, 2)], kit['foliage'])


def volcano_banks(ctx):
    """Scorched rocks round the lava lake's shore."""
    T, water, ground, nearest = ctx['T'], ctx['water'], ctx['ground'], ctx['nearest']
    rng = random.Random(613 + T.index)
    x0, x1, z0, z1 = ctx['watershed'].bounds
    rock = mat('Glowing rock', (.22, .10, .08)); basalt = mat('Basalt column', (.17, .16, .16))
    for _ in range(500):
        x = rng.uniform(x0 - 3, x1 + 3); z = rng.uniform(z0 - 3, z1 + 3)
        wet, level, _ = water(x, z)
        if not -2.5 < wet < .3 or nearest(x, z) < 3 or not T.within(x, z, 124, 122) or not ctx['rail'].clear(x, z, 1):
            continue
        y = ground(x, z); size = rng.uniform(.3, .9)
        ico('Shore rock', (x, y + size * .2, z), (size, size * .5, size * .7), rock if rng.random() < .4 else basalt, 1)


def raven(T, number, kit):
    return bird(T, number, kit, mat('Raven black', (.06, .06, .07)), 1.1, mat('Raven beak', (.12, .12, .12)))


def goat(T, number, kit):
    coat = mat('Goat coat', (.78, .74, .66)); horn = mat('Goat horn', (.34, .30, .26))
    def horns(side, k):
        kit['rod']('Goat horn', (side * .12 * k, 2.0 * k, .88 * k), (side * .22 * k, 2.35 * k, .6 * k), .05 * k, horn, 3)
    return quadruped(kit, number, .55, coat, lambda k: kit['ell']('Goat tail', (0, 1.3 * k, -.8 * k), (.08 * k, .14 * k, .1 * k), coat),
                     ears=(.12, .08, .05), snout=coat, horns=horns)


STYLE_TREES['volcano'] = volcano_tree
STYLE_SCENERY['volcano'] = volcano_scenery
STYLE_PATCHES['volcano'] = volcano_patch
STYLE_BANKS['volcano'] = volcano_banks
SPECIES.update({
    'raven': dict(kind=4, build=raven),
    'goat': dict(kind=0, build=goat),
    'black lizard': dict(kind=3, build=lizard),
})
