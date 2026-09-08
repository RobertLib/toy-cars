"""
gen_cars.py - generates the toy car models for ToyCars.

Usage:
  Blender -b --factory-startup --python tools/blender/gen_cars.py -- [--preview] [--only ID]

Model conventions:
  the car faces -Y, Z is up, the origin sits on the ground mid-wheelbase.
  The wheels are separate objects WheelFL/FR/RL/RR whose origin is the axle.
"""
import bpy
import sys
import os
import math
import json

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
from lib import tcmesh as T          # noqa: E402
from lib import tcrender as R        # noqa: E402

OUT = os.path.abspath(os.path.join(_HERE, "..", "..", "ToyCars", "Assets3D"))
PREVIEW = os.path.abspath(os.path.join(_HERE, "..", "preview"))

TAU = math.pi * 2

# how far the outer face of a wheel must stand proud of the body so the
# surfaces do not overlap (metres)
WHEEL_GAP = 0.022

# extra station spacing credited to a panel per radian the body outline turns
# (metres/radian) - see side_stations
PANEL_BEND = 0.55


# ------------------------------------------------------------------ profile

def _mono_interp(keys, t):
    """Smooth interpolation over a list of (t, value...) - Catmull-Rom per component."""
    ts = [k[0] for k in keys]
    if t <= ts[0]:
        return keys[0][1:]
    if t >= ts[-1]:
        return keys[-1][1:]
    i = 0
    while i < len(ts) - 2 and t > ts[i + 1]:
        i += 1
    t0, t1 = ts[i], ts[i + 1]
    u = (t - t0) / max(1e-6, (t1 - t0))
    u = u * u * (3 - 2 * u)          # smoothstep - nicely rounded transitions
    a, b = keys[i][1:], keys[i + 1][1:]
    return tuple(a[j] + (b[j] - a[j]) * u for j in range(len(a)))


def hull_ring(y, hw, z_bot, z_sh, z_top, px=3.6, p_top=2.6,
              n_bottom=4, n_wall=3, n_top=13, tuck=0.0,
              flare=0.0, flare_z0=0.0, flare_z1=1.0):
    """
    Body cross-section in the X-Z plane: a flat floor, (nearly) vertical
    sides and a rounded shoulder easing into the roof. The vertical sides are
    what make the windows and side surfaces readable - unlike a pure
    superellipse.
    """
    def widen(x, z):
        """The wheel arch bulge - built into the body itself, no seams."""
        if flare <= 0.0:
            return x
        # a bell centred at axle height -> the bulge sits where the wheel is
        u = (z - flare_z0) / flare_z1
        w = max(0.0, 1.0 - u * u) ** 1.35
        if x == 0.0:
            return x
        return math.copysign(abs(x) + flare * w, x)

    pts = []
    bw = hw * (1.0 - tuck)
    for i in range(n_bottom):                       # floor, left to right
        u = i / n_bottom
        x = -bw + 2 * bw * u
        pts.append((widen(x, z_bot), y, z_bot))
    for i in range(n_wall):                         # right side going up
        u = i / n_wall
        x = bw + (hw - bw) * (u ** 0.6)
        z = z_bot + (z_sh - z_bot) * u
        pts.append((widen(x, z), y, z))
    for i in range(n_top):                          # the roof arc
        a = math.pi * i / n_top
        c, sn = math.cos(a), math.sin(a)
        x = math.copysign(abs(c) ** (2.0 / px), c) * hw
        z = z_sh + (abs(sn) ** (2.0 / p_top)) * (z_top - z_sh)
        pts.append((widen(x, z), y, z))
    for i in range(n_wall):                         # left side going down
        u = i / n_wall
        x = -(bw + (hw - bw) * ((1 - u) ** 0.6))
        z = z_sh - (z_sh - z_bot) * u
        pts.append((widen(x, z), y, z))
    return pts


def body_half_width(spec, t, z):
    """Body half width at longitudinal position t and height z (arch bulge
    included). Returns None if z falls outside the profile."""
    keys = spec["body"]
    hw, zb, zt = _mono_interp(keys, t)
    if z < zb or z > zt:
        return None
    sh = spec.get("shoulder", 0.55)
    tuck = spec.get("tuck", 0.20)
    px = spec.get("px", 3.6)
    p_top = spec.get("p_top", 2.6)
    zs = zb + (zt - zb) * sh
    bw = hw * (1.0 - tuck)
    if z <= zs:
        u = 0.0 if zs <= zb else (z - zb) / (zs - zb)
        x = bw + (hw - bw) * (u ** 0.6)
    else:
        v = (z - zs) / max(1e-6, (zt - zs))
        sn = min(1.0, v ** (p_top / 2.0))
        cs = math.sqrt(max(0.0, 1.0 - sn * sn))
        x = (cs ** (2.0 / px)) * hw
    # the wheel arch bulge
    haunch = spec.get("haunch", 0.0)
    if haunch > 0.0:
        wr = spec["wheel_r"]
        arch = spec.get("arch_len", wr * 1.9)
        wy = spec["wheelbase"] * 0.5
        L = spec["length"]
        y = -L * 0.5 + L * t
        fl = 0.0
        for cy in (-wy, wy):
            d = abs(y - cy) / arch
            if d < 1.0:
                fl = max(fl, haunch * (1.0 - d * d) ** 1.3)
        if fl > 0.0:
            u2 = (z - spec.get("haunch_z0", wr)) / spec.get("haunch_z1",
                                                            wr * 1.30)
            x += fl * max(0.0, 1.0 - u2 * u2) ** 1.35
    return x


def body_top(spec, t):
    return _mono_interp(spec["body"], t)[2]


def clamp_into_body(spec, t, z, margin=0.0):
    """Clamps an accessory's height into the body profile at position t."""
    hw, zb, zt = _mono_interp(spec["body"], t)
    if zt - zb <= 2 * margin:
        return (zb + zt) * 0.5
    return min(max(z, zb + margin), zt - margin)


def body_surface_z(spec, t, x):
    """
    Height of the body's upper surface at position t and lateral offset x.
    Above the shoulder (the widest point) the half width only decreases with
    height, so bisecting the shoulder-to-roof interval is enough. Used to seat
    accessories that are meant to stand on the body - otherwise they float.
    """
    hw, zb, zt = _mono_interp(spec["body"], t)
    zs = zb + (zt - zb) * spec.get("shoulder", 0.55)
    if (body_half_width(spec, t, zs) or 0.0) < abs(x):
        return zs                     # the body does not reach that far out
    lo, hi = zs, zt
    for _ in range(28):
        mid = (lo + hi) * 0.5
        w = body_half_width(spec, t, mid)
        if w is not None and w >= abs(x):
            lo = mid
        else:
            hi = mid
    return lo


def sweep_path(name, path, w, h, mat, closed=False, round_seg=6, smooth=True):
    """
    Sweeps a rounded rectangular cross-section (width w across the path,
    height h) along a path of points [(x, y, z), ...]. Used for bumpers,
    trims and track kerbs.
    """
    n = len(path)
    sections = []
    for i in range(n):
        p = path[i]
        if i == 0:
            d = (path[1][0] - p[0], path[1][1] - p[1])
        elif i == n - 1:
            d = (p[0] - path[-2][0], p[1] - path[-2][1])
        else:
            d = (path[i + 1][0] - path[i - 1][0], path[i + 1][1] - path[i - 1][1])
        ln = math.hypot(d[0], d[1]) or 1.0
        nx, ny = -d[1] / ln, d[0] / ln          # normal in the XY plane
        ring = []
        for k in range(round_seg * 4):
            a = TAU * k / (round_seg * 4)
            cu = math.copysign(abs(math.cos(a)) ** 0.7, math.cos(a))
            cv = math.copysign(abs(math.sin(a)) ** 0.7, math.sin(a))
            ring.append((p[0] + nx * cu * w * 0.5,
                         p[1] + ny * cu * w * 0.5,
                         p[2] + cv * h * 0.5))
        sections.append(ring)
    return T.loft(name, sections, mat, cap_start=not closed, cap_end=not closed,
                  smooth=smooth)


def build_wrap_bumper(spec, mat, front=True):
    """A bumper wrapping the front/rear exactly along the body outline."""
    key = "front_bumper" if front else "rear_bumper"
    b = spec.get(key)
    if not b:
        return None
    L = spec["length"]
    z = b["z"]
    reach = b.get("reach", 0.22)          # how far back it wraps
    out = b.get("out", 0.035)
    steps = 16
    left, right = [], []
    for i in range(steps + 1):
        u = i / steps
        t = u * reach if front else 1.0 - u * reach
        hw = body_half_width(spec, t, z)
        if hw is None:
            hw = _mono_interp(spec["body"], t)[0] * 0.6
        y = -L * 0.5 + L * t
        right.append((hw + out, y, z))
        left.append((-(hw + out), y, z))
    path = list(reversed(left)) + right
    ob = sweep_path("Bumper", path, b.get("d", 0.14), b.get("h", 0.15), mat,
                    round_seg=4)
    return ob


