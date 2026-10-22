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
import flora
import flora_temperate
import flora_dry
import flora_lush
import landmarks_city
import landmarks_wild
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
        obj['vegetation_motion'] = (obj.get('sway', T.sway), float(bool(obj.get('frond'))), *obj.get('crown', (0., 0.)))


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


MOSS = {'Mossy boulder': (.27, .40, .14), 'Lichen boulder': (.62, .62, .36), 'Garden boulder': (.34, .46, .18)}


def boulders(ctx, count, color, name='Boulder', squash=1.):
    rng = ctx['rng']
    key, build = flora.rock(name, color, MOSS.get(name))

    def place(x, y, z):
        s = (rng.uniform(1.0, 2.4), rng.uniform(.8, 2.2) * squash, rng.uniform(1, 2.2))
        flora.plant(name, key, build, x, y + .45 * s[1], z, s)
    scatter(ctx, count, 6.5, place)


# --- Canyon Dash: red rock desert ---------------------------------------------------------------
def desert_tree(T, x, y, z, s, rng):
    kind = rng.random()
    if kind < .55:
        # Saguaro: a fluted column with one or two upturned arms. The arm
        # bearings are still drawn so the placements after it stay put.
        arms = 1 + (rng.random() < .7)
        for _ in range(arms):
            rng.uniform(0, math.tau)
        flora.plant('Saguaro', ('saguaro', str(arms)), lambda v, a=arms: flora_dry.saguaro(v, a), x, y, z, s)
    elif kind < .8:
        flora.plant('Joshua tree', ('joshua',), flora_dry.joshua, x, y, z, s)
    else:
        flora.plant('Ironwood', ('ironwood',), flora_dry.ironwood, x, y, z, s)


def canyon_scenery(ctx):
    """The ranch, its water tower and windpumps and a covered wagon, from tools/landmarks_wild.py."""
    api, T, rng, ground = ctx['api'], ctx['T'], ctx['rng'], ctx['ground']
    lw = landmarks_wild

    def water_tower(x, y, z):
        lw.put('Water tower', ('water tower',), lw.water_tower, x, y - .05, z)

    def windpump(x, y, z):
        lw.put('Windpump', ('windpump',), lw.windpump, x, y - .05, z)

    def ranch(x, y, z):
        lw.put('Ranch house', ('ranch',), lw.ranch_house, x, y - .05, z)
        # Corral fence.
        for k in range(10):
            a, b = k * math.tau / 10, (k + 1) * math.tau / 10
            p = (x + 14 + math.cos(a) * 6, z + math.sin(a) * 6)
            q = (x + 14 + math.cos(b) * 6, z + math.sin(b) * 6)
            py, qy = ground(*p), ground(*q)
            lw.put('Corral post', ('fence post',), lw.fence_post, p[0], py - .05, p[1])
            for h in (.6, 1.2):
                lw.span('Corral rail', ('corral rail',), lw.corral_rail, (p[0], py + h, p[1]), (q[0], qy + h, q[1]))

    def wagon(x, y, z):
        lw.put('Covered wagon', ('wagon',), lw.wagon, x, y - .03, z)

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
        flora.plant('Tumbleweed', ('tumbleweed',), flora_dry.tumbleweed, x, y + .45, z, 1.1)
    elif number % 3 == 0:
        flora.plant('Desert scrub', ('sagebrush',), flora_dry.sagebrush, x, y - .03, z, 1.)
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
            key, build = flora_dry.rock('Red rock', tuple(kit['stone'].diffuse_color[:3]))
            flora.plant('Red rock', key, build, x, y + .2, z, (.6, .35, .5))


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
            flora.plant('Palm', ('palm', 'dry'), flora_dry.palm, x, y - .05, z, rng.uniform(.7, 1.0))
            for obj in set(bpy.context.scene.objects) - before:
                obj['vegetation_origin'] = (x, y - .05, z, 7 * .85)
                obj['vegetation_motion'] = (1.0, float(bool(obj.get('frond'))), *obj.get('crown', (0., 0.)))
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
def quadruped(kit, number, size, coat, tail, ears=(.13, .26, .09), snout=None, horns=None, belly=None):
    """A four-legged animal scaled from the deer proportions; the head and neck
    nod about the neck's base, the diagonal leg pairs step together."""
    ell, rod, black = kit['ell'], kit['rod'], kit['black']
    k = size
    under = belly.diffuse_color[:3] if belly is not None else None
    # Barrel body over a deeper chest, the rump slightly higher.
    ell('Body', (0, 1.2 * k, -.02 * k), (.37 * k, .43 * k, .80 * k), coat, under=under, lon=10, lat=6)
    ell('Chest', (0, 1.16 * k, .42 * k), (.30 * k, .40 * k, .34 * k), coat, under=under, lon=8, lat=4)
    rod('Neck', (0, 1.28 * k, .5 * k), (0, 1.76 * k, .8 * k), .19 * k, coat, 3, r2=.12 * k, sides=6)
    ell('Head', (0, 1.84 * k, .96 * k), (.19 * k, .2 * k, .29 * k), coat, 3, lon=8, lat=5)
    ell('Muzzle', (0, 1.74 * k, 1.2 * k), (.12 * k, .11 * k, .15 * k), snout or coat, 3, lon=6, lat=4)
    ell('Nose', (0, 1.77 * k, 1.33 * k), (.06 * k, .045 * k, .035 * k), black, 3, lon=5, lat=3)
    for side in (-1, 1):
        # Ears splay outwards and back.
        ell('Ear', (side * .19 * k, 2.03 * k, .86 * k), tuple(v * k for v in (ears[0] * .7, ears[1] * .75, ears[2] * 1.1)),
            coat, 3, lon=5, lat=3, tilt=(-side * .55, -.25))
        ell('Eye', (side * .16 * k, 1.9 * k, 1.07 * k), (.032 * k, .036 * k, .032 * k), black, 3, lon=5, lat=3)
        for front in (-1, 1):
            part = 1 if side * front > 0 else 2
            top = .095 if front > 0 else .11
            rod('Leg', (side * .22 * k, 1.02 * k, front * .52 * k), (side * .24 * k, .1 * k, front * .55 * k),
                top * k, coat, part, r2=.04 * k, sides=5)
            ell('Hoof', (side * .24 * k, .06 * k, front * .57 * k), (.055 * k, .06 * k, .08 * k), black, part, lon=5, lat=3)
        if horns:
            horns(side, k)
    if tail:
        tail(k)
    return k - 1


