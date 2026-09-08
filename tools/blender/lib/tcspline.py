"""
tcspline - the centreline of a track.

A track is defined by control points, through which a closed Catmull-Rom
spline is fitted. The spline is then resampled into a dense polyline with a
constant step length. That same polyline drives both the Blender mesh and the
game logic in Swift, so geometry and physics can never drift apart.

Blender coordinates: X, Y = ground, Z = height.
"""
import math


def catmull_rom(p0, p1, p2, p3, t, alpha=0.5):
    """Centripetal Catmull-Rom - produces neither loops nor sharp cusps."""
    def tj(ti, pa, pb):
        d = math.dist(pa, pb)
        return ti + (d ** alpha if d > 1e-9 else 1e-9)

    t0 = 0.0
    t1 = tj(t0, p0, p1)
    t2 = tj(t1, p1, p2)
    t3 = tj(t2, p2, p3)
    tt = t1 + (t2 - t1) * t

    def lerp(a, b, ta, tb):
        f = (tt - ta) / (tb - ta) if abs(tb - ta) > 1e-9 else 0.0
        return tuple(a[i] + (b[i] - a[i]) * f for i in range(len(a)))

    a1 = lerp(p0, p1, t0, t1)
    a2 = lerp(p1, p2, t1, t2)
    a3 = lerp(p2, p3, t2, t3)
    b1 = lerp(a1, a2, t0, t2)
    b2 = lerp(a2, a3, t1, t3)
    return lerp(b1, b2, t1, t2)


