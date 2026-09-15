"""Export bridge rail endpoints from the visible Blender beams."""
import os
import struct
import bpy
from mathutils import Vector


def export_guardrails(path):
    rails = []
    for obj in sorted(bpy.context.scene.objects, key=lambda obj: obj.name):
        if not (obj.name == 'Bridge railing' or obj.name.startswith('Bridge railing.')):
            continue
        ends = []
        for z in (min(v.co.z for v in obj.data.vertices),
                  max(v.co.z for v in obj.data.vertices)):
            v = obj.matrix_world @ Vector((0, 0, z))
            ends.extend((v.x, v.z - 1.1, -v.y))
        rails.append(ends)
    with open(path, 'wb') as f:
        f.write(b'TCG1' + struct.pack('<I', len(rails)))
        for rail in rails:
            f.write(struct.pack('<6f', *rail))


if __name__ == '__main__':
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for name in ('country', 'beach', 'winter'):
        bpy.ops.wm.open_mainfile(filepath=os.path.join(root, 'art', name + '.blend'))
        export_guardrails(os.path.join(root, 'assets', 'tracks', name + '.tcg'))