def body_front_t(spec, x, z, front=True):
    """
    The longitudinal position t where the body has half width |x| at height z.
    Bisection rather than a stepped scan - coarse sampling turned the
    windscreen into a staircase.
    """
    narrow, wide_end = (0.0, 0.5) if front else (1.0, 0.5)
    target = abs(x)

    def wide(t):
        hw = body_half_width(spec, t, z)
        return hw is not None and hw >= target

    if wide(narrow):
        return narrow
    if not wide(wide_end):
        return wide_end
    a, b = narrow, wide_end
    for _ in range(28):
        m = (a + b) * 0.5
        if wide(m):
            b = m
        else:
            a = m
    return b


def lift_off_surface(verts, faces, eps):
    """
    Offsets every vertex of a panel along the surface normal by eps
    (a scalar, or a list of per-vertex values).
    Offsetting along X alone is not enough - where the side curls into the
    front mask the normal is almost longitudinal and the panel would stay
    buried in the body.
    """
    if not isinstance(eps, (list, tuple)):
        eps = [eps] * len(verts)
    acc = [[0.0, 0.0, 0.0] for _ in verts]
    for f in faces:
        p0, p1, p2 = verts[f[0]], verts[f[1]], verts[f[2]]
        ux, uy, uz = (p1[k] - p0[k] for k in range(3))
        vx, vy, vz = (p2[k] - p0[k] for k in range(3))
        n = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
        for idx in f:
            for k in range(3):
                acc[idx][k] += n[k]
    out = []
    for p, n, e in zip(verts, acc, eps):
        ln = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
        if ln < 1e-12:
            out.append(tuple(p))
        else:
            out.append(tuple(p[k] + n[k] / ln * e for k in range(3)))
    return out


def side_depth(spec, p):
    """How deep a point lies below the body side (positive = buried)."""
    L = spec["length"]
    t = (p[1] + L * 0.5) / L
    if not 0.0 <= t <= 1.0:
        return 0.0
    hw = body_half_width(spec, t, p[2])
    if hw is None:
        return 0.0
    return hw - abs(p[0])


def end_depth(spec, p, front):
    """How deep a point lies below the front (or rear) mask of the body."""
    L = spec["length"]
    x, y, z = p
    ts = body_front_t(spec, x, z, front)
    ys = -L * 0.5 + L * ts
    return (y - ys) if front else (ys - y)


def press_out_of_body(spec, verts, faces, eps, depth, rounds=4):
    """
    Pushes a panel out of the body so its whole surface, not merely its
    vertices, sits above it. A flat face between two sections follows a chord
    beneath the convex surface, so near the nose or under the roof the panel
    digs in and the window or stripe visually tears. We measure the sag inside
    each face and grow the offset there.

    The offset is per vertex, and spread over the neighbouring vertices before
    it is applied. One figure for the whole panel used to be raised to the
    worst sag found anywhere on it: the tight wrap around the nose needs some
    45 mm, and that pushed the flank - which needs nothing - out past the
    wheels, leaving the dark stripe standing proud of the body all round the
    car. Smoothing is what keeps the panel from rippling, so the peak stays
    where it is needed and only its surroundings are eased up to meet it.
    """
    n = len(verts)
    nbr = [set() for _ in range(n)]
    for f in faces:
        for a in f:
            nbr[a].update(k for k in f if k != a)

    def smooth(vals):
        return [(vals[i] + sum(vals[j] for j in nbr[i])) / (1.0 + len(nbr[i]))
                for i in range(n)]

    need = [eps] * n
    out = lift_off_surface(verts, faces, need)
    for _ in range(rounds):
        add = [0.0] * n
        worst = 0.0
        for f in faces:
            P = [out[k] for k in f]
            sag = 0.0
            for a, b in ((0.5, 0.5), (0.25, 0.5), (0.75, 0.5),
                         (0.5, 0.25), (0.5, 0.75)):
                q = []
                for k in range(3):
                    e0 = P[0][k] + (P[1][k] - P[0][k]) * a
                    e1 = P[3][k] + (P[2][k] - P[3][k]) * a
                    q.append(e0 + (e1 - e0) * b)
                sag = max(sag, depth(q))
            if sag > 0.0:
                worst = max(worst, sag)
                for k in f:
                    add[k] = max(add[k], sag)
        if worst <= 5e-4:
            break
        # a ramp out to the sagging corner: blurring alone would fall short of
        # what that corner asked for, so each vertex keeps the larger of the two
        eased = smooth(smooth(add))
        need = [q + max(a, e) for q, a, e in zip(need, add, eased)]
        out = lift_off_surface(verts, faces, need)
    return out


def side_top_z(spec, t, frac=0.80):
    """
    The highest point at which the body side is still a "side". Above it the
    surface curls into the roof and the half width falls towards zero - a
    panel stretched that far would tear across the roof.
    """
    hw, zb, zt = _mono_interp(spec["body"], t)
    zs = zb + (zt - zb) * spec.get("shoulder", 0.55)
    target = (body_half_width(spec, t, zs) or hw) * frac
    lo, hi = zs, zt
    for _ in range(24):
        m = (lo + hi) * 0.5
        w = body_half_width(spec, t, m)
        if w is not None and w >= target:
            lo = m
        else:
            hi = m
    return lo


def side_stations(spec, t0, t1, zm, n):
    """
    Distributes n+1 longitudinal sections by outline length rather than evenly
    in t. The body narrows quickly near the nose, and even steps would "cut
    the corner" between sections - burying the panel in the body.
    """
    L = spec["length"]
    fine = 120
    ts, xs, ys = [], [], []
    for i in range(fine + 1):
        t = t0 + (t1 - t0) * i / fine
        z = min(zm, side_top_z(spec, t))
        hw = body_half_width(spec, t, z)
        if hw is None:
            hw = _mono_interp(spec["body"], t)[0]
        ts.append(t)
        xs.append(hw)
        ys.append(-L * 0.5 + L * t)
    # Spacing follows length *and* turning. A panel is a chain of flat quads,
    # and it is the turning, not the length, that makes them cut the corner:
    # the wrap around the nose is short but swings through most of a right
    # angle, so on arc length alone it drew two stations there and sagged
    # 45 mm into the body.
    cum = [0.0]
    for i in range(1, fine + 1):
        step = math.hypot(xs[i] - xs[i - 1], ys[i] - ys[i - 1])
        turn = 0.0
        if i < fine:
            a0 = math.atan2(ys[i] - ys[i - 1], xs[i] - xs[i - 1])
            a1 = math.atan2(ys[i + 1] - ys[i], xs[i + 1] - xs[i])
            turn = abs((a1 - a0 + math.pi) % TAU - math.pi)
        cum.append(cum[-1] + step + turn * PANEL_BEND)
    total = cum[-1]
    if total < 1e-6:
        return [t0 + (t1 - t0) * i / n for i in range(n + 1)]
    out, k = [], 0
    for i in range(n + 1):
        target = total * i / n
        while k < fine and cum[k + 1] < target:
            k += 1
        span = cum[k + 1] - cum[k] if k < fine else 0.0
        f = (target - cum[k]) / span if span > 1e-9 else 0.0
        out.append(ts[k] + (ts[min(k + 1, fine)] - ts[k]) * f)
    return out


def side_panel(spec, t0, t1, z0, z1, mat, name="Panel", eps=0.006,
               nt=18, nz=5, both=True, taper=0.0):
    """A thin panel following the body side (windows, stripes, numbers)."""
    L = spec["length"]
    obs = []
    stations = side_stations(spec, t0, t1, (z0 + z1) * 0.5, nt)
    for sgn in ((-1, 1) if both else (1,)):
        verts, faces = [], []
        for i, t in enumerate(stations):
            u = i / nt
            shrink = taper * math.sin(u * math.pi)
            zmax = side_top_z(spec, t)
            zbot = _mono_interp(spec["body"], t)[1]
            for j in range(nz + 1):
                v = j / nz
                z = z0 + (z1 - z0) * v + shrink
                z = min(max(z, zbot + 0.005), zmax)
                hw = body_half_width(spec, t, z)
                if hw is None:
                    hw = _mono_interp(spec["body"], t)[0]
                verts.append((sgn * hw, -L * 0.5 + L * t, z))
        for i in range(nt):
            for j in range(nz):
                a = i * (nz + 1) + j
                b = (i + 1) * (nz + 1) + j
                # winding chosen so the normal points out of the side (+X right)
                quad = (a, b, b + 1, a + 1)
                faces.append(quad if sgn > 0 else tuple(reversed(quad)))
        verts = press_out_of_body(spec, verts, faces, eps,
                                  lambda q: side_depth(spec, q))
        obs.append(T.mesh_from_pydata(name, verts, faces, mat, smooth=True))
    return obs


