"""
tctrack - building the track geometry from the centreline.

Everything is built from a single Centerline, so the visuals and the game
logic always agree. Output: a handful of objects (to keep the draw call count
down) plus the JSON for the game.
"""
import bpy
import math
import json
import os

from . import tcmesh as T

SURF_ASPHALT = 0
SURF_DIRT = 1
SURF_SAND = 2
SURF_SNOW = 3
SURF_ICE = 4
SURF_WOOD = 5


# ------------------------------------------------------------ spatial index

class TrackField:
    """
    Fast 'how far and how high is the track' query for any point.

    Two levels of grid: a fine one over the whole centreline for the
    neighbourhood of the road, and a coarse one over every fourth point so a
    query out on the mountains - hundreds of metres away - still resolves in
    a couple of ring steps instead of growing a search over the whole map.
    """

    def __init__(self, cl, cell=6.0, coarse=54.0, stride=4):
        self.cl = cl
        self.cell = cell
        self.coarse_cell = coarse
        self.stride = stride
        self.grid = {}
        self.coarse = {}
        xs, ys = [], []
        for i in range(len(cl)):
            x, y, _ = cl.pos(i)
            xs.append(x)
            ys.append(y)
            self.grid.setdefault((int(x // cell), int(y // cell)), []).append(i)
            if i % stride == 0:
                self.coarse.setdefault(
                    (int(x // coarse), int(y // coarse)), []).append(i)
        self.x0, self.x1 = min(xs), max(xs)
        self.y0, self.y1 = min(ys), max(ys)

    def box_gap(self, x, y):
        """A cheap lower bound on the distance from (x, y) to the road."""
        dx = max(self.x0 - x, x - self.x1, 0.0)
        dy = max(self.y0 - y, y - self.y1, 0.0)
        return math.hypot(dx, dy)

    def _search(self, grid, cell, x, y, rmax):
        cx, cy = int(x // cell), int(y // cell)
        best, bestd, found = None, 1e18, None
        r = 0
        while r <= rmax:
            for dx in range(-r, r + 1):
                for dy in range(-r, r + 1):
                    if r and max(abs(dx), abs(dy)) != r:
                        continue
                    for i in grid.get((cx + dx, cy + dy), ()):
                        px, py, _ = self.cl.pos(i)
                        d = (px - x) ** 2 + (py - y) ** 2
                        if d < bestd:
                            bestd, best = d, i
            if best is not None:
                if found is None:
                    found = r          # one more ring, then the hit is safe
                elif r > found:
                    break
            r += 1
        return best, bestd

    def nearest(self, x, y):
        best, bestd = self._search(self.grid, self.cell, x, y, 6)
        if best is None:
            best, bestd = self._search(self.coarse, self.coarse_cell,
                                       x, y, 16)
            if best is None:                  # nothing anywhere: brute force
                for i in range(0, len(self.cl), self.stride):
                    px, py, _ = self.cl.pos(i)
                    d = (px - x) ** 2 + (py - y) ** 2
                    if d < bestd:
                        bestd, best = d, i
            # the coarse level skips points - refine over the gap it left
            lo = best - self.stride
            for i in range(lo, lo + 2 * self.stride + 1):
                px, py, _ = self.cl.pos(i)
                d = (px - x) ** 2 + (py - y) ** 2
                if d < bestd:
                    bestd, best = d, i
            best %= len(self.cl)
        return best, math.sqrt(bestd)


# ------------------------------------------------------------------ road

def build_road(cl, mats, name="Road", lift=0.06, plank=None, plank_len=1.6):
    """
    The road strip. The material is chosen from the surface of each section,
    so the asphalt -> sand -> snow transition lives in the geometry itself.
    plank: (surface_id, mat) - on the wooden pier the planks alternate.
    """
    n = len(cl)
    verts, faces, fidx = [], [], []
    order = list(mats.keys())
    extra = []
    if plank:
        extra = [plank[1]]
    for i in range(n):
        hw = cl.halfwidth(i)
        l = cl.offset_point(i, hw, lift)
        r = cl.offset_point(i, -hw, lift)
        verts.append(l)
        verts.append(r)
    for i in range(n):
        a = i * 2
        b = ((i + 1) % n) * 2
        faces.append((a, a + 1, b + 1, b))
        surf = cl.surface(i)
        mi = order.index(surf) if surf in order else 0
        if plank and surf == plank[0] and \
                int((i * cl.step) / plank_len) % 2 == 1:
            mi = len(order)
        fidx.append(mi)
    ob = T.mesh_from_pydata(name, verts, faces, None, smooth=False)
    T.face_up(ob)
    for k in order:
        ob.data.materials.append(mats[k])
    for m in extra:
        ob.data.materials.append(m)
    for i, p in enumerate(ob.data.polygons):
        p.material_index = fidx[i]
    T.shade_smooth(ob, angle=math.radians(28))
    return ob


def build_edge_lines(cl, mat, name="Lines", width=0.35, lift=0.075,
                     inset=0.25, skip=None):
    """White edge lines on both sides - readability of the track from above."""
    n = len(cl)
    verts, faces = [], []
    for side in (1, -1):
        base = len(verts)
        for i in range(n):
            hw = cl.halfwidth(i) - inset
            verts.append(cl.offset_point(i, side * hw, lift))
            verts.append(cl.offset_point(i, side * (hw - width), lift))
        for i in range(n):
            if skip and skip(i):
                continue
            a = base + i * 2
            b = base + ((i + 1) % n) * 2
            faces.append((a, a + 1, b + 1, b) if side > 0
                         else (a, b, b + 1, a + 1))
    ob = T.mesh_from_pydata(name, verts, faces, mat, smooth=False)
    T.face_up(ob)
    return ob


def build_edge_border(cl, mat, name="Border", width=0.75, lift=0.062,
                     inset=0.0):
    """A dark border around the road - again for readability from above."""
    n = len(cl)
    verts, faces = [], []
    for side in (1, -1):
        base = len(verts)
        for i in range(n):
            hw = cl.halfwidth(i) + inset
            verts.append(cl.offset_point(i, side * hw, lift))
            verts.append(cl.offset_point(i, side * (hw + width), lift))
        for i in range(n):
            a = base + i * 2
            b = base + ((i + 1) % n) * 2
            faces.append((a, a + 1, b + 1, b) if side > 0
                         else (a, b, b + 1, a + 1))
    ob = T.mesh_from_pydata(name, verts, faces, mat, smooth=False)
    T.face_up(ob)
    return ob


def build_kerbs(cl, mat_a, mat_b, name="Kerbs", width=1.1, height=0.10,
                lift=0.06, curve_min=0.012, block=2.2):
    """
    Red-and-white kerbs in the corners - a classic racing feature and at the
    same time a clear signal of where the track ends.
    """
    n = len(cl)
    verts, faces, fidx = [], [], []
    for i in range(n):
        k = cl.curvature[i]
        if abs(k) < curve_min:
            continue
        side = 1 if k > 0 else -1        # inside of the corner
        for sgn in ((side,) if abs(k) < curve_min * 2.2 else (1, -1)):
            hw = cl.halfwidth(i)
            j = (i + 1) % n
            hw2 = cl.halfwidth(j)
            base = len(verts)
            verts.append(cl.offset_point(i, sgn * hw, lift))
            verts.append(cl.offset_point(i, sgn * (hw + width), lift + height))
            verts.append(cl.offset_point(j, sgn * (hw2 + width), lift + height))
            verts.append(cl.offset_point(j, sgn * hw2, lift))
            if sgn > 0:
                faces.append((base, base + 1, base + 2, base + 3))
            else:
                faces.append((base + 3, base + 2, base + 1, base))
            seg = int((i * cl.step) / block) % 2
            fidx.append(seg)
    ob = T.mesh_from_pydata(name, verts, faces, None, smooth=False)
    T.face_up(ob)
    ob.data.materials.append(mat_a)
    ob.data.materials.append(mat_b)
    for i, p in enumerate(ob.data.polygons):
        p.material_index = fidx[i]
    return ob


def build_ramps(cl, ramps, mats, name="Ramps", lift=0.06):
    """
    Jump ramps. The profile is u^2, so a ramp ends at its steepest - the car
    takes off instead of merely driving up a bump. Side walls and white
    stripes make a ramp readable from a distance.
    """
    if not ramps:
        return None
    parts = []
    n = len(cl)
    for r in ramps:
        i0 = cl.index_at_s(r["s0"])
        i1 = cl.index_at_s(r["s1"])
        count = (i1 - i0) % n
        if count < 2:
            continue
        h = r["height"]
        wfrac = r.get("width", 0.98)
        top, bot = [], []
        verts, faces = [], []
        for k in range(count + 1):
            i = (i0 + k) % n
            u = k / count
            z = h * (u * u)
            hw = cl.halfwidth(i) * wfrac
            top.append(len(verts))
            verts.append(cl.offset_point(i, hw, lift + z))
            verts.append(cl.offset_point(i, -hw, lift + z))
        for k in range(count):
            a, b = k * 2, (k + 1) * 2
            faces.append((a, a + 1, b + 1, b))
        base = len(verts)
        for k in range(count + 1):
            i = (i0 + k) % n
            hw = cl.halfwidth(i) * wfrac
            verts.append(cl.offset_point(i, hw, lift))
            verts.append(cl.offset_point(i, -hw, lift))
        for k in range(count):
            a0, a1 = k * 2, (k + 1) * 2
            b0, b1 = base + k * 2, base + (k + 1) * 2
            faces.append((a0, b0, b1, a1))
            faces.append((a1 + 1, b1 + 1, b0 + 1, a0 + 1))
        faces.append((count * 2, count * 2 + 1,
                      base + count * 2 + 1, base + count * 2))
        parts.append(T.mesh_from_pydata("RampBody", verts, faces, mats["ramp"],
                                        smooth=False))

        # white trims along the sides of the ramp
        for sgn in (1, -1):
            v2, f2 = [], []
            for k in range(count + 1):
                i = (i0 + k) % n
                u = k / count
                z = h * (u * u)
                hw = cl.halfwidth(i) * wfrac
                v2.append(cl.offset_point(i, sgn * hw, lift + z + 0.012))
                v2.append(cl.offset_point(i, sgn * (hw - 0.75),
                                          lift + z + 0.012))
            for k in range(count):
                a, b = k * 2, (k + 1) * 2
                f2.append((a, a + 1, b + 1, b) if sgn > 0
                          else (a, b, b + 1, a + 1))
            parts.append(T.mesh_from_pydata("RampEdge", v2, f2,
                                            mats["ramp_trim"], smooth=False))

        # arrows on the approach
        arrow_mat = mats["ramp_trim"]
        for a_i in range(2):
            k = int(count * (0.25 + 0.32 * a_i))
            j = (i0 + k) % n
            u = k / count
            z = h * (u * u) + lift + 0.02
            c = cl.offset_point(j, 0.0, z)
            nx, ny = cl.normal(j)
            tx, ty = cl.tangent(j)
            w = cl.halfwidth(j) * 0.45
            tip = (c[0] + tx * 2.2, c[1] + ty * 2.2, c[2] + h * 0.05)
            v3 = [tip,
                  (c[0] + nx * w, c[1] + ny * w, c[2]),
                  (c[0] + nx * w * 0.42, c[1] + ny * w * 0.42, c[2]),
                  (c[0] - nx * w * 0.42, c[1] - ny * w * 0.42, c[2]),
                  (c[0] - nx * w, c[1] - ny * w, c[2])]
            f3 = [(0, 1, 2), (0, 2, 3), (0, 3, 4)]
            parts.append(T.mesh_from_pydata("RampArrow", v3, f3, arrow_mat,
                                            smooth=False))
    if not parts:
        return None
    return T.join(parts, name)


def build_start_line(cl, s, mats, width_frac=1.0, name="StartLine"):
    """The chequered start line."""
    n = len(cl)
    i0 = cl.index_at_s(s)
    depth = max(2, int(1.6 / cl.step))
    cells = 12
    verts, faces, fidx = [], [], []
    for k in range(depth + 1):
        i = (i0 + k) % n
        hw = cl.halfwidth(i) * width_frac
        for c in range(cells + 1):
            u = c / cells
            verts.append(cl.offset_point(i, hw - 2 * hw * u, 0.08))
    for k in range(depth):
        for c in range(cells):
            a = k * (cells + 1) + c
            b = (k + 1) * (cells + 1) + c
            faces.append((a, a + 1, b + 1, b))
            fidx.append((k + c) % 2)
    ob = T.mesh_from_pydata(name, verts, faces, None, smooth=False)
    T.face_up(ob)
    ob.data.materials.append(mats["check_a"])
    ob.data.materials.append(mats["check_b"])
    for i, p in enumerate(ob.data.polygons):
        p.material_index = fidx[i]
    return ob


# ------------------------------------------------------------------ terrain

def terrain_height(cl, tf, theme, land, x, y):
    """
    Ground level at a point - the mesh, the props and the landmarks all share
    this one formula.

    Next to the road there is a flat verge continuing the carriageway; it
    reaches past the invisible wall the car is held behind, so the car can
    never be seen floating above the ground. Beyond it the ground blends into
    the landscape (`land`) over a short run-out, which is what turns the road
    into a cutting where the hillside is higher and an embankment where it
    falls away.
    """
    gap = tf.box_gap(x, y)
    if gap > theme.get("blend_far", 320.0):
        # far past any blend into the road - this is pure landscape, and
        # skipping the lookup keeps the outer terrain cheap
        return land.height(x, y), gap, 7.0, 0.0
    i, d = tf.nearest(x, y)
    hw = cl.halfwidth(i)
    road_z = cl.pos(i)[2]
    edge = hw + theme.get("verge", 6.0)
    apron = road_z - theme.get("verge_drop", 0.10)
    if d <= edge:
        return apron, d, hw, road_z
    hz = land.height(x, y)
    # a tall step needs a longer run-out, otherwise the bank becomes a wall
    fo = max(theme.get("falloff", 22.0),
             abs(hz - apron) / theme.get("max_bank", 1.15))
    u = min(1.0, (d - edge) / fo)
    u = u * u * (3 - 2 * u)
    return apron + (hz - apron) * u, d, hw, road_z


def graded_axis(lo, hi, cell, out, grow=1.30, max_cell=56.0):
    """
    Sample positions along one axis: uniform `cell` over [lo, hi], then
    geometrically growing steps for `out` metres further out.

    A tensor product of two such axes is still a conforming quad grid - no
    cracks and no T-junctions - so the terrain can carry fine detail beside
    the road and still reach the mountains on the horizon without spending
    a hundred thousand triangles on the ground in between.
    """
    n = max(1, int(math.ceil((hi - lo) / cell)))
    xs = [lo + k * cell for k in range(n + 1)]
    c, v = cell, xs[-1]
    while v < hi + out:
        c = min(max_cell, c * grow)
        v += c
        xs.append(v)
    pre, c, v = [], cell, xs[0]
    while v > lo - out:
        c = min(max_cell, c * grow)
        v -= c
        pre.append(v)
    return list(reversed(pre)) + xs


def build_terrain(cl, tf, theme, land, name="Terrain", cell=3.4, margin=56.0,
                  outer=None):
    """
    The height field around the track. Materials are picked per face from the
    distance to the road, the altitude, the steepness and some noise - which
    is how the ground gets grass, rock faces and snowy tops without a single
    texture.
    """
    xs_r = [cl.pos(i)[0] for i in range(len(cl))]
    ys_r = [cl.pos(i)[1] for i in range(len(cl))]
    x0, x1 = min(xs_r) - margin, max(xs_r) + margin
    y0, y1 = min(ys_r) - margin, max(ys_r) + margin
    if outer is None:
        # far enough that no hill or mountain is sliced off at the edge
        outer = max(180.0, land.reach(x0, y0, x1, y1) + 70.0)
    xs = graded_axis(x0, x1, cell, outer)
    ys = graded_axis(y0, y1, cell, outer)
    nx, ny = len(xs), len(ys)

    heights, dists = [], []
    for y in ys:
        for x in xs:
            h, d, hw, road_z = terrain_height(cl, tf, theme, land, x, y)
            heights.append(h)
            dists.append((d, hw, road_z))

    # The nearest centreline point is a step function, so the run-out beside
    # the road comes out slightly terraced. A few Laplacian passes take that
    # off. Only the neighbourhood of the track is touched: further out the
    # field is analytic and already smooth, and smoothing there would just
    # flatten the mountains.
    verge = theme.get("verge", 6.0)
    near = theme.get("smooth_reach", 150.0)
    for _ in range(theme.get("smooth_passes", 4)):
        out = list(heights)
        for iy in range(1, ny - 1):
            row = iy * nx
            for ix in range(1, nx - 1):
                k = row + ix
                d = dists[k][0]
                if d <= dists[k][1] + verge + 1.0 or d > near:
                    continue
                out[k] = (heights[k] * 0.36
                          + (heights[k - 1] + heights[k + 1]
                             + heights[k - nx] + heights[k + nx]) * 0.16)
        heights = out

    verts = []
    for iy, y in enumerate(ys):
        for ix, x in enumerate(xs):
            verts.append((x, y, heights[iy * nx + ix]))
    faces, fidx = [], []
    rules = theme["terrain_rules"]
    mats = theme["_terrain_mats"]
    noise = land.noise
    for iy in range(ny - 1):
        dy = ys[iy + 1] - ys[iy]
        for ix in range(nx - 1):
            dx = xs[ix + 1] - xs[ix]
            a = iy * nx + ix
            b = a + 1
            c = a + nx + 1
            d = a + nx
            faces.append((a, b, c, d))
            cx = (xs[ix] + xs[ix + 1]) * 0.5
            cy = (ys[iy] + ys[iy + 1]) * 0.5
            ha, hb, hc, hd = heights[a], heights[b], heights[c], heights[d]
            dd = (dists[a][0] + dists[b][0] + dists[c][0] + dists[d][0]) / 4
            hh = (ha + hb + hc + hd) / 4
            rel = hh - dists[a][2]
            slope = math.hypot(((hb - ha) + (hc - hd)) / (2 * dx),
                               ((hd - ha) + (hc - hb)) / (2 * dy))
            nv = noise.fbm(cx * 0.008, cy * 0.008, 3)
            nv += noise.fbm(cx * 0.030, cy * 0.030, 2) * 0.30
            fidx.append(rules(dd, dists[a][1], rel, nv, cx, cy, slope, hh))
    ob = T.mesh_from_pydata(name, verts, faces, None, smooth=True)
    for m in mats:
        ob.data.materials.append(m)
    for i, p in enumerate(ob.data.polygons):
        p.material_index = max(0, min(len(mats) - 1, fidx[i]))
    T.shade_smooth(ob, angle=math.radians(50))
    return ob, min(heights)


def build_water(cl, level, bounds_pad=620.0, mat=None, name="Water"):
    xs = [cl.pos(i)[0] for i in range(len(cl))]
    ys = [cl.pos(i)[1] for i in range(len(cl))]
    x0, x1 = min(xs) - bounds_pad, max(xs) + bounds_pad
    y0, y1 = min(ys) - bounds_pad, max(ys) + bounds_pad
    verts = [(x0, y0, level), (x1, y0, level), (x1, y1, level), (x0, y1, level)]
    return T.mesh_from_pydata(name, verts, [(0, 1, 2, 3)], mat, smooth=False)


# -------------------------------------------------------- track furniture

def build_barriers(cl, mats, name="Barriers", curve_min=0.022, lift=0.06,
                   offset=1.6, height=0.62, post_every=3.4):
    """
    Barriers on the outside of the corners. They also frame the track
    visually, so from above the player reads where a corner leads.
    """
    n = len(cl)
    runs = []
    cur = None
    for i in range(n):
        k = cl.curvature[i]
        if abs(k) > curve_min:
            side = -1 if k > 0 else 1        # outside of the corner
            if cur and cur[2] == side and i - cur[1] <= 3:
                cur = (cur[0], i, side)
            else:
                if cur and cur[1] - cur[0] > 8:
                    runs.append(cur)
                cur = (i, i, side)
    if cur and cur[1] - cur[0] > 8:
        runs.append(cur)

    parts = []
    for a, b, side in runs:
        verts, faces, fidx = [], [], []
        idxs = list(range(a - 4, b + 5))
        for k, i in enumerate(idxs):
            hw = cl.halfwidth(i) + offset
            lo = cl.offset_point(i, side * hw, lift + height * 0.45)
            hi = cl.offset_point(i, side * hw, lift + height)
            verts.append(lo)
            verts.append(hi)
        for k in range(len(idxs) - 1):
            p0, p1 = k * 2, (k + 1) * 2
            quad = (p0, p0 + 1, p1 + 1, p1)
            faces.append(quad if side > 0 else tuple(reversed(quad)))
            fidx.append(int((idxs[k] * cl.step) / 2.6) % 2)
        ob = T.mesh_from_pydata("Rail", verts, faces, None, smooth=False)
        ob.data.materials.append(mats["barrier_a"])
        ob.data.materials.append(mats["barrier_b"])
        for j, poly in enumerate(ob.data.polygons):
            poly.material_index = fidx[j]
        parts.append(ob)

        step_i = max(1, int(post_every / cl.step))
        for i in range(a - 3, b + 4, step_i):
            hw = cl.halfwidth(i) + offset
            p = cl.offset_point(i, side * hw, lift)
            parts.append(T.box("Post", (0.16, 0.16, height),
                               (p[0], p[1], p[2] + height * 0.5),
                               mats["metal"]))
    if not parts:
        return None
    return T.join(parts, name)


def build_edge_poles(cl, mats, name="Poles", every=11.0, offset=2.4,
                     height=1.5):
    """Marker poles along the track - essential for readability in winter."""
    n = len(cl)
    step_i = max(1, int(every / cl.step))
    parts = []
    for i in range(0, n, step_i):
        for side in (1, -1):
            hw = cl.halfwidth(i) + offset
            p = cl.offset_point(i, side * hw, 0.0)
            parts.append(T.cylinder("Pole", 0.075, height, 6,
                                    (p[0], p[1], p[2] + height * 0.5), 'Z',
                                    mats["pole"]))
            parts.append(T.cylinder("PoleTip", 0.09, 0.30, 6,
                                    (p[0], p[1], p[2] + height - 0.12), 'Z',
                                    mats["pole_tip"]))
    if not parts:
        return None
    return T.join(parts, name)


def build_base_plane(cl, mat, pad=700.0, drop=1.5, floor=None,
                     name="BasePlane"):
    """
    A large plane below the terrain - so the sky does not flash through at the
    horizon. `floor` is the lowest point of the terrain mesh; the plane goes
    under that, not merely under the road.
    """
    xs = [cl.pos(i)[0] for i in range(len(cl))]
    ys = [cl.pos(i)[1] for i in range(len(cl))]
    if floor is None:
        floor = min(cl.pos(i)[2] for i in range(len(cl)))
    x0, x1 = min(xs) - pad, max(xs) + pad
    y0, y1 = min(ys) - pad, max(ys) + pad
    z = floor - drop
    verts = [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)]
    return T.mesh_from_pydata(name, verts, [(0, 1, 2, 3)], mat, smooth=False)


def export_track_json(cl, meta, path, racing_line=None):
    """
    The game data. Coordinates are already in the RealityKit convention:
      RK.x = blender.x, RK.y = blender.z (up), RK.z = -blender.y
    """
    n = len(cl)
    px, py, pz, tx, tz, hw, surf, curv = [], [], [], [], [], [], [], []
    for i in range(n):
        x, y, z = cl.pos(i)
        px.append(round(x, 3))
        py.append(round(z, 3))
        pz.append(round(-y, 3))
        a, b = cl.tangent(i)
        tx.append(round(a, 4))
        tz.append(round(-b, 4))
        hw.append(round(cl.halfwidth(i), 3))
        surf.append(cl.surface(i))
        curv.append(round(cl.curvature[i], 5))
    data = dict(meta)
    data.update(dict(
        step=round(cl.step, 4), length=round(cl.length, 3), count=n,
        px=px, py=py, pz=pz, tx=tx, tz=tz, hw=hw, surf=surf, curv=curv,
    ))
    if racing_line is not None:
        data["line"] = [round(v, 3) for v in racing_line]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(data, f, separators=(",", ":"))
    return path
