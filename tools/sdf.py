"""Signed distance modeling for the vehicles, meshed with OpenVDB inside Blender.

Shapes are numpy functions of broadcastable coordinate arrays in game axes
(x right, y up, z forward): a profile that only depends on (z, y) is evaluated
on that plane and extruded for free. Negative values are inside. Fillets come
from the smooth booleans, so every seam reads like a molded die-cast part.
"""
import math
import numpy as np


def smin(a, b, k):
    """Polynomial smooth union with a fillet radius of about k."""
    if k <= 0:
        return np.minimum(a, b)
    h = np.clip(.5 + .5 * (b - a) / k, 0, 1)
    return b + (a - b) * h - k * h * (1 - h)


def smax(a, b, k):
    return -smin(-a, -b, k)


def union(*parts, k=0):
    d = parts[0]
    for p in parts[1:]:
        d = smin(d, p, k)
    return d


def subtract(a, b, k=0):
    return smax(a, -b, k)


def box(x, y, z, center, half, r=0):
    qx = np.abs(x - center[0]) - half[0] + r
    qy = np.abs(y - center[1]) - half[1] + r
    qz = np.abs(z - center[2]) - half[2] + r
    outside = np.sqrt(np.maximum(qx, 0) ** 2 + np.maximum(qy, 0) ** 2 + np.maximum(qz, 0) ** 2)
    return outside + np.minimum(np.maximum(qx, np.maximum(qy, qz)), 0) - r


def rect2(u, v, center, half, r=0):
    qu = np.abs(u - center[0]) - half[0] + r
    qv = np.abs(v - center[1]) - half[1] + r
    return np.sqrt(np.maximum(qu, 0) ** 2 + np.maximum(qv, 0) ** 2) + np.minimum(np.maximum(qu, qv), 0) - r


def circle2(u, v, center, radius):
    return np.sqrt((u - center[0]) ** 2 + (v - center[1]) ** 2) - radius


def ellipsoid(x, y, z, center, radii):
    """Iquilez's bound: exact on the axes, close enough elsewhere for meshing."""
    px, py, pz = (x - center[0]) / radii[0], (y - center[1]) / radii[1], (z - center[2]) / radii[2]
    k0 = np.sqrt(px * px + py * py + pz * pz)
    k1 = np.sqrt((px / radii[0]) ** 2 + (py / radii[1]) ** 2 + (pz / radii[2]) ** 2)
    return k0 * (k0 - 1) / np.maximum(k1, 1e-9)


def capsule(x, y, z, a, b, r):
    pa = [x - a[0], y - a[1], z - a[2]]
    ba = [b[0] - a[0], b[1] - a[1], b[2] - a[2]]
    h = np.clip((pa[0] * ba[0] + pa[1] * ba[1] + pa[2] * ba[2]) / (ba[0] ** 2 + ba[1] ** 2 + ba[2] ** 2), 0, 1)
    return np.sqrt((pa[0] - ba[0] * h) ** 2 + (pa[1] - ba[1] * h) ** 2 + (pa[2] - ba[2] * h) ** 2) - r


def cylinder_x(x, y, z, center, radius, half, r=0):
    """A disc or drum on the x axis, its rims rounded by r."""
    d = np.sqrt((y - center[1]) ** 2 + (z - center[2]) ** 2) - radius + r
    w = np.abs(x - center[0]) - half + r
    return np.minimum(np.maximum(d, w), 0) + np.sqrt(np.maximum(d, 0) ** 2 + np.maximum(w, 0) ** 2) - r


def cylinder_y(x, y, z, center, radius, half, r=0):
    return cylinder_x(y, x, z, (center[1], center[0], center[2]), radius, half, r)


def cylinder_z(x, y, z, center, radius, half, r=0):
    return cylinder_x(z, y, x, (center[2], center[1], center[0]), radius, half, r)


def torus_x(x, y, z, center, major, minor):
    q = np.sqrt((y - center[1]) ** 2 + (z - center[2]) ** 2) - major
    return np.sqrt(q * q + (x - center[0]) ** 2) - minor


def polygon2(u, v, points):
    """Exact signed distance to a closed polygon in the (u, v) plane."""
    n = len(points)
    d = None
    sign = None
    for i in range(n):
        ax, ay = points[i - 1]
        bx, by = points[i]
        ex, ey = bx - ax, by - ay
        wx, wy = u - ax, v - ay
        t = np.clip((wx * ex + wy * ey) / max(ex * ex + ey * ey, 1e-12), 0, 1)
        dist = (wx - ex * t) ** 2 + (wy - ey * t) ** 2
        d = dist if d is None else np.minimum(d, dist)
        # Winding test: crossing count of a ray towards +u.
        c1 = v >= ay
        c2 = v < by
        c3 = ex * wy > ey * wx
        flip = (c1 & c2 & c3) | (~c1 & ~c2 & ~c3)
        sign = flip if sign is None else sign ^ flip
    return np.where(sign, -1.0, 1.0) * np.sqrt(d)