def front_panel(spec, half_w, z0, z1, mat, name="Windshield", eps=0.008,
                nx=16, nz=6, front=True):
    """A panel following the front (or rear) mask - a van's windscreen, etc."""
    L = spec["length"]
    verts, faces = [], []
    for i in range(nx + 1):
        u = i / nx
        x = -half_w + 2 * half_w * u
        for j in range(nz + 1):
            v = j / nz
            z = z0 + (z1 - z0) * v
            t = body_front_t(spec, x, z, front)
            verts.append((x, -L * 0.5 + L * t, z))
    for i in range(nx):
        for j in range(nz):
            a = i * (nz + 1) + j
            b = (i + 1) * (nz + 1) + j
            # winding chosen so the normal points forwards (-Y),
            # or backwards for the rear panel
            quad = (a, b, b + 1, a + 1)
            faces.append(quad if front else tuple(reversed(quad)))
    verts = press_out_of_body(spec, verts, faces, eps,
                              lambda q: end_depth(spec, q, front))
    return T.mesh_from_pydata(name, verts, faces, mat, smooth=True)


def add_panels(parts, spec, mats):
    """Windows and stripes declared in the car specification."""
    for w in spec.get("windows", []):
        mat = mats[w.get("mat", "glass")]
        frame = w.get("mat") == "accent"
        eps = w.get("eps", 0.004 if frame else 0.010)
        if w["kind"] == "side":
            parts.extend(side_panel(spec, w["t0"], w["t1"], w["z0"], w["z1"],
                                    mat, "SideWindow", eps=eps,
                                    taper=w.get("taper", 0.0)))
        elif w["kind"] == "front":
            parts.append(front_panel(spec, w["w"], w["z0"], w["z1"], mat,
                                     "Windshield", eps=eps + 0.004, front=True))
        elif w["kind"] == "rear":
            parts.append(front_panel(spec, w["w"], w["z0"], w["z1"], mat,
                                     "RearWindow", eps=eps + 0.004,
                                     front=False))
    for st in spec.get("stripes", []):
        mat = T.material("Stripe_%s_%d" % (spec["id"], st.get("i", 0)),
                         st["color"], roughness=0.3, metallic=0.05)
        parts.extend(side_panel(spec, st["t0"], st["t1"], st["z0"], st["z1"],
                                mat, "Stripe", eps=st.get("eps", 0.005)))


def build_body(name, spec, mat):
    """Body lofted from the profile. spec['body'] = [(t, halfWidth, zBot, zTop), ...]"""
    L = spec["length"]
    keys = spec["body"]
    steps = spec.get("body_steps", 28)
    sh = spec.get("shoulder", 0.55)
    haunch = spec.get("haunch", 0.0)
    wr = spec["wheel_r"]
    arch = spec.get("arch_len", wr * 1.9)
    wy = spec["wheelbase"] * 0.5
    fz0 = spec.get("haunch_z0", wr)              # bell centre = the axle
    fz1 = spec.get("haunch_z1", wr * 1.30)       # bell reach
    sections = []
    for i in range(steps + 1):
        t = i / steps
        hw, zb, zt = _mono_interp(keys, t)
        # y: t=0 front (-L/2), t=1 rear (+L/2)
        y = -L * 0.5 + L * t
        zs = zb + (zt - zb) * sh
        fl = 0.0
        if haunch > 0.0:
            for cy in (-wy, wy):
                d = abs(y - cy) / arch
                if d < 1.0:
                    fl = max(fl, haunch * (1.0 - d * d) ** 1.3)
        sections.append(hull_ring(y, max(hw, 0.012), zb, zs, zt,
                                  flare=fl, flare_z0=fz0, flare_z1=fz1,
                                  px=spec.get("px", 3.6),
                                  p_top=spec.get("p_top", 2.6),
                                  n_bottom=spec.get("n_bottom", 4),
                                  n_wall=spec.get("n_wall", 3),
                                  n_top=spec.get("n_top", 13),
                                  tuck=spec.get("tuck", 0.20)))
    ob = T.loft(name, sections, mat, cap_start=True, cap_end=True, smooth=True)
    return ob


def build_cabin(name, spec, mat_glass, mat_body):
    """
    The cabin as one continuous loft (the greenhouse). Materials are assigned
    from each face's position in the loft: vertical sides = side windows, the
    middle of the arc = roof in the body colour, the raked ends of the arc =
    windscreen and rear window.
    """
    c = spec.get("cabin")
    if not c:
        return []
    L = spec["length"]
    keys = c["profile"]
    steps = c.get("steps", 16)
    t0, t1 = c["t0"], c["t1"]
    sh = c.get("shoulder", 0.62)
    sections = []
    for i in range(steps + 1):
        u = i / steps
        t = t0 + (t1 - t0) * u
        hw, zb, zt = _mono_interp(keys, u)
        y = -L * 0.5 + L * t
        zs = zb + (zt - zb) * sh
        sections.append(hull_ring(y, max(hw, 0.01), zb, zs, zt,
                                  px=c.get("px", 3.4),
                                  p_top=c.get("p_top", 2.6),
                                  n_bottom=c.get("n_bottom", 4),
                                  n_wall=c.get("n_wall", 3),
                                  n_top=c.get("n_top", 13),
                                  tuck=0.04))
    nb = c.get("n_bottom", 4)
    nw = c.get("n_wall", 3)
    nt = c.get("n_top", 13)
    rake_f = int(round(steps * c.get("rake_front", 0.24)))
    rake_r = int(round(steps * (1.0 - c.get("rake_rear", 0.20))))
    has_roof = c.get("roof", True)

    def face_mat(kind, s_idx, i, n):
        if not has_roof:
            return 1
        if kind in ('cap0', 'cap1'):
            return 1
        if i < nb:                       # cabin floor - hidden inside the body
            return 0
        if i < nb + nw:                  # right side = side window
            return 1
        if i < nb + nw + nt:             # roof arc; the raked ends are glass
            return 1 if (s_idx < rake_f or s_idx >= rake_r) else 0
        return 1                         # left side = side window

    ob = T.loft(name, sections, None, cap_start=True, cap_end=True,
                smooth=True, mats=[mat_body, mat_glass], face_mat=face_mat)
    return [ob]


def build_fender(name, cx, cy, wheel_r, wheel_w, mat, spec):
    """A rounded arch over the wheel - hides the wheel/body intersection, toy-like."""
    f = spec.get("fender", {})
    r0 = wheel_r * f.get("gap", 1.13)
    thick = wheel_r * f.get("thick", 0.16)
    a0 = math.radians(f.get("a0", -26))
    a1 = math.radians(f.get("a1", 206))
    steps = 14
    sgn = 1.0 if cx >= 0 else -1.0
    x_out = cx + sgn * (wheel_w * 0.5 + wheel_r * f.get("flare", 0.10))
    x_in = cx - sgn * (wheel_w * 0.5 + wheel_r * 0.06)
    sections = []
    for i in range(steps + 1):
        a = a0 + (a1 - a0) * i / steps
        ca, sa = math.cos(a), math.sin(a)
        y0, z0 = cy + ca * r0, wheel_r + sa * r0
        y1, z1 = cy + ca * (r0 + thick), wheel_r + sa * (r0 + thick)
        ring = [(x_in, y0, z0), (x_out, y0, z0),
                (x_out, y1, z1), (x_in, y1, z1)]
        if sgn < 0:
            ring = list(reversed(ring))
        sections.append(ring)
    ob = T.loft(name, sections, mat, cap_start=True, cap_end=True, smooth=True)
    T.bevel(ob, thick * 0.30, 2, angle_limit=math.radians(50))
    T.apply_modifiers(ob)
    return ob


def build_arch_liner(name, cx, cy, wheel_r, wheel_w, mat, spec):
    """A dark wheel arch liner - visually closes the opening around the wheel."""
    f = spec.get("fender", {})
    r0 = wheel_r * f.get("gap", 1.13)
    a0 = math.radians(f.get("a0", -26))
    a1 = math.radians(f.get("a1", 206))
    steps = 12
    sgn = 1.0 if cx >= 0 else -1.0
    x_in = cx - sgn * (wheel_w * 0.5 + wheel_r * 0.05)
    x_mid = cx - sgn * (wheel_w * 0.5 + wheel_r * 0.30)
    sections = []
    for i in range(steps + 1):
        a = a0 + (a1 - a0) * i / steps
        ca, sa = math.cos(a), math.sin(a)
        y0, z0 = cy + ca * r0, wheel_r + sa * r0
        yi, zi = cy + ca * r0 * 0.72, wheel_r + sa * r0 * 0.72
        ring = [(x_in, y0, z0), (x_mid, yi, zi),
                (x_mid, yi, zi + 0.001), (x_in, y0, z0 + 0.001)]
        if sgn < 0:
            ring = list(reversed(ring))
        sections.append(ring)
    return T.loft(name, sections, mat, cap_start=False, cap_end=False,
                  smooth=False)


