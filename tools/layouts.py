"""Circuit layouts for the later tracks, independent of Blender.

Harvest Hills, Sunshine Coast and Alpine Rush keep their Catmull-Rom knots in
build_assets.py. The later circuits are authored as closed polygons of corner
points, each with the radius of its bend: straights run between the arcs, and
the rounded line is resampled to knots every 5 m for the same spline. Canyon
Dash is a hand-placed knot loop. Coordinates are metres, (x, z) in game space.
"""
import math

def rounded(corners, spacing=8.):
    """corners: [(x, z, radius), ...]; radius 0 keeps a kink (avoid)."""
    n = len(corners)
    pieces = []  # list of (start point, end point, arc data or None)
    tangents = []
    for i in range(n):
        p0 = corners[i - 1][:2]; v = corners[i][:2]; p1 = corners[(i + 1) % n][:2]; r = corners[i][2]
        a = (v[0] - p0[0], v[1] - p0[1]); la = math.hypot(*a); a = (a[0] / la, a[1] / la)
        b = (p1[0] - v[0], p1[1] - v[1]); lb = math.hypot(*b); b = (b[0] / lb, b[1] / lb)
        cross = a[0] * b[1] - a[1] * b[0]; dot = a[0] * b[0] + a[1] * b[1]
        theta = math.atan2(cross, dot)  # signed turn
        t = r * math.tan(abs(theta) / 2)
        lim = min(la, lb) * .5
        if t > lim:
            t = lim; r = t / max(1e-9, math.tan(abs(theta) / 2))
        s = (v[0] - a[0] * t, v[1] - a[1] * t); e = (v[0] + b[0] * t, v[1] + b[1] * t)
        tangents.append((s, e, a, theta, r))
    pts = []
    for i in range(n):
        s, e, a, theta, r = tangents[i]
        # arc from s to e
        steps = max(2, int(abs(theta) * r / 1.0))
        h = math.atan2(a[1], a[0])
        x, z = s
        pts.append((x, z))
        for k in range(steps):
            h0 = h; h += theta / steps
            step = 2 * r * math.sin(abs(theta / steps) / 2); hm = (h0 + h) / 2
            x += math.cos(hm) * step; z += math.sin(hm) * step; pts.append((x, z))
        # straight to next arc start
        ns = tangents[(i + 1) % n][0]
        L = math.dist((x, z), ns); m = max(1, int(L))
        for k in range(1, m):
            pts.append((x + (ns[0] - x) * k / m, z + (ns[1] - z) * k / m))
    return pts

def to_knots(pts, spacing=8., start=None):
    if start is not None:
        k0 = min(range(len(pts)), key=lambda i: math.dist(pts[i], start)); pts = pts[k0:] + pts[:k0]
    out = [pts[0]]; acc = 0.
    for a, b in zip(pts, pts[1:]):
        acc += math.dist(a, b)
        if acc >= spacing: out.append(b); acc = 0.
    if math.dist(out[-1], out[0]) < spacing * .6: out.pop()
    return [(round(x, 2), round(z, 2)) for x, z in out]


def knots(layout):
    if 'knots' in layout:
        return layout['knots']
    return to_knots(rounded(layout['corners']), 5., layout['start'])


# A pac-man loop round the canyon floor with a twisted tongue, a slot canyon
# serpentine on the north side and a switchback in the west. One crossing.
CANYON = dict(knots=[
    (-61,-109), (-31,-113), (-1,-114), (29,-112), (59,-105), (87,-92), (109,-72), (125,-45), (129,-22),
    (115,-9), (89,-12), (59,-5), (29,6), (-1,18), (-29,27), (-53,23), (-63,3), (-53,-17), (-29,-25),
    (-1,-19), (29,-5), (59,11), (89,27), (114,41), (121,65), (103,85), (77,87), (57,73), (35,75),
    (21,95), (-1,111), (-31,113), (-61,103), (-85,85), (-99,61), (-85,39), (-97,17), (-119,11),
    (-129,-17), (-123,-49), (-109,-77), (-89,-97)])