class Centerline:
    """A dense polyline down the middle of the track with tangents, width and surface."""

    def __init__(self, points, step=0.6, closed=True, subdiv=64):
        """
        points: [(x, y, z, halfwidth, surface_id), ...] - control points
        step:   desired sample spacing in metres
        """
        self.closed = closed
        self.step = step
        raw = self._densify(points, subdiv)
        self._resample(raw, step)
        self._finish()

    # ------------------------------------------------------------ internal
    @staticmethod
    def _densify(cps, subdiv):
        n = len(cps)
        out = []
        for i in range(n):
            p0 = cps[(i - 1) % n]
            p1 = cps[i]
            p2 = cps[(i + 1) % n]
            p3 = cps[(i + 2) % n]
            for k in range(subdiv):
                t = k / subdiv
                pos = catmull_rom(p0[:3], p1[:3], p2[:3], p3[:3], t)
                hw = p1[3] + (p2[3] - p1[3]) * (t * t * (3 - 2 * t))
                surf = p1[4] if t < 0.5 else p2[4]
                out.append((pos[0], pos[1], pos[2], hw, surf))
        return out

    def _resample(self, raw, step):
        # cumulative length
        n = len(raw)
        acc = [0.0]
        for i in range(1, n + 1):
            a = raw[i - 1]
            b = raw[i % n]
            acc.append(acc[-1] + math.dist(a[:3], b[:3]))
        total = acc[-1]
        count = max(16, int(round(total / step)))
        self.length = total
        self.step = total / count
        pts = []
        j = 0
        for i in range(count):
            s = i * self.step
            while j < n and acc[j + 1] < s:
                j += 1
            a = raw[j]
            b = raw[(j + 1) % n]
            seg = acc[j + 1] - acc[j]
            f = (s - acc[j]) / seg if seg > 1e-9 else 0.0
            # position and width interpolate; the surface is an id and must
            # not - halfway between sand (2) and wood (5) is ice (4), which
            # is how a beach ended up with a metre of snow on it
            pts.append(tuple(a[k] + (b[k] - a[k]) * f for k in range(4))
                       + (a[4] if f < 0.5 else b[4],))
        self.pts = pts

    def _finish(self, curv_span=2.8):
        n = len(self.pts)
        self.tangents = []
        self.normals = []
        self.curvature = []
        for i in range(n):
            a = self.pts[(i - 1) % n]
            b = self.pts[(i + 1) % n]
            dx, dy = b[0] - a[0], b[1] - a[1]
            ln = math.hypot(dx, dy) or 1.0
            tx, ty = dx / ln, dy / ln
            self.tangents.append((tx, ty))
            self.normals.append((-ty, tx))       # left of the direction of travel
        # Curvature over a baseline of a few metres. Across two neighbouring
        # samples (1.4 m) the turn angle is mostly resampling noise, and this
        # number decides how far the track narrows in hairpins and how hard
        # the AI brakes - both want the shape of the corner, not the jitter.
        w = max(1, int(round(curv_span / self.step)))
        for i in range(n):
            t0 = self.tangents[(i - w) % n]
            t1 = self.tangents[(i + w) % n]
            cross = t0[0] * t1[1] - t0[1] * t1[0]
            dot = max(-1.0, min(1.0, t0[0] * t1[0] + t0[1] * t1[1]))
            ang = math.atan2(cross, dot)
            self.curvature.append(ang / (2 * w * self.step))

    # ------------------------------------------------------------ queries
    def __len__(self):
        return len(self.pts)

    def pos(self, i):
        p = self.pts[i % len(self.pts)]
        return (p[0], p[1], p[2])

    def halfwidth(self, i):
        return self.pts[i % len(self.pts)][3]

    def surface(self, i):
        return int(self.pts[i % len(self.pts)][4])

    def tangent(self, i):
        return self.tangents[i % len(self.tangents)]

    def normal(self, i):
        return self.normals[i % len(self.normals)]

    def offset_point(self, i, lat, height=0.0):
        """A point offset lat metres to the left of the centre (positive = left)."""
        p = self.pos(i)
        nx, ny = self.normal(i)
        return (p[0] + nx * lat, p[1] + ny * lat, p[2] + height)

    def index_at_s(self, s):
        return int(round((s % self.length) / self.step)) % len(self.pts)

    def smooth_elevation(self, passes=2, radius=3):
        """Smooths the elevation profile so no bumps appear."""
        n = len(self.pts)
        for _ in range(passes):
            new = []
            for i in range(n):
                acc = 0.0
                cnt = 0
                for k in range(-radius, radius + 1):
                    acc += self.pts[(i + k) % n][2]
                    cnt += 1
                p = self.pts[i]
                new.append((p[0], p[1], acc / cnt, p[3], p[4]))
            self.pts = new

    def limit_grade(self, max_grade=0.16, verbose=True):
        """
        Caps the gradient of the road. The elevation profile comes from the
        landscape, and a landscape does not care whether a car can climb it -
        so wherever the ground is steeper than `max_grade` (0.16 = 16 %) the
        road is levelled and the difference becomes a cut or an embankment in
        the terrain beside it. Exactly what a road builder does.

        Both Lipschitz envelopes are computed - the highest profile that fits
        under the terrain and the lowest one that fits over it - and the road
        runs down the middle of the two. Each envelope already satisfies the
        gradient limit and the limit is a convex constraint, so their average
        does too: one pass, no iteration, and the summit is cut by as much as
        the dip is filled.
        """
        n = len(self.pts)
        z = [p[2] for p in self.pts]
        lim = max_grade * self.step
        before = self._max_grade(z)

        def envelope(sign):
            # sign +1: the highest lim-Lipschitz profile that stays below z
            # sign -1: the lowest one that stays above it
            e = [sign * v for v in z]
            for _ in range(2):
                for i in range(n):
                    p = e[(i - 1) % n] + lim
                    if p < e[i]:
                        e[i] = p
                for i in range(n - 1, -1, -1):
                    p = e[(i + 1) % n] + lim
                    if p < e[i]:
                        e[i] = p
            return [sign * v for v in e]

        lo = envelope(1)
        hi = envelope(-1)
        z = [(lo[i] + hi[i]) * 0.5 for i in range(n)]
        self.pts = [(p[0], p[1], z[i], p[3], p[4])
                    for i, p in enumerate(self.pts)]
        after = self._max_grade(z)
        if verbose and before > max_grade + 1e-4:
            print("  grade limited: %.1f%% -> %.1f%% (cap %.0f%%)"
                  % (before * 100, after * 100, max_grade * 100))
        return after

    def _max_grade(self, z=None):
        z = z or [p[2] for p in self.pts]
        n = len(z)
        return max(abs(z[(i + 1) % n] - z[i]) / self.step for i in range(n))

    def elevation_stats(self):
        """(lowest, highest, steepest grade, total climb) - printed on export."""
        z = [p[2] for p in self.pts]
        n = len(z)
        climb = sum(max(0.0, z[(i + 1) % n] - z[i]) for i in range(n))
        return min(z), max(z), self._max_grade(z), climb

    def smooth_width(self, passes=1, radius=4):
        n = len(self.pts)
        for _ in range(passes):
            new = []
            for i in range(n):
                acc = 0.0
                for k in range(-radius, radius + 1):
                    acc += self.pts[(i + k) % n][3]
                p = self.pts[i]
                new.append((p[0], p[1], p[2], acc / (2 * radius + 1), p[4]))
            self.pts = new

    def clamp_width_to_curvature(self, margin=0.85, blend=6, verbose=True):
        """
        A strip of width w cannot follow a corner whose radius is smaller than
        w - the inner edge folds over itself and an overlapping triangle sticks
        out at the apex (in the engine the terrain shows through it). In
        hairpins we therefore narrow the track so the half width stays below
        the corner radius. Graphics and physics read the same value, so they
        cannot diverge.
        """
        n = len(self.pts)
        lim = []
        for i in range(n):
            k = abs(self.curvature[i])
            lim.append(1e9 if k < 1e-9 else margin / k)
        # spread the limit into the neighbourhood so the narrowing is not abrupt
        win = [min(lim[(i + d) % n] for d in range(-blend, blend + 1))
               for i in range(n)]
        hit = sum(1 for i, p in enumerate(self.pts) if win[i] < p[3] - 1e-6)
        if not hit:
            return 0
        self.pts = [(p[0], p[1], p[2], min(p[3], win[i]), p[4])
                    for i, p in enumerate(self.pts)]
        self.smooth_width(passes=3, radius=blend + 4)
        # clamp hard again after smoothing so the limit really holds
        self.pts = [(p[0], p[1], p[2], min(p[3], lim[i]), p[4])
                    for i, p in enumerate(self.pts)]
        if verbose:
            print("  narrowed in tight corners: %d points, narrowest %.2f m"
                  % (hit, 2 * min(p[3] for p in self.pts)))
        return hit

    def racing_line(self, aggression=0.72, passes=140, max_lat=None):
        """
        Computes the racing line as a lateral offset from the centre.
        It iteratively shortens the path (the classic 'shortest path'
        relaxation) while keeping it inside the track. Used by the AI.
        """
        n = len(self.pts)
        lat = [0.0] * n
        for _ in range(passes):
            new = list(lat)
            for i in range(n):
                a = self.offset_point((i - 1) % n, lat[(i - 1) % n])
                c = self.offset_point((i + 1) % n, lat[(i + 1) % n])
                mid = ((a[0] + c[0]) * 0.5, (a[1] + c[1]) * 0.5)
                p = self.pos(i)
                nx, ny = self.normal(i)
                d = (mid[0] - p[0]) * nx + (mid[1] - p[1]) * ny
                lim = (max_lat if max_lat is not None
                       else self.halfwidth(i) * aggression)
                v = lat[i] + (d - lat[i]) * 0.35
                new[i] = max(-lim, min(lim, v))
            lat = new
        # smoothing
        for _ in range(6):
            lat = [(lat[(i - 1) % n] + 2 * lat[i] + lat[(i + 1) % n]) / 4.0
                   for i in range(n)]
        return lat