# ------------------------------------------------------------------ wheels

def build_wheel(name, r, width, spec, mat_tire, mat_rim, mat_hub):
    seg = 20
    parts = []
    tire = T.cylinder(name + "_T", r, width, seg, (0, 0, 0), 'X', mat_tire)
    T.bevel(tire, width=min(r * 0.22, width * 0.3), segments=3)
    T.apply_modifiers(tire)
    parts.append(tire)
    # rims on both sides
    rr = r * spec.get("rim_frac", 0.58)
    for sx in (-1, 1):
        rim = T.cylinder(name + "_R%d" % sx, rr, width * 0.24, seg,
                         (sx * width * 0.40, 0, 0), 'X', mat_rim)
        T.bevel(rim, width=rr * 0.12, segments=2)
        T.apply_modifiers(rim)
        parts.append(rim)
        hub = T.cylinder(name + "_H%d" % sx, rr * 0.34, width * 0.30, 10,
                         (sx * width * 0.44, 0, 0), 'X', mat_hub)
        parts.append(hub)
    # the tyre tread - small lugs for the toy look
    if spec.get("tread", True):
        lugs = spec.get("tread_count", 14)
        for i in range(lugs):
            a = TAU * i / lugs
            # build the lug at the origin, rotate it around the rim, only then
            # move it - rotate_mesh turns about the origin, not the part centre
            lug = T.box(name + "_L%d" % i,
                        (width * 1.02, r * 0.20, r * 0.14), mat=mat_tire)
            T.rotate_mesh(lug, a, 'X')
            T.move(lug, (0, math.cos(a) * r * 0.97, math.sin(a) * r * 0.97))
            parts.append(lug)
    w = T.join(parts, name)
    T.shade_smooth(w, angle=math.radians(32))
    return w


# ------------------------------------------------------------------ accessories

def add_grille(parts, spec, mat):
    """The radiator grille - a rounded panel following the front outline."""
    g = spec.get("grille")
    if not g:
        return
    L = spec["length"]
    z = g["z"]
    t = g.get("t", 0.045)
    hw = body_half_width(spec, t, z)
    if hw is None:
        hw = g["w"] * 0.5
    w = min(g["w"], hw * 1.75)
    steps = 10
    sections = []
    for i in range(steps + 1):
        u = i / steps
        x = -w * 0.5 + w * u
        # forward distance taken from the outline -> the panel bends with the mask
        rel = abs(x) / max(hw, 1e-3)
        push = math.sqrt(max(0.0, 1.0 - min(1.0, rel) ** 2))
        y = -L * 0.5 + L * t - 0.02 * push
        hh = g["h"] * 0.5
        sections.append([(x, y, z - hh), (x, y - 0.06, z - hh * 0.72),
                         (x, y - 0.06, z + hh * 0.72), (x, y, z + hh)])
    ob = T.loft("Grille", sections, mat, smooth=True)
    parts.append(ob)


def add_lights(parts, spec, L, mat_head, mat_tail):
    """Lights are seated exactly on the body surface, following its outline."""
    hl = spec.get("headlights")
    if hl:
        t = hl.get("t", 0.055)
        # the light must lie within the body profile, or it floats above the bonnet
        z = clamp_into_body(spec, t, hl["z"], hl["r"] * 0.5)
        hw = body_half_width(spec, t, z) or hl["x"]
        y = -L * 0.5 + L * t
        for sgn in (-1, 1):
            x = sgn * min(hl["x"], hw - hl["r"] * 0.35)
            surf = body_half_width(spec, t, z) or hw
            depth = math.sqrt(max(0.02, 1.0 - (abs(x) / max(surf, 1e-3)) ** 2))
            r = hl["r"]
            # the lens stands slightly proud of the body, with a dark ring around it
            lens_ = T.uv_sphere("HL", r, 18, 10, (x, y - r * 0.22 * depth, z),
                                mat_head, scale=(1, 0.62, 1))
            parts.append(lens_)
            ring = T.torus("HLRing", r * 1.02, r * 0.15, 20, 8,
                           (x, y - r * 0.06 * depth, z), spec["_accent_mat"],
                           axis='Y')
            parts.append(ring)
    tl = spec.get("taillights")
    if tl:
        t = 1.0 - tl.get("t", 0.045)
        z = clamp_into_body(spec, t, tl["z"], tl["h"] * 0.5)
        hw = body_half_width(spec, t, z) or tl["x"]
        full = tl["x"] == 0.0                 # a light spanning the whole tail
        w = min(tl["w"], hw * 1.84) if full else tl["w"]
        for sgn in (-1, 1):
            x = 0.0 if full else sgn * min(tl["x"], max(0.0, hw - w * 0.5))
            # seat the light on the real tail surface using its outer corner -
            # otherwise it stays sunk into the panel and only a thin sliver
            # shows
            ts = body_front_t(spec, abs(x) + w * 0.5, z, False)
            y = -L * 0.5 + L * ts - tl["d"] * 0.35
            l = T.box("TL", (w, tl["d"], tl["h"]), (x, y, z), mat_tail)
            T.bevel(l, min(w, tl["h"]) * 0.3, 2)
            T.apply_modifiers(l)
            parts.append(l)
            if full:
                break


def add_spoiler(parts, spec, mat):
    sp = spec.get("spoiler")
    if not sp:
        return
    L = spec["length"]
    y = L * 0.5 - sp.get("y", 0.05)
    wing = T.box("Wing", (sp["w"], sp["d"], sp["t"]), mat=mat)
    T.rotate_mesh(wing, math.radians(sp.get("angle", 8)), 'X')
    T.move(wing, (0, y, sp["z"]))
    T.bevel(wing, sp["t"] * 0.4, 2)
    T.apply_modifiers(wing)
    parts.append(wing)
    ys = y + sp["d"] * 0.1
    ts = min(1.0, max(0.0, (ys + L * 0.5) / L))
    hwb, zb, zt = _mono_interp(spec["body"], ts)
    zsh = zb + (zt - zb) * spec.get("shoulder", 0.55)
    deck = (body_half_width(spec, ts, zsh) or hwb) * 0.72
    for sx in (-1, 1):
        # the stay stands on the deck, not beside it, and reaches the body -
        # otherwise the wing hangs in mid air
        x = sx * min(sp["w"] * 0.36, deck)
        h = max(sp["h"], sp["z"] - body_surface_z(spec, ts, x) + 0.03)
        st = T.box("WingStay", (sp["t"] * 0.9, sp["d"] * 0.5, h),
                   (x, ys, sp["z"] - h * 0.5), mat)
        T.bevel(st, sp["t"] * 0.3, 2)
        T.apply_modifiers(st)
        parts.append(st)


def add_rollcage(parts, spec, mat):
    rc = spec.get("rollcage")
    if not rc:
        return
    L = spec["length"]
    r = rc.get("r", 0.035)
    hw = rc["w"]
    top = rc["z"]
    base = rc.get("base", 0.14)
    for t in rc["hoops"]:
        y = -L * 0.5 + L * t
        for sx in (-1, 1):
            p = T.cylinder("RC", r, top - base, 8, (sx * hw, y,
                                                    base + (top - base) / 2),
                           'Z', mat)
            parts.append(p)
        bar = T.cylinder("RC", r, hw * 2, 8, (0, y, top), 'X', mat)
        parts.append(bar)
    ys = [-L * 0.5 + L * t for t in rc["hoops"]]
    if len(ys) >= 2:
        for sx in (-1, 1):
            length = abs(ys[-1] - ys[0])
            p = T.cylinder("RC", r, length, 8,
                           (sx * hw, (ys[0] + ys[-1]) / 2, top), 'Y', mat)
            parts.append(p)


def add_exhaust(parts, spec, mat):
    ex = spec.get("exhaust")
    if not ex:
        return
    L = spec["length"]
    for sx in ex.get("sides", (-1, 1)):
        p = T.cylinder("Exh", ex["r"], ex["len"], 10,
                       (sx * ex["x"], L * 0.5 + ex["len"] * 0.32, ex["z"]),
                       'Y', mat)
        parts.append(p)


def add_bumpers(parts, spec, mat):
    for front in (True, False):
        ob = build_wrap_bumper(spec, mat, front)
        if ob is not None:
            parts.append(ob)


# ------------------------------------------------------------------ catalog

def C(h):
    return h


