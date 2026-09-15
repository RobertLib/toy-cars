"""Original low-poly trackside television crews, baked into the scenery batch."""
import math
import bpy
from mathutils import Vector


def build(api, theme, path, ground, water, obstacles):
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
                    if (x/113)**2 + (z/112)**2 > .97:
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
        def B(name,p,size,color,bevel=0):
            return box('TV '+name, point(p[0],base+p[1],p[2]), size, color, bevel, yaw)
        def L(name,a,b,r,color):
            return beam('TV '+name, point(a[0],base+a[1],a[2]), point(b[0],base+b[1],b[2]),r,color,6)
        def I(name,p,size,color):
            return ico('TV '+name, point(p[0],base+p[1],p[2]),size,color,2)
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
            B('camera head',(a,y+1.78,c),(.58,.42,.86),ink,.035)
            B('long lens hood',(a,y+1.78,c+.61),(.42,.34,.44),ink)
            B('lens glass',(a,y+1.78,c+.84),(.32,.25,.025),glass)
            B('viewfinder',(a-.37,y+1.96,c-.19),(.23,.18,.22),ink)
            B('red tally',(a,y+2.015,c+.30),(.12,.07,.09),red)
            L('pan handle',(a+.2,y+1.55,c),(a+.35,y+1.48,c-.65),.035,ink)
            person(a,c-1.05,'Camera operator')
        camera(0,1 if compound else .5)
        B('flight case',(-1.6,floor(-1.6,-.4)-base+.3,-.4),(.85,.6,.65),ink,.035)
        B('case lid',(-1.6,floor(-1.6,-.4)-base+.61,-.4),(.89,.055,.69),metal)
        # Cables lie on the sampled terrain, including gently sloping shoulders.
        for k in range(12):
            a,c=-1.5*k/12, .5-1.1*k/12
            b,d=-1.5*(k+1)/12,.5-1.1*(k+1)/12
            L('camera cable',(a,floor(a,c)-base+.035,c),(b,floor(b,d)-base+.035,d),.025,ink)
        if compound:
            # Truck on four grounded wheels with a roof dish and a readable TV mark.
            for a in (-1.15,1.15):
                for c in (-4.4,-1.1):
                    gy=floor(a-2.6,c)-base
                    L('van tire',(a-2.78,gy+.5,c),(a-2.42,gy+.5,c),.50,ink)
                    L('van hub',(a-2.80,gy+.5,c),(a-2.40,gy+.5,c),.24,metal)
            B('OB van',(-2.6,1.70,-2.8),(2.65,2.35,5.1),ivory,.15)
            B('van stripe',(-2.6,1.30,-2.8),(2.72,.44,5.13),blue)
            B('windshield',(-2.6,2.25,-.23),(2.18,.83,.04),glass)
            for a in (-3.52,-1.68):
                B('headlight',(a,.98,-.21),(.40,.22,.06),ivory)
            for a in (-3.94,-1.26):
                B('side window',(a,2.25,-.9),(.035,.75,.95),glass)
                # Block-letter T and V on both sides of the production vehicle.
                L('logo T',(a,2.40,-3.7),(a,2.40,-3.0),.065,blue)
                L('logo T',(a,2.40,-3.35),(a,1.82,-3.35),.065,blue)
                L('logo V',(a,2.40,-2.8),(a,1.82,-2.48),.065,blue)
                L('logo V',(a,1.82,-2.48),(a,2.40,-2.16),.065,blue)
            L('dish mast',(-2.6,2.9,-3.3),(-2.6,3.7,-3.3),.09,metal)
            # Concave upward parabola, original faceted mesh with feed support.
            vs=[point(-2.6,base+3.65,-3.3)]
            for k in range(16):
                t=k*math.pi/8
                vs.append(point(-2.6+1.05*math.cos(t),base+4.04,-3.3+1.05*math.sin(t)))
            api['mesh']('TV satellite dish',vs,[(0,k+1,(k+1)%16+1) for k in range(16)],ivory)
            L('dish feed',(-2.6,3.65,-3.3),(-2.6,4.45,-3.3),.045,ink)
            L('radio mast',(-3.5,2.9,-4.7),(-3.5,5.1,-4.7),.035,metal)
            person(2.2,.5,'Reporter')
            person(3.9,-.4,'Sound engineer')
            person(2.4,-3.8,'Vision technician')
            B('production desk',(2.5,.95,-2.7),(2.4,.13,.85),metal)
            for a in (1.5,3.5):
                L('desk leg',(a,floor(a,-2.7)-base,-2.7),(a,.95,-2.7),.055,ink)
                B('monitor',(a,1.43,-2.65),(.83,.65,.12),ink)
                B('monitor image',(a,1.43,-2.73),(.71,.51,.035),screen)
                L('monitor stand',(a,1.0,-2.65),(a,1.20,-2.65),.06,ink)
        for obj in set(bpy.context.scene.objects)-before:
            obj['broadcast_station']=index
            obj['broadcast_compound']=compound
        return set(bpy.context.scene.objects)-before

    created=station(12,True)
    for idx in (70,205,355,500):
        created.update(station(idx))
    print('BROADCAST',theme,'5 stations, 8 crew,',len(created),'objects',flush=True)
    return created