# ------------------------------------------------------------------ routes

def _arc_step(p, th, turn, r):
    """End point, heading and centre of an arc. `turn` in radians, + left."""
    s = 1.0 if turn >= 0 else -1.0
    cx = p[0] - math.sin(th) * r * s
    cy = p[1] + math.cos(th) * r * s
    vx, vy = p[0] - cx, p[1] - cy
    ca, sa = math.cos(turn), math.sin(turn)
    return ((cx + vx * ca - vy * sa, cy + vx * sa + vy * ca),
            th + turn, (cx, cy))


def route_control(segs, start=(0.0, 0.0), heading=0.0, gap=30.0, arc_gap=15.0):
    """
    Turtle authoring of a plan view - control points from straights and arcs.

        ("s", length, half width, surface)              a straight
        ("a", turn, radius, half width, surface)        an arc, + turns left

    A circuit written this way states what it is - a 290 m straight, a 180
    degree sweeper at 82 m radius, a 140 degree hairpin at 22 - rather than a
    list of coordinates, which is the only way two tracks can be given
    genuinely different characters and be *seen* to have them.

    The turns fix the shape, so they have to add up to a full turn (+-360
    degrees) or nothing can close. Exactly two straights then pass `None`
    for their length and those two are solved for, which is what makes the
    lap join up: their headings must differ, or the pair of equations is
    singular. The output is the same (x, y, half width, surface) control
    points a track can also be written out by hand.
    """
    segs = [(s[0], math.radians(s[1]) if s[0] == "a" else s[1]) + tuple(s[2:])
            for s in segs]
    th0 = math.radians(heading)

    # what the fixed segments contribute, and in which direction each of the
    # two solved straights runs
    th = th0
    fixed = [0.0, 0.0]
    free = []
    for k, sg in enumerate(segs):
        if sg[0] == "s":
            d = (math.cos(th), math.sin(th))
            if sg[1] is None:
                free.append((k, d))
            else:
                fixed[0] += d[0] * sg[1]
                fixed[1] += d[1] * sg[1]
        else:
            (ex, ey), th, _ = _arc_step((0.0, 0.0), th, sg[1], sg[2])
            fixed[0] += ex
            fixed[1] += ey
    turn = th - th0
    if abs(abs(turn) - 2 * math.pi) > 1e-6:
        raise ValueError("route turns by %.1f deg, needs +-360"
                         % math.degrees(turn))
    if len(free) != 2:
        raise ValueError("route needs exactly 2 straights of length None")
    (k1, d1), (k2, d2) = free
    det = d1[0] * d2[1] - d2[0] * d1[1]
    if abs(det) < 1e-6:
        raise ValueError("the two solved straights run in the same direction")
    lens = {k1: (-fixed[0] * d2[1] + fixed[1] * d2[0]) / det,
            k2: (fixed[0] * d1[1] - fixed[1] * d1[0]) / det}
    for k, L in lens.items():
        if L < gap:
            raise ValueError("solved straight %d comes out at %.1f m" % (k, L))

    # walk it again and drop control points along the way; every segment
    # emits its own start, so the closing joint needs no special case
    out = []
    p, th = start, th0
    for k, sg in enumerate(segs):
        hw, surf = sg[-2], sg[-1]
        if sg[0] == "s":
            L = lens.get(k, sg[1])
            n = max(1, int(round(L / gap)))
            dx, dy = math.cos(th) * L / n, math.sin(th) * L / n
            for i in range(n):
                out.append((p[0] + dx * i, p[1] + dy * i, hw, surf))
            p = (p[0] + dx * n, p[1] + dy * n)
        else:
            turn, r = sg[1], sg[2]
            n = max(3, int(math.ceil(abs(turn) * r / arc_gap)))
            for i in range(n):
                q, _, _ = _arc_step(p, th, turn * i / n, r)
                out.append((q[0], q[1], hw, surf))
            p, th, _ = _arc_step(p, th, turn, r)
    return [(round(x, 3), round(y, 3), hw, surf) for x, y, hw, surf in out]