def bezier(points, steps=12):
    """Cubic Catmull-Rom through the points of a closed outline, as a polygon."""
    out = []
    n = len(points)
    for i in range(n):
        p0, p1, p2, p3 = points[i - 1], points[i], points[(i + 1) % n], points[(i + 2) % n]
        for s in range(steps):
            t = s / steps
            t2, t3 = t * t, t * t * t
            out.append(tuple(.5 * ((2 * p1[k]) + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2
                                   + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3) for k in range(2)))
    return out


def rounded_polygon(points, radius, steps=6):
    """Fillets every corner of a polygon given as (u, v, corner radius) or (u, v)."""
    out = []
    n = len(points)
    for i in range(n):
        p = points[i]
        r = p[2] if len(p) > 2 else radius
        a, b = points[i - 1], points[(i + 1) % n]
        if r <= 0:
            out.append((p[0], p[1]))
            continue
        ux, uy = a[0] - p[0], a[1] - p[1]
        vx, vy = b[0] - p[0], b[1] - p[1]
        lu, lv = math.hypot(ux, uy), math.hypot(vx, vy)
        ux, uy, vx, vy = ux / lu, uy / lu, vx / lv, vy / lv
        cos = max(-1, min(1, ux * vx + uy * vy))
        half = math.acos(cos) / 2
        if half < 1e-3 or half > math.pi / 2 - 1e-3:
            out.append((p[0], p[1]))
            continue
        t = min(r / math.tan(half), lu * .49, lv * .49)
        r = t * math.tan(half)
        sx, sy = p[0] + ux * t, p[1] + uy * t
        ex, ey = p[0] + vx * t, p[1] + vy * t
        bx, by = ux + vx, uy + vy
        bl = math.hypot(bx, by)
        cx, cy = p[0] + bx / bl * r / math.sin(half), p[1] + by / bl * r / math.sin(half)
        a0 = math.atan2(sy - cy, sx - cx)
        a1 = math.atan2(ey - cy, ex - cx)
        da = (a1 - a0 + math.pi) % (2 * math.pi) - math.pi
        for s in range(steps + 1):
            ang = a0 + da * s / steps
            out.append((cx + r * math.cos(ang), cy + r * math.sin(ang)))
    return out


class Grid:
    """A dense sampling lattice; voxel centres sit on multiples of the voxel size."""

    def __init__(self, lo, hi, voxel):
        self.voxel = voxel
        self.origin = [math.floor(l / voxel) for l in lo]
        self.shape = [math.ceil(h / voxel) - o + 1 for h, o in zip(hi, self.origin)]
        self.axes = [(np.arange(n) + o) * voxel for n, o in zip(self.shape, self.origin)]

    def evaluate(self, field, chunk=24):
        """Evaluates field(x, y, z) slab by slab along z to bound peak memory."""
        nx, ny, nz = self.shape
        out = np.empty((nx, ny, nz), np.float32)
        x = self.axes[0][:, None, None]
        y = self.axes[1][None, :, None]
        for k in range(0, nz, chunk):
            z = self.axes[2][None, None, k:k + chunk]
            d = field(x, y, z)
            out[:, :, k:k + chunk] = np.broadcast_to(d, (nx, ny, min(chunk, nz - k)))
        return out


def mesh_field(grid, values, adaptivity=0.):
    """OpenVDB's adaptive dual contouring: returns points and polygons in metres."""
    import openvdb
    np.clip(values, -3 * grid.voxel, 3 * grid.voxel, out=values)  # the narrow band is all it needs
    vdb = openvdb.FloatGrid(3 * grid.voxel)
    vdb.copyFromArray(values, ijk=tuple(grid.origin), tolerance=0)
    vdb.transform = openvdb.createLinearTransform(voxelSize=grid.voxel)
    vdb.gridClass = openvdb.GridClass.LEVEL_SET
    points, triangles, quads = vdb.convertToPolygons(isovalue=0.0, adaptivity=adaptivity)
    return np.asarray(points, np.float64), np.asarray(triangles, np.int64), np.asarray(quads, np.int64)


def gradient(grid, values, points):
    """Unit gradients of the sampled field at points, by trilinear central differences."""
    shape = np.array(grid.shape)
    f = points / grid.voxel - np.array(grid.origin)
    i = np.clip(np.floor(f).astype(int), 1, shape - 3)
    t = np.clip(f - i, 0, 1)
    out = np.zeros_like(points)
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (np.where(dx, t[:, 0], 1 - t[:, 0]) * np.where(dy, t[:, 1], 1 - t[:, 1])
                     * np.where(dz, t[:, 2], 1 - t[:, 2]))
                c = i + (dx, dy, dz)
                for axis in range(3):
                    hi, lo = c.copy(), c.copy()
                    hi[:, axis] += 1
                    lo[:, axis] -= 1
                    out[:, axis] += w * (values[hi[:, 0], hi[:, 1], hi[:, 2]] - values[lo[:, 0], lo[:, 1], lo[:, 2]])
    return out / np.maximum(np.linalg.norm(out, axis=1)[:, None], 1e-9)


