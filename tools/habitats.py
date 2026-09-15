"""Deterministic habitat dressing and articulated wildlife for Blender exports.

Animal vertices carry species/part and a locally verified ground plane. Runtime
animation uses bounded paths, keeping animals clear of the rally and obstacles.
"""
import math
import bmesh
import random


def build(api, theme, path, ground, water, obstacles):
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
        return ((x / 116)**2 + (z / 114)**2 < 1 and
                all(math.hypot(x-p[0], z-p[2]) > p[5]+radius+2 for p in path) and
                all(x+radius < a or x-radius > b or z+radius < c or z-radius > d
                    for a, b, c, d in bounds))

    foliage = mat('Habitat foliage '+str(theme), [(.29,.43,.16), (.43,.50,.25), (.17,.30,.25)][theme])
    dry = mat('Habitat dry stems '+str(theme), [(.64,.53,.27), (.70,.63,.39), (.42,.35,.27)][theme])
    flower = mat('Habitat flowers '+str(theme), [(.77,.69,.86), (.91,.78,.48), (.76,.84,.82)][theme])
    bark = mat('Weathered fallen wood', (.35,.29,.22))
    stone = mat('Habitat stone '+str(theme), [(.47,.49,.37), (.64,.57,.43), (.46,.54,.57)][theme])
    dressing_start = set(bpy.context.scene.objects)
    # Irregular patches leave open spaces and preserve sight lines at corners.
    patches = 0
    for _ in range(1800):
        x, z = rng.uniform(-108,108), rng.uniform(-106,106)
        if not clear(x,z,1.6) or water(x,z)[0] > -2.5:
            continue
        if math.sin(x*.12+z*.09)*math.cos(z*.16) < .12:
            continue
        y = ground(x,z)
        if patches % 11 == 0:
            end = (x+2.3, ground(x+2.3,z+.8)+.22, z+.8)
            beam('Driftwood' if theme == 1 else 'Fallen branch', (x,y+.22,z), end, .22, bark, 7)
            beam('Broken branch', (x+1,y+.3,z+.35), (x+1.3,y+.8,z+1.2), .09,bark,5)
        elif theme == 2:
            ico('Alpine scree', (x,y+.25,z), (.85,.5,.7), stone)
            for j in range(3):
                dx,dz=rng.uniform(-.8,.8),rng.uniform(-.8,.8)
                ico('Dwarf mountain pine',(x+dx,ground(x+dx,z+dz)+.35,z+dz),(.7,.55,.6),foliage)
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
                ico('Hedgerow shrub' if theme == 0 else 'Coastal scrub',(x,y+.55,z),(1.1,.85,.9),foliage,2)
        patches += 1
        if patches == 115:
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
    plumage=mat('Bird plumage '+str(theme),[(.25,.29,.26),(.90,.91,.86),(.16,.20,.22)][theme])
    wanted=([0]*5+[1]*5+[2]*6+[3]*3+[4]*9 if theme == 0 else
            [3]*4+[4]*16 if theme == 1 else [0]*4+[1]*6+[4]*7)
    occupied=[]; counts={}
    for number,species in enumerate(wanted):
        flying=species == 4
        found=False
        for _ in range(3500):
            x,z=rng.uniform(-94,94),rng.uniform(-94,94)
            if flying and (x/98)**2+(z/96)**2>1:continue
            if not flying and (not clear(x,z,3.0) or water(x,z)[0]>-3.2):
                continue
            if species == 2 and water(x,z)[0] < -16:
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
            raise RuntimeError(('No safe wildlife habitat',theme,species))
        if flying:
            # Entire flight circle clears the highest terrain below it.
            y=max(ground(x+math.cos(a)*12,z+math.sin(a)*12) for a in range(64))+12+number%4
            sx=sz=0
        occupied.append((x,z))
        start=set(bpy.context.scene.objects)
        def ell(label,p,s,material,part=0):
            obj=ico(label,(x+p[0],y+p[1],z+p[2]),s,material,2 if part==0 else 1)
            obj['wildlife_part']=part
            return obj
        def rod(label,a,b,r,material,part=0):
            obj=beam(label,(x+a[0],y+a[1],z+a[2]),(x+b[0],y+b[1],z+b[2]),r,material,6)
            obj['wildlife_part']=part
        if species == 0:
            ell('Deer body',(0,1.18,0),(.40,.50,.83),fur)
            rod('Deer neck',(0,1.28,.5),(0,1.95,.78),.23,fur,3)
            ell('Deer head',(0,1.99,.96),(.24,.25,.40),fur,3)
            ell('Deer muzzle',(0,1.88,1.23),(.18,.14,.20),black,3)
            for side in (-1,1):
                ell('Deer ear',(side*.28,2.21,.80),(.13,.26,.09),fur,3)
                ell('Deer eye',(side*.21,2.05,1.07),(.035,.045,.04),black,3)
                for front in (-1,1):
                    part=1 if side*front>0 else 2
                    rod('Deer leg',(side*.25,.99,front*.55),(side*.27,.08,front*.57),.075,fur,part)
                    ell('Deer hoof',(side*.27,.09,front*.60),(.09,.09,.14),black,part)
                if number%3 == 0:
                    rod('Stag antler',(side*.13,2.12,.8),(side*.44,2.95,.63),.045,dry,3)
                    for k in range(2):
                        rod('Antler tine',(side*(.25+k*.09),2.4+k*.23,.73),(side*(.5+k*.10),2.65+k*.23,.87),.03,dry,3)
            ell('Deer tail',(0,1.35,-.81),(.14,.18,.26),pale)
        elif species == 1:
            ell('Hare body',(0,.36,0),(.29,.35,.49),hare)
            ell('Hare head',(0,.66,.38),(.22,.24,.24),hare,3)
            for side in (-1,1):
                ell('Hare ear',(side*.13,1.03,.35),(.075,.32,.075),hare,3)
                ell('Hare eye',(side*.18,.73,.51),(.032,.035,.032),black,3)
                ell('Hare hind paw',(side*.23,.10,-.20),(.15,.10,.28),hare,1)
                ell('Hare forepaw',(side*.17,.08,.34),(.075,.08,.17),hare,2)
            ell('Hare tail',(0,.4,-.48),(.14,.14,.14),pale)
        elif species == 2:
            ell('Goose body',(0,.50,0),(.34,.37,.57),pale)
            rod('Goose neck',(0,.57,.35),(0,1.10,.44),.12,pale,3)
            ell('Goose head',(0,1.12,.48),(.17,.19,.23),pale,3)
            ell('Goose bill',(0,1.10,.73),(.105,.07,.15),orange,3)
            for side in (-1,1):
                ell('Goose eye',(side*.15,1.18,.56),(.028,.03,.03),black,3)
                rod('Goose leg',(side*.16,.30,0),(side*.16,.05,.04),.045,orange,1 if side<0 else 2)
                ell('Goose foot',(side*.16,.05,.12),(.12,.045,.15),orange,1 if side<0 else 2)
        elif species == 3:
            for j in range(16):
                zz=-.85+j*.12
                ell('Snake scales',(math.sin(j*.65)*.13,.075,zz),(.08,.07,.13),snake)
            ell('Snake head',(math.sin(15*.65)*.13,.09,1.04),(.115,.085,.16),snake,3)
        else:
            ell('Bird body',(0,0,0),(.16,.17,.43),plumage)
            ell('Bird head',(0,.09,.35),(.13,.14,.16),plumage)
            ell('Bird beak',(0,.07,.52),(.055,.04,.11),orange)
            for side in (-1,1):
                obj=mesh('Bird articulated wing',[(x+side*.12,y,z+.16),(x+side*.68,y+.08,z+.11),
                    (x+side*1.13,y-.04,z-.26),(x+side*.52,y,z-.24),(x+side*.13,y,z-.2)],
                    [(0,1,3),(1,2,3),(0,3,4)],wing)
                obj['wildlife_part']=1 if side<0 else 2
            ell('Bird tail',(0,0,-.43),(.19,.05,.22),wing)
        for obj in set(bpy.context.scene.objects)-start:
            obj['wildlife_origin']=(x,y,z,0.0)
            obj['wildlife_motion']=(species,obj['wildlife_part'],sx,sz)
            obj['wildlife_id']=number
            obj['wildlife_species']=['deer','hare','goose','snake','bird'][species]
        counts[species]=counts.get(species,0)+1
    print('HABITAT',theme,patches,'patches; wildlife',counts,flush=True)
