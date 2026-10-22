"""The land beyond each island: seamless, scenery only, and out to the horizon.

Run with: python3 tests/surroundings.py
"""
from pathlib import Path
import math
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from themes import THEMES  # noqa: E402
import meshdata  # noqa: E402

HORIZON = 700.  # The far-plane fade hides everything beyond about 650 m
BINS = 360


def points(name, floats):
    data = meshdata.read(ROOT / "assets" / "tracks" / name)
    count, = struct.unpack_from("<I", data, 4)
    if floats == 3:
        count *= 3
    values = struct.unpack_from("<%df" % (count * floats), data, 8)
    return [values[i:i + 3] for i in range(0, len(values), floats)]


def key(p):
    return tuple(round(v, 4) for v in p)


def reach(vertices, bins=64):
    """The farthest vertex in each direction from the island's centre."""
    farthest = [0.] * bins
    for x, _, z in vertices:
        b = int((math.atan2(z, x) + math.pi) / (2 * math.pi) * bins) % bins
        farthest[b] = max(farthest[b], math.hypot(x, z))
    return min(farthest)


for T in THEMES:
    name = T.key
    surface = points(name + ".tcs", 3)
    land = points(name + "-land.tcm", 10)
    water = points(name + "-water.tcm", 10)
    # The driving surface still ends at the outline, and no land lies inside it.
    assert max(T.island(x, z) for x, _, z in surface) < 1.0001, (name, "driving surface beyond the outline")
    assert min(T.island(x, z) for x, _, z in land) > .995, (name, "surrounding land inside the island")
    # Every outline edge of the island is also an edge of the land: no slit.
    uses = {}
    for i in range(0, len(surface), 3):
        tri = [key(p) for p in surface[i:i + 3]]
        for a, b in zip(tri, tri[1:] + tri[:1]):
            edge = (a, b) if a < b else (b, a)
            uses[edge] = uses.get(edge, 0) + 1
    outline = {e for e, n in uses.items() if n == 1 and all(T.island(p[0], p[2]) > .995 for p in e)}
    land_edges = set()
    for i in range(0, len(land), 3):
        tri = [key(p) for p in land[i:i + 3]]
        for a, b in zip(tri, tri[1:] + tri[:1]):
            land_edges.add((a, b) if a < b else (b, a))
    missing = outline - land_edges
    assert len(outline) > 150 and not missing, (name, "slit between island and land", len(missing))
    rim = [-1e9] * BINS
    for e in outline:
        for x, y, z in e:
            b = int((math.atan2(z, x) + math.pi) / (2 * math.pi) * BINS) % BINS
            rim[b] = max(rim[b], y)
    # Beside the island the land stays low enough for a chase camera trailing beyond the edge.
    for x, y, z in land:
        a = math.atan2(z, x)
        if math.hypot(x, z) - T.edge_radius(a) < 40:
            b = int((a + math.pi) / (2 * math.pi) * BINS)
            nearby = max(rim[(b + k) % BINS] for k in range(-5, 6))
            assert y < nearby + 25, (name, "land rises too steeply beside the island", (x, y, z))
    assert reach(land) > HORIZON, (name, "land ends before the horizon", reach(land))
    if T.sea:
        assert reach(water) > HORIZON, (name, "sea ends before the horizon", reach(water))
    elif not T.water.get("lake"):
        beyond = max(math.hypot(x, z) - T.edge_radius(math.atan2(z, x)) for x, _, z in water)
        assert beyond > 200, (name, "the outlet stream stops at the island", beyond)
    print(f"{name}: {len(land) // 3} land triangles, {len(outline)} seam edges, "
          f"land reach {reach(land):.0f} m", flush=True)
print("Surroundings PASSED", flush=True)