def coyote(T, number, kit):
    coat = mat('Coyote tawny coat', (.62, .47, .30))
    pale = mat('Coyote cream', (.83, .76, .62))
    return quadruped(kit, number, .58, coat, lambda k: kit['ell']('Bushy tail', (0, .9 * k, -1.0 * k),
                     (.15 * k, .18 * k, .48 * k), coat, tilt=(0, -.6)), ears=(.09, .24, .06), snout=pale, belly=pale)


def hare(kit, coat, ears=.42):
    """A crouched hare: a pear body, long ears laid back, big hind feet."""
    ell, black = kit['ell'], kit['black']
    under = kit['pale'].diffuse_color[:3]
    ell('Hare body', (0, .36, -.04), (.26, .32, .44), coat, under=under, tilt=(0, .25))
    ell('Hare haunch', (0, .3, -.22), (.27, .27, .27), coat, under=under, lon=8, lat=5)
    ell('Hare head', (0, .66, .34), (.17, .19, .22), coat, 3, lon=9, lat=5)
    ell('Hare muzzle', (0, .61, .5), (.09, .08, .08), coat, 3, lon=6, lat=4)
    ell('Hare nose', (0, .64, .57), (.03, .025, .02), black, 3, lon=5, lat=3)
    for side in (-1, 1):
        ell('Long ear', (side * .09, 1.0, .24), (.07, ears, .045), coat, 3, lon=6, lat=4, tilt=(-side * .25, .45))
        ell('Hare eye', (side * .14, .72, .44), (.034, .036, .03), black, 3, lon=5, lat=3)
        ell('Hare hind paw', (side * .2, .08, -.16), (.1, .08, .27), coat, 1, lon=6, lat=4)
        ell('Hare forepaw', (side * .12, .1, .3), (.06, .1, .07), coat, 2, lon=6, lat=3)
    ell('Hare tail', (0, .44, -.47), (.1, .1, .1), kit['pale'], lon=6, lat=4)
    return 0.


def jackrabbit(T, number, kit):
    return hare(kit, mat('Jackrabbit coat', (.60, .50, .36)))


def serpent(kit, scales, length=1.9, girth=.075, head=(.115, .085, .16), segments=16):
    """One continuous, tapering body along an S curve, so the slither bends it smoothly."""
    spine, radii = [], []
    for j in range(segments):
        t = j / (segments - 1)
        spine.append((math.sin(j * .65) * .13, girth, -length * .45 + t * length * .9))
        radii.append(girth * (.25 + .75 * min(1, t * 2.2)) * (1 - .15 * t))
    kit['tube']('Snake scales', spine, radii, scales)
    hx = math.sin((segments - 1) * .65) * .13
    kit['ell']('Snake head', (hx, girth * 1.15, length * .45 + head[2] * .6), head, scales, 3, lon=8, lat=5)
    for side in (-1, 1):
        kit['ell']('Snake eye', (hx + side * head[0] * .75, girth * 1.5, length * .45 + head[2] * .9),
                   (.022, .022, .022), kit['black'], 3, lon=5, lat=3)


def lizard(T, number, kit):
    scales = mat('Lizard scales', (.55, .42, .25))
    spine = [(math.sin(j * .7) * .05, .05, -.55 + j * .1) for j in range(10)]
    kit['tube']('Lizard body', spine, [.012, .02, .03, .04, .052, .062, .066, .064, .055, .045], scales)
    kit['ell']('Lizard head', (math.sin(9 * .7) * .05, .06, .44), (.065, .045, .1), scales, 3, lon=7, lat=4)
    for side in (-1, 1):
        for dz in (-.12, .22):
            kit['rod']('Lizard leg', (side * .04, .05, dz), (side * .13, .015, dz + .03), .018, scales, r2=.012, sides=5)
    return 0.


def bird(T, number, kit, plumage, size=1., beak=None, wing=None, belly=None):
    """A gliding bird: a streamlined body, feathered wings flapping about the shoulders, a fanned tail."""
    ell, rod = kit['ell'], kit['rod']
    k = size
    beak = beak or kit['orange']
    under = belly.diffuse_color[:3] if belly is not None else None
    ell('Bird body', (0, 0, -.02 * k), (.15 * k, .15 * k, .40 * k), plumage, under=under, lon=10, lat=6)
    ell('Bird head', (0, .07 * k, .33 * k), (.11 * k, .12 * k, .14 * k), plumage, lon=8, lat=5)
    rod('Bird beak', (0, .06 * k, .44 * k), (0, .03 * k, .58 * k), .04 * k, beak, r2=.008 * k, sides=5)
    for side in (-1, 1):
        ell('Bird eye', (side * .085 * k, .1 * k, .4 * k), (.022 * k, .024 * k, .02 * k), kit['black'], lon=5, lat=3)
    kit['wings'](k, wing or kit['wing'], .02, .04)
    ell('Bird tail', (0, .01 * k, -.46 * k), (.15 * k, .03 * k, .2 * k), wing or kit['wing'], lon=7, lat=3)
    return 0.


def vulture(T, number, kit):
    plumage = mat('Vulture plumage', (.18, .15, .14))
    return bird(T, number, kit, plumage, 1.5, mat('Vulture beak', (.75, .62, .45)), plumage)


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
    kind = rng.random()
    if kind < .5:
        # Maple in one of the forest's leaf colours; the second colour draw is
        # still taken so that every later placement stays where it was.
        tone = rng.randrange(len(AUTUMN_LEAVES))
        rng.randrange(len(AUTUMN_LEAVES))
        flora.plant('Maple', ('maple', str(tone)),
                    lambda v: flora_temperate.maple(v, AUTUMN_LEAVES[tone]), x, y, z, s)
    elif kind < .72:
        # Birch: a white, black-banded stem under a golden crown.
        flora.plant('Birch', ('birch', 'gold'), flora_temperate.birch, x, y, z, s)
    else:
        # Spruce: dark, drooping tiers.
        flora.plant('Spruce', ('spruce',), flora_temperate.spruce, x, y, z, s)


