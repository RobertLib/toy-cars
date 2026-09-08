"""
tcprops - props for the surroundings of a track (trees, palms, rocks, stands...).

Every function builds its object at the origin and returns it; the caller then
moves and rotates it and finally joins everything into a few objects to keep
the draw call count down.
"""
import math
import random

from . import tcmesh as T

TAU = math.pi * 2


def _place(ob, x, y, z=0.0, rot=0.0, scale=1.0):
    if scale != 1.0:
        T.scale_mesh(ob, scale)
    if rot:
        T.rotate_mesh(ob, rot, 'Z')
    T.move(ob, (x, y, z))
    return ob


# ------------------------------------------------------------------ trees

def tree_round(pal, h=5.0, rnd=None, snow=False):
    rnd = rnd or random
    parts = []
    tr = h * 0.085
    trunk = T.cylinder("Trunk", tr, h * 0.46, 8, (0, 0, h * 0.23), 'Z',
                       pal["bark"], radius_top=tr * 0.8)
    parts.append(trunk)
    base = h * 0.50
    blobs = [(0.0, 0.0, base + h * 0.16, h * 0.29),
             (-h * 0.10, h * 0.06, base + h * 0.30, h * 0.23),
             (h * 0.11, -h * 0.05, base + h * 0.27, h * 0.21),
             (0.0, -h * 0.02, base + h * 0.44, h * 0.17)]
    for bx, by, bz, br in blobs:
        parts.append(T.uv_sphere("Leaf", br * rnd.uniform(0.9, 1.12), 10, 6,
                                 (bx, by, bz), pal["leaf"],
                                 scale=(1.0, 1.0, 0.86)))
        if snow:
            parts.append(T.uv_sphere("Snow", br * 0.86, 10, 5,
                                     (bx, by, bz + br * 0.30), pal["snow"],
                                     scale=(1.0, 1.0, 0.42)))
    return T.join(parts, "Tree")


def tree_pine(pal, h=7.0, rnd=None, snow=True):
    rnd = rnd or random
    parts = []
    tr = h * 0.055
    parts.append(T.cylinder("Trunk", tr, h * 0.30, 7, (0, 0, h * 0.15), 'Z',
                            pal["bark"], radius_top=tr * 0.7))
    tiers = 4
    for k in range(tiers):
        u = k / (tiers - 1)
        r = h * (0.30 - 0.19 * u) * rnd.uniform(0.94, 1.06)
        z = h * (0.22 + 0.60 * u)
        ch = h * 0.30
        parts.append(T.cone("Tier", r, ch, 10, (0, 0, z + ch * 0.5),
                            pal["leaf"], radius_top=r * 0.16))
        if snow:
            parts.append(T.cone("SnowTier", r * 0.80, ch * 0.34,
                                10, (0, 0, z + ch * 0.72), pal["snow"],
                                radius_top=r * 0.10))
    return T.join(parts, "Pine")


def palm(pal, h=6.5, rnd=None):
    rnd = rnd or random
    parts = []
    lean = rnd.uniform(-0.16, 0.16)
    seg = 7
    sections = []
    for i in range(seg + 1):
        u = i / seg
        r = h * (0.055 - 0.028 * u)
        x = math.sin(u * 1.5) * h * 0.16 * (1 if lean >= 0 else -1)
        z = h * 0.86 * u
        ring = []
        for k in range(8):
            a = TAU * k / 8
            ring.append((x + math.cos(a) * r, math.sin(a) * r, z))
        sections.append(ring)
    parts.append(T.loft("PalmTrunk", sections, pal["bark"], smooth=True))
    tipx = math.sin(1.5) * h * 0.16 * (1 if lean >= 0 else -1)
    tipz = h * 0.86
    fronds = 7
    for k in range(fronds):
        a = TAU * k / fronds + rnd.uniform(-0.2, 0.2)
        ln = h * rnd.uniform(0.34, 0.46)
        secs = []
        steps = 6
        for i in range(steps + 1):
            u = i / steps
            rad = ln * u
            droop = -ln * 0.55 * u * u
            w = h * 0.055 * (1.0 - abs(u - 0.4) * 1.1)
            w = max(w, h * 0.006)
            cx = tipx + math.cos(a) * rad
            cy = math.sin(a) * rad
            cz = tipz + h * 0.06 + droop
            nx, ny = -math.sin(a), math.cos(a)
            secs.append([(cx + nx * w, cy + ny * w, cz),
                         (cx + nx * w, cy + ny * w, cz - h * 0.012),
                         (cx - nx * w, cy - ny * w, cz - h * 0.012),
                         (cx - nx * w, cy - ny * w, cz)])
        parts.append(T.loft("Frond", secs, pal["leaf"], smooth=True))
    if rnd.random() < 0.6:
        for k in range(3):
            a = TAU * k / 3
            parts.append(T.uv_sphere("Coco", h * 0.035, 6, 4,
                                     (tipx + math.cos(a) * h * 0.045,
                                      math.sin(a) * h * 0.045, tipz + h * 0.02),
                                     pal["bark"]))
    return T.join(parts, "Palm")


