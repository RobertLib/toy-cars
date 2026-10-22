"""Level crossings baked into each diorama, with the path data the runtime drives.

Harvest Hills and Alpine Rush get a short railway between two tunnel portals that
crosses the circuit once at grade. Sunshine Coast has no room for a railway; a
seaside road train runs between two stops on a promenade with a turning loop at
each end, and crosses the circuit in both directions. The terrain is graded to
the rail bed and the tunnel hills before the island mesh is built; geometry is
added afterwards and the path, crossing and barrier data export as TCR1.
"""
import bisect
import math
import struct
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

# Each theme's rail entry gives the road station of the crossing (or its share
# of the lap), the rail angle from the road heading, the distances of the two
# tunnel portals (or road-train stops) from the road, and the kind of line.
KINDS = {'steam': 0, 'road': 1, 'alpine': 2, 'freight': 3}
TUNNEL = 14.        # Hidden track beyond each portal
BED = .62           # Rail top above the graded formation
FORMATION = 2.2     # Half-width of the flat formation
FACADE = 1.6        # Portal wall thickness
STEP = 1.2          # The hillside closes the tunnel this far behind the portal front
OPENING = 2.3       # Half-width of the tunnel mouth
SPRING = 3.7        # Height of the arch springing line above the rail
WALL = 4.9          # Half-width of the portal facade
WING = 6.           # Length of the retaining walls in front of a portal
COVER = 7.4         # Hill crest above the rail
SWEPT = 2.75        # Half-width a passing vehicle sweeps around the path
LANE = 1.7          # Road-train lanes either side of the promenade axis
LOOP = 6.           # Radius of each turning loop


def smoothstep(x):
    x = max(0., min(1., x))
    return x*x*(3-2*x)


