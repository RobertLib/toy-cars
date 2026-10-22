"""Original trackside television crews, baked into the scenery batch."""
import math
import bpy
from mathutils import Vector
import figures, landmarks, roadside


def build(api, T, path, ground, water, obstacles, stations):
    theme = T.index
    box, beam, ico, mat = (api[k] for k in ('box', 'beam', 'ico', 'mat'))
    ink, metal, glass, ivory = (api[k] for k in ('dark', 'chrome', 'glass', 'cream'))
    blue = mat('Broadcast cobalt', (.055, .24, .62))
    vest = mat('Media turquoise vest', (.08, .65, .65))
    red = mat('Live tally red', (.98, .055, .035))
    skin = mat('Broadcast crew skin', (.73, .45, .28))
    screen = mat('Broadcast monitor picture', (.19, .60, .46))
    bpy.context.view_layer.update()
    bounds = []
    for obj in obstacles:
        if obj.type != 'MESH':
            continue
        corners = [api['eng'](obj.matrix_world @ Vector(p)) for p in obj.bound_box]
        bounds.append((min(p[0] for p in corners), max(p[0] for p in corners),
                       min(p[2] for p in corners), max(p[2] for p in corners)))
    occupied = []

    def locate(index, radius):
        for offset in range(0, len(path), 7):
            p = path[(index + offset) % len(path)]
            for extra in (radius + 3.5, radius + 7, radius + 12, radius + 18):
                for side in (1, -1):
                    x, z = p[0] + side*p[4]*(p[5]+extra), p[2] - side*p[3]*(p[5]+extra)
                    if not T.within(x, z, 113 * .97 ** .5, 112 * .97 ** .5):
                        continue
                    if any(a-radius < x < b+radius and c-radius < z < d+radius for a,b,c,d in bounds):
                        continue
                    if any(math.hypot(x-a, z-b) < radius+r+1 for a,b,r in occupied):
                        continue
                    if min(math.hypot(x-q[0], z-q[2])-q[5] for q in path) < radius+2:
                        continue
                    samples = [(x, z)] + [(x+radius*math.cos(k*math.pi/4), z+radius*math.sin(k*math.pi/4)) for k in range(8)]
                    heights = [ground(a,b) for a,b in samples]
                    if max(heights)-min(heights) > radius*.22 or any(water(a,b)[0] > -1.5 for a,b in samples):
                        continue
                    occupied.append((x,z,radius))
                    return x, z, math.atan2(p[0]-x, p[2]-z)
        raise AssertionError(('No broadcast location', theme, index, radius))

    def station(index, compound=False):
        x,z,yaw = locate(index, 7.5 if compound else 2.6)
        before = set(bpy.context.scene.objects)
        def point(a,b,c):
            return x+math.cos(yaw)*a+math.sin(yaw)*c, b, z-math.sin(yaw)*a+math.cos(yaw)*c
        def floor(a,c):
            wx,_,wz = point(a,0,c)
            return ground(wx,wz)
        base = max(floor(a,c) for a,c in [(-3,-4),(3,-4),(-3,3),(3,3)]) if compound else floor(0,0)
        # Rounded parts: each keeps its own object and name, centred where it stands.
        def B(name,p,size,color,bevel=0,e=.4):
            return figures.shape('TV '+name, point(p[0],base+p[1],p[2]), yaw,
                                 [figures.blob((0,0,0),[v/2 for v in size],8,5,e)], color, None)
        def L(name,a,b,r,color):
            mid=[(u+v)/2 for u,v in zip(a,b)]
            return figures.shape('TV '+name, point(mid[0],base+mid[1],mid[2]), yaw,
                                 [figures.limb([u-m for u,m in zip(a,mid)],[v-m for v,m in zip(b,mid)],r,sides=6)],
                                 color, None)
        def I(name,p,size,color):
            return figures.shape('TV '+name, point(p[0],base+p[1],p[2]), yaw,
                                 [figures.blob((0,0,0),size,10,6)], color, None)
        def M(name,key,build,p):
            return roadside.place('TV '+name,key,build,*point(p[0],base+p[1],p[2]),yaw)
        def person(a,c,role):
            y = floor(a,c)-base
            for side in (-1,1):
                B('crew boot',(a+side*.17,y+.1,c),(.23,.20,.40),ink)
                L('crew leg',(a+side*.17,y+.2,c),(a+side*.15,y+.92,c),.115,ink)
            B(role+' jacket',(a,y+1.24,c),(.62,.65,.38),blue)
            B('MEDIA vest',(a,y+1.29,c+.21),(.53,.43,.055),vest)
            B('press pass',(a+.12,y+1.30,c+.25),(.13,.18,.025),ivory)
            I('crew head',(a,y+1.81,c),(.23,.28,.23),skin)
            B('crew cap',(a,y+2.04,c),(.48,.12,.47),blue)
            for side in (-1,1):
                L('crew sleeve',(a+side*.32,y+1.46,c),(a+side*.43,y+1.18,c+.16),.105,blue)
                L('crew forearm',(a+side*.43,y+1.18,c+.16),(a+side*.20,y+1.48,c+.48),.075,skin)
                I('headset ear',(a+side*.25,y+1.82,c),(.075,.13,.10),ink)
            L('headset band',(a-.24,y+1.94,c),(a+.24,y+1.94,c),.035,ink)
            if role == 'Reporter':
                L('microphone handle',(a+.2,y+1.46,c+.48),(a+.2,y+1.79,c+.48),.045,ink)
                B('microphone flag',(a+.2,y+1.72,c+.48),(.18,.17,.18),blue)
                I('microphone foam',(a+.2,y+1.87,c+.48),(.10,.12,.10),ink)
            if role == 'Sound engineer':
                B('sound mixer bag',(a,y+1.03,c+.36),(.53,.28,.22),ink)
                L('boom pole',(a-.2,y+1.48,c+.48),(a+1.2,y+3.2,c+.8),.035,metal)
                L('boom microphone',(a+1.2,y+3.2,c+.8),(a+1.2,y+3.2,c+1.4),.11,ink)
        def camera(a,c):
            y=floor(a,c)-base
            for dx,dz in [(-.65,-.35),(.65,-.35),(0,.70)]:
                L('tripod',(a+dx,floor(a+dx,c+dz)-base+.04,c+dz),(a,y+1.60,c),.045,metal)
            M('camera head',('tv camera',),landmarks.tv_camera,(a,y+1.78,c))
            B('lens glass',(a,y+1.78,c+.845),(.3,.24,.03),glass,e=.3)
            person(a,c-1.05,'Camera operator')
        camera(0,1 if compound else .5)
        B('flight case',(-1.6,floor(-1.6,-.4)-base+.3,-.4),(.85,.6,.65),ink,e=.2)
        B('case lid',(-1.6,floor(-1.6,-.4)-base+.61,-.4),(.89,.055,.69),metal,e=.2)
        # Cables lie on the sampled terrain, including gently sloping shoulders.
        for k in range(12):
            a,c=-1.5*k/12, .5-1.1*k/12
            b,d=-1.5*(k+1)/12,.5-1.1*(k+1)/12
            L('camera cable',(a,floor(a,c)-base+.035,c),(b,floor(b,d)-base+.035,d),.025,ink)
        if compound:
            # The production truck with its lettering, and the uplink dish on its roof.
            M('OB van',('ob van',),landmarks.ob_van,(-2.6,floor(-2.6,-2.8)-base,-2.8))
            M('satellite dish',('satellite dish',),landmarks.satellite_dish,(-2.6,floor(-2.6,-2.8)-base+3.02,-3.3))
            person(2.2,.5,'Reporter')
            person(3.9,-.4,'Sound engineer')
            person(2.4,-3.8,'Vision technician')
            B('production desk',(2.5,.95,-2.7),(2.4,.13,.85),metal,e=.2)
            for a in (1.5,3.5):
                L('desk leg',(a,floor(a,-2.7)-base,-2.7),(a,.95,-2.7),.055,ink)
                B('monitor',(a,1.43,-2.65),(.83,.65,.12),ink,e=.2)
                B('monitor image',(a,1.43,-2.73),(.71,.51,.035),screen,e=.15)
                L('monitor stand',(a,1.0,-2.65),(a,1.20,-2.65),.06,ink)
        for obj in set(bpy.context.scene.objects)-before:
            obj['broadcast_station']=index
            obj['broadcast_compound']=compound
        return set(bpy.context.scene.objects)-before

    created=station(stations[0],True)
    for idx in stations[1:]:
        created.update(station(idx))
    print('BROADCAST',theme,'5 stations, 8 crew,',len(created),'objects',flush=True)
    return created
