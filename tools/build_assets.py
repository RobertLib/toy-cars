"""Run with Blender: blender -b --python tools/build_assets.py.

All visible game meshes, tracks and the editable source scenes are built in
Blender. Export is a deliberately small, versioned triangle/vertex-color format.
No Blender installation or Python interpreter is needed to play the game.
"""
import bpy, math, random, struct, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hydrology import Watershed
from railway import Railway, build_vehicles
from themes import THEMES, NAMES, spline_point
import biomes
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

def road_marking(name,surface,lap_length,start,end,left,right,material,
                 lane_shift=0,height=.035,geometry=None):
    """Clip paint to road triangles in (distance, lane) space, including the seam.

    Interpolating the original triangle vertices preserves the exact road height
    and banking, even where a marking crosses a bend or a triangle diagonal.
    """
    vertices,faces=geometry if geometry is not None else ([],[])
    for cycle in range(math.floor(start/lap_length),math.floor(end/lap_length)+1):
        lo=start-cycle*lap_length;hi=end-cycle*lap_length
        for triangle in surface:
            if max(v[0] for v in triangle)<=lo or min(v[0] for v in triangle)>=hi:continue
            polygon=list(triangle)
            for axis,bound,sign in [(0,lo,1),(0,hi,-1),(1,left,1),(1,right,-1)]:
                clipped=[]
                for previous,current in zip(polygon[-1:]+polygon[:-1],polygon):
                    pa=previous[axis]-(lane_shift*(previous[0]-lo)/(end-start) if axis==1 else 0)
                    pb=current[axis]-(lane_shift*(current[0]-lo)/(end-start) if axis==1 else 0)
                    a=(pa-bound)*sign>=0;b=(pb-bound)*sign>=0
                    if a!=b:
                        f=(bound-pa)/(pb-pa)
                        clipped.append(tuple(x+(y-x)*f for x,y in zip(previous,current)))
                    if b:clipped.append(current)
                polygon=clipped
            if len(polygon)<3:continue
            for k in range(1,len(polygon)-1):
                a,b,c=polygon[0],polygon[k],polygon[k+1]
                if abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))<1e-9:continue
                index=len(vertices)
                vertices.extend((v[2],v[3]+height,v[4]) for v in (a,b,c))
                faces.append((index,index+1,index+2))
    if geometry is None:return mesh(name,vertices,faces,material)

def tire_marks(surface,path,length,road,T):
    """Permanent paired skid marks, clipped onto the banked road below its paint."""
    theme=T.index
    rng=random.Random(928+theme)
    geometries=[([],[]) for _ in range(4)]
    # A few isolated marks on straights, with repeated braking/drifting lines
    # concentrated around the strongest changes of heading.
    spans=12 if T.legacy else max(12,int(round(12*length/1000)))
    starts=[8.]+[length*(i+.35)/spans+rng.uniform(-5,5) for i in range(1,spans)]
    runs=[(start,rng.uniform(8,21),rng.choice([-1,1])*rng.uniform(1.4,2.7),
           rng.uniform(-.65,.65)) for start in starts]
    bends=[]
    for i,p in enumerate(path):
        before,after=path[(i-8)%len(path)],path[(i+8)%len(path)]
        turn=math.atan2(before[3]*after[4]-before[4]*after[3],
                        before[3]*after[3]+before[4]*after[4])
        bends.append((abs(turn),turn,p[6]))
    chosen=[]
    for curvature,turn,distance in sorted(bends,reverse=True):
        if curvature<.18:break
        if any(min(abs(distance-other),length-abs(distance-other))<48 for other in chosen):continue
        chosen.append(distance)
        inside=-1 if turn>0 else 1
        for _ in range(rng.randint(6,9)):
            runs.append((distance-rng.uniform(10,18),rng.uniform(19,30),
                         inside*rng.uniform(.5,1.9),inside*rng.uniform(.3,1.2)))
        if len(chosen)==(7 if T.legacy else max(7,int(round(7*length/1000)))):break
    for run,(start,span,lane,drift) in enumerate(runs):
        # Keep the finish seam clear and give overlaps distinct surface layers.
        end=min(length-.5,start+span)
        start=max(.5,start);span=end-start
        if span<=0:continue
        count=math.ceil(span/.7)
        # Only inspect the local road triangles for each short ribbon.
        nearby=[tri for tri in surface if max(v[0] for v in tri)>start
                and min(v[0] for v in tri)<start+span]
        def offset(t):return lane+drift*sin(t*pi*.85)
        for i in range(count):
            a=i/count;b=(i+1)/count
            strength=min(1,a*6,(1-b)*6)
            shade=min(3,int(strength*4))
            width=.105*(.45+.55*strength)
            for wheel in [-.72,.72]:
                center=offset(a)+wheel
                road_marking('Tire marks',nearby,length,start+a*span,start+b*span,
                             center-width,center+width,None,
                             lane_shift=offset(b)-offset(a),height=.016+run%150*.00006,
                             geometry=geometries[shade])
    for shade,(vertices,faces) in enumerate(geometries):
        strength=.07+shade*.05
        color=tuple(c*(1-strength) for c in road.diffuse_color[:3])
        mesh('Tire marks '+str(shade),vertices,faces,
             mat('Worn rubber '+str(theme)+' '+str(shade),color))

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

# The garage's other cars share the rally car's footprint, so collisions and
# the chase camera fit them all. Lamp centres match CAR_LAMPS in renderer.c.
def wheels(x,y,z,radius,width,hub):
    for side in [-x,x]:
        for along in z:
            beam('All terrain tire',(side-width/2,y,along),(side+width/2,y,along),radius,rubber,16)
            beam('Recessed wheel hub',(side-width/2-.02,y,along),(side+width/2+.02,y,along),radius*hub,chrome,12)
            beam('Hub center',(side-width/2-.03,y,along),(side+width/2+.03,y,along),radius*.24,dark,10)

def build_sprite():
    clear()
    box('Rounded hatch body',(0,.50,0),(1.62,.50,2.6),paint,.22)
    box('Short hood',(0,.79,.84),(1.50,.16,.74),paint,.08)
    box('Bubble glazing',(0,1.08,-.16),(1.42,.70,1.62),glass,.25)
    box('Roof',(0,1.45,-.2),(1.44,.12,1.38),paint,.1)
    box('Roof stripe',(0,1.512,-.2),(.32,.014,1.18),cream,.003)
    box('Hood stripe',(0,.875,.86),(.32,.012,.62),cream,.003)
    box('Roof spoiler',(0,1.43,-.96),(1.30,.07,.22),dark,.03)
    wheels(.80,.38,[-.85,.85],.38,.28,.62)
    for x in [-.81,.81]:
        box('Side mirror',(x,1.0,.42),(.2,.14,.2),paint,.06)
        box('Door handle',(x,.86,-.2),(.06,.07,.2),cream,.015)
    box('Front bumper',(0,.40,1.32),(1.55,.20,.12),chrome,.05)
    box('Rear bumper',(0,.40,-1.32),(1.55,.20,.12),chrome,.05)
    box('Grille',(0,.58,1.30),(.5,.12,.05),dark,.02)
    for x in [-.55,.55]:
        box('Headlamp',(x,.74,1.30),(.32,.26,.06),cream,.06)
        box('Tail lamp',(x*1.09,.82,-1.31),(.22,.30,.05),orange,.025)
    box('Number plate',(0,.62,-1.35),(.42,.14,.04),cream,.01)
    export(os.path.join(ROOT,'assets/models/sprite.tcm'));save('sprite')