def autumn_scenery(ctx):
    """Log cabins, the sawmill, a fire lookout and pumpkin patches, from tools/landmarks_wild.py."""
    api, T, rng, ground = ctx['api'], ctx['T'], ctx['rng'], ctx['ground']
    lw = landmarks_wild

    def cabin(x, y, z):
        yaw = rng.uniform(-.4, .4)
        lw.put('Log cabin', ('log cabin',), lw.log_cabin, x, y - .05, z, yaw)
        c, s_ = math.cos(yaw), math.sin(yaw)
        wx, wz = x - c * 4.6 - s_ * .6, z + s_ * 4.6 - c * .6
        lw.put('Woodpile log', ('woodpile',), lw.woodpile, wx, ground(wx, wz) - .03, wz, yaw)

    def sawmill(x, y, z):
        lw.put('Sawmill shed', ('sawmill',), lw.sawmill, x, y - .05, z)
        lw.put('Log pile', ('log pile',), lw.log_pile, x, ground(x - 2.3, z + 9) - .08, z + 6)

    def lookout(x, y, z):
        lw.put('Lookout tower', ('lookout',), lw.lookout, x, y - .05, z)

    def pumpkins(x, y, z):
        for k in range(18):
            a, r = rng.uniform(0, math.tau), rng.uniform(0, 5)
            px, pz = x + math.cos(a) * r, z + math.sin(a) * r
            py = ground(px, pz); size = rng.uniform(.35, .7)
            lw.put('Pumpkin', ('pumpkin', k % 3), lambda v=k % 3: lw.pumpkin(v), px, py - .04 * size, pz, a, size)
        for k in range(3):
            a = k * 2.1
            px, pz = x + math.cos(a) * 7, z + math.sin(a) * 7
            lw.put('Hay bale', ('hay bale',), lw.hay_bale, px, ground(px, pz) - .06, pz, a)
        # A scarecrow keeps watch.
        lw.put('Scarecrow', ('scarecrow',), lw.scarecrow, x, y - .1, z)

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
    ft = flora_temperate
    if number % 7 == 0:
        # A mossy fallen trunk with a shelf fungus, lying towards its far end.
        end = (x + 2.6, ground(x + 2.6, z + .6) + .25, z + .6)
        ft.place('Fallen log', ('fallen-log',), ft.fallen_log, x, y + .25, z, 1, yaw=math.atan2(.6, 2.6),
                 pitch=-math.atan2(end[1] - y - .25, 2.67))
    elif number % 3 == 0:
        # Fly agarics: red caps with white warts on pale stems.
        for k in range(rng.randrange(2, 5)):
            a, b = x + rng.uniform(-.6, .6), z + rng.uniform(-.6, .6)
            yy = ground(a, b); h = rng.uniform(.12, .25)
            flora.plant('Toadstool', ('toadstool',), ft.toadstool, a, yy, b, h / .2)
    else:
        for j in range(rng.randrange(3, 7)):
            a, b = x + rng.uniform(-1.2, 1.2), z + rng.uniform(-1.2, 1.2)
            yy = ground(a, b)
            rx, rz = rng.uniform(.3, .6), rng.uniform(.3, .6)
            tone = rng.randrange(5)
            flora.plant('Leaf pile', ('leaf-pile', str(tone)), lambda v, t=tone: ft.leaf_pile(v, AUTUMN_LEAVES[t]),
                        a, yy, b, (rx, .4, rz))
        if number % 4 == 1:
            flora.plant('Fern frond', ('fern', 'rust'), ft.fern, x, y, z, 1)


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


def antlers(kit, side, k, tines=2, spread=.42):
    """A curved beam with forward-sweeping tines, part of the nodding head."""
    rod, dry = kit['rod'], kit['dry']
    base = (side * .1 * k, 2.03 * k, .86 * k)
    mid = (side * (.12 + spread * .55) * k, 2.45 * k, .78 * k)
    tip = (side * (.12 + spread) * k, 2.85 * k, .62 * k)
    rod('Stag antler', base, mid, .04 * k, dry, 3, r2=.032 * k, sides=4)
    rod('Stag antler', mid, tip, .032 * k, dry, 3, r2=.015 * k, sides=4)
    for j in range(tines):
        t = (j + 1) / (tines + 1)
        a = tuple(m + (e - m) * t * 1.6 for m, e in zip(base, mid)) if t < .5 else tuple(m + (e - m) * (t - .5) * 2 for m, e in zip(mid, tip))
        rod('Antler tine', a, (a[0] + side * .05 * k, a[1] + .22 * k, a[2] + .16 * k), .022 * k, dry, 3, r2=.01 * k, sides=4)


def deer(T, number, kit):
    coat = mat('Deer russet fur', (.48, .29, .15))
    def horns(side, k):
        if number % 3 == 0:
            antlers(kit, side, k)
    return quadruped(kit, number, 1., coat, lambda k: kit['ell']('Deer tail', (0, 1.4, -.8), (.1, .14, .2), kit['pale'],
                                                                   tilt=(0, -.5)),
                     horns=horns, belly=kit['pale'])


def fox(T, number, kit):
    coat = mat('Fox red coat', (.80, .36, .10))
    white = mat('Fox white', (.92, .90, .84))
    def brush(k):
        kit['ell']('Fox brush', (0, .9 * k, -1.12 * k), (.19 * k, .21 * k, .58 * k), coat, tilt=(0, -.45))
        kit['ell']('Fox brush tip', (0, .66 * k, -1.6 * k), (.12 * k, .13 * k, .15 * k), white, lon=6, lat=4)
    return quadruped(kit, number, .46, coat, brush, ears=(.09, .22, .05), snout=white, belly=white)


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
    ell('Duck body', (0, .3, 0), (.19, .17, .31), body, lon=10, lat=6, tilt=(0, -.12))
    ell('Duck tail', (0, .36, -.3), (.1, .06, .12), body, lon=6, lat=3, tilt=(0, -.5))
    ell('Duck collar', (0, .4, .2), (.1, .04, .1), mat('Duck collar', (.92, .92, .88)), 3, lon=6, lat=3)
    rod('Duck neck', (0, .36, .2), (0, .55, .25), .075, head, 3, r2=.06, sides=6)
    ell('Duck head', (0, .58, .28), (.085, .09, .11), head, 3, lon=8, lat=5)
    ell('Duck bill', (0, .555, .41), (.05, .022, .075), kit['orange'], 3, lon=6, lat=3)
    for side in (-1, 1):
        ell('Duck eye', (side * .07, .61, .32), (.016, .018, .016), kit['black'], 3, lon=5, lat=3)
        rod('Duck leg', (side * .08, .16, 0), (side * .08, .03, .02), .022, kit['orange'], 1 if side < 0 else 2, sides=5)
        ell('Duck foot', (side * .08, .02, .06), (.05, .012, .07), kit['orange'], 1 if side < 0 else 2, lon=5, lat=3)
    return 0.


