"""Validate generated broadcast crews and their road clearance in Blender."""
from pathlib import Path
import struct
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

root = Path(__file__).resolve().parents[1]
for name in ('country', 'beach', 'winter'):
    bpy.ops.wm.open_mainfile(filepath=str(root/'art'/f'{name}.blend'))
    objs = [o for o in bpy.context.scene.objects if 'broadcast_station' in o]
    assert {o['broadcast_station'] for o in objs} == {12,70,205,355,500}
    for prefix, count in [('TV Camera operator jacket',5), ('TV Reporter jacket',1),
                          ('TV Sound engineer jacket',1), ('TV Vision technician jacket',1),
                          ('TV OB van',1), ('TV satellite dish',1), ('TV lens glass',5)]:
        assert sum(o.name.startswith(prefix) for o in objs) == count, (name,prefix)
    raw = (root/'assets'/'tracks'/f'{name}.tcp').read_bytes()
    count, = struct.unpack_from('<I',raw,4)
    path = [struct.unpack_from('<7f',raw,20+i*28) for i in range(count)]
    island = bpy.data.objects['Sculpted island']
    bvh = BVHTree.FromPolygons([island.matrix_world@v.co for v in island.data.vertices],
                               [p.vertices[:] for p in island.data.polygons])
    for o in objs:
        assert 'spectator_group' not in o, 'Crew must be in the static scene batch'
        for corner in o.bound_box:
            p = o.matrix_world@Vector(corner)
            assert min(((p.x-q[0])**2+(-p.y-q[2])**2)**.5-q[5] for q in path) > 1.5, (name,o.name,'road clearance')
        if o.name.startswith('TV crew boot'):
            p = o.matrix_world.translation.copy()
            hit,_,_,_ = bvh.ray_cast(p+Vector((0,0,10)),Vector((0,0,-1)))
            assert hit is not None and abs(p.z-.10-hit.z)<.025, (name,o.name,'ground contact')
    print('PASS broadcast',name,len(objs),'objects; five cameras, eight crew; road clearance and grounded feet')