class Railway:
    def __init__(self, api, T, path, ribbon, length):
        site = T.rail
        theme = T.index
        self.T = T
        self.api, self.theme, self.path = api, theme, path
        self.length = length
        self.stations = [p[6] for p in path]
        self.index = T.rail_station(path)
        self.road = path[self.index]
        heading = math.atan2(self.road[3], self.road[4]) + math.radians(site['angle'])
        self.u = (math.sin(heading), math.cos(heading))
        self.n = (self.u[1], -self.u[0])
        self.angle = math.radians(site['angle'])
        self.before, self.after = site['before'], site['after']
        self.kind = site['kind']
        self.loop = self.kind == 'road'
        self.gauge = .5 if self.kind == 'alpine' else .72
        ribbon.data.calc_loop_triangles()
        self.road_bvh = BVHTree.FromPolygons(
            [v.co.copy() for v in ribbon.data.vertices],
            [t.vertices[:] for t in ribbon.data.loop_triangles], all_triangles=True)
        # Fit the crossing plane to the actual pavement along the rail.
        half = self.road[5] / abs(math.sin(self.angle))
        self.half = half
        samples = []
        for k in range(-20, 21):
            t = half * .97 * k / 20
            y = self.road_height(*self.at(t))
            assert y is not None, ('crossing leaves the road', theme, t)
            samples.append((t, y))
        mt = sum(t for t, _ in samples) / len(samples)
        my = sum(y for _, y in samples) / len(samples)
        self.b = (sum((t-mt)*(y-my) for t, y in samples) /
                  sum((t-mt)**2 for t, _ in samples))
        self.a = my - self.b * mt
        # Rails follow the pavement's own small undulations across the road.
        self.residual = [(t, y - self.a - self.b*t) for t, y in samples]
        worst = max(abs(r) for _, r in self.residual)
        assert worst < .15, ('crossing is not planar', theme, worst)
        self.drop = (0., 0.)
        self.points = []
        self.span = self.crossing_span()
        self.barrier_s = self.span[0] - 1.2

    # --- Geometry of the line -------------------------------------------------
    def at(self, t, d=0.):
        x, z = self.road[0], self.road[2]
        return x + self.u[0]*t + self.n[0]*d, z + self.u[1]*t + self.n[1]*d

    def local(self, x, z):
        dx, dz = x - self.road[0], z - self.road[2]
        return dx*self.u[0] + dz*self.u[1], dx*self.n[0] + dz*self.n[1]

    def road_height(self, x, z):
        hit, _, _, _ = self.road_bvh.ray_cast(Vector((x, -z, 60)), Vector((0, 0, -1)))
        return None if hit is None else hit.z

    def portal(self, side):
        return self.after if side > 0 else -self.before

    def profile(self, t):
        """Rail top: the pavement plane across the road, easing to each portal."""
        base = self.a + self.b * t
        lo, hi = self.residual[0][0], self.residual[-1][0]
        if lo <= t <= hi:
            k = min(len(self.residual) - 2, int((t - lo) / (hi - lo) * (len(self.residual) - 1)))
            (t0, r0), (t1, r1) = self.residual[k], self.residual[k + 1]
            base += r0 + (r1 - r0) * (t - t0) / (t1 - t0)
        else:
            edge = self.residual[-1 if t > hi else 0]
            base += edge[1] * max(0., 1 - abs(t - edge[0]) / 2)
        side = 1 if t > 0 else -1
        start = self.half + 4
        end = self.after if side > 0 else self.before
        return base + self.drop[side > 0] * smoothstep((abs(t) - start) / (end - start))

    def prepare(self, natural):
        """Called once the base terrain exists: choose the grades and the path."""
        self.natural = natural
        if self.loop:
            self.prepare_promenade()
            return
        drops = []
        for side in (-1, 1):
            t = self.portal(side)
            ground = natural(*self.at(t))
            limit = self.T.rail.get('limit', 1.2)
            drops.append(max(-limit, min(limit, .35 * (ground - (self.a + self.b*t)))))
        self.drop = tuple(drops)
        start, end = -self.before - TUNNEL, self.after + TUNNEL
        count = int((end - start) / .8)
        for k in range(count + 1):
            t = start + (end - start) * k / count
            x, z = self.at(t)
            self.points.append((x, self.profile(t), z))

    def prepare_promenade(self):
        """A closed road-train route: out on one lane, around a loop, back."""
        d, r = self.after, LOOP
        control = []
        for t in range(-int(self.before) + 2, int(d) - 1, 3):
            control.append((t, LANE))
        for k in range(1, 12):
            a = math.pi - k * math.pi * 2 / 12
            control.append((d + r + math.cos(a) * r * 1.05, math.sin(a) * r))
        for t in range(int(d) - 2, -int(self.before) + 1, -3):
            control.append((t, -LANE))
        for k in range(1, 12):
            a = -k * math.pi * 2 / 12
            control.append((-self.before - r + math.cos(a) * r * 1.05, math.sin(a) * r))
        for _ in range(4):
            smooth = []
            for p, q in zip(control, control[1:] + control[:1]):
                smooth.append((p[0]*.75 + q[0]*.25, p[1]*.75 + q[1]*.25))
                smooth.append((p[0]*.25 + q[0]*.75, p[1]*.25 + q[1]*.75))
            control = smooth
        # Resample at an even spacing for the runtime.
        lengths = [0.]
        for p, q in zip(control, control[1:] + control[:1]):
            lengths.append(lengths[-1] + math.dist(p, q))
        total = lengths[-1]
        count = int(total / .6)
        local = []
        j = 0
        for k in range(count):
            s = total * k / count
            while lengths[j + 1] < s:
                j += 1
            p, q = control[j], control[(j + 1) % len(control)]
            f = (s - lengths[j]) / max(1e-9, lengths[j + 1] - lengths[j])
            local.append((p[0] + (q[0]-p[0])*f, p[1] + (q[1]-p[1])*f))
        # Promenade height: the pavement where it crosses, eased into the sand.
        base = []
        for t, dd in local:
            x, z = self.at(t, dd)
            road = self.road_height(x, z) if abs(t) < self.half + 1 else None
            base.append((road, self.natural(x, z) + .06))
        heights = []
        n = len(local)
        for k in range(n):
            window = [base[(k + j) % n][1] for j in range(-10, 11)]
            heights.append(sum(window) / len(window))
        for k, (t, dd) in enumerate(local):
            plane = self.a + self.b * t + .005
            f = smoothstep((abs(t) - self.half - .5) / 4)
            heights[k] = plane + (heights[k] - plane) * f
        for k, ((t, dd), y) in enumerate(zip(local, heights)):
            x, z = self.at(t, dd)
            self.points.append((x, y, z))
        self.local_path = local
        self.tree = KDTree(len(self.points))
        for k, p in enumerate(self.points):
            self.tree.insert(Vector((p[0], 0, p[2])), k)
        self.tree.balance()

    # --- Distances along the exported path --------------------------------------
    def distances(self):
        out = [0.]
        for p, q in zip(self.points, self.points[1:]):
            out.append(out[-1] + math.dist(p, q))
        return out

    def nearest_path(self, x, z):
        _, k, distance = self.tree.find(Vector((x, 0, z)))
        return distance, k

    # --- Terrain ------------------------------------------------------------------
    def grade(self, x, z, y, road_gap, wet):
        """Terrain height after the rail bed, the tunnel hills or the promenade.

        road_gap is the distance beyond the nearest road edge, wet the distance
        inside the nearest water (negative outside it)."""
        if self.loop:
            distance, k = self.nearest_path(x, z)
            if distance > 5:
                return y
            weight = 1 - smoothstep((distance - 2.6) / 2.4)
            weight *= smoothstep((road_gap - .6) / 1.6)
            return y + (self.points[k][1] - .06 - y) * weight
        t, d = self.local(x, z)
        # A broad dome over each tunnel, easing down towards nearby roads and
        # leaving a stream its valley.
        clear = smoothstep((road_gap - 2) / 10) * smoothstep((-wet - 3) / 6)
        for side in (-1, 1):
            inward = (t - self.portal(side)) * side
            r = math.hypot((inward - 8) / 19, d / 17)
            if r >= 1:
                continue
            peak = self.profile(self.portal(side)) + COVER
            if peak > y:
                y += (peak - y) * smoothstep((1 - r) / .62) * clear
        inward = max((t - self.after), (-self.before - t))
        if inward >= STEP or abs(d) > 30:
            return y
        bed = self.profile(t) - BED
        wing = smoothstep((inward + WING) / WING)
        core = FORMATION + (WALL - .45 - FORMATION) * wing
        slope = max(1., abs(y - bed) * 1.5) * (1 - wing) + .25 * wing
        f = smoothstep((abs(d) - core) / slope)
        target = bed + (y - bed) * f
        weight = smoothstep((road_gap - 1.5) / 2.5)
        return y + (target - y) * weight

    def refine_size(self, x, z):
        """Wanted terrain edge length near the line, or None to leave it coarse."""
        if self.loop:
            distance, _ = self.nearest_path(x, z)
            return 1.4 if distance < 6 else None
        t, d = self.local(x, z)
        for side in (-1, 1):
            inward = (t - self.portal(side)) * side
            if -WING - 1.5 < inward < 3 and abs(d) < WALL + 2:
                return .6
        if abs(d) < 9 and -self.before - 3 < t < self.after + 3:
            return 1.2
        return None

    def refine(self, vs, fs):
        """Halve long edges near the line; split triangles without slivers."""
        for _ in range(4):
            split = {}
            for face in fs:
                for a, b in zip(face, face[1:] + face[:1]):
                    key = (a, b) if a < b else (b, a)
                    if key in split:
                        continue
                    p, q = vs[a], vs[b]
                    size = self.refine_size((p[0] + q[0]) / 2, (p[2] + q[2]) / 2)
                    if size is None:
                        size = min((s for s in (self.refine_size(p[0], p[2]), self.refine_size(q[0], q[2]))
                                    if s is not None), default=None)
                    if size is not None and math.hypot(p[0] - q[0], p[2] - q[2]) > size:
                        split[key] = len(vs)
                        vs.append(tuple((u + v) / 2 for u, v in zip(p, q)))
            if not split:
                break
            refined = []
            for a, b, c in fs:
                m = [split.get((u, v) if u < v else (v, u)) for u, v in ((a, b), (b, c), (c, a))]
                count = sum(x is not None for x in m)
                if count == 0:
                    refined.append((a, b, c))
                elif count == 3:
                    ab, bc, ca = m
                    refined.extend([(a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)])
                else:
                    # Rotate so the first split edge starts at the first corner.
                    corners, mids = [a, b, c], m
                    while mids[0] is None:
                        corners, mids = corners[1:] + corners[:1], mids[1:] + mids[:1]
                    a2, b2, c2 = corners
                    ab, bc, ca = mids
                    if count == 1:
                        refined.extend([(a2, ab, c2), (ab, b2, c2)])
                    elif bc is not None:
                        refined.extend([(a2, ab, c2), (ab, b2, bc), (ab, bc, c2)])
                    else:
                        refined.extend([(a2, ab, ca), (ab, b2, c2), (ab, c2, ca)])
            fs = refined
        return vs, fs

    def darkness(self, vs, fs):
        """Split off the hillside faces closing each tunnel; they render black."""
        if self.loop:
            return vs, fs, []
        dark = []
        for k, face in enumerate(fs):
            cx = sum(vs[i][0] for i in face) / 3
            cz = sum(vs[i][2] for i in face) / 3
            t, d = self.local(cx, cz)
            a, b, c = (Vector(vs[i]) for i in face)
            normal = (b - a).cross(c - a).normalized()
            for side in (-1, 1):
                inward = (t - self.portal(side)) * side
                # Only the steep step behind the mouth, not the hill above it.
                if 0 < inward < STEP + .6 and abs(d) < OPENING + .4 and abs(normal.y) < .6:
                    dark.append(k)
        split = []
        for k in dark:
            face = []
            for i in fs[k]:
                face.append(len(vs))
                vs.append(vs[i])
            fs[k] = tuple(face)
            split.extend(face)
        return vs, fs, split

    def clear(self, x, z, radius):
        """True when scenery of this radius stays off the line and its hills."""
        if self.loop:
            distance, _ = self.nearest_path(x, z)
            if distance < radius + 4:
                return False
            for t in (-self.before - LOOP - 4.5, self.after + LOOP + 4.5):
                sx, sz = self.at(t)
                if math.hypot(x - sx, z - sz) < radius + 5:
                    return False
            return True
        t, d = self.local(x, z)
        if -self.before - 2 < t < self.after + 2 and abs(d) < radius + 6:
            return False
        for side in (-1, 1):
            inward = (t - self.portal(side)) * side
            if -WING - 2 < inward < 6 and abs(d) < radius + WALL + 2:
                return False
        return True

    # --- Road geometry around the crossing -------------------------------------------
    def road_at(self, s, lane):
        path = self.path
        length = self.length
        s %= length
        k = bisect.bisect_right(self.stations, s) - 1
        a, b = path[k], path[(k + 1) % len(path)]
        end = length if k == len(path) - 1 else b[6]
        f = (s - a[6]) / (end - a[6])
        x, z = a[0] + (b[0]-a[0])*f, a[2] + (b[2]-a[2])*f
        dx, dz = b[0]-a[0], b[2]-a[2]
        l = math.hypot(dx, dz)
        dx, dz = dx/l, dz/l
        return x + dz*lane, z - dx*lane, (dx, dz)

    def swept(self, x, z, margin):
        """Distance from the vehicles' swept band, negative inside it."""
        if self.loop:
            t, d = self.local(x, z)
            if abs(t) > self.half + 8:
                return 1e9
            return abs(d) - (LANE + 1.2 + margin)
        _, d = self.local(x, z)
        return abs(d) - (SWEPT + margin)

    def crossing_span(self):
        """Road distances where any lane meets the swept band."""
        half = self.road[5]
        lo, hi = 1e9, -1e9
        s0 = self.road[6]
        for k in range(-30, 31):
            lane = half * k / 30
            for j in range(-400, 401):
                s = s0 + j * .05
                x, z, _ = self.road_at(s, lane)
                if self.swept(x, z, 0) <= 0:
                    lo, hi = min(lo, s), max(hi, s)
        assert lo < hi, 'crossing span not found'
        return lo, hi

    # --- Scene ----------------------------------------------------------------------
    def near_road(self, s, margin):
        """True within margin metres of the barriers and the crossing."""
        lo, hi = self.barrier_s - margin, self.span[1] + margin
        return any(lo <= s + k * self.length <= hi for k in (-1, 0, 1))

    def build(self, surface, ground):
        """Adds the scene and returns hidden markers that keep scenery away."""
        api = self.api
        self.ground = ground
        self.objects = []
        self.road_s = self.road[6]
        self.stop_s = self.barrier_s - 2.2
        self.clear_s = self.span[1] + 2.0
        if self.loop:
            self.build_promenade(surface, self.length)
        else:
            self.build_track(surface, self.length)
            self.build_portals()
        self.build_equipment()
        markers = []
        hidden = api['mat']('Railway clearance', (1, 0, 1))
        if self.loop:
            for k in range(0, len(self.points), 6):
                x, y, z = self.points[k]
                markers.append(api['box']('Railway clearance', (x, y + 1, z), (5, 2, 5), hidden))
            for t in self.stops:
                x, z = self.at(t + (3.6 if t > 0 else -3.6))
                markers.append(api['box']('Railway clearance', (x, self.ground(x, z) + 1, z), (9, 2, 9), hidden))
        else:
            for k in range(int(-self.before - 8), int(self.after + 9), 4):
                x, z = self.at(k)
                markers.append(api['box']('Railway clearance', (x, self.profile(k) + 1, z), (8, 2, 8), hidden))
            for side in (-1, 1):
                x, z = self.at(self.portal(side) + side * 2)
                markers.append(api['box']('Railway clearance', (x, self.profile(0) + 1, z),
                                          (WALL * 2 + 4, 2, WALL * 2 + 4), hidden))
        for marker in markers:
            marker.hide_render = True
        return markers

    def track_sections(self, start, end, step):
        """(x, y, z, ux, uz) frames along the straight line between two distances."""
        out = []
        count = max(1, int(math.ceil((end - start) / step)))
        for k in range(count + 1):
            t = start + (end - start) * k / count
            x, z = self.at(t)
            out.append((x, self.profile(t), z, self.u[0], self.u[1]))
        return out

    def sweep(self, name, frames, section, material):
        vs, fs = [], []
        width = len(section)
        for x, y, z, ux, uz in frames:
            nx, nz = uz, -ux
            for lateral, height in section:
                vs.append((x + nx*lateral, y + height, z + nz*lateral))
        for i in range(len(frames) - 1):
            for j in range(width - 1):
                a = i*width + j
                fs.append((a, (i+1)*width + j, (i+1)*width + j + 1, a + 1))
        obj = self.api['mesh'](name, vs, fs, material)
        self.objects.append(obj)
        return obj

    def build_track(self, surface, length):
        api = self.api
        mat = api['mat']
        theme = self.theme
        ballast = mat('Rail ballast ' + str(theme), self.T.rail['ballast'])
        steel = mat('Rail steel', (.40, .38, .36))
        sleeper = mat('Rail sleepers ' + str(theme), self.T.rail['sleeper'])
        panel = mat('Crossing panels', (.47, .44, .39))
        # Off the road: ballast shoulders, sleepers and rails into each tunnel.
        edge = self.half + 1.2
        for lo, hi, name in ((-self.before - 6, -edge, 'north'), (edge, self.after + 6, 'south')):
            frames = self.track_sections(lo, hi, 1.)
            self.sweep('Rail ballast', frames,
                       [(-2.5, -1.0), (-1.55, -.32), (1.55, -.32), (2.5, -1.0)], ballast)
            for side in (-1, 1):
                g = self.gauge * side
                self.sweep('Rail', frames, [(g - .04, -.16), (g - .04, 0), (g + .04, 0), (g + .04, -.16)],
                           steel)
            vs, fs = [], []
            count = int((hi - lo) / .62)
            for k in range(count + 1):
                t = lo + (hi - lo) * k / count
                x, z = self.at(t)
                y = self.profile(t)
                w = self.gauge + .55
                corners = []
                for along, across in ((-.11, -w), (.11, -w), (.11, w), (-.11, w)):
                    corners.append((x + self.u[0]*along + self.n[0]*across,
                                    z + self.u[1]*along + self.n[1]*across))
                base = len(vs)
                for cx, cz in corners:
                    vs.append((cx, y - .38, cz))
                for cx, cz in corners:
                    vs.append((cx, y - .16, cz))
                fs.extend([(base+4, base+5, base+6, base+7), (base, base+1, base+5, base+4),
                           (base+1, base+2, base+6, base+5), (base+2, base+3, base+7, base+6),
                           (base+3, base, base+4, base+7)])
            obj = api['mesh']('Rail sleepers', vs, fs, sleeper)
            self.objects.append(obj)
        # Across the pavement: flush crossing panels with the rail heads set in.
        self.paint(surface, length, self.band('d', 0, self.gauge + .62), .04, panel, 'Crossing panels')
        for side in (-1, 1):
            self.paint(surface, length, self.band('d', self.gauge * side, .045), .05, steel, 'Crossing rail')

    def band(self, axis, center, half):
        """Two half-planes (a, b, c) with a*x + b*z + c >= 0 around a line offset."""
        ax, az = self.n if axis == 'd' else self.u
        c0 = -(ax * self.road[0] + az * self.road[2]) - center
        return [(ax, az, c0 + half), (-ax, -az, -c0 + half)]

    def paint(self, surface, length, planes, height, material, name):
        """Clip road triangles to a convex region, keeping the road's exact height."""
        vertices, faces = [], []
        lo, hi = self.road[6] - 30, self.road[6] + 30
        for triangle in surface:
            if max(v[0] for v in triangle) < lo or min(v[0] for v in triangle) > hi:
                continue
            polygon = [v[2:] for v in triangle]
            for a, b, c in planes:
                clipped = []
                for p, q in zip(polygon[-1:] + polygon[:-1], polygon):
                    fp, fq = a*p[0] + b*p[2] + c, a*q[0] + b*q[2] + c
                    if (fp >= 0) != (fq >= 0):
                        f = fp / (fp - fq)
                        clipped.append(tuple(u + (v - u) * f for u, v in zip(p, q)))
                    if fq >= 0:
                        clipped.append(q)
                polygon = clipped
                if len(polygon) < 3:
                    break
            if len(polygon) < 3:
                continue
            for k in range(1, len(polygon) - 1):
                a, b, c = polygon[0], polygon[k], polygon[k + 1]
                if abs((b[0]-a[0])*(c[2]-a[2]) - (b[2]-a[2])*(c[0]-a[0])) < 1e-7:
                    continue
                index = len(vertices)
                vertices.extend((v[0], v[1] + height, v[2]) for v in (a, b, c))
                faces.append((index, index + 1, index + 2))
        obj = self.api['mesh'](name, vertices, faces, material)
        self.objects.append(obj)
        return obj

    def build_portals(self):
        api = self.api
        mat = api['mat']
        theme = self.theme
        stone = mat('Portal stone ' + str(theme), self.T.rail['stone'])
        trim = mat('Portal trim ' + str(theme), self.T.rail['trim'])
        darkness = mat('Tunnel darkness', (.025, .025, .03))
        snow = mat('Fresh snow', (.89, .95, .94))
        for side in (-1, 1):
            tp = self.portal(side)
            rail = self.profile(tp)
            # Local frame: forward into the hill, right, origin on the rail at the face.
            fx, fz = self.u[0] * side, self.u[1] * side
            rx, rz = fz, -fx
            ox, oz = self.at(tp)

            def P(a, h, b):
                return (ox + rx*a + fx*b, rail + h, oz + rz*a + fz*b)
            top = COVER - .2
            bottom = -BED - .3
            arch = [(-OPENING, bottom), (-OPENING, SPRING)]
            for k in range(1, 12):
                a = math.pi - k * math.pi / 12
                arch.append((math.cos(a) * OPENING, SPRING + math.sin(a) * OPENING))
            arch += [(OPENING, SPRING), (OPENING, bottom)]
            outline = [(-WALL, bottom), (-WALL, top), (WALL, top), (WALL, bottom)]
            vs, fs = [], []
            for depth in (0, FACADE):
                for a, h in outline + list(reversed(arch)):
                    vs.append(P(a, h, depth))
            n = len(outline) + len(arch)
            fs.append(tuple(range(n)))
            fs.append(tuple(reversed(range(n, 2*n))))
            ring = list(range(n))
            for a, b in zip(ring, ring[1:] + ring[:1]):
                fs.append((a, b, b + n, a + n))
            obj = api['mesh']('Tunnel portal', vs, fs, stone)
            self.objects.append(obj)
            # A stone arch ring, jambs and a parapet frame the mouth.
            yaw = math.atan2(fx, fz)
            ring_v, ring_f = [], []
            for k in range(13):
                a = math.pi - k * math.pi / 12
                for radius in (OPENING, OPENING + .55):
                    for depth in (-.14, 0):
                        ring_v.append(P(math.cos(a) * radius, SPRING + math.sin(a) * radius, depth))
            for k in range(12):
                i, j = k * 4, (k + 1) * 4
                ring_f.extend([(i, i + 2, j + 2, j), (i + 2, i + 3, j + 3, j + 2), (i + 1, i, j, j + 1)])
            self.objects.append(api['mesh']('Portal arch ring', ring_v, ring_f, trim))
            for lateral in (-1, 1):
                self.objects.append(api['box']('Portal jamb', P(lateral * (OPENING + .275), (SPRING + bottom) / 2, -.07),
                                               (.55, SPRING - bottom, .14), trim, .02, yaw))
            self.objects.append(api['box']('Portal parapet', P(0, top + .2, .5), (WALL * 2 + .4, .4, FACADE + .4),
                                           trim, .05, yaw))
            if self.T.rail.get('cap'):
                self.objects.append(api['box']('Portal snow cap', P(0, top + .5, .5),
                                               (WALL * 2 + .5, .22, FACADE + .5), snow, .1, yaw))
            # Retaining walls hold the cutting in front of the face.
            for lateral in (-1, 1):
                inner, outer = lateral * (WALL - .5), lateral * (WALL + .3)
                low = 1.1 - BED
                corners = [(inner, -WING, bottom), (outer, -WING, bottom), (outer, 0, bottom), (inner, 0, bottom),
                           (inner, -WING, low), (outer, -WING, low), (outer, 0, top), (inner, 0, top)]
                vs = [P(a, h, b) for a, b, h in corners]
                fs = [(4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
                if lateral > 0:
                    fs = [tuple(reversed(f)) for f in fs]
                self.objects.append(api['mesh']('Retaining wall', vs, fs, stone))
                self.objects.append(api['box']('Retaining wall coping', P(lateral * (WALL - .1), (top + low) / 2 + .12, -WING / 2),
                                               (.95, .2, math.hypot(WING, top - low) + .1), trim, .03, yaw))
                coping = self.objects[-1]
                coping.rotation_euler = (Matrix.Rotation(yaw, 4, 'Z') @
                                         Matrix.Rotation(-math.atan2(top - low, WING), 4, 'X')).to_euler()
            guard = api['beam']('Tunnel guard', P(-OPENING, 1.1, -.2), P(OPENING, 1.1, -.2), .1, darkness)
            guard.hide_render = True
            guard['guardrail'] = True

    def build_promenade(self, surface, length):
        api = self.api
        mat = api['mat']
        paving = mat('Promenade paving', (.84, .67, .58))
        kerb = mat('Promenade kerb', (.93, .90, .84))
        stripe = mat('Road train crossing paint', (.97, .80, .20))
        # Two lanes of pavers with a kerb outside each turning loop, off the road.
        frames = []
        for k, (x, y, z) in enumerate(self.points):
            p, q = self.points[k - 1], self.points[(k + 1) % len(self.points)]
            dx, dz = q[0] - p[0], q[2] - p[2]
            l = math.hypot(dx, dz)
            t, _ = self.local(x, z)
            frames.append((x, y, z, dx / l, dz / l, abs(t) < self.half + .4))
        runs, current = [], []
        for f in frames + frames[:1]:
            if f[5]:
                if len(current) > 1:
                    runs.append(current)
                current = []
            else:
                current.append(f[:5])
        if len(current) > 1:
            runs.append(current)
        for run in runs:
            self.sweep('Promenade', run, [(-1.75, -.25), (-1.65, .0), (1.65, .0), (1.75, -.25)], paving)
        # Painted edges across the pavement mark where the road train crosses.
        across = self.band('t', 0, self.half + 2)
        for edge in (-1, 1):
            self.paint(surface, length, across + self.band('d', edge * (LANE + 1.5), .14), .04, stripe,
                       'Road train crossing line')
        for k in range(-3, 4):
            self.paint(surface, length, across + self.band('d', k * .95, .22), .035, kerb,
                       'Road train crossing stripe')
        self.build_stops()
        # The promenade and its stops keep clear of every other road.
        checks = [(x, z) for x, _, z in self.points]
        checks += [self.at(t + (4 if t > 0 else -4)) for t in self.stops]
        for x, z in checks:
            if abs(self.local(x, z)[0]) < self.half + 8:
                continue
            for k, p in enumerate(self.path):
                if abs(p[6] - self.road_s) < 12:
                    continue
                gap = math.hypot(p[0] - x, p[2] - z) - p[5]
                assert gap > 3, ('promenade too close to the road', round(gap, 2), k)

    def build_stops(self):
        api = self.api
        mat = api['mat']
        box, beam, cone, ico = (api[k] for k in ('box', 'beam', 'cone', 'ico'))
        canopy = mat('Stop canopy', (.15, .60, .66))
        post = api['cream']
        wood = api['wood']
        sign = mat('Stop sign board', (.97, .78, .22))
        planter = mat('Loop planter', (.90, .86, .78))
        self.stops = []
        for side in (-1, 1):
            t = side * (self.after + LOOP * 2.05) if side > 0 else -(self.before + LOOP * 2.05)
            # Stop at the far side of the loop, where the station stands.
            sx, sz = self.at(t + side * 3.6)
            yaw = math.atan2(-self.u[0] * side, -self.u[1] * side)
            gy = self.ground(sx, sz)
            before = len(self.objects)

            def P(a, h, b):
                return (sx + math.cos(yaw)*a + math.sin(yaw)*b, gy + h, sz - math.sin(yaw)*a + math.cos(yaw)*b)
            self.objects.append(box('Stop platform', P(0, .12, -.4), (6.5, .24, 2.4), planter, .04, yaw))
            for a in (-2.8, 2.8):
                self.objects.append(beam('Stop canopy post', P(a, .2, -.9), P(a, 2.9, -.9), .08, post))
            self.objects.append(box('Stop canopy', P(0, 3.0, -.6), (6.8, .22, 2.6), canopy, .08, yaw))
            for b in (-1.2, 0):
                self.objects.append(box('Stop bench seat', P(-1.2 + b * 1.6, .62, -1.1), (1.6, .1, .45), wood, .02, yaw))
            self.objects.append(beam('Stop sign post', P(3.6, 0, .2), P(3.6, 3.2, .2), .07, post))
            self.objects.append(box('Stop sign', P(3.6, 3.0, .2), (1.3, .8, .08), sign, .04, yaw))
            # A palm planter fills the turning loop.
            cx, cz = self.at(t - side * LOOP * 1.05)
            cy = self.ground(cx, cz)
            self.objects.append(cone('Loop planter', (cx, cy + .3, cz), LOOP - 2.4, LOOP - 2.5, .6, planter, 24))
            self.objects.append(cone('Loop flower bed', (cx, cy + .62, cz), LOOP - 2.7, LOOP - 2.8, .06,
                                     mat('Loop flowers', (.36, .55, .27)), 24))
            api['tree'](cx, cy + .6, cz, .8, 1) if self.T.legacy else api['biomes'].tree(self.T, cx, cy + .6, cz, .8)
            self.stops.append(t)

    def build_equipment(self):
        """Entry barriers, flashing signals and crossbucks, plus a warning sign."""
        api = self.api
        mat = api['mat']
        box, beam = api['box'], api['beam']
        housing = mat('Barrier housing', (.86, .84, .78))
        red = mat('Signal red', (.80, .10, .08))
        white = mat('Signal white', (.95, .94, .90))
        black = mat('Signal black', (.07, .07, .08))
        half = self.road[5]
        self.arms, self.lights = [], []
        s = self.barrier_s
        for side in (-1, 1):
            # Positive lanes lie to the driver's left.
            post_x, post_z, d = self.road_at(s, side * (half + .9))
            yaw = math.atan2(d[0], d[1])
            gy = self.ground(post_x, post_z)
            ry = self.road_height(*self.road_at(s, side * (half - .2))[:2])

            def P(a, h, b, x=post_x, z=post_z, base=gy):
                return (x + math.cos(yaw)*a + math.sin(yaw)*b, base + h, z - math.sin(yaw)*a + math.cos(yaw)*b)
            self.objects.append(box('Barrier housing', P(0, .55, 0), (.55, 1.1, .55), housing, .05, yaw))
            self.objects.append(box('Barrier housing cap', P(0, 1.14, 0), (.62, .1, .62), red, .03, yaw))
            pivot = P(-side * .3, 1.0, .0)
            # The arm points across the road towards its centre line.
            self.arms.append((pivot[0], max(pivot[1], ry + .95), pivot[2],
                              -side * d[1], side * d[0], half + .9 - .25 - .3))
            # Flashing signal facing the approaching cars, with a crossbuck above.
            sx, sz, _ = self.road_at(s + .4, side * (half + 1.9))
            sy = self.ground(sx, sz)

            def Q(a, h, b):
                return P(a, h, b, sx, sz, sy)
            self.objects.append(beam('Signal mast', Q(0, 0, 0), Q(0, 3.75, 0), .075, white))
            for k in range(4):
                self.objects.append(beam('Signal mast band', Q(0, .3 + k * .6, 0), Q(0, .6 + k * .6, 0), .085, black))
            self.objects.append(box('Signal backplate', Q(0, 2.45, -.12), (1.25, .5, .06), black, .05, yaw))
            for lamp in (-1, 1):
                self.objects.append(beam('Signal lamp', Q(lamp * .32, 2.45, -.12), Q(lamp * .32, 2.45, -.36), .17, black))
                self.objects.append(box('Signal visor', Q(lamp * .32, 2.66, -.4), (.4, .05, .3), black, .01, yaw))
            self.lights.append((*Q(0, 2.45, -.37), -d[0], -d[1], .32))
            for lean in (-1, 1):
                for name, depth, size, color in (('Crossbuck border', -.1, (1.45, .3, .04), red),
                                                 ('Crossbuck', -.13 - lean * .01, (1.32, .2, .03), white)):
                    cross = box(name, Q(0, 3.35, depth), size, color, .01)
                    cross.rotation_euler = (Matrix.Rotation(yaw, 4, 'Z') @
                                            Matrix.Rotation(lean * .62, 4, 'Y')).to_euler()
                    self.objects.append(cross)
        # An advance warning sign on the driver's right, well before the barrier.
        wx, wz, d = self.road_at(s - 55, -(half + 2.2))
        yaw = math.atan2(d[0], d[1])
        wy = self.ground(wx, wz)

        def W(a, h, b):
            return (wx + math.cos(yaw)*a + math.sin(yaw)*b, wy + h, wz - math.sin(yaw)*a + math.cos(yaw)*b)
        self.objects.append(beam('Warning sign post', W(0, 0, 0), W(0, 2.4, 0), .06, white))
        tri = [W(-.75, 2.0, -.08), W(.75, 2.0, -.08), W(0, 3.3, -.08)]
        self.objects.append(api['mesh']('Warning sign', tri + [W(-.75, 2.0, -.03), W(.75, 2.0, -.03), W(0, 3.3, -.03)],
                                        [(0, 2, 1), (3, 4, 5), (0, 1, 4, 3), (1, 2, 5, 4), (2, 0, 3, 5)], red))
        inner = [W(-.52, 2.12, -.1), W(.52, 2.12, -.1), W(0, 3.02, -.1)]
        self.objects.append(api['mesh']('Warning sign face', inner, [(0, 2, 1)], white))
        if self.loop:
            self.objects.append(box('Warning sign symbol', W(0, 2.42, -.11), (.5, .22, .02), black, 0, yaw))
            for a in (-.16, .16):
                self.objects.append(box('Warning sign symbol', W(a, 2.27, -.11), (.12, .12, .02), black, 0, yaw))
        else:
            for k in range(3):
                self.objects.append(box('Warning sign symbol', W(-.24 + k * .24, 2.38 + (k % 2) * .08, -.11),
                                        (.07, .44, .02), black, 0, yaw))

    # --- Runtime export ---------------------------------------------------------------
    def export(self, path):
        distances = self.distances()
        total = distances[-1] + (math.dist(self.points[-1], self.points[0]) if self.loop else 0)
        passes = []
        if self.loop:
            for k, (t, _) in enumerate(self.local_path):
                t2, _ = self.local_path[(k + 1) % len(self.local_path)]
                if (t < 0) != (t2 < 0) and abs(t) < 3:
                    f = t / (t - t2)
                    passes.append(distances[k] + f * (distances[k + 1] - distances[k]
                                                      if k + 1 < len(distances) else total - distances[k]))
            stops = []
            for t in self.stops:
                sx, sz = self.at(t)
                _, k = self.nearest_path(sx, sz)
                stops.append(distances[k])
            visible = (0., total)
            zone = self.road[5] + 1.2
        else:
            passes = [self.before + TUNNEL]
            stops = []
            # Vehicles show while any part of them is in front of the dark tunnel ends.
            visible = (TUNNEL - STEP - .3, distances[-1] - TUNNEL + STEP + .3)
            zone = (self.road[5] + 1.6) / abs(math.sin(self.angle)) + SWEPT
        assert len(passes) == (2 if self.loop else 1), passes
        kind = KINDS[self.kind]
        with open(path, 'wb') as f:
            f.write(b'TCR1')
            f.write(struct.pack('<7I', kind, 1 if self.loop else 0, len(self.points), len(passes),
                                len(stops), len(self.arms), len(self.lights)))
            f.write(struct.pack('<6f', self.road_s, self.stop_s, self.clear_s, zone, *visible))
            for p in self.points:
                f.write(struct.pack('<3f', *p))
            for p in passes:
                f.write(struct.pack('<f', p))
            for p in stops:
                f.write(struct.pack('<f', p))
            for arm in self.arms:
                f.write(struct.pack('<6f', *arm))
            for light in self.lights:
                f.write(struct.pack('<6f', *light))
        print('RAILWAY', path.rsplit('/', 1)[-1], 'path', round(total, 1), 'm passes',
              [round(p, 1) for p in passes], 'stops', [round(p, 1) for p in stops],
              'road', round(self.stop_s, 1), round(self.road_s, 1), round(self.clear_s, 1), flush=True)


# --- Rolling stock -------------------------------------------------------------------
# Lengths match the runtime consists in src/game.c. Every model stands on its rails
# (or tyres) at y = 0, centred on its length, facing +Z.
VEHICLES = {
    'steam-locomotive': 7.8, 'steam-tender': 5.4, 'coach': 10.6,
    'alpine-locomotive': 10.4, 'alpine-coach': 11.2,
    'road-train-tractor': 4.4, 'road-train-carriage': 5.2,
    'diesel-locomotive': 12.5, 'container-wagon': 13.1,
}


def build_vehicles(api, root):
    import os
    box, beam, cone, ico, mesh, mat = (api[k] for k in ('box', 'beam', 'cone', 'ico', 'mesh', 'mat'))
    clear, export, save = api['clear'], api['export'], api['save']
    cream, dark, glass, chrome, rubber = (api[k] for k in ('cream', 'dark', 'glass', 'chrome', 'rubber'))
    # Body colours marked as paint take each circuit's livery at runtime.
    green = mat('Locomotive green', (.12, .39, .27), 1)
    black = mat('Locomotive black', (.07, .075, .08))
    red = mat('Buffer beam red', (.74, .13, .10))
    brass = mat('Polished brass', (.88, .68, .28))
    coal = mat('Coal', (.05, .05, .055))
    roof = mat('Coach roof grey', (.42, .44, .45))
    alpine = mat('Alpine railway red', (.76, .09, .08), 1)
    trim = mat('Alpine trim white', (.93, .93, .90))
    machinery = mat('Bogie grey', (.22, .23, .25))
    land = mat('Road train red', (.90, .20, .14))
    sun = mat('Road train yellow', (.98, .78, .18))
    sea = mat('Road train turquoise', (.10, .62, .66))
    wood = api['wood']
    exported = []

    def wheelset(z, y, radius, half, rim, hub, width=.16):
        for side in (-1, 1):
            beam('Wheel', (side * half, y, z), (side * (half + width), y, z), radius, rim, 14)
            beam('Wheel centre', (side * (half + width), y, z), (side * (half + width + .03), y, z),
                 radius * .55, hub, 10)

    def buffers(z, y, sign, spread=.85):
        for side in (-1, 1):
            beam('Buffer', (side * spread, y, z), (side * spread, y, z + sign * .32), .1, black, 8)
            beam('Buffer head', (side * spread, y, z + sign * .32), (side * spread, y, z + sign * .38), .2, chrome, 10)

    def bogie(z, half, radius=.42):
        box('Bogie frame', (0, radius + .05, z), (half * 2 - .1, .32, 2.5), machinery, .04)
        for dz in (-.8, .8):
            wheelset(z + dz, radius, radius, half, black, chrome, .12)

    def done(name):
        # Earlier models are hidden, so each export holds one vehicle.
        export(os.path.join(root, 'assets/models', name + '.tcm'))
        seen = {o for _, objects in exported for o in objects}
        objects = [o for o in api['bpy'].context.scene.objects if o not in seen]
        exported.append((name, objects))
        for o in objects:
            o.hide_render = True

    clear()
    # Steam locomotive: green boiler, red buffer beams, three coupled driving wheels.
    box('Locomotive frame', (0, 1.0, .2), (2.2, .34, 7.0), black, .04)
    box('Running board', (0, 1.42, 1.0), (2.85, .08, 5.0), black, .02)
    box('Running board edge', (0, 1.42, 1.0), (2.9, .05, 5.02), red, .01)
    box('Front buffer beam', (0, 1.0, 3.55), (2.8, .55, .25), red, .03)
    buffers(3.67, 1.0, 1)
    beam('Boiler', (0, 2.12, -1.1), (0, 2.12, 2.6), .82, green, 18)
    for z in (-.3, .8, 1.9):
        beam('Boiler band', (0, 2.12, z), (0, 2.12, z + .07), .845, brass, 18)
    beam('Smokebox', (0, 2.12, 2.6), (0, 2.12, 3.35), .86, black, 18)
    beam('Smokebox door', (0, 2.12, 3.35), (0, 2.12, 3.42), .68, machinery, 18)
    beam('Chimney', (0, 2.8, 2.85), (0, 3.85, 2.85), .27, black, 12)
    cone('Chimney crown', (0, 3.92, 2.85), .26, .38, .22, black, 12)
    ico('Steam dome', (0, 2.92, 1.2), (.42, .38, .42), brass, 2)
    ico('Sand dome', (0, 2.88, .1), (.34, .3, .34), green, 2)
    beam('Whistle', (0, 2.9, -1.0), (0, 3.25, -1.0), .06, brass, 6)
    box('Headlamp', (0, 3.0, 3.38), (.36, .3, .22), cream, .05)
    for side in (-1, 1):
        box('Buffer beam lamp', (side * .7, 1.55, 3.6), (.22, .22, .16), cream, .03)
        beam('Cylinder', (side * 1.2, 1.0, 2.0), (side * 1.2, 1.0, 3.1), .33, green, 12)
        box('Coupling rod', (side * 1.33, .72, .25), (.06, .12, 2.75), chrome, .01)
        box('Connecting rod', (side * 1.36, .85, 1.55), (.05, .1, 1.6), chrome, .01)
    for z in (-1.1, .25, 1.6):
        wheelset(z, .74, .74, 1.0, red, black, .2)
        for side in (-1, 1):
            beam('Tyre', (side * 1.0, .74, z), (side * 1.2, .74, z), .76, black, 16)
            beam('Wheel', (side * 1.2, .74, z), (side * 1.24, .74, z), .66, red, 16)
    wheelset(3.0, .42, .42, 1.0, black, chrome, .14)
    box('Cab', (0, 2.35, -2.35), (2.6, 2.3, 2.1), green, .06)
    box('Cab lining', (0, 2.0, -2.35), (2.64, .08, 2.12), brass, .01)
    box('Cab roof', (0, 3.6, -2.45), (2.9, .14, 2.6), black, .05)
    for side in (-1, 1):
        box('Cab front window', (side * .62, 2.9, -1.29), (.6, .55, .05), glass, .04)
        box('Cab side window', (side * 1.31, 2.9, -2.2), (.05, .6, .9), glass, .04)
    box('Rear buffer beam', (0, 1.0, -3.5), (2.6, .5, .2), red, .03)
    done('steam-locomotive')

    box('Tender frame', (0, 1.0, 0), (2.3, .32, 5.0), black, .04)
    box('Tender tank', (0, 1.95, 0), (2.7, 1.55, 4.9), green, .1)
    box('Tender lining', (0, 2.05, 0), (2.74, .08, 4.94), brass, .01)
    box('Coal load', (0, 2.82, .4), (2.3, .3, 3.6), coal, .12)
    for k in range(7):
        ico('Coal lump', (-.8 + (k % 4) * .5, 2.98, -.9 + k * .45), (.32, .2, .3), coal, 1)
    box('Tool box', (0, 2.95, -1.95), (2.3, .5, .6), green, .04)
    for z in (-1.7, 0, 1.7):
        wheelset(z, .48, .48, 1.0, black, red, .14)
    buffers(2.5, 1.0, 1)
    buffers(-2.5, 1.0, -1)
    done('steam-tender')

    box('Coach underframe', (0, .95, 0), (2.3, .34, 9.9), black, .04)
    box('Coach lower panels', (0, 1.72, 0), (2.85, 1.0, 10.0), green, .05)
    box('Coach upper panels', (0, 2.82, 0), (2.85, 1.2, 10.0), cream, .05)
    box('Coach waist line', (0, 2.24, 0), (2.89, .07, 10.04), brass, .01)
    box('Coach roof', (0, 3.6, 0), (2.95, .38, 10.2), roof, .16)
    for k in range(6):
        z = -3.55 + k * 1.42
        for side in (-1, 1):
            box('Coach window', (side * 1.43, 2.92, z), (.05, .72, 1.0), glass, .04)
    for end in (-1, 1):
        for side in (-1, 1):
            box('Coach door', (side * 1.43, 2.3, end * 4.55), (.06, 1.95, .7), green, .02)
            box('Coach door window', (side * 1.46, 2.95, end * 4.55), (.04, .5, .42), glass, .02)
        buffers(end * 5.0, 1.0, end)
        bogie(end * 3.4, 1.0)
    done('coach')

    # Alpine railway: a red narrow-gauge locomotive and panorama coaches.
    box('Alpine body', (0, 2.25, 0), (2.6, 2.55, 9.4), alpine, .22)
    box('Alpine stripe', (0, 1.55, 0), (2.64, .14, 9.42), trim, .02)
    box('Alpine roof deck', (0, 3.62, 0), (2.1, .2, 7.8), machinery, .05)
    for end in (-1, 1):
        box('Alpine windscreen', (0, 2.95, end * 4.72), (2.05, .85, .06), glass, .05)
        box('Alpine nose', (0, 1.65, end * 4.78), (2.4, .7, .1), trim, .03)
        for side in (-1, 1):
            box('Alpine headlamp', (side * .72, 1.68, end * 4.84), (.28, .22, .06), cream, .03)
            box('Alpine cab window', (side * 1.31, 2.95, end * 3.9), (.05, .7, .9), glass, .04)
            box('Alpine grille', (side * 1.31, 2.75, end * 1.4), (.05, .7, 1.6), machinery, .02)
        box('Alpine top lamp', (0, 3.5, end * 4.76), (.26, .2, .06), cream, .03)
        buffers(end * 4.85, 1.05, end, .7)
        bogie(end * 2.9, .75, .45)
    # Pantograph: a folded diamond on insulators.
    for side in (-1, 1):
        beam('Insulator', (side * .55, 3.72, -.6), (side * .55, 3.95, -.6), .07, cream, 8)
        beam('Pantograph arm', (side * .55, 3.95, -.6), (side * .2, 4.45, .35), .035, dark, 6)
        beam('Pantograph arm', (side * .2, 4.45, .35), (side * .5, 4.75, -.2), .035, dark, 6)
    box('Pantograph head', (0, 4.78, -.2), (1.5, .06, .2), dark, .01)
    done('alpine-locomotive')

    box('Panorama underframe', (0, 1.0, 0), (2.2, .34, 10.6), machinery, .04)
    box('Panorama lower body', (0, 1.75, 0), (2.6, 1.15, 10.8), alpine, .06)
    box('Panorama stripe', (0, 2.27, 0), (2.64, .1, 10.84), trim, .01)
    box('Panorama glazing', (0, 3.0, 0), (2.55, 1.35, 10.0), glass, .08)
    box('Panorama roof glazing', (0, 3.72, 0), (2.2, .32, 9.8), glass, .14)
    box('Panorama roof spine', (0, 3.86, 0), (.5, .1, 9.9), alpine, .03)
    for k in range(7):
        z = -4.5 + k * 1.5
        for side in (-1, 1):
            box('Panorama pillar', (side * 1.28, 3.0, z), (.06, 1.35, .14), alpine, .01)
    for end in (-1, 1):
        box('Panorama end wall', (0, 2.6, end * 5.15), (2.6, 2.75, .3), alpine, .08)
        for side in (-1, 1):
            box('Panorama door', (side * 1.3, 2.25, end * 4.6), (.06, 1.9, .7), trim, .02)
        buffers(end * 5.3, 1.05, end, .7)
        bogie(end * 3.7, .75, .45)
    done('alpine-coach')

    # Seaside road train: a toy locomotive on tyres and open carriages.
    box('Road train chassis', (0, .62, 0), (1.8, .4, 4.1), dark, .05)
    beam('Road train boiler', (0, 1.32, .2), (0, 1.32, 1.95), .62, land, 16)
    beam('Road train boiler band', (0, 1.32, 1.1), (0, 1.32, 1.17), .64, sun, 16)
    beam('Road train smokebox', (0, 1.32, 1.95), (0, 1.32, 2.12), .66, dark, 16)
    beam('Road train chimney', (0, 1.8, 1.6), (0, 2.45, 1.6), .18, dark, 10)
    cone('Road train chimney crown', (0, 2.52, 1.6), .17, .3, .2, sun, 10)
    ico('Road train bell', (0, 2.0, .75), (.17, .2, .17), brass, 1)
    box('Road train headlamp', (0, 1.95, 2.14), (.3, .25, .12), cream, .04)
    box('Road train bumper', (0, .62, 2.15), (2.0, .25, .15), sun, .04)
    box('Road train cab floor', (0, .95, -1.05), (1.9, .25, 1.9), land, .05)
    box('Road train cab back', (0, 1.6, -1.95), (1.9, 1.3, .12), land, .04)
    box('Road train seat', (0, 1.25, -1.45), (.9, .3, .55), sea, .05)
    for x in (-.9, .9):
        for z in (-1.95, -.15):
            beam('Road train canopy post', (x, 1.0, z), (x, 2.62, z), .045, cream, 6)
    box('Road train canopy', (0, 2.7, -1.0), (2.15, .12, 2.2), sun, .05)
    box('Road train driver', (0, 1.72, -1.3), (.55, .62, .35), sea, .08)
    ico('Road train driver head', (0, 2.2, -1.25), (.2, .23, .2), mat('Driver skin', (.86, .62, .45)), 1)
    box('Road train driver cap', (0, 2.42, -1.2), (.42, .08, .42), cream, .02)
    for z in (1.45, -1.3):
        for side in (-1, 1):
            beam('Road train tyre', (side * .82, .42, z), (side * 1.1, .42, z), .42, rubber, 14)
            beam('Road train hub', (side * 1.1, .42, z), (side * 1.12, .42, z), .2, sun, 10)
    done('road-train-tractor')

    box('Carriage floor', (0, .8, 0), (2.05, .22, 4.8), sea, .05)
    box('Carriage skirt', (0, .58, 0), (1.9, .3, 4.5), dark, .03)
    box('Carriage trim', (0, .93, 0), (2.09, .06, 4.84), sun, .01)
    for k in range(3):
        z = -1.55 + k * 1.55
        box('Carriage bench', (0, 1.22, z), (1.9, .12, .5), wood, .03)
        box('Carriage backrest', (0, 1.55, z - .24), (1.9, .55, .08), wood, .02)
        for side in (-1, 1):
            shirt = mat('Passenger shirt ' + str((k * 2 + side) % 5),
                        [(.92, .22, .12), (.12, .49, .70), (.97, .68, .12), (.54, .28, .65), (.20, .65, .43)]
                        [(k * 2 + side) % 5])
            box('Passenger', (side * .48, 1.6, z + .02), (.5, .6, .32), shirt, .08)
            ico('Passenger head', (side * .48, 2.08, z + .04), (.19, .22, .19),
                mat('Passenger skin ' + str(k % 3), [(.94, .69, .49), (.73, .45, .28), (.59, .35, .23)][k % 3]), 1)
    for x in (-.98, .98):
        for z in (-2.3, 0, 2.3):
            beam('Carriage canopy post', (x, .9, z), (x, 2.55, z), .045, cream, 6)
    for k in range(6):
        box('Carriage canopy stripe', (0, 2.62, -2.08 + k * .833), (2.3, .1, .833),
            sea if k % 2 else cream, .02)
    for z in (1.6, -1.6):
        for side in (-1, 1):
            beam('Carriage tyre', (side * .78, .36, z), (side * 1.02, .36, z), .36, rubber, 12)
            beam('Carriage hub', (side * 1.02, .36, z), (side * 1.04, .36, z), .17, sun, 8)
    beam('Drawbar', (0, .5, 2.4), (0, .5, 2.9), .05, dark, 6)
    done('road-train-carriage')

    # Harbour freight: a hood diesel with a cab at each end, and container flats.
    diesel = mat('Diesel livery', (.10, .30, .62), 1)
    stripe = mat('Diesel stripe', (.95, .78, .16))
    box('Diesel frame', (0, 1.05, 0), (2.6, .5, 12.0), machinery, .05)
    box('Diesel hood', (0, 2.2, 0), (2.2, 1.9, 8.2), diesel, .12)
    box('Diesel hood stripe', (0, 1.55, 0), (2.24, .22, 8.24), stripe, .02)
    for end in (-1, 1):
        box('Diesel cab', (0, 2.6, end * 5.0), (2.8, 2.7, 2.2), diesel, .12)
        box('Diesel cab roof', (0, 4.05, end * 5.0), (2.9, .2, 2.3), roof, .05)
        box('Diesel windscreen', (0, 3.15, end * 6.12), (2.3, .9, .06), glass, .05)
        box('Diesel nose stripe', (0, 1.6, end * 6.12), (2.82, .3, .06), stripe, .02)
        for side in (-1, 1):
            box('Diesel headlamp', (side * .8, 2.05, end * 6.14), (.32, .22, .06), cream, .03)
            box('Diesel cab window', (side * 1.41, 3.15, end * 5.0), (.05, .8, 1.2), glass, .04)
        buffers(end * 6.2, 1.1, end, .85)
        bogie(end * 3.8, 1.0)
    for k in range(6):
        box('Diesel louvre', (0, 3.18, -2.5 + k), (1.6, .06, .5), machinery, .01)
    done('diesel-locomotive')

    flat = mat('Wagon deck', (.30, .28, .27))
    reds = mat('Container rust red', (.66, .20, .12))
    blues = mat('Container blue', (.14, .36, .60))
    box('Wagon deck', (0, 1.25, 0), (2.5, .3, 12.9), flat, .04)
    box('Wagon frame', (0, .95, 0), (2.0, .3, 12.5), machinery, .03)
    for k, (z, m) in enumerate(((-3.15, reds), (3.15, blues))):
        box('Container', (0, 2.7, z), (2.44, 2.6, 6.05), m, .03)
        for j in range(7):
            box('Container rib', (1.23, 2.7, z - 2.7 + j * .9), (.04, 2.5, .12), m, .005)
            box('Container rib', (-1.23, 2.7, z - 2.7 + j * .9), (.04, 2.5, .12), m, .005)
        box('Container doors', (0, 2.7, z + 3.04 * (1 if k else -1)), (2.3, 2.4, .05), machinery, .01)
    for end in (-1, 1):
        buffers(end * 6.5, 1.1, end, .85)
        bogie(end * 4.6, 1.0)
    done('container-wagon')

    # The editable scene lays the stock out side by side.
    x = 0
    for name, objects in exported:
        for o in objects:
            o.hide_render = False
            o.location.x += x
        x += 4
    save('trains')