def crow(T, number, kit):
    plumage = mat('Crow black', (.08, .08, .09))
    return bird(T, number, kit, plumage, 1.0, mat('Crow beak', (.15, .15, .15)), plumage)


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
    """Emergent kapoks, palms, banana plants and tree ferns from tools/flora_lush.py.

    The random draws of the former primitive trees are kept, so every later
    placement on the island stays where it was."""
    kind = rng.random()
    if kind < .35:
        flora.plant('Kapok trunk', ('lush', 'kapok'), flora_lush.kapok, x, y, z, s)
    elif kind < .6:
        flora.plant('Palm', ('lush', 'palm'), flora_lush.jungle_palm, x, y, z, s * rng.uniform(.8, 1.1))
    elif kind < .82:
        for k in range(6):
            rng.random()
        flora.plant('Banana stem', ('lush', 'banana'), flora_lush.banana, x, y, z, s)
    else:
        flora.plant('Tree fern trunk', ('lush', 'tree fern'), flora_lush.tree_fern, x, y, z, s)


def jungle_scenery(ctx):
    api, T, rng, ground = ctx['api'], ctx['T'], ctx['rng'], ctx['ground']
    L = landmarks_city

    def pyramid(x, y, z):
        # Six battered tiers, a stairway on every face, the shrine and its braziers on top.
        L.place('Temple pyramid', ('pyramid',), L.temple_pyramid, x, y, z)

    def head(x, y, z):
        yaw = rng.uniform(-1, 1)
        L.place('Stone head', ('stone head',), L.stone_head, x, y, z, yaw)

    def colonnade(x, y, z):
        yaw = rng.uniform(0, math.pi)
        c, s_ = math.cos(yaw), math.sin(yaw)
        for k in range(6):
            a = (k - 2.5) * 3.2
            h = rng.uniform(2.5, 5.5) if k % 3 else 5.5
            px, pz = x + c * a, z - s_ * a
            py = ground(px, pz)
            # Broken columns use the nearest of three stump heights.
            height = 5.5 if k % 3 == 0 else min((3.0, 4.0, 5.0), key=lambda v: abs(v - h))
            L.place('Ruined column', ('column', height), lambda h=height: L.column(h, h < 5.5), px, py - .05, pz,
                    flora.hash01(px, pz) * math.tau)
            if k % 3 == 0 and k < 5:
                L.place('Column lintel', ('lintel',), L.lintel, px + c * 1.6, py + 5.8, pz - s_ * 1.6, yaw)
            if rng.random() < .5:
                dx, dz = px + s_ * 1.5, pz + c * 1.5
                L.place('Fallen drum', ('drum',), L.drum, dx, ground(dx, dz) - .08, dz, flora.hash01(dx, dz) * math.tau)
        for k in range(4):
            px, pz = x + c * (k - 1.5) * 4.5 + s_ * 4, z - s_ * (k - 1.5) * 4.5 + c * 4
            L.place('Torch pole', ('torch',), L.torch, px, ground(px, pz) - .05, pz)

    def idol(x, y, z):
        L.place('Jade idol', ('jade idol',), L.jade_idol, x, y - .05, z)

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
    """Elephant ears, flowering shrubs and ground ferns, drawing the same random
    numbers as the former primitives so later placements stay put."""
    if number % 6 == 0:
        for k in range(4):
            rng.random()
        flora.plant('Elephant ear leaf', ('lush', 'elephant ears'), flora_lush.elephant_ears, x, y, z, 1.1)
    elif number % 4 == 0:
        for k in range(3):
            rng.uniform(-.7, .7); rng.random(); rng.uniform(-.7, .7); rng.randrange(3)
        flora.plant('Jungle shrub', ('lush', 'flowering shrub'), flora_lush.flowering_shrub, x, y, z, 1.25)
    else:
        fronds = rng.randrange(4, 8)
        for k in range(fronds):
            rng.random(); rng.uniform(.6, 1.1)
        flora.plant('Fern frond', ('lush', 'fern'), flora_lush.fern_clump, x, y, z, .75 + .07 * fronds)
        if number % 5 == 1:
            key, build = flora.rock('Mossy stone', tuple(kit['stone'].diffuse_color[:3]), (.22, .36, .14))
            flora.plant('Mossy stone', key, build, x + .8, y + .2, z, (.5, .3, .45))


def jungle_banks(ctx):
    T, water, ground, nearest = ctx['T'], ctx['water'], ctx['ground'], ctx['nearest']
    rng = random.Random(613 + T.index)
    x0, x1, z0, z1 = ctx['watershed'].bounds
    colour = tuple(T.colors['pebble'])
    L = landmarks_city
    for _ in range(800):
        x = rng.uniform(x0 - 2, x1 + 2); z = rng.uniform(z0 - 2, z1 + 2)
        wet, level, _ = water(x, z)
        if not -2.2 < wet < .5 or nearest(x, z) < 3 or not T.within(x, z, 124, 122) or not ctx['rail'].clear(x, z, 1):
            continue
        y = ground(x, z)
        if rng.random() < .5:
            size = rng.uniform(.25, .6)
            v = int(flora.hash01(x, z) * 3)
            L.place('Riverbank pebble', ('pebble', colour, v), lambda v=v: L.pebble_model(colour, v),
                    x, y + size * .1, z, flora.hash01(z, x) * math.tau, (size, size * .8, size))
        else:
            # The same draws as the old single blades, now one modeled reed tuft.
            blades = rng.randrange(4, 8)
            for blade in range(blades):
                rng.uniform(-.4, .4); rng.uniform(-.4, .4); rng.random()
            L.place('Waterside reed', ('reeds',), L.reed_clump, x, y - .03, z, flora.hash01(x, z) * math.tau,
                    .7 + blades * .08)


def parrot(T, number, kit):
    colors = [(.86, .14, .10), (.12, .40, .80), (.20, .62, .25), (.95, .78, .12)]
    plumage = mat('Parrot plumage ' + str(number % 4), colors[number % 4])
    wings = mat('Parrot wing ' + str(number % 4), colors[(number + 1) % 4])
    return bird(T, number, kit, plumage, .8, mat('Parrot beak', (.95, .88, .70)), wings)


def toucan(T, number, kit):
    black = mat('Toucan black', (.07, .07, .08))
    return bird(T, number, kit, black, .9, mat('Toucan bill', (.98, .55, .08)), black, mat('Toucan bib', (.98, .90, .55)))