def build_bruiser():
    clear()
    box('Pickup chassis',(0,.70,0),(1.78,.52,3.15),paint,.16)
    box('Hood',(0,1.01,1.08),(1.66,.14,.92),paint,.07)
    box('Cab glazing',(0,1.27,.22),(1.58,.64,1.02),glass,.14)
    box('Cab roof',(0,1.63,.2),(1.62,.12,.98),paint,.08)
    box('Bed floor',(0,.97,-.86),(1.54,.04,1.42),dark)
    for x in [-.83,.83]:
        box('Bed side',(x,1.10,-.86),(.12,.30,1.44),paint,.03)
        box('Side mirror',(x*1.05,1.22,.82),(.16,.2,.18),dark,.04)
        beam('Roll bar post',(x*.86,.98,-.2),(x*.86,1.78,-.2),.05,dark)
    box('Tailgate',(0,1.10,-1.53),(1.66,.30,.1),paint,.03)
    beam('Roll bar',(-.72,1.78,-.2),(.72,1.78,-.2),.05,dark)
    for x in [-.45,-.15,.15,.45]:
        box('Light bar lamp',(x,1.84,-.18),(.22,.12,.1),cream,.02)
    wheels(.88,.52,[-1.05,1.05],.52,.36,.5)
    for z in [-1.05,1.05]:
        for x in [-.88,.88]:
            box('Wheel arch',(x,1.02,z),(.30,.08,1.08),dark,.03)
    box('Bull bar',(0,.66,1.64),(1.5,.14,.12),chrome,.04)
    for x in [-.55,.55]:
        beam('Bull bar upright',(x,.6,1.66),(x,1.08,1.66),.045,chrome)
    beam('Bull bar top',(-.55,1.08,1.66),(.55,1.08,1.66),.045,chrome)
    box('Grille',(0,.82,1.58),(.62,.22,.05),dark,.02)
    for x in [-.60,.60]:
        box('Headlamp',(x,.98,1.56),(.34,.22,.07),cream,.05)
        box('Tail lamp',(x*1.2,1.02,-1.58),(.2,.22,.05),orange,.02)
    box('Rear bumper',(0,.50,-1.60),(1.70,.18,.14),chrome,.04)
    export(os.path.join(ROOT,'assets/models/bruiser.tcm'));save('bruiser')

def build_comet():
    clear()
    box('Low coupe body',(0,.42,0),(1.82,.40,3.2),paint,.16)
    box('Long hood',(0,.65,.86),(1.70,.12,1.40),paint,.06)
    box('Fastback glazing',(0,.86,-.36),(1.36,.44,1.25),glass,.2)
    box('Roof',(0,1.07,-.42),(1.32,.09,.88),paint,.06)
    box('Rear deck',(0,.69,-1.2),(1.74,.14,.72),paint,.06)
    for x in [-.3,.3]:
        box('Hood stripe',(x,.715,.86),(.16,.012,1.3),cream,.003)
        box('Roof stripe',(x,1.118,-.42),(.13,.012,.8),cream,.002)
        box('Deck stripe',(x,.765,-1.2),(.16,.012,.62),cream,.003)
    box('Rear wing',(0,1.06,-1.46),(1.95,.07,.38),dark,.03)
    for x in [-.6,.6]:
        box('Wing strut',(x,.88,-1.42),(.07,.30,.1),dark)
    box('Front splitter',(0,.24,1.62),(1.80,.06,.2),dark,.02)
    for x in [-.92,.92]:
        box('Side skirt',(x,.27,0),(.06,.1,2.0),dark,.02)
        box('Side mirror',(x*.86,.9,.28),(.2,.11,.18),paint,.05)
        box('Side intake',(x*1.0,.48,-.62),(.03,.16,.42),dark,.01)
    wheels(.86,.37,[-1.08,1.08],.37,.3,.7)
    for x in [-.62,.62]:
        box('Headlamp',(x,.60,1.58),(.42,.12,.06),cream,.03)
        box('Tail lamp',(x*.89,.62,-1.60),(.5,.1,.05),orange,.02)
    for x in [-.3,.3]:
        beam('Exhaust',(x,.32,-1.55),(x,.32,-1.68),.07,chrome)
    box('Number plate',(0,.46,-1.62),(.42,.13,.04),cream,.01)
    export(os.path.join(ROOT,'assets/models/comet.tcm'));save('comet')

def build_cars():
    build_car();build_sprite();build_bruiser();build_comet()

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

# Layouts, terrain and palettes live in themes.py: the country infield weave,
# three coastal lobes, alpine switchbacks and the seven later circuits.
def spline(t,T):
    return spline_point(T.knots,t)

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

def spectator(position,yaw,T,kind,pose,rng,ground_height,group):
    style=T.outfit
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
        # Later circuits seat the sole at its centre, which matters on steep ground.
        foot=point((side*.18,0,.04 if T.legacy else .07));floor=(ground_height(foot[0],foot[2])-position[1])/scale
        block((side*.18,floor+.09,.07),(.23,.18,.40),rubber)
        knee=(side*.18,.55,.015);hip=(side*.15,.95,0)
        limb((side*.18,floor+.17,.03),knee,.095,skin if style in ('beach','tropic') else trousers)
        limb(knee,hip,.12,trousers)
    block((0,1.23,0),(.58*width,.64,.34),shirt)
    if style=='winter':
        block((0,1.47,.02),(.65*width,.13,.40),orange)
        block((.16,1.27,.20),(.12,.35,.055),orange)
    limb((0,1.50,0),(0,1.64,0),.105,skin)
    head((0,1.80,.015),(.22,.27,.22),skin)
    block((0,2.015,-.025),(.40,.13,.34),hair)
    # A nose and dark glasses make the direction of attention readable.
    block((0,1.80,.245),(.09,.10,.09),skin)
    if style in ('beach','tropic','desert') or pose=='camera':block((0,1.88,.225),(.37,.065,.045),dark)
    hat=rng.randrange(3)
    if style=='winter':
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
        limb(elbow,hand,.08,shirt if style=='winter' else skin)
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

