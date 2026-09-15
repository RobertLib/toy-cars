"""Validate authored ground contact with Blender's independent ray caster.

blender -b --python-exit-code 1 --python tests/ground_contact.py
"""
from pathlib import Path
import struct

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[1]


def surface(obj):
    obj.data.calc_loop_triangles()
    return BVHTree.FromPolygons(
        [obj.matrix_world @ v.co for v in obj.data.vertices],
        [p.vertices[:] for p in obj.data.loop_triangles], all_triangles=True)


for name in ("country", "beach", "winter"):
    bpy.ops.wm.open_mainfile(filepath=str(ROOT / "art" / (name + ".blend")))
    island = surface(bpy.data.objects["Sculpted island"])
    road = bpy.data.objects["Elevated ribbon road"]
    road.data.calc_loop_triangles()
    clearances = []
    for triangle in road.data.loop_triangles:
        a, b, c = [road.matrix_world @ road.data.vertices[i].co for i in triangle.vertices]
        for sample in (a, b, c, (a + b + c) / 3):
            hit, _, _, _ = island.ray_cast(sample + Vector((0, 0, 10)), Vector((0, 0, -1)))
            assert hit is not None, (name, "road has no terrain below it")
            clearance = sample.z - hit.z
            # The tight country hairpin needs a deeper covered cut where one
            # terrain face spans both sides; most of the bed is about .22 deep.
            assert .015 < clearance < .75, (name, "road must sit on a shallow bed", clearance)
            clearances.append(clearance)
    assert sum(clearances) / len(clearances) < .3, (name, "terrain forms a trench along the road")
    for obj in bpy.context.scene.objects:
        if not obj.name.startswith("Sculpted road bed"):
            continue
        vertices = [obj.matrix_world @ v.co for v in obj.data.vertices]
        # The outside edge intersects the actual terrain all along the loop.
        for i in range(1, len(vertices), 2):
            a, b = vertices[i], vertices[(i + 2) % len(vertices)]
            for fraction in (0, .25, .5, .75):
                sample = a.lerp(b, fraction)
                hit, _, _, _ = island.ray_cast(sample + Vector((0, 0, 10)), Vector((0, 0, -1)))
                assert hit is not None
                assert sample.z - hit.z < .025, (name, "shoulder floats above terrain", sample.z-hit.z)

    data = (ROOT / "assets" / "tracks" / (name + ".tcs")).read_bytes()
    count, = struct.unpack_from("<I", data, 4)
    assert data[:4] == b"TCS1" and len(data) == 8 + count * 36
    actual = {data[i:i + 36] for i in range(8, len(data), 36)}
    expected = set()
    for obj in bpy.context.scene.objects:
        if not obj.get("drivable"):
            continue
        obj.data.calc_loop_triangles()
        for triangle in obj.data.loop_triangles:
            points = [obj.matrix_world @ obj.data.vertices[i].co for i in triangle.vertices]
            a, b, c = points
            if abs((b-a).cross(c-a).z) < 1e-6:
                continue
            expected.add(struct.pack("<9f", *(v for p in points for v in (p.x, p.z, -p.y))))
    assert actual == expected, (name, "collision triangles differ from visible surfaces")
    rendered = (ROOT / "assets" / "tracks" / (name + ".tcm")).read_bytes()
    positions = {rendered[i:i+12] for i in range(8, len(rendered), 40)}
    assert all(data[i:i+12] in positions for i in range(8, len(data), 12)), (
        name, "collision geometry is missing from the rendered export")
    print(f"{name}: {count} matching collision triangles; road clearance "
          f"{min(clearances):.3f}–{max(clearances):.3f}; shoulders grounded", flush=True)

print("Ground contact geometry PASSED", flush=True)
