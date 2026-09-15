"""Run with Blender: blender -b --python tools/build_assets.py.

All visible game meshes, tracks and the editable source scenes are built in
Blender. Export is a deliberately small, versioned triangle/vertex-color format.
No Blender installation or Python interpreter is needed to play the game.
"""
import bpy, math, random, struct, os
from mathutils import Vector
from mathutils.bvhtree import BVHTree
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

def export(path):
    bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get();data=[]
    for o in bpy.context.scene.objects:
        if o.type!='MESH' or o.hide_render:continue
        e=o.evaluated_get(deps);me=e.to_mesh();me.calc_loop_triangles();normal=e.matrix_world.to_3x3().inverted().transposed()
        for tri in me.loop_triangles:
            material=me.materials[tri.material_index] if me.materials else None
            color=tuple(material.diffuse_color[:3]) if material else (.5,.5,.5)
            paint=material.get('paint',0) if material else 0
            for idx in tri.vertices:
                v=me.vertices[idx];p=e.matrix_world@v.co;n=(normal@(v.normal if tri.use_smooth else tri.normal)).normalized()
                data.extend((*eng(p),*eng(n),*color,paint))
        e.to_mesh_clear()
    with open(path,'wb') as f:
        f.write(b'TCM1');f.write(struct.pack('<I',len(data)//10));f.write(struct.pack('<%sf'%len(data),*data))
    print('EXPORTED',os.path.basename(path),len(data)//30,'triangles',flush=True)

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

KNOTS=[(-62,-68),(-18,-81),(43,-76),(80,-50),(84,-17),(51,-2),(34,22),(62,55),(46,78),(1,84),(-43,67),(-73,43),(-71,9),(-39,-7),(-38,-36),(-68,-42)]
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

def build_track(theme,name):
    clear();random.seed(185+theme)
    N=640;width=10.4 if theme!=2 else 11.0
    path=[];distance=0
    for i in range(N):
        t=i/N;x,z=spline(t,theme);x2,z2=spline((t+.0001)%1,theme);d=Vector((x2-x,0,z2-z)).normalized()
        y=terrain(x,z,theme)+.15
        if i:distance+=Vector((x-path[-1][0],y-path[-1][1],z-path[-1][2])).length
        path.append((x,y,z,d.x,d.z,width*.5,distance))
    length=distance+Vector((path[0][0]-path[-1][0],path[0][1]-path[-1][1],path[0][2]-path[-1][2])).length
    grass=mat(['Meadow','Golden sand','Powder snow'][theme],[(.48,.64,.30),(.83,.73,.49),(.76,.87,.87)][theme])
    road=mat('Road '+name,[(.28,.31,.29),(.53,.44,.31),(.37,.48,.54)][theme])
    curb=mat('Curb '+name,[(.80,.31,.18),(.89,.43,.18),(.21,.50,.64)][theme])
    banks=[]
    for i in range(N):
        pp,pn=path[(i-5)%N],path[(i+5)%N]
        banks.append(max(-.33,min(.33,(pp[3]*pn[4]-pp[4]*pn[3])*.55)))
    # Author the road first so terrain grading can respect its tightest bends.
    verts=[]
    for i,p in enumerate(path):
        x,y,z,dx,dz,half,_=p
        for side in [-1,1]:verts.append((x+dz*half*side,y+banks[i]*side,z-dx*half*side))
    faces=[(i*2,((i+1)%N)*2,((i+1)%N)*2+1,i*2+1) for i in range(N)]
    ribbon=mesh('Elevated ribbon road',verts,faces,road)
    ribbon['drivable']=True
    for poly in ribbon.data.polygons:poly.use_smooth=True
    ribbon.data.calc_loop_triangles()
    # Grade a broad shoulder into the hills, without a trench beneath the road.
    vs=[(0,terrain(0,0,theme)-.14,0)];rings=40;slices=192
    for r in range(1,rings+1):
        for j in range(slices):
            a=j*2*pi/slices;rad=r/rings
            x=cos(a)*128*rad;z=sin(a)*126*rad
            edge=max(0,(rad-.87)/.13)
            y=terrain(x,z,theme)-.14-edge*edge*5
            idx=min(range(N),key=lambda i:(path[i][0]-x)**2+(path[i][2]-z)**2)
            projections=[]
            for segment in ((idx-1)%N,idx):
                p,q=path[segment],path[(segment+1)%N]
                dx,dz=q[0]-p[0],q[2]-p[2]
                f=max(0,min(1,((x-p[0])*dx+(z-p[2])*dz)/(dx*dx+dz*dz)))
                center=tuple(a+(b-a)*f for a,b in zip(p,q))
                projections.append((math.hypot(center[0]-x,center[2]-z),segment,f,center))
            road_distance,idx,f,closest=min(projections)
            lane=(x-closest[0])*closest[4]-(z-closest[2])*closest[3]
            blend=max(0,min(1,(road_distance-width*.5-2)/5))
            blend=blend*blend*(3-2*blend)
            bank=banks[idx]+(banks[(idx+1)%N]-banks[idx])*f
            grade=closest[1]+bank*max(-1,min(1,lane/closest[5]))-.22
            y=grade+(y-grade)*blend
            vs.append((x,y,z))
    fs=[]
    for j in range(slices):fs.append((0,1+(j+1)%slices,1+j))
    for r in range(1,rings):
        for j in range(slices):
            a=1+(r-1)*slices+j;b=1+(r-1)*slices+(j+1)%slices;c=1+r*slices+(j+1)%slices;d=1+r*slices+j
            fs.extend([(a,b,c),(a,c,d)])
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
    edgevs=[];edgefs=[]
    for j in range(slices):
        a=j*2*pi/slices;x=cos(a)*128;z=sin(a)*126;edgevs.extend([(x,terrain(x,z,theme)-5.14,z),(x,-7,z)])
    for j in range(slices):edgefs.append((j*2,j*2+1,((j+1)%slices)*2+1,((j+1)%slices)*2))
    mesh('Exposed diorama edge',edgevs,edgefs,mat('Island edge '+name,[(.51,.37,.21),(.72,.59,.36),(.44,.62,.67)][theme]))
    if theme==1:
        cone('Turquoise ocean plinth',(0,-7.5,0),145,145,1.5,mat('Lagoon water',(.16,.63,.67)),128)
        for i in range(18):
            a=i*2*pi/18;x,z=cos(a)*133,sin(a)*133
            box('Foam glint',(x,-6.6,z),(5,.04,.4),cream,.1,yaw=-a)
    # Paint follows the road's exact elevation and banking.
    ribbon.data.calc_loop_triangles();surface=[]
    for triangle in ribbon.data.loop_triangles:
        segment=triangle.polygon_index;points=[]
        for vertex in triangle.vertices:
            i=vertex//2;s=length if segment==N-1 and i==0 else path[i][6]
            points.append((s,path[i][5]*(1 if vertex%2 else -1),*verts[vertex]))
        surface.append(points)
    # The shoulder meets the actual island mesh; no unsupported road skirt.
    for side in [-1,1]:
        bed=[]
        for i,p in enumerate(path):
            a=verts[i*2+(side+1)//2]
            x=a[0]+p[4]*1.5*side;z=a[2]-p[3]*1.5*side
            bed.extend([a,(x,ground_height(x,z)-.04,z)])
        shoulder=mesh('Sculpted road bed',bed,[(i*2,i*2+1,((i+1)%N)*2+1,((i+1)%N)*2) for i in range(N)],mat('Road foundation '+name,[(.41,.44,.31),(.68,.57,.36),(.66,.78,.80)][theme]))
        shoulder['drivable']=True
    # Curbs, dashed lane markings, trackside guardrail and reflectors.
    for i in range(0,N,2):
        p=path[i];q=path[(i+2)%N]
        for side in [-1,1]:
            a=verts[i*2+(side+1)//2];b=verts[((i+2)%N)*2+(side+1)//2]
            vv=[a,b,(b[0]+q[4]*.72*side,b[1]+.1,b[2]-q[3]*.72*side),(a[0]+p[4]*.72*side,a[1]+.1,a[2]-p[3]*.72*side)]
            curb_mesh=mesh('Alternating safety curb',vv,[(0,1,2,3)],curb if (i//4)%2 else cream)
            curb_mesh['drivable']=True
        if i%8==0:
            yaw=math.atan2(p[3],p[4]);box('Center dash',(p[0],p[1]+.045,p[2]),(.15,.018,1.65),cream,0,yaw)
        if i%16==0:
            for side in [-1,1]:
                xx=p[0]+p[4]*(width*.5+1.4)*side;zz=p[2]-p[3]*(width*.5+1.4)*side
                box('Reflector post',(xx,p[1]+.60,zz),(.22,1.2,.22),cream,.03)
                box('Reflector',(xx,p[1]+.95,zz),(.25,.17,.25),curb,.015)
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
    # Place vegetation only outside the racing surface.
    def nearest(x,z):return min((x-p[0])**2+(z-p[2])**2 for p in path[::4])**.5
    placed=0
    for _ in range(900):
        x=random.uniform(-111,111);z=random.uniform(-108,108)
        if (x/119)**2+(z/118)**2>1 or nearest(x,z)<10:continue
        if (x+12)**2+(z+35)**2<190:continue
        y=terrain(x,z,theme)-.05
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
    export(os.path.join(ROOT,'assets/tracks',name+'.tcm'))
    export_surface(os.path.join(ROOT,'assets/tracks',name+'.tcs'))
    cans=[]
    for k,idx in enumerate([42,130,214,332,407,542,601]):cans.append((float(idx),(-1 if k%2 else 1)*2.4))
    with open(os.path.join(ROOT,'assets/tracks',name+'.tcp'),'wb') as f:
        f.write(b'TCP1');f.write(struct.pack('<IIIf',N,len(cans),len(ramps),length))
        for p in path:f.write(struct.pack('<7f',*p))
        for p in cans:f.write(struct.pack('<2f',*p))
        for p in ramps:f.write(struct.pack('<4f',*p))
    save(name)
    print('TRACK',name,'length',round(length,1),'elevation',round(max(p[1] for p in path)-min(p[1] for p in path),1),flush=True)

if __name__=='__main__':
    build_car();build_can()
    for theme,name in enumerate(['country','beach','winter']):build_track(theme,name)
    print('All Blender source scenes and runtime assets generated.',flush=True)