def rock(pal, r=1.2, rnd=None, mat_key="rock"):
    rnd = rnd or random
    ob = T.uv_sphere("Rock", r, 9, 6, (0, 0, r * 0.42), pal[mat_key],
                     scale=(1.0, rnd.uniform(0.7, 1.25), rnd.uniform(0.5, 0.8)))
    me = ob.data
    for v in me.vertices:
        v.co.x *= rnd.uniform(0.82, 1.18)
        v.co.y *= rnd.uniform(0.82, 1.18)
        v.co.z *= rnd.uniform(0.85, 1.15)
    T.shade_flat(ob)
    return ob


def crag(pal, r=2.2, h=4.5, rnd=None, mat_key="rock", seg=7):
    """
    A rock outcrop. On a bare hillside this is what tells the player the
    slope is rock and not a lawn, and beside the road it gives the cutting
    an edge. The base sits below ground so its cap never lands coplanar
    with the terrain (coplanar faces flicker in the engine).
    """
    rnd = rnd or random
    lean = (rnd.uniform(-0.30, 0.30), rnd.uniform(-0.30, 0.30))
    a0 = rnd.uniform(0, TAU)
    rings = []
    for u, k in ((0.0, 1.0), (0.34, 0.94), (0.72, 0.74), (1.0, 0.46)):
        z = -0.9 + (h + 0.9) * u
        ring = []
        for i in range(seg):
            a = a0 + TAU * i / seg
            rr = r * k * rnd.uniform(0.68, 1.32)
            ring.append((math.cos(a) * rr + lean[0] * z,
                         math.sin(a) * rr + lean[1] * z, z))
        rings.append(ring)
    ob = T.loft("Crag", rings, pal[mat_key], smooth=False)
    T.shade_flat(ob)
    return ob


def bush(pal, r=0.9, rnd=None):
    rnd = rnd or random
    parts = []
    for k in range(3):
        a = TAU * k / 3
        parts.append(T.uv_sphere("Bush", r * rnd.uniform(0.6, 0.95), 9, 6,
                                 (math.cos(a) * r * 0.4, math.sin(a) * r * 0.4,
                                  r * 0.5), pal["leaf"],
                                 scale=(1, 1, 0.75)))
    return T.join(parts, "Bush")


# ------------------------------------------------------------------ fencing

def tyre_stack(pal, count=3, r=0.55, rnd=None):
    parts = []
    for k in range(count):
        parts.append(T.torus("Tyre", r, r * 0.34, 14, 7,
                             (0, 0, r * 0.34 + k * r * 0.62), pal["tyre"]))
    return T.join(parts, "TyreStack")


def barrier_block(pal, w=2.4, d=0.7, h=0.85, key_a="barrier_a", key_b="barrier_b",
                  alt=False):
    m = pal[key_b if alt else key_a]
    ob = T.box("Barrier", (w, d, h), (0, 0, h * 0.5), m)
    T.bevel(ob, 0.07, 2)
    T.apply_modifiers(ob)
    return ob


def snow_bank(pal, w=3.2, d=1.6, h=0.9, rnd=None):
    rnd = rnd or random
    parts = []
    for k in range(3):
        parts.append(T.uv_sphere("Bank", h * rnd.uniform(0.8, 1.15), 10, 6,
                                 ((k - 1) * w * 0.34, rnd.uniform(-0.2, 0.2),
                                  h * 0.32), pal["snow"],
                                 scale=(1.5, 1.0, 0.72)))
    return T.join(parts, "SnowBank")


def hay_bale(pal, r=0.62, w=1.1):
    ob = T.cylinder("Hay", r, w, 12, (0, 0, r), 'X', pal["hay"])
    return ob