def frog(T, number, kit):
    ell = kit['ell']
    skin = mat('Tree frog green', (.30, .72, .22))
    belly = mat('Tree frog belly', (.88, .86, .55))
    ell('Frog body', (0, .12, 0), (.15, .095, .19), skin, under=belly.diffuse_color[:3], lon=9, lat=5, tilt=(0, .2))
    ell('Frog head', (0, .16, .16), (.12, .075, .1), skin, 3, lon=8, lat=4)
    for side in (-1, 1):
        ell('Frog eye', (side * .075, .23, .19), (.042, .042, .042), mat('Frog eye red', (.85, .15, .08)), 3, lon=6, lat=4)
        ell('Frog pupil', (side * .09, .245, .215), (.016, .02, .012), kit['black'], 3, lon=5, lat=3)
        ell('Frog hind leg', (side * .15, .06, -.08), (.06, .045, .15), skin, 1, lon=6, lat=4, tilt=(0, .3))
        ell('Frog toes', (side * .17, .015, .04), (.05, .012, .05), mat('Frog toes', (.95, .55, .15)), 1, lon=5, lat=3)
        ell('Frog front leg', (side * .1, .045, .14), (.035, .045, .05), skin, 2, lon=5, lat=3)
    return 0.


def tapir(T, number, kit):
    coat = mat('Tapir grey', (.22, .21, .22))
    pale = mat('Tapir saddle', (.80, .78, .74))
    return quadruped(kit, number, .7, coat, lambda k: kit['ell']('Tapir saddle', (0, 1.25 * k, -.1 * k), (.42 * k, .4 * k, .5 * k), pale),
                     ears=(.08, .14, .06), snout=coat)


def jungle_snake(T, number, kit):
    serpent(kit, mat('Emerald scales', (.12, .50, .22)))
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
    """Cherries in blossom, cloud-pruned pines, Japanese maples and bamboo from
    tools/flora_lush.py. The former trees' random draws are kept, so every later
    placement on the island stays where it was."""
    kind = rng.random()
    if kind < .45:
        for k in range(2):
            rng.random(); rng.randrange(3)
        rng.randrange(3)
        flora.plant('Cherry trunk', ('lush', 'cherry'), flora_lush.cherry, x, y, z, s)
    elif kind < .65:
        rng.uniform(-.8, .8)
        for k in range(3):
            rng.uniform(-.8, .8); rng.uniform(-.8, .8)
        flora.plant('Pine trunk', ('lush', 'black pine'), flora_lush.black_pine, x, y, z, s)
    elif kind < .8:
        flora.plant('Tree trunk', ('lush', 'japanese maple'), flora_lush.japanese_maple, x, y, z, s)
    else:
        for k in range(7):
            rng.uniform(0, math.tau); rng.uniform(0, .9); rng.uniform(4.5, 7)
        flora.plant('Bamboo cane', ('lush', 'bamboo'), flora_lush.bamboo, x, y, z, s)


def sakura_scenery(ctx):
    api, T, rng, ground = ctx['api'], ctx['T'], ctx['rng'], ctx['ground']
    L = landmarks_city

    def pagoda(x, y, z):
        L.place('Pagoda', ('pagoda',), L.pagoda, x, y - .05, z)

    def torii(x, y, z, yaw=0., s=1.):
        # A rigid gate: it stands on its lower foot, the higher post sinks into the slope.
        c, s_ = math.cos(yaw), math.sin(yaw)
        feet = [ground(x + c * side * 2.2 * s, z - s_ * side * 2.2 * s) for side in (-1, 1)]
        L.place('Torii gate', ('torii',), L.torii, x, min(feet + [y]) - .05, z, yaw, s)

    def torii_path(x, y, z):
        yaw = rng.uniform(0, math.pi)
        c, s_ = math.cos(yaw), math.sin(yaw)
        for k in range(6):
            px, pz = x + s_ * (k - 2.5) * 3.6, z + c * (k - 2.5) * 3.6
            torii(px, ground(px, pz), pz, yaw, .8)

    def lantern(x, y, z):
        L.place('Stone lantern', ('stone lantern',), L.stone_lantern, x, y - .04, z, flora.hash01(x, z) * math.tau)

    def tea_house(x, y, z):
        yaw = rng.uniform(-.5, .5)
        L.place('Tea house', ('tea house',), L.tea_house, x, y - .05, z, yaw)

    def zen_garden(x, y, z):
        L.place('Zen garden', ('zen garden',), L.zen_garden, x, y - .03, z)

    def stalls(x, y, z):
        yaw = rng.uniform(0, math.pi)
        c, s_ = math.cos(yaw), math.sin(yaw)
        awnings = [(.86, .20, .14), (.94, .92, .88)]
        for k in range(4):
            px, pz = x + c * (k - 1.5) * 4, z - s_ * (k - 1.5) * 4
            awning = awnings[k % 2] if k % 2 == 0 else (.18, .36, .62)
            L.place('Festival stall', ('stall', awning), lambda a=awning: L.festival_stall(a),
                    px, ground(px, pz) - .04, pz, yaw)

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
    """Azaleas in three colours, moss cushions by garden stones and iris clumps,
    drawing the same random numbers as the former primitives."""
    if number % 4 == 0:
        for k in range(rng.randrange(2, 4)):
            ax, az = x + rng.uniform(-.8, .8), z + rng.uniform(-.8, .8)
            key, build = flora_lush.azalea(('pink', 'white', 'magenta')[rng.randrange(3)])
            flora.plant('Azalea', key, build, ax, ground(ax, az), az, 1.1)
    elif number % 4 == 1:
        flora.plant('Moss mound', ('lush', 'moss'), flora_lush.moss_mound, x, y, z, 1)
        key, build = flora.rock('Garden stone', tuple(kit['stone'].diffuse_color[:3]))
        flora.plant('Garden stone', key, build, x + .6, y + .2, z, (.4, .3, .35))
    else:
        for j in range(rng.randrange(3, 6)):
            a, b = x + rng.uniform(-1, 1), z + rng.uniform(-1, 1)
            h = rng.uniform(.3, .6)
            if j % 2 == 0:
                flora.plant('Iris leaves', ('lush', 'iris'), flora_lush.iris, a, ground(a, b), b, .55 + h)


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
        # A planked drum bridge spanning across the stream, along (nx, nz).
        landmarks_city.place('Arched bridge deck', ('drum bridge',), landmarks_city.arched_bridge, x, y, z,
                             math.atan2(nx, nz))
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
    ell('Crane body', (0, 1.0, 0), (.24, .26, .5), white, lon=10, lat=6, tilt=(0, -.15))
    ell('Crane tail', (0, 1.05, -.45), (.19, .13, .3), black, lon=8, lat=4, tilt=(0, -.35))
    rod('Crane neck', (0, 1.15, .35), (0, 1.5, .44), .08, white, 3, r2=.06, sides=6)
    rod('Crane neck', (0, 1.5, .44), (0, 1.76, .5), .06, black, 3, r2=.05, sides=6)
    ell('Crane head', (0, 1.8, .55), (.08, .085, .12), white, 3, lon=7, lat=4)
    ell('Crane crown', (0, 1.87, .53), (.05, .025, .05), red, 3, lon=6, lat=3)
    rod('Crane beak', (0, 1.79, .64), (0, 1.76, .82), .025, kit['orange'], 3, r2=.005, sides=5)
    for side in (-1, 1):
        rod('Crane leg', (side * .1, .82, 0), (side * .1, .45, .02), .028, black, 1 if side < 0 else 2, r2=.02, sides=5)
        rod('Crane leg', (side * .1, .45, .02), (side * .1, .03, .04), .02, black, 1 if side < 0 else 2, r2=.016, sides=5)
    return 0.