CARS = [
    # ---------------------------------------------------------------- 1
    dict(
        id="bumble", name="Bumble", tagline="Cheerful little city runabout",
        paint="#ffc21c", accent="#3a3f4b", rim="#f4f6fa",
        length=3.30, width=1.60, wheelbase=2.06,
        wheel_r=0.40, wheel_w=0.26, wheel_out=0.0,
        px=3.8, p_top=2.8, body_steps=34, n_wall=4, n_top=15, tuck=0.24,
        shoulder=0.50, haunch=0.12, arch_len=0.80,
        body=[(0.00, 0.38, 0.36, 0.54),
              (0.04, 0.62, 0.29, 0.64),
              (0.12, 0.70, 0.25, 0.73),
              (0.26, 0.72, 0.23, 0.79),
              (0.55, 0.72, 0.23, 0.83),
              (0.82, 0.72, 0.24, 0.83),
              (0.93, 0.66, 0.27, 0.79),
              (1.00, 0.42, 0.35, 0.65)],
        cabin=dict(t0=0.28, t1=0.90, steps=20, px=3.4, p_top=2.8,
                   shoulder=0.58, rake_front=0.26, rake_rear=0.18,
                   n_wall=4, n_top=15,
                   profile=[(0.00, 0.57, 0.73, 0.85),
                            (0.20, 0.67, 0.75, 1.16),
                            (0.74, 0.67, 0.75, 1.18),
                            (1.00, 0.59, 0.73, 0.93)]),
        grille=dict(w=0.98, h=0.20, z=0.48, t=0.03),
        headlights=dict(x=0.46, z=0.66, r=0.17, t=0.075),
        taillights=dict(x=0.50, w=0.20, d=0.07, h=0.17, z=0.64, t=0.05),
        front_bumper=dict(z=0.36, d=0.12, h=0.13, reach=0.085, out=0.012),
        rear_bumper=dict(z=0.38, d=0.12, h=0.13, reach=0.075, out=0.012),
        exhaust=dict(x=0.38, z=0.26, r=0.05, len=0.16, sides=(1,)),
        stripes=[dict(t0=0.02, t1=0.98, z0=0.40, z1=0.50, color="#3a3f4b")],
        stats=dict(top=1.00, accel=1.00, grip=1.02, weight=1.00, fuel=1.05),
    ),
    # ---------------------------------------------------------------- 2
    dict(
        id="chili", name="Chili", tagline="Sharp sports coupe",
        paint="#e8342c", accent="#2a2e38", rim="#ffd76a",
        length=3.85, width=1.74, wheelbase=2.48,
        wheel_r=0.39, wheel_w=0.30, wheel_out=0.005,
        px=4.2, p_top=3.0, body_steps=38, n_wall=4, n_top=15, tuck=0.26,
        shoulder=0.46, haunch=0.14, arch_len=0.86,
        body=[(0.00, 0.34, 0.26, 0.44),
              (0.03, 0.62, 0.21, 0.52),
              (0.10, 0.74, 0.18, 0.58),
              (0.24, 0.78, 0.17, 0.63),
              (0.46, 0.79, 0.17, 0.68),
              (0.72, 0.79, 0.17, 0.71),
              (0.90, 0.75, 0.19, 0.70),
              (1.00, 0.46, 0.26, 0.60)],
        cabin=dict(t0=0.30, t1=0.86, steps=22, px=3.6, p_top=2.9,
                   shoulder=0.50, rake_front=0.30, rake_rear=0.26,
                   n_wall=4, n_top=15,
                   profile=[(0.00, 0.60, 0.60, 0.70),
                            (0.28, 0.71, 0.62, 0.99),
                            (0.62, 0.71, 0.62, 1.00),
                            (1.00, 0.62, 0.60, 0.76)]),
        grille=dict(w=0.96, h=0.16, z=0.38, t=0.028),
        headlights=dict(x=0.52, z=0.53, r=0.14, t=0.065),
        taillights=dict(x=0.52, w=0.26, d=0.07, h=0.12, z=0.58, t=0.04),
        front_bumper=dict(z=0.29, d=0.13, h=0.12, reach=0.075, out=0.012),
        spoiler=dict(w=1.52, d=0.28, t=0.055, z=0.90, h=0.20, angle=10, y=0.12),
        exhaust=dict(x=0.42, z=0.24, r=0.055, len=0.18),
        stripes=[dict(t0=0.02, t1=0.98, z0=0.30, z1=0.40, color="#ffffff")],
        stats=dict(top=1.08, accel=1.06, grip=0.98, weight=0.96, fuel=0.92),
    ),
    # ---------------------------------------------------------------- 3
    dict(
        id="sprout", name="Sprout", tagline="Off-road buggy for every pothole",
        paint="#3fbf5f", accent="#26313a", rim="#ffffff", tire="#25282e",
        length=3.25, width=1.66, wheelbase=2.12,
        wheel_r=0.50, wheel_w=0.36, wheel_out=0.09, tread_count=16,
        px=3.4, p_top=2.6, body_steps=32, n_wall=4, n_top=13, tuck=0.22,
        shoulder=0.50, haunch=0.10, arch_len=0.86,
        body=[(0.00, 0.36, 0.44, 0.64),
              (0.06, 0.62, 0.38, 0.76),
              (0.18, 0.70, 0.36, 0.84),
              (0.36, 0.72, 0.35, 0.90),
              (0.62, 0.72, 0.35, 0.92),
              (0.86, 0.70, 0.36, 0.88),
              (1.00, 0.44, 0.44, 0.76)],
        cabin=dict(t0=0.34, t1=0.74, steps=12, roof=False, shoulder=0.55,
                   n_wall=3, n_top=11,
                   profile=[(0.00, 0.56, 0.88, 1.00),
                            (0.40, 0.62, 0.90, 1.08),
                            (1.00, 0.58, 0.88, 1.04)]),
        rollcage=dict(w=0.60, z=1.34, base=0.88, r=0.045,
                      hoops=[0.38, 0.60, 0.76]),
        grille=dict(w=0.92, h=0.22, z=0.62, t=0.04),
        headlights=dict(x=0.44, z=0.80, r=0.16, t=0.085),
        taillights=dict(x=0.48, w=0.18, d=0.07, h=0.15, z=0.78, t=0.05),
        front_bumper=dict(z=0.56, d=0.14, h=0.16, reach=0.10, out=0.02),
        rear_bumper=dict(z=0.58, d=0.14, h=0.16, reach=0.09, out=0.02),
        lightbar=dict(z=1.40, t=0.42, w=0.92, count=4),
        stats=dict(top=0.96, accel=1.03, grip=1.14, weight=1.02, fuel=1.10),
    ),
    # ---------------------------------------------------------------- 4
    dict(
        id="frosty", name="Frosty", tagline="Winter pick-up with a snow plough",
        paint="#3aa7e8", accent="#e9f4ff", rim="#c9d6e2",
        length=4.05, width=1.82, wheelbase=2.62,
        wheel_r=0.45, wheel_w=0.33, wheel_out=0.03, tread_count=16,
        px=4.4, p_top=3.4, body_steps=40, n_wall=4, n_top=13, tuck=0.22,
        shoulder=0.52, haunch=0.12, arch_len=0.90,
        body=[(0.00, 0.40, 0.42, 0.70),
              (0.04, 0.68, 0.36, 0.84),
              (0.12, 0.80, 0.33, 0.92),
              (0.30, 0.82, 0.32, 0.96),
              (0.50, 0.82, 0.32, 0.96),
              (0.545, 0.82, 0.32, 0.80),
              (0.62, 0.81, 0.33, 0.78),
              (0.92, 0.80, 0.34, 0.78),
              (1.00, 0.52, 0.42, 0.72)],
        cabin=dict(t0=0.22, t1=0.53, steps=14, px=3.8, p_top=3.2,
                   shoulder=0.62, rake_front=0.34, rake_rear=0.12,
                   n_wall=4, n_top=13,
                   profile=[(0.00, 0.64, 0.94, 1.06),
                            (0.38, 0.76, 0.96, 1.40),
                            (1.00, 0.76, 0.96, 1.40)]),
        grille=dict(w=1.10, h=0.26, z=0.62, t=0.03),
        headlights=dict(x=0.56, z=0.82, r=0.15, t=0.065),
        taillights=dict(x=0.62, w=0.18, d=0.07, h=0.20, z=0.66, t=0.03),
        front_bumper=dict(z=0.48, d=0.15, h=0.17, reach=0.07, out=0.015),
        rear_bumper=dict(z=0.50, d=0.15, h=0.17, reach=0.06, out=0.015),
        plow=dict(w=1.62, h=0.62, z=0.30, gap=0.14, color="#ff7a3d"),
        bedrails=dict(t0=0.57, t1=0.97, w=0.80, z=0.78, h=0.18),
        exhaust=dict(x=0.46, z=0.30, r=0.06, len=0.18, sides=(1,)),
        stats=dict(top=1.00, accel=1.04, grip=1.12, weight=1.10, fuel=1.22),
    ),
    # ---------------------------------------------------------------- 5
    dict(
        id="scoop", name="Scoop", tagline="Ice cream van heading for the beach",
        paint="#ff9ec4", accent="#5d4a55", rim="#ffd76a",
        length=3.80, width=1.78, wheelbase=2.46,
        wheel_r=0.39, wheel_w=0.28, wheel_out=0.0,
        px=4.8, p_top=4.0, body_steps=40, n_wall=4, n_top=13, tuck=0.18,
        shoulder=0.70, haunch=0.11, arch_len=0.86,
        body=[(0.00, 0.44, 0.32, 0.86),
              (0.03, 0.70, 0.28, 1.10),
              (0.08, 0.79, 0.26, 1.34),
              (0.16, 0.82, 0.25, 1.48),
              (0.55, 0.83, 0.24, 1.54),
              (0.88, 0.83, 0.25, 1.52),
              (0.96, 0.78, 0.28, 1.38),
              (1.00, 0.52, 0.34, 1.10)],
        cabin=None,
        # each window must stay on its own face - a windscreen wide enough to
        # wrap the corner overlapped the side one and the two panels tore
        # each other apart
        windows=[dict(kind="front", w=0.58, z0=0.90, z1=1.38, mat="accent"),
                 dict(kind="front", w=0.50, z0=0.94, z1=1.34),
                 dict(kind="side", t0=0.13, t1=0.34, z0=0.90, z1=1.34,
                      mat="accent"),
                 dict(kind="side", t0=0.15, t1=0.32, z0=0.94, z1=1.30),
                 dict(kind="side", t0=0.42, t1=0.78, z0=0.88, z1=1.32,
                      mat="accent"),
                 dict(kind="side", t0=0.44, t1=0.76, z0=0.92, z1=1.28),
                 dict(kind="rear", w=0.46, z0=0.90, z1=1.26)],
        stripes=[dict(t0=0.05, t1=0.97, z0=0.72, z1=0.86, color="#7fe3d8"),
                 dict(t0=0.05, t1=0.97, z0=0.58, z1=0.70, color="#fff6e8", i=1)],
        grille=dict(w=1.00, h=0.18, z=0.50, t=0.026),
        headlights=dict(x=0.52, z=0.66, r=0.15, t=0.055),
        taillights=dict(x=0.58, w=0.17, d=0.07, h=0.24, z=0.70, t=0.03),
        front_bumper=dict(z=0.38, d=0.14, h=0.15, reach=0.06, out=0.015),
        rear_bumper=dict(z=0.40, d=0.14, h=0.15, reach=0.05, out=0.015),
        conetop=dict(z=1.50, r=0.25, h=0.60),
        stats=dict(top=1.00, accel=1.02, grip=1.18, weight=1.06, fuel=1.28),
    ),
    # ---------------------------------------------------------------- 6
    dict(
        id="bolt", name="Bolt", tagline="A formula car for the brave",
        paint="#2f6df6", accent="#12182a", rim="#ff3355", tire="#1c1f26",
        length=4.35, width=1.05, wheelbase=2.92, track=1.74,
        wheel_r=0.40, wheel_w=0.36, rim_frac=0.5,
        px=3.2, p_top=2.4, body_steps=42, n_wall=3, n_top=13, tuck=0.26,
        shoulder=0.46, haunch=0.0,
        body=[(0.00, 0.17, 0.14, 0.32),
              (0.07, 0.30, 0.13, 0.40),
              (0.20, 0.42, 0.12, 0.50),
              (0.38, 0.50, 0.12, 0.64),
              (0.56, 0.52, 0.12, 0.74),
              (0.74, 0.46, 0.13, 0.78),
              (0.90, 0.34, 0.14, 0.62),
              (1.00, 0.20, 0.16, 0.46)],
        cabin=None,
        cockpit=dict(t=0.50, z=0.68, r=0.22,
                     airbox=dict(w=0.22, z=0.98, drop=0.10, len=0.58)),
        stripes=[dict(t0=0.14, t1=0.90, z0=0.28, z1=0.40, color="#ffffff")],
        spoiler=dict(w=1.42, d=0.36, t=0.06, z=0.94, h=0.34, angle=14, y=0.04),
        frontwing=dict(w=1.56, d=0.38, t=0.06, z=0.20, y=0.16),
        sidepods=dict(w=0.36, d=1.30, h=0.40, x=0.50, z=0.34, t=0.55),
        headlights=None,
        taillights=dict(x=0.0, w=0.14, d=0.06, h=0.10, z=0.42, t=0.03),
        exhaust=dict(x=0.0, z=0.60, r=0.06, len=0.22, sides=(0,)),
        stats=dict(top=1.24, accel=1.22, grip=1.06, weight=0.88, fuel=0.88),
    ),
    # ---------------------------------------------------------------- 7
    dict(
        id="rocky", name="Rocky", tagline="Monster truck that climbs anything",
        paint="#8b5cf6", accent="#2b2233", rim="#ffcf3d", tire="#22242b",
        length=3.70, width=1.86, wheelbase=2.36,
        wheel_r=0.66, wheel_w=0.46, wheel_out=0.10, tread_count=18,
        rim_frac=0.5,
        px=3.6, p_top=2.8, body_steps=34, n_wall=4, n_top=13, tuck=0.24,
        shoulder=0.50, haunch=0.06, arch_len=0.95,
        body=[(0.00, 0.40, 0.72, 1.02),
              (0.06, 0.68, 0.66, 1.14),
              (0.20, 0.78, 0.64, 1.20),
              (0.40, 0.80, 0.63, 1.24),
              (0.66, 0.80, 0.63, 1.24),
              (0.90, 0.76, 0.65, 1.16),
              (1.00, 0.46, 0.72, 1.04)],
        cabin=dict(t0=0.26, t1=0.70, steps=14, px=3.6, p_top=3.0,
                   shoulder=0.58, rake_front=0.30, rake_rear=0.20,
                   n_wall=4, n_top=13,
                   profile=[(0.00, 0.62, 1.20, 1.34),
                            (0.32, 0.74, 1.22, 1.64),
                            (0.78, 0.74, 1.22, 1.64),
                            (1.00, 0.64, 1.20, 1.40)]),
        grille=dict(w=1.02, h=0.24, z=0.92, t=0.04),
        headlights=dict(x=0.50, z=1.08, r=0.17, t=0.08),
        taillights=dict(x=0.56, w=0.20, d=0.07, h=0.18, z=1.02, t=0.05),
        front_bumper=dict(z=0.82, d=0.16, h=0.18, reach=0.09, out=0.02),
        rear_bumper=dict(z=0.84, d=0.16, h=0.18, reach=0.08, out=0.02),
        lightbar=dict(z=1.70, t=0.44, w=1.00, count=5),
        exhaust=dict(x=0.60, z=1.14, r=0.07, len=0.52, sides=(-1, 1),
                     vertical=True),
        stats=dict(top=1.06, accel=1.08, grip=1.20, weight=1.16, fuel=1.15),
    ),
    # ---------------------------------------------------------------- 8
    dict(
        id="neo", name="Neo", tagline="Glowing concept from the future",
        paint="#18e0c8", accent="#101728", rim="#00f0ff", tire="#181b22",
        emissive_trim="#00f6ff",
        length=4.00, width=1.78, wheelbase=2.62,
        wheel_r=0.39, wheel_w=0.30, wheel_out=0.0, rim_frac=0.66, tread=False,
        px=5.0, p_top=3.4, body_steps=38, n_wall=4, n_top=15, tuck=0.28,
        shoulder=0.44, haunch=0.13, arch_len=0.88,
        body=[(0.00, 0.32, 0.24, 0.40),
              (0.03, 0.62, 0.20, 0.46),
              (0.10, 0.76, 0.18, 0.52),
              (0.26, 0.80, 0.17, 0.58),
              (0.50, 0.81, 0.17, 0.64),
              (0.76, 0.80, 0.18, 0.68),
              (0.92, 0.74, 0.20, 0.64),
              (1.00, 0.48, 0.25, 0.54)],
        cabin=dict(t0=0.24, t1=0.86, steps=22, px=4.2, p_top=3.2,
                   shoulder=0.52, rake_front=0.34, rake_rear=0.24,
                   n_wall=4, n_top=15,
                   profile=[(0.00, 0.56, 0.54, 0.62),
                            (0.34, 0.72, 0.58, 0.94),
                            (0.70, 0.72, 0.58, 0.96),
                            (1.00, 0.60, 0.56, 0.72)]),
        headlights=dict(x=0.56, z=0.44, r=0.11, t=0.055),
        taillights=dict(x=0.0, w=1.16, d=0.06, h=0.08, z=0.58, t=0.03),
        underglow=True,
        spoiler=dict(w=1.52, d=0.24, t=0.05, z=0.84, h=0.14, angle=6, y=0.08),
        stats=dict(top=1.22, accel=1.18, grip=1.18, weight=0.92, fuel=1.02),
    ),
]