def cone_marker(pal, h=0.7):
    parts = [T.box("ConeBase", (h * 0.62, h * 0.62, h * 0.09), (0, 0, h * 0.045),
                   pal["cone"]),
             T.cone("ConeTop", h * 0.26, h * 0.92, 10, (0, 0, h * 0.52),
                    pal["cone"], radius_top=h * 0.05)]
    return T.join(parts, "Cone")


# ------------------------------------------------------------------ buildings

def grandstand(pal, w=16.0, rows=6, rnd=None):
    parts = []
    depth = 1.5
    step = 0.75
    for r in range(rows):
        z = r * step
        y = r * depth
        parts.append(T.box("Row", (w, depth, step * 0.55),
                           (0, y, z + step * 0.28), pal["stand"]))
        seatm = pal["seat_a"] if r % 2 == 0 else pal["seat_b"]
        seats = int(w / 1.0)
        for i in range(seats):
            x = (i - (seats - 1) / 2) * (w / seats)
            parts.append(T.box("Seat", (w / seats * 0.72, depth * 0.5,
                                        step * 0.42),
                               (x, y - depth * 0.12, z + step * 0.72), seatm))
    # back wall and roof
    parts.append(T.box("Back", (w, 0.4, rows * step + 1.2),
                       (0, rows * depth, (rows * step + 1.2) * 0.5),
                       pal["stand"]))
    roof = T.box("Roof", (w + 0.5, rows * depth * 0.72, 0.22),
                 (0, rows * depth * 0.72, rows * step + 1.9), pal["roof"])
    parts.append(roof)
    for sx in (-1, 1):
        parts.append(T.box("Post", (0.28, 0.28, rows * step + 1.9),
                           (sx * (w * 0.5 - 0.4), rows * depth * 0.42,
                            (rows * step + 1.9) * 0.5), pal["stand"]))
    return T.join(parts, "Grandstand")


def start_gate(pal, span=18.0, h=6.2, banner="banner"):
    parts = []
    for sx in (-1, 1):
        parts.append(T.box("GatePost", (0.7, 0.7, h),
                           (sx * span * 0.5, 0, h * 0.5), pal["stand"]))
        parts.append(T.box("GateFoot", (1.4, 1.4, 0.35),
                           (sx * span * 0.5, 0, 0.17), pal["stand"]))
    parts.append(T.box("GateBeam", (span + 1.4, 1.0, 1.5),
                       (0, 0, h - 0.75), pal[banner]))
    parts.append(T.box("GateTop", (span + 1.8, 1.2, 0.3),
                       (0, 0, h + 0.15), pal["roof"]))
    return T.join(parts, "StartGate")


def lamp_post(pal, h=5.0):
    parts = [T.cylinder("Pole", 0.10, h, 8, (0, 0, h * 0.5), 'Z', pal["metal"]),
             T.cylinder("Arm", 0.08, 1.1, 8, (0.5, 0, h - 0.1), 'X',
                        pal["metal"]),
             T.uv_sphere("Lamp", 0.26, 10, 6, (1.0, 0, h - 0.22), pal["lamp"],
                         scale=(1, 1, 0.7))]
    return T.join(parts, "Lamp")


def flag_pole(pal, h=4.0, key="banner"):
    parts = [T.cylinder("Pole", 0.06, h, 6, (0, 0, h * 0.5), 'Z', pal["metal"])]
    secs = []
    for i in range(7):
        u = i / 6
        x = u * 1.5
        y = math.sin(u * 3.2) * 0.18
        secs.append([(x, y, h * 0.62), (x, y, h * 0.94),
                     (x, y + 0.03, h * 0.94), (x, y + 0.03, h * 0.62)])
    parts.append(T.loft("Flag", secs, pal[key], smooth=False))
    return T.join(parts, "Flag")


def umbrella(pal, h=2.4, r=1.5):
    parts = [T.cylinder("Pole", 0.05, h, 6, (0, 0, h * 0.5), 'Z', pal["metal"])]
    seg = 8
    for k in range(seg):
        a0 = TAU * k / seg
        a1 = TAU * (k + 1) / seg
        m = pal["umb_a"] if k % 2 == 0 else pal["umb_b"]
        v = [(0, 0, h), (math.cos(a0) * r, math.sin(a0) * r, h - 0.42),
             (math.cos(a1) * r, math.sin(a1) * r, h - 0.42)]
        parts.append(T.mesh_from_pydata("Umb", v, [(0, 1, 2)], m))
    return T.join(parts, "Umbrella")


