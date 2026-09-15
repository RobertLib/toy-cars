"""Blender integration check: exports, habitats and entire animated ground paths.

blender -b --python-exit-code 1 --python tests/wildlife.py
"""
from collections import Counter
from pathlib import Path
import math
import struct
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[1]
for theme, name in enumerate(('country', 'beach', 'winter')):
    bpy.ops.wm.open_mainfile(filepath=str(ROOT/'art'/(name+'.blend')))
    island=bpy.data.objects['Sculpted island']
    ground=BVHTree.FromPolygons([island.matrix_world@v.co for v in island.data.vertices],
                               [p.vertices[:] for p in island.data.polygons])
    def height(x,z):
        hit,_,_,_=ground.ray_cast(Vector((x,-z,150)),Vector((0,0,-1)))
        assert hit is not None
        return hit.z
    raw=(ROOT/'assets/tracks'/(name+'.tcp')).read_bytes()
    count,=struct.unpack_from('<I',raw,4)
    path=[struct.unpack_from('<7f',raw,20+i*28) for i in range(count)]
    raw=(ROOT/'assets/tracks'/(name+'-wildlife.tcf')).read_bytes()
    count,=struct.unpack_from('<I',raw,4)
    assert raw[:4]==b'TCF1' and len(raw)==8+count*72 and count%3==0
    records=list(struct.iter_unpack('<18f',raw[8:]))
    assert all(all(math.isfinite(v) for v in row) and row[14] in range(5) and
               row[15] in range(4) and abs(row[16])<=.21 and abs(row[17])<=.21 for row in records)
    positions={raw[i:i+12] for i in range(8,len(raw),72)}
    static=(ROOT/'assets/tracks'/(name+'.tcm')).read_bytes()
    assert positions.isdisjoint({static[i:i+12] for i in range(8,len(static),40)})
    animals={}
    for obj in bpy.context.scene.objects:
        if 'wildlife_id' not in obj:continue
        animals[obj['wildlife_id']]=obj
        assert not obj.get('drivable')
        for v in obj.data.vertices:
            p=obj.matrix_world@v.co
            assert struct.pack('<3f',p.x,p.z,-p.y) in positions
    counts=Counter(o['wildlife_species'] for o in animals.values())
    assert counts==[Counter(deer=5,hare=5,goose=6,snake=3,bird=9),
                    Counter(snake=4,bird=16),Counter(deer=4,hare=6,bird=7)][theme],counts
    for obj in animals.values():
        x,y,z,_=obj['wildlife_origin'];kind,_,sx,sz=obj['wildlife_motion']
        for step in range(120):
            a=step*math.tau/120
            xx=x+math.cos(a)*(12 if kind==4 else .65)
            zz=z+math.sin(a)*(12 if kind==4 else .65)
            if kind==4:
                assert y-.6>height(xx,zz)+5
            else:
                assert all(math.hypot(xx-p[0],zz-p[2])>p[5]+3 for p in path)
                assert abs(height(xx,zz)-(y+sx*(xx-x)+sz*(zz-z)))<.09,(name,'ground path')
    print(name,dict(counts),'exported; animated paths clear and grounded',flush=True)
print('Wildlife PASSED',flush=True)