# ------------------------------------------------------------------ assembly

def body_half_width_at_wheel(spec):
    """
    The widest point of the body across the whole volume the wheel occupies.
    Measuring at axle height alone is not enough - the body is widest at the
    shoulder, which may sit above the axle (or, on high-riding cars, below it).
    """
    L = spec["length"]
    wr = spec["wheel_r"]
    wy = spec["wheelbase"] * 0.5
    hw = 0.0
    for cy in (-wy, wy):
        for i in range(13):                    # radius 0..wr
            rad = wr * i / 12.0
            for k in range(24):                # angle around the axle
                a = TAU * k / 24
                y = cy + math.cos(a) * rad
                z = wr + math.sin(a) * rad
                t = (y + L * 0.5) / L
                if not 0.0 <= t <= 1.0 or z <= 0.0:
                    continue
                w = body_half_width(spec, t, z)
                if w is None:
                    continue
                hw = max(hw, w)
    if hw <= 0.0:
        t = (-spec["wheelbase"] * 0.5 + L * 0.5) / L
        hw = _mono_interp(spec["body"], t)[0]
    return hw


def auto_track(spec):
    """
    A track width that keeps the outer face of each wheel proud of the body.
    If a wheel merely sits flush with the body side, both surfaces occupy the
    same place and the engine redraws them alternately (flickering, a wheel
    half swallowed by the body). Hence we always leave the measured WHEEL_GAP.
    """
    if spec.get("track"):
        return spec["track"]
    hw = body_half_width_at_wheel(spec)
    out = hw + WHEEL_GAP + spec.get("wheel_out", 0.0)
    return 2.0 * (out - spec["wheel_w"] * 0.5)