def snowman(pal, h=2.0):
    parts = [
        T.uv_sphere("S1", h * 0.30, 12, 8, (0, 0, h * 0.28), pal["snow"]),
        T.uv_sphere("S2", h * 0.22, 12, 8, (0, 0, h * 0.66), pal["snow"]),
        T.uv_sphere("S3", h * 0.16, 12, 8, (0, 0, h * 0.94), pal["snow"]),
        T.cone("Nose", h * 0.035, h * 0.16, 8,
               (0, -h * 0.16, h * 0.96), pal["cone"], radius_top=0.004),
        T.cylinder("Hat", h * 0.14, h * 0.16, 10, (0, 0, h * 1.12), 'Z',
                   pal["tyre"]),
        T.cylinder("Brim", h * 0.21, h * 0.03, 10, (0, 0, h * 1.05), 'Z',
                   pal["tyre"]),
    ]
    return T.join(parts, "Snowman")


def buoy(pal, h=1.5):
    parts = [T.cone("Buoy", 0.35, h, 10, (0, 0, h * 0.4), pal["cone"],
                    radius_top=0.06),
             T.uv_sphere("BuoyBase", 0.45, 10, 6, (0, 0, 0.0), pal["banner"],
                         scale=(1, 1, 0.5))]
    return T.join(parts, "Buoy")


def ice_arch(pal, span=16.0, h=7.0):
    parts = []
    steps = 12
    secs = []
    for i in range(steps + 1):
        u = i / steps
        a = math.pi * u
        x = -math.cos(a) * span * 0.5
        z = math.sin(a) * h
        w = 1.1 + 0.5 * math.sin(a)
        # the cross-section must be perpendicular to the arc. A horizontal
        # plate (the original version) lay exactly along the sweep direction
        # at the apex, so the arc came out flat with undefined normals.
        tx, tz = math.sin(a) * span * 0.5, math.cos(a) * h
        tl = math.hypot(tx, tz) or 1.0
        vx, vz = -tz / tl, tx / tl          # normal to the arc in the XZ plane
        th = w * 0.4
        secs.append([(x - vx * th, -w, z - vz * th),
                     (x + vx * th, -w, z + vz * th),
                     (x + vx * th, w, z + vz * th),
                     (x - vx * th, w, z - vz * th)])
    parts.append(T.loft("Arch", secs, pal["ice"], smooth=True))
    for i in range(6):
        u = 0.18 + 0.64 * (i / 5)
        a = math.pi * u
        x = -math.cos(a) * span * 0.5
        z = math.sin(a) * h
        parts.append(T.cone("Icicle", 0.22, 1.1 + (i % 3) * 0.5, 7,
                            (x, 0, z - 0.9), pal["ice"], radius_top=0.02))
    return T.join(parts, "IceArch")


def boardwalk_post(pal, h=1.6):
    return T.cylinder("Post", 0.16, h, 8, (0, 0, h * 0.5), 'Z', pal["wood"])


def tuft(pal, h=0.55, rnd=None, key="leaf"):
    """A tuft of grass - a cheap detail that fills the space beside the track."""
    rnd = rnd or random
    parts = []
    blades = rnd.randint(3, 5)
    for k in range(blades):
        a = TAU * k / blades + rnd.uniform(-0.3, 0.3)
        lean = rnd.uniform(0.1, 0.32)
        hh = h * rnd.uniform(0.7, 1.25)
        v = [(math.cos(a) * 0.05, math.sin(a) * 0.05, 0.0),
             (math.cos(a + 1.9) * 0.05, math.sin(a + 1.9) * 0.05, 0.0),
             (math.cos(a) * lean * hh, math.sin(a) * lean * hh, hh)]
        parts.append(T.mesh_from_pydata("Blade", v, [(0, 1, 2)], pal[key]))
    return T.join(parts, "Tuft")


def flower(pal, h=0.4, rnd=None, key="banner"):
    rnd = rnd or random
    parts = [T.cylinder("Stem", 0.025, h, 5, (0, 0, h * 0.5), 'Z', pal["leaf"]),
             T.uv_sphere("Bloom", 0.10, 8, 5, (0, 0, h), pal[key],
                         scale=(1, 1, 0.55))]
    return T.join(parts, "Flower")


def snow_mound(pal, r=0.7, rnd=None):
    rnd = rnd or random
    return T.uv_sphere("Mound", r, 10, 6, (0, 0, r * 0.25), pal["snow"],
                       scale=(1.5, rnd.uniform(0.8, 1.3), 0.45))


