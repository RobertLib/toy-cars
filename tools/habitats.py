"""Deterministic habitat dressing and articulated wildlife for Blender exports.

Animal vertices carry species/part and a locally verified ground plane. Runtime
animation uses bounded paths, keeping animals clear of the rally and obstacles.
"""
import math
import bmesh
import random
import flora, flora_temperate


# --- Smooth wildlife primitives -----------------------------------------------------------------
# Animals are small but close to the cameras: they are made of smooth-shaded,
# properly tessellated ellipsoids and tapered capsules rather than faceted
# icospheres, with a soft top-lit gradient baked into the 'Riverbed' colour
# layer that export() reads per vertex. Every surface is closed and wound
# outwards, so no vertex is shared by opposite faces (which has no normal).

def _object(bpy, name, points, faces, material, origin=None):
    """A smooth mesh from game-axis points; faces are turned to face away from origin
    (a point, or a function giving the inside point for a face centroid)."""
    verts = [(p[0], -p[2], p[1]) for p in points]
    fixed = []
    for f in faces:
        a, b, c = (verts[f[0]], verts[f[1]], verts[f[2]])
        n = ((b[1]-a[1])*(c[2]-a[2])-(b[2]-a[2])*(c[1]-a[1]), (b[2]-a[2])*(c[0]-a[0])-(b[0]-a[0])*(c[2]-a[2]),
             (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))
        centre = [sum(verts[i][k] for i in f) / len(f) for k in range(3)]
        inner = origin(centre) if callable(origin) else origin
        out = [centre[k] - inner[k] for k in range(3)]
        fixed.append(tuple(f) if sum(n[k]*out[k] for k in range(3)) >= 0 else tuple(reversed(f)))
    data = bpy.data.meshes.new(name)
    data.from_pydata(verts, [], fixed)
    data.update()
    data.polygons.foreach_set('use_smooth', [True] * len(data.polygons))
    data.materials.append(material)
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    return obj


def shade(obj, material, depth=.28, under=None):
    """Bake a top-lit gradient (and optionally a paler underside) into 'Riverbed'."""
    data = obj.data
    base = material.diffuse_color[:3]
    layer = data.color_attributes.new('Riverbed', 'FLOAT_COLOR', 'POINT')
    for i, v in enumerate(data.vertices):
        up = v.normal.z * .5 + .5
        c = [b * (1 - depth * (1 - up)) for b in base]
        if under is not None and v.normal.z < -.25:
            t = min(1., (-v.normal.z - .25) * 2.2)
            c = [x + (u - x) * t for x, u in zip(c, under)]
        layer.data[i].color = (*c, 1)
    return obj


