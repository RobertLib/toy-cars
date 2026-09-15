"""Landscape-led park paths and individual benches, baked into the diorama."""
import math
import random
from mathutils import Vector


def build(api, theme, path, ground, water, obstacles):
    bpy = api['bpy']
    box, beam, ico = (api[k] for k in ('box', 'beam', 'ico'))
    mat = api['mat']
    wood, dark = (api[k] for k in ('wood', 'dark'))
    rng = random.Random(1840 + theme)
    bpy.context.view_layer.update()
    def bounds_of(obj):
        corners = [api['eng'](obj.matrix_world @ Vector(v)) for v in obj.bound_box]
        return (min(v[0] for v in corners), max(v[0] for v in corners),
                min(v[2] for v in corners), max(v[2] for v in corners))
    def near_bounds(x,z,r,bounds):
        return any(a-r<x<b+r and c-r<z<d+r for a,b,c,d in bounds)
    def safe(x,z,r):
        return ((x/113)**2+(z/111)**2<1 and
                all(math.hypot(x-p[0],z-p[2])>p[5]+r+2.5 for p in path) and
                all(water(x+dx*r,z+dz*r)[0]<-2 for dx,dz in
                    [(0,0),(-1,0),(1,0),(0,-1),(0,1),(-.7,-.7),(.7,.7),(-.7,.7),(.7,-.7)]))
    gravel=mat('Natural footpath '+str(theme),[(.56,.51,.37),(.72,.65,.46),(.73,.80,.80)][theme])
    paths=[]
    def trail(points,width,name):
        # Narrow ribbons retain the grass between paths and follow the island.
        vertices=[]
        for i,(x,z) in enumerate(points):
            a=points[max(0,i-1)];b=points[min(len(points)-1,i+1)]
            dx,dz=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dz)
            w=width*(1+.045*math.sin(i*.67))*.5
            for side in (-1,1):
                xx,zz=x+side*dz/length*w,z-side*dx/length*w
                vertices.append((xx,ground(xx,zz)+.045,zz))
        obj=api['mesh'](name,vertices,[(2*i,2*i+1,2*i+3,2*i+2) for i in range(len(points)-1)],gravel)
        obj['leisure_path']=True
        paths.extend(points)
        return obj
    def curve(control):
        result=[]
        for a,b,c,d in zip(control,control[1:],control[2:],control[3:]):
            count=max(8,int(math.dist(b,c)/.3))
            for i in range(count):
                t=i/count
                result.append(tuple(.5*(2*b[j]+(-a[j]+c[j])*t+(2*a[j]-5*b[j]+4*c[j]-d[j])*t*t+
                                        (-a[j]+3*b[j]-3*c[j]+d[j])*t*t*t) for j in range(2)))
        result.append(control[-2])
        return result
    def bench_at(x,z,yaw,site):
        before=set(bpy.context.scene.objects)
        def pos(a,h,b):
            return (x+math.cos(yaw)*a+math.sin(yaw)*b,h,z-math.sin(yaw)*a+math.cos(yaw)*b)
        def floor(a,b):
            xx,_,zz=pos(a,0,b)
            return ground(xx,zz)
        def block(name,a,h,b,size,material):
            return box(name,pos(a,h,b),size,material,0,yaw)
        a=b=0
        y = max(floor(a+dx,b+dz) for dx in (-1.05,1.05) for dz in (-.3,.3))
        for dx in (-1.05,1.05):
            for dz in (-.3,.3):
                beam('Bench leg',pos(a+dx,floor(a+dx,b+dz),b+dz),
                     pos(a+dx,y+.7,b+dz),.065,dark)
            beam('Bench back support',pos(a+dx,y+.55,b-.32),
                 pos(a+dx,y+1.45,b-.32),.055,dark)
        for dz in (-.27,0,.27):
            block('Bench seat slat',a,y+.72,b+dz,(2.7,.12,.23),wood)
        for h in (1.03,1.32):
            block('Bench back slat',a,y+h,b-.37,(2.7,.22,.10),wood)
        occupied = site%3 != 0
        if occupied:
            skin = mat('Resting visitor skin '+str(site),
                       [(.75,.47,.30),(.94,.69,.49),(.42,.26,.18),(.64,.40,.26)][site%4])
            shirt = mat('Resting visitor clothes '+str(site),
                        [(.18,.49,.61),(.79,.28,.19),(.67,.47,.71),(.86,.65,.19)][site%4])
            # Bent thighs rest on the seat; hands rest on the knees.
            for dx in (-.18,.18):
                foot_y = floor(a+dx,b+.63)
                block('Seated visitor shoe',a+dx,foot_y+.09,b+.66,(.23,.18,.39),dark)
                beam('Seated visitor shin',pos(a+dx,foot_y+.17,b+.56),pos(a+dx,y+.83,b+.51),.09,dark)
                beam('Seated visitor thigh',pos(a+dx,y+.83,b+.51),pos(a+dx,y+.83,b-.05),.12,dark)
                beam('Seated visitor upper arm',pos(a+dx*1.7,y+1.37,b-.03),pos(a+dx*2,y+1.05,b+.18),.10,shirt)
                beam('Seated visitor forearm',pos(a+dx*2,y+1.05,b+.18),pos(a+dx,y+.96,b+.46),.08,skin)
                ico('Seated visitor hand',pos(a+dx,y+.96,b+.46),(.10,.09,.10),skin)
            block('Seated visitor coat',a,y+1.12,b-.04,(.58,.60,.34),shirt)
            ico('Seated visitor head',pos(a,y+1.69,b-.02),(.23,.28,.23),skin,2)
            block('Seated visitor nose',a,y+1.69,b+.21,(.09,.10,.10),skin)
            block('Seated visitor hat',a,y+1.92,b-.04,(.47,.14,.43),shirt if theme==2 else dark)
        for obj in set(bpy.context.scene.objects)-before:
            obj['landscape_bench']=site
            obj['bench_occupied']=occupied
        return set(bpy.context.scene.objects)-before

    # The countryside gets one substantial park. Its lawn is the existing
    # terrain, with an irregular walking loop, open glades and grouped trees.
    park=None
    if theme==0:
        fixed=[bounds_of(o) for o in obstacles if o.type=='MESH' and
               'vegetation_origin' not in o and not o.name.startswith('Granite boulder')]
        candidates=[]
        for x in range(-96,97,3):
            for z in range(-96,97,3):
                if not safe(x,z,12) or near_bounds(x,z,12,fixed):continue
                heights=[ground(x+dx,z+dz) for dx,dz in [(-9,0),(9,0),(0,-9),(0,9),(0,0)]]
                slope=max(heights)-min(heights)
                if slope<4.5:candidates.append((slope+.012*math.hypot(x,z),x,z))
        assert candidates,'No suitable landscape for countryside park'
        _,cx,cz=min(candidates);park=(cx,cz)
        # Replace random scatter here with deliberate groves and clear walks.
        for obj in list(obstacles):
            origin=obj.get('vegetation_origin')
            if origin: ox,_,oz,_=origin
            elif obj.name.startswith('Granite boulder'): ox,_,oz=api['eng'](obj.location)
            else:continue
            if math.hypot(ox-cx,oz-cz)<20:
                obstacles.discard(obj);bpy.data.objects.remove(obj,do_unlink=True)
        controls=[(cx+dx*.65,cz+dz*.65) for dx,dz in
                  [(-11,-4),(-7,-9),(2,-10),(11,-5),(12,3),(5,9),(-4,10),(-12,5),(-11,-4),(-7,-9),(2,-10)]]
        loop=curve(controls)
        trail(loop,1.35,'Park winding gravel walk')
        # Join the nearest outward-facing points of the loop to the verge.
        # Avoid shortcuts across the lawn and keep entrances well separated.
        entries=[]
        for start in loop[::12]:
            q=min(path,key=lambda p:math.hypot(p[0]-start[0],p[2]-start[1]))
            dx,dz=start[0]-q[0],start[1]-q[2];length=math.hypot(dx,dz)
            if (start[0]-cx)*dx+(start[1]-cz)*dz>=0:continue
            end=(q[0]+dx/length*(q[5]+3.5),q[2]+dz/length*(q[5]+3.5))
            entries.append((math.dist(start,end),start,end))
        entrances=[]
        bounds=[bounds_of(o) for o in obstacles if o.type=='MESH']
        for _,start,end in sorted(entries):
            if len(entrances)==2:break
            if any(math.dist(start,p)<9 for p in entrances):continue
            mid=((start[0]+end[0])/2+.6,(start[1]+end[1])/2-.6)
            points=curve([start,start,mid,end,end])
            if all(safe(x,z,.7) and not near_bounds(x,z,.9,bounds) for x,z in points):
                trail(points,1.3,'Park entrance path');entrances.append(start)
        assert entrances,'Park must connect to the surrounding verge'
        for i,t in enumerate((.12,.40,.70)):
            px,pz=loop[int(t*(len(loop)-1))]
            dx,dz=px-cx,pz-cz;length=math.hypot(dx,dz)
            x,z=px+dx/length*1.55,pz+dz/length*1.55
            bench_at(x,z,math.atan2(-dx,-dz),i)
        # Broadleaf groups around the perimeter, with a few interior shade trees.
        for dx,dz,s in [(-15,-7,.95),(-12,-12,1.1),(-4,-15,.85),(7,-14,1.05),
                        (15,-7,1.15),(16,2,.8),(10,13,1.05),(-1,15,1.1),
                        (-12,11,.9),(-16,3,1.15),(-3,2,1.0),(3,-3,.8)]:
            x,z=cx+dx*.65,cz+dz*.65;y=ground(x,z)
            if any(math.hypot(x-a,z-b)<1.8 for a,b in paths):continue
            before=set(bpy.context.scene.objects)
            beam('Park deciduous trunk',(x,y,z),(x+.2,y+3.5*s,z),.22*s,wood)
            for ox,oy,oz,r in [(0,4.4,0,2.2),(1.3,4.0,.5,1.8),(-1.0,4.5,-.6,1.65)]:
                ico('Park leafy crown',(x+ox*s,y+oy*s,z+oz*s),(r*s,r*.95*s,r*.9*s),
                    mat('Park leaf '+str(int(s*10)%3),[(.31,.48,.22),(.39,.55,.25),(.27,.44,.21)][int(s*10)%3]),2)
            for obj in set(bpy.context.scene.objects)-before:
                obj['vegetation_origin']=(x,y,z,7*s);obj['vegetation_motion']=(.65,0.,0.,0.)
        # Loose shrub/flower drifts, never boxed planters or a hard park boundary.
        for dx,dz in [(-14,-10),(5,-14),(13,9),(-8,13)]:
            for j in range(7):
                x,z=cx+dx*.65+rng.uniform(-1.8,1.8),cz+dz*.65+rng.uniform(-1.3,1.3)
                if any(math.hypot(x-a,z-b)<1.3 for a,b in paths):continue
                y=ground(x,z)
                ico('Park shrub drift',(x,y+.35,z),(.65,.55,.6),mat('Park shrubs',(.30,.43,.19)),2)
                if j%2==0:
                    for k in range(3):
                        ico('Park flower drift',(x+rng.uniform(-.4,.4),y+.75,z+rng.uniform(-.3,.3)),(.12,.10,.12),
                            mat('Park wildflowers',(.81,.69,.83)))
        print('LANDSCAPE PARK',park,flush=True)

    # Single benches sit directly on the verge, facing a view or the race.
    # They have no repeated paving pad, planter pair or obligatory litter bin.
    bpy.context.view_layer.update()
    bounds=[bounds_of(o) for o in obstacles if o.type=='MESH']
    sites=[]
    for attempt in range(5000):
        if len(sites)==5:break
        p=path[rng.randrange(len(path))]
        lane=rng.choice([-1,1])*(p[5]+rng.uniform(5.5,9))
        x,z=p[0]+p[4]*lane,p[2]-p[3]*lane
        if not safe(x,z,1.8) or near_bounds(x,z,2,bounds):continue
        if park and math.hypot(x-park[0],z-park[1])<23:continue
        if any(math.hypot(x-a,z-b)<27 for a,b in sites):continue
        h=ground(x,z)
        if max(abs(ground(x+dx,z+dz)-h) for dx,dz in [(-1.5,0),(1.5,0),(0,-1),(0,1)])>.28:continue
        sites.append((x,z))
        bench_at(x,z,math.atan2(p[0]-x,p[2]-z)+rng.uniform(-.35,.35),len(sites)+3)
    assert len(sites)==5,('Too few natural bench locations',theme)
    print('INDIVIDUAL BENCHES',theme,sites,flush=True)