def sika(T, number, kit):
    coat = mat('Sika deer coat', (.58, .36, .20))
    spots = mat('Sika spots', (.92, .88, .78))
    k = .85
    pivot = quadruped(kit, number, k, coat, lambda kk: kit['ell']('Deer tail', (0, 1.3 * kk, -.8 * kk), (.12 * kk, .16 * kk, .2 * kk), spots))
    # White dapples on either flank.
    for j in range(8):
        side = 1 if j % 2 else -1
        zz = ((j // 2) - 1.5) * .26 * k
        kit['ell']('Sika spot', (side * .355 * k, (1.32 + .06 * ((j // 2) % 2)) * k, zz), (.012, .05, .05), spots, 0, lon=5, lat=3)
    return pivot


def white_eye(T, number, kit):
    olive = mat('White-eye olive', (.52, .62, .22))
    return bird(T, number, kit, olive, .6, mat('White-eye beak', (.2, .2, .2)), olive, mat('White-eye breast', (.85, .82, .45)))


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
    if rng.random() < .35:
        flora.plant('Palm', ('palm', 'dry'), flora_dry.palm, x, y, z, s * .85)
        return
    # A clipped street linden in a square concrete planter.
    flora.plant('Street linden', ('linden',), flora_dry.linden, x, y, z, s)


def harbor_scenery(ctx):
    api, T, rng, ground, nearest, water = ctx['api'], ctx['T'], ctx['rng'], ctx['ground'], ctx['nearest'], ctx['water']
    L = landmarks_city

    def building(x, y, z, w, d, floors, wall, yaw=0., lit=.5):
        # The old window and tank draws still decide the variant, so the city stays put.
        cols = max(1, int(w / 3.2)); rows = max(1, int(d / 3.2))
        for _ in range(floors * 2 * (cols + rows)):
            rng.random()
        variant = 0 if rng.random() < .4 else 1
        L.place('Building', ('building', floors, wall, variant),
                lambda: L.building(floors, L.FACADES[wall], variant), x, y - .05, z, yaw, (w / 10, 1, d / 10))

    def block(x, y, z):
        # A city block of three to five buildings round a court.
        for k in range(rng.randrange(3, 6)):
            bx, bz = x + rng.uniform(-9, 9), z + rng.uniform(-9, 9)
            if nearest(bx, bz) < 12:
                continue
            building(bx, ground(bx, bz), bz, rng.uniform(7, 12), rng.uniform(7, 12), rng.randrange(2, 7),
                     rng.randrange(len(L.FACADES)), rng.choice((0, math.pi / 2)) + rng.uniform(-.05, .05))

    def warehouse(x, y, z):
        yaw = rng.choice((0, math.pi / 2))
        wall = 0 if rng.random() < .5 else 2
        L.place('Warehouse', ('warehouse', wall), lambda: L.warehouse(L.FACADES[wall]), x, y - .05, z, yaw)

    def crane(x, y, z):
        # The legs reach below the quay, so the crane stands on its lowest foot.
        yaw = rng.uniform(0, math.pi)
        c, s_ = math.cos(yaw), math.sin(yaw)
        feet = [ground(x + c * side * 5 + s_ * end * 4, z - s_ * side * 5 + c * end * 4)
                for side in (-1, 1) for end in (-1, 1)]
        L.place('Crane portal', ('dock crane',), L.dock_crane, x, max(min(feet), y - 2.5), z, yaw)

    def container_stack(x, y, z):
        yaw = rng.choice((0, math.pi / 2))
        c, s_ = math.cos(yaw), math.sin(yaw)
        for row in range(3):
            for col in range(2):
                for level in range(rng.randrange(1, 4)):
                    px, pz = x + c * (row - 1) * 6.4 + s_ * (col - .5) * 2.6, z - s_ * (row - 1) * 6.4 + c * (col - .5) * 2.6
                    colour = rng.randrange(6)
                    L.place('Shipping container', ('container', colour), lambda k=colour: L.container(L.CONTAINERS[k]),
                            px, y - .02 + level * 2.6, pz, yaw)

    def lighthouse(x, y, z):
        L.place('Lighthouse tower', ('lighthouse',), L.lighthouse, x, y - .05, z)

    def ship(x, z, yaw):
        # Deck at the same height as before, the waterline below it.
        L.place('Ship hull', ('ship',), L.ship, x, -6.85, z, yaw)

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
                # The arm reaches out over the road.
                L.place('Street lamp post', ('street lamp',), L.street_lamp, x, y - .03, z,
                        math.atan2(-p[4] * side, p[3] * side))

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
        flora.plant('Planter shrub', ('planter shrub',), flora_dry.planter_shrub, x, y, z, 1.)


def gull(T, number, kit):
    return bird(T, number, kit, mat('Gull white', (.92, .93, .92)), .9, mat('Gull beak', (.95, .78, .2)),
                mat('Gull wing grey', (.62, .65, .68)))


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
        # Scots pine: a tall trunk, flaking orange above, under flat clouds of
        # needles; its lean comes from the same draw as before, in three steps.
        lean = (-.3, 0., .3)[min(2, int((rng.uniform(-.4, .4) + .4) / .8 * 3))]
        flora.plant('Scots pine', ('scots-pine', '%+.1f' % lean),
                    lambda v: flora_temperate.scots_pine(v, lean), x, y, z, s)
    elif kind < .8:
        flora.plant('Birch', ('birch', 'highland'),
                    lambda v: flora_temperate.birch(v, ((.17, .27, .07), (.56, .67, .24))), x, y, z, s * .85)
    else:
        # Gorse: a prickly dark bush in yellow flower.
        flora.plant('Gorse bush', ('gorse',), flora_temperate.gorse, x, y, z, s)


def highland_scenery(ctx):
    """The castle, crofts, a stone circle, a phone box and dry-stone walls, from tools/landmarks_wild.py."""
    api, T, rng, ground, nearest = ctx['api'], ctx['T'], ctx['rng'], ctx['ground'], ctx['nearest']
    lw = landmarks_wild

    def castle(x, y, z):
        # A ruined keep, a curtain wall with a gate and corner towers.
        lw.put('Castle keep', ('keep',), lw.keep, x, y - .05, z)
        for k in range(4):
            a = k * math.pi / 2 + math.pi / 4
            tx, tz = x + math.cos(a) * 16, z + math.sin(a) * 16
            ty = ground(tx, tz)
            height = 10 - (k % 2) * 3
            lw.put('Corner tower', ('tower', height), lambda h=height: lw.tower(h), tx, ty, tz, a)
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
                lw.put('Curtain wall', ('curtain', h), lambda h=h: lw.curtain(h), mx, ground(mx, mz), mz,
                       math.atan2(-(bz - az), bx - ax), ((math.hypot(bx - ax, bz - az) + .2) / 3.97, 1, 1))

    def croft(x, y, z):
        yaw = rng.uniform(-.6, .6)
        thatched = rng.random() < .5
        lw.put('Croft', ('croft', thatched), lambda t=thatched: lw.croft(t), x, y - .05, z, yaw)

    def stones(x, y, z):
        # A circle of standing stones round a fallen altar.
        for k in range(9):
            a = k * math.tau / 9
            px, pz = x + math.cos(a) * 7, z + math.sin(a) * 7
            h = rng.uniform(2.2, 3.6)
            lw.put('Standing stone', ('menhir', k % 3), lambda v=k % 3: lw.standing_stone(v), px, ground(px, pz), pz,
                   -a + rng.uniform(-.2, .2), (1, h / 3, 1))
        lw.put('Altar stone', ('altar',), lw.altar, x, y - .05, z, rng.uniform(0, math.pi))

    def phone_box(x, y, z):
        lw.put('Phone box', ('phone box',), lw.phone_box, x, y - .03, z)

    def sheep_pen(x, y, z):
        for k in range(12):
            a, b = k * math.tau / 12, (k + 1) * math.tau / 12
            ax, az = x + math.cos(a) * 6, z + math.sin(a) * 6
            bx, bz = x + math.cos(b) * 6, z + math.sin(b) * 6
            if k == 0:
                continue
            mx, mz = (ax + bx) / 2, (az + bz) / 2
            lw.put('Pen wall', ('dry stone',), lw.drystone, mx, ground(mx, mz) - .05, mz, math.atan2(-(bz - az), bx - ax),
                   ((math.hypot(bx - ax, bz - az) + .2) / 3.2, .9, .6 / .7))

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
                lw.put('Dry stone wall', ('dry stone',), lw.drystone, mx, ground(mx, mz) - .05, mz, -a, (3.15 / 3.2, 1, 1))
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
    ft = flora_temperate
    if number % 3 == 0:
        # Cushions of heather in flower.
        for k in range(rng.randrange(3, 7)):
            a, b = x + rng.uniform(-1.2, 1.2), z + rng.uniform(-1.2, 1.2)
            flora.plant('Heather clump', ('heather',), ft.heather, a, ground(a, b), b, 1)
    elif number % 3 == 1:
        # A bracken clump; the old per-frond draws are still taken.
        for k in range(rng.randrange(4, 7)):
            rng.random()
        flora.plant('Bracken frond', ('fern', 'bracken'),
                    lambda v: ft.fern(v, ((.44, .34, .13), (.74, .60, .28))), x, y, z, 1.1)
    else:
        # A moor stone, the odd one crusted with lichen.
        key, build = flora.rock('Moor stone', tuple(kit['stone'].diffuse_color[:3]),
                                (.72, .74, .52) if number % 2 else None)
        flora.plant('Moor stone', key, build, x, y + .25, z, (.7, .4, .6))


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
    # A fleece of clustered tufts over a woolly barrel.
    ell('Fleece', (0, 1.1 * k, 0), (.5 * k, .5 * k, .8 * k), wool, lon=10, lat=6)
    for j, (fx, fy, fz, r) in enumerate([(-.28, 1.3, .38, .3), (.28, 1.3, .38, .3), (-.3, 1.28, -.36, .32),
                                          (.3, 1.28, -.36, .32), (0, 1.52, 0, .34), (0, 1.38, .62, .26)]):
        ell('Fleece tuft', (fx * k, fy * k, fz * k), (r * k, r * .85 * k, r * k), wool, 3 if j == 5 else 0, lon=7, lat=4)
    rod('Neck', (0, 1.25 * k, .6 * k), (0, 1.5 * k, .85 * k), .16 * k, wool, 3, r2=.12 * k, sides=6)
    ell('Head', (0, 1.5 * k, 1.0 * k), (.16 * k, .19 * k, .28 * k), face, 3, lon=8, lat=5, tilt=(0, .35))
    for side in (-1, 1):
        ell('Ear', (side * .2 * k, 1.62 * k, .9 * k), (.13 * k, .045 * k, .065 * k), face, 3, lon=6, lat=3, tilt=(side * .3, 0))
        ell('Eye', (side * .11 * k, 1.58 * k, 1.08 * k), (.03 * k, .03 * k, .03 * k), mat('Sheep eye', (.75, .62, .25)), 3, lon=5, lat=3)
        for front in (-1, 1):
            part = 1 if side * front > 0 else 2
            rod('Leg', (side * .24 * k, .82 * k, front * .48 * k), (side * .25 * k, .06 * k, front * .5 * k), .07 * k, face, part,
                r2=.05 * k, sides=5)
    return k - 1


def highland_cow(T, number, kit):
    coat = mat('Highland coat', (.72, .38, .16)); horn = mat('Highland horn', (.88, .82, .66))
    shag = mat('Highland shag', (.80, .46, .20))
    def horns(side, k):
        kit['rod']('Horn', (side * .16 * k, 1.97 * k, .9 * k), (side * .62 * k, 2.12 * k, .96 * k), .075 * k, horn, 3, r2=.05 * k, sides=5)
        kit['rod']('Horn tip', (side * .62 * k, 2.12 * k, .96 * k), (side * .8 * k, 2.5 * k, .88 * k), .05 * k, horn, 3, r2=.015 * k, sides=4)
    def tail(k):
        kit['rod']('Tail', (0, 1.5 * k, -.78 * k), (0, .7 * k, -.9 * k), .035 * k, coat, r2=.03 * k, sides=5)
        kit['ell']('Tail tuft', (0, .62 * k, -.9 * k), (.08 * k, .15 * k, .08 * k), shag, lon=6, lat=4)
    pivot = quadruped(kit, number, .9, coat, tail, ears=(.14, .1, .08), snout=mat('Cow nose', (.25, .18, .16)), horns=horns)
    k = .9
    # A long shaggy fringe falls over the eyes.
    kit['ell']('Shaggy fringe', (0, 1.98 * k, 1.0 * k), (.24 * k, .14 * k, .2 * k), shag, 3, lon=8, lat=4, tilt=(0, .4))
    return pivot


def red_deer(T, number, kit):
    coat = mat('Red deer coat', (.52, .30, .18))
    def horns(side, k):
        antlers(kit, side, k, tines=3, spread=.5)
    return quadruped(kit, number, 1.05, coat, lambda k: kit['ell']('Deer tail', (0, 1.38 * k, -.8 * k), (.09 * k, .13 * k, .18 * k),
                                                                     kit['pale'], tilt=(0, -.5)),
                     horns=horns if number % 2 == 0 else None, belly=kit['pale'])


def mountain_hare(T, number, kit):
    return hare(kit, mat('Mountain hare coat', (.62, .58, .52)), .36)


def eagle(T, number, kit):
    brown = mat('Eagle brown', (.30, .20, .12))
    return bird(T, number, kit, brown, 1.6, mat('Eagle beak', (.92, .74, .20)), brown)


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
        flora.plant('Palm', ('palm', 'dry'), flora_dry.palm, x, y, z, s * .9)
    elif kind < .65:
        # A charred snag left by an old flow; its two branch draws are kept.
        rng.random(); rng.random()
        flora.plant('Charred trunk', ('charred',), flora_dry.charred, x, y, z, s)
    else:
        # A tough ohia with red pompom blossoms.
        flora.plant('Ohia', ('ohia',), flora_dry.ohia, x, y, z, s * .8)


def volcano_scenery(ctx):
    """Basalt columns, the observatory, a research station, warning signs and vents,
    from tools/landmarks_wild.py."""
    api, T, rng, ground = ctx['api'], ctx['T'], ctx['rng'], ctx['ground']
    lw = landmarks_wild

    def columns(x, y, z):
        for k in range(14):
            a, r = rng.uniform(0, math.tau), rng.uniform(0, 4)
            px, pz = x + math.cos(a) * r, z + math.sin(a) * r
            h = rng.uniform(1.5, 6.5)
            lw.put('Basalt column', ('basalt', k % 3), lambda v=k % 3: lw.basalt_column(v), px, ground(px, pz) - .3, pz,
                   a, (1, h / 4, 1))

    def observatory(x, y, z):
        lw.put('Observatory', ('observatory',), lw.observatory, x, y - .05, z)

    def station(x, y, z):
        yaw = rng.uniform(0, math.pi)
        c, s_ = math.cos(yaw), math.sin(yaw)
        for k in range(2):
            px, pz = x + c * (k - .5) * 7, z - s_ * (k - .5) * 7
            lw.put('Research hut', ('research hut',), lw.research_hut, px, ground(px, pz) - .05, pz, yaw)
        mx, mz = x + s_ * 5, z + c * 5
        lw.put('Antenna mast', ('mast',), lw.mast, mx, ground(mx, mz) - .05, mz, yaw)

    def warning(x, y, z):
        lw.put('Warning sign', ('warning sign',), lw.warning_sign, x, y - .05, z)

    def vent(x, y, z):
        lw.put('Vent rock', ('fumarole',), lw.fumarole, x, y, z, x * .37)

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
        key, build = flora_dry.rock('Lava rock', tuple(kit['stone'].diffuse_color[:3]))
        for k in range(rng.randrange(3, 6)):
            a, b = x + rng.uniform(-1, 1), z + rng.uniform(-1, 1)
            flora.plant('Lava rock', key, build, a, ground(a, b) + .15, b, (.35, .25, .3))
    else:
        # A fern rosette; the old per-frond draws still size it.
        fronds = rng.randrange(3, 5)
        for k in range(fronds):
            rng.random()
        flora.plant('Fern frond', ('fern',), flora_dry.fern_clump, x, y, z, .75 + .15 * fronds)


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
    black = mat('Raven black', (.06, .06, .07))
    return bird(T, number, kit, black, 1.1, mat('Raven beak', (.12, .12, .12)), black)


def goat(T, number, kit):
    coat = mat('Goat coat', (.78, .74, .66)); horn = mat('Goat horn', (.34, .30, .26))
    def horns(side, k):
        kit['rod']('Goat horn', (side * .1 * k, 2.02 * k, .88 * k), (side * .18 * k, 2.32 * k, .7 * k), .055 * k, horn, 3, r2=.04 * k, sides=5)
        kit['rod']('Goat horn', (side * .18 * k, 2.32 * k, .7 * k), (side * .26 * k, 2.3 * k, .45 * k), .04 * k, horn, 3, r2=.015 * k, sides=5)
    def tail(k):
        kit['ell']('Goat tail', (0, 1.42 * k, -.8 * k), (.06 * k, .14 * k, .08 * k), coat, tilt=(0, -.6), lon=6, lat=4)
        kit['ell']('Goat beard', (0, 1.56 * k, 1.18 * k), (.05 * k, .14 * k, .05 * k), coat, 3, lon=5, lat=3)
    return quadruped(kit, number, .55, coat, tail, ears=(.12, .08, .05), snout=coat, horns=horns)


STYLE_TREES['volcano'] = volcano_tree
STYLE_SCENERY['volcano'] = volcano_scenery
STYLE_PATCHES['volcano'] = volcano_patch
STYLE_BANKS['volcano'] = volcano_banks
SPECIES.update({
    'raven': dict(kind=4, build=raven),
    'goat': dict(kind=0, build=goat),
    'black lizard': dict(kind=3, build=lizard),
})
