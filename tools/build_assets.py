"""Run with Blender: blender -b --python tools/build_assets.py.

All visible game meshes, tracks and the editable source scenes are built in
Blender. Export is a deliberately small, versioned triangle/vertex-color format.
No Blender installation or Python interpreter is needed to play the game.
"""
import bpy, math, random, struct, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hydrology import Watershed
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree
from math import sin, cos, pi

ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
random.seed(83)
materials={}

def mat(name,color,paint=0):
    if name in materials: return materials[name]
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m['paint']=paint
    materials[name]=m
    return m

def xyz(p): return (p[0],-p[2],p[1])
def eng(v): return (v.x,v.z,-v.y)
def clear():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)

def finish(o,name,material):
    o.name=name;o.data.materials.append(material)
    return o

def box(name,p,size,material,bevel=0,yaw=0):
    bpy.ops.mesh.primitive_cube_add(size=1,location=xyz(p))
    o=bpy.context.object;o.scale=(size[0],size[2],size[1]);o.rotation_euler.z=yaw
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if bevel:
        m=o.modifiers.new('Soft molded edges','BEVEL');m.width=bevel;m.segments=2
        n=o.modifiers.new('Weighted corner normals','WEIGHTED_NORMAL')
    return finish(o,name,material)

def ico(name,p,scale,material,sub=1):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=sub,radius=1,location=xyz(p))
    o=bpy.context.object;o.scale=(scale[0],scale[2],scale[1]);return finish(o,name,material)

def cone(name,p,r1,r2,depth,material,vertices=10):
    bpy.ops.mesh.primitive_cone_add(vertices=vertices,radius1=r1,radius2=r2,depth=depth,location=xyz(p))
    return finish(bpy.context.object,name,material)

def beam(name,a,b,r,material,vertices=8):
    va,vb=Vector(xyz(a)),Vector(xyz(b));d=vb-va
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=r,depth=d.length,location=(va+vb)*.5)
    o=bpy.context.object;o.rotation_euler=d.to_track_quat('Z','Y').to_euler()
    return finish(o,name,material)

def mesh(name,vs,fs,material):
    m=bpy.data.meshes.new(name);m.from_pydata([xyz(v) for v in vs],[],fs);m.update()
    o=bpy.data.objects.new(name,m);bpy.context.collection.objects.link(o);o.data.materials.append(material);return o

def road_marking(name,surface,lap_length,start,end,left,right,material):
    """Clip paint to road triangles in (distance, lane) space, including the seam.

    Interpolating the original triangle vertices preserves the exact road height
    and banking, even where a marking crosses a bend or a triangle diagonal.
    """
    vertices=[];faces=[]
    for cycle in range(math.floor(start/lap_length),math.floor(end/lap_length)+1):
        lo=start-cycle*lap_length;hi=end-cycle*lap_length
        for triangle in surface:
            if max(v[0] for v in triangle)<=lo or min(v[0] for v in triangle)>=hi:continue
            polygon=list(triangle)
            for axis,bound,sign in [(0,lo,1),(0,hi,-1),(1,left,1),(1,right,-1)]:
                clipped=[]
                for previous,current in zip(polygon[-1:]+polygon[:-1],polygon):
                    a=(previous[axis]-bound)*sign>=0;b=(current[axis]-bound)*sign>=0
                    if a!=b:
                        f=(bound-previous[axis])/(current[axis]-previous[axis])
                        clipped.append(tuple(x+(y-x)*f for x,y in zip(previous,current)))
                    if b:clipped.append(current)
                polygon=clipped
            if len(polygon)<3:continue
            for k in range(1,len(polygon)-1):
                a,b,c=polygon[0],polygon[k],polygon[k+1]
                if abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))<1e-9:continue
                index=len(vertices)
                vertices.extend((v[2],v[3]+.035,v[4]) for v in (a,b,c))
                faces.append((index,index+1,index+2))
    return mesh(name,vertices,faces,material)