def build_spectators(path,T,ground_height,obstacles,water_sample,sites):
    theme=T.index;rng=random.Random(920+theme)
    # Hair materials are named by body type; drop the previous circuit's so a
    # full build and a single-track build choose the same colors.
    for key in [key for key in materials if key.startswith('Spectator hair')]:del materials[key]
    bpy.context.view_layer.update()
    bounds=[]
    for obj in obstacles:
        if obj.type!='MESH':continue
        corners=[eng(obj.matrix_world@Vector(p)) for p in obj.bound_box]
        bounds.append((min(p[0] for p in corners)-.85,max(p[0] for p in corners)+.85,
                       min(p[2] for p in corners)-.85,max(p[2] for p in corners)+.85))
    occupied=[];counts=[]
    # Unevenly spaced viewing spots, on both sides and around all three jumps.
    for group,idx in enumerate(sites):
        side=-1 if group%3==0 else 1;count=0;wanted=[7,4,6,5,3,8][group%6]
        for attempt in range(160):
            if count==wanted:break
            # A crowded or steep viewing spot can spread along the bend or
            # use the opposite shoulder rather than intersecting scenery.
            spread=7 if attempt<80 else 16
            p=path[(idx+int(rng.uniform(-spread,spread)))%len(path)]
            lane=side*(-1 if attempt>=120 else 1)*(p[5]+rng.uniform(3.2,6.6))
            x,z=p[0]+p[4]*lane,p[2]-p[3]*lane
            if not T.within(x,z,124,122):continue
            if water_sample(x,z)[0]>-2:continue
            if any(a<x<b and c<z<d for a,b,c,d in bounds):continue
            if any(math.hypot(x-a,z-b)<1.65 for a,b in occupied):continue
            near=min(path,key=lambda q:(q[0]-x)**2+(q[2]-z)**2)
            if math.hypot(near[0]-x,near[2]-z)<near[5]+2.6:continue
            # Later circuits widen and narrow their roads and pass other levels
            # nearby: keep clear of every station.
            if not T.legacy and any(math.hypot(q[0]-x,q[2]-z)<q[5]+2.6 for q in path):continue
            y=ground_height(x,z)
            if max(abs(ground_height(x+dx,z+dz)-y) for dx,dz in [(-.4,0),(.4,0),(0,-.4),(0,.4)])>.24:continue
            yaw=math.atan2(near[0]-x,near[2]-z)+rng.uniform(-.18,.18)
            kind=['adult','child','broad','senior','tall'][(count+group)%5]
            pose=['flag','cheer','wave','camera','clap'][(count+group*2)%5]
            spectator((x,y,z),yaw,T,kind,pose,rng,ground_height,group)
            occupied.append((x,z));count+=1
        assert count>=3,('Too little room for spectator group',theme,group,count)
        counts.append(count)
    print('SPECTATORS',theme,sum(counts),'in',len(counts),'groups',counts,flush=True)

def color_waterbed(island,sample,T,grass):
    # Interpolated vertex colors keep the shoreline organic even on coarse
    # terrain triangles; assigning whole dark faces makes a saw-toothed bank.
    # Later themes can also tint the ground itself, such as sandstone strata.
    old=island.data.color_attributes.get('Riverbed')
    if old:island.data.color_attributes.remove(old)
    colors=island.data.color_attributes.new(name='Riverbed',type='FLOAT_COLOR',domain='POINT')
    bed=T.colors['bed']
    tint=getattr(T,'tint',None)
    for i,vertex in enumerate(island.data.vertices):
        x,y,z=eng(vertex.co);wet,level,_=sample(x,z)
        base=tint(x,y,z) if tint else grass
        blend=max(0,min(1,(wet+1.5)/3))*max(0,min(1,(level+.6-y)/.8))
        blend=blend*blend*(3-2*blend)
        colors.data[i].color=(*(a+(b-a)*blend for a,b in zip(base,bed)),1)