def bounded(field, lo, hi):
    """Evaluates a detail only inside its box; outside, the box distance bounds it.

    Works on the separable axis arrays that Grid.evaluate passes, so a small
    detail costs its own volume rather than the whole car's.
    """
    def g(x, y, z):
        if np.ndim(x) != 3 or np.ndim(y) != 3 or np.ndim(z) != 3:
            return field(x, y, z)  # scattered points, not a lattice
        masks = [(a.ravel() >= l) & (a.ravel() <= h) for a, l, h in zip((x, y, z), lo, hi)]
        centre = [(l + h) / 2 for l, h in zip(lo, hi)]
        half = [(h - l) / 2 for l, h in zip(lo, hi)]
        out = box(x, y, z, centre, half) + 1e-3
        out = np.broadcast_to(out, np.broadcast_shapes(x.shape, y.shape, z.shape)).copy()
        if not all(m.any() for m in masks):
            return out
        ix, iy, iz = (np.nonzero(m)[0] for m in masks)
        sub = (slice(ix[0], ix[-1] + 1) if x.shape[0] > 1 else slice(None),
               slice(iy[0], iy[-1] + 1) if y.shape[1] > 1 else slice(None),
               slice(iz[0], iz[-1] + 1) if z.shape[2] > 1 else slice(None))
        xs = x[:, :, :][sub[0], :, :] if x.shape[0] > 1 else x
        ys = y[:, sub[1], :] if y.shape[1] > 1 else y
        zs = z[:, :, sub[2]] if z.shape[2] > 1 else z
        out[sub] = np.maximum(field(xs, ys, zs), out[sub])
        return out
    return g


def polyline2(u, v, points):
    """Unsigned distance to an open polyline in the (u, v) plane."""
    d = None
    for (ax, ay), (bx, by) in zip(points[:-1], points[1:]):
        ex, ey = bx - ax, by - ay
        wx, wy = u - ax, v - ay
        t = np.clip((wx * ex + wy * ey) / max(ex * ex + ey * ey, 1e-12), 0, 1)
        dist = (wx - ex * t) ** 2 + (wy - ey * t) ** 2
        d = dist if d is None else np.minimum(d, dist)
    return np.sqrt(d)


class Profile:
    """A closed outline in a plane, tabulated once and sampled bilinearly.

    Lookups cost the same for any outline, so profiles can be swept, crowned
    or tapered with warped coordinates in full 3D at little expense.
    """

    def __init__(self, points, step=.002, margin=.12):
        self.points = points
        us, vs = [p[0] for p in points], [p[1] for p in points]
        self.lo = (min(us) - margin, min(vs) - margin)
        self.step = step
        self.n = (int((max(us) + margin - self.lo[0]) / step) + 2, int((max(vs) + margin - self.lo[1]) / step) + 2)
        u = self.lo[0] + np.arange(self.n[0]) * step
        v = self.lo[1] + np.arange(self.n[1]) * step
        self.table = polygon2(u[:, None], v[None, :], points).astype(np.float32)

    def __call__(self, u, v):
        """Quadratic B-spline sampling: exact on straight edges, with continuous slopes."""
        u, v = np.broadcast_arrays(u, v)
        fu = (u - self.lo[0]) / self.step
        fv = (v - self.lo[1]) / self.step
        cu = np.clip(fu, 1, self.n[0] - 2.001)
        cv = np.clip(fv, 1, self.n[1] - 2.001)
        iu, iv = np.rint(cu).astype(np.int32), np.rint(cv).astype(np.int32)
        tu, tv = cu - iu, cv - iv
        wu = (.5 * (.5 - tu) ** 2, .75 - tu * tu, .5 * (.5 + tu) ** 2)
        wv = (.5 * (.5 - tv) ** 2, .75 - tv * tv, .5 * (.5 + tv) ** 2)
        t = self.table
        d = 0
        for a in range(3):
            row = 0
            for b in range(3):
                row = row + t[iu + a - 1, iv + b - 1] * wv[b]
            d = d + row * wu[a]
        # Beyond the table every point is outside; add the distance to it.
        return d + np.hypot(fu - cu, fv - cv) * self.step


def normals(field, points, h):
    """Central-difference gradients of field at scattered points, normalized."""
    x, y, z = points[:, 0], points[:, 1], points[:, 2]
    g = np.stack([field(x + h, y, z) - field(x - h, y, z),
                  field(x, y + h, z) - field(x, y - h, z),
                  field(x, y, z + h) - field(x, y, z - h)], 1)
    return g / np.maximum(np.linalg.norm(g, axis=1)[:, None], 1e-12)