def build_car(spec):
    T.clear_material_cache()
    spec["track"] = auto_track(spec)
    # the width used by physics must match what is actually visible (the
    # wheels stick out of the body, otherwise cars would pass through each other)
    spec["width"] = round(max(spec["width"],
                              spec["track"] + spec["wheel_w"]), 3)
    cid = spec["id"]
    paint = T.material("Paint_" + cid, spec["paint"], roughness=0.26,
                       metallic=0.10, clearcoat=0.8)
    accent = T.material("Accent_" + cid, spec.get("accent", "#2b2f38"),
                        roughness=0.42)
    glass = T.material("Glass_" + cid, "#2b3a4e", roughness=0.08,
                       metallic=0.0, alpha=1.0, clearcoat=1.0)
    tire = T.material("Tire_" + cid, spec.get("tire", "#2a2d34"), roughness=0.82)
    rim = T.material("Rim_" + cid, spec.get("rim", "#e8ecf2"), roughness=0.22,
                     metallic=0.85)
    hub = T.material("Hub_" + cid, "#4a4f5a", roughness=0.35, metallic=0.7)
    head = T.material("Head_" + cid, "#fff8e0", roughness=0.1,
                      emission="#fff3cc", emission_strength=0.9)
    tail = T.material("Tail_" + cid, "#ff2e3c", roughness=0.15,
                      emission="#ff2020", emission_strength=1.2)
    chrome = T.material("Chrome_" + cid, "#c8cdd6", roughness=0.18, metallic=1.0)

    L = spec["length"]
    spec["_accent_mat"] = accent
    parts = []
    body = build_body("Body", spec, paint)
    parts.append(body)
    parts.extend(build_cabin("Glass", spec, glass, paint))
    # arches and liners over the wheels
    if spec.get("fenders", False):
        wb0 = spec["wheelbase"]
        tr0 = spec["track"]
        ins = spec.get("wheel_inset", 0.0)
        for sx in (-1, 1):
            for sy in (-1, 1):
                x = sx * (tr0 * 0.5 + ins)
                y = sy * wb0 * 0.5
                parts.append(build_fender("Fender", x, y, spec["wheel_r"],
                                          spec["wheel_w"], paint, spec))
                parts.append(build_arch_liner("Arch", x, y, spec["wheel_r"],
                                              spec["wheel_w"], accent, spec))
    add_panels(parts, spec, dict(glass=glass, paint=paint, accent=accent,
                                 head=head, tail=tail, chrome=chrome))
    add_grille(parts, spec, accent)
    add_lights(parts, spec, L, head, tail)
    add_spoiler(parts, spec, accent)
    add_rollcage(parts, spec, chrome)
    add_bumpers(parts, spec, accent)

    ex = spec.get("exhaust")
    if ex and ex.get("vertical"):
        for sx in ex.get("sides", (-1, 1)):
            p = T.cylinder("Exh", ex["r"], ex["len"], 10,
                           (sx * ex["x"], L * 0.18, ex["z"] + ex["len"] * 0.4),
                           'Z', chrome)
            parts.append(p)
    else:
        add_exhaust(parts, spec, chrome)

    fw = spec.get("frontwing")
    if fw:
        w = T.box("FrontWing", (fw["w"], fw["d"], fw["t"]), mat=accent)
        T.rotate_mesh(w, math.radians(-6), 'X')
        T.move(w, (0, -L * 0.5 + fw["y"], fw["z"]))
        T.bevel(w, fw["t"] * 0.4, 2)
        T.apply_modifiers(w)
        parts.append(w)
        for sx in (-1, 1):
            ep = T.box("WingPlate", (0.05, fw["d"] * 0.9, 0.18),
                       (sx * fw["w"] * 0.48, -L * 0.5 + fw["y"], fw["z"] + 0.08),
                       paint)
            T.bevel(ep, 0.02, 2)
            T.apply_modifiers(ep)
            parts.append(ep)

    sp = spec.get("sidepods")
    if sp:
        for sx in (-1, 1):
            pod = T.box("Pod", (sp["w"], sp["d"], sp["h"]),
                        (sx * sp["x"], L * 0.06, sp["z"]), paint)
            T.bevel(pod, sp["h"] * 0.3, 3)
            T.apply_modifiers(pod)
            parts.append(pod)

    ck = spec.get("cockpit")
    if ck:
        y = -L * 0.5 + L * ck["t"]
        hole = T.uv_sphere("Cockpit", ck["r"], 20, 10, (0, y, ck["z"]),
                           T.material("Cockpit_" + cid, "#14161d",
                                      roughness=0.55),
                           scale=(0.72, 1.5, 0.34))
        parts.append(hole)
        hr = T.uv_sphere("Headrest", ck["r"] * 0.62, 16, 9,
                         (0, y + ck["r"] * 1.35, ck["z"] + ck["r"] * 0.16),
                         accent, scale=(0.9, 0.7, 0.9))
        parts.append(hr)
        ab = ck.get("airbox")
        if ab:
            sections = []
            for i in range(9):
                u = i / 8
                yy = y + ck["r"] * 1.7 + ab["len"] * u
                hw = ab["w"] * (1.0 - 0.55 * u)
                zt = ab["z"] - ab["drop"] * u * u
                sections.append([(-hw, yy, ck["z"] + 0.02),
                                 (hw, yy, ck["z"] + 0.02),
                                 (hw * 0.7, yy, zt), (-hw * 0.7, yy, zt)])
            parts.append(T.loft("Airbox", sections, paint, smooth=True))
            intake = T.uv_sphere("Intake", ab["w"] * 0.72, 16, 9,
                                 (0, y + ck["r"] * 1.62, ab["z"] - 0.06),
                                 T.material("Intake_" + cid, "#0d1018",
                                            roughness=0.6),
                                 scale=(1.0, 0.5, 0.8))
            parts.append(intake)

    lb = spec.get("lightbar")
    if lb:
        bar = T.box("LightBar", (lb["w"], 0.09, 0.10), (0, -L * 0.18, lb["z"]),
                    accent)
        T.bevel(bar, 0.03, 2)
        T.apply_modifiers(bar)
        parts.append(bar)
        n = lb["count"]
        for i in range(n):
            x = (i - (n - 1) / 2) * (lb["w"] / n)
            lp = T.cylinder("LBL", 0.055, 0.05, 12,
                            (x, -L * 0.18 - 0.06, lb["z"]), 'Y', head)
            parts.append(lp)
        for sx in (-1, 1):
            st = T.box("LBStay", (0.05, 0.05, lb["t"] * 0.5),
                       (sx * lb["w"] * 0.4, -L * 0.18, lb["z"] - lb["t"] * 0.25),
                       accent)
            parts.append(st)

    plow = spec.get("plow")
    if plow:
        pm = T.material("Plow_" + cid, plow["color"], roughness=0.32,
                        metallic=0.35)
        pw = plow["w"] * 0.5
        z0 = plow["z"]
        h = plow["h"]
        thick = 0.055
        sweep = plow.get("bow", 0.11) * plow["w"]     # wings raked backwards
        lean = plow.get("lean", 0.17)                 # top edge leaning back
        nx, nv = 12, 10

        # the rearmost point of the blade (top outer corner of a wing) must
        # stay ahead of the body's nose, or the plough grows through the
        # bonnet and the headlights
        back = -L * 0.5 - plow.get("gap", 0.16)
        base = back - sweep - lean

        def blade(x, v):
            """A point on the blade: v=0 low and forward, v=1 high and raked back."""
            bow = (abs(x) / pw) ** 2 * sweep
            return base + bow + math.sin(v * math.pi * 0.66) * lean

        sections = []
        for i in range(nx + 1):
            x = -pw + 2 * pw * (i / nx)
            fronts, backs = [], []
            for j in range(nv + 1):
                v = j / nv
                z = z0 + h * v
                y = blade(x, v)
                fronts.append((x, y, z))
                backs.append((x, y + thick, z))
            sections.append(fronts + list(reversed(backs)))
        parts.append(T.loft("Plow", sections, pm, smooth=True))

        wm = T.material("PlowTrim_" + cid, "#ffffff", roughness=0.4)
        trim = []
        for i in range(nx + 1):
            x = -pw + 2 * pw * (i / nx)
            trim.append((x, blade(x, 1.0) - 0.014, z0 + h))
        parts.append(sweep_path("PlowTrim", trim, 0.07, 0.08, wm, round_seg=3))

        # frame: two arms from the body to the blade plus a cross brace.
        # an arm must start behind the blade, otherwise its pins poke out in front
        for sx in (-1, 1):
            ax = sx * 0.28
            y_arm = blade(ax, 0.30) + thick
            arm_len = -L * 0.5 + 0.10 - y_arm
            arm = T.cylinder("PlowArm", 0.05, arm_len, 8,
                             (ax, y_arm + arm_len * 0.5, z0 + h * 0.30),
                             'Y', chrome)
            parts.append(arm)
        parts.append(T.cylinder("PlowBar", 0.045, 0.60, 8,
                                (0, blade(0.30, 0.28) + thick + 0.03,
                                 z0 + h * 0.28), 'X', chrome))

    br = spec.get("bedrails")
    if br:
        # the bed rail follows the body edge - a straight bar at constant width
        # would end in mid air at the back, where the body narrows
        xf = br.get("xf", 0.94)
        steps = 14
        edge = []
        for i in range(steps + 1):
            t = br["t0"] + (br["t1"] - br["t0"]) * i / steps
            hw = _mono_interp(spec["body"], t)[0] * xf
            z = body_surface_z(spec, t, hw) + br["h"] * 0.35
            edge.append((hw, -L * 0.5 + L * t, z))
        # left side front to back, round the bed onto the right side and back
        path = ([(-x, y, z) for x, y, z in edge]
                + [(x, y, z) for x, y, z in reversed(edge)])
        parts.append(sweep_path("BedRail", path, 0.10, br["h"], paint,
                                round_seg=3))

    ct = spec.get("conetop")
    if ct:
        cm = T.material("Cone_" + cid, "#f0b95e", roughness=0.65)
        sm = T.material("Scoop_" + cid, "#fff1f6", roughness=0.32)
        sm2 = T.material("Scoop2_" + cid, "#ffb3c9", roughness=0.32)
        cy = L * 0.10
        # the cone: build at the origin, turn the tip downwards, only then move it
        c = T.cone("ConeBody", ct["r"], ct["h"], 16, (0, 0, 0), cm,
                   radius_top=0.03)
        T.rotate_mesh(c, math.pi, 'X')
        T.move(c, (0, cy, ct["z"] + ct["h"] * 0.5))
        parts.append(c)
        base = ct["z"] + ct["h"]
        parts.append(T.uv_sphere("ScoopBall", ct["r"] * 0.98, 18, 11,
                                 (0, cy, base - ct["r"] * 0.22), sm))
        parts.append(T.uv_sphere("ScoopBall2", ct["r"] * 0.66, 16, 10,
                                 (0, cy, base + ct["r"] * 0.42), sm2))
        parts.append(T.uv_sphere("Cherry", ct["r"] * 0.20, 12, 8,
                                 (0, cy, base + ct["r"] * 0.98),
                                 T.material("Cherry_" + cid, "#e8342c",
                                            roughness=0.3)))
        # the rod holding the cone
        parts.append(T.cylinder("ConeRod", 0.045, 0.24, 8,
                                (0, cy, ct["z"] - 0.10), 'Z', chrome))

    if spec.get("underglow"):
        gm = T.material("Glow_" + cid, spec.get("emissive_trim", "#00f6ff"),
                        roughness=0.3, emission=spec.get("emissive_trim",
                                                         "#00f6ff"),
                        emission_strength=2.0)
        for sx in (-1, 1):
            strip = T.box("Glow", (0.05, L * 0.62, 0.05),
                          (sx * spec["width"] * 0.46, 0.02, 0.20), gm)
            parts.append(strip)

    body_ob = T.join(parts, "Body")
    T.bevel(body_ob, spec.get("body_bevel", 0.012), 2,
            angle_limit=math.radians(55))
    T.apply_modifiers(body_ob)
    T.shade_smooth(body_ob, angle=math.radians(38))

    # wheels as separate objects whose origin is the axle
    wb = spec["wheelbase"]
    tr = spec["track"]
    r = spec["wheel_r"]
    ww = spec["wheel_w"]
    inset = spec.get("wheel_inset", 0.0)
    wheels = []
    layout = [("WheelFL", -1, -1), ("WheelFR", 1, -1),
              ("WheelRL", -1, 1), ("WheelRR", 1, 1)]
    for name, sx, sy in layout:
        w = build_wheel(name, r, ww, spec, tire, rim, hub)
        x = sx * (tr * 0.5 + inset)
        y = sy * wb * 0.5
        T.set_origin_to(w, (0, 0, 0))
        w.location = (x, y, r)
        wheels.append(w)

    root = bpy.data.objects.new("Car_" + cid, None)
    bpy.context.collection.objects.link(root)
    body_ob.parent = root
    for w in wheels:
        w.parent = root
    return root, spec


