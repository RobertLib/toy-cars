"""Validate drainage and the rendered/collidable channel in the Blender scenes.

blender -b --python-exit-code 1 --python tests/water_layout.py
"""
from pathlib import Path
import math
import struct
import sys

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from hydrology import Watershed
from themes import THEMES
import os
# TOYCARS_TRACKS=canyon,volcano checks only those scenes.
THEMES = [T for T in THEMES if T.key in os.environ.get("TOYCARS_TRACKS", T.key).split(",")]


def cross(a,b,c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


def hull_area(points):
    points=sorted(set(points))
    lower=[];upper=[]
    for chain,ordered in ((lower,points),(upper,reversed(points))):
        for p in ordered:
            while len(chain)>1 and cross(chain[-2],chain[-1],p)<=0:chain.pop()
            chain.append(p)
    hull=lower[:-1]+upper[:-1]
    return abs(sum(a[0]*b[1]-a[1]*b[0] for a,b in zip(hull,hull[1:]+hull[:1])))*.5


# Deliberately read authored route metadata and exported meshes, rather than
# deriving expected positions from a track index or accepting disconnected pools.
# Meanders of the first circuits; later rivers wind at least as much as Beach's.
SINUOSITY = {'country': 1.20, 'beach': 1.10, 'winter': 1.12}
for T in THEMES:
    theme, name = T.index, T.key
    def outside(x, z, limit):
        return (math.hypot(x/128, z/126) if T.legacy else T.island(x, z)) > limit
    bpy.ops.wm.open_mainfile(filepath=str(ROOT / 'art' / (name + '.blend')))
    water = next(o for o in bpy.context.scene.objects if o.get('water'))
    raw = list(water['water_route'])
    route = [raw[i:i+3] for i in range(0,len(raw),3)]
    # A vent beside the crater's lava lake is no stream: only the lake is checked.
    stream = not T.lava
    assert not stream or len(route) > 40, (name, 'short stream')
    assert all(b[1] <= a[1]+1e-6 for a,b in zip(route,route[1:])), (name, 'uphill flow')
    distance=sum(math.hypot(b[0]-a[0],b[2]-a[2]) for a,b in zip(route,route[1:]))
    chord=math.hypot(route[-1][0]-route[0][0],route[-1][2]-route[0][2])
    assert not stream or distance/chord>SINUOSITY.get(name,1.10), (name,'channel reverted to a straight drain')
    water.data.calc_loop_triangles()
    water_bvh = BVHTree.FromPolygons(
        [v.co for v in water.data.vertices],
        [t.vertices[:] for t in water.data.loop_triangles], all_triangles=True)
    def water_at(x,z):
        hit,_,_,_ = water_bvh.ray_cast(Vector((x,-z,100)),Vector((0,0,-1)))
        return None if hit is None else hit.z
    widths=[]
    for i in range(8,len(route)-5,6):
        a,p,b=route[i-1],route[i],route[i+1]
        dx,dz=b[0]-a[0],b[2]-a[2];length=math.hypot(dx,dz)
        widths.append(sum(water_at(p[0]-dz/length*j*.15,p[2]+dx/length*j*.15) is not None
                          for j in range(-24,25))*.15)
    assert not stream or max(widths)>min(widths)*1.4, (name,'constant-width water ribbon',widths)
    # Check every metre of the actual channel after its concealed spring head.
    for p in route[4:]:
        if outside(p[0],p[2],.99):continue
        y=water_at(p[0],p[2])
        assert y is not None, (name,'disconnected channel',p)
        expected=max(Watershed.SEA_LEVEL,p[1]) if T.sea else p[1]
        assert abs(y-expected)<.13, (name,'channel departs from drainage profile',p,y)
    data=(ROOT/'assets/tracks'/(name+'.tcp')).read_bytes()
    n,=struct.unpack_from('<I',data,4)
    path=[struct.unpack_from('<7f',data,20+i*28) for i in range(n)]
    wet=[i for i,p in enumerate(path) if (y:=water_at(p[0],p[2])) is not None and y>p[1]+.025]
    # Bridges can replace a ford; any remaining flooded sections must be short.
    assert len(wet)<24, (name,'ford has become a flooded road section')
    # Curbs would dam a ford even if water were painted over the pavement.
    for obj in bpy.context.scene.objects:
        if not obj.name.startswith('Alternating safety curb'):continue
        center=sum((obj.matrix_world@v.co for v in obj.data.vertices),Vector())/len(obj.data.vertices)
        if (y:=water_at(center.x,-center.y)) is not None:
            assert center.z<y-.015 or center.z>y+3, (name,'raised curb across watercourse')
    if T.water.get('lake'):
        level=water['water_lake_level']
        # Identify standing water from the exported current, not an X cutoff:
        # the new winding inlet overlaps the pond's bounding box.
        exported=(ROOT/'assets/tracks'/(name+'-water.tcm')).read_bytes()
        rows=[struct.unpack_from('<10f',exported,i) for i in range(8,len(exported),40)]
        pond_vertices=[Vector((v[0],-v[2],v[1])) for v in rows if abs(v[7])+abs(v[8])<1e-6]
        assert len(pond_vertices)>300
        assert all(abs(v.z-level)<1e-5 for v in pond_vertices), 'pond surface must be level'
        area=0
        for i in range(0,len(rows),3):
            a,b,c=rows[i:i+3]
            if all(abs(v[7])+abs(v[8])<1e-6 for v in (a,b,c)):
                area+=abs((b[0]-a[0])*(c[2]-a[2])-(b[2]-a[2])*(c[0]-a[0]))*.5
        envelope=hull_area([(v.x,v.y) for v in pond_vertices])
        if name=='country':
            assert area/envelope<.84, ('pond lost its inlets and sediment promontories',area/envelope)
        assert all(math.hypot(v.x-p[0],-v.y-p[2])>p[5] or p[1]>level+.1
                   for v in pond_vertices[::10] for p in path), 'pond must not flood the road'
    else:
        assert outside(route[-1][0],route[-1][2],1) or T.sea, (name,'stream must leave the map')
    print(f'{name}: sinuosity {distance/chord:.2f}, '
          + (f'width {min(widths):.2f}–{max(widths):.2f}, ' if widths else 'lava vent, ') +
          f'continuous downhill channel, wet road stations {wet}, '
          f"outlet={water['water_destination']}",flush=True)
print('Watershed geometry PASSED',flush=True)