def detail(radius):
    """Longitude and latitude counts for an ellipsoid of this size."""
    lon = max(5, min(12, int(5 + 12 * radius)))
    return lon, max(3, lon // 2 + 1)


def ellipsoid(bpy, name, centre, radii, material, lon=None, lat=None, tilt=(0., 0.)):
    """An ellipsoid in game axes; tilt rotates it about z then x (radians)."""
    if lon is None:
        lon, lat = detail(max(radii))
    cz, sz = math.cos(tilt[0]), math.sin(tilt[0])
    cx, sx = math.cos(tilt[1]), math.sin(tilt[1])

    def place(p):
        x, y, z = p
        x, y = x * cz - y * sz, x * sz + y * cz
        y, z = y * cx - z * sx, y * sx + z * cx
        return (centre[0] + x, centre[1] + y, centre[2] + z)
    points = [place((0, -radii[1], 0))]
    for j in range(1, lat):
        ph = -math.pi / 2 + math.pi * j / lat
        for k in range(lon):
            th = math.tau * k / lon
            points.append(place((radii[0] * math.cos(ph) * math.cos(th), radii[1] * math.sin(ph),
                                 radii[2] * math.cos(ph) * math.sin(th))))
    points.append(place((0, radii[1], 0)))
    return _object(bpy, name, points, _rings(lon, lat - 1), material, (centre[0], -centre[2], centre[1]))


def _rings(n, count):
    """Faces of `count` stacked loops of n points between a first and a last pole."""
    faces = [(0, 1 + (k + 1) % n, 1 + k) for k in range(n)]
    for j in range(count - 1):
        a = 1 + j * n
        faces += [(a + k, a + (k + 1) % n, a + n + (k + 1) % n, a + n + k) for k in range(n)]
    top = 1 + count * n
    a = 1 + (count - 1) * n
    faces += [(a + k, a + (k + 1) % n, top) for k in range(n)]
    return faces


def capsule(bpy, Vector, name, a, b, ra, material, rb=None, sides=6):
    """A tapered capsule from a to b with rounded ends."""
    rb = ra if rb is None else rb
    a, b = Vector(a), Vector(b)
    axis = (b - a).normalized()
    u = axis.cross(Vector((0, 1, 0)))
    if u.length < .01:
        u = axis.cross(Vector((1, 0, 0)))
    u.normalize()
    v = axis.cross(u)
    loops = []
    for c, r, h in ((a, ra, -.75), (a, ra, 0), (b, rb, 0), (b, rb, .75)):
        k = (1 - h * h) ** .5
        loops.append([tuple(c + axis * h * r + r * k * (u * math.cos(i * math.tau / sides) + v * math.sin(i * math.tau / sides)))
                      for i in range(sides)])
    points = [tuple(a - axis * ra)] + [p for loop in loops for p in loop] + [tuple(b + axis * rb)]
    ga, gb = a, b

    def inside(centre):
        # The nearest point of the axis, in Blender axes.
        q = Vector((centre[0], centre[2], -centre[1]))
        t = max(0., min(1., (q - ga).dot(gb - ga) / max((gb - ga).length_squared, 1e-12)))
        p = ga + (gb - ga) * t
        return (p.x, -p.z, p.y)
    return _object(bpy, name, points, _rings(sides, len(loops)), material, inside)


def tube(bpy, Vector, name, spine, radii, material, sides=7):
    """A closed tube along a polyline of game-axis points with a radius per point."""
    spine = [Vector(p) for p in spine]
    loops = []
    for i, (p, r) in enumerate(zip(spine, radii)):
        d = (spine[min(i + 1, len(spine) - 1)] - spine[max(i - 1, 0)]).normalized()
        u = d.cross(Vector((0, 1, 0)))
        if u.length < .01:
            u = d.cross(Vector((1, 0, 0)))
        u.normalize()
        v = d.cross(u)
        loops.append([tuple(p + r * (u * math.cos(k * math.tau / sides) + v * math.sin(k * math.tau / sides) * .8))
                      for k in range(sides)])
    head = spine[0] - (spine[1] - spine[0]).normalized() * radii[0]
    tail = spine[-1] + (spine[-1] - spine[-2]).normalized() * radii[-1]
    points = [tuple(head)] + [p for loop in loops for p in loop] + [tuple(tail)]

    def inside(centre):
        q = Vector((centre[0], centre[2], -centre[1]))
        best = min(spine, key=lambda s: (s - q).length_squared)
        return (best.x, -best.z, best.y)
    return _object(bpy, name, points, _rings(sides, len(loops)), material, inside)


def feathers(bpy, name, root, span, chord, material, side, lift=.06, thickness=.018, fingers=4):
    """A thin, closed wing with a feathered trailing edge, rooted near x = side*.12."""
    rx, ry, rz = root
    outline = [(0, 0, chord * .45), (span * .45, lift, chord * .42), (span * .85, lift * .4, chord * .1),
               (span, -lift * .3, -chord * .35)]
    # Primary feathers fan out along the trailing edge back to the body.
    for f in range(fingers):
        t = 1 - (f + 1) / (fingers + 1)
        outline.append((span * t + span * .05, 0, -chord * (.55 if f % 2 == 0 else .38)))
    outline.append((0, 0, -chord * .35))
    top = [(rx + side * x, ry + y + thickness, rz + z) for x, y, z in outline]
    bottom = [(rx + side * x, ry + y - thickness, rz + z) for x, y, z in outline]
    n = len(outline)
    points = top + bottom
    faces = [tuple(range(n)), tuple(range(n, 2 * n))]
    faces += [(k, (k + 1) % n, n + (k + 1) % n, n + k) for k in range(n)]
    data = bpy.data.meshes.new(name)
    data.from_pydata([(p[0], -p[2], p[1]) for p in points], [], faces)
    data.validate()
    data.update()
    # Recalculate outward winding with bmesh, then shade flat: a thin plate's
    # smooth normals would bend across its rim.
    import bmesh as _bm
    bm = _bm.new()
    bm.from_mesh(data)
    _bm.ops.triangulate(bm, faces=bm.faces)
    _bm.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(data)
    bm.free()
    data.materials.append(material)
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    return obj


def build(api, T, path, ground, water, obstacles):
    theme = T.index
    C = T.colors
    bpy, Vector = api['bpy'], api['Vector']
    ico, beam, mesh, mat = (api[k] for k in ('ico', 'beam', 'mesh', 'mat'))
    # Direct mesh construction avoids a scene-wide dependency update for each
    # small flower or limb, keeping the full authoring pass practical.
    def ico(name, p, scale, material, sub=1):
        bm=bmesh.new()
        bmesh.ops.create_icosphere(bm,subdivisions=sub,radius=1)
        for v in bm.verts:
            v.co.x*=scale[0];v.co.y*=scale[2];v.co.z*=scale[1]
        data=bpy.data.meshes.new(name);bm.to_mesh(data);bm.free()
        obj=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(obj)
        obj.location=api['xyz'](p);data.materials.append(material)
        return obj

    def beam(name, a, b, radius, material, vertices=8):
        a,b=Vector(a),Vector(b);direction=(b-a).normalized()
        u=direction.cross(Vector((0,1,0)))
        if u.length<.01:u=direction.cross(Vector((1,0,0)))
        u.normalize();v=direction.cross(u)
        points=[tuple(p+radius*(u*math.cos(j*math.tau/vertices)+v*math.sin(j*math.tau/vertices)))
                for p in (a,b) for j in range(vertices)]
        faces=[tuple(reversed(range(vertices))),tuple(range(vertices,vertices*2))]
        faces.extend((j,(j+1)%vertices,(j+1)%vertices+vertices,j+vertices) for j in range(vertices))
        return mesh(name,points,faces,material)

    rng = random.Random(4170 + theme)
    bpy.context.view_layer.update()
    bounds = []
    for obj in obstacles:
        if obj.type != 'MESH':
            continue
        corners = [api['eng'](obj.matrix_world @ Vector(p)) for p in obj.bound_box]
        bounds.append((min(p[0] for p in corners), max(p[0] for p in corners),
                       min(p[2] for p in corners), max(p[2] for p in corners)))

    def clear(x, z, radius):
        return (T.within(x, z, 116, 114) and
                all(math.hypot(x-p[0], z-p[2]) > p[5]+radius+2 for p in path) and
                all(x+radius < a or x-radius > b or z+radius < c or z-radius > d
                    for a, b, c, d in bounds))

    foliage = mat('Habitat foliage '+str(theme), C['foliage'])
    dry = mat('Habitat dry stems '+str(theme), C['dry'])
    flower = mat('Habitat flowers '+str(theme), C['flower'])
    bark = mat('Weathered fallen wood', (.35,.29,.22))
    stone = mat('Habitat stone '+str(theme), C['stone'])
    dressing_start = set(bpy.context.scene.objects)
    # Irregular patches leave open spaces and preserve sight lines at corners.
    patches = 0
    for _ in range(1800 if T.legacy else 9000):
        x, z = rng.uniform(-T.sx(108),T.sx(108)), rng.uniform(-T.sz(106),T.sz(106))
        if not clear(x,z,1.6) or water(x,z)[0] > -2.5:
            continue
        if math.sin(x*.12+z*.09)*math.cos(z*.16) < .12:
            continue
        y = ground(x,z)
        if not T.legacy:
            api['biomes'].patch(T, patches, x, y, z, rng, ground, dict(ico=ico, beam=beam, mesh=mesh, mat=mat,
                                foliage=foliage, dry=dry, flower=flower, bark=bark, stone=stone))
        elif patches % 11 == 0:
            flora.plant('Driftwood' if theme == 1 else 'Fallen branch', ('fallen-log',),
                        flora_temperate.fallen_log, x, y - .06, z, .85)
        elif theme == 2:
            key, build = flora.rock('Alpine scree', C['stone'], budget=90)
            flora.plant('Alpine scree', key, build, x, y + .1, z, (.85, .45, .7))
            for j in range(3):
                dx,dz=rng.uniform(-.8,.8),rng.uniform(-.8,.8)
                flora.plant('Dwarf mountain pine', ('dwarf pine',),
                            lambda v: flora.fir(v, snow=(.93, .96, .98), tiers=3, budget=90),
                            x+dx, ground(x+dx,z+dz)-.05, z+dz, .2)
        else:
            for j in range(rng.randrange(5,10)):
                dx,dz=rng.uniform(-1.1,1.1),rng.uniform(-1.1,1.1)
                a,b=x+dx,z+dz; h=rng.uniform(.35,.85); yy=ground(a,b)
                for k in range(3):
                    angle=k*2.1+j
                    mesh('Meadow grass' if theme == 0 else 'Dune marram',
                         [(a-.07,yy,b),(a+.07,yy,b),(a+math.cos(angle)*.25,yy+h,b+math.sin(angle)*.25)],
                         [(0,1,2)],foliage if j%3 else dry)
                if j%3 == 0:
                    ico('Wildflower' if theme == 0 else 'Dune seed head',(a,yy+h,b),(.12,.09,.12),flower)
            if patches%4 == 0:
                flora.plant('Hedgerow shrub' if theme == 0 else 'Coastal scrub', ('shrub',), flora.shrub,
                            x, y - .05, z, 1.7)
        patches += 1
        if patches == T.patches:
            break

    bpy.context.view_layer.update()
    for obj in set(bpy.context.scene.objects)-dressing_start:
        corners=[api['eng'](obj.matrix_world @ Vector(p)) for p in obj.bound_box]
        bounds.append((min(p[0] for p in corners),max(p[0] for p in corners),
                       min(p[2] for p in corners),max(p[2] for p in corners)))

    # Species IDs: deer, hare, goose, snake, flying bird. Part 0 is torso,
    # 1/2 legs or wings, 3 head/neck; snake vertices form a continuous body.
    fur=mat('Deer russet fur',(.48,.29,.15))
    pale=mat('Wildlife pale fur '+str(theme),(.85,.83,.73) if theme != 2 else (.91,.94,.93))
    black=mat('Wildlife eyes and hooves',(.055,.065,.06))
    orange=mat('Goose bill',(.87,.48,.12))
    snake=mat('Snake olive scales',(.32,.36,.16))
    hare=mat('Hare coat '+str(theme),(.49,.42,.30) if theme != 2 else (.88,.92,.92))
    wing=mat('Bird wing tips',(.23,.27,.29))
    plumage=mat('Bird plumage '+str(theme),[(.25,.29,.26),(.90,.91,.86),(.16,.20,.22)][theme] if T.legacy else C.get('plumage',(.22,.24,.24)))
    names=['deer','hare','goose','snake','bird']
    if T.legacy:
        wanted=([0]*5+[1]*5+[2]*6+[3]*3+[4]*9 if theme == 0 else
                [3]*4+[4]*16 if theme == 1 else [0]*4+[1]*6+[4]*7)
        wanted=[(k,names[k]) for k in wanted]
    else:
        species_table=api['biomes'].SPECIES
        wanted=[(species_table[n]['kind'],n) for n in T.wildlife]
    occupied=[]; counts={}
    for number,(species,label) in enumerate(wanted):
        flying=species == 4
        found=False
        for _ in range(16000):
            x,z=rng.uniform(-T.sx(108),T.sx(108)),rng.uniform(-T.sz(106),T.sz(106))
            if flying and not T.within(x,z,98,96):continue
            if not flying and (not clear(x,z,3.0) or water(x,z)[0]>-3.2):
                continue
            near_water = species == 2 if T.legacy else species_table[label].get('water', False)
            if near_water and water(x,z)[0] < -16:
                continue
            if any(math.hypot(x-a,z-b)<6 for a,b in occupied):
                continue
            y=ground(x,z)
            sx=(ground(x+.5,z)-ground(x-.5,z))
            sz=(ground(x,z+.5)-ground(x,z-.5))
            if not flying and (abs(sx)>.20 or abs(sz)>.20 or
                    any(abs(ground(x+dx,z+dz)-(y+sx*dx+sz*dz))>.075
                        for dx in (-2,-1,0,1,2) for dz in (-2,-1,0,1,2))):
                continue
            found=True
            break
        if not found:
            raise RuntimeError(('No safe wildlife habitat',theme,label))
        if flying:
            # Entire flight circle clears the highest terrain below it.
            y=max(ground(x+math.cos(a)*12,z+math.sin(a)*12) for a in range(64))+12+number%4
            sx=sz=0
        occupied.append((x,z))
        start=set(bpy.context.scene.objects)
        def ell(label,p,s,material,part=0,lon=None,lat=None,tilt=(0.,0.),under=None):
            obj=ellipsoid(bpy,label,(x+p[0],y+p[1],z+p[2]),s,material,lon,lat,tilt)
            shade(obj,material,under=under)
            obj['wildlife_part']=part
            return obj
        def rod(label,a,b,r,material,part=0,r2=None,sides=6):
            obj=capsule(bpy,Vector,label,(x+a[0],y+a[1],z+a[2]),(x+b[0],y+b[1],z+b[2]),r,material,r2,sides)
            shade(obj,material)
            obj['wildlife_part']=part
            return obj
        def body_tube(label,spine,radii,material,part=0,sides=7):
            obj=tube(bpy,Vector,label,[(x+p[0],y+p[1],z+p[2]) for p in spine],radii,material,sides)
            shade(obj,material,.22)
            obj['wildlife_part']=part
            return obj
        def wing_pair(k,material,root_y=0.,root_z=0.,span=1.13,chord=.42):
            for side in (-1,1):
                obj=feathers(bpy,'Bird articulated wing',(x+side*.12*k,y+root_y*k,z+root_z*k),span*k,chord*k,material,side)
                obj['wildlife_part']=1 if side<0 else 2
        pivot=0.
        kit=dict(ell=ell,rod=rod,mesh=mesh,mat=mat,x=x,y=y,z=z,black=black,orange=orange,wing=wing,dry=dry,pale=pale,
                 tube=body_tube,wings=wing_pair)
        zoo=api['biomes']
        if not T.legacy:
            pivot=zoo.animal(T,label,number,kit)
        elif species == 0:
            pivot=zoo.deer(T,number,kit)
        elif species == 1:
            zoo.hare(kit,hare)
        elif species == 2:
            # A plump grey-lag goose: the head and long neck nod as it grazes.
            ell('Goose body',(0,.50,0),(.32,.33,.55),pale,lon=10,lat=6,tilt=(0,-.1))
            ell('Goose tail',(0,.58,-.5),(.16,.1,.18),pale,lon=7,lat=4,tilt=(0,-.5))
            for side in (-1,1):
                ell('Goose wing',(side*.27,.6,-.05),(.07,.2,.42),wing if theme==2 else pale,lon=6,lat=4,tilt=(0,-.1))
            rod('Goose neck',(0,.6,.36),(0,1.06,.45),.11,pale,3,r2=.075,sides=6)
            ell('Goose head',(0,1.11,.5),(.13,.14,.18),pale,3,lon=8,lat=5)
            ell('Goose bill',(0,1.08,.7),(.07,.05,.12),orange,3,lon=6,lat=4)
            for side in (-1,1):
                ell('Goose eye',(side*.11,1.16,.56),(.025,.028,.025),black,3,lon=5,lat=3)
                rod('Goose leg',(side*.15,.3,0),(side*.16,.05,.04),.04,orange,1 if side<0 else 2,r2=.03,sides=5)
                ell('Goose foot',(side*.16,.03,.12),(.09,.02,.12),orange,1 if side<0 else 2,lon=6,lat=3)
        elif species == 3:
            zoo.serpent(kit,snake)
        else:
            zoo.bird(T,number,kit,plumage,1.,orange,wing)
        for obj in set(bpy.context.scene.objects)-start:
            obj['wildlife_origin']=(x,y,z,pivot)
            obj['wildlife_motion']=(species,obj['wildlife_part'],sx,sz)
            obj['wildlife_id']=number
            obj['wildlife_species']=label
        counts[species if T.legacy else label]=counts.get(species if T.legacy else label,0)+1
    print('HABITAT',theme,patches,'patches; wildlife',counts,flush=True)