def export(path,crowd=False,wildlife=False,vegetation=False):
    bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get();data=[]
    for o in bpy.context.scene.objects:
        if o.type!='MESH' or o.hide_render:continue
        if o.get('water'):continue
        if ('spectator_group' in o)!=crowd:continue
        if ('wildlife_origin' in o)!=wildlife:continue
        if ('vegetation_origin' in o)!=vegetation:continue
        e=o.evaluated_get(deps);me=e.to_mesh();me.calc_loop_triangles();normal=e.matrix_world.to_3x3().inverted().transposed()
        for tri in me.loop_triangles:
            material=me.materials[tri.material_index] if me.materials else None
            color=tuple(material.diffuse_color[:3]) if material else (.5,.5,.5)
            paint=material.get('paint',0) if material else 0
            for idx in tri.vertices:
                v=me.vertices[idx];p=e.matrix_world@v.co;n=(normal@(v.normal if tri.use_smooth else tri.normal)).normalized()
                vertex_color=me.color_attributes.get('Riverbed')
                tint=tuple(vertex_color.data[idx].color[:3]) if vertex_color else color
                data.extend((*eng(p),*eng(n),*tint,paint))
                if crowd:
                    data.extend((*o['spectator_position'],o['spectator_yaw'],
                                 ['cheer','wave','clap','camera','flag'].index(o['spectator_pose']),
                                 o['spectator_parts'][idx],o['spectator_scale'],o['spectator_width']))
                if wildlife:data.extend((*o['wildlife_origin'],*o['wildlife_motion']))
                if vegetation:data.extend((*o['vegetation_origin'],*o['vegetation_motion']))
        e.to_mesh_clear()
    with open(path,'wb') as f:
        f.write(b'TCV1' if vegetation else b'TCF1' if wildlife else b'TCA1' if crowd else b'TCM1');f.write(struct.pack('<I',len(data)//(18 if crowd or wildlife or vegetation else 10)));f.write(struct.pack('<%sf'%len(data),*data))
    print('EXPORTED',os.path.basename(path),len(data)//(54 if crowd or wildlife or vegetation else 30),'triangles',flush=True)

def export_surface(path):
    """Collision uses the same triangles as the visible driving surfaces."""
    bpy.context.view_layer.update();data=[]
    for o in bpy.context.scene.objects:
        if not o.get('drivable'):continue
        o.data.calc_loop_triangles()
        for tri in o.data.loop_triangles:
            points=[eng(o.matrix_world@o.data.vertices[i].co) for i in tri.vertices]
            a,b,c=points
            if abs((b[0]-a[0])*(c[2]-a[2])-(b[2]-a[2])*(c[0]-a[0]))<1e-6:continue
            for p in points:data.extend(p)
    with open(path,'wb') as f:
        f.write(b'TCS1');f.write(struct.pack('<I',len(data)//9))
        f.write(struct.pack('<%sf'%len(data),*data))

def save(name):
    scene=bpy.context.scene
    scene.world.color=(.3,.35,.4)
    bpy.ops.object.light_add(type='SUN',location=(0,0,80));bpy.context.object.rotation_euler=(.35,-.4,-.5);bpy.context.object.data.energy=3
    bpy.ops.object.camera_add(location=xyz((180,190,210)))
    c=bpy.context.object;c.rotation_euler=(Vector((0,0,0))-c.location).to_track_quat('-Z','Y').to_euler();c.data.type='ORTHO';c.data.ortho_scale=300;scene.camera=c
    scene.render.resolution_x=1600;scene.render.resolution_y=1200;scene.render.resolution_percentage=100
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'art',name+'.blend'))

cream=mat('Warm ivory',(.94,.91,.80));rubber=mat('Soft rubber',(.055,.065,.073));chrome=mat('Brushed aluminum',(.60,.66,.65));glass=mat('Smoky blue glass',(.075,.18,.22));orange=mat('Signal orange',(.96,.25,.075));dark=mat('Ink',(.09,.13,.16));wood=mat('Warm cedar',(.53,.29,.14))
paint=mat('Custom body paint',(.94,.28,.10),1)

def build_car():
    clear()
    box('Die cast rally chassis',(0,.51,0),(1.7,.53,3.0),paint,.19)
    box('Hood',(0,.84,.90),(1.58,.23,.96),paint,.09)
    box('Cabin glazing',(0,1.09,-.24),(1.37,.66,1.31),glass,.19)
    box('Floating roof',(0,1.43,-.29),(1.39,.15,1.10),paint,.12)
    for x in [-.52,.52]:
        box('Hood racing stripe',(x,.963,.91),(.17,.012,.8),cream,.003)
        box('Roof racing stripe',(x,1.51,-.28),(.14,.015,.86),cream,.002)
    for x in [-.85,.85]:
        for z in [-.95,.94]:
            beam('All terrain tire',(x-.15,.39,z),(x+.15,.39,z),.43,rubber,16)
            beam('Recessed wheel hub',(x-.17,.39,z),(x+.17,.39,z),.245,chrome,12)
            beam('Hub center',(x-.18,.39,z),(x+.18,.39,z),.105,dark,10)
        box('Side mirror',(x,1.05,.32),(.23,.15,.23),paint,.06)
        box('Door handle',(x,.92,-.38),(.06,.07,.23),cream,.015)
    box('Front bumper',(0,.43,1.52),(1.63,.22,.16),chrome,.05)
    box('Intake',(0,.64,1.506),(.58,.20,.055),dark,.02)
    for x in [-.57,.57]:
        box('Headlamp',(x,.80,1.48),(.39,.25,.08),cream,.055)
        box('Tail lamp',(x,.73,-1.49),(.37,.16,.055),orange,.025)
    box('Rear wing',(0,1.03,-1.36),(1.91,.13,.34),dark,.035)
    for x in [-.58,.58]:box('Wing strut',(x,.84,-1.35),(.07,.31,.08),dark)
    box('Number plate',(0,.73,-1.54),(.44,.15,.04),cream,.01)
    export(os.path.join(ROOT,'assets/models/car.tcm'));save('rally_car')

def build_can():
    clear();fuel=mat('Fuel mint',(.43,.93,.63))
    box('Jerrycan body',(0,.61,0),(.79,1.06,.39),fuel,.1)
    box('Handle',(0,1.19,0),(.5,.13,.24),dark,.055)
    for x in [-.2,.2]:box('Handle neck',(x,1.11,0),(.09,.22,.22),dark,.03)
    box('Fuel label',(0,.67,.203),(.5,.47,.018),cream,.025)
    box('Label slash',(0,.67,.22),(.14,.32,.02),orange,.01,yaw=-.3)
    beam('Cap',(.3,1.04,0),(.3,1.17,0),.13,dark)
    export(os.path.join(ROOT,'assets/models/fuel.tcm'));save('fuel_can')

def build_helicopter():
    """Separate rotor exports pivot at the origin; the editable scene is assembled."""
    clear()
    blue=mat('Broadcast navy',(.12,.24,.31))
    ico('Rounded cabin',(0,0,.45),(1.0,.85,1.75),cream,2)
    ico('Cockpit glazing',(0,.22,1.28),(.89,.65,1.02),glass,2)
    box('Cabin roof',(0,.75,.38),(1.5,.18,1.75),blue,.12)
    ico('Engine housing',(0,.69,-.62),(.61,.49,.91),blue,2)
    beam('Rotor mast',(0,.9,0),(0,1.55,0),.12,chrome)
    beam('Tapered tail silhouette',(0,.10,-.9),(0,.52,-4.1),.19,blue)
    mesh('Tail fin',[(-.08,.4,-4.25),(-.08,1.7,-4.35),(-.08,1.7,-3.93),(-.08,.4,-3.55),
                     (.08,.4,-4.25),(.08,1.7,-4.35),(.08,1.7,-3.93),(.08,.4,-3.55)],
         [(0,1,2,3),(4,7,6,5),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],orange)
    box('Tail stabilizer',(0,.5,-3.25),(2.0,.10,.48),cream,.04)
    for side in [-1,1]:
        x=side*.97
        beam('Landing skid',(x,-1.12,-1.25),(x,-1.12,1.45),.065,dark)
        beam('Skid curved nose',(x,-1.12,1.45),(x,-.96,1.77),.065,dark)
        for z in [-.73,.85]:beam('Skid support',(side*.6,-.5,z),(x,-1.12,z),.055,chrome)
        box('Broadcast side stripe',(side*.96,-.05,.05),(.035,.22,1.3),orange,.015)
    ico('Underslung camera',(0,-.87,1.12),(.29,.29,.29),dark,2)
    beam('Camera lens',(0,-.90,1.25),(0,-1.02,1.43),.15,glass)
    body=list(bpy.context.scene.objects)
    export(os.path.join(ROOT,'assets/models/helicopter.tcm'))
    for o in body:o.hide_render=True
    for angle in [0,pi/2]:
        box('Main rotor blade',(0,0,0),(6.8,.045,.17),dark,.018,yaw=angle)
    ico('Main rotor hub',(0,0,0),(.24,.12,.24),chrome,1)
    rotor=[o for o in bpy.context.scene.objects if o not in body]
    export(os.path.join(ROOT,'assets/models/helicopter-rotor.tcm'))
    for o in rotor:o.hide_render=True
    box('Tail rotor blade',(0,0,0),(.055,1.25,.12),dark,.015)
    box('Tail rotor blade',(0,0,0),(.055,.12,1.25),dark,.015)
    tail=[o for o in bpy.context.scene.objects if o not in body and o not in rotor]
    export(os.path.join(ROOT,'assets/models/helicopter-tail-rotor.tcm'))
    for o in body+rotor:o.hide_render=False
    for o in rotor:o.location+=Vector(xyz((0,1.55,0)))
    for o in tail:o.location+=Vector(xyz((.25,.74,-4.02)))
    save('helicopter')

# The final two bends need enough radius for the inside road edge and curb.
KNOTS=[(-62,-68),(-18,-81),(43,-76),(80,-50),(84,-17),(51,-2),(34,22),(62,55),(46,78),(1,84),(-43,67),(-73,43),(-71,9),(-39,-7),(-44,-30),(-64,-43)]
def spline(t,theme):
    knots=KNOTS
    if theme==1:knots=[(-71,-64),(-24,-85),(36,-79),(78,-53),(94,-9),(78,32),(54,70),(5,83),(-29,60),(-11,29),(28,18),(28,-8),(-15,-18),(-58,0),(-86,-20)]
    if theme==2:knots=[(-62,-73),(-16,-83),(35,-73),(66,-43),(75,12),(48,67),(13,86),(-27,71),(-18,39),(14,26),(20,5),(-7,-7),(-30,15),(-62,25),(-80,2),(-73,-32)]
    q=t*len(knots);i=int(q)%len(knots);u=q-int(q)
    p=[knots[(i+j)%len(knots)] for j in [-1,0,1,2]]
    v=[.5*((2*p[1][a])+(-p[0][a]+p[2][a])*u+(2*p[0][a]-5*p[1][a]+4*p[2][a]-p[3][a])*u*u+(-p[0][a]+3*p[1][a]-3*p[2][a]+p[3][a])*u*u*u) for a in [0,1]]
    return v

def terrain(x,z,theme):
    if theme==0:return 4.0+4.4*sin(x*.025+.8)*cos(z*.029)+2.1*sin(z*.055+x*.02)
    if theme==1:return 3.5+3.4*sin(x*.027)*cos(z*.031)+1.8*sin(z*.038+.6)
    return 8+7.2*sin(x*.023+.5)*cos(z*.029)+3.3*cos(z*.041+x*.014)

def tree(x,y,z,s,theme):
    before=set(bpy.context.scene.objects)
    if theme==1:
        beam('Palm trunk',(x,y,z),(x+s*.7,y+s*7,z),s*.25,wood)
        top=(x+s*.7,y+s*7,z)
        for k in range(7):
            a=k*pi*2/7
            end=(top[0]+cos(a)*s*4.3,top[1]-.9*s,top[2]+sin(a)*s*4.3)
            mid=(top[0]+cos(a)*s*2,top[1]+s*.6,top[2]+sin(a)*s*2)
            perp=(-sin(a)*s*.7,0,cos(a)*s*.7)
            mesh('Sculpted palm frond',[top,(mid[0]+perp[0],mid[1],mid[2]+perp[2]),end,(mid[0]-perp[0],mid[1],mid[2]-perp[2]),mid],[(0,1,4),(1,2,4),(2,3,4),(3,0,4)],mat('Palm green',(.15,.47,.29)))
        for k in range(3):ico('Coconut',(top[0]+.35*s*cos(k*2),top[1]-.4*s,top[2]+.35*s*sin(k*2)),(.35*s,)*3,wood)
    else:
        cone('Tree trunk',(x,y+1.2*s,z),.3*s,.22*s,2.4*s,wood)
        if theme==2 or random.random()<.5:
            for k in range(3):
                cone('Layered fir',(x,y+(2.6+k*1.5)*s,z),(2.1-k*.42)*s,0,3.3*s,mat('Pine needles',(.13,.31,.25)))
                if theme==2:cone('Snow cap',(x,y+(3.15+k*1.5)*s,z),(1.78-k*.38)*s,0,2.55*s,mat('Fresh snow',(.89,.95,.94)))
        else:
            ico('Rounded crown',(x,y+4.2*s,z),(2.6*s,3*s,2.3*s),mat('Leaf green',(.31,.52,.25)),2)
            ico('Small crown',(x+1.7*s,y+3.7*s,z+.6*s),(1.8*s,2.1*s,1.7*s),mat('Leaf light',(.48,.64,.28)),1)

    for obj in set(bpy.context.scene.objects)-before:
        obj['vegetation_origin']=(x,y,z,7*s)
        obj['vegetation_motion']=(1.0 if theme==1 else .4 if theme==2 else .65,
                                  float(obj.name.startswith('Sculpted palm frond')),0.,0.)

def barn(x,y,z,yaw,theme):
    wall=mat('Barn vermilion',(.72,.22,.13)) if theme==0 else mat('Chalet wood',(.45,.30,.23))
    roof=mat('Roof charcoal',(.19,.24,.26)) if theme==0 else mat('Snow roofs',(.91,.94,.92))
    objs=set(bpy.context.scene.objects)
    box('Barn walls',(x,y+3,z),(8,6,11),wall,.12)
    vs=[(x-4.8,y+5.8,z-6),(x+4.8,y+5.8,z-6),(x,y+8.6,z-6),(x-4.8,y+5.8,z+6),(x+4.8,y+5.8,z+6),(x,y+8.6,z+6)]
    mesh('Pitched roof',vs,[(0,3,5,2),(2,5,4,1),(0,2,1),(3,4,5)],roof)
    box('Barn double door',(x,y+2,z+5.53),(3.2,4,.13),cream,.04)
    box('Door inset',(x,y+2,z+5.61),(2.7,3.5,.05),wall)
    for dx in [-1.32,0,1.32]:box('Door frame',(x+dx,y+2,z+5.7),(.14,3.6,.09),cream)
    for k in range(5):box('Wall batten',(x-4.04,y+3,z-4+k*2),(.1,5.4,.15),cream)
    for dx in [-2.8,2.8]:box('Barn window',(x+dx,y+4,z+5.58),(1.15,1.3,.1),glass,.05)
    if theme==0:
        cone('Grain silo',(x+6,y+4,z-2),2,2,8,mat('Silo silver',(.58,.64,.60)),16)
        cone('Silo lid',(x+6,y+8.8,z-2),2.2,0,1.6,roof,16)

def spectator(position,yaw,theme,kind,pose,rng,ground_height,group):
    """One low-poly, vertex-colored toy figure, baked into the track batch."""
    scale={'child':.70,'adult':1.,'tall':1.12,'broad':1.02,'senior':.96}[kind]
    width=1.22 if kind=='broad' else 1.
    skins=[(.94,.69,.49),(.73,.45,.28),(.40,.23,.15),(.59,.35,.23)]
    shirts=[(.92,.22,.12),(.12,.49,.70),(.97,.68,.12),(.54,.28,.65),(.20,.65,.43),(.92,.86,.69)]
    skin=mat('Spectator skin '+str(c:=rng.randrange(len(skins))),skins[c])
    shirt=mat('Spectator shirt '+str(c:=rng.randrange(len(shirts))),shirts[c])
    trousers=mat('Spectator denim',(.13,.23,.34)) if rng.random()<.6 else dark
    hair=mat('Spectator hair '+kind,(.68,.69,.65) if kind=='senior' else rng.choice([(.16,.10,.07),(.48,.25,.10),(.85,.65,.28)]))
    vertices=[];faces=[];face_materials=[];palette=[];parts=[];body_part=0
    def point(p):
        x,y,z=p;x*=scale;y*=scale;z*=scale
        return (position[0]+cos(yaw)*x+sin(yaw)*z,position[1]+y,position[2]-sin(yaw)*x+cos(yaw)*z)
    def part(vs,fs,material):
        if material not in palette:palette.append(material)
        start=len(vertices);vertices.extend(point(p) for p in vs)
        parts.extend([body_part]*len(vs))
        faces.extend(tuple(start+i for i in f) for f in fs)
        face_materials.extend([palette.index(material)]*len(fs))
    def block(p,size,material):
        x,y,z=p;a,b,c=[v/2 for v in size]
        part([(x+dx*a,y+dy*b,z+dz*c) for dx,dy,dz in
              [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]],
             [(0,3,2,1),(4,5,6,7),(0,4,7,3),(1,2,6,5),(0,1,5,4),(3,7,6,2)],material)
    def limb(a,b,r,material):
        a,b=Vector(a),Vector(b);axis=(b-a).normalized()
        u=axis.cross(Vector((0,0,1)))
        if u.length<.01:u=axis.cross(Vector((1,0,0)))
        u.normalize();v=axis.cross(u)
        vs=[tuple(p+r*(cos(k*pi/3)*u+sin(k*pi/3)*v)) for p in (a,b) for k in range(6)]
        part(vs,[tuple(reversed(range(6))),tuple(range(6,12))]+[(k,(k+1)%6,(k+1)%6+6,k+6) for k in range(6)],material)
    def head(p,size,material):
        x,y,z=p;rx,ry,rz=size
        vs=[(x+rx*cos(k*pi/4)*r,y+ry*h,z+rz*sin(k*pi/4)*r)
            for h,r in [(-1,.5),(-.5,1),(.5,1),(1,.5)] for k in range(8)]
        part(vs,[tuple(range(8)),tuple(reversed(range(24,32)))]+
             [(j*8+k,(j+1)*8+k,(j+1)*8+(k+1)%8,j*8+(k+1)%8) for j in range(3) for k in range(8)],material)
    # Each shoe follows the actual triangulated island, including hillside groups.
    for side in [-1,1]:
        foot=point((side*.18,0,.04));floor=(ground_height(foot[0],foot[2])-position[1])/scale
        block((side*.18,floor+.09,.07),(.23,.18,.40),rubber)
        knee=(side*.18,.55,.015);hip=(side*.15,.95,0)
        limb((side*.18,floor+.17,.03),knee,.095,skin if theme==1 else trousers)
        limb(knee,hip,.12,trousers)
    block((0,1.23,0),(.58*width,.64,.34),shirt)
    if theme==2:
        block((0,1.47,.02),(.65*width,.13,.40),orange)
        block((.16,1.27,.20),(.12,.35,.055),orange)
    limb((0,1.50,0),(0,1.64,0),.105,skin)
    head((0,1.80,.015),(.22,.27,.22),skin)
    block((0,2.015,-.025),(.40,.13,.34),hair)
    # A nose and dark glasses make the direction of attention readable.
    block((0,1.80,.245),(.09,.10,.09),skin)
    if theme==1 or pose=='camera':block((0,1.88,.225),(.37,.065,.045),dark)
    hat=rng.randrange(3)
    if theme==2:
        head((0,2.035,0),(.24,.16,.24),shirt)
        block((0,2.19,0),(.13,.13,.13),cream)
    elif hat==0:
        block((0,2.04,0),(.45,.15,.43),shirt)
        block((0,2.01,.22),(.43,.055,.28),shirt)
    elif hat==1:
        block((0,1.84,-.20),(.39,.32,.12),hair)
    poses={
        'cheer':[((-.48,1.76,0),(-.65,2.16,.06)),((.48,1.76,0),(.65,2.16,.06))],
        'wave':[((-.44,1.11,0),(-.43,.91,.12)),((.48,1.80,0),(.37,2.22,.08))],
        'clap':[((-.43,1.43,.17),(-.10,1.65,.42)),((.43,1.43,.17),(.10,1.65,.42))],
        'camera':[((-.43,1.45,.10),(-.17,1.79,.42)),((.43,1.45,.10),(.17,1.79,.42))],
        'flag':[((-.44,1.16,0),(-.40,.99,.12)),((.49,1.69,.08),(.64,1.97,.12))]}
    for side,(elbow,hand) in zip([-1,1],poses[pose]):
        body_part=1 if side<0 else 2
        shoulder=(side*.30*width,1.45,0)
        limb(shoulder,elbow,.105,shirt)
        limb(elbow,hand,.08,shirt if theme==2 else skin)
        block(hand,(.16,.18,.16),skin)
    if pose=='camera':
        body_part=0
        block((0,1.80,.45),(.40,.24,.18),dark)
        limb((0,1.80,.51),(0,1.80,.67),.11,chrome)
    if pose=='flag':
        body_part=2
        limb((.64,1.68,.12),(.64,3.0,.12),.027,wood)
        # Faceted cloth with front and back faces for either viewing direction.
        vv=[(.64,2.98,.12),(1.05,2.92,.24),(1.51,2.99,.09),(.64,2.42,.12),(1.05,2.36,.24),(1.51,2.43,.09)]
        body_part=3
        part(vv,[(0,3,4,1),(1,4,5,2),(1,4,3,0),(2,5,4,1)],shirt)
        block((.85,2.69,.19),(.13,.43,.025),cream)
    obj=mesh('Rally spectator',vertices,faces,palette[0])
    for material in palette[1:]:obj.data.materials.append(material)
    for polygon,index in zip(obj.data.polygons,face_materials):polygon.material_index=index
    obj['spectator_group']=group;obj['spectator_kind']=kind;obj['spectator_pose']=pose
    obj['spectator_position']=position;obj['spectator_yaw']=yaw
    obj['spectator_parts']=parts;obj['spectator_scale']=scale;obj['spectator_width']=width
    return obj

def build_spectators(path,theme,ground_height,obstacles,water_sample):
    rng=random.Random(920+theme)
    bpy.context.view_layer.update()
    bounds=[]
    for obj in obstacles:
        if obj.type!='MESH':continue
        corners=[eng(obj.matrix_world@Vector(p)) for p in obj.bound_box]
        bounds.append((min(p[0] for p in corners)-.85,max(p[0] for p in corners)+.85,
                       min(p[2] for p in corners)-.85,max(p[2] for p in corners)+.85))
    occupied=[];counts=[]
    # Unevenly spaced viewing spots, on both sides and around all three jumps.
    for group,idx in enumerate([8,58,95,116,170,218,282,309,360,405,470,503,550,601,628]):
        side=-1 if group%3==0 else 1;count=0;wanted=[7,4,6,5,3,8][group%6]
        for attempt in range(160):
            if count==wanted:break
            # A crowded or steep viewing spot can spread along the bend or
            # use the opposite shoulder rather than intersecting scenery.
            spread=7 if attempt<80 else 16
            p=path[(idx+int(rng.uniform(-spread,spread)))%len(path)]
            lane=side*(-1 if attempt>=120 else 1)*(p[5]+rng.uniform(3.2,6.6))
            x,z=p[0]+p[4]*lane,p[2]-p[3]*lane
            if water_sample(x,z)[0]>-2:continue
            if any(a<x<b and c<z<d for a,b,c,d in bounds):continue
            if any(math.hypot(x-a,z-b)<1.65 for a,b in occupied):continue
            near=min(path,key=lambda q:(q[0]-x)**2+(q[2]-z)**2)
            if math.hypot(near[0]-x,near[2]-z)<near[5]+2.6:continue
            y=ground_height(x,z)
            if max(abs(ground_height(x+dx,z+dz)-y) for dx,dz in [(-.4,0),(.4,0),(0,-.4),(0,.4)])>.24:continue
            yaw=math.atan2(near[0]-x,near[2]-z)+rng.uniform(-.18,.18)
            kind=['adult','child','broad','senior','tall'][(count+group)%5]
            pose=['flag','cheer','wave','camera','clap'][(count+group*2)%5]
            spectator((x,y,z),yaw,theme,kind,pose,rng,ground_height,group)
            occupied.append((x,z));count+=1
        assert count>=3,('Too little room for spectator group',theme,group,count)
        counts.append(count)
    print('SPECTATORS',theme,sum(counts),'in',len(counts),'groups',counts,flush=True)

def color_waterbed(island,sample,theme,grass):
    # Interpolated vertex colors keep the shoreline organic even on coarse
    # terrain triangles; assigning whole dark faces makes a saw-toothed bank.
    old=island.data.color_attributes.get('Riverbed')
    if old:island.data.color_attributes.remove(old)
    colors=island.data.color_attributes.new(name='Riverbed',type='FLOAT_COLOR',domain='POINT')
    bed=[(.43,.44,.33),(.63,.56,.39),(.43,.53,.55)][theme]
    for i,vertex in enumerate(island.data.vertices):
        x,y,z=eng(vertex.co);wet,level,_=sample(x,z)
        blend=max(0,min(1,(wet+1.5)/3))*max(0,min(1,(level+.6-y)/.8))
        blend=blend*blend*(3-2*blend)
        colors.data[i].color=(*(a+(b-a)*blend for a,b in zip(grass,bed)),1)

def build_water(name,theme,sample,layout,ground_height,road_bvh):
    vertices=[];faces=[];payload=[]
    def vertex(wx,wz):
        edge,level,flow=sample(wx,wz)
        if (wx/128)**2+(wz/126)**2<.99:
            bed=ground_height(wx,wz)
            hit,_,_,_=road_bvh.ray_cast(Vector((wx,-wz,100)),Vector((0,0,-1)))
            if hit is not None:bed=max(bed,hit.z+.035)
        else:bed=-8
        return (wx,level,wz,min(edge,level-bed),flow[0],flow[1])
    def emit(tri,ocean=False):
        if theme==1 and not ocean and max(q[1] for q in tri)<=-6.849:return
        # Clip at the actual bank/road intersection, avoiding square shorelines.
        poly=[]
        for a,b in zip(tri[-1:]+tri[:-1],tri):
            if (a[3]>0)!=(b[3]>0):
                f=a[3]/(a[3]-b[3]);poly.append(tuple(c+(d-c)*f for c,d in zip(a,b)))
            if b[3]>0:poly.append(b)
        for i in range(1,len(poly)-1):
            face=[]
            for q in (poly[0],poly[i],poly[i+1]):
                face.append(len(vertices));vertices.append(q[:3])
                payload.extend((*q[:3],0,1,0,max(0,q[3]),q[4],q[5],0))
            faces.append(tuple(face))
    # Fine geometry where wheels meet the ford, inexpensive distant sea ring.
    x0,x1,z0,z1=layout.bounds
    step=.7
    for j in range(math.floor(z0/step),math.ceil(z1/step)):
        for i in range(math.floor(x0/step),math.ceil(x1/step)):
            x,z=i*step,j*step
            a,b,c,d=vertex(x,z),vertex(x+step,z),vertex(x+step,z+step),vertex(x,z+step)
            emit([a,c,b]);emit([a,d,c])
    if theme==1:
        # Ocean replaces the old opaque turquoise plinth. Its centre is hidden
        # by the island; the inner edge extends under the carved river mouth.
        for i in range(192):
            a=i*2*pi/192;b=(i+1)*2*pi/192
            q=[(cos(t)*r,-6.85,sin(t)*r,3.,.18,.08) for t,r in [(a,118),(a,145),(b,145),(b,118)]]
            emit([q[0],q[2],q[1]],True);emit([q[0],q[3],q[2]],True)
    obj=mesh('Watershed — spring to basin' if theme==0 else 'Watershed — downhill outlet',vertices,faces,mat('Water preview',(.12,.36,.33)))
    obj['water']=True
    obj['water_route']=[v for p in layout.route for v in p]
    obj['water_source']=layout.source
    obj['water_destination']=['sheltered pond with sediment shelves','tidal estuary','rocky meltwater outlet'][theme]
    obj['water_character']=['meadow meanders','sand-bar creek','confined alpine channel'][theme]
    if layout.lake_level is not None:obj['water_lake_level']=layout.lake_level
    with open(os.path.join(ROOT,'assets/tracks',name+'-water.tcm'),'wb') as f:
        f.write(b'TCM1');f.write(struct.pack('<I',len(payload)//10));f.write(struct.pack('<%sf'%len(payload),*payload))
    print('WATER',name,len(payload)//30,'triangles',flush=True)

def refine_water_ground(vs,fs,sample):
    for _ in range(2):
        edges=set()
        for face in fs:
            for a,b in zip(face,face[1:]+face[:1]):
                p,q=vs[a],vs[b]
                if math.hypot(p[0]-q[0],p[2]-q[2])<=1.35:continue
                probes=[sample(p[0],p[2]),sample(q[0],q[2]),
                        sample((p[0]+q[0])/2,(p[2]+q[2])/2)]
                # Refine the narrow stream and shore, not the flat pond floor.
                if any(-2<edge and (edge<.5 or math.hypot(*flow)>.1)
                       for edge,_,flow in probes):
                    edges.add(tuple(sorted((a,b))))
        midpoints={}
        for a,b in sorted(edges):
            p,q=vs[a],vs[b];midpoints[a,b]=len(vs)
            vs.append(tuple((u+v)/2 for u,v in zip(p,q)))
        refined=[]
        for face in fs:
            boundary=[]
            for a,b in zip(face,face[1:]+face[:1]):
                boundary.append(a)
                key=tuple(sorted((a,b)))
                if key in midpoints:boundary.append(midpoints[key])
            if len(boundary)==3:
                refined.append(face);continue
            center=len(vs);vs.append(tuple(sum(vs[i][j] for i in face)/3 for j in range(3)))
            refined.extend((a,b,center) for a,b in zip(boundary,boundary[1:]+boundary[:1]))
        fs=refined
    return vs,fs

# Entry taper, four-lane start/end, exit taper. Keep the broad carriageway on
# open perimeter sections, away from the fords and the infield hairpins.
FOUR_LANE_SECTIONS=[(.055,.09,.205,.24),(.06,.095,.25,.285),(.185,.22,.335,.37)]

def road_half_width(t,theme):
    entry,start,end,exit=FOUR_LANE_SECTIONS[theme]
    base=5.5 if theme==2 else 5.2
    if t<=entry or t>=exit:return base
    if start<=t<=end:return base*2
    u=(t-entry)/(start-entry) if t<start else (exit-t)/(exit-end)
    # Mostly straight tapers with eased joins to the constant-width sections.
    ease=.15
    if u<ease:blend=u*u/(2*ease)
    elif u>1-ease:blend=1-ease-(1-u)*(1-u)/(2*ease)
    else:blend=u-ease/2
    return base*(1+blend/(1-ease))

def grade_fords(path,banks,watershed):
    # Only a stream actually crossing the pavement creates a ford. Proximity
    # to a pond or a bank must not carve independent holes into either edge.
    wet=[]
    for i,p in enumerate(path):
        steps=math.ceil(2*p[5]/.4)
        for j in range(steps+1):
            lane=p[5]*(2*j/steps-1)
            x,z=p[0]+p[4]*lane,p[2]-p[3]*lane
            edge,level,flow=watershed.sample(x,z)
            if edge>-.2 and math.hypot(*flow)>.25:wet.append((i,x,z,level))
    groups=[]
    for sample in wet:
        if not groups or sample[0]>groups[-1][-1][0]+1:groups.append([])
        groups[-1].append(sample)
    for group in groups:
        # Fit the local stream's plane, so the whole cross-section meets the
        # water at the same shallow depth instead of twisting its two edges.
        cx,cz,cy=(sum(p[k] for p in group)/len(group) for k in (1,2,3))
        xx=zz=.01;xz=xy=zy=0.
        for _,x,z,y in group:
            x-=cx;z-=cz;y-=cy
            xx+=x*x;zz+=z*z;xz+=x*z;xy+=x*y;zy+=z*y
        determinant=xx*zz-xz*xz
        gx=(xy*zz-zy*xz)/determinant;gz=(zy*xx-xy*xz)/determinant
        cy-=max(0.,max(cy+gx*(x-cx)+gz*(z-cz)-y for _,x,z,y in group))
        start=path[group[0][0]][6]-2;end=path[group[-1][0]][6]+2
        # Finish grading before a jump's footprint; otherwise the approach
        # steepens under its fixed wedge and exposes a vertical entry lip.
        ramp_distances=[path[i][6] for i in (99,296,486)]
        approach=min([28.]+[max(8.,start-s-6.) for s in ramp_distances if s<start])
        departure=min([28.]+[max(8.,s-end-6.) for s in ramp_distances if s>end])
        for i,p in enumerate(path):
            distance=max(start-p[6],p[6]-end,0.)
            taper=approach if p[6]<start else departure
            u=max(0.,1-distance/taper)
            blend=u*u*u*(10-15*u+6*u*u)
            level=cy+gx*(p[0]-cx)+gz*(p[2]-cz)-.30
            y=p[1]+(level-p[1])*blend
            bank=p[5]*(gx*p[4]-gz*p[3])
            banks[i]+=(bank-banks[i])*blend
            path[i]=(p[0],y,*p[2:])
    # Runtime progress, markings and ramp positions use the graded centreline.
    distance=0.
    for i,p in enumerate(path):
        if i:distance+=math.dist(p[:3],path[i-1][:3])
        path[i]=(*p[:6],distance)
    return distance+math.dist(path[-1][:3],path[0][:3])

def build_track(theme,name):
    clear();random.seed(185+theme)
    N=640;width=10.4 if theme!=2 else 11.0
    watershed=Watershed(theme,terrain)
    water_sample=watershed.sample
    path=[];distance=0
    for i in range(N):
        t=i/N;x,z=spline(t,theme);x2,z2=spline((t+.0001)%1,theme);d=Vector((x2-x,0,z2-z)).normalized()
        y=terrain(x,z,theme)+.15
        if i:distance+=Vector((x-path[-1][0],y-path[-1][1],z-path[-1][2])).length
        path.append((x,y,z,d.x,d.z,road_half_width(t,theme),distance))
    grass=mat(['Meadow','Golden sand','Powder snow'][theme],[(.48,.64,.30),(.83,.73,.49),(.76,.87,.87)][theme])
    road=mat('Road '+name,[(.28,.31,.29),(.53,.44,.31),(.37,.48,.54)][theme])
    curb=mat('Curb '+name,[(.80,.31,.18),(.89,.43,.18),(.21,.50,.64)][theme])
    banks=[]
    for i in range(N):
        pp,pn=path[(i-5)%N],path[(i+5)%N]
        banks.append(max(-.33,min(.33,(pp[3]*pn[4]-pp[4]*pn[3])*.55)))
    length=grade_fords(path,banks,watershed)
    # Author the road first so terrain grading can respect its tightest bends.
    verts=[]
    for i,p in enumerate(path):
        x,y,z,dx,dz,half,_=p
        for side in [-1,1]:
            xx,zz=x+dz*half*side,z-dx*half*side
            yy=y+banks[i]*side
            verts.append((xx,yy,zz))
    # Short cross-road cells approximate the ruled surface between stations.
    # A single diagonal across 22 metres twists the whole carriageway when
    # its direction, width or banking changes. Keep edge indices stable for
    # shoulders and curbs, and track (station, lane) for all paint vertices.
    across=8
    coordinates=[(i,side*path[i][5]) for i in range(N) for side in [-1,1]]
    rows=[]
    for i,p in enumerate(path):
        left,right=verts[i*2:i*2+2];row=[i*2]
        for j in range(1,across):
            f=j/across;row.append(len(verts))
            verts.append(tuple(a+(b-a)*f for a,b in zip(left,right)))
            coordinates.append((i,p[5]*(2*f-1)))
        row.append(i*2+1);rows.append(row)
    faces=[(rows[i][j],rows[(i+1)%N][j],rows[(i+1)%N][j+1],rows[i][j+1])
           for i in range(N) for j in range(across)]
    ribbon=mesh('Elevated ribbon road',verts,faces,road)
    ribbon['drivable']=True
    for poly in ribbon.data.polygons:poly.use_smooth=True
    ribbon.data.calc_loop_triangles()
    road_points=KDTree(N)
    for i,p in enumerate(path):road_points.insert(Vector((p[0],0,p[2])),i)
    road_points.balance()
    # Grade shoulders from the actual ford vertices, including their crossfall.
    def graded_ground(x,z):
        y=watershed.ground(x,z)
        _,idx,_=road_points.find(Vector((x,0,z)))
        projections=[]
        for segment in ((idx-1)%N,idx):
            p,q=path[segment],path[(segment+1)%N]
            dx,dz=q[0]-p[0],q[2]-p[2]
            f=max(0,min(1,((x-p[0])*dx+(z-p[2])*dz)/(dx*dx+dz*dz)))
            center=tuple(a+(b-a)*f for a,b in zip(p,q))
            projections.append((math.hypot(center[0]-x,center[2]-z),segment,f,center))
        road_distance,idx,f,closest=min(projections)
        lane=(x-closest[0])*closest[4]-(z-closest[2])*closest[3]
        blend=max(0,min(1,(road_distance-closest[5]-2)/5))
        blend=blend*blend*(3-2*blend)
        left=verts[idx*2][1]+(verts[((idx+1)%N)*2][1]-verts[idx*2][1])*f
        right=verts[idx*2+1][1]+(verts[((idx+1)%N)*2+1][1]-verts[idx*2+1][1])*f
        grade=left+(right-left)*max(0,min(1,(lane/closest[5]+1)/2))-.22
        y=grade+(y-grade)*blend
        wet,level,flow=water_sample(x,z)
        if wet>-3:
            bank_blend=max(0,min(1,(wet+3)/3))
            road_blend=max(0,min(1,(road_distance-closest[5]-2)/3))
            # Keep gentle shoulders at the submerged ford, but do not let an
            # elevated road's verge dam a stream running alongside it.
            exposed=max(0,min(1,(road_distance-closest[5])/.5))
            raised=max(0,min(1,(grade-level-.1)/.5))
            road_blend+=(exposed-road_blend)*raised
            bed=level-max(-.15,min(.8,wet*.40))
            # Eroded beds and deposited banks follow the meandering profile.
            # Keep the pond's existing deeper floor, but seat moving channels
            # on a continuous bed even where a bend crosses a lower slope.
            target=bed if math.hypot(*flow)>.25 else min(y,bed)
            y+=(target-y)*bank_blend*road_blend
        return y
    vs=[(0,0,0)];rings=40;slices=192
    for r in range(1,rings+1):
        for j in range(slices):
            a=j*2*pi/slices;rad=r/rings
            vs.append((cos(a)*128*rad,0,sin(a)*126*rad))
    fs=[(0,1+(j+1)%slices,1+j) for j in range(slices)]
    for r in range(1,rings):
        for j in range(slices):
            a=1+(r-1)*slices+j;b=1+(r-1)*slices+(j+1)%slices;c=1+r*slices+(j+1)%slices;d=1+r*slices+j
            fs.extend([(a,b,c),(a,c,d)])
    vs,fs=refine_water_ground(vs,fs,water_sample)
    vs=[(x,graded_ground(x,z),z) for x,_,z in vs]
    # A coarse terrain face can span both sides of a hairpin. Cut only those
    # faces that intersect the actual road, keeping a shallow covered road bed.
    ground_bvh=BVHTree.FromPolygons([Vector(xyz(v)) for v in vs],fs,all_triangles=True)
    cuts=[0.0]*len(vs)
    for triangle in ribbon.data.loop_triangles:
        a,b,c=[ribbon.data.vertices[i].co for i in triangle.vertices]
        for sample in (a,b,c,(a+b)/2,(b+c)/2,(c+a)/2,(a+b+c)/3):
            hit,_,face,_=ground_bvh.ray_cast(sample+Vector((0,0,30)),Vector((0,0,-1)))
            if hit is not None:
                cut=max(0,hit.z-sample.z+.12)
                for vertex in fs[face]:cuts[vertex]=max(cuts[vertex],cut)
    vs=[(x,y-cuts[i],z) for i,(x,y,z) in enumerate(vs)]
    # Fill ford approaches with a support surface matching the pavement's
    # topology. Shared coarse terrain vertices cannot follow both sides of a
    # tight, dipped bend: cutting them alone leaves a void below the high side.
    # This buried soil surface joins the existing shoulder skirts at the edges.
    # Keep this local: distant tight hairpins can overlap themselves in XZ,
    # where an upper support sheet would protrude through the lower road.
    foundation={}
    for triangle in ribbon.data.loop_triangles:
        if max(water_sample(verts[i][0],verts[i][2])[0] for i in triangle.vertices)<=-12:continue
        face=[]
        for i in triangle.vertices:
            if i not in foundation:
                foundation[i]=len(vs);x,y,z=verts[i];vs.append((x,y-.14,z))
            face.append(foundation[i])
        fs.append(tuple(face))
    island=mesh('Sculpted island',vs,fs,grass)
    island['drivable']=True
    ground_bvh=BVHTree.FromPolygons([Vector(xyz(v)) for v in vs],fs,all_triangles=True)
    def ground_height(x,z):
        hit,_,_,_=ground_bvh.ray_cast(Vector((x,-z,100)),Vector((0,0,-1)))
        assert hit is not None,(name,x,z)
        return hit.z
    # Per-face palette variation gives the ground a hand-built faceted finish.
    for k in range(7):island.data.materials.append(mat(name+' ground tone '+str(k),tuple(min(1,v*(.985+k*.005)) for v in grass.diffuse_color[:3])))
    for poly in island.data.polygons:poly.material_index=random.randrange(1,8);poly.use_smooth=True
    for material in island.data.materials:material['paint']=-1-theme
    color_waterbed(island,water_sample,theme,grass.diffuse_color[:3])
    edgevs=[];edgefs=[]
    for j in range(slices):
        a=j*2*pi/slices;x=cos(a)*128;z=sin(a)*126
        y=graded_ground(x,z)
        edgevs.extend([(x,y,z),(x,-9 if theme==1 else -7,z)])
    for j in range(slices):edgefs.append((j*2,j*2+1,((j+1)%slices)*2+1,((j+1)%slices)*2))
    mesh('Exposed diorama edge',edgevs,edgefs,mat('Island edge '+name,[(.51,.37,.21),(.72,.59,.36),(.44,.62,.67)][theme]))
    road_bvh=BVHTree.FromPolygons([v.co for v in ribbon.data.vertices],
                                  [p.vertices[:] for p in ribbon.data.loop_triangles],all_triangles=True)
    build_water(name,theme,water_sample,watershed,ground_height,road_bvh)
    # Paint follows the road's exact elevation and banking.
    ribbon.data.calc_loop_triangles();surface=[]
    for triangle in ribbon.data.loop_triangles:
        segment=triangle.polygon_index//across;points=[]
        for vertex in triangle.vertices:
            i,lane=coordinates[vertex];s=length if segment==N-1 and i==0 else path[i][6]
            points.append((s,lane,*verts[vertex]))
        surface.append(points)
    # The shoulder meets the actual island mesh; no unsupported road skirt.
    for side in [-1,1]:
        bed=[]
        for i,p in enumerate(path):
            a=verts[i*2+(side+1)//2]
            x=a[0]+p[4]*1.5*side;z=a[2]-p[3]*1.5*side
            bed.extend([a,(x,ground_height(x,z)-.04,z)])
        # A narrow channel can lie between two shoulder vertices. Seat the
        # connecting edge below the terrain at intermediate samples as well.
        cuts=[0.]*N
        for i in range(N):
            a,b=bed[i*2+1],bed[((i+1)%N)*2+1]
            for f in [.25,.5,.75]:
                x,y,z=(u+(v-u)*f for u,v in zip(a,b))
                cut=max(0.,y-ground_height(x,z)+.015)
                cuts[i]=max(cuts[i],cut);cuts[(i+1)%N]=max(cuts[(i+1)%N],cut)
        for i,cut in enumerate(cuts):
            x,y,z=bed[i*2+1];bed[i*2+1]=(x,y-cut,z)
        shoulder=mesh('Sculpted road bed' ,bed,[(i*2,i*2+1,((i+1)%N)*2+1,((i+1)%N)*2) for i in range(N)],mat('Road foundation '+name,[(.41,.44,.31),(.68,.57,.36),(.66,.78,.80)][theme]))
        shoulder['drivable']=True
    # Curbs, dashed lane markings, trackside guardrail and reflectors.
    for i in range(0,N,2):
        p=path[i];q=path[(i+2)%N]
        for side in [-1,1]:
            a=verts[i*2+(side+1)//2];b=verts[((i+2)%N)*2+(side+1)//2]
            if max(water_sample(a[0],a[2])[0],water_sample(b[0],b[2])[0])>-1.2:continue
            vv=[a,b,(b[0]+q[4]*.72*side,b[1]+.1,b[2]-q[3]*.72*side),(a[0]+p[4]*.72*side,a[1]+.1,a[2]-p[3]*.72*side)]
            curb_mesh=mesh('Alternating safety curb',vv,[(0,1,2,3)],curb if (i//4)%2 else cream)
            curb_mesh['drivable']=True
        if i%8==0 and water_sample(p[0],p[2])[0]<-2:
            road_marking('Center dash',surface,length,p[6]-.825,p[6]+.825,-.075,.075,cream)
        if i%16==0 and water_sample(p[0],p[2])[0]<-2:
            for side in [-1,1]:
                xx=p[0]+p[4]*(p[5]+1.4)*side;zz=p[2]-p[3]*(p[5]+1.4)*side
                box('Reflector post',(xx,p[1]+.60,zz),(.22,1.2,.22),cream,.03)
                box('Reflector',(xx,p[1]+.95,zz),(.25,.17,.25),curb,.015)
    # Two extra dividers make four equal lanes on the broad carriageway.
    # Carry the dividers into the tapers until the extra lanes merge away.
    for i,p in enumerate(path):
        if i%8 or p[5]<width*.5+.6:continue
        for side in [-1,1]:
            lane=side*width*.5
            road_marking('Extra lane divider',surface,length,p[6]-.825,p[6]+.825,
                         lane-.075,lane+.075,cream)
    # Three raised wedges: identical exported collision metadata and actual mesh.
    ramps=[]
    for idx in [99,296,486]:
        p=path[idx];yaw=math.atan2(p[3],p[4]);length_r=5.8;height=1.40 if theme!=2 else 1.65
        def local(x,y,z):return(p[0]+cos(yaw)*x+sin(yaw)*z,p[1]+y,p[2]-sin(yaw)*x+cos(yaw)*z)
        vv=[local(x,y,z) for x,y,z in [(-3.9,0,-length_r/2),(3.9,0,-length_r/2),(-3.9,height,length_r/2),(3.9,height,length_r/2),(-3.9,0,length_r/2),(3.9,0,length_r/2)]]
        ramp_mesh=mesh('Jump ramp',vv,[(0,2,3,1),(0,4,2),(1,3,5),(2,4,5,3)],mat('Ramp deck',(.67,.42,.18)))
        ramp_mesh['drivable']=True
        for k in range(3):
            zz=-1.8+k*1.55
            for side in [-1,1]:
                a=local(side*2.6,(zz+length_r/2)/length_r*height+.035,zz-.4)
                b=local(0,(zz+.7+length_r/2)/length_r*height+.035,zz+.7)
                beam('Ramp chevron',a,b,.065,cream,5)
        ramps.append((float(idx),length_r,height,3.9))
    # Start gantry, grids and checker stripe are meshes, too.
    p=path[0];yaw=math.atan2(p[3],p[4])
    def startpos(x,y,z):return(p[0]+cos(yaw)*x+sin(yaw)*z,p[1]+y,p[2]-sin(yaw)*x+cos(yaw)*z)
    for side in [-1,1]:box('Finish gantry pillar',startpos(side*6.4,3.6,0),(.65,7.2,.65),dark,.08,yaw)
    box('Finish gantry board',startpos(0,7,0),(13.5,1.55,.7),orange,.13,yaw)
    for k in range(14):
        for j in range(2):
            box('Gantry checker',startpos(-5.9+k*.9,6.65+j*.65,.38),(.65,.52,.05),cream if (k+j)%2 else dark,0,yaw)
    half=width*.5-.12;cell=half*2/12
    for k in range(12):
        for j in range(2):
            road_marking('Finish road checker',surface,length,-(j+1)*.85+.008,-j*.85-.008,
                         -half+k*cell+.008,-half+(k+1)*cell-.008,cream if (k+j)%2 else dark)
    for row in range(4):
        # Match game_start's row spacing and lanes; the line sits ahead of the nose.
        s=-5-row*4.2+1.7
        for side in [-1,1]:
            road_marking('Starting grid',surface,length,s-.075,s+.075,side*2-1.2,side*2+1.2,cream)
    scenery_before=set(bpy.context.scene.objects)
    # Place vegetation only outside the racing surface.
    def nearest(x,z):return min(math.hypot(x-p[0],z-p[2])-p[5] for p in path[::4])
    placed=0
    for _ in range(900):
        x=random.uniform(-111,111);z=random.uniform(-108,108)
        if (x/119)**2+(z/118)**2>1 or nearest(x,z)<4.8:continue
        if (x+12)**2+(z+35)**2<190:continue
        if water_sample(x,z)[0]>-4:continue
        y=ground_height(x,z)-.05
        if placed<105:
            tree(x,y,z,random.uniform(.65,1.3),theme)
        else:
            ico('Granite boulder',(x,y+.6,z),(random.uniform(1.0,2.5),random.uniform(.9,2.5),random.uniform(1,2.3)),mat('Boulder '+name,[(.59,.60,.46),(.68,.57,.38),(.59,.72,.78)][theme]))
        placed+=1
        if placed>=145:break
    if theme!=1:
        barn(-10,terrain(-10,-36,theme),-36,0,theme)
        for i in range(9):
            x=-28+i*4;z=-53;y=terrain(x,z,theme)
            box('Fence post',(x,y+1,z),(.25,2,.25),wood,.025)
            if i<8:
                for h in [.65,1.45]:beam('Fence rail',(x,y+h,z),(x+4,terrain(x+4,z,theme)+h,z),.09,cream,5)
        if theme==0:
            for j in range(5):
                for i in range(7):
                    x=-29+i*2.1;z=24+j*3.2
                    box('Crop row',(x,terrain(x,z,theme)+.5,z),(.6,1.0,1.8),mat('Harvest gold',(.80,.64,.22)),.16)
            for x,z in [(15,-32),(21,-32),(18,-27)]:cone('Hay bale',(x,terrain(x,z,theme)+1,z),1.5,1.5,2,mat('Hay straw',(.80,.62,.29)),12)
        else:
            for x,z in [(-16,25),(-13,26),(-18,27)]:
                y=terrain(x,z,theme);ico('Snowman body',(x,y+1.3,z),(1.2,1.3,1.2),cream,2);ico('Snowman head',(x,y+2.95,z),(.78,.78,.78),cream,2)
                cone('Snowman hat',(x,y+3.65,z),.83,.83,.19,dark);cone('Hat top',(x,y+4,z),.51,.51,.65,dark)
    else:
        for x,z,col in [(-12,-35,(.92,.34,.19)),(3,-27,(.23,.59,.68)),(-20,25,(.91,.69,.25)),(25,31,(.92,.34,.19))]:
            y=terrain(x,z,theme);beam('Beach parasol pole',(x,y,z),(x,y+4,z),.1,cream)
            cone('Striped parasol',(x,y+4,z),3.5,0,.95,mat('Parasol '+str(col),col),12)
            box('Beach towel',(x+3,y+.08,z+3),(1.7,.08,3.5),cream,.04,yaw=.2)
            box('Beach lounger',(x-2,y+.55,z+2),(1.4,.2,2.8),wood,.06)
        box('Lifeguard hut',(-10,terrain(-10,42,theme)+2.7,42),(6,4,5),mat('Hut aqua',(.22,.58,.57)),.15)
        box('Lifeguard roof',(-10,terrain(-10,42,theme)+5,42),(7,.45,6),cream,.12)
    # Direction boards before selected bends.
    for idx in [45,150,240,345,410,555]:
        p=path[idx];xx=p[0]+p[4]*8;zz=p[2]-p[3]*8;yaw=math.atan2(p[3],p[4])
        box('Direction sign',(xx,p[1]+2,zz),(2.8,1.15,.18),orange,.08,yaw)
        box('Sign post',(xx,p[1]+.9,zz),(.16,1.8,.16),dark,.02)
    # Habitat-specific, clustered banks, not the same necklace of stones around
    # every body of water. Sand banks stay mostly bare; upland rock is coarser.
    bank_rng=random.Random(613+theme)
    x0,x1,z0,z1=watershed.bounds
    for _ in range(450):
        x=bank_rng.uniform(x0-2,x1+2);z=bank_rng.uniform(z0-2,z1+2)
        wet,level,_=water_sample(x,z)
        if not -1.8<wet<.55 or nearest(x,z)<2.8 or (x/124)**2+(z/122)**2>1:continue
        patch=sin(x*.19+z*.27)*sin(z*.13-x*.23)
        if theme==0 and patch<-.1:continue
        if theme==1 and bank_rng.random()<.78:continue
        y=ground_height(x,z)
        size=bank_rng.uniform(.22,.55) if theme!=2 else bank_rng.uniform(.35,.95)
        ico('Riverbank pebble',(x,y+size*.20,z),(size,size*.45,size*.72),
            mat('Wet river stones '+name,[(.40,.43,.34),(.57,.52,.38),(.46,.56,.59)][theme]),1 if theme==2 else 2)
        if theme==0 and wet<0 and patch>.05:
            for blade in range(bank_rng.randrange(5,10)):
                rx=x+bank_rng.uniform(-.35,.35);rz=z+bank_rng.uniform(-.35,.35)
                ry=ground_height(rx,rz)
                beam('Waterside reed',(rx,ry,rz),(rx+.12,ry+.6+bank_rng.random()*.6,rz+.1),
                     .035,mat('Reed green',(.35,.40,.16)),5)
    # Water emerges from a rocky spring / melting snow at the uphill head.
    sx,sz=watershed.source
    for _ in range([2,1,4][theme]):
        rx=sx+bank_rng.uniform(-1.,1.);rz=sz+bank_rng.uniform(-.5,1.)
        size=bank_rng.uniform(.55,.95) if theme!=2 else bank_rng.uniform(.85,1.7)
        ico('Spring head rock' if theme!=2 else 'Meltwater snowbank',
            (rx,ground_height(rx,rz)+size*.3,rz),(size,size*.55,size*.8),
            mat('Spring source '+name,[(.43,.46,.35),(.57,.53,.40),(.89,.95,.94)][theme]),2)
    obstacles=set(bpy.context.scene.objects)-scenery_before
    obstacles.update(o for o in scenery_before if o.name.startswith(('Finish gantry','Reflector')))
    build_spectators(path,theme,ground_height,obstacles,water_sample)
    from habitats import build as build_habitats
    build_habitats(globals(),theme,path,ground_height,water_sample,
                   obstacles | {o for o in bpy.context.scene.objects if 'spectator_group' in o})
    export(os.path.join(ROOT,'assets/tracks',name+'-vegetation.tcv'),vegetation=True)
    export(os.path.join(ROOT,'assets/tracks',name+'-wildlife.tcf'),wildlife=True)
    export(os.path.join(ROOT,'assets/tracks',name+'.tcm'))
    export(os.path.join(ROOT,'assets/tracks',name+'-crowd.tca'),crowd=True)
    export_surface(os.path.join(ROOT,'assets/tracks',name+'.tcs'))
    cans=[]
    for k,idx in enumerate([130,260,410,580]):cans.append((float(idx),(-1 if k%2 else 1)*3.8))
    with open(os.path.join(ROOT,'assets/tracks',name+'.tcp'),'wb') as f:
        f.write(b'TCP1');f.write(struct.pack('<IIIf',N,len(cans),len(ramps),length))
        for p in path:f.write(struct.pack('<7f',*p))
        for p in cans:f.write(struct.pack('<2f',*p))
        for p in ramps:f.write(struct.pack('<4f',*p))
    save(name)
    print('TRACK',name,'length',round(length,1),'elevation',round(max(p[1] for p in path)-min(p[1] for p in path),1),flush=True)

if __name__=='__main__':
    if '--tracks-only' not in sys.argv:build_car();build_can();build_helicopter()
    for theme,name in enumerate(['country','beach','winter']):build_track(theme,name)
    print('All Blender source scenes and runtime assets generated.',flush=True)
