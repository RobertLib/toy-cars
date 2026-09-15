"""Validate authored ground contact with Blender's independent ray caster.

blender -b --python-exit-code 1 --python tests/ground_contact.py
"""
from pathlib import Path
import math
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
    # Folded inside edges turn triangles upside down; large diagonals across a
    # wide, turning road form ridges even when terrain stays underneath it.
    path_data = (ROOT / "assets" / "tracks" / (name + ".tcp")).read_bytes()
    station_count, = struct.unpack_from("<I", path_data, 4)
    points = [struct.unpack_from("<7f", path_data, 20 + i * 28) for i in range(station_count)]
    edge_vertices = [road.matrix_world @ v.co for v in road.data.vertices[:station_count * 2]]
    for i,p in enumerate(points):
        left,right=edge_vertices[i*2:i*2+2]
        assert abs((left.z+right.z)/2-p[1]) < .001, (
            name, i, "road edges dip independently of the driving profile")
        previous=(i-1)%station_count;following=(i+1)%station_count
        before=math.dist(points[previous][:3],p[:3])
        after=math.dist(p[:3],points[following][:3])
        for side in (0,1):
            a,b,c=(edge_vertices[j*2+side].z for j in (previous,i,following))
            curvature=abs((c-b)/after-(b-a)/before)/((before+after)/2)
            assert curvature < .12, (name,i,"abrupt vertical kink in road edge",curvature)
    cells_per_station = len(road.data.polygons) // station_count
    normals = {}
    for triangle in road.data.loop_triangles:
        assert triangle.normal.z > 0, (name, "folded road triangle", triangle.polygon_index)
        previous = normals.get(triangle.polygon_index)
        station = triangle.polygon_index // cells_per_station
        if previous is not None and points[station][5] > 6:
            angle = math.degrees(previous.angle(triangle.normal))
            assert angle < 1.5, (name, "diagonal ridge across wide road", station, angle)
        normals[triangle.polygon_index] = triangle.normal.copy()
    road_bvh = surface(road)
    posts = [o for o in bpy.context.scene.objects
             if o.name.startswith(('Sign post', 'Reflector post'))]
    assert len([o for o in posts if o.name.startswith('Sign post')]) == 6
    for obj in posts:
        base = obj.matrix_world.translation.copy()
        base.z = min((obj.matrix_world @ v.co).z for v in obj.data.vertices)
        hits = [bvh.ray_cast(base + Vector((0, 0, .1)), Vector((0, 0, -1)))[0]
                for bvh in (island, road_bvh)]
        heights = [hit.z for hit in hits if hit is not None]
        assert heights and -.06 <= base.z - max(heights) <= .015, (
            name, obj.name, 'roadside post is not seated on terrain or bridge deck',
            base.z - max(heights) if heights else None)
    overlaps = []
    for vertex in road.data.vertices[:station_count * 2]:
        p = road.matrix_world @ vertex.co
        hit, _, _, _ = road_bvh.ray_cast(p - Vector((0, 0, .15)), Vector((0, 0, -1)))
        if hit is not None:
            gap = p.z - hit.z
            assert gap - .65 > 3, (name, "bridge edge leaves too little headroom", gap)
            overlaps.append(gap)
    assert overlaps and any(o.name.startswith("Bridge deck underside") for o in bpy.context.scene.objects)
    clearances = []
    bridge_lift = road.get("bridge_lift", [0.] * station_count)
    for triangle in road.data.loop_triangles:
        a, b, c = [road.matrix_world @ road.data.vertices[i].co for i in triangle.vertices]
        for sample in (a, b, c, (a + b + c) / 3):
            hit, _, _, _ = island.ray_cast(sample + Vector((0, 0, 10)), Vector((0, 0, -1)))
            assert hit is not None, (name, "road has no terrain below it")
            clearance = sample.z - hit.z
            # Tight hairpins need a deeper covered cut where a terrain face
            # spans both sides; most of the bed remains about .22 deep.
            bridge = bridge_lift[triangle.polygon_index // cells_per_station] > .05
            assert clearance > .015 and (bridge or clearance < .80), (name, "road must sit on a shallow bed", clearance)
            if not bridge: clearances.append(clearance)
    assert sum(clearances) / len(clearances) < .3, (name, "terrain forms a trench along the road")
    for obj in bpy.context.scene.objects:
        if not obj.name.startswith("Sculpted road bed"):
            continue
        vertices = [obj.matrix_world @ v.co for v in obj.data.vertices]
        # The outside edge intersects the actual terrain all along the loop.
        used = {v for polygon in obj.data.polygons for v in polygon.vertices}
        for i in range(1, len(vertices), 2):
            if i not in used or (i + 2) % len(vertices) not in used: continue
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