def hut(pal, w=6.0, d=5.0, h=3.2, wall="stand", roof="roof", rnd=None):
    """A simple building - used as the pit garage, a beach bar and a mountain hut."""
    rnd = rnd or random
    parts = []
    body = T.box("Hut", (w, d, h), (0, 0, h * 0.5), pal[wall])
    T.bevel(body, 0.08, 2)
    T.apply_modifiers(body)
    parts.append(body)
    # gabled roof
    secs = []
    for i in range(5):
        u = i / 4
        y = -d * 0.62 + d * 1.24 * u
        secs.append([(-w * 0.62, y, h), (0, y, h + h * 0.34),
                     (w * 0.62, y, h), (w * 0.62, y, h - 0.16),
                     (0, y, h + h * 0.34 - 0.16), (-w * 0.62, y, h - 0.16)])
    parts.append(T.loft("HutRoof", secs, pal[roof], smooth=False))
    # doors and windows
    parts.append(T.box("Door", (w * 0.22, 0.12, h * 0.55),
                       (0, -d * 0.5 - 0.02, h * 0.28), pal["tyre"]))
    for sx in (-1, 1):
        parts.append(T.box("Win", (w * 0.20, 0.12, h * 0.26),
                           (sx * w * 0.28, -d * 0.5 - 0.02, h * 0.6),
                           pal["window"]))
    return T.join(parts, "Hut")


def tower(pal, h=6.0, rnd=None):
    """Marshal / lifeguard tower - a strong landmark beside the track."""
    parts = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            parts.append(T.box("Leg", (0.22, 0.22, h),
                               (sx * 1.1, sy * 1.1, h * 0.5), pal["wood"]))
    parts.append(T.box("Deck", (3.2, 3.2, 0.22), (0, 0, h), pal["wood"]))
    parts.append(T.box("Cab", (2.6, 2.6, 1.6), (0, 0, h + 0.9), pal["stand"]))
    parts.append(T.box("CabWin", (2.4, 0.1, 0.8),
                       (0, -1.3, h + 1.1), pal["window"]))
    secs = []
    for i in range(3):
        u = i / 2
        y = -1.7 + 3.4 * u
        secs.append([(-1.9, y, h + 1.7), (0, y, h + 2.5), (1.9, y, h + 1.7),
                     (1.9, y, h + 1.55), (0, y, h + 2.35), (-1.9, y, h + 1.55)])
    parts.append(T.loft("TowerRoof", secs, pal["roof"], smooth=False))
    for k in range(6):
        parts.append(T.box("Step", (1.2, 0.3, 0.12),
                           (1.5, -1.4 + k * 0.5, h * (k + 1) / 7.0),
                           pal["wood"]))
    return T.join(parts, "Tower")


def water_tower(pal, h=8.0):
    parts = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            parts.append(T.cylinder("Leg", 0.16, h, 6,
                                    (sx * 1.2, sy * 1.2, h * 0.5), 'Z',
                                    pal["metal"]))
    parts.append(T.cylinder("Tank", 2.1, 2.6, 14, (0, 0, h + 1.3), 'Z',
                            pal["banner"]))
    parts.append(T.cone("TankTop", 2.2, 1.0, 14, (0, 0, h + 3.0),
                        pal["roof"], radius_top=0.1))
    return T.join(parts, "WaterTower")


def pickup_canister(pal):
    """A fuel canister - a collectible item."""
    parts = []
    body = T.box("Can", (0.62, 0.40, 0.78), (0, 0, 0.39), pal["can"])
    T.bevel(body, 0.07, 3)
    T.apply_modifiers(body)
    parts.append(body)
    parts.append(T.cylinder("Neck", 0.11, 0.18, 10, (0.14, 0, 0.86), 'Z',
                            pal["can_dark"]))
    parts.append(T.box("Handle", (0.34, 0.07, 0.07), (-0.06, 0, 0.86),
                       pal["can_dark"]))
    for sx in (-1, 1):
        parts.append(T.box("HandleLeg", (0.07, 0.07, 0.10),
                           (-0.06 + sx * 0.14, 0, 0.80), pal["can_dark"]))
    parts.append(T.box("Stripe", (0.40, 0.42, 0.13), (0, 0, 0.50),
                       pal["can_light"]))
    return T.join(parts, "Canister")
