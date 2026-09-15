"""Validate spectator placement and runtime exports using Blender.

blender -b --python-exit-code 1 --python tests/spectators.py
"""
from collections import Counter
from pathlib import Path
import math
import struct

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[1]

for name in ('country', 'beach', 'winter'):
    bpy.ops.wm.open_mainfile(filepath=str(ROOT / 'art' / (name + '.blend')))
    island = bpy.data.objects['Sculpted island']
    ground = BVHTree.FromPolygons(
        [island.matrix_world @ v.co for v in island.data.vertices],
        [p.vertices[:] for p in island.data.polygons])
    data = (ROOT / 'assets/tracks' / (name + '.tcp')).read_bytes()
    count, = struct.unpack_from('<I', data, 4)
    path = [struct.unpack_from('<7f', data, 20 + i * 28) for i in range(count)]
    rendered = (ROOT / 'assets/tracks' / (name + '-crowd.tca')).read_bytes()
    vertex_count, = struct.unpack_from('<I', rendered, 4)
    assert rendered[:4] == b'TCA1' and len(rendered) == 8 + vertex_count * 72
    positions = {rendered[i:i+12] for i in range(8, len(rendered), 72)}
    static = (ROOT / 'assets/tracks' / (name + '.tcm')).read_bytes()
    static_positions = {static[i:i+12] for i in range(8, len(static), 40)}
    assert positions.isdisjoint(static_positions), (name, 'static duplicate of animated crowd')
    records = [struct.unpack_from('<18f', rendered, i) for i in range(8, len(rendered), 72)]
    assert all(all(math.isfinite(v) for v in row) and row[14] in range(5) and
               row[15] in range(4) and .5 <= row[16] <= 2 and .5 <= row[17] <= 2 for row in records)
    spectators = [o for o in bpy.context.scene.objects if 'spectator_group' in o]
    groups = Counter(o['spectator_group'] for o in spectators)
    assert len(groups) == 15 and min(groups.values()) >= 3, (name, groups)
    assert len(set(groups.values())) >= 3, (name, 'group sizes lack variety')
    assert {o['spectator_kind'] for o in spectators} == {'adult', 'child', 'broad', 'senior', 'tall'}
    assert {o['spectator_pose'] for o in spectators} == {'cheer', 'wave', 'clap', 'camera', 'flag'}
    sides = set()
    for obj in spectators:
        assert len(obj['spectator_parts']) == len(obj.data.vertices)
        assert set(obj['spectator_parts']) == ({0, 1, 2, 3} if obj['spectator_pose'] == 'flag' else {0, 1, 2})
        assert not obj.get('drivable'), (name, 'spectator changes driving surface')
        x, y, z = obj['spectator_position']
        nearest = min(path, key=lambda p: (p[0]-x)**2 + (p[2]-z)**2)
        sides.add(math.copysign(1, (x-nearest[0])*nearest[4] - (z-nearest[2])*nearest[3]))
        facing = Vector((math.sin(obj['spectator_yaw']), math.cos(obj['spectator_yaw'])))
        assert facing.dot(Vector((nearest[0]-x, nearest[2]-z)).normalized()) > .95
        vertices = [obj.matrix_world @ v.co for v in obj.data.vertices]
        for vertex in vertices:
            point = (vertex.x, vertex.z, -vertex.y)
            assert struct.pack('<3f', *point) in positions, (name, 'spectator missing from export')
            assert all(math.hypot(point[0]-p[0], point[2]-p[2]) > p[5]+1.25 for p in path), (
                name, 'spectator or flag encroaches on road')
        # The two shoes are the first two blocks; their centers must touch the
        # actual mesh, not the analytic hill function used before road grading.
        for base in (0, 32):
            shoe = vertices[base:base+8]
            center = sum(shoe, Vector()) / 8
            bottom = min(v.z for v in shoe)
            hit, _, _, _ = ground.ray_cast(Vector((center.x, center.y, 100)), Vector((0, 0, -1)))
            assert hit is not None and abs(bottom-hit.z) < .025, (name, 'floating shoe', bottom, hit)
    assert sides == {-1., 1.}, (name, 'crowds only on one side')
    for i, a in enumerate(spectators):
        x, _, z = a['spectator_position']
        assert all(math.hypot(x-b['spectator_position'][0], z-b['spectator_position'][2]) >= 1.64
                   for b in spectators[i+1:]), (name, 'overlapping spectators')
    print(f'{name}: {len(spectators)} spectators in {len(groups)} varied groups; grounded and exported', flush=True)

print('Spectator geometry PASSED', flush=True)