def body_overhang(root, spec):
    """
    How far the bodywork reaches past the wheels. Nothing should: a panel or a
    bumper standing out beyond the tyres reads as a chassis sticking out of the
    car, and it also overruns the collision width, which is derived from the
    track. Returns (widest bodywork, wheel envelope) in metres.
    """
    env = (spec["track"] * 0.5 + spec.get("wheel_inset", 0.0)
           + spec["wheel_w"] * 0.59)          # 0.59 = the outer face of a hub
    worst = 0.0
    for ob in root.children:
        if ob.type != 'MESH' or ob.name.startswith("Wheel"):
            continue
        mw = ob.matrix_world
        for v in ob.data.vertices:
            worst = max(worst, abs((mw @ v.co).x))
    return worst, env


# ------------------------------------------------------------------ main

def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    do_preview = "--preview" in argv
    only = None
    if "--only" in argv:
        only = argv[argv.index("--only") + 1]

    manifest = []
    for spec in CARS:
        if only and spec["id"] != only:
            continue
        T.reset_scene()
        root, spec = build_car(spec)
        bad = sum(T.check_normals(o, "%s/%s" % (spec["id"], o.name))
                  for o in root.children if o.type == 'MESH')
        wide, env = body_overhang(root, spec)
        if wide > env:
            print("WARNING %s: bodywork reaches %.0f mm past the wheels "
                  "(%.3f > %.3f)" % (spec["id"], (wide - env) * 1000, wide, env))
        path = os.path.join(OUT, "Cars", "car_%s.usdz" % spec["id"])
        T.export_usdz(path)
        tris = T.tri_count()
        print("CAR %-8s tris=%-6d normals=%s body/wheels=%.3f/%.3f -> %s"
              % (spec["id"], tris, "OK" if bad == 0 else "%d BAD" % bad,
                 wide, env, path))
        manifest.append(dict(id=spec["id"], name=spec["name"],
                             tagline=spec["tagline"], paint=spec["paint"],
                             length=spec["length"], width=spec["width"],
                             wheelR=spec["wheel_r"], wheelbase=spec["wheelbase"],
                             tris=tris, **spec["stats"]))
        if do_preview:
            R.setup_studio()
            R.add_ground()
            L = spec["length"]
            R.render(os.path.join(PREVIEW, "car_%s.png" % spec["id"]),
                     cam_loc=(4.2, -5.0, 3.0), look_at=(0, 0, 0.55),
                     res=(620, 460), lens=70)
            if "--views" in argv:
                R.render(os.path.join(PREVIEW, "car_%s_side.png" % spec["id"]),
                         cam_loc=(0, 0, 0.7), look_at=(-1, 0, 0.7),
                         res=(620, 300), ortho=L * 1.25)
                R.render(os.path.join(PREVIEW, "car_%s_front.png" % spec["id"]),
                         cam_loc=(0, -6, 0.7), look_at=(0, 0, 0.7),
                         res=(420, 340), ortho=spec["width"] * 2.0)
                R.render(os.path.join(PREVIEW, "car_%s_top.png" % spec["id"]),
                         cam_loc=(0, 0, 8), look_at=(0, 0, 0),
                         res=(360, 620), ortho=L * 1.25)

    if not only:
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, "cars_manifest.json"), "w") as f:
            json.dump(manifest, f, indent=1)
    print("DONE cars")


main()