def build_water(T,sample,layout,ground_height,road_bvh):
    name=T.key;W=T.water
    vertices=[];faces=[];payload=[]
    def vertex(wx,wz):
        edge,level,flow=sample(wx,wz)
        if T.within(wx,wz,128*.99**.5,126*.99**.5):
            bed=ground_height(wx,wz)
            hit,_,_,_=road_bvh.ray_cast(Vector((wx,-wz,level+2)),Vector((0,0,-1)))
            if hit is not None:bed=max(bed,hit.z+.035)
        else:bed=-8
        return (wx,level,wz,min(edge,level-bed),flow[0],flow[1])
    def emit(tri,ocean=False):
        if T.sea and not ocean and max(q[1] for q in tri)<=-6.849:return
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
            if T.legacy:
                emit([a,c,b]);emit([a,d,c]);continue
            # A fan round the cell's centre keeps channels narrower than a cell.
            e=vertex(x+step/2,z+step/2)
            emit([a,e,b]);emit([b,e,c]);emit([c,e,d]);emit([d,e,a])
    if T.sea:
        # Ocean replaces the old opaque turquoise plinth. Its centre is hidden
        # by the island; the inner edge extends under the carved river mouth.
        segments=192 if T.legacy else 288
        for i in range(segments):
            a=i*2*pi/segments;b=(i+1)*2*pi/segments
            def radii(t):
                if T.legacy:return 118,145
                edge=T.edge_radius(t);return edge-12,edge+26
            q=[(cos(t)*r,-6.85,sin(t)*r,3.,.18,.08) for t,r in
               [(a,radii(a)[0]),(a,radii(a)[1]),(b,radii(b)[1]),(b,radii(b)[0])]]
            emit([q[0],q[2],q[1]],True);emit([q[0],q[3],q[2]],True)
    obj=mesh(W['title'],vertices,faces,mat('Water preview',(.12,.36,.33)))
    obj['water']=True
    obj['water_route']=[v for p in layout.route for v in p]
    obj['water_source']=layout.source
    obj['water_destination']=W['destination']
    obj['water_character']=W['character']
    if layout.lake_level is not None:obj['water_lake_level']=layout.lake_level
    with open(os.path.join(ROOT,'assets/tracks',name+'-water.tcm'),'wb') as f:
        f.write(b'TCM1');f.write(struct.pack('<I',len(payload)//10));f.write(struct.pack('<%sf'%len(payload),*payload))
    print('WATER',name,len(payload)//30,'triangles',flush=True)

def refine_water_ground(vs,fs,sample,limits=(1.35,1.35)):
    for limit in limits:
        edges=set()
        for face in fs:
            for a,b in zip(face,face[1:]+face[:1]):
                p,q=vs[a],vs[b]
                if math.hypot(p[0]-q[0],p[2]-q[2])<=limit:continue
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
def road_half_width(t,T):
    entry,start,end,exit=T.four_lane
    base=T.half
    if t<=entry or t>=exit:return base
    if start<=t<=end:return base*2
    u=(t-entry)/(start-entry) if t<start else (exit-t)/(exit-end)
    # Mostly straight tapers with eased joins to the constant-width sections.
    ease=.15
    if u<ease:blend=u*u/(2*ease)
    elif u>1-ease:blend=1-ease-(1-u)*(1-u)/(2*ease)
    else:blend=u-ease/2
    return base*(1+blend/(1-ease))

def passing_section(xz,heights,total,minimum):
    """The four-lane section of a later circuit, as lap fractions (entry,
    full, end, exit): the longest stretch away from the start line and the
    bridge approaches whose bends stay wider than 40 m (down to 20 m on the
    twistiest layouts) and climb gently, at most a quarter of the lap. Fixed fractions would
    run the wide road through hairpins."""
    n=len(xz);step=total/n;k=max(2,int(round(5/step)))
    def heading(i):
        a,b=xz[(i-1)%n],xz[(i+1)%n];return math.atan2(b[0]-a[0],b[1]-a[1])
    turns=[abs((heading((i+k)%n)-heading((i-k)%n)+pi)%(2*pi)-pi)/(2*k*step) for i in range(n)]
    grades=[abs(heights[(i+k)%n]-heights[(i-k)%n])/(2*k*step) for i in range(n)]
    # Bridge approaches climb: a wide road there would twist on its bends.
    crossings=[]
    for i in range(n):
        (ax,az),(bx,bz)=xz[i],xz[(i+1)%n]
        for j in range(i+3,n-(i==0)):
            (cx,cz),(ex,ez)=xz[j],xz[(j+1)%n]
            det=(bx-ax)*(ez-cz)-(bz-az)*(ex-cx)
            if abs(det)<1e-9:continue
            u=((cx-ax)*(ez-cz)-(cz-az)*(ex-cx))/det;v=((cx-ax)*(bz-az)-(cz-az)*(bx-ax))/det
            if 0<u<1 and 0<v<1:crossings+=[i,j]
    def clear(i):
        return all(min(abs(i-c),n-abs(i-c))*step>110 for c in crossings)
    for radius in (40,30,25,20):
        # A bend that also climbs twists a wide carriageway: at 40 m radius
        # it may climb 14%, at 20 m only 7%.
        gentle=[turns[i]<1/radius and turns[i]*grades[i]<.0035 and 50<i*step<total-50 and clear(i)
                for i in range(n)]
        best=(0,0);i=0
        while i<n:
            if not gentle[i]:i+=1;continue
            j=i
            while j+1<n and gentle[j+1]:j+=1
            if j-i>best[1]-best[0]:best=(i,j)
            i=j+1
        first,last=best[0]*step,min(best[1]*step,best[0]*step+total*.25)
        if last-first>=70+minimum:break
    # Tapers of 35 m, shortened (to 20 m at most) to keep the theme's minimum
    # at full width.
    taper=min(35.,(last-first-minimum)/2)
    assert taper>=20,('No straight for the passing section',last-first)
    return (first/total,(first+taper)/total,(last-taper)/total,last/total)

def centreline(T):
    """Stations (x, y, z, dx, dz, half width, distance) before grading.

    The first circuits place their 640 stations evenly in the spline parameter
    and follow the terrain directly. Later circuits space stations evenly along
    the road and smooth the profile, the way a road is cut and filled."""
    path=[];distance=0
    if T.legacy:
        N=T.stations
        for i in range(N):
            t=i/N;x,z=spline(t,T);x2,z2=spline((t+.0001)%1,T);d=Vector((x2-x,0,z2-z)).normalized()
            y=T.terrain(x,z)+.15
            if i:distance+=Vector((x-path[-1][0],y-path[-1][1],z-path[-1][2])).length
            path.append((x,y,z,d.x,d.z,road_half_width(t,T),distance))
        return path
    dense=[spline(i/(len(T.knots)*40),T) for i in range(len(T.knots)*40)]
    lengths=[0.]
    for a,b in zip(dense,dense[1:]+dense[:1]):lengths.append(lengths[-1]+math.dist(a,b))
    total=lengths[-1];N=T.stations or int(round(total/1.6))
    xz=[];k=0
    for i in range(N):
        s=total*i/N
        while lengths[k+1]<s:k+=1
        a,b=dense[k],dense[(k+1)%len(dense)];f=(s-lengths[k])/max(1e-9,lengths[k+1]-lengths[k])
        xz.append((a[0]+(b[0]-a[0])*f,a[1]+(b[1]-a[1])*f))
    raw=[T.terrain(x,z) for x,z in xz]
    # A circular Gaussian over the stations removes bumps a car would launch off.
    sigma=T.smooth/(total/N);reach=int(sigma*3)+1
    weights=[math.exp(-.5*(k/sigma)**2) for k in range(-reach,reach+1)]
    heights=[sum(w*raw[(i+k)%N] for k,w in zip(range(-reach,reach+1),weights))/sum(weights) for i in range(N)]
    # Cut and fill keep every climb within the theme's grade: each pair of
    # neighbours that is too steep moves symmetrically towards the limit.
    limit=T.max_grade*total/N
    for _ in range(4000):
        worst=0.
        for i in range(N):
            j=(i+1)%N;excess=abs(heights[j]-heights[i])-limit
            if excess>0:
                shift=math.copysign(excess*.5,heights[j]-heights[i])
                heights[i]+=shift;heights[j]-=shift;worst=max(worst,excess)
        if worst<1e-4:break
    # Round off the corners where a climb meets its limit; averaging keeps it.
    sigma=5/(total/N);reach=int(sigma*3)+1
    weights=[math.exp(-.5*(k/sigma)**2) for k in range(-reach,reach+1)]
    heights=[sum(w*heights[(i+k)%N] for k,w in zip(range(-reach,reach+1),weights))/sum(weights) for i in range(N)]
    T.four_lane=passing_section(xz,heights,total,T.passing)
    for i in range(N):
        (x,z),(x2,z2)=xz[i],xz[(i+1)%N];(x0,z0)=xz[i-1]
        d=Vector((x2-x0,0,z2-z0)).normalized();y=heights[i]+.15
        if i:distance+=Vector((x-path[-1][0],y-path[-1][1],z-path[-1][2])).length
        path.append((x,y,z,d.x,d.z,road_half_width(i/N,T),distance))
    return path

def grade_fords(path,banks,watershed,ramps):
    # Only a stream actually crossing the pavement creates a ford. Proximity
    # to a pond or a bank must not carve independent holes into either edge.
    wet=[]
    bridge_lift,_=grade_bridges(path.copy(),watershed.T.legacy)
    for i,p in enumerate(path):
        if bridge_lift[i]>.05:continue
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
        ramp_distances=[path[i][6] for i,_ in ramps]
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

def grade_bridges(path,legacy=True):
    """Raise the earlier branch at each crossing, with smooth 55 m approaches.
    Later circuits lengthen the approaches with the lift, keeping its own
    grade below about 24%."""
    n=len(path);crossings=[]
    for i,a in enumerate(path):
        b=path[(i+1)%n];dx,dz=b[0]-a[0],b[2]-a[2]
        for j in range(i+3,n):
            c,d=path[j],path[(j+1)%n];ex,ez=d[0]-c[0],d[2]-c[2]
            det=dx*ez-dz*ex
            if abs(det)<1e-8:continue
            u=((c[0]-a[0])*ez-(c[2]-a[2])*ex)/det
            v=((c[0]-a[0])*dz-(c[2]-a[2])*dx)/det
            if 0<u<1 and 0<v<1:crossings.append((i,j))
    assert crossings, "Each circuit needs a real bridge crossing"
    lift=[0.]*n
    rise=max(9.,max(path[lower][1]-path[upper][1]+9. for upper,lower in crossings))
    approach=55. if legacy else max(55.,rise*1.875/.24)
    for upper,lower in crossings:
        # A broad level span clears the entire lower carriageway, including verges.
        start=path[upper][6]-24;end=path[upper][6]+24
        for i,p in enumerate(path):
            u=max(0.,1-max(start-p[6],p[6]-end,0)/approach)
            blend=u*u*u*(10-15*u+6*u*u)
            lift[i]+= (rise-lift[i])*blend
    distance=0.
    for i,p in enumerate(path):
        q=(p[0],p[1]+lift[i],*p[2:6])
        if i:distance+=math.dist(q[:3],path[i-1][:3])
        path[i]=(*q,distance)
    return lift,distance+math.dist(path[-1][:3],path[0][:3])

def island_grid(T):
    """Flat terrain grid before grading: vertices, triangles and the outline.

    The first circuits use a polar grid of 40 rings and 192 spokes. Later
    islands follow their superellipse outline with rings about 3.4 m apart,
    and each ring has as many vertices as its circumference needs."""
    if T.legacy:
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
        outline=[(cos(j*2*pi/192)*128,sin(j*2*pi/192)*126) for j in range(192)]
        return vs,fs,outline
    big=max(T.radius);rings=int(round(big/3.4))
    vs=[(0,0,0)];ring_start=[0];ring_count=[1]
    for r in range(1,rings+1):
        count=max(8,int(round(2*pi*big*r/rings/4.6)))
        ring_start.append(len(vs));ring_count.append(count)
        for j in range(count):
            a=j*2*pi/count;rad=T.edge_radius(a)*r/rings
            vs.append((cos(a)*rad,0,sin(a)*rad))
    fs=[]
    for r in range(1,rings+1):
        inner,outer=ring_start[r-1],ring_start[r];ni,no=ring_count[r-1],ring_count[r]
        if r==1:
            fs.extend((0,outer+(j+1)%no,outer+j) for j in range(no));continue
        # Zip two rings of different counts together by angle.
        i=j=0
        while i<ni or j<no:
            ai=(i+1)/ni;aj=(j+1)/no
            if j<no and (i>=ni or aj<=ai):
                fs.append((inner+i%ni,outer+(j+1)%no,outer+j));j+=1
            else:
                fs.append((inner+i%ni,inner+(i+1)%ni,outer+j%no));i+=1
    outline=[(vs[ring_start[rings]+j][0],vs[ring_start[rings]+j][2]) for j in range(ring_count[rings])]
    return vs,fs,outline

def crossing_lift(path,legacy=True):
    """Bridge lift of each station, from the horizontal layout alone."""
    return grade_bridges([p[:6]+(p[6],) for p in path],legacy)[0]

def straightness(path,i,before,after):
    """Heading change and vertical curvature around station i, lower is straighter."""
    n=len(path);step=path[1][6]-path[0][6]
    a,b=int(before/step),int(after/step)
    turn=0.;h=None
    for k in range(-a,b+1,2):
        p=path[(i+k)%n];q=path[(i+k+2)%n]
        heading=math.atan2(q[0]-p[0],q[2]-p[2])
        if h is not None:turn+=abs((heading-h+pi)%(2*pi)-pi)
        h=heading
    bump=sum(abs(path[(i+k-4)%n][1]-2*path[(i+k)%n][1]+path[(i+k+4)%n][1]) for k in range(-a,b+1,4))
    return turn+bump*.4

def spread_sites(T,path,count,phase,avoid,before,after,gap,keep_clear=(),open_road=False,
                 start=60,end=40,span=(0.,1.)):
    """Stations spread evenly over a share of the lap (all of it by default),
    each on the straightest nearby stretch."""
    n=len(path);length=path[-1][6];step=length/n
    lift=crossing_lift(path,T.legacy)
    rail=T.rail_station(path)
    chosen=[]
    def site(target,window):
        best=None
        for i in range(n):
            s=path[i][6]
            off=abs(s-target);off=min(off,length-off)
            if off>length*(span[1]-span[0])*window/count:continue
            if s<start or s>length-end:continue
            if not open_road and max(lift[(i+k)%n] for k in range(-int(15/step),int(30/step)+1,2))>.05:continue
            rail_gap=abs(s-path[rail][6]);rail_gap=min(rail_gap,length-rail_gap)
            if rail_gap<avoid:continue
            if not open_road and path[i][5]>T.half+.01:continue
            if any(min(abs(s-c),length-abs(s-c))<gap for c in chosen):continue
            if any(min(abs(s-c),length-abs(s-c))<35 for c in keep_clear):continue
            score=straightness(path,i,before,after)+off/length*2
            if best is None or score<best[0]:best=(score,i,s)
        return best
    for j in range(count):
        target=length*(span[0]+(span[1]-span[0])*(((j+phase)/count)%1))
        # Prefer a site close to the even spacing; widen only if none is clear.
        best=site(target,.2) or site(target,.5) or site(target,1.)
        assert best,('No site for a ramp or pickup',T.key,j)
        chosen.append(best[2])
    return sorted(int(round(path_index)) for path_index in
                  (min(range(n),key=lambda i:abs(path[i][6]-c)) for c in chosen))

def pick_rail_site(T,path):
    """The straightest station near the authored share of the lap whose line,
    tunnels included, stays clear of every other part of the road. Wider
    searches and other crossing angles are tried if the first finds nothing."""
    n=len(path);length=path[-1][6];step=length/n
    lift=crossing_lift(path,T.legacy)
    site=T.rail;target=site['fraction']*length
    tunnel=0 if site['kind']=='road' else 14
    coarse=path[::3]
    # Tunnel hills ease down beside graded roads but not beside bridge decks,
    # so their whole dome must keep clear of every raised approach.
    decks=[q for q,l in zip(coarse,lift[::3]) if l>.05]
    domes=[] if site['kind']=='road' else [
        (side*(site['after'] if side>0 else site['before'])+side*k,d)
        for side in (-1,1) for k in range(-11,28,3) for d in (-14,-7,0,7,14)]
    def clear_line(i,angle):
        p=path[i];s=p[6];heading=math.atan2(p[3],p[4])+math.radians(angle)
        ux,uz=math.sin(heading),math.cos(heading)
        if site['kind']=='road':
            # The promenade runs along the line with a turning loop at each end.
            reach=[(t,d) for t in range(-int(site['before'])-14,int(site['after'])+15,2) for d in (-6,0,6)]
        else:
            reach=[(t,0) for t in range(-int(site['before'])-tunnel,int(site['after'])+tunnel+1,2)]
        for t,d in reach:
            x=p[0]+ux*t+uz*d;z=p[2]+uz*t-ux*d
            if not T.within(x,z,112,110):return False
            # The crossing road itself only needs to clear the portals and
            # tunnels; a bend may otherwise bring it right up to a portal.
            portal=tunnel and abs(t)>=(site['before'] if t<0 else site['after'])-6
            for q in coarse:
                gap=abs(q[6]-s);gap=min(gap,length-gap)
                if not portal and gap<abs(t)/max(.3,abs(math.sin(math.radians(angle))))+20:continue
                if math.hypot(q[0]-x,q[2]-z)<q[5]+(12 if portal else 9):return False
        for t,d in domes:
            x=p[0]+ux*t+uz*d;z=p[2]+uz*t-ux*d
            if any(math.hypot(q[0]-x,q[2]-z)<q[5]+4 for q in decks):return False
        return True
    for window,angles in ((site.get('window',.12),(site['angle'],)),(.5,(site['angle'],100,80,120,65,135))):
        best=None
        for i in range(n):
            s=path[i][6];off=abs(s-target);off=min(off,length-off)
            if off>length*window or s<80 or s>length-50:continue
            if max(lift[(i+k)%n] for k in range(-int(60/step),int(60/step)+1,3))>.05:continue
            if any(path[(i+k)%n][5]>T.half+.01 for k in range(-int(40/step),int(40/step)+1,3)):continue
            score=straightness(path,i,40,40)+off/length*3
            if best is not None and score>=best[0]:continue
            for angle in angles:
                if clear_line(i,angle):
                    best=(score,i,angle);break
        if best:break
    assert best,('No clear site for the level crossing',T.key)
    site['angle']=best[2]
    print('RAIL SITE',T.key,'station',best[1],'distance',round(path[best[1]][6]),'angle',best[2],flush=True)
    return best[1]

def choose_cans(T,path,ramps):
    if T.legacy:return T.cans
    return spread_sites(T,path,T.can_count,.08,45,25,15,T.can_spacing*path[-1][6]/T.can_count,
                        [path[i][6] for i,_ in ramps],True)

def choose_ramps(T,path):
    if T.legacy:return T.ramps
    # Jumps share the last three quarters of the lap, once the grid has
    # spread out, and stay clear of the finish line.
    sites=spread_sites(T,path,T.ramp_count,.5,70,32,30,120,start=150,end=80,
                       span=(.26,.97))
    return [(i,T.ramp_height) for i in sites]

def build_track(T):
    theme=T.index;name=T.key;C=T.colors
    clear();random.seed(185+theme)
    width=2*T.half
    watershed=Watershed(T)
    water_sample=watershed.sample
    path=centreline(T);N=len(path)
    if not T.legacy and 'index' not in T.rail:
        T.rail['index']=pick_rail_site(T,path)
    grass=mat(C['grass_name'],C['grass'])
    road=mat('Road '+name,C['road'])
    curb=mat('Curb '+name,C['curb'])
    banks=[]
    for i in range(N):
        # Crossfall follows the heading change over about 16 m of road.
        k=5 if T.legacy else max(3,int(round(8/(path[1][6]-path[0][6]))))
        pp,pn=path[(i-k)%N],path[(i+k)%N]
        banks.append(max(-T.bank_limit,min(T.bank_limit,(pp[3]*pn[4]-pp[4]*pn[3])*T.bank)))
    if not T.legacy:
        # Ease the crossfall in and out over about 6 m, so the wide passing
        # sections and their tapers do not twist between stations.
        step=path[1][6]-path[0][6];sigma=6/step;reach=int(sigma*3)+1
        weights=[math.exp(-.5*(k/sigma)**2) for k in range(-reach,reach+1)]
        banks=[sum(w*banks[(i+k)%N] for k,w in zip(range(-reach,reach+1),weights))/sum(weights) for i in range(N)]
    ramps=choose_ramps(T,path)
    length=grade_fords(path,banks,watershed,ramps)
    bridge_lift,length=grade_bridges(path,T.legacy)
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
    across=8 if T.legacy else 12
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
    ribbon['bridge_lift']=bridge_lift
    for poly in ribbon.data.polygons:poly.use_smooth=True
    ribbon.data.calc_loop_triangles()
    # A level crossing: the rail bed and tunnel hills grade the terrain below.
    rail=Railway(globals(),T,path,ribbon,length)
    road_points=KDTree(N)
    for i,p in enumerate(path):
        if bridge_lift[i]<.05:road_points.insert(Vector((p[0],0,p[2])),i)
    road_points.balance()
    # Grade shoulders from the actual ford vertices, including their crossfall.
    def graded_ground(x,z,rail_grading=True):
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
        if not T.legacy:
            # Cuttings and embankments of the hillier islands stay below
            # about 30 degrees, so a car leaving the road meets drivable ground.
            reach=.55*max(0,road_distance-closest[5]-1.5)
            y=grade+max(-reach,min(reach,y-grade))
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
        return rail.grade(x,z,y,road_distance-closest[5],wet) if rail_grading else y
    rail.prepare(lambda x,z:graded_ground(x,z,False))
    vs,fs,outline=island_grid(T)
    # The later islands also carve the trickle below a spring at 0.7 m.
    vs,fs=refine_water_ground(vs,fs,water_sample,(1.35,1.35) if T.legacy else (1.35,1.35,.7))
    vs,fs=rail.refine(vs,fs)
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
        if bridge_lift[triangle.polygon_index//across]>.05:continue
        if max(water_sample(verts[i][0],verts[i][2])[0] for i in triangle.vertices)<=-12:continue
        face=[]
        for i in triangle.vertices:
            if i not in foundation:
                foundation[i]=len(vs);x,y,z=verts[i];vs.append((x,y-.14,z))
            face.append(foundation[i])
        fs.append(tuple(face))
    vs,fs,tunnel_faces=rail.darkness(vs,fs)
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
    for material in island.data.materials:material['paint']=-T.ground
    color_waterbed(island,water_sample,T,grass.diffuse_color[:3])
    # The hillside closing each tunnel reads as its dark interior.
    if tunnel_faces:
        dark_index=len(island.data.materials)
        island.data.materials.append(mat('Tunnel darkness',(.025,.025,.03)))
        split=set(tunnel_faces)
        colors=island.data.color_attributes['Riverbed']
        for i in split:colors.data[i].color=(.025,.025,.03,1)
        for poly in island.data.polygons:
            if set(poly.vertices)<=split:poly.material_index=dark_index
    edgevs=[];edgefs=[];slices=len(outline)
    for x,z in outline:
        y=graded_ground(x,z)
        edgevs.extend([(x,y,z),(x,T.edge_depth,z)])
    for j in range(slices):edgefs.append((j*2,j*2+1,((j+1)%slices)*2+1,((j+1)%slices)*2))
    mesh('Exposed diorama edge',edgevs,edgefs,mat('Island edge '+name,C['edge']))
    road_bvh=BVHTree.FromPolygons([v.co for v in ribbon.data.vertices],
                                  [p.vertices[:] for p in ribbon.data.loop_triangles
                                   if bridge_lift[p.polygon_index//across]<.05],all_triangles=True)
    build_water(T,water_sample,watershed,ground_height,road_bvh)
    # Paint follows the road's exact elevation and banking.
    ribbon.data.calc_loop_triangles();surface=[]
    for triangle in ribbon.data.loop_triangles:
        segment=triangle.polygon_index//across;points=[]
        for vertex in triangle.vertices:
            i,lane=coordinates[vertex];s=length if segment==N-1 and i==0 else path[i][6]
            points.append((s,lane,*verts[vertex]))
        surface.append(points)
    tire_marks(surface,path,length,road,T)
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
        shoulder=mesh('Sculpted road bed' ,bed,[(i*2,i*2+1,((i+1)%N)*2+1,((i+1)%N)*2) for i in range(N) if max(bridge_lift[i],bridge_lift[(i+1)%N])<.05],mat('Road foundation '+name,C['foundation']))
        shoulder['drivable']=True
    # Exposed bridge deck and side girders leave the lower road completely open.
    bridge_material=mat('Bridge concrete '+name,(.56,.59,.55))
    steel=mat('Bridge steel '+name,C['steel'])
    for i,p in enumerate(path):
        j=(i+1)%N;q=path[j]
        if min(bridge_lift[i],bridge_lift[j])<.05:continue
        a,b=verts[i*2:i*2+2];c,d=verts[j*2:j*2+2]
        bottom=[(x,y-.65,z) for x,y,z in (a,b,c,d)]
        mesh('Bridge deck underside',[a,b,c,d]+bottom,
             [(0,2,6,4),(1,5,7,3),(4,6,7,5)],bridge_material)
        if i%4==0:
            for side in (-1,1):
                edge=verts[i*2+(side+1)//2]
                next_edge=verts[((i+4)%N)*2+(side+1)//2]
                beam('Bridge railing', (edge[0],edge[1]+1.1,edge[2]),
                     (next_edge[0],next_edge[1]+1.1,next_edge[2]),.12,steel)
                beam('Bridge railing post',edge,(edge[0],edge[1]+1.1,edge[2]),.10,steel)
        if i%16==0 and bridge_lift[i]>2:
            # Keep piers outside every lower road, even at oblique crossings.
            for side in (-1,1):
                x=p[0]+p[4]*(p[5]-.5)*side;z=p[2]-p[3]*(p[5]-.5)*side
                if any(math.hypot(x-r[0],z-r[2])<r[5]+3 for k,r in enumerate(path)
                       if abs(k-i)>35 and r[1]<p[1]-3):continue
                y=ground_height(x,z);height=p[1]-.65-y
                if height>1:box('Bridge pier',(x,y+height/2,z),(1.1,height,1.1),bridge_material,.08)
    def roadside_anchor(i,side,offset):
        p=path[i]
        # On bridges, seat posts just inside the deck edge, including banking.
        # Elsewhere the actual terrain, not the road profile, supports them.
        lane=p[5]-.18 if bridge_lift[i]>.05 else p[5]+offset
        x=p[0]+p[4]*lane*side;z=p[2]-p[3]*lane*side
        y=p[1]+banks[i]*side*lane/p[5] if bridge_lift[i]>.05 else ground_height(x,z)
        return x,y-.04,z

    # Curbs, dashed lane markings, trackside guardrail and reflectors.
    for i in range(0,N,2):
        p=path[i];q=path[(i+2)%N]
        # Crossings have no curbs, dashes or reflectors between the barriers.
        crossing=rail.near_road(p[6],3) or rail.near_road(q[6],3)
        for side in [-1,1]:
            a=verts[i*2+(side+1)//2];b=verts[((i+2)%N)*2+(side+1)//2]
            if crossing:continue
            if max(water_sample(a[0],a[2])[0],water_sample(b[0],b[2])[0])>-1.2:continue
            vv=[a,b,(b[0]+q[4]*.72*side,b[1]+.1,b[2]-q[3]*.72*side),(a[0]+p[4]*.72*side,a[1]+.1,a[2]-p[3]*.72*side)]
            curb_mesh=mesh('Alternating safety curb',vv,[(0,1,2,3)],curb if (i//4)%2 else cream)
            curb_mesh['drivable']=True
        if i%8==0 and water_sample(p[0],p[2])[0]<-2 and not crossing:
            road_marking('Center dash',surface,length,p[6]-.825,p[6]+.825,-.075,.075,cream)
        if i%16==0 and water_sample(p[0],p[2])[0]<-2 and not rail.near_road(p[6],8):
            for side in [-1,1]:
                xx,yy,zz=roadside_anchor(i,side,1.4)
                box('Reflector post',(xx,yy+.60,zz),(.22,1.2,.22),cream,.03)
                box('Reflector',(xx,yy+.95,zz),(.25,.17,.25),curb,.015)
    # Two extra dividers make four equal lanes on the broad carriageway.
    # Carry the dividers into the tapers until the extra lanes merge away.
    for i,p in enumerate(path):
        if i%8 or p[5]<width*.5+.6 or rail.near_road(p[6],3):continue
        for side in [-1,1]:
            lane=side*width*.5
            road_marking('Extra lane divider',surface,length,p[6]-.825,p[6]+.825,
                         lane-.075,lane+.075,cream)
    # Raised wedges: identical exported collision metadata and actual mesh.
    ramp_records=[]
    for idx,height in ramps:
        p=path[idx];yaw=math.atan2(p[3],p[4]);length_r=5.8
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
        ramp_records.append((float(idx),length_r,height,3.9))
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
    rail_markers=rail.build(surface,ground_height)
    scenery_before=set(bpy.context.scene.objects)
    # Place vegetation only outside the racing surface.
    if T.legacy:
        def nearest(x,z):return min(math.hypot(x-p[0],z-p[2])-p[5] for p in path[::4])
    else:
        coarse=KDTree(len(path[::4]))
        for k,p in enumerate(path[::4]):coarse.insert(Vector((p[0],0,p[2])),k)
        coarse.balance()
        def nearest(x,z):
            return min(d-path[k*4][5] for _,k,d in coarse.find_n(Vector((x,0,z)),6))
    def scenery_site(x,z,radius):
        reach=int(T.sx(96))
        sites=[(x,z)]+sorted(((a,b) for a in range(-reach,reach+1,4) for b in range(-int(T.sz(96)),int(T.sz(96))+1,4)),
                            key=lambda p:math.hypot(p[0]-x,p[1]-z))
        for a,b in sites:
            if T.within(a,b,110,108) and nearest(a,b)>radius+3 and water_sample(a,b)[0]<-radius \
               and rail.clear(a,b,radius):
                return a,b
        raise RuntimeError(('No room for scenery',name,radius))
    ctx=dict(api=globals(),T=T,path=path,ground=ground_height,water=water_sample,nearest=nearest,
             site=scenery_site,rail=rail,watershed=watershed,anchor=roadside_anchor,length=length)
    if T.legacy:
        placed=0
        for _ in range(900):
            x=random.uniform(-111,111);z=random.uniform(-108,108)
            if (x/119)**2+(z/118)**2>1 or nearest(x,z)<4.8:continue
            if (x+12)**2+(z+35)**2<190:continue
            if water_sample(x,z)[0]>-4 or not rail.clear(x,z,2.5):continue
            y=ground_height(x,z)-.05
            if placed<105:
                tree(x,y,z,random.uniform(.65,1.3),theme)
            else:
                ico('Granite boulder',(x,y+.6,z),(random.uniform(1.0,2.5),random.uniform(.9,2.5),random.uniform(1,2.3)),mat('Boulder '+name,C['boulder']))
            placed+=1
            if placed>=145:break
        if theme!=1:
            bx,bz=scenery_site(-10,-36,10)
            barn(bx,ground_height(bx,bz),bz,0,theme)
            for i in range(9):
                x=-28+i*4;z=-53;y=T.terrain(x,z)
                if nearest(x,z)<2.5 or not rail.clear(x,z,1):continue
                box('Fence post',(x,y+1,z),(.25,2,.25),wood,.025)
                if i<8 and nearest(x+4,z)>2.5:
                    for h in [.65,1.45]:beam('Fence rail',(x,y+h,z),(x+4,T.terrain(x+4,z)+h,z),.09,cream,5)
            if theme==0:
                for j in range(5):
                    for i in range(7):
                        x=-29+i*2.1;z=24+j*3.2
                        if nearest(x,z)<2 or not rail.clear(x,z,1):continue
                        box('Crop row',(x,T.terrain(x,z)+.5,z),(.6,1.0,1.8),mat('Harvest gold',(.80,.64,.22)),.16)
                for x,z in [(15,-32),(21,-32),(18,-27)]:
                    if nearest(x,z)>3 and rail.clear(x,z,1.5):cone('Hay bale',(x,ground_height(x,z)+1,z),1.5,1.5,2,mat('Hay straw',(.80,.62,.29)),12)
            else:
                for x,z in [(-16,25),(-13,26),(-18,27)]:
                    x,z=scenery_site(x,z,2)
                    y=ground_height(x,z);ico('Snowman body',(x,y+1.3,z),(1.2,1.3,1.2),cream,2);ico('Snowman head',(x,y+2.95,z),(.78,.78,.78),cream,2)
                    cone('Snowman hat',(x,y+3.65,z),.83,.83,.19,dark);cone('Hat top',(x,y+4,z),.51,.51,.65,dark)
        else:
            for x,z,col in [(-12,-35,(.92,.34,.19)),(3,-27,(.23,.59,.68)),(-20,25,(.91,.69,.25)),(25,31,(.92,.34,.19))]:
                x,z=scenery_site(x,z,6)
                y=ground_height(x,z);beam('Beach parasol pole',(x,y,z),(x,y+4,z),.1,cream)
                cone('Striped parasol',(x,y+4,z),3.5,0,.95,mat('Parasol '+str(col),col),12)
                box('Beach towel',(x+3,y+.08,z+3),(1.7,.08,3.5),cream,.04,yaw=.2)
                box('Beach lounger',(x-2,y+.55,z+2),(1.4,.2,2.8),wood,.06)
            hx,hz=scenery_site(-10,42,6)
            box('Lifeguard hut',(hx,ground_height(hx,hz)+2.7,hz),(6,4,5),mat('Hut aqua',(.22,.58,.57)),.15)
            box('Lifeguard roof',(hx,ground_height(hx,hz)+5,hz),(7,.45,6),cream,.12)
    else:
        biomes.scenery(ctx)
    # Direction boards before selected bends.
    for idx in (T.signs if T.legacy else biomes.sign_sites(T,path)):
        # A board may not stand among the crossing's signals.
        while rail.near_road(path[idx][6],10):idx-=4
        p=path[idx];xx,yy,zz=roadside_anchor(idx,1,2);yaw=math.atan2(p[3],p[4])
        box('Direction sign',(xx,yy+2,zz),(2.8,1.15,.18),orange,.08,yaw)
        box('Sign post',(xx,yy+.9,zz),(.16,1.8,.16),dark,.02)
    if T.legacy:
        # Habitat-specific, clustered banks, not the same necklace of stones around
        # every body of water. Sand banks stay mostly bare; upland rock is coarser.
        bank_rng=random.Random(613+theme)
        x0,x1,z0,z1=watershed.bounds
        for _ in range(450):
            x=bank_rng.uniform(x0-2,x1+2);z=bank_rng.uniform(z0-2,z1+2)
            wet,level,_=water_sample(x,z)
            if not -1.8<wet<.55 or nearest(x,z)<2.8 or (x/124)**2+(z/122)**2>1 or not rail.clear(x,z,1):continue
            patch=sin(x*.19+z*.27)*sin(z*.13-x*.23)
            if theme==0 and patch<-.1:continue
            if theme==1 and bank_rng.random()<.78:continue
            y=ground_height(x,z)
            size=bank_rng.uniform(.22,.55) if theme!=2 else bank_rng.uniform(.35,.95)
            ico('Riverbank pebble',(x,y+size*.20,z),(size,size*.45,size*.72),
                mat('Wet river stones '+name,C['pebble']),1 if theme==2 else 2)
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
                mat('Spring source '+name,C['spring']),2)
    else:
        biomes.banks(ctx)
    obstacles=set(bpy.context.scene.objects)-scenery_before
    obstacles.update(o for o in scenery_before if o.name.startswith(('Finish gantry','Reflector')))
    obstacles.update(rail_markers)
    build_spectators(path,T,ground_height,obstacles,water_sample,
                     T.crowd if T.legacy else biomes.crowd_sites(T,path,ramps))
    from leisure import build as build_leisure
    leisure_before=set(bpy.context.scene.objects)
    build_leisure(globals(),T,path,ground_height,water_sample,
                  obstacles | {o for o in bpy.context.scene.objects if 'spectator_group' in o})
    obstacles.intersection_update(set(bpy.context.scene.objects))
    obstacles.update(set(bpy.context.scene.objects)-leisure_before)
    from broadcast import build as build_broadcast
    obstacles.update(build_broadcast(globals(),T,path,ground_height,water_sample,
                     obstacles | {o for o in bpy.context.scene.objects if 'spectator_group' in o},
                     T.broadcast if T.legacy else biomes.broadcast_sites(T,path)))
    from habitats import build as build_habitats
    build_habitats(globals(),T,path,ground_height,water_sample,
                   obstacles | {o for o in bpy.context.scene.objects if 'spectator_group' in o})
    export(os.path.join(ROOT,'assets/tracks',name+'-vegetation.tcv'),vegetation=True)
    export(os.path.join(ROOT,'assets/tracks',name+'-wildlife.tcf'),wildlife=True)
    export(os.path.join(ROOT,'assets/tracks',name+'.tcm'))
    export(os.path.join(ROOT,'assets/tracks',name+'-crowd.tca'),crowd=True)
    export_surface(os.path.join(ROOT,'assets/tracks',name+'.tcs'))
    rail.export(os.path.join(ROOT,'assets/tracks',name+'.tcr'))
    from export_guardrails import export_guardrails
    export_guardrails(os.path.join(ROOT,'assets/tracks',name+'.tcg'))
    cans=[]
    for k,idx in enumerate(choose_cans(T,path,ramps)):cans.append((float(idx),(-1 if k%2 else 1)*(3.8 if T.legacy else T.half-1.4)))
    with open(os.path.join(ROOT,'assets/tracks',name+'.tcp'),'wb') as f:
        f.write(b'TCP1');f.write(struct.pack('<IIIf',N,len(cans),len(ramp_records),length))
        for p in path:f.write(struct.pack('<7f',*p))
        for p in cans:f.write(struct.pack('<2f',*p))
        for p in ramp_records:f.write(struct.pack('<4f',*p))
    save(name)
    print('TRACK',name,'length',round(length,1),'elevation',round(max(p[1] for p in path)-min(p[1] for p in path),1),flush=True)

if __name__=='__main__':
    if '--vehicles-only' in sys.argv:
        clear();build_vehicles(globals(),ROOT);sys.exit(0)
    if '--cars-only' in sys.argv:
        build_cars();sys.exit(0)
    if '--tracks-only' not in sys.argv:build_cars();build_can();build_helicopter();build_vehicles(globals(),ROOT)
    only=sys.argv[sys.argv.index('--only')+1] if '--only' in sys.argv else None
    for T in THEMES:
        if only in (None,T.key):build_track(T)
    print('All Blender source scenes and runtime assets generated.',flush=True)