# A forest loop with two inward corkscrew curls, woodland esses on the north
# side, a dip into the clearing and a hairpin in the west. Two crossings.
AUTUMN = dict(start=(-73, -121), corners=[
    (-11, -123, 28), (5, -45, 14), (41, -45, 14), (69, -119, 22), (123, -87, 30), (125, -31, 15),
    (109, 15, 16), (73, 25, 16), (61, -11, 16), (97, -15, 16), (127, 39, 25), (121, 81, 30),
    (93, 107, 28), (55, 119, 20), (27, 105, 14), (-1, 121, 14), (-31, 105, 14), (-59, 119, 20),
    (-95, 109, 28), (-125, 69, 20), (-107, 33, 16), (-69, 25, 16), (-57, 61, 16), (-97, 63, 16),
    (-135, -3, 16), (-55, -11, 12), (-61, -53, 12), (-131, -75, 20), (-121, -107, 30)])
# An outer ring, a tongue that dips across the temple ring twice, the ring
# itself round the pyramid and an exit crossing the way in. Three crossings.
JUNGLE = dict(start=(-28, -126), corners=[
    (74.4, -126.8, 40), (130.2, -84, 40), (141.4, -9.6, 40), (130.2, 64.8, 40), (88.4, 113.2, 35),
    (37.2, 128, 22), (16.7, 111.3, 14), (11.2, 46.2, 14), (-27.9, 40.6, 14), (-33.5, 111.3, 14),
    (-65.1, 128, 22), (-119, 102, 40), (-141.4, 36.9, 40), (-125.6, -46.8, 25), (-55.8, -65.4, 20),
    (74.4, -63.5, 30), (81.8, 66.7, 30), (-76.3, 74.1, 30), (-93, -121.2, 30)])
# A figure eight after the classic Japanese circuit, with a switchback in the
# east, esses over the hill and a comb of hairpins in the west. One crossing.
SAKURA = dict(start=(-77, -120), corners=[
    (33, -120, 30), (115, 66, 25), (121, 126, 25), (153, 112, 25), (165, 16, 14), (119, 2, 10),
    (125, -34, 10), (161, -52, 20), (139, -104, 30), (101, -116, 20), (83, -98, 20), (8, 41, 20),
    (-7, 74, 18), (-37, 114, 18), (-67, 80, 18), (-97, 118, 18), (-149, 106, 30), (-165, 56, 16),
    (-55, 42, 12), (-63, 2, 12), (-161, -8, 14), (-167, -34, 16), (-63, -44, 12), (-71, -84, 12),
    (-147, -86, 22), (-129, -120, 30)])
# City blocks: a long quay, square corners and an elevated avenue that passes
# over two side streets. Two crossings.
HARBOR = dict(start=(-15, -133), corners=[
    (-107, -133, 30), (107, -133, 30), (143, -97, 30), (143, -29, 14), (97, -29, 14), (97, 29, 14),
    (143, 29, 14), (143, 94, 30), (107, 133, 30), (44, 133, 16), (44, -84, 16), (-19, -84, 14),
    (-19, 13, 14), (71, 13, 14), (71, 75, 14), (-48, 75, 14), (-48, -32, 14), (-87, -32, 14),
    (-87, 123, 14), (-141, 123, 22), (-143, -73, 30), (-131, -107, 30)])
# Three lobes in a chain: the loch, the castle hill with its keyhole climb and
# the moor, joined by long diagonals that cross twice.
HIGHLAND = dict(start=(-145, -162), corners=[
    (-98, -162, 40), (-94, -60, 30), (-18, 48, 18), (-56, 154, 30), (56, 154, 30), (18, 48, 18),
    (94, -60, 30), (98, -162, 40), (152, -156, 40), (180, -78, 30), (170, 4, 30), (184, 86, 30),
    (152, 154, 40), (98, 162, 30), (94, 68, 30), (0, -68, 30), (-94, 68, 30), (-98, 162, 30),
    (-152, 154, 40), (-184, 86, 30), (-170, 4, 30), (-180, -78, 30), (-152, -156, 40)])
# A coastal ring, a ring on the flank, a lap round the crater rim and a descent
# that crosses the rim road and the way in. Two crossings.
VOLCANO = dict(start=(-23, -186), corners=[
    (97, -184, 50), (182, -96, 50), (187, 64, 50), (117, 174, 50), (-43, 186, 45), (-148, 139, 45),
    (-188, 24, 45), (-168, -91, 40), (-63, -121, 30), (77, -106, 35), (117, -16, 35), (87, 104, 35),
    (-43, 129, 30), (-123, 74, 30), (-128, -16, 25), (-33, -58, 20), (32, -46, 22), (52, 24, 22),
    (12, 69, 22), (-48, 49, 20), (-65, -16, 18), (-91, -158, 25), (-78, -186, 30)])
