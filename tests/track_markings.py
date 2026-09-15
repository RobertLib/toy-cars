"""Validate start markings: blender -b --python-exit-code 1 --python tests/track_markings.py."""
import math
from pathlib import Path
import struct

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[1]


def track_position(points, length, distance, lane):
    distance %= length
    index = max(i for i, point in enumerate(points) if point[6] <= distance)
    a, b = points[index], points[(index + 1) % len(points)]
    end = length if index == len(points) - 1 else b[6]
    fraction = (distance - a[6]) / (end - a[6])
    direction = (Vector(b[:3]) - Vector(a[:3])).normalized()
    center = Vector(a[:3]).lerp(Vector(b[:3]), fraction)
    center += Vector((direction.z * lane, 0, -direction.x * lane))
    return Vector((center.x, -center.z, center.y))


for name in ("country", "beach", "winter"):
    bpy.ops.wm.open_mainfile(filepath=str(ROOT / "art" / (name + ".blend")))
    data = (ROOT / "assets" / "tracks" / (name + ".tcp")).read_bytes()
    count, _, _, length = struct.unpack_from("<IIIf", data, 4)
    points = [struct.unpack_from("<7f", data, 20 + i * 28) for i in range(count)]
    exported = (ROOT / "assets" / "tracks" / (name + ".tcm")).read_bytes()
    exported_positions = {exported[i:i + 12] for i in range(8, len(exported), 40)}
    road = bpy.data.objects["Elevated ribbon road"]
    road.data.calc_loop_triangles()
    road_vertices = [road.matrix_world @ v.co for v in road.data.vertices]
    base = min(p[5] for p in points)
    wide = [i for i,p in enumerate(points) if abs(p[5] - 2 * base) < .001]
    assert wide and wide == list(range(wide[0], wide[-1] + 1)), (
        name, "four-lane road must be one continuous constant-width section")
    assert points[wide[-1]][6] - points[wide[0]][6] > 65, (
        name, "four-lane section is too short")
    assert sum(abs(p[5] - base) < .001 for p in points) > count * .6, (
        name, "keep most of the circuit two lanes wide")
    for i,p in enumerate(points):
        left,right = road_vertices[i * 2:i * 2 + 2]
        assert abs(math.hypot(left.x-right.x, left.y-right.y) - 2*p[5]) < .001, (
            name, i, "visible road and gameplay width differ")
    surface = BVHTree.FromPolygons(road_vertices,
                                  [p.vertices[:] for p in road.data.loop_triangles],
                                  all_triangles=True)
    markings = [o for o in bpy.context.scene.objects
                if o.name.startswith(("Starting grid", "Finish road checker", "Extra lane divider", "Center dash"))]
    grids = sorted((o for o in markings if o.name.startswith("Starting grid")), key=lambda o: o.name)
    checkers = [o for o in markings if o.name.startswith("Finish road checker")]
    assert len(grids) == 8 and len(checkers) == 24, (name, "missing start markings")
    dividers = [o for o in markings if o.name.startswith("Extra lane divider")]
    assert len(dividers) >= 16, (name, "missing four-lane markings")
    samples = 0
    for obj in markings:
        obj.data.calc_loop_triangles()
        assert obj.data.loop_triangles, (name, obj.name, "empty paint mesh")
        for triangle in obj.data.loop_triangles:
            a, b, c = [obj.matrix_world @ obj.data.vertices[i].co for i in triangle.vertices]
            for vertex in (a, b, c):
                assert struct.pack("<3f", vertex.x, vertex.z, -vertex.y) in exported_positions, (
                    name, obj.name, "paint is missing from the runtime export")
            # Edges and interiors catch paint bridging above or cutting through bends.
            for sample in (a, b, c, (a + b) / 2, (b + c) / 2, (c + a) / 2, (a + b + c) / 3):
                hit, _, _, _ = surface.ray_cast(sample + Vector((0, 0, 2)), Vector((0, 0, -1)), 4)
                assert hit is not None, (name, obj.name, "paint is outside the road", tuple(sample))
                clearance = sample.z - hit.z
                assert .025 < clearance < .045, (name, obj.name, "paint leaves the surface", clearance)
                samples += 1
    for grid, obj in enumerate(grids):
        # Area weighting avoids shifting the center toward clipped triangle corners.
        center = Vector((0, 0, 0)); area = 0
        for triangle in obj.data.loop_triangles:
            a, b, c = [obj.matrix_world @ obj.data.vertices[i].co for i in triangle.vertices]
            weight = (b - a).cross(c - a).length
            center += (a + b + c) / 3 * weight
            area += weight
        center /= area
        expected = track_position(points, length, -5 - (grid // 2) * 4.2 + 1.7,
                                  2 if grid % 2 else -2)
        assert math.hypot(center.x - expected.x, center.y - expected.y) < .15, (
            name, obj.name, "grid does not line up with its car")
    print(f"{name}: {len(markings)} road markings, {samples} surface samples, 8 grid positions PASSED",
          flush=True)

print("Track marking tests PASSED", flush=True)
